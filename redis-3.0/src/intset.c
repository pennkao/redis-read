/*
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intset.h"
#include "zmalloc.h"
#include "endianconv.h"

/* Note that these encodings are ordered, so:
 * INTSET_ENC_INT16 < INTSET_ENC_INT32 < INTSET_ENC_INT64. */
/*
 * intset �ı��뷽ʽ
 */
#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))

/* Return the required encoding for the provided value. 
 * INT32_MIN  -2147483648    INT32_MAX 2147483647
 * INT16_MIN  -32768         INT16_MAX 32767
 * ���������ڴ���ֵ v �ı��뷽ʽ
 * ��������ֵ���ڵķ�Χȷ��v�� 64λ��32λ��16λ
 * T = O(1)
 */
static uint8_t _intsetValueEncoding(int64_t v) {
    if (v < INT32_MIN || v > INT32_MAX)
        return INTSET_ENC_INT64;
    else if (v < INT16_MIN || v > INT16_MAX)
        return INTSET_ENC_INT32;
    else
        return INTSET_ENC_INT16;
}

/* Return the value at pos, given an encoding. 
 *
 * ���ݸ����ı��뷽ʽ enc �����ؼ��ϵĵײ������� pos �����ϵ�Ԫ�ء�
 *
 * T = O(1)
 */
static int64_t _intsetGetEncoded(intset *is, int pos, uint8_t enc) {
    int64_t v64;
    int32_t v32;
    int16_t v16;

    // ((ENCODING*)is->contents) ���Ƚ�����ת���ر����������
    // Ȼ�� ((ENCODING*)is->contents)+pos �����Ԫ���������е���ȷλ��
    // ֮�� member(&vEnc, ..., sizeof(vEnc)) �ٴ������п�������ȷ�������ֽ�
    // �������Ҫ�Ļ��� memrevEncifbe(&vEnc) ��Կ��������ֽڽ��д�С��ת��
    // ���ֵ����
    if (enc == INTSET_ENC_INT64) {
        memcpy(&v64,((int64_t*)is->contents)+pos,sizeof(v64));
        memrev64ifbe(&v64);
        return v64;
    } else if (enc == INTSET_ENC_INT32) {
        memcpy(&v32,((int32_t*)is->contents)+pos,sizeof(v32));
        memrev32ifbe(&v32);
        return v32;
    } else {
        memcpy(&v16,((int16_t*)is->contents)+pos,sizeof(v16));
        memrev16ifbe(&v16);
        return v16;
    }
}

/* Return the value at pos, using the configured encoding. 
 *
 * ���ݼ��ϵı��뷽ʽ�����صײ������� pos �����ϵ�ֵ
 *
 * T = O(1)
 */
static int64_t _intsetGet(intset *is, int pos) {
    return _intsetGetEncoded(is,pos,intrev32ifbe(is->encoding));
}

/* Set the value at pos, using the configured encoding. 
 *
 * ���ݼ��ϵı��뷽ʽ�����ײ������� pos λ���ϵ�ֵ��Ϊ value ��
 *
 * T = O(1)
 */
static void _intsetSet(intset *is, int pos, int64_t value) {

    // ȡ�����ϵı��뷽ʽ
    uint32_t encoding = intrev32ifbe(is->encoding);

    // ���ݱ��� ((Enc_t*)is->contents) ������ת������ȷ������
    // Ȼ�� ((Enc_t*)is->contents)[pos] ��λ������������
    // ���� ((Enc_t*)is->contents)[pos] = value ��ֵ��������
    // ��� ((Enc_t*)is->contents)+pos ��λ���ո����õ���ֵ�� 
    // �������Ҫ�Ļ��� memrevEncifbe ����ֵ���д�С��ת��
    if (encoding == INTSET_ENC_INT64) {
        ((int64_t*)is->contents)[pos] = value;
        memrev64ifbe(((int64_t*)is->contents)+pos);
    } else if (encoding == INTSET_ENC_INT32) {
        ((int32_t*)is->contents)[pos] = value;
        memrev32ifbe(((int32_t*)is->contents)+pos);
    } else {
        ((int16_t*)is->contents)[pos] = value;
        memrev16ifbe(((int16_t*)is->contents)+pos);
    }
}

/* Create an empty intset. 
 *
 * ����������һ���µĿ���������
 *
 * T = O(1)
 */
intset *intsetNew(void) {

    // Ϊ�������Ͻṹ����ռ�
    intset *is = zmalloc(sizeof(intset));

    // ���ó�ʼ����,Ĭ��16λ
    is->encoding = intrev32ifbe(INTSET_ENC_INT16);

    // ��ʼ��Ԫ������
    is->length = 0;

    return is;
}

/* Resize the intset 
 *
 * �����������ϵ��ڴ�ռ��С
 *
 * ���������Ĵ�СҪ�ȼ���ԭ���Ĵ�СҪ��
 * ��ô������ԭ��Ԫ�ص�ֵ���ᱻ�ı䡣
 *
 * ����ֵ��������С�����������
 *
 * T = O(N)
 */
static intset *intsetResize(intset *is, uint32_t len) {

    // ��������Ŀռ��С
    uint32_t size = len*intrev32ifbe(is->encoding);

    // ���ݿռ��С�����·���ռ�
    // ע������ʹ�õ��� zrealloc ��
    // ��������¿ռ��С��ԭ���Ŀռ��СҪ��
    // ��ô����ԭ�е����ݻᱻ����
    is = zrealloc(is,sizeof(intset)+size);

    return is;
}

/* Search for the position of "value".
 * 
 * �ڼ��� is �ĵײ������в���ֵ value ���ڵ�������
 *
 * Return 1 when the value was found and 
 * sets "pos" to the position of the value within the intset. 
 *
 * �ɹ��ҵ� value ʱ���������� 1 ������ *pos ��ֵ��Ϊ value ���ڵ�������
 *
 * Return 0 when the value is not present in the intset 
 * and sets "pos" to the position where "value" can be inserted. 
 *
 * ����������û�ҵ� value ʱ������ 0 ��
 * ���� *pos ��ֵ��Ϊ value ���Բ��뵽�����е�λ�á�
 *
 * T = O(log N)
 */
static uint8_t intsetSearch(intset *is, int64_t value, uint32_t *pos) {
    int min = 0, max = intrev32ifbe(is->length)-1, mid = -1;
    int64_t cur = -1;

    /* The value can never be found when the set is empty */
    // ���� is Ϊ��ʱ�����
    if (intrev32ifbe(is->length) == 0) {
        if (pos) *pos = 0;
        return 0;
    } else {
        /* Check for the case where we know we cannot find the value,
         * but do know the insert position. */
        // ��Ϊ�ײ�����������ģ���� value �����������һ��ֵ��Ҫ��
        // ��ô value �϶��������ڼ����У�
        // ����Ӧ�ý� value ��ӵ��ײ��������ĩ��
        if (value > _intsetGet(is,intrev32ifbe(is->length)-1)) {
            if (pos) *pos = intrev32ifbe(is->length);
            return 0;
        // ��Ϊ�ײ�����������ģ���� value ����������ǰһ��ֵ��ҪС
        // ��ô value �϶��������ڼ����У�
        // ����Ӧ�ý�����ӵ��ײ��������ǰ��
        } else if (value < _intsetGet(is,0)) {
            if (pos) *pos = 0;
            return 0;
        }
    }

    // �����������н��ж��ֲ���
    // T = O(log N)
    while(max >= min) {
        mid = (min+max)/2;
        cur = _intsetGet(is,mid);
        if (value > cur) {
            min = mid+1;
        } else if (value < cur) {
            max = mid-1;
        } else {
            break;
        }
    }

    // ����Ƿ��Ѿ��ҵ��� value
    if (value == cur) {
        if (pos) *pos = mid;
        return 1;
    } else {
        if (pos) *pos = min;
        return 0;
    }
}

/* Upgrades the intset to a larger encoding and inserts the given integer. 
 *
 * ����ֵ value ��ʹ�õı��뷽ʽ�����������ϵı������������
 * ����ֵ value ��ӵ�����������������С�
 *
 * ����ֵ�������Ԫ��֮�����������
 *
 * T = O(N)
 *��Ĭ��Ϊ16λ������������ݴ���16Ϊ������Ҫ�������������
 */
static intset *intsetUpgradeAndAdd(intset *is, int64_t value) {
    
    // ��ǰ�ı��뷽ʽ
    uint8_t curenc = intrev32ifbe(is->encoding);

    // ��ֵ����ı��뷽ʽ
    uint8_t newenc = _intsetValueEncoding(value);

    // ��ǰ���ϵ�Ԫ������
    int length = intrev32ifbe(is->length);
	
    // ���� value ��ֵ�������ǽ�����ӵ��ײ��������ǰ�˻�������
    // ע�⣬��Ϊ value �ı���ȼ���ԭ�е�����Ԫ�صı��붼Ҫ��
    // ���� value Ҫô���ڼ����е�����Ԫ�أ�ҪôС�ڼ����е�����Ԫ��
    // ��ˣ�value ֻ����ӵ��ײ��������ǰ�˻�����
    int prepend = value < 0 ? 1 : 0;

    /* First set new encoding and resize */
    // ���¼��ϵı��뷽ʽ
    is->encoding = intrev32ifbe(newenc);
    // �����±���Լ��ϣ��ĵײ����飩���пռ����
    // T = O(N)
    is = intsetResize(is,intrev32ifbe(is->length)+1);

    /* Upgrade back-to-front so we don't overwrite values.
     * Note that the "prepend" variable is used to make sure we have an empty
     * space at either the beginning or the end of the intset. */
    // ���ݼ���ԭ���ı��뷽ʽ���ӵײ�������ȡ������Ԫ��
    // Ȼ���ٽ�Ԫ�����±���ķ�ʽ��ӵ�������
    // ��������������֮�󣬼���������ԭ�е�Ԫ�ؾ�����˴Ӿɱ��뵽�±����ת��
    // ��Ϊ�·���Ŀռ䶼��������ĺ�ˣ����Գ����ȴӺ����ǰ���ƶ�Ԫ��
    // �ٸ����ӣ�����ԭ���� curenc ���������Ԫ�أ��������������������£�
    // | x | y | z | 
    // ���������������ط���֮������ͱ������ˣ����� �� ��ʾδʹ�õ��ڴ棩��
    // | x | y | z | ? |   ?   |   ?   |
    // ��ʱ����������˿�ʼ�����²���Ԫ�أ�
    // | x | y | z | ? |   z   |   ?   |
    // | x | y |   y   |   z   |   ?   |
    // |   x   |   y   |   z   |   ?   |
    // ��󣬳�����Խ���Ԫ����ӵ���� �� �ű�ʾ��λ���У�
    // |   x   |   y   |   z   |  new  |
    // ������ʾ������Ԫ�ر�ԭ��������Ԫ�ض���������Ҳ���� prepend == 0
    // ����Ԫ�ر�ԭ��������Ԫ�ض�Сʱ��prepend == 1���������Ĺ������£�
    // | x | y | z | ? |   ?   |   ?   |
    // | x | y | z | ? |   ?   |   z   |
    // | x | y | z | ? |   y   |   z   |
    // | x | y |   x   |   y   |   z   |
    // �������ֵʱ��ԭ���� | x | y | �����ݽ�����ֵ����
    // |  new  |   x   |   y   |   z   |
    // T = O(N)
	// �ƶ�ԭ����Ԫ�أ�Ϊ��Ԫ�ز�����׼��������Ǹ���������Ԫ�������ƶ������������ƶ�
	// ��������ת����_intsetGetEncoded�Ȱ�ԭ����ȡ��ֵ��_intsetSet��ֵ���뵽�±�����λ�ã�����Ǹ����������ƶ�
    while(length--)
        _intsetSet(is,length+prepend,_intsetGetEncoded(is,length,curenc));

    /* Set the value at the beginning or the end. */
    // ������ֵ������ prepend ��ֵ����������ӵ�����ͷ��������β
    if (prepend)
        _intsetSet(is,0,value);
    else
        _intsetSet(is,intrev32ifbe(is->length),value);

    // �����������ϵ�Ԫ������
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);

    return is;
}

/*
 * ��ǰ���Ⱥ��ƶ�ָ��������Χ�ڵ�����Ԫ��
 *
 * �������е� MoveTail ��ʵ��һ�������Ե����֣�
 * �������������ǰ������ƶ�Ԫ�أ�
 * �������������
 *
 * �������Ԫ�ص�����ʱ������Ҫ��������ƶ���
 * ��������ʾ���£�����ʾһ��δ������ֵ�Ŀռ䣩��
 * | x | y | z | ? |
 *     |<----->|
 * ����Ԫ�� n �� pos Ϊ 1 ����ô���齫�ƶ� y �� z ����Ԫ��
 * | x | y | y | z |
 *         |<----->|
 * ���žͿ��Խ���Ԫ�� n ���õ� pos ���ˣ�
 * | x | n | y | z |
 *
 * ����������ɾ��Ԫ��ʱ������Ҫ������ǰ�ƶ���
 * ��������ʾ���£����� b ΪҪɾ����Ŀ�꣺
 * | a | b | c | d |
 *         |<----->|
 * ��ô����ͻ��ƶ� b �������Ԫ����ǰһ��Ԫ�ص�λ�ã�
 * �Ӷ����� b �����ݣ�
 * | a | c | d | d |
 *     |<----->|
 * ��󣬳����ٴ�����ĩβɾ��һ��Ԫ�صĿռ䣺
 * | a | c | d |
 * �����������ɾ��������
 *
 * T = O(N)
 */
static void intsetMoveTail(intset *is, uint32_t from, uint32_t to) {

    void *src, *dst;

    // Ҫ�ƶ���Ԫ�ظ���
    uint32_t bytes = intrev32ifbe(is->length)-from;

    // ���ϵı��뷽ʽ
    uint32_t encoding = intrev32ifbe(is->encoding);

    // ���ݲ�ͬ�ı���
    // src = (Enc_t*)is->contents+from ��¼�ƶ���ʼ��λ��
    // dst = (Enc_t*)is_.contents+to ��¼�ƶ�������λ��
    // bytes *= sizeof(Enc_t) ����һ��Ҫ�ƶ������ֽ�
    if (encoding == INTSET_ENC_INT64) {
        src = (int64_t*)is->contents+from;
        dst = (int64_t*)is->contents+to;
        bytes *= sizeof(int64_t);
    } else if (encoding == INTSET_ENC_INT32) {
        src = (int32_t*)is->contents+from;
        dst = (int32_t*)is->contents+to;
        bytes *= sizeof(int32_t);
    } else {
        src = (int16_t*)is->contents+from;
        dst = (int16_t*)is->contents+to;
        bytes *= sizeof(int16_t);
    }

    // �����ƶ�
    // T = O(N)
    memmove(dst,src,bytes);
}

/* Insert an integer in the intset 
 * 
 * ���Խ�Ԫ�� value ��ӵ����������С�
 *
 * *success ��ֵָʾ����Ƿ�ɹ���
 * - �����ӳɹ�����ô�� *success ��ֵ��Ϊ 1 ��
 * - ��ΪԪ���Ѵ��ڶ�������ʧ��ʱ���� *success ��ֵ��Ϊ 0 ��
 *
 * T = O(N)
 */
intset *intsetAdd(intset *is, int64_t value, uint8_t *success) {

    // ������� value ����ĳ���
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;

    // Ĭ�����ò���Ϊ�ɹ�
    if (success) *success = 1;

    /* Upgrade encoding if necessary. If we need to upgrade, we know that
     * this value should be either appended (if > 0) or prepended (if < 0),
     * because it lies outside the range of existing values. */
    // ��� value �ı���������������ڵı���Ҫ��
    // ��ô��ʾ value ��Ȼ������ӵ�����������
    // ��������������Ҫ����������������������� value ����ı���
    if (valenc > intrev32ifbe(is->encoding)) {
        /* This always succeeds, so we don't need to curry *success. */
        // T = O(N)
        return intsetUpgradeAndAdd(is,value);
    } else {
        // ���е������ʾ�����������еı��뷽ʽ������ value

        /* Abort if the value is already present in the set.
         * This call will populate "pos" with the right position to insert
         * the value when it cannot be found. */
        // �����������в��� value �������Ƿ���ڣ�
        // - ������ڣ���ô�� *success ����Ϊ 0 ��������δ���Ķ�����������
        // - ��������ڣ���ô���Բ��� value ��λ�ý������浽 pos ָ����
        //   �ȴ���������ʹ��
        if (intsetSearch(is,value,&pos)) {
            if (success) *success = 0;
            return is;
        }

        // ���е������ʾ value �������ڼ�����
        // ������Ҫ�� value ��ӵ�����������
    
        // Ϊ value �ڼ����з���ռ�
        is = intsetResize(is,intrev32ifbe(is->length)+1);
        // �����Ԫ�ز��Ǳ���ӵ��ײ������ĩβ
        // ��ô��Ҫ������Ԫ�ص����ݽ����ƶ����ճ� pos �ϵ�λ�ã�����������ֵ
        // �ٸ�����
        // �������Ϊ��
        // | x | y | z | ? |
        //     |<----->|
        // ����Ԫ�� n �� pos Ϊ 1 ����ô���齫�ƶ� y �� z ����Ԫ��
        // | x | y | y | z |
        //         |<----->|
        // �����Ϳ��Խ���Ԫ�����õ� pos ���ˣ�
        // | x | n | y | z |
        // T = O(N)
        if (pos < intrev32ifbe(is->length)) intsetMoveTail(is,pos,pos+1);
    }

    // ����ֵ���õ��ײ������ָ��λ����
    _intsetSet(is,pos,value);

    // ��һ����Ԫ�������ļ�����
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);

    // ���������Ԫ�غ����������
    return is;

    /* p.s. ����Ĵ�������ع������¸��򵥵���ʽ��
    
    if (valenc > intrev32ifbe(is->encoding)) {
        return intsetUpgradeAndAdd(is,value);
    }
     
    if (intsetSearch(is,value,&pos)) {
        if (success) *success = 0;
        return is;
    } else {
        is = intsetResize(is,intrev32ifbe(is->length)+1);
        if (pos < intrev32ifbe(is->length)) intsetMoveTail(is,pos,pos+1);
        _intsetSet(is,pos,value);

        is->length = intrev32ifbe(intrev32ifbe(is->length)+1);
        return is;
    }
    */
}

/* Delete integer from intset 
 *
 * ������������ɾ��ֵ value ��
 *
 * *success ��ֵָʾɾ���Ƿ�ɹ���
 * - ��ֵ�����ڶ����ɾ��ʧ��ʱ��ֵΪ 0 ��
 * - ɾ���ɹ�ʱ��ֵΪ 1 ��
 *
 * T = O(N)
 */
intset *intsetRemove(intset *is, int64_t value, int *success) {

    // ���� value �ı��뷽ʽ
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;

    // Ĭ�����ñ�ʶֵΪɾ��ʧ��
    if (success) *success = 0;

    // �� value �ı����СС�ڻ���ڼ��ϵĵ�ǰ���뷽ʽ��˵�� value �п��ܴ����ڼ��ϣ�
    // ���� intsetSearch �Ľ��Ϊ�棬��ôִ��ɾ��
    // T = O(log N)
    if (valenc <= intrev32ifbe(is->encoding) && intsetSearch(is,value,&pos)) {

        // ȡ�����ϵ�ǰ��Ԫ������
        uint32_t len = intrev32ifbe(is->length);

        /* We know we can delete */
        // ���ñ�ʶֵΪɾ���ɹ�
        if (success) *success = 1;

        /* Overwrite value with tail and update length */
        // ��� value ����λ�������ĩβ
        // ��ô��Ҫ��ԭ��λ�� value ֮���Ԫ�ؽ����ƶ�
        //
        // �ٸ����ӣ���������ʾ���£��� b Ϊɾ����Ŀ��
        // | a | b | c | d |
        // ��ô intsetMoveTail �� b ֮�������������ǰ�ƶ�һ��Ԫ�صĿռ䣬
        // ���� b ԭ��������
        // | a | c | d | d |
        // ֮�� intsetResize ��С�ڴ��Сʱ��
        // ����ĩβ�������һ��Ԫ�صĿռ佫���Ƴ�
        // | a | c | d |
        if (pos < (len-1)) intsetMoveTail(is,pos+1,pos);
        // ��С����Ĵ�С���Ƴ���ɾ��Ԫ��ռ�õĿռ�
        // T = O(N)
        is = intsetResize(is,len-1);
        // ���¼��ϵ�Ԫ������
        is->length = intrev32ifbe(len-1);
    }

    return is;
}

/* Determine whether a value belongs to this set 
 *
 * ������ֵ value �Ƿ񼯺��е�Ԫ�ء�
 *
 * �Ƿ��� 1 �����Ƿ��� 0 ��
 *
 * T = O(log N)
 */
uint8_t intsetFind(intset *is, int64_t value) {

    // ���� value �ı���
    uint8_t valenc = _intsetValueEncoding(value);

    // ��� value �ı�����ڼ��ϵĵ�ǰ���룬��ô value һ���������ڼ���
    // �� value �ı���С�ڵ��ڼ��ϵĵ�ǰ����ʱ��
    // ����ʹ�� intsetSearch ���в���
    return valenc <= intrev32ifbe(is->encoding) && intsetSearch(is,value,NULL);
}

/* Return random member 
 *
 * �������������������һ��Ԫ��
 *
 * ֻ���ڼ��Ϸǿ�ʱʹ��
 *
 * T = O(1)
 */
int64_t intsetRandom(intset *is) {
    // intrev32ifbe(is->length) ȡ�����ϵ�Ԫ������
    // �� rand() % intrev32ifbe(is->length) ����Ԫ����������һ���������
    // Ȼ�� _intsetGet ��������������������ֵ
    return _intsetGet(is,rand()%intrev32ifbe(is->length));
}

/* Sets the value to the value at the given position. When this position is
 * out of range the function returns 0, when in range it returns 1. */
/* 
 * ȡ�����ϵײ�����ָ��λ���е�ֵ�����������浽 value ָ���С�
 *
 * ��� pos û���������������Χ����ô���� 1 �����������������ô���� 0 ��
 *
 * p.s. ����ԭ�ĵ��ĵ�˵���������������ֵ�����Ǵ���ġ�
 *
 * T = O(1)
 */
uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value) {

    // pos < intrev32ifbe(is->length) 
    // ��� pos �Ƿ��������ķ�Χ
    if (pos < intrev32ifbe(is->length)) {

        // ����ֵ��ָ��
        *value = _intsetGet(is,pos);

        // ���سɹ�ָʾֵ
        return 1;
    }

    // ����������Χ
    return 0;
}

/* Return intset length 
 *
 * ���������������е�Ԫ�ظ���
 *
 * T = O(1)
 */
uint32_t intsetLen(intset *is) {
    return intrev32ifbe(is->length);
}

/* Return intset blob size in bytes. 
 *
 * ����������������ռ�õ��ֽ�������
 * ������������������ϵĽṹ��С���Լ�������������Ԫ�ص��ܴ�С
 *
 * T = O(1)
 */
size_t intsetBlobLen(intset *is) {
    return sizeof(intset)+intrev32ifbe(is->length)*intrev32ifbe(is->encoding);
}

#ifdef INTSET_TEST_MAIN
#include <sys/time.h>

void intsetRepr(intset *is) {
    int i;
    for (i = 0; i < intrev32ifbe(is->length); i++) {
        printf("%lld\n", (uint64_t)_intsetGet(is,i));
    }
    printf("\n");
}

void error(char *err) {
    printf("%s\n", err);
    exit(1);
}

void ok(void) {
    printf("OK\n");
}

long long usec(void) {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
}

#define assert(_e) ((_e)?(void)0:(_assert(#_e,__FILE__,__LINE__),exit(1)))
void _assert(char *estr, char *file, int line) {
    printf("\n\n=== ASSERTION FAILED ===\n");
    printf("==> %s:%d '%s' is not true\n",file,line,estr);
}

intset *createSet(int bits, int size) {
    uint64_t mask = (1<<bits)-1;
    uint64_t i, value;
    intset *is = intsetNew();

    for (i = 0; i < size; i++) {
        if (bits > 32) {
            value = (rand()*rand()) & mask;
        } else {
            value = rand() & mask;
        }
        is = intsetAdd(is,value,NULL);
    }
    return is;
}

void checkConsistency(intset *is) {
    int i;

    for (i = 0; i < (intrev32ifbe(is->length)-1); i++) {
        uint32_t encoding = intrev32ifbe(is->encoding);

        if (encoding == INTSET_ENC_INT16) {
            int16_t *i16 = (int16_t*)is->contents;
            assert(i16[i] < i16[i+1]);
        } else if (encoding == INTSET_ENC_INT32) {
            int32_t *i32 = (int32_t*)is->contents;
            assert(i32[i] < i32[i+1]);
        } else {
            int64_t *i64 = (int64_t*)is->contents;
            assert(i64[i] < i64[i+1]);
        }
    }
}

int main(int argc, char **argv) {
    uint8_t success;
    int i;
    intset *is;
    sranddev();

    printf("Value encodings: "); {
        assert(_intsetValueEncoding(-32768) == INTSET_ENC_INT16);
        assert(_intsetValueEncoding(+32767) == INTSET_ENC_INT16);
        assert(_intsetValueEncoding(-32769) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(+32768) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(-2147483648) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(+2147483647) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(-2147483649) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(+2147483648) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(-9223372036854775808ull) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(+9223372036854775807ull) == INTSET_ENC_INT64);
        ok();
    }

    printf("Basic adding: "); {
        is = intsetNew();
        is = intsetAdd(is,5,&success); assert(success);
        is = intsetAdd(is,6,&success); assert(success);
        is = intsetAdd(is,4,&success); assert(success);
        is = intsetAdd(is,4,&success); assert(!success);
        ok();
    }

    printf("Large number of random adds: "); {
        int inserts = 0;
        is = intsetNew();
        for (i = 0; i < 1024; i++) {
            is = intsetAdd(is,rand()%0x800,&success);
            if (success) inserts++;
        }
        assert(intrev32ifbe(is->length) == inserts);
        checkConsistency(is);
        ok();
    }

    printf("Upgrade from int16 to int32: "); {
        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        assert(intsetFind(is,32));
        assert(intsetFind(is,65535));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,-65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        assert(intsetFind(is,32));
        assert(intsetFind(is,-65535));
        checkConsistency(is);
        ok();
    }

    printf("Upgrade from int16 to int64: "); {
        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,32));
        assert(intsetFind(is,4294967295));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,-4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,32));
        assert(intsetFind(is,-4294967295));
        checkConsistency(is);
        ok();
    }

    printf("Upgrade from int32 to int64: "); {
        is = intsetNew();
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        is = intsetAdd(is,4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,65535));
        assert(intsetFind(is,4294967295));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        is = intsetAdd(is,-4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,65535));
        assert(intsetFind(is,-4294967295));
        checkConsistency(is);
        ok();
    }

    printf("Stress lookups: "); {
        long num = 100000, size = 10000;
        int i, bits = 20;
        long long start;
        is = createSet(bits,size);
        checkConsistency(is);

        start = usec();
        for (i = 0; i < num; i++) intsetSearch(is,rand() % ((1<<bits)-1),NULL);
        printf("%ld lookups, %ld element set, %lldusec\n",num,size,usec()-start);
    }

    printf("Stress add+delete: "); {
        int i, v1, v2;
        is = intsetNew();
        for (i = 0; i < 0xffff; i++) {
            v1 = rand() % 0xfff;
            is = intsetAdd(is,v1,NULL);
            assert(intsetFind(is,v1));

            v2 = rand() % 0xfff;
            is = intsetRemove(is,v2,NULL);
            assert(!intsetFind(is,v2));
        }
        checkConsistency(is);
        ok();
    }
}
#endif
