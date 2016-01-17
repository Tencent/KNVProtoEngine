/*
Tencent is pleased to support the open source community by making Key-N-Value Protocol Engine available.
Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

// knv_net.cc
// Encapsulation for socket
// 
// 2013-10-31	Created
//

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <iostream>

#include "knv_net.h"

namespace KnvNet
{
	KnvSockAddr::KnvSockAddr(const char *ipstr, int port)
	{
		if(strchr(ipstr, ':')) // ipv6
		{
			inet_pton(AF_INET6, ipstr, &a6.sin6_addr);
			addr_len = sizeof(sockaddr_in6);
			a6.sin6_family = AF_INET6;
			a6.sin6_port = htons(port);
		}
		else // default to ipv4
		{
			inet_pton(AF_INET, ipstr, &a4.sin_addr);
			addr_len = sizeof(sockaddr_in);
			a4.sin_family = AF_INET;
			a4.sin_port = htons(port);
		}
	}

	const char *KnvSockAddr::to_str()
	{
		static __thread char str[INET6_ADDRSTRLEN+1];
		if(addr_len==sizeof(struct sockaddr_in6))
			return inet_ntop(AF_INET6, (char*)&a6.sin6_addr, str, addr_len);
		else
			return inet_ntop(AF_INET, (char*)&a4.sin_addr, str, addr_len);
	}
	const char *KnvSockAddr::to_str_with_port()
	{
		static __thread char str[INET6_ADDRSTRLEN+1+32];
		snprintf(str, sizeof(str), "%s%c%hu", to_str(), addr_len==sizeof(struct sockaddr_in6)? '/':':', ntohs(addr_len==sizeof(struct sockaddr_in6)? a6.sin6_port : a4.sin_port));
		return str;
	}

	int SetSocketNonblock(int sock, bool nonblock)
	{
		int iFlags;
		iFlags = fcntl(sock, F_GETFL, 0);
		if(nonblock)
		{
			iFlags |= O_NONBLOCK;
			iFlags |= O_NDELAY;
		}
		else
		{
			iFlags &= ~O_NONBLOCK;
			iFlags &= ~O_NDELAY;
		}
		return fcntl(sock, F_SETFL, iFlags);
	}

	int SetSocketRecvTimeout(int sockfd, uint64_t iTimeoutMs)
	{
		struct timeval tv;
		tv.tv_sec = iTimeoutMs / 1000;
		tv.tv_usec = (iTimeoutMs % 1000)*1000;
		return setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));
	}

	int CreateUdpListenSocket(int port, bool reuse, bool use_ipv6, bool nonblock)
	{
		int s, en;

		s = socket(use_ipv6?AF_INET6:AF_INET, SOCK_DGRAM, 0);
		if(s < 0)
		{
			perror("socket");
			return -1;
		}

		if(reuse)
		{
			int reuse_addr = 1;
			setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse_addr,
					sizeof(reuse_addr));
		}

		if(nonblock)
		{
			int flag = fcntl(s, F_GETFL, 0);
			if(flag<0) flag = 0;
			fcntl(s, F_SETFL, flag|O_NONBLOCK|O_NDELAY);
		}

		KnvSockAddr addr;

		/* Setup internet address information.
		This is used with the bind() call */
		if(!use_ipv6)
		{
			memset(&addr.a4, 0, sizeof(addr.a4));
			addr.a4.sin_family = AF_INET;
			addr.a4.sin_port = htons(port);
			addr.a4.sin_addr.s_addr = htonl(INADDR_ANY);
			addr.addr_len = sizeof(addr.a4);
		}
		else
		{
			static struct in6_addr sin6_any = IN6ADDR_ANY_INIT;
			memset(&addr.a6, 0, sizeof(addr.a6));
			addr.a6.sin6_family = AF_INET6;
			addr.a6.sin6_port = htons(port);
			addr.a6.sin6_addr = sin6_any;
			addr.addr_len = sizeof(addr.a6);
		}

		if(bind(s, &addr.addr, addr.addr_len) < 0)
		{
			en = errno;
			perror("bind");
			close(s);
			errno = en;
			return -2;
		}

		return s;
	}

};



