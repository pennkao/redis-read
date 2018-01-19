/* Redis Cluster implementation.
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
 
/*
����Gossip
    ���ﻹ��һ�����⣬�����Ⱥ�й���N���ڵ�Ļ��������½ڵ�������ʱ���ѵ��������е�ÿ���ڵ㣬����Ҫ����
 һ�Ρ�CLUSTER  MEET������ýڵ���ܱ���Ⱥ�е������ڵ�����ʶ�𣿵�Ȼ������ô����ֻҪͨ��GossipЭ�飬
 ֻ����Ⱥ�е���һ�ڵ㷢������½����ܼ��뵽��Ⱥ�У����������нڵ�����ʶ��

    Gossip�Ƿֲ�ʽϵͳ�б��㷺ʹ�õ�Э�飬����Ҫ����ʵ�ֲַ�ʽ�ڵ�֮�����Ϣ������Gossip�㷨��������
 ��������ڰ칫�Ұ��ԣ�ֻҪһ���˰���һ�£������޵�ʱ�������е��˶���֪���ð��Ե���Ϣ��Ҳ������
 ν�ġ�һ��ʮ��ʮ���١������ַ�ʽҲ�벡���������ƣ����Gossip���ڶ�ı������л��㷨���������鴫
 ���㷨������������Ⱦ�㷨������ҥ�Դ����㷨����
 
    Gossip���ص��ǣ���һ���н������У�ÿ���ڵ㶼������������ڵ�ͨ�ţ�����һ���������µ�ͨ�ţ�����
 ���нڵ��״̬������һ�¡�ÿ���ڵ����֪�����������ڵ㣬Ҳ���ܽ�֪�������ھӽڵ㣬ֻҪ��Щ�ڿ���
 ͨ��������ͨ���������ǵ�״̬����һ�µģ���Ȼ��Ҳ�����鴫�����ص㡣
    Gossip��һ������һ�����㷨����Ȼ�޷���֤��ĳ��ʱ�����нڵ�״̬һ�£������Ա�֤�ڡ����ա����н�
 ��һ�£������ա���һ����ʵ�д��ڣ����������޷�֤����ʱ��㡣��Gossip��ȱ��Ҳ�����ԣ�����ͨ�Ż�
 ����·����CPU��Դ��ɺܴ�ĸ��ء�

    ���嵽Redis��Ⱥ�ж��ԣ�Redis��Ⱥ�е�ÿ���ڵ㣬ÿ��һ��ʱ��ͻ��������ڵ㷢�����������������г��˰�
 ���Լ�����Ϣ֮�⣬���������������ʶ�������ڵ����Ϣ���������ν��gossip���֡�
    �ڵ��յ��������󣬻��������Ƿ�����Լ�������ʶ�Ľڵ㣬���У��ͻ���ýڵ㷢���������̡�
 �ٸ����ӣ������Ⱥ�У���A��B��C��D�ĸ��ڵ㣬A��B�໥��ʶ��C��D�໥��ʶ����ʱֻҪ�ͻ�����A���͡� CLUSTER  MEET nodeC_ip  nodeC_port�����
 ��A����ڵ�C����MEET��ʱ����MEET���л�����нڵ�B����Ϣ��C�յ���MEET���󣬲�����ʶ��A�ڵ㣬Ҳ����ʶB�ڵ㡣
 ͬ����C��������A��B����PING��ʱ����PING����Ҳ����нڵ�D����Ϣ������A��BҲ����ʶ��D�ڵ㡣��ˣ�����һ��ʱ
 ��֮��A��B��C��D�ĸ��ڵ���໥��ʶ�ˡ�
*/

#include "redis.h"
#include "cluster.h"
#include "endianconv.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/file.h>

/*
http://www.cnblogs.com/tankaixiong/articles/4022646.html

http://blog.csdn.net/dc_726/article/details/48552531

��Ⱥ  
CLUSTER INFO ��ӡ��Ⱥ����Ϣ  
CLUSTER NODES �г���Ⱥ��ǰ��֪�����нڵ㣨node�����Լ���Щ�ڵ�������Ϣ��  
�ڵ�  
CLUSTER MEET <ip> <port> �� ip �� port ��ָ���Ľڵ���ӵ���Ⱥ���У�������Ϊ��Ⱥ��һ���ӡ� 
//���ĳ���ڵ���ˣ���ô�ڵ��ڼ�Ⱥ�б��Ϊfail״̬������ͨ��cluster forget�Ѹýڵ�Ӽ�Ⱥ���Ƴ�(cluster nodes�Ϳ������ýڵ���)����������ڵ��������ǻ��Զ�����ü�Ⱥ��
CLUSTER REPLICATE <node_id> ����ǰ�ڵ�����Ϊ node_id ָ���Ľڵ�Ĵӽڵ㡣  
CLUSTER FORGET <node_id> �Ӽ�Ⱥ���Ƴ� node_id ָ���Ľڵ㡣  ���Ҫ�Ӽ�Ⱥ���Ƴ��ýڵ㣬��Ҫ�ȼ�Ⱥ�е����нڵ㷢��cluster forget
CLUSTER SAVECONFIG ���ڵ�������ļ����浽Ӳ�����档  
��(slot)  
CLUSTER ADDSLOTS <slot> [slot ...] ��һ�������ۣ�slot��ָ�ɣ�assign������ǰ�ڵ㡣  
CLUSTER DELSLOTS <slot> [slot ...] �Ƴ�һ�������۶Ե�ǰ�ڵ��ָ�ɡ�  
CLUSTER FLUSHSLOTS �Ƴ�ָ�ɸ���ǰ�ڵ�����вۣ��õ�ǰ�ڵ���һ��û��ָ���κβ۵Ľڵ㡣  
CLUSTER SLOTS  �鿴��λ�ֲ�

CLUSTER SETSLOT <slot> NODE <node_id> ���� slot ָ�ɸ� node_id ָ���Ľڵ㣬������Ѿ�ָ�ɸ���һ���ڵ㣬��ô������һ���ڵ�ɾ���ò�>��Ȼ���ٽ���ָ�ɡ�  
CLUSTER SETSLOT <slot> MIGRATING <node_id> �����ڵ�Ĳ� slot Ǩ�Ƶ� node_id ָ���Ľڵ��С�  
CLUSTER SETSLOT <slot> IMPORTING <node_id> �� node_id ָ���Ľڵ��е���� slot �����ڵ㡣  
CLUSTER SETSLOT <slot> STABLE ȡ���Բ� slot �ĵ��루import������Ǩ�ƣ�migrate����  
�����������MIGRATE host port key destination-db timeout replace���в�λresharding
�ڵ�Ĳ�λ�仯��ͨ��PING PONG��Ⱥ�ڵ㽻���㲥�ģ���clusterMsg->myslots[]��Я����ȥ��Ҳ����ͨ��clusterSendUpdate(clusterMsgDataUpdate.slots)���ͳ�ȥ

�ο� redis�����ʵ�� ��17�� ��Ⱥ  17.4 ���·�Ƭ
redis-cli -c -h 192.168.1.100 -p 7000 cluster addslots {0..5000} ͨ��redis-cli����slot��Χ�����ǲ���redis-cli���������У�Ȼ����cluster addslots {0..5000}
127.0.0.1:7000> cluster addslots {0..5000}
(error) ERR Invalid or out of range slot
127.0.0.1:7000> quit
[root@s10-2-4-4 yazhou.yang]# redis-cli -c  -p 7000 cluster addslots {0..5000}  ����ʵ��������redis-cli�Ѹ÷�Χ�滻Ϊcluster addslots 0 1 2 3 .. 5000�����͸�redis��
OK
[root@s10-2-4-4 yazhou.yang]# 


��  
CLUSTER KEYSLOT <key> ����� key Ӧ�ñ��������ĸ����ϡ�  
CLUSTER COUNTKEYSINSLOT <slot> ���ز� slot Ŀǰ�����ļ�ֵ��������  
CLUSTER GETKEYSINSLOT <slot> <count> ���� count �� slot ���еļ���

//CLUSTER SLAVES <NODE ID> ��ӡ�������ڵ�����дӽڵ����Ϣ 
//���⣬Manual Failover��force�ͷ�force���������ڣ���force��Ҫ�ȴӽڵ���ȫͬ�������ڵ�����ݺ�Ž���failover����֤����ʧ���ݣ���������У�ԭ���ڵ�ֹͣд��������force�����н�����������ͬ����ֱ�ӽ���failover��
//CLUSTER FAILOVER [FORCE]  ִ���ֶ�����ת��            ֻ�ܷ���slave
//cluster set-config-epoch <epoch>����ǿ������configEpoch
//CLUSTER RESET [SOFT|HARD]��Ⱥ��λ��������Ƚ�Σ��

��Ⱥ����:http://carlosfu.iteye.com/blog/2254573   http://www.soso.io/article/68131.html
*/

/*
��Ⱥ�ڵ������ͨ��PING PONG �ȱ��Ľ�����֪�ģ�������ͨ��epoll errorʱ���֪
*/

/* A global reference to myself is handy to make code more clear.
 * Myself always points to server.cluster->myself, that is, the clusterNode
 * that represents this node. */
// Ϊ�˷��������ά��һ�� myself ȫ�ֱ�������������ָ�� cluster->myself ��
////��Ⱥģʽ������nodes.conf�������ã���nodes.conf�����а���myself��������Ϊ��������
//���߷Ǽ�Ⱥ��ʽ����nodes.conf�����ڻ���û�����ã�����clusterLoadConfig����ʧ�ܺ�����myself
clusterNode *myself = NULL;

clusterNode *createClusterNode(char *nodename, int flags);
int clusterAddNode(clusterNode *node);
void clusterAcceptHandler(aeEventLoop *el, int fd, void *privdata, int mask);
void clusterReadHandler(aeEventLoop *el, int fd, void *privdata, int mask);
void clusterSendPing(clusterLink *link, int type);
void clusterSendFail(char *nodename);
void clusterSendFailoverAuthIfNeeded(clusterNode *node, clusterMsg *request);
void clusterUpdateState(void);
int clusterNodeGetSlotBit(clusterNode *n, int slot);
sds clusterGenNodesDescription(int filter);
clusterNode *clusterLookupNode(char *name);
int clusterNodeAddSlave(clusterNode *master, clusterNode *slave);
int clusterAddSlot(clusterNode *n, int slot);
int clusterDelSlot(int slot);
int clusterDelNodeSlots(clusterNode *node);
int clusterNodeSetSlotBit(clusterNode *n, int slot);
void clusterSetMaster(clusterNode *n);
void clusterHandleSlaveFailover(void);
void clusterHandleSlaveMigration(int max_slaves);
int bitmapTestBit(unsigned char *bitmap, int pos);
void clusterDoBeforeSleep(int flags);
void clusterSendUpdate(clusterLink *link, clusterNode *node);
void resetManualFailover(void);
void clusterCloseAllSlots(void);
void clusterSetNodeAsMaster(clusterNode *n);
void clusterDelNode(clusterNode *delnode);

/* -----------------------------------------------------------------------------
 * Initialization
 * -------------------------------------------------------------------------- */

/* Return the greatest configEpoch found in the cluster. */
uint64_t clusterGetMaxEpoch(void) {
    uint64_t max = 0;
    dictIterator *di;
    dictEntry *de;

    // ѡ���ڵ��е�����Ԫ
    di = dictGetSafeIterator(server.cluster->nodes);
    while((de = dictNext(di)) != NULL) {
        clusterNode *node = dictGetVal(de);
        if (node->configEpoch > max) max = node->configEpoch;
    }
    dictReleaseIterator(di);
    if (max < server.cluster->currentEpoch) max = server.cluster->currentEpoch;
    return max;
}

/* Load the cluster config from 'filename'.
 *
 * If the file does not exist or is zero-length (this may happen because
 * when we lock the nodes.conf file, we create a zero-length one for the
 * sake of locking if it does not already exist), REDIS_ERR is returned.
 * If the configuration was loaded from the file, REDIS_OK is returned. */
int clusterLoadConfig(char *filename) {// ���뼯Ⱥ����          clusterSaveConfig��clusterLoadConfig��Ӧ
    FILE *fp = fopen(filename,"r");
    struct stat sb;
    char *line;
    int maxline, j;

    if (fp == NULL) { //node.conf�ļ�������
        if (errno == ENOENT) {
            return REDIS_ERR;
        } else {
            redisLog(REDIS_WARNING,
                "Loading the cluster node config from %s: %s",
                filename, strerror(errno));
            exit(1);
        }
    }

    /* Check if the file is zero-length: if so return REDIS_ERR to signal
     * we have to write the config. */
    if (fstat(fileno(fp),&sb) != -1 && sb.st_size == 0) { //�ļ�����Ϊ��
        fclose(fp);
        return REDIS_ERR;
    }

    /* Parse the file. Note that single liens of the cluster config file can     
    * be really long as they include all the hash slots of the node.    
    * ��Ⱥ�����ļ��е��п��ܻ�ǳ�����     
    * ��Ϊ�������������¼���й�ϣ�۵Ľڵ㡣     
    *     * This means in the worst possible case, half of the Redis slots will be     
    * present in a single line, possibly in importing or migrating state, so     
    * together with the node ID of the sender/receiver.     *     
    * �������£�һ���п��ܱ����˰����Ĺ�ϣ�����ݣ�     
    * ���ҿ��ܴ��е���򵼳�״̬���Լ������ߺͽ����ߵ� ID ��     
    *     * To simplify we allocate 1024+REDIS_CLUSTER_SLOTS*128 bytes per line.      *     
    * Ϊ�˼����������Ϊÿ�з��� 1024+REDIS_CLUSTER_SLOTS*128 �ֽڵĿռ�     */

    maxline = 1024+REDIS_CLUSTER_SLOTS*128;
    line = zmalloc(maxline);
    while(fgets(line,maxline,fp) != NULL) {
        int argc;
        sds *argv;
        clusterNode *n, *master;
        char *p, *s;

        /* Skip blank lines, they can be created either by users manually
         * editing nodes.conf or by the config writing process if stopped
         * before the truncate() call. */
        if (line[0] == '\n') continue;

        /* Split the line into arguments for processing. */
        argv = sdssplitargs(line,&argc);
        if (argv == NULL) goto fmterr;  

        /* Handle the special "vars" line. Don't pretend it is the last
         * line even if it actually is when generated by Redis. */
        if (strcasecmp(argv[0],"vars") == 0) {
            for (j = 1; j < argc; j += 2) {
                if (strcasecmp(argv[j],"currentEpoch") == 0) {
                    server.cluster->currentEpoch =
                            strtoull(argv[j+1],NULL,10);
                } else if (strcasecmp(argv[j],"lastVoteEpoch") == 0) {
                    server.cluster->lastVoteEpoch =
                            strtoull(argv[j+1],NULL,10);
                } else {
                    redisLog(REDIS_WARNING,
                        "Skipping unknown cluster config variable '%s'",
                        argv[j]);
                }
            }
            continue;
        }

        /* Create this node if it does not exist */
        /// ���ڵ��Ƿ��Ѿ�����
        n = clusterLookupNode(argv[0]);
        if (!n) {
             // δ�����򴴽�����ڵ�
            n = createClusterNode(argv[0],0);
            clusterAddNode(n);
        }
        /* Address and port */
         // ���ýڵ�� ip �� port
        if ((p = strchr(argv[1],':')) == NULL) goto fmterr;
        *p = '\0';
        memcpy(n->ip,argv[1],strlen(argv[1])+1);
        n->port = atoi(p+1);

        /* Parse flags */
         // �����ڵ�� flag
        p = s = argv[2];
        while(p) {
            p = strchr(s,',');
            if (p) *p = '\0';
           // ���ǽڵ㱾��
            if (!strcasecmp(s,"myself")) {
                redisAssert(server.cluster->myself == NULL);
                myself = server.cluster->myself = n; //��Ⱥģʽ������nodes.conf�������ã���nodes.conf�����а���myself��������Ϊ��������
                n->flags |= REDIS_NODE_MYSELF;
            // ����һ�����ڵ�
            } else if (!strcasecmp(s,"master")) {
                n->flags |= REDIS_NODE_MASTER;
            // ����һ���ӽڵ�
            } else if (!strcasecmp(s,"slave")) {
                n->flags |= REDIS_NODE_SLAVE;
            // ����һ���������߽ڵ�
            } else if (!strcasecmp(s,"fail?")) {
                n->flags |= REDIS_NODE_PFAIL;
            // ����һ�������߽ڵ�
            } else if (!strcasecmp(s,"fail")) {
                n->flags |= REDIS_NODE_FAIL;
                n->fail_time = mstime();
           // �ȴ���ڵ㷢�� PING
            } else if (!strcasecmp(s,"handshake")) {
                n->flags |= REDIS_NODE_HANDSHAKE;
            // ��δ�������ڵ�ĵ�ַ
            } else if (!strcasecmp(s,"noaddr")) {
                n->flags |= REDIS_NODE_NOADDR;
            // �� flag
            } else if (!strcasecmp(s,"noflags")) {
                /* nothing to do */
            } else {
                redisPanic("Unknown flag in redis cluster config file");
            }
            if (p) s = p+1;
        }

        /* Get master if any. Set the master and populate master's
         * slave list. */
         // ��������ڵ�Ļ�����ô�������ڵ�
        if (argv[3][0] != '-') {
            master = clusterLookupNode(argv[3]);
            // ������ڵ㲻���ڣ���ô�����
            if (!master) {
                master = createClusterNode(argv[3],0);
                clusterAddNode(master);
            }
             // �������ڵ�
            n->slaveof = master;
            // ���ڵ� n ���뵽���ڵ� master �Ĵӽڵ�������
            clusterNodeAddSlave(master,n);
        }

        /* Set ping sent / pong received timestamps */
       // �������һ�η��� PING �����Լ����� PING ����ظ���ʱ���
        if (atoi(argv[4])) n->ping_sent = mstime();
        if (atoi(argv[5])) n->pong_received = mstime();

        /* Set configEpoch for this node. */
        // �������ü�Ԫ
        n->configEpoch = strtoull(argv[6],NULL,10);

        /* Populate hash slots served by this instance. */
        // ȡ���ڵ����Ĳ�
        for (j = 8; j < argc; j++) {
            int start, stop;

            // ���ڵ���򵼳���
            if (argv[j][0] == '[') {
                /* Here we handle migrating / importing slots */
                int slot;
                char direction;
                clusterNode *cn;

                p = strchr(argv[j],'-');
                redisAssert(p != NULL);
                *p = '\0';
                // ���� or ������
                direction = p[1]; /* Either '>' or '<' */
                 // ��
                slot = atoi(argv[j]+1);
                p += 3;
                // Ŀ��ڵ�
                cn = clusterLookupNode(p);
                // ���Ŀ�겻���ڣ���ô����
                if (!cn) {
                    cn = createClusterNode(p,0);
                    clusterAddNode(cn);
                }
                 // ���ݷ����趨���ڵ�Ҫ������ߵ����Ĳ۵�Ŀ��
                if (direction == '>') {
                    server.cluster->migrating_slots_to[slot] = cn;
                } else {
                    server.cluster->importing_slots_from[slot] = cn;
                }
                continue;

            // û�е���򵼳�������һ�����䷶Χ�Ĳ�            // ���� 0 - 10086
            } else if ((p = strchr(argv[j],'-')) != NULL) {
                *p = '\0';
                start = atoi(argv[j]);
                stop = atoi(p+1);

             // û�е���򵼳������ǵ�һ����            // ���� 10086
            } else {
                start = stop = atoi(argv[j]);
            }

            // ��������ڵ�
            while(start <= stop) clusterAddSlot(n, start++);
        }

        sdsfreesplitres(argv,argc);
    }
    zfree(line);
    fclose(fp);

    /*
    nodes.conf��������һ�п��У�˵��Ҳû�У���redis����������clusterLoadConfig�����⣬����һ�㲻Ҫ
    ȥ����nodes.conf�ļ���������ü�Ⱥ״̬��ϵ����Э�������ֱ��ɾ��nodes.conf�����
    */
    /* Config sanity check */
    redisAssert(server.cluster->myself != NULL); //���nodes.conf�о�ֻ��һ�п��У�����ߵ�����쳣�˳�
    redisLog(REDIS_NOTICE,"Node configuration loaded, I'm %.40s", myself->name);

    /* Something that should never happen: currentEpoch smaller than
     * the max epoch found in the nodes configuration. However we handle this
     * as some form of protection against manual editing of critical files. */
    if (clusterGetMaxEpoch() > server.cluster->currentEpoch) {
        server.cluster->currentEpoch = clusterGetMaxEpoch();
    }
    return REDIS_OK;

fmterr:
    redisLog(REDIS_WARNING,
        "Unrecoverable error: corrupted cluster config file.");
    fclose(fp);
    exit(1);
}

/* Cluster node configuration is exactly the same as CLUSTER NODES output.
 *
 * This function writes the node config and returns 0, on error -1
 * is returned.
 *
 * Note: we need to write the file in an atomic way from the point of view
 * of the POSIX filesystem semantics, so that if the server is stopped
 * or crashes during the write, we'll end with either the old file or the
 * new one. Since we have the full payload to write available we can use
 * a single write to write the whole file. If the pre-existing file was
 * bigger we pad our payload with newlines that are anyway ignored and truncate
 * the file afterward. */
//��������node�ڵ��ȡ��Ӧ�Ľڵ�master slave����Ϣ������:4ae3f6e2ff456e6e397ea6708dac50a16807911c 192.168.1.103:7000 myself,slave dc824af0bff649bb292dbf5b37307a54ed4d361f 0 0 0 connected
//����ͨ��cluster nodes�����ȡ�����ջ�д��nodes.conf

// д�� nodes.conf �ļ�  �������¼�����ݺ�cluster nodes���������Ϣ������ͬ
int clusterSaveConfig(int do_fsync) {
    sds ci;
    size_t content_size;
    struct stat sb;
    int fd;

    server.cluster->todo_before_sleep &= ~CLUSTER_TODO_SAVE_CONFIG;

    /* Get the nodes description and concatenate our "vars" directive to
     * save currentEpoch and lastVoteEpoch. */
    ci = clusterGenNodesDescription(REDIS_NODE_HANDSHAKE);
    ci = sdscatprintf(ci,"vars currentEpoch %llu lastVoteEpoch %llu\n",
        (unsigned long long) server.cluster->currentEpoch,
        (unsigned long long) server.cluster->lastVoteEpoch);
    content_size = sdslen(ci);

    if ((fd = open(server.cluster_configfile,O_WRONLY|O_CREAT,0644))
        == -1) goto err;

    /* Pad the new payload if the existing file length is greater. */
    if (fstat(fd,&sb) != -1) {
        if (sb.st_size > content_size) {
            ci = sdsgrowzero(ci,sb.st_size);
            memset(ci+content_size,'\n',sb.st_size-content_size);
        }
    }
    if (write(fd,ci,sdslen(ci)) != (ssize_t)sdslen(ci)) goto err;
    if (do_fsync) {
        server.cluster->todo_before_sleep &= ~CLUSTER_TODO_FSYNC_CONFIG;
        fsync(fd);
    }

    /* Truncate the file if needed to remove the final \n padding that
     * is just garbage. */
    if (content_size != sdslen(ci) && ftruncate(fd,content_size) == -1) {
        /* ftruncate() failing is not a critical error. */
    }
    close(fd);
    sdsfree(ci);
    return 0;

err:
    if (fd != -1) close(fd);
    sdsfree(ci);
    return -1;
}

// ����д�� nodes.conf �ļ���ʧ�����˳���ֻҪ�ڵ�configEpoch�����仯�����߽ڵ�failover���дnodes.conf�����ļ�
void clusterSaveConfigOrDie(int do_fsync) {
    if (clusterSaveConfig(do_fsync) == -1) { // �������¼�����ݺ�cluster nodes���������Ϣ������ͬ
        redisLog(REDIS_WARNING,"Fatal: can't update cluster config file.");
        exit(1);
    }
}

/* Lock the cluster config using flock(), and leaks the file descritor used to
 * acquire the lock so that the file will be locked forever.
 *
 * This works because we always update nodes.conf with a new version
 * in-place, reopening the file, and writing to it in place (later adjusting
 * the length with ftruncate()).
 *
 * On success REDIS_OK is returned, otherwise an error is logged and
 * the function returns REDIS_ERR to signal a lock was not acquired. */
int clusterLockConfig(char *filename) {
    /* To lock it, we need to open the file in a way it is created if
     * it does not exist, otherwise there is a race condition with other
     * processes. */
    int fd = open(filename,O_WRONLY|O_CREAT,0644);
    if (fd == -1) {
        redisLog(REDIS_WARNING,
            "Can't open %s in order to acquire a lock: %s",
            filename, strerror(errno));
        return REDIS_ERR;
    }

    if (flock(fd,LOCK_EX|LOCK_NB) == -1) {
        if (errno == EWOULDBLOCK) {
            redisLog(REDIS_WARNING,
                 "Sorry, the cluster configuration file %s is already used "
                 "by a different Redis Cluster node. Please make sure that "
                 "different nodes use different cluster configuration "
                 "files.", filename);
        } else {
            redisLog(REDIS_WARNING,
                "Impossible to lock %s: %s", filename, strerror(errno));
        }
        close(fd);
        return REDIS_ERR;
    }
    /* Lock acquired: leak the 'fd' by not closing it, so that we'll retain the
     * lock to the file as long as the process exists. */
    return REDIS_OK;
}

// ��ʼ����Ⱥ
void clusterInit(void) {
    int saveconf = 0;

    // ��ʼ������
    server.cluster = zmalloc(sizeof(clusterState));
    server.cluster->myself = NULL;
    server.cluster->currentEpoch = 0;
    server.cluster->state = REDIS_CLUSTER_FAIL;
    server.cluster->size = 1;
    server.cluster->todo_before_sleep = 0;
    server.cluster->nodes = dictCreate(&clusterNodesDictType,NULL);
    server.cluster->nodes_black_list =
        dictCreate(&clusterNodesBlackListDictType,NULL);
    server.cluster->failover_auth_time = 0;
    server.cluster->failover_auth_count = 0;
    server.cluster->failover_auth_rank = 0;
    server.cluster->failover_auth_epoch = 0;
    server.cluster->lastVoteEpoch = 0;
    server.cluster->stats_bus_messages_sent = 0;
    server.cluster->stats_bus_messages_received = 0;
    server.cluster->cant_failover_reason = REDIS_CLUSTER_CANT_FAILOVER_NONE;
    memset(server.cluster->slots,0, sizeof(server.cluster->slots));
    clusterCloseAllSlots();

    /* Lock the cluster config file to make sure every node uses
     * its own nodes.conf. */
    if (clusterLockConfig(server.cluster_configfile) == REDIS_ERR)
        exit(1);

    /* Load or create a new nodes configuration. */
    if (clusterLoadConfig(server.cluster_configfile) == REDIS_ERR) {
        //��һ�����е�ʱ��nodes.confΪ�ջ���û�и��ļ������ߵ�������
        /* No configuration found. We will just use the random name provided
         * by the createClusterNode() function. */
        myself = server.cluster->myself =
            createClusterNode(NULL,REDIS_NODE_MYSELF|REDIS_NODE_MASTER); //û��nodes.conf����nodes.confΪ�գ�����nodes.conf���������⣬�����Ĭ��Ϊmaster
        redisLog(REDIS_NOTICE,"No cluster configuration found, I'm %.40s",
            myself->name);
        clusterAddNode(myself);
        saveconf = 1;
    }

    // ���� nodes.conf �ļ�  ֻ�����ʼû��nodes.conf����nodes.confΪ�ջ���nodes.conf��ĳЩ���������������²Ż�ִ�е�clusterSaveConfigOrDie
    if (saveconf) clusterSaveConfigOrDie(1);

    /* We need a listening TCP port for our cluster messaging needs. */
    // ���� TCP �˿�
    server.cfd_count = 0;

    /* Port sanity check II
     * The other handshake port check is triggered too late to stop
     * us from trying to use a too-high cluster port number. */
    if (server.port > (65535-REDIS_CLUSTER_PORT_INCR)) { //��Ϊ��Ⱥ�ڲ�ͨ�Ŷ˿��Ǽ����˿ڼ�REDIS_CLUSTER_PORT_INCR��������
        redisLog(REDIS_WARNING, "Redis port number too high. "
                   "Cluster communication port is 10,000 port "
                   "numbers higher than your Redis port. "
                   "Your Redis port number must be "
                   "lower than 55535.");
        exit(1);
    }

    if (listenToPort(server.port+REDIS_CLUSTER_PORT_INCR,
        server.cfd,&server.cfd_count) == REDIS_ERR)
    {
        exit(1);
    } else {
        int j;

        /* ��Ⱥ�ڲ�����ʱ�䴦�� */
        for (j = 0; j < server.cfd_count; j++) {
            // ���������¼�������
            if (aeCreateFileEvent(server.el, server.cfd[j], AE_READABLE,
                clusterAcceptHandler, NULL) == AE_ERR)
                    redisPanic("Unrecoverable error creating Redis Cluster "
                                "file event.");
        }
    }

    /* The slots -> keys map is a sorted set. Init it. */
    // slots -> keys ӳ����һ�����򼯺�
    server.cluster->slots_to_keys = zslCreate();
    resetManualFailover();
}

/* Reset a node performing a soft or hard reset:
 *
 * 1) All other nodes are forget.
 * 2) All the assigned / open slots are released.
 * 3) If the node is a slave, it turns into a master.
 * 5) Only for hard reset: a new Node ID is generated.
 * 6) Only for hard reset: currentEpoch and configEpoch are set to 0.
 * 7) The new configuration is saved and the cluster state updated.  */
void clusterReset(int hard) {
    dictIterator *di;
    dictEntry *de;
    int j;

    /* Turn into master. */
    if (nodeIsSlave(myself)) {
        clusterSetNodeAsMaster(myself);
        replicationUnsetMaster();
    }

    /* Close slots, reset manual failover state. */
    clusterCloseAllSlots();
    resetManualFailover();

    /* Unassign all the slots. */
    for (j = 0; j < REDIS_CLUSTER_SLOTS; j++) clusterDelSlot(j);

    /* Forget all the nodes, but myself. */
    di = dictGetSafeIterator(server.cluster->nodes);
    while((de = dictNext(di)) != NULL) {
        clusterNode *node = dictGetVal(de);

        if (node == myself) continue;
        clusterDelNode(node);
    }
    dictReleaseIterator(di);

    /* Hard reset only: set epochs to 0, change node ID. */
    if (hard) {
        sds oldname;

        server.cluster->currentEpoch = 0;
        server.cluster->lastVoteEpoch = 0;
        myself->configEpoch = 0;

        /* To change the Node ID we need to remove the old name from the
         * nodes table, change the ID, and re-add back with new name. */
        oldname = sdsnewlen(myself->name, REDIS_CLUSTER_NAMELEN);
        dictDelete(server.cluster->nodes,oldname);
        sdsfree(oldname);
        getRandomHexChars(myself->name, REDIS_CLUSTER_NAMELEN);
        clusterAddNode(myself);
    }

    /* Make sure to persist the new config and update the state. */
    clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG|
                         CLUSTER_TODO_UPDATE_STATE|
                         CLUSTER_TODO_FSYNC_CONFIG);
}

/* -----------------------------------------------------------------------------
 * CLUSTER communication link
 * -------------------------------------------------------------------------- */
/*
A MEET B,A����������B����ʱ��A�ڵ��ϼ�¼��clusterNode��link�ǹ����ģ�����B�������Ӻ�����link����ʱ���link��node��ԱΪNULL
������B��A�������ӣ�B�ϼ�¼��A�ڵ��clusterNode��link�ǹ����ģ�����A�������Ӻ�����link��node��ԱΪNULL
ֻ�������������ӵ�ʱ������link����clusterLink.node��Աָ��Զ˵�clusterNode�������������ӵ�һ�˽�����clusterLink,����node��ԱһֱΪNULL
*/

// �����ڵ�����  
/*
ע��createClusterLink��createClusterNode��link��node�Ĺ�ϵ����:

A cluster meet B��ʱ����clusterCommand�ᴴ��B��node��B-node->link=null(״̬ΪREDIS_NODE_HANDSHAKE)��Ȼ����clusterCrone�з���node->linkΪNULL
Ҳ���ǻ�û�к�B-node��������ͬʱ����B-link�����Ƿ���B-node���ӣ�����B-node�����B-link��������B-node->link=B-link��
���A����Bһֱ���Ӳ��ϣ���ʱ���B�������������½���B-node,����Ҫ����ִ��A cluster meet B����

B�յ�A����������clusterAcceptHandler������������A���ӵ�link����link->node=null,Ȼ��ͨ����link����A���͹�����MEET��
B��clusterProcessPacket���յ�MMET�����ֱ���û��A�ڵ�node������clusterProcessPacket����A�ڵ㣬Ȼ����clusterCron�з���A-node->link=NULL
�������������A�����Ӳ�����link�����ӳɹ�����A-node->link=link��

A meet B,A���ػᴴ��A��name��B�յ���Ҳ�ᴴ��һ��A-node�����������ǲ�һ���ģ�ͨ���໥PING PONGͨ��������һ�£���clusterRenameNode

��ˣ�ֻ�������������ӵ�һ����node->link=link,link->node=node,�����������ӵ�һ�˴�����link����link->nodeʼ��ΪNULL
*/
clusterLink *createClusterLink(clusterNode *node) {
    clusterLink *link = zmalloc(sizeof(*link));
    link->ctime = mstime();
    link->sndbuf = sdsempty();
    link->rcvbuf = sdsempty();
    link->node = node;
    link->fd = -1;
    return link;
}

/* Free a cluster link, but does not free the associated node of course.
 * This function will just make sure that the original node associated
 * with this link will have the 'link' field set to NULL. */
 //clusterCron����ڵ��linkΪNULL������Ҫ������������freeClusterLink������ͼ�Ⱥ��ĳ���ڵ��쳣�ҵ���
 //�򱾽ڵ�ͨ����д�¼�����֪����Ȼ����freeClusterLink��ΪNULL
    // ���������������// ��������������ӵĽڵ�� link ������Ϊ NULL
void freeClusterLink(clusterLink *link) { //�����´ν���clusterCron����Ϊ����link=NULL�����º͸�link��Ӧ��NOde��������

    // ɾ���¼�������
    if (link->fd != -1) {
        aeDeleteFileEvent(server.el, link->fd, AE_WRITABLE);
        aeDeleteFileEvent(server.el, link->fd, AE_READABLE);
    }

    // �ͷ����뻺���������������
    sdsfree(link->sndbuf);
    sdsfree(link->rcvbuf);

    // ���ڵ�� link ������Ϊ NULL
    if (link->node)
        link->node->link = NULL;

     // �ر�����
    close(link->fd);

    // �ͷ����ӽṹ
    zfree(link);
}

// �����¼�������

#define MAX_CLUSTER_ACCEPTS_PER_CALL 1000

//�ͻ��������˷���meet�󣬿ͻ���ͨ���ͷ���˽�����������¼����˽ڵ�clusterNode->link��clusterCron
//����˽��յ����Ӻ�ͨ��clusterAcceptHandler�����ͻ��˽ڵ��clusterNode.link����clusterAcceptHandler


//Aͨ��cluster meet bip bport  B��B����clusterAcceptHandler->clusterReadHandler�������ӣ�A��ͨ��
//clusterCommand->clusterStartHandshake����clusterCron->anetTcpNonBlockBindConnect���ӷ�����
void clusterAcceptHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    int cport, cfd;
    int max = MAX_CLUSTER_ACCEPTS_PER_CALL;
    char cip[REDIS_IP_STR_LEN];
    clusterLink *link;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);
    REDIS_NOTUSED(privdata);

    /* If the server is starting up, don't accept cluster connections:
     * UPDATE messages may interact with the database content. */
    if (server.masterhost == NULL && server.loading) return;

    while(max--) {
        cfd = anetTcpAccept(server.neterr, fd, cip, sizeof(cip), &cport);
        if (cfd == ANET_ERR) {
            if (errno != EWOULDBLOCK)
                redisLog(REDIS_VERBOSE,
                    "Accepting cluster node: %s", server.neterr);
            return;
        }
        anetNonBlock(NULL,cfd);
        anetEnableTcpNoDelay(NULL,cfd);

        /* Use non-blocking I/O for cluster messages. */
        redisLog(REDIS_VERBOSE,"Accepted cluster node %s:%d", cip, cport);
        /* Create a link object we use to handle the connection.
         * It gets passed to the readable handler when data is available.
         * Initiallly the link->node pointer is set to NULL as we don't know
         * which node is, but the right node is references once we know the
         * node identity. */
        link = createClusterLink(NULL);
        link->fd = cfd;
        aeCreateFileEvent(server.el,cfd,AE_READABLE,clusterReadHandler,link);
    }
}

/* -----------------------------------------------------------------------------
 * Key space handling
 * -------------------------------------------------------------------------- */

/* We have 16384 hash slots. The hash slot of a given key is obtained
 * as the least significant 14 bits of the crc16 of the key.
 *
 * However if the key contains the {...} pattern, only the part between
 * { and } is hashed. This may be useful in the future to force certain
 * keys to be in the same node (assuming no resharding is in progress). */
    // ���������Ӧ�ñ����䵽�Ǹ���   ���key�а���{},��ֻ��{}�е��ַ�����hash������abccxx{DDDD}��
    //��ֻ���DDDD��HASH,����ʹ��{}��Ƿֲ���ͬһ����λ��key���Ϳ���ʹ��mget mset del���key��
unsigned int keyHashSlot(char *key, int keylen) {
    int s, e; /* start-end indexes of { and } */

    for (s = 0; s < keylen; s++)
        if (key[s] == '{') break;

    /* No '{' ? Hash the whole key. This is the base case. */
    if (s == keylen) return crc16(key,keylen) & 0x3FFF;

    /* '{' found? Check if we have the corresponding '}'. */
    for (e = s+1; e < keylen; e++)
        if (key[e] == '}') break;

    /* No '}' or nothing betweeen {} ? Hash the whole key. */
    if (e == keylen || e == s+1) return crc16(key,keylen) & 0x3FFF;

    /* If we are here there is both a { and a } on its right. Hash
     * what is in the middle between { and }. */
    return crc16(key+s+1,e-s-1) & 0x3FFF;
}

/* -----------------------------------------------------------------------------
 * CLUSTER node API
 * -------------------------------------------------------------------------- */

/* Create a new cluster node, with the specified flags.
 *
 * ����һ������ָ�� flag �ļ�Ⱥ�ڵ㡣 
 * 
 * If "nodename" is NULL this is considered a first handshake and a random 
 * node name is assigned to this node (it will be fixed later when we'll 
 * receive the first pong). * 
 * ��� nodename ����Ϊ NULL ����ô��ʾ������δ��ڵ㷢�� PING ��
 * ��Ⱥ��Ϊ�ڵ�����һ���������� * ���������֮����յ��ڵ�� PONG �ظ�֮��ͻᱻ���¡� 
 * 
 * The node is created and returned to the user, but it is not automatically * added to the nodes hash table. 
 * 
 * �����᷵�ر������Ľڵ㣬�������Զ�������ӵ���ǰ�ڵ�Ľڵ��ϣ���� * ��nodes hash table���� */

 
//A����cluster meet ��B��ʱ��A�ڵ����洴��B�ڵ��clusterNode��clusterStartHandshake��Ȼ����B�ڵ㷢��
//���Ӳ�����MEET��Ϣ��B�ڵ���յ�MEET��Ϣ����clusterProcessPacket�д���A�ڵ��clusterNode
//node��������������MEET��һ�˴����ڵ㣬���߱������ն˷��ֱ���û�и�sender��Ϣ�򴴽�����createClusterNode  

/*
ע��createClusterLink��createClusterNode��link��node�Ĺ�ϵ����:

A cluster meet B��ʱ����clusterCommand�ᴴ��B��node��B-node->link=null(״̬ΪREDIS_NODE_HANDSHAKE)��Ȼ����clusterCrone�з���node->linkΪNULL
Ҳ���ǻ�û�к�B-node��������ͬʱ����B-link�����Ƿ���B-node���ӣ�����B-node�����B-link��������B-node->link=B-link��
���A����Bһֱ���Ӳ��ϣ���ʱ���B�������������½���B-node,����Ҫ����ִ��A cluster meet B����

B�յ�A����������clusterAcceptHandler������������A���ӵ�link����link->node=null,Ȼ��ͨ����link����A���͹�����MEET��
B��clusterProcessPacket���յ�MMET�����ֱ���û��A�ڵ�node������clusterProcessPacket����A�ڵ㣬Ȼ����clusterCron�з���A-node->link=NULL
�������������A�����Ӳ�����link�����ӳɹ�����A-node->link=link��

A meet B,A���ػᴴ��A��name��B�յ���Ҳ�ᴴ��һ��A-node�����������ǲ�һ���ģ�ͨ���໥PING PONGͨ��������һ�£���clusterRenameNode

��ˣ�ֻ�������������ӵ�һ����node->link=link,link->node=node,�����������ӵ�һ�˴�����link����link->nodeʼ��ΪNULL
*/
clusterNode *createClusterNode(char *nodename, int flags) { //createClusterNode����node  ��node��ӵ���ȺclusterAddNode���Ӽ�Ⱥ�Ƴ�clusterDelNode
    //ע��createClusterLink��createClusterNode
    clusterNode *node = zmalloc(sizeof(*node));

     // ��������
    if (nodename)
        memcpy(node->name, nodename, REDIS_CLUSTER_NAMELEN);
    else
        getRandomHexChars(node->name, REDIS_CLUSTER_NAMELEN);

     // ��ʼ������
    node->ctime = mstime();
    node->configEpoch = 0;
    node->flags = flags;
    memset(node->slots,0,sizeof(node->slots));
    node->numslots = 0;
    node->numslaves = 0;
    node->slaves = NULL;
    node->slaveof = NULL;
    node->ping_sent = node->pong_received = 0;
    node->fail_time = 0;
    node->link = NULL;
    memset(node->ip,0,sizeof(node->ip));
    node->port = 0;
    node->fail_reports = listCreate();
    node->voted_time = 0;
    node->repl_offset_time = 0;
    node->repl_offset = 0;
    listSetFreeMethod(node->fail_reports,zfree);

    return node;
}

/* This function is called every time we get a failure report from a node.
 *
 * ����������ڵ�ǰ�ڵ�ӵ�ĳ���ڵ�����߱���ʱ���á� * 
 * The side effect is to populate the fail_reports list (or to update * the timestamp of an existing report). * 
 * ���������þ��ǽ����߽ڵ�����߱�����ӵ� fail_reports �б� 
 * ���������߽ڵ�����߱����Ѿ����ڣ� 
 * ��ô���¸ñ����ʱ����� * 
 * 'failing' is the node that is in failure state according to the * 'sender' node. * 
 * failing ����ָ�����߽ڵ㣬�� sender ������ָ�򱨸� failing �����ߵĽڵ㡣 *
 * The function returns 0 if it just updates a timestamp of an existing 
 * failure report from the same sender. 1 is returned if a new failure 
 * report is created.  *
 * �������� 0 ��ʾ���Ѵ��ڵı�������˸��£� 
 * ���� 1 ���ʾ������һ���µ����߱��档
 */
int clusterNodeAddFailureReport(clusterNode *failing, clusterNode *sender) {
//sender�ڵ���߱��ڵ㣬failing�ڵ������ˣ����ڵ���Ҫ��¼����Ϣ��faling->fail_reports
    // ָ�򱣴����߱��������
    list *l = failing->fail_reports;

    listNode *ln;
    listIter li;
    clusterNodeFailReport *fr;

    /* If a failure report from the same sender already exists, just update
     * the timestamp. */
    // ���� sender �ڵ�����߱����Ƿ��Ѿ�����
    listRewind(l,&li);
    while ((ln = listNext(&li)) != NULL) {
        fr = ln->value;
        // ������ڵĻ�����ôֻ���¸ñ����ʱ���
        if (fr->node == sender) {
            fr->time = mstime();
            return 0;
        }
    }

    /* Otherwise create a new report. */
   // ����Ļ����ʹ���һ���µı���
    fr = zmalloc(sizeof(*fr));
    fr->node = sender;
    fr->time = mstime();

    // ��������ӵ��б�
    listAddNodeTail(l,fr);

    return 1;
}

/* Remove failure reports that are too old, where too old means reasonably
 * older than the global node timeout. Note that anyway for a node to be
 * flagged as FAIL we need to have a local PFAIL state that is at least
 * older than the global node timeout, so we don't just trust the number
 * of failure reports from other nodes. 
 *
 * �Ƴ��� node �ڵ�Ĺ��ڵ����߱��棬 * �೤ʱ��Ϊ�����Ǹ��� node timeout ѡ���ֵ�������ġ� 
 * 
 * ע�⣬ 
 * Ҫ��һ���ڵ���Ϊ FAIL ״̬�� 
 * ��ǰ�ڵ㽫 node ���Ϊ PFAIL ״̬��ʱ������Ӧ�ó��� node timeout �� 
 * ���Ա��� node �����ߵĽڵ����������ǵ�ǰ�ڵ㽫 node ���Ϊ FAIL ��Ψһ������
 */
void clusterNodeCleanupFailureReports(clusterNode *node) {

    // ָ��ýڵ���������߱���
    list *l = node->fail_reports;

    listNode *ln;
    listIter li;
    clusterNodeFailReport *fr;

    // ���߱����������ڣ��������ʱ��ı���ᱻɾ����
    mstime_t maxtime = server.cluster_node_timeout *
                     REDIS_CLUSTER_FAIL_REPORT_VALIDITY_MULT;
    mstime_t now = mstime();

    // �����������߱���
    listRewind(l,&li);
    while ((ln = listNext(&li)) != NULL) {
        fr = ln->value;
        // ɾ�����ڱ���
        if (now - fr->time > maxtime) listDelNode(l,ln);
    }
}

/* Remove the failing report for 'node' if it was previously considered
 * failing by 'sender'. This function is called when a node informs us via
 * gossip that a node is OK from its point of view (no FAIL or PFAIL flags).
 *
 * �� node �ڵ�����߱������Ƴ� sender �� node �����߱��档 
 *
 * ����������������ʹ�ã���ǰ�ڵ���Ϊ node �����ߣ�FAIL ���� PFAIL���� 
 * �� sender ȴ��ǰ�ڵ㷢�����棬˵����Ϊ node �ڵ�û�����ߣ� 
 * ��ô��ǰ�ڵ��Ҫ�Ƴ� sender �� node �����߱���  
 * ���� ��� sender ��������� node ���ߵĻ��� 
 * 
 * Note that this function is called relatively often as it gets called even 
 * when there are no nodes failing, and is O(N), however when the cluster is 
 * fine the failure reports list is empty so the function runs in constant 
 * time. 
 * 
 * ��ʹ�ڽڵ�û�����ߵ�����£��������Ҳ�ᱻ���ã����ҵ��õĴ������Ƚ�Ƶ����
 * ��һ������£���������ĸ��Ӷ�Ϊ O(N) �� 
 * �����ڲ��������߱��������£���������ĸ��ӶȽ�Ϊ����ʱ�䡣
 * 
 * The function returns 1 if the failure report was found and removed. 
 * Otherwise 0 is returned.  
 * 
 * �������� 1 ��ʾ���߱����Ѿ����ɹ��Ƴ��� 
 * 0 ��ʾ sender û�з��͹� node �����߱��棬ɾ��ʧ�ܡ�
 */
int clusterNodeDelFailureReport(clusterNode *node, clusterNode *sender) {
    list *l = node->fail_reports;
    listNode *ln;
    listIter li;
    clusterNodeFailReport *fr;

    /* Search for a failure report from this sender. */
    // ���� sender �� node �����߱���
    listRewind(l,&li);
    while ((ln = listNext(&li)) != NULL) {
        fr = ln->value;
        if (fr->node == sender) break;
    }
    // sender û�б���� node ���ߣ�ֱ�ӷ���
    if (!ln) return 0; /* No failure report from this sender. */

    /* Remove the failure report. */
    // ɾ�� sender �� node �����߱���
    listDelNode(l,ln);
    // ɾ���� node �����߱����У����ڵı���
    clusterNodeCleanupFailureReports(node);

    return 1;
}

/* Return the number of external nodes that believe 'node' is failing,
 * not including this node, that may have a PFAIL or FAIL state for this
 * node as well. 
 *
 * ���㲻�������ڵ����ڵģ� 
 * �� node ���Ϊ PFAIL ���� FAIL �Ľڵ��������
 */ //��������master�ڵ㱨���node�ڵ�pfail����fail��master��
int clusterNodeFailureReportsCount(clusterNode *node) {

     // �Ƴ����ڵ����߱���
    clusterNodeCleanupFailureReports(node);

    // ͳ�����߱��������
    return listLength(node->fail_reports);
}

// �Ƴ����ڵ� master �Ĵӽڵ� slave�������ӽڵ㻹���ڸ����ڵ��slaves[]����
int clusterNodeRemoveSlave(clusterNode *master, clusterNode *slave) {
    int j;

    // �� slaves �������ҵ��ӽڵ� slave ���������ڵ㣬    // �����ڵ��е� slave ��Ϣ�Ƴ�
    for (j = 0; j < master->numslaves; j++) {
        if (master->slaves[j] == slave) {
            memmove(master->slaves+j,master->slaves+(j+1),
                (master->numslaves-1)-j);
            master->numslaves--;
            return REDIS_OK;
        }
    }
    return REDIS_ERR;
}

// �� slave ���뵽 master �Ĵӽڵ�������
int clusterNodeAddSlave(clusterNode *master, clusterNode *slave) {
    int j;

    /* If it's already a slave, don't add it again. */
     // ��� slave �Ѿ����ڣ���ô��������
    for (j = 0; j < master->numslaves; j++)
        if (master->slaves[j] == slave) return REDIS_ERR;

   // �� slave ��ӵ� slaves ��������
    master->slaves = zrealloc(master->slaves,
        sizeof(clusterNode*)*(master->numslaves+1));
    master->slaves[master->numslaves] = slave;
    master->numslaves++;

    return REDIS_OK;
}

// ���ø����ڵ�Ĵӽڵ�����
void clusterNodeResetSlaves(clusterNode *n) {
    zfree(n->slaves);
    n->numslaves = 0;
    n->slaves = NULL;
}

int clusterCountNonFailingSlaves(clusterNode *n) {
    int j, okslaves = 0;

    for (j = 0; j < n->numslaves; j++)
        if (!nodeFailed(n->slaves[j])) okslaves++;
    return okslaves;
}

//�ڵ㴴����clusterAddNode  �ڵ��ͷ���freeClusterNode
// �ͷŽڵ�
void freeClusterNode(clusterNode *n) {
    sds nodename;

    nodename = sdsnewlen(n->name, REDIS_CLUSTER_NAMELEN);

   // �� nodes ����ɾ���ڵ�
    redisAssert(dictDelete(server.cluster->nodes,nodename) == DICT_OK);
    sdsfree(nodename);

    // �Ƴ��ӽڵ�
    if (n->slaveof) clusterNodeRemoveSlave(n->slaveof, n);

     // �ͷ�����
    if (n->link) freeClusterLink(n->link);
    
      // �ͷ�ʧ�ܱ���
    listRelease(n->fail_reports);

     // �ͷŽڵ�ṹ
    zfree(n);
}

/* Add a node to the nodes hash table */
// ������ node ��ӵ��ڵ������   
int clusterAddNode(clusterNode *node) { //createClusterNode����node  ��node��ӵ���ȺclusterAddNode���Ӽ�Ⱥ�Ƴ�clusterDelNode
    int retval;
     // �� node ��ӵ���ǰ�ڵ�� nodes ����    // ������������ǰ�ڵ�ͻᴴ������ node �Ľڵ�
    retval = dictAdd(server.cluster->nodes,
            sdsnewlen(node->name,REDIS_CLUSTER_NAMELEN), node);
    return (retval == DICT_OK) ? REDIS_OK : REDIS_ERR;
}

/* Remove a node from the cluster:
 *
 * �Ӽ�Ⱥ���Ƴ�һ���ڵ㣺 
 *
 * 1) Mark all the nodes handled by it as unassigned. 
 *    �������ɸýڵ㸺��Ĳ�ȫ������Ϊδ���� 
 * 2) Remove all the failure reports sent by this node. 
 *    �Ƴ�����������ڵ㷢�͵����߱��棨failure report�� 
 * 3) Free the node, that will in turn remove it from the hash table 
 *    and from the list of slaves of its master, if it is a slave node. 
 *    �ͷ�����ڵ㣬 *    ������ڸ����ڵ�� nodes ���е����ݣ� 
 *    �������һ���ӽڵ�Ļ��� 
 *    ��Ҫ���������ڵ�� slaves ���������������ڵ�����ݡ�
 */
void clusterDelNode(clusterNode *delnode) { //createClusterNode����node  ��node��ӵ���ȺclusterAddNode���Ӽ�Ⱥ�Ƴ�clusterDelNode
    int j;
    dictIterator *di;
    dictEntry *de;

    /* 1) Mark slots as unassigned. */
    for (j = 0; j < REDIS_CLUSTER_SLOTS; j++) {
        // ȡ����ýڵ���ղ۵ļƻ�
        if (server.cluster->importing_slots_from[j] == delnode)
            server.cluster->importing_slots_from[j] = NULL;
        // ȡ����ýڵ��ƽ��۵ļƻ�
        if (server.cluster->migrating_slots_to[j] == delnode)
            server.cluster->migrating_slots_to[j] = NULL;
       // �������ɸýڵ㸺��Ĳ�����Ϊδ����
        if (server.cluster->slots[j] == delnode)
            clusterDelSlot(j);
    }

    /* 2) Remove failure reports. */
    // �Ƴ������ɸýڵ㷢�͵����߱���
    di = dictGetSafeIterator(server.cluster->nodes);
    while((de = dictNext(di)) != NULL) {
        clusterNode *node = dictGetVal(de);

        if (node == delnode) continue;
        clusterNodeDelFailureReport(node,delnode);
    }
    dictReleaseIterator(di);

    /* 3) Remove this node from its master's slaves if needed. */
     // ���ڵ���������ڵ�Ĵӽڵ��б����Ƴ�
    if (nodeIsSlave(delnode) && delnode->slaveof)
        clusterNodeRemoveSlave(delnode->slaveof,delnode);

    /* 4) Free the node, unlinking it from the cluster. */
     // �ͷŽڵ�
    freeClusterNode(delnode);
}

/* Node lookup by name */
// �������֣����Ҹ����Ľڵ�
clusterNode *clusterLookupNode(char *name) {
    sds s = sdsnewlen(name, REDIS_CLUSTER_NAMELEN);
    dictEntry *de;

    de = dictFind(server.cluster->nodes,s);
    sdsfree(s);
    if (de == NULL) return NULL;
    return dictGetVal(de);
}

/* This is only used after the handshake. When we connect a given IP/PORT
 * as a result of CLUSTER MEET we don't have the node name yet, so we
 * pick a random one, and will fix it when we receive the PONG request using
 * this function. */
// �ڵ�һ����ڵ㷢�� CLUSTER MEET �����ʱ��
// ��Ϊ��������Ľڵ㻹��֪��Ŀ��ڵ������
// ���������Ŀ��ڵ����һ�����������
// ��Ŀ��ڵ����ͽڵ㷵�� PONG �ظ�ʱ
// ���ͽڵ��֪����Ŀ��ڵ�� IP �� port
// ��ʱ���ͽڵ�Ϳ���ͨ�������������
// ΪĿ��ڵ����
void clusterRenameNode(clusterNode *node, char *newname) {
    int retval;
    sds s = sdsnewlen(node->name, REDIS_CLUSTER_NAMELEN);

    redisLog(REDIS_DEBUG,"Renaming node %.40s into %.40s",
        node->name, newname);
    retval = dictDelete(server.cluster->nodes, s);
    sdsfree(s);
    redisAssert(retval == DICT_OK);
    memcpy(node->name, newname, REDIS_CLUSTER_NAMELEN);
    clusterAddNode(node);
}

/* -----------------------------------------------------------------------------
 * CLUSTER nodes blacklist
 *
 * ��Ⱥ�ڵ������

 *
 * The nodes blacklist is just a way to ensure that a given node with a given
 * Node ID is not readded before some time elapsed (this time is specified
 * in seconds in REDIS_CLUSTER_BLACKLIST_TTL).
 *
 * ���������ڽ�ֹһ�������Ľڵ��� REDIS_CLUSTER_BLACKLIST_TTL ָ����ʱ���ڣ� 
  ��������ӵ���Ⱥ�С�

 *
 * This is useful when we want to remove a node from the cluster completely:
 * when CLUSTER FORGET is called, it also puts the node into the blacklist so
 * that even if we receive gossip messages from other nodes that still remember
 * about the node we want to remove, we don't re-add it before some time.
 *
 * ��������Ҫ�Ӽ�Ⱥ�г����Ƴ�һ���ڵ�ʱ������Ҫ�õ��������� 
 * ��ִ�� CLUSTER FORGET ����ʱ���ڵ�ᱻ��ӽ����������棬 
 * ������ʹ���Ǵ���Ȼ�ǵñ��Ƴ��ڵ�������ڵ������յ����ڱ��Ƴ��ڵ����Ϣ��
 * ����Ҳ�������½����Ƴ��ڵ��������Ⱥ��
 *
 * Currently the REDIS_CLUSTER_BLACKLIST_TTL is set to 1 minute, this means
 * that redis-trib has 60 seconds to send CLUSTER FORGET messages to nodes
 * in the cluster without dealing with the problem of other nodes re-adding
 * back the node to nodes we already sent the FORGET command to.
 *
 * REDIS_CLUSTER_BLACKLIST_TTL ��ǰ��ֵΪ 1 ���ӣ� 
 * ����ζ�� redis-trib �� 60 ���ʱ�䣬������Ⱥ�е����нڵ㷢�� CLUSTER FORGET 
 * ��������ص����������ڵ�Ὣ�� CLUSTER FORGET �Ƴ��Ľڵ�������ӵ���Ⱥ���档
 *
 * The data structure used is a hash table with an sds string representing
 * the node ID as key, and the time when it is ok to re-add the node as
 * value.
 *
 * �������ĵײ�ʵ����һ���ֵ䣬 
 * �ֵ�ļ�Ϊ SDS ��ʾ�Ľڵ� id ���ֵ��ֵΪ����������ӽڵ��ʱ�����
 * -------------------------------------------------------------------------- */

#define REDIS_CLUSTER_BLACKLIST_TTL 60      /* 1 minute. */


/* Before of the addNode() or Exists() operations we always remove expired
 * entries from the black list. This is an O(N) operation but it is not a
 * problem since add / exists operations are called very infrequently and
 * the hash table is supposed to contain very little elements at max.
 *
 * ��ִ�� addNode() �������� Exists() ����֮ǰ��
 * �������ǻ���ִ������������Ƴ��������еĹ��ڽڵ㡣
 *
 * ��������ĸ��Ӷ�Ϊ O(N) �������������Ч�ʲ���Ӱ�죬 
 * ��Ϊ�������ִ�еĴ�������Ƶ���������ֵ��������������Ľڵ�����Ҳ�ǳ��١�
 *
 * However without the cleanup during long uptimes and with some automated
 * node add/removal procedures, entries could accumulate. 
 *
 * ����������ڽڵ���Ϊ�˷�ֹ�ֵ��еĽڵ�ѻ����ࡣ
 */
void clusterBlacklistCleanup(void) {
    dictIterator *di;
    dictEntry *de;

    // �����������е����нڵ�
    di = dictGetSafeIterator(server.cluster->nodes_black_list);
    while((de = dictNext(di)) != NULL) {
        int64_t expire = dictGetUnsignedIntegerVal(de);

        // ɾ�����ڽڵ�
        if (expire < server.unixtime)
            dictDelete(server.cluster->nodes_black_list,dictGetKey(de));
    }
    dictReleaseIterator(di);
}

/* Cleanup the blacklist and add a new node ID to the black list. */
// ����������еĹ��ڽڵ㣬Ȼ���µĽڵ���ӵ���������
void clusterBlacklistAddNode(clusterNode *node) {
    dictEntry *de;
    sds id = sdsnewlen(node->name,REDIS_CLUSTER_NAMELEN);

   // �������������
    clusterBlacklistCleanup();

    // ��ӽڵ�
    if (dictAdd(server.cluster->nodes_black_list,id,NULL) == DICT_OK) {
        /* If the key was added, duplicate the sds string representation of
         * the key for the next lookup. We'll free it at the end. */
        id = sdsdup(id);
    }
     // ���ù���ʱ��
    de = dictFind(server.cluster->nodes_black_list,id);
    dictSetUnsignedIntegerVal(de,time(NULL)+REDIS_CLUSTER_BLACKLIST_TTL);
    sdsfree(id);
}

/* Return non-zero if the specified node ID exists in the blacklist.
 * You don't need to pass an sds string here, any pointer to 40 bytes
 * will work. */
// ������ id ��ָ���Ľڵ��Ƿ�����ں������С�
// nodeid ����������һ�� SDS ֵ��ֻҪһ�� 40 �ֽڳ����ַ�������
int clusterBlacklistExists(char *nodeid) {

    // ���� SDS ��ʾ�Ľڵ���
    sds id = sdsnewlen(nodeid,REDIS_CLUSTER_NAMELEN);
    int retval;

     // ������ں�����
    clusterBlacklistCleanup();

     // ���ڵ��Ƿ����
    retval = dictFind(server.cluster->nodes_black_list,id) != NULL;
    sdsfree(id);

    return retval;
}

/* -----------------------------------------------------------------------------
 * CLUSTER messages exchange - PING/PONG and gossip
 * -------------------------------------------------------------------------- */

/* This function checks if a given node should be marked as FAIL.
 * It happens if the following conditions are met:
 *
 * �˺��������ж��Ƿ���Ҫ�� node ���Ϊ FAIL �� * 
 * �� node ���Ϊ FAIL ��Ҫ������������������
 *
 * 1) We received enough failure reports from other master nodes via gossip.
 *    Enough means that the majority of the masters signaled the node is
 *    down recently.
 *    �а������ϵ����ڵ㽫 node ���Ϊ PFAIL ״̬�� 
 * 2) We believe this node is in PFAIL state. 
 *    ��ǰ�ڵ�Ҳ�� node ���Ϊ PFAIL ״̬��
 *
 * If a failure is detected we also inform the whole cluster about this
 * event trying to force every other node to set the FAIL flag for the node.
 *
 * ���ȷ�� node �Ѿ������� FAIL ״̬�� 
 * ��ô�ڵ㻹���������ڵ㷢�� FAIL ��Ϣ���������ڵ�Ҳ�� node ���Ϊ FAIL ��

 *
 * Note that the form of agreement used here is weak, as we collect the majority
 * of masters state during some time, and even if we force agreement by
 * propagating the FAIL message, because of partitions we may not reach every
 * node. However:
 *
 * ע�⣬��Ⱥ�ж�һ�� node ���� FAIL ���������������weak���ģ� 
 * ��Ϊ�ڵ��Ƕ� node ��״̬���沢����ʵʱ�ģ�������һ��ʱ����
 * �����ʱ���� node ��״̬�����Ѿ������˸ı䣩��
 * ���Ҿ��ܵ�ǰ�ڵ���������ڵ㷢�� FAIL ��Ϣ��
 * ����Ϊ������ѣ�network partition�������⣬ 
 * ��һ���ֽڵ���ܻ��ǻ᲻֪���� node ���Ϊ FAIL �� 
 * 
 * ������ 
 * 
 * 1) Either we reach the majority and eventually the FAIL state will propagate
 *    to all the cluster.
 *    ֻҪ���ǳɹ��� node ���Ϊ FAIL �� 
 *    ��ô��� FAIL ״̬���գ�eventually���ܻᴫ����������Ⱥ�����нڵ㡣 
 * 2) Or there is no majority so no slave promotion will be authorized and the
 *    FAIL flag will be cleared after some time.
 *    �ֻ��ߣ���Ϊû�а����Ľڵ�֧�֣���ǰ�ڵ㲻�ܽ� node ���Ϊ FAIL ��
 *    ���Զ� FAIL �ڵ�Ĺ���ת�ƽ��޷����У� FAIL ��ʶ���ܻ���֮���Ƴ���
 *    
 */ //���ڵ�ʹӽڵ㶼����ִ�иú���markNodeAsFailingIfNeeded���жϸýڵ��Ƿ��pfail->fail
void markNodeAsFailingIfNeeded(clusterNode *node) { //node�������ڵ���Ϊ��node�ڵ�pfail����fail�Ľڵ㣬���ǲ���nodeΪfail����pfail�ڵ�
    int failures;

   // ���Ϊ FAIL ����Ľڵ���������Ҫ������Ⱥ�ڵ�������һ��
    int needed_quorum = (server.cluster->size / 2) + 1; //��Ҫ�����λ�����ڵ�����һ��+1, �������¼�������ڵ㲢�Ҵ����λ���Ľڵ��һ��+1

    //�����ڵ㷢�͹�����MEETЯ����node��pfail״̬�����Ҳ���fail״̬�ĲŻ���к����Ĵ���
    if (!nodeTimedOut(node)) return; /* We can reach it. */ //ֻ��node�ڵ���pfail״̬�ĲŽ��к��洦������pfail״̬��ֱ���˳�
    if (nodeFailed(node)) return; /* Already FAILing. */ //˵���ýڵ��Ѿ�ȷ�Ͻ���fail�ˣ�return

     // ͳ�ƽ� node ���Ϊ PFAIL ���� FAIL �Ľڵ���������������ǰ�ڵ㣩
    failures = clusterNodeFailureReportsCount(node);

    /* Also count myself as a voter if I'm a master. */
    // �����ǰ�ڵ������ڵ㣬��ô����ǰ�ڵ�Ҳ���� failures ֮��
    if (nodeIsMaster(myself)) failures++;
    // �������߽ڵ����������ڵ�������һ�룬���ܽ��ڵ��ж�Ϊ FAIL ������
    if (failures < needed_quorum) return; /* No weak agreement from masters. */

    redisLog(REDIS_NOTICE,
        "Marking node %.40s as failing (quorum reached).", node->name);

    /* Mark the node as failing. */
     // �� node ���Ϊ FAIL
    node->flags &= ~REDIS_NODE_PFAIL;
    node->flags |= REDIS_NODE_FAIL;
    node->fail_time = mstime();

    /* Broadcast the failing node name to everybody, forcing all the other
     * reachable nodes to flag the node as FAIL. */
    // �����ǰ�ڵ������ڵ�Ļ�����ô�������ڵ㷢�ͱ��� node �� FAIL ��Ϣ    // �������ڵ�Ҳ�� node ���Ϊ FAIL
    if (nodeIsMaster(myself)) clusterSendFail(node->name); 
    clusterDoBeforeSleep(CLUSTER_TODO_UPDATE_STATE|CLUSTER_TODO_SAVE_CONFIG); //clusterBeforeSleep����������״̬ clusterUpdateState�и���״̬
}

/* This function is called only if a node is marked as FAIL, but we are able
 * to reach it again. It checks if there are the conditions to undo the FAIL
 * state. 
 *
 * ��������ڵ�ǰ�ڵ���յ�һ�������Ϊ FAIL �Ľڵ������յ���Ϣʱʹ�ã�
 * �����Լ���Ƿ�Ӧ�ý��ڵ�� FAIL ״̬�Ƴ���

 */
void clearNodeFailureIfNeeded(clusterNode *node) {
    mstime_t now = mstime();

    redisAssert(nodeFailed(node));

    /* For slaves we always clear the FAIL flag if we can contact the
     * node again. */
    // ��� FAIL ���Ǵӽڵ㣬��ô��ǰ�ڵ��ֱ���Ƴ��ýڵ�� FAIL
    if (nodeIsSlave(node) || node->numslots == 0) {
        redisLog(REDIS_NOTICE,
            "Clear FAIL state for node %.40s: %s is reachable again.",
                node->name,
                nodeIsSlave(node) ? "slave" : "master without slots");
        // �Ƴ�
        node->flags &= ~REDIS_NODE_FAIL;

        clusterDoBeforeSleep(CLUSTER_TODO_UPDATE_STATE|CLUSTER_TODO_SAVE_CONFIG);
    }

    /* If it is a master and...
     *
     * ��� FAIL ����һ�����ڵ㣬���ң�     
     *     
     * 1) The FAIL state is old enough.    
     *    �ڵ㱻���Ϊ FAIL ״̬�Ѿ���һ��ʱ����    
     *    
     * 2) It is yet serving slots from our point of view (not failed over).    
     *    �ӵ�ǰ�ڵ���ӽ�����������ڵ㻹�и�����Ĳ�     
     *     
     * Apparently no one is going to fix these slots, clear the FAIL flag.      
     *     
     * ��ô˵�� FAIL �ڵ���Ȼ�в�û��Ǩ���꣬��ô��ǰ�ڵ��Ƴ��ýڵ�� FAIL ��ʶ��
     */
    if (nodeIsMaster(node) && node->numslots > 0 &&
        (now - node->fail_time) >
        (server.cluster_node_timeout * REDIS_CLUSTER_FAIL_UNDO_TIME_MULT))
    {
        redisLog(REDIS_NOTICE,
            "Clear FAIL state for node %.40s: is reachable again and nobody is serving its slots after some time.",
                node->name);

        // ���� FAIL ״̬
        node->flags &= ~REDIS_NODE_FAIL;

        clusterDoBeforeSleep(CLUSTER_TODO_UPDATE_STATE|CLUSTER_TODO_SAVE_CONFIG);
    }
}

/* Return true if we already have a node in HANDSHAKE state matching the
 * specified ip address and port number. This function is used in order to
 * avoid adding a new handshake node for the same address multiple times. 
 *
 * �����ǰ�ڵ��Ѿ��� ip �� port ��ָ���Ľڵ���������֣�
 * ��ô���� 1 ��
 * 
 * ����������ڷ�ֹ��ͬһ���ڵ���ж�����֡�
 */
int clusterHandshakeInProgress(char *ip, int port) {
    dictIterator *di;
    dictEntry *de;

    // ����������֪�ڵ�
    di = dictGetSafeIterator(server.cluster->nodes);
    while((de = dictNext(di)) != NULL) {
        clusterNode *node = dictGetVal(de);

         // ����������״̬�Ľڵ㣬֮��ʣ�µĶ����������ֵĽڵ�
        if (!nodeInHandshake(node)) continue;

        // ���� ip �� port �Ľڵ����ڽ�������
        if (!strcasecmp(node->ip,ip) && node->port == port) break;
    }
    dictReleaseIterator(di);

     // ���ڵ��Ƿ���������
    return de != NULL;
}

/* Start an handshake with the specified address if there is not one
 * already in progress. Returns non-zero if the handshake was actually
 * started. On error zero is returned and errno is set to one of the
 * following values:
 *
 * �����û����ָ���ĵ�ַ���й����֣���ô�������֡� 
 * ���� 1 ��ʾ�����Ѿ���ʼ�� 
 * ���� 0 ���� errno ����Ϊ����ֵ����ʾ��������� 
 * 
 * EAGAIN - There is already an handshake in progress for this address. 
 *          �Ѿ��������ڽ������ˡ� 
 * EINVAL - IP or port are not valid.  
 *          ip ���� port �������Ϸ���
 */ 
//A����cluster meet ��B��ʱ��A�ڵ����洴��B�ڵ��clusterNode��clusterStartHandshake��Ȼ����B�ڵ㷢��
//���Ӳ�����MEET��Ϣ��B�ڵ���յ�MEET��Ϣ����clusterProcessPacket�д���A�ڵ��clusterNode



 //Aͨ��cluster meet bip bport  B��B����clusterAcceptHandler->clusterReadHandler�������ӣ�A��ͨ��
 //clusterCommand->clusterStartHandshake����clusterCron->anetTcpNonBlockBindConnect���ӷ�����
int clusterStartHandshake(char *ip, int port) {//ע��ú���û�д���connect������connect��clusterCron->anetTcpNonBlockBindConnect
    clusterNode *n;
    char norm_ip[REDIS_IP_STR_LEN];
    struct sockaddr_storage sa;

    /* IP sanity check */
     // ip �Ϸ��Լ��
    if (inet_pton(AF_INET,ip,
            &(((struct sockaddr_in *)&sa)->sin_addr)))
    {
        sa.ss_family = AF_INET;
    } else if (inet_pton(AF_INET6,ip,
            &(((struct sockaddr_in6 *)&sa)->sin6_addr)))
    {
        sa.ss_family = AF_INET6;
    } else {
        errno = EINVAL;
        return 0;
    }

    /* Port sanity check */
    // port �Ϸ��Լ��
    if (port <= 0 || port > (65535-REDIS_CLUSTER_PORT_INCR)) {
        errno = EINVAL;
        return 0;
    }

    /* Set norm_ip as the normalized string representation of the node
     * IP address. */
    if (sa.ss_family == AF_INET)
        inet_ntop(AF_INET,
            (void*)&(((struct sockaddr_in *)&sa)->sin_addr),
            norm_ip,REDIS_IP_STR_LEN);
    else
        inet_ntop(AF_INET6,
            (void*)&(((struct sockaddr_in6 *)&sa)->sin6_addr),
            norm_ip,REDIS_IP_STR_LEN);

    // ���ڵ��Ƿ��Ѿ�����������������ǵĻ�����ôֱ�ӷ��أ���ֹ�����ظ�����
    if (clusterHandshakeInProgress(norm_ip,port)) {
        errno = EAGAIN;
        return 0;
    }

    /* Add the node with a random address (NULL as first argument to
     * createClusterNode()). Everything will be fixed during the
     * handskake. */
    // �Ը�����ַ�Ľڵ�����һ���������  
    // �� HANDSHAKE ���ʱ����ǰ�ڵ��ȡ�ø�����ַ�ڵ���������� 
    // ��ʱ���������滻�����

    //��A�Ŀͻ�����cluster meet B��A�ϻᴴ��B��clusterNode�����ǻ�û��clusterNode.link��Ϣ����clusterCron���
    // ���ǻ�û�з���meet��Ϣ��B����clusterCron����
    n = createClusterNode(NULL,REDIS_NODE_HANDSHAKE|REDIS_NODE_MEET); //�ڵ���Ϣ��clusterNode�����ڵ�͸ýڵ��������Ϣ��clusterNode.link
    memcpy(n->ip,norm_ip,sizeof(n->ip));
    n->port = port;

    // ���ڵ���ӵ���Ⱥ����
    clusterAddNode(n);

    return 1;
}

/* Process the gossip section of PING or PONG packets.
 *
 * ���� MEET �� PING �� PONG ��Ϣ�к� gossip Э���йص���Ϣ��
 *
 * Note that this function assumes that the packet is already sanity-checked
 * by the caller, not in the content of the gossip section, but in the
 * length. 
 *
 * ע�⣬�����������������Ѿ�������Ϣ�ĳ��ȣ�����Ϣ���й��Ϸ��Լ�顣

 */

/*
���յ������ڵ㷢�͹�����ping��Ϣ��Я����172.16.3.40:7002��172.16.3.66:7000�ڵ�
21478:M 07 Nov 18:03:10.123 . --- Processing packet of type 0, 2416 bytes
21478:M 07 Nov 18:03:10.123 . Ping packet received: (nil)
21478:M 07 Nov 18:03:10.123 . ping packet received: (nil)
21478:M 07 Nov 18:03:10.123 . GOSSIP 9a214ea3bc2e6069c409c01cb06244cd1310104c 172.16.3.40:7002 master
21478:M 07 Nov 18:03:10.123 . GOSSIP eb8939a845c486d52ad0017b199ae1ae806a8442 172.16.3.66:7000 master

���ڵ㷢��ping��Ϣ���ڵ�9a214ea3bc2e6069c409c01cb06244cd1310104c��Ȼ���յ��Է���pong
21478:M 07 Nov 18:03:10.125 . Pinging node 9a214ea3bc2e6069c409c01cb06244cd1310104c
21478:M 07 Nov 18:03:10.126 . --- Processing packet of type 1, 2416 bytes
21478:M 07 Nov 18:03:10.126 . pong packet received: 0x7fcb6217dc00
21478:M 07 Nov 18:03:10.126 . GOSSIP eb8939a845c486d52ad0017b199ae1ae806a8442 172.16.3.66:7000 master
21478:M 07 Nov 18:03:10.126 . GOSSIP 79f9474661ca51f7cb8920715dc6fb3db12ff032 172.16.3.41:7004 slave
*/ //���� MEET �� PING �� PONG ��Ϣ�к� gossip Э���йص���Ϣ��
void clusterProcessGossipSection(clusterMsg *hdr, clusterLink *link) {

     // ��¼������Ϣ�а����˶��ٸ��ڵ����Ϣ
    uint16_t count = ntohs(hdr->count);

    // ָ���һ���ڵ����Ϣ
    clusterMsgDataGossip *g = (clusterMsgDataGossip*) hdr->data.ping.gossip;

    // ȡ��������
    clusterNode *sender = link->node ? link->node : clusterLookupNode(hdr->sender);

    // �������нڵ����Ϣ
    while(count--) {
        sds ci = sdsempty();

        // �����ڵ�� flag
        uint16_t flags = ntohs(g->flags);

        // ��Ϣ�ڵ�
        clusterNode *node;

        // ȡ���ڵ�� flag
        if (flags == 0) ci = sdscat(ci,"noflags,");
        if (flags & REDIS_NODE_MYSELF) ci = sdscat(ci,"myself,");
        if (flags & REDIS_NODE_MASTER) ci = sdscat(ci,"master,");
        if (flags & REDIS_NODE_SLAVE) ci = sdscat(ci,"slave,");
        if (flags & REDIS_NODE_PFAIL) ci = sdscat(ci,"fail?,");
        if (flags & REDIS_NODE_FAIL) ci = sdscat(ci,"fail,");
        if (flags & REDIS_NODE_HANDSHAKE) ci = sdscat(ci,"handshake,");
        if (flags & REDIS_NODE_NOADDR) ci = sdscat(ci,"noaddr,");
        if (ci[sdslen(ci)-1] == ',') ci[sdslen(ci)-1] = ' ';

        redisLog(REDIS_DEBUG,"GOSSIP %.40s %s:%d %s",
            g->nodename,
            g->ip,
            ntohs(g->port),
            ci);
        sdsfree(ci);

        /* Update our state accordingly to the gossip sections */
        // ʹ����Ϣ�е���Ϣ�Խڵ���и���
        node = clusterLookupNode(g->nodename);
        // �ڵ��Ѿ������ڵ�ǰ�ڵ�
        if (node) {
            /* We already know this node.
               Handle failure reports, only when the sender is a master. */
             // ��� sender ��һ�����ڵ㣬��ô������Ҫ�������߱���
            if (sender && nodeIsMaster(sender) && node != myself) {
                // �ڵ㴦�� FAIL ���� PFAIL ״̬
                if (flags & (REDIS_NODE_FAIL|REDIS_NODE_PFAIL)) {//���Ͷ�ÿ��1s��Ӽ�Ⱥ��ѡһ���ڵ�������PING���ο�CLUSTERMSG_TYPE_PING

                    // ��� sender �� node �����߱���
                    if (clusterNodeAddFailureReport(node,sender)) { 
                    //clusterProcessGossipSection->clusterNodeAddFailureReport�ѽ��յ�fail����pfail��ӵ�����fail_reports
                        redisLog(REDIS_VERBOSE,
                            "Node %.40s reported node %.40s as not reachable.",
                            sender->name, node->name); //sender�ڵ���߱��ڵ�node�ڵ��쳣��
                    }

                    // ���Խ� node ���Ϊ FAIL
                    markNodeAsFailingIfNeeded(node);

                 // �ڵ㴦������״̬
                } else {

                     // ��� sender �������͹��� node �����߱���      
                     // ��ô����ñ���
                    if (clusterNodeDelFailureReport(node,sender)) {
                        redisLog(REDIS_VERBOSE,
                            "Node %.40s reported node %.40s is back online.",
                            sender->name, node->name);
                    }
                }
            }

            /* If we already know this node, but it is not reachable, and
             * we see a different address in the gossip section, start an
             * handshake with the (possibly) new address: this will result
             * into a node address update if the handshake will be
             * successful. */
            // ����ڵ�֮ǰ���� PFAIL ���� FAIL ״̬         
            // ���Ҹýڵ�� IP ���߶˿ں��Ѿ������仯       
            // ��ô�����ǽڵ㻻���µ�ַ�����Զ�����������
            if (node->flags & (REDIS_NODE_FAIL|REDIS_NODE_PFAIL) &&
                (strcasecmp(node->ip,g->ip) || node->port != ntohs(g->port)))
            {
                clusterStartHandshake(g->ip,ntohs(g->port));
            }

         // ��ǰ�ڵ㲻��ʶ node
        } else {
            /* If it's not in NOADDR state and we don't have it, we
             * start a handshake process against this IP/PORT pairs.
             *
             * ��� node ���� NOADDR ״̬�����ҵ�ǰ�ڵ㲻��ʶ node            
             * ��ô�� node ���� HANDSHAKE ��Ϣ��
             *
             * Note that we require that the sender of this gossip message
             * is a well known node in our cluster, otherwise we risk
             * joining another cluster.
             *
              * ע�⣬��ǰ�ڵ���뱣֤ sender �Ǳ���Ⱥ�Ľڵ㣬 
              * �������ǽ��м�������һ����Ⱥ�ķ��ա�
             */
            if (sender &&
                !(flags & REDIS_NODE_NOADDR) &&
                !clusterBlacklistExists(g->nodename)) 
            //������ڵ�ͨ��cluster forget��ĳ���ڵ�ɾ�����ڵ㼯Ⱥ�Ļ�����ô�����ɾ�Ľڵ���Ҫ�Ⱥ��������ں󱾽ڵ���ܷ���handshark
            {
                clusterStartHandshake(g->ip,ntohs(g->port)); //�������ؾͻᴴ����������ڵ�node�ڵ��ˣ�����Ҳ������sender�����У�����û�еĽڵ���
            }
        }

        /* Next node */
        // �����¸��ڵ����Ϣ
        g++;
    }
}

/* IP -> string conversion. 'buf' is supposed to at least be 46 bytes. */
// �� ip ת��Ϊ�ַ���

void nodeIp2String(char *buf, clusterLink *link) {
    anetPeerToString(link->fd, buf, REDIS_IP_STR_LEN, NULL);
}

/* Update the node address to the IP address that can be extracted
 * from link->fd, and at the specified port.
 *
 * ���½ڵ�ĵ�ַ�� IP �Ͷ˿ڿ��Դ� link->fd ��á�

 *
 * Also disconnect the node link so that we'll connect again to the new
 * address.
 *
 * ���ҶϿ���ǰ�Ľڵ����ӣ��������µ�ַ���������ӡ� 
 * 
 * If the ip/port pair are already correct no operation is performed at 
 * all. 
 *
 * ��� ip �Ͷ˿ں����ڵ�������ͬ����ô��ִ���κζ�����
 * 
 * The function returns 0 if the node address is still the same, 
 * otherwise 1 is returned.  
 * 
 * �������� 0 ��ʾ��ַ���䣬��ַ�ѱ������򷵻� 1 ��
 *
 * The function returns 0 if the node address is still the same,
 * otherwise 1 is returned. 
 *
 * �������� 0 ��ʾ��ַ���䣬��ַ�ѱ������򷵻� 1 ��
 */
int nodeUpdateAddressIfNeeded(clusterNode *node, clusterLink *link, int port) {
    char ip[REDIS_IP_STR_LEN];

    /* We don't proceed if the link is the same as the sender link, as this
     * function is designed to see if the node link is consistent with the
     * symmetric link that is used to receive PINGs from the node.
     *
     * As a side effect this function never frees the passed 'link', so
     * it is safe to call during packet processing. */
    // ���Ӳ��䣬ֱ�ӷ���
    if (link == node->link) return 0;

    // ��ȡ�ַ�����ʽ�� ip ��ַ
    nodeIp2String(ip,link);
   // ��ȡ�˿ں�
    if (node->port == port && strcmp(ip,node->ip) == 0) return 0;

    /* IP / port is different, update it. */
    memcpy(node->ip,ip,sizeof(ip));
    node->port = port;

     // �ͷž����ӣ������ӻ���֮���Զ�������
    if (node->link) freeClusterLink(node->link);

    redisLog(REDIS_WARNING,"Address updated for node %.40s, now %s:%d",
        node->name, node->ip, node->port);

    /* Check if this is our master and we have to change the
     * replication target as well. */
    // ����������Ե�ǰ�ڵ㣨�ӽڵ㣩�����ڵ㣬��ô�����µ�ַ���ø��ƶ���
    if (nodeIsSlave(myself) && myself->slaveof == node)
        replicationSetMaster(node->ip, node->port);
    return 1;
}

/* Reconfigure the specified node 'n' as a master. This function is called when
 * a node that we believed to be a slave is now acting as master in order to
 * update the state of the node. 
 *
 * ���ڵ� n ����Ϊ���ڵ㡣

 */
void clusterSetNodeAsMaster(clusterNode *n) {

    // �Ѿ������ڵ��ˡ�   
    if (nodeIsMaster(n)) 
        return;    
    // �Ƴ� slaveof    
    if (n->slaveof) 
        clusterNodeRemoveSlave(n->slaveof,n);    
    // �ر� SLAVE ��ʶ   
    n->flags &= ~REDIS_NODE_SLAVE;    
    // �� MASTER ��ʶ    
    n->flags |= REDIS_NODE_MASTER;    
        // ���� slaveof ����
    n->slaveof = NULL;

    /* Update config and state. */
    clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG|
                         CLUSTER_TODO_UPDATE_STATE);
}

/* This function is called when we receive a master configuration via a
 * PING, PONG or UPDATE packet. What we receive is a node, a configEpoch of the
 * node, and the set of slots claimed under this configEpoch.
 *
 * ��������ڽڵ�ͨ�� PING �� PONG �� UPDATE ��Ϣ���յ�һ�� master ������ʱ���ã�
 * ������һ���ڵ㣬�ڵ�� configEpoch �� 
 * �Լ��ڵ��� configEpoch ��Ԫ�µĲ�������Ϊ������
 *
 * What we do is to rebind the slots with newer configuration compared to our
 * local configuration, and if needed, we turn ourself into a replica of the
 * node (see the function comments for more info).
 *
 * �������Ҫ���ľ����� slots �����������úͱ��ڵ�ĵ�ǰ���ý��жԱȣ� 
 * �����±��ڵ�Բ۵Ĳ��֣� 
 * �������Ҫ�Ļ����������Ὣ���ڵ�ת��Ϊ sender �Ĵӽڵ㣬
 * ������Ϣ��ο������е�ע�͡�
 *
 * The 'sender' is the node for which we received a configuration update.
 * Sometimes it is not actaully the "Sender" of the information, like in the case
 * we receive the info via an UPDATE packet. 
 *
 * ��������� sender ������������Ϣ�ķ����ߣ�Ҳ��������Ϣ�����ߵ����ڵ㡣
 */ //���յ������ڵ��PING PONG UPDATE��Ϣ��ͨ���Ա����ü�Ԫ���Բ�λ���ֽ��и���
void clusterUpdateSlotsConfigWith(clusterNode *sender, uint64_t senderConfigEpoch, unsigned char *slots) {
    int j;
    clusterNode *curmaster, *newmaster = NULL;
    /* The dirty slots list is a list of slots for which we lose the ownership
     * while having still keys inside. This usually happens after a failover
     * or after a manual cluster reconfiguration operated by the admin.
     *
     * If the update message is not able to demote a master to slave (in this
     * case we'll resync with the master updating the whole key space), we
     * need to delete all the keys in the slots we lost ownership. */
    uint16_t dirty_slots[REDIS_CLUSTER_SLOTS];
    int dirty_slots_count = 0;

    /* Here we set curmaster to this node or the node this node
     * replicates to if it's a slave. In the for loop we are
     * interested to check if slots are taken away from curmaster. */
    // 1�������ǰ�ڵ������ڵ㣬��ô�� curmaster ����Ϊ��ǰ�ڵ�   
    // 2�������ǰ�ڵ��Ǵӽڵ㣬��ô�� curmaster ����Ϊ��ǰ�ڵ����ڸ��Ƶ����ڵ�   
    // �Ժ��� for ѭ�������ǽ�ʹ�� curmaster ����뵱ǰ�ڵ��йصĲ��Ƿ����˱䶯
    curmaster = nodeIsMaster(myself) ? myself : myself->slaveof;

    if (sender == myself) {
        redisLog(REDIS_WARNING,"Discarding UPDATE message about myself.");
        return;
    }

    // ���²۲���
    for (j = 0; j < REDIS_CLUSTER_SLOTS; j++) {

         // ��� slots �еĲ� j �Ѿ���ָ�ɣ���ôִ�����´���
        if (bitmapTestBit(slots,j)) {
            /* The slot is already bound to the sender of this message. */
            if (server.cluster->slots[j] == sender) continue;

            /* The slot is in importing state, it should be modified only
             * manually via redis-trib (example: a resharding is in progress
             * and the migrating side slot was already closed and is advertising
             * a new config. We still want the slot to be closed manually). */
            if (server.cluster->importing_slots_from[j]) continue;

            /* We rebind the slot to the new node claiming it if:
             * 1) The slot was unassigned or the new node claims it with a
             *    greater configEpoch.
             * 2) We are not currently importing the slot. */
            if (server.cluster->slots[j] == NULL ||
                server.cluster->slots[j]->configEpoch < senderConfigEpoch) 
            //sender���͹�����slots��Ϣ�ͱ��ڵ㱾����Ϊ��slot��Ϣ�г�ͻ����Ҫ����configEPOch���жϣ����CLUSTER SETSLOT <SLOT> NODE <NODE ID>��clustercommand�����Ķ�
            {
                /* Was this slot mine, and still contains keys? Mark it as
                 * a dirty slot. */
                if (server.cluster->slots[j] == myself &&
                    countKeysInSlot(j) &&
                    sender != myself) //sender��Ϊj��λ����sender�ڵ㣬���Ǳ��ؼ�⵽j��λ�����ڱ��ڵ㣬��ͻ��!!!!
                {
                    dirty_slots[dirty_slots_count] = j; //��λ��ͻ��¼
                    dirty_slots_count++;
                }

                 // ����� j ��ԭ�ڵ��ǵ�ǰ�ڵ�����ڵ㣿              
                 // ����ǵĻ���˵������ת�Ʒ����ˣ�����ǰ�ڵ�ĸ��ƶ�������Ϊ�µ����ڵ�

                 //���һ����master������2��savle�����master���ˣ�ͨ��ѡ��slave1��ѡΪ�µ�������slave2ͨ�������������������ӵ���������slave1��ͨ����λ�仯����֪����clusterUpdateSlotsConfigWith
                if (server.cluster->slots[j] == curmaster) 
                    newmaster = sender;

                // ���� j ��Ϊδָ��
                clusterDelSlot(j);

                 // ���� j ָ�ɸ� sender
                clusterAddSlot(sender,j); //��j��λָ�ɸ�sender

                clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG|
                                     CLUSTER_TODO_UPDATE_STATE|
                                     CLUSTER_TODO_FSYNC_CONFIG);
            }
        }
    }

    /* If at least one slot was reassigned from a node to another node
     * with a greater configEpoch, it is possible that:
     *
     * �����ǰ�ڵ㣨���ߵ�ǰ�ڵ�����ڵ㣩������һ���۱�ָ�ɵ��� sender    
     * ���� sender �� configEpoch �ȵ�ǰ�ڵ�ļ�ԪҪ��    
     * ��ô���ܷ����ˣ�    
     *    
     * 1) We are a master left without slots. This means that we were   
     *    failed over and we should turn into a replica of the new     
     *    master.    
     *    ��ǰ�ڵ���һ�����ٴ����κβ۵����ڵ㣬   
     *    ��ʱӦ�ý���ǰ�ڵ�����Ϊ�����ڵ�Ĵӽڵ㡣   
     * 2) We are a slave and our master is left without slots. We need    
     *    to replicate to the new slots owner.    
     *    ��ǰ�ڵ���һ���ӽڵ㣬   
     *    ���ҵ�ǰ�ڵ�����ڵ��Ѿ����ٴ����κβۣ�   
     *    ��ʱӦ�ý���ǰ�ڵ�����Ϊ�����ڵ�Ĵӽڵ㡣
     */
    if (newmaster && curmaster->numslots == 0) {
        redisLog(REDIS_WARNING,
            "Configuration change detected. Reconfiguring myself "
            "as a replica of %.40s", sender->name);
         // �� sender ����Ϊ��ǰ�ڵ�����ڵ�
        clusterSetMaster(sender);

        clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG|
                             CLUSTER_TODO_UPDATE_STATE|
                             CLUSTER_TODO_FSYNC_CONFIG);
    } else if (dirty_slots_count) {
        /* If we are here, we received an update message which removed
         * ownership for certain slots we still have keys about, but still
         * we are serving some slots, so this master node was not demoted to
         * a slave.
         *
         * In order to maintain a consistent state between keys and slots
         * we need to remove all the keys from the slots we lost. */
        for (j = 0; j < dirty_slots_count; j++)
            delKeysInSlot(dirty_slots[j]);
    }
}

/* This function is called when this node is a master, and we receive from
 * another master a configuration epoch that is equal to our configuration
 * epoch.
 *
 * BACKGROUND
 *
 * It is not possible that different slaves get the same config
 * epoch during a failover election, because the slaves need to get voted
 * by a majority. However when we perform a manual resharding of the cluster
 * the node will assign a configuration epoch to itself without to ask
 * for agreement. Usually resharding happens when the cluster is working well
 * and is supervised by the sysadmin, however it is possible for a failover
 * to happen exactly while the node we are resharding a slot to assigns itself
 * a new configuration epoch, but before it is able to propagate it.
 *
 * So technically it is possible in this condition that two nodes end with
 * the same configuration epoch.
 *
 * Another possibility is that there are bugs in the implementation causing
 * this to happen.
 *
 * Moreover when a new cluster is created, all the nodes start with the same
 * configEpoch. This collision resolution code allows nodes to automatically
 * end with a different configEpoch at startup automatically.
 *
 * In all the cases, we want a mechanism that resolves this issue automatically
 * as a safeguard. The same configuration epoch for masters serving different
 * set of slots is not harmful, but it is if the nodes end serving the same
 * slots for some reason (manual errors or software bugs) without a proper
 * failover procedure.
 *
 * In general we want a system that eventually always ends with different
 * masters having different configuration epochs whatever happened, since
 * nothign is worse than a split-brain condition in a distributed system.
 *
 * BEHAVIOR
 *
 * When this function gets called, what happens is that if this node
 * has the lexicographically smaller Node ID compared to the other node
 * with the conflicting epoch (the 'sender' node), it will assign itself
 * the greatest configuration epoch currently detected among nodes plus 1.
 *
 * This means that even if there are multiple nodes colliding, the node
 * with the greatest Node ID never moves forward, so eventually all the nodes
 * end with a different configuration epoch.
 */ //�Է����sender�ڵ��configEpoch�ͱ��ڵ��configEpoch��ͬ�������Լ���configEpoch��1��ͬʱ����������Ⱥ�İ汾��currentEpoch
void clusterHandleConfigEpochCollision(clusterNode *sender) {
    /* Prerequisites: nodes have the same configEpoch and are both masters. */
    if (sender->configEpoch != myself->configEpoch ||
        !nodeIsMaster(sender) || !nodeIsMaster(myself)) return;
    /* Don't act if the colliding node has a smaller Node ID. */
    if (memcmp(sender->name,myself->name,REDIS_CLUSTER_NAMELEN) <= 0) return;
    /* Get the next ID available at the best of this node knowledge. */
    server.cluster->currentEpoch++;

    //�Է����sender�ڵ��configEpoch�ͱ��ڵ��configEpoch��ͬ�������Լ���configEpoch��1��ͬʱ����������Ⱥ�İ汾��currentEpoch
    myself->configEpoch = server.cluster->currentEpoch; //ͨ��������Ա�֤ÿ���ڵ��configEpoch��ͬ
    clusterSaveConfigOrDie(1);
    redisLog(REDIS_VERBOSE,
        "WARNING: configEpoch collision with node %.40s."
        " Updating my configEpoch to %llu",
        sender->name,
        (unsigned long long) myself->configEpoch);
}

/* When this function is called, there is a packet to process starting
 * at node->rcvbuf. Releasing the buffer is up to the caller, so this
 * function should just handle the higher level stuff of processing the
 * packet, modifying the cluster state if needed.
 * * ���������������ʱ��˵�� node->rcvbuf ����һ�����������Ϣ��
 * ��Ϣ�������֮����ͷŹ����ɵ����ߴ��������������ֻ�踺������Ϣ�Ϳ����ˡ�

 *
 * The function returns 1 if the link is still valid after the packet
 * was processed, otherwise 0 if the link was freed since the packet
 * processing lead to some inconsistency error (for instance a PONG
 * received from the wrong sender ID). 
 *
 * ����������� 1 ����ô˵��������Ϣʱû���������⣬������Ȼ���á�
 * ����������� 0 ����ô˵����Ϣ����ʱ�����˲�һ������
 * ��������յ��� PONG �Ƿ����Բ���ȷ�ķ����� ID �ģ��������Ѿ����ͷš�
 */ 

 //clusterReadHandler->clusterProcessPacket       ���clusterMsg��clusterBuildMessageHdr�������clusterProcessPacket
int clusterProcessPacket(clusterLink *link) { //cluster�ڵ�֮������Ҫ��������������ΪclusterProcessPacket��clusterCron
   // ָ����Ϣͷ
    clusterMsg *hdr = (clusterMsg*) link->rcvbuf;

   // ��Ϣ�ĳ���
    uint32_t totlen = ntohl(hdr->totlen);

    // ��Ϣ������
    uint16_t type = ntohs(hdr->type);

    // ��Ϣ�����ߵı�ʶ
    uint16_t flags = ntohs(hdr->flags);

    uint64_t senderCurrentEpoch = 0, senderConfigEpoch = 0;

    clusterNode *sender; //ͨ������������������node������name��hdr->senderƥ��Ľڵ�

    // ���½�����Ϣ������
    server.cluster->stats_bus_messages_received++;

    redisLog(REDIS_DEBUG,"--- Processing packet of type %d, %lu bytes",
        type, (unsigned long) totlen);

    /* Perform sanity checks */
     // �Ϸ��Լ��
    if (totlen < 16) return 1; /* At least signature, version, totlen, count. */
    if (ntohs(hdr->ver) != 0) return 1; /* Can't handle versions other than 0.*/
    if (totlen > sdslen(link->rcvbuf)) return 1;
    if (type == CLUSTERMSG_TYPE_PING || type == CLUSTERMSG_TYPE_PONG ||
        type == CLUSTERMSG_TYPE_MEET)
    {
        uint16_t count = ntohs(hdr->count);
        uint32_t explen; /* expected length of this packet */

        explen = sizeof(clusterMsg)-sizeof(union clusterMsgData); 
        explen += (sizeof(clusterMsgDataGossip)*count); //ping pong meet��Ϣ��
        if (totlen != explen) return 1;
    } else if (type == CLUSTERMSG_TYPE_FAIL) {
        uint32_t explen = sizeof(clusterMsg)-sizeof(union clusterMsgData);

        explen += sizeof(clusterMsgDataFail);
        if (totlen != explen) return 1;
    } else if (type == CLUSTERMSG_TYPE_PUBLISH) {
        uint32_t explen = sizeof(clusterMsg)-sizeof(union clusterMsgData);

        explen += sizeof(clusterMsgDataPublish) +
                ntohl(hdr->data.publish.msg.channel_len) +
                ntohl(hdr->data.publish.msg.message_len);
        if (totlen != explen) return 1;
    } else if (type == CLUSTERMSG_TYPE_FAILOVER_AUTH_REQUEST ||
               type == CLUSTERMSG_TYPE_FAILOVER_AUTH_ACK ||
               type == CLUSTERMSG_TYPE_MFSTART)
    {
        uint32_t explen = sizeof(clusterMsg)-sizeof(union clusterMsgData);

        if (totlen != explen) return 1;
    } else if (type == CLUSTERMSG_TYPE_UPDATE) {
        uint32_t explen = sizeof(clusterMsg)-sizeof(union clusterMsgData);

        explen += sizeof(clusterMsgDataUpdate);
        if (totlen != explen) return 1;
    }

    /* Check if the sender is a known node. */
    // ���ҷ����߽ڵ�  ȷ�����ĸ��ڵ㷢�͵ı��ĵ����ڵ�
    sender = clusterLookupNode(hdr->sender);
    // �ڵ���ڣ����Ҳ��� HANDSHAKE �ڵ�    // ��ô���½ڵ�����ü�Ԫ��Ϣ
    if (sender && !nodeInHandshake(sender)) {
        /* Update our curretEpoch if we see a newer epoch in the cluster. */
        senderCurrentEpoch = ntohu64(hdr->currentEpoch);
        senderConfigEpoch = ntohu64(hdr->configEpoch);
        if (senderCurrentEpoch > server.cluster->currentEpoch)
            server.cluster->currentEpoch = senderCurrentEpoch;
        /* Update the sender configEpoch if it is publishing a newer one. */
        if (senderConfigEpoch > sender->configEpoch) {
            //����Ҫ���·����߽ڵ��configEpoch
            sender->configEpoch = senderConfigEpoch; //���·�����ڵ��configEpoch�ڱ���server.cluster->nodes��ָ��ڵ�(Ҳ���Ƿ��ͽڵ��)configEpoll
            clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG|
                                 CLUSTER_TODO_FSYNC_CONFIG);
        }
        /* Update the replication offset info for this node. */
        sender->repl_offset = ntohu64(hdr->offset);
        sender->repl_offset_time = mstime();
        /* If we are a slave performing a manual failover and our master
         * sent its offset while already paused, populate the MF state. */
        if (server.cluster->mf_end &&
            nodeIsSlave(myself) &&
            myself->slaveof == sender &&
            hdr->mflags[0] & CLUSTERMSG_FLAG0_PAUSED &&
            server.cluster->mf_master_offset == 0) 
        //�����յ��ӵ�CLUSTERMSG_TYPE_MFSTART���ĺ�������failover״̬��Ҳ����mf_endʱ�����0��Ȼ����ͨ������PING���ĵ���slave��Я������offset
        {
            //��ȷ������offset����¼��������clusterHandleManualFailover���ж��ֶ�cluster failover�ڼ䣬���Ƿ��ȡ����ȫ����������
            server.cluster->mf_master_offset = sender->repl_offset; 
            
            redisLog(REDIS_WARNING,
                "Received replication offset for paused "
                "master manual failover: %lld",
                server.cluster->mf_master_offset);
        }
    }

    /* Process packets by type. */
    // ������Ϣ�����ͣ�����ڵ�    
    // ����һ�� PING ��Ϣ���� MEET ��Ϣ
    if (type == CLUSTERMSG_TYPE_PING || type == CLUSTERMSG_TYPE_MEET) {
        redisLog(REDIS_DEBUG,"Ping packet received: %p", (void*)link->node);

        /* Add this node if it is new for us and the msg type is MEET.
         *
        * �����ǰ�ڵ��ǵ�һ����������ڵ㣬���ҶԷ��������� MEET ��Ϣ��     
        * ��ô������ڵ���ӵ���Ⱥ�Ľڵ��б����档
         *
         * In this stage we don't try to add the node with the right
         * flags, slaveof pointer, and so forth, as this details will be
         * resolved when we'll receive PONGs from the node. 
         *
         * �ڵ�Ŀǰ�� flag �� slaveof �����Ե�ֵ����δ���õģ�    
         * �ȵ�ǰ�ڵ���Է����� PING ����֮��      
         * ��Щ��Ϣ���ԴӶԷ��ظ��� PONG ��Ϣ��ȡ�á�
         */
        if (!sender && type == CLUSTERMSG_TYPE_MEET) { //�Զ˷���meet����������û�и�node�ڵ���Ϣ���򴴽�
            clusterNode *node;

            //A����cluster meet ��B��ʱ��A�ڵ����洴��B�ڵ��clusterNode��clusterStartHandshake��Ȼ����B�ڵ㷢��
            //���Ӳ�����MEET��Ϣ��B�ڵ���յ�MEET��Ϣ����clusterProcessPacket�д���A�ڵ��clusterNode

             // ���� HANDSHAKE ״̬���½ڵ�
            node = createClusterNode(NULL,REDIS_NODE_HANDSHAKE);

            // ���� IP �Ͷ˿�
            nodeIp2String(node->ip,link);
            node->port = ntohs(hdr->port);

            //A����meet��Ϣ��B���������ע�������node.linkû�и�ֵ����ΪNULL,ΪNULL��Ϊ����ʲô�أ�����Ϊ����clusterCron��Ҳ��A�������Ӳ�����PING��

    /*  clusterNode��clusterLink ��ϵͼ
    A����B������MEET��A�ᴴ��B��clusterNode-B�����Ҵ���B��link1����clusterNode-B��link1��clusterCron�н�����ϵ
    B�յ�meet����clusterAcceptHandler�д���link2����clusterProcessPacket�д���B��clusterNode-A,������ʱ���link2��clusterNode-Aû�н�����ϵ
    ������B��clusterCron�з���clusterNode-A��linkΪNULL������B��ʼ��A�������ӣ��Ӷ�����link3������PING,����clusterNode2��link3������A�յ�
    B���͵���������󣬴����µ�link4,���ն�Ӧ��ϵ��:
    
    A�ڵ�                   B�ڵ�
    clusterNode-B(link1) --->    link2(��link�������κ�clusterNode)     (A����meet��B)                                               ����1
    link4      <----         clusterNode-A(link3) (��link�������κ�clusterNode)  (B�յ�meet������һ��clustercron����A��������)     ����2
    */ 
            
             // ���½ڵ���ӵ���Ⱥ
            clusterAddNode(node);

            clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG);
        }

        /* Get info from the gossip section */
        // ������ȡ����Ϣ�е� gossip �ڵ���Ϣ
        clusterProcessGossipSection(hdr,link);

        /* Anyway reply with a PONG */
         // ��Ŀ��ڵ㷵��һ�� PONG
        clusterSendPing(link,CLUSTERMSG_TYPE_PONG);
    }

    /* PING or PONG: process config information. */
    // ����һ�� PING �� PONG ���� MEET ��Ϣ
    if (type == CLUSTERMSG_TYPE_PING || type == CLUSTERMSG_TYPE_PONG ||
        type == CLUSTERMSG_TYPE_MEET)
    {
        redisLog(REDIS_DEBUG,"%s packet received: %p",
            type == CLUSTERMSG_TYPE_PING ? "ping" : "pong",
            (void*)link->node);

        // ���ӵ� clusterNode �ṹ����
        if (link->node) { 
        //������������ping��һ�ˣ���link->nodeΪ���ո�ping��node������nodeӦ��pong��ʱ�򣬱��ڵ��յ�PONG����ʱ���link->node���Ƿ���pong�Ľڵ�
        //�ڵ㴦�� HANDSHAKE ״̬
            if (nodeInHandshake(link->node)) {
                /* If we already have this node, try to change the
                 * IP/port of the node with the new one. */
                if (sender) {
                    redisLog(REDIS_VERBOSE,
                        "Handshake: we already know node %.40s, "
                        "updating the address if needed.", sender->name);
                     // �������Ҫ�Ļ������½ڵ�ĵ�ַ
                    if (nodeUpdateAddressIfNeeded(sender,link,ntohs(hdr->port)))
                    {
                        clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG|
                                             CLUSTER_TODO_UPDATE_STATE);
                    }
                    /* Free this node as we alrady have it. This will
                     * cause the link to be freed as well. */
                    // �ͷŽڵ�
                    freeClusterNode(link->node);
                    return 0;
                }

                /* First thing to do is replacing the random name with the
                 * right node name if this was a handshake stage. */
                 //�ýڵ�������滻�� HANDSHAKE ʱ�������������
                clusterRenameNode(link->node, hdr->sender);
                redisLog(REDIS_DEBUG,"Handshake with node %.40s completed.",
                    link->node->name);

                // �ر� HANDSHAKE ״̬
                link->node->flags &= ~REDIS_NODE_HANDSHAKE;

                // ���ýڵ�Ľ�ɫ
                link->node->flags |= flags&(REDIS_NODE_MASTER|REDIS_NODE_SLAVE);

                clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG);

             // �ڵ��Ѵ��ڣ������� id �͵�ǰ�ڵ㱣��� id ��ͬ
            } else if (memcmp(link->node->name,hdr->sender,
                        REDIS_CLUSTER_NAMELEN) != 0) //����ĳ���ڵ���ˣ����������ֶ���nodes.conf�еĸýڵ�name�޸��ˣ������
            {
                /* If the reply has a non matching node ID we
                 * disconnect this node and set it as not having an associated
                 * address. */
                // ��ô������ڵ���Ϊ NOADDR                 
                // ���Ͽ�����   ����һ��clusterCron��ʱ���ж�node->link=null,�����½����͸�node������
                redisLog(REDIS_DEBUG,"PONG contains mismatching sender ID");
                link->node->flags |= REDIS_NODE_NOADDR;
                link->node->ip[0] = '\0';
                link->node->port = 0;

                // �Ͽ�����
                freeClusterLink(link); 

                clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG);
                return 0;
            }
        }

        /* Update the node address if it changed. */
        // ������͵���ϢΪ PING       
        // ���ҷ����߲��� HANDSHAKE ״̬    
        // ��ô���·����ߵ���Ϣ
        if (sender && type == CLUSTERMSG_TYPE_PING &&
            !nodeInHandshake(sender) &&
            nodeUpdateAddressIfNeeded(sender,link,ntohs(hdr->port)))
        {
            clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG|
                                 CLUSTER_TODO_UPDATE_STATE);
        }

        /* Update our info about the node */
         // �������һ�� PONG ��Ϣ����ô�������ǹ��� node �ڵ����ʶ
        if (link->node && type == CLUSTERMSG_TYPE_PONG) {

            // ���һ�νӵ��ýڵ�� PONG ��ʱ��
            link->node->pong_received = mstime();

             // �������һ�εȴ� PING �����ʱ��
            link->node->ping_sent = 0;

            /* The PFAIL condition can be reversed without external
             * help if it is momentary (that is, if it does not
             * turn into a FAIL state).
             *
             * �ӵ��ڵ�� PONG �ظ������ǿ����Ƴ��ڵ�� PFAIL ״̬��
             *
             * The FAIL condition is also reversible under specific
             * conditions detected by clearNodeFailureIfNeeded(). 
             *
             * ����ڵ��״̬Ϊ FAIL ��           
             * ��ô�Ƿ�����״̬Ҫ���� clearNodeFailureIfNeeded() ������������
             */
            if (nodeTimedOut(link->node)) {
                 // ���� PFAIL
                link->node->flags &= ~REDIS_NODE_PFAIL;

                clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG|
                                     CLUSTER_TODO_UPDATE_STATE);
            } else if (nodeFailed(link->node)) { 
            //���ڵ���������߽ڵ��������ˣ�������м�Ⱥ�ڵ㶼�����ˣ��ñ��ڵ㼯Ⱥ�ֿ��Լ���ʹ���ˡ������ڵ�Ҳ��ͨ���������жϼ�Ⱥ�ڵ��ǲ���ȫ������
                 // ���Ƿ���Գ��� FAIL
                clearNodeFailureIfNeeded(link->node);
            }
        }

        //�����л����
        
        /* Check for role switch: slave -> master or master -> slave. */
        // ���ڵ�������Ϣ��������Ҫʱ���и���
        if (sender) {

            // ������Ϣ�Ľڵ�� slaveof Ϊ REDIS_NODE_NULL_NAME   
            // ��ô sender ����һ�����ڵ�
            if (!memcmp(hdr->slaveof,REDIS_NODE_NULL_NAME,
                sizeof(hdr->slaveof)))
            {
                /* Node is a master. */
                 // ���� sender Ϊ���ڵ�
                clusterSetNodeAsMaster(sender);

            // sender �� slaveof ��Ϊ�գ���ô����һ���ӽڵ�
            } else {

                /* Node is a slave. */
                 // ȡ�� sender �����ڵ�
                clusterNode *master = clusterLookupNode(hdr->slaveof);

                // sender �����ڵ����˴ӽڵ㣬�������� sender
                if (nodeIsMaster(sender)) {
                    /* Master turned into a slave! Reconfigure the node. */

                   // ɾ�������ɸýڵ㸺��Ĳ�
                    clusterDelNodeSlots(sender);

                    // ���±�ʶ
                    sender->flags &= ~REDIS_NODE_MASTER;
                    sender->flags |= REDIS_NODE_SLAVE;

                    /* Remove the list of slaves from the node. */
                    // �Ƴ� sender �Ĵӽڵ�����
                    if (sender->numslaves) clusterNodeResetSlaves(sender);

                    /* Update config and state. */
                    clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG|
                                         CLUSTER_TODO_UPDATE_STATE);
                }

                /* Master node changed for this slave? */

                 // ��� sender �����ڵ��Ƿ���
                if (master && sender->slaveof != master) {
                    // ��� sender ֮ǰ�����ڵ㲻�����ڵ����ڵ�                
                    // ��ô�ھ����ڵ�Ĵӽڵ��б����Ƴ� sender
                    if (sender->slaveof)
                        clusterNodeRemoveSlave(sender->slaveof,sender);

                   // ���������ڵ�Ĵӽڵ��б������ sender
                    clusterNodeAddSlave(master,sender);

                     // ���� sender �����ڵ�
                    sender->slaveof = master;

                    /* Update config. */
                    clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG);
                }
            }
        }

        /* Update our info about served slots.
         *
         * ���µ�ǰ�ڵ�� sender ������۵���ʶ��
         *
         * Note: this MUST happen after we update the master/slave state
         * so that REDIS_NODE_MASTER flag will be set. 
         *
         * �ⲿ�ֵĸ��� *����* �ڸ��� sender ����/�ӽڵ���Ϣ֮��     
         * ��Ϊ������Ҫ�õ� REDIS_NODE_MASTER ��ʶ��
         */

        /* Many checks are only needed if the set of served slots this
         * instance claims is different compared to the set of slots we have
         * for it. Check this ASAP to avoid other computational expansive
         * checks later. */
        clusterNode *sender_master = NULL; /* Sender or its master if slave. */
        int dirty_slots = 0; /* Sender claimed slots don't match my view? */

        if (sender) {
            sender_master = nodeIsMaster(sender) ? sender : sender->slaveof;
            if (sender_master) {
                dirty_slots = memcmp(sender_master->slots,
                        hdr->myslots,sizeof(hdr->myslots)) != 0;
            }
        }

        /* 1) If the sender of the message is a master, and we detected that
         *    the set of slots it claims changed, scan the slots to see if we
         *    need to update our configuration. */
       // ��� sender �����ڵ㣬���� sender �Ĳ۲��ֳ����˱䶯      
       // ��ô��鵱ǰ�ڵ�� sender �Ĳ۲������ã����Ƿ���Ҫ���и���
        if (sender && nodeIsMaster(sender) && dirty_slots)
            clusterUpdateSlotsConfigWith(sender,senderConfigEpoch,hdr->myslots);

        /* 2) We also check for the reverse condition, that is, the sender
         *    claims to serve slots we know are served by a master with a
         *    greater configEpoch. If this happens we inform the sender.
         *
         *    �������� 1 ���෴������Ҳ���ǣ�     
         *    sender ����Ĳ۵����ü�Ԫ�ȵ�ǰ�ڵ���֪��ĳ���ڵ�����ü�ԪҪ�ͣ�     
         *    ����������Ļ���֪ͨ sender ��
         *
         * This is useful because sometimes after a partition heals, a
         * reappearing master may be the last one to claim a given set of
         * hash slots, but with a configuration that other instances know to
         * be deprecated. Example:
         *
         * ����������ܻ��������������У�   
         * һ���������ߵ����ڵ���ܻ�����Ѿ���ʱ�Ĳ۲��֡�

         *
         * ����˵��       
         *        
         * A and B are master and slave for slots 1,2,3.    
         * A ����� 1 �� 2 �� 3 ���� B �� A �Ĵӽڵ㡣       
         *       
         * A is partitioned away, B gets promoted.        
         * A �������з��ѳ�ȥ��B ������Ϊ���ڵ㡣      
         *     
         * B is partitioned away, and A returns available.     
         * B �������з��ѳ�ȥ�� A �������ߣ���������ʹ�õĲ۲����Ǿɵģ���
         *
         * Usually B would PING A publishing its set of served slots and its
         * configEpoch, but because of the partition B can't inform A of the
         * new configuration, so other nodes that have an updated table must
         * do it. In this way A will stop to act as a master (or can try to
         * failover if there are the conditions to win the election).
         *
         * ����������£� B Ӧ���� A ���� PING ��Ϣ����֪ A ���Լ���B���Ѿ�������    
         * �� 1�� 2�� 3 �����Ҵ��и��������ü�Ԫ������Ϊ������ѵ�Ե�ʣ�       
         * �ڵ� B û�취֪ͨ�ڵ� A ��       
         * ����֪ͨ�ڵ� A �����еĲ۲����Ѿ����µĹ����ͽ�������֪�� B ���и������ü�Ԫ�Ľڵ�������     
         * �� A �ӵ������ڵ���ڽڵ� B ����Ϣʱ��      
         * �ڵ� A �ͻ�ֹͣ�Լ������ڵ㹤�����ֻ������½��й���ת�ơ�

         */
        if (sender && dirty_slots) {
            int j;

            for (j = 0; j < REDIS_CLUSTER_SLOTS; j++) {

                // ��� slots �еĲ� j �Ƿ��Ѿ���ָ��
                if (bitmapTestBit(hdr->myslots,j)) {

                     // ��ǰ�ڵ���Ϊ�� j �� sender ������              
                     // ���ߵ�ǰ�ڵ���Ϊ�ò�δָ�ɣ���ô�����ò�
                    if (server.cluster->slots[j] == sender ||
                        server.cluster->slots[j] == NULL) continue;

                    // ��ǰ�ڵ�� j �����ü�Ԫ�� sender �����ü�ԪҪ��
                    if (server.cluster->slots[j]->configEpoch >
                        senderConfigEpoch)
                    {
                        redisLog(REDIS_VERBOSE,
                            "Node %.40s has old slots configuration, sending "
                            "an UPDATE message about %.40s",
                                sender->name, server.cluster->slots[j]->name);

                       // �� sender ���͹��ڲ� j �ĸ�����Ϣ
                        clusterSendUpdate(sender->link,
                            server.cluster->slots[j]);

                        /* TODO: instead of exiting the loop send every other
                         * UPDATE packet for other nodes that are the new owner
                         * of sender's slots. */
                        break;
                    }
                }
            }
        }

        /* If our config epoch collides with the sender's try to fix
         * the problem. */
        if (sender &&
            nodeIsMaster(myself) && nodeIsMaster(sender) &&
            senderConfigEpoch == myself->configEpoch) 
        //�Է����sender�ڵ��configEpoch�ͱ��ڵ��configEpoch��ͬ�������Լ���configEpoch��1��ͬʱ����������Ⱥ�İ汾��currentEpoch
        {
            clusterHandleConfigEpochCollision(sender);
        }

        /* Get info from the gossip section */
       // ��������ȡ����Ϣ gossip Э�鲿�ֵ���Ϣ
        clusterProcessGossipSection(hdr,link);

     // ����һ�� FAIL ��Ϣ�� sender ��֪��ǰ�ڵ㣬ĳ���ڵ��Ѿ����� FAIL ״̬��
    } else if (type == CLUSTERMSG_TYPE_FAIL) { 
    //һ���Ǽ�Ⱥ�е����ڵ㷢�ּ�Ⱥ��ĳ�����ڵ���ˣ����֪ͨ����
        clusterNode *failing;

        if (sender) {

             // ��ȡ���߽ڵ����Ϣ
            failing = clusterLookupNode(hdr->data.fail.about.nodename);
            // ���ߵĽڵ�Ȳ��ǵ�ǰ�ڵ㣬Ҳû�д��� FAIL ״̬
            if (failing &&
                !(failing->flags & (REDIS_NODE_FAIL|REDIS_NODE_MYSELF))) 
                //������ڵ��Ѿ������Ϊ���߽ڵ�������߽ڵ���ǽڵ��Լ������ӡ����
            {
                redisLog(REDIS_NOTICE,
                    "FAIL message received from %.40s about %.40s",
                    hdr->sender, hdr->data.fail.about.nodename);

                 // �� FAIL ״̬
                failing->flags |= REDIS_NODE_FAIL;
                failing->fail_time = mstime();
               // �ر� PFAIL ״̬
                failing->flags &= ~REDIS_NODE_PFAIL;
                clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG|
                                     CLUSTER_TODO_UPDATE_STATE);
            }
        } else {
            redisLog(REDIS_NOTICE,
                "Ignoring FAIL message from unknonw node %.40s about %.40s",
                hdr->sender, hdr->data.fail.about.nodename);
        }
        
        // ����һ�� PUBLISH ��Ϣ
    } else if (type == CLUSTERMSG_TYPE_PUBLISH) {
        robj *channel, *message;
        uint32_t channel_len, message_len;

        /* Don't bother creating useless objects if there are no
         * Pub/Sub subscribers. */
       // ֻ���ж�����ʱ������Ϣ����
        if (dictSize(server.pubsub_channels) ||
           listLength(server.pubsub_patterns))
        {
            // Ƶ������
            channel_len = ntohl(hdr->data.publish.msg.channel_len);

            // ��Ϣ����
            message_len = ntohl(hdr->data.publish.msg.message_len);

             // Ƶ��
            channel = createStringObject(
                        (char*)hdr->data.publish.msg.bulk_data,channel_len);

           // ��Ϣ
            message = createStringObject(
                        (char*)hdr->data.publish.msg.bulk_data+channel_len,
                        message_len);
             // ������Ϣ
            pubsubPublishMessage(channel,message);

            decrRefCount(channel);
            decrRefCount(message);
        }

    // ����һ�������ù���Ǩ����Ȩ����Ϣ�� sender ����ǰ�ڵ�Ϊ�����й���ת��ͶƱ
    } else if (type == CLUSTERMSG_TYPE_FAILOVER_AUTH_REQUEST) { 
        //slave��⵽�Լ���master���ˣ���clusterRequestFailoverAuth���͸�failover request����sender slave�ڵ�Ҫ���������ͶƱ
        if (!sender) return 1;  /* We don't know that node. */
        // �����������Ļ����� sender ͶƱ��֧�������й���ת��
        clusterSendFailoverAuthIfNeeded(sender,hdr);

     // ����һ������Ǩ��ͶƱ��Ϣ�� sender ֧�ֵ�ǰ�ڵ�ִ�й���ת�Ʋ���
    } else if (type == CLUSTERMSG_TYPE_FAILOVER_AUTH_ACK) {
        if (!sender) return 1;  /* We don't know that node. */

        /* We consider this vote only if the sender is a master serving
         * a non zero number of slots, and its currentEpoch is greater or
         * equal to epoch where this node started the election. */
            // ֻ�����ڴ�������һ���۵����ڵ��ͶƱ�ᱻ��Ϊ����ЧͶƱ        
            // ֻ�з������������� sender ��ͶƱ������Ч��        
            // 1�� sender �����ڵ�      
            // 2�� sender ���ڴ�������һ����       
            // 3�� sender �����ü�Ԫ���ڵ��ڵ�ǰ�ڵ�����ü�Ԫ
        if (nodeIsMaster(sender) && sender->numslots > 0 &&
            senderCurrentEpoch >= server.cluster->failover_auth_epoch)
        {
            // ����֧��Ʊ��
            server.cluster->failover_auth_count++;

            /* Maybe we reached a quorum here, set a flag to make sure
             * we check ASAP. */
            clusterDoBeforeSleep(CLUSTER_TODO_HANDLE_FAILOVER);
        }

    } else if (type == CLUSTERMSG_TYPE_MFSTART) {
        /* �ӽڵ�ͨ��CLUSTERMSG_TYPE_MFSTART����֪ͨ���ڵ㿪ʼ�����ֶ�����ת��׼��������cluster failover */
        /* This message is acceptable only if I'm a master and the sender
         * is one of my slaves. */
        if (!sender || sender->slaveof != myself) return 1;
        /* Manual failover requested from slaves. Initialize the state
         * accordingly. */
        resetManualFailover();

        /* 
        mf_end�����ֵ��Ϊ0��˵���ڽ����ֶ�����ת�ƹ����У�����clusterBuildMessageHdr����ͷ�ǻ�Я����ʶCLUSTERMSG_FLAG0_PAUSED��
        ͬʱclusterRequestFailoverAuth�����CLUSTERMSG_FLAG0_FORCEACK��ʶ��һֱ�ȵ�manualFailoverCheckTimeout��0��ֵ 
        */
        server.cluster->mf_end = mstime() + REDIS_CLUSTER_MF_TIMEOUT;
        server.cluster->mf_slave = sender;
        
        //���յ�cluster failover����ʼ����ǿ�ƹ���ת�ƣ������ڵ���ʱ�����ͻ����������10s�ӽ���ǿ�ƹ���
        //ת��,�������Ա�֤ͨ��offset�ôӷ��������������������Ļ������е���������
        pauseClients(mstime()+(REDIS_CLUSTER_MF_TIMEOUT*2)); 
        redisLog(REDIS_WARNING,"Manual failover requested by slave %.40s.",
            sender->name);
    } else if (type == CLUSTERMSG_TYPE_UPDATE) {
        clusterNode *n; /* The node the update is about. */
        uint64_t reportedConfigEpoch =
                    ntohu64(hdr->data.update.nodecfg.configEpoch);

        if (!sender) return 1;  /* We don't know the sender. */

        // ��ȡ��Ҫ���µĽڵ�
        n = clusterLookupNode(hdr->data.update.nodecfg.nodename);
        if (!n) return 1;   /* We don't know the reported node. */

        // ��Ϣ�ļ�Ԫ�������ڽڵ� n ���������ü�Ԫ 
        // �������
        if (n->configEpoch >= reportedConfigEpoch) return 1; /* Nothing new. */

        /* If in our current config the node is a slave, set it as a master. */
        // ����ڵ� n Ϊ�ӽڵ㣬�����Ĳ����ø�����    
        // ��ô˵������ڵ��Ѿ���Ϊ���ڵ㣬��������Ϊ���ڵ�
        if (nodeIsSlave(n)) clusterSetNodeAsMaster(n);

        /* Update the node's configEpoch. */
        n->configEpoch = reportedConfigEpoch;
        clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG|
                             CLUSTER_TODO_FSYNC_CONFIG);

        /* Check the bitmap of served slots and udpate our
         * config accordingly. */
        // ����Ϣ�ж� n �Ĳ۲����뵱ǰ�ڵ�� n �Ĳ۲��ֽ��жԱ�     
        // ������Ҫʱ���µ�ǰ�ڵ�� n �Ĳ۲��ֵ���ʶ
        clusterUpdateSlotsConfigWith(n,reportedConfigEpoch,
            hdr->data.update.nodecfg.slots);
    } else {
        redisLog(REDIS_WARNING,"Received unknown packet type: %d", type);
    }
    return 1;
}

/* This function is called when we detect the link with this node is lost.

   ��������ڷ��ֽڵ�������Ѿ���ʧʱʹ�á�   
   We set the node as no longer connected. The Cluster Cron will detect   this connection and will try to get it connected again.   
   ���ǽ��ڵ��״̬����Ϊ�Ͽ�״̬��Cluster Cron ����ݸ�״̬�����������ӽڵ㡣   
   Instead if the node is a temporary node used to accept a query, we   completely free the node on error.    
   ���������һ����ʱ���ӵĻ�����ô���ͻᱻ�����ͷţ����ٽ���������
   */
void handleLinkIOError(clusterLink *link) {
    freeClusterLink(link);
}

/* Send data. This is handled using a trivial send buffer that gets
 * consumed by write(). We don't try to optimize this for speed too much
 * as this is a very low traffic channel. 
 *
 * д�¼���������������Ⱥ�ڵ㷢����Ϣ��

 */
void clusterWriteHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    clusterLink *link = (clusterLink*) privdata;
    ssize_t nwritten;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);

     // д����Ϣ
    nwritten = write(fd, link->sndbuf, sdslen(link->sndbuf));

    // д�����
    if (nwritten <= 0) {
        redisLog(REDIS_DEBUG,"I/O error writing to node link: %s",
            strerror(errno));
        handleLinkIOError(link);
        return;
    }

     // ɾ����д��Ĳ���
    sdsrange(link->sndbuf,nwritten,-1);

    // ������е�ǰ�ڵ����������������������ݶ��Ѿ�д�����   
    // ��������Ϊ�գ�    
    // ��ôɾ��д�¼�������
    if (sdslen(link->sndbuf) == 0)
        aeDeleteFileEvent(server.el, link->fd, AE_WRITABLE);
}

//Aͨ��cluster meet bip bport  B��B����clusterAcceptHandler->clusterReadHandler�������ӣ�A��ͨ��
 //clusterCommand->clusterStartHandshake����clusterCron->anetTcpNonBlockBindConnect���ӷ�����

/* Read data. Try to read the first field of the header first to check the
 * full length of the packet. When a whole packet is in memory this function
 * will call the function to process the packet. And so forth. */
    // ���¼�������
    // ���ȶ������ݵ�ͷ�����ж϶������ݵĳ���
    // ���������һ�� whole packet ����ô���ú������������ packet ��

//���ɱ�����ڵ�redis���ڵ�ͨ��readQueryFromClient(����������ʵʱKV�����)����clusterReadHandler(��Ⱥ֮��ͨ�������)�е�read���쳣�¼���⵽�ڵ��쳣
void clusterReadHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    char buf[sizeof(clusterMsg)];
    ssize_t nread;
    clusterMsg *hdr;
    clusterLink *link = (clusterLink*) privdata;
    int readlen, rcvbuflen;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);

    // �����ܵض������
    while(1) { /* Read as long as there is data to read. */

         // ������뻺�����ĳ���
        rcvbuflen = sdslen(link->rcvbuf);
        // ͷ��Ϣ��8 �ֽڣ�δ������
        if (rcvbuflen < 8) {
            /* First, obtain the first 8 bytes to get the full message
             * length. */
            readlen = 8 - rcvbuflen;
         // �Ѷ�����������Ϣ
        } else {
            /* Finally read the full message. */
            hdr = (clusterMsg*) link->rcvbuf;
            if (rcvbuflen == 8) {
                /* Perform some sanity check on the message signature
                 * and length. */
                if (memcmp(hdr->sig,"RCmb",4) != 0 ||
                    ntohl(hdr->totlen) < CLUSTERMSG_MIN_LEN)
                {
                    redisLog(REDIS_WARNING,
                        "Bad message length or signature received "
                        "from Cluster bus.");
                    handleLinkIOError(link);
                    return;
                }
            }
            // ��¼�Ѷ������ݳ���
            readlen = ntohl(hdr->totlen) - rcvbuflen;
            if (readlen > sizeof(buf)) readlen = sizeof(buf);
        }

        // ��������
        nread = read(fd,buf,readlen);

         // û�����ݿɶ�
        if (nread == -1 && errno == EAGAIN) return; /* No more data ready. */

        // ����������
        if (nread <= 0) {
            //��Ⱥĳ���ڵ���ˣ������˳�
            /* I/O error... */
            redisLog(REDIS_DEBUG,"I/O error reading from node link: %s",
                (nread == 0) ? "connection closed" : strerror(errno));
            handleLinkIOError(link);
            return;
        } else {
            /* Read data and recast the pointer to the new buffer. */
            // �����������׷�ӽ����뻺��������
            link->rcvbuf = sdscatlen(link->rcvbuf,buf,nread);
            hdr = (clusterMsg*) link->rcvbuf;
            rcvbuflen += nread;
        }

        /* Total length obtained? Process this packet. */
         // ����Ѷ������ݵĳ��ȣ����Ƿ�������Ϣ�Ѿ���������
        if (rcvbuflen >= 8 && rcvbuflen == ntohl(hdr->totlen)) {
            // ����ǵĻ���ִ�д�����Ϣ�ĺ���
            if (clusterProcessPacket(link)) {
                sdsfree(link->rcvbuf);
                link->rcvbuf = sdsempty();
            } else {
                return; /* Link no longer valid. */
            }
        }
    }
}

/* Put stuff into the send buffer.
 *
 * ������Ϣ
 *
 * It is guaranteed that this function will never have as a side effect
 * the link to be invalidated, so it is safe to call this function
 * from event handlers that will do stuff with the same link later. 
 *
 * ��Ϊ���Ͳ�������ӱ�����ɲ����ĸ����ã�
 * ���Կ����ڷ�����Ϣ�Ĵ���������һЩ������ӱ���Ķ�����
 */
void clusterSendMessage(clusterLink *link, unsigned char *msg, size_t msglen) {
    // ��װд�¼�������
    if (sdslen(link->sndbuf) == 0 && msglen != 0)
        aeCreateFileEvent(server.el,link->fd,AE_WRITABLE,
                    clusterWriteHandler,link);

   // ����Ϣ׷�ӵ����������
    link->sndbuf = sdscatlen(link->sndbuf, msg, msglen);

     // ��һ������Ϣ����
    server.cluster->stats_bus_messages_sent++;
}

/* Send a message to all the nodes that are part of the cluster having
 * a connected link.
 *
 * ��ڵ����ӵ����������ڵ㷢����Ϣ��
 *
 * It is guaranteed that this function will never have as a side effect
 * some node->link to be invalidated, so it is safe to call this function
 * from event handlers that will do stuff with node links later. */
void clusterBroadcastMessage(void *buf, size_t len) { //buf���������ΪclusterMsg+clusterMsgData
    dictIterator *di;
    dictEntry *de;

     // ����������֪�ڵ�
    di = dictGetSafeIterator(server.cluster->nodes);
    while((de = dictNext(di)) != NULL) {
        clusterNode *node = dictGetVal(de);

         // ����δ���ӽڵ㷢����Ϣ
        if (!node->link) continue;

         // ����ڵ�������� HANDSHAKE ״̬�Ľڵ㷢����Ϣ
        if (node->flags & (REDIS_NODE_MYSELF|REDIS_NODE_HANDSHAKE))
            continue;

         // ������Ϣ
        clusterSendMessage(node->link,buf,len);
    }
    dictReleaseIterator(di);
}

/* Build the message header */
// ������Ϣ   ��Ⱥ���ڵ㽻����Ϣ��ͨ��ͷ����Ϣ   ���clusterMsg��clusterBuildMessageHdr�������clusterProcessPacket
void clusterBuildMessageHdr(clusterMsg *hdr, int type) { //typeȡֵCLUSTERMSG_TYPE_PING��
    int totlen = 0;
    uint64_t offset;
    clusterNode *master;

    /* If this node is a master, we send its slots bitmap and configEpoch.
     *
     * �������һ�����ڵ㣬��ô���͸ýڵ�Ĳ� bitmap �����ü�Ԫ��
     *
     * If this node is a slave we send the master's information instead (the
     * node is flagged as slave so the receiver knows that it is NOT really
     * in charge for this slots.
     *�������һ���ӽڵ㣬    
     * ��ô��������ڵ�����ڵ�Ĳ� bitmap �����ü�Ԫ��   
     *   
     * ��Ϊ������Ϣ�Ľڵ�ͨ����ʶ����֪������ڵ���һ���ӽڵ㣬   
     * ���Խ�����Ϣ�Ľڵ㲻�Ὣ�ӽڵ�����������ڵ㡣
     */
    master = (nodeIsSlave(myself) && myself->slaveof) ?
              myself->slaveof : myself;
    // ������Ϣͷ
    memset(hdr,0,sizeof(*hdr));

    hdr->sig[0] = 'R';
    hdr->sig[1] = 'C';
    hdr->sig[2] = 'm';
    hdr->sig[3] = 'b';

     // ������Ϣ����
    hdr->type = htons(type);

    // ������Ϣ������
    memcpy(hdr->sender,myself->name,REDIS_CLUSTER_NAMELEN);

     // ���õ�ǰ�ڵ㸺��Ĳ�
    memcpy(hdr->myslots,master->slots,sizeof(hdr->myslots));

    // ���� slaveof ��
    memset(hdr->slaveof,0,REDIS_CLUSTER_NAMELEN);

    // ����ڵ��Ǵӽڵ�Ļ�����ô���� slaveof ��
    if (myself->slaveof != NULL)
        memcpy(hdr->slaveof,myself->slaveof->name, REDIS_CLUSTER_NAMELEN);

     // ���ö˿ں�
    hdr->port = htons(server.port);

     // ���ñ�ʶ
    hdr->flags = htons(myself->flags);

    // ����״̬
    hdr->state = server.cluster->state;

    /* Set the currentEpoch and configEpochs. */
    // ���ü�Ⱥ��ǰ���ü�Ԫ
    hdr->currentEpoch = htonu64(server.cluster->currentEpoch);
    // �������ڵ㵱ǰ���ü�Ԫ
    hdr->configEpoch = htonu64(master->configEpoch);

    /* Set the replication offset. */
     // ���ø���ƫ����
    if (nodeIsSlave(myself)) //���ڵ��Ǵӽڵ�
        offset = replicationGetSlaveOffset();
    else //���ڵ������ڵ�
        offset = server.master_repl_offset;
        
    hdr->offset = htonu64(offset); //�������Լ��ĸ���ƫ����

    /* Set the message flags. */
    if (nodeIsMaster(myself) && server.cluster->mf_end) //����ڽ���cluster failover�ֶ�����ת�ƣ�����ϸñ�ʶ
        hdr->mflags[0] |= CLUSTERMSG_FLAG0_PAUSED;

    /* Compute the message length for certain messages. For other messages
     * this is up to the caller. */
    // ������Ϣ�ĳ���
    if (type == CLUSTERMSG_TYPE_FAIL) {
        totlen = sizeof(clusterMsg)-sizeof(union clusterMsgData);
        totlen += sizeof(clusterMsgDataFail);
    } else if (type == CLUSTERMSG_TYPE_UPDATE) {
        totlen = sizeof(clusterMsg)-sizeof(union clusterMsgData);
        totlen += sizeof(clusterMsgDataUpdate);
    }

     // ������Ϣ�ĳ���
    hdr->totlen = htonl(totlen);
    /* For PING, PONG, and MEET, fixing the totlen field is up to the caller. */
}

/* Send a PING or PONG packet to the specified node, making sure to add enough
 * gossip informations. */

// ��ָ���ڵ㷢��һ�� MEET �� PING ���� PONG ��Ϣ��link��Ӧ�Ľڵ㣬ͬʱ����ϱ��ڵ����ڼ�Ⱥ�е��������������ڵ���Ϣ����link��Ӧ�Ľڵ�
void clusterSendPing(clusterLink *link, int type) { //�����ȥ���ڵ����ڼ�Ⱥ�е�������������node�ڵ�(������link���ڵ��link��Ӧ�Ľڵ�)��Ϣ���͸�link��Ӧ�Ľڵ�
    unsigned char buf[sizeof(clusterMsg)];
    clusterMsg *hdr = (clusterMsg*) buf;
    int gossipcount = 0, totlen;
    /* freshnodes is the number of nodes we can still use to populate the
     * gossip section of the ping packet. Basically we start with the nodes
     * we have in memory minus two (ourself and the node we are sending the
     * message to). Every time we add a node we decrement the counter, so when
     * it will drop to <= zero we know there is no more gossip info we can
     * send. */
     // freshnodes �����ڷ��� gossip ��Ϣ�ļ�����   
     // ÿ�η���һ����Ϣʱ������ freshnodes ��ֵ��һ  
     // �� freshnodes ����ֵС�ڵ��� 0 ʱ������ֹͣ���� gossip ��Ϣ   
     // freshnodes �������ǽڵ�Ŀǰ�� nodes ���еĽڵ�������ȥ 2   
     // ����� 2 ָ�����ڵ㣬һ���� myself �ڵ㣨Ҳ���Ƿ�����Ϣ������ڵ㣩
     // ��һ���ǽ��� gossip ��Ϣ�Ľڵ�
    int freshnodes = dictSize(server.cluster->nodes)-2; //��ȥ���ڵ�ͽ��ձ�ping��Ϣ�Ľڵ��⣬������Ⱥ���ж��������ڵ�

   // ������͵���Ϣ�� PING ����ô�������һ�η��� PING �����ʱ���
    if (link->node && type == CLUSTERMSG_TYPE_PING)
        link->node->ping_sent = mstime();

   // ����ǰ�ڵ����Ϣ���������֡���ַ���˿ںš�������Ĳۣ���¼����Ϣ����
    clusterBuildMessageHdr(hdr,type);

    /* Populate the gossip fields */
    // �ӵ�ǰ�ڵ���֪�Ľڵ������ѡ�������ڵ�   
    // ��ͨ��������Ϣ�Ӵ���Ŀ��ڵ㣬�Ӷ�ʵ�� gossip Э��  
    // ÿ���ڵ��� freshnodes �η��� gossip ��Ϣ�Ļ���  
    // ÿ����Ŀ��ڵ㷢�� 2 ����ѡ�нڵ�� gossip ��Ϣ��gossipcount ������
    while(freshnodes > 0 && gossipcount < 3) {
        // �� nodes �ֵ������ѡ��һ���ڵ㣨��ѡ�нڵ㣩
        dictEntry *de = dictGetRandomKey(server.cluster->nodes);
        clusterNode *this = dictGetVal(de);

        clusterMsgDataGossip *gossip; ////ping  pong meet��Ϣ�岿���øýṹ
        int j;

        /* In the gossip section don't include:
         * ���½ڵ㲻����Ϊ��ѡ�нڵ㣺        
         * 1) Myself.       
         *    �ڵ㱾��        
         * 2) Nodes in HANDSHAKE state.      
         *    ���� HANDSHAKE ״̬�Ľڵ㡣     
         * 3) Nodes with the NOADDR flag set.      
         *    ���� NOADDR ��ʶ�Ľڵ�    
         * 4) Disconnected nodes if they don't have configured slots.   
         *    ��Ϊ�������κβ۶����Ͽ����ӵĽڵ� 
         */
        if (this == myself ||
            this->flags & (REDIS_NODE_HANDSHAKE|REDIS_NODE_NOADDR) ||
            (this->link == NULL && this->numslots == 0))
        {
                freshnodes--; /* otherwise we may loop forever. */
                continue;
        }

        /* Check if we already added this node */
         // ��鱻ѡ�нڵ��Ƿ��Ѿ��� hdr->data.ping.gossip ��������       
         // ����ǵĻ�˵������ڵ�֮ǰ�Ѿ���ѡ����   
         // ��Ҫ��ѡ����������ͻ�����ظ���
        for (j = 0; j < gossipcount; j++) {  //�����Ǳ���ǰ�����ѡ��clusterNode��ʱ���ظ�ѡ����ͬ�Ľڵ�
            if (memcmp(hdr->data.ping.gossip[j].nodename,this->name,
                    REDIS_CLUSTER_NAMELEN) == 0) break;
        }
        if (j != gossipcount) continue;

        /* Add it */

         // �����ѡ�нڵ���Ч����������һ
        freshnodes--;

          // ָ�� gossip ��Ϣ�ṹ
        gossip = &(hdr->data.ping.gossip[gossipcount]);

        // ����ѡ�нڵ�����ּ�¼�� gossip ��Ϣ    
        memcpy(gossip->nodename,this->name,REDIS_CLUSTER_NAMELEN);  
        // ����ѡ�нڵ�� PING �����ʱ�����¼�� gossip ��Ϣ       
        gossip->ping_sent = htonl(this->ping_sent);      
        // ����ѡ�нڵ�� PING ����ظ���ʱ�����¼�� gossip ��Ϣ     
        gossip->pong_received = htonl(this->pong_received);   
        // ����ѡ�нڵ�� IP ��¼�� gossip ��Ϣ       
        memcpy(gossip->ip,this->ip,sizeof(this->ip));    
        // ����ѡ�нڵ�Ķ˿ںż�¼�� gossip ��Ϣ    
        gossip->port = htons(this->port);       
        // ����ѡ�нڵ�ı�ʶֵ��¼�� gossip ��Ϣ   
        gossip->flags = htons(this->flags);       
        // �����ѡ�нڵ���Ч����������һ
        gossipcount++;
    }

    // ������Ϣ����    
    totlen = sizeof(clusterMsg)-sizeof(union clusterMsgData);  
    totlen += (sizeof(clusterMsgDataGossip)*gossipcount);    
    // ����ѡ�нڵ��������gossip ��Ϣ�а����˶��ٸ��ڵ����Ϣ��   
    // ��¼�� count ��������   
    hdr->count = htons(gossipcount);   
    // ����Ϣ�ĳ��ȼ�¼����Ϣ����  
    hdr->totlen = htonl(totlen);   
    // ������Ϣ
    clusterSendMessage(link,buf,totlen);
}

/* Send a PONG packet to every connected node that's not in handshake state
 * and for which we have a valid link.
 *
 * ������δ�� HANDSHAKE ״̬���������������Ľڵ㷢�� PONG �ظ���

 *
 * In Redis Cluster pongs are not used just for failure detection, but also
 * to carry important configuration information. So broadcasting a pong is
 * useful when something changes in the configuration and we want to make
 * the cluster aware ASAP (for instance after a slave promotion). *
 * �ڼ�Ⱥ�У� PONG ���������������ڵ�״̬��
 * ������Я��һЩ��Ҫ����Ϣ��
 *
 * ��˹㲥 PONG �ظ������÷����仯������ӽڵ�ת��Ϊ���ڵ㣩��
 * ���ҵ�ǰ�ڵ����������ڵ㾡��֪Ϥ��һ�仯��ʱ��
 * �ͻ�㲥 PONG �ظ���
 *
 * The 'target' argument specifies the receiving instances using the
 * defines below:
 *
 * CLUSTER_BROADCAST_ALL -> All known instances.
 * CLUSTER_BROADCAST_LOCAL_SLAVES -> All slaves in my master-slaves ring.
 */
#define CLUSTER_BROADCAST_ALL 0
#define CLUSTER_BROADCAST_LOCAL_SLAVES 1

/*
����clusterBroadcastPong������������ڵ�����дӽڵ㷢��PONG������ͷ���ִ�
�е�ǰ�ӽڵ�ĸ�������������������ӽڵ��յ�֮�󣬿��Ը����Լ������������ֱ�ӷ��أ�
*/
void clusterBroadcastPong(int target) {
    dictIterator *di;
    dictEntry *de;

    // �������нڵ�
    di = dictGetSafeIterator(server.cluster->nodes);
    while((de = dictNext(di)) != NULL) {
        clusterNode *node = dictGetVal(de);

        // ����δ�������ӵĽڵ㷢��
        if (!node->link) continue;
        if (node == myself || nodeInHandshake(node)) continue;
        if (target == CLUSTER_BROADCAST_LOCAL_SLAVES) {
            int local_slave =
                nodeIsSlave(node) && node->slaveof &&
                (node->slaveof == myself || node->slaveof == myself->slaveof);
            if (!local_slave) continue;
        }
         // ���� PONG ��Ϣ
        clusterSendPing(node->link,CLUSTERMSG_TYPE_PONG);
    }
    dictReleaseIterator(di);
}

/* Send a PUBLISH message.
 *
 * ����һ�� PUBLISH ��Ϣ��

 *
 * If link is NULL, then the message is broadcasted to the whole cluster. 
 *
 * ��� link ����Ϊ NULL ����ô����Ϣ�㲥��������Ⱥ��
 */
void clusterSendPublish(clusterLink *link, robj *channel, robj *message) {
    unsigned char buf[sizeof(clusterMsg)], *payload;
    clusterMsg *hdr = (clusterMsg*) buf;
    uint32_t totlen;
    uint32_t channel_len, message_len;

    // Ƶ��
    channel = getDecodedObject(channel);

     // ��Ϣ
    message = getDecodedObject(message);

     // Ƶ������Ϣ�ĳ���
    channel_len = sdslen(channel->ptr);
    message_len = sdslen(message->ptr);

    // ������Ϣ
    clusterBuildMessageHdr(hdr,CLUSTERMSG_TYPE_PUBLISH);
    totlen = sizeof(clusterMsg)-sizeof(union clusterMsgData);
    totlen += sizeof(clusterMsgDataPublish) + channel_len + message_len;

    hdr->data.publish.msg.channel_len = htonl(channel_len);
    hdr->data.publish.msg.message_len = htonl(message_len);
    hdr->totlen = htonl(totlen);

    /* Try to use the local buffer if possible */
    if (totlen < sizeof(buf)) {
        payload = buf;
    } else {
        payload = zmalloc(totlen);
        memcpy(payload,hdr,sizeof(*hdr));
        hdr = (clusterMsg*) payload;
    }

    // ����Ƶ������Ϣ����Ϣ�ṹ��
    memcpy(hdr->data.publish.msg.bulk_data,channel->ptr,sdslen(channel->ptr));
    memcpy(hdr->data.publish.msg.bulk_data+sdslen(channel->ptr),
        message->ptr,sdslen(message->ptr));

    // ѡ���͵��ڵ㻹�ǹ㲥��������Ⱥ
    if (link)
        clusterSendMessage(link,payload,totlen);
    else
        clusterBroadcastMessage(payload,totlen);

    decrRefCount(channel);
    decrRefCount(message);
    if (payload != buf) zfree(payload);
}

/* Send a FAIL message to all the nodes we are able to contact.
 *
 * ��ǰ�ڵ���֪�����нڵ㷢�� FAIL ��Ϣ��
 *
 * The FAIL message is sent when we detect that a node is failing
 * (REDIS_NODE_PFAIL) and we also receive a gossip confirmation of this:
 * we switch the node state to REDIS_NODE_FAIL and ask all the other
 * nodes to do the same ASAP. 
 *
 * �����ǰ�ڵ㽫 node ���Ϊ PFAIL ״̬��
 * ����ͨ�� gossip Э�飬
 * ���㹻�����Ľڵ���Щ�õ��� node �Ѿ����ߵ�֧�֣� 
 * ��ô��ǰ�ڵ�Ὣ node ���Ϊ FAIL ��
 * ��ִ����������������� node ���� FAIL ��Ϣ�� 
 * Ҫ������Ҳ�� node ���Ϊ FAIL ��
 */ //ֻ�����ڵ��жϳ�ĳ���ڵ�fail�˲Ż���øú���֪ͨ�����������ڵ㣬�����ڵ�ͻ�Ѹ����߽ڵ���Ϊfail
void clusterSendFail(char *nodename) { 
//�������һ������ڵ���Ϊ��nodename�ڵ������ˣ�����Ҫ�Ѹýڵ�������Ϣͬ��������cluster��Ⱥ
    unsigned char buf[sizeof(clusterMsg)];
    clusterMsg *hdr = (clusterMsg*) buf;

     // ����������Ϣ  
     clusterBuildMessageHdr(hdr,CLUSTERMSG_TYPE_FAIL);   
     // ��¼����    
     memcpy(hdr->data.fail.about.nodename,nodename,REDIS_CLUSTER_NAMELEN);  

     // �㲥��Ϣ
    clusterBroadcastMessage(buf,ntohl(hdr->totlen));
}

/* Send an UPDATE message to the specified link carrying the specified 'node'
 * slots configuration. The node name, slots bitmap, and configEpoch info
 * are included. 
 *
 * ������ link ���Ͱ������� node �����õ� UPDATE ��Ϣ��
 * �����ڵ����ƣ���λͼ���Լ����ü�Ԫ��
 */
void clusterSendUpdate(clusterLink *link, clusterNode *node) {
    unsigned char buf[sizeof(clusterMsg)];
    clusterMsg *hdr = (clusterMsg*) buf;

    if (link == NULL) return;

    // ������Ϣ   
    clusterBuildMessageHdr(hdr,CLUSTERMSG_TYPE_UPDATE);   
    // ���ýڵ���   
    memcpy(hdr->data.update.nodecfg.nodename,node->name,REDIS_CLUSTER_NAMELEN);   
    // �������ü�Ԫ   
    hdr->data.update.nodecfg.configEpoch = htonu64(node->configEpoch);    
    // ���½ڵ�Ĳ�λͼ   
    memcpy(hdr->data.update.nodecfg.slots,node->slots,sizeof(node->slots));    

    // ������Ϣ
    clusterSendMessage(link,buf,ntohl(hdr->totlen));
}

/* -----------------------------------------------------------------------------
 * CLUSTER Pub/Sub support
 *
 * For now we do very little, just propagating PUBLISH messages across the whole
 * cluster. In the future we'll try to get smarter and avoiding propagating those
 * messages to hosts without receives for a given channel.
 * -------------------------------------------------------------------------- */
    // ��������Ⱥ�� channel Ƶ���й㲥��Ϣ messages
void clusterPropagatePublish(robj *channel, robj *message) {
    clusterSendPublish(NULL, channel, message);
}

/* -----------------------------------------------------------------------------
 * SLAVE node specific functions
 * -------------------------------------------------------------------------- */

/* This function sends a FAILOVE_AUTH_REQUEST message to every node in order to
 * see if there is the quorum for this slave instance to failover its failing
 * master.
 *
 * ���������нڵ㷢�� FAILOVE_AUTH_REQUEST ��Ϣ�� 
 * �������Ƿ�ͬ��������ӽڵ��������ߵ����ڵ���й���ת�ơ�

 *
 * Note that we send the failover request to everybody, master and slave nodes,
 * but only the masters are supposed to reply to our query. 
 *
 * ��Ϣ�ᱻ���͸����нڵ㣬�������ڵ�ʹӽڵ㣬��ֻ�����ڵ��ظ�������Ϣ�� 
 */ // ���������нڵ㷢����Ϣ���������Ƿ�֧���ɱ��ڵ������������ڵ���й���ת��
 
 //slave��⵽�Լ���master���ˣ�����clusterRequestFailoverAuth����failover request�������ڵ��յ�����
 //clusterSendFailoverAuthIfNeeded����ͶƱ��ֻ�����ڵ��Ӧ��
void clusterRequestFailoverAuth(void) {
    unsigned char buf[sizeof(clusterMsg)];
    clusterMsg *hdr = (clusterMsg*) buf;
    uint32_t totlen;

   // ������Ϣͷ��������ǰ�ڵ����Ϣ��
    clusterBuildMessageHdr(hdr,CLUSTERMSG_TYPE_FAILOVER_AUTH_REQUEST);
    /* If this is a manual failover, set the CLUSTERMSG_FLAG0_FORCEACK bit
     * in the header to communicate the nodes receiving the message that
     * they should authorized the failover even if the master is working. */
    if (server.cluster->mf_end) hdr->mflags[0] |= CLUSTERMSG_FLAG0_FORCEACK;
    totlen = sizeof(clusterMsg)-sizeof(union clusterMsgData);
    hdr->totlen = htonl(totlen);

    // ������Ϣ
    clusterBroadcastMessage(buf,totlen);
}

/* Send a FAILOVER_AUTH_ACK message to the specified node. */
// ��ڵ� node ͶƱ��֧�������й���Ǩ��
void clusterSendFailoverAuth(clusterNode *node) {
    unsigned char buf[sizeof(clusterMsg)];
    clusterMsg *hdr = (clusterMsg*) buf;
    uint32_t totlen;

    if (!node->link) return;
    clusterBuildMessageHdr(hdr,CLUSTERMSG_TYPE_FAILOVER_AUTH_ACK);
    totlen = sizeof(clusterMsg)-sizeof(union clusterMsgData);
    hdr->totlen = htonl(totlen);
    clusterSendMessage(node->link,buf,totlen);
}

/* Send a MFSTART message to the specified node. */
// ������Ľڵ㷢��һ�� MFSTART ��Ϣ
void clusterSendMFStart(clusterNode *node) { //��ͨ��redis-cli��slave�ڵ㷢��cluster failoverʱ���ӽڵ�ᷢ��CLUSTERMSG_TYPE_MFSTART���Լ������ڵ�
    unsigned char buf[sizeof(clusterMsg)];
    clusterMsg *hdr = (clusterMsg*) buf;
    uint32_t totlen;

    if (!node->link) return;
    clusterBuildMessageHdr(hdr,CLUSTERMSG_TYPE_MFSTART);
    totlen = sizeof(clusterMsg)-sizeof(union clusterMsgData);
    hdr->totlen = htonl(totlen);
    clusterSendMessage(node->link,buf,totlen);
}

/* Vote for the node asking for our vote if there are the conditions. */
// ���������������£�Ϊ������й���ת�ƵĽڵ� node ����ͶƱ��֧�������й���ת��
//slave��⵽�Լ���master���ˣ�����clusterRequestFailoverAuth����failover request�������ڵ��յ�����clusterSendFailoverAuthIfNeeded����ͶƱ��ֻ�����ڵ��Ӧ��
void clusterSendFailoverAuthIfNeeded(clusterNode *node, clusterMsg *request) {//nodeΪsender
//���ڵ��slave���͹�����CLUSTERMSG_TYPE_FAILOVER_AUTH_REQUEST����ͶƱ
/*
��Ⱥ�����нڵ��յ�������Ʊ��CLUSTERMSG_TYPE_FAILOVER_AUTH_REQUEST����ֻ�и���һ����λ�����ڵ���ͶƱ������û�ʸ�Ľڵ�ֱ�Ӻ��Ե��ð���
*/
    // ����ڵ�����ڵ�    
    clusterNode *master = node->slaveof;   

    // ����ڵ�ĵ�ǰ���ü�Ԫ   
    uint64_t requestCurrentEpoch = ntohu64(request->currentEpoch);    

    // ����ڵ���Ҫ���ͶƱ�ļ�Ԫ  
    uint64_t requestConfigEpoch = ntohu64(request->configEpoch);   
    // ����ڵ�Ĳ۲���

    unsigned char *claimed_slots = request->myslots;
    int force_ack = request->mflags[0] & CLUSTERMSG_FLAG0_FORCEACK;
    int j;

    /* IF we are not a master serving at least 1 slot, we don't have the
     * right to vote, as the cluster size in Redis Cluster is the number
     * of masters serving at least one slot, and quorum is the cluster
     * size + 1 */

    // ����ڵ�Ϊ�ӽڵ㣬������һ��û�д����κβ۵����ڵ㣬 
    // ��ô��û��ͶƱȨ
    if (nodeIsSlave(myself) || myself->numslots == 0) return; //�ӽڵ�Ͳ������λ�����ֱ�ӷ��أ�������ͶƱ

    /* Request epoch must be >= our currentEpoch. */
     // ��������ü�Ԫ������ڵ��ڵ�ǰ�ڵ�����ü�Ԫ
     /*
     ��������ߵ�currentEpochС�ڵ�ǰ�ڵ��currentEpoch����ܾ�Ϊ��ͶƱ����Ϊ�����ߵ�״̬�뵱ǰ��Ⱥ״̬��һ�£�
     �����ǳ�ʱ�����ߵĽڵ�ո����ߣ���������£�ֱ�ӷ��ؼ��ɣ�
     */
    if (requestCurrentEpoch < server.cluster->currentEpoch) {
        redisLog(REDIS_WARNING,
            "Failover auth denied to %.40s: reqEpoch (%llu) < curEpoch(%llu)",
            node->name,
            (unsigned long long) requestCurrentEpoch,
            (unsigned long long) server.cluster->currentEpoch);
        return;
    }

    /* I already voted for this epoch? Return ASAP. */
    // �Ѿ�Ͷ��Ʊ��
    /*
    �����ǰ�ڵ�lastVoteEpoch���뵱ǰ�ڵ��currentEpoch��ȣ�˵������ѡ���У���ǰ�ڵ��Ѿ�Ͷ��Ʊ�ˣ���
    ���ظ�ͶƱ��ֱ�ӷ��أ���ˣ�����������ӽڵ�ͬʱ������Ʊ����ǰ�ڵ����յ��ĸ��ڵ�İ�����ֻ���Ǹ�
    �ڵ�ͶƱ��ע�⣬��ʹ�������ӽڵ������ͬ���ڵ㣬Ҳֻ����һ���ӽڵ���ѡƱ����
    */
    if (server.cluster->lastVoteEpoch == server.cluster->currentEpoch) {
        redisLog(REDIS_WARNING,
                "Failover auth denied to %.40s: already voted for epoch %llu",
                node->name,
                (unsigned long long) server.cluster->currentEpoch);
        return;
    }
    
    /* Node must be a slave and its master down.
     * The master can be non failing if the request is flagged
     * with CLUSTERMSG_FLAG0_FORCEACK (manual failover). */
    /*
    ������ͽڵ������ڵ㣻���߷��ͽڵ���Ȼ�Ǵӽڵ㣬�����Ҳ��������ڵ㣻���߷��ͽڵ�����ڵ㲢δ����
    �����ⲻ���ֶ�ǿ�ƿ�ʼ�Ĺ���ת�����̣�����ݲ�ͬ����������¼��־��ֱ�ӷ��أ�
    */
    if (nodeIsMaster(node) || master == NULL ||
        (!nodeFailed(master) && !force_ack)) { //����ӽ��յ�cluster failover��Ȼ����auth reqҪ��ͶƱ����master�ܵ��󣬾��Ǹ�master����Ҳ��Ҫ����ͶƱ
        if (nodeIsMaster(node)) { //auth  request������slave����
            redisLog(REDIS_WARNING,
                    "Failover auth denied to %.40s: it is a master node",
                    node->name);
        } else if (master == NULL) {
        //slave��Ϊ�Լ���master�����ˣ����Ǳ��ڵ㲻֪������master���Ǹ���Ҳ�Ͳ�֪����Ϊ�Ǹ�master��slaveͶƱ��
        //��Ϊ����Ҫ��¼�Ƕ��Ǹ�master�Ĵӽڵ�ͶƱ�ģ���if���������
            redisLog(REDIS_WARNING,
                    "Failover auth denied to %.40s: I don't know its master",
                    node->name);
        } else if (!nodeFailed(master)) { //������Ҳ���Կ��������뼯Ⱥ����һ�����ڵ��жϳ�ĳ���ڵ�fail�ˣ��Żᴦ��slave���͹�����auth req
        //slave��Ϊ�Լ���master�����ˣ����Ƿ��͹���auth request,�����ڵ��յ�����Ϣ�󣬷���
        //��slave��Ӧ��master�������ģ���˸�����ӡ����ͶƱ
            redisLog(REDIS_WARNING,
                    "Failover auth denied to %.40s: its master is up",
                    node->name);
        }
        return;
    }
    /* We did not voted for a slave about this master for two
     * times the node timeout. This is not strictly needed for correctness
     * of the algorithm but makes the base case more linear. */
     /*
     ���ͬһ���������ڵ㣬��2*server.cluster_node_timeoutʱ���ڣ�ֻ��Ͷһ��Ʊ���Ⲣ�Ǳ����������
     ������Ϊ֮ǰ��lastVoteEpoch�жϣ��Ѿ����Ա��������ӽڵ�ͬʱӮ�ñ���ѡ���ˣ������������ʹ�û�
     ʤ�ӽڵ���ʱ�佫���Ϊ�����ڵ����Ϣ֪ͨ�������ӽڵ㣬�Ӷ�������һ���ӽڵ㷢����һ��ѡ���ֽ�
     ��һ��û��Ҫ�Ĺ���ת�ƣ�
     */
     // ���֮ǰһ��ʱ���Ѿ�������ڵ���й�ͶƱ����ô������ͶƱ
    if (mstime() - node->slaveof->voted_time < server.cluster_node_timeout * 2)
    {
        redisLog(REDIS_WARNING,
                "Failover auth denied to %.40s: "
                "can't vote about this master before %lld milliseconds",
                node->name,
                (long long) ((server.cluster_node_timeout*2)-
                             (mstime() - node->slaveof->voted_time)));
        return;
    }

    /* The slave requesting the vote must have a configEpoch for the claimed
     * slots that is >= the one of the masters currently serving the same
     * slots in the current configuration. */
    /*
        �жϷ��ͽڵ㣬��������Ҫ����Ĳ�λ���Ƿ��֮ǰ������Щ��λ�Ľڵ㣬������Ȼ���µ����ü�ԪconfigEpoch��
    �ò�λ��ǰ�ĸ���ڵ��configEpoch���Ƿ�ȷ��ͽڵ��configEpochҪ�����ǣ�˵�����ͽڵ��������Ϣ�������µģ�
    ������һ����ʱ�����ߵĽڵ������������ˣ���������£����ܸ���ͶƱ�����ֱ�ӷ��أ�
    */
    for (j = 0; j < REDIS_CLUSTER_SLOTS; j++) {

         // ����δָ�ɽڵ�
        if (bitmapTestBit(claimed_slots, j) == 0) continue;

        // �����Ƿ���ĳ���۵����ü�Ԫ���ڽڵ�����ļ�Ԫ
        if (server.cluster->slots[j] == NULL || server.cluster->slots[j]->configEpoch <= requestConfigEpoch) 
        //�������configEpoch�����������õĵط�
        {
            continue;
        }

        // ����еĻ���˵���ڵ�����ļ�Ԫ�Ѿ����ڣ�û�б�Ҫ����ͶƱ
        /* If we reached this point we found a slot that in our current slots
         * is served by a master with a greater configEpoch than the one claimed
         * by the slave requesting our vote. Refuse to vote for this slave. */
        redisLog(REDIS_WARNING,
                "Failover auth denied to %.40s: "
                "slot %d epoch (%llu) > reqEpoch (%llu)",
                node->name, j,
                (unsigned long long) server.cluster->slots[j]->configEpoch,
                (unsigned long long) requestConfigEpoch);
        return;
    }

    /* We can vote for this slave. */
    // Ϊ�ڵ�ͶƱ    
    clusterSendFailoverAuth(node);
    // ����ʱ��ֵ
    server.cluster->lastVoteEpoch = server.cluster->currentEpoch;
    node->slaveof->voted_time = mstime();

    redisLog(REDIS_WARNING, "Failover auth granted to %.40s for epoch %llu",
        node->name, (unsigned long long) server.cluster->currentEpoch);
}

/* This function returns the "rank" of this instance, a slave, in the context
 * of its master-slaves ring. The rank of the slave is given by the number of
 * other slaves for the same master that have a better replication offset
 * compared to the local one (better means, greater, so they claim more data).
 *
 * A slave with rank 0 is the one with the greatest (most up to date)
 * replication offset, and so forth. Note that because how the rank is computed
 * multiple slaves may have the same rank, in case they have the same offset.
 *
 * The slave rank is used to add a delay to start an election in order to
 * get voted and replace a failing master. Slaves with better replication
 * offsets are more likely to win. */
 /*
rank��ʾ�ӽڵ��������������ָ��ǰ�ӽڵ����������ڵ�����дӽڵ��е�������������Ҫ�Ǹ��ݸ���������������
����������Խ�࣬����Խ��ǰ����ˣ����н϶ิ���������Ĵӽڵ���Ը��緢�����ת�����̣��Ӷ������ܳ�Ϊ�µ����ڵ㡣

Ȼ�����replicationGetSlaveOffset�������õ���ǰ�ӽڵ�ĸ���ƫ����myoffset����������ѵmaster->slaves���飬
ֻҪ���дӽڵ�ĸ���ƫ��������myoffset������������rank��ֵ����û�п�ʼ����ת��֮ǰ��ÿ��һ��ʱ��ͻ����
һ��clusterGetSlaveRank�������Ը��µ�ǰ�ӽڵ��������
 */
int clusterGetSlaveRank(void) { 
//Ϊ���������ڵ�֪�����ڵ��myoffset��ÿ���ڵ�����clusterBroadcastPong�����Լ���ƫ�������߸��Է���
//�����Է��Ϳ��Ի�ȡ���˴����µ�ƫ�������Ӷ��Ϳ��Եõ�����ѡ������
    long long myoffset;
    int j, rank = 0;
    clusterNode *master;

    redisAssert(nodeIsSlave(myself));
    master = myself->slaveof;
    if (master == NULL) return 0; /* Never called by slaves without master. */

    myoffset = replicationGetSlaveOffset();
    for (j = 0; j < master->numslaves; j++)
        if (master->slaves[j] != myself &&
            master->slaves[j]->repl_offset > myoffset) rank++;
    return rank;
}

/* This function is called by clusterHandleSlaveFailover() in order to
 * let the slave log why it is not able to failover. Sometimes there are
 * not the conditions, but since the failover function is called again and
 * again, we can't log the same things continuously.
 *
 * This function works by logging only if a given set of conditions are
 * true:
 *
 * 1) The reason for which the failover can't be initiated changed.
 *    The reasons also include a NONE reason we reset the state to
 *    when the slave finds that its master is fine (no FAIL flag).
 * 2) Also, the log is emitted again if the master is still down and
 *    the reason for not failing over is still the same, but more than
 *    REDIS_CLUSTER_CANT_FAILOVER_RELOG_PERIOD seconds elapsed.
 * 3) Finally, the function only logs if the slave is down for more than
 *    five seconds + NODE_TIMEOUT. This way nothing is logged when a
 *    failover starts in a reasonable time.
 *
 * The function is called with the reason why the slave can't failover
 * which is one of the integer macros REDIS_CLUSTER_CANT_FAILOVER_*.
 *
 * The function is guaranteed to be called only if 'myself' is a slave. */
void clusterLogCantFailover(int reason) {
    char *msg;
    static time_t lastlog_time = 0;
    mstime_t nolog_fail_time = server.cluster_node_timeout + 5000;

    /* Don't log if we have the same reason for some time. */
    if (reason == server.cluster->cant_failover_reason &&
        time(NULL)-lastlog_time < REDIS_CLUSTER_CANT_FAILOVER_RELOG_PERIOD) //��ͬԭ��һ��ʱ��Ŵ�ӡ
        return;

    server.cluster->cant_failover_reason = reason;

    /* We also don't emit any log if the master failed no long ago, the
     * goal of this function is to log slaves in a stalled condition for
     * a long time. */
    if (myself->slaveof &&
        nodeFailed(myself->slaveof) &&
        (mstime() - myself->slaveof->fail_time) < nolog_fail_time) return; //�������ô�ò��ܴ�ӡ

    switch(reason) {
    case REDIS_CLUSTER_CANT_FAILOVER_DATA_AGE:
        msg = "Disconnected from master for longer than allowed. "
              "Please check the 'cluster-slave-validity-factor' configuration "
              "option.";
        break;
    case REDIS_CLUSTER_CANT_FAILOVER_WAITING_DELAY:
        msg = "Waiting the delay before I can start a new failover.";
        break;
    case REDIS_CLUSTER_CANT_FAILOVER_EXPIRED:
        msg = "Failover attempt expired.";
        break;
    case REDIS_CLUSTER_CANT_FAILOVER_WAITING_VOTES:
        msg = "Waiting for votes, but majority still not reached.";
        break;
    default:
        msg = "Unknown reason code.";
        break;
    }
    lastlog_time = time(NULL);
    redisLog(REDIS_WARNING,"Currently unable to failover: %s", msg);
}


/* This function is called if we are a slave node and our master serving
 * a non-zero amount of hash slots is in FAIL state.
 *
 * �����ǰ�ڵ���һ���ӽڵ㣬���������ڸ��Ƶ�һ�����������۵����ڵ㴦�� FAIL ״̬��
 * ��ôִ�����������
 *
 * The gaol of this function is: 
 * 
 * �������������Ŀ�꣺
 *
 * 1) To check if we are able to perform a failover, is our data updated? 
 *    ����Ƿ���Զ����ڵ�ִ��һ�ι���ת�ƣ��ڵ�Ĺ������ڵ����Ϣ�Ƿ�׼ȷ�����£�updated���� 
 * 2) Try to get elected by masters. 
 *    ѡ��һ���µ����ڵ� 
 * 3) Perform the failover informing all the other nodes.
 *    ִ�й���ת�ƣ���֪ͨ�����ڵ�
 */ 
/*
�ӽڵ�Ĺ���ת�ƣ����ں���clusterHandleSlaveFailover�д���ģ��ú����ڼ�Ⱥ��ʱ������clusterCron�е��á�������
���ڴ���ӽڵ���й���ת�Ƶ��������̣��������ж��Ƿ���Է���ѡ�٣��ж�ѡ���Ƿ�ʱ���ж��Լ��Ƿ���
�����㹻��ѡƱ��ʹ�Լ�����Ϊ�µ����ڵ���Щ�������̡�
*/
 //slave����
void clusterHandleSlaveFailover(void) { //clusterBeforeSleep��CLUSTER_TODO_HANDLE_FAILOVER״̬�Ĵ���,����clusterCron��ʵʱ����
    //Ҳ���ǵ�ǰ�ӽڵ������ڵ��Ѿ������˶೤ʱ��,��ͨ��ping pong��ʱ����⵽��slave��master�����ˣ�����ʱ��ʼ��
    mstime_t data_age;
    //�ñ�����ʾ���뷢�����ת�����̣��Ѿ���ȥ�˶���ʱ�䣻
    mstime_t auth_age = mstime() - server.cluster->failover_auth_time;
    //�ñ�����ʾ��ǰ�ӽڵ�������ٻ�ö���ѡƱ�����ܳ�Ϊ�µ����ڵ�
    int needed_quorum = (server.cluster->size / 2) + 1;
    //��ʾ�Ƿ��ǹ���Ա�ֶ������Ĺ���ת�����̣�
    int manual_failover = server.cluster->mf_end != 0 &&
                          server.cluster->mf_can_start; //˵����ӷ�����cluster failover forceҪ��ôӽ���ǿ�ƹ���ת��
    int j;
    //�ñ�����ʾ����ת������(����ͶƱ���ȴ���Ӧ)�ĳ�ʱʱ�䣬������ʱ���û�л���㹻��ѡƱ�����ʾ���ι���ת��ʧ�ܣ�
    mstime_t auth_timeout, 
    //�ñ�����ʾ�ж��Ƿ���Կ�ʼ��һ�ι���ת�����̵�ʱ�䣬ֻ�о�����һ�η������ת��ʱ���Ѿ�����auth_retry_time֮��
    //�ű�ʾ���Կ�ʼ��һ�ι���ת���ˣ�auth_age > auth_retry_time����
             auth_retry_time;

    server.cluster->todo_before_sleep &= ~CLUSTER_TODO_HANDLE_FAILOVER;

    /* Compute the failover timeout (the max time we have to send votes
     * and wait for replies), and the failover retry time (the time to wait
     * before waiting again.
     *
     * Timeout is MIN(NODE_TIMEOUT*2,2000) milliseconds.
     * Retry is two times the Timeout.
     */
    auth_timeout = server.cluster_node_timeout*2;
    if (auth_timeout < 2000) auth_timeout = 2000;
    auth_retry_time = auth_timeout*2;

    /* Pre conditions to run the function, that must be met both in case
     * of an automatic or manual failover:
     * 1) We are a slave.
     * 2) Our master is flagged as FAIL, or this is a manual failover.
     * 3) It is serving slots. */
    /*
    ��ǰ�ڵ������ڵ㣻��ǰ�ڵ��Ǵӽڵ㵫��û�����ڵ㣻��ǰ�ڵ�����ڵ㲻��������״̬���Ҳ����ֶ�ǿ�ƽ��й���ת�ƣ�
    ��ǰ�ڵ�����ڵ�û�и���Ĳ�λ������������һ���������ܽ��й���ת�ƣ�ֱ�ӷ��ؼ��ɣ�
    */
    if (nodeIsMaster(myself) ||
        myself->slaveof == NULL ||
        (!nodeFailed(myself->slaveof) && !manual_failover) ||
        myself->slaveof->numslots == 0) {
        //������slaveof��ΪNULL�ں���������ѡ��Ϊ����ʱ�����ã��������replicationUnsetMaster
        /* There are no reasons to failover, so we set the reason why we
         * are returning without failing over to NONE. */
        server.cluster->cant_failover_reason = REDIS_CLUSTER_CANT_FAILOVER_NONE;
        return;
    }; 

    //slave�ӽڵ���к����������Һ����������Ͽ�������

    /* Set data_age to the number of seconds we are disconnected from
     * the master. */
    //��data_age����Ϊ�ӽڵ������ڵ�ĶϿ�����
    if (server.repl_state == REDIS_REPL_CONNECTED) { //�������֮������Ϊ���粻ͨ����ģ�read�жϲ���epoll err�¼�����״̬Ϊ���
        data_age = (mstime_t)(server.unixtime - server.master->lastinteraction) 
                   * 1000; //Ҳ���ǵ�ǰ�ӽڵ������ڵ����һ��ͨ�Ź��˶����
    } else { 
    //����һ�㶼��ֱ��kill��master���̣���epoll err��֪���ˣ�����replicationHandleMasterDisconnection��״̬��ΪREDIS_REPL_CONNECT
        //���ӽڵ�����ڵ�Ͽ��˶�ã�
        data_age = (mstime_t)(server.unixtime - server.repl_down_since) * 1000; 
    }

    /* Remove the node timeout from the data age as it is fine that we are
     * disconnected from our master at least for the time it was down to be
     * flagged as FAIL, that's the baseline. */
    // node timeout ��ʱ�䲻�������ʱ��֮�� ���data_age����server.cluster_node_timeout�����data_age��
    //��ȥserver.cluster_node_timeout����Ϊ����server.cluster_node_timeoutʱ��û���յ����ڵ��PING�ظ����ŻὫ����ΪPFAIL
    if (data_age > server.cluster_node_timeout)
        data_age -= server.cluster_node_timeout; //��ͨ��ping pong��ʱ����⵽��slave��master�����ˣ�����ʱ��ʼ��

    /* Check if our data is recent enough. For now we just use a fixed
     * constant of ten times the node timeout since the cluster should
     * react much faster to a master down.
     *
     * Check bypassed for manual failovers. */
    // �������ӽڵ�������Ƿ���£�   
    // Ŀǰ�ļ��취�Ƕ���ʱ�䲻�ܳ��� node timeout ��ʮ��
    /* data_age��Ҫ�����жϵ�ǰ�ӽڵ���������ʶȣ����data_age������һ��ʱ�䣬��ʾ��ǰ�ӽڵ�������Ѿ�̫���ˣ�
    �����滻���������ڵ㣬����ڲ����ֶ�ǿ�ƹ���ת�Ƶ�����£�ֱ�ӷ��أ�*/
    if (data_age >
        ((mstime_t)server.repl_ping_slave_period * 1000) +
        (server.cluster_node_timeout * REDIS_CLUSTER_SLAVE_VALIDITY_MULT))
    {
        if (!manual_failover) {
            clusterLogCantFailover(REDIS_CLUSTER_CANT_FAILOVER_DATA_AGE);
            return;
        }
    }

    /* If the previous failover attempt timedout and the retry time has
     * elapsed, we can setup a new one. */

    /*
    ���缯Ⱥ��7��master������redis1������2��slave,ͻȻredis1���ˣ���slave1��slave2����Ҫ������6��master����ͶƱ�������6��
    masterͶƱ��slave1��slave2��Ʊ������3��Ҳ����3��masterͶ����slave1,����3��masterͶ����slave2����ô����slave���ò�������һ��
    ��Ʊ������ֻ�п�����ĳ�ʱ����������ͶƱ�ˡ�����һ������������ٷ�������Ϊ����ͶƱ��ʱ��������ģ����һ��һ��slave��ͶƱ����auth req���
    ��һ��slave��ͶƱ�����ȷ�����Խ�ȷ���Խ���׵õ�ͶƱ
    */ 
    /*
    ���auth_age����auth_retry_time����ʾ���Կ�ʼ������һ�ι���ת���ˡ����֮ǰû�н��й�����ת�ƣ���auth_age��
    ��mstime���϶�����auth_retry_time�����֮ǰ���й�����ת�ƣ���ֻ�о�����һ�η������ת��ʱ���Ѿ�����
    auth_retry_time֮�󣬲ű�ʾ���Կ�ʼ��һ�ι���ת�ơ�
    */
    if (auth_age > auth_retry_time) {  
    //ÿ�γ�ʱ���·���auth reqҪ��������masterͶƱ�������������if��Ȼ���´ε��øú����Ż���if���������
        server.cluster->failover_auth_time = mstime() +
            500 + /* Fixed delay of 500 milliseconds, let FAIL msg propagate. */
            random() % 500; /* Random delay between 0 and 500 milliseconds. */ //�ȵ����ʱ�䵽�Ž��й���ת��
        server.cluster->failover_auth_count = 0;
        server.cluster->failover_auth_sent = 0;
        server.cluster->failover_auth_rank = clusterGetSlaveRank();//���ڵ㰴����master�е�repl_offset����ȡ����
        /* We add another delay that is proportional to the slave rank.
         * Specifically 1 second * rank. This way slaves that have a probably
         * less updated replication offset, are penalized. */
        server.cluster->failover_auth_time +=
            server.cluster->failover_auth_rank * 1000;
            
        /* However if this is a manual failover, no delay is needed. */

        /*
        ע������ǹ���Ա������ֶ�ǿ��ִ�й���ת�ƣ�������server.cluster->failover_auth_timeΪ��ǰʱ�䣬��ʾ��
        ������ʼ����ת�����̣���󣬵���clusterBroadcastPong������������ڵ�����дӽڵ㷢��PONG������ͷ���ִ�
        �е�ǰ�ӽڵ�ĸ�������������������ӽڵ��յ�֮�󣬿��Ը����Լ������������ֱ�ӷ��أ�
        */
        if (server.cluster->mf_end) {
            server.cluster->failover_auth_time = mstime();
            server.cluster->failover_auth_rank = 0;
        }
        redisLog(REDIS_WARNING,
            "Start of election delayed for %lld milliseconds "
            "(rank #%d, offset %lld).",
            server.cluster->failover_auth_time - mstime(),
            server.cluster->failover_auth_rank,
            replicationGetSlaveOffset());
        /* Now that we have a scheduled election, broadcast our offset
         * to all the other slaves so that they'll updated their offsets
         * if our offset is better. */
        /*
        ����clusterBroadcastPong������������ڵ�����дӽڵ㷢��PONG������ͷ���ִ�
        �е�ǰ�ӽڵ�ĸ�������������������ӽڵ��յ�֮�󣬿��Ը����Լ������������ֱ�ӷ��أ�
        */
        clusterBroadcastPong(CLUSTER_BROADCAST_LOCAL_SLAVES);
        return;
    }

    /* ���й���ת�� */

    /* It is possible that we received more updated offsets from other
     * slaves for the same master since we computed our election delay.
     * Update the delay if our rank changed.
     *
     * Not performed if this is a manual failover. */
    /*
    �����û�п�ʼ����ת�ƣ������clusterGetSlaveRank��ȡ�õ�ǰ�ӽڵ��������������Ϊ�ڿ�ʼ����ת��֮ǰ��
    ���ܻ��յ������ӽڵ㷢������������������Ը����������еĸ���ƫ�������±��ڵ�����������������newrank��
    ���newrank��֮ǰ��������������Ҫ���ӹ���ת�ƿ�ʼʱ����ӳ٣�Ȼ��newrank��¼��server.cluster->failover_auth_rank�У�
    */
    if (server.cluster->failover_auth_sent == 0 &&
        server.cluster->mf_end == 0) //��û�н��й�����ׯ��
    {
        int newrank = clusterGetSlaveRank();
        if (newrank > server.cluster->failover_auth_rank) {
            long long added_delay =
                (newrank - server.cluster->failover_auth_rank) * 1000;
            server.cluster->failover_auth_time += added_delay;
            server.cluster->failover_auth_rank = newrank;
            redisLog(REDIS_WARNING,
                "Slave rank updated to #%d, added %lld milliseconds of delay.",
                newrank, added_delay);
        }
    }

    /* Return ASAP if we can't still start the election. */
     // ���ִ�й���ת�Ƶ�ʱ��δ�����ȷ���
    if (mstime() < server.cluster->failover_auth_time) {
        clusterLogCantFailover(REDIS_CLUSTER_CANT_FAILOVER_WAITING_DELAY);
        return;
    }

    /* Return ASAP if the election is too old to be valid. */
    // �������Ӧ��ִ�й���ת�Ƶ�ʱ���Ѿ����˺ܾ�   
    // ��ô��Ӧ����ִ�й���ת���ˣ���Ϊ�����Ѿ�û����Ҫ�ˣ�
    // ֱ�ӷ���
    if (auth_age > auth_timeout) {// ���auth_age����auth_timeout��˵��֮ǰ�Ĺ���ת�Ƴ�ʱ�ˣ����ֱ�ӷ��أ�
        clusterLogCantFailover(REDIS_CLUSTER_CANT_FAILOVER_EXPIRED);
        return;
    }
    

    /* Ask for votes if needed. */
   // �������ڵ㷢�͹���ת������
    if (server.cluster->failover_auth_sent == 0) {

         // �������ü�Ԫ
        server.cluster->currentEpoch++;

         // ��¼�������ת�Ƶ����ü�Ԫ
        server.cluster->failover_auth_epoch = server.cluster->currentEpoch;

        redisLog(REDIS_WARNING,"Starting a failover election for epoch %llu.",
            (unsigned long long) server.cluster->currentEpoch);

        //���������нڵ㷢����Ϣ���������Ƿ�֧���ɱ��ڵ������������ڵ���й���ת��
        clusterRequestFailoverAuth();

         // �򿪱�ʶ����ʾ�ѷ�����Ϣ
        server.cluster->failover_auth_sent = 1;

        // TODO:       
        // �ڽ����¸��¼�ѭ��֮ǰ��ִ�У�      
        // 1�����������ļ�      
        // 2�����½ڵ�״̬        
        // 3��ͬ������
        clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG|
                             CLUSTER_TODO_UPDATE_STATE|
                             CLUSTER_TODO_FSYNC_CONFIG);
        return; /* Wait for replies. */
    }

    /* Check if we reached the quorum. */
   // �����ǰ�ڵ������㹻���ͶƱ����ô���������ڵ���й���ת��
    if (server.cluster->failover_auth_count >= needed_quorum) {
        // �����ڵ�
        clusterNode *oldmaster = myself->slaveof; //�ں���clusterSetNodeAsMaster�а�slaveof��ΪNULL

        redisLog(REDIS_WARNING,
            "Failover election won: I'm the new master.");
        redisLog(REDIS_WARNING,
                "configEpoch set to %llu after successful failover",
                (unsigned long long) myself->configEpoch);

        /* We have the quorum, perform all the steps to correctly promote
         * this slave to a master.
         *
         * 1) Turn this node into a master. 
         *    ����ǰ�ڵ������ɴӽڵ��Ϊ���ڵ�
         */
        clusterSetNodeAsMaster(myself);
        // �ôӽڵ�ȡ�����ƣ���Ϊ�µ����ڵ�
        replicationUnsetMaster();

        /* 2) Claim all the slots assigned to our master. */
       // �����������ڵ㸺����Ĳ�  ��ѵ16384����λ����ǰ�ڵ�����ϵ����ڵ㸺��Ĳ�λ��
        for (j = 0; j < REDIS_CLUSTER_SLOTS; j++) {
            if (clusterNodeGetSlotBit(oldmaster,j)) {
                 // ��������Ϊδ�����               
                 clusterDelSlot(j);            
                 // ���۵ĸ���������Ϊ��ǰ�ڵ�
                clusterAddSlot(myself,j);
            }
        }

        /* 3) Update my configEpoch to the epoch of the election. */
        // ���¼�Ⱥ���ü�Ԫ  ���ڵ��ʱ������epoch���Ǽ�Ⱥ������configEpoch
        myself->configEpoch = server.cluster->failover_auth_epoch;

        /* 4) Update state and save config. */
        // ���½ڵ�״̬       
        clusterUpdateState();     
        // �����������ļ�
        clusterSaveConfigOrDie(1);

        //���һ����master������2��savle�����master���ˣ�ͨ��ѡ��slave1��ѡΪ�µ�������slave2ͨ�������������������ӵ���������slave1����clusterUpdateSlotsConfigWith
        /* 5) Pong all the other nodes so that they can update the state
         *    accordingly and detect that we switched to master role. */
        // �����нڵ㷢�� PONG ��Ϣ      
        // �����ǿ���֪����ǰ�ڵ��Ѿ�����Ϊ���ڵ���      
        clusterBroadcastPong(CLUSTER_BROADCAST_ALL);  //��������������������ͬһ��master��slave�ڵ㣬���ӵ������ѡ�ٳ���master������clusterUpdateSlotsConfigWith   
        /* 6) If there was a manual failover in progress, clear the state. */      

        // ������ֶ�����ת������ִ�У���ô��������йص�״̬
        resetManualFailover();
    } else {
        //˵��û�л���㹻��Ʊ������ӡ:Waiting for votes, but majority still not reached.
        clusterLogCantFailover(REDIS_CLUSTER_CANT_FAILOVER_WAITING_VOTES); //����6�����ڵ㣬����ֻ��1�����ڵ�ͶƱauth ack�����ˣ�����ӡ���
    }
}

/* -----------------------------------------------------------------------------
 * CLUSTER slave migration
 *
 * Slave migration is the process that allows a slave of a master that is
 * already covered by at least another slave, to "migrate" to a master that
 * is orpaned, that is, left with no working slaves.
 * -------------------------------------------------------------------------- */

/* This function is responsible to decide if this replica should be migrated
 * to a different (orphaned) master. It is called by the clusterCron() function
 * only if:
 *
 * 1) We are a slave node.
 * 2) It was detected that there is at least one orphaned master in
 *    the cluster.
 * 3) We are a slave of one of the masters with the greatest number of
 *    slaves.
 *
 * This checks are performed by the caller since it requires to iterate
 * the nodes anyway, so we spend time into clusterHandleSlaveMigration()
 * if definitely needed.
 *
 * The fuction is called with a pre-computed max_slaves, that is the max
 * number of working (not in FAIL state) slaves for a single master.
 *
 * Additional conditions for migration are examined inside the function.
 */
/*
��ѵ�����нڵ�֮��������ڹ������ڵ㣬����max_slaves���ڵ���2�����ҵ�ǰ�ڵ�պ����Ǹ�ӵ�����
δ���ߴӽڵ�����ڵ���ڶ�ӽڵ�֮һ������ú���clusterHandleSlaveMigration����������������£���
�дӽڵ�Ǩ�ƣ�Ҳ���ǽ���ǰ�ӽڵ���Ϊĳ�������ڵ�Ĵӽڵ㡣
*/
void clusterHandleSlaveMigration(int max_slaves) {
    int j, okslaves = 0;
    clusterNode *mymaster = myself->slaveof, *target = NULL, *candidate = NULL;
    dictIterator *di;
    dictEntry *de;

    /* Step 1: Don't migrate if the cluster state is not ok. */
    if (server.cluster->state != REDIS_CLUSTER_OK) return;

    /* Step 2: Don't migrate if my master will not be left with at least
     *         'migration-barrier' slaves after my migration. */
    if (mymaster == NULL) return;
    for (j = 0; j < mymaster->numslaves; j++)
        if (!nodeFailed(mymaster->slaves[j]) &&
            !nodeTimedOut(mymaster->slaves[j])) okslaves++;
    if (okslaves <= server.cluster_migration_barrier) return;

    /* Step 3: Idenitfy a candidate for migration, and check if among the
     * masters with the greatest number of ok slaves, I'm the one with the
     * smaller node ID.
     *
     * Note that this means that eventually a replica migration will occurr
     * since slaves that are reachable again always have their FAIL flag
     * cleared. At the same time this does not mean that there are no
     * race conditions possible (two slaves migrating at the same time), but
     * this is extremely unlikely to happen, and harmless. */
    candidate = myself;
    di = dictGetSafeIterator(server.cluster->nodes);
    while((de = dictNext(di)) != NULL) {
        clusterNode *node = dictGetVal(de);
        int okslaves;

        /* Only iterate over working masters. */
        if (nodeIsSlave(node) || nodeFailed(node)) continue;
        okslaves = clusterCountNonFailingSlaves(node);

        if (okslaves == 0 && target == NULL && node->numslots > 0)
            target = node;

        if (okslaves == max_slaves) {
            for (j = 0; j < node->numslaves; j++) {
                if (memcmp(node->slaves[j]->name,
                           candidate->name,
                           REDIS_CLUSTER_NAMELEN) < 0)
                {
                    candidate = node->slaves[j];
                }
            }
        }
    }

    /* Step 4: perform the migration if there is a target, and if I'm the
     * candidate. */
    if (target && candidate == myself) {
        redisLog(REDIS_WARNING,"Migrating to orphaned master %.40s",
            target->name);
        clusterSetMaster(target);
    }
}

/* -----------------------------------------------------------------------------
 * CLUSTER manual failover
 *
 * This are the important steps performed by slaves during a manual failover:
 * 1) User send CLUSTER FAILOVER command. The failover state is initialized
 *    setting mf_end to the millisecond unix time at which we'll abort the
 *    attempt.
 * 2) Slave sends a MFSTART message to the master requesting to pause clients
 *    for two times the manual failover timeout REDIS_CLUSTER_MF_TIMEOUT.
 *    When master is paused for manual failover, it also starts to flag
 *    packets with CLUSTERMSG_FLAG0_PAUSED.
 * 3) Slave waits for master to send its replication offset flagged as PAUSED.
 * 4) If slave received the offset from the master, and its offset matches,
 *    mf_can_start is set to 1, and clusterHandleSlaveFailover() will perform
 *    the failover as usually, with the difference that the vote request
 *    will be modified to force masters to vote for a slave that has a
 *    working master.
 *
 * From the point of view of the master things are simpler: when a
 * PAUSE_CLIENTS packet is received the master sets mf_end as well and
 * the sender in mf_slave. During the time limit for the manual failover
 * the master will just send PINGs more often to this slave, flagged with
 * the PAUSED flag, so that the slave will set mf_master_offset when receiving
 * a packet from the master with this flag set.
 *
 * The gaol of the manual failover is to perform a fast failover without
 * data loss due to the asynchronous master-slave replication.
 * -------------------------------------------------------------------------- */

/* Reset the manual failover state. This works for both masters and slavesa
 * as all the state about manual failover is cleared.
 *
 * �������ֶ�����ת���йص�״̬�����ڵ�ʹӽڵ㶼����ʹ�á� 
 * 
 * The function can be used both to initialize the manual failover state at 
 * startup or to abort a manual failover in progress. 
 * ��������ȿ���������������Ⱥʱ���г�ʼ���� 
 * �ֿ���ʵ�ʵ�Ӧ�����ֶ�����ת�Ƶ������
 */
void resetManualFailover(void) {
    if (server.cluster->mf_end && clientsArePaused()) {
        server.clients_pause_end_time = 0;
        clientsArePaused(); /* Just use the side effect of the function. */
    }
    server.cluster->mf_end = 0; /* No manual failover in progress. */
    server.cluster->mf_can_start = 0;
    server.cluster->mf_slave = NULL;
    server.cluster->mf_master_offset = 0;
}

/* If a manual failover timed out, abort it. */
void manualFailoverCheckTimeout(void) {
    if (server.cluster->mf_end && server.cluster->mf_end < mstime()) {
        redisLog(REDIS_WARNING,"Manual failover timed out.");
        resetManualFailover();
    }
}

/* This function is called from the cluster cron function in order to go
 * forward with a manual failover state machine. */
void clusterHandleManualFailover(void) {
    /* Return ASAP if no manual failover is in progress. */
    if (server.cluster->mf_end == 0) return;

    /* If mf_can_start is non-zero, the failover was alrady triggered so the
     * next steps are performed by clusterHandleSlaveFailover(). */
    if (server.cluster->mf_can_start) return;

    if (server.cluster->mf_master_offset == 0) return; /* Wait for offset... */

    if (server.cluster->mf_master_offset == replicationGetSlaveOffset()) { 
//�ӻ�ȡ������ȫ�������ˣ���ӿ��Խ���auth reqҪ������master���Լ�ͶƱ�ˣ�ע����ʱ��ӵ��������ߵģ�
//�����Ҫauth req���Ĵ���CLUSTERMSG_FLAG0_FORCEACK��ʶ����������master����ͶƱ����clusterSendFailoverAuthIfNeeded
        /* Our replication offset matches the master replication offset
         * announced after clients were paused. We can start the failover. */
        server.cluster->mf_can_start = 1;
        redisLog(REDIS_WARNING,
            "All master replication stream processed, "
            "manual failover can start.");
    }
}

/* -----------------------------------------------------------------------------
 * CLUSTER cron job
 * -------------------------------------------------------------------------- */

/* This is executed 10 times every second */
// ��Ⱥ�������������Ĭ��ÿ��ִ�� 10 �Σ�ÿ��� 100 ����ִ��һ�Σ�
void clusterCron(void) { //cluster�ڵ�֮������Ҫ��������������ΪclusterProcessPacket��clusterCron
    dictIterator *di;
    dictEntry *de;
    int update_state = 0;
    //û�йҴӽڵ�����ڵ����
    int orphaned_masters; /* How many masters there are without ok slaves. */
    //�������ڵ�����ӽڵ������Ƕ��ٸ��ӽڵ�
    int max_slaves; /* Max number of ok slaves for a single master. */
    //����Ϊ�ӽڵ㣬���ӽڵ��Ӧ�����ڵ������ж��ٸ��ӽڵ�
    int this_slaves; /* Number of ok slaves for our master (if we are slave). */
    mstime_t min_pong = 0, now = mstime();
    clusterNode *min_pong_node = NULL;
     // ������������һ����̬����
    static unsigned long long iteration = 0;
    mstime_t handshake_timeout;

    // ��¼һ�ε���
    iteration++; /* Number of times this function was called so far. */

    /* The handshake timeout is the time after which a handshake node that was
     * not turned into a normal node is removed from the nodes. Usually it is
     * just the NODE_TIMEOUT value, but when NODE_TIMEOUT is too small we use
     * the value of 1 second. */
   // ���һ�� handshake �ڵ�û���� handshake timeout ��  
   // ת������ͨ�ڵ㣨normal node����    
   // ��ô�ڵ��� nodes �����Ƴ���� handshake �ڵ�    
   // һ����˵ handshake timeout ��ֵ���ǵ��� NODE_TIMEOUT   
   // ������� NODE_TIMEOUT ̫�ٵĻ�������Ὣֵ��Ϊ 1 ����
    handshake_timeout = server.cluster_node_timeout;
    if (handshake_timeout < 1000) handshake_timeout = 1000;

    /* Check if we have disconnected nodes and re-establish the connection. */
    // ��Ⱥ�е����ж��߻���δ���ӽڵ㷢����Ϣ
    di = dictGetSafeIterator(server.cluster->nodes);
    while((de = dictNext(di)) != NULL) { //��ͼ�Ⱥ������δ�������ӵĽڵ����connect������meet����ping���Ӷ��������ӣ������е�һ�ν���
        clusterNode *node = dictGetVal(de); //node��������������MEET��һ�˴����ڵ㣬���߱������ն˷��ֱ���û�и�sender��Ϣ�򴴽�����createClusterNode  

        // ������ǰ�ڵ��Լ�û�е�ַ�Ľڵ�
        if (node->flags & (REDIS_NODE_MYSELF|REDIS_NODE_NOADDR)) continue;

        /* A Node in HANDSHAKE state has a limited lifespan equal to the
         * configured node timeout. */
        /*
        A cluster meet B��ʱ����clusterCommand�ᴴ��B��node��B-node->link=null(״̬ΪREDIS_NODE_HANDSHAKE)��Ȼ����clusterCrone�з���node->linkΪNULL
        Ҳ���ǻ�û�к�B-node��������ͬʱ����B-link�����Ƿ���B-node���ӣ�����B-node�����B-link��������B-node->link=B-link��
        ���A����Bһֱ���Ӳ��ϣ���ʱ���B�������������½���B-node,����Ҫ����ִ��A cluster meet B����
        */
        // ��� handshake �ڵ��ѳ�ʱ���ͷ���      
        if (nodeInHandshake(node) && now - node->ctime > handshake_timeout) {
            freeClusterNode(node);
            continue;
        }

    /*  clusterNode��clusterLink ��ϵͼ
    A����B������MEET��A�ᴴ��B��clusterNode-B�����Ҵ���B��link1����clusterNode-B��link1��clusterCron�н�����ϵ
    B�յ�meet����clusterAcceptHandler�д���link2����clusterProcessPacket�д���B��clusterNode-A,������ʱ���link2��clusterNode-Aû�н�����ϵ
    ������B��clusterCron�з���clusterNode-A��linkΪNULL������B��ʼ��A�������ӣ��Ӷ�����link3������PING,����clusterNode2��link3������A�յ�
    B���͵���������󣬴����µ�link4,���ն�Ӧ��ϵ��:
    
    A�ڵ�                   B�ڵ�
    clusterNode-B(link1) --->    link2(��link�������κ�clusterNode)     (A����meet��B)                                               ����1
    link4      <----         clusterNode-A(link3) (��link�������κ�clusterNode)  (B�յ�meet������һ��clustercron����A��������)     ����2
    */  

        //A meet B��ʱ��linkΪNULL��B���յ�MEET��Ϣ�󣬻ᴴ��clusterNode����ʱ���clusterNode.link=NULL���������������һ��if������������
        //���߼�Ⱥ��ĳ���ڵ�ҵ��ˣ���������ﷴ������������ҵ��Ľڵ㣬�ȴ����ٴ����ӵ���Ⱥ
        // Ϊδ�������ӵĽڵ㴴�����ӣ������κ�һ���ڵ㶼��ͼ�Ⱥ��ÿһ��clusterNode��������
        if (node->link == NULL) { //�Զ˽ڵ�clusterNode.link�����ﴴ���͸�ֵ   
            /* 
            ���������������������һ����A�ڵ�meet cluster B�ڵ㣬A�ڵ�ᴴ��B�ڵ��clusterNode��link��Ϣ������clusterNode.link=link
            */
            
            int fd;
            mstime_t old_ping_sent;
            clusterLink *link;

            fd = anetTcpNonBlockBindConnect(server.neterr, node->ip,
                node->port+REDIS_CLUSTER_PORT_INCR,
                    server.bindaddr_count ? server.bindaddr[0] : NULL);
            if (fd == -1) {
                redisLog(REDIS_DEBUG, "Unable to connect to "
                    "Cluster Node [%s]:%d -> %s", node->ip,
                    node->port+REDIS_CLUSTER_PORT_INCR,
                    server.neterr);
                continue;
            }

            //�ͻ��������˷���meet�󣬿ͻ���ͨ���ͷ���˽�����������¼����˽ڵ�clusterNode->link��clusterCron
            //����˽��յ����Ӻ�ͨ��clusterAcceptHandler�����ͻ��˽ڵ��clusterNode.link����clusterAcceptHandler
            link = createClusterLink(node);  
            link->fd = fd;
            node->link = link;

            //Aͨ��cluster meet bip bport  B��B����clusterAcceptHandler->clusterReadHandler�������ӣ�A��ͨ��
            //clusterCommand->clusterStartHandshake����clusterCron->anetTcpNonBlockBindConnect���ӷ�����
            aeCreateFileEvent(server.el,link->fd,AE_READABLE,
                    clusterReadHandler,link);
            /* Queue a PING in the new connection ASAP: this is crucial
             * to avoid false positives in failure detection.
             *
             * If the node is flagged as MEET, we send a MEET message instead
             * of a PING one, to force the receiver to add us in its node
             * table. */
             // �������ӵĽڵ㷢�� PING �����ֹ�ڵ㱻ʶ��������         
             // ����ڵ㱻���Ϊ MEET ����ô���� MEET ��������� PING ����
            old_ping_sent = node->ping_sent;
            //������������meet��Ϣ���Զ�node�ڵ�ĵط���clusterStartHandshake
            clusterSendPing(link, node->flags & REDIS_NODE_MEET ?
                    CLUSTERMSG_TYPE_MEET : CLUSTERMSG_TYPE_PING); 

           // �ⲻ�ǵ�һ�η��� PING ��Ϣ�����Կ��Ի�ԭ���ʱ��      
           // �� clusterSendPing() ������������
            if (old_ping_sent) { //Ҳ�����ڱ��η���ping֮ǰ����һ�η���ping��ʱ��
                /* If there was an active ping before the link was
                 * disconnected, we want to restore the ping time, otherwise
                 * replaced by the clusterSendPing() call. */
                node->ping_sent = old_ping_sent;
            }

            /* We can clear the flag after the first packet is sent.
             *
             * �ڷ��� MEET ��Ϣ֮������ڵ�� MEET ��ʶ��
             *
             * If we'll never receive a PONG, we'll never send new packets
             * to this node. Instead after the PONG is received and we
             * are no longer in meet/handshake status, we want to send
             * normal PING packets. 
             *
             *  �����ǰ�ڵ㣨�����ߣ�û���յ� MEET ��Ϣ�Ļظ���      
             * ��ô����������Ŀ��ڵ㷢�����         
             *          
             * ������յ��ظ��Ļ�����ô�ڵ㽫���ٴ��� HANDSHAKE ״̬��     
             * ��������Ŀ��ڵ㷢����ͨ PING ���
             */
            node->flags &= ~REDIS_NODE_MEET;

            redisLog(REDIS_DEBUG,"Connecting with Node %.40s at %s:%d",
                    node->name, node->ip, node->port+REDIS_CLUSTER_PORT_INCR);
        }
    }
    dictReleaseIterator(di);

    /* Ping some random node 1 time every 10 iterations, so that we usually ping
     * one random node every second. */
   // clusterCron() ÿִ�� 10 �Σ����ټ��һ���ӣ�������һ������ڵ㷢�� gossip ��Ϣ

   //ǰ���if�Ѿ��ͼ�Ⱥ���������ӣ������if����ÿ��1s����һ��ping����
   /*
    Ĭ��ÿ��1s������֪�ڵ��б������ѡ��5���ڵ㣬Ȼ�����5���ӵ����ʱ��û�з��͹�PING��Ϣ�Ľڵ㷢��PING
    ��Ϣ���Դ�����ⱻѡ�еĽڵ��Ƿ����ߡ�
   */
    if (!(iteration % 10)) { //Ҳ����ÿ���Ӹ�if����һ��
        int j;

        /* Check a few random nodes and ping the one with the oldest
         * pong_received time. */
        // ��� 5 ���ڵ㣬ѡ������һ��
        for (j = 0; j < 5; j++) { 
        //���������ȡ��Ҫ��ĳ���ڵ�һֱûȡ�������Ǻ͸�node�ڵ�ʧȥ��ϵ��??? ���Ժ����ͨ��server.cluster_node_timeout/2ʱ���û�з��͹�ping�ˣ�
        //��Ҫ�ٴη���ping���жϽڵ�״̬����������Ҳ���Կ���cluster_node_timeout����ԼС�ڵ�״̬���Խ��

            // ����ڼ�Ⱥ����ѡ�ڵ�
            de = dictGetRandomKey(server.cluster->nodes);
            clusterNode *this = dictGetVal(de);

            /* Don't ping nodes disconnected or with a ping currently active. */
              // ��Ҫ PING ���ӶϿ��Ľڵ㣬Ҳ��Ҫ PING ����Ѿ� PING ���Ľڵ�
            if (this->link == NULL || this->ping_sent != 0) continue; 

            if (this->flags & (REDIS_NODE_MYSELF|REDIS_NODE_HANDSHAKE))
                continue;

             // ѡ�� 5 ������ڵ������һ�ν��� PONG �ظ�����������ɵĽڵ�
            if (min_pong_node == NULL || min_pong > this->pong_received) { 
            //��������Ⱥ�����ȡ��5��clusterNode��������5������ڵ���ѡ�����û�кͱ��ڵ�ظ�pong��Ϣ�Ľڵ�
                min_pong_node = this;
                min_pong = this->pong_received;
            }
        }

        //��������Ⱥ�����ȡ��5��clusterNode��������5������ڵ���ѡ�����û�кͱ��ڵ�ظ�pong��Ϣ�Ľڵ㣬������ڵ㷢��PING
         // �����û���յ� PONG �ظ��Ľڵ㷢�� PING ����
        if (min_pong_node) {
            redisLog(REDIS_DEBUG,"Pinging node %.40s", min_pong_node->name);
            clusterSendPing(min_pong_node->link, CLUSTERMSG_TYPE_PING);
        }
    }

    // �������нڵ㣬����Ƿ���Ҫ��ĳ���ڵ���Ϊ����
    /* Iterate nodes to check if we need to flag something as failing.
     * This loop is also responsible to:
     * 1) Check if there are orphaned masters (masters without non failing
     *    slaves).
     * 2) Count the max number of non failing slaves for a single master.
     * 3) Count the number of slaves for our master, if we are a slave. */
    orphaned_masters = 0;
    max_slaves = 0;
    this_slaves = 0;
    di = dictGetSafeIterator(server.cluster->nodes);
    while((de = dictNext(di)) != NULL) {
        clusterNode *node = dictGetVal(de);
        now = mstime(); /* Use an updated time at every iteration. */
        mstime_t delay;

       // �����ڵ㱾���޵�ַ�ڵ㡢HANDSHAKE ״̬�Ľڵ�
        if (node->flags &
            (REDIS_NODE_MYSELF|REDIS_NODE_NOADDR|REDIS_NODE_HANDSHAKE))
                continue;

        /* Orphaned master check, useful only if the current instance
         * is a slave that may migrate to another master. */
         //���ڵ��Ǵӽڵ㣬�Զ�node�Ǳ��ڵ����������node�ڵ������ж��ٸ��ӽڵ�
        if (nodeIsSlave(myself) && nodeIsMaster(node) && !nodeFailed(node)) {
            int okslaves = clusterCountNonFailingSlaves(node); //����node�ڵ��ж��ٸ��ӽڵ�

            //max_slavesΪ�������ڵ��дӽڵ������Ϊ����

            //����max slaves��Ϊ�����migrate��׼���ģ���migrate�й�
            if (okslaves == 0 && node->numslots > 0) orphaned_masters++;
            if (okslaves > max_slaves) max_slaves = okslaves;
            if (nodeIsSlave(myself) && myself->slaveof == node)
                this_slaves = okslaves;
        }

        /* If we are waiting for the PONG more than half the cluster
         * timeout, reconnect the link: maybe there is a connection
         * issue even if the node is alive. */
        // ����ȵ� PONG �����ʱ�䳬���� node timeout һ�������      
        // ��Ϊ���ܽڵ���Ȼ�����������ӿ����Ѿ���������
        if (node->link && /* is connected */
            now - node->link->ctime >
            server.cluster_node_timeout && /* was not already reconnected */
            node->ping_sent && /* we already sent a ping */
            node->pong_received < node->ping_sent && /* still waiting pong */
            /* and we are waiting for the pong more than timeout/2 */
            now - node->ping_sent > server.cluster_node_timeout/2) //�ҷ�����ping�����ǶԶ�node����server.cluster_node_timeout/2��û��Ӧ��
        {
            /* Disconnect the link, it will be reconnected automatically. */
            // �ͷ����ӣ��´� clusterCron() ���Զ���������Ϊ
            freeClusterLink(node->link); //��������link��ΪNULL���´��ٴν���ú����������ǰ���link=NULL,�Ӷ����½�������
        }

        /* If we have currently no active ping in this instance, and the
         * received PONG is older than half the cluster timeout, send
         * a new ping now, to ensure all the nodes are pinged without
         * a too big delay. */
        // ���Ŀǰû���� PING �ڵ�       
        // �����Ѿ��� node timeout һ���ʱ��û�дӽڵ������յ� PONG �ظ�    
        // ��ô��ڵ㷢��һ�� PING ��ȷ���ڵ����Ϣ����̫��   
        // ����Ϊһ���ֽڵ����һֱû�б�����У�
        if (node->link &&
            node->ping_sent == 0 &&
            (now - node->pong_received) > server.cluster_node_timeout/2) //���Ѿ�server.cluster_node_timeout/2��ô��ʱ��û��Է�����ping��
        {
            /*
            ����ڵ�A���һ���յ��ڵ�B���͵�PONG��Ϣ��ʱ����뵱ǰʱ���Ѿ������˽ڵ�A��cluster-node-timeout
            ѡ��ʱ����һ�룬��ô�ڵ�AҲ����ڵ�B����PING��Ϣ������Է�ֹ�ڵ�A��Ϊ��ʱ��û�����ѡ�нڵ�B��Ϊ
            PING��Ϣ�ķ��Ͷ�������¶Խڵ�B����Ϣ�����ͺ�
            */
            clusterSendPing(node->link, CLUSTERMSG_TYPE_PING);
            continue;
        }

        /* If we are a master and one of the slaves requested a manual
         * failover, ping it continuously. */
         // �������һ�����ڵ㣬������һ���ӷ�������������ֶ�����ת��     
         // ��ô��ӷ��������� PING ��
        if (server.cluster->mf_end &&
            nodeIsMaster(myself) &&
            server.cluster->mf_slave == node &&
            node->link)
        {
            clusterSendPing(node->link, CLUSTERMSG_TYPE_PING); //������������offsetЯ����ȥ����֤�ӽ���ȫ�������ݣ���֤���ݲ���
            continue;
        }

        /* Check only if we have an active ping for this instance. */
        // ���´���ֻ�ڽڵ㷢���� PING ����������ִ��
        if (node->ping_sent == 0) continue;

        //˵��������ping,���ǶԷ���û��pong
        
        /* Compute the delay of the PONG. Note that if we already received
         * the PONG, then node->ping_sent is zero, so can't reach this
         * code at all. */
        // ����ȴ� PONG �ظ���ʱ��        
        delay = now - node->ping_sent;      

        // �ȴ� PONG �ظ���ʱ������������ֵ����Ŀ��ڵ���Ϊ PFAIL ���������ߣ�
        if (delay > server.cluster_node_timeout) {
            /* Timeout reached. Set the node as possibly failing if it is
             * not already in this state. */
            if (!(node->flags & (REDIS_NODE_PFAIL|REDIS_NODE_FAIL))) {
                redisLog(REDIS_DEBUG,"*** NODE %.40s possibly failing",
                    node->name);
                 // ���������߱��
                node->flags |= REDIS_NODE_PFAIL;
                update_state = 1;
            }
        }
    }
    dictReleaseIterator(di);

    /* If we are a slave node but the replication is still turned off,
     * enable it if we know the address of our master and it appears to
     * be up. */
     // ����ӽڵ�û���ڸ������ڵ㣬��ô�Դӽڵ��������
    if (nodeIsSlave(myself) &&
        server.masterhost == NULL &&
        myself->slaveof &&
        nodeHasAddr(myself->slaveof))
    {
        replicationSetMaster(myself->slaveof->ip, myself->slaveof->port);
    }

    /* Abourt a manual failover if the timeout is reached. */
    manualFailoverCheckTimeout();

    if (nodeIsSlave(myself)) {
        clusterHandleManualFailover();
        clusterHandleSlaveFailover();
        /* If there are orphaned slaves, and we are a slave among the masters
         * with the max number of non-failing slaves, consider migrating to
         * the orphaned masters. Note that it does not make sense to try
         * a migration if there is no master with at least *two* working
         * slaves. */
        /*
        ��ѵ�����нڵ�֮��������ڹ������ڵ㣬����max_slaves���ڵ���2�����ҵ�ǰ�ڵ�պ����Ǹ�ӵ�����
        δ���ߴӽڵ�����ڵ���ڶ�ӽڵ�֮һ������ú���clusterHandleSlaveMigration����������������£���
        �дӽڵ�Ǩ�ƣ�Ҳ���ǽ���ǰ�ӽڵ���Ϊĳ�������ڵ�Ĵӽڵ㡣
        */
        if (orphaned_masters && max_slaves >= 2 && this_slaves == max_slaves)
            clusterHandleSlaveMigration(max_slaves);
    }

    // ���¼�Ⱥ״̬
    if (update_state || server.cluster->state == REDIS_CLUSTER_FAIL)
        clusterUpdateState();   
}

/* This function is called before the event handler returns to sleep for
 * events. It is useful to perform operations that must be done ASAP in
 * reaction to events fired but that are not safe to perform inside event
 * handlers, or to perform potentially expansive tasks that we need to do
 * a single time before replying to clients. 
 *
 * �ڽ����¸��¼�ѭ��ʱ���á� 
 * ������������¶�����Ҫ����ִ�У����ǲ�����ִ���ļ��¼��ڼ��������顣
 */ //clusterBeforeSleep:�ڽ����¸��¼�ѭ��ǰ��ִ��һЩ��Ⱥ��β����
void clusterBeforeSleep(void) { //��ֵ��clusterDoBeforeSleep��������Ч��clusterBeforeSleep

    /* Handle failover, this is needed when it is likely that there is already
     * the quorum from masters in order to react fast. */
     // ִ�й���Ǩ��
    if (server.cluster->todo_before_sleep & CLUSTER_TODO_HANDLE_FAILOVER) 
    //���յ�CLUSTERMSG_TYPE_FAILOVER_AUTH_ACK����Ҫ��������̣���clusterProcessPacket
        clusterHandleSlaveFailover();

    /* Update the cluster state. */
    // ���½ڵ��״̬
    if (server.cluster->todo_before_sleep & CLUSTER_TODO_UPDATE_STATE)
        clusterUpdateState();

    /* Save the config, possibly using fsync. */
     // ���� nodes.conf �����ļ�
    if (server.cluster->todo_before_sleep & CLUSTER_TODO_SAVE_CONFIG) {
        int fsync = server.cluster->todo_before_sleep &
                    CLUSTER_TODO_FSYNC_CONFIG;
        clusterSaveConfigOrDie(fsync);
    }

    /* Reset our flags (not strictly needed since every single function
     * called for flags set should be able to clear its flag). */
    server.cluster->todo_before_sleep = 0;
}

////clusterBeforeSleep:�ڽ����¸��¼�ѭ��ǰ����Ⱥ��Ҫ��������  
//clusterDoBeforeSleep:�ڵ��ڽ���һ���¼�ѭ��ʱҪ���Ĺ���

// �� todo_before_sleep ��ָ����ʶ
// ÿ����ʶ�����˽ڵ��ڽ���һ���¼�ѭ��ʱҪ���Ĺ���
void clusterDoBeforeSleep(int flags) { //��ֵ��clusterDoBeforeSleep��������Ч��clusterBeforeSleep
    server.cluster->todo_before_sleep |= flags;
}

/* -----------------------------------------------------------------------------
 * Slots management
 * -------------------------------------------------------------------------- */

/* Test bit 'pos' in a generic bitmap. Return 1 if the bit is set,
 * otherwise 0. */
    // ���λͼ bitmap �� pos λ���Ƿ��Ѿ�������
    // ���� 1 ��ʾ�ѱ����ã����� 0 ��ʾδ�����á�
int bitmapTestBit(unsigned char *bitmap, int pos) {
    off_t byte = pos/8;
    int bit = pos&7;
    return (bitmap[byte] & (1<<bit)) != 0;
}

/* Set the bit at position 'pos' in a bitmap. */
// ����λͼ bitmap �� pos λ�õ�ֵ
void bitmapSetBit(unsigned char *bitmap, int pos) {
    off_t byte = pos/8;
    int bit = pos&7;
    bitmap[byte] |= 1<<bit;
}

/* Clear the bit at position 'pos' in a bitmap. */
// ���λͼ bitmap �� pos λ�õ�ֵ
void bitmapClearBit(unsigned char *bitmap, int pos) {
    off_t byte = pos/8;
    int bit = pos&7;
    bitmap[byte] &= ~(1<<bit);
}

/* Set the slot bit and return the old value. */
// Ϊ�۶�����λ������ֵ�������ؾ�ֵ
int clusterNodeSetSlotBit(clusterNode *n, int slot) {
    int old = bitmapTestBit(n->slots,slot);
    bitmapSetBit(n->slots,slot);
    if (!old) n->numslots++;
    return old;
}

/* Clear the slot bit and return the old value. */
// ��ղ۶�����λ�������ؾ�ֵ
int clusterNodeClearSlotBit(clusterNode *n, int slot) {
    int old = bitmapTestBit(n->slots,slot);
    bitmapClearBit(n->slots,slot);
    if (old) n->numslots--;
    return old;
}

/* Return the slot bit from the cluster node structure. */
// ���ز۵Ķ�����λ��ֵ
int clusterNodeGetSlotBit(clusterNode *n, int slot) {
    return bitmapTestBit(n->slots,slot);
}

/* Add the specified slot to the list of slots that node 'n' will
 * serve. Return REDIS_OK if the operation ended with success.
 * If the slot is already assigned to another instance this is considered
 * an error and REDIS_ERR is returned. */
// ���� slot ��ӵ��ڵ� n ��Ҫ����Ĳ۵��б���
// ��ӳɹ����� REDIS_OK ,������Ѿ�������ڵ㴦����
// ��ô���� REDIS_ERR ��
int clusterAddSlot(clusterNode *n, int slot) {

     // �� slot �Ѿ��ǽڵ� n �������   
     if (server.cluster->slots[slot]) return REDIS_ERR;   

     // ���� bitmap   
     clusterNodeSetSlotBit(n,slot);  
     
     // ���¼�Ⱥ״̬
    server.cluster->slots[slot] = n;

    return REDIS_OK;
}

/* Delete the specified slot marking it as unassigned.
 *
 * ��ָ���۱��Ϊδ���䣨unassigned���� 
 * * Returns REDIS_OK if the slot was assigned, otherwise if the slot was
 * already unassigned REDIS_ERR is returned. 
 *
 * ��ǳɹ����� REDIS_OK �� 
 * ������Ѿ���δ����ģ���ô���� REDIS_ERR ��
 */
int clusterDelSlot(int slot) {

    // ��ȡ��ǰ����� slot �Ľڵ� n   
    clusterNode *n = server.cluster->slots[slot];   
    if (!n) return REDIS_ERR;    
    // ���λͼ    
    redisAssert(clusterNodeClearSlotBit(n,slot) == 1);   

    // ��ո�����۵Ľڵ�
    server.cluster->slots[slot] = NULL;

    return REDIS_OK;
}

/* Delete all the slots associated with the specified node.
 * The number of deleted slots is returned. */

// ɾ�������ɸ����ڵ㴦��Ĳۣ������ر�ɾ���۵�����
int clusterDelNodeSlots(clusterNode *node) {
    int deleted = 0, j;

    for (j = 0; j < REDIS_CLUSTER_SLOTS; j++) {
        // ���������ɸýڵ㸺����ôɾ����
        if (clusterNodeGetSlotBit(node,j)) clusterDelSlot(j);
        deleted++;
    }
    return deleted;
}

/* Clear the migrating / importing state for all the slots.
 * This is useful at initialization and when turning a master into slave. */
// �������в۵�Ǩ�ƺ͵���״̬// ͨ���ڳ�ʼ�����߽����ڵ�תΪ�ӽڵ�ʱʹ��
void clusterCloseAllSlots(void) {
    memset(server.cluster->migrating_slots_to,0,
        sizeof(server.cluster->migrating_slots_to));
    memset(server.cluster->importing_slots_from,0,
        sizeof(server.cluster->importing_slots_from));
}

/* -----------------------------------------------------------------------------
 * Cluster state evaluation function
 * -------------------------------------------------------------------------- */

/* The following are defines that are only used in the evaluation function
 * and are based on heuristics. Actaully the main point about the rejoin and
 * writable delay is that they should be a few orders of magnitude larger
 * than the network latency. */
#define REDIS_CLUSTER_MAX_REJOIN_DELAY 5000
#define REDIS_CLUSTER_MIN_REJOIN_DELAY 500
#define REDIS_CLUSTER_WRITABLE_DELAY 2000

// ���½ڵ�״̬
void clusterUpdateState(void) { //���±��ڵ�����������Ⱥ��״̬
    int j, new_state;
    int unreachable_masters = 0;
    static mstime_t among_minority_time;
    static mstime_t first_call_time = 0;

    server.cluster->todo_before_sleep &= ~CLUSTER_TODO_UPDATE_STATE;

    /* If this is a master node, wait some time before turning the state
     * into OK, since it is not a good idea to rejoin the cluster as a writable
     * master, after a reboot, without giving the cluster a chance to
     * reconfigure this node. Note that the delay is calculated starting from
     * the first call to this function and not since the server start, in order
     * to don't count the DB loading time. */
    if (first_call_time == 0) first_call_time = mstime();
    if (nodeIsMaster(myself) &&
        mstime() - first_call_time < REDIS_CLUSTER_WRITABLE_DELAY) return; //״̬������20ms�ӳ�

    /* Start assuming the state is OK. We'll turn it into FAIL if there
     * are the right conditions. */

    // �ȼ���ڵ�״̬Ϊ OK �������ټ��ڵ��Ƿ��������
    new_state = REDIS_CLUSTER_OK;

    /* Check if all the slots are covered. */
     // ����Ƿ����в۶��Ѿ���ĳ���ڵ��ڴ���
    for (j = 0; j < REDIS_CLUSTER_SLOTS; j++) {
        if (server.cluster->slots[j] == NULL ||
            server.cluster->slots[j]->flags & (REDIS_NODE_FAIL))
        {
            new_state = REDIS_CLUSTER_FAIL;
            break;
        }
    }

    /* Compute the cluster size, that is the number of master nodes
     * serving at least a single slot.
     *
     * At the same time count the number of unreachable masters with
     * at least one node. */
     // ͳ�����߲������ڴ�������һ���۵� master ��������    // �Լ����� master ������
    {
        dictIterator *di;
        dictEntry *de;

        server.cluster->size = 0;
        di = dictGetSafeIterator(server.cluster->nodes);
        while((de = dictNext(di)) != NULL) {
            clusterNode *node = dictGetVal(de);

            if (nodeIsMaster(node) && node->numslots) {
                server.cluster->size++; //���������ߵĽڵ�
                if (node->flags & (REDIS_NODE_FAIL|REDIS_NODE_PFAIL))
                    unreachable_masters++; //�쳣�ڵ������
            }
        }
        dictReleaseIterator(di);
    }

    /* If we can't reach at least half the masters, change the cluster state
     * to FAIL, as we are not even able to mark nodes as FAIL in this side
     * of the netsplit because of lack of majority.
     *
     * ����������ӵ��������Ͻڵ㣬��ô�������Լ���״̬����Ϊ FAIL     
     * ��Ϊ�����ڰ����ڵ������£��ڵ����޷���һ���ڵ��ж�Ϊ FAIL �ġ�
     */
    {
        int needed_quorum = (server.cluster->size / 2) + 1;

        if (unreachable_masters >= needed_quorum) { //������Ⱥ��һ���ĸ������λ�Ľڵ㶼�����ˣ����Ǽ�ȺΪFAIL
            new_state = REDIS_CLUSTER_FAIL;
            among_minority_time = mstime();
        }
    }

    /* Log a state change */
        // ��¼״̬���
    if (new_state != server.cluster->state) {
        mstime_t rejoin_delay = server.cluster_node_timeout;

        /* If the instance is a master and was partitioned away with the
         * minority, don't let it accept queries for some time after the
         * partition heals, to make sure there is enough time to receive
         * a configuration update. */
        if (rejoin_delay > REDIS_CLUSTER_MAX_REJOIN_DELAY)
            rejoin_delay = REDIS_CLUSTER_MAX_REJOIN_DELAY;
        if (rejoin_delay < REDIS_CLUSTER_MIN_REJOIN_DELAY)
            rejoin_delay = REDIS_CLUSTER_MIN_REJOIN_DELAY;

        if (new_state == REDIS_CLUSTER_OK &&
            nodeIsMaster(myself) &&
            mstime() - among_minority_time < rejoin_delay)
        {
            return;
        }

        /* Change the state and log the event. */
        redisLog(REDIS_WARNING,"Cluster state changed: %s",
            new_state == REDIS_CLUSTER_OK ? "ok" : "fail");

       // ������״̬
        server.cluster->state = new_state;
    }
}

/* This function is called after the node startup in order to verify that data
 * loaded from disk is in agreement with the cluster configuration:
 *
 * 1) If we find keys about hash slots we have no responsibility for, the
 *    following happens:
 *    A) If no other node is in charge according to the current cluster
 *       configuration, we add these slots to our node.
 *    B) If according to our config other nodes are already in charge for
 *       this lots, we set the slots as IMPORTING from our point of view
 *       in order to justify we have those slots, and in order to make
 *       redis-trib aware of the issue, so that it can try to fix it.
 * 2) If we find data in a DB different than DB0 we return REDIS_ERR to
 *    signal the caller it should quit the server with an error message
 *    or take other actions.
 *
 * The function always returns REDIS_OK even if it will try to correct
 * the error described in "1". However if data is found in DB different
 * from DB0, REDIS_ERR is returned.
 *
 * The function also uses the logging facility in order to warn the user
 * about desynchronizations between the data we have in memory and the
 * cluster configuration. */
    // ��鵱ǰ�ڵ�Ľڵ������Ƿ���ȷ�������������Ƿ���ȷ// ��������Ⱥʱ�����ã��� redis.c ��
int verifyClusterConfigWithData(void) {
    int j;
    int update_config = 0;

    /* If this node is a slave, don't perform the check at all as we
     * completely depend on the replication stream. */
 // ���Դӽڵ���м��   
    if (nodeIsSlave(myself)) return REDIS_OK;  

    /* Make sure we only have keys in DB0. */    
    // ȷ��ֻ�� 0 �����ݿ�������
    for (j = 1; j < server.dbnum; j++) { //��Ⱥ����ֻ����0�����ݿ��������ݵ�ʱ�����Ч������������ݿ���Ҳ����������Ч
        if (dictSize(server.db[j].dict)) return REDIS_ERR;
    }

    /* Check that all the slots we see populated memory have a corresponding
     * entry in the cluster table. Otherwise fix the table. */
    // ���۱��Ƿ�����Ӧ�Ľڵ㣬������ǵĻ��������޸�
    for (j = 0; j < REDIS_CLUSTER_SLOTS; j++) {
        if (!countKeysInSlot(j)) continue; /* No keys in this slot. */
        /* Check if we are assigned to this slot or if we are importing it.
         * In both cases check the next slot as the configuration makes
         * sense. */
        // �������ڵ���Ĳ�
        if (server.cluster->slots[j] == myself ||
            server.cluster->importing_slots_from[j] != NULL) continue;

        /* If we are here data and cluster config don't agree, and we have
         * slot 'j' populated even if we are not importing it, nor we are
         * assigned to this slot. Fix this condition. */

        update_config++;
        /* Case A: slot is unassigned. Take responsability for it. */
        if (server.cluster->slots[j] == NULL) {
             // ����δ�����ܵĲ�
            redisLog(REDIS_WARNING, "I've keys about slot %d that is "
                                    "unassigned. Taking responsability "
                                    "for it.",j);
                                    
 //ע���ڴ�rdb�ļ�����aof�ļ��ж�ȡ��key-value�Ե�ʱ����������˼�Ⱥ���ܻ���dbAdd->slotToKeyAdd(key);
 //�а�key��slot�Ķ�Ӧ��ϵ��ӵ�slots_to_keys������verifyClusterConfigWithData->clusterAddSlot�дӶ�ָ�ɶ�Ӧ��slot��
 //Ҳ���Ǳ��������е�rdb�е�key-value��Ӧ��slot�������������
            clusterAddSlot(myself,j);
        } else {
            // ���һ�����Ѿ��������ڵ�ӹ�            // ��ô�����е����Ϸ��͸��Է�
            redisLog(REDIS_WARNING, "I've keys about slot %d that is "
                                    "already assigned to a different node. "
                                    "Setting it in importing state.",j);
            server.cluster->importing_slots_from[j] = server.cluster->slots[j];
        }
    }

     // ���� nodes.conf �ļ�
    if (update_config) clusterSaveConfigOrDie(1);

    return REDIS_OK;
}

/* -----------------------------------------------------------------------------
 * SLAVE nodes handling
 * -------------------------------------------------------------------------- */

/* Set the specified node 'n' as master for this node.
 * If this node is currently a master, it is turned into a slave. */

//slavof����ͬ������:(slaveof ip port����)slaveofCommand->replicationSetMaster  (cluster replicate����)clusterCommand->clusterSetMaster->replicationSetMaster 
//��Ⱥ����ѡ�ٺ�����ͬ������:��������server.repl_state = REDIS_REPL_CONNECT���Ӷ�����connectWithMaster����һ������slaveTryPartialResynchronization����psyn��������ͬ��
//CLUSTER REPLICATE <node_id> ����ǰ�ڵ�����Ϊ node_id ָ���Ľڵ�Ĵӽڵ㡣

// ���ڵ� n ����Ϊ��ǰ�ڵ�����ڵ�// �����ǰ�ڵ�Ϊ���ڵ㣬��ô����ת��Ϊ�ӽڵ�
void clusterSetMaster(clusterNode *n) { //�ڵ�����Ĭ��Ϊmaster�ģ���ִ��
    redisAssert(n != myself);
    redisAssert(myself->numslots == 0);

    if (nodeIsMaster(myself)) {
        myself->flags &= ~REDIS_NODE_MASTER;
        myself->flags |= REDIS_NODE_SLAVE;
        clusterCloseAllSlots();
    } else {
        if (myself->slaveof)
            clusterNodeRemoveSlave(myself->slaveof,myself);
    }

    // �� slaveof ����ָ�����ڵ�   
    myself->slaveof = n;    

    // �������ڵ�� IP �͵�ַ����ʼ�������и���
    clusterNodeAddSlave(n,myself);
    replicationSetMaster(n->ip, n->port);
    resetManualFailover();
}

/* -----------------------------------------------------------------------------
 * CLUSTER command
 * -------------------------------------------------------------------------- */
/*
[root@centlhw1 ~]# cat /usr/local/cluster/7000/nodes.conf 
fec9b202debce01bd96a4a8615e1a1792e1b9b61 127.0.0.1:7001 master - 0 1474338184213 0 connected 5462-10922
5735dc1c29b86bd96477da5bfc90a330b03188f9 127.0.0.1:7002 master - 0 1474338184718 2 connected 10923-16383
6132067dc2e287c092abba2565b8a3b2f89639ff :0 myself,master - 0 0 1 connected 0-5461
vars currentEpoch 2 lastVoteEpoch 0
*/

/* Generate a csv-alike representation of the specified cluster node.
 * See clusterGenNodesDescription() top comment for more information.
 *
 * The function returns the string representation as an SDS string. */
    // ���ɽڵ��״̬������Ϣ
sds clusterGenNodeDescription(clusterNode *node) { //��Ⱥ�е�ÿ��cluster�ڵ��״̬��Ϣ
    int j, start;
    sds ci;

    /* Node coordinates */
    ci = sdscatprintf(sdsempty(),"%.40s %s:%d ",
        node->name,
        node->ip,
        node->port);

    /* Flags */
    if (node->flags == 0) ci = sdscat(ci,"noflags,");
    if (node->flags & REDIS_NODE_MYSELF) ci = sdscat(ci,"myself,");
    if (node->flags & REDIS_NODE_MASTER) ci = sdscat(ci,"master,");
    if (node->flags & REDIS_NODE_SLAVE) ci = sdscat(ci,"slave,");
    if (node->flags & REDIS_NODE_PFAIL) ci = sdscat(ci,"fail?,");
    if (node->flags & REDIS_NODE_FAIL) ci = sdscat(ci,"fail,");
    if (node->flags & REDIS_NODE_HANDSHAKE) ci =sdscat(ci,"handshake,");
    if (node->flags & REDIS_NODE_NOADDR) ci = sdscat(ci,"noaddr,");
    if (ci[sdslen(ci)-1] == ',') ci[sdslen(ci)-1] = ' ';

    /* Slave of... or just "-" */
    if (node->slaveof)
        ci = sdscatprintf(ci,"slave of %.40s ",node->slaveof->name); //���ڵ�����
    else
        ci = sdscatprintf(ci,"- "); //����Լ������ڵ㣬��ֱ����ʾ-

    /* Latency from the POV of this node, link status */
    ci = sdscatprintf(ci,"%lld %lld %llu %s",
        (long long) node->ping_sent,
        (long long) node->pong_received,
        (unsigned long long) node->configEpoch,
        (node->link || node->flags & REDIS_NODE_MYSELF) ?
                    "connected" : "disconnected");

    /* Slots served by this instance */
    start = -1;
    for (j = 0; j < REDIS_CLUSTER_SLOTS; j++) {
        int bit;

        if ((bit = clusterNodeGetSlotBit(node,j)) != 0) {
            if (start == -1) start = j;
        }
        if (start != -1 && (!bit || j == REDIS_CLUSTER_SLOTS-1)) {
            if (bit && j == REDIS_CLUSTER_SLOTS-1) j++;

            if (start == j-1) {
                ci = sdscatprintf(ci," %d",start);
            } else {
                ci = sdscatprintf(ci," %d-%d",start,j-1);
            }
            start = -1;
        }
    }

    /* Just for MYSELF node we also dump info about slots that
     * we are migrating to other instances or importing from other
     * instances. */
    if (node->flags & REDIS_NODE_MYSELF) {
        for (j = 0; j < REDIS_CLUSTER_SLOTS; j++) {
            if (server.cluster->migrating_slots_to[j]) {
                ci = sdscatprintf(ci," [%d->-%.40s]",j,
                    server.cluster->migrating_slots_to[j]->name);
            } else if (server.cluster->importing_slots_from[j]) {
                ci = sdscatprintf(ci," [%d-<-%.40s]",j,
                    server.cluster->importing_slots_from[j]->name);
            }
        }
    }
    
    return ci;
}

/* Generate a csv-alike representation of the nodes we are aware of,
 * including the "myself" node, and return an SDS string containing the
 * representation (it is up to the caller to free it).
 *
 * �� csv ��ʽ��¼��ǰ�ڵ���֪���нڵ����Ϣ��������ǰ�ڵ�������
 * ��Щ��Ϣ�����浽һ�� sds ���棬����Ϊ����ֵ���ء�

 *
 * All the nodes matching at least one of the node flags specified in
 * "filter" are excluded from the output, so using zero as a filter will
 * include all the known nodes in the representation, including nodes in
 * the HANDSHAKE state.
 *
 * filter ������������ָ���ڵ�� flag ��ʶ��
 * ���б�ָ����ʶ�Ľڵ㲻�ᱻ��¼������ṹ�У� 
 * filter Ϊ 0 ��ʾ��¼���нڵ����Ϣ������ HANDSHAKE ״̬�Ľڵ㡣

 *
 * The representation obtained using this function is used for the output
 * of the CLUSTER NODES function, and as format for the cluster
 * configuration file (nodes.conf) for a given node. 
 *
 * ����������ɵĽ���ᱻ���� CLUSTER NODES ���
 * �Լ��������� nodes.conf �����ļ���

 */ 
 //��������node�ڵ��ȡ��Ӧ�Ľڵ�master slave����Ϣ������:4ae3f6e2ff456e6e397ea6708dac50a16807911c 192.168.1.103:7000 myself,slave dc824af0bff649bb292dbf5b37307a54ed4d361f 0 0 0 connected
//����ͨ��cluster nodes�����ȡ�����ջ�д��nodes.conf
sds clusterGenNodesDescription(int filter) {
    sds ci = sdsempty(), ni;
    dictIterator *di;
    dictEntry *de;

   // ������Ⱥ�е����нڵ�
    di = dictGetSafeIterator(server.cluster->nodes);
    while((de = dictNext(di)) != NULL) {
        clusterNode *node = dictGetVal(de);

         // ����ӡ����ָ�� flag �Ľڵ�
        if (node->flags & filter) continue;

        ni = clusterGenNodeDescription(node);
        ci = sdscatsds(ci,ni);
        sdsfree(ni);
        ci = sdscatlen(ci,"\n",1);
    }
    dictReleaseIterator(di);

    return ci;
}

// ȡ��һ�� slot ��ֵ
int getSlotOrReply(redisClient *c, robj *o) {
    long long slot;

    if (getLongLongFromObject(o,&slot) != REDIS_OK ||
        slot < 0 || slot >= REDIS_CLUSTER_SLOTS)
    {
        addReplyError(c,"Invalid or out of range slot");
        return -1;
    }
    return (int) slot;
}

/*
http://www.cnblogs.com/tankaixiong/articles/4022646.html

http://blog.csdn.net/dc_726/article/details/48552531

��Ⱥ  
CLUSTER INFO ��ӡ��Ⱥ����Ϣ  
CLUSTER NODES �г���Ⱥ��ǰ��֪�����нڵ㣨node�����Լ���Щ�ڵ�������Ϣ��  

�ڵ�  
CLUSTER MEET <ip> <port> �� ip �� port ��ָ���Ľڵ���ӵ���Ⱥ���У�������Ϊ��Ⱥ��һ���ӡ�  
CLUSTER FORGET <node_id> �Ӽ�Ⱥ���Ƴ� node_id ָ���Ľڵ㡣���Ҫ�Ӽ�Ⱥ���Ƴ��ýڵ㣬��Ҫ�ȼ�Ⱥ�е����нڵ㷢��cluster forget  
CLUSTER REPLICATE <node_id> ����ǰ�ڵ�����Ϊ node_id ָ���Ľڵ�Ĵӽڵ㡣   CLUSTER REPLICATE  ע���slaveof������slaveof��������cluster,��slaveofCommand
CLUSTER SAVECONFIG ���ڵ�������ļ����浽Ӳ�����档  
��(slot)  
CLUSTER ADDSLOTS <slot> [slot ...] ��һ�������ۣ�slot��ָ�ɣ�assign������ǰ�ڵ㡣  
CLUSTER DELSLOTS <slot> [slot ...] �Ƴ�һ�������۶Ե�ǰ�ڵ��ָ�ɡ�  
CLUSTER FLUSHSLOTS �Ƴ�ָ�ɸ���ǰ�ڵ�����вۣ��õ�ǰ�ڵ���һ��û��ָ���κβ۵Ľڵ㡣  
CLUSTER SLOTS  �鿴��λ�ֲ�

CLUSTER SETSLOT <slot> NODE <node_id> ���� slot ָ�ɸ� node_id ָ���Ľڵ㣬������Ѿ�ָ�ɸ���һ���ڵ㣬��ô������һ���ڵ�ɾ���ò�>��Ȼ���ٽ���ָ�ɡ�  
CLUSTER SETSLOT <slot> MIGRATING <node_id> �����ڵ�Ĳ� slot Ǩ�Ƶ� node_id ָ���Ľڵ��С�  
CLUSTER SETSLOT <slot> IMPORTING <node_id> �� node_id ָ���Ľڵ��е���� slot �����ڵ㡣  
CLUSTER SETSLOT <slot> STABLE ȡ���Բ� slot �ĵ��루import������Ǩ�ƣ�migrate����  
�����������MIGRATE host port key destination-db timeout replace���в�λresharding������Ǩ��

�ڵ�Ĳ�λ�仯��ͨ��PING PONG��Ⱥ�ڵ㽻���㲥�ģ���clusterMsg->myslots[]��Я����ȥ��Ҳ����ͨ��clusterSendUpdate(clusterMsgDataUpdate.slots)���ͳ�ȥ

�ο� redis�����ʵ�� ��17�� ��Ⱥ  17.4 ���·�Ƭ
redis-cli -c -h 192.168.1.100 -p 7000 cluster addslots {0..5000} ͨ��redis-cli����slot��Χ�����ǲ���redis-cli���������У�Ȼ����cluster addslots {0..5000}
127.0.0.1:7000> cluster addslots {0..5000}
(error) ERR Invalid or out of range slot
127.0.0.1:7000> quit
[root@s10-2-4-4 yazhou.yang]# redis-cli -c  -p 7000 cluster addslots {0..5000}  ����ʵ��������redis-cli�Ѹ÷�Χ�滻Ϊcluster addslots 0 1 2 3 .. 5000�����͸�redis��
OK
[root@s10-2-4-4 yazhou.yang]# 



��  
CLUSTER KEYSLOT <key> ����� key Ӧ�ñ��������ĸ����ϡ�  
CLUSTER COUNTKEYSINSLOT <slot> ���ز� slot Ŀǰ�����ļ�ֵ��������  
CLUSTER GETKEYSINSLOT <slot> <count> ���� count �� slot ���еļ���

//CLUSTER SLAVES <NODE ID> ��ӡ�������ڵ�����дӽڵ����Ϣ 
//���⣬Manual Failover��force�ͷ�force���������ڣ���force��Ҫ�ȴӽڵ���ȫͬ�������ڵ�����ݺ�Ž���failover����֤����ʧ���ݣ���������У�ԭ���ڵ�ֹͣд��������force�����н�����������ͬ����ֱ�ӽ���failover��
//CLUSTER FAILOVER [FORCE]  ִ���ֶ�����ת��       ֻ�ܷ���slave  
//cluster set-config-epoch <epoch>����ǿ������configEpoch
//CLUSTER RESET [SOFT|HARD]��Ⱥ��λ��������Ƚ�Σ��

��Ⱥ����:http://carlosfu.iteye.com/blog/2254573
*/

//CLUSTER �����ʵ��
void clusterCommand(redisClient *c) {   
// �����ڷǼ�Ⱥģʽ��ʹ�ø�����

    if (server.cluster_enabled == 0) {
        addReplyError(c,"This instance has cluster support disabled");
        return;
    }

    if (!strcasecmp(c->argv[1]->ptr,"meet") && c->argc == 4) {
        /* CLUSTER MEET <ip> <port> */
         // ��������ַ�Ľڵ���ӵ���ǰ�ڵ������ļ�Ⱥ����

        long long port;

        // ��� port �����ĺϷ���
        if (getLongLongFromObject(c->argv[3], &port) != REDIS_OK) {
            addReplyErrorFormat(c,"Invalid TCP port specified: %s",
                                (char*)c->argv[3]->ptr);
            return;
        }

        //Aͨ��cluster meet bip bport  B��B����clusterAcceptHandler�������ӣ�A��ͨ��clusterCommand->clusterStartHandshake���ӷ�����
        // �����������ַ�Ľڵ��������
        if (clusterStartHandshake(c->argv[2]->ptr,port) == 0 &&
            errno == EINVAL)
        {
             // ����ʧ��
            addReplyErrorFormat(c,"Invalid node address specified: %s:%s",
                            (char*)c->argv[2]->ptr, (char*)c->argv[3]->ptr);
        } else {
             // ���ӳɹ�
            addReply(c,shared.ok);
        }

    } else if (!strcasecmp(c->argv[1]->ptr,"nodes") && c->argc == 2) {
        /* CLUSTER NODES */
        // �г���Ⱥ���нڵ����Ϣ
        robj *o;
        sds ci = clusterGenNodesDescription(0);

        o = createObject(REDIS_STRING,ci);
        addReplyBulk(c,o);
        decrRefCount(o);

    } else if (!strcasecmp(c->argv[1]->ptr,"flushslots") && c->argc == 2) {
        /* CLUSTER FLUSHSLOTS */
         // ɾ����ǰ�ڵ�����вۣ�������Ϊ�������κβ�        
         // ɾ���۱��������ݿ�Ϊ�յ�����½���       

         if (dictSize(server.db[0].dict) != 0) {       
         addReplyError(c,"DB must be empty to perform CLUSTER FLUSHSLOTS.");      
         return;        
         }        
         // ɾ�������ɸýڵ㴦��Ĳ�
        clusterDelNodeSlots(myself);
        clusterDoBeforeSleep(CLUSTER_TODO_UPDATE_STATE|CLUSTER_TODO_SAVE_CONFIG);
        addReply(c,shared.ok);

    } else if ((!strcasecmp(c->argv[1]->ptr,"addslots") ||
               !strcasecmp(c->argv[1]->ptr,"delslots")) && c->argc >= 3)
    {
        /* CLUSTER ADDSLOTS <slot> [slot] ... */
         // ��һ������ slot ��ӵ���ǰ�ڵ�     
         /* CLUSTER DELSLOTS <slot> [slot] ... */      
         // �ӵ�ǰ�ڵ���ɾ��һ������ slot     

        /*
        redis-cli -c -h 192.168.1.100 -p 7000 cluster addslots {0..5000} ͨ��redis-cli����slot��Χ�����ǲ���redis-cli���������У�Ȼ����cluster addslots {0..5000}
        127.0.0.1:7000> cluster addslots {0..5000}
        (error) ERR Invalid or out of range slot
        127.0.0.1:7000> quit
        [root@s10-2-4-4 yazhou.yang]# redis-cli -c  -p 7000 cluster addslots {0..5000}  ����ʵ��������redis-cli�Ѹ÷�Χ�滻Ϊcluster addslots 0 1 2 3 .. 5000�����͸�redis��
        OK
        [root@s10-2-4-4 yazhou.yang]# 
        */
         
        int j, slot;

        // һ�����飬��¼����Ҫ��ӻ���ɾ���Ĳ�
        unsigned char *slots = zmalloc(REDIS_CLUSTER_SLOTS);

        // ������� delslots ���� addslots
        int del = !strcasecmp(c->argv[1]->ptr,"delslots");

         // �� slots ���������ֵ����Ϊ 0
        memset(slots,0,REDIS_CLUSTER_SLOTS);

        /* Check that all the arguments are parsable and that all the
         * slots are not already busy. */
         // ������������ slot ����
        for (j = 2; j < c->argc; j++) {

             // ��ȡ slot ����
            if ((slot = getSlotOrReply(c,c->argv[j])) == -1) {
                zfree(slots);
                return;
            }

            // ������� delslots �������ָ����Ϊδָ������ô����һ������
            if (del && server.cluster->slots[slot] == NULL) {
                addReplyErrorFormat(c,"Slot %d is already unassigned", slot);
                zfree(slots);
                return;
             // ������� addslots ������Ҳ��Ѿ��нڵ��ڸ�����ô����һ������
            } else if (!del && server.cluster->slots[slot]) {
                addReplyErrorFormat(c,"Slot %d is already busy", slot);
                zfree(slots);
                return;
            }

             // ���ĳ����ָ����һ�����ϣ���ô����һ������
            if (slots[slot]++ == 1) {
                addReplyErrorFormat(c,"Slot %d specified multiple times",
                    (int)slot);
                zfree(slots);
                return;
            }
        }

         // ������������ slot
        for (j = 0; j < REDIS_CLUSTER_SLOTS; j++) {
            if (slots[j]) {
                int retval;

                /* If this slot was set as importing we can clear this 
                 * state as now we are the real owner of the slot. */
                // ���ָ�� slot ֮ǰ��״̬Ϊ����״̬����ô���ڿ��������һ״̬          
                // ��Ϊ��ǰ�ڵ������Ѿ��� slot �ĸ�������
                if (server.cluster->importing_slots_from[j])
                    server.cluster->importing_slots_from[j] = NULL;

                 // ��ӻ���ɾ��ָ�� slot
                retval = del ? clusterDelSlot(j) :
                               clusterAddSlot(myself,j);
                redisAssertWithInfo(c,NULL,retval == REDIS_OK);
            }
        }
        zfree(slots);
        clusterDoBeforeSleep(CLUSTER_TODO_UPDATE_STATE|CLUSTER_TODO_SAVE_CONFIG);
        addReply(c,shared.ok);

    } else if (!strcasecmp(c->argv[1]->ptr,"setslot") && c->argc >= 4) {
        /* SETSLOT 10 MIGRATING <node ID> */
        /* SETSLOT 10 IMPORTING <node ID> */
        /* SETSLOT 10 STABLE */
        /* SETSLOT 10 NODE <node ID> */

        /*
        //ע��:CLUSTER SETSLOT <slot> NODE <node_id>��Ҫ���͸�Ǩ�Ƶ���Ŀ�Ľڵ㣬Ȼ��ͨ��gossipЭ��㲥��ȥ��������͸������ڵ㣬��
        CLUSTER SETSLOT <slot> NODE <node_id> ���� slot ָ�ɸ� node_id ָ���Ľڵ㣬������Ѿ�ָ�ɸ���һ���ڵ㣬��ô������һ���ڵ�ɾ���ò�>��Ȼ���ٽ���ָ�ɡ�  
        CLUSTER SETSLOT <slot> MIGRATING <node_id> �����ڵ�Ĳ� slot Ǩ�Ƶ� node_id ָ���Ľڵ��С�  
        CLUSTER SETSLOT <slot> IMPORTING <node_id> �� node_id ָ���Ľڵ��е���� slot �����ڵ㡣  
        CLUSTER SETSLOT <slot> STABLE ȡ���Բ� slot �ĵ��루import������Ǩ�ƣ�migrate����  

        �����������MIGRATE host port key destination-db timeout���в�λresharding
        �ο� redis�����ʵ�� ��17�� ��Ⱥ  17.4 ���·�Ƭ
        */
        int slot;
        clusterNode *n;

         // ȡ�� slot ֵ
        if ((slot = getSlotOrReply(c,c->argv[2])) == -1) return; //�����￴��һ��ֻ�ܽ���һ����λ���������Ҫ��β�λ��������Ҫ�����Լ�ʵ�ֶ���������

        // CLUSTER SETSLOT <slot> MIGRATING <node id>
        // �����ڵ�Ĳ� slot Ǩ���� node id ��ָ���Ľڵ�
        if (!strcasecmp(c->argv[3]->ptr,"migrating") && c->argc == 5) {
            // ��Ǩ�ƵĲ۱������ڱ��ڵ�
            if (server.cluster->slots[slot] != myself) {
                addReplyErrorFormat(c,"I'm not the owner of hash slot %u",slot);
                return;
            }
            // Ǩ�Ƶ�Ŀ��ڵ�����Ǳ��ڵ���֪��

            if ((n = clusterLookupNode(c->argv[4]->ptr)) == NULL) {
                addReplyErrorFormat(c,"I don't know about node %s",
                    (char*)c->argv[4]->ptr);
                return;
            }

            // Ϊ������Ǩ��Ŀ��ڵ�
            server.cluster->migrating_slots_to[slot] = n;

        // CLUSTER SETSLOT <slot> IMPORTING <node id>
        // �ӽڵ� node id �е���� slot �����ڵ�
        } else if (!strcasecmp(c->argv[3]->ptr,"importing") && c->argc == 5) {

             // ��� slot �۱����Ѿ��ɱ��ڵ㴦����ô������е���
            if (server.cluster->slots[slot] == myself) {
                addReplyErrorFormat(c,
                    "I'm already the owner of hash slot %u",slot);
                return;
            }

            // node id ָ���Ľڵ�����Ǳ��ڵ���֪�ģ��������ܴ�Ŀ��ڵ㵼���
            if ((n = clusterLookupNode(c->argv[4]->ptr)) == NULL) {
                addReplyErrorFormat(c,"I don't know about node %s",
                    (char*)c->argv[3]->ptr);
                return;
            }

            // Ϊ�����õ���Ŀ��ڵ�
            server.cluster->importing_slots_from[slot] = n;

        } else if (!strcasecmp(c->argv[3]->ptr,"stable") && c->argc == 4) {
            /* CLUSTER SETSLOT <SLOT> STABLE */
             // ȡ���Բ� slot ��Ǩ�ƻ��ߵ���
            //ע��:��Ǩ��Դ�ڵ��е�ĳ����λ��Ŀ�ļ�Ⱥ��Ϻ�(��������Ǩ�����)����Ҫ��Դ�ڵ㷢��cluster setslot <slot> stable��֪ͨ
            //Դredis�ڵ��λǨ����ϣ������������cluster nodes�л����Ǩ�ƹ���״̬������6f4e14557ff3ea0111ef802438bb612043f18480 10.2.4.5:7001 myself,master - 0 0 5 connected 4-5461 [2->-4f3012ab3fcaf52d21d453219f6575cdf06d2ca6] [3->-4f3012ab3fcaf52d21d453219f6575cdf06d2ca6]
            server.cluster->importing_slots_from[slot] = NULL;
            server.cluster->migrating_slots_to[slot] = NULL;

        } else if (!strcasecmp(c->argv[3]->ptr,"node") && c->argc == 5) {
            /* CLUSTER SETSLOT <SLOT> NODE <NODE ID> */
            // ��δָ�� slot ָ�ɸ� node id ָ���Ľڵ�            // ����Ŀ��ڵ�
            clusterNode *n = clusterLookupNode(c->argv[4]->ptr);

           // Ŀ��ڵ�����Ѵ���
            if (!n) {
                addReplyErrorFormat(c,"Unknown node %s",
                    (char*)c->argv[4]->ptr);
                return;
            }

            /* If this hash slot was served by 'myself' before to switch
             * make sure there are no longer local keys for this hash slot. */
           // ��������֮ǰ�ɵ�ǰ�ڵ㸺������ô���뱣֤������û�м�����
            if (server.cluster->slots[slot] == myself && n != myself) {
                if (countKeysInSlot(slot) != 0) {
                    addReplyErrorFormat(c,
                        "Can't assign hashslot %d to a different node "
                        "while I still hold keys for this hash slot.", slot);
                    return;
                }
            }
            /* If this slot is in migrating status but we have no keys
             * for it assigning the slot to another node will clear
             * the migratig status. */
            if (countKeysInSlot(slot) == 0 &&
                server.cluster->migrating_slots_to[slot])
                server.cluster->migrating_slots_to[slot] = NULL;

            /* If this node was importing this slot, assigning the slot to
             * itself also clears the importing status. */
            // �������ڵ�� slot �ĵ���ƻ�
            if (n == myself &&
                server.cluster->importing_slots_from[slot])
            {
                /* This slot was manually migrated, set this node configEpoch
                 * to a new epoch so that the new version can be propagated
                 * by the cluster.
                 *
                 * Note that if this ever results in a collision with another
                 * node getting the same configEpoch, for example because a
                 * failover happens at the same time we close the slot, the
                 * configEpoch collision resolution will fix it assigning
                 * a different epoch to each node. */
                uint64_t maxEpoch = clusterGetMaxEpoch();

                if (myself->configEpoch == 0 ||
                    myself->configEpoch != maxEpoch)
                {
                    server.cluster->currentEpoch++; 
                /* ���importing���ݵ�Ŀ�Ľڵ�(A->B,B�ڵ�)��epoch���Ǽ�Ⱥ�����ģ�������Ҫ�Ѹýڵ�epoll��Ϊ��Ⱥ�����ļ�1��
                Ŀ���Ǳ�֤���ڵ���Ϣͨ��PING PONG���͵������ڵ��ʱ�������ڵ㷢�ֱ��ڵ�epoll������ǲŻ����slotsλͼ
                 ��clusterUpdateSlotsConfigWith
                */
                    myself->configEpoch = server.cluster->currentEpoch;
                    clusterDoBeforeSleep(CLUSTER_TODO_FSYNC_CONFIG);
                    redisLog(REDIS_WARNING,
                        "configEpoch set to %llu after importing slot %d",
                        (unsigned long long) myself->configEpoch, slot);
                }
                server.cluster->importing_slots_from[slot] = NULL;
            }

             // ��������Ϊδָ��
            clusterDelSlot(slot);

             // ����ָ�ɸ�Ŀ��ڵ�
            clusterAddSlot(n,slot);

        } else {
            addReplyError(c,
                "Invalid CLUSTER SETSLOT action or number of arguments");
            return;
        }
        clusterDoBeforeSleep(CLUSTER_TODO_SAVE_CONFIG|CLUSTER_TODO_UPDATE_STATE);
        addReply(c,shared.ok);

    } else if (!strcasecmp(c->argv[1]->ptr,"info") && c->argc == 2) {
        /* CLUSTER INFO */
        // ��ӡ����Ⱥ�ĵ�ǰ��Ϣ

        char *statestr[] = {"ok","fail","needhelp"};
        int slots_assigned = 0, slots_ok = 0, slots_pfail = 0, slots_fail = 0;
        int j;

        // ͳ�Ƽ�Ⱥ�е���ָ�ɽڵ㡢�����߽ڵ㡢�������߽ڵ�������ڵ������
        for (j = 0; j < REDIS_CLUSTER_SLOTS; j++) {
            clusterNode *n = server.cluster->slots[j];

             // ����δָ�ɽڵ�
            if (n == NULL) continue;

             // ͳ����ָ�ɽڵ������
            slots_assigned++;
            if (nodeFailed(n)) {
                slots_fail++;
            } else if (nodeTimedOut(n)) {
                slots_pfail++;
            } else {
                 // �����ڵ�
                slots_ok++;
            }
        }

        /*
        �����ڵ�ʱ��Ĵ�ӡ����:
        127.0.0.1:7002> cluster info
        cluster_state:ok
        cluster_slots_assigned:16384
        cluster_slots_ok:16384
        cluster_slots_pfail:0
        cluster_slots_fail:0
        cluster_known_nodes:3
        cluster_size:3
        cluster_current_epoch:2
        cluster_stats_messages_sent:106495
        cluster_stats_messages_received:106495
        */

        // ��ӡ��Ϣ
        sds info = sdscatprintf(sdsempty(),
            "cluster_state:%s\r\n"
            "cluster_slots_assigned:%d\r\n"
            "cluster_slots_ok:%d\r\n"
            "cluster_slots_pfail:%d\r\n"
            "cluster_slots_fail:%d\r\n"
            "cluster_known_nodes:%lu\r\n"
            "cluster_size:%d\r\n"
            "cluster_current_epoch:%llu\r\n"
            "cluster_stats_messages_sent:%lld\r\n"
            "cluster_stats_messages_received:%lld\r\n"
            , statestr[server.cluster->state], //��Ⱥ״̬
            slots_assigned,  //��Ⱥ����ָ�ɵĲ�λ�����������Ӧ��ΪREDIS_CLUSTER_SLOTS 16384(16K)
            slots_ok, //��λ����ָ����
            slots_pfail, //��λpfail�������������Ӧ��Ϊ0����������ڵ㱻�ж�Ϊpfail��ýڵ�Ĳ�λ���Ͱ�����slots_pfail��
            slots_fail, //��Ϊ�ڵ㱻�ж�Ϊ���ߣ���Щ�ڵ����������Ĳ�λ��
            dictSize(server.cluster->nodes), //�ڵ��������������ߵ�,Ӧ�ð������ڵ�
            server.cluster->size, //���߲������ڴ�������һ���۵� master �����������������ߵ�
            (unsigned long long) server.cluster->currentEpoch, //������ȡ�İ汾��
            server.cluster->stats_bus_messages_sent, //���ڵ㷢������cluster�ڵ�����ݰ���С
            server.cluster->stats_bus_messages_received //�����ڵ㷢����cluster�ڵ�����ݰ���С
        );
        addReplySds(c,sdscatprintf(sdsempty(),"$%lu\r\n",
            (unsigned long)sdslen(info)));
        addReplySds(c,info);
        addReply(c,shared.crlf);

    } else if (!strcasecmp(c->argv[1]->ptr,"saveconfig") && c->argc == 2) {
        // CLUSTER SAVECONFIG 命令
        // �� nodes.conf �ļ����浽��������       
        // ����
        int retval = clusterSaveConfig(1);

        // ������
        if (retval == 0)
            addReply(c,shared.ok);
        else
            addReplyErrorFormat(c,"error saving the cluster node config: %s",
                strerror(errno));

    } else if (!strcasecmp(c->argv[1]->ptr,"keyslot") && c->argc == 3) {
        /* CLUSTER KEYSLOT <key> */
         // ���� key Ӧ�ñ� hash ���Ǹ�����

        sds key = c->argv[2]->ptr;

        addReplyLongLong(c,keyHashSlot(key,sdslen(key)));

    } else if (!strcasecmp(c->argv[1]->ptr,"countkeysinslot") && c->argc == 3) {
        /* CLUSTER COUNTKEYSINSLOT <slot> */
        // ����ָ�� slot �ϵļ�����

        long long slot;

         // ȡ�� slot ����
        if (getLongLongFromObjectOrReply(c,c->argv[2],&slot,NULL) != REDIS_OK)
            return;
        if (slot < 0 || slot >= REDIS_CLUSTER_SLOTS) {
            addReplyError(c,"Invalid slot");
            return;
        }

        addReplyLongLong(c,countKeysInSlot(slot));

    } else if (!strcasecmp(c->argv[1]->ptr,"getkeysinslot") && c->argc == 4) {
        /* CLUSTER GETKEYSINSLOT <slot> <count> */
         // ��ӡ count ������ slot �۵ļ�     
         long long maxkeys, slot;       
         unsigned int numkeys, j;     
         robj **keys;       
         // ȡ�� slot ����     
         if (getLongLongFromObjectOrReply(c,c->argv[2],&slot,NULL) != REDIS_OK)        
         return;       

         // ȡ�� count ����     
         if (getLongLongFromObjectOrReply(c,c->argv[3],&maxkeys,NULL)         
            != REDIS_OK)           
            return;        

        // �������ĺϷ���
        if (slot < 0 || slot >= REDIS_CLUSTER_SLOTS || maxkeys < 0) {
            addReplyError(c,"Invalid slot or number of keys");
            return;
        }

         // ����һ�������������      
         keys = zmalloc(sizeof(robj*)*maxkeys);     
         // ������¼�� keys ����    
         numkeys = getKeysInSlot(slot, keys, maxkeys);    

         // ��ӡ��õļ�
        addReplyMultiBulkLen(c,numkeys);
        for (j = 0; j < numkeys; j++) addReplyBulk(c,keys[j]);
        zfree(keys);

    } else if (!strcasecmp(c->argv[1]->ptr,"forget") && c->argc == 3) {
        /* CLUSTER FORGET <NODE ID> */
        // �Ӽ�Ⱥ��ɾ�� NODE_ID ָ���Ľڵ�     
        // ���� NODE_ID ָ���Ľڵ�     

        clusterNode *n = clusterLookupNode(c->argv[2]->ptr);   

        // �ýڵ㲻�����ڼ�Ⱥ��
        if (!n) {
            addReplyErrorFormat(c,"Unknown node %s", (char*)c->argv[2]->ptr);
            return;
        } else if (n == myself) {
            addReplyError(c,"I tried hard but I can't forget myself...");
            return;
        } else if (nodeIsSlave(myself) && myself->slaveof == n) {
            addReplyError(c,"Can't forget my master!");
            return;
        }

        // ����Ⱥ��ӵ�������    
        clusterBlacklistAddNode(n);    
        // �Ӽ�Ⱥ��ɾ���ýڵ�
        clusterDelNode(n);
        clusterDoBeforeSleep(CLUSTER_TODO_UPDATE_STATE|
                             CLUSTER_TODO_SAVE_CONFIG);
        addReply(c,shared.ok);

    } else if (!strcasecmp(c->argv[1]->ptr,"replicate") && c->argc == 3) {
        /* CLUSTER REPLICATE <NODE ID> */
        // ����ǰ�ڵ�����Ϊ NODE_ID ָ���Ľڵ�Ĵӽڵ㣨����Ʒ��     
        // �������ֲ��ҽڵ�
        clusterNode *n = clusterLookupNode(c->argv[2]->ptr);

        /* Lookup the specified node in our table. */
        if (!n) {
            addReplyErrorFormat(c,"Unknown node %s", (char*)c->argv[2]->ptr);
            return;
        }

        /* I can't replicate myself. */
        // ָ���ڵ����Լ������ܽ��и���      
        if (n == myself) {           
            addReplyError(c,"Can't replicate myself");           
            return;     
        }      
        /* Can't replicate a slave. */      
        // ���ܸ���һ���ӽڵ�
        if (n->slaveof != NULL) {
            addReplyError(c,"I can only replicate a master, not a slave.");
            return;
        }

        /* If the instance is currently a master, it should have no assigned
         * slots nor keys to accept to replicate some other node.
         * Slaves can switch to another master without issues. */
        // �ڵ����û�б�ָ���κβۣ��������ݿ����Ϊ��
        if (nodeIsMaster(myself) &&
            (myself->numslots != 0 || dictSize(server.db[0].dict) != 0)) {
            addReplyError(c,
                "To set a master the node must be empty and "
                "without assigned slots.");
            return;
        }

        /* Set the master. */
         // ���ڵ� n ��Ϊ���ڵ�����ڵ�
        clusterSetMaster(n); 
        clusterDoBeforeSleep(CLUSTER_TODO_UPDATE_STATE|CLUSTER_TODO_SAVE_CONFIG);
        addReply(c,shared.ok);
    } else if (!strcasecmp(c->argv[1]->ptr,"slaves") && c->argc == 3) {
        /* CLUSTER SLAVES <NODE ID> ��ӡ�������ڵ�����дӽڵ����Ϣ*/
          // ��ӡ�������ڵ�����дӽڵ����Ϣ

        //CLUSTER SLAVES <NODE ID> ��ӡ�������ڵ�����дӽڵ����Ϣ 
        //CLUSTER FAILOVER [FORCE]  ִ���ֶ�����ת��
        //cluster set-config-epoch <epoch>����ǿ������configEpoch
        //CLUSTER RESET [SOFT|HARD]��Ⱥ��λ��������Ƚ�Σ��
        clusterNode *n = clusterLookupNode(c->argv[2]->ptr);
        int j;

        /* Lookup the specified node in our table. */
        if (!n) {
            addReplyErrorFormat(c,"Unknown node %s", (char*)c->argv[2]->ptr);
            return;
        }

        if (nodeIsSlave(n)) {
            addReplyError(c,"The specified node is not a master");
            return;
        }

        addReplyMultiBulkLen(c,n->numslaves);
        for (j = 0; j < n->numslaves; j++) {
            sds ni = clusterGenNodeDescription(n->slaves[j]);
            addReplyBulkCString(c,ni);
            sdsfree(ni);
        }
    } else if (!strcasecmp(c->argv[1]->ptr,"failover") &&
               (c->argc == 2 || c->argc == 3))
    {
        /* CLUSTER FAILOVER [FORCE]  ֻ�ܷ���slave */
        // ִ���ֶ�����ת��
        /*
        Manual Failover��һ����ά���ܣ������ֶ����ôӽڵ�Ϊ�µ����ڵ㣬��ʹ���ڵ㻹���š�

        ���⣬Manual Failover��force�ͷ�force���������ڣ���force��Ҫ�ȴӽڵ���ȫͬ�������ڵ�����ݺ�Ž���failover����֤����
        ʧ���ݣ���������У�ԭ���ڵ�ֹͣд��������force�����н�����������ͬ����ֱ�ӽ���failover��

        �ֶ�����ת��
            Redis��Ⱥ֧���ֶ�����ת�ơ�Ҳ������ӽڵ㷢�͡�CLUSTER  FAILOVER�����ʹ�������ڵ�δ���ߵ�����£�
         �������ת�����̣�����Ϊ�µ����ڵ㣬��ԭ�������ڵ㽵��Ϊ�ӽڵ㡣
         Ϊ�˲���ʧ���ݣ���ӽڵ㷢�͡�CLUSTER  FAILOVER������(����fource)���������£�
         a���ӽڵ��յ�����������ڵ㷢��CLUSTERMSG_TYPE_MFSTART����
         b�����ڵ��յ��ð��󣬻Ὣ�����пͻ�����������״̬��Ҳ������10s��ʱ���ڣ����ٴ���ͻ��˷���������������䷢�͵��������У������CLUSTERMSG_FLAG0_PAUSED��ǣ�
         c���ӽڵ��յ����ڵ㷢���ģ���CLUSTERMSG_FLAG0_PAUSED��ǵ��������󣬴��л�ȡ���ڵ㵱ǰ�ĸ���ƫ�������ӽڵ�ȵ��Լ��ĸ���ƫ�����ﵽ��ֵ�󣬲ŻῪʼִ�й���ת�����̣�����ѡ�١�ͳ��ѡƱ��Ӯ��ѡ�١�����Ϊ���ڵ㲢�������ã�
 
         ��CLUSTER  FAILOVER������֧������ѡ�FORCE�Ͳ���forece��ʹ��������ѡ����Ըı����������̡�
         �����FORCEѡ���ӽڵ㲻�������ڵ���н��������ڵ�Ҳ����������ͻ��ˣ����Ǵӽڵ�������ʼ����ת�����̣�����ѡ�١�ͳ��ѡƱ��Ӯ��ѡ�١�����Ϊ���ڵ㲢�������á�

         ��ˣ�ʹ��FORCEѡ����ڵ�����Ѿ����ߣ�����ʹ���κ�ѡ�ֻ���͡�CLUSTER  FAILOVER������Ļ������ڵ�������ߡ�
        */
        
        int force = 0;

        if (c->argc == 3) {
            if (!strcasecmp(c->argv[2]->ptr,"force")) {
                force = 1;
            } else {
                addReply(c,shared.syntaxerr);
                return;
            }
        }

         // ����ֻ�ܷ��͸��ӽڵ�
        if (nodeIsMaster(myself)) {
            addReplyError(c,"You should send CLUSTER FAILOVER to a slave");
            return;
        } else if (!force &&
                   (myself->slaveof == NULL || nodeFailed(myself->slaveof) ||
                   myself->slaveof->link == NULL))
        {
            // ������ڵ������߻��ߴ���ʧЧ״̬          
            // ��������û�и��� force ��������ô����ִ��ʧ��
            addReplyError(c,"Master is down or failed, "
                            "please use CLUSTER FAILOVER FORCE");
            return;
        }

        // �����ֶ�����ת�Ƶ��й�����      
        resetManualFailover();       
        // �趨�ֶ�����ת�Ƶ����ִ��ʱ��
        server.cluster->mf_end = mstime() + REDIS_CLUSTER_MF_TIMEOUT;

        /* If this is a forced failover, we don't need to talk with our master
         * to agree about the offset. We just failover taking over it without
         * coordination. */
         // �������ǿ�Ƶ��ֶ� failover ����ôֱ�ӿ�ʼ failover ��      
         // ���������� master ��ͨƫ������       
        if (force) {        
            // �������ǿ�Ƶ��ֶ�����ת�ƣ���ôֱ�ӿ�ʼִ�й���ת�Ʋ���        
            server.cluster->mf_can_start = 1;      
        } else {           
            // �������ǿ�ƵĻ�����ô��Ҫ�����ڵ�ȶ��໥��ƫ�����Ƿ�һ��
            clusterSendMFStart(myself->slaveof);
        }
        redisLog(REDIS_WARNING,"Manual failover user request accepted.");
        addReply(c,shared.ok);
    } else if (!strcasecmp(c->argv[1]->ptr,"set-config-epoch") && c->argc == 3)
    {
        /* CLUSTER SET-CONFIG-EPOCH <epoch>
         *
         * The user is allowed to set the config epoch only when a node is
         * totally fresh: no config epoch, no other known node, and so forth.
         * This happens at cluster creation time to start with a cluster where
         * every node has a different node ID, without to rely on the conflicts
         * resolution system which is too slow when a big cluster is created. */
        long long epoch;

        if (getLongLongFromObjectOrReply(c,c->argv[2],&epoch,NULL) != REDIS_OK)
            return;

        if (epoch < 0) {
            addReplyErrorFormat(c,"Invalid config epoch specified: %lld",epoch);
        } else if (dictSize(server.cluster->nodes) > 1) {
            addReplyError(c,"The user can assign a config epoch only when the "
                            "node does not know any other node.");
        } else if (myself->configEpoch != 0) {
            addReplyError(c,"Node config epoch is already non-zero");
        } else {
            myself->configEpoch = epoch;
            /* No need to fsync the config here since in the unlucky event
             * of a failure to persist the config, the conflict resolution code
             * will assign an unique config to this node. */
            clusterDoBeforeSleep(CLUSTER_TODO_UPDATE_STATE|
                                 CLUSTER_TODO_SAVE_CONFIG);
            addReply(c,shared.ok);
        }
    } else if (!strcasecmp(c->argv[1]->ptr,"reset") &&
               (c->argc == 2 || c->argc == 3))
    {
        /* CLUSTER RESET [SOFT|HARD] */
        int hard = 0;

        /* Parse soft/hard argument. Default is soft. */
        if (c->argc == 3) {
            if (!strcasecmp(c->argv[2]->ptr,"hard")) {
                hard = 1;
            } else if (!strcasecmp(c->argv[2]->ptr,"soft")) {
                hard = 0;
            } else {
                addReply(c,shared.syntaxerr);
                return;
            }
        }

        /* Slaves can be reset while containing data, but not master nodes
         * that must be empty. */
        if (nodeIsMaster(myself) && dictSize(c->db->dict) != 0) {
            addReplyError(c,"CLUSTER RESET can't be called with "
                            "master nodes containing keys");
            return;
        }
        clusterReset(hard);
        addReply(c,shared.ok); 
    } else {
        addReplyError(c,"Wrong CLUSTER subcommand or number of arguments");
    }
}


/*
DUMP���л�  restore�����л�����ʵ����������У�鹦��

10.2.4.5:7001> set yang 11111
OK
10.2.4.5:7001> DUMP yang
"\x00\xc1g+\x06\x00}\xb0\xa0\xe2+\xa7\x91\a"
10.2.4.5:7001> RESTORE yangxx 0 "\x00\xc1g+\x06\x00}\xb0\xa0\xe2+\xa7\x91\a"
OK
10.2.4.5:7001> get yang xx
(error) ERR wrong number of arguments for 'get' command
10.2.4.5:7001> get yangxx
"11111"
10.2.4.5:7001> 
*/
/* -----------------------------------------------------------------------------
 * DUMP, RESTORE and MIGRATE commands
 * -------------------------------------------------------------------------- */

/* Generates a DUMP-format representation of the object 'o', adding it to the
 * io stream pointed by 'rio'. This function can't fail. 
 *
 * �������� o ��һ�� DUMP ��ʽ��ʾ��
 * ��������ӵ� rio ָ��ָ��� io �����С�
 */
void createDumpPayload(rio *payload, robj *o) {
    unsigned char buf[2];
    uint64_t crc;

    /* Serialize the object in a RDB-like format. It consist of an object type
     * byte followed by the serialized object. This is understood by RESTORE. */
    // ���������л�Ϊһ�� RDB ��ʽ����  
    // ���л������Զ�������Ϊ�ף�������л���Ķ���   
    // ��ͼ  
    //   
    // |<-- RDB payload  -->|  
    //      ���л�����   
    // +-------------+------+    
    // | 1 byte type | obj  |   
    // +-------------+------+

    rioInitWithBuffer(payload,sdsempty());
    redisAssert(rdbSaveObjectType(payload,o));
    redisAssert(rdbSaveObject(payload,o));

    /* Write the footer, this is how it looks like:
     * ----------------+---------------------+---------------+
     * ... RDB payload | 2 bytes RDB version | 8 bytes CRC64 |
     * ----------------+---------------------+---------------+
     * RDB version and CRC are both in little endian.
     */

    /* RDB version */
     // д�� RDB �汾
    buf[0] = REDIS_RDB_VERSION & 0xff;
    buf[1] = (REDIS_RDB_VERSION >> 8) & 0xff;
    payload->io.buffer.ptr = sdscatlen(payload->io.buffer.ptr,buf,2);

    /* CRC64 */
    // д�� CRC У���
    crc = crc64(0,(unsigned char*)payload->io.buffer.ptr,
                sdslen(payload->io.buffer.ptr));
    memrev64ifbe(&crc);
    payload->io.buffer.ptr = sdscatlen(payload->io.buffer.ptr,&crc,8);

    // �������ݵĽṹ:   
    //   
    // | <--- ���л����� -->|
    // +-------------+------+---------------------+---------------+
    // | 1 byte type | obj  | 2 bytes RDB version | 8 bytes CRC64 |
    // +-------------+------+---------------------+---------------+

}

/* Verify that the RDB version of the dump payload matches the one of this Redis
 * instance and that the checksum is ok.
 *
 * �������� DUMP �����У� RDB �汾�Ƿ�͵�ǰ Redis ʵ����ʹ�õ� RDB �汾��ͬ��
 * �����У����Ƿ���ȷ��
 *
 * If the DUMP payload looks valid REDIS_OK is returned, otherwise REDIS_ERR
 * is returned. 
 *
 ����������� REDIS_OK �����򷵻� REDIS_ERR ��
 */
int verifyDumpPayload(unsigned char *p, size_t len) {
    unsigned char *footer;
    uint16_t rdbver;
    uint64_t crc;

    /* At least 2 bytes of RDB version and 8 of CRC64 should be present. */
    // ��Ϊ���л��������ٰ��� 2 ���ֽڵ� RDB �汾  
    // �Լ� 8 ���ֽڵ� CRC64 У���   
    // �������л����ݲ��������� 10 ���ֽ�
    if (len < 10) return REDIS_ERR;

     // ָ�����ݵ���� 10 ���ֽ�
    footer = p+(len-10);

    /* Verify RDB version */
    // ������л����ݵİ汾�ţ����Ƿ�͵�ǰʵ��ʹ�õİ汾��һ��
    rdbver = (footer[1] << 8) | footer[0];
    if (rdbver != REDIS_RDB_VERSION) return REDIS_ERR;

    /* Verify CRC64 */
    // ������ݵ� CRC64 У����Ƿ���ȷ
    crc = crc64(0,p,len-8);
    memrev64ifbe(&crc);
    return (memcmp(&crc,footer+2,8) == 0) ? REDIS_OK : REDIS_ERR;
}

/*
DUMP key

���л����� key �������ر����л���ֵ��ʹ�� RESTORE ������Խ����ֵ�����л�Ϊ Redis ����

���л����ɵ�ֵ�����¼����ص㣺
?������ 64 λ��У��ͣ����ڼ����� RESTORE �ڽ��з����л�֮ǰ���ȼ��У��͡�
?ֵ�ı����ʽ�� RDB �ļ�����һ�¡�
?RDB �汾�ᱻ���������л�ֵ���У������Ϊ Redis �İ汾��ͬ��� RDB ��ʽ�����ݣ���ô Redis ��ܾ������ֵ���з����л�������

���л���ֵ�������κ�����ʱ����Ϣ��
���ð汾��>= 2.6.0ʱ�临�Ӷȣ�

���Ҹ������ĸ��Ӷ�Ϊ O(1) ���Լ��������л��ĸ��Ӷ�Ϊ O(N*M) ������ N �ǹ��� key �� Redis ������������� M ������Щ�����ƽ����С��

������л��Ķ����ǱȽ�С���ַ�������ô���Ӷ�Ϊ O(1) ��
����ֵ��

��� key �����ڣ���ô���� nil ��

���򣬷������л�֮���ֵ��


redis> SET greeting "hello, dumping world!"
OK

redis> DUMP greeting
"\x00\x15hello, dumping world!\x06\x00E\xa0Z\x82\xd8r\xc1\xde"

redis> DUMP not-exists-key
(nil)
*/

/*
DUMP���л�  restore�����л�����ʵ����������У�鹦��

10.2.4.5:7001> set yang 11111
OK
10.2.4.5:7001> DUMP yang
"\x00\xc1g+\x06\x00}\xb0\xa0\xe2+\xa7\x91\a"
10.2.4.5:7001> RESTORE yangxx 0 "\x00\xc1g+\x06\x00}\xb0\xa0\xe2+\xa7\x91\a"
OK
10.2.4.5:7001> get yang xx
(error) ERR wrong number of arguments for 'get' command
10.2.4.5:7001> get yangxx
"11111"
10.2.4.5:7001> 
*/


//ִ�� DUMP ���� ���������л���Ȼ���͵�Ŀ��ʵ����Ŀ��ʵ����ʹ�� RESTORE �����ݽ��з����л�
/* DUMP keyname
 * DUMP is actually not used by Redis Cluster but it is the obvious
 * complement of RESTORE and can be useful for different applications. */
void dumpCommand(redisClient *c) {
    robj *o, *dumpobj;
    rio payload;

    /* Check if the key is here. */
    // ȡ����������ֵ
    if ((o = lookupKeyRead(c->db,c->argv[1])) == NULL) {
        addReply(c,shared.nullbulk);
        return;
    }

    /* Create the DUMP encoded representation. */
    // ��������ֵ��һ�� DUMP �����ʾ
    createDumpPayload(&payload,o);

    /* Transfer to the client */
     // �������ļ�ֵ�����ݷ��ظ��ͻ���
    dumpobj = createObject(REDIS_STRING,payload.io.buffer.ptr);
    addReplyBulk(c,dumpobj);
    decrRefCount(dumpobj);

    return;
}

/*
RESTORE key ttl serialized-value [REPLACE]

�����л����������л�ֵ���������͸����� key ������

���� ttl �Ժ���Ϊ��λΪ key ��������ʱ�䣻��� ttl Ϊ 0 ����ô����������ʱ�䡣

RESTORE ��ִ�з����л�֮ǰ���ȶ����л�ֵ�� RDB �汾������У��ͽ��м�飬��� RDB �汾����ͬ�������ݲ������Ļ�����ô RESTORE ��ܾ����з����л���������һ������

����� key �Ѿ����ڣ� ���Ҹ����� REPLACE ѡ� ��ôʹ�÷����л��ó���ֵ������� key ԭ�е�ֵ�� �෴�أ� ����� key �Ѿ����ڣ� ����û�и��� REPLACE ѡ� ��ô�����һ������

������Ϣ���Բο� DUMP ���
���ð汾��>= 2.6.0ʱ�临�Ӷȣ�

���Ҹ������ĸ��Ӷ�Ϊ O(1) ���Լ����з����л��ĸ��Ӷ�Ϊ O(N*M) ������ N �ǹ��� key �� Redis ������������� M ������Щ�����ƽ����С��

���򼯺�(sorted set)�ķ����л����Ӷ�Ϊ O(N*M*log(N)) ����Ϊ���򼯺�ÿ�β���ĸ��Ӷ�Ϊ O(log(N)) ��

��������л��Ķ����ǱȽ�С���ַ�������ô���Ӷ�Ϊ O(1) ��
����ֵ��

��������л��ɹ���ô���� OK �����򷵻�һ������


# ����һ��������Ϊ DUMP ���������

redis> SET greeting "hello, dumping world!"
OK

redis> DUMP greeting
"\x00\x15hello, dumping world!\x06\x00E\xa0Z\x82\xd8r\xc1\xde"

# �����л����� RESTORE ����һ��������

redis> RESTORE greeting-again 0 "\x00\x15hello, dumping world!\x06\x00E\xa0Z\x82\xd8r\xc1\xde"
OK

redis> GET greeting-again
"hello, dumping world!"

# ��û�и��� REPLACE ѡ�������£��ٴγ��Է����л���ͬһ������ʧ��

redis> RESTORE greeting-again 0 "\x00\x15hello, dumping world!\x06\x00E\xa0Z\x82\xd8r\xc1\xde"
(error) ERR Target key name is busy.

# ���� REPLACE ѡ���ͬһ�������з����л��ɹ�

redis> RESTORE greeting-again 0 "\x00\x15hello, dumping world!\x06\x00E\xa0Z\x82\xd8r\xc1\xde" REPLACE
OK

# ����ʹ����Ч��ֵ���з����л�������

redis> RESTORE fake-message 0 "hello moto moto blah blah"
(error) ERR DUMP payload version or checksum are wrong


*/

/*
DUMP���л�  restore�����л�����ʵ����������У�鹦��

10.2.4.5:7001> set yang 11111
OK
10.2.4.5:7001> DUMP yang
"\x00\xc1g+\x06\x00}\xb0\xa0\xe2+\xa7\x91\a"
10.2.4.5:7001> RESTORE yangxx 0 "\x00\xc1g+\x06\x00}\xb0\xa0\xe2+\xa7\x91\a"
OK
10.2.4.5:7001> get yang xx
(error) ERR wrong number of arguments for 'get' command
10.2.4.5:7001> get yangxx
"11111"
10.2.4.5:7001> 
*/

//*select 0 +  (RESTORE-ASKING | RESTORE) + KEY-VALUE-EXPIRE + dump���л�value + [replace]   ��ӦrestoreCommand�Ը�KV�������л���ԭ

/* RESTORE key ttl serialized-value [REPLACE] */
// ���ݸ����� DUMP ���ݣ���ԭ��һ����ֵ�����ݣ����������浽���ݿ�����
void restoreCommand(redisClient *c) { //migrateCommand��restoreCommand��Ӧ
    long long ttl;
    rio payload;
    int j, type, replace = 0;
    robj *obj;

    /* Parse additional options */
     // �Ƿ�ʹ���� REPLACE ѡ�
    for (j = 4; j < c->argc; j++) {
        if (!strcasecmp(c->argv[j]->ptr,"replace")) {
            replace = 1;
        } else {
            addReply(c,shared.syntaxerr);
            return;
        }
    }

    /* Make sure this key does not already exist here... */
        // ���û�и��� REPLACE ѡ����Ҽ��Ѿ����ڣ���ô���ش���
    if (!replace && lookupKeyWrite(c->db,c->argv[1]) != NULL) {
        addReply(c,shared.busykeyerr);
        return;
    }

    /* Check if the TTL value makes sense */
    // ȡ���������еģ� TTL ֵ
    if (getLongLongFromObjectOrReply(c,c->argv[2],&ttl,NULL) != REDIS_OK) {
        return;
    } else if (ttl < 0) {
        addReplyError(c,"Invalid TTL value, must be >= 0");
        return;
    }

    /* Verify RDB version and data checksum. */
    // ��� RDB �汾��У���
    if (verifyDumpPayload(c->argv[3]->ptr,sdslen(c->argv[3]->ptr)) == REDIS_ERR)
    {
        addReplyError(c,"DUMP payload version or checksum are wrong");
        return;
    }

     // ��ȡ DUMP ���ݣ��������л�����ֵ�Ե����ͺ�ֵ
    rioInitWithBuffer(&payload,c->argv[3]->ptr);
    if (((type = rdbLoadObjectType(&payload)) == -1) ||
        ((obj = rdbLoadObject(type,&payload)) == NULL))
    {
        addReplyError(c,"Bad data format");
        return;
    }

    /* Remove the old key if needed. */
        // ��������� REPLACE ѡ���ô��ɾ�����ݿ����Ѵ��ڵ�ͬ����
    if (replace) dbDelete(c->db,c->argv[1]);

    /* Create the key and set the TTL if any */
    // ����ֵ����ӵ����ݿ�
    dbAdd(c->db,c->argv[1],obj);

    // ��������� TTL �Ļ������ü��� TTL
    if (ttl) setExpire(c->db,c->argv[1],mstime()+ttl);

    signalModifiedKey(c->db,c->argv[1]);

    addReply(c,shared.ok);
    server.dirty++;
}

/* MIGRATE socket cache implementation.
 *
 * MIGRATE �׽��ֻ���ʵ��
 *
 * We take a map between host:ip and a TCP socket that we used to connect
 * to this instance in recent time.
 *
 * ����һ���ֵ䣬�ֵ�ļ�Ϊ host:ip ��ֵΪ���ʹ�õ�������ָ����ַ�� TCP �׽��֡�

 *
 * This sockets are closed when the max number we cache is reached, and also
 * in serverCron() when they are around for more than a few seconds. 
 *
 * ����ֵ��ڻ������ﵽ����ʱ���ͷţ� 
 * ���� serverCron() Ҳ�ᶨ��ɾ���ֵ��е�һЩ�����׽��֡�
 */
// ��󻺴���
#define MIGRATE_SOCKET_CACHE_ITEMS 64 /* max num of items in the cache. */
// �׽��ֱ����ڣ��������ʱ����׽��ֻᱻɾ����

#define MIGRATE_SOCKET_CACHE_TTL 10 /* close cached socekts after 10 sec. */

/*
��������:
    ��Ϊһ������£�����Ҫ�����key��AǨ�Ƶ�B�У�Ϊ�˱���A��B֮����Ҫ���TCP��������������˻�������
��ʵ�ַ�����������ԣ���Ǩ�Ƶ�һ��keyʱ���ڵ�A��ڵ�B������������TCP���ӻ���������һ��ʱ���ڣ�����Ҫ
Ǩ����һ��keyʱ������ֱ��ʹ�û�������ӣ��������ظ���������������������ʱ�䲻�ã�����Զ��ͷš�
*/
typedef struct migrateCachedSocket { //�洢��migrate_cached_sockets

    // �׽���������   
    int fd;    

    //��һ��ʹ�õ�Ŀ�Ľڵ�����ݿ�ID���Լ���������һ�α�ʹ�õ�ʱ�䡣
    // ���һ��ʹ�õ�ʱ��
    time_t last_use_time; //���������һ��ʱ�䶼û�õĻ�������ͷ����ӣ���migrateCloseTimedoutSockets

} migrateCachedSocket;

/* Return a TCP scoket connected with the target instance, possibly returning
 * a cached one.
 *
 * ����һ��������ָ����ַ�� TCP �׽��֣�����׽��ֿ�����һ�������׽��֡�

 *
 * This function is responsible of sending errors to the client if a
 * connection can't be established. In this case -1 is returned.
 * Otherwise on success the socket is returned, and the caller should not
 * attempt to free it after usage.
 *
 * ������ӳ�����ô�������� -1 �� 
 ���������������ô�������� TCP �׽�����������
 *
 * If the caller detects an error while using the socket, migrateCloseSocket()
 * should be called so that the connection will be craeted from scratch
 * the next time. 
 *
 * �����������ʹ������������ص��׽���ʱ���ϴ���
 * ��ô�����߻�ʹ�� migrateCloseSocket() ���رճ�����׽��֣�
 * �����´�Ҫ������ͬ��ַʱ���������ͻᴴ���µ��׽������������ӡ�
 */

/*
��������
    ��Ϊһ������£�����Ҫ�����key��AǨ�Ƶ�B�У�Ϊ�˱���A��B֮����Ҫ���TCP��������������˻���
 ���ӵ�ʵ�ַ�����������ԣ���Ǩ�Ƶ�һ��keyʱ���ڵ�A��ڵ�B������������TCP���ӻ���������һ��ʱ
 ���ڣ�����ҪǨ����һ��keyʱ������ֱ��ʹ�û�������ӣ��������ظ���������������������ʱ�䲻�ã�����Զ��ͷš�
*/
int migrateGetSocket(redisClient *c, robj *host, robj *port, long timeout) {
    int fd;
    sds name = sdsempty();
    migrateCachedSocket *cs;

    /* Check if we have an already cached socket for this ip:port pair. */
    // ���� ip �� port ������ַ����  name��ip:port�ַ���
    name = sdscatlen(name,host->ptr,sdslen(host->ptr));
    name = sdscatlen(name,":",1);
    name = sdscatlen(name,port->ptr,sdslen(port->ptr));
    
    // ���׽��ֻ����в����׽����Ƿ��Ѿ�����
    cs = dictFetchValue(server.migrate_cached_sockets,name);
    // ������ڣ��������һ��ʹ��ʱ�䣬�����������������׽��ֶ����ͷ�
    if (cs) {
        sdsfree(name);
        cs->last_use_time = server.unixtime;
        return cs->fd;
    }

    /* No cached socket, create one. */
    // û�л��棬����һ���µĻ���
    if (dictSize(server.migrate_cached_sockets) == MIGRATE_SOCKET_CACHE_ITEMS) {

        // ����������Ѿ��ﵽ���ߣ���ô�ڴ����׽���֮ǰ�������ɾ��һ������

        /* Too many items, drop one at random. */
        dictEntry *de = dictGetRandomKey(server.migrate_cached_sockets);
        cs = dictGetVal(de);
        close(cs->fd);
        zfree(cs);
        dictDelete(server.migrate_cached_sockets,dictGetKey(de));
    }

    /* Create the socket */
        // ��������
    fd = anetTcpNonBlockConnect(server.neterr,c->argv[1]->ptr,
                atoi(c->argv[2]->ptr));
    if (fd == -1) {
        sdsfree(name);
        addReplyErrorFormat(c,"Can't connect to target node: %s",
            server.neterr);
        return -1;
    }
    anetEnableTcpNoDelay(server.neterr,fd);

    /* Check if it connects within the specified timeout. */
     // ������ӵĳ�ʱ����
    if ((aeWait(fd,AE_WRITABLE,timeout) & AE_WRITABLE) == 0) {
        sdsfree(name);
        addReplySds(c,
            sdsnew("-IOERR error or timeout connecting to the client\r\n"));
        close(fd);
        return -1;
    }

    /* Add to the cache and return it to the caller. */
    // ��������ӵ�����
    cs = zmalloc(sizeof(*cs));
    cs->fd = fd;
    cs->last_use_time = server.unixtime;
    dictAdd(server.migrate_cached_sockets,name,cs); 

    return fd;
}

/* Free a migrate cached connection. */
// �ͷ�һ����������
void migrateCloseSocket(robj *host, robj *port) {
    sds name = sdsempty();
    migrateCachedSocket *cs;

    // ���� ip �� port �������ӵ�����
    name = sdscatlen(name,host->ptr,sdslen(host->ptr));
    name = sdscatlen(name,":",1);
    name = sdscatlen(name,port->ptr,sdslen(port->ptr));
    // ��������
    cs = dictFetchValue(server.migrate_cached_sockets,name);
    if (!cs) {
        sdsfree(name);
        return;
    }

    // �ر�����
    close(cs->fd);
    zfree(cs);

    // �ӻ�����ɾ��������
    dictDelete(server.migrate_cached_sockets,name);
    sdsfree(name);
}

// �Ƴ����ڵ����ӣ��� redis.c/serverCron() ����
void migrateCloseTimedoutSockets(void) {
    dictIterator *di = dictGetSafeIterator(server.migrate_cached_sockets);
    dictEntry *de;

    while((de = dictNext(di)) != NULL) {
        migrateCachedSocket *cs = dictGetVal(de);

        // ����׽������һ��ʹ�õ�ʱ���Ѿ����� MIGRATE_SOCKET_CACHE_TTL     
        // ��ô��ʾ���׽��ֹ��ڣ��ͷ�����
        if ((server.unixtime - cs->last_use_time) > MIGRATE_SOCKET_CACHE_TTL) {
            close(cs->fd);
            zfree(cs);
            dictDelete(server.migrate_cached_sockets,dictGetKey(de));
        }
    }
    dictReleaseIterator(di);
}

/*
MIGRATE

MIGRATE host port key destination-db timeout [COPY] [REPLACE]

�� key ԭ���Եشӵ�ǰʵ�����͵�Ŀ��ʵ����ָ�����ݿ��ϣ�һ�����ͳɹ��� key ��֤�������Ŀ��ʵ���ϣ�����ǰʵ���ϵ� key �ᱻɾ����

���������һ��ԭ�Ӳ���������ִ�е�ʱ�����������Ǩ�Ƶ�����ʵ����ֱ������������������Ǩ�Ƴɹ���Ǩ��ʧ�ܣ��ȴ���ʱ��

������ڲ�ʵ���������ģ����ڵ�ǰʵ���Ը��� key ִ�� DUMP ���� ���������л���Ȼ���͵�Ŀ��ʵ����Ŀ��ʵ����ʹ�� RESTORE �����ݽ��з����л������������л����õ�������ӵ����ݿ��У���ǰʵ������Ŀ��ʵ���Ŀͻ���������ֻҪ���� RESTORE ����� OK �����ͻ���� DEL ɾ���Լ����ݿ��ϵ� key ��

timeout �����Ժ���Ϊ��ʽ��ָ����ǰʵ����Ŀ��ʵ�����й�ͨ�������ʱ�䡣��˵����������һ��Ҫ�� timeout ��������ɣ�ֻ��˵���ݴ��͵�ʱ�䲻�ܳ������ timeout ����

MIGRATE ������Ҫ�ڸ�����ʱ��涨����� IO ����������ڴ�������ʱ���� IO ���󣬻��ߴﵽ�˳�ʱʱ�䣬��ô�����ִֹͣ�У�������һ������Ĵ��� IOERR ��

�� IOERR ����ʱ�����������ֿ��ܣ�
?key ���ܴ���������ʵ��
?key ����ֻ�����ڵ�ǰʵ��

Ψһ�����ܷ�����������Ƕ�ʧ key ����ˣ����һ���ͻ���ִ�� MIGRATE ������Ҳ������� IOERR ������ô����ͻ���ΨһҪ���ľ��Ǽ���Լ����ݿ��ϵ� key �Ƿ��Ѿ�����ȷ��ɾ����

�������������������ô MIGRATE ��֤ key ֻ������ڵ�ǰʵ���С�����Ȼ��Ŀ��ʵ���ĸ������ݿ��Ͽ����к� key ͬ���ļ���������� MIGRATE ����û�й�ϵ����

��ѡ�
?COPY �����Ƴ�Դʵ���ϵ� key ��
?REPLACE ���滻Ŀ��ʵ�����Ѵ��ڵ� key ��
���ð汾��>= 2.6.0ʱ�临�Ӷȣ�

���������Դʵ����ʵ��ִ�� DUMP ����� DEL �����Ŀ��ʵ��ִ�� RESTORE ����鿴����������ĵ����Կ�����ϸ�ĸ��Ӷ�˵����

key ����������ʵ��֮�䴫��ĸ��Ӷ�Ϊ O(N) ��
����ֵ��Ǩ�Ƴɹ�ʱ���� OK �����򷵻���Ӧ�Ĵ���

ʾ��

���������� Redis ʵ����һ��ʹ��Ĭ�ϵ� 6379 �˿ڣ�һ��ʹ�� 7777 �˿ڡ�


$ ./redis-server &
[1] 3557

...

$ ./redis-server --port 7777 &
[2] 3560

...


Ȼ���ÿͻ������� 6379 �˿ڵ�ʵ��������һ������Ȼ����Ǩ�Ƶ� 7777 �˿ڵ�ʵ���ϣ�


$ ./redis-cli

redis 127.0.0.1:6379> flushdb
OK

redis 127.0.0.1:6379> SET greeting "Hello from 6379 instance"
OK

redis 127.0.0.1:6379> MIGRATE 127.0.0.1 7777 greeting 0 1000
OK

redis 127.0.0.1:6379> EXISTS greeting                           # Ǩ�Ƴɹ��� key ��ɾ��
(integer) 0


ʹ����һ���ͻ��ˣ��鿴 7777 �˿��ϵ�ʵ����


$ ./redis-cli -p 7777

redis 127.0.0.1:7777> GET greeting
"Hello from 6379 instance"


*/

/*
CLUSTER SETSLOT <slot> NODE <node_id> ���� slot ָ�ɸ� node_id ָ���Ľڵ㣬������Ѿ�ָ�ɸ���һ���ڵ㣬��ô������һ���ڵ�ɾ���ò�>��Ȼ���ٽ���ָ�ɡ�  
CLUSTER SETSLOT <slot> MIGRATING <node_id> �����ڵ�Ĳ� slot Ǩ�Ƶ� node_id ָ���Ľڵ��С�  
CLUSTER SETSLOT <slot> IMPORTING <node_id> �� node_id ָ���Ľڵ��е���� slot �����ڵ㡣  
CLUSTER SETSLOT <slot> STABLE ȡ���Բ� slot �ĵ��루import������Ǩ�ƣ�migrate����  

�����������MIGRATE host port key destination-db timeout replace���в�λresharding
�ο� redis�����ʵ�� ��17�� ��Ⱥ  17.4 ���·�Ƭ
*/

/* MIGRATE host port key dbid timeout [COPY | REPLACE] */
void migrateCommand(redisClient *c) {  //migrateCommand��restoreCommand��Ӧ
    //*select 0 +  (RESTORE-ASKING | RESTORE) + KEY-VALUE-EXPIRE + dump���л�value + [replace]   ��ӦrestoreCommand�Ը�KV�������л���ԭ
    int fd, copy, replace, j;
    long timeout;
    long dbid;
    long long ttl, expireat;
    robj *o;
    rio cmd, payload;
    int retry_num = 0;

try_again:
    /* Initialization */
    copy = 0;
    replace = 0;
    ttl = 0;

    /* Parse additional options */
    /* COPY �����Ƴ�Դʵ���ϵ�key��  REPLACE ���滻Ŀ��ʵ�����Ѵ��ڵ� key �� */
    
    // ���� COPY ���� REPLACE ѡ��
    for (j = 6; j < c->argc; j++) {
        if (!strcasecmp(c->argv[j]->ptr,"copy")) {
            copy = 1;
        } else if (!strcasecmp(c->argv[j]->ptr,"replace")) {
            replace = 1;
        } else {
            addReply(c,shared.syntaxerr);
            return;
        }
    }

    /* Sanity check */
    // ��������������ȷ��
    if (getLongFromObjectOrReply(c,c->argv[5],&timeout,NULL) != REDIS_OK)
        return;
    if (getLongFromObjectOrReply(c,c->argv[4],&dbid,NULL) != REDIS_OK)
        return;
    if (timeout <= 0) timeout = 1000;

    /* Check if the key is here. If not we reply with success as there is
     * nothing to migrate (for instance the key expired in the meantime), but
     * we include such information in the reply string. */
     /*
     Ȼ��ӿͻ��˵�ǰ���ӵ����ݿ��У�����key���õ���ֵ����o������Ҳ���key����ظ����ͻ���"+NOKEY"���ⲻ���Ǵ�����Ϊ���ܸ�key�պó�ʱ��ɾ���ˣ�
     */
     // ȡ������ֵ����
    if ((o = lookupKeyRead(c->db,c->argv[3])) == NULL) {
        addReplySds(c,sdsnew("+NOKEY\r\n"));
        return;
    }

    /* Connect */
     // ��ȡ�׽�������
    fd = migrateGetSocket(c,c->argv[1],c->argv[2],timeout);
    if (fd == -1) return; /* error sent to the client by migrateGetSocket() */

    /*
    ��ʼ����Ҫ���͸�Զ��Redis��RESTORE������ȳ�ʼ��rio�ṹ��cmd���ýṹ�м�¼Ҫ���͵������������
    ���е�dbid�����ϴ�Ǩ��ʱ��dbid��ͬ������Ҫ������cmd�����"SELECT  <dbid>"���Ȼ��ȡ�ø�key�ĳ�ʱʱ
    ��expireat������ת��Ϊ���ʱ��ttl�������ǰ���ڼ�Ⱥģʽ�£�����cmd�����"RESTORE-ASKING"���������
    ��"RESTORE"���Ȼ����cmd�����key���Լ�ttl��Ȼ�����createDumpPayload��������ֵ����o������DUMP�ĸ�
    ʽ��䵽payload�У�Ȼ���ٽ�payload��䵽cmd�У�������һ�����������REPLACE������Ҫ���"REPLACE"
    ��cmd�У�
    */

    //*select 0 +  (RESTORE-ASKING | RESTORE) + KEY-VALUE-EXPIRE + dump���л�value + [replace]  ��ӦrestoreCommand�Ը�KV�������л���ԭ
    
    /* Create RESTORE payload and generate the protocol to call the command. */
    // ��������ָ�����ݿ�� SELECT ��������ֵ�Ա���ԭ���˴���ĵط�
    rioInitWithBuffer(&cmd,sdsempty());
    redisAssertWithInfo(c,NULL,rioWriteBulkCount(&cmd,'*',2));
    redisAssertWithInfo(c,NULL,rioWriteBulkString(&cmd,"SELECT",6));
    redisAssertWithInfo(c,NULL,rioWriteBulkLongLong(&cmd,dbid));

    // ȡ�����Ĺ���ʱ���

    expireat = getExpire(c->db,c->argv[3]);
    if (expireat != -1) {
        ttl = expireat-mstime();
        if (ttl < 1) ttl = 1;
    } 

    //���Я��replace����*������5�������� key value expire restore����restore-asking replace,���Ϊ4û��replace
    redisAssertWithInfo(c,NULL,rioWriteBulkCount(&cmd,'*',replace ? 5 : 4)); 
    

     // ��������ڼ�Ⱥģʽ�£���ô���͵�����Ϊ RESTORE-ASKING    
     // ��������ڷǼ�Ⱥģʽ�£���ô���͵�����Ϊ RESTORE
    if (server.cluster_enabled)
        redisAssertWithInfo(c,NULL,
            rioWriteBulkString(&cmd,"RESTORE-ASKING",14));
    else
        redisAssertWithInfo(c,NULL,rioWriteBulkString(&cmd,"RESTORE",7));

    // д������͹���ʱ��
    redisAssertWithInfo(c,NULL,sdsEncodedObject(c->argv[3]));
    redisAssertWithInfo(c,NULL,rioWriteBulkString(&cmd,c->argv[3]->ptr,
            sdslen(c->argv[3]->ptr)));
    redisAssertWithInfo(c,NULL,rioWriteBulkLongLong(&cmd,ttl));

    /* Emit the payload argument, that is the serialized object using
     * the DUMP format. */
     // ��ֵ����������л�   
     createDumpPayload(&payload,o); //oΪvalueֵ����
     // д�����л�����
    redisAssertWithInfo(c,NULL,rioWriteBulkString(&cmd,payload.io.buffer.ptr,
                                sdslen(payload.io.buffer.ptr)));
    sdsfree(payload.io.buffer.ptr);

    /* Add the REPLACE option to the RESTORE command if it was specified
     * as a MIGRATE option. */
     // �Ƿ������� REPLACE ���  
     if (replace)       
     // д�� REPLACE ����
        redisAssertWithInfo(c,NULL,rioWriteBulkString(&cmd,"REPLACE",7));

    /* Transfer the query to the other node in 64K chunks. */
     // �� 64 kb ÿ�εĴ�С��Է���������
    errno = 0;
    {
        sds buf = cmd.io.buffer.ptr;
        size_t pos = 0, towrite;
        int nwritten = 0;

        //ע������������ʽ��д��һֱ�ȵ�������д�����д�쳣
        while ((towrite = sdslen(buf)-pos) > 0) { //һ����෢��64K���ݣ�ֻ���������
            towrite = (towrite > (64*1024) ? (64*1024) : towrite);
            nwritten = syncWrite(fd,buf+pos,towrite,timeout);
            if (nwritten != (signed)towrite) goto socket_wr_err;
            pos += nwritten;
        }
    }

    /* Read back the reply. */
        // ��ȡ����Ļظ�
    {
        char buf1[1024];
        char buf2[1024];

        /* Read the two replies */
        if (syncReadLine(fd, buf1, sizeof(buf1), timeout) <= 0)
            goto socket_rd_err;
        if (syncReadLine(fd, buf2, sizeof(buf2), timeout) <= 0)
            goto socket_rd_err;

        // ��� RESTORE ����ִ���Ƿ�ɹ�

        if (buf1[0] == '-' || buf2[0] == '-') {

            // ִ�г�������

            addReplyErrorFormat(c,"Target instance replied with error: %s",
                (buf1[0] == '-') ? buf1+1 : buf2+1);//��kvǨ����ɣ�����migrate����Ŀͻ���Ǩ�ƹ��߷���ERROR��Ϣ
        } else {

            // ִ�гɹ�������

            robj *aux;

            // ���û��ָ�� COPY ѡ���ôɾ���������ݿ��еļ�     ���سɹ���Ż��ڱ��ڵ�ɾ����KEY�����Բ�����KEY���������
            if (!copy) {
                /* No COPY option: remove the local key, signal the change. */
                dbDelete(c->db,c->argv[3]);
                signalModifiedKey(c->db,c->argv[3]);
            }
            addReply(c,shared.ok); //��kvǨ����ɣ�����migrate����Ŀͻ���Ǩ�ƹ��߷���OK
            server.dirty++;

            /* Translate MIGRATE as DEL for replication/AOF. */
             // �������ɾ���˵Ļ����� AOF �ļ��ʹӷ�����/�ڵ㷢��һ�� DEL ����
            aux = createStringObject("DEL",3);
            rewriteClientCommandVector(c,2,aux,c->argv[3]);
            decrRefCount(aux);
        }
    }

    sdsfree(cmd.io.buffer.ptr);
    return;

socket_wr_err:
    sdsfree(cmd.io.buffer.ptr);
    migrateCloseSocket(c->argv[1],c->argv[2]);
    if (errno != ETIMEDOUT && retry_num++ == 0) goto try_again;
    addReplySds(c,
        sdsnew("-IOERR error or timeout writing to target instance\r\n"));
    return;

socket_rd_err:
    sdsfree(cmd.io.buffer.ptr);
    migrateCloseSocket(c->argv[1],c->argv[2]);
    if (errno != ETIMEDOUT && retry_num++ == 0) goto try_again;
    addReplySds(c,
        sdsnew("-IOERR error or timeout reading from target node\r\n"));
    return;
}

/* -----------------------------------------------------------------------------
 * Cluster functions related to serving / redirecting clients
 * -------------------------------------------------------------------------- */

/* The ASKING command is required after a -ASK redirection.
 *
 * �ͻ����ڽӵ� -ASK ת��֮����Ҫ���� ASKING ���
 *
 * The client should issue ASKING before to actually send the command to
 * the target instance. See the Redis Cluster specification for more
 * information. 
 *
 * �ͻ���Ӧ������Ŀ��ڵ㷢������֮ǰ����ڵ㷢�� ASKING ��� 
 * ����ԭ����ο� Redis ��Ⱥ�淶��
 */

/*
���ͻ��˽��յ�ASK����ת�������ڵ���۵Ľڵ�ʱ���ͻ��˻�����ڵ㷢��һ��ASKING���Ȼ�������
������Ҫִ�е����������Ϊ����ͻ��˲�����ASKING�����ֱ�ӷ�����Ҫִ�е�����Ļ�����ô�ͻ��˷��͵�����
�����ڵ�ܾ�ִ�У�������MOVED����
*/
void askingCommand(redisClient *c) {

    if (server.cluster_enabled == 0) {
        addReplyError(c,"This instance has cluster support disabled");
        return;
    }

        // �򿪿ͻ��˵ı�ʶ
    c->flags |= REDIS_ASKING;

    addReply(c,shared.ok);
}

/* The READONLY command is uesd by clients to enter the read-only mode.
 * In this mode slaves will not redirect clients as long as clients access
 * with read-only commands to keys that are served by the slave's master. */
void readonlyCommand(redisClient *c) {
    if (server.cluster_enabled == 0) {
        addReplyError(c,"This instance has cluster support disabled");
        return;
    }
    c->flags |= REDIS_READONLY;
    addReply(c,shared.ok);
}

/* The READWRITE command just clears the READONLY command state. */
void readwriteCommand(redisClient *c) {
    c->flags &= ~REDIS_READONLY;
    addReply(c,shared.ok);
}

/* Return the pointer to the cluster node that is able to serve the command.
 * For the function to succeed the command should only target either:
 *
 * 1) A single key (even multiple times like LPOPRPUSH mylist mylist).
 * 2) Multiple keys in the same hash slot, while the slot is stable (no
 *    resharding in progress).
 *
 * On success the function returns the node that is able to serve the request.
 * If the node is not 'myself' a redirection must be perfomed. The kind of
 * redirection is specified setting the integer passed by reference
 * 'error_code', which will be set to REDIS_CLUSTER_REDIR_ASK or
 * REDIS_CLUSTER_REDIR_MOVED.
 *
 * When the node is 'myself' 'error_code' is set to REDIS_CLUSTER_REDIR_NONE.
 *
 * If the command fails NULL is returned, and the reason of the failure is
 * provided via 'error_code', which will be set to:
 *
 * REDIS_CLUSTER_REDIR_CROSS_SLOT if the request contains multiple keys that
 * don't belong to the same hash slot.
 *
 * REDIS_CLUSTER_REDIR_UNSTABLE if the request contains mutliple keys
 * belonging to the same slot, but the slot is not stable (in migration or
 * importing state, likely because a resharding is in progress). */
clusterNode *getNodeByQuery(redisClient *c, struct redisCommand *cmd, robj **argv, int argc, int *hashslot, int *error_code) {

     // ��ʼ��Ϊ NULL �� 
     // ��������������޲��������ô n �ͻ����Ϊ NULL
    clusterNode *n = NULL;

    robj *firstkey = NULL;
    int multiple_keys = 0;
    multiState *ms, _ms;
    multiCmd mc;
    int i, slot = 0, migrating_slot = 0, importing_slot = 0, missing_keys = 0;

    /* Set error code optimistically for the base case. */
    if (error_code) *error_code = REDIS_CLUSTER_REDIR_NONE;

    /* We handle all the cases as if they were EXEC commands, so we have
     * a common code path for everything */
      // ��Ⱥ����ִ������  
      // ������ȷ�������е�������������ĳ����ͬ�ļ����е�  
      // ��� if �ͽ������� for ���еľ�����һ�Ϸ��Լ��
    if (cmd->proc == execCommand) {
        /* If REDIS_MULTI flag is not set EXEC is just going to return an
         * error. */
        if (!(c->flags & REDIS_MULTI)) return myself;
        ms = &c->mstate;
    } else {
        /* In order to have a single codepath create a fake Multi State
         * structure if the client is not in MULTI/EXEC state, this way
         * we have a single codepath below. */
        ms = &_ms;
        _ms.commands = &mc;
        _ms.count = 1;
        mc.argv = argv;
        mc.argc = argc;
        mc.cmd = cmd;
    }

    /* Check that all the keys are in the same hash slot, and obtain this
     * slot and the node associated. */
    for (i = 0; i < ms->count; i++) {
        struct redisCommand *mcmd;
        robj **margv;
        int margc, *keyindex, numkeys, j;

        mcmd = ms->commands[i].cmd;
        margc = ms->commands[i].argc;
        margv = ms->commands[i].argv;

       // ��λ����ļ�λ��       

       keyindex = getKeysFromCommand(mcmd,margv,margc,&numkeys);       
       // ���������е����м�
        for (j = 0; j < numkeys; j++) {
            robj *thiskey = margv[keyindex[j]];
            int thisslot = keyHashSlot((char*)thiskey->ptr,
                                       sdslen(thiskey->ptr)); /* ����key����Ӧ��slot */

            if (firstkey == NULL) {
                 // ���������е�һ��������ļ�            
                 // ��ȡ�ü��Ĳۺ͸�����ò۵Ľڵ�
                /* This is the first key we see. Check what is the slot
                 * and node. */
                firstkey = thiskey;
                slot = thisslot;
                n = server.cluster->slots[slot];
                redisAssertWithInfo(c,firstkey,n != NULL);
                /* If we are migrating or importing this slot, we need to check
                 * if we have all the keys in the request (the only way we
                 * can safely serve the request, otherwise we return a TRYAGAIN
                 * error). To do so we set the importing/migrating state and
                 * increment a counter for every missing key. */
                if (n == myself &&
                    server.cluster->migrating_slots_to[slot] != NULL) 
     //��slot�Ѿ�����Ǩ����server.cluster->migrating_slots_to[slot]�ڵ����importing�ڵ�Ĺ����У������ĳ��key���͵��˱��ڵ㣬
     //����߶Է�ask server.cluster->migrating_slots_to[slot],Ҳ���Ǹ�key�Ĳ���Ӧ�÷��뵽����µ�Ŀ�Ľڵ��У���getNodeByQuery
                {
                    migrating_slot = 1;
                } else if (server.cluster->importing_slots_from[slot] != NULL) {
                    importing_slot = 1;
                }
            } else {
                /* If it is not the first key, make sure it is exactly
                 * the same key as the first we saw. */ 
                 //mget  mset del��������key������ͬһ��slot���棬���򱨴�-CROSSSLOT Keys in request don't hash to the same slot
                if (!equalStringObjects(firstkey,thiskey)) {
                    if (slot != thisslot) {
                        /* Error: multiple keys from different slots. */
                        getKeysFreeResult(keyindex);
                        if (error_code)
                            *error_code = REDIS_CLUSTER_REDIR_CROSS_SLOT;
                        return NULL;
                    } else {
                        /* Flag this request as one with multiple different
                         * keys. */
                        multiple_keys = 1;
                    }
                }
            }

            /* Migarting / Improrting slot? Count keys we don't have. */
            if ((migrating_slot || importing_slot) &&
                lookupKeyRead(&server.db[0],thiskey) == NULL)
            {
                missing_keys++;
            }
        }
        getKeysFreeResult(keyindex);
    }

    /* No key at all in command? then we can serve the request
     * without redirections or errors. */
    if (n == NULL) return myself;

    /* Return the hashslot by reference. */
    if (hashslot) *hashslot = slot;

    /* This request is about a slot we are migrating into another instance?
     * Then if we have all the keys. */

    /* If we don't have all the keys and we are migrating the slot, send
     * an ASK redirection. */
    if (migrating_slot && missing_keys) {
        if (error_code) *error_code = REDIS_CLUSTER_REDIR_ASK;
        return server.cluster->migrating_slots_to[slot];
    }

    /* If we are receiving the slot, and the client correctly flagged the
     * request as "ASKING", we can serve the request. However if the request
     * involves multiple keys and we don't have them all, the only option is
     * to send a TRYAGAIN error. */
    if (importing_slot &&
        (c->flags & REDIS_ASKING || cmd->flags & REDIS_CMD_ASKING))
    {
        if (multiple_keys && missing_keys) {
            if (error_code) *error_code = REDIS_CLUSTER_REDIR_UNSTABLE;
            return NULL;
        } else {
            return myself;
        }
    }

    /* Handle the read-only client case reading from a slave: if this
     * node is a slave and the request is about an hash slot our master
     * is serving, we can reply without redirection. */
    if (c->flags & REDIS_READONLY &&
        cmd->flags & REDIS_CMD_READONLY &&
        nodeIsSlave(myself) &&
        myself->slaveof == n)
    {
        return myself;
    }

    /* Base case: just return the right node. However if this node is not
     * myself, set error_code to MOVED since we need to issue a rediretion. */
    if (n != myself && error_code) *error_code = REDIS_CLUSTER_REDIR_MOVED;

    // ���ظ������ slot �Ľڵ� n
    return n;
}
