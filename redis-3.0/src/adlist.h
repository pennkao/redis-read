/* adlist.h - A generic doubly linked list implementation
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

#ifndef __ADLIST_H__
#define __ADLIST_H__

/* Node, List, and Iterator are the only data structures used currently. */

/*
 * ˫������ڵ�
 */
typedef struct listNode {

    // ǰ�ýڵ�
    struct listNode *prev; //�����list��ͷ��㣬��prevָ��NULL

    // ���ýڵ�
    struct listNode *next;//�����listβ����㣬��nextָ��NULL

    // �ڵ��ֵ
    void *value;

} listNode;

/*
 * ˫�����������
 */
typedef struct listIter {

    // ��ǰ�������Ľڵ�
    listNode *next;

    // �����ķ���
    int direction; //ȡֵAL_START_HEAD��

} listIter;


/*
���������ڵ�� API

�� 3-1 �г����������ڲ������������ڵ�� API ��


�� 3-1 ���������ڵ� API

����                                                        ����                                                ʱ�临�Ӷ�

listSetDupMethod    �������ĺ�������Ϊ����Ľڵ�ֵ���ƺ�����                                O(1) �� 
listGetDupMethod    ��������ǰ����ʹ�õĽڵ�ֵ���ƺ�����                  ���ƺ�������ͨ������� dup ����ֱ�ӻ�ã� O(1) 
listSetFreeMethod   �������ĺ�������Ϊ����Ľڵ�ֵ�ͷź�����                 O(1) �� 
listGetFree         ��������ǰ����ʹ�õĽڵ�ֵ�ͷź�����                  �ͷź�������ͨ������� free ����ֱ�ӻ�ã� O(1) 
listSetMatchMethod  �������ĺ�������Ϊ����Ľڵ�ֵ�ԱȺ�����                 O(1) 
listGetMatchMethod  ��������ǰ����ʹ�õĽڵ�ֵ�ԱȺ�����                  �ԱȺ�������ͨ������� match ����ֱ�ӻ�ã� O(1) 
listLength          ��������ĳ��ȣ������˶��ٸ��ڵ㣩��                    �����ȿ���ͨ������� len ����ֱ�ӻ�ã� O(1) �� 
listFirst           ��������ı�ͷ�ڵ㡣                                    ��ͷ�ڵ����ͨ������� head ����ֱ�ӻ�ã� O(1) �� 
listLast            ��������ı�β�ڵ㡣                                    ��β�ڵ����ͨ������� tail ����ֱ�ӻ�ã� O(1) �� 
listPrevNode        ���ظ����ڵ��ǰ�ýڵ㡣                                ǰ�ýڵ����ͨ���ڵ�� prev ����ֱ�ӻ�ã� O(1) �� 
listNextNode        ���ظ����ڵ�ĺ��ýڵ㡣                                ���ýڵ����ͨ���ڵ�� next ����ֱ�ӻ�ã� O(1) �� 
listNodeValue       ���ظ����ڵ�Ŀǰ���ڱ����ֵ��                          �ڵ�ֵ����ͨ���ڵ�� value ����ֱ�ӻ�ã� O(1) �� 
listCreate          ����һ���������κνڵ��������                         O(1) 
listAddNodeHead     ��һ����������ֵ���½ڵ���ӵ���������ı�ͷ��           O(1) 
listAddNodeTail     ��һ����������ֵ���½ڵ���ӵ���������ı�β��           O(1) 
listInsertNode      ��һ����������ֵ���½ڵ���ӵ������ڵ��֮ǰ����֮��   O(1) 
listSearchKey       ���Ҳ����������а�������ֵ�Ľڵ㡣                       O(N) �� N Ϊ�����ȡ� 
listIndex           ���������ڸ��������ϵĽڵ㡣                             O(N) �� N Ϊ�����ȡ� 
listDelNode         ��������ɾ�������ڵ㡣                                   O(1) �� 
listRotate          ������ı�β�ڵ㵯����Ȼ�󽫱������Ľڵ���뵽����
                    �ı�ͷ�� ��Ϊ�µı�ͷ�ڵ㡣                              O(1) 
listDup             ����һ����������ĸ�����                                 O(N) �� N Ϊ�����ȡ� 
listRelease         �ͷŸ��������Լ������е����нڵ㡣                      O(N) �� N Ϊ�����ȡ� 
*/


/*
 dup �������ڸ�������ڵ��������ֵ��
 free ���������ͷ�����ڵ��������ֵ��
 match ���������ڶԱ�����ڵ��������ֵ����һ������ֵ�Ƿ���ȡ�

 Redis ������ʵ�ֵ����Կ����ܽ����£�
 ?˫�ˣ� ����ڵ���� prev �� next ָ�룬 ��ȡĳ���ڵ��ǰ�ýڵ�ͺ��ýڵ�ĸ��Ӷȶ��� O(1) ��
 ?�޻��� ��ͷ�ڵ�� prev ָ��ͱ�β�ڵ�� next ָ�붼ָ�� NULL �� ������ķ����� NULL Ϊ�յ㡣
 ?����ͷָ��ͱ�βָ�룺 ͨ�� list �ṹ�� head ָ��� tail ָ�룬 �����ȡ����ı�ͷ�ڵ�ͱ�β�ڵ�ĸ��Ӷ�Ϊ O(1) ��
 ?�������ȼ������� ����ʹ�� list �ṹ�� len �������� list ���е�����ڵ���м����� �����ȡ�����нڵ������ĸ��Ӷ�Ϊ O(1) ��
 ?��̬�� ����ڵ�ʹ�� void* ָ��������ڵ�ֵ�� ���ҿ���ͨ�� list �ṹ�� dup �� free �� match ��������Ϊ�ڵ�ֵ���������ض������� ��������������ڱ�����ֲ�ͬ���͵�ֵ��

 */ //listCreate��������

/*
 * ˫������ṹ
 */
typedef struct list {

    // ��ͷ�ڵ�
    listNode *head;

    // ��β�ڵ�
    listNode *tail;

    // �ڵ�ֵ���ƺ���
    void *(*dup)(void *ptr);

    // �ڵ�ֵ�ͷź���
    void (*free)(void *ptr);

    // �ڵ�ֵ�ԱȺ���
    int (*match)(void *ptr, void *key);

    // �����������Ľڵ�����
    unsigned long len;

} list;

/* Functions implemented as macros */
// ���ظ��������������Ľڵ�����
// T = O(1)
#define listLength(l) ((l)->len)
// ���ظ�������ı�ͷ�ڵ�
// T = O(1)
#define listFirst(l) ((l)->head)
// ���ظ�������ı�β�ڵ�
// T = O(1)
#define listLast(l) ((l)->tail)
// ���ظ����ڵ��ǰ�ýڵ�
// T = O(1)
#define listPrevNode(n) ((n)->prev)
// ���ظ����ڵ�ĺ��ýڵ�
// T = O(1)
#define listNextNode(n) ((n)->next)
// ���ظ����ڵ��ֵ
// T = O(1)
#define listNodeValue(n) ((n)->value)

// ������ l ��ֵ���ƺ�������Ϊ m
// T = O(1)
#define listSetDupMethod(l,m) ((l)->dup = (m))
// ������ l ��ֵ�ͷź�������Ϊ m
// T = O(1)
#define listSetFreeMethod(l,m) ((l)->free = (m))
// ������ĶԱȺ�������Ϊ m
// T = O(1)
#define listSetMatchMethod(l,m) ((l)->match = (m))

// ���ظ��������ֵ���ƺ���
// T = O(1)
#define listGetDupMethod(l) ((l)->dup)
// ���ظ��������ֵ�ͷź���
// T = O(1)
#define listGetFree(l) ((l)->free)
// ���ظ��������ֵ�ԱȺ���
// T = O(1)
#define listGetMatchMethod(l) ((l)->match)

/* Prototypes */
list *listCreate(void);
void listRelease(list *list);
list *listAddNodeHead(list *list, void *value);
list *listAddNodeTail(list *list, void *value);
list *listInsertNode(list *list, listNode *old_node, void *value, int after);
void listDelNode(list *list, listNode *node);
listIter *listGetIterator(list *list, int direction);
listNode *listNext(listIter *iter);
void listReleaseIterator(listIter *iter);
list *listDup(list *orig);
listNode *listSearchKey(list *list, void *key);
listNode *listIndex(list *list, long index);
void listRewind(list *list, listIter *li);
void listRewindTail(list *list, listIter *li);
void listRotate(list *list);

/* Directions for iterators 
 *
 * ���������е����ķ���
 */
// �ӱ�ͷ���β���е���
#define AL_START_HEAD 0
// �ӱ�β����ͷ���е���
#define AL_START_TAIL 1

#endif /* __ADLIST_H__ */
