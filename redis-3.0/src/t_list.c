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

void signalListAsReady(redisClient *c, robj *key);

/*-----------------------------------------------------------------------------
 * List API
 *----------------------------------------------------------------------------*/

/* Check the argument length to see if it requires us to convert the ziplist
 * to a real list. Only check raw-encoded objects because integer encoded
 * objects are never too long. 
 *
 * ������ֵ value ���м�飬���Ƿ���Ҫ�� subject �� ziplist ת��Ϊ˫������
 * �Ա㱣��ֵ value ��
 *
 * ����ֻ�� REDIS_ENCODING_RAW ����� value ���м�飬
 * ��Ϊ���������ֵ�����ܳ�����
 */
void listTypeTryConversion(robj *subject, robj *value) {

    // ȷ�� subject Ϊ ZIPLIST ����
    if (subject->encoding != REDIS_ENCODING_ZIPLIST) return;

    if (sdsEncodedObject(value) &&
        // ���ַ����Ƿ����
        sdslen(value->ptr) > server.list_max_ziplist_value)
            // ������ת��Ϊ˫������
            listTypeConvert(subject,REDIS_ENCODING_LINKEDLIST);
}

/* The function pushes an element to the specified list object 'subject',
 * at head or tail position as specified by 'where'.
 *
 * ������Ԫ����ӵ��б�ı�ͷ���β��
 *
 * ���� where ��������Ԫ����ӵ�λ�ã�
 *
 *  - REDIS_HEAD ����Ԫ����ӵ���ͷ
 *
 *  - REDIS_TAIL ����Ԫ����ӵ���β
 *
 * There is no need for the caller to increment the refcount of 'value' as
 * the function takes care of it if needed. 
 *
 * ���������뵣�� value �����ü�������Ϊ��������Ḻ���ⷽ��Ĺ�����
 */
void listTypePush(robj *subject, robj *value, int where) {

    /* Check if we need to convert the ziplist */
    // �Ƿ���Ҫת�����룿
    listTypeTryConversion(subject,value);

    if (subject->encoding == REDIS_ENCODING_ZIPLIST &&
        ziplistLen(subject->ptr) >= server.list_max_ziplist_entries)
            listTypeConvert(subject,REDIS_ENCODING_LINKEDLIST);

    // ZIPLIST
    if (subject->encoding == REDIS_ENCODING_ZIPLIST) {
        int pos = (where == REDIS_HEAD) ? ZIPLIST_HEAD : ZIPLIST_TAIL;
        // ȡ�������ֵ����Ϊ ZIPLIST ֻ�ܱ����ַ���������
        value = getDecodedObject(value);
        subject->ptr = ziplistPush(subject->ptr,value->ptr,sdslen(value->ptr),pos);
        decrRefCount(value);

    // ˫������
    } else if (subject->encoding == REDIS_ENCODING_LINKEDLIST) {
        if (where == REDIS_HEAD) {
            listAddNodeHead(subject->ptr,value);
        } else {
            listAddNodeTail(subject->ptr,value);
        }
        incrRefCount(value);

    // δ֪����
    } else {
        redisPanic("Unknown list encoding");
    }
}

/*
 * ���б�ı�ͷ���β�е���һ��Ԫ�ء�
 *
 * ���� where �����˵���Ԫ�ص�λ�ã� 
 *
 *  - REDIS_HEAD �ӱ�ͷ����
 *
 *  - REDIS_TAIL �ӱ�β����
 */
robj *listTypePop(robj *subject, int where) {

    robj *value = NULL;

    // ZIPLIST
    if (subject->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *p;
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;

        // ��������Ԫ�ص�λ��
        int pos = (where == REDIS_HEAD) ? 0 : -1;

        p = ziplistIndex(subject->ptr,pos);
        if (ziplistGet(p,&vstr,&vlen,&vlong)) {
            // Ϊ������Ԫ�ش�������
            if (vstr) {
                value = createStringObject((char*)vstr,vlen);
            } else {
                value = createStringObjectFromLongLong(vlong);
            }
            /* We only need to delete an element when it exists */
            // �� ziplist ��ɾ��������Ԫ��
            subject->ptr = ziplistDelete(subject->ptr,&p);
        }

    // ˫������
    } else if (subject->encoding == REDIS_ENCODING_LINKEDLIST) {

        list *list = subject->ptr;

        listNode *ln;

        if (where == REDIS_HEAD) {
            ln = listFirst(list);
        } else {
            ln = listLast(list);
        }

        // ɾ���������ڵ�
        if (ln != NULL) {
            value = listNodeValue(ln);
            incrRefCount(value);
            listDelNode(list,ln);
        }

    // δ֪����
    } else {
        redisPanic("Unknown list encoding");
    }

    // ���ؽڵ����
    return value;
}

/*
 * �����б�Ľڵ�����
 */
unsigned long listTypeLength(robj *subject) {

    // ZIPLIST
    if (subject->encoding == REDIS_ENCODING_ZIPLIST) {
        return ziplistLen(subject->ptr);

    // ˫������
    } else if (subject->encoding == REDIS_ENCODING_LINKEDLIST) {
        return listLength((list*)subject->ptr);

    // δ֪����
    } else {
        redisPanic("Unknown list encoding");
    }
}

/* Initialize an iterator at the specified index.
 *
 * ����������һ���б��������
 *
 * ���� index ������ʼ�������б�������
 *
 * ���� direction ������˵����ķ���
 *
 * listTypeIterator �� redis.h �ļ��ж��塣
 */
listTypeIterator *listTypeInitIterator(robj *subject, long index, unsigned char direction) {

    listTypeIterator *li = zmalloc(sizeof(listTypeIterator));

    li->subject = subject;

    li->encoding = subject->encoding;

    li->direction = direction;

    // ZIPLIST
    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        li->zi = ziplistIndex(subject->ptr,index);

    // ˫������
    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {
        li->ln = listIndex(subject->ptr,index);

    // δ֪����
    } else {
        redisPanic("Unknown list encoding");
    }

    return li;
}

/* Clean up the iterator. 
 *
 * �ͷŵ�����
 */
void listTypeReleaseIterator(listTypeIterator *li) {
    zfree(li);
}

/* Stores pointer to current the entry in the provided entry structure
 * and advances the position of the iterator. Returns 1 when the current
 * entry is in fact an entry, 0 otherwise. 
 *
 * ʹ�� entry �ṹ��¼��������ǰָ��Ľڵ㣬������������ָ���ƶ�����һ��Ԫ�ء�
 *
 * ����б��л���Ԫ�ؿɵ�������ô���� 1 �����򣬷��� 0 ��
 */
int listTypeNext(listTypeIterator *li, listTypeEntry *entry) {
    /* Protect from converting when iterating */
    redisAssert(li->subject->encoding == li->encoding);

    entry->li = li;

    // ���� ZIPLIST
    if (li->encoding == REDIS_ENCODING_ZIPLIST) {

        // ��¼��ǰ�ڵ㵽 entry
        entry->zi = li->zi;

        // �ƶ���������ָ��
        if (entry->zi != NULL) {
            if (li->direction == REDIS_TAIL)
                li->zi = ziplistNext(li->subject->ptr,li->zi);
            else
                li->zi = ziplistPrev(li->subject->ptr,li->zi);
            return 1;
        }

    // ����˫������
    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {

        // ��¼��ǰ�ڵ㵽 entry
        entry->ln = li->ln;

        // �ƶ���������ָ��
        if (entry->ln != NULL) {
            if (li->direction == REDIS_TAIL)
                li->ln = li->ln->next;
            else
                li->ln = li->ln->prev;
            return 1;
        }

    // δ֪����
    } else {
        redisPanic("Unknown list encoding");
    }

    // �б�Ԫ���Ѿ�ȫ��������
    return 0;
}

/* Return entry or NULL at the current position of the iterator. 
 *
 * ���� entry �ṹ��ǰ��������б�ڵ㡣
 *
 * ��� entry û�м�¼�κνڵ㣬��ô���� NULL ��
 */
robj *listTypeGet(listTypeEntry *entry) {

    listTypeIterator *li = entry->li;

    robj *value = NULL;

    // ������������ ZIPLIST ��ȡ���ڵ��ֵ
    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;
        redisAssert(entry->zi != NULL);
        if (ziplistGet(entry->zi,&vstr,&vlen,&vlong)) {
            if (vstr) {
                value = createStringObject((char*)vstr,vlen);
            } else {
                value = createStringObjectFromLongLong(vlong);
            }
        }

    // ��˫��������ȡ���ڵ��ֵ
    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {
        redisAssert(entry->ln != NULL);
        value = listNodeValue(entry->ln);
        incrRefCount(value);

    } else {
        redisPanic("Unknown list encoding");
    }

    return value;
}

/*
 * ������ value ���뵽�б�ڵ��֮ǰ��֮��
 *
 * where ���������˲����λ�ã�
 *
 *  - REDIS_HEAD ���뵽�ڵ�֮ǰ
 *
 *  - REDIS_TAIL ���뵽�ڵ�֮��
 */
void listTypeInsert(listTypeEntry *entry, robj *value, int where) {

    robj *subject = entry->li->subject;

    // ���뵽 ZIPLIST
    if (entry->li->encoding == REDIS_ENCODING_ZIPLIST) {

        // ���ض���δ�����ֵ
        value = getDecodedObject(value);

        if (where == REDIS_TAIL) {
            unsigned char *next = ziplistNext(subject->ptr,entry->zi);

            /* When we insert after the current element, but the current element
             * is the tail of the list, we need to do a push. */
            if (next == NULL) {
                // next �Ǳ�β�ڵ㣬push �½ڵ㵽��β
                subject->ptr = ziplistPush(subject->ptr,value->ptr,sdslen(value->ptr),REDIS_TAIL);
            } else {
                // ���뵽���ڵ�֮��
                subject->ptr = ziplistInsert(subject->ptr,next,value->ptr,sdslen(value->ptr));
            }
        } else {
            subject->ptr = ziplistInsert(subject->ptr,entry->zi,value->ptr,sdslen(value->ptr));
        }
        decrRefCount(value);

    // ���뵽˫������
    } else if (entry->li->encoding == REDIS_ENCODING_LINKEDLIST) {

        if (where == REDIS_TAIL) {
            listInsertNode(subject->ptr,entry->ln,value,AL_START_TAIL);
        } else {
            listInsertNode(subject->ptr,entry->ln,value,AL_START_HEAD);
        }

        incrRefCount(value);

    } else {
        redisPanic("Unknown list encoding");
    }
}

/* Compare the given object with the entry at the current position. 
 *
 * ����ǰ�ڵ��ֵ�Ͷ��� o ���жԱ�
 *
 * ��������ֵ���ʱ���� 1 �������ʱ���� 0 ��
 */
int listTypeEqual(listTypeEntry *entry, robj *o) {

    listTypeIterator *li = entry->li;

    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        redisAssertWithInfo(NULL,o,sdsEncodedObject(o));
        return ziplistCompare(entry->zi,o->ptr,sdslen(o->ptr));

    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {
        return equalStringObjects(o,listNodeValue(entry->ln));

    } else {
        redisPanic("Unknown list encoding");
    }
}

/* Delete the element pointed to. 
 *
 * ɾ�� entry ��ָ��Ľڵ�
 */
void listTypeDelete(listTypeEntry *entry) {

    listTypeIterator *li = entry->li;

    // ZIPLIST
    if (li->encoding == REDIS_ENCODING_ZIPLIST) {

        unsigned char *p = entry->zi;

        li->subject->ptr = ziplistDelete(li->subject->ptr,&p);

        /* Update position of the iterator depending on the direction */
        // ɾ���ڵ�֮�󣬸��µ�������ָ��
        if (li->direction == REDIS_TAIL)
            li->zi = p;
        else
            li->zi = ziplistPrev(li->subject->ptr,p);

    // ˫������
    } else if (entry->li->encoding == REDIS_ENCODING_LINKEDLIST) {

        // ��¼���ýڵ�
        listNode *next;

        if (li->direction == REDIS_TAIL)
            next = entry->ln->next;
        else
            next = entry->ln->prev;

        // ɾ����ǰ�ڵ�
        listDelNode(li->subject->ptr,entry->ln);

        // ɾ���ڵ�֮�󣬸��µ�������ָ��
        li->ln = next;

    } else {
        redisPanic("Unknown list encoding");
    }
}

/*
 * ���б�ĵײ����� ziplist ת����˫������
 */
void listTypeConvert(robj *subject, int enc) {

    listTypeIterator *li;

    listTypeEntry entry;

    redisAssertWithInfo(NULL,subject,subject->type == REDIS_LIST);

    // ת����˫������
    if (enc == REDIS_ENCODING_LINKEDLIST) {

        list *l = listCreate();

        listSetFreeMethod(l,decrRefCountVoid);

        /* listTypeGet returns a robj with incremented refcount */
        // ���� ziplist �����������ֵȫ����ӵ�˫��������
        li = listTypeInitIterator(subject,0,REDIS_TAIL);
        while (listTypeNext(li,&entry)) listAddNodeTail(l,listTypeGet(&entry));
        listTypeReleaseIterator(li);

        // ���±���
        subject->encoding = REDIS_ENCODING_LINKEDLIST;

        // �ͷ�ԭ���� ziplist
        zfree(subject->ptr);

        // ���¶���ֵָ��
        subject->ptr = l;

    } else {
        redisPanic("Unsupported list conversion");
    }
}

/*-----------------------------------------------------------------------------
 * List Commands
 *----------------------------------------------------------------------------*/
/*
lpush����ͨ��pushGenericCommand->createZiplistObject�����б����(Ĭ�ϱ��뷽ʽREDIS_ENCODING_ZIPLIST)��Ȼ����listTypePush->listTypeTryConversion��
�����б��нڵ����Ƿ�������ò���list_max_ziplist_value(Ĭ��64)�����������value�б�ֵ��ѹ�����Ϊ˫��������뷽ʽREDIS_ENCODING_LINKEDLIST����listTypePush->listTypeConvert
*/
void pushGenericCommand(redisClient *c, int where) {

    int j, waiting = 0, pushed = 0;

    // ȡ���б����
    robj *lobj = lookupKeyWrite(c->db,c->argv[1]);

    // ����б���󲻴��ڣ���ô�����пͻ����ڵȴ�������ĳ���
    int may_have_waiting_clients = (lobj == NULL);

    if (lobj && lobj->type != REDIS_LIST) {
        addReply(c,shared.wrongtypeerr);
        return;
    }

    // ���б�״̬����Ϊ����
    if (may_have_waiting_clients) signalListAsReady(c,c->argv[1]);

    // ������������ֵ������������ӵ��б���
    for (j = 2; j < c->argc; j++) {

        // ����ֵ
        c->argv[j] = tryObjectEncoding(c->argv[j]);

        // ����б���󲻴��ڣ���ô����һ���������������ݿ�
        if (!lobj) {
            lobj = createZiplistObject(); 
      /*
        lpush����ͨ��pushGenericCommand->createZiplistObject�����б����(Ĭ�ϱ��뷽ʽREDIS_ENCODING_ZIPLIST)��Ȼ����listTypePush->listTypeTryConversion��
        �����б��нڵ����Ƿ�������ò���list_max_ziplist_value(Ĭ��64)�����������value�б�ֵ��ѹ�����Ϊ˫��������뷽ʽREDIS_ENCODING_LINKEDLIST����listTypePush->listTypeConvert
        */
            dbAdd(c->db,c->argv[1],lobj);
        }

        // ��ֵ���뵽�б�
        listTypePush(lobj,c->argv[j],where);

        pushed++;
    }

    // ������ӵĽڵ�����
    addReplyLongLong(c, waiting + (lobj ? listTypeLength(lobj) : 0));

    // ���������һ��Ԫ�ر��ɹ����룬��ôִ�����´���
    if (pushed) {
        char *event = (where == REDIS_HEAD) ? "lpush" : "rpush";

        // ���ͼ��޸��ź�
        signalModifiedKey(c->db,c->argv[1]);

        // �����¼�֪ͨ
        notifyKeyspaceEvent(REDIS_NOTIFY_LIST,event,c->argv[1],c->db->id);
    }

    server.dirty += pushed;
}

void lpushCommand(redisClient *c) {
    pushGenericCommand(c,REDIS_HEAD);
}

void rpushCommand(redisClient *c) {
    pushGenericCommand(c,REDIS_TAIL);
}

void pushxGenericCommand(redisClient *c, robj *refval, robj *val, int where) {
    robj *subject;
    listTypeIterator *iter;
    listTypeEntry entry;
    int inserted = 0;

    // ȡ���б����
    if ((subject = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,subject,REDIS_LIST)) return;

    // ִ�е��� LINSERT ����
    if (refval != NULL) {
        /* We're not sure if this value can be inserted yet, but we cannot
         * convert the list inside the iterator. We don't want to loop over
         * the list twice (once to see if the value can be inserted and once
         * to do the actual insert), so we assume this value can be inserted
         * and convert the ziplist to a regular list if necessary. */
        // ������ֵ value �Ƿ���Ҫ���б����ת��Ϊ˫������
        listTypeTryConversion(subject,val);

        /* Seek refval from head to tail */
        // ���б��в��� refval ����
        iter = listTypeInitIterator(subject,0,REDIS_TAIL);
        while (listTypeNext(iter,&entry)) {
            if (listTypeEqual(&entry,refval)) {
                // �ҵ��ˣ���ֵ���뵽�ڵ��ǰ������
                listTypeInsert(&entry,val,where);
                inserted = 1;
                break;
            }
        }
        listTypeReleaseIterator(iter);

        if (inserted) {
            /* Check if the length exceeds the ziplist length threshold. */
            // �鿴����֮���Ƿ���Ҫ������ת��Ϊ˫������
            if (subject->encoding == REDIS_ENCODING_ZIPLIST &&
                ziplistLen(subject->ptr) > server.list_max_ziplist_entries)
                    listTypeConvert(subject,REDIS_ENCODING_LINKEDLIST);

            signalModifiedKey(c->db,c->argv[1]);

            notifyKeyspaceEvent(REDIS_NOTIFY_LIST,"linsert",
                                c->argv[1],c->db->id);
            server.dirty++;
        } else {
            /* Notify client of a failed insert */
            // refval �����ڣ�����ʧ��
            addReply(c,shared.cnegone);
            return;
        }

    // ִ�е��� LPUSHX �� RPUSHX ����
    } else {
        char *event = (where == REDIS_HEAD) ? "lpush" : "rpush";

        listTypePush(subject,val,where);

        signalModifiedKey(c->db,c->argv[1]);

        notifyKeyspaceEvent(REDIS_NOTIFY_LIST,event,c->argv[1],c->db->id);

        server.dirty++;
    }

    addReplyLongLong(c,listTypeLength(subject));
}

void lpushxCommand(redisClient *c) {
    c->argv[2] = tryObjectEncoding(c->argv[2]);
    pushxGenericCommand(c,NULL,c->argv[2],REDIS_HEAD);
}

void rpushxCommand(redisClient *c) {
    c->argv[2] = tryObjectEncoding(c->argv[2]);
    pushxGenericCommand(c,NULL,c->argv[2],REDIS_TAIL);
}

void linsertCommand(redisClient *c) {

    // ���� refval ����
    c->argv[4] = tryObjectEncoding(c->argv[4]);

    if (strcasecmp(c->argv[2]->ptr,"after") == 0) {
        pushxGenericCommand(c,c->argv[3],c->argv[4],REDIS_TAIL);

    } else if (strcasecmp(c->argv[2]->ptr,"before") == 0) {
        pushxGenericCommand(c,c->argv[3],c->argv[4],REDIS_HEAD);

    } else {
        addReply(c,shared.syntaxerr);
    }
}

void llenCommand(redisClient *c) {

    robj *o = lookupKeyReadOrReply(c,c->argv[1],shared.czero);

    if (o == NULL || checkType(c,o,REDIS_LIST)) return;

    addReplyLongLong(c,listTypeLength(o));
}

void lindexCommand(redisClient *c) {

    robj *o = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk);

    if (o == NULL || checkType(c,o,REDIS_LIST)) return;
    long index;
    robj *value = NULL;

    // ȡ������ֵ���� index
    if ((getLongFromObjectOrReply(c, c->argv[2], &index, NULL) != REDIS_OK))
        return;

    // �������������� ziplist ��ֱ��ָ��λ��
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *p;
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;

        p = ziplistIndex(o->ptr,index);

        if (ziplistGet(p,&vstr,&vlen,&vlong)) {
            if (vstr) {
                value = createStringObject((char*)vstr,vlen);
            } else {
                value = createStringObjectFromLongLong(vlong);
            }
            addReplyBulk(c,value);
            decrRefCount(value);
        } else {
            addReply(c,shared.nullbulk);
        }

    // ��������������˫������ֱ��ָ��λ��
    } else if (o->encoding == REDIS_ENCODING_LINKEDLIST) {

        listNode *ln = listIndex(o->ptr,index);

        if (ln != NULL) {
            value = listNodeValue(ln);
            addReplyBulk(c,value);
        } else {
            addReply(c,shared.nullbulk);
        }
    } else {
        redisPanic("Unknown list encoding");
    }
}

void lsetCommand(redisClient *c) {

    // ȡ���б����
    robj *o = lookupKeyWriteOrReply(c,c->argv[1],shared.nokeyerr);

    if (o == NULL || checkType(c,o,REDIS_LIST)) return;
    long index;

    // ȡ��ֵ���� value
    robj *value = (c->argv[3] = tryObjectEncoding(c->argv[3]));

    // ȡ������ֵ���� index
    if ((getLongFromObjectOrReply(c, c->argv[2], &index, NULL) != REDIS_OK))
        return;

    // �鿴���� value ֵ�Ƿ���Ҫת���б�ĵײ����
    listTypeTryConversion(o,value);

    // ���õ� ziplist
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *p, *zl = o->ptr;
        // ��������
        p = ziplistIndex(zl,index);
        if (p == NULL) {
            addReply(c,shared.outofrangeerr);
        } else {
            // ɾ�����е�ֵ
            o->ptr = ziplistDelete(o->ptr,&p);
            // ������ֵ��ָ������
            value = getDecodedObject(value);
            o->ptr = ziplistInsert(o->ptr,p,value->ptr,sdslen(value->ptr));
            decrRefCount(value);

            addReply(c,shared.ok);
            signalModifiedKey(c->db,c->argv[1]);
            notifyKeyspaceEvent(REDIS_NOTIFY_LIST,"lset",c->argv[1],c->db->id);
            server.dirty++;
        }

    // ���õ�˫������
    } else if (o->encoding == REDIS_ENCODING_LINKEDLIST) {

        listNode *ln = listIndex(o->ptr,index);

        if (ln == NULL) {
            addReply(c,shared.outofrangeerr);
        } else {
            // ɾ����ֵ����
            decrRefCount((robj*)listNodeValue(ln));
            // ָ���¶���
            listNodeValue(ln) = value;
            incrRefCount(value);

            addReply(c,shared.ok);
            signalModifiedKey(c->db,c->argv[1]);
            notifyKeyspaceEvent(REDIS_NOTIFY_LIST,"lset",c->argv[1],c->db->id);
            server.dirty++;
        }
    } else {
        redisPanic("Unknown list encoding");
    }
}

void popGenericCommand(redisClient *c, int where) {

    // ȡ���б����
    robj *o = lookupKeyWriteOrReply(c,c->argv[1],shared.nullbulk);

    if (o == NULL || checkType(c,o,REDIS_LIST)) return;

    // �����б�Ԫ��
    robj *value = listTypePop(o,where);

    // ���ݵ���Ԫ���Ƿ�Ϊ�գ�������������
    if (value == NULL) {
        addReply(c,shared.nullbulk);
    } else {
        char *event = (where == REDIS_HEAD) ? "lpop" : "rpop";

        addReplyBulk(c,value);
        decrRefCount(value);
        notifyKeyspaceEvent(REDIS_NOTIFY_LIST,event,c->argv[1],c->db->id);
        if (listTypeLength(o) == 0) {
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",
                                c->argv[1],c->db->id);
            dbDelete(c->db,c->argv[1]);
        }
        signalModifiedKey(c->db,c->argv[1]);
        server.dirty++;
    }
}

void lpopCommand(redisClient *c) {
    popGenericCommand(c,REDIS_HEAD);
}

void rpopCommand(redisClient *c) {
    popGenericCommand(c,REDIS_TAIL);
}

void lrangeCommand(redisClient *c) {
    robj *o;
    long start, end, llen, rangelen;

    // ȡ������ֵ start �� end
    if ((getLongFromObjectOrReply(c, c->argv[2], &start, NULL) != REDIS_OK) ||
        (getLongFromObjectOrReply(c, c->argv[3], &end, NULL) != REDIS_OK)) return;

    // ȡ���б����
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptymultibulk)) == NULL
         || checkType(c,o,REDIS_LIST)) return;

    // ȡ���б���
    llen = listTypeLength(o);

    /* convert negative indexes */
    // ����������ת������������
    if (start < 0) start = llen+start;
    if (end < 0) end = llen+end;
    if (start < 0) start = 0;

    /* Invariant: start >= 0, so this test will be true when end < 0.
     * The range is empty when start > end or start >= length. */
    if (start > end || start >= llen) {
        addReply(c,shared.emptymultibulk);
        return;
    }
    if (end >= llen) end = llen-1;
    rangelen = (end-start)+1;

    /* Return the result in form of a multi-bulk reply */
    addReplyMultiBulkLen(c,rangelen);

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *p = ziplistIndex(o->ptr,start);
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;

        // ���� ziplist ������ָ�������ϵ�ֵ��ӵ��ظ���
        while(rangelen--) {
            ziplistGet(p,&vstr,&vlen,&vlong);
            if (vstr) {
                addReplyBulkCBuffer(c,vstr,vlen);
            } else {
                addReplyBulkLongLong(c,vlong);
            }
            p = ziplistNext(o->ptr,p);
        }

    } else if (o->encoding == REDIS_ENCODING_LINKEDLIST) {
        listNode *ln;

        /* If we are nearest to the end of the list, reach the element
         * starting from tail and going backward, as it is faster. */
        if (start > llen/2) start -= llen;
        ln = listIndex(o->ptr,start);

        // ����˫��������ָ�������ϵ�ֵ��ӵ��ظ�
        while(rangelen--) {
            addReplyBulk(c,ln->value);
            ln = ln->next;
        }

    } else {
        redisPanic("List encoding is not LINKEDLIST nor ZIPLIST!");
    }
}

void ltrimCommand(redisClient *c) {
    robj *o;
    long start, end, llen, j, ltrim, rtrim;
    list *list;
    listNode *ln;

    // ȡ������ֵ start �� end
    if ((getLongFromObjectOrReply(c, c->argv[2], &start, NULL) != REDIS_OK) ||
        (getLongFromObjectOrReply(c, c->argv[3], &end, NULL) != REDIS_OK)) return;

    // ȡ���б����
    if ((o = lookupKeyWriteOrReply(c,c->argv[1],shared.ok)) == NULL ||
        checkType(c,o,REDIS_LIST)) return;

    // �б���
    llen = listTypeLength(o);

    /* convert negative indexes */
    // ����������ת������������
    if (start < 0) start = llen+start;
    if (end < 0) end = llen+end;
    if (start < 0) start = 0;

    /* Invariant: start >= 0, so this test will be true when end < 0.
     * The range is empty when start > end or start >= length. */
    if (start > end || start >= llen) {
        /* Out of range start or start > end result in empty list */
        ltrim = llen;
        rtrim = 0;
    } else {
        if (end >= llen) end = llen-1;
        ltrim = start;
        rtrim = llen-end-1;
    }

    /* Remove list elements to perform the trim */
    // ɾ��ָ���б����˵�Ԫ��

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        // ɾ�����Ԫ��
        o->ptr = ziplistDeleteRange(o->ptr,0,ltrim);
        // ɾ���Ҷ�Ԫ��
        o->ptr = ziplistDeleteRange(o->ptr,-rtrim,rtrim);

    } else if (o->encoding == REDIS_ENCODING_LINKEDLIST) {
        list = o->ptr;
        // ɾ�����Ԫ��
        for (j = 0; j < ltrim; j++) {
            ln = listFirst(list);
            listDelNode(list,ln);
        }
        // ɾ���Ҷ�Ԫ��
        for (j = 0; j < rtrim; j++) {
            ln = listLast(list);
            listDelNode(list,ln);
        }

    } else {
        redisPanic("Unknown list encoding");
    }

    // ����֪ͨ
    notifyKeyspaceEvent(REDIS_NOTIFY_LIST,"ltrim",c->argv[1],c->db->id);

    // ����б��Ѿ�Ϊ�գ���ôɾ����
    if (listTypeLength(o) == 0) {
        dbDelete(c->db,c->argv[1]);
        notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1],c->db->id);
    }

    signalModifiedKey(c->db,c->argv[1]);

    server.dirty++;

    addReply(c,shared.ok);
}

void lremCommand(redisClient *c) {
    robj *subject, *obj;

    // ����Ŀ����� elem
    obj = c->argv[3] = tryObjectEncoding(c->argv[3]);
    long toremove;
    long removed = 0;
    listTypeEntry entry;

    // ȡ��ָ��ɾ��ģʽ�� count ����
    if ((getLongFromObjectOrReply(c, c->argv[2], &toremove, NULL) != REDIS_OK))
        return;

    // ȡ���б����
    subject = lookupKeyWriteOrReply(c,c->argv[1],shared.czero);
    if (subject == NULL || checkType(c,subject,REDIS_LIST)) return;

    /* Make sure obj is raw when we're dealing with a ziplist */
    if (subject->encoding == REDIS_ENCODING_ZIPLIST)
        obj = getDecodedObject(obj);

    listTypeIterator *li;

    // ���� toremove �����������Ǵӱ�ͷ���Ǳ�β��ʼ����ɾ��
    if (toremove < 0) {
        toremove = -toremove;
        li = listTypeInitIterator(subject,-1,REDIS_HEAD);
    } else {
        li = listTypeInitIterator(subject,0,REDIS_TAIL);
    }

    // ���ң��ȶԶ��󣬲�����ɾ��
    while (listTypeNext(li,&entry)) {
        if (listTypeEqual(&entry,obj)) {
            listTypeDelete(&entry);
            server.dirty++;
            removed++;
            // �Ѿ�����ɾ��������ֹͣ
            if (toremove && removed == toremove) break;
        }
    }
    listTypeReleaseIterator(li);

    /* Clean up raw encoded object */
    if (subject->encoding == REDIS_ENCODING_ZIPLIST)
        decrRefCount(obj);

    // ɾ�����б�
    if (listTypeLength(subject) == 0) dbDelete(c->db,c->argv[1]);

    addReplyLongLong(c,removed);

    if (removed) signalModifiedKey(c->db,c->argv[1]);
}

/* This is the semantic of this command:
 *  RPOPLPUSH srclist dstlist:
 *    IF LLEN(srclist) > 0
 *      element = RPOP srclist
 *      LPUSH dstlist element
 *      RETURN element
 *    ELSE
 *      RETURN nil
 *    END
 *  END
 *
 * The idea is to be able to get an element from a list in a reliable way
 * since the element is not just returned but pushed against another list
 * as well. This command was originally proposed by Ezra Zygmuntowicz.
 */

void rpoplpushHandlePush(redisClient *c, robj *dstkey, robj *dstobj, robj *value) {
    /* Create the list if the key does not exist */
    // ���Ŀ���б����ڣ���ô����һ��
    if (!dstobj) {
        dstobj = createZiplistObject();
        dbAdd(c->db,dstkey,dstobj);
        signalListAsReady(c,dstkey);
    }

    signalModifiedKey(c->db,dstkey);

    // ��ֵ����Ŀ���б���
    listTypePush(dstobj,value,REDIS_HEAD);

    notifyKeyspaceEvent(REDIS_NOTIFY_LIST,"lpush",dstkey,c->db->id);
    /* Always send the pushed value to the client. */
    addReplyBulk(c,value);
}

void rpoplpushCommand(redisClient *c) {
    robj *sobj, *value;
    
    // ��Դ�б�
    if ((sobj = lookupKeyWriteOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,sobj,REDIS_LIST)) return;

    // ���б�û��Ԫ�ؿ� pop ��ֱ�ӷ���
    if (listTypeLength(sobj) == 0) {
        /* This may only happen after loading very old RDB files. Recent
         * versions of Redis delete keys of empty lists. */
        addReply(c,shared.nullbulk);

    // Դ�б�ǿ�
    } else {

        // Ŀ�����
        robj *dobj = lookupKeyWrite(c->db,c->argv[2]);
        robj *touchedkey = c->argv[1];

        // ���Ŀ������Ƿ��б�
        if (dobj && checkType(c,dobj,REDIS_LIST)) return;
        // ��Դ�б��е���ֵ
        value = listTypePop(sobj,REDIS_TAIL);
        /* We saved touched key, and protect it, since rpoplpushHandlePush
         * may change the client command argument vector (it does not
         * currently). */
        incrRefCount(touchedkey);
        // ��ֵ����Ŀ���б��У����Ŀ���б����ڣ���ô����һ�����б�
        rpoplpushHandlePush(c,c->argv[2],dobj,value);

        /* listTypePop returns an object with its refcount incremented */
        decrRefCount(value);

        /* Delete the source list when it is empty */
        notifyKeyspaceEvent(REDIS_NOTIFY_LIST,"rpop",touchedkey,c->db->id);

        // ���Դ�б��Ѿ�Ϊ�գ���ô����ɾ��
        if (listTypeLength(sobj) == 0) {
            dbDelete(c->db,touchedkey);
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",
                                touchedkey,c->db->id);
        }

        signalModifiedKey(c->db,touchedkey);

        decrRefCount(touchedkey);

        server.dirty++;
    }
}

/*-----------------------------------------------------------------------------
 * Blocking POP operations
 *----------------------------------------------------------------------------*/

/* This is how the current blocking POP works, we use BLPOP as example:
 *
 * ������Ŀǰ������ POP ������������������ BLPOP ��Ϊ���ӣ�
 *
 * - If the user calls BLPOP and the key exists and contains a non empty list
 *   then LPOP is called instead. So BLPOP is semantically the same as LPOP
 *   if blocking is not required.
 *
 * - ����û����� BLPOP �������б�ǿգ���ô����ִ�� LPOP ��
 *   ��ˣ����б�ǿ�ʱ������ BLPOP ���ڵ��� LPOP��
 *
 * - If instead BLPOP is called and the key does not exists or the list is
 *   empty we need to block. In order to do so we remove the notification for
 *   new data to read in the client socket (so that we'll not serve new
 *   requests if the blocking request is not served). Also we put the client
 *   in a dictionary (db->blocking_keys) mapping keys to a list of clients
 *   blocking for this keys.
 *
 * - �� BLPOP ��һ���ռ�ִ��ʱ���ͻ��˲Żᱻ������
 *   ���������ٶ�����ͻ��˷����κ����ݣ�
 *   ������ͻ��˵�״̬��Ϊ������������ֱ���������Ϊֹ��
 *   ���ҿͻ��˻ᱻ���뵽һ����������Ϊ key ��
 *   �Ա������ͻ���Ϊ value ���ֵ� db->blocking_keys �С�
 *
 * - If a PUSH operation against a key with blocked clients waiting is
 *   performed, we mark this key as "ready", and after the current command,
 *   MULTI/EXEC block, or script, is executed, we serve all the clients waiting
 *   for this list, from the one that blocked first, to the last, accordingly
 *   to the number of elements we have in the ready list.
 *
 * - ���� PUSH ����������һ����ɿͻ��������ļ�ʱ��
 *   ������������Ϊ����������������ִ�������������񡢻�ű�֮��
 *   ����ᰴ���������ȷ��񡱵�˳�򣬽��б��Ԫ�ط��ظ���Щ�������Ŀͻ��ˣ�
 *   ����������Ŀͻ�������ȡ���� PUSH ���������Ԫ��������
 */

/* Set a client in blocking mode for the specified key, with the specified
 * timeout */
// ���ݸ��������� key ���Ը����ͻ��˽�������
// ������
// keys    ������ key
// numkeys keys �ļ�����
// timeout �������ʱ��
// target  �ڽ������ʱ����������浽��� key ���󣬶����Ƿ��ظ��ͻ���
//         ֻ���� BRPOPLPUSH ����
void blockForKeys(redisClient *c, robj **keys, int numkeys, mstime_t timeout, robj *target) {
    dictEntry *de;
    list *l;
    int j;

    // ��������״̬�ĳ�ʱ��Ŀ��ѡ��
    c->bpop.timeout = timeout;

    // target ��ִ�� RPOPLPUSH ����ʱʹ��
    c->bpop.target = target;

    if (target != NULL) incrRefCount(target);

    // ���������ͻ��˺ͼ��������Ϣ
    for (j = 0; j < numkeys; j++) {

        /* If the key already exists in the dict ignore it. */
        // c->bpop.keys ��һ�����ϣ�ֵΪ NULL ���ֵ䣩
        // ����¼������ɿͻ��������ļ�
        // ��������ڼ��������ڼ��ϵ�ʱ�򣬽�����ӵ�����
        if (dictAdd(c->bpop.keys,keys[j],NULL) != DICT_OK) continue;

        incrRefCount(keys[j]);

        /* And in the other "side", to map keys -> clients */
        // c->db->blocking_keys �ֵ�ļ�Ϊ��ɿͻ��������ļ�
        // ��ֵ����һ�����������а��������б������Ŀͻ���
        // ���³����������ͱ������ͻ��˹�������
        de = dictFind(c->db->blocking_keys,keys[j]);
        if (de == NULL) {
            // �������ڣ��´���һ�����������������ֵ���
            int retval;

            /* For every key we take a list of clients blocked for it */
            l = listCreate();
            retval = dictAdd(c->db->blocking_keys,keys[j],l);
            incrRefCount(keys[j]);
            redisAssertWithInfo(c,keys[j],retval == DICT_OK);
        } else {
            l = dictGetVal(de);
        }
        // ���ͻ�����ӵ��������ͻ��˵�������
        listAddNodeTail(l,c);
    }
    blockClient(c,REDIS_BLOCKED_LIST);
}

/* Unblock a client that's waiting in a blocking operation such as BLPOP.
 * You should never call this function directly, but unblockClient() instead. */
void unblockClientWaitingData(redisClient *c) {
    dictEntry *de;
    dictIterator *di;
    list *l;

    redisAssertWithInfo(c,NULL,dictSize(c->bpop.keys) != 0);

    // �������� key �������Ǵӿͻ��� db->blocking_keys ���������Ƴ�
    di = dictGetIterator(c->bpop.keys);
    /* The client may wait for multiple keys, so unblock it for every key. */
    while((de = dictNext(di)) != NULL) {
        robj *key = dictGetKey(de);

        /* Remove this client from the list of clients waiting for this key. */
        // ��ȡ������Ϊ key ���������Ŀͻ��˵�����
        l = dictFetchValue(c->db->blocking_keys,key);

        redisAssertWithInfo(c,key,l != NULL);

        // ��ָ���ͻ��˴�������ɾ��
        listDelNode(l,listSearchKey(l,c));

        /* If the list is empty we need to remove it to avoid wasting memory */
        // ����Ѿ�û�������ͻ������������ key �ϣ���ôɾ���������
        if (listLength(l) == 0)
            dictDelete(c->db->blocking_keys,key);
    }
    dictReleaseIterator(di);

    /* Cleanup the client structure */
    // ��� bpop.keys ���ϣ��ֵ䣩
    dictEmpty(c->bpop.keys,NULL);
    if (c->bpop.target) {
        decrRefCount(c->bpop.target);
        c->bpop.target = NULL;
    }
}

/* If the specified key has clients blocked waiting for list pushes, this
 * function will put the key reference into the server.ready_keys list.
 * Note that db->ready_keys is a hash table that allows us to avoid putting
 * the same key again and again in the list in case of multiple pushes
 * made by a script or in the context of MULTI/EXEC.
 *
 * ����пͻ�������Ϊ�ȴ����� key �� push ��������
 * ��ô����� key �ķŽ� server.ready_keys �б����档
 *
 * ע�� db->ready_keys ��һ����ϣ��
 * ����Ա�����������߽ű��У���ͬһ�� key һ����һ����ӵ��б��������֡�
 *
 * The list will be finally processed by handleClientsBlockedOnLists() 
 *
 * ����б����ջᱻ handleClientsBlockedOnLists() ��������
 */
void signalListAsReady(redisClient *c, robj *key) {
    readyList *rl;

    /* No clients blocking for this key? No need to queue it. */
    // û�пͻ��˱������������ֱ�ӷ���
    if (dictFind(c->db->blocking_keys,key) == NULL) return;

    /* Key was already signaled? No need to queue it again. */
    // ������Ѿ�����ӵ� ready_keys ���ˣ�ֱ�ӷ���
    if (dictFind(c->db->ready_keys,key) != NULL) return;

    /* Ok, we need to queue this key into server.ready_keys. */
    // ����һ�� readyList �ṹ������������ݿ�
    // Ȼ�� readyList ��ӵ� server.ready_keys ��
    rl = zmalloc(sizeof(*rl));
    rl->key = key;
    rl->db = c->db;
    incrRefCount(key);
    listAddNodeTail(server.ready_keys,rl);

    /* We also add the key in the db->ready_keys dictionary in order
     * to avoid adding it multiple times into a list with a simple O(1)
     * check. 
     *
     * �� key ��ӵ� c->db->ready_keys �����У���ֹ�ظ����
     */
    incrRefCount(key);
    redisAssert(dictAdd(c->db->ready_keys,key,NULL) == DICT_OK);
}

/* This is a helper function for handleClientsBlockedOnLists(). It's work
 * is to serve a specific client (receiver) that is blocked on 'key'
 * in the context of the specified 'db', doing the following:
 * 
 * �����Ա������Ŀͻ��� receiver ����������� key �� key ���ڵ����ݿ� db
 * �Լ�һ��ֵ value ��һ��λ��ֵ where ִ�����¶�����
 *
 * 1) Provide the client with the 'value' element.
 *
 *    �� value �ṩ�� receiver
 *
 * 2) If the dstkey is not NULL (we are serving a BRPOPLPUSH) also push the
 *    'value' element on the destination list (the LPUSH side of the command).
 *
 *    ��� dstkey ��Ϊ�գ�BRPOPLPUSH���������
 *    ��ôҲ�� value ���뵽 dstkey ָ�����б��С�
 *
 * 3) Propagate the resulting BRPOP, BLPOP and additional LPUSH if any into
 *    the AOF and replication channel.
 *
 *    �� BRPOP �� BLPOP �Ϳ����е� LPUSH ������ AOF ��ͬ���ڵ�
 *
 * The argument 'where' is REDIS_TAIL or REDIS_HEAD, and indicates if the
 * 'value' element was popped fron the head (BLPOP) or tail (BRPOP) so that
 * we can propagate the command properly.
 * 
 * where ������ REDIS_TAIL ���� REDIS_HEAD ������ʶ��� value �Ǵ��Ǹ��ط� POP
 * �����������������������ͬ������ BLPOP ���� BRPOP ��

 * The function returns REDIS_OK if we are able to serve the client, otherwise
 * REDIS_ERR is returned to signal the caller that the list POP operation
 * should be undone as the client was not served: This only happens for
 * BRPOPLPUSH that fails to push the value to the destination key as it is
 * of the wrong type. 
 *
 * ���һ�гɹ������� REDIS_OK ��
 * ���ִ��ʧ�ܣ���ô���� REDIS_ERR ���� Redis ������Ŀ��ڵ�� POP ������
 * ʧ�ܵ����ֻ������� BRPOPLPUSH �����У�
 * ���� POP Դ�б�ɹ���ȴ���� PUSH ��Ŀ��������б�ʱ�������ͻ����ʧ�ܡ�
 */
int serveClientBlockedOnList(redisClient *receiver, robj *key, robj *dstkey, redisDb *db, robj *value, int where)
{
    robj *argv[3];

    // ִ�е��� BLPOP �� BRPOP
    if (dstkey == NULL) {
        /* Propagate the [LR]POP operation. */
        argv[0] = (where == REDIS_HEAD) ? shared.lpop :
                                          shared.rpop;
        argv[1] = key;
        propagate((where == REDIS_HEAD) ?
            server.lpopCommand : server.rpopCommand,
            db->id,argv,2,REDIS_PROPAGATE_AOF|REDIS_PROPAGATE_REPL);

        /* BRPOP/BLPOP */
        addReplyMultiBulkLen(receiver,2);
        addReplyBulk(receiver,key);
        addReplyBulk(receiver,value);

    // ִ�е��� BRPOPLPUSH 
    } else {
        /* BRPOPLPUSH */

        // ȡ��Ŀ�����
        robj *dstobj =
            lookupKeyWrite(receiver->db,dstkey);
        if (!(dstobj &&
             checkType(receiver,dstobj,REDIS_LIST)))
        {
            /* Propagate the RPOP operation. */
            // ���� RPOP ����
            argv[0] = shared.rpop;
            argv[1] = key;
            propagate(server.rpopCommand,
                db->id,argv,2,
                REDIS_PROPAGATE_AOF|
                REDIS_PROPAGATE_REPL);

            // ��ֵ���뵽 dstobj �У���� dstobj �����ڣ�
            // ��ô�´���һ��
            rpoplpushHandlePush(receiver,dstkey,dstobj,
                value);

            /* Propagate the LPUSH operation. */
            // ���� LPUSH ����
            argv[0] = shared.lpush;
            argv[1] = dstkey;
            argv[2] = value;
            propagate(server.lpushCommand,
                db->id,argv,3,
                REDIS_PROPAGATE_AOF|
                REDIS_PROPAGATE_REPL);
        } else {
            /* BRPOPLPUSH failed because of wrong
             * destination type. */
            return REDIS_ERR;
        }
    }
    return REDIS_OK;
}

/* This function should be called by Redis every time a single command,
 * a MULTI/EXEC block, or a Lua script, terminated its execution after
 * being called by a client.
 *
 * ����������� Redis ÿ��ִ���굥����������� Lua �ű�֮����á�
 *
 * All the keys with at least one client blocked that received at least
 * one new element via some PUSH operation are accumulated into
 * the server.ready_keys list. This function will run the list and will
 * serve clients accordingly. Note that the function will iterate again and
 * again as a result of serving BRPOPLPUSH we can have new blocking clients
 * to serve because of the PUSH side of BRPOPLPUSH. 
 *
 * �����б�������ĳ���ͻ��˵� key ��˵��ֻҪ��� key ��ִ����ĳ�� PUSH ����
 * ��ô��� key �ͻᱻ�ŵ� serve.ready_keys ȥ��
 * 
 * ���������������� serve.ready_keys ����
 * ��������� key ��Ԫ�ص������������ͻ��ˣ�
 * �Ӷ�����ͻ��˵�����״̬��
 *
 * ������һ����һ�εؽ��е�����
 * �������ִ�� BRPOPLPUSH ����������Ҳ����������ȡ����ȷ���±������ͻ��ˡ�
 */
void handleClientsBlockedOnLists(void) {

    // �������� ready_keys ����
    while(listLength(server.ready_keys) != 0) {
        list *l;

        /* Point server.ready_keys to a fresh list and save the current one
         * locally. This way as we run the old list we are free to call
         * signalListAsReady() that may push new elements in server.ready_keys
         * when handling clients blocked into BRPOPLPUSH. */
        // ���ݾɵ� ready_keys ���ٸ��������˸�ֵһ���µ�
        l = server.ready_keys;
        server.ready_keys = listCreate();

        while(listLength(l) != 0) {

            // ȡ�� ready_keys �е��׸�����ڵ�
            listNode *ln = listFirst(l);

            // ָ�� readyList �ṹ
            readyList *rl = ln->value;

            /* First of all remove this key from db->ready_keys so that
             * we can safely call signalListAsReady() against this key. */
            // �� ready_keys ���Ƴ������� key
            dictDelete(rl->db->ready_keys,rl->key);

            /* If the key exists and it's a list, serve blocked clients
             * with data. */
            // ��ȡ�������������Ӧ���Ƿǿյģ��������б�
            robj *o = lookupKeyWrite(rl->db,rl->key);
            if (o != NULL && o->type == REDIS_LIST) {
                dictEntry *de;

                /* We serve clients in the same order they blocked for
                 * this key, from the first blocked to the last. */
                // ȡ�����б���� key �����Ŀͻ���
                de = dictFind(rl->db->blocking_keys,rl->key);
                if (de) {
                    list *clients = dictGetVal(de);
                    int numclients = listLength(clients);

                    while(numclients--) {
                        // ȡ���ͻ���
                        listNode *clientnode = listFirst(clients);
                        redisClient *receiver = clientnode->value;

                        // ���õ�����Ŀ�����ֻ�� BRPOPLPUSH ʱʹ�ã�
                        robj *dstkey = receiver->bpop.target;

                        // ���б��е���Ԫ��
                        // ������λ��ȡ������ִ�� BLPOP ���� BRPOP ���� BRPOPLPUSH
                        int where = (receiver->lastcmd &&
                                     receiver->lastcmd->proc == blpopCommand) ?
                                    REDIS_HEAD : REDIS_TAIL;
                        robj *value = listTypePop(o,where);

                        // ����Ԫ�ؿɵ������� NULL��
                        if (value) {
                            /* Protect receiver->bpop.target, that will be
                             * freed by the next unblockClient()
                             * call. */
                            if (dstkey) incrRefCount(dstkey);

                            // ȡ���ͻ��˵�����״̬
                            unblockClient(receiver);

                            // ��ֵ value ���뵽��ɿͻ��� receiver ������ key ��
                            if (serveClientBlockedOnList(receiver,
                                rl->key,dstkey,rl->db,value,
                                where) == REDIS_ERR)
                            {
                                /* If we failed serving the client we need
                                 * to also undo the POP operation. */
                                    listTypePush(o,value,where);
                            }

                            if (dstkey) decrRefCount(dstkey);
                            decrRefCount(value);
                        } else {
                            // ���ִ�е������ʾ��������һ���ͻ��˱���������
                            // ��Щ�ͻ���Ҫ�ȴ��Լ����´� PUSH
                            break;
                        }
                    }
                }
                
                // ����б�Ԫ���Ѿ�Ϊ�գ���ô�����ݿ��н���ɾ��
                if (listTypeLength(o) == 0) dbDelete(rl->db,rl->key);
                /* We don't call signalModifiedKey() as it was already called
                 * when an element was pushed on the list. */
            }

            /* Free this item. */
            decrRefCount(rl->key);
            zfree(rl);
            listDelNode(l,ln);
        }
        listRelease(l); /* We have the new list on place at this point. */
    }
}

/* Blocking RPOP/LPOP */
void blockingPopGenericCommand(redisClient *c, int where) {
    robj *o;
    mstime_t timeout;
    int j;

    // ȡ�� timeout ����
    if (getTimeoutFromObjectOrReply(c,c->argv[c->argc-1],&timeout,UNIT_SECONDS)
        != REDIS_OK) return;

    // ���������б��
    for (j = 1; j < c->argc-1; j++) {

        // ȡ���б��
        o = lookupKeyWrite(c->db,c->argv[j]);

        // �зǿ��б�
        if (o != NULL) {
            if (o->type != REDIS_LIST) {
                addReply(c,shared.wrongtypeerr);
                return;
            } else {
                // �ǿ��б�
                if (listTypeLength(o) != 0) {
                    /* Non empty list, this is like a non normal [LR]POP. */
                    char *event = (where == REDIS_HEAD) ? "lpop" : "rpop";

                    // ����ֵ
                    robj *value = listTypePop(o,where);

                    redisAssert(value != NULL);

                    // �ظ��ͻ���
                    addReplyMultiBulkLen(c,2);
                    // �ظ�����Ԫ�ص��б�
                    addReplyBulk(c,c->argv[j]);
                    // �ظ�����ֵ
                    addReplyBulk(c,value);

                    decrRefCount(value);

                    notifyKeyspaceEvent(REDIS_NOTIFY_LIST,event,
                                        c->argv[j],c->db->id);

                    // ɾ�����б�
                    if (listTypeLength(o) == 0) {
                        dbDelete(c->db,c->argv[j]);
                        notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",
                                            c->argv[j],c->db->id);
                    }

                    signalModifiedKey(c->db,c->argv[j]);

                    server.dirty++;

                    /* Replicate it as an [LR]POP instead of B[LR]POP. */
                    // ����һ�� [LR]POP ������ B[LR]POP
                    rewriteClientCommandVector(c,2,
                        (where == REDIS_HEAD) ? shared.lpop : shared.rpop,
                        c->argv[j]);
                    return;
                }
            }
        }
    }

    /* If we are inside a MULTI/EXEC and the list is empty the only thing
     * we can do is treating it as a timeout (even with timeout 0). */
    // ���������һ��������ִ�У���ôΪ�˲��������ȴ�
    // ������ֻ����ͻ��˷���һ���ջظ�
    if (c->flags & REDIS_MULTI) {
        addReply(c,shared.nullmultibulk);
        return;
    }

    /* If the list is empty or the key does not exists we must block */
    // ���������б���������ڣ�ֻ��������
    blockForKeys(c, c->argv + 1, c->argc - 2, timeout, NULL);
}

void blpopCommand(redisClient *c) {
    blockingPopGenericCommand(c,REDIS_HEAD);
}

void brpopCommand(redisClient *c) {
    blockingPopGenericCommand(c,REDIS_TAIL);
}

void brpoplpushCommand(redisClient *c) {
    mstime_t timeout;

    // ȡ�� timeout ����
    if (getTimeoutFromObjectOrReply(c,c->argv[3],&timeout,UNIT_SECONDS)
        != REDIS_OK) return;

    // ȡ���б��
    robj *key = lookupKeyWrite(c->db, c->argv[1]);

    // ��Ϊ�գ�����
    if (key == NULL) {
        if (c->flags & REDIS_MULTI) {
            /* Blocking against an empty list in a multi state
             * returns immediately. */
            addReply(c, shared.nullbulk);
        } else {
            /* The list is empty and the client blocks. */
            blockForKeys(c, c->argv + 1, 1, timeout, c->argv[2]);
        }

    // ���ǿգ�ִ�� RPOPLPUSH
    } else {
        if (key->type != REDIS_LIST) {
            addReply(c, shared.wrongtypeerr);
        } else {
            /* The list exists and has elements, so
             * the regular rpoplpushCommand is executed. */
            redisAssertWithInfo(c,key,listTypeLength(key) > 0);

            rpoplpushCommand(c);
        }
    }
}
