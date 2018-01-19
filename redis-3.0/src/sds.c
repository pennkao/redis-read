/* SDSLib, A C dynamic strings library
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "sds.h"
#include "zmalloc.h"

/*
 * ���ݸ����ĳ�ʼ���ַ��� init ���ַ������� initlen
 * ����һ���µ� sds
 *
 * ����
 *  init ����ʼ���ַ���ָ��
 *  initlen ����ʼ���ַ����ĳ���
 *
 * ����ֵ
 *  sds �������ɹ����� sdshdr ���Ӧ�� sds
 *        ����ʧ�ܷ��� NULL
 *
 * ���Ӷ�
 *  T = O(N)
 */
/* Create a new sds string with the content specified by the 'init' pointer
 * and 'initlen'.
 * If NULL is used for 'init' the string is initialized with zero bytes.
 *
 * The string is always null-termined (all the sds strings are, always) so
 * even if you create an sds string with:
 *
 * mystring = sdsnewlen("abc",3");
 *
 * You can print the string with printf() as there is an implicit \0 at the
 * end of the string. However the string is binary safe and can contain
 * \0 characters in the middle, as the length is stored in the sds header. */
sds sdsnewlen(const void *init, size_t initlen) {

    struct sdshdr *sh;

    // �����Ƿ��г�ʼ�����ݣ�ѡ���ʵ����ڴ���䷽ʽ
    // T = O(N)
    if (init) {
        // zmalloc ����ʼ����������ڴ�
        sh = zmalloc(sizeof(struct sdshdr)+initlen+1);
    } else {
        // zcalloc ��������ڴ�ȫ����ʼ��Ϊ 0
        sh = zcalloc(sizeof(struct sdshdr)+initlen+1);
    }

    // �ڴ����ʧ�ܣ�����
    if (sh == NULL) return NULL;

    // ���ó�ʼ������
    sh->len = initlen;
    // �� sds ��Ԥ���κοռ�
    sh->free = 0;
    // �����ָ����ʼ�����ݣ������Ǹ��Ƶ� sdshdr �� buf ��
    // T = O(N)
    if (initlen && init)
        memcpy(sh->buf, init, initlen);
    // �� \0 ��β
    sh->buf[initlen] = '\0';

    // ���� buf ���֣����������� sdshdr
    return (char*)sh->buf;
}

/*
 * ����������һ��ֻ�����˿��ַ��� "" �� sds
 *
 * ����ֵ
 *  sds �������ɹ����� sdshdr ���Ӧ�� sds
 *        ����ʧ�ܷ��� NULL
 *
 * ���Ӷ�
 *  T = O(1)
 */
/* Create an empty (zero length) sds string. Even in this case the string
 * always has an implicit null term. */
sds sdsempty(void) {
    return sdsnewlen("",0);
}

/*
 * ���ݸ����ַ��� init ������һ������ͬ���ַ����� sds
 *
 * ����
 *  init ���������Ϊ NULL ����ô����һ���հ� sds
 *         �����´����� sds �а����� init ������ͬ�ַ���
 *
 * ����ֵ
 *  sds �������ɹ����� sdshdr ���Ӧ�� sds
 *        ����ʧ�ܷ��� NULL
 *
 * ���Ӷ�
 *  T = O(N)
 */
/* Create a new sds string starting from a null termined C string. */
sds sdsnew(const char *init) {
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init, initlen);
}

/*
 * ���Ƹ��� sds �ĸ���
 *
 * ����ֵ
 *  sds �������ɹ��������� sds �ĸ���
 *        ����ʧ�ܷ��� NULL
 *
 * ���Ӷ�
 *  T = O(N)
 */
/* Duplicate an sds string. */
sds sdsdup(const sds s) {
    return sdsnewlen(s, sdslen(s));
}

/*
 * �ͷŸ����� sds
 *
 * ���Ӷ�
 *  T = O(N)
 */
/* Free an sds string. No operation is performed if 's' is NULL. */
void sdsfree(sds s) {
    if (s == NULL) return;
    zfree(s-sizeof(struct sdshdr));
}

// δʹ�ú����������ѷ���
/* Set the sds string length to the length as obtained with strlen(), so
 * considering as content only up to the first null term character.
 *
 * This function is useful when the sds string is hacked manually in some
 * way, like in the following example:
 *
 * s = sdsnew("foobar");
 * s[2] = '\0';
 * sdsupdatelen(s);
 * printf("%d\n", sdslen(s));
 *
 * The output will be "2", but if we comment out the call to sdsupdatelen()
 * the output will be "6" as the string was modified but the logical length
 * remains 6 bytes. */
void sdsupdatelen(sds s) {
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    int reallen = strlen(s);
    sh->free += (sh->len-reallen);
    sh->len = reallen;
}

/*
 * �ڲ��ͷ� SDS ���ַ����ռ������£�
 * ���� SDS ��������ַ���Ϊ���ַ�����
 *
 * ���Ӷ�
 *  T = O(1)
 */
/* Modify an sds string on-place to make it empty (zero length).
 * However all the existing buffer is not discarded but set as free space
 * so that next append operations will not require allocations up to the
 * number of bytes previously available. */
void sdsclear(sds s) {

    // ȡ�� sdshdr
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));

    // ���¼�������
    sh->free += sh->len;
    sh->len = 0;

    // ���������ŵ���ǰ�棨�൱�ڶ��Ե�ɾ�� buf �е����ݣ�
    sh->buf[0] = '\0';
}

/* Enlarge the free space at the end of the sds string so that the caller
 * is sure that after calling this function can overwrite up to addlen
 * bytes after the end of the string, plus one more byte for nul term.
 * 
 * Note: this does not change the *length* of the sds string as returned
 * by sdslen(), but only the free buffer space we have. */

/*
�ռ�Ԥ����?
�ռ�Ԥ���������Ż� SDS ���ַ������������� �� SDS �� API ��һ�� SDS �����޸ģ� ������Ҫ�� SDS ���пռ���չ��ʱ�� ���򲻽���Ϊ 
SDS �����޸�������Ҫ�Ŀռ䣬 ����Ϊ SDS ��������δʹ�ÿռ䡣

���У� ��������δʹ�ÿռ����������¹�ʽ������
����� SDS �����޸�֮�� SDS �ĳ��ȣ�Ҳ���� len ���Ե�ֵ����С�� 1 MB �� ��ô�������� len ����ͬ����С��δʹ�ÿռ䣬 ��ʱ 
SDS len ���Ե�ֵ���� free ���Ե�ֵ��ͬ�� �ٸ����ӣ� ��������޸�֮�� SDS �� len ����� 13 �ֽڣ� ��ô����Ҳ����� 13 �ֽڵ�
δʹ�ÿռ䣬 SDS �� buf �����ʵ�ʳ��Ƚ���� 13 + 13 + 1 = 27 �ֽڣ������һ�ֽ����ڱ�����ַ�����

����� SDS �����޸�֮�� SDS �ĳ��Ƚ����ڵ��� 1 MB �� ��ô�������� 1 MB ��δʹ�ÿռ䡣 �ٸ����ӣ� ��������޸�֮�� SDS 
�� len ����� 30 MB �� ��ô�������� 1 MB ��δʹ�ÿռ䣬 SDS �� buf �����ʵ�ʳ��Ƚ�Ϊ 30 MB + 1 MB + 1 byte ��

ͨ���ռ�Ԥ������ԣ� Redis ���Լ�������ִ���ַ�����������������ڴ��ط�������� 
ͨ������Ԥ������ԣ� SDS ���������� N ���ַ���������ڴ��ط�������ӱض� N �ν���Ϊ��� N �Ρ� ��sdscat
*/

/*
 * �� sds �� buf �ĳ��Ƚ�����չ��ȷ���ں���ִ��֮��
 * buf ���ٻ��� addlen + 1 ���ȵĿ���ռ�
 * ������� 1 �ֽ���Ϊ \0 ׼���ģ�
 *
 * ����ֵ
 *  sds ����չ�ɹ�������չ��� sds
 *        ��չʧ�ܷ��� NULL
 *
 * ���Ӷ�
 *  T = O(N)
 */
sds sdsMakeRoomFor(sds s, size_t addlen) {

    struct sdshdr *sh, *newsh;

    // ��ȡ s Ŀǰ�Ŀ���ռ䳤��
    size_t free = sdsavail(s);

    size_t len, newlen;

    // s Ŀǰ�Ŀ���ռ��Ѿ��㹻�������ٽ�����չ��ֱ�ӷ���
    if (free >= addlen) return s;

    // ��ȡ s Ŀǰ��ռ�ÿռ�ĳ���
    len = sdslen(s);
    sh = (void*) (s-(sizeof(struct sdshdr)));

    // s ������Ҫ�ĳ���
    newlen = (len+addlen);

    // �����³��ȣ�Ϊ s �����¿ռ�����Ĵ�С
    if (newlen < SDS_MAX_PREALLOC)
        // ����³���С�� SDS_MAX_PREALLOC 
        // ��ôΪ���������������賤�ȵĿռ�
        newlen *= 2;
    else
        // ���򣬷��䳤��ΪĿǰ���ȼ��� SDS_MAX_PREALLOC
        newlen += SDS_MAX_PREALLOC;
    // T = O(N)
    newsh = zrealloc(sh, sizeof(struct sdshdr)+newlen+1);

    // �ڴ治�㣬����ʧ�ܣ�����
    if (newsh == NULL) return NULL;

    // ���� sds �Ŀ��೤��
    newsh->free = newlen - len;

    // ���� sds
    return newsh->buf;
}

/*
 * ���� sds �еĿ��пռ䣬
 * ���ղ���� sds �б�����ַ����������κ��޸ġ�
 *
 * ����ֵ
 *  sds ���ڴ������� sds
 *
 * ���Ӷ�
 *  T = O(N)
 */
/* Reallocate the sds string so that it has no free space at the end. The
 * contained string remains not altered, but next concatenation operations
 * will require a reallocation.
 *
 * After the call, the passed sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */
sds sdsRemoveFreeSpace(sds s) {
    struct sdshdr *sh;

    sh = (void*) (s-(sizeof(struct sdshdr)));

    // �����ڴ��ط��䣬�� buf �ĳ��Ƚ����㹻�����ַ�������
    // T = O(N) ����֮ǰ�Ŀռ���sizeof(struct sdshdr)+sh->len +10000��������zreallocֻ�Ƿ���sizeof(struct sdshdr)+sh->len��������10000�ֽھͱ�ϵͳ������
    sh = zrealloc(sh, sizeof(struct sdshdr)+sh->len+1);

    // ����ռ�Ϊ 0
    sh->free = 0;

    return sh->buf;
}

/*
 * ���ظ��� sds ������ڴ��ֽ���
 *
 * ���Ӷ�
 *  T = O(1)
 */
/* Return the total size of the allocation of the specifed sds string,
 * including:
 * 1) The sds header before the pointer.
 * 2) The string.
 * 3) The free buffer at the end if any.
 * 4) The implicit null term.
 */
size_t sdsAllocSize(sds s) {
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));

    return sizeof(*sh)+sh->len+sh->free+1;
}

/* Increment the sds length and decrements the left free space at the
 * end of the string according to 'incr'. Also set the null term
 * in the new end of the string.
 *
 * ���� incr ���������� sds �ĳ��ȣ���������ռ䣬
 * ���� \0 �ŵ����ַ�����β��
 *
 * This function is used in order to fix the string length after the
 * user calls sdsMakeRoomFor(), writes something after the end of
 * the current string, and finally needs to set the new length.
 *
 * ����������ڵ��� sdsMakeRoomFor() ���ַ���������չ��
 * Ȼ���û����ַ���β��д����ĳЩ����֮��
 * ������ȷ���� free �� len ���Եġ�
 *
 * Note: it is possible to use a negative increment in order to
 * right-trim the string.
 *
 * ��� incr ����Ϊ��������ô���ַ��������ҽضϲ�����
 *
 * Usage example:
 *
 * Using sdsIncrLen() and sdsMakeRoomFor() it is possible to mount the
 * following schema, to cat bytes coming from the kernel to the end of an
 * sds string without copying into an intermediate buffer:
 *
 * ������ sdsIncrLen ��������
 *
 * oldlen = sdslen(s);
 * s = sdsMakeRoomFor(s, BUFFER_SIZE);
 * nread = read(fd, s+oldlen, BUFFER_SIZE);
 * ... check for nread <= 0 and handle it ...
 * sdsIncrLen(s, nread);
 *
 * ���Ӷ�
 *  T = O(1)
 */
void sdsIncrLen(sds s, int incr) {
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));

    // ȷ�� sds �ռ��㹻
    assert(sh->free >= incr);

    // ��������
    sh->len += incr;
    sh->free -= incr;

    // ��� assert ��ʵ���Ժ���
    // ��Ϊǰһ�� assert �Ѿ�ȷ�� sh->free - incr >= 0 ��
    assert(sh->free >= 0);

    // �����µĽ�β����
    s[sh->len] = '\0';
}

/* Grow the sds to have the specified length. Bytes that were not part of
 * the original length of the sds will be set to zero.
 *
 * if the specified length is smaller than the current length, no operation
 * is performed. */
/*
 * �� sds ������ָ�����ȣ�δʹ�õĿռ��� 0 �ֽ���䡣
 *
 * ����ֵ
 *  sds ������ɹ������� sds ��ʧ�ܷ��� NULL
 *
 * ���Ӷȣ�
 *  T = O(N)
 */  // �ÿ��ַ��� SDS ��չ���������ȡ�   O(N) �� N Ϊ��չ�������ֽ����� 
sds sdsgrowzero(sds s, size_t len) {
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t totlen, curlen = sh->len;

    // ��� len ���ַ��������г���С��
    // ��ôֱ�ӷ��أ���������
    if (len <= curlen) return s;

    // ��չ sds
    // T = O(N)
    s = sdsMakeRoomFor(s,len-curlen);
    // ����ڴ治�㣬ֱ�ӷ���
    if (s == NULL) return NULL;

    /* Make sure added region doesn't contain garbage */
    // ���·���Ŀռ��� 0 ��䣬��ֹ������������
    // T = O(N)
    sh = (void*)(s-(sizeof(struct sdshdr)));
    memset(s+curlen,0,(len-curlen+1)); /* also set trailing \0 byte */

    // ��������
    totlen = sh->len+sh->free;
    sh->len = len;
    sh->free = totlen-sh->len;

    // �����µ� sds
    return s;
}

/*
 * ������Ϊ len ���ַ��� t ׷�ӵ� sds ���ַ���ĩβ
 *
 * ����ֵ
 *  sds ��׷�ӳɹ������� sds ��ʧ�ܷ��� NULL
 *
 * ���Ӷ�
 *  T = O(N)
 */
/* Append the specified binary-safe string pointed by 't' of 'len' bytes to the
 * end of the specified sds string 's'.
 *
 * After the call, the passed sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */
sds sdscatlen(sds s, const void *t, size_t len) {
    
    struct sdshdr *sh;
    
    // ԭ���ַ�������
    size_t curlen = sdslen(s);

    // ��չ sds �ռ�
    // T = O(N)
    s = sdsMakeRoomFor(s,len);

    // �ڴ治�㣿ֱ�ӷ���
    if (s == NULL) return NULL;

    // ���� t �е����ݵ��ַ�����
    // T = O(N)
    sh = (void*) (s-(sizeof(struct sdshdr)));
    memcpy(s+curlen, t, len);

    // ��������
    sh->len = curlen+len;
    sh->free = sh->free-len;

    // ����½�β����
    s[curlen+len] = '\0';

    // ������ sds
    return s;
}

/*
 * �������ַ��� t ׷�ӵ� sds ��ĩβ
 * 
 * ����ֵ
 *  sds ��׷�ӳɹ������� sds ��ʧ�ܷ��� NULL
 *
 * ���Ӷ�
 *  T = O(N)
 */
/* Append the specified null termianted C string to the sds string 's'.
 *
 * After the call, the passed sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */

/*
�ռ�Ԥ����?
�ռ�Ԥ���������Ż� SDS ���ַ������������� �� SDS �� API ��һ�� SDS �����޸ģ� ������Ҫ�� SDS ���пռ���չ��ʱ�� ���򲻽���Ϊ 
SDS �����޸�������Ҫ�Ŀռ䣬 ����Ϊ SDS ��������δʹ�ÿռ䡣

���У� ��������δʹ�ÿռ����������¹�ʽ������
����� SDS �����޸�֮�� SDS �ĳ��ȣ�Ҳ���� len ���Ե�ֵ����С�� 1 MB �� ��ô�������� len ����ͬ����С��δʹ�ÿռ䣬 ��ʱ 
SDS len ���Ե�ֵ���� free ���Ե�ֵ��ͬ�� �ٸ����ӣ� ��������޸�֮�� SDS �� len ����� 13 �ֽڣ� ��ô����Ҳ����� 13 �ֽڵ�
δʹ�ÿռ䣬 SDS �� buf �����ʵ�ʳ��Ƚ���� 13 + 13 + 1 = 27 �ֽڣ������һ�ֽ����ڱ�����ַ�����

����� SDS �����޸�֮�� SDS �ĳ��Ƚ����ڵ��� 1 MB �� ��ô�������� 1 MB ��δʹ�ÿռ䡣 �ٸ����ӣ� ��������޸�֮�� SDS 
�� len ����� 30 MB �� ��ô�������� 1 MB ��δʹ�ÿռ䣬 SDS �� buf �����ʵ�ʳ��Ƚ�Ϊ 30 MB + 1 MB + 1 byte ��

ͨ���ռ�Ԥ������ԣ� Redis ���Լ�������ִ���ַ�����������������ڴ��ط�������� 
ͨ������Ԥ������ԣ� SDS ���������� N ���ַ���������ڴ��ط�������ӱض� N �ν���Ϊ��� N �Ρ� ��sdscat
*/

sds sdscat(sds s, const char *t) {
    return sdscatlen(s, t, strlen(t));
}

/*
 * ����һ�� sds ׷�ӵ�һ�� sds ��ĩβ
 * 
 * ����ֵ
 *  sds ��׷�ӳɹ������� sds ��ʧ�ܷ��� NULL
 *
 * ���Ӷ�
 *  T = O(N)
 */
/* Append the specified sds 't' to the existing sds 's'.
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */
sds sdscatsds(sds s, const sds t) {
    return sdscatlen(s, t, sdslen(t));
}

/*
 * ���ַ��� t ��ǰ len ���ַ����Ƶ� sds s ���У�
 * �����ַ������������ս����
 *
 * ��� sds �ĳ������� len ���ַ�����ô��չ sds
 *
 * ���Ӷ�
 *  T = O(N)
 *
 * ����ֵ
 *  sds �����Ƴɹ������µ� sds �����򷵻� NULL
 */
/* Destructively modify the sds string 's' to hold the specified binary
 * safe string pointed by 't' of length 'len' bytes. */
 // �������� C �ַ������Ƶ� SDS ���棬 ���� SDS ԭ�е��ַ�����              O(N) �� N Ϊ������ C �ַ����ĳ��ȡ� 
sds sdscpylen(sds s, const char *t, size_t len) {

    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));

    // sds ���� buf �ĳ���
    size_t totlen = sh->free+sh->len;

    // ��� s �� buf ���Ȳ����� len ����ô��չ��
    if (totlen < len) {
        // T = O(N)
        s = sdsMakeRoomFor(s,len-sh->len);
        if (s == NULL) return NULL;
        sh = (void*) (s-(sizeof(struct sdshdr)));
        totlen = sh->free+sh->len;
    }

    // ��������
    // T = O(N)
    memcpy(s, t, len);

    // ����ս����
    s[len] = '\0';

    // ��������
    sh->len = len;
    sh->free = totlen-len;

    // �����µ� sds
    return s;
}

/*
 * ���ַ������Ƶ� sds ���У�
 * ����ԭ�е��ַ���
 *
 * ��� sds �ĳ��������ַ����ĳ��ȣ���ô��չ sds ��
 *
 * ���Ӷ�
 *  T = O(N)
 *
 * ����ֵ
 *  sds �����Ƴɹ������µ� sds �����򷵻� NULL
 */
/* Like sdscpylen() but 't' must be a null-termined string so that the length
 * of the string is obtained with strlen(). */
 // �������� C �ַ������Ƶ� SDS ���棬 ���� SDS ԭ�е��ַ�����              O(N) �� N Ϊ������ C �ַ����ĳ��ȡ� 
sds sdscpy(sds s, const char *t) {
    return sdscpylen(s, t, strlen(t));
}

/* Helper for sdscatlonglong() doing the actual number -> string
 * conversion. 's' must point to a string with room for at least
 * SDS_LLSTR_SIZE bytes.
 *
 * The function returns the lenght of the null-terminated string
 * representation stored at 's'. */
#define SDS_LLSTR_SIZE 21
int sdsll2str(char *s, long long value) {
    char *p, aux;
    unsigned long long v;
    size_t l;

    /* Generate the string representation, this method produces
     * an reversed string. */
    v = (value < 0) ? -value : value;
    p = s;
    do {
        *p++ = '0'+(v%10);
        v /= 10;
    } while(v);
    if (value < 0) *p++ = '-';

    /* Compute length and add null term. */
    l = p-s;
    *p = '\0';

    /* Reverse the string. */
    p--;
    while(s < p) {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    return l;
}

/* Identical sdsll2str(), but for unsigned long long type. */
int sdsull2str(char *s, unsigned long long v) {
    char *p, aux;
    size_t l;

    /* Generate the string representation, this method produces
     * an reversed string. */
    p = s;
    do {
        *p++ = '0'+(v%10);
        v /= 10;
    } while(v);

    /* Compute length and add null term. */
    l = p-s;
    *p = '\0';

    /* Reverse the string. */
    p--;
    while(s < p) {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    return l;
}

/* Create an sds string from a long long value. It is much faster than:
 *
 * sdscatprintf(sdsempty(),"%lld\n", value);
 */
// ��������� long long ֵ value ������һ�� SDS
sds sdsfromlonglong(long long value) {
    char buf[SDS_LLSTR_SIZE];
    int len = sdsll2str(buf,value);

    return sdsnewlen(buf,len);
}

/* 
 * ��ӡ�������� sdscatprintf ������
 *
 * T = O(N^2)
 */
/* Like sdscatpritf() but gets va_list instead of being variadic. */
sds sdscatvprintf(sds s, const char *fmt, va_list ap) {
    va_list cpy;
    char staticbuf[1024], *buf = staticbuf, *t;
    size_t buflen = strlen(fmt)*2;

    /* We try to start using a static buffer for speed.
     * If not possible we revert to heap allocation. */
    if (buflen > sizeof(staticbuf)) {
        buf = zmalloc(buflen);
        if (buf == NULL) return NULL;
    } else {
        buflen = sizeof(staticbuf);
    }

    /* Try with buffers two times bigger every time we fail to
     * fit the string in the current buffer size. */
    while(1) {
        buf[buflen-2] = '\0';
        va_copy(cpy,ap);
        // T = O(N)
        vsnprintf(buf, buflen, fmt, cpy);
        if (buf[buflen-2] != '\0') {
            if (buf != staticbuf) zfree(buf);
            buflen *= 2;
            buf = zmalloc(buflen);
            if (buf == NULL) return NULL;
            continue;
        }
        break;
    }

    /* Finally concat the obtained string to the SDS string and return it. */
    t = sdscat(s, buf);
    if (buf != staticbuf) zfree(buf);
    return t;
}

/*
 * ��ӡ�����������ַ�����������Щ�ַ���׷�ӵ����� sds ��ĩβ
 *
 * T = O(N^2)
 */
/* Append to the sds string 's' a string obtained using printf-alike format
 * specifier.
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call.
 *
 * Example:
 *
 * s = sdsempty("Sum is: ");
 * s = sdscatprintf(s,"%d+%d = %d",a,b,a+b).
 *
 * Often you need to create a string from scratch with the printf-alike
 * format. When this is the need, just use sdsempty() as the target string:
 *
 * s = sdscatprintf(sdsempty(), "... your format ...", args);
 */
sds sdscatprintf(sds s, const char *fmt, ...) {
    va_list ap;
    char *t;
    va_start(ap, fmt);
    // T = O(N^2)
    t = sdscatvprintf(s,fmt,ap);
    va_end(ap);
    return t;
}

/* This function is similar to sdscatprintf, but much faster as it does
 * not rely on sprintf() family functions implemented by the libc that
 * are often very slow. Moreover directly handling the sds string as
 * new data is concatenated provides a performance improvement.
 *
 * However this function only handles an incompatible subset of printf-alike
 * format specifiers:
 *
 * %s - C String
 * %S - SDS string
 * %i - signed int
 * %I - 64 bit signed integer (long long, int64_t)
 * %u - unsigned int
 * %U - 64 bit unsigned integer (unsigned long long, uint64_t)
 * %% - Verbatim "%" character.
 */
sds sdscatfmt(sds s, char const *fmt, ...) {
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    size_t initlen = sdslen(s);
    const char *f = fmt;
    int i;
    va_list ap;

    va_start(ap,fmt);
    f = fmt;    /* Next format specifier byte to process. */
    i = initlen; /* Position of the next byte to write to dest str. */
    while(*f) {
        char next, *str;
        size_t l;
        long long num;
        unsigned long long unum;

        /* Make sure there is always space for at least 1 char. */
        if (sh->free == 0) {
            s = sdsMakeRoomFor(s,1);
            sh = (void*) (s-(sizeof(struct sdshdr)));
        }

        switch(*f) {
        case '%':
            next = *(f+1);
            f++;
            switch(next) {
            case 's':
            case 'S':
                str = va_arg(ap,char*);
                l = (next == 's') ? strlen(str) : sdslen(str);
                if (sh->free < l) {
                    s = sdsMakeRoomFor(s,l);
                    sh = (void*) (s-(sizeof(struct sdshdr)));
                }
                memcpy(s+i,str,l);
                sh->len += l;
                sh->free -= l;
                i += l;
                break;
            case 'i':
            case 'I':
                if (next == 'i')
                    num = va_arg(ap,int);
                else
                    num = va_arg(ap,long long);
                {
                    char buf[SDS_LLSTR_SIZE];
                    l = sdsll2str(buf,num);
                    if (sh->free < l) {
                        s = sdsMakeRoomFor(s,l);
                        sh = (void*) (s-(sizeof(struct sdshdr)));
                    }
                    memcpy(s+i,buf,l);
                    sh->len += l;
                    sh->free -= l;
                    i += l;
                }
                break;
            case 'u':
            case 'U':
                if (next == 'u')
                    unum = va_arg(ap,unsigned int);
                else
                    unum = va_arg(ap,unsigned long long);
                {
                    char buf[SDS_LLSTR_SIZE];
                    l = sdsull2str(buf,unum);
                    if (sh->free < l) {
                        s = sdsMakeRoomFor(s,l);
                        sh = (void*) (s-(sizeof(struct sdshdr)));
                    }
                    memcpy(s+i,buf,l);
                    sh->len += l;
                    sh->free -= l;
                    i += l;
                }
                break;
            default: /* Handle %% and generally %<unknown>. */
                s[i++] = next;
                sh->len += 1;
                sh->free -= 1;
                break;
            }
            break;
        default:
            s[i++] = *f;
            sh->len += 1;
            sh->free -= 1;
            break;
        }
        f++;
    }
    va_end(ap);

    /* Add null-term */
    s[i] = '\0';
    return s;
}

/*
 * �� sds �������˽����޼���������� cset ָ���������ַ�
 *
 * ���� sdsstrim(xxyyabcyyxy, "xy") ������ "abc"
 *
 * �����ԣ�
 *  T = O(M*N)��M Ϊ SDS ���ȣ� N Ϊ cset ���ȡ�
 */
/* Remove the part of the string from left and from right composed just of
 * contiguous characters found in 'cset', that is a null terminted C string.
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call.
 *
 * Example:
 *
 * s = sdsnew("AA...AA.a.aa.aHelloWorld     :::");
 * s = sdstrim(s,"A. :");
 * printf("%s\n", s);
 *
 * Output will be just "Hello World".
 */

/*
���Կռ��ͷ�

���Կռ��ͷ������Ż� SDS ���ַ������̲����� �� SDS �� API ��Ҫ���� SDS ������ַ���ʱ�� ���򲢲�����ʹ���ڴ��ط�����������
�̺��������ֽڣ� ����ʹ�� free ���Խ���Щ�ֽڵ�������¼������ ���ȴ�����ʹ�á�

�ٸ����ӣ� sdstrim ��������һ�� SDS ��һ�� C �ַ�����Ϊ������ �� SDS �������˷ֱ��Ƴ������� C �ַ����г��ֹ����ַ���
*/
//���ͷŵ��ַ����ֽ�����ӵ�free�У�ƾ��free��len�Ϳ�����Ч����ռ�

//����һ�� SDS ��һ�� C �ַ�����Ϊ������ �� SDS �������˷ֱ��Ƴ������� C �ַ����г��ֹ����ַ���

sds sdstrim(sds s, const char *cset) {
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    char *start, *end, *sp, *ep;
    size_t len;

    // ���úͼ�¼ָ��
    sp = start = s;
    ep = end = s+sdslen(s)-1;

    // �޼�, T = O(N^2)
    while(sp <= end && strchr(cset, *sp)) sp++;
    while(ep > start && strchr(cset, *ep)) ep--;

    // ���� trim ���֮��ʣ����ַ�������
    len = (sp > ep) ? 0 : ((ep-sp)+1);
    
    // �������Ҫ��ǰ���ַ�������
    // T = O(N)
    if (sh->buf != sp) memmove(sh->buf, sp, len);

    // ����ս��
    sh->buf[len] = '\0';

    // ��������
    sh->free = sh->free+(sh->len-len);
    sh->len = len;

    // �����޼���� sds
    return s;
}

/*
 * �������Խ�ȡ sds �ַ���������һ��
 * start �� end ���Ǳ����䣨�������ڣ�
 *
 * ������ 0 ��ʼ�����Ϊ sdslen(s) - 1
 * ���������Ǹ����� sdslen(s) - 1 == -1
 *
 * ���Ӷ�
 *  T = O(N)
 */
/* Turn the string into a smaller (or equal) string containing only the
 * substring specified by the 'start' and 'end' indexes.
 *
 * start and end can be negative, where -1 means the last character of the
 * string, -2 the penultimate character, and so forth.
 *
 * The interval is inclusive, so the start and end characters will be part
 * of the resulting string.
 *
 * The string is modified in-place.
 *
 * Example:
 *
 * s = sdsnew("Hello World");
 * sdsrange(s,1,-1); => "ello World"
 */ // ���� SDS ���������ڵ����ݣ� ���������ڵ����ݻᱻ���ǻ������            O(N) �� N Ϊ���������ݵ��ֽ����� 
void sdsrange(sds s, int start, int end) {//��buf��δ���������ݿ������ڴ�ͷ�������´μ������յ����ݺ��������һ��������Ƿ���������key����value�ַ���
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    size_t newlen, len = sdslen(s);

    if (len == 0) 
        return;
        
    if (start < 0) { //�������start=-1�����end��ǰstart�ֽڿ�ʼ
        start = len+start;
        if (start < 0) 
            start = 0;
    }
    
    if (end < 0) { //end=-1��ʾ��β��Ϊ��ʱ�ڶ��ֽڴ�
        end = len+end;
        if (end < 0) 
            end = 0;
    }
    newlen = (start > end) ? 0 : (end-start)+1;
    if (newlen != 0) {
        if (start >= (signed)len) {
            newlen = 0;
        } else if (end >= (signed)len) { //���end����len������endֻ��Ϊlenĩβ��
            end = len-1;
            newlen = (start > end) ? 0 : (end-start)+1;
        }
    } else {
        start = 0;
    }

    // �������Ҫ�����ַ��������ƶ�
    // T = O(N)
    if (start && newlen) 
        memmove(sh->buf, sh->buf+start, newlen);

    // ����ս��
    sh->buf[newlen] = 0;

    // ��������
    sh->free = sh->free+(sh->len-newlen);
    sh->len = newlen;
}

/*
 * �� sds �ַ����е������ַ�ת��ΪСд
 *
 * T = O(N)
 */
/* Apply tolower() to every character of the sds string 's'. */
void sdstolower(sds s) {
    int len = sdslen(s), j;

    for (j = 0; j < len; j++) s[j] = tolower(s[j]);
}

/*
 * �� sds �ַ����е������ַ�ת��Ϊ��д
 *
 * T = O(N)
 */
/* Apply toupper() to every character of the sds string 's'. */
void sdstoupper(sds s) {
    int len = sdslen(s), j;

    for (j = 0; j < len; j++) s[j] = toupper(s[j]);
}

/*
 * �Ա����� sds �� strcmp �� sds �汾
 *
 * ����ֵ
 *  int ����ȷ��� 0 ��s1 �ϴ󷵻������� s2 �ϴ󷵻ظ���
 *
 * T = O(N)
 */
/* Compare two sds strings s1 and s2 with memcmp().
 *
 * Return value:
 *
 *     1 if s1 > s2.
 *    -1 if s1 < s2.
 *     0 if s1 and s2 are exactly the same binary string.
 *
 * If two strings share exactly the same prefix, but one of the two has
 * additional characters, the longer string is considered to be greater than
 * the smaller one. */  //�Ա����� SDS �ַ����Ƿ���ͬ��   
int sdscmp(const sds s1, const sds s2) {
    size_t l1, l2, minlen;
    int cmp;

    l1 = sdslen(s1);
    l2 = sdslen(s2);
    minlen = (l1 < l2) ? l1 : l2;
    cmp = memcmp(s1,s2,minlen);

    if (cmp == 0) return l1-l2;

    return cmp;
}

/* Split 's' with separator in 'sep'. An array
 * of sds strings is returned. *count will be set
 * by reference to the number of tokens returned.
 *
 * ʹ�÷ָ��� sep �� s ���зָ����һ�� sds �ַ��������顣
 * *count �ᱻ����Ϊ��������Ԫ�ص�������
 *
 * On out of memory, zero length string, zero length
 * separator, NULL is returned.
 *
 * ��������ڴ治�㡢�ַ�������Ϊ 0 ��ָ�������Ϊ 0
 * ����������� NULL
 *
 * Note that 'sep' is able to split a string using
 * a multi-character separator. For example
 * sdssplit("foo_-_bar","_-_"); will return two
 * elements "foo" and "bar".
 *
 * ע��ָ������Ե��ǰ�������ַ����ַ���
 *
 * This version of the function is binary-safe but
 * requires length arguments. sdssplit() is just the
 * same function but for zero-terminated strings.
 *
 * ����������� len ������������Ƕ����ư�ȫ�ġ�
 * ���ĵ����ᵽ�� sdssplit() �ѷ�����
 *
 * T = O(N^2)
 */
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count) {
    int elements = 0, slots = 5, start = 0, j;
    sds *tokens;

    if (seplen < 1 || len < 0) return NULL;

    tokens = zmalloc(sizeof(sds)*slots);
    if (tokens == NULL) return NULL;

    if (len == 0) {
        *count = 0;
        return tokens;
    }
    
    // T = O(N^2)
    for (j = 0; j < (len-(seplen-1)); j++) {
        /* make sure there is room for the next element and the final one */
        if (slots < elements+2) {
            sds *newtokens;

            slots *= 2;
            newtokens = zrealloc(tokens,sizeof(sds)*slots);
            if (newtokens == NULL) goto cleanup;
            tokens = newtokens;
        }
        /* search the separator */
        // T = O(N)
        if ((seplen == 1 && *(s+j) == sep[0]) || (memcmp(s+j,sep,seplen) == 0)) {
            tokens[elements] = sdsnewlen(s+start,j-start);
            if (tokens[elements] == NULL) goto cleanup;
            elements++;
            start = j+seplen;
            j = j+seplen-1; /* skip the separator */
        }
    }
    /* Add the final element. We are sure there is room in the tokens array. */
    tokens[elements] = sdsnewlen(s+start,len-start);
    if (tokens[elements] == NULL) goto cleanup;
    elements++;
    *count = elements;
    return tokens;

cleanup:
    {
        int i;
        for (i = 0; i < elements; i++) sdsfree(tokens[i]);
        zfree(tokens);
        *count = 0;
        return NULL;
    }
}

/*
 * �ͷ� tokens ������ count �� sds
 *
 * T = O(N^2)
 */
/* Free the result returned by sdssplitlen(), or do nothing if 'tokens' is NULL. */
void sdsfreesplitres(sds *tokens, int count) {
    if (!tokens) return;
    while(count--)
        sdsfree(tokens[count]);
    zfree(tokens);
}

/*
 * ������Ϊ len ���ַ��� p �Դ����ţ�quoted���ĸ�ʽ
 * ׷�ӵ����� sds ��ĩβ
 *
 * T = O(N)
 */
/* Append to the sds string "s" an escaped string representation where
 * all the non-printable characters (tested with isprint()) are turned into
 * escapes in the form "\n\r\a...." or "\x<hex-number>".
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */
sds sdscatrepr(sds s, const char *p, size_t len) {

    s = sdscatlen(s,"\"",1);

    while(len--) {
        switch(*p) {
        case '\\':
        case '"':
            s = sdscatprintf(s,"\\%c",*p);
            break;
        case '\n': s = sdscatlen(s,"\\n",2); break;
        case '\r': s = sdscatlen(s,"\\r",2); break;
        case '\t': s = sdscatlen(s,"\\t",2); break;
        case '\a': s = sdscatlen(s,"\\a",2); break;
        case '\b': s = sdscatlen(s,"\\b",2); break;
        default:
            if (isprint(*p))
                s = sdscatprintf(s,"%c",*p);
            else
                s = sdscatprintf(s,"\\x%02x",(unsigned char)*p);
            break;
        }
        p++;
    }

    return sdscatlen(s,"\"",1);
}

/* Helper function for sdssplitargs() that returns non zero if 'c'
 * is a valid hex digit. */
/*
 * ��� c Ϊʮ�����Ʒ��ŵ�����һ������������
 *
 * T = O(1)
 */
int is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

/* Helper function for sdssplitargs() that converts a hex digit into an
 * integer from 0 to 15 */
/*
 * ��ʮ�����Ʒ���ת��Ϊ 10 ����
 *
 * T = O(1)
 */
int hex_digit_to_int(char c) {
    switch(c) {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a': case 'A': return 10;
    case 'b': case 'B': return 11;
    case 'c': case 'C': return 12;
    case 'd': case 'D': return 13;
    case 'e': case 'E': return 14;
    case 'f': case 'F': return 15;
    default: return 0;
    }
}

/* Split a line into arguments, where every argument can be in the
 * following programming-language REPL-alike form:
 *
 * ��һ���ı��ָ�ɶ��������ÿ���������������µ��������� REPL ��ʽ��
 *
 * foo bar "newline are supported\n" and "\xff\x00otherstuff"
 *
 * The number of arguments is stored into *argc, and an array
 * of sds is returned.
 *
 * �����ĸ����ᱣ���� *argc �У���������һ�� sds ���顣
 *
 * The caller should free the resulting array of sds strings with
 * sdsfreesplitres().
 *
 * ������Ӧ��ʹ�� sdsfreesplitres() ���ͷź������ص� sds ���顣
 *
 * Note that sdscatrepr() is able to convert back a string into
 * a quoted string in the same format sdssplitargs() is able to parse.
 *
 * sdscatrepr() ���Խ�һ���ַ���ת��Ϊһ�������ţ�quoted�����ַ�����
 * ��������ŵ��ַ������Ա� sdssplitargs() ������
 *
 * The function returns the allocated tokens on success, even when the
 * input string is empty, or NULL if the input contains unbalanced
 * quotes or closed quotes followed by non space characters
 * as in: "foo"bar or "foo'
 *
 * ��ʹ������ֿ��ַ����� NULL �������������δ��Ӧ�����ţ�
 * �������Ὣ�ѳɹ�������ַ����ȷ��ء�
 *
 * ���������Ҫ���� config.c �ж������ļ����з�����
 * ���ӣ�
 *  sds *arr = sdssplitargs("timeout 10086\r\nport 123321\r\n");
 * ��ó�
 *  arr[0] = "timeout"
 *  arr[1] = "10086"
 *  arr[2] = "port"
 *  arr[3] = "123321"
 *
 * T = O(N^2)
 */
sds *sdssplitargs(const char *line, int *argc) {
    const char *p = line;
    char *current = NULL;
    char **vector = NULL;

    *argc = 0;
    while(1) {

        /* skip blanks */
        // �����հ�
        // T = O(N)
        while(*p && isspace(*p)) p++;

        if (*p) {
            /* get a token */
            int inq=0;  /* set to 1 if we are in "quotes" */
            int insq=0; /* set to 1 if we are in 'single quotes' */
            int done=0;

            if (current == NULL) current = sdsempty();

            // T = O(N)
            while(!done) {
                if (inq) {
                    if (*p == '\\' && *(p+1) == 'x' &&
                                             is_hex_digit(*(p+2)) &&
                                             is_hex_digit(*(p+3)))
                    {
                        unsigned char byte;

                        byte = (hex_digit_to_int(*(p+2))*16)+
                                hex_digit_to_int(*(p+3));
                        current = sdscatlen(current,(char*)&byte,1);
                        p += 3;
                    } else if (*p == '\\' && *(p+1)) {
                        char c;

                        p++;
                        switch(*p) {
                        case 'n': c = '\n'; break;
                        case 'r': c = '\r'; break;
                        case 't': c = '\t'; break;
                        case 'b': c = '\b'; break;
                        case 'a': c = '\a'; break;
                        default: c = *p; break;
                        }
                        current = sdscatlen(current,&c,1);
                    } else if (*p == '"') {
                        /* closing quote must be followed by a space or
                         * nothing at all. */
                        if (*(p+1) && !isspace(*(p+1))) goto err;
                        done=1;
                    } else if (!*p) {
                        /* unterminated quotes */
                        goto err;
                    } else {
                        current = sdscatlen(current,p,1);
                    }
                } else if (insq) {
                    if (*p == '\\' && *(p+1) == '\'') {
                        p++;
                        current = sdscatlen(current,"'",1);
                    } else if (*p == '\'') {
                        /* closing quote must be followed by a space or
                         * nothing at all. */
                        if (*(p+1) && !isspace(*(p+1))) goto err;
                        done=1;
                    } else if (!*p) {
                        /* unterminated quotes */
                        goto err;
                    } else {
                        current = sdscatlen(current,p,1);
                    }
                } else {
                    switch(*p) {
                    case ' ':
                    case '\n':
                    case '\r':
                    case '\t':
                    case '\0':
                        done=1;
                        break;
                    case '"':
                        inq=1;
                        break;
                    case '\'':
                        insq=1;
                        break;
                    default:
                        current = sdscatlen(current,p,1);
                        break;
                    }
                }
                if (*p) p++;
            }
            /* add the token to the vector */
            // T = O(N)
            vector = zrealloc(vector,((*argc)+1)*sizeof(char*));
            vector[*argc] = current;
            (*argc)++;
            current = NULL;
        } else {
            /* Even on empty input string return something not NULL. */
            if (vector == NULL) vector = zmalloc(sizeof(void*));
            return vector;
        }
    }

err:
    while((*argc)--)
        sdsfree(vector[*argc]);
    zfree(vector);
    if (current) sdsfree(current);
    *argc = 0;
    return NULL;
}

/* Modify the string substituting all the occurrences of the set of
 * characters specified in the 'from' string to the corresponding character
 * in the 'to' array.
 *
 * ���ַ��� s �У�
 * ������ from �г��ֵ��ַ����滻�� to �е��ַ�
 *
 * For instance: sdsmapchars(mystring, "ho", "01", 2)
 * will have the effect of turning the string "hello" into "0ell1".
 *
 * ������� sdsmapchars(mystring, "ho", "01", 2)
 * �ͻὫ "hello" ת��Ϊ "0ell1"
 *
 * The function returns the sds string pointer, that is always the same
 * as the input pointer since no resize is needed. 
 * ��Ϊ����� sds ���д�С������
 * ���Է��ص� sds ����� sds һ��
 *
 * T = O(N^2)
 */
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen) {
    size_t j, i, l = sdslen(s);

    // ���������ַ���
    for (j = 0; j < l; j++) {
        // ����ӳ��
        for (i = 0; i < setlen; i++) {
            // �滻�ַ���
            if (s[j] == from[i]) {
                s[j] = to[i];
                break;
            }
        }
    }
    return s;
}

/* Join an array of C strings using the specified separator (also a C string).
 * Returns the result as an sds string. */
sds sdsjoin(char **argv, int argc, char *sep) {
    sds join = sdsempty();
    int j;

    for (j = 0; j < argc; j++) {
        join = sdscat(join, argv[j]);
        if (j != argc-1) join = sdscat(join,sep);
    }
    return join;
}

#ifdef SDS_TEST_MAIN
#include <stdio.h>
#include "testhelp.h"
#include "limits.h"

int main(void) {
    {
        struct sdshdr *sh;
        sds x = sdsnew("foo"), y;

        test_cond("Create a string and obtain the length",
            sdslen(x) == 3 && memcmp(x,"foo\0",4) == 0)

        sdsfree(x);
        x = sdsnewlen("foo",2);
        test_cond("Create a string with specified length",
            sdslen(x) == 2 && memcmp(x,"fo\0",3) == 0)

        x = sdscat(x,"bar");
        test_cond("Strings concatenation",
            sdslen(x) == 5 && memcmp(x,"fobar\0",6) == 0);

        x = sdscpy(x,"a");
        test_cond("sdscpy() against an originally longer string",
            sdslen(x) == 1 && memcmp(x,"a\0",2) == 0)

        x = sdscpy(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk");
        test_cond("sdscpy() against an originally shorter string",
            sdslen(x) == 33 &&
            memcmp(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk\0",33) == 0)

        sdsfree(x);
        x = sdscatprintf(sdsempty(),"%d",123);
        test_cond("sdscatprintf() seems working in the base case",
            sdslen(x) == 3 && memcmp(x,"123\0",4) == 0)

        sdsfree(x);
        x = sdsnew("--");
        x = sdscatfmt(x, "Hello %s World %I,%I--", "Hi!", LLONG_MIN,LLONG_MAX);
        test_cond("sdscatfmt() seems working in the base case",
            sdslen(x) == 60 &&
            memcmp(x,"--Hello Hi! World -9223372036854775808,"
                     "9223372036854775807--",60) == 0)

        sdsfree(x);
        x = sdsnew("--");
        x = sdscatfmt(x, "%u,%U--", UINT_MAX, ULLONG_MAX);
        test_cond("sdscatfmt() seems working with unsigned numbers",
            sdslen(x) == 35 &&
            memcmp(x,"--4294967295,18446744073709551615--",35) == 0)

        sdsfree(x);
        x = sdsnew("xxciaoyyy");
        sdstrim(x,"xy");
        test_cond("sdstrim() correctly trims characters",
            sdslen(x) == 4 && memcmp(x,"ciao\0",5) == 0)

        y = sdsdup(x);
        sdsrange(y,1,1);
        test_cond("sdsrange(...,1,1)",
            sdslen(y) == 1 && memcmp(y,"i\0",2) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,1,-1);
        test_cond("sdsrange(...,1,-1)",
            sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,-2,-1);
        test_cond("sdsrange(...,-2,-1)",
            sdslen(y) == 2 && memcmp(y,"ao\0",3) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,2,1);
        test_cond("sdsrange(...,2,1)",
            sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,1,100);
        test_cond("sdsrange(...,1,100)",
            sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

        sdsfree(y);
        y = sdsdup(x);
        sdsrange(y,100,100);
        test_cond("sdsrange(...,100,100)",
            sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

        sdsfree(y);
        sdsfree(x);
        x = sdsnew("foo");
        y = sdsnew("foa");
        test_cond("sdscmp(foo,foa)", sdscmp(x,y) > 0)

        sdsfree(y);
        sdsfree(x);
        x = sdsnew("bar");
        y = sdsnew("bar");
        test_cond("sdscmp(bar,bar)", sdscmp(x,y) == 0)

        sdsfree(y);
        sdsfree(x);
        x = sdsnew("aar");
        y = sdsnew("bar");
        test_cond("sdscmp(bar,bar)", sdscmp(x,y) < 0)

        sdsfree(y);
        sdsfree(x);
        x = sdsnewlen("\a\n\0foo\r",7);
        y = sdscatrepr(sdsempty(),x,sdslen(x));
        test_cond("sdscatrepr(...data...)",
            memcmp(y,"\"\\a\\n\\x00foo\\r\"",15) == 0)

        {
            int oldfree;

            sdsfree(x);
            x = sdsnew("0");
            sh = (void*) (x-(sizeof(struct sdshdr)));
            test_cond("sdsnew() free/len buffers", sh->len == 1 && sh->free == 0);
            x = sdsMakeRoomFor(x,1);
            sh = (void*) (x-(sizeof(struct sdshdr)));
            test_cond("sdsMakeRoomFor()", sh->len == 1 && sh->free > 0);
            oldfree = sh->free;
            x[1] = '1';
            sdsIncrLen(x,1);
            test_cond("sdsIncrLen() -- content", x[0] == '0' && x[1] == '1');
            test_cond("sdsIncrLen() -- len", sh->len == 2);
            test_cond("sdsIncrLen() -- free", sh->free == oldfree-1);
        }
    }
    test_report()
    return 0;
}
#endif
