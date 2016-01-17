/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

/* knv_node.h
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
 * 2013-10-12	Created
 * 2013-11-04	Add parent pointer and eval_size to optimize folding
 * 2014-01-17   Use mem_pool for dynamic memory management
 * 2014-01-28   Use KnvHt to optimize hash initialization
 * 2014-05-17   Meta use KnvNode instead of KnvLeaf
 *
 */

#ifndef __KNV_NODE__
#define __KNV_NODE__

#include "knv_codec.h"
#include "obj_base.h"
#include "mem_pool.h"
#include <string>
#include <ostream>
#include <iostream>
#include <vector>
#include <stdint.h>



using namespace std;

#define KNV_MAX_CHILD_NUM	1000
#define KNV_DEFAULT_HT_SIZE	32
#define UC_MAX_META_NUM	 	10

#define KNV_NODE        	KNV_STRING  // a node is also a string
#define KNV_DEFAULT_TYPE	KNV_STRING

typedef knv_field_val_t knv_value_t;
typedef uint32_t knv_tag_t;

class knv_dynamic_data_t // data struct for dynamically allocated data
{
public:
	knv_dynamic_data_t():sz(0),data(NULL),mem(NULL){}

	char *alloc(uint32_t req_sz);
	char *assign(UcMem *m, uint32_t size);
	void free();
private:
	uint32_t sz; // allocated size
	char *data;
	UcMem *mem;
	char small_buf[64];
};


class knv_key_t
{
public:
	knv_key_t():type(KNV_DEFAULT_TYPE),len(0),val(NULL),dyn_data(){}
	knv_key_t(knv_type_t t, uint16_t l, char *v):type(t),len(l),val(v),dyn_data(){}
	knv_key_t(uint64_t iv):dyn_data(){ knv_value_t v; v.i64 = iv; init(KNV_VARINT, &v, false); }
	knv_key_t(char *sv, int l):type(KNV_STRING),len(l),val(sv),dyn_data(){}
	knv_key_t(string &s):type(KNV_STRING),len(s.length()),val((char*)s.data()),dyn_data(){}
	knv_key_t(const knv_key_t &k):dyn_data() { init(k.type, &k.GetValue(), false); } // key copy
	knv_key_t(knv_type_t t, const knv_value_t &v):dyn_data() { init(t, &v, false); }
	// Here's a convention: knv_key_t will NEVER manage dynamic buffer by itself
	// The caller should guarantee that pointer passed to knv_key_t() constructors
	// should be available through knv_key_t's life cycle
	~knv_key_t() {}

	bool operator<(const knv_key_t &k) const { return (type<k.type || len<k.len || (len && memcmp(val, k.val, len)<0)); }
	bool operator==(const knv_key_t &k) const { return (type==k.type && len==k.len && (len==0 || memcmp(val, k.val, len)==0)); }
	bool operator!=(const knv_key_t &k) const { return !(*this==k); }
	knv_key_t & operator=(const knv_key_t &k) { init(k.type, &k.GetValue(), true); return *this; }

	void assign(knv_type_t _type, uint16_t _len, char *_val) { type=_type; len=_len; val=_val; }

	// returns a union type
	const knv_value_t &GetValue() const	{
		static __thread knv_value_t v; if(type==KNV_STRING) { v.str.len = len; v.str.data = val; }
		else { v.i64 = 0; if(val && len) memcpy(&v.i64, val, len>8? 8:len); } return v;
	}
	// returns an int type, may be truncated, use with care
	uint64_t GetIntVal() const { uint64_t iv = 0; if(val && len) memcpy(&iv, val, len>8? 8: len); return iv; }
	string GetStrVal() const { string s; s.assign(val, len); return s; }
	knv_type_t GetType() const { return type; }
	uint16_t GetLength() const { return len; }
	const char *GetData() const { return len? val:NULL; }

private: // data part
	knv_type_t type; // key's tag is always 1, here we only need it's type
	uint16_t len; // key length
	char *val;    // key value

	// member used only by KnvNode to manage dynamic data
	knv_dynamic_data_t dyn_data; // dynamic buffer

private:
	int init(knv_type_t ktype, const knv_value_t *kval, bool own_buf);

friend class KnvNode;
friend class KnvHt;
};

// A knv leaf
class KnvLeaf : public ObjBase
{
public:
	KnvLeaf():ObjBase(),tag(0),type(KNV_DEFAULT_TYPE),dyn_data() { val.str.len = 0; val.str.data = NULL; }
	virtual ~KnvLeaf() {}
	const knv_tag_t &GetTag() const { return tag; }
	const knv_type_t &GetType() const { return type; }
	const knv_value_t &GetValue() const { return val; }

protected:
	knv_tag_t tag;
	knv_type_t type;
	knv_value_t val;

	// dynamically allocated buffer, KnvLeaf::val.str.data points here if in use
	// dyn_data will never be released except when enlarging space
	knv_dynamic_data_t dyn_data;

	// needed by obj_pool
	virtual void ReleaseObject(){ tag = 0; val.i64 = 0; val.str.data = NULL; dyn_data.free(); }

friend class ObjPool<KnvLeaf>;
friend class KnvNode;
};


class KnvNode;

// calculate how many elements are needed to store the bitmap of (KnvNode *)[sz]
#define bitmap_size(sz) (((sz)+(8*sizeof(KnvNode*)-1))/(8*sizeof(KnvNode*)))
#define default_bitmap_sz bitmap_size(KNV_DEFAULT_HT_SIZE)

class KnvHt
{
public:
	KnvHt();
	~KnvHt();

	void clear();

	typedef KnvNode **HtPos;
	// pos is a place holder for the returned pointer in ht
	// if the first element in ht[hash] is returned, than pos points to ht[hash]
	// else pos points to an element's ht_next
	KnvNode *get(knv_tag_t tag, const char *k, int klen, HtPos &pos);
	KnvNode *get(knv_tag_t tag, const char *k, int klen);
	int put(KnvNode *node);
	int remove(KnvNode *node, HtPos pos);
	int remove(KnvNode *node);

private:
	int increase();

private:
	int nr; // current number of KnvNodes, served for checking if ht expansion is needed
	int sz; // current ht allocated size
	UcMem *mem;
	// ht_default[default_bitmap_sz] is bitmap for (ht_default+default_bitmap_sz)[KNV_DEFAULT_HT_SIZE]
	KnvNode *ht_default[KNV_DEFAULT_HT_SIZE+default_bitmap_sz];
	KnvNode **ht;
	uint8_t *bm;
};


// A knv tree or node
//   A leaf is a special node that cannot be expanded
//   A Node is a leaf when folded
// A knv tree consists of a list of metas and a list of child nodes
//   the first field with tag=1 is always the key
//   tags 1~10 are metas (including key)
//   tags >=11 are child nodes
class KnvNode : public KnvLeaf
{
private: // private data
	knv_key_t key;
	KnvNode *ht_next; // next in parent's hash list

	int child_num; // -1 not expanded yet, >=0 num of children expanded/inserted
	KnvHt ht; // hash table, allocated from UcMemPool
	KnvNode *childlist; // last is needed for sequential insertion
	KnvNode *metalist;  // tag 1~10, up to 10 meta pb fields
	KnvNode *metas[UC_MAX_META_NUM+1];  // keeps 1 meta for each tag 1~10

	bool subnode_dirty; // mark child or meta being modified
	bool child_has_key; // after expansion, mark wether its direct children have key
	bool no_key; // mark this node will not handle key

	KnvNode *parent; // pointer to parent node
	int eval_sz; // -1 not evaluated, >=0, evaluated buffer size needed for serialization, updated when data changed
	int eval_val_sz; // if(eval_sz>=0 && type==KNV_NODE) evaluated value's length (not including length of tag/type/len)

private:
	KnvNode():KnvLeaf(),key(),ht_next(NULL),\
		  child_num(-1),ht(),childlist(NULL),metalist(NULL),\
		  subnode_dirty(false),child_has_key(false),no_key(false),parent(NULL),eval_sz(-1),eval_val_sz(0),errmsg(NULL)
		  { memset(metas, 0, sizeof(metas)); }
	virtual ~KnvNode();
	virtual void ReleaseObject(); // needed by ObjPool to reclaim resources
	void ReleaseNode(); // release node data

	// InitNode is the only initialization entry for KnvNode
	// If you add new members to KnvNode, please make sure to initialize them here
	int InitNode(knv_tag_t _tag, knv_type_t _type, const knv_value_t *value, bool own_buf, bool update_eval=true, int field_sz=0, bool force_no_key=false);

	// duplicate(), but for inner use
	KnvNode *InnerDuplicate(bool own_buf, bool force_no_key);

	void InitChildList(int childnum);
	int UpdateParentEvalueAndSetDirty(int offset);

	// Expand and Fold are automatically performed when needed
	int InnerExpand(bool force_no_key=false);
	int Expand(); // de-serialize
	int Fold();   // serialize
	int SetKey(knv_type_t _keytype, const knv_value_t *_key, bool own_buf);

	// internal methods, allow not updating parent's dirty state and eval_sz
	int InnerGetSubTree(KnvNode *req_tree, KnvNode *(&out), KnvNode *(&empty), bool no_empty);
	int InnerInsertChild(KnvNode *child, bool take_ownership, bool own_buf, bool update_parent, bool at_tail=true);
	int SetMeta(knv_tag_t _tag, knv_type_t _type, const knv_value_t *_data, bool own_buf, bool update_parent);
	KnvNode *DupEmptyNode();
	int InnerRemoveMeta(knv_tag_t _tag);
	int ReleaseKnvNodeList(KnvNode *&list);

public: // constructors
	// construct from a KNV(one-field string) message, a KNV message is like
	//     message knv_node_holder
	//     {
	//        required knv_node msg = 11; // tag is a key factor for knv node
	//     }
	// to make it simple, knv_node = tag + length + pb_message
	// if you wish to construct a node representing a pb message not encapsulated in a tag, call NewFromMessage() instead
	//
	// arguments:
	//    data/data_len/bin -- a message consists of knv_node_holder
	//    own_buf           -- true:  inner buffer is allocated to store node data
	//                         false: external buffer from data/bin is used
	//                                the caller should make sure that buffer is available through knvnode's life-cycle
	// returns:
	//    success -- a node representing a knv_node
	//    failure -- NULL is returned, call KnvNode::GetGlobalErrorMsg() to get the error message
	static KnvNode *New(const char *data, int data_len, bool own_buf=true);
	static KnvNode *New(const string &bin, bool own_buf=true);
	// construct from given tag/key/val
	static KnvNode *New(knv_tag_t _tag, knv_type_t _type, knv_type_t _keytype,
			const knv_value_t *_key, const knv_value_t *_val, bool own_buf=true);
	static KnvNode *New(knv_tag_t _tag, knv_type_t _type, const knv_key_t &_key,
			const knv_value_t *_val, bool own_buf=true);
	// the easiest way to construct an empty tree
	static KnvNode *NewTree(knv_tag_t _tag, const knv_key_t *_key=NULL);
	// construct from a leaf
	static KnvNode *New(const KnvLeaf &l, bool own_buf=true);
	static KnvNode *New(knv_tag_t _tag, knv_type_t _type, UcMem *val, int length);
	// construct from PB message string, you can optionally specify a tag for the node
	// Note: if you wish to get this message back, call GetValue() instead of Serialize()
	static KnvNode *NewFromMessage(const string &msg, knv_tag_t _tag=1);

	// Delete a tree
	// <<Warning>>: user SHOULD NOT delete a child node in a tree
	// if you want to do this, call parent->RemoveChild()/node->Remove() instead
	static void Delete(KnvNode *tree);

	// error msg for static functions: New()
	static const char *GetGlobalErrorMsg();

	// new copy of self
	KnvNode *Duplicate(bool own_buf);

public: // methods
	bool IsValid()      { return tag!=0; }
	bool IsExpanded()   { return child_num>=0; }
	bool IsLeaf()       { return type!=KNV_NODE || (child_num<0 && Expand()) || (child_num==0 && metalist==NULL);  }
	bool IsBufferValid(){ return (val.str.len>0 && val.str.data); }
	bool IsMatch(knv_tag_t t, const char *k, int klen);

	const knv_key_t &GetKey() { return key; }
	const knv_type_t GetKeyType() { return key.type; }

	int EvaluateSize(); // return the packed size

public: // API for data operations
	const KnvLeaf *GetValue(); // fold(), return a leaf representing data
	uint64_t GetIntVal(); // get value as an int, returns 0 for non-int type
	string GetStrVal(); // get value as a string, return empty string for non-string type
	int Serialize(string &out); // fold(), return a buffer representing the whole knv tree
	int Serialize(char *buf, int &len, bool with_header = true); // serialize to user given buffer (also pack tag if with_header=true)
	// set interface can not change key
	// own_buf: true - node has its own buffer, false - node use passed buffer
	int SetValue(const char *str_val, int len, bool own_buf); // message or string
	int SetValue(const uint64_t int_val);
	int SetValue(knv_type_t _type, const knv_value_t &new_val, bool own_buf);
	int SetTag(knv_tag_t t); // a same node hung at different trees may have different tags

	// operations to child nodes
	// pos allow user to find and remove later
	KnvNode *FindChildByTag(knv_tag_t t);
	KnvNode *FindChildByTag(knv_tag_t t, KnvHt::HtPos &pos);
	KnvNode *FindChild(knv_tag_t t, const char *k, uint32_t klen);
	KnvNode *FindChild(knv_tag_t t, const char *k, uint32_t klen, KnvHt::HtPos &pos);
	bool RemoveChildByPos(KnvNode *n, KnvHt::HtPos pos);

	// methods for creating child nodes
	// own_buf: true - node create a new buffer, false - node use passed buffer
	// Used when key is not present or key is included in _data (_data may be NULL)
	KnvNode *InsertChild(knv_tag_t _tag, knv_type_t _type, const knv_value_t *_data, bool own_buf);
	// Used when key is available (_data may be NULL)
	KnvNode *InsertChild(knv_tag_t _tag, knv_type_t _type, const knv_key_t &_key, const knv_value_t *_data, bool own_buf);
	KnvNode *InsertSubNode(knv_tag_t _tag, const knv_key_t *_key = NULL);
	KnvNode *InsertIntLeaf(knv_tag_t _tag, uint64_t _val);
	KnvNode *InsertStrLeaf(knv_tag_t _tag, const char *_val, int _len);
	// Used when a child node is already created
	int InsertChild(KnvNode *child, bool take_ownership, bool own_buf=true, bool at_tail=true);
	bool RemoveChild(knv_tag_t t, const char *k, uint32_t klen);
	int RemoveChildrenByTag(knv_tag_t _tag); // Returns number of children removed or <0 on failure

	int Remove(); // Remove this node from parent tree
	int Detach(); // Detach this node from parent tree
	bool DetachChild(KnvNode *child);

	// Child Iterations
	int GetChildNum() { Expand(); return child_num<0? 0 : child_num; }
	KnvNode *GetFirstChild();
	KnvNode *GetSibling(); // Get parent's next child
	KnvNode *GetLastChild();
	// GetPrevSibling() never returns NULL, since first->prev() is equal to parent->last()
	// check if it is the first by prev()==parent->first()
	KnvNode *GetPrevSibling();

	// Operations for meta
	KnvNode *GetFirstMeta(); // for iterating metas
	static KnvNode *GetNextMeta(KnvNode *cur);
	KnvNode *GetMeta(knv_tag_t _tag);
	uint64_t GetMetaInt(knv_tag_t _tag);
	string GetMetaStr(knv_tag_t _tag);

	int SetMeta(knv_tag_t _tag, knv_type_t _type, const knv_value_t *_data);
	int SetMetaInt(knv_tag_t _tag, uint64_t _val);
	int SetMetaStr(knv_tag_t _tag, uint32_t _len, const char *_val);

	int RemoveMeta(knv_tag_t _tag);

	// FIXME: UC does not allow meta to be repeated
	// So these interfaces are for non-UC use only
	int AddMeta(knv_tag_t _tag, knv_type_t _type, const knv_value_t *_data);
	int AddMetaInt(knv_tag_t _tag, uint64_t _val);
	int AddMetaStr(knv_tag_t _tag, uint32_t _len, const char *_val);
	int RemoveMetaByTag(knv_tag_t _tag);

	uint64_t GetChildInt(knv_tag_t _tag);
	string GetChildStr(knv_tag_t _tag);

	int SetChild(knv_tag_t _tag, knv_type_t _type, const knv_value_t *_data);
	int SetChildInt(knv_tag_t _tag, uint64_t _val);
	int SetChildStr(knv_tag_t _tag, uint32_t _len, const char *_val);
	int AddChild(knv_tag_t _tag, knv_type_t _type, const knv_value_t *_data);
	int AddChildInt(knv_tag_t _tag, uint64_t _val);
	int AddChildStr(knv_tag_t _tag, int len, const char *_val);

	// Easy way to access a child field (meta or non-meta)
	uint64_t GetFieldInt(knv_tag_t _tag);
	int64_t GetFieldSInt(knv_tag_t _tag);
	float GetFieldFloat(knv_tag_t _tag);
	double GetFieldDouble(knv_tag_t _tag);
	string GetFieldStr(knv_tag_t _tag);
	KnvNode *GetField(knv_tag_t _tag);
	// [ optional ]
	int SetFieldInt(knv_tag_t _tag, uint64_t _val);
	int SetFieldSInt(knv_tag_t _tag, int64_t _val);
	int SetFieldFloat(knv_tag_t _tag, float _val);
	int SetFieldDouble(knv_tag_t _tag, double _val);
	int SetFieldStr(knv_tag_t _tag, uint32_t _len, const char *_val);
	// [ repeated get ]
	int GetFieldsInt(knv_tag_t _tag, vector<uint64_t>& vals);
	int GetFieldsSInt(knv_tag_t _tag, vector<int64_t>& vals);
	int GetFieldsFloat(knv_tag_t _tag, vector<float>& vals);
	int GetFieldsDouble(knv_tag_t _tag, vector<double>& vals);
	int GetFieldsStr(knv_tag_t _tag, vector<string>& vals);
	int GetFields(knv_tag_t _tag, vector<KnvNode *>& fields);
	// [ repeated set ] returns the number of fields
	int AddFieldInt(knv_tag_t _tag, uint64_t _val);
	int AddFieldSInt(knv_tag_t _tag, uint64_t _val);
	int AddFieldFloat(knv_tag_t _tag, uint64_t _val);
	int AddFieldDouble(knv_tag_t _tag, uint64_t _val);
	int AddFieldStr(knv_tag_t _tag, uint32_t _len, const char *_val);
	int RemoveField(knv_tag_t _tag);
	// allow iteration through fields: metas and/or children (non-metas)
	// if _tag is non-zero, all fields of the specified tag are iterated,
	// if _tag is zero, all fields (meta and non-meta) are iterated
	KnvNode *GetFirstField(knv_tag_t _tag=0);
	KnvNode *GetNextField(KnvNode *cur, knv_tag_t _tag=0);

	//对比函数, 与pstNode进行比较
	//如果本Node的数据在pstCmpNode都存在且相等, 则nRetCode返回0, 函数返回NULL
	//如果本Node的数据在pstCmdNode不存在, 或者不相等, 则nRetCode返回0, 函数返回不相等的树
	//如果nRetCode返回1, 说明创建树失败, 函数返回NULL
	KnvNode *Compare(KnvNode *pstNode, int &nRetCode);
public:
	// operations between trees

	// 根据请求树构造并返回对应的数据树，同时返回由无效请求构成的树
	// 请求树的定义：
	//    是一棵普通的树，但：
	//      不一定有key，如果没有指定key，匹配所有tag相同的节点
	//      如果节点为int类型，表示是否请求tag对应节点的数据
	//      如果节点为Node，但没有子节点，表示请求这个节点下所有的子节点
	// 返回：
	//    out_tree -- 存储返回数据的tree,数据部分指向 this，调用者必须保证out_tree先于this被释放
	//    empty_req_tree -- 存储无效请求的请求树,数据指向req_tree，必须先于req_tree被释放掉
	//    这两棵树用完后必须用KnvNode::Delete(tree)删除
	// 返回码：
	//    0  成功
	//    <0 失败
	int GetSubTree(KnvNode *req_tree, KnvNode *(&out_tree), KnvNode *(&empty_req_tree), bool no_empty = false);

	// 用于根据变化的节点淘汰数据
	// 返回：
	//    match_req_tree -- 存储节点匹配的请求tree，是req_tree的一部分，用于回填使用
	// 返回码：
	//    1  成功：删除整棵树（不做任何操作，由上层删除）
	//    0  成功：删除部分子节点
	//    <0 失败
	int DeleteSubTree(KnvNode *req_tree, KnvNode *(&match_req_tree), int depth=0);

	// 根据已有的数据树来更新当前的树，更新步骤：
	//    如果有相同节点，则覆盖原来的节点
	//    否则，插入新的节点
	// 参数：
	//    update_tree -- 用于更新的数据树
	//    max_level   -- 展开的最大深度，大于或等于这个层次时，更新整个节点，不再展开
	int UpdateSubTree(KnvNode *update_tree, int max_level);

	KnvNode *MakeRequestTree(int max_level);

public: // debuging
	void Print(string prefix, ostream &outstr=cout); // print this tree
	const char *GetErrorMsg() { return errmsg; }
private:
	const char *errmsg;

	friend class ObjPool<KnvNode>;
	friend class KnvHt;
};


// inline methods put here

inline KnvNode *KnvNode::New(knv_tag_t _tag, knv_type_t _type, UcMem *val, int length)
{
	knv_value_t v;
	v.str.len = length;
	v.str.data = (char*)val->ptr();
	KnvNode *p = KnvNode::New(_tag, _type, KNV_DEFAULT_TYPE, NULL, &v, false);
	if(p==NULL)
		return NULL;
	p->dyn_data.assign(val, length);
	return p;
}

inline KnvNode *KnvNode::New(const string &bin, bool own_buf)
{
	return KnvNode::New(bin.c_str(), bin.length(), own_buf);
}

inline KnvNode *KnvNode::New(knv_tag_t _tag, knv_type_t _type, const knv_key_t &_key, const knv_value_t *_val, bool own_buf)
{
	return KnvNode::New(_tag, _type, _key.type, &_key.GetValue(), _val, own_buf);
}

inline KnvNode *KnvNode::NewTree(knv_tag_t _tag, const knv_key_t *_key)
{
	return KnvNode::New(_tag, KNV_NODE, _key? _key->type:KNV_DEFAULT_TYPE, _key? &_key->GetValue() : NULL, NULL, true);
}

inline KnvNode *KnvNode::New(const KnvLeaf &l, bool own_buf)
{
	return KnvNode::New(l.GetTag(), l.GetType(), KNV_DEFAULT_TYPE, NULL, &l.GetValue(), own_buf);
}

inline KnvNode *KnvNode::NewFromMessage(const string &msg, knv_tag_t _tag)
{
	knv_value_t v;
	v.str.len = msg.length();
	v.str.data = (char*)msg.data();
	return KnvNode::New(_tag, KNV_STRING, KNV_STRING, NULL, &v, true);
}

inline bool KnvNode::IsMatch(knv_tag_t t, const char *k, int klen)
{
	return (t==tag && key.len==klen && (klen==0 || memcmp(key.val, k, klen)==0));
}

inline int KnvNode::SetValue(knv_type_t _type, const knv_value_t &new_val, bool own_buf)
{
	return (_type==KNV_STRING)? SetValue(new_val.str.data, new_val.str.len, own_buf) : SetValue(new_val.i64);
}

inline KnvNode *KnvNode::InsertChild(knv_tag_t _tag, knv_type_t _type, const knv_value_t *_data, bool own_buf)
{
	KnvNode *c;
	if((c=KnvNode::New(_tag, _type, KNV_DEFAULT_TYPE, NULL, _data, own_buf))==NULL)
	{
		errmsg = GetGlobalErrorMsg();
		return NULL;
	}
	if(InsertChild(c, true, false, true))
	{
		KnvNode::Delete(c);
		return NULL;
	}
	return c;
}

inline KnvNode *KnvNode::InsertChild(knv_tag_t _tag, knv_type_t _type, const knv_key_t &_key, const knv_value_t *_data, bool own_buf)
{
	KnvNode *c;
	if((c=KnvNode::New(_tag,_type,_key,_data,own_buf))==NULL)
	{
		errmsg = GetGlobalErrorMsg();
		return NULL;
	}
	if(InsertChild(c, true, false, true))
	{
		KnvNode::Delete(c);
		return NULL;
	}
	return c;
}

inline KnvNode *KnvNode::InsertSubNode(knv_tag_t _tag, const knv_key_t *_key)
{
	KnvNode *c;
	if((c=KnvNode::NewTree(_tag,_key))==NULL)
	{
		errmsg = GetGlobalErrorMsg();
		return NULL;
	}
	if(InsertChild(c, true, true, true))
	{
		KnvNode::Delete(c);
		return NULL;
	}
	return c;
}

inline KnvNode *KnvNode::InsertIntLeaf(knv_tag_t _tag, uint64_t _val)
{
	knv_value_t v; v.i64 = _val;
	return InsertChild(_tag, KNV_VARINT, &v, true);
}

inline KnvNode *KnvNode::InsertStrLeaf(knv_tag_t _tag, const char *_val, int _len)
{
	knv_value_t v;
	v.str.len = _len;
	v.str.data = (char*)_val;
	return InsertChild(_tag, KNV_STRING, &v, true);
}

inline int KnvNode::AddChildInt(knv_tag_t _tag, uint64_t _val)
{
	return InsertIntLeaf(_tag, _val)? 0 : -1;
}

inline int KnvNode::AddChildStr(knv_tag_t _tag, int _len, const char *_val)
{
	return InsertStrLeaf(_tag, _val, _len)? 0 : -1;
}

inline uint64_t KnvNode::GetIntVal()
{
	if(type==KNV_VARINT || type==KNV_FIXED64)
		return val.i64;
	if(type==KNV_FIXED32) return val.i32;
	return 0;
}

inline string KnvNode::GetStrVal()
{
	const KnvLeaf *l = GetValue();
	if(l && type==KNV_STRING)
	{
		const knv_value_t &v = l->GetValue();
		return string(v.str.data,v.str.len);
	}
	return string();
}

inline uint64_t KnvNode::GetMetaInt(knv_tag_t _tag)
{
	KnvNode *l = GetMeta(_tag);
	if(l) return l->GetIntVal();
	return 0;
}

inline string KnvNode::GetMetaStr(knv_tag_t _tag)
{
	KnvNode *l = GetMeta(_tag);
	if(l) return l->GetStrVal();
	return string();
}

inline int KnvNode::SetMeta(knv_tag_t _tag, knv_type_t _type, const knv_value_t *_data)
{
	if(!no_key && _tag==1) // should be causous about updating key
		return SetKey(_type, _data, true);
	else
		return SetMeta(_tag, _type, _data, true, true);
}

inline int KnvNode::SetMetaInt(knv_tag_t _tag, uint64_t _val)
{
	knv_value_t v;
	v.i64=_val;
	return SetMeta(_tag, KNV_VARINT, &v);
}

inline int KnvNode::SetMetaStr(knv_tag_t _tag, uint32_t _len, const char *_val)
{
	knv_value_t v;
	v.str.len=_len;
	v.str.data=(char*)_val;
	return SetMeta(_tag, KNV_STRING, &v);
}

inline int KnvNode::RemoveMeta(knv_tag_t _tag)
{
	if(_tag==1)
		return SetKey(key.type, NULL, true);
	else
		return InnerRemoveMeta(_tag);
}

inline int KnvNode::AddMetaInt(knv_tag_t _tag, uint64_t _val)
{
	knv_value_t v;
	v.i64=_val;
	return AddMeta(_tag, KNV_VARINT, &v);
}

inline int KnvNode::AddMetaStr(knv_tag_t _tag, uint32_t _len, const char *_val)
{
	knv_value_t v;
	v.str.len=_len;
	v.str.data=(char*)_val;
	return AddMeta(_tag, KNV_STRING, &v);
}

inline uint64_t KnvNode::GetChildInt(knv_tag_t _tag)
{
	KnvNode *n = FindChildByTag(_tag);
	if(n)
		return n->GetIntVal();
	return 0;
}

inline string KnvNode::GetChildStr(knv_tag_t _tag)
{
	KnvNode *n = FindChildByTag(_tag);
	if(n)
		return n->GetStrVal();
	return string();
}

inline int KnvNode::SetChild(knv_tag_t _tag, knv_type_t _type, const knv_value_t *_data)
{
	KnvNode *c = FindChildByTag(_tag);
	if(c)
	{
		if((_type==KNV_STRING && c->type!=_type) ||
			(_type!=KNV_STRING && c->type==KNV_STRING))
		{
			errmsg = "Tag already exists with different type";
			return -2;
		}
		if(c->SetValue(_type, *_data, true))
		{
			errmsg = c->errmsg;
			return -1;
		}
	}
	else
	{
		c = InsertChild(_tag, _type, _data, true);
		return c? 0 : -3;
	}
	return 0;
}

inline int KnvNode::AddChild(knv_tag_t _tag, knv_type_t _type, const knv_value_t *_data)
{
	return InsertChild(_tag, _type, _data, true)? 0 : -1;
}

inline int KnvNode::SetChildInt(knv_tag_t _tag, uint64_t _val)
{
	knv_value_t v;
	v.i64 = _val;
	return SetChild(_tag, KNV_VARINT, &v);
}

inline int KnvNode::SetChildStr(knv_tag_t _tag, uint32_t _len, const char *_val)
{
	knv_value_t v;
	v.str.len = _len;
	v.str.data = (char*)_val;
	return SetChild(_tag, KNV_STRING, &v);
}

inline uint64_t KnvNode::GetFieldInt(knv_tag_t _tag)
{
	if(_tag<=UC_MAX_META_NUM)
		return GetMetaInt(_tag);
	else
		return GetChildInt(_tag);
}

inline int64_t KnvNode::GetFieldSInt(knv_tag_t _tag)
{
	uint64_t i = GetFieldInt(_tag);
	return pb_uint2int(i);
}

inline float KnvNode::GetFieldFloat(knv_tag_t _tag)
{
	uint64_t i = GetFieldInt(_tag);
	return *(float*)&i;
}

inline double KnvNode::GetFieldDouble(knv_tag_t _tag)
{
	uint64_t i = GetFieldInt(_tag);
	return *(double*)&i;
}

inline KnvNode *KnvNode::GetField(knv_tag_t _tag)
{
	if(_tag<=UC_MAX_META_NUM)
		return GetMeta(_tag);
	else
		return FindChildByTag(_tag);
}

inline int KnvNode::GetFieldsInt(knv_tag_t _tag, vector<uint64_t>& vals)
{
	int nr = 0;
	KnvNode *f = GetFirstField(_tag);
	while(f)
	{
		if(f->type!=KNV_STRING)
		{
			vals.push_back(f->val.i64);
			nr ++;
		}
		f = GetNextField(f, _tag);
	}
	return nr;
}

inline int KnvNode::GetFieldsSInt(knv_tag_t _tag, vector<int64_t>& vals)
{
	int nr = 0;
	KnvNode *f = GetFirstField(_tag);
	while(f)
	{
		if(f->type==KNV_VARINT)
		{
			vals.push_back(pb_uint2int(f->val.i64));
			nr ++;
		}
		f = GetNextField(f, _tag);
	}
	return nr;
}

inline int KnvNode::GetFieldsFloat(knv_tag_t _tag, vector<float>& vals)
{
	int nr = 0;
	KnvNode *f = GetFirstField(_tag);
	while(f)
	{
		if(f->type==KNV_FIXED32)
		{
			float ft;
			*(uint32_t*)&ft = f->val.i32;
			vals.push_back(ft);
			nr ++;
		}
		f = GetNextField(f, _tag);
	}
	return nr;
}

inline int KnvNode::GetFieldsDouble(knv_tag_t _tag, vector<double>& vals)
{
	int nr = 0;
	KnvNode *f = GetFirstField(_tag);
	while(f)
	{
		if(f->type==KNV_FIXED64)
		{
			double db;
			*(uint64_t*)&db = f->val.i64;
			vals.push_back(db);
			nr ++;
		}
		f = GetNextField(f, _tag);
	}
	return nr;
}

inline int KnvNode::GetFieldsStr(knv_tag_t _tag, vector<string>& vals)
{
	int nr = 0;
	KnvNode *f = GetFirstField(_tag);
	while(f)
	{
		if(f->type==KNV_STRING)
		{
			vals.push_back(f->GetStrVal());
			nr ++;
		}
		f = GetNextField(f, _tag);
	}
	return nr;
}

inline int KnvNode::GetFields(knv_tag_t _tag, vector<KnvNode *>& fields)
{
	int nr = 0;
	KnvNode *f = GetFirstField(_tag);
	while(f)
	{
		fields.push_back(f);
		nr ++;
		f = GetNextField(f, _tag);
	}
	return nr;
}

inline int KnvNode::SetFieldSInt(knv_tag_t _tag, int64_t _val)
{
	uint64_t i = pb_int2uint(_val);
	return SetFieldInt(_tag, i);
}

inline int KnvNode::SetFieldFloat(knv_tag_t _tag, float _val)
{
	knv_value_t v;
	*(float*)&v.i32 = _val;
	if(_tag<=UC_MAX_META_NUM)
		return SetMeta(_tag, KNV_FIXED32, &v);
	else
		return SetChild(_tag, KNV_FIXED32, &v);
}

inline int KnvNode::SetFieldDouble(knv_tag_t _tag, double _val)
{
	knv_value_t v;
	*(double*)&v.i64 = _val;
	if(_tag<=UC_MAX_META_NUM)
		return SetMeta(_tag, KNV_FIXED64, &v);
	else
		return SetChild(_tag, KNV_FIXED64, &v);
}

inline int KnvNode::AddFieldSInt(knv_tag_t _tag, uint64_t _val)
{
	uint64_t i = pb_int2uint(_val);
	return AddFieldInt(_tag, i);
}

inline int KnvNode::AddFieldFloat(knv_tag_t _tag, uint64_t _val)
{
	knv_value_t v;
	*(float*)&v.i32 = _val;
	if(_tag<=UC_MAX_META_NUM)
		return AddMeta(_tag, KNV_FIXED32, &v);
	else
		return AddChild(_tag, KNV_FIXED32, &v);
}

inline int KnvNode::AddFieldDouble(knv_tag_t _tag, uint64_t _val)
{
	knv_value_t v;
	*(double*)&v.i64 = _val;
	if(_tag<=UC_MAX_META_NUM)
		return AddMeta(_tag, KNV_FIXED64, &v);
	else
		return AddChild(_tag, KNV_FIXED64, &v);
}

inline int KnvNode::RemoveField(knv_tag_t _tag)
{
	if(_tag<=UC_MAX_META_NUM)
		return RemoveMetaByTag(_tag);
	else
		return RemoveChildrenByTag(_tag);
}

inline string KnvNode::GetFieldStr(knv_tag_t _tag)
{
	if(_tag<=UC_MAX_META_NUM)
		return GetMetaStr(_tag);
	else
		return GetChildStr(_tag);
}

inline int KnvNode::SetFieldInt(knv_tag_t _tag, uint64_t _val)
{
	if(_tag<=UC_MAX_META_NUM)
		return SetMetaInt(_tag, _val);
	else
		return SetChildInt(_tag, _val);
}

inline int KnvNode::SetFieldStr(knv_tag_t _tag, uint32_t _len, const char *_val)
{
	if(_tag<=UC_MAX_META_NUM)
		return SetMetaStr(_tag, _len, _val);
	else
		return SetChildStr(_tag, _len, _val);
}

inline int KnvNode::AddFieldInt(knv_tag_t _tag, uint64_t _val)
{
	if(_tag<=UC_MAX_META_NUM)
		return AddMetaInt(_tag, _val);
	else
		return AddChildInt(_tag, _val);
}

inline int KnvNode::AddFieldStr(knv_tag_t _tag, uint32_t _len, const char *_val)
{
	if(_tag<=UC_MAX_META_NUM)
		return AddMetaStr(_tag, _len, _val);
	else
		return AddChildStr(_tag, _len, _val);
}

inline KnvNode *KnvNode::GetFirstField(knv_tag_t _tag)
{
	KnvNode *n;
	if(_tag)
	{
		if(_tag<=UC_MAX_META_NUM)
			n = GetFirstMeta();
		else
			n = GetFirstChild();
		while(n && n->GetTag()!=_tag)
			n = n->GetSibling();
	}
	else
	{
		n = GetFirstMeta();
		if(n==NULL)
			n = GetFirstChild();
	}
	return n;
}

inline KnvNode *KnvNode::GetNextField(KnvNode *cur, knv_tag_t _tag)
{
	if(cur)
	{
		if(_tag)
		{
			cur = cur->GetSibling();
			while(cur && cur->GetTag()!=_tag)
				cur = cur->GetSibling();
			return cur;
		}
		if(cur->next) // has sibling
			return (KnvNode *)cur->next;
		// no sibling, but is the last of metas, return the first child
		if(metalist && metalist->prev==(void*)cur)
			return GetFirstChild();
	}
	return NULL;
}

// Remove this node from parent tree
inline int KnvNode::Remove()
{
	if(parent==NULL) // not in a tree
	{
		KnvNode::Delete(this);
		return 0;
	}

	if(parent->RemoveChildByPos(this, NULL)==false)
	{
		errmsg = "bug: this node not owned by parent";
		return -1;
	}
	return 0;
}

// Detach this node from parent tree
inline int KnvNode::Detach()
{
	if(parent==NULL) // not in a tree
		return 0;
	if(parent->DetachChild(this)==false)
	{
		errmsg = "bug: this node not owned by parent";
		return -1;
	}
	return 0;
}

#endif


