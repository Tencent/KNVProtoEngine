/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

/* pb.h
 * Low-level protobuf handling routines intended to directly access pb streams without .proto file
 *
 * Created 2013-09-11
 */

#include "pb.h"

#define inline	__attribute__((always_inline))

#define RETURN_WITH_ERROR(field,ret,msg) \
    do {\
        if ((field)->errmsg == NULL) \
            (field)->errmsg = (msg); \
        return ret; \
    } while(0)


#if 0
#include <stdio.h>
#define DBGOUT(fmt, arg ...)	printf(fmt, # arg)
#else
#define DBGOUT(fmt, ...)	
#endif


static inline int pb_get(pb_field_t *f, uint8_t *buf, size_t count)
{
	if (f->left < count)
		RETURN_WITH_ERROR(f, -1, "end of buffer");

	if(buf) memcpy(buf, f->ptr, count);
	f->ptr += count;
	f->left -= count;

	return 0;
}

static inline int pb_get_byte(pb_field_t *f, uint8_t *buf)
{
	if (f->left < 1)
		RETURN_WITH_ERROR(f, -1, "end of buffer");

	if(buf) *buf = *(uint8_t*)f->ptr;
	f->ptr += 1;
	f->left -= 1;
	return 0;
}

static inline int pb_get_dword(pb_field_t *f, uint32_t *buf)
{
	if (f->left < 4)
		RETURN_WITH_ERROR(f, -1, "end of buffer");

	if(buf) *buf = *(uint32_t*)f->ptr;
	f->ptr += 4;
	f->left -= 4;
	return 0;
}

static inline int pb_get_ddword(pb_field_t *f, uint64_t *buf)
{
	if (f->left < 8)
		RETURN_WITH_ERROR(f, -1, "end of buffer");

	if(buf) *buf = *(uint64_t*)f->ptr;
	f->ptr += 8;
	f->left -= 8;
	return 0;
}

static inline int pb_decode_varint(pb_field_t *f)
{
	uint8_t byte;
	uint8_t bitpos = 0;
	uint64_t result = 0;

	do
	{
		if (bitpos >= 64)
			RETURN_WITH_ERROR(f, -1, "varint overflow");

		if(pb_get_byte(f, &byte))
			return -2;

		result |= (uint64_t)(byte & 0x7F) << bitpos;
		bitpos = (uint8_t)(bitpos + 7);
	} while (byte & 0x80);

	f->val.i64 = result;
	return 0;
}

static inline int pb_decode_fixed32(pb_field_t *f)
{
	return pb_get_dword(f, &f->val.i32);
}

static inline int pb_decode_fixed64(pb_field_t *f)
{
	return pb_get_ddword(f, &f->val.i64);
}

static inline int pb_decode_string(pb_field_t *f)
{
	uint32_t sz;
	if (pb_decode_varint(f))
		return -1;

	if (f->val.i64 > f->left)
		RETURN_WITH_ERROR(f, -2, "string overflow");

	sz = f->val.i64;
	f->val.str.len = sz;
	f->val.str.data = (char*)f->ptr;
	f->ptr += sz;
	f->left -= sz;
	return 0;
}


static inline int pb_skip_varint(pb_field_t *f)
{
	uint8_t byte;
	do
	{
		if(pb_get_byte(f, &byte))
			return 0;
	} while (byte & 0x80);
	return 0;
}

static inline int pb_skip_string(pb_field_t *f)
{
	if (pb_decode_varint(f))
		return -1;

	return pb_get(f, NULL, f->val.i32);
}

static inline int pb_decode_tag(pb_field_t *f)
{
	if (pb_decode_varint(f))
	{
		if(f->left == 0)
			f->eom = 1; // signal end-of-message
		return -1;
	}

	if (f->val.i64 == 0)
	{
		f->eom = 1; // Allow 0-terminated messages.
		RETURN_WITH_ERROR(f, -2, "0-terminated msg");
	}

	f->tag = f->val.i64 >> 3;
	f->type = (pb_type_t)(f->val.i32 & 7);

	DBGOUT("tag %u, type %u\n", f->tag, f->type);
	switch (f->type)
	{
		case PB_TYPE_VARINT: return pb_decode_varint(f);
		case PB_TYPE_FIXED64: return pb_decode_fixed64(f);
		case PB_TYPE_STRING: return pb_decode_string(f);
		case PB_TYPE_FIXED32: return pb_decode_fixed32(f);
		default: RETURN_WITH_ERROR(f, -3, "invalid type");
	}

	return 0;
}

int pb_skip_field(pb_field_t *f, pb_type_t type)
{
	switch (type)
	{
		case PB_TYPE_VARINT: return pb_skip_varint(f);
		case PB_TYPE_FIXED64: return pb_get_ddword(f, NULL);
		case PB_TYPE_STRING: return pb_skip_string(f);
		case PB_TYPE_FIXED32: return pb_get_dword(f, NULL);
		default: RETURN_WITH_ERROR(f, -1, "invalid type");
	}
}

pb_field_t *pb_begin(pb_field_t *f, const void *data, int dlen)
{
	f->start = f->ptr = (char *)data;
	f->size = f->left = dlen;
	f->eom = 0;
	f->errmsg = NULL;

	if(pb_decode_tag(f))
		return NULL;
	return f;
}



pb_field_t *pb_begin_delimited(pb_field_t *f, const void *data, int dlen)
{
	f->start = f->ptr = (char *)data;
	f->size = f->left = dlen;
	f->eom = 0;

	if (pb_decode_varint(f))
		return NULL;
	if(f->val.i64 > f->left)
		RETURN_WITH_ERROR(f, NULL, "delimited overflow");
	f->left = f->val.i64;

	if(pb_decode_tag(f))
		return NULL;
	return f;
}

pb_field_t *pb_next(pb_field_t *f)
{
	if(pb_decode_tag(f))
		return NULL;
	return f;
}

static inline int pb_add(pb_buff_t *b, const void *buf, size_t count)
{
	if(b->left < count)
		RETURN_WITH_ERROR(b, -1, "buf overflow");

	DBGOUT("add(len=%u):", count);
	//int i; 
	//for(i=0; i<count; i++) DBGOUT(" %02x", ((uint8_t*)buf)[i]);
	DBGOUT("\n");
	memcpy(b->ptr, buf, count);
	b->ptr += count;
	b->left -= count;

	return 0;
}

int pb_add_user(pb_buff_t *b, const void *buf, size_t count)
{
	DBGOUT("add_user:");
	return pb_add(b, buf, count);
}
/*
static inline int pb_add_byte(pb_buff_t *b, const uint8_t *buf)
{
	if(b->left < 1)
		RETURN_WITH_ERROR(b, -1, "buf overflow");

	*(uint8_t*)b->ptr = *buf;
	b->ptr += 1;
	b->left -= 1;

	DBGOUT("add_byte: %02x\n", *buf);
	return 0;
}
*/
static inline int pb_add_dword(pb_buff_t *b, uint32_t val)
{
	if(b->left < 4)
		RETURN_WITH_ERROR(b, -1, "buf overflow");

	*(uint32_t*)b->ptr = val;
	DBGOUT("add_dword: %02x %02x %02x %02x\n", b->ptr[0], b->ptr[1], b->ptr[2], b->ptr[3]);
	b->ptr += 4;
	b->left -= 4;

	return 0;
}

static inline int pb_add_ddword(pb_buff_t *b, uint64_t val)
{
	if(b->left < 8)
		RETURN_WITH_ERROR(b, -1, "buf overflow");

	*(uint64_t*)b->ptr = val;
	DBGOUT("add_ddword: %02x %02x %02x %02x %02x %02x %02x %02x\n", b->ptr[0], b->ptr[1], b->ptr[2], b->ptr[3], b->ptr[4], b->ptr[5], b->ptr[6], b->ptr[7]);
	b->ptr += 8;
	b->left -= 8;

	return 0;
}

static inline int pb_add_vint(pb_buff_t *b, uint64_t value)
{
	uint8_t buffer[10];
	size_t i = 0;

	DBGOUT("add_vi:");
	if (value == 0)
		return pb_add(b, (uint8_t*)&value, 1);

	while (value)
	{
		buffer[i] = (uint8_t)((value & 0x7F) | 0x80);
		value >>= 7;
		i++;
	}
	buffer[i-1] &= 0x7F; /* Unset top bit on last byte */

	return pb_add(b, buffer, i);
}


static inline int pb_add_tag(pb_buff_t *b, pb_type_t wiretype, uint32_t field_number)
{
	uint64_t tag = wiretype | (field_number << 3);
	DBGOUT("add_tag(%u):", field_number);
	return pb_add_vint(b, tag);
}

int pb_add_field_val(pb_buff_t *b, uint32_t tag, pb_type_t type, const pb_field_val_t *val)
{
	DBGOUT("add_field_val:");
	int ret = pb_add_tag(b, type, tag);
	if(ret)
		return -1;
	switch (type)
	{
		case PB_TYPE_VARINT: return pb_add_vint(b, val->i64);
		case PB_TYPE_FIXED64: return pb_add_ddword(b, val->i64);
		case PB_TYPE_STRING: if(pb_add_vint(b, val->str.len)) return -2; return pb_add(b, val->str.data, val->str.len);
		case PB_TYPE_FIXED32: return pb_add_dword(b, val->i32);
		default: RETURN_WITH_ERROR(b, -2, "invalid wire_type");
	}
	return 0;
}

int pb_add_field(pb_buff_t *b, const pb_field_t *f)
{
	DBGOUT("add_field:");
	return pb_add_field_val(b, f->tag, f->type, &f->val);
}

int pb_add_varint(pb_buff_t *b, uint32_t tag, uint64_t value)
{
	DBGOUT("add_vint:");
	int ret = pb_add_tag(b, PB_TYPE_VARINT, tag);
	if(ret)
		return -1;
	return pb_add_vint(b, value);
}

int pb_add_string(pb_buff_t *b, uint32_t tag, const void *buffer, size_t size)
{
	DBGOUT("add_string:");
	int ret = pb_add_tag(b, PB_TYPE_STRING, tag);
	if(ret)
		return -1;
	ret = pb_add_vint(b, size);
	if(ret)
		return -2;
	return pb_add(b, buffer, size);
}

int pb_add_string_head(pb_buff_t *b, uint32_t tag, size_t size)
{
	DBGOUT("add_string_head:");
	int ret = pb_add_tag(b, PB_TYPE_STRING, tag);
	if(ret)
		return -1;
	return pb_add_vint(b, size);
}


int pb_add_fixed32(pb_buff_t *b, uint32_t tag, uint32_t value)
{
	DBGOUT("add_int32:");
	int ret = pb_add_tag(b, PB_TYPE_FIXED32, tag);
	if(ret)
		return -1;
	return pb_add_dword(b, value);
}

int pb_add_fixed64(pb_buff_t *b, uint32_t tag, uint64_t value)
{
	DBGOUT("add_int64:");
	int ret = pb_add_tag(b, PB_TYPE_FIXED64, tag);
	if(ret)
		return -1;
	return pb_add_ddword(b, value);
}

