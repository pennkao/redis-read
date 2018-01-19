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

/* ================================ MULTI/EXEC ============================== */

/* Client state initialization for MULTI/EXEC 
 *
 * ��ʼ���ͻ��˵�����״̬
 */
void initClientMultiState(redisClient *c) {

    // �������
    c->mstate.commands = NULL;

    // �������
    c->mstate.count = 0;
}

/* Release all the resources associated with MULTI/EXEC state 
 *
 * �ͷ���������״̬��ص���Դ
 */
void freeClientMultiState(redisClient *c) {
    int j;

    // �����������
    for (j = 0; j < c->mstate.count; j++) {
        int i;
        multiCmd *mc = c->mstate.commands+j;

        // �ͷ������������
        for (i = 0; i < mc->argc; i++)
            decrRefCount(mc->argv[i]);

        // �ͷŲ������鱾��
        zfree(mc->argv);
    }

    // �ͷ��������
    zfree(c->mstate.commands);
}

/* Add a new command into the MULTI commands queue 
 *
 * ��һ����������ӵ����������
 */
void queueMultiCommand(redisClient *c) {
    multiCmd *mc;
    int j;

    // Ϊ������Ԫ�ط���ռ�
    c->mstate.commands = zrealloc(c->mstate.commands,
            sizeof(multiCmd)*(c->mstate.count+1));

    // ָ����Ԫ��
    mc = c->mstate.commands+c->mstate.count;

    // ��������������������������Լ�����Ĳ���
    mc->cmd = c->cmd;
    mc->argc = c->argc;
    mc->argv = zmalloc(sizeof(robj*)*c->argc);
    memcpy(mc->argv,c->argv,sizeof(robj*)*c->argc);
    for (j = 0; j < c->argc; j++)
        incrRefCount(mc->argv[j]);

    // ��������������������һ
    c->mstate.count++;
}

void discardTransaction(redisClient *c) {

    // ��������״̬
    freeClientMultiState(c);
    initClientMultiState(c);

    // ��������״̬
    c->flags &= ~(REDIS_MULTI|REDIS_DIRTY_CAS|REDIS_DIRTY_EXEC);;

    // ȡ�������м��ļ���
    unwatchAllKeys(c);
}

/* Flag the transacation as DIRTY_EXEC so that EXEC will fail.
 *
 * ������״̬��Ϊ DIRTY_EXEC ����֮��� EXEC ����ʧ�ܡ�
 *
 * Should be called every time there is an error while queueing a command. 
 *
 * ÿ��������������ʱ����
 */
void flagTransaction(redisClient *c) {
    if (c->flags & REDIS_MULTI)
        c->flags |= REDIS_DIRTY_EXEC;
}

void multiCommand(redisClient *c) {

    // ������������Ƕ������
    if (c->flags & REDIS_MULTI) {
        addReplyError(c,"MULTI calls can not be nested");
        return;
    }

    // ������ FLAG
    c->flags |= REDIS_MULTI;

    addReply(c,shared.ok);
}

void discardCommand(redisClient *c) {

    // �����ڿͻ���δ��������״̬֮ǰʹ��
    if (!(c->flags & REDIS_MULTI)) {
        addReplyError(c,"DISCARD without MULTI");
        return;
    }

    discardTransaction(c);

    addReply(c,shared.ok);
}

/* Send a MULTI command to all the slaves and AOF file. Check the execCommand
 * implementation for more information. 
 *
 * �����и����ڵ�� AOF �ļ����� MULTI ���
 */
void execCommandPropagateMulti(redisClient *c) {
    robj *multistring = createStringObject("MULTI",5);

    propagate(server.multiCommand,c->db->id,&multistring,1,
              REDIS_PROPAGATE_AOF|REDIS_PROPAGATE_REPL);

    decrRefCount(multistring);
}

//���ӻ��ƴ�����touchWatchedKey�����Ƿ񴥷�REDIS_DIRTY_CAS   ȡ�����ﺯ������watch�ļ��Ƿ��д���REDIS_DIRTY_CAS�������Ƿ����
//ִ�������е������execCommand��//ȡ��watch��unwatchAllKeys
void execCommand(redisClient *c) {
    int j;
    robj **orig_argv;
    int orig_argc;
    struct redisCommand *orig_cmd;
    int must_propagate = 0; /* Need to propagate MULTI/EXEC to AOF / slaves? */

    // �ͻ���û��ִ������
    if (!(c->flags & REDIS_MULTI)) {
        addReplyError(c,"EXEC without MULTI");
        return;
    }

    /* Check if we need to abort the EXEC because:
     *
     * ����Ƿ���Ҫ��ֹ����ִ�У���Ϊ��
     *
     * 1) Some WATCHed key was touched.
     *    �б����ӵļ��Ѿ����޸���
     *
     * 2) There was a previous error while queueing commands.
     *    ���������ʱ��������
     *    ��ע�������Ϊ�� 2.6.4 �Ժ���޸ĵģ�֮ǰ�Ǿ�Ĭ������ӳ������
     *
     * A failed EXEC in the first case returns a multi bulk nil object
     * (technically it is not an error but a special behavior), while
     * in the second an EXECABORT error is returned. 
     *
     * ��һ��������ض�������ظ��Ŀն���
     * ���ڶ�������򷵻�һ�� EXECABORT ����

     ԭ����:�������ԭ����ָ���ǣ����ݿ⽫�����еĶ����������һ��������ִ�У�������Ҫô
��ִ�������е����в�����Ҫô��һ������Ҳ��ִ�С�
     */ 
    if (c->flags & (REDIS_DIRTY_CAS|REDIS_DIRTY_EXEC)) {//��������е��κ�һ��������������key��watch���ӣ����Ҹ�key��ִ��watch��exec����֮�䷢���˱仯

        addReply(c, c->flags & REDIS_DIRTY_EXEC ? shared.execaborterr :
                                                  shared.nullmultibulk);

        // ȡ������
        discardTransaction(c);

        goto handle_monitor;
    }

    /* Exec all the queued commands */
    // �Ѿ����Ա�֤��ȫ���ˣ�ȡ���ͻ��˶����м��ļ���
    unwatchAllKeys(c); /* Unwatch ASAP otherwise we'll waste CPU cycles */

    // ��Ϊ�����е�������ִ��ʱ���ܻ��޸����������Ĳ���
    // ����Ϊ����ȷ�ش��������Ҫ�ֱ�����Щ����Ͳ���
    orig_argv = c->argv;
    orig_argc = c->argc;
    orig_cmd = c->cmd;

    addReplyMultiBulkLen(c,c->mstate.count);

    // ִ�������е�����
    for (j = 0; j < c->mstate.count; j++) {

        // ��Ϊ Redis ����������ڿͻ��˵���������ִ��
        // ����Ҫ����������е����������������ø��ͻ���
        c->argc = c->mstate.commands[j].argc;
        c->argv = c->mstate.commands[j].argv;
        c->cmd = c->mstate.commands[j].cmd;

        /* Propagate a MULTI request once we encounter the first write op.
         *
         * �����ϵ�һ��д����ʱ������ MULTI ���
         *
         * This way we'll deliver the MULTI/..../EXEC block as a whole and
         * both the AOF and the replication link will have the same consistency
         * and atomicity guarantees. 
         *
         * �����ȷ���������� AOF �ļ��Լ������ڵ������һ���ԡ�
         */
        if (!must_propagate && !(c->cmd->flags & REDIS_CMD_READONLY)) {

            // ���� MULTI ����
            execCommandPropagateMulti(c);

            // ��������ֻ����һ��
            must_propagate = 1;
        }

        // ִ������
        call(c,REDIS_CALL_FULL);

        /* Commands may alter argc/argv, restore mstate. */
        // ��Ϊִ�к��������������ܻᱻ�ı�
        // ���� SPOP �ᱻ��дΪ SREM
        // ����������Ҫ������������е�����Ͳ���
        // ȷ�������ڵ�� AOF ������һ����
        c->mstate.commands[j].argc = c->argc;
        c->mstate.commands[j].argv = c->argv;
        c->mstate.commands[j].cmd = c->cmd;
    }

    // ��ԭ����������
    c->argv = orig_argv;
    c->argc = orig_argc;
    c->cmd = orig_cmd;

    // ��������״̬
    discardTransaction(c);

    /* Make sure the EXEC command will be propagated as well if MULTI
     * was already propagated. */
    // ����������Ϊ�࣬ȷ�� EXEC ����Ҳ�ᱻ����
    if (must_propagate) server.dirty++;

handle_monitor:
    /* Send EXEC to clients waiting data from MONITOR. We do it here
     * since the natural order of commands execution is actually:
     * MUTLI, EXEC, ... commands inside transaction ...
     * Instead EXEC is flagged as REDIS_CMD_SKIP_MONITOR in the command
     * table, and we do it here with correct ordering. */
    if (listLength(server.monitors) && !server.loading)
        replicationFeedMonitors(c,server.monitors,c->db->id,c->argv,c->argc);
}

/* ===================== WATCH (CAS alike for MULTI/EXEC) ===================
 *
 * The implementation uses a per-DB hash table mapping keys to list of clients
 * WATCHing those keys, so that given a key that is going to be modified
 * we can mark all the associated clients as dirty.
 *
 * ���ʵ��Ϊÿ�����ݿⶼ������һ���� key ӳ��Ϊ list ���ֵ䣬
 * list �б���������м������ key �Ŀͻ��ˣ�
 * �����Ϳ�������� key ���޸ĵ�ʱ��
 * ����ض����м������ key �Ŀͻ��˽��д���
 *
 * Also every client contains a list of WATCHed keys so that's possible to
 * un-watch such keys when the client is freed or when UNWATCH is called. 
 *
 * ���⣬ÿ���ͻ��˶��ᱣ��һ���������б����Ӽ����б�
 * �����Ϳ��Է���ض����б����Ӽ����� UNWATCH ��
 */

/* In the client->watched_keys list we need to use watchedKey structures
 * as in order to identify a key in Redis we need both the key name and the
 * DB 
 *
 * �ڼ���һ����ʱ��
 * ���Ǽ���Ҫ���汻���ӵļ���
 * ����Ҫ����ü����ڵ����ݿ⡣
 */
typedef struct watchedKey {

    // �����ӵļ�
    robj *key;

    // �����ڵ����ݿ�
    redisDb *db;

} watchedKey;

/* Watch for the specified key 
 *
 * �ÿͻ��� c ���Ӹ����ļ� key
 */
void watchForKey(redisClient *c, robj *key) {

    list *clients = NULL;
    listIter li;
    listNode *ln;
    watchedKey *wk;

    /* Check if we are already watching for this key */
    // ��� key �Ƿ��Ѿ������� watched_keys �����У�
    // ����ǵĻ���ֱ�ӷ���
    listRewind(c->watched_keys,&li);
    while((ln = listNext(&li))) {
        wk = listNodeValue(ln);
        if (wk->db == c->db && equalStringObjects(key,wk->key))
            return; /* Key already watched */
    }

    // ���������� watched_keys �������

    // ������һ�� key ���������ֵ�����ӣ�
    // before :
    // {
    //  'key-1' : [c1, c2, c3],
    //  'key-2' : [c1, c2],
    // }
    // after c-10086 WATCH key-1 and key-3:
    // {
    //  'key-1' : [c1, c2, c3, c-10086],
    //  'key-2' : [c1, c2],
    //  'key-3' : [c-10086]
    // }

    /* This key is not already watched in this DB. Let's add it */
    // ��� key �Ƿ���������ݿ�� watched_keys �ֵ���
    clients = dictFetchValue(c->db->watched_keys,key);
    // ��������ڵĻ��������
    if (!clients) { 
        // ֵΪ����
        clients = listCreate();
        // ������ֵ�Ե��ֵ�
        dictAdd(c->db->watched_keys,key,clients);
        incrRefCount(key);
    }
    // ���ͻ�����ӵ������ĩβ
    listAddNodeTail(clients,c);

    /* Add the new key to the list of keys watched by this client */
    // ���� watchedKey �ṹ��ӵ��ͻ��� watched_keys ����ı�β
    // ������һ����� watchedKey �ṹ������
    // before:
    // [
    //  {
    //   'key': 'key-1',
    //   'db' : 0
    //  }
    // ]
    // after client watch key-123321 in db 0:
    // [
    //  {
    //   'key': 'key-1',
    //   'db' : 0
    //  }
    //  ,
    //  {
    //   'key': 'key-123321',
    //   'db': 0
    //  }
    // ]
    wk = zmalloc(sizeof(*wk));
    wk->key = key;
    wk->db = c->db;
    incrRefCount(key);
    listAddNodeTail(c->watched_keys,wk);
}

/* Unwatch all the keys watched by this client. To clean the EXEC dirty
 * flag is up to the caller. 
 *
 * ȡ���ͻ��˶����м��ļ��ӡ�
 *
 * ����ͻ�������״̬�������ɵ�����ִ�С�
 */
void unwatchAllKeys(redisClient *c) {
    listIter li;
    listNode *ln;

    // û�м������ӣ�ֱ�ӷ���
    if (listLength(c->watched_keys) == 0) return;

    // �������������б��ͻ��˼��ӵļ�
    listRewind(c->watched_keys,&li);
    while((ln = listNext(&li))) {
        list *clients;
        watchedKey *wk;

        /* Lookup the watched key -> clients list and remove the client
         * from the list */
        // �����ݿ�� watched_keys �ֵ�� key ����
        // ɾ������������Ŀͻ��˽ڵ�
        wk = listNodeValue(ln);
        // ȡ���ͻ�������
        clients = dictFetchValue(wk->db->watched_keys, wk->key);
        redisAssertWithInfo(c, NULL, clients != NULL);
        // ɾ�������еĿͻ��˽ڵ�
        listDelNode(clients,listSearchKey(clients,c));

        /* Kill the entry at all if this was the only client */
        // ��������Ѿ�����գ���ôɾ�������
        if (listLength(clients) == 0)
            dictDelete(wk->db->watched_keys, wk->key);

        /* Remove this watched key from the client->watched list */
        // ���������Ƴ� key �ڵ�
        listDelNode(c->watched_keys,ln);

        decrRefCount(wk->key);
        zfree(wk);
    }
}

/* "Touch" a key, so that if this key is being WATCHed by some client the
 * next EXEC will fail. 
 *

 ���ж����ݿ�����޸ĵ��������SET��LPUSH��SADD��ZREM����DEL��FLUSHDB�ȵȣ���ִ��֮�󶼻�touchWatchKey������watched_keys�ֵ�
 ���м�飬�鿴�Ƿ��пͻ������ڼ��Ӹոձ������޸Ĺ������ݿ��������еĻ�����ôtouchWatchKey�����Ὣ���ӱ��޸ļ��Ŀͻ��˵�
 REDIS_DIRTY_CAS��ʶ�򿪣���ʾ�ÿͻ��˵�����ȫ���Ѿ����ƻ���


 
 * ��������һ�����������������ڱ�ĳ��/ĳЩ�ͻ��˼����ţ�
 * ��ô���/��Щ�ͻ�����ִ�� EXEC ʱ����ʧ�ܡ�
 */ //���ӻ��ƴ�����touchWatchedKey  
 //���ӻ��ƴ�����touchWatchedKey�����Ƿ񴥷�REDIS_DIRTY_CAS   ȡ�����ﺯ������watch�ļ��Ƿ��д���REDIS_DIRTY_CAS�������Ƿ����ִ�������е������execCommand
//ȡ��watch��unwatchAllKeys
void touchWatchedKey(redisDb *db, robj *key) {
    list *clients;
    listIter li;
    listNode *ln;

    // �ֵ�Ϊ�գ�û���κμ�������
    if (dictSize(db->watched_keys) == 0) return;

    // ��ȡ���м���������Ŀͻ���
    clients = dictFetchValue(db->watched_keys, key);
    if (!clients) return;

    /* Mark all the clients watching this key as REDIS_DIRTY_CAS */
    /* Check if we are already watching for this key */
    // �������пͻ��ˣ������ǵ� REDIS_DIRTY_CAS ��ʶ
    listRewind(clients,&li);
    while((ln = listNext(&li))) {
        redisClient *c = listNodeValue(ln);

        c->flags |= REDIS_DIRTY_CAS;
    }
}

/* On FLUSHDB or FLUSHALL all the watched keys that are present before the
 * flush but will be deleted as effect of the flushing operation should
 * be touched. "dbid" is the DB that's getting the flush. -1 if it is
 * a FLUSHALL operation (all the DBs flushed). 
 *
 * ��һ�����ݿⱻ FLUSHDB ���� FLUSHALL ���ʱ��
 * �����ݿ��ڵ����� key ��Ӧ�ñ�������
 *
 * dbid ����ָ��Ҫ�� FLUSH �����ݿ⡣
 *
 * ��� dbid Ϊ -1 ����ô��ʾִ�е��� FLUSHALL ��
 * �������ݿⶼ���� FLUSH
 */ 
void touchWatchedKeysOnFlush(int dbid) {
    listIter li1, li2;
    listNode *ln;

    // �����˼·ͦ��Ȥ�ģ����Ǳ������ݿ������ key ���ÿͻ��˱�Ϊ DIRTY
    // ���Ǳ������пͻ��ˣ�Ȼ������ͻ��˼��ӵļ���������Ӧ�Ŀͻ��˱�Ϊ DIRTY
    // ����Ҫ��ǰ�߸�Ч�ܶ�

    /* For every client, check all the waited keys */
    // �������пͻ���
    listRewind(server.clients,&li1);
    while((ln = listNext(&li1))) {

        redisClient *c = listNodeValue(ln);

        // �����ͻ��˼��ӵļ�
        listRewind(c->watched_keys,&li2);
        while((ln = listNext(&li2))) {

            // ȡ�����ӵļ��ͼ������ݿ�
            watchedKey *wk = listNodeValue(ln);

            /* For every watched key matching the specified DB, if the
             * key exists, mark the client as dirty, as the key will be
             * removed. */
            // ������ݿ������ͬ������ִ�е�����Ϊ FLUSHALL
            // ��ô���ͻ�������Ϊ REDIS_DIRTY_CAS
            if (dbid == -1 || wk->db->id == dbid) {
                if (dictFind(wk->db->dict, wk->key->ptr) != NULL)
                    c->flags |= REDIS_DIRTY_CAS;
            }
        }
    }
}

void watchCommand(redisClient *c) {
    int j;

    // ����������ʼ��ִ��
    if (c->flags & REDIS_MULTI) {
        addReplyError(c,"WATCH inside MULTI is not allowed");
        return;
    }

    // ����������������
    for (j = 1; j < c->argc; j++)
        watchForKey(c,c->argv[j]);

    addReply(c,shared.ok);
}

void unwatchCommand(redisClient *c) {

    // ȡ���ͻ��˶����м��ļ���
    unwatchAllKeys(c);
    
    // ����״̬
    c->flags &= (~REDIS_DIRTY_CAS);

    addReply(c,shared.ok);
}
