/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

// obj_pool.h
// A general-purpose object pool
//
// 2013-08-28	Created
// 2013-10-12	Use pointer in set<> instead of object
// 2014-02-04   Use pointer to replace iterator
// 2014-03-15   Remove the use of vector
//
#include <stdint.h>
#include <string.h>

#include "obj_base.h"
#include "report_attr.h"

#ifndef __UC_OBJ_POOL__
#define __UC_OBJ_POOL__

using namespace std;

//
// Object pool for managing dynamically allocated objects
//
// The implementation is based on the use of pointers
// Restrictions for obj_type:
// 1, obj_type must contain members of curr and next of int type
// 2, obj_type must contain member function of ReleaseObject(), which will release the resources in the object
//
template<class obj_type> class ObjPool
{
public:
	ObjPool() : obj_freelist(NULL) { }
	~ObjPool() { while(obj_freelist) { obj_type *next=(obj_type *)obj_freelist->next; delete obj_freelist; obj_freelist = next; } }

	obj_type *New(); // Create standalone object (not in object list)
	int Delete(obj_type *obj); // Delete standalone object (not in object list)

	obj_type *New(obj_type *&first); // Create object and insert at list tail pointed to by first, which must be initialized to NULL for new list
	obj_type *NewFront(obj_type *&first); // Create object and insert at list head pointed to by first, which must be initialized to NULL for new list
	int Delete(obj_type *&first, obj_type *obj); // Delete object obj and remove from list pointed by first
	int Detach(obj_type *&first, obj_type *obj); // Remove object obj from list pointed by first, obj must be deleted with Delete(obj) when no longer in use
	int DeleteAll(obj_type *&first); // Delete all objects in list pointed by first
	int AddToFreeList(obj_type *first); // Add list to free list

private:
	obj_type *obj_freelist; // list for keeping released objects
};

//
// Implementation part
//

template<class obj_type> inline obj_type *ObjPool<obj_type>::New()
{
	obj_type *o;

	if(obj_freelist)
	{
		o = obj_freelist;
		obj_freelist = (obj_type *)obj_freelist->next;
	}
	else
	{
		Attr_API(ATTR_OBJ_POOL_NEW_OBJ, 1);
		try {
			o = new obj_type();
		}catch(...) {
			Attr_API(ATTR_OBJ_POOL_NEW_OBJ_FAIL, 1);
			return NULL;
		}
	}

	// insert to obj list
	o->next = NULL;
	o->prev = o;
	return o;
}

template<class obj_type> inline int ObjPool<obj_type>::Delete(obj_type *obj)
{
	obj->ReleaseObject(); // NOTE: ReleaseObject() should not change prev/next pointers

	// put to free list
	obj->next = obj_freelist;
	obj_freelist = obj;
	return 0;
}

template<class obj_type> inline obj_type *ObjPool<obj_type>::New(obj_type *&first)
{
	obj_type *o;

	if(obj_freelist)
	{
		o = obj_freelist;
		obj_freelist = (obj_type *)obj_freelist->next;
	}
	else
	{
		Attr_API(ATTR_OBJ_POOL_NEW_OBJ, 1);
		try {
			o = new obj_type();
		}catch(...) {
			Attr_API(ATTR_OBJ_POOL_NEW_OBJ_FAIL, 1);
			return NULL;
		}
	}

	// put to obj list
	o->next = NULL;
	if(first)
	{
		obj_type *lst = (obj_type *)first->prev; // must not be null
		lst->next = o;
		o->prev = lst;
		first->prev = o;
	}
	else
	{
		first = o;
		o->prev = o;
	}
	return o;
}

template<class obj_type> inline obj_type *ObjPool<obj_type>::NewFront(obj_type *&first)
{
	obj_type *o;

	if(obj_freelist)
	{
		o = obj_freelist;
		obj_freelist = (obj_type *)obj_freelist->next;
	}
	else
	{
		Attr_API(ATTR_OBJ_POOL_NEW_OBJ, 1);
		try {
			o = new obj_type();
		}catch(...) {
			Attr_API(ATTR_OBJ_POOL_NEW_OBJ_FAIL, 1);
			return NULL;
		}
	}

	// put to obj list head
	o->next = first;
	if(first)
	{
		o->prev = first->prev;
		first->prev = o;
	}
	else
	{
		o->prev = o;
	}
	first = o;
	return o;
}



template<class obj_type> inline int ObjPool<obj_type>::Delete(obj_type *&first, obj_type *obj)
{
	if(obj==first)
	{
		if(obj->next) // new first
			((obj_type *)obj->next)->prev = first->prev;
		first = (obj_type *)obj->next;
	}
	else
	{
		obj_type *p = (obj_type *)obj->prev; // must not null
		obj_type *n = (obj_type *)obj->next;
		p->next = n;
		if(n)
			n->prev = p;
		else // obj is the last
			first->prev = p;
	}

	obj->ReleaseObject();

	obj->next = obj_freelist;
	obj_freelist = obj;

	return 0;
}

template<class obj_type> inline int ObjPool<obj_type>::Detach(obj_type *&first, obj_type *obj)
{
	if(obj==first)
	{
		if(obj->next) // new first
			((obj_type *)obj->next)->prev = first->prev;
		first = (obj_type *)obj->next;
	}
	else
	{
		obj_type *p = (obj_type *)obj->prev; // must not null
		obj_type *n = (obj_type *)obj->next;
		p->next = n;
		if(n)
			n->prev = p;
		else // obj is the last
			first->prev = p;
	}

	obj->next = obj->prev = NULL;
	return 0;
}

template<class obj_type> inline int ObjPool<obj_type>::AddToFreeList(obj_type *obj)
{
	((obj_type *)obj->prev)->next = obj_freelist;
	obj_freelist = obj;
	return 0;
}

template<class obj_type> inline int ObjPool<obj_type>::DeleteAll(obj_type *&first)
{
	obj_type *o = first;
	while(o)
	{
		o->ReleaseObject(); // NOTE: ReleaseObject() should not change prev/next pointers
		o = (obj_type *)o->next;
	}

	// put obj to free list
	AddToFreeList(first);
	first = NULL;
	return 0;
}

#endif
