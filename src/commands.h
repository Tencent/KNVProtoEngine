/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

/*
 * commands.h
 * Application specific commands and error code definitions
 *
 * 2013-10-23	Created
 */

#ifndef __KNV_COMMANDS__
#define __KNV_COMMANDS__

#include <stdint.h>
#include <string>
#include <map>

using namespace std;



namespace knv
{
	const char *GetCmdName(uint64_t cmd);
	const char *GetErrorCodeName(uint64_t errcode);


	extern map<uint64_t, string> g_knv_cmd_maps;
	extern map<uint64_t, string> g_knv_errcode_maps;

	// constructor class for internal use
	class KnvCommandRegisterer
	{
		public:
			KnvCommandRegisterer(const char *desc, uint64_t val)
			{
				g_knv_cmd_maps[val] = desc;
			}
			~KnvCommandRegisterer() {}
	};

	#define RegisterKnvCommand(cmdstr, cmdval) \
		enum { cmdstr = cmdval }; \
		static KnvCommandRegisterer g_knv_cmd_reg_##cmdval(#cmdstr, cmdval)

	// constructor class for internal use
	class KnvErrorCodeRegisterer
	{
		public:
			KnvErrorCodeRegisterer(const char *desc, uint64_t val)
			{
				g_knv_errcode_maps[val] = desc;
			}
			~KnvErrorCodeRegisterer() {}
	};

	#define RegisterKnvErrorCode(token, codeval) \
		enum KnvErrorCode##token { token = codeval }; \
		static KnvErrorCodeRegisterer g_knv_errcode_reg_##codeval(#token, codeval)


	//
	// Please define your commands like those below
	//

	// Unified Cache cache-layer commands
	RegisterKnvCommand(CacheReadCommand, 0x4001);
	RegisterKnvCommand(CacheWriteCommand, 0x4002);
	RegisterKnvCommand(CacheEraseCommand, 0x4004); // erase data and re-read from UnionSession if needed fill-back

	RegisterKnvCommand(CacheSyncCommand, 0x4003); // for slaves only, sync-ed from master
	RegisterKnvCommand(CacheFillbackCommand, 0x4005); // read data from UnionSession and fillback to cache
	RegisterKnvCommand(CacheFastSyncCommand, 0x4006); // same as sync, but no need to reply

	// Unified Cache Key-Value commands
	RegisterKnvCommand(CacheReadAllCommand, 0x4007); // read whole key
	RegisterKnvCommand(CacheWriteAllCommand, 0x4008); // overwrite whole key
	RegisterKnvCommand(CacheEraseAllCommand, 0x4009); // erase whole key

	// Unified Cache Sync Center commands
	RegisterKnvCommand(ScSyncNoData, 0x4101); // User-end agent sync to SC server, no data
	RegisterKnvCommand(ScSyncWithData, 0x4102); // with data

	// Unified Cache Union Session commands
	RegisterKnvCommand(UsReadCommand, 0x4201); // Read data from data source

	enum UcSyncSubCommand
	{
		CacheSyncFull = 0, // full data
		CacheSyncUpdate = 1, // incremental update
		CacheSyncErase = 2, // incremental delete
	};

	RegisterKnvCommand(ProfileSet, 0x4ff);
	RegisterKnvCommand(ProfileBatchGetSimple, 0x5e1);
	RegisterKnvCommand(ProfileBatchGetDetail, 0x5eb);


	//
	// Please define your error code like those below
	//

	RegisterKnvErrorCode(Successful, 0);

	// UC common
	RegisterKnvErrorCode(UC_BadKey, 1);
	RegisterKnvErrorCode(UC_BadRequest, 2);
	RegisterKnvErrorCode(UC_SystemError, 3);
	RegisterKnvErrorCode(UC_Timeout, 4);

	// Cache layer
	RegisterKnvErrorCode(CacheRead_FillbackError, 101);
	RegisterKnvErrorCode(CacheRead_FilterFailed, 102);

	// CacheRead follows
	RegisterKnvErrorCode(CacheWrite_RequestBodyEmpty, 201);

	// CacheWrite follows
	RegisterKnvErrorCode(CacheWrite_WritToNonMaster, 301);

};

#endif

