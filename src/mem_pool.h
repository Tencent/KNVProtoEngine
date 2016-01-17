/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

// mem_pool.h
// A memory buffer pool based on ObjPool
//
// 2014-1-15	Created
//

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <map>
#include <string>

#include "obj_pool.h"

#ifndef __UC_MEM_POOL__
#define __UC_MEM_POOL__

using namespace std;

class UcMemPool;
class UcMemManager;

class UcMem : public ObjBase
{
public:
	UcMem() : ObjBase(), mem(NULL), pool(NULL)
	{
	}
	UcMem(uint64_t sz) : ObjBase(), pool(NULL)
	{
		mem = malloc(sz);
	}

	virtual ~UcMem()
	{
		if(mem) free(mem);
	}

	virtual void ReleaseObject()
	{
		// nothing to be released
	}

	operator void *() { return (void *)mem; }
	void *ptr() { return (void *)mem; }

	uint64_t GetAllocSize();

private:
	void *mem;

	friend class UcMemPool;
	friend class UcMemManager;
	UcMemPool *pool;
};

//
// the mem pool
//
class UcMemPool
{
public:
	UcMemPool() : sz_each(0), sz_total(0), sz_free(0), sz_max(0), pool(){ }
	~UcMemPool() {}

	UcMem *Alloc(bool &exceed_limit);
	void Free(UcMem *m);

	uint64_t Shrink(); // shrink memory for other pool, return the shrinked size
	uint64_t GetMemSize() { return sz_each; }
private:
	uint64_t sz_each;  // size of each mem
	uint64_t sz_total; // current total size
	uint64_t sz_free;  // current free size
	uint64_t sz_max;   // max total size
	ObjPool<UcMem> pool;

friend class UcMemManager;
};

inline uint64_t UcMem::GetAllocSize()
{
	return pool? pool->GetMemSize() : 0;
}

class UcMemManager
{
public:
	static UcMem *Alloc(uint64_t sz);
	static void Free(UcMem *m);

	static void SetMaxSize(uint64_t sz) { GetInstance()->sz_max = sz; }

private:
	static UcMemManager *GetInstance();
	UcMemManager(uint64_t max_sz):sz_max(max_sz) {}
	UcMemPool *GetPool(int magic);
	uint64_t Shrink(int magic);

	uint64_t sz_max;
};


#endif
