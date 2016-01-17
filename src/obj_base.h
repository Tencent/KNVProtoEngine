/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

// obj_base.h
// Describing interfaces needed by ObjPool
//
// 2013-08-28	Created
//

#ifndef __UC_OBJ_BASE__
#define __UC_OBJ_BASE__

template<class obj_type> class ObjPool;
template<class obj_type> class ObjPoolR;

// Base object class for ObjPool
// Objects can be linked in a list through next member
class ObjBase
{
public:
	ObjBase(): prev(NULL), next(NULL) { }
	virtual ~ObjBase() { }

	void *prev; // previous object in obj
	void *next; // Next object in ObjPool

	virtual void ReleaseObject() = 0;
};


#endif

