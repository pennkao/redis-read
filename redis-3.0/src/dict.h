/* Hash Tables Implementation.
 *
 * This file implements in-memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto-resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * ����ļ�ʵ����һ���ڴ��ϣ��
 * ��֧�ֲ��롢ɾ�����滻�����Һͻ�ȡ���Ԫ�صȲ�����
 *
 * ��ϣ����Զ��ڱ�Ĵ�С�Ķ��η�֮����е�����
 *
 * ���ĳ�ͻͨ�������������
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include <stdint.h>

#ifndef __DICT_H
#define __DICT_H

/*
 * �ֵ�Ĳ���״̬
 */
// �����ɹ�
#define DICT_OK 0
// ����ʧ�ܣ������
#define DICT_ERR 1

/* Unused arguments generate annoying warnings... */
// ����ֵ��˽�����ݲ�ʹ��ʱ
// ����������������������
#define DICT_NOTUSED(V) ((void) V)

/*
    �����������е��ַ����ȱ��浽����xxxCommand(��setCommand)����ʱ�����������ַ��������Ѿ�ת��ΪredisObject���浽redisClient->argv[]�У�
    Ȼ����setKey����غ����а�key-valueת��ΪdictEntry�ڵ��key��v(�ֱ��Ӧkey��value)��ӵ�dict->dictht->table[]  hash��,������ô��ӵ�dictEntry
    �ڵ��key��value�п��Բο�dict->type( typeΪxxxDictType ����keyptrDictType��) ����dictCreate
*/

/*
 * ��ϣ��ڵ�
 */
typedef struct dictEntry { 
//�ýṹʽ�ֵ�dictht->table[]  hash�еĳ�Ա�ṹ���κ�key - value��ֵ�Զ�����ӵ�ת��ΪdictEntry�ṹ��ӵ��ֵ�hash table��
    /*
  key ���Ա����ż�ֵ���еļ��� 
  �� v �����򱣴��ż�ֵ���е�ֵ�� ���м�ֵ�Ե�ֵ������һ��ָ�룬 ������һ�� uint64_t ������ �ֻ�����һ�� int64_t ������
     */
     /*
    �����������е��ַ����ȱ��浽����xxxCommand(��setCommand)����ʱ�����������ַ��������Ѿ�ת��ΪredisObject���浽redisClient->argv[]�У�
    Ȼ����setKey����غ����а�key-valueת��ΪdictEntry�ڵ��key��v(�ֱ��Ӧkey��value)��ӵ�dictht->table[]  hash��
    */
    // ��
    void *key; //��Ӧһ��robj
    
    /*
      key ���Ա����ż�ֵ���еļ��� 
      �� v �����򱣴��ż�ֵ���е�ֵ�� ���м�ֵ�Ե�ֵ������һ��ָ�룬 ������һ�� uint64_t ������ �ֻ�����һ�� int64_t ������
     */
     /*
    �����������е��ַ����ȱ��浽����xxxCommand(��setCommand)����ʱ�����������ַ��������Ѿ�ת��ΪredisObject���浽redisClient->argv[]�У�
    Ȼ����setKey����غ����а�key-valueת��ΪdictEntry�ڵ��key��v(�ֱ��Ӧkey��value)��ӵ�dictht->table[]  hash��
    */
    // ֵ
    union {
        void *val;
        uint64_t u64;
        int64_t s64;//һ���¼���ǹ��ڼ�db->expires��ÿ�����Ĺ���ʱ��  ��λms
    } v;//��Ӧһ��robj

    
    //next ������ָ����һ����ϣ��ڵ��ָ�룬 ���ָ����Խ������ϣֵ��ͬ�ļ�ֵ��������һ�Σ� �Դ����������ͻ��collision�������⡣
    // ������̽ڵ�
    // ָ���¸���ϣ��ڵ㣬�γ�����
    struct dictEntry *next;

} dictEntry;


/*
 * �ֵ������ض�����
 */ //dictType��Ҫ��xxxDictType(dbDictType zsetDictType setDictType��)
typedef struct dictType {//����privdata������dict->privdata�л�ȡ

    // �����ϣֵ�ĺ��� // ������Ĺ�ϣֵ����, ����key��hash table�еĴ洢λ�ã���ͬ��dict�����в�ͬ��hash function.
    unsigned int (*hashFunction)(const void *key);//dictHashKey��ִ�иú���

    // ���Ƽ��ĺ���
    void *(*keyDup)(void *privdata, const void *key);//dictSetKey

    // ����ֵ�ĺ��� //Ҳ������keyΪ��ֵ�����һ��ֵ��ѡ����Ӧ��hashͰ����key�ڵ����Ͱ�У�ͬʱִ��valdup������ռ�洢key��Ӧ��value
    void *(*valDup)(void *privdata, const void *obj); //dictSetVal  ������dictEntry->v->value�У�Ȼ����

    // �Աȼ��ĺ���
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);//dictCompareKeys

    // ���ټ��ĺ���
    void (*keyDestructor)(void *privdata, void *key);//dictFreeKey
    
    // ����ֵ�ĺ��� // ֵ���͹�����  dictFreeVal  ɾ��hash�е�key�ڵ��ʱ���ִ�иú�������ɾ��value
    void (*valDestructor)(void *privdata, void *obj);//dictFreeVal

} dictType;


/*
�������ͻ

�������������������ļ������䵽�˹�ϣ�������ͬһ����������ʱ�� ���ǳ���Щ�������˳�ͻ��collision����

Redis �Ĺ�ϣ��ʹ������ַ����separate chaining�����������ͻ�� ÿ����ϣ��ڵ㶼��һ�� next ָ�룬 �����ϣ��ڵ������ next 
ָ�빹��һ���������� �����䵽ͬһ�������ϵĶ���ڵ��������������������������� ��ͽ���˼���ͻ�����⡣

�ٸ����ӣ� �������Ҫ����ֵ�� k2 �� v2 ��ӵ�ͼ 4-6 ��ʾ�Ĺ�ϣ�����棬 ���Ҽ���ó� k2 ������ֵΪ 2 �� ��ô�� k1 �� k2 ��
������ͻ�� �������ͻ�İ취����ʹ�� next ָ�뽫�� k2 �� k1 ���ڵĽڵ�����������

��Ϊ dictEntry �ڵ���ɵ�����û��ָ�������β��ָ�룬 ����Ϊ���ٶȿ��ǣ� �������ǽ��½ڵ���ӵ�����ı�ͷλ�ã����Ӷ�Ϊ O(1)���� 
�����������нڵ��ǰ�档
*/

/*
rehash

���Ų����Ĳ���ִ�У� ��ϣ����ļ�ֵ�Ի��𽥵�������߼��٣� Ϊ���ù�ϣ��ĸ������ӣ�load factor��ά����һ������ķ�Χ֮�ڣ� 
����ϣ����ļ�ֵ������̫�����̫��ʱ�� ������Ҫ�Թ�ϣ��Ĵ�С������Ӧ����չ����������

��չ��������ϣ��Ĺ�������ͨ��ִ�� rehash ������ɢ�У���������ɣ� Redis ���ֵ�Ĺ�ϣ��ִ�� rehash �Ĳ������£�
1.Ϊ�ֵ�� ht[1] ��ϣ�����ռ䣬 �����ϣ��Ŀռ��Сȡ����Ҫִ�еĲ����� �Լ� ht[0] ��ǰ�����ļ�ֵ������ ��Ҳ���� ht[0].used 
���Ե�ֵ�������ִ�е�����չ������ ��ô ht[1] �Ĵ�СΪ��һ�����ڵ��� ht[0].used * 2 �� 2^n ��2 �� n �η��ݣ�����������used��5����
ht[1] hash���е�Ͱ�������Ǵ��ڵ���5 *2=10�������һ��2��n���ݣ�Ҳ����16��ͬ�������8�ȣ���Ϊ���ڵ���8*2=16�����2��n���ݣ�����16

���ִ�е������������� ��ô ht[1] �Ĵ�СΪ��һ�����ڵ��� ht[0].used �� 2^n ������5�������������2��n����Ϊ8�����Ϊ8������Ϊ8 ????????�о�����е����⣬�ⲻ�������û����

2.�������� ht[0] �е����м�ֵ�� rehash �� ht[1] ���棺 rehash ָ�������¼�����Ĺ�ϣֵ������ֵ�� Ȼ�󽫼�ֵ�Է��õ� ht[1] ��ϣ���ָ��λ���ϡ�
3.�� ht[0] ���������м�ֵ�Զ�Ǩ�Ƶ��� ht[1] ֮�� ��ht[0] ��Ϊ�ձ��� �ͷ� ht[0] �� �� ht[1] ����Ϊ ht[0] �� ���� ht[1] �´���һ���հ׹�ϣ�� Ϊ��һ�� rehash ��׼����

�ٸ����ӣ� �������Ҫ���ֵ�� ht[0] ������չ������ ��ô����ִ�����²��裺
1.ht[0].used ��ǰ��ֵΪ 4 �� 4 * 2 = 8 �� �� 8 ��2^3��ǡ���ǵ�һ�����ڵ��� 4 �� 2 �� n �η��� ���Գ���Ὣ ht[1] ��ϣ��Ĵ�С����Ϊ 8 �� ��չ��Ĵ�С�ο�_dictExpandIfNeeded
2.�� ht[0] �������ĸ���ֵ�Զ� rehash �� ht[1]
3.�ͷ� ht[0] ������ ht[1] ����Ϊ ht[0] ��Ȼ��Ϊ ht[1] ����һ���հ׹�ϣ��

���ˣ� �Թ�ϣ�����չ����ִ����ϣ� ����ɹ�����ϣ��Ĵ�С��ԭ���� 4 ��Ϊ�����ڵ� 8 ��



��ϣ�����չ������

�����������е�����һ��������ʱ�� ������Զ���ʼ�Թ�ϣ��ִ����չ������
1.������Ŀǰû����ִ�� BGSAVE ������� BGREWRITEAOF ��� ���ҹ�ϣ��ĸ������Ӵ��ڵ��� 1 ��
2.������Ŀǰ����ִ�� BGSAVE ������� BGREWRITEAOF ��� ���ҹ�ϣ��ĸ������Ӵ��ڵ��� 5 ��
��һ���棬 ����ϣ��ĸ�������С�� 0.1 ʱ�� �����Զ���ʼ�Թ�ϣ��ִ������������

���й�ϣ��ĸ������ӿ���ͨ����ʽ��


# �������� = ��ϣ���ѱ���ڵ����� / ��ϣ���С
load_factor = ht[0].used / ht[0].size

����ó���
����˵�� ����һ����СΪ 4 �� ���� 4 ����ֵ�ԵĹ�ϣ����˵�� �����ϣ��ĸ�������Ϊ��
load_factor = 4 / 4 = 1
�ֱ���˵�� ����һ����СΪ 512 �� ���� 256 ����ֵ�ԵĹ�ϣ����˵�� �����ϣ��ĸ�������Ϊ��
load_factor = 256 / 512 = 0.5

���� BGSAVE ����� BGREWRITEAOF �����Ƿ�����ִ�У� ������ִ����չ��������ĸ������Ӳ�����ͬ�� ������Ϊ��ִ�� BGSAVE ����
�� BGREWRITEAOF ����Ĺ����У� Redis ��Ҫ������ǰ���������̵��ӽ��̣� �����������ϵͳ������дʱ���ƣ�copy-on-write������
���Ż��ӽ��̵�ʹ��Ч�ʣ� �������ӽ��̴����ڼ䣬 �����������ִ����չ��������ĸ������ӣ� �Ӷ������ܵر������ӽ��̴����ڼ�
���й�ϣ����չ������ ����Ա��ⲻ��Ҫ���ڴ�д������� ����޶ȵؽ�Լ�ڴ档


��һ���棬 ����ϣ��ĸ�������С�� 0.1 ʱ�� �����Զ���ʼ�Թ�ϣ��ִ������������

1���ܵ�Ԫ�ظ��� �� DICTͰ�ĸ����õ�ÿ��Ͱƽ���洢��Ԫ�ظ���(pre_num),��� pre_num > dict_force_resize_ratio,�ͻᴥ��dict ���������dict_force_resize_ratio = 5��

2������Ԫ�� * 10 < Ͱ�ĸ�����Ҳ����,����ʱ���<10%, DICT��������������total / bk_num �ӽ� 1:1��


rehash������ù�ϵ���ù���:dictAddRaw->_dictKeyIndex->_dictExpandIfNeeded->dictExpand������������ù�ϵ����Ҫ����dict�ĵ��ù�ϵ��
*/

/*
����ʽ rehash?

��һ��˵���� ��չ��������ϣ����Ҫ�� ht[0] ��������м�ֵ�� rehash �� ht[1] ���棬 ���ǣ� ��� rehash ����������һ���ԡ�
����ʽ����ɵģ� ���Ƿֶ�Ρ�����ʽ����ɵġ�

��������ԭ�����ڣ� ��� ht[0] ��ֻ�������ĸ���ֵ�ԣ� ��ô������������˲��ͽ���Щ��ֵ��ȫ�� rehash �� ht[1] �� ���ǣ� 
�����ϣ���ﱣ��ļ�ֵ�����������ĸ��� �����İ�����ǧ���������ڸ���ֵ�ԣ� ��ôҪһ���Խ���Щ��ֵ��ȫ�� rehash �� ht[1] 
�Ļ��� �Ӵ�ļ��������ܻᵼ�·�������һ��ʱ����ֹͣ����

��ˣ� Ϊ�˱��� rehash �Է������������Ӱ�죬 ����������һ���Խ� ht[0] ��������м�ֵ��ȫ�� rehash �� ht[1] �� ���Ƿֶ�Ρ�
����ʽ�ؽ� ht[0] ����ļ�ֵ�������� rehash �� ht[1] ��

�����ǹ�ϣ����ʽ rehash ����ϸ���裺
1.Ϊ ht[1] ����ռ䣬 ���ֵ�ͬʱ���� ht[0] �� ht[1] ������ϣ��
2.���ֵ���ά��һ���������������� rehashidx �� ��������ֵ����Ϊ 0 �� ��ʾ rehash ������ʽ��ʼ��rehashidx��ʾ����hashͰ��ǣ�
    ���ڲ��������Ǹ�hashͰtable[i]
3.�� rehash �����ڼ䣬 ÿ�ζ��ֵ�ִ����ӡ�ɾ�������һ��߸��²���ʱ�� �������ִ��ָ���Ĳ������⣬ ����˳���� ht[0] ��ϣ���� 
  rehashidx �����ϵ����м�ֵ�� rehash �� ht[1] �� �� rehash �������֮�� ���� rehashidx ���Ե�ֵ��һ��Ҳ����rehash�������ɶԸ�
  hash����Ͱtable[i]������ ɾ�� ���� ���µȲ��������ģ���������Ͱ����Ѹ�Ͱ�����ݷ���ht[1] hash����
4.�����ֵ�����Ĳ���ִ�У� ������ĳ��ʱ����ϣ� ht[0] �����м�ֵ�Զ��ᱻ rehash �� ht[1] �� ��ʱ���� rehashidx ���Ե�ֵ��Ϊ -1 �� ��ʾ rehash ��������ɡ�

����ʽ rehash �ĺô���������ȡ�ֶ���֮�ķ�ʽ�� �� rehash ��ֵ������ļ��㹤����̲�����ֵ��ÿ����ӡ�ɾ�������Һ͸��²����ϣ� 
�Ӷ������˼���ʽ rehash ���������Ӵ��������


����ʽ rehash ִ���ڼ�Ĺ�ϣ�����?

��Ϊ�ڽ��н���ʽ rehash �Ĺ����У� �ֵ��ͬʱʹ�� ht[0] �� ht[1] ������ϣ�� �����ڽ���ʽ rehash �����ڼ䣬 �ֵ��ɾ����delete����
���ң�find�������£�update���Ȳ�������������ϣ���Ͻ��У� ����˵�� Ҫ���ֵ��������һ�����Ļ��� ��������� ht[0] ������в��ң� 
���û�ҵ��Ļ��� �ͻ������ ht[1] ������в��ң� ������ࡣ

���⣬ �ڽ���ʽ rehash ִ���ڼ䣬 ����ӵ��ֵ�ļ�ֵ��һ�ɻᱻ���浽 ht[1] ���棬 �� ht[0] ���ٽ����κ���Ӳ����� 
��һ��ʩ��֤�� ht[0] �����ļ�ֵ��������ֻ�������� ������ rehash ������ִ�ж����ձ�ɿձ�


 rehash������ù�ϵ���ù���:dictAddRaw->_dictKeyIndex->_dictExpandIfNeeded(��������Ƿ���Ҫ����)->dictExpand 
//����hash����:serverCron->tryResizeHashTables->dictResize(��������������Ͱ��)->dictExpand 

ʵ�ʴ�ht[0]��ht[1]�Ĺ���:ÿ�� rehash �����ƶ���ϣ��������ĳ�������ϵ���������ڵ㣬���Դ� ht[0] Ǩ�Ƶ� ht[1] ��key 
        ���ܲ�ֹһ����������dictRehash
*/



/* This is our hash table structure. Every dictionary has two of this as we
 * implement incremental rehashing, for the old to the new table. */
/*
 * ��ϣ��
 *
 * ÿ���ֵ䶼ʹ��������ϣ���Ӷ�ʵ�ֽ���ʽ rehash ��
 */ 

/*
    �����������е��ַ����ȱ��浽����xxxCommand(��setCommand)����ʱ�����������ַ��������Ѿ�ת��ΪredisObject���浽redisClient->argv[]�У�
    Ȼ����setKey����غ����а�key-valueת��ΪdictEntry�ڵ��key��v(�ֱ��Ӧkey��value)��ӵ�dict->dictht->table[]  hash��,������ô��ӵ�dictEntry
    �ڵ��key��value�п��Բο�dict->type( typeΪxxxDictType ����keyptrDictType��) ����dictCreate
*/

 //��ʼ������ֵ��dictExpand
typedef struct dictht {//dictht hashͰ������dict�ṹ��

    //ÿ������table[i]�еĽڵ�����������dictEntry �ṹ��ʾ�� ÿ�� dictEntry �ṹ��������һ����ֵ�ԣ�
    // ��ϣ��ڵ�ָ�����飨�׳�Ͱ��bucket��
    // ��ϣ������
    dictEntry **table;//table[idx]ָ������׸�dictEntry�ڵ㣬��dictAddRaw  ������ʼ��tableͰ��dictExpand

    // ��ϣ���С
    unsigned long size;//��ʾ��hash��ʵ��Ͱ�ĸ���
    
    // ��ϣ���С���룬���ڼ�������ֵ
    // ���ǵ��� size - 1 // ָ������ĳ������룬���ڼ�������ֵ   ��Ч��_dictKeyIndex
    unsigned long sizemask; //sizemask = size-1 ��Ϊ�����Ͱ�Ǵ�0��size-1

    // �ù�ϣ�����нڵ������
    unsigned long used;

} dictht;

/*
�ֵ� API

����                                 ����                                         ʱ�临�Ӷ�


dictCreate                      ����һ���µ��ֵ䡣                                  O(1) 
dictAdd                         �������ļ�ֵ����ӵ��ֵ����档                      O(1) 
dictReplace                     �������ļ�ֵ����ӵ��ֵ����棬 ������Ѿ�
                                �������ֵ䣬��ô����ֵȡ��ԭ�е�ֵ��                O(1) 
dictFetchValue                  ���ظ�������ֵ��                                    O(1) 
dictGetRandomKey                ���ֵ����������һ����ֵ�ԡ�                        O(1) 
dictDelete                      ���ֵ���ɾ������������Ӧ�ļ�ֵ�ԡ�                  O(1) 
dictRelease                     �ͷŸ����ֵ䣬�Լ��ֵ��а��������м�ֵ�ԡ�          O(N) �� N Ϊ�ֵ�����ļ�ֵ�������� 
_dictExpandIfNeeded             hash���ݴ�С�������ж�

*/


/*
    �����������е��ַ����ȱ��浽����xxxCommand(��setCommand)����ʱ�����������ַ��������Ѿ�ת��ΪredisObject���浽redisClient->argv[]�У�
    Ȼ����setKey����غ����а�key-valueת��ΪdictEntry�ڵ��key��v(�ֱ��Ӧkey��value)��ӵ�dict->dictht->table[]  hash��,������ô��ӵ�dictEntry
    �ڵ��key��value�п��Բο�dict->type( typeΪxxxDictType ����keyptrDictType��) ����dictCreate
*/

/*
 * �ֵ�
 */
typedef struct dict {//dictCreate�����ͳ�ʼ��

    //type ������һ��ָ�� dictType �ṹ��ָ�룬 ÿ�� dictType �ṹ������һ�����ڲ����ض����ͼ�ֵ�Եĺ����� Redis ��Ϊ��;��
//ͬ���ֵ����ò�ͬ�������ض�������
    // �����ض�����
    dictType *type;

    // ˽������ // ���ʹ�������˽������  privdata �����򱣴�����Ҫ������Щ����type�ض������Ŀ�ѡ������
    void *privdata;

    /*
    ht ������һ����������������飬 �����е�ÿ�����һ�� dictht ��ϣ�� һ������£� �ֵ�ֻʹ�� ht[0] ��ϣ�� ht[1] ��ϣ��ֻ
    ���ڶ� ht[0] ��ϣ����� rehash ʱʹ�á�
    
    ���� ht[1] ֮�⣬ ��һ���� rehash �йص����Ծ��� rehashidx �� ����¼�� rehash Ŀǰ�Ľ��ȣ� ���Ŀǰû���ڽ��� rehash �� 
    ��ô����ֵΪ -1 ��
    */

    // ��ϣ��
    dictht ht[2];//dictht hashͰ��ʼ��������dictExpand     

    // rehash ����
    // �� rehash ���ڽ���ʱ��ֵΪ -1  // ��¼ rehash ���ȵı�־��ֵΪ-1 ��ʾ rehash δ����
    
    //�ж��Ƿ���Ҫrehash dictIsRehashing  _dictInit��ʼ��-1 //dictRehash��������dictExpand����0�������Ǩ�������-1
    int rehashidx; /* rehashing not in progress if rehashidx == -1 */

    // Ŀǰ�������еİ�ȫ������������
    int iterators; /* number of iterators currently running */

} dict; //dict�ռ䴴����ʼ����dictExpand����һ������_dictExpandIfNeededif->dictExpand(d, DICT_HT_INITIAL_SIZE);

/* If safe is set to 1 this is a safe iterator, that means, you can call
 * dictAdd, dictFind, and other functions against the dictionary even while
 * iterating. Otherwise it is a non safe iterator, and only dictNext()
 * should be called while iterating. */
/*
 * �ֵ������
 *
 * ��� safe ���Ե�ֵΪ 1 ����ô�ڵ������еĹ����У�
 * ������Ȼ����ִ�� dictAdd �� dictFind ���������������ֵ�����޸ġ�
 *
 * ��� safe ��Ϊ 1 ����ô����ֻ����� dictNext ���ֵ���е�����
 * �������ֵ�����޸ġ�
 */
typedef struct dictIterator {
        
    // ���������ֵ�
    dict *d;

    // table �����ڱ������Ĺ�ϣ����룬ֵ������ 0 �� 1 ��  dictht ht[2];�е���һ��
    // index ����������ǰ��ָ��Ĺ�ϣ������λ�á�  ��Ӧ�����Ͱ��λ��
    // safe ����ʶ����������Ƿ�ȫ
    int table, index, safe;

    // entry ����ǰ�������Ľڵ��ָ��
    // nextEntry ����ǰ�����ڵ����һ���ڵ�
    //             ��Ϊ�ڰ�ȫ����������ʱ�� entry ��ָ��Ľڵ���ܻᱻ�޸ģ�
    //             ������Ҫһ�������ָ����������һ�ڵ��λ�ã�
    //             �Ӷ���ָֹ�붪ʧ
    dictEntry *entry, *nextEntry;

    long long fingerprint; /* unsafe iterator fingerprint for misuse detection */
} dictIterator;

typedef void (dictScanFunction)(void *privdata, const dictEntry *de);

/* This is the initial size of every hash table */
/*
 * ��ϣ��ĳ�ʼ��С
 */
#define DICT_HT_INITIAL_SIZE     4

/* ------------------------------- Macros ------------------------------------*/
// �ͷŸ����ֵ�ڵ��ֵ
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata, (entry)->v.val)

// ���ø����ֵ�ڵ��ֵ
#define dictSetVal(d, entry, _val_) do { \
    if ((d)->type->valDup) \
        entry->v.val = (d)->type->valDup((d)->privdata, _val_); \
    else \
        entry->v.val = (_val_); \
} while(0)

// ��һ���з���������Ϊ�ڵ��ֵ
#define dictSetSignedIntegerVal(entry, _val_) \
    do { entry->v.s64 = _val_; } while(0)

// ��һ���޷���������Ϊ�ڵ��ֵ
#define dictSetUnsignedIntegerVal(entry, _val_) \
    do { entry->v.u64 = _val_; } while(0)

//dictType��Ҫ��xxxDictType(dbDictType zsetDictType setDictType��)
// �ͷŸ����ֵ�ڵ�ļ�
#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata, (entry)->key)

//dictType��Ҫ��xxxDictType(dbDictType zsetDictType setDictType��)

// ���ø����ֵ�ڵ�ļ�
#define dictSetKey(d, entry, _key_) do { \
    if ((d)->type->keyDup) \
        entry->key = (d)->type->keyDup((d)->privdata, _key_); \
    else \
        entry->key = (_key_); \
} while(0)

//dictType��Ҫ��xxxDictType(dbDictType zsetDictType setDictType��)

// �ȶ�������
#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privdata, key1, key2) : \
        (key1) == (key2))

// ����������Ĺ�ϣֵ
#define dictHashKey(d, key) (d)->type->hashFunction(key)
// ���ػ�ȡ�����ڵ�ļ�
#define dictGetKey(he) ((he)->key)
// ���ػ�ȡ�����ڵ��ֵ
#define dictGetVal(he) ((he)->v.val)
// ���ػ�ȡ�����ڵ���з�������ֵ
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
// ���ظ����ڵ���޷�������ֵ
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
// ���ظ����ֵ�Ĵ�С   hashͰ�ĸ���
#define dictSlots(d) ((d)->ht[0].size+(d)->ht[1].size)
// �����ֵ�����нڵ�����  ����Ͱ�нڵ�֮��
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)
// �鿴�ֵ��Ƿ����� rehash
#define dictIsRehashing(ht) ((ht)->rehashidx != -1)

/* API */
dict *dictCreate(dictType *type, void *privDataPtr);
int dictExpand(dict *d, unsigned long size);
int dictAdd(dict *d, void *key, void *val);
dictEntry *dictAddRaw(dict *d, void *key);
int dictReplace(dict *d, void *key, void *val);
dictEntry *dictReplaceRaw(dict *d, void *key);
int dictDelete(dict *d, const void *key);
int dictDeleteNoFree(dict *d, const void *key);
void dictRelease(dict *d);
dictEntry * dictFind(dict *d, const void *key);
void *dictFetchValue(dict *d, const void *key);
int dictResize(dict *d);
dictIterator *dictGetIterator(dict *d);
dictIterator *dictGetSafeIterator(dict *d);
dictEntry *dictNext(dictIterator *iter);
void dictReleaseIterator(dictIterator *iter);
dictEntry *dictGetRandomKey(dict *d);
int dictGetRandomKeys(dict *d, dictEntry **des, int count);
void dictPrintStats(dict *d);
unsigned int dictGenHashFunction(const void *key, int len);
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len);
void dictEmpty(dict *d, void(callback)(void*));
void dictEnableResize(void);
void dictDisableResize(void);
int dictRehash(dict *d, int n);
int dictRehashMilliseconds(dict *d, int ms);
void dictSetHashFunctionSeed(unsigned int initval);
unsigned int dictGetHashFunctionSeed(void);
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, void *privdata);

/* Hash table types */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;

#endif /* __DICT_H */
