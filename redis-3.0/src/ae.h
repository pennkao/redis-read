/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
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

#ifndef __AE_H__
#define __AE_H__

/*
 * �¼�ִ��״̬
 */
// �ɹ�
#define AE_OK 0
// ����
#define AE_ERR -1

/*
 * �ļ��¼�״̬
 */
// δ����
#define AE_NONE 0
// �ɶ�
#define AE_READABLE 1
// ��д
#define AE_WRITABLE 2

/*
 * ʱ�䴦������ִ�� flags
 */
// �ļ��¼�
#define AE_FILE_EVENTS 1
// ʱ���¼�
#define AE_TIME_EVENTS 2
// �����¼�
#define AE_ALL_EVENTS (AE_FILE_EVENTS|AE_TIME_EVENTS)
// ��������Ҳ�����еȴ�
#define AE_DONT_WAIT 4

/*
 ����¼�����������AE_NOMORE����ô����¼�Ϊ��ʱ�¼������¼��ڴﵽһ��֮��ͻᱻɾ����֮���ٵ��
 ����¼�����������һ����AE NOMORE������ֵ����ô����¼�Ϊ������ʱ�䣺��һ��ʱ���¼�����֮�󣬷�����������¼����������ص�ֵ��
 ��ʱ���¼���when���Խ��и��£�������¼���һ��ʱ��֮���ٴε���������ַ�ʽһֱ���²�������ȥ��
 ����˵�����һ��ʱ���¼��ĸ�������������ֵ30����ô������Ӧ�ö����ʱ���¼����и��£�������¼���30����֮���ٴε��


 * ����ʱ���¼��Ƿ�Ҫ����ִ�е� flag
 */
#define AE_NOMORE -1

/* Macros */
#define AE_NOTUSED(V) ((void) V)

/*
 * �¼�������״̬
 */
struct aeEventLoop;

/* Types and data structures 
 *
 * �¼��ӿ�
 */
typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);
typedef int aeTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData);
typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop, void *clientData);
typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);

/* File event structure
 *
 * �ļ��¼��ṹ
 */
typedef struct aeFileEvent {

    // �����¼��������룬
    // ֵ������ AE_READABLE �� AE_WRITABLE ��
    // ���� AE_READABLE | AE_WRITABLE
    int mask; /* one of AE_(READABLE|WRITABLE) */

    // ���¼�������
    aeFileProc *rfileProc;//aeProcessEvents��ִ��

    // д�¼�������
    aeFileProc *wfileProc; //aeProcessEvents��ִ��

    // ��·���ÿ��˽������
    void *clientData;

} aeFileEvent;

/* Time event structure
 *
 * ʱ���¼��ṹ
 */
typedef struct aeTimeEvent {

    // ʱ���¼���Ψһ��ʶ��
    long long id; /* time event identifier. */

    // �¼��ĵ���ʱ��
    long when_sec; /* seconds */
    long when_ms; /* milliseconds */

    // �¼�������  processTimeEvents
    aeTimeProc *timeProc; //����ִ�иú������Ƿ񷵻�AE_NOMORE�������Ƿ���Ҫ�ٴ���Ӹö�ʱ����Ҳ��������ִ�У���processTimeEvents processTimeEvents

    // �¼��ͷź���
    aeEventFinalizerProc *finalizerProc;

    // ��·���ÿ��˽������
    void *clientData;

    // ָ���¸�ʱ���¼��ṹ���γ�����
    struct aeTimeEvent *next;

} aeTimeEvent;

/* A fired event
 *
 * �Ѿ����¼�
 */
typedef struct aeFiredEvent {

    // �Ѿ����ļ�������
    int fd;

    // �¼��������룬
    // ֵ������ AE_READABLE �� AE_WRITABLE
    // ���������ߵĻ�
    int mask;

} aeFiredEvent;

/* State of an event based program 
 *
 * �¼���������״̬
 */
typedef struct aeEventLoop {

    // Ŀǰ��ע������������
    int maxfd;   /* highest file descriptor currently registered */

    // Ŀǰ��׷�ٵ����������
    int setsize; /* max number of file descriptors tracked */

    // ��������ʱ���¼� id
    long long timeEventNextId;

    // ���һ��ִ��ʱ���¼���ʱ��
    time_t lastTime;     /* Used to detect system clock skew */

    // ��ע����ļ��¼�
    aeFileEvent *events; /* Registered events */

    // �Ѿ������ļ��¼�
    aeFiredEvent *fired; /* Fired events */

    // ʱ���¼�
    aeTimeEvent *timeEventHead;

    // �¼��������Ŀ���
    int stop;

    // ��·���ÿ��˽������
    void *apidata; /* This is used for polling API specific data */

    // �ڴ����¼�ǰҪִ�еĺ���
    aeBeforeSleepProc *beforesleep; //��ֵΪbeforeSleep���ں���aeMain��ִ��

} aeEventLoop;

/* Prototypes */
aeEventLoop *aeCreateEventLoop(int setsize);
void aeDeleteEventLoop(aeEventLoop *eventLoop);
void aeStop(aeEventLoop *eventLoop);
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData);
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask);
int aeGetFileEvents(aeEventLoop *eventLoop, int fd);
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc);
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id);
int aeProcessEvents(aeEventLoop *eventLoop, int flags);
int aeWait(int fd, int mask, long long milliseconds);
void aeMain(aeEventLoop *eventLoop);
char *aeGetApiName(void);
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep);
int aeGetSetSize(aeEventLoop *eventLoop);
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize);

#endif
