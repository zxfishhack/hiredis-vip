#ifndef _REDIS_CLIENT_H_
#define _REDIS_CLIENT_H_

#include "redis_reply.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#ifndef _HIREDIS_VIP_DLL
#define HIREDIS_VIP_EXPORT __declspec(dllimport)
#else
#define HIREDIS_VIP_EXPORT
#endif
#else
#define HIREDIS_VIP_EXPORT
#endif

typedef struct redisClient redisClient;
// addr format: ip:port,ip:port
// connect use hiredis when addr has 1 ip:port, 
// connect use hiredis-vip when addr has more than 1 ip:port
HIREDIS_VIP_EXPORT redisClient *redisClientConnect(const char *addr);
HIREDIS_VIP_EXPORT redisClient *redisClientConnectWithTimeout(const char *addr, const struct timeval tv);

HIREDIS_VIP_EXPORT int redisClientReconnect(redisClient *c);

HIREDIS_VIP_EXPORT int redisClientSetTimeout(redisClient *c, const struct timeval tv);
HIREDIS_VIP_EXPORT int redisClientEnableKeepAlive(redisClient *c);
HIREDIS_VIP_EXPORT void redisClientFree(redisClient *c);

/* In a blocking context, this function first checks if there are unconsumed
* replies to return and returns one if so. Otherwise, it flushes the output
* buffer to the socket and reads until it has a reply. In a non-blocking
* context, it will return unconsumed replies until there are no more. */
HIREDIS_VIP_EXPORT int redisClientGetReply(redisClient *c, void **reply);

/* Write a formatted command to the output buffer. Use these functions in blocking mode
* to get a pipeline of commands. */
HIREDIS_VIP_EXPORT int redisClientAppendFormattedCommand(redisClient *c, const char *cmd, size_t len);

/* Write a command to the output buffer. Use these functions in blocking mode
* to get a pipeline of commands. */
HIREDIS_VIP_EXPORT int redisClientvAppendCommand(redisClient *c, const char *format, va_list ap);
HIREDIS_VIP_EXPORT int redisClientAppendCommand(redisClient *c, const char *format, ...);
HIREDIS_VIP_EXPORT int redisClientAppendCommandArgv(redisClient *c, int argc, const char **argv, const size_t *argvlen);

/* Issue a command to Redis. In a blocking context, it is identical to calling
* redisAppendCommand, followed by redisGetReply. The function will return
* NULL if there was an error in performing the request, otherwise it will
* return the reply. In a non-blocking context, it is identical to calling
* only redisAppendCommand and will always return NULL. */
HIREDIS_VIP_EXPORT void *redisClientvCommand(redisClient *c, const char *format, va_list ap);
HIREDIS_VIP_EXPORT void *redisClientCommand(redisClient *c, const char *format, ...);
HIREDIS_VIP_EXPORT void *redisClientCommandArgv(redisClient *c, int argc, const char **argv, const size_t *argvlen);

#ifdef __cplusplus
}
#endif

#endif
