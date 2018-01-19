/* Redis Object implementation.
 *
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include "redis.h"
#include <math.h>
#include <ctype.h>

/*
 * ����һ���� robj ����
 */
robj *createObject(int type, void *ptr) {

    robj *o = zmalloc(sizeof(*o));

    o->type = type;
    o->encoding = REDIS_ENCODING_RAW;
    o->ptr = ptr;
    o->refcount = 1;

    /* Set the LRU to the current lruclock (minutes resolution). */
    o->lru = LRU_CLOCK();
    return o;
}

/* Create a string object with encoding REDIS_ENCODING_RAW, that is a plain
 * string object where o->ptr points to a proper sds string. */
// ����һ�� REDIS_ENCODING_RAW ������ַ�����
// �����ָ��ָ��һ�� sds �ṹ
robj *createRawStringObject(char *ptr, size_t len) {
    return createObject(REDIS_STRING,sdsnewlen(ptr,len));
}

/* Create a string object with encoding REDIS_ENCODING_EMBSTR, that is
 * an object where the sds string is actually an unmodifiable string
 * allocated in the same chunk as the object itself. */
// ����һ�� REDIS_ENCODING_EMBSTR ������ַ�����
// ����ַ��������е� sds ����ַ�������� redisObject �ṹһ�����
// �������ַ�Ҳ�ǲ����޸ĵ�
robj *createEmbeddedStringObject(char *ptr, size_t len) {
    robj *o = zmalloc(sizeof(robj)+sizeof(struct sdshdr)+len+1);
    struct sdshdr *sh = (void*)(o+1);

    o->type = REDIS_STRING;
    o->encoding = REDIS_ENCODING_EMBSTR;
    o->ptr = sh+1;
    o->refcount = 1;
    o->lru = LRU_CLOCK();

    sh->len = len;
    sh->free = 0;
    if (ptr) {
        memcpy(sh->buf,ptr,len);
        sh->buf[len] = '\0';
    } else {
        memset(sh->buf,0,len+1);
    }
    return o;
}

/* Create a string object with EMBSTR encoding if it is smaller than
 * REIDS_ENCODING_EMBSTR_SIZE_LIMIT, otherwise the RAW encoding is
 * used.
 *
 * The current limit of 39 is chosen so that the biggest string object
 * we allocate as EMBSTR will still fit into the 64 byte arena of jemalloc. */
#define REDIS_ENCODING_EMBSTR_SIZE_LIMIT 39
robj *createStringObject(char *ptr, size_t len) {
    /* �����ַ�ʽ���������ֽ����ٵ�ʱ��ֱ��һ���Է���zmalloc(sizeof(robj)+sizeof(struct sdshdr)+len+1) 
        �������39��ֱ����sizeof(robj)��sizeof(struct sdshdr)+len�ռ� */
    if (len <= REDIS_ENCODING_EMBSTR_SIZE_LIMIT)
        return createEmbeddedStringObject(ptr,len);
    else
        return createRawStringObject(ptr,len);
}

/*
 * ���ݴ��������ֵ������һ���ַ�������
 *
 * ����ַ����Ķ��󱣴�Ŀ����� INT ����� long ֵ��
 * Ҳ������ RAW ����ġ���ת�����ַ����� long long ֵ��
 */
robj *createStringObjectFromLongLong(long long value) {

    robj *o;

    // value �Ĵ�С���� REDIS ���������ķ�Χ
    // ��ô����һ���������
    if (value >= 0 && value < REDIS_SHARED_INTEGERS) {
        incrRefCount(shared.integers[value]);
        o = shared.integers[value];

    // �����Ϲ���Χ������һ���µ���������
    } else {
        // ֵ������ long ���ͱ��棬
        // ����һ�� REDIS_ENCODING_INT ������ַ�������
        if (value >= LONG_MIN && value <= LONG_MAX) {
            o = createObject(REDIS_STRING, NULL);
            o->encoding = REDIS_ENCODING_INT;
            o->ptr = (void*)((long)value);

        // ֵ������ long ���ͱ��棨long long ���ͣ�����ֵת��Ϊ�ַ�����
        // ������һ�� REDIS_ENCODING_RAW ���ַ�������������ֵ
        } else {
            o = createObject(REDIS_STRING,sdsfromlonglong(value));
        }
    }

    return o;
}

/* Note: this function is defined into object.c since here it is where it
 * belongs but it is actually designed to be used just for INCRBYFLOAT */
/*
 * ���ݴ���� long double ֵ��Ϊ������һ���ַ�������
 *
 * ���� long double ת��Ϊ�ַ���������
 */
robj *createStringObjectFromLongDouble(long double value) {
    char buf[256];
    int len;

    /* We use 17 digits precision since with 128 bit floats that precision
     * after rounding is able to represent most small decimal numbers in a way
     * that is "non surprising" for the user (that is, most small decimal
     * numbers will be represented in a way that when converted back into
     * a string are exactly the same as what the user typed.) */
    // ʹ�� 17 λС�����ȣ����־��ȿ����ڴ󲿷ֻ����ϱ� rounding �����ı�
    len = snprintf(buf,sizeof(buf),"%.17Lf", value);

    /* Now remove trailing zeroes after the '.' */
    // �Ƴ�β���� 0 
    // ���� 3.1400000 ����� 3.14
    // �� 3.00000 ����� 3
    if (strchr(buf,'.') != NULL) {
        char *p = buf+len-1;
        while(*p == '0') {
            p--;
            len--;
        }
        // �������ҪС���㣬��ô�Ƴ���
        if (*p == '.') len--;
    }

    // ��������
    return createStringObject(buf,len);
}

/* Duplicate a string object, with the guarantee that the returned object
 * has the same encoding as the original one.
 *
 * ����һ���ַ������󣬸��Ƴ��Ķ�����������ӵ����ͬ���롣
 *
 * This function also guarantees that duplicating a small integere object
 * (or a string object that contains a representation of a small integer)
 * will always result in a fresh object that is unshared (refcount == 1).
 *
 * ���⣬
 * ��������ڸ���һ����������ֵ���ַ�������ʱ�����ǲ���һ���ǹ���Ķ���
 *
 * The resulting object always has refcount set to 1. 
 *
 * �������� refcount ��Ϊ 1 ��
 */
robj *dupStringObject(robj *o) {
    robj *d;

    redisAssert(o->type == REDIS_STRING);

    switch(o->encoding) {

    case REDIS_ENCODING_RAW:
        return createRawStringObject(o->ptr,sdslen(o->ptr));

    case REDIS_ENCODING_EMBSTR:
        return createEmbeddedStringObject(o->ptr,sdslen(o->ptr));

    case REDIS_ENCODING_INT:
        d = createObject(REDIS_STRING, NULL);
        d->encoding = REDIS_ENCODING_INT;
        d->ptr = o->ptr;
        return d;

    default:
        redisPanic("Wrong encoding.");
        break;
    }
}

/*
 * ����һ�� LINKEDLIST ������б����
 */
robj *createListObject(void) {

    list *l = listCreate();

    robj *o = createObject(REDIS_LIST,l);

    listSetFreeMethod(l,decrRefCountVoid);

    o->encoding = REDIS_ENCODING_LINKEDLIST;

    return o;
}

/*
 * ����һ�� ZIPLIST ������б����
 */

/*
lpush����ͨ��pushGenericCommand->createZiplistObject�����б����(Ĭ�ϱ��뷽ʽREDIS_ENCODING_ZIPLIST)��Ȼ����listTypePush->listTypeTryConversion��
�����б��нڵ����Ƿ�������ò���list_max_ziplist_value(Ĭ��64)�����������value�б�ֵ��ѹ�����Ϊ˫��������뷽ʽREDIS_ENCODING_LINKEDLIST����listTypePush->listTypeConvert
*/
robj *createZiplistObject(void) {

    unsigned char *zl = ziplistNew();

    robj *o = createObject(REDIS_LIST,zl);

    o->encoding = REDIS_ENCODING_ZIPLIST;

    return o;
}

/*
 * ����һ�� SET ����ļ��϶���
 */
robj *createSetObject(void) {

    dict *d = dictCreate(&setDictType,NULL);

    robj *o = createObject(REDIS_SET,d);

    o->encoding = REDIS_ENCODING_HT;

    return o;
}

/*
 * ����һ�� INTSET ����ļ��϶���
 */
robj *createIntsetObject(void) {

    intset *is = intsetNew();

    robj *o = createObject(REDIS_SET,is);

    o->encoding = REDIS_ENCODING_INTSET;

    return o;
}

/*
 * ����һ�� ZIPLIST ����Ĺ�ϣ����
 */
robj *createHashObject(void) {

    unsigned char *zl = ziplistNew();

    robj *o = createObject(REDIS_HASH, zl);

    o->encoding = REDIS_ENCODING_ZIPLIST;

    return o;
}

/*
 * ����һ�� SKIPLIST ��������򼯺�
 */
robj *createZsetObject(void) {

    zset *zs = zmalloc(sizeof(*zs));

    robj *o;

    zs->dict = dictCreate(&zsetDictType,NULL);
    zs->zsl = zslCreate();

    o = createObject(REDIS_ZSET,zs);

    o->encoding = REDIS_ENCODING_SKIPLIST;

    return o;
}

/*
 * ����һ�� ZIPLIST ��������򼯺�
 */
robj *createZsetZiplistObject(void) {

    unsigned char *zl = ziplistNew();

    robj *o = createObject(REDIS_ZSET,zl);

    o->encoding = REDIS_ENCODING_ZIPLIST;

    return o;
}

/*
 * �ͷ��ַ�������
 */
void freeStringObject(robj *o) {
    if (o->encoding == REDIS_ENCODING_RAW) {
        sdsfree(o->ptr);
    }
}

/*
 * �ͷ��б����
 */
void freeListObject(robj *o) {

    switch (o->encoding) {

    case REDIS_ENCODING_LINKEDLIST:
        listRelease((list*) o->ptr);
        break;

    case REDIS_ENCODING_ZIPLIST:
        zfree(o->ptr);
        break;

    default:
        redisPanic("Unknown list encoding type");
    }
}

/*
 * �ͷż��϶���
 */
void freeSetObject(robj *o) {

    switch (o->encoding) {

    case REDIS_ENCODING_HT:
        dictRelease((dict*) o->ptr);
        break;

    case REDIS_ENCODING_INTSET:
        zfree(o->ptr);
        break;

    default:
        redisPanic("Unknown set encoding type");
    }
}

/*
 * �ͷ����򼯺϶���
 */
void freeZsetObject(robj *o) {

    zset *zs;

    switch (o->encoding) {

    case REDIS_ENCODING_SKIPLIST:
        zs = o->ptr;
        dictRelease(zs->dict);
        zslFree(zs->zsl);
        zfree(zs);
        break;

    case REDIS_ENCODING_ZIPLIST:
        zfree(o->ptr);
        break;

    default:
        redisPanic("Unknown sorted set encoding");
    }
}

/*
 * �ͷŹ�ϣ����
 */
void freeHashObject(robj *o) {

    switch (o->encoding) {

    case REDIS_ENCODING_HT:
        dictRelease((dict*) o->ptr);
        break;

    case REDIS_ENCODING_ZIPLIST:
        zfree(o->ptr);
        break;

    default:
        redisPanic("Unknown hash encoding type");
        break;
    }
}

/*
 * Ϊ��������ü�����һ
 */
void incrRefCount(robj *o) {
    o->refcount++;
}

/*
 * Ϊ��������ü�����һ
 *
 * ����������ü�����Ϊ 0 ʱ���ͷŶ���
 */
void decrRefCount(robj *o) {

    if (o->refcount <= 0) redisPanic("decrRefCount against refcount <= 0");

    // �ͷŶ���
    if (o->refcount == 1) {
        switch(o->type) {
        case REDIS_STRING: freeStringObject(o); break;
        case REDIS_LIST: freeListObject(o); break;
        case REDIS_SET: freeSetObject(o); break;
        case REDIS_ZSET: freeZsetObject(o); break;
        case REDIS_HASH: freeHashObject(o); break;
        default: redisPanic("Unknown object type"); break;
        }
        zfree(o);

    // ���ټ���
    } else {
        o->refcount--;
    }
}

/* This variant of decrRefCount() gets its argument as void, and is useful
 * as free method in data structures that expect a 'void free_object(void*)'
 * prototype for the free method. 
 *
 * �������ض����ݽṹ���ͷź�����װ
 */
void decrRefCountVoid(void *o) {
    decrRefCount(o);
}

/* This function set the ref count to zero without freeing the object.
 *
 * �����������������ü�����Ϊ 0 ���������ͷŶ���
 *
 * It is useful in order to pass a new object to functions incrementing
 * the ref count of the received object. Example:
 *
 * ��������ڽ�һ��������һ�����������ü����ĺ�����ʱ���ǳ����á�
 * ����������
 *
 *    functionThatWillIncrementRefCount(resetRefCount(CreateObject(...)));
 *
 * Otherwise you need to resort to the less elegant pattern:
 *
 * û����������Ļ�������ͻ�Ƚ��鷳�ˣ�
 *
 *    *obj = createObject(...);
 *    functionThatWillIncrementRefCount(obj);
 *    decrRefCount(obj);
 */
robj *resetRefCount(robj *obj) {
    obj->refcount = 0;
    return obj;
}

/*
 * ������ o �������Ƿ�� type ��ͬ��
 *
 *  - ��ͬ���� 0 
 *
 *  - ����ͬ���� 1 ������ͻ��˻ظ�һ������
 */
int checkType(redisClient *c, robj *o, int type) {

    if (o->type != type) {
        addReply(c,shared.wrongtypeerr);
        return 1;
    }

    return 0;
}

/*
 * ������ o �е�ֵ�ܷ��ʾΪ long long ���ͣ�
 *
 *  - �����򷵻� REDIS_OK ������ long long ֵ���浽 *llval �С�
 *
 *  - �������򷵻� REDIS_ERR
 */ //��ȡo��Ӧ������ֵ
int isObjectRepresentableAsLongLong(robj *o, long long *llval) {

    redisAssertWithInfo(NULL,o,o->type == REDIS_STRING);

    // INT ����� long ֵ�����ܱ���Ϊ long long
    if (o->encoding == REDIS_ENCODING_INT) {
        if (llval) *llval = (long) o->ptr;
        return REDIS_OK;

    // ������ַ����Ļ�����ô���Խ���ת��Ϊ long long
    } else {
        return string2ll(o->ptr,sdslen(o->ptr),llval) ? REDIS_OK : REDIS_ERR;
    }
}

/* Try to encode a string object in order to save space */
// ���Զ��ַ���������б��룬�Խ�Լ�ڴ档
robj *tryObjectEncoding(robj *o) {
    long value;

    sds s = o->ptr;
    size_t len;

    /* Make sure this is a string object, the only type we encode
     * in this function. Other types use encoded memory efficient
     * representations but are handled by the commands implementing
     * the type. */
    redisAssertWithInfo(NULL,o,o->type == REDIS_STRING);

    /* We try some specialized encoding only for objects that are
     * RAW or EMBSTR encoded, in other words objects that are still
     * in represented by an actually array of chars. */
    // ֻ���ַ����ı���Ϊ RAW ���� EMBSTR ʱ���Խ��б���
    if (!sdsEncodedObject(o)) return o;

    /* It's not safe to encode shared objects: shared objects can be shared
     * everywhere in the "object space" of Redis and may end in places where
     * they are not handled. We handle them only as values in the keyspace. */
     // ���Թ��������б���
     if (o->refcount > 1) return o;

    /* Check if we can represent this string as a long integer.
     * Note that we are sure that a string larger than 21 chars is not
     * representable as a 32 nor 64 bit integer. */
    // ���ַ������м��
    // ֻ�Գ���С�ڻ���� 21 �ֽڣ����ҿ��Ա�����Ϊ�������ַ������б���
    len = sdslen(s);
    if (len <= 21 && string2l(s,len,&value)) { //����������ַ���
        /* This object is encodable as a long. Try to use a shared object.
         * Note that we avoid using shared integers when maxmemory is used
         * because every object needs to have a private LRU field for the LRU
         * algorithm to work well. */
        if (server.maxmemory == 0 &&
            value >= 0 &&
            value < REDIS_SHARED_INTEGERS)
        {
            decrRefCount(o); //�����10000���ڵ��ַ�������ֱ��ʹ��shared.integers[value]��Ǿ����ˣ����������ü���
            incrRefCount(shared.integers[value]);
            return shared.integers[value];
        } else { //����Ǵ���10000���ַ�������ֱ��ת��ΪREDIS_ENCODING_INT���뷽ʽ�洢��
            if (o->encoding == REDIS_ENCODING_RAW) sdsfree(o->ptr);
            o->encoding = REDIS_ENCODING_INT;
            o->ptr = (void*) value; //ֱ����ptr�洢�ַ�����Ӧ��������ת��Ϊ��ַ
            return o;
        }
    }

    /* If the string is small and is still RAW encoded,
     * try the EMBSTR encoding which is more efficient.
     * In this representation the object and the SDS string are allocated
     * in the same chunk of memory to save space and cache misses. */
    // ���Խ� RAW ������ַ�������Ϊ EMBSTR ����
    if (len <= REDIS_ENCODING_EMBSTR_SIZE_LIMIT) { 
    //����ַ���С��39������֮ǰ����REDIS_ENCODING_EMBSTR(obj+sdshdr+data)�ڴ������ģ���ת��ΪREDIS_ENCODING_EMBSTR�ڴ��������뷽ʽ
        robj *emb;

        if (o->encoding == REDIS_ENCODING_EMBSTR) return o;
        emb = createEmbeddedStringObject(s,sdslen(s));
        decrRefCount(o);
        return emb;
    }

    /* We can't encode the object...
     *
     * Do the last try, and at least optimize the SDS string inside
     * the string object to require little space, in case there
     * is more than 10% of free space at the end of the SDS string.
     *
     * We do that only for relatively large strings as this branch
     * is only entered if the length of the string is greater than
     * REDIS_ENCODING_EMBSTR_SIZE_LIMIT. */
    // �������û�취���б��룬���Դ� SDS ���Ƴ����п���ռ�
    if (o->encoding == REDIS_ENCODING_RAW &&
        sdsavail(s) > len/10)  //ʣ��ռ�����ܷ���obj�ռ��ʮ��֮һ
    {
        o->ptr = sdsRemoveFreeSpace(o->ptr);
    }

    /* Return the original object. */
    return o;
}

/* Get a decoded version of an encoded object (returned as a new object).
 *
 * ���¶������ʽ������һ���������Ľ���汾��RAW ���룩��
 *
 * If the object is already raw-encoded just increment the ref count. 
 *
 * ��������Ѿ��� RAW ����ģ���ô�������������ü�����һ��
 * Ȼ�󷵻��������
 */
robj *getDecodedObject(robj *o) {
    robj *dec;

    if (sdsEncodedObject(o)) {
        incrRefCount(o);
        return o;
    }

    // ������󣬽������ֵ������ת��Ϊ�ַ���
    if (o->type == REDIS_STRING && o->encoding == REDIS_ENCODING_INT) {
        char buf[32];

        ll2string(buf,32,(long)o->ptr);
        dec = createStringObject(buf,strlen(buf));
        return dec;

    } else {
        redisPanic("Unknown encoding type");
    }
}

/* Compare two string objects via strcmp() or strcoll() depending on flags.
 *
 * ���� flags ��ֵ��������ʹ�� strcmp() ���� strcoll() ���Ա��ַ�������
 *
 * Note that the objects may be integer-encoded. In such a case we
 * use ll2string() to get a string representation of the numbers on the stack
 * and compare the strings, it's much faster than calling getDecodedObject().
 *
 * ע�⣬��Ϊ�ַ����������ʵ���ϱ����������ֵ��
 * ������������������ô�����Ƚ�����ת��Ϊ�ַ�����
 * Ȼ���ٶԱ������ַ�����
 * ���������ȵ��� getDecodedObject() ����
 *
 * Important note: when REDIS_COMPARE_BINARY is used a binary-safe comparison
 * is used. 
 * �� flags Ϊ REDIS_COMPARE_BINARY ʱ��
 * �Ա��Զ����ư�ȫ�ķ�ʽ���С�
 */

#define REDIS_COMPARE_BINARY (1<<0)
#define REDIS_COMPARE_COLL (1<<1)

int compareStringObjectsWithFlags(robj *a, robj *b, int flags) {
    redisAssertWithInfo(NULL,a,a->type == REDIS_STRING && b->type == REDIS_STRING);

    char bufa[128], bufb[128], *astr, *bstr;
    size_t alen, blen, minlen;

    if (a == b) return 0;

	// ָ���ַ���ֵ����������Ҫʱ��������ת��Ϊ�ַ��� a
    if (sdsEncodedObject(a)) {
        astr = a->ptr;
        alen = sdslen(astr);
    } else {
        alen = ll2string(bufa,sizeof(bufa),(long) a->ptr);
        astr = bufa;
    }

	// ͬ�������ַ��� b
    if (sdsEncodedObject(b)) {
        bstr = b->ptr;
        blen = sdslen(bstr);
    } else {
        blen = ll2string(bufb,sizeof(bufb),(long) b->ptr);
        bstr = bufb;
    }


	// �Ա�
    if (flags & REDIS_COMPARE_COLL) {
        return strcoll(astr,bstr);
    } else {
        int cmp;

        minlen = (alen < blen) ? alen : blen;
        cmp = memcmp(astr,bstr,minlen);
        if (cmp == 0) return alen-blen;
        return cmp;
    }
}

/* Wrapper for compareStringObjectsWithFlags() using binary comparison. */
int compareStringObjects(robj *a, robj *b) {
    return compareStringObjectsWithFlags(a,b,REDIS_COMPARE_BINARY);
}

/* Wrapper for compareStringObjectsWithFlags() using collation. */
int collateStringObjects(robj *a, robj *b) {
    return compareStringObjectsWithFlags(a,b,REDIS_COMPARE_COLL);
}

/* Equal string objects return 1 if the two objects are the same from the
 * point of view of a string comparison, otherwise 0 is returned. 
 *
 * ������������ֵ���ַ�������ʽ����ȣ���ô���� 1 �� ���򷵻� 0 ��
 *
 * Note that this function is faster then checking for (compareStringObject(a,b) == 0)
 * because it can perform some more optimization. 
 *
 * �������������Ӧ���Ż������Ա� (compareStringObject(a, b) == 0) ����һЩ��
 */
int equalStringObjects(robj *a, robj *b) {

    // ����ı���Ϊ INT ��ֱ�ӶԱ�ֵ
    // ��������˽�����ֵת��Ϊ�ַ���������Ч�ʸ���
    if (a->encoding == REDIS_ENCODING_INT &&
        b->encoding == REDIS_ENCODING_INT){
        /* If both strings are integer encoded just check if the stored
         * long is the same. */
        return a->ptr == b->ptr;

    // �����ַ�������
    } else {
        return compareStringObjects(a,b) == 0;
    }
}

/*
 * �����ַ����������ַ���ֵ�ĳ���
 */
size_t stringObjectLen(robj *o) {

    redisAssertWithInfo(NULL,o,o->type == REDIS_STRING);

    if (sdsEncodedObject(o)) {
        return sdslen(o->ptr);

    // INT ���룬���㽫���ֵת��Ϊ�ַ���Ҫ�����ֽ�
    // �൱�ڷ������ĳ���
    } else {
        char buf[32];
        return ll2string(buf,32,(long)o->ptr);
    }
}

/*
 * ���ԴӶ�����ȡ�� double ֵ
 *
 *  - ת���ɹ���ֵ������ *target �У��������� REDIS_OK
 *
 *  - ���򣬺������� REDIS_ERR
 */
int getDoubleFromObject(robj *o, double *target) {
    double value;
    char *eptr;

    if (o == NULL) {
        value = 0;

    } else {
        redisAssertWithInfo(NULL,o,o->type == REDIS_STRING);

        // ���Դ��ַ�����ת�� double ֵ
        if (sdsEncodedObject(o)) {
            errno = 0;
            value = strtod(o->ptr, &eptr);
            if (isspace(((char*)o->ptr)[0]) ||
                eptr[0] != '\0' ||
                (errno == ERANGE &&
                    (value == HUGE_VAL || value == -HUGE_VAL || value == 0)) ||
                errno == EINVAL ||
                isnan(value))
                return REDIS_ERR;

        // INT ����
        } else if (o->encoding == REDIS_ENCODING_INT) {
            value = (long)o->ptr;

        } else {
            redisPanic("Unknown string encoding");
        }
    }

    // ����ֵ
    *target = value;
    return REDIS_OK;
}

/*
 * ���ԴӶ��� o ��ȡ�� double ֵ��
 *
 *  - �������ʧ�ܵĻ����ͷ���ָ���Ļظ� msg ���ͻ��ˣ��������� REDIS_ERR ��
 *
 *  - ȡ���ɹ��Ļ�����ֵ������ *target �У��������� REDIS_OK ��
 */
int getDoubleFromObjectOrReply(redisClient *c, robj *o, double *target, const char *msg) {

    double value;

    if (getDoubleFromObject(o, &value) != REDIS_OK) {
        if (msg != NULL) {
            addReplyError(c,(char*)msg);
        } else {
            addReplyError(c,"value is not a valid float");
        }
        return REDIS_ERR;
    }

    *target = value;
    return REDIS_OK;
}

/*
 * ���ԴӶ�����ȡ�� long double ֵ
 *
 *  - ת���ɹ���ֵ������ *target �У��������� REDIS_OK
 *
 *  - ���򣬺������� REDIS_ERR
 */
int getLongDoubleFromObject(robj *o, long double *target) {
    long double value;
    char *eptr;

    if (o == NULL) {
        value = 0;
    } else {

        redisAssertWithInfo(NULL,o,o->type == REDIS_STRING);

        // RAW ���룬���Դ��ַ�����ת�� long double
        if (sdsEncodedObject(o)) {
            errno = 0;
            value = strtold(o->ptr, &eptr);
            if (isspace(((char*)o->ptr)[0]) || eptr[0] != '\0' ||
                errno == ERANGE || isnan(value))
                return REDIS_ERR;

        // INT ���룬ֱ�ӱ���
        } else if (o->encoding == REDIS_ENCODING_INT) {
            value = (long)o->ptr;

        } else {
            redisPanic("Unknown string encoding");
        }
    }

    *target = value;
    return REDIS_OK;
}

/*
 * ���ԴӶ��� o ��ȡ�� long double ֵ��
 *
 *  - �������ʧ�ܵĻ����ͷ���ָ���Ļظ� msg ���ͻ��ˣ��������� REDIS_ERR ��
 *
 *  - ȡ���ɹ��Ļ�����ֵ������ *target �У��������� REDIS_OK ��
 */
int getLongDoubleFromObjectOrReply(redisClient *c, robj *o, long double *target, const char *msg) {

    long double value;

    if (getLongDoubleFromObject(o, &value) != REDIS_OK) {
        if (msg != NULL) {
            addReplyError(c,(char*)msg);
        } else {
            addReplyError(c,"value is not a valid float");
        }
        return REDIS_ERR;
    }

    *target = value;
    return REDIS_OK;
}

/*
 * ���ԴӶ��� o ��ȡ������ֵ��
 * ���߳��Խ����� o �������ֵת��Ϊ����ֵ��
 * �����������ֵ���浽 *target �С�
 *
 * ��� o Ϊ NULL ����ô�� *target ��Ϊ 0 ��
 *
 * ������� o �е�ֵ�������������Ҳ���ת��Ϊ��������ô�������� REDIS_ERR ��
 *
 * �ɹ�ȡ�����߳ɹ�����ת��ʱ������ REDIS_OK ��
 *
 * T = O(N)
 */ //��ȡo��Ӧ������
int getLongLongFromObject(robj *o, long long *target) {
    long long value;
    char *eptr;

    if (o == NULL) {
        // o Ϊ NULL ʱ����ֵ��Ϊ 0 ��
        value = 0;
    } else {

        // ȷ������Ϊ REDIS_STRING ����
        redisAssertWithInfo(NULL,o,o->type == REDIS_STRING);
        if (sdsEncodedObject(o)) {
            errno = 0;
            // T = O(N)
            value = strtoll(o->ptr, &eptr, 10);
            if (isspace(((char*)o->ptr)[0]) || eptr[0] != '\0' ||
                errno == ERANGE)
                return REDIS_ERR;
        } else if (o->encoding == REDIS_ENCODING_INT) {
            // ���� REDIS_ENCODING_INT ���������ֵ
            // ֱ�ӽ�����ֵ���浽 value ��
            value = (long)o->ptr;
        } else {
            redisPanic("Unknown string encoding");
        }
    }

    // ����ֵ��ָ��
    if (target) *target = value;

    // ���ؽ����ʶ��
    return REDIS_OK;
}

/*
 * ���ԴӶ��� o ��ȡ������ֵ��
 * ���߳��Խ����� o �е�ֵת��Ϊ����ֵ��
 * ��������ó�������ֵ���浽 *target ��
 *
 * ���ȡ��/ת���ɹ��Ļ������� REDIS_OK ��
 * ���򣬷��� REDIS_ERR ������ͻ��˷���һ������ظ���
 *
 * T = O(N)
 */ //��ȡo��Ӧ�����ִ浽target��
int getLongLongFromObjectOrReply(redisClient *c, robj *o, long long *target, const char *msg) {

    long long value;

    // T = O(N)
    if (getLongLongFromObject(o, &value) != REDIS_OK) {
        if (msg != NULL) {
            addReplyError(c,(char*)msg);
        } else {
            addReplyError(c,"value is not an integer or out of range");
        }
        return REDIS_ERR;
    }

    *target = value;

    return REDIS_OK;
}

/*
 * ���ԴӶ��� o ��ȡ�� long ����ֵ��
 * ���߳��Խ����� o �е�ֵת��Ϊ long ����ֵ��
 * ��������ó�������ֵ���浽 *target ��
 *
 * ���ȡ��/ת���ɹ��Ļ������� REDIS_OK ��
 * ���򣬷��� REDIS_ERR ������ͻ��˷���һ�� msg ����ظ���
 */ //��ȡo��Ӧ�����ִ浽target��
int getLongFromObjectOrReply(redisClient *c, robj *o, long *target, const char *msg) {
    long long value;

    // �ȳ����� long long ����ȡ��ֵ
    if (getLongLongFromObjectOrReply(c, o, &value, msg) != REDIS_OK) return REDIS_ERR;

    // Ȼ����ֵ�Ƿ��� long ���͵ķ�Χ֮��
    if (value < LONG_MIN || value > LONG_MAX) {
        if (msg != NULL) {
            addReplyError(c,(char*)msg);
        } else {
            addReplyError(c,"value is out of range");
        }
        return REDIS_ERR;
    }

    *target = value;
    return REDIS_OK;
}

/*
 * ���ر�����ַ�����ʾ
 */
char *strEncoding(int encoding) {

    switch(encoding) {

    case REDIS_ENCODING_RAW: return "raw";
    case REDIS_ENCODING_INT: return "int";
    case REDIS_ENCODING_HT: return "hashtable";
    case REDIS_ENCODING_LINKEDLIST: return "linkedlist";
    case REDIS_ENCODING_ZIPLIST: return "ziplist";
    case REDIS_ENCODING_INTSET: return "intset";
    case REDIS_ENCODING_SKIPLIST: return "skiplist";
    case REDIS_ENCODING_EMBSTR: return "embstr";
    default: return "unknown";
    }
}

/* Given an object returns the min number of milliseconds the object was never
 * requested, using an approximated LRU algorithm. */
// ʹ�ý��� LRU �㷨��������������������ʱ��
unsigned long long estimateObjectIdleTime(robj *o) { //����ö���o�Ѿ����û�з�����
    unsigned long long lruclock = LRU_CLOCK();
    if (lruclock >= o->lru) {
        return (lruclock - o->lru) * REDIS_LRU_CLOCK_RESOLUTION;
    } else {//�������һ�㲻�ᷢ��������ʱ֤��redis�м��ı���ʱ���Ѿ�wrap��
        return (lruclock + (REDIS_LRU_CLOCK_MAX - o->lru)) *
                    REDIS_LRU_CLOCK_RESOLUTION;
    }
}

/* This is a helper function for the OBJECT command. We need to lookup keys
 * without any modification of LRU or other parameters.
 *
 * OBJECT ����ĸ��������������ڲ��޸� LRU ʱ�������£����Ի�ȡ key ����
 */
robj *objectCommandLookup(redisClient *c, robj *key) {
    dictEntry *de;

    if ((de = dictFind(c->db->dict,key->ptr)) == NULL) return NULL;
    return (robj*) dictGetVal(de);
}

/*
 * �ڲ��޸� LRU ʱ�������£���ȡ key ��Ӧ�Ķ���
 *
 * ������󲻴��ڣ���ô��ͻ��˷��ͻظ� reply ��
 */
robj *objectCommandLookupOrReply(redisClient *c, robj *key, robj *reply) {
    robj *o = objectCommandLookup(c,key);

    if (!o) addReply(c, reply);
    return o;
}

/*
OBJECT subcommand [arguments [arguments]]

OBJECT ����������ڲ��쿴���� key �� Redis ����


��ͨ�����ڳ���(debugging)�����˽�Ϊ�˽�ʡ�ռ���� key ʹ���������������

����Redis�����������ʱ����Ҳ����ͨ�� OBJECT �����е���Ϣ������ key ���������(eviction policies)��

OBJECT �����ж�������
?OBJECT REFCOUNT <key> ���ظ��� key �����������ֵ�Ĵ�������������Ҫ���ڳ���
?OBJECT ENCODING <key> ���ظ��� key �������ֵ��ʹ�õ��ڲ���ʾ(representation)��
?OBJECT IDLETIME <key> ���ظ��� key �Դ��������Ŀ���ʱ��(idle�� û�б���ȡҲû�б�д��)������Ϊ��λ��


��������Զ��ַ�ʽ���룺
?�ַ������Ա�����Ϊ raw (һ���ַ���)�� int (Ϊ�˽�Լ�ڴ棬Redis �Ὣ�ַ�����ʾ�� 64 λ�з�����������Ϊ���������д��棩��
?�б���Ա�����Ϊ ziplist �� linkedlist �� ziplist ��Ϊ��Լ��С��С���б�ռ�����������ʾ��
?���Ͽ��Ա�����Ϊ intset ���� hashtable �� intset ��ֻ�������ֵ�С���ϵ������ʾ��
?��ϣ����Ա���Ϊ zipmap ���� hashtable �� zipmap ��С��ϣ��������ʾ��
?���򼯺Ͽ��Ա�����Ϊ ziplist ���� skiplist ��ʽ�� ziplist ���ڱ�ʾС�����򼯺ϣ��� skiplist �����ڱ�ʾ�κδ�С�����򼯺ϡ�


����������ʲô�� Redis û�취��ʹ�ý�ʡ�ռ�ı���ʱ(���罫һ��ֻ�� 1 ��Ԫ�صļ�����չΪһ���� 100 ���Ԫ�صļ���)�������������(specially encoded types)���Զ�ת����ͨ������(general type)��
���ð汾��>= 2.2.3ʱ�临�Ӷȣ�O(1)����ֵ��

REFCOUNT �� IDLETIME �������֡�

ENCODING ������Ӧ�ı������͡�


redis> SET game "COD"           # ����һ���ַ���
OK

redis> OBJECT REFCOUNT game     # ֻ��һ������
(integer) 1

redis> OBJECT IDLETIME game     # �ȴ�һ�󡣡���Ȼ��鿴����ʱ��
(integer) 90

redis> GET game                 # ��ȡgame�� �������ڻ�Ծ(active)״̬
"COD"

redis> OBJECT IDLETIME game     # ���ٴ��ڿ���״̬
(integer) 0

redis> OBJECT ENCODING game     # �ַ����ı��뷽ʽ
"raw"

redis> SET big-number 23102930128301091820391092019203810281029831092  # �ǳ��������ֻᱻ����Ϊ�ַ���
OK

redis> OBJECT ENCODING big-number
"raw"

redis> SET small-number 12345  # ���̵�������ᱻ����Ϊ����
OK

redis> OBJECT ENCODING small-number
"int"


*/
/* Object command allows to inspect the internals of an Redis Object.
 * Usage: OBJECT <verb> ... arguments ... */
void objectCommand(redisClient *c) {
    robj *o;

    // ���ض�Ϸ�ĸ������ü���
    if (!strcasecmp(c->argv[1]->ptr,"refcount") && c->argc == 3) {
        if ((o = objectCommandLookupOrReply(c,c->argv[2],shared.nullbulk))
                == NULL) return;
        addReplyLongLong(c,o->refcount);

    // ���ض���ı���
    } else if (!strcasecmp(c->argv[1]->ptr,"encoding") && c->argc == 3) {
        if ((o = objectCommandLookupOrReply(c,c->argv[2],shared.nullbulk))
                == NULL) return;
        addReplyBulkCString(c,strEncoding(o->encoding));
    
    // ���ض���Ŀ���ʱ��
    } else if (!strcasecmp(c->argv[1]->ptr,"idletime") && c->argc == 3) {
        if ((o = objectCommandLookupOrReply(c,c->argv[2],shared.nullbulk))
                == NULL) return;
        addReplyLongLong(c,estimateObjectIdleTime(o)/1000);
    } else {
        addReplyError(c,"Syntax error. Try OBJECT (refcount|encoding|idletime)");
    }
}
