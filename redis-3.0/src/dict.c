/* Hash Tables Implementation.
 *
 * This file implements in memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/time.h>
#include <ctype.h>

#include "dict.h"
#include "zmalloc.h"
#include "redisassert.h"

/* Using dictEnableResize() / dictDisableResize() we make possible to
 * enable/disable resizing of the hash table as needed. This is very important
 * for Redis, as we use copy-on-write and don't want to move too much memory
 * around when there is a child performing saving operations.
 *
 * ͨ�� dictEnableResize() �� dictDisableResize() ����������
 * ��������ֶ����������ֹ��ϣ����� rehash ��
 * ���� Redis ʹ���ӽ��̽��б������ʱ��������Ч������ copy-on-write ���ơ�
 *
 * Note that even when dict_can_resize is set to 0, not all resizes are
 * prevented: a hash table is still allowed to grow if the ratio between
 * the number of elements and the buckets > dict_force_resize_ratio.
 *
 * ��Ҫע����ǣ��������� rehash ���ᱻ dictDisableResize ��ֹ��
 * �����ʹ�ýڵ���������ֵ��С֮��ı��ʣ�
 * �����ֵ�ǿ�� rehash ���� dict_force_resize_ratio ��
 * ��ô rehash ��Ȼ�ᣨǿ�ƣ����С�
 */
// ָʾ�ֵ��Ƿ����� rehash �ı�ʶ
static int dict_can_resize = 1; //dictEnableResize����1   dictDisableResize����0
// ǿ�� rehash �ı��� 
//1���ܵ�Ԫ�ظ��� �� DICTͰ�ĸ����õ�ÿ��Ͱƽ���洢��Ԫ�ظ���(pre_num),��� pre_num > dict_force_resize_ratio,�ͻᴥ��dict ���������dict_force_resize_ratio = 5��

static unsigned int dict_force_resize_ratio = 5;

/* -------------------------- private prototypes ---------------------------- */

static int _dictExpandIfNeeded(dict *ht);
static unsigned long _dictNextPower(unsigned long size);
static int _dictKeyIndex(dict *ht, const void *key);
static int _dictInit(dict *ht, dictType *type, void *privDataPtr);

/* -------------------------- hash functions -------------------------------- */

/* Thomas Wang's 32 bit Mix Function */
unsigned int dictIntHashFunction(unsigned int key)
{
    key += ~(key << 15);
    key ^=  (key >> 10);
    key +=  (key << 3);
    key ^=  (key >> 6);
    key += ~(key << 11);
    key ^=  (key >> 16);
    return key;
}

/* Identity hash function for integer keys */
unsigned int dictIdentityHashFunction(unsigned int key)
{
    return key;
}

static uint32_t dict_hash_function_seed = 5381;

void dictSetHashFunctionSeed(uint32_t seed) {
    dict_hash_function_seed = seed;
}

uint32_t dictGetHashFunctionSeed(void) {
    return dict_hash_function_seed;
}

/* MurmurHash2, by Austin Appleby
 * Note - This code makes a few assumptions about how your machine behaves -
 * 1. We can read a 4-byte value from any address without crashing
 * 2. sizeof(int) == 4
 *
 * And it has a few limitations -
 *
 * 1. It will not work incrementally.
 * 2. It will not produce the same results on little-endian and big-endian
 *    machines.
 */
unsigned int dictGenHashFunction(const void *key, int len) {
    /* 'm' and 'r' are mixing constants generated offline.
     They're not really 'magic', they just happen to work well.  */
    uint32_t seed = dict_hash_function_seed;
    const uint32_t m = 0x5bd1e995;
    const int r = 24;

    /* Initialize the hash to a 'random' value */
    uint32_t h = seed ^ len;

    /* Mix 4 bytes at a time into the hash */
    const unsigned char *data = (const unsigned char *)key;

    while(len >= 4) {
        uint32_t k = *(uint32_t*)data;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len -= 4;
    }

    /* Handle the last few bytes of the input array  */
    switch(len) {
    case 3: h ^= data[2] << 16;
    case 2: h ^= data[1] << 8;
    case 1: h ^= data[0]; h *= m;
    };

    /* Do a few final mixes of the hash to ensure the last few
     * bytes are well-incorporated. */
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return (unsigned int)h;
}

/* And a case insensitive hash function (based on djb hash) */
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len) {
    unsigned int hash = (unsigned int)dict_hash_function_seed;

    while (len--)
        hash = ((hash << 5) + hash) + (tolower(*buf++)); /* hash * 33 + c */
    return hash;
}

/* ----------------------------- API implementation ------------------------- */

/* Reset a hash table already initialized with ht_init().
 * NOTE: This function should only be called by ht_destroy(). */
/*
 * ���ã����ʼ����������ϣ��ĸ�������ֵ
 *
 * p.s. �����Ӣ��ע���Ѿ�����
 *
 * T = O(1)
 */

/* rehash������ù�ϵ���ù���:dictAddRaw->_dictKeyIndex->_dictExpandIfNeeded(��������Ƿ���Ҫ��չhash)->dictExpand 
����hash����:serverCron->tryResizeHashTables->dictResize(��������������Ͱ��)->dictExpand */

static void _dictReset(dictht *ht)
{
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

/* Create a new hash table */
/*
 * ����һ���µ��ֵ�
 *
 * T = O(1)
 */ //typeΪxxxDictType(����keyptrDictType��)
dict *dictCreate(dictType *type,
        void *privDataPtr)
{
    dict *d = zmalloc(sizeof(*d));

    _dictInit(d,type,privDataPtr);

    return d;
}

/* Initialize the hash table */
/*
 * ��ʼ����ϣ��
 *
 * T = O(1)
 */
int _dictInit(dict *d, dictType *type,
        void *privDataPtr)
{
    // ��ʼ��������ϣ��ĸ�������ֵ
    // ����ʱ���������ڴ����ϣ������
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);

    // ���������ض�����
    d->type = type;

    // ����˽������
    d->privdata = privDataPtr;

    // ���ù�ϣ�� rehash ״̬
    d->rehashidx = -1;

    // �����ֵ�İ�ȫ����������
    d->iterators = 0;

    return DICT_OK;
}

/* Resize the table to the minimal size that contains all the elements,
 * but with the invariant of a USED/BUCKETS ratio near to <= 1 */
/*
 * ��С�����ֵ�
 * ���������ýڵ������ֵ��С֮��ı��ʽӽ� 1:1
 *
 * ���� DICT_ERR ��ʾ�ֵ��Ѿ��� rehash ������ dict_can_resize Ϊ�١�
 *
 * �ɹ����������С�� ht[1] �����Կ�ʼ resize ʱ������ DICT_OK��
 *
 * T = O(N)
 */ /* rehash������ù�ϵ���ù���:dictAddRaw->_dictKeyIndex->_dictExpandIfNeeded(��������Ƿ���Ҫ��չhash)->dictExpand 
����hash����:_dictRehashStep  serverCron->tryResizeHashTables->dictResize(��������������Ͱ��)->dictExpand */
int dictResize(dict *d)
{
    int minimal;

    // �����ڹر� rehash �������� rehash ��ʱ�����
    if (!dict_can_resize || dictIsRehashing(d)) return DICT_ERR;

    // �����ñ��ʽӽ� 1��1 ����Ҫ�����ٽڵ�����
    minimal = d->ht[0].used;
    if (minimal < DICT_HT_INITIAL_SIZE)
        minimal = DICT_HT_INITIAL_SIZE;

    // �����ֵ�Ĵ�С
    // T = O(N)
    return dictExpand(d, minimal); //���½�Ͱ����Ͱ�ĸ���Ϊminimal��ӽ���2�ı��������ڵ���Ϊminimal,��˽ڵ���/Ͱ���϶�С�ڵ���1
}

/* Expand or create the hash table */
/*
 * ����һ���µĹ�ϣ���������ֵ�������ѡ����������һ�����������У�
 *
 * 1) ����ֵ�� 0 �Ź�ϣ��Ϊ�գ���ô���¹�ϣ������Ϊ 0 �Ź�ϣ��
 * 2) ����ֵ�� 0 �Ź�ϣ��ǿգ���ô���¹�ϣ������Ϊ 1 �Ź�ϣ��
 *    �����ֵ�� rehash ��ʶ��ʹ�ó�����Կ�ʼ���ֵ���� rehash
 *
 * size ���������󣬻��� rehash �Ѿ��ڽ���ʱ������ DICT_ERR ��
 *
 * �ɹ����� 0 �Ź�ϣ������ 1 �Ź�ϣ��ʱ������ DICT_OK ��
 *
 * T = O(N)
 */ /* rehash������ù�ϵ���ù���:dictAddRaw->_dictKeyIndex->_dictExpandIfNeeded(��������Ƿ���Ҫ��չhash)->dictExpand 
����hash����:serverCron->tryResizeHashTables->dictResize(��������������Ͱ��)->dictExpand */
int dictExpand(dict *d, unsigned long size)
{
    // �¹�ϣ��
    dictht n; /* the new hash table */

    // ���� size �����������ϣ��Ĵ�С
    // T = O(1)
    unsigned long realsize = _dictNextPower(size);

    /* the size is invalid if it is smaller than the number of
     * elements already inside the hash table */
    // �������ֵ����� rehash ʱ����
    // size ��ֵҲ����С�� 0 �Ź�ϣ��ĵ�ǰ��ʹ�ýڵ�
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    /* Allocate the new hash table and initialize all pointers to NULL */
    // Ϊ��ϣ�����ռ䣬��������ָ��ָ�� NULL
    n.size = realsize;
    n.sizemask = realsize-1;
    // T = O(N)
    n.table = zcalloc(realsize*sizeof(dictEntry*));
    n.used = 0;

    /* Is this the first initialization? If so it's not really a rehashing
     * we just set the first hash table so that it can accept keys. */
    // ��� 0 �Ź�ϣ��Ϊ�գ���ô����һ�γ�ʼ����
    // �����¹�ϣ���� 0 �Ź�ϣ���ָ�룬Ȼ���ֵ�Ϳ��Կ�ʼ�����ֵ���ˡ�
    if (d->ht[0].table == NULL) {
        d->ht[0] = n;
        return DICT_OK;
    }

    /* Prepare a second hash table for incremental rehashing */
    // ��� 0 �Ź�ϣ��ǿգ���ô����һ�� rehash ��
    // �����¹�ϣ������Ϊ 1 �Ź�ϣ��
    // �����ֵ�� rehash ��ʶ�򿪣��ó�����Կ�ʼ���ֵ���� rehash
    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;

    /* ˳��һ�ᣬ����Ĵ�������ع���������ʽ��
    
    if (d->ht[0].table == NULL) {
        // ��ʼ��
        d->ht[0] = n;
    } else {
        // rehash 
        d->ht[1] = n;
        d->rehashidx = 0;
    }

    return DICT_OK;

    */
}

/* Performs N steps of incremental rehashing. Returns 1 if there are still
 * keys to move from the old to the new hash table, otherwise 0 is returned.
 *
 * ִ�� N ������ʽ rehash ��
 *
 * ���� 1 ��ʾ���м���Ҫ�� 0 �Ź�ϣ���ƶ��� 1 �Ź�ϣ��
 * ���� 0 ���ʾ���м����Ѿ�Ǩ����ϡ�
 *
 * Note that a rehashing step consists in moving a bucket (that may have more
 * than one key as we use chaining) from the old to the new hash table.
 *
 * ע�⣬ÿ�� rehash ������һ����ϣ��������Ͱ����Ϊ��λ�ģ�
 * һ��Ͱ����ܻ��ж���ڵ㣬
 * �� rehash ��Ͱ������нڵ㶼�ᱻ�ƶ����¹�ϣ��
 *
 * T = O(N)
 */
int dictRehash(dict *d, int n) { 
//rehash��Ϊ����rehash��dictRehashMillisecondsҲ���Ƕ�ʱȥrehash�����е�KV��ʱ�䲻����һֱ��Ǩ�Ƶ�ht[1],
//��һ�����ɿͻ��˷��ʵ�ʱ�򱻶�����rehash����_dictRehashStep

    // ֻ������ rehash ������ʱִ��
    if (!dictIsRehashing(d)) return 0;

    // ���� N ��Ǩ��
    // T = O(N)
    while(n--) {
        dictEntry *de, *nextde;

        /* Check if we already rehashed the whole table... */
        // ��� 0 �Ź�ϣ��Ϊ�գ���ô��ʾ rehash ִ�����
        // T = O(1)
        if (d->ht[0].used == 0) {
            // �ͷ� 0 �Ź�ϣ��
            zfree(d->ht[0].table);
            // ��ԭ���� 1 �Ź�ϣ������Ϊ�µ� 0 �Ź�ϣ��
            d->ht[0] = d->ht[1];
            // ���þɵ� 1 �Ź�ϣ��
            _dictReset(&d->ht[1]);
            // �ر� rehash ��ʶ
            d->rehashidx = -1;
            // ���� 0 ��������߱�ʾ rehash �Ѿ����
            return 0;
        }

        /* Note that rehashidx can't overflow as we are sure there are more
         * elements because ht[0].used != 0 */
        // ȷ�� rehashidx û��Խ��
        assert(d->ht[0].size > (unsigned)d->rehashidx);

        // �Թ�������Ϊ�յ��������ҵ���һ���ǿ�����
        while(d->ht[0].table[d->rehashidx] == NULL) d->rehashidx++;

        // ָ��������������ͷ�ڵ�
        de = d->ht[0].table[d->rehashidx];
        /* Move all the keys in this bucket from the old to the new hash HT */
        // �������е����нڵ�Ǩ�Ƶ��¹�ϣ��
        // T = O(1)
        while(de) { /* ��hdelһ��ɾ����ʮ����ʱ������պô���dictRehash,������ܻ��������п��ܸ�table�����ϵ��������ܴ� */
            unsigned int h;

            // �����¸��ڵ��ָ��
            nextde = de->next;

            /* Get the index in the new hash table */
            // �����¹�ϣ��Ĺ�ϣֵ���Լ��ڵ���������λ��
            h = dictHashKey(d, de->key) & d->ht[1].sizemask;

            // ����ڵ㵽�¹�ϣ��
            de->next = d->ht[1].table[h];
            d->ht[1].table[h] = de;

            // ���¼�����
            d->ht[0].used--;
            d->ht[1].used++;

            // ���������¸��ڵ�
            de = nextde;
        }
        // ����Ǩ����Ĺ�ϣ��������ָ����Ϊ��
        d->ht[0].table[d->rehashidx] = NULL;
        // ���� rehash ����
        d->rehashidx++;
    }

    return 1;
}

/*
 * �����Ժ���Ϊ��λ�� UNIX ʱ���
 *
 * T = O(1)
 */
long long timeInMilliseconds(void) {
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
}

/* Rehash for an amount of time between ms milliseconds and ms+1 milliseconds */
/*
 * �ڸ����������ڣ��� 100 ��Ϊ��λ�����ֵ���� rehash ��
 *
 * T = O(N)
 */
    //rehash��Ϊ����rehash��dictRehashMillisecondsҲ���Ƕ�ʱȥrehash�����е�KV��ʱ�䲻����һֱ��Ǩ�Ƶ�ht[1],
    //��һ�����ɿͻ��˷��ʵ�ʱ�򱻶�����rehash����_dictRehashStep
int dictRehashMilliseconds(dict *d, int ms) {
    // ��¼��ʼʱ��
    long long start = timeInMilliseconds();
    int rehashes = 0;

    while(dictRehash(d,100)) {
        rehashes += 100;
        // ���ʱ���ѹ�������
        if (timeInMilliseconds()-start > ms) break;
    }

    return rehashes;
}

/* This function performs just a step of rehashing, and only if there are
 * no safe iterators bound to our hash table. When we have iterators in the
 * middle of a rehashing we can't mess with the two hash tables otherwise
 * some element can be missed or duplicated.
 *
 * ���ֵ䲻���ڰ�ȫ������������£����ֵ���е��� rehash ��
 *
 * �ֵ��а�ȫ������������²��ܽ��� rehash ��
 * ��Ϊ���ֲ�ͬ�ĵ������޸Ĳ������ܻ�Ū���ֵ䡣
 *
 * This function is called by common lookup or update operations in the
 * dictionary so that the hash table automatically migrates from H1 to H2
 * while it is actively used. 
 *
 * ������������ͨ�õĲ��ҡ����²������ã�
 * ���������ֵ��ڱ�ʹ�õ�ͬʱ���� rehash ��
 *
 * T = O(1)
 */
    //rehash��Ϊ����rehash��dictRehashMillisecondsҲ���Ƕ�ʱȥrehash�����е�KV��ʱ�䲻����һֱ��Ǩ�Ƶ�ht[1],
    //��һ�����ɿͻ��˷��ʵ�ʱ�򱻶�����rehash����_dictRehashStep
static void _dictRehashStep(dict *d) {
    if (d->iterators == 0) dictRehash(d,1);
}

/* Add an element to the target hash table */
/*
 * ���Խ�������ֵ����ӵ��ֵ���
 *
 * ֻ�и����� key ���������ֵ�ʱ����Ӳ����Ż�ɹ�
 *
 * ��ӳɹ����� DICT_OK ��ʧ�ܷ��� DICT_ERR
 *
 * � T = O(N) ��ƽ̲ O(1) 
 */ // �������ļ�ֵ����ӵ��ֵ����档      
int dictAdd(dict *d, void *key, void *val) //��key��val��Ӧ��obj��ӵ�һ��entry�� 
{
    // ������Ӽ����ֵ䣬�����ذ�������������¹�ϣ�ڵ�
    // T = O(N)
    dictEntry *entry = dictAddRaw(d,key);

    // ���Ѵ��ڣ����ʧ��
    if (!entry) return DICT_ERR;

    // �������ڣ����ýڵ��ֵ
    // T = O(1)
    dictSetVal(d, entry, val);

    // ��ӳɹ�
    return DICT_OK;
}

/* Low level add. This function adds the entry but instead of setting
 * a value returns the dictEntry structure to the user, that will make
 * sure to fill the value field as he wishes.
 *
 * This function is also directly exposed to user API to be called
 * mainly in order to store non-pointers inside the hash value, example:
 *
 * entry = dictAddRaw(dict,mykey);
 * if (entry != NULL) dictSetSignedIntegerVal(entry,1000);
 *
 * Return values:
 *
 * If key already exists NULL is returned.
 * If key was added, the hash entry is returned to be manipulated by the caller.
 */
/*
 * ���Խ������뵽�ֵ���
 *
 * ������Ѿ����ֵ���ڣ���ô���� NULL
 *
 * ����������ڣ���ô���򴴽��µĹ�ϣ�ڵ㣬
 * ���ڵ�ͼ������������뵽�ֵ䣬Ȼ�󷵻ؽڵ㱾��
 *
 * T = O(N)
 */ /* rehash������ù�ϵ���ù���:dictAddRaw->_dictKeyIndex->_dictExpandIfNeeded(��������Ƿ���Ҫ��չhash)->dictExpand 
����hash����:serverCron->tryResizeHashTables->dictResize(��������������Ͱ��)->dictExpand */
dictEntry *dictAddRaw(dict *d, void *key)
{
    int index;
    dictEntry *entry;
    dictht *ht;

    // �����������Ļ������е��� rehash
    // T = O(1)
    if (dictIsRehashing(d)) _dictRehashStep(d);

    /* Get the index of the new element, or -1 if
     * the element already exists. */
    // ������ڹ�ϣ���е�����ֵ
    // ���ֵΪ -1 ����ô��ʾ���Ѿ�����
    // T = O(N)
    if ((index = _dictKeyIndex(d, key)) == -1)
        return NULL;

    // T = O(1)
    /* Allocate the memory and store the new entry */
    // ����ֵ����� rehash ����ô���¼���ӵ� 1 �Ź�ϣ��
    // ���򣬽��¼���ӵ� 0 �Ź�ϣ��
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    // Ϊ�½ڵ����ռ�
    entry = zmalloc(sizeof(*entry));
    // ���½ڵ���뵽�����ͷ
    entry->next = ht->table[index];
    ht->table[index] = entry;
    // ���¹�ϣ����ʹ�ýڵ�����
    ht->used++;

    /* Set the hash entry fields. */
    // �����½ڵ�ļ�
    // T = O(1)
    dictSetKey(d, entry, key);

    return entry;
}

/* Add an element, discarding the old if the key already exists.
 *
 * �������ļ�ֵ����ӵ��ֵ��У�������Ѿ����ڣ���ôɾ�����еļ�ֵ�ԡ�
 *
 * Return 1 if the key was added from scratch, 0 if there was already an
 * element with such key and dictReplace() just performed a value update
 * operation. 
 *
 * �����ֵ��Ϊȫ����ӣ���ô���� 1 ��
 * �����ֵ����ͨ����ԭ�еļ�ֵ�Ը��µ����ģ���ô���� 0 ��
 *
 * T = O(N)
 */ //�������ļ�ֵ����ӵ��ֵ����棬 ������Ѿ��������ֵ䣬��ô����ֵȡ��ԭ�е�ֵ�� 
int dictReplace(dict *d, void *key, void *val)
{
    dictEntry *entry, auxentry;

    /* Try to add the element. If the key
     * does not exists dictAdd will suceed. */
    // ����ֱ�ӽ���ֵ����ӵ��ֵ�
    // ����� key �����ڵĻ�����ӻ�ɹ�
    // T = O(N)
    if (dictAdd(d, key, val) == DICT_OK)
        return 1;

    /* It already exists, get the entry */
    // ���е����˵���� key �Ѿ����ڣ���ô�ҳ�������� key �Ľڵ�
    // T = O(1)
    entry = dictFind(d, key);
    /* Set the new value and free the old one. Note that it is important
     * to do that in this order, as the value may just be exactly the same
     * as the previous one. In this context, think to reference counting,
     * you want to increment (set), and then decrement (free), and not the
     * reverse. */
    // �ȱ���ԭ�е�ֵ��ָ��
    auxentry = *entry;
    // Ȼ�������µ�ֵ
    // T = O(1)
    dictSetVal(d, entry, val);
    // Ȼ���ͷž�ֵ
    // T = O(1)
    dictFreeVal(d, &auxentry);

    return 0;
}

/* dictReplaceRaw() is simply a version of dictAddRaw() that always
 * returns the hash entry of the specified key, even if the key already
 * exists and can't be added (in that case the entry of the already
 * existing key is returned.)
 *
 * See dictAddRaw() for more information. */
/*
 * dictAddRaw() ���ݸ��� key �ͷŴ��ڣ�ִ�����¶�����
 *
 * 1) key �Ѿ����ڣ����ذ����� key ���ֵ�ڵ�
 * 2) key �����ڣ���ô�� key ��ӵ��ֵ�
 *
 * ���۷������ϵ���һ�������
 * dictAddRaw() �����Ƿ��ذ������� key ���ֵ�ڵ㡣
 *
 * T = O(N)
 */
dictEntry *dictReplaceRaw(dict *d, void *key) {
    
    // ʹ�� key ���ֵ��в��ҽڵ�
    // T = O(1)
    dictEntry *entry = dictFind(d,key);

    // ����ڵ��ҵ���ֱ�ӷ��ؽڵ㣬������Ӳ�����һ���½ڵ�
    // T = O(N)
    return entry ? entry : dictAddRaw(d,key);
}

/* Search and remove an element */
/*
 * ���Ҳ�ɾ�������������Ľڵ�
 *
 * ���� nofree �����Ƿ���ü���ֵ���ͷź���
 * 0 ��ʾ���ã�1 ��ʾ������
 *
 * �ҵ����ɹ�ɾ������ DICT_OK ��û�ҵ��򷵻� DICT_ERR
 *
 * T = O(1)
 */
static int dictGenericDelete(dict *d, const void *key, int nofree)
{
    unsigned int h, idx;
    dictEntry *he, *prevHe;
    int table;

    // �ֵ䣨�Ĺ�ϣ��Ϊ��
    if (d->ht[0].size == 0) return DICT_ERR; /* d->ht[0].table is NULL */

    // ���е��� rehash ��T = O(1)
    if (dictIsRehashing(d)) _dictRehashStep(d);

    // �����ϣֵ
    h = dictHashKey(d, key);

    // ������ϣ��
    // T = O(1)
    for (table = 0; table <= 1; table++) {

        // ��������ֵ 
        idx = h & d->ht[table].sizemask;
        // ָ��������ϵ�����
        he = d->ht[table].table[idx];
        prevHe = NULL;
        // ���������ϵ����нڵ�
        // T = O(1)
        while(he) {
        
            if (dictCompareKeys(d, key, he->key)) {
                // ����Ŀ��ڵ�

                /* Unlink the element from the list */
                // ��������ɾ��
                if (prevHe)
                    prevHe->next = he->next;
                else
                    d->ht[table].table[idx] = he->next;

                // �ͷŵ��ü���ֵ���ͷź�����
                if (!nofree) {
                    dictFreeKey(d, he);
                    dictFreeVal(d, he);
                }
                
                // �ͷŽڵ㱾��
                zfree(he);

                // ������ʹ�ýڵ�����
                d->ht[table].used--;

                // �������ҵ��ź�
                return DICT_OK;
            }

            prevHe = he;
            he = he->next;
        }

        // ���ִ�е����˵���� 0 �Ź�ϣ�����Ҳ���������
        // ��ô�����ֵ��Ƿ����ڽ��� rehash ������Ҫ��Ҫ���� 1 �Ź�ϣ��
        if (!dictIsRehashing(d)) break;
    }

    // û�ҵ�
    return DICT_ERR; /* not found */
}

/*
 * ���ֵ���ɾ�������������Ľڵ�
 * 
 * ���ҵ��ü�ֵ���ͷź�����ɾ����ֵ
 *
 * �ҵ����ɹ�ɾ������ DICT_OK ��û�ҵ��򷵻� DICT_ERR
 * T = O(1)
 */
int dictDelete(dict *ht, const void *key) {
    return dictGenericDelete(ht,key,0);
}

/*
 * ���ֵ���ɾ�������������Ľڵ�
 * 
 * �������ü�ֵ���ͷź�����ɾ����ֵ
 *
 * �ҵ����ɹ�ɾ������ DICT_OK ��û�ҵ��򷵻� DICT_ERR
 * T = O(1)
 */
int dictDeleteNoFree(dict *ht, const void *key) {
    return dictGenericDelete(ht,key,1);
}

/* Destroy an entire dictionary */
/*
 * ɾ����ϣ���ϵ����нڵ㣬�����ù�ϣ��ĸ�������
 *
 * T = O(N)
 */
int _dictClear(dict *d, dictht *ht, void(callback)(void *)) {
    unsigned long i;

    /* Free all the elements */
    // ����������ϣ��
    // T = O(N)
    for (i = 0; i < ht->size && ht->used > 0; i++) {
        dictEntry *he, *nextHe;

        if (callback && (i & 65535) == 0) callback(d->privdata); //���������������һ��callback����

        // ����������
        if ((he = ht->table[i]) == NULL) continue;

        // ������������
        // T = O(1)
        while(he) {
            nextHe = he->next;
            // ɾ����
            dictFreeKey(d, he);
            // ɾ��ֵ
            dictFreeVal(d, he);
            // �ͷŽڵ�
            zfree(he);

            // ������ʹ�ýڵ����
            ht->used--;

            // �����¸��ڵ�
            he = nextHe;
        }
    }

    /* Free the table and the allocated cache structure */
    // �ͷŹ�ϣ��ṹ
    zfree(ht->table);

    /* Re-initialize the table */
    // ���ù�ϣ������
    _dictReset(ht);

    return DICT_OK; /* never fails */
}

/* Clear & Release the hash table */
/*
 * ɾ�����ͷ������ֵ�
 *
 * T = O(N)
 */
void dictRelease(dict *d)
{
    // ɾ�������������ϣ��
    _dictClear(d,&d->ht[0],NULL);
    _dictClear(d,&d->ht[1],NULL);
    // �ͷŽڵ�ṹ
    zfree(d);
}

/*
 * �����ֵ��а����� key �Ľڵ�
 *
 * �ҵ����ؽڵ㣬�Ҳ������� NULL
 *
 * T = O(1)
 */
dictEntry *dictFind(dict *d, const void *key)
{
    dictEntry *he;
    unsigned int h, idx, table;

    // �ֵ䣨�Ĺ�ϣ��Ϊ��
    if (d->ht[0].size == 0) return NULL; /* We don't have a table at all */

    // �����������Ļ������е��� rehash
    if (dictIsRehashing(d)) _dictRehashStep(d);

    // ������Ĺ�ϣֵ
    h = dictHashKey(d, key);
    // ���ֵ�Ĺ�ϣ���в��������
    // T = O(1)
    for (table = 0; table <= 1; table++) {

        // ��������ֵ
        idx = h & d->ht[table].sizemask;

        // �������������ϵ���������нڵ㣬���� key
        he = d->ht[table].table[idx];
        // T = O(1)
        while(he) {

            if (dictCompareKeys(d, key, he->key))
                return he;

            he = he->next;
        }

        // ������������ 0 �Ź�ϣ����Ȼû�ҵ�ָ���ļ��Ľڵ�
        // ��ô��������ֵ��Ƿ��ڽ��� rehash ��
        // Ȼ��ž�����ֱ�ӷ��� NULL �����Ǽ������� 1 �Ź�ϣ��
        if (!dictIsRehashing(d)) return NULL;
    }

    // ���е�����ʱ��˵��������ϣ��û�ҵ�
    return NULL;
}

/*
 * ��ȡ�����������Ľڵ��ֵ
 *
 * ����ڵ㲻Ϊ�գ����ؽڵ��ֵ
 * ���򷵻� NULL
 *
 * T = O(1)
 */
void *dictFetchValue(dict *d, const void *key) {
    dictEntry *he;

    // T = O(1)
    he = dictFind(d,key);

    return he ? dictGetVal(he) : NULL;
}

/* A fingerprint is a 64 bit number that represents the state of the dictionary
 * at a given time, it's just a few dict properties xored together.
 * When an unsafe iterator is initialized, we get the dict fingerprint, and check
 * the fingerprint again when the iterator is released.
 * If the two fingerprints are different it means that the user of the iterator
 * performed forbidden operations against the dictionary while iterating. */
long long dictFingerprint(dict *d) {
    long long integers[6], hash = 0;
    int j;

    integers[0] = (long) d->ht[0].table;
    integers[1] = d->ht[0].size;
    integers[2] = d->ht[0].used;
    integers[3] = (long) d->ht[1].table;
    integers[4] = d->ht[1].size;
    integers[5] = d->ht[1].used;

    /* We hash N integers by summing every successive integer with the integer
     * hashing of the previous sum. Basically:
     *
     * Result = hash(hash(hash(int1)+int2)+int3) ...
     *
     * This way the same set of integers in a different order will (likely) hash
     * to a different number. */
    for (j = 0; j < 6; j++) {
        hash += integers[j];
        /* For the hashing step we use Tomas Wang's 64 bit integer hash. */
        hash = (~hash) + (hash << 21); // hash = (hash << 21) - hash - 1;
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8); // hash * 265
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4); // hash * 21
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
    }
    return hash;
}

/*
 * ���������ظ����ֵ�Ĳ���ȫ������
 *
 * T = O(1)
 */
dictIterator *dictGetIterator(dict *d)
{
    dictIterator *iter = zmalloc(sizeof(*iter));

    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;

    return iter;
}

/*
 * ���������ظ����ڵ�İ�ȫ������
 *
 * T = O(1)
 */
dictIterator *dictGetSafeIterator(dict *d) {
    dictIterator *i = dictGetIterator(d);

    // ���ð�ȫ��������ʶ
    i->safe = 1;

    return i;
}

/*
 * ���ص�����ָ��ĵ�ǰ�ڵ�
 *
 * �ֵ�������ʱ������ NULL
 *
 * T = O(1)
 */ //����Ͱ�е����нڵ㣬��һ�α����ú�����ʱ��ֱ�ӻ�ȡ����Ͱ��Ԫ�أ�����ʹ��nextEntry
dictEntry *dictNext(dictIterator *iter)
{
    while (1) {

        // �������ѭ�������ֿ��ܣ�
        // 1) ���ǵ�������һ������
        // 2) ��ǰ���������еĽڵ��Ѿ������꣨NULL Ϊ����ı�β��
        if (iter->entry == NULL) {

            // ָ�򱻵����Ĺ�ϣ��
            dictht *ht = &iter->d->ht[iter->table];

            // ���ε���ʱִ��
            if (iter->index == -1 && iter->table == 0) {
                // ����ǰ�ȫ����������ô���°�ȫ������������
                if (iter->safe)
                    iter->d->iterators++;
                // ����ǲ���ȫ����������ô����ָ��
                else
                    iter->fingerprint = dictFingerprint(iter->d);
            }
            // ��������
            iter->index++; //Ĭ��ֵΪ-1���ԼӺ�պ�Ϊ0������һ��Ͱ

            // ����������ĵ�ǰ�������ڵ�ǰ�������Ĺ�ϣ��Ĵ�С
            // ��ô˵�������ϣ���Ѿ��������
            if (iter->index >= (signed) ht->size) {
                // ������� rehash �Ļ�����ô˵�� 1 �Ź�ϣ��Ҳ����ʹ����
                // ��ô������ 1 �Ź�ϣ����е���
                if (dictIsRehashing(iter->d) && iter->table == 0) {
                    iter->table++;
                    iter->index = 0;
                    ht = &iter->d->ht[1];
                // ���û�� rehash ����ô˵�������Ѿ����
                } else {
                    break;
                }
            }

            // ������е����˵�������ϣ��δ������
            // ���½ڵ�ָ�룬ָ���¸���������ı�ͷ�ڵ�
            iter->entry = ht->table[iter->index];
        } else {
            // ִ�е����˵���������ڵ���ĳ������
            // ���ڵ�ָ��ָ��������¸��ڵ�
            iter->entry = iter->nextEntry;
        }

        // �����ǰ�ڵ㲻Ϊ�գ���ôҲ��¼�¸ýڵ���¸��ڵ�
        // ��Ϊ��ȫ�������п��ܻὫ���������صĵ�ǰ�ڵ�ɾ��
        if (iter->entry) {
            /* We need to save the 'next' here, the iterator user
             * may delete the entry we are returning. */
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }

    // �������
    return NULL;
}

/*
 * �ͷŸ����ֵ������
 *
 * T = O(1)
 */
void dictReleaseIterator(dictIterator *iter)
{

    if (!(iter->index == -1 && iter->table == 0)) {
        // �ͷŰ�ȫ������ʱ����ȫ��������������һ
        if (iter->safe)
            iter->d->iterators--;
        // �ͷŲ���ȫ������ʱ����ָ֤���Ƿ��б仯
        else
            assert(iter->fingerprint == dictFingerprint(iter->d));
    }
    zfree(iter);
}

/* Return a random entry from the hash table. Useful to
 * implement randomized algorithms */
/*
 * ��������ֵ�������һ���ڵ㡣
 *
 * ������ʵ��������㷨��
 *
 * ����ֵ�Ϊ�գ����� NULL ��
 *
 * T = O(N)
*/
dictEntry *dictGetRandomKey(dict *d)
{
    dictEntry *he, *orighe;
    unsigned int h;
    int listlen, listele;

    // �ֵ�Ϊ��
    if (dictSize(d) == 0) return NULL;

    // ���е��� rehash
    if (dictIsRehashing(d)) _dictRehashStep(d);

    // ������� rehash ����ô�� 1 �Ź�ϣ��Ҳ��Ϊ������ҵ�Ŀ��
    if (dictIsRehashing(d)) {
        // T = O(N)
        do {
            h = random() % (d->ht[0].size+d->ht[1].size);
            he = (h >= d->ht[0].size) ? d->ht[1].table[h - d->ht[0].size] :
                                      d->ht[0].table[h];
        } while(he == NULL);
    // ����ֻ�� 0 �Ź�ϣ���в��ҽڵ�
    } else {
        // T = O(N)
        do {
            h = random() & d->ht[0].sizemask;
            he = d->ht[0].table[h];
        } while(he == NULL);
    }

    /* Now we found a non empty bucket, but it is a linked
     * list and we need to get a random element from the list.
     * The only sane way to do so is counting the elements and
     * select a random index. */
    // Ŀǰ he �Ѿ�ָ��һ���ǿյĽڵ�����
    // ���򽫴���������������һ���ڵ�
    listlen = 0;
    orighe = he;
    // ����ڵ�����, T = O(1)
    while(he) { //��������Ͱ���������ͱ�����KV��
        he = he->next;
        listlen++;
    }
    // ȡģ���ó�����ڵ������
    listele = random() % listlen; //�Ӿ���Ͱ�������ȡ��һ��KV�ڵ�
    he = orighe;
    // ���������ҽڵ�
    // T = O(1)
    while(listele--) he = he->next;

    // ��������ڵ�
    return he;
}

/* This is a version of dictGetRandomKey() that is modified in order to
 * return multiple entries by jumping at a random place of the hash table
 * and scanning linearly for entries.
 *
 * Returned pointers to hash table entries are stored into 'des' that
 * points to an array of dictEntry pointers. The array must have room for
 * at least 'count' elements, that is the argument we pass to the function
 * to tell how many random elements we need.
 *
 * The function returns the number of items stored into 'des', that may
 * be less than 'count' if the hash table has less than 'count' elements
 * inside.
 *
 * Note that this function is not suitable when you need a good distribution
 * of the returned items, but only when you need to "sample" a given number
 * of continuous elements to run some kind of algorithm or to produce
 * statistics. However the function is much faster than dictGetRandomKey()
 * at producing N elements, and the elements are guaranteed to be non
 * repeating. */
int dictGetRandomKeys(dict *d, dictEntry **des, int count) {
    int j; /* internal hash table id, 0 or 1. */
    int stored = 0;

    if (dictSize(d) < count) count = dictSize(d);
    while(stored < count) {
        for (j = 0; j < 2; j++) {
            /* Pick a random point inside the hash table 0 or 1. */
            unsigned int i = random() & d->ht[j].sizemask;
            int size = d->ht[j].size;

            /* Make sure to visit every bucket by iterating 'size' times. */
            while(size--) {
                dictEntry *he = d->ht[j].table[i];
                while (he) {
                    /* Collect all the elements of the buckets found non
                     * empty while iterating. */
                    *des = he;
                    des++;
                    he = he->next;
                    stored++;
                    if (stored == count) return stored;
                }
                i = (i+1) & d->ht[j].sizemask;
            }
            /* If there is only one table and we iterated it all, we should
             * already have 'count' elements. Assert this condition. */
            assert(dictIsRehashing(d) != 0);
        }
    }
    return stored; /* Never reached. */
}

/* Function to reverse bits. Algorithm from:
 * http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel */
static unsigned long rev(unsigned long v) {
    unsigned long s = 8 * sizeof(v); // bit size; must be power of 2
    unsigned long mask = ~0;
    while ((s >>= 1) > 0) {
        mask ^= (mask << s);
        v = ((v >> s) & mask) | ((v << s) & ~mask);
    }
    return v;
}

/* dictScan() is used to iterate over the elements of a dictionary.
 *
 * dictScan() �������ڵ��������ֵ��е�Ԫ�ء�
 *
 * Iterating works in the following way:
 *
 * ���������·�ʽִ�У�
 *
 * 1) Initially you call the function using a cursor (v) value of 0.
 *    һ��ʼ����ʹ�� 0 ��Ϊ�α������ú�����
 * 2) The function performs one step of the iteration, and returns the
 *    new cursor value that you must use in the next call.
 *    ����ִ��һ������������
 *    ������һ���´ε���ʱʹ�õ����αꡣ
 * 3) When the returned cursor is 0, the iteration is complete.
 *    ���������ص��α�Ϊ 0 ʱ��������ɡ�
 *
 * The function guarantees that all the elements that are present in the
 * dictionary from the start to the end of the iteration are returned.
 * However it is possible that some element is returned multiple time.
 *
 * ������֤���ڵ����ӿ�ʼ�������ڼ䣬һֱ�������ֵ��Ԫ�ؿ϶��ᱻ��������
 * ��һ��Ԫ�ؿ��ܻᱻ���ض�Ρ�
 *
 * For every element returned, the callback 'fn' passed as argument is
 * called, with 'privdata' as first argument and the dictionar entry
 * 'de' as second argument.
 *
 * ÿ��һ��Ԫ�ر�����ʱ���ص����� fn �ͻᱻִ�У�
 * fn �����ĵ�һ�������� privdata �����ڶ������������ֵ�ڵ� de ��
 *
 * HOW IT WORKS.
 * ����ԭ��
 *
 * The algorithm used in the iteration was designed by Pieter Noordhuis.
 * The main idea is to increment a cursor starting from the higher order
 * bits, that is, instead of incrementing the cursor normally, the bits
 * of the cursor are reversed, then the cursor is incremented, and finally
 * the bits are reversed again.
 *
 * ������ʹ�õ��㷨���� Pieter Noordhuis ��Ƶģ�
 * �㷨����Ҫ˼·���ڶ����Ƹ�λ�϶��α���мӷ�����
 * Ҳ����˵�����ǰ������İ취�����α���мӷ����㣬
 * �������Ƚ��α�Ķ�����λ��ת��reverse��������
 * Ȼ��Է�ת���ֵ���мӷ����㣬
 * ����ٴζԼӷ�����֮��Ľ�����з�ת��
 *
 * This strategy is needed because the hash table may be resized from one
 * call to the other call of the same iteration.
 *
 * ��һ�����Ǳ�Ҫ�ģ���Ϊ��һ�������ĵ��������У�
 * ��ϣ��Ĵ�С�п��������ε���֮�䷢���ı䡣
 *
 * dict.c hash tables are always power of two in size, and they
 * use chaining, so the position of an element in a given table is given
 * always by computing the bitwise AND between Hash(key) and SIZE-1
 * (where SIZE-1 is always the mask that is equivalent to taking the rest
 *  of the division between the Hash of the key and SIZE).
 *
 * ��ϣ��Ĵ�С���� 2 ��ĳ���η������ҹ�ϣ��ʹ�������������ͻ��
 * ���һ������Ԫ����һ���������λ���ܿ���ͨ�� Hash(key) & SIZE-1
 * ��ʽ������ó���
 * ���� SIZE-1 �ǹ�ϣ����������ֵ��
 * ����������ֵ���ǹ�ϣ��� mask �����룩��
 *
 * For example if the current hash table size is 16, the mask is
 * (in binary) 1111. The position of a key in the hash table will be always
 * the last four bits of the hash output, and so forth.
 *
 * �ٸ����ӣ������ǰ��ϣ��Ĵ�СΪ 16 ��
 * ��ô����������Ƕ�����ֵ 1111 ��
 * �����ϣ�������λ�ö�����ʹ�ù�ϣֵ������ĸ�������λ����¼��
 *
 * WHAT HAPPENS IF THE TABLE CHANGES IN SIZE?
 * �����ϣ��Ĵ�С�ı�����ô�죿
 *
 * If the hash table grows, elements can go anyway in one multiple of
 * the old bucket: for example let's say that we already iterated with
 * a 4 bit cursor 1100, since the mask is 1111 (hash table size = 16).
 *
 * ���Թ�ϣ�������չʱ��Ԫ�ؿ��ܻ��һ�����ƶ�����һ���ۣ�
 * �ٸ����ӣ��������Ǹպõ����� 4 λ�α� 1100 ��
 * ����ϣ��� mask Ϊ 1111 ����ϣ��Ĵ�СΪ 16 ����
 *
 * If the hash table will be resized to 64 elements, and the new mask will
 * be 111111, the new buckets that you obtain substituting in ??1100
 * either 0 or 1, can be targeted only by keys that we already visited
 * when scanning the bucket 1100 in the smaller hash table.
 *
 * �����ʱ��ϣ����С��Ϊ 64 ����ô��ϣ��� mask ����Ϊ 111111 ��
 *
 * By iterating the higher bits first, because of the inverted counter, the
 * cursor does not need to restart if the table size gets bigger, and will
 * just continue iterating with cursors that don't have '1100' at the end,
 * nor any other combination of final 4 bits already explored.
 *
 * Similarly when the table size shrinks over time, for example going from
 * 16 to 8, If a combination of the lower three bits (the mask for size 8
 * is 111) was already completely explored, it will not be visited again
 * as we are sure that, we tried for example, both 0111 and 1111 (all the
 * variations of the higher bit) so we don't need to test it again.
 *
 * WAIT... YOU HAVE *TWO* TABLES DURING REHASHING!
 * �ȵȡ������� rehash ��ʱ����ǻ����������ϣ��İ���
 *
 * Yes, this is true, but we always iterate the smaller one of the tables,
 * testing also all the expansions of the current cursor into the larger
 * table. So for example if the current cursor is 101 and we also have a
 * larger table of size 16, we also test (0)101 and (1)101 inside the larger
 * table. This reduces the problem back to having only one table, where
 * the larger one, if exists, is just an expansion of the smaller one.
 *
 * LIMITATIONS
 * ����
 *
 * This iterator is completely stateless, and this is a huge advantage,
 * including no additional memory used.
 * �������������ȫ��״̬�ģ�����һ���޴�����ƣ�
 * ��Ϊ���������ڲ�ʹ���κζ����ڴ������½��С�
 *
 * The disadvantages resulting from this design are:
 * �����Ƶ�ȱ�����ڣ�
 *
 * 1) It is possible that we return duplicated elements. However this is usually
 *    easy to deal with in the application level.
 *    �������ܻ᷵���ظ���Ԫ�أ��������������Ժ�������Ӧ�ò�����
 * 2) The iterator must return multiple elements per call, as it needs to always
 *    return all the keys chained in a given bucket, and all the expansions, so
 *    we are sure we don't miss keys moving.
 *    Ϊ�˲�����κ�Ԫ�أ�
 *    ��������Ҫ���ظ���Ͱ�ϵ����м���
 *    �Լ���Ϊ��չ��ϣ��������������±�
 *    ���Ե�����������һ�ε����з��ض��Ԫ�ء�
 * 3) The reverse cursor is somewhat hard to understand at first, but this
 *    comment is supposed to help.
 *    ���α���з�ת��reverse����ԭ�������ȥ�Ƚ�������⣬
 *    �����Ķ����ע��Ӧ�û�����������
 */ //���Բο�http://chenzhenianqing.cn/articles/1101.html
unsigned long dictScan(dict *d,
                       unsigned long v,
                       dictScanFunction *fn,
                       void *privdata)
{
    dictht *t0, *t1;
    const dictEntry *de;
    unsigned long m0, m1;

    // �������ֵ�
    if (dictSize(d) == 0) return 0;

    // ����ֻ��һ����ϣ����ֵ�
    if (!dictIsRehashing(d)) {

        // ָ���ϣ��
        t0 = &(d->ht[0]);

        // ��¼ mask
        m0 = t0->sizemask;

        /* Emit entries at cursor */
        // ָ���ϣͰ
        de = t0->table[v & m0];
        // ����Ͱ�е����нڵ�
        while (de) {
            fn(privdata, de);
            de = de->next;
        }

    // ������������ϣ����ֵ�
    } else {

        // ָ��������ϣ��
        t0 = &d->ht[0];
        t1 = &d->ht[1];

        /* Make sure t0 is the smaller and t1 is the bigger table */
        // ȷ�� t0 �� t1 ҪС
        if (t0->size > t1->size) {
            t0 = &d->ht[1];
            t1 = &d->ht[0];
        }

        // ��¼����
        m0 = t0->sizemask;
        m1 = t1->sizemask;

        /* Emit entries at cursor */
        // ָ��Ͱ��������Ͱ�е����нڵ�
        de = t0->table[v & m0];
        while (de) {
            fn(privdata, de);
            de = de->next;
        }

        /* Iterate over indices in larger table that are the expansion
         * of the index pointed to by the cursor in the smaller table */
        // Iterate over indices in larger table             // ��������е�Ͱ
        // that are the expansion of the index pointed to   // ��ЩͰ�������� expansion ��ָ��
        // by the cursor in the smaller table               //
        do {
            /* Emit entries at cursor */
            // ָ��Ͱ��������Ͱ�е����нڵ�
            de = t1->table[v & m1];
            while (de) {
                fn(privdata, de);
                de = de->next;
            }

            /* Increment bits not covered by the smaller mask */
            v = (((v | m0) + 1) & ~m0) | (v & m0);

            /* Continue while bits covered by mask difference is non-zero */
        } while (v & (m0 ^ m1));
    }

    /* Set unmasked bits so incrementing the reversed cursor
     * operates on the masked bits of the smaller table */
    v |= ~m0;

    /* Increment the reverse cursor */
    v = rev(v);
    v++;
    v = rev(v);

    return v;
}

/* ------------------------- private functions ------------------------------ */

/* Expand the hash table if needed */
/*
 * ������Ҫ����ʼ���ֵ䣨�Ĺ�ϣ�������߶��ֵ䣨�����й�ϣ��������չ
 *
 * T = O(N)
 */ /* rehash������ù�ϵ���ù���:dictAddRaw->_dictKeyIndex->_dictExpandIfNeeded(��������Ƿ���Ҫ��չhash)->dictExpand 
����hash����:serverCron->tryResizeHashTables->dictResize(��������������Ͱ��)->dictExpand */
static int _dictExpandIfNeeded(dict *d)
{
    /* Incremental rehashing already in progress. Return. */
    // ����ʽ rehash �Ѿ��ڽ����ˣ�ֱ�ӷ���
    if (dictIsRehashing(d)) return DICT_OK;

    /* If the hash table is empty expand it to the initial size. */
    // ����ֵ䣨�� 0 �Ź�ϣ��Ϊ�գ���ô���������س�ʼ����С�� 0 �Ź�ϣ��
    // T = O(1)
    if (d->ht[0].size == 0) return dictExpand(d, DICT_HT_INITIAL_SIZE);

    /* If we reached the 1:1 ratio, and we are allowed to resize the hash
     * table (global setting) or we should avoid it but the ratio between
     * elements/buckets is over the "safe" threshold, we resize doubling
     * the number of buckets. */
    // һ����������֮һΪ��ʱ�����ֵ������չ
    // 1���ֵ���ʹ�ýڵ������ֵ��С֮��ı��ʽӽ� 1��1
    //    ���� dict_can_resize Ϊ��
    // 2����ʹ�ýڵ������ֵ��С֮��ı��ʳ��� dict_force_resize_ratio
    if (d->ht[0].used >= d->ht[0].size &&
        (dict_can_resize ||
         d->ht[0].used/d->ht[0].size > dict_force_resize_ratio))
    {
        // �¹�ϣ��Ĵ�С������Ŀǰ��ʹ�ýڵ���������
        // T = O(N)
        return dictExpand(d, d->ht[0].used*2);
    }

    return DICT_OK;
}

/* Our hash table capability is a power of two */
/*
 * �����һ�����ڵ��� size �� 2 �� N �η���������ϣ���ֵ
 *
 * T = O(1)
 */
static unsigned long _dictNextPower(unsigned long size)
{
    unsigned long i = DICT_HT_INITIAL_SIZE;

    if (size >= LONG_MAX) return LONG_MAX;
    while(1) {
        if (i >= size)
            return i;
        i *= 2;
    }
}

/* Returns the index of a free slot that can be populated with
 * a hash entry for the given 'key'.
 * If the key already exists, -1 is returned.
 *
 * ���ؿ��Խ� key ���뵽��ϣ�������λ��
 * ��� key �Ѿ������ڹ�ϣ����ô���� -1
 *
 * Note that if we are in the process of rehashing the hash table, the
 * index is always returned in the context of the second (new) hash table. 
 *
 * ע�⣬����ֵ����ڽ��� rehash ����ô���Ƿ��� 1 �Ź�ϣ���������
 * ��Ϊ���ֵ���� rehash ʱ���½ڵ����ǲ��뵽 1 �Ź�ϣ��
 *
 * T = O(N)
 */ /* rehash������ù�ϵ���ù���:dictAddRaw->_dictKeyIndex->_dictExpandIfNeeded(��������Ƿ���Ҫ��չhash)->dictExpand 
����hash����:serverCron->tryResizeHashTables->dictResize(��������������Ͱ��)->dictExpand */
static int _dictKeyIndex(dict *d, const void *key) //����key���㺯����keyΪ��������keyֵ
{
    unsigned int h, idx, table;
    dictEntry *he;

    /* Expand the hash table if needed */
    // ���� rehash
    // T = O(N)
    if (_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;

    /* Compute the key hash value */
    // ���� key �Ĺ�ϣֵ
    h = dictHashKey(d, key);
    // T = O(1)
    for (table = 0; table <= 1; table++) {

        // ��������ֵ
        idx = h & d->ht[table].sizemask;

        /* Search if this slot does not already contain the given key */
        // ���� key �Ƿ����
        // T = O(1)
        he = d->ht[table].table[idx];
        while(he) {
            if (dictCompareKeys(d, key, he->key))
                return -1;
            he = he->next;
        }

        // ������е�����ʱ��˵�� 0 �Ź�ϣ�������нڵ㶼������ key
        // �����ʱ rehahs ���ڽ��У���ô������ 1 �Ź�ϣ����� rehash
        if (!dictIsRehashing(d)) break;
    }

    // ��������ֵ
    return idx;
}

/*
 * ����ֵ��ϵ����й�ϣ��ڵ㣬�������ֵ�����
 *
 * T = O(N)
 */
void dictEmpty(dict *d, void(callback)(void*)) {

    // ɾ��������ϣ���ϵ����нڵ�
    // T = O(N)
    _dictClear(d,&d->ht[0],callback);
    _dictClear(d,&d->ht[1],callback);
    // �������� 
    d->rehashidx = -1;
    d->iterators = 0;
}

/*
 * �����Զ� rehash
 *
 * T = O(1)
 */
void dictEnableResize(void) {
    dict_can_resize = 1;
}

/*
 * �ر��Զ� rehash
 *
 * T = O(1)
 */
void dictDisableResize(void) {
    dict_can_resize = 0;
}

#if 0

/* The following is code that we don't use for Redis currently, but that is part
of the library. */

/* ----------------------- Debugging ------------------------*/

#define DICT_STATS_VECTLEN 50
static void _dictPrintStatsHt(dictht *ht) {
    unsigned long i, slots = 0, chainlen, maxchainlen = 0;
    unsigned long totchainlen = 0;
    unsigned long clvector[DICT_STATS_VECTLEN];

    if (ht->used == 0) {
        printf("No stats available for empty dictionaries\n");
        return;
    }

    for (i = 0; i < DICT_STATS_VECTLEN; i++) clvector[i] = 0;
    for (i = 0; i < ht->size; i++) {
        dictEntry *he;

        if (ht->table[i] == NULL) {
            clvector[0]++;
            continue;
        }
        slots++;
        /* For each hash entry on this slot... */
        chainlen = 0;
        he = ht->table[i];
        while(he) {
            chainlen++;
            he = he->next;
        }
        clvector[(chainlen < DICT_STATS_VECTLEN) ? chainlen : (DICT_STATS_VECTLEN-1)]++;
        if (chainlen > maxchainlen) maxchainlen = chainlen;
        totchainlen += chainlen;
    }
    printf("Hash table stats:\n");
    printf(" table size: %ld\n", ht->size);
    printf(" number of elements: %ld\n", ht->used);
    printf(" different slots: %ld\n", slots);
    printf(" max chain length: %ld\n", maxchainlen);
    printf(" avg chain length (counted): %.02f\n", (float)totchainlen/slots);
    printf(" avg chain length (computed): %.02f\n", (float)ht->used/slots);
    printf(" Chain length distribution:\n");
    for (i = 0; i < DICT_STATS_VECTLEN-1; i++) {
        if (clvector[i] == 0) continue;
        printf("   %s%ld: %ld (%.02f%%)\n",(i == DICT_STATS_VECTLEN-1)?">= ":"", i, clvector[i], ((float)clvector[i]/ht->size)*100);
    }
}

void dictPrintStats(dict *d) {
    _dictPrintStatsHt(&d->ht[0]);
    if (dictIsRehashing(d)) {
        printf("-- Rehashing into ht[1]:\n");
        _dictPrintStatsHt(&d->ht[1]);
    }
}

/* ----------------------- StringCopy Hash Table Type ------------------------*/

static unsigned int _dictStringCopyHTHashFunction(const void *key)
{
    return dictGenHashFunction(key, strlen(key));
}

static void *_dictStringDup(void *privdata, const void *key)
{
    int len = strlen(key);
    char *copy = zmalloc(len+1);
    DICT_NOTUSED(privdata);

    memcpy(copy, key, len);
    copy[len] = '\0';
    return copy;
}

static int _dictStringCopyHTKeyCompare(void *privdata, const void *key1,
        const void *key2)
{
    DICT_NOTUSED(privdata);

    return strcmp(key1, key2) == 0;
}

static void _dictStringDestructor(void *privdata, void *key)
{
    DICT_NOTUSED(privdata);

    zfree(key);
}

dictType dictTypeHeapStringCopyKey = {
    _dictStringCopyHTHashFunction, /* hash function */
    _dictStringDup,                /* key dup */
    NULL,                          /* val dup */
    _dictStringCopyHTKeyCompare,   /* key compare */
    _dictStringDestructor,         /* key destructor */
    NULL                           /* val destructor */
};

/* This is like StringCopy but does not auto-duplicate the key.
 * It's used for intepreter's shared strings. */
dictType dictTypeHeapStrings = {
    _dictStringCopyHTHashFunction, /* hash function */
    NULL,                          /* key dup */
    NULL,                          /* val dup */
    _dictStringCopyHTKeyCompare,   /* key compare */
    _dictStringDestructor,         /* key destructor */
    NULL                           /* val destructor */
};

/* This is like StringCopy but also automatically handle dynamic
 * allocated C strings as values. */
dictType dictTypeHeapStringCopyKeyValue = {
    _dictStringCopyHTHashFunction, /* hash function */
    _dictStringDup,                /* key dup */
    _dictStringDup,                /* val dup */
    _dictStringCopyHTKeyCompare,   /* key compare */
    _dictStringDestructor,         /* key destructor */
    _dictStringDestructor,         /* val destructor */
};
#endif
