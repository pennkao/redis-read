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
#include <math.h>

/*-----------------------------------------------------------------------------
 * Hash type API
 *----------------------------------------------------------------------------*/

/* Check the length of a number of objects to see if we need to convert a
 * ziplist to a real hash. 
 *
 * �� argv �����еĶ��������м�飬
 * ���Ƿ���Ҫ������ı���� REDIS_ENCODING_ZIPLIST ת���� REDIS_ENCODING_HT
 *
 * Note that we only check string encoded objects
 * as their string length can be queried in constant time. 
 *
 * ע�����ֻ����ַ���ֵ����Ϊ���ǵĳ��ȿ����ڳ���ʱ����ȡ�á�
 */
void hashTypeTryConversion(robj *o, robj **argv, int start, int end) {
    int i;

    // ��������� ziplist ���룬��ôֱ�ӷ���
    if (o->encoding != REDIS_ENCODING_ZIPLIST) return;

    // �������������󣬿����ǵ��ַ���ֵ�Ƿ񳬹���ָ������
    for (i = start; i <= end; i++) {
        if (sdsEncodedObject(argv[i]) &&
            sdslen(argv[i]->ptr) > server.hash_max_ziplist_value)
        {
            // ������ı���ת���� REDIS_ENCODING_HT
            hashTypeConvert(o, REDIS_ENCODING_HT);
            break;
        }
    }
}

/* Encode given objects in-place when the hash uses a dict. 
 *
 * �� subject �ı���Ϊ REDIS_ENCODING_HT ʱ��
 * ���ԶԶ��� o1 �� o2 ���б��룬
 * �Խ�ʡ�����ڴ档
 */
void hashTypeTryObjectEncoding(robj *subject, robj **o1, robj **o2) {
    if (subject->encoding == REDIS_ENCODING_HT) {
        if (o1) *o1 = tryObjectEncoding(*o1);
        if (o2) *o2 = tryObjectEncoding(*o2);
    }
}

/* Get the value from a ziplist encoded hash, identified by field.
 * Returns -1 when the field cannot be found. 
 *
 * �� ziplist ����� hash ��ȡ���� field ���Ӧ��ֵ��
 *
 * ������
 *  field   ��
 *  vstr    ֵ���ַ���ʱ���������浽���ָ��
 *  vlen    �����ַ����ĳ���
 *  ll      ֵ������ʱ���������浽���ָ��
 *
 * ����ʧ��ʱ���������� -1 ��
 * ���ҳɹ�ʱ������ 0 ��
 */
int hashTypeGetFromZiplist(robj *o, robj *field,
                           unsigned char **vstr,
                           unsigned int *vlen,
                           long long *vll)
{
    unsigned char *zl, *fptr = NULL, *vptr = NULL;
    int ret;

    // ȷ��������ȷ
    redisAssert(o->encoding == REDIS_ENCODING_ZIPLIST);

    // ȡ��δ�������
    field = getDecodedObject(field);

    // ���� ziplist ���������λ��
    zl = o->ptr;
    fptr = ziplistIndex(zl, ZIPLIST_HEAD);
    if (fptr != NULL) {
        // ��λ������Ľڵ�
        fptr = ziplistFind(fptr, field->ptr, sdslen(field->ptr), 1);
        if (fptr != NULL) {
            /* Grab pointer to the value (fptr points to the field) */
            // ���Ѿ��ҵ���ȡ���������Ӧ��ֵ��λ��
            vptr = ziplistNext(zl, fptr);
            redisAssert(vptr != NULL);
        }
    }

    decrRefCount(field);

    // �� ziplist �ڵ���ȡ��ֵ
    if (vptr != NULL) {
        ret = ziplistGet(vptr, vstr, vlen, vll);
        redisAssert(ret);
        return 0;
    }

    // û�ҵ�
    return -1;
}

/* Get the value from a hash table encoded hash, identified by field.
 * Returns -1 when the field cannot be found. 
 *
 * �� REDIS_ENCODING_HT ����� hash ��ȡ���� field ���Ӧ��ֵ��
 *
 * �ɹ��ҵ�ֵʱ���� 0 ��û�ҵ����� -1 ��
 */
int hashTypeGetFromHashTable(robj *o, robj *field, robj **value) {
    dictEntry *de;

    // ȷ��������ȷ
    redisAssert(o->encoding == REDIS_ENCODING_HT);

    // ���ֵ��в����򣨼���
    de = dictFind(o->ptr, field);

    // ��������
    if (de == NULL) return -1;

    // ȡ���򣨼�����ֵ
    *value = dictGetVal(de);

    // �ɹ��ҵ�
    return 0;
}

/* Higher level function of hashTypeGet*() that always returns a Redis
 * object (either new or with refcount incremented), so that the caller
 * can retain a reference or call decrRefCount after the usage.
 *
 * The lower level function can prevent copy on write so it is
 * the preferred way of doing read operations. */
/*
 * ��̬ GET �������� hash ��ȡ���� field ��ֵ��������һ��ֵ����
 *
 * �ҵ�����ֵ����û�ҵ����� NULL ��
 */
robj *hashTypeGetObject(robj *o, robj *field) {
    robj *value = NULL;

    // �� ziplist ��ȡ��ֵ
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        if (hashTypeGetFromZiplist(o, field, &vstr, &vlen, &vll) == 0) {
            // ����ֵ����
            if (vstr) {
                value = createStringObject((char*)vstr, vlen);
            } else {
                value = createStringObjectFromLongLong(vll);
            }
        }

    // ���ֵ���ȡ��ֵ
    } else if (o->encoding == REDIS_ENCODING_HT) {
        robj *aux;

        if (hashTypeGetFromHashTable(o, field, &aux) == 0) {
            incrRefCount(aux);
            value = aux;
        }

    } else {
        redisPanic("Unknown hash encoding");
    }

    // ����ֵ���󣬻��� NULL
    return value;
}

/* Test if the specified field exists in the given hash. Returns 1 if the field
 * exists, and 0 when it doesn't. 
 *
 * �������� feild �Ƿ������ hash ���� o �С�
 *
 * ���ڷ��� 1 �������ڷ��� 0 ��
 */
int hashTypeExists(robj *o, robj *field) {

    // ��� ziplist
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        if (hashTypeGetFromZiplist(o, field, &vstr, &vlen, &vll) == 0) return 1;

    // ����ֵ�
    } else if (o->encoding == REDIS_ENCODING_HT) {
        robj *aux;

        if (hashTypeGetFromHashTable(o, field, &aux) == 0) return 1;

    // δ֪����
    } else {
        redisPanic("Unknown hash encoding");
    }

    // ������
    return 0;
}

/* Add an element, discard the old if the key already exists.
 * Return 0 on insert and 1 on update.
 *
 * �������� field-value ����ӵ� hash �У�
 * ��� field �Ѿ����ڣ���ôɾ���ɵ�ֵ����������ֵ��
 *
 * This function will take care of incrementing the reference count of the
 * retained fields and value objects. 
 *
 * ������������ field �� value �����������ü���������
 *
 * ���� 0 ��ʾԪ���Ѿ����ڣ���κ�������ִ�е��Ǹ��²�����
 *
 * ���� 1 ���ʾ����ִ�е�������Ӳ�����
 */
int hashTypeSet(robj *o, robj *field, robj *value) {
    int update = 0;

    // ��ӵ� ziplist
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl, *fptr, *vptr;

        // ������ַ�����������
        field = getDecodedObject(field);
        value = getDecodedObject(value);

        // �������� ziplist �����Բ��Ҳ����� field ��������Ѿ����ڵĻ���
        zl = o->ptr;
        fptr = ziplistIndex(zl, ZIPLIST_HEAD);
        if (fptr != NULL) {
            // ��λ���� field
            fptr = ziplistFind(fptr, field->ptr, sdslen(field->ptr), 1);
            if (fptr != NULL) {
                /* Grab pointer to the value (fptr points to the field) */
                // ��λ�����ֵ
                vptr = ziplistNext(zl, fptr);
                redisAssert(vptr != NULL);

                // ��ʶ��β���Ϊ���²���
                update = 1;

                /* Delete value */
                // ɾ���ɵļ�ֵ��
                zl = ziplistDelete(zl, &vptr);

                /* Insert new value */
                // ����µļ�ֵ��
                zl = ziplistInsert(zl, vptr, value->ptr, sdslen(value->ptr));
            }
        }

        // ����ⲻ�Ǹ��²�������ô�����һ����Ӳ���
        if (!update) {
            /* Push new field/value pair onto the tail of the ziplist */
            // ���µ� field-value �����뵽 ziplist ��ĩβ
            zl = ziplistPush(zl, field->ptr, sdslen(field->ptr), ZIPLIST_TAIL);
            zl = ziplistPush(zl, value->ptr, sdslen(value->ptr), ZIPLIST_TAIL);
        }
        
        // ���¶���ָ��
        o->ptr = zl;

        // �ͷ���ʱ����
        decrRefCount(field);
        decrRefCount(value);

        /* Check if the ziplist needs to be converted to a hash table */
        // �������Ӳ������֮���Ƿ���Ҫ�� ZIPLIST ����ת���� HT ����
        if (hashTypeLength(o) > server.hash_max_ziplist_entries)
            hashTypeConvert(o, REDIS_ENCODING_HT);

    // ��ӵ��ֵ�
    } else if (o->encoding == REDIS_ENCODING_HT) {

        // ��ӻ��滻��ֵ�Ե��ֵ�
        // ��ӷ��� 1 ���滻���� 0
        if (dictReplace(o->ptr, field, value)) { /* Insert */
            incrRefCount(field);
        } else { /* Update */
            update = 1;
        }

        incrRefCount(value);
    } else {
        redisPanic("Unknown hash encoding");
    }

    // ����/���ָʾ����
    return update;
}

/* Delete an element from a hash.
 *
 * ������ field ���� value �ӹ�ϣ����ɾ��
 *
 * Return 1 on deleted and 0 on not found. 
 *
 * ɾ���ɹ����� 1 ����Ϊ�򲻴��ڶ���ɵ�ɾ��ʧ�ܷ��� 0 ��
 */
int hashTypeDelete(robj *o, robj *field) {
    int deleted = 0;

    // �� ziplist ��ɾ��
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl, *fptr;

        field = getDecodedObject(field);

        zl = o->ptr;
        fptr = ziplistIndex(zl, ZIPLIST_HEAD);
        if (fptr != NULL) {
            // ��λ����
            fptr = ziplistFind(fptr, field->ptr, sdslen(field->ptr), 1);
            if (fptr != NULL) {
                // ɾ�����ֵ
                zl = ziplistDelete(zl,&fptr);
                zl = ziplistDelete(zl,&fptr);
                o->ptr = zl;
                deleted = 1;
            }
        }

        decrRefCount(field);

    // ���ֵ���ɾ��
    } else if (o->encoding == REDIS_ENCODING_HT) {
        if (dictDelete((dict*)o->ptr, field) == REDIS_OK) {
            deleted = 1;

            /* Always check if the dictionary needs a resize after a delete. */
            // ɾ���ɹ�ʱ�����ֵ��Ƿ���Ҫ����
            if (htNeedsResize(o->ptr)) dictResize(o->ptr);
        }
    } else {
        redisPanic("Unknown hash encoding");
    }

    return deleted;
}

/* Return the number of elements in a hash. 
 *
 * ���ع�ϣ��� field-value ������
 */
unsigned long hashTypeLength(robj *o) {
    unsigned long length = ULONG_MAX;

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        // ziplist �У�ÿ�� field-value �Զ���Ҫʹ�������ڵ�������
        length = ziplistLen(o->ptr) / 2;
    } else if (o->encoding == REDIS_ENCODING_HT) {
        length = dictSize((dict*)o->ptr);
    } else {
        redisPanic("Unknown hash encoding");
    }

    return length;
}

/*
 * ����һ����ϣ���͵ĵ�����
 * hashTypeIterator ���Ͷ����� redis.h
 *
 * ���Ӷȣ�O(1)
 *
 * ����ֵ��
 *  hashTypeIterator
 */
hashTypeIterator *hashTypeInitIterator(robj *subject) {

    hashTypeIterator *hi = zmalloc(sizeof(hashTypeIterator));

    // ָ�����
    hi->subject = subject;

    // ��¼����
    hi->encoding = subject->encoding;

    // �� ziplist �ķ�ʽ��ʼ��������
    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        hi->fptr = NULL;
        hi->vptr = NULL;

    // ���ֵ�ķ�ʽ��ʼ��������
    } else if (hi->encoding == REDIS_ENCODING_HT) {
        hi->di = dictGetIterator(subject->ptr);

    } else {
        redisPanic("Unknown hash encoding");
    }

    // ���ص�����
    return hi;
}

/*
 * �ͷŵ�����
 */
void hashTypeReleaseIterator(hashTypeIterator *hi) {

    // �ͷ��ֵ������
    if (hi->encoding == REDIS_ENCODING_HT) {
        dictReleaseIterator(hi->di);
    }

    // �ͷ� ziplist ������
    zfree(hi);
}

/* Move to the next entry in the hash. 
 *
 * ��ȡ��ϣ�е���һ���ڵ㣬���������浽��������
 *
 * could be found and REDIS_ERR when the iterator reaches the end. 
 *
 * �����ȡ�ɹ������� REDIS_OK ��
 *
 * ����Ѿ�û��Ԫ�ؿɻ�ȡ��Ϊ�գ����ߵ�����ϣ�����ô���� REDIS_ERR ��
 */
int hashTypeNext(hashTypeIterator *hi) {

    // ���� ziplist
    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl;
        unsigned char *fptr, *vptr;

        zl = hi->subject->ptr;
        fptr = hi->fptr;
        vptr = hi->vptr;

        // ��һ��ִ��ʱ����ʼ��ָ��
        if (fptr == NULL) {
            /* Initialize cursor */
            redisAssert(vptr == NULL);
            fptr = ziplistIndex(zl, 0);

        // ��ȡ��һ�������ڵ�
        } else {
            /* Advance cursor */
            redisAssert(vptr != NULL);
            fptr = ziplistNext(zl, vptr);
        }

        // ������ϣ����� ziplist Ϊ��
        if (fptr == NULL) return REDIS_ERR;

        /* Grab pointer to the value (fptr points to the field) */
        // ��¼ֵ��ָ��
        vptr = ziplistNext(zl, fptr);
        redisAssert(vptr != NULL);

        /* fptr, vptr now point to the first or next pair */
        // ���µ�����ָ��
        hi->fptr = fptr;
        hi->vptr = vptr;

    // �����ֵ�
    } else if (hi->encoding == REDIS_ENCODING_HT) {
        if ((hi->de = dictNext(hi->di)) == NULL) return REDIS_ERR;

    // δ֪����
    } else {
        redisPanic("Unknown hash encoding");
    }

    // �����ɹ�
    return REDIS_OK;
}

/* Get the field or value at iterator cursor, for an iterator on a hash value
 * encoded as a ziplist. Prototype is similar to `hashTypeGetFromZiplist`. 
 *
 * �� ziplist ����Ĺ�ϣ�У�ȡ��������ָ�뵱ǰָ��ڵ�����ֵ��
 */
void hashTypeCurrentFromZiplist(hashTypeIterator *hi, int what,
                                unsigned char **vstr,
                                unsigned int *vlen,
                                long long *vll)
{
    int ret;

    // ȷ��������ȷ
    redisAssert(hi->encoding == REDIS_ENCODING_ZIPLIST);

    // ȡ����
    if (what & REDIS_HASH_KEY) {
        ret = ziplistGet(hi->fptr, vstr, vlen, vll);
        redisAssert(ret);

    // ȡ��ֵ
    } else {
        ret = ziplistGet(hi->vptr, vstr, vlen, vll);
        redisAssert(ret);
    }
}

/* Get the field or value at iterator cursor, for an iterator on a hash value
 * encoded as a ziplist. Prototype is similar to `hashTypeGetFromHashTable`. 
 *
 * ���ݵ�������ָ�룬���ֵ����Ĺ�ϣ��ȡ����ָ��ڵ�� field ���� value ��
 */
void hashTypeCurrentFromHashTable(hashTypeIterator *hi, int what, robj **dst) {
    redisAssert(hi->encoding == REDIS_ENCODING_HT);

    // ȡ����
    if (what & REDIS_HASH_KEY) {
        *dst = dictGetKey(hi->de);

    // ȡ��ֵ
    } else {
        *dst = dictGetVal(hi->de);
    }
}

/* A non copy-on-write friendly but higher level version of hashTypeCurrent*()
 * that returns an object with incremented refcount (or a new object). 
 *
 * һ���� copy-on-write �Ѻã����ǲ�θ��ߵ� hashTypeCurrent() ������
 * �����������һ�����������ü����Ķ��󣬻���һ���¶���
 *
 * It is up to the caller to decrRefCount() the object if no reference is
 * retained. 
 *
 * ��ʹ���귵�ض���֮�󣬵�������Ҫ�Զ���ִ�� decrRefCount() ��
 */
robj *hashTypeCurrentObject(hashTypeIterator *hi, int what) {
    robj *dst;

    // ziplist
    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        // ȡ������ֵ
        hashTypeCurrentFromZiplist(hi, what, &vstr, &vlen, &vll);

        // ��������ֵ�Ķ���
        if (vstr) {
            dst = createStringObject((char*)vstr, vlen);
        } else {
            dst = createStringObjectFromLongLong(vll);
        }

    // �ֵ�
    } else if (hi->encoding == REDIS_ENCODING_HT) {
        // ȡ��������ֵ
        hashTypeCurrentFromHashTable(hi, what, &dst);
        // �Զ�������ü�����������
        incrRefCount(dst);

    // δ֪����
    } else {
        redisPanic("Unknown hash encoding");
    }

    // ���ض���
    return dst;
}

/*
 * �� key �����ݿ��в��Ҳ�������Ӧ�Ĺ�ϣ����
 * ������󲻴��ڣ���ô����һ���¹�ϣ���󲢷��ء�
 */
robj *hashTypeLookupWriteOrCreate(redisClient *c, robj *key) {

    robj *o = lookupKeyWrite(c->db,key);

    // ���󲻴��ڣ������µ�
    if (o == NULL) {
        o = createHashObject();
        dbAdd(c->db,key,o);

    // ������ڣ��������
    } else {
        if (o->type != REDIS_HASH) {
            addReply(c,shared.wrongtypeerr);
            return NULL;
        }
    }

    // ���ض���
    return o;
}

/*
 * ��һ�� ziplist ����Ĺ�ϣ���� o ת������������
 */
void hashTypeConvertZiplist(robj *o, int enc) {
    redisAssert(o->encoding == REDIS_ENCODING_ZIPLIST);

    // ��������� ZIPLIST ����ô��������
    if (enc == REDIS_ENCODING_ZIPLIST) {
        /* Nothing to do... */

    // ת���� HT ����
    } else if (enc == REDIS_ENCODING_HT) {

        hashTypeIterator *hi;
        dict *dict;
        int ret;

        // ������ϣ������
        hi = hashTypeInitIterator(o);

        // �����հ׵����ֵ�
        dict = dictCreate(&hashDictType, NULL);

        // �������� ziplist
        while (hashTypeNext(hi) != REDIS_ERR) {
            robj *field, *value;

            // ȡ�� ziplist ��ļ�
            field = hashTypeCurrentObject(hi, REDIS_HASH_KEY);
            field = tryObjectEncoding(field);

            // ȡ�� ziplist ���ֵ
            value = hashTypeCurrentObject(hi, REDIS_HASH_VALUE);
            value = tryObjectEncoding(value);

            // ����ֵ����ӵ��ֵ�
            ret = dictAdd(dict, field, value);
            if (ret != DICT_OK) {
                redisLogHexDump(REDIS_WARNING,"ziplist with dup elements dump",
                    o->ptr,ziplistBlobLen(o->ptr));
                redisAssert(ret == DICT_OK);
            }
        }

        // �ͷ� ziplist �ĵ�����
        hashTypeReleaseIterator(hi);

        // �ͷŶ���ԭ���� ziplist
        zfree(o->ptr);

        // ���¹�ϣ�ı����ֵ����
        o->encoding = REDIS_ENCODING_HT;
        o->ptr = dict;

    } else {
        redisPanic("Unknown hash encoding");
    }
}

/*
 * �Թ�ϣ���� o �ı��뷽ʽ����ת��
 *
 * Ŀǰֻ֧�ֽ� ZIPLIST ����ת���� HT ����
 */
void hashTypeConvert(robj *o, int enc) {

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        hashTypeConvertZiplist(o, enc);

    } else if (o->encoding == REDIS_ENCODING_HT) {
        redisPanic("Not implemented");

    } else {
        redisPanic("Unknown hash encoding");
    }
}

/*-----------------------------------------------------------------------------
 * Hash type commands
 *----------------------------------------------------------------------------*/

void hsetCommand(redisClient *c) {
    int update;
    robj *o;

    // ȡ�����´�����ϣ����
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    // �����Ҫ�Ļ���ת����ϣ����ı���
    hashTypeTryConversion(o,c->argv,2,3);

    // ���� field �� value �����Խ�Լ�ռ�
    hashTypeTryObjectEncoding(o,&c->argv[2], &c->argv[3]);

    // ���� field �� value �� hash
    update = hashTypeSet(o,c->argv[2],c->argv[3]);

    // ����״̬����ʾ field-value ��������ӻ��Ǹ���
    addReply(c, update ? shared.czero : shared.cone);

    // ���ͼ��޸��ź�
    signalModifiedKey(c->db,c->argv[1]);

    // �����¼�֪ͨ
    notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hset",c->argv[1],c->db->id);

    // ����������Ϊ��
    server.dirty++;
}

void hsetnxCommand(redisClient *c) {
    robj *o;

    // ȡ�����´�����ϣ����
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    // �����Ҫ�Ļ���ת����ϣ����ı���
    hashTypeTryConversion(o,c->argv,2,3);

    // ��� field-value ���Ѿ�����
    // ��ô�ظ� 0 
    if (hashTypeExists(o, c->argv[2])) {
        addReply(c, shared.czero);

    // �������� field-value ��
    } else {
        // �� field �� value ������룬�Խ�ʡ�ռ�
        hashTypeTryObjectEncoding(o,&c->argv[2], &c->argv[3]);
        // ����
        hashTypeSet(o,c->argv[2],c->argv[3]);

        // �ظ� 1 ����ʾ���óɹ�
        addReply(c, shared.cone);

        // ���ͼ��޸��ź�
        signalModifiedKey(c->db,c->argv[1]);

        // �����¼�֪ͨ
        notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hset",c->argv[1],c->db->id);

        // �����ݿ���Ϊ��
        server.dirty++;
    }
}

void hmsetCommand(redisClient *c) {
    int i;
    robj *o;

    // field-value ��������ɶԳ���
    if ((c->argc % 2) == 1) {
        addReplyError(c,"wrong number of arguments for HMSET");
        return;
    }

    // ȡ�����´�����ϣ����
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    // �����Ҫ�Ļ���ת����ϣ����ı���
    hashTypeTryConversion(o,c->argv,2,c->argc-1);

    // �������������� field-value ��
    for (i = 2; i < c->argc; i += 2) {
        // ���� field-value �ԣ��Խ�Լ�ռ�
        hashTypeTryObjectEncoding(o,&c->argv[i], &c->argv[i+1]);
        // ����
        hashTypeSet(o,c->argv[i],c->argv[i+1]);
    }

    // ��ͻ��˷��ͻظ�
    addReply(c, shared.ok);

    // ���ͼ��޸��ź�
    signalModifiedKey(c->db,c->argv[1]);

    // �����¼�֪ͨ
    notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hset",c->argv[1],c->db->id);

    // �����ݿ���Ϊ��
    server.dirty++;
}

void hincrbyCommand(redisClient *c) {
    long long value, incr, oldvalue;
    robj *o, *current, *new;

    // ȡ�� incr ������ֵ������������
    if (getLongLongFromObjectOrReply(c,c->argv[3],&incr,NULL) != REDIS_OK) return;

    // ȡ�����´�����ϣ����
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    // ȡ�� field �ĵ�ǰֵ
    if ((current = hashTypeGetObject(o,c->argv[2])) != NULL) {
        // ȡ��ֵ��������ʾ
        if (getLongLongFromObjectOrReply(c,current,&value,
            "hash value is not an integer") != REDIS_OK) {
            decrRefCount(current);
            return;
        }
        decrRefCount(current);
    } else {
        // ���ֵ��ǰ�����ڣ���ôĬ��Ϊ 0
        value = 0;
    }

    // �������Ƿ��������
    oldvalue = value;
    if ((incr < 0 && oldvalue < 0 && incr < (LLONG_MIN-oldvalue)) ||
        (incr > 0 && oldvalue > 0 && incr > (LLONG_MAX-oldvalue))) {
        addReplyError(c,"increment or decrement would overflow");
        return;
    }

    // ������
    value += incr;
    // Ϊ��������µ�ֵ����
    new = createStringObjectFromLongLong(value);
    // ����ֵ����
    hashTypeTryObjectEncoding(o,&c->argv[2],NULL);
    // ���������µ�ֵ��������Ѿ��ж�����ڣ���ô���¶����滻��
    hashTypeSet(o,c->argv[2],new);
    decrRefCount(new);

    // �������������ظ�
    addReplyLongLong(c,value);

    // ���ͼ��޸��ź�
    signalModifiedKey(c->db,c->argv[1]);

    // �����¼�֪ͨ
    notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hincrby",c->argv[1],c->db->id);

    // �����ݿ���Ϊ��
    server.dirty++;
}

void hincrbyfloatCommand(redisClient *c) {
    double long value, incr;
    robj *o, *current, *new, *aux;

    // ȡ�� incr ����
    if (getLongDoubleFromObjectOrReply(c,c->argv[3],&incr,NULL) != REDIS_OK) return;

    // ȡ�����´�����ϣ����
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    // ȡ��ֵ����
    if ((current = hashTypeGetObject(o,c->argv[2])) != NULL) {
        // ��ֵ������ȡ������ֵ
        if (getLongDoubleFromObjectOrReply(c,current,&value,
            "hash value is not a valid float") != REDIS_OK) {
            decrRefCount(current);
            return;
        }
        decrRefCount(current);
    } else {
        // ֵ���󲻴��ڣ�Ĭ��ֵΪ 0
        value = 0;
    }

    // ������
    value += incr;
    // Ϊ����������ֵ����
    new = createStringObjectFromLongDouble(value);
    // ����ֵ����
    hashTypeTryObjectEncoding(o,&c->argv[2],NULL);
    // ���������µ�ֵ��������Ѿ��ж�����ڣ���ô���¶����滻��
    hashTypeSet(o,c->argv[2],new);

    // �����µ�ֵ������Ϊ�ظ�
    addReplyBulk(c,new);

    // ���ͼ��޸��ź�
    signalModifiedKey(c->db,c->argv[1]);

    // �����¼�֪ͨ
    notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hincrbyfloat",c->argv[1],c->db->id);

    // �����ݿ�������
    server.dirty++;

    /* Always replicate HINCRBYFLOAT as an HSET command with the final value
     * in order to make sure that differences in float pricision or formatting
     * will not create differences in replicas or after an AOF restart. */
    // �ڴ��� INCRBYFLOAT ����ʱ�������� SET �������滻 INCRBYFLOAT ����
    // �Ӷ���ֹ��Ϊ��ͬ�ĸ��㾫�Ⱥ͸�ʽ����� AOF ����ʱ�����ݲ�һ��
    aux = createStringObject("HSET",4);
    rewriteClientCommandArgument(c,0,aux);
    decrRefCount(aux);
    rewriteClientCommandArgument(c,3,new);
    decrRefCount(new);
}

/*
 * ��������������ϣ���� field ��ֵ��ӵ��ظ���
 */
static void addHashFieldToReply(redisClient *c, robj *o, robj *field) {
    int ret;

    // ���󲻴���
    if (o == NULL) {
        addReply(c, shared.nullbulk);
        return;
    }

    // ziplist ����
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        // ȡ��ֵ
        ret = hashTypeGetFromZiplist(o, field, &vstr, &vlen, &vll);
        if (ret < 0) {
            addReply(c, shared.nullbulk);
        } else {
            if (vstr) {
                addReplyBulkCBuffer(c, vstr, vlen);
            } else {
                addReplyBulkLongLong(c, vll);
            }
        }

    // �ֵ�
    } else if (o->encoding == REDIS_ENCODING_HT) {
        robj *value;

        // ȡ��ֵ
        ret = hashTypeGetFromHashTable(o, field, &value);
        if (ret < 0) {
            addReply(c, shared.nullbulk);
        } else {
            addReplyBulk(c, value);
        }

    } else {
        redisPanic("Unknown hash encoding");
    }
}

void hgetCommand(redisClient *c) {
    robj *o;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,o,REDIS_HASH)) return;

    // ȡ�����������ֵ
    addHashFieldToReply(c, o, c->argv[2]);
}

void hmgetCommand(redisClient *c) {
    robj *o;
    int i;

    /* Don't abort when the key cannot be found. Non-existing keys are empty
     * hashes, where HMGET should respond with a series of null bulks. */
    // ȡ����ϣ����
    o = lookupKeyRead(c->db, c->argv[1]);

    // ������ڣ��������
    if (o != NULL && o->type != REDIS_HASH) {
        addReply(c, shared.wrongtypeerr);
        return;
    }

    // ��ȡ��� field ��ֵ
    addReplyMultiBulkLen(c, c->argc-2);
    for (i = 2; i < c->argc; i++) {
        addHashFieldToReply(c, o, c->argv[i]);
    }
}

void hdelCommand(redisClient *c) {
    robj *o;
    int j, deleted = 0, keyremoved = 0;

    // ȡ������
    if ((o = lookupKeyWriteOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,REDIS_HASH)) return;

    // ɾ��ָ����ֵ��
    for (j = 2; j < c->argc; j++) {
        if (hashTypeDelete(o,c->argv[j])) {

            // �ɹ�ɾ��һ����ֵ��ʱ���м���
            deleted++;

            // �����ϣ�Ѿ�Ϊ�գ���ôɾ���������
            if (hashTypeLength(o) == 0) {
                dbDelete(c->db,c->argv[1]);
                keyremoved = 1;
                break;
            }
        }
    }

    // ֻҪ������һ����ֵ�Ա��޸��ˣ���ôִ�����´���
    if (deleted) {
        // ���ͼ��޸��ź�
        signalModifiedKey(c->db,c->argv[1]);

        // �����¼�֪ͨ
        notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hdel",c->argv[1],c->db->id);

        // �����¼�֪ͨ
        if (keyremoved)
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1],
                                c->db->id);

        // �����ݿ���Ϊ��
        server.dirty += deleted;
    }

    // ���ɹ�ɾ������ֵ��������Ϊ������ظ��ͻ���
    addReplyLongLong(c,deleted);
}

void hlenCommand(redisClient *c) {
    robj *o;

    // ȡ����ϣ����
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,REDIS_HASH)) return;

    // �ظ�
    addReplyLongLong(c,hashTypeLength(o));
}

/*
 * �ӵ�������ǰָ��Ľڵ���ȡ����ϣ�� field �� value
 */
static void addHashIteratorCursorToReply(redisClient *c, hashTypeIterator *hi, int what) {

    // ���� ZIPLIST
    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        hashTypeCurrentFromZiplist(hi, what, &vstr, &vlen, &vll);
        if (vstr) {
            addReplyBulkCBuffer(c, vstr, vlen);
        } else {
            addReplyBulkLongLong(c, vll);
        }

    // ���� HT
    } else if (hi->encoding == REDIS_ENCODING_HT) {
        robj *value;

        hashTypeCurrentFromHashTable(hi, what, &value);
        addReplyBulk(c, value);

    } else {
        redisPanic("Unknown hash encoding");
    }
}

void genericHgetallCommand(redisClient *c, int flags) {
    robj *o;
    hashTypeIterator *hi;
    int multiplier = 0;
    int length, count = 0;

    // ȡ����ϣ����
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptymultibulk)) == NULL
        || checkType(c,o,REDIS_HASH)) return;

    // ����Ҫȡ����Ԫ������
    if (flags & REDIS_HASH_KEY) multiplier++;
    if (flags & REDIS_HASH_VALUE) multiplier++;

    length = hashTypeLength(o) * multiplier;

    addReplyMultiBulkLen(c, length);

    // �����ڵ㣬��ȡ��Ԫ��
    hi = hashTypeInitIterator(o);
    while (hashTypeNext(hi) != REDIS_ERR) {
        // ȡ����
        if (flags & REDIS_HASH_KEY) {
            addHashIteratorCursorToReply(c, hi, REDIS_HASH_KEY);
            count++;
        }
        // ȡ��ֵ
        if (flags & REDIS_HASH_VALUE) {
            addHashIteratorCursorToReply(c, hi, REDIS_HASH_VALUE);
            count++;
        }
    }

    // �ͷŵ�����
    hashTypeReleaseIterator(hi);
    redisAssert(count == length);
}

void hkeysCommand(redisClient *c) {
    genericHgetallCommand(c,REDIS_HASH_KEY);
}

void hvalsCommand(redisClient *c) {
    genericHgetallCommand(c,REDIS_HASH_VALUE);
}

void hgetallCommand(redisClient *c) {
    genericHgetallCommand(c,REDIS_HASH_KEY|REDIS_HASH_VALUE);
}

void hexistsCommand(redisClient *c) {
    robj *o;

    // ȡ����ϣ����
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,REDIS_HASH)) return;

    // ���������Ƿ����
    addReply(c, hashTypeExists(o,c->argv[2]) ? shared.cone : shared.czero);
}

void hscanCommand(redisClient *c) {
    robj *o;
    unsigned long cursor;

    if (parseScanCursorOrReply(c,c->argv[2],&cursor) == REDIS_ERR) return;
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptyscan)) == NULL ||
        checkType(c,o,REDIS_HASH)) return;
    scanGenericCommand(c,o,cursor);
}
