/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include "protocol.h"
#include "version.h"

#define MAX_LEN (1024*64)

void hex_squeeze(char *str)
{
	char *data = str;
	while(*data)
	{
		if(isxdigit(*data)) 
		{
			*str++ = *data ++;
		}
		else
		{
			*data ++; 
		}
	}
	*data = 0;
}


int str2bin(const char *psAsciiData, unsigned char *sBinData, int iBinSize)
{
	hex_squeeze((char*)psAsciiData);
	int iAsciiLength = strlen(psAsciiData);
	
	int iRealLength = (iAsciiLength/2 > iBinSize) ? iBinSize : (iAsciiLength/2);
	int i;
	
	for (i = 0 ; i < iRealLength ; i++)
	{
		char sTmpdata[3];
		sTmpdata[0] = psAsciiData[i*2];
		sTmpdata[1] = psAsciiData[i*2+1];
		sTmpdata[2] = 0;
		sBinData[i] = (unsigned char)strtol(sTmpdata, NULL, 16);
	}
	return iRealLength;
}

static enum { bin_fmt, hex_fmt, tcpdump } fmt;

enum OidbHeadTag
{
	OidbHeadTag_Uin = 1,
	OidbHeadTag_Command = 2,
	OidbHeadTag_ServiceType = 3,
	OidbHeadTag_Seq = 4,
	OidbHeadTag_ClientAddr = 5,
	OidbHeadTag_ClientAddrIpv6 = 15,
	OidbHeadTag_ServerAddr = 6,
	OidbHeadTag_ServerAddrIpv6 = 16,
	OidbHeadTag_Result = 7,
	OidbHeadTag_ErrorMsg = 8,
	OidbHeadTag_LoginSig = 9,
	OidbHeadTag_UserName = 10,
	OidbHeadTag_Servicename = 11,
	OidbHeadTag_Flag = 12,
	OidbHeadTag_FromAddr = 13,
	OidbHeadTag_LocalAddr = 14,
	OidbHeadTag_ModuleId = 17,
};


int show_oidb_bin(const char *bin_str, int len)
{
	uint32_t hlen = ntohl(*(uint32_t*)(bin_str+1));
	uint32_t blen = ntohl(*(uint32_t*)(bin_str+5));
	if(hlen+blen+10 > (unsigned)len)
	{
		return -1;
	}

	KnvNode *head, *body;
	knv_value_t v;
	v.str.len = hlen;
	v.str.data = (char *)bin_str+9;
	head = KnvNode::New(1, KNV_NODE, KNV_VARINT, NULL, &v, true);
	if(head==NULL)
	{
		cout << "Invalid OIDB header: " << KnvNode::GetGlobalErrorMsg() << endl;
		return -2;
	}

	v.str.len = blen;
	v.str.data = (char *)bin_str+9+hlen;
	body = KnvNode::New(1, KNV_NODE, KNV_VARINT, NULL, &v, true);
	if(body==NULL)
	{
		cout << "Invalid OIDB body: " << KnvNode::GetGlobalErrorMsg() << endl;
		KnvNode::Delete(head);
		head = NULL;
		return -3;
	}

	string prefix = "[OIDB]";
	cout << prefix << "    +Header" << endl;
	cout << prefix << "        Uin: " << head->GetFieldInt(OidbHeadTag_Uin) << endl;
	char cmd[20]; sprintf(cmd, "0x%X", (uint32_t)head->GetFieldInt(OidbHeadTag_Command));
	cout << prefix << "        Command: " << cmd << endl;
	cout << prefix << "        ServiceType: " << head->GetFieldInt(OidbHeadTag_ServiceType) << endl;
	cout << prefix << "        Sequence: " << head->GetFieldInt(OidbHeadTag_Seq) << endl;
	if(head->GetFieldInt(OidbHeadTag_ClientAddr))
	{
		struct in_addr in = { head->GetFieldInt(OidbHeadTag_ClientAddr) };
		cout << prefix << "        ClientAddr: " << inet_ntoa(in) << endl;
	}
	else if(head->GetFieldStr(OidbHeadTag_ClientAddrIpv6).length()>0)
	{
		char dst[INET6_ADDRSTRLEN];
		cout << prefix << "        ClientAddr: " << inet_ntop(AF_INET6, head->GetFieldStr(OidbHeadTag_ClientAddrIpv6).data(), dst, sizeof(dst)) << endl;
	}
	if(head->GetFieldInt(OidbHeadTag_ServerAddr))
	{
		struct in_addr in = { head->GetFieldInt(OidbHeadTag_ServerAddr) };
		cout << prefix << "        ServiceAddr: " << inet_ntoa(in) << endl;
	}
	else if(head->GetFieldStr(OidbHeadTag_ServerAddrIpv6).length()>0)
	{
		char dst[INET6_ADDRSTRLEN];
		cout << prefix << "        ServiceAddr: " << inet_ntop(AF_INET6, head->GetFieldStr(OidbHeadTag_ServerAddrIpv6).data(), dst, sizeof(dst)) << endl;
	}
	cout << prefix << "        Result: " << head->GetFieldInt(OidbHeadTag_Result) << endl;
	if(head->GetFieldStr(OidbHeadTag_ErrorMsg).length()>0)
		cout << prefix << "        ErrorMsg: " << head->GetFieldStr(OidbHeadTag_ErrorMsg) << endl;
	if(head->GetFieldStr(OidbHeadTag_LoginSig).length()>0)
		cout << prefix << "        LoginSig: " << head->GetFieldStr(OidbHeadTag_LoginSig) << endl;
	if(head->GetFieldStr(OidbHeadTag_UserName).length()>0)
		cout << prefix << "        UserName: " << head->GetFieldStr(OidbHeadTag_UserName) << endl;
	if(head->GetFieldStr(OidbHeadTag_Servicename).length()>0)
		cout << prefix << "        Servicename: " << head->GetFieldStr(OidbHeadTag_Servicename) << endl;
	if(head->GetFieldInt(OidbHeadTag_Flag))
		cout << prefix << "        Flag: " << head->GetFieldInt(OidbHeadTag_Flag) << endl;
	if(head->GetFieldInt(OidbHeadTag_FromAddr))
	{
		struct in_addr in = { head->GetFieldInt(OidbHeadTag_FromAddr) };
		cout << prefix << "        FromAddr: " << inet_ntoa(in) << endl;
	}
	if(head->GetFieldInt(OidbHeadTag_LocalAddr))
	{
		struct in_addr in = { head->GetFieldInt(OidbHeadTag_LocalAddr) };
		cout << prefix << "        LocalAddr: " << inet_ntoa(in) << endl;
	}
	if(head->GetFieldInt(OidbHeadTag_ModuleId))
		cout << prefix << "        ModuleId: " << head->GetFieldInt(OidbHeadTag_ModuleId) << endl;

	// show unknown children in header
	for(KnvNode *n=head->GetFirstChild(); n; n=n->GetSibling())
	{
		if(n->GetTag()>17)
			n->Print(prefix+"        ");
	}

	cout << prefix << "    +Body" << endl;
	body->Print(prefix+"    ");

	KnvNode::Delete(head);
	KnvNode::Delete(body);
	return 0;    
}

int show_bin(const char *bin_str, int len)
{
	string s(bin_str, len);

	// If it is an UC/OIDB protocol, show it as protocol

	// UC
	UcProtocol p(s);
	if(p.IsValid())
		return p.Print("[UC]");

	// OIDB
	if(len>10 && bin_str[0]==0x28 && bin_str[len-1]==0x29 && show_oidb_bin(bin_str, len)!=-1) // OIDB
	{
		return 0;
	}

	// Not a protocol, show as normal knv_node
	KnvNode *n = KnvNode::New(s, false);
	if(n)
	{
		n->Print("[KNV]");
		KnvNode::Delete(n);
		return 0;
	}

	cout << "Invalid knv package[len=" << len << "]" << endl;
	return -1;
}

int read_data(FILE *fd, char *bin_str, int &bin_len)
{
	if(fmt==bin_fmt)
	{
		bin_len = fread(bin_str, 1, bin_len, fd);
		if(bin_len<=0)
		{
			printf("read failed.\n");
			return -1;
		}
		return 0;
	}
	else if(fmt==hex_fmt)
	{
		static char hstr[1024*1024];
		int hlen = fread(hstr, 1, sizeof(hstr), fd);
		if(hlen<=0)
		{
			printf("read failed.\n");
			return -1;
		}
		hstr[hlen] = 0;
		bin_len = str2bin(hstr, (unsigned char *)bin_str, bin_len);
		if(bin_len<=0)
		{
			printf("str2bin failed.\n");
			return -1;
		}
		return 0;
	}
	else // tcpdump
	{
		static char str[1024*1024];
		int len, ln_len;
		char ln[1024], *ps, *pstr;

		if(fgets(ln, sizeof(ln)-1, fd)==NULL)
		{
			printf("read header failed.\n");
			return -1;
		}
		if((ps=strstr(ln," IP "))==NULL || (ps=strstr(ps, " > "))==NULL)
		{
			printf("parse header failed.\n");
			return -1;
		}
		ln_len = strlen(ln) - 1;
		while(ln_len>0 && (ln[ln_len]=='\n' || ln[ln_len]=='\r' || ln[ln_len]=='\t' || ln[ln_len]==' '))
			ln_len --;
		if(ln_len<=0 || (ln[ln_len]<'0' || ln[ln_len]>'9'))
		{
			printf("parse header length failed.\n");
			return -1;
		}
		while(ln_len>=0 && (ln[ln_len]>='0' && ln[ln_len]<='9'))
			ln_len --;
		
		int pkg_len = atoi(ln+ln_len);
		if(pkg_len<4 || pkg_len>65530)
		{
			printf("parse header length2 failed.\n");
			return -1;
		}

		if(fgets(ln, sizeof(ln)-1, fd)==NULL) // first line hex_fmt, skip
		{
			printf("read body failed.\n");
			return -1;
		}

		if(fgets(ln, sizeof(ln)-1, fd)==NULL) // second line hex_fmt
		{
			printf("read body failed.\n");
			return -1;
		}

		if((ps=strstr(ln, " 9adb ")))
		{
			strcpy(str, "9adb"); len = 4;
			strncpy(str+4, ps+6, 4); len+= 4; 
			pstr = str + len;
		}
		else if((ps=strstr(ln, " 28")))
		{
			if(ps[5]==' ' && ps[10]==' ')
			{
				strcpy(str, "28"); len = 2;
				strncpy(str+2, ps+3, 2); len+= 2;
				strncpy(str+4, ps+6, 4); len+= 4;
				pstr = str + len;
			}
			else
			{
				printf("unkown packet format: ps=%s\n", ps);
				return -1;
			}
		}
		else
		{
			printf("unkown packet format.\n");
			return -1;
		}

		int lnr = (pkg_len+28+15)/16 - 2;
		int plen = pkg_len - 4;
		for(int i=lnr; i>0 && plen>0; i--)
		{
			if(fgets(ln, sizeof(ln)-1, fd)==NULL)
			{
				printf("read body failed: left_lines=%d, left_len=%d, total_len=%d, total_lines=%d\n", i, plen, pkg_len, lnr);
				return -1;
			}
			if((ps=strstr(ln, ": "))==NULL)
			{
				printf("parse protocol body failed.\n");
				return -1;
			}
			ps += 2;
			int cnt = plen > 16? 16 : plen;
			while(cnt>0)
			{
				while(*ps==' ') ps ++;
				strncat(pstr, ps, 4); len+=4; pstr+=4; ps +=4;
				plen -= 2;
				cnt -= 2;
			}
		}
		str[len] = 0;
	//	printf("\n%s\n", str);
		bin_len = str2bin(str, (unsigned char *)bin_str, bin_len);
		if(bin_len<=0)
		{
			printf("str2bin failed.\n");
			return -1;
		}
		bin_len = pkg_len;
		return 0;
	}
}

bool skip_space(const char *(&proto))
{
	bool skipped = false;
	while(isspace(*proto)){ skipped = true; printf("%c", *proto); proto++; }
	return skipped;
}

bool skip_comment(const char *(&proto))
{
	char comment[1024];
	bool skipped = false;

	skip_space(proto);
	while(proto[0]=='/' && proto[1]=='/')
	{
		skipped = true;
		const char *end = strchr(proto, '\n');
		if(end)
		{
			strncpy(comment, proto, end+1-proto);
			comment[end+1-proto] = 0;
		}
		else
			strcpy(comment, proto);
		printf("%s", comment);
		if(end)
		{
			proto = end + 1;
			skip_space(proto);
		}
		else
		{
			proto = "";
		}
	}
	return skipped;
}

const char *get_token(const char *(&proto))
{
	static char token[1024];
	skip_comment(proto);
	if(*proto)
	{
		const char *tok = proto;

		while(isalnum(*proto) || *proto=='_' || *proto=='.')
			proto ++;
		if(proto==tok)
			proto ++;

		strncpy(token, tok, proto-tok);
		token[proto-tok] = 0;
		return token;
	}
	return NULL;
}

int parse_proto(const char *proto, int len)
{
	const char *tok;
	bool in_msg = false;
	char name[1024];
	int nsnum = 0;

	((char*)proto)[len-1] = 0;

	const char *eol = strchr(proto, '\r')? "\r\n" : "\n";
	skip_comment(proto);

	// handle token
	while((tok=get_token(proto)))
	{
		if(strcmp(tok, "package")==0)
		{
			const char *subtok = get_token(proto);
			if(subtok==NULL || !isalpha(*subtok))
			{
				fprintf(stderr, "incomplete proto: message name missing\n");
				return -1;
			}
			const char *end;
			do
			{
				end = strchr(subtok, '.');
				if(end)
					*(char*)end = '\0';
				printf("%snamespace %s%s{%s", eol, subtok, eol, eol);
				nsnum ++;
				if(end)
					subtok = end + 1;
			}
			while(end);
		}
		else if(strcmp(tok, "message")==0)
		{
			const char *subtok = get_token(proto);
			if(subtok==NULL || !isalpha(*subtok))
			{
				fprintf(stderr, "incomplete proto: message name missing\n");
				return -1;
			}
			strcpy(name, subtok);
			printf("%senum %sTags", eol, subtok);
		}
		else if(strcmp(tok, "enum")==0) // output as it is
		{
			printf("enum");
			const char *subtok;
			while((subtok=get_token(proto)) && subtok[0]!='}')
			{
				if(subtok[0]==';')
					printf(",");
				else
					printf(subtok);
			}
			if(subtok && subtok[0]=='}')
				printf("};");
		}
		else if(strcmp(tok, "{")==0)
		{
			if(in_msg)
			{
				fprintf(stderr, "inline submsg unsupported\n");
				return -1;
			}
			printf("{");
			in_msg = true;
		}
		else if(strcmp(tok, "}")==0)
		{
			if(in_msg)
			{
				printf("};");
				in_msg = false;
			}
			else
			{
				fprintf(stderr, "unexpected } token\n");
				return -1;
			}
		}
		else if(strcmp(tok, "optional")==0 || strcmp(tok, "repeated")==0 || strcmp(tok, "required")==0)
		{
			const char *subtok;
			if(get_token(proto)==NULL || (subtok = get_token(proto))==NULL)
			{
				fprintf(stderr, "incomplete proto: field name missing\n");
				return -1;
			}
			char fname[256];
			strcpy(fname, subtok);
			if(get_token(proto)==NULL || (subtok = get_token(proto))==NULL || !isdigit(*subtok))
			{
				fprintf(stderr, "incomplete proto: field %s tag number missing\n", fname);
				return -1;
			}
			printf("%sTag_%s = %s,", name, fname, subtok);
		}
		else if(strcmp(tok, "[")==0) // skip [...]
		{
			const char *subtok;
			while((subtok=get_token(proto))!=NULL && subtok[0]!=']');
		}
		else // skip
		{
		}
	}
	while(nsnum-->0)
	{
		printf("%s}; //namespace%s", eol, eol);
	}
	return 0;
}

int main(int argc, char **argv)
{
	static char bin_str[1024*1024*4];
	int bin_len;

	enum { show_op, convert_op, parse_op } op = show_op;
	if(argc!=2 && argc!=3)
	{
error:
		printf("usage:\n");
		printf("       %s -t  <file>    # parse input as output of tcpdump -Xlnnps0 \n", argv[0]);
		printf("       %s -h  <file>    # parse input as hex string\n", argv[0]);
		printf("       %s -b  <file>    # parse input as binary format\n", argv[0]);
		printf("       %s -ct <file>    # convert tcpdump output to bin and write to stdout\n", argv[0]);
		printf("       %s -ch <file>    # convert hex string to bin and write to stdout\n", argv[0]);
		printf("       %s -p  <file>    # parse .proto file and generate macro definitions for tags\n", argv[0]);
		printf("       %s -v|--version  # display libknv version\n", argv[0]);
		printf("\nBy default, read from stdin if <file> is [-] or not specified.\n\n");
		return (0);
	}

	if(strcmp(argv[1],"-v")==0 || strcmp(argv[1],"--version")==0)
	{
		printf("LibKnv is a protocol engine for fast manipulation of tree-like generic protocol.\n");
		printf("The current version is %d.%d\n", LIB_KNV_MAJOR_VERSION, LIB_KNV_MINOR_VERSION);
		return 0;
	}
	else if(strcmp(argv[1],"-t")==0 || strcmp(argv[1],"-ct")==0 ||  strcmp(argv[1],"--tcpdump")==0)
	{
		fmt = tcpdump;
		if(argv[1][1]=='c') op = convert_op;
	}
	else if(strcmp(argv[1],"-h")==0 || strcmp(argv[1],"-ch")==0  || strcmp(argv[1],"--hex")==0)
	{
		fmt = hex_fmt;
		if(argv[1][1]=='c') op = convert_op;
	}
	else if(strcmp(argv[1],"-b")==0 || strcmp(argv[1],"--bin")==0)
	{
		fmt = bin_fmt;
	}
	else if(strcmp(argv[1],"-p")==0 || strcmp(argv[1],"--parse")==0)
	{
		fmt = bin_fmt;
		op = parse_op;
	}
	else
	{
		goto error;
	}

	FILE *fd = stdin;
	if(argc>2 && argv[2][0] && strcmp(argv[2], "-"))
	{
		fd = fopen(argv[2], "r");
		if(fd==0)
		{
			fprintf(stderr, "failed to open file %s\n", argv[1]);
			return -1;
		}
	}

	bin_len = sizeof(bin_str);
	if(read_data(fd, bin_str, bin_len))
		return -1;

	if(op==parse_op)
	{
		return parse_proto(bin_str, bin_len);
	}
	else if(op==show_op)
	{
		return show_bin(bin_str, bin_len);
	}
	else
	{
		return fwrite(bin_str, bin_len, 1, stdout);
	}
	return 0;
}

