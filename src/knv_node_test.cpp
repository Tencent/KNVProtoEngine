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

#include "knv_node.h"

static inline string key2hex(const knv_key_t &k)
{
	int len=k.GetLength();
	string s2 = "";
	char str[10];
	for(int i=0; i<len; i++)
	{
		sprintf(str, "%02x", (unsigned char)k.GetData()[i]);
		s2 += str;
	}
	return s2;
}

int Read(const knv_key_t &k, string &buffer)
{
	ifstream fs(key2hex(k).c_str(), ifstream::binary);
	if(fs)
	{
		int len;
		fs.seekg(0, fs.end);
		len = fs.tellg();
		fs.seekg(0, fs.beg);
		buffer.resize(len);
		fs.read((char *)buffer.data(), len);
		fs.close();
		return 0;
	}
	else // 没有对应文件
	{
		cout << "No such key" << endl;
		return -1;
	}
}

int ReadTest(uint64_t key)
{
	knv_key_t k(KNV_VARINT, 8, (char*)&key);

	string s;
	if(Read(k, s))
		return -1;

	if(s.length()<1)
	{
		cout << "Key " << key << " has no data" << endl;
		return -2;
	}

	KnvNode *n;
	n = KnvNode::New(s, true);
	if(n==0)
	{
		cout << "Create tree for key " << key << " failed: " << KnvNode::GetGlobalErrorMsg() << endl;
		return -3;
	}

	KnvNode *dm;
	dm = n->FindChild(11, NULL, 0); // 11 - UDC
	if(dm==0)
	{
		cout << "Domain UDC(11) not found" << endl;
		return -3;
	}

	cout << "Field num in domain UDC: " << dm->GetChildNum() << endl;

	KnvNode *c;
	const KnvLeaf *l;
	c = dm->FindChild(101, NULL, 0);
	if(c==NULL || (l=c->GetValue())==NULL)
	{
		cout << "Read field 101 failed: " << (c==NULL? dm->GetErrorMsg() : c->GetErrorMsg()) << endl;
		return -4;
	}
	cout << "    Nick: " << string(l->GetValue().str.data, l->GetValue().str.len).c_str() << endl;

	c = dm->FindChild(102, NULL, 0);
	if(c==NULL || (l=c->GetValue())==NULL)
	{
		cout << "Read field 102 failed: " << (c==NULL? dm->GetErrorMsg() : c->GetErrorMsg()) << endl;
		return -5;
	}
	cout << "    Birth: " << l->GetValue().i64 << endl;

	c = dm->FindChild(103, NULL, 0);
	if(c==NULL || (l=c->GetValue())==NULL)
	{
		cout << "Read field 103 failed: " << (c==NULL? dm->GetErrorMsg() : c->GetErrorMsg()) << endl;
		return -4;
	}
	cout << "    Gender: " << string(l->GetValue().str.data, l->GetValue().str.len).c_str() << endl;

	cout << endl << "Tree Data:" << endl;
	n->Print("    ");

	KnvNode::Delete(n);
	return 0;
}

string to_string(int i)
{
	char s[32];
	sprintf(s, "%d", i);
	return s;
}

int MakeReqTree(knv_key_t &k, KnvNode *(&req_tree), KnvNode *(&data_tree), int subkeys, int fields)
{
	req_tree = KnvNode::NewTree(3501, &k);
	if(req_tree==NULL)
	{
		cout << "KnvNode::New() returns: " << KnvNode::GetGlobalErrorMsg() << endl;
		return -1;
	}
	data_tree = KnvNode::NewTree(3501, &k);
	if(data_tree==NULL)
	{
		cout << "KnvNode::New() returns: " << KnvNode::GetGlobalErrorMsg() << endl;
		return -1;
	}

	KnvNode *dm = req_tree->InsertChild(13, KNV_NODE, NULL, true); //Group
	if(dm==NULL)
	{
		cout << "Add domain Group failed: " << KnvNode::GetGlobalErrorMsg() << endl;
		return -1;
	}

	for(int i=0; i<subkeys; i++)
	{
		uint64_t u = 220200200+i;
		knv_key_t k(KNV_VARINT, 8, (char*)&u);
		KnvNode *guin = dm->InsertChild(11, KNV_NODE, k, NULL, true); // friend list
		if(guin==NULL)
		{
			cout << "Add group uin "<<u<<" failed: " << dm->GetErrorMsg() << endl;
			return -1;
		}

		for(int j=0; j<3 && j<fields; j++)
			guin->InsertIntLeaf(300+j, 1);
	}

	dm = data_tree->InsertChild(13, KNV_NODE, NULL, true); //Group
	if(dm==NULL)
	{
		cout << "Add domain Group failed: " << KnvNode::GetGlobalErrorMsg() << endl;
		return -1;
	}

	for(int i=0; i<subkeys; i++)
	{
		uint64_t u = 220200200+i;
		knv_key_t k(KNV_VARINT, 8, (char*)&u);
		KnvNode *guin = dm->InsertChild(11, KNV_NODE, k, NULL, true); // friend list
		if(guin==NULL)
		{
			cout << "Add group uin "<<u<<" failed: " << dm->GetErrorMsg() << endl;
			return -1;
		}

		guin->InsertStrLeaf(300, "testname", 8);
		if(fields>1)
		guin->InsertIntLeaf(301, 1);
		if(fields>2)
		guin->InsertStrLeaf(302, "13824427433", 11);
		if(fields>3)
		guin->InsertStrLeaf(303, "abcdefg@test.com", 16);
		if(fields>4)
		guin->InsertStrLeaf(304, "aaabbbbcccddd", 13);

		for(int j=5; j<fields; j++)
		{
			if(j%2)
				guin->InsertIntLeaf(300+j, j);
			else
				guin->InsertStrLeaf(300+j, string("f"+to_string(j)).data(), j>9?3:2);
		}
	}

/*
	if(dm->SetMetaInt(1, 10242345)<0)
	{
		cout << "SetMetaInt failed: " << tree->GetErrorMsg() << endl;
		return -6;
	}
	string s;
	if(tree->Serialize(s)<0)
	{
		cout << "Serialize 1 failed: " << tree->GetErrorMsg() << endl;
		return -7;
	}

	s = "abcd cdef dfgg";
	if(dm->SetMetaStr(2, s.length(), s.c_str())<0)
	{
		cout << "SetMetaStr failed: " << tree->GetErrorMsg() << endl;
		return -8;
	}
	if(tree->Serialize(s)<0)
	{
		cout << "Serialize 2 failed: " << tree->GetErrorMsg() << endl;
		return -9;
	}
*/
	return 0;
}

int PressTest(int subkeys, int fields, bool bTestGetSubTree)
{
	uint64_t kv = 12345678;
	knv_key_t k(KNV_VARINT, 8, (char*)&kv);

	KnvNode *req_tree, *data_tree;
	if(MakeReqTree(k, req_tree, data_tree, subkeys, fields))
		return -1;

	//req_tree->Print("[P]");
	//return 0;

	string s, s2;
	if(req_tree->Serialize(s))
	{
		KnvNode::Delete(req_tree);
		cout << "Serialize knv failed: " << req_tree->GetErrorMsg() << endl;
		return -1;
	}
	KnvNode::Delete(req_tree);

	if(s.length()<1)
	{
		cout << "Key " << kv << " has no data" << endl;
		return -2;
	}
	int req_name = -1;

	for(int i=0; i<1000000; i++)
	{
		KnvNode *tree;
		tree = KnvNode::New(s, true);
		if(tree==0)
		{
			cout << "Create tree for key " << kv << " failed: " << KnvNode::GetGlobalErrorMsg() << endl;
			return -3;
		}
		if(bTestGetSubTree==false)
		{
			KnvNode *dm = tree->GetFirstChild();
			if(dm)
			{
				uint64_t u = 220200200+subkeys-1;
				knv_key_t k(KNV_VARINT, 8, (char*)&u);
				KnvNode *subkey = dm->FindChild(11, (const char *)&u, 8);
				if(subkey)
					req_name = subkey->GetChildInt(300);
			}
			data_tree->Serialize(s2);
		}
		else
		{
			KnvNode *out, *empty;
			if(data_tree->GetSubTree(req_tree, out, empty)<0)
			{
				cout << "GetSubTree failed: " << tree->GetErrorMsg() << endl;
				return -4;
			}
			if(out)
				KnvNode::Delete(out);
			if(empty)
				KnvNode::Delete(empty);
		}
		KnvNode::Delete(tree);
	}
	cout<<"req_len:"<<s.length()<<", data_len:"<<s2.length()<<", req_name="<<req_name<<endl;
	return 0;
}

#define FAIL_IF(x) if((x)<0) { cout <<__LINE__<<":"<< tree->GetErrorMsg()<<endl; return -1; }

int FieldTest(uint64_t key)
{
	knv_key_t k(key);
	KnvNode *tree = KnvNode::NewTree(3501, &k);
	if(tree==NULL)
	{
		cout << "KnvNode::New() returns: " << KnvNode::GetGlobalErrorMsg() << endl;
		return -1;
	}

	FAIL_IF(tree->SetFieldStr(3, 4,"3333"))
	FAIL_IF(tree->SetFieldStr(4, 4,"4444"));
	FAIL_IF(tree->AddMetaStr(3, 4,"3122"));
	FAIL_IF(tree->AddMetaInt(3, 1234));
	FAIL_IF(tree->AddMetaInt(1, 1234));
	FAIL_IF(tree->RemoveField(1)); // remove key
	FAIL_IF(tree->AddFieldInt(1, 12345678));
	FAIL_IF(tree->AddFieldInt(1, 112345678));
	FAIL_IF(tree->AddFieldInt(1, 1112345678));
	FAIL_IF(tree->AddFieldInt(1, 11112345678ULL));
	FAIL_IF(tree->SetFieldStr(13, 6,"test13"));
	FAIL_IF(tree->SetFieldStr(12, 6,"test12"));
	FAIL_IF(tree->SetFieldStr(11, 6,"test11"));
	FAIL_IF(tree->SetFieldStr(14, 6,"test14"));
	tree->Print("[T]");
	string s;
	FAIL_IF(tree->Serialize(s));
	KnvNode::Delete(tree);
	tree = KnvNode::New(s);
	for(KnvNode *l = tree->GetFirstField(); l; l=tree->GetNextField(l))
	{
		cout << "tag="<<l->GetTag()<<", type="<<l->GetType()<<", val=";
		if(l->GetType()==KNV_VARINT)
			cout << l->GetIntVal() << endl;
		else
			cout << "\"" <<l->GetStrVal() << "\"" << endl;
	}
	return 0;
}

int WriteTest(uint64_t key)
{
	knv_key_t k(key);
	KnvNode *tree = KnvNode::NewTree(3501, &k);
	if(tree==NULL)
	{
		cout << "KnvNode::New() returns: " << KnvNode::GetGlobalErrorMsg() << endl;
		return -1;
	}

	//cout << "tree's value length[1]: " << tree->GetValue()->GetValue().str.len <<endl;

	KnvNode *dm = tree->InsertSubNode(11); //UDC
	if(dm==NULL)
	{
		cout << "Add domain UDC failed: " << tree->GetErrorMsg() << endl;
		return -2;
	}
	//cout << "tree's value length[2]: " << tree->GetValue()->GetValue().str.len <<endl;

	//dm = tree->FindChildByTag(11);
	knv_value_t v;
	if(dm->InsertStrLeaf(101, (char*)"Shaneyu", 8)==NULL)
	{
		cout << "Add field 101 failed: " << dm->GetErrorMsg() << endl;
		return -3;
	}
	//cout << "tree's value length[3]: " << tree->GetValue()->GetValue().str.len <<endl;

	//dm = tree->FindChildByTag(11);
	v.i64 = 19801010;
	if(dm->InsertChild(102, KNV_VARINT, &v, true)==NULL)
	{
		cout << "Add field 102 failed: " << dm->GetErrorMsg() << endl;
		return -3;
	}
	//cout << "tree's value length[4]: " << tree->GetValue()->GetValue().str.len <<endl;

	//dm = tree->FindChildByTag(11);
	v.str.data = (char *)"Boy";
	v.str.len = strlen(v.str.data);
	if(dm->InsertChild(103, KNV_STRING, &v, true)==NULL)
	{
		cout << "Add field 103 failed: " << dm->GetErrorMsg() << endl;
		return -3;
	}
	//cout << "tree's value length[5]: " << tree->GetValue()->GetValue().str.len <<endl;

	//dm = tree->FindChildByTag(11);
	dm = tree->InsertChild(12, KNV_NODE, NULL, true); //SNS
	if(dm==NULL)
	{
		cout << "Add domain SNS failed: " << KnvNode::GetGlobalErrorMsg() << endl;
		return -2;
	}
	//cout << "tree's value length[6]: " << tree->GetValue()->GetValue().str.len <<endl;

	//dm = tree->FindChildByTag(11);
	KnvNode *nfl = dm->InsertChild(201, KNV_NODE, NULL, true); // friend list
	if(nfl==NULL)
	{
		cout << "Add field 201 failed: " << dm->GetErrorMsg() << endl;
		return -5;
	}
	//cout << "tree's value length[7]: " << tree->GetValue()->GetValue().str.len <<endl;

	//dm = tree->FindChildByTag(11);
	nfl = dm->FindChildByTag(201);
	for(int i=0; i<20; i++)
	{
		v.i64 = 828000201 + i;
		if(nfl->InsertChild(11, KNV_VARINT, &v, true)==NULL)
		{
			cout << "Add sub_field 201:11 failed(i="<<i<<"): " << nfl->GetErrorMsg() << endl;
			return -5;
		}
	}
	//cout << "tree's value length[8]: " << tree->GetValue()->GetValue().str.len <<endl;

	//dm = tree->FindChildByTag(11);
	dm = tree->InsertChild(13, KNV_NODE, NULL, true); //Group
	if(dm==NULL)
	{
		cout << "Add domain Group failed: " << KnvNode::GetGlobalErrorMsg() << endl;
		return -2;
	}
	//cout << "tree's value length[9]: " << tree->GetValue()->GetValue().str.len <<endl;

	//dm = tree->FindChildByTag(11);
	for(int i=0; i<10; i++)
	{
		unsigned long long u = 220200200+i;
		knv_key_t k(KNV_VARINT, 8, (char*)&u);
		KnvNode *guin = dm->InsertChild(11, KNV_NODE, k, NULL, true); // friend list
		if(guin==NULL)
		{
			cout << "Add group uin "<<u<<" failed: " << dm->GetErrorMsg() << endl;
			return -5;
		}

		char name[100];
		sprintf(name, "Name%llu", u);
		v.str.data = name;
		v.str.len = strlen(name);
		guin->InsertChild(300, KNV_STRING, &v, true);

		v.i64 = 1;
		guin->InsertChild(301, KNV_VARINT, &v, true);

		sprintf(name, "Phone%llu", u);
		v.str.data = name;
		v.str.len = strlen(name);
		guin->InsertChild(302, KNV_STRING, &v, true);

		sprintf(name, "Email%llu", u);
		v.str.data = name;
		v.str.len = strlen(name);
		guin->InsertChild(303, KNV_STRING, &v, true);

		sprintf(name, "Remark%llu", u);
		v.str.data = name;
		v.str.len = strlen(name);
		guin->InsertChild(304, KNV_STRING, &v, true);
	//	cout << "tree's value length[9."<<i<<"]: " << tree->EvaluateSize() <<endl;
	}


	tree->SetMetaInt(2, 31);
	string s;
	tree->SetMetaStr(3, s.length(), s.assign("crazyshen").c_str());

	cout << "tree's value length: " << tree->GetValue()->GetValue().str.len <<endl;
	tree->Print("[data]");
#if 0
	KnvNode *req = tree->MakeRequestTree(2);
	if(req==NULL)
	{
		cout << "MakeRequestTree failed: " << tree->GetErrorMsg() << endl;
	}
	else
	{
//		req->Print("[req]");

		req->RemoveChildrenByTag(11);
		req->RemoveChildrenByTag(12);
/*
		req->RemoveChildrenByTag(13);
		KnvNode::Delete(req);
		req = KnvNode::NewTree(1);
		dm = req->InsertSubNode(13);
		dm = dm->InsertSubNode(11);
		dm->InsertIntLeaf(300, 1);
		dm->InsertIntLeaf(301, 1);
		dm->InsertIntLeaf(302, 1);
		dm->InsertIntLeaf(303, 1);
		dm->InsertIntLeaf(304, 1);
		dm->InsertIntLeaf(305, 1);
		dm->InsertIntLeaf(306, 1);
*/
		req->Print("[req]");
//		tree->Print("[data2]");

/*
		KnvNode *match = NULL, *empty = NULL;
		int ret = tree->GetSubTree(req, match, empty);
		if(ret)
		{
			cout << "GetSubTree returns " <<tree->GetErrorMsg()<<endl;
		}
		else
		{
		//	tree->Print("[data2]");
			if(match)
				match->Print("[match]");
			if(empty)
				empty->Print("[empty]");
		}
*/
		KnvNode *match = NULL;
		int ret = tree->DeleteSubTree(req, match);
		if(ret)
		{
			cout << "DeleteSubTree returns " <<tree->GetErrorMsg()<<endl;
		}
		else
		{
			tree->Print("[deleted]");
			if(match)
				match->Print("[match]");
		}
	}
#endif
	if(tree->Serialize(s))
	{
		cout << "Serialize failed: " << tree->GetErrorMsg() << endl;
		return -6;
	}

	ofstream fs(key2hex(k).c_str(), ios_base::binary|ios_base::out|ios_base::trunc);
	fs<<s;
	fs.close();

	KnvNode::Delete(tree);
	return 0;
}

int main(int argc, char *argv[])
{
	if(argc<2)
	{
	err:
		cout << "Arguments: " << endl;
		cout << "           " << argv[0] << " r <uin>  # read test" << endl;
		cout << "           " << argv[0] << " w <uin>  # write test" << endl;
		cout << "           " << argv[0] << " rw <uin> # read and write test" << endl;
		cout << "           " << argv[0] << " wr <uin> # write and read test" << endl;
		cout << "           " << argv[0] << " pc  <subkey_num> <field_num>  # decode/encode pressure test" << endl;
		cout << "           " << argv[0] << " pe  <subkey_num> <field_num>  # extract pressure test" << endl;
		cout << "           " << argv[0] << " f        # test field api" << endl;
		return 1;
	}

	if(strcmp(argv[1], "r")==0)
	{
		if(ReadTest(argc>2? strtoul(argv[2],NULL,10):12345678)==0)
			cout << "Read successfully." << endl;
		return 0;
	}
	if(strcmp(argv[1], "w")==0)
	{
		if(WriteTest(argc>2? strtoul(argv[2],NULL,10):12345678)==0)
			cout << "Write successfully." << endl;
		return 0;
	}
	if(strcmp(argv[1], "rw")==0)
	{
		if(ReadTest(argc>2? strtoul(argv[2],NULL,10):12345678)==0)
		{
			cout << "Read successfully." << endl;

			if(WriteTest(argc>2? strtoul(argv[2],NULL,10):12345678)==0)
				cout << "Write successfully." << endl;
		}
		return 0;
	}
	if(strcmp(argv[1], "wr")==0)
	{
		if(WriteTest(argc>2? strtoul(argv[2],NULL,10):12345678)==0)
		{
			cout << "Write successfully." << endl;

			if(ReadTest(argc>2? strtoul(argv[2],NULL,10):12345678)==0)
				cout << "Read successfully." << endl;
		}
		return 0;
	}
	if(strcmp(argv[1], "pc")==0 && argc==4)
	{
		if(PressTest(atoi(argv[2]), atoi(argv[3]), false)==0)
			cout << "Encode/Decode press test successfully." << endl;
		return 0;
	}
	if(strcmp(argv[1], "pe")==0 && argc==4)
	{
		if(PressTest(atoi(argv[2]), atoi(argv[3]), true)==0)
			cout << "Extract press test successfully." << endl;
		return 0;
	}
	if(strcmp(argv[1], "f")==0)
	{
		if(FieldTest(1)==0)
			cout << "Field test successfully." << endl;
		return 0;
	}
	goto err;
}
