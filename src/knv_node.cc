/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

/* knv_node.cc
 *
 * A node in Key-N-Value tree, also a KNV tree
 *
 * Key-N-Value philosophy:
 *  A KNV tree is common PB(Protocol Buffers) tree
 *  except that each node is identified by Tag + Key,
 *  Tag is the PB tag number, Key is the value of a special sub node with tag=1
 *  A Leaf is a special node that is either non-expandable or has not been expanded
 *
 *  The first 10 tag numbers are reserved for META data in a node,
 *  if you wish KNV tree to manage nodes, please define your data nodes beginning at tag 11
 *
 *
 * 2013-10-12	Created
 * 2013-11-04	Add parent pointer and eval_size to optimize folding
 * 2014-01-17	Use mem_pool for dynamic memory management
 * 2014-01-28	Use KnvHt to optimize hash initialization
 *
 *
 */

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <fstream>
#include <iostream>
#include <tr1/unordered_set>

#include "knv_node.h"
#include "obj_pool.h"

#ifndef PIC
#define inline __attribute__((always_inline))
#define force_inline __attribute__((always_inline))
#else
#define inline
#define force_inline
#endif

using namespace std::tr1;

static __thread const char *errorstr = "";

const char *KnvNode::GetGlobalErrorMsg()
{
	return errorstr;
}

static __thread ObjPool<KnvNode> *nodepool = NULL;

static inline ObjPool<KnvNode> *GetNodePool()
{
	if(nodepool)
		return nodepool;
	try {
		nodepool = new ObjPool<KnvNode>;
		return nodepool;
	}
	catch(...)
	{
		return NULL;
	}
}

force_inline char *knv_dynamic_data_t::alloc(uint32_t req_sz)
{
	if(req_sz <= sizeof(small_buf)) // use internal small buf if possible
	{
		if(mem)
		{
			UcMemManager::Free(mem);
			mem = NULL;
		}
		data = small_buf;
		sz = sizeof(small_buf);
		return data;
	}
	// re-allocate only when req_sz differs too much from allocated one
	if(data==NULL || req_sz>sz || (req_sz<<10) < sz)
	{
		UcMem *m = UcMemManager::Alloc(req_sz);
		if(m==NULL)
		{
			return NULL;
		}
		if(mem)
			UcMemManager::Free(mem);
		mem = m;
		data = (char*)mem->ptr();
		sz = mem->GetAllocSize();
		if(sz==0) sz = req_sz;
	}
	return data;
}

force_inline char *knv_dynamic_data_t::assign(UcMem *m, uint32_t size)
{
	if(mem)
	{
		UcMemManager::Free(mem);
		mem = NULL;
	}
	if(m)
	{
		mem = m;
		data = (char*)mem->ptr();
		sz = mem->GetAllocSize();
		if(sz==0) sz = size;
			return data;
	}
	cout << "dyn assig with NULL m" << endl;
	return NULL;
}

force_inline void knv_dynamic_data_t::free()
{
	if(mem)
	{
		UcMemManager::Free(mem);
		mem = NULL;
	}
	data = NULL;
	sz = 0;
}

static force_inline uint32_t get_keyhash(knv_tag_t t, const char *k, int len, int sz)
{
	if(len>0)
	{
		for(; len>3; len-=4,k+=4)
			t += *(uint32_t*)k;

		for(; len-->0; k++)
			t += ((uint32_t)(*(uint8_t*)k))<<(len*8);
	}
	return t&(sz-1); // t%sz, sz should be power of 2
}


KnvHt::KnvHt() : nr(0), mem(NULL)
{
	clear();
	bm = (uint8_t*)ht_default;
	ht = ht_default + default_bitmap_sz;
}

KnvHt::~KnvHt()
{
	if(mem)
		UcMemManager::Free(mem);
}

force_inline void KnvHt::clear()
{
	nr = 0;
	if(mem)
	{
		UcMemManager::Free(mem);
		mem = NULL;
		// reset bm/ht to point to default
		// if mem is NULL, they are already pointing to default
		bm = (uint8_t*)ht_default;
		ht = ht_default + default_bitmap_sz;
	}
	sz = KNV_DEFAULT_HT_SIZE;
	ht_default[0] = NULL; // bitmap
	if(default_bitmap_sz > 1)
	{
		ht_default[1] = NULL;
		for(unsigned i=2; i<default_bitmap_sz; i++)
			ht_default[i] = NULL;
	}
}

#define GET_BIT(bm, bit) ((bm)[(bit)/8] & (1<<((bit)%8)))
#define SET_BIT(bm, bit) (bm)[(bit)/8] |= (1<<((bit)%8))
#define CLEAR_BIT(bm, bit) (bm)[(bit)/8] &= (~(1<<((bit)%8))

inline KnvNode *KnvHt::get(knv_tag_t tag, const char *k, int klen)
{
	uint32_t hi = get_keyhash(tag, k, klen, sz);

	if(GET_BIT(bm, hi))
	{
		KnvNode *c = ht[hi];
		while(c)
		{
			if(c->IsMatch(tag, k, klen))
				return c;
			c = c->ht_next;
		}
	}
	return NULL;
}

inline KnvNode *KnvHt::get(knv_tag_t tag, const char *k, int klen, HtPos &pos)
{
	uint32_t hi = get_keyhash(tag, k, klen, sz);

	if(GET_BIT(bm, hi))
	{
		HtPos ht_pos = &ht[hi];
		KnvNode *c = *ht_pos;
		while(c)
		{
			if(c->IsMatch(tag, k, klen))
			{
				pos = ht_pos;
				return c;
			}
			ht_pos = &c->ht_next;
			c = *ht_pos;
		}
	}
	return NULL;
}


// levels should be power of 2
static const int ht_levels[] = { 256, 8192 };
#define biggest_level                 8192

inline int KnvHt::increase()
{
	if(sz >= biggest_level) // no increasing again
		return 0;

	int new_sz = biggest_level;
	int new_bm_sz = bitmap_size(new_sz);
	for(unsigned i=0; i<sizeof(ht_levels)/sizeof(ht_levels[0]); i++)
	{
		if(ht_levels[i] > sz)
		{
			new_sz = ht_levels[i];
			break;
		}
	}

	UcMem *new_mem = UcMemManager::Alloc((new_bm_sz+new_sz)*sizeof(KnvNode*));
	if(new_mem==NULL)
	{
		errorstr = "UcMemManager::Alloc failed";
		return -1;
	}

	uint8_t *new_bm = (uint8_t *)new_mem->ptr();
	KnvNode **new_ht = ((KnvNode **)new_mem->ptr()) + new_bm_sz;
	memset(new_bm, 0, new_bm_sz*sizeof(KnvNode *));

	// move old ht to new one
	int new_nr = 0;
	for(int i=0; i<sz; i++)
	{
		if(GET_BIT(bm, i))
		{
			KnvNode *c = ht[i];
			while(c)
			{
				int hi = get_keyhash(c->tag, c->key.val, c->key.len, new_sz);
				KnvNode *hnext = c->ht_next;
				if(!GET_BIT(new_bm, hi))
				{
					SET_BIT(new_bm, hi);
					new_ht[hi] = c;
					c->ht_next = NULL; 
				}
				else
				{
					KnvNode *hc = new_ht[hi];
					new_ht[hi] = c;
					c->ht_next = hc;
				}

				c = hnext;
				new_nr ++;
			}
		}
	}
	// clean up
	if(mem)
		UcMemManager::Free(mem);
	mem = new_mem;
	sz = new_sz;
	nr = new_nr;
	bm = new_bm;
	ht = new_ht;

	return 0;
}


inline int KnvHt::put(KnvNode *n)
{
	if(nr+1 > sz)
	{
	//	cout << "increasing ht" << endl;
		if(increase())
			return -1;
	}

	int hi = get_keyhash(n->tag, n->key.val, n->key.len, sz);

	if(!GET_BIT(bm, hi))
	{
		SET_BIT(bm, hi);
		n->ht_next = NULL; 
	}
	else
	{
		n->ht_next = ht[hi];
	}
	ht[hi] = n;
	nr ++;
	return 0;
}

inline int KnvHt::remove(KnvNode *node, HtPos pos)
{
	*pos = node->ht_next;
	nr --;
	return 0;
}

inline int KnvHt::remove(KnvNode *node)
{
	KnvNode *c;
	uint32_t hi = get_keyhash(node->tag, node->key.val, node->key.len, sz);

	if(GET_BIT(bm, hi) && (c=ht[hi]))
	{
		KnvNode *prev = NULL;
		do
		{
			if(c==node)
			{
				if(prev) prev->ht_next = c->ht_next;
				else ht[hi] = c->ht_next;
				nr --;
				return 0;
			}
			prev = c;
		}while((c=c->ht_next));
	}

	errorstr = "tag/key not found in ht";
	return -1;
}


force_inline int knv_key_t::init(knv_type_t ktype, const knv_value_t *kval, bool own_buf)
{
	if(kval) // has key
	{
		switch(ktype)
		{
		case KNV_STRING: // string key
			len = kval->str.len;
			if(own_buf)
			{
				val = dyn_data.alloc(len);
				if(val)
					memcpy(val, kval->str.data, len);
				else // error ...
				{
					len = 0;
					return -1;
				}
			}
			else
			{
				val = (char *)kval->str.data;
			}
			type = KNV_STRING;
			return 0;
		case KNV_FIXED32:
			len = sizeof(uint32_t);
			val = dyn_data.alloc(len);
			*(uint32_t*)val = kval->i32;
			type = KNV_FIXED32;
			return 0;
		case KNV_VARINT:
		case KNV_FIXED64:
			len = sizeof(uint64_t);
			val = dyn_data.alloc(len);
			*(uint64_t*)val = kval->i64;
			type = ktype;
			return 0;
		default:
			break;
		}
	}
	// set to null
	len = 0;
	val = NULL;
	type = ktype;
	return 0;
}

KnvNode::~KnvNode()
{
	key.dyn_data.free();
	dyn_data.free();
}

//
// InitNode is the only initialization entry for KnvNode
// If you add new members to KnvNode, please make sure to initialize them here
//
inline int KnvNode::InitNode(knv_tag_t _tag, knv_type_t _type, const knv_value_t *value, bool own_buf, bool update_eval, int field_sz, bool force_no_key)
{
	uint32_t str_len = 0;
	tag = _tag;
	type = _type;
	parent = NULL;
	if(value)
	{
		if(own_buf && type==KNV_NODE)
		{
			str_len = value->str.len;
			if(str_len)
			{
				char *pdata;
				if((pdata=dyn_data.alloc(str_len))==NULL)
				{
					errmsg = "out of memory";
					return -1;
				}
				memcpy(pdata, value->str.data, str_len);
				val.str.data = pdata;
				val.str.len = str_len;
			}
			else
			{
				val.str.len = 0;
				val.str.data = NULL;
			}
		}
		else
		{
			// points to external buff
			if(type==KNV_NODE)
			{
				val.str.len = str_len = value->str.len;
				val.str.data = value->str.data;
			}
			else
			{
				val.i64 = value->i64;
			}
		}
	}
	else
	{
		val.i64 = 0;
		val.str.data = NULL;
	}

	int ch_nr = -1;
	if(update_eval)
	{
		eval_val_sz = str_len;
		eval_sz = field_sz>0? field_sz : knv_eval_field_length(_tag, _type, &val);

		if(str_len) // type==KNV_NODE && val.str.len
		{
			if(!force_no_key)
			{
				knv_field_t f2, *pf2;
				if((pf2=knv_begin(&f2, val.str.data, val.str.len)) && pf2->tag==1)
				{
					key.init(pf2->type, &pf2->val, false); // won't fail
				}
				else
				{
					key.init(KNV_NODE, NULL, false);
					if(!pf2) ch_nr = 0;
				}
			}
			else
			{
				key.init(KNV_NODE, NULL, false);
			}
		}
		else
		{
			key.init(KNV_NODE, NULL, false);
			ch_nr = 0;
		}
	}
	else
	{
		eval_val_sz = eval_sz = 0;
		ch_nr = 0;
	}

	no_key = (force_no_key || type!=KNV_NODE);
	// if non-message, set child_num=0 to mark it expanded
	InitChildList(ch_nr);
	return 0;
}

inline void KnvNode::ReleaseNode()
{
	// Most initializations are performed when a node is reused
	subnode_dirty = false;
	child_num = -1;
	key.len = 0;
	key.val = NULL;

	key.dyn_data.free();
	ht.clear();

	KnvLeaf::ReleaseObject();
}

inline int KnvNode::ReleaseKnvNodeList(KnvNode *&list)
{
	//
	// here we use list concatenation to aviod recursion
	//
	KnvNode *fclist = NULL; // free child list
	KnvNode *clist = list;
	while(clist)
	{
		KnvNode *cclist = NULL, *n;
		for(KnvNode *c=clist; c; c=(KnvNode*)c->next)
		{
			if(c->child_num>=0)
			{
				if((n=c->metalist))
				{
					((KnvNode *)n->prev)->next = cclist;
					if(cclist) n->prev = cclist->prev; // new last
					cclist = n;
				}
				if((n=c->childlist))
				{
					((KnvNode *)n->prev)->next = cclist;
					if(cclist) n->prev = cclist->prev; // new last
					cclist = n;
				}
			}
			c->ReleaseNode();
		}
		((KnvNode*)clist->prev)->next = fclist;
		if(fclist) clist->prev = fclist->prev; // new last
		fclist = clist;
		clist = cclist;
	}
	if(fclist)
	{
		nodepool->AddToFreeList(fclist);
		list = NULL;
	}
	return 0;
}

void KnvNode::ReleaseObject()
{
	if(child_num<0) // not expanded
	{
		ReleaseNode();
		return;
	}

	// release meta list
	if(metalist) ReleaseKnvNodeList(metalist);
	// release child list
	if(childlist) ReleaseKnvNodeList(childlist);

	// release node data
	ReleaseNode();
}

KnvNode *KnvNode::New(const char *data, int data_len, bool own_buf)
{
	knv_field_t f, *pf;

	// 初始化 nodepool 只在New的时候检查，其它地方不做检查
	if(GetNodePool()==NULL)
	{
		errorstr = "Out of memory";
		Attr_API(ATTR_KNV_CREATE_POOL_FAIL, 1);
		return NULL;
	}

	pf = knv_begin(&f, data, data_len);
	if(pf==NULL) // non-message, not allowed here
	{
		errorstr = "Invalid bin format";
		return NULL;
	}
	int real_len = (char*)f.ptr - data;

	KnvNode *n = nodepool->New();
	if(n==NULL)
	{
		errorstr = "Out of memory";
		return NULL;
	}

	if(n->InitNode(pf->tag, pf->type, &pf->val, own_buf, true, real_len))
	{
		errorstr = n->errmsg;
		nodepool->Delete(n);
		return NULL;
	}

	return n;
}


KnvNode *KnvNode::New(knv_tag_t _tag, knv_type_t _type,
		knv_type_t _keytype, const knv_value_t *_key, const knv_value_t *_val, bool own_buf)
{
	// 初始化 nodepool 只在New的时候检查，其它地方不做检查
	if(GetNodePool()==NULL)
	{
		errorstr = "Out of memory";
		Attr_API(ATTR_KNV_CREATE_POOL_FAIL, 1);
		return NULL;
	}

	if(_tag==0) // at least tag should not be 0
	{
		errorstr = "Invalid tag argument";
		return NULL;
	}

	KnvNode *n = nodepool->New();
	if(n==NULL)
	{
		errorstr = "Out of memory";
		return NULL;
	}

	if(n->InitNode(_tag, _type, _val, own_buf))
	{
		errorstr = n->errmsg;
		nodepool->Delete(n);
		return NULL;
	}

	if(_key)
	{
		if(n->key.init(_keytype, _key, own_buf))
		{
			nodepool->Delete(n);
			errorstr = "init_key out of memory";
			return NULL;
		}

		if(n->key.len)// update key in meta
		{
			if(n->SetMeta(1, _keytype, &n->key.GetValue(), false, true)<0)
			{
				errorstr = n->GetErrorMsg();
				nodepool->Delete(n);
				return NULL;
			}
		}
	}
	return n;
}

int KnvNode::SetKey(knv_type_t _keytype, const knv_value_t *_key, bool own_buf)
{
	int ret;

	// update parent's ht
	if(parent)
		parent->ht.remove(this);

	if(key.init(_keytype, _key, own_buf))
	{
		errmsg = "init_key out of memory";
		ret = -1;
		goto out;
	}

	if(key.len)// update key in meta
	{
		if(SetMeta(1, _keytype, &key.GetValue(), false, true)<0)
		{
			ret = -2;
			goto out;
		}
	}
	else
	{
		if(InnerRemoveMeta(1)<0)
		{
			ret = -3;
			goto out;
		}
	}
	no_key = false;
	ret = 0;

out:
	if(parent)
		parent->ht.put(this);
	return ret;
}

inline KnvNode *KnvNode::InnerDuplicate(bool own_buf, bool force_no_key)
{
	knv_value_t v;
	UcMem *m = NULL;
	if(type==KNV_NODE && val.str.len==0 && child_num>=0)
	{
		// tree needs to be folded to get the value
		m = UcMemManager::Alloc(EvaluateSize());
		if(m==NULL)
		{
			errmsg = "Out of memory";
			return NULL;
		}
		char *buff = (char*)m->ptr();
		int pack_len = m->GetAllocSize();

		int ret = Serialize(buff, pack_len, false); //serialize without header
		if(ret)
		{
			UcMemManager::Free(m);
			return NULL;
		}
		if(eval_val_sz != pack_len)
		{
			UcMemManager::Free(m);
			errmsg = "bug: eval size differ from pack size";
			return NULL;
		}

		v.str.data = (char*)m->ptr();
		v.str.len = pack_len;
	}
	else
	{
		// use current val
		v = val;
	}

	KnvNode *n = nodepool->New();
	if(n==NULL)
	{
		if(m) UcMemManager::Free(m);
		errorstr = "Out of memory";
		return NULL;
	}

	if(n->InitNode(tag, type, &v, m? false:own_buf, true, eval_sz, force_no_key))
	{
		if(m) UcMemManager::Free(m);
		errorstr = n->errmsg;
		nodepool->Delete(n);
		return NULL;
	}

	if(m) // attach to dyn_data so that it will be freed automatically
		n->dyn_data.assign(m, v.str.len);

	return n;
}

// Make a new tree identical to current tree
KnvNode *KnvNode::Duplicate(bool own_buf)
{
	if(!IsValid())
	{
		errmsg = "Invalid node";
		return NULL;
	}
	return InnerDuplicate(own_buf, false);
}

inline KnvNode *KnvNode::DupEmptyNode()
{
	KnvNode *n = nodepool->New();
	if(n==NULL)
	{
		errmsg = "Out of memory";
		return NULL;
	}

	if(n->InitNode(tag, type, NULL, false, false))
	{
		errmsg = n->errmsg;
		nodepool->Delete(n);
		return NULL;
	}

	n->key.type = key.type;
	n->key.len = key.len;
	n->key.val = key.val;
	n->key.dyn_data.free();
	if(key.len>0)
		n->eval_val_sz = knv_eval_field_length(1, key.type, &key.GetValue());
	return n;
}

void KnvNode::Delete(KnvNode *tree)
{
	if(tree) nodepool->Delete(tree);
}

inline void KnvNode::InitChildList(int childnum)
{
	child_num = childnum;
	childlist = NULL;
	metalist = NULL;
}

inline int KnvNode::InnerExpand(bool force_no_key)
{
	if(IsExpanded()) // already expanded, nothing to do
		return 0;

	InitChildList(0);
	child_has_key = false;

	if(type!=KNV_NODE) // leaf, cannot be expanded
		return 0;

	// if this is tag 1, and parent has key, this node is probably a key
	// in this case, we assume it is non-expansible
	if(tag==1 && parent && parent->key.len)
		return 0;

	knv_field_t f, *pf;

	const void *prev_pos, *cur_pos = val.str.data;
	pf = knv_begin(&f, val.str.data, val.str.len);
	if(pf==NULL) // non-message, this is a leaf
	{
		return 0;
	}

	do
	{
		prev_pos = cur_pos;
		cur_pos = f.ptr;

		if(pf->tag<=UC_MAX_META_NUM) // 1~10 are reserved for meta
		{
			if(metalist==NULL)
			{
				memset(metas, 0, sizeof(metas));
			}
			KnvNode *n = nodepool->New(metalist);
			if(n==NULL)
			{
				errmsg = "Out of memory";
				child_num = 0;
				if(childlist) nodepool->DeleteAll(childlist);
				if(metalist) nodepool->DeleteAll(metalist);
				return -1;
			}

			// metas always have no key
			if(n->InitNode(pf->tag, pf->type, &pf->val, false, true, ((char*)cur_pos)-(char*)prev_pos, true /*force_no_key*/))
			{
				errmsg = n->errmsg;
				child_num = 0;
				if(childlist) nodepool->DeleteAll(childlist);
				if(metalist) nodepool->DeleteAll(metalist);
				return -2;
			}
			n->parent = this;

			metas[pf->tag] = n;
		}
		else
		{
			KnvNode *n = nodepool->New(childlist);
			if(n==NULL)
			{
				errmsg = "Out of memory";
				child_num = 0;
				if(childlist) nodepool->DeleteAll(childlist);
				if(metalist) nodepool->DeleteAll(metalist);
				return -3;
			}
	
			if(n->InitNode(pf->tag, pf->type, &pf->val, false, true, ((char*)cur_pos)-(char*)prev_pos, force_no_key)) // impossible
			{
				errmsg = n->errmsg;
				child_num = 0;
				if(childlist) nodepool->DeleteAll(childlist);
				if(metalist) nodepool->DeleteAll(metalist);
				return -4;
			}
			if(n->key.len>0) // mark child_has_key flag
				child_has_key = true;
			n->parent = this;
	
			// insert into hash table
			ht.put(n);
			child_num ++;
		}
	} while((pf=knv_next(pf)));

	subnode_dirty = false;
	if(!f.eom) // message not ending correctly
	{
		child_num = 0;
		if(childlist) nodepool->DeleteAll(childlist);
		if(metalist) nodepool->DeleteAll(metalist);
	}
	return 0;
}

int KnvNode::Expand()
{
	if(!IsValid())
	{
		errmsg = "node data is invalid";
		return -1;
	}

	return InnerExpand(false);
}


int KnvNode::EvaluateSize()
{
	if(eval_sz>=0) // already evaluated
		return eval_sz;

	// if there's a key, it must be in the meta (expanded), or in the value (folded)
	if(type!=KNV_NODE || child_num<0 || (child_num==0 && metalist==NULL) || // a leaf
		(IsBufferValid() && !subnode_dirty)) // already folded
	{
		eval_val_sz = type!=KNV_NODE? 0 : val.str.len;
		return (eval_sz=knv_eval_field_length(tag, type, &val));
	}

	eval_val_sz = 0;
	bool has_key = false;

	// key
	if(!no_key && key.len>0 && key.val)
	{
		has_key = true;
		eval_val_sz = knv_eval_field_length(1, key.type, &key.GetValue());
	}

	// metas
	for(KnvNode *f = metalist; f; f=(KnvNode *)f->next)
	{
		if(f==metalist)
		{
			if(has_key && f->tag==1)
				continue;
		}
		eval_val_sz += f->EvaluateSize();
	}

	// children
	for(KnvNode *n=childlist; n; n=(KnvNode *)n->next)
	{
		eval_val_sz += n->EvaluateSize();
	}

	knv_value_t v;
	v.str.len = eval_val_sz;
	eval_sz = knv_eval_field_length(tag, type, &v);

	return eval_sz;
}

static void PrintLeaf(string prefix, KnvLeaf *l, ostream &outstr);

int KnvNode::Fold()
{
	if(!IsValid())
	{
		errmsg = "node is invalid";
		return -1;
	}

	// a leaf does not need to be folded
	if(type!=KNV_NODE || child_num<0 || (child_num==0 && metalist==NULL))
	{
		return 0;
	}

	if(IsBufferValid() && !subnode_dirty) // already folded and no write to children occurs
		return 0;

	//
	// 只有当用户更新了子节点或Meta的数据才会走到这里，否则上面就返回了
	//

	UcMem *m = UcMemManager::Alloc(EvaluateSize());
	if(m==NULL)
	{
		errmsg = "Out of memory";
		return -2;
	}
	char *buff = (char*)m->ptr();
	int pack_len = m->GetAllocSize();

	int ret = Serialize(buff, pack_len, false); //serialize without header
	if(ret)
	{
		UcMemManager::Free(m);
		return -3;
	}
	if(eval_val_sz != pack_len)
	{
		UcMemManager::Free(m);
		errmsg = "bug: eval size differ from pack size";
		return -4;
	}

	val.str.data = dyn_data.assign(m, pack_len);
	val.str.len = pack_len;
	subnode_dirty = false;

	child_num = -1;
	if(metalist) nodepool->DeleteAll(metalist);
	if(childlist) nodepool->DeleteAll(childlist);

	return 0;
}

int KnvNode::Serialize(char *buf, int &len, bool with_header)
{
	if(!IsValid())
	{
		errmsg = "node is invalid";
		return -1;
	}

	int sz = len;
	knv_buff_t b;
	int ret = knv_init_buff(&b, buf, sz);
	if(ret)
	{
		errmsg = b.errmsg;
		return -2;
	}

	// if there's a key, it must be in the meta (expanded), or in the value (folded)
	if(type!=KNV_NODE || child_num<0 || (child_num==0 && metalist==NULL) || // a leaf
		(IsBufferValid() && !subnode_dirty)) // already folded
	{
		if(with_header)
		{
			ret = knv_add_field_val(&b, tag, type, &val);
			if(ret)
			{
				errmsg = b.errmsg;
				return -3;
			}
			len = knv_get_encoded_length(&b);
		}
		else // value only
		{
			switch(type)
			{
			case KNV_STRING:
				if(len>(int)val.str.len)
				{
					memcpy(buf, val.str.data, val.str.len);
					len = val.str.len;
					return 0;
				}
				errmsg = "not enoug space for value";
				return -4;
			default:
				errmsg = "not support serializing value for non-message";
				return -5;
			}
		}
		return 0;
	}

	int evalsize = EvaluateSize();

	if(with_header) // serialize header
	{
		// add message tag/type/length
		ret = knv_add_string_head(&b, tag, eval_val_sz);
		if(ret)
		{
			errmsg = b.errmsg;
			return -6;
		}
	}

	bool finished_key = false;

	// key should be placed in the first place
	if(!no_key && key.len>0 && key.val)
	{
		ret = knv_add_field_val(&b, 1, key.type, &key.GetValue());
		if(ret)
		{
			errmsg = b.errmsg;
			return -7;
		}
		finished_key = true;
	}

	KnvNode *f = metalist;
	if(finished_key && f && f->tag==1) // key alread serialized above
		f = (KnvNode *)f->next;

	int cur_len = knv_get_encoded_length(&b);

	// metas
	for(; f; f=(KnvNode *)f->next)
	{
		int left = sz - cur_len;
		int ret = f->Serialize(buf+cur_len, left, true);
		if(ret)
		{
			errmsg = f->errmsg;
			return -8;
		}
		cur_len += left;
	}

	for(KnvNode *n=childlist; n; n=(KnvNode *)n->next)
	{
		int left = sz - cur_len;
		int ret = n->Serialize(buf+cur_len, left, true);
		if(ret)
		{
			errmsg = n->errmsg;
			return -9;
		}
		cur_len += left;
	}

	if((with_header && cur_len != evalsize) || (with_header==false && cur_len != eval_val_sz))
	{
		errmsg = "Bug: eval size incorrect";
		return -10;
	}
	len = cur_len;
	return 0;
}

const KnvLeaf *KnvNode::GetValue()
{
	int ret = Fold();
	if(ret)
		return NULL;
	return this;
}

// fold(), return a buffer representing the whole tree
int KnvNode::Serialize(string &out)
{
	int eval_len = EvaluateSize();
	int pack_len = eval_len;
	try {
		out.resize(pack_len);
	}
	catch(...)
	{
		errmsg = "expand string buffer failed";
		return -1;
	}
	int ret = Serialize((char*)out.data(), pack_len);
	if(ret)
		return -2;
	if(eval_len != pack_len)
	{
		errmsg = "bug: eval size differ from pack size";
		return -3;
	}
	return 0;
}

#define UPDATE_EVAL_SZ(node, offset) do { if(offset) {\
	knv_value_t _e_v;\
	node->eval_val_sz += offset;\
	_e_v.str.len = node->eval_val_sz;\
	if(node->type!=KNV_STRING)\
	{ cout << "UPDATE_EVAL_SZ: node is not type of KNV_NODE" << endl; }\
	int _new_eval_sz = knv_eval_field_length(node->tag, node->type, &_e_v);\
	offset = _new_eval_sz - node->eval_sz;\
	node->eval_sz = _new_eval_sz;\
}}while(0)

#define SET_VALUE_DIRTY(node) do { node->val.str.len = 0; node->subnode_dirty = true; } while(0)

int KnvNode::SetValue(const char *str_val, int len, bool own_buf)
{

	if(!IsValid())
	{
		errmsg = "invalid data node";
		return -1;
	}

	if(type!=KNV_STRING)
	{
		errmsg = "node type mismatch";
		return -2;
	}

	// since key may be mis-interpreted for a leaf
	// we only restrict key from being changed if this is a sub_tree that has child
	if(child_num>0 || (child_num==0 && metalist==NULL))
	{
		// If a sub_tree has key, it's dangerous to change it raw value
		// but if you still want to change it, at least you need to make sure that key is not changed
		if(!no_key && key.len && key.val)
		{
			knv_key_t k;
			knv_field_t f, *pf;
			pf = knv_begin(&f, str_val, len);
			if(pf && pf->tag!=1)
				key.init(pf->type, &pf->val, false); // no memory allocation, won't fail
			else
				key.init(KNV_NODE, NULL, false);

			if(k.len && k!=key) // should not change key
			{
				errmsg = "key differ from existing data";
				return -3;
			}
		}
	}

	if(own_buf && len)
	{
		val.str.data = dyn_data.alloc(len);
		if(val.str.data==NULL)
		{
			errmsg = "out of memory";
			return -4;
		}
		val.str.len = len;
		memcpy(val.str.data, str_val, len);
	}
	else
	{
		val.str.len = len;
		val.str.data = len? (char *)str_val : (char*)NULL;
	}

	if(!no_key && key.len && key.val) // renew key's pointer, old pointer may be deleted later
	{
		// update parent's ht
		if(parent)
			parent->ht.remove(this);

		knv_field_t f, *pf;
		pf = knv_begin(&f, val.str.data, len);
		if(pf && pf->tag==1)
			key.init(pf->type, &pf->val, false);
		else
			key.init(KNV_NODE, NULL, false);

		if(parent)
			parent->ht.put(this);
	}

	// expanded data are no longer up to date, so delete them
	if(child_num>=0)
	{
		if(childlist) nodepool->DeleteAll(childlist);
		if(metalist) nodepool->DeleteAll(metalist);
		child_num = -1;
	}

	subnode_dirty = true; // parent needs to be folded
	int offset = 0;
	if(eval_sz>=0)
	{
		offset = val.str.len - eval_val_sz;
		UPDATE_EVAL_SZ(this, offset);
	}
	UpdateParentEvalueAndSetDirty(offset);

	return 0;
}

int KnvNode::SetValue(uint64_t int_val)
{
	if(!IsValid())
	{
		errmsg = "invalid data node";
		return -1;
	}

	if(type!=KNV_VARINT && type!=KNV_FIXED64 && type!=KNV_FIXED32)
	{
		errmsg = "node type mismatch";
		return -2;
	}

	// update value
	val.i64 = int_val;

	int old_evalsz = eval_sz, offset;
	eval_sz = knv_eval_field_length(tag, type, &val);
	offset = eval_sz-old_evalsz;
	eval_val_sz = 0;

	subnode_dirty = true; // parent needs to be folded
	UpdateParentEvalueAndSetDirty(offset);

	return 0;
}

int KnvNode::SetTag(knv_tag_t t)
{
	if(t==0)
	{
		errmsg = "Bad tag argument";
		return -1;
	}
	if(!IsValid())
	{
		errmsg = "Knv tree is not initialized";
		return -2;
	}

	// update parent's ht
	if(parent)
		parent->ht.remove(this);

	tag = t;

	if(parent)
		parent->ht.put(this);

	int offset = 0;
	if(eval_sz>=0)
	{
		int old_evalsz = eval_sz;
		if(type==KNV_NODE)
		{
			knv_value_t v; v.str.len = eval_val_sz;
			eval_sz = knv_eval_field_length(tag, type, &v);
		}
		else
		{
			eval_sz = knv_eval_field_length(tag, type, &val);
		}
		offset = eval_sz - old_evalsz;
	}

	subnode_dirty = true; // parent needs to be folded
	UpdateParentEvalueAndSetDirty(offset);
	return 0;
}

// 从子节点中查找tag+key相同的节点
KnvNode *KnvNode::FindChild(knv_tag_t t, const char *k, uint32_t klen)
{
	if(!IsValid() || Expand() || child_num<=0)
		return NULL;

	return ht.get(t, k, klen);
}

KnvNode *KnvNode::FindChild(knv_tag_t t, const char *k, uint32_t klen, KnvHt::HtPos &pos)
{
	if(!IsValid() || Expand() || child_num<=0)
		return NULL;

	return ht.get(t, k, klen, pos);
}

bool KnvNode::DetachChild(KnvNode *n)
{
	if(n)
	{
		int offset = eval_sz>=0? -n->EvaluateSize() : 0;

		// remove from ht
		ht.remove(n);

		// remove from child list
		if(!nodepool->Detach(childlist, n))
		{
			child_num --;
			if(eval_sz>=0)
			{
				UPDATE_EVAL_SZ(this, offset);
			}
			SET_VALUE_DIRTY(this);
			UpdateParentEvalueAndSetDirty(offset);
			return true;
		}
	}
	return false;
}

bool KnvNode::RemoveChildByPos(KnvNode *n, KnvHt::HtPos pos)
{
	if(n)
	{
		int offset = eval_sz>=0? -n->EvaluateSize() : 0;

		// remove from ht
		if(pos==NULL)
			ht.remove(n);
		else
			ht.remove(n, pos);

		// remove from child list
		if(!nodepool->Delete(childlist, n))
		{
			child_num --;
			if(eval_sz>=0)
			{
				UPDATE_EVAL_SZ(this, offset);
			}
			SET_VALUE_DIRTY(this);
			UpdateParentEvalueAndSetDirty(offset);
			return true;
		}
	}
	return false;
}


KnvNode *KnvNode::FindChildByTag(knv_tag_t _tag)
{
	if(!IsValid())
		return NULL;

	if(GetChildNum()<=0)
	{
		// leaf
		return NULL;
	}

	if(child_has_key)
	{
		KnvNode *c = childlist;
		while(c)
		{
			if(c->tag==_tag)
			{
				return c;
			}
			c = (KnvNode *)c->next;
		}
		return NULL;
	}

	return ht.get(_tag, NULL, 0);
}

KnvNode *KnvNode::FindChildByTag(knv_tag_t _tag, KnvHt::HtPos &pos)
{
	if(!IsValid())
		return NULL;

	if(GetChildNum()<=0)
	{
		// leaf
		return NULL;
	}

	if(child_has_key)
	{
		KnvNode *c = childlist;
		while(c)
		{
			if(c->tag==_tag)
			{
				pos = NULL;
				return c;
			}
			c = (KnvNode *)c->next;
		}
		return NULL;
	}

	return ht.get(_tag, NULL, 0, pos);
}


KnvNode *KnvNode::GetMeta(knv_tag_t _tag)
{
	if(!IsValid() || _tag>UC_MAX_META_NUM)
		return NULL;

	if((child_num<0 && Expand()) || metalist==NULL)
	{
		// error
		return NULL;
	}

	return metas[_tag];
}



inline int KnvNode::UpdateParentEvalueAndSetDirty(int offset)
{
	bool updatedirty = true;
	bool updateeval = (offset!=0);
	KnvNode *p = parent;
	while(p && (updatedirty || updateeval))
	{
		if(updateeval)
		{
			if(p->eval_sz<0 || // p has not been evaluated
				offset==0) // length is not changed
				updateeval = false;
			else
			{
				UPDATE_EVAL_SZ(p, offset);
			}
		}
		if(updatedirty)
		{
			if(p->subnode_dirty) // p is dirty, all upper parents are dirty already
				updatedirty = false;
			else
			{
				SET_VALUE_DIRTY(p);
			}
		}
		p = p->parent;
	}
	return 0;
}

int KnvNode::SetMeta(knv_tag_t _tag, knv_type_t _type, const knv_value_t *_data, bool own_buf, bool update_parent)
{
	if(!IsValid())
	{
		errmsg = "Node is not initialized";
		return -1;
	}
	if(type!=KNV_NODE)
	{
		errmsg = "Meta not supported for non-message";
		return -2;
	}
	if(_tag>UC_MAX_META_NUM)
	{
		errmsg = "Tag is out of range";
		return -3;
	}
	if(child_num<0 && Expand())
	{
		return -4;
	}

	KnvNode *m = GetMeta(_tag);
	int old_sz = m? m->EvaluateSize():0;

	if(!m)
	{
		if(metalist==NULL)
		{
			memset(metas, 0, sizeof(metas));
		}
		// if it is a key, we need to make sure it is at the first position
		m = _tag==1? nodepool->NewFront(metalist) : nodepool->New(metalist);
		if(!m)
		{
			errmsg = "nodepool out of memory";
			return -5;
		}
		// metas always have no key
		if(m->InitNode(_tag, _type, _data, own_buf, true, 0, true /*force_no_key*/))
		{
			errmsg = m->errmsg;
			nodepool->Delete(metalist, m);
			return -6;
		}
		m->parent = this;
		metas[_tag] = m;
	}
	else
	{
		m->type = _type;
		if(_data)
		{
			if(_type==KNV_STRING && own_buf)
			{
				m->val.str.data = dyn_data.alloc(_data->str.len);
				if(m->val.str.data==NULL)
				{
					m->val.str.len = 0;
					errmsg = "mempool out of memory";
					return -6;
				}
				memcpy(m->val.str.data, _data->str.data, _data->str.len);
				m->val.str.len = _data->str.len;
			}
			else
				m->val = *_data;
		}
		else
		{
			memset(&m->val, 0, sizeof(m->val));
		}
	
		// expanded data are no longer up to date, so delete them
		if(m->child_num>=0)
		{
			if(m->childlist) nodepool->DeleteAll(m->childlist);
			if(m->metalist) nodepool->DeleteAll(m->metalist);
			m->child_num = (!no_key && _tag==1)? 0:-1;
		}
	}

	// since we have invoked m->EvaluateSize() at the begining
	// we need to adjust the eval value to correct one
	m->eval_val_sz = _type==KNV_NODE? m->val.str.len : 0;
	m->eval_sz = knv_eval_field_length(_tag, _type, &m->val);

	// internal call, no need to set dirty and update parent status
	if(!update_parent)
	{
		eval_val_sz += knv_eval_field_length(_tag, _type, &m->val) - old_sz;
		return 0;
	}

	int offset = 0;
	if(eval_sz>=0) // update eval_sz
	{
		offset = m->eval_sz - old_sz;
		UPDATE_EVAL_SZ(this, offset);
	}

	SET_VALUE_DIRTY(this);
	UpdateParentEvalueAndSetDirty(offset);

	return 0;
}

int KnvNode::InnerRemoveMeta(knv_tag_t _tag)
{
	if(!IsValid())
	{
		errmsg = "Node is not initialized";
		return -1;
	}
	if(_tag>UC_MAX_META_NUM)
	{
		errmsg = "Tag is out of range";
		return -2;
	}

	if(child_num<0 && Expand())
	{
		return -3;
	}

	if(metalist==NULL || metas[_tag]==NULL) // no such meta
		return 0;

	int offset = eval_sz>=0? -metas[_tag]->EvaluateSize() : 0;

	if(nodepool->Delete(metalist, metas[_tag]))
	{
		errmsg = "Bug: tag in metas[] but delete failed";
		return -4;
	}
	metas[_tag] = NULL;

	if(eval_sz>=0) // update eval_sz
	{
		UPDATE_EVAL_SZ(this, offset);
	}

	SET_VALUE_DIRTY(this);
	UpdateParentEvalueAndSetDirty(offset);
	return 0;
}

int KnvNode::AddMeta(knv_tag_t _tag, knv_type_t _type, const knv_value_t *_data)
{
	if(GetMeta(_tag)==NULL)
		return SetMeta(_tag, _type, _data);

	KnvNode *m = nodepool->New(metalist);
	if(!m)
	{
		errmsg = "nodepool out of memory";
		return -1;
	}
	// metas always have no key
	if(m->InitNode(_tag, _type, _data, true, true, 0, true /*force_no_key*/))
	{
		errmsg = m->errmsg;
		nodepool->Delete(metalist, m);
		return -2;
	}
	m->parent = this;

	int offset = 0;
	if(eval_sz>=0) // update eval_sz
	{
		offset = knv_eval_field_length(_tag, _type, &m->val);
		UPDATE_EVAL_SZ(this, offset);
	}

	SET_VALUE_DIRTY(this);
	UpdateParentEvalueAndSetDirty(offset);

	return 0;
}

int KnvNode::RemoveMetaByTag(knv_tag_t _tag)
{
	if(!IsValid())
	{
		errmsg = "Node is not initialized";
		return -1;
	}
	if(_tag>UC_MAX_META_NUM)
	{
		errmsg = "Tag is out of range";
		return -2;
	}
	if(child_num<0 && Expand())
	{
		return -3;
	}

	if(metalist==NULL || metas[_tag]==NULL) // no such meta
		return 0;

	if(!no_key && _tag==1) // removing key
	{
		int ret = SetKey(key.type, NULL, true);
		if(ret<0)
			return ret;
	}

	int offset = 0, ret = 0;

	KnvNode *m = metalist, *n;
	for(; m; m=n)
	{
		n = (KnvNode*)m->next;
		if(m->GetTag()!=_tag)
			continue;

		if(eval_sz>=0)
			offset -= m->EvaluateSize();

		if(nodepool->Delete(metalist, m)<0) // should not fail, no way to turn back
		{ 
			errmsg = "Bug: delete meta in metalist failed";
			break;
		}
	}

	metas[_tag] = NULL;

	if(eval_sz>=0) // update eval_sz
	{
		UPDATE_EVAL_SZ(this, offset);
	}

	SET_VALUE_DIRTY(this);
	UpdateParentEvalueAndSetDirty(offset);

	return ret;
}

#define INSERT_CHILD(p, c, at_tail)                                     \
do {                                                                    \
	KnvNode *ic_c;                                                  \
	(c)->next = NULL;                                               \
	if((ic_c=(p)->childlist))                                       \
	{                                                               \
		if(at_tail)                                             \
		{                                                       \
			KnvNode *lc = (KnvNode *)ic_c->prev;            \
			lc->next = (c);                                 \
			(c)->prev = lc;                                 \
			ic_c->prev = (c);                               \
		}                                                       \
		else /* in front */                                     \
		{                                                       \
			c->prev = ic_c->prev;                           \
			c->next = ic_c;                                 \
			ic_c->prev = c;                                 \
			(p)->childlist = c;                             \
		}                                                       \
	}                                                               \
	else                                                            \
	{                                                               \
		(p)->childlist = (c);                                   \
		(c)->prev = (c);                                        \
	}                                                               \
	(p)->ht.put(c);                                                 \
	if(!(p)->child_has_key && (c)->key.len)                         \
		(p)->child_has_key = true;                              \
	(p)->child_num ++;                                              \
} while(0)

inline int KnvNode::InnerInsertChild(KnvNode *child, bool take_ownership, bool own_buf, bool update_parent, bool at_tail)
{
	if(!take_ownership) // if we donot have ownership, clone one
	{
		if(update_parent)
			child = child->InnerDuplicate(own_buf, false);
		else // internal use
			child = child->InnerDuplicate(false, true);
		if(child==NULL)
		{
			errmsg = "Out of memory";
			return -1;
		}
	}
	child->parent = this;

	INSERT_CHILD(this, child, at_tail);

	// internal call, no need to set dirty and update parent status
	if(!update_parent)
	{
		eval_val_sz += child->EvaluateSize();
		return 0;
	}

	int offset = 0;
	if(eval_sz>=0)
	{
		offset = child->EvaluateSize();
		UPDATE_EVAL_SZ(this, offset);
	}
	SET_VALUE_DIRTY(this);
	UpdateParentEvalueAndSetDirty(offset);
	return 0;
}

int KnvNode::InsertChild(KnvNode *child, bool take_ownership, bool own_buf, bool at_tail)
{
	if(!IsValid())
	{
		errmsg = "invalid node";
		return -2;
	}

	if(type!=KNV_NODE)
	{
		errmsg = "leaf cannot have child";
		return -3;
	}

	if(child_num<0 && Expand())
	{
		return -4;
	}

	if(child)
		return InnerInsertChild(child, take_ownership, own_buf, true, at_tail);

	errmsg = "bad iterator";
	return -5;
}

bool KnvNode::RemoveChild(knv_tag_t t, const char *k, uint32_t klen)
{
	KnvHt::HtPos pos;
	KnvNode *c = FindChild(t, k, klen, pos);
	if(c==NULL)
		return false;

	return RemoveChildByPos(c, pos);
}

int KnvNode::RemoveChildrenByTag(knv_tag_t _tag)
{
	if(!IsValid())
	{
		errmsg = "Tree is invalid";
		return -1;
	}

	if(GetChildNum()<=0)
	{
		// leaf, no child node
		return 0;
	}

	int match_nr = 0;
	int offset = 0, c_sz;
	KnvNode *c = childlist;
	while(c)
	{
		if(c->tag==_tag)
		{
			KnvNode *next_child = (KnvNode *)c->next;
			c_sz = eval_sz>=0? c->EvaluateSize() : 0;

			// remove in ht
			ht.remove(c);

			if(!nodepool->Delete(childlist, c))
			{
				child_num --;
				offset -= c_sz;
			}

			match_nr ++;
			c = next_child;
		}
		else // not matched
		{
			c = (KnvNode *)c->next;
		}
	}

	if(match_nr)
	{
		if(eval_sz>=0)
		{
			UPDATE_EVAL_SZ(this, offset);
		}
		SET_VALUE_DIRTY(this);
		UpdateParentEvalueAndSetDirty(offset);
	}
	return match_nr;
}

KnvNode *KnvNode::GetFirstChild()
{
	if(Expand())
		return NULL;
	errmsg = NULL; // Mark no error
	if(child_num<=0)
		return NULL;
	return childlist;
}

KnvNode *KnvNode::GetLastChild()
{
	if(Expand())
		return NULL;
	errmsg = NULL; // Mark no error
	if(childlist==NULL)
		return NULL;
	return (KnvNode *)childlist->prev;
}

// Get parent's next child
KnvNode *KnvNode::GetSibling()
{
	errmsg = NULL; // Mark no error
	return (KnvNode *)next;
}

KnvNode *KnvNode::GetPrevSibling()
{
	errmsg = NULL; // Mark no error
	return (KnvNode *)prev;
}

// for iterating metas
KnvNode *KnvNode::GetFirstMeta()
{
	if(Expand())
		return NULL;
	errmsg = NULL; // Mark no error
	if(child_num<0)
		return NULL;
	return metalist;
}

KnvNode *KnvNode::GetNextMeta(KnvNode *cur)
{
	return cur? (KnvNode *)cur->next : NULL;
}


// dup without children
#define DUPLICATE_NODE_META(pt, from) do {\
	pt = from->DupEmptyNode();\
	if(pt==NULL)\
	{\
		errmsg = from->errmsg;\
		if(out) KnvNode::Delete(out);\
		if(empty) KnvNode::Delete(empty);\
		empty = out = NULL;\
		return -1;\
	}\
} while(0)


#define DUP_CHILD_FROM(parent, from) do {\
	if(parent->InnerInsertChild(from, false, false, false))\
	{\
		errmsg = parent->errmsg;\
		if(out) KnvNode::Delete(out);\
		if(empty) KnvNode::Delete(empty);\
		empty = out = NULL;\
		return -1;\
	}\
} while(0)

#define GET_SUBDATA_SUB_TREE() \
do {\
	KnvNode *o, *e;\
	int ret = sub_data->InnerGetSubTree(sub_req, o, e, no_empty);\
	if(ret)\
	{\
		errmsg = sub_data->errmsg;\
		if(out) KnvNode::Delete(out);\
		if(empty) KnvNode::Delete(empty);\
		empty = out = NULL;\
		return -2;\
	}\
\
	if(o)\
	{\
		if(out==NULL) {\
			DUPLICATE_NODE_META(out, this);\
		}\
		out->InnerInsertChild(o, true, false, false);\
	}\
	if(!no_empty && e)\
	{\
		if(empty==NULL) {\
			DUPLICATE_NODE_META(empty, req_tree);\
		}\
		empty->InnerInsertChild(e, true, false, false);\
	}\
} while(0)


//
// 根据请求树构造并返回对应的数据树和由无效请求构成的树
//
int KnvNode::InnerGetSubTree(KnvNode *req_tree, KnvNode *(&out), KnvNode *(&empty), bool no_empty)
{
	out = NULL;
	empty = NULL;

	if(req_tree->type!=KNV_STRING)
	{
		if(req_tree->val.i64==0) // not requesting this node
			return 0;
	}

	if(req_tree->InnerExpand(false) || req_tree->child_num<=0)  // request the whole node
	{
		out = this->InnerDuplicate(false, true);
		if(out==NULL)return -2;
		if(key.len)
		{
			out->key.len = key.len;
			out->key.val = key.val;
		}
		return 0;
	}

	if(InnerExpand(!req_tree->child_has_key) || child_num<=0) // This is a leaf, but req_tree isn't
	{
		if(no_empty) return 0;

		empty = req_tree->InnerDuplicate(false, true);
		if(empty==NULL) { errmsg = req_tree->errmsg; return -3; }
		if(req_tree->key.len)
		{
			empty->key.len = req_tree->key.len;
			empty->key.val = req_tree->key.val;
		}
		return 0;
	}

	// if the request tree contains meta, request the coresponding meta in data if there is
	for(KnvNode *m=req_tree->metalist; m; m=(KnvNode *)m->next)
	{
		if(m->tag!=1 && m->type==KNV_VARINT && m->val.i64)
		{
			KnvNode *md = metas[m->tag];
			if(md)
			{
				if(out==NULL) { DUPLICATE_NODE_META(out, this); }
				const KnvLeaf *l = md->GetValue();
				if(l==NULL)
				{
					KnvNode::Delete(out);
					errmsg = md->errmsg;
					return -4;
				}
				out->SetMeta(md->GetTag(), md->GetType(), &l->GetValue(), false, false);
			}
		}
	}

	for(KnvNode *sub_req=req_tree->childlist; sub_req; sub_req=(KnvNode *)sub_req->next)
	{
		bool matched = false;
		if(sub_req->key.len) // sub_req has key field, only match one sub_node
		{
			KnvNode *sub_data = ht.get(sub_req->tag, sub_req->key.val, sub_req->key.len);
			if(sub_data)
			{
				GET_SUBDATA_SUB_TREE();
				matched = true;
			}
		}
		else // sub_req does not contain key field, match all sub-nodes with same tag
		{
			if(!child_has_key) // this->ht is a hash table of tags only
			{
				for(KnvNode *sub_data=ht.get(sub_req->tag,NULL,0); sub_data; sub_data=sub_data->ht_next)
				{
					if(sub_data->tag == sub_req->tag)
					{
						GET_SUBDATA_SUB_TREE();
						matched = true;
					}
				}
			}
			else // this->ht takes key into hash calculation
			{
				for(KnvNode *sub_data=childlist; sub_data; sub_data=(KnvNode *)sub_data->next)
				{
					if(sub_data->tag == sub_req->tag)
					{
						GET_SUBDATA_SUB_TREE();
						matched = true;
					}
				}
			}
		}
		if(!matched && !no_empty)
		{
			if(empty==NULL) { DUPLICATE_NODE_META(empty, req_tree); }
			DUP_CHILD_FROM(empty, sub_req);
		}
	}
	if(empty)
	{
		knv_value_t v;
		v.str.len = empty->eval_val_sz;
		empty->eval_sz = knv_eval_field_length(empty->tag, KNV_NODE, &v);
	}
	if(out)
	{
		knv_value_t v;
		v.str.len = out->eval_val_sz;
		out->eval_sz = knv_eval_field_length(out->tag, KNV_NODE, &v);
	}
	return 0;
}

int KnvNode::GetSubTree(KnvNode *req_tree, KnvNode *(&out), KnvNode *(&empty), bool no_empty)
{
	if(req_tree==NULL || req_tree->tag==0)
	{
		errmsg = "Bad argument";
		return -1;
	}

	if(tag != req_tree->tag || // no same node
			(req_tree->key.len && key!=req_tree->key)) // req contains key but different
	{
		out = empty = NULL;
		return 0;
	}

	return InnerGetSubTree(req_tree, out, empty, no_empty);
}

#define DELETE_SUBDATA_SUB_TREE() \
({\
	KnvNode *sub_match = NULL;\
	int del_ret = sub_data->DeleteSubTree(sub_req, sub_match, depth+1);\
	if(del_ret<0) /*fail */\
	{\
		errmsg = sub_data->errmsg;\
		if(match_req_tree)\
		{\
			KnvNode::Delete(match_req_tree);\
			match_req_tree = NULL;\
		}\
		del_ret = -4;\
	}\
	else /*succeed */\
	{\
		if(sub_match)\
		{\
			if(match_req_tree==NULL &&\
					(match_req_tree=KnvNode::NewTree(req_tree->tag, &req_tree->key))==NULL)\
			{\
				errmsg = "Out of memory"; del_ret = -5;;\
			} else {\
				match_req_tree->InnerInsertChild(sub_match, true, false, true);\
			}\
		}\
	}\
	del_ret;\
})

// returns:
//   0  successful
//   1  successful, the whole tree deleted
//   <0 failed
int KnvNode::DeleteSubTree(KnvNode *req_tree, KnvNode *(&match_req_tree), int depth)
{
	if(req_tree==NULL || req_tree->tag==0)
	{
		errmsg = "bad argument";
		return -1;
	}

	match_req_tree = NULL;

	if(tag != req_tree->tag || // no same node
			(req_tree->key.len && key!=req_tree->key)) // allow req not containing key
		return 0;

	if(req_tree->InnerExpand(false) || req_tree->child_num<=0)  // request the whole node
	{
		// req_tree is a leaf, request the whole tree
		if(req_tree->key.len) // if req_tree has key, construct an empty node
			match_req_tree = KnvNode::NewTree(req_tree->tag, &req_tree->key);
		else // if req_tree has no key, construct an int node with value 1
		{
			knv_value_t v; v.i64 = 1;
			match_req_tree = KnvNode::New(req_tree->tag, KNV_VARINT, KNV_NODE, NULL, &v);
		}
		return 1;
	}

	if(InnerExpand(!req_tree->child_has_key) || child_num<=0) // This is a leaf, but req_tree isn't
	{
		return 0; // no such node
	}

	for(KnvNode *sub_req=req_tree->childlist; sub_req; sub_req=(KnvNode *)sub_req->next)
	{
		if(sub_req->key.len) // contains key, remove by tag+key
		{
			KnvHt::HtPos pos; 
			KnvNode *sub_data = ht.get(sub_req->tag, sub_req->key.val, sub_req->key.len, pos);
			if(sub_data==NULL) continue; // no match
			int ret = DELETE_SUBDATA_SUB_TREE();
			if(ret<0) return ret;
			if(ret==1||sub_data->GetChildNum()<=0) // delete the whole node
				RemoveChildByPos(sub_data, pos);
		}
		else // sub_req does not contain key
		{
			if(sub_req->InnerExpand(false) || sub_req->child_num<=0) // remove by tag
			{
				if(sub_req->type==KNV_NODE || (sub_req->type==KNV_VARINT && sub_req->val.i64)) // requesting this node
				{
					RemoveChildrenByTag(sub_req->tag);
					if(match_req_tree==NULL &&
							(match_req_tree=KnvNode::NewTree(req_tree->tag, &req_tree->key))==NULL)
					{
						errmsg = "Out of memory"; return -5;
					}
					if(match_req_tree->InnerInsertChild(sub_req, false, false, true))
					{
						errmsg = match_req_tree->errmsg;
						if(match_req_tree) {KnvNode::Delete(match_req_tree); match_req_tree = NULL;}
						return -6;
					}
				}
			}
			else
			{
				KnvNode *sub_data=childlist;
				while(sub_data)
				{
					if(sub_data->tag == sub_req->tag)
					{
						int ret = DELETE_SUBDATA_SUB_TREE();
						if(ret<0) return ret;
						if(ret==1 || sub_data->GetChildNum()<=0) // if sub_data becomes empty, also remove it
						{
							KnvNode *sib = (KnvNode *)sub_data->next;
							RemoveChildByPos(sub_data, NULL);
							sub_data = sib;
							continue;
						}
					}
					sub_data = (KnvNode *)sub_data->next;
				}
			}
		}
	}

	if(GetChildNum()<=0)
	{
		// NOTE: we should keep what's removed in the match_tree
#if 0
		// data_tree becomes empty, delete the whole tree
		if(match_req_tree) KnvNode::Delete(match_req_tree);
		if(req_tree->key.len) // if req_tree has key, construct an empty node
			match_req_tree = KnvNode::NewTree(req_tree->tag, &req_tree->key);
		else // if req_tree has no key, construct an int node with value 1
		{
			knv_value_t v; v.i64 = 1;
			match_req_tree = KnvNode::New(req_tree->tag, KNV_VARINT, KNV_NODE, NULL, &v);
		}
#endif
		return 1;
	}
	return 0;
}

int KnvNode::UpdateSubTree(KnvNode *update_tree, int max_level)
{
	if(update_tree==NULL || update_tree->tag==0)
	{
		errmsg = "bad argument";
		return -1;
	}

	if(tag!=update_tree->tag || key!=update_tree->key) // not same node
	{
		return 0;
	}

	if(max_level==0) // 最后一级不需要处理key了
	{
		InnerExpand(true);
		update_tree->InnerExpand(true);
	}

	if(max_level<0 || // do not continue to expand
		update_tree->GetChildNum()<=0 || // update_tree is a leaf
		GetChildNum()<=0) // this is a leaf
	{
		const KnvLeaf *l = update_tree->GetValue();
		if(l==NULL)
		{
			errmsg = update_tree->errmsg;
			return -2;
		}

		if(SetValue(update_tree->type, l->GetValue(), true))
		{
			return -3;
		}
		return 0;
	}

	for(KnvLeaf *m=update_tree->metalist; m; m=(KnvLeaf *)m->next)
	{
		if(m->GetTag()>1 && // tag==1 is key, skip key
			SetMeta(m->GetTag(), m->GetType(), &m->GetValue(), false, true)<0)
		{
			return -6;
		}
	}

	max_level --;

	//如果某个（可能有多个）sub_update只有tag，没有key，必须保证旧的tag被删掉（就是一个reapted项不能被分开）
	if(max_level<0)
	{
		unordered_set<knv_tag_t> tags;
		// 最后一层是字段，如果tag存在，必须先删除，新旧的tag不能同时存在
		for(KnvNode *sub_update=update_tree->childlist; sub_update; sub_update=(KnvNode *)sub_update->next)
		{
			tags.insert(sub_update->tag);
		}
		for(unordered_set<knv_tag_t>::iterator it=tags.begin(); it!=tags.end(); it++)
		{
			RemoveChildrenByTag(*it);
		}
	}

	for(KnvNode *sub_update=update_tree->childlist; sub_update; sub_update=(KnvNode *)sub_update->next)
	{
		KnvNode *sub_data;
		if(max_level>=0 &&
			(sub_data=ht.get(sub_update->tag, sub_update->key.val, sub_update->key.len))) // match
		{
			if(sub_data->UpdateSubTree(sub_update, max_level))
			{
				errmsg = sub_data->errmsg;
				return -4;
			}
		}
		else // no match, insert
		{
			if(InnerInsertChild(sub_update, false, false, true))
			{
				return -5;
			}
		}
	}
	return 0;
}

// make a request tree out of this (a data tree)
KnvNode *KnvNode::MakeRequestTree(int max_level)
{
	if(!IsValid())
	{
		errmsg = "Bad data tree";
		return NULL;
	}

	KnvNode *new_tr = NULL;

	if(max_level<0 || GetChildNum()<=0)
	{
		if(child_num<0 || metalist==NULL) // a leaf with no key, convert to a int(1)
		{
			knv_value_t v; v.i64 = 1;
			new_tr = KnvNode::New(tag, KNV_VARINT, KNV_DEFAULT_TYPE, NULL, &v, false);
		}
		else
		{
			new_tr = KnvNode::NewTree(tag, &key);
		}
		if(!new_tr)
			errmsg = GetGlobalErrorMsg();
		return new_tr;
	}

	for(KnvNode *sub_tree=childlist; sub_tree; sub_tree=(KnvNode *)sub_tree->next)
	{
		if(new_tr==NULL)
		{
			new_tr = KnvNode::NewTree(tag, &key);
			if(new_tr==NULL)
			{
				errmsg = "Out of memory";
				return NULL;
			}
		}

		KnvNode *sub_req = sub_tree->MakeRequestTree(max_level-1);
		if(sub_req==NULL)
		{
			errmsg = sub_tree->errmsg;
			return NULL;
		}

		new_tr->InnerInsertChild(sub_req, true, false, true);
	}

	return new_tr;
}


static inline string GetTypeName(knv_type_t t, int child_num, KnvLeaf *metalist)
{
	switch(t)
	{
	case KNV_STRING:
		if(child_num<0 || (child_num<1 && metalist==NULL))
			return "String";
		return "Node";
	case KNV_VARINT:
			return "Int";
	case KNV_FIXED32:
			return "Int32";
	case KNV_FIXED64:
			return "Int64";
	default:
			return "Unknown";
	}
}

static bool is_string(const string &s)
{
	for(unsigned i=0;i<s.length(); i++)
	{
		if(!isprint(s[i]))
			return false;
	}
	return true;
}

static void PrintLeaf(string prefix, KnvLeaf *l, ostream &outstr)
{
	int sz = knv_eval_field_length(l->GetTag(), l->GetType(), &l->GetValue());
	outstr << prefix << "tag=" << l->GetTag() << ", type=" << GetTypeName(l->GetType(), 0, NULL);
	if(l->GetType()==KNV_STRING)
	{
		outstr << ", length=" << l->GetValue().str.len << ", val=";
		string s(l->GetValue().str.data, l->GetValue().str.len);
		// simple test for visible string
		if(is_string(s))
			outstr << s << endl;
		else
		{
			char s[4];
			for(unsigned i=0; i<l->GetValue().str.len; i++)
			{
				snprintf(s, sizeof(s), "%02X", (uint8_t)l->GetValue().str.data[i]);
				outstr << s;
			}
			outstr << endl;
		}
	}
	else
	{
		outstr << ", size=" << sz << ", val=";
		if(l->GetType()==KNV_FIXED32)
		{
			outstr << l->GetValue().i32 << endl;
		}
		else
		{
			outstr << l->GetValue().i64 << endl;
		}
	}
}



void KnvNode::Print(string prefix, ostream &outstr)
{
	if(!IsValid())
	{
		outstr << prefix << "(NULL)" << endl;
		return;
	}
	Expand();
	if(child_num>0 || (child_num>=0 && metalist))
	{
		outstr << prefix << "[+] tag=" << tag << ", msg_size=" << eval_sz << ", parent=" << (parent?(int)parent->tag:-1) << endl;
		if(child_num>=0 && metalist)
		{
			KnvLeaf *l = metalist;
			while(l)
			{
				PrintLeaf(prefix+"    [m] ", l, outstr);
				l = (KnvLeaf *)l->next;
			}
		}
		if(child_num>0)
		{
			KnvNode *n = GetFirstChild();
			while(n)
			{
				n->Print(prefix+"    ", outstr);
				n = n->GetSibling();
			}
		}
	}
	else
	{
		PrintLeaf(prefix, (KnvLeaf*)GetValue(), outstr);
	}
}

/*
 * 遍历本结点的数据, 看输入的结点中是否存在, 如果不存在, 则添加到返回数据中, 如果存在, 判断数据是否一致, 如果不一致, 则添加到返回数据中.
 */
KnvNode *KnvNode::Compare(KnvNode *pstNode, int &nRetCode)
{
	nRetCode = 0;
	if(pstNode == NULL || GetKey() != pstNode->GetKey())
	{
		KnvNode *pstDupNode = Duplicate(true);
		if(pstDupNode == NULL)
		{
			nRetCode = 1;
		}
		return pstDupNode;
	}
	KnvNode *pstRetNode = NULL;
	for(KnvNode *pstChild = GetFirstChild(); pstChild; pstChild = pstChild->GetSibling())
	{
		const knv_key_t &stChildKey = pstChild->GetKey();
		KnvNode *pstCmpChild = pstNode->FindChild(pstChild->GetTag(), stChildKey.GetData(), stChildKey.GetLength());
		if(pstCmpChild == NULL)
		{
			//从Node里边获得的Child数据, 需要由外层来释放
			if(! pstRetNode)
			{
				knv_tag_t stTag = GetTag();
				pstRetNode = KnvNode::NewTree(stTag, &GetKey());
				if(pstRetNode == NULL)
				{
					errmsg = "Out of memory";
					nRetCode = 1;
				}
				return NULL;
			}
			if(pstRetNode->InsertChild(pstChild, false, true) != 0)
			{
				errmsg = pstRetNode->GetErrorMsg();
				nRetCode = 1;
				KnvNode::Delete(pstRetNode);
				return NULL;
			}
		}
		else
		{
			//从Compare获取到的数据(手动用New创建的), 需要自动释放
			KnvNode *pstSubRet = pstChild->Compare(pstCmpChild, nRetCode);
			if(nRetCode != 0)
			{
				errmsg = pstChild->GetErrorMsg();
				KnvNode::Delete(pstRetNode);
				return NULL;
			}
			if(pstSubRet != NULL)
			{
				if(! pstRetNode)
				{
					knv_tag_t stTag = GetTag();
					pstRetNode = KnvNode::NewTree(stTag, &GetKey());
					if(pstRetNode == NULL)
					{
						errmsg = "Out of memory";
						nRetCode = 1;
					}
					return NULL;
				}
				if(pstRetNode->InsertChild(pstSubRet, true, false) != 0)
				{
					errmsg = pstRetNode->GetErrorMsg();
					nRetCode = 1;
					KnvNode::Delete(pstRetNode);
					return NULL;
				}
			}
		}
	}
	return pstRetNode;
}
