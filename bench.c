#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "hiredis.h"

#define TEST_IP "localhost"
#define TEST_PORT 6379
#define TEST_CMD "FT.AGGREGATE gh * LOAD 2 @type @date"
#define ITERATIONS 3

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    redisReader *reader =
        redisReaderCreateWithFunctions(&redisReplyV2Functions);
    redisContext *ctx = redisConnectWithReader(TEST_IP, TEST_PORT, reader,
                                               &redisReplyV2Accessors);
    // redisContext *ctx = redisConnect(TEST_IP, TEST_PORT);
    assert(ctx);
    for (size_t ii = 0; ii < ITERATIONS; ++ii) {
        redisReply *resp = redisCommand(ctx, TEST_CMD);
        assert(resp);
        assert(REDIS_REPLY_GETTYPE(ctx, resp) == REDIS_REPLY_ARRAY);
        freeReplyObject(resp);
    }
    redisFree(ctx);
    return 0;
}