/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

#ifndef __REPORT_ATTRRS__
#define __REPORT_ATTRRS__

enum KnvAttrs
{
	ATTR_KNV_CREATE_POOL_FAIL = 395517,
	ATTR_MEM_POOL_NEW_OBJ_FAIL = 380762,
	ATTR_MEM_POOL_NEW_OBJ = 380763,
	ATTR_MEM_POOL_LIMIT_REACHED = 380764,
	ATTR_MEM_POOL_MALLOC_FAIL = 380765,
	ATTR_MEM_POOL_BUG_SZ_FREE_BAD1 = 380766,
	ATTR_MEM_POOL_BUG_SZ_TOTAL_BAD = 380767,
	ATTR_MEM_POOL_SHRINK_SPACE = 380768,
	ATTR_MEM_POOL_BUG_SZ_FREE_BAD2 = 380769,
	ATTR_MEM_POOL_BUG_SZ_FREE_BAD3 = 380770,
	ATTR_MEM_POOL_BUG_SZ_FREE_BAD4 = 380771,
	ATTR_MEM_POOL_BUG_SZ_MAX_BAD = 380772,
	ATTR_MEM_POOL_ALLOC_SZ_EXCEED_LIMIT = 380773,
	ATTR_MEM_POOL_GOT_SPACE_BY_SHRINKING_LARGER_POOL = 380774,
	ATTR_MEM_POOL_SHRINKING_SMALLER_POOL = 380775,
	ATTR_MEM_POOL_ALLOC_DIRECTLY = 380776,
	ATTR_MEM_POOL_TRY_ALLOC_AFTER_SHRINK = 380777,
	ATTR_MEM_POOL_EXCEED_LIMIT_AFTER_SHRINK = 380778,
	ATTR_MEM_POOL_SUCC_AFTER_SHRINK = 380779,
	ATTR_MEM_POOL_NO_SPACE_SHRUNK = 380780,
	ATTR_OBJ_POOL_NEW_OBJ = 390992,
	ATTR_OBJ_POOL_NEW_OBJ_FAIL = 391020,
	ATTR_PROTO_INCOMPLETE_PART_OVERWRITTEN = 391963,
	ATTR_PROTO_REAL_SIZE_SMALLER_THAN_EVAL_SIZE = 392288,
	ATTR_PROTO_PKG_NUM_SMALLER_THAN_EVAL_NUM = 392289
};

#ifdef HAS_ATTR_API
extern "C" int Attr_API(unsigned int attr, unsigned int val);
#else
#define Attr_API(attr, val)
#endif


#endif

