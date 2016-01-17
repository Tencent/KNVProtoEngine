/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

#include <iostream>
#include "mem_pool.h"

using namespace std;


int test_mempool()
{
	UcMemManager::SetMaxSize(39*1024*1024);
	UcMem *m, *ma[100];
	int i, j;

	for(j=0; j<100000; j++)
	{
		for(i=0; i<4; i++)
		{
			ma[i] = UcMemManager::Alloc(1024*1025);
			if(ma[i]==NULL)
			{
				cout << "palloc 1024*1025 ["<<i<<"] failed" << endl;
				return -1;
			}
			if(ma[i]->GetAllocSize() != 4194304)
			{
				cout << "alloc return mismatch size 4194304 <-> " << ma[i]->GetAllocSize() << endl;
			}
		}
		for(i=0; i<100; i++)
		{
			m = UcMemManager::Alloc(48);
			if(m==NULL)
			{
				cout << "alloc 32 failed" << endl;
				return -1;
			}
			if(i<4) UcMemManager::Free(ma[i]);
			ma[i] = m;
		}
		m = UcMemManager::Alloc(289340);
		if(m==NULL)
		{
			cout << "alloc 289340 failed" << endl;
			return -1;
		}
		if(m->GetAllocSize() != 1048576)
		{
			cout << "alloc return mismatch size 1048576 <-> " << m->GetAllocSize() << endl;
		}
		UcMemManager::Free(m);
		for(i=0; i<100; i++)
		{
			m = UcMemManager::Alloc(1024+10*i);
			if(m==NULL)
			{
				cout << "alloc "<<1024+10*i<<" failed" << endl;
				return -1;
			}
			UcMemManager::Free(ma[i]);
			ma[i] = m;
		}
		m = UcMemManager::Alloc(5200000);
		i = 99;
		if(m==NULL)
		{
			cout << "alloc 5200000 failed" << endl;
			for(;i>=90; i--)
				UcMemManager::Free(ma[i]);
			m = UcMemManager::Alloc(5200000);
			if(m==NULL)
			{
				cout << "alloc 5200000 failed after freeing ma[90-99]" << endl;
				return -1;
			}
		}
		UcMemManager::Free(m);
		for(; i>=0; i--)
			UcMemManager::Free(ma[i]);
	}
	return 0;
}

int test_clib()
{
	void *m, *ma[100];
	int i, j;

	for(j=0; j<100000; j++)
	{
		for(i=0; i<4; i++)
		{
			ma[i] = malloc(1024*1025);
			if(ma[i]==NULL)
			{
				cout << "palloc 1024*1025 ["<<i<<"] failed" << endl;
				return -1;
			}
		}
		for(i=0; i<100; i++)
		{
			m = malloc(48);
			if(m==NULL)
			{
				cout << "alloc 32 failed" << endl;
				return -1;
			}
			if(i<4) free(ma[i]);
			ma[i] = m;
		}
		m = malloc(289340);
		if(m==NULL)
		{
			cout << "alloc 289340 failed" << endl;
			return -1;
		}
		free(m);
		for(i=0; i<100; i++)
		{
			m = malloc(1024+10*i);
			if(m==NULL)
			{
				cout << "alloc "<<1024+10*i<<" failed" << endl;
				return -1;
			}
			free(ma[i]);
			ma[i] = m;
		}
		m = malloc(5200000);
		if(m==NULL)
		{
			cout << "alloc 5200000 failed" << endl;
			for(i=99; i>=90; i--)
				free(ma[i]);
			m = malloc(5200000);
			if(m==NULL)
			{
				cout << "alloc 5200000 failed after freeing ma[90-99]" << endl;
				return -1;
			}
		}
		free(m);
		for(i=0; i<100; i++)
			free(ma[i]);
	}
	return 0;
}

int test_cpplib()
{
	char *m, *ma[100];
	int i, j;

	for(j=0; j<100000; j++)
	{
		for(i=0; i<4; i++)
		{
			ma[i] = new char[1024*1025];
			if(ma[i]==NULL)
			{
				cout << "palloc 1024*1025 ["<<i<<"] failed" << endl;
				return -1;
			}
		}
		for(i=0; i<100; i++)
		{
			m = new char[48];
			if(m==NULL)
			{
				cout << "alloc 32 failed" << endl;
				return -1;
			}
			if(i<4) delete[] ma[i];
			ma[i] = m;
		}
		m = new char [289340];
		if(m==NULL)
		{
			cout << "alloc 289340 failed" << endl;
			return -1;
		}
		delete [] m;
		for(i=0; i<100; i++)
		{
			m = new char [1024+10*i];
			if(m==NULL)
			{
				cout << "alloc "<<1024+10*i<<" failed" << endl;
				return -1;
			}
			delete [] ma[i];
			ma[i] = m;
		}
		m = new char [5200000];
		if(m==NULL)
		{
			cout << "alloc 5200000 failed" << endl;
			for(i=99; i>=90; i--)
				delete[] ma[i];
			m = new char [5200000];
			if(m==NULL)
			{
				cout << "alloc 5200000 failed after freeing ma[90-99]" << endl;
				return -1;
			}
		}
		delete [] m;
		for(i=0; i<100; i++)
			delete[] ma[i];
	}
	return 0;
}

int main(int argc, char *argv[])
{
	if(argc!=2)
	{
		cout << "usage: " << argv[0] << " [pool|libc|cpp]  -- pool: use mempool method; libc: use libc method; cpp: use new/delete" << endl;
		return -1;
	}
	if(strcmp(argv[1],"pool")==0)
		cout << "test_mempool() returns " << test_mempool() << endl;
	else if(strcmp(argv[1],"libc")==0)
		cout << "test_clib() returns " << test_clib() << endl;
	else
		cout << "test_cpp() returns " << test_cpplib() << endl;

	return 0;
}
