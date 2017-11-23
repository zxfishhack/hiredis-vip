#include "redis_client.h"
#include "hircluster.h"
#include "hiutil.h"
#include <stdarg.h>
#include <stdarg.h>

enum REDIS_SERVER_TYPE
{
	REDIS_SINGLE_NODE,
	REDIS_CLUSTER
};

struct redisClient
{
	int serverType;
	union
	{
		redisContext* node;
		redisClusterContext* cluster;
	};
};

// addr format: ip:port,ip:port
// connect use hiredis when addr has 1 ip:port, 
// connect use hiredis-vip when addr has more than 1 ip:port
redisClient *redisClientConnect(const char *addr)
{
	redisClient* clt = (redisClient*)malloc(sizeof(redisClient));
	if (strstr(addr, ",") == NULL)
	{
		int n;
		int port = 6379;
		char* ip = strdup(addr);
		char* end = strstr(ip, ":");
		if (end == NULL)
		{
			n = strlen(addr);
		}
		else
		{
			*end = 0;
			port = atoi(end + 1);
		}
		clt->serverType = REDIS_SINGLE_NODE;
		clt->node = redisConnect(ip, port);
		free(ip);
	}
	else
	{
		clt->serverType = REDIS_CLUSTER;
		clt->cluster = redisClusterContextInit();
		if (redisClusterSetOptionAddNodes(clt->cluster, addr) != REDIS_OK)
		{
			goto fail;
		}
		if (redisClusterConnect2(clt->cluster) != REDIS_OK)
		{
			goto fail;
		}
	}
exit:
	return clt;
fail:
	free(clt);
	clt = NULL;
	return clt;
}
redisClient *redisClientConnectWithTimeout(const char *addr, const struct timeval tv)
{
	redisClient* clt = (redisClient*)malloc(sizeof(redisClient));
	if (strstr(addr, ",") == NULL)
	{
		int n;
		int port = 6379;
		char* ip = strdup(addr);
		char* end = strstr(ip, ":");
		if (end == NULL)
		{
			n = strlen(addr);
		}
		else
		{
			*end = 0;
			port = atoi(end + 1);
		}
		clt->serverType = REDIS_SINGLE_NODE;
		clt->node = redisConnectWithTimeout(ip, port, tv);
		free(ip);
	}
	else
	{
		clt->serverType = REDIS_CLUSTER;
		clt->cluster = redisClusterContextInit();
		if (redisClusterSetOptionAddNodes(clt->cluster, addr) != REDIS_OK)
		{
			goto fail;
		}
		if (redisClusterSetOptionConnectTimeout(clt->cluster, tv) != REDIS_OK)
		{
			goto fail;
		}
		if (redisClusterConnect2(clt->cluster) != REDIS_OK)
		{
			goto fail;
		}
	}
exit:
	return clt;
fail:
	free(clt);
	clt = NULL;
	return clt;
}

int redisClientReconnect(redisClient *c)
{
	if (c->serverType == REDIS_SINGLE_NODE)
	{
		return redisReconnect(c->node);
	}
	return REDIS_OK;
}

int redisClientSetTimeout(redisClient *c, const struct timeval tv)
{
	if (c->serverType == REDIS_SINGLE_NODE)
	{
		return redisSetTimeout(c->node, tv);
	}
	else if (c->serverType == REDIS_CLUSTER)
	{
		return redisClusterSetOptionTimeout(c->cluster, tv);
	}
	return REDIS_OK;
}

int redisClientEnableKeepAlive(redisClient *c)
{
	if (c->serverType == REDIS_SINGLE_NODE)
	{
		return redisEnableKeepAlive(c->node);
	}
	return REDIS_OK;
}

void redisClientFree(redisClient *c)
{
	if (c->serverType == REDIS_SINGLE_NODE)
	{
		redisFree(c->node);
	}
	else if (c->serverType == REDIS_CLUSTER)
	{
		redisClusterFree(c->cluster);
	}
	free(c);
}

/* In a blocking context, this function first checks if there are unconsumed
* replies to return and returns one if so. Otherwise, it flushes the output
* buffer to the socket and reads until it has a reply. In a non-blocking
* context, it will return unconsumed replies until there are no more. */
int redisClientGetReply(redisClient *c, void **reply)
{
	if (c->serverType == REDIS_SINGLE_NODE)
	{
		return redisGetReply(c->node, reply);
	}
	else if (c->serverType == REDIS_CLUSTER)
	{
		return redisClusterGetReply(c->cluster, reply);
	}
	return REDIS_OK;
}

/* Write a formatted command to the output buffer. Use these functions in blocking mode
* to get a pipeline of commands. */
int redisClientAppendFormattedCommand(redisClient *c, const char *cmd, size_t len)
{
	if (c->serverType == REDIS_SINGLE_NODE)
	{
		return redisAppendFormattedCommand(c->node, cmd, len);
	}
	else if (c->serverType == REDIS_CLUSTER)
	{
		return redisClusterAppendCommand(c->cluster, cmd, len);
	}
	return REDIS_OK;
}

/* Write a command to the output buffer. Use these functions in blocking mode
* to get a pipeline of commands. */
int redisClientvAppendCommand(redisClient *c, const char *format, va_list ap)
{
	if (c->serverType == REDIS_SINGLE_NODE)
	{
		return redisvAppendCommand(c->node, format, ap);
	}
	else if (c->serverType == REDIS_CLUSTER)
	{
		return redisClustervAppendCommand(c->cluster, format, ap);
	}
	return REDIS_OK;
}

int redisClientAppendCommand(redisClient *c, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int ret = redisClientvAppendCommand(c, format, ap);
	va_end(ap);
	return ret;
}

int redisClientAppendCommandArgv(redisClient *c, int argc, const char **argv, const size_t *argvlen)
{
	if (c->serverType == REDIS_SINGLE_NODE)
	{
		return redisAppendCommandArgv(c->node, argc, argv, argvlen);
	}
	else if (c->serverType == REDIS_CLUSTER)
	{
		return redisClusterAppendCommandArgv(c->cluster, argc, argv, argvlen);
	}
	return REDIS_OK;
}

/* Issue a command to Redis. In a blocking context, it is identical to calling
* redisAppendCommand, followed by redisGetReply. The function will return
* NULL if there was an error in performing the request, otherwise it will
* return the reply. In a non-blocking context, it is identical to calling
* only redisAppendCommand and will always return NULL. */
void *redisClientvCommand(redisClient *c, const char *format, va_list ap)
{
	if (c->serverType == REDIS_SINGLE_NODE)
	{
		return redisvCommand(c->node, format, ap);
	}
	else if (c->serverType == REDIS_CLUSTER)
	{
		return redisClustervCommand(c->cluster, format, ap);
	}
	return NULL;
}

void *redisClientCommand(redisClient *c, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	void* reply = redisClientvCommand(c, format, ap);
	va_end(ap);
	return reply;
}

void *redisClientCommandArgv(redisClient *c, int argc, const char **argv, const size_t *argvlen)
{
	if (c->serverType == REDIS_SINGLE_NODE)
	{
		return redisCommandArgv(c->node, argc, argv, argvlen);
	}
	else if (c->serverType == REDIS_CLUSTER)
	{
		return redisClusterCommandArgv(c->cluster, argc, argv, argvlen);
	}
	return NULL;
}