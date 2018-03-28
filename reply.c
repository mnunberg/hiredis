#include "reply.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static redisReplyV2 emptyStringReply_g = {.type = REDIS_REPLY_FLAG_STATIC |
                                             REDIS_REPLY_FLAG_TINY |
                                             REDIS_REPLY_STRING };

static redisReplyV2 emptyArrayReply_g = {.type = REDIS_REPLY_FLAG_STATIC |
                                                 REDIS_REPLY_FLAG_TINY |
                                                 REDIS_REPLY_ARRAY};

static redisReplyV2 nullReply_g = {.type = REDIS_REPLY_FLAG_STATIC |
                                           REDIS_REPLY_NIL};

static redisReplyV2 integerReply0_g = {
    .type = REDIS_REPLY_FLAG_STATIC | REDIS_REPLY_INTEGER,
    .value = {.integer = 0}};

static redisReplyV2 integerReply1_g = {
    .type = REDIS_REPLY_FLAG_STATIC | REDIS_REPLY_INTEGER,
    .value = {.integer = 1}};

static redisReplyV2 integerReplyNeg1_g = {
    .type = REDIS_REPLY_FLAG_STATIC | REDIS_REPLY_INTEGER,
    .value = {.integer = -1}};

static redisReplyV2 *createV2Reply(const redisReadTask *task) {
    (void)task;
    redisReplyV2 *r = malloc(sizeof(redisReplyV2));
    r->type = REDIS_REPLY_FLAG_V2;
    return r;
}

static redisReplyV2 * assignParentChild(const redisReadTask *task, redisReplyV2 *resp) {
    if (task->parent) {
        redisReplyV2 *parent = task->parent->obj;
        assert((parent->type & REDIS_REPLY_MASK) == REDIS_REPLY_ARRAY);
        if (parent->type & REDIS_REPLY_FLAG_TINY) {
            parent->value.tinyarray.elems[task->idx] = (redisReplyV2 *)resp;
        } else {
            parent->value.array.element[task->idx] = (redisReplyV2 *)resp;
        }
    }
    return (redisReplyV2 *)resp;
}

static void *createStringV2(const redisReadTask *task, char *str, size_t len) {
    redisReplyV2 *r;
    if (len == 0 && task->type == REDIS_REPLY_STRING) {
        return assignParentChild(task, &emptyStringReply_g);
    }

    r = createV2Reply(task);
    r->type |= task->type;

    if (len < REDIS_TINY_STR_BUFBYTES-1) {
        memcpy(r->value.tinystr.str, str, len);
        r->value.tinystr.len = len;
        r->value.tinystr.str[len] = 0;
        r->type |= REDIS_REPLY_FLAG_TINY;
    } else {
        r->value.str.str = malloc(len + 1);
        r->value.str.str[len] = 0;
        r->value.str.len = len;
        memcpy(r->value.str.str, str, len);
    }

    return assignParentChild(task, r);
}

static void *createArrayV2(const redisReadTask *task, int size) {
    if (size == 0) {
        return assignParentChild(task, &emptyArrayReply_g);
    }

    redisReplyV2 *resp = createV2Reply(task);
    resp->type |= REDIS_REPLY_ARRAY;

    if (size <= REDIS_TINY_ARRAY_ELEMS) {
        resp->type |= REDIS_REPLY_FLAG_TINY;
        for (size_t ii = 0; ii < REDIS_TINY_ARRAY_ELEMS; ++ii) {
            resp->value.tinyarray.elems[ii] = NULL;
        }
        return assignParentChild(task, resp);
    }

    resp->value.array.element = malloc(sizeof(redisReplyV2 *) * size);
    resp->value.array.elements = size;
    return assignParentChild(task, resp);
}

static void *createIntegerV2(const redisReadTask *task, long long value) {
    redisReplyV2 *resp;
    if (value == 0) {
        resp = &integerReply0_g;
    } else if (value == 1) {
        resp = &integerReply1_g;
    } else if (value == -1) {
        resp = &integerReplyNeg1_g;
    } else if (value >= 0 && value <= UINT32_MAX) {
        resp = malloc(8);
        resp->type =
            REDIS_REPLY_FLAG_V2 | REDIS_REPLY_FLAG_TINY | REDIS_REPLY_INTEGER;
        resp->intpad = value;
    } else {
        resp = createV2Reply(task);
        resp->type |= REDIS_REPLY_INTEGER;
        resp->value.integer = value;
    }
    return assignParentChild(task, resp);
}

static void *createNilV2(const redisReadTask *task) {
    return assignParentChild(task, &nullReply_g);
}

static void freeV2Reply(void *p) {
    redisReplyV2 *resp = p;
    if (resp == NULL) {
        return;
    }
    int type = resp->type & REDIS_REPLY_MASK;

    if (type == REDIS_REPLY_ARRAY) {
        if (resp->type & REDIS_REPLY_FLAG_TINY) {
            for (size_t ii = 0; ii < REDIS_TINY_ARRAY_ELEMS; ++ii) {
                if (resp->value.tinyarray.elems[ii] == NULL) {
                    break;
                } else {
                    freeV2Reply(resp->value.tinyarray.elems[ii]);
                }
            }
        } else {
            for (size_t ii = 0; ii < resp->value.array.elements; ++ii) {
                freeV2Reply(resp->value.array.element[ii]);
            }
            free(resp->value.array.element);
        }
    } else if (type == REDIS_REPLY_STRING) {
        if (!(resp->type & REDIS_REPLY_FLAG_TINY)) {
            free(resp->value.str.str);
        }
    }

    if (!(resp->type & REDIS_REPLY_FLAG_STATIC)) {
        free(resp);
    }
}

static const char *getV2String(void *obj, size_t *lenp) {
    redisReplyV2 *v2 = obj;
    if (v2->type & REDIS_REPLY_FLAG_TINY) {
        if (lenp) {
            *lenp = v2->value.tinystr.len;
        }
        return v2->value.tinystr.str;
    } else {
        if (lenp) {
            *lenp = v2->value.str.len;
        }
        return v2->value.str.str;
    }
}

static long long getV2Integer(void *obj) {
    redisReplyV2 *v2 = obj;
    if (v2->type & REDIS_REPLY_FLAG_TINY) {
        return v2->intpad;
    } else {
        return v2->value.integer;
    }
}

static size_t getV2ArrayLength(void *obj) {
    redisReplyV2 *v2 = obj;
    if (v2->type & REDIS_REPLY_FLAG_TINY) {
        for (size_t ii = 0; ii < REDIS_TINY_ARRAY_ELEMS; ++ii) {
            if (v2->value.tinyarray.elems[ii] == NULL) {
                return ii;
            }
        }
        return REDIS_TINY_ARRAY_ELEMS;
    } else {
        return v2->value.array.elements;
    }
}

static void *getV2ArrayElement(void *obj, size_t pos) {
    redisReplyV2 *v2 = obj;
    if (v2->type & REDIS_REPLY_FLAG_TINY) {
        return v2->value.tinyarray.elems[pos];
    } else {
        return v2->value.array.element[pos];
    }
}

static int getCommonType(void *obj) {
    return *(int *)obj & REDIS_REPLY_MASK;
}

redisReplyObjectFunctions redisReplyV2Functions = {
    .createString = createStringV2,
    .createArray = createArrayV2,
    .createInteger = createIntegerV2,
    .createNil = createNilV2,
    .freeObject = freeV2Reply};

redisReplyAccessors redisReplyV2Accessors = {
    .getType = getCommonType,
    .getString = getV2String,
    .getInteger = getV2Integer,
    .getArrayLength = getV2ArrayLength,
    .getArrayElement = getV2ArrayElement};

/* Create a reply object */
static redisReplyLegacy *createReplyObject(int type) {
    redisReplyLegacy *r = calloc(1, sizeof(*r));

    if (r == NULL) return NULL;

    r->type = type;
    return r;
}

/* Free a reply object */
void freeLegacyReplyObject(void *reply) {
    redisReplyLegacy *r = reply;
    size_t j;

    if (r == NULL) return;

    switch (r->type) {
        case REDIS_REPLY_INTEGER:
            break; /* Nothing to free */
        case REDIS_REPLY_ARRAY:
            if (r->element != NULL) {
                for (j = 0; j < r->elements; j++)
                    if (r->element[j] != NULL)
                        freeLegacyReplyObject(r->element[j]);
                free(r->element);
            }
            break;
        case REDIS_REPLY_ERROR:
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_STRING:
            if (r->str != NULL) free(r->str);
            break;
    }
    free(r);
}

static void *createLegacyStringObject(const redisReadTask *task, char *str,
                                      size_t len) {
    redisReplyLegacy *r, *parent;
    char *buf;

    r = createReplyObject(task->type);
    if (r == NULL) return NULL;

    buf = malloc(len + 1);
    if (buf == NULL) {
        freeLegacyReplyObject(r);
        return NULL;
    }

    assert(task->type == REDIS_REPLY_ERROR ||
           task->type == REDIS_REPLY_STATUS ||
           task->type == REDIS_REPLY_STRING);

    /* Copy string value */
    memcpy(buf, str, len);
    buf[len] = '\0';
    r->str = buf;
    r->len = len;

    if (task->parent) {
        parent = task->parent->obj;
        assert(parent->type == REDIS_REPLY_ARRAY);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createLegacyArrayObject(const redisReadTask *task, int elements) {
    redisReplyLegacy *r, *parent;

    r = createReplyObject(REDIS_REPLY_ARRAY);
    if (r == NULL) return NULL;

    if (elements > 0) {
        r->element = calloc(elements, sizeof(redisReplyLegacy *));
        if (r->element == NULL) {
            freeLegacyReplyObject(r);
            return NULL;
        }
    }

    r->elements = elements;

    if (task->parent) {
        parent = task->parent->obj;
        assert(parent->type == REDIS_REPLY_ARRAY);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createLegacyIntegerObject(const redisReadTask *task,
                                       long long value) {
    redisReplyLegacy *r, *parent;

    r = createReplyObject(REDIS_REPLY_INTEGER);
    if (r == NULL) return NULL;

    r->integer = value;

    if (task->parent) {
        parent = task->parent->obj;
        assert(parent->type == REDIS_REPLY_ARRAY);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createLegacyNilObject(const redisReadTask *task) {
    redisReplyLegacy *r, *parent;

    r = createReplyObject(REDIS_REPLY_NIL);
    if (r == NULL) return NULL;

    if (task->parent) {
        parent = task->parent->obj;
        assert(parent->type == REDIS_REPLY_ARRAY);
        parent->element[task->idx] = r;
    }
    return r;
}

static const char *getLegacyString(void *obj, size_t *lenp) {
    redisReplyLegacy *reply = obj;
    if (lenp) {
        *lenp = reply->len;
    }
    return reply->str;
}

static long long getLegacyInteger(void *obj) {
    return ((redisReplyLegacy *)obj)->integer;
}

static size_t getLegacyArrayLength(void *obj) {
    return ((redisReplyLegacy *)obj)->elements;
}

static void *getLegacyArrayElement(void *obj, size_t pos) {
    return ((redisReplyLegacy *)obj)->element[pos];
}

/* Default set of functions to build the reply. Keep in mind that such a
 * function returning NULL is interpreted as OOM. */
redisReplyObjectFunctions redisReplyLegacyFunctions = {
    .createString = createLegacyStringObject,
    .createArray = createLegacyArrayObject,
    .createInteger = createLegacyIntegerObject,
    .createNil = createLegacyNilObject,
    .freeObject = freeLegacyReplyObject};


redisReplyAccessors redisReplyLegacyAccessors = {
    .getType = getCommonType,
    .getString = getLegacyString,
    .getInteger = getLegacyInteger,
    .getArrayLength = getLegacyArrayLength,
    .getArrayElement = getLegacyArrayElement};

void freeReplyObject(void *obj) {
    if (*(int *)obj & REDIS_REPLY_FLAG_V2) {
        freeV2Reply(obj);
    } else {
        freeLegacyReplyObject(obj);
    }
}
