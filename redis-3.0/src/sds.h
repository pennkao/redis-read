/* SDSLib, A C dynamic strings library
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

#ifndef __SDS_H
#define __SDS_H

/*
 * ���Ԥ���䳤��
 */
#define SDS_MAX_PREALLOC (1024*1024)

#include <sys/types.h>
#include <stdarg.h>

/*
 * ���ͱ���������ָ�� sdshdr �� buf ����
 */
typedef char *sds;

/*
�����ư�ȫ

C �ַ����е��ַ��������ĳ�ֱ��루���� ASCII���� ���ҳ����ַ�����ĩβ֮�⣬ �ַ������治�ܰ������ַ��� �������ȱ��������Ŀ��ַ�
��������Ϊ���ַ�����β ���� ��Щ����ʹ�� C �ַ���ֻ�ܱ����ı����ݣ� �����ܱ�����ͼƬ����Ƶ����Ƶ��ѹ���ļ������Ķ��������ݡ�

�ٸ����ӣ� �����һ��ʹ�ÿ��ַ����ָ������ʵ��������ݸ�ʽ(Redis\0Cluster\0)�� ��ô���ָ�ʽ�Ͳ���ʹ�� C �ַ��������棬 ��ΪC 
�ַ������õĺ���ֻ��ʶ������е� "Redis" �� ������֮��� "Cluster" ��

��Ȼ���ݿ�һ�����ڱ����ı����ݣ� ��ʹ�����ݿ���������������ݵĳ���Ҳ���ټ��� ��ˣ� Ϊ��ȷ�� Redis ���������ڸ��ֲ�ͬ��ʹ�ó����� 
SDS �� API ���Ƕ����ư�ȫ�ģ�binary-safe���� ���� SDS API �����Դ�������Ƶķ�ʽ������ SDS ����� buf ����������ݣ� ���򲻻��
���е��������κ����ơ����ˡ����߼��� ���� ������д��ʱ��ʲô���ģ� ������ȡʱ����ʲô����

��Ҳ�����ǽ� SDS �� buf ���Գ�Ϊ�ֽ������ԭ�� ���� Redis ��������������������ַ��� ��������������һϵ�ж��������ݡ�
*/

/*
�� 2-1 C �ַ����� SDS ֮�������

C �ַ���                                                            SDS
��ȡ�ַ������ȵĸ��Ӷ�Ϊ O(N) ��                            ��ȡ�ַ������ȵĸ��Ӷ�Ϊ O(1) �� 
API �ǲ���ȫ�ģ����ܻ���ɻ����������                      API �ǰ�ȫ�ģ�������ɻ���������� 
�޸��ַ������� N �α�Ȼ��Ҫִ�� N ���ڴ��ط��䡣            �޸��ַ������� N �������Ҫִ�� N ���ڴ��ط��䡣 
ֻ�ܱ����ı����ݡ�                                          ���Ա����ı����߶��������ݡ� 
����ʹ������ <string.h> ���еĺ�����                        ����ʹ��һ���� <string.h> ���еĺ����� 
*/

/*
�� 2-2 SDS ����Ҫ���� API


����                                    ����                                                ʱ�临�Ӷ�


sdsnew           ����һ���������� C �ַ����� SDS ��                                     O(N) �� N Ϊ���� C �ַ����ĳ��ȡ� 
sdsempty        ����һ���������κ����ݵĿ� SDS ��                                       O(1) 
sdsfree         �ͷŸ����� SDS ��                                                       O(1) 
sdslen          ���� SDS ����ʹ�ÿռ��ֽ�����                                           ���ֵ����ͨ����ȡ SDS �� len ������ֱ�ӻ�ã� ���Ӷ�Ϊ O(1) �� 
sdsavail            ���� SDS ��δʹ�ÿռ��ֽ�����                                       ���ֵ����ͨ����ȡ SDS �� free ������ֱ�ӻ�ã� ���Ӷ�Ϊ O(1) �� 
sdsdup          ����һ������ SDS �ĸ�����copy����                                       O(N) �� N Ϊ���� SDS �ĳ��ȡ� 
sdsclear        ��� SDS ������ַ������ݡ�                                             ��Ϊ���Կռ��ͷŲ��ԣ����Ӷ�Ϊ O(1) �� 
sdscat          ������ C �ַ���ƴ�ӵ� SDS �ַ�����ĩβ��                                O(N) �� N Ϊ��ƴ�� C �ַ����ĳ��ȡ� 
sdscatsds       ������ SDS �ַ���ƴ�ӵ���һ�� SDS �ַ�����ĩβ��                        O(N) �� N Ϊ��ƴ�� SDS �ַ����ĳ��ȡ� 
sdscpy          �������� C �ַ������Ƶ� SDS ���棬 ���� SDS ԭ�е��ַ�����              O(N) �� N Ϊ������ C �ַ����ĳ��ȡ� 
sdsgrowzero         �ÿ��ַ��� SDS ��չ���������ȡ�                                     O(N) �� N Ϊ��չ�������ֽ����� 
sdsrange        ���� SDS ���������ڵ����ݣ� ���������ڵ����ݻᱻ���ǻ������            O(N) �� N Ϊ���������ݵ��ֽ����� 
sdstrim         ����һ�� SDS ��һ�� C �ַ�����Ϊ������ �� SDS �������˷ֱ��Ƴ�
                ������ C �ַ����г��ֹ����ַ���                                         O(M*N) �� M Ϊ SDS �ĳ��ȣ� N Ϊ���� C �ַ����ĳ��ȡ� 
sdscmp          �Ա����� SDS �ַ����Ƿ���ͬ��                                           O(N) �� N Ϊ���� SDS �н϶̵��Ǹ� SDS �ĳ��ȡ� 
*/

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
 * �����ַ�������Ľṹ
 */
struct sdshdr {//�ýṹһ�������̿��Բο�createStringObject
    
    // buf ����ռ�ÿռ�ĳ���
    int len;

    // buf ��ʣ����ÿռ�ĳ���
    int free;

    // ���ݿռ�
    char buf[]; //���Բο�sdsnewlen
};

/*
 * ���� sds ʵ�ʱ�����ַ����ĳ���
 *
 * T = O(1)
 */
static inline size_t sdslen(const sds s) {
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->len;
}

/*
 * ���� sds ���ÿռ�ĳ���
 *
 * T = O(1)
 */
static inline size_t sdsavail(const sds s) {
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->free;
}

sds sdsnewlen(const void *init, size_t initlen);
sds sdsnew(const char *init);
sds sdsempty(void);
size_t sdslen(const sds s);
sds sdsdup(const sds s);
void sdsfree(sds s);
size_t sdsavail(const sds s);
sds sdsgrowzero(sds s, size_t len);
sds sdscatlen(sds s, const void *t, size_t len);
sds sdscat(sds s, const char *t);
sds sdscatsds(sds s, const sds t);
sds sdscpylen(sds s, const char *t, size_t len);
sds sdscpy(sds s, const char *t);

sds sdscatvprintf(sds s, const char *fmt, va_list ap);
#ifdef __GNUC__
sds sdscatprintf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
sds sdscatprintf(sds s, const char *fmt, ...);
#endif

sds sdscatfmt(sds s, char const *fmt, ...);
sds sdstrim(sds s, const char *cset);
void sdsrange(sds s, int start, int end);
void sdsupdatelen(sds s);
void sdsclear(sds s);
int sdscmp(const sds s1, const sds s2);
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count);
void sdsfreesplitres(sds *tokens, int count);
void sdstolower(sds s);
void sdstoupper(sds s);
sds sdsfromlonglong(long long value);
sds sdscatrepr(sds s, const char *p, size_t len);
sds *sdssplitargs(const char *line, int *argc);
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);
sds sdsjoin(char **argv, int argc, char *sep);

/* Low level functions exposed to the user API */
sds sdsMakeRoomFor(sds s, size_t addlen);
void sdsIncrLen(sds s, int incr);
sds sdsRemoveFreeSpace(sds s);
size_t sdsAllocSize(sds s);

#endif
