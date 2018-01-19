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
 * Set Commands
 *----------------------------------------------------------------------------*/

void sunionDiffGenericCommand(redisClient *c, robj **setkeys, int setnum, robj *dstkey, int op);

/* Factory method to return a set that *can* hold "value". 
 *
 * ����һ�����Ա���ֵ value �ļ��ϡ�
 *
 * When the object has an integer-encodable value, 
 * an intset will be returned. Otherwise a regular hash table. 
 *
 * �������ֵ���Ա�����Ϊ����ʱ������ intset ��
 * ���򣬷�����ͨ�Ĺ�ϣ��
 */
robj *setTypeCreate(robj *value) {

    if (isObjectRepresentableAsLongLong(value,NULL) == REDIS_OK)
        return createIntsetObject();

    return createSetObject();
}

/*
 * ��̬ add ����
 *
 * ��ӳɹ����� 1 �����Ԫ���Ѿ����ڣ����� 0 ��
 */
int setTypeAdd(robj *subject, robj *value) {
    long long llval;

    // �ֵ�
    if (subject->encoding == REDIS_ENCODING_HT) {
        // �� value ��Ϊ���� NULL ��Ϊֵ����Ԫ����ӵ��ֵ���
        if (dictAdd(subject->ptr,value,NULL) == DICT_OK) {
            incrRefCount(value);
            return 1;
        }

    // intset
    } else if (subject->encoding == REDIS_ENCODING_INTSET) {
        
        // ��������ֵ���Ա���Ϊ�����Ļ�����ô�������ֵ��ӵ� intset ��
        if (isObjectRepresentableAsLongLong(value,&llval) == REDIS_OK) {
            uint8_t success = 0;
            subject->ptr = intsetAdd(subject->ptr,llval,&success);
            if (success) {
                /* Convert to regular set when the intset contains
                 * too many entries. */
                // ��ӳɹ�
                // ��鼯���������Ԫ��֮���Ƿ���Ҫת��Ϊ�ֵ�
                if (intsetLen(subject->ptr) > server.set_max_intset_entries)
                    setTypeConvert(subject,REDIS_ENCODING_HT);
                return 1;
            }

        // ��������ֵ���ܱ���Ϊ��������ô�����ϴ� intset ����ת��Ϊ HT ����
        // Ȼ����ִ����Ӳ���
        } else {
            /* Failed to get integer from object, convert to regular set. */
            setTypeConvert(subject,REDIS_ENCODING_HT);

            /* The set *was* an intset and this value is not integer
             * encodable, so dictAdd should always work. */
            redisAssertWithInfo(NULL,value,dictAdd(subject->ptr,value,NULL) == DICT_OK);
            incrRefCount(value);
            return 1;
        }

    // δ֪����
    } else {
        redisPanic("Unknown set encoding");
    }

    // ���ʧ�ܣ�Ԫ���Ѿ�����
    return 0;
}

/*
 * ��̬ remove ����
 *
 * ɾ���ɹ����� 1 ����ΪԪ�ز����ڶ�����ɾ��ʧ�ܷ��� 0 ��
 */
int setTypeRemove(robj *setobj, robj *value) {
    long long llval;

    // HT
    if (setobj->encoding == REDIS_ENCODING_HT) {
        // ���ֵ���ɾ����������Ԫ�أ�
        if (dictDelete(setobj->ptr,value) == DICT_OK) {
            // ���Ƿ��б�Ҫ��ɾ��֮����С�ֵ�Ĵ�С
            if (htNeedsResize(setobj->ptr)) dictResize(setobj->ptr);
            return 1;
        }

    // INTSET
    } else if (setobj->encoding == REDIS_ENCODING_INTSET) {
        // ��������ֵ���Ա���Ϊ�����Ļ�����ô���Դ� intset ���Ƴ�Ԫ��
        if (isObjectRepresentableAsLongLong(value,&llval) == REDIS_OK) {
            int success;
            setobj->ptr = intsetRemove(setobj->ptr,llval,&success);
            if (success) return 1;
        }

    // δ֪����
    } else {
        redisPanic("Unknown set encoding");
    }

    // ɾ��ʧ��
    return 0;
}

/*
 * ��̬ ismember ����
 */
int setTypeIsMember(robj *subject, robj *value) {
    long long llval;

    // HT
    if (subject->encoding == REDIS_ENCODING_HT) {
        return dictFind((dict*)subject->ptr,value) != NULL;

    // INTSET
    } else if (subject->encoding == REDIS_ENCODING_INTSET) {
        if (isObjectRepresentableAsLongLong(value,&llval) == REDIS_OK) {
            return intsetFind((intset*)subject->ptr,llval);
        }

    // δ֪����
    } else {
        redisPanic("Unknown set encoding");
    }

    // ����ʧ��
    return 0;
}

/*
 * ����������һ����̬���ϵ�����
 *
 * setTypeIterator ������ redis.h
 */
setTypeIterator *setTypeInitIterator(robj *subject) {

    setTypeIterator *si = zmalloc(sizeof(setTypeIterator));
    
    // ָ�򱻵����Ķ���
    si->subject = subject;

    // ��¼����ı���
    si->encoding = subject->encoding;

    // HT
    if (si->encoding == REDIS_ENCODING_HT) {
        // �ֵ������
        si->di = dictGetIterator(subject->ptr);

    // INTSET
    } else if (si->encoding == REDIS_ENCODING_INTSET) {
        // ����
        si->ii = 0;

    // δ֪����
    } else {
        redisPanic("Unknown set encoding");
    }

    // ���ص�����
    return si;
}

/*
 * �ͷŵ�����
 */
void setTypeReleaseIterator(setTypeIterator *si) {

    if (si->encoding == REDIS_ENCODING_HT)
        dictReleaseIterator(si->di);

    zfree(si);
}

/* Move to the next entry in the set. Returns the object at the current
 * position.
 *
 * ȡ����������ָ��ĵ�ǰ����Ԫ�ء�
 *
 * Since set elements can be internally be stored as redis objects or
 * simple arrays of integers, setTypeNext returns the encoding of the
 * set object you are iterating, and will populate the appropriate pointer
 * (eobj) or (llobj) accordingly.
 *
 * ��Ϊ���ϼ����Ա���Ϊ intset ��Ҳ���Ա���Ϊ��ϣ��
 * ���Գ������ݼ��ϵı��룬ѡ��ֵ���浽�Ǹ������
 *
 *  - ������Ϊ intset ʱ��Ԫ�ر�ָ�� llobj ����
 *
 *  - ������Ϊ��ϣ��ʱ��Ԫ�ر�ָ�� eobj ����
 *
 * ���Һ����᷵�ر��������ϵı��룬����ʶ��
 *
 * When there are no longer elements -1 is returned.
 *
 * �������е�Ԫ��ȫ�����������ʱ���������� -1 ��
 *
 * Returned objects ref count is not incremented, so this function is
 * copy on write friendly. 
 *
 * ��Ϊ�����صĶ�����û�б��������ü����ģ�
 * ������������Ƕ� copy-on-write �Ѻõġ�
 */
int setTypeNext(setTypeIterator *si, robj **objele, int64_t *llele) {

    // ���ֵ���ȡ������
    if (si->encoding == REDIS_ENCODING_HT) {

        // ���µ�����
        dictEntry *de = dictNext(si->di);

        // �ֵ��ѵ�����
        if (de == NULL) return -1;

        // ���ؽڵ�ļ������ϵ�Ԫ�أ�
        *objele = dictGetKey(de);

    // �� intset ��ȡ��Ԫ��
    } else if (si->encoding == REDIS_ENCODING_INTSET) {
        if (!intsetGet(si->subject->ptr,si->ii++,llele))
            return -1;
    }

    // ���ر���
    return si->encoding;
}

/* The not copy on write friendly version but easy to use version
 * of setTypeNext() is setTypeNextObject(), returning new objects
 * or incrementing the ref count of returned objects. So if you don't
 * retain a pointer to this object you should call decrRefCount() against it.
 *
 * setTypeNext �ķ� copy-on-write �Ѻð汾��
 * ���Ƿ���һ���µġ������Ѿ����ӹ����ü����Ķ���
 *
 * ��������ʹ�������֮��Ӧ�öԶ������ decrRefCount() ��
 *
 * This function is the way to go for write operations where COW is not
 * an issue as the result will be anyway of incrementing the ref count. 
 *
 * �������Ӧ���ڷ� copy-on-write ʱ���á�
 */
robj *setTypeNextObject(setTypeIterator *si) {
    int64_t intele;
    robj *objele;
    int encoding;

    // ȡ��Ԫ��
    encoding = setTypeNext(si,&objele,&intele);
    // ����ΪԪ�ش�������
    switch(encoding) {
        // ��Ϊ��
        case -1:    return NULL;
        // INTSET ����һ������ֵ����ҪΪ���ֵ��������
        case REDIS_ENCODING_INTSET:
            return createStringObjectFromLongLong(intele);
        // HT �����Ѿ����ض����ˣ�ֻ��ִ�� incrRefCount()
        case REDIS_ENCODING_HT:
            incrRefCount(objele);
            return objele;
        default:
            redisPanic("Unsupported encoding");
    }

    return NULL; /* just to suppress warnings */
}

/* Return random element from a non empty set.
 *
 * �ӷǿռ��������ȡ��һ��Ԫ�ء�
 *
 * The returned element can be a int64_t value if the set is encoded
 * as an "intset" blob of integers, or a redis object if the set
 * is a regular set.
 *
 * ������ϵı���Ϊ intset ����ô��Ԫ��ָ�� int64_t ָ�� llele ��
 * ������ϵı���Ϊ HT ����ô��Ԫ�ض���ָ�����ָ�� objele ��
 *
 * The caller provides both pointers to be populated with the right
 * object. The return value of the function is the object->encoding
 * field of the object and is used by the caller to check if the
 * int64_t pointer or the redis object pointer was populated.
 *
 * �����ķ���ֵΪ���ϵı��뷽ʽ��ͨ���������ֵ����֪���Ǹ�ָ�뱣����Ԫ�ص�ֵ��
 *
 * When an object is returned (the set was a real set) the ref count
 * of the object is not incremented so this function can be considered
 * copy on write friendly. 
 *
 * ��Ϊ�����صĶ�����û�б��������ü����ģ�
 * ������������Ƕ� copy-on-write �Ѻõġ�
 */
int setTypeRandomElement(robj *setobj, robj **objele, int64_t *llele) {

    if (setobj->encoding == REDIS_ENCODING_HT) {
        dictEntry *de = dictGetRandomKey(setobj->ptr);
        *objele = dictGetKey(de);

    } else if (setobj->encoding == REDIS_ENCODING_INTSET) {
        *llele = intsetRandom(setobj->ptr);

    } else {
        redisPanic("Unknown set encoding");
    }

    return setobj->encoding;
}

/*
 * ���϶�̬ size ����
 */
unsigned long setTypeSize(robj *subject) {

    if (subject->encoding == REDIS_ENCODING_HT) {
        return dictSize((dict*)subject->ptr);

    } else if (subject->encoding == REDIS_ENCODING_INTSET) {
        return intsetLen((intset*)subject->ptr);

    } else {
        redisPanic("Unknown set encoding");
    }
}

/* Convert the set to specified encoding. 
 *
 * �����϶��� setobj �ı���ת��Ϊ REDIS_ENCODING_HT ��
 *
 * The resulting dict (when converting to a hash table)
 * is presized to hold the number of elements in the original set.
 *
 * �´����Ľ���ֵ�ᱻԤ�ȷ���Ϊ��ԭ���ļ���һ����
 */
void setTypeConvert(robj *setobj, int enc) {

    setTypeIterator *si;

    // ȷ�����ͺͱ�����ȷ
    redisAssertWithInfo(NULL,setobj,setobj->type == REDIS_SET &&
                             setobj->encoding == REDIS_ENCODING_INTSET);

    if (enc == REDIS_ENCODING_HT) {
        int64_t intele;
        // �������ֵ�
        dict *d = dictCreate(&setDictType,NULL);
        robj *element;

        /* Presize the dict to avoid rehashing */
        // Ԥ����չ�ռ�
        dictExpand(d,intsetLen(setobj->ptr));

        /* To add the elements we extract integers and create redis objects */
        // �������ϣ�����Ԫ����ӵ��ֵ���
        si = setTypeInitIterator(setobj);
        while (setTypeNext(si,NULL,&intele) != -1) {
            element = createStringObjectFromLongLong(intele);
            redisAssertWithInfo(NULL,element,dictAdd(d,element,NULL) == DICT_OK);
        }
        setTypeReleaseIterator(si);

        // ���¼��ϵı���
        setobj->encoding = REDIS_ENCODING_HT;
        zfree(setobj->ptr);
        // ���¼��ϵ�ֵ����
        setobj->ptr = d;
    } else {
        redisPanic("Unsupported set conversion");
    }
}

void saddCommand(redisClient *c) {
    robj *set;
    int j, added = 0;

    // ȡ�����϶���
    set = lookupKeyWrite(c->db,c->argv[1]);

    // ���󲻴��ڣ�����һ���µģ����������������ݿ�
    if (set == NULL) {
        set = setTypeCreate(c->argv[2]);
        dbAdd(c->db,c->argv[1],set);

    // ������ڣ��������
    } else {
        if (set->type != REDIS_SET) {
            addReply(c,shared.wrongtypeerr);
            return;
        }
    }

    // ����������Ԫ����ӵ�������
    for (j = 2; j < c->argc; j++) {
        c->argv[j] = tryObjectEncoding(c->argv[j]);
        // ֻ��Ԫ��δ�����ڼ���ʱ������һ�γɹ����
        if (setTypeAdd(set,c->argv[j])) added++;
    }

    // ���������һ��Ԫ�ر��ɹ���ӣ���ôִ�����³���
    if (added) {
        // ���ͼ��޸��ź�
        signalModifiedKey(c->db,c->argv[1]);
        // �����¼�֪ͨ
        notifyKeyspaceEvent(REDIS_NOTIFY_SET,"sadd",c->argv[1],c->db->id);
    }

    // �����ݿ���Ϊ��
    server.dirty += added;

    // �������Ԫ�ص�����
    addReplyLongLong(c,added);
}

void sremCommand(redisClient *c) {
    robj *set;
    int j, deleted = 0, keyremoved = 0;

    // ȡ�����϶���
    if ((set = lookupKeyWriteOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,set,REDIS_SET)) return;

    // ɾ�����������Ԫ��
    for (j = 2; j < c->argc; j++) {
        
        // ֻ��Ԫ���ڼ�����ʱ������һ�γɹ�ɾ��
        if (setTypeRemove(set,c->argv[j])) {
            deleted++;
            // ��������Ѿ�Ϊ�գ���ôɾ�����϶���
            if (setTypeSize(set) == 0) {
                dbDelete(c->db,c->argv[1]);
                keyremoved = 1;
                break;
            }
        }
    }

    // ���������һ��Ԫ�ر��ɹ�ɾ������ôִ�����³���
    if (deleted) {
        // ���ͼ��޸��ź�
        signalModifiedKey(c->db,c->argv[1]);
        // �����¼�֪ͨ
        notifyKeyspaceEvent(REDIS_NOTIFY_SET,"srem",c->argv[1],c->db->id);
        // �����¼�֪ͨ
        if (keyremoved)
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1],
                                c->db->id);
        // �����ݿ���Ϊ��
        server.dirty += deleted;
    }
    
    // �����ɹ�ɾ��Ԫ�ص�������Ϊ�ظ�
    addReplyLongLong(c,deleted);
}

void smoveCommand(redisClient *c) {
    robj *srcset, *dstset, *ele;

    // ȡ��Դ����
    srcset = lookupKeyWrite(c->db,c->argv[1]);
    // ȡ��Ŀ�꼯��
    dstset = lookupKeyWrite(c->db,c->argv[2]);

    // ����Ԫ��
    ele = c->argv[3] = tryObjectEncoding(c->argv[3]);

    /* If the source key does not exist return 0 */
    // Դ���ϲ����ڣ�ֱ�ӷ���
    if (srcset == NULL) {
        addReply(c,shared.czero);
        return;
    }

    /* If the source key has the wrong type, or the destination key
     * is set and has the wrong type, return with an error. */
    // ���Դ���ϵ����ʹ���
    // ����Ŀ�꼯�ϴ��ڡ��������ʹ���
    // ��ôֱ�ӷ���
    if (checkType(c,srcset,REDIS_SET) ||
        (dstset && checkType(c,dstset,REDIS_SET))) return;

    /* If srcset and dstset are equal, SMOVE is a no-op */
    // ���Դ���Ϻ�Ŀ�꼯����ȣ���ôֱ�ӷ���
    if (srcset == dstset) {
        addReply(c,shared.cone);
        return;
    }

    /* If the element cannot be removed from the src set, return 0. */
    // ��Դ�������Ƴ�Ŀ��Ԫ��
    // ���Ŀ��Ԫ�ز�������Դ�����У���ôֱ�ӷ���
    if (!setTypeRemove(srcset,ele)) {
        addReply(c,shared.czero);
        return;
    }

    // �����¼�֪ͨ
    notifyKeyspaceEvent(REDIS_NOTIFY_SET,"srem",c->argv[1],c->db->id);

    /* Remove the src set from the database when empty */
    // ���Դ�����Ѿ�Ϊ�գ���ô���������ݿ���ɾ��
    if (setTypeSize(srcset) == 0) {
        // ɾ�����϶���
        dbDelete(c->db,c->argv[1]);
        // �����¼�֪ͨ
        notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1],c->db->id);
    }

    // ���ͼ��޸��ź�
    signalModifiedKey(c->db,c->argv[1]);
    signalModifiedKey(c->db,c->argv[2]);

    // �����ݿ���Ϊ��
    server.dirty++;

    /* Create the destination set when it doesn't exist */
    // ���Ŀ�꼯�ϲ����ڣ���ô������
    if (!dstset) {
        dstset = setTypeCreate(ele);
        dbAdd(c->db,c->argv[2],dstset);
    }

    /* An extra key has changed when ele was successfully added to dstset */
    // ��Ԫ����ӵ�Ŀ�꼯��
    if (setTypeAdd(dstset,ele)) {
        // �����ݿ���Ϊ��
        server.dirty++;
        // �����¼�֪ͨ
        notifyKeyspaceEvent(REDIS_NOTIFY_SET,"sadd",c->argv[2],c->db->id);
    }

    // �ظ� 1 
    addReply(c,shared.cone);
}

void sismemberCommand(redisClient *c) {
    robj *set;

    // ȡ�����϶���
    if ((set = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,set,REDIS_SET)) return;

    // ��������Ԫ��
    c->argv[2] = tryObjectEncoding(c->argv[2]);

    // ����Ƿ����
    if (setTypeIsMember(set,c->argv[2]))
        addReply(c,shared.cone);
    else
        addReply(c,shared.czero);
}

void scardCommand(redisClient *c) {
    robj *o;

    // ȡ������
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,REDIS_SET)) return;

    // ���ؼ��ϵĻ���
    addReplyLongLong(c,setTypeSize(o));
}

void spopCommand(redisClient *c) {
    robj *set, *ele, *aux;
    int64_t llele;
    int encoding;

    // ȡ������
    if ((set = lookupKeyWriteOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,set,REDIS_SET)) return;

    // �Ӽ��������ȡ��һ��Ԫ��
    encoding = setTypeRandomElement(set,&ele,&llele);

    // ����ȡ��Ԫ�شӼ�����ɾ��
    if (encoding == REDIS_ENCODING_INTSET) {
        ele = createStringObjectFromLongLong(llele);
        set->ptr = intsetRemove(set->ptr,llele,NULL);
    } else {
        incrRefCount(ele);
        setTypeRemove(set,ele);
    }

    // �����¼�֪ͨ
    notifyKeyspaceEvent(REDIS_NOTIFY_SET,"spop",c->argv[1],c->db->id);

    /* Replicate/AOF this command as an SREM operation */
    // ���������� SREM ����������ֹ�����к�������ԣ��������ݲ�һ��
    // ����ͬ�ķ��������ɾ����ͬ��Ԫ�أ�
    aux = createStringObject("SREM",4);
    rewriteClientCommandVector(c,3,aux,c->argv[1],ele);
    decrRefCount(ele);
    decrRefCount(aux);

    // ���ػظ�
    addReplyBulk(c,ele);

    // ��������Ѿ�Ϊ�գ���ô�����ݿ���ɾ����
    if (setTypeSize(set) == 0) {
        // ɾ������
        dbDelete(c->db,c->argv[1]);
        // �����¼�֪ͨ
        notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1],c->db->id);
    }

    // ���ͼ��޸��ź�
    signalModifiedKey(c->db,c->argv[1]);

    // �����ݿ���Ϊ��
    server.dirty++;
}

/* handle the "SRANDMEMBER key <count>" variant. The normal version of the
 * command is handled by the srandmemberCommand() function itself. 
 *
 * ʵ�� SRANDMEMBER key <count> ���֣�
 * ԭ���� SRANDMEMBER key �� srandmemberCommand() ʵ��
 */

/* How many times bigger should be the set compared to the requested size
 * for us to don't use the "remove elements" strategy? Read later in the
 * implementation for more info. 
 *
 * ��� count ������������������õĻ����ȼ��ϵĻ���Ҫ��
 * ��ô����Ͳ�ʹ�á�ɾ��Ԫ�ء��Ĳ��ԡ�
 *
 * ������Ϣ��ο�����ĺ������塣
 */
#define SRANDMEMBER_SUB_STRATEGY_MUL 3

void srandmemberWithCountCommand(redisClient *c) {
    long l;
    unsigned long count, size;

    // Ĭ���ڼ����в������ظ�Ԫ��
    int uniq = 1;

    robj *set, *ele;
    int64_t llele;
    int encoding;

    dict *d;

    // ȡ�� l ����
    if (getLongFromObjectOrReply(c,c->argv[2],&l,NULL) != REDIS_OK) return;
    if (l >= 0) {
        // l Ϊ��������ʾ����Ԫ�ظ�����ͬ
        count = (unsigned) l;
    } else {
        /* A negative count means: return the same elements multiple times
         * (i.e. don't remove the extracted element after every extraction). */
        // ��� l Ϊ��������ô��ʾ���صĽ���п������ظ�Ԫ��
        count = -l;
        uniq = 0;
    }

    // ȡ�����϶���
    if ((set = lookupKeyReadOrReply(c,c->argv[1],shared.emptymultibulk))
        == NULL || checkType(c,set,REDIS_SET)) return;
    // ȡ�����ϻ���
    size = setTypeSize(set);

    /* If count is zero, serve it ASAP to avoid special cases later. */
    // count Ϊ 0 ��ֱ�ӷ���
    if (count == 0) {
        addReply(c,shared.emptymultibulk);
        return;
    }

    /* CASE 1: The count was negative, so the extraction method is just:
     * "return N random elements" sampling the whole set every time.
     * This case is trivial and can be served without auxiliary data
     * structures. 
     *
     * ���� 1��count Ϊ��������������Դ����ظ�Ԫ��
     * ֱ�ӴӼ�����ȡ�������� N �����Ԫ�ؾͿ�����
     *
     * �������β���Ҫ����Ľṹ����������
     */
    if (!uniq) {
        addReplyMultiBulkLen(c,count);

        while(count--) {
            // ȡ�����Ԫ��
            encoding = setTypeRandomElement(set,&ele,&llele);
            if (encoding == REDIS_ENCODING_INTSET) {
                addReplyBulkLongLong(c,llele);
            } else {
                addReplyBulk(c,ele);
            }
        }

        return;
    }

    /* CASE 2:
     * The number of requested elements is greater than the number of
     * elements inside the set: simply return the whole set. 
     *
     * ��� count �ȼ��ϵĻ���Ҫ����ôֱ�ӷ�����������
     */
    if (count >= size) {
        sunionDiffGenericCommand(c,c->argv+1,1,NULL,REDIS_OP_UNION);
        return;
    }

    /* For CASE 3 and CASE 4 we need an auxiliary dictionary. */
    // �������� 3 ������ 4 ����Ҫһ��������ֵ�
    d = dictCreate(&setDictType,NULL);

    /* CASE 3:
     * 
     * ���� 3��
     *
     * The number of elements inside the set is not greater than
     * SRANDMEMBER_SUB_STRATEGY_MUL times the number of requested elements.
     *
     * count �������� SRANDMEMBER_SUB_STRATEGY_MUL �Ļ��ȼ��ϵĻ���Ҫ��
     *
     * In this case we create a set from scratch with all the elements, and
     * subtract random elements to reach the requested number of elements.
     *
     * ����������£����򴴽�һ�����ϵĸ�����
     * ���Ӽ�����ɾ��Ԫ�أ�ֱ�����ϵĻ������� count ����ָ��������Ϊֹ��
     *
     * This is done because if the number of requsted elements is just
     * a bit less than the number of elements in the set, the natural approach
     * used into CASE 3 is highly inefficient. 
     *
     * ʹ������������ԭ���ǣ��� count �������ӽ��ڼ��ϵĻ���ʱ��
     * �Ӽ��������ȡ�� count �������ķ����Ƿǳ���Ч�ġ�
     */
    if (count*SRANDMEMBER_SUB_STRATEGY_MUL > size) {
        setTypeIterator *si;

        /* Add all the elements into the temporary dictionary. */
        // �������ϣ�������Ԫ����ӵ���ʱ�ֵ���
        si = setTypeInitIterator(set);
        while((encoding = setTypeNext(si,&ele,&llele)) != -1) {
            int retval = DICT_ERR;

            // ΪԪ�ش������󣬲���ӵ��ֵ���
            if (encoding == REDIS_ENCODING_INTSET) {
                retval = dictAdd(d,createStringObjectFromLongLong(llele),NULL);
            } else {
                retval = dictAdd(d,dupStringObject(ele),NULL);
            }
            redisAssert(retval == DICT_OK);
            /* ����Ĵ�������ع�Ϊ
            
            robj *elem_obj;
            if (encoding == REDIS_ENCODING_INTSET) {
                elem_obj = createStringObjectFromLongLong(...)
            } else if () {
                ...
            } else if () {
                ...
            }

            redisAssert(dictAdd(d, elem_obj) == DICT_OK)

            */
        }
        setTypeReleaseIterator(si);
        redisAssert(dictSize(d) == size);

        /* Remove random elements to reach the right count. */
        // ���ɾ��Ԫ�أ�ֱ�����ϻ������� count ������ֵ
        while(size > count) {
            dictEntry *de;

            // ȡ����ɾ�����Ԫ��
            de = dictGetRandomKey(d);
            dictDelete(d,dictGetKey(de));

            size--;
        }
    }
    
    /* CASE 4: We have a big set compared to the requested number of elements.
     *
     * ���� 4 �� count ����Ҫ�ȼ��ϻ���С�ܶࡣ
     *
     * In this case we can simply get random elements from the set and add
     * to the temporary set, trying to eventually get enough unique elements
     * to reach the specified count. 
     *
     * ����������£����ǿ���ֱ�ӴӼ����������ȡ��Ԫ�أ�
     * ��������ӵ���������У�ֱ��������Ļ������� count Ϊֹ��
     */
    else {
        unsigned long added = 0;

        while(added < count) {

            // ����ش�Ŀ�꼯����ȡ��Ԫ��
            encoding = setTypeRandomElement(set,&ele,&llele);

            // ��Ԫ��ת��Ϊ����
            if (encoding == REDIS_ENCODING_INTSET) {
                ele = createStringObjectFromLongLong(llele);
            } else {
                ele = dupStringObject(ele);
            }

            /* Try to add the object to the dictionary. If it already exists
             * free it, otherwise increment the number of objects we have
             * in the result dictionary. */
            // ���Խ�Ԫ����ӵ��ֵ���
            // dictAdd ֻ����Ԫ�ز��������ֵ�ʱ���Ż᷵�� 1
            // ������������Ѿ���ͬ����Ԫ�أ���ô�����ִ�� else ����
            // ֻ��Ԫ�ز������ڽ����ʱ����ӲŻ�ɹ�
            if (dictAdd(d,ele,NULL) == DICT_OK)
                added++;
            else
                decrRefCount(ele);
        }
    }

    /* CASE 3 & 4: send the result to the user. */
    {
        // ���� 3 �� 4 ����������ظ����ͻ���
        dictIterator *di;
        dictEntry *de;

        addReplyMultiBulkLen(c,count);
        // ���������Ԫ��
        di = dictGetIterator(d);
        while((de = dictNext(di)) != NULL)
            // �ظ�
            addReplyBulk(c,dictGetKey(de));
        dictReleaseIterator(di);
        dictRelease(d);
    }
}

void srandmemberCommand(redisClient *c) {
    robj *set, *ele;
    int64_t llele;
    int encoding;

    // ������� count ��������ô���� srandmemberWithCountCommand ������
    if (c->argc == 3) {
        srandmemberWithCountCommand(c);
        return;

    // ��������
    } else if (c->argc > 3) {
        addReply(c,shared.syntaxerr);
        return;
    }

    // ���ȡ������Ԫ�ؾͿ�����

    // ȡ�����϶���
    if ((set = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,set,REDIS_SET)) return;

    // ���ȡ��һ��Ԫ��
    encoding = setTypeRandomElement(set,&ele,&llele);
    // �ظ�
    if (encoding == REDIS_ENCODING_INTSET) {
        addReplyBulkLongLong(c,llele);
    } else {
        addReplyBulk(c,ele);
    }
}

/*
 * ���㼯�� s1 �Ļ�����ȥ���� s2 �Ļ���֮��
 */
int qsortCompareSetsByCardinality(const void *s1, const void *s2) {
    return setTypeSize(*(robj**)s1)-setTypeSize(*(robj**)s2);
}

/* This is used by SDIFF and in this case we can receive NULL that should
 * be handled as empty sets. 
 *
 * ���㼯�� s2 �Ļ�����ȥ���� s1 �Ļ���֮��
 */
int qsortCompareSetsByRevCardinality(const void *s1, const void *s2) {
    robj *o1 = *(robj**)s1, *o2 = *(robj**)s2;

    return  (o2 ? setTypeSize(o2) : 0) - (o1 ? setTypeSize(o1) : 0);
}

void sinterGenericCommand(redisClient *c, robj **setkeys, unsigned long setnum, robj *dstkey) {

    // ��������
    robj **sets = zmalloc(sizeof(robj*)*setnum);

    setTypeIterator *si;
    robj *eleobj, *dstset = NULL;
    int64_t intobj;
    void *replylen = NULL;
    unsigned long j, cardinality = 0;
    int encoding;

    for (j = 0; j < setnum; j++) {

        // ȡ������
        // ��һ��ִ��ʱ��ȡ������ dest ����
        // ֮��ִ��ʱ��ȡ���Ķ��� source ����
        robj *setobj = dstkey ?
            lookupKeyWrite(c->db,setkeys[j]) :
            lookupKeyRead(c->db,setkeys[j]);

        // ���󲻴��ڣ�����ִ�У���������
        if (!setobj) {
            zfree(sets);
            if (dstkey) {
                if (dbDelete(c->db,dstkey)) {
                    signalModifiedKey(c->db,dstkey);
                    server.dirty++;
                }
                addReply(c,shared.czero);
            } else {
                addReply(c,shared.emptymultibulk);
            }
            return;
        }
        
        // �����������
        if (checkType(c,setobj,REDIS_SET)) {
            zfree(sets);
            return;
        }

        // ������ָ��ָ�򼯺϶���
        sets[j] = setobj;
    }

    /* Sort sets from the smallest to largest, this will improve our
     * algorithm's performance */
    // �������Լ��Ͻ����������������㷨��Ч��
    qsort(sets,setnum,sizeof(robj*),qsortCompareSetsByCardinality);

    /* The first thing we should output is the total number of elements...
     * since this is a multi-bulk write, but at this stage we don't know
     * the intersection set size, so we use a trick, append an empty object
     * to the output list and save the pointer to later modify it with the
     * right length */
    // ��Ϊ��֪����������ж��ٸ�Ԫ�أ�����û�а취ֱ�����ûظ�������
    // ����ʹ����һ��С���ɣ�ֱ��ʹ��һ�� BUFF �б�
    // Ȼ��֮��Ļظ�����ӵ��б���
    if (!dstkey) {
        replylen = addDeferredMultiBulkLength(c);
    } else {
        /* If we have a target key where to store the resulting set
         * create this key with an empty set inside */
        dstset = createIntsetObject();
    }

    /* Iterate all the elements of the first (smallest) set, and test
     * the element against all the other sets, if at least one set does
     * not include the element it is discarded */
    // ����������С�ĵ�һ������
    // ��������Ԫ�غ������������Ͻ��жԱ�
    // ���������һ�����ϲ��������Ԫ�أ���ô���Ԫ�ز����ڽ���
    si = setTypeInitIterator(sets[0]);
    while((encoding = setTypeNext(si,&eleobj,&intobj)) != -1) {
        // �����������ϣ����Ԫ���Ƿ�����Щ�����д���
        for (j = 1; j < setnum; j++) {

            // ������һ�����ϣ���Ϊ���ǽ��������ʼֵ
            if (sets[j] == sets[0]) continue;

            // Ԫ�صı���Ϊ INTSET 
            // �����������в�����������Ƿ����
            if (encoding == REDIS_ENCODING_INTSET) {
                /* intset with intset is simple... and fast */
                if (sets[j]->encoding == REDIS_ENCODING_INTSET &&
                    !intsetFind((intset*)sets[j]->ptr,intobj))
                {
                    break;
                /* in order to compare an integer with an object we
                 * have to use the generic function, creating an object
                 * for this */
                } else if (sets[j]->encoding == REDIS_ENCODING_HT) {
                    eleobj = createStringObjectFromLongLong(intobj);
                    if (!setTypeIsMember(sets[j],eleobj)) {
                        decrRefCount(eleobj);
                        break;
                    }
                    decrRefCount(eleobj);
                }

            // Ԫ�صı���Ϊ �ֵ�
            // �����������в�����������Ƿ����
            } else if (encoding == REDIS_ENCODING_HT) {
                /* Optimization... if the source object is integer
                 * encoded AND the target set is an intset, we can get
                 * a much faster path. */
                if (eleobj->encoding == REDIS_ENCODING_INT &&
                    sets[j]->encoding == REDIS_ENCODING_INTSET &&
                    !intsetFind((intset*)sets[j]->ptr,(long)eleobj->ptr))
                {
                    break;
                /* else... object to object check is easy as we use the
                 * type agnostic API here. */
                } else if (!setTypeIsMember(sets[j],eleobj)) {
                    break;
                }
            }
        }

        /* Only take action when all sets contain the member */
        // ������м��϶�����Ŀ��Ԫ�صĻ�����ôִ�����´���
        if (j == setnum) {

            // SINTER ���ֱ�ӷ��ؽ����Ԫ��
            if (!dstkey) {
                if (encoding == REDIS_ENCODING_HT)
                    addReplyBulk(c,eleobj);
                else
                    addReplyBulkLongLong(c,intobj);
                cardinality++;

            // SINTERSTORE ����������ӵ��������
            } else {
                if (encoding == REDIS_ENCODING_INTSET) {
                    eleobj = createStringObjectFromLongLong(intobj);
                    setTypeAdd(dstset,eleobj);
                    decrRefCount(eleobj);
                } else {
                    setTypeAdd(dstset,eleobj);
                }
            }
        }
    }
    setTypeReleaseIterator(si);

    // SINTERSTORE �������������������ݿ�
    if (dstkey) {
        /* Store the resulting set into the target, if the intersection
         * is not an empty set. */
        // ɾ�����ڿ����е� dstkey
        int deleted = dbDelete(c->db,dstkey);

        // ���������ǿգ���ô�������������ݿ���
        if (setTypeSize(dstset) > 0) {
            dbAdd(c->db,dstkey,dstset);
            addReplyLongLong(c,setTypeSize(dstset));
            notifyKeyspaceEvent(REDIS_NOTIFY_SET,"sinterstore",
                dstkey,c->db->id);
        } else {
            decrRefCount(dstset);
            addReply(c,shared.czero);
            if (deleted)
                notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",
                    dstkey,c->db->id);
        }

        signalModifiedKey(c->db,dstkey);

        server.dirty++;

    // SINTER ����ظ�������Ļ���
    } else {
        setDeferredMultiBulkLength(c,replylen,cardinality);
    }

    zfree(sets);
}

void sinterCommand(redisClient *c) {
    sinterGenericCommand(c,c->argv+1,c->argc-1,NULL);
}

void sinterstoreCommand(redisClient *c) {
    sinterGenericCommand(c,c->argv+2,c->argc-2,c->argv[1]);
}

/*
 * ���������
 */
#define REDIS_OP_UNION 0
#define REDIS_OP_DIFF 1
#define REDIS_OP_INTER 2

void sunionDiffGenericCommand(redisClient *c, robj **setkeys, int setnum, robj *dstkey, int op) {

    // ��������
    robj **sets = zmalloc(sizeof(robj*)*setnum);

    setTypeIterator *si;
    robj *ele, *dstset = NULL;
    int j, cardinality = 0;
    int diff_algo = 1;

    // ȡ�����м��϶��󣬲���ӵ�����������
    for (j = 0; j < setnum; j++) {
        robj *setobj = dstkey ?
            lookupKeyWrite(c->db,setkeys[j]) :
            lookupKeyRead(c->db,setkeys[j]);

        // �����ڵļ��ϵ��� NULL ������
        if (!setobj) {
            sets[j] = NULL;
            continue;
        }

        // �ж����Ǽ��ϣ�ִֹͣ�У���������
        if (checkType(c,setobj,REDIS_SET)) {
            zfree(sets);
            return;
        }

        // ��¼����
        sets[j] = setobj;
    }

    /* Select what DIFF algorithm to use.
     *
     * ѡ��ʹ���Ǹ��㷨��ִ�м���
     *
     * Algorithm 1 is O(N*M) where N is the size of the element first set
     * and M the total number of sets.
     *
     * �㷨 1 �ĸ��Ӷ�Ϊ O(N*M) ������ N Ϊ��һ�����ϵĻ�����
     * �� M ��Ϊ�������ϵ�������
     *
     * Algorithm 2 is O(N) where N is the total number of elements in all
     * the sets.
     *
     * �㷨 2 �ĸ��Ӷ�Ϊ O(N) ������ N Ϊ���м����е�Ԫ������������
     *
     * We compute what is the best bet with the current input here. 
     *
     * ����ͨ����������������ʹ���Ǹ��㷨
     */
    if (op == REDIS_OP_DIFF && sets[0]) {
        long long algo_one_work = 0, algo_two_work = 0;

        // �������м���
        for (j = 0; j < setnum; j++) {
            if (sets[j] == NULL) continue;

            // ���� setnum ���� sets[0] �Ļ���֮��
            algo_one_work += setTypeSize(sets[0]);
            // �������м��ϵĻ���֮��
            algo_two_work += setTypeSize(sets[j]);
        }

        /* Algorithm 1 has better constant times and performs less operations
         * if there are elements in common. Give it some advantage. */
        // �㷨 1 �ĳ����Ƚϵͣ����ȿ����㷨 1
        algo_one_work /= 2;
        diff_algo = (algo_one_work <= algo_two_work) ? 1 : 2;

        if (diff_algo == 1 && setnum > 1) {
            /* With algorithm 1 it is better to order the sets to subtract
             * by decreasing size, so that we are more likely to find
             * duplicated elements ASAP. */
            // ���ʹ�õ����㷨 1 ����ô��ö� sets[0] ������������Ͻ�������
            // �����������Ż��㷨������
            qsort(sets+1,setnum-1,sizeof(robj*),
                qsortCompareSetsByRevCardinality);
        }
    }

    /* We need a temp set object to store our union. If the dstkey
     * is not NULL (that is, we are inside an SUNIONSTORE operation) then
     * this set object will be the resulting object to set into the target key
     *
     * ʹ��һ����ʱ�����������������������ִ�е��� SUNIONSTORE ���
     * ��ô�����������Ϊ�����ļ���ֵ����
     */
    dstset = createIntsetObject();

    // ִ�е��ǲ�������
    if (op == REDIS_OP_UNION) {
        /* Union is trivial, just add every element of every set to the
         * temporary set. */
        // �������м��ϣ���Ԫ����ӵ��������Ϳ�����
        for (j = 0; j < setnum; j++) {
            if (!sets[j]) continue; /* non existing keys are like empty sets */

            si = setTypeInitIterator(sets[j]);
            while((ele = setTypeNextObject(si)) != NULL) {
                // setTypeAdd ֻ�ڼ��ϲ�����ʱ���ŻὫԪ����ӵ����ϣ������� 1 
                if (setTypeAdd(dstset,ele)) cardinality++;
                decrRefCount(ele);
            }
            setTypeReleaseIterator(si);
        }

    // ִ�е��ǲ���㣬����ʹ���㷨 1
    } else if (op == REDIS_OP_DIFF && sets[0] && diff_algo == 1) {
        /* DIFF Algorithm 1:
         *
         * ��㷨 1 ��
         *
         * We perform the diff by iterating all the elements of the first set,
         * and only adding it to the target set if the element does not exist
         * into all the other sets.
         *
         * ������� sets[0] �����е�����Ԫ�أ�
         * �������Ԫ�غ��������ϵ�����Ԫ�ؽ��жԱȣ�
         * ֻ�����Ԫ�ز��������������м���ʱ��
         * �Ž����Ԫ����ӵ��������
         *
         * This way we perform at max N*M operations, where N is the size of
         * the first set, and M the number of sets. 
         *
         * ����㷨ִ����� N*M ���� N �ǵ�һ�����ϵĻ�����
         * �� M ���������ϵ�������
         */
        si = setTypeInitIterator(sets[0]);
        while((ele = setTypeNextObject(si)) != NULL) {

            // ���Ԫ�������������Ƿ����
            for (j = 1; j < setnum; j++) {
                if (!sets[j]) continue; /* no key is an empty set. */
                if (sets[j] == sets[0]) break; /* same set! */
                if (setTypeIsMember(sets[j],ele)) break;
            }

            // ֻ��Ԫ�����������������ж�������ʱ���Ž�����ӵ��������
            if (j == setnum) {
                /* There is no other set with this element. Add it. */
                setTypeAdd(dstset,ele);
                cardinality++;
            }

            decrRefCount(ele);
        }
        setTypeReleaseIterator(si);

    // ִ�е��ǲ���㣬����ʹ���㷨 2
    } else if (op == REDIS_OP_DIFF && sets[0] && diff_algo == 2) {
        /* DIFF Algorithm 2:
         *
         * ��㷨 2 ��
         *
         * Add all the elements of the first set to the auxiliary set.
         * Then remove all the elements of all the next sets from it.
         *
         * �� sets[0] ������Ԫ�ض���ӵ�������У�
         * Ȼ������������м��ϣ�����ͬ��Ԫ�شӽ������ɾ����
         *
         * This is O(N) where N is the sum of all the elements in every set. 
         *
         * �㷨���Ӷ�Ϊ O(N) ��N Ϊ���м��ϵĻ���֮�͡�
         */
        for (j = 0; j < setnum; j++) {
            if (!sets[j]) continue; /* non existing keys are like empty sets */

            si = setTypeInitIterator(sets[j]);
            while((ele = setTypeNextObject(si)) != NULL) {
                // sets[0] ʱ��������Ԫ����ӵ�����
                if (j == 0) {
                    if (setTypeAdd(dstset,ele)) cardinality++;
                // ���� sets[0] ʱ�������м��ϴӽ�������Ƴ�
                } else {
                    if (setTypeRemove(dstset,ele)) cardinality--;
                }
                decrRefCount(ele);
            }
            setTypeReleaseIterator(si);

            /* Exit if result set is empty as any additional removal
             * of elements will have no effect. */
            if (cardinality == 0) break;
        }
    }

    /* Output the content of the resulting set, if not in STORE mode */
    // ִ�е��� SDIFF ���� SUNION
    // ��ӡ������е�����Ԫ��
    if (!dstkey) {
        addReplyMultiBulkLen(c,cardinality);

        // �������ظ�������е�Ԫ��
        si = setTypeInitIterator(dstset);
        while((ele = setTypeNextObject(si)) != NULL) {
            addReplyBulk(c,ele);
            decrRefCount(ele);
        }
        setTypeReleaseIterator(si);

        decrRefCount(dstset);

    // ִ�е��� SDIFFSTORE ���� SUNIONSTORE
    } else {
        /* If we have a target key where to store the resulting set
         * create this key with the result set inside */
        // ��ɾ�����ڿ����е� dstkey
        int deleted = dbDelete(c->db,dstkey);

        // ����������Ϊ�գ��������������ݿ���
        if (setTypeSize(dstset) > 0) {
            dbAdd(c->db,dstkey,dstset);
            // ���ؽ�����Ļ���
            addReplyLongLong(c,setTypeSize(dstset));
            notifyKeyspaceEvent(REDIS_NOTIFY_SET,
                op == REDIS_OP_UNION ? "sunionstore" : "sdiffstore",
                dstkey,c->db->id);

        // �����Ϊ��
        } else {
            decrRefCount(dstset);
            // ���� 0 
            addReply(c,shared.czero);
            if (deleted)
                notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",
                    dstkey,c->db->id);
        }

        signalModifiedKey(c->db,dstkey);

        server.dirty++;
    }

    zfree(sets);
}

void sunionCommand(redisClient *c) {
    sunionDiffGenericCommand(c,c->argv+1,c->argc-1,NULL,REDIS_OP_UNION);
}

void sunionstoreCommand(redisClient *c) {
    sunionDiffGenericCommand(c,c->argv+2,c->argc-2,c->argv[1],REDIS_OP_UNION);
}

void sdiffCommand(redisClient *c) {
    sunionDiffGenericCommand(c,c->argv+1,c->argc-1,NULL,REDIS_OP_DIFF);
}

void sdiffstoreCommand(redisClient *c) {
    sunionDiffGenericCommand(c,c->argv+2,c->argc-2,c->argv[1],REDIS_OP_DIFF);
}

void sscanCommand(redisClient *c) {
    robj *set;
    unsigned long cursor;

    if (parseScanCursorOrReply(c,c->argv[2],&cursor) == REDIS_ERR) return;
    if ((set = lookupKeyReadOrReply(c,c->argv[1],shared.emptyscan)) == NULL ||
        checkType(c,set,REDIS_SET)) return;
    scanGenericCommand(c,set,cursor);
}
