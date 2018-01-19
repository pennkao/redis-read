/*
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
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

/*-----------------------------------------------------------------------------
 * Sorted set API
 *----------------------------------------------------------------------------*/

/* ZSETs are ordered sets using two data structures to hold the same elements
 * in order to get O(log(N)) INSERT and REMOVE operations into a sorted
 * data structure.
 *
 * ZSET ͬʱʹ���������ݽṹ������ͬһ��Ԫ�أ�
 * �Ӷ��ṩ O(log(N)) ���Ӷȵ��������ݽṹ�Ĳ�����Ƴ�������
 *
 * The elements are added to a hash table mapping Redis objects to scores.
 * At the same time the elements are added to a skip list mapping scores
 * to Redis objects (so objects are sorted by scores in this "view"). 
 *
 * ��ϣ�� Redis ����ӳ�䵽��ֵ�ϡ�
 * ����Ծ���򽫷�ֵӳ�䵽 Redis �����ϣ�
 * ����Ծ����ӽ�����������˵ Redis �����Ǹ��ݷ�ֵ������ġ�
 */

/* This skiplist implementation is almost a C translation of the original
 * algorithm described by William Pugh in "Skip Lists: A Probabilistic
 * Alternative to Balanced Trees", modified in three ways:
 *
 * Redis ����Ծ��ʵ�ֺ� William Pugh 
 * �ڡ�Skip Lists: A Probabilistic Alternative to Balanced Trees��������
 * ��������Ծ�������ͬ�����������������ط������޸ģ�
 *
 * a) this implementation allows for repeated scores.
 *    ���ʵ���������ظ��ķ�ֵ
 *
 * b) the comparison is not just by key (our 'score') but by satellite data.
 *    ��Ԫ�صıȶԲ���Ҫ�ȶ����ǵķ�ֵ����Ҫ�ȶ����ǵĶ���
 *
 * c) there is a back pointer, so it's a doubly linked list with the back
 * pointers being only at "level 1". This allows to traverse the list
 * from tail to head, useful for ZREVRANGE. 
 *    ÿ����Ծ��ڵ㶼����һ������ָ�룬
 *    �����������ִ���� ZREVRANGE ����������ʱ���ӱ�β���ͷ������Ծ��
 */

#include "redis.h"
#include <math.h>

static int zslLexValueGteMin(robj *value, zlexrangespec *spec);
static int zslLexValueLteMax(robj *value, zlexrangespec *spec);

/*
 * ����һ������Ϊ level ����Ծ��ڵ㣬
 * �����ڵ�ĳ�Ա��������Ϊ obj ����ֵ����Ϊ score ��
 *
 * ����ֵΪ�´�������Ծ��ڵ�
 *
 * T = O(1)
 */
zskiplistNode *zslCreateNode(int level, double score, robj *obj) {
    
    // ����ռ�
    zskiplistNode *zn = zmalloc(sizeof(*zn)+level*sizeof(struct zskiplistLevel));

    // ��������
    zn->score = score;
    zn->obj = obj;

    return zn;
}

/*
 * ����������һ���µ���Ծ��
 *
 * T = O(1)
 */
zskiplist *zslCreate(void) {
    int j;
    zskiplist *zsl;

    // ����ռ�
    zsl = zmalloc(sizeof(*zsl));

    // ���ø߶Ⱥ���ʼ����
    zsl->level = 1;
    zsl->length = 0;

    // ��ʼ����ͷ�ڵ�
    // T = O(1)
    zsl->header = zslCreateNode(ZSKIPLIST_MAXLEVEL,0,NULL);
    for (j = 0; j < ZSKIPLIST_MAXLEVEL; j++) {
        zsl->header->level[j].forward = NULL;
        zsl->header->level[j].span = 0;
    }
    zsl->header->backward = NULL;

    // ���ñ�β
    zsl->tail = NULL;

    return zsl;
}

/*
 * �ͷŸ�������Ծ��ڵ�
 *
 * T = O(1)
 */
void zslFreeNode(zskiplistNode *node) {

    decrRefCount(node->obj);

    zfree(node);
}

/*
 * �ͷŸ�����Ծ���Լ����е����нڵ�
 *
 * T = O(N)
 */
void zslFree(zskiplist *zsl) {

    zskiplistNode *node = zsl->header->level[0].forward, *next;

    // �ͷű�ͷ
    zfree(zsl->header);

    // �ͷű������нڵ�
    // T = O(N)
    while(node) {

        next = node->level[0].forward;

        zslFreeNode(node);

        node = next;
    }
    
    // �ͷ���Ծ��ṹ
    zfree(zsl);
}

/* Returns a random level for the new skiplist node we are going to create.
 *
 * ����һ�����ֵ����������Ծ��ڵ�Ĳ�����
 *
 * The return value of this function is between 1 and ZSKIPLIST_MAXLEVEL
 * (both inclusive), with a powerlaw-alike distribution where higher
 * levels are less likely to be returned. 
 *
 * ����ֵ��� 1 �� ZSKIPLIST_MAXLEVEL ֮�䣨���� ZSKIPLIST_MAXLEVEL����
 * ��������㷨��ʹ�õ��ݴζ��ɣ�Խ���ֵ���ɵļ���ԽС��
 *
 * T = O(N)
 */
int zslRandomLevel(void) {
    int level = 1;

    while ((random()&0xFFFF) < (ZSKIPLIST_P * 0xFFFF))
        level += 1;

    return (level<ZSKIPLIST_MAXLEVEL) ? level : ZSKIPLIST_MAXLEVEL;
}

/*
 * ����һ����ԱΪ obj ����ֵΪ score ���½ڵ㣬
 * ��������½ڵ���뵽��Ծ�� zsl �С�
 * 
 * �����ķ���ֵΪ�½ڵ㡣
 *
 * T_wrost = O(N^2), T_avg = O(N log N)
 */
zskiplistNode *zslInsert(zskiplist *zsl, double score, robj *obj) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned int rank[ZSKIPLIST_MAXLEVEL];
    int i, level;

    redisAssert(!isnan(score));

    // �ڸ�������ҽڵ�Ĳ���λ��
    // T_wrost = O(N^2), T_avg = O(N log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {

        /* store rank that is crossed to reach the insert position */
        // ��� i ���� zsl->level-1 ��
        // ��ô i �����ʼ rank ֵΪ i+1 ��� rank ֵ
        // ������� rank ֵһ����ۻ�
        // ���� rank[0] ��ֵ��һ�����½ڵ��ǰ�ýڵ����λ
        // rank[0] ���ں����Ϊ���� span ֵ�� rank ֵ�Ļ���
        rank[i] = i == (zsl->level-1) ? 0 : rank[i+1];

        // ����ǰ��ָ�������Ծ��
        // T_wrost = O(N^2), T_avg = O(N log N)
        while (x->level[i].forward &&
            (x->level[i].forward->score < score ||
                // �ȶԷ�ֵ
                (x->level[i].forward->score == score &&
                // �ȶԳ�Ա�� T = O(N)
                compareStringObjects(x->level[i].forward->obj,obj) < 0))) {

            // ��¼��;��Խ�˶��ٸ��ڵ�
            rank[i] += x->level[i].span;

            // �ƶ�����һָ��
            x = x->level[i].forward;
        }
        // ��¼��Ҫ���½ڵ������ӵĽڵ�
        update[i] = x;
    }

    /* we assume the key is not already inside, since we allow duplicated
     * scores, and the re-insertion of score and redis object should never
     * happen since the caller of zslInsert() should test in the hash table
     * if the element is already inside or not. 
     *
     * zslInsert() �ĵ����߻�ȷ��ͬ��ֵ��ͬ��Ա��Ԫ�ز�����֣�
     * �������ﲻ��Ҫ��һ�����м�飬����ֱ�Ӵ�����Ԫ�ء�
     */

    // ��ȡһ�����ֵ��Ϊ�½ڵ�Ĳ���
    // T = O(N)
    level = zslRandomLevel();

    // ����½ڵ�Ĳ����ȱ��������ڵ�Ĳ�����Ҫ��
    // ��ô��ʼ����ͷ�ڵ���δʹ�õĲ㣬�������Ǽ�¼�� update ������
    // ����Ҳָ���½ڵ�
    if (level > zsl->level) {

        // ��ʼ��δʹ�ò�
        // T = O(1)
        for (i = zsl->level; i < level; i++) {
            rank[i] = 0;
            update[i] = zsl->header;
            update[i]->level[i].span = zsl->length;
        }

        // ���±��нڵ�������
        zsl->level = level;
    }

    // �����½ڵ�
    x = zslCreateNode(level,score,obj);

    // ��ǰ���¼��ָ��ָ���½ڵ㣬������Ӧ������
    // T = O(1)
    for (i = 0; i < level; i++) {
        
        // �����½ڵ�� forward ָ��
        x->level[i].forward = update[i]->level[i].forward;
        
        // ����;��¼�ĸ����ڵ�� forward ָ��ָ���½ڵ�
        update[i]->level[i].forward = x;

        /* update span covered by update[i] as x is inserted here */
        // �����½ڵ��Խ�Ľڵ�����
        x->level[i].span = update[i]->level[i].span - (rank[0] - rank[i]);

        // �����½ڵ����֮����;�ڵ�� span ֵ
        // ���е� +1 ��������½ڵ�
        update[i]->level[i].span = (rank[0] - rank[i]) + 1;
    }

    /* increment span for untouched levels */
    // δ�Ӵ��Ľڵ�� span ֵҲ��Ҫ��һ����Щ�ڵ�ֱ�Ӵӱ�ͷָ���½ڵ�
    // T = O(1)
    for (i = level; i < zsl->level; i++) {
        update[i]->level[i].span++;
    }

    // �����½ڵ�ĺ���ָ��
    x->backward = (update[0] == zsl->header) ? NULL : update[0];
    if (x->level[0].forward)
        x->level[0].forward->backward = x;
    else
        zsl->tail = x;

    // ��Ծ��Ľڵ������һ
    zsl->length++;

    return x;
}

/* Internal function used by zslDelete, zslDeleteByScore and zslDeleteByRank 
 * 
 * �ڲ�ɾ��������
 * �� zslDelete �� zslDeleteRangeByScore �� zslDeleteByRank �Ⱥ������á�
 *
 * T = O(1)
 */
void zslDeleteNode(zskiplist *zsl, zskiplistNode *x, zskiplistNode **update) {
    int i;

    // �������кͱ�ɾ���ڵ� x �йصĽڵ��ָ�룬�������֮��Ĺ�ϵ
    // T = O(1)
    for (i = 0; i < zsl->level; i++) {
        if (update[i]->level[i].forward == x) {
            update[i]->level[i].span += x->level[i].span - 1;
            update[i]->level[i].forward = x->level[i].forward;
        } else {
            update[i]->level[i].span -= 1;
        }
    }

    // ���±�ɾ���ڵ� x ��ǰ���ͺ���ָ��
    if (x->level[0].forward) {
        x->level[0].forward->backward = x->backward;
    } else {
        zsl->tail = x->backward;
    }

    // ������Ծ����������ֻ�ڱ�ɾ���ڵ�����Ծ������ߵĽڵ�ʱ��ִ�У�
    // T = O(1)
    while(zsl->level > 1 && zsl->header->level[zsl->level-1].forward == NULL)
        zsl->level--;

    // ��Ծ��ڵ��������һ
    zsl->length--;
}

/* Delete an element with matching score/object from the skiplist. 
 *
 * ����Ծ�� zsl ��ɾ�����������ڵ� score ���Ҵ���ָ������ obj �Ľڵ㡣
 *
 * T_wrost = O(N^2), T_avg = O(N log N)
 */
int zslDelete(zskiplist *zsl, double score, robj *obj) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    int i;

    // ������Ծ������Ŀ��ڵ㣬����¼������;�ڵ�
    // T_wrost = O(N^2), T_avg = O(N log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {

        // ������Ծ��ĸ��Ӷ�Ϊ T_wrost = O(N), T_avg = O(log N)
        while (x->level[i].forward &&
            (x->level[i].forward->score < score ||
                // �ȶԷ�ֵ
                (x->level[i].forward->score == score &&
                // �ȶԶ���T = O(N)
                compareStringObjects(x->level[i].forward->obj,obj) < 0)))

            // ����ǰ��ָ���ƶ�
            x = x->level[i].forward;

        // ��¼��;�ڵ�
        update[i] = x;
    }

    /* We may have multiple elements with the same score, what we need
     * is to find the element with both the right score and object. 
     *
     * ����ҵ���Ԫ�� x ��ֻ�������ķ�ֵ�Ͷ�����ͬʱ���Ž���ɾ����
     */
    x = x->level[0].forward;
    if (x && score == x->score && equalStringObjects(x->obj,obj)) {
        // T = O(1)
        zslDeleteNode(zsl, x, update);
        // T = O(1)
        zslFreeNode(x);
        return 1;
    } else {
        return 0; /* not found */
    }

    return 0; /* not found */
}

/*
 * ������ֵ value �Ƿ���ڣ�����ڵ��ڣ���Χ spec �е� min �
 *
 * ���� 1 ��ʾ value ���ڵ��� min ����򷵻� 0 ��
 *
 * T = O(1)
 */
static int zslValueGteMin(double value, zrangespec *spec) {
    return spec->minex ? (value > spec->min) : (value >= spec->min);
}

/*
 * ������ֵ value �Ƿ�С�ڣ���С�ڵ��ڣ���Χ spec �е� max �
 *
 * ���� 1 ��ʾ value С�ڵ��� max ����򷵻� 0 ��
 *
 * T = O(1)
 */
static int zslValueLteMax(double value, zrangespec *spec) {
    return spec->maxex ? (value < spec->max) : (value <= spec->max);
}

/* Returns if there is a part of the zset is in range.
 *
 * ��������ķ�ֵ��Χ��������Ծ��ķ�ֵ��Χ֮�ڣ�
 * ��ô���� 1 �����򷵻� 0 ��
 *
 * T = O(1)
 */
int zslIsInRange(zskiplist *zsl, zrangespec *range) {
    zskiplistNode *x;

    /* Test for ranges that will always be empty. */
    // ���ų���Ϊ�յķ�Χֵ
    if (range->min > range->max ||
            (range->min == range->max && (range->minex || range->maxex)))
        return 0;

    // �������ֵ
    x = zsl->tail;
    if (x == NULL || !zslValueGteMin(x->score,range))
        return 0;

    // �����С��ֵ
    x = zsl->header->level[0].forward;
    if (x == NULL || !zslValueLteMax(x->score,range))
        return 0;

    return 1;
}

/* Find the first node that is contained in the specified range.
 *
 * ���� zsl �е�һ����ֵ���� range ��ָ����Χ�Ľڵ㡣
 * Returns NULL when no element is contained in the range.
 *
 * ��� zsl ��û�з��Ϸ�Χ�Ľڵ㣬���� NULL ��
 *
 * T_wrost = O(N), T_avg = O(log N)
 */
zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range) {
    zskiplistNode *x;
    int i;

    /* If everything is out of range, return early. */
    if (!zslIsInRange(zsl,range)) return NULL;

    // ������Ծ�����ҷ��Ϸ�Χ min ��Ľڵ�
    // T_wrost = O(N), T_avg = O(log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        /* Go forward while *OUT* of range. */
        while (x->level[i].forward &&
            !zslValueGteMin(x->level[i].forward->score,range))
                x = x->level[i].forward;
    }

    /* This is an inner range, so the next node cannot be NULL. */
    x = x->level[0].forward;
    redisAssert(x != NULL);

    /* Check if score <= max. */
    // ���ڵ��Ƿ���Ϸ�Χ�� max ��
    // T = O(1)
    if (!zslValueLteMax(x->score,range)) return NULL;
    return x;
}

/* Find the last node that is contained in the specified range.
 * Returns NULL when no element is contained in the range.
 *
 * ���� zsl �����һ����ֵ���� range ��ָ����Χ�Ľڵ㡣
 *
 * ��� zsl ��û�з��Ϸ�Χ�Ľڵ㣬���� NULL ��
 *
 * T_wrost = O(N), T_avg = O(log N)
 */
zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range) {
    zskiplistNode *x;
    int i;

    /* If everything is out of range, return early. */
    // ��ȷ����Ծ����������һ���ڵ���� range ָ���ķ�Χ��
    // ����ֱ��ʧ��
    // T = O(1)
    if (!zslIsInRange(zsl,range)) return NULL;

    // ������Ծ�����ҷ��Ϸ�Χ max ��Ľڵ�
    // T_wrost = O(N), T_avg = O(log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        /* Go forward while *IN* range. */
        while (x->level[i].forward &&
            zslValueLteMax(x->level[i].forward->score,range))
                x = x->level[i].forward;
    }

    /* This is an inner range, so this node cannot be NULL. */
    redisAssert(x != NULL);

    /* Check if score >= min. */
    // ���ڵ��Ƿ���Ϸ�Χ�� min ��
    // T = O(1)
    if (!zslValueGteMin(x->score,range)) return NULL;

    // ���ؽڵ�
    return x;
}

/* Delete all the elements with score between min and max from the skiplist.
 *
 * ɾ�����з�ֵ�ڸ�����Χ֮�ڵĽڵ㡣
 *
 * Min and max are inclusive, so a score >= min || score <= max is deleted.
 * 
 * min �� max �������ǰ����ڷ�Χ֮�ڵģ����Է�ֵ >= min �� <= max �Ľڵ㶼�ᱻɾ����
 *
 * Note that this function takes the reference to the hash table view of the
 * sorted set, in order to remove the elements from the hash table too.
 *
 * �ڵ㲻�������Ծ����ɾ�������һ����Ӧ���ֵ���ɾ����
 *
 * ����ֵΪ��ɾ���ڵ������
 *
 * T = O(N)
 */
unsigned long zslDeleteRangeByScore(zskiplist *zsl, zrangespec *range, dict *dict) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long removed = 0;
    int i;

    // ��¼���кͱ�ɾ���ڵ㣨�ǣ��йصĽڵ�
    // T_wrost = O(N) , T_avg = O(log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        while (x->level[i].forward && (range->minex ?
            x->level[i].forward->score <= range->min :
            x->level[i].forward->score < range->min))
                x = x->level[i].forward;
        update[i] = x;
    }

    /* Current node is the last with score < or <= min. */
    // ��λ��������Χ��ʼ�ĵ�һ���ڵ�
    x = x->level[0].forward;

    /* Delete nodes while in range. */
    // ɾ����Χ�е����нڵ�
    // T = O(N)
    while (x &&
           (range->maxex ? x->score < range->max : x->score <= range->max))
    {
        // ��¼�¸��ڵ��ָ��
        zskiplistNode *next = x->level[0].forward;
        zslDeleteNode(zsl,x,update);
        dictDelete(dict,x->obj);
        zslFreeNode(x);
        removed++;
        x = next;
    }
    return removed;
}

unsigned long zslDeleteRangeByLex(zskiplist *zsl, zlexrangespec *range, dict *dict) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long removed = 0;
    int i;


    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        while (x->level[i].forward &&
            !zslLexValueGteMin(x->level[i].forward->obj,range))
                x = x->level[i].forward;
        update[i] = x;
    }

    /* Current node is the last with score < or <= min. */
    x = x->level[0].forward;

    /* Delete nodes while in range. */
    while (x && zslLexValueLteMax(x->obj,range)) {
        zskiplistNode *next = x->level[0].forward;

        // ����Ծ����ɾ����ǰ�ڵ�
        zslDeleteNode(zsl,x,update);
        // ���ֵ���ɾ����ǰ�ڵ�
        dictDelete(dict,x->obj);
        // �ͷŵ�ǰ��Ծ��ڵ�Ľṹ
        zslFreeNode(x);

        // ����ɾ��������
        removed++;

        // ���������¸��ڵ�
        x = next;
    }

    // ���ر�ɾ���ڵ������
    return removed;
}

/* Delete all the elements with rank between start and end from the skiplist.
 *
 * ����Ծ����ɾ�����и�����λ�ڵĽڵ㡣
 *
 * Start and end are inclusive. Note that start and end need to be 1-based 
 *
 * start �� end ����λ�ö��ǰ������ڵġ�ע�����Ƕ����� 1 Ϊ��ʼֵ��
 *
 * �����ķ���ֵΪ��ɾ���ڵ��������
 *
 * T = O(N)
 */
unsigned long zslDeleteRangeByRank(zskiplist *zsl, unsigned int start, unsigned int end, dict *dict) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long traversed = 0, removed = 0;
    int i;

    // ����ǰ��ָ���ƶ���ָ����λ����ʼλ�ã�����¼������;ָ��
    // T_wrost = O(N) , T_avg = O(log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        while (x->level[i].forward && (traversed + x->level[i].span) < start) {
            traversed += x->level[i].span;
            x = x->level[i].forward;
        }
        update[i] = x;
    }

    // �ƶ�����λ����ʼ�ĵ�һ���ڵ�
    traversed++;
    x = x->level[0].forward;
    // ɾ�������ڸ�����λ��Χ�ڵĽڵ�
    // T = O(N)
    while (x && traversed <= end) {

        // ��¼��һ�ڵ��ָ��
        zskiplistNode *next = x->level[0].forward;

        // ����Ծ����ɾ���ڵ�
        zslDeleteNode(zsl,x,update);
        // ���ֵ���ɾ���ڵ�
        dictDelete(dict,x->obj);
        // �ͷŽڵ�ṹ
        zslFreeNode(x);

        // Ϊɾ����������һ
        removed++;

        // Ϊ��λ��������һ
        traversed++;

        // �����¸��ڵ�
        x = next;
    }

    // ���ر�ɾ���ڵ������
    return removed;
}

/* Find the rank for an element by both score and key.
 *
 * ���Ұ���������ֵ�ͳ�Ա����Ľڵ�����Ծ���е���λ��
 *
 * Returns 0 when the element cannot be found, rank otherwise.
 *
 * ���û�а���������ֵ�ͳ�Ա����Ľڵ㣬���� 0 �����򷵻���λ��
 *
 * Note that the rank is 1-based due to the span of zsl->header to the
 * first element. 
 *
 * ע�⣬��Ϊ��Ծ��ı�ͷҲ���������ڣ����Է��ص���λ�� 1 Ϊ��ʼֵ��
 *
 * T_wrost = O(N), T_avg = O(log N)
 */
unsigned long zslGetRank(zskiplist *zsl, double score, robj *o) {
    zskiplistNode *x;
    unsigned long rank = 0;
    int i;

    // ����������Ծ��
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {

        // �����ڵ㲢�Ա�Ԫ��
        while (x->level[i].forward &&
            (x->level[i].forward->score < score ||
                // �ȶԷ�ֵ
                (x->level[i].forward->score == score &&
                // �ȶԳ�Ա����
                compareStringObjects(x->level[i].forward->obj,o) <= 0))) {

            // �ۻ���Խ�Ľڵ�����
            rank += x->level[i].span;

            // ����ǰ��ָ�������Ծ��
            x = x->level[i].forward;
        }

        /* x might be equal to zsl->header, so test if obj is non-NULL */
        // ����ȷ��������ֵ��ȣ����ҳ�Ա����ҲҪ���
        // T = O(N)
        if (x->obj && equalStringObjects(x->obj,o)) {
            return rank;
        }
    }

    // û�ҵ�
    return 0;
}

/* Finds an element by its rank. The rank argument needs to be 1-based. 
 * 
 * ������λ����Ծ���в���Ԫ�ء���λ����ʼֵΪ 1 ��
 *
 * �ɹ����ҷ�����Ӧ����Ծ��ڵ㣬û�ҵ��򷵻� NULL ��
 *
 * T_wrost = O(N), T_avg = O(log N)
 */
zskiplistNode* zslGetElementByRank(zskiplist *zsl, unsigned long rank) {
    zskiplistNode *x;
    unsigned long traversed = 0;
    int i;

    // T_wrost = O(N), T_avg = O(log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {

        // ������Ծ���ۻ�Խ���Ľڵ�����
        while (x->level[i].forward && (traversed + x->level[i].span) <= rank)
        {
            traversed += x->level[i].span;
            x = x->level[i].forward;
        }

        // ���Խ���Ľڵ������Ѿ����� rank
        // ��ô˵���Ѿ�����Ҫ�ҵĽڵ�
        if (traversed == rank) {
            return x;
        }

    }

    // û�ҵ�Ŀ��ڵ�
    return NULL;
}

/* Populate the rangespec according to the objects min and max. 
 *
 * �� min �� max ���з��������������ֵ������ spec �С�
 *
 * �����ɹ����� REDIS_OK ������������ʧ�ܷ��� REDIS_ERR ��
 *
 * T = O(N)
 */
static int zslParseRange(robj *min, robj *max, zrangespec *spec) {
    char *eptr;

    // Ĭ��Ϊ������
    spec->minex = spec->maxex = 0;

    /* Parse the min-max interval. If one of the values is prefixed
     * by the "(" character, it's considered "open". For instance
     * ZRANGEBYSCORE zset (1.5 (2.5 will match min < x < max
     * ZRANGEBYSCORE zset 1.5 2.5 will instead match min <= x <= max */
    if (min->encoding == REDIS_ENCODING_INT) {
        // min ��ֵΪ������������
        spec->min = (long)min->ptr;
    } else {
        // min ����Ϊ�ַ��������� min ��ֵ����������
        if (((char*)min->ptr)[0] == '(') {
            // T = O(N)
            spec->min = strtod((char*)min->ptr+1,&eptr);
            if (eptr[0] != '\0' || isnan(spec->min)) return REDIS_ERR;
            spec->minex = 1;
        } else {
            // T = O(N)
            spec->min = strtod((char*)min->ptr,&eptr);
            if (eptr[0] != '\0' || isnan(spec->min)) return REDIS_ERR;
        }
    }

    if (max->encoding == REDIS_ENCODING_INT) {
        // max ��ֵΪ������������
        spec->max = (long)max->ptr;
    } else {
        // max ����Ϊ�ַ��������� max ��ֵ����������
        if (((char*)max->ptr)[0] == '(') {
            // T = O(N)
            spec->max = strtod((char*)max->ptr+1,&eptr);
            if (eptr[0] != '\0' || isnan(spec->max)) return REDIS_ERR;
            spec->maxex = 1;
        } else {
            // T = O(N)
            spec->max = strtod((char*)max->ptr,&eptr);
            if (eptr[0] != '\0' || isnan(spec->max)) return REDIS_ERR;
        }
    }

    return REDIS_OK;
}

/* ------------------------ Lexicographic ranges ---------------------------- */

/* Parse max or min argument of ZRANGEBYLEX.
  * (foo means foo (open interval)
  * [foo means foo (closed interval)
  * - means the min string possible
  * + means the max string possible
  *
  * If the string is valid the *dest pointer is set to the redis object
  * that will be used for the comparision, and ex will be set to 0 or 1
  * respectively if the item is exclusive or inclusive. REDIS_OK will be
  * returned.
  *
  * If the string is not a valid range REDIS_ERR is returned, and the value
  * of *dest and *ex is undefined. */
int zslParseLexRangeItem(robj *item, robj **dest, int *ex) {
    char *c = item->ptr;

    switch(c[0]) {
    case '+':
        if (c[1] != '\0') return REDIS_ERR;
        *ex = 0;
        *dest = shared.maxstring;
        incrRefCount(shared.maxstring);
        return REDIS_OK;
    case '-':
        if (c[1] != '\0') return REDIS_ERR;
        *ex = 0;
        *dest = shared.minstring;
        incrRefCount(shared.minstring);
        return REDIS_OK;
    case '(':
        *ex = 1;
        *dest = createStringObject(c+1,sdslen(c)-1);
        return REDIS_OK;
    case '[':
        *ex = 0;
        *dest = createStringObject(c+1,sdslen(c)-1);
        return REDIS_OK;
    default:
        return REDIS_ERR;
    }
}

/* Populate the rangespec according to the objects min and max.
 *
 * Return REDIS_OK on success. On error REDIS_ERR is returned.
 * When OK is returned the structure must be freed with zslFreeLexRange(),
 * otherwise no release is needed. */
static int zslParseLexRange(robj *min, robj *max, zlexrangespec *spec) {
    /* The range can't be valid if objects are integer encoded.
     * Every item must start with ( or [. */
    if (min->encoding == REDIS_ENCODING_INT ||
        max->encoding == REDIS_ENCODING_INT) return REDIS_ERR;

    spec->min = spec->max = NULL;
    if (zslParseLexRangeItem(min, &spec->min, &spec->minex) == REDIS_ERR ||
        zslParseLexRangeItem(max, &spec->max, &spec->maxex) == REDIS_ERR) {
        if (spec->min) decrRefCount(spec->min);
        if (spec->max) decrRefCount(spec->max);
        return REDIS_ERR;
    } else {
        return REDIS_OK;
    }
}

/* Free a lex range structure, must be called only after zelParseLexRange()
 * populated the structure with success (REDIS_OK returned). */
void zslFreeLexRange(zlexrangespec *spec) {
    decrRefCount(spec->min);
    decrRefCount(spec->max);
}

/* This is just a wrapper to compareStringObjects() that is able to
 * handle shared.minstring and shared.maxstring as the equivalent of
 * -inf and +inf for strings */
int compareStringObjectsForLexRange(robj *a, robj *b) {
    if (a == b) return 0; /* This makes sure that we handle inf,inf and
                             -inf,-inf ASAP. One special case less. */
    if (a == shared.minstring || b == shared.maxstring) return -1;
    if (a == shared.maxstring || b == shared.minstring) return 1;
    return compareStringObjects(a,b);
}

static int zslLexValueGteMin(robj *value, zlexrangespec *spec) {
    return spec->minex ?
        (compareStringObjectsForLexRange(value,spec->min) > 0) :
        (compareStringObjectsForLexRange(value,spec->min) >= 0);
}

static int zslLexValueLteMax(robj *value, zlexrangespec *spec) {
    return spec->maxex ?
        (compareStringObjectsForLexRange(value,spec->max) < 0) :
        (compareStringObjectsForLexRange(value,spec->max) <= 0);
}

/* Returns if there is a part of the zset is in the lex range. */
int zslIsInLexRange(zskiplist *zsl, zlexrangespec *range) {
    zskiplistNode *x;

    /* Test for ranges that will always be empty. */
    if (compareStringObjectsForLexRange(range->min,range->max) > 1 ||
            (compareStringObjects(range->min,range->max) == 0 &&
            (range->minex || range->maxex)))
        return 0;
    x = zsl->tail;
    if (x == NULL || !zslLexValueGteMin(x->obj,range))
        return 0;
    x = zsl->header->level[0].forward;
    if (x == NULL || !zslLexValueLteMax(x->obj,range))
        return 0;
    return 1;
}

/* Find the first node that is contained in the specified lex range.
 * Returns NULL when no element is contained in the range. */
zskiplistNode *zslFirstInLexRange(zskiplist *zsl, zlexrangespec *range) {
    zskiplistNode *x;
    int i;

    /* If everything is out of range, return early. */
    if (!zslIsInLexRange(zsl,range)) return NULL;

    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        /* Go forward while *OUT* of range. */
        while (x->level[i].forward &&
            !zslLexValueGteMin(x->level[i].forward->obj,range))
                x = x->level[i].forward;
    }

    /* This is an inner range, so the next node cannot be NULL. */
    x = x->level[0].forward;
    redisAssert(x != NULL);

    /* Check if score <= max. */
    if (!zslLexValueLteMax(x->obj,range)) return NULL;
    return x;
}

/* Find the last node that is contained in the specified range.
 * Returns NULL when no element is contained in the range. */
zskiplistNode *zslLastInLexRange(zskiplist *zsl, zlexrangespec *range) {
    zskiplistNode *x;
    int i;

    /* If everything is out of range, return early. */
    if (!zslIsInLexRange(zsl,range)) return NULL;

    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        /* Go forward while *IN* range. */
        while (x->level[i].forward &&
            zslLexValueLteMax(x->level[i].forward->obj,range))
                x = x->level[i].forward;
    }

    /* This is an inner range, so this node cannot be NULL. */
    redisAssert(x != NULL);

    /* Check if score >= min. */
    if (!zslLexValueGteMin(x->obj,range)) return NULL;
    return x;
}

/*-----------------------------------------------------------------------------
 * Ziplist-backed sorted set API
 *----------------------------------------------------------------------------*/

/*
 * ȡ�� sptr ָ��ڵ�����������򼯺�Ԫ�صķ�ֵ
 */
double zzlGetScore(unsigned char *sptr) {
    unsigned char *vstr;
    unsigned int vlen;
    long long vlong;
    char buf[128];
    double score;

    redisAssert(sptr != NULL);
    // ȡ���ڵ�ֵ
    redisAssert(ziplistGet(sptr,&vstr,&vlen,&vlong));

    if (vstr) {
        // �ַ���ת double
        memcpy(buf,vstr,vlen);
        buf[vlen] = '\0';
        score = strtod(buf,NULL);
    } else {
        // double ֵ
        score = vlong;
    }

    return score;
}

/* Return a ziplist element as a Redis string object.
 * This simple abstraction can be used to simplifies some code at the
 * cost of some performance. */
robj *ziplistGetObject(unsigned char *sptr) {
    unsigned char *vstr;
    unsigned int vlen;
    long long vlong;

    redisAssert(sptr != NULL);
    redisAssert(ziplistGet(sptr,&vstr,&vlen,&vlong));

    if (vstr) {
        return createStringObject((char*)vstr,vlen);
    } else {
        return createStringObjectFromLongLong(vlong);
    }
}

/* Compare element in sorted set with given element. 
 *
 * �� eptr �е�Ԫ�غ� cstr ���жԱȡ�
 *
 * ��ȷ��� 0 ��
 * ����Ȳ��� eptr ���ַ����� cstr ��ʱ��������������
 * ����Ȳ��� eptr ���ַ����� cstr Сʱ�����ظ�������
 */
int zzlCompareElements(unsigned char *eptr, unsigned char *cstr, unsigned int clen) {
    unsigned char *vstr;
    unsigned int vlen;
    long long vlong;
    unsigned char vbuf[32];
    int minlen, cmp;

    // ȡ���ڵ��е��ַ���ֵ���Լ����ĳ���
    redisAssert(ziplistGet(eptr,&vstr,&vlen,&vlong));
    if (vstr == NULL) {
        /* Store string representation of long long in buf. */
        vlen = ll2string((char*)vbuf,sizeof(vbuf),vlong);
        vstr = vbuf;
    }

    // �Ա�
    minlen = (vlen < clen) ? vlen : clen;
    cmp = memcmp(vstr,cstr,minlen);
    if (cmp == 0) return vlen-clen;
    return cmp;
}

/*
 * ������Ծ�������Ԫ������
 */
unsigned int zzlLength(unsigned char *zl) {
    return ziplistLen(zl)/2;
}

/* Move to next entry based on the values in eptr and sptr. Both are set to
 * NULL when there is no next entry. 
 *
 * ���� eptr �� sptr ���ƶ����Ƿֱ�ָ���¸���Ա���¸���ֵ��
 *
 * ��������Ѿ�û��Ԫ�أ���ô����ָ�붼����Ϊ NULL ��
 */
void zzlNext(unsigned char *zl, unsigned char **eptr, unsigned char **sptr) {
    unsigned char *_eptr, *_sptr;

    redisAssert(*eptr != NULL && *sptr != NULL);

    // ָ���¸���Ա
    _eptr = ziplistNext(zl,*sptr);
    if (_eptr != NULL) {
        // ָ���¸���ֵ
        _sptr = ziplistNext(zl,_eptr);
        redisAssert(_sptr != NULL);
    } else {
        /* No next entry. */
        _sptr = NULL;
    }

    *eptr = _eptr;
    *sptr = _sptr;
}

/* Move to the previous entry based on the values in eptr and sptr. Both are
 * set to NULL when there is no next entry. 
 *
 * ���� eptr �� sptr ��ֵ���ƶ�ָ��ָ��ǰһ���ڵ㡣
 *
 * eptr �� sptr �ᱣ���ƶ�֮�����ָ�롣
 *
 * ���ָ���ǰ���Ѿ�û�нڵ㣬��ô���� NULL ��
 */
void zzlPrev(unsigned char *zl, unsigned char **eptr, unsigned char **sptr) {
    unsigned char *_eptr, *_sptr;
    redisAssert(*eptr != NULL && *sptr != NULL);

    _sptr = ziplistPrev(zl,*eptr);
    if (_sptr != NULL) {
        _eptr = ziplistPrev(zl,_sptr);
        redisAssert(_eptr != NULL);
    } else {
        /* No previous entry. */
        _eptr = NULL;
    }

    *eptr = _eptr;
    *sptr = _sptr;
}

/* Returns if there is a part of the zset is in range. Should only be used
 * internally by zzlFirstInRange and zzlLastInRange. 
 *
 * ��������� ziplist ������һ���ڵ���� range ��ָ���ķ�Χ��
 * ��ô�������� 1 �����򷵻� 0 ��
 */
int zzlIsInRange(unsigned char *zl, zrangespec *range) {
    unsigned char *p;
    double score;

    /* Test for ranges that will always be empty. */
    if (range->min > range->max ||
            (range->min == range->max && (range->minex || range->maxex)))
        return 0;

    // ȡ�� ziplist �е�����ֵ������ range �����ֵ�Ա�
    p = ziplistIndex(zl,-1); /* Last score. */
    if (p == NULL) return 0; /* Empty sorted set */
    score = zzlGetScore(p);
    if (!zslValueGteMin(score,range))
        return 0;

    // ȡ�� ziplist �е���Сֵ������ range ����Сֵ���жԱ�
    p = ziplistIndex(zl,1); /* First score. */
    redisAssert(p != NULL);
    score = zzlGetScore(p);
    if (!zslValueLteMax(score,range))
        return 0;

    // ziplist ������һ���ڵ���Ϸ�Χ
    return 1;
}

/* Find pointer to the first element contained in the specified range.
 *
 * ���ص�һ�� score ֵ�ڸ�����Χ�ڵĽڵ�
 *
 * Returns NULL when no element is contained in the range. 
 * Returns NULL when no element is contained in the range.
 *
 * ���û�нڵ�� score ֵ�ڸ�����Χ������ NULL ��
 */
unsigned char *zzlFirstInRange(unsigned char *zl, zrangespec *range) {
    // �ӱ�ͷ��ʼ����
    unsigned char *eptr = ziplistIndex(zl,0), *sptr;
    double score;

    /* If everything is out of range, return early. */
    if (!zzlIsInRange(zl,range)) return NULL;

    // ��ֵ�� ziplist ���Ǵ�С�������е�
    // �ӱ�ͷ���β����
    while (eptr != NULL) {
        sptr = ziplistNext(zl,eptr);
        redisAssert(sptr != NULL);

        score = zzlGetScore(sptr);
        if (zslValueGteMin(score,range)) {
            /* Check if score <= max. */
            // ���ϵ�һ�����Ϸ�Χ�ķ�ֵ��
            // �������Ľڵ�ָ��
            if (zslValueLteMax(score,range))
                return eptr;
            return NULL;
        }

        /* Move to next element. */
        eptr = ziplistNext(zl,sptr);
    }

    return NULL;
}

/* Find pointer to the last element contained in the specified range.
 *
 * ���� score ֵ�ڸ�����Χ�ڵ����һ���ڵ�
 *
 * Returns NULL when no element is contained in the range. 
 *
 * û��Ԫ�ذ�����ʱ������ NULL
 */
unsigned char *zzlLastInRange(unsigned char *zl, zrangespec *range) {
    // �ӱ�β��ʼ����
    unsigned char *eptr = ziplistIndex(zl,-2), *sptr;
    double score;

    /* If everything is out of range, return early. */
    if (!zzlIsInRange(zl,range)) return NULL;

    // ������� ziplist ��ӱ�β����ͷ����
    while (eptr != NULL) {
        sptr = ziplistNext(zl,eptr);
        redisAssert(sptr != NULL);

        // ��ȡ�ڵ�� score ֵ
        score = zzlGetScore(sptr);
        if (zslValueLteMax(score,range)) {
            /* Check if score >= min. */
            if (zslValueGteMin(score,range))
                return eptr;
            return NULL;
        }

        /* Move to previous element by moving to the score of previous element.
         * When this returns NULL, we know there also is no element. */
        sptr = ziplistPrev(zl,eptr);
        if (sptr != NULL)
            redisAssert((eptr = ziplistPrev(zl,sptr)) != NULL);
        else
            eptr = NULL;
    }

    return NULL;
}

static int zzlLexValueGteMin(unsigned char *p, zlexrangespec *spec) {
    robj *value = ziplistGetObject(p);
    int res = zslLexValueGteMin(value,spec);
    decrRefCount(value);
    return res;
}

static int zzlLexValueLteMax(unsigned char *p, zlexrangespec *spec) {
    robj *value = ziplistGetObject(p);
    int res = zslLexValueLteMax(value,spec);
    decrRefCount(value);
    return res;
}

/* Returns if there is a part of the zset is in range. Should only be used
 * internally by zzlFirstInRange and zzlLastInRange. */
int zzlIsInLexRange(unsigned char *zl, zlexrangespec *range) {
    unsigned char *p;

    /* Test for ranges that will always be empty. */
    if (compareStringObjectsForLexRange(range->min,range->max) > 1 ||
            (compareStringObjects(range->min,range->max) == 0 &&
            (range->minex || range->maxex)))
        return 0;

    p = ziplistIndex(zl,-2); /* Last element. */
    if (p == NULL) return 0;
    if (!zzlLexValueGteMin(p,range))
        return 0;

    p = ziplistIndex(zl,0); /* First element. */
    redisAssert(p != NULL);
    if (!zzlLexValueLteMax(p,range))
        return 0;

    return 1;
}

/* Find pointer to the first element contained in the specified lex range.
 * Returns NULL when no element is contained in the range. */
unsigned char *zzlFirstInLexRange(unsigned char *zl, zlexrangespec *range) {
    unsigned char *eptr = ziplistIndex(zl,0), *sptr;

    /* If everything is out of range, return early. */
    if (!zzlIsInLexRange(zl,range)) return NULL;

    while (eptr != NULL) {
        if (zzlLexValueGteMin(eptr,range)) {
            /* Check if score <= max. */
            if (zzlLexValueLteMax(eptr,range))
                return eptr;
            return NULL;
        }

        /* Move to next element. */
        sptr = ziplistNext(zl,eptr); /* This element score. Skip it. */
        redisAssert(sptr != NULL);
        eptr = ziplistNext(zl,sptr); /* Next element. */
    }

    return NULL;
}

/* Find pointer to the last element contained in the specified lex range.
 * Returns NULL when no element is contained in the range. */
unsigned char *zzlLastInLexRange(unsigned char *zl, zlexrangespec *range) {
    unsigned char *eptr = ziplistIndex(zl,-2), *sptr;

    /* If everything is out of range, return early. */
    if (!zzlIsInLexRange(zl,range)) return NULL;

    while (eptr != NULL) {
        if (zzlLexValueLteMax(eptr,range)) {
            /* Check if score >= min. */
            // �ҵ����һ�����Ϸ�Χ��ֵ
            // ��������ָ��
            if (zzlLexValueGteMin(eptr,range))
                return eptr;
            return NULL;
        }

        /* Move to previous element by moving to the score of previous element.
         * When this returns NULL, we know there also is no element. */
        sptr = ziplistPrev(zl,eptr);
        if (sptr != NULL)
            redisAssert((eptr = ziplistPrev(zl,sptr)) != NULL);
        else
            eptr = NULL;
    }

    return NULL;
}

/*
 * �� ziplist ��������򼯺��в��� ele ��Ա���������ķ�ֵ���浽 score ��
 *
 * Ѱ�ҳɹ�����ָ���Ա ele ��ָ�룬����ʧ�ܷ��� NULL ��
 */
unsigned char *zzlFind(unsigned char *zl, robj *ele, double *score) {

    // ��λ���׸�Ԫ��
    unsigned char *eptr = ziplistIndex(zl,0), *sptr;

    // �����Ա
    ele = getDecodedObject(ele);

    // �������� ziplist ������Ԫ�أ�ȷ�ϳ�Ա���ڣ�����ȡ�����ķ�ֵ��
    while (eptr != NULL) {
        // ָ���ֵ
        sptr = ziplistNext(zl,eptr);
        redisAssertWithInfo(NULL,ele,sptr != NULL);

        // �ȶԳ�Ա
        if (ziplistCompare(eptr,ele->ptr,sdslen(ele->ptr))) {
            /* Matching element, pull out score. */
            // ��Աƥ�䣬ȡ����ֵ
            if (score != NULL) *score = zzlGetScore(sptr);
            decrRefCount(ele);
            return eptr;
        }

        /* Move to next element. */
        eptr = ziplistNext(zl,sptr);
    }

    decrRefCount(ele);
    
    // û���ҵ�
    return NULL;
}

/* Delete (element,score) pair from ziplist. Use local copy of eptr because we
 * don't want to modify the one given as argument. 
 *
 * �� ziplist ��ɾ�� eptr ��ָ�������򼯺�Ԫ�أ�������Ա�ͷ�ֵ��
 */
unsigned char *zzlDelete(unsigned char *zl, unsigned char *eptr) {
    unsigned char *p = eptr;

    /* TODO: add function to ziplist API to delete N elements from offset. */
    zl = ziplistDelete(zl,&p);
    zl = ziplistDelete(zl,&p);
    return zl;
}

/*
 * �����и�����Ա�ͷ�ֵ���½ڵ���뵽 eptr ��ָ��Ľڵ��ǰ�棬
 * ��� eptr Ϊ NULL ����ô���½ڵ���뵽 ziplist ��ĩ�ˡ�
 *
 * �������ز���������֮��� ziplist
 */
unsigned char *zzlInsertAt(unsigned char *zl, unsigned char *eptr, robj *ele, double score) {
    unsigned char *sptr;
    char scorebuf[128];
    int scorelen;
    size_t offset;

    // �����ֵ���ֽڳ���
    redisAssertWithInfo(NULL,ele,sdsEncodedObject(ele));
    scorelen = d2string(scorebuf,sizeof(scorebuf),score);

    // ���뵽��β�����߿ձ�
    if (eptr == NULL) {
        // | member-1 | score-1 | member-2 | score-2 | ... | member-N | score-N |
        // ������Ԫ��
        zl = ziplistPush(zl,ele->ptr,sdslen(ele->ptr),ZIPLIST_TAIL);
        // �������ֵ
        zl = ziplistPush(zl,(unsigned char*)scorebuf,scorelen,ZIPLIST_TAIL);

    // ���뵽ĳ���ڵ��ǰ��
    } else {
        /* Keep offset relative to zl, as it might be re-allocated. */
        // �����Ա
        offset = eptr-zl;
        zl = ziplistInsert(zl,eptr,ele->ptr,sdslen(ele->ptr));
        eptr = zl+offset;

        /* Insert score after the element. */
        // ����ֵ�����ڳ�Ա֮��
        redisAssertWithInfo(NULL,ele,(sptr = ziplistNext(zl,eptr)) != NULL);
        zl = ziplistInsert(zl,sptr,(unsigned char*)scorebuf,scorelen);
    }

    return zl;
}

/* Insert (element,score) pair in ziplist. 
 *
 * �� ele ��Ա�����ķ�ֵ score ��ӵ� ziplist ����
 *
 * ziplist ��ĸ����ڵ㰴 score ֵ��С��������
 *
 * This function assumes the element is not yet present in the list. 
 *
 * ����������� elem ������������
 */
unsigned char *zzlInsert(unsigned char *zl, robj *ele, double score) {

    // ָ�� ziplist ��һ���ڵ㣨Ҳ�������򼯵� member ��
    unsigned char *eptr = ziplistIndex(zl,0), *sptr;
    double s;

    // ����ֵ
    ele = getDecodedObject(ele);

    // �������� ziplist
    while (eptr != NULL) {

        // ȡ����ֵ
        sptr = ziplistNext(zl,eptr);
        redisAssertWithInfo(NULL,ele,sptr != NULL);
        s = zzlGetScore(sptr);

        if (s > score) {
            /* First element with score larger than score for element to be
             * inserted. This means we should take its spot in the list to
             * maintain ordering. */
            // ������һ�� score ֵ������ score ��Ľڵ�
            // ���½ڵ����������ڵ��ǰ�棬
            // �ýڵ��� ziplist ����� score ��С��������
            zl = zzlInsertAt(zl,eptr,ele,score);
            break;
        } else if (s == score) {
            /* Ensure lexicographical ordering for elements. */
            // ������� score �ͽڵ�� score ��ͬ
            // ��ô���� member ���ַ���λ���������½ڵ�Ĳ���λ��
            if (zzlCompareElements(eptr,ele->ptr,sdslen(ele->ptr)) > 0) {
                zl = zzlInsertAt(zl,eptr,ele,score);
                break;
            }
        }

        /* Move to next element. */
        // ���� score �Ƚڵ�� score ֵҪ��
        // �ƶ�����һ���ڵ�
        eptr = ziplistNext(zl,sptr);
    }

    /* Push on tail of list when it was not yet inserted. */
    if (eptr == NULL)
        zl = zzlInsertAt(zl,NULL,ele,score);

    decrRefCount(ele);
    return zl;
}

/*
 * ɾ�� ziplist �з�ֵ��ָ����Χ�ڵ�Ԫ��
 *
 * deleted ��Ϊ NULL ʱ����ɾ�����֮�󣬽���ɾ��Ԫ�ص��������浽 *deleted �С�
 */
unsigned char *zzlDeleteRangeByScore(unsigned char *zl, zrangespec *range, unsigned long *deleted) {
    unsigned char *eptr, *sptr;
    double score;
    unsigned long num = 0;

    if (deleted != NULL) *deleted = 0;

    // ָ�� ziplist �е�һ�����Ϸ�Χ�Ľڵ�
    eptr = zzlFirstInRange(zl,range);
    if (eptr == NULL) return zl;

    /* When the tail of the ziplist is deleted, eptr will point to the sentinel
     * byte and ziplistNext will return NULL. */
    // һֱɾ���ڵ㣬ֱ���������ڷ�Χ�ڵ�ֵΪֹ
    // �ڵ��е�ֵ���������
    while ((sptr = ziplistNext(zl,eptr)) != NULL) {
        score = zzlGetScore(sptr);
        if (zslValueLteMax(score,range)) {
            /* Delete both the element and the score. */
            zl = ziplistDelete(zl,&eptr);
            zl = ziplistDelete(zl,&eptr);
            num++;
        } else {
            /* No longer in range. */
            break;
        }
    }

    if (deleted != NULL) *deleted = num;
    return zl;
}

unsigned char *zzlDeleteRangeByLex(unsigned char *zl, zlexrangespec *range, unsigned long *deleted) {
    unsigned char *eptr, *sptr;
    unsigned long num = 0;

    if (deleted != NULL) *deleted = 0;

    eptr = zzlFirstInLexRange(zl,range);
    if (eptr == NULL) return zl;

    /* When the tail of the ziplist is deleted, eptr will point to the sentinel
     * byte and ziplistNext will return NULL. */
    while ((sptr = ziplistNext(zl,eptr)) != NULL) {
        if (zzlLexValueLteMax(eptr,range)) {
            /* Delete both the element and the score. */
            zl = ziplistDelete(zl,&eptr);
            zl = ziplistDelete(zl,&eptr);
            num++;
        } else {
            /* No longer in range. */
            break;
        }
    }

    if (deleted != NULL) *deleted = num;

    return zl;
}

/* Delete all the elements with rank between start and end from the skiplist.
 *
 * ɾ�� ziplist �������ڸ�����λ��Χ�ڵ�Ԫ�ء�
 *
 * Start and end are inclusive. Note that start and end need to be 1-based 
 *
 * start �� end �������ǰ������ڵġ��������Ƕ��� 1 Ϊ��ʼֵ��
 *
 * ��� deleted ��Ϊ NULL ����ô��ɾ���������֮�󣬽�ɾ��Ԫ�ص��������浽 *deleted ��
 */
unsigned char *zzlDeleteRangeByRank(unsigned char *zl, unsigned int start, unsigned int end, unsigned long *deleted) {
    unsigned int num = (end-start)+1;

    if (deleted) *deleted = num;

    // ÿ��Ԫ��ռ�������ڵ㣬����ɾ������ʵλ��Ҫ���� 2 
    // ������Ϊ ziplist �������� 0 Ϊ��ʼֵ���� zzl ����ʼֵΪ 1 ��
    // ������Ҫ start - 1 
    zl = ziplistDeleteRange(zl,2*(start-1),2*num);

    return zl;
}

/*-----------------------------------------------------------------------------
 * Common sorted set API
 *----------------------------------------------------------------------------*/

unsigned int zsetLength(robj *zobj) {

    int length = -1;

    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        length = zzlLength(zobj->ptr);

    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        length = ((zset*)zobj->ptr)->zsl->length;

    } else {
        redisPanic("Unknown sorted set encoding");
    }

    return length;
}

/*
 * ����Ծ����� zobj �ĵײ����ת��Ϊ encoding ��
 */
void zsetConvert(robj *zobj, int encoding) {
    zset *zs;
    zskiplistNode *node, *next;
    robj *ele;
    double score;

    if (zobj->encoding == encoding) return;

    // �� ZIPLIST ����ת��Ϊ SKIPLIST ����
    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl = zobj->ptr;
        unsigned char *eptr, *sptr;
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;

        if (encoding != REDIS_ENCODING_SKIPLIST)
            redisPanic("Unknown target encoding");

        // �������򼯺Ͻṹ
        zs = zmalloc(sizeof(*zs));
        // �ֵ�
        zs->dict = dictCreate(&zsetDictType,NULL);
        // ��Ծ��
        zs->zsl = zslCreate();

        // ���򼯺��� ziplist �е����У�
        //
        // | member-1 | score-1 | member-2 | score-2 | ... |
        //
        // ָ�� ziplist �е��׸��ڵ㣨������Ԫ�س�Ա��
        eptr = ziplistIndex(zl,0);
        redisAssertWithInfo(NULL,zobj,eptr != NULL);
        // ָ�� ziplist �еĵڶ����ڵ㣨������Ԫ�ط�ֵ��
        sptr = ziplistNext(zl,eptr);
        redisAssertWithInfo(NULL,zobj,sptr != NULL);

        // �������� ziplist �ڵ㣬����Ԫ�صĳ�Ա�ͷ�ֵ��ӵ����򼯺���
        while (eptr != NULL) {
            
            // ȡ����ֵ
            score = zzlGetScore(sptr);

            // ȡ����Ա
            redisAssertWithInfo(NULL,zobj,ziplistGet(eptr,&vstr,&vlen,&vlong));
            if (vstr == NULL)
                ele = createStringObjectFromLongLong(vlong);
            else
                ele = createStringObject((char*)vstr,vlen);

            /* Has incremented refcount since it was just created. */
            // ����Ա�ͷ�ֵ�ֱ��������Ծ����ֵ���
            node = zslInsert(zs->zsl,score,ele);
            redisAssertWithInfo(NULL,zobj,dictAdd(zs->dict,ele,&node->score) == DICT_OK);
            incrRefCount(ele); /* Added to dictionary. */

            // �ƶ�ָ�룬ָ���¸�Ԫ��
            zzlNext(zl,&eptr,&sptr);
        }

        // �ͷ�ԭ���� ziplist
        zfree(zobj->ptr);

        // ���¶����ֵ���Լ����뷽ʽ
        zobj->ptr = zs;
        zobj->encoding = REDIS_ENCODING_SKIPLIST;

    // �� SKIPLIST ת��Ϊ ZIPLIST ����
    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {

        // �µ� ziplist
        unsigned char *zl = ziplistNew();

        if (encoding != REDIS_ENCODING_ZIPLIST)
            redisPanic("Unknown target encoding");

        /* Approach similar to zslFree(), since we want to free the skiplist at
         * the same time as creating the ziplist. */
        // ָ����Ծ��
        zs = zobj->ptr;

        // ���ͷ��ֵ䣬��Ϊֻ��Ҫ��Ծ��Ϳ��Ա����������򼯺���
        dictRelease(zs->dict);

        // ָ����Ծ���׸��ڵ�
        node = zs->zsl->header->level[0].forward;

        // �ͷ���Ծ���ͷ
        zfree(zs->zsl->header);
        zfree(zs->zsl);

        // ������Ծ��ȡ�������Ԫ�أ�����������ӵ� ziplist
        while (node) {

            // ȡ��������ֵ����
            ele = getDecodedObject(node->obj);

            // ���Ԫ�ص� ziplist
            zl = zzlInsertAt(zl,NULL,ele,node->score);
            decrRefCount(ele);

            // ������Ծ��ĵ� 0 ��ǰ��
            next = node->level[0].forward;
            zslFreeNode(node);
            node = next;
        }

        // �ͷ���Ծ��
        zfree(zs);

        // ���¶����ֵ���Լ�����ı��뷽ʽ
        zobj->ptr = zl;
        zobj->encoding = REDIS_ENCODING_ZIPLIST;
    } else {
        redisPanic("Unknown sorted set encoding");
    }
}

/*-----------------------------------------------------------------------------
 * Sorted set commands 
 *----------------------------------------------------------------------------*/

/* This generic command implements both ZADD and ZINCRBY. */
void zaddGenericCommand(redisClient *c, int incr) {

    static char *nanerr = "resulting score is not a number (NaN)";

    robj *key = c->argv[1];
    robj *ele;
    robj *zobj;
    robj *curobj;
    double score = 0, *scores = NULL, curscore = 0.0;
    int j, elements = (c->argc-2)/2;
    int added = 0, updated = 0;

    // ����� score - member ���������ǳɶԳ��ֵ�
    if (c->argc % 2) {
        addReply(c,shared.syntaxerr);
        return;
    }

    /* Start parsing all the scores, we need to emit any syntax error
     * before executing additions to the sorted set, as the command should
     * either execute fully or nothing at all. */
    // ȡ����������� score ��ֵ
    scores = zmalloc(sizeof(double)*elements);
    for (j = 0; j < elements; j++) {
        if (getDoubleFromObjectOrReply(c,c->argv[2+j*2],&scores[j],NULL)
            != REDIS_OK) goto cleanup;
    }

    /* Lookup the key and create the sorted set if does not exist. */
    // ȡ�����򼯺϶���
    zobj = lookupKeyWrite(c->db,key);
    if (zobj == NULL) {
        // ���򼯺ϲ����ڣ����������򼯺�
        if (server.zset_max_ziplist_entries == 0 ||
            server.zset_max_ziplist_value < sdslen(c->argv[3]->ptr))
        {
            zobj = createZsetObject();
        } else {
            zobj = createZsetZiplistObject();
        }
        // �����������ݿ�
        dbAdd(c->db,key,zobj);
    } else {
        // ������ڣ��������
        if (zobj->type != REDIS_ZSET) {
            addReply(c,shared.wrongtypeerr);
            goto cleanup;
        }
    }

    // ��������Ԫ��
    for (j = 0; j < elements; j++) {
        score = scores[j];

        // ���򼯺�Ϊ ziplist ����
        if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
            unsigned char *eptr;

            /* Prefer non-encoded element when dealing with ziplists. */
            // ���ҳ�Ա
            ele = c->argv[3+j*2];
            if ((eptr = zzlFind(zobj->ptr,ele,&curscore)) != NULL) {

                // ��Ա�Ѵ���

                // ZINCRYBY ����ʱʹ��
                if (incr) {
                    score += curscore;
                    if (isnan(score)) {
                        addReplyError(c,nanerr);
                        goto cleanup;
                    }
                }

                /* Remove and re-insert when score changed. */
                // ִ�� ZINCRYBY ����ʱ��
                // �����û�ͨ�� ZADD �޸ĳ�Ա�ķ�ֵʱִ��
                if (score != curscore) {
                    // ɾ������Ԫ��
                    zobj->ptr = zzlDelete(zobj->ptr,eptr);
                    // ���²���Ԫ��
                    zobj->ptr = zzlInsert(zobj->ptr,ele,score);
                    // ������
                    server.dirty++;
                    updated++;
                }
            } else {
                /* Optimize: check if the element is too large or the list
                 * becomes too long *before* executing zzlInsert. */
                // Ԫ�ز����ڣ�ֱ�����
                zobj->ptr = zzlInsert(zobj->ptr,ele,score);

                // �鿴Ԫ�ص�������
                // ���Ƿ���Ҫ�� ZIPLIST ����ת��Ϊ���򼯺�
                if (zzlLength(zobj->ptr) > server.zset_max_ziplist_entries)
                    zsetConvert(zobj,REDIS_ENCODING_SKIPLIST);

                // �鿴�����Ԫ�صĳ���
                // ���Ƿ���Ҫ�� ZIPLIST ����ת��Ϊ���򼯺�
                if (sdslen(ele->ptr) > server.zset_max_ziplist_value)
                    zsetConvert(zobj,REDIS_ENCODING_SKIPLIST);

                server.dirty++;
                added++;
            }

        // ���򼯺�Ϊ SKIPLIST ����
        } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
            zset *zs = zobj->ptr;
            zskiplistNode *znode;
            dictEntry *de;

            // �������
            ele = c->argv[3+j*2] = tryObjectEncoding(c->argv[3+j*2]);

            // �鿴��Ա�Ƿ����
            de = dictFind(zs->dict,ele);
            if (de != NULL) {

                // ��Ա����

                // ȡ����Ա
                curobj = dictGetKey(de);
                // ȡ����ֵ
                curscore = *(double*)dictGetVal(de);

                // ZINCRYBY ʱִ��
                if (incr) {
                    score += curscore;
                    if (isnan(score)) {
                        addReplyError(c,nanerr);
                        /* Don't need to check if the sorted set is empty
                         * because we know it has at least one element. */
                        goto cleanup;
                    }
                }

                /* Remove and re-insert when score changed. We can safely
                 * delete the key object from the skiplist, since the
                 * dictionary still has a reference to it. */
                // ִ�� ZINCRYBY ����ʱ��
                // �����û�ͨ�� ZADD �޸ĳ�Ա�ķ�ֵʱִ��
                if (score != curscore) {
                    // ɾ��ԭ��Ԫ��
                    redisAssertWithInfo(c,curobj,zslDelete(zs->zsl,curscore,curobj));

                    // ���²���Ԫ��
                    znode = zslInsert(zs->zsl,score,curobj);
                    incrRefCount(curobj); /* Re-inserted in skiplist. */

                    // �����ֵ�ķ�ֵָ��
                    dictGetVal(de) = &znode->score; /* Update score ptr. */

                    server.dirty++;
                    updated++;
                }
            } else {

                // Ԫ�ز����ڣ�ֱ����ӵ���Ծ��
                znode = zslInsert(zs->zsl,score,ele);
                incrRefCount(ele); /* Inserted in skiplist. */

                // ��Ԫ�ع������ֵ�
                redisAssertWithInfo(c,NULL,dictAdd(zs->dict,ele,&znode->score) == DICT_OK);
                incrRefCount(ele); /* Added to dictionary. */

                server.dirty++;
                added++;
            }
        } else {
            redisPanic("Unknown sorted set encoding");
        }
    }

    if (incr) /* ZINCRBY */
        addReplyDouble(c,score);
    else /* ZADD */
        addReplyLongLong(c,added);

cleanup:
    zfree(scores);
    if (added || updated) {
        signalModifiedKey(c->db,key);
        notifyKeyspaceEvent(REDIS_NOTIFY_ZSET,
            incr ? "zincr" : "zadd", key, c->db->id);
    }
}

void zaddCommand(redisClient *c) {
    zaddGenericCommand(c,0);
}

void zincrbyCommand(redisClient *c) {
    zaddGenericCommand(c,1);
}

void zremCommand(redisClient *c) {
    robj *key = c->argv[1];
    robj *zobj;
    int deleted = 0, keyremoved = 0, j;

    // ȡ�����򼯺϶���
    if ((zobj = lookupKeyWriteOrReply(c,key,shared.czero)) == NULL ||
        checkType(c,zobj,REDIS_ZSET)) return;

    // �� ziplist ��ɾ��
    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *eptr;

        // ������������Ԫ��
        for (j = 2; j < c->argc; j++) {
            // ���Ԫ���� ziplist �д��ڵĻ�
            if ((eptr = zzlFind(zobj->ptr,c->argv[j],NULL)) != NULL) {
                // Ԫ�ش���ʱ��ɾ������������һ
                deleted++;
                // ��ôɾ������
                zobj->ptr = zzlDelete(zobj->ptr,eptr);
                
                // ziplist ����գ������򼯺ϴ����ݿ���ɾ��
                if (zzlLength(zobj->ptr) == 0) {
                    dbDelete(c->db,key);
                    break;
                }
            }
        }

    // ����Ծ����ֵ���ɾ��
    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        zset *zs = zobj->ptr;
        dictEntry *de;
        double score;

        // ������������Ԫ��
        for (j = 2; j < c->argc; j++) {

            // ����Ԫ��
            de = dictFind(zs->dict,c->argv[j]);

            if (de != NULL) {
                // Ԫ�ش���ʱ��ɾ������������һ
                deleted++;

                /* Delete from the skiplist */
                // ��Ԫ�ش���Ծ����ɾ��
                score = *(double*)dictGetVal(de);
                redisAssertWithInfo(c,c->argv[j],zslDelete(zs->zsl,score,c->argv[j]));

                /* Delete from the hash table */
                // ��Ԫ�ش��ֵ���ɾ��
                dictDelete(zs->dict,c->argv[j]);

                // ����Ƿ���Ҫ��С�ֵ�
                if (htNeedsResize(zs->dict)) dictResize(zs->dict);

                // �ֵ��ѱ���գ����򼯺��Ѿ�����գ����������ݿ���ɾ��
                if (dictSize(zs->dict) == 0) {
                    dbDelete(c->db,key);
                    break;
                }
            }
        }
    } else {
        redisPanic("Unknown sorted set encoding");
    }

    // ���������һ��Ԫ�ر�ɾ���Ļ�����ôִ�����´���
    if (deleted) {

        notifyKeyspaceEvent(REDIS_NOTIFY_ZSET,"zrem",key,c->db->id);

        if (keyremoved)
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",key,c->db->id);

        signalModifiedKey(c->db,key);

        server.dirty += deleted;
    }

    // �ظ���ɾ��Ԫ�ص�����
    addReplyLongLong(c,deleted);
}

/* Implements ZREMRANGEBYRANK, ZREMRANGEBYSCORE, ZREMRANGEBYLEX commands. */
#define ZRANGE_RANK 0
#define ZRANGE_SCORE 1
#define ZRANGE_LEX 2
void zremrangeGenericCommand(redisClient *c, int rangetype) {
    robj *key = c->argv[1];
    robj *zobj;
    int keyremoved = 0;
    unsigned long deleted;
    zrangespec range;
    zlexrangespec lexrange;
    long start, end, llen;

    /* Step 1: Parse the range. */
    if (rangetype == ZRANGE_RANK) {
        if ((getLongFromObjectOrReply(c,c->argv[2],&start,NULL) != REDIS_OK) ||
            (getLongFromObjectOrReply(c,c->argv[3],&end,NULL) != REDIS_OK))
            return;
    } else if (rangetype == ZRANGE_SCORE) {
        if (zslParseRange(c->argv[2],c->argv[3],&range) != REDIS_OK) {
            addReplyError(c,"min or max is not a float");
            return;
        }
    } else if (rangetype == ZRANGE_LEX) {
        if (zslParseLexRange(c->argv[2],c->argv[3],&lexrange) != REDIS_OK) {
            addReplyError(c,"min or max not valid string range item");
            return;
        }
    }

    /* Step 2: Lookup & range sanity checks if needed. */
    if ((zobj = lookupKeyWriteOrReply(c,key,shared.czero)) == NULL ||
        checkType(c,zobj,REDIS_ZSET)) goto cleanup;

    if (rangetype == ZRANGE_RANK) {
        /* Sanitize indexes. */
        llen = zsetLength(zobj);
        if (start < 0) start = llen+start;
        if (end < 0) end = llen+end;
        if (start < 0) start = 0;

        /* Invariant: start >= 0, so this test will be true when end < 0.
         * The range is empty when start > end or start >= length. */
        if (start > end || start >= llen) {
            addReply(c,shared.czero);
            goto cleanup;
        }
        if (end >= llen) end = llen-1;
    }

    /* Step 3: Perform the range deletion operation. */
    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        switch(rangetype) {
        case ZRANGE_RANK:
            zobj->ptr = zzlDeleteRangeByRank(zobj->ptr,start+1,end+1,&deleted);
            break;
        case ZRANGE_SCORE:
            zobj->ptr = zzlDeleteRangeByScore(zobj->ptr,&range,&deleted);
            break;
        case ZRANGE_LEX:
            zobj->ptr = zzlDeleteRangeByLex(zobj->ptr,&lexrange,&deleted);
            break;
        }
        if (zzlLength(zobj->ptr) == 0) {
            dbDelete(c->db,key);
            keyremoved = 1;
        }

    // ����Ծ����ֵ���ɾ��
    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        zset *zs = zobj->ptr;
        switch(rangetype) {
        case ZRANGE_RANK:
            deleted = zslDeleteRangeByRank(zs->zsl,start+1,end+1,zs->dict);
            break;
        case ZRANGE_SCORE:
            deleted = zslDeleteRangeByScore(zs->zsl,&range,zs->dict);
            break;
        case ZRANGE_LEX:
            deleted = zslDeleteRangeByLex(zs->zsl,&lexrange,zs->dict);
            break;
        }
        if (htNeedsResize(zs->dict)) dictResize(zs->dict);

        // ��������գ������ݿ���ɾ��
        if (dictSize(zs->dict) == 0) {
            dbDelete(c->db,key);
            keyremoved = 1;
        }
    } else {
        redisPanic("Unknown sorted set encoding");
    }

    /* Step 4: Notifications and reply. */
    if (deleted) {
        char *event[3] = {"zremrangebyrank","zremrangebyscore","zremrangebylex"};
        signalModifiedKey(c->db,key);
        notifyKeyspaceEvent(REDIS_NOTIFY_ZSET,event[rangetype],key,c->db->id);
        if (keyremoved)
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",key,c->db->id);
    }

    server.dirty += deleted;

    // �ظ���ɾ��Ԫ�صĸ���
    addReplyLongLong(c,deleted);

cleanup:
    if (rangetype == ZRANGE_LEX) zslFreeLexRange(&lexrange);
}

void zremrangebyrankCommand(redisClient *c) {
    zremrangeGenericCommand(c,ZRANGE_RANK);
}

void zremrangebyscoreCommand(redisClient *c) {
    zremrangeGenericCommand(c,ZRANGE_SCORE);
}

void zremrangebylexCommand(redisClient *c) {
    zremrangeGenericCommand(c,ZRANGE_LEX);
}

/*
 * ��̬���ϵ��������ɵ������ϻ������򼯺�
 */
typedef struct {

    // �������Ķ���
    robj *subject;

    // ���������
    int type; /* Set, sorted set */

    // ����
    int encoding;

    // Ȩ��
    double weight;

    union {
        /* Set iterators. */
        // ���ϵ�����
        union _iterset {
            // intset ������
            struct {
                // �������� intset
                intset *is;
                // ��ǰ�ڵ�����
                int ii;
            } is;
            // �ֵ������
            struct {
                // ���������ֵ�
                dict *dict;
                // �ֵ������
                dictIterator *di;
                // ��ǰ�ֵ�ڵ�
                dictEntry *de;
            } ht;
        } set;

        /* Sorted set iterators. */
        // ���򼯺ϵ�����
        union _iterzset {
            // ziplist ������
            struct {
                // �������� ziplist
                unsigned char *zl;
                // ��ǰ��Աָ��͵�ǰ��ֵָ��
                unsigned char *eptr, *sptr;
            } zl;
            // zset ������
            struct {
                // �������� zset
                zset *zs;
                // ��ǰ��Ծ��ڵ�
                zskiplistNode *node;
            } sl;
        } zset;
    } iter;
} zsetopsrc;


/* Use dirty flags for pointers that need to be cleaned up in the next
 * iteration over the zsetopval. 
 *
 * DIRTY �������ڱ�ʶ���´ε���֮ǰҪ��������
 *
 * The dirty flag for the long long value is special,
 * since long long values don't need cleanup. 
 *
 * �� DIRTY ���������� long long ֵʱ����ֵ����Ҫ������
 *
 * Instead, it means that we already checked that "ell" holds a long long,
 * or tried to convert another representation into a long long value.
 *
 * ��Ϊ����ʾ ell �Ѿ�����һ�� long long ֵ��
 * �����Ѿ���һ������ת��Ϊ long long ֵ��
 *
 * When this was successful, OPVAL_VALID_LL is set as well. 
 *
 * ��ת���ɹ�ʱ�� OPVAL_VALID_LL �����á�
 */
#define OPVAL_DIRTY_ROBJ 1
#define OPVAL_DIRTY_LL 2
#define OPVAL_VALID_LL 4

/* Store value retrieved from the iterator. 
 *
 * ���ڱ���ӵ�������ȡ�õ�ֵ�Ľṹ
 */
typedef struct {

    int flags;

    unsigned char _buf[32]; /* Private buffer. */

    // �������ڱ��� member �ļ�������
    robj *ele;
    unsigned char *estr;
    unsigned int elen;
    long long ell;

    // ��ֵ
    double score;

} zsetopval;

// ���ͱ���
typedef union _iterset iterset;
typedef union _iterzset iterzset;

/*
 * ��ʼ��������
 */
void zuiInitIterator(zsetopsrc *op) {

    // ��������Ϊ�գ��޶���
    if (op->subject == NULL)
        return;

    // ��������
    if (op->type == REDIS_SET) {

        iterset *it = &op->iter.set;

        // ���� intset
        if (op->encoding == REDIS_ENCODING_INTSET) {
            it->is.is = op->subject->ptr;
            it->is.ii = 0;

        // �����ֵ�
        } else if (op->encoding == REDIS_ENCODING_HT) {
            it->ht.dict = op->subject->ptr;
            it->ht.di = dictGetIterator(op->subject->ptr);
            it->ht.de = dictNext(it->ht.di);

        } else {
            redisPanic("Unknown set encoding");
        }

    // �������򼯺�
    } else if (op->type == REDIS_ZSET) {

        iterzset *it = &op->iter.zset;

        // ���� ziplist
        if (op->encoding == REDIS_ENCODING_ZIPLIST) {
            it->zl.zl = op->subject->ptr;
            it->zl.eptr = ziplistIndex(it->zl.zl,0);
            if (it->zl.eptr != NULL) {
                it->zl.sptr = ziplistNext(it->zl.zl,it->zl.eptr);
                redisAssert(it->zl.sptr != NULL);
            }

        // ������Ծ��
        } else if (op->encoding == REDIS_ENCODING_SKIPLIST) {
            it->sl.zs = op->subject->ptr;
            it->sl.node = it->sl.zs->zsl->header->level[0].forward;

        } else {
            redisPanic("Unknown sorted set encoding");
        }

    // δ֪��������
    } else {
        redisPanic("Unsupported type");
    }
}

/*
 * ��յ�����
 */
void zuiClearIterator(zsetopsrc *op) {

    if (op->subject == NULL)
        return;

    if (op->type == REDIS_SET) {

        iterset *it = &op->iter.set;

        if (op->encoding == REDIS_ENCODING_INTSET) {
            REDIS_NOTUSED(it); /* skip */

        } else if (op->encoding == REDIS_ENCODING_HT) {
            dictReleaseIterator(it->ht.di);

        } else {
            redisPanic("Unknown set encoding");
        }

    } else if (op->type == REDIS_ZSET) {

        iterzset *it = &op->iter.zset;

        if (op->encoding == REDIS_ENCODING_ZIPLIST) {
            REDIS_NOTUSED(it); /* skip */

        } else if (op->encoding == REDIS_ENCODING_SKIPLIST) {
            REDIS_NOTUSED(it); /* skip */

        } else {
            redisPanic("Unknown sorted set encoding");
        }
    } else {
        redisPanic("Unsupported type");
    }
}

/*
 * �������ڱ�������Ԫ�صĳ���
 */
int zuiLength(zsetopsrc *op) {

    if (op->subject == NULL)
        return 0;

    if (op->type == REDIS_SET) {
        if (op->encoding == REDIS_ENCODING_INTSET) {
            return intsetLen(op->subject->ptr);
        } else if (op->encoding == REDIS_ENCODING_HT) {
            dict *ht = op->subject->ptr;
            return dictSize(ht);
        } else {
            redisPanic("Unknown set encoding");
        }

    } else if (op->type == REDIS_ZSET) {

        if (op->encoding == REDIS_ENCODING_ZIPLIST) {
            return zzlLength(op->subject->ptr);
        } else if (op->encoding == REDIS_ENCODING_SKIPLIST) {
            zset *zs = op->subject->ptr;
            return zs->zsl->length;
        } else {
            redisPanic("Unknown sorted set encoding");
        }

    } else {
        redisPanic("Unsupported type");
    }
}

/* Check if the current value is valid. If so, store it in the passed structure
 * and move to the next element. 
 *
 * ����������ǰָ���Ԫ���Ƿ�Ϸ�������ǵĻ����������浽����� val �ṹ�У�
 * Ȼ�󽫵������ĵ�ǰָ��ָ����һԪ�أ��������� 1 ��
 *
 * If not valid, this means we have reached the
 * end of the structure and can abort. 
 *
 * �����ǰָ���Ԫ�ز��Ϸ�����ô˵�������Ѿ�������ϣ��������� 0 ��
 */
int zuiNext(zsetopsrc *op, zsetopval *val) {

    if (op->subject == NULL)
        return 0;

    // ���ϴεĶ����������
    if (val->flags & OPVAL_DIRTY_ROBJ)
        decrRefCount(val->ele);

    // ���� val �ṹ
    memset(val,0,sizeof(zsetopval));

    // ��������
    if (op->type == REDIS_SET) {

        iterset *it = &op->iter.set;

        // ziplist ����ļ���
        if (op->encoding == REDIS_ENCODING_INTSET) {
            int64_t ell;

            // ȡ����Ա
            if (!intsetGet(it->is.is,it->is.ii,&ell))
                return 0;
            val->ell = ell;
            // ��ֵĬ��Ϊ 1.0
            val->score = 1.0;

            /* Move to next element. */
            it->is.ii++;

        // �ֵ����ļ���
        } else if (op->encoding == REDIS_ENCODING_HT) {

            // ��Ϊ�գ�
            if (it->ht.de == NULL)
                return 0;

            // ȡ����Ա
            val->ele = dictGetKey(it->ht.de);
            // ��ֵĬ��Ϊ 1.0
            val->score = 1.0;

            /* Move to next element. */
            it->ht.de = dictNext(it->ht.di);
        } else {
            redisPanic("Unknown set encoding");
        }

    // �������򼯺�
    } else if (op->type == REDIS_ZSET) {

        iterzset *it = &op->iter.zset;

        // ziplist ��������򼯺�
        if (op->encoding == REDIS_ENCODING_ZIPLIST) {

            /* No need to check both, but better be explicit. */
            // Ϊ�գ�
            if (it->zl.eptr == NULL || it->zl.sptr == NULL)
                return 0;

            // ȡ����Ա
            redisAssert(ziplistGet(it->zl.eptr,&val->estr,&val->elen,&val->ell));
            // ȡ����ֵ
            val->score = zzlGetScore(it->zl.sptr);

            /* Move to next element. */
            zzlNext(it->zl.zl,&it->zl.eptr,&it->zl.sptr);

        // SKIPLIST ��������򼯺�
        } else if (op->encoding == REDIS_ENCODING_SKIPLIST) {

            if (it->sl.node == NULL)
                return 0;

            val->ele = it->sl.node->obj;
            val->score = it->sl.node->score;

            /* Move to next element. */
            it->sl.node = it->sl.node->level[0].forward;
        } else {
            redisPanic("Unknown sorted set encoding");
        }
    } else {
        redisPanic("Unsupported type");
    }

    return 1;
}

/*
 * �� val ��ȡ�� long long ֵ��
 */
int zuiLongLongFromValue(zsetopval *val) {

    if (!(val->flags & OPVAL_DIRTY_LL)) {

        // �򿪱�ʶ DIRTY LL
        val->flags |= OPVAL_DIRTY_LL;

        // �Ӷ�����ȡֵ
        if (val->ele != NULL) {
            // �� INT ������ַ�����ȡ������
            if (val->ele->encoding == REDIS_ENCODING_INT) {
                val->ell = (long)val->ele->ptr;
                val->flags |= OPVAL_VALID_LL;
            // ��δ������ַ�����ת������
            } else if (sdsEncodedObject(val->ele)) {
                if (string2ll(val->ele->ptr,sdslen(val->ele->ptr),&val->ell))
                    val->flags |= OPVAL_VALID_LL;

            } else {
                redisPanic("Unsupported element encoding");
            }

        // �� ziplist �ڵ���ȡֵ
        } else if (val->estr != NULL) {
            // ���ڵ�ֵ��һ���ַ�����ת��Ϊ����
            if (string2ll((char*)val->estr,val->elen,&val->ell))
                val->flags |= OPVAL_VALID_LL;

        } else {
            /* The long long was already set, flag as valid. */
            // ���Ǵ� VALID LL ��ʶ
            val->flags |= OPVAL_VALID_LL;
        }
    }

    // ��� VALID LL ��ʶ�Ƿ��Ѵ�
    return val->flags & OPVAL_VALID_LL;
}

/*
 * ���� val �е�ֵ����������
 */
robj *zuiObjectFromValue(zsetopval *val) {

    if (val->ele == NULL) {

        // �� long long ֵ�д�������
        if (val->estr != NULL) {
            val->ele = createStringObject((char*)val->estr,val->elen);
        } else {
            val->ele = createStringObjectFromLongLong(val->ell);
        }

        // �� ROBJ ��ʶ
        val->flags |= OPVAL_DIRTY_ROBJ;
    }

    // ����ֵ����
    return val->ele;
}

/*
 * �� val ��ȡ���ַ���
 */
int zuiBufferFromValue(zsetopval *val) {

    if (val->estr == NULL) {
        if (val->ele != NULL) {
            if (val->ele->encoding == REDIS_ENCODING_INT) {
                val->elen = ll2string((char*)val->_buf,sizeof(val->_buf),(long)val->ele->ptr);
                val->estr = val->_buf;
            } else if (sdsEncodedObject(val->ele)) {
                val->elen = sdslen(val->ele->ptr);
                val->estr = val->ele->ptr;
            } else {
                redisPanic("Unsupported element encoding");
            }
        } else {
            val->elen = ll2string((char*)val->_buf,sizeof(val->_buf),val->ell);
            val->estr = val->_buf;
        }
    }

    return 1;
}

/* Find value pointed to by val in the source pointer to by op. When found,
 * return 1 and store its score in target. Return 0 otherwise. 
 *
 * �ڵ�����ָ���Ķ����в��Ҹ���Ԫ��
 *
 * �ҵ����� 1 �����򷵻� 0 ��
 */
int zuiFind(zsetopsrc *op, zsetopval *val, double *score) {

    if (op->subject == NULL)
        return 0;

    // ����
    if (op->type == REDIS_SET) {
        // ��ԱΪ��������ֵΪ 1.0
        if (op->encoding == REDIS_ENCODING_INTSET) {
            if (zuiLongLongFromValue(val) &&
                intsetFind(op->subject->ptr,val->ell))
            {
                *score = 1.0;
                return 1;
            } else {
                return 0;
            }

        // ��ΪΪ���󣬷�ֵΪ 1.0
        } else if (op->encoding == REDIS_ENCODING_HT) {
            dict *ht = op->subject->ptr;
            zuiObjectFromValue(val);
            if (dictFind(ht,val->ele) != NULL) {
                *score = 1.0;
                return 1;
            } else {
                return 0;
            }
        } else {
            redisPanic("Unknown set encoding");
        }

    // ���򼯺�
    } else if (op->type == REDIS_ZSET) {
        // ȡ������
        zuiObjectFromValue(val);

        // ziplist
        if (op->encoding == REDIS_ENCODING_ZIPLIST) {

            // ȡ����Ա�ͷ�ֵ
            if (zzlFind(op->subject->ptr,val->ele,score) != NULL) {
                /* Score is already set by zzlFind. */
                return 1;
            } else {
                return 0;
            }

        // SKIPLIST ����
        } else if (op->encoding == REDIS_ENCODING_SKIPLIST) {
            zset *zs = op->subject->ptr;
            dictEntry *de;

            // ���ֵ��в��ҳ�Ա����
            if ((de = dictFind(zs->dict,val->ele)) != NULL) {
                // ȡ����ֵ
                *score = *(double*)dictGetVal(de);
                return 1;
            } else {
                return 0;
            }
        } else {
            redisPanic("Unknown sorted set encoding");
        }
    } else {
        redisPanic("Unsupported type");
    }
}

/*
 * �Ա���������������Ļ���
 */
int zuiCompareByCardinality(const void *s1, const void *s2) {
    return zuiLength((zsetopsrc*)s1) - zuiLength((zsetopsrc*)s2);
}

#define REDIS_AGGR_SUM 1
#define REDIS_AGGR_MIN 2
#define REDIS_AGGR_MAX 3
#define zunionInterDictValue(_e) (dictGetVal(_e) == NULL ? 1.0 : *(double*)dictGetVal(_e))

/*
 * ���� aggregate ������ֵ��������ζ� *target �� val ���оۺϼ��㡣
 */
inline static void zunionInterAggregate(double *target, double val, int aggregate) {

    // ���
    if (aggregate == REDIS_AGGR_SUM) {
        *target = *target + val;
        /* The result of adding two doubles is NaN when one variable
         * is +inf and the other is -inf. When these numbers are added,
         * we maintain the convention of the result being 0.0. */
        // ����Ƿ����
        if (isnan(*target)) *target = 0.0;

    // ������С��
    } else if (aggregate == REDIS_AGGR_MIN) {
        *target = val < *target ? val : *target;

    // �����ߴ���
    } else if (aggregate == REDIS_AGGR_MAX) {
        *target = val > *target ? val : *target;

    } else {
        /* safety net */
        redisPanic("Unknown ZUNION/INTER aggregate type");
    }
}

void zunionInterGenericCommand(redisClient *c, robj *dstkey, int op) {
    int i, j;
    long setnum;
    int aggregate = REDIS_AGGR_SUM;
    zsetopsrc *src;
    zsetopval zval;
    robj *tmp;
    unsigned int maxelelen = 0;
    robj *dstobj;
    zset *dstzset;
    zskiplistNode *znode;
    int touched = 0;

    /* expect setnum input keys to be given */
    // ȡ��Ҫ��������򼯺ϵĸ��� setnum
    if ((getLongFromObjectOrReply(c, c->argv[2], &setnum, NULL) != REDIS_OK))
        return;

    if (setnum < 1) {
        addReplyError(c,
            "at least 1 input key is needed for ZUNIONSTORE/ZINTERSTORE");
        return;
    }

    /* test if the expected number of keys would overflow */
    // setnum �����ʹ���� key ��������ͬ������
    if (setnum > c->argc-3) {
        addReply(c,shared.syntaxerr);
        return;
    }

    /* read keys to be used for input */
    // Ϊÿ������ key ����һ��������
    src = zcalloc(sizeof(zsetopsrc) * setnum);
    for (i = 0, j = 3; i < setnum; i++, j++) {

        // ȡ�� key ����
        robj *obj = lookupKeyWrite(c->db,c->argv[j]);

        // ����������
        if (obj != NULL) {
            if (obj->type != REDIS_ZSET && obj->type != REDIS_SET) {
                zfree(src);
                addReply(c,shared.wrongtypeerr);
                return;
            }

            src[i].subject = obj;
            src[i].type = obj->type;
            src[i].encoding = obj->encoding;

        // �����ڵĶ�����Ϊ NULL
        } else {
            src[i].subject = NULL;
        }

        /* Default all weights to 1. */
        // Ĭ��Ȩ��Ϊ 1.0
        src[i].weight = 1.0;
    }

    /* parse optional extra arguments */
    // �����������ѡ����
    if (j < c->argc) {
        int remaining = c->argc - j;

        while (remaining) {
            if (remaining >= (setnum + 1) && !strcasecmp(c->argv[j]->ptr,"weights")) {
                j++; remaining--;
                // Ȩ�ز���
                for (i = 0; i < setnum; i++, j++, remaining--) {
                    if (getDoubleFromObjectOrReply(c,c->argv[j],&src[i].weight,
                            "weight value is not a float") != REDIS_OK)
                    {
                        zfree(src);
                        return;
                    }
                }

            } else if (remaining >= 2 && !strcasecmp(c->argv[j]->ptr,"aggregate")) {
                j++; remaining--;
                // �ۺϷ�ʽ
                if (!strcasecmp(c->argv[j]->ptr,"sum")) {
                    aggregate = REDIS_AGGR_SUM;
                } else if (!strcasecmp(c->argv[j]->ptr,"min")) {
                    aggregate = REDIS_AGGR_MIN;
                } else if (!strcasecmp(c->argv[j]->ptr,"max")) {
                    aggregate = REDIS_AGGR_MAX;
                } else {
                    zfree(src);
                    addReply(c,shared.syntaxerr);
                    return;
                }
                j++; remaining--;

            } else {
                zfree(src);
                addReply(c,shared.syntaxerr);
                return;
            }
        }
    }

    /* sort sets from the smallest to largest, this will improve our
     * algorithm's performance */
    // �����м��Ͻ��������Լ����㷨�ĳ�����
    qsort(src,setnum,sizeof(zsetopsrc),zuiCompareByCardinality);

    // �������������
    dstobj = createZsetObject();
    dstzset = dstobj->ptr;
    memset(&zval, 0, sizeof(zval));

    // ZINTERSTORE ����
    if (op == REDIS_OP_INTER) {

        /* Skip everything if the smallest input is empty. */
        // ֻ����ǿռ���
        if (zuiLength(&src[0]) > 0) {

            /* Precondition: as src[0] is non-empty and the inputs are ordered
             * by size, all src[i > 0] are non-empty too. */
            // ����������С�� src[0] ����
            zuiInitIterator(&src[0]);
            while (zuiNext(&src[0],&zval)) {
                double score, value;

                // �����Ȩ��ֵ
                score = src[0].weight * zval.score;
                if (isnan(score)) score = 0;

                // �� src[0] �����е�Ԫ�غ����������е�Ԫ������Ȩ�ۺϼ���
                for (j = 1; j < setnum; j++) {
                    /* It is not safe to access the zset we are
                     * iterating, so explicitly check for equal object. */
                    // �����ǰ�������� src[j] �Ķ���� src[0] �Ķ���һ����
                    // ��ô src[0] ���ֵ�Ԫ�ر�ȻҲ������ src[j]
                    // ��ô���ǿ���ֱ�Ӽ���ۺ�ֵ��
                    // ���ؽ��� zuiFind ȥȷ��Ԫ���Ƿ����
                    // ���������ĳ�� key ���������Σ�
                    // ������� key ���������뼯���л�����С�ļ���ʱ�����
                    if (src[j].subject == src[0].subject) {
                        value = zval.score*src[j].weight;
                        zunionInterAggregate(&score,value,aggregate);

                    // ����������������ҵ���ǰ��������Ԫ�صĻ�
                    // ��ô���оۺϼ���
                    } else if (zuiFind(&src[j],&zval,&value)) {
                        value *= src[j].weight;
                        zunionInterAggregate(&score,value,aggregate);

                    // �����ǰԪ��û������ĳ�����ϣ���ô���� for ѭ��
                    // �����¸�Ԫ��
                    } else {
                        break;
                    }
                }

                /* Only continue when present in every input. */
                // ֻ�ڽ���Ԫ�س���ʱ����ִ�����´���
                if (j == setnum) {
                    // ȡ��ֵ����
                    tmp = zuiObjectFromValue(&zval);
                    // ���뵽���򼯺���
                    znode = zslInsert(dstzset->zsl,score,tmp);
                    incrRefCount(tmp); /* added to skiplist */
                    // ���뵽�ֵ���
                    dictAdd(dstzset->dict,tmp,&znode->score);
                    incrRefCount(tmp); /* added to dictionary */

                    // �����ַ����������󳤶�
                    if (sdsEncodedObject(tmp)) {
                        if (sdslen(tmp->ptr) > maxelelen)
                            maxelelen = sdslen(tmp->ptr);
                    }
                }
            }
            zuiClearIterator(&src[0]);
        }

    // ZUNIONSTORE
    } else if (op == REDIS_OP_UNION) {

        // �����������뼯��
        for (i = 0; i < setnum; i++) {

            // �����ռ���
            if (zuiLength(&src[i]) == 0)
                continue;

            // �������м���Ԫ��
            zuiInitIterator(&src[i]);
            while (zuiNext(&src[i],&zval)) {
                double score, value;

                /* Skip an element that when already processed */
                // �����Ѵ���Ԫ��
                if (dictFind(dstzset->dict,zuiObjectFromValue(&zval)) != NULL)
                    continue;

                /* Initialize score */
                // ��ʼ����ֵ
                score = src[i].weight * zval.score;
                // ���ʱ��Ϊ 0
                if (isnan(score)) score = 0;

                /* We need to check only next sets to see if this element
                 * exists, since we process every element just one time so
                 * it can't exist in a previous set (otherwise it would be
                 * already processed). */
                for (j = (i+1); j < setnum; j++) {
                    /* It is not safe to access the zset we are
                     * iterating, so explicitly check for equal object. */
                    // ��ǰԪ�صļ��Ϻͱ���������һ��
                    // ����ͬһ��Ԫ�ر�Ȼ������ src[j] �� src[i]
                    // ����ֱ�Ӽ������ǵľۺ�ֵ
                    // ������ʹ�� zuiFind �����Ԫ���Ƿ����
                    if(src[j].subject == src[i].subject) {
                        value = zval.score*src[j].weight;
                        zunionInterAggregate(&score,value,aggregate);

                    // ����Ա�Ƿ����
                    } else if (zuiFind(&src[j],&zval,&value)) {
                        value *= src[j].weight;
                        zunionInterAggregate(&score,value,aggregate);
                    }
                }

                // ȡ����Ա
                tmp = zuiObjectFromValue(&zval);
                // ���벢��Ԫ�ص���Ծ��
                znode = zslInsert(dstzset->zsl,score,tmp);
                incrRefCount(zval.ele); /* added to skiplist */
                // ���Ԫ�ص��ֵ�
                dictAdd(dstzset->dict,tmp,&znode->score);
                incrRefCount(zval.ele); /* added to dictionary */

                // �����ַ�����󳤶�
                if (sdsEncodedObject(tmp)) {
                    if (sdslen(tmp->ptr) > maxelelen)
                        maxelelen = sdslen(tmp->ptr);
                }
            }
            zuiClearIterator(&src[i]);
        }
    } else {
        redisPanic("Unknown operator");
    }

    // ɾ���Ѵ��ڵ� dstkey ���ȴ��������¶��������
    if (dbDelete(c->db,dstkey)) {
        signalModifiedKey(c->db,dstkey);
        touched = 1;
        server.dirty++;
    }

    // ���������ϵĳ��Ȳ�Ϊ 0 
    if (dstzset->zsl->length) {
        /* Convert to ziplist when in limits. */
        // ���Ƿ���Ҫ�Խ�����Ͻ��б���ת��
        if (dstzset->zsl->length <= server.zset_max_ziplist_entries &&
            maxelelen <= server.zset_max_ziplist_value)
                zsetConvert(dstobj,REDIS_ENCODING_ZIPLIST);

        // ��������Ϲ��������ݿ�
        dbAdd(c->db,dstkey,dstobj);

        // �ظ�������ϵĳ���
        addReplyLongLong(c,zsetLength(dstobj));

        if (!touched) signalModifiedKey(c->db,dstkey);

        notifyKeyspaceEvent(REDIS_NOTIFY_ZSET,
            (op == REDIS_OP_UNION) ? "zunionstore" : "zinterstore",
            dstkey,c->db->id);

        server.dirty++;

    // �����Ϊ��
    } else {

        decrRefCount(dstobj);

        addReply(c,shared.czero);

        if (touched)
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",dstkey,c->db->id);
    }

    zfree(src);
}

void zunionstoreCommand(redisClient *c) {
    zunionInterGenericCommand(c,c->argv[1], REDIS_OP_UNION);
}

void zinterstoreCommand(redisClient *c) {
    zunionInterGenericCommand(c,c->argv[1], REDIS_OP_INTER);
}

void zrangeGenericCommand(redisClient *c, int reverse) {
    robj *key = c->argv[1];
    robj *zobj;
    int withscores = 0;
    long start;
    long end;
    int llen;
    int rangelen;

    // ȡ�� start �� end ����
    if ((getLongFromObjectOrReply(c, c->argv[2], &start, NULL) != REDIS_OK) ||
        (getLongFromObjectOrReply(c, c->argv[3], &end, NULL) != REDIS_OK)) return;

    // ȷ���Ƿ���ʾ��ֵ
    if (c->argc == 5 && !strcasecmp(c->argv[4]->ptr,"withscores")) {
        withscores = 1;
    } else if (c->argc >= 5) {
        addReply(c,shared.syntaxerr);
        return;
    }

    // ȡ�����򼯺϶���
    if ((zobj = lookupKeyReadOrReply(c,key,shared.emptymultibulk)) == NULL
         || checkType(c,zobj,REDIS_ZSET)) return;

    /* Sanitize indexes. */
    // ����������ת��Ϊ��������
    llen = zsetLength(zobj);
    if (start < 0) start = llen+start;
    if (end < 0) end = llen+end;
    if (start < 0) start = 0;

    /* Invariant: start >= 0, so this test will be true when end < 0.
     * The range is empty when start > end or start >= length. */
    // ����/��������
    if (start > end || start >= llen) {
        addReply(c,shared.emptymultibulk);
        return;
    }
    if (end >= llen) end = llen-1;
    rangelen = (end-start)+1;

    /* Return the result in form of a multi-bulk reply */
    addReplyMultiBulkLen(c, withscores ? (rangelen*2) : rangelen);

    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl = zobj->ptr;
        unsigned char *eptr, *sptr;
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;

        // ���������ķ���
        if (reverse)
            eptr = ziplistIndex(zl,-2-(2*start));
        else
            eptr = ziplistIndex(zl,2*start);

        redisAssertWithInfo(c,zobj,eptr != NULL);
        sptr = ziplistNext(zl,eptr);

        // ȡ��Ԫ��
        while (rangelen--) {
            redisAssertWithInfo(c,zobj,eptr != NULL && sptr != NULL);
            redisAssertWithInfo(c,zobj,ziplistGet(eptr,&vstr,&vlen,&vlong));
            if (vstr == NULL)
                addReplyBulkLongLong(c,vlong);
            else
                addReplyBulkCBuffer(c,vstr,vlen);

            if (withscores)
                addReplyDouble(c,zzlGetScore(sptr));

            if (reverse)
                zzlPrev(zl,&eptr,&sptr);
            else
                zzlNext(zl,&eptr,&sptr);
        }

    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        zset *zs = zobj->ptr;
        zskiplist *zsl = zs->zsl;
        zskiplistNode *ln;
        robj *ele;

        /* Check if starting point is trivial, before doing log(N) lookup. */
        // �����ķ���
        if (reverse) {
            ln = zsl->tail;
            if (start > 0)
                ln = zslGetElementByRank(zsl,llen-start);
        } else {
            ln = zsl->header->level[0].forward;
            if (start > 0)
                ln = zslGetElementByRank(zsl,start+1);
        }

        // ȡ��Ԫ��
        while(rangelen--) {
            redisAssertWithInfo(c,zobj,ln != NULL);
            ele = ln->obj;
            addReplyBulk(c,ele);
            if (withscores)
                addReplyDouble(c,ln->score);
            ln = reverse ? ln->backward : ln->level[0].forward;
        }
    } else {
        redisPanic("Unknown sorted set encoding");
    }
}

void zrangeCommand(redisClient *c) {
    zrangeGenericCommand(c,0);
}

void zrevrangeCommand(redisClient *c) {
    zrangeGenericCommand(c,1);
}

/* This command implements ZRANGEBYSCORE, ZREVRANGEBYSCORE. */
void genericZrangebyscoreCommand(redisClient *c, int reverse) {
    zrangespec range;
    robj *key = c->argv[1];
    robj *zobj;
    long offset = 0, limit = -1;
    int withscores = 0;
    unsigned long rangelen = 0;
    void *replylen = NULL;
    int minidx, maxidx;

    /* Parse the range arguments. */
    if (reverse) {
        /* Range is given as [max,min] */
        maxidx = 2; minidx = 3;
    } else {
        /* Range is given as [min,max] */
        minidx = 2; maxidx = 3;
    }

    // ���������뷶Χ
    if (zslParseRange(c->argv[minidx],c->argv[maxidx],&range) != REDIS_OK) {
        addReplyError(c,"min or max is not a float");
        return;
    }

    /* Parse optional extra arguments. Note that ZCOUNT will exactly have
     * 4 arguments, so we'll never enter the following code path. */
    // �����������ѡ����
    if (c->argc > 4) {
        int remaining = c->argc - 4;
        int pos = 4;

        while (remaining) {
            if (remaining >= 1 && !strcasecmp(c->argv[pos]->ptr,"withscores")) {
                pos++; remaining--;
                withscores = 1;
            } else if (remaining >= 3 && !strcasecmp(c->argv[pos]->ptr,"limit")) {
                if ((getLongFromObjectOrReply(c, c->argv[pos+1], &offset, NULL) != REDIS_OK) ||
                    (getLongFromObjectOrReply(c, c->argv[pos+2], &limit, NULL) != REDIS_OK)) return;
                pos += 3; remaining -= 3;
            } else {
                addReply(c,shared.syntaxerr);
                return;
            }
        }
    }

    /* Ok, lookup the key and get the range */
    // ȡ�����򼯺϶���
    if ((zobj = lookupKeyReadOrReply(c,key,shared.emptymultibulk)) == NULL ||
        checkType(c,zobj,REDIS_ZSET)) return;

    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl = zobj->ptr;
        unsigned char *eptr, *sptr;
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;
        double score;

        /* If reversed, get the last node in range as starting point. */
        // �����ķ���
        if (reverse) {
            eptr = zzlLastInRange(zl,&range);
        } else {
            eptr = zzlFirstInRange(zl,&range);
        }

        /* No "first" element in the specified interval. */
        // û��Ԫ����ָ����Χ֮��
        if (eptr == NULL) {
            addReply(c, shared.emptymultibulk);
            return;
        }

        /* Get score pointer for the first element. */
        redisAssertWithInfo(c,zobj,eptr != NULL);
        sptr = ziplistNext(zl,eptr);

        /* We don't know in advance how many matching elements there are in the
         * list, so we push this object that will represent the multi-bulk
         * length in the output buffer, and will "fix" it later */
        replylen = addDeferredMultiBulkLength(c);

        /* If there is an offset, just traverse the number of elements without
         * checking the score because that is done in the next loop. */
        // ���� offset ָ��������Ԫ��
        while (eptr && offset--) {
            if (reverse) {
                zzlPrev(zl,&eptr,&sptr);
            } else {
                zzlNext(zl,&eptr,&sptr);
            }
        }

        // ���������������ڷ�Χ�ڵ�Ԫ��
        while (eptr && limit--) {

            // ��ֵ
            score = zzlGetScore(sptr);

            /* Abort when the node is no longer in range. */
            // ����ֵ�Ƿ���Ϸ�Χ
            if (reverse) {
                if (!zslValueGteMin(score,&range)) break;
            } else {
                if (!zslValueLteMax(score,&range)) break;
            }

            /* We know the element exists, so ziplistGet should always succeed */
            redisAssertWithInfo(c,zobj,ziplistGet(eptr,&vstr,&vlen,&vlong));

            rangelen++;
            if (vstr == NULL) {
                addReplyBulkLongLong(c,vlong);
            } else {
                addReplyBulkCBuffer(c,vstr,vlen);
            }

            if (withscores) {
                addReplyDouble(c,score);
            }

            /* Move to next node */
            if (reverse) {
                zzlPrev(zl,&eptr,&sptr);
            } else {
                zzlNext(zl,&eptr,&sptr);
            }
        }

    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        zset *zs = zobj->ptr;
        zskiplist *zsl = zs->zsl;
        zskiplistNode *ln;

        /* If reversed, get the last node in range as starting point. */
        // ����
        if (reverse) {
            ln = zslLastInRange(zsl,&range);
        } else {
            ln = zslFirstInRange(zsl,&range);
        }

        /* No "first" element in the specified interval. */
        // û��ֵ��ָ����Χ֮��
        if (ln == NULL) {
            addReply(c, shared.emptymultibulk);
            return;
        }

        /* We don't know in advance how many matching elements there are in the
         * list, so we push this object that will represent the multi-bulk
         * length in the output buffer, and will "fix" it later */
        replylen = addDeferredMultiBulkLength(c);

        /* If there is an offset, just traverse the number of elements without
         * checking the score because that is done in the next loop. */
        // ���� offset ����ָ����Ԫ������
        while (ln && offset--) {
            if (reverse) {
                ln = ln->backward;
            } else {
                ln = ln->level[0].forward;
            }
        }

        // ���������������ڷ�Χ�ڵ�Ԫ��
        while (ln && limit--) {
            /* Abort when the node is no longer in range. */
            if (reverse) {
                if (!zslValueGteMin(ln->score,&range)) break;
            } else {
                if (!zslValueLteMax(ln->score,&range)) break;
            }

            rangelen++;
            addReplyBulk(c,ln->obj);

            if (withscores) {
                addReplyDouble(c,ln->score);
            }

            /* Move to next node */
            if (reverse) {
                ln = ln->backward;
            } else {
                ln = ln->level[0].forward;
            }
        }
    } else {
        redisPanic("Unknown sorted set encoding");
    }

    if (withscores) {
        rangelen *= 2;
    }

    setDeferredMultiBulkLength(c, replylen, rangelen);
}

void zrangebyscoreCommand(redisClient *c) {
    genericZrangebyscoreCommand(c,0);
}

void zrevrangebyscoreCommand(redisClient *c) {
    genericZrangebyscoreCommand(c,1);
}

void zcountCommand(redisClient *c) {
    robj *key = c->argv[1];
    robj *zobj;
    zrangespec range;
    int count = 0;

    /* Parse the range arguments */
    // ���������뷶Χ����
    if (zslParseRange(c->argv[2],c->argv[3],&range) != REDIS_OK) {
        addReplyError(c,"min or max is not a float");
        return;
    }

    /* Lookup the sorted set */
    // ȡ�����򼯺�
    if ((zobj = lookupKeyReadOrReply(c, key, shared.czero)) == NULL ||
        checkType(c, zobj, REDIS_ZSET)) return;

    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl = zobj->ptr;
        unsigned char *eptr, *sptr;
        double score;

        /* Use the first element in range as the starting point */
        // ָ��ָ����Χ�ڵ�һ��Ԫ�صĳ�Ա
        eptr = zzlFirstInRange(zl,&range);

        /* No "first" element */
        // û���κ�Ԫ���������Χ�ڣ�ֱ�ӷ���
        if (eptr == NULL) {
            addReply(c, shared.czero);
            return;
        }

        /* First element is in range */
        // ȡ����ֵ
        sptr = ziplistNext(zl,eptr);
        score = zzlGetScore(sptr);
        redisAssertWithInfo(c,zobj,zslValueLteMax(score,&range));

        /* Iterate over elements in range */
        // ������Χ�ڵ�����Ԫ��
        while (eptr) {

            score = zzlGetScore(sptr);

            /* Abort when the node is no longer in range. */
            // �����ֵ�����Ϸ�Χ������
            if (!zslValueLteMax(score,&range)) {
                break;

            // ��ֵ���Ϸ�Χ������ count ������
            // Ȼ��ָ����һ��Ԫ��
            } else {
                count++;
                zzlNext(zl,&eptr,&sptr);
            }
        }

    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        zset *zs = zobj->ptr;
        zskiplist *zsl = zs->zsl;
        zskiplistNode *zn;
        unsigned long rank;

        /* Find first element in range */
        // ָ��ָ����Χ�ڵ�һ��Ԫ��
        zn = zslFirstInRange(zsl, &range);

        /* Use rank of first element, if any, to determine preliminary count */
        // ���������һ��Ԫ���ڷ�Χ�ڣ���ôִ�����´���
        if (zn != NULL) {
            // ȷ����Χ�ڵ�һ��Ԫ�ص���λ
            rank = zslGetRank(zsl, zn->score, zn->obj);

            count = (zsl->length - (rank - 1));

            /* Find last element in range */
            // ָ��ָ����Χ�ڵ����һ��Ԫ��
            zn = zslLastInRange(zsl, &range);

            /* Use rank of last element, if any, to determine the actual count */
            // �����Χ�ڵ����һ��Ԫ�ز�Ϊ�գ���ôִ�����´���
            if (zn != NULL) {
                // ȷ����Χ�����һ��Ԫ�ص���λ
                rank = zslGetRank(zsl, zn->score, zn->obj);

                // �������ľ��ǵ�һ�������һ������Ԫ��֮���Ԫ������
                // ������������Ԫ�أ�
                count -= (zsl->length - rank);
            }
        }

    } else {
        redisPanic("Unknown sorted set encoding");
    }

    addReplyLongLong(c, count);
}

void zlexcountCommand(redisClient *c) {
    robj *key = c->argv[1];
    robj *zobj;
    zlexrangespec range;
    int count = 0;

    /* Parse the range arguments */
    if (zslParseLexRange(c->argv[2],c->argv[3],&range) != REDIS_OK) {
        addReplyError(c,"min or max not valid string range item");
        return;
    }

    /* Lookup the sorted set */
    if ((zobj = lookupKeyReadOrReply(c, key, shared.czero)) == NULL ||
        checkType(c, zobj, REDIS_ZSET))
    {
        zslFreeLexRange(&range);
        return;
    }

    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl = zobj->ptr;
        unsigned char *eptr, *sptr;

        /* Use the first element in range as the starting point */
        eptr = zzlFirstInLexRange(zl,&range);

        /* No "first" element */
        if (eptr == NULL) {
            zslFreeLexRange(&range);
            addReply(c, shared.czero);
            return;
        }

        /* First element is in range */
        sptr = ziplistNext(zl,eptr);
        redisAssertWithInfo(c,zobj,zzlLexValueLteMax(eptr,&range));

        /* Iterate over elements in range */
        while (eptr) {
            /* Abort when the node is no longer in range. */
            if (!zzlLexValueLteMax(eptr,&range)) {
                break;
            } else {
                count++;
                zzlNext(zl,&eptr,&sptr);
            }
        }
    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        zset *zs = zobj->ptr;
        zskiplist *zsl = zs->zsl;
        zskiplistNode *zn;
        unsigned long rank;

        /* Find first element in range */
        zn = zslFirstInLexRange(zsl, &range);

        /* Use rank of first element, if any, to determine preliminary count */
        if (zn != NULL) {
            rank = zslGetRank(zsl, zn->score, zn->obj);
            count = (zsl->length - (rank - 1));

            /* Find last element in range */
            zn = zslLastInLexRange(zsl, &range);

            /* Use rank of last element, if any, to determine the actual count */
            if (zn != NULL) {
                rank = zslGetRank(zsl, zn->score, zn->obj);
                count -= (zsl->length - rank);
            }
        }
    } else {
        redisPanic("Unknown sorted set encoding");
    }

    zslFreeLexRange(&range);
    addReplyLongLong(c, count);
}

/* This command implements ZRANGEBYLEX, ZREVRANGEBYLEX. */
void genericZrangebylexCommand(redisClient *c, int reverse) {
    zlexrangespec range;
    robj *key = c->argv[1];
    robj *zobj;
    long offset = 0, limit = -1;
    unsigned long rangelen = 0;
    void *replylen = NULL;
    int minidx, maxidx;

    /* Parse the range arguments. */
    if (reverse) {
        /* Range is given as [max,min] */
        maxidx = 2; minidx = 3;
    } else {
        /* Range is given as [min,max] */
        minidx = 2; maxidx = 3;
    }

    if (zslParseLexRange(c->argv[minidx],c->argv[maxidx],&range) != REDIS_OK) {
        addReplyError(c,"min or max not valid string range item");
        return;
    }

    /* Parse optional extra arguments. Note that ZCOUNT will exactly have
     * 4 arguments, so we'll never enter the following code path. */
    if (c->argc > 4) {
        int remaining = c->argc - 4;
        int pos = 4;

        while (remaining) {
            if (remaining >= 3 && !strcasecmp(c->argv[pos]->ptr,"limit")) {
                if ((getLongFromObjectOrReply(c, c->argv[pos+1], &offset, NULL) != REDIS_OK) ||
                    (getLongFromObjectOrReply(c, c->argv[pos+2], &limit, NULL) != REDIS_OK)) return;
                pos += 3; remaining -= 3;
            } else {
                zslFreeLexRange(&range);
                addReply(c,shared.syntaxerr);
                return;
            }
        }
    }

    /* Ok, lookup the key and get the range */
    if ((zobj = lookupKeyReadOrReply(c,key,shared.emptymultibulk)) == NULL ||
        checkType(c,zobj,REDIS_ZSET))
    {
        zslFreeLexRange(&range);
        return;
    }

    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl = zobj->ptr;
        unsigned char *eptr, *sptr;
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;

        /* If reversed, get the last node in range as starting point. */
        if (reverse) {
            eptr = zzlLastInLexRange(zl,&range);
        } else {
            eptr = zzlFirstInLexRange(zl,&range);
        }

        /* No "first" element in the specified interval. */
        if (eptr == NULL) {
            addReply(c, shared.emptymultibulk);
            zslFreeLexRange(&range);
            return;
        }

        /* Get score pointer for the first element. */
        redisAssertWithInfo(c,zobj,eptr != NULL);
        sptr = ziplistNext(zl,eptr);

        /* We don't know in advance how many matching elements there are in the
         * list, so we push this object that will represent the multi-bulk
         * length in the output buffer, and will "fix" it later */
        replylen = addDeferredMultiBulkLength(c);

        /* If there is an offset, just traverse the number of elements without
         * checking the score because that is done in the next loop. */
        while (eptr && offset--) {
            if (reverse) {
                zzlPrev(zl,&eptr,&sptr);
            } else {
                zzlNext(zl,&eptr,&sptr);
            }
        }

        while (eptr && limit--) {
            /* Abort when the node is no longer in range. */
            if (reverse) {
                if (!zzlLexValueGteMin(eptr,&range)) break;
            } else {
                if (!zzlLexValueLteMax(eptr,&range)) break;
            }

            /* We know the element exists, so ziplistGet should always
             * succeed. */
            redisAssertWithInfo(c,zobj,ziplistGet(eptr,&vstr,&vlen,&vlong));

            rangelen++;
            if (vstr == NULL) {
                addReplyBulkLongLong(c,vlong);
            } else {
                addReplyBulkCBuffer(c,vstr,vlen);
            }

            /* Move to next node */
            if (reverse) {
                zzlPrev(zl,&eptr,&sptr);
            } else {
                zzlNext(zl,&eptr,&sptr);
            }
        }
    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        zset *zs = zobj->ptr;
        zskiplist *zsl = zs->zsl;
        zskiplistNode *ln;

        /* If reversed, get the last node in range as starting point. */
        if (reverse) {
            ln = zslLastInLexRange(zsl,&range);
        } else {
            ln = zslFirstInLexRange(zsl,&range);
        }

        /* No "first" element in the specified interval. */
        if (ln == NULL) {
            addReply(c, shared.emptymultibulk);
            zslFreeLexRange(&range);
            return;
        }

        /* We don't know in advance how many matching elements there are in the
         * list, so we push this object that will represent the multi-bulk
         * length in the output buffer, and will "fix" it later */
        replylen = addDeferredMultiBulkLength(c);

        /* If there is an offset, just traverse the number of elements without
         * checking the score because that is done in the next loop. */
        while (ln && offset--) {
            if (reverse) {
                ln = ln->backward;
            } else {
                ln = ln->level[0].forward;
            }
        }

        while (ln && limit--) {
            /* Abort when the node is no longer in range. */
            if (reverse) {
                if (!zslLexValueGteMin(ln->obj,&range)) break;
            } else {
                if (!zslLexValueLteMax(ln->obj,&range)) break;
            }

            rangelen++;
            addReplyBulk(c,ln->obj);

            /* Move to next node */
            if (reverse) {
                ln = ln->backward;
            } else {
                ln = ln->level[0].forward;
            }
        }
    } else {
        redisPanic("Unknown sorted set encoding");
    }

    zslFreeLexRange(&range);
    setDeferredMultiBulkLength(c, replylen, rangelen);
}

void zrangebylexCommand(redisClient *c) {
    genericZrangebylexCommand(c,0);
}

void zrevrangebylexCommand(redisClient *c) {
    genericZrangebylexCommand(c,1);
}

void zcardCommand(redisClient *c) {
    robj *key = c->argv[1];
    robj *zobj;

    // ȡ�����򼯺�
    if ((zobj = lookupKeyReadOrReply(c,key,shared.czero)) == NULL ||
        checkType(c,zobj,REDIS_ZSET)) return;

    // ���ؼ��ϻ���
    addReplyLongLong(c,zsetLength(zobj));
}

void zscoreCommand(redisClient *c) {
    robj *key = c->argv[1];
    robj *zobj;
    double score;

    if ((zobj = lookupKeyReadOrReply(c,key,shared.nullbulk)) == NULL ||
        checkType(c,zobj,REDIS_ZSET)) return;

    // ziplist
    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        // ȡ��Ԫ��
        if (zzlFind(zobj->ptr,c->argv[2],&score) != NULL)
            // �ظ���ֵ
            addReplyDouble(c,score);
        else
            addReply(c,shared.nullbulk);

    // SKIPLIST
    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        zset *zs = zobj->ptr;
        dictEntry *de;

        c->argv[2] = tryObjectEncoding(c->argv[2]);
        // ֱ�Ӵ��ֵ���ȡ�������ط�ֵ
        de = dictFind(zs->dict,c->argv[2]);
        if (de != NULL) {
            score = *(double*)dictGetVal(de);
            addReplyDouble(c,score);
        } else {
            addReply(c,shared.nullbulk);
        }

    } else {
        redisPanic("Unknown sorted set encoding");
    }
}

void zrankGenericCommand(redisClient *c, int reverse) {
    robj *key = c->argv[1];
    robj *ele = c->argv[2];
    robj *zobj;
    unsigned long llen;
    unsigned long rank;

    // ���򼯺�
    if ((zobj = lookupKeyReadOrReply(c,key,shared.nullbulk)) == NULL ||
        checkType(c,zobj,REDIS_ZSET)) return;

    // Ԫ������
    llen = zsetLength(zobj);

    redisAssertWithInfo(c,ele,sdsEncodedObject(ele));

    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl = zobj->ptr;
        unsigned char *eptr, *sptr;

        eptr = ziplistIndex(zl,0);
        redisAssertWithInfo(c,zobj,eptr != NULL);
        sptr = ziplistNext(zl,eptr);
        redisAssertWithInfo(c,zobj,sptr != NULL);

        // ��������
        rank = 1;
        while(eptr != NULL) {
            if (ziplistCompare(eptr,ele->ptr,sdslen(ele->ptr)))
                break;
            rank++;
            zzlNext(zl,&eptr,&sptr);
        }

        if (eptr != NULL) {
            // ZRANK ���� ZREVRANK ��
            if (reverse)
                addReplyLongLong(c,llen-rank);
            else
                addReplyLongLong(c,rank-1);
        } else {
            addReply(c,shared.nullbulk);
        }

    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        zset *zs = zobj->ptr;
        zskiplist *zsl = zs->zsl;
        dictEntry *de;
        double score;

        // ���ֵ���ȡ��Ԫ��
        ele = c->argv[2] = tryObjectEncoding(c->argv[2]);
        de = dictFind(zs->dict,ele);
        if (de != NULL) {

            // ȡ��Ԫ�صķ�ֵ
            score = *(double*)dictGetVal(de);

            // ����Ծ���м����Ԫ�ص���λ
            rank = zslGetRank(zsl,score,ele);
            redisAssertWithInfo(c,ele,rank); /* Existing elements always have a rank. */

            // ZRANK ���� ZREVRANK ��
            if (reverse)
                addReplyLongLong(c,llen-rank);
            else
                addReplyLongLong(c,rank-1);
        } else {
            addReply(c,shared.nullbulk);
        }

    } else {
        redisPanic("Unknown sorted set encoding");
    }
}

void zrankCommand(redisClient *c) {
    zrankGenericCommand(c, 0);
}

void zrevrankCommand(redisClient *c) {
    zrankGenericCommand(c, 1);
}

void zscanCommand(redisClient *c) {
    robj *o;
    unsigned long cursor;

    if (parseScanCursorOrReply(c,c->argv[2],&cursor) == REDIS_ERR) return;
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptyscan)) == NULL ||
        checkType(c,o,REDIS_ZSET)) return;
    scanGenericCommand(c,o,cursor);
}
