/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

/* knv_codec.h
 * Key-N-Value data encode/decode interface
 *
 * Created 2013-10-10
 */

#ifndef _KNV_CODEC_H_
#define _KNV_CODEC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef USE_EXTERNAL_CODEC

// Basic types
typedef enum {
	KNV_VARINT = 0,
	KNV_FIXED64  = 1,
	KNV_STRING = 2,
	KNV_FIXED32  = 5
} knv_type_t;


typedef union {
	uint32_t i32;
	uint64_t i64;
	struct
	{
		uint32_t len;
		char *data;
	} str;
} knv_field_val_t;

typedef struct 
{
	uint32_t tag;
	char eom; // set to 1 if end-of-message
	knv_type_t type;
	knv_field_val_t val;

	// msg start
	const char *start;
	uint32_t size;

	const char *ptr; // cur unhandled position
	int left; // unhandled length

	const char *errmsg;
}knv_field_t;


typedef struct 
{
	char *start; // buffer start
	size_t size; // buffer size

	char *ptr; // cur unwritten pointer
	int left; // unwritten length

	const char *errmsg;
}knv_buff_t;


typedef struct
{
	// functions for iteration through knv fields
	knv_field_t *(*knv_begin)(knv_field_t *f, const void *data, int dlen);
	knv_field_t *(*knv_begin_delimited)(knv_field_t *f, const void *data, int dlen);
	knv_field_t *(*knv_next)(knv_field_t *f);

	int (*knv_init_buff)(knv_buff_t *b, void *buf, size_t sz);
	int (*knv_add_field)(knv_buff_t *b, const knv_field_t *field);
	int (*knv_add_field_val)(knv_buff_t *b, uint32_t tag, knv_type_t type, const knv_field_val_t *val);
	int (*knv_add_varint)(knv_buff_t *b, uint32_t tag, uint64_t value);
	int (*knv_add_string)(knv_buff_t *b, uint32_t tag, const void *buffer, size_t size);
	int (*knv_add_string_head)(knv_buff_t *b, uint32_t tag, size_t size);
	int (*knv_add_fixed32)(knv_buff_t *b, uint32_t tag, uint32_t value);
	int (*knv_add_fixed64)(knv_buff_t *b, uint32_t tag, uint64_t value);
	int (*knv_add_user)(knv_buff_t *b, const void *buf, size_t count); // add user buffer
	int (*knv_get_encoded_length)(knv_buff_t *b);
	int (*knv_eval_field_length)(uint32_t tag, knv_type_t type, const knv_field_val_t *val);

} knv_codecs_t;

// the main thread is responsible for setting up correct codecs for us
extern knv_codecs_t *g_knv_codecs;

#else

#include "pb.h"

typedef pb_type_t knv_type_t;
typedef pb_field_val_t knv_field_val_t;
typedef pb_field_t knv_field_t;
typedef pb_buff_t knv_buff_t;

#define KNV_VARINT  PB_TYPE_VARINT
#define KNV_FIXED64 PB_TYPE_FIXED64
#define KNV_STRING  PB_TYPE_STRING
#define KNV_FIXED32 PB_TYPE_FIXED32

#endif

static inline knv_field_t *knv_begin(knv_field_t *f, const void *data, int dlen)
{
#ifdef USE_EXTERNAL_CODEC
	if(g_knv_codecs)
		return g_knv_codecs->knv_begin(f, data, dlen);
#endif
	return pb_begin(f, data, dlen);
}

static inline knv_field_t *knv_begin_delimited(knv_field_t *f, const void *data, int dlen)
{
#ifdef USE_EXTERNAL_CODEC
	if(g_knv_codecs)
		return g_knv_codecs->knv_begin_delimited(f, data, dlen);
#endif
	return pb_begin_delimited(f, data, dlen);
}

static inline knv_field_t *knv_next(knv_field_t *f)
{
#ifdef USE_EXTERNAL_CODEC
	if(g_knv_codecs)
		return g_knv_codecs->knv_next(f);
#endif
	return pb_next(f);
}

static inline int knv_init_buff(knv_buff_t *b, void *buf, size_t sz)
{
#ifdef USE_EXTERNAL_CODEC
	if(g_knv_codecs)
		return g_knv_codecs->knv_init_buff(b, buf, sz);
#endif
	return pb_init_buff(b, buf, sz);
}

static inline int knv_add_field(knv_buff_t *b, const knv_field_t *field)
{
#ifdef USE_EXTERNAL_CODEC
	if(g_knv_codecs)
		return g_knv_codecs->knv_add_field(b, field);
#endif
	return pb_add_field(b, field);
}

static inline int knv_add_field_val(knv_buff_t *b, uint32_t tag, knv_type_t type, const knv_field_val_t *val)
{
#ifdef USE_EXTERNAL_CODEC
	if(g_knv_codecs)
		return g_knv_codecs->knv_add_field_val(b, tag, type, val);
#endif
	return pb_add_field_val(b, tag, type, val);
}

static inline int knv_add_varint(knv_buff_t *b, uint32_t tag, uint64_t value)
{
#ifdef USE_EXTERNAL_CODEC
	if(g_knv_codecs)
		return g_knv_codecs->knv_add_varint(b, tag, value);
#endif
	return pb_add_varint(b, tag, value);
}

static inline int knv_add_string(knv_buff_t *b, uint32_t tag, const void *buffer, size_t size)
{
#ifdef USE_EXTERNAL_CODEC
	if(g_knv_codecs)
		return g_knv_codecs->knv_add_string(b, tag, buffer, size);
#endif
	return pb_add_string(b, tag, buffer, size);
}

static inline int knv_add_string_head(knv_buff_t *b, uint32_t tag, size_t size)
{
#ifdef USE_EXTERNAL_CODEC
	if(g_knv_codecs)
		return g_knv_codecs->knv_add_string_head(b, tag, size);
#endif
	return pb_add_string_head(b, tag, size);
}

static inline int knv_add_fixed32(knv_buff_t *b, uint32_t tag, uint32_t value)
{
#ifdef USE_EXTERNAL_CODEC
	if(g_knv_codecs)
		return g_knv_codecs->knv_add_fixed32(b, tag, value);
#endif
	return pb_add_fixed32(b, tag, value);
}

static inline int knv_add_fixed64(knv_buff_t *b, uint32_t tag, uint64_t value)
{
#ifdef USE_EXTERNAL_CODEC
	if(g_knv_codecs)
		return g_knv_codecs->knv_add_fixed64(b, tag, value);
#endif
	return pb_add_fixed64(b, tag, value);
}

static inline int knv_add_user(knv_buff_t *b, const void *buf, size_t count)
{
#ifdef USE_EXTERNAL_CODEC
	if(g_knv_codecs)
		return g_knv_codecs->knv_add_user(b, buf, count);
#endif
	return pb_add_user(b, buf, count);
}

static inline int knv_get_encoded_length(knv_buff_t *b)
{
#ifdef USE_EXTERNAL_CODEC
	if(g_knv_codecs)
		return g_knv_codecs->knv_get_encoded_length(b);
#endif
	return pb_get_encoded_length(b);
}

static inline int knv_eval_field_length(uint32_t tag, knv_type_t type, const knv_field_val_t *val)
{
#ifdef USE_EXTERNAL_CODEC
	if(g_knv_codecs)
		return g_knv_codecs->knv_eval_field_length(tag, type, val);
#endif
	return pb_eval_field_length(tag, type, val);
}

#define KNV_GET_ERROR(p) ((p)->errmsg ? (p)->errmsg : "(none)")

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
