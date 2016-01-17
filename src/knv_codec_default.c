/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

/* knv_codec_default.h
 * Key-N-Value data encode/decode default implimentation
 *
 * use our own libpb for Google's Protocol Buffers
 *
 * Created 2013-10-10
 */

#ifdef USE_EXTERNAL_CODEC

#include "pb.h"
#include "knv_codec.h"

static int my_pb_init_buff(pb_buff_t *b, void *buf, size_t sz)
{
    return pb_init_buff(b, buf, sz);
}

int my_pb_get_encoded_length(pb_buff_t *b)
{
    return pb_get_encoded_length(b);
}

int my_pb_eval_field_length(uint32_t tag, knv_type_t type, const knv_field_val_t *val)
{
	return pb_eval_field_length(tag, (pb_wire_type_t)type, (const pb_field_val_t *)val);
}

static knv_codecs_t codecs = // typeof convertion is a trick to suppress compiler's warning
{
    // decoders
    .knv_begin                = (typeof(codecs.knv_begin))              pb_begin,
    .knv_begin_delimited      = (typeof(codecs.knv_begin_delimited))    pb_begin_delimited,
    .knv_next                 = (typeof(codecs.knv_next))               pb_next,

    // encoders
    .knv_init_buff            = (typeof(codecs.knv_init_buff))          my_pb_init_buff,
    .knv_add_field            = (typeof(codecs.knv_add_field))          pb_add_field,
    .knv_add_field_val        = (typeof(codecs.knv_add_field_val))      pb_add_field_val,
    .knv_add_varint           = (typeof(codecs.knv_add_varint))         pb_add_varint,
    .knv_add_string           = (typeof(codecs.knv_add_string))         pb_add_string,
    .knv_add_string_head      = (typeof(codecs.knv_add_string_head))    pb_add_string_head,
    .knv_add_fixed32          = (typeof(codecs.knv_add_fixed32))        pb_add_fixed32,
    .knv_add_fixed64          = (typeof(codecs.knv_add_fixed64))        pb_add_fixed64,
    .knv_add_user             = (typeof(codecs.knv_add_user))           pb_add_user,
    .knv_get_encoded_length   = (typeof(codecs.knv_get_encoded_length)) my_pb_get_encoded_length,
	.knv_eval_field_length    = (typeof(codecs.knv_eval_field_length))  my_pb_eval_field_length
};

knv_codecs_t *g_knv_codecs = &codecs;

#endif

