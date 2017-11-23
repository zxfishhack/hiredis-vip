#ifndef _REDIS_REPLY_H_
#define _REDIS_REPLY_H_

#ifdef __cplusplus
extern "C" {
#endif


#define REDIS_REPLY_STRING 1
#define REDIS_REPLY_ARRAY 2
#define REDIS_REPLY_INTEGER 3
#define REDIS_REPLY_NIL 4
#define REDIS_REPLY_STATUS 5
#define REDIS_REPLY_ERROR 6

/* This is the reply object returned by redisCommand() */
typedef struct redisReply {
	int type; /* REDIS_REPLY_* */
	long long integer; /* The integer when type is REDIS_REPLY_INTEGER */
	int len; /* Length of string */
	char *str; /* Used for both REDIS_REPLY_ERROR and REDIS_REPLY_STRING */
	size_t elements; /* number of elements, for REDIS_REPLY_ARRAY */
	struct redisReply **element; /* elements vector for REDIS_REPLY_ARRAY */
} redisReply;

/* Function to free the reply objects hiredis returns by default. */
void freeReplyObject(void *reply);

#ifdef __cplusplus
}
#endif

#endif
