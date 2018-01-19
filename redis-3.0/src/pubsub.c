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

/*-----------------------------------------------------------------------------
 * Pubsub low level API
 *----------------------------------------------------------------------------*/

/*
 * �ͷŸ�����ģʽ p
 */
void freePubsubPattern(void *p) {
    pubsubPattern *pat = p;

    decrRefCount(pat->pattern);
    zfree(pat);
}

/*
 * �Ա�ģʽ a �� b �Ƿ���ͬ����ͬ���� 1 ������ͬ���� 0 ��
 */
int listMatchPubsubPattern(void *a, void *b) {
    pubsubPattern *pa = a, *pb = b;

    return (pa->client == pb->client) &&
           (equalStringObjects(pa->pattern,pb->pattern));
}

/* Subscribe a client to a channel. Returns 1 if the operation succeeded, or
 * 0 if the client was already subscribed to that channel. 
 *
 * ���ÿͻ��� c ����Ƶ�� channel ��
 *
 * ���ĳɹ����� 1 ������ͻ����Ѿ������˸�Ƶ������ô���� 0 ��
 */
int pubsubSubscribeChannel(redisClient *c, robj *channel) {
    dictEntry *de;
    list *clients = NULL;
    int retval = 0;

    /* Add the channel to the client -> channels hash table */
    // �� channels ��ӵ� c->pubsub_channels �ļ����У�ֵΪ NULL ���ֵ���Ϊ���ϣ�
    if (dictAdd(c->pubsub_channels,channel,NULL) == DICT_OK) {
        retval = 1;
        incrRefCount(channel);

        // ����ʾ��ͼ
        // {
        //  Ƶ����        ����Ƶ���Ŀͻ���
        //  'channel-a' : [c1, c2, c3],
        //  'channel-b' : [c5, c2, c1],
        //  'channel-c' : [c10, c2, c1]
        // }
        /* Add the client to the channel -> list of clients hash table */
        // �� pubsub_channels �ֵ���ȡ�����������ж����� channel �Ŀͻ��˵�����
        // ��� channel ���������ֵ䣬��ô��ӽ�ȥ
        de = dictFind(server.pubsub_channels,channel);
        if (de == NULL) {
            clients = listCreate();
            dictAdd(server.pubsub_channels,channel,clients);
            incrRefCount(channel);
        } else {
            clients = dictGetVal(de);
        }

        // before:
        // 'channel' : [c1, c2]
        // after:
        // 'channel' : [c1, c2, c3]
        // ���ͻ�����ӵ������ĩβ
        listAddNodeTail(clients,c);
    }

    /* Notify the client */
    // �ظ��ͻ��ˡ�
    // ʾ����
    // redis 127.0.0.1:6379> SUBSCRIBE xxx
    // Reading messages... (press Ctrl-C to quit)
    // 1) "subscribe"
    // 2) "xxx"
    // 3) (integer) 1
    addReply(c,shared.mbulkhdr[3]);
    // "subscribe\n" �ַ���
    addReply(c,shared.subscribebulk);
    // �����ĵĿͻ���
    addReplyBulk(c,channel);
    // �ͻ��˶��ĵ�Ƶ����ģʽ����
    addReplyLongLong(c,dictSize(c->pubsub_channels)+listLength(c->pubsub_patterns));

    return retval;
}

/* Unsubscribe a client from a channel. Returns 1 if the operation succeeded, or
 * 0 if the client was not subscribed to the specified channel. 
 *
 * �ͻ��� c �˶�Ƶ�� channel ��
 *
 * ���ȡ���ɹ����� 1 �������Ϊ�ͻ���δ����Ƶ���������ȡ��ʧ�ܣ����� 0 ��
 */
int pubsubUnsubscribeChannel(redisClient *c, robj *channel, int notify) {
    dictEntry *de;
    list *clients;
    listNode *ln;
    int retval = 0;

    /* Remove the channel from the client -> channels hash table */
    // ��Ƶ�� channel �� client->channels �ֵ����Ƴ�
    incrRefCount(channel); /* channel may be just a pointer to the same object
                            we have in the hash tables. Protect it... */
    // ʾ��ͼ��
    // before:
    // {
    //  'channel-x': NULL,
    //  'channel-y': NULL,
    //  'channel-z': NULL,
    // }
    // after unsubscribe channel-y ��
    // {
    //  'channel-x': NULL,
    //  'channel-z': NULL,
    // }
    if (dictDelete(c->pubsub_channels,channel) == DICT_OK) {

        // channel �Ƴ��ɹ�����ʾ�ͻ��˶��������Ƶ����ִ�����´���

        retval = 1;
        /* Remove the client from the channel -> clients list hash table */
        // �� channel->clients �� clients �����У��Ƴ� client 
        // ʾ��ͼ��
        // before:
        // {
        //  'channel-x' : [c1, c2, c3],
        // }
        // after c2 unsubscribe channel-x:
        // {
        //  'channel-x' : [c1, c3]
        // }
        de = dictFind(server.pubsub_channels,channel);
        redisAssertWithInfo(c,NULL,de != NULL);
        clients = dictGetVal(de);
        ln = listSearchKey(clients,c);
        redisAssertWithInfo(c,NULL,ln != NULL);
        listDelNode(clients,ln);

        // ����Ƴ� client ֮������Ϊ�գ���ôɾ����� channel ��
        // ʾ��ͼ��
        // before
        // {
        //  'channel-x' : [c1]
        // }
        // after c1 ubsubscribe channel-x
        // then also delete 'channel-x' key in dict
        // {
        //  // nothing here
        // }
        if (listLength(clients) == 0) {
            /* Free the list and associated hash entry at all if this was
             * the latest client, so that it will be possible to abuse
             * Redis PUBSUB creating millions of channels. */
            dictDelete(server.pubsub_channels,channel);
        }
    }

    /* Notify the client */
    // �ظ��ͻ���
    if (notify) {
        addReply(c,shared.mbulkhdr[3]);
        // "ubsubscribe" �ַ���
        addReply(c,shared.unsubscribebulk);
        // ���˶���Ƶ��
        addReplyBulk(c,channel);
        // �˶�Ƶ��֮��ͻ������ڶ��ĵ�Ƶ����ģʽ������
        addReplyLongLong(c,dictSize(c->pubsub_channels)+
                       listLength(c->pubsub_patterns));

    }

    decrRefCount(channel); /* it is finally safe to release it */

    return retval;
}

/* Subscribe a client to a pattern. Returns 1 if the operation succeeded, or 0 if the client was already subscribed to that pattern. 
 *
 * ���ÿͻ��� c ����ģʽ pattern ��
 *
 * ���ĳɹ����� 1 ������ͻ����Ѿ������˸�ģʽ����ô���� 0 ��
 */
int pubsubSubscribePattern(redisClient *c, robj *pattern) {
    int retval = 0;

    // �������в���ģʽ�����ͻ����Ƿ��Ѿ����������ģʽ
    // ����Ϊʲô���� channel ���������ֵ������м���أ�
    // ��Ȼ pattern ������һ����˵������
    if (listSearchKey(c->pubsub_patterns,pattern) == NULL) {
        
        // ���û�еĻ���ִ�����´���

        retval = 1;

        pubsubPattern *pat;

        // �� pattern ��ӵ� c->pubsub_patterns ������
        listAddNodeTail(c->pubsub_patterns,pattern);

        incrRefCount(pattern);

        // �����������µ� pubsubPattern �ṹ
        pat = zmalloc(sizeof(*pat));
        pat->pattern = getDecodedObject(pattern);
        pat->client = c;

        // ��ӵ�ĩβ
        listAddNodeTail(server.pubsub_patterns,pat);
    }

    /* Notify the client */
    // �ظ��ͻ��ˡ�
    // ʾ����
    // redis 127.0.0.1:6379> PSUBSCRIBE xxx*
    // Reading messages... (press Ctrl-C to quit)
    // 1) "psubscribe"
    // 2) "xxx*"
    // 3) (integer) 1
    addReply(c,shared.mbulkhdr[3]);
    // �ظ� "psubscribe" �ַ���
    addReply(c,shared.psubscribebulk);
    // �ظ������ĵ�ģʽ
    addReplyBulk(c,pattern);
    // �ظ��ͻ��˶��ĵ�Ƶ����ģʽ������
    addReplyLongLong(c,dictSize(c->pubsub_channels)+listLength(c->pubsub_patterns));

    return retval;
}

/* Unsubscribe a client from a channel. Returns 1 if the operation succeeded, or
 * 0 if the client was not subscribed to the specified channel. 
 *
 * ȡ���ͻ��� c ��ģʽ pattern �Ķ��ġ�
 *
 * ȡ���ɹ����� 1 ����Ϊ�ͻ���δ���� pattern �����ȡ��ʧ�ܣ����� 0 ��
 */
int pubsubUnsubscribePattern(redisClient *c, robj *pattern, int notify) {
    listNode *ln;
    pubsubPattern pat;
    int retval = 0;

    incrRefCount(pattern); /* Protect the object. May be the same we remove */

    // ��ȷ��һ�£��ͻ����Ƿ��������ģʽ
    if ((ln = listSearchKey(c->pubsub_patterns,pattern)) != NULL) {

        retval = 1;

        // ��ģʽ�ӿͻ��˵Ķ����б���ɾ��
        listDelNode(c->pubsub_patterns,ln);

        // ���� pubsubPattern �ṹ
        pat.client = c;
        pat.pattern = pattern;

        // �ڷ������в���
        ln = listSearchKey(server.pubsub_patterns,&pat);
        listDelNode(server.pubsub_patterns,ln);
    }

    /* Notify the client */
    // �ظ��ͻ���
    if (notify) {
        addReply(c,shared.mbulkhdr[3]);
        // "punsubscribe" �ַ���
        addReply(c,shared.punsubscribebulk);
        // ���˶���ģʽ
        addReplyBulk(c,pattern);
        // �˶�Ƶ��֮��ͻ������ڶ��ĵ�Ƶ����ģʽ������
        addReplyLongLong(c,dictSize(c->pubsub_channels)+
                       listLength(c->pubsub_patterns));
    }

    decrRefCount(pattern);

    return retval;
}

/* Unsubscribe from all the channels. Return the number of channels the
 * client was subscribed from. 
 *
 * �˶��ͻ��� c ���ĵ�����Ƶ����
 *
 * ���ر��˶�Ƶ����������
 */
int pubsubUnsubscribeAllChannels(redisClient *c, int notify) {

    // Ƶ��������
    dictIterator *di = dictGetSafeIterator(c->pubsub_channels);
    dictEntry *de;
    int count = 0;

    // �˶�
    while((de = dictNext(di)) != NULL) {
        robj *channel = dictGetKey(de);

        count += pubsubUnsubscribeChannel(c,channel,notify);
    }

    /* We were subscribed to nothing? Still reply to the client. */
    // �����ִ���������ʱ���ͻ���û�ж����κ�Ƶ����
    // ��ô��ͻ��˷��ͻظ�
    if (notify && count == 0) {
        addReply(c,shared.mbulkhdr[3]);
        addReply(c,shared.unsubscribebulk);
        addReply(c,shared.nullbulk);
        addReplyLongLong(c,dictSize(c->pubsub_channels)+
                       listLength(c->pubsub_patterns));
    }

    dictReleaseIterator(di);

    // ���˶���Ƶ��������
    return count;
}

/* Unsubscribe from all the patterns. Return the number of patterns the
 * client was subscribed from. 
 *
 * �˶��ͻ��� c ���ĵ�����ģʽ��
 *
 * ���ر��˶�ģʽ��������
 */
int pubsubUnsubscribeAllPatterns(redisClient *c, int notify) {
    listNode *ln;
    listIter li;
    int count = 0;

    // �����ͻ��˶���ģʽ������
    listRewind(c->pubsub_patterns,&li);
    while ((ln = listNext(&li)) != NULL) {
        robj *pattern = ln->value;

        // �˶����������˶���
        count += pubsubUnsubscribePattern(c,pattern,notify);
    }

    // �����ִ���������ʱ���ͻ���û�ж����κ�ģʽ��
    // ��ô��ͻ��˷��ͻظ�
    if (notify && count == 0) {
        /* We were subscribed to nothing? Still reply to the client. */
        addReply(c,shared.mbulkhdr[3]);
        addReply(c,shared.punsubscribebulk);
        addReply(c,shared.nullbulk);
        addReplyLongLong(c,dictSize(c->pubsub_channels)+
                       listLength(c->pubsub_patterns));
    }

    // �˶�����
    return count;
}

/* Publish a message 
 *
 * �� message ���͵����ж���Ƶ�� channel �Ŀͻ��ˣ�
 * �Լ����ж����˺� channel Ƶ��ƥ���ģʽ�Ŀͻ��ˡ�

 pubsubPublishMessage������PUBLISH�����ʵ�ֺ�����ִ���������
��ͬ��ִ��PUBLISH����������ݿ�֪ͨ�Ŀͻ����յ�����Ϣ�������������������
 */ //publish �����ʵ�ֺ�����ִ�иú����൱�ڿͻ�����redis����������publish����
int pubsubPublishMessage(robj *channel, robj *message) {
    int receivers = 0;
    dictEntry *de;
    listNode *ln;
    listIter li;

    /* Send to clients listening for that channel */
    // ȡ���������ж���Ƶ�� channel �Ŀͻ��˵�����
    // ������Ϣ���͸�����
    de = dictFind(server.pubsub_channels,channel);
    if (de) {
        list *list = dictGetVal(de);
        listNode *ln;
        listIter li;

        // �����ͻ��������� message ���͸�����
        listRewind(list,&li);
        while ((ln = listNext(&li)) != NULL) {
            redisClient *c = ln->value;

            // �ظ��ͻ��ˡ�
            // ʾ����
            // 1) "message"
            // 2) "xxx"
            // 3) "hello"
            addReply(c,shared.mbulkhdr[3]);
            // "message" �ַ���
            addReply(c,shared.messagebulk);
            // ��Ϣ����ԴƵ��
            addReplyBulk(c,channel);
            // ��Ϣ����
            addReplyBulk(c,message);

            // ���տͻ��˼���
            receivers++;
        }
    }

    /* Send to clients listening to matching channels */
    // ����ϢҲ���͸���Щ��Ƶ��ƥ���ģʽ
    if (listLength(server.pubsub_patterns)) {

        // ����ģʽ����
        listRewind(server.pubsub_patterns,&li);
        channel = getDecodedObject(channel);
        while ((ln = listNext(&li)) != NULL) {

            // ȡ�� pubsubPattern
            pubsubPattern *pat = ln->value;

            // ��� channel �� pattern ƥ��
            // �͸����ж��ĸ� pattern �Ŀͻ��˷�����Ϣ
            if (stringmatchlen((char*)pat->pattern->ptr,
                                sdslen(pat->pattern->ptr),
                                (char*)channel->ptr,
                                sdslen(channel->ptr),0)) {

                // �ظ��ͻ���
                // ʾ����
                // 1) "pmessage"
                // 2) "*"
                // 3) "xxx"
                // 4) "hello"
                addReply(pat->client,shared.mbulkhdr[4]);
                addReply(pat->client,shared.pmessagebulk);
                addReplyBulk(pat->client,pat->pattern);
                addReplyBulk(pat->client,channel);
                addReplyBulk(pat->client,message);

                // �Խ�����Ϣ�Ŀͻ��˽��м���
                receivers++;
            }
        }

        decrRefCount(channel);
    }

    // ���ؼ���
    return receivers;
}

/*-----------------------------------------------------------------------------
 * Pubsub commands implementation
 *----------------------------------------------------------------------------*/

void subscribeCommand(redisClient *c) {
    int j;

    for (j = 1; j < c->argc; j++)
        pubsubSubscribeChannel(c,c->argv[j]);
}

void unsubscribeCommand(redisClient *c) {
    if (c->argc == 1) {
        pubsubUnsubscribeAllChannels(c,1);
    } else {
        int j;

        for (j = 1; j < c->argc; j++)
            pubsubUnsubscribeChannel(c,c->argv[j],1);
    }
}

void psubscribeCommand(redisClient *c) {
    int j;

    for (j = 1; j < c->argc; j++)
        pubsubSubscribePattern(c,c->argv[j]);
}

void punsubscribeCommand(redisClient *c) {
    if (c->argc == 1) {
        pubsubUnsubscribeAllPatterns(c,1);
    } else {
        int j;

        for (j = 1; j < c->argc; j++)
            pubsubUnsubscribePattern(c,c->argv[j],1);
    }
}

void publishCommand(redisClient *c) {

    int receivers = pubsubPublishMessage(c->argv[1],c->argv[2]);
    if (server.cluster_enabled)
        clusterPropagatePublish(c->argv[1],c->argv[2]);
    else
        forceCommandPropagation(c,REDIS_PROPAGATE_REPL);
    addReplyLongLong(c,receivers);
}

/* PUBSUB command for Pub/Sub introspection. */
void pubsubCommand(redisClient *c) {

    // PUBSUB CHANNELS [pattern] ������
    if (!strcasecmp(c->argv[1]->ptr,"channels") &&
        (c->argc == 2 || c->argc ==3))
    {
        /* PUBSUB CHANNELS [<pattern>] */
        // ������������Ƿ������ pattern ����
        // ���û�и����Ļ�������Ϊ NULL
        sds pat = (c->argc == 2) ? NULL : c->argv[2]->ptr;

        // ���� pubsub_channels ���ֵ������
        // ���ֵ�ļ�ΪƵ����ֵΪ����
        // �����б��������ж��ļ�����Ӧ��Ƶ���Ŀͻ���
        dictIterator *di = dictGetIterator(server.pubsub_channels);
        dictEntry *de;
        long mblen = 0;
        void *replylen;

        replylen = addDeferredMultiBulkLength(c);
        // �ӵ������л�ȡһ���ͻ���
        while((de = dictNext(di)) != NULL) {

            // ���ֵ���ȡ���ͻ��������ĵ�Ƶ��
            robj *cobj = dictGetKey(de);
            sds channel = cobj->ptr;

            // ˳��һ��
            // ��Ϊ Redis ���ֵ�ʵ��ֻ�ܱ����ֵ��ֵ���ͻ��ˣ�
            // ��������Ż��б����ֵ�ֵȻ��ͨ���ֵ�ֵȡ���ֵ����Ƶ�����������÷�

            // ���û�и��� pattern ��������ô��ӡ�����ҵ���Ƶ��
            // ��������� pattern ��������ôֻ��ӡ�� pattern ��ƥ���Ƶ��
            if (!pat || stringmatchlen(pat, sdslen(pat),
                                       channel, sdslen(channel),0))
            {
                // ��ͻ������Ƶ��
                addReplyBulk(c,cobj);
                mblen++;
            }
        }
        // �ͷ��ֵ������
        dictReleaseIterator(di);
        setDeferredMultiBulkLength(c,replylen,mblen);

    // PUBSUB NUMSUB [channel-1 channel-2 ... channel-N] ������
    } else if (!strcasecmp(c->argv[1]->ptr,"numsub") && c->argc >= 2) {
        /* PUBSUB NUMSUB [Channel_1 ... Channel_N] */
        int j;

        addReplyMultiBulkLen(c,(c->argc-2)*2);
        for (j = 2; j < c->argc; j++) {

            // c->argv[j] Ҳ���ǿͻ�������ĵ� N ��Ƶ������
            // pubsub_channels ���ֵ�ΪƵ������
            // ��ֵ���Ǳ����� c->argv[j] Ƶ�����ж����ߵ�����
            // ������ dictFetchValue Ҳ����ȡ�����ж��ĸ���Ƶ���Ŀͻ���
            list *l = dictFetchValue(server.pubsub_channels,c->argv[j]);

            addReplyBulk(c,c->argv[j]);
            // ��ͻ��˷�������ĳ�������
            // ������Ծ���ĳ��Ƶ���Ķ���������
            // ���磺���һ��Ƶ�������������ߣ���ô����ĳ��Ⱦ��� 3
            // �����ظ��ͻ��˵�����Ҳ����
            addReplyBulkLongLong(c,l ? listLength(l) : 0);
        }

    // PUBSUB NUMPAT ������
    } else if (!strcasecmp(c->argv[1]->ptr,"numpat") && c->argc == 2) {
        /* PUBSUB NUMPAT */

        // pubsub_patterns �������˷����������б����ĵ�ģʽ
        // pubsub_patterns �ĳ��Ⱦ��Ƿ������б�����ģʽ������
        addReplyLongLong(c,listLength(server.pubsub_patterns));

    // ������
    } else {
        addReplyErrorFormat(c,
            "Unknown PUBSUB subcommand or wrong number of arguments for '%s'",
            (char*)c->argv[1]->ptr);
    }
}
