/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

/*
 * commands.h
 * Mappings for application specific commands
 *
 * 2013-10-23	Created
 */

#include <stdint.h>
#include <string>
#include <map>
#include <string.h>
using namespace std;

#define uint_to_hstr(ival, str, len) \
	do {\
		char s[32]; int i=32, h; uint64_t v=ival; \
		while(v) { h=(v%16); s[--i] = h>9? h-10+'A':h+'0'; v/=16; }\
		str[0]='0'; str[1]='x';\
		if(i==32) {str[2]='0'; str[3]='\0'; } \
		else { memcpy(str+2, s+i, 32-i); str[2+32-i]=0;}\
	} while(0)


namespace knv
{
	map<uint64_t, string> g_knv_cmd_maps;
	map<uint64_t, string> g_knv_errcode_maps;

	const char *GetCmdName(uint64_t c)
	{
		const map<uint64_t, string>::iterator it = g_knv_cmd_maps.find(c);
		if(it==g_knv_cmd_maps.end())
		{
			static __thread char cmd_name[128];
			uint_to_hstr(c,cmd_name,sizeof(cmd_name)); strncat(cmd_name, ":UnknownCommand", sizeof(cmd_name));
			return cmd_name;
		}
		return it->second.c_str();
	}

	const char *GetErrorCodeName(uint64_t c)
	{
		const map<uint64_t, string>::iterator it = g_knv_errcode_maps.find(c);
		if(it==g_knv_errcode_maps.end())
		{
			static __thread char code_name[128];
			uint_to_hstr(c,code_name,sizeof(code_name)); strncat(code_name, ":UnknownErrorCode", sizeof(code_name));
			return code_name;
		}
		return it->second.c_str();
	}


};

