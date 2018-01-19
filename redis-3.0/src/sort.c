/* SORT command and helper functions.
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
#include "pqsort.h" /* Partial qsort for SORT+LIMIT */
#include <math.h> /* isnan() */

zskiplistNode* zslGetElementByRank(zskiplist *zsl, unsigned long rank);

// ����һ�� SORT ����
redisSortOperation *createSortOperation(int type, robj *pattern) {

    redisSortOperation *so = zmalloc(sizeof(*so));

    so->type = type;
    so->pattern = pattern;

    return so;
}

/* Return the value associated to the key with a name obtained using
 * the following rules:
 *
 * �������¹��򣬷��ظ������ֵļ���������ֵ��
 *
 * 1) The first occurrence of '*' in 'pattern' is substituted with 'subst'.
 *	  ģʽ�г��ֵĵ�һ�� '*' �ַ����滻Ϊ subst
 *
 * 2) If 'pattern' matches the "->" string, everything on the left of
 *    the arrow is treated as the name of a hash field, and the part on the
 *    left as the key name containing a hash. The value of the specified
 *    field is returned.
 *	  ���ģʽ�а���һ�� "->" �ַ�����
 *    ��ô�ַ�������߲��ֻᱻ������һ�� Hash �������֣�
 *    ���ַ������ұ߲��ֻᱻ������ Hash ���е�������field name����
 *    ����������Ӧ��ֵ�ᱻ���ء�
 *
 * 3) If 'pattern' equals "#", the function simply returns 'subst' itself so
 *    that the SORT command can be used like: SORT key GET # to retrieve
 *    the Set/List elements directly.
 *    ���ģʽ���� "#" ����ô����ֱ�ӷ��� subst ����
 *	  �����÷�ʹ�� SORT �������ʹ�� SORT key GET # �ķ�ʽ��ֱ�ӻ�ȡ���ϻ����б��Ԫ�ء�
 *
 * 4) ��� pattern ���� "#" �����Ҳ����� '*' �ַ�����ôֱ�ӷ��� NULL ��
 *
 * The returned object will always have its refcount increased by 1
 * when it is non-NULL. 
 *
 * ������صĶ����� NULL ����ô�����������ü������Ǳ���һ�ġ�
 */
robj *lookupKeyByPattern(redisDb *db, robj *pattern, robj *subst) {
    char *p, *f, *k;
    sds spat, ssub;
    robj *keyobj, *fieldobj = NULL, *o;
    int prefixlen, sublen, postfixlen, fieldlen;

    /* If the pattern is "#" return the substitution object itself in order
     * to implement the "SORT ... GET #" feature. */
	// ���ģʽ�� # ����ôֱ�ӷ��� subst
    spat = pattern->ptr;
    if (spat[0] == '#' && spat[1] == '\0') {
        incrRefCount(subst);
        return subst;
    }

    /* The substitution object may be specially encoded. If so we create
     * a decoded object on the fly. Otherwise getDecodedObject will just
     * increment the ref count, that we'll decrement later. */
    // ��ȡ������ subst
    subst = getDecodedObject(subst);
    // ָ�� subst ��������ַ���
    ssub = subst->ptr;

    /* If we can't find '*' in the pattern we return NULL as to GET a
     * fixed key does not make sense. */
	// ���ģʽ���� "#" ������ģʽ�в��� '*' ����ôֱ�ӷ��� NULL
    // ��Ϊһֱ���ع̶��ļ���û�������
    p = strchr(spat,'*');
    if (!p) {
        decrRefCount(subst);
        return NULL;
    }

    /* Find out if we're dealing with a hash dereference. */
	// ���ָ�������ַ��������� Hash ��
    if ((f = strstr(p+1, "->")) != NULL && *(f+2) != '\0') {
		// Hash ��
        // ��ĳ���
        fieldlen = sdslen(spat)-(f-spat)-2;
        // ��Ķ���
        fieldobj = createStringObject(f+2,fieldlen);
    } else {
		// �ַ�������û����
        fieldlen = 0;
    }

    /* Perform the '*' substitution. */
	// ����ģʽ�������滻
	// ����˵�� subst Ϊ www ��ģʽΪ nono_happ_*
	// ��ô�滻������� nono_happ_www
    // �ֱ���˵�� subst Ϊ peter ��ģʽΪ *-info->age
    // ��ô�滻������� peter-info->age
    prefixlen = p-spat;
    sublen = sdslen(ssub);
    postfixlen = sdslen(spat)-(prefixlen+1)-(fieldlen ? fieldlen+2 : 0);
    keyobj = createStringObject(NULL,prefixlen+sublen+postfixlen);
    k = keyobj->ptr;
    memcpy(k,spat,prefixlen);
    memcpy(k+prefixlen,ssub,sublen);
    memcpy(k+prefixlen+sublen,p+1,postfixlen);
    decrRefCount(subst); /* Incremented by decodeObject() */

    /* Lookup substituted key */
	// �����滻 key
    o = lookupKeyRead(db,keyobj);
    if (o == NULL) goto noobj;

    // ����һ�� Hash ��
    if (fieldobj) {
        if (o->type != REDIS_HASH) goto noobj;

        /* Retrieve value from hash by the field name. This operation
         * already increases the refcount of the returned object. */
		// �� Hash ����ָ�����л�ȡֵ
        o = hashTypeGetObject(o, fieldobj);

    // ����һ���ַ�����
    } else {
        if (o->type != REDIS_STRING) goto noobj;

        /* Every object that this function returns needs to have its refcount
         * increased. sortCommand decreases it again. */
		// ��һ�ַ������ļ���
        incrRefCount(o);
    }
    decrRefCount(keyobj);
    if (fieldobj) decrRefCount(fieldobj);

    // ����ֵ
    return o;

noobj:
    decrRefCount(keyobj);
    if (fieldlen) decrRefCount(fieldobj);
    return NULL;
}

/* sortCompare() is used by qsort in sortCommand(). Given that qsort_r with
 * the additional parameter is not standard but a BSD-specific we have to
 * pass sorting parameters via the global 'server' structure */
// �����㷨��ʹ�õĶԱȺ���
int sortCompare(const void *s1, const void *s2) {
    const redisSortObject *so1 = s1, *so2 = s2;
    int cmp;

    if (!server.sort_alpha) {

        /* Numeric sorting. Here it's trivial as we precomputed scores */
		// ��ֵ����

        if (so1->u.score > so2->u.score) {
            cmp = 1;
        } else if (so1->u.score < so2->u.score) {
            cmp = -1;
        } else {
            /* Objects have the same score, but we don't want the comparison
             * to be undefined, so we compare objects lexicographically.
             * This way the result of SORT is deterministic. */
			// ����Ԫ�صķ�ֵһ������Ϊ��������Ľ����ȷ���Եģ�deterministic��
			// ���Ƕ�Ԫ�ص��ַ�����������ֵ�������
            cmp = compareStringObjects(so1->obj,so2->obj);
        }
    } else {

        /* Alphanumeric sorting */
		// �ַ�����

        if (server.sort_bypattern) {

		    // ��ģʽ���жԱ�

			// ������һ������Ϊ NULL
            if (!so1->u.cmpobj || !so2->u.cmpobj) {
                /* At least one compare object is NULL */
                if (so1->u.cmpobj == so2->u.cmpobj)
                    cmp = 0;
                else if (so1->u.cmpobj == NULL)
                    cmp = -1;
                else
                    cmp = 1;
            } else {
                /* We have both the objects, compare them. */
				// �������󶼲�Ϊ NULL

                if (server.sort_store) {
					// �Զ����Ʒ�ʽ�Ա�����ģʽ
                    cmp = compareStringObjects(so1->u.cmpobj,so2->u.cmpobj);
                } else {
                    /* Here we can use strcoll() directly as we are sure that
                     * the objects are decoded string objects. */
					// �Ա��ر���Ա�����ģʽ
                    cmp = strcoll(so1->u.cmpobj->ptr,so2->u.cmpobj->ptr);
                }
            }

        } else {


            /* Compare elements directly. */
			// �Ա��ַ�������

            if (server.sort_store) {
				// �Զ����Ʒ�ʽ�Ա��ַ�������
                cmp = compareStringObjects(so1->obj,so2->obj);
            } else {
				// �Ա��ر���Ա��ַ�������
                cmp = collateStringObjects(so1->obj,so2->obj);
            }
        }
    }

    return server.sort_desc ? -cmp : cmp;
}

/*
SORT?

SORT key [BY pattern] [LIMIT offset count] [GET pattern [GET pattern ...]] [ASC | DESC] [ALPHA] [STORE destination]

���ػ򱣴�����б����ϡ����򼯺� key �о��������Ԫ�ء�

����Ĭ����������Ϊ����ֵ������Ϊ˫���ȸ�������Ȼ����бȽϡ�


һ�� SORT �÷�

��򵥵� SORT ʹ�÷����� SORT key �� SORT key DESC ��
?SORT key ���ؼ�ֵ��С��������Ľ����
?SORT key DESC ���ؼ�ֵ�Ӵ�С����Ľ����

���� today_cost �б����˽��յĿ����� ��ô������ SORT ���������������


# ��������б�

redis> LPUSH today_cost 30 1.5 10 8
(integer) 4

# ����

redis> SORT today_cost
1) "1.5"
2) "8"
3) "10"
4) "30"

# ��������

redis 127.0.0.1:6379> SORT today_cost DESC
1) "30"
2) "10"
3) "8"
4) "1.5"



ʹ�� ALPHA ���η����ַ�����������

��Ϊ SORT ����Ĭ���������Ϊ���֣� ����Ҫ���ַ�����������ʱ�� ��Ҫ��ʽ���� SORT ����֮����� ALPHA ���η���


# ��ַ

redis> LPUSH website "www.reddit.com"
(integer) 1

redis> LPUSH website "www.slashdot.com"
(integer) 2

redis> LPUSH website "www.infoq.com"
(integer) 3

# Ĭ�ϣ������֣�����

redis> SORT website
1) "www.infoq.com"
2) "www.slashdot.com"
3) "www.reddit.com"

# ���ַ�����

redis> SORT website ALPHA
1) "www.infoq.com"
2) "www.reddit.com"
3) "www.slashdot.com"


���ϵͳ��ȷ�������� LC_COLLATE ���������Ļ���Redis��ʶ�� UTF-8 ���롣


ʹ�� LIMIT ���η����Ʒ��ؽ��

����֮�󷵻�Ԫ�ص���������ͨ�� LIMIT ���η��������ƣ� ���η����� offset �� count ����������
?offset ָ��Ҫ������Ԫ��������
?count ָ������ offset ��ָ����Ԫ��֮��Ҫ���ض��ٸ�����

�������ӷ�����������ǰ 5 ������( offset Ϊ 0 ��ʾû��Ԫ�ر�����)��


# ��Ӳ������ݣ��б�ֵΪ 1 ָ 10

redis 127.0.0.1:6379> RPUSH rank 1 3 5 7 9
(integer) 5

redis 127.0.0.1:6379> RPUSH rank 2 4 6 8 10
(integer) 10

# �����б�����С�� 5 ��ֵ

redis 127.0.0.1:6379> SORT rank LIMIT 0 5
1) "1"
2) "2"
3) "3"
4) "4"
5) "5"


�������ʹ�ö�����η����������ӷ��شӴ�С�����ǰ 5 ������


redis 127.0.0.1:6379> SORT rank LIMIT 0 5 DESC
1) "10"
2) "9"
3) "8"
4) "7"
5) "6"



ʹ���ⲿ key ��������

����ʹ���ⲿ key ��������ΪȨ�أ�����Ĭ�ϵ�ֱ�ӶԱȼ�ֵ�ķ�ʽ����������

�����������û��������£�






uid

user_name_{uid}

user_level_{uid}


1 admin 9999 
2 jack 10 
3 peter 25 
4 mary 70 

���´��뽫�������뵽 Redis �У�


# admin

redis 127.0.0.1:6379> LPUSH uid 1
(integer) 1

redis 127.0.0.1:6379> SET user_name_1 admin
OK

redis 127.0.0.1:6379> SET user_level_1 9999
OK

# jack

redis 127.0.0.1:6379> LPUSH uid 2
(integer) 2

redis 127.0.0.1:6379> SET user_name_2 jack
OK

redis 127.0.0.1:6379> SET user_level_2 10
OK

# peter

redis 127.0.0.1:6379> LPUSH uid 3
(integer) 3

redis 127.0.0.1:6379> SET user_name_3 peter
OK

redis 127.0.0.1:6379> SET user_level_3 25
OK

# mary

redis 127.0.0.1:6379> LPUSH uid 4
(integer) 4

redis 127.0.0.1:6379> SET user_name_4 mary
OK

redis 127.0.0.1:6379> SET user_level_4 70
OK



BY ѡ��

Ĭ������£� SORT uid ֱ�Ӱ� uid �е�ֵ����


redis 127.0.0.1:6379> SORT uid
1) "1"      # admin
2) "2"      # jack
3) "3"      # peter
4) "4"      # mary


ͨ��ʹ�� BY ѡ������� uid ����������Ԫ��������

����˵�� ���´����� uid ������ user_level_{uid} �Ĵ�С������


redis 127.0.0.1:6379> SORT uid BY user_level_*
1) "2"      # jack , level = 10
2) "3"      # peter, level = 25
3) "4"      # mary, level = 70
4) "1"      # admin, level = 9999


user_level_* ��һ��ռλ���� ����ȡ�� uid �е�ֵ�� Ȼ���������ֵ��������Ӧ�ļ���

�����ڶ� uid �б��������ʱ�� ����ͻ���ȡ�� uid ��ֵ 1 �� 2 �� 3 �� 4 �� Ȼ��ʹ�� user_level_1 �� user_level_2 �� user_level_3 �� user_level_4 ��ֵ��Ϊ���� uid ��Ȩ�ء�


GET ѡ��

ʹ�� GET ѡ� ���Ը�������Ľ����ȡ����Ӧ�ļ�ֵ��

����˵�� ���´��������� uid �� ��ȡ���� user_name_{uid} ��ֵ��


redis 127.0.0.1:6379> SORT uid GET user_name_*
1) "admin"
2) "jack"
3) "peter"
4) "mary"



���ʹ�� BY �� GET

ͨ�����ʹ�� BY �� GET �� �������������Ը�ֱ�۵ķ�ʽ��ʾ������

����˵�� ���´����Ȱ� user_level_{uid} ������ uid �б� ��ȡ����Ӧ�� user_name_{uid} ��ֵ��


redis 127.0.0.1:6379> SORT uid BY user_level_* GET user_name_*
1) "jack"       # level = 10
2) "peter"      # level = 25
3) "mary"       # level = 70
4) "admin"      # level = 9999


���ڵ�������Ҫ��ֻʹ�� SORT uid BY user_level_* Ҫֱ�۵öࡣ


��ȡ����ⲿ��

����ͬʱʹ�ö�� GET ѡ� ��ȡ����ⲿ����ֵ��

���´���Ͱ� uid �ֱ��ȡ user_level_{uid} �� user_name_{uid} ��


redis 127.0.0.1:6379> SORT uid GET user_level_* GET user_name_*
1) "9999"       # level
2) "admin"      # name
3) "10"
4) "jack"
5) "25"
6) "peter"
7) "70"
8) "mary"


GET ��һ������Ĳ��������Ǿ��� ���� ������ # ��ȡ���������ֵ��

���´���ͽ� uid ��ֵ��������Ӧ�� user_level_* �� user_name_* ������Ϊ�����


redis 127.0.0.1:6379> SORT uid GET # GET user_level_* GET user_name_*
1) "1"          # uid
2) "9999"       # level
3) "admin"      # name
4) "2"
5) "10"
6) "jack"
7) "3"
8) "25"
9) "peter"
10) "4"
11) "70"
12) "mary"



��ȡ�ⲿ����������������

ͨ����һ�������ڵļ���Ϊ�������� BY ѡ� ������ SORT ������������� ֱ�ӷ��ؽ����


redis 127.0.0.1:6379> SORT uid BY not-exists-key
1) "4"
2) "3"
3) "2"
4) "1"


�����÷��ڵ���ʹ��ʱ��ûʲôʵ���ô���

������ͨ���������÷��� GET ѡ����ϣ� �Ϳ����ڲ����������£� ��ȡ����ⲿ���� �൱��ִ��һ�����ϵĻ�ȡ������������ SQL ���ݿ�� join �ؼ��֣���

���´�����ʾ�ˣ�����ڲ��������������£�ʹ�� SORT �� BY �� GET ��ȡ����ⲿ����


redis 127.0.0.1:6379> SORT uid BY not-exists-key GET # GET user_level_* GET user_name_*
1) "4"      # id
2) "70"     # level
3) "mary"   # name
4) "3"
5) "25"
6) "peter"
7) "2"
8) "10"
9) "jack"
10) "1"
11) "9999"
12) "admin"



����ϣ����Ϊ GET �� BY �Ĳ���

���˿��Խ��ַ�����֮�⣬ ��ϣ��Ҳ������Ϊ GET �� BY ѡ��Ĳ�����ʹ�á�

����˵������ǰ��������û���Ϣ��






uid

user_name_{uid}

user_level_{uid}


1 admin 9999 
2 jack 10 
3 peter 25 
4 mary 70 

���ǿ��Բ����û������ֺͼ��𱣴��� user_name_{uid} �� user_level_{uid} �����ַ������У� ������һ������ name ��� level ��Ĺ�ϣ�� user_info_{uid} �������û������ֺͼ�����Ϣ��


redis 127.0.0.1:6379> HMSET user_info_1 name admin level 9999
OK

redis 127.0.0.1:6379> HMSET user_info_2 name jack level 10
OK

redis 127.0.0.1:6379> HMSET user_info_3 name peter level 25
OK

redis 127.0.0.1:6379> HMSET user_info_4 name mary level 70
OK


֮�� BY �� GET ѡ������� key->field �ĸ�ʽ����ȡ��ϣ���е����ֵ�� ���� key ��ʾ��ϣ����� �� field ���ʾ��ϣ�����


redis 127.0.0.1:6379> SORT uid BY user_info_*->level
1) "2"
2) "3"
3) "4"
4) "1"

redis 127.0.0.1:6379> SORT uid BY user_info_*->level GET user_info_*->name
1) "jack"
2) "peter"
3) "mary"
4) "admin"



����������

Ĭ������£� SORT ����ֻ�Ǽ򵥵ط��������������������κα��������

ͨ���� STORE ѡ��ָ��һ�� key ���������Խ����������浽�����ļ��ϡ�

�����ָ���� key �Ѵ��ڣ���ôԭ�е�ֵ�������������ǡ�


# ��������

redis 127.0.0.1:6379> RPUSH numbers 1 3 5 7 9
(integer) 5

redis 127.0.0.1:6379> RPUSH numbers 2 4 6 8 10
(integer) 10

redis 127.0.0.1:6379> LRANGE numbers 0 -1
1) "1"
2) "3"
3) "5"
4) "7"
5) "9"
6) "2"
7) "4"
8) "6"
9) "8"
10) "10"

redis 127.0.0.1:6379> SORT numbers STORE sorted-numbers
(integer) 10

# �����Ľ��

redis 127.0.0.1:6379> LRANGE sorted-numbers 0 -1
1) "1"
2) "2"
3) "3"
4) "4"
5) "5"
6) "6"
7) "7"
8) "8"
9) "9"
10) "10"


����ͨ���� SORT �����ִ�н�����棬���� EXPIRE Ϊ�����������ʱ�䣬�Դ�������һ�� SORT �����Ľ�����档

�����Ϳ��Ա���� SORT ������Ƶ�����ã�ֻ�е����������ʱ������Ҫ�ٵ���һ�� SORT ������

���⣬Ϊ����ȷʵ����һ�÷����������Ҫ�����Ա������ͻ���ͬʱ���л����ؽ�(Ҳ���Ƕ���ͻ��ˣ�ͬһʱ����� SORT ������������Ϊ�����)������μ� SETNX ���
���ð汾��>= 1.0.0ʱ�临�Ӷȣ�

O(N+M*log(M))�� N ΪҪ������б�򼯺��ڵ�Ԫ�������� M ΪҪ���ص�Ԫ��������

���ֻ��ʹ�� SORT ����� GET ѡ���ȡ���ݶ�û�н�������ʱ�临�Ӷ� O(N)��
����ֵ��

û��ʹ�� STORE �����������б���ʽ����������

ʹ�� STORE ������������������Ԫ��������

*/
/* The SORT command is the most complex command in Redis. Warning: this code
 * is optimized for speed and a bit less for readability */
void sortCommand(redisClient *c) { 
//sortֻ����ʱ��ȡ������������ͻ��ˣ�ԭ���б����hash���߼����еĴ洢˳�򲻱䣬����lpush yang 1 a b c d 3 5, sort yang alpha�Ľ����1 3 5 a b c d
//������ԭ���б��еĳ�Ա�洢˳����1 a b c d 3 5  ���������Լ���store���������Ľ�����浽һ���µ��б���

    
    /*
    SORT����Ϊÿ��������ļ�������һ�����������ͬ�����飬�����ÿ�����һ��_redisSortObject�ṹ������SORT����ʹ�õ�ѡ�ͬ��
    ����ʹ��_redisSortObject�ṹ�ķ�ʽҲ��ͬ�����������˳������ݴ�����ʵ�֣������鿪ʼ������ĩβ��������߽��򡣵�����ԭ�����ݵĴ洢��ϵ
    ˳���ǲ���ģ�˳������������_redisSortObject������
    */


    /*ѡ���ִ��˳��
      �������ѡ�������ֵĻ���һ��SORT�����ִ�й��̿��Է�Ϊ�����Ĳ���
        1)��������һ���������ʹ��ALPHA��ASC��DESC��BY�⼸��ѡ��������
    �������򣬲��õ�һ������������
      2)�������������ĳ��ȣ�����һ���������ʹ��LIMITѡ������������ĳ���
    �������ƣ�ֻ��LIMITѡ��ָ�����ǲ���Ԫ�ػᱻ���������������С�
      3)��ȡ�ⲿ��������һ���������ʹ��GETѡ��������������е�Ԫ�أ��Լ�
    GETѡ��ָ����ģʽ�����Ҳ���ȡָ������ֵ��������Щֵ����Ϊ�µ�����������
      4)������������������һ���������ʹ��STOREѡ��������������浽ָ����
    ������ȥ��
      5)��ͻ��˷��������������������һ�����������������������������ͻ���
    �������������е�Ԫ�ء�
      ��������Щ�����У���һ�����������ǰһ���������֮����С�
      �ٸ����ӣ�����ͻ���������������������
      SORT <key> AIPHA DESC BY <by-pattern> LIMIT <offset> <count> GET <get-pattern>
        STORE <store_key>
      ��ô�������Ȼ�ִ�У�
      SORT <key> ALPHA DESC BY <by-pattern>
      ����ִ�У�
      LIMIT <offset> <count>
      �Ⱥ�ִ�У�
      GET <get-pattern>
      ֮��ִ�У�
      STORE <store_key>
      ����������������������������е�Ԫ�����η��ظ��ͻ��ˡ�
    */
    

    list *operations; 
    unsigned int outputlen = 0;
    int desc = 0, alpha = 0;
    long limit_start = 0, limit_count = -1, start, end;
    int j, dontsort = 0, vectorlen;
    int getop = 0; /* GET operation counter */
    int int_convertion_error = 0;
    int syntax_error = 0;
    robj *sortval, *sortby = NULL, *storekey = NULL;
    redisSortObject *vector; /* Resulting vector to sort */

    /* Lookup the key to sort. It must be of the right types */
	// ��ȡҪ����ļ�����������Ƿ���Ա����������
    sortval = lookupKeyRead(c->db,c->argv[1]);
    if (sortval && sortval->type != REDIS_SET &&
                   sortval->type != REDIS_LIST &&
                   sortval->type != REDIS_ZSET)
    {
        addReply(c,shared.wrongtypeerr);
        return;
    }

    /* Create a list of operations to perform for every sorted element.
     * Operations can be GET/DEL/INCR/DECR */
	// ����һ�����������б�����Ҫ������������Ԫ��ִ�еĲ���
	// ���������� GET �� DEL �� INCR ���� DECR
    operations = listCreate();
    listSetFreeMethod(operations,zfree);

	// ָ�����λ��
    j = 2; /* options start at argv[2] */

    /* Now we need to protect sortval incrementing its count, in the future
     * SORT may have options able to overwrite/delete keys during the sorting
     * and the sorted key itself may get destroyed */
	// Ϊ sortval �����ü�����һ
	// �ڽ����� SORT �������������ĳ�����Ĺ����У����ǻ���ɾ���Ǹ���
    if (sortval)
        incrRefCount(sortval);
    else
        sortval = createListObject();

    /* The SORT command has an SQL-alike syntax, parse it */
	// ���벢���� SORT �����ѡ��
    while(j < c->argc) {

        int leftargs = c->argc-j-1;

		// ASC ѡ��
        if (!strcasecmp(c->argv[j]->ptr,"asc")) { //�������� ���������asc����descĬ���������� 
            desc = 0;

		// DESC ѡ��
        } else if (!strcasecmp(c->argv[j]->ptr,"desc")) { //�������� ���������asc����descĬ���������� 
            desc = 1;

		// ALPHA ѡ��
        } else if (!strcasecmp(c->argv[j]->ptr,"alpha")) {//����alphaĬ�ϰ�����������
            alpha = 1;

        /*
          LIMITѡ��ĸ�ʽΪLIMIT <offset> <count>:
        ��offset������ʾҪ������������Ԫ��������
        ��count������ʾ��������������������Ԫ��֮��Ҫ���ص�������Ԫ��������
        �ٸ����ӣ����´������ȶ�alphabet���Ͻ������򣬽�������0��������Ԫ�أ�Ȼ�󷵻�4��������Ԫ�أ�
          */
		// LIMIT ѡ�� ��Ĭ������£�SORT�����ܻὫ����������Ԫ�ض����ظ��ͻ��ˣ�����ͨ�� LIMITѡ��ĸ�ʽΪLIMIT <offset> <count>ѡ��������
        } else if (!strcasecmp(c->argv[j]->ptr,"limit") && leftargs >= 2) {
			// start ������ count ����
            if ((getLongFromObjectOrReply(c, c->argv[j+1], &limit_start, NULL)
                 != REDIS_OK) ||
                (getLongFromObjectOrReply(c, c->argv[j+2], &limit_count, NULL)
                 != REDIS_OK))
            {
                syntax_error++;
                break;
            }
            j+=2;

        /* ��Ĭ������£�SORT����ֻ��ͻ��˷�������������������������. ���ǣ�ͨ��ʹ��STOREѡ����ǿ��Խ�������������ָ���ļ����� */
		// STORE ѡ��  store�������԰������Ľ����������˳��rpush����storekey����,Ҳ����storekey�½��ǰ����б�洢��
        } else if (!strcasecmp(c->argv[j]->ptr,"store") && leftargs >= 1) {
			// Ŀ���
            storekey = c->argv[j+1];
            j++;

		// BY ѡ�� ��Ĭ������£�SORT����ʹ�ñ������������Ԫ����Ϊ�����Ȩ�أ�Ԫ�ر��������Ԫ��������֮��������λ�á� ����BY���԰����Լ��ƶ���Ȩ��������
        } else if (!strcasecmp(c->argv[j]->ptr,"by") && leftargs >= 1) { 

			// �����˳�������ģʽ����
            sortby = c->argv[j+1];

            /* If the BY pattern does not contain '*', i.e. it is constant,
             * we don't need to sort nor to lookup the weight keys. */
			// ��� sortby ģʽ���治���� '*' ���ţ�
            // ��ô����ִ���������
            if (strchr(c->argv[j+1]->ptr,'*') == NULL) {
                dontsort = 1;
            } else {
                /* If BY is specified with a real patter, we can't accept
                 * it in cluster mode. */
                if (server.cluster_enabled) {
                    addReplyError(c,"BY option of SORT denied in Cluster mode.");
                    syntax_error++;
                    break;
                }
            }
            j++;

		// GET ѡ��
        } else if (!strcasecmp(c->argv[j]->ptr,"get") && leftargs >= 1) {

			// ����һ�� GET ����

            // �����ڼ�Ⱥģʽ��ʹ�� GET ѡ��
            if (server.cluster_enabled) {
                addReplyError(c,"GET option of SORT denied in Cluster mode.");
                syntax_error++;
                break;
            }
            listAddNodeTail(operations,createSortOperation(
                REDIS_SORT_GET,c->argv[j+1]));
            getop++;
            j++;

		// δ֪ѡ��﷨����
        } else {
            addReply(c,shared.syntaxerr);
            syntax_error++;
            break;
        }

        j++;
    }

    /* Handle syntax errors set during options parsing. */
    if (syntax_error) {
        decrRefCount(sortval);
        listRelease(operations);
        return;
    }

    /* For the STORE option, or when SORT is called from a Lua script,
     * we want to force a specific ordering even when no explicit ordering
     * was asked (SORT BY nosort). This guarantees that replication / AOF
     * is deterministic.
	 *
	 * ���� STORE ѡ��Լ��� Lua �ű��е��� SORT ��������������
	 * �����뼴ʹ��û��ָ������ʽ������£�Ҳǿ��ָ��һ�����򷽷���
	 * ����Ա�֤����/AOF ��ȷ���Եġ�
     *
     * However in the case 'dontsort' is true, but the type to sort is a
     * sorted set, we don't need to do anything as ordering is guaranteed
     * in this special case. 
	 *
	 * �� dontsort Ϊ�棬���ұ�����ļ��������򼯺�ʱ��
	 * ���ǲ���ҪΪ����ָ������ʽ��
     * ��Ϊ���򼯺ϵĳ�Ա�Ѿ���������ˡ�
	 */
    if ((storekey || c->flags & REDIS_LUA_CLIENT) &&
        (dontsort && sortval->type != REDIS_ZSET))
    {
        /* Force ALPHA sorting */
		// ǿ�� ALPHA ����
        dontsort = 0;
        alpha = 1;
        sortby = NULL;
    }

    /* Destructively convert encoded sorted sets for SORT. */
	// ����������򼯺ϱ����� SKIPLIST �����
    // ������ǵĻ�����ô����ת���� SKIPLIST ����
    if (sortval->type == REDIS_ZSET)
        zsetConvert(sortval, REDIS_ENCODING_SKIPLIST);

    /* Objtain the length of the object to sort. */
	// ��ȡҪ�������ĳ���
    switch(sortval->type) {
    case REDIS_LIST: vectorlen = listTypeLength(sortval); break;
    case REDIS_SET: vectorlen =  setTypeSize(sortval); break;
    case REDIS_ZSET: vectorlen = dictSize(((zset*)sortval->ptr)->dict); break;
    default: vectorlen = 0; redisPanic("Bad SORT type"); /* Avoid GCC warning */
    }

    /* Perform LIMIT start,count sanity checking. */
	// �� LIMIT ѡ��� start �� count �������м��
    start = (limit_start < 0) ? 0 : limit_start;
    end = (limit_count < 0) ? vectorlen-1 : start+limit_count-1;
    if (start >= vectorlen) {
        start = vectorlen-1;
        end = vectorlen-2;
    }
    if (end >= vectorlen) end = vectorlen-1;

    /* Optimization:
	 * �Ż�
     *
     * 1) if the object to sort is a sorted set.
	 *    �������Ķ��������򼯺�
     * 2) There is nothing to sort as dontsort is true (BY <constant string>).
	 *	  dontsort Ϊ�棬��ʾû��ʲô��Ҫ����
     * 3) We have a LIMIT option that actually reduces the number of elements
     *    to fetch.
	 *	  LIMIT ѡ�������õķ�Χ�������򼯺ϵĳ���ҪС
     *
     * In this case to load all the objects in the vector is a huge waste of
     * resources. We just allocate a vector that is big enough for the selected
     * range length, and make sure to load just this part in the vector. 
	 *
	 * ����������£�����Ҫ�������򼯺��е�����Ԫ�أ�ֻҪ���������Χ��range���ڵ�Ԫ�ؾͿ����ˡ�
	 */
    if (sortval->type == REDIS_ZSET &&
        dontsort &&
        (start != 0 || end != vectorlen-1))
    {
        vectorlen = end-start+1;
    }

    /* Load the sorting vector with all the objects to sort */
	// ���� redisSortObject ����
    vector = zmalloc(sizeof(redisSortObject)*vectorlen);
    j = 0;

	// ���б����������
    if (sortval->type == REDIS_LIST) {
        listTypeIterator *li = listTypeInitIterator(sortval,0,REDIS_TAIL);
        listTypeEntry entry;
        while(listTypeNext(li,&entry)) {
            vector[j].obj = listTypeGet(&entry);
            vector[j].u.score = 0;
            vector[j].u.cmpobj = NULL;
            j++;
        }
        listTypeReleaseIterator(li);

	// ������Ԫ�ط�������
    } else if (sortval->type == REDIS_SET) {
        setTypeIterator *si = setTypeInitIterator(sortval);
        robj *ele;
        while((ele = setTypeNextObject(si)) != NULL) {
            vector[j].obj = ele;
            vector[j].u.score = 0;
            vector[j].u.cmpobj = NULL;
            j++;
        }
        setTypeReleaseIterator(si);

	// �� dontsort Ϊ��������
	// �����򼯺ϵĲ��ֳ�Ա�Ž�����
    } else if (sortval->type == REDIS_ZSET && dontsort) {
        /* Special handling for a sorted set, if 'dontsort' is true.
         * This makes sure we return elements in the sorted set original
         * ordering, accordingly to DESC / ASC options.
         *
         * Note that in this case we also handle LIMIT here in a direct
         * way, just getting the required range, as an optimization. */

		// ����ǰ��˵���ģ����Խ����Ż��� case

        zset *zs = sortval->ptr;
        zskiplist *zsl = zs->zsl;
        zskiplistNode *ln;
        robj *ele;
        int rangelen = vectorlen;

        /* Check if starting point is trivial, before doing log(N) lookup. */
		// ���� desc ���� asc ����ָ���ʼ�ڵ�
        if (desc) {
            long zsetlen = dictSize(((zset*)sortval->ptr)->dict);

            ln = zsl->tail;
            if (start > 0)
                ln = zslGetElementByRank(zsl,zsetlen-start);
        } else {
            ln = zsl->header->level[0].forward;
            if (start > 0)
                ln = zslGetElementByRank(zsl,start+1);
        }

		// ������Χ�е����нڵ㣬���Ž�����
        while(rangelen--) {
            redisAssertWithInfo(c,sortval,ln != NULL);
            ele = ln->obj;
            vector[j].obj = ele;
            vector[j].u.score = 0;
            vector[j].u.cmpobj = NULL;
            j++;
            ln = desc ? ln->backward : ln->level[0].forward;
        }
        /* The code producing the output does not know that in the case of
         * sorted set, 'dontsort', and LIMIT, we are able to get just the
         * range, already sorted, so we need to adjust "start" and "end"
         * to make sure start is set to 0. */
        end -= start;
        start = 0;

	// ��ͨ����µ����򼯺ϣ������м��ϳ�Ա�Ž�����
    } else if (sortval->type == REDIS_ZSET) {
        dict *set = ((zset*)sortval->ptr)->dict;
        dictIterator *di;
        dictEntry *setele;
        di = dictGetIterator(set);
        while((setele = dictNext(di)) != NULL) {
            vector[j].obj = dictGetKey(setele);
            vector[j].u.score = 0;
            vector[j].u.cmpobj = NULL;
            j++;
        }
        dictReleaseIterator(di);
    } else {
        redisPanic("Unknown type");
    }
    redisAssertWithInfo(c,sortval,j == vectorlen);

    /* Now it's time to load the right scores in the sorting vector */
	// ����Ȩ��ֵ
    if (dontsort == 0) {

        for (j = 0; j < vectorlen; j++) {
            robj *byval;

			// ���ʹ���� BY ѡ���ô�͸���ָ���Ķ�����ΪȨ��
            if (sortby) {
                /* lookup value to sort by */
                byval = lookupKeyByPattern(c->db,sortby,vector[j].obj);
                if (!byval) continue;
			// ���û��ʹ�� BY ѡ���ôʹ�ö�������ΪȨ��
            } else {
                /* use object itself to sort by */
                byval = vector[j].obj;
            }

			// ����� ALPHA ������ô���Աȶ����Ϊ������ byval
            if (alpha) {
                if (sortby) vector[j].u.cmpobj = getDecodedObject(byval);
			// ���򣬽��ַ�������ת���� double ����
            } else {
                if (sdsEncodedObject(byval)) {
                    char *eptr;
					// ���ַ���ת���� double ����
                    vector[j].u.score = strtod(byval->ptr,&eptr);
                    if (eptr[0] != '\0' || errno == ERANGE ||
                        isnan(vector[j].u.score))
                    {
                        int_convertion_error = 1;
                    }
                } else if (byval->encoding == REDIS_ENCODING_INT) {
                    /* Don't need to decode the object if it's
                     * integer-encoded (the only encoding supported) so
                     * far. We can just cast it */
					// ֱ�ӽ���������ΪȨ��
                    vector[j].u.score = (long)byval->ptr;
                } else {
                    redisAssertWithInfo(c,sortval,1 != 1);
                }
            }

            /* when the object was retrieved using lookupKeyByPattern,
             * its refcount needs to be decreased. */
            if (sortby) {
                decrRefCount(byval);
            }
        }

    }

	// ����
    if (dontsort == 0) {
        server.sort_desc = desc;
        server.sort_alpha = alpha;
        server.sort_bypattern = sortby ? 1 : 0;
        server.sort_store = storekey ? 1 : 0;

        if (sortby && (start != 0 || end != vectorlen-1))
            pqsort(vector,vectorlen,sizeof(redisSortObject),sortCompare, start,end);
        else
            qsort(vector,vectorlen,sizeof(redisSortObject),sortCompare);
    }

    /* Send command output to the output buffer, performing the specified
     * GET/DEL/INCR/DECR operations if any. */
	// �����������ŵ����������
	// Ȼ��ִ�и����� GET / DEL / INCR �� DECR ����
    outputlen = getop ? getop*(end-start+1) : end-start+1;
    if (int_convertion_error) {
        addReplyError(c,"One or more scores can't be converted into double");
    } else if (storekey == NULL) {

        /* STORE option not specified, sent the sorting result to client */
		// STORE ѡ��δʹ�ã�ֱ�ӽ����������͸��ͻ���

        addReplyMultiBulkLen(c,outputlen);
        for (j = start; j <= end; j++) {
            listNode *ln;
            listIter li;

			// û������ GET ѡ�ֱ�ӽ������ӵ��ظ�
            if (!getop) addReplyBulk(c,vector[j].obj);

            // ������ GET ѡ�����

			// �������õĲ���
            listRewind(operations,&li);
            while((ln = listNext(&li))) {
                redisSortOperation *sop = ln->value;

				// ���Ͳ����Ҽ�
                robj *val = lookupKeyByPattern(c->db,sop->pattern,
                    vector[j].obj);

				// ִ�� GET ��������ָ������ֵ��ӵ��ظ�
                if (sop->type == REDIS_SORT_GET) {
                    if (!val) {
                        addReply(c,shared.nullbulk);
                    } else {
                        addReplyBulk(c,val);
                        decrRefCount(val);
                    }

				// DEL ��INCR �� DECR ��������δʵ��
                } else {
                    /* Always fails */
                    redisAssertWithInfo(c,sortval,sop->type == REDIS_SORT_GET);
                }
            }
        }
    } else {
        robj *sobj = createZiplistObject();

        /* STORE option specified, set the sorting result as a List object */
		// ������ STORE ѡ������������浽�б����

        for (j = start; j <= end; j++) {
            listNode *ln;
            listIter li;

			// û�� GET ��ֱ�ӷ�������Ԫ��
            if (!getop) {
                listTypePush(sobj,vector[j].obj,REDIS_TAIL);

			// �� GET ����ȡָ���ļ�
            } else {
                listRewind(operations,&li);
                while((ln = listNext(&li))) {
                    redisSortOperation *sop = ln->value;
                    robj *val = lookupKeyByPattern(c->db,sop->pattern,
                        vector[j].obj);

                    if (sop->type == REDIS_SORT_GET) {
                        if (!val) val = createStringObject("",0);

                        /* listTypePush does an incrRefCount, so we should take care
                         * care of the incremented refcount caused by either
                         * lookupKeyByPattern or createStringObject("",0) */
                        listTypePush(sobj,val,REDIS_TAIL);
                        decrRefCount(val);
                    } else {
                        /* Always fails */
                        redisAssertWithInfo(c,sortval,sop->type == REDIS_SORT_GET);
                    }
                }
            }
        }

		// �����������Ϊ�գ���ô������б���������ݿ�����������¼�
        if (outputlen) {
            setKey(c->db,storekey,sobj);
            notifyKeyspaceEvent(REDIS_NOTIFY_LIST,"sortstore",storekey,
                                c->db->id);
            server.dirty += outputlen;
		// ���������Ϊ�գ���ôֻҪɾ�� storekey �Ϳ����ˣ���Ϊû�н�����Ա���
        } else if (dbDelete(c->db,storekey)) {
            signalModifiedKey(c->db,storekey);
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",storekey,c->db->id);
            server.dirty++;
        }
        decrRefCount(sobj);
        addReplyLongLong(c,outputlen);
    }

    /* Cleanup */
    if (sortval->type == REDIS_LIST || sortval->type == REDIS_SET)
        for (j = 0; j < vectorlen; j++)
            decrRefCount(vector[j].obj);
    decrRefCount(sortval);
    listRelease(operations);
    for (j = 0; j < vectorlen; j++) {
        if (alpha && vector[j].u.cmpobj)
            decrRefCount(vector[j].u.cmpobj);
    }
    zfree(vector);
}
