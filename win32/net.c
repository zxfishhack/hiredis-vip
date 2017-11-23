/* Extracted from anet.c to work properly with Hiredis error reporting.
*
* Copyright (c) 2009-2011, Salvatore Sanfilippo <antirez at gmail dot com>
* Copyright (c) 2010-2014, Pieter Noordhuis <pcnoordhuis at gmail dot com>
* Copyright (c) 2015, Matt Stancliff <matt at genges dot com>,
*                     Jan-Erik Rediger <janerik at fnordig dot com>
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
*   * Redistributions of source code must retain the above copyright notice,
*     this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in the
*     documentation and/or other materials provided with the distribution.
*   * Neither the name of Redis nor the names of its contributors may be used
*     to endorse or promote products derived from this software without
*     specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include "fmacros.h"

#include "net.h"
#include "sds.h"

#include <ws2tcpip.h>

/* Defined in hiredis.c */
void __redisSetError(redisContext *c, int type, const char *str);

static void redisContextCloseFd(redisContext *c) {
	if (c && c->sock != INVALID_SOCKET) {
		closesocket(c->sock);
		c->sock = INVALID_SOCKET;
	}
}

static void __redisSetErrorFromErrno(redisContext *c, int type, const char *prefix) {
	char buf[128] = { 0 };
	size_t len = 0;

	if (prefix != NULL)
		len = snprintf(buf, sizeof(buf), "%s: ", prefix);
	__redis_strerror_r(errno, (char *)(buf + len), sizeof(buf) - len);
	__redisSetError(c, type, buf);
}

static int redisSetReuseAddr(redisContext *c) {
	int on = 1;
	if (setsockopt(c->sock, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on)) == -1) {
		__redisSetErrorFromErrno(c, REDIS_ERR_IO, NULL);
		redisContextCloseFd(c);
		return REDIS_ERR;
	}
	return REDIS_OK;
}

static int redisCreateSocket(redisContext *c, int type) {
	SOCKET s;
	if ((s = socket(type, SOCK_STREAM, 0)) == -1) {
		__redisSetErrorFromErrno(c, REDIS_ERR_IO, NULL);
		return REDIS_ERR;
	}
	c->sock = s;
	if (type == AF_INET) {
		if (redisSetReuseAddr(c) == REDIS_ERR) {
			return REDIS_ERR;
		}
	}
	return REDIS_OK;
}

static int redisSetBlocking(redisContext *c, int blocking) {
	u_long iMode = 0;

	if (blocking)
		iMode = 0;
	else
		iMode = 1;

	if (ioctlsocket(c->sock, FIONBIO, &iMode) != NO_ERROR) {
		__redisSetErrorFromErrno(c, REDIS_ERR_IO, "fcntl(F_SETFL)");
		redisContextCloseFd(c);
		return REDIS_ERR;
	}
	return REDIS_OK;
}

int redisKeepAlive(redisContext *c, int interval) {
	int val = 1;
	SOCKET fd = c->sock;
	char errBuf[1024];

	if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char*)&val, sizeof(val)) == -1) {
		strerror_s(errBuf, sizeof(errBuf), errno);
		__redisSetError(c, REDIS_ERR_OTHER, errBuf);
		return REDIS_ERR;
	}

	return REDIS_OK;
}

static int redisSetTcpNoDelay(redisContext *c) {
	int yes = 1;
	if (setsockopt(c->sock, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes)) == -1) {
		__redisSetErrorFromErrno(c, REDIS_ERR_IO, "setsockopt(TCP_NODELAY)");
		redisContextCloseFd(c);
		return REDIS_ERR;
	}
	return REDIS_OK;
}

#define __MAX_MSEC (((LONG_MAX) - 999) / 1000)

static int redisContextWaitReady(redisContext *c, const struct timeval *timeout) {
	struct pollfd   wfd[1];
	long msec;

	msec = -1;
	wfd[0].fd = c->sock;
	wfd[0].events = POLLOUT;

	/* Only use timeout when not NULL. */
	if (timeout != NULL) {
		if (timeout->tv_usec > 1000000 || timeout->tv_sec > __MAX_MSEC) {
			__redisSetErrorFromErrno(c, REDIS_ERR_IO, NULL);
			redisContextCloseFd(c);
			return REDIS_ERR;
		}

		msec = (timeout->tv_sec * 1000) + ((timeout->tv_usec + 999) / 1000);

		if (msec < 0 || msec > INT_MAX) {
			msec = INT_MAX;
		}
	}

	if (errno == EINPROGRESS) {
		int res;

		if ((res = WSAPoll(wfd, 1, msec)) == -1) {
			__redisSetErrorFromErrno(c, REDIS_ERR_IO, "poll(2)");
			redisContextCloseFd(c);
			return REDIS_ERR;
		}
		else if (res == 0) {
			errno = ETIMEDOUT;
			__redisSetErrorFromErrno(c, REDIS_ERR_IO, NULL);
			redisContextCloseFd(c);
			return REDIS_ERR;
		}

		if (redisCheckSocketError(c) != REDIS_OK)
			return REDIS_ERR;

		return REDIS_OK;
	}

	__redisSetErrorFromErrno(c, REDIS_ERR_IO, NULL);
	redisContextCloseFd(c);
	return REDIS_ERR;
}

int redisCheckSocketError(redisContext *c) {
	int err = 0;
	int errlen = sizeof(err);

	if (getsockopt(c->sock, SOL_SOCKET, SO_ERROR, (char*)&err, &errlen) == -1) {
		__redisSetErrorFromErrno(c, REDIS_ERR_IO, "getsockopt(SO_ERROR)");
		return REDIS_ERR;
	}

	if (err) {
		errno = err;
		__redisSetErrorFromErrno(c, REDIS_ERR_IO, NULL);
		return REDIS_ERR;
	}

	return REDIS_OK;
}

int redisContextSetTimeout(redisContext *c, const struct timeval tv) {
	if (setsockopt(c->sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv)) == -1) {
		__redisSetErrorFromErrno(c, REDIS_ERR_IO, "setsockopt(SO_RCVTIMEO)");
		return REDIS_ERR;
	}
	if (setsockopt(c->sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv)) == -1) {
		__redisSetErrorFromErrno(c, REDIS_ERR_IO, "setsockopt(SO_SNDTIMEO)");
		return REDIS_ERR;
	}
	return REDIS_OK;
}

static int _redisContextConnectTcp(redisContext *c, const char *addr, int port,
	const struct timeval *timeout,
	const char *source_addr) {
	SOCKET s;
	int rv, n;
	char _port[6];  /* strlen("65535"); */
	struct addrinfo hints, *servinfo, *bservinfo, *p, *b;
	int blocking = (c->flags & REDIS_BLOCK);
	int reuseaddr = (c->flags & REDIS_REUSEADDR);
	int reuses = 0;

	c->connection_type = REDIS_CONN_TCP;
	c->tcp.port = port;

	/* We need to take possession of the passed parameters
	* to make them reusable for a reconnect.
	* We also carefully check we don't free data we already own,
	* as in the case of the reconnect method.
	*
	* This is a bit ugly, but atleast it works and doesn't leak memory.
	**/
	if (c->tcp.host != addr) {
		if (c->tcp.host)
			free(c->tcp.host);

		c->tcp.host = _strdup(addr);
	}

	if (timeout) {
		if (c->timeout != timeout) {
			if (c->timeout == NULL)
				c->timeout = malloc(sizeof(struct timeval));

			memcpy(c->timeout, timeout, sizeof(struct timeval));
		}
	}
	else {
		if (c->timeout)
			free(c->timeout);
		c->timeout = NULL;
	}

	if (source_addr == NULL) {
		free(c->tcp.source_addr);
		c->tcp.source_addr = NULL;
	}
	else if (c->tcp.source_addr != source_addr) {
		free(c->tcp.source_addr);
		c->tcp.source_addr = _strdup(source_addr);
	}

	snprintf(_port, 6, "%d", port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	/* Try with IPv6 if no IPv4 address was found. We do it in this order since
	* in a Redis client you can't afford to test if you have IPv6 connectivity
	* as this would add latency to every connect. Otherwise a more sensible
	* route could be: Use IPv6 if both addresses are available and there is IPv6
	* connectivity. */
	if ((rv = getaddrinfo(c->tcp.host, _port, &hints, &servinfo)) != 0) {
		hints.ai_family = AF_INET6;
		if ((rv = getaddrinfo(addr, _port, &hints, &servinfo)) != 0) {
			__redisSetError(c, REDIS_ERR_OTHER, gai_strerror(rv));
			return REDIS_ERR;
		}
	}
	for (p = servinfo; p != NULL; p = p->ai_next) {
	addrretry:
		if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
			continue;

		c->sock = s;
		if (redisSetBlocking(c, 0) != REDIS_OK)
			goto error;
		if (c->tcp.source_addr) {
			int bound = 0;
			/* Using getaddrinfo saves us from self-determining IPv4 vs IPv6 */
			if ((rv = getaddrinfo(c->tcp.source_addr, NULL, &hints, &bservinfo)) != 0) {
				char buf[128];
				snprintf(buf, sizeof(buf), "Can't get addr: %s", gai_strerror(rv));
				__redisSetError(c, REDIS_ERR_OTHER, buf);
				goto error;
			}

			if (reuseaddr) {
				n = 1;
				if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&n,
					sizeof(n)) < 0) {
					goto error;
				}
			}

			for (b = bservinfo; b != NULL; b = b->ai_next) {
				if (bind(s, b->ai_addr, (int)b->ai_addrlen) != -1) {
					bound = 1;
					break;
				}
			}
			freeaddrinfo(bservinfo);
			if (!bound) {
				char buf[128];
				snprintf(buf, sizeof(buf), "Can't bind socket: %s", strerror(errno));
				__redisSetError(c, REDIS_ERR_OTHER, buf);
				goto error;
			}
		}
		if (connect(s, p->ai_addr, (int)p->ai_addrlen) == -1) {
			if (errno == EHOSTUNREACH) {
				redisContextCloseFd(c);
				continue;
			}
			else if (errno == EINPROGRESS && !blocking) {
				/* This is ok. */
			}
			else if (errno == EADDRNOTAVAIL && reuseaddr) {
				if (++reuses >= REDIS_CONNECT_RETRIES) {
					goto error;
				}
				else {
					goto addrretry;
				}
			}
			else {
				if (redisContextWaitReady(c, c->timeout) != REDIS_OK)
					goto error;
			}
		}
		if (blocking && redisSetBlocking(c, 1) != REDIS_OK)
			goto error;
		if (redisSetTcpNoDelay(c) != REDIS_OK)
			goto error;

		c->flags |= REDIS_CONNECTED;
		rv = REDIS_OK;
		goto end;
	}
	if (p == NULL) {
		char buf[128];
		snprintf(buf, sizeof(buf), "Can't create socket: %s", strerror(errno));
		__redisSetError(c, REDIS_ERR_OTHER, buf);
		goto error;
	}

error:
	rv = REDIS_ERR;
end:
	freeaddrinfo(servinfo);
	return rv;  // Need to return REDIS_OK if alright
}

int redisContextConnectTcp(redisContext *c, const char *addr, int port,
	const struct timeval *timeout) {
	return _redisContextConnectTcp(c, addr, port, timeout, NULL);
}

int redisContextConnectBindTcp(redisContext *c, const char *addr, int port,
	const struct timeval *timeout,
	const char *source_addr) {
	return _redisContextConnectTcp(c, addr, port, timeout, source_addr);
}
