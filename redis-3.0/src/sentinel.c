/* Redis Sentinel implementation
 *
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

#include "redis.h"
#include "hiredis.h"
#include "async.h"

#include <ctype.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>

extern char **environ;

// sentinel ��Ĭ�϶˿ں�
#define REDIS_SENTINEL_PORT 26379

/* ======================== Sentinel global state =========================== */

/* Address object, used to describe an ip:port pair. */
/* ��ַ�������ڱ��� IP ��ַ�Ͷ˿� */
typedef struct sentinelAddr {
    char *ip;
    int port;
} sentinelAddr;

/* A Sentinel Redis Instance object is monitoring. */
/* ÿ�������ӵ� Redis ʵ�����ᴴ��һ�� sentinelRedisInstance �ṹ
 * ��ÿ���ṹ�� flags ֵ�������³�����һ�������Ĳ� */
// ʵ����һ����������
#define SRI_MASTER  (1<<0)
// ʵ����һ���ӷ�����
#define SRI_SLAVE   (1<<1)
// ʵ����һ�� Sentinel
#define SRI_SENTINEL (1<<2)
// ʵ���Ѷ���
#define SRI_DISCONNECTED (1<<3)
// ʵ���Ѵ��� SDOWN ״̬
#define SRI_S_DOWN (1<<4)   /* Subjectively down (no quorum). */
// ʵ���Ѵ��� ODOWN ״̬
#define SRI_O_DOWN (1<<5)   /* Objectively down (confirmed by others). */
// Sentinel ��Ϊ��������������
#define SRI_MASTER_DOWN (1<<6) /* A Sentinel with this flag set thinks that
                                   its master is down. */
// ���ڶ������������й���Ǩ��
#define SRI_FAILOVER_IN_PROGRESS (1<<7) /* Failover is in progress for
                                           this master. */
// ʵ���Ǳ�ѡ�е�������������Ŀǰ���Ǵӷ�������
#define SRI_PROMOTED (1<<8)            /* Slave selected for promotion. */
// ��ӷ��������� SLAVEOF ���������ת��������������
#define SRI_RECONF_SENT (1<<9)     /* SLAVEOF <newmaster> sent. */
// �ӷ�������������������������ͬ��
#define SRI_RECONF_INPROG (1<<10)   /* Slave synchronization in progress. */
// �ӷ�����������������ͬ����ϣ���ʼ��������������
#define SRI_RECONF_DONE (1<<11)     /* Slave synchronized with new master. */
// ����������ǿ��ִ�й���Ǩ�Ʋ���
#define SRI_FORCE_FAILOVER (1<<12)  /* Force failover with master up. */
// �Ѿ��Է��� -BUSY �ķ��������� SCRIPT KILL ����
#define SRI_SCRIPT_KILL_SENT (1<<13) /* SCRIPT KILL already sent on -BUSY */

/* Note: times are in milliseconds. */
/* ����ʱ�䳣�����Ժ���Ϊ��λ */
// ���� INFO ����ļ��
#define SENTINEL_INFO_PERIOD 10000
// ���� PING ����ļ��
#define SENTINEL_PING_PERIOD 1000
// ���� ASK ����ļ��
#define SENTINEL_ASK_PERIOD 1000
// ���� PUBLISH ����ļ��
#define SENTINEL_PUBLISH_PERIOD 2000
// Ĭ�ϵ��жϷ����������ߵ�ʱ��
#define SENTINEL_DEFAULT_DOWN_AFTER 30000
// Ĭ�ϵ���ϢƵ��
#define SENTINEL_HELLO_CHANNEL "__sentinel__:hello"
// Ĭ�ϵ� TILT ����ʱ��
#define SENTINEL_TILT_TRIGGER 2000
// Ĭ�ϵ� TILT ����ʱ����Ҫ��ò����˳� TITL ģʽ��
#define SENTINEL_TILT_PERIOD (SENTINEL_PING_PERIOD*30)
// Ĭ�ϴӷ��������ȼ�
#define SENTINEL_DEFAULT_SLAVE_PRIORITY 100
#define SENTINEL_SLAVE_RECONF_TIMEOUT 10000
// Ĭ�ϵ�ͬʱ���������������и��ƵĴӷ���������
#define SENTINEL_DEFAULT_PARALLEL_SYNCS 1
// Ĭ�ϵ����������Ӽ��
#define SENTINEL_MIN_LINK_RECONNECT_PERIOD 15000
// Ĭ�ϵĹ���Ǩ��ִ��ʱ��
#define SENTINEL_DEFAULT_FAILOVER_TIMEOUT (60*3*1000)
// Ĭ�ϵ�����ѹ��������
#define SENTINEL_MAX_PENDING_COMMANDS 100
// Ĭ�ϵ�ѡ�ٳ�ʱʱ��
#define SENTINEL_ELECTION_TIMEOUT 10000
#define SENTINEL_MAX_DESYNC 1000

/* Failover machine different states. */
/* ����ת��ʱ��״̬ */
// û��ִ�й���Ǩ��
#define SENTINEL_FAILOVER_STATE_NONE 0  /* No failover in progress. */
// ���ڵȴ���ʼ����Ǩ��
#define SENTINEL_FAILOVER_STATE_WAIT_START 1  /* Wait for failover_start_time*/ 
// ������ѡ��Ϊ�����������Ĵӷ�����
#define SENTINEL_FAILOVER_STATE_SELECT_SLAVE 2 /* Select slave to promote */
// ��ѡ�еĴӷ��������� SLAVEOF no one
#define SENTINEL_FAILOVER_STATE_SEND_SLAVEOF_NOONE 3 /* Slave -> Master */
// �ȴ��ӷ�����ת����������� 
#define SENTINEL_FAILOVER_STATE_WAIT_PROMOTION 4 /* Wait slave to change role */
// �����������������������ӷ��������� SLAVEOF ����
// �����Ǹ����µ���������
#define SENTINEL_FAILOVER_STATE_RECONF_SLAVES 5 /* SLAVEOF newmaster */
// ���ӱ������Ĵӷ�����
#define SENTINEL_FAILOVER_STATE_UPDATE_CONFIG 6 /* Monitor promoted slave. */

/* ���ӷ�����֮�������״̬ */
// ��������
#define SENTINEL_MASTER_LINK_STATUS_UP 0
// ���ӶϿ�
#define SENTINEL_MASTER_LINK_STATUS_DOWN 1

/* Generic flags that can be used with different functions.
 * They use higher bits to avoid colliding with the function specific
 * flags. */
/* �������ڶ��������ͨ�ñ�ʶ��
 * ʹ�ø�λ��������һ���ʶ��ͻ�� */
// û�б�ʶ
#define SENTINEL_NO_FLAGS 0
// �����¼�
#define SENTINEL_GENERATE_EVENT (1<<16)
// ��ͷ
#define SENTINEL_LEADER (1<<17)
// �۲���
#define SENTINEL_OBSERVER (1<<18)

/* Script execution flags and limits. */
/* �ű�ִ��״̬������ */
// �ű�Ŀǰû�б�ִ��
#define SENTINEL_SCRIPT_NONE 0
// �ű�����ִ��
#define SENTINEL_SCRIPT_RUNNING 1
// �ű����б���ű����������ֵ
#define SENTINEL_SCRIPT_MAX_QUEUE 256
// ͬһʱ���ִ�нű����������
#define SENTINEL_SCRIPT_MAX_RUNNING 16
// �ű������ִ��ʱ��
#define SENTINEL_SCRIPT_MAX_RUNTIME 60000 /* 60 seconds max exec time. */
// �ű������������
#define SENTINEL_SCRIPT_MAX_RETRY 10
// �ű�����֮ǰ���ӳ�ʱ��
#define SENTINEL_SCRIPT_RETRY_DELAY 30000 /* 30 seconds between retries. */

// Sentinel ��Ϊÿ�������ӵ� Redis ʵ��������Ӧ�� sentinelRedisInstance ʵ��
// �������ӵ�ʵ�������������������ӷ��������������� Sentinel ��
typedef struct sentinelRedisInstance { //�ýṹ�ڵ������sentinelState->master�ֵ���
    
    // ��ʶֵ����¼��ʵ�������ͣ��Լ���ʵ���ĵ�ǰ״̬
    int flags;      /* See SRI_... defines */
    
    // ʵ��������
    // �����������������û��������ļ�������
    // �ӷ������Լ� Sentinel �������� Sentinel �Զ�����
    // ��ʽΪ ip:port ������ "127.0.0.1:26379"
    char *name;     /* Master name from the point of view of this sentinel. */

    // ʵ�������� ID
    char *runid;    /* run ID of this instance. */

    // ���ü�Ԫ������ʵ�ֹ���ת�� /* current epoch��cluster epoch���Բο�http://redis.cn/topics/cluster-spec.html */
    //������Ч�ĵط���clusterSendFailoverAuthIfNeeded���ڸú����н����ж�
    uint64_t config_epoch;  /* Configuration epoch. */

    // ʵ���ĵ�ַ
    sentinelAddr *addr; /* Master host. */

    // ���ڷ���������첽����
    redisAsyncContext *cc; /* Hiredis context for commands. */

    // ����ִ�� SUBSCRIBE �������Ƶ����Ϣ���첽����
    // ����ʵ��Ϊ��������ʱʹ��
    redisAsyncContext *pc; /* Hiredis context for Pub / Sub. */

    // �ѷ��͵���δ�ظ�����������
    int pending_commands;   /* Number of commands sent waiting for a reply. */

    // cc ���ӵĴ���ʱ��
    mstime_t cc_conn_time; /* cc connection time. */
    
    // pc ���ӵĴ���ʱ��
    mstime_t pc_conn_time; /* pc connection time. */

    // ���һ�δ����ʵ��������Ϣ��ʱ��
    mstime_t pc_last_activity; /* Last time we received any message. */

    // ʵ�����һ�η�����ȷ�� PING ����ظ���ʱ��
    mstime_t last_avail_time; /* Last time the instance replied to ping with
                                 a reply we consider valid. */
    // ʵ�����һ�η��� PING �����ʱ��
    mstime_t last_ping_time;  /* Last time a pending ping was sent in the
                                 context of the current command connection
                                 with the instance. 0 if still not sent or
                                 if pong already received. */
    // ʵ�����һ�η��� PING �����ʱ�䣬����������ȷ���
    mstime_t last_pong_time;  /* Last time the instance replied to ping,
                                 whatever the reply was. That's used to check
                                 if the link is idle and must be reconnected. */

    // ���һ����Ƶ�������ʺ���Ϣ��ʱ��
    // ֻ�ڵ�ǰʵ��Ϊ sentinel ʱʹ��
    mstime_t last_pub_time;   /* Last time we sent hello via Pub/Sub. */

    // ���һ�ν��յ���� sentinel �������ʺ���Ϣ��ʱ��
    // ֻ�ڵ�ǰʵ��Ϊ sentinel ʱʹ��
    mstime_t last_hello_time; /* Only used if SRI_SENTINEL is set. Last time
                                 we received a hello from this Sentinel
                                 via Pub/Sub. */

    // ���һ�λظ� SENTINEL is-master-down-by-addr �����ʱ��
    // ֻ�ڵ�ǰʵ��Ϊ sentinel ʱʹ��
    mstime_t last_master_down_reply_time; /* Time of last reply to
                                             SENTINEL is-master-down command. */

    // ʵ�����ж�Ϊ SDOWN ״̬��ʱ��
    mstime_t s_down_since_time; /* Subjectively down since time. */

    // ʵ�����ж�Ϊ ODOWN ״̬��ʱ��
    mstime_t o_down_since_time; /* Objectively down since time. */

    // SENTINEL down-after-milliseconds ѡ�����趨��ֵ
    // ʵ������Ӧ���ٺ���֮��Żᱻ�ж�Ϊ�������ߣ�subjectively down��
    mstime_t down_after_period; /* Consider it down after that period. */

    // ��ʵ����ȡ INFO ����Ļظ���ʱ��
    mstime_t info_refresh;  /* Time at which we received INFO output from it. */

    /* Role and the first time we observed it.
     * This is useful in order to delay replacing what the instance reports
     * with our own configuration. We need to always wait some time in order
     * to give a chance to the leader to report the new configuration before
     * we do silly things. */
    // ʵ���Ľ�ɫ
    int role_reported;
    // ��ɫ�ĸ���ʱ��
    mstime_t role_reported_time;

    // ���һ�δӷ�����������������ַ�����ʱ��
    mstime_t slave_conf_change_time; /* Last time slave master addr changed. */

    /* Master specific. */
    /*
    SentinelΪ��������������ʵ���ṹ�е�sentinels�ֵ䱣���˳�Sentinel����֮�⣬����ͬ�����������������������Sentinel�����ϣ�
    sentinels�ֵ�ļ�������һ��Sentinel�����֣���ʽΪip:port���������IP��ַΪ127.0.0.1���˿ں�Ϊ26379��Sentinel��˵�����Sentinel��sentinels�ֵ��еļ�����"127.0.0.1��26379"��
    sentinels�ֵ��ֵ���Ǽ�����ӦSentinel��ʵ���ṹ,��sentinelRedisInstance
     */
    
    /* ��������ʵ�����е����� -------------------------------------------------------------*/

    // ����ͬ���������������������� sentinel
    dict *sentinels;    /* Other sentinels monitoring the same master. */

    // ������ʵ���������һ����������
    // ��ô����ֵ䱣���������������µĴӷ�����
    // �ֵ�ļ��Ǵӷ����������֣��ֵ��ֵ�Ǵӷ�������Ӧ�� sentinelRedisInstance �ṹ
    dict *slaves;       /* Slaves for this master instance. */

    // SENTINEL monitor <master-name> <IP> <port> <quorum> ѡ���е� quorum ����
    // �ж����ʵ��Ϊ�͹����ߣ�objectively down�������֧��ͶƱ����   ����Ҫ�м���sentinel�������ж���������������
    int quorum;         /* Number of sentinels that need to agree on failure. */

    // SENTINEL parallel-syncs <master-name> <number> ѡ���ֵ
    // ��ִ�й���ת�Ʋ���ʱ������ͬʱ���µ�������������ͬ���Ĵӷ���������
    int parallel_syncs; /* How many slaves to reconfigure at same time. */

    // �������������ʹӷ��������������
    char *auth_pass;    /* Password to use for AUTH against master & slaves. */

    /* Slave specific. */
    /* �ӷ�����ʵ�����е����� -------------------------------------------------------------*/

    // ���ӷ��������ӶϿ���ʱ��
    mstime_t master_link_down_time; /* Slave replication link down time. */

    // �ӷ��������ȼ�
    int slave_priority; /* Slave priority according to its INFO output. */

    // ִ�й���ת�Ʋ���ʱ���ӷ��������� SLAVEOF <new-master> �����ʱ��
    mstime_t slave_reconf_sent_time; /* Time at which we sent SLAVE OF <new> */

    // ����������ʵ�����ڱ�ʵ��Ϊ�ӷ�����ʱʹ�ã�
    struct sentinelRedisInstance *master; /* Master instance if it's slave. */

    // INFO ����Ļظ��м�¼���������� IP
    char *slave_master_host;    /* Master host as reported by INFO */
    
    // INFO ����Ļظ��м�¼�����������˿ں�
    int slave_master_port;      /* Master port as reported by INFO */

    // INFO ����Ļظ��м�¼�����ӷ���������״̬
    int slave_master_link_status; /* Master link status as reported by INFO */

    // �ӷ������ĸ���ƫ����
    unsigned long long slave_repl_offset; /* Slave replication offset. */

    /* Failover */
    /* ����ת��������� -------------------------------------------------------------------*/


    // �������һ����������ʵ������ô leader ���Ǹ�����й���ת�Ƶ� Sentinel ������ ID ��
    // �������һ�� Sentinel ʵ������ô leader ���Ǳ�ѡ�ٳ�������ͷ Sentinel ��
    // �����ֻ�� Sentinel ʵ���� flags ���Ե� SRI_MASTER_DOWN ��־���ڴ�״̬ʱ����Ч��
    char *leader;       /* If this is a master instance, this is the runid of
                           the Sentinel that should perform the failover. If
                           this is a Sentinel, this is the runid of the Sentinel
                           that this Sentinel voted as leader. */
    // ��ͷ�ļ�Ԫ
    uint64_t leader_epoch; /* Epoch of the 'leader' field. */
    // ��ǰִ���еĹ���ת�Ƶļ�Ԫ
    uint64_t failover_epoch; /* Epoch of the currently started failover. */
    // ����ת�Ʋ����ĵ�ǰ״̬
    int failover_state; /* See SENTINEL_FAILOVER_STATE_* defines. */

    // ״̬�ı��ʱ��
    mstime_t failover_state_change_time;

    // ���һ�ν��й���Ǩ�Ƶ�ʱ��
    mstime_t failover_start_time;   /* Last failover attempt start time. */

    // SENTINEL failover-timeout <master-name> <ms> ѡ���ֵ
    // ˢ�¹���Ǩ��״̬�����ʱ��
    mstime_t failover_timeout;      /* Max time to refresh failover state. */

    mstime_t failover_delay_logged; /* For what failover_start_time value we
                                       logged the failover delay. */
    // ָ������Ϊ�����������Ĵӷ�������ָ��
    struct sentinelRedisInstance *promoted_slave; /* Promoted slave instance. */

    /* Scripts executed to notify admin or reconfigure clients: when they
     * are set to NULL no script is executed. */
    // һ���ļ�·���������� WARNING ������¼�����ʱִ�еģ�
    // ����֪ͨ����Ա�Ľű��ĵ�ַ
    char *notification_script;

    // һ���ļ�·���������Ź���ת��ִ��֮ǰ��֮�󡢻��߱���ֹʱ��
    // ��Ҫִ�еĽű��ĵ�ַ
    char *client_reconfig_script;

} sentinelRedisInstance;

/* Main state. */
/* Sentinel ��״̬�ṹ */
struct sentinelState {

    // ��ǰ��Ԫ  ����ʵ�ֹ���ת�� /* current epoch��cluster epoch���Բο�http://redis.cn/topics/cluster-spec.html */
    uint64_t current_epoch;     /* Current epoch. */

    // ���������б���� sentinel ���ӵ���������
    // �ֵ�ļ�����������������
    // �ֵ��ֵ����һ��ָ�� sentinelRedisInstance �ṹ��ָ��
    dict *masters;      /* Dictionary of master sentinelRedisInstances.
                           Key is the instance name, value is the
                           sentinelRedisInstance structure pointer. */

    // �Ƿ������ TILT ģʽ��  ��νTITLģʽ��ֻ�ռ����ݣ�������fail-over
    int tilt;           /* Are we in TILT mode? */

    // Ŀǰ����ִ�еĽű�������
    int running_scripts;    /* Number of scripts in execution right now. */

    // ���� TILT ģʽ��ʱ��
    mstime_t tilt_start_time;   /* When TITL started. */

    // ���һ��ִ��ʱ�䴦������ʱ��
    mstime_t previous_time;     /* Last time we ran the time handler. */

    // һ�� FIFO ���У�������������Ҫִ�е��û��ű�
    list *scripts_queue;    /* Queue of user scripts to execute. */

} sentinel;

/* A script execution job. */
// �ű�����״̬
typedef struct sentinelScriptJob {

    // ��־����¼�˽ű��Ƿ�����
    int flags;              /* Script job flags: SENTINEL_SCRIPT_* */

    // �ýű����ѳ���ִ�д���
    int retry_num;          /* Number of times we tried to execute it. */

    // Ҫ�����ű��Ĳ���
    char **argv;            /* Arguments to call the script. */

    // ��ʼ���нű���ʱ��
    mstime_t start_time;    /* Script execution time if the script is running,
                               otherwise 0 if we are allowed to retry the
                               execution at any time. If the script is not
                               running and it's not 0, it means: do not run
                               before the specified time. */

    // �ű����ӽ���ִ�У������Լ�¼�ӽ��̵� pid
    pid_t pid;              /* Script execution pid. */

} sentinelScriptJob;

/* ======================= hiredis ae.c adapters =============================
 * Note: this implementation is taken from hiredis/adapters/ae.h, however
 * we have our modified copy for Sentinel in order to use our allocator
 * and to have full control over how the adapter works. */

// �ͻ�����������adapter���ṹ
typedef struct redisAeEvents {

    // �ͻ�������������
    redisAsyncContext *context;

    // ���������¼�ѭ��
    aeEventLoop *loop;

    // �׽���
    int fd;

    // ��¼���¼��Լ�д�¼��Ƿ����
    int reading, writing;

} redisAeEvents;

// ���¼������� 
static void redisAeReadEvent(aeEventLoop *el, int fd, void *privdata, int mask) {
    ((void)el); ((void)fd); ((void)mask);

    redisAeEvents *e = (redisAeEvents*)privdata;
    // �������н��ж�ȡ
    redisAsyncHandleRead(e->context);
}

// д�¼�������
static void redisAeWriteEvent(aeEventLoop *el, int fd, void *privdata, int mask) {
    ((void)el); ((void)fd); ((void)mask);

    redisAeEvents *e = (redisAeEvents*)privdata;
    // �������н���д��
    redisAsyncHandleWrite(e->context);
}

// �����¼���������װ���¼�ѭ����
static void redisAeAddRead(void *privdata) {
    redisAeEvents *e = (redisAeEvents*)privdata;
    aeEventLoop *loop = e->loop;
    // ������¼�������δ��װ����ô���а�װ
    if (!e->reading) {
        e->reading = 1;
        aeCreateFileEvent(loop,e->fd,AE_READABLE,redisAeReadEvent,e);
    }
}

// ���¼�ѭ����ɾ�����¼�������
static void redisAeDelRead(void *privdata) {
    redisAeEvents *e = (redisAeEvents*)privdata;
    aeEventLoop *loop = e->loop;
    // ���ڶ��¼��������Ѱ�װ������½���ɾ��
    if (e->reading) {
        e->reading = 0;
        aeDeleteFileEvent(loop,e->fd,AE_READABLE);
    }
}

// ��д�¼���������װ���¼�ѭ����
static void redisAeAddWrite(void *privdata) {
    redisAeEvents *e = (redisAeEvents*)privdata;
    aeEventLoop *loop = e->loop;
    if (!e->writing) {
        e->writing = 1;
        aeCreateFileEvent(loop,e->fd,AE_WRITABLE,redisAeWriteEvent,e);
    }
}

// ���¼�ѭ����ɾ��д�¼�������
static void redisAeDelWrite(void *privdata) {
    redisAeEvents *e = (redisAeEvents*)privdata;
    aeEventLoop *loop = e->loop;
    if (e->writing) {
        e->writing = 0;
        aeDeleteFileEvent(loop,e->fd,AE_WRITABLE);
    }
}

// �����¼�
static void redisAeCleanup(void *privdata) {
    redisAeEvents *e = (redisAeEvents*)privdata;
    redisAeDelRead(privdata);
    redisAeDelWrite(privdata);
    zfree(e);
}

// Ϊ������ ae ���¼�ѭ�� loop ���� hiredis ������
// ��������ص��첽������
static int redisAeAttach(aeEventLoop *loop, redisAsyncContext *ac) {
    redisContext *c = &(ac->c);
    redisAeEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    // ����������
    e = (redisAeEvents*)zmalloc(sizeof(*e));
    e->context = ac;
    e->loop = loop;
    e->fd = c->fd;
    e->reading = e->writing = 0;

    /* Register functions to start/stop listening for events */
    // �����첽���ú���
    ac->ev.addRead = redisAeAddRead;
    ac->ev.delRead = redisAeDelRead;
    ac->ev.addWrite = redisAeAddWrite;
    ac->ev.delWrite = redisAeDelWrite;
    ac->ev.cleanup = redisAeCleanup;
    ac->ev.data = e;

    return REDIS_OK;
}

/* ============================= Prototypes ================================= */

void sentinelLinkEstablishedCallback(const redisAsyncContext *c, int status);
void sentinelDisconnectCallback(const redisAsyncContext *c, int status);
void sentinelReceiveHelloMessages(redisAsyncContext *c, void *reply, void *privdata);
sentinelRedisInstance *sentinelGetMasterByName(char *name);
char *sentinelGetSubjectiveLeader(sentinelRedisInstance *master);
char *sentinelGetObjectiveLeader(sentinelRedisInstance *master);
int yesnotoi(char *s);
void sentinelDisconnectInstanceFromContext(const redisAsyncContext *c);
void sentinelKillLink(sentinelRedisInstance *ri, redisAsyncContext *c);
const char *sentinelRedisInstanceTypeStr(sentinelRedisInstance *ri);
void sentinelAbortFailover(sentinelRedisInstance *ri);
void sentinelEvent(int level, char *type, sentinelRedisInstance *ri, const char *fmt, ...);
sentinelRedisInstance *sentinelSelectSlave(sentinelRedisInstance *master);
void sentinelScheduleScriptExecution(char *path, ...);
void sentinelStartFailover(sentinelRedisInstance *master);
void sentinelDiscardReplyCallback(redisAsyncContext *c, void *reply, void *privdata);
int sentinelSendSlaveOf(sentinelRedisInstance *ri, char *host, int port);
char *sentinelVoteLeader(sentinelRedisInstance *master, uint64_t req_epoch, char *req_runid, uint64_t *leader_epoch);
void sentinelFlushConfig(void);
void sentinelGenerateInitialMonitorEvents(void);
int sentinelSendPing(sentinelRedisInstance *ri);

/* ========================= Dictionary types =============================== */

unsigned int dictSdsHash(const void *key);
int dictSdsKeyCompare(void *privdata, const void *key1, const void *key2);
void releaseSentinelRedisInstance(sentinelRedisInstance *ri);

void dictInstancesValDestructor (void *privdata, void *obj) {
    releaseSentinelRedisInstance(obj);
}

/* Instance name (sds) -> instance (sentinelRedisInstance pointer)
 *
 * also used for: sentinelRedisInstance->sentinels dictionary that maps
 * sentinels ip:port to last seen time in Pub/Sub hello message. */
// ����ֵ��������������ã�
// 1�� ��ʵ������ӳ�䵽һ�� sentinelRedisInstance ָ��
// 2�� �� sentinelRedisInstance ָ��ӳ�䵽һ���ֵ䣬
//     �ֵ�ļ��� Sentinel �� ip:port ��ַ��
//     �ֵ��ֵ�Ǹ� Sentinel ���һ����Ƶ��������Ϣ��ʱ��
dictType instancesDictType = {
    dictSdsHash,               /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dictSdsKeyCompare,         /* key compare */
    NULL,                      /* key destructor */
    dictInstancesValDestructor /* val destructor */
};

/* Instance runid (sds) -> votes (long casted to void*)
 *
 * This is useful into sentinelGetObjectiveLeader() function in order to
 * count the votes and understand who is the leader. */
// ��һ������ ID ӳ�䵽һ�� cast �� void* ���͵� long ֵ��ͶƱ������
// ����ͳ�ƿ͹� leader sentinel
dictType leaderVotesDictType = {
    dictSdsHash,               /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dictSdsKeyCompare,         /* key compare */
    NULL,                      /* key destructor */
    NULL                       /* val destructor */
};

/* =========================== Initialization =============================== */

void sentinelCommand(redisClient *c);
void sentinelInfoCommand(redisClient *c);
void sentinelSetCommand(redisClient *c);
void sentinelPublishCommand(redisClient *c);

// �������� sentinel ģʽ�¿�ִ�е�����  //sentinelcmds  redisCommandTable  �����ļ����ؼ�loadServerConfigFromString 
struct redisCommand sentinelcmds[] = {
    {"ping",pingCommand,1,"",0,NULL,0,0,0,0,0},
    {"sentinel",sentinelCommand,-2,"",0,NULL,0,0,0,0,0}, //sentinel�����ļ����ؼ�sentinelHandleConfiguration��sentinel�����sentinelHandleConfiguration
    {"subscribe",subscribeCommand,-2,"",0,NULL,0,0,0,0,0},
    {"unsubscribe",unsubscribeCommand,-1,"",0,NULL,0,0,0,0,0},
    {"psubscribe",psubscribeCommand,-2,"",0,NULL,0,0,0,0,0},
    {"punsubscribe",punsubscribeCommand,-1,"",0,NULL,0,0,0,0,0},
    {"publish",sentinelPublishCommand,3,"",0,NULL,0,0,0,0,0},
    {"info",sentinelInfoCommand,-1,"",0,NULL,0,0,0,0,0},
    {"shutdown",shutdownCommand,-1,"",0,NULL,0,0,0,0,0}
};

/* This function overwrites a few normal Redis config default with Sentinel
 * specific defaults. */
// ����������� Sentinel ���������Ը��Ƿ�����Ĭ�ϵ�����
void initSentinelConfig(void) {
    server.port = REDIS_SENTINEL_PORT;
}

/* Perform the Sentinel mode initialization. */
// �� Sentinel ģʽ��ʼ��������
void initSentinel(void) {
    int j;

    /* Remove usual Redis commands from the command table, then just add
     * the SENTINEL command. */

    // ��� Redis ��������������ñ�������ͨģʽ��
    dictEmpty(server.commands,NULL);
    // �� SENTINEL ģʽ���õ�������ӽ������
    for (j = 0; j < sizeof(sentinelcmds)/sizeof(sentinelcmds[0]); j++) {
        int retval;
        struct redisCommand *cmd = sentinelcmds+j;

        retval = dictAdd(server.commands, sdsnew(cmd->name), cmd);
        redisAssert(retval == DICT_OK);
    }

    /* Initialize various data structures. */
    /* ��ʼ�� Sentinel ��״̬ */
    // ��ʼ����Ԫ
    sentinel.current_epoch = 0;

    // ��ʼ����������������Ϣ���ֵ�
    sentinel.masters = dictCreate(&instancesDictType,NULL);

    // ��ʼ�� TILT ģʽ�����ѡ��
    sentinel.tilt = 0;
    sentinel.tilt_start_time = 0;
    sentinel.previous_time = mstime();

    // ��ʼ���ű����ѡ��
    sentinel.running_scripts = 0;
    sentinel.scripts_queue = listCreate();
}

/* This function gets called when the server is in Sentinel mode, started,
 * loaded the configuration, and is ready for normal operations. */
// ��������� Sentinel ׼������������ִ�в���ʱִ��
void sentinelIsRunning(void) {
    redisLog(REDIS_WARNING,"Sentinel runid is %s", server.runid);

    // Sentinel ������û�������ļ��������ִ��
    if (server.configfile == NULL) {
        redisLog(REDIS_WARNING,
            "Sentinel started without a config file. Exiting...");
        exit(1);
    } else if (access(server.configfile,W_OK) == -1) {
        redisLog(REDIS_WARNING,
            "Sentinel config file %s is not writable: %s. Exiting...",
            server.configfile,strerror(errno));
        exit(1);
    }

    /* We want to generate a +monitor event for every configured master
     * at startup. */
    sentinelGenerateInitialMonitorEvents();
}

/* ============================== sentinelAddr ============================== */

/* Create a sentinelAddr object and return it on success.
 *
 * ����һ�� sentinel ��ַ���󣬲��ڴ����ɹ�ʱ���ظö���
 *
 * On error NULL is returned and errno is set to:
 *
 * �����ڳ���ʱ���� NULL ������ errnor ��Ϊ����ֵ��
 *
 *  ENOENT: Can't resolve the hostname.
 *          ���ܽ��� hostname
 *
 *  EINVAL: Invalid port number.
 *          �˿ںŲ���ȷ
 */
sentinelAddr *createSentinelAddr(char *hostname, int port) {
    char buf[32];
    sentinelAddr *sa;

    // ���˿ں�
    if (port <= 0 || port > 65535) {
        errno = EINVAL;
        return NULL;
    }

    // ��鲢������ַ
    if (anetResolve(NULL,hostname,buf,sizeof(buf)) == ANET_ERR) {
        errno = ENOENT;
        return NULL;
    }

    // ���������ص�ַ�ṹ
    sa = zmalloc(sizeof(*sa));
    sa->ip = sdsnew(buf);
    sa->port = port;
    return sa;
}

/* Return a duplicate of the source address. */
// ���Ʋ����ظ�����ַ��һ������
sentinelAddr *dupSentinelAddr(sentinelAddr *src) {
    sentinelAddr *sa;

    sa = zmalloc(sizeof(*sa));
    sa->ip = sdsnew(src->ip);
    sa->port = src->port;
    return sa;
}

/* Free a Sentinel address. Can't fail. */
// �ͷ� Sentinel ��ַ
void releaseSentinelAddr(sentinelAddr *sa) {
    sdsfree(sa->ip);
    zfree(sa);
}

/* Return non-zero if two addresses are equal. */
// ���������ַ��ͬ����ô���� 0
int sentinelAddrIsEqual(sentinelAddr *a, sentinelAddr *b) {
    return a->port == b->port && !strcasecmp(a->ip,b->ip);
}

/* =========================== Events notification ========================== */

/* Send an event to log, pub/sub, user notification script.
 *
 * ���¼����͵���־��Ƶ�����Լ��û����ѽű���
 * 
 * 'level' is the log level for logging. Only REDIS_WARNING events will trigger
 * the execution of the user notification script.
 *
 * level ����־�ļ���ֻ�� REDIS_WARNING �������־�ᴥ���û����ѽű���
 *
 * 'type' is the message type, also used as a pub/sub channel name.
 *
 * type ����Ϣ�����ͣ�Ҳ����Ƶ�������֡�
 *
 * 'ri', is the redis instance target of this event if applicable, and is
 * used to obtain the path of the notification script to execute.
 *
 * ri �������¼��� Redis ʵ����������������ȡ��ִ�е��û��ű���
 *
 * The remaining arguments are printf-alike.
 *
 * ʣ�µĶ��������ڴ��� printf �����Ĳ�����
 *
 * If the format specifier starts with the two characters "%@" then ri is
 * not NULL, and the message is prefixed with an instance identifier in the
 * following format:
 *
 * �����ʽָ���� "%@" �����ַ���ͷ������ ri ��Ϊ�գ�
 * ��ô��Ϣ��ʹ������ʵ����ʶ��Ϊ��ͷ��
 *
 *  <instance type> <instance name> <ip> <port>
 *
 *  If the instance type is not master, than the additional string is
 *  added to specify the originating master:
 *
 *  ���ʵ�������Ͳ���������������ô�������ݻᱻ׷�ӵ���Ϣ�ĺ��棬
 *  ����ָ��Ŀ������������
 *
 *  @ <master name> <master ip> <master port>
 *
 *  Any other specifier after "%@" is processed by printf itself.
 *
 * "%@" ֮�������ָ������specifier������ printf ������ʹ�õ�ָ����һ����
 */
void sentinelEvent(int level, char *type, sentinelRedisInstance *ri,
                   const char *fmt, ...) {
    va_list ap;
    // ��־�ַ���
    char msg[REDIS_MAX_LOGMSG_LEN];
    robj *channel, *payload;

    /* Handle %@ */
    // ���� %@
    if (fmt[0] == '%' && fmt[1] == '@') {

        // ��� ri ʵ����������������ô master ���� NULL 
        // ���� ri ����һ���ӷ��������� sentinel ���� master ���Ǹ�ʵ������������
        //
        // sentinelRedisInstance *master = NULL;
        // if (~(ri->flags & SRI_MASTER))
        //     master = ri->master;
        sentinelRedisInstance *master = (ri->flags & SRI_MASTER) ?
                                         NULL : ri->master;

        if (master) {
            
            // ri ������������

            snprintf(msg, sizeof(msg), "%s %s %s %d @ %s %s %d",
                // ��ӡ ri ������
                sentinelRedisInstanceTypeStr(ri),
                // ��ӡ ri �����֡�IP �Ͷ˿ں�
                ri->name, ri->addr->ip, ri->addr->port,
                // ��ӡ ri ���������������֡� IP �Ͷ˿ں�
                master->name, master->addr->ip, master->addr->port);
        } else {

            // ri ����������

            snprintf(msg, sizeof(msg), "%s %s %s %d",
                // ��ӡ ri ������
                sentinelRedisInstanceTypeStr(ri),
                // ��ӡ ri �����֡�IP �Ͷ˿ں�
                ri->name, ri->addr->ip, ri->addr->port);
        }

        // �����Ѵ���� "%@" �ַ�
        fmt += 2;

    } else {
        msg[0] = '\0';
    }

    /* Use vsprintf for the rest of the formatting if any. */
    // ��ӡ֮������ݣ���ʽ��ƽ���� printf һ��
    if (fmt[0] != '\0') {
        va_start(ap, fmt);
        vsnprintf(msg+strlen(msg), sizeof(msg)-strlen(msg), fmt, ap);
        va_end(ap);
    }

    /* Log the message if the log level allows it to be logged. */
    // �����־�ļ����㹻�ߵĻ�����ô��¼����־��
    if (level >= server.verbosity)
        redisLog(level,"%s %s",type,msg);

    /* Publish the message via Pub/Sub if it's not a debugging one. */
    // �����־���� DEBUG ��־����ô�������͵�Ƶ����
    if (level != REDIS_DEBUG) {
        // Ƶ��
        channel = createStringObject(type,strlen(type));
        // ����
        payload = createStringObject(msg,strlen(msg));
        // ������Ϣ
        pubsubPublishMessage(channel,payload);
        decrRefCount(channel);
        decrRefCount(payload);
    }

    /* Call the notification script if applicable. */
    // �������Ҫ�Ļ����������ѽű�
    if (level == REDIS_WARNING && ri != NULL) {
        sentinelRedisInstance *master = (ri->flags & SRI_MASTER) ?
                                         ri : ri->master;
        if (master->notification_script) {
            sentinelScheduleScriptExecution(master->notification_script,
                type,msg,NULL);
        }
    }
}

/* This function is called only at startup and is used to generate a
 * +monitor event for every configured master. The same events are also
 * generated when a master to monitor is added at runtime via the
 * SENTINEL MONITOR command. */
// �� Sentinel ����ʱִ�У����ڴ��������� +monitor �¼�
void sentinelGenerateInitialMonitorEvents(void) {
    dictIterator *di;
    dictEntry *de;

    di = dictGetIterator(sentinel.masters);
    while((de = dictNext(di)) != NULL) {
        sentinelRedisInstance *ri = dictGetVal(de);
        sentinelEvent(REDIS_WARNING,"+monitor",ri,"%@ quorum %d",ri->quorum);
    }
    dictReleaseIterator(di);
}

/* ============================ script execution ============================ */

/* Release a script job structure and all the associated data. */
// �ͷ�һ���ű�����ṹ���Լ��������������ݡ�
void sentinelReleaseScriptJob(sentinelScriptJob *sj) {
    int j = 0;

    while(sj->argv[j]) sdsfree(sj->argv[j++]);
    zfree(sj->argv);
    zfree(sj);
}

// �����������ͽű��������
#define SENTINEL_SCRIPT_MAX_ARGS 16
void sentinelScheduleScriptExecution(char *path, ...) {
    va_list ap;
    char *argv[SENTINEL_SCRIPT_MAX_ARGS+1];
    int argc = 1;
    sentinelScriptJob *sj;

    // ���ɲ���
    va_start(ap, path);
    while(argc < SENTINEL_SCRIPT_MAX_ARGS) {
        argv[argc] = va_arg(ap,char*);
        if (!argv[argc]) break;
        argv[argc] = sdsnew(argv[argc]); /* Copy the string. */
        argc++;
    }
    va_end(ap);
    argv[0] = sdsnew(path);
    
    // ��ʼ���ű��ṹ
    sj = zmalloc(sizeof(*sj));
    sj->flags = SENTINEL_SCRIPT_NONE;
    sj->retry_num = 0;
    sj->argv = zmalloc(sizeof(char*)*(argc+1));
    sj->start_time = 0;
    sj->pid = 0;
    memcpy(sj->argv,argv,sizeof(char*)*(argc+1));

    // ��ӵ��ȴ�ִ�нű����е�ĩβ�� FIFO
    listAddNodeTail(sentinel.scripts_queue,sj);

    /* Remove the oldest non running script if we already hit the limit. */
    // �����ӵĽű�����̫�࣬��ô�Ƴ���ɵ�δִ�нű�
    if (listLength(sentinel.scripts_queue) > SENTINEL_SCRIPT_MAX_QUEUE) {
        listNode *ln;
        listIter li;

        listRewind(sentinel.scripts_queue,&li);
        while ((ln = listNext(&li)) != NULL) {
            sj = ln->value;

            // ��ɾ���������еĽű�
            if (sj->flags & SENTINEL_SCRIPT_RUNNING) continue;
            /* The first node is the oldest as we add on tail. */
            listDelNode(sentinel.scripts_queue,ln);
            sentinelReleaseScriptJob(sj);
            break;
        }
        redisAssert(listLength(sentinel.scripts_queue) <=
                    SENTINEL_SCRIPT_MAX_QUEUE);
    }
}

/* Lookup a script in the scripts queue via pid, and returns the list node
 * (so that we can easily remove it from the queue if needed). */
// ���� pid ���������������еĽű�
listNode *sentinelGetScriptListNodeByPid(pid_t pid) {
    listNode *ln;
    listIter li;

    listRewind(sentinel.scripts_queue,&li);
    while ((ln = listNext(&li)) != NULL) {
        sentinelScriptJob *sj = ln->value;

        if ((sj->flags & SENTINEL_SCRIPT_RUNNING) && sj->pid == pid)
            return ln;
    }
    return NULL;
}

/* Run pending scripts if we are not already at max number of running
 * scripts. */
// ���еȴ�ִ�еĽű�
void sentinelRunPendingScripts(void) {
    listNode *ln;
    listIter li;
    mstime_t now = mstime();

    /* Find jobs that are not running and run them, from the top to the
     * tail of the queue, so we run older jobs first. */
    // ������еĽű�����δ�������ֵ��
    // ��ô�� FIFO ������ȡ��δ���еĽű��������иýű�
    listRewind(sentinel.scripts_queue,&li);
    while (sentinel.running_scripts < SENTINEL_SCRIPT_MAX_RUNNING &&
           (ln = listNext(&li)) != NULL)
    {
        sentinelScriptJob *sj = ln->value;
        pid_t pid;

        /* Skip if already running. */
        // ���������нű�
        if (sj->flags & SENTINEL_SCRIPT_RUNNING) continue;

        /* Skip if it's a retry, but not enough time has elapsed. */
        // ����һ�����Խű��������ո�ִ���꣬�Ժ�������
        if (sj->start_time && sj->start_time > now) continue;

        // �����б��
        sj->flags |= SENTINEL_SCRIPT_RUNNING;
        // ��¼��ʼʱ��
        sj->start_time = mstime();
        // �������Լ�����
        sj->retry_num++;

        // �����ӽ���
        pid = fork();

        if (pid == -1) {
            
            // �����ӽ���ʧ��

            /* Parent (fork error).
             * We report fork errors as signal 99, in order to unify the
             * reporting with other kind of errors. */
            sentinelEvent(REDIS_WARNING,"-script-error",NULL,
                          "%s %d %d", sj->argv[0], 99, 0);
            sj->flags &= ~SENTINEL_SCRIPT_RUNNING;
            sj->pid = 0;
        } else if (pid == 0) {

            // �ӽ���ִ�нű�

            /* Child */
            execve(sj->argv[0],sj->argv,environ);
            /* If we are here an error occurred. */
            _exit(2); /* Don't retry execution. */
        } else {

            // ������
            
            // �������нű�������
            sentinel.running_scripts++;

            // ��¼ pid
            sj->pid = pid;

            // ���ͽű������ź�
            sentinelEvent(REDIS_DEBUG,"+script-child",NULL,"%ld",(long)pid);
        }
    }
}

/* How much to delay the execution of a script that we need to retry after
 * an error?
 *
 * We double the retry delay for every further retry we do. So for instance
 * if RETRY_DELAY is set to 30 seconds and the max number of retries is 10
 * starting from the second attempt to execute the script the delays are:
 * 30 sec, 60 sec, 2 min, 4 min, 8 min, 16 min, 32 min, 64 min, 128 min. */
// �������Խű�ǰ���ӳ�ʱ��
mstime_t sentinelScriptRetryDelay(int retry_num) {
    mstime_t delay = SENTINEL_SCRIPT_RETRY_DELAY;

    while (retry_num-- > 1) delay *= 2;
    return delay;
}

/* Check for scripts that terminated, and remove them from the queue if the
 * script terminated successfully. If instead the script was terminated by
 * a signal, or returned exit code "1", it is scheduled to run again if
 * the max number of retries did not already elapsed. */
// ���ű����˳�״̬�����ڽű��ɹ��˳�ʱ�����ű��Ӷ�����ɾ����
// ����ű����ź��սᣬ���߷����˳����� 1 ����ôֻҪ�ýű������Դ���δ��������
// ��ô�ýű��ͻᱻ���ȣ����ȴ�����
void sentinelCollectTerminatedScripts(void) {
    int statloc;
    pid_t pid;

    // ��ȡ�ӽ����ź�
    while ((pid = wait3(&statloc,WNOHANG,NULL)) > 0) {
        int exitcode = WEXITSTATUS(statloc);
        int bysignal = 0;
        listNode *ln;
        sentinelScriptJob *sj;

        // ���ͽű��ս��ź�
        if (WIFSIGNALED(statloc)) bysignal = WTERMSIG(statloc);
        sentinelEvent(REDIS_DEBUG,"-script-child",NULL,"%ld %d %d",
            (long)pid, exitcode, bysignal);
        
        // �ڶ����а� pid ���ҽű�
        ln = sentinelGetScriptListNodeByPid(pid);
        if (ln == NULL) {
            redisLog(REDIS_WARNING,"wait3() returned a pid (%ld) we can't find in our scripts execution queue!", (long)pid);
            continue;
        }
        sj = ln->value;

        /* If the script was terminated by a signal or returns an
         * exit code of "1" (that means: please retry), we reschedule it
         * if the max number of retries is not already reached. */
        if ((bysignal || exitcode == 1) &&
            sj->retry_num != SENTINEL_SCRIPT_MAX_RETRY)
        {
            // ���Խű�

            sj->flags &= ~SENTINEL_SCRIPT_RUNNING;
            sj->pid = 0;
            sj->start_time = mstime() +
                             sentinelScriptRetryDelay(sj->retry_num);
        } else {
            /* Otherwise let's remove the script, but log the event if the
             * execution did not terminated in the best of the ways. */

            // ���ͽű�ִ�д����¼�
            if (bysignal || exitcode != 0) {
                sentinelEvent(REDIS_WARNING,"-script-error",NULL,
                              "%s %d %d", sj->argv[0], bysignal, exitcode);
            }

            // ���ű��Ӷ�����ɾ��
            listDelNode(sentinel.scripts_queue,ln);
            sentinelReleaseScriptJob(sj);
            sentinel.running_scripts--;
        }
    }
}

/* Kill scripts in timeout, they'll be collected by the
 * sentinelCollectTerminatedScripts() function. */
// ɱ����ʱ�ű�����Щ�ű��ᱻ sentinelCollectTerminatedScripts �������մ���
void sentinelKillTimedoutScripts(void) {
    listNode *ln;
    listIter li;
    mstime_t now = mstime();

    // ���������е����нű�
    listRewind(sentinel.scripts_queue,&li);
    while ((ln = listNext(&li)) != NULL) {
        sentinelScriptJob *sj = ln->value;

        // ѡ����Щ����ִ�У�����ִ��ʱ�䳬�����ƵĽű�
        if (sj->flags & SENTINEL_SCRIPT_RUNNING &&
            (now - sj->start_time) > SENTINEL_SCRIPT_MAX_RUNTIME)
        {
            // ���ͽű���ʱ�¼�
            sentinelEvent(REDIS_WARNING,"-script-timeout",NULL,"%s %ld",
                sj->argv[0], (long)sj->pid);

            // ɱ���ű�����
            kill(sj->pid,SIGKILL);
        }
    }
}

/* Implements SENTINEL PENDING-SCRIPTS command. */
// ��ӡ�ű����������нű���״̬
void sentinelPendingScriptsCommand(redisClient *c) {
    listNode *ln;
    listIter li;

    addReplyMultiBulkLen(c,listLength(sentinel.scripts_queue));
    listRewind(sentinel.scripts_queue,&li);
    while ((ln = listNext(&li)) != NULL) {
        sentinelScriptJob *sj = ln->value;
        int j = 0;

        addReplyMultiBulkLen(c,10);

        addReplyBulkCString(c,"argv");
        while (sj->argv[j]) j++;
        addReplyMultiBulkLen(c,j);
        j = 0;
        while (sj->argv[j]) addReplyBulkCString(c,sj->argv[j++]);

        addReplyBulkCString(c,"flags");
        addReplyBulkCString(c,
            (sj->flags & SENTINEL_SCRIPT_RUNNING) ? "running" : "scheduled");

        addReplyBulkCString(c,"pid");
        addReplyBulkLongLong(c,sj->pid);

        if (sj->flags & SENTINEL_SCRIPT_RUNNING) {
            addReplyBulkCString(c,"run-time");
            addReplyBulkLongLong(c,mstime() - sj->start_time);
        } else {
            mstime_t delay = sj->start_time ? (sj->start_time-mstime()) : 0;
            if (delay < 0) delay = 0;
            addReplyBulkCString(c,"run-delay");
            addReplyBulkLongLong(c,delay);
        }

        addReplyBulkCString(c,"retry-num");
        addReplyBulkLongLong(c,sj->retry_num);
    }
}

/* This function calls, if any, the client reconfiguration script with the
 * following parameters:
 *
 * ���ú���ִ��ʱ��ʹ�����¸�ʽ�Ĳ������ÿͻ��������ýű�
 *
 * <master-name> <role> <state> <from-ip> <from-port> <to-ip> <to-port>
 *
 * It is called every time a failover is performed.
 *
 * ���������ÿ��ִ�й���Ǩ��ʱ����ִ��һ�Ρ�
 *
 * <state> is currently always "failover".
 * <role> is either "leader" or "observer".
 *
 * <state> ���� "failover" ���� <role> ������ "leader" ���� "observer"
 *
 * from/to fields are respectively master -> promoted slave addresses for
 * "start" and "end". 
 */
void sentinelCallClientReconfScript(sentinelRedisInstance *master, int role, char *state, sentinelAddr *from, sentinelAddr *to) {
    char fromport[32], toport[32];

    if (master->client_reconfig_script == NULL) return;
    ll2string(fromport,sizeof(fromport),from->port);
    ll2string(toport,sizeof(toport),to->port);
    // �����������ͽű��Ž����У��ȴ�ִ��
    sentinelScheduleScriptExecution(master->client_reconfig_script,
        master->name,
        (role == SENTINEL_LEADER) ? "leader" : "observer",
        state, from->ip, fromport, to->ip, toport, NULL);
}

/* ========================== sentinelRedisInstance ========================= */

/* Create a redis instance, the following fields must be populated by the
 * caller if needed:
 *
 * ����һ�� Redis ʵ����������Ҫʱ��������������Ҫ�ӵ�������ȡ��
 *
 * runid: set to NULL but will be populated once INFO output is received.
 *        ����Ϊ NULL �����ڽ��յ� INFO ����Ļظ�ʱ����
 *
 * info_refresh: is set to 0 to mean that we never received INFO so far.
 *               ������ֵΪ 0 ����ô��ʾ����δ�յ��� INFO ��Ϣ��
 *
 * If SRI_MASTER is set into initial flags the instance is added to
 * sentinel.masters table.
 *
 * ��� flags ����Ϊ SRI_MASTER ��
 * ��ô���ʵ���ᱻ��ӵ� sentinel.masters ��
 *
 * if SRI_SLAVE or SRI_SENTINEL is set then 'master' must be not NULL and the
 * instance is added into master->slaves or master->sentinels table.
 *
 * ��� flags Ϊ SRI_SLAVE ���� SRI_SENTINEL ��
 * ��ô master ��������Ϊ NULL ��
 * SRI_SLAVE ���͵�ʵ���ᱻ��ӵ� master->slaves ���У�
 * �� SRI_SENTINEL ���͵�ʵ����ᱻ��ӵ� master->sentinels ���С�
 *
 * If the instance is a slave or sentinel, the name parameter is ignored and
 * is created automatically as hostname:port.
 *
 * ���ʵ���Ǵӷ��������� sentinel ����ô name �����ᱻ�Զ����ԣ�
 * ʵ�������ֻᱻ�Զ�����Ϊ hostname:port ��
 *
 * The function fails if hostname can't be resolved or port is out of range.
 * When this happens NULL is returned and errno is set accordingly to the
 * createSentinelAddr() function.
 *
 * �� hostname ���ܱ����ͣ����߳�����Χʱ��������ʧ�ܡ�
 * ���������� NULL �������� errno ������
 * ����ĳ���ֵ��ο� createSentinelAddr() ������
 *
 * The function may also fail and return NULL with errno set to EBUSY if
 * a master or slave with the same name already exists. 
 *
 * ����ͬ���ֵ������������ߴӷ������Ѿ�����ʱ���������� NULL ��
 * ���� errno ��Ϊ EBUSY ��
 */
sentinelRedisInstance *createSentinelRedisInstance(char *name, int flags, char *hostname, int port, int quorum, sentinelRedisInstance *master) {
    sentinelRedisInstance *ri;
    sentinelAddr *addr;
    dict *table = NULL;
    char slavename[128], *sdsname;

    redisAssert(flags & (SRI_MASTER|SRI_SLAVE|SRI_SENTINEL));
    redisAssert((flags & SRI_MASTER) || master != NULL);

    /* Check address validity. */
    // ���� IP ��ַ�Ͷ˿ںŵ� addr
    addr = createSentinelAddr(hostname,port);
    if (addr == NULL) return NULL;

    /* For slaves and sentinel we use ip:port as name. */
    // ���ʵ���Ǵӷ��������� sentinel ����ôʹ�� ip:port ��ʽΪʵ����������
    if (flags & (SRI_SLAVE|SRI_SENTINEL)) {
        snprintf(slavename,sizeof(slavename),
            strchr(hostname,':') ? "[%s]:%d" : "%s:%d",
            hostname,port);
        name = slavename;
    }

    /* Make sure the entry is not duplicated. This may happen when the same
     * name for a master is used multiple times inside the configuration or
     * if we try to add multiple times a slave or sentinel with same ip/port
     * to a master. */
    // �����ļ���������ظ���������������
    // ���߳������һ����ͬ ip ���߶˿ںŵĴӷ��������� sentinel ʱ
    // �Ϳ��ܳ����ظ����ͬһ��ʵ�������
    // Ϊ�˱����������󣬳����������ʵ��֮ǰ����Ҫ�ȼ��ʵ���Ƿ��Ѵ���
    // ֻ�в����ڵ�ʵ���ᱻ���

    // ѡ��Ҫ��ӵı�
    // ע��������ᱻ��ӵ� sentinel.masters ��
    // ���ӷ������� sentinel ��ᱻ��ӵ� master ������ slaves ��� sentinels ����
    if (flags & SRI_MASTER) table = sentinel.masters;
    else if (flags & SRI_SLAVE) table = master->slaves;
    else if (flags & SRI_SENTINEL) table = master->sentinels;
    sdsname = sdsnew(name);
    if (dictFind(table,sdsname)) {

        // ʵ���Ѵ��ڣ�����ֱ�ӷ���

        sdsfree(sdsname);
        errno = EBUSY;
        return NULL;
    }

    /* Create the instance object. */
    // ����ʵ������
    ri = zmalloc(sizeof(*ri));
    /* Note that all the instances are started in the disconnected state,
     * the event loop will take care of connecting them. */
    // �������Ӷ��Ѷ���Ϊ��ʼ״̬��sentinel ������Ҫʱ�Զ�Ϊ����������
    ri->flags = flags | SRI_DISCONNECTED;
    ri->name = sdsname;
    ri->runid = NULL;
    ri->config_epoch = 0;
    ri->addr = addr;
    ri->cc = NULL;
    ri->pc = NULL;
    ri->pending_commands = 0;
    ri->cc_conn_time = 0;
    ri->pc_conn_time = 0;
    ri->pc_last_activity = 0;
    /* We set the last_ping_time to "now" even if we actually don't have yet
     * a connection with the node, nor we sent a ping.
     * This is useful to detect a timeout in case we'll not be able to connect
     * with the node at all. */
    ri->last_ping_time = mstime();
    ri->last_avail_time = mstime();
    ri->last_pong_time = mstime();
    ri->last_pub_time = mstime();
    ri->last_hello_time = mstime();
    ri->last_master_down_reply_time = mstime();
    ri->s_down_since_time = 0;
    ri->o_down_since_time = 0;
    ri->down_after_period = master ? master->down_after_period :
                            SENTINEL_DEFAULT_DOWN_AFTER;
    ri->master_link_down_time = 0;
    ri->auth_pass = NULL;
    ri->slave_priority = SENTINEL_DEFAULT_SLAVE_PRIORITY;
    ri->slave_reconf_sent_time = 0;
    ri->slave_master_host = NULL;
    ri->slave_master_port = 0;
    ri->slave_master_link_status = SENTINEL_MASTER_LINK_STATUS_DOWN;
    ri->slave_repl_offset = 0;
    ri->sentinels = dictCreate(&instancesDictType,NULL);
    ri->quorum = quorum;
    ri->parallel_syncs = SENTINEL_DEFAULT_PARALLEL_SYNCS;
    ri->master = master;
    ri->slaves = dictCreate(&instancesDictType,NULL);
    ri->info_refresh = 0;

    /* Failover state. */
    ri->leader = NULL;
    ri->leader_epoch = 0;
    ri->failover_epoch = 0;
    ri->failover_state = SENTINEL_FAILOVER_STATE_NONE;
    ri->failover_state_change_time = 0;
    ri->failover_start_time = 0;
    ri->failover_timeout = SENTINEL_DEFAULT_FAILOVER_TIMEOUT;
    ri->failover_delay_logged = 0;
    ri->promoted_slave = NULL;
    ri->notification_script = NULL;
    ri->client_reconfig_script = NULL;

    /* Role */
    ri->role_reported = ri->flags & (SRI_MASTER|SRI_SLAVE);
    ri->role_reported_time = mstime();
    ri->slave_conf_change_time = mstime();

    /* Add into the right table. */
    // ��ʵ����ӵ��ʵ��ı���
    dictAdd(table, ri->name, ri);

    // ����ʵ��
    return ri;
}

/* Release this instance and all its slaves, sentinels, hiredis connections.
 *
 * �ͷ�һ��ʵ�����Լ��������дӷ�������sentinel ���Լ� hiredis ���ӡ�
 *
 * This function does not take care of unlinking the instance from the main
 * masters table (if it is a master) or from its master sentinels/slaves table
 * if it is a slave or sentinel. 
 *
 * ������ʵ����һ���ӷ��������� sentinel ��
 * ��ô�������Ҳ��Ӹ�ʵ��������������������ɾ������ӷ�����/sentinel ��
 */
void releaseSentinelRedisInstance(sentinelRedisInstance *ri) {

    /* Release all its slaves or sentinels if any. */
    // �ͷţ������еģ�sentinel �� slave
    dictRelease(ri->sentinels);
    dictRelease(ri->slaves);

    /* Release hiredis connections. */
    // �ͷ�����
    if (ri->cc) sentinelKillLink(ri,ri->cc);
    if (ri->pc) sentinelKillLink(ri,ri->pc);

    /* Free other resources. */
    // �ͷ�������Դ
    sdsfree(ri->name);
    sdsfree(ri->runid);
    sdsfree(ri->notification_script);
    sdsfree(ri->client_reconfig_script);
    sdsfree(ri->slave_master_host);
    sdsfree(ri->leader);
    sdsfree(ri->auth_pass);
    releaseSentinelAddr(ri->addr);

    /* Clear state into the master if needed. */
    // �������ת�ƴ�����״̬
    if ((ri->flags & SRI_SLAVE) && (ri->flags & SRI_PROMOTED) && ri->master)
        ri->master->promoted_slave = NULL;

    zfree(ri);
}

/* Lookup a slave in a master Redis instance, by ip and port. */
// ���� IP �Ͷ˿ںţ�������������ʵ���Ĵӷ�����
sentinelRedisInstance *sentinelRedisInstanceLookupSlave(
                sentinelRedisInstance *ri, char *ip, int port)
{
    sds key;
    sentinelRedisInstance *slave;
  
    redisAssert(ri->flags & SRI_MASTER);
    key = sdscatprintf(sdsempty(),
        strchr(ip,':') ? "[%s]:%d" : "%s:%d",
        ip,port);
    slave = dictFetchValue(ri->slaves,key);
    sdsfree(key);
    return slave;
}

/* Return the name of the type of the instance as a string. */
// ���ַ�����ʽ����ʵ��������
const char *sentinelRedisInstanceTypeStr(sentinelRedisInstance *ri) {
    if (ri->flags & SRI_MASTER) return "master";
    else if (ri->flags & SRI_SLAVE) return "slave";
    else if (ri->flags & SRI_SENTINEL) return "sentinel";
    else return "unknown";
}

/* This function removes all the instances found in the dictionary of
 * sentinels in the specified 'master', having either:
 * 
 * 1) The same ip/port as specified.
 *    ʵ�������� IP �Ͷ˿ںź��ֵ������е�ʵ����ͬ
 *
 * 2) The same runid.
 *    ʵ������������ ID ���ֵ������е�ʵ����ͬ
 *
 * "1" and "2" don't need to verify at the same time, just one is enough.
 *
 * ����������������һ�����Ƴ������ͻᱻִ�С�
 *
 * If "runid" is NULL it is not checked.
 * Similarly if "ip" is NULL it is not checked.
 *
 * ��� runid ����Ϊ NULL ����ô�����ò�����
 * ��� ip ����Ϊ NULL ����ô�����ò�����
 *
 * This function is useful because every time we add a new Sentinel into
 * a master's Sentinels dictionary, we want to be very sure about not
 * having duplicated instances for any reason. This is important because
 * other sentinels are needed to reach ODOWN quorum, and later to get
 * voted for a given configuration epoch in order to perform the failover.
 *
 * ��Ϊ sentinel �Ĳ����������ת�ƣ���Ҫ��� sentinel ͶƱ���ܽ��С�
 * �������Ǳ��뱣֤����ӵĸ��� sentinel ���ǲ���ͬ����һ�޶��ģ�
 * ��������ȷ��ͶƱ�ĺϷ��ԡ�
 *
 * The function returns the number of Sentinels removed. 
 *
 * �����ķ���ֵΪ���Ƴ� sentinel ������
 */
int removeMatchingSentinelsFromMaster(sentinelRedisInstance *master, char *ip, int port, char *runid) {
    dictIterator *di;
    dictEntry *de;
    int removed = 0;

    di = dictGetSafeIterator(master->sentinels);
    while((de = dictNext(di)) != NULL) {
        sentinelRedisInstance *ri = dictGetVal(de);

        // ���� ID ��ͬ������ IP �Ͷ˿ں���ͬ����ô�Ƴ���ʵ��
        if ((ri->runid && runid && strcmp(ri->runid,runid) == 0) ||
            (ip && strcmp(ri->addr->ip,ip) == 0 && port == ri->addr->port))
        {
            dictDelete(master->sentinels,ri->name);
            removed++;
        }
    }
    dictReleaseIterator(di);

    return removed;
}

/* Search an instance with the same runid, ip and port into a dictionary
 * of instances. Return NULL if not found, otherwise return the instance
 * pointer.
 *
 * �ڸ�����ʵ���в��Ҿ�����ͬ runid ��ip ��port ��ʵ����
 * û�ҵ��򷵻� NULL ��
 *
 * runid or ip can be NULL. In such a case the search is performed only
 * by the non-NULL field. 
 *
 * runid ���� ip ������Ϊ NULL ������������£�����ֻ���ǿ���
 */
sentinelRedisInstance *getSentinelRedisInstanceByAddrAndRunID(dict *instances, char *ip, int port, char *runid) {
    dictIterator *di;
    dictEntry *de;
    sentinelRedisInstance *instance = NULL;

    redisAssert(ip || runid);   /* User must pass at least one search param. */

    // ������������ʵ��
    di = dictGetIterator(instances);
    while((de = dictNext(di)) != NULL) {
        sentinelRedisInstance *ri = dictGetVal(de);

        // runid ����ͬ�����Ը�ʵ��
        if (runid && !ri->runid) continue;

        // ��� ip �Ͷ˿ں��Ƿ���ͬ
        if ((runid == NULL || strcmp(ri->runid, runid) == 0) &&
            (ip == NULL || (strcmp(ri->addr->ip, ip) == 0 &&
                            ri->addr->port == port)))
        {
            instance = ri;
            break;
        }
    }
    dictReleaseIterator(di);

    return instance;
}

// �������ֲ�����������
/* Master lookup by name */
sentinelRedisInstance *sentinelGetMasterByName(char *name) {
    sentinelRedisInstance *ri;
    sds sdsname = sdsnew(name);

    ri = dictFetchValue(sentinel.masters,sdsname);
    sdsfree(sdsname);
    return ri;
}

/* Add the specified flags to all the instances in the specified dictionary. */
// Ϊ���������ʵ����ָ���� flags
void sentinelAddFlagsToDictOfRedisInstances(dict *instances, int flags) {
    dictIterator *di;
    dictEntry *de;

    di = dictGetIterator(instances);
    while((de = dictNext(di)) != NULL) {
        sentinelRedisInstance *ri = dictGetVal(de);
        ri->flags |= flags;
    }
    dictReleaseIterator(di);
}

/* Remove the specified flags to all the instances in the specified
 * dictionary. */
// ���ֵ����Ƴ�����ʵ���ĸ��� flags
void sentinelDelFlagsToDictOfRedisInstances(dict *instances, int flags) {
    dictIterator *di;
    dictEntry *de;

    // ��������ʵ��
    di = dictGetIterator(instances);
    while((de = dictNext(di)) != NULL) {
        sentinelRedisInstance *ri = dictGetVal(de);
        // �Ƴ� flags
        ri->flags &= ~flags;
    }
    dictReleaseIterator(di);
}

/* Reset the state of a monitored master:
 *
 * �������������ļ��״̬
 *
 * 1) Remove all slaves.
 *    �Ƴ��������������дӷ�����
 * 2) Remove all sentinels.
 *    �Ƴ��������������� sentinel
 * 3) Remove most of the flags resulting from runtime operations.
 *    �Ƴ��󲿷�����ʱ������־
 * 4) Reset timers to their default value.
 *    ���ü�ʱ��ΪĬ��ֵ
 * 5) In the process of doing this undo the failover if in progress.
 *    �������ת������ִ�еĻ�����ôȡ������
 * 6) Disconnect the connections with the master (will reconnect automatically).
 *    �Ͽ� sentinel ���������������ӣ�֮����Զ�������
 */

#define SENTINEL_RESET_NO_SENTINELS (1<<0)
void sentinelResetMaster(sentinelRedisInstance *ri, int flags) {

    redisAssert(ri->flags & SRI_MASTER);

    dictRelease(ri->slaves);
    ri->slaves = dictCreate(&instancesDictType,NULL);

    if (!(flags & SENTINEL_RESET_NO_SENTINELS)) {
        dictRelease(ri->sentinels);
        ri->sentinels = dictCreate(&instancesDictType,NULL);
    }

    if (ri->cc) sentinelKillLink(ri,ri->cc);

    if (ri->pc) sentinelKillLink(ri,ri->pc);

    // ���ñ�ʶΪ���ߵ���������
    ri->flags &= SRI_MASTER|SRI_DISCONNECTED;

    if (ri->leader) {
        sdsfree(ri->leader);
        ri->leader = NULL;
    }

    ri->failover_state = SENTINEL_FAILOVER_STATE_NONE;
    ri->failover_state_change_time = 0;
    ri->failover_start_time = 0;
    ri->promoted_slave = NULL;
    sdsfree(ri->runid);
    sdsfree(ri->slave_master_host);
    ri->runid = NULL;
    ri->slave_master_host = NULL;
    ri->last_ping_time = mstime();
    ri->last_avail_time = mstime();
    ri->last_pong_time = mstime();
    ri->role_reported_time = mstime();
    ri->role_reported = SRI_MASTER;
    // �����������������¼�
    if (flags & SENTINEL_GENERATE_EVENT)
        sentinelEvent(REDIS_WARNING,"+reset-master",ri,"%@");
}

/* Call sentinelResetMaster() on every master with a name matching the specified
 * pattern. */
// �������з��ϸ���ģʽ����������
int sentinelResetMastersByPattern(char *pattern, int flags) {
    dictIterator *di;
    dictEntry *de;
    int reset = 0;

    di = dictGetIterator(sentinel.masters);
    while((de = dictNext(di)) != NULL) {
        sentinelRedisInstance *ri = dictGetVal(de);

        if (ri->name) {
            if (stringmatch(pattern,ri->name,0)) {
                sentinelResetMaster(ri,flags);
                reset++;
            }
        }
    }
    dictReleaseIterator(di);
    return reset;
}

/* Reset the specified master with sentinelResetMaster(), and also change
 * the ip:port address, but take the name of the instance unmodified.
 *
 * �� master ʵ���� IP �Ͷ˿ں��޸ĳɸ����� ip �� port ��
 * ������ master ԭ�������֡�
 *
 * This is used to handle the +switch-master event.
 *
 * ����������ڴ��� +switch-master �¼�
 *
 * The function returns REDIS_ERR if the address can't be resolved for some
 * reason. Otherwise REDIS_OK is returned.  
 *
 * �������޷����͵�ַʱ���� REDIS_ERR �����򷵻� REDIS_OK ��
 */
int sentinelResetMasterAndChangeAddress(sentinelRedisInstance *master, char *ip, int port) {
    sentinelAddr *oldaddr, *newaddr;
    sentinelAddr **slaves = NULL;
    int numslaves = 0, j;
    dictIterator *di;
    dictEntry *de;

    // ���� ip �� port ������������ַ�ṹ
    newaddr = createSentinelAddr(ip,port);
    if (newaddr == NULL) return REDIS_ERR;

    /* Make a list of slaves to add back after the reset.
     * Don't include the one having the address we are switching to. */
    // ����һ������ԭ�����������дӷ�����ʵ��������
    // ���������õ�ַ֮����м��
    // ������������ԭ��������������һ���ӷ��������ĵ�ַ������������������
    di = dictGetIterator(master->slaves);
    while((de = dictNext(di)) != NULL) {
        sentinelRedisInstance *slave = dictGetVal(de);

        // ��������������
        if (sentinelAddrIsEqual(slave->addr,newaddr)) continue;

        // ���ӷ��������浽������
        slaves = zrealloc(slaves,sizeof(sentinelAddr*)*(numslaves+1));
        slaves[numslaves++] = createSentinelAddr(slave->addr->ip,
                                                 slave->addr->port);
    }
    dictReleaseIterator(di);
    
    /* If we are switching to a different address, include the old address
     * as a slave as well, so that we'll be able to sense / reconfigure
     * the old master. */
    // ����µ�ַ�� master �ĵ�ַ����ͬ��
    // �� master �ĵ�ַҲ��Ϊ�ӷ�������ַ��ӵ����������дӷ�������ַ��������
    // ���������ڽ�����������������Ϊ�����������Ĵӷ�������
    if (!sentinelAddrIsEqual(newaddr,master->addr)) {
        slaves = zrealloc(slaves,sizeof(sentinelAddr*)*(numslaves+1));
        slaves[numslaves++] = createSentinelAddr(master->addr->ip,
                                                 master->addr->port);
    }

    /* Reset and switch address. */
    // ���� master ʵ���ṹ
    sentinelResetMaster(master,SENTINEL_RESET_NO_SENTINELS);
    oldaddr = master->addr;
    // Ϊ master ʵ�������µĵ�ַ
    master->addr = newaddr;
    master->o_down_since_time = 0;
    master->s_down_since_time = 0;

    /* Add slaves back. */
    // Ϊʵ���ӻ�֮ǰ��������дӷ�����
    for (j = 0; j < numslaves; j++) {
        sentinelRedisInstance *slave;

        slave = createSentinelRedisInstance(NULL,SRI_SLAVE,slaves[j]->ip,
                    slaves[j]->port, master->quorum, master);

        releaseSentinelAddr(slaves[j]);

        if (slave) {
            sentinelEvent(REDIS_NOTICE,"+slave",slave,"%@");
            sentinelFlushConfig();
        }
    }
    zfree(slaves);

    /* Release the old address at the end so we are safe even if the function
     * gets the master->addr->ip and master->addr->port as arguments. */
    // �ͷžɵ�ַ
    releaseSentinelAddr(oldaddr);
    sentinelFlushConfig();
    return REDIS_OK;
}

/* Return non-zero if there was no SDOWN or ODOWN error associated to this
 * instance in the latest 'ms' milliseconds. */
// ���ʵ���ڸ��� ms ��û�г��ֹ� SDOWN ���� ODOWN ״̬
// ��ô��������һ������ֵ
int sentinelRedisInstanceNoDownFor(sentinelRedisInstance *ri, mstime_t ms) {
    mstime_t most_recent;

    most_recent = ri->s_down_since_time;
    if (ri->o_down_since_time > most_recent)
        most_recent = ri->o_down_since_time;
    return most_recent == 0 || (mstime() - most_recent) > ms;
}

/* Return the current master address, that is, its address or the address
 * of the promoted slave if already operational. */
// ���ص�ǰ���������ĵ�ַ
// ��� Sentinel ���ڶ������������й���Ǩ�ƣ���ô���������������ĵ�ַ
sentinelAddr *sentinelGetCurrentMasterAddress(sentinelRedisInstance *master) {
    /* If we are failing over the master, and the state is already
     * SENTINEL_FAILOVER_STATE_RECONF_SLAVES or greater, it means that we
     * already have the new configuration epoch in the master, and the
     * slave acknowledged the configuration switch. Advertise the new
     * address. */
    if ((master->flags & SRI_FAILOVER_IN_PROGRESS) &&
        master->promoted_slave &&
        master->failover_state >= SENTINEL_FAILOVER_STATE_RECONF_SLAVES)
    {
        return master->promoted_slave->addr;
    } else {
        return master->addr;
    }
}

/* This function sets the down_after_period field value in 'master' to all
 * the slaves and sentinel instances connected to this master. */
void sentinelPropagateDownAfterPeriod(sentinelRedisInstance *master) {
    dictIterator *di;
    dictEntry *de;
    int j;
    dict *d[] = {master->slaves, master->sentinels, NULL};

    for (j = 0; d[j]; j++) {
        di = dictGetIterator(d[j]);
        while((de = dictNext(di)) != NULL) {
            sentinelRedisInstance *ri = dictGetVal(de);
            ri->down_after_period = master->down_after_period;
        }
        dictReleaseIterator(di);
    }
}

/* ============================ Config handling ============================= */

// Sentinel �����ļ������� //sentinel�����ļ����ؼ�sentinelHandleConfiguration�����������ļ����ؼ�loadServerConfigFromStringsentinel�������sentinelHandleConfiguration
char *sentinelHandleConfiguration(char **argv, int argc) {
    sentinelRedisInstance *ri;

    // SENTINEL monitor ѡ��
    if (!strcasecmp(argv[0],"monitor") && argc == 5) {
        /* monitor <name> <host> <port> <quorum> */
        /*
        sentinel  monitor  master  127.0.0.1 6379 2
    ��ô������ǰSentinel���ڣ�ֻҪ�ܹ�������Sentinel��Ϊ���������Ѿ���������״̬����ô��ǰSentinel�ͽ����������ж�Ϊ�͹�����
         */
        // ���� quorum ����
        int quorum = atoi(argv[4]);

        // ��� quorum ����������� 0
        if (quorum <= 0) return "Quorum must be 1 or greater.";

        // ������������ʵ��
        if (createSentinelRedisInstance(argv[1],SRI_MASTER,argv[2],
                                        atoi(argv[3]),quorum,NULL) == NULL)
        {
            switch(errno) {
            case EBUSY: return "Duplicated master name.";
            case ENOENT: return "Can't resolve master instance hostname.";
            case EINVAL: return "Invalid port number";
            }
        }

    // SENTINEL down-after-milliseconds ѡ��
    } else if (!strcasecmp(argv[0],"down-after-milliseconds") && argc == 3) {

        /* down-after-milliseconds <name> <milliseconds> */

        // ������������
        ri = sentinelGetMasterByName(argv[1]);
        if (!ri) return "No such master with specified name.";

        // ����ѡ��
        ri->down_after_period = atoi(argv[2]);
        if (ri->down_after_period <= 0)
            return "negative or zero time parameter.";

        sentinelPropagateDownAfterPeriod(ri);

    // SENTINEL failover-timeout ѡ��
    } else if (!strcasecmp(argv[0],"failover-timeout") && argc == 3) {

        /* failover-timeout <name> <milliseconds> */

        // ������������
        ri = sentinelGetMasterByName(argv[1]);
        if (!ri) return "No such master with specified name.";

        // ����ѡ��
        ri->failover_timeout = atoi(argv[2]);
        if (ri->failover_timeout <= 0)
            return "negative or zero time parameter.";

   // Sentinel parallel-syncs ѡ��
   } else if (!strcasecmp(argv[0],"parallel-syncs") && argc == 3) {

        /* parallel-syncs <name> <milliseconds> */

        // ������������
        ri = sentinelGetMasterByName(argv[1]);
        if (!ri) return "No such master with specified name.";

        // ����ѡ��
        ri->parallel_syncs = atoi(argv[2]);

    // SENTINEL notification-script ѡ��
   } else if (!strcasecmp(argv[0],"notification-script") && argc == 3) {

        /* notification-script <name> <path> */
        
        // ������������
        ri = sentinelGetMasterByName(argv[1]);
        if (!ri) return "No such master with specified name.";

        // ������·����ָ����ļ��Ƿ���ڣ��Լ��Ƿ��ִ��
        if (access(argv[2],X_OK) == -1)
            return "Notification script seems non existing or non executable.";

        // ����ѡ��
        ri->notification_script = sdsnew(argv[2]);

    // SENTINEL client-reconfig-script ѡ��
   } else if (!strcasecmp(argv[0],"client-reconfig-script") && argc == 3) {

        /* client-reconfig-script <name> <path> */

        // ������������
        ri = sentinelGetMasterByName(argv[1]);
        if (!ri) return "No such master with specified name.";
        // ������·����ָ����ļ��Ƿ���ڣ��Լ��Ƿ��ִ��
        if (access(argv[2],X_OK) == -1)
            return "Client reconfiguration script seems non existing or "
                   "non executable.";

        // ����ѡ��
        ri->client_reconfig_script = sdsnew(argv[2]);

    // ���� SENTINEL auth-pass ѡ��
   } else if (!strcasecmp(argv[0],"auth-pass") && argc == 3) {

        /* auth-pass <name> <password> */

        // ������������
        ri = sentinelGetMasterByName(argv[1]);
        if (!ri) return "No such master with specified name.";

        // ����ѡ��
        ri->auth_pass = sdsnew(argv[2]);

    } else if (!strcasecmp(argv[0],"current-epoch") && argc == 2) {
        /* current-epoch <epoch> */
        unsigned long long current_epoch = strtoull(argv[1],NULL,10);
        if (current_epoch > sentinel.current_epoch)
            sentinel.current_epoch = current_epoch;

    // SENTINEL config-epoch ѡ��
    } else if (!strcasecmp(argv[0],"config-epoch") && argc == 3) {

        /* config-epoch <name> <epoch> */

        ri = sentinelGetMasterByName(argv[1]);
        if (!ri) return "No such master with specified name.";

        ri->config_epoch = strtoull(argv[2],NULL,10);
        /* The following update of current_epoch is not really useful as
         * now the current epoch is persisted on the config file, but
         * we leave this check here for redundancy. */
        if (ri->config_epoch > sentinel.current_epoch)
            sentinel.current_epoch = ri->config_epoch;

    } else if (!strcasecmp(argv[0],"leader-epoch") && argc == 3) {
        /* leader-epoch <name> <epoch> */
        ri = sentinelGetMasterByName(argv[1]);
        if (!ri) return "No such master with specified name.";
        ri->leader_epoch = strtoull(argv[2],NULL,10);

    // SENTINEL known-slave ѡ��
    } else if (!strcasecmp(argv[0],"known-slave") && argc == 4) {
        sentinelRedisInstance *slave;

        /* known-slave <name> <ip> <port> */

        ri = sentinelGetMasterByName(argv[1]);
        if (!ri) return "No such master with specified name.";
        if ((slave = createSentinelRedisInstance(NULL,SRI_SLAVE,argv[2],
                    atoi(argv[3]), ri->quorum, ri)) == NULL)
        {
            return "Wrong hostname or port for slave.";
        }

    // SENTINEL known-sentinel ѡ��
    } else if (!strcasecmp(argv[0],"known-sentinel") &&
               (argc == 4 || argc == 5)) {
        sentinelRedisInstance *si;

        /* known-sentinel <name> <ip> <port> [runid] */

        ri = sentinelGetMasterByName(argv[1]);
        if (!ri) return "No such master with specified name.";
        if ((si = createSentinelRedisInstance(NULL,SRI_SENTINEL,argv[2],
                    atoi(argv[3]), ri->quorum, ri)) == NULL)
        {
            return "Wrong hostname or port for sentinel.";
        }
        if (argc == 5) si->runid = sdsnew(argv[4]);

    } else {
        return "Unrecognized sentinel configuration statement.";
    }
    return NULL;
}

/* Implements CONFIG REWRITE for "sentinel" option.
 * This is used not just to rewrite the configuration given by the user
 * (the configured masters) but also in order to retain the state of
 * Sentinel across restarts: config epoch of masters, associated slaves
 * and sentinel instances, and so forth. */
// CONFIG REWIRTE �����к� sentinel ѡ���йصĲ���
// ����������������û�ִ�� CONFIG REWRITE ��ʱ��
// Ҳ���ڱ��� Sentinel ״̬���Ա� Sentinel ����ʱ����״̬ʹ��
void rewriteConfigSentinelOption(struct rewriteConfigState *state) {
    dictIterator *di, *di2;
    dictEntry *de;
    sds line;

    /* For every master emit a "sentinel monitor" config entry. */
    di = dictGetIterator(sentinel.masters);
    while((de = dictNext(di)) != NULL) {
        sentinelRedisInstance *master, *ri;
        sentinelAddr *master_addr;

        /* sentinel monitor */
        master = dictGetVal(de);
        master_addr = sentinelGetCurrentMasterAddress(master);
        line = sdscatprintf(sdsempty(),"sentinel monitor %s %s %d %d",
            master->name, master_addr->ip, master_addr->port,
            master->quorum);
        rewriteConfigRewriteLine(state,"sentinel",line,1);

        /* sentinel down-after-milliseconds */
        if (master->down_after_period != SENTINEL_DEFAULT_DOWN_AFTER) {
            line = sdscatprintf(sdsempty(),
                "sentinel down-after-milliseconds %s %ld",
                master->name, (long) master->down_after_period);
            rewriteConfigRewriteLine(state,"sentinel",line,1);
        }

        /* sentinel failover-timeout */
        if (master->failover_timeout != SENTINEL_DEFAULT_FAILOVER_TIMEOUT) {
            line = sdscatprintf(sdsempty(),
                "sentinel failover-timeout %s %ld",
                master->name, (long) master->failover_timeout);
            rewriteConfigRewriteLine(state,"sentinel",line,1);
        }

        /* sentinel parallel-syncs */
        if (master->parallel_syncs != SENTINEL_DEFAULT_PARALLEL_SYNCS) {
            line = sdscatprintf(sdsempty(),
                "sentinel parallel-syncs %s %d",
                master->name, master->parallel_syncs);
            rewriteConfigRewriteLine(state,"sentinel",line,1);
        }

        /* sentinel notification-script */
        if (master->notification_script) {
            line = sdscatprintf(sdsempty(),
                "sentinel notification-script %s %s",
                master->name, master->notification_script);
            rewriteConfigRewriteLine(state,"sentinel",line,1);
        }

        /* sentinel client-reconfig-script */
        if (master->client_reconfig_script) {
            line = sdscatprintf(sdsempty(),
                "sentinel client-reconfig-script %s %s",
                master->name, master->client_reconfig_script);
            rewriteConfigRewriteLine(state,"sentinel",line,1);
        }

        /* sentinel auth-pass */
        if (master->auth_pass) {
            line = sdscatprintf(sdsempty(),
                "sentinel auth-pass %s %s",
                master->name, master->auth_pass);
            rewriteConfigRewriteLine(state,"sentinel",line,1);
        }

        /* sentinel config-epoch */
        line = sdscatprintf(sdsempty(),
            "sentinel config-epoch %s %llu",
            master->name, (unsigned long long) master->config_epoch);
        rewriteConfigRewriteLine(state,"sentinel",line,1);

        /* sentinel leader-epoch */
        line = sdscatprintf(sdsempty(),
            "sentinel leader-epoch %s %llu",
            master->name, (unsigned long long) master->leader_epoch);
        rewriteConfigRewriteLine(state,"sentinel",line,1);

        /* sentinel known-slave */
        di2 = dictGetIterator(master->slaves);
        while((de = dictNext(di2)) != NULL) {
            sentinelAddr *slave_addr;

            ri = dictGetVal(de);
            slave_addr = ri->addr;

            /* If master_addr (obtained using sentinelGetCurrentMasterAddress()
             * so it may be the address of the promoted slave) is equal to this
             * slave's address, a failover is in progress and the slave was
             * already successfully promoted. So as the address of this slave
             * we use the old master address instead. */
            if (sentinelAddrIsEqual(slave_addr,master_addr))
                slave_addr = master->addr;
            line = sdscatprintf(sdsempty(),
                "sentinel known-slave %s %s %d",
                master->name, ri->addr->ip, ri->addr->port);
            rewriteConfigRewriteLine(state,"sentinel",line,1);
        }
        dictReleaseIterator(di2);

        /* sentinel known-sentinel */
        di2 = dictGetIterator(master->sentinels);
        while((de = dictNext(di2)) != NULL) {
            ri = dictGetVal(de);
            line = sdscatprintf(sdsempty(),
                "sentinel known-sentinel %s %s %d%s%s",
                master->name, ri->addr->ip, ri->addr->port,
                ri->runid ? " " : "",
                ri->runid ? ri->runid : "");
            rewriteConfigRewriteLine(state,"sentinel",line,1);
        }
        dictReleaseIterator(di2);
    }

    /* sentinel current-epoch is a global state valid for all the masters. */
    line = sdscatprintf(sdsempty(),
        "sentinel current-epoch %llu", (unsigned long long) sentinel.current_epoch);
    rewriteConfigRewriteLine(state,"sentinel",line,1);

    dictReleaseIterator(di);
}

/* This function uses the config rewriting Redis engine in order to persist
 * the state of the Sentinel in the current configuration file.
 *
 * ʹ�� CONFIG REWRITE ���ܣ�����ǰ Sentinel ��״̬�־û��������ļ����档
 *
 * Before returning the function calls fsync() against the generated
 * configuration file to make sure changes are committed to disk.
 *
 * �ں�������֮ǰ����������һ�� fsync() ��ȷ���ļ��Ѿ������浽�������档
 *
 * On failure the function logs a warning on the Redis log. 
 *
 * �������ʧ�ܣ���ô��ӡһ��������־��
 */
void sentinelFlushConfig(void) {
    int fd = -1;
    int saved_hz = server.hz;
    int rewrite_status;

    server.hz = REDIS_DEFAULT_HZ;
    rewrite_status = rewriteConfig(server.configfile);
    server.hz = saved_hz;

    if (rewrite_status == -1) goto werr;
    if ((fd = open(server.configfile,O_RDONLY)) == -1) goto werr;
    if (fsync(fd) == -1) goto werr;
    if (close(fd) == EOF) goto werr;
    return;

werr:
    if (fd != -1) close(fd);
    redisLog(REDIS_WARNING,"WARNING: Sentinel was not able to save the new configuration on disk!!!: %s", strerror(errno));
}

/* ====================== hiredis connection handling ======================= */

/* Completely disconnect a hiredis link from an instance. */
// �Ͽ�ʵ��������
void sentinelKillLink(sentinelRedisInstance *ri, redisAsyncContext *c) {
    if (ri->cc == c) {
        ri->cc = NULL;
        ri->pending_commands = 0;
    }
    if (ri->pc == c) ri->pc = NULL;
    c->data = NULL;

    // �򿪶��߱�־
    ri->flags |= SRI_DISCONNECTED;

    // �Ͽ�����
    redisAsyncFree(c);
}

/* This function takes a hiredis context that is in an error condition
 * and make sure to mark the instance as disconnected performing the
 * cleanup needed.
 *
 * ������һ����������������ȷ�Ķ��߱�־����ִ���������
 *
 * Note: we don't free the hiredis context as hiredis will do it for us
 * for async connections. 
 *
 * �������û���ֶ��ͷ����ӣ���Ϊ�첽���ӻ��Զ��ͷ�
 */
void sentinelDisconnectInstanceFromContext(const redisAsyncContext *c) {
    sentinelRedisInstance *ri = c->data;
    int pubsub;

    if (ri == NULL) return; /* The instance no longer exists. */

    // ���Ͷ����¼�
    pubsub = (ri->pc == c);
    sentinelEvent(REDIS_DEBUG, pubsub ? "-pubsub-link" : "-cmd-link", ri,
        "%@ #%s", c->errstr);

    if (pubsub)
        ri->pc = NULL;
    else
        ri->cc = NULL;

    // �򿪱�־
    ri->flags |= SRI_DISCONNECTED;
}

// �첽���ӵ����ӻص�����
void sentinelLinkEstablishedCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        sentinelDisconnectInstanceFromContext(c);
    } else {
        sentinelRedisInstance *ri = c->data;
        int pubsub = (ri->pc == c);

        // ���������¼�
        sentinelEvent(REDIS_DEBUG, pubsub ? "+pubsub-link" : "+cmd-link", ri,
            "%@");
    }
}

// �첽���ӵĶ��߻ص�����
void sentinelDisconnectCallback(const redisAsyncContext *c, int status) {
    sentinelDisconnectInstanceFromContext(c);
}

/* Send the AUTH command with the specified master password if needed.
 * Note that for slaves the password set for the master is used.
 *
 * ��� sentinel ������ auth-pass ѡ���ô�������������ߴӷ�����������֤���롣
 * ע��ӷ�����ʹ�õ����������������롣
 *
 * We don't check at all if the command was successfully transmitted
 * to the instance as if it fails Sentinel will detect the instance down,
 * will disconnect and reconnect the link and so forth. 
 *
 * ��������������Ƿ񱻳ɹ����ͣ���Ϊ���Ŀ������������˵Ļ��� sentinel ��ʶ�𵽣�
 * ���������������ӣ�Ȼ�������·��� AUTH ���
 */
void sentinelSendAuthIfNeeded(sentinelRedisInstance *ri, redisAsyncContext *c) {

    // ��� ri ��������������ôʹ��ʵ���Լ�������
    // ��� ri �Ǵӷ���������ôʹ����������������
    char *auth_pass = (ri->flags & SRI_MASTER) ? ri->auth_pass :
                                                 ri->master->auth_pass;

    // ���� AUTH ����
    if (auth_pass) {
        if (redisAsyncCommand(c, sentinelDiscardReplyCallback, NULL, "AUTH %s",
            auth_pass) == REDIS_OK) ri->pending_commands++;
    }
}

/* Use CLIENT SETNAME to name the connection in the Redis instance as
 * sentinel-<first_8_chars_of_runid>-<connection_type>
 * The connection type is "cmd" or "pubsub" as specified by 'type'.
 *
 * This makes it possible to list all the sentinel instances connected
 * to a Redis servewr with CLIENT LIST, grepping for a specific name format. */
// ʹ�� CLIENT SETNAME ���Ϊ�����Ŀͻ����������֡�
void sentinelSetClientName(sentinelRedisInstance *ri, redisAsyncContext *c, char *type) {
    char name[64];

    snprintf(name,sizeof(name),"sentinel-%.8s-%s",server.runid,type);
    if (redisAsyncCommand(c, sentinelDiscardReplyCallback, NULL,
        "CLIENT SETNAME %s", name) == REDIS_OK)
    {
        ri->pending_commands++;
    }
}

/* Create the async connections for the specified instance if the instance
 * is disconnected. Note that the SRI_DISCONNECTED flag is set even if just
 * one of the two links (commands and pub/sub) is missing. */
// ��� sentinel ��ʵ�����ڶ��ߣ�δ���ӣ�״̬����ô��������ʵ�����첽���ӡ�
void sentinelReconnectInstance(sentinelRedisInstance *ri) {

    // ʾ��δ���ߣ������ӣ�������
    if (!(ri->flags & SRI_DISCONNECTED)) return;

    /* Commands connection. */
    // ������ʵ������һ�����ڷ��� Redis ���������
    if (ri->cc == NULL) {

        // ����ʵ��
        ri->cc = redisAsyncConnect(ri->addr->ip,ri->addr->port);

        // ���ӳ���
        if (ri->cc->err) {
            sentinelEvent(REDIS_DEBUG,"-cmd-link-reconnection",ri,"%@ #%s",
                ri->cc->errstr);
            sentinelKillLink(ri,ri->cc);

        // ���ӳɹ�
        } else {
            // ������������
            ri->cc_conn_time = mstime();
            ri->cc->data = ri;
            redisAeAttach(server.el,ri->cc);
            // �������� callback
            redisAsyncSetConnectCallback(ri->cc,
                                            sentinelLinkEstablishedCallback);
            // ���ö��� callback
            redisAsyncSetDisconnectCallback(ri->cc,
                                            sentinelDisconnectCallback);
            // ���� AUTH �����֤���
            sentinelSendAuthIfNeeded(ri,ri->cc);
            sentinelSetClientName(ri,ri->cc,"cmd");

            /* Send a PING ASAP when reconnecting. */
            sentinelSendPing(ri);
        }
    }

    /* Pub / Sub */
    // �����������ʹӷ�����������һ�����ڶ���Ƶ��������
    if ((ri->flags & (SRI_MASTER|SRI_SLAVE)) && ri->pc == NULL) {

        // ����ʵ��
        ri->pc = redisAsyncConnect(ri->addr->ip,ri->addr->port);

        // ���ӳ���
        if (ri->pc->err) {
            sentinelEvent(REDIS_DEBUG,"-pubsub-link-reconnection",ri,"%@ #%s",
                ri->pc->errstr);
            sentinelKillLink(ri,ri->pc);

        // ���ӳɹ�
        } else {
            int retval;

            // ������������
            ri->pc_conn_time = mstime();
            ri->pc->data = ri;
            redisAeAttach(server.el,ri->pc);
            // �������� callback
            redisAsyncSetConnectCallback(ri->pc,
                                            sentinelLinkEstablishedCallback);
            // ���ö��� callback
            redisAsyncSetDisconnectCallback(ri->pc,
                                            sentinelDisconnectCallback);
            // ���� AUTH �����֤���
            sentinelSendAuthIfNeeded(ri,ri->pc);

            // Ϊ�ͻ����������� "pubsub"
            sentinelSetClientName(ri,ri->pc,"pubsub");

            /* Now we subscribe to the Sentinels "Hello" channel. */
            // ���� SUBSCRIBE __sentinel__:hello �������Ƶ��
            retval = redisAsyncCommand(ri->pc,
                sentinelReceiveHelloMessages, NULL, "SUBSCRIBE %s",
                    SENTINEL_HELLO_CHANNEL);
            
            // ���ĳ����Ͽ�����
            if (retval != REDIS_OK) {
                /* If we can't subscribe, the Pub/Sub connection is useless
                 * and we can simply disconnect it and try again. */
                sentinelKillLink(ri,ri->pc);
                return;
            }
        }
    }

    /* Clear the DISCONNECTED flags only if we have both the connections
     * (or just the commands connection if this is a sentinel instance). */
    // ���ʵ���������������ߴӷ���������ô�� cc �� pc �������Ӷ������ɹ�ʱ���ر� DISCONNECTED ��ʶ
    // ���ʵ���� Sentinel ����ô�� cc ���Ӵ����ɹ�ʱ���ر� DISCONNECTED ��ʶ
    if (ri->cc && (ri->flags & SRI_SENTINEL || ri->pc))
        ri->flags &= ~SRI_DISCONNECTED;
}

/* ======================== Redis instances pinging  ======================== */

/* Return true if master looks "sane", that is:
 *
 * ���������������ȥ�Ǻ���sane������ô�����档�ж��Ƿ������������£�
 *
 * 1) It is actually a master in the current configuration.
 *    ���ڵ�ǰ�����еĽ�ɫΪ��������
 * 2) It reports itself as a master.
 *    �������Լ���һ����������
 * 3) It is not SDOWN or ODOWN.
 *    ����������������� SDOWN ���� ODOWN ״̬
 * 4) We obtained last INFO no more than two times the INFO period time ago. 
 *    �����������һ��ˢ�� INFO ��Ϣ�������ڲ����� SENTINEL_INFO_PERIOD ������ʱ��
 */
int sentinelMasterLooksSane(sentinelRedisInstance *master) {
    return
        master->flags & SRI_MASTER &&
        master->role_reported == SRI_MASTER &&
        (master->flags & (SRI_S_DOWN|SRI_O_DOWN)) == 0 &&
        (mstime() - master->info_refresh) < SENTINEL_INFO_PERIOD*2;
}

/* Process the INFO output from masters. */
// �������������ߴӷ����������ص� INFO ����Ļظ��з��������Ϣ
// �������Ӣ��ע�ʹ��ˣ�������������������������� INFO �ظ���������ӷ������� INFO �ظ���
void sentinelRefreshInstanceInfo(sentinelRedisInstance *ri, const char *info) {
    sds *lines;
    int numlines, j;
    int role = 0;

    /* The following fields must be reset to a given value in the case they
     * are not found at all in the INFO output. */
    // ���ñ�������Ϊ 0 ������ INFO �ظ����޸�ֵ�����
    ri->master_link_down_time = 0;

    /* Process line by line. */
    // �� INFO ����Ļظ��������з���
    lines = sdssplitlen(info,strlen(info),"\r\n",2,&numlines);
    for (j = 0; j < numlines; j++) {
        sentinelRedisInstance *slave;
        sds l = lines[j];

        /* run_id:<40 hex chars>*/
        // ��ȡ������ runid
        if (sdslen(l) >= 47 && !memcmp(l,"run_id:",7)) {

            // ������ runid
            if (ri->runid == NULL) {
                ri->runid = sdsnewlen(l+7,40);
            } else {
                // RUNID ��ͬ��˵��������������
                if (strncmp(ri->runid,l+7,40) != 0) {
                    sentinelEvent(REDIS_NOTICE,"+reboot",ri,"%@");

                    // �ͷž� ID �������� ID
                    sdsfree(ri->runid);
                    ri->runid = sdsnewlen(l+7,40);
                }
            }
        }

        // ��ȡ�ӷ������� ip �Ͷ˿ں�
        /* old versions: slave0:<ip>,<port>,<state>
         * new versions: slave0:ip=127.0.0.1,port=9999,... */
        if ((ri->flags & SRI_MASTER) &&
            sdslen(l) >= 7 &&
            !memcmp(l,"slave",5) && isdigit(l[5]))
        {
            char *ip, *port, *end;

            if (strstr(l,"ip=") == NULL) {
                /* Old format. */
                ip = strchr(l,':'); if (!ip) continue;
                ip++; /* Now ip points to start of ip address. */
                port = strchr(ip,','); if (!port) continue;
                *port = '\0'; /* nul term for easy access. */
                port++; /* Now port points to start of port number. */
                end = strchr(port,','); if (!end) continue;
                *end = '\0'; /* nul term for easy access. */
            } else {
                /* New format. */
                ip = strstr(l,"ip="); if (!ip) continue;
                ip += 3; /* Now ip points to start of ip address. */
                port = strstr(l,"port="); if (!port) continue;
                port += 5; /* Now port points to start of port number. */
                /* Nul term both fields for easy access. */
                end = strchr(ip,','); if (end) *end = '\0';
                end = strchr(port,','); if (end) *end = '\0';
            }

            /* Check if we already have this slave into our table,
             * otherwise add it. */
            // ����������µĴӷ��������֣���ôΪ�����ʵ��
            if (sentinelRedisInstanceLookupSlave(ri,ip,atoi(port)) == NULL) {
                if ((slave = createSentinelRedisInstance(NULL,SRI_SLAVE,ip,
                            atoi(port), ri->quorum, ri)) != NULL)
                {
                    sentinelEvent(REDIS_NOTICE,"+slave",slave,"%@");
                }
            }
        }

        /* master_link_down_since_seconds:<seconds> */
        // ��ȡ���ӷ������Ķ���ʱ��
        // ���ֻ����ʵ���Ǵӷ������������������ӶϿ�������³���
        if (sdslen(l) >= 32 &&
            !memcmp(l,"master_link_down_since_seconds",30))
        {
            ri->master_link_down_time = strtoll(l+31,NULL,10)*1000;
        }

        /* role:<role> */
        // ��ȡʵ���Ľ�ɫ
        if (!memcmp(l,"role:master",11)) role = SRI_MASTER;
        else if (!memcmp(l,"role:slave",10)) role = SRI_SLAVE;

        // ����ӷ�����
        if (role == SRI_SLAVE) {

            /* master_host:<host> */
            // �������������� IP
            if (sdslen(l) >= 12 && !memcmp(l,"master_host:",12)) {
                if (ri->slave_master_host == NULL ||
                    strcasecmp(l+12,ri->slave_master_host))
                {
                    sdsfree(ri->slave_master_host);
                    ri->slave_master_host = sdsnew(l+12);
                    ri->slave_conf_change_time = mstime();
                }
            }

            /* master_port:<port> */
            // �������������Ķ˿ں�
            if (sdslen(l) >= 12 && !memcmp(l,"master_port:",12)) {
                int slave_master_port = atoi(l+12);

                if (ri->slave_master_port != slave_master_port) {
                    ri->slave_master_port = slave_master_port;
                    ri->slave_conf_change_time = mstime();
                }
            }
            
            /* master_link_status:<status> */
            // ��������������״̬
            if (sdslen(l) >= 19 && !memcmp(l,"master_link_status:",19)) {
                ri->slave_master_link_status =
                    (strcasecmp(l+19,"up") == 0) ?
                    SENTINEL_MASTER_LINK_STATUS_UP :
                    SENTINEL_MASTER_LINK_STATUS_DOWN;
            }

            /* slave_priority:<priority> */
            // ����ӷ����������ȼ�
            if (sdslen(l) >= 15 && !memcmp(l,"slave_priority:",15))
                ri->slave_priority = atoi(l+15);

            /* slave_repl_offset:<offset> */
            // ����ӷ������ĸ���ƫ����
            if (sdslen(l) >= 18 && !memcmp(l,"slave_repl_offset:",18))
                ri->slave_repl_offset = strtoull(l+18,NULL,10);
        }
    }

    // ����ˢ�� INFO ����ظ���ʱ��
    ri->info_refresh = mstime();
    sdsfreesplitres(lines,numlines);

    /* ---------------------------- Acting half -----------------------------
     * Some things will not happen if sentinel.tilt is true, but some will
     * still be processed. 
     *
     * ��� sentinel ������ TILT ģʽ����ô����ֻ��һ���ֶ����ᱻִ��
     */

    /* Remember when the role changed. */
    if (role != ri->role_reported) {
        ri->role_reported_time = mstime();
        ri->role_reported = role;
        if (role == SRI_SLAVE) ri->slave_conf_change_time = mstime();
        /* Log the event with +role-change if the new role is coherent or
         * with -role-change if there is a mismatch with the current config. */
        sentinelEvent(REDIS_VERBOSE,
            ((ri->flags & (SRI_MASTER|SRI_SLAVE)) == role) ?
            "+role-change" : "-role-change",
            ri, "%@ new reported role is %s",
            role == SRI_MASTER ? "master" : "slave",
            ri->flags & SRI_MASTER ? "master" : "slave");
    }

    /* None of the following conditions are processed when in tilt mode, so
     * return asap. */
    // ��� Sentinel ������ TILT ģʽ����ô������ִ�����µ���䡣
    if (sentinel.tilt) return;

    /* Handle master -> slave role switch. */
    // ʵ���� Sentinel ��ʶΪ���������������� INFO ����Ļظ�
    // ���ʵ�������Ϊ�ӷ�����
    if ((ri->flags & SRI_MASTER) && role == SRI_SLAVE) {
        /* Nothing to do, but masters claiming to be slaves are
         * considered to be unreachable by Sentinel, so eventually
         * a failover will be triggered. */
        // ���һ������������Ϊ�ӷ���������ô Sentinel ������������������ǲ����õ�
    }

    /* Handle slave -> master role switch. */
    // ����ӷ�����ת��Ϊ�������������
    if ((ri->flags & SRI_SLAVE) && role == SRI_MASTER) {
        /* If this is a promoted slave we can change state to the
         * failover state machine. */

        // ������Ǳ�ѡ������Ϊ�����������Ĵӷ�����
        // ��ô������صĹ���ת������
        if ((ri->master->flags & SRI_FAILOVER_IN_PROGRESS) &&
            (ri->master->failover_state ==
                SENTINEL_FAILOVER_STATE_WAIT_PROMOTION))
        {
            /* Now that we are sure the slave was reconfigured as a master
             * set the master configuration epoch to the epoch we won the
             * election to perform this failover. This will force the other
             * Sentinels to update their config (assuming there is not
             * a newer one already available). */
            // ����һ���� Sentinel ���� SLAVEOF no one ֮���ɴӷ�������Ϊ����������ʵ��
            // ��������������������ü�Ԫ����Ϊ Sentinel Ӯ����ͷѡ�ٵļ�Ԫ
            // ��һ������ǿ������ Sentinel ���������Լ�������
            // ������û��һ�����µļ�Ԫ���ڵĻ���
            // ���´ӷ����������������������ߣ������ü�Ԫ
            ri->master->config_epoch = ri->master->failover_epoch;
            // ���ôӷ����������������������ߣ��Ĺ���ת��״̬
            // ���״̬���ôӷ�������ʼͬ���µ���������
            ri->master->failover_state = SENTINEL_FAILOVER_STATE_RECONF_SLAVES;
            // ���´ӷ����������������������ߣ��Ĺ���ת��״̬���ʱ��
            ri->master->failover_state_change_time = mstime();
            // ����ǰ Sentinel ״̬���浽�����ļ�����
            sentinelFlushConfig();
            // �����¼�
            sentinelEvent(REDIS_WARNING,"+promoted-slave",ri,"%@");
            sentinelEvent(REDIS_WARNING,"+failover-state-reconf-slaves",
                ri->master,"%@");
            // ִ�нű�
            sentinelCallClientReconfScript(ri->master,SENTINEL_LEADER,
                "start",ri->master->addr,ri->addr);

        // ���ʵ���ɴӷ�������Ϊ����������������û�н��� TILT ģʽ
        // ����������Ϊ������ɵģ�����֮ǰ�����������������������ˣ�
        } else {
            /* A slave turned into a master. We want to force our view and
             * reconfigure as slave. Wait some time after the change before
             * going forward, to receive new configs if any. */
            // ���һ���ӷ�������Ϊ��������������ô���ǻῼ�ǽ������һ���ӷ�����

            // �� PUBLISH ����ķ���ʱ����� 4 ������һ������ʱ��
            mstime_t wait_time = SENTINEL_PUBLISH_PERIOD*4;

            // ������ʵ��������������������
            // ����ʵ����һ��ʱ����û�н���� SDOWN ״̬���� ODOWN ״̬
            // ����ʵ��������������������ʱ���Ѿ����� wait_time
            if (sentinelMasterLooksSane(ri->master) &&
               sentinelRedisInstanceNoDownFor(ri,wait_time) &&
               mstime() - ri->role_reported_time > wait_time)
            {
                // ���½�ʵ������Ϊ�ӷ�����
                int retval = sentinelSendSlaveOf(ri,
                        ri->master->addr->ip,
                        ri->master->addr->port);
                
                // �����¼�
                if (retval == REDIS_OK)
                    sentinelEvent(REDIS_NOTICE,"+convert-to-slave",ri,"%@");
            }
        }
    }

    /* Handle slaves replicating to a different master address. */
    // �ôӷ��������¸��ƻ���ȷ����������
    if ((ri->flags & SRI_SLAVE) &&
        role == SRI_SLAVE &&
        // �ӷ��������ڵ�����������ַ�� Sentinel �������Ϣ��һ��
        (ri->slave_master_port != ri->master->addr->port ||
         strcasecmp(ri->slave_master_host,ri->master->addr->ip)))
    {
        mstime_t wait_time = ri->master->failover_timeout;

        /* Make sure the master is sane before reconfiguring this instance
         * into a slave. */
        // 1) ���ʵ������������״̬�Ƿ�����
        // 2) ���ʵ���ڸ���ʱ�����Ƿ����� SDOWN ���� ODOWN ״̬
        // 3) ���ʵ����ݱ����ʱ���Ƿ��Ѿ�������ָ��ʱ��
        // ����ǵĻ���ִ�д��롣����
        if (sentinelMasterLooksSane(ri->master) &&
            sentinelRedisInstanceNoDownFor(ri,wait_time) &&
            mstime() - ri->slave_conf_change_time > wait_time)
        {
            // ���½�ʵ��ָ��ԭ������������
            int retval = sentinelSendSlaveOf(ri,
                    ri->master->addr->ip,
                    ri->master->addr->port);

            if (retval == REDIS_OK)
                sentinelEvent(REDIS_NOTICE,"+fix-slave-config",ri,"%@");
        }
    }

    /* Detect if the slave that is in the process of being reconfigured
     * changed state. */
    // Sentinel ���ӵ�ʵ��Ϊ�ӷ������������Ѿ��������� SLAVEOF ����
    if ((ri->flags & SRI_SLAVE) && role == SRI_SLAVE &&
        (ri->flags & (SRI_RECONF_SENT|SRI_RECONF_INPROG)))
    {
        /* SRI_RECONF_SENT -> SRI_RECONF_INPROG. */
        // �� SENT ״̬��Ϊ INPROG ״̬����ʾͬ�����ڽ���
        if ((ri->flags & SRI_RECONF_SENT) &&
            ri->slave_master_host &&
            strcmp(ri->slave_master_host,
                    ri->master->promoted_slave->addr->ip) == 0 &&
            ri->slave_master_port == ri->master->promoted_slave->addr->port)
        {
            ri->flags &= ~SRI_RECONF_SENT;
            ri->flags |= SRI_RECONF_INPROG;
            sentinelEvent(REDIS_NOTICE,"+slave-reconf-inprog",ri,"%@");
        }

        /* SRI_RECONF_INPROG -> SRI_RECONF_DONE */
        // �� INPROG ״̬��Ϊ DONE ״̬����ʾͬ�������
        if ((ri->flags & SRI_RECONF_INPROG) &&
            ri->slave_master_link_status == SENTINEL_MASTER_LINK_STATUS_UP)
        {
            ri->flags &= ~SRI_RECONF_INPROG;
            ri->flags |= SRI_RECONF_DONE;
            sentinelEvent(REDIS_NOTICE,"+slave-reconf-done",ri,"%@");
        }
    }
}

// ���� INFO ����Ļظ�
void sentinelInfoReplyCallback(redisAsyncContext *c, void *reply, void *privdata) {
    sentinelRedisInstance *ri = c->data;
    redisReply *r;

    if (ri) ri->pending_commands--;
    if (!reply || !ri) return;
    r = reply;

    if (r->type == REDIS_REPLY_STRING) {
        sentinelRefreshInstanceInfo(ri,r->str);
    }
}

/* Just discard the reply. We use this when we are not monitoring the return
 * value of the command but its effects directly. */
// ����ص��������ڴ�����Ҫ���ظ������ֻʹ������ĸ����ã�
void sentinelDiscardReplyCallback(redisAsyncContext *c, void *reply, void *privdata) {
    sentinelRedisInstance *ri = c->data;

    if (ri) ri->pending_commands--;
}

// ���� PING ����Ļظ�
void sentinelPingReplyCallback(redisAsyncContext *c, void *reply, void *privdata) {
    sentinelRedisInstance *ri = c->data;
    redisReply *r;

    if (ri) ri->pending_commands--;
    if (!reply || !ri) return;
    r = reply;

    if (r->type == REDIS_REPLY_STATUS ||
        r->type == REDIS_REPLY_ERROR) {

        /* Update the "instance available" field only if this is an
         * acceptable reply. */
        // ֻ��ʵ������ acceptable �ظ�ʱ���� last_avail_time
        if (strncmp(r->str,"PONG",4) == 0 ||
            strncmp(r->str,"LOADING",7) == 0 ||
            strncmp(r->str,"MASTERDOWN",10) == 0)
        {
            // ʵ����������
            ri->last_avail_time = mstime();
            ri->last_ping_time = 0; /* Flag the pong as received. */
        } else {

            // ʵ������������

            /* Send a SCRIPT KILL command if the instance appears to be
             * down because of a busy script. */
            // �����������Ϊִ�нű������� BUSY ״̬��
            // ��ô����ͨ������ SCRIPT KILL ���ָ�������
            if (strncmp(r->str,"BUSY",4) == 0 &&
                (ri->flags & SRI_S_DOWN) &&
                !(ri->flags & SRI_SCRIPT_KILL_SENT))
            {
                if (redisAsyncCommand(ri->cc,
                        sentinelDiscardReplyCallback, NULL,
                        "SCRIPT KILL") == REDIS_OK)
                    ri->pending_commands++;
                ri->flags |= SRI_SCRIPT_KILL_SENT;
            }
        }
    }

    // ����ʵ�����һ�λظ� PING �����ʱ��
    ri->last_pong_time = mstime();
}

/* This is called when we get the reply about the PUBLISH command we send
 * to the master to advertise this sentinel. */
// ���� PUBLISH ����Ļظ�
void sentinelPublishReplyCallback(redisAsyncContext *c, void *reply, void *privdata) {
    sentinelRedisInstance *ri = c->data;
    redisReply *r;

    if (ri) ri->pending_commands--;
    if (!reply || !ri) return;
    r = reply;

    /* Only update pub_time if we actually published our message. Otherwise
     * we'll retry against in 100 milliseconds. */
    // �������ͳɹ�����ô���� last_pub_time
    if (r->type != REDIS_REPLY_ERROR)
        ri->last_pub_time = mstime();
}

/* Process an hello message received via Pub/Sub in master or slave instance,
 * or sent directly to this sentinel via the (fake) PUBLISH command of Sentinel.
 *
 * ����� Pub/Sub ���ӵ����ģ����������������ߴӷ������� hello ��Ϣ��
 * hello ��ϢҲ��������һ�� Sentinel ͨ�� PUBLISH ����͹����ġ�
 *
 * If the master name specified in the message is not known, the message is
 * discareded. 
 *
 * �����Ϣ����ָ��������������������δ֪�ģ���ô������Ϣ����������
 */
void sentinelProcessHelloMessage(char *hello, int hello_len) {
    /* Format is composed of 8 tokens:
     * 0=ip,1=port,2=runid,3=current_epoch,4=master_name,
     * 5=master_ip,6=master_port,7=master_config_epoch. */
    int numtokens, port, removed, master_port;
    uint64_t current_epoch, master_config_epoch;
    char **token = sdssplitlen(hello, hello_len, ",", 1, &numtokens);
    sentinelRedisInstance *si, *master;

    if (numtokens == 8) {
        /* Obtain a reference to the master this hello message is about */
        // ��ȡ�������������֣���������δ֪����������ص���Ϣ��
        master = sentinelGetMasterByName(token[4]);
        if (!master) goto cleanup; /* Unknown master, skip the message. */

        /* First, try to see if we already have this sentinel. */
        // ����� Sentinel �Ƿ��Ѿ���ʶ������Ϣ�� Sentinel
        port = atoi(token[1]);
        master_port = atoi(token[6]);
        si = getSentinelRedisInstanceByAddrAndRunID(
                        master->sentinels,token[0],port,token[2]);
        current_epoch = strtoull(token[3],NULL,10);
        master_config_epoch = strtoull(token[7],NULL,10);

        if (!si) {

            // ��� Sentinel ����ʶ������Ϣ�� Sentinel 
            // ���Է����뵽 Sentinel �б���

            /* If not, remove all the sentinels that have the same runid
             * OR the same ip/port, because it's either a restart or a
             * network topology change. */
            removed = removeMatchingSentinelsFromMaster(master,token[0],port,
                            token[2]);
            if (removed) {
                sentinelEvent(REDIS_NOTICE,"-dup-sentinel",master,
                    "%@ #duplicate of %s:%d or %s",
                    token[0],port,token[2]);
            }

            /* Add the new sentinel. */
            si = createSentinelRedisInstance(NULL,SRI_SENTINEL,
                            token[0],port,master->quorum,master);
            if (si) {
                sentinelEvent(REDIS_NOTICE,"+sentinel",si,"%@");
                /* The runid is NULL after a new instance creation and
                 * for Sentinels we don't have a later chance to fill it,
                 * so do it now. */
                si->runid = sdsnew(token[2]);
                sentinelFlushConfig();
            }
        }

        /* Update local current_epoch if received current_epoch is greater.*/
        // �����Ϣ�м�¼�ļ�Ԫ�� Sentinel ��ǰ�ļ�ԪҪ�ߣ���ô���¼�Ԫ
        if (current_epoch > sentinel.current_epoch) {
            sentinel.current_epoch = current_epoch;
            sentinelFlushConfig();
            sentinelEvent(REDIS_WARNING,"+new-epoch",master,"%llu",
                (unsigned long long) sentinel.current_epoch);
        }

        /* Update master info if received configuration is newer. */
        // �����Ϣ�м�¼��������Ϣ���£���ô��������������Ϣ���и���
        if (master->config_epoch < master_config_epoch) {
            master->config_epoch = master_config_epoch;
            if (master_port != master->addr->port ||
                strcmp(master->addr->ip, token[5]))
            {
                sentinelAddr *old_addr;

                sentinelEvent(REDIS_WARNING,"+config-update-from",si,"%@");
                sentinelEvent(REDIS_WARNING,"+switch-master",
                    master,"%s %s %d %s %d",
                    master->name,
                    master->addr->ip, master->addr->port,
                    token[5], master_port);

                old_addr = dupSentinelAddr(master->addr);
                sentinelResetMasterAndChangeAddress(master, token[5], master_port);
                sentinelCallClientReconfScript(master,
                    SENTINEL_OBSERVER,"start",
                    old_addr,master->addr);
                releaseSentinelAddr(old_addr);
            }
        }

        /* Update the state of the Sentinel. */
        // �����ҷ� Sentinel ��¼�ĶԷ� Sentinel ����Ϣ��
        if (si) si->last_hello_time = mstime();
    }

cleanup:
    sdsfreesplitres(token,numtokens);
}


/* This is our Pub/Sub callback for the Hello channel. It's useful in order
 * to discover other sentinels attached at the same master. */
// �˻ص��������ڴ��� Hello Ƶ���ķ���ֵ�������Է����������ڶ���ͬһ���������� Sentinel
void sentinelReceiveHelloMessages(redisAsyncContext *c, void *reply, void *privdata) {
    sentinelRedisInstance *ri = c->data;
    redisReply *r;

    if (!reply || !ri) return;
    r = reply;

    /* Update the last activity in the pubsub channel. Note that since we
     * receive our messages as well this timestamp can be used to detect
     * if the link is probably disconnected even if it seems otherwise. */
    // �������һ�ν���Ƶ�������ʱ��
    ri->pc_last_activity = mstime();
   
    /* Sanity check in the reply we expect, so that the code that follows
     * can avoid to check for details. */
    // ֻ����Ƶ����������Ϣ����������ʱ���˶�ʱ��������Ϣ
    if (r->type != REDIS_REPLY_ARRAY ||
        r->elements != 3 ||
        r->element[0]->type != REDIS_REPLY_STRING ||
        r->element[1]->type != REDIS_REPLY_STRING ||
        r->element[2]->type != REDIS_REPLY_STRING ||
        strcmp(r->element[0]->str,"message") != 0) return;

    /* We are not interested in meeting ourselves */
    // ֻ������Լ����͵���Ϣ
    if (strstr(r->element[2]->str,server.runid) != NULL) return;

    sentinelProcessHelloMessage(r->element[2]->str, r->element[2]->len);
}

/* Send an "Hello" message via Pub/Sub to the specified 'ri' Redis
 * instance in order to broadcast the current configuraiton for this
 * master, and to advertise the existence of this Sentinel at the same time.
 *
 * ����� ri ʵ����Ƶ��������Ϣ��
 * �Ӷ��������ڸ����������������ã�
 * �������� Sentinel ���汾 Sentinel �Ĵ��ڡ�
 *
 * The message has the following format:
 *
 * ������Ϣ�ĸ�ʽ���£� 
 *
 * sentinel_ip,sentinel_port,sentinel_runid,current_epoch,
 * master_name,master_ip,master_port,master_config_epoch.
 *
 * Sentinel IP,Sentinel �˿ں�,Sentinel ������ ID,Sentinel ��ǰ�ļ�Ԫ,
 * ��������������,���������� IP,���������Ķ˿ں�,�������������ü�Ԫ.
 *
 * Returns REDIS_OK if the PUBLISH was queued correctly, otherwise
 * REDIS_ERR is returned. 
 *
 * PUBLISH ����ɹ����ʱ���� REDIS_OK ��
 * ���򷵻� REDIS_ERR ��
 */
int sentinelSendHello(sentinelRedisInstance *ri) {
    char ip[REDIS_IP_STR_LEN];
    char payload[REDIS_IP_STR_LEN+1024];
    int retval;

    // ���ʵ����������������ôʹ�ô�ʵ������Ϣ
    // ���ʵ���Ǵӷ���������ôʹ������ӷ�������������������Ϣ
    sentinelRedisInstance *master = (ri->flags & SRI_MASTER) ? ri : ri->master;

    // ��ȡ��ַ��Ϣ
    sentinelAddr *master_addr = sentinelGetCurrentMasterAddress(master);

    /* Try to obtain our own IP address. */
    // ��ȡʵ������ĵ�ַ
    if (anetSockName(ri->cc->c.fd,ip,sizeof(ip),NULL) == -1) return REDIS_ERR;
    if (ri->flags & SRI_DISCONNECTED) return REDIS_ERR;

    /* Format and send the Hello message. */
    // ��ʽ����Ϣ
    snprintf(payload,sizeof(payload),
        "%s,%d,%s,%llu," /* Info about this sentinel. */
        "%s,%s,%d,%llu", /* Info about current master. */
        ip, server.port, server.runid,
        (unsigned long long) sentinel.current_epoch,
        /* --- */
        master->name,master_addr->ip,master_addr->port,
        (unsigned long long) master->config_epoch);
    
    // ������Ϣ
    retval = redisAsyncCommand(ri->cc,
        sentinelPublishReplyCallback, NULL, "PUBLISH %s %s",
            SENTINEL_HELLO_CHANNEL,payload);

    if (retval != REDIS_OK) return REDIS_ERR;

    ri->pending_commands++;

    return REDIS_OK;
}

/* Send a PING to the specified instance and refresh the last_ping_time
 * if it is zero (that is, if we received a pong for the previous ping).
 *
 * On error zero is returned, and we can't consider the PING command
 * queued in the connection. */
// ��ָ���� Sentinel ���� PING ���
int sentinelSendPing(sentinelRedisInstance *ri) {
    int retval = redisAsyncCommand(ri->cc,
        sentinelPingReplyCallback, NULL, "PING");
    if (retval == REDIS_OK) {
        ri->pending_commands++;
        /* We update the ping time only if we received the pong for
         * the previous ping, otherwise we are technically waiting
         * since the first ping that did not received a reply. */
        if (ri->last_ping_time == 0) ri->last_ping_time = mstime();
        return 1;
    } else {
        return 0;
    }
}

// ����ʱ���ʵ�����͵��������ʵ������������� INFO ��PING �� PUBLISH
// ��Ȼ���������ְ��� Ping ���������ֻ���� PING ����
/* Send periodic PING, INFO, and PUBLISH to the Hello channel to
 * the specified master or slave instance. */
void sentinelSendPeriodicCommands(sentinelRedisInstance *ri) {
    mstime_t now = mstime();
    mstime_t info_period, ping_period;
    int retval;

    /* Return ASAP if we have already a PING or INFO already pending, or
     * in the case the instance is not properly connected. */
    // ������������������δ����ʱִ��
    if (ri->flags & SRI_DISCONNECTED) return;

    /* For INFO, PING, PUBLISH that are not critical commands to send we
     * also have a limit of SENTINEL_MAX_PENDING_COMMANDS. We don't
     * want to use a lot of memory just because a link is not working
     * properly (note that anyway there is a redundant protection about this,
     * that is, the link will be disconnected and reconnected if a long
     * timeout condition is detected. */
    // Ϊ�˱��� sentinel ��ʵ�����ڲ�����״̬ʱ�����͹�������
    // sentinel ֻ�ڴ��������������δ���� SENTINEL_MAX_PENDING_COMMANDS ����ʱ
    // �Ž��������
    if (ri->pending_commands >= SENTINEL_MAX_PENDING_COMMANDS) return;

    /* If this is a slave of a master in O_DOWN condition we start sending
     * it INFO every second, instead of the usual SENTINEL_INFO_PERIOD
     * period. In this state we want to closely monitor slaves in case they
     * are turned into masters by another Sentinel, or by the sysadmin. */
    // ���ڴӷ�������˵�� sentinel Ĭ��ÿ SENTINEL_INFO_PERIOD ����������һ�� INFO ����
    // ���ǣ����ӷ������������������� SDOWN ״̬����������ִ�й���ת��ʱ
    // Ϊ�˸����ٵز�׽�ӷ������ı䶯�� sentinel �Ὣ���� INFO �����Ƶ�ʸ�Ϊÿ��һ��
    if ((ri->flags & SRI_SLAVE) &&
        (ri->master->flags & (SRI_O_DOWN|SRI_FAILOVER_IN_PROGRESS))) {
        info_period = 1000;
    } else {
        info_period = SENTINEL_INFO_PERIOD;
    }

    /* We ping instances every time the last received pong is older than
     * the configured 'down-after-milliseconds' time, but every second
     * anyway if 'down-after-milliseconds' is greater than 1 second. */
    ping_period = ri->down_after_period;
    if (ping_period > SENTINEL_PING_PERIOD) ping_period = SENTINEL_PING_PERIOD;

    // ʵ������ Sentinel �������������ߴӷ�������
    // ������������������һ��������
    // 1��SENTINEL δ�յ�������������� INFO ����ظ�
    // 2��������һ�θ�ʵ���ظ� INFO �����Ѿ����� info_period ���
    // ��ô��ʵ������ INFO ����
    if ((ri->flags & SRI_SENTINEL) == 0 &&
        (ri->info_refresh == 0 ||
        (now - ri->info_refresh) > info_period))
    {
        /* Send INFO to masters and slaves, not sentinels. */
        retval = redisAsyncCommand(ri->cc,
            sentinelInfoReplyCallback, NULL, "INFO");
        if (retval == REDIS_OK) ri->pending_commands++;
    } else if ((now - ri->last_pong_time) > ping_period) {
        /* Send PING to all the three kinds of instances. */
        sentinelSendPing(ri);
    } else if ((now - ri->last_pub_time) > SENTINEL_PUBLISH_PERIOD) {
        /* PUBLISH hello messages to all the three kinds of instances. */
        sentinelSendHello(ri);
    }
}

/* =========================== SENTINEL command ============================= */

// �����ַ�����ʾ�Ĺ���ת��״̬
const char *sentinelFailoverStateStr(int state) {
    switch(state) {
    case SENTINEL_FAILOVER_STATE_NONE: return "none";
    case SENTINEL_FAILOVER_STATE_WAIT_START: return "wait_start";
    case SENTINEL_FAILOVER_STATE_SELECT_SLAVE: return "select_slave";
    case SENTINEL_FAILOVER_STATE_SEND_SLAVEOF_NOONE: return "send_slaveof_noone";
    case SENTINEL_FAILOVER_STATE_WAIT_PROMOTION: return "wait_promotion";
    case SENTINEL_FAILOVER_STATE_RECONF_SLAVES: return "reconf_slaves";
    case SENTINEL_FAILOVER_STATE_UPDATE_CONFIG: return "update_config";
    default: return "unknown";
    }
}

/* Redis instance to Redis protocol representation. */
// �� Redis Э�����ʽ���� Redis ʵ�������
void addReplySentinelRedisInstance(redisClient *c, sentinelRedisInstance *ri) {
    char *flags = sdsempty();
    void *mbl;
    int fields = 0;

    mbl = addDeferredMultiBulkLength(c);

    addReplyBulkCString(c,"name");
    addReplyBulkCString(c,ri->name);
    fields++;

    addReplyBulkCString(c,"ip");
    addReplyBulkCString(c,ri->addr->ip);
    fields++;

    addReplyBulkCString(c,"port");
    addReplyBulkLongLong(c,ri->addr->port);
    fields++;

    addReplyBulkCString(c,"runid");
    addReplyBulkCString(c,ri->runid ? ri->runid : "");
    fields++;

    addReplyBulkCString(c,"flags");
    if (ri->flags & SRI_S_DOWN) flags = sdscat(flags,"s_down,");
    if (ri->flags & SRI_O_DOWN) flags = sdscat(flags,"o_down,");
    if (ri->flags & SRI_MASTER) flags = sdscat(flags,"master,");
    if (ri->flags & SRI_SLAVE) flags = sdscat(flags,"slave,");
    if (ri->flags & SRI_SENTINEL) flags = sdscat(flags,"sentinel,");
    if (ri->flags & SRI_DISCONNECTED) flags = sdscat(flags,"disconnected,");
    if (ri->flags & SRI_MASTER_DOWN) flags = sdscat(flags,"master_down,");
    if (ri->flags & SRI_FAILOVER_IN_PROGRESS)
        flags = sdscat(flags,"failover_in_progress,");
    if (ri->flags & SRI_PROMOTED) flags = sdscat(flags,"promoted,");
    if (ri->flags & SRI_RECONF_SENT) flags = sdscat(flags,"reconf_sent,");
    if (ri->flags & SRI_RECONF_INPROG) flags = sdscat(flags,"reconf_inprog,");
    if (ri->flags & SRI_RECONF_DONE) flags = sdscat(flags,"reconf_done,");

    if (sdslen(flags) != 0) sdsrange(flags,0,-2); /* remove last "," */
    addReplyBulkCString(c,flags);
    sdsfree(flags);
    fields++;

    addReplyBulkCString(c,"pending-commands");
    addReplyBulkLongLong(c,ri->pending_commands);
    fields++;

    if (ri->flags & SRI_FAILOVER_IN_PROGRESS) {
        addReplyBulkCString(c,"failover-state");
        addReplyBulkCString(c,(char*)sentinelFailoverStateStr(ri->failover_state));
        fields++;
    }

    addReplyBulkCString(c,"last-ping-sent");
    addReplyBulkLongLong(c,
        ri->last_ping_time ? (mstime() - ri->last_ping_time) : 0);
    fields++;

    addReplyBulkCString(c,"last-ok-ping-reply");
    addReplyBulkLongLong(c,mstime() - ri->last_avail_time);
    fields++;

    addReplyBulkCString(c,"last-ping-reply");
    addReplyBulkLongLong(c,mstime() - ri->last_pong_time);
    fields++;

    if (ri->flags & SRI_S_DOWN) {
        addReplyBulkCString(c,"s-down-time");
        addReplyBulkLongLong(c,mstime()-ri->s_down_since_time);
        fields++;
    }

    if (ri->flags & SRI_O_DOWN) {
        addReplyBulkCString(c,"o-down-time");
        addReplyBulkLongLong(c,mstime()-ri->o_down_since_time);
        fields++;
    }

    addReplyBulkCString(c,"down-after-milliseconds");
    addReplyBulkLongLong(c,ri->down_after_period);
    fields++;

    /* Masters and Slaves */
    if (ri->flags & (SRI_MASTER|SRI_SLAVE)) {
        addReplyBulkCString(c,"info-refresh");
        addReplyBulkLongLong(c,mstime() - ri->info_refresh);
        fields++;

        addReplyBulkCString(c,"role-reported");
        addReplyBulkCString(c, (ri->role_reported == SRI_MASTER) ? "master" :
                                                                   "slave");
        fields++;

        addReplyBulkCString(c,"role-reported-time");
        addReplyBulkLongLong(c,mstime() - ri->role_reported_time);
        fields++;
    }

    /* Only masters */
    if (ri->flags & SRI_MASTER) {
        addReplyBulkCString(c,"config-epoch");
        addReplyBulkLongLong(c,ri->config_epoch);
        fields++;

        addReplyBulkCString(c,"num-slaves");
        addReplyBulkLongLong(c,dictSize(ri->slaves));
        fields++;

        addReplyBulkCString(c,"num-other-sentinels");
        addReplyBulkLongLong(c,dictSize(ri->sentinels));
        fields++;

        addReplyBulkCString(c,"quorum");
        addReplyBulkLongLong(c,ri->quorum);
        fields++;

        addReplyBulkCString(c,"failover-timeout");
        addReplyBulkLongLong(c,ri->failover_timeout);
        fields++;

        addReplyBulkCString(c,"parallel-syncs");
        addReplyBulkLongLong(c,ri->parallel_syncs);
        fields++;

        if (ri->notification_script) {
            addReplyBulkCString(c,"notification-script");
            addReplyBulkCString(c,ri->notification_script);
            fields++;
        }

        if (ri->client_reconfig_script) {
            addReplyBulkCString(c,"client-reconfig-script");
            addReplyBulkCString(c,ri->client_reconfig_script);
            fields++;
        }
    }

    /* Only slaves */
    if (ri->flags & SRI_SLAVE) {
        addReplyBulkCString(c,"master-link-down-time");
        addReplyBulkLongLong(c,ri->master_link_down_time);
        fields++;

        addReplyBulkCString(c,"master-link-status");
        addReplyBulkCString(c,
            (ri->slave_master_link_status == SENTINEL_MASTER_LINK_STATUS_UP) ?
            "ok" : "err");
        fields++;

        addReplyBulkCString(c,"master-host");
        addReplyBulkCString(c,
            ri->slave_master_host ? ri->slave_master_host : "?");
        fields++;

        addReplyBulkCString(c,"master-port");
        addReplyBulkLongLong(c,ri->slave_master_port);
        fields++;

        addReplyBulkCString(c,"slave-priority");
        addReplyBulkLongLong(c,ri->slave_priority);
        fields++;

        addReplyBulkCString(c,"slave-repl-offset");
        addReplyBulkLongLong(c,ri->slave_repl_offset);
        fields++;
    }

    /* Only sentinels */
    if (ri->flags & SRI_SENTINEL) {
        addReplyBulkCString(c,"last-hello-message");
        addReplyBulkLongLong(c,mstime() - ri->last_hello_time);
        fields++;

        addReplyBulkCString(c,"voted-leader");
        addReplyBulkCString(c,ri->leader ? ri->leader : "?");
        fields++;

        addReplyBulkCString(c,"voted-leader-epoch");
        addReplyBulkLongLong(c,ri->leader_epoch);
        fields++;
    }

    setDeferredMultiBulkLength(c,mbl,fields*2);
}

/* Output a number of instances contained inside a dictionary as
 * Redis protocol. */
// ��ӡ����ʵ�������
void addReplyDictOfRedisInstances(redisClient *c, dict *instances) {
    dictIterator *di;
    dictEntry *de;

    di = dictGetIterator(instances);
    addReplyMultiBulkLen(c,dictSize(instances));
    while((de = dictNext(di)) != NULL) {
        sentinelRedisInstance *ri = dictGetVal(de);

        addReplySentinelRedisInstance(c,ri);
    }
    dictReleaseIterator(di);
}

/* Lookup the named master into sentinel.masters.
 * If the master is not found reply to the client with an error and returns
 * NULL. */
// �� sentinel.masters �ֵ��в��Ҹ������ֵ� master
// û�ҵ��򷵻� NULL
sentinelRedisInstance *sentinelGetMasterByNameOrReplyError(redisClient *c,
                        robj *name)
{
    sentinelRedisInstance *ri;

    ri = dictFetchValue(sentinel.masters,c->argv[2]->ptr);
    if (!ri) {
        addReplyError(c,"No such master with that name");
        return NULL;
    }
    return ri;
}

// SENTINEL �����ʵ��
void sentinelCommand(redisClient *c) {
    if (!strcasecmp(c->argv[1]->ptr,"masters")) {
        /* SENTINEL MASTERS */
        if (c->argc != 2) goto numargserr;
        addReplyDictOfRedisInstances(c,sentinel.masters);
    } else if (!strcasecmp(c->argv[1]->ptr,"master")) {
        /* SENTINEL MASTER <name> */
        sentinelRedisInstance *ri;

        if (c->argc != 3) goto numargserr;
        if ((ri = sentinelGetMasterByNameOrReplyError(c,c->argv[2]))
            == NULL) return;
        addReplySentinelRedisInstance(c,ri);
    } else if (!strcasecmp(c->argv[1]->ptr,"slaves")) {
        /* SENTINEL SLAVES <master-name> */
        sentinelRedisInstance *ri;

        if (c->argc != 3) goto numargserr;
        if ((ri = sentinelGetMasterByNameOrReplyError(c,c->argv[2])) == NULL)
            return;
        addReplyDictOfRedisInstances(c,ri->slaves);
    } else if (!strcasecmp(c->argv[1]->ptr,"sentinels")) {
        /* SENTINEL SENTINELS <master-name> */
        sentinelRedisInstance *ri;

        if (c->argc != 3) goto numargserr;
        if ((ri = sentinelGetMasterByNameOrReplyError(c,c->argv[2])) == NULL)
            return;
        addReplyDictOfRedisInstances(c,ri->sentinels);
    } else if (!strcasecmp(c->argv[1]->ptr,"is-master-down-by-addr")) {
    /*
    SENTINEL is-master-down-by-addr <ip> <port><current_epoch> <runid>
����ѯ������Sentinel�Ƿ�ͬ���������������ߣ������еĸ������������������ʾ��
�����������������ש�����������������������������������������������������������������������������������
��    ��  ��    ��    ��  ��                                                                        ��
�ǩ��������������贈����������������������������������������������������������������������������������
��    ip        ��    ��Sentinel�ж�Ϊ�������ߵ��������������ַ                                    ��
�ǩ��������������贈����������������������������������������������������������������������������������
��    port      ��    ��Sentinel�ж�Ϊ�������ߵ����������϶˿ں�                                    ��
�ǩ��������������贈����������������������������������������������������������������������������������
��current_epoch ��    Sentinel��ǰ�����ü�Ԫ������ѡ����ͷSentinel                                  ��
�ǩ��������������贈����������������������������������������������������������������������������������
��              ��    �����Ƿ���*����Sentinel������ID:*���Ŵ�������������ڼ�����������Ŀ͹�����   ��
��    runid     ��                                                                                  ��
��              ��״̬����Sentinel������ID������ѡ����ͷSentinel����ϸ���ý����¡���˵��            ��
�����������������ߩ�����������������������������������������������������������������������������������

    ��һ��Sentinel��Ŀ��Sentinel�����յ���һ��Sentinel��ԴSentinel��������SENTINEL is-master-down-by����ʱ��Ŀ��Sentinel�����
��ȡ�����������а����ĸ������������������е���������IP�Ͷ˿ںţ�������������Ƿ������ߣ�Ȼ����ԴSentinel����һ
    ����������������Multi Bulk�ظ���ΪSENTINEL is-master-down-by����Ļظ���
        1) <down��state>
        2)<leader��runid>
        3)<leader_epoch>
    �� SENTINEL is-master-down-by-addr�ظ�������
    �������������������ש�������������������������������������������������������������������������������������
    ��    ��  ��      ��    ��  ��                                                                          ��
    �ǩ����������������贈������������������������������������������������������������������������������������
    ��    down state  ��    ����Ŀ��sentinel�����������ļ������l�����������������ߣ�0������������δ����  ��
    �ǩ����������������贈������������������������������������������������������������������������������������
    ��leader runid    ��    ������*���Ż���Ŀ��Sentinel�ľֲ���ͷSentinel������ID:*���Ŵ��������������     ��
    ��                �������������������״̬�����ֲ���ͷSentincl������ID������ѡ����ͷSentincl            ��
    �ǩ����������������贈������������������������������������������������������������������������������������
    ��leader_epoch    ��    Ŀ��Sentinel�ľֲ���ͷSentinel�����ü�Ԫ������ѡ����ͷSentinel����ϸ���ý�����  ��
    ��                ��һ��˵��������leader runid��ֵ��Ϊ*ʱ��Ч�����leader runid��ֵΪ*����ô            ��
    ��                ��leader_epoch��Ϊ0                                                                   ��
    �������������������ߩ�������������������������������������������������������������������������������������
        �ٸ����ӣ����һ��Sentinel�������»ظ���ΪSENTINEL is-master-down-by-addr����Ļظ���
        1)  1
        2)  *
        3)  0
        ��ô˵��SentinelҲͬ���������������ߡ�

   ��������Sentinel���ص�SENTINEL is-master-down-by-addr����ظ���Sentinel��ͳ������Sentinelͬ���������������ߵ�����������һ����
�ﵽ����ָ�����ж��ݹ��������������ʱ��Sentinel�Ὣ��������ʵ���ṹflags���Ե�SRI_O_DOWN��ʶ�򿪣���ʾ���������Ѿ�����͹�����״̬

    sentinel  monitor  master  127.0.0.1 6379 2
��ô������ǰSentinel���ڣ�ֻҪ�ܹ�������Sentinel��Ϊ���������Ѿ���������״̬����ô��ǰSentinel�ͽ����������ж�Ϊ�͹�����
    */   /* SENTINEL IS-MASTER-DOWN-BY-ADDR <ip> <port> <current-epoch> <runid>*/
        sentinelRedisInstance *ri;
        long long req_epoch;
        uint64_t leader_epoch = 0;
        char *leader = NULL;
        long port;
        int isdown = 0;

        if (c->argc != 6) goto numargserr;
        if (getLongFromObjectOrReply(c,c->argv[3],&port,NULL) != REDIS_OK ||
            getLongLongFromObjectOrReply(c,c->argv[4],&req_epoch,NULL)
                                                              != REDIS_OK)
            return;
        ri = getSentinelRedisInstanceByAddrAndRunID(sentinel.masters,
            c->argv[2]->ptr,port,NULL);

        /* It exists? Is actually a master? Is subjectively down? It's down.
         * Note: if we are in tilt mode we always reply with "0". */
        if (!sentinel.tilt && ri && (ri->flags & SRI_S_DOWN) &&
                                    (ri->flags & SRI_MASTER))
            isdown = 1;

        /* Vote for the master (or fetch the previous vote) if the request
         * includes a runid, otherwise the sender is not seeking for a vote. */
        if (ri && ri->flags & SRI_MASTER && strcasecmp(c->argv[5]->ptr,"*")) {
            leader = sentinelVoteLeader(ri,(uint64_t)req_epoch,
                                            c->argv[5]->ptr,
                                            &leader_epoch);
        }

        /* Reply with a three-elements multi-bulk reply:
         * down state, leader, vote epoch. */
        // �����ظ�
        // 1) <down_state>    1 �������ߣ� 0 ����δ����
        // 2) <leader_runid>  Sentinel ѡ����Ϊ��ͷ Sentinel ������ ID
        // 3) <leader_epoch>  ��ͷ Sentinel Ŀǰ�����ü�Ԫ
        addReplyMultiBulkLen(c,3);
        addReply(c, isdown ? shared.cone : shared.czero);
        addReplyBulkCString(c, leader ? leader : "*");
        addReplyLongLong(c, (long long)leader_epoch);
        if (leader) sdsfree(leader);
    } else if (!strcasecmp(c->argv[1]->ptr,"reset")) {
        /* SENTINEL RESET <pattern> */
        if (c->argc != 3) goto numargserr;
        addReplyLongLong(c,sentinelResetMastersByPattern(c->argv[2]->ptr,SENTINEL_GENERATE_EVENT));
    } else if (!strcasecmp(c->argv[1]->ptr,"get-master-addr-by-name")) {
        /* SENTINEL GET-MASTER-ADDR-BY-NAME <master-name> */
        sentinelRedisInstance *ri;

        if (c->argc != 3) goto numargserr;
        ri = sentinelGetMasterByName(c->argv[2]->ptr);
        if (ri == NULL) {
            addReply(c,shared.nullmultibulk);
        } else {
            sentinelAddr *addr = sentinelGetCurrentMasterAddress(ri);

            addReplyMultiBulkLen(c,2);
            addReplyBulkCString(c,addr->ip);
            addReplyBulkLongLong(c,addr->port);
        }
    } else if (!strcasecmp(c->argv[1]->ptr,"failover")) {
        /* SENTINEL FAILOVER <master-name> */
        sentinelRedisInstance *ri;

        if (c->argc != 3) goto numargserr;
        if ((ri = sentinelGetMasterByNameOrReplyError(c,c->argv[2])) == NULL)
            return;
        if (ri->flags & SRI_FAILOVER_IN_PROGRESS) {
            addReplySds(c,sdsnew("-INPROG Failover already in progress\r\n"));
            return;
        }
        if (sentinelSelectSlave(ri) == NULL) {
            addReplySds(c,sdsnew("-NOGOODSLAVE No suitable slave to promote\r\n"));
            return;
        }
        redisLog(REDIS_WARNING,"Executing user requested FAILOVER of '%s'",
            ri->name);
        sentinelStartFailover(ri);
        ri->flags |= SRI_FORCE_FAILOVER;
        addReply(c,shared.ok);
    } else if (!strcasecmp(c->argv[1]->ptr,"pending-scripts")) {
        /* SENTINEL PENDING-SCRIPTS */

        if (c->argc != 2) goto numargserr;
        sentinelPendingScriptsCommand(c);
    } else if (!strcasecmp(c->argv[1]->ptr,"monitor")) {
        /* SENTINEL MONITOR <name> <ip> <port> <quorum> */
        sentinelRedisInstance *ri;
        long quorum, port;
        char buf[32];

        if (c->argc != 6) goto numargserr;
        if (getLongFromObjectOrReply(c,c->argv[5],&quorum,"Invalid quorum")
            != REDIS_OK) return;
        if (getLongFromObjectOrReply(c,c->argv[4],&port,"Invalid port")
            != REDIS_OK) return;
        /* Make sure the IP field is actually a valid IP before passing it
         * to createSentinelRedisInstance(), otherwise we may trigger a
         * DNS lookup at runtime. */
        if (anetResolveIP(NULL,c->argv[3]->ptr,buf,sizeof(buf)) == ANET_ERR) {
            addReplyError(c,"Invalid IP address specified");
            return;
        }

        /* Parameters are valid. Try to create the master instance. */
        ri = createSentinelRedisInstance(c->argv[2]->ptr,SRI_MASTER,
                c->argv[3]->ptr,port,quorum,NULL);
        if (ri == NULL) {
            switch(errno) {
            case EBUSY:
                addReplyError(c,"Duplicated master name");
                break;
            case EINVAL:
                addReplyError(c,"Invalid port number");
                break;
            default:
                addReplyError(c,"Unspecified error adding the instance");
                break;
            }
        } else {
            sentinelFlushConfig();
            sentinelEvent(REDIS_WARNING,"+monitor",ri,"%@ quorum %d",ri->quorum);
            addReply(c,shared.ok);
        }
    } else if (!strcasecmp(c->argv[1]->ptr,"remove")) {
        /* SENTINEL REMOVE <name> */
        sentinelRedisInstance *ri;

        if ((ri = sentinelGetMasterByNameOrReplyError(c,c->argv[2]))
            == NULL) return;
        sentinelEvent(REDIS_WARNING,"-monitor",ri,"%@");
        dictDelete(sentinel.masters,c->argv[2]->ptr);
        sentinelFlushConfig();
        addReply(c,shared.ok);
    } else if (!strcasecmp(c->argv[1]->ptr,"set")) {
        if (c->argc < 3 || c->argc % 2 == 0) goto numargserr;
        sentinelSetCommand(c);
    } else {
        addReplyErrorFormat(c,"Unknown sentinel subcommand '%s'",
                               (char*)c->argv[1]->ptr);
    }
    return;

numargserr:
    addReplyErrorFormat(c,"Wrong number of arguments for 'sentinel %s'",
                          (char*)c->argv[1]->ptr);
}

/* SENTINEL INFO [section] */
// sentinel ģʽ�µ� INFO ����ʵ��
void sentinelInfoCommand(redisClient *c) {
    char *section = c->argc == 2 ? c->argv[1]->ptr : "default";
    sds info = sdsempty();
    int defsections = !strcasecmp(section,"default");
    int sections = 0;

    if (c->argc > 2) {
        addReply(c,shared.syntaxerr);
        return;
    }

    if (!strcasecmp(section,"server") || defsections) {
        if (sections++) info = sdscat(info,"\r\n");
        sds serversection = genRedisInfoString("server");
        info = sdscatlen(info,serversection,sdslen(serversection));
        sdsfree(serversection);
    }

    if (!strcasecmp(section,"sentinel") || defsections) {
        dictIterator *di;
        dictEntry *de;
        int master_id = 0;

        if (sections++) info = sdscat(info,"\r\n");
        info = sdscatprintf(info,
            "# Sentinel\r\n"
            "sentinel_masters:%lu\r\n"
            "sentinel_tilt:%d\r\n"
            "sentinel_running_scripts:%d\r\n"
            "sentinel_scripts_queue_length:%ld\r\n",
            dictSize(sentinel.masters),
            sentinel.tilt,
            sentinel.running_scripts,
            listLength(sentinel.scripts_queue));

        di = dictGetIterator(sentinel.masters);
        while((de = dictNext(di)) != NULL) {
            sentinelRedisInstance *ri = dictGetVal(de);
            char *status = "ok";

            if (ri->flags & SRI_O_DOWN) status = "odown";
            else if (ri->flags & SRI_S_DOWN) status = "sdown";
            info = sdscatprintf(info,
                "master%d:name=%s,status=%s,address=%s:%d,"
                "slaves=%lu,sentinels=%lu\r\n",
                master_id++, ri->name, status,
                ri->addr->ip, ri->addr->port,
                dictSize(ri->slaves),
                dictSize(ri->sentinels)+1);
        }
        dictReleaseIterator(di);
    }

    addReplySds(c,sdscatprintf(sdsempty(),"$%lu\r\n",
        (unsigned long)sdslen(info)));
    addReplySds(c,info);
    addReply(c,shared.crlf);
}

/* SENTINEL SET <mastername> [<option> <value> ...] */
void sentinelSetCommand(redisClient *c) {
    sentinelRedisInstance *ri;
    int j, changes = 0;
    char *option, *value;

    if ((ri = sentinelGetMasterByNameOrReplyError(c,c->argv[2]))
        == NULL) return;

    /* Process option - value pairs. */
    for (j = 3; j < c->argc; j += 2) {
        option = c->argv[j]->ptr;
        value = c->argv[j+1]->ptr;
        robj *o = c->argv[j+1];
        long long ll;

        if (!strcasecmp(option,"down-after-milliseconds")) {
            /* down-after-millisecodns <milliseconds> */
            if (getLongLongFromObject(o,&ll) == REDIS_ERR || ll <= 0)
                goto badfmt;
            ri->down_after_period = ll;
            sentinelPropagateDownAfterPeriod(ri);
            changes++;
        } else if (!strcasecmp(option,"failover-timeout")) {
            /* failover-timeout <milliseconds> */
            if (getLongLongFromObject(o,&ll) == REDIS_ERR || ll <= 0)
                goto badfmt;
            ri->failover_timeout = ll;
            changes++;
       } else if (!strcasecmp(option,"parallel-syncs")) {
            /* parallel-syncs <milliseconds> */
            if (getLongLongFromObject(o,&ll) == REDIS_ERR || ll <= 0)
                goto badfmt;
            ri->parallel_syncs = ll;
            changes++;
       } else if (!strcasecmp(option,"notification-script")) {
            /* notification-script <path> */
            if (strlen(value) && access(value,X_OK) == -1) {
                addReplyError(c,
                    "Notification script seems non existing or non executable");
                if (changes) sentinelFlushConfig();
                return;
            }
            sdsfree(ri->notification_script);
            ri->notification_script = strlen(value) ? sdsnew(value) : NULL;
            changes++;
       } else if (!strcasecmp(option,"client-reconfig-script")) {
            /* client-reconfig-script <path> */
            if (strlen(value) && access(value,X_OK) == -1) {
                addReplyError(c,
                    "Client reconfiguration script seems non existing or "
                    "non executable");
                if (changes) sentinelFlushConfig();
                return;
            }
            sdsfree(ri->client_reconfig_script);
            ri->client_reconfig_script = strlen(value) ? sdsnew(value) : NULL;
            changes++;
       } else if (!strcasecmp(option,"auth-pass")) {
            /* auth-pass <password> */
            sdsfree(ri->auth_pass);
            ri->auth_pass = strlen(value) ? sdsnew(value) : NULL;
            changes++;
       } else if (!strcasecmp(option,"quorum")) {
            /* quorum <count> */
            if (getLongLongFromObject(o,&ll) == REDIS_ERR || ll <= 0)
                goto badfmt;
            ri->quorum = ll;
            changes++;
        } else {
            addReplyErrorFormat(c,"Unknown option '%s' for SENTINEL SET",
                option);
            if (changes) sentinelFlushConfig();
            return;
        }
        sentinelEvent(REDIS_WARNING,"+set",ri,"%@ %s %s",option,value);
    }

    if (changes) sentinelFlushConfig();
    addReply(c,shared.ok);
    return;

badfmt: /* Bad format errors */
    if (changes) sentinelFlushConfig();
    addReplyErrorFormat(c,"Invalid argument '%s' for SENTINEL SET '%s'",
            value, option);
}

/* Our fake PUBLISH command: it is actually useful only to receive hello messages
 * from the other sentinel instances, and publishing to a channel other than
 * SENTINEL_HELLO_CHANNEL is forbidden.
 *
 * Because we have a Sentinel PUBLISH, the code to send hello messages is the same
 * for all the three kind of instances: masters, slaves, sentinels. */
void sentinelPublishCommand(redisClient *c) {
    if (strcmp(c->argv[1]->ptr,SENTINEL_HELLO_CHANNEL)) {
        addReplyError(c, "Only HELLO messages are accepted by Sentinel instances.");
        return;
    }
    sentinelProcessHelloMessage(c->argv[2]->ptr,sdslen(c->argv[2]->ptr));
    addReplyLongLong(c,1);
}

/* ===================== SENTINEL availability checks ======================= */

/* Is this instance down from our point of view? */
// ���ʵ���Ƿ������ߣ��ӱ� Sentinel �ĽǶ�������
void sentinelCheckSubjectivelyDown(sentinelRedisInstance *ri) {

    mstime_t elapsed = 0;

    if (ri->last_ping_time)
        elapsed = mstime() - ri->last_ping_time;

    /* Check if we are in need for a reconnection of one of the 
     * links, because we are detecting low activity.
     *
     * �����⵽���ӵĻ�Ծ�ȣ�activity���ܵͣ���ô�����ضϿ����ӣ�����������
     *
     * 1) Check if the command link seems connected, was connected not less
     *    than SENTINEL_MIN_LINK_RECONNECT_PERIOD, but still we have a
     *    pending ping for more than half the timeout. */
    // ���ǶϿ�ʵ���� cc ����
    if (ri->cc &&
        (mstime() - ri->cc_conn_time) > SENTINEL_MIN_LINK_RECONNECT_PERIOD &&
        ri->last_ping_time != 0 && /* Ther is a pending ping... */
        /* The pending ping is delayed, and we did not received
         * error replies as well. */
        (mstime() - ri->last_ping_time) > (ri->down_after_period/2) &&
        (mstime() - ri->last_pong_time) > (ri->down_after_period/2))
    {
        sentinelKillLink(ri,ri->cc);
    }

    /* 2) Check if the pubsub link seems connected, was connected not less
     *    than SENTINEL_MIN_LINK_RECONNECT_PERIOD, but still we have no
     *    activity in the Pub/Sub channel for more than
     *    SENTINEL_PUBLISH_PERIOD * 3.
     */
    // ���ǶϿ�ʵ���� pc ����
    if (ri->pc &&
        (mstime() - ri->pc_conn_time) > SENTINEL_MIN_LINK_RECONNECT_PERIOD &&
        (mstime() - ri->pc_last_activity) > (SENTINEL_PUBLISH_PERIOD*3))
    {
        sentinelKillLink(ri,ri->pc);
    }

    /* Update the SDOWN flag. We believe the instance is SDOWN if:
     *
     * ���� SDOWN ��ʶ������������������㣬��ô Sentinel ��Ϊʵ�������ߣ�
     *
     * 1) It is not replying.
     *    ��û�л�Ӧ����
     * 2) We believe it is a master, it reports to be a slave for enough time
     *    to meet the down_after_period, plus enough time to get two times
     *    INFO report from the instance. 
     *    Sentinel ��Ϊʵ������������������������� Sentinel ����������Ϊ�ӷ�������
     *    ���ڳ�������ʱ��֮�󣬷�������Ȼû�������һ��ɫת����
     */
    if (elapsed > ri->down_after_period ||
        (ri->flags & SRI_MASTER &&
         ri->role_reported == SRI_SLAVE &&
         mstime() - ri->role_reported_time >
          (ri->down_after_period+SENTINEL_INFO_PERIOD*2)))
    {
        /* Is subjectively down */
        if ((ri->flags & SRI_S_DOWN) == 0) {
            // �����¼�
            sentinelEvent(REDIS_WARNING,"+sdown",ri,"%@");
            // ��¼���� SDOWN ״̬��ʱ��
            ri->s_down_since_time = mstime();
            // �� SDOWN ��־
            ri->flags |= SRI_S_DOWN;
        }
    } else {
        // �Ƴ��������еģ� SDOWN ״̬
        /* Is subjectively up */
        if (ri->flags & SRI_S_DOWN) {
            // �����¼�
            sentinelEvent(REDIS_WARNING,"-sdown",ri,"%@");
            // �Ƴ���ر�־
            ri->flags &= ~(SRI_S_DOWN|SRI_SCRIPT_KILL_SENT);
        }
    }
}


/* Is this instance down according to the configured quorum?
 *
 * ���ݸ��������� Sentinel ͶƱ���ж�ʵ���Ƿ������ߡ�
 *
 * Note that ODOWN is a weak quorum, it only means that enough Sentinels
 * reported in a given time range that the instance was not reachable.
 *
 * ע�� ODOWN ��һ�� weak quorum ����ֻ��ζ�����㹻��� Sentinel 
 * ��**������ʱ�䷶Χ��**����ʵ�����ɴ
 *
 * However messages can be delayed so there are no strong guarantees about
 * N instances agreeing at the same time about the down state. 
 *
 * ��Ϊ Sentinel ��ʵ���ļ����Ϣ���ܴ����ӳ٣�
 * ����ʵ���� N �� Sentinel **��������ͬһʱ����**�ж�������������������״̬��
 */
void sentinelCheckObjectivelyDown(sentinelRedisInstance *master) {
    dictIterator *di;
    dictEntry *de;
    int quorum = 0, odown = 0;

    // �����ǰ Sentinel �����������ж�Ϊ��������
    // ��ô����Ƿ������� Sentinel ͬ����һ�ж�
    // ��ͬ��������㹻ʱ�������������ж�Ϊ�͹�����
    if (master->flags & SRI_S_DOWN) {
        /* Is down for enough sentinels? */

        // ͳ��ͬ��� Sentinel ��������ʼ�� 1 ���� Sentinel��
        quorum = 1; /* the current sentinel. */

        /* Count all the other sentinels. */
        // ͳ��������Ϊ master ��������״̬�� Sentinel ������
        di = dictGetIterator(master->sentinels);
        while((de = dictNext(di)) != NULL) {
            sentinelRedisInstance *ri = dictGetVal(de);
                
            // �� SENTINEL Ҳ��Ϊ master ������
            if (ri->flags & SRI_MASTER_DOWN) quorum++;
        }
        dictReleaseIterator(di);
        
        // ���ͶƱ�ó���֧����Ŀ���ڵ����ж� ODOWN �����Ʊ��
        // ��ô���� ODOWN ״̬
        if (quorum >= master->quorum) odown = 1;
    }

    /* Set the flag accordingly to the outcome. */
    if (odown) {

        // master �� ODOWN

        if ((master->flags & SRI_O_DOWN) == 0) {
            // �����¼�
            sentinelEvent(REDIS_WARNING,"+odown",master,"%@ #quorum %d/%d",
                quorum, master->quorum);
            // �� ODOWN ��־
            master->flags |= SRI_O_DOWN;
            // ��¼���� ODOWN ��ʱ��
            master->o_down_since_time = mstime();
        }
    } else {

        // δ���� ODOWN

        if (master->flags & SRI_O_DOWN) {

            // ��� master ��������� ODOWN ״̬����ô�Ƴ���״̬

            // �����¼�
            sentinelEvent(REDIS_WARNING,"-odown",master,"%@");
            // �Ƴ� ODOWN ��־
            master->flags &= ~SRI_O_DOWN;
        }
    }
}

/* Receive the SENTINEL is-master-down-by-addr reply, see the
 * sentinelAskMasterStateToOtherSentinels() function for more information. */
// ���ص��������ڴ���SENTINEL ���յ����� SENTINEL 
// ���ص� SENTINEL is-master-down-by-addr ����Ļظ�
void sentinelReceiveIsMasterDownReply(redisAsyncContext *c, void *reply, void *privdata) {
    sentinelRedisInstance *ri = c->data;
    redisReply *r;

    if (ri) ri->pending_commands--;
    if (!reply || !ri) return;
    r = reply;

    /* Ignore every error or unexpected reply.
     * ���Դ���ظ�
     * Note that if the command returns an error for any reason we'll
     * end clearing the SRI_MASTER_DOWN flag for timeout anyway. */
    if (r->type == REDIS_REPLY_ARRAY && r->elements == 3 &&
        r->element[0]->type == REDIS_REPLY_INTEGER &&
        r->element[1]->type == REDIS_REPLY_STRING &&
        r->element[2]->type == REDIS_REPLY_INTEGER)
    {
        // �������һ�λظ�ѯ�ʵ�ʱ��
        ri->last_master_down_reply_time = mstime();

        // ���� SENTINEL ��Ϊ����������״̬
        if (r->element[0]->integer == 1) {
            // ������
            ri->flags |= SRI_MASTER_DOWN;
        } else {
            // δ����
            ri->flags &= ~SRI_MASTER_DOWN;
        }

        // ������� ID ���� "*" �Ļ�����ô����һ����ͶƱ�Ļظ�
        if (strcmp(r->element[1]->str,"*")) {
            /* If the runid in the reply is not "*" the Sentinel actually
             * replied with a vote. */
            sdsfree(ri->leader);
            // ��ӡ��־
            if (ri->leader_epoch != r->element[2]->integer)
                redisLog(REDIS_WARNING,
                    "%s voted for %s %llu", ri->name,
                    r->element[1]->str,
                    (unsigned long long) r->element[2]->integer);
            // ����ʵ������ͷ
            ri->leader = sdsnew(r->element[1]->str);
            ri->leader_epoch = r->element[2]->integer;
        }
    }
}

/* If we think the master is down, we start sending
 * SENTINEL IS-MASTER-DOWN-BY-ADDR requests to other sentinels
 * in order to get the replies that allow to reach the quorum
 * needed to mark the master in ODOWN state and trigger a failover. */
// ��� Sentinel ��Ϊ�������������ߣ�
// ��ô����ͨ�������� Sentinel ���� SENTINEL is-master-down-by-addr ���
// ���Ի���㹻��Ʊ�����������������Ϊ ODOWN ״̬������ʼһ�ι���ת�Ʋ���
#define SENTINEL_ASK_FORCED (1<<0)
void sentinelAskMasterStateToOtherSentinels(sentinelRedisInstance *master, int flags) {
    dictIterator *di;
    dictEntry *de;

    // �������ڼ�����ͬ master ������ sentinel
    // �����Ƿ��� SENTINEL is-master-down-by-addr ����
    di = dictGetIterator(master->sentinels);
    while((de = dictNext(di)) != NULL) {
        sentinelRedisInstance *ri = dictGetVal(de);

        // ����� sentinel ���һ�λظ� SENTINEL master-down-by-addr �����Ѿ����˶��
        mstime_t elapsed = mstime() - ri->last_master_down_reply_time;

        char port[32];
        int retval;

        /* If the master state from other sentinel is too old, we clear it. */
        // ���Ŀ�� Sentinel ����������������Ϣ�Ѿ�̫��û���£���ô���������
        if (elapsed > SENTINEL_ASK_PERIOD*5) {
            ri->flags &= ~SRI_MASTER_DOWN;
            sdsfree(ri->leader);
            ri->leader = NULL;
        }

        /* Only ask if master is down to other sentinels if:
         *
         * ֻ�������������ʱ���������� sentinel ѯ�����������Ƿ�������
         *
         * 1) We believe it is down, or there is a failover in progress.
         *    �� sentinel ���ŷ������Ѿ����ߣ�������Ը����������Ĺ���ת�Ʋ�������ִ��
         * 2) Sentinel is connected.
         *    Ŀ�� Sentinel �뱾 Sentinel ������
         * 3) We did not received the info within SENTINEL_ASK_PERIOD ms. 
         *    ��ǰ Sentinel �� SENTINEL_ASK_PERIOD ������û�л�ù�Ŀ�� Sentinel ��������Ϣ
         * 4) ���� 1 ������ 2 ��������� 3 �����㣬���� flags ���������� SENTINEL_ASK_FORCED ��ʶ
         */
        if ((master->flags & SRI_S_DOWN) == 0) continue;
        if (ri->flags & SRI_DISCONNECTED) continue;
        if (!(flags & SENTINEL_ASK_FORCED) &&
            mstime() - ri->last_master_down_reply_time < SENTINEL_ASK_PERIOD)
            continue;

        /* Ask */
        // ���� SENTINEL is-master-down-by-addr ����
        ll2string(port,sizeof(port),master->addr->port);
        retval = redisAsyncCommand(ri->cc,
                    sentinelReceiveIsMasterDownReply, NULL,
                    "SENTINEL is-master-down-by-addr %s %s %llu %s",
                    master->addr->ip, port,
                    sentinel.current_epoch,
                    // ����� Sentinel �Ѿ���⵽ master ���� ODOWN 
                    // ����Ҫ��ʼһ�ι���ת�ƣ���ô������ Sentinel �����Լ������� ID
                    // �öԷ������Լ�ͶһƱ������Է��������Ԫ�ڻ�û��ͶƱ�Ļ���
                    (master->failover_state > SENTINEL_FAILOVER_STATE_NONE) ?
                    server.runid : "*");
        if (retval == REDIS_OK) ri->pending_commands++;
    }
    dictReleaseIterator(di);
}

/* =============================== FAILOVER ================================= */

/* Vote for the sentinel with 'req_runid' or return the old vote if already
 * voted for the specifed 'req_epoch' or one greater.
 *
 * Ϊ���� ID Ϊ req_runid �� Sentinel Ͷ��һƱ�������ֶ���������ܳ��֣�
 * 1) ��� Sentinel �� req_epoch ��Ԫ�Ѿ�Ͷ��Ʊ�ˣ���ô����֮ǰͶ��Ʊ��
 * 2) ��� Sentinel �Ѿ�Ϊ���� req_epoch �ļ�ԪͶ��Ʊ�ˣ���ô���ظ����Ԫ��ͶƱ��
 *
 * If a vote is not available returns NULL, otherwise return the Sentinel
 * runid and populate the leader_epoch with the epoch of the vote. 
 *
 * ���ͶƱ��ʱ�����ã���ô���� NULL ��
 * ���򷵻� Sentinel ������ ID ��������ͶƱ�ļ�Ԫ���浽 leader_epoch ָ���ֵ���档
 */
char *sentinelVoteLeader(sentinelRedisInstance *master, uint64_t req_epoch, char *req_runid, uint64_t *leader_epoch) {
    if (req_epoch > sentinel.current_epoch) {
        sentinel.current_epoch = req_epoch;
        sentinelFlushConfig();
        sentinelEvent(REDIS_WARNING,"+new-epoch",master,"%llu",
            (unsigned long long) sentinel.current_epoch);
    }

    if (master->leader_epoch < req_epoch && sentinel.current_epoch <= req_epoch)
    {
        sdsfree(master->leader);
        master->leader = sdsnew(req_runid);
        master->leader_epoch = sentinel.current_epoch;
        sentinelFlushConfig();
        sentinelEvent(REDIS_WARNING,"+vote-for-leader",master,"%s %llu",
            master->leader, (unsigned long long) master->leader_epoch);
        /* If we did not voted for ourselves, set the master failover start
         * time to now, in order to force a delay before we can start a
         * failover for the same master. */
        if (strcasecmp(master->leader,server.runid))
            master->failover_start_time = mstime()+rand()%SENTINEL_MAX_DESYNC;
    }

    *leader_epoch = master->leader_epoch;
    return master->leader ? sdsnew(master->leader) : NULL;
}

// ��¼�͹� leader ͶƱ�Ľṹ
struct sentinelLeader {

    // sentinel ������ id
    char *runid;

    // �� sentinel ��õ�Ʊ��
    unsigned long votes;
};

/* Helper function for sentinelGetLeader, increment the counter
 * relative to the specified runid. */
// Ϊ���� ID �� Sentinel ʵ������һƱ
int sentinelLeaderIncr(dict *counters, char *runid) {
    dictEntry *de = dictFind(counters,runid);
    uint64_t oldval;

    if (de) {
        oldval = dictGetUnsignedIntegerVal(de);
        dictSetUnsignedIntegerVal(de,oldval+1);
        return oldval+1;
    } else {
        de = dictAddRaw(counters,runid);
        redisAssert(de != NULL);
        dictSetUnsignedIntegerVal(de,1);
        return 1;
    }
}

/* Scan all the Sentinels attached to this master to check if there
 * is a leader for the specified epoch.
 *
 * ɨ�����м��� master �� Sentinels ���鿴�Ƿ��� Sentinels �������Ԫ����ͷ��
 *
 * To be a leader for a given epoch, we should have the majorify of
 * the Sentinels we know that reported the same instance as
 * leader for the same epoch. 
 *
 * Ҫ��һ�� Sentinel ��Ϊ����Ԫ����ͷ��
 * ��� Sentinel �����ô�������� Sentinel �������Ǹü�Ԫ����ͷ���С�
 */
// ѡ�ٳ� master ��ָ�� epoch �ϵ���ͷ
char *sentinelGetLeader(sentinelRedisInstance *master, uint64_t epoch) {
    dict *counters;
    dictIterator *di;
    dictEntry *de;
    unsigned int voters = 0, voters_quorum;
    char *myvote;
    char *winner = NULL;
    uint64_t leader_epoch;
    uint64_t max_votes = 0;

    redisAssert(master->flags & (SRI_O_DOWN|SRI_FAILOVER_IN_PROGRESS));

    // ͳ����
    counters = dictCreate(&leaderVotesDictType,NULL);

    /* Count other sentinels votes */
    // ͳ������ sentinel ������ leader ͶƱ
    di = dictGetIterator(master->sentinels);
    while((de = dictNext(di)) != NULL) {
        sentinelRedisInstance *ri = dictGetVal(de);

        // ΪĿ�� Sentinel ѡ������ͷ Sentinel ����һƱ
        if (ri->leader != NULL && ri->leader_epoch == sentinel.current_epoch)
            sentinelLeaderIncr(counters,ri->leader);

        // ͳ��ͶƱ����
        voters++;
    }
    dictReleaseIterator(di);

    /* Check what's the winner. For the winner to win, it needs two conditions:
     *
     * ѡ����ͷ leader ��������������������������
     *
     * 1) Absolute majority between voters (50% + 1).
     *    �ж���һ��� Sentinel ֧��
     * 2) And anyway at least master->quorum votes. 
     *    ͶƱ������Ҫ�� master->quorum ��ô��
     */
    di = dictGetIterator(counters);
    while((de = dictNext(di)) != NULL) {

        // ȡ��Ʊ��
        uint64_t votes = dictGetUnsignedIntegerVal(de);

        // ѡ��Ʊ��������
        if (votes > max_votes) {
            max_votes = votes;
            winner = dictGetKey(de);
        }
    }
    dictReleaseIterator(di);

    /* Count this Sentinel vote:
     * if this Sentinel did not voted yet, either vote for the most
     * common voted sentinel, or for itself if no vote exists at all. */
    // �� Sentinel ����ͶƱ
    // ��� Sentinel ֮ǰ��û�н���ͶƱ����ô������ѡ��
    // 1�����ѡ���� winner �����Ʊ��֧�ֵ� Sentinel ������ô��� Sentinel ҲͶ winner һƱ
    // 2�����û��ѡ�� winner ����ô Sentinel Ͷ�Լ�һƱ
    if (winner)
        myvote = sentinelVoteLeader(master,epoch,winner,&leader_epoch);
    else
        myvote = sentinelVoteLeader(master,epoch,server.runid,&leader_epoch);

    // ��ͷ Sentinel ��ѡ����������ͷ�ļ�Ԫ�͸����ļ�Ԫһ��
    if (myvote && leader_epoch == epoch) {

        // Ϊ��ͷ Sentinel ����һƱ����һƱ���Ա� Sentinel ��
        uint64_t votes = sentinelLeaderIncr(counters,myvote);

        // ���ͶƱ֮���Ʊ�������Ʊ��Ҫ����ô������ͷ Sentinel
        if (votes > max_votes) {
            max_votes = votes;
            winner = myvote;
        }
    }
    voters++; /* Anyway, count me as one of the voters. */

    // ���֧����ͷ��ͶƱ��������������
    // ����֧��Ʊ�������� master ����ָ����ͶƱ����
    // ��ô�����ͷѡ����Ч
    voters_quorum = voters/2+1;
    if (winner && (max_votes < voters_quorum || max_votes < master->quorum))
        winner = NULL;

    // ������ͷ Sentinel ������ NULL
    winner = winner ? sdsnew(winner) : NULL;
    sdsfree(myvote);
    dictRelease(counters);
    return winner;
}

/* Send SLAVEOF to the specified instance, always followed by a
 * CONFIG REWRITE command in order to store the new configuration on disk
 * when possible (that is, if the Redis instance is recent enough to support
 * config rewriting, and if the server was started with a configuration file).
 *
 * ��ָ��ʵ������ SLAVEOF ������ڿ���ʱ��ִ�� CONFIG REWRITE ���
 * ����ǰ���ñ��浽�����С�
 *
 * If Host is NULL the function sends "SLAVEOF NO ONE".
 *
 * ��� host ����Ϊ NULL ����ô��ʵ������ SLAVEOF NO ONE ����
 *
 * The command returns REDIS_OK if the SLAVEOF command was accepted for
 * (later) delivery otherwise REDIS_ERR. The command replies are just
 * discarded. 
 *
 * ������ӳɹ����첽���ͣ�ʱ���������� REDIS_OK ��
 * ���ʧ��ʱ���� REDIS_ERR ��
 * ����ظ��ᱻ������
 */
int sentinelSendSlaveOf(sentinelRedisInstance *ri, char *host, int port) {
    char portstr[32];
    int retval;

    ll2string(portstr,sizeof(portstr),port);

    if (host == NULL) {
        host = "NO";
        memcpy(portstr,"ONE",4);
    }

    // ���� SLAVEOF NO ONE
    retval = redisAsyncCommand(ri->cc,
        sentinelDiscardReplyCallback, NULL, "SLAVEOF %s %s", host, portstr);
    if (retval == REDIS_ERR) return retval;

    ri->pending_commands++;

    // ���� CONFIG REWRITE
    if (redisAsyncCommand(ri->cc,
        sentinelDiscardReplyCallback, NULL, "CONFIG REWRITE") == REDIS_OK)
    {
        ri->pending_commands++;
    }

    return REDIS_OK;
}

/* Setup the master state to start a failover. */
// ��������������״̬����ʼһ�ι���ת��
void sentinelStartFailover(sentinelRedisInstance *master) {
    redisAssert(master->flags & SRI_MASTER);

    // ���¹���ת��״̬
    master->failover_state = SENTINEL_FAILOVER_STATE_WAIT_START;

    // ������������״̬
    master->flags |= SRI_FAILOVER_IN_PROGRESS;

    // ���¼�Ԫ
    master->failover_epoch = ++sentinel.current_epoch;

    sentinelEvent(REDIS_WARNING,"+new-epoch",master,"%llu",
        (unsigned long long) sentinel.current_epoch);

    sentinelEvent(REDIS_WARNING,"+try-failover",master,"%@");

    // ��¼����ת��״̬�ı��ʱ��
    master->failover_start_time = mstime()+rand()%SENTINEL_MAX_DESYNC;
    master->failover_state_change_time = mstime();
}

/* This function checks if there are the conditions to start the failover,
 * that is:
 *
 * �����������Ƿ���Ҫ��ʼһ�ι���ת�Ʋ�����
 *
 * 1) Master must be in ODOWN condition.
 *    ���������Ѿ����� ODOWN ״̬��
 * 2) No failover already in progress.
 *    ��ǰû�����ͬһ���������Ĺ���ת�Ʋ�����ִ�С�
 * 3) No failover already attempted recently.
 *    ���ʱ���ڣ������������û�г��Թ�ִ�й���ת��
 *    ��Ӧ����Ϊ�˷�ֹƵ��ִ�У���
 * 
 * We still don't know if we'll win the election so it is possible that we
 * start the failover but that we'll not be able to act.
 *
 * ��Ȼ Sentinel ���Է���һ�ι���ת�ƣ�����Ϊ����ת�Ʋ���������ͷ Sentinel ִ�еģ�
 * ���Է������ת�Ƶ� Sentinel ��һ������ִ�й���ת�Ƶ� Sentinel ��
 *
 * Return non-zero if a failover was started. 
 *
 * �������ת�Ʋ����ɹ���ʼ����ô�������ط� 0 ֵ��
 */
int sentinelStartFailoverIfNeeded(sentinelRedisInstance *master) {

    /* We can't failover if the master is not in O_DOWN state. */
    if (!(master->flags & SRI_O_DOWN)) return 0;

    /* Failover already in progress? */
    if (master->flags & SRI_FAILOVER_IN_PROGRESS) return 0;

    /* Last failover attempt started too little time ago? */
    if (mstime() - master->failover_start_time <
        master->failover_timeout*2)
    {
        if (master->failover_delay_logged != master->failover_start_time) {
            time_t clock = (master->failover_start_time +
                            master->failover_timeout*2) / 1000;
            char ctimebuf[26];

            ctime_r(&clock,ctimebuf);
            ctimebuf[24] = '\0'; /* Remove newline. */
            master->failover_delay_logged = master->failover_start_time;
            redisLog(REDIS_WARNING,
                "Next failover delay: I will not start a failover before %s",
                ctimebuf);
        }
        return 0;
    }

    // ��ʼһ�ι���ת��
    sentinelStartFailover(master);

    return 1;
}

/* Select a suitable slave to promote. The current algorithm only uses
 * the following parameters:
 *
 * �ڶ���ӷ�������ѡ��һ����Ϊ�µ�����������
 * �㷨ʹ�����²�����
 *
 * 1) None of the following conditions: S_DOWN, O_DOWN, DISCONNECTED.
 *    ���� S_DOWN �� O_DOWN �� DISCONNECTED ״̬�Ĵӷ��������ᱻѡ�С�
 * 2) Last time the slave replied to ping no more than 5 times the PING period.
 *    �������һ�λظ� PING ����� 5 �����ϵĴӷ��������ᱻѡ�С�
 * 3) info_refresh not older than 3 times the INFO refresh period.
 *    �������һ�λظ� INFO �����ʱ�䳬�� info_refresh ʱ�������Ĵӷ��������ᱻ���ǡ�
 * 4) master_link_down_time no more than:
 *    ���ӷ�����֮������ӶϿ�ʱ�䲻�ܳ�����
 *     (now - master->s_down_since_time) + (master->down_after_period * 10).
 *    Basically since the master is down from our POV, the slave reports
 *    to be disconnected no more than 10 times the configured down-after-period.
 *    ��Ϊ�ӵ�ǰ Sentinel ���������������Ѿ���������״̬��
 *    ����������˵��
 *    �ӷ���������������֮������ӶϿ�ʱ�䲻Ӧ�ó��� down-after-period ��ʮ����
 *    This is pretty much black magic but the idea is, the master was not
 *    available so the slave may be lagging, but not over a certain time.
 *    ������ȥ�е����ħ������������жϵ������������ģ�
 *    ��������������֮�����ӷ����������Ӿͻ�Ͽ�����ֻҪ�����ߵ����������������Ǵӷ�����
 *    �����仰˵�����ӶϿ������������������Ǵӷ�������ɵģ�
 *    ��ô���ӷ�����֮������ӶϿ�ʱ��Ͳ���̫����
 *    Anyway we'll select the best slave according to replication offset.
 *    ������ֻ��һ�������ֶΣ���Ϊ�������Ƕ���ʹ�ø���ƫ��������ѡ�ӷ�������
 * 5) Slave priority can't be zero, otherwise the slave is discarded.
 *    �ӷ����������ȼ�����Ϊ 0 �����ȼ�Ϊ 0 �Ĵӷ�������ʾ�����á�
 *
 * Among all the slaves matching the above conditions we select the slave
 * with, in order of sorting key:
 *
 * �������������Ĵӷ����������ǻᰴ���������Դӷ�������������
 *
 * - lower slave_priority.
 *   ���ȼ���С�Ĵӷ��������ȡ�
 * - bigger processed replication offset.
 *   ����ƫ�����ϴ�Ĵӷ��������ȡ�
 * - lexicographically smaller runid.
 *   ���� ID ��С�Ĵӷ��������ȡ�
 *
 * Basically if runid is the same, the slave that processed more commands
 * from the master is selected.
 *
 * ������� ID ��ͬ����ôִ�����������϶���Ǹ��ӷ������ᱻѡ�С�
 *
 * The function returns the pointer to the selected slave, otherwise
 * NULL if no suitable slave was found.
 *
 * sentinelSelectSlave �������ر�ѡ�дӷ�������ʵ��ָ�룬
 * ���û�к�ʱ�Ĵӷ���������ô���� NULL ��
 */

/* Helper for sentinelSelectSlave(). This is used by qsort() in order to
 * sort suitable slaves in a "better first" order, to take the first of
 * the list. */
// ������������ѡ�����õĴӷ�����
int compareSlavesForPromotion(const void *a, const void *b) {
    sentinelRedisInstance **sa = (sentinelRedisInstance **)a,
                          **sb = (sentinelRedisInstance **)b;
    char *sa_runid, *sb_runid;

    // ���ȼ���С�Ĵӷ�����ʤ��
    if ((*sa)->slave_priority != (*sb)->slave_priority)
        return (*sa)->slave_priority - (*sb)->slave_priority;

    /* If priority is the same, select the slave with greater replication
     * offset (processed more data frmo the master). */
    // ������ȼ���ͬ����ô����ƫ�����ϴ���Ǹ��ӷ�����ʤ��
    // ��ƫ�����ϴ��ʾ������������ȡ�����ݸ��࣬��������
    if ((*sa)->slave_repl_offset > (*sb)->slave_repl_offset) {
        return -1; /* a < b */
    } else if ((*sa)->slave_repl_offset < (*sb)->slave_repl_offset) {
        return 1; /* b > a */
    }

    /* If the replication offset is the same select the slave with that has
     * the lexicographically smaller runid. Note that we try to handle runid
     * == NULL as there are old Redis versions that don't publish runid in
     * INFO. A NULL runid is considered bigger than any other runid. */
    // �������ƫ����Ҳ��ͬ����ôѡ������ ID �ϵ͵��Ǹ��ӷ�����
    // ע�⣬����û������ ID �ľɰ� Redis ��˵��Ĭ�ϵ����� ID Ϊ NULL
    sa_runid = (*sa)->runid;
    sb_runid = (*sb)->runid;
    if (sa_runid == NULL && sb_runid == NULL) return 0;
    else if (sa_runid == NULL) return 1;  /* a > b */
    else if (sb_runid == NULL) return -1; /* a < b */
    return strcasecmp(sa_runid, sb_runid);
}

// ���������������дӷ������У���ѡһ����Ϊ�µ���������
// ���û�кϸ����������������ô���� NULL
sentinelRedisInstance *sentinelSelectSlave(sentinelRedisInstance *master) {

    sentinelRedisInstance **instance =
        zmalloc(sizeof(instance[0])*dictSize(master->slaves));
    sentinelRedisInstance *selected = NULL;
    int instances = 0;
    dictIterator *di;
    dictEntry *de;
    mstime_t max_master_down_time = 0;

    // ������Խ��յģ��ӷ���������������֮����������ʱ��
    // ���ֵ���Ա�֤��ѡ�еĴӷ����������ݿⲻ��̫��
    if (master->flags & SRI_S_DOWN)
        max_master_down_time += mstime() - master->s_down_since_time;
    max_master_down_time += master->down_after_period * 10;

    // �������дӷ�����
    di = dictGetIterator(master->slaves);
    while((de = dictNext(di)) != NULL) {

        // �ӷ�����ʵ��
        sentinelRedisInstance *slave = dictGetVal(de);
        mstime_t info_validity_time;

        // �������� SDOWN ��ODOWN �����Ѷ��ߵĴӷ�����
        if (slave->flags & (SRI_S_DOWN|SRI_O_DOWN|SRI_DISCONNECTED)) continue;
        if (mstime() - slave->last_avail_time > SENTINEL_PING_PERIOD*5) continue;
        if (slave->slave_priority == 0) continue;

        /* If the master is in SDOWN state we get INFO for slaves every second.
         * Otherwise we get it with the usual period so we need to account for
         * a larger delay. */
        // ��������������� SDOWN ״̬����ô Sentinel ��ÿ��һ�ε�Ƶ����ӷ��������� INFO ����
        // ������ƽ��Ƶ����ӷ��������� INFO ����
        // ����Ҫ��� INFO ����ķ���ֵ�Ƿ�Ϸ�������ʱ������һ���������Լ����ӳ�
        if (master->flags & SRI_S_DOWN)
            info_validity_time = SENTINEL_PING_PERIOD*5;
        else
            info_validity_time = SENTINEL_INFO_PERIOD*3;

        // INFO �ظ��ѹ��ڣ�������
        if (mstime() - slave->info_refresh > info_validity_time) continue;

        // �ӷ��������ߵ�ʱ�������������
        if (slave->master_link_down_time > max_master_down_time) continue;

        // ����ѡ�е� slave ���浽������
        instance[instances++] = slave;
    }
    dictReleaseIterator(di);

    if (instances) {

        // �Ա�ѡ�еĴӷ�������������
        qsort(instance,instances,sizeof(sentinelRedisInstance*),
            compareSlavesForPromotion);
        
        // ��ֵ��͵Ĵӷ�����Ϊ��ѡ�з�����
        selected = instance[0];
    }
    zfree(instance);

    // ���ر�ѡ�еĴӷ�����
    return selected;
}

/* ---------------- Failover state machine implementation ------------------- */

// ׼��ִ�й���ת��
void sentinelFailoverWaitStart(sentinelRedisInstance *ri) {
    char *leader;
    int isleader;

    /* Check if we are the leader for the failover epoch. */
    // ��ȡ������Ԫ����ͷ Sentinel
    leader = sentinelGetLeader(ri, ri->failover_epoch);
    // �� Sentinel �Ƿ�Ϊ��ͷ Sentinel ��
    isleader = leader && strcasecmp(leader,server.runid) == 0;
    sdsfree(leader);

    /* If I'm not the leader, and it is not a forced failover via
     * SENTINEL FAILOVER, then I can't continue with the failover. */
    // ����� Sentinel ������ͷ��������ι���Ǩ�Ʋ���һ��ǿ�ƹ���Ǩ�Ʋ���
    // ��ô�� Sentinel ��������
    if (!isleader && !(ri->flags & SRI_FORCE_FAILOVER)) {
        int election_timeout = SENTINEL_ELECTION_TIMEOUT;

        /* The election timeout is the MIN between SENTINEL_ELECTION_TIMEOUT
         * and the configured failover timeout. */
        // ��ѡ��ʱ�������������ڣ��� SENTINEL_ELECTION_TIMEOUT
        // �� Sentinel ���õĹ���Ǩ��ʱ��֮��Ľ�С�Ǹ�ֵ
        if (election_timeout > ri->failover_timeout)
            election_timeout = ri->failover_timeout;

        /* Abort the failover if I'm not the leader after some time. */
        // Sentinel �ĵ�ѡʱ���ѹ���ȡ������ת�Ƽƻ�
        if (mstime() - ri->failover_start_time > election_timeout) {
            sentinelEvent(REDIS_WARNING,"-failover-abort-not-elected",ri,"%@");
            // ȡ������ת��
            sentinelAbortFailover(ri);
        }
        return;
    }

    // �� Sentinel ��Ϊ��ͷ����ʼִ�й���Ǩ�Ʋ���...

    sentinelEvent(REDIS_WARNING,"+elected-leader",ri,"%@");

    // ����ѡ��ӷ�����״̬
    ri->failover_state = SENTINEL_FAILOVER_STATE_SELECT_SLAVE;
    ri->failover_state_change_time = mstime();

    sentinelEvent(REDIS_WARNING,"+failover-state-select-slave",ri,"%@");
}

// ѡ����ʵĴӷ�������Ϊ�µ���������
void sentinelFailoverSelectSlave(sentinelRedisInstance *ri) {

    // �ھ��������������Ĵӷ������У�ѡ���·�����
    sentinelRedisInstance *slave = sentinelSelectSlave(ri);

    /* We don't handle the timeout in this state as the function aborts
     * the failover or go forward in the next state. */
    // û�к��ʵĴӷ�������ֱ����ֹ����ת�Ʋ���
    if (slave == NULL) {

        // û�п��õĴӷ�������������Ϊ����������������ת�Ʋ����޷�ִ��
        sentinelEvent(REDIS_WARNING,"-failover-abort-no-good-slave",ri,"%@");

        // ��ֹ����ת��
        sentinelAbortFailover(ri);

    } else {

        // �ɹ�ѡ������������

        // �����¼�
        sentinelEvent(REDIS_WARNING,"+selected-slave",slave,"%@");

        // ��ʵ�����������
        slave->flags |= SRI_PROMOTED;

        // ��¼��ѡ�еĴӷ�����
        ri->promoted_slave = slave;

        // ���¹���ת��״̬
        ri->failover_state = SENTINEL_FAILOVER_STATE_SEND_SLAVEOF_NOONE;

        // ����״̬�ı�ʱ��
        ri->failover_state_change_time = mstime();

        // �����¼�
        sentinelEvent(REDIS_NOTICE,"+failover-state-send-slaveof-noone",
            slave, "%@");
    }
}

// ��ѡ�еĴӷ��������� SLAVEOF no one ����
// ��������Ϊ�µ���������
void sentinelFailoverSendSlaveOfNoOne(sentinelRedisInstance *ri) {
    int retval;

    /* We can't send the command to the promoted slave if it is now
     * disconnected. Retry again and again with this state until the timeout
     * is reached, then abort the failover. */
    // ���ѡ�еĴӷ����������ˣ���ô�ڸ�����ʱ��������
    // �������ʱ����ѡ�еĴӷ�����Ҳû�����ߣ���ô��ֹ����Ǩ�Ʋ���
    // ��һ����˵������������Ļ����С����Ϊ��ѡ���µ���������ʱ��
    // �Ѿ����ߵĴӷ������ǲ��ᱻѡ�еģ������������ֻ�������
    // �ӷ�������ѡ�У����ҷ��� SLAVEOF NO ONE ����֮ǰ�����ʱ���ڣ�
    if (ri->promoted_slave->flags & SRI_DISCONNECTED) {

        // �������ʱ�ޣ��Ͳ�������
        if (mstime() - ri->failover_state_change_time > ri->failover_timeout) {
            sentinelEvent(REDIS_WARNING,"-failover-abort-slave-timeout",ri,"%@");
            sentinelAbortFailover(ri);
        }
        return;
    }

    /* Send SLAVEOF NO ONE command to turn the slave into a master.
     *
     * �������Ĵӷ��������� SLAVEOF NO ONE ���������Ϊһ������������
     *
     * We actually register a generic callback for this command as we don't
     * really care about the reply. We check if it worked indirectly observing
     * if INFO returns a different role (master instead of slave). 
     *
     * ����û��Ϊ����ظ�����һ���ص���������Ϊ�ӷ������Ƿ��Ѿ�ת��Ϊ������������
     * ͨ����ӷ��������� INFO ������ȷ��
     */
    retval = sentinelSendSlaveOf(ri->promoted_slave,NULL,0);
    if (retval != REDIS_OK) return;
    sentinelEvent(REDIS_NOTICE, "+failover-state-wait-promotion",
        ri->promoted_slave,"%@");

    // ����״̬
    // ���״̬���� Sentinel �ȴ���ѡ�еĴӷ���������Ϊ��������
    ri->failover_state = SENTINEL_FAILOVER_STATE_WAIT_PROMOTION;

    // ����״̬�ı��ʱ��
    ri->failover_state_change_time = mstime();
}

/* We actually wait for promotion indirectly checking with INFO when the
 * slave turns into a master. */
// Sentinel ��ͨ�� INFO ����Ļظ����ӷ������Ƿ��Ѿ�ת��Ϊ��������
// ����ֻ������ʱ��
void sentinelFailoverWaitPromotion(sentinelRedisInstance *ri) {
    /* Just handle the timeout. Switching to the next state is handled
     * by the function parsing the INFO command of the promoted slave. */
    if (mstime() - ri->failover_state_change_time > ri->failover_timeout) {
        sentinelEvent(REDIS_WARNING,"-failover-abort-slave-timeout",ri,"%@");
        sentinelAbortFailover(ri);
    }
}

// �жϹ���ת�Ʋ����Ƿ����
// ����������Ϊ��ʱ��Ҳ������Ϊ���дӷ������Ѿ�ͬ��������������
void sentinelFailoverDetectEnd(sentinelRedisInstance *master) {
    int not_reconfigured = 0, timeout = 0;
    dictIterator *di;
    dictEntry *de;

    // �ϴ� failover ״̬����������������ʱ��
    mstime_t elapsed = mstime() - master->failover_state_change_time;

    /* We can't consider failover finished if the promoted slave is
     * not reachable. */
    // ��������������Ѿ����ߣ���ô����ת�Ʋ������ɹ�
    if (master->promoted_slave == NULL ||
        master->promoted_slave->flags & SRI_S_DOWN) return;

    /* The failover terminates once all the reachable slaves are properly
     * configured. */
    // ����δ���ͬ���Ĵӷ�����������
    di = dictGetIterator(master->slaves);
    while((de = dictNext(di)) != NULL) {
        sentinelRedisInstance *slave = dictGetVal(de);

        // �����������������ͬ���Ĵӷ���������������
        if (slave->flags & (SRI_PROMOTED|SRI_RECONF_DONE)) continue;

        // �����ߵĴӷ���������������
        if (slave->flags & SRI_S_DOWN) continue;

        // ��һ
        not_reconfigured++;
    }
    dictReleaseIterator(di);

    /* Force end of failover on timeout. */
    // ���ϲ�����Ϊ��ʱ������
    if (elapsed > master->failover_timeout) {
        // ����δ��ɵĴӷ�����
        not_reconfigured = 0;
        // �򿪳�ʱ��־
        timeout = 1;
        // ���ͳ�ʱ�¼�
        sentinelEvent(REDIS_WARNING,"+failover-end-for-timeout",master,"%@");
    }

    // ���дӷ������������ͬ��������ת�ƽ���
    if (not_reconfigured == 0) {
        sentinelEvent(REDIS_WARNING,"+failover-end",master,"%@");
        // ���¹���ת��״̬
        // ��һ״̬����֪ Sentinel �����дӷ��������Ѿ�ͬ��������������
        master->failover_state = SENTINEL_FAILOVER_STATE_UPDATE_CONFIG;
        // ����״̬�ı��ʱ��
        master->failover_state_change_time = mstime();
    }

    /* If I'm the leader it is a good idea to send a best effort SLAVEOF
     * command to all the slaves still not reconfigured to replicate with
     * the new master. */
    if (timeout) {
        dictIterator *di;
        dictEntry *de;

        // �������дӷ�����
        di = dictGetIterator(master->slaves);
        while((de = dictNext(di)) != NULL) {
            sentinelRedisInstance *slave = dictGetVal(de);
            int retval;

            // �����ѷ��� SLAVEOF ����Լ��Ѿ����ͬ�������дӷ�����
            if (slave->flags &
                (SRI_RECONF_DONE|SRI_RECONF_SENT|SRI_DISCONNECTED)) continue;

            // ��������
            retval = sentinelSendSlaveOf(slave,
                    master->promoted_slave->addr->ip,
                    master->promoted_slave->addr->port);
            if (retval == REDIS_OK) {
                sentinelEvent(REDIS_NOTICE,"+slave-reconf-sent-be",slave,"%@");
                // �򿪴ӷ������� SLAVEOF �����ѷ��ͱ��
                slave->flags |= SRI_RECONF_SENT;
            }
        }
        dictReleaseIterator(di);
    }
}

/* Send SLAVE OF <new master address> to all the remaining slaves that
 * still don't appear to have the configuration updated. */
// ��������δͬ�������������Ĵӷ��������� SLAVEOF <new-master-address> ����
void sentinelFailoverReconfNextSlave(sentinelRedisInstance *master) {
    dictIterator *di;
    dictEntry *de;
    int in_progress = 0;

    // ��������ͬ�������������Ĵӷ���������
    di = dictGetIterator(master->slaves);
    while((de = dictNext(di)) != NULL) {
        sentinelRedisInstance *slave = dictGetVal(de);

        // SLAVEOF �����ѷ��ͣ�����ͬ�����ڽ���
        if (slave->flags & (SRI_RECONF_SENT|SRI_RECONF_INPROG))
            in_progress++;
    }
    dictReleaseIterator(di);

    // �������ͬ���Ĵӷ��������������� parallel-syncs ѡ���ֵ
    // ��ô���������ӷ����������ôӷ���������������������ͬ��
    di = dictGetIterator(master->slaves);
    while(in_progress < master->parallel_syncs &&
          (de = dictNext(di)) != NULL)
    {
        sentinelRedisInstance *slave = dictGetVal(de);
        int retval;

        /* Skip the promoted slave, and already configured slaves. */
        // �����������������Լ��Ѿ������ͬ���Ĵӷ�����
        if (slave->flags & (SRI_PROMOTED|SRI_RECONF_DONE)) continue;

        /* If too much time elapsed without the slave moving forward to
         * the next state, consider it reconfigured even if it is not.
         * Sentinels will detect the slave as misconfigured and fix its
         * configuration later. */
        if ((slave->flags & SRI_RECONF_SENT) &&
            (mstime() - slave->slave_reconf_sent_time) >
            SENTINEL_SLAVE_RECONF_TIMEOUT)
        {
            // ������ʰͬ���¼�
            sentinelEvent(REDIS_NOTICE,"-slave-reconf-sent-timeout",slave,"%@");
            // ����ѷ��� SLAVEOF ����ı��
            slave->flags &= ~SRI_RECONF_SENT;
            slave->flags |= SRI_RECONF_DONE;
        }

        /* Nothing to do for instances that are disconnected or already
         * in RECONF_SENT state. */
        // �������ӷ��������� SLAVEOF �������ͬ�����ڽ���
        // �ֻ��ߴӷ������Ѷ��ߣ���ô�Թ��÷�����
        if (slave->flags & (SRI_DISCONNECTED|SRI_RECONF_SENT|SRI_RECONF_INPROG))
            continue;

        /* Send SLAVEOF <new master>. */
        // ��ӷ��������� SLAVEOF �������ͬ������������
        retval = sentinelSendSlaveOf(slave,
                master->promoted_slave->addr->ip,
                master->promoted_slave->addr->port);
        if (retval == REDIS_OK) {

            // ��״̬��Ϊ SLAVEOF �����ѷ���
            slave->flags |= SRI_RECONF_SENT;
            // ���·��� SLAVEOF �����ʱ��
            slave->slave_reconf_sent_time = mstime();
            sentinelEvent(REDIS_NOTICE,"+slave-reconf-sent",slave,"%@");
            // ���ӵ�ǰ����ͬ���Ĵӷ�����������
            in_progress++;
        }
    }
    dictReleaseIterator(di);

    /* Check if all the slaves are reconfigured and handle timeout. */
    // �ж��Ƿ����дӷ�������ͬ�����Ѿ����
    sentinelFailoverDetectEnd(master);
}

/* This function is called when the slave is in
 * SENTINEL_FAILOVER_STATE_UPDATE_CONFIG state. In this state we need
 * to remove it from the master table and add the promoted slave instead. */
// ��������� master �����ߣ����Ҷ���� master �Ĺ���Ǩ�Ʋ����Ѿ����ʱ����
// ��� master �ᱻ�Ƴ��� master ��񣬲����µ�������������
void sentinelFailoverSwitchToPromotedSlave(sentinelRedisInstance *master) {

    /// ѡ��Ҫ��ӵ� master
    sentinelRedisInstance *ref = master->promoted_slave ?
                                 master->promoted_slave : master;

    // ���͸��� master �¼�
    sentinelEvent(REDIS_WARNING,"+switch-master",master,"%s %s %d %s %d",
        // ԭ master ��Ϣ
        master->name, master->addr->ip, master->addr->port,
        // �� master ��Ϣ
        ref->addr->ip, ref->addr->port);

    // ����������������Ϣ����ԭ master ����Ϣ
    sentinelResetMasterAndChangeAddress(master,ref->addr->ip,ref->addr->port);
}

// ִ�й���ת��
void sentinelFailoverStateMachine(sentinelRedisInstance *ri) {
    redisAssert(ri->flags & SRI_MASTER);

    // master δ�������ת��״̬��ֱ�ӷ���
    if (!(ri->flags & SRI_FAILOVER_IN_PROGRESS)) return;

    switch(ri->failover_state) {

        // �ȴ�����ת�ƿ�ʼ
        case SENTINEL_FAILOVER_STATE_WAIT_START:
            sentinelFailoverWaitStart(ri);
            break;

        // ѡ������������
        case SENTINEL_FAILOVER_STATE_SELECT_SLAVE:
            sentinelFailoverSelectSlave(ri);
            break;
        
        // ������ѡ�еĴӷ�����Ϊ����������
        case SENTINEL_FAILOVER_STATE_SEND_SLAVEOF_NOONE:
            sentinelFailoverSendSlaveOfNoOne(ri);
            break;

        // �ȴ�������Ч�����������ʱ����ô����ѡ������������
        // ��������뿴 sentinelRefreshInstanceInfo ����
        case SENTINEL_FAILOVER_STATE_WAIT_PROMOTION:
            sentinelFailoverWaitPromotion(ri);
            break;

        // ��ӷ��������� SLAVEOF ���������ͬ������������
        case SENTINEL_FAILOVER_STATE_RECONF_SLAVES:
            sentinelFailoverReconfNextSlave(ri);
            break;
    }
}

/* Abort a failover in progress:
 *
 * ������;ִֹͣ�й���ת�ƣ�
 *
 * This function can only be called before the promoted slave acknowledged
 * the slave -> master switch. Otherwise the failover can't be aborted and
 * will reach its end (possibly by timeout). 
 *
 * �������ֻ���ڱ�ѡ�еĴӷ���������Ϊ�µ���������֮ǰ���ã�
 * �������ת�ƾͲ�����;ֹͣ��
 * ���һ�һֱִ�е�������
 */
void sentinelAbortFailover(sentinelRedisInstance *ri) {
    redisAssert(ri->flags & SRI_FAILOVER_IN_PROGRESS);
    redisAssert(ri->failover_state <= SENTINEL_FAILOVER_STATE_WAIT_PROMOTION);

    // �Ƴ���ر�ʶ
    ri->flags &= ~(SRI_FAILOVER_IN_PROGRESS|SRI_FORCE_FAILOVER);

    // ���״̬
    ri->failover_state = SENTINEL_FAILOVER_STATE_NONE;
    ri->failover_state_change_time = mstime();

    // ���������������������ʶ
    if (ri->promoted_slave) {
        ri->promoted_slave->flags &= ~SRI_PROMOTED;
        // ����·�����
        ri->promoted_slave = NULL;
    }
}

/* ======================== SENTINEL timer handler ==========================
 * This is the "main" our Sentinel, being sentinel completely non blocking
 * in design. The function is called every second.
 * -------------------------------------------------------------------------- */

/* Perform scheduled operations for the specified Redis instance. */
// �Ը�����ʵ��ִ�ж��ڲ���
void sentinelHandleRedisInstance(sentinelRedisInstance *ri) {

    /* ========== MONITORING HALF ============ */
    /* ==========     ��ز���    =========*/

    /* Every kind of instance */
    /* ����������ʵ�����д��� */

    // �������Ҫ�Ļ�����������ʵ������������
    sentinelReconnectInstance(ri);

    // �����������ʵ������ PING�� INFO ���� PUBLISH ����
    sentinelSendPeriodicCommands(ri);

    /* ============== ACTING HALF ============= */
    /* ==============  ���ϼ��   ============= */

    /* We don't proceed with the acting half if we are in TILT mode.
     * TILT happens when we find something odd with the time, like a
     * sudden change in the clock. */
    // ��� Sentinel ���� TILT ģʽ����ô��ִ�й��ϼ�⡣
    if (sentinel.tilt) {

        // ��� TILI ģʽδ�������ô��ִ�ж���
        if (mstime()-sentinel.tilt_start_time < SENTINEL_TILT_PERIOD) return;

        // ʱ���ѹ����˳� TILT ģʽ
        sentinel.tilt = 0;
        sentinelEvent(REDIS_WARNING,"-tilt",NULL,"#tilt mode exited");
    }

    /* Every kind of instance */
    // ������ʵ���Ƿ���� SDOWN ״̬
    sentinelCheckSubjectivelyDown(ri);

    /* Masters and slaves */
    if (ri->flags & (SRI_MASTER|SRI_SLAVE)) {
        /* Nothing so far. */
    }

    /* Only masters */
    /* �������������д��� */
    if (ri->flags & SRI_MASTER) {

        // �ж� master �Ƿ���� ODOWN ״̬
        sentinelCheckObjectivelyDown(ri);

        // ����������������� ODOWN ״̬����ô��ʼһ�ι���ת�Ʋ���
        if (sentinelStartFailoverIfNeeded(ri))
            // ǿ�������� Sentinel ���� SENTINEL is-master-down-by-addr ����
            // ˢ������ Sentinel ��������������״̬
            sentinelAskMasterStateToOtherSentinels(ri,SENTINEL_ASK_FORCED);

        // ִ�й���ת��
        sentinelFailoverStateMachine(ri);

        // �������Ҫ�Ļ��������� Sentinel ���� SENTINEL is-master-down-by-addr ����
        // ˢ������ Sentinel ��������������״̬
        // ��һ���Ƕ���Щû�н��� if(sentinelStartFailoverIfNeeded(ri)) { /* ... */ }
        // ������������ʹ�õ�
        sentinelAskMasterStateToOtherSentinels(ri,SENTINEL_NO_FLAGS);
    }
}

/* Perform scheduled operations for all the instances in the dictionary.
 * Recursively call the function against dictionaries of slaves. */
// �Ա� Sentinel ���ӵ�����ʵ�������������������ӷ����������� Sentinel ��
// ���ж��ڲ���
//
//  sentinelHandleRedisInstance
//              |
//              |
//              v
//            master
//             /  \
//            /    \
//           v      v
//       slaves    sentinels
void sentinelHandleDictOfRedisInstances(dict *instances) {
    dictIterator *di;
    dictEntry *de;
    sentinelRedisInstance *switch_to_promoted = NULL;

    /* There are a number of things we need to perform against every master. */
    // �������ʵ������Щʵ�������Ƕ����������������ӷ��������߶�� sentinel
    di = dictGetIterator(instances);
    while((de = dictNext(di)) != NULL) {

        // ȡ��ʵ����Ӧ��ʵ���ṹ
        sentinelRedisInstance *ri = dictGetVal(de);

        // ִ�е��Ȳ���
        sentinelHandleRedisInstance(ri);

        // �������������������������ô�ݹ�ر������������������дӷ�����
        // �Լ����� sentinel
        if (ri->flags & SRI_MASTER) {

            // ���дӷ�����
            sentinelHandleDictOfRedisInstances(ri->slaves);

            // ���� sentinel
            sentinelHandleDictOfRedisInstances(ri->sentinels);

            // ������������������ri���Ĺ���Ǩ���Ѿ����
            // ri �����дӷ��������Ѿ�ͬ��������������
            if (ri->failover_state == SENTINEL_FAILOVER_STATE_UPDATE_CONFIG) {
                // ��ѡ���µ���������
                switch_to_promoted = ri;
            }
        }
    }

    // ��ԭ���������������ߣ�����������������Ƴ�����ʹ������������������
    if (switch_to_promoted)
        sentinelFailoverSwitchToPromotedSlave(switch_to_promoted);

    dictReleaseIterator(di);
}

/* This function checks if we need to enter the TITL mode.
 *
 * ���������� sentinel �Ƿ���Ҫ���� TITL ģʽ��
 *
 * The TILT mode is entered if we detect that between two invocations of the
 * timer interrupt, a negative amount of time, or too much time has passed.
 *
 * ������������ִ�� sentinel ֮���ʱ���Ϊ��ֵ�����߹���ʱ��
 * �ͻ���� TILT ģʽ��
 *
 * Note that we expect that more or less just 100 milliseconds will pass
 * if everything is fine. However we'll see a negative number or a
 * difference bigger than SENTINEL_TILT_TRIGGER milliseconds if one of the
 * following conditions happen:
 *
 * ͨ����˵������ִ�� sentinel ֮��Ĳ���� 100 �������ң�
 * ����������������ʱ�������Ϳ��ܻ�����쳣��
 *
 * 1) The Sentinel process for some time is blocked, for every kind of
 * random reason: the load is huge, the computer was frozen for some time
 * in I/O or alike, the process was stopped by a signal. Everything.
 *    sentinel ������ΪĳЩԭ������������������������̫�󣬼���� I/O �����أ�
 *    ���̱��ź�ֹͣ��������ࡣ
 *
 * 2) The system clock was altered significantly.
 *    ϵͳ��ʱ�Ӳ����˷ǳ����Եı仯��
 *
 * Under both this conditions we'll see everything as timed out and failing
 * without good reasons. Instead we enter the TILT mode and wait
 * for SENTINEL_TILT_PERIOD to elapse before starting to act again.
 *
 * �����������������ʱ�� sentinel ���ܻὫ�κ�ʵ������Ϊ���ߣ�����ԭ����ж�ʵ��ΪʧЧ��
 * Ϊ�˱������������������ sentinel ���� TILT ģʽ��
 * ֹͣ�����κζ��������ȴ� SENTINEL_TILT_PERIOD ���ӡ� 
 *
 * During TILT time we still collect information, we just do not act. 
 *
 * TILT ģʽ�µ� sentinel ��Ȼ����м�ز��ռ���Ϣ��
 * ��ֻ�ǲ�ִ���������ת�ơ������ж�֮��Ĳ������ѡ�
 */
void sentinelCheckTiltCondition(void) {

    // ���㵱ǰʱ��
    mstime_t now = mstime();

    // �����ϴ����� sentinel �͵�ǰʱ��Ĳ�
    mstime_t delta = now - sentinel.previous_time;

    // �����Ϊ���������ߴ��� 2 ���ӣ���ô���� TILT ģʽ
    if (delta < 0 || delta > SENTINEL_TILT_TRIGGER) {
        // �򿪱��
        sentinel.tilt = 1;
        // ��¼���� TILT ģʽ�Ŀ�ʼʱ��
        sentinel.tilt_start_time = mstime();
        // ��ӡ�¼�
        sentinelEvent(REDIS_WARNING,"+tilt",NULL,"#tilt mode entered");
    }

    // �������һ�� sentinel ����ʱ��
    sentinel.previous_time = mstime();
}

// sentinel ģʽ������������ redis.c/serverCron ��������
void sentinelTimer(void) {

    // ��¼���� sentinel ���õ��¼���
    // ���ж��Ƿ���Ҫ���� TITL ģʽ
    sentinelCheckTiltCondition();

    // ִ�ж��ڲ���
    // ���� PING ʵ�����������������ʹӷ������� INFO ����
    // ������������ͬ���������� sentinel �����ʺ���Ϣ
    // ���������� sentinel �������ʺ���Ϣ
    // ִ�й���ת�Ʋ������ȵ�
    sentinelHandleDictOfRedisInstances(sentinel.masters);

    // ���еȴ�ִ�еĽű�
    sentinelRunPendingScripts();

    // ������ִ����ϵĽű��������Գ���Ľű�
    sentinelCollectTerminatedScripts();

    // ɱ�����г�ʱ�Ľű�
    sentinelKillTimedoutScripts();

    /* We continuously change the frequency of the Redis "timer interrupt"
     * in order to desynchronize every Sentinel from every other.
     * This non-determinism avoids that Sentinels started at the same time
     * exactly continue to stay synchronized asking to be voted at the
     * same time again and again (resulting in nobody likely winning the
     * election because of split brain voting). */
    server.hz = REDIS_DEFAULT_HZ + rand() % REDIS_DEFAULT_HZ;
}
