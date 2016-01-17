/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

// knv_net.h
// Encapsulation for socket
//
// 2013-10-31	Created
//

#ifndef __KNV_NET__
#define __KNV_NET__

#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <string.h>

namespace KnvNet
{
	// mask the difference between IPv4/IPv6
	class KnvSockAddr
	{
	public:
		KnvSockAddr(bool use_ipv6 = false):a6() { addr_len = use_ipv6? sizeof(sockaddr_in6) : sizeof(sockaddr_in); }
		KnvSockAddr(const char *ipstr, int port);
		KnvSockAddr(const struct sockaddr_in &a): a4(a), addr_len(sizeof(a4)) {}
		KnvSockAddr(const struct sockaddr_in6 &a): a6(a), addr_len(sizeof(a6)) {}
		KnvSockAddr(const struct sockaddr *a, socklen_t l): addr_len(l) {memcpy(&addr, a, l);}
		KnvSockAddr(const KnvSockAddr &a): a6(a.a6), addr_len(a.addr_len) {}

		const char *to_str();
		const char *to_str_with_port();
		bool IsIpv4() const { return addr_len==sizeof(a4); }
		bool IsIpv6() const { return !IsIpv4(); }
		KnvSockAddr &operator= (const KnvSockAddr &a) { a6 = a.a6; addr_len = a.addr_len; return *this; }

		union
		{
			struct sockaddr     addr;
			struct sockaddr_in  a4;
			struct sockaddr_in6 a6;
		};
		socklen_t addr_len;
	};

	int SetSocketNonblock(int sockfd, bool nonblock);
	int SetSocketRecvTimeout(int sockfd, uint64_t iTimeoutMs);
	int CreateUdpListenSocket(int port, bool reuse = true, bool use_ipv6 = false, bool nblock = true);
};

#endif

