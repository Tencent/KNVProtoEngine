/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <fstream>
#include <iostream>
#include <map>

#include "knv_codec.h"
#include "protocol.h"

#define INIT_HEADER_INFO() do{ \
	cmd = 0; \
	subcmd = 0; \
	seq = 0; \
	retcode = 0; \
	retmsglen = 0; \
	retmsg = NULL; \
	rspaddr.addr_len = 0; \
	allow_split = max_pkg_sz = total_split_count = curr_split_index = 0; \
}while(0)

#define InitHeaderInfo() (\
{\
	INIT_HEADER_INFO();\
\
	if(header)\
	{\
		KnvLeaf *m = header->GetMeta(KNV_PKG_HDR_CMD_TAG);\
		if(m && m->GetType()==KNV_VARINT)\
		{\
			cmd = m->GetValue().i32;\
		}\
\
		m = header->GetMeta(KNV_PKG_HDR_SUBCMD_TAG);\
		if(m && m->GetType()==KNV_VARINT)\
		{\
			subcmd = m->GetValue().i32;\
		}\
\
		m = header->GetMeta(KNV_PKG_HDR_RET_TAG);\
		if(m && m->GetType()==KNV_VARINT)\
		{\
			retcode = m->GetValue().i32;\
		}\
\
		m = header->GetMeta(KNV_PKG_HDR_ERR_TAG);\
		if(m && m->GetType()==KNV_STRING)\
		{\
			retmsglen = m->GetValue().str.len;\
			retmsg = m->GetValue().str.data;\
		}\
\
		m = header->GetMeta(KNV_PKG_HDR_SEQ_TAG);\
		if(m && m->GetType()==KNV_VARINT)\
		{\
			seq = m->GetValue().i64;\
		}\
\
		m = header->FindChildByTag(KNV_PKG_HDR_RSP_ADDR);\
		if(m && m->GetType()==KNV_STRING && \
			(m->GetValue().str.len==sizeof(rspaddr.a4) || m->GetValue().str.len==sizeof(rspaddr.a6)))\
		{\
			rspaddr.addr_len = m->GetValue().str.len;\
			memcpy(&rspaddr.addr, m->GetValue().str.data, m->GetValue().str.len);\
		}\
		m = header->FindChildByTag(KNV_PKG_HDR_ALLOW_SPLIT);\
		if(m && m->GetType()==KNV_VARINT && m->GetValue().i64)\
		{\
			allow_split = true;\
		}\
		m = header->FindChildByTag(KNV_PKG_HDR_MAX_PKG_SIZE);\
		if(m && m->GetType()==KNV_VARINT && m->GetValue().i64)\
		{\
			max_pkg_sz = m->GetValue().i64;\
		}\
		m = header->FindChildByTag(KNV_PKG_HDR_TOTAL_SPLIT_COUNT);\
		if(m && m->GetType()==KNV_VARINT && m->GetValue().i64)\
		{\
			total_split_count = m->GetValue().i64;\
		}\
		m = header->FindChildByTag(KNV_PKG_HDR_CURR_SPLIT_INDEX);\
		if(m && m->GetType()==KNV_VARINT && m->GetValue().i64)\
		{\
			curr_split_index = m->GetValue().i64;\
		}\
	}\
	0;\
})

#define EXPAND_TREE() ({ \
	int _exp_r = 0;\
	if(header==NULL)\
	{\
		body = FindFirstBody(tree->GetFirstChild()); \
		header = tree->FindChildByTag(KNV_PKG_HDR_TAG); \
		if(header==NULL) /* body can be NULL but header can not */ \
		{\
			tree->Print("[no_header]");\
			if(auto_delete) KnvNode::Delete(tree);\
			tree = body = NULL;\
			errmsg = "Protocol has no header part";\
			_exp_r = -1;\
		}\
	}\
	_exp_r;\
})

#define InitProtocol() (\
{\
	if(!tree || tree->GetTag()!=KNV_PKG_TAG || EXPAND_TREE())\
	{\
		if(tree && auto_delete) KnvNode::Delete(tree);\
		tree = NULL;\
		header = NULL;\
		body = NULL;\
	}\
	InitHeaderInfo();\
})


KnvProtocol::KnvProtocol(KnvProtocol &proto, bool take_ownership, bool deep_copy): \
	tree(proto.tree), header(NULL), body(NULL), retmsg(NULL), auto_delete(take_ownership)
{
	if(!proto.IsValid())
	{
		tree = NULL;
		errmsg = "proto is invalid";
	}
	if(take_ownership)
	{
		if(proto.auto_delete)
			proto.auto_delete = false;
		else // proto don't have ownership
		{
			if(proto.tree)
			{
				tree = proto.tree->Duplicate(true);
				if(tree==NULL)
				{
					errmsg = proto.tree->GetErrorMsg();
				}
			}
		}
	}
	else if(deep_copy && tree)
	{
		auto_delete = true;
		tree = tree->Duplicate(true); // use own buffer
		if(tree==NULL)
		{
			errmsg = proto.tree->GetErrorMsg();
		}
	}
	InitProtocol();
}

KnvProtocol::KnvProtocol(KnvNode *hdr, KnvNode *bdy, bool take_ownership): \
	tree(KnvNode::NewTree(KNV_PKG_TAG)), header(hdr), body(bdy), retmsg(NULL), auto_delete(true)
{
	if(tree==NULL)
	{
failure:
		if(take_ownership) // we have to make sure no memory leakage happens on failure
		{
			if(header) KnvNode::Delete(header);
			if(body) KnvNode::Delete(body);
		}
	}
	else
	{
		if(!header || tree->InsertChild(header, take_ownership, true))
		{
			KnvNode::Delete(tree); tree = NULL;
			goto failure;
		}

		if(body && tree->InsertChild(body, take_ownership, true))
		{
			// header is inserted into tree, but body is not
			KnvNode::Delete(tree); tree = NULL; // will also delete header
			if(take_ownership) KnvNode::Delete(body);
		}
		if(!take_ownership) // If we don't own hdr/bdy, they will be expanded from tree
		{
			body = header = NULL;
			if(EXPAND_TREE()) tree = NULL;
		}
	}
	InitProtocol();
}

void KnvProtocol::InitFromOidbPkg(const char *buf, int buflen, bool own_buf)
{
	//
	// OIDB packet
	// STX + dwHeadLen + dwBodyLen + { header content } + { {body1} {body2} ...  }
	//
	if(buflen<10)
	{
		errmsg = "Bad OidbIpv6 packet: insufficient length";
		return;
	}

	uint32_t hlen = ntohl(*(uint32_t*)(buf+1));
	uint32_t blen = ntohl(*(uint32_t*)(buf+5));
	int total_len = hlen+blen+10;

	if(total_len > buflen || buf[total_len-1]!=ETX_IPV6_PB)
	{
		errmsg = "Bad OidbIpv6 packet: ";
		if(total_len > buflen)
			errmsg += "insufficient length";
		else
			errmsg += "ETX token missing";
		return;
	}

	knv_value_t v;
	v.str.data = (char *)(buf+9);
	v.str.len = hlen;
	header = KnvNode::New(KNV_PKG_HDR_TAG, KNV_NODE, KNV_VARINT, NULL, &v, own_buf);
	if(header==NULL)
	{
		errmsg = "Construct header failed: ";
		errmsg += KnvNode::GetGlobalErrorMsg();
		return;
	}

	//KNV supports multiple bodies, but OIDB allows only one body
	//hence, we pack all bodies together into OIDB's body
	//reversedly, when unpacking, the OIDB's body is treated as the KNV's body collection
	if(ntohs(*(uint16_t*)(buf+9+hlen))==STX_KNV_BDY)
	{
		v.str.data = (char*)(buf+9+hlen);
		v.str.len = blen;
		tree = KnvNode::New(KNV_PKG_TAG, KNV_NODE, KNV_VARINT, NULL, &v, own_buf);
		if(tree==NULL)
		{
			errmsg = "Construct knv tree failed: ";
			errmsg += KnvNode::GetGlobalErrorMsg();
			KnvNode::Delete(header); header = NULL;
			return;
		}
		if(tree->InsertChild(header, true, true, false)<0) // insert in front
		{
			errmsg = "Insert header to knv tree failed: ";
			errmsg += tree->GetErrorMsg();
			KnvNode::Delete(header); header = NULL;
			KnvNode::Delete(tree); tree = NULL;
			return;
		}
	}
	else // compat with non-KNV style PB
	{
		v.str.data = (char*)(buf+9+hlen);
		v.str.len = blen;
		body = KnvNode::New(KNV_PKG_BDY_TAG, KNV_NODE, KNV_VARINT, NULL, &v, own_buf);
		if(body)
			tree = KnvNode::NewTree(KNV_PKG_TAG);
		if(body==NULL || tree==NULL)
		{
			errmsg = "Construct knv tree failed: ";
			errmsg += KnvNode::GetGlobalErrorMsg();
			KnvNode::Delete(header); header = NULL;
			if(body) { KnvNode::Delete(body); body = NULL; }
			return;
		}
		if(tree->InsertChild(header, true)<0)
		{
			errmsg = "Insert header to knv tree failed: ";
			errmsg += tree->GetErrorMsg();
			KnvNode::Delete(header); header = NULL;
			KnvNode::Delete(body); body = NULL;
			KnvNode::Delete(tree); tree = NULL;
			return;
		}
		if(tree->InsertChild(body, true)<0)
		{
			errmsg = "Insert body to knv tree failed: ";
			errmsg += tree->GetErrorMsg();
			KnvNode::Delete(body); body = NULL;
			KnvNode::Delete(tree); tree = NULL;
			header = NULL;
			return;
		}
	}
}

KnvProtocol::KnvProtocol(const string &buf, bool own_buf): \
	tree(NULL), header(NULL), body(NULL), retmsg(NULL), auto_delete(true)
{
	if(buf[0]==STX_IPV6_PB) // OidbIpv6 packet
	{
		InitFromOidbPkg(buf.data(), buf.length(), own_buf);
	}
	else // Knv packet
	{
		tree = KnvNode::New(buf, own_buf);
		if(tree==NULL)
		{
			errmsg = "Construct knv tree failed: ";
			errmsg += KnvNode::GetGlobalErrorMsg();
		}
	}
	InitProtocol();
}

KnvProtocol::KnvProtocol(const char *buf, int buf_len, bool own_buf): \
	tree(NULL), header(NULL), body(NULL), retmsg(NULL), auto_delete(true)
{
	if(buf && buf[0]==STX_IPV6_PB) // OidbIpv6 packet
	{
		InitFromOidbPkg(buf, buf_len, own_buf);
	}
	else // Knv packet
	{
		tree = KnvNode::New(buf, buf_len, own_buf);
		if(tree==NULL)
		{
			errmsg = "Construct knv tree failed: ";
			errmsg += KnvNode::GetGlobalErrorMsg();
		}
	}
	InitProtocol();
}

int KnvProtocol::assign(const char *buf, int buf_len, bool own_buf)
{
	KnvNode *tr = KnvNode::New(buf, buf_len, own_buf);
	if(tr==NULL)
	{
		errmsg = "Construct KnvNode failed: "; errmsg += KnvNode::GetGlobalErrorMsg();
		return -1;
	}
	Delete();
	tree = tr;
	header = body = NULL;
	auto_delete = true;
	InitProtocol();
	return 0;
}

// construct a new tree with header info
KnvProtocol::KnvProtocol(uint32_t dwCmd, uint32_t dwSubCmd, uint32_t dwSeq): \
	tree(NULL), header(NULL), body(NULL), retmsg(NULL), auto_delete(true)
{
	InitProtocol(); // empty the protocol

	tree = KnvNode::NewTree(KNV_PKG_TAG);
	if(tree && (header=tree->InsertSubNode(KNV_PKG_HDR_TAG))==NULL)
	{
		KnvNode::Delete(tree); tree = NULL;
	}

	if(header)
	{
		if(SetCommand(dwCmd)) goto failure;
		if(SetSubCommand(dwSubCmd)) goto failure;
		if(SetSequence(dwSeq)) goto failure;

		cmd = dwCmd;
		subcmd = dwSubCmd;
		seq = dwSeq;
	}
	return;
failure:
	KnvNode::Delete(tree);
	tree = header = NULL;
}

int KnvProtocol::EncodeOidb(UcMem *(&mem), KnvNode *body_tree)
{
	// pack header_content + body_tag_and_content into OIDB packet
	const KnvLeaf *hdr = header->GetValue();

	int hdr_sz = hdr->GetValue().str.len;
	int bdy_sz = body_tree? body_tree->EvaluateSize() : 0;
	int total_sz = hdr_sz + bdy_sz + 10; //STX+ddwHdrLen+ddwBdyLen+ETX

	mem = UcMemManager::Alloc(total_sz);
	if(mem==NULL)
	{
		errmsg = "UcMemManager::Alloc failed";
		return -4;
	}

	char *buf = (char *)mem->ptr();
	buf[0] = STX_IPV6_PB;
	*(uint32_t*)(buf+1) = htonl(hdr_sz);
	*(uint32_t*)(buf+5) = htonl(bdy_sz);
	memcpy(buf+9, hdr->GetValue().str.data, hdr_sz);
	if(bdy_sz)
	{
		if(body_tree->Serialize(buf+9+hdr_sz, bdy_sz)<0)
		{
			errmsg = "Serialize body failed: ";
			errmsg += body_tree->GetErrorMsg();
			return -5;
		}
	}
	buf[total_sz-1] = ETX_IPV6_PB;
	return total_sz;
}

int KnvProtocol::EncodeAllOidb(UcMem *(&mem))
{
	// pack header_content + multiple_body_tag_and_content into OIDB packet
	const KnvLeaf *hdr = header->GetValue();

	int hdr_sz = hdr->GetValue().str.len;
	int max_bdy_sz = tree->EvaluateSize();
	int max_total_sz = hdr_sz + max_bdy_sz + 10; //STX+ddwHdrLen+ddwBdyLen+ETX

	mem = UcMemManager::Alloc(max_total_sz);
	if(mem==NULL)
	{
		errmsg = "UcMemManager::Alloc failed";
		return -4;
	}

	uint8_t *buf = (uint8_t *)mem->ptr();
	buf[0] = STX_IPV6_PB;
	*(uint32_t*)(buf+1) = htonl(hdr_sz);
	memcpy(buf+9, hdr->GetValue().str.data, hdr_sz);

	int bdy_sz = 0, left_sz;
	char *bdy = (char *)(buf+9+hdr_sz);
	for(KnvNode *n = tree->GetFirstChild(); n; n=n->GetSibling())
	{
		left_sz = max_bdy_sz-bdy_sz;
		if(n->GetTag()==KNV_PKG_HDR_TAG) // skip header
			continue;
		if(n->Serialize(bdy, left_sz, true)<0)
		{
			errmsg = "Serialize body failed: ";
			errmsg += n->GetErrorMsg();
			return -5;
		}
		bdy_sz += left_sz;
		bdy += left_sz;
	}
	*(uint32_t*)(buf+5) = htonl(bdy_sz);
	buf[hdr_sz+bdy_sz+9] = ETX_IPV6_PB;
	return hdr_sz+bdy_sz+9;
}

int KnvProtocol::EncodeCompatOidb(UcMem *(&mem), KnvNode *body_tree)
{
	// pack header_content + body_content into OIDB packet
	const KnvLeaf *hdr = header->GetValue();
	const KnvLeaf *bdy = body_tree? body_tree->GetValue() : NULL;

	int hdr_sz = hdr->GetValue().str.len;
	int bdy_sz = bdy? bdy->GetValue().str.len : 0;
	int total_sz = hdr_sz + bdy_sz + 10; //STX+ddwHdrLen+ddwBdyLen+ETX

	mem = UcMemManager::Alloc(total_sz);
	if(mem==NULL)
	{
		errmsg = "UcMemManager::Alloc failed";
		return -4;
	}

	uint8_t *buf = (uint8_t *)mem->ptr();
	buf[0] = STX_IPV6_PB;
	*(uint32_t*)(buf+1) = htonl(hdr_sz);
	*(uint32_t*)(buf+5) = htonl(bdy_sz);
	memcpy(buf+9, hdr->GetValue().str.data, hdr_sz);
	if(bdy_sz)
		memcpy(buf+9+hdr_sz, bdy->GetValue().str.data, bdy_sz);
	buf[total_sz-1] = ETX_IPV6_PB;
	return total_sz;
}

int KnvProtocol::Encode(UcMem *(&mem), uint32_t ret, const char *err, int errlen, KnvNode *body_tree, bool encode_oidb, bool compat_oidb)
{
	if(!IsValid())
		return -1;

	if(ret!=retcode && SetRetCode(ret))
	{
		errmsg = "Set retcode failed: " + errmsg;
		return -2;
	}

	if(err!=retmsg || errlen!=(int)retmsglen)
	{
		if(SetRetErrorMsg(err, errlen))
		{
			errmsg = "Set ret msg failed: " + errmsg;
			return -3;
		}
	}

	if(encode_oidb)
		return compat_oidb? EncodeCompatOidb(mem, body_tree) : EncodeOidb(mem, body_tree);

	// pack tag + len + header + body_tree
	int hdr_sz = header->EvaluateSize();
	int bdy_sz = body_tree? body_tree->EvaluateSize() : 0;

	int total_val_sz = hdr_sz + bdy_sz;

	knv_value_t v;
	v.str.len = total_val_sz;
	int total_sz = knv_eval_field_length(KNV_PKG_TAG, KNV_NODE, &v);

	mem = UcMemManager::Alloc(total_sz);
	if(mem==NULL)
	{
		errmsg = "UcMemManager::Alloc failed";
		return -6;
	}

	knv_buff_t b;
	ret = knv_init_buff(&b, (char*)mem->ptr(), total_sz);
	if(ret)
	{
		UcMemManager::Free(mem);
		mem = NULL;
		errmsg = "knv_init_buff failed: "; errmsg += b.errmsg;
		return -7;
	}

	// add message tag/type/length
	ret = knv_add_string_head(&b, KNV_PKG_TAG, total_val_sz);
	if(ret)
	{
		UcMemManager::Free(mem);
		mem = NULL;
		errmsg = "knv_add_string_head failed: "; errmsg += b.errmsg;
		return -8;
	}

	int cur_len = knv_get_encoded_length(&b);
	int left = total_sz - cur_len;
	ret = header->Serialize(((char*)mem->ptr())+cur_len, left);
	if(ret)
	{
		UcMemManager::Free(mem);
		mem = NULL;
		errmsg = "serializing header failed: "; errmsg += header->GetErrorMsg();
		return -9;
	}
	cur_len += left;

	if(bdy_sz>0)
	{
		left = total_sz - cur_len;
		ret = body_tree->Serialize(((char*)mem->ptr())+cur_len, left);
		if(ret)
		{
			UcMemManager::Free(mem);
			mem = NULL;
			errmsg = "serializing body failed: "; errmsg += body_tree->GetErrorMsg();
			return -10;
		}
		cur_len += left;
	}

	return total_sz;
}

int KnvProtocol::AddBody(KnvNode *b, bool take_ownership)
{
	if(!IsValid())
	{
		errmsg = "Protocol is not initialized";
		return -1;
	}
	if(b==NULL || b->GetTag()!=KNV_PKG_BDY_TAG)
	{
		errmsg = "Request body is invalid";
		return -2;
	}

	int ret = tree->InsertChild(b, take_ownership, false);
	if(ret)
	{
		errmsg = "Error inserting KnvNode: "; errmsg += tree->GetErrorMsg();
		return -3;
	}
	body = b; // b is now current
	return 0;
}


int KnvProtocol::SetHeaderIntField(uint32_t ftag, uint64_t new_val)
{
	if(!IsValid())
	{
		errmsg = "Protocol is not initialized";
		return -1;
	}

	if(header->SetFieldInt(ftag, new_val))
	{
		errmsg = "Set header meta failed: "; errmsg += header->GetErrorMsg();
		return -2;
	}
	// update corresponding variables
	switch(ftag)
	{
	case KNV_PKG_HDR_CMD_TAG: cmd = new_val; break;
	case KNV_PKG_HDR_SUBCMD_TAG: subcmd = new_val; break;
	case KNV_PKG_HDR_SEQ_TAG: seq = new_val; break;
	case KNV_PKG_HDR_RET_TAG: retcode = new_val; break;
	default: break;
	}
	return 0;
}

int KnvProtocol::SetHeaderStringField(uint32_t ftag, uint32_t new_len, char *new_val)
{
	if(!IsValid())
	{
		errmsg = "Protocol is not initialized";
		return -1;
	}

	int ret;
	if(new_val && new_len)
		ret = header->SetFieldStr(ftag, new_len, new_val);
	else
		ret = header->RemoveField(ftag);
	if(ret)
	{
		errmsg = "Set header meta failed: "; errmsg += header->GetErrorMsg();
		return -2;
	}

	switch(ftag)
	{
	case KNV_PKG_HDR_ERR_TAG: retmsg = new_val; retmsglen = new_len; break;
	default: break;
	}
	return 0;
}

int KnvProtocol::SetRspAddr(const KnvNet::KnvSockAddr &addr)
{
	if(!IsValid())
	{
		errmsg = "Protocol is not initialized";
		return -1;
	}

	rspaddr = addr;

	int ret;
	uint32_t new_len = rspaddr.addr_len;
	char *new_val = (char *)&rspaddr.addr;

	KnvNode *n = header->FindChildByTag(KNV_PKG_HDR_RSP_ADDR);
	if(n)
	{
		ret = n->SetValue(new_val, new_len, true);
		if(ret<0)
		{
			errmsg = "Update header value failed: "; errmsg += n->GetErrorMsg();
			return -1;
		}
	}
	else
	{
		if(header->InsertStrLeaf(KNV_PKG_HDR_RSP_ADDR, new_val, new_len)==NULL)
		{
			errmsg = "Insert header value failed: "; errmsg += header->GetErrorMsg();
			return -2;
		}
	}

	return 0;
}

static inline const string &tostr(uint64_t i, string s)
{
	char cs[32];
	sprintf(cs, "%llu", (unsigned long long)i);
	return (s=cs);
}

int KnvProtocol::AddPartial(KnvProtocol &part, bool own_buf)
{
	if(!part.IsValid())
	{
		errmsg = "part is invalid";
		return -1;
	}

	if(tree==NULL)
	{
		return assign(part, own_buf);
	}

	bool this_complete;
	if((this_complete=IsComplete()) || part.IsComplete() ||
		total_split_count!=part.total_split_count || part.curr_split_index>=total_split_count)
	{
		if(!this_complete)
		{
			Attr_API(ATTR_PROTO_INCOMPLETE_PART_OVERWRITTEN, 1); // incomplete part overwritten
		}
		return assign(part, own_buf);
	}

	const KnvLeaf *l;
	string s;
	KnvNode *p = part.tree->FindChildByTag(KNV_PKG_PART_TAG_BASE+part.curr_split_index);
	if(p==NULL)
	{
		errmsg = "protocol has no corresponding part ";
		errmsg += tostr(part.curr_split_index, s);
		return -2;
	}
	if(p->GetType()!=KNV_STRING)
	{
		errmsg = "part ";
		errmsg += tostr(part.curr_split_index, s);
		errmsg += " is not a buffer as expected";
		return -3;
	}

	KnvNode *p2 = tree->FindChildByTag(KNV_PKG_PART_TAG_BASE+part.curr_split_index);
	if(p2)
	{
		errmsg = "part ";
		errmsg += tostr(part.curr_split_index, s);
		errmsg += " is already in the protocol";
		return -4;
	}
	if(tree->InsertChild(p, false, own_buf))
	{
		errmsg = "Insert partial body failed: ";
		errmsg += tree->GetErrorMsg();
		return -5;
	}

	// now check if all parts are available
	int total_len = 0;
	for(int i=0; i<total_split_count; i++)
	{
		p = tree->FindChildByTag(KNV_PKG_PART_TAG_BASE+i);
		if(p==NULL) // not yet completed
			return 0;
		l = p->GetValue();
		if(l==NULL || l->GetValue().str.len==0)
		{
			errmsg = "protocol part is empty";
			return -6;
		}
		if(l->GetType()!=KNV_STRING)
		{
			errmsg = "protocol part is not a buffer";
			return -7;
		}
		total_len += l->GetValue().str.len;
	}

	if(total_len<=0) // not yet completed
	{
		errmsg = "merged length is 0";
		return -8;
	}

	// now all parts have arrived
	UcMem *m = UcMemManager::Alloc(total_len);
	if(m==NULL)
	{
		errmsg = "UcMemManager out of memory";
		return -9;
	}

	int cur_len = 0;
	for(int i=0; i<total_split_count; i++)
	{
		p = tree->FindChildByTag(KNV_PKG_PART_TAG_BASE+i);
		if(p==NULL)
		{
			errmsg = "bug: part exists but get part failed";
			UcMemManager::Free(m);
			return -10;
		}
		l = p->GetValue();
		if(l==NULL)
		{
			errmsg = "protocol part is empty";
			UcMemManager::Free(m);
			return -11;
		}
		if(cur_len + l->GetValue().str.len > (unsigned)total_len)
		{
			errmsg = "bug: pre-calc buffer size is not enough for parts";
			UcMemManager::Free(m);
			return -12;
		}
		memcpy(((char*)(m->ptr()))+cur_len, l->GetValue().str.data, l->GetValue().str.len);
		cur_len += l->GetValue().str.len;
		tree->RemoveChildByPos(p, NULL);
	}
	int ret = assign((char*)m->ptr(), cur_len, true);
	if(ret)
	{
		errmsg = "assign merged protocol failed: " + errmsg;
		UcMemManager::Free(m);
		return -13;
	}
	// now packet is fully merged
	UcMemManager::Free(m);
	return 0;
}

int KnvProtocol::Split(KnvNode *b)
{
	uint32_t cur_sz, max_sz;
	uint32_t hdr_sz;
	uint32_t sz_part;
	uint32_t nr_pkgs;
	int encoded_len;
	UcMem *m;

	if(!IsValid())
	{
		errmsg = "Bad protocol tree";
		return -1;
	}

	// these fields should not be present in the network packets
	total_split_count = 1;
	header->RemoveChildrenByTag(KNV_PKG_HDR_TOTAL_SPLIT_COUNT);
	header->RemoveChildrenByTag(KNV_PKG_HDR_CURR_SPLIT_INDEX);

	if(b) // use external body
		cur_sz = header->EvaluateSize() + b->EvaluateSize() + 32;
	else
		cur_sz = tree->EvaluateSize() + 32;
	max_sz = GetMaxPkgSize();

	if(!allow_split || (b==NULL && body==NULL) || cur_sz <= max_sz) // no need to split
	{
no_need_split:
		if(b)
		{
			KnvNode *p = b->Duplicate(false);
			if(p==NULL)
			{
				errmsg = "Duplicate body failed: ";
				errmsg += b->GetErrorMsg();
				return -2;
			}
			if(p->SetTag(KNV_PKG_PART_TAG_BASE))
			{
				errmsg = "Change tag failed: ";
				errmsg += p->GetErrorMsg();
				KnvNode::Delete(p);
				return -3;
			}
			if(tree->InsertChild(p, true, true))
			{
				errmsg = "Insert part body failed: ";
				errmsg += tree->GetErrorMsg();
				KnvNode::Delete(p);
				return -4;
			}
		}
		return 0;
	}

	// encode to a buffer m
	if(b)
		encoded_len = EncodeWithBody(b, m);
	else
		encoded_len = Encode(m);

	if(encoded_len<0)
	{
		errmsg = "Encode failed: " + errmsg;
		return -5;
	}

	if(encoded_len <= (int)max_sz) // warning: real size is smaller than eval size
	{
		// there will be no harm except one unnecessary encoding wasted
		Attr_API(ATTR_PROTO_REAL_SIZE_SMALLER_THAN_EVAL_SIZE, 1);
		UcMemManager::Free(m);
		goto no_need_split;
	}

	hdr_sz = header->EvaluateSize() + 16;
	if(hdr_sz >= max_sz)
	{
		string s;
		errmsg = "Header size(";
		errmsg += tostr(hdr_sz, s);
		errmsg += ") is larger than max_pkg_size(";
		errmsg += tostr(max_sz, s);
		errmsg += ")";
		UcMemManager::Free(m);
		return -6;
	}

	sz_part = max_sz - hdr_sz;
	nr_pkgs = (encoded_len+sz_part-1) / sz_part;
	if(nr_pkgs<=1) // bug: nr_pkgs is smaller than eval nr
	{
		Attr_API(ATTR_PROTO_PKG_NUM_SMALLER_THAN_EVAL_NUM, 1);
		UcMemManager::Free(m);
		goto no_need_split;
	}
	uint32_t last = nr_pkgs - 1;
	uint32_t sz_last = encoded_len - (last*sz_part);

	total_split_count = nr_pkgs;
	if(header->SetChildInt(KNV_PKG_HDR_TOTAL_SPLIT_COUNT, nr_pkgs))
	{
		errmsg = "Set total_split_count failed: ";
		errmsg += header->GetErrorMsg();
		UcMemManager::Free(m);
		return -7;
	}

	for(uint32_t i=0; i<nr_pkgs; i++)
	{
		knv_value_t v;
		v.str.len = i==last? sz_last : sz_part;
		v.str.data = ((char *)m->ptr())+(i*sz_part);
		KnvNode *part = KnvNode::New(KNV_PKG_PART_TAG_BASE+i, KNV_STRING, KNV_DEFAULT_TYPE, NULL, &v, true);
		if(part==NULL)
		{
			errmsg = "construct part body failed: ";
			errmsg += KnvNode::GetGlobalErrorMsg();
			UcMemManager::Free(m);
			return -8;
		}

		tree->RemoveChildrenByTag(KNV_PKG_PART_TAG_BASE+i);
		if(tree->InsertChild(part, true, true))
		{
			errmsg = "Insert part body failed: ";
			errmsg += tree->GetErrorMsg();
			KnvNode::Delete(part);
			UcMemManager::Free(m);
			return -9;
		}
	}
	UcMemManager::Free(m);
	return 0;
}

int KnvProtocol::EncodePart(int index, UcMem *(&mem))
{
	if(!IsValid())
	{
		errmsg = "Protocol not initialized";
		return -1;
	}
	if(index>=total_split_count)
	{
		errmsg = "Bad part index";
		return -1;
	}
	if(total_split_count==1) // no need to split
	{
		int ret;
		KnvNode *b = tree->FindChildByTag(KNV_PKG_PART_TAG_BASE);
		if(b)
		{
			b->SetTag(KNV_PKG_BDY_TAG);
			ret = EncodeWithBody(b, mem);
			b->SetTag(KNV_PKG_PART_TAG_BASE);
			return ret;
		}
		else
		{
			return Encode(mem);
		}
	}
	KnvNode *b = tree->FindChildByTag(KNV_PKG_PART_TAG_BASE+index);
	if(b==NULL)
	{
		errmsg = "No such part";
		return -2;
	}
	if(header->SetChildInt(KNV_PKG_HDR_CURR_SPLIT_INDEX, index))
	{
		errmsg = "set header part index failed: ";
		errmsg += header->GetErrorMsg();
		return -3;
	}
	return EncodeWithBody(b, mem);
}

#include "commands.h"

int KnvProtocol::Print(const string &prefix, ostream &outstr)
{
	if(!IsValid())
	{
		outstr << prefix << "Invalid protocol tree." << endl;
		return -1;
	}

	outstr << prefix;
	outstr << "[#] cmd=" << knv::GetCmdName(cmd);
	outstr << ", subcmd=" << subcmd;
	outstr << ", seq="<< seq;
	if(rspaddr.addr_len>0)
		outstr << ", rspaddr="<<rspaddr.to_str_with_port();
	outstr << ", retcode="<< knv::GetErrorCodeName(retcode);
	outstr << ", retmsg=\"" << string(retmsg,retmsglen) << "\"";
	outstr << endl;
	tree->Print(prefix, outstr);
	return 0;
}

