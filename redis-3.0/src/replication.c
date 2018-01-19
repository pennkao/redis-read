/* Asynchronous replication implementation.
 *
 * �첽����ʵ��
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

#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>

void replicationDiscardCachedMaster(void);
void replicationResurrectCachedMaster(int newfd);
void replicationSendAck(void);

/* ---------------------------------- MASTER -------------------------------- */

// ���� backlog
void createReplicationBacklog(void) {

    redisAssert(server.repl_backlog == NULL);

    // backlog
    server.repl_backlog = zmalloc(server.repl_backlog_size);
    // ���ݳ���
    server.repl_backlog_histlen = 0;
    // ����ֵ����������ʱʹ��
    server.repl_backlog_idx = 0;
    /* When a new backlog buffer is created, we increment the replication
     * offset by one to make sure we'll not be able to PSYNC with any
     * previous slave. This is needed because we avoid incrementing the
     * master_repl_offset if no backlog exists nor slaves are attached. */
    // ÿ�δ��� backlog ʱ���� master_repl_offset ��һ
    // ����Ϊ�˷�ֹ֮ǰʹ�ù� backlog �Ĵӷ�������������� PSYNC ����
    server.master_repl_offset++;

    /* We don't have any data inside our buffer, but virtually the first
     * byte we have is the next byte that will be generated for the
     * replication stream. */
    // ����û���κ����ݣ�
    // �� backlog ��һ���ֽڵ��߼�λ��Ӧ���� repl_offset ��ĵ�һ���ֽ�
    server.repl_backlog_off = server.master_repl_offset+1;
}

/* This function is called when the user modifies the replication backlog
 * size at runtime. It is up to the function to both update the
 * server.repl_backlog_size and to resize the buffer and setup it so that
 * it contains the same data as the previous one (possibly less data, but
 * the most recent bytes, or the same data and more free space in case the
 * buffer is enlarged). */
// ��̬���� backlog ��С
// �� backlog �Ǳ�����ʱ��ԭ�е����ݻᱻ������
// ��Ϊ����ռ�ʹ�õ��� realloc
void resizeReplicationBacklog(long long newsize) {

    // ����С����С��С
    if (newsize < REDIS_REPL_BACKLOG_MIN_SIZE)
        newsize = REDIS_REPL_BACKLOG_MIN_SIZE;

    // ��С��Ŀǰ��С���
    if (server.repl_backlog_size == newsize) return;

    // �����´�С
    server.repl_backlog_size = newsize;
    if (server.repl_backlog != NULL) {
        /* What we actually do is to flush the old buffer and realloc a new
         * empty one. It will refill with new data incrementally.
         * The reason is that copying a few gigabytes adds latency and even
         * worse often we need to alloc additional space before freeing the
         * old buffer. */
        // �ͷ� backlog
        zfree(server.repl_backlog);
        // ���´�С������ backlog
        server.repl_backlog = zmalloc(server.repl_backlog_size);
        server.repl_backlog_histlen = 0;
        server.repl_backlog_idx = 0;
        /* Next byte we have is... the next since the buffer is emtpy. */
        server.repl_backlog_off = server.master_repl_offset+1;
    }
}

// �ͷ� backlog
void freeReplicationBacklog(void) {
    redisAssert(listLength(server.slaves) == 0);
    zfree(server.repl_backlog);
    server.repl_backlog = NULL;
}

/* Add data to the replication backlog.
 * This function also increments the global replication offset stored at
 * server.master_repl_offset, because there is no case where we want to feed
 * the backlog without incrementing the buffer. 
 *
 * ������ݵ����� backlog ��
 * ���Ұ���������ݵĳ��ȸ��� server.master_repl_offset ƫ������
 */

  /*���ƻ�ѹ��������feedReplicationBacklog�������Ҫ�����������ϣ�Ȼ��ͨ��client->reply����洢������ЩKV����ЩKV������
 ���ͳ�ȥ�ˣ�ʵ���϶Է�û���յ�,�´θĿͻ������Ϻ�ͨ��replconf ack xxx��ͨ���Լ���offset��master�յ���Ϳ����ж϶Է��Ƿ���û��ȫ������
 ���͵�client��ʵʱKV��ѹbuffer������checkClientOutputBufferLimits 

 ��������������:client->reply        checkClientOutputBufferLimits    ��ҪӦ�Կͻ��˶�ȡ����ͬʱ���д���KV���뱾�ڵ㣬��ɻ�ѹ
 ���ƻ�ѹ������:server.repl_backlog    feedReplicationBacklog    ��ҪӦ���������ϣ����в�����ͬ��psyn������ȫ��ͬ��
 */

//info replication�е�repl_backlog_first_byte_offset����ʾ��ѹ��������Ч���ݵ���ʵƫ���� 
//master_repl_offset��ʾ��ѹ�������еĽ���ƫ����������ƫ��������ʼ�������е�buf���ǿ��õ�����
//repl_backlog_histlen��ʾ��ѹ�����������ݵĴ�С
void feedReplicationBacklog(void *ptr, size_t len) { //��ptr������server.repl_backlog���ƻ�ѹ��������
    unsigned char *p = ptr;

    // �������ۼӵ�ȫ�� offset ��
    server.master_repl_offset += len;

    /* This is a circular buffer, so write as much data we can at every
     * iteration and rewind the "idx" index if we reach the limit. */
    // ���� buffer ��ÿ��д�����ܶ�����ݣ����ڵ���β��ʱ�� idx ���õ�ͷ��
    while(len) {
        // �� idx �� backlog β�����ֽ���   repl_backlog_sizeΪ������ѹ�������Ĵ�С
        size_t thislen = server.repl_backlog_size - server.repl_backlog_idx;
        // ��� idx �� backlog β����οռ���������Ҫд�������
        // ��ôֱ�ӽ�д�����ݳ�����Ϊ len
        // �ڽ���Щ len �ֽڸ���֮����� while ѭ��������
        if (thislen > len) thislen = len;
        // �� p �е� thislen �ֽ����ݸ��Ƶ� backlog
        memcpy(server.repl_backlog+server.repl_backlog_idx,p,thislen); //��ptr������server.repl_backlog���ƻ�ѹ��������
        // ���� idx ��ָ����д�������֮��
        server.repl_backlog_idx += thislen;
        // ���д��ﵽβ������ô���������õ�ͷ��
        if (server.repl_backlog_idx == server.repl_backlog_size)
            server.repl_backlog_idx = 0;
        // ��ȥ��д����ֽ���
        len -= thislen;
        // ��ָ���ƶ����ѱ�д�����ݵĺ��棬ָ��δ���������ݵĿ�ͷ
        p += thislen;
        // ����ʵ�ʳ���
        server.repl_backlog_histlen += thislen;
    }
    // histlen �����ֵֻ�ܵ��� backlog_size
    // ���⣬�� histlen ���� repl_backlog_size ʱ��
    // ��ʾд�����ݵ�ǰͷ��һ�������ݱ��Լ���β��������
    // �ٸ����ӣ����� abcde Ҫд�뵽һ��ֻ�������ֽڵĻ���������
    // �Ҽ�������Ϊ 0
    // ��ô abc ���ȱ�д�룬����Ϊ [a, b, c] 
    // Ȼ�� de ��д�룬����Ϊ [d, e, c]
    if (server.repl_backlog_histlen > server.repl_backlog_size)
        server.repl_backlog_histlen = server.repl_backlog_size;
    /* Set the offset of the first byte we have in the backlog. */
    // ��¼����������� backlog ����ԭ�����ݵĵ�һ���ֽڵ�ƫ����
    // ���� master_repl_offset = 10086
    // repl_backlog_histlen = 30
    // ��ô backlog ����������ݵĵ�һ���ֽڵ�ƫ����Ϊ
    // 10086 - 30 + 1 = 10056 + 1 = 10057
    // ��˵������ӷ���������� 10057 �� 10086 ֮����κ�ʱ�����
    // ��ô�ӷ�����������ʹ�� PSYNC
    server.repl_backlog_off = server.master_repl_offset -
                              server.repl_backlog_histlen + 1;
}

/* Wrapper for feedReplicationBacklog() that takes Redis string objects
 * as input. */
// �� Redis ����Ž� replication backlog ����
void feedReplicationBacklogWithObject(robj *o) {
    char llstr[REDIS_LONGSTR_SIZE];
    void *p;
    size_t len;

    if (o->encoding == REDIS_ENCODING_INT) {
        len = ll2string(llstr,sizeof(llstr),(long)o->ptr);
        p = llstr;
    } else {
        len = sdslen(o->ptr);
        p = o->ptr;
    }
    feedReplicationBacklog(p,len);
}

// ������Ĳ������͸��ӷ�����
// ������Ϊ������
// 1�� ����Э������
// 2�� ��Э�����ݱ��ݵ� backlog
// 3�� �����ݷ��͸������ӷ�����
void replicationFeedSlaves(list *slaves, int dictid, robj **argv, int argc) {
//��slave��master��ͨѶ����ͨ����������listen�˿ڣ�����listenΪ7000����Ⱥ���Ķ˿ھ���17000�����Ǵ��Ǻ�7000ͨѶ
    listNode *ln;
    listIter li;
    int j, len;
    char llstr[REDIS_LONGSTR_SIZE];

    /* If there aren't slaves, and there is no backlog buffer to populate,
     * we can return ASAP. */
    // backlog Ϊ�գ���û�дӷ�������ֱ�ӷ���
    if (server.repl_backlog == NULL && listLength(slaves) == 0) return;

    /* We can't have slaves attached and no backlog. */
    redisAssert(!(listLength(slaves) != 0 && server.repl_backlog == NULL));

    /* Send SELECT command to every slave if needed. */
    // �������Ҫ�Ļ������� SELECT ���ָ�����ݿ�
    if (server.slaveseldb != dictid) {
        robj *selectcmd;

        /* For a few DBs we have pre-computed SELECT command. */
        if (dictid >= 0 && dictid < REDIS_SHARED_SELECT_CMDS) {
            selectcmd = shared.select[dictid];
        } else {
            int dictid_len;

            dictid_len = ll2string(llstr,sizeof(llstr),dictid);
            selectcmd = createObject(REDIS_STRING,
                sdscatprintf(sdsempty(),
                "*2\r\n$6\r\nSELECT\r\n$%d\r\n%s\r\n",
                dictid_len, llstr));
        }

        /* Add the SELECT command into the backlog. */
        // �� SELECT ������ӵ� backlog
        if (server.repl_backlog) feedReplicationBacklogWithObject(selectcmd);

        /* Send it to slaves. */
        // ���͸����дӷ�����
        listRewind(slaves,&li);
        while((ln = listNext(&li))) {
            redisClient *slave = ln->value;
            addReply(slave,selectcmd);
        }

        if (dictid < 0 || dictid >= REDIS_SHARED_SELECT_CMDS)
            decrRefCount(selectcmd);
    }

    server.slaveseldb = dictid;

    /* Write the command to the replication backlog if any. */
    // ������д�뵽backlog
    if (server.repl_backlog) {
        char aux[REDIS_LONGSTR_SIZE+3];

        /* Add the multi bulk reply length. */
        aux[0] = '*';
        len = ll2string(aux+1,sizeof(aux)-1,argc);
        aux[len+1] = '\r';
        aux[len+2] = '\n';
        feedReplicationBacklog(aux,len+3);

        for (j = 0; j < argc; j++) {
            long objlen = stringObjectLen(argv[j]);

            /* We need to feed the buffer with the object as a bulk reply
             * not just as a plain string, so create the $..CRLF payload len 
             * ad add the final CRLF */
            // �������Ӷ���ת����Э���ʽ
            aux[0] = '$';
            len = ll2string(aux+1,sizeof(aux)-1,objlen);
            aux[len+1] = '\r';
            aux[len+2] = '\n';
            feedReplicationBacklog(aux,len+3);
            feedReplicationBacklogWithObject(argv[j]);
            feedReplicationBacklog(aux+len+1,2);
        }
    }

    /* Write the command to every slave. */
    listRewind(slaves,&li);
    while((ln = listNext(&li))) { //�᲻ͣ������client out buffer��д���ݣ��������client-output-buffer-limit���ã����ֱ�ӶϿ��ÿͻ�������

        // ָ��ӷ�����
        redisClient *slave = ln->value;

        /* Don't feed slaves that are still waiting for BGSAVE to start */
        // ��Ҫ�����ڵȴ� BGSAVE ��ʼ�Ĵӷ�������������
        if (slave->replstate == REDIS_REPL_WAIT_BGSAVE_START) continue;

        /* Feed slaves that are waiting for the initial SYNC (so these commands
         * are queued in the output buffer until the initial SYNC completes),
         * or are already in sync with the master. */
        // ���Ѿ�����������ڽ��� RDB �ļ��Ĵӷ�������������
        // ����ӷ��������ڽ��������������͵� RDB �ļ���
        // ��ô�ڳ��� SYNC ���֮ǰ�������������͵����ݻᱻ�Ž�һ������������

        /* Add the multi bulk length. */
        addReplyMultiBulkLen(slave,argc);

        /* Finally any additional argument that was not stored inside the
         * static buffer if any (from j to argc). */
        for (j = 0; j < argc; j++)
            addReplyBulk(slave,argv[j]);
    }
}

// ��Э�鷢�� Monitor
void replicationFeedMonitors(redisClient *c, list *monitors, int dictid, robj **argv, int argc) {
    listNode *ln;
    listIter li;
    int j;
    sds cmdrepr = sdsnew("+");
    robj *cmdobj;
    struct timeval tv;

    // ��ȡʱ���
    gettimeofday(&tv,NULL);
    cmdrepr = sdscatprintf(cmdrepr,"%ld.%06ld ",(long)tv.tv_sec,(long)tv.tv_usec);
    if (c->flags & REDIS_LUA_CLIENT) {
        cmdrepr = sdscatprintf(cmdrepr,"[%d lua] ",dictid);
    } else if (c->flags & REDIS_UNIX_SOCKET) {
        cmdrepr = sdscatprintf(cmdrepr,"[%d unix:%s] ",dictid,server.unixsocket);
    } else {
        cmdrepr = sdscatprintf(cmdrepr,"[%d %s] ",dictid,getClientPeerId(c));
    }

    // ��ȡ����Ͳ���
    for (j = 0; j < argc; j++) {
        if (argv[j]->encoding == REDIS_ENCODING_INT) {
            cmdrepr = sdscatprintf(cmdrepr, "\"%ld\"", (long)argv[j]->ptr);
        } else {
            cmdrepr = sdscatrepr(cmdrepr,(char*)argv[j]->ptr,
                        sdslen(argv[j]->ptr));
        }
        if (j != argc-1)
            cmdrepr = sdscatlen(cmdrepr," ",1);
    }
    cmdrepr = sdscatlen(cmdrepr,"\r\n",2);
    cmdobj = createObject(REDIS_STRING,cmdrepr);

    // �����ݷ��͸����� MONITOR 
    listRewind(monitors,&li);
    while((ln = listNext(&li))) {
        redisClient *monitor = ln->value;
        addReply(monitor,cmdobj);
    }
    decrRefCount(cmdobj);
}

/* Feed the slave 'c' with the replication backlog starting from the
 * specified 'offset' up to the end of the backlog. */
// ��ӷ����� c ���� backlog �д� offset �� backlog β��֮�������
long long addReplyReplicationBacklog(redisClient *c, long long offset) {
    long long j, skip, len;

    redisLog(REDIS_DEBUG, "[PSYNC] Slave request offset: %lld", offset);

    if (server.repl_backlog_histlen == 0) {
        redisLog(REDIS_DEBUG, "[PSYNC] Backlog history len is zero");
        return 0;
    }

    redisLog(REDIS_DEBUG, "[PSYNC] Backlog size: %lld",
             server.repl_backlog_size);
    redisLog(REDIS_DEBUG, "[PSYNC] First byte: %lld",
             server.repl_backlog_off);
    redisLog(REDIS_DEBUG, "[PSYNC] History len: %lld",
             server.repl_backlog_histlen);
    redisLog(REDIS_DEBUG, "[PSYNC] Current index: %lld",
             server.repl_backlog_idx);

    /* Compute the amount of bytes we need to discard. */
    skip = offset - server.repl_backlog_off;
    redisLog(REDIS_DEBUG, "[PSYNC] Skipping: %lld", skip);

    /* Point j to the oldest byte, that is actaully our
     * server.repl_backlog_off byte. */
    j = (server.repl_backlog_idx +
        (server.repl_backlog_size-server.repl_backlog_histlen)) %
        server.repl_backlog_size;
    redisLog(REDIS_DEBUG, "[PSYNC] Index of first byte: %lld", j);

    /* Discard the amount of data to seek to the specified 'offset'. */
    j = (j + skip) % server.repl_backlog_size;

    /* Feed slave with data. Since it is a circular buffer we have to
     * split the reply in two parts if we are cross-boundary. */
    len = server.repl_backlog_histlen - skip;
    redisLog(REDIS_DEBUG, "[PSYNC] Reply total length: %lld", len);
    while(len) {
        long long thislen =
            ((server.repl_backlog_size - j) < len) ?
            (server.repl_backlog_size - j) : len;

        redisLog(REDIS_DEBUG, "[PSYNC] addReply() length: %lld", thislen);
        addReplySds(c,sdsnewlen(server.repl_backlog + j, thislen));
        len -= thislen;
        j = 0;
    }
    return server.repl_backlog_histlen - skip;
}

/* This function handles the PSYNC command from the point of view of a
 * master receiving a request for partial resynchronization.
 *
 * On success return REDIS_OK, otherwise REDIS_ERR is returned and we proceed
 * with the usual full resync. */

/*
PSYNC����ĵ��÷��������֣�
 1. ����ӷ�������ǰû�и��ƹ��κ���������������֮ǰִ�й�SLAVEOF no one
   �����ô�ӷ������ڿ�ʼһ���µĸ���ʱ����������������PSYNC?-1���
   ��������������������������ͬ������Ϊ��ʱ������ִ�в�����ͬ������
   ���෴�أ�����ӷ������Ѿ����ƹ�ĳ��������������ô�ӷ������ڿ�ʼһ���µĸ�
   ��ʱ����������������PSYNC <runid> <offset>�������runid����һ��
   ���Ƶ���������������ID����offset���Ǵӷ�������ǰ�ĸ���ƫ���������յ���
   �����������������ͨ���������������ж�Ӧ�öԴӷ�����ִ������ͬ��������
 ������������յ�PSYNC�����������������ӷ����������������ֻظ�������һ�֣�
 2. ���������������+FULLRESYNC  <runid>  <offset>�ظ�����ô��ʾ������
   ������ӷ�����ִ��������ͬ������������runid�������������������ID����
   �������Ὣ���ID��������������һ�η���PSYNC����ʱʹ�ã���offset����
   ����������ǰ�ĸ���ƫ�������ӷ������Ὣ���ֵ��Ϊ�Լ��ĳ�ʼ��ƫ������
3. ���������������+CONTINUE�ظ�����ô��ʾ������������ӷ�����ִ�в�����
  ͬ���������ӷ�����ֻҪ���������������Լ�ȱ�ٵ��ǲ������ݷ��͹����Ϳ����ˡ�
4. ���������������-ERR�ظ�����ô��ʾ���������İ汾����Redis 2.8����ʶ��
  ��PSYNC����ӷ���������������������SYNC���������������ִ������ͬ
  ��������

*/
 
// ���Խ��в��� resync ���ɹ����� REDIS_OK ��ʧ�ܷ��� REDIS_ERR ��
//�������������ڴӷ��������ӶϿ������У�����������ִ�еĸ���д������ȷ���"+CONTINUE"�ַ������ӷ���������ʾ��ʼ���Ͷ˿��ڼ��д����
int masterTryPartialResynchronization(redisClient *c) {
    long long psync_offset, psync_len;
    char *master_runid = c->argv[1]->ptr;
    char buf[128];
    int buflen;

    /* Is the runid of this master the same advertised by the wannabe slave
     * via PSYNC? If runid changed this master is a different instance and
     * there is no way to continue. */

      /*
    ����������ID: ���˸���ƫ�����͸��ƻ�ѹ������֮�⣬ʵ�ֲ�����ͬ������Ҫ�õ�����������ID
    1. ÿ��Redis�����������������������Ǵӷ��񣬶������Լ�������ID��
    2. ����rD�ڷ���������ʱ�Զ����ɣ���40�������ʮ�������ַ���ɣ�����53b9b28df8042fdc9ab5e3fcbbbabffld5dce2b3��
    ���ӷ������������������г��θ���ʱ�����������Ὣ�Լ�������ID���͸��ӷ����������ӷ�������Ὣ�������ID����������
    ���ӷ��������߲���������һ����������ʱ���ӷ���������ǰ���ӵ�������������֮ǰ���������ID:
    1. ����ӷ��������������ID�͵�ǰ���ӵ���������������ID��ͬ����ô˵���ӷ���������֮ǰ���Ƶľ��ǵ�ǰ���ӵ��������������
    �����������Լ�������ִ�в�����ͬ��������
    2. �෴�أ�����ӷ��������������ID�͵�ǰ���ӵ���������������ID������ͬ����ô˵���ӷ���������֮ǰ���Ƶ���������������
    ��ǰ���ӵ�������������������������Դӷ�����ִ��������ͬ��������

    ���ӷ������������������г��θ���ʱ�����������Ὣ�Լ�������ID���͸��ӷ����������ӷ�������Ὣ�������ID����������
        ���ӷ��������߲���������һ����������ʱ���ӷ���������ǰ���ӵ�������������֮ǰ���������ID:

    �ٸ����ӣ�����ӷ�����ԭ�����ڸ���һ������IDΪ53b9b28df8042fdc9ab5e3fcbbbabf fld5dce2b3��������������ô������Ͽ����ӷ�����
������������������֮�󣬴ӷ��������������������������ID���������������Լ�������ID�Ƿ�53b9b28df8042fdc9ab5e3fcbbbabffld5dce2b3��
�ж���ִ�в�����ͬ������ִ��������ͬ����
     */
    // ��� master id �Ƿ�� runid һ�£�ֻ��һ�µ�����²��� PSYNC �Ŀ���
    if (strcasecmp(master_runid, server.runid)) {
        /* Run id "?" is used by slaves that want to force a full resync. */
        // �ӷ������ṩ�� run id �ͷ������� run id ��һ��
        if (master_runid[0] != '?') {
            redisLog(REDIS_NOTICE,"Partial resynchronization not accepted: "
                "Runid mismatch (Client asked for runid '%s', my runid is '%s')",
                master_runid, server.runid);
        // �ӷ������ṩ�� run id Ϊ '?' ����ʾǿ�� FULL RESYNC
        } else {
            redisLog(REDIS_NOTICE,"Full resync requested by slave.");
        }
        // ��Ҫ full resync
        goto need_full_resync;
    }

    /* We still have the data our slave is asking for? */
    // ȡ�� psync_offset ����
    if (getLongLongFromObjectOrReply(c,c->argv[2],&psync_offset,NULL) !=
       REDIS_OK) goto need_full_resync;

        // ���û�� backlog
    if (!server.repl_backlog ||
        // ���� psync_offset С�� server.repl_backlog_off
        // ����Ҫ�ָ����ǲ��������Ѿ������ǣ�
        psync_offset < server.repl_backlog_off ||
        // psync offset ���� backlog ����������ݵ�ƫ����
        psync_offset > (server.repl_backlog_off + server.repl_backlog_histlen))  //��ѹ�����������ݲ�ȫ��Ҫ��ӽ���ȥ��ͬ��
    {
        // ִ�� FULL RESYNC
        redisLog(REDIS_NOTICE,
            "Unable to partial resync with the slave for lack of backlog (Slave request was: %lld).", psync_offset);
        if (psync_offset > server.master_repl_offset) {
            redisLog(REDIS_WARNING,
                "Warning: slave tried to PSYNC with an offset that is greater than the master replication offset.");
        }
        goto need_full_resync;
    }

    /* If we reached this point, we are able to perform a partial resync:
     * �������е����˵������ִ�� partial resync
     *
     * 1) Set client state to make it a slave.
     *    ���ͻ���״̬��Ϊ salve  
     *
     * 2) Inform the client we can continue with +CONTINUE
     *    �� slave ���� +CONTINUE ����ʾ partial resync �����󱻽���
     *
     * 3) Send the backlog data (from the offset to the end) to the slave. 
     *    ���� backlog �У��ͻ�������Ҫ������
     */
    c->flags |= REDIS_SLAVE;
    c->replstate = REDIS_REPL_ONLINE;
    c->repl_ack_time = server.unixtime;
    listAddNodeTail(server.slaves,c);
    /* We can't use the connection buffers since they are used to accumulate
     * new commands at this stage. But we are sure the socket send buffer is
     * emtpy so this write will never fail actually. */
    // ��ӷ���������һ��ͬ�� +CONTINUE ����ʾ PSYNC ����ִ��
    buflen = snprintf(buf,sizeof(buf),"+CONTINUE\r\n");
    if (write(c->fd,buf,buflen) != buflen) {
        freeClientAsync(c);
        return REDIS_OK;
    }
    // ���� backlog �е����ݣ�Ҳ���Ǵӷ�����ȱʧ����Щ���ݣ����ӷ�����
    psync_len = addReplyReplicationBacklog(c,psync_offset);
    redisLog(REDIS_NOTICE,
        "Partial resynchronization request accepted. Sending %lld bytes of backlog starting from offset %lld.", psync_len, psync_offset);
    /* Note that we don't need to set the selected DB at server.slaveseldb
     * to -1 to force the master to emit SELECT, since the slave already
     * has this state from the previous connection with the master. */

    // ˢ�µ��ӳٴӷ�����������
    refreshGoodSlavesCount();
    return REDIS_OK; /* The caller can return, no full resync needed. */

need_full_resync:
    /* We need a full resync for some reason... notify the client. */
    // ˢ�� psync_offset
    psync_offset = server.master_repl_offset;
    /* Add 1 to psync_offset if it the replication backlog does not exists
     * as when it will be created later we'll increment the offset by one. */
    // ˢ�� psync_offset
    if (server.repl_backlog == NULL) psync_offset++;
    /* Again, we can't use the connection buffers (see above). */
    // ���� +FULLRESYNC ����ʾ��Ҫ������ͬ��
    buflen = snprintf(buf,sizeof(buf),"+FULLRESYNC %s %lld\r\n",
                      server.runid,psync_offset);
    if (write(c->fd,buf,buflen) != buflen) {
        freeClientAsync(c);
        return REDIS_OK;
    }
    return REDIS_ERR; //��ʾ��Ҫ��ͬ��
}

/*
PSYNC����ĵ��÷��������֣�
 1. ����ӷ�������ǰû�и��ƹ��κ���������������֮ǰִ�й�SLAVEOF no one
   �����ô�ӷ������ڿ�ʼһ���µĸ���ʱ����������������PSYNC?-1���
   ��������������������������ͬ������Ϊ��ʱ������ִ�в�����ͬ������
   ���෴�أ�����ӷ������Ѿ����ƹ�ĳ��������������ô�ӷ������ڿ�ʼһ���µĸ�
   ��ʱ����������������PSYNC <runid> <offset>�������runid����һ��
   ���Ƶ���������������ID����offset���Ǵӷ�������ǰ�ĸ���ƫ���������յ���
   �����������������ͨ���������������ж�Ӧ�öԴӷ�����ִ������ͬ��������
 ������������յ�PSYNC�����������������ӷ����������������ֻظ�������һ�֣�
 2. ���������������+FULLRESYNC  <runid>  <offset>�ظ�����ô��ʾ������
   ������ӷ�����ִ��������ͬ������������runid�������������������ID����
   �������Ὣ���ID��������������һ�η���PSYNC����ʱʹ�ã���offset����
   ����������ǰ�ĸ���ƫ�������ӷ������Ὣ���ֵ��Ϊ�Լ��ĳ�ʼ��ƫ������
3. ���������������+CONTINUE�ظ�����ô��ʾ������������ӷ�����ִ�в�����
  ͬ���������ӷ�����ֻҪ���������������Լ�ȱ�ٵ��ǲ������ݷ��͹����Ϳ����ˡ�
4. ���������������-ERR�ظ�����ô��ʾ���������İ汾����Redis 2.8����ʶ��
  ��PSYNC����ӷ���������������������SYNC���������������ִ������ͬ
  ��������

*/

/* SYNC ad PSYNC command implemenation. */
//ע����SYNC��PSYNC֮ǰ�м��ν�������slaveTryPartialResynchronization

//������ִ��ip+port�󣬻����replicationSetMaster���Ѹø÷�������������дӷ��������ӶϿ����������Ա�֤��ͬʱҪ�ͷ�֮ǰsalveof���õľɵ�ip+port�˿�
void syncCommand(redisClient *c) { //sync��psync�ص����Ǹýӿ�

    /* ignore SYNC if already slave or in monitor mode */
    //˵��֮ǰ�Ѿ�ͬ������������ȫ��ͬ���ˣ����ǲ�������ʽ���ͶϿ����Ӻ������������²���������
    // �Ѿ��� SLAVE �����ߴ��� MONITOR ģʽ������
    if (c->flags & REDIS_SLAVE) return; 

    /* Refuse SYNC requests if we are a slave but the link with our master
     * is not ok... */
    // �������һ���ӷ���������������������������δ��������ô�ܾ� SYNC  ���籾������ִ����slaveof ip port,���Ǹ�ip port�ķ�����û��������Ҳ�������Ӳ��ϣ���ô���������;ܾ����ܴӷ�������syn psyn����
    if (server.masterhost && server.repl_state != REDIS_REPL_CONNECTED) {
        addReplyError(c,"Can't SYNC while not connected with my master");
        return;
    }

    /* SYNC can't be issued when the server has pending data to send to
     * the client about already issued commands. We need a fresh reply
     * buffer registering the differences between the BGSAVE and the current
     * dataset, so that we can copy to other slaves if needed. */
    // �ڿͻ�������������ݵȴ���������� SYNC
    if (listLength(c->reply) != 0 || c->bufpos != 0) {
        addReplyError(c,"SYNC and PSYNC are invalid with pending output");
        return;
    }

    redisLog(REDIS_NOTICE,"Slave asks for synchronization");

    /* Try a partial resynchronization if this is a PSYNC command.
     * �������һ�� PSYNC �����ô���� partial resynchronization ��
     *
     * If it fails, we continue with usual full resynchronization, however
     * when this happens masterTryPartialResynchronization() already
     * replied with:
     *
     * ���ʧ�ܣ���ôʹ�� full resynchronization ��
     * ����������£� masterTryPartialResynchronization() �����������ݣ�
     *
     * +FULLRESYNC <runid> <offset>
     *
     * So the slave knows the new runid and offset to try a PSYNC later
     * if the connection with the master is lost. 
     *
     * �����Ļ���֮��������������Ͽ�����ô�ӷ������Ϳ��Գ��� PSYNC �ˡ�
     */
    if (!strcasecmp(c->argv[0]->ptr,"psync")) {
        // ���Խ��� PSYNC
        if (masterTryPartialResynchronization(c) == REDIS_OK) { //����REDIS_ERR��ʾ��Ҫ��ͬ��
            // ��ִ�� PSYNC    ��������ͬ�����ѻ�ѹ���������ݷ��͹�ȥ����
            server.stat_sync_partial_ok++;
            return; /* No full resync needed, return. */
        } else {
            // ����ִ�� PSYNC   ��Ҫ��������ͬ��
            char *master_runid = c->argv[1]->ptr;
            
            /* Increment stats for failed PSYNCs, but only if the
             * runid is not "?", as this is used by slaves to force a full
             * resync on purpose when they are not albe to partially
             * resync. */ //�����ͻ���������������ʽͬ��������ȷ����������(�����ѹ������д��ѭ�������˵�)����ʵ���Ͻ���������ȫ��ͬ��
            if (master_runid[0] != '?') server.stat_sync_partial_err++;
        }
    } else {
        /* If a slave uses SYNC, we are dealing with an old implementation
         * of the replication protocol (like redis-cli --slave). Flag the client
         * so that we don't expect to receive REPLCONF ACK feedbacks. */
        // �ɰ�ʵ�֣����ñ�ʶ��������� REPLCONF ACK 
        c->flags |= REDIS_PRE_PSYNC;
    }

    // ������������ͬ�������������

    /* Full resynchronization. */
    // ִ�� full resynchronization �����Ӽ���
    server.stat_sync_full++;

    /* Here we need to check if there is a background saving operation
     * in progress, or if it is required to start one */
    // ����Ƿ��� BGSAVE ��ִ��
    if (server.rdb_child_pid != -1) {
        /* Ok a background save is in progress. Let's check if it is a good
         * one for replication, i.e. if there is another slave that is
         * registering differences since the server forked to save */
        redisClient *slave;
        listNode *ln;
        listIter li;

        // ���������һ�� slave �ڵȴ���� BGSAVE ���
        // ��ô˵�����ڽ��е� BGSAVE �������� RDB Ҳ����Ϊ���� slave ����

        //�����ж���ӷ���������ֻ��ҪΪһ���ӷ���������RDB�ļ�����
        listRewind(server.slaves,&li);
        while((ln = listNext(&li))) {
            slave = ln->value;
            if (slave->replstate == REDIS_REPL_WAIT_BGSAVE_END) //ֻҪ��Ϊһ���ӷ�����������BGSAVE���˳�����ʱ��ln��ΪNULL
                break;
        }

        if (ln) { 
        //�ڷ��͸���������sync����ǰ���Ѿ��з��������ڽ���RDB�ļ���д�ˣ���ô���ǾͿ�������Ϊ֮ǰ��һ�����Ŀͻ��˶����е�RDB�ļ��������rdb�ļ�д��ɺ󣬽���ͬ��
        //�ڱ�slave֮ǰ����rdb��д�ڸ�if (server.rdb_child_pid != -1) {} else {}�߼�����

            /* Perfect, the server is already registering differences for
             * another slave. Set the right state, and copy the buffer. */
            // ���˵����������ʹ��Ŀǰ BGSAVE �����ɵ� RDB
            copyClientOutputBuffer(c,slave);
            c->replstate = REDIS_REPL_WAIT_BGSAVE_END;
            redisLog(REDIS_NOTICE,"Waiting for end of BGSAVE for SYNC");
        } else {

        //��Ϊ��Ȼ��RDB�ļ���д��ɣ����ǲ�����Ϊ
        //slave������ȫ��ͬ�������rdb��д��������Ϊ����bgsave���������������save time count������bgsave�������������rdb��д
            /* No way, we need to wait for the next BGSAVE in order to
             * register differences */
            // �����˵����������ȴ��¸� BGSAVE     ����updateSlavesWaitingBgsave����rdb��д
            //���updateSlavesWaitingBgsave�Ķ����������״̬��slave����master��ҪΪ�����RDB��д����Ϊ���ǵ�һ����������slave,����ڽ���rdb��д������������slave������������Ҳ����ֱ�����ø�RDB
            c->replstate = REDIS_REPL_WAIT_BGSAVE_START;  //��ʾ����ڵ��ǵ�һ���������ı���������������Ҫ��rdb
            redisLog(REDIS_NOTICE,"Waiting for next BGSAVE for SYNC");
        }
    } else { 
        
        /* Ok we don't have a BGSAVE in progress, let's start one */
        // û�� BGSAVE �ڽ��У���ʼһ���µ� BGSAVE
        redisLog(REDIS_NOTICE,"Starting BGSAVE for SYNC");
        if (rdbSaveBackground(server.rdb_filename) != REDIS_OK) {
            redisLog(REDIS_NOTICE,"Replication failed, can't BGSAVE");
            addReplyError(c,"Unable to perform background save");
            return;
        }
        // ����״̬  //��ʾ�Ѿ������ӽ���������bgsave�ˣ��ȴ�д�ɹ�������
        c->replstate = REDIS_REPL_WAIT_BGSAVE_END;
        /* Flush the script cache for the new slave. */
        // ��Ϊ�� slave ���룬ˢ�¸��ƽű�����
        replicationScriptCacheFlush();
    }

    if (server.repl_disable_tcp_nodelay)
        anetDisableTcpNoDelay(NULL, c->fd); /* Non critical if it fails. */

    c->repldbfd = -1;

    c->flags |= REDIS_SLAVE;

    server.slaveseldb = -1; /* Force to re-emit the SELECT command. */

    // ��ӵ� slave �б���
    listAddNodeTail(server.slaves,c);
    // ����ǵ�һ�� slave ����ô��ʼ�� backlog
    if (listLength(server.slaves) == 1 && server.repl_backlog == NULL)
        createReplicationBacklog();
    return;
}

/* REPLCONF <option> <value> <option> <value> ...
 * This command is used by a slave in order to configure the replication
 * process before starting it with the SYNC command.
 *
 * �� slave ʹ�ã��� SYNC ֮ǰ���ø��ƽ��̣�process��
 *
 * Currently the only use of this command is to communicate to the master
 * what is the listening port of the Slave redis instance, so that the
 * master can accurately list slaves and their listening ports in
 * the INFO output.
 *
 * Ŀǰ���������Ψһ���þ��ǣ��� slave ���� master �����ڼ����Ķ˿ں�
 * Ȼ�� master �Ϳ����� INFO ���������д�ӡ��������ˡ�
 *
 * In the future the same command can be used in order to configure
 * the replication to initiate an incremental replication instead of a
 * full resync. 
 *
 * �������ܻ������������ʵ������ʽ���ƣ�ȡ�� full resync ��
 */ //�ӷ���������REPLCONF xxx, syncWithMaster����
void replconfCommand(redisClient *c) {
    int j;

    if ((c->argc % 2) == 0) {
        /* Number of arguments must be odd to make sure that every
         * option has a corresponding value. */
        addReply(c,shared.syntaxerr);
        return;
    }

    /* Process every option-value pair. */
    for (j = 1; j < c->argc; j+=2) {

        // �ӷ��������� REPLCONF listening-port <port> ����
        // �����������ӷ����������Ķ˿ںż�¼����
        // Ҳ���� INFO replication �е� slaveN ..., port = xxx ��һ��
        if (!strcasecmp(c->argv[j]->ptr,"listening-port")) {
            long port;

            if ((getLongFromObjectOrReply(c,c->argv[j+1],
                    &port,NULL) != REDIS_OK))
                return;
            c->slave_listening_port = port;

        // �ӷ��������� REPLCONF ACK <offset> ����
        // ��֪�����������ӷ������Ѵ���ĸ�������ƫ����
        } else if (!strcasecmp(c->argv[j]->ptr,"ack")) { //����ͨ��info replication����鿴�ӷ�����ack���һ�η��͹��������ڶ����

        /*
            �������
          ��������׶Σ��ӷ�����Ĭ�ϻ���ÿ��һ�ε�Ƶ�ʣ������������������ REPLCONF ACK <replication��offset>
          ����replication��offset�Ǵӷ�������ǰ�ĸ���ƫ������ ����REPLCONFACK����������ӷ��������������ã�
          ������ӷ���������������״̬��    ����ʵ��min-slaveѡ�������ʧ��


          ������ʧ
      �����Ϊ������ϣ����������������ӷ�������д�����ڰ�·��ʧ����ô���ӷ�������������������REPLCONF ACK����ʱ������������������
  ��������ǰ�ĸ���ƫ���������Լ��ĸ���ƫ������Ȼ�����������ͻ���ݴӷ������ύ�ĸ���ƫ�������ڸ��ƻ�ѹ�����������ҵ��ӷ�����ȱ�ٵ�
  ���ݣ�������Щ�������·��͸��ӷ�������
      �ٸ����ӣ���������������һ��״̬�����ӷ����������ǵĸ���ƫ��������200�������ʱ��������ִ��������SET key value��Э���ʽ�ĳ���Ϊ33�ֽڣ���
 ���Լ��ĸ���ƫ�������µ���233����������ӷ�������������SET key value������������ȴ��Ϊ������϶��ڴ�����;�ж�ʧ����ô���ӷ�����֮��ĸ�
 ��ƫ�����ͻ���ֲ�һ�£����������ĸ���ƫ�����ᱻ����Ϊ233�����ӷ������ĸ���ƫ������ȻΪ200
     ����֮�󣬵��ӷ�������������������REPLCONF ACK����ĶԺ��������������ӷ������ĸ���ƫ������ȻΪ200�����Լ��ĸ���ƫ����Ϊ233��
 ��˵�����ƻ�ѹ���������渴��ƫ����Ϊ201��233�����ݣ�Ҳ��������SET key value���ڴ��������ж�ʧ�ˣ����������������ٴ���ӷ�����������
 ��SET key value���ӷ�����ͨ�����ղ�ִ�����������Խ��Լ�����������������ǰ������״̬
    ��������ͨ����ӷ������������������´ӷ�������״̬���������ӷ�����һ�£����ӷ�������ͨ��������������������������������⣬�Լ����ʧ��⡣
          */
        
        
            /* REPLCONF ACK is used by slave to inform the master the amount
             * of replication stream that it processed so far. It is an
             * internal only command that normal clients should never use. */
            // �ӷ�����ʹ�� REPLCONF ACK ��֪����������
            // �ӷ�����Ŀǰ�Ѵ���ĸ�������ƫ����
            // ���������������ļ�¼ֵ
            // Ҳ���� INFO replication �е�  slaveN ..., offset = xxx ��һ��
            long long offset;

            if (!(c->flags & REDIS_SLAVE)) return;
            if ((getLongLongFromObject(c->argv[j+1], &offset) != REDIS_OK))
                return;
            // ��� offset �Ѹı䣬��ô����
            if (offset > c->repl_ack_off)
                c->repl_ack_off = offset;
            // �������һ�η��� ack ��ʱ��
            c->repl_ack_time = server.unixtime;
            /* Note: this command does not reply anything! */
            return;
        } else if (!strcasecmp(c->argv[j]->ptr,"getack")) {
            /* REPLCONF GETACK is used in order to request an ACK ASAP
             * to the slave. */
            if (server.masterhost && server.master) replicationSendAck();
            /* Note: this command does not reply anything! */
        } else {
            addReplyErrorFormat(c,"Unrecognized REPLCONF option: %s",
                (char*)c->argv[j]->ptr);
            return;
        }
    }
    addReply(c,shared.ok);
}

/*
������ִ��slavof ip+port�󣬻����replicationSetMaster���Ѹø÷�������������дӷ��������ӶϿ����������Ա�֤��
ͬʱҪ�ͷ�֮ǰsalveof���õľɵ�ip+port�˿�,�������Ա�֤�����¼�������ʼ�������ϼ���sdb�ļ�Ϊ׼����������redis2��redis3��
�����������������ͬ������ʱ����redis2ִ��slaveof��ʹredis1��Ϊredis2��������������redis2���ȶϿ���redis3�����ӣ�Ȼ����������redis1
����ȡRDBͬ���ļ���Ȼ��redis3�����������»�ȡredis2��rdb�ļ�������ȫ����һ����

�����������ִ����slaveof ip port,���Ǹ�ip port�ķ�����û��������Ҳ�������Ӳ��ϣ���ô���������;ܾ����ܴӷ�������syn psyn����
*/


//rdbSaveBackground������ȫ��д��rdb�ļ��У�Ȼ����serverCron->backgroundSaveDoneHandler->updateSlavesWaitingBgsave��ͬ��rdb�ļ����ݵ��ӷ�����

//RDB�ļ����͸�ʽ����ͨ�����ַ���Э���ʽһ����$length\r\n+ʵ�����ݣ�rdb�ļ����ݷ�����updateSlavesWaitingBgsave->sendBulkToSlave��
//rdb��������ͬ��(������ʽ)���ݽ��պ���ΪreadSyncBulkPayload����������(������������ʽ����ͬ��)���ս�����processMultibulkBuffer

// master �� RDB �ļ����͸� slave ��д�¼�������  //�ļ��¼�aeCreateFileEvent   ʱ���¼�aeCreateTimeEvent  aeProcessEvents��ִ���ļ���ʱ���¼�
void sendBulkToSlave(aeEventLoop *el, int fd, void *privdata, int mask) { //�ú���ִ����aeProcessEvents
    redisClient *slave = privdata;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);
    char buf[REDIS_IOBUF_LEN]; 
    //ͨ���޸�REDIS_IOBUF_LENΪ20���������ε��ú��������aeDeleteFileEvent���ԣ�write���������ݺ�
    //�´�epoll_wait���ǻ�ú���ִ�У�Ҳ���������ɾ����event�����ഥ��һ��write�¼�����Ҳ��Ϊʲô������Էֿ鴫�䣬ÿ�δ���16K��Ϻ��´λ�����
    //ִ�е��ú�����������ȡ16Kִ��
    ssize_t nwritten, buflen;

    /* Before sending the RDB file, we send the preamble as configured by the
     * replication process. Currently the preamble is just the bulk count of
     * the file in the form "$<length>\r\n". */
    if (slave->replpreamble) { //�ȷ���$<length>\r\n,��ʾ����rdb�ļ��ж�������ӷ�������֪��ʲôʱ�����
        nwritten = write(fd,slave->replpreamble,sdslen(slave->replpreamble));
        if (nwritten == -1) {
            redisLog(REDIS_VERBOSE,"Write error sending RDB preamble to slave: %s",
                strerror(errno));
            freeClient(slave, NGX_FUNC_LINE);
            return;
        }
        sdsrange(slave->replpreamble,nwritten,-1);
        if (sdslen(slave->replpreamble) == 0) {
            sdsfree(slave->replpreamble);
            slave->replpreamble = NULL;
            /* fall through sending data. */
        } else {
            return;
        }
    }

    /* If the preamble was already transfered, send the RDB bulk data. */
    lseek(slave->repldbfd,slave->repldboff,SEEK_SET);
    // ��ȡ RDB ����
    buflen = read(slave->repldbfd,buf,REDIS_IOBUF_LEN);
    
    if (buflen <= 0) {
        redisLog(REDIS_WARNING,"Read error sending DB to slave: %s",
            (buflen == 0) ? "premature EOF" : strerror(errno));
        freeClient(slave, NGX_FUNC_LINE);
        return;
    }
    // д�����ݵ� slave
    if ((nwritten = write(fd,buf,buflen)) == -1) {
        if (errno != EAGAIN) {
            redisLog(REDIS_WARNING,"Write error sending DB to slave: %s",
                strerror(errno));
            freeClient(slave, NGX_FUNC_LINE);
        }
        return;
    }

    

    // ���д��ɹ�����ô����д���ֽ����� repldboff ���ȴ��´μ���д��
    slave->repldboff += nwritten;

    // ���д���Ѿ����
    if (slave->repldboff == slave->repldbsize) {
        
        // �ر� RDB �ļ�������
        close(slave->repldbfd);
        slave->repldbfd = -1;
        // ɾ��֮ǰ�󶨵�д�¼�������
        aeDeleteFileEvent(server.el,slave->fd,AE_WRITABLE);
        // ��״̬����Ϊ REDIS_REPL_ONLINE
        slave->replstate = REDIS_REPL_ONLINE;
        // ������Ӧʱ��
        slave->repl_ack_time = server.unixtime;
        // ������ӷ��������������д�¼�������
        // �����沢���� RDB �ڼ�Ļظ�ȫ�����͸��ӷ�����
        if (aeCreateFileEvent(server.el, slave->fd, AE_WRITABLE,
            sendReplyToClient, slave) == AE_ERR) {
            redisLog(REDIS_WARNING,"Unable to register writable event for slave bulk transfer: %s", strerror(errno));
            freeClient(slave, NGX_FUNC_LINE);
            return;
        }
        // ˢ�µ��ӳ� slave ����
        refreshGoodSlavesCount();
        redisLog(REDIS_NOTICE,"Synchronization with slave succeeded");
    }
}

/* This function is called at the end of every background saving.
 * ��ÿ�� BGSAVE ִ�����֮��ʹ��
 *
 * The argument bgsaveerr is REDIS_OK if the background saving succeeded
 * otherwise REDIS_ERR is passed to the function.
 * bgsaveerr ������ REDIS_OK ���� REDIS_ERR ����ʾ BGSAVE ��ִ�н��
 *
 * The goal of this function is to handle slaves waiting for a successful
 * background saving in order to perform non-blocking synchronization. 
 * 
 * ����������� BGSAVE ���֮����첽�ص�������
 * ��ָ������ôִ�к� slave ��ص� RDB ��һ��������
 */ 

/*
������ִ��slavof ip+port�󣬻����replicationSetMaster���Ѹø÷�������������дӷ��������ӶϿ����������Ա�֤��
ͬʱҪ�ͷ�֮ǰsalveof���õľɵ�ip+port�˿�,�������Ա�֤�����¼�������ʼ�������ϼ���sdb�ļ�Ϊ׼����������redis2��redis3��
�����������������ͬ������ʱ����redis2ִ��slaveof��ʹredis1��Ϊredis2��������������redis2���ȶϿ���redis3�����ӣ�Ȼ����������redis1
����ȡRDBͬ���ļ���Ȼ��redis3�����������»�ȡredis2��rdb�ļ�������ȫ����һ����

�����������ִ����slaveof ip port,���Ǹ�ip port�ķ�����û��������Ҳ�������Ӳ��ϣ���ô���������;ܾ����ܴӷ�������syn psyn����
*/

 
//rdbSaveBackground������ȫ��д��rdb�ļ��У�Ȼ����serverCron->backgroundSaveDoneHandler->updateSlavesWaitingBgsave��ͬ��rdb�ļ����ݵ��ӷ�����

//RDB�ļ����͸�ʽ����ͨ�����ַ���Э���ʽһ����$length\r\n+ʵ�����ݣ�rdb�ļ����ݷ�����updateSlavesWaitingBgsave->sendBulkToSlave��
//rdb��������ͬ��(������ʽ)���ݽ��պ���ΪreadSyncBulkPayload����������(������������ʽ����ͬ��)���ս�����processMultibulkBuffer

void updateSlavesWaitingBgsave(int bgsaveerr) { //��rdb�ļ����ݴ��͵�slave
    listNode *ln;
    int startbgsave = 0;
    listIter li;

    // �������� slave
    listRewind(server.slaves,&li);
    while((ln = listNext(&li))) { //li�ϱ�����Ǳ�master�����дӷ�����
        redisClient *slave = ln->value;

        if (slave->replstate == REDIS_REPL_WAIT_BGSAVE_START) { //���syncCommand�Ķ���˵����Ҫ����RDB�ļ���д����Ϊ��Ȼ��RDB�ļ���д��ɣ����ǲ�����Ϊ
        //slave������ȫ��ͬ�������rdb��д��������Ϊ����bgsave���������������save time count������bgsave�������������rdb��д
            // ֮ǰ�� RDB �ļ����ܱ� slave ʹ�ã�
            // ��ʼ�µ� BGSAVE
            startbgsave = 1;
            slave->replstate = REDIS_REPL_WAIT_BGSAVE_END;
        } else if (slave->replstate == REDIS_REPL_WAIT_BGSAVE_END) { 
        
        //��slave��Ӧ��rdb�ļ���д��ɣ����߱�slave���ӵ�master��ʱ�򣬸պ�������slave������slave��д����Ͳ����ڽ���rdb��д��ֱ�����øղŽ���rdb��д��rdb�ļ�����

            // ִ�е����˵���� slave �ڵȴ� BGSAVE ���

            struct redis_stat buf;

            // ���� BGSAVE ִ�д���
            if (bgsaveerr != REDIS_OK) {
                // �ͷ� slave
                freeClient(slave, NGX_FUNC_LINE);
                redisLog(REDIS_WARNING,"SYNC failed. BGSAVE child returned an error");
                continue;
            }

            // �� RDB �ļ�
            if ((slave->repldbfd = open(server.rdb_filename,O_RDONLY)) == -1 ||
                redis_fstat(slave->repldbfd,&buf) == -1) {
                freeClient(slave, NGX_FUNC_LINE);
                redisLog(REDIS_WARNING,"SYNC failed. Can't open/stat DB after BGSAVE: %s", strerror(errno));
                continue;
            }

            // ����ƫ����������ֵ
            slave->repldboff = 0;
            slave->repldbsize = buf.st_size;
            // ����״̬
            slave->replstate = REDIS_REPL_SEND_BULK;

            //RDB�ļ���$�ļ��ֽ�����ʼ�������ӷ�������֪�����rdb�ļ������ж�󣬾���ʲôʱ������ȡ���
            slave->replpreamble = sdscatprintf(sdsempty(),"$%lld\r\n",
                (unsigned long long) slave->repldbsize);

            // ���֮ǰ��д�¼�������
            aeDeleteFileEvent(server.el,slave->fd,AE_WRITABLE);
            // �� sendBulkToSlave ��װΪ slave ��д�¼�������
            // �����ڽ� RDB �ļ����͸� slave
            if (aeCreateFileEvent(server.el, slave->fd, AE_WRITABLE, sendBulkToSlave, slave) == AE_ERR) {
                freeClient(slave, NGX_FUNC_LINE);
                continue;
            }
        }
    }

    // ��Ҫִ���µ� BGSAVE
    if (startbgsave) {
        /* Since we are starting a new background save for one or more slaves,
         * we flush the Replication Script Cache to use EVAL to propagate every
         * new EVALSHA for the first time, since all the new slaves don't know
         * about previous scripts. */
        // ��ʼ�е� BGSAVE ������սű�����
        replicationScriptCacheFlush();
        if (rdbSaveBackground(server.rdb_filename) != REDIS_OK) {
            listIter li;

            listRewind(server.slaves,&li);
            redisLog(REDIS_WARNING,"SYNC failed. BGSAVE failed");
            while((ln = listNext(&li))) {
                redisClient *slave = ln->value;

                if (slave->replstate == REDIS_REPL_WAIT_BGSAVE_START)
                    freeClient(slave, NGX_FUNC_LINE);
            }
        }
    }
}

/* ----------------------------------- SLAVE -------------------------------- */

//����ͬ����ר�Ŵ���һ��repl_transfer_s�׽���(connectWithMaster)����������ͬ����ͬ����ɺ���replicationAbortSyncTransfer�йرո��׽���

/* Abort the async download of the bulk dataset while SYNC-ing with master */
// ֹͣ���� RDB �ļ�
void replicationAbortSyncTransfer(void) {
    redisAssert(server.repl_state == REDIS_REPL_TRANSFER);

    aeDeleteFileEvent(server.el,server.repl_transfer_s,AE_READABLE);
    close(server.repl_transfer_s);
    close(server.repl_transfer_fd);
    unlink(server.repl_transfer_tmpfile);
    zfree(server.repl_transfer_tmpfile);
    server.repl_state = REDIS_REPL_CONNECT;
}

/* Avoid the master to detect the slave is timing out while loading the
 * RDB file in initial synchronization. We send a single newline character
 * that is valid protocol but is guaranteed to either be sent entierly or
 * not, since the byte is indivisible.
 *
 * The function is called in two contexts: while we flush the current
 * data with emptyDb(), and while we load the new data received as an
 * RDB file from the master. */
/* �ӿͻ��˷��Ϳ��и����ͻ��ˣ��ƻ���ԭ����Э���ʽ�����������ͻ��˼����ӿͻ��˳�ʱ����� */
//���͸����е�����������������������������Ϊ�ӷ�������ʱ��processInlineBuffer�յ���\n�ַ��������repl_ack_time
void replicationSendNewlineToMaster(void) {
    static time_t newline_sent;
    if (time(NULL) != newline_sent) {
        newline_sent = time(NULL);
        if (write(server.repl_transfer_s,"\n",1) == -1) {
            /* Pinging back in this stage is best-effort. */
        }
    }
}

/* Callback used by emptyDb() while flushing away old data to load
 * the new dataset received by the master. */

void replicationEmptyDbCallback(void *privdata) {
    REDIS_NOTUSED(privdata);
    replicationSendNewlineToMaster();
}

/* Asynchronously read the SYNC payload we receive from a master */
// �첽 RDB �ļ���ȡ����
#define REPL_MAX_WRITTEN_BEFORE_FSYNC (1024*1024*8) /* 8 MB */

/*
������ִ��slavof ip+port�󣬻����replicationSetMaster���Ѹø÷�������������дӷ��������ӶϿ����������Ա�֤��
ͬʱҪ�ͷ�֮ǰsalveof���õľɵ�ip+port�˿�,�������Ա�֤�����¼�������ʼ�������ϼ���sdb�ļ�Ϊ׼����������redis2��redis3��
�����������������ͬ������ʱ����redis2ִ��slaveof��ʹredis1��Ϊredis2��������������redis2���ȶϿ���redis3�����ӣ�Ȼ����������redis1
����ȡRDBͬ���ļ���Ȼ��redis3�����������»�ȡredis2��rdb�ļ�������ȫ����һ����

�����������ִ����slaveof ip port,���Ǹ�ip port�ķ�����û��������Ҳ�������Ӳ��ϣ���ô���������;ܾ����ܴӷ�������syn psyn����
*/



//rdbSaveBackground������ȫ��д��rdb�ļ��У�Ȼ����serverCron->backgroundSaveDoneHandler->updateSlavesWaitingBgsave��ͬ��rdb�ļ����ݵ��ӷ�����

//RDB�ļ����͸�ʽ����ͨ�����ַ���Э���ʽһ����$length\r\n+ʵ�����ݣ�rdb�ļ����ݷ�����updateSlavesWaitingBgsave->sendBulkToSlave��
//rdb��������ͬ��(������ʽ)���ݽ��պ���ΪreadSyncBulkPayload����������(������������ʽ����ͬ��)���ս�����processMultibulkBuffer

void readSyncBulkPayload(aeEventLoop *el, int fd, void *privdata, int mask) {
    char buf[4096];
    ssize_t nread, readlen;
    off_t left;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(privdata);
    REDIS_NOTUSED(mask);

    /* If repl_transfer_size == -1 we still have to read the bulk length
     * from the master reply. */
    // ��ȡ RDB �ļ��Ĵ�С
    if (server.repl_transfer_size == -1) {

        // ���ö�����
        if (syncReadLine(fd,buf,1024,server.repl_syncio_timeout*1000) == -1) {
            redisLog(REDIS_WARNING,
                "I/O error reading bulk count from MASTER: %s",
                strerror(errno));
            goto error;
        }

        // ����
        if (buf[0] == '-') {
            redisLog(REDIS_WARNING,
                "MASTER aborted replication with an error: %s",
                buf+1);
            goto error;
        } else if (buf[0] == '\0') {
            /* At this stage just a newline works as a PING in order to take
             * the connection live. So we refresh our last interaction
             * timestamp. */
            // ֻ�ӵ���һ�����ú� PING һ���� '\0'
            // ������󻥶�ʱ��
            server.repl_transfer_lastio = server.unixtime;
            return;
        } else if (buf[0] != '$') { //��ʾ����rdb���ݿ��ļ���С      rdb�ļ���ʽ: $length\r\n+ʵ������
            // ��������ݳ�����Э���ʽ����
            redisLog(REDIS_WARNING,"Bad protocol from MASTER, the first byte is not '$' (we received '%s'), are you sure the host and port are right?", buf);
            goto error;
        }

        // ���� RDB �ļ���С
        server.repl_transfer_size = strtol(buf+1,NULL,10);

        redisLog(REDIS_NOTICE,
            "MASTER <-> SLAVE sync: receiving %lld bytes from master",
            (long long) server.repl_transfer_size);
        return;
    }

    /* Read bulk data */
    // ������

    // ���ж����ֽ�Ҫ����
    left = server.repl_transfer_size - server.repl_transfer_read;
    readlen = (left < (signed)sizeof(buf)) ? left : (signed)sizeof(buf);
    // ��ȡ
    nread = read(fd,buf,readlen);
    if (nread <= 0) {
        redisLog(REDIS_WARNING,"I/O error trying to sync with MASTER: %s",
            (nread == -1) ? strerror(errno) : "connection lost");
        replicationAbortSyncTransfer();
        return;
    }
    // ������� RDB ������ IO ʱ��
    server.repl_transfer_lastio = server.unixtime;
    if (write(server.repl_transfer_fd,buf,nread) != nread) { 
    //wirte��bufֻ�ǵ����ļ��ں˻�������û������д����̣���˻���һֱռ�����ڴ棬����rdb_fsync_range����������ļ��ں˻������е�����д�����
        redisLog(REDIS_WARNING,"Write error or short write writing to the DB dump file needed for MASTER <-> SLAVE synchronization: %s", strerror(errno));
        goto error;
    }
    // ���ϸն�ȡ�õ��ֽ���
    server.repl_transfer_read += nread;

    /* Sync data on disk from time to time, otherwise at the end of the transfer
     * we may suffer a big delay as the memory buffers are copied into the
     * actual disk. */
    // ���ڽ�������ļ� fsync �����̣����� buffer ̫�࣬һ����д��ʱ�ű� IO
    if (server.repl_transfer_read >=
        server.repl_transfer_last_fsync_off + REPL_MAX_WRITTEN_BEFORE_FSYNC)
    {
        off_t sync_size = server.repl_transfer_read -
                          server.repl_transfer_last_fsync_off;
        rdb_fsync_range(server.repl_transfer_fd,
            server.repl_transfer_last_fsync_off, sync_size);
        server.repl_transfer_last_fsync_off += sync_size;
    }

    /* Check if the transfer is now complete */
    // ��� RDB �Ƿ��Ѿ��������
    if (server.repl_transfer_read == server.repl_transfer_size) {
        // ��ϣ�����ʱ�ļ�����Ϊ dump.rdb
        if (rename(server.repl_transfer_tmpfile,server.rdb_filename) == -1) {
            redisLog(REDIS_WARNING,"Failed trying to rename the temp DB into dump.rdb in MASTER <-> SLAVE synchronization: %s", strerror(errno));
            replicationAbortSyncTransfer();
            return;
        }

        /*
        ??????����������Ⱥ�ڵ�᲻���Ǹýڵ�������????��Ϊ������������������Ҫ���Ĵ���ʱ�䣬������Ⱥ��PING--PONGʱ���� 
        �������emptyDb��rdbLoad���ʱ���������Ϊ���ʱ���ǲ��ᴦ��clusterCron��ʱ����ģ���������ڵ��Ѹ�slave���Ϊpfail����
        fail��������û��ϵ����rdbLoadִ����󣬸ýڵ����ִ�ж�ʱ���򣬷���ping�������ڵ��յ��󣬻���°Ѹýڵ���ΪONline.
        */
        
        // ����վ����ݿ�
        redisLog(REDIS_NOTICE, "MASTER <-> SLAVE sync: Flushing old data");
        signalFlushedDb(-1);  
        
        emptyDb(replicationEmptyDbCallback);//���ڴ��е�����dbid(select id�е�id)���ݿ�key-value��������Ϊ-1����ʾ��˵�����ݿ����
        /* Before loading the DB into memory we need to delete the readable
         * handler, otherwise it will get called recursively since
         * rdbLoad() will call the event loop to process events from time to
         * time for non blocking loading. */
        // ��ɾ�����������Ķ��¼���������Ϊ rdbLoad() ����Ҳ��������¼�
        aeDeleteFileEvent(server.el,server.repl_transfer_s,AE_READABLE);

        // ���� RDB
        if (rdbLoad(server.rdb_filename) != REDIS_OK) {
            redisLog(REDIS_WARNING,"Failed trying to load the MASTER synchronization DB from disk");
            replicationAbortSyncTransfer();
            return;
        }

        /* Final setup of the connected slave <- master link */
        // �ر���ʱ�ļ�
        zfree(server.repl_transfer_tmpfile);
        close(server.repl_transfer_fd);

        /*
    �ӷ���������������������PSYNC���ִ��ͬ�������������Լ������ݿ�����������������ݿ⵱ǰ������״̬��
  ֵ��һ����ǣ���ͬ������ִ��֮ǰ��ֻ�дӷ����������������Ŀͻ��ˣ�������ִ��ͬ������֮����������Ҳ���Ϊ�ӷ������Ŀͻ��ˣ�
    1. ���PSYNC����ִ�е���������ͬ����������ô����������Ҫ��Ϊ�ӷ������Ŀͻ��ˣ����ܽ������ڻ����������д����͸��ӷ�����ִ�С�
    2. ���PSYNC����ִ�е��ǲ�����ͬ����������ô����������Ҫ��Ϊ�ӷ������Ŀͻ��ˣ�������ӷ��������ͱ����ڸ��ƻ�ѹ�����������д���
    ��ˣ���ͬ������ִ��֮�����ӷ�����˫�����ǶԷ��Ŀͻ��ˣ����ǿ��Ի�����Է������������󣬻��߻�����Է���������ظ�
    ����Ϊ����������Ϊ�˴ӷ������Ŀͻ��ˣ��������������ſ���ͨ������д�������ı�ӷ������������״̬������ͬ��������Ҫ�õ���һ�㣬
    ��Ҳ�����������Դӷ�����ִ������������Ļ���
     */

     /*
    ����ͬ����ר�Ŵ���һ��repl_transfer_s�׽���(connectWithMaster)����������ͬ����ͬ����ɺ���replicationAbortSyncTransfer�йرո��׽���
    ����ͬ����ɺ���������Ҫ�򱾴ӷ���������ʵʱKV������Ҫһ��ģ���redisClient,��Ϊredis����ͨ��redisClient�е�fd�����տͻ��˷��͵�KV,
    ����ͬ����ɺ��ʱ��KV���������������ͨ����master(redisClient)��fd������������ͨ�ŵ�
    */
        
        // �������������ó�һ�� redis client
        // ע�� createClient ��Ϊ�����������¼���Ϊ��������������������������׼��
        //redis����ͨ��redisClient�ṹ��fd�����նԶ˷��͹�����KV
        server.master = createClient(server.repl_transfer_s);
        // �������ͻ���Ϊ��������
        server.master->flags |= REDIS_MASTER;
        // �����Ϊ����֤���
        server.master->authenticated = 1;
        // ���¸���״̬
        server.repl_state = REDIS_REPL_CONNECTED;
        // �������������ĸ���ƫ����  ��ʾ���ӷ�������Ӧ��offset
        server.master->reploff = server.repl_master_initial_offset;
        // �������������� RUN ID
        memcpy(server.master->replrunid, server.repl_master_runid,
            sizeof(server.repl_master_runid));

        /* If master offset is set to -1, this master is old and is not
         * PSYNC capable, so we flag it accordingly. */
        // ��� offset ������Ϊ -1 ����ô��ʾ���������İ汾���� 2.8 
        // �޷�ʹ�� PSYNC ��������Ҫ������Ӧ�ı�ʶֵ
        if (server.master->reploff == -1)
            server.master->flags |= REDIS_PRE_PSYNC;
        redisLog(REDIS_NOTICE, "MASTER <-> SLAVE sync: Finished with success");

        /* Restart the AOF subsystem now that we finished the sync. This
         * will trigger an AOF rewrite, and when done will start appending
         * to the new file. */
        // ����п��� AOF �־û�����ô���� AOF ���ܣ���ǿ�����������ݿ�� AOF �ļ�
        if (server.aof_state != REDIS_AOF_OFF) {
            int retry = 10;

            // �ر�
            stopAppendOnly();
            // ������
            while (retry-- && startAppendOnly() == REDIS_ERR) {
                redisLog(REDIS_WARNING,"Failed enabling the AOF after successful master synchronization! Trying it again in one second.");
                sleep(1);
            }
            if (!retry) {
                redisLog(REDIS_WARNING,"FATAL: this slave instance finished the synchronization with its master, but the AOF can't be turned on. Exiting now.");
                exit(1);
            }
        }
    }

    return;

error:
    replicationAbortSyncTransfer();
    return;
}

/* Send a synchronous command to the master. Used to send AUTH and
 * REPLCONF commands before starting the replication with SYNC.
 *
 * The command returns an sds string representing the result of the
 * operation. On error the first byte is a "-".
 */
// Redis ͨ��������ǽ�����ķ��ͺͻظ��ò�ͬ���¼����������첽�����
// ��������ͬ���ط���Ȼ���ȡ

//�����ַ���������������ֱ�����������ȴ��Է�Ӧ��
char *sendSynchronousCommand(int fd, ...) {
    va_list ap;
    sds cmd = sdsempty();
    char *arg, buf[256];

    /* Create the command to send to the master, we use simple inline
     * protocol for simplicity as currently we only send simple strings. */
    va_start(ap,fd);
    while(1) {
        arg = va_arg(ap, char*);
        if (arg == NULL) break;

        if (sdslen(cmd) != 0) cmd = sdscatlen(cmd," ",1);
        cmd = sdscat(cmd,arg);
    }
    cmd = sdscatlen(cmd,"\r\n",2);

    /* Transfer command to the server. */
    // ���������������
    if (syncWrite(fd,cmd,sdslen(cmd),server.repl_syncio_timeout*1000) == -1) {
        sdsfree(cmd);
        return sdscatprintf(sdsempty(),"-Writing to master: %s",
                strerror(errno));
    }
    sdsfree(cmd);

    /* Read the reply from the server. */
    // �����������ж�ȡ�ظ�
    if (syncReadLine(fd,buf,sizeof(buf),server.repl_syncio_timeout*1000) == -1)
    {
        return sdscatprintf(sdsempty(),"-Reading from master: %s",
                strerror(errno));
    }
    return sdsnew(buf);
}

/* Try a partial resynchronization with the master if we are about to reconnect.
 *
 * ��������֮�󣬳��Խ��в�����ͬ����
 *
 * If there is no cached master structure, at least try to issue a
 * "PSYNC ? -1" command in order to trigger a full resync using the PSYNC
 * command in order to obtain the master run id and the master replication
 * global offset.
 *
 * ��� master ����Ϊ�գ���ôͨ�� "PSYNC ? -1" ����������һ�� full resync ��
 * ������������ run id �͸���ƫ�������Դ��������ڵ����档
 *
 * This function is designed to be called from syncWithMaster(), so the
 * following assumptions are made:
 *
 * ��������� syncWithMaster() �������ã����������¼��裺
 *
 * 1) We pass the function an already connected socket "fd".
 *    һ���������׽��� fd �ᱻ���뺯��
 * 2) This function does not close the file descriptor "fd". However in case
 *    of successful partial resynchronization, the function will reuse
 *    'fd' as file descriptor of the server.master client structure.
 *    ��������ر� fd ��
 *    ������ͬ���ɹ�ʱ�������Ὣ fd ���� server.master �ͻ��˽ṹ�е�
 *    �ļ���������
 *
 * The function returns:
 * �����Ǻ����ķ���ֵ��
 *
 * PSYNC_CONTINUE: If the PSYNC command succeded and we can continue.
 *                 PSYNC ����ɹ������Լ�����
 * PSYNC_FULLRESYNC: If PSYNC is supported but a full resync is needed.
 *                   In this case the master run_id and global replication
 *                   offset is saved.
 *                   ��������֧�� PSYNC ���ܣ���Ŀǰ�����Ҫִ�� full resync ��
 *                   ����������£� run_id ��ȫ�ָ���ƫ�����ᱻ���档
 * PSYNC_NOT_SUPPORTED: If the server does not understand PSYNC at all and
 *                      the caller should fall back to SYNC.
 *                      ����������֧�� PSYNC ��������Ӧ���½��� SYNC ���
 */

//slavof����ͬ������:(slaveof ip port����)slaveofCommand->replicationSetMaster  (cluster replicate����)clusterCommand->clusterSetMaster->replicationSetMaster 
//��Ⱥ����ѡ�ٺ�����ͬ������:��������server.repl_state = REDIS_REPL_CONNECT���Ӷ�����connectWithMaster����һ������slaveTryPartialResynchronization����psyn��������ͬ��

#define PSYNC_CONTINUE 0  //������������ʽ��������Ĺ���
#define PSYNC_FULLRESYNC 1 //����������������ͬ����ʽ����
#define PSYNC_NOT_SUPPORTED 2 // ����������֧�� PSYNC  �ӷ�������Ҫ���½���SYNC
int slaveTryPartialResynchronization(int fd) {
    char *psync_runid;
    char psync_offset[32];
    sds reply;

    /* Initially set repl_master_initial_offset to -1 to mark the current
     * master run_id and offset as not valid. Later if we'll be able to do
     * a FULL resync using the PSYNC command we'll set the offset at the
     * right value, so that this information will be propagated to the
     * client structure representing the master into server.master. */
    server.repl_master_initial_offset = -1;
//����ӷ�����֮ǰ���������������ϣ���ͬ�������ݣ���;���ˣ������Ӷ��˺����replicationCacheMaster��server.cached_master = server.master;
//��ʾ֮ǰ�����ӵ���������
    if (server.cached_master) {
        // ������ڣ����Բ�����ͬ��
        // ����Ϊ "PSYNC <master_run_id> <repl_offset>"
        psync_runid = server.cached_master->replrunid;
        snprintf(psync_offset,sizeof(psync_offset),"%lld", server.cached_master->reploff+1);
        redisLog(REDIS_NOTICE,"Trying a partial resynchronization (request %s:%s).", psync_runid, psync_offset);
    } else {
        // ���治����
        // ���� "PSYNC ? -1" ��Ҫ��������ͬ��
        redisLog(REDIS_NOTICE,"Partial resynchronization not possible (no cached master)");
        psync_runid = "?";
        memcpy(psync_offset,"-1",3); //
    }

    /* Issue the PSYNC command */
    // �������������� PSYNC ����
    reply = sendSynchronousCommand(fd,"PSYNC",psync_runid,psync_offset,NULL);

    // ���յ� FULLRESYNC ������ full-resync
    if (!strncmp(reply,"+FULLRESYNC",11)) {
        char *runid = NULL, *offset = NULL;

        /* FULL RESYNC, parse the reply in order to extract the run id
         * and the replication offset. */
        // ��������¼���������� run id
        runid = strchr(reply,' ');
        if (runid) {
            runid++;
            offset = strchr(runid,' ');
            if (offset) offset++;
        }
        // ��� run id �ĺϷ���
        if (!runid || !offset || (offset-runid-1) != REDIS_RUN_ID_SIZE) {
            redisLog(REDIS_WARNING,
                "Master replied with wrong +FULLRESYNC syntax.");
            /* This is an unexpected condition, actually the +FULLRESYNC
             * reply means that the master supports PSYNC, but the reply
             * format seems wrong. To stay safe we blank the master
             * runid to make sure next PSYNCs will fail. */
            // ��������֧�� PSYNC ������ȴ�������쳣�� run id
            // ֻ�ý� run id ��Ϊ 0 �����´� PSYNC ʱʧ��
            memset(server.repl_master_runid,0,REDIS_RUN_ID_SIZE+1);
        } else {
            // ���� run id
            memcpy(server.repl_master_runid, runid, offset-runid-1);
            server.repl_master_runid[REDIS_RUN_ID_SIZE] = '\0';
            // �Լ� initial offset
            server.repl_master_initial_offset = strtoll(offset,NULL,10);
            // ��ӡ��־������һ�� FULL resync
            redisLog(REDIS_NOTICE,"Full resync from master: %s:%lld",
                server.repl_master_runid,
                server.repl_master_initial_offset);
        }
        /* We are going to full resync, discard the cached master structure. */
        // Ҫ��ʼ������ͬ���������е� master �Ѿ�û���ˣ������
        replicationDiscardCachedMaster();
        sdsfree(reply);
        
        // ����״̬
        return PSYNC_FULLRESYNC;
    }

    // ���յ� CONTINUE ������ partial resync
    if (!strncmp(reply,"+CONTINUE",9)) {
        /* Partial resync was accepted, set the replication state accordingly */
        redisLog(REDIS_NOTICE,
            "Successful partial resynchronization with master.");
        sdsfree(reply);
        // �������е� master ��Ϊ��ǰ master
        replicationResurrectCachedMaster(fd);

        // ����״̬
        return PSYNC_CONTINUE;
    }

    /* If we reach this point we receied either an error since the master does
     * not understand PSYNC, or an unexpected reply from the master.
     * Return PSYNC_NOT_SUPPORTED to the caller in both cases. */

    // ���յ�����
    if (strncmp(reply,"-ERR",4)) {
        /* If it's not an error, log the unexpected event. */
        redisLog(REDIS_WARNING,
            "Unexpected reply to PSYNC from master: %s", reply);
    } else {
        redisLog(REDIS_NOTICE,
            "Master does not support PSYNC or is in "
            "error state (reply: %s)", reply);
    }

    //����ӷ���������PSYNC��ȥ���������ԭ�򷵻ش���Ȼ����serverCron�����½��������ٴδ�����һ��PSYNC����SYNC����
    sdsfree(reply);
    replicationDiscardCachedMaster();

    // ����������֧�� PSYNC
    return PSYNC_NOT_SUPPORTED;
}

// �ӷ���������ͬ�����������Ļص�����
void syncWithMaster(aeEventLoop *el, int fd, void *privdata, int mask) {
    char tmpfile[256], *err;
    int dfd, maxtries = 5;
    int sockerr = 0, psync_result;
    socklen_t errlen = sizeof(sockerr);
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(privdata);
    REDIS_NOTUSED(mask);

    /* If this event fired after the user turned the instance into a master
     * with SLAVEOF NO ONE we must just return ASAP. */
    // ������� SLAVEOF NO ONE ģʽ����ô�ر� fd
    if (server.repl_state == REDIS_REPL_NONE) {
        close(fd);
        return;
    }

    /* Check for errors in the socket. */
    // ����׽��ִ���
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockerr, &errlen) == -1)
        sockerr = errno;
    if (sockerr) {
        aeDeleteFileEvent(server.el,fd,AE_READABLE|AE_WRITABLE);
        redisLog(REDIS_WARNING,"Error condition on socket for SYNC: %s",
            strerror(sockerr));
        goto error;
    }

    /* If we were connecting, it's time to send a non blocking PING, we want to
     * make sure the master is able to reply before going into the actual
     * replication process where we have long timeouts in the order of
     * seconds (in the meantime the slave would block). */
    // ���״̬Ϊ CONNECTING ����ô�ڽ��г���ͬ��֮ǰ��
    // ��������������һ���������� PONG 
    // ��Ϊ�������� RDB �ļ����ͷǳ���ʱ������������ȷ��������������ܷ���
    if (server.repl_state == REDIS_REPL_CONNECTING) {
        //�������ʾ���ӳɹ�
        redisLog(REDIS_NOTICE,"Non blocking connect for SYNC fired the event.");
        /* Delete the writable event so that the readable event remains
         * registered and we can wait for the PONG reply. */
        // �ֶ�����ͬ�� PING ����ʱȡ������д�¼�
        aeDeleteFileEvent(server.el,fd,AE_WRITABLE);
        // ����״̬
        server.repl_state = REDIS_REPL_RECEIVE_PONG;
        /* Send the PING, don't check for errors at all, we have the timeout
         * that will take care about this. */
        // ͬ������ PING
        syncWrite(fd,"PING\r\n",6,100);

        // ���أ��ȴ� PONG ����
        return;
    }

    /* Receive the PONG command. */
    // ���� PONG ���� �ͻ������ӳɹ���ᷢ��PING�ַ������������ˣ��������˻ظ�PONG,�Ӷ����������
    if (server.repl_state == REDIS_REPL_RECEIVE_PONG) {
        char buf[1024];

        /* Delete the readable event, we no longer need it now that there is
         * the PING reply to read. */
        // �ֶ�ͬ������ PONG ����ʱȡ���������¼�
        aeDeleteFileEvent(server.el,fd,AE_READABLE);

        /* Read the reply with explicit timeout. */
        // ������ָ��ʱ�������ڶ�ȡ PONG
        buf[0] = '\0';
        // ͬ������ PONG
        if (syncReadLine(fd,buf,sizeof(buf),
            server.repl_syncio_timeout*1000) == -1)
        {
            redisLog(REDIS_WARNING,
                "I/O error reading PING reply from master: %s",
                strerror(errno));
            goto error;
        }

        /* We accept only two replies as valid, a positive +PONG reply
         * (we just check for "+") or an authentication error.
         * Note that older versions of Redis replied with "operation not
         * permitted" instead of using a proper error code, so we test
         * both. */
        // ���յ�������ֻ�����ֿ��ܣ�
        // ��һ���� +PONG ���ڶ�������Ϊδ��֤�����ֵ� -NOAUTH ����
        if (buf[0] != '+' &&
            strncmp(buf,"-NOAUTH",7) != 0 &&
            strncmp(buf,"-ERR operation not permitted",28) != 0)
        {
            // ���յ�δ��֤����
            redisLog(REDIS_WARNING,"Error reply to PING from master: '%s'",buf);
            goto error;
        } else {
            // ���յ� PONG
            redisLog(REDIS_NOTICE,
                "Master replied to PING, replication can continue...");
        }
    }

    /* AUTH with the master if required. */
    // ���������֤
    if(server.masterauth) { //�ӷ���������AUTH �����ַ�������������ͨ��authCommand��֤����
        err = sendSynchronousCommand(fd,"AUTH",server.masterauth,NULL);
        if (err[0] == '-') {
            redisLog(REDIS_WARNING,"Unable to AUTH to MASTER: %s",err);
            sdsfree(err);
            goto error;
        }
        sdsfree(err);
    }

    /* Set the slave port, so that Master's INFO command can list the
     * slave listening port correctly. */
    // ���ӷ������Ķ˿ڷ��͸�����������
    // ʹ������������ INFO ���������ʾ�ӷ��������ڼ����Ķ˿�
    {
        sds port = sdsfromlonglong(server.port); 
        err = sendSynchronousCommand(fd,"REPLCONF","listening-port",port,
                                         NULL); //replconfCommand�н���
        sdsfree(port);
        /* Ignore the error if any, not all the Redis versions support
         * REPLCONF listening-port. */
        if (err[0] == '-') {
            redisLog(REDIS_NOTICE,"(Non critical) Master does not understand REPLCONF listening-port: %s", err);
        }
        sdsfree(err);
    }

    /* Try a partial resynchonization. If we don't have a cached master
     * slaveTryPartialResynchronization() will at least try to use PSYNC
     * to start a full resynchronization so that we get the master run id
     * and the global offset, to try a partial resync at the next
     * reconnection attempt. */
    // ���ݷ��صĽ��������ִ�в��� resync ������ full-resync
    psync_result = slaveTryPartialResynchronization(fd);

    // ����ִ�в��� resync
    if (psync_result == PSYNC_CONTINUE) {
        redisLog(REDIS_NOTICE, "MASTER <-> SLAVE sync: Master accepted a Partial Resynchronization.");
        // ����
        return;
    }

    /* Fall back to SYNC if needed. Otherwise psync_result == PSYNC_FULLRESYNC
     * and the server.repl_master_runid and repl_master_initial_offset are
     * already populated. */
    // ����������֧�� PSYNC ������ SYNC
    if (psync_result == PSYNC_NOT_SUPPORTED) {
        redisLog(REDIS_NOTICE,"Retrying with SYNC...");
        // �������������� SYNC ����
        if (syncWrite(fd,"SYNC\r\n",6,server.repl_syncio_timeout*1000) == -1) {
            redisLog(REDIS_WARNING,"I/O error writing to MASTER: %s",
                strerror(errno));
            goto error;
        }
    }

    // ���ִ�е����
    // ��ô psync_result == PSYNC_FULLRESYNC �� PSYNC_NOT_SUPPORTED

    /* Prepare a suitable temp file for bulk transfer */
    // ��һ����ʱ�ļ�������д��ͱ������������������������ RDB �ļ�����
    while(maxtries--) {
        snprintf(tmpfile,256,
            "temp-%d.%ld.rdb",(int)server.unixtime,(long int)getpid());
        dfd = open(tmpfile,O_CREAT|O_WRONLY|O_EXCL,0644);
        if (dfd != -1) break;
        sleep(1);
    }
    if (dfd == -1) {
        redisLog(REDIS_WARNING,"Opening the temp file needed for MASTER <-> SLAVE synchronization: %s",strerror(errno));
        goto error;
    }

    /* Setup the non blocking download of the bulk file. */
    // ����һ�����¼�������������ȡ���������� RDB �ļ�
    if (aeCreateFileEvent(server.el,fd, AE_READABLE,readSyncBulkPayload,NULL)
            == AE_ERR)
    {
        redisLog(REDIS_WARNING,
            "Can't create readable event for SYNC: %s (fd=%d)",
            strerror(errno),fd);
        goto error;
    }

    // ����״̬
    server.repl_state = REDIS_REPL_TRANSFER;

    // ����ͳ����Ϣ
    server.repl_transfer_size = -1;
    server.repl_transfer_read = 0;
    server.repl_transfer_last_fsync_off = 0;
    server.repl_transfer_fd = dfd;
    server.repl_transfer_lastio = server.unixtime;
    server.repl_transfer_tmpfile = zstrdup(tmpfile);

    return;

error:
    close(fd);
    server.repl_transfer_s = -1;
    server.repl_state = REDIS_REPL_CONNECT;
    return;
}

//slavof����ͬ������:(slaveof ip port����)slaveofCommand->replicationSetMaster  (cluster replicate����)clusterCommand->clusterSetMaster->replicationSetMaster 
//��Ⱥ����ѡ�ٺ�����ͬ������:��������server.repl_state = REDIS_REPL_CONNECT���Ӷ�����connectWithMaster����һ������slaveTryPartialResynchronization����psyn��������ͬ��

//����ͬ����ר�Ŵ���һ��repl_transfer_s�׽���(connectWithMaster)����������ͬ����ͬ����ɺ���replicationAbortSyncTransfer�йرո��׽���

// �Է�������ʽ������������
int connectWithMaster(void) {
    int fd;

    // ������������
    fd = anetTcpNonBlockConnect(NULL,server.masterhost,server.masterport);
    if (fd == -1) {
        redisLog(REDIS_WARNING,"Unable to connect to MASTER: %s",
            strerror(errno));
        return REDIS_ERR;
    }

    // ������������ fd �Ķ���д�¼��������ļ��¼�������
    if (aeCreateFileEvent(server.el,fd,AE_READABLE|AE_WRITABLE,syncWithMaster,NULL) ==
            AE_ERR)
    {
        close(fd);
        redisLog(REDIS_WARNING,"Can't create readable event for SYNC");
        return REDIS_ERR;
    }

    // ��ʼ��ͳ�Ʊ���
    server.repl_transfer_lastio = server.unixtime;
    server.repl_transfer_s = fd;

    // ��״̬��Ϊ������
    server.repl_state = REDIS_REPL_CONNECTING;

    return REDIS_OK;
}

/* This function can be called when a non blocking connection is currently
 * in progress to undo it. */
// ȡ�����ڽ��е�����
void undoConnectWithMaster(void) {
    int fd = server.repl_transfer_s;

    // ���ӱ��봦����������״̬
    redisAssert(server.repl_state == REDIS_REPL_CONNECTING ||
                server.repl_state == REDIS_REPL_RECEIVE_PONG);
    aeDeleteFileEvent(server.el,fd,AE_READABLE|AE_WRITABLE);
    close(fd);
    server.repl_transfer_s = -1;
    // �ص� CONNECT ״̬
    server.repl_state = REDIS_REPL_CONNECT;
}

/* This function aborts a non blocking replication attempt if there is one
 * in progress, by canceling the non-blocking connect attempt or
 * the initial bulk transfer.
 *
 * ��������ڽ��еķ����������ڽ��У���ôȡ������
 *
 * If there was a replication handshake in progress 1 is returned and
 * the replication state (server.repl_state) set to REDIS_REPL_CONNECT.
 *
 * ������������ֽ׶α�ȡ������ô���� 1 ��
 * ���� server.repl_state ������Ϊ REDIS_REPL_CONNECT ��
 *
 * Otherwise zero is returned and no operation is perforemd at all. 
 *
 * ���򷵻� 0 �����Ҳ�ִ���κβ�����
 */
int cancelReplicationHandshake(void) {
    if (server.repl_state == REDIS_REPL_TRANSFER) {
        replicationAbortSyncTransfer();
    } else if (server.repl_state == REDIS_REPL_CONNECTING ||
             server.repl_state == REDIS_REPL_RECEIVE_PONG)
    {
        undoConnectWithMaster();
    } else {
        return 0;
    }
    return 1;
}

//slavof����ͬ������:(slaveof ip port����)slaveofCommand->replicationSetMaster  (cluster replicate����)clusterCommand->clusterSetMaster->replicationSetMaster 
//��Ⱥ����ѡ�ٺ�����ͬ������:��������server.repl_state = REDIS_REPL_CONNECT���Ӷ�����connectWithMaster����һ������slaveTryPartialResynchronization����psyn��������ͬ��



/* Set replication to the specified master address and port. */
// ����������Ϊָ����ַ�Ĵӷ�����

//�ӷ�����ִ��slaveof ip port����Ҫ�Ѹø÷�������������дӷ��������ӶϿ����������Ա�֤��ͬʱҪ�ͷ�֮ǰsalveof���õľɵ�ip+port�˿�
void replicationSetMaster(char *ip, int port) { //������slaveof ip port,�������ַ���������replicationCron������

    // ���ԭ�е�����������ַ������еĻ���
    sdsfree(server.masterhost);

    // IP
    server.masterhost = sdsnew(ip);

    // �˿�
    server.masterport = port;

    // ���ԭ�������е�����������Ϣ������

    /*  ���籾������2�Ƿ�����3�������������������������ֳ�����������1�Ĵӷ���������ô��Ҫ�Ͽ��ʹӷ�����3�����ӵȣ����е���ϢҪ�����ϼ�������1Ϊ׼��*/
    
    // ���֮ǰ��������ַ����ô�ͷ���
    if (server.master) freeClient(server.master, NGX_FUNC_LINE);
    // �Ͽ����дӷ����������ӣ�ǿ�����дӷ�����ִ����ͬ��
    disconnectSlaves(); /* Force our slaves to resync with us as well. */
    // ��տ����е� master ���棬��Ϊ�Ѿ�����ִ�� PSYNC ��
    replicationDiscardCachedMaster(); /* Don't try a PSYNC. */
    // �ͷ� backlog ��ͬ�� PSYNC Ŀǰ�Ѿ�����ִ����
    freeReplicationBacklog(); /* Don't allow our chained slaves to PSYNC. */
    // ȡ��֮ǰ�ĸ��ƽ��̣�����еĻ���
    cancelReplicationHandshake();

    // ��������״̬���ص㣩
    server.repl_state = REDIS_REPL_CONNECT;
    server.master_repl_offset = 0;
    server.repl_down_since = 0;
}

/* Cancel replication, setting the instance as a master itself. */
// ȡ�����ƣ�������������Ϊ��������
void replicationUnsetMaster(void) { //cluster reset xxx ����slave of���Լ�slave��master�л�������øú���

    if (server.masterhost == NULL) return; /* Nothing to do. */

    sdsfree(server.masterhost);
    server.masterhost = NULL;

    if (server.master) {
        if (listLength(server.slaves) == 0) {
            /* If this instance is turned into a master and there are no
             * slaves, it inherits the replication offset from the master.
             * Under certain conditions this makes replicas comparable by
             * replication offset to understand what is the most updated. */
            server.master_repl_offset = server.master->reploff;
            freeReplicationBacklog();
        }
        freeClient(server.master, NGX_FUNC_LINE);
    }

    replicationDiscardCachedMaster();

    cancelReplicationHandshake();

    server.repl_state = REDIS_REPL_NONE;
}

/*
PSYNC����ĵ��÷��������֣�
 1. ����ӷ�������ǰû�и��ƹ��κ���������������֮ǰִ�й�SLAVEOF no one
   �����ô�ӷ������ڿ�ʼһ���µĸ���ʱ����������������PSYNC?-1���
   ��������������������������ͬ������Ϊ��ʱ������ִ�в�����ͬ������
   ���෴�أ�����ӷ������Ѿ����ƹ�ĳ��������������ô�ӷ������ڿ�ʼһ���µĸ�
   ��ʱ����������������PSYNC <runid> <offset>�������runid����һ��
   ���Ƶ���������������ID����offset���Ǵӷ�������ǰ�ĸ���ƫ���������յ���
   �����������������ͨ���������������ж�Ӧ�öԴӷ�����ִ������ͬ��������
 ������������յ�PSYNC�����������������ӷ����������������ֻظ�������һ�֣�
 2. ���������������+FULLRESYNC  <runid>  <offset>�ظ�����ô��ʾ������
   ������ӷ�����ִ��������ͬ������������runid�������������������ID����
   �������Ὣ���ID��������������һ�η���PSYNC����ʱʹ�ã���offset����
   ����������ǰ�ĸ���ƫ�������ӷ������Ὣ���ֵ��Ϊ�Լ��ĳ�ʼ��ƫ������
3. ���������������+CONTINUE�ظ�����ô��ʾ������������ӷ�����ִ�в�����
  ͬ���������ӷ�����ֻҪ���������������Լ�ȱ�ٵ��ǲ������ݷ��͹����Ϳ����ˡ�
4. ���������������-ERR�ظ�����ô��ʾ���������İ汾����Redis 2.8����ʶ��
  ��PSYNC����ӷ���������������������SYNC���������������ִ������ͬ
  ��������
CLUSTER REPLICATE Ҳ����ӽڵ�Ϊĳ���ڵ�Ĵӽڵ㣬��clusterCommand  ע���slaveof������slaveof��������cluster,��slaveofCommand

*/
//��Ⱥģʽ�£����������slaveof�����slaveofCommand     slaveof�ǿͻ��˷��͵����redis�����ǲ��ᷢ�͸�����ģ����ɿͻ����Լ�����
void slaveofCommand(redisClient *c) { //����һ���ڵ�Aͨ��slaveof��Ϊ��һ���ڵ�B��slave���������B�ڵ���ģʽ��
    /* SLAVEOF is not allowed in cluster mode as replication is automatically
     * configured using the current address of the master node. */
    // �������ڼ�Ⱥģʽ��ʹ��
    if (server.cluster_enabled) {
        addReplyError(c,"SLAVEOF not allowed in cluster mode.");
        return;
    }

    /* The special host/port combination "NO" "ONE" turns the instance
     * into a master. Otherwise the new master address is set. */
    // SLAVEOF NO ONE �ôӷ�����תΪ��������   Ҳ���Ǳ����������ڴ�����ĳ�����������ˣ�replicationUnsetMaster�л��masterhost��ΪNULL
    if (!strcasecmp(c->argv[1]->ptr,"no") &&
        !strcasecmp(c->argv[2]->ptr,"one")) { //slave no one�����֮ǰ�����������������򱾴ζϿ��������������ӣ��Լ�����
        if (server.masterhost) {
            // �÷�����ȡ�����ƣ���Ϊ��������
            replicationUnsetMaster();
            redisLog(REDIS_NOTICE,"MASTER MODE enabled (user request)");
        }
    } else { //�ӷ�����ִ��slaveof ip port
        long port;

        // ��ȡ�˿ڲ���
        if ((getLongFromObjectOrReply(c, c->argv[2], &port, NULL) != REDIS_OK))
            return;

        /* Check if we are already attached to the specified slave */
        // �������� host �� port �Ƿ������Ŀǰ����������
        // ����ǵĻ�����ͻ��˷��� +OK ��������������

        //˵��֮ǰ�Ѿ�slaveof ip port���������ִ�и����˵��֮ǰ�Ѿ����ӽ����ɹ�����
        if (server.masterhost && !strcasecmp(server.masterhost,c->argv[1]->ptr)
            && server.masterport == port) {
            redisLog(REDIS_NOTICE,"SLAVE OF would result into synchronization with the master we are already connected with. No operation performed.");
            addReplySds(c,sdsnew("+OK Already connected to specified master\r\n"));//��ӷ�������redis-cli�ͻ��˷��͸�����
            return;
        }

        //��һ��ִ��slaveof ip port,�µ�ip����port
        /* There was no previous master or the user specified a different one,
         * we can continue. */
        // û��ǰ���������������߿ͻ���ָ�����µ���������
        // ��ʼִ�и��Ʋ���
        replicationSetMaster(c->argv[1]->ptr, port); 
        redisLog(REDIS_NOTICE,"SLAVE OF %s:%d enabled (user request)",
            server.masterhost, server.masterport);
    }
    addReply(c,shared.ok);
}

/* Send a REPLCONF ACK command to the master to inform it about the current
 * processed offset. If we are not connected with a master, the command has
 * no effects. */

/*
Masterÿ��10��(repl-ping-slave-period��������)��Slave����PING����
Slaveÿ��1����Master���͡�REPLCONF�� ��ACK�� ������(slave_repl_offset)��
*/
 
// �������������� REPLCONF AKC ����֪��ǰ�����ƫ����
// ���δ������������������ô�������û��ʵ��Ч��
void replicationSendAck(void) { 
    redisClient *c = server.master;

    if (c != NULL) {
        c->flags |= REDIS_MASTER_FORCE_REPLY;
        addReplyMultiBulkLen(c,3);
        addReplyBulkCString(c,"REPLCONF");
        addReplyBulkCString(c,"ACK");
        // ����ƫ����
        addReplyBulkLongLong(c,c->reploff);
        c->flags &= ~REDIS_MASTER_FORCE_REPLY;
    }
}

/* ---------------------- MASTER CACHING FOR PSYNC -------------------------- */

/* In order to implement partial synchronization we need to be able to cache
 * our master's client structure after a transient disconnection.
 *
 * Ϊ��ʵ�� partial synchronization ��
 * slave ��Ҫһ�� cache ���� master ����ʱ�� master ���浽 cache �ϡ�
 *
 * It is cached into server.cached_master and flushed away using the following
 * functions. 
 *
 * �����Ǹ� cache �����ú����������
 */

/* This function is called by freeClient() in order to cache the master
 * client structure instead of destryoing it. freeClient() will return
 * ASAP after this function returns, so every action needed to avoid problems
 * with a client that is really "suspended" has to be done by this function.
 *
 * ��������� freeClient() �������ã�������ǰ�� master ��¼�� master cache ���棬
 * Ȼ�󷵻ء�
 *
 * The other functions that will deal with the cached master are:
 *
 * ������ master cahce �йصĺ����ǣ�
 *
 * replicationDiscardCachedMaster() that will make sure to kill the client
 * as for some reason we don't want to use it in the future.
 *
 * replicationDiscardCachedMaster() ȷ��������� master �����������л��档
 *
 * replicationResurrectCachedMaster() that is used after a successful PSYNC
 * handshake in order to reactivate the cached master.
 *
 * replicationResurrectCachedMaster() �� PSYNC �ɹ�ʱ�������е� master ��ȡ������
 * ���³�Ϊ�µ� master ��
 */
void replicationCacheMaster(redisClient *c) { 
//��master���ӶϿ�������Ҫ��¼��master��cache�У��Ա��´����ӵ���master���ܹ����в���ͬ��psyn������������һ��
//ȫ��ͬ��
    listNode *ln;

    redisAssert(server.master != NULL && server.cached_master == NULL);
    redisLog(REDIS_NOTICE,"Caching the disconnected master state.");

    /* Remove from the list of clients, we don't want this client to be
     * listed by CLIENT LIST or processed in any way by batch operations. */
    // �ӿͻ����������Ƴ���������
    ln = listSearchKey(server.clients,c);
    redisAssert(ln != NULL);
    listDelNode(server.clients,ln);

    /* Save the master. Server.master will be set to null later by
     * replicationHandleMasterDisconnection(). */
    // ���� master
    server.cached_master = server.master;

    /* Remove the event handlers and close the socket. We'll later reuse
     * the socket of the new connection with the master during PSYNC. */
    // ɾ���¼����ӣ��ر� socket
    aeDeleteFileEvent(server.el,c->fd,AE_READABLE);
    aeDeleteFileEvent(server.el,c->fd,AE_WRITABLE);
    close(c->fd);

    /* Set fd to -1 so that we can safely call freeClient(c) later. */
    c->fd = -1;

    /* Invalidate the Peer ID cache. */
    if (c->peerid) {
        sdsfree(c->peerid);
        c->peerid = NULL;
    }

    /* Caching the master happens instead of the actual freeClient() call,
     * so make sure to adjust the replication state. This function will
     * also set server.master to NULL. */
    // ���ø���״̬������ server.master ��Ϊ NULL
    // ��ǿ�ƶϿ���������������дӷ�������������ִ�� resync 
    replicationHandleMasterDisconnection();
}

/* Free a cached master, called when there are no longer the conditions for
 * a partial resync on reconnection. 
 *
 * ��� master ���棬�������Ѿ�������ִ�� partial resync ʱִ��
 */
void replicationDiscardCachedMaster(void) {

    if (server.cached_master == NULL) return;

    redisLog(REDIS_NOTICE,"Discarding previously cached master state.");
    server.cached_master->flags &= ~REDIS_MASTER;
    freeClient(server.cached_master, NGX_FUNC_LINE);
    server.cached_master = NULL;
}

/* Turn the cached master into the current master, using the file descriptor
 * passed as argument as the socket for the new master.
 *
 * �������е� master ����Ϊ�������ĵ�ǰ master ��
 *
 * This funciton is called when successfully setup a partial resynchronization
 * so the stream of data that we'll receive will start from were this
 * master left. 
 *
 * ��������ͬ��׼������֮�󣬵������������
 * master �Ͽ�֮ǰ�������������ݿ��Լ���ʹ�á�
 */
void replicationResurrectCachedMaster(int newfd) {
    
    // ���� master
    server.master = server.cached_master;
    server.cached_master = NULL;

    server.master->fd = newfd;

    server.master->flags &= ~(REDIS_CLOSE_AFTER_REPLY|REDIS_CLOSE_ASAP);

    server.master->authenticated = 1;
    server.master->lastinteraction = server.unixtime;

    // �ص�������״̬
    server.repl_state = REDIS_REPL_CONNECTED;

    /* Re-add to the list of clients. */
    // �� master ���¼��뵽�ͻ����б���
    listAddNodeTail(server.clients,server.master);
    // ���� master �Ķ��¼�
    if (aeCreateFileEvent(server.el, newfd, AE_READABLE,
                          readQueryFromClient, server.master)) {
        redisLog(REDIS_WARNING,"Error resurrecting the cached master, impossible to add the readable handler: %s", strerror(errno));
        freeClientAsync(server.master); /* Close ASAP. */
    }

    /* We may also need to install the write handler as well if there is
     * pending data in the write buffers. */
    if (server.master->bufpos || listLength(server.master->reply)) {
        if (aeCreateFileEvent(server.el, newfd, AE_WRITABLE,
                          sendReplyToClient, server.master)) {
            redisLog(REDIS_WARNING,"Error resurrecting the cached master, impossible to add the writable handler: %s", strerror(errno));
            freeClientAsync(server.master); /* Close ASAP. */
        }
    }
}

/* ------------------------- MIN-SLAVES-TO-WRITE  --------------------------- */

/* This function counts the number of slaves with lag <= min-slaves-max-lag.
 * 
 * ������Щ�ӳ�ֵ���ڵ��� min-slaves-max-lag �Ĵӷ�����������
 *
 * If the option is active, the server will prevent writes if there are not
 * enough connected slaves with the specified lag (or less). 
 *
 * ��������������� min-slaves-max-lag ѡ�
 * ��ô�����ѡ����ָ���������ﲻ��ʱ������������ֹд����ִ�С�
 */
void refreshGoodSlavesCount(void) {
    listIter li;
    listNode *ln;
    int good = 0;

    if (!server.repl_min_slaves_to_write ||
        !server.repl_min_slaves_max_lag) return;

    listRewind(server.slaves,&li);
    while((ln = listNext(&li))) {
        redisClient *slave = ln->value;

        // �����ӳ�ֵ
        time_t lag = server.unixtime - slave->repl_ack_time;

        // ���� GOOD
        if (slave->replstate == REDIS_REPL_ONLINE &&
            lag <= server.repl_min_slaves_max_lag) good++;
    }

    // ����״̬���õĴӷ���������
    server.repl_good_slaves_count = good;
}

/* ----------------------- REPLICATION SCRIPT CACHE --------------------------
 * The goal of this code is to keep track of scripts already sent to every
 * connected slave, in order to be able to replicate EVALSHA as it is without
 * translating it to EVAL every time it is possible.
 *
 * �ⲿ�ִ����Ŀ���ǣ�
 * ����Щ�Ѿ����͸����������Ӵӷ������Ľű����浽�������棬
 * ������ִ�й�һ�� EVAL ֮������ʱ�򶼿���ֱ�ӷ��� EVALSHA �ˡ�
 *
 * We use a capped collection implemented by a hash table for fast lookup
 * of scripts we can send as EVALSHA, plus a linked list that is used for
 * eviction of the oldest entry when the max number of items is reached.
 *
 * ���򹹽���һ���̶���С�ļ��ϣ�capped collection����
 * �ü����ɹ�ϣ�ṹ��һ��������ɣ�
 * ��ϣ������ٲ��ң������������γ�һ�� FIFO ���У�
 * �ڽű��������������ֵʱ�����ȱ���Ľű�����ɾ����
 *
 * We don't care about taking a different cache for every different slave
 * since to fill the cache again is not very costly, the goal of this code
 * is to avoid that the same big script is trasmitted a big number of times
 * per second wasting bandwidth and processor speed, but it is not a problem
 * if we need to rebuild the cache from scratch from time to time, every used
 * script will need to be transmitted a single time to reappear in the cache.
 *
 * Redis ���ǲ���Ϊÿ���ӷ�������������Ľű����棬
 * ���������дӷ�����������һ��ȫ�ֻ��档
 * ������Ϊ�������ű��������еĲ�����������
 * ��������Ŀ���Ǳ����ڶ�ʱ���ڷ���ͬһ����ű���Σ�
 * ��ɴ���� CPU �˷ѣ�
 * ��ʱ��ʱ���½���һ�λ���Ĵ��벢���߰���
 * ÿ�ν�һ���ű���ӵ�������ʱ������Ҫ��������ű�һ�Ρ�
 *
 * This is how the system works:
 *
 * ���������ϵͳ�Ĺ�����ʽ��
 *
 * 1) Every time a new slave connects, we flush the whole script cache.
 *    ÿ�����µĴӷ���������ʱ��������нű����档
 *
 * 2) We only send as EVALSHA what was sent to the master as EVALSHA, without
 *    trying to convert EVAL into EVALSHA specifically for slaves.
 *    ����ֻ�����������ӵ� EVALSHA ʱ����ӷ��������� EVALSHA ��
 *    �������������Խ� EVAL ת���� EVALSHA ��
 *
 * 3) Every time we trasmit a script as EVAL to the slaves, we also add the
 *    corresponding SHA1 of the script into the cache as we are sure every
 *    slave knows about the script starting from now.
 *    ÿ�ν��ű�ͨ�� EVAL ����͸����дӷ�����ʱ��
 *    ���ű��� SHA1 �����浽�ű��ֵ��У��ֵ�ļ�Ϊ SHA1 ��ֵΪ NULL ��
 *    �������Ǿ�֪����ֻҪ�ű��� SHA1 ���ֵ��У�
 *    ��ô����ű��ʹ��������� slave �С�
 *
 * 4) On SCRIPT FLUSH command, we replicate the command to all the slaves
 *    and at the same time flush the script cache.
 *    ���ͻ���ִ�� SCRIPT FLUSH ��ʱ�򣬷�������������Ƹ����дӷ�������
 *    ������Ҳˢ���Լ��Ľű����档
 *
 * 5) When the last slave disconnects, flush the cache.
 *    �����дӷ��������Ͽ�ʱ����սű���
 *
 * 6) We handle SCRIPT LOAD as well since that's how scripts are loaded
 *    in the master sometimes.
 *    SCRIPT LOAD ���������ű���������ú� EVAL һ����
 */

/* Initialize the script cache, only called at startup. */
// ��ʼ�����棬ֻ�ڷ���������ʱ����
void replicationScriptCacheInit(void) {
    // ��󻺴�ű���
    server.repl_scriptcache_size = 10000;
    // �ֵ�
    server.repl_scriptcache_dict = dictCreate(&replScriptCacheDictType,NULL);
    // FIFO ����
    server.repl_scriptcache_fifo = listCreate();
}

/* Empty the script cache. Should be called every time we are no longer sure
 * that every slave knows about all the scripts in our set, or when the
 * current AOF "context" is no longer aware of the script. In general we
 * should flush the cache:
 *
 * ��սű����档
 *
 * �����������ִ�У�
 *
 * 1) Every time a new slave reconnects to this master and performs a
 *    full SYNC (PSYNC does not require flushing).
 *    ���´ӷ��������룬����ִ����һ�� full SYNC �� PSYNC ������ջ���
 * 2) Every time an AOF rewrite is performed.
 *    ÿ��ִ�� AOF ��дʱ
 * 3) Every time we are left without slaves at all, and AOF is off, in order
 *    to reclaim otherwise unused memory.
 *    ��û���κδӷ�������AOF �رյ�ʱ��Ϊ��Լ�ڴ��ִ����ա�
 */
void replicationScriptCacheFlush(void) {
    dictEmpty(server.repl_scriptcache_dict,NULL);
    listRelease(server.repl_scriptcache_fifo);
    server.repl_scriptcache_fifo = listCreate();
}

/* Add an entry into the script cache, if we reach max number of entries the
 * oldest is removed from the list. 
 *
 * ���ű��� SHA1 ��ӵ������У�
 * �������������Ѵﵽ���ֵ����ôɾ����ɵ��Ǹ��ű���FIFO��
 */
void replicationScriptCacheAdd(sds sha1) {
    int retval;
    sds key = sdsdup(sha1);

    /* Evict oldest. */
    // �����С�����������ƣ���ôɾ�����
    if (listLength(server.repl_scriptcache_fifo) == server.repl_scriptcache_size)
    {
        listNode *ln = listLast(server.repl_scriptcache_fifo);
        sds oldest = listNodeValue(ln);

        retval = dictDelete(server.repl_scriptcache_dict,oldest);
        redisAssert(retval == DICT_OK);
        listDelNode(server.repl_scriptcache_fifo,ln);
    }

    /* Add current. */
    // ��� SHA1
    retval = dictAdd(server.repl_scriptcache_dict,key,NULL);
    listAddNodeHead(server.repl_scriptcache_fifo,key);
    redisAssert(retval == DICT_OK);
}

/* Returns non-zero if the specified entry exists inside the cache, that is,
 * if all the slaves are aware of this script SHA1. */
// ����ű������ڽű�����ô���� 1 �����򣬷��� 0 ��
int replicationScriptCacheExists(sds sha1) {
    return dictFind(server.repl_scriptcache_dict,sha1) != NULL;
}

/* ----------------------- SYNCHRONOUS REPLICATION --------------------------
 * Redis synchronous replication design can be summarized in points:
 *
 * - Redis masters have a global replication offset, used by PSYNC.
 * - Master increment the offset every time new commands are sent to slaves.
 * - Slaves ping back masters with the offset processed so far.
 *
 * So synchronous replication adds a new WAIT command in the form:
 *
 *   WAIT <num_replicas> <milliseconds_timeout>
 *
 * That returns the number of replicas that processed the query when
 * we finally have at least num_replicas, or when the timeout was
 * reached.
 *
 * The command is implemented in this way:
 *
 * - Every time a client processes a command, we remember the replication
 *   offset after sending that command to the slaves.
 * - When WAIT is called, we ask slaves to send an acknowledgement ASAP.
 *   The client is blocked at the same time (see blocked.c).
 * - Once we receive enough ACKs for a given offset or when the timeout
 *   is reached, the WAIT command is unblocked and the reply sent to the
 *   client.
 */

/* This just set a flag so that we broadcast a REPLCONF GETACK command
 * to all the slaves in the beforeSleep() function. Note that this way
 * we "group" all the clients that want to wait for synchronouns replication
 * in a given event loop iteration, and send a single GETACK for them all. */
void replicationRequestAckFromSlaves(void) {
    server.get_ack_from_slaves = 1;
}

/* Return the number of slaves that already acknowledged the specified
 * replication offset. */
int replicationCountAcksByOffset(long long offset) {
    listIter li;
    listNode *ln;
    int count = 0;

    listRewind(server.slaves,&li);
    while((ln = listNext(&li))) {
        redisClient *slave = ln->value;

        if (slave->replstate != REDIS_REPL_ONLINE) continue;
        if (slave->repl_ack_off >= offset) count++;
    }
    return count;
}

/* WAIT for N replicas to acknowledge the processing of our latest
 * write command (and all the previous commands). */
void waitCommand(redisClient *c) {
    mstime_t timeout;
    long numreplicas, ackreplicas;
    long long offset = c->woff;

    /* Argument parsing. */
    if (getLongFromObjectOrReply(c,c->argv[1],&numreplicas,NULL) != REDIS_OK)
        return;
    if (getTimeoutFromObjectOrReply(c,c->argv[2],&timeout,UNIT_MILLISECONDS)
        != REDIS_OK) return;

    /* First try without blocking at all. */
    ackreplicas = replicationCountAcksByOffset(c->woff);
    if (ackreplicas >= numreplicas || c->flags & REDIS_MULTI) {
        addReplyLongLong(c,ackreplicas);
        return;
    }

    /* Otherwise block the client and put it into our list of clients
     * waiting for ack from slaves. */
    c->bpop.timeout = timeout;
    c->bpop.reploffset = offset;
    c->bpop.numreplicas = numreplicas;
    listAddNodeTail(server.clients_waiting_acks,c);
    blockClient(c,REDIS_BLOCKED_WAIT);

    /* Make sure that the server will send an ACK request to all the slaves
     * before returning to the event loop. */
    replicationRequestAckFromSlaves();
}

/* This is called by unblockClient() to perform the blocking op type
 * specific cleanup. We just remove the client from the list of clients
 * waiting for replica acks. Never call it directly, call unblockClient()
 * instead. */
void unblockClientWaitingReplicas(redisClient *c) {
    listNode *ln = listSearchKey(server.clients_waiting_acks,c);
    redisAssert(ln != NULL);
    listDelNode(server.clients_waiting_acks,ln);
}

/* Check if there are clients blocked in WAIT that can be unblocked since
 * we received enough ACKs from slaves. */
void processClientsWaitingReplicas(void) {
    long long last_offset = 0;
    int last_numreplicas = 0;

    listIter li;
    listNode *ln;

    listRewind(server.clients_waiting_acks,&li);
    while((ln = listNext(&li))) {
        redisClient *c = ln->value;

        /* Every time we find a client that is satisfied for a given
         * offset and number of replicas, we remember it so the next client
         * may be unblocked without calling replicationCountAcksByOffset()
         * if the requested offset / replicas were equal or less. */
        if (last_offset && last_offset > c->bpop.reploffset &&
                           last_numreplicas > c->bpop.numreplicas)
        {
            unblockClient(c);
            addReplyLongLong(c,last_numreplicas);
        } else {
            int numreplicas = replicationCountAcksByOffset(c->bpop.reploffset);

            if (numreplicas >= c->bpop.numreplicas) {
                last_offset = c->bpop.reploffset;
                last_numreplicas = numreplicas;
                unblockClient(c);
                addReplyLongLong(c,numreplicas);
            }
        }
    }
}

/* Return the slave replication offset for this instance, that is
 * the offset for which we already processed the master replication stream. */
long long replicationGetSlaveOffset(void) {
    long long offset = 0;

    if (server.masterhost != NULL) {
        if (server.master) { //���master����������ͬ����ɺ�ͨ���õ�
            offset = server.master->reploff;
        } else if (server.cached_master) {
            offset = server.cached_master->reploff;
        }
    }
    /* offset may be -1 when the master does not support it at all, however
     * this function is designed to return an offset that can express the
     * amount of data processed by the master, so we return a positive
     * integer. */
    if (offset < 0) offset = 0;
    return offset;
}

/* --------------------------- REPLICATION CRON  ---------------------------- */

/* Replication cron funciton, called 1 time per second. */
// ���� cron ������ÿ�����һ��
void replicationCron(void) {

    /* Non blocking connection timeout? */
    // �������ӵ���������������ʱ
    if (server.masterhost &&
        (server.repl_state == REDIS_REPL_CONNECTING ||
         server.repl_state == REDIS_REPL_RECEIVE_PONG) &&
        (time(NULL)-server.repl_transfer_lastio) > server.repl_timeout) //Master bgsave����RDBʧ�ܻ�������п����ߵ����if�ڲ�
    /* �����ʱ��������:�ӷ���syncҪ������������ͬ���� repl_transfer_lastio��ʾÿ�ν��յ�RDB�ļ���һ���ʱ�򶼽��и���*/
    /* ����ڽ�������ͬ���ڼ䣬�ӱ�����sync��ʼ�����������ֻҪ��������rdb�ļ����������ڷ��Ͳ���rdb�ļ��󣬲��ڼ������ͣ������������ﳬʱ��Ȼ������ */
    {
        redisLog(REDIS_WARNING, "Timeout connecting to the MASTER...");
        // ȡ������
        undoConnectWithMaster(); //����������ʱ������
    }

    /* Bulk transfer I/O timeout? */
    // RDB �ļ��Ĵ����ѳ�ʱ��
    if (server.masterhost && server.repl_state == REDIS_REPL_TRANSFER &&
        (time(NULL)-server.repl_transfer_lastio) > server.repl_timeout)
    {
        redisLog(REDIS_WARNING,"Timeout receiving bulk data from MASTER... If the problem persists try to set the 'repl-timeout' parameter in redis.conf to a larger value.");
        // ֹͣ���ͣ���ɾ����ʱ�ļ�
        replicationAbortSyncTransfer();
    }

/*
Masterÿ��10��(repl-ping-slave-period��������)��Slave����PING����
Slaveÿ��1����Master���͡�REPLCONF�� ��ACK�� ������(slave_repl_offset)��
*/
    /* Timed out master when we are an already connected slave? */
    // �ӷ������������������������������ڳ�ʱ   �Ӻܾ�û���յ�����PING��Ϣ�ˣ���ʱ    �����ӵ�replconf ack xxx�����Ƿ�ʱ���ڸú�������
    if (server.masterhost && server.repl_state == REDIS_REPL_CONNECTED &&
        (time(NULL)-server.master->lastinteraction) > server.repl_timeout)
    {
        redisLog(REDIS_WARNING,"MASTER timeout: no data nor PING received...");
        // �ͷ���������
        freeClient(server.master, NGX_FUNC_LINE);
    }

    /* Check if we should connect to a MASTER */
    // ����������������
    if (server.repl_state == REDIS_REPL_CONNECT) {
        redisLog(REDIS_NOTICE,"Connecting to MASTER %s:%d",
            server.masterhost, server.masterport);
        if (connectWithMaster() == REDIS_OK) {
            redisLog(REDIS_NOTICE,"MASTER <-> SLAVE sync started");
        }
    }

    /* Send ACK to master from time to time.
     * Note that we do not send periodic acks to masters that don't
     * support PSYNC and replication offsets. */
    // ������������������ ACK ����
    // ������������������� REDIS_PRE_PSYNC �Ļ��Ͳ�����
    // ��Ϊ���иñ�ʶ�İ汾Ϊ < 2.8 �İ汾����Щ�汾��֧�� ACK ����
    if (server.masterhost && server.master &&
        !(server.master->flags & REDIS_PRE_PSYNC)) //ֻ��֧��PSYNC��master����replconf ack
        replicationSendAck();


/*
Masterÿ��10��(repl-ping-slave-period��������)��Slave����PING����
Slaveÿ��1����Master���͡�REPLCONF�� ��ACK�� ������(slave_repl_offset)��
*/
    /* If we have attached slaves, PING them from time to time.
     *
     * ����������дӷ���������ʱ�����Ƿ��� PING ��
     *
     * So slaves can implement an explicit timeout to masters, and will
     * be able to detect a link disconnection even if the TCP connection
     * will not actually go down. 
     *
     * �����ӷ������Ϳ���ʵ����ʽ�� master ��ʱ�жϻ��ƣ�
     * ��ʹ TCP ����δ�Ͽ�Ҳ����ˡ�
     */
    if (!(server.cronloops % (server.repl_ping_slave_period * server.hz))) {
        listIter li;
        listNode *ln;
        robj *ping_argv[1];

        /* First, send PING */
        // ������������ slave ��״̬Ϊ ONLINE������ PING
        ping_argv[0] = createStringObject("PING",4); //���͵���PING�ַ����������Ǽ�Ⱥ�ڵ�֮���CLUSTERMSG_TYPE_PING����
        replicationFeedSlaves(server.slaves, server.slaveseldb, ping_argv, 1);
        decrRefCount(ping_argv[0]);

        /* Second, send a newline to all the slaves in pre-synchronization
         * stage, that is, slaves waiting for the master to create the RDB file.
         *
         * ����Щ���ڵȴ� RDB �ļ��Ĵӷ�������״̬Ϊ BGSAVE_START �� BGSAVE_END��
         * ���� "\n"
         *
         * The newline will be ignored by the slave but will refresh the
         * last-io timer preventing a timeout. 
         *
         * ��� "\n" �ᱻ�ӷ��������ԣ�
         * �������þ���������ֹ����������Ϊ���ڲ�������Ϣ�����ӷ���������Ϊ��ʱ
         */
        listRewind(server.slaves,&li);
        while((ln = listNext(&li))) {
            redisClient *slave = ln->value;

            if (slave->replstate == REDIS_REPL_WAIT_BGSAVE_START ||
                slave->replstate == REDIS_REPL_WAIT_BGSAVE_END) {
                if (write(slave->fd, "\n", 1) == -1) {
                    /* Don't worry, it's just a ping. */
                }
            }
        }
    }

    /* Disconnect timedout slaves. */
    // �Ͽ���ʱ�ӷ�����
    if (listLength(server.slaves)) { //������master�����Ƿ��з���REPLCONF ACK����Ĺ����� �Ӽ������PING��ǰ��
        listIter li;
        listNode *ln;

        // �������дӷ�����
        listRewind(server.slaves,&li);
        while((ln = listNext(&li))) {
            redisClient *slave = ln->value;

            // �Թ�δ ONLINE �Ĵӷ�����
            if (slave->replstate != REDIS_REPL_ONLINE) continue;

            // �����ɰ�Ĵӷ�����
            if (slave->flags & REDIS_PRE_PSYNC) continue;

            // �ͷų�ʱ�ӷ�����
            if ((server.unixtime - slave->repl_ack_time) > server.repl_timeout)
            {
                char ip[REDIS_IP_STR_LEN];
                int port;

                if (anetPeerToString(slave->fd,ip,sizeof(ip),&port) != -1) {
                    redisLog(REDIS_WARNING,
                        "Disconnecting timedout slave: %s:%d",
                        ip, slave->slave_listening_port);
                }
                
                // �ͷ�
                freeClient(slave, NGX_FUNC_LINE);
            }
        }
    }

    /* If we have no attached slaves and there is a replication backlog
     * using memory, free it after some (configured) time. */
    // ��û���κδӷ������� N ��֮���ͷ� backlog
    if (listLength(server.slaves) == 0 && server.repl_backlog_time_limit &&
        server.repl_backlog)
    {
        time_t idle = server.unixtime - server.repl_no_slaves_since;

        if (idle > server.repl_backlog_time_limit) {
            // �ͷ�
            freeReplicationBacklog();
            redisLog(REDIS_NOTICE,
                "Replication backlog freed after %d seconds "
                "without connected slaves.",
                (int) server.repl_backlog_time_limit);
        }
    }

    /* If AOF is disabled and we no longer have attached slaves, we can
     * free our Replication Script Cache as there is no need to propagate
     * EVALSHA at all. */
    // ��û���κδӷ�������AOF �رյ�����£���� script ����
    // ��Ϊ�Ѿ�û�д��� EVALSHA �ı�Ҫ��
    if (listLength(server.slaves) == 0 &&
        server.aof_state == REDIS_AOF_OFF &&
        listLength(server.repl_scriptcache_fifo) != 0)
    {
        replicationScriptCacheFlush();
    }

    /* Refresh the number of slaves with lag <= min-slaves-max-lag. */
    // ���·��ϸ����ӳ�ֵ�Ĵӷ�����������
    refreshGoodSlavesCount();
}

