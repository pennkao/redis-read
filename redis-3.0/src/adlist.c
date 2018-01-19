/* adlist.c - A generic doubly linked list implementation
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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


#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"

/* Create a new list. The created list can be freed with
 * AlFreeList(), but private value of every node need to be freed
 * by the user before to call AlFreeList().
 *
 * On error, NULL is returned. Otherwise the pointer to the new list. */
/*
 * ����һ���µ�����
 *
 * �����ɹ���������ʧ�ܷ��� NULL ��
 *
 * T = O(1)
 */
list *listCreate(void)
{
    struct list *list;

    // �����ڴ�
    if ((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;

    // ��ʼ������
    list->head = list->tail = NULL;
    list->len = 0;
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;

    return list;
}

/* Free the whole list.
 *
 * This function can't fail. */
/*
 * �ͷ����������Լ����������нڵ�
 *
 * T = O(N)
 */
void listRelease(list *list)
{
    unsigned long len;
    listNode *current, *next;

    // ָ��ͷָ��
    current = list->head;
    // ������������
    len = list->len;
    while(len--) {
        next = current->next;

        // ���������ֵ�ͷź�������ô������
        if (list->free) list->free(current->value);

        // �ͷŽڵ�ṹ
        zfree(current);

        current = next;
    }

    // �ͷ�����ṹ
    zfree(list);
}

/* Add a new node to the list, to head, contaning the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
/*
 * ��һ�������и���ֵָ�� value ���½ڵ���ӵ�����ı�ͷ
 *
 * ���Ϊ�½ڵ�����ڴ������ô��ִ���κζ����������� NULL
 *
 * ���ִ�гɹ������ش��������ָ��
 *
 * T = O(1)
 */
list *listAddNodeHead(list *list, void *value)
{
    listNode *node;

    // Ϊ�ڵ�����ڴ�
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    // ����ֵָ��
    node->value = value;

    // ��ӽڵ㵽������
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    // ��ӽڵ㵽�ǿ�����
    } else {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }

    // ��������ڵ���
    list->len++;

    return list;
}

/* Add a new node to the list, to tail, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
/*
 * ��һ�������и���ֵָ�� value ���½ڵ���ӵ�����ı�β
 *
 * ���Ϊ�½ڵ�����ڴ������ô��ִ���κζ����������� NULL
 *
 * ���ִ�гɹ������ش��������ָ��
 *
 * T = O(1)
 */
list *listAddNodeTail(list *list, void *value)
{
    listNode *node;

    // Ϊ�½ڵ�����ڴ�
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    // ����ֵָ��
    node->value = value;

    // Ŀ������Ϊ��
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    // Ŀ������ǿ�
    } else {
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }

    // ��������ڵ���
    list->len++;

    return list;
}

/*
 * ����һ������ֵ value ���½ڵ㣬���������뵽 old_node ��֮ǰ��֮��
 *
 * ��� after Ϊ 0 �����½ڵ���뵽 old_node ֮ǰ��
 * ��� after Ϊ 1 �����½ڵ���뵽 old_node ֮��
 *
 * T = O(1)
 */
list *listInsertNode(list *list, listNode *old_node, void *value, int after) {
    listNode *node;

    // �����½ڵ�
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    // ����ֵ
    node->value = value;

    // ���½ڵ���ӵ������ڵ�֮��
    if (after) {
        node->prev = old_node;
        node->next = old_node->next;
        // �����ڵ���ԭ��β�ڵ�
        if (list->tail == old_node) {
            list->tail = node;
        }
    // ���½ڵ���ӵ������ڵ�֮ǰ
    } else {
        node->next = old_node;
        node->prev = old_node->prev;
        // �����ڵ���ԭ��ͷ�ڵ�
        if (list->head == old_node) {
            list->head = node;
        }
    }

    // �����½ڵ��ǰ��ָ��
    if (node->prev != NULL) {
        node->prev->next = node;
    }
    // �����½ڵ�ĺ���ָ��
    if (node->next != NULL) {
        node->next->prev = node;
    }

    // ��������ڵ���
    list->len++;

    return list;
}

/* Remove the specified node from the specified list.
 * It's up to the caller to free the private value of the node.
 *
 * This function can't fail. */
/*
 * ������ list ��ɾ�������ڵ� node 
 * 
 * �Խڵ�˽��ֵ(private value of the node)���ͷŹ����ɵ����߽��С�
 *
 * T = O(1)
 */
void listDelNode(list *list, listNode *node)
{
    // ����ǰ�ýڵ��ָ��
    if (node->prev)
        node->prev->next = node->next;
    else
        list->head = node->next;

    // �������ýڵ��ָ��
    if (node->next)
        node->next->prev = node->prev;
    else
        list->tail = node->prev;

    // �ͷ�ֵ
    if (list->free) list->free(node->value);

    // �ͷŽڵ�
    zfree(node);

    // ��������һ
    list->len--;
}

/* Returns a list iterator 'iter'. After the initialization every
 * call to listNext() will return the next element of the list.
 *
 * This function can't fail. */
/*
 * Ϊ����������һ����������
 * ֮��ÿ�ζ�������������� listNext �����ر�������������ڵ�
 *
 * direction ���������˵������ĵ�������
 *  AL_START_HEAD ���ӱ�ͷ���β����
 *  AL_START_TAIL ���ӱ�β���ͷ����
 *
 * T = O(1)
 */  //��ȡ�б�list���ײ��׶λ���β�����
listIter *listGetIterator(list *list, int direction)
{
    // Ϊ�����������ڴ�
    listIter *iter;
    if ((iter = zmalloc(sizeof(*iter))) == NULL) return NULL;

    // ���ݵ����������õ���������ʼ�ڵ�
    if (direction == AL_START_HEAD)
        iter->next = list->head;
    else
        iter->next = list->tail;

    // ��¼��������
    iter->direction = direction;

    return iter;
}

/* Release the iterator memory */
/*
 * �ͷŵ�����
 *
 * T = O(1)
 */
void listReleaseIterator(listIter *iter) {
    zfree(iter);
}

/* Create an iterator in the list private iterator structure */
/*
 * ���������ķ�������Ϊ AL_START_HEAD ��
 * ��������ָ������ָ���ͷ�ڵ㡣
 *
 * T = O(1)
 */
void listRewind(list *list, listIter *li) {
    li->next = list->head;
    li->direction = AL_START_HEAD;
}

/*
 * ���������ķ�������Ϊ AL_START_TAIL ��
 * ��������ָ������ָ���β�ڵ㡣
 *
 * T = O(1)
 */
void listRewindTail(list *list, listIter *li) {
    li->next = list->tail;
    li->direction = AL_START_TAIL;
}

/* Return the next element of an iterator.
 * It's valid to remove the currently returned element using
 * listDelNode(), but not to remove other elements.
 *
 * The function returns a pointer to the next element of the list,
 * or NULL if there are no more elements, so the classical usage patter
 * is:
 *
 * iter = listGetIterator(list,<direction>);
 * while ((node = listNext(iter)) != NULL) {
 *     doSomethingWith(listNodeValue(node));
 * }
 *
 * */
/*
 * ���ص�������ǰ��ָ��Ľڵ㡣
 *
 * ɾ����ǰ�ڵ�������ģ��������޸�������������ڵ㡣
 *
 * ����Ҫô����һ���ڵ㣬Ҫô���� NULL ���������÷��ǣ�
 *
 * iter = listGetIterator(list,<direction>);
 * while ((node = listNext(iter)) != NULL) {
 *     doSomethingWith(listNodeValue(node));
 * }
 *
 * T = O(1)
 */
listNode *listNext(listIter *iter)
{
    listNode *current = iter->next;

    if (current != NULL) {
        // ���ݷ���ѡ����һ���ڵ�
        if (iter->direction == AL_START_HEAD)
            // ������һ���ڵ㣬��ֹ��ǰ�ڵ㱻ɾ�������ָ�붪ʧ
            iter->next = current->next;
        else
            // ������һ���ڵ㣬��ֹ��ǰ�ڵ㱻ɾ�������ָ�붪ʧ
            iter->next = current->prev;
    }

    return current;
}

/* Duplicate the whole list. On out of memory NULL is returned.
 * On success a copy of the original list is returned.
 *
 * The 'Dup' method set with listSetDupMethod() function is used
 * to copy the node value. Otherwise the same pointer value of
 * the original node is used as value of the copied node.
 *
 * The original list both on success or error is never modified. */
/*
 * ������������
 *
 * ���Ƴɹ�������������ĸ�����
 * �����Ϊ�ڴ治�����ɸ���ʧ�ܣ����� NULL ��
 *
 * �������������ֵ���ƺ��� dup ����ô��ֵ�ĸ��ƽ�ʹ�ø��ƺ������У�
 * �����½ڵ㽫�;ɽڵ㹲��ͬһ��ָ�롣
 *
 * ���۸����ǳɹ�����ʧ�ܣ�����ڵ㶼�����޸ġ�
 *
 * T = O(N)
 */
list *listDup(list *orig)
{
    list *copy;
    listIter *iter;
    listNode *node;

    // ����������
    if ((copy = listCreate()) == NULL)
        return NULL;

    // ���ýڵ�ֵ������
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;

    // ����������������
    iter = listGetIterator(orig, AL_START_HEAD);
    while((node = listNext(iter)) != NULL) {
        void *value;

        // ���ƽڵ�ֵ���½ڵ�
        if (copy->dup) {
            value = copy->dup(node->value);
            if (value == NULL) {
                listRelease(copy);
                listReleaseIterator(iter);
                return NULL;
            }
        } else
            value = node->value;

        // ���ڵ���ӵ�����
        if (listAddNodeTail(copy, value) == NULL) {
            listRelease(copy);
            listReleaseIterator(iter);
            return NULL;
        }
    }

    // �ͷŵ�����
    listReleaseIterator(iter);

    // ���ظ���
    return copy;
}

/* Search the list for a node matching a given key.
 * The match is performed using the 'match' method
 * set with listSetMatchMethod(). If no 'match' method
 * is set, the 'value' pointer of every node is directly
 * compared with the 'key' pointer.
 *
 * On success the first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned. */
/* 
 * �������� list ��ֵ�� key ƥ��Ľڵ㡣
 * 
 * �ԱȲ���������� match ����������У�
 * ���û������ match ������
 * ��ôֱ��ͨ���Ա�ֵ��ָ���������Ƿ�ƥ�䡣
 *
 * ���ƥ��ɹ�����ô��һ��ƥ��Ľڵ�ᱻ���ء�
 * ���û��ƥ���κνڵ㣬��ô���� NULL ��
 *
 * T = O(N)
 */
listNode *listSearchKey(list *list, void *key)
{
    listIter *iter;
    listNode *node;

    // ������������
    iter = listGetIterator(list, AL_START_HEAD);
    while((node = listNext(iter)) != NULL) {
        
        // �Ա�
        if (list->match) {
            if (list->match(node->value, key)) {
                listReleaseIterator(iter);
                // �ҵ�
                return node;
            }
        } else {
            if (key == node->value) {
                listReleaseIterator(iter);
                // �ҵ�
                return node;
            }
        }
    }
    
    listReleaseIterator(iter);

    // δ�ҵ�
    return NULL;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimate
 * and so on. If the index is out of range NULL is returned. */
/*
 * ���������ڸ��������ϵ�ֵ��
 *
 * ������ 0 Ϊ��ʼ��Ҳ�����Ǹ����� -1 ��ʾ�������һ���ڵ㣬������ࡣ
 *
 * �������������Χ��out of range�������� NULL ��
 *
 * T = O(N)
 */
listNode *listIndex(list *list, long index) {
    listNode *n;

    // �������Ϊ�������ӱ�β��ʼ����
    if (index < 0) {
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    // �������Ϊ�������ӱ�ͷ��ʼ����
    } else {
        n = list->head;
        while(index-- && n) n = n->next;
    }

    return n;
}

/* Rotate the list removing the tail node and inserting it to the head. */
/*
 * ȡ������ı�β�ڵ㣬�������ƶ�����ͷ����Ϊ�µı�ͷ�ڵ㡣
 *
 * T = O(1)
 */
void listRotate(list *list) {
    listNode *tail = list->tail;

    if (listLength(list) <= 1) return;

    /* Detach current tail */
    // ȡ����β�ڵ�
    list->tail = tail->prev;
    list->tail->next = NULL;

    /* Move it as head */
    // ���뵽��ͷ
    list->head->prev = tail;
    tail->prev = NULL;
    tail->next = list->head;
    list->head = tail;
}
