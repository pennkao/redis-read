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
#include <sys/uio.h>
#include <math.h>

static void setProtocolError(redisClient *c, int pos);

/* To evaluate the output buffer size of a client we need to get size of
 * allocated objects, however we can't used zmalloc_size() directly on sds
 * strings because of the trick they use to work (the header is before the
 * returned pointer), so we use this helper function. */
// ��������������Ĵ�С 
size_t zmalloc_size_sds(sds s) {
    return zmalloc_size(s-sizeof(struct sdshdr));
}

/* Return the amount of memory used by the sds string at object->ptr
 * for a string object. */
// ���� object->ptr ��ָ����ַ���������ʹ�õ��ڴ�������
size_t getStringObjectSdsUsedMemory(robj *o) {
    redisAssertWithInfo(NULL,o,o->type == REDIS_STRING);
    switch(o->encoding) {
    case REDIS_ENCODING_RAW: return zmalloc_size_sds(o->ptr); //����o->ptrָ��Ŀռ��С
    case REDIS_ENCODING_EMBSTR: return sdslen(o->ptr);
    default: return 0; /* Just integer encoding for now. */
    }
}

/*
 * �ظ����ݸ��ƺ���
 */
void *dupClientReplyValue(void *o) {
    incrRefCount((robj*)o);
    return o;
}

/*
 * ����ģʽ�ԱȺ���
 */
int listMatchObjects(void *a, void *b) {
    return equalStringObjects(a,b);
}

/*
 * ����һ���¿ͻ���
 */
redisClient *createClient(int fd) { //createClient���� ��ȫ��ͬ����ɺ󣬱�����һ��client��������������ʵʱKV

    // ����ռ�
    redisClient *c = zmalloc(sizeof(redisClient));

    /* passing -1 as fd it is possible to create a non connected client.
     * This is useful since all the Redis commands needs to be executed
     * in the context of a client. When commands are executed in other
     * contexts (for instance a Lua script) we need a non connected client. */
    // �� fd ��Ϊ -1 ʱ���������������ӵĿͻ���
    // ��� fd Ϊ -1 ����ô�������������ӵ�α�ͻ���
    // ��Ϊ Redis ����������ڿͻ��˵���������ʹ�ã�������ִ�� Lua �����е�����ʱ
    // ��Ҫ�õ�����α�ն�
    if (fd != -1) {
        // ������
        anetNonBlock(NULL,fd);
        // ���� Nagle �㷨
        anetEnableTcpNoDelay(NULL,fd);
        // ���� keep alive
        if (server.tcpkeepalive)
            anetKeepAlive(NULL,fd,server.tcpkeepalive);
        // �󶨶��¼����¼� loop ����ʼ������������ //accept���յ��ͻ������ӵ�ʱ����øú�����fd�����¼�����
        if (aeCreateFileEvent(server.el,fd,AE_READABLE,
            readQueryFromClient, c) == AE_ERR)
        {
            close(fd);
            zfree(c);
            return NULL;
        }
    }

    // ��ʼ����������

    // Ĭ�����ݿ�
    selectDb(c,0); //Ĭ��ѡ��select 0
    // �׽���
    c->fd = fd;
    // ����
    c->name = NULL;
    // �ظ���������ƫ����
    c->bufpos = 0;
    // ��ѯ������
    c->querybuf = sdsempty();
    // ��ѯ��������ֵ
    c->querybuf_peak = 0;
    // �������������
    c->reqtype = 0;
    // �����������
    c->argc = 0;
    // �������
    c->argv = NULL;
    // ��ǰִ�е���������һ��ִ�е�����
    c->cmd = c->lastcmd = NULL;
    // ��ѯ��������δ�����������������
    c->multibulklen = 0;
    // ����Ĳ����ĳ���
    c->bulklen = -1;
    // �ѷ����ֽ���
    c->sentlen = 0;
    // ״̬ FLAG
    c->flags = 0;
    // ����ʱ������һ�λ���ʱ��
    c->ctime = c->lastinteraction = server.unixtime;
    // ��֤״̬
    c->authenticated = 0;
    // ����״̬
    c->replstate = REDIS_REPL_NONE;
    // ����ƫ����
    c->reploff = 0;
    // ͨ�� ACK ������յ���ƫ����
    c->repl_ack_off = 0;
    // ͨ�� AKC ������յ�ƫ������ʱ��
    c->repl_ack_time = 0;
    // �ͻ���Ϊ�ӷ�����ʱʹ�ã���¼�˴ӷ�������ʹ�õĶ˿ں�
    c->slave_listening_port = 0;
    // �ظ�����
    c->reply = listCreate();
    // �ظ�������ֽ���
    c->reply_bytes = 0;
    // �ظ���������С�ﵽ�����Ƶ�ʱ��
    c->obuf_soft_limit_reached_time = 0;
    // �ظ�������ͷź͸��ƺ���
    listSetFreeMethod(c->reply,decrRefCountVoid);
    listSetDupMethod(c->reply,dupClientReplyValue);
    // ��������
    c->btype = REDIS_BLOCKED_NONE;
    // ������ʱ
    c->bpop.timeout = 0;
    // ��ɿͻ����������б��
    c->bpop.keys = dictCreate(&setDictType,NULL);
    // �ڽ������ʱ��Ԫ�����뵽 target ָ���ļ���
    // BRPOPLPUSH ����ʱʹ��
    c->bpop.target = NULL;
    c->bpop.numreplicas = 0;
    c->bpop.reploffset = 0;
    c->woff = 0;
    // ��������ʱ���ӵļ�
    c->watched_keys = listCreate();
    // ���ĵ�Ƶ����ģʽ
    c->pubsub_channels = dictCreate(&setDictType,NULL);
    c->pubsub_patterns = listCreate();
    c->peerid = NULL;
    listSetFreeMethod(c->pubsub_patterns,decrRefCountVoid);
    listSetMatchMethod(c->pubsub_patterns,listMatchObjects);
    // �������α�ͻ��ˣ���ô��ӵ��������Ŀͻ���������
    if (fd != -1) listAddNodeTail(server.clients,c);
    // ��ʼ���ͻ��˵�����״̬
    initClientMultiState(c);

    // ���ؿͻ���
    return c;
}

/* This function is called every time we are going to transmit new data
 * to the client. The behavior is the following:
 *
 * ���������ÿ����ͻ��˷�������ʱ���ᱻ���á���������Ϊ���£�
 *
 * If the client should receive new data (normal clients will) the function
 * returns REDIS_OK, and make sure to install the write handler in our event
 * loop so that when the socket is writable new data gets written.
 *
 * ���ͻ��˿��Խ���������ʱ��ͨ������¶������������������� REDIS_OK ��
 * ����д��������write handler����װ���¼�ѭ���У�
 * �������׽��ֿ�дʱ�������ݾͻᱻд�롣
 *
 * If the client should not receive new data, because it is a fake client,
 * a master, a slave not yet online, or because the setup of the write handler
 * failed, the function returns REDIS_ERR.
 *
 * ������Щ��Ӧ�ý��������ݵĿͻ��ˣ�
 * ����α�ͻ��ˡ� master �Լ� δ ONLINE �� slave ��
 * ����д��������װʧ��ʱ��
 * �������� REDIS_ERR ��
 *
 * Typically gets called every time a reply is built, before adding more
 * data to the clients output buffers. If the function returns REDIS_ERR no
 * data should be appended to the output buffers. 
 *
 * ͨ����ÿ���ظ�������ʱ���ã������������ REDIS_ERR ��
 * ��ôû�����ݻᱻ׷�ӵ������������
 */
int prepareClientToWrite(redisClient *c) {

    // LUA �ű�������ʹ�õ�α�ͻ������ǿ�д��
    if (c->flags & REDIS_LUA_CLIENT) return REDIS_OK;
    
    // �ͻ����������������Ҳ����ܲ�ѯ��
    // ��ô���ǲ���д�ģ�����
    if ((c->flags & REDIS_MASTER) &&
        !(c->flags & REDIS_MASTER_FORCE_REPLY)) return REDIS_ERR;

    // �����ӵ�α�ͻ������ǲ���д��
    if (c->fd <= 0) return REDIS_ERR; /* Fake client */

    // һ�������Ϊ�ͻ����׽��ְ�װд���������¼�ѭ��
    if (c->bufpos == 0 && listLength(c->reply) == 0 &&
        (c->replstate == REDIS_REPL_NONE ||
         c->replstate == REDIS_REPL_ONLINE) &&
        aeCreateFileEvent(server.el, c->fd, AE_WRITABLE,
        sendReplyToClient, c) == AE_ERR) return REDIS_ERR;

    return REDIS_OK;
}

/* Create a duplicate of the last object in the reply list when
 * it is not exclusively owned by the reply list. */
// ���ظ��б��е����һ�����󲢷����ڻظ���һ����ʱ
// �����ö����һ������Ʒ
robj *dupLastObjectIfNeeded(list *reply) {
    robj *new, *cur;
    listNode *ln;
    redisAssert(listLength(reply) > 0);
    ln = listLast(reply);
    cur = listNodeValue(ln);
    if (cur->refcount > 1) {
        new = dupStringObject(cur);
        decrRefCount(cur);
        listNodeValue(ln) = new;
    }
    return listNodeValue(ln);
}

/* -----------------------------------------------------------------------------
 * Low level functions to add more data to output buffers.
 * -------------------------------------------------------------------------- */

/*
 * ���Խ��ظ���ӵ� c->buf ��
 */
int _addReplyToBuffer(redisClient *c, char *s, size_t len) {
    size_t available = sizeof(c->buf)-c->bufpos;

    // ��׼���رտͻ��ˣ������ٷ�������
    if (c->flags & REDIS_CLOSE_AFTER_REPLY) return REDIS_OK;

    /* If there already are entries in the reply list, we cannot
     * add anything more to the static buffer. */
    // �ظ��������Ѿ������ݣ���������ݵ� c->buf ������Ǵ�����
    if (listLength(c->reply) > 0) return REDIS_ERR;

    /* Check that the buffer has enough space available for this string. */
    // �ռ��������
    if (len > available) return REDIS_ERR;

    // �������ݵ� c->buf ����
    memcpy(c->buf+c->bufpos,s,len);
    c->bufpos+=len;

    return REDIS_OK;
}

/*
 * ���ظ�����һ�� SDS ����ӵ� c->reply �ظ�������
 */
void _addReplyObjectToList(redisClient *c, robj *o) {
    robj *tail;

    // �ͻ��˼������رգ������ٷ��ͻظ�
    if (c->flags & REDIS_CLOSE_AFTER_REPLY) return;

    // �������޻���飬ֱ�ӽ�����׷�ӵ�������
    if (listLength(c->reply) == 0) {
        incrRefCount(o);
        listAddNodeTail(c->reply,o);

        // ���������л���飬���Խ��ظ���ӵ�����
        // �����ǰ�Ŀ鲻�����ɻظ��Ļ�����ô�½�һ����
        c->reply_bytes += getStringObjectSdsUsedMemory(o);
    } else {

        // ȡ����β�� SDS
        tail = listNodeValue(listLast(c->reply));

        /* Append to this object when possible. */
        // �����β SDS �����ÿռ���϶���ĳ��ȣ�С�� REDIS_REPLY_CHUNK_BYTES
        // ��ô���¶��������ƴ�ӵ���β SDS ��ĩβ
        if (tail->ptr != NULL &&
            tail->encoding == REDIS_ENCODING_RAW &&
            sdslen(tail->ptr)+sdslen(o->ptr) <= REDIS_REPLY_CHUNK_BYTES)
        {
            c->reply_bytes -= zmalloc_size_sds(tail->ptr);
            tail = dupLastObjectIfNeeded(c->reply);
            // ƴ��
            tail->ptr = sdscatlen(tail->ptr,o->ptr,sdslen(o->ptr));
            c->reply_bytes += zmalloc_size_sds(tail->ptr);

        // ֱ�ӽ�����׷�ӵ�ĩβ
        } else {
            incrRefCount(o);
            listAddNodeTail(c->reply,o);
            c->reply_bytes += getStringObjectSdsUsedMemory(o);
        }
    }

    // ���ظ��������Ĵ�С���������ϵͳ���ƵĻ�����ô�رտͻ���
    asyncCloseClientOnOutputBufferLimitReached(c);
}

/* This method takes responsibility over the sds. When it is no longer
 * needed it will be free'd, otherwise it ends up in a robj. */
// �� _addReplyObjectToList ���ƣ����Ḻ�� SDS ���ͷŹ��ܣ������Ҫ�Ļ���
void _addReplySdsToList(redisClient *c, sds s) {
    robj *tail;

    if (c->flags & REDIS_CLOSE_AFTER_REPLY) {
        sdsfree(s);
        return;
    }

    if (listLength(c->reply) == 0) {
        listAddNodeTail(c->reply,createObject(REDIS_STRING,s));
        c->reply_bytes += zmalloc_size_sds(s);
    } else {
        tail = listNodeValue(listLast(c->reply));

        /* Append to this object when possible. */
        if (tail->ptr != NULL && tail->encoding == REDIS_ENCODING_RAW &&
            sdslen(tail->ptr)+sdslen(s) <= REDIS_REPLY_CHUNK_BYTES)
        {
            c->reply_bytes -= zmalloc_size_sds(tail->ptr);
            tail = dupLastObjectIfNeeded(c->reply);
            tail->ptr = sdscatlen(tail->ptr,s,sdslen(s));
            c->reply_bytes += zmalloc_size_sds(tail->ptr);
            sdsfree(s);
        } else {
            listAddNodeTail(c->reply,createObject(REDIS_STRING,s));
            c->reply_bytes += zmalloc_size_sds(s);
        }
    }
    asyncCloseClientOnOutputBufferLimitReached(c);
}

void _addReplyStringToList(redisClient *c, char *s, size_t len) {
    robj *tail;

    if (c->flags & REDIS_CLOSE_AFTER_REPLY) return;

    if (listLength(c->reply) == 0) {
        // Ϊ�ַ��������ַ�������׷�ӵ��ظ�����ĩβ
        robj *o = createStringObject(s,len);

        listAddNodeTail(c->reply,o);
        c->reply_bytes += getStringObjectSdsUsedMemory(o);
    } else {
        tail = listNodeValue(listLast(c->reply));

        /* Append to this object when possible. */
        if (tail->ptr != NULL && tail->encoding == REDIS_ENCODING_RAW &&
            sdslen(tail->ptr)+len <= REDIS_REPLY_CHUNK_BYTES)
        {
            c->reply_bytes -= zmalloc_size_sds(tail->ptr);
            tail = dupLastObjectIfNeeded(c->reply);
            // ���ַ���ƴ�ӵ�һ�� SDS ֮��
            tail->ptr = sdscatlen(tail->ptr,s,len);
            c->reply_bytes += zmalloc_size_sds(tail->ptr);
        } else {
            // Ϊ�ַ��������ַ�������׷�ӵ��ظ�����ĩβ
            robj *o = createStringObject(s,len);

            listAddNodeTail(c->reply,o);
            c->reply_bytes += getStringObjectSdsUsedMemory(o);
        }
    }
    asyncCloseClientOnOutputBufferLimitReached(c);
}

/* -----------------------------------------------------------------------------
 * Higher level functions to queue data on the client output buffer.
 * The following functions are the ones that commands implementations will call.
 * -------------------------------------------------------------------------- */
//sendReplyToClientΪʵ�ʵ�����write�ĵط������һ�����һ������෢�Ͷ��٣���������
void addReply(redisClient *c, robj *obj) {

    // Ϊ�ͻ��˰�װд���������¼�ѭ��
    if (prepareClientToWrite(c) != REDIS_OK) return;

    /* This is an important place where we can avoid copy-on-write
     * when there is a saving child running, avoiding touching the
     * refcount field of the object if it's not needed.
     *
     * �����ʹ���ӽ��̣���ô�����ܵر����޸Ķ���� refcount ��
     *
     * If the encoding is RAW and there is room in the static buffer
     * we'll be able to send the object to the client without
     * messing with its page. 
     *
     * �������ı���Ϊ RAW �����Ҿ�̬���������пռ�
     * ��ô�Ϳ����ڲ�Ū���ڴ�ҳ������£��������͸��ͻ��ˡ�
     */
    if (sdsEncodedObject(obj)) {
        // ���ȳ��Ը������ݵ� c->buf �У��������Ա����ڴ����
        if (_addReplyToBuffer(c,obj->ptr,sdslen(obj->ptr)) != REDIS_OK)
            // ��� c->buf �еĿռ䲻�����͸��Ƶ� c->reply ������
            // ���ܻ������ڴ����
            _addReplyObjectToList(c,obj);
    } else if (obj->encoding == REDIS_ENCODING_INT) {
        /* Optimization: if there is room in the static buffer for 32 bytes
         * (more than the max chars a 64 bit integer can take as string) we
         * avoid decoding the object and go for the lower level approach. */
        // �Ż������ c->buf ���е��ڻ���� 32 ���ֽڵĿռ�
        // ��ô������ֱ�����ַ�������ʽ���Ƶ� c->buf ��
        if (listLength(c->reply) == 0 && (sizeof(c->buf) - c->bufpos) >= 32) {
            char buf[32];
            int len;

            len = ll2string(buf,sizeof(buf),(long)obj->ptr);
            if (_addReplyToBuffer(c,buf,len) == REDIS_OK)
                return;
            /* else... continue with the normal code path, but should never
             * happen actually since we verified there is room. */
        }
        // ִ�е����������������������ҳ��ȴ��� 32 λ
        // ����ת��Ϊ�ַ���
        obj = getDecodedObject(obj);
        // ���浽������
        if (_addReplyToBuffer(c,obj->ptr,sdslen(obj->ptr)) != REDIS_OK)
            _addReplyObjectToList(c,obj);
        decrRefCount(obj);
    } else {
        redisPanic("Wrong obj->encoding in addReply()");
    }
}

/*
 * �� SDS �е����ݸ��Ƶ��ظ�������
 */
void addReplySds(redisClient *c, sds s) {
    if (prepareClientToWrite(c) != REDIS_OK) {
        /* The caller expects the sds to be free'd. */
        sdsfree(s);
        return;
    }
    if (_addReplyToBuffer(c,s,sdslen(s)) == REDIS_OK) {
        sdsfree(s);
    } else {
        /* This method free's the sds when it is no longer needed. */
        _addReplySdsToList(c,s);
    }
}

/*
 * �� C �ַ����е����ݸ��Ƶ��ظ�������
 */
void addReplyString(redisClient *c, char *s, size_t len) {
    if (prepareClientToWrite(c) != REDIS_OK) return;
    if (_addReplyToBuffer(c,s,len) != REDIS_OK)
        _addReplyStringToList(c,s,len);
}

void addReplyErrorLength(redisClient *c, char *s, size_t len) {
    addReplyString(c,"-ERR ",5);
    addReplyString(c,s,len);
    addReplyString(c,"\r\n",2);
}

/*
 * ����һ������ظ�
 *
 * ���� -ERR unknown command 'foobar'
 */
void addReplyError(redisClient *c, char *err) {
    addReplyErrorLength(c,err,strlen(err));
}

void addReplyErrorFormat(redisClient *c, const char *fmt, ...) {
    size_t l, j;
    va_list ap;
    va_start(ap,fmt);
    sds s = sdscatvprintf(sdsempty(),fmt,ap);
    va_end(ap);
    /* Make sure there are no newlines in the string, otherwise invalid protocol
     * is emitted. */
    l = sdslen(s);
    for (j = 0; j < l; j++) {
        if (s[j] == '\r' || s[j] == '\n') s[j] = ' ';
    }
    addReplyErrorLength(c,s,sdslen(s));
    sdsfree(s);
}

void addReplyStatusLength(redisClient *c, char *s, size_t len) {
    addReplyString(c,"+",1);
    addReplyString(c,s,len);
    addReplyString(c,"\r\n",2);
}

/*
 * ����һ��״̬�ظ�
 *
 * ���� +OK\r\n
 */
void addReplyStatus(redisClient *c, char *status) {
    addReplyStatusLength(c,status,strlen(status));
}

void addReplyStatusFormat(redisClient *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    sds s = sdscatvprintf(sdsempty(),fmt,ap);
    va_end(ap);
    addReplyStatusLength(c,s,sdslen(s));
    sdsfree(s);
}

/* Adds an empty object to the reply list that will contain the multi bulk
 * length, which is not known when this function is called. */
// ������ Multi Bulk �ظ�ʱ���ȴ���һ���յ�����֮������ʵ�ʵĻظ������
void *addDeferredMultiBulkLength(redisClient *c) {
    /* Note that we install the write event here even if the object is not
     * ready to be sent, since we are sure that before returning to the
     * event loop setDeferredMultiBulkLength() will be called. */
    if (prepareClientToWrite(c) != REDIS_OK) return NULL;
    listAddNodeTail(c->reply,createObject(REDIS_STRING,NULL));
    return listLast(c->reply);
}

/* Populate the length object and try gluing it to the next chunk. */
// ���� Multi Bulk �ظ��ĳ���
void setDeferredMultiBulkLength(redisClient *c, void *node, long length) {
    listNode *ln = (listNode*)node;
    robj *len, *next;

    /* Abort when *node is NULL (see addDeferredMultiBulkLength). */
    if (node == NULL) return;

    len = listNodeValue(ln);
    len->ptr = sdscatprintf(sdsempty(),"*%ld\r\n",length);
    len->encoding = REDIS_ENCODING_RAW; /* in case it was an EMBSTR. */
    c->reply_bytes += zmalloc_size_sds(len->ptr);
    if (ln->next != NULL) {
        next = listNodeValue(ln->next);

        /* Only glue when the next node is non-NULL (an sds in this case) */
        if (next->ptr != NULL) {
            c->reply_bytes -= zmalloc_size_sds(len->ptr);
            c->reply_bytes -= getStringObjectSdsUsedMemory(next);
            len->ptr = sdscatlen(len->ptr,next->ptr,sdslen(next->ptr));
            c->reply_bytes += zmalloc_size_sds(len->ptr);
            listDelNode(c->reply,ln->next);
        }
    }
    asyncCloseClientOnOutputBufferLimitReached(c);
}

/* Add a double as a bulk reply */
/*
 * �� bulk �ظ�����ʽ������һ��˫���ȸ�����
 *
 * ���� $4\r\n3.14\r\n
 */
void addReplyDouble(redisClient *c, double d) {
    char dbuf[128], sbuf[128];
    int dlen, slen;
    if (isinf(d)) {
        /* Libc in odd systems (Hi Solaris!) will format infinite in a
         * different way, so better to handle it in an explicit way. */
        addReplyBulkCString(c, d > 0 ? "inf" : "-inf");
    } else {
        dlen = snprintf(dbuf,sizeof(dbuf),"%.17g",d);
        slen = snprintf(sbuf,sizeof(sbuf),"$%d\r\n%s\r\n",dlen,dbuf);
        addReplyString(c,sbuf,slen);
    }
}

/* Add a long long as integer reply or bulk len / multi bulk count.
 * 
 * ���һ�� long long Ϊ�����ظ������� bulk �� multi bulk ����Ŀ
 *
 * Basically this is used to output <prefix><long long><crlf>. 
 *
 * �����ʽΪ <prefix><long long><crlf>
 *
 * ����:
 *
 * *5\r\n10086\r\n
 *
 * $5\r\n10086\r\n
 */
void addReplyLongLongWithPrefix(redisClient *c, long long ll, char prefix) {
    char buf[128];
    int len;

    /* Things like $3\r\n or *2\r\n are emitted very often by the protocol
     * so we have a few shared objects to use if the integer is small
     * like it is most of the times. */
    if (prefix == '*' && ll < REDIS_SHARED_BULKHDR_LEN) {
        // ���������ظ�
        addReply(c,shared.mbulkhdr[ll]);
        return;
    } else if (prefix == '$' && ll < REDIS_SHARED_BULKHDR_LEN) {
        // �����ظ�
        addReply(c,shared.bulkhdr[ll]);
        return;
    }

    buf[0] = prefix;
    len = ll2string(buf+1,sizeof(buf)-1,ll);
    buf[len+1] = '\r';
    buf[len+2] = '\n';
    addReplyString(c,buf,len+3);
}

/*
 * ����һ�������ظ�
 * 
 * ��ʽΪ :10086\r\n
 */
void addReplyLongLong(redisClient *c, long long ll) {
    if (ll == 0)
        addReply(c,shared.czero);
    else if (ll == 1)
        addReply(c,shared.cone);
    else
        addReplyLongLongWithPrefix(c,ll,':');
}

void addReplyMultiBulkLen(redisClient *c, long length) {
    if (length < REDIS_SHARED_BULKHDR_LEN)
        addReply(c,shared.mbulkhdr[length]);
    else
        addReplyLongLongWithPrefix(c,length,'*');
}

/* Create the length prefix of a bulk reply, example: $2234 */
void addReplyBulkLen(redisClient *c, robj *obj) {
    size_t len;

    if (sdsEncodedObject(obj)) {
        len = sdslen(obj->ptr);
    } else {
        long n = (long)obj->ptr;

        /* Compute how many bytes will take this integer as a radix 10 string */
        len = 1;
        if (n < 0) {
            len++;
            n = -n;
        }
        while((n = n/10) != 0) {
            len++;
        }
    }

    if (len < REDIS_SHARED_BULKHDR_LEN)
        addReply(c,shared.bulkhdr[len]);
    else
        addReplyLongLongWithPrefix(c,len,'$');
}

/* Add a Redis Object as a bulk reply 
 *
 * ����һ�� Redis ������Ϊ�ظ�
 */
void addReplyBulk(redisClient *c, robj *obj) {
    addReplyBulkLen(c,obj);
    addReply(c,obj);
    addReply(c,shared.crlf);
}

/* Add a C buffer as bulk reply 
 *
 * ����һ�� C ��������Ϊ�ظ�
 */
void addReplyBulkCBuffer(redisClient *c, void *p, size_t len) {
    addReplyLongLongWithPrefix(c,len,'$');
    addReplyString(c,p,len);
    addReply(c,shared.crlf);
}

/* Add a C nul term string as bulk reply 
 *
 * ����һ�� C �ַ�����Ϊ�ظ�
 */
void addReplyBulkCString(redisClient *c, char *s) {
    if (s == NULL) {
        addReply(c,shared.nullbulk);
    } else {
        addReplyBulkCBuffer(c,s,strlen(s));
    }
}

/* Add a long long as a bulk reply 
 *
 * ����һ�� long long ֵ��Ϊ�ظ�
 */
void addReplyBulkLongLong(redisClient *c, long long ll) {
    char buf[64];
    int len;

    len = ll2string(buf,64,ll);
    addReplyBulkCBuffer(c,buf,len);
}

/* Copy 'src' client output buffers into 'dst' client output buffers.
 * The function takes care of freeing the old output buffers of the
 * destination client. */
// �ͷ� dst �ͻ���ԭ�е�������ݣ����� src �ͻ��˵�������ݸ��Ƹ� dst
void copyClientOutputBuffer(redisClient *dst, redisClient *src) {

    // �ͷ� dst ԭ�еĻظ�����
    listRelease(dst->reply);
    // ���������� dst
    dst->reply = listDup(src->reply);

    // �������ݵ��ظ� buf
    memcpy(dst->buf,src->buf,src->bufpos);

    // ͬ��ƫ�������ֽ���
    dst->bufpos = src->bufpos;
    dst->reply_bytes = src->reply_bytes;
}

/*
 * TCP ���� accept ������
 */
#define MAX_ACCEPTS_PER_CALL 1000
static redisClient* acceptCommonHandler(int fd, int flags) { //accept���յ��ͻ������ӵ�ʱ�����

    // �����ͻ���
    redisClient *c;
    if ((c = createClient(fd)) == NULL) {
        redisLog(REDIS_WARNING,
            "Error registering fd event for the new client: %s (fd=%d)",
            strerror(errno),fd);
        close(fd); /* May be already closed, just ignore errors */
        return NULL;
    }

    /* If maxclient directive is set and this is one client more... close the
     * connection. Note that we create the client instead to check before
     * for this condition, since now the socket is already set in non-blocking
     * mode and we can send an error for free using the Kernel I/O */
    // �������ӵĿͻ���������������ͻ��������ﵽ��
    // ��ô���¿ͻ���д�������Ϣ�����ر��¿ͻ���
    // �ȴ����ͻ��ˣ��ٽ������������Ϊ�˷���ؽ��д�����Ϣд��
    if (listLength(server.clients) > server.maxclients) {
        char *err = "-ERR max number of clients reached\r\n";

        /* That's a best effort error message, don't check write errors */
        if (write(c->fd,err,strlen(err)) == -1) {
            /* Nothing to do, Just to avoid the warning... */
        }
        // ���¾ܾ�������
        server.stat_rejected_conn++;
        freeClient(c, NGX_FUNC_LINE);
        return NULL;
    }

    // �������Ӵ���
    server.stat_numconnections++;

    // ���� FLAG
    c->flags |= flags;

    return c;
}

/* 
 * ����һ�� TCP ���Ӵ�����   
 */ //����TCP������acceptTcpHandler���ر����Ӳ��ͷ���Դ��freeClient    ��ȡ������readQueryFromClient
void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    int cport, cfd, max = MAX_ACCEPTS_PER_CALL;
    char cip[REDIS_IP_STR_LEN];
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);
    REDIS_NOTUSED(privdata);
    redisClient *c;

    while(max--) {
        // accept �ͻ�������
        cfd = anetTcpAccept(server.neterr, fd, cip, sizeof(cip), &cport);
        if (cfd == ANET_ERR) {
            if (errno != EWOULDBLOCK)
                redisLog(REDIS_WARNING,
                    "Accepting client connection: %s", server.neterr);
            return;
        }
        
       // snprintf()
        // Ϊ�ͻ��˴����ͻ���״̬��redisClient��
        c = acceptCommonHandler(cfd,0);
        if(c != NULL) {
            snprintf(c->cip, sizeof(c->cip), "%s", cip);
            c->cport = cport;
        }

        redisLog(REDIS_VERBOSE,"Accepted %s:%d  %s:%d ", cip, cport, c->cip, c->cport);
    }
}

/*
 * ����һ���������Ӵ�����
 */
void acceptUnixHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    int cfd, max = MAX_ACCEPTS_PER_CALL;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);
    REDIS_NOTUSED(privdata);

    while(max--) {
        // accept ���ؿͻ�������
        cfd = anetUnixAccept(server.neterr, fd);
        if (cfd == ANET_ERR) {
            if (errno != EWOULDBLOCK)
                redisLog(REDIS_WARNING,
                    "Accepting client connection: %s", server.neterr);
            return;
        }
        redisLog(REDIS_VERBOSE,"Accepted connection to %s", server.unixsocket);
        // Ϊ���ؿͻ��˴����ͻ���״̬
        acceptCommonHandler(cfd,REDIS_UNIX_SOCKET);
    }
}

/*
 * ��������������
 */
static void freeClientArgv(redisClient *c) {
    int j;
    for (j = 0; j < c->argc; j++)
        decrRefCount(c->argv[j]);
    c->argc = 0;
    c->cmd = NULL;
}

/* Close all the slaves connections. This is useful in chained replication
 * when we resync with our own master and want to force all our slaves to
 * resync with us as well. */
// �Ͽ����дӷ����������ӣ�ǿ�����дӷ�����ִ����ͬ��
void disconnectSlaves(void) {
    while (listLength(server.slaves)) {
        listNode *ln = listFirst(server.slaves);
        freeClient((redisClient*)ln->value, NGX_FUNC_LINE);
    }
}

/* This function is called when the slave lose the connection with the
 * master into an unexpected way. */
// ��������ڴӷ���������غ���������ʧȥ��ϵʱ����
void replicationHandleMasterDisconnection(void) { 
//�ӷ������������������ӶϿ����ӷ��������Ĵ�����:�п��ܸôӷ��������滹�дӷ���������Ѹôӷ�������������дӷ��������ӶϿ�
    server.master = NULL;
    server.repl_state = REDIS_REPL_CONNECT;
    server.repl_down_since = server.unixtime;
    /* We lost connection with our master, force our slaves to resync
     * with us as well to load the new data set.
     *
     * ����������ʧ����ǿ����������������Ĵӷ����� resync ��
     * �ȴ����������ݡ�
     *
     * If server.masterhost is NULL the user called SLAVEOF NO ONE so
     * slave resync is not needed. 
     *
     * ��� masterhost �����ڣ���ô�������أ���
     * ��ô���� SLAVEOF NO ONE ������ slave resync
     */
    if (server.masterhost != NULL) disconnectSlaves(); //
}

/*
һ����ͨ�ͻ��˿�����Ϊ����ԭ������رգ�
����ͻ��˽����˳����߱�ɱ������ô�ͻ����������֮����������ӽ����رգ��Ӷ���ɿͻ��˱��رա�
����ͻ���������������˴��в�����Э���ʽ������������ô����ͻ���Ҳ�ᱻ�������رա�
����ͻ��˳�Ϊ��CLIENTKtLL�����Ŀ�꣬��ô��Ҳ�ᱻ�رա�
����û�Ϊ������������timeout����ѡ���ô���ͻ��˵Ŀ�תʱ�䳬��timeoutѡ�����õ�ֵʱ���ͻ��˽����رա�����timeoutѡ����һЩ������
    ��������ͻ�������������������REDIS_MASTER��־�����ӷ�����������REDIS_SLAVE��־�������ڱ�BLPOP����������������REDIS_BLOCKED��
    ־������������ִ��SUBSCRIBE��PSUBSCRIBE�ȶ��������ô��ʹ�ͻ��˵Ŀ�תʱ�䳬����timeoutѡ���ֵ���ͻ���Ҳ���ᱻ�������رա�
����ͻ��˷��͵���������Ĵ�С���������뻺���������ƴ�С��Ĭ��Ϊl GB������ô����ͻ��˻ᱻ�������رա�
���Ҫ���͸��ͻ��˵�����ظ��Ĵ�С��������������������ƴ�С����ô����ͻ��˻ᱻ�������رա�




  �����������ʱ���ᵽ�����ɱ��С��������һ��������������ַ���������ɣ���������˵��������������Ա������ⳤ������ظ���
  ���ǣ�Ϊ�˱���ͻ��˵Ļظ�����ռ�ù���ķ�������Դ����������ʱ�̼��ͻ��˵�����������Ĵ�С�����ڻ������Ĵ�С������Χʱ��
  ִ����Ӧ�����Ʋ�����
    ������ʹ������ģʽ�����ƿͻ�������������Ĵ�С��
    Ӳ������( hard limit)���������������Ĵ�С������Ӳ�����������õĴ�С����ô�����������رտͻ��ˡ�
    ��������( soft limit)���������������Ĵ�С�������������������õĴ�С������û����Ӳ�����ƣ���ô��������ʹ�ÿͻ���״̬�ṹ��
    obuf_soft_limit_reached_time���Լ�¼�¿ͻ��˵����������Ƶ���ʼʱ�䣻֮���������������ӿͻ��ˣ��������������Ĵ�Сһֱ����
    �������ƣ����ҳ���ʱ�䳬���������趨��ʱ������ô���������رտͻ��ˣ��෴�أ��������������Ĵ�С��ָ��ʱ�䷦�ڣ����ٳ����������ƣ�
    ��ô�ͻ��˾Ͳ��ᱻ�رգ�����obuf_soft_limit_reached_time���Ե�ֵҲ�ᱻ���㡣client-output-buffer-limit��������

     ʹ��client-output-buffer-limitѡ�����Ϊ��ͨ�ͻ��ˡ��ӷ������ͻ��ˡ�ִ�з����붩�Ĺ��ܵĿͻ��˷ֱ����ò�ͬ���������ƺ�Ӳ�����ƣ���ѡ��ĸ�ʽΪ��
    client-output-buffer-limit <class><hard limit> <soft limit> <soft seconds>
    ��������������ʾ����
    client-output-buffer-limit normal 0 0 0
    client-output-buffer-limit slave 256mb 64mb 60
    cliant-output-buffer-limit pubsub 32mb 8mb 60
    ��һ�����ý���ͨ�ͻ��˵�Ӳ�����ƺ��������ƶ�����ΪO����ʾ�����ƿͻ��˵������������С��
    �ڶ������ý��ӷ������ͻ��˵�Ӳ����������Ϊ256 MB����������������Ϊ64 MB���������Ƶ�ʱ��Ϊ60�롣
    ���������ý�ִ�з����붩�Ĺ��ܵĿͻ��˵�Ӳ����������Ϊ32 MB��������������Ϊ8 MB���������Ƶ�ʱ��Ϊ60�롣
*/

/*
 * �ͷſͻ���
 */ //����TCP������acceptTcpHandler���ر����Ӳ��ͷ���Դ��freeClient
 /* �ͷ�freeClient��Ҫ��ΪMaster��Slave2���������ͬ�Ĵ��� */
void freeClient(redisClient *c, const char *func, unsigned int line) {
    listNode *ln;

    redisLog(REDIS_WARNING, "free client ip:%s, port:%d <%s, %d>", c->cip, c->cport, func, line);
    /* If this is marked as current client unset it */
    if (server.current_client == c) server.current_client = NULL;

    /* If it is our master that's beging disconnected we should make sure
     * to cache the state to try a partial resynchronization later.
     *
     * Note that before doing this we make sure that the client is not in
     * some unexpected state, by checking its flags. */
     //�����slave��Ϊmasterͨ��replicationDiscardCachedMaster�ߵ������ʱ��server.master=null����ΪҪ��������if��֧��replicationCacheMaster���������ΪNULL
     //�������֪���������ˣ��������������֧��������ѡ��Ϊ���󣬻�ͨ��replicationDiscardCachedMaster�ߵ�if����ķ�֧
    if (server.master && c->flags & REDIS_MASTER) { //
        redisLog(REDIS_WARNING,"Connection with master lost.");
        if (!(c->flags & (REDIS_CLOSE_AFTER_REPLY|
                          REDIS_CLOSE_ASAP|
                          REDIS_BLOCKED|
                          REDIS_UNBLOCKED))) 
        { //�����Master�ͻ��ˣ���Ҫ������Client�Ĵ�������Ѹ���������ûָ������������ͷ��������
            replicationCacheMaster(c);
            return;
        }
    }

    /* Log link disconnection with slave */
    if ((c->flags & REDIS_SLAVE) && !(c->flags & REDIS_MONITOR)) {
        char ip[REDIS_IP_STR_LEN];

        if (anetPeerToString(c->fd,ip,sizeof(ip),NULL) != -1) {
            redisLog(REDIS_WARNING,"Connection with slave %s:%d lost.",
                ip, c->slave_listening_port);
        }
    }

    /* Free the query buffer */
    sdsfree(c->querybuf);
    c->querybuf = NULL;

    /* Deallocate structures used to block on blocking ops. */
    if (c->flags & REDIS_BLOCKED) unblockClient(c);
    dictRelease(c->bpop.keys);

    /* UNWATCH all the keys */
    // ��� WATCH ��Ϣ
    unwatchAllKeys(c);
    listRelease(c->watched_keys);

    /* Unsubscribe from all the pubsub channels */
    // �˶�����Ƶ����ģʽ
    pubsubUnsubscribeAllChannels(c,0);
    pubsubUnsubscribeAllPatterns(c,0);
    dictRelease(c->pubsub_channels);
    listRelease(c->pubsub_patterns);

    /* Close socket, unregister events, and remove list of replies and
     * accumulated arguments. */
    // �ر��׽��֣������¼���������ɾ�����׽��ֵ��¼�
    if (c->fd != -1) {
        aeDeleteFileEvent(server.el, c->fd, AE_READABLE);
        aeDeleteFileEvent(server.el, c->fd, AE_WRITABLE);
        close(c->fd);
    }

    // ��ջظ�������
    listRelease(c->reply);

    // ����������
    freeClientArgv(c);

    /* Remove from the list of clients */
    // �ӷ������Ŀͻ���������ɾ������
    if (c->fd != -1) {
        ln = listSearchKey(server.clients,c);
        redisAssert(ln != NULL);
        listDelNode(server.clients,ln);
    }

    /* When client was just unblocked because of a blocking operation,
     * remove it from the list of unblocked clients. */
    // ɾ���ͻ��˵�������Ϣ
    if (c->flags & REDIS_UNBLOCKED) {
        ln = listSearchKey(server.unblocked_clients,c);
        redisAssert(ln != NULL);
        listDelNode(server.unblocked_clients,ln);
    }

    /* Master/slave cleanup Case 1:
     * we lost the connection with a slave. */
    if (c->flags & REDIS_SLAVE) {
        if (c->replstate == REDIS_REPL_SEND_BULK) {
            if (c->repldbfd != -1) close(c->repldbfd);
            if (c->replpreamble) sdsfree(c->replpreamble);
        }
        list *l = (c->flags & REDIS_MONITOR) ? server.monitors : server.slaves;
        ln = listSearchKey(l,c);
        redisAssert(ln != NULL);
        listDelNode(l,ln);
        /* We need to remember the time when we started to have zero
         * attached slaves, as after some time we'll free the replication
         * backlog. */
        if (c->flags & REDIS_SLAVE && listLength(server.slaves) == 0)
            server.repl_no_slaves_since = server.unixtime;
        refreshGoodSlavesCount();
    }

    /* Master/slave cleanup Case 2:
     * we lost the connection with the master. */
    if (c->flags & REDIS_MASTER) replicationHandleMasterDisconnection();

    /* If this client was scheduled for async freeing we need to remove it
     * from the queue. */
    if (c->flags & REDIS_CLOSE_ASAP) {
        ln = listSearchKey(server.clients_to_close,c);
        redisAssert(ln != NULL);
        listDelNode(server.clients_to_close,ln);
    }

    /* Release other dynamically allocated client structure fields,
     * and finally release the client structure itself. */
    if (c->name) decrRefCount(c->name);
    // ��������ռ�
    zfree(c->argv);
    // �������״̬��Ϣ
    freeClientMultiState(c);
    sdsfree(c->peerid);
    // �ͷſͻ��� redisClient �ṹ����
    zfree(c);
}

/* Schedule a client to free it at a safe time in the serverCron() function.
 * This function is useful when we need to terminate a client but we are in
 * a context where calling freeClient() is not possible, because the client
 * should be valid for the continuation of the flow of the program. */
// �첽���ͷŸ����Ŀͻ���
void freeClientAsync(redisClient *c) {
    if (c->flags & REDIS_CLOSE_ASAP) return;
    c->flags |= REDIS_CLOSE_ASAP;
    listAddNodeTail(server.clients_to_close,c);
}

// �ر���Ҫ�첽�رյĿͻ���
void freeClientsInAsyncFreeQueue(void) {
    
    // ��������Ҫ�رյĿͻ���
    while (listLength(server.clients_to_close)) {
        listNode *ln = listFirst(server.clients_to_close);
        redisClient *c = listNodeValue(ln);

        c->flags &= ~REDIS_CLOSE_ASAP;
        // �رտͻ���
        freeClient(c, NGX_FUNC_LINE);
        // �ӿͻ���������ɾ�����رյĿͻ���
        listDelNode(server.clients_to_close,ln);
    }
}

/*
 * ����������ظ���д������
 */ //readQueryFromClient��sendReplyToClient��Ӧ��һ�����գ�һ������  //����TCP������acceptTcpHandler
 //��������������һ���Է���64M���������ݹ�������
void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    redisClient *c = privdata;
    int nwritten = 0, totwritten = 0, objlen;
    size_t objmem;
    robj *o;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);

    // һֱѭ����ֱ���ظ�������Ϊ��
    // ����ָ����������Ϊֹ
    while(c->bufpos > 0 || listLength(c->reply)) {

        if (c->bufpos > 0) {

            // c->bufpos > 0

            // д�����ݵ��׽���
            // c->sentlen ���������� short write ��
            // ������ short write ������д��δ��һ�����ʱ��
            // c->buf+c->sentlen �ͻ�ƫ�Ƶ���ȷ��δд�룩���ݵ�λ���ϡ�
            nwritten = write(fd,c->buf+c->sentlen,c->bufpos-c->sentlen);
            // ����������
            if (nwritten <= 0) break;
            // �ɹ�д�������д�����������
            c->sentlen += nwritten;
            totwritten += nwritten;

            /* If the buffer was sent, set bufpos to zero to continue with
             * the remainder of the reply. */
            // ����������е������Ѿ�ȫ��д�����
            // ��ô��տͻ��˵���������������
            if (c->sentlen == c->bufpos) {
                c->bufpos = 0;
                c->sentlen = 0;
            }
        } else {

            // listLength(c->reply) != 0

            // ȡ��λ��������ǰ��Ķ���
            o = listNodeValue(listFirst(c->reply));
            objlen = sdslen(o->ptr);
            objmem = getStringObjectSdsUsedMemory(o);

            // �Թ��ն���
            if (objlen == 0) {
                listDelNode(c->reply,listFirst(c->reply));
                c->reply_bytes -= objmem;
                continue;
            }

            // д�����ݵ��׽���
            // c->sentlen ���������� short write ��
            // ������ short write ������д��δ��һ�����ʱ��
            // c->buf+c->sentlen �ͻ�ƫ�Ƶ���ȷ��δд�룩���ݵ�λ���ϡ�
            nwritten = write(fd, ((char*)o->ptr)+c->sentlen,objlen-c->sentlen);
            // д�����������
            if (nwritten <= 0) break;
            // �ɹ�д�������д�����������
            c->sentlen += nwritten;
            totwritten += nwritten;

            /* If we fully sent the object on head go to the next one */
            // �������������ȫ��д����ϣ���ôɾ����д����ϵĽڵ�
            if (c->sentlen == objlen) {
                listDelNode(c->reply,listFirst(c->reply));
                c->sentlen = 0;
                c->reply_bytes -= objmem;
            }
        }
        /* Note that we avoid to send more than REDIS_MAX_WRITE_PER_EVENT
         * bytes, in a single threaded server it's a good idea to serve
         * other clients as well, even if a very large request comes from
         * super fast link that is always able to accept data (in real world
         * scenario think about 'KEYS *' against the loopback interface).
         *
         * Ϊ�˱���һ���ǳ���Ļظ���ռ��������
         * ��д������������� REDIS_MAX_WRITE_PER_EVENT ��
         * ��ʱ�ж�д�룬������ʱ���ø������ͻ��ˣ�
         * ʣ������ݵ��´�д������ټ���д��
         *
         * However if we are over the maxmemory limit we ignore that and
         * just deliver as much data as it is possible to deliver. 
         *
         * ������������������ڴ�ռ���Ѿ����������ƣ�
         * ��ôΪ�˽��ظ��������е����ݾ���д����ͻ��ˣ�
         * Ȼ���ͷŻظ��������Ŀռ��������ڴ棬
         * ��ʱ��ʹд���������� REDIS_MAX_WRITE_PER_EVENT ��
         * ����Ҳ��������д��
         */
        if (totwritten > REDIS_MAX_WRITE_PER_EVENT &&  //���д64M
            (server.maxmemory == 0 ||
             zmalloc_used_memory() < server.maxmemory)) break;
    }

    // д�������
    if (nwritten == -1) {
        if (errno == EAGAIN) {
            nwritten = 0;
        } else {
            redisLog(REDIS_VERBOSE,
                "Error writing to client: %s", strerror(errno));
            freeClient(c, NGX_FUNC_LINE);
            return;
        }
    }

    if (totwritten > 0) {
        /* For clients representing masters we don't count sending data
         * as an interaction, since we always send REPLCONF ACK commands
         * that take some time to just fill the socket output buffer.
         * We just rely on data / pings received for timeout detection. */
        if (!(c->flags & REDIS_MASTER)) c->lastinteraction = server.unixtime;
    }
    if (c->bufpos == 0 && listLength(c->reply) == 0) {
        c->sentlen = 0;

        // ɾ�� write handler
        aeDeleteFileEvent(server.el,c->fd,AE_WRITABLE);

        /* Close connection after entire reply has been sent. */
        // ���ָ����д��֮��رտͻ��� FLAG ����ô�رտͻ���
        if (c->flags & REDIS_CLOSE_AFTER_REPLY) freeClient(c, NGX_FUNC_LINE);
    }
}

/* resetClient prepare the client to process the next command */
// �ڿͻ���ִ��������֮��ִ�У����ÿͻ�����׼��ִ���¸�����
void resetClient(redisClient *c) {
    redisCommandProc *prevcmd = c->cmd ? c->cmd->proc : NULL;

    freeClientArgv(c);
    c->reqtype = 0;
    c->multibulklen = 0;
    c->bulklen = -1;
    /* We clear the ASKING flag as well if we are not inside a MULTI, and
     * if what we just executed is not the ASKING command itself. */
    if (!(c->flags & REDIS_MULTI) && prevcmd != askingCommand)
        c->flags &= (~REDIS_ASKING);
}

/*
 * �������������������������
 *
 * ��������ĸ��������Կո�ֿ������� \r\n ��β
 * ���ӣ�
 *
 * <arg0> <arg1> <arg...> <argN>\r\n
 *
 * ��Щ���ݻᱻ���ڴ�����������
 * ����
 *
 * argv[0] = arg0
 * argv[1] = arg1
 * argv[2] = arg2
 */
int processInlineBuffer(redisClient *c) {
    char *newline;
    int argc, j;
    sds *argv, aux;
    size_t querylen;

    /* Search for end of line */
    newline = strchr(c->querybuf,'\n');

    /* Nothing to do without a \r\n */
    // �յ��Ĳ�ѯ���ݲ�����Э���ʽ������
    if (newline == NULL) {
        if (sdslen(c->querybuf) > REDIS_INLINE_MAX_SIZE) {
            addReplyError(c,"Protocol error: too big inline request");
            setProtocolError(c,0);
        }
        return REDIS_ERR;
    }

    /* Handle the \r\n case. */
    if (newline && newline != c->querybuf && *(newline-1) == '\r')
        newline--;

    /* Split the input buffer up to the \r\n */
    // ���ݿո񣬷ָ�����Ĳ���
    // ����˵ SET msg hello \r\n ���ָ�Ϊ
    // argv[0] = SET
    // argv[1] = msg
    // argv[2] = hello
    // argc = 3
    querylen = newline-(c->querybuf);
    aux = sdsnewlen(c->querybuf,querylen);
    argv = sdssplitargs(aux,&argc);
    sdsfree(aux);
    if (argv == NULL) {
        addReplyError(c,"Protocol error: unbalanced quotes in request");
        setProtocolError(c,0);
        return REDIS_ERR;
    }

    /* Newline from slaves can be used to refresh the last ACK time.
     * This is useful for a slave to ping back while loading a big
     * RDB file. */
    //�ӷ�������rdbȫ��ͬ����ɵ�ʱ�򣬻�������豸�����ȫ��KV�������������̣�Ϊ�˱�����ʱ��master�Ѹ�slave�ж�Ϊ��ʱ�������replicationEmptyDbCallback
    if (querylen == 0 && c->flags & REDIS_SLAVE)
        c->repl_ack_time = server.unixtime; 
   

    /* Leave data after the first line of the query in the buffer */

    // �ӻ�������ɾ���� argv �Ѷ�ȡ������
    // ʣ���������δ��ȡ��
    sdsrange(c->querybuf,querylen+2,-1);

    /* Setup argv array on client structure */
    // Ϊ�ͻ��˵Ĳ�������ռ�
    if (c->argv) zfree(c->argv);
    c->argv = zmalloc(sizeof(robj*)*argc);

    /* Create redis objects for all arguments. */
    // Ϊÿ����������һ���ַ�������
    for (c->argc = 0, j = 0; j < argc; j++) {
        if (sdslen(argv[j])) {
            // argv[j] �Ѿ��� SDS ��
            // ���Դ������ַ�������ֱ��ָ��� SDS
            c->argv[c->argc] = createObject(REDIS_STRING,argv[j]);
            c->argc++;
        } else {
            sdsfree(argv[j]);
        }
    }

    zfree(argv);

    return REDIS_OK;
}

/* Helper function. Trims query buffer to make the function that processes
 * multi bulk requests idempotent. */
// ����ڶ���Э������ʱ���������ݲ�����Э�飬��ô�첽�عر�����ͻ��ˡ�
static void setProtocolError(redisClient *c, int pos) {
    if (server.verbosity >= REDIS_VERBOSE) {
        sds client = catClientInfoString(sdsempty(),c);
        redisLog(REDIS_VERBOSE,
            "Protocol error from client: %s", client);
        sdsfree(client);
    }
    c->flags |= REDIS_CLOSE_AFTER_REPLY;
    sdsrange(c->querybuf,pos,-1);
}

/*
 * �� c->querybuf �е�Э������ת���� c->argv �еĲ�������
 * 
 * ���� *3\r\n$3\r\nSET\r\n$3\r\nMSG\r\n$5\r\nHELLO\r\n
 * ����ת��Ϊ��
 * argv[0] = SET
 * argv[1] = MSG
 * argv[2] = HELLO
 */ 
//rdbSaveBackground������ȫ��д��rdb�ļ��У�Ȼ����serverCron->backgroundSaveDoneHandler->updateSlavesWaitingBgsave��ͬ��rdb�ļ����ݵ��ӷ�����

//RDB�ļ����͸�ʽ����ͨ�����ַ���Э���ʽһ����$length\r\n+ʵ�����ݣ�rdb�ļ����ݷ�����updateSlavesWaitingBgsave->sendBulkToSlave��
//rdb��������ͬ��(������ʽ)���ݽ��պ���ΪreadSyncBulkPayload����������(������������ʽ����ͬ��)���ս�����processMultibulkBuffer
int processMultibulkBuffer(redisClient *c) {
    char *newline = NULL;
    int pos = 0, ok;
    long long ll;

    // ��������Ĳ�������
    // ���� *3\r\n$3\r\nSET\r\n... ���� c->multibulklen = 3
    if (c->multibulklen == 0) {
        /* The client should have been reset */
        redisAssertWithInfo(c,NULL,c->argc == 0);

        /* Multi bulk length cannot be read without a \r\n */
        // ��黺���������ݵ�һ�� "\r\n"
        newline = strchr(c->querybuf,'\r');
        if (newline == NULL) {
            if (sdslen(c->querybuf) > REDIS_INLINE_MAX_SIZE) {
                addReplyError(c,"Protocol error: too big mbulk count string");
                setProtocolError(c,0);
            }
            return REDIS_ERR;
        }
        /* Buffer should also contain \n */
        if (newline-(c->querybuf) > ((signed)sdslen(c->querybuf)-2))
            return REDIS_ERR;

        /* We know for sure there is a whole line since newline != NULL,
         * so go ahead and find out the multi bulk length. */
        // Э��ĵ�һ���ַ������� '*'
        redisAssertWithInfo(c,NULL,c->querybuf[0] == '*');
        // ������������Ҳ���� * ֮�� \r\n ֮ǰ������ȡ�������浽 ll ��
        // ������� *3\r\n ����ô ll ������ 3
        ok = string2ll(c->querybuf+1,newline-(c->querybuf+1),&ll);
        // ������������������
        if (!ok || ll > 1024*1024) {
            addReplyError(c,"Protocol error: invalid multibulk length");
            setProtocolError(c,pos);
            return REDIS_ERR;
        }

        // ��������֮���λ��
        // ������� *3\r\n$3\r\n$SET\r\n... ��˵��
        // pos ָ�� *3\r\n$3\r\n$SET\r\n...
        //                ^
        //                |
        //               pos
        pos = (newline-c->querybuf)+2;
        // ��� ll <= 0 ����ô���������һ���հ�����
        // ��ô��������ݴӲ�ѯ��������ɾ����ֻ����δ�Ķ����ǲ�������
        // Ϊʲô���������ǿյ��أ�
        // processInputBuffer ����ע�͵� "Multibulk processing could see a <= 0 length"
        // ����û����ϸ˵��ԭ��
        if (ll <= 0) {
            sdsrange(c->querybuf,pos,-1);
            return REDIS_OK;
        }

        // ���ò�������
        c->multibulklen = ll;

        /* Setup argv array on client structure */
        // ���ݲ���������Ϊ���������������ռ�
        if (c->argv) zfree(c->argv);
        c->argv = zmalloc(sizeof(robj*)*c->multibulklen);
    }

    redisAssertWithInfo(c,NULL,c->multibulklen > 0);

    // �� c->querybuf �ж�������������������������� c->argv
    while(c->multibulklen) {

        /* Read bulk length if unknown */
        // �����������
        if (c->bulklen == -1) {

            // ȷ�� "\r\n" ����
            newline = strchr(c->querybuf+pos,'\r');
            if (newline == NULL) {
                if (sdslen(c->querybuf) > REDIS_INLINE_MAX_SIZE) {
                    addReplyError(c,
                        "Protocol error: too big bulk count string");
                    setProtocolError(c,0);
                    return REDIS_ERR;
                }
                break;
            }
            /* Buffer should also contain \n */
            if (newline-(c->querybuf) > ((signed)sdslen(c->querybuf)-2))
                break;

            //rdb���ݽ��պ���ΪreadSyncBulkPayload���������ݽ��ս�����processMultibulkBuffer

            // ȷ��Э����ϲ�����ʽ��������е� $...
            // ���� $3\r\nSET\r\n
            if (c->querybuf[pos] != '$') {
                addReplyErrorFormat(c,
                    "Protocol error: expected '$', got '%c'",
                    c->querybuf[pos]);
                setProtocolError(c,pos);
                return REDIS_ERR;
            }

            // ��ȡ����
            // ���� $3\r\nSET\r\n ������ ll ��ֵ���� 3
            ok = string2ll(c->querybuf+pos+1,newline-(c->querybuf+pos+1),&ll);
            if (!ok || ll < 0 || ll > 512*1024*1024) {//����key����value�ַ������512M
                addReplyError(c,"Protocol error: invalid bulk length");
                setProtocolError(c,pos);
                return REDIS_ERR;
            }

            // ��λ�������Ŀ�ͷ
            // ���� 
            // $3\r\nSET\r\n...
            //       ^
            //       |
            //      pos
            pos += newline-(c->querybuf+pos)+2;
            // ��������ǳ�������ô��һЩԤ����ʩ���Ż��������Ĳ������Ʋ���
            if (ll >= REDIS_MBULK_BIG_ARG) { //32K
                size_t qblen;

                /* If we are going to read a large object from network
                 * try to make it likely that it will start at c->querybuf
                 * boundary so that we can optimize object creation
                 * avoiding a large copy of data. */
                 //��buf��δ���������ݿ������ڴ�ͷ�������´μ������յ����ݺ��������һ��������Ƿ���������key����value�ַ���processMultibulkBuffer
                sdsrange(c->querybuf,pos,-1);
                
                pos = 0;
                qblen = sdslen(c->querybuf);
                /* Hint the sds library about the amount of bytes this string is
                 * going to contain. */
                if (qblen < ll+2) //ʵ����Ҫll+2�ֽڣ�����querybufֻ������qblen�ֽ����ݣ������Ҫ����ռ�����ȡδ������ֽ���
                    c->querybuf = sdsMakeRoomFor(c->querybuf,ll+2-qblen);
            }
            // �����ĳ���
            c->bulklen = ll; //set key  value�е�key����value�ַ����ĳ���
        }

        /* Read bulk argument */
        // �������
        if (sdslen(c->querybuf)-pos < (unsigned)(c->bulklen+2)) { //˵�����������ݲ���������$200\r\nSET\r\nʵ����Ҫ200�ֽڣ�����ȴֻ������50�ֽڣ���150�ֽ�
            // ȷ�����ݷ���Э���ʽ
            // ���� $3\r\nSET\r\n �ͼ�� SET ֮��� \r\n
            /* Not enough data (+2 == trailing \r\n) */
            break;
        } else {
            // Ϊ���������ַ�������  
            /* Optimization: if the buffer contains JUST our bulk element
             * instead of creating a new object by *copying* the sds we
             * just use the current sds string. */
            if (pos == 0 &&
                c->bulklen >= REDIS_MBULK_BIG_ARG &&
                (signed) sdslen(c->querybuf) == c->bulklen+2) //����ùؼ��ַ�������32K����������洦��ֱ����argv[]ָ���Ӧ��ȡ���ַ������������¿��ٿռ�
            {
                c->argv[c->argc++] = createObject(REDIS_STRING,c->querybuf);
                sdsIncrLen(c->querybuf,-2); /* remove CRLF */
                c->querybuf = sdsempty();
                /* Assume that if we saw a fat argument we'll see another one
                 * likely... */
                c->querybuf = sdsMakeRoomFor(c->querybuf,c->bulklen+2);
                pos = 0;
            } else { //���¿��ٿռ����ѿͻ��˷��͹���ͨ��querybuf���յ��������¿������¿��ٵĿռ䣬�´μ�����querybuf���տͻ�������
                c->argv[c->argc++] =
                    createStringObject(c->querybuf+pos,c->bulklen); //argv[]����ָ���Ӧ��set key value�е�key����value�ַ�������
                pos += c->bulklen+2;
            }

            // ��ղ�������
            c->bulklen = -1;

            // ���ٻ������Ĳ�������
            c->multibulklen--;
        }
    }

    /* Trim to pos */
    // �� querybuf ��ɾ���ѱ���ȡ������
    if (pos) //��buf��δ���������ݿ������ڴ�ͷ�������´μ������յ����ݺ��������һ��������Ƿ���������key����value�ַ��� 
        sdsrange(c->querybuf,pos,-1);

    /* We're done when c->multibulk == 0 */
    // ���������������в������Ѷ�ȡ�꣬��ô����
    if (c->multibulklen == 0) return REDIS_OK;

    /* Still not read to process the command */
    // ������в���δ��ȡ�꣬��ô��Э�������д�
    return REDIS_ERR; 
//����ͻ������õ�set  key value�е�key����value�ַ���̫�������뼸M,����Э���ʽ����󣬿�����Ҫ���read���ܶ�ȡ��һ��������key����value,���ظ�ֵ����ʾ��Ҫ������������
}

// ����ͻ����������������
void processInputBuffer(redisClient *c) {

    /* Keep processing while there is something in the input buffer */
    // �����ܵش����ѯ�������е�����
    // �����ȡ���� short read ����ô���ܻ������������ڶ�ȡ����������
    // ��Щ��������Ҳ������������һ������Э������
    // ��Ҫ�ȴ��´ζ��¼��ľ���
    while(sdslen(c->querybuf)) {

        /* Return if clients are paused. */
        // ����ͻ�����������ͣ״̬����ôֱ�ӷ���
        if (!(c->flags & REDIS_SLAVE) && clientsArePaused()) return;//���ڽ���cluster failover�ֶ�����ת�ƣ�processInputBuffer->clientsArePaused����ͣ����ͻ�������

        /* Immediately abort if the client is in the middle of something. */
        // REDIS_BLOCKED ״̬��ʾ�ͻ������ڱ�����
        if (c->flags & REDIS_BLOCKED) return;

        /* REDIS_CLOSE_AFTER_REPLY closes the connection once the reply is
         * written to the client. Make sure to not let the reply grow after
         * this flag has been set (i.e. don't process more commands). */
        // �ͻ����Ѿ������˹ر� FLAG ��û�б�Ҫ����������
        if (c->flags & REDIS_CLOSE_AFTER_REPLY) return;

        /* Determine request type when unknown. */
        // �ж����������
        // �������͵���������� Redis ��ͨѶЭ���ϲ鵽��
        // http://redis.readthedocs.org/en/latest/topic/protocol.html
        // ����˵��������ѯ��һ��ͻ��˷������ģ�
        // ��������ѯ���� TELNET ��������

/*
get a ab ��Ӧ�ı���Ϊ����;

*3
$3
get
$1
a
$2
ab

��һ��������Ҳ����*��ʼ�൱��argc�� ��ʾ�����м�������$��������ֱ�ʾ����������ַ����м����ֽڡ���get a ab��һ��3�����ֱ���get ,a,b�� 
$3��ʾ�����get��3���ֽڡ�
*/
        if (!c->reqtype) {
            if (c->querybuf[0] == '*') {
                // ������ѯ
                c->reqtype = REDIS_REQ_MULTIBULK;
            } else {
                // ������ѯ
                c->reqtype = REDIS_REQ_INLINE;
            }
        }

        // ���������е�����ת��������Լ��������
        if (c->reqtype == REDIS_REQ_INLINE) {
            if (processInlineBuffer(c) != REDIS_OK) break;
        } else if (c->reqtype == REDIS_REQ_MULTIBULK) {
            if (processMultibulkBuffer(c) != REDIS_OK) break;
        } else {
            redisPanic("Unknown request type");
        }

        /* Multibulk processing could see a <= 0 length. */
        if (c->argc == 0) {
            resetClient(c);
        } else {
            /* Only reset the client when the command was executed. */
            // ִ����������ÿͻ���
            if (processCommand(c) == REDIS_OK)
                resetClient(c);
        }
    }
}

/* 
//���ɱ�����ڵ�redis���ڵ�ͨ��readQueryFromClient(����������ʵʱKV�����)����clusterReadHandler(��Ⱥ֮��ͨ�������)�е�read���쳣�¼���⵽�ڵ��쳣
 * ��ȡ�ͻ��˵Ĳ�ѯ����������
 */ //readQueryFromClient��sendReplyToClient��Ӧ��һ�����գ�һ������  //����TCP������acceptTcpHandler���ر����Ӳ��ͷ���Դ��freeClient    ��ȡ������readQueryFromClient
void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask) {
//�� ��ȫ��ͬ����ɺ󣬱�����һ��client��������������ʵʱKV,ͨ��readQueryFromClient����������ʵʱKV����
    redisClient *c = (redisClient*) privdata;
    int nread, readlen;
    size_t qblen;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);

    // ���÷������ĵ�ǰ�ͻ���
    server.current_client = c;
    
    // ���볤�ȣ�Ĭ��Ϊ 16 MB��
    readlen = REDIS_IOBUF_LEN;

    /* If this is a multi bulk request, and we are processing a bulk reply
     * that is large enough, try to maximize the probability that the query
     * buffer contains exactly the SDS string representing the object, even
     * at the risk of requiring more read(2) calls. This way the function
     * processMultiBulkBuffer() can avoid copying buffers to create the
     * Redis Object representing the argument. */
    if (c->reqtype == REDIS_REQ_MULTIBULK && c->multibulklen && c->bulklen != -1
        && c->bulklen >= REDIS_MBULK_BIG_ARG)
    {
        int remaining = (unsigned)(c->bulklen+2)-sdslen(c->querybuf);

        if (remaining < readlen) readlen = remaining;
    }

    // ��ȡ��ѯ��������ǰ���ݵĳ���
    // �����ȡ���� short read ����ô���ܻ������������ڶ�ȡ����������
    // ��Щ��������Ҳ������������һ������Э������
    qblen = sdslen(c->querybuf);
    // �������Ҫ�����»��������ݳ��ȵķ�ֵ��peak��
    if (c->querybuf_peak < qblen) 
        c->querybuf_peak = qblen;
    // Ϊ��ѯ����������ռ�
    c->querybuf = sdsMakeRoomFor(c->querybuf, readlen);
    // �������ݵ���ѯ����
    nread = read(fd, c->querybuf+qblen, readlen);

    // �������
    if (nread == -1) {
        if (errno == EAGAIN) {
            nread = 0;
        } else {
            redisLog(REDIS_VERBOSE, "Reading from client: %s",strerror(errno));
            freeClient(c, NGX_FUNC_LINE);
            return;
        }
    // ���� EOF
    } else if (nread == 0) {
        redisLog(REDIS_VERBOSE, "Client closed connection");
        freeClient(c, NGX_FUNC_LINE);
        return;
    }

    if (nread) {
        // �������ݣ����²�ѯ��������SDS�� free �� len ����
        // ���� '\0' ��ȷ�طŵ����ݵ����
        sdsIncrLen(c->querybuf,nread);
        // ��¼�������Ϳͻ������һ�λ�����ʱ��
        c->lastinteraction = server.unixtime;
        // ����ͻ����� master �Ļ����������ĸ���ƫ����
        if (c->flags & REDIS_MASTER) c->reploff += nread;
    } else {
        // �� nread == -1 �� errno == EAGAIN ʱ����
        server.current_client = NULL;
        return;
    }

    // ��ѯ���������ȳ�����������󻺳�������
    // ��ջ��������ͷſͻ���
    if (sdslen(c->querybuf) > server.client_max_querybuf_len) {
        sds ci = catClientInfoString(sdsempty(),c), bytes = sdsempty();

        bytes = sdscatrepr(bytes,c->querybuf,64);
        redisLog(REDIS_WARNING,"Closing client that reached max query buffer length: %s (qbuf initial bytes: %s)", ci, bytes);
        sdsfree(ci);
        sdsfree(bytes);
        freeClient(c, NGX_FUNC_LINE);
        return;
    }

    // �Ӳ�ѯ�����ض�ȡ���ݣ�������������ִ������
    // ������ִ�е������е��������ݶ���������Ϊֹ
    processInputBuffer(c);

    server.current_client = NULL;
}

// ��ȡ�ͻ���Ŀǰ����һ�黺�����Ĵ�С
void getClientsMaxBuffers(unsigned long *longest_output_list,
                          unsigned long *biggest_input_buffer) {
    redisClient *c;
    listNode *ln;
    listIter li;
    unsigned long lol = 0, bib = 0;

    listRewind(server.clients,&li);
    while ((ln = listNext(&li)) != NULL) {
        c = listNodeValue(ln);

        if (listLength(c->reply) > lol) lol = listLength(c->reply);
        if (sdslen(c->querybuf) > bib) bib = sdslen(c->querybuf);
    }
    *longest_output_list = lol;
    *biggest_input_buffer = bib;
}

/* This is a helper function for genClientPeerId().
 * It writes the specified ip/port to "peerid" as a null termiated string
 * in the form ip:port if ip does not contain ":" itself, otherwise
 * [ip]:port format is used (for IPv6 addresses basically). */
void formatPeerId(char *peerid, size_t peerid_len, char *ip, int port) {
    if (strchr(ip,':'))
        snprintf(peerid,peerid_len,"[%s]:%d",ip,port);
    else
        snprintf(peerid,peerid_len,"%s:%d",ip,port);
}

/* A Redis "Peer ID" is a colon separated ip:port pair.
 * For IPv4 it's in the form x.y.z.k:pork, example: "127.0.0.1:1234".
 * For IPv6 addresses we use [] around the IP part, like in "[::1]:1234".
 * For Unix socekts we use path:0, like in "/tmp/redis:0".
 *
 * A Peer ID always fits inside a buffer of REDIS_PEER_ID_LEN bytes, including
 * the null term.
 *
 * The function returns REDIS_OK on succcess, and REDIS_ERR on failure.
 *
 * On failure the function still populates 'peerid' with the "?:0" string
 * in case you want to relax error checking or need to display something
 * anyway (see anetPeerToString implementation for more info). */
int genClientPeerId(redisClient *client, char *peerid, size_t peerid_len) {
    char ip[REDIS_IP_STR_LEN];
    int port;

    if (client->flags & REDIS_UNIX_SOCKET) {
        /* Unix socket client. */
        snprintf(peerid,peerid_len,"%s:0",server.unixsocket);
        return REDIS_OK;
    } else {
        /* TCP client. */
        int retval = anetPeerToString(client->fd,ip,sizeof(ip),&port);
        formatPeerId(peerid,peerid_len,ip,port);
        return (retval == -1) ? REDIS_ERR : REDIS_OK;
    }
}

/* This function returns the client peer id, by creating and caching it
 * if client->perrid is NULL, otherwise returning the cached value.
 * The Peer ID never changes during the life of the client, however it
 * is expensive to compute. */
char *getClientPeerId(redisClient *c) {
    char peerid[REDIS_PEER_ID_LEN];

    if (c->peerid == NULL) {
        genClientPeerId(c,peerid,sizeof(peerid));
        c->peerid = sdsnew(peerid);
    }
    return c->peerid;
}

/* Concatenate a string representing the state of a client in an human
 * readable format, into the sds string 's'. */
// ��ȡ�ͻ��˵ĸ�����Ϣ�������Ǵ��浽 sds ֵ s ���棬�����ء�
sds catClientInfoString(sds s, redisClient *client) {
    char flags[16], events[3], *p;
    int emask;

    p = flags;
    if (client->flags & REDIS_SLAVE) {
        if (client->flags & REDIS_MONITOR)
            *p++ = 'O';
        else
            *p++ = 'S';
    }
    if (client->flags & REDIS_MASTER) *p++ = 'M';
    if (client->flags & REDIS_MULTI) *p++ = 'x';
    if (client->flags & REDIS_BLOCKED) *p++ = 'b';
    if (client->flags & REDIS_DIRTY_CAS) *p++ = 'd';
    if (client->flags & REDIS_CLOSE_AFTER_REPLY) *p++ = 'c';
    if (client->flags & REDIS_UNBLOCKED) *p++ = 'u';
    if (client->flags & REDIS_CLOSE_ASAP) *p++ = 'A';
    if (client->flags & REDIS_UNIX_SOCKET) *p++ = 'U';
    if (client->flags & REDIS_READONLY) *p++ = 'r';
    if (p == flags) *p++ = 'N';
    *p++ = '\0';

    emask = client->fd == -1 ? 0 : aeGetFileEvents(server.el,client->fd);
    p = events;
    if (emask & AE_READABLE) *p++ = 'r';
    if (emask & AE_WRITABLE) *p++ = 'w';
    *p = '\0';
    return sdscatfmt(s,
        "addr=%s fd=%i name=%s age=%I idle=%I flags=%s db=%i sub=%i psub=%i multi=%i qbuf=%U qbuf-free=%U obl=%U oll=%U omem=%U events=%s cmd=%s",
        getClientPeerId(client),
        client->fd,
        client->name ? (char*)client->name->ptr : "",
        (long long)(server.unixtime - client->ctime),
        (long long)(server.unixtime - client->lastinteraction),
        flags,
        client->db->id,
        (int) dictSize(client->pubsub_channels),
        (int) listLength(client->pubsub_patterns),
        (client->flags & REDIS_MULTI) ? client->mstate.count : -1,
        (unsigned long long) sdslen(client->querybuf),
        (unsigned long long) sdsavail(client->querybuf),
        (unsigned long long) client->bufpos,
        (unsigned long long) listLength(client->reply),
        (unsigned long long) getClientOutputBufferMemoryUsage(client),
        events,
        client->lastcmd ? client->lastcmd->name : "NULL");
}

/*
 * ��ӡ���������ӵ��������Ŀͻ��˵���Ϣ
 */
sds getAllClientsInfoString(void) {
    listNode *ln;
    listIter li;
    redisClient *client;
    sds o = sdsempty();

    o = sdsMakeRoomFor(o,200*listLength(server.clients));
    listRewind(server.clients,&li);
    while ((ln = listNext(&li)) != NULL) {
        client = listNodeValue(ln);
        o = catClientInfoString(o,client);
        o = sdscatlen(o,"\n",1);
    }
    return o;
}

/*
 * CLIENT �����ʵ��
 */
void clientCommand(redisClient *c) {
    listNode *ln;
    listIter li;
    redisClient *client;

    // CLIENT list
    if (!strcasecmp(c->argv[1]->ptr,"list") && c->argc == 2) {
        sds o = getAllClientsInfoString();
        addReplyBulkCBuffer(c,o,sdslen(o));
        sdsfree(o);

    // CLIENT kill
    } else if (!strcasecmp(c->argv[1]->ptr,"kill") && c->argc == 3) {

        // �����ͻ���������ɱ��ָ����ַ�Ŀͻ���
        listRewind(server.clients,&li);
        while ((ln = listNext(&li)) != NULL) {
            char *peerid;

            client = listNodeValue(ln);
            peerid = getClientPeerId(client);
            if (strcmp(peerid,c->argv[2]->ptr) == 0) {
                addReply(c,shared.ok);
                if (c == client) {
                    client->flags |= REDIS_CLOSE_AFTER_REPLY;
                } else {
                    freeClient(client, NGX_FUNC_LINE);
                }
                return;
            }
        }
        addReplyError(c,"No such client");

    // CLIENT setname ���ÿͻ�������
    } else if (!strcasecmp(c->argv[1]->ptr,"setname") && c->argc == 3) {
        int j, len = sdslen(c->argv[2]->ptr);
        char *p = c->argv[2]->ptr;

        /* Setting the client name to an empty string actually removes
         * the current name. */
        // ����Ϊ��ʱ����տͻ��˵�����
        if (len == 0) {
            if (c->name) decrRefCount(c->name);
            c->name = NULL;
            addReply(c,shared.ok);
            return;
        }

        /* Otherwise check if the charset is ok. We need to do this otherwise
         * CLIENT LIST format will break. You should always be able to
         * split by space to get the different fields. */
        for (j = 0; j < len; j++) {
            if (p[j] < '!' || p[j] > '~') { /* ASCII is assumed. */
                addReplyError(c,
                    "Client names cannot contain spaces, "
                    "newlines or special characters.");
                return;
            }
        }
        if (c->name) decrRefCount(c->name);
        c->name = c->argv[2];
        incrRefCount(c->name);
        addReply(c,shared.ok);

    // CLIENT getname ��ȡ�ͻ��˵�����
    } else if (!strcasecmp(c->argv[1]->ptr,"getname") && c->argc == 2) {
        if (c->name)
            addReplyBulk(c,c->name);
        else
            addReply(c,shared.nullbulk);
    } else if (!strcasecmp(c->argv[1]->ptr,"pause") && c->argc == 3) {
        long long duration;

        if (getTimeoutFromObjectOrReply(c,c->argv[2],&duration,UNIT_MILLISECONDS)
                                        != REDIS_OK) return;
        pauseClients(duration);
        addReply(c,shared.ok);
    } else {
        addReplyError(c, "Syntax error, try CLIENT (LIST | KILL ip:port | GETNAME | SETNAME connection-name)");
    }
}

/* Rewrite the command vector of the client. All the new objects ref count
 * is incremented. The old command vector is freed, and the old objects
 * ref count is decremented. */
// �޸Ŀͻ��˵Ĳ�������
void rewriteClientCommandVector(redisClient *c, int argc, ...) {
    va_list ap;
    int j;
    robj **argv; /* The new argument vector */

    // �����²���
    argv = zmalloc(sizeof(robj*)*argc);
    va_start(ap,argc);
    for (j = 0; j < argc; j++) {
        robj *a;
        
        a = va_arg(ap, robj*);
        argv[j] = a;
        incrRefCount(a);
    }
    /* We free the objects in the original vector at the end, so we are
     * sure that if the same objects are reused in the new vector the
     * refcount gets incremented before it gets decremented. */
    // �ͷžɲ���
    for (j = 0; j < c->argc; j++) decrRefCount(c->argv[j]);
    zfree(c->argv);

    /* Replace argv and argc with our new versions. */
    // ���²����滻
    c->argv = argv;
    c->argc = argc;
    c->cmd = lookupCommandOrOriginal(c->argv[0]->ptr);
    redisAssertWithInfo(c,NULL,c->cmd != NULL);
    va_end(ap);
}

/* Rewrite a single item in the command vector.
 * The new val ref count is incremented, and the old decremented. */
// �޸ĵ�������
void rewriteClientCommandArgument(redisClient *c, int i, robj *newval) {
    robj *oldval;
   
    redisAssertWithInfo(c,NULL,i < c->argc);
    oldval = c->argv[i];
    c->argv[i] = newval;
    incrRefCount(newval);
    decrRefCount(oldval);

    /* If this is the command name make sure to fix c->cmd. */
    if (i == 0) {
        c->cmd = lookupCommandOrOriginal(c->argv[0]->ptr);
        redisAssertWithInfo(c,NULL,c->cmd != NULL);
    }
}

/* This function returns the number of bytes that Redis is virtually
 * using to store the reply still not read by the client.
 * It is "virtual" since the reply output list may contain objects that
 * are shared and are not really using additional memory.
 *
 * �������ؿ����ڱ���Ŀǰ��δ���ظ��ͻ��˵Ļظ��������С�����ֽ�Ϊ��λ����
 * ֮����˵�������С����Ϊ�ظ��б��п����а�������Ķ���
 *
 * The function returns the total sum of the length of all the objects
 * stored in the output list, plus the memory used to allocate every
 * list node. The static reply buffer is not taken into account since it
 * is allocated anyway.
 *
 * �������ػظ��б�����������ȫ�����������ܺͣ�
 * �����б�ڵ�������Ŀռ䡣
 * ��̬�ظ����������ᱻ�������ڣ���Ϊ�����ǻᱻ����ġ�
 *
 * Note: this function is very fast so can be called as many time as
 * the caller wishes. The main usage of this function currently is
 * enforcing the client output length limits. 
 *
 * ע�⣺����������ٶȺܿ죬���������Ա�����ص��ö�Ρ�
 * �������Ŀǰ����Ҫ���þ�������ǿ�ƿͻ�������������ơ�
 */
unsigned long getClientOutputBufferMemoryUsage(redisClient *c) {
    unsigned long list_item_size = sizeof(listNode)+sizeof(robj);

    return c->reply_bytes + (list_item_size*listLength(c->reply));
}

/* Get the class of a client, used in order to enforce limits to different
 * classes of clients.
 *
 * ��ȡ�ͻ��˵����ͣ����ڶԲ�ͬ���͵Ŀͻ���Ӧ�ò�ͬ�����ơ�
 *
 * The function will return one of the following:
 * 
 * ������������������ֵ������һ����
 *
 * REDIS_CLIENT_LIMIT_CLASS_NORMAL -> Normal client
 *                                    ��ͨ�ͻ���
 *
 * REDIS_CLIENT_LIMIT_CLASS_SLAVE  -> Slave or client executing MONITOR command
 *                                    �ӷ���������������ִ�� MONITOR ����Ŀͻ���
 *
 * REDIS_CLIENT_LIMIT_CLASS_PUBSUB -> Client subscribed to Pub/Sub channels
 *                                    ���ڽ��ж��Ĳ�����SUBSCRIBE/PSUBSCRIBE���Ŀͻ���
 */
int getClientLimitClass(redisClient *c) {
    if (c->flags & REDIS_SLAVE) return REDIS_CLIENT_LIMIT_CLASS_SLAVE;
    if (dictSize(c->pubsub_channels) || listLength(c->pubsub_patterns))
        return REDIS_CLIENT_LIMIT_CLASS_PUBSUB;
    return REDIS_CLIENT_LIMIT_CLASS_NORMAL;
}

// �������֣���ȡ�ͻ��˵����ͳ���
int getClientLimitClassByName(char *name) {
    if (!strcasecmp(name,"normal")) return REDIS_CLIENT_LIMIT_CLASS_NORMAL;
    else if (!strcasecmp(name,"slave")) return REDIS_CLIENT_LIMIT_CLASS_SLAVE;
    else if (!strcasecmp(name,"pubsub")) return REDIS_CLIENT_LIMIT_CLASS_PUBSUB;
    else return -1;
}

// ���ݿͻ��˵����ͣ���ȡ����
char *getClientLimitClassName(int class) {
    switch(class) {
    case REDIS_CLIENT_LIMIT_CLASS_NORMAL:   return "normal";
    case REDIS_CLIENT_LIMIT_CLASS_SLAVE:    return "slave";
    case REDIS_CLIENT_LIMIT_CLASS_PUBSUB:   return "pubsub";
    default:                                return NULL;
    }
}

/* The function checks if the client reached output buffer soft or hard
 * limit, and also update the state needed to check the soft limit as
 * a side effect.
 *
 * ����������ͻ����Ƿ�ﵽ����������������ԣ�soft�����ƻ���Ӳ�ԣ�hard�����ƣ�
 * ���ڵ���������ʱ���Կͻ��˽��б�ǡ�
 *
 * Return value: non-zero if the client reached the soft or the hard limit.
 *               Otherwise zero is returned. 
 *
 * ����ֵ�������������ƻ���Ӳ������ʱ�����ط� 0 ֵ��
 *         ���򷵻� 0 ��
 */ 
 /*���ƻ�ѹ��������feedReplicationBacklog�������Ҫ�����������ϣ�Ȼ��ͨ��client->reply����洢������ЩKV����ЩKV������
���ͳ�ȥ�ˣ�ʵ���϶Է�û���յ�,�´θĿͻ������Ϻ�ͨ��replconf ack xxx��ͨ���Լ���offset��master�յ���Ϳ����ж϶Է��Ƿ���û��ȫ������
���͵�client��ʵʱKV��ѹbuffer������checkClientOutputBufferLimits 

��������������:client->reply        checkClientOutputBufferLimits    ��ҪӦ�Կͻ��˶�ȡ����ͬʱ���д���KV���뱾�ڵ㣬��ɻ�ѹ
���ƻ�ѹ������:server.repl_backlog    feedReplicationBacklog    ��ҪӦ���������ϣ����в�����ͬ��psyn������ȫ��ͬ��
*/
int checkClientOutputBufferLimits(redisClient *c) { 
//���ƻ�ѹ��������С����ͨ��repl-backlog-size���ã�Ĭ��Ϊ1M�� ���͵��ͻ��˵�out buffer��KV�������ƣ����ͨ��������
    int soft = 0, hard = 0, class;

    // ��ȡ�ͻ��˻ظ��������Ĵ�С
    unsigned long used_mem = getClientOutputBufferMemoryUsage(c);

    // ��ȡ�ͻ��˵����ƴ�С
    class = getClientLimitClass(c);

    // �����������
    if (server.client_obuf_limits[class].hard_limit_bytes &&
        used_mem >= server.client_obuf_limits[class].hard_limit_bytes)
        hard = 1;

    // ���Ӳ������
    if (server.client_obuf_limits[class].soft_limit_bytes &&
        used_mem >= server.client_obuf_limits[class].soft_limit_bytes)
        soft = 1;

    /* We need to check if the soft limit is reached continuously for the
     * specified amount of seconds. */
    // �ﵽ��������
    if (soft) {

        // ��һ�δﵽ��������
        if (c->obuf_soft_limit_reached_time == 0) {
            // ��¼ʱ��
            c->obuf_soft_limit_reached_time = server.unixtime;
            // �ر��������� flag
            soft = 0; /* First time we see the soft limit reached */

        // �ٴδﵽ��������
        } else {
            // �������Ƶ�����ʱ��
            time_t elapsed = server.unixtime - c->obuf_soft_limit_reached_time;

            // ���û�г����������ʱ���Ļ�����ô�ر��������� flag
            // ����������������ʱ���Ļ����������� flag �ͻᱻ����
            if (elapsed <=
                server.client_obuf_limits[class].soft_limit_seconds) {
                soft = 0; /* The client still did not reached the max number of
                             seconds for the soft limit to be considered
                             reached. */
            }
        }
    } else {
        // δ�ﵽ�������ƣ������������������ƣ���ô����������ƵĽ���ʱ��
        c->obuf_soft_limit_reached_time = 0;
    }

    return soft || hard;
}

/* Asynchronously close a client if soft or hard limit is reached on the
 * output buffer size. The caller can check if the client will be closed
 * checking if the client REDIS_CLOSE_ASAP flag is set.
 *
 * ����ͻ��˴ﵽ��������С�����Ի���Ӳ�����ƣ���ô�򿪿ͻ��˵� ``REDIS_CLOSE_ASAP`` ״̬��
 * �÷������첽�عرտͻ��ˡ�
 *
 * Note: we need to close the client asynchronously because this function is
 * called from contexts where the client can't be freed safely, i.e. from the
 * lower level functions pushing data inside the client output buffers. 
 *
 * ע�⣺
 * ���ǲ���ֱ�ӹرտͻ��ˣ���Ҫ�첽�رյ�ԭ���ǿͻ���������һ�����ܱ���ȫ�عرյ��������С�
 * ����˵�������еײ㺯�������������ݵ��ͻ��˵�������������档      
 */ //���ƻ�ѹ��������С����ͨ��repl-backlog-size���ã�Ĭ��Ϊ1M
void asyncCloseClientOnOutputBufferLimitReached(redisClient *c) {
    redisAssert(c->reply_bytes < ULONG_MAX-(1024*64));

    // �Ѿ��������
    if (c->reply_bytes == 0 || c->flags & REDIS_CLOSE_ASAP) return;

    // �������
    if (checkClientOutputBufferLimits(c)) {
        sds client = catClientInfoString(sdsempty(),c);

        // �첽�ر�
        freeClientAsync(c);
        redisLog(REDIS_WARNING,"Client %s scheduled to be closed ASAP for overcoming of output buffer limits.", client);
        sdsfree(client);
    }
}

/* Helper function used by freeMemoryIfNeeded() in order to flush slaves
 * output buffers without returning control to the event loop. */
// freeMemoryIfNeeded() �����ĸ���������
// �����ڲ������¼�ѭ��������£���ϴ���дӷ������������������
void flushSlavesOutputBuffers(void) {
    listIter li;
    listNode *ln;

    listRewind(server.slaves,&li);
    while((ln = listNext(&li))) {
        redisClient *slave = listNodeValue(ln);
        int events;

        events = aeGetFileEvents(server.el,slave->fd);
        if (events & AE_WRITABLE &&
            slave->replstate == REDIS_REPL_ONLINE &&
            listLength(slave->reply))
        {
            sendReplyToClient(server.el,slave->fd,slave,0);
        }
    }
}

/* Pause clients up to the specified unixtime (in ms). While clients
 * are paused no command is processed from clients, so the data set can't
 * change during that time.
 *
 * However while this function pauses normal and Pub/Sub clients, slaves are
 * still served, so this function can be used on server upgrades where it is
 * required that slaves process the latest bytes from the replication stream
 * before being turned to masters.
 *
 * This function is also internally used by Redis Cluster for the manual
 * failover procedure implemented by CLUSTER FAILOVER.
 *
 * The function always succeed, even if there is already a pause in progress.
 * In such a case, the pause is extended if the duration is more than the
 * time left for the previous duration. However if the duration is smaller
 * than the time left for the previous pause, no change is made to the
 * left duration. */
// ��ͣ�ͻ��ˣ��÷�������ָ����ʱ���ڲ��ٽ��ܱ���ͣ�ͻ��˷���������
// ��������ϵͳ���£������ڲ��� CLUSTER FAILOVER ����ʹ�á�
void pauseClients(mstime_t end) { 
//���յ�slave��CLUSTERMSG_TYPE_MFSTART����clients_paused=1, ����ͣ�������пͻ��˵�����֪��pause��ʱʱ�䵽����clientsArePaused

    // ������ͣʱ��
    if (!server.clients_paused || end > server.clients_pause_end_time)
        server.clients_pause_end_time = end;

    // �򿪿ͻ��˵ġ��ѱ���ͣ����־
    server.clients_paused = 1;
}

/* Return non-zero if clients are currently paused. As a side effect the
 * function checks if the pause time was reached and clear it. */
 // �жϷ�����Ŀǰ����ͣ�ͻ��˵�������û���κοͻ��˱���ͣʱ������ 0 ��
int clientsArePaused(void) {//��clientsArePaused���  
//���ڽ���cluster failover�ֶ�����ת�ƣ�processInputBuffer->clientsArePaused����ͣ����ͻ�������
    if (server.clients_paused && server.clients_pause_end_time < server.mstime) { //client���õ�����ʱ�䵽���������״̬��ʶ���Լ������ܿͻ���������
        listNode *ln;
        listIter li;
        redisClient *c;

        server.clients_paused = 0;

        /* Put all the clients in the unblocked clients queue in order to
         * force the re-processing of the input buffer if any. */
        listRewind(server.clients,&li);
        while ((ln = listNext(&li)) != NULL) {
            c = listNodeValue(ln);

            if (c->flags & REDIS_SLAVE) continue;
            listAddNodeTail(server.unblocked_clients,c);
        }
    }
    
    return server.clients_paused;
}

/* This function is called by Redis in order to process a few events from
 * time to time while blocked into some not interruptible operation.
 * This allows to reply to clients with the -LOADING error while loading the
 * data set at startup or after a full resynchronization with the master
 * and so forth.
 *
 * It calls the event loop in order to process a few events. Specifically we
 * try to call the event loop for times as long as we receive acknowledge that
 * some event was processed, in order to go forward with the accept, read,
 * write, close sequence needed to serve a client.
 *
 * The function returns the total number of events processed. */
// �÷������ڱ�����������£���Ȼ����ĳЩ�¼���
int processEventsWhileBlocked(void) {
    int iterations = 4; /* See the function top-comment. */
    int count = 0;
    while (iterations--) {
        int events = aeProcessEvents(server.el, AE_FILE_EVENTS|AE_DONT_WAIT); //����ֻ����FILE�¼������ᴦ��TIMEʱ��
        if (!events) break;
        count += events;
    }
    return count;
}
