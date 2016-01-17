/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/


// protocol.h
// KNV protocol API, depending on KnvNode
//
// 2013-10-23	Created
// 2013-11-01	Support batch request (mutiple requests in a tree)
// 2014-06-18	Support OIDB protocol format
//

#ifndef __KNV_PROTOCOL__
#define __KNV_PROTOCOL__

#include "knv_node.h"
#include "knv_net.h"
#include <iostream>
#include <sstream>
#include <string>

#define KNV_DEFUALT_MAX_PKG_SIZE    64000
//#define KNV_DEFUALT_MAX_PKG_SIZE    512

#define KNV_PKG_TAG             0xdb3 // paket  tag, first 3 bytes: 9A DB 01
#define KNV_PKG_HDR_TAG         0xbad // header tag, first 3 bytes: EA BA 01
#define KNV_PKG_BDY_TAG         0xdad // body   tag, first 3 bytes: EA DA 01
#define KNV_PKG_UNFINISHED_TAG  0xddd // for batch request response: tag for unfinished keys, each key is a repeated field with tag 1
#define KNV_PKG_PART_TAG_BASE   0x1ee // splitted partial tag base
                                      // the k's splitted packet has tag KNV_PKG_PART_BDY_TAG_BASE+k
                                      // tags in range 0x1ee~0x2ee are reserved for partitions
#define KNV_PKG_HDR_KEY_TAG     1
#define KNV_PKG_HDR_CMD_TAG     2
#define KNV_PKG_HDR_SUBCMD_TAG  3
#define KNV_PKG_HDR_SEQ_TAG     4
#define KNV_PKG_HDR_RET_TAG     7
#define KNV_PKG_HDR_ERR_TAG     8
// tags 2001-2999 are reserved for KNV header metas
#define KNV_PKG_HDR_RSP_ADDR              2001 // all commands support specifying response address
#define KNV_PKG_HDR_ALLOW_SPLIT           2002 // allow packet splitting when a rsp pakcet excceeds max_pkg_size
#define KNV_PKG_HDR_MAX_PKG_SIZE          2003 // user supplied max_pkg_size for splitting
#define KNV_PKG_HDR_TOTAL_SPLIT_COUNT     2004 // totoal splitted packet count
#define KNV_PKG_HDR_CURR_SPLIT_INDEX      2005 // current index (0~(total-1))

#define KNV_DM_TAGLIST_TAG      2 // tag number of taglist in domain node, used in packing request for union session
#define KNV_DM_UPDATE_TIME_TAG  2 // tag number of update time in domain node, updated on write
#define KNV_DM_ACCESS_SEQ_TAG   3 // tag number of access sequence in domain node

#define KNV_SK_UPDATE_TIME_TAG  2 // tag number of update time in subkey, auto updated on write


// Oidb Ipv6 packet tokens
#define STX_IPV6_PB 0x28
#define ETX_IPV6_PB 0x29

// KNV packet tokens
#define STX_KNV    	0x9adb
#define STX_KNV_HDR	0xeaba
#define STX_KNV_BDY	0xeada

using namespace std;

// accessible through UcProtocol
#define UcProtocol KnvProtocol

// This is an  encapsulation for KnvNode
class KnvProtocol
{
private:
	KnvNode *tree; // the whole protocol tree, invisible to user

	KnvNode *header; // header part
	// for batch operation: body points to current (first when decoded) req,
	// use GetFirstRequest()/GetNextRequest() to iterate all requests
	KnvNode *body;   // body part

	// Header infos we care about
	uint32_t cmd;       // cmd parsed from header
	uint32_t subcmd;    // subcmd parsed from header
	uint64_t seq;       // sequence number
	uint32_t retcode;   // return code
	uint32_t retmsglen; // return msg len when return code is not 0
	char    *retmsg;    // return msg
	KnvNet::KnvSockAddr rspaddr; // reply address, if rspaddr.addrlen==0, use client address

	// splitting-support parameters in header
	uint8_t  allow_split;
	uint16_t max_pkg_sz;
	uint8_t total_split_count;
	uint8_t curr_split_index;

	string errmsg;
	bool auto_delete; // delete tree on destruction

public:
	uint32_t GetCommand() const { return cmd; }
	uint32_t GetSubCommand() const { return subcmd; }
	uint64_t GetSequence() const { return seq; }
	uint32_t GetRetCode() const { return retcode; }
	string GetRetMsg() const { string s(retmsg, retmsglen); return s; }
	const KnvNet::KnvSockAddr &GetRspAddr() const { return rspaddr; }

public:
	// construct but not initialize, user should call assign() before using it
	KnvProtocol():tree(NULL),header(NULL),body(NULL) {}

	// construct from a existing protocol
	// if take_ownership is true, proto will no longer owns the tree, deep_copy is not used
	// if take_ownership is false, we will not own the tree, set deep_copy=true to clone a new protocol tree
	KnvProtocol(KnvProtocol &proto, bool take_ownership, bool deep_copy = false);

	// construct from proto header/body nodes
	// if take_ownership is true, hdr/bdy will be auto-deleted by KnvProtocol (even if init failed)
	KnvProtocol(KnvNode *hdr, KnvNode *bdy, bool take_ownership = true);

	// construct from decoding a stream, use IsValid() to check whether decoding is successful
	KnvProtocol(const char *buf, int buf_len, bool own_buf = true);
	KnvProtocol(const string &buf, bool own_buf = true);

	// construct a new tree with header info
	KnvProtocol(uint32_t dwCmd, uint32_t dwSubCmd, uint32_t dwSeq);

	~KnvProtocol() { Delete(); } // auto delete if needed
	void Delete();

	// decode
	bool IsValid();
	KnvNode *GetHeader();
	KnvNode *GetBody();
	const knv_key_t *GetKey();

	// own_buf -- control where the protocal can use augument's buffer
	//   true : create a new buffer and perform a deep copy
	//   false : protocol's value points to existing buffer, this is a shallow copy
	int assign(KnvProtocol &prot, bool own_buf = true);
	int assign(const char *buff, int len, bool own_buf = true);

	// For batch operations
	KnvNode *GetFirstRequest(); // First
	KnvNode *GetNextRequest(); // Next

	// Add a new request body to the protocol tree
	// b==NULL -- construct a new body
	// b!=NULL -- copy or simply use b depending on take_ownership
	// take_ownership:
	//   true  -- KnvProtocol is responsible for deleting b
	//   false -- User need to delete b by himself
	int AddBody(KnvNode *b, bool take_ownership = true);
	int AddBody(knv_type_t keytype, const knv_value_t &key);
	int AddBody(const knv_key_t &key);

	// allow user to reuse the protocol tree with only header part
	int RemoveAllBodies();

	int ReassignBody(KnvNode *new_body, bool take_ownership = true);

	// Domain iterations, operated on current body
	int GetDomainNum();
	KnvNode *GetFirstDomain();
	// Use KnvNode::GetSibling() to get next domain
	// Use KnvNode::GetTag() to get domain ID
	#define GetDomainID()	GetTag()

	// Get domain from current request body
	// Use GetFirstRequest()/GetNextRequest()/AddBody() to change current request
	KnvNode *GetDomain(int iDomainId);

	// Add a new domain to the current body
	// Use GetFirstRequest()/GetNextRequest()/AddBody() to change current request body
	KnvNode *AddDomain(int iDomainId);

	// Add an existing domain to the current body
	// Use GetFirstRequest()/GetNextRequest()/AddBody() to change current request body
	int AddDomain(KnvNode *domain, bool take_ownership = false);

	int RemoveDomain(int iDomainId);

	// Encode the protocol tree to a string
	// Note that retcode/retmsg will effect the output, call SetRetCode()/SetRetErrorMsg() to update them
	// the user is responsible for calling UcMemManager::Free(mem) to free memory
	int Encode(UcMem *(&mem));
	int EncodeWithError(uint32_t ret, const string &errmsg, UcMem *(&mem));
	int EncodeWithBody(KnvNode *b, UcMem *(&mem));
	int EncodeCompatOidb(UcMem *(&mem));

	// string version
	int Encode(string &s);
	int EncodeWithError(uint32_t ret, const string &errmsg, string &s);
	int EncodeWithBody(KnvNode *b, string &s);

	// OidbIPv6 encoding in UC style, i.e. multiple bodies are encoded inside the OIDB's body
	int EncodeOidb(string &s);
	int EncodeOidbWithError(uint32_t ret, const string &errmsg, string &s);
	int EncodeOidbWithBody(KnvNode *b, string &s);

	// OidbIPv6 encoding in compatible mode, only one body is encoded
	// If you are writing non-UC code, you probably should call this
	int EncodeCompatOidb(string &s);
	int EncodeCompatOidbWithError(uint32_t ret, const string &errmsg, string &s);
	int EncodeCompatOidbWithBody(KnvNode *b, string &s);

	// Allowing you to get/update fields in header
	// updated fields will be encoded in the string
	uint64_t GetHeaderIntField(uint32_t ftag);
	string GetHeaderStringField(uint32_t ftag);
	int SetHeaderIntField(uint32_t ftag, uint64_t new_val);
	int SetHeaderStringField(uint32_t ftag, uint32_t new_len, char *new_val);
	int SetCommand(uint32_t new_cmd) { return SetHeaderIntField(KNV_PKG_HDR_CMD_TAG, new_cmd); }
	int SetSubCommand(uint32_t new_subcmd) { return SetHeaderIntField(KNV_PKG_HDR_SUBCMD_TAG, new_subcmd); }
	int SetSequence(uint64_t new_seq) { return SetHeaderIntField(KNV_PKG_HDR_SEQ_TAG, new_seq); }
	int SetRetCode(uint32_t new_retcode) { return SetHeaderIntField(KNV_PKG_HDR_RET_TAG, new_retcode); }
	int SetRetErrorMsg(const char *msg, int len) { return SetHeaderStringField(KNV_PKG_HDR_ERR_TAG, len, (char*)msg); }
	int SetRspAddr(const KnvNet::KnvSockAddr &addr);

	int EvalMaxSize(); // calculate the message size

	// Splitting support, body in part will become invalid
	bool GetAllowSplit() const { return allow_split!=0; }
	uint16_t GetMaxPkgSize() const { return (max_pkg_sz<128 || max_pkg_sz>KNV_DEFUALT_MAX_PKG_SIZE)? KNV_DEFUALT_MAX_PKG_SIZE : max_pkg_sz; }
	void SetAllowSplit(bool allow, uint32_t pkg_sz=0); // set allow splitting for sending a packet
	int SetReqSplit(bool allow, uint32_t pkg_sz=0); // set whether the peer could split the reply packet or not
	bool IsComplete() { return IsValid() && (retcode!=0 || total_split_count==0 || GetBody() != NULL); }
	int AddPartial(KnvProtocol &part, bool own_buf=true); // if part's buffer can be used by this, set own_buf to false
	int Split(KnvNode *b = NULL); // if b is NULL, split GetBody()
	int GetTotalPartNum() const { return total_split_count; }
	int EncodePart(int index, UcMem *(&mem));
	int EncodePart(int index, string &s);

	// Debugging
	int Print(const string &prefix=string(""), ostream &outstr=cout);
	string PrintToString(const string &prefix=string(""));
	const string &GetErrorMsg() const { return errmsg; }

private:
	// Encode with specified arguments
	// return the number of bytes encoded or < 0 on failure
	int Encode(UcMem *(&mem), uint32_t ret, const char *err, int errlen, KnvNode *body_tree, bool encode_oidb = false, bool compat_oidb = false);
	// encode the whole tree, user is responsible to setup everything in the protocol tree
	// return the number of bytes encoded or < 0 on failure
	int EncodeAll(UcMem *(&mem), bool encode_oidb = false, bool compat_oidb = false);

	// string version
	int Encode(string &s, uint32_t ret, const char *err, int errlen, KnvNode *body_tree, bool encode_oidb = false, bool compat_oidb = false);
	int EncodeAll(string &s, bool encode_oidb = false, bool compat_oidb = false);
	int EncodeOidb(UcMem *(&mem), KnvNode *body_tree); // encode oidb with a given tree
	int EncodeAllOidb(UcMem *(&mem)); // encode the whole tree to oidb
	int EncodeCompatOidb(UcMem *(&mem), KnvNode *body_tree);

	void InitFromOidbPkg(const char *buf, int buflen, bool own_buf);
};

inline int KnvProtocol::assign(KnvProtocol &prot, bool own_buf)
{
	cmd = prot.cmd;
	subcmd = prot.subcmd;
	seq = prot.seq;
	retcode = prot.retcode;
	rspaddr = prot.rspaddr;
	allow_split = prot.allow_split;
	max_pkg_sz = prot.max_pkg_sz;
	total_split_count = prot.total_split_count;
	curr_split_index = prot.curr_split_index;

	Delete();
	if(own_buf)
	{
		retmsglen = 0;
		retmsg = NULL;
		tree = prot.tree->Duplicate(true);
		if(tree==NULL)
		{
			errmsg = "Out of memory";
			return -1;
		}
		header = tree->FindChildByTag(KNV_PKG_HDR_TAG);
		if(header==NULL)
		{
			errmsg = "No header in protocol";
			KnvNode::Delete(tree);
			tree = NULL;
			return -2;
		}
		KnvLeaf *m = header->GetMeta(KNV_PKG_HDR_ERR_TAG);
		if(m && m->GetType()==KNV_STRING)
		{
			retmsglen = m->GetValue().str.len;
			retmsg = m->GetValue().str.data;
		}
		body = tree->FindChildByTag(KNV_PKG_BDY_TAG);
		auto_delete = true;
	}
	else
	{
		tree = prot.tree;
		header = prot.header;
		body = prot.body;
		retmsglen = prot.retmsglen;
		retmsg = prot.retmsg;
		if(prot.auto_delete)
		{
			auto_delete = true;
			prot.auto_delete = false;
		}
		else
		{
			auto_delete = false; // pointing to somewhere else no one knows
		}
	}
	return 0;
}

inline void KnvProtocol::Delete()
{
	if(auto_delete && tree)
		KnvNode::Delete(tree);
	tree = header = body = NULL;
}

inline bool KnvProtocol::IsValid()
{
	return tree!=NULL && tree->GetTag()==KNV_PKG_TAG;
}

inline KnvNode *KnvProtocol::GetHeader()
{
	return IsValid()? header:NULL;
}

inline KnvNode *KnvProtocol::GetBody()
{
	return (IsValid() && body && body->IsValid())? body:NULL;
}

inline const knv_key_t *KnvProtocol::GetKey()
{
	return (IsValid() && body && body->IsValid())? &body->GetKey() : NULL;
}

inline uint64_t KnvProtocol::GetHeaderIntField(uint32_t ftag)
{
	if(header)
		return header->GetFieldInt(ftag);
	return 0;
}

inline string KnvProtocol::GetHeaderStringField(uint32_t ftag)
{
	static string empty_str;
	if(header)
		return header->GetFieldStr(ftag);
	return empty_str;
}

// User should not use directly
inline KnvNode *FindFirstBody(KnvNode *b)
{
	while(b && b->GetTag()!=KNV_PKG_BDY_TAG)
		b = b->GetSibling();
	return b;
}

inline KnvNode *KnvProtocol::GetFirstRequest()
{
	KnvNode *b = FindFirstBody(tree? tree->GetFirstChild():NULL);
	return b? (body=b):NULL;
}

inline KnvNode *KnvProtocol::GetNextRequest()
{
	KnvNode *b = FindFirstBody(body? body->GetSibling():NULL);
	return b? (body=b):NULL;
}

inline int KnvProtocol::AddBody(knv_type_t keytype, const knv_value_t &key)
{
	KnvNode *n = KnvNode::New(KNV_PKG_BDY_TAG, KNV_NODE, keytype, &key, NULL, true);
	if(n)
		return AddBody(n, true);
	errmsg = "Out of memory";
	return -1;
}

inline int KnvProtocol::AddBody(const knv_key_t &key)
{
	KnvNode *n = KnvNode::NewTree(KNV_PKG_BDY_TAG, &key);
	if(n)
		return AddBody(n, true);
	errmsg = "Out of memory";
	return -1;
}

inline int KnvProtocol::RemoveAllBodies() // allow user to reuse the protocol tree with only header part
{
	if(!IsValid())
	{
		errmsg = "Protocol not initialized";
		return -1;
	}
	if(tree->RemoveChildrenByTag(KNV_PKG_BDY_TAG)<0)
	{
		errmsg = "Failed to remove body tag: ";
		errmsg += tree->GetErrorMsg();
		return -2;
	}
	body = NULL;
	return 0;
}

inline int KnvProtocol::ReassignBody(KnvNode *new_body, bool take_ownership)
{
	if(RemoveAllBodies())
		return -1;
	if(new_body && AddBody(new_body, take_ownership))
		return -2;
	return 0;
}

inline int KnvProtocol::GetDomainNum()
{
	if(IsValid() && body)
		return body->GetChildNum();
	return 0;
}

inline KnvNode *KnvProtocol::GetFirstDomain()
{
	if(body) return body->GetFirstChild();
	return NULL;
}

inline KnvNode *KnvProtocol::GetDomain(int iDomainId)
{
	if(!IsValid() || body==NULL || !body->IsValid())
		return NULL;
	// The current definition, domain has no key field
	return body->FindChild(iDomainId, NULL, 0);
}

inline KnvNode *KnvProtocol::AddDomain(int iDomainId)
{
	if(!IsValid() || body==NULL || !body->IsValid())
	{
		errmsg = "Protocol not initialized";
		return NULL;
	}
	KnvNode *n = body->FindChild(iDomainId, NULL, 0);
	if(n==NULL)
	{
		n = body->InsertChild(iDomainId, KNV_NODE, NULL, true);
		if(n==NULL)
		{
			errmsg = body->GetErrorMsg();
			return NULL;
		}
	}
	return n;
}

inline int KnvProtocol::AddDomain(KnvNode *domain, bool take_ownership)
{
	if(!IsValid() || body==NULL || !body->IsValid())
	{
		errmsg = "Protocol not initialized";
		return -1;
	}
	KnvNode *n = body->FindChild(domain->GetTag(), NULL, 0);
	if(n)
	{
		errmsg = "There is already a domain with the same ID, please remove it first";
		return -2;
	}
	if(body->InsertChild(domain, take_ownership, true))
	{
		errmsg = body->GetErrorMsg();
		return -3;
	}
	return 0;
}


inline int KnvProtocol::RemoveDomain(int iDomainId)
{
	if(!IsValid() || body==NULL || !body->IsValid())
	{
		errmsg = "Protocol not initialized";
		return -1;
	}
	if(body->RemoveChildrenByTag(iDomainId)<0)
	{
		errmsg = string("RemoveChildrenByTag failed: ") + body->GetErrorMsg();
		return -2;
	}
	return 0;
}


inline int KnvProtocol::EvalMaxSize()
{
	if(!IsValid())
	{
		errmsg = "Protocol not initialized";
		return -1;
	}
	return tree->EvaluateSize();
}

inline string KnvProtocol::PrintToString(const string &prefix)
{
	string s;
	s.clear();
	ostringstream oss;
	if(Print(prefix, oss))
		return s;
	return (s=oss.str());
}

inline int KnvProtocol::EncodeCompatOidb(UcMem *(&mem))
{
	return retcode? Encode(mem, retcode, retmsg, retmsglen, NULL, true, true) : EncodeAll(mem, true, true);
}

inline int KnvProtocol::Encode(UcMem *(&mem))
{
	return retcode? Encode(mem, retcode, retmsg, retmsglen, NULL) : EncodeAll(mem);
}

inline int KnvProtocol::EncodeWithError(uint32_t ret, const string &errmsg, UcMem *(&mem))
{
	return Encode(mem, ret, errmsg.c_str(), errmsg.length(), NULL);
}

inline int KnvProtocol::EncodeWithBody(KnvNode *b, UcMem *(&mem))
{
	return Encode(mem, 0, NULL, 0, b);
}

inline int KnvProtocol::Encode(string &s)
{
	return retcode? Encode(s, retcode, retmsg, retmsglen, NULL) : EncodeAll(s);
}

inline int KnvProtocol::EncodeWithError(uint32_t ret, const string &errmsg, string &s)
{
	return Encode(s, ret, errmsg.c_str(), errmsg.length(), NULL);
}

inline int KnvProtocol::EncodeWithBody(KnvNode *b, string &s)
{
	return Encode(s, 0, NULL, 0, b);
}

inline int KnvProtocol::EncodeAll(UcMem *(&mem), bool encode_oidb, bool compat_oidb)
{
	if(!IsValid())
	{
		errmsg = "Protocol is not initialized";
		return -1;
	}

	if(encode_oidb)
	{
		if(compat_oidb)
			return EncodeCompatOidb(mem, body);
		else
			return EncodeAllOidb(mem);
	}

	int sz = tree->EvaluateSize();
	mem = UcMemManager::Alloc(sz);
	if(mem==NULL)
	{
		errmsg = "UcMemManager::Alloc failed";
		return -2;
	}

	if(tree->Serialize((char*)mem->ptr(), sz))
	{
		UcMemManager::Free(mem); mem = NULL;
		errmsg = "Serializing tree failed: ";
		if(tree->GetErrorMsg())
			errmsg += tree->GetErrorMsg();
		else
			errmsg += "Unknown error";
		return -3;
	}
	return sz;
}

inline int KnvProtocol::EncodeOidb(string &s)
{
	return retcode? Encode(s, retcode, retmsg, retmsglen, NULL, true) : EncodeAll(s, true);
}

inline int KnvProtocol::EncodeOidbWithError(uint32_t ret, const string &errmsg, string &s)
{
	return Encode(s, ret, errmsg.c_str(), errmsg.length(), NULL, true);
}

inline int KnvProtocol::EncodeOidbWithBody(KnvNode *b, string &s)
{
	return Encode(s, 0, NULL, 0, b, true);
}

inline int KnvProtocol::EncodeCompatOidb(string &s)
{
	return retcode? Encode(s, retcode, retmsg, retmsglen, NULL, true, true) : EncodeAll(s, true, true);
}

inline int KnvProtocol::EncodeCompatOidbWithError(uint32_t ret, const string &errmsg, string &s)
{
	return Encode(s, ret, errmsg.c_str(), errmsg.length(), NULL, true, true);
}

inline int KnvProtocol::EncodeCompatOidbWithBody(KnvNode *b, string &s)
{
	return Encode(s, 0, NULL, 0, b, true, true);
}

inline int KnvProtocol::Encode(string &s, uint32_t ret, const char *err, int errlen, KnvNode *body_tree, bool encode_oidb, bool compat_oidb)
{
	UcMem *m;
	int l = Encode(m, ret, err, errlen, body_tree, encode_oidb, compat_oidb);
	if(l<0)
		return l;
	try
	{
		s.assign((char*)m->ptr(), l);
	}
	catch(...)
	{
		errmsg = "out of memory";
		return -100;
	}
	UcMemManager::Free(m);
	return 0;
}

// set allow splitting for sending a packet
inline void KnvProtocol::SetAllowSplit(bool allow, uint32_t pkg_sz)
{
	allow_split = allow;
	if(pkg_sz) max_pkg_sz = pkg_sz;
}

// set whether the peer could reply with splitted packet or not
inline int KnvProtocol::SetReqSplit(bool allow, uint32_t pkg_sz)
{
	int ret;

	if(!IsValid())
	{
		errmsg = "protocol is not initialized";
		return -1;
	}

	if(allow)
		ret = header->SetChildInt(KNV_PKG_HDR_ALLOW_SPLIT, 1);
	else
		ret = header->RemoveChildrenByTag(KNV_PKG_HDR_ALLOW_SPLIT);
	if(ret)
	{
		errmsg = "Set header allow field failed: ";
		errmsg += header->GetErrorMsg();
		return -2;
	}

	if(pkg_sz && header->SetChildInt(KNV_PKG_HDR_MAX_PKG_SIZE, pkg_sz))
	{
		errmsg = "Set header max_pkg field failed: ";
		errmsg += header->GetErrorMsg();
		return -3;
	}
	return 0;
}

inline int KnvProtocol::EncodePart(int index, string &s)
{
	UcMem *m;
	int l = EncodePart(index, m);
	if(l<0)
		return l;
	try
	{
		s.assign((char*)m->ptr(), l);
	}
	catch(...)
	{
		errmsg = "out of memory";
		return -100;
	}
	UcMemManager::Free(m);
	return 0;
}

inline int KnvProtocol::EncodeAll(string &s, bool encode_oidb, bool compat_oidb)
{
	UcMem *m;
	int l = EncodeAll(m, encode_oidb, compat_oidb);
	if(l<0)
		return l;
	try
	{
		s.assign((char*)m->ptr(), l);
	}
	catch(...)
	{
		errmsg = "out of memory";
		return -100;
	}
	UcMemManager::Free(m);
	return 0;
}

#endif

