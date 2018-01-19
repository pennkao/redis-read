/*
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
#include "cluster.h"

#include <signal.h>
#include <ctype.h>

void slotToKeyAdd(robj *key);
void slotToKeyDel(robj *key);
void slotToKeyFlush(void);

/*-----------------------------------------------------------------------------
 * C-level DB API
 *----------------------------------------------------------------------------*/

/*
 * �����ݿ� db ��ȡ���� key ��ֵ������
 *
 * ��� key ��ֵ���ڣ���ô���ظ�ֵ�����򣬷��� NULL ��
 */ //��ȡkey��Ӧ��val
robj *lookupKey(redisDb *db, robj *key) {

    // ���Ҽ��ռ�
    dictEntry *de = dictFind(db->dict,key->ptr);

    // �ڵ����
    if (de) {
        

        // ȡ��ֵ
        robj *val = dictGetVal(de);

        /* Update the access time for the ageing algorithm.
         * Don't do it if we have a saving child, as this will trigger
         * a copy on write madness. */
        // ����ʱ����Ϣ��ֻ�ڲ������ӽ���ʱִ�У���ֹ�ƻ� copy-on-write ���ƣ�
        if (server.rdb_child_pid == -1 && server.aof_child_pid == -1)
            val->lru = LRU_CLOCK();

        // ����ֵ
        return val;
    } else {

        // �ڵ㲻����

        return NULL;
    }
}

/*
 * Ϊִ�ж�ȡ������ȡ���� key �����ݿ� db �е�ֵ��
 *
 * �������Ƿ�ɹ��ҵ�ֵ�����·�����������/��������Ϣ��
 *
 * �ҵ�ʱ����ֵ����û�ҵ����� NULL ��
 */
robj *lookupKeyRead(redisDb *db, robj *key) {
    robj *val;

    // ��� key �ͷ��Ѿ�����
    expireIfNeeded(db,key);

    // �����ݿ���ȡ������ֵ
    val = lookupKey(db,key);

    // ��������/��������Ϣ
    if (val == NULL)
        server.stat_keyspace_misses++;
    else
        server.stat_keyspace_hits++;

    // ����ֵ
    return val;
}

/*
 * Ϊִ��д�������ȡ���� key �����ݿ� db �е�ֵ��
 *
 * �� lookupKeyRead ��ͬ���������������·�����������/��������Ϣ��
 *
 * �ҵ�ʱ����ֵ����û�ҵ����� NULL ��
 */
robj *lookupKeyWrite(redisDb *db, robj *key) {

    // ɾ�����ڼ�
    expireIfNeeded(db,key);

    // ���Ҳ����� key ��ֵ����
    return lookupKey(db,key);
}

/*
 * Ϊִ�ж�ȡ�����������ݿ��в��ҷ��� key ��ֵ��
 *
 * ��� key ���ڣ���ô���� key ��ֵ����
 *
 * ��� key �����ڣ���ô��ͻ��˷��� reply �����е���Ϣ�������� NULL ��
 */
robj *lookupKeyReadOrReply(redisClient *c, robj *key, robj *reply) {

    // ����
    robj *o = lookupKeyRead(c->db, key);

    // �����Ƿ�����Ϣ
    if (!o) addReply(c,reply);

    return o;
}

/*
 * Ϊִ��д������������ݿ��в��ҷ��� key ��ֵ��
 *
 * ��� key ���ڣ���ô���� key ��ֵ����
 *
 * ��� key �����ڣ���ô��ͻ��˷��� reply �����е���Ϣ�������� NULL ��
 */

robj *lookupKeyWriteOrReply(redisClient *c, robj *key, robj *reply) {

    robj *o = lookupKeyWrite(c->db, key);

    if (!o) addReply(c,reply);

    return o;
}

/* Add the key to the DB. It's up to the caller to increment the reference
 * counter of the value if needed.
 *
 * ���Խ���ֵ�� key �� val ��ӵ����ݿ��С�
 *
 * �����߸���� key �� val �����ü����������ӡ�
 *
 * The program is aborted if the key already exists. 
 *
 * �����ڼ��Ѿ�����ʱ��ֹͣ��
 */
void dbAdd(redisDb *db, robj *key, robj *val) {

    // ���Ƽ���
    sds copy = sdsdup(key->ptr);

    // ������Ӽ�ֵ��
    int retval = dictAdd(db->dict, copy, val);

    // ������Ѿ����ڣ���ôֹͣ
    redisAssertWithInfo(NULL,key,retval == REDIS_OK);

    // ��������˼�Ⱥģʽ����ô�������浽������
    if (server.cluster_enabled) slotToKeyAdd(key);
 }

/* Overwrite an existing key with a new value. Incrementing the reference
 * count of the new value is up to the caller.
 *
 * Ϊ�Ѵ��ڵļ�����һ����ֵ��
 *
 * �����߸������ֵ val �����ü����������ӡ�
 *
 * This function does not modify the expire time of the existing key.
 *
 * ������������޸ļ��Ĺ���ʱ�䡣
 *
 * The program is aborted if the key was not already present. 
 *
 * ����������ڣ���ô����ֹͣ��
 */
void dbOverwrite(redisDb *db, robj *key, robj *val) {
    dictEntry *de = dictFind(db->dict,key->ptr);
    
    // �ڵ������ڣ�������ֹ
    redisAssertWithInfo(NULL,key,de != NULL);

    // ��д��ֵ
    dictReplace(db->dict, key->ptr, val);
}

/* High level Set operation. This function can be used in order to set
 * a key, whatever it was existing or not, to a new object.
 *
 * �߲�ε� SET ����������
 *
 * ������������ڲ��ܼ� key �Ƿ���ڵ�����£������� val ����������
 *
 * 1) The ref count of the value object is incremented.
 *    ֵ��������ü����ᱻ����
 *
 * 2) clients WATCHing for the destination key notified.
 *    ���Ӽ� key �Ŀͻ��˻��յ����Ѿ����޸ĵ�֪ͨ
 *
 * 3) The expire time of the key is reset (the key is made persistent). 
 *    ���Ĺ���ʱ��ᱻ�Ƴ�������Ϊ�־õģ�
 */
void setKey(redisDb *db, robj *key, robj *val) {

    // ��ӻ�д���ݿ��еļ�ֵ��
    if (lookupKeyWrite(db,key) == NULL) {
        dbAdd(db,key,val);
    } else {
        dbOverwrite(db,key,val);
    }

    incrRefCount(val);

    // �Ƴ����Ĺ���ʱ��
    removeExpire(db,key);

    // ���ͼ��޸�֪ͨ
    signalModifiedKey(db,key);
}

/*
 * ���� key �Ƿ���������ݿ��У����ڷ��� 1 �������ڷ��� 0 ��
 */
int dbExists(redisDb *db, robj *key) {
    return dictFind(db->dict,key->ptr) != NULL;
}

/* Return a random key, in form of a Redis object.
 * If there are no keys, NULL is returned.
 *
 * ��������ݿ���ȡ��һ�����������ַ�������ķ�ʽ�����������
 *
 * ������ݿ�Ϊ�գ���ô���� NULL ��
 *
 * The function makes sure to return keys not already expired. 
 *
 * ���������֤�����صļ�����δ���ڵġ�
 */
robj *dbRandomKey(redisDb *db) {
    dictEntry *de;

    while(1) {
        sds key;
        robj *keyobj;

        // �Ӽ��ռ������ȡ��һ�����ڵ�
        de = dictGetRandomKey(db->dict);

        // ���ݿ�Ϊ��
        if (de == NULL) return NULL;

        // ȡ����
        key = dictGetKey(de);
        // Ϊ������һ���ַ������󣬶����ֵΪ��������
        keyobj = createStringObject(key,sdslen(key));
        // �����Ƿ���й���ʱ��
        if (dictFind(db->expires,key)) {
            // ������Ѿ����ڣ���ô����ɾ��������������¸���
            if (expireIfNeeded(db,keyobj)) {
                decrRefCount(keyobj);
                continue; /* search for another key. This expired. */
            }
        }

        // ���ر�������ļ��������֣�
        return keyobj;
    }
}

/* Delete a key, value, and associated expiration entry if any, from the DB 
 *
 * �����ݿ���ɾ�������ļ�������ֵ���Լ����Ĺ���ʱ�䡣
 *
 * ɾ���ɹ����� 1 ����Ϊ�������ڶ�����ɾ��ʧ��ʱ������ 0 ��
 */
int dbDelete(redisDb *db, robj *key) {

    /* Deleting an entry from the expires dict will not free the sds of
     * the key, because it is shared with the main dictionary. */
    // ɾ�����Ĺ���ʱ��
    if (dictSize(db->expires) > 0) dictDelete(db->expires,key->ptr);

    // ɾ����ֵ��
    if (dictDelete(db->dict,key->ptr) == DICT_OK) {
        // ��������˼�Ⱥģʽ����ô�Ӳ���ɾ�������ļ�
        if (server.cluster_enabled) slotToKeyDel(key);
        return 1;
    } else {
        // ��������
        return 0;
    }
}

/* Prepare the string object stored at 'key' to be modified destructively
 * to implement commands like SETBIT or APPEND.
 *
 * An object is usually ready to be modified unless one of the two conditions
 * are true:
 *
 * 1) The object 'o' is shared (refcount > 1), we don't want to affect
 *    other users.
 * 2) The object encoding is not "RAW".
 *
 * If the object is found in one of the above conditions (or both) by the
 * function, an unshared / not-encoded copy of the string object is stored
 * at 'key' in the specified 'db'. Otherwise the object 'o' itself is
 * returned.
 *
 * USAGE:
 *
 * The object 'o' is what the caller already obtained by looking up 'key'
 * in 'db', the usage pattern looks like this:
 *
 * o = lookupKeyWrite(db,key);
 * if (checkType(c,o,REDIS_STRING)) return;
 * o = dbUnshareStringValue(db,key,o);
 *
 * At this point the caller is ready to modify the object, for example
 * using an sdscat() call to append some data, or anything else.
 */
robj *dbUnshareStringValue(redisDb *db, robj *key, robj *o) {
    redisAssert(o->type == REDIS_STRING);
    if (o->refcount != 1 || o->encoding != REDIS_ENCODING_RAW) {
        robj *decoded = getDecodedObject(o);
        o = createRawStringObject(decoded->ptr, sdslen(decoded->ptr));
        decrRefCount(decoded);
        dbOverwrite(db,key,o);
    }
    return o;
}

/*
 * ��շ��������������ݡ�
 */ //���ڴ��е�����dbid(select id�е�id)���ݿ�key-value��������Ϊ-1����ʾ��˵�����ݿ����
long long emptyDb(void(callback)(void*)) { //ע�����KV���ܴ󣬿��ܻ���������
    int j;
    long long removed = 0;

    // ����������ݿ�
    for (j = 0; j < server.dbnum; j++) {

        // ��¼��ɾ����������
        removed += dictSize(server.db[j].dict);

        // ɾ�����м�ֵ��
        dictEmpty(server.db[j].dict,callback);
        // ɾ�����м��Ĺ���ʱ��
        dictEmpty(server.db[j].expires,callback);
    }

    // ��������˼�Ⱥģʽ����ô��Ҫ�Ƴ��ۼ�¼
    if (server.cluster_enabled) slotToKeyFlush();

    // ���ؼ�������
    return removed;
}

/*
 * ���ͻ��˵�Ŀ�����ݿ��л�Ϊ id ��ָ�������ݿ�
 */
int selectDb(redisClient *c, int id) {

    // ȷ�� id ����ȷ��Χ��
    if (id < 0 || id >= server.dbnum)
        return REDIS_ERR;

    // �л����ݿ⣨����ָ�룩
    c->db = &server.db[id];

    return REDIS_OK;
}

/*-----------------------------------------------------------------------------
 * Hooks for key space changes.
 *
 * ���ռ�Ķ��Ĺ��ӡ�
 *
 * Every time a key in the database is modified the function
 * signalModifiedKey() is called.
 *
 * ÿ�����ݿ��еļ����Ķ�ʱ�� signalModifiedKey() �������ᱻ���á�
 *
 * Every time a DB is flushed the function signalFlushDb() is called.
 *
 * ÿ��һ�����ݿⱻ���ʱ�� signalFlushDb() ���ᱻ���á�
 *----------------------------------------------------------------------------*/

void signalModifiedKey(redisDb *db, robj *key) {
    touchWatchedKey(db,key);
}

void signalFlushedDb(int dbid) {
    touchWatchedKeysOnFlush(dbid);
}

/*-----------------------------------------------------------------------------
 * Type agnostic commands operating on the key space
 *
 * �������޹ص����ݿ������
 *----------------------------------------------------------------------------*/

/*
 * ��տͻ���ָ�������ݿ�
 */
void flushdbCommand(redisClient *c) {

    server.dirty += dictSize(c->db->dict);

    // ����֪ͨ
    signalFlushedDb(c->db->id);

    // ���ָ�����ݿ��е� dict �� expires �ֵ�
    dictEmpty(c->db->dict,NULL);
    dictEmpty(c->db->expires,NULL);

    // ��������˼�Ⱥģʽ����ô��Ҫ�Ƴ��ۼ�¼
    if (server.cluster_enabled) slotToKeyFlush();

    addReply(c,shared.ok);
}

/*
 * ��շ������е��������ݿ�
 */
void flushallCommand(redisClient *c) {

    // ����֪ͨ
    signalFlushedDb(-1);

    // ����������ݿ�
    server.dirty += emptyDb(NULL);
    addReply(c,shared.ok);

    // ������ڱ����µ� RDB ����ôȡ���������
    if (server.rdb_child_pid != -1) {
        kill(server.rdb_child_pid,SIGUSR1);
        rdbRemoveTempFile(server.rdb_child_pid);
    }

    // ���� RDB �ļ�
    if (server.saveparamslen > 0) {
        /* Normally rdbSave() will reset dirty, but we don't want this here
         * as otherwise FLUSHALL will not be replicated nor put into the AOF. */
        // rdbSave() ����շ������� dirty ����
        // ��Ϊ��ȷ�� FLUSHALL ����ᱻ����������
        // ������Ҫ���沢�� rdbSave() ����֮��ԭ�������� dirty ����
        int saved_dirty = server.dirty;

        rdbSave(server.rdb_filename);

        server.dirty = saved_dirty;
    }

    server.dirty++;
}

/*
DEL

DEL key [key ...]

ɾ��������һ������ key ��

�����ڵ� key �ᱻ���ԡ�
���ð汾��>= 1.0.0ʱ�临�Ӷȣ�

O(N)�� N Ϊ��ɾ���� key ��������


ɾ�������ַ������͵� key ��ʱ�临�Ӷ�ΪO(1)��

ɾ�������б����ϡ����򼯺ϻ��ϣ�����͵� key ��ʱ�临�Ӷ�ΪO(M)�� M Ϊ�������ݽṹ�ڵ�Ԫ��������
����ֵ����ɾ�� key ��������

#  ɾ������ key

redis> SET name huangz
OK

redis> DEL name
(integer) 1


# ɾ��һ�������ڵ� key

redis> EXISTS phone
(integer) 0

redis> DEL phone # ʧ�ܣ�û�� key ��ɾ��
(integer) 0


# ͬʱɾ����� key

redis> SET name "redis"
OK

redis> SET type "key-value store"
OK

redis> SET website "redis.com"
OK

redis> DEL name type website
(integer) 3


*/
void delCommand(redisClient *c) {
    int deleted = 0, j;

    // �������������
    for (j = 1; j < c->argc; j++) {

        // ��ɾ�����ڵļ�
        expireIfNeeded(c->db,c->argv[j]);

        // ����ɾ����
        if (dbDelete(c->db,c->argv[j])) {

            // ɾ�����ɹ�������֪ͨ

            signalModifiedKey(c->db,c->argv[j]);
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,
                "del",c->argv[j],c->db->id);

            server.dirty++;

            // �ɹ�ɾ�������� deleted ��������ֵ
            deleted++;
        }
    }

    // ���ر�ɾ����������
    addReplyLongLong(c,deleted);
}

/*
EXISTS key

������ key �Ƿ���ڡ�
���ð汾��>= 1.0.0ʱ�临�Ӷȣ�O(1)����ֵ���� key ���ڣ����� 1 �����򷵻� 0 ��

redis> SET db "redis"
OK

redis> EXISTS db
(integer) 1

redis> DEL db
(integer) 1

redis> EXISTS db
(integer) 0

*/
void existsCommand(redisClient *c) {

    // �����Ƿ��Ѿ����ڣ�����ѹ��ڵĻ�����ô����ɾ��
    // ����Ա����ѹ��ڵļ�������Ϊ����
    expireIfNeeded(c->db,c->argv[1]);

    // �����ݿ��в���
    if (dbExists(c->db,c->argv[1])) {
        addReply(c, shared.cone);
    } else {
        addReply(c, shared.czero);
    }
}

void selectCommand(redisClient *c) {
    long id;

    // ���Ϸ������ݿ����
    if (getLongFromObjectOrReply(c, c->argv[1], &id,
        "invalid DB index") != REDIS_OK)
        return;

    if (server.cluster_enabled && id != 0) {
        addReplyError(c,"SELECT is not allowed in cluster mode");
        return;
    }

    // �л����ݿ�
    if (selectDb(c,id) == REDIS_ERR) {
        addReplyError(c,"invalid DB index");
    } else {
        addReply(c,shared.ok);
    }
}

/*
RANDOMKEY

�ӵ�ǰ���ݿ����������(��ɾ��)һ�� key ��
���ð汾��>= 1.0.0ʱ�临�Ӷȣ�O(1)����ֵ��

�����ݿⲻΪ��ʱ������һ�� key ��

�����ݿ�Ϊ��ʱ������ nil ��


# ���ݿⲻΪ��

redis> MSET fruit "apple" drink "beer" food "cookies"   # ���ö�� key
OK

redis> RANDOMKEY
"fruit"

redis> RANDOMKEY
"food"

redis> KEYS *    # �鿴���ݿ�������key��֤�� RANDOMKEY ����ɾ�� key
1) "food"
2) "drink"
3) "fruit"


# ���ݿ�Ϊ��

redis> FLUSHDB  # ɾ����ǰ���ݿ����� key
OK

redis> RANDOMKEY
(nil)


*/
void randomkeyCommand(redisClient *c) {
    robj *key;

    // ������ؼ�
    if ((key = dbRandomKey(c->db)) == NULL) {
        addReply(c,shared.nullbulk);
        return;
    }

    addReplyBulk(c,key);
    decrRefCount(key);
}

/*
KEYS pattern

�������з��ϸ���ģʽ pattern �� key ��


KEYS * ƥ�����ݿ������� key ��

KEYS h?llo ƥ�� hello �� hallo �� hxllo �ȡ�

KEYS h*llo ƥ�� hllo �� heeeeello �ȡ�

KEYS h[ae]llo ƥ�� hello �� hallo ������ƥ�� hillo ��

��������� \ ����



KEYS ���ٶȷǳ��죬����һ��������ݿ���ʹ������Ȼ��������������⣬�������Ҫ��һ�����ݼ��в����ض��� key ������û����� Redis �ļ��Ͻṹ(set)�����档
 ���ð汾��>= 1.0.0ʱ�临�Ӷȣ�O(N)�� N Ϊ���ݿ��� key ������������ֵ�����ϸ���ģʽ�� key �б�

redis> MSET one 1 two 2 three 3 four 4  # һ������ 4 �� key
OK

redis> KEYS *o*
1) "four"
2) "two"
3) "one"

redis> KEYS t??
1) "two"

redis> KEYS t[w]*
1) "two"

redis> KEYS *  # ƥ�����ݿ������� key
1) "four"
2) "three"
3) "two"
4) "one"


*/
void keysCommand(redisClient *c) {
    dictIterator *di;
    dictEntry *de;

    // ģʽ
    sds pattern = c->argv[1]->ptr;

    int plen = sdslen(pattern), allkeys;
    unsigned long numkeys = 0;
    void *replylen = addDeferredMultiBulkLength(c);

    // �����������ݿ⣬���أ����֣���ģʽƥ��ļ�
    di = dictGetSafeIterator(c->db->dict);
    allkeys = (pattern[0] == '*' && pattern[1] == '\0');
    while((de = dictNext(di)) != NULL) {
        sds key = dictGetKey(de);
        robj *keyobj;

        // ��������ģʽ���бȶ�
        if (allkeys || stringmatchlen(pattern,plen,key,sdslen(key),0)) {

            // ����һ����������ֵ��ַ�������
            keyobj = createStringObject(key,sdslen(key));

            // ɾ���ѹ��ڼ�
            if (expireIfNeeded(c->db,keyobj) == 0) {
                addReplyBulk(c,keyobj);
                numkeys++;
            }

            decrRefCount(keyobj);
        }
    }
    dictReleaseIterator(di);

    setDeferredMultiBulkLength(c,replylen,numkeys);
}

/* This callback is used by scanGenericCommand in order to collect elements
 * returned by the dictionary iterator into a list. */
void scanCallback(void *privdata, const dictEntry *de) {
    void **pd = (void**) privdata;
    list *keys = pd[0];
    robj *o = pd[1];
    robj *key, *val = NULL;

    if (o == NULL) {
        sds sdskey = dictGetKey(de);
        key = createStringObject(sdskey, sdslen(sdskey));
    } else if (o->type == REDIS_SET) {
        key = dictGetKey(de);
        incrRefCount(key);
    } else if (o->type == REDIS_HASH) {
        key = dictGetKey(de);
        incrRefCount(key);
        val = dictGetVal(de);
        incrRefCount(val);
    } else if (o->type == REDIS_ZSET) {
        key = dictGetKey(de);
        incrRefCount(key);
        val = createStringObjectFromLongDouble(*(double*)dictGetVal(de));
    } else {
        redisPanic("Type not handled in SCAN callback.");
    }

    listAddNodeTail(keys, key);
    if (val) listAddNodeTail(keys, val);
}

/* Try to parse a SCAN cursor stored at object 'o':
 * if the cursor is valid, store it as unsigned integer into *cursor and
 * returns REDIS_OK. Otherwise return REDIS_ERR and send an error to the
 * client. */ /* ��ȡscan������α� */
int parseScanCursorOrReply(redisClient *c, robj *o, unsigned long *cursor) { /**/
    char *eptr;

    /* Use strtoul() because we need an *unsigned* long, so
     * getLongLongFromObject() does not cover the whole cursor space. */
    errno = 0;
    *cursor = strtoul(o->ptr, &eptr, 10);
    if (isspace(((char*)o->ptr)[0]) || eptr[0] != '\0' || errno == ERANGE)
    {
        addReplyError(c, "invalid cursor");
        return REDIS_ERR;
    }
    return REDIS_OK;
}

/* This command implements SCAN, HSCAN and SSCAN commands.
 *
 * ���� SCAN �� HSCAN �� SSCAN �����ʵ�ֺ�����
 *
 * If object 'o' is passed, then it must be a Hash or Set object, otherwise
 * if 'o' is NULL the command will operate on the dictionary associated with
 * the current database.
 *
 * ��������˶��� o ����ô��������һ����ϣ������߼��϶���
 * ��� o Ϊ NULL �Ļ���������ʹ�õ�ǰ���ݿ���Ϊ��������
 *
 * When 'o' is not NULL the function assumes that the first argument in
 * the client arguments vector is a key so it skips it before iterating
 * in order to parse options.
 *
 * ������� o ��Ϊ NULL ����ô˵������һ�������󣬺�����������Щ������
 * �Ը���������ѡ����з�����parse����
 *
 * In the case of a Hash object the function returns both the field and value
 * of every element on the Hash. 
 *
 * ������������ǹ�ϣ������ô�������ص��Ǽ�ֵ�ԡ�
 */

/*
Redis��SCAN���������������������ƣ��޷��ṩ�ر�׼��scan������������һ����can �� t guarantee �� just do my best����ʵ�֣���ȱ�����£�

�ŵ㣺
    �ṩ���ռ�ı���������֧���α꣬���Ӷ�O(1), �������һ��ֻ��ҪO(N)��
    �ṩ���ģʽƥ�䣻
    ֧��һ�η��ص������������ã��������Ǹ�hints����ʱ�򷵻صĻ�ࣻ
    ��״̬������״ֻ̬��Ҫ�ͻ�����Ҫά��һ���αꣻ
ȱ�㣺
    �޷��ṩ�����Ŀ��ձ�����Ҳ�����м�����������޸ģ�������Щ�漰�Ķ������ݱ���������
    ÿ�η��ص�����������һ�������������ڲ�ʵ�֣�
    ���ص����ݿ������ظ���Ӧ�ò�����ܹ����������߼���
*/
void scanGenericCommand(redisClient *c, robj *o, unsigned long cursor) { /* cursor�α� */
    //SCAN cursor [MATCH pattern] [COUNT count]
    int rv;
    int i, j;
    char buf[REDIS_LONGSTR_SIZE];
    list *keys = listCreate();
    listNode *node, *nextnode;
    long count = 10;
    sds pat;
    int patlen, use_pattern = 0;
    dict *ht;

    /* Object must be NULL (to iterate keys names), or the type of the object
     * must be Set, Sorted Set, or Hash. */
    // �������ͼ��
    redisAssert(o == NULL || o->type == REDIS_SET || o->type == REDIS_HASH ||
                o->type == REDIS_ZSET);

    /* Set i to the first option argument. The previous one is the cursor. */
    // ���õ�һ��ѡ�����������λ��
    // 0    1      2      3  
    // SCAN OPTION <op_arg>         SCAN �����ѡ��ֵ������ 2 ��ʼ
    // HSCAN <key> OPTION <op_arg>  ������ *SCAN �����ѡ��ֵ������ 3 ��ʼ
    i = (o == NULL) ? 2 : 3; /* Skip the key argument if needed. */

    /* Step 1: Parse options. */
    // ����ѡ�����
    while (i < c->argc) {
        j = c->argc - i;

        // COUNT <number>
        if (!strcasecmp(c->argv[i]->ptr, "count") && j >= 2) { //�û�����ͨ������ʽ���������ṩ�� COUNT ѡ����ָ��ÿ�ε�������Ԫ�ص����ֵ��
            if (getLongFromObjectOrReply(c, c->argv[i+1], &count, NULL)
                != REDIS_OK)
            {
                goto cleanup;
            }

            if (count < 1) {
                addReply(c,shared.syntaxerr);
                goto cleanup;
            }

            i += 2;

        // MATCH <pattern>
        } else if (!strcasecmp(c->argv[i]->ptr, "match") && j >= 2) {
            pat = c->argv[i+1]->ptr;
            patlen = sdslen(pat);

            /* The pattern always matches if it is exactly "*", so it is
             * equivalent to disabling it. */
            use_pattern = !(pat[0] == '*' && patlen == 1);

            i += 2;

        // error
        } else {
            addReply(c,shared.syntaxerr);
            goto cleanup;
        }
    }

    /* Step 2: Iterate the collection.
     *
     * Note that if the object is encoded with a ziplist, intset, or any other
     * representation that is not a hash table, we are sure that it is also
     * composed of a small number of elements. So to avoid taking state we
     * just return everything inside the object in a single call, setting the
     * cursor to zero to signal the end of the iteration. */
     // �������ĵײ�ʵ��Ϊ ziplist ��intset �����ǹ�ϣ��
     // ��ô��Щ����Ӧ��ֻ����������Ԫ�أ�
     // Ϊ�˱��ֲ��÷�������¼����״̬�����
     // ���ǽ� ziplist ���� intset ���������Ԫ�ض�һ�η��ظ�������
     // ��������߷����α꣨cursor�� 0

    /* Handle the case of a hash table. */
    ht = NULL;
    if (o == NULL) { //��Ҫɨ���ֵ��е�����key
        // ����Ŀ��Ϊ���ݿ�
        ht = c->db->dict;
    } else if (o->type == REDIS_SET && o->encoding == REDIS_ENCODING_HT) {
        // ����Ŀ��Ϊ HT ����ļ���
        ht = o->ptr;
    } else if (o->type == REDIS_HASH && o->encoding == REDIS_ENCODING_HT) {
        // ����Ŀ��Ϊ HT ����Ĺ�ϣ
        ht = o->ptr;
        count *= 2; /* We return key / value for this type. */
    } else if (o->type == REDIS_ZSET && o->encoding == REDIS_ENCODING_SKIPLIST) {
        // ����Ŀ��Ϊ HT �������Ծ��
        zset *zs = o->ptr;
        ht = zs->dict;
        count *= 2; /* We return key / value for this type. */
    }

    if (ht) {
        void *privdata[2];

        /* We pass two pointers to the callback: the list to which it will
         * add new elements, and the object containing the dictionary so that
         * it is possible to fetch more data in a type-dependent way. */
        // ������ص�������������ָ�룺
        // һ�������ڼ�¼������Ԫ�ص��б�
        // ��һ�����ֵ����
        // �Ӷ�ʵ�������޹ص�������ȡ����
        privdata[0] = keys; //�����洢scan����10������
        privdata[1] = o;
        do {//һ����ɨ�裬��cursor��ʼ��Ȼ����ûص��������������õ�keys�������ݼ����档
            cursor = dictScan(ht, cursor, scanCallback, privdata);
        } while (cursor && listLength(keys) < count); //һ�����scan count�����ݣ�Ĭ��10������
    } else if (o->type == REDIS_SET) {
        int pos = 0;
        int64_t ll;

        while(intsetGet(o->ptr,pos++,&ll))
            listAddNodeTail(keys,createStringObjectFromLongLong(ll));
        cursor = 0;
    } else if (o->type == REDIS_HASH || o->type == REDIS_ZSET) {
        unsigned char *p = ziplistIndex(o->ptr,0);
        unsigned char *vstr;
        unsigned int vlen;
        long long vll;

        while(p) {
            ziplistGet(p,&vstr,&vlen,&vll);
            listAddNodeTail(keys,
                (vstr != NULL) ? createStringObject((char*)vstr,vlen) :
                                 createStringObjectFromLongLong(vll));
            p = ziplistNext(o->ptr,p);
        }
        cursor = 0;
    } else {
        redisPanic("Not handled encoding in SCAN.");
    }

    /* Step 3: Filter elements. */
    node = listFirst(keys);
    while (node) {
        robj *kobj = listNodeValue(node);
        nextnode = listNextNode(node);
        int filter = 0;

        /* Filter element if it does not match the pattern. */
        if (!filter && use_pattern) {
            if (sdsEncodedObject(kobj)) {
                if (!stringmatchlen(pat, patlen, kobj->ptr, sdslen(kobj->ptr), 0))
                    filter = 1;
            } else {
                char buf[REDIS_LONGSTR_SIZE];
                int len;

                redisAssert(kobj->encoding == REDIS_ENCODING_INT);
                len = ll2string(buf,sizeof(buf),(long)kobj->ptr);
                if (!stringmatchlen(pat, patlen, buf, len, 0)) filter = 1;
            }
        }

        /* Filter element if it is an expired key. */
        if (!filter && o == NULL && expireIfNeeded(c->db, kobj)) filter = 1;

        /* Remove the element and its associted value if needed. */
        if (filter) {
            decrRefCount(kobj);
            listDelNode(keys, node);
        }

        /* If this is a hash or a sorted set, we have a flat list of
         * key-value elements, so if this element was filtered, remove the
         * value, or skip it if it was not filtered: we only match keys. */
        if (o && (o->type == REDIS_ZSET || o->type == REDIS_HASH)) {
            node = nextnode;
            nextnode = listNextNode(node);
            if (filter) {
                kobj = listNodeValue(node);
                decrRefCount(kobj);
                listDelNode(keys, node);
            }
        }
        node = nextnode;
    }

    /* Step 4: Reply to the client. */
    addReplyMultiBulkLen(c, 2);
    rv = snprintf(buf, sizeof(buf), "%lu", cursor);
    redisAssert(rv < sizeof(buf));
    addReplyBulkCBuffer(c, buf, rv);

    addReplyMultiBulkLen(c, listLength(keys));
    while ((node = listFirst(keys)) != NULL) {
        robj *kobj = listNodeValue(node);
        addReplyBulk(c, kobj);
        decrRefCount(kobj);
        listDelNode(keys, node);
    }

cleanup:
    listSetFreeMethod(keys,decrRefCountVoid);
    listRelease(keys);
}

/* The SCAN command completely relies on scanGenericCommand. */
void scanCommand(redisClient *c) {
    unsigned long cursor;
    if (parseScanCursorOrReply(c,c->argv[1],&cursor) == REDIS_ERR) return;
    scanGenericCommand(c,NULL,cursor);
}

void dbsizeCommand(redisClient *c) {
    addReplyLongLong(c,dictSize(c->db->dict));
}

void lastsaveCommand(redisClient *c) {
    addReplyLongLong(c,server.lastsave);
}

/*
TYPE key

���� key �������ֵ�����͡�
���ð汾��>= 1.0.0ʱ�临�Ӷȣ�O(1)����ֵ��

none (key������)

string (�ַ���)

list (�б�)

set (����)

zset (����)

hash (��ϣ��)


# �ַ���

redis> SET weather "sunny"
OK

redis> TYPE weather
string


# �б�

redis> LPUSH book_list "programming in scala"
(integer) 1

redis> TYPE book_list
list


# ����

redis> SADD pat "dog"
(integer) 1

redis> TYPE pat
set


*/
void typeCommand(redisClient *c) {
    robj *o;
    char *type;

    o = lookupKeyRead(c->db,c->argv[1]);

    if (o == NULL) {
        type = "none";
    } else {
        switch(o->type) {
        case REDIS_STRING: type = "string"; break;
        case REDIS_LIST: type = "list"; break;
        case REDIS_SET: type = "set"; break;
        case REDIS_ZSET: type = "zset"; break;
        case REDIS_HASH: type = "hash"; break;
        default: type = "unknown"; break;
        }
    }

    addReplyStatus(c,type);
}

void shutdownCommand(redisClient *c) {
    int flags = 0;

    if (c->argc > 2) {
        addReply(c,shared.syntaxerr);
        return;
    } else if (c->argc == 2) {

        // ͣ��ʱ�����б���
        if (!strcasecmp(c->argv[1]->ptr,"nosave")) {
            flags |= REDIS_SHUTDOWN_NOSAVE;

        // ͣ��ʱ���б���
        } else if (!strcasecmp(c->argv[1]->ptr,"save")) {
            flags |= REDIS_SHUTDOWN_SAVE;

        } else {
            addReply(c,shared.syntaxerr);
            return;
        }
    }

    /* When SHUTDOWN is called while the server is loading a dataset in
     * memory we need to make sure no attempt is performed to save
     * the dataset on shutdown (otherwise it could overwrite the current DB
     * with half-read data).
     *
     * Also when in Sentinel mode clear the SAVE flag and force NOSAVE. */
    if (server.loading || server.sentinel_mode)
        flags = (flags & ~REDIS_SHUTDOWN_SAVE) | REDIS_SHUTDOWN_NOSAVE;

    if (prepareForShutdown(flags) == REDIS_OK) exit(0);

    addReplyError(c,"Errors trying to SHUTDOWN. Check logs.");
}

void renameGenericCommand(redisClient *c, int nx) {
    robj *o;
    long long expire;

    /* To use the same key as src and dst is probably an error */
    // ��Դ����Ŀ���������ͬ
    if (sdscmp(c->argv[1]->ptr,c->argv[2]->ptr) == 0) {
        addReply(c,shared.sameobjecterr);
        return;
    }

    // ȡ����Դ��
    if ((o = lookupKeyWriteOrReply(c,c->argv[1],shared.nokeyerr)) == NULL)
        return;

    // �������ü�������Ϊ����Ŀ���Ҳ�������������
    // ��������ӵĻ�������Դ����ɾ��ʱ�����ֵ����Ҳ�ᱻɾ��
    incrRefCount(o);

    // ȡ����Դ���Ĺ���ʱ��
    expire = getExpire(c->db,c->argv[1]);

    // ���Ŀ����Ƿ����
    if (lookupKeyWrite(c->db,c->argv[2]) != NULL) {

        // ���Ŀ������ڣ�����ִ�е��� RENAMENX ����ôֱ�ӷ���
        if (nx) {
            decrRefCount(o);
            addReply(c,shared.czero);
            return;
        }

        // ���ִ�е��� RENAME ����ôɾ�����е�Ŀ���
        /* Overwrite: delete the old key before creating the new one
         * with the same name. */
        dbDelete(c->db,c->argv[2]);
    }

    // ����Դ����ֵ�����Ŀ������й���
    dbAdd(c->db,c->argv[2],o);

    // ����й���ʱ�䣬��ôΪĿ������ù���ʱ��
    if (expire != -1) setExpire(c->db,c->argv[2],expire);

    // ɾ����Դ��
    dbDelete(c->db,c->argv[1]);

    signalModifiedKey(c->db,c->argv[1]);
    signalModifiedKey(c->db,c->argv[2]);

    notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"rename_from",
        c->argv[1],c->db->id);
    notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"rename_to",
        c->argv[2],c->db->id);

    server.dirty++;

    addReply(c,nx ? shared.cone : shared.ok);
}

/*
RENAME key newkey

�� key ����Ϊ newkey ��

�� key �� newkey ��ͬ������ key ������ʱ������һ������

�� newkey �Ѿ�����ʱ�� RENAME ������Ǿ�ֵ��
���ð汾��>= 1.0.0ʱ�临�Ӷȣ�O(1)����ֵ�������ɹ�ʱ��ʾ OK ��ʧ��ʱ�򷵻�һ������

# key ������ newkey ������

redis> SET message "hello world"
OK

redis> RENAME message greeting
OK

redis> EXISTS message               # message ��������
(integer) 0

redis> EXISTS greeting              # greeting ȡ����֮
(integer) 1


# �� key ������ʱ�����ش���

redis> RENAME fake_key never_exists
(error) ERR no such key


# newkey �Ѵ���ʱ�� RENAME �Ḳ�Ǿ� newkey

redis> SET pc "lenovo"
OK

redis> SET personal_computer "dell"
OK

redis> RENAME pc personal_computer
OK

redis> GET pc
(nil)

redis:1> GET personal_computer      # ԭ����ֵ dell ��������
"lenovo"


*/
void renameCommand(redisClient *c) {
    renameGenericCommand(c,0);
}

/*
RENAMENX key newkey

���ҽ��� newkey ������ʱ���� key ����Ϊ newkey ��

�� key ������ʱ������һ������
���ð汾��>= 1.0.0ʱ�临�Ӷȣ�O(1)����ֵ��

�޸ĳɹ�ʱ������ 1 ��

��� newkey �Ѿ����ڣ����� 0 ��


# newkey �����ڣ������ɹ�

redis> SET player "MPlyaer"
OK

redis> EXISTS best_player
(integer) 0

redis> RENAMENX player best_player
(integer) 1


# newkey����ʱ��ʧ��

redis> SET animal "bear"
OK

redis> SET favorite_animal "butterfly"
OK

redis> RENAMENX animal favorite_animal
(integer) 0

redis> get animal
"bear"

redis> get favorite_animal
"butterfly"


*/
void renamenxCommand(redisClient *c) {
    renameGenericCommand(c,1);
}

/*
MOVE key db

����ǰ���ݿ�� key �ƶ������������ݿ� db ���С�

�����ǰ���ݿ�(Դ���ݿ�)�͸������ݿ�(Ŀ�����ݿ�)����ͬ���ֵĸ��� key ������ key �������ڵ�ǰ���ݿ⣬��ô MOVE û���κ�Ч����

��ˣ�Ҳ����������һ���ԣ��� MOVE ������(locking)ԭ��(primitive)��
���ð汾��>= 1.0.0ʱ�临�Ӷȣ�O(1)����ֵ���ƶ��ɹ����� 1 ��ʧ���򷵻� 0 ��

# key �����ڵ�ǰ���ݿ�

redis> SELECT 0                             # redisĬ��ʹ�����ݿ� 0��Ϊ�������������������ʽָ��һ�Ρ�
OK

redis> SET song "secret base - Zone"
OK

redis> MOVE song 1                          # �� song �ƶ������ݿ� 1
(integer) 1

redis> EXISTS song                          # song �Ѿ�������
(integer) 0

redis> SELECT 1                             # ʹ�����ݿ� 1
OK

redis:1> EXISTS song                        # ֤ʵ song ���Ƶ������ݿ� 1 (ע��������ʾ�������"redis:1"����������ʹ�����ݿ� 1)
(integer) 1


# �� key �����ڵ�ʱ��

redis:1> EXISTS fake_key
(integer) 0

redis:1> MOVE fake_key 0                    # ��ͼ�����ݿ� 1 �ƶ�һ�������ڵ� key �����ݿ� 0��ʧ��
(integer) 0

redis:1> select 0                           # ʹ�����ݿ�0
OK

redis> EXISTS fake_key                      # ֤ʵ fake_key ������
(integer) 0


# ��Դ���ݿ��Ŀ�����ݿ�����ͬ�� key ʱ

redis> SELECT 0                             # ʹ�����ݿ�0
OK
redis> SET favorite_fruit "banana"
OK

redis> SELECT 1                             # ʹ�����ݿ�1
OK
redis:1> SET favorite_fruit "apple"
OK

redis:1> SELECT 0                           # ʹ�����ݿ�0������ͼ�� favorite_fruit �ƶ������ݿ� 1
OK

redis> MOVE favorite_fruit 1                # ��Ϊ�������ݿ�����ͬ�� key��MOVE ʧ��
(integer) 0

redis> GET favorite_fruit                   # ���ݿ� 0 �� favorite_fruit û��
"banana"

redis> SELECT 1
OK

redis:1> GET favorite_fruit                 # ���ݿ� 1 �� favorite_fruit Ҳ��
"apple"


*/ //ע��MOVED��ASING������
void moveCommand(redisClient *c) {
    robj *o;
    redisDb *src, *dst;
    int srcid;

    if (server.cluster_enabled) {
        addReplyError(c,"MOVE is not allowed in cluster mode");
        return;
    }

    /* Obtain source and target DB pointers */
    // Դ���ݿ�
    src = c->db;
    // Դ���ݿ�� id
    srcid = c->db->id;

    // �л���Ŀ�����ݿ�
    if (selectDb(c,atoi(c->argv[2]->ptr)) == REDIS_ERR) {
        addReply(c,shared.outofrangeerr);
        return;
    }

    // Ŀ�����ݿ�
    dst = c->db;

    // �л���Դ���ݿ�
    selectDb(c,srcid); /* Back to the source DB */

    /* If the user is moving using as target the same
     * DB as the source DB it is probably an error. */
    // ���Դ���ݿ��Ŀ�����ݿ���ȣ���ô���ش���
    if (src == dst) {
        addReply(c,shared.sameobjecterr);
        return;
    }

    /* Check if the element exists and get a reference */
    // ȡ��Ҫ�ƶ��Ķ���
    o = lookupKeyWrite(c->db,c->argv[1]);
    if (!o) {
        addReply(c,shared.czero);
        return;
    }

    /* Return zero if the key already exists in the target DB */
    // ������Ѿ�������Ŀ�����ݿ⣬��ô����
    if (lookupKeyWrite(dst,c->argv[1]) != NULL) {
        addReply(c,shared.czero);
        return;
    }

    // ������ӵ�Ŀ�����ݿ���
    dbAdd(dst,c->argv[1],o);
    // ���ӶԶ�������ü����������������Դ���ݿ���ɾ��ʱ o ������
    incrRefCount(o);

    /* OK! key moved, free the entry in the source DB */
    // ������Դ���ݿ��з���
    dbDelete(src,c->argv[1]);

    server.dirty++;

    addReply(c,shared.cone);
}

/*-----------------------------------------------------------------------------
 * Expires API
 *----------------------------------------------------------------------------*/

/*
 * �Ƴ��� key �Ĺ���ʱ��
 */
int removeExpire(redisDb *db, robj *key) {
    /* An expire may only be removed if there is a corresponding entry in the
     * main dict. Otherwise, the key will never be freed. */
    // ȷ�������й���ʱ��
    redisAssertWithInfo(NULL,key,dictFind(db->dict,key->ptr) != NULL);

    // ɾ������ʱ��
    return dictDelete(db->expires,key->ptr) == DICT_OK;
}

/*
 * ���� key �Ĺ���ʱ����Ϊ when
 */
void setExpire(redisDb *db, robj *key, long long when) {

    dictEntry *kde, *de;

    /* Reuse the sds from the main dict in the expire dict */
    // ȡ����
    kde = dictFind(db->dict,key->ptr);

    redisAssertWithInfo(NULL,key,kde != NULL);

    // ���ݼ�ȡ�����Ĺ���ʱ��
    de = dictReplaceRaw(db->expires,dictGetKey(kde));

    // ���ü��Ĺ���ʱ��
    // ������ֱ��ʹ������ֵ���������ʱ�䣬������ INT ����� String ����
    dictSetSignedIntegerVal(de,when);
}

/* Return the expire time of the specified key, or -1 if no expire
 * is associated with this key (i.e. the key is non volatile) 
 *
 * ���ظ��� key �Ĺ���ʱ�䡣
 *
 * �����û�����ù���ʱ�䣬��ô���� -1 ��
 */
long long getExpire(redisDb *db, robj *key) {
    dictEntry *de;

    /* No expire? return ASAP */
    // ��ȡ���Ĺ���ʱ��
    // �������ʱ�䲻���ڣ���ôֱ�ӷ���
    if (dictSize(db->expires) == 0 ||
       (de = dictFind(db->expires,key->ptr)) == NULL) return -1;

    /* The entry was found in the expire dict, this means it should also
     * be present in the main dict (safety check). */
    redisAssertWithInfo(NULL,key,dictFind(db->dict,key->ptr) != NULL);

    // ���ع���ʱ��
    return dictGetSignedIntegerVal(de);
}

/* Propagate expires into slaves and the AOF file.
 * When a key expires in the master, a DEL operation for this key is sent
 * to all the slaves and the AOF file if enabled.
 *
 * ������ʱ�䴫���������ڵ�� AOF �ļ���
 *
 * ��һ���������ڵ��й���ʱ��
 * ���ڵ�������и����ڵ�� AOF �ļ�����һ����ʽ�� DEL ���
 *
 * This way the key expiry is centralized in one place, and since both
 * AOF and the master->slave link guarantee operation ordering, everything
 * will be consistent even if we allow write operations against expiring
 * keys. 
 *
 * ��������ʹ�öԼ��Ĺ��ڿ��Լ�����һ������
 * ��Ϊ AOF �Լ����ڵ�͸����ڵ�֮������ӣ������Ա�֤������ִ��˳��
 * ���Լ�ʹ��д�����Թ��ڼ�ִ�У��������ݶ����� consistent �ġ�
 */ ////��������Ҫ�����ӹ��ڣ�
void propagateExpire(redisDb *db, robj *key) { //�����ں���Ҫ�����д�redis���ݿ�֪����������ҪЩһ��del��aOF
    robj *argv[2];

    // ����һ�� DEL key ����
    argv[0] = shared.del;
    argv[1] = key;
    incrRefCount(argv[0]);
    incrRefCount(argv[1]);

    // ������ AOF 
    if (server.aof_state != REDIS_AOF_OFF)
        feedAppendOnlyFile(server.delCommand,db->id,argv,2);

    // ���������и����ڵ�
    replicationFeedSlaves(server.slaves,db->id,argv,2);

    decrRefCount(argv[0]);
    decrRefCount(argv[1]);
}

/*
 * ��� key �Ƿ��Ѿ����ڣ�����ǵĻ������������ݿ���ɾ����
 *
 * ���� 0 ��ʾ��û�й���ʱ�䣬���߼�δ���ڡ�
 *
 * ���� 1 ��ʾ���Ѿ���Ϊ���ڶ���ɾ���ˡ�
 */ //ע��activeExpireCycle(����ɾ��)��freeMemoryIfNeeded(�������������ڴ棬�������ڴ���)  expireIfNeeded(��������ɾ�����ɶԸü�������ʱ������ж��Ƿ�ʱ)������
int expireIfNeeded(redisDb *db, robj *key) {

    // ȡ�����Ĺ���ʱ��
    mstime_t when = getExpire(db,key);
    mstime_t now;

    // û�й���ʱ��
    if (when < 0) return 0; /* No expire for this key */

    /* Don't expire anything while loading. It will be done later. */
    // ������������ڽ������룬��ô�������κι��ڼ��
    if (server.loading) return 0;

    /* If we are in the context of a Lua script, we claim that time is
     * blocked to when the Lua script started. This way a key can expire
     * only the first time it is accessed and not in the middle of the
     * script execution, making propagation to slaves / AOF consistent.
     * See issue #1525 on Github for more information. */
    now = server.lua_caller ? server.lua_time_start : mstime();

    /* If we are running in the context of a slave, return ASAP:
     * the slave key expiration is controlled by the master that will
     * send us synthesized DEL operations for expired keys.
     *
     * Still we try to return the right information to the caller, 
     * that is, 0 if we think the key should be still valid, 1 if
     * we think the key is expired at this time. */
    // �������������� replication ģʽʱ
    // �����ڵ㲢������ɾ�� key
    // ��ֻ����һ���߼�����ȷ�ķ���ֵ
    // ������ɾ������Ҫ�ȴ����ڵ㷢��ɾ������ʱ��ִ��
    // �Ӷ���֤���ݵ�ͬ��
    if (server.masterhost != NULL) return now > when;

    // ���е������ʾ�����й���ʱ�䣬���ҷ�����Ϊ���ڵ�

    /* Return when this key has not expired */
    // ���δ���ڣ����� 0
    if (now <= when) return 0;

    /* Delete the key */
    server.stat_expiredkeys++;

    // �� AOF �ļ��͸����ڵ㴫��������Ϣ
    propagateExpire(db,key);

    // �����¼�֪ͨ
    notifyKeyspaceEvent(REDIS_NOTIFY_EXPIRED,
        "expired",key,db->id);

    // �����ڼ������ݿ���ɾ��
    return dbDelete(db,key);
}

/*-----------------------------------------------------------------------------
 * Expires Commands
 *----------------------------------------------------------------------------*/

/* This is the generic command implementation for EXPIRE, PEXPIRE, EXPIREAT
 * and PEXPIREAT. Because the commad second argument may be relative or absolute
 * the "basetime" argument is used to signal what the base time is (either 0
 * for *AT variants of the command, or the current time for relative expires).
 *
 * ��������� EXPIRE �� PEXPIRE �� EXPIREAT �� PEXPIREAT ����ĵײ�ʵ�ֺ�����
 *
 * ����ĵڶ������������Ǿ���ֵ��Ҳ���������ֵ��
 * ��ִ�� *AT ����ʱ�� basetime Ϊ 0 ������������£�������ľ��ǵ�ǰ�ľ���ʱ�䡣
 *
 * unit is either UNIT_SECONDS or UNIT_MILLISECONDS, and is only used for
 * the argv[2] parameter. The basetime is always specified in milliseconds. 
 *
 * unit ����ָ�� argv[2] ���������ʱ�䣩�ĸ�ʽ��
 * �������� UNIT_SECONDS �� UNIT_MILLISECONDS ��
 * basetime ���������Ǻ����ʽ�ġ�
 */
void expireGenericCommand(redisClient *c, long long basetime, int unit) {
    robj *key = c->argv[1], *param = c->argv[2];
    long long when; /* unix time in milliseconds when the key will expire. */

    // ȡ�� when ����
    if (getLongLongFromObjectOrReply(c, param, &when, NULL) != REDIS_OK)
        return;

    // �������Ĺ���ʱ��������Ϊ��λ�ģ���ô����ת��Ϊ����
    if (unit == UNIT_SECONDS) when *= 1000;
    when += basetime;

    /* No key, return zero. */
    // ȡ����
    if (lookupKeyRead(c->db,key) == NULL) {
        addReply(c,shared.czero);
        return;
    }

    /* EXPIRE with negative TTL, or EXPIREAT with a timestamp into the past
     * should never be executed as a DEL when load the AOF or in the context
     * of a slave instance.
     *
     * ����������ʱ�����߷�����Ϊ�����ڵ�ʱ��
     * ��ʹ EXPIRE �� TTL Ϊ���������� EXPIREAT �ṩ��ʱ����Ѿ����ڣ�
     * ������Ҳ��������ɾ������������ǵȴ����ڵ㷢����ʽ�� DEL ���
     *
     * Instead we take the other branch of the IF statement setting an expire
     * (possibly in the past) and wait for an explicit DEL from the master. 
     *
     * ������������һ�������Ѿ����ڵ� TTL������Ϊ���Ĺ���ʱ�䣬
     * ���ҵȴ����ڵ㷢�� DEL ���
     */
    if (when <= mstime() && !server.loading && !server.masterhost) {

        // when �ṩ��ʱ���Ѿ����ڣ�������Ϊ���ڵ㣬����û����������

        robj *aux;

        redisAssertWithInfo(c,key,dbDelete(c->db,key));
        server.dirty++;

        /* Replicate/AOF this as an explicit DEL. */
        // ���� DEL ����
        aux = createStringObject("DEL",3);

        rewriteClientCommandVector(c,2,aux,key);
        decrRefCount(aux);

        signalModifiedKey(c->db,key);
        notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",key,c->db->id);

        addReply(c, shared.cone);

        return;
    } else {

        // ���ü��Ĺ���ʱ��
        // ���������Ϊ�����ڵ㣬���߷������������룬
        // ��ô��� when �п����Ѿ����ڵ�
        setExpire(c->db,key,when);

        addReply(c,shared.cone);

        signalModifiedKey(c->db,key);
        notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"expire",key,c->db->id);

        server.dirty++;

        return;
    }
}

/*
EXPIRE key seconds

Ϊ���� key ��������ʱ�䣬�� key ����ʱ(����ʱ��Ϊ 0 )�����ᱻ�Զ�ɾ����

�� Redis �У���������ʱ��� key ����Ϊ����ʧ�ġ�(volatile)��

����ʱ�����ͨ��ʹ�� DEL ������ɾ������ key ���Ƴ������߱� SET �� GETSET ���д(overwrite)������ζ�ţ����һ������ֻ���޸�(alter)һ��������ʱ��� key ��ֵ��������һ���µ� key ֵ������(replace)���Ļ�����ô����ʱ�䲻�ᱻ�ı䡣

����˵����һ�� key ִ�� INCR �����һ���б���� LPUSH ������߶�һ����ϣ��ִ�� HSET �����������������޸� key ���������ʱ�䡣

��һ���棬���ʹ�� RENAME ��һ�� key ���и�������ô������� key ������ʱ��͸���ǰһ����

RENAME �������һ�ֿ����ǣ����Խ�һ��������ʱ��� key ��������һ��������ʱ��� another_key ����ʱ�ɵ� another_key (�Լ���������ʱ��)�ᱻɾ����Ȼ��ɵ� key �����Ϊ another_key ����ˣ��µ� another_key ������ʱ��Ҳ��ԭ���� key һ����

ʹ�� PERSIST ��������ڲ�ɾ�� key ������£��Ƴ� key ������ʱ�䣬�� key ���³�Ϊһ�����־õġ�(persistent) key ��

��������ʱ��

���Զ�һ���Ѿ���������ʱ��� key ִ�� EXPIRE �����ָ��������ʱ���ȡ���ɵ�����ʱ�䡣

����ʱ��ľ�ȷ��

�� Redis 2.4 �汾�У�����ʱ����ӳ��� 1 ����֮�� ���� Ҳ���ǣ����� key �Ѿ����ڣ��������ǿ����ڹ���֮��һ����֮�ڱ����ʵ��������µ� Redis 2.6 �汾�У��ӳٱ����͵� 1 ����֮�ڡ�

Redis 2.1.3 ֮ǰ�Ĳ�֮ͬ��

�� Redis 2.1.3 ֮ǰ�İ汾�У��޸�һ����������ʱ��� key �ᵼ������ key ��ɾ������һ��Ϊ���ܵ�ʱ����(replication)������ƶ������ģ�������һ�����Ѿ����޸���
���ð汾��>= 1.0.0ʱ�临�Ӷȣ�O(1)����ֵ��

���óɹ����� 1 ��

�� key �����ڻ��߲���Ϊ key ��������ʱ��ʱ(�����ڵ��� 2.1.3 �汾�� Redis ���㳢�Ը��� key ������ʱ��)������ 0 ��


redis> SET cache_page "www.google.com"
OK

redis> EXPIRE cache_page 30  # ���ù���ʱ��Ϊ 30 ��
(integer) 1

redis> TTL cache_page    # �鿴ʣ������ʱ��
(integer) 23

redis> EXPIRE cache_page 30000   # ���¹���ʱ��
(integer) 1

redis> TTL cache_page
(integer) 29996

*/
void expireCommand(redisClient *c) { //PERSIST key�Ƴ����� key ������ʱ��
    expireGenericCommand(c,mstime(),UNIT_SECONDS);
}


/*
EXPIREAT

EXPIREAT key timestamp

EXPIREAT �����ú� EXPIRE ���ƣ�������Ϊ key ��������ʱ�䡣

��ͬ���� EXPIREAT ������ܵ�ʱ������� UNIX ʱ���(unix timestamp)��
���ð汾��>= 1.2.0ʱ�临�Ӷȣ�O(1)����ֵ��

�������ʱ�����óɹ������� 1 ��

�� key �����ڻ�û�취��������ʱ�䣬���� 0 ��


redis> SET cache www.google.com
OK

redis> EXPIREAT cache 1355292000     # ��� key ���� 2012.12.12 ����
(integer) 1

redis> TTL cache
(integer) 45081860
*/
void expireatCommand(redisClient *c) {//PERSIST key�Ƴ����� key ������ʱ��
    expireGenericCommand(c,0,UNIT_SECONDS);
}

/*
PEXPIRE key milliseconds

�������� EXPIRE ������������ƣ��������Ժ���Ϊ��λ���� key ������ʱ�䣬������ EXPIRE ��������������Ϊ��λ��
���ð汾��>= 2.6.0ʱ�临�Ӷȣ�O(1)����ֵ��

���óɹ������� 1

key �����ڻ�����ʧ�ܣ����� 0


redis> SET mykey "Hello"
OK

redis> PEXPIRE mykey 1500
(integer) 1

redis> TTL mykey    # TTL �ķ���ֵ����Ϊ��λ
(integer) 2

redis> PTTL mykey   # PTTL ���Ը���׼ȷ�ĺ�����
(integer) 1499




PEXPIREAT key milliseconds-timestamp

�������� EXPIREAT �������ƣ������Ժ���Ϊ��λ���� key �Ĺ��� unix ʱ������������� EXPIREAT ����������Ϊ��λ��
���ð汾��>= 2.6.0ʱ�临�Ӷȣ�O(1)����ֵ��

�������ʱ�����óɹ������� 1 ��

�� key �����ڻ�û�취��������ʱ��ʱ������ 0 ��(�鿴 EXPIRE �����ȡ������Ϣ)


redis> SET mykey "Hello"
OK

redis> PEXPIREAT mykey 1555555555005
(integer) 1

redis> TTL mykey           # TTL ������
(integer) 223157079

redis> PTTL mykey          # PTTL ���غ���
(integer) 223157079318

*/

void pexpireatCommand(redisClient *c) {//PERSIST key�Ƴ����� key ������ʱ��
    expireGenericCommand(c,0,UNIT_MILLISECONDS);
}

void pexpireCommand(redisClient *c) {//PERSIST key�Ƴ����� key ������ʱ��
    expireGenericCommand(c,mstime(),UNIT_MILLISECONDS);
}


/*
 * ���ؼ���ʣ������ʱ�䡣
 *
 * output_ms ָ������ֵ�ĸ�ʽ��
 *
 *  - Ϊ 1 ʱ�����غ���
 *
 *  - Ϊ 0 ʱ��������
 */
void ttlGenericCommand(redisClient *c, int output_ms) {
    long long expire, ttl = -1;

    /* If the key does not exist at all, return -2 */
    // ȡ����
    if (lookupKeyRead(c->db,c->argv[1]) == NULL) {
        addReplyLongLong(c,-2);
        return;
    }

    /* The key exists. Return -1 if it has no expire, or the actual
     * TTL value otherwise. */
    // ȡ������ʱ��
    expire = getExpire(c->db,c->argv[1]);

    if (expire != -1) {
        // ����ʣ������ʱ��
        ttl = expire-mstime();
        if (ttl < 0) ttl = 0;
    }

    if (ttl == -1) {
        // ���ǳ־õ�
        addReplyLongLong(c,-1);
    } else {
        // ���� TTL 
        // (ttl+500)/1000 ������ǽ�������
        addReplyLongLong(c,output_ms ? ttl : ((ttl+500)/1000));
    }
}

/*
TTL key

����Ϊ��λ�����ظ��� key ��ʣ������ʱ��(TTL, time to live)��
���ð汾��>= 1.0.0ʱ�临�Ӷȣ�O(1)����ֵ��

�� key ������ʱ������ -2 ��

�� key ���ڵ�û������ʣ������ʱ��ʱ������ -1 ��

��������Ϊ��λ������ key ��ʣ������ʱ�䡣



�� Redis 2.8 ��ǰ���� key �����ڣ����� key û������ʣ������ʱ��ʱ��������� -1 ��
 

# �����ڵ� key

redis> FLUSHDB
OK

redis> TTL key
(integer) -2


# key ���ڣ���û������ʣ������ʱ��

redis> SET key value
OK

redis> TTL key
(integer) -1


# ��ʣ������ʱ��� key

redis> EXPIRE key 10086
(integer) 1

redis> TTL key
(integer) 10084


*/
void ttlCommand(redisClient *c) {
    ttlGenericCommand(c, 0);
}

/*
PTTL key

������������� TTL ��������Ժ���Ϊ��λ���� key ��ʣ������ʱ�䣬�������� TTL ��������������Ϊ��λ��
���ð汾��>= 2.6.0���Ӷȣ�O(1)����ֵ��

�� key ������ʱ������ -2 ��

�� key ���ڵ�û������ʣ������ʱ��ʱ������ -1 ��

�����Ժ���Ϊ��λ������ key ��ʣ������ʱ�䡣



�� Redis 2.8 ��ǰ���� key �����ڣ����� key û������ʣ������ʱ��ʱ��������� -1 ��
 

# �����ڵ� key

redis> FLUSHDB
OK

redis> PTTL key
(integer) -2


# key ���ڣ���û������ʣ������ʱ��

redis> SET key value
OK

redis> PTTL key
(integer) -1


# ��ʣ������ʱ��� key

redis> PEXPIRE key 10086
(integer) 1

redis> PTTL key
(integer) 6179


*/
void pttlCommand(redisClient *c) {
    ttlGenericCommand(c, 1);
}

/*
PERSIST key

�Ƴ����� key ������ʱ�䣬����� key �ӡ���ʧ�ġ�(������ʱ�� key )ת���ɡ��־õġ�(һ����������ʱ�䡢�������ڵ� key )��
���ð汾��>= 2.2.0ʱ�临�Ӷȣ�O(1)����ֵ��

������ʱ���Ƴ��ɹ�ʱ������ 1 .

��� key �����ڻ� key û����������ʱ�䣬���� 0 ��


redis> SET mykey "Hello"
OK

redis> EXPIRE mykey 10  # Ϊ key ��������ʱ��
(integer) 1

redis> TTL mykey
(integer) 10

redis> PERSIST mykey    # �Ƴ� key ������ʱ��
(integer) 1

redis> TTL mykey
(integer) -1


*/
void persistCommand(redisClient *c) {
    dictEntry *de;

    // ȡ����
    de = dictFind(c->db->dict,c->argv[1]->ptr);

    if (de == NULL) {
        // ��û�й���ʱ��
        addReply(c,shared.czero);

    } else {

        // �����й���ʱ�䣬��ô�����Ƴ�
        if (removeExpire(c->db,c->argv[1])) {
            addReply(c,shared.cone);
            server.dirty++;

        // ���Ѿ��ǳ־õ���
        } else {
            addReply(c,shared.czero);
        }
    }
}

/* -----------------------------------------------------------------------------
 * API to get key arguments from commands
 * ---------------------------------------------------------------------------*/

/* The base case is to use the keys position as given in the command table
 * (firstkey, lastkey, step). */
int *getKeysUsingCommandTable(struct redisCommand *cmd,robj **argv, int argc, int *numkeys) {
    int j, i = 0, last, *keys;
    REDIS_NOTUSED(argv);

    if (cmd->firstkey == 0) {
        *numkeys = 0;
        return NULL;
    }
    last = cmd->lastkey;
    if (last < 0) last = argc+last;
    keys = zmalloc(sizeof(int)*((last - cmd->firstkey)+1));
    for (j = cmd->firstkey; j <= last; j += cmd->keystep) {
        redisAssert(j < argc);
        keys[i++] = j;
    }
    *numkeys = i;
    return keys;
}

/* Return all the arguments that are keys in the command passed via argc / argv.
 *
 * The command returns the positions of all the key arguments inside the array,
 * so the actual return value is an heap allocated array of integers. The
 * length of the array is returned by reference into *numkeys.
 *
 * 'cmd' must be point to the corresponding entry into the redisCommand
 * table, according to the command name in argv[0].
 *
 * This function uses the command table if a command-specific helper function
 * is not required, otherwise it calls the command-specific function. */
int *getKeysFromCommand(struct redisCommand *cmd, robj **argv, int argc, int *numkeys) {
    if (cmd->getkeys_proc) {
        return cmd->getkeys_proc(cmd,argv,argc,numkeys);
    } else {
        return getKeysUsingCommandTable(cmd,argv,argc,numkeys);
    }
}

/* Free the result of getKeysFromCommand. */
void getKeysFreeResult(int *result) {
    zfree(result);
}

/* Helper function to extract keys from following commands:
 * ZUNIONSTORE <destkey> <num-keys> <key> <key> ... <key> <options>
 * ZINTERSTORE <destkey> <num-keys> <key> <key> ... <key> <options> */
int *zunionInterGetKeys(struct redisCommand *cmd, robj **argv, int argc, int *numkeys) {
    int i, num, *keys;
    REDIS_NOTUSED(cmd);

    num = atoi(argv[2]->ptr);
    /* Sanity check. Don't return any key if the command is going to
     * reply with syntax error. */
    if (num > (argc-3)) {
        *numkeys = 0;
        return NULL;
    }

    /* Keys in z{union,inter}store come from two places:
     * argv[1] = storage key,
     * argv[3...n] = keys to intersect */
    keys = zmalloc(sizeof(int)*(num+1));

    /* Add all key positions for argv[3...n] to keys[] */
    for (i = 0; i < num; i++) keys[i] = 3+i;

    /* Finally add the argv[1] key position (the storage key target). */
    keys[num] = 1;
    *numkeys = num+1;  /* Total keys = {union,inter} keys + storage key */
    return keys;
}

/* Helper function to extract keys from the following commands:
 * EVAL <script> <num-keys> <key> <key> ... <key> [more stuff]
 * EVALSHA <script> <num-keys> <key> <key> ... <key> [more stuff] */
int *evalGetKeys(struct redisCommand *cmd, robj **argv, int argc, int *numkeys) {
    int i, num, *keys;
    REDIS_NOTUSED(cmd);

    num = atoi(argv[2]->ptr);
    /* Sanity check. Don't return any key if the command is going to
     * reply with syntax error. */
    if (num > (argc-3)) {
        *numkeys = 0;
        return NULL;
    }

    keys = zmalloc(sizeof(int)*num);
    *numkeys = num;

    /* Add all key positions for argv[3...n] to keys[] */
    for (i = 0; i < num; i++) keys[i] = 3+i;

    return keys;
}

/* Helper function to extract keys from the SORT command.
 *
 * SORT <sort-key> ... STORE <store-key> ...
 *
 * The first argument of SORT is always a key, however a list of options
 * follow in SQL-alike style. Here we parse just the minimum in order to
 * correctly identify keys in the "STORE" option. */
int *sortGetKeys(struct redisCommand *cmd, robj **argv, int argc, int *numkeys) {
    int i, j, num, *keys;
    REDIS_NOTUSED(cmd);

    num = 0;
    keys = zmalloc(sizeof(int)*2); /* Alloc 2 places for the worst case. */

    keys[num++] = 1; /* <sort-key> is always present. */

    /* Search for STORE option. By default we consider options to don't
     * have arguments, so if we find an unknown option name we scan the
     * next. However there are options with 1 or 2 arguments, so we
     * provide a list here in order to skip the right number of args. */
    struct {
        char *name;
        int skip;
    } skiplist[] = {
        {"limit", 2},
        {"get", 1},
        {"by", 1},
        {NULL, 0} /* End of elements. */
    };

    for (i = 2; i < argc; i++) {
        for (j = 0; skiplist[j].name != NULL; j++) {
            if (!strcasecmp(argv[i]->ptr,skiplist[j].name)) {
                i += skiplist[j].skip;
                break;
            } else if (!strcasecmp(argv[i]->ptr,"store") && i+1 < argc) {
                /* Note: we don't increment "num" here and continue the loop
                 * to be sure to process the *last* "STORE" option if multiple
                 * ones are provided. This is same behavior as SORT. */
                keys[num] = i+1; /* <store-key> */
                break;
            }
        }
    }
    *numkeys = num;
    return keys;
}

/* Slot to Key API. This is used by Redis Cluster in order to obtain in
 * a fast way a key that belongs to a specified hash slot. This is useful
 * while rehashing the cluster. */

//ע���ڴ�rdb�ļ�����aof�ļ��ж�ȡ��key-value�Ե�ʱ����������˼�Ⱥ���ܻ���dbAdd->slotToKeyAdd(key);�а�key��slot�Ķ�Ӧ��ϵ��ӵ�slots_to_keys
//����verifyClusterConfigWithData->clusterAddSlot�дӶ�ָ�ɶ�Ӧ��slot��Ҳ���Ǳ��������е�rdb�е�key-value��Ӧ��slot�������������


// ����������ӵ������棬
// �ڵ�� slots_to_keys ����Ծ���¼�� slot -> key ֮���ӳ��
// �������Կ��ٵش���ۺͼ��Ĺ�ϵ���� rehash ��ʱ�����á�
void slotToKeyAdd(robj *key) {

    // ������������Ĳ�
    unsigned int hashslot = keyHashSlot(key->ptr,sdslen(key->ptr));

    // ���� slot ��Ϊ��ֵ������Ϊ��Ա����ӵ� slots_to_keys ��Ծ������
    zslInsert(server.cluster->slots_to_keys,hashslot,key);
    incrRefCount(key);
}

// �Ӳ���ɾ�������ļ� key
void slotToKeyDel(robj *key) {
    unsigned int hashslot = keyHashSlot(key->ptr,sdslen(key->ptr));

    zslDelete(server.cluster->slots_to_keys,hashslot,key);
}

// ��սڵ����в۱�������м�
void slotToKeyFlush(void) {
    zslFree(server.cluster->slots_to_keys);
    server.cluster->slots_to_keys = zslCreate();
}

// ��¼ count ������ hashslot �۵ļ��� keys ����
// �����ر���¼��������
unsigned int getKeysInSlot(unsigned int hashslot, robj **keys, unsigned int count) {
    zskiplistNode *n;
    zrangespec range;
    int j = 0;

    range.min = range.max = hashslot;
    range.minex = range.maxex = 0;

    // ��λ����һ������ָ�� slot �ļ�����
    n = zslFirstInRange(server.cluster->slots_to_keys, &range);
    // ������Ծ������������ָ�� slot �ļ�
    // n && n->score ��鵱ǰ���Ƿ�����ָ�� slot
    // && count-- ��������
    while(n && n->score == hashslot && count--) {
        // ��¼��
        keys[j++] = n->obj;
        n = n->level[0].forward;
    }
    return j;
}

/* Remove all the keys in the specified hash slot.
 * The number of removed items is returned. */
//ɾ����λ�ϵ�����KV
unsigned int delKeysInSlot(unsigned int hashslot) {
    zskiplistNode *n;
    zrangespec range;
    int j = 0;

    range.min = range.max = hashslot;
    range.minex = range.maxex = 0;

    n = zslFirstInRange(server.cluster->slots_to_keys, &range);
    while(n && n->score == hashslot) {
        robj *key = n->obj;
        n = n->level[0].forward; /* Go to the next item before freeing it. */
        incrRefCount(key); /* Protect the object while freeing it. */
        dbDelete(&server.db[0],key);
        decrRefCount(key);
        j++;
    }
    return j;
}

//ע���ڴ�rdb�ļ�����aof�ļ��ж�ȡ��key-value�Ե�ʱ����������˼�Ⱥ���ܻ���dbAdd->slotToKeyAdd(key);�а�key��slot�Ķ�Ӧ��ϵ��ӵ�slots_to_keys
//����verifyClusterConfigWithData->clusterAddSlot�дӶ�ָ�ɶ�Ӧ��slot��Ҳ���Ǳ��������е�rdb�е�key-value��Ӧ��slot�������������

// ����ָ�� slot �����ļ�����
unsigned int countKeysInSlot(unsigned int hashslot) {
    zskiplist *zsl = server.cluster->slots_to_keys;
    zskiplistNode *zn;
    zrangespec range;
    int rank, count = 0;

    range.min = range.max = hashslot;
    range.minex = range.maxex = 0;

    /* Find first element in range */
    // ��λ����һ����ָ�� slot �ϵļ�
    zn = zslFirstInRange(zsl, &range);

    /* Use rank of first element, if any, to determine preliminary count */
    // ʹ�õ�һ��ָ�� slot ������λ��ȥ���һ��ָ�� slot ������λ
    // ��һ���������� slot ��������
    // ���������㷨

    // ��һ����ָ�� slot �ϵļ�����
    if (zn != NULL) {
        // ��ȡ��һ��������λ
        rank = zslGetRank(zsl, zn->score, zn->obj);
        count = (zsl->length - (rank - 1));

        /* Find last element in range */
        // ��ȡ���һ��ָ�� slot �ļ�
        zn = zslLastInRange(zsl, &range);

        /* Use rank of last element, if any, to determine the actual count */
        // ���һ��������
        if (zn != NULL) {
            // ��ȡ��λ
            rank = zslGetRank(zsl, zn->score, zn->obj);
            // ��������
            count -= (zsl->length - rank);
        }
    }

    return count;
}

