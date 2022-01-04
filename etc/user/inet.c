/*
 * Copyright (c) 2009 Intel Corp., Inc.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>

static int inet_pton4(const char *src, struct in_addr *addr)
{
	unsigned long ip;

	ip = inet_addr(src);
	if (ip == INADDR_NONE)
		return 0;

	addr->S_un.S_addr = ip;
	return 1;
}

enum in6_addr_format_state
{
	in6_fs_num,
	in6_fs_colon,
	in6_fs_0_colon,
	in6_fs_0_num
};

static int inet_check_groups(const char *src)
{
	int i;
	int digits = 0, groups = 0;
	enum in6_addr_format_state state;

	if (src[0] == ':') {
		if (src[1] == ':') {
			i = 2;
			state = in6_fs_0_colon;
		} else {
			return -1;
		}
	} else {
		i = 0;
		state = in6_fs_num;
	}

	for (; src[i] != '\0'; i++) {
		if (src[i] == ':') {

			switch (state) {
			case in6_fs_num:
				state = in6_fs_colon;
				break;
			case in6_fs_colon:
			case in6_fs_0_num:
				state = in6_fs_0_colon;
				break;
			default:
				return -1;
			}
			digits = 0;

		} else if (isxdigit(src[i]) && digits++ < 4) {

			switch (state) {
			case in6_fs_colon:
				state = in6_fs_num;
				groups++;
				break;
			case in6_fs_0_colon:
				state = in6_fs_0_num;
				groups++;
				break;
			default:
				break;
			}
		} else {
			return -1;
		}
	}

	if (groups > 8 || state == in6_fs_colon)
		return -1;
	
	return groups;
}

/*
 * We don't handle the format x:x:x:x:x:x:d.d.d.d
 */
static int inet_pton6(const char *src, struct in6_addr *addr)
{
	const char *pos;
	int i, skip;

	skip = 8 - inet_check_groups(src);
	if (skip > 8)
		return -1;

	memset(addr, 0, sizeof(*addr));
	if (src[0] == ':') {
		pos = src + 2;
		i = skip;
	} else {
		pos = src;
		i = 0;
	}

	for (; i < 8; i++) {
		addr->u.Word[i] = htons((u_short) strtoul(pos, (char **) &pos, 16));
		pos++;
		if (*pos == ':') {
			pos++;
			i += skip;
		}
	}

	return 1;
}

#if WINVER < 0x600
int inet_pton(int af, const char *src, void *dst)
{
	switch (af) {
	case AF_INET:
		return inet_pton4(src, (struct in_addr *) dst);
	case AF_INET6:
		return inet_pton6(src, (struct in6_addr *) dst);
	default:
		return -1;
	}
}
#endif

static const char *inet_ntop4(const void *src, char *dst, socklen_t cnt)
{
	struct sockaddr_in in;

	in.sin_family = AF_INET;
	memcpy(&in.sin_addr, src, 4);
	if (getnameinfo((struct sockaddr *) &in,
					(socklen_t) sizeof(struct sockaddr_in),
					dst, cnt, NULL, 0, NI_NUMERICHOST))
		return NULL;

	return dst;
}

static const char *inet_ntop6(const void *src, char *dst, socklen_t cnt)
{
	struct sockaddr_in6 in6;
	char tmp[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"];
	int i, n = 0;

	memset(&in6, 0, sizeof in6);
	in6.sin6_family = AF_INET6;
	memcpy(&in6.sin6_addr, src, sizeof(struct in_addr6));

	/*
	 * If no ipv6 support return simple IPv6 format rule:
	 * A series of "0's in a 16bit block can be represented by "0" 
	 */
	if (getnameinfo((struct sockaddr *) &in6, (socklen_t) (sizeof in6),
					dst, cnt, NULL, 0, NI_NUMERICHOST)) 
	{
				
		if (cnt < sizeof(tmp))
			return NULL;

		for (i = 0; i < 8; i++) 
			n += sprintf(tmp+n, "%s%x", i ? ":" : "",
						 ntohs(((unsigned short*) src)[i]));
		tmp[n] = '\0';
		strcpy(dst, tmp);
	}
	return dst;
}

#if WINVER < 0x600
const char *inet_ntop(int af, const void *src, char *dst, socklen_t cnt)
{
	switch (af) {
	case AF_INET:
		return inet_ntop4(src, dst, cnt);
	case AF_INET6:
		return inet_ntop6(src, dst, cnt);
	default:
		return NULL;
	}
}
#endif
