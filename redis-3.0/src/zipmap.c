/* String -> String Map data structure optimized for size.
 *
 * Ϊ��Լ�ռ��ʵ�ֵ��ַ������ַ���ӳ��ṹ
 *
 * This file implements a data structure mapping strings to other strings
 * implementing an O(n) lookup data structure designed to be very memory
 * efficient.
 *
 * ���ļ�ʵ����һ�����ַ���ӳ�䵽��һ���ַ��������ݽṹ��
 * ������ݽṹ�ǳ���Լ�ڴ棬����֧�ָ��Ӷ�Ϊ O(N) �Ĳ��Ҳ�����
 *
 * The Redis Hash type uses this data structure for hashes composed of a small
 * number of elements, to switch to a hash table once a given number of
 * elements is reached.
 * 
 * Redis ʹ��������ݽṹ�������ֵ����������� Hash ��
 * һ����ֵ�Ե���������ĳ������ֵ��Hash �ĵײ��ʾ�ͻ��Զ�ת���ɹ�ϣ��
 *
 * Given that many times Redis Hashes are used to represent objects composed
 * of few fields, this is a very big win in terms of used memory.
 * 
 * ��Ϊ�ܶ�ʱ��һ�� Hash ��ֻ������������ key-value �ԣ�
 * ����ʹ�� zipmap ����ֱ��ʹ�������Ĺ�ϣ��Ҫ��Լ�����ڴ档
 *
 * --------------------------------------------------------------------------
 *
 * ע�⣬�� 2.6 �汾��ʼ�� Redis ʹ�� ziplist ����ʾС Hash ��
 * ������ʹ�� zipmap ��
 * ������Ϣ�����https://github.com/antirez/redis/issues/188
 * -- huangz
 *
 * --------------------------------------------------------------------------
 *
 * Copyright (c) 2009-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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

/* Memory layout of a zipmap, for the map "foo" => "bar", "hello" => "world":
 *
 * �����Ǵ��� "foo" => "bar" �� "hello" => "world" ����ӳ��� zipmap ���ڴ�ṹ��
 *
 * <zmlen><len>"foo"<len><free>"bar"<len>"hello"<len><free>"world"<ZIPMAP_END>
 *
 * <zmlen> is 1 byte length that holds the current size of the zipmap.
 * When the zipmap length is greater than or equal to 254, this value
 * is not used and the zipmap needs to be traversed to find out the length.
 *
 * <zmlen> �ĳ���Ϊ 1 �ֽڣ�����¼�� zipmap ����ļ�ֵ��������
 *
 *  1) ֻ���� zipmap �ļ�ֵ������ < 254 ʱ�����ֵ�ű�ʹ�á�
 *
 *  2) �� zipmap �ļ�ֵ������ >= 254 ��������Ҫ�������� zipmap ����֪������ȷ�д�С��
 *
 * <len> is the length of the following string (key or value).
 * <len> lengths are encoded in a single value or in a 5 bytes value.
 * If the first byte value (as an unsigned 8 bit value) is between 0 and
 * 252, it's a single-byte length. If it is 253 then a four bytes unsigned
 * integer follows (in the host byte ordering). A value of 255 is used to
 * signal the end of the hash. The special value 254 is used to mark
 * empty space that can be used to add new key/value pairs.
 *
 * <len> ��ʾ������������ַ���(����ֵ)�ĳ��ȡ�
 *
 * <len> ������ 1 �ֽڻ��� 5 �ֽ������룺
 *
 *   * ��� <len> �ĵ�һ�ֽ�(�޷��� 8 bit)�ǽ��� 0 �� 252 ֮���ֵ��
 *     ��ô����ֽھ����ַ����ĳ��ȡ�
 *
 *   * �����һ�ֽڵ�ֵΪ 253 ����ô����ֽ�֮��� 4 �ֽ��޷�������
 *     (��/С������������������)�����ַ����ĳ��ȡ�
 *
 *   * ֵ 254 ���ڱ�ʶδ��ʹ�õġ���������� key-value �ԵĿռ䡣
 *
 *   * ֵ 255 ���ڱ�ʾ zipmap ��ĩβ��
 *
 * <free> is the number of free unused bytes after the string, resulting 
 * from modification of values associated to a key. For instance if "foo"
 * is set to "bar", and later "foo" will be set to "hi", it will have a
 * free byte to use if the value will enlarge again later, or even in
 * order to add a key/value pair if it fits.
 *
 * <free> ���ַ���֮��δ��ʹ�õ��ֽ�������
 *
 * <free> ��ֵ���ڼ�¼��Щ��Ϊֵ���޸ģ�������Լ�����Ŀռ�������
 *
 * �ٸ����ӣ�
 * zimap ��ԭ����һ�� "foo" -> "bar" ��ӳ�䣬
 * ���Ǻ��������޸�Ϊ "foo" -> "hi" ��
 * ���ڣ����ַ��� "hi" ֮�����һ���ֽڵ�δʹ�ÿռ䡣
 *
 * �ƺ������δʹ�õĿռ�������ڽ����ٴζ�ֵ���޸�
 * �����磬�ٴν� "foo" ��ֵ�޸�Ϊ "yoo" ���ȵȣ�
 * ���δʹ�ÿռ��㹻����ô�����������һ���µ� key-value ��Ҳ�ǿ��ܵġ�
 *
 * <free> is always an unsigned 8 bit number, because if after an
 * update operation there are more than a few free bytes, the zipmap will be
 * reallocated to make sure it is as small as possible.
 *
 * <free> ����һ���޷��� 8 λ���֡�
 * ��ִ�и��²���֮�����ʣ���ֽ������ڵ��� ZIPMAP_VALUE_MAX_FREE ��
 * ��ô zipmap �ͻ�����ط��䣬��������ռ���н�����
 * ��ˣ� <free> ��ֵ����ܴ�8 λ�ĳ��ȶ��ڱ��� <free> ��˵�Ѿ��㹻��
 *
 * The most compact representation of the above two elements hash is actually:
 *
 * һ������ "foo" -> "bar" �� "hello" -> "world" ӳ��� zipmap ������յı�ʾ���£�
 *
 * "\x02\x03foo\x03\x00bar\x05hello\x05\x00world\xff"
 *
 * Note that because keys and values are prefixed length "objects",
 * the lookup will take O(N) where N is the number of elements
 * in the zipmap and *not* the number of bytes needed to represent the zipmap.
 * This lowers the constant times considerably.
 *
 * ע�⣬��Ϊ����ֵ���Ǵ��г���ǰ׺�Ķ���
 * ��� zipmap �Ĳ��Ҳ����ĸ��Ӷ�Ϊ O(N) ��
 * ���� N �Ǽ�ֵ�Ե������������� zipmap ���ֽ�������ǰ�ߵĳ�����СһЩ����
 */

#include <stdio.h>
#include <string.h>
#include "zmalloc.h"
#include "endianconv.h"

// һ���ֽ����ܱ���� zipmap Ԫ���������ܵ��ڻ򳬹����ֵ
#define ZIPMAP_BIGLEN 254

// zipmap �Ľ�����ʶ
#define ZIPMAP_END 255

/* The following defines the max value for the <free> field described in the
 * comments above, that is, the max number of trailing bytes in a value. */
// <free> ����������ֵ
#define ZIPMAP_VALUE_MAX_FREE 4

/* The following macro returns the number of bytes needed to encode the length
 * for the integer value _l, that is, 1 byte for lengths < ZIPMAP_BIGLEN and
 * 5 bytes for all the other lengths. */
// ���ر���������� _l ������ֽ���
#define ZIPMAP_LEN_BYTES(_l) (((_l) < ZIPMAP_BIGLEN) ? 1 : sizeof(unsigned int)+1)

/* Create a new empty zipmap. 
 *
 * ����һ���µ� zipmap
 *
 * T = O(1)
 */
unsigned char *zipmapNew(void) {

    unsigned char *zm = zmalloc(2);

    zm[0] = 0; /* Length */
    zm[1] = ZIPMAP_END;

    return zm;
}

/* Decode the encoded length pointed by 'p' 
 *
 * ���벢���� p ��ָ����ѱ��볤��
 *
 * T = O(1)
 */
static unsigned int zipmapDecodeLength(unsigned char *p) {

    unsigned int len = *p;

    // ���ֽڳ���
    if (len < ZIPMAP_BIGLEN) return len;

    // 5 �ֽڳ���
    memcpy(&len,p+1,sizeof(unsigned int));
    memrev32ifbe(&len);
    return len;
}

/* Encode the length 'l' writing it in 'p'. If p is NULL it just returns
 * the amount of bytes required to encode such a length. 
 *
 * ���볤�� l ������д�뵽 p �У�Ȼ�󷵻ر��� l ������ֽ�����
 * ��� p �� NULL ����ô����ֻ���ر��� l ������ֽ������������κ�д�롣
 *
 * T = O(1)
 */
static unsigned int zipmapEncodeLength(unsigned char *p, unsigned int len) {

    // ֻ���ر���������ֽ���
    if (p == NULL) {
        return ZIPMAP_LEN_BYTES(len);

    // ���룬��д�룬Ȼ�󷵻ر���������ֽ���
    } else {
        if (len < ZIPMAP_BIGLEN) {
            p[0] = len;
            return 1;
        } else {
            p[0] = ZIPMAP_BIGLEN;
            memcpy(p+1,&len,sizeof(len));
            memrev32ifbe(p+1);
            return 1+sizeof(len);
        }
    }
}

/* Search for a matching key, returning a pointer to the entry inside the
 * zipmap. Returns NULL if the key is not found.
 *
 * �� zipmap �в��Һ͸��� key ƥ��Ľڵ㣺
 *
 *  1)�ҵ��Ļ��ͷ��ؽڵ��ָ�롣
 *
 *  2)û�ҵ��򷵻� NULL ��
 *
 * If NULL is returned, and totlen is not NULL, it is set to the entire
 * size of the zimap, so that the calling function will be able to
 * reallocate the original zipmap to make room for more entries. 
 *
 * ���û���ҵ���Ӧ�Ľڵ㣨�������� NULL�������� totlen ��Ϊ NULL ��
 * ��ô *totlen ��ֵ������Ϊ���� zipmap �Ĵ�С��
 * ���������߾Ϳ��Ը��� *totlen ��ֵ���� zipmap �����ڴ��ط��䣬
 * �Ӷ��� zipmap ���ɸ���ڵ㡣
 *
 * T = O(N^2)
 */
static unsigned char *zipmapLookupRaw(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned int *totlen) {

    // zm+1 �Թ� <zmlen> ���ԣ��� p ָ�� zipmap ���׸��ڵ�
    unsigned char *p = zm+1, *k = NULL;
    unsigned int l,llen;

    // �������� zipmap ��Ѱ��
    // T = O(N^2)
    while(*p != ZIPMAP_END) {
        unsigned char free;

        /* Match or skip the key */
        // ������ĳ���
        l = zipmapDecodeLength(p);
        // ���������ĳ���������ֽ���
        llen = zipmapEncodeLength(NULL,l);
        // �Ա� key
        // T = O(N)
        if (key != NULL && k == NULL && l == klen && !memcmp(p+llen,key,l)) {
            /* Only return when the user doesn't care
             * for the total length of the zipmap. */
            if (totlen != NULL) {
                // �����������Ҫ֪������ zipmap �ĳ��ȣ���ô��¼�ҵ���ָ�뵽���� k
                // ֮�����ʱ������ֻ���� zipmap ʣ��ڵ�ĳ��ȣ������� memcmp ���жԱ�
                // ��Ϊ k �Ѿ���Ϊ NULL ��
                k = p;
            } else {
                // ��������߲���Ҫ֪������ zipmap �ĳ��ȣ���ôֱ�ӷ��� p 
                return p;
            }
        }

        // Խ�����ڵ㣬ָ��ֵ�ڵ�
        p += llen+l;

        /* Skip the value as well */
        // ����ֵ�ĳ���
        l = zipmapDecodeLength(p);
        // �������ֵ�ĳ���������ֽ�����
        // ���ƶ�ָ�� p ��Խ���� <len> ���ԣ�ָ�� <free> ����
        p += zipmapEncodeLength(NULL,l);
        // ȡ�� <free> ���Ե�ֵ
        free = p[0];
        // �Թ�ֵ�ڵ㣬ָ����һ�ڵ�
        p += l+1+free; /* +1 to skip the free byte */
    }

    // ���㲢��¼ zipmap �Ŀռ䳤��
    // + 1 �ǽ� ZIPMAP_END Ҳ��������
    if (totlen != NULL) *totlen = (unsigned int)(p-zm)+1;

    // �����ҵ� key ��ָ��
    return k;
}

/*
 * ���ؼ�����Ϊ klen ��ֵ����Ϊ vlen ���½ڵ�������ֽ���
 *
 * T = O(1)
 */
static unsigned long zipmapRequiredLength(unsigned int klen, unsigned int vlen) {
    unsigned int l;

    // �ڵ�Ļ�������Ҫ��
    // 1) klen : ���ĳ���
    // 2) vlen : ֵ�ĳ���
    // 3) �����ַ�������Ҫ���� 1 �ֽ������泤�ȣ��� <free> Ҳ��Ҫ 1 ���ֽڣ��� 3 �ֽ�
    l = klen+vlen+3;

    // ��� 1 �ֽڲ����Ա�����ĳ��ȣ���ô��Ҫ�� 4 ���ֽ�
    if (klen >= ZIPMAP_BIGLEN) l += 4;

    // ��� 1 �ֽڲ����Ա���ֵ�ĳ��ȣ���ô��Ҫ�� 4 ���ֽ�
    if (vlen >= ZIPMAP_BIGLEN) l += 4;

    return l;
}

/* Return the total amount used by a key (encoded length + payload) 
 *
 * ���ؼ���ռ�õ��ֽ���
 *
 * �������볤��ֵ������ֽ������Լ�ֵ�ĳ��ȱ���
 *
 * T = O(1)
 */
static unsigned int zipmapRawKeyLength(unsigned char *p) {
    unsigned int l = zipmapDecodeLength(p);
    return zipmapEncodeLength(NULL,l) + l;
}

/* Return the total amount used by a value
 * (encoded length + single byte free count + payload) 
 *
 * ����ֵ��ռ�õ��ֽ�����
 *
 * �������볤��ֵ������ֽ����������ֽڵ� <free> ���ԣ��Լ�ֵ�ĳ��ȱ���
 *
 * T = O(1)
 */
static unsigned int zipmapRawValueLength(unsigned char *p) {

    // ȡ��ֵ�ĳ���
    unsigned int l = zipmapDecodeLength(p);
    unsigned int used;
    
    // ���볤��������ֽ���
    used = zipmapEncodeLength(NULL,l);
    // �����ܺ�
    used += p[used] + 1 + l;

    return used;
}

/* If 'p' points to a key, this function returns the total amount of
 * bytes used to store this entry (entry = key + associated value + trailing
 * free space if any). 
 *
 * ��� p ָ��һ��������ô����������ر�������ڵ�������ֽ�������
 *
 * �ڵ������ = ��ռ�õĿռ����� + ֵռ�õĿռ����� + free ����ռ�õĿռ�����
 *
 * T = O(1)
 */
static unsigned int zipmapRawEntryLength(unsigned char *p) {
    unsigned int l = zipmapRawKeyLength(p);
    return l + zipmapRawValueLength(p+l);
}

/*
 * �� zipmap �Ĵ�С����Ϊ len ��
 *
 * T = O(N)
 */
static inline unsigned char *zipmapResize(unsigned char *zm, unsigned int len) {

    zm = zrealloc(zm, len);

    zm[len-1] = ZIPMAP_END;

    return zm;
}

/* Set key to value, creating the key if it does not already exist.
 *
 * �� key ��ֵ����Ϊ value ����� key �������� zipmap �У���ô�´���һ����
 *
 * If 'update' is not NULL, *update is set to 1 if the key was
 * already preset, otherwise to 0. 
 *
 * ��� update ��Ϊ NULL ��
 *
 *  1) ��ô�� key �Ѿ�����ʱ���� *update ��Ϊ 1 ��
 *
 *  2) ��� key δ���ڣ��� *update ��Ϊ 0 ��
 *
 * T = O(N^2)
 */
unsigned char *zipmapSet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char *val, unsigned int vlen, int *update) {
    unsigned int zmlen, offset;
    // ����ڵ�����ĳ���
    unsigned int freelen, reqlen = zipmapRequiredLength(klen,vlen);
    unsigned int empty, vempty;
    unsigned char *p;
   
    freelen = reqlen;
    if (update) *update = 0;
    // �� key �� zipmap �в��ҽڵ�
    // T = O(N^2)
    p = zipmapLookupRaw(zm,key,klen,&zmlen);
    if (p == NULL) {

        /* Key not found: enlarge */
        // key �����ڣ���չ zipmap

        // T = O(N)
        zm = zipmapResize(zm, zmlen+reqlen);
        p = zm+zmlen-1;
        zmlen = zmlen+reqlen;

        /* Increase zipmap length (this is an insert) */
        if (zm[0] < ZIPMAP_BIGLEN) zm[0]++;
    } else {

        /* Key found. Is there enough space for the new value? */
        /* Compute the total length: */
        // ���Ѿ����ڣ����ɵ�ֵ�ռ��С�ܷ�������ֵ
        // ���������Ļ�����չ zipmap ���ƶ�����

        if (update) *update = 1;
        // T = O(1)
        freelen = zipmapRawEntryLength(p);
        if (freelen < reqlen) {

            /* Store the offset of this key within the current zipmap, so
             * it can be resized. Then, move the tail backwards so this
             * pair fits at the current position. */
            // ������пռ䲻������ֵ����ռ䣬��ô�� zipmap ������չ
            // T = O(N)
            offset = p-zm;
            zm = zipmapResize(zm, zmlen-freelen+reqlen);
            p = zm+offset;

            /* The +1 in the number of bytes to be moved is caused by the
             * end-of-zipmap byte. Note: the *original* zmlen is used. */
            // ����ƶ����ݣ�Ϊ�ڵ�ճ����Դ����ֵ�Ŀռ�
            // T = O(N)
            memmove(p+reqlen, p+freelen, zmlen-(offset+freelen+1));
            zmlen = zmlen-freelen+reqlen;
            freelen = reqlen;
        }
    }

    /* We now have a suitable block where the key/value entry can
     * be written. If there is too much free space, move the tail
     * of the zipmap a few bytes to the front and shrink the zipmap,
     * as we want zipmaps to be very space efficient. */
    // ����ڵ����ռ�ĳ��ȣ��������ռ�̫���ˣ��ͽ�������
    empty = freelen-reqlen;
    if (empty >= ZIPMAP_VALUE_MAX_FREE) {
        /* First, move the tail <empty> bytes to the front, then resize
         * the zipmap to be <empty> bytes smaller. */
        offset = p-zm;
        // ǰ�����ݣ����ǿ���ռ�
        // T = O(N)
        memmove(p+reqlen, p+freelen, zmlen-(offset+freelen+1));
        zmlen -= empty;
        // ��С zipmap ���Ƴ�����Ŀռ�
        // T = O(N)
        zm = zipmapResize(zm, zmlen);
        p = zm+offset;
        vempty = 0;
    } else {
        vempty = empty;
    }

    /* Just write the key + value and we are done. */

    /* Key: */
    // д���
    // T = O(N)
    p += zipmapEncodeLength(p,klen);
    memcpy(p,key,klen);
    p += klen;

    /* Value: */
    // д��ֵ
    // T = O(N)
    p += zipmapEncodeLength(p,vlen);
    *p++ = vempty;
    memcpy(p,val,vlen);

    return zm;
}

/* Remove the specified key. If 'deleted' is not NULL the pointed integer is
 * set to 0 if the key was not found, to 1 if it was found and deleted. 
 *
 * �� zipmap ��ɾ���������� key �Ľڵ㡣
 *
 * ��� deleted ������Ϊ NULL ����ô��
 *
 *  1) �����Ϊ key û�ҵ�������ɾ��ʧ�ܣ���ô�� *deleted ��Ϊ 0 ��
 *
 *  2) ��� key �ҵ��ˣ����ҳɹ�����ɾ���ˣ���ô�� *deleted ��Ϊ 1 ��
 *
 * T = O(N^2)
 */
unsigned char *zipmapDel(unsigned char *zm, unsigned char *key, unsigned int klen, int *deleted) {

    unsigned int zmlen, freelen;

    // T = O(N^2)
    unsigned char *p = zipmapLookupRaw(zm,key,klen,&zmlen);
    if (p) {
        // �ҵ�������ɾ��

        // ����ڵ���ܳ�
        freelen = zipmapRawEntryLength(p);
        // �ƶ��ڴ棬���Ǳ�ɾ��������
        // T = O(N)
        memmove(p, p+freelen, zmlen-((p-zm)+freelen+1));
        // ��С zipmap 
        // T = O(N)
        zm = zipmapResize(zm, zmlen-freelen);

        /* Decrease zipmap length */
        // ���� zipmap �Ľڵ�����
        // ע�⣬����ڵ������Ѿ����ڵ��� ZIPMAP_BIGLEN 
        // ��ô���ﲻ����м��٣�ֻ���ڵ��� zipmapLen ��ʱ��
        // �������Ҫ�Ļ�����ȷ�Ľڵ������Żᱻ����
        // �����뿴 zipmapLen ��Դ��
        if (zm[0] < ZIPMAP_BIGLEN) zm[0]--;

        if (deleted) *deleted = 1;
    } else {
        if (deleted) *deleted = 0;
    }

    return zm;
}

/* Call before iterating through elements via zipmapNext() 
 *
 * ��ͨ�� zipmapNext ���� zipmap ֮ǰ����
 *
 * ����ָ�� zipmap �׸��ڵ��ָ�롣
 *
 * T = O(1)
 */
unsigned char *zipmapRewind(unsigned char *zm) {
    return zm+1;
}

/* This function is used to iterate through all the zipmap elements.
 *
 * ����������ڱ��� zipmap ������Ԫ�ء�
 *
 * In the first call the first argument is the pointer to the zipmap + 1.
 *
 * �ڵ�һ�ε����������ʱ�� zm ������ֵΪ zipmap + 1 
 * ��Ҳ���ǣ�ָ�� zipmap �ĵ�һ���ڵ㣩
 *
 * In the next calls what zipmapNext returns is used as first argument.
 *
 * ����֮��ĵ����У� zm ������ֵΪ֮ǰ���� zipmapNext ʱ�����ص�ֵ��
 *
 * Example:
 * 
 * ʾ����
 *
 * unsigned char *i = zipmapRewind(my_zipmap);
 * while((i = zipmapNext(i,&key,&klen,&value,&vlen)) != NULL) {
 *     printf("%d bytes key at $p\n", klen, key);
 *     printf("%d bytes value at $p\n", vlen, value);
 * }
 *
 * T = O(1)
 */
unsigned char *zipmapNext(unsigned char *zm, unsigned char **key, unsigned int *klen, unsigned char **value, unsigned int *vlen) {

    // �ѵ����б�ĩβ��ֹͣ����
    if (zm[0] == ZIPMAP_END) return NULL;

    // ȡ�����������浽 key ������
    if (key) {
        *key = zm;
        *klen = zipmapDecodeLength(zm);
        *key += ZIPMAP_LEN_BYTES(*klen);
    }
    // Խ����
    zm += zipmapRawKeyLength(zm);

    // ȡ��ֵ�������浽 value ������
    if (value) {
        *value = zm+1;
        *vlen = zipmapDecodeLength(zm);
        *value += ZIPMAP_LEN_BYTES(*vlen);
    }
    // Խ��ֵ
    zm += zipmapRawValueLength(zm);

    // ����ָ����һ�ڵ��ָ��
    return zm;
}

/* Search a key and retrieve the pointer and len of the associated value.
 * If the key is found the function returns 1, otherwise 0. 
 *
 * �� zipmap �а� key ���в��ң�
 * ��ֵ��ָ�뱣�浽 *value �У�����ֵ�ĳ��ȱ��浽 *vlen �С�
 *
 * �ɹ��ҵ�ֵʱ�������� 1 ��û�ҵ��򷵻� 0 ��
 *
 * T = O(N^2)
 */
int zipmapGet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char **value, unsigned int *vlen) {
    unsigned char *p;

    // �� zipmap �а� key ����
    // û�ҵ�ֱ�ӷ��� 0 
    // T = O(N^2)
    if ((p = zipmapLookupRaw(zm,key,klen,NULL)) == NULL) return 0;

    // Խ������ָ��ֵ
    p += zipmapRawKeyLength(p);
    // ȡ��ֵ�ĳ���
    *vlen = zipmapDecodeLength(p);
    // �� *value ָ��ֵ�� +1 ΪԽ�� <free> ����
    *value = p + ZIPMAP_LEN_BYTES(*vlen) + 1;

    // �ҵ������� 1 
    return 1;
}

/* Return 1 if the key exists, otherwise 0 is returned. 
 *
 * ������� key ������ zipmap �У���ô���� 1 ���������򷵻� 0 ��
 *
 * T = O(N^2)
 */
int zipmapExists(unsigned char *zm, unsigned char *key, unsigned int klen) {
    return zipmapLookupRaw(zm,key,klen,NULL) != NULL;
}

/* Return the number of entries inside a zipmap 
 *
 * ���� zipmap �а����Ľڵ���
 *
 * T = O(N)
 */
unsigned int zipmapLen(unsigned char *zm) {

    unsigned int len = 0;

    if (zm[0] < ZIPMAP_BIGLEN) {
        // ���ȿ����� 1 �ֽڱ���
        // T = O(1)
        len = zm[0];
    } else {
        // ���Ȳ����� 1 �ֽڱ��棬��Ҫ�������� zipmap
        // T = O(N)
        unsigned char *p = zipmapRewind(zm);
        while((p = zipmapNext(p,NULL,NULL,NULL,NULL)) != NULL) len++;

        /* Re-store length if small enough */
        // ����ֽ������Ѿ����� ZIPMAP_BIGLEN ����ô���½�ֵ���浽 len ��
        // ��������ڽڵ������� ZIPMAP_BIGLEN ֮���нڵ㱻ɾ��ʱ�����
        if (len < ZIPMAP_BIGLEN) zm[0] = len;
    }

    return len;
}

/* Return the raw size in bytes of a zipmap, so that we can serialize
 * the zipmap on disk (or everywhere is needed) just writing the returned
 * amount of bytes of the C array starting at the zipmap pointer. 
 *
 * �������� zipmap ռ�õ��ֽڴ�С
 *
 * T = O(N)
 */
size_t zipmapBlobLen(unsigned char *zm) {

    unsigned int totlen;

    // ��Ȼ zipmapLookupRaw һ������µĸ��Ӷ�Ϊ O(N^2)
    // ���ǵ� key ����Ϊ NULL ʱ������ʹ�� memcmp �������ַ����Ա�
    // zipmapLookupRaw �˻���һ�������ļ��㳤�ȵĺ�����ʹ��
    // ��������£� zipmapLookupRaw �ĸ��Ӷ�Ϊ O(N)
    zipmapLookupRaw(zm,NULL,0,&totlen);

    return totlen;
}

#ifdef ZIPMAP_TEST_MAIN
void zipmapRepr(unsigned char *p) {
    unsigned int l;

    printf("{status %u}",*p++);
    while(1) {
        if (p[0] == ZIPMAP_END) {
            printf("{end}");
            break;
        } else {
            unsigned char e;

            l = zipmapDecodeLength(p);
            printf("{key %u}",l);
            p += zipmapEncodeLength(NULL,l);
            if (l != 0 && fwrite(p,l,1,stdout) == 0) perror("fwrite");
            p += l;

            l = zipmapDecodeLength(p);
            printf("{value %u}",l);
            p += zipmapEncodeLength(NULL,l);
            e = *p++;
            if (l != 0 && fwrite(p,l,1,stdout) == 0) perror("fwrite");
            p += l+e;
            if (e) {
                printf("[");
                while(e--) printf(".");
                printf("]");
            }
        }
    }
    printf("\n");
}

int main(void) {
    unsigned char *zm;

    zm = zipmapNew();

    zm = zipmapSet(zm,(unsigned char*) "name",4, (unsigned char*) "foo",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "surname",7, (unsigned char*) "foo",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "age",3, (unsigned char*) "foo",3,NULL);
    zipmapRepr(zm);

    zm = zipmapSet(zm,(unsigned char*) "hello",5, (unsigned char*) "world!",6,NULL);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "bar",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "!",1,NULL);
    zipmapRepr(zm);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "12345",5,NULL);
    zipmapRepr(zm);
    zm = zipmapSet(zm,(unsigned char*) "new",3, (unsigned char*) "xx",2,NULL);
    zm = zipmapSet(zm,(unsigned char*) "noval",5, (unsigned char*) "",0,NULL);
    zipmapRepr(zm);
    zm = zipmapDel(zm,(unsigned char*) "new",3,NULL);
    zipmapRepr(zm);

    printf("\nLook up large key:\n");
    {
        unsigned char buf[512];
        unsigned char *value;
        unsigned int vlen, i;
        for (i = 0; i < 512; i++) buf[i] = 'a';

        zm = zipmapSet(zm,buf,512,(unsigned char*) "long",4,NULL);
        if (zipmapGet(zm,buf,512,&value,&vlen)) {
            printf("  <long key> is associated to the %d bytes value: %.*s\n",
                vlen, vlen, value);
        }
    }

    printf("\nPerform a direct lookup:\n");
    {
        unsigned char *value;
        unsigned int vlen;

        if (zipmapGet(zm,(unsigned char*) "foo",3,&value,&vlen)) {
            printf("  foo is associated to the %d bytes value: %.*s\n",
                vlen, vlen, value);
        }
    }
    printf("\nIterate through elements:\n");
    {
        unsigned char *i = zipmapRewind(zm);
        unsigned char *key, *value;
        unsigned int klen, vlen;

        while((i = zipmapNext(i,&key,&klen,&value,&vlen)) != NULL) {
            printf("  %d:%.*s => %d:%.*s\n", klen, klen, key, vlen, vlen, value);
        }
    }
    return 0;
}
#endif
