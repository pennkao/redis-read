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

#ifndef __INTSET_H
#define __INTSET_H
#include <stdint.h>

/*
�������� API?

����             ����                                     ʱ�临�Ӷ�

intsetNew       ����һ���µ��������ϡ�                          O(1) 
intsetAdd       ������Ԫ����ӵ������������档                  O(N) 
intsetRemove    �������������Ƴ�����Ԫ�ء�                      O(N) 
intsetFind      ������ֵ�Ƿ�����ڼ��ϡ�                      ��Ϊ�ײ��������򣬲��ҿ���ͨ�����ֲ��ҷ������У� ���Ը��Ӷ�Ϊ O(\log N) �� 
intsetRandom    �������������������һ��Ԫ�ء�                  O(1) 
intsetGet       ȡ���ײ������ڸ��������ϵ�Ԫ�ء�                O(1) 
intsetLen       �����������ϰ�����Ԫ�ظ�����                    O(1) 
intsetBlobLen   ������������ռ�õ��ڴ��ֽ�����                  O(1) 
memrev64ifbe    ��С��ת�������Ǵ洢��ת��Ϊ��ˣ�ȡ��ʱתΪС��
*/

/*
����
ÿ������Ҫ��һ����Ԫ����ӵ������������棬 ������Ԫ�ص����ͱ�����������������Ԫ�ص����Ͷ�Ҫ��ʱ�� ����������Ҫ�Ƚ���
������upgrade���� Ȼ����ܽ���Ԫ����ӵ������������档

����
�������ϲ�֧�ֽ��������� һ������������������� ����ͻ�һֱ�����������״̬��
�ٸ����ӣ� ����ͼ 6-11 ��ʾ������������˵�� ��ʹ���ǽ�������Ψһһ��������Ҫʹ�� int64_t �����������Ԫ�� 4294967295 ɾ���ˣ� 
�������ϵı�����Ȼ��ά�� INTSET_ENC_INT64 �� �ײ�����Ҳ��Ȼ���� int64_t ���͵ģ�


�����������ϲ������Ԫ�ع���Ϊ�������У�
1.������Ԫ�ص����ͣ� ��չ�������ϵײ�����Ŀռ��С�� ��Ϊ��Ԫ�ط���ռ䡣
2.���ײ��������е�����Ԫ�ض�ת��������Ԫ����ͬ�����ͣ� ��������ת�����Ԫ�ط��õ���ȷ��λ�ϣ� �����ڷ���Ԫ�صĹ����У� 
  ��Ҫ����ά�ֵײ�������������ʲ��䡣
3.����Ԫ����ӵ��ײ��������档
*/
typedef struct intset {

    /*
 ��Ȼ intset �ṹ�� contents ��������Ϊ int8_t ���͵����飬 ��ʵ���� contents ���鲢�������κ� int8_t ���͵�ֵ ���� 
 contents �������������ȡ���� encoding ���Ե�ֵ��
 
 ?��� encoding ���Ե�ֵΪ INTSET_ENC_INT16 �� ��ô contents ����һ�� int16_t ���͵����飬 �������ÿ�����һ�� int16_t 
 ���͵�����ֵ ����СֵΪ -32,768 �����ֵΪ 32,767 ����
 ?��� encoding ���Ե�ֵΪ INTSET_ENC_INT32 �� ��ô contents ����һ�� int32_t ���͵����飬 �������ÿ�����һ�� int32_t 
 ���͵�����ֵ ����СֵΪ -2,147,483,648 �����ֵΪ 2,147,483,647 ����
 ?��� encoding ���Ե�ֵΪ INTSET_ENC_INT64 �� ��ô contents ����һ�� int64_t ���͵����飬 �������ÿ�����һ�� int64_t 
 ���͵�����ֵ ����СֵΪ -9,223,372,036,854,775,808 �����ֵΪ 9,223,372,036,854,775,807 ����
 */
    // ���뷽ʽ // ����Ԫ����ʹ�õ����͵ĳ���
    uint32_t encoding;//ע��:����һ���ײ�Ϊ int16_t ����������������һ�� int64_t ���͵�����ֵʱ�� �����������е�����Ԫ�ض��ᱻת���� int64_t ����

    // ���ϰ�����Ԫ������
    uint32_t length; // Ԫ�ظ��� length ���Լ�¼���������ϰ�����Ԫ�������� Ҳ���� contents ����ĳ��ȡ�

    // ����Ԫ�ص�����
    int8_t contents[];// ����Ԫ�ص����� �������������а�ֵ�Ĵ�С��С������������У� ���������в������κ��ظ��

} intset;

intset *intsetNew(void);
intset *intsetAdd(intset *is, int64_t value, uint8_t *success);
intset *intsetRemove(intset *is, int64_t value, int *success);
uint8_t intsetFind(intset *is, int64_t value);
int64_t intsetRandom(intset *is);
uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value);
uint32_t intsetLen(intset *is);
size_t intsetBlobLen(intset *is);

#endif // __INTSET_H
