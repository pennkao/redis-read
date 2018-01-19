/*
 * Copyright (c) 2013, Salvatore Sanfilippo <antirez at gmail dot com>
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

/* This file implements keyspace events notification via Pub/Sub ad
 * described at http://redis.io/topics/keyspace-events. */

/* Turn a string representing notification classes into an integer
 * representing notification classes flags xored.
 *
 * �Դ�����ַ����������з����� ������Ӧ�� flags ֵ
 *
 * The function returns -1 if the input contains characters not mapping to
 * any class. 
 *
 * ���������ַ������в���ʶ����ַ�������ô���� -1 ��
 */
int keyspaceEventsStringToFlags(char *classes) {
    char *p = classes;
    int c, flags = 0;

    while((c = *p++) != '\0') {
        switch(c) {
        case 'A': flags |= REDIS_NOTIFY_ALL; break;
        case 'g': flags |= REDIS_NOTIFY_GENERIC; break;
        case '$': flags |= REDIS_NOTIFY_STRING; break;
        case 'l': flags |= REDIS_NOTIFY_LIST; break;
        case 's': flags |= REDIS_NOTIFY_SET; break;
        case 'h': flags |= REDIS_NOTIFY_HASH; break;
        case 'z': flags |= REDIS_NOTIFY_ZSET; break;
        case 'x': flags |= REDIS_NOTIFY_EXPIRED; break;
        case 'e': flags |= REDIS_NOTIFY_EVICTED; break;
        case 'K': flags |= REDIS_NOTIFY_KEYSPACE; break;
        case 'E': flags |= REDIS_NOTIFY_KEYEVENT; break;
        // ����ʶ��
        default: return -1;
        }
    }

    return flags;
}

/* This function does exactly the revese of the function above: it gets
 * as input an integer with the xored flags and returns a string representing
 * the selected classes. The string returned is an sds string that needs to
 * be released with sdsfree(). */
/*
 * ���� flags ֵ��ԭ������� flags ������ַ���
 */
sds keyspaceEventsFlagsToString(int flags) {
    sds res;

    res = sdsempty();
    if ((flags & REDIS_NOTIFY_ALL) == REDIS_NOTIFY_ALL) {
        res = sdscatlen(res,"A",1);
    } else {
        if (flags & REDIS_NOTIFY_GENERIC) res = sdscatlen(res,"g",1);
        if (flags & REDIS_NOTIFY_STRING) res = sdscatlen(res,"$",1);
        if (flags & REDIS_NOTIFY_LIST) res = sdscatlen(res,"l",1);
        if (flags & REDIS_NOTIFY_SET) res = sdscatlen(res,"s",1);
        if (flags & REDIS_NOTIFY_HASH) res = sdscatlen(res,"h",1);
        if (flags & REDIS_NOTIFY_ZSET) res = sdscatlen(res,"z",1);
        if (flags & REDIS_NOTIFY_EXPIRED) res = sdscatlen(res,"x",1);
        if (flags & REDIS_NOTIFY_EVICTED) res = sdscatlen(res,"e",1);
    }
    if (flags & REDIS_NOTIFY_KEYSPACE) res = sdscatlen(res,"K",1);
    if (flags & REDIS_NOTIFY_KEYEVENT) res = sdscatlen(res,"E",1);

    return res;
}

/* The API provided to the rest of the Redis core is a simple function:
 *
 * notifyKeyspaceEvent(char *event, robj *key, int dbid);
 *
 * 'event' is a C string representing the event name.
 *
 * event ������һ���ַ�����ʾ���¼���
 *
 * 'key' is a Redis object representing the key name.
 *
 * key ������һ�� Redis �����ʾ�ļ���
 *
 * 'dbid' is the database ID where the key lives.  
 *
 * dbid ����Ϊ�����ڵ����ݿ�
 */

/*
������type�����ǵ�ǰ��Ҫ���͵�֪ͨ�����ͣ������������ֵ���ж�֪ͨ�Ƿ���Ƿ���������notify- keyspace-eventsѡ����ѡ����֪ͨ���ͣ�
�Ӷ������Ƿ���֪ͨ��
  event��keys��dbid�ֱ����¼������ơ������¼��ļ����Լ������¼������ݿ��
�룬���������type�����Լ������������������¼�֪ͨ�����ݣ��Լ�����֪ͨ��Ƶ������
  ÿ��һ��Redis������Ҫ�������ݿ�֪ͨ��ʱ�򣬸������ʵ�ֺ����ͻ����notify-
KeyspaceEvent���������������ݴ��ݸ��������������¼��������Ϣ��


����:��SADD�������ٳɹ����򼯺������һ������Ԫ��֮������ͻᷢ��֪ͨ����֪ͨ
������ΪREDIS NOTIFY SET����ʾ����һ�����ϼ�֪ͨ��������Ϊsacid����ʾ����ִ
��SADD������������֪ͨ����
*/ //���ռ�֪ͨʹ�ÿͻ��˿���ͨ������Ƶ����ģʽ�� ��������Щ��ĳ�ַ�ʽ�Ķ��� Redis ���ݼ����¼���
void notifyKeyspaceEvent(int type, char *event, robj *key, int dbid) {
    sds chan;
    robj *chanobj, *eventobj;
    int len = -1;
    char buf[24];

    /* If notifications for this class of events are off, return ASAP. */
    // �������������Ϊ������ type ���͵�֪ͨ����ôֱ�ӷ���
    if (!(server.notify_keyspace_events & type)) return;

    // �¼�������
    eventobj = createStringObject(event,strlen(event));

    /* __keyspace@<db>__:<key> <event> notifications. */
    // ���ͼ��ռ�֪ͨ
    if (server.notify_keyspace_events & REDIS_NOTIFY_KEYSPACE) {

        // ����Ƶ������
        chan = sdsnewlen("__keyspace@",11);
        len = ll2string(buf,sizeof(buf),dbid);
        chan = sdscatlen(chan, buf, len);
        chan = sdscatlen(chan, "__:", 3);
        chan = sdscatsds(chan, key->ptr);

        chanobj = createObject(REDIS_STRING, chan);

        // ͨ�� publish �����֪ͨ
        pubsubPublishMessage(chanobj, eventobj);

        // �ͷ�Ƶ������
        decrRefCount(chanobj);
    }

    /* __keyevente@<db>__:<event> <key> notifications. */
    // ���ͼ��¼�֪ͨ
    if (server.notify_keyspace_events & REDIS_NOTIFY_KEYEVENT) {

        // ����Ƶ������
        chan = sdsnewlen("__keyevent@",11);
        // �����ǰ�淢�ͼ��ռ�֪ͨ��ʱ������� len ����ô���Ͳ����� -1
        // ����Ա���������� buf �ĳ���
        if (len == -1) len = ll2string(buf,sizeof(buf),dbid);
        chan = sdscatlen(chan, buf, len);
        chan = sdscatlen(chan, "__:", 3);
        chan = sdscatsds(chan, eventobj->ptr);

        chanobj = createObject(REDIS_STRING, chan);

        // ͨ�� publish �����֪ͨ
        pubsubPublishMessage(chanobj, key);

        // �ͷ�Ƶ������
        decrRefCount(chanobj);
    }

    // �ͷ��¼�����
    decrRefCount(eventobj);
}
