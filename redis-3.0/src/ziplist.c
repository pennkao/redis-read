/* The ziplist is a specially encoded dually linked list that is designed
 * to be very memory efficient. 
 *
 * Ziplist ��Ϊ�˾����ܵؽ�Լ�ڴ����Ƶ��������˫������
 *
 * It stores both strings and integer values,
 * where integers are encoded as actual integers instead of a series of
 * characters. 
 *
 * Ziplist ���Դ����ַ���ֵ������ֵ��
 * ���У�����ֵ������Ϊʵ�ʵ��������������ַ����顣
 *
 * It allows push and pop operations on either side of the list
 * in O(1) time. However, because every operation requires a reallocation of
 * the memory used by the ziplist, the actual complexity is related to the
 * amount of memory used by the ziplist.
 *
 * Ziplist �������б�����˽��� O(1) ���Ӷȵ� push �� pop ������
 * ���ǣ���Ϊ��Щ��������Ҫ������ ziplist �����ڴ��ط��䣬
 * ����ʵ�ʵĸ��ӶȺ� ziplist ռ�õ��ڴ��С�йء�
 *
 * ----------------------------------------------------------------------------
 *
 * ZIPLIST OVERALL LAYOUT:
 * Ziplist �����岼�֣�
 *
 * The general layout of the ziplist is as follows:
 * ������ ziplist ��һ�㲼�֣�
 *
 * <zlbytes><zltail><zllen><entry><entry><zlend>
 *
 * <zlbytes> is an unsigned integer to hold the number of bytes that the
 * ziplist occupies. This value needs to be stored to be able to resize the
 * entire structure without the need to traverse it first.
 *
 * <zlbytes> ��һ���޷��������������� ziplist ʹ�õ��ڴ�������
 *
 * ͨ�����ֵ���������ֱ�Ӷ� ziplist ���ڴ��С���е�����
 * ������Ϊ�˼��� ziplist ���ڴ��С�����������б�
 *
 * <zltail> is the offset to the last entry in the list. This allows a pop
 * operation on the far side of the list without the need for full traversal.
 *
 * <zltail> �����ŵ����б������һ���ڵ��ƫ������
 *
 * ���ƫ����ʹ�öԱ�β�� pop ����������������������б������½��С�
 *
 * <zllen> is the number of entries.When this value is larger than 2**16-2,
 * we need to traverse the entire list to know how many items it holds.
 *
 * <zllen> �������б��еĽڵ�������
 * 
 * �� zllen �����ֵ���� 2**16-2 ʱ��
 * ������Ҫ���������б����֪���б�ʵ�ʰ����˶��ٸ��ڵ㡣
 *
 * <zlend> is a single byte special value, equal to 255, which indicates the
 * end of the list.
 *
 * <zlend> �ĳ���Ϊ 1 �ֽڣ�ֵΪ 255 ����ʶ�б��ĩβ��
 *
 * ZIPLIST ENTRIES:
 * ZIPLIST �ڵ㣺
 *
 * Every entry in the ziplist is prefixed by a header that contains two pieces
 * of information. First, the length of the previous entry is stored to be
 * able to traverse the list from back to front. Second, the encoding with an
 * optional string length of the entry itself is stored.
 *
 * ÿ�� ziplist �ڵ��ǰ�涼����һ�� header ����� header ������������Ϣ��
 *
 * 1)ǰ�ýڵ�ĳ��ȣ��ڳ���Ӻ���ǰ����ʱʹ�á�
 *
 * 2)��ǰ�ڵ��������ֵ�����ͺͳ��ȡ�
 *
 * The length of the previous entry is encoded in the following way:
 * If this length is smaller than 254 bytes, it will only consume a single
 * byte that takes the length as value. When the length is greater than or
 * equal to 254, it will consume 5 bytes. The first byte is set to 254 to
 * indicate a larger value is following. The remaining 4 bytes take the
 * length of the previous entry as value.
 *
 * ����ǰ�ýڵ�ĳ��ȵķ������£�
 *
 * 1) ���ǰ�ýڵ�ĳ���С�� 254 �ֽڣ���ô����ʹ�� 1 ���ֽ��������������ֵ��
 *
 * 2) ���ǰ�ýڵ�ĳ��ȴ��ڵ��� 254 �ֽڣ���ô����ʹ�� 5 ���ֽ��������������ֵ��
 *    a) �� 1 ���ֽڵ�ֵ������Ϊ 254 �����ڱ�ʶ����һ�� 5 �ֽڳ��ĳ���ֵ��
 *    b) ֮��� 4 ���ֽ������ڱ���ǰ�ýڵ��ʵ�ʳ��ȡ�
 *
 * The other header field of the entry itself depends on the contents of the
 * entry. When the entry is a string, the first 2 bits of this header will hold
 * the type of encoding used to store the length of the string, followed by the
 * actual length of the string. When the entry is an integer the first 2 bits
 * are both set to 1. The following 2 bits are used to specify what kind of
 * integer will be stored after this header. An overview of the different
 * types and encodings is as follows:
 *
 * header ��һ���ֵ����ݺͽڵ��������ֵ�йء�
 *
 * 1) ����ڵ㱣������ַ���ֵ��
 *    ��ô�ⲿ�� header ��ͷ 2 ��λ����������ַ���������ʹ�õ����ͣ�
 *    ��֮����ŵ����������ַ�����ʵ�ʳ��ȡ�
 *
 * |00pppppp| - 1 byte
 *      String value with length less than or equal to 63 bytes (6 bits).
 *      �ַ����ĳ���С�ڻ���� 63 �ֽڡ�
 * |01pppppp|qqqqqqqq| - 2 bytes
 *      String value with length less than or equal to 16383 bytes (14 bits).
 *      �ַ����ĳ���С�ڻ���� 16383 �ֽڡ�
 * |10______|qqqqqqqq|rrrrrrrr|ssssssss|tttttttt| - 5 bytes
 *      String value with length greater than or equal to 16384 bytes.
 *      �ַ����ĳ��ȴ��ڻ���� 16384 �ֽڡ�
 *
 * 2) ����ڵ㱣���������ֵ��
 *    ��ô�ⲿ�� header ��ͷ 2 λ����������Ϊ 1 ��
 *    ��֮����ŵ� 2 λ�����ڱ�ʶ�ڵ�����������������͡�
 *
 * |11000000| - 1 byte
 *      Integer encoded as int16_t (2 bytes).
 *      �ڵ��ֵΪ int16_t ���͵�����������Ϊ 2 �ֽڡ�
 * |11010000| - 1 byte
 *      Integer encoded as int32_t (4 bytes).
 *      �ڵ��ֵΪ int32_t ���͵�����������Ϊ 4 �ֽڡ�
 * |11100000| - 1 byte
 *      Integer encoded as int64_t (8 bytes).
 *      �ڵ��ֵΪ int64_t ���͵�����������Ϊ 8 �ֽڡ�
 * |11110000| - 1 byte
 *      Integer encoded as 24 bit signed (3 bytes).
 *      �ڵ��ֵΪ 24 λ��3 �ֽڣ�����������
 * |11111110| - 1 byte
 *      Integer encoded as 8 bit signed (1 byte).
 *      �ڵ��ֵΪ 8 λ��1 �ֽڣ�����������
 * |1111xxxx| - (with xxxx between 0000 and 1101) immediate 4 bit integer.
 *      Unsigned integer from 0 to 12. The encoded value is actually from
 *      1 to 13 because 0000 and 1111 can not be used, so 1 should be
 *      subtracted from the encoded 4 bit value to obtain the right value.
 *      �ڵ��ֵΪ���� 0 �� 12 ֮����޷���������
 *      ��Ϊ 0000 �� 1111 ������ʹ�ã�����λ��ʵ��ֵ���� 1 �� 13 ��
 *      ������ȡ���� 4 ��λ��ֵ֮�󣬻���Ҫ��ȥ 1 �����ܼ������ȷ��ֵ��
 *      ����˵�����λ��ֵΪ 0001 = 1 ����ô���򷵻ص�ֵ���� 1 - 1 = 0 ��
 * |11111111| - End of ziplist.
 *      ziplist �Ľ�β��ʶ
 *
 * All the integers are represented in little endian byte order.
 *
 * ������������ʾΪС���ֽ���
 *
 * ----------------------------------------------------------------------------
 *
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
#include <stdint.h>
#include <limits.h>
#include "zmalloc.h"
#include "util.h"
#include "ziplist.h"
#include "endianconv.h"
#include "redisassert.h"

/*
 * ziplist ĩ�˱�ʶ�����Լ� 5 �ֽڳ����ȱ�ʶ��
 */
#define ZIP_END 255
#define ZIP_BIGLEN 254

/* Different encoding/length possibilities */
/*
 * �ַ���������������������
 */
#define ZIP_STR_MASK 0xc0
#define ZIP_INT_MASK 0x30

/*
 * �ַ�����������
 */
#define ZIP_STR_06B (0 << 6)
#define ZIP_STR_14B (1 << 6)
#define ZIP_STR_32B (2 << 6)

/*
 * ������������
 */
#define ZIP_INT_16B (0xc0 | 0<<4)
#define ZIP_INT_32B (0xc0 | 1<<4)
#define ZIP_INT_64B (0xc0 | 2<<4)
#define ZIP_INT_24B (0xc0 | 3<<4)
#define ZIP_INT_8B 0xfe

/* 4 bit integer immediate encoding 
 *
 * 4 λ������������������
 */
#define ZIP_INT_IMM_MASK 0x0f
#define ZIP_INT_IMM_MIN 0xf1    /* 11110001 */
#define ZIP_INT_IMM_MAX 0xfd    /* 11111101 */
#define ZIP_INT_IMM_VAL(v) (v & ZIP_INT_IMM_MASK)

/*
 * 24 λ���������ֵ����Сֵ
 */
#define INT24_MAX 0x7fffff
#define INT24_MIN (-INT24_MAX - 1)

/* Macro to determine type 
 *
 * �鿴�������� enc �Ƿ��ַ�������
 */
#define ZIP_IS_STR(enc) (((enc) & ZIP_STR_MASK) < ZIP_STR_MASK)








/*  ѹ���б���һ��Ϊ��Լ�ڴ��������˳�������ݽṹ��
ѹ���б��ʽ:
zlbytes+zltail+zllen+entry1+entry2+entry3+...+entryN+zlend(0Xff)

���������һ��ָ��ѹ���б���ʼ��ַ��ָ�� p �� ��ôֻҪ��ָ�� p ����ƫ����zltail �� �Ϳ��Լ������β�ڵ� entryN �ĵ�ַ��


ѹ���б������ɲ��ֵ���ϸ˵��:
����     ����       ����                    ��;

zlbytes  uint32_t   4 �ֽ�      ��¼����ѹ���б�ռ�õ��ڴ��ֽ������ڶ�ѹ���б�����ڴ��ط��䣬 ���߼��� zlend ��λ��ʱʹ�á� 
zltail   uint32_t   4 �ֽ�      ��¼ѹ���б��β�ڵ����ѹ���б����ʼ��ַ�ж����ֽڣ� ͨ�����ƫ���������������������ѹ���б�Ϳ���ȷ����β�ڵ�ĵ�ַ�� 
zllen    uint16_t   2 �ֽ�      ��¼��ѹ���б�����Ľڵ������� ��������Ե�ֵС�� UINT16_MAX ��65535��ʱ�� ������Ե�ֵ����ѹ���б�����ڵ�������� �����ֵ���� UINT16_MAX ʱ�� �ڵ����ʵ������Ҫ��������ѹ���б���ܼ���ó��� 
entryX   �б�ڵ�   ����        ѹ���б�����ĸ����ڵ㣬�ڵ�ĳ����ɽڵ㱣������ݾ����� 
zlend   uint8_t 1   �ֽ�        ����ֵ 0xFF ��ʮ���� 255 �������ڱ��ѹ���б��ĩ�ˡ� 



entry-nѹ���б�ڵ�Ĺ���

ÿ��ѹ���б�ڵ���Ա���һ���ֽ��������һ������ֵ�� ���У� �ֽ�����������������ֳ��ȵ�����һ�֣�
1.����С�ڵ��� 63 ��2^{6}-1���ֽڵ��ֽ����飻
2.����С�ڵ��� 16383 ��2^{14}-1�� �ֽڵ��ֽ����飻
3.����С�ڵ��� 4294967295 ��2^{32}-1���ֽڵ��ֽ����飻

������ֵ��������������ֳ��ȵ�����һ�֣�
1.4 λ�������� 0 �� 12 ֮����޷���������
2.1 �ֽڳ����з���������
3.3 �ֽڳ����з���������
4.int16_t ����������
5.int32_t ����������
6.int64_t ����������

ÿ��ѹ���б�ڵ�entry-n���� previous_entry_length �� encoding �� content ����������ɡ�
previous_entry_length + encoding + content


previous_entry_length:
�ڵ�� previous_entry_length �������ֽ�Ϊ��λ�� ��¼��ѹ���б���ǰһ���ڵ�ĳ��ȡ�
previous_entry_length ���Եĳ��ȿ����� 1 �ֽڻ��� 5 �ֽڣ�
���ǰһ�ڵ�ĳ���С�� 254 �ֽڣ� ��ô previous_entry_length ���Եĳ���Ϊ 1 �ֽڣ� ǰһ�ڵ�ĳ��Ⱦͱ�������һ���ֽ����档
���ǰһ�ڵ�ĳ��ȴ��ڵ��� 254 �ֽڣ� ��ô previous_entry_length ���Եĳ���Ϊ 5 �ֽڣ� �������Եĵ�һ�ֽڻᱻ����Ϊ 0xFE ��ʮ����ֵ 254���� 
    ��֮����ĸ��ֽ������ڱ���ǰһ�ڵ�ĳ��ȡ�

    ѹ���б�Ĵӱ�β���ͷ������������ʹ����һԭ��ʵ�ֵģ� ֻҪ����ӵ����һ��ָ��ĳ���ڵ���ʼ��ַ��ָ�룬 ��ôͨ�����ָ���Լ�����ڵ�� 
previous_entry_length ���ԣ� ����Ϳ���һֱ��ǰһ���ڵ���ݣ� ���յ���ѹ���б�ı�ͷ�ڵ㡣


encoding:
�ڵ�� encoding ���Լ�¼�˽ڵ�� content �������������ݵ������Լ����ȣ�
һ�ֽڡ����ֽڻ������ֽڳ��� ֵ�����λΪ 00 �� 01 ���� 10 �����ֽ�������룺 ���ֱ����ʾ�ڵ�� content ���Ա������ֽ����飬 ����
    �ĳ����ɱ����ȥ�����λ֮�������λ��¼��
һ�ֽڳ��� ֵ�����λ�� 11 ��ͷ�����������룺 ���ֱ����ʾ�ڵ�� content ���Ա���������ֵ�� ����ֵ�����ͺͳ����ɱ����ȥ�����λ֮�������λ��¼��

�ֽ��������


����                                            ���볤��                content ���Ա����ֵ
00bbbbbb                                        1 �ֽ�                  ����С�ڵ��� 63 �ֽڵ��ֽ����顣 
01bbbbbb xxxxxxxx                               2 �ֽ�                  ����С�ڵ��� 16383 �ֽڵ��ֽ����顣 
10______ aaaaaaaa bbbbbbbb cccccccc dddddddd    5 �ֽ�                  ����С�ڵ��� 4294967295 ���ֽ����顣 

��������

����                ���볤��                content ���Ա����ֵ


11000000            1 �ֽ�                  int16_t ���͵������� 
11010000            1 �ֽ�                  int32_t ���͵������� 
11100000            1 �ֽ�                  int64_t ���͵������� 
11110000            1 �ֽ�                  24 λ�з��������� 
11111110            1 �ֽ�                  8 λ�з��������� 
1111xxxx            1 �ֽ�                  ʹ����һ����Ľڵ�û����Ӧ�� content ���ԣ� ��Ϊ���뱾��� xxxx �ĸ�λ�Ѿ�������һ��
                                            ���� 0 �� 12 ֮���ֵ�� ���������� content ���ԡ� 


��������?
��һ��ѹ���б��У� �ж�������ġ����Ƚ��� 250 �ֽڵ� 253 �ֽ�֮��Ľڵ� e1 �� eN ,��Ϊ e1 �� eN �����нڵ�ĳ��ȶ�С�� 254 �ֽڣ� 
���Լ�¼��Щ�ڵ�ĳ���ֻ��Ҫ 1 �ֽڳ��� previous_entry_length ���ԣ� ���仰˵�� e1 �� eN �����нڵ�� previous_entry_length ���Զ��� 1 �ֽڳ��ġ�
��ʱ�� ������ǽ�һ�����ȴ��ڵ��� 254 �ֽڵ��½ڵ� new ����Ϊѹ���б�ı�ͷ�ڵ㣬 ��ô new ����Ϊ e1 ��ǰ�ýڵ�
��Ϊ e1 �� previous_entry_length ���Խ��� 1 �ֽڣ� ��û�취�����½ڵ� new �ĳ��ȣ� ���Գ��򽫶�ѹ���б�ִ�пռ��ط�������� ��
�� e1 �ڵ�� previous_entry_length ���Դ�ԭ���� 1 �ֽڳ���չΪ 5 �ֽڳ���
���ڣ� �鷳���������� ���� e1 ԭ���ĳ��Ƚ��� 250 �ֽ��� 253 �ֽ�֮�䣬 ��Ϊ previous_entry_length ���������ĸ��ֽڵĿռ�֮��
e1 �ĳ��Ⱦͱ���˽��� 254 �ֽ��� 257 �ֽ�֮�䣬 �����ֳ���ʹ�� 1 �ֽڳ��� previous_entry_length ������û�취����ġ�

��ˣ� Ϊ���� e2 �� previous_entry_length ���Կ��Լ�¼�� e1 �ĳ��ȣ� ������Ҫ�ٴζ�ѹ���б�ִ�пռ��ط�������� ���� e2 �ڵ�� 
previous_entry_length ���Դ�ԭ���� 1 �ֽڳ���չΪ 5 �ֽڳ���

��������½ڵ���ܻ�������������֮�⣬ ɾ���ڵ�Ҳ���ܻ������������¡�

��Ϊ������������������Ҫ��ѹ���б�ִ�� N �οռ��ط�������� ��ÿ�οռ��ط��������Ӷ�Ϊ O(N) �� �����������µ�����Ӷ�Ϊ O(N^2) ��
Ҫע����ǣ� �����������µĸ��ӶȽϸߣ� �������������������ļ����Ǻܵ͵ģ�
?���ȣ� ѹ���б���Ҫǡ���ж�������ġ����Ƚ��� 250 �ֽ��� 253 �ֽ�֮��Ľڵ㣬 �������²��п��ܱ������� ��ʵ���У� ����������������
?��Σ� ��ʹ�����������£� ��ֻҪ�����µĽڵ��������࣬ �Ͳ������������κ�Ӱ�죺 ����˵�� ��������ڵ�������������Ǿ��Բ���Ӱ�����ܵģ�
*/

/*
ѹ���б� API

����                        ����            �㷨���Ӷ�


ziplistNew          ����һ���µ�ѹ���б� O(1) 
ziplistPush         ����һ����������ֵ���½ڵ㣬 ��������½ڵ���ӵ�ѹ���б�ı�ͷ���߱�β�� ƽ�� O(N) ��� O(N^2) �� 
ziplistInsert       ����������ֵ���½ڵ���뵽�����ڵ�֮�� ƽ�� O(N) ��� O(N^2) �� 
ziplistIndex        ����ѹ���б���������ϵĽڵ㡣 O(N) 
ziplistFind         ��ѹ���б��в��Ҳ����ذ����˸���ֵ�Ľڵ㡣 ��Ϊ�ڵ��ֵ������һ���ֽ����飬 ���Լ��ڵ�ֵ�͸���ֵ�Ƿ���ͬ�ĸ��Ӷ�Ϊ O(N) �� 
                    �����������б�ĸ��Ӷ���Ϊ O(N^2) �� 
ziplistNext         ���ظ����ڵ����һ���ڵ㡣 O(1) 
ziplistPrev         ���ظ����ڵ��ǰһ���ڵ㡣 O(1) 
ziplistGet          ��ȡ�����ڵ��������ֵ�� O(1) 
ziplistDelete       ��ѹ���б���ɾ�������Ľڵ㡣 ƽ�� O(N) ��� O(N^2) �� 
ziplistDeleteRange  ɾ��ѹ���б��ڸ��������ϵ���������ڵ㡣 ƽ�� O(N) ��� O(N^2) �� 
ziplistBlobLen      ����ѹ���б�Ŀǰռ�õ��ڴ��ֽ����� O(1) 
ziplistLen          ����ѹ���б�Ŀǰ�����Ľڵ������� �ڵ�����С�� 65535 ʱ O(1) �� ���� 65535 ʱ O(N) �� 

��Ϊ ziplistPush �� ziplistInsert �� ziplistDelete �� ziplistDeleteRange �ĸ��������п��ܻ������������£� �������ǵ�����Ӷȶ��� O(N^2) ��
*/








/* Utility macros */
/*
 * ziplist ���Ժ�
 */
// ��λ�� ziplist �� bytes ���ԣ������Լ�¼������ ziplist ��ռ�õ��ڴ��ֽ���
// ����ȡ�� bytes ���Ե�����ֵ������Ϊ bytes ���Ը�����ֵ
#define ZIPLIST_BYTES(zl)       (*((uint32_t*)(zl)))
// ��λ�� ziplist �� offset ���ԣ������Լ�¼�˵����β�ڵ��ƫ����
// ����ȡ�� offset ���Ե�����ֵ������Ϊ offset ���Ը�����ֵ
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t*)((zl)+sizeof(uint32_t))))
// ��λ�� ziplist �� length ���ԣ������Լ�¼�� ziplist �����Ľڵ�����
// ����ȡ�� length ���Ե�����ֵ������Ϊ length ���Ը�����ֵ
#define ZIPLIST_LENGTH(zl)      (*((uint16_t*)((zl)+sizeof(uint32_t)*2)))
// ���� ziplist ��ͷ�Ĵ�С
#define ZIPLIST_HEADER_SIZE     (sizeof(uint32_t)*2+sizeof(uint16_t))
// ����ָ�� ziplist ��һ���ڵ㣨����ʼλ�ã���ָ��
#define ZIPLIST_ENTRY_HEAD(zl)  ((zl)+ZIPLIST_HEADER_SIZE)
// ����ָ�� ziplist ���һ���ڵ㣨����ʼλ�ã���ָ��
#define ZIPLIST_ENTRY_TAIL(zl)  ((zl)+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)))
// ����ָ�� ziplist ĩ�� ZIP_END ������ʼλ�ã���ָ��
#define ZIPLIST_ENTRY_END(zl)   ((zl)+intrev32ifbe(ZIPLIST_BYTES(zl))-1)

/* 
�հ� ziplist ʾ��ͼ

area        |<---- ziplist header ---->|<-- end -->|

size          4 bytes   4 bytes 2 bytes  1 byte
            +---------+--------+-------+-----------+
component   | zlbytes | zltail | zllen | zlend     |
            |         |        |       |           |
value       |  1011   |  1010  |   0   | 1111 1111 |
            +---------+--------+-------+-----------+
                                       ^
                                       |
                               ZIPLIST_ENTRY_HEAD
                                       &
address                        ZIPLIST_ENTRY_TAIL
                                       &
                               ZIPLIST_ENTRY_END

�ǿ� ziplist ʾ��ͼ

area        |<---- ziplist header ---->|<----------- entries ------------->|<-end->|

size          4 bytes  4 bytes  2 bytes    ?        ?        ?        ?     1 byte
            +---------+--------+-------+--------+--------+--------+--------+-------+
component   | zlbytes | zltail | zllen | entry1 | entry2 |  ...   | entryN | zlend |
            +---------+--------+-------+--------+--------+--------+--------+-------+
                                       ^                          ^        ^
address                                |                          |        |
                                ZIPLIST_ENTRY_HEAD                |   ZIPLIST_ENTRY_END
                                                                  |
                                                        ZIPLIST_ENTRY_TAIL
*/

/* We know a positive increment can only be 1 because entries can only be
 * pushed one at a time. */
/*
 * ���� ziplist �Ľڵ���
 *
 * T = O(1)
 */
#define ZIPLIST_INCR_LENGTH(zl,incr) { \
    if (ZIPLIST_LENGTH(zl) < UINT16_MAX) \
        ZIPLIST_LENGTH(zl) = intrev16ifbe(intrev16ifbe(ZIPLIST_LENGTH(zl))+incr); \
}

/*
 * ���� ziplist �ڵ���Ϣ�Ľṹ
 */
typedef struct zlentry {

    // prevrawlen ��ǰ�ýڵ�ĳ���
    // prevrawlensize ������ prevrawlen ������ֽڴ�С
    unsigned int prevrawlensize, prevrawlen;

    // len ����ǰ�ڵ�ֵ�ĳ���
    // lensize ������ len ������ֽڴ�С
    unsigned int lensize, len;

    // ��ǰ�ڵ� header �Ĵ�С
    // ���� prevrawlensize + lensize
    unsigned int headersize;

    // ��ǰ�ڵ�ֵ��ʹ�õı�������
    unsigned char encoding;

    // ָ��ǰ�ڵ��ָ��
    unsigned char *p;

} zlentry;

/* Extract the encoding from the byte pointed by 'ptr' and set it into
 * 'encoding'. 
 *
 * �� ptr ��ȡ���ڵ�ֵ�ı������ͣ����������浽 encoding �����С�
 *
 * T = O(1)
 */
#define ZIP_ENTRY_ENCODING(ptr, encoding) do {  \
    (encoding) = (ptr[0]); \
    if ((encoding) < ZIP_STR_MASK) (encoding) &= ZIP_STR_MASK; \
} while(0)

/* Return bytes needed to store integer encoded by 'encoding' 
 *
 * ���ر��� encoding �����ֵ������ֽ�����
 *
 * T = O(1)
 */
static unsigned int zipIntSize(unsigned char encoding) {

    switch(encoding) {
    case ZIP_INT_8B:  return 1;
    case ZIP_INT_16B: return 2;
    case ZIP_INT_24B: return 3;
    case ZIP_INT_32B: return 4;
    case ZIP_INT_64B: return 8;
    default: return 0; /* 4 bit immediate */
    }

    assert(NULL);
    return 0;
}

/* Encode the length 'l' writing it in 'p'. If p is NULL it just returns
 * the amount of bytes required to encode such a length. 
 *
 * ����ڵ㳤��ֵ l ��������д�뵽 p �У�Ȼ�󷵻ر��� l ������ֽ�������
 *
 * ��� p Ϊ NULL ����ô�����ر��� l ������ֽ�������������д�롣
 *
 * T = O(1)
 */
static unsigned int zipEncodeLength(unsigned char *p, unsigned char encoding, unsigned int rawlen) {
    unsigned char len = 1, buf[5];

    // �����ַ���
    if (ZIP_IS_STR(encoding)) {
        /* Although encoding is given it may not be set for strings,
         * so we determine it here using the raw length. */
        if (rawlen <= 0x3f) {
            if (!p) return len;
            buf[0] = ZIP_STR_06B | rawlen;
        } else if (rawlen <= 0x3fff) {
            len += 1;
            if (!p) return len;
            buf[0] = ZIP_STR_14B | ((rawlen >> 8) & 0x3f);
            buf[1] = rawlen & 0xff;
        } else {
            len += 4;
            if (!p) return len;
            buf[0] = ZIP_STR_32B;
            buf[1] = (rawlen >> 24) & 0xff;
            buf[2] = (rawlen >> 16) & 0xff;
            buf[3] = (rawlen >> 8) & 0xff;
            buf[4] = rawlen & 0xff;
        }

    // ��������
    } else {
        /* Implies integer encoding, so length is always 1. */
        if (!p) return len;
        buf[0] = encoding;
    }

    /* Store this length at p */
    // �������ĳ���д�� p 
    memcpy(p,buf,len);

    // ���ر���������ֽ���
    return len;
}

/* Decode the length encoded in 'ptr'. The 'encoding' variable will hold the
 * entries encoding, the 'lensize' variable will hold the number of bytes
 * required to encode the entries length, and the 'len' variable will hold the
 * entries length. 
 *
 * ���� ptr ָ�룬ȡ���б�ڵ�������Ϣ���������Ǳ��������±����У�
 *
 * - encoding ����ڵ�ֵ�ı������͡�
 *
 * - lensize �������ڵ㳤��������ֽ�����
 *
 * - len ����ڵ�ĳ��ȡ�
 *
 * T = O(1)
 */
#define ZIP_DECODE_LENGTH(ptr, encoding, lensize, len) do {                    \
                                                                               \
    /* ȡ��ֵ�ı������� */                                                     \
    ZIP_ENTRY_ENCODING((ptr), (encoding));                                     \
                                                                               \
    /* �ַ������� */                                                           \
    if ((encoding) < ZIP_STR_MASK) {                                           \
        if ((encoding) == ZIP_STR_06B) {                                       \
            (lensize) = 1;                                                     \
            (len) = (ptr)[0] & 0x3f;                                           \
        } else if ((encoding) == ZIP_STR_14B) {                                \
            (lensize) = 2;                                                     \
            (len) = (((ptr)[0] & 0x3f) << 8) | (ptr)[1];                       \
        } else if (encoding == ZIP_STR_32B) {                                  \
            (lensize) = 5;                                                     \
            (len) = ((ptr)[1] << 24) |                                         \
                    ((ptr)[2] << 16) |                                         \
                    ((ptr)[3] <<  8) |                                         \
                    ((ptr)[4]);                                                \
        } else {                                                               \
            assert(NULL);                                                      \
        }                                                                      \
                                                                               \
    /* �������� */                                                             \
    } else {                                                                   \
        (lensize) = 1;                                                         \
        (len) = zipIntSize(encoding);                                          \
    }                                                                          \
} while(0);

/* Encode the length of the previous entry and write it to "p". Return the
 * number of bytes needed to encode this length if "p" is NULL. 
 *
 * ��ǰ�ýڵ�ĳ��� len ���б��룬������д�뵽 p �У�
 * Ȼ�󷵻ر��� len ������ֽ�������
 *
 * ��� p Ϊ NULL ����ô������д�룬�����ر��� len ������ֽ�������
 *
 * T = O(1)
 */
static unsigned int zipPrevEncodeLength(unsigned char *p, unsigned int len) {

    // �����ر��� len ������ֽ�����
    if (p == NULL) {
        return (len < ZIP_BIGLEN) ? 1 : sizeof(len)+1;

    // д�벢���ر��� len ������ֽ�����
    } else {

        // 1 �ֽ�
        if (len < ZIP_BIGLEN) {
            p[0] = len;
            return 1;

        // 5 �ֽ�
        } else {
            // ��� 5 �ֽڳ��ȱ�ʶ
            p[0] = ZIP_BIGLEN;
            // д�����
            memcpy(p+1,&len,sizeof(len));
            // ����б�Ҫ�Ļ������д�С��ת��
            memrev32ifbe(p+1);
            // ���ر��볤��
            return 1+sizeof(len);
        }
    }
}

/* Encode the length of the previous entry and write it to "p". This only
 * uses the larger encoding (required in __ziplistCascadeUpdate). 
 *
 * ��ԭ��ֻ��Ҫ 1 ���ֽ��������ǰ�ýڵ㳤�� len ������һ�� 5 �ֽڳ��� header �С�
 *
 * T = O(1)
 */
static void zipPrevEncodeLengthForceLarge(unsigned char *p, unsigned int len) {

    if (p == NULL) return;

    // ���� 5 �ֽڳ��ȱ�ʶ
    p[0] = ZIP_BIGLEN;

    // д�� len
    memcpy(p+1,&len,sizeof(len));
    memrev32ifbe(p+1);
}

/* Decode the number of bytes required to store the length of the previous
 * element, from the perspective of the entry pointed to by 'ptr'. 
 *
 * ���� ptr ָ�룬
 * ȡ������ǰ�ýڵ㳤��������ֽ��������������浽 prevlensize �����С�
 *
 * T = O(1)
 */
#define ZIP_DECODE_PREVLENSIZE(ptr, prevlensize) do {                          \
    if ((ptr)[0] < ZIP_BIGLEN) {                                               \
        (prevlensize) = 1;                                                     \
    } else {                                                                   \
        (prevlensize) = 5;                                                     \
    }                                                                          \
} while(0);

/* Decode the length of the previous element, from the perspective of the entry
 * pointed to by 'ptr'. 
 *
 * ���� ptr ָ�룬
 * ȡ������ǰ�ýڵ㳤��������ֽ�����
 * ��������ֽ������浽 prevlensize �С�
 *
 * Ȼ����� prevlensize ���� ptr ��ȡ��ǰ�ýڵ�ĳ���ֵ��
 * �����������ֵ���浽 prevlen �����С�
 *
 * T = O(1)
 */
#define ZIP_DECODE_PREVLEN(ptr, prevlensize, prevlen) do {                     \
                                                                               \
    /* �ȼ��㱻���볤��ֵ���ֽ��� */                                           \
    ZIP_DECODE_PREVLENSIZE(ptr, prevlensize);                                  \
                                                                               \
    /* �ٸ��ݱ����ֽ�����ȡ������ֵ */                                         \
    if ((prevlensize) == 1) {                                                  \
        (prevlen) = (ptr)[0];                                                  \
    } else if ((prevlensize) == 5) {                                           \
        assert(sizeof((prevlensize)) == 4);                                    \
        memcpy(&(prevlen), ((char*)(ptr)) + 1, 4);                             \
        memrev32ifbe(&prevlen);                                                \
    }                                                                          \
} while(0);

/* Return the difference in number of bytes needed to store the length of the
 * previous element 'len', in the entry pointed to by 'p'. 
 *
 * ��������µ�ǰ�ýڵ㳤�� len ������ֽ�����
 * ��ȥ���� p ԭ����ǰ�ýڵ㳤��������ֽ���֮�
 *
 * T = O(1)
 */
static int zipPrevLenByteDiff(unsigned char *p, unsigned int len) {
    unsigned int prevlensize;

    // ȡ������ԭ����ǰ�ýڵ㳤��������ֽ���
    // T = O(1)
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);

    // ������� len ������ֽ�����Ȼ����м�������
    // T = O(1)
    return zipPrevEncodeLength(NULL, len) - prevlensize;
}

/* Return the total number of bytes used by the entry pointed to by 'p'. 
 *
 * ����ָ�� p ��ָ��Ľڵ�ռ�õ��ֽ����ܺ͡�
 *
 * T = O(1)
 */
static unsigned int zipRawEntryLength(unsigned char *p) {
    unsigned int prevlensize, encoding, lensize, len;

    // ȡ������ǰ�ýڵ�ĳ���������ֽ���
    // T = O(1)
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);

    // ȡ����ǰ�ڵ�ֵ�ı������ͣ�����ڵ�ֵ����������ֽ������Լ��ڵ�ֵ�ĳ���
    // T = O(1)
    ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);

    // ����ڵ�ռ�õ��ֽ����ܺ�
    return prevlensize + lensize + len;
}

/* Check if string pointed to by 'entry' can be encoded as an integer.
 * Stores the integer value in 'v' and its encoding in 'encoding'. 
 *
 * ��� entry ��ָ����ַ����ܷ񱻱���Ϊ������
 *
 * ������ԵĻ���
 * ������������������ָ�� v ��ֵ�У���������ķ�ʽ������ָ�� encoding ��ֵ�С�
 *
 * ע�⣬����� entry ��ǰ�����ڵ�� entry ����һ����˼��
 *
 * T = O(N)
 */
static int zipTryEncoding(unsigned char *entry, unsigned int entrylen, long long *v, unsigned char *encoding) {
    long long value;

    // ����̫����̫�̵��ַ���
    if (entrylen >= 32 || entrylen == 0) return 0;

    // ����ת��
    // T = O(N)
    if (string2ll((char*)entry,entrylen,&value)) {

        /* Great, the string can be encoded. Check what's the smallest
         * of our encoding types that can hold this value. */
        // ת���ɹ����Դ�С�����˳�����ʺ�ֵ value �ı��뷽ʽ
        if (value >= 0 && value <= 12) {
            *encoding = ZIP_INT_IMM_MIN+value;
        } else if (value >= INT8_MIN && value <= INT8_MAX) {
            *encoding = ZIP_INT_8B;
        } else if (value >= INT16_MIN && value <= INT16_MAX) {
            *encoding = ZIP_INT_16B;
        } else if (value >= INT24_MIN && value <= INT24_MAX) {
            *encoding = ZIP_INT_24B;
        } else if (value >= INT32_MIN && value <= INT32_MAX) {
            *encoding = ZIP_INT_32B;
        } else {
            *encoding = ZIP_INT_64B;
        }

        // ��¼ֵ��ָ��
        *v = value;

        // ����ת���ɹ���ʶ
        return 1;
    }

    // ת��ʧ��
    return 0;
}

/* Store integer 'value' at 'p', encoded as 'encoding' 
 * 
 * �� encoding ָ���ı��뷽ʽ��������ֵ value д�뵽 p ��
 *
 * T = O(1)
 */
static void zipSaveInteger(unsigned char *p, int64_t value, unsigned char encoding) {
    int16_t i16;
    int32_t i32;
    int64_t i64;

    if (encoding == ZIP_INT_8B) {
        ((int8_t*)p)[0] = (int8_t)value;
    } else if (encoding == ZIP_INT_16B) {
        i16 = value;
        memcpy(p,&i16,sizeof(i16));
        memrev16ifbe(p);
    } else if (encoding == ZIP_INT_24B) {
        i32 = value<<8;
        memrev32ifbe(&i32);
        memcpy(p,((uint8_t*)&i32)+1,sizeof(i32)-sizeof(uint8_t));
    } else if (encoding == ZIP_INT_32B) {
        i32 = value;
        memcpy(p,&i32,sizeof(i32));
        memrev32ifbe(p);
    } else if (encoding == ZIP_INT_64B) {
        i64 = value;
        memcpy(p,&i64,sizeof(i64));
        memrev64ifbe(p);
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        /* Nothing to do, the value is stored in the encoding itself. */
    } else {
        assert(NULL);
    }
}

/* Read integer encoded as 'encoding' from 'p'
 * 
 * �� encoding ָ���ı��뷽ʽ����ȡ������ָ�� p �е�����ֵ��
 *
 * T = O(1)
 */
static int64_t zipLoadInteger(unsigned char *p, unsigned char encoding) {
    int16_t i16;
    int32_t i32;
    int64_t i64, ret = 0;

    if (encoding == ZIP_INT_8B) {
        ret = ((int8_t*)p)[0];
    } else if (encoding == ZIP_INT_16B) {
        memcpy(&i16,p,sizeof(i16));
        memrev16ifbe(&i16);
        ret = i16;
    } else if (encoding == ZIP_INT_32B) {
        memcpy(&i32,p,sizeof(i32));
        memrev32ifbe(&i32);
        ret = i32;
    } else if (encoding == ZIP_INT_24B) {
        i32 = 0;
        memcpy(((uint8_t*)&i32)+1,p,sizeof(i32)-sizeof(uint8_t));
        memrev32ifbe(&i32);
        ret = i32>>8;
    } else if (encoding == ZIP_INT_64B) {
        memcpy(&i64,p,sizeof(i64));
        memrev64ifbe(&i64);
        ret = i64;
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        ret = (encoding & ZIP_INT_IMM_MASK)-1;
    } else {
        assert(NULL);
    }

    return ret;
}

/* Return a struct with all information about an entry. 
 *
 * �� p ��ָ����б�ڵ����Ϣȫ�����浽 zlentry �У������ظ� zlentry ��
 *
 * T = O(1)
 */
static zlentry zipEntry(unsigned char *p) {
    zlentry e;

    // e.prevrawlensize �����ű���ǰһ���ڵ�ĳ���������ֽ���
    // e.prevrawlen ������ǰһ���ڵ�ĳ���
    // T = O(1)
    ZIP_DECODE_PREVLEN(p, e.prevrawlensize, e.prevrawlen);

    // p + e.prevrawlensize ��ָ���ƶ����б�ڵ㱾��
    // e.encoding �����Žڵ�ֵ�ı�������
    // e.lensize �����ű���ڵ�ֵ����������ֽ���
    // e.len �����Žڵ�ֵ�ĳ���
    // T = O(1)
    ZIP_DECODE_LENGTH(p + e.prevrawlensize, e.encoding, e.lensize, e.len);

    // ����ͷ�����ֽ���
    e.headersize = e.prevrawlensize + e.lensize;

    // ��¼ָ��
    e.p = p;

    return e;
}

/* Create a new empty ziplist. 
 *
 * ����������һ���µ� ziplist 
 *
 * T = O(1)
 */
unsigned char *ziplistNew(void) {

    // ZIPLIST_HEADER_SIZE �� ziplist ��ͷ�Ĵ�С
    // 1 �ֽ��Ǳ�ĩ�� ZIP_END �Ĵ�С
    unsigned int bytes = ZIPLIST_HEADER_SIZE+1;

    // Ϊ��ͷ�ͱ�ĩ�˷���ռ�
    unsigned char *zl = zmalloc(bytes);

    // ��ʼ��������
    ZIPLIST_BYTES(zl) = intrev32ifbe(bytes);
    ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(ZIPLIST_HEADER_SIZE);
    ZIPLIST_LENGTH(zl) = 0;

    // ���ñ�ĩ��
    zl[bytes-1] = ZIP_END;

    return zl;
}

/* Resize the ziplist. 
 *
 * ���� ziplist �Ĵ�СΪ len �ֽڡ�
 *
 * �� ziplist ԭ�еĴ�СС�� len ʱ����չ ziplist ����ı� ziplist ԭ�е�Ԫ�ء�
 *
 * T = O(N)
 */
static unsigned char *ziplistResize(unsigned char *zl, unsigned int len) {

    // �� zrealloc ����չʱ���ı�����Ԫ��
    zl = zrealloc(zl,len);

    // ���� bytes ����
    ZIPLIST_BYTES(zl) = intrev32ifbe(len);

    // �������ñ�ĩ��
    zl[len-1] = ZIP_END;

    return zl;
}

/* When an entry is inserted, we need to set the prevlen field of the next
 * entry to equal the length of the inserted entry. It can occur that this
 * length cannot be encoded in 1 byte and the next entry needs to be grow
 * a bit larger to hold the 5-byte encoded prevlen. This can be done for free,
 * because this only happens when an entry is already being inserted (which
 * causes a realloc and memmove). However, encoding the prevlen may require
 * that this entry is grown as well. This effect may cascade throughout
 * the ziplist when there are consecutive entries with a size close to
 * ZIP_BIGLEN, so we need to check that the prevlen can be encoded in every
 * consecutive entry.
 *
 * ����һ���½ڵ���ӵ�ĳ���ڵ�֮ǰ��ʱ��
 * ���ԭ�ڵ�� header �ռ䲻���Ա����½ڵ�ĳ��ȣ�
 * ��ô����Ҫ��ԭ�ڵ�� header �ռ������չ���� 1 �ֽ���չ�� 5 �ֽڣ���
 *
 * ���ǣ�����ԭ�ڵ������չ֮��ԭ�ڵ����һ���ڵ�� prevlen ���ܳ��ֿռ䲻�㣬
 * ��������ڶ�������ڵ�ĳ��ȶ��ӽ� ZIP_BIGLEN ʱ���ܷ�����
 *
 * ������������ڼ�鲢�޸������ڵ�Ŀռ����⡣
 *
 * Note that this effect can also happen in reverse, where the bytes required
 * to encode the prevlen field can shrink. This effect is deliberately ignored,
 * because it can cause a "flapping" effect where a chain prevlen fields is
 * first grown and then shrunk again after consecutive inserts. Rather, the
 * field is allowed to stay larger than necessary, because a large prevlen
 * field implies the ziplist is holding large entries anyway.
 *
 * ������˵��
 * ��Ϊ�ڵ�ĳ��ȱ�С�������������СҲ�ǿ��ܳ��ֵģ�
 * ������Ϊ�˱�����չ-��С-��չ-��С����������������֣�flapping����������
 * ���ǲ���������������������� prevlen ������ĳ��ȸ�����
 
 * The pointer "p" points to the first entry that does NOT need to be
 * updated, i.e. consecutive fields MAY need an update. 
 *
 * ע�⣬����ļ������� p �ĺ����ڵ㣬������ p ��ָ��Ľڵ㡣
 * ��Ϊ�ڵ� p �ڴ���֮ǰ�Ѿ����������Ŀռ���չ������
 *
 * T = O(N^2)
 */
static unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p) {
    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), rawlen, rawlensize;
    size_t offset, noffset, extra;
    unsigned char *np;
    zlentry cur, next;

    // T = O(N^2)
    while (p[0] != ZIP_END) {

        // �� p ��ָ��Ľڵ����Ϣ���浽 cur �ṹ��
        cur = zipEntry(p);
        // ��ǰ�ڵ�ĳ���
        rawlen = cur.headersize + cur.len;
        // ������뵱ǰ�ڵ�ĳ���������ֽ���
        // T = O(1)
        rawlensize = zipPrevEncodeLength(NULL,rawlen);

        /* Abort if there is no next entry. */
        // ����Ѿ�û�к����ռ���Ҫ�����ˣ�����
        if (p[rawlen] == ZIP_END) break;

        // ȡ�������ڵ����Ϣ�����浽 next �ṹ��
        // T = O(1)
        next = zipEntry(p+rawlen);

        /* Abort when "prevlen" has not changed. */
        // �����ڵ���뵱ǰ�ڵ�Ŀռ��Ѿ��㹻�������ٽ����κδ�������
        // ����֤����ֻҪ����һ���ռ��㹻�Ľڵ㣬
        // ��ô����ڵ�֮������нڵ�Ŀռ䶼���㹻��
        if (next.prevrawlen == rawlen) break;

        if (next.prevrawlensize < rawlensize) {

            /* The "prevlen" field of "next" needs more bytes to hold
             * the raw length of "cur". */
            // ִ�е������ʾ next �ռ�Ĵ�С�����Ա��� cur �ĳ���
            // ���Գ�����Ҫ�� next �ڵ�ģ�header ���֣��ռ������չ

            // ��¼ p ��ƫ����
            offset = p-zl;
            // ������Ҫ���ӵĽڵ�����
            extra = rawlensize-next.prevrawlensize;
            // ��չ zl �Ĵ�С
            // T = O(N)
            zl = ziplistResize(zl,curlen+extra);
            // ��ԭָ�� p
            p = zl+offset;

            /* Current pointer and offset for next element. */
            // ��¼��һ�ڵ��ƫ����
            np = p+rawlen;
            noffset = np-zl;

            /* Update tail offset when next element is not the tail element. */
            // �� next �ڵ㲻�Ǳ�β�ڵ�ʱ�������б���β�ڵ��ƫ����
            // 
            // ���ø��µ������next Ϊ��β�ڵ㣩��
            //
            // |     | next |      ==>    |     | new next          |
            //       ^                          ^
            //       |                          |
            //     tail                        tail
            //
            // ��Ҫ���µ������next ���Ǳ�β�ڵ㣩��
            //
            // | next |     |   ==>     | new next          |     |
            //        ^                        ^
            //        |                        |
            //    old tail                 old tail
            // 
            // ����֮��
            //
            // | new next          |     |
            //                     ^
            //                     |
            //                  new tail
            // T = O(1)
            if ((zl+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))) != np) {
                ZIPLIST_TAIL_OFFSET(zl) =
                    intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+extra);
            }

            /* Move the tail to the back. */
            // ����ƶ� cur �ڵ�֮������ݣ�Ϊ cur ���� header �ڳ��ռ�
            //
            // ʾ����
            //
            // | header | value |  ==>  | header |    | value |  ==>  | header      | value |
            //                                   |<-->|
            //                            Ϊ�� header �ڳ��Ŀռ�
            // T = O(N)
            memmove(np+rawlensize,
                np+next.prevrawlensize,
                curlen-noffset-next.prevrawlensize-1);
            // ���µ�ǰһ�ڵ㳤��ֵ������µ� next �ڵ�� header
            // T = O(1)
            zipPrevEncodeLength(np,rawlen);

            /* Advance the cursor */
            // �ƶ�ָ�룬���������¸��ڵ�
            p += rawlen;
            curlen += extra;
        } else {
            if (next.prevrawlensize > rawlensize) {
                /* This would result in shrinking, which we want to avoid.
                 * So, set "rawlen" in the available bytes. */
                // ִ�е����˵�� next �ڵ����ǰ�ýڵ�� header �ռ��� 5 �ֽ�
                // ������ rawlen ֻ��Ҫ 1 �ֽ�
                // ���ǳ��򲻻�� next ������С��
                // ��������ֻ�� rawlen д�� 5 �ֽڵ� header �о����ˡ�
                // T = O(1)
                zipPrevEncodeLengthForceLarge(p+rawlen,rawlen);
            } else {
                // ���е����
                // ˵�� cur �ڵ�ĳ������ÿ��Ա��뵽 next �ڵ�� header ��
                // T = O(1)
                zipPrevEncodeLength(p+rawlen,rawlen);
            }

            /* Stop here, as the raw length of "next" has not changed. */
            break;
        }
    }

    return zl;
}

/* Delete "num" entries, starting at "p". Returns pointer to the ziplist. 
 *
 * ��λ�� p ��ʼ������ɾ�� num ���ڵ㡣
 *
 * �����ķ���ֵΪ����ɾ������֮��� ziplist ��
 *
 * T = O(N^2)
 */
static unsigned char *__ziplistDelete(unsigned char *zl, unsigned char *p, unsigned int num) {
    unsigned int i, totlen, deleted = 0;
    size_t offset;
    int nextdiff = 0;
    zlentry first, tail;

    // ���㱻ɾ���ڵ��ܹ�ռ�õ��ڴ��ֽ���
    // �Լ���ɾ���ڵ���ܸ���
    // T = O(N)
    first = zipEntry(p);
    for (i = 0; p[0] != ZIP_END && i < num; i++) {
        p += zipRawEntryLength(p);
        deleted++;
    }

    // totlen �����б�ɾ���ڵ��ܹ�ռ�õ��ڴ��ֽ���
    totlen = p-first.p;
    if (totlen > 0) {
        if (p[0] != ZIP_END) {

            // ִ�������ʾ��ɾ���ڵ�֮����Ȼ�нڵ����

            /* Storing `prevrawlen` in this entry may increase or decrease the
             * number of bytes required compare to the current `prevrawlen`.
             * There always is room to store this, because it was previously
             * stored by an entry that is now being deleted. */
            // ��Ϊλ�ڱ�ɾ����Χ֮��ĵ�һ���ڵ�� header ���ֵĴ�С
            // �������ɲ����µ�ǰ�ýڵ㣬������Ҫ�����¾�ǰ�ýڵ�֮����ֽ�����
            // T = O(1)
            nextdiff = zipPrevLenByteDiff(p,first.prevrawlen);
            // �������Ҫ�Ļ�����ָ�� p ���� nextdiff �ֽڣ�Ϊ�� header �ճ��ռ�
            p -= nextdiff;
            // �� first ��ǰ�ýڵ�ĳ��ȱ����� p ��
            // T = O(1)
            zipPrevEncodeLength(p,first.prevrawlen);

            /* Update offset for tail */
            // ���µ����β��ƫ����
            // T = O(1)
            ZIPLIST_TAIL_OFFSET(zl) =
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))-totlen);

            /* When the tail contains more than one entry, we need to take
             * "nextdiff" in account as well. Otherwise, a change in the
             * size of prevlen doesn't have an effect on the *tail* offset. */
            // �����ɾ���ڵ�֮���ж���һ���ڵ�
            // ��ô������Ҫ�� nextdiff ��¼���ֽ���Ҳ���㵽��βƫ������
            // ���������ñ�βƫ������ȷ�����β�ڵ�
            // T = O(1)
            tail = zipEntry(p);
            if (p[tail.headersize+tail.len] != ZIP_END) {
                ZIPLIST_TAIL_OFFSET(zl) =
                   intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
            }

            /* Move tail to the front of the ziplist */
            // �ӱ�β���ͷ�ƶ����ݣ����Ǳ�ɾ���ڵ������
            // T = O(N)
            memmove(first.p,p,
                intrev32ifbe(ZIPLIST_BYTES(zl))-(p-zl)-1);
        } else {

            // ִ�������ʾ��ɾ���ڵ�֮���Ѿ�û�������ڵ���

            /* The entire tail was deleted. No need to move memory. */
            // T = O(1)
            ZIPLIST_TAIL_OFFSET(zl) =
                intrev32ifbe((first.p-zl)-first.prevrawlen);
        }

        /* Resize and update length */
        // ��С������ ziplist �ĳ���
        offset = first.p-zl;
        zl = ziplistResize(zl, intrev32ifbe(ZIPLIST_BYTES(zl))-totlen+nextdiff);
        ZIPLIST_INCR_LENGTH(zl,-deleted);
        p = zl+offset;

        /* When nextdiff != 0, the raw length of the next entry has changed, so
         * we need to cascade the update throughout the ziplist */
        // ��� p ��ָ��Ľڵ�Ĵ�С�Ѿ��������ô���м�������
        // ��� p ֮������нڵ��Ƿ���� ziplist �ı���Ҫ��
        // T = O(N^2)
        if (nextdiff != 0)
            zl = __ziplistCascadeUpdate(zl,p);
    }

    return zl;
}

/* Insert item at "p". */
/*
 * ����ָ�� p ��ָ����λ�ã�������Ϊ slen ���ַ��� s ���뵽 zl �С�
 *
 * �����ķ���ֵΪ��ɲ������֮��� ziplist
 *
 * T = O(N^2)
 */
static unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {
    // ��¼��ǰ ziplist �ĳ���
    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), reqlen, prevlen = 0;
    size_t offset;
    int nextdiff = 0;
    unsigned char encoding = 0;
    long long value = 123456789; /* initialized to avoid warning. Using a value
                                    that is easy to see if for some reason
                                    we use it uninitialized. */
    zlentry entry, tail;

    /* Find out prevlen for the entry that is inserted. */
    if (p[0] != ZIP_END) {
        // ��� p[0] ��ָ���б�ĩ�ˣ�˵���б�ǿգ����� p ��ָ���б������һ���ڵ�
        // ��ôȡ�� p ��ָ��ڵ����Ϣ�����������浽 entry �ṹ��
        // Ȼ���� prevlen ������¼ǰ�ýڵ�ĳ���
        // ���������½ڵ�֮�� p ��ָ��Ľڵ�ͳ����½ڵ��ǰ�ýڵ㣩
        // T = O(1)
        entry = zipEntry(p);
        prevlen = entry.prevrawlen;
    } else {
        // ��� p ָ���βĩ�ˣ���ô������Ҫ����б��Ƿ�Ϊ��
        // 1)��� ptail Ҳָ�� ZIP_END ����ô�б�Ϊ�գ�
        // 2)����б�Ϊ�գ���ô ptail ��ָ���б�����һ���ڵ㡣
        unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);
        if (ptail[0] != ZIP_END) {
            // ��β�ڵ�Ϊ�½ڵ��ǰ�ýڵ�

            // ȡ����β�ڵ�ĳ���
            // T = O(1)
            prevlen = zipRawEntryLength(ptail);
        }
    }

    /* See if the entry can be encoded */
    // ���Կ��ܷ������ַ���ת��Ϊ����������ɹ��Ļ���
    // 1)value ������ת���������ֵ
    // 2)encoding �򱣴������� value �ı��뷽ʽ
    // ����ʹ��ʲô���룬 reqlen ������ڵ�ֵ�ĳ���
    // T = O(N)
    if (zipTryEncoding(s,slen,&value,&encoding)) {
        /* 'encoding' is set to the appropriate integer encoding */
        reqlen = zipIntSize(encoding);
    } else {
        /* 'encoding' is untouched, however zipEncodeLength will use the
         * string length to figure out how to encode it. */
        reqlen = slen;
    }
    /* We need space for both the length of the previous entry and
     * the length of the payload. */
    // �������ǰ�ýڵ�ĳ�������Ĵ�С
    // T = O(1)
    reqlen += zipPrevEncodeLength(NULL,prevlen);
    // ������뵱ǰ�ڵ�ֵ����Ĵ�С
    // T = O(1)
    reqlen += zipEncodeLength(NULL,encoding,slen);

    /* When the insert position is not equal to the tail, we need to
     * make sure that the next entry can hold this entry's length in
     * its prevlen field. */
    // ֻҪ�½ڵ㲻�Ǳ���ӵ��б�ĩ�ˣ�
    // ��ô�������Ҫ��鿴 p ��ָ��Ľڵ㣨�� header���ܷ�����½ڵ�ĳ��ȡ�
    // nextdiff �������¾ɱ���֮����ֽڴ�С�������ֵ���� 0 
    // ��ô˵����Ҫ�� p ��ָ��Ľڵ㣨�� header ��������չ
    // T = O(1)
    nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p,reqlen) : 0;

    /* Store offset because a realloc may change the address of zl. */
    // ��Ϊ�ط���ռ���ܻ�ı� zl �ĵ�ַ
    // �����ڷ���֮ǰ����Ҫ��¼ zl �� p ��ƫ������Ȼ���ڷ���֮������ƫ������ԭ p 
    offset = p-zl;
    // curlen �� ziplist ԭ���ĳ���
    // reqlen �������½ڵ�ĳ���
    // nextdiff ���½ڵ�ĺ�̽ڵ���չ header �ĳ��ȣ�Ҫô 0 �ֽڣ�Ҫô 4 ���ֽڣ�
    // T = O(N)
    zl = ziplistResize(zl,curlen+reqlen+nextdiff);
    p = zl+offset;

    /* Apply memory move when necessary and update tail offset. */
    if (p[0] != ZIP_END) {
        // ��Ԫ��֮���нڵ㣬��Ϊ��Ԫ�صļ��룬��Ҫ����Щԭ�нڵ���е���

        /* Subtract one because of the ZIP_END bytes */
        // �ƶ�����Ԫ�أ�Ϊ��Ԫ�صĲ���ռ��ڳ�λ��
        // T = O(N)
        memmove(p+reqlen,p-nextdiff,curlen-offset-1+nextdiff);

        /* Encode this entry's raw length in the next entry. */
        // ���½ڵ�ĳ��ȱ��������ýڵ�
        // p+reqlen ��λ�����ýڵ�
        // reqlen ���½ڵ�ĳ���
        // T = O(1)
        zipPrevEncodeLength(p+reqlen,reqlen);

        /* Update offset for tail */
        // ���µ����β��ƫ���������½ڵ�ĳ���Ҳ����
        ZIPLIST_TAIL_OFFSET(zl) =
            intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+reqlen);

        /* When the tail contains more than one entry, we need to take
         * "nextdiff" in account as well. Otherwise, a change in the
         * size of prevlen doesn't have an effect on the *tail* offset. */
        // ����½ڵ�ĺ����ж���һ���ڵ�
        // ��ô������Ҫ�� nextdiff ��¼���ֽ���Ҳ���㵽��βƫ������
        // ���������ñ�βƫ������ȷ�����β�ڵ�
        // T = O(1)
        tail = zipEntry(p+reqlen);
        if (p[reqlen+tail.headersize+tail.len] != ZIP_END) {
            ZIPLIST_TAIL_OFFSET(zl) =
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
        }
    } else {
        /* This element will be the new tail. */
        // ��Ԫ�����µı�β�ڵ�
        ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p-zl);
    }

    /* When nextdiff != 0, the raw length of the next entry has changed, so
     * we need to cascade the update throughout the ziplist */
    // �� nextdiff != 0 ʱ���½ڵ�ĺ�̽ڵ�ģ�header ���֣������Ѿ����ı䣬
    // ������Ҫ�����ظ��º����Ľڵ�
    if (nextdiff != 0) {
        offset = p-zl;
        // T  = O(N^2)
        zl = __ziplistCascadeUpdate(zl,p+reqlen);
        p = zl+offset;
    }

    /* Write the entry */
    // һ�и㶨����ǰ�ýڵ�ĳ���д���½ڵ�� header
    p += zipPrevEncodeLength(p,prevlen);
    // ���ڵ�ֵ�ĳ���д���½ڵ�� header
    p += zipEncodeLength(p,encoding,slen);
    // д��ڵ�ֵ
    if (ZIP_IS_STR(encoding)) {
        // T = O(N)
        memcpy(p,s,slen);
    } else {
        // T = O(1)
        zipSaveInteger(p,value,encoding);
    }

    // �����б�Ľڵ�����������
    // T = O(1)
    ZIPLIST_INCR_LENGTH(zl,1);

    return zl;
}

/*
 * ������Ϊ slen ���ַ��� s ���뵽 zl �С�
 *
 * where ������ֵ����������ķ���
 * - ֵΪ ZIPLIST_HEAD ʱ������ֵ���뵽��ͷ��
 * - ���򣬽���ֵ���뵽��ĩ�ˡ�
 *
 * �����ķ���ֵΪ�����ֵ��� ziplist ��
 *
 * T = O(N^2)
 */
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where) {

    // ���� where ������ֵ��������ֵ���뵽��ͷ���Ǳ�β
    unsigned char *p;
    p = (where == ZIPLIST_HEAD) ? ZIPLIST_ENTRY_HEAD(zl) : ZIPLIST_ENTRY_END(zl);

    // ���������ֵ��� ziplist
    // T = O(N^2)
    return __ziplistInsert(zl,p,s,slen);
}

/* Returns an offset to use for iterating with ziplistNext. When the given
 * index is negative, the list is traversed back to front. When the list
 * doesn't contain an element at the provided index, NULL is returned. */
/*
 * ���ݸ��������������б�����������ָ���ڵ��ָ�롣
 *
 * �������Ϊ������ô�ӱ�ͷ���β������
 * �������Ϊ������ô�ӱ�β���ͷ������
 * ���������� 0 ��ʼ������������ -1 ��ʼ��
 *
 * ������������б�Ľڵ������������б�Ϊ�գ���ô���� NULL ��
 *
 * T = O(N)
 */
unsigned char *ziplistIndex(unsigned char *zl, int index) {

    unsigned char *p;

    zlentry entry;

    // ����������
    if (index < 0) {

        // ������ת��Ϊ����
        index = (-index)-1;
        
        // ��λ����β�ڵ�
        p = ZIPLIST_ENTRY_TAIL(zl);

        // ����б�Ϊ�գ���ô������
        if (p[0] != ZIP_END) {

            // �ӱ�β���ͷ����
            entry = zipEntry(p);
            // T = O(N)
            while (entry.prevrawlen > 0 && index--) {
                // ǰ��ָ��
                p -= entry.prevrawlen;
                // T = O(1)
                entry = zipEntry(p);
            }
        }

    // ������������
    } else {

        // ��λ����ͷ�ڵ�
        p = ZIPLIST_ENTRY_HEAD(zl);

        // T = O(N)
        while (p[0] != ZIP_END && index--) {
            // ����ָ��
            // T = O(1)
            p += zipRawEntryLength(p);
        }
    }

    // ���ؽ��
    return (p[0] == ZIP_END || index > 0) ? NULL : p;
}

/* Return pointer to next entry in ziplist.
 *
 * zl is the pointer to the ziplist
 * p is the pointer to the current element
 *
 * The element after 'p' is returned, otherwise NULL if we are at the end. */
/*
 * ���� p ��ָ��ڵ�ĺ��ýڵ㡣
 *
 * ��� p Ϊ��ĩ�ˣ����� p �Ѿ��Ǳ�β�ڵ㣬��ô���� NULL ��
 *
 * T = O(1)
 */
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p) {
    ((void) zl);

    /* "p" could be equal to ZIP_END, caused by ziplistDelete,
     * and we should return NULL. Otherwise, we should return NULL
     * when the *next* element is ZIP_END (there is no next entry). */
    // p �Ѿ�ָ���б�ĩ��
    if (p[0] == ZIP_END) {
        return NULL;
    }

    // ָ���һ�ڵ�
    p += zipRawEntryLength(p);
    if (p[0] == ZIP_END) {
        // p �Ѿ��Ǳ�β�ڵ㣬û�к��ýڵ�
        return NULL;
    }

    return p;
}

/* Return pointer to previous entry in ziplist. */
/*
 * ���� p ��ָ��ڵ��ǰ�ýڵ㡣
 *
 * ��� p ��ָ��Ϊ���б����� p �Ѿ�ָ���ͷ�ڵ㣬��ô���� NULL ��
 *
 * T = O(1)
 */
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p) {
    zlentry entry;

    /* Iterating backwards from ZIP_END should return the tail. When "p" is
     * equal to the first element of the list, we're already at the head,
     * and should return NULL. */
    
    // ��� p ָ���б�ĩ�ˣ��б�Ϊ�գ����߸տ�ʼ�ӱ�β���ͷ������
    // ��ô����ȡ���б�β�˽ڵ�
    if (p[0] == ZIP_END) {
        p = ZIPLIST_ENTRY_TAIL(zl);
        // β�˽ڵ�Ҳָ���б�ĩ�ˣ���ô�б�Ϊ��
        return (p[0] == ZIP_END) ? NULL : p;
    
    // ��� p ָ���б�ͷ����ô˵�������Ѿ����
    } else if (p == ZIPLIST_ENTRY_HEAD(zl)) {
        return NULL;

    // �Ȳ��Ǳ�ͷҲ���Ǳ�β���ӱ�β���ͷ�ƶ�ָ��
    } else {
        // ����ǰһ���ڵ�Ľڵ���
        entry = zipEntry(p);
        assert(entry.prevrawlen > 0);
        // �ƶ�ָ�룬ָ��ǰһ���ڵ�
        return p-entry.prevrawlen;
    }
}

/* Get entry pointed to by 'p' and store in either 'e' or 'v' depending
 * on the encoding of the entry. 'e' is always set to NULL to be able
 * to find out whether the string pointer or the integer value was set.
 * Return 0 if 'p' points to the end of the ziplist, 1 otherwise. */
/*
 * ȡ�� p ��ָ��ڵ��ֵ��
 *
 * - ����ڵ㱣������ַ�������ô���ַ���ֵָ�뱣�浽 *sstr �У��ַ������ȱ��浽 *slen
 *
 * - ����ڵ㱣�������������ô���������浽 *sval
 *
 * �������ͨ����� *sstr �Ƿ�Ϊ NULL �����ֵ���ַ�������������
 *
 * ��ȡֵ�ɹ����� 1 ��
 * ��� p Ϊ�գ����� p ָ������б�ĩ�ˣ���ô���� 0 ����ȡֵʧ�ܡ�
 *
 * T = O(1)
 */ //��ȡpͷ��ַ���ڵ�entry�ڵ����ݣ����浽sval����sstr��
unsigned int ziplistGet(unsigned char *p, unsigned char **sstr, unsigned int *slen, long long *sval) {

    zlentry entry;
    if (p == NULL || p[0] == ZIP_END) return 0;
    if (sstr) *sstr = NULL;

    // ȡ�� p ��ָ��Ľڵ�ĸ�����Ϣ�������浽�ṹ entry ��
    // T = O(1)
    entry = zipEntry(p);

    // �ڵ��ֵΪ�ַ��������ַ������ȱ��浽 *slen ���ַ������浽 *sstr
    // T = O(1)
    if (ZIP_IS_STR(entry.encoding)) {
        if (sstr) {
            *slen = entry.len;
            *sstr = p+entry.headersize;
        }
    
    // �ڵ��ֵΪ����������ֵ������ֵ���浽 *sval
    // T = O(1)
    } else {
        if (sval) {
            *sval = zipLoadInteger(p+entry.headersize,entry.encoding);
        }
    }

    return 1;
}

/* Insert an entry at "p". 
 *
 * ����������ֵ s ���½ڵ���뵽������λ�� p �С�
 *
 * ��� p ָ��һ���ڵ㣬��ô�½ڵ㽫����ԭ�нڵ��ǰ�档
 *
 * T = O(N^2)
 */
unsigned char *ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {
    return __ziplistInsert(zl,p,s,slen);
}

/* Delete a single entry from the ziplist, pointed to by *p.
 * Also update *p in place, to be able to iterate over the
 * ziplist, while deleting entries. 
 *
 * �� zl ��ɾ�� *p ��ָ��Ľڵ㣬
 * ����ԭ�ظ��� *p ��ָ���λ�ã�ʹ�ÿ����ڵ����б�Ĺ����жԽڵ����ɾ����
 *
 * T = O(N^2)
 */
unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p) {

    // ��Ϊ __ziplistDelete ʱ��� zl �����ڴ��ط���
    // ���ڴ�������ܻ�ı� zl ���ڴ��ַ
    // ����������Ҫ��¼���� *p ��ƫ����
    // ������ɾ���ڵ�֮��Ϳ���ͨ��ƫ�������� *p ��ԭ����ȷ��λ��
    size_t offset = *p-zl;
    zl = __ziplistDelete(zl,*p,1);

    /* Store pointer to current element in p, because ziplistDelete will
     * do a realloc which might result in a different "zl"-pointer.
     * When the delete direction is back to front, we might delete the last
     * entry and end up with "p" pointing to ZIP_END, so check this. */
    *p = zl+offset;

    return zl;
}

/* Delete a range of entries from the ziplist. 
 *
 * �� index ����ָ���Ľڵ㿪ʼ�������ش� zl ��ɾ�� num ���ڵ㡣
 *
 * T = O(N^2)
 */
unsigned char *ziplistDeleteRange(unsigned char *zl, unsigned int index, unsigned int num) {

    // ����������λ���ڵ�
    // T = O(N)
    unsigned char *p = ziplistIndex(zl,index);

    // ����ɾ�� num ���ڵ�
    // T = O(N^2)
    return (p == NULL) ? zl : __ziplistDelete(zl,p,num);
}

/* Compare entry pointer to by 'p' with 'entry'. Return 1 if equal. 
 *
 * �� p ��ָ��Ľڵ��ֵ�� sstr ���жԱȡ�
 *
 * ����ڵ�ֵ�� sstr ��ֵ��ȣ����� 1 ��������򷵻� 0 ��
 *
 * T = O(N)
 */
unsigned int ziplistCompare(unsigned char *p, unsigned char *sstr, unsigned int slen) {
    zlentry entry;
    unsigned char sencoding;
    long long zval, sval;
    if (p[0] == ZIP_END) return 0;

    // ȡ���ڵ�
    entry = zipEntry(p);
    if (ZIP_IS_STR(entry.encoding)) {

        // �ڵ�ֵΪ�ַ����������ַ����Ա�

        /* Raw compare */
        if (entry.len == slen) {
            // T = O(N)
            return memcmp(p+entry.headersize,sstr,slen) == 0;
        } else {
            return 0;
        }
    } else {
        
        // �ڵ�ֵΪ���������������Ա�

        /* Try to compare encoded values. Don't compare encoding because
         * different implementations may encoded integers differently. */
        if (zipTryEncoding(sstr,slen,&sval,&sencoding)) {
          // T = O(1)
          zval = zipLoadInteger(p+entry.headersize,entry.encoding);
          return zval == sval;
        }
    }

    return 0;
}

/* Find pointer to the entry equal to the specified entry. 
 * 
 * Ѱ�ҽڵ�ֵ�� vstr ��ȵ��б�ڵ㣬�����ظýڵ��ָ�롣
 * 
 * Skip 'skip' entries between every comparison. 
 *
 * ÿ�αȶ�֮ǰ������ skip ���ڵ㡣
 *
 * Returns NULL when the field could not be found. 
 *
 * ����Ҳ�����Ӧ�Ľڵ㣬�򷵻� NULL ��
 *
 * T = O(N^2)
 */
unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip) {
    int skipcnt = 0;
    unsigned char vencoding = 0;
    long long vll = 0;

    // ֻҪδ�����б�ĩ�ˣ���һֱ����
    // T = O(N^2)
    while (p[0] != ZIP_END) {
        unsigned int prevlensize, encoding, lensize, len;
        unsigned char *q;

        ZIP_DECODE_PREVLENSIZE(p, prevlensize);
        ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);
        q = p + prevlensize + lensize;

        if (skipcnt == 0) {

            /* Compare current entry with specified entry */
            // �Ա��ַ���ֵ
            // T = O(N)
            if (ZIP_IS_STR(encoding)) {
                if (len == vlen && memcmp(q, vstr, vlen) == 0) {
                    return p;
                }
            } else {
                /* Find out if the searched field can be encoded. Note that
                 * we do it only the first time, once done vencoding is set
                 * to non-zero and vll is set to the integer value. */
                // ��Ϊ����ֵ�п��ܱ������ˣ�
                // ���Ե���һ�ν���ֵ�Ա�ʱ�������Դ���ֵ���н���
                // ����������ֻ�����һ��
                if (vencoding == 0) {
                    if (!zipTryEncoding(vstr, vlen, &vll, &vencoding)) {
                        /* If the entry can't be encoded we set it to
                         * UCHAR_MAX so that we don't retry again the next
                         * time. */
                        vencoding = UCHAR_MAX;
                    }
                    /* Must be non-zero by now */
                    assert(vencoding);
                }

                /* Compare current entry with specified entry, do it only
                 * if vencoding != UCHAR_MAX because if there is no encoding
                 * possible for the field it can't be a valid integer. */
                // �Ա�����ֵ
                if (vencoding != UCHAR_MAX) {
                    // T = O(1)
                    long long ll = zipLoadInteger(q, encoding);
                    if (ll == vll) {
                        return p;
                    }
                }
            }

            /* Reset skip count */
            skipcnt = skip;
        } else {
            /* Skip entry */
            skipcnt--;
        }

        /* Move to next entry */
        // ����ָ�룬ָ����ýڵ�
        p = q + len;
    }

    // û���ҵ�ָ���Ľڵ�
    return NULL;
}

/* Return length of ziplist. 
 * 
 * ���� ziplist �еĽڵ����
 *
 * T = O(N)
 */
unsigned int ziplistLen(unsigned char *zl) {

    unsigned int len = 0;

    // �ڵ���С�� UINT16_MAX
    // T = O(1)
    if (intrev16ifbe(ZIPLIST_LENGTH(zl)) < UINT16_MAX) {
        len = intrev16ifbe(ZIPLIST_LENGTH(zl));

    // �ڵ������� UINT16_MAX ʱ����Ҫ���������б���ܼ�����ڵ���
    // T = O(N)
    } else {
        unsigned char *p = zl+ZIPLIST_HEADER_SIZE;
        while (*p != ZIP_END) {
            p += zipRawEntryLength(p);
            len++;
        }

        /* Re-store length if small enough */
        if (len < UINT16_MAX) ZIPLIST_LENGTH(zl) = intrev16ifbe(len);
    }

    return len;
}

/* Return ziplist blob size in bytes. 
 *
 * �������� ziplist ռ�õ��ڴ��ֽ���
 *
 * T = O(1)
 */
size_t ziplistBlobLen(unsigned char *zl) {
    return intrev32ifbe(ZIPLIST_BYTES(zl));
}

void ziplistRepr(unsigned char *zl) {
    unsigned char *p;
    int index = 0;
    zlentry entry;

    printf(
        "{total bytes %d} "
        "{length %u}\n"
        "{tail offset %u}\n",
        intrev32ifbe(ZIPLIST_BYTES(zl)),
        intrev16ifbe(ZIPLIST_LENGTH(zl)),
        intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)));
    p = ZIPLIST_ENTRY_HEAD(zl);
    while(*p != ZIP_END) {
        entry = zipEntry(p);
        printf(
            "{"
                "addr 0x%08lx, "
                "index %2d, "
                "offset %5ld, "
                "rl: %5u, "
                "hs %2u, "
                "pl: %5u, "
                "pls: %2u, "
                "payload %5u"
            "} ",
            (long unsigned)p,
            index,
            (unsigned long) (p-zl),
            entry.headersize+entry.len,
            entry.headersize,
            entry.prevrawlen,
            entry.prevrawlensize,
            entry.len);
        p += entry.headersize;
        if (ZIP_IS_STR(entry.encoding)) {
            if (entry.len > 40) {
                if (fwrite(p,40,1,stdout) == 0) perror("fwrite");
                printf("...");
            } else {
                if (entry.len &&
                    fwrite(p,entry.len,1,stdout) == 0) perror("fwrite");
            }
        } else {
            printf("%lld", (long long) zipLoadInteger(p,entry.encoding));
        }
        printf("\n");
        p += entry.len;
        index++;
    }
    printf("{end}\n\n");
}

#ifdef ZIPLIST_TEST_MAIN
#include <sys/time.h>
#include "adlist.h"
#include "sds.h"

#define debug(f, ...) { if (DEBUG) printf(f, __VA_ARGS__); }

unsigned char *createList() {
    unsigned char *zl = ziplistNew();
    zl = ziplistPush(zl, (unsigned char*)"foo", 3, ZIPLIST_TAIL);
    zl = ziplistPush(zl, (unsigned char*)"quux", 4, ZIPLIST_TAIL);
    zl = ziplistPush(zl, (unsigned char*)"hello", 5, ZIPLIST_HEAD);
    zl = ziplistPush(zl, (unsigned char*)"1024", 4, ZIPLIST_TAIL);
    return zl;
}

unsigned char *createIntList() {
    unsigned char *zl = ziplistNew();
    char buf[32];

    sprintf(buf, "100");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    sprintf(buf, "128000");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    sprintf(buf, "-100");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
    sprintf(buf, "4294967296");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
    sprintf(buf, "non integer");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    sprintf(buf, "much much longer non integer");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    return zl;
}

long long usec(void) {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
}

void stress(int pos, int num, int maxsize, int dnum) {
    int i,j,k;
    unsigned char *zl;
    char posstr[2][5] = { "HEAD", "TAIL" };
    long long start;
    for (i = 0; i < maxsize; i+=dnum) {
        zl = ziplistNew();
        for (j = 0; j < i; j++) {
            zl = ziplistPush(zl,(unsigned char*)"quux",4,ZIPLIST_TAIL);
        }

        /* Do num times a push+pop from pos */
        start = usec();
        for (k = 0; k < num; k++) {
            zl = ziplistPush(zl,(unsigned char*)"quux",4,pos);
            zl = ziplistDeleteRange(zl,0,1);
        }
        printf("List size: %8d, bytes: %8d, %dx push+pop (%s): %6lld usec\n",
            i,intrev32ifbe(ZIPLIST_BYTES(zl)),num,posstr[pos],usec()-start);
        zfree(zl);
    }
}

void pop(unsigned char *zl, int where) {
    unsigned char *p, *vstr;
    unsigned int vlen;
    long long vlong;

    p = ziplistIndex(zl,where == ZIPLIST_HEAD ? 0 : -1);
    if (ziplistGet(p,&vstr,&vlen,&vlong)) {
        if (where == ZIPLIST_HEAD)
            printf("Pop head: ");
        else
            printf("Pop tail: ");

        if (vstr)
            if (vlen && fwrite(vstr,vlen,1,stdout) == 0) perror("fwrite");
        else
            printf("%lld", vlong);

        printf("\n");
        ziplistDeleteRange(zl,-1,1);
    } else {
        printf("ERROR: Could not pop\n");
        exit(1);
    }
}

int randstring(char *target, unsigned int min, unsigned int max) {
    int p = 0;
    int len = min+rand()%(max-min+1);
    int minval, maxval;
    switch(rand() % 3) {
    case 0:
        minval = 0;
        maxval = 255;
    break;
    case 1:
        minval = 48;
        maxval = 122;
    break;
    case 2:
        minval = 48;
        maxval = 52;
    break;
    default:
        assert(NULL);
    }

    while(p < len)
        target[p++] = minval+rand()%(maxval-minval+1);
    return len;
}

void verify(unsigned char *zl, zlentry *e) {
    int i;
    int len = ziplistLen(zl);
    zlentry _e;

    for (i = 0; i < len; i++) {
        memset(&e[i], 0, sizeof(zlentry));
        e[i] = zipEntry(ziplistIndex(zl, i));

        memset(&_e, 0, sizeof(zlentry));
        _e = zipEntry(ziplistIndex(zl, -len+i));

        assert(memcmp(&e[i], &_e, sizeof(zlentry)) == 0);
    }
}

int main(int argc, char **argv) {
    unsigned char *zl, *p;
    unsigned char *entry;
    unsigned int elen;
    long long value;

    /* If an argument is given, use it as the random seed. */
    if (argc == 2)
        srand(atoi(argv[1]));

    zl = createIntList();
    ziplistRepr(zl);

    zl = createList();
    ziplistRepr(zl);

    pop(zl,ZIPLIST_TAIL);
    ziplistRepr(zl);

    pop(zl,ZIPLIST_HEAD);
    ziplistRepr(zl);

    pop(zl,ZIPLIST_TAIL);
    ziplistRepr(zl);

    pop(zl,ZIPLIST_TAIL);
    ziplistRepr(zl);

    printf("Get element at index 3:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 3);
        if (!ziplistGet(p, &entry, &elen, &value)) {
            printf("ERROR: Could not access index 3\n");
            return 1;
        }
        if (entry) {
            if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            printf("\n");
        } else {
            printf("%lld\n", value);
        }
        printf("\n");
    }

    printf("Get element at index 4 (out of range):\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 4);
        if (p == NULL) {
            printf("No entry\n");
        } else {
            printf("ERROR: Out of range index should return NULL, returned offset: %ld\n", p-zl);
            return 1;
        }
        printf("\n");
    }

    printf("Get element at index -1 (last element):\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -1);
        if (!ziplistGet(p, &entry, &elen, &value)) {
            printf("ERROR: Could not access index -1\n");
            return 1;
        }
        if (entry) {
            if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            printf("\n");
        } else {
            printf("%lld\n", value);
        }
        printf("\n");
    }

    printf("Get element at index -4 (first element):\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -4);
        if (!ziplistGet(p, &entry, &elen, &value)) {
            printf("ERROR: Could not access index -4\n");
            return 1;
        }
        if (entry) {
            if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            printf("\n");
        } else {
            printf("%lld\n", value);
        }
        printf("\n");
    }

    printf("Get element at index -5 (reverse out of range):\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -5);
        if (p == NULL) {
            printf("No entry\n");
        } else {
            printf("ERROR: Out of range index should return NULL, returned offset: %ld\n", p-zl);
            return 1;
        }
        printf("\n");
    }

    printf("Iterate list from 0 to end:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 0);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            p = ziplistNext(zl,p);
            printf("\n");
        }
        printf("\n");
    }

    printf("Iterate list from 1 to end:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 1);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            p = ziplistNext(zl,p);
            printf("\n");
        }
        printf("\n");
    }

    printf("Iterate list from 2 to end:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 2);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            p = ziplistNext(zl,p);
            printf("\n");
        }
        printf("\n");
    }

    printf("Iterate starting out of range:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 4);
        if (!ziplistGet(p, &entry, &elen, &value)) {
            printf("No entry\n");
        } else {
            printf("ERROR\n");
        }
        printf("\n");
    }

    printf("Iterate from back to front:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -1);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            p = ziplistPrev(zl,p);
            printf("\n");
        }
        printf("\n");
    }

    printf("Iterate from back to front, deleting all items:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -1);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            zl = ziplistDelete(zl,&p);
            p = ziplistPrev(zl,p);
            printf("\n");
        }
        printf("\n");
    }

    printf("Delete inclusive range 0,0:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 0, 1);
        ziplistRepr(zl);
    }

    printf("Delete inclusive range 0,1:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 0, 2);
        ziplistRepr(zl);
    }

    printf("Delete inclusive range 1,2:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 1, 2);
        ziplistRepr(zl);
    }

    printf("Delete with start index out of range:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 5, 1);
        ziplistRepr(zl);
    }

    printf("Delete with num overflow:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 1, 5);
        ziplistRepr(zl);
    }

    printf("Delete foo while iterating:\n");
    {
        zl = createList();
        p = ziplistIndex(zl,0);
        while (ziplistGet(p,&entry,&elen,&value)) {
            if (entry && strncmp("foo",(char*)entry,elen) == 0) {
                printf("Delete foo\n");
                zl = ziplistDelete(zl,&p);
            } else {
                printf("Entry: ");
                if (entry) {
                    if (elen && fwrite(entry,elen,1,stdout) == 0)
                        perror("fwrite");
                } else {
                    printf("%lld",value);
                }
                p = ziplistNext(zl,p);
                printf("\n");
            }
        }
        printf("\n");
        ziplistRepr(zl);
    }

    printf("Regression test for >255 byte strings:\n");
    {
        char v1[257],v2[257];
        memset(v1,'x',256);
        memset(v2,'y',256);
        zl = ziplistNew();
        zl = ziplistPush(zl,(unsigned char*)v1,strlen(v1),ZIPLIST_TAIL);
        zl = ziplistPush(zl,(unsigned char*)v2,strlen(v2),ZIPLIST_TAIL);

        /* Pop values again and compare their value. */
        p = ziplistIndex(zl,0);
        assert(ziplistGet(p,&entry,&elen,&value));
        assert(strncmp(v1,(char*)entry,elen) == 0);
        p = ziplistIndex(zl,1);
        assert(ziplistGet(p,&entry,&elen,&value));
        assert(strncmp(v2,(char*)entry,elen) == 0);
        printf("SUCCESS\n\n");
    }

    printf("Regression test deleting next to last entries:\n");
    {
        char v[3][257];
        zlentry e[3];
        int i;

        for (i = 0; i < (sizeof(v)/sizeof(v[0])); i++) {
            memset(v[i], 'a' + i, sizeof(v[0]));
        }

        v[0][256] = '\0';
        v[1][  1] = '\0';
        v[2][256] = '\0';

        zl = ziplistNew();
        for (i = 0; i < (sizeof(v)/sizeof(v[0])); i++) {
            zl = ziplistPush(zl, (unsigned char *) v[i], strlen(v[i]), ZIPLIST_TAIL);
        }

        verify(zl, e);

        assert(e[0].prevrawlensize == 1);
        assert(e[1].prevrawlensize == 5);
        assert(e[2].prevrawlensize == 1);

        /* Deleting entry 1 will increase `prevrawlensize` for entry 2 */
        unsigned char *p = e[1].p;
        zl = ziplistDelete(zl, &p);

        verify(zl, e);

        assert(e[0].prevrawlensize == 1);
        assert(e[1].prevrawlensize == 5);

        printf("SUCCESS\n\n");
    }

    printf("Create long list and check indices:\n");
    {
        zl = ziplistNew();
        char buf[32];
        int i,len;
        for (i = 0; i < 1000; i++) {
            len = sprintf(buf,"%d",i);
            zl = ziplistPush(zl,(unsigned char*)buf,len,ZIPLIST_TAIL);
        }
        for (i = 0; i < 1000; i++) {
            p = ziplistIndex(zl,i);
            assert(ziplistGet(p,NULL,NULL,&value));
            assert(i == value);

            p = ziplistIndex(zl,-i-1);
            assert(ziplistGet(p,NULL,NULL,&value));
            assert(999-i == value);
        }
        printf("SUCCESS\n\n");
    }

    printf("Compare strings with ziplist entries:\n");
    {
        zl = createList();
        p = ziplistIndex(zl,0);
        if (!ziplistCompare(p,(unsigned char*)"hello",5)) {
            printf("ERROR: not \"hello\"\n");
            return 1;
        }
        if (ziplistCompare(p,(unsigned char*)"hella",5)) {
            printf("ERROR: \"hella\"\n");
            return 1;
        }

        p = ziplistIndex(zl,3);
        if (!ziplistCompare(p,(unsigned char*)"1024",4)) {
            printf("ERROR: not \"1024\"\n");
            return 1;
        }
        if (ziplistCompare(p,(unsigned char*)"1025",4)) {
            printf("ERROR: \"1025\"\n");
            return 1;
        }
        printf("SUCCESS\n\n");
    }

    printf("Stress with random payloads of different encoding:\n");
    {
        int i,j,len,where;
        unsigned char *p;
        char buf[1024];
        int buflen;
        list *ref;
        listNode *refnode;

        /* Hold temp vars from ziplist */
        unsigned char *sstr;
        unsigned int slen;
        long long sval;

        for (i = 0; i < 20000; i++) {
            zl = ziplistNew();
            ref = listCreate();
            listSetFreeMethod(ref,sdsfree);
            len = rand() % 256;

            /* Create lists */
            for (j = 0; j < len; j++) {
                where = (rand() & 1) ? ZIPLIST_HEAD : ZIPLIST_TAIL;
                if (rand() % 2) {
                    buflen = randstring(buf,1,sizeof(buf)-1);
                } else {
                    switch(rand() % 3) {
                    case 0:
                        buflen = sprintf(buf,"%lld",(0LL + rand()) >> 20);
                        break;
                    case 1:
                        buflen = sprintf(buf,"%lld",(0LL + rand()));
                        break;
                    case 2:
                        buflen = sprintf(buf,"%lld",(0LL + rand()) << 20);
                        break;
                    default:
                        assert(NULL);
                    }
                }

                /* Add to ziplist */
                zl = ziplistPush(zl, (unsigned char*)buf, buflen, where);

                /* Add to reference list */
                if (where == ZIPLIST_HEAD) {
                    listAddNodeHead(ref,sdsnewlen(buf, buflen));
                } else if (where == ZIPLIST_TAIL) {
                    listAddNodeTail(ref,sdsnewlen(buf, buflen));
                } else {
                    assert(NULL);
                }
            }

            assert(listLength(ref) == ziplistLen(zl));
            for (j = 0; j < len; j++) {
                /* Naive way to get elements, but similar to the stresser
                 * executed from the Tcl test suite. */
                p = ziplistIndex(zl,j);
                refnode = listIndex(ref,j);

                assert(ziplistGet(p,&sstr,&slen,&sval));
                if (sstr == NULL) {
                    buflen = sprintf(buf,"%lld",sval);
                } else {
                    buflen = slen;
                    memcpy(buf,sstr,buflen);
                    buf[buflen] = '\0';
                }
                assert(memcmp(buf,listNodeValue(refnode),buflen) == 0);
            }
            zfree(zl);
            listRelease(ref);
        }
        printf("SUCCESS\n\n");
    }

    printf("Stress with variable ziplist size:\n");
    {
        stress(ZIPLIST_HEAD,100000,16384,256);
        stress(ZIPLIST_TAIL,100000,16384,256);
    }

    return 0;
}

#endif
