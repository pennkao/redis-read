/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
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

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "ae.h"
#include "zmalloc.h"
#include "config.h"

/* Include the best multiplexing layer supported by this system.
 * The following should be ordered by performances, descending. */
#ifdef HAVE_EVPORT
#include "ae_evport.c"
#else
    #ifdef HAVE_EPOLL
    #include "ae_epoll.c"
    #else
        #ifdef HAVE_KQUEUE
        #include "ae_kqueue.c"
        #else
        #include "ae_select.c"
        #endif
    #endif
#endif

/*
 * ��ʼ���¼�������״̬
 */
aeEventLoop *aeCreateEventLoop(int setsize) {
    aeEventLoop *eventLoop;
    int i;

    // �����¼�״̬�ṹ
    if ((eventLoop = zmalloc(sizeof(*eventLoop))) == NULL) goto err;

    // ��ʼ���ļ��¼��ṹ���Ѿ����ļ��¼��ṹ����
    eventLoop->events = zmalloc(sizeof(aeFileEvent)*setsize);
    eventLoop->fired = zmalloc(sizeof(aeFiredEvent)*setsize);
    if (eventLoop->events == NULL || eventLoop->fired == NULL) goto err;
    // ���������С
    eventLoop->setsize = setsize;
    // ��ʼ��ִ�����һ��ִ��ʱ��
    eventLoop->lastTime = time(NULL);

    // ��ʼ��ʱ���¼��ṹ
    eventLoop->timeEventHead = NULL;
    eventLoop->timeEventNextId = 0;

    eventLoop->stop = 0;
    eventLoop->maxfd = -1;
    eventLoop->beforesleep = NULL;
    if (aeApiCreate(eventLoop) == -1) goto err;

    /* Events with mask == AE_NONE are not set. So let's initialize the
     * vector with it. */
    // ��ʼ�������¼�
    for (i = 0; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;

    // �����¼�ѭ��
    return eventLoop;

err:
    if (eventLoop) {
        zfree(eventLoop->events);
        zfree(eventLoop->fired);
        zfree(eventLoop);
    }
    return NULL;
}

/* Return the current set size. */
// ���ص�ǰ�¼��۴�С
int aeGetSetSize(aeEventLoop *eventLoop) {
    return eventLoop->setsize;
}

/* Resize the maximum set size of the event loop.
 *
 * �����¼��۵Ĵ�С
 *
 * If the requested set size is smaller than the current set size, but
 * there is already a file descriptor in use that is >= the requested
 * set size minus one, AE_ERR is returned and the operation is not
 * performed at all.
 *
 * ������Ե�����СΪ setsize �������� >= setsize ���ļ�����������
 * ��ô���� AE_ERR ���������κζ�����
 *
 * Otherwise AE_OK is returned and the operation is successful. 
 *
 * ����ִ�д�С���������������� AE_OK ��
 */
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize) {
    int i;

    if (setsize == eventLoop->setsize) return AE_OK;
    if (eventLoop->maxfd >= setsize) return AE_ERR;
    if (aeApiResize(eventLoop,setsize) == -1) return AE_ERR;

    eventLoop->events = zrealloc(eventLoop->events,sizeof(aeFileEvent)*setsize);
    eventLoop->fired = zrealloc(eventLoop->fired,sizeof(aeFiredEvent)*setsize);
    eventLoop->setsize = setsize;

    /* Make sure that if we created new slots, they are initialized with
     * an AE_NONE mask. */
    for (i = eventLoop->maxfd+1; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;
    return AE_OK;
}

/*
 * ɾ���¼�������
 */
void aeDeleteEventLoop(aeEventLoop *eventLoop) {
    aeApiFree(eventLoop);
    zfree(eventLoop->events);
    zfree(eventLoop->fired);
    zfree(eventLoop);
}

/*
 * ֹͣ�¼�������
 */
void aeStop(aeEventLoop *eventLoop) {
    eventLoop->stop = 1;
}

/*
 * ���� mask ������ֵ������ fd �ļ���״̬��
 * �� fd ����ʱ��ִ�� proc ����
 */ //�ļ��¼�aeCreateFileEvent   ʱ���¼�aeCreateTimeEvent  aeProcessEvents��ִ���ļ���ʱ���¼�
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask, //mask��Ӧ���Ƕ�д�¼�AE_READABLE  AE_WRITABLE��
        aeFileProc *proc, void *clientData)
{
    if (fd >= eventLoop->setsize) {
        errno = ERANGE;
        return AE_ERR;
    }

    if (fd >= eventLoop->setsize) return AE_ERR;

    // ȡ���ļ��¼��ṹ
    aeFileEvent *fe = &eventLoop->events[fd];

    // ����ָ�� fd ��ָ���¼�
    if (aeApiAddEvent(eventLoop, fd, mask) == -1)
        return AE_ERR;

    // �����ļ��¼����ͣ��Լ��¼��Ĵ�����
    fe->mask |= mask;
    if (mask & AE_READABLE) fe->rfileProc = proc;
    if (mask & AE_WRITABLE) fe->wfileProc = proc;

    // ˽������
    fe->clientData = clientData;

    // �������Ҫ�������¼������������ fd
    if (fd > eventLoop->maxfd)
        eventLoop->maxfd = fd;

    return AE_OK;
}

/*
 * �� fd �� mask ָ���ļ���������ɾ��
 */
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask)
{
    if (fd >= eventLoop->setsize) return;

    // ȡ���ļ��¼��ṹ
    aeFileEvent *fe = &eventLoop->events[fd];

    // δ���ü������¼����ͣ�ֱ�ӷ���
    if (fe->mask == AE_NONE) return;

    // ����������
    fe->mask = fe->mask & (~mask);
    if (fd == eventLoop->maxfd && fe->mask == AE_NONE) {
        /* Update the max fd */
        int j;

        for (j = eventLoop->maxfd-1; j >= 0; j--)
            if (eventLoop->events[j].mask != AE_NONE) break;
        eventLoop->maxfd = j;
    }

    // ȡ���Ը��� fd �ĸ����¼��ļ���
    aeApiDelEvent(eventLoop, fd, mask);
}

/*
 * ��ȡ���� fd ���ڼ������¼�����
 */
int aeGetFileEvents(aeEventLoop *eventLoop, int fd) {
    if (fd >= eventLoop->setsize) return 0;
    aeFileEvent *fe = &eventLoop->events[fd];

    return fe->mask;
}

/*
 * ȡ����ǰʱ�����ͺ��룬
 * ���ֱ����Ǳ��浽 seconds �� milliseconds ������
 */
static void aeGetTime(long *seconds, long *milliseconds)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec/1000;
}

/*
 * �ڵ�ǰʱ���ϼ��� milliseconds ���룬
 * ���ҽ�����֮��������ͺ������ֱ𱣴��� sec �� ms ָ���С�
 */
static void aeAddMillisecondsToNow(long long milliseconds, long *sec, long *ms) {
    long cur_sec, cur_ms, when_sec, when_ms;

    // ��ȡ��ǰʱ��
    aeGetTime(&cur_sec, &cur_ms);

    // �������� milliseconds ֮��������ͺ�����
    when_sec = cur_sec + milliseconds/1000;
    when_ms = cur_ms + milliseconds%1000;

    // ��λ��
    // ��� when_ms ���ڵ��� 1000
    // ��ô�� when_sec ����һ��
    if (when_ms >= 1000) {
        when_sec ++;
        when_ms -= 1000;
    }

    // ���浽ָ����
    *sec = when_sec;
    *ms = when_ms;
}

/*
 * ����ʱ���¼�
 */ //�ļ��¼�aeCreateFileEvent   ʱ���¼�aeCreateTimeEvent aeProcessEvents��ִ���ļ���ʱ���¼�
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc)
{
    // ����ʱ�������
    long long id = eventLoop->timeEventNextId++;

    // ����ʱ���¼��ṹ
    aeTimeEvent *te;

    te = zmalloc(sizeof(*te));
    if (te == NULL) return AE_ERR;

    // ���� ID
    te->id = id;

    // �趨�����¼���ʱ��
    aeAddMillisecondsToNow(milliseconds,&te->when_sec,&te->when_ms);
    // �����¼�������
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;
    // ����˽������
    te->clientData = clientData;

    // �����¼������ͷ
    te->next = eventLoop->timeEventHead;
    eventLoop->timeEventHead = te;

    return id;
}

/*
 * ɾ������ id ��ʱ���¼�
 */
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id)
{
    aeTimeEvent *te, *prev = NULL;

    // ��������
    te = eventLoop->timeEventHead;
    while(te) {

        // ����Ŀ���¼���ɾ��
        if (te->id == id) {

            if (prev == NULL)
                eventLoop->timeEventHead = te->next;
            else
                prev->next = te->next;

            // ִ����������
            if (te->finalizerProc)
                te->finalizerProc(eventLoop, te->clientData);

            // �ͷ�ʱ���¼�
            zfree(te);

            return AE_OK;
        }
        prev = te;
        te = te->next;
    }

    return AE_ERR; /* NO event with the specified ID found */
}

/* Search the first timer to fire.
 * This operation is useful to know how many time the select can be
 * put in sleep without to delay any event.
 * If there are no timers NULL is returned.
 *
 * Note that's O(N) since time events are unsorted.
 * Possible optimizations (not needed by Redis so far, but...):
 * 1) Insert the event in order, so that the nearest is just the head.
 *    Much better but still insertion or deletion of timers is O(N).
 * 2) Use a skiplist to have this operation as O(1) and insertion as O(log(N)).
 */
// Ѱ����Ŀǰʱ�������ʱ���¼�
// ��Ϊ����������ģ����Բ��Ҹ��Ӷ�Ϊ O��N��
static aeTimeEvent *aeSearchNearestTimer(aeEventLoop *eventLoop)
{
    aeTimeEvent *te = eventLoop->timeEventHead;
    aeTimeEvent *nearest = NULL;

    while(te) {
        if (!nearest || te->when_sec < nearest->when_sec ||
                (te->when_sec == nearest->when_sec &&
                 te->when_ms < nearest->when_ms))
            nearest = te;
        te = te->next;
    }
    return nearest;
}

/* Process time events
 *
 * ���������ѵ����ʱ���¼�
 */ // //�ļ��¼�aeCreateFileEvent   ʱ���¼�aeCreateTimeEvent aeProcessEvents��ִ���ļ���ʱ���¼�
static int processTimeEvents(aeEventLoop *eventLoop) {
    int processed = 0;
    aeTimeEvent *te;
    long long maxId;
    time_t now = time(NULL);

    /* If the system clock is moved to the future, and then set back to the
     * right value, time events may be delayed in a random way. Often this
     * means that scheduled operations will not be performed soon enough.
     *
     * Here we try to detect system clock skews, and force all the time
     * events to be processed ASAP when this happens: the idea is that
     * processing events earlier is less dangerous than delaying them
     * indefinitely, and practice suggests it is. */
    // ͨ�������¼�������ʱ�䣬
    // ��ֹ��ʱ�䴩�壨skew������ɵ��¼��������
    if (now < eventLoop->lastTime) {
        te = eventLoop->timeEventHead;
        while(te) {
            te->when_sec = 0;
            te = te->next;
        }
    }
    // �������һ�δ���ʱ���¼���ʱ��
    eventLoop->lastTime = now;

    // ��������
    // ִ����Щ�Ѿ�������¼�
    te = eventLoop->timeEventHead;
    maxId = eventLoop->timeEventNextId-1;
    while(te) {
        long now_sec, now_ms;
        long long id;

        // ������Ч�¼�
        if (te->id > maxId) {
            te = te->next;
            continue;
        }
        
        // ��ȡ��ǰʱ��
        aeGetTime(&now_sec, &now_ms);

        // �����ǰʱ����ڻ�����¼���ִ��ʱ�䣬��ô˵���¼��ѵ��ִ������¼�
        if (now_sec > te->when_sec ||
            (now_sec == te->when_sec && now_ms >= te->when_ms))
        {
            int retval;

            id = te->id;
            // ִ���¼�������������ȡ����ֵ
            retval = te->timeProc(eventLoop, id, te->clientData);
            processed++;
            /* After an event is processed our time event list may
             * no longer be the same, so we restart from head.
             * Still we make sure to don't process events registered
             * by event handlers itself in order to don't loop forever.
             * To do so we saved the max ID we want to handle.
             *
             * FUTURE OPTIMIZATIONS:
             * Note that this is NOT great algorithmically. Redis uses
             * a single time event so it's not a problem but the right
             * way to do this is to add the new elements on head, and
             * to flag deleted elements in a special way for later
             * deletion (putting references to the nodes to delete into
             * another linked list). */
            /*
 ����¼�����������AE_NOMORE����ô����¼�Ϊ��ʱ�¼������¼��ڴﵽһ��֮��ͻᱻɾ����֮���ٵ��
 ����¼�����������һ����AE NOMORE������ֵ����ô����¼�Ϊ������ʱ�䣺��һ��ʱ���¼�����֮�󣬷�����������¼����������ص�ֵ��
 ��ʱ���¼���when���Խ��и��£�������¼���һ��ʱ��֮���ٴε���������ַ�ʽһֱ���²�������ȥ��
 ����˵�����һ��ʱ���¼��ĸ�������������ֵ30����ô������Ӧ�ö����ʱ���¼����и��£�������¼���30����֮���ٴε��


 * ����ʱ���¼��Ƿ�Ҫ����ִ�е� flag
 */
            // ��¼�Ƿ�����Ҫѭ��ִ������¼�ʱ��
            if (retval != AE_NOMORE) {
                // �ǵģ� retval ����֮�����ִ�����ʱ���¼�
                aeAddMillisecondsToNow(retval,&te->when_sec,&te->when_ms);
            } else {
                // ����������¼�ɾ��
                aeDeleteTimeEvent(eventLoop, id);
            }

            // ��Ϊִ���¼�֮���¼��б�����Ѿ����ı���
            // �����Ҫ�� te �Żر�ͷ��������ʼִ���¼�
            te = eventLoop->timeEventHead;
        } else {
            te = te->next;
        }
    }
    return processed;
}

/* Process every pending time event, then every pending file event
 * (that may be registered by time event callbacks just processed).
 *
 * ���������ѵ����ʱ���¼����Լ������Ѿ������ļ��¼���
 *
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurs (if any).
 *
 * ������������� flags �Ļ�����ô����˯��ֱ���ļ��¼�������
 * �����¸�ʱ���¼��������еĻ�����
 *
 * If flags is 0, the function does nothing and returns.
 * ��� flags Ϊ 0 ����ô��������������ֱ�ӷ��ء�
 *
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 * ��� flags ���� AE_ALL_EVENTS ���������͵��¼����ᱻ����
 *
 * if flags has AE_FILE_EVENTS set, file events are processed.
 * ��� flags ���� AE_FILE_EVENTS ����ô�����ļ��¼���
 *
 * if flags has AE_TIME_EVENTS set, time events are processed.
 * ��� flags ���� AE_TIME_EVENTS ����ô����ʱ���¼���
 *
 * if flags has AE_DONT_WAIT set the function returns ASAP until all
 * the events that's possible to process without to wait are processed.
 * ��� flags ���� AE_DONT_WAIT ��
 * ��ô�����ڴ��������в����������¼�֮�󣬼��̷��ء�
 *
 * The function returns the number of events processed. 
 * �����ķ���ֵΪ�Ѵ����¼�������
 */ // //�ļ��¼�aeCreateFileEvent   ʱ���¼�aeCreateTimeEvent aeProcessEvents��ִ���ļ���ʱ���¼�
int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
    int processed = 0, numevents;

    /* Nothing to do? return ASAP */
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;

    /* Note that we want call select() even if there are no
     * file events to process as long as we want to process time
     * events, in order to sleep until the next time event is ready
     * to fire. */
    if (eventLoop->maxfd != -1 ||
        ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
        int j;
        aeTimeEvent *shortest = NULL;
        struct timeval tv, *tvp;

        // ��ȡ�����ʱ���¼�
        if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
            shortest = aeSearchNearestTimer(eventLoop);
        if (shortest) {
            // ���ʱ���¼����ڵĻ�
            // ��ô���������ִ��ʱ���¼�������ʱ���ʱ����������ļ��¼�������ʱ��
            long now_sec, now_ms;

            /* Calculate the time missing for the nearest
             * timer to fire. */
            // �����������ʱ���¼���Ҫ��ò��ܴﵽ
            // ������ʱ��ౣ���� tv �ṹ��
            aeGetTime(&now_sec, &now_ms);
            tvp = &tv;
            tvp->tv_sec = shortest->when_sec - now_sec;
            if (shortest->when_ms < now_ms) {
                tvp->tv_usec = ((shortest->when_ms+1000) - now_ms)*1000;
                tvp->tv_sec --;
            } else {
                tvp->tv_usec = (shortest->when_ms - now_ms)*1000;
            }

            // ʱ���С�� 0 ��˵���¼��Ѿ�����ִ���ˣ�����ͺ�����Ϊ 0 ����������
            if (tvp->tv_sec < 0) tvp->tv_sec = 0;
            if (tvp->tv_usec < 0) tvp->tv_usec = 0;
        } else {
            
            // ִ�е���һ����˵��û��ʱ���¼�
            // ��ô���� AE_DONT_WAIT �Ƿ������������Ƿ��������Լ�������ʱ�䳤��

            /* If we have to check for events but need to return
             * ASAP because of AE_DONT_WAIT we need to set the timeout
             * to zero */
            if (flags & AE_DONT_WAIT) {
                // �����ļ��¼�������
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                /* Otherwise we can block */
                // �ļ��¼���������ֱ�����¼�����Ϊֹ
                tvp = NULL; /* wait forever */
            }
        }

        /*
    aeApiPolI��������������Լ��ɵ���ʱ����ӽ���ǰʱ���ʱ���¼���������������ȿ��Ա����������ʱ���¼�����Ƶ������ѯ��æ�ȴ�����
    Ҳ����ȷ��aeApiPoIl����������������ʱ�䡣

    ��Ϊʱ���¼����ļ��¼�֮��ִ�У������¼�֮�䲻�������ռ������ʱ���¼���ʵ�ʴ���ʱ�䣬ͨ�����ʱ���¼��趨�ĵ���ʱ������һЩ��
          */

        // �����ļ��¼�������ʱ���� tvp ����
        numevents = aeApiPoll(eventLoop, tvp);
        for (j = 0; j < numevents; j++) {
            // ���Ѿ��������л�ȡ�¼�
            aeFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];

            int mask = eventLoop->fired[j].mask;
            int fd = eventLoop->fired[j].fd;
            int rfired = 0;

           /* note the fe->mask & mask & ... code: maybe an already processed
             * event removed an element that fired and we still didn't
             * processed, so we check if the event is still valid. */
            // ���¼�
            if (fe->mask & mask & AE_READABLE) {
                // rfired ȷ����/д�¼�ֻ��ִ������һ��
                rfired = 1;
                fe->rfileProc(eventLoop,fd,fe->clientData,mask);
            }
            // д�¼�
            if (fe->mask & mask & AE_WRITABLE) {
                if (!rfired || fe->wfileProc != fe->rfileProc)
                    fe->wfileProc(eventLoop,fd,fe->clientData,mask);
            }

            processed++;
        }
    }

    /* Check time events */
    // ִ��ʱ���¼�
    if (flags & AE_TIME_EVENTS)
        processed += processTimeEvents(eventLoop);

    return processed; /* return the number of processed file/time events */
}

/* Wait for milliseconds until the given file descriptor becomes
 * writable/readable/exception 
 *
 * �ڸ��������ڵȴ���ֱ�� fd ��ɿ�д���ɶ����쳣
 */
int aeWait(int fd, int mask, long long milliseconds) {
    struct pollfd pfd;
    int retmask = 0, retval;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    if (mask & AE_READABLE) pfd.events |= POLLIN;
    if (mask & AE_WRITABLE) pfd.events |= POLLOUT;

    if ((retval = poll(&pfd, 1, milliseconds))== 1) {
        if (pfd.revents & POLLIN) retmask |= AE_READABLE;
        if (pfd.revents & POLLOUT) retmask |= AE_WRITABLE;
	if (pfd.revents & POLLERR) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLHUP) retmask |= AE_WRITABLE;
        return retmask;
    } else {
        return retval;
    }
}

/*
 * �¼�����������ѭ��
 */ //serverCron��initServer->aeCreateTimeEvent�д�����Ȼ����aeMain(server.el); ��ִ��  ������ʱ���¼�(��ʱʱ�䣬��ʱ�¼�)�Ͷ�д�¼����ڸú�����ִ��
void aeMain(aeEventLoop *eventLoop) {

    eventLoop->stop = 0;

    while (!eventLoop->stop) {

        // �������Ҫ���¼�����ǰִ�еĺ�������ô������
        if (eventLoop->beforesleep != NULL)
            eventLoop->beforesleep(eventLoop); //beforeSleep

        // ��ʼ�����¼�
        aeProcessEvents(eventLoop, AE_ALL_EVENTS);
    }
}

/*
 * ������ʹ�õĶ�·���ÿ������
 */
char *aeGetApiName(void) {
    return aeApiName();
}

/*
 * ���ô����¼�ǰ��Ҫ��ִ�еĺ���
 */
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep) {
    eventLoop->beforesleep = beforesleep;
}
