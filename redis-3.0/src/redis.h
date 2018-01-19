/*
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

#ifndef __REDIS_H
#define __REDIS_H

#include "fmacros.h"
#include "config.h"

#if defined(__sun)
#include "solarisfixes.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <syslog.h>
#include <netinet/in.h>
#include <lua.h>
#include <signal.h>

#include "ae.h"      /* Event driven programming library */
#include "sds.h"     /* Dynamic safe strings */
#include "dict.h"    /* Hash tables */
#include "adlist.h"  /* Linked lists */
#include "zmalloc.h" /* total memory usage aware version of malloc/free */
#include "anet.h"    /* Networking the easy way */
#include "ziplist.h" /* Compact list data structure */
#include "intset.h"  /* Compact integer set structure */
#include "version.h" /* Version macro */
#include "util.h"    /* Misc functions useful in many places */

#define NGX_FUNC_LINE __FUNCTION__, __LINE__


/* Error codes */
#define REDIS_OK                0
#define REDIS_ERR               -1

/* Static server configuration */
/* Ĭ�ϵķ���������ֵ */
#define REDIS_DEFAULT_HZ        10      /* Time interrupt calls/sec. */
#define REDIS_MIN_HZ            1
#define REDIS_MAX_HZ            500 
#define REDIS_SERVERPORT        6379    /* TCP port */
#define REDIS_TCP_BACKLOG       511     /* TCP listen backlog */
#define REDIS_MAXIDLETIME       0       /* default client timeout: infinite */
#define REDIS_DEFAULT_DBNUM     16
#define REDIS_CONFIGLINE_MAX    1024
#define REDIS_DBCRON_DBS_PER_CALL 16
#define REDIS_MAX_WRITE_PER_EVENT (1024*64)
#define REDIS_SHARED_SELECT_CMDS 10
#define REDIS_SHARED_INTEGERS 10000
#define REDIS_SHARED_BULKHDR_LEN 32
#define REDIS_MAX_LOGMSG_LEN    1024 /* Default maximum length of syslog messages */
#define REDIS_AOF_REWRITE_PERC  100
#define REDIS_AOF_REWRITE_MIN_SIZE (64*1024*1024)

/*
 ��ʵ���У�Ϊ�˱�����ִ������ʱ��ɿͻ������뻺�����������д�����ڴ����б�
��ϣ�����ϡ����򼯺������ֿ��ܻ���ж��Ԫ�صļ�ʱ�����ȼ�����������Ԫ����
�������Ԫ�ص�����������redis��h/REDIS_AOF_REWRITE_ITEMS_PER_CMD������
ֵ����ô��д����ʹ�ö�����������¼����ֵ����������ʹ��һ�����
    ��Ŀǰ�汾�У�REDIS_AOF_REWRITE_ITEMS_PER_CMD������ֵΪ64����Ҳ����
˵�����һ���ںϼ������˳���64��Ԫ�أ���ô��д������ö���SADD��������¼���
���ϣ�����ÿ���������õ�Ԫ������ҲΪ64����
SADD <set-key> <eleml> <elem2> _ <elem64>
SADD <set-key> <elem65> <elem66> ~ ~ ~ <elem128>
SADD <set-key> <elem129> <elem130>  ~ ~ ~  <elem192>
*/
#define REDIS_AOF_REWRITE_ITEMS_PER_CMD 64
#define REDIS_SLOWLOG_LOG_SLOWER_THAN 10000
#define REDIS_SLOWLOG_MAX_LEN 128
#define REDIS_MAX_CLIENTS 10000
#define REDIS_AUTHPASS_MAX_LEN 512
#define REDIS_DEFAULT_SLAVE_PRIORITY 100
#define REDIS_REPL_TIMEOUT 60
#define REDIS_REPL_PING_SLAVE_PERIOD 10
#define REDIS_RUN_ID_SIZE 40
#define REDIS_OPS_SEC_SAMPLES 16
#define REDIS_DEFAULT_REPL_BACKLOG_SIZE (1024*1024)    /* 1mb */
#define REDIS_DEFAULT_REPL_BACKLOG_TIME_LIMIT (60*60)  /* 1 hour */
#define REDIS_REPL_BACKLOG_MIN_SIZE (1024*16)          /* 16k */
#define REDIS_BGSAVE_RETRY_DELAY 5 /* Wait a few secs before trying again. */
#define REDIS_DEFAULT_PID_FILE "/var/run/redis.pid"
#define REDIS_DEFAULT_SYSLOG_IDENT "redis"
#define REDIS_DEFAULT_CLUSTER_CONFIG_FILE "nodes.conf"
#define REDIS_DEFAULT_DAEMONIZE 0
#define REDIS_DEFAULT_UNIX_SOCKET_PERM 0
#define REDIS_DEFAULT_TCP_KEEPALIVE 0
#define REDIS_DEFAULT_LOGFILE ""
#define REDIS_DEFAULT_SYSLOG_ENABLED 0
#define REDIS_DEFAULT_STOP_WRITES_ON_BGSAVE_ERROR 1
#define REDIS_DEFAULT_RDB_COMPRESSION 1
#define REDIS_DEFAULT_RDB_CHECKSUM 1
#define REDIS_DEFAULT_RDB_FILENAME "dump.rdb"
#define REDIS_DEFAULT_SLAVE_SERVE_STALE_DATA 1
#define REDIS_DEFAULT_SLAVE_READ_ONLY 1
#define REDIS_DEFAULT_REPL_DISABLE_TCP_NODELAY 0
#define REDIS_DEFAULT_MAXMEMORY 0
#define REDIS_DEFAULT_MAXMEMORY_SAMPLES 5
#define REDIS_DEFAULT_AOF_FILENAME "appendonly.aof"
#define REDIS_DEFAULT_AOF_NO_FSYNC_ON_REWRITE 0
#define REDIS_DEFAULT_ACTIVE_REHASHING 1
#define REDIS_DEFAULT_AOF_REWRITE_INCREMENTAL_FSYNC 1
#define REDIS_DEFAULT_MIN_SLAVES_TO_WRITE 0
#define REDIS_DEFAULT_MIN_SLAVES_MAX_LAG 10
#define REDIS_IP_STR_LEN INET6_ADDRSTRLEN
#define REDIS_PEER_ID_LEN (REDIS_IP_STR_LEN+32) /* Must be enough for ip:port */
#define REDIS_BINDADDR_MAX 16
#define REDIS_MIN_RESERVED_FDS 32

#define ACTIVE_EXPIRE_CYCLE_LOOKUPS_PER_LOOP 20 /* Loopkups per loop. */
#define ACTIVE_EXPIRE_CYCLE_FAST_DURATION 1000 /* Microseconds */
#define ACTIVE_EXPIRE_CYCLE_SLOW_TIME_PERC 25 /* CPU max % for keys collection */
#define ACTIVE_EXPIRE_CYCLE_SLOW 0
#define ACTIVE_EXPIRE_CYCLE_FAST 1

/* Protocol and I/O related defines */
#define REDIS_MAX_QUERYBUF_LEN  (1024*1024*1024) /* 1GB max query buffer. */
#define REDIS_IOBUF_LEN         (1024*16)  /* Generic I/O buffer size */
#define REDIS_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */
#define REDIS_INLINE_MAX_SIZE   (1024*64) /* Max size of inline reads */
#define REDIS_MBULK_BIG_ARG     (1024*32)
#define REDIS_LONGSTR_SIZE      21          /* Bytes needed for long -> str */
// ָʾ AOF ����ÿ�ۻ��������д������
// ��ִ��һ����ʽ�� fsync
#define REDIS_AOF_AUTOSYNC_BYTES (1024*1024*32) /* fdatasync every 32MB */
/* When configuring the Redis eventloop, we setup it so that the total number
 * of file descriptors we can handle are server.maxclients + RESERVED_FDS + FDSET_INCR
 * that is our safety margin. */
#define REDIS_EVENTLOOP_FDSET_INCR (REDIS_MIN_RESERVED_FDS+96)

/* Hash table parameters */
#define REDIS_HT_MINFILL        10      /* Minimal hash table fill 10% */

/* Command flags. Please check the command table defined in the redis.c file
 * for more information about the meaning of every flag. */
// �����־
#define REDIS_CMD_WRITE 1                   /* "w" flag */
#define REDIS_CMD_READONLY 2                /* "r" flag */
#define REDIS_CMD_DENYOOM 4                 /* "m" flag */
#define REDIS_CMD_NOT_USED_1 8              /* no longer used flag */
#define REDIS_CMD_ADMIN 16                  /* "a" flag */
#define REDIS_CMD_PUBSUB 32                 /* "p" flag */
#define REDIS_CMD_NOSCRIPT  64              /* "s" flag */
#define REDIS_CMD_RANDOM 128                /* "R" flag */
#define REDIS_CMD_SORT_FOR_SCRIPT 256       /* "S" flag */
#define REDIS_CMD_LOADING 512               /* "l" flag */
#define REDIS_CMD_STALE 1024                /* "t" flag */
#define REDIS_CMD_SKIP_MONITOR 2048         /* "M" flag */
#define REDIS_CMD_ASKING 4096               /* "k" flag */

/*
 * ��������   �����ȡֵ�洢��redisObject->type

 
���� Redis ���ݿⱣ��ļ�ֵ����˵�� ������һ���ַ������� ��ֵ��������ַ��������б���󡢹�ϣ���󡢼��϶���������򼯺϶��������һ�֣� ��ˣ�
�����ǳƺ�һ�����ݿ��Ϊ���ַ�������ʱ�� ����ָ���ǡ�������ݿ������Ӧ��ֵΪ�ַ������󡱣�
�����ǳƺ�һ����Ϊ���б����ʱ�� ����ָ���ǡ�������ݿ������Ӧ��ֵΪ�б���󡱣�
 
TYPE �����ʵ�ַ�ʽҲ������ƣ� �����Ƕ�һ�����ݿ��ִ�� TYPE ����ʱ�� ����صĽ��Ϊ���ݿ����Ӧ��ֵ��������ͣ� �����Ǽ���������ͣ�
# ��Ϊ�ַ�������ֵΪ�ַ�������
 
 redis> SET msg "hello world"
 OK
 
 redis> TYPE msg
 string
 
# ��Ϊ�ַ�������ֵΪ�б����
 
 redis> RPUSH numbers 1 3 5
 (integer) 6
 
 redis> TYPE numbers
 list
 
# ��Ϊ�ַ�������ֵΪ��ϣ����
 
 redis> HMSET profile name Tome age 25 career Programmer
 OK
 
 redis> TYPE profile
 hash
 
# ��Ϊ�ַ�������ֵΪ���϶���
 
 redis> SADD fruits apple banana cherry
 (integer) 3
 
 redis> TYPE fruits
 set
 
# ��Ϊ�ַ�������ֵΪ���򼯺϶���
 
 redis> ZADD price 8.5 apple 5.0 banana 6.0 cherry
 (integer) 3
 
 redis> TYPE price
 zset

 */

/*

����                ����                    ����

REDIS_STRING    REDIS_ENCODING_INT ʹ������ֵʵ�ֵ��ַ������� 
REDIS_STRING    REDIS_ENCODING_EMBSTR ʹ�� embstr ����ļ򵥶�̬�ַ���ʵ�ֵ��ַ������� 
REDIS_STRING    REDIS_ENCODING_RAW ʹ�ü򵥶�̬�ַ���ʵ�ֵ��ַ������� 
REDIS_LIST      REDIS_ENCODING_ZIPLIST ʹ��ѹ���б�ʵ�ֵ��б���� 
REDIS_LIST      REDIS_ENCODING_LINKEDLIST ʹ��˫������ʵ�ֵ��б���� 
REDIS_HASH      REDIS_ENCODING_ZIPLIST ʹ��ѹ���б�ʵ�ֵĹ�ϣ���� 
REDIS_HASH      REDIS_ENCODING_HT ʹ���ֵ�ʵ�ֵĹ�ϣ���� 
REDIS_SET       REDIS_ENCODING_INTSET ʹ����������ʵ�ֵļ��϶��� 
REDIS_SET       REDIS_ENCODING_HT ʹ���ֵ�ʵ�ֵļ��϶��� 
REDIS_ZSET      REDIS_ENCODING_ZIPLIST ʹ��ѹ���б�ʵ�ֵ����򼯺϶��� 
REDIS_ZSET      REDIS_ENCODING_SKIPLIST ʹ����Ծ����ֵ�ʵ�ֵ����򼯺϶��� 
*/

/* Object types */
// ��������
/*
�ַ�������ı�������� REDIS_ENCODING_RAW ���� REDIS_ENCODING_EMBSTR ����REDIS_ENCODING_INT �� ������ַ������֣���ΪREDIS_ENCODING_INT
����ַ������󱣴����һ���ַ���ֵ�� ��������ַ���ֵ�ĳ��ȴ��� 39 �ֽڣ� ���ñ��뷽ʽREDIS_ENCODING_EMBSTR��������ñ��뷽ʽREDIS_ENCODING_RAW
*/
#define REDIS_STRING 0 //�ο�set�����setCommand����ִ������

/*
�б����ı��뷽ʽ������REDIS_ENCODING_LINKEDLIST˫���б��REDIS_ENCODING_ZIPLISTѹ����Ĭ��REDIS_ENCODING_ZIPLIST,����������������ʱ
��ʹ��˫���б���뷽ʽ��

���б�������ͬʱ����������������ʱ�� �б����ʹ�� ziplist ���룺
1.�б���󱣴�������ַ���Ԫ�صĳ��ȶ�С�� 64 �ֽڣ�
2.�б���󱣴��Ԫ������С�� 512 ����

���������������������б������Ҫʹ�� linkedlist ���롣
*/
#define REDIS_LIST 1 //�ο�lpush�����lpushCommand����ִ������

/*
���϶�����뷽ʽ֧��REDIS_ENCODING_INTSET��REDIS_ENCODING_HT 

���sadd�������������Ҽ���Ԫ�ظ���������512�������REDIS_ENCODING_INTSET  ����REDIS_ENCODING_HT

�����϶������ͬʱ����������������ʱ�� ����ʹ�� intset ���룺
1.���϶��󱣴������Ԫ�ض�������ֵ��
2.���϶��󱣴��Ԫ������������ 512 ����

�������������������ļ��϶�����Ҫʹ�� hashtable ���롣

*/
#define REDIS_SET 2//�ο�sadd�����saddCommand����ִ������

/*
����֧��ѹ����REDIS_ENCODING_ZIPLIST����Ծ��REDIS_ENCODING_SKIPLIST���뷽ʽ�� Ĭ��ѹ����

�����ת��:

�����򼯺϶������ͬʱ����������������ʱ�� ����ʹ�� ziplist ���룺
1.���򼯺ϱ����Ԫ������С�� 128 ����
2.���򼯺ϱ��������Ԫ�س�Ա�ĳ��ȶ�С�� 64 �ֽڣ�

�������������������������򼯺϶���ʹ�� skiplist ���롣

*/
#define REDIS_ZSET 3//�ο�zadd�����zaddCommand����ִ������

/*
��ϣ����ı�������� ziplist(REDIS_ENCODING_ZIPLIST) ���� hashtable(REDIS_ENCODING_HT) ��
����ת��: Ĭ��ʹ��REDIS_ENCODING_ZIPLIST���뷽ʽ������������������Ϊ��REDIS_ENCODING_HT���뷽ʽ��

����ϣ�������ͬʱ����������������ʱ�� ��ϣ����ʹ�� ziplist ���룺
1.��ϣ���󱣴�����м�ֵ�Եļ���ֵ���ַ������ȶ�С�� 64 �ֽڣ�
2.��ϣ���󱣴�ļ�ֵ������С�� 512 ����

�������������������Ĺ�ϣ������Ҫʹ�� hashtable ���롣
*/

#define REDIS_HASH 4

/*
����ı���                  

   ���볣��                         ��������Ӧ�ĵײ����ݽṹ
 REDIS_ENCODING_INT                     long ���͵����� 
 REDIS_ENCODING_EMBSTR                  embstr ����ļ򵥶�̬�ַ��� 
 REDIS_ENCODING_RAW                     �򵥶�̬�ַ��� 
 REDIS_ENCODING_HT                      �ֵ� 
 REDIS_ENCODING_LINKEDLIST              ˫������ 
 REDIS_ENCODING_ZIPLIST                 ѹ���б� 
 REDIS_ENCODING_INTSET                  �������� 
 REDIS_ENCODING_SKIPLIST                ��Ծ��

 REDIS_ENCODING_EMBSTR��REDIS_ENCODING_RAW����?
 REDIS_ENCODING_RAW:����ַ������󱣴����һ���ַ���ֵ�� ��������ַ���ֵ�ĳ��ȴ��� 39 �ֽڣ� ��ô�ַ�������ʹ��һ���򵥶�̬�ַ�����SDS��
                    ����������ַ���ֵ�� ��������ı�������Ϊ raw ��
 REDIS_ENCODING_EMBSTR:����ַ������󱣴����һ���ַ���ֵ�� ��������ַ���ֵ�ĳ���С�ڵ��� 39 �ֽڣ� ��ô�ַ�������ʹ�� embstr ����ķ�ʽ��
                       ��������ַ���ֵ��

    embstr ������ר�����ڱ�����ַ�����һ���Ż����뷽ʽ�� ���ֱ���� raw ����һ���� ��ʹ�� redisObject �ṹ�� sdshdr �ṹ����ʾ�ַ������� 
 �� raw �������������ڴ���亯�����ֱ𴴽� redisObject �ṹ�� sdshdr �ṹ�� �� embstr ������ͨ������һ���ڴ���亯��������һ��������
 �ռ䣬 �ռ������ΰ��� redisObject �� sdshdr �����ṹ��

 embstr ������ַ���������ִ������ʱ�� ������Ч���� raw ������ַ�������ִ������ʱ������Ч������ͬ�ģ� ��ʹ�� embstr ������ַ���������������ַ���ֵ�����ºô���
 1.embstr ���뽫�����ַ�������������ڴ��������� raw ��������ν���Ϊһ�Ρ�
 2.�ͷ� embstr ������ַ�������ֻ��Ҫ����һ���ڴ��ͷź����� ���ͷ� raw ������ַ���������Ҫ���������ڴ��ͷź�����
 3.��Ϊ embstr ������ַ���������������ݶ�������һ���������ڴ����棬 �������ֱ�����ַ���������� raw ������ַ��������ܹ����õ����û�����������ơ�

    long double ���ͱ�ʾ�ĸ������� Redis ��Ҳ����Ϊ�ַ���ֵ������ģ� �������Ҫ����һ�����������ַ����������棬 ��ô�����
�Ƚ����������ת�����ַ���ֵ�� Ȼ���ٱ�����ת�����õ��ַ���ֵ��

    �ٸ����ӣ� ִ�����´��뽫����һ������ 3.14 ���ַ�����ʾ "3.14" ���ַ�������
 redis> SET pi 3.14
 OK
 redis> OBJECT ENCODING pi
 "embstr"


 ÿ�����͵Ķ�������ʹ�������ֲ�ͬ�ı��룬 
��ͬ���ͺͱ���Ķ���
 
 ����                       ����                                ����
 REDIS_STRING           REDIS_ENCODING_INT              ʹ������ֵʵ�ֵ��ַ������� 
 REDIS_STRING           REDIS_ENCODING_EMBSTR           ʹ�� embstr ����ļ򵥶�̬�ַ���ʵ�ֵ��ַ������� 
 REDIS_STRING           REDIS_ENCODING_RAW              ʹ�ü򵥶�̬�ַ���ʵ�ֵ��ַ������� 
 
 REDIS_LIST             REDIS_ENCODING_ZIPLIST          ʹ��ѹ���б�ʵ�ֵ��б���� 
 REDIS_LIST             REDIS_ENCODING_LINKEDLIST       ʹ��˫������ʵ�ֵ��б���� 
 
 REDIS_HASH             REDIS_ENCODING_ZIPLIST          ʹ��ѹ���б�ʵ�ֵĹ�ϣ���� 
 REDIS_HASH             REDIS_ENCODING_HT               ʹ���ֵ�ʵ�ֵĹ�ϣ����
 
 REDIS_SET              REDIS_ENCODING_INTSET           ʹ����������ʵ�ֵļ��϶��� 
 REDIS_SET              REDIS_ENCODING_HT               ʹ���ֵ�ʵ�ֵļ��϶��� 
 
 REDIS_ZSET             REDIS_ENCODING_ZIPLIST          ʹ��ѹ���б�ʵ�ֵ����򼯺϶��� 
 REDIS_ZSET             REDIS_ENCODING_SKIPLIST         ʹ����Ծ����ֵ�ʵ�ֵ����򼯺϶��� 
 
 ʹ�� OBJECT ENCODING ������Բ鿴һ�����ݿ����ֵ����ı��룺
 
 OBJECT ENCODING �Բ�ͬ��������:
 
 ������ʹ�õĵײ����ݽṹ                   ���볣��                                OBJECT ENCODING �������
 
    ����                               REDIS_ENCODING_INT           "int"           createStringObjectFromLongLong createIntsetObject tryObjectEncoding
embstr ����ļ򵥶�̬�ַ�����SDS��     REDIS_ENCODING_EMBSTR        "embstr"        createEmbeddedStringObject
�򵥶�̬�ַ���                         REDIS_ENCODING_RAW           "raw"           createObject
�ֵ�                                   REDIS_ENCODING_HT            "hashtable"     createSetObject  hashTypeConvertZiplist
˫������                               REDIS_ENCODING_LINKEDLIST    "linkedlist"    createListObject
ѹ���б�                               REDIS_ENCODING_ZIPLIST       "ziplist"       createZiplistObject createHashObject createZsetZiplistObject
��������                               REDIS_ENCODING_INTSET        "intset"        createIntsetObject
��Ծ����ֵ�                           REDIS_ENCODING_SKIPLIST      "skiplist"      createZsetObject


    ͨ�� encoding �������趨������ʹ�õı��룬 ������Ϊ�ض����͵Ķ������һ�̶ֹ��ı��룬 ����������� Redis ������Ժ�Ч�ʣ� ��Ϊ 
Redis ���Ը��ݲ�ͬ��ʹ�ó�����Ϊһ���������ò�ͬ�ı��룬 �Ӷ��Ż�������ĳһ�����µ�Ч�ʡ�
 
�ٸ����ӣ� ���б���������Ԫ�رȽ���ʱ�� Redis ʹ��ѹ���б���Ϊ�б����ĵײ�ʵ�֣�
��Ϊѹ���б��˫���������Լ�ڴ棬 ������Ԫ����������ʱ�� ���ڴ����������鷽ʽ�����ѹ���б����˫��������Ը��챻���뵽�����У�
�����б���������Ԫ��Խ��Խ�࣬ ʹ��ѹ���б�������Ԫ�ص���������ʧʱ�� ����ͻὫ�ײ�ʵ�ִ�ѹ���б�ת���ܸ�ǿ��Ҳ���ʺϱ������Ԫ�ص�˫���������棻


 * �������
 *
 * �� String �� Hash �����Ķ��󣬿����ж����ڲ���ʾ��
 * ����� encoding ���Կ�������Ϊ�����������һ�֡�
 */


/* Objects encoding. Some kind of objects like Strings and Hashes can be
 * internally represented in multiple ways. The 'encoding' field of the object
 * is set to one of this fields for this object. */
// �������


/* �ο�5������REDIS_STRING���Ǳ߽��ͱȽϺ� */


/*
����ַ������󱣴����һ���ַ���ֵ�� ��������ַ���ֵ�ĳ��ȴ��� 39 �ֽڣ� ��ô�ַ�������ʹ��һ���򵥶�̬�ַ�����SDS������������ַ���ֵ�� ��������ı�������Ϊ raw ��
*/
#define REDIS_ENCODING_RAW 0     /* Raw representation */ //REDIS_ENCODING_EMBSTR��REDIS_ENCODING_RAW���뷽ʽ�����createStringObject
//���key ����value�ַ�������С��39�ֽ� REDIS_ENCODING_EMBSTR_SIZE_LIMIT����ʹ�ø����ͱ��룬��createStringObject
#define REDIS_ENCODING_EMBSTR 8  /* Embedded sds string encoding */  //REDIS_ENCODING_EMBSTR��REDIS_ENCODING_RAW���뷽ʽ�����createStringObject


#define REDIS_ENCODING_INT 1     /* Encoded as integer */
#define REDIS_ENCODING_HT 2      /* Encoded as hash table */
//Redis������zipmap���ݽṹ����֤��hashtable�մ����Լ�Ԫ�ؽ���ʱ���ø��ٵ��ڴ����洢��ͬʱ�Բ�ѯ��Ч��Ҳ������̫���Ӱ��
#define REDIS_ENCODING_ZIPMAP 3  /* Encoded as zipmap */

/*
����ת��

���б�������ͬʱ����������������ʱ�� �б����ʹ�� ziplist ���룺
1.�б���󱣴�������ַ���Ԫ�صĳ��ȶ�С�� 64 �ֽڣ�
2.�б���󱣴��Ԫ������С�� 512 ����

���������������������б������Ҫʹ�� linkedlist ���롣

lpush����ͨ��pushGenericCommand->createZiplistObject�����б����(Ĭ�ϱ��뷽ʽREDIS_ENCODING_ZIPLIST)��Ȼ����listTypePush->listTypeTryConversion��
�����б��нڵ����Ƿ�������ò���list_max_ziplist_value(Ĭ��64)�����������value�б�ֵ��ѹ�����Ϊ˫��������뷽ʽREDIS_ENCODING_LINKEDLIST����listTypePush->listTypeConvert
*/
#define REDIS_ENCODING_LINKEDLIST 4 /* Encoded as regular linked list */
/*
lpush����ͨ��pushGenericCommand->createZiplistObject�����б����(Ĭ�ϱ��뷽ʽREDIS_ENCODING_ZIPLIST)��Ȼ����listTypePush->listTypeTryConversion��
�����б��нڵ��ַ��������Ƿ�������ò���list_max_ziplist_value(Ĭ��64)�����������value�б�ֵ��ѹ�����Ϊ˫��������뷽ʽREDIS_ENCODING_LINKEDLIST����listTypePush->listTypeConvert
*/
#define REDIS_ENCODING_ZIPLIST 5 /* Encoded as ziplist */



#define REDIS_ENCODING_INTSET 6  /* Encoded as intset */
//skiplist ��������򼯺϶���ʹ�� zset �ṹ��Ϊ�ײ�ʵ�֣� һ�� zset �ṹͬʱ����һ���ֵ��һ����Ծ��
#define REDIS_ENCODING_SKIPLIST 7  /* Encoded as skiplist */

/* Defines related to the dump file format. To store 32 bits lengths for short
 * keys requires a lot of space, so we check the most significant 2 bits of
 * the first byte to interpreter the length:
 *
 * 00|000000 => if the two MSB are 00 the len is the 6 bits of this byte
 * 01|000000 00000000 =>  01, the len is 14 byes, 6 bits + 8 bits of next byte
 * 10|000000 [32 bit integer] => if it's 10, a full 32 bit len will follow
 * 11|000000 this means: specially encoded object will follow. The six bits
 *           number specify the kind of object that follows.
 *           See the REDIS_RDB_ENC_* defines.
 *
 * Lengths up to 63 are stored using a single byte, most DB keys, and may
 * values, will fit inside. */
#define REDIS_RDB_6BITLEN 0
#define REDIS_RDB_14BITLEN 1
#define REDIS_RDB_32BITLEN 2
#define REDIS_RDB_ENCVAL 3
#define REDIS_RDB_LENERR UINT_MAX

/* When a length of a string object stored on disk has the first two bits
 * set, the remaining two bits specify a special encoding for the object
 * accordingly to the following defines: */
#define REDIS_RDB_ENC_INT8 0        /* 8 bit signed integer */
#define REDIS_RDB_ENC_INT16 1       /* 16 bit signed integer */
#define REDIS_RDB_ENC_INT32 2       /* 32 bit signed integer */
#define REDIS_RDB_ENC_LZF 3         /* string compressed with FASTLZ */

/* AOF states */
#define REDIS_AOF_OFF 0             /* AOF is off */
#define REDIS_AOF_ON 1              /* AOF is on */  //��Ҫ��������AOF��д
#define REDIS_AOF_WAIT_REWRITE 2    /* AOF waits rewrite to start appending */

/* Client flags */
/*
�����ӷ��������и��Ʋ���ʱ�������������Ϊ�ӷ������Ŀͻ��ˣ����ӷ�����Ҳ���Ϊ���������Ŀͻ��ˡ�REDIS_MASTER��־��ʾ�ͻ��˴�
�����һ������������REDIS��SLAVE��־��ʾ�ͻ��˴������һ���ӷ�������
*/
//��monitorCommand  masterTryPartialResynchronization  syncCommand�����ط���redisClient->flag����ΪREDIS_SLAVE
#define REDIS_SLAVE (1<<0)   /* This client is a slave server */
#define REDIS_MASTER (1<<1)  /* This client is a master server */ //�������ͻ���Ϊ������������readSyncBulkPayload
//��־��ʾ�ͻ�������ִ��MONITOR���
#define REDIS_MONITOR (1<<2) /* This client is a slave monitor, see MONITOR */
//��ʾ�ͻ�������ִ������ //ִ��multi�����ʱ����multiCommand�����øñ�ǣ���ʾ������ж���������룬ֻ�е�exec�������ִ���м����� multiCommand
#define REDIS_MULTI (1<<3)   /* This client is in a MULTI context */
//��ʾ�ͻ������ڱ�BRPOP��BLPOP������������
#define REDIS_BLOCKED (1<<4) /* The client is waiting in a blocking operation */
/*
REDIS_DIRTY_EXEC��ʾ�����������˶�ʱ�����˴���REDIS_DIRTY_CAS  REDIS_DIRTY_EXEC������־����ʾ����İ�ȫ���Ѿ����ƻ���ֻҪ
����������е�����һ�����򿪣�EXEC�����Ȼ��ִ��ʧ�ܡ���������־ֻ���ڿͻ��˴���REDIS_MULTI��־�������ʹ�á�
*/
//���ӻ��ƴ�����touchWatchedKey�����Ƿ񴥷�REDIS_DIRTY_CAS   ȡ�����ﺯ������watch�ļ��Ƿ��д���REDIS_DIRTY_CAS�������Ƿ����ִ�������е������execCommand
//��ʾ����ʹ��WATCH������ӵ����ݿ���Ѿ����޸ģ���touchWatchedKey  ��Ч��execCommand //ȡ��watch��unwatchAllKeys
#define REDIS_DIRTY_CAS (1<<5) /* Watched keys modified. EXEC will fail. */
/*
��ʾ���û�������ͻ���ִ����CLIENT KILL������߿ͻ��˷��͸������������������а����˴����Э�����ݡ��������Ὣ
�ͻ��˻���������������е��������ݷ��͸��ͻ��ˣ�ɷ��رտͻ��ˡ�
*/
#define REDIS_CLOSE_AFTER_REPLY (1<<6) /* Close after writing entire reply. */
//��ʾ�ͻ����Ѿ���REDIS��BLOCKED��־����ʾ������״̬���������������������REDIS��UNBLOCKED��־ֻ����REDIS��BLOCKED��־�Ѿ��򿪵������ʹ�á�
#define REDIS_UNBLOCKED (1<<7) /* This client was unblocked and is stored in
                                  server.unblocked_clients */
//REDIS_LUA_CLIENT��ʶ��ʾ�ͻ�����ר�����ڴ���Lua�ű����������Redis�����α�ͻ��ˡ�
#define REDIS_LUA_CLIENT (1<<8) /* This is a non connected client used by Lua */
//��ʾ�ͻ�����Ⱥ�ڵ㣨�����ڼ�Ⱥģʽ�µķ�������������ASKING���  REDIS_ASKING�Ǹ�һ���Ա�ʶ�����ڵ�ִ���˴��иñ�ʶ�Ŀͻ��˵��������Ƴ��ñ�ʶ
//ע��MOVED��ASING������
#define REDIS_ASKING (1<<9)     /* Client issued the ASKING command */
/*
��־��ʾ�ͻ��˵������������С�����˷���������ķ�Χ��������������һ��ִ��serverCron����ʱ�ر�����ͻ��ˣ�������������ȶ���
�ܵ�����ͻ���Ӱ�졣����������������е��������ݻ�ֱ�ӱ��ͷţ����᷵�ظ��ͻ��ˡ�
*/
#define REDIS_CLOSE_ASAP (1<<10)/* Close this client ASAP */
//��־��ʾ������ʹ��UNIX�׽��������ӿͻ��ˡ�
#define REDIS_UNIX_SOCKET (1<<11) /* Client connected via Unix domain socket */
/*
REDIS_DIRTY_EXEC��ʾ�����������˶�ʱ�����˴���REDIS_DIRTY_CAS  REDIS_DIRTY_EXEC������־����ʾ����İ�ȫ���Ѿ����ƻ���ֻҪ����������е�����һ�����򿪣�
EXEC�����Ȼ��ִ��ʧ�ܡ���������־ֻ���ڿͻ��˴���REDIS_MULTI��־�������ʹ�á�
*/ //�����������ʱ����1����flagTransaction,��Ҫ��û�ҵ�redisCommandTable�е�����
#define REDIS_DIRTY_EXEC (1<<12)  /* EXEC will fail for errors while queueing */
/*
�����ӷ���������������ڼ䣬�ӷ�������Ҫ��������������REPLICATION ACK����ڷ����������֮ǰ���ӷ��������������������Ӧ��
�ͻ��˵�REDIS_MASTER_FORCE_REPLY��־�������Ͳ����ᱻ�ܾ�ִ�С�
*/
#define REDIS_MASTER_FORCE_REPLY (1<<13)  /* Queue replies even if is master */
/*
    ͨ������£�Redisֻ�Ὣ��Щ�����ݿ�������޸ĵ�����д�뵽AOF�ļ��������Ƶ������ӷ����������һ������û�ж����ݿ�����κ��޸ģ���ô
���ͻᱻ��Ϊ��ֻ������������ᱻд�뵽AOF�İ飬Ҳ���ᱻ���Ƶ��ӷ�������
    ���Ϲ��������ھ��󲿷�Redis�����PUBSUB�����SCRIPT LOAD���������е����⡣PUBSUB������Ȼû���޸����ݿ⣬��PUBSUB������Ƶ����
���ж����߷�����Ϣ��һ��Ϊ���и����ã����յ���Ϣ�����пͻ��˵�״̬������Ϊ���������ı䡣��ˣ���������Ҫʹ��REDIS_FORCE_AOF��־��
ǿ�ƽ��������д��AOF�ļ��������ڽ�������AOF�ļ�ʱ���������Ϳ����ٴ�ִ����ͬ��PUBSUB�����������ͬ�ĸ����á�SCRIPT LOAD��������
��PUBSUB��������
*/
#define REDIS_FORCE_AOF (1<<14)   /* Force AOF propagation of current cmd. */
/* ǿ�Ʒ���������ǰִ�е�����д�뵽AOF�ļ����棬REDIS_FORCE_REPL��־ǿ��������������ǰִ�е�����Ƹ����дӷ�������
ִ��PUBSUB�����ʹ�ͻ��˴�REDIS_FORCE_AOF��־��ִ��SCRIPT LOAD�����ʹ�ͻ��˴�REDIS_FORCE_AOF��־��REDIS_FORCE_REPL��־�� */
#define REDIS_FORCE_REPL (1<<15)  /* Force replication of current cmd. */
//��ô��ʾ���������İ汾���� 2.8 
// �޷�ʹ�� PSYNC ��������Ҫ������Ӧ�ı�ʶֵ
#define REDIS_PRE_PSYNC (1<<16)   /* Instance don't understand PSYNC. */
#define REDIS_READONLY (1<<17)    /* Cluster client is in read-only state. */

/* Client block type (btype field in client structure)
 * if REDIS_BLOCKED flag is set. */
#define REDIS_BLOCKED_NONE 0    /* Not blocked, no REDIS_BLOCKED flag set. */
#define REDIS_BLOCKED_LIST 1    /* BLPOP & co. */
#define REDIS_BLOCKED_WAIT 2    /* WAIT for synchronous replication. */

/* Client request types */
#define REDIS_REQ_INLINE 1 
#define REDIS_REQ_MULTIBULK 2 //һ��ͻ��˷��͵������еĵ�һ���ֽ���*

/* Client classes for client limits, currently used only for
 * the max-client-output-buffer limit implementation. */
#define REDIS_CLIENT_LIMIT_CLASS_NORMAL 0
#define REDIS_CLIENT_LIMIT_CLASS_SLAVE 1
#define REDIS_CLIENT_LIMIT_CLASS_PUBSUB 2
#define REDIS_CLIENT_LIMIT_NUM_CLASSES 3

/* Slave replication state - from the point of view of the slave. */

//REDIS_REPL_CONNECT->REDIS_REPL_CONNECTING->REDIS_REPL_CONNECTED
#define REDIS_REPL_NONE 0 /* No active replication */
//�ᴥ��������������connectWithMaster
#define REDIS_REPL_CONNECT 1 /* Must connect to master */ //��replicationSetMaster  ��ʾδ����״̬
#define REDIS_REPL_CONNECTING 2 /* Connecting to master */ //��connectWithMaster
#define REDIS_REPL_RECEIVE_PONG 3 /* Wait for PING reply */ //�ӷ������������������ɹ�����ping�ַ���������������Ȼ������״̬����ʾ�ȴ���������Ӧ��PONG
#define REDIS_REPL_TRANSFER 4 /* Receiving .rdb from master */
#define REDIS_REPL_CONNECTED 5 /* Connected to master */ //������״̬������syn��ɣ�������״̬

/* Slave replication state - from the point of view of the master.
 * In SEND_BULK and ONLINE state the slave receives new updates
 * in its output queue. In the WAIT_BGSAVE state instead the server is waiting
 * to start the next background saving in order to send updates to it. */
#define REDIS_REPL_WAIT_BGSAVE_START 6 /* We need to produce a new RDB file. */
//��ʾ�Ѿ������ӽ���������bgsave�ˣ��ȴ�д�ɹ�������
#define REDIS_REPL_WAIT_BGSAVE_END 7 /* Waiting RDB file creation to finish. */
//rdb�ļ���д�ɹ��󣬿�ʼ�������紫�䣬��rdb�ļ����䵽slave�ڵ㣬��updateSlavesWaitingBgsave
#define REDIS_REPL_SEND_BULK 8 /* Sending RDB file to slave. */ //��ʼ����bgsave������ļ����ӷ������ˣ���updateSlavesWaitingBgsave
#define REDIS_REPL_ONLINE 9 /* RDB file transmitted, sending just updates. */ //дrdb�ļ����ݵ��ͻ�����ɣ���sendBulkToSlave

/* Synchronous read timeout - slave side */
#define REDIS_REPL_SYNCIO_TIMEOUT 5

/* List related stuff */
#define REDIS_HEAD 0
#define REDIS_TAIL 1

/* Sort operations */
#define REDIS_SORT_GET 0
#define REDIS_SORT_ASC 1
#define REDIS_SORT_DESC 2
#define REDIS_SORTKEY_MAX 1024

/* Log levels */
#define REDIS_DEBUG 0
#define REDIS_VERBOSE 1
#define REDIS_NOTICE 2
#define REDIS_WARNING 3
#define REDIS_LOG_RAW (1<<10) /* Modifier to log without timestamp */
#define REDIS_DEFAULT_VERBOSITY REDIS_NOTICE

/* Anti-warning macro... */
#define REDIS_NOTUSED(V) ((void) V)

#define ZSKIPLIST_MAXLEVEL 32 /* Should be enough for 2^32 elements */
#define ZSKIPLIST_P 0.25      /* Skiplist P = 1/4 */

/* Append only defines */
#define AOF_FSYNC_NO 0
#define AOF_FSYNC_ALWAYS 1
#define AOF_FSYNC_EVERYSEC 2
#define REDIS_DEFAULT_AOF_FSYNC AOF_FSYNC_EVERYSEC

/* Zip structure related defaults */
#define REDIS_HASH_MAX_ZIPLIST_ENTRIES 512
#define REDIS_HASH_MAX_ZIPLIST_VALUE 64
#define REDIS_LIST_MAX_ZIPLIST_ENTRIES 512
#define REDIS_LIST_MAX_ZIPLIST_VALUE 64
#define REDIS_SET_MAX_INTSET_ENTRIES 512
#define REDIS_ZSET_MAX_ZIPLIST_ENTRIES 128
#define REDIS_ZSET_MAX_ZIPLIST_VALUE 64

/* HyperLogLog defines */
#define REDIS_DEFAULT_HLL_SPARSE_MAX_BYTES 3000

/* Sets operations codes */
#define REDIS_OP_UNION 0
#define REDIS_OP_DIFF 1
#define REDIS_OP_INTER 2

/* Redis maxmemory strategies */
#define REDIS_MAXMEMORY_VOLATILE_LRU 0
#define REDIS_MAXMEMORY_VOLATILE_TTL 1
#define REDIS_MAXMEMORY_VOLATILE_RANDOM 2
#define REDIS_MAXMEMORY_ALLKEYS_LRU 3
#define REDIS_MAXMEMORY_ALLKEYS_RANDOM 4
#define REDIS_MAXMEMORY_NO_EVICTION 5
#define REDIS_DEFAULT_MAXMEMORY_POLICY REDIS_MAXMEMORY_NO_EVICTION

/* Scripting */
#define REDIS_LUA_TIME_LIMIT 5000 /* milliseconds */

/* Units */
#define UNIT_SECONDS 0 //��λ����
#define UNIT_MILLISECONDS 1 //��λ�Ǻ���

/* SHUTDOWN flags */
#define REDIS_SHUTDOWN_SAVE 1       /* Force SAVE on SHUTDOWN even if no save
                                       points are configured. */
#define REDIS_SHUTDOWN_NOSAVE 2     /* Don't SAVE on SHUTDOWN. */

/* Command call flags, see call() function */
#define REDIS_CALL_NONE 0
#define REDIS_CALL_SLOWLOG 1 //���ĳ�������ִ��ʱ�䳬����һ��ʱ�䣬����¼һ������־
#define REDIS_CALL_STATS 2
#define REDIS_CALL_PROPAGATE 4
#define REDIS_CALL_FULL (REDIS_CALL_SLOWLOG | REDIS_CALL_STATS | REDIS_CALL_PROPAGATE)

/* Command propagation flags, see propagate() function */
#define REDIS_PROPAGATE_NONE 0
#define REDIS_PROPAGATE_AOF 1
#define REDIS_PROPAGATE_REPL 2

/* Keyspace changes notification classes. Every class is associated with a
 * character for configuration purposes. */ //notifyKeyspaceEvent
#define REDIS_NOTIFY_KEYSPACE (1<<0)    /* K */
#define REDIS_NOTIFY_KEYEVENT (1<<1)    /* E */
#define REDIS_NOTIFY_GENERIC (1<<2)     /* g */ //��ʾ����һ��ͨ������֪ͨ
#define REDIS_NOTIFY_STRING (1<<3)      /* $ */
#define REDIS_NOTIFY_LIST (1<<4)        /* l */
#define REDIS_NOTIFY_SET (1<<5)         /* s */  //��ʾ����һ�����ϼ�֪ͨ
#define REDIS_NOTIFY_HASH (1<<6)        /* h */
#define REDIS_NOTIFY_ZSET (1<<7)        /* z */
#define REDIS_NOTIFY_EXPIRED (1<<8)     /* x */
#define REDIS_NOTIFY_EVICTED (1<<9)     /* e */
#define REDIS_NOTIFY_ALL (REDIS_NOTIFY_GENERIC | REDIS_NOTIFY_STRING | REDIS_NOTIFY_LIST | REDIS_NOTIFY_SET | REDIS_NOTIFY_HASH | REDIS_NOTIFY_ZSET | REDIS_NOTIFY_EXPIRED | REDIS_NOTIFY_EVICTED)      /* A */

/* Using the following macro you can run code inside serverCron() with the
 * specified period, specified in milliseconds.
 * The actual resolution depends on server.hz. */
 //��ʾ����ms����������㣬��������ڿ�ʼ����ms���룬����������  ͨ��serverCron(100msִ��һ��)ѭ�����������㣬����msһ����100��������
#define run_with_period(_ms_) if ((_ms_ <= 1000/server.hz) || !(server.cronloops%((_ms_)/(1000/server.hz))))

/* We can print the stacktrace, so our assert is defined this way: */
#define redisAssertWithInfo(_c,_o,_e) ((_e)?(void)0 : (_redisAssertWithInfo(_c,_o,#_e,__FILE__,__LINE__),_exit(1)))
#define redisAssert(_e) ((_e)?(void)0 : (_redisAssert(#_e,__FILE__,__LINE__),_exit(1)))
#define redisPanic(_e) _redisPanic(#_e,__FILE__,__LINE__),_exit(1)

/*-----------------------------------------------------------------------------
 * Data types
 *----------------------------------------------------------------------------*/

typedef long long mstime_t; /* millisecond time type. */

/* A redis object, that is a type able to hold a string / list / set */

/* The actual Redis Object */
/*
 * Redis ����
 */
#define REDIS_LRU_BITS 24
#define REDIS_LRU_CLOCK_MAX ((1<<REDIS_LRU_BITS)-1) /* Max value of obj->lru */
#define REDIS_LRU_CLOCK_RESOLUTION 1000 /* LRU clock resolution in ms */

/*
    �����������е��ַ����ȱ��浽����xxxCommand(��setCommand)����ʱ�����������ַ��������Ѿ�ת��ΪredisObject���浽redisClient->argv[]�У�
    Ȼ����setKey����غ����а�key-valueת��ΪdictEntry�ڵ��key��v(�ֱ��Ӧkey��value)��ӵ�dict->dictht->table[]  hash��,������ô��ӵ�dictEntry
    �ڵ��key��value�п��Բο�dict->type( typeΪxxxDictType ����keyptrDictType��) ����dictCreate
*/

typedef struct redisObject { //key value�ֱ��и��Ե�һ��redisobject�ṹ����dbAdd->dictAdd

    /*
     REDIS_STRING �ַ������� 
     REDIS_LIST �б���� 
     REDIS_HASH ��ϣ���� 
     REDIS_SET ���϶��� 
     REDIS_ZSET ���򼯺϶��� 
     */
    // ����
    unsigned type:4;

    // ���� // ���뷽ʽ  encoding ���Լ�¼�˶�����ʹ�õı��룬 Ҳ����˵�������ʹ����ʲô���ݽṹ��Ϊ����ĵײ�ʵ��
    unsigned encoding:4;

    /*
    ���˿��Ա� OBJECT IDLETIME �����ӡ����֮�⣬ ���Ŀ�תʱ����������һ�����ã� ������������� maxmemory ѡ� ���ҷ�����
    ���ڻ����ڴ���㷨Ϊ volatile-lru ���� allkeys-lru �� ��ô��������ռ�õ��ڴ��������� maxmemory ѡ�������õ�����ֵʱ�� ��ת
    ʱ���ϸߵ��ǲ��ּ������ȱ��������ͷţ� �Ӷ������ڴ档
     */
    // LRU ʱ�䣨����� server.lruclock�� OBJECT IDLETIME ������Դ�ӡ���������Ŀ�תʱ���� ��һ��תʱ������ͨ������ǰʱ���ȥ����ֵ����� lru ʱ�����ó���
    // �������һ�α����ʵ�ʱ��  // �����Լ�¼�˶������һ�α����������ʵ�ʱ�䣺 10sͳ��һ��
    unsigned lru:REDIS_LRU_BITS; /* lru time (relative to server.lruclock) */ //ÿ�η���KV��ֻҪû�����ڽ���fork rdb����aof�������ͻ����,��lookupKey

    /*
     incrRefCount ����������ü���ֵ��һ�� 
     decrRefCount ����������ü���ֵ��һ�� ����������ü���ֵ���� 0 ʱ�� �ͷŶ��� 
     resetRefCount ����������ü���ֵ����Ϊ 0 �� �������ͷŶ��� �������ͨ������Ҫ�������ö�������ü���ֵʱʹ�á� 

     ���ܹ�������ӵĶ�����Խ�Լ������ڴ棬 ���ܵ� CPU ʱ������ƣ� Redis ֻ�԰�������ֵ���ַ���������й���
     */
    // ���ü��� �������ͨ�����ٶ�������ü�����Ϣ�� ���ʵ���ʱ���Զ��ͷŶ��󲢽����ڴ���ա� 
    // ���ü���
    int refcount;

    /*
       �������REDIS_ENCODING_INT���뷽ʽ��ptrָ��һ��int 4�ֽ��ڴ�ռ䣬���洢���õ�int���ݡ�
       �����REDIS_ENCODING_RAW,��ptrָ�����һ��sdshdr�ṹ 
       ������ʾ����ı����������������
     */
    // ָ������ֵ  ����� ptr ָ��ָ�����ĵײ�ʵ�����ݽṹ�� ����Щ���ݽṹ�ɶ���� encoding ���Ծ�����
    // ָ��ʵ��ֵ��ָ��  �ο�����createObject�ĵط���ֵ
    //���Բο���createStringObject(ptrָ��sdsnewlen)  createListObject(ptrָ��list) �ȵȣ�ÿ��type���Ϳ��Զ��ֱ��뷽ʽ

    /*  
         ����                               REDIS_ENCODING_INT           "int"           createStringObjectFromLongLong createIntsetObject tryObjectEncoding
     embstr ����ļ򵥶�̬�ַ�����SDS��     REDIS_ENCODING_EMBSTR        "embstr"        createEmbeddedStringObject
     �򵥶�̬�ַ���                         REDIS_ENCODING_RAW           "raw"           createObject
     �ֵ�                                   REDIS_ENCODING_HT            "hashtable"     createSetObject  hashTypeConvertZiplist  hashTypeConvertZiplist
     ˫������                               REDIS_ENCODING_LINKEDLIST    "linkedlist"    createListObject
     ѹ���б�                               REDIS_ENCODING_ZIPLIST       "ziplist"       createZiplistObject createHashObject createZsetZiplistObject
     ��������                               REDIS_ENCODING_INTSET        "intset"        createIntsetObject
     ��Ծ����ֵ�                           REDIS_ENCODING_SKIPLIST      "skiplist"      createZsetObject
    */ //��ֵ��createObject
    void *ptr;//����Ǵ���10000���ַ�������ֱ��ת��ΪREDIS_ENCODING_INT���뷽ʽ�洢����tryObjectEncoding��ֱ�������ָ��洢��Ӧ���ַ�����ַ

} robj;

/* Macro used to obtain the current LRU clock.
 * If the current resolution is lower than the frequency we refresh the
 * LRU clock (as it should be in production servers) we return the
 * precomputed value, otherwise we need to resort to a function call. */
#define LRU_CLOCK() ((1000/server.hz <= REDIS_LRU_CLOCK_RESOLUTION) ? server.lruclock : getLRUClock())

/* Macro used to initialize a Redis object allocated on the stack.
 * Note that this macro is taken near the structure definition to make sure
 * we'll update it when the structure is changed, to avoid bugs like
 * bug #85 introduced exactly in this way. */
#define initStaticStringObject(_var,_ptr) do { \
    _var.refcount = 1; \
    _var.type = REDIS_STRING; \
    _var.encoding = REDIS_ENCODING_RAW; \
    _var.ptr = _ptr; \
} while(0);

/* To improve the quality of the LRU approximation we take a set of keys
 * that are good candidate for eviction across freeMemoryIfNeeded() calls.
 *
 * Entries inside the eviciton pool are taken ordered by idle time, putting
 * greater idle times to the right (ascending order).
 *
 * Empty entries have the key pointer set to NULL. */
#define REDIS_EVICTION_POOL_SIZE 16
struct evictionPoolEntry {
    unsigned long long idle;    /* Object idle time. */
    sds key;                    /* Key name. */
};

/* Redis database representation. There are multiple databases identified
 * by integers from 0 (the default database) up to the max configured
 * database. The database number is the 'id' field in the structure. */
typedef struct redisDb { //��ʼ����ֵ��initServer  //��---ֵ�洢�������ݽṹ�ο�<redis�漰��ʵ��9.3>

    // ���ݿ���ռ䣬���������ݿ��е����м�ֵ�� // key space��������ֵ���� redisDb�ṹ��dict�ֵ䱣�������ݿ��е����м�ֵ�ԣ����ǽ�����ֵ��Ϊ���ռ�
    //server.db[j].dict = dictCreate(&dbDictType,NULL);
    dict *dict;                 /* The keyspace for this DB */

    /*
    redisDb�ṹ��expires�ֵ䱣�������ݿ������м��Ĺ���ʱ�䣬���ǳ�����ֵ�Ϊ�����ֵ䣺
    �����ֵ�ļ���һ��ָ�룬���ָ��ָ����ռ��е�ĳ��������Ҳ����ĳ�����ݿ������
    �����ֵ��ֵ��һ��long long���͵�������������������˼���ָ������ݿ���Ĺ���ʱ�䡪�������뾫�ȵ�UNIXʱ�����
    ���ռ�ļ��͹����ֵ�ļ���ָ��ͬһ�����������Բ�������κ��ظ�����Ҳ�����˷��κοռ䡣
     */
    // ���Ĺ���ʱ�䣬�ֵ�ļ�Ϊ�����ֵ��ֵΪ�����¼� UNIX ʱ��� server.db[j].expires = dictCreate(&keyptrDictType,NULL);
    dict *expires;              /* Timeout of keys with a timeout set */

    // ����������״̬�ļ� server.db[j].blocking_keys = dictCreate(&keylistDictType,NULL);
    dict *blocking_keys;        /* Keys with clients waiting for data (BLPOP) */

    // ���Խ�������ļ� server.db[j].ready_keys = dictCreate(&setDictType,NULL);
    dict *ready_keys;           /* Blocked keys that received a PUSH */

    /*
     ÿ��Redis���ݿⶼ������һ��watched_keys�ֵ䣬����ֵ�ļ���ĳ����WATCH������ӵ����ݿ�������ֵ��ֵ���ǡ�������
     �����м�¼�����м�����Ӧ���ݿ���Ŀͻ���
     */
    // ���ڱ� WATCH ������ӵļ� server.db[j].watched_keys = dictCreate(&keylistDictType,NULL);
    dict *watched_keys;         /* WATCHED keys for MULTI/EXEC CAS */

    struct evictionPoolEntry *eviction_pool;    /* Eviction pool of keys */

    // ���ݿ����
    int id;                     /* Database ID */

    // ���ݿ�ļ���ƽ�� TTL ��ͳ����Ϣ  �൱�ڸ����ݿ������м�ֵ�Ե�ƽ������ʱ�䣬��λms
    long long avg_ttl;          /* Average TTL, just for stats */

} redisDb;

/* Client MULTI/EXEC state */

/*
 * ��������
 */ 
//����ִ��multi;  get xx; set tt xx; exec���ĸ������multi��set tt xx��������͸�����һ��multiCmd�ṹ����(commands�����е���������ṹ)��count = 2
typedef struct multiCmd {

    // ����
    robj **argv;

    // ��������
    int argc;

    // ����ָ��
    struct redisCommand *cmd;

} multiCmd;

/*
 * ����״̬
 */ 
//����ִ��multi;  get xx; set tt xx; exec���ĸ������multi��set tt xx��������͸�����һ��multiCmd�ṹ����(commands�����е���������ṹ)��count = 2
typedef struct multiState {

    // ������У�FIFO ˳��
    multiCmd *commands;     /* Array of MULTI commands */

    // ������������
    int count;              /* Total number of MULTI commands */
    int minreplicas;        /* MINREPLICAS for synchronous replication */
    time_t minreplicas_timeout; /* MINREPLICAS timeout as unixtime. */
} multiState; //���ݽṹԴͷ��redisClient->mstate

/* This structure holds the blocking operation state for a client.
 * The fields used depend on client->btype. */
// ����״̬
typedef struct blockingState {

    /* Generic fields. */
    // ����ʱ��
    mstime_t timeout;       /* Blocking operation timeout. If UNIX current time
                             * is > timeout then the operation timed out. */

    /* REDIS_BLOCK_LIST */
    // ��������ļ�
    dict *keys;             /* The keys we are waiting to terminate a blocking
                             * operation such as BLPOP. Otherwise NULL. */
    // �ڱ������ļ�����Ԫ�ؽ���ʱ����Ҫ����Щ��Ԫ����ӵ������Ŀ���
    // ���� BRPOPLPUSH ����
    robj *target;           /* The key that should receive the element,
                             * for BRPOPLPUSH. */

    /* REDIS_BLOCK_WAIT */
    // �ȴ� ACK �ĸ��ƽڵ�����
    int numreplicas;        /* Number of replicas we are waiting for ACK. */
    // ����ƫ����
    long long reploffset;   /* Replication offset to reach. */

} blockingState;

/* The following structure represents a node in the server.ready_keys list,
 * where we accumulate all the keys that had clients blocked with a blocking
 * operation such as B[LR]POP, but received new data in the context of the
 * last executed command.
 *
 * After the execution of every command or script, we run this list to check
 * if as a result we should serve data to clients blocked, unblocking them.
 * Note that server.ready_keys will not have duplicates as there dictionary
 * also called ready_keys in every structure representing a Redis database,
 * where we make sure to remember if a given key was already added in the
 * server.ready_keys list. */
// ��¼����˿ͻ��˵�����״̬�ļ����Լ������ڵ����ݿ⡣
typedef struct readyList {
    redisDb *db;
    robj *key;
} readyList;


/*
get a ab ��Ӧ�ı���Ϊ����;

*3
$3
get
$1
a
$2
ab

��һ��������Ҳ����*��ʼ�൱��argc�� ��ʾ�����м�������$��������ֱ�ʾ����������ַ����м����ֽڡ���get a ab��һ��3�����ֱ���get ,a,b�� 
$3��ʾ�����get��3���ֽڡ�
*/


/* With multiplexing we need to take per-client state.
 * Clients are taken in a liked list.
 *
   ִ��CLIENT list��������г�Ŀǰ�������ӵ�����������ͨ�ͻ��ˣ���������е�fd����ʾ�˷��������ӿͻ�����ʹ�õ��׽�����������
    reciis> ClIENT list
    addr=127.0.0.1:53428  fd=6  name=  age=1242jdle=0��
    addr-127.0.0.1:53469  fd=7  name=  age=4  idle=4��
    
 * ��Ϊ I/O ���õ�Ե�ʣ���ҪΪÿ���ͻ���ά��һ��״̬��
 *
 * ����ͻ���״̬������������������������
 */ //��������ֵ��createClient
typedef struct redisClient {   //redisServer��redisClient��Ӧ

    /*
        α�ͻ���( fake client)��fd���Ե�ֵΪ-1:α�ͻ��˴��������������Դ��AOF�ļ�����Lua�ű������������磬�������ֿͻ��˲���Ҫ�׽�
    �����ӣ���ȻҲ����Ҫ��¼�׽�����������ĿǰRedis���������������ط��õ�α�ͻ��ˣ�һ����������AOF�ļ�����ԭ���ݿ�״̬������һ��
    ������ִ��Lua�ű��а�����Redis���
        ��ͨ�ͻ��˵�fd���Ե�ֵΪ����-1����������ͨ�ͻ���ʹ���׽����������������ͨ�ţ����Է���������fd��������¼�ͻ����׽��ֵ���������
    ��Ϊ�Ϸ����׽���������������-1��������ͨ�ͻ��˵��׽�����������ֵ��Ȼ��
     */
    // �׽���������
    int fd;
    char cip[REDIS_IP_STR_LEN];
    int cport;
    
    // ָ��ǰĿ�����ݿ��ָ�� redisClient->dbָ��ָ��redisSerrer->db[]���������һ��Ԫ�أ�����ָ���Ԫ�ؾ��ǿͻ��˵�Ŀ�����ݿ⡣
    // ��ǰ����ʹ�õ����ݿ�  �ڽ��տͻ������Ӻ�Ĭ����createClient-> selectDb(c,0); //Ĭ��ѡ��select 0
    redisDb *db;//�ͻ��˵�ǰ����ʹ�õ����ݿ�  ͨ��select�л�ָ���Ӧ�����ݿ�

    // ��ǰ����ʹ�õ����ݿ�� id �����룩
    int dictid;

    // �ͻ��˵����� Ĭ������£�һ�����ӵ��������Ŀͻ�����û�����ֵġ� ����ͨ��CLIENT setname�������ÿͻ�����
    robj *name;             /* As set by CLIENT SETNAME */

    /*
    �ͻ���״̬�����뻺�������ڱ���ͻ��˷��͵���������
    �ٸ����ӣ�����ͻ����������������������������
    SET key value
    ��ô�ͻ���״̬��querybuf���Խ���һ�������������ݵ�SDSֵ��
    ��3 \r\n$ 3\r\nSET\r\n$3\ r\nkey\r\n$ 5\r\nvalue\r\n

    ���뻺�����Ĵ�С������������ݶ�̬����С�������󣬵���������С���ܳ���1GB(REDIS_MAX_QUERYBUF_LEN)��������������ر�����ͻ��ˡ�
     */
    // ��ѯ������  Ĭ�Ͽռ��СREDIS_IOBUF_LEN����readQueryFromClient
    sds querybuf;//�������Ĳ������������argc��argv��   

    // ��ѯ���������ȷ�ֵ  querybuf���������ж�ȡ���Ŀͻ���������ݳ���   querybuf���������ݳ��ȵķ�ֵ
    size_t querybuf_peak;   /* Recent (100ms or more) peak of querybuf size */

    /*
    �ڷ��������ͻ��˷��͵��������󱣴浽�ͻ���״̬��querybuf����֮�󣬷���������������������ݽ��з����������ó�����������Լ�
��������ĸ����ֱ𱣴浽�ͻ���״̬��argv���Ժ�argc���ԣ�����ͻ���������:set yang xxx����argc=3,argv[0]Ϊset��argv[1]Ϊyang argv[2]Ϊxxx
     */
    // ��������
    int argc;   //�ͻ������������processMultibulkBuffer   //ע��slowlog����¼32����������slowlogCreateEntry

    /*
    �����������е��ַ����ȱ��浽����xxxCommand(��setCommand)����ʱ�����������ַ��������Ѿ�ת��ΪredisObject���浽redisClient->argv[]�У�
    Ȼ����setKey����غ����а�key-valueת��ΪdictEntry�ڵ��key��v(�ֱ��Ӧkey��value)��ӵ�dict->dictht->table[]  hash��,������ô��ӵ�dictEntry
    �ڵ��key��value�п��Բο�dict->type( typeΪxxxDictType ����keyptrDictType��) ����dictCreate
*/
    // ������������  resetClient->freeClientArgv���ͷſռ�
    robj **argv; //�ͻ������������processMultibulkBuffer  �����ռ�͸�ֵ��processMultibulkBuffer���ж��ٸ���������multibulklen���ʹ������ٸ�robj(redisObject)�洢�����Ľṹ

    /*
    ��������������гɹ��ҵ�argv[0]����Ӧ��redisCommand�ṹʱ�����Ὣ�ͻ���״̬��cmdָ��ָ������ṹ��
    //redisServer->orig_commands redisServer->commands(��populateCommandTable)������ֵ��е�Ԫ�����ݴ�redisCommandTable�л�ȡ���� processCommand->lookupCommand����
 //ʵ�ʿͻ�������Ĳ����ں���lookupCommandOrOriginal��
     */
    // ��¼���ͻ���ִ�е�����  processCommand->lookupCommand����
    struct redisCommand *cmd, *lastcmd;

    // ��������ͣ���������Ƕ�������
    int reqtype;

    // ʣ��δ��ȡ��������������  ��������������select tt,��multibulklen=2��set yang xxx������multibulklen=3
    int multibulklen;       /* number of multi bulk arguments left to read */ //û������һ��set����yang����xxx�����ֵ��1��Ϊ0��ʾ������ϣ��ɹ�����

    // �������ݵĳ���
    long bulklen;           /* length of bulk argument in multi bulk request */

    /*
     ִ���������õ�����ظ��ᱻ�����ڿͻ���״̬��������������棬ÿ���ͻ��˶�������������������ã�һ���������Ĵ�С�ǹ̶��ģ�
 ��һ���������Ĵ�С�ǿɱ�ģ��ڹ̶���С�Ļ��������ڱ�����Щ���ȱȽ�С�Ļظ�������OK����̵��ַ���ֵ������ֵ������ظ��ȵȡ�
    �ɱ��С�Ļ��������ڱ�����Щ���ȱȽϴ�Ļظ�������һ���ǳ������ַ���ֵ��һ���ɺܶ�����ɵ��б�һ�������˺ܶ�Ԫ�صļ��ϵȵȡ�
    �ͻ��˵Ĺ̶���С��������buf��bufpos����������ɣ� ��̬������ΪedisClient->reply
     */

/*
 ���ƻ�ѹ��������feedReplicationBacklog�������Ҫ�����������ϣ�Ȼ��ͨ��client->reply����洢������ЩKV����ЩKV������
 ���ͳ�ȥ�ˣ�ʵ���϶Է�û���յ�,�´θĿͻ������Ϻ�ͨ��replconf ack xxx��ͨ���Լ���offset��master�յ���Ϳ����ж϶Է��Ƿ���û��ȫ������
 ���͵�client��ʵʱKV��ѹbuffer������checkClientOutputBufferLimits 

 ��������������:client->reply        checkClientOutputBufferLimits    ��ҪӦ�Կͻ��˶�ȡ����ͬʱ���д���KV���뱾�ڵ㣬��ɻ�ѹ
 ���ƻ�ѹ������:server.repl_backlog    feedReplicationBacklog    ��ҪӦ���������ϣ����в�����ͬ��psyn������ȫ��ͬ��
 */
    // �ظ�����   ���͵��ÿͻ��˵��������ݶ������buf������һ���
    list *reply;

    // �ظ������ж�����ܴ�С  �ο�_addReplyObjectToList
    unsigned long reply_bytes; /* Tot bytes of objects in reply list */

    // �ѷ����ֽڣ����� short write ��
    int sentlen;            /* Amount of bytes already sent in the current
                               buffer or object being sent. */

    // �����ͻ��˵�ʱ�� ctime���Լ�¼�˴����ͻ��˵�ʱ�䣬���ʱ�������������ͻ�����������Ѿ������˶����룬CLIENT list�����age���¼�����������
    time_t ctime;           /* Client creation time */

    // �ͻ������һ�κͷ�����������ʱ��
    time_t lastinteraction; /* time of the last interaction, used for timeout */

    /*
      �����������ʱ���ᵽ�����ɱ��С��������һ��������������ַ���������ɣ���������˵��������������Ա������ⳤ������ظ���
  ���ǣ�Ϊ�˱���ͻ��˵Ļظ�����ռ�ù���ķ�������Դ����������ʱ�̼��ͻ��˵�����������Ĵ�С�����ڻ������Ĵ�С������Χʱ��
  ִ����Ӧ�����Ʋ�����
    ������ʹ������ģʽ�����ƿͻ�������������Ĵ�С��
    Ӳ������( hard limit)���������������Ĵ�С������Ӳ�����������õĴ�С����ô�����������رտͻ��ˡ�
    ��������( soft limit)���������������Ĵ�С�������������������õĴ�С������û����Ӳ�����ƣ���ô��������ʹ�ÿͻ���״̬�ṹ��
    obuf_soft_limit_reached_time���Լ�¼�¿ͻ��˵����������Ƶ���ʼʱ�䣻֮���������������ӿͻ��ˣ��������������Ĵ�Сһֱ����
    �������ƣ����ҳ���ʱ�䳬���������趨��ʱ������ô���������رտͻ��ˣ��෴�أ��������������Ĵ�С��ָ��ʱ�䷦�ڣ����ٳ����������ƣ�
    ��ô�ͻ��˾Ͳ��ᱻ�رգ�����obuf_soft_limit_reached_time���Ե�ֵҲ�ᱻ���㡣client-output-buffer-limit��������

     ʹ��client-output-buffer-limitѡ�����Ϊ��ͨ�ͻ��ˡ��ӷ������ͻ��ˡ�ִ�з����붩�Ĺ��ܵĿͻ��˷ֱ����ò�ͬ���������ƺ�Ӳ�����ƣ���ѡ��ĸ�ʽΪ��
    client-output-buffer-limit <class><hard limit> <soft limit> <soft seconds>
    ��������������ʾ����
    client-output-buffer-limit normal 0 0 0
    client-output-buffer-limit slave 256mb 64mb 60
    cliant-output-buffer-limit pubsub 32mb 8mb 60
    ��һ�����ý���ͨ�ͻ��˵�Ӳ�����ƺ��������ƶ�����ΪO����ʾ�����ƿͻ��˵������������С��
    �ڶ������ý��ӷ������ͻ��˵�Ӳ����������Ϊ256 MB����������������Ϊ64 MB���������Ƶ�ʱ��Ϊ60�롣
    ���������ý�ִ�з����붩�Ĺ��ܵĿͻ��˵�Ӳ����������Ϊ32 MB��������������Ϊ8 MB���������Ƶ�ʱ��Ϊ60�롣
     */
    // �ͻ��˵���������������������Ƶ�ʱ�� ��¼�������������һ�ε�����������( soft limit)��ʱ��
    time_t obuf_soft_limit_reached_time;

    // �ͻ���״̬��־
    int flags;              /* REDIS_SLAVE | REDIS_MONITOR | REDIS_MULTI ... */

    // �� server.requirepass ��Ϊ NULL ʱ
    // ������֤��״̬
    // 0 ����δ��֤�� 1 ��������֤    ��������ļ���requirepass ������֤���룬��ͻ�����ҪAUTH ���������֤
    int authenticated;      /* when requirepass is non-NULL */

    // ����״̬  REDIS_REPL_WAIT_BGSAVE_END��
    int replstate;          /* replication state if this is a slave */
    // ���ڱ����������������� RDB �ļ����ļ�������
    int repldbfd;           /* replication DB file descriptor */

    // ��ȡ�������������� RDB �ļ���ƫ����
    off_t repldboff;        /* replication DB file offset */
    // �������������� RDB �ļ��Ĵ�С
    off_t repldbsize;       /* replication DB file size */

    //slave->replpreamble = sdscatprintf(sdsempty(),"$%lld\r\n", (unsigned long long) slave->repldbsize);
    //// �������������� RDB �ļ��Ĵ�С�ַ�����ʽslave->repldbsize
    sds replpreamble;       /* replication DB preamble. */

    // ���������ĸ���ƫ����  ��ǰ���ӶϿ�ǰ���ܵ��������������������´��ٴ����ӵ�����������������ƫ��������ȥ������������֪��Ӧ�ô�����ط����Ŵ�
    //��slaveTryPartialResynchronization   //��������ֻ���ڽ��յ�����������������RDB�ļ��󣬲Ż���¸�ֵ��������ʵʱ���ݹ�����ʱ��Ҳ��Ҫ���¸�ֵ��
    //�����redis������������ͬ��rdb�ļ���ͬ��һ��RDB�ļ�����ʱ�������쳣�����ٴ������Ϻ�����Ҫ��������RDBͬ����
    //��Ϊֻ��ͬ����������RDB�ļ���Ż����ƫ����reploff����slaveTryPartialResynchronization 
    long long reploff;      /* replication offset if this is our master */ //ͨ��replicationSendAck�����master
    // �ӷ��������һ�η��� REPLCONF ACK ʱ��ƫ����
    long long repl_ack_off; /* replication ack offset, if this is a slave */
    // �ӷ��������һ�η��� REPLCONF ACK ��ʱ��   ��ֵ��replconfCommand
    long long repl_ack_time;/* replication ack time, if this is a slave */
    // ���������� master run ID
    // �����ڿͻ��ˣ�����ִ�в�����ͬ��
    char replrunid[REDIS_RUN_ID_SIZE+1]; /* master run id if this is a master */
    // �ӷ������ļ����˿ں�  ��ֵ��replconfCommand  �ӷ�������������������ʱ��slaveof 

    //slave_listening_port����������ִ��info replication����ʱ��ӡ���ӷ������Ķ˿ںš�
    // ��ʾ�ӷ�������Ϊ�������ˣ��ȴ�redis-cli�ͻ������ӵ�ʱ�򣬷������˵ļ����˿ڣ���initServer listenToPort(server.port,server.ipfd,&server.ipfd_count) == REDIS_ERR)
    int slave_listening_port; /* As configured with: SLAVECONF listening-port */

    // ����״̬
    multiState mstate;      /* MULTI/EXEC state */

    // ��������
    int btype;              /* Type of blocking op if REDIS_BLOCKED. */
    // ����״̬
    blockingState bpop;     /* blocking state */

    // ���д���ȫ�ָ���ƫ����
    long long woff;         /* Last write global replication offset. */

    // �����ӵļ�
    list *watched_keys;     /* Keys WATCHED for MULTI/EXEC CAS */

    // ����ֵ��¼�˿ͻ������ж��ĵ�Ƶ��
    // ��ΪƵ�����֣�ֵΪ NULL
    // Ҳ���ǣ�һ��Ƶ���ļ���
    dict *pubsub_channels;  /* channels a client is interested in (SUBSCRIBE) */

    // ����������� pubsubPattern �ṹ
    // ��¼�����ж���Ƶ���Ŀͻ��˵���Ϣ
    // �� pubsubPattern �ṹ���Ǳ���ӵ���β
    list *pubsub_patterns;  /* patterns a client is interested in (SUBSCRIBE) */
    sds peerid;             /* Cached peer ID. */

    /*
     ִ���������õ�����ظ��ᱻ�����ڿͻ���״̬��������������棬ÿ���ͻ��˶�������������������ã�һ���������Ĵ�С�ǹ̶��ģ�
 ��һ���������Ĵ�С�ǿɱ�ģ��ڹ̶���С�Ļ��������ڱ�����Щ���ȱȽ�С�Ļظ�������OK����̵��ַ���ֵ������ֵ������ظ��ȵȡ�
    �ɱ��С�Ļ��������ڱ�����Щ���ȱȽϴ�Ļظ�������һ���ǳ������ַ���ֵ��һ���ɺܶ�����ɵ��б�һ�������˺ܶ�Ԫ�صļ��ϵȵȡ�
    �ͻ��˵Ĺ̶���С��������buf��bufpos����������ɣ� ��̬������ΪedisClient->reply
     */
    /* Response buffer */
    // �ظ�ƫ����
    int bufpos;
    // �ظ�������
    char buf[REDIS_REPLY_CHUNK_BYTES];

} redisClient;

// �������ı���������BGSAVE �Զ�ִ�е�������
struct saveparam {

    // ������֮��
    time_t seconds;

    // �������ٴ��޸�
    int changes;

};

// ͨ�������������ڴ���Ƭ���Լ����ٲ�����ʱ�Ĺ������
struct sharedObjectsStruct { //��Ӧ�ַ�����createSharedObjects
    robj *crlf, *ok, *err, *emptybulk, *czero, *cone, *cnegone, *pong, *space,
    *colon, *nullbulk, *nullmultibulk, *queued,
    *emptymultibulk, *wrongtypeerr, *nokeyerr, *syntaxerr, *sameobjecterr,
    *outofrangeerr, *noscripterr, *loadingerr, *slowscripterr, *bgsaveerr,
    *masterdownerr, *roslaveerr, *execaborterr, *noautherr, *noreplicaserr,
    *busykeyerr, *oomerr, *plus, *messagebulk, *pmessagebulk, *subscribebulk,
    *unsubscribebulk, *psubscribebulk, *punsubscribebulk, *del, *rpop, *lpop,
    *lpush, *emptyscan, *minstring, *maxstring,
    *select[REDIS_SHARED_SELECT_CMDS],
    *integers[REDIS_SHARED_INTEGERS],
    *mbulkhdr[REDIS_SHARED_BULKHDR_LEN], /* "*<value>\r\n" */
    *bulkhdr[REDIS_SHARED_BULKHDR_LEN];  /* "$<value>\r\n" */
};

/* ZSETs use a specialized version of Skiplists */
/*
 * ��Ծ��ڵ�
 */
/*
��Ծ�� API


����                                            ����                                                ʱ�临�Ӷ�


zslCreate               ����һ���µ���Ծ��                                        O(1) 
zslFree                 �ͷŸ�����Ծ���Լ����а��������нڵ㡣                    O(N) �� N Ϊ��Ծ��ĳ��ȡ� 
zslInsert               ������������Ա�ͷ�ֵ���½ڵ���ӵ���Ծ���С�                ƽ�� O(\log N) ��� O(N) �� N Ϊ��Ծ���ȡ� 
zslDelete               ɾ����Ծ���а���������Ա�ͷ�ֵ�Ľڵ㡣                      ƽ�� O(\log N) ��� O(N) �� N Ϊ��Ծ���ȡ� 
zslGetRank              ���ذ���������Ա�ͷ�ֵ�Ľڵ�����Ծ���е���λ��              ƽ�� O(\log N) ��� O(N) �� N Ϊ��Ծ���ȡ� 
zslGetElementByRank     ������Ծ���ڸ�����λ�ϵĽڵ㡣                              ƽ�� O(\log N) ��� O(N) �� N Ϊ��Ծ���ȡ� 
zslIsInRange            ����һ����ֵ��Χ��range���� ���� 0 �� 15 �� 20 �� 28 ��
                        ������࣬ ��������ķ�ֵ��Χ��������Ծ��ķ�ֵ��Χ֮�ڣ� 
                        ��ô���� 1 �����򷵻� 0 ��                                     ͨ����Ծ��ı�ͷ�ڵ�ͱ�β�ڵ㣬 ����������� O(1) ���Ӷ���ɡ� 
zslFirstInRange         ����һ����ֵ��Χ�� ������Ծ���е�һ�����������Χ�Ľڵ㡣      ƽ�� O(\log N) ��� O(N) �� N Ϊ��Ծ���ȡ� 
zslLastInRange          ����һ����ֵ��Χ�� ������Ծ�������һ�����������Χ�Ľڵ㡣    ƽ�� O(\log N) ��� O(N) �� N Ϊ��Ծ���ȡ� 
zslDeleteRangeByScore   ����һ����ֵ��Χ�� ɾ����Ծ���������������Χ֮�ڵĽڵ㡣      O(N) �� N Ϊ��ɾ���ڵ������� 
zslDeleteRangeByRank    ����һ����λ��Χ�� ɾ����Ծ���������������Χ֮�ڵĽڵ㡣      O(N) �� N Ϊ��ɾ���ڵ������� 
*/

/* ZSETs use a specialized version of Skiplists */
/*
 * ��Ծ��ڵ�
 ��ͬһ����Ծ���У� �����ڵ㱣��ĳ�Ա���������Ψһ�ģ� ���Ƕ���ڵ㱣��ķ�ֵȴ��������ͬ�ģ� ��ֵ��ͬ�Ľڵ㽫���ճ�Ա
 �������ֵ����еĴ�С���������� ��Ա�����С�Ľڵ������ǰ�棨������ͷ�ķ��򣩣� ����Ա����ϴ�Ľڵ�������ں��棨��
 ����β�ķ��򣩡�
 */ //��Ծ����Բο�http://blog.csdn.net/ict2014/article/details/17394259

typedef struct zskiplistNode {

     // ��ͷ�ڵ�Ҳ�к���ָ�롢��ֵ�ͳ�Ա���� ������ͷ�ڵ����Щ���Զ����ᱻ�õ�
    // member ����      �ڵ�������ĳ�Ա����
    robj *obj;

    // ��ֵ // ��ֵ ����Ծ���У��ڵ㰴����������ķ�ֵ��С�������С� ��Ծ���е�����zskiplistNode�ڵ㶼����ֵ��С����������
    double score;

    // ����ָ��
    struct zskiplistNode *backward;

    // ��
    /*
     �ڵ����� L1 �� L2 �� L3 ��������ǽڵ�ĸ����㣬 L1 �����һ�㣬 L2 ����ڶ��㣬�Դ����ơ�ÿ���㶼�����������ԣ�ǰ��ָ��Ϳ�ȡ�

     ��Ծ��ڵ�� level ������԰������Ԫ�أ� ÿ��Ԫ�ض�����һ��ָ�������ڵ��ָ�룬 �������ͨ����Щ�����ӿ���������ڵ��
     �ٶȣ� һ����˵�� �������Խ�࣬ ���������ڵ���ٶȾ�Խ�졣
     
     ÿ�δ���һ������Ծ��ڵ��ʱ�� ���򶼸����ݴζ��� ��power law��Խ��������ֵĸ���ԽС�� �������һ������ 1 �� 32 ֮��
     ��ֵ��Ϊ level ����Ĵ�С�� �����С���ǲ�ġ��߶ȡ���
     */
    struct zskiplistLevel {

        // ǰ��ָ��
        // ǰ��ָ�� ǰ��ָ�����ڷ���λ�ڱ�β����������ڵ�  ������ӱ�ͷ���β���б���ʱ�����ʻ����Ų��ǰ��ָ����С�
        struct zskiplistNode *forward;

        // ���
        // ������Խ�Ľڵ�����  ������¼��ǰ��ָ����ָ��ڵ�͵�ǰ�ڵ�ľ��롣
        unsigned int span;

    } level[]; //�����µ�zskiplistNode�ڵ��ʱ��level��������[]��Сʱ��������ģ���zslInsert->zslRandomLevel

} zskiplistNode; //�洢��zskiplist��Ծ��ṹ��

/*
 * ��Ծ��
 */
/*
header ��ָ����Ծ��ı�ͷ�ڵ㡣
?tail ��ָ����Ծ��ı�β�ڵ㡣
?level ����¼Ŀǰ��Ծ���ڣ����������Ǹ��ڵ�Ĳ�������ͷ�ڵ�Ĳ������������ڣ���
?length ����¼��Ծ��ĳ��ȣ�Ҳ���ǣ���Ծ��Ŀǰ�����ڵ����������ͷ�ڵ㲻�������ڣ���
*/ //��Ծ����Բο�http://blog.csdn.net/ict2014/article/details/17394259
typedef struct zskiplist { //zslCreate

    // ��ͷ�ڵ�ͱ�β�ڵ� // ͷ�ڵ㣬β�ڵ�  ע���ڴ���zskiplist��ʱ��Ĭ���д���һ��ͷ�ڵ㣬��zslCreate
    struct zskiplistNode *header, *tail;

    // ���нڵ������
    unsigned long length;

    // Ŀǰ���ڽڵ�������� level ������������ O(1) ���Ӷ��ڻ�ȡ��Ծ���в�������Ǹ��ڵ�Ĳ������� ע���ͷ�ڵ�Ĳ�߲����������ڡ�
    //����zskiplist��ʱ��zslCreate��Ĭ����1
    int level; //�����µ�zskiplistNode�ڵ��ʱ��level��������[]��Сʱ��������ģ���zslInsert->zslRandomLevel

} zskiplist;

/*
Ϊʲô���򼯺���Ҫͬʱʹ����Ծ����ֵ���ʵ�֣�

����������˵�� ���򼯺Ͽ��Ե���ʹ���ֵ������Ծ�������һ�����ݽṹ��ʵ�֣� �����۵���ʹ���ֵ仹����Ծ�� �������϶�
����ͬʱʹ���ֵ����Ծ�����������͡�

�ٸ����ӣ� �������ֻʹ���ֵ���ʵ�����򼯺ϣ� ��ô��Ȼ�� O(1) ���ӶȲ��ҳ�Ա�ķ�ֵ��һ���Իᱻ������ ���ǣ� ��Ϊ�ֵ���
����ķ�ʽ�����漯��Ԫ�أ� ����ÿ����ִ�з�Χ�Ͳ��� ���� ���� ZRANK �� ZRANGE ������ʱ�� ������Ҫ���ֵ䱣�������Ԫ
�ؽ������� �������������Ҫ���� O(N \log N) ʱ�临�Ӷȣ� �Լ������ O(N) �ڴ�ռ� ����ΪҪ����һ������������������Ԫ�أ���

��һ���棬 �������ֻʹ����Ծ����ʵ�����򼯺ϣ� ��ô��Ծ��ִ�з�Χ�Ͳ����������ŵ㶼�ᱻ������ ����Ϊû�����ֵ䣬 ����
���ݳ�Ա���ҷ�ֵ��һ�����ĸ��ӶȽ��� O(1) ����Ϊ O(\log N) ��

��Ϊ����ԭ�� Ϊ�������򼯺ϵĲ��Һͷ�Χ�Ͳ����������ܿ��ִ�У� Redis ѡ����ͬʱʹ���ֵ����Ծ���������ݽṹ��ʵ�����򼯺ϡ�
*/
/*
 * ����
 */

typedef struct zset {

    // �ֵ䣬��Ϊ��Ա��ֵΪ��ֵ
    // ����֧�� O(1) ���Ӷȵİ���Աȡ��ֵ����
    dict *dict;

    // ��Ծ������ֵ�����Ա
    // ����֧��ƽ�����Ӷ�Ϊ O(log N) �İ���ֵ��λ��Ա����
    // �Լ���Χ����
    zskiplist *zsl;

} zset;

// �ͻ��˻���������
typedef struct clientBufferLimitsConfig {
    // Ӳ����
    unsigned long long hard_limit_bytes;
    // ������
    unsigned long long soft_limit_bytes;
    // ������ʱ��
    time_t soft_limit_seconds;
} clientBufferLimitsConfig;

// ���ƿ����ж��
extern clientBufferLimitsConfig clientBufferLimitsDefaults[REDIS_CLIENT_LIMIT_NUM_CLASSES];

/* The redisOp structure defines a Redis Operation, that is an instance of
 * a command with an argument vector, database ID, propagation target
 * (REDIS_PROPAGATE_*), and command pointer.
 *
 * redisOp �ṹ������һ�� Redis ������
 * ������ָ��ִ�������ָ�롢����Ĳ��������ݿ� ID ������Ŀ�꣨REDIS_PROPAGATE_*����
 *
 * Currently only used to additionally propagate more commands to AOF/Replication
 * after the propagation of the executed command. 
 *
 * Ŀǰֻ�����ڴ�����ִ������֮�󣬴������ӵ�������� AOF �� Replication �С�
 */
typedef struct redisOp {

    // ����
    robj **argv;

    // �������������ݿ� ID ������Ŀ��
    int argc, dbid, target;

    // ��ִ�������ָ��
    struct redisCommand *cmd;

} redisOp;

/* Defines an array of Redis operations. There is an API to add to this
 * structure in a easy way.
 *
 * redisOpArrayInit();
 * redisOpArrayAppend();
 * redisOpArrayFree();
 */
typedef struct redisOpArray {
    redisOp *ops;
    int numops;
} redisOpArray;

/*-----------------------------------------------------------------------------
 * Global server state
 *----------------------------------------------------------------------------*/

struct clusterState;

/* 
redis���������������ݿⱣ���ڷ�����״̬redis.h/redisserver�ṹ��db������
*/
struct redisServer {//struct redisServer server; 

    /* General */

    // �����ļ��ľ���·��
    char *configfile;           /* Absolute config file path, or NULL */

    // serverCron() ÿ����õĴ���  Ĭ��ΪREDIS_DEFAULT_HZ  ��ʾÿ������serverCron���ٴ�   �����������ļ��е�hz�����������д���
    int hz;                     /* serverCron() calls frequency in hertz */

    // ���ݿ� selectDb��ֵ��ÿ�κͿͻ��˶�ȡ������ʱ�򣬶��ж�Ӧ��select x�������
    redisDb *db; //�������ݿⶼ�����ڸ�db�����У�ÿ��redisDb����һ�����ݿ�

    //������ֵ��е�Ԫ�����ݴ�redisCommandTable�л�ȡ����populateCommandTable  ʵ�ʿͻ�������Ĳ����ں���lookupCommandOrOriginal lookupCommand��
    // ������ܵ� rename ����ѡ������ã�  �ο�commandTableDictType���Կ�����dict��Ӧ��key�Ƚ��ǲ����ִ�Сд��
    //�����sentinel��ʽ�������򻹻��sentinelcmds�л�ȡ���ο�initSentinel
    dict *commands;             /* Command table */
    // ������� rename ����ѡ������ã� 
    //redisServer->orig_commands redisServer->commands(��populateCommandTable)������ֵ��е�Ԫ�����ݴ�redisCommandTable�л�ȡ����populateCommandTable
    dict *orig_commands;        /* Command table before command renaming. */

    // �¼�״̬
    aeEventLoop *el;

    // ���һ��ʹ��ʱ��  getLRUClock();
    unsigned lruclock:REDIS_LRU_BITS; /* Clock for LRU eviction */

    // �رշ������ı�ʶ  ÿ��serverCron��������ʱ�����򶼻�Է�����״̬��shutdown_asap���Խ��м�飬���������Ե�ֵ�����Ƿ�رշ�������
    /* 
     �رշ������ı�ʶ��ֵΪ1ʱ���رշ�������ֵΪ0ʱ������������
     */
    int shutdown_asap;          /* SHUTDOWN needed ASAP */

    // ��ִ�� serverCron() ʱ���н���ʽ rehash
    int activerehashing;        /* Incremental rehash in serverCron() */

    // �Ƿ����������� requirepass ������֤����
    char *requirepass;          /* Pass for AUTH command, or NULL */

    // PID �ļ�  ���̺�д����ļ��У���createPidFile
    char *pidfile;              /* PID file path */

    // �ܹ�����
    int arch_bits;              /* 32 or 64 depending on sizeof(long) */
 
    // serverCron() ���������д���������  serverCronÿ����һ�ξ�����1
    int cronloops;              /* Number of times the cron function run */

    /*
    ����������ID: ���˸���ƫ�����͸��ƻ�ѹ������֮�⣬ʵ�ֲ�����ͬ������Ҫ�õ�����������ID
    1. ÿ��Redis�����������������������Ǵӷ��񣬶������Լ�������ID��
    2. ����rD�ڷ���������ʱ�Զ����ɣ���40�������ʮ�������ַ���ɣ�����53b9b28df8042fdc9ab5e3fcbbbabffld5dce2b3��
    ���ӷ������������������г��θ���ʱ�����������Ὣ�Լ�������ID���͸��ӷ����������ӷ�������Ὣ�������ID����������
    ���ӷ��������߲���������һ����������ʱ���ӷ���������ǰ���ӵ�������������֮ǰ���������ID:
    1. ����ӷ��������������ID�͵�ǰ���ӵ���������������ID��ͬ����ô˵���ӷ���������֮ǰ���Ƶľ��ǵ�ǰ���ӵ��������������
    �����������Լ�������ִ�в�����ͬ��������
    2. �෴�أ�����ӷ��������������ID�͵�ǰ���ӵ���������������ID������ͬ����ô˵���ӷ���������֮ǰ���Ƶ���������������
    ��ǰ���ӵ�������������������������Դӷ�����ִ��������ͬ��������

    �ٸ����ӣ�����ӷ�����ԭ�����ڸ���һ������IDΪ53b9b28df8042fdc9ab5e3fcbbbabf fld5dce2b3��������������ô������Ͽ����ӷ�����
������������������֮�󣬴ӷ��������������������������ID���������������Լ�������ID�Ƿ�53b9b28df8042fdc9ab5e3fcbbbabffld5dce2b3��
�ж���ִ�в�����ͬ������ִ��������ͬ����
     */
    // ���������� RUN ID
    char runid[REDIS_RUN_ID_SIZE+1];  /* ID always different at every exec. */

    // �������Ƿ������� SENTINEL ģʽ   ��checkForSentinelMode
    int sentinel_mode;          /* True if this instance is a Sentinel. */


    /* Networking */

    //����������redis�������и�ֵ��Ĭ��REDIS_SERVERPORT   �����Sentinel��ʽ��������ֵΪREDIS_SENTINEL_PORT
    // TCP �����˿� port��������  ��ʾ��Ϊ�������ˣ��ȴ�redis-cli�ͻ������ӵ�ʱ�򣬷������˵ļ����˿ڣ���initServer listenToPort(server.port,server.ipfd,&server.ipfd_count) == REDIS_ERR)
    int port;                   /* TCP listening port */

    int tcp_backlog;            /* TCP listen() backlog */

    // ��ַ
    char *bindaddr[REDIS_BINDADDR_MAX]; /* Addresses we should bind to */
    // ��ַ����
    int bindaddr_count;         /* Number of addresses in server.bindaddr[] */

    // UNIX �׽���
    char *unixsocket;           /* UNIX socket path */
    mode_t unixsocketperm;      /* UNIX socket permission */

    // ������  ��Ӧ����redis��������listen��ʱ�򴴽����׽��֣��������˿���bind����˿ں͵�ַ����bind�������м���ipfd_count
    int ipfd[REDIS_BINDADDR_MAX]; /* TCP socket file descriptors */
    // ���������� listenToPort(server.port,server.ipfd,&server.ipfd_count)�и�ֵ
    int ipfd_count;             /* Used slots in ipfd[] */

    // UNIX �׽����ļ�������
    int sofd;                   /* Unix socket file descriptor */

    int cfd[REDIS_BINDADDR_MAX];/* Cluster bus listening socket */
    int cfd_count;              /* Used slots in cfd[] */

    // һ���������������пͻ���״̬�ṹ  createClient�а�redisClient�ͻ�����ӵ��÷��������clients������  if (fd != -1) listAddNodeTail(server.clients,c);
    list *clients;               /* List of active clients */
    // �������������д��رյĿͻ���
    list *clients_to_close;     /* Clients to close asynchronously */

    // �������������дӷ�������
    list *slaves,  //ע���п��ܴӷ��������滹��ҽӴӷ�����
    //�������������Ӹ÷���������ִ����monitor�ĵĿͻ���
    //��������ÿ�δ�����������ǰ���������replicationFeedMonitors��������������������������������������Ϣ���͸�������������
    *monitors;    /* List of slaves and MONITORs */

    // �������ĵ�ǰ�ͻ��ˣ������ڱ�������
    redisClient *current_client; /* Current client, only used on crash report */

    int clients_paused;         /* True if clients are currently paused */
    //�����յ�slave��CLUSTERMSG_TYPE_MFSTART������REDIS_CLUSTER_MF_TIMEOUT*2
    //���Ӹ����Ĵӽ���ǿ�ƹ���ת��cluster failover��ʱ������ʱ�����ͻ���������ô��ʱ���ѽ���ǿ�ƹ���ת�ƣ���֤����������ȫͬ�����ӣ���pauseClients
    mstime_t clients_pause_end_time; /* Time when we undo clients_paused */

    // �������
    char neterr[ANET_ERR_LEN];   /* Error buffer for anet.c */

    // MIGRATE ����  �洢����KV��KΪip:port�ַ�����VΪ�׽�����Ϣ��ip port��Ӧ���׽�����Ϣserver.migrateCachedSocket��
    dict *migrate_cached_sockets;/* MIGRATE cached sockets */ //��dict�����ֻ����MIGRATE_SOCKET_CACHE_ITEMS���׽�����Ϣ����migrateGetSocket

    /* RDB / AOF loading information */

    // ���ֵΪ��ʱ����ʾ���������ڽ�������     �����ֵΪ1������д�ӡaddReply(c, shared.loadingerr);
    int loading;                /* We are loading data from disk if true */

    // ������������ݵĴ�С
    off_t loading_total_bytes;

    // ���������ݵĴ�С
    off_t loading_loaded_bytes;

    // ��ʼ���������ʱ��
    time_t loading_start_time;
    off_t loading_process_events_interval_bytes;

    /* Fast pointers to often looked up command */
    // ��������Ŀ������
    struct redisCommand *delCommand, *multiCommand, *lpushCommand, *lpopCommand,
                        *rpopCommand;


    /* Fields used only for stats */

    // ����������ʱ��
    time_t stat_starttime;          /* Server start time */

    // �Ѵ������������  ��ǰ�ܵ�ָ����������  trackOperationsPerSecond   
    //call������
    long long stat_numcommands;     /* Number of processed commands */

    // �������ӵ���������������
    long long stat_numconnections;  /* Number of connections received */

    // �ѹ��ڵļ�����
    long long stat_expiredkeys;     /* Number of expired keys */

    // ��Ϊ�����ڴ�����ͷŵĹ��ڼ�������
    long long stat_evictedkeys;     /* Number of evicted keys (maxmemory) */

    // �ɹ����Ҽ��Ĵ��� //�������ݿ�����ִ���������ͨ��info stats����鿴
    long long stat_keyspace_hits;   /* Number of successful lookups of keys */

    // ���Ҽ�ʧ�ܵĴ��� //�������ݿ��δ���ִ���������ͨ��info stats����鿴
    long long stat_keyspace_misses; /* Number of failed lookups of keys */

    /*
    INFO memory�����used memory_peak��used_memory_peak_human������ֱ������ָ�ʽ��¼�˷��������ڴ��ֵ
     */
    // ��ʹ���ڴ��ֵ  ��¼�˷��������ڴ��ֵ��С��
    size_t stat_peak_memory;        /* Max used memory record */

    // ���һ��ִ�� fork() ʱ���ĵ�ʱ��
    long long stat_fork_time;       /* Time needed to perform latest fork() */

    // ��������Ϊ�ͻ�������������ܾ��ͻ������ӵĴ���
    long long stat_rejected_conn;   /* Clients rejected because of maxclients */

    // ִ�� full sync �Ĵ���
    long long stat_sync_full;       /* Number of full resyncs with slaves. */

    // PSYNC �ɹ�ִ�еĴ���
    long long stat_sync_partial_ok; /* Number of accepted PSYNC requests. */

    // PSYNC ִ��ʧ�ܵĴ���
    long long stat_sync_partial_err;/* Number of unaccepted PSYNC requests. */


    /* slowlog */

    // ��������������ѯ��־������  ��slowlogPushEntryIfNeeded
    list *slowlog;                  /* SLOWLOG list of commands */ //����ڵ�����slowlogEntry

    // ��һ������ѯ��־�� ID  ���Եĳ�ʼֵΪO��ÿ������һ���µ�����ѯ��־ʱ��������Ե�ֵ�ͻ���������־��idֵ��֮�������������Ե�ֵ��һ��
    long long slowlog_entry_id;     /* SLOWLOG current entry ID */

    /*
        slowlog-log-slower-thanѡ��ָ��ִ��ʱ�䳬������΢�루1�����1 000 0 0 0΢�룩����������ᱻ��¼����־�ϡ�
    �ٸ����ӣ�������ѡ���ֵΪ1 0 0����ôִ��ʱ�䳬��1 0 0΢�������ͻᱻ��¼������ѯ��־
       */
    // ���������� slowlog-log-slower-than ѡ���ֵ
    long long slowlog_log_slower_than; /* SLOWLOG time limit (to get logged) */

    /*
       lowlog-max-lenѡ��ָ����������ౣ�����������ѯ��־��������ʹ���Ƚ��ȳ��ķ�ʽ�����������ѯ��־�����������洢����
       ��ѯ��־��������slowlog-max-lenѡ���ֵʱ�������������һ���µ�����ѯ��־֮ǰ�����Ƚ���ɵ�һ������ѯ��־ɾ����
      */
    // ���������� slowlog-max-len ѡ���ֵ  //Redis�ӳټ�ؿ�����http://ghoulich.xninja.org/2016/12/08/how-to-use-latency-monitor-in-redis/
    unsigned long slowlog_max_len;     /* SLOWLOG max number of items logged */
    size_t resident_set_size;       /* RSS sampled in serverCron(). */
    /* The following two are used to track instantaneous "load" in terms
     * of operations per second. */
    // ���һ�ν��г�����ʱ�� 
    long long ops_sec_last_sample_time; /* Timestamp of last sample (in ms) */
    // ���һ�γ���ʱ����������ִ����������� trackOperationsPerSecond
    long long ops_sec_last_sample_ops;  /* numcommands in last sample */
    // �������  ��С��Ĭ��ֵΪ16���Ļ������飬�����е�ÿ�����¼��һ�γ��������
    long long ops_sec_samples[REDIS_OPS_SEC_SAMPLES];
    // �������������ڱ�����������������Ҫʱ���Ƶ� 0    ÿ�γ�����ֵ����һ����ֵ����16ʱ����Ϊ�ڣ� ����һ���������顣
    int ops_sec_idx; //ÿ�γ�����1����trackOperationsPerSecond


    /* Configuration */

    // ��־�ɼ���  ��־�Ǽ����� loglevel debug��
    int verbosity;                  /* Loglevel in redis.conf */

    // �ͻ�������תʱ��  Ĭ��REDIS_MAXIDLETIME�����
    int maxidletime;                /* Client timeout in seconds */

    /*
    2��ʹ��TCP��keepalive����,UNIX�����̲��Ƽ�ʹ��SO_KEEPALIVE�����������(Ϊʲô??)��
    keepaliveԭ��:TCP��Ƕ��������,�Է����Ϊ��,��server��⵽����һ��ʱ��(/proc/sys/net/ipv4/tcp_keepalive_time 7200 ��2Сʱ)û�����ݴ���,��ô����client�˷���һ��keepalive packet,��ʱclient�������ַ�Ӧ:
    1��client����������,����һ��ACK.server���յ�ACK�����ü�ʱ��,��2Сʱ���ڷ���̽��.���2Сʱ�������������ݴ���,��ô�ڸ�ʱ��Ļ������������2Сʱ����̽���;
    2���ͻ����쳣�ر�,������Ͽ���client����Ӧ,server�ղ���ACK,��һ��ʱ��(/proc/sys/net/ipv4/tcp_keepalive_intvl 75 ��75��)���ط�keepalive packet, �����ط�һ������(/proc/sys/net/ipv4/tcp_keepalive_probes 9 ��9��);
    3���ͻ�����������,���Ѿ�����.server�յ���̽����Ӧ��һ����λ,server����ֹ���ӡ�
    �޸�����������ϵͳĬ��ֵ
    ��ʱ����:�������ļ���ֱ��д�����,ϵͳ������Ҫ��������;
    ��ʱ����:sysctl -w net.ipv4.tcp_keepalive_intvl=20
    ȫ������:�ɸ���/etc/sysctl.conf,����:
    net.ipv4.tcp_keepalive_intvl = 20
    net.ipv4.tcp_keepalive_probes = 3
    net.ipv4.tcp_keepalive_time = 60
    */
    // �Ƿ��� SO_KEEPALIVE ѡ��  tcp-keepalive ���ã�Ĭ�ϲ�����
    int tcpkeepalive;               /* Set SO_KEEPALIVE if non-zero. */
    //Ĭ�ϳ�ʼ��Ϊ1
    int active_expire_enabled;      /* Can be disabled for testing purposes. */
    size_t client_max_querybuf_len; /* Limit for client query buffer length */ //REDIS_MAX_QUERYBUF_LEN
    /*
    Ĭ������£�Redis�ͻ��˵�Ŀ�����ݿ�ΪO�����ݿ⣬���ͻ��˿���ͨ��ִ��SELECT�������л�Ŀ�����ݿ⡣
     */
    //��ʼ����������ʱ�򣬸���dbnum�������������ٸ����ݿ�  Ĭ��16  databases��������
    int dbnum;                      /* Total number of configured DBs */
    int daemonize;                  /* True if running as a daemon */
    // �ͻ��������������С����
    // �����Ԫ���� REDIS_CLIENT_LIMIT_NUM_CLASSES ��
    // ÿ������һ��ͻ��ˣ���ͨ���ӷ�������pubsub���������
    clientBufferLimitsConfig client_obuf_limits[REDIS_CLIENT_LIMIT_NUM_CLASSES];


    /* AOF persistence */

    // AOF ״̬������/�ر�/��д�� //appendonly  yes | no����  ����Ϊno�򲻻����aof�ļ�appendonly.aof
    int aof_state;                  /* REDIS_AOF_(ON|OFF|WAIT_REWRITE) */

    // ��ʹ�õ� fsync ���ԣ�ÿ��д��/ÿ��/�Ӳ���
    int aof_fsync;                  /* Kind of fsync() policy */
    char *aof_filename;             /* Name of the AOF file */
    int aof_no_fsync_on_rewrite;    /* Don't fsync if a rewrite is in prog. */
    //Ĭ��100��������auto-aof-rewrite-percentage�������� 
    //auto-aof-rewrite-percentage(100)����AOF�ļ����������������������������һ���������̨rewrite�Զ�����
    int aof_rewrite_perc;           /* Rewrite AOF if % growth is > M and... */
    //auto-aof-rewrite-min-size(64mb)�����к���rewriteҪ�����СAOF�ļ���С��������ѡ�ͬ�����˺���rewrite�����Ƿ񵽴����е�ʱ��
    off_t aof_rewrite_min_size;     /* the AOF file is at least N bytes. */

    // ���һ��ִ�� BGREWRITEAOF ʱ�� AOF �ļ��Ĵ�С
    off_t aof_rewrite_base_size;    /* AOF size on latest startup or rewrite. */

    // AOF �ļ��ĵ�ǰ�ֽڴ�С  BGREWRITEAOF�����µ��������׷�ӵ��ļ�ĩβ������aof_current_size��aof_rewrite_base_size��
    off_t aof_current_size;         /* AOF current size. */
    /*
�ڷ�����ִ��BGSA VE��������ڼ䣬����ͻ��������������BGREWRITEAOF�����ô�������ὫBGREWRITEAOF�����ִ��ʱ���ӳٵ�BGSAVE����ִ�����֮��
��������aof rewrite scheduled��ʶ��¼�˷������Ƿ��ӳ���BGREWRITEAOF����
ÿ��serverCron����ִ��ʱ������������BGSAVE�������BGREWIUTEAOF�����Ƿ�����ִ�У�������������û��ִ�У�����aof rewrite scheduled���Ե�
ֵΪ1����ô�������ͻ�ִ��֮ǰ�����ӵ�BGREWRITEAOF����
     */
    int aof_rewrite_scheduled;      /* Rewrite once BGSAVE terminates. */

    /*
     ������״̬ʹ��rdb_child_pid���Ժ�aof_child_pid���Լ�¼ִ��BGSAVE�����BGREWRITEAOF������ӽ��̵�ID������������Ҳ�������ڼ��
     BGSAVE�������BGREWRITEAOF�����Ƿ�����ִ��
     */
    // ������� AOF ��д���ӽ��� ID  ��¼ִ��BGREWRITEAOF������ӽ��̵�ID:���������û����ִ��BGREWRITEAOF����ô������Ե�ֵΪ-1��
    pid_t aof_child_pid;            /* PID if rewriting process */

    // AOF ��д�������������Ŷ�������   
    //�ڽ���AOF�����У�����в����µ�д������µ�д�������ʱ���浽aof_rewrite_buf_blocks�������У�Ȼ����aofRewriteBufferWrite׷��д��AOF�ļ�ĩβ
    list *aof_rewrite_buf_blocks;   /* Hold changes during an AOF rewrite. */

    /* ��AOF�־û����ܴ��ڴ�״̬ʱ����������ִ����һ��д����֮�󣬻���Э���ʽ
����ִ�е�д����׷�ӵ�������״̬��aof buf��������ĩβ�� */
    // AOF ������
    sds aof_buf;      /* AOF buffer, written before entering the event loop */

    // AOF �ļ���������
    int aof_fd;       /* File descriptor of currently selected AOF file */

    // AOF �ĵ�ǰĿ�����ݿ�
    int aof_selected_db; /* Currently selected DB in AOF */

    // �Ƴ� write ������ʱ��
    time_t aof_flush_postponed_start; /* UNIX time of postponed AOF flush */

    // ���һֱִ�� fsync ��ʱ��
    time_t aof_last_fsync;            /* UNIX time of last fsync() */
    time_t aof_rewrite_time_last;   /* Time used by last AOF rewrite run. */

    // AOF ��д�Ŀ�ʼʱ��
    time_t aof_rewrite_time_start;  /* Current AOF rewrite start time. */

    // ���һ��ִ�� BGREWRITEAOF �Ľ��
    int aof_lastbgrewrite_status;   /* REDIS_OK or REDIS_ERR */

    // ��¼ AOF �� write �������Ƴ��˶��ٴ�
    unsigned long aof_delayed_fsync;  /* delayed AOF fsync() counter */

    // ָʾ�Ƿ���Ҫÿд��һ���������ݣ�������ִ��һ�� fsync()
    int aof_rewrite_incremental_fsync;/* fsync incrementally while rewriting? */
    //ֻ����flushAppendOnlyFileʧ�ܵ�ʱ��Ż�REDIS_ERR  һ�㶼���ڴ治�����ߴ��̿ռ䲻����ʱ�����ERR
    int aof_last_write_status;      /* REDIS_OK or REDIS_ERR */
    int aof_last_write_errno;       /* Valid if aof_last_write_status is ERR */
    /* RDB persistence */

    /*
    ������ÿ���޸�һ����֮�󣬶�����ࣨdirty������������ֵ��l������������ᴥ���������ĳ־û��Լ����Ʋ���
     */
    // �Դ��ϴ� SAVE ִ�����������ݿⱻ�޸ĵĴ���������д�롢ɾ�������µȲ������� ִ��save bgsave�����������Ϊ0
    //��bgsaveִ����Ϻ󣬻���backgroundSaveDoneHandler���¸�ֵ
    long long dirty;                /* Changes to DB from the last save */

    // BGSAVE ִ��ǰ�����ݿⱻ�޸Ĵ���  //��bgsaveִ����Ϻ󣬻���backgroundSaveDoneHandler���¸�ֵ
    long long dirty_before_bgsave;  /* Used to restore dirty on failed BGSAVE */

    /*
     ������״̬ʹ��rdb_child_pid���Ժ�aof_child_pid���Լ�¼ִ��BGSAVE�����BGREWRITEAOF������ӽ��̵�ID������������Ҳ�������ڼ��
     BGSAVE�������BGREWRITEAOF�����Ƿ�����ִ��
     */
    // ����ִ�� BGSAVE ���ӽ��̵� ID
    // û��ִ�� BGSAVE ʱ����Ϊ -1  ��¼ִ��BGSA VE������ӽ��̵�ID�����������û����ִ��BGSA VE����ô������Ե�ֵΪ-l��
    pid_t rdb_child_pid;            /* PID of RDB saving child */

/*
��Redis����������ʱ���û�����ͨ��ָ�������ļ����ߴ������������ķ�ʽ����saveѡ�����û�û����������saveѡ����۷�������Ϊsaveѡ������Ĭ��������
    save 900 1
    save 300 10
    save 60 10000
saveparams������һ�����飬�����е�ÿ��Ԫ�ض���һ��saveparam�ṹ��ÿ��saveparam�ṹ��������һ��saveѡ�����õı�������
*/  //saveparams������Ч�ط���serverCron  
    struct saveparam *saveparams;   /* Save points array for RDB */
    int saveparamslen;              /* Number of saving points */
    char *rdb_filename;             /* Name of RDB file */ //dbfilename XXX����  Ĭ��REDIS_DEFAULT_RDB_FILENAME
    int rdb_compression;            /* Use compression in RDB? */ //rdbcompression  yes | off
    //Ĭ��1����REDIS_DEFAULT_RDB_CHECKSUM
    int rdb_checksum;               /* Use RDB checksum? */

    // ���һ����� SAVE ��ʱ�� lastsave������һ��UNIXʱ�������¼�˷�������һ�γɹ�ִ��SA VE�������BGSAVE�����ʱ�䡣
    time_t lastsave;                /* Unix time of last successful save */ //��bgsaveִ����Ϻ󣬻���backgroundSaveDoneHandler���¸�ֵ

    // ���һ�γ���ִ�� BGSAVE ��ʱ��
    time_t lastbgsave_try;          /* Unix time of last attempted bgsave */

    // ���һ�� BGSAVE ִ�кķѵ�ʱ��
    time_t rdb_save_time_last;      /* Time used by last RDB save run. */

    // ���ݿ����һ�ο�ʼִ�� BGSAVE ��ʱ��
    time_t rdb_save_time_start;     /* Current RDB save start time. */

    // ���һ��ִ�� SAVE ��״̬ //��bgsaveִ����Ϻ󣬻���backgroundSaveDoneHandler���¸�ֵ
    //ֻ���ڽ���bgsave��ʱ��forkʧ�ܻ���bgsave�����б��жϣ�д�ļ�ʧ�ܵ�ʱ��Ż�ΪREDIS_ERR
    int lastbgsave_status;          /* REDIS_OK or REDIS_ERR */ 
    int stop_writes_on_bgsave_err;  /* Don't allow writes if can't BGSAVE */


    /* Propagation of commands in AOF / replication */
    redisOpArray also_propagate;    /* Additional command to propagate. */


    /* Logging */
    char *logfile;                  /* Path of log file */
    int syslog_enabled;             /* Is syslog enabled? */
    char *syslog_ident;             /* Syslog ident */
    int syslog_facility;            /* Syslog facility */


    /* Replication (master) */
    int slaveseldb;                 /* Last SELECTed DB in replication output */
    /*
    ������Ҫ�������ƻ�ѹ�������Ĵ�С
        RedisΪ���ƻ�ѹ���������õ�Ĭ�ϴ�СΪl MB���������������Ҫִ�д���д����ֻ������ӷ��������ߺ������������ʱ��Ƚϳ���
    ��ô�����СҲ�������ʡ�
        ������ƻ�ѹ�������Ĵ�С���õò�ǡ������ôPSYNC����ĸ�����ͬ��ģʽ�Ͳ��������������ã���ˣ���ȷ��������ø��ƻ�ѹ�������Ĵ�С�ǳ���Ҫ��
        ���ƻ�ѹ����������С��С���Ը��ݹ�ʽsecond * write_size_per_second�����㣺
        ����secondΪ�ӷ��������ߺ������������������������ƽ��ʱ�ͣ��������):
        ��write_size_per_second������������ƽ��ÿ�������д������������Э���ʽ��д����ĳ����ܺ͡���
        ���磬�����������ƽ��ÿ�����1MB��д���ݣ����ӷ���������֮��ƽ��Ҫ5���������������������������ô���ƻ�ѹ�������Ĵ�С�Ͳ��ܵ���5 MB��
        Ϊ�˰�ȫ��������Խ����ƻ�ѹ�������Ĵ�С��Ϊ2 * second * write_size_per_second���������Ա�֤���󲿷ֶ�����������ò�����ͬ��������



    ���ƻ�ѹ������������������ά����һ���̶����ȣ�fixed-size���Ƚ��ȳ�(FIFO)���У�Ĭ�ϴ�СΪlMB��
    �̶������Ƚ��ȳ����е���Ӻͳ��ӹ������ͨ���Ƚ��ȳ�����һ������Ԫ�ش�һ�߽�����У�����Ԫ�ش���һ�ߵ������С�
    ����ͨ�Ƚ��ȳ���������Ԫ�ص����Ӻͼ��ٶ���̬�������Ȳ�ͬ���̶������Ƚ��ȳ����еĳ����ǹ̶��ģ������Ԫ�ص��������ڶ��г���ʱ��
    ������ӵ�Ԫ�ػᱻ����������Ԫ���ᱻ������С�

���ӷ���������������������ʱ���ӷ�������ͨ��PSYNC����Լ��ĸ���ƫ����offset���͸�������������������������������ƫ��������
���Դӷ�����ִ�к���ͬ��������
    ���offsetƫ����֮������ݣ�Ҳ����ƫ����offset+l��ʼ�����ݣ���Ȼ�����ڸ��ƻ�ѹ���������棬��ô�����������Դӷ�����ִ�в���өͬ��������
    �෴�����offsetƫ����֮��������Ѿ��������ڸ��ƻ�ѹ����������ô�����������Դӷ�����ִ��������ͬ��������

    ���ӷ�����A����֮������������������������������������������PSYNC��������Լ��ĸ���ƫ����Ϊ10086��
    ���������յ��ӷ�����������PSYNC�����Լ�ƫ����10086֮���������������ƫ����1 0 0 8 6֮��������Ƿ�����ڸ��ƻ�ѹ���������棬
        ���������Щ������Ȼ���ڣ���������������ӷ���������"+CONTINUE"�ظ�����ʾ����ͬ�����Բ�����ͬ��ģʽ�����С�
    �������������Ὣ���ƻ�ѹ������10086ƫ����֮����������ݣ�ƫ����Ϊ10087��10119�������͸��ӷ�������
*/
    
    // ȫ�ָ���ƫ������һ���ۼ�ֵ��  ���ܳ��ȣ�Ҳ����д���ѹ��������ʱ��ͼӣ�д���ټӶ���
    //info replication�е�repl_backlog_first_byte_offset����ʾ��ѹ��������Ч���ݵ���ʵƫ���� 
    //master_repl_offset��ʾ��ѹ�������еĽ���ƫ����������ƫ��������ʼ�������е�buf���ǿ��õ�����
    //repl_backlog_histlen��ʾ��ѹ�����������ݵĴ�С ��feedReplicationBacklog
    long long master_repl_offset;   /* Global replication offset */
    // ������������ PING ��Ƶ��
    int repl_ping_slave_period;     /* Master pings the slave every N seconds */

     /*���ƻ�ѹ��������feedReplicationBacklog�������Ҫ�����������ϣ�Ȼ��ͨ��client->reply����洢������ЩKV����ЩKV������
    ���ͳ�ȥ�ˣ�ʵ���϶Է�û���յ�,�´θĿͻ������Ϻ�ͨ��replconf ack xxx��ͨ���Լ���offset��master�յ���Ϳ����ж϶Է��Ƿ���û��ȫ������
    ���͵�client��ʵʱKV��ѹbuffer������checkClientOutputBufferLimits 
    
    ��������������:client->reply        checkClientOutputBufferLimits    ��ҪӦ�Կͻ��˶�ȡ����ͬʱ���д���KV���뱾�ڵ㣬��ɻ�ѹ
    ���ƻ�ѹ������:server.repl_backlog    feedReplicationBacklog    ��ҪӦ���������ϣ����в�����ͬ��psyn������ȫ��ͬ��
    */

    //����ͨ��replicationFeedSlavesʵ��ʵʱ����ͬ���� ʵʱ����д���ѹ�������ڽӿ�feedReplicationBacklog
    // backlog ���� repl_backlog��ѹ�������ռ� repl_backlog_size��ѹ�������ܴ�С �ο�resizeReplicationBacklog ��ѹ�������ռ������createReplicationBacklog
    char *repl_backlog;             /* Replication backlog for partial syncs */ //��ѹ��������freeReplicationBacklog���ͷ�
    //repl_backlog��ѹ�������ռ�  repl_backlog_size��ѹ�������ܴ�С  �ο�resizeReplicationBacklog
    // backlog �ĳ��� //repl-backlog-size��������  ��ʾ�ڴӷ������Ͽ����ӹ����У�����кܴ�д������øô�С�Ļ�ѹ���������洢��Щ�����ģ��´��ٴ�������ֱ�Ӱѻ�ѹ�����������ݷ��͸��ӷ���������
    long long repl_backlog_size;    /* Backlog circular buffer size */
    //info replication�е�repl_backlog_first_byte_offset����ʾ��ѹ��������Ч���ݵ���ʵƫ���� 
    //master_repl_offset��ʾ��ѹ�������еĽ���ƫ����������ƫ��������ʼ�������е�buf���ǿ��õ�����
    //repl_backlog_histlen��ʾ��ѹ�����������ݵĴ�С ��feedReplicationBacklog
    // backlog �����ݵĳ���  repl_backlog_histlen �����ֵֻ�ܵ��� repl_backlog_size 
    long long repl_backlog_histlen; /* Backlog actual data length */ //������Ч�ĵط���masterTryPartialResynchronization
    // backlog �ĵ�ǰ����  Ҳ�����´�����Ӧ�ôӻ��λ��������Ǹ��ط���ʼд����feedReplicationBacklog
    long long repl_backlog_idx;     /* Backlog circular buffer current offset */
    // backlog �п��Ա���ԭ�ĵ�һ���ֽڵ�ƫ����    ��feedReplicationBacklog
    //info replication�е�repl_backlog_first_byte_offset����ʾ��ѹ��������Ч���ݵ���ʵƫ����  master_repl_offset��ʾ��ѹ�������еĽ���ƫ����������ƫ��������ʼ�������е�buf���ǿ��õ�����
    //������Ч�ĵط���masterTryPartialResynchronization
    long long repl_backlog_off;     /* Replication offset of first byte in the
                                       backlog buffer. */
    // backlog �Ĺ���ʱ��
    time_t repl_backlog_time_limit; /* Time without slaves after the backlog
                                       gets released. */

    // ������һ���дӷ�������ʱ��
    time_t repl_no_slaves_since;    /* We have no slaves since that time.
                                       Only valid if server.slaves len is 0. */

    /*
     ����ʵ��min-slaves����ѡ��
         Redis��min-slaves-to-write��min-slaves-max-lag����ѡ����Է�ֹ���������ڲ���ȫ�������ִ��д����ٸ����ӣ�������������������ṩ�������ã�
       min-slaves-to-write 3
       min-slaves-max-lag 10
       ��ô�ڴӷ���������������3�������������ӷ��������ӳ�(lag)ֵ�����ڻ����10��ʱ�������������ܾ�ִ��д���������ӳ�ֵ��������
       �ᵽ��INFO replication�����lagֵ��
     */
    // �Ƿ�����С�����ӷ�����д�빦��
    int repl_min_slaves_to_write;   /* Min number of slaves to write. */
    // ������С�����ӷ�����������ӳ�ֵ
    int repl_min_slaves_max_lag;    /* Max lag of <count> slaves to write. */
    // �ӳ����õĴӷ�����������  ֻ��Ϊ��info��ʱ��鿴��
    int repl_good_slaves_count;     /* Number of slaves with lag <= max_lag. */


    /* Replication (slave) */
    // ������������֤����  masterauth����
    char *masterauth;               /* AUTH with this password with master */
    //slaveof 10.2.2.2 1234 �е�masterhost=10.2.2.2 masterport=1234  ��ֵ��replicationSetMaster  ����������replicationCron
    // ���������ĵ�ַ  ��������λ�գ���˵���ò���ָ�����Ǹ÷�������Ӧ������������Ҳ���Ǳ��������Ǵӷ�����

    //���ӷ�����ִ��slave no on�������������ڴ�����ĳ�����������˻���slave���±�ѡ��Ϊmaster��ʱ��replicationUnsetMaster�л��masterhost��ΪNULL
    char *masterhost;               /* Hostname of master */ //��ֵ��replicationSetMaster  ����������replicationCron
    // ���������Ķ˿� //slaveof 10.2.2.2 1234 �е�masterhost=10.2.2.2 masterport=1234
    int masterport;                 /* Port of master */ //��ֵ��replicationSetMaster  ����������replicationCron
    // ��ʱʱ��  ���������ĳ�ʱʱ��   ��Ҫ�����ж������ӵ�PING����ʹӵ�����replconf ack����
    int repl_timeout;               /* Timeout after N seconds of master idle */

    /*
    �ӷ���������������������PSYNC���ִ��ͬ�������������Լ������ݿ�����������������ݿ⵱ǰ������״̬��
  ֵ��һ����ǣ���ͬ������ִ��֮ǰ��ֻ�дӷ����������������Ŀͻ��ˣ�������ִ��ͬ������֮����������Ҳ���Ϊ�ӷ������Ŀͻ��ˣ�
    1. ���PSYNC����ִ�е���������ͬ����������ô����������Ҫ��Ϊ�ӷ������Ŀͻ��ˣ����ܽ������ڻ����������д����͸��ӷ�����ִ�С�
    2. ���PSYNC����ִ�е��ǲ�����ͬ����������ô����������Ҫ��Ϊ�ӷ������Ŀͻ��ˣ�������ӷ��������ͱ����ڸ��ƻ�ѹ�����������д���
    ��ˣ���ͬ������ִ��֮�����ӷ�����˫�����ǶԷ��Ŀͻ��ˣ����ǿ��Ի�����Է������������󣬻��߻�����Է���������ظ�
    ����Ϊ����������Ϊ�˴ӷ������Ŀͻ��ˣ��������������ſ���ͨ������д�������ı�ӷ������������״̬������ͬ��������Ҫ�õ���һ�㣬
    ��Ҳ�����������Դӷ�����ִ������������Ļ���
     */

    /*
    ����ͬ����ר�Ŵ���һ��repl_transfer_s�׽���(connectWithMaster)����������ͬ����ͬ����ɺ���replicationAbortSyncTransfer�йرո��׽���
    ����ͬ����ɺ���������Ҫ�򱾴ӷ���������ʵʱKV������Ҫһ��ģ���redisClient,��Ϊredis����ͨ��redisClient�е�fd�����տͻ��˷��͵�KV,
    ����ͬ����ɺ��ʱ��KV���������������ͨ����master(redisClient)��fd������������ͨ�ŵ�
    */
    
    // ������������Ӧ�Ŀͻ��ˣ���readSyncBulkPayload    replicationSetMaster���Ƿ��master��Դ  Ҳ���Ǳ��������������������󣬼�readSyncBulkPayload
    //ʹ�������ĸ������������������������ӷ���������Ϊ����һ�㶼�Ǵӿͻ��˷��͸��������˵ģ�

    //�����ڵ��Ǵӽڵ��ʱ�򣬾ʹ���һ��redisClient�ڵ�����ר��ͬ�����ڵ㷢�����ڵ��ʵʱKV�Ը����ӷ�����  
    //����ӷ��������ӵ��������������ͻ��˸�����KV��������²�����ͨ����redisClient������������ͨ��
    //replicationHandleMasterDisconnection����ΪNULL   �������������ͬ����ʱ�������ǲ���Ӧ����ᴥ��undoConnectWithMaster��������
    redisClient *master;     /* Client that is master for this slave */ //������������Ӷ˿ڣ��򱸻�Ѹ�master��¼��cached_master�У���replicationCacheMaster
    // �����������������PSYNC ʱʹ��  
//����ӷ�����֮ǰ���������������ϣ���ͬ�������ݣ���;���ˣ������Ӷ��˺����replicationCacheMaster��server.cached_master = server.master;��ʾ֮ǰ�����ӵ���������
//��¼��������������������״̬��ԭ���ǣ������һ������������ˣ���ֻ��Ҫ���ִ�ͬ���Ϳ����ˣ�����������һ��ȫ��ͬ��
    redisClient *cached_master; /* Cached master to be reused for PSYNC. */
    int repl_syncio_timeout; /* Timeout for synchronous I/O calls */
    // ���Ƶ�״̬���������Ǵӷ�����ʱʹ�ã�  ��ȡ��������ͬ��������rdb�ļ�����Ϊserver.repl_state = REDIS_REPL_CONNECTED; ��readSyncBulkPayload
    int repl_state;          /* Replication status if the instance is a slave */
    // RDB �ļ����ܵĴ�С
    off_t repl_transfer_size; /* Size of RDB to read from master during sync. */
    // �Ѷ� RDB �ļ����ݵ��ֽ���  repl_transfer_size - repl_transfer_read���ǻ�δ��ȡ��rdb�ļ���С
    off_t repl_transfer_read;  /* Amount of RDB read from master during sync. */
    // ���һ��ִ�� fsync ʱ��ƫ����
    // ���� sync_file_range ����  д����̵��ļ���С����readSyncBulkPayload
    off_t repl_transfer_last_fsync_off; /* Offset when we fsync-ed last time. */

    /*
    ����ͬ����ר�Ŵ���һ��repl_transfer_s�׽���(connectWithMaster)����������ͬ����ͬ����ɺ���replicationAbortSyncTransfer�йرո��׽���
    ����ͬ����ɺ���������Ҫ�򱾴ӷ���������ʵʱKV������Ҫһ��ģ���redisClient,��Ϊredis����ͨ��redisClient�е�fd�����տͻ��˷��͵�KV,
    ����ͬ����ɺ��ʱ��KV���������������ͨ����master(redisClient)��fd������������ͨ�ŵ�
    */
   
    // �����������׽���  ��connectWithMaster  ��������ͬ��  
    //����ͬ����ר�Ŵ���һ��repl_transfer_s�׽���(connectWithMaster)����������ͬ����ͬ����ɺ���replicationAbortSyncTransfer�йرո��׽���
    int repl_transfer_s;     /* Slave -> Master SYNC socket */
    // ���� RDB �ļ�����ʱ�ļ���������
    int repl_transfer_fd;    /* Slave -> Master SYNC temp file descriptor */
    // ���� RDB �ļ�����ʱ�ļ�����
    char *repl_transfer_tmpfile; /* Slave-> master SYNC temp file name */
    // ���һ�ζ��� RDB ���ݵ�ʱ��  �ڽ���RDB��ʱ��ֻҪ���յ�һ�����ݾ͸��¸�ֵ����readSyncBulkPayload
    time_t repl_transfer_lastio; /* Unix time of the latest read, for timeout */
    int repl_serve_stale_data; /* Serve stale data when link is down? */
    // �Ƿ�ֻ���ӷ�������  �ӷ�����Ĭ����ֻ�������ܽ������д����
    int repl_slave_ro;          /* Slave is read only? */
    // ���ӶϿ���ʱ��
    time_t repl_down_since; /* Unix time at which link with master went down */
    // �Ƿ�Ҫ�� SYNC ֮��ر� NODELAY ��
    int repl_disable_tcp_nodelay;   /* Disable TCP_NODELAY after SYNC? */
    // �ӷ��������ȼ�
    int slave_priority;             /* Reported in INFO and used by Sentinel. */
    // �����������ӷ���������ǰ���������� RUN ID
    char repl_master_runid[REDIS_RUN_ID_SIZE+1];  /* Master run id for PSYNC. */
    // ��ʼ��ƫ����
    long long repl_master_initial_offset;         /* Master PSYNC offset. */


    /* Replication script cache. */
    // ���ƽű�����
    // �ֵ�
    dict *repl_scriptcache_dict;        /* SHA1 all slaves are aware of. */
    // FIFO ����
    list *repl_scriptcache_fifo;        /* First in, first out LRU eviction. */
    // ����Ĵ�С
    int repl_scriptcache_size;          /* Max number of elements. */

    /* Synchronous replication. */
    list *clients_waiting_acks;         /* Clients waiting in WAIT command. */
    int get_ack_from_slaves;            /* If true we send REPLCONF GETACK. */
    /* Limits */
    int maxclients;                 /* Max number of simultaneous clients */
    //��Ч�Ƚϼ�freeMemoryIfNeeded��ʵ���ڴ����������  maxmemory������������
    //����ڴ泬����ֵ��������set��ʱ����ӡshared.oomerr
    unsigned long long maxmemory;   /* Max number of memory bytes to use */
    int maxmemory_policy;           /* Policy for key eviction */
    //maxmemory-samples������
    /*
    ����ʱ������û����õ�maxmemory-policy�����ʵ�������һ����LRU��TTL���������LRU��TTL���Բ��������redis������key��
    �����������ļ��е�maxmemory-samples��key��Ϊ�����ؽ��г�������
    */
    int maxmemory_samples;          /* Pricision of random sampling */


    /* Blocked clients */
    unsigned int bpop_blocked_clients; /* Number of clients blocked by lists */
    list *unblocked_clients; /* list of clients to unblock before next loop */
    list *ready_keys;        /* List of readyList structures for BLPOP & co */


    /* Sort parameters - qsort_r() is only available under BSD so we
     * have to take this state global, in order to pass it to sortCompare() */
    int sort_desc;
    int sort_alpha;
    int sort_bypattern;
    int sort_store;


    /* Zip structure config, see redis.conf for more information  */
    size_t hash_max_ziplist_entries;
    size_t hash_max_ziplist_value;
    size_t list_max_ziplist_entries;
    size_t list_max_ziplist_value; //Ĭ��=REDIS_LIST_MAX_ZIPLIST_VALUE������ͨ��list-max-ziplist-value����
    size_t set_max_intset_entries;
    size_t zset_max_ziplist_entries;
    size_t zset_max_ziplist_value;
    size_t hll_sparse_max_bytes;
    /*
     ��ΪserverCron����Ĭ�ϻ���ÿ100����һ�ε�Ƶ�ʸ���unixtime���Ժ�mstime���ԣ��������������Լ�¼��ʱ��ľ�ȷ�Ȳ����ߣ�
     */
    //�������뼶���ȵ�ϵͳ��ǰUNIXʱ���  updateCachedTime�и���
    time_t unixtime;        /* Unix time sampled every cron cycle. */
    //�����˺��뼶���ȵ�ϵͳ��ǰUNIXʱ��� updateCachedTime�и���
    long long mstime;       /* Like 'unixtime' but with milliseconds resolution. */


     /*
    ��һ���ͻ���ִ��SUBSCRIBE�����ĳ����ĳЩƵ����ʱ������ͻ����뱻����Ƶ��֮��ͽ�������һ�ֶ��Ĺ�ϵ��
        Redis������Ƶ���Ķ��Ĺ�ϵ�������ڷ�����״̬��pubsub_channels�ֵ����棬����ֵ�ļ���ĳ�������ĵ�Ƶ����������ֵ����һ������
    ���������¼�����ж������Ƶ���Ŀͻ��ˣ�
        subscribeƵ�����Ĺ�ϵ���浽pubsub_channels��psubscribeģʽ���Ĺ�ϵ���浽pubsub_patterns����

        �ͻ���1����:psubscribe  aaa[12]c, �������ͻ���2publish aaa1c xxx����publish aaa2c xxx��ʱ�򣬿ͻ���1�����ܵ������Ϣ
         subscribe  ---  unsubscribe   Ƶ������   
         psubscribe ---  punsubscribe  ģʽ����
    */
    
    /* Pubsub */
    // �ֵ䣬��ΪƵ����ֵΪ����
    // �����б��������ж���ĳ��Ƶ���Ŀͻ���
    // �¿ͻ������Ǳ���ӵ�����ı�β
    dict *pubsub_channels;  /* Map channels to list of subscribed clients */

    /*
     pubsub_patterns������һ�����������е�ÿ���ڵ㶼������һ��pubsub_pattern�ṹ������ṹ��pattern���Լ�¼�˱����ĵ�ģʽ��
     ��client�������¼�˶���ģʽ�Ŀͻ���
     */
    // ��������¼�˿ͻ��˶��ĵ�����ģʽ������  �����еĳ�Ա�ṹʽpubsubPattern
    list *pubsub_patterns;  /* A list of pubsub_patterns */

    int notify_keyspace_events; /* Events to propagate via Pub/Sub. This is an
                                   xor of REDIS_NOTIFY... flags. */


    /* Cluster */

    int cluster_enabled;      /* Is cluster enabled? */
    //Ĭ��REDIS_CLUSTER_DEFAULT_NODE_TIMEOUT ms // �ȴ� PONG �ظ���ʱ������������ֵ����Ŀ��ڵ���Ϊ PFAIL ���������ߣ�
    //�����õĵط���clusterCron
    mstime_t cluster_node_timeout; /* Cluster node timeout. */
    char *cluster_configfile; /* Cluster auto-generated config file name. */ //��nodes.conf�����룬��clusterLoadConfig
    //server.cluster = zmalloc(sizeof(clusterState));
    //��Ⱥ������ü�����clusterLoadConfig,  server.cluster���ܴ�nodes.conf�����ļ��м���Ҳ�������û��nodes.conf�����ļ����������ļ��ջ����������������ʧ�ܺ�
    //������Ӧ��cluster�ڵ� clusterInit��ʼ�������ռ�
    struct clusterState *cluster;  /* State of the cluster */

    int cluster_migration_barrier; /* Cluster replicas migration barrier. */
    /* Scripting */

    // Lua ����
    lua_State *lua; /* The Lua interpreter. We use just one for all clients */
    
    // ����ִ�� Lua �ű��е� Redis �����α�ͻ���
    //���������ڳ�ʼ��ʱ��������ִ��Lua�ű��а�����Redis�����α�ͻ��ˣ��������α�ͻ��˹����ڷ�����״̬�ṹ��lua_client������
    redisClient *lua_client;   /* The "fake client" to query Redis from Lua */

    // ��ǰ����ִ�� EVAL ����Ŀͻ��ˣ����û�о��� NULL
    redisClient *lua_caller;   /* The client running EVAL right now, or NULL */

    // һ���ֵ䣬ֵΪ Lua �ű�����Ϊ�ű��� SHA1 У���
    dict *lua_scripts;         /* A dictionary of SHA1 -> Lua scripts */
    // Lua �ű���ִ��ʱ��
    mstime_t lua_time_limit;  /* Script timeout in milliseconds */
    // �ű���ʼִ�е�ʱ��
    mstime_t lua_time_start;  /* Start time of script, milliseconds time */

    // �ű��Ƿ�ִ�й�д����
    int lua_write_dirty;  /* True if a write command was called during the
                             execution of the current script. */

    // �ű��Ƿ�ִ�й�����������ʵ�����
    int lua_random_dirty; /* True if a random command was called during the
                             execution of the current script. */

    // �ű��Ƿ�ʱ
    int lua_timedout;     /* True if we reached the time limit for script
                             execution. */

    // �Ƿ�Ҫɱ���ű�
    int lua_kill;         /* Kill the script if true. */


    /* Assert & bug reporting */

    char *assert_failed;
    char *assert_file;
    int assert_line;
    int bug_report_start; /* True if bug report header was already logged. */
    int watchdog_period;  /* Software watchdog period in ms. 0 = off */
};

/*
 * ��¼����ģʽ�Ľṹ
 */
typedef struct pubsubPattern { //��Ϊpsubscribe ģʽ���Ĵ�������ؽṹ���ο�psubscribeCommand

    // ����ģʽ�Ŀͻ���
    redisClient *client;

    // �����ĵ�ģʽ
    robj *pattern;  //publish channel  message��ʱ����Ҫ��channel��pattern����ƥ���жϣ�ƥ����̲ο�stringmatchlen

} pubsubPattern;

typedef void redisCommandProc(redisClient *c);
typedef int *redisGetKeysProc(struct redisCommand *cmd, robj **argv, int argc, int *numkeys);

/*
 * Redis ����
 */
struct redisCommand { //redisCommandTable��ʹ��

    // ��������
    char *name;

    // ʵ�ֺ���  ָ�������ʵ�ֺ���
    redisCommandProc *proc; //����ִ����processCommand->call

/*  
��������ĸ��������ڼ����������ĸ�ʽ�Ƿ���ȷ��������ֵΪ����-N����ô��ʾ�������������ڵ���N��ע��
��������ֱ���Ҳ��һ������������˵SET msg hello world ����Ĳ��������� 
*/
    // ��������
    int arity;

//�ַ�����ʽ�ı�ʶֵ�����ֵ��¼����������ԣ��������������д����Ƕ������������Ƿ���������������ʱʹ�ã���������Ƿ�������Lua�ű���ʹ�õȵ�                   
    // �ַ�����ʾ�� FLAG
    char *sflags; /* Flags as string representation, one char per flag. */

//��sflags��ʶ���з����ó��Ķ����Ʊ�ʶ���ɳ����Զ����ɡ��������������ʶ���м��ʱʹ�õĶ���flags���Զ�����sflags���ԣ���Ϊ�Զ����Ʊ�ʶ�ļ����Է����ͨ������^��-�Ȳ������
    // ʵ�� FLAG
    int flags;    /* The actual flags, obtained from the 'sflags' field. */

    /* Use a function to determine keys arguments in a command line.
     * Used for Redis Cluster redirect. */
    // ���������ж�����ļ��������� Redis ��Ⱥת��ʱʹ�á�
    redisGetKeysProc *getkeys_proc;

    /* What keys should be loaded in background when calling this command? */
    // ָ����Щ������ key
    int firstkey; /* The first argument that's a key (0 = no keys) */
    int lastkey;  /* The last argument that's a key */
    int keystep;  /* The step between first and last key */

    // ͳ����Ϣ
    // microseconds ��¼������ִ�кķѵ��ܺ�΢����
    // calls �����ִ�е��ܴ���
    long long microseconds,  //������ִ������������ķѵ���ʱ�� 
    calls; //�������ܹ�ִ���˶��ٴ��������
};

struct redisFunctionSym {
    char *name;
    unsigned long pointer;
};

/*
SORT����Ϊÿ��������ļ�������һ�����������ͬ�����飬�����ÿ�����һ��_redisSortObject�ṹ������SORT����ʹ�õ�ѡ�ͬ��
����ʹ��_redisSortObject�ṹ�ķ�ʽҲ��ͬ�����������˳������ݴ�����ʵ�֣������鿪ʼ������ĩβ��������߽���
*/
// ���ڱ��汻����ֵ����Ȩ�صĽṹ
typedef struct _redisSortObject {

    // ���������ֵ
    robj *obj;

    // Ȩ��
    union {

        // ��������ֵʱʹ��
        double score;

        // �����ַ���ʱʹ��
        robj *cmpobj;

    } u;

} redisSortObject;

// �������
typedef struct _redisSortOperation {

    // ���������ͣ������� GET �� DEL ��INCR ���� DECR
    // Ŀǰֻʵ���� GET
    int type;

    // �û�������ģʽ
    robj *pattern;

} redisSortOperation;

/* Structure to hold list iteration abstraction.
 *
 * �б����������
 */
typedef struct {

    // �б����
    robj *subject;

    // ������ʹ�õı���
    unsigned char encoding;

    // �����ķ���
    unsigned char direction; /* Iteration direction */

    // ziplist ���������� ziplist ������б�ʱʹ��
    unsigned char *zi;

    // ����ڵ��ָ�룬����˫�����������б�ʱʹ��
    listNode *ln;

} listTypeIterator;

/* Structure for an entry while iterating over a list.
 *
 * �����б�ʱʹ�õļ�¼�ṹ��
 * ���ڱ�����������Լ����������ص��б�ڵ㡣
 */
typedef struct {

    // �б������
    listTypeIterator *li;

    // ziplist �ڵ�����
    unsigned char *zi;  /* Entry in ziplist */

    // ˫������ڵ�ָ��
    listNode *ln;       /* Entry in linked list */

} listTypeEntry;

/* Structure to hold set iteration abstraction. */
/*
 * ��̬���ϵ�����
 */
typedef struct {

    // �������Ķ���
    robj *subject;

    // ����ı���
    int encoding;

    // ����ֵ������Ϊ intset ʱʹ��
    int ii; /* intset iterator */

    // �ֵ������������Ϊ HT ʱʹ��
    dictIterator *di;

} setTypeIterator;

/* Structure to hold hash iteration abstraction. Note that iteration over
 * hashes involves both fields and values. Because it is possible that
 * not both are required, store pointers in the iterator to avoid
 * unnecessary memory allocation for fields/values. */
/*
 * ��ϣ����ĵ�����
 */
typedef struct {

    // �������Ĺ�ϣ����
    robj *subject;

    // ��ϣ����ı���
    int encoding;

    // ��ָ���ֵָ��
    // �ڵ��� ZIPLIST ����Ĺ�ϣ����ʱʹ��
    unsigned char *fptr, *vptr;

    // �ֵ��������ָ��ǰ�����ֵ�ڵ��ָ��
    // �ڵ��� HT ����Ĺ�ϣ����ʱʹ��
    dictIterator *di;
    dictEntry *de;
} hashTypeIterator;

#define REDIS_HASH_KEY 1
#define REDIS_HASH_VALUE 2

/*-----------------------------------------------------------------------------
 * Extern declarations
 *----------------------------------------------------------------------------*/

extern struct redisServer server;
extern struct sharedObjectsStruct shared;
extern dictType setDictType;
extern dictType zsetDictType;
extern dictType clusterNodesDictType;
extern dictType clusterNodesBlackListDictType;
extern dictType dbDictType;
extern dictType shaScriptObjectDictType;
extern double R_Zero, R_PosInf, R_NegInf, R_Nan;
extern dictType hashDictType;
extern dictType replScriptCacheDictType;

/*-----------------------------------------------------------------------------
 * Functions prototypes
 *----------------------------------------------------------------------------*/

/* Utils */
long long ustime(void);
long long mstime(void);
void getRandomHexChars(char *p, unsigned int len);
uint64_t crc64(uint64_t crc, const unsigned char *s, uint64_t l);
void exitFromChild(int retcode);
size_t redisPopcount(void *s, long count);
void redisSetProcTitle(char *title);

/* networking.c -- Networking and Client related operations */
redisClient *createClient(int fd);
void closeTimedoutClients(void);
void freeClient(redisClient *c, const char *func, unsigned int line);
void freeClientAsync(redisClient *c);
void resetClient(redisClient *c);
void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask);
void addReply(redisClient *c, robj *obj);
void *addDeferredMultiBulkLength(redisClient *c);
void setDeferredMultiBulkLength(redisClient *c, void *node, long length);
void addReplySds(redisClient *c, sds s);
void processInputBuffer(redisClient *c);
void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask);
void acceptUnixHandler(aeEventLoop *el, int fd, void *privdata, int mask);
void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask);
void addReplyBulk(redisClient *c, robj *obj);
void addReplyBulkCString(redisClient *c, char *s);
void addReplyBulkCBuffer(redisClient *c, void *p, size_t len);
void addReplyBulkLongLong(redisClient *c, long long ll);
void acceptHandler(aeEventLoop *el, int fd, void *privdata, int mask);
void addReply(redisClient *c, robj *obj);
void addReplySds(redisClient *c, sds s);
void addReplyError(redisClient *c, char *err);
void addReplyStatus(redisClient *c, char *status);
void addReplyDouble(redisClient *c, double d);
void addReplyLongLong(redisClient *c, long long ll);
void addReplyMultiBulkLen(redisClient *c, long length);
void copyClientOutputBuffer(redisClient *dst, redisClient *src);
void *dupClientReplyValue(void *o);
void getClientsMaxBuffers(unsigned long *longest_output_list,
                          unsigned long *biggest_input_buffer);
void formatPeerId(char *peerid, size_t peerid_len, char *ip, int port);
char *getClientPeerId(redisClient *client);
sds catClientInfoString(sds s, redisClient *client);
sds getAllClientsInfoString(void);
void rewriteClientCommandVector(redisClient *c, int argc, ...);
void rewriteClientCommandArgument(redisClient *c, int i, robj *newval);
unsigned long getClientOutputBufferMemoryUsage(redisClient *c);
void freeClientsInAsyncFreeQueue(void);
void asyncCloseClientOnOutputBufferLimitReached(redisClient *c);
int getClientLimitClassByName(char *name);
char *getClientLimitClassName(int class);
void flushSlavesOutputBuffers(void);
void disconnectSlaves(void);
int listenToPort(int port, int *fds, int *count);
void pauseClients(mstime_t duration);
int clientsArePaused(void);
int processEventsWhileBlocked(void);

#ifdef __GNUC__
void addReplyErrorFormat(redisClient *c, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void addReplyStatusFormat(redisClient *c, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
void addReplyErrorFormat(redisClient *c, const char *fmt, ...);
void addReplyStatusFormat(redisClient *c, const char *fmt, ...);
#endif

/* List data type */
void listTypeTryConversion(robj *subject, robj *value);
void listTypePush(robj *subject, robj *value, int where);
robj *listTypePop(robj *subject, int where);
unsigned long listTypeLength(robj *subject);
listTypeIterator *listTypeInitIterator(robj *subject, long index, unsigned char direction);
void listTypeReleaseIterator(listTypeIterator *li);
int listTypeNext(listTypeIterator *li, listTypeEntry *entry);
robj *listTypeGet(listTypeEntry *entry);
void listTypeInsert(listTypeEntry *entry, robj *value, int where);
int listTypeEqual(listTypeEntry *entry, robj *o);
void listTypeDelete(listTypeEntry *entry);
void listTypeConvert(robj *subject, int enc);
void unblockClientWaitingData(redisClient *c);
void handleClientsBlockedOnLists(void);
void popGenericCommand(redisClient *c, int where);

/* MULTI/EXEC/WATCH... */
void unwatchAllKeys(redisClient *c);
void initClientMultiState(redisClient *c);
void freeClientMultiState(redisClient *c);
void queueMultiCommand(redisClient *c);
void touchWatchedKey(redisDb *db, robj *key);
void touchWatchedKeysOnFlush(int dbid);
void discardTransaction(redisClient *c);
void flagTransaction(redisClient *c);

/* Redis object implementation */
void decrRefCount(robj *o);
void decrRefCountVoid(void *o);
void incrRefCount(robj *o);
robj *resetRefCount(robj *obj);
void freeStringObject(robj *o);
void freeListObject(robj *o);
void freeSetObject(robj *o);
void freeZsetObject(robj *o);
void freeHashObject(robj *o);
robj *createObject(int type, void *ptr);
robj *createStringObject(char *ptr, size_t len);
robj *createRawStringObject(char *ptr, size_t len);
robj *createEmbeddedStringObject(char *ptr, size_t len);
robj *dupStringObject(robj *o);
int isObjectRepresentableAsLongLong(robj *o, long long *llongval);
robj *tryObjectEncoding(robj *o);
robj *getDecodedObject(robj *o);
size_t stringObjectLen(robj *o);
robj *createStringObjectFromLongLong(long long value);
robj *createStringObjectFromLongDouble(long double value);
robj *createListObject(void);
robj *createZiplistObject(void);
robj *createSetObject(void);
robj *createIntsetObject(void);
robj *createHashObject(void);
robj *createZsetObject(void);
robj *createZsetZiplistObject(void);
int getLongFromObjectOrReply(redisClient *c, robj *o, long *target, const char *msg);
int checkType(redisClient *c, robj *o, int type);
int getLongLongFromObjectOrReply(redisClient *c, robj *o, long long *target, const char *msg);
int getDoubleFromObjectOrReply(redisClient *c, robj *o, double *target, const char *msg);
int getLongLongFromObject(robj *o, long long *target);
int getLongDoubleFromObject(robj *o, long double *target);
int getLongDoubleFromObjectOrReply(redisClient *c, robj *o, long double *target, const char *msg);
char *strEncoding(int encoding);
int compareStringObjects(robj *a, robj *b);
int collateStringObjects(robj *a, robj *b);
int equalStringObjects(robj *a, robj *b);
unsigned long long estimateObjectIdleTime(robj *o);

#define sdsEncodedObject(objptr) \
    (objptr->encoding == REDIS_ENCODING_RAW || objptr->encoding == REDIS_ENCODING_EMBSTR)

/* Synchronous I/O with timeout */
ssize_t syncWrite(int fd, char *ptr, ssize_t size, long long timeout);
ssize_t syncRead(int fd, char *ptr, ssize_t size, long long timeout);
ssize_t syncReadLine(int fd, char *ptr, ssize_t size, long long timeout);

/* Replication */
void replicationFeedSlaves(list *slaves, int dictid, robj **argv, int argc);
void replicationFeedMonitors(redisClient *c, list *monitors, int dictid, robj **argv, int argc);
void updateSlavesWaitingBgsave(int bgsaveerr);
void replicationCron(void);
void replicationHandleMasterDisconnection(void);
void replicationCacheMaster(redisClient *c);
void resizeReplicationBacklog(long long newsize);
void replicationSetMaster(char *ip, int port);
void replicationUnsetMaster(void);
void refreshGoodSlavesCount(void);
void replicationScriptCacheInit(void);
void replicationScriptCacheFlush(void);
void replicationScriptCacheAdd(sds sha1);
int replicationScriptCacheExists(sds sha1);
void processClientsWaitingReplicas(void);
void unblockClientWaitingReplicas(redisClient *c);
int replicationCountAcksByOffset(long long offset);
void replicationSendNewlineToMaster(void);
long long replicationGetSlaveOffset(void);

/* Generic persistence functions */
void startLoading(FILE *fp);
void loadingProgress(off_t pos);
void stopLoading(void);

/* RDB persistence */
#include "rdb.h"

/* AOF persistence */
void flushAppendOnlyFile(int force);
void feedAppendOnlyFile(struct redisCommand *cmd, int dictid, robj **argv, int argc);
void aofRemoveTempFile(pid_t childpid);
int rewriteAppendOnlyFileBackground(void);
int loadAppendOnlyFile(char *filename);
void stopAppendOnly(void);
int startAppendOnly(void);
void backgroundRewriteDoneHandler(int exitcode, int bysignal);
void aofRewriteBufferReset(void);
unsigned long aofRewriteBufferSize(void);

/* Sorted sets data type */

/* Struct to hold a inclusive/exclusive range spec by score comparison. */
// ��ʾ������/�����䷶Χ�Ľṹ
typedef struct {

    // ��Сֵ�����ֵ
    double min, max;

    // ָʾ��Сֵ�����ֵ�Ƿ�*��*�����ڷ�Χ֮��
    // ֵΪ 1 ��ʾ��������ֵΪ 0 ��ʾ����
    int minex, maxex; /* are min or max exclusive? */
} zrangespec;

/* Struct to hold an inclusive/exclusive range spec by lexicographic comparison. */
typedef struct {
    robj *min, *max;  /* May be set to shared.(minstring|maxstring) */
    int minex, maxex; /* are min or max exclusive? */
} zlexrangespec;

zskiplist *zslCreate(void);
void zslFree(zskiplist *zsl);
zskiplistNode *zslInsert(zskiplist *zsl, double score, robj *obj);
unsigned char *zzlInsert(unsigned char *zl, robj *ele, double score);
int zslDelete(zskiplist *zsl, double score, robj *obj);
zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range);
zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range);
double zzlGetScore(unsigned char *sptr);
void zzlNext(unsigned char *zl, unsigned char **eptr, unsigned char **sptr);
void zzlPrev(unsigned char *zl, unsigned char **eptr, unsigned char **sptr);
unsigned int zsetLength(robj *zobj);
void zsetConvert(robj *zobj, int encoding);
unsigned long zslGetRank(zskiplist *zsl, double score, robj *o);

/* Core functions */
int freeMemoryIfNeeded(void);
int processCommand(redisClient *c);
void setupSignalHandlers(void);
struct redisCommand *lookupCommand(sds name);
struct redisCommand *lookupCommandByCString(char *s);
struct redisCommand *lookupCommandOrOriginal(sds name);
void call(redisClient *c, int flags);
void propagate(struct redisCommand *cmd, int dbid, robj **argv, int argc, int flags);
void alsoPropagate(struct redisCommand *cmd, int dbid, robj **argv, int argc, int target);
void forceCommandPropagation(redisClient *c, int flags);
int prepareForShutdown();
#ifdef __GNUC__
void redisLog(int level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
void redisLog(int level, const char *fmt, ...);
#endif
void redisLogRaw(int level, const char *msg);
void redisLogFromHandler(int level, const char *msg);
void usage();
void updateDictResizePolicy(void);
int htNeedsResize(dict *dict);
void oom(const char *msg);
void populateCommandTable(void);
void resetCommandTableStats(void);
void adjustOpenFilesLimit(void);
void closeListeningSockets(int unlink_unix_socket);
void updateCachedTime(void);
void resetServerStats(void);
unsigned int getLRUClock(void);

/* Set data type */
robj *setTypeCreate(robj *value);
int setTypeAdd(robj *subject, robj *value);
int setTypeRemove(robj *subject, robj *value);
int setTypeIsMember(robj *subject, robj *value);
setTypeIterator *setTypeInitIterator(robj *subject);
void setTypeReleaseIterator(setTypeIterator *si);
int setTypeNext(setTypeIterator *si, robj **objele, int64_t *llele);
robj *setTypeNextObject(setTypeIterator *si);
int setTypeRandomElement(robj *setobj, robj **objele, int64_t *llele);
unsigned long setTypeSize(robj *subject);
void setTypeConvert(robj *subject, int enc);

/* Hash data type */
void hashTypeConvert(robj *o, int enc);
void hashTypeTryConversion(robj *subject, robj **argv, int start, int end);
void hashTypeTryObjectEncoding(robj *subject, robj **o1, robj **o2);
robj *hashTypeGetObject(robj *o, robj *key);
int hashTypeExists(robj *o, robj *key);
int hashTypeSet(robj *o, robj *key, robj *value);
int hashTypeDelete(robj *o, robj *key);
unsigned long hashTypeLength(robj *o);
hashTypeIterator *hashTypeInitIterator(robj *subject);
void hashTypeReleaseIterator(hashTypeIterator *hi);
int hashTypeNext(hashTypeIterator *hi);
void hashTypeCurrentFromZiplist(hashTypeIterator *hi, int what,
                                unsigned char **vstr,
                                unsigned int *vlen,
                                long long *vll);
void hashTypeCurrentFromHashTable(hashTypeIterator *hi, int what, robj **dst);
robj *hashTypeCurrentObject(hashTypeIterator *hi, int what);
robj *hashTypeLookupWriteOrCreate(redisClient *c, robj *key);

/* Pub / Sub */
int pubsubUnsubscribeAllChannels(redisClient *c, int notify);
int pubsubUnsubscribeAllPatterns(redisClient *c, int notify);
void freePubsubPattern(void *p);
int listMatchPubsubPattern(void *a, void *b);
int pubsubPublishMessage(robj *channel, robj *message);

/* Keyspace events notification */
void notifyKeyspaceEvent(int type, char *event, robj *key, int dbid);
int keyspaceEventsStringToFlags(char *classes);
sds keyspaceEventsFlagsToString(int flags);

/* Configuration */
void loadServerConfig(char *filename, char *options);
void appendServerSaveParams(time_t seconds, int changes);
void resetServerSaveParams();
struct rewriteConfigState; /* Forward declaration to export API. */
void rewriteConfigRewriteLine(struct rewriteConfigState *state, char *option, sds line, int force);
int rewriteConfig(char *path);

/* db.c -- Keyspace access API */
int removeExpire(redisDb *db, robj *key);
void propagateExpire(redisDb *db, robj *key);
int expireIfNeeded(redisDb *db, robj *key);
long long getExpire(redisDb *db, robj *key);
void setExpire(redisDb *db, robj *key, long long when);
robj *lookupKey(redisDb *db, robj *key);
robj *lookupKeyRead(redisDb *db, robj *key);
robj *lookupKeyWrite(redisDb *db, robj *key);
robj *lookupKeyReadOrReply(redisClient *c, robj *key, robj *reply);
robj *lookupKeyWriteOrReply(redisClient *c, robj *key, robj *reply);
void dbAdd(redisDb *db, robj *key, robj *val);
void dbOverwrite(redisDb *db, robj *key, robj *val);
void setKey(redisDb *db, robj *key, robj *val);
int dbExists(redisDb *db, robj *key);
robj *dbRandomKey(redisDb *db);
int dbDelete(redisDb *db, robj *key);
robj *dbUnshareStringValue(redisDb *db, robj *key, robj *o);
long long emptyDb(void(callback)(void*));
int selectDb(redisClient *c, int id);
void signalModifiedKey(redisDb *db, robj *key);
void signalFlushedDb(int dbid);
unsigned int getKeysInSlot(unsigned int hashslot, robj **keys, unsigned int count);
unsigned int countKeysInSlot(unsigned int hashslot);
unsigned int delKeysInSlot(unsigned int hashslot);
int verifyClusterConfigWithData(void);
void scanGenericCommand(redisClient *c, robj *o, unsigned long cursor);
int parseScanCursorOrReply(redisClient *c, robj *o, unsigned long *cursor);

/* API to get key arguments from commands */
int *getKeysFromCommand(struct redisCommand *cmd, robj **argv, int argc, int *numkeys);
void getKeysFreeResult(int *result);
int *zunionInterGetKeys(struct redisCommand *cmd,robj **argv, int argc, int *numkeys);
int *evalGetKeys(struct redisCommand *cmd, robj **argv, int argc, int *numkeys);
int *sortGetKeys(struct redisCommand *cmd, robj **argv, int argc, int *numkeys);

/* Cluster */
void clusterInit(void);
unsigned short crc16(const char *buf, int len);
unsigned int keyHashSlot(char *key, int keylen);
void clusterCron(void);
void clusterPropagatePublish(robj *channel, robj *message);
void migrateCloseTimedoutSockets(void);
void clusterBeforeSleep(void);

/* Sentinel */
void initSentinelConfig(void);
void initSentinel(void);
void sentinelTimer(void);
char *sentinelHandleConfiguration(char **argv, int argc);
void sentinelIsRunning(void);

/* Scripting */
void scriptingInit(void);

/* Blocked clients */
void processUnblockedClients(void);
void blockClient(redisClient *c, int btype);
void unblockClient(redisClient *c);
void replyToBlockedClientTimedOut(redisClient *c);
int getTimeoutFromObjectOrReply(redisClient *c, robj *object, mstime_t *timeout, int unit);

/* Git SHA1 */
char *redisGitSHA1(void);
char *redisGitDirty(void);
uint64_t redisBuildId(void);

/* Commands prototypes */
void authCommand(redisClient *c);
void pingCommand(redisClient *c);
void echoCommand(redisClient *c);
void setCommand(redisClient *c);
void setnxCommand(redisClient *c);
void setexCommand(redisClient *c);
void psetexCommand(redisClient *c);
void getCommand(redisClient *c);
void delCommand(redisClient *c);
void existsCommand(redisClient *c);
void setbitCommand(redisClient *c);
void getbitCommand(redisClient *c);
void setrangeCommand(redisClient *c);
void getrangeCommand(redisClient *c);
void incrCommand(redisClient *c);
void decrCommand(redisClient *c);
void incrbyCommand(redisClient *c);
void decrbyCommand(redisClient *c);
void incrbyfloatCommand(redisClient *c);
void selectCommand(redisClient *c);
void randomkeyCommand(redisClient *c);
void keysCommand(redisClient *c);
void scanCommand(redisClient *c);
void dbsizeCommand(redisClient *c);
void lastsaveCommand(redisClient *c);
void saveCommand(redisClient *c);
void bgsaveCommand(redisClient *c);
void bgrewriteaofCommand(redisClient *c);
void shutdownCommand(redisClient *c);
void moveCommand(redisClient *c);
void renameCommand(redisClient *c);
void renamenxCommand(redisClient *c);
void lpushCommand(redisClient *c);
void rpushCommand(redisClient *c);
void lpushxCommand(redisClient *c);
void rpushxCommand(redisClient *c);
void linsertCommand(redisClient *c);
void lpopCommand(redisClient *c);
void rpopCommand(redisClient *c);
void llenCommand(redisClient *c);
void lindexCommand(redisClient *c);
void lrangeCommand(redisClient *c);
void ltrimCommand(redisClient *c);
void typeCommand(redisClient *c);
void lsetCommand(redisClient *c);
void saddCommand(redisClient *c);
void sremCommand(redisClient *c);
void smoveCommand(redisClient *c);
void sismemberCommand(redisClient *c);
void scardCommand(redisClient *c);
void spopCommand(redisClient *c);
void srandmemberCommand(redisClient *c);
void sinterCommand(redisClient *c);
void sinterstoreCommand(redisClient *c);
void sunionCommand(redisClient *c);
void sunionstoreCommand(redisClient *c);
void sdiffCommand(redisClient *c);
void sdiffstoreCommand(redisClient *c);
void sscanCommand(redisClient *c);
void syncCommand(redisClient *c);
void flushdbCommand(redisClient *c);
void flushallCommand(redisClient *c);
void sortCommand(redisClient *c);
void lremCommand(redisClient *c);
void rpoplpushCommand(redisClient *c);
void infoCommand(redisClient *c);
void mgetCommand(redisClient *c);
void monitorCommand(redisClient *c);
void expireCommand(redisClient *c);
void expireatCommand(redisClient *c);
void pexpireCommand(redisClient *c);
void pexpireatCommand(redisClient *c);
void getsetCommand(redisClient *c);
void ttlCommand(redisClient *c);
void pttlCommand(redisClient *c);
void persistCommand(redisClient *c);
void slaveofCommand(redisClient *c);
void debugCommand(redisClient *c);
void msetCommand(redisClient *c);
void msetnxCommand(redisClient *c);
void zaddCommand(redisClient *c);
void zincrbyCommand(redisClient *c);
void zrangeCommand(redisClient *c);
void zrangebyscoreCommand(redisClient *c);
void zrevrangebyscoreCommand(redisClient *c);
void zrangebylexCommand(redisClient *c);
void zrevrangebylexCommand(redisClient *c);
void zcountCommand(redisClient *c);
void zlexcountCommand(redisClient *c);
void zrevrangeCommand(redisClient *c);
void zcardCommand(redisClient *c);
void zremCommand(redisClient *c);
void zscoreCommand(redisClient *c);
void zremrangebyscoreCommand(redisClient *c);
void zremrangebylexCommand(redisClient *c);
void multiCommand(redisClient *c);
void execCommand(redisClient *c);
void discardCommand(redisClient *c);
void blpopCommand(redisClient *c);
void brpopCommand(redisClient *c);
void brpoplpushCommand(redisClient *c);
void appendCommand(redisClient *c);
void strlenCommand(redisClient *c);
void zrankCommand(redisClient *c);
void zrevrankCommand(redisClient *c);
void hsetCommand(redisClient *c);
void hsetnxCommand(redisClient *c);
void hgetCommand(redisClient *c);
void hmsetCommand(redisClient *c);
void hmgetCommand(redisClient *c);
void hdelCommand(redisClient *c);
void hlenCommand(redisClient *c);
void zremrangebyrankCommand(redisClient *c);
void zunionstoreCommand(redisClient *c);
void zinterstoreCommand(redisClient *c);
void zscanCommand(redisClient *c);
void hkeysCommand(redisClient *c);
void hvalsCommand(redisClient *c);
void hgetallCommand(redisClient *c);
void hexistsCommand(redisClient *c);
void hscanCommand(redisClient *c);
void configCommand(redisClient *c);
void hincrbyCommand(redisClient *c);
void hincrbyfloatCommand(redisClient *c);
void subscribeCommand(redisClient *c);
void unsubscribeCommand(redisClient *c);
void psubscribeCommand(redisClient *c);
void punsubscribeCommand(redisClient *c);
void publishCommand(redisClient *c);
void pubsubCommand(redisClient *c);
void watchCommand(redisClient *c);
void unwatchCommand(redisClient *c);
void clusterCommand(redisClient *c);
void restoreCommand(redisClient *c);
void migrateCommand(redisClient *c);
void askingCommand(redisClient *c);
void readonlyCommand(redisClient *c);
void readwriteCommand(redisClient *c);
void dumpCommand(redisClient *c);
void objectCommand(redisClient *c);
void clientCommand(redisClient *c);
void evalCommand(redisClient *c);
void evalShaCommand(redisClient *c);
void scriptCommand(redisClient *c);
void timeCommand(redisClient *c);
void bitopCommand(redisClient *c);
void bitcountCommand(redisClient *c);
void bitposCommand(redisClient *c);
void replconfCommand(redisClient *c);
void waitCommand(redisClient *c);
void pfselftestCommand(redisClient *c);
void pfaddCommand(redisClient *c);
void pfcountCommand(redisClient *c);
void pfmergeCommand(redisClient *c);
void pfdebugCommand(redisClient *c);

#if defined(__GNUC__)
void *calloc(size_t count, size_t size) __attribute__ ((deprecated));
void free(void *ptr) __attribute__ ((deprecated));
void *malloc(size_t size) __attribute__ ((deprecated));
void *realloc(void *ptr, size_t size) __attribute__ ((deprecated));
#endif

/* Debugging stuff */
void _redisAssertWithInfo(redisClient *c, robj *o, char *estr, char *file, int line);
void _redisAssert(char *estr, char *file, int line);
void _redisPanic(char *msg, char *file, int line);
void bugReportStart(void);
void redisLogObjectDebugInfo(robj *o);
void sigsegvHandler(int sig, siginfo_t *info, void *secret);
sds genRedisInfoString(char *section);
void enableWatchdog(int period);
void disableWatchdog(void);
void watchdogScheduleSignal(int period);
void redisLogHexDump(int level, char *descr, void *value, size_t len);

#define redisDebug(fmt, ...) \
    printf("DEBUG %s:%d > " fmt "\n", __FILE__, __LINE__, __VA_ARGS__)
#define redisDebugMark() \
    printf("-- MARK %s:%d --\n", __FILE__, __LINE__)

#endif
