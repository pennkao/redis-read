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

#include "redis.h"
#include "cluster.h"
#include "slowlog.h"
#include "bio.h"

#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <locale.h>

/* Our shared "common" objects */

struct sharedObjectsStruct shared;//��Ӧ�ַ�����createSharedObjects

/* Global vars that are actually used as constants. The following double
 * values are used for double on-disk serialization, and are initialized
 * at runtime to avoid strange compiler optimizations. */

double R_Zero, R_PosInf, R_NegInf, R_Nan;

/*================================= Globals ================================= */

/* Global vars */
struct redisServer server; /* server global state */
struct redisCommand *commandTable;

/* Our command table.
 *
 * �����
 *
 * Every entry is composed of the following fields:
 *
 * ���е�ÿ�������������ɣ�
 *
 * name: a string representing the command name.
 *       ���������
 *
 * function: pointer to the C function implementing the command.
 *           һ��ָ�������ʵ�ֺ�����ָ��
 *
 * arity: number of arguments, it is possible to use -N to say >= N
 *        ������������������ -N ��ʾ >= N 
 *
 * sflags: command flags as string. See below for a table of flags.
 *         �ַ�����ʽ�� FLAG �������������µ���ʵ FLAG 
 *
 * flags: flags as bitmask. Computed by Redis using the 'sflags' field.
 *        λ������ʽ�� FLAG ������ sflags ���ַ�������ó�
 *
 * get_keys_proc: an optional function to get key arguments from a command.
 *                This is only used when the following three fields are not
 *                enough to specify what arguments are keys.
 *                һ����ѡ�ĺ��������ڴ�������ȡ�� key �����������������������������Ա�ʾ key ����ʱʹ��
 *
 * first_key_index: first argument that is a key
 *                  ��һ�� key ������λ��
 *
 * last_key_index: last argument that is a key
 *                 ���һ�� key ������λ��
 *
 * key_step: step to get all the keys from first to last argument. For instance
 *           in MSET the step is two since arguments are key,val,key,val,...
 *           �� first ������ last ����֮�䣬���� key �Ĳ�����step��
 *           ����˵�� MSET ����ĸ�ʽΪ MSET key value [key value ...]
 *           ���� step ��Ϊ 2
 *
 * microseconds: microseconds of total execution time for this command.
 *               ִ���������ķѵ���΢����
 *
 * calls: total number of calls of this command.
 *        ���ִ�е��ܴ���
 *
 * The flags, microseconds and calls fields are computed by Redis and should
 * always be set to zero.
 *
 * microseconds �� call �� Redis ���㣬���ǳ�ʼ��Ϊ 0 ��
 *
 * Command flags are expressed using strings where every character represents
 * a flag. Later the populateCommandTable() function will take care of
 * populating the real 'flags' field using this characters.
 *
 * ����� FLAG ������ SFLAG �����ã�֮�� populateCommandTable() ������ sflags �����м���������� FLAG �� flags �����С�
 *
 * This is the meaning of the flags:
 *
 * �����Ǹ��� FLAG �����壺
 *
 * w: write command (may modify the key space).
 *    д��������ܻ��޸� key space
 *
 * r: read command  (will never modify the key space).
 *    ��������޸� key space
 * m: may increase memory usage once called. Don't allow if out of memory.
 *    ���ܻ�ռ�ô����ڴ���������ʱ���ڴ�ռ�ý��м��
 *
 * a: admin command, like SAVE or SHUTDOWN.
 *    ������;��������� SAVE �� SHUTDOWN
 *
 * p: Pub/Sub related command.
 *    ����/������ص�����
 *
 * f: force replication of this command, regardless of server.dirty.
 *    ���� server.dirty ��ǿ�Ƹ���������
 *
 * s: command not allowed in scripts.
 *    �������ڽű���ʹ�õ�����
 *
 * R: random command. Command is not deterministic, that is, the same command
 *    with the same arguments, with the same key space, may have different
 *    results. For instance SPOP and RANDOMKEY are two random commands.
 *    ������
 *    �����Ƿ�ȷ���Եģ�����ͬ�������ͬ���Ĳ�����ͬ���ļ���������ܲ�ͬ��
 *    ���� SPOP �� RANDOMKEY �������������ӡ�
 *
 * S: Sort command output array if called from script, so that the output
 *    is deterministic.
 *    ��������� Lua �ű���ִ�У���ô������������򣬴Ӷ��ó�ȷ���Ե������
 *
 * l: Allow command while loading the database.
 *    �������������ݿ�ʱʹ�õ����
 *
 * t: Allow command while a slave has stale data but is not allowed to
 *    server this data. Normally no command is accepted in this condition
 *    but just a few.
 *    �����ڸ����ڵ���й�������ʱִ�е����
 *    ������������У�ֻ�м�����
 *
 * M: Do not automatically propagate the command on MONITOR.
 *    ��Ҫ�� MONITOR ģʽ���Զ��㲥�����
 *
 * k: Perform an implicit ASKING for this command, so the command will be
 *    accepted in cluster mode if the slot is marked as 'importing'.
 *    Ϊ�������ִ��һ����ʽ�� ASKING ��
 *    ʹ���ڼ�Ⱥģʽ�£�һ������ʾΪ importing �Ĳۿ��Խ��������
 */ 
/*
���г���sflags���Կ���ʹ�õı�ʶֵ���Լ���Щ��ʶ�����塣
    sflags���Եı�ʶ
�������������ש����������������������������������������ש�����������������������������������������
��    ��ʶ  ��    ����                                ��    ���������ʶ������                  ��
�ǩ����������贈���������������������������������������贈����������������������������������������
��    W     ��  ���ǡ���д��������ܻ��޸����ݿ�    ��  SET��RPUSH��DEL�ȵ�                   ��
�ǩ����������贈���������������������������������������贈����������������������������������������
��    r     ��  ���ǡ���ֻ����������޸����ݿ�      ��  GET. S7RLEN_ EXTSTS�ȵ�               ��
�ǩ����������贈���������������������������������������贈����������������������������������������
��          ��  ���������ܻ�ռ�ô����ڴ棬ִ��֮ǰ  ��                                        ��
��    m     ����Ҫ�ȼ����������ڴ�ʹ����������    ��  SET. APPEND. RPUSH. LPUSH. SADD.      ��
��          ��                                        ��SINTERSTORE�ȵ�                         ��
��          ���ڴ��ȱ�Ļ��ͽ�ִֹ���������          ��                                        ��
�ǩ����������贈���������������������������������������贈����������������������������������������
��    a     ��  ����һ����������                      ��  ��VE��BGSA VE��SHVTDOWN�ȵ�           ��
�ǩ����������贈���������������������������������������贈����������������������������������������
��    p     ��  ����һ�������붩˲���ܷ��������      ��  PUBLISH. SUBSCRIBE_ PUBSUB�ȵ�        ��
�ǩ����������贈���������������������������������������贈����������������������������������������
��    s     ��  ������������Lua�ű���ʹ��         ��BRPOP. BLPOP.  BRPOPLPUSH.  SPOP�ȵ�    ��
�ǩ����������贈���������������������������������������贈����������������������������������������
��    R     ��  ����һ��������������ͬ�����ݼ���  ��  SPOP. SRANDMEMBER . SSCAN.            ��
��          ����ͬ�Ĳ���������صĽ�����ܲ�ͬ      ��RANDOMKEY�ȵ�                           ��
�ǩ����������贈���������������������������������������贈����������������������������������������
��          ��  ����Lua�ű���ʹ���������ʱ������     ��                                        ��
��    S     �������������������һ������ʹ����    ��   SINTER _  SUNION.  SDIFF.  SMEMBERS. ��
��          ��                                        ��KEYS���                                ��
��          ����Ľ������                            ��                                        ��
�ǩ����������贈���������������������������������������贈����������������������������������������
��          ��  �����������ڷ������������ݵĹ�����  ��                                        ��
��    1     ��                                        ��  INFO��surrrDOW��PUBUSH�ȵ�            ��
��          ��ʹ��                                    ��                                        ��
�ǩ����������贈���������������������������������������贈����������������������������������������
��          ��  ���ǡ�������ӷ������ڴ��й�������ʱ  ��                                        ��
��    t     ��                                        ��  SLAVEOF��PING��INFO�ȵ�               ��
��          ��ʹ�õ�����                              ��                                        ��
�ǩ����������贈���������������������������������������贈����������������������������������������
��    M     ��  ��������ڼ�������rmmitor��ģʽ�²��� ��  EYEC                                  ��
��          ���Զ���������propagate��                 ��                                        ��
�������������ߩ����������������������������������������ߩ�����������������������������������������



    SET���������Ϊ��setn��ʵ�ֺ���ΪsetCommand������Ĳ�������Ϊ-3����ʾ����������������������Ĳ���������ı�ʶΪHwm����
��ʾSET������һ��д�����������ִ���������֮ǰ��������Ӧ�ö�ռ���ڴ�״�����м�飬��Ϊ���������ܻ�ռ�ô����ڴ档
    GET���������Ϊ��get����ʵ�ֺ���ΪgetCommand����������Ĳ�������Ϊ2����ʾ����ֻ������������������ı�ʶΪ��r������ʾ����һ��ֻ�����
*/

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

 //redisServer->orig_commands redisServer->commands(��populateCommandTable)������ֵ��е�Ԫ�����ݴ�redisCommandTable�л�ȡ�� processCommand->lookupCommand����
 //ʵ�ʿͻ�������Ĳ����ں���lookupCommandOrOriginal��  �ο�commandTableDictType���Կ�����dict��Ӧ��key�Ƚ��ǲ����ִ�Сд�� //�ͻ������������processMultibulkBuffer
struct redisCommand redisCommandTable[] = {  //sentinelcmds  redisCommandTable  �����ļ����ؼ�loadServerConfigFromString ���������ļ����ؼ�loadServerConfigFromStringsentinel
    {"get",getCommand,2,"r",0,NULL,1,1,1,0,0},
    {"set",setCommand,-3,"wm",0,NULL,1,1,1,0,0},
    {"setnx",setnxCommand,3,"wm",0,NULL,1,1,1,0,0},
    {"setex",setexCommand,4,"wm",0,NULL,1,1,1,0,0},
    {"psetex",psetexCommand,4,"wm",0,NULL,1,1,1,0,0},
    {"append",appendCommand,3,"wm",0,NULL,1,1,1,0,0},
    {"strlen",strlenCommand,2,"r",0,NULL,1,1,1,0,0},
    {"del",delCommand,-2,"w",0,NULL,1,-1,1,0,0},
    {"exists",existsCommand,2,"r",0,NULL,1,1,1,0,0},
    {"setbit",setbitCommand,4,"wm",0,NULL,1,1,1,0,0},
    {"getbit",getbitCommand,3,"r",0,NULL,1,1,1,0,0},
    {"setrange",setrangeCommand,4,"wm",0,NULL,1,1,1,0,0},
    {"getrange",getrangeCommand,4,"r",0,NULL,1,1,1,0,0},
    {"substr",getrangeCommand,4,"r",0,NULL,1,1,1,0,0},
    {"incr",incrCommand,2,"wm",0,NULL,1,1,1,0,0},
    {"decr",decrCommand,2,"wm",0,NULL,1,1,1,0,0},
    {"mget",mgetCommand,-2,"r",0,NULL,1,-1,1,0,0},
    {"rpush",rpushCommand,-3,"wm",0,NULL,1,1,1,0,0},
    {"lpush",lpushCommand,-3,"wm",0,NULL,1,1,1,0,0},
    {"rpushx",rpushxCommand,3,"wm",0,NULL,1,1,1,0,0},
    {"lpushx",lpushxCommand,3,"wm",0,NULL,1,1,1,0,0},
    {"linsert",linsertCommand,5,"wm",0,NULL,1,1,1,0,0},
    {"rpop",rpopCommand,2,"w",0,NULL,1,1,1,0,0},
    {"lpop",lpopCommand,2,"w",0,NULL,1,1,1,0,0},
    {"brpop",brpopCommand,-3,"ws",0,NULL,1,1,1,0,0},
    {"brpoplpush",brpoplpushCommand,4,"wms",0,NULL,1,2,1,0,0},
    {"blpop",blpopCommand,-3,"ws",0,NULL,1,-2,1,0,0},
    {"llen",llenCommand,2,"r",0,NULL,1,1,1,0,0},
    {"lindex",lindexCommand,3,"r",0,NULL,1,1,1,0,0},
    {"lset",lsetCommand,4,"wm",0,NULL,1,1,1,0,0},
    {"lrange",lrangeCommand,4,"r",0,NULL,1,1,1,0,0},
    {"ltrim",ltrimCommand,4,"w",0,NULL,1,1,1,0,0},
    {"lrem",lremCommand,4,"w",0,NULL,1,1,1,0,0},
    {"rpoplpush",rpoplpushCommand,3,"wm",0,NULL,1,2,1,0,0},
    {"sadd",saddCommand,-3,"wm",0,NULL,1,1,1,0,0},
    {"srem",sremCommand,-3,"w",0,NULL,1,1,1,0,0},
    {"smove",smoveCommand,4,"w",0,NULL,1,2,1,0,0},
    {"sismember",sismemberCommand,3,"r",0,NULL,1,1,1,0,0},
    {"scard",scardCommand,2,"r",0,NULL,1,1,1,0,0},
    {"spop",spopCommand,2,"wRs",0,NULL,1,1,1,0,0},
    {"srandmember",srandmemberCommand,-2,"rR",0,NULL,1,1,1,0,0},
    {"sinter",sinterCommand,-2,"rS",0,NULL,1,-1,1,0,0},
    {"sinterstore",sinterstoreCommand,-3,"wm",0,NULL,1,-1,1,0,0},
    {"sunion",sunionCommand,-2,"rS",0,NULL,1,-1,1,0,0},
    {"sunionstore",sunionstoreCommand,-3,"wm",0,NULL,1,-1,1,0,0},
    {"sdiff",sdiffCommand,-2,"rS",0,NULL,1,-1,1,0,0},
    {"sdiffstore",sdiffstoreCommand,-3,"wm",0,NULL,1,-1,1,0,0},
    {"smembers",sinterCommand,2,"rS",0,NULL,1,1,1,0,0},
    {"sscan",sscanCommand,-3,"rR",0,NULL,1,1,1,0,0},
    {"zadd",zaddCommand,-4,"wm",0,NULL,1,1,1,0,0},
    {"zincrby",zincrbyCommand,4,"wm",0,NULL,1,1,1,0,0},
    {"zrem",zremCommand,-3,"w",0,NULL,1,1,1,0,0},
    {"zremrangebyscore",zremrangebyscoreCommand,4,"w",0,NULL,1,1,1,0,0},
    {"zremrangebyrank",zremrangebyrankCommand,4,"w",0,NULL,1,1,1,0,0},
    {"zremrangebylex",zremrangebylexCommand,4,"w",0,NULL,1,1,1,0,0},
    {"zunionstore",zunionstoreCommand,-4,"wm",0,zunionInterGetKeys,0,0,0,0,0},
    {"zinterstore",zinterstoreCommand,-4,"wm",0,zunionInterGetKeys,0,0,0,0,0},
    {"zrange",zrangeCommand,-4,"r",0,NULL,1,1,1,0,0},
    {"zrangebyscore",zrangebyscoreCommand,-4,"r",0,NULL,1,1,1,0,0},
    {"zrevrangebyscore",zrevrangebyscoreCommand,-4,"r",0,NULL,1,1,1,0,0},
    {"zrangebylex",zrangebylexCommand,-4,"r",0,NULL,1,1,1,0,0},
    {"zrevrangebylex",zrevrangebylexCommand,-4,"r",0,NULL,1,1,1,0,0},
    {"zcount",zcountCommand,4,"r",0,NULL,1,1,1,0,0},
    {"zlexcount",zlexcountCommand,4,"r",0,NULL,1,1,1,0,0},
    {"zrevrange",zrevrangeCommand,-4,"r",0,NULL,1,1,1,0,0},
    {"zcard",zcardCommand,2,"r",0,NULL,1,1,1,0,0},
    {"zscore",zscoreCommand,3,"r",0,NULL,1,1,1,0,0},
    {"zrank",zrankCommand,3,"r",0,NULL,1,1,1,0,0},
    {"zrevrank",zrevrankCommand,3,"r",0,NULL,1,1,1,0,0},
    {"zscan",zscanCommand,-3,"rR",0,NULL,1,1,1,0,0},
    {"hset",hsetCommand,4,"wm",0,NULL,1,1,1,0,0},
    {"hsetnx",hsetnxCommand,4,"wm",0,NULL,1,1,1,0,0},
    {"hget",hgetCommand,3,"r",0,NULL,1,1,1,0,0},
    {"hmset",hmsetCommand,-4,"wm",0,NULL,1,1,1,0,0},
    {"hmget",hmgetCommand,-3,"r",0,NULL,1,1,1,0,0},
    {"hincrby",hincrbyCommand,4,"wm",0,NULL,1,1,1,0,0},
    {"hincrbyfloat",hincrbyfloatCommand,4,"wm",0,NULL,1,1,1,0,0},
    {"hdel",hdelCommand,-3,"w",0,NULL,1,1,1,0,0},
    {"hlen",hlenCommand,2,"r",0,NULL,1,1,1,0,0},
    {"hkeys",hkeysCommand,2,"rS",0,NULL,1,1,1,0,0},
    {"hvals",hvalsCommand,2,"rS",0,NULL,1,1,1,0,0},
    {"hgetall",hgetallCommand,2,"r",0,NULL,1,1,1,0,0},
    {"hexists",hexistsCommand,3,"r",0,NULL,1,1,1,0,0},
    {"hscan",hscanCommand,-3,"rR",0,NULL,1,1,1,0,0},
    {"incrby",incrbyCommand,3,"wm",0,NULL,1,1,1,0,0},
    {"decrby",decrbyCommand,3,"wm",0,NULL,1,1,1,0,0},
    {"incrbyfloat",incrbyfloatCommand,3,"wm",0,NULL,1,1,1,0,0},
    {"getset",getsetCommand,3,"wm",0,NULL,1,1,1,0,0},
    {"mset",msetCommand,-3,"wm",0,NULL,1,-1,2,0,0},
    {"msetnx",msetnxCommand,-3,"wm",0,NULL,1,-1,2,0,0},
    {"randomkey",randomkeyCommand,1,"rR",0,NULL,0,0,0,0,0},
    {"select",selectCommand,2,"rl",0,NULL,0,0,0,0,0},
    {"move",moveCommand,3,"w",0,NULL,1,1,1,0,0},
    {"rename",renameCommand,3,"w",0,NULL,1,2,1,0,0},
    {"renamenx",renamenxCommand,3,"w",0,NULL,1,2,1,0,0},
    {"expire",expireCommand,3,"w",0,NULL,1,1,1,0,0},
    {"expireat",expireatCommand,3,"w",0,NULL,1,1,1,0,0},
    {"pexpire",pexpireCommand,3,"w",0,NULL,1,1,1,0,0},
    {"pexpireat",pexpireatCommand,3,"w",0,NULL,1,1,1,0,0},
    {"keys",keysCommand,2,"rS",0,NULL,0,0,0,0,0},
    {"scan",scanCommand,-2,"rR",0,NULL,0,0,0,0,0},
    {"dbsize",dbsizeCommand,1,"r",0,NULL,0,0,0,0,0},
    {"auth",authCommand,2,"rslt",0,NULL,0,0,0,0,0},
    {"ping",pingCommand,1,"rt",0,NULL,0,0,0,0,0}, //�ӷ�������������������TCP���ӳɹ������ȷ���һ��ping�ַ���������̽����·�Ƿ��������
    {"echo",echoCommand,2,"r",0,NULL,0,0,0,0,0},
    /* SAVE���������Redis���������̣�ֱ��RDB�ļ��������Ϊֹ���ڷ��������������ڼ䣬���������ܴ����κ���������
    BGSAVE�����������һ���ӽ��̣�Ȼ�����ӽ��̸��𴴽�RDB�ļ������������̣������̣����������������� ����ִ��rdbSave */ //
    {"save",saveCommand,1,"ars",0,NULL,0,0,0,0,0}, //ע�����save�����⣬���и�save����
    {"bgsave",bgsaveCommand,1,"ar",0,NULL,0,0,0,0,0}, //bgsave������ִ�п���ͨ�������ļ��е�save���ã�����save 10 1 ��ʾ10���ڶ����ݿ����ٽ�����һ���޸ģ��򴥷�ִ��bgsave�����
/*
��Ϊ AOF ��������ʽ�ǲ��ϵؽ�����׷�ӵ��ļ���ĩβ�� ��������д������Ĳ������ӣ� AOF �ļ������Ҳ����Խ��Խ��
�ٸ����ӣ� ������һ�������������� 100 �� INCR �� ��ô������Ϊ�˱�������������ĵ�ǰֵ�� AOF �ļ�����Ҫʹ�� 100 ����¼��entry����
Ȼ����ʵ���ϣ� ֻʹ��һ�� SET �����Ѿ����Ա���������ĵ�ǰֵ�ˣ� ���� 99 ����¼ʵ���϶��Ƕ���ġ�
Ϊ�˴������������ Redis ֧��һ����Ȥ�����ԣ� �����ڲ���Ϸ���ͻ��˵�����£� �� AOF �ļ������ؽ���rebuild����
ִ�� BGREWRITEAOF ��� Redis ������һ���µ� AOF �ļ��� ����ļ������ؽ���ǰ���ݼ�������������
*/
    {"bgrewriteaof",bgrewriteaofCommand,1,"ar",0,NULL,0,0,0,0,0},
    {"shutdown",shutdownCommand,-1,"arlt",0,NULL,0,0,0,0,0},
    {"lastsave",lastsaveCommand,1,"rR",0,NULL,0,0,0,0,0},
    {"type",typeCommand,2,"r",0,NULL,1,1,1,0,0},

    /*
     WATCH������һ���ֹ���(optimistic locking)����������EXEC����ִ��֮ǰ�������������������ݿ��������EXEC����ִ��ʱ����鱻����
     �ļ��Ƿ�������һ���Ѿ����޸Ĺ��ˣ�����ǵĻ������������ܾ�ִ�����񣬲���ͻ��˷��ش�������ִ��ʧ�ܵĿջظ���Ҳ���Ǽ���multi��
     exec֮��ִ�е��������Ƿ����޸�watch xxx���ӵ�xxx�����������֮�����������޸�xxx�����򷵻�null���ܾ�ִ�и�������������
     */
     
    //watch��������multi��exec֮�������ִ����Ӱ��
    /* multi��exec�п�����Ӷ��������exec��ʱ������ִ�У������watch������Щ�����е��κ�һ����ֻ�б����ӵ�������һ����ִ�е�ʱ���޸ļ�����ֹͣ���������ִ�� */
//���ӻ��ƴ�����touchWatchedKey�����Ƿ񴥷�REDIS_DIRTY_CAS   ȡ�����ﺯ������watch�ļ��Ƿ��д���REDIS_DIRTY_CAS�������Ƿ����ִ�������е������execCommand
    {"multi",multiCommand,1,"rs",0,NULL,0,0,0,0,0},
    /*//multi��exec�е�����Ҫôȫ��ִ�гɹ���Ҫôһ������ִ�У���
    //ԭ����:�������ԭ����ָ���ǣ����ݿ⽫�����еĶ����������һ��������ִ�У�������Ҫô��ִ�������е����в�����Ҫô��һ������Ҳ��ִ�С�
    Redis������ʹ�ͳ�Ĺ�ϵ�����ݿ����������������ڣ�Redis��֧������ع�����( rollback)����ʹ��������е�ĳ��������ִ���ڼ�����˴���
    ��������Ҳ�����ִ����ȥ��ֱ������������е��������ִ�����Ϊֹ��
        �����ԭ����ָ������������ʽ���ͨ������û�б�watch���������һ����ִ��ȫ������������������ִ�С��������������������
    ��ԭ����ֻ�Ǳ�֤��������õ�ִ�У���������֤ÿ������ִ�й�����ʼ�ճɹ����������һ������ִ�з��ش��󣬻��ǻ����ִ�к�������
     */
    {"exec",execCommand,1,"sM",0,NULL,0,0,0,0,0},
    
    {"discard",discardCommand,1,"rs",0,NULL,0,0,0,0,0},
    {"sync",syncCommand,1,"ars",0,NULL,0,0,0,0,0},
    {"psync",syncCommand,3,"ars",0,NULL,0,0,0,0,0},
    {"replconf",replconfCommand,-1,"arslt",0,NULL,0,0,0,0,0},
    {"flushdb",flushdbCommand,1,"w",0,NULL,0,0,0,0,0},
    {"flushall",flushallCommand,1,"w",0,NULL,0,0,0,0,0},
    {"sort",sortCommand,-2,"wm",0,sortGetKeys,1,1,1,0,0},
    {"info",infoCommand,-1,"rlt",0,NULL,0,0,0,0,0},
    /* 
    �������������Ӹ÷���������ִ����monitor�ĵĿͻ��˷�������ÿ�δ�����������ǰ���������replicationFeedMonitors������
    ��������������������������������Ϣ���͸������������� 
    */
    {"monitor",monitorCommand,1,"ars",0,NULL,0,0,0,0,0},
    {"ttl",ttlCommand,2,"r",0,NULL,1,1,1,0,0},
    {"pttl",pttlCommand,2,"r",0,NULL,1,1,1,0,0},
    {"persist",persistCommand,2,"w",0,NULL,1,1,1,0,0}, //�Ƴ����� key ������ʱ�䣬����� key �ӡ���ʧ�ġ�(������ʱ�� key )ת���ɡ��־õġ�(һ����������ʱ�䡢�������ڵ� key )��
    {"slaveof",slaveofCommand,3,"ast",0,NULL,0,0,0,0,0},
    {"debug",debugCommand,-2,"as",0,NULL,0,0,0,0,0},
    {"config",configCommand,-2,"art",0,NULL,0,0,0,0,0},  //config setֱ��ִ�������ļ��е���������ֱ���ڴ���Ч  ��config set slowlog-max-len 5
    /*
     ���������õ�notify-keyspace-eventsѡ������˷�����������֪ͨ�����ͣ�
    �����÷����������������͵ļ��ռ�֪ͨ�ͼ��¼�֪ͨ�����Խ�ѡ���ֵ����ΪAKE��
    �����÷����������������͵ļ��ռ�֪ͨ�����Խ�ѡ���ֵ����ΪAK��
    �����÷����������������͵ļ��¼�֪ͨ�����Խ�ѡ���ֵ����ΪAE��
    �����÷�����ֻ���ͺ��ַ������йصļ��ռ�֪ͨ�����Խ�ѡ���ֵ����ΪK$��
    �����÷�����ֻ���ͺ��б���йصļ��¼�֪ͨ�����Խ�ѡ���ֵ����ΪEl��
     */

    /*
        ͨ��ִ��SUBSCRIBE����ͻ��˿��Զ���һ������Ƶ�����Ӷ���Ϊ��ЩƵ���Ķ�����( subscriber)��ÿ���������ͻ����򱻶��ĵ�Ƶ��
    ������Ϣ(message)ʱ��Ƶ�������ж����߶����յ�������Ϣ��unsubscribeȡ�����ġ�subscribe xx1 xx2��ʾ����xx1��xx2����Ƶ����unsubscribe xx1 xx2Ϊȡ������������

    �ͻ���1
127.0.0.1:6379> SUBSCRIBE test
Reading messages... (press Ctrl-C to quit)
1) "subscribe"
2) "test"
3) (integer) 1

��һ���ͻ���2ִ��PUBLISH(������)���ӡ����:
1) "message"
2) "test"
3) "ttttttttttttt"

�ͻ���2
127.0.0.1:6379> PUBLISH test ttttttttttttt
(integer) 1

     ���˶���Ƶ��֮�⣬�ͻ��˻�����ͨ��ִ��PSUBSCRIBE�����һ������ģʽ���Ӷ���Ϊ��Щģʽ�Ķ����ߣ�ÿ���������ͻ�����ĳ��Ƶ��������Ϣʱ��
     ��Ϣ�����ᱻ���͸����Ƶ�������ж����ߣ������ᱻ���͸����������Ƶ����ƥ���ģʽ�Ķ����ߡ�
     �ͻ���1����:psubscribe  aaa[12]c, �������ͻ���2publish aaa1c xxx����publish aaa2c xxx��ʱ�򣬿ͻ���1�����ܵ������Ϣ

     �ͻ��˶��ĵ�������ݽṹ�ο�redisServer->pubsub_channels
     PUBLISH <channel> <message>
     subscribe channel ---  unsubscribe   Ƶ������     
     psubscribe parttern---  punsubscribe  ģʽ����

     PUBSUB����ͻ��˿���ͨ������������鿴Ƶ������ģʽ�������Ϣ������ĳ��Ƶ��Ŀǰ�ж��ٶ����ߣ��ֻ���ĳ��ģʽĿǰ�ж��ٶ�����
     */
    {"subscribe",subscribeCommand,-2,"rpslt",0,NULL,0,0,0,0,0},//���ĸ�����һ������Ƶ������Ϣ�� notify-keyspace-events����ѡ������˷���������֪ͨ������
    {"unsubscribe",unsubscribeCommand,-1,"rpslt",0,NULL,0,0,0,0,0},
    {"psubscribe",psubscribeCommand,-2,"rpslt",0,NULL,0,0,0,0,0},
    {"punsubscribe",punsubscribeCommand,-1,"rpslt",0,NULL,0,0,0,0,0},
    {"publish",publishCommand,3,"pltr",0,NULL,0,0,0,0,0},
    {"pubsub",pubsubCommand,-2,"pltrR",0,NULL,0,0,0,0,0},

    /*
     WATCH������һ���ֹ���(optimistic locking)����������EXEC����ִ��֮ǰ�������������������ݿ��������EXEC����ִ��ʱ����鱻����
     �ļ��Ƿ�������һ���Ѿ����޸Ĺ��ˣ�����ǵĻ������������ܾ�ִ�����񣬲���ͻ��˷��ش�������ִ��ʧ�ܵĿջظ���Ҳ���Ǽ���multi��
     exec֮��ִ�е��������Ƿ����޸�watch xxx���ӵ�xxx�����������֮�����������޸�xxx�����򷵻�null���ܾ�ִ�и�������������
     */ 
     //���ӻ��ƴ�����touchWatchedKey�����Ƿ񴥷�REDIS_DIRTY_CAS   ȡ�����ﺯ������watch�ļ��Ƿ��д���REDIS_DIRTY_CAS�������Ƿ�
     //����ִ�������е������execCommand  //ȡ��watch��unwatchAllKeys
    {"watch",watchCommand,-2,"rs",0,NULL,1,-1,1,0,0},
    {"unwatch",unwatchCommand,1,"rs",0,NULL,0,0,0,0,0},
    
    {"cluster",clusterCommand,-2,"ar",0,NULL,0,0,0,0,0},
    {"restore",restoreCommand,-4,"awm",0,NULL,1,1,1,0,0},
    {"restore-asking",restoreCommand,-4,"awmk",0,NULL,1,1,1,0,0},
    {"migrate",migrateCommand,-6,"aw",0,NULL,0,0,0,0,0},
    {"asking",askingCommand,1,"r",0,NULL,0,0,0,0,0},
    {"readonly",readonlyCommand,1,"r",0,NULL,0,0,0,0,0},
    {"readwrite",readwriteCommand,1,"r",0,NULL,0,0,0,0,0},
    {"dump",dumpCommand,2,"ar",0,NULL,1,1,1,0,0},
    {"object",objectCommand,-2,"r",0,NULL,2,2,2,0,0},
    {"client",clientCommand,-2,"ar",0,NULL,0,0,0,0,0},
    {"eval",evalCommand,-3,"s",0,evalGetKeys,0,0,0,0,0},
    {"evalsha",evalShaCommand,-3,"s",0,evalGetKeys,0,0,0,0,0},
    {"slowlog",slowlogCommand,-2,"r",0,NULL,0,0,0,0,0},
    {"script",scriptCommand,-2,"ras",0,NULL,0,0,0,0,0},
    {"time",timeCommand,1,"rR",0,NULL,0,0,0,0,0},
    {"bitop",bitopCommand,-4,"wm",0,NULL,2,-1,1,0,0},
    {"bitcount",bitcountCommand,-2,"r",0,NULL,1,1,1,0,0},
    {"bitpos",bitposCommand,-3,"r",0,NULL,1,1,1,0,0},
    {"wait",waitCommand,3,"rs",0,NULL,0,0,0,0,0},
    {"pfselftest",pfselftestCommand,1,"r",0,NULL,0,0,0,0,0},
    {"pfadd",pfaddCommand,-2,"wm",0,NULL,1,1,1,0,0},
    {"pfcount",pfcountCommand,-2,"w",0,NULL,1,1,1,0,0},
    {"pfmerge",pfmergeCommand,-2,"wm",0,NULL,1,-1,1,0,0},
    {"pfdebug",pfdebugCommand,-3,"w",0,NULL,0,0,0,0,0}
};

struct evictionPoolEntry *evictionPoolAlloc(void);

/*============================ Utility functions ============================ */

/* Low level logging. To use only for very big messages, otherwise
 * redisLog() is to prefer. */
void redisLogRaw(int level, const char *msg) { //��־�����Ǵ��ļ���д�룬Ȼ��رգ�����Ӧ�ÿ����Ż���ֻ���һ�μ���
    const int syslogLevelMap[] = { LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING };
    const char *c = ".-*#";
    FILE *fp;
    char buf[64];
    int rawmode = (level & REDIS_LOG_RAW);
    int log_to_stdout = server.logfile[0] == '\0';

    level &= 0xff; /* clear flags */
    if (level < server.verbosity) return;

    fp = log_to_stdout ? stdout : fopen(server.logfile,"a");
    if (!fp) return;

    if (rawmode) {
        fprintf(fp,"%s",msg);
    } else {
        int off;
        struct timeval tv;

        gettimeofday(&tv,NULL);
        off = strftime(buf,sizeof(buf),"%d %b %H:%M:%S.",localtime(&tv.tv_sec));
        snprintf(buf+off,sizeof(buf)-off,"%03d",(int)tv.tv_usec/1000);
        fprintf(fp,"[%d] %s %c %s\n",(int)getpid(),buf,c[level],msg);
    }
    fflush(fp);

    if (!log_to_stdout) fclose(fp);
    if (server.syslog_enabled) syslog(syslogLevelMap[level], "%s", msg);
}

/* Like redisLogRaw() but with printf-alike support. This is the function that
 * is used across the code. The raw version is only used in order to dump
 * the INFO output on crash. */
void redisLog(int level, const char *fmt, ...) {
    va_list ap;
    char msg[REDIS_MAX_LOGMSG_LEN];

    if ((level&0xff) < server.verbosity) return;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    redisLogRaw(level,msg);
}

/* Log a fixed message without printf-alike capabilities, in a way that is
 * safe to call from a signal handler.
 *
 * We actually use this only for signals that are not fatal from the point
 * of view of Redis. Signals that are going to kill the server anyway and
 * where we need printf-alike features are served by redisLog(). */
void redisLogFromHandler(int level, const char *msg) {
    int fd;
    int log_to_stdout = server.logfile[0] == '\0';
    char buf[64];

    if ((level&0xff) < server.verbosity || (log_to_stdout && server.daemonize))
        return;
    fd = log_to_stdout ? STDOUT_FILENO : 
                         open(server.logfile, O_APPEND|O_CREAT|O_WRONLY, 0644);
    if (fd == -1) return;
    ll2string(buf,sizeof(buf),getpid());
    if (write(fd,"[",1) == -1) goto err;
    if (write(fd,buf,strlen(buf)) == -1) goto err;
    if (write(fd," | signal handler] (",20) == -1) goto err;
    ll2string(buf,sizeof(buf),time(NULL));
    if (write(fd,buf,strlen(buf)) == -1) goto err;
    if (write(fd,") ",2) == -1) goto err;
    if (write(fd,msg,strlen(msg)) == -1) goto err;
    if (write(fd,"\n",1) == -1) goto err;
err:
    if (!log_to_stdout) close(fd);
}

/* Return the UNIX time in microseconds */
// ����΢���ʽ�� UNIX ʱ��
// 1 �� = 1 000 000 ΢��
long long ustime(void) {
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec)*1000000;
    ust += tv.tv_usec;
    return ust;
}

/* Return the UNIX time in milliseconds */
// ���غ����ʽ�� UNIX ʱ��
// 1 �� = 1 000 ����
long long mstime(void) {
    return ustime()/1000;
}

/* After an RDB dump or AOF rewrite we exit from children using _exit() instead of
 * exit(), because the latter may interact with the same file objects used by
 * the parent process. However if we are testing the coverage normal exit() is
 * used in order to obtain the right coverage information. */
void exitFromChild(int retcode) {
#ifdef COVERAGE_TEST
    exit(retcode);
#else
    _exit(retcode);
#endif
}

/*====================== Hash table type implementation  ==================== */

/* This is a hash table type that uses the SDS dynamic strings library as
 * keys and radis objects as values (objects can hold SDS strings,
 * lists, sets). */

void dictVanillaFree(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);
    zfree(val);
}

void dictListDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);
    listRelease((list*)val);
}

int dictSdsKeyCompare(void *privdata, const void *key1,
        const void *key2)
{
    int l1,l2;
    DICT_NOTUSED(privdata);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

/* A case insensitive version used for the command lookup table and other
 * places where case insensitive non binary-safe comparison is needed. */
int dictSdsKeyCaseCompare(void *privdata, const void *key1,
        const void *key2)
{
    DICT_NOTUSED(privdata);

    return strcasecmp(key1, key2) == 0;
}

void dictRedisObjectDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    if (val == NULL) return; /* Values of swapped out keys as set to NULL */
    decrRefCount(val);
}

void dictSdsDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    sdsfree(val);
}

int dictObjKeyCompare(void *privdata, const void *key1,
        const void *key2)
{
    const robj *o1 = key1, *o2 = key2;
    return dictSdsKeyCompare(privdata,o1->ptr,o2->ptr);
}

unsigned int dictObjHash(const void *key) {
    const robj *o = key;
    return dictGenHashFunction(o->ptr, sdslen((sds)o->ptr));
}

unsigned int dictSdsHash(const void *key) {
    return dictGenHashFunction((unsigned char*)key, sdslen((char*)key));
}

unsigned int dictSdsCaseHash(const void *key) {
    return dictGenCaseHashFunction((unsigned char*)key, sdslen((char*)key));
}

int dictEncObjKeyCompare(void *privdata, const void *key1,
        const void *key2)
{
    robj *o1 = (robj*) key1, *o2 = (robj*) key2;
    int cmp;

    if (o1->encoding == REDIS_ENCODING_INT &&
        o2->encoding == REDIS_ENCODING_INT)
            return o1->ptr == o2->ptr;

    o1 = getDecodedObject(o1);
    o2 = getDecodedObject(o2);
    cmp = dictSdsKeyCompare(privdata,o1->ptr,o2->ptr);
    decrRefCount(o1);
    decrRefCount(o2);
    return cmp;
}

unsigned int dictEncObjHash(const void *key) {
    robj *o = (robj*) key;

    if (sdsEncodedObject(o)) {
        return dictGenHashFunction(o->ptr, sdslen((sds)o->ptr));
    } else {
        if (o->encoding == REDIS_ENCODING_INT) {
            char buf[32];
            int len;

            len = ll2string(buf,32,(long)o->ptr);
            return dictGenHashFunction((unsigned char*)buf, len);
        } else {
            unsigned int hash;

            o = getDecodedObject(o);
            hash = dictGenHashFunction(o->ptr, sdslen((sds)o->ptr));
            decrRefCount(o);
            return hash;
        }
    }
}

/* Sets type hash table */
dictType setDictType = {
    dictEncObjHash,            /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dictEncObjKeyCompare,      /* key compare */
    dictRedisObjectDestructor, /* key destructor */
    NULL                       /* val destructor */
};

/* Sorted sets hash (note: a skiplist is used in addition to the hash table) */
dictType zsetDictType = {
    dictEncObjHash,            /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dictEncObjKeyCompare,      /* key compare */
    dictRedisObjectDestructor, /* key destructor */
    NULL                       /* val destructor */
};

/* Db->dict, keys are sds strings, vals are Redis objects. */
dictType dbDictType = {
    dictSdsHash,                /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictSdsKeyCompare,          /* key compare */
    dictSdsDestructor,          /* key destructor */
    dictRedisObjectDestructor   /* val destructor */
};

/* server.lua_scripts sha (as sds string) -> scripts (as robj) cache. */
dictType shaScriptObjectDictType = {
    dictSdsCaseHash,            /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictSdsKeyCaseCompare,      /* key compare */
    dictSdsDestructor,          /* key destructor */
    dictRedisObjectDestructor   /* val destructor */
};

/* Db->expires */
dictType keyptrDictType = {
    dictSdsHash,               /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dictSdsKeyCompare,         /* key compare */
    NULL,                      /* key destructor */
    NULL                       /* val destructor */
};

/* Command table. sds string -> command struct pointer. */
dictType commandTableDictType = {
    dictSdsCaseHash,           /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dictSdsKeyCaseCompare,     /* key compare */
    dictSdsDestructor,         /* key destructor */
    NULL                       /* val destructor */
};

/* Hash type hash table (note that small hashes are represented with ziplists) */
dictType hashDictType = {
    dictEncObjHash,             /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictEncObjKeyCompare,       /* key compare */
    dictRedisObjectDestructor,  /* key destructor */
    dictRedisObjectDestructor   /* val destructor */
};

/* Keylist hash table type has unencoded redis objects as keys and
 * lists as values. It's used for blocking operations (BLPOP) and to
 * map swapped keys to a list of clients waiting for this keys to be loaded. */
dictType keylistDictType = {
    dictObjHash,                /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictObjKeyCompare,          /* key compare */
    dictRedisObjectDestructor,  /* key destructor */
    dictListDestructor          /* val destructor */
};

/* Cluster nodes hash table, mapping nodes addresses 1.2.3.4:6379 to
 * clusterNode structures. */
dictType clusterNodesDictType = {
    dictSdsHash,                /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictSdsKeyCompare,          /* key compare */
    dictSdsDestructor,          /* key destructor */
    NULL                        /* val destructor */
};

/* Cluster re-addition blacklist. This maps node IDs to the time
 * we can re-add this node. The goal is to avoid readding a removed
 * node for some time. */
dictType clusterNodesBlackListDictType = {
    dictSdsCaseHash,            /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictSdsKeyCaseCompare,      /* key compare */
    dictSdsDestructor,          /* key destructor */
    NULL                        /* val destructor */
};

/* Migrate cache dict type. */
dictType migrateCacheDictType = {
    dictSdsHash,                /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictSdsKeyCompare,          /* key compare */
    dictSdsDestructor,          /* key destructor */
    NULL                        /* val destructor */
};

/* Replication cached script dict (server.repl_scriptcache_dict).
 * Keys are sds SHA1 strings, while values are not used at all in the current
 * implementation. */
dictType replScriptCacheDictType = {
    dictSdsCaseHash,            /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictSdsKeyCaseCompare,      /* key compare */
    dictSdsDestructor,          /* key destructor */
    NULL                        /* val destructor */
};

int htNeedsResize(dict *dict) {
    long long size, used;

    size = dictSlots(dict);
    used = dictSize(dict);
    return (size && used && size > DICT_HT_INITIAL_SIZE &&
            (used*100/size < REDIS_HT_MINFILL));
}

/* If the percentage of used slots in the HT reaches REDIS_HT_MINFILL
 * we resize the hash table to save memory */
// ����ֵ��ʹ���ʱ� REDIS_HT_MINFILL ����Ҫ��
// ��ôͨ����С�ֵ���������Լ�ڴ�
/* rehash������ù�ϵ���ù���:dictAddRaw->_dictKeyIndex->_dictExpandIfNeeded(��������Ƿ���Ҫ��չhash)->dictExpand 
����hash����:serverCron->tryResizeHashTables->dictResize(��������������Ͱ��)->dictExpand */

void tryResizeHashTables(int dbid) {
    if (htNeedsResize(server.db[dbid].dict))
        dictResize(server.db[dbid].dict);
    if (htNeedsResize(server.db[dbid].expires))
        dictResize(server.db[dbid].expires);
}

/* Our hash table implementation performs rehashing incrementally while
 * we write/read from the hash table. Still if the server is idle, the hash
 * table will use two tables for a long time. So we try to use 1 millisecond
 * of CPU time at every call of this function to perform some rehahsing.
 *
 * ��Ȼ�������ڶ����ݿ�ִ�ж�ȡ/д������ʱ������ݿ���н���ʽ rehash ��
 * ���������������û��ִ������Ļ������ݿ��ֵ�� rehash �Ϳ���һֱû�취��ɣ�
 * Ϊ�˷�ֹ�������������������Ҫ�����ݿ�ִ������ rehash ��
 *
 * The function returns 1 if some rehashing was performed, otherwise 0
 * is returned. 
 *
 * ������ִ�������� rehash ʱ���� 1 �����򷵻� 0 ��
 */
int incrementallyRehash(int dbid) {

    /* Keys dictionary */
    if (dictIsRehashing(server.db[dbid].dict)) {
        dictRehashMilliseconds(server.db[dbid].dict,1);
        return 1; /* already used our millisecond for this loop... */
    }

    /* Expires */
    if (dictIsRehashing(server.db[dbid].expires)) {
        dictRehashMilliseconds(server.db[dbid].expires,1);
        return 1; /* already used our millisecond for this loop... */
    }

    return 0;
}

/* This function is called once a background process of some kind terminates,
 * as we want to avoid resizing the hash tables when there is a child in order
 * to play well with copy-on-write (otherwise when a resize happens lots of
 * memory pages are copied). The goal of this function is to update the ability
 * for dict.c to resize the hash tables accordingly to the fact we have o not
 * running childs. */
void updateDictResizePolicy(void) {
    if (server.rdb_child_pid == -1 && server.aof_child_pid == -1)
        dictEnableResize();
    else
        dictDisableResize();
}

/* ======================= Cron: called every 100 ms ======================== */

/* Helper function for the activeExpireCycle() function.
 * This function will try to expire the key that is stored in the hash table
 * entry 'de' of the 'expires' hash table of a Redis database.
 *
 * activeExpireCycle() ����ʹ�õļ����Ƿ���ڵĸ���������
 *
 * If the key is found to be expired, it is removed from the database and
 * 1 is returned. Otherwise no operation is performed and 0 is returned.
 *
 * ��� de �еļ��Ѿ����ڣ���ô�Ƴ����������� 1 �������������������� 0 ��
 *
 * When a key is expired, server.stat_expiredkeys is incremented.
 *
 * The parameter 'now' is the current time in milliseconds as is passed
 * to the function to avoid too many gettimeofday() syscalls.
 *
 * ���� now �Ǻ����ʽ�ĵ�ǰʱ��
 */ //Redis�ӳټ�ؿ�����http://ghoulich.xninja.org/2016/12/08/how-to-use-latency-monitor-in-redis/
int activeExpireCycleTryExpire(redisDb *db, dictEntry *de, long long now) {
    // ��ȡ���Ĺ���ʱ��
    long long t = dictGetSignedIntegerVal(de);
    if (now > t) {

        // ���ѹ���

        sds key = dictGetKey(de);
        robj *keyobj = createStringObject(key,sdslen(key));

        // ������������
        propagateExpire(db,keyobj); //��������Ҫ�����ӹ��ڣ�
        // �����ݿ���ɾ���ü�
        dbDelete(db,keyobj);
        // �����¼�
        notifyKeyspaceEvent(REDIS_NOTIFY_EXPIRED,
            "expired",keyobj,db->id);
        decrRefCount(keyobj);
        // ���¼�����
        server.stat_expiredkeys++;
        return 1;
    } else {

        // ��δ����
        return 0;
    }
}

/* Try to expire a few timed out keys. The algorithm used is adaptive and
 * will use few CPU cycles if there are few expiring keys, otherwise
 * it will get more aggressive to avoid that too much memory is used by
 * keys that can be removed from the keyspace.
 *
 * ��������ɾ�����ݿ����Ѿ����ڵļ���
 * �����й���ʱ��ļ��Ƚ���ʱ���������еñȽϱ��أ�
 * ������й���ʱ��ļ��Ƚ϶࣬��ô�������Ը������ķ�ʽ��ɾ�����ڼ���
 * �Ӷ����ܵ��ͷű����ڼ�ռ�õ��ڴ档
 *
 * No more than REDIS_DBCRON_DBS_PER_CALL databases are tested at every
 * iteration.
 *
 * ÿ��ѭ���б����Ե����ݿ���Ŀ���ᳬ�� REDIS_DBCRON_DBS_PER_CALL ��
 *
 * This kind of call is used when Redis detects that timelimit_exit is
 * true, so there is more work to do, and we do it more incrementally from
 * the beforeSleep() function of the event loop.
 *
 * ��� timelimit_exit Ϊ�棬��ô˵�����и���ɾ������Ҫ����
 * ��ô�� beforeSleep() ��������ʱ��������ٴ�ִ�����������
 *
 * Expire cycle type:
 *
 * ����ѭ�������ͣ�
 *
 * If type is ACTIVE_EXPIRE_CYCLE_FAST the function will try to run a
 * "fast" expire cycle that takes no longer than EXPIRE_FAST_CYCLE_DURATION
 * microseconds, and is not repeated again before the same amount of time.
 *
 * ���ѭ��������Ϊ ACTIVE_EXPIRE_CYCLE_FAST ��
 * ��ô�������ԡ����ٹ��ڡ�ģʽִ�У�
 * ִ�е�ʱ�䲻�᳤�� EXPIRE_FAST_CYCLE_DURATION ���룬
 * ������ EXPIRE_FAST_CYCLE_DURATION ����֮�ڲ���������ִ�С�
 *
 * If type is ACTIVE_EXPIRE_CYCLE_SLOW, that normal expire cycle is
 * executed, where the time limit is a percentage of the REDIS_HZ period
 * as specified by the REDIS_EXPIRELOOKUPS_TIME_PERC define. 
 *
 * ���ѭ��������Ϊ ACTIVE_EXPIRE_CYCLE_SLOW ��
 * ��ô�������ԡ��������ڡ�ģʽִ�У�
 * ������ִ��ʱ��Ϊ REDIS_HS ������һ���ٷֱȣ�
 * ����ٷֱ��� REDIS_EXPIRELOOKUPS_TIME_PERC ���塣
 */
/*
���ڼ��Ķ���ɾ��������redis.c/activeExpireCycle����ʵ�֣�ÿ��Redis��
�����������Բ���redis��c/serverCron����ִ��ʱ��activeExpireCycle�����ͻ�
�����ã����ڹ涨��ʱ���ڣ��ֶ�α����������еĸ������ݿ⣬�����ݿ��expires��
����������һ���ּ��Ĺ���ʱ�䣬��ɾ�����еĹ��ڼ���
*/ //���ȡ�����ֵ�hash�еĽڵ㣬Ȼ����г�ʱ�ж�ɾ�����˳������ǣ��ú������ִ�ж���ʱ�䣬�����Ѿ�ɾ��������޶ȸ����ڼ����˳�
//ע��activeExpireCycle(����ɾ��)��freeMemoryIfNeeded(�������������ڴ棬�������ڴ���)  expireIfNeeded(��������ɾ�����ɶԸü�������ʱ������ж��Ƿ�ʱ)������
void activeExpireCycle(int type) { //���ڼ��Ķ���ɾ�� //ע��activeExpireCycle��freeMemoryIfNeeded  expireIfNeeded������
    //ֻ��master�Ż�����ʱ����������������ǲ�������ʱ��������ģ�slave�����������ں���del������������й��ڣ���activeExpireCycleTryExpire->propagateExpire

    
    /* This function has some global state in order to continue the work
     * incrementally across calls. */
    // ��̬�����������ۻ���������ִ��ʱ������
    static unsigned int current_db = 0; /* Last DB tested. */
    static int timelimit_exit = 0;      /* Time limit hit in previous call? */
    static long long last_fast_cycle = 0; /* When last fast cycle ran. */

    unsigned int j, iteration = 0;
    // Ĭ��ÿ�δ�������ݿ�����
    unsigned int dbs_per_call = REDIS_DBCRON_DBS_PER_CALL;
    // ������ʼ��ʱ��
    long long start = ustime(), 
        timelimit; //����ڸú���������timelimit usʱ��

    // ����ģʽ
    if (type == ACTIVE_EXPIRE_CYCLE_FAST) { 
    //ֻ���ϴ�ִ�иú�������Ϊ�ڸú����к�ʱ������ָ��ʱ�䣬�����������ϴ�ִ�иú�����ʱ�䳬��2ms��ʱ��Ż�����fastģʽ
        /* Don't start a fast cycle if the previous cycle did not exited
         * for time limt. Also don't repeat a fast cycle for the same period
         * as the fast cycle total duration itself. */
        // ����ϴκ���û�д��� timelimit_exit ����ô��ִ�д���
        if (!timelimit_exit) return; //����ϴ�ִ�иú���������Ϊִ�иú�����ʱ�䳬��ָ��ʱ������ģ�����Чfast����
        // ��������ϴ�ִ��δ��һ��ʱ�䣬��ô��ִ�д���
        if (start < last_fast_cycle + ACTIVE_EXPIRE_CYCLE_FAST_DURATION*2) return;
        // ���е����˵��ִ�п��ٴ�����¼��ǰʱ��
        last_fast_cycle = start;
    }

    /* We usually should test REDIS_DBCRON_DBS_PER_CALL per iteration, with
     * two exceptions:
     *
     * һ������£�����ֻ���� REDIS_DBCRON_DBS_PER_CALL �����ݿ⣬
     * ���ǣ�
     *
     * 1) Don't test more DBs than we have.
     *    ��ǰ���ݿ������С�� REDIS_DBCRON_DBS_PER_CALL
     * 2) If last time we hit the time limit, we want to scan all DBs
     * in this iteration, as there is work to do in some DB and we don't want
     * expired keys to use memory for too much time. 
     *     ����ϴδ���������ʱ�����ޣ���ô�����Ҫ���������ݿ����ɨ�裬
     *     ����Ա������Ĺ��ڼ�ռ�ÿռ�
     */
    if (dbs_per_call > server.dbnum || timelimit_exit)
        dbs_per_call = server.dbnum;

    /* We can use at max ACTIVE_EXPIRE_CYCLE_SLOW_TIME_PERC percentage of CPU time
     * per iteration. Since this function gets called with a frequency of
     * server.hz times per second, the following is the max amount of
     * microseconds we can spend in this function. */
    // ���������΢��ʱ������
    // ACTIVE_EXPIRE_CYCLE_SLOW_TIME_PERC Ĭ��Ϊ 25 ��Ҳ���� 25 % �� CPU ʱ��
    timelimit = 1000000*ACTIVE_EXPIRE_CYCLE_SLOW_TIME_PERC/server.hz/100; //25ms  25000us
    timelimit_exit = 0;
    if (timelimit <= 0) timelimit = 1;

    // ����������ڿ���ģʽ֮��
    // ��ô���ֻ������ FAST_DURATION ΢�� 
    // Ĭ��ֵΪ 1000 ��΢�룩
    if (type == ACTIVE_EXPIRE_CYCLE_FAST)
        timelimit = ACTIVE_EXPIRE_CYCLE_FAST_DURATION; /* in microseconds. */ //1MS

    // �������ݿ�
    for (j = 0; j < dbs_per_call; j++) {
        int expired; //���ڼ�ɾ���ĸ���
        // ָ��Ҫ��������ݿ�
        redisDb *db = server.db+(current_db % server.dbnum);

        /* Increment the DB now so we are sure if we run out of time
         * in the current DB we'll restart from the next. This allows to
         * distribute the time evenly across DBs. */
        // Ϊ DB ��������һ��������� do ѭ��֮����Ϊ��ʱ������
        // ��ô�´λ�ֱ�Ӵ��¸� DB ��ʼ����
        current_db++;

        /* Continue to expire if at the end of the cycle more than 25%
         * of the keys were expired. */
        do {
            unsigned long num, slots;
            long long now, ttl_sum;
            int ttl_samples;

            /* If there is nothing to expire try next DB ASAP. */
            // ��ȡ���ݿ��д�����ʱ��ļ�������
            // ���������Ϊ 0 ��ֱ������������ݿ�
            if ((num = dictSize(db->expires)) == 0) { //num��ȡͰ����
                db->avg_ttl = 0;
                break;
            }
            // ��ȡ���ݿ��м�ֵ�Ե�����  ��ȡ����Ͱ�г�Ա��
            slots = dictSlots(db->expires);
            // ��ǰʱ��
            now = mstime();

            /* When there are less than 1% filled slots getting random
             * keys is expensive, so stop here waiting for better times...
             * The dictionary will be resized asap. */
            // ������ݿ��ʹ���ʵ��� 1% ��ɨ������̫�����ˣ��󲿷ֶ��� MISS��
            // �������ȴ��ֵ�������������
            if (num && slots > DICT_HT_INITIAL_SIZE &&
                (num*100/slots < 1)) break;  //Ҳ����ÿ������Ͱ��ƽ��������100����Ա�����棬���ɨ�����������������Ҫ����Ͱ��������ʾɨ������ٵ�

            /* The main collection cycle. Sample random keys among keys
             * with an expire set, checking for expired ones. 
             *
             * ����������
             */
            // �Ѵ�����ڼ�������
            expired = 0;
            // ������ TTL ������
            ttl_sum = 0;
            // �ܹ�����ļ�������
            ttl_samples = 0;

            // ÿ�����ֻ�ܼ�� LOOKUPS_PER_LOOP ����
            if (num > ACTIVE_EXPIRE_CYCLE_LOOKUPS_PER_LOOP) //20
                num = ACTIVE_EXPIRE_CYCLE_LOOKUPS_PER_LOOP;

            // ��ʼ�������ݿ�
            while (num--) {
                dictEntry *de;
                long long ttl;

                // �� expires �����ȡ��һ��������ʱ��ļ�
                if ((de = dictGetRandomKey(db->expires)) == NULL) break;
                // ���� TTL
                ttl = dictGetSignedIntegerVal(de)-now;
                // ������Ѿ����ڣ���ôɾ���������� expired ��������һ
                //key-value�������������ڴ���ɾ�����Ǹ�activeExpireCycleTryExpire����
                if (activeExpireCycleTryExpire(db,de,now)) expired++;
                if (ttl < 0) ttl = 0;
                // �ۻ����� TTL
                ttl_sum += ttl;
                // �ۻ�������ĸ���
                ttl_samples++;
            }

            /* Update the average TTL stats for this database. */
            // Ϊ������ݿ����ƽ�� TTL ͳ������
            if (ttl_samples) {
                // ���㵱ǰƽ��ֵ
                long long avg_ttl = ttl_sum/ttl_samples;
                
                // ������ǵ�һ���������ݿ�ƽ�� TTL ����ô���г�ʼ��
                if (db->avg_ttl == 0) db->avg_ttl = avg_ttl;
                /* Smooth the value averaging with the previous one. */
                // ȡ���ݿ���ϴ�ƽ�� TTL �ͽ��ƽ�� TTL ��ƽ��ֵ
                db->avg_ttl = (db->avg_ttl+avg_ttl)/2;
            }

            /* We can't block forever here even if there are many keys to
             * expire. So after a given amount of milliseconds return to the
             * caller waiting for the other active expire cycle. */
            // ���ǲ�����̫��ʱ�䴦����ڼ���
            // �����������ִ��һ��ʱ��֮���Ҫ����

            // ���±�������
            iteration++;

            // ÿ���� 16 ��ִ��һ��
            if ((iteration & 0xf) == 0 && /* check once every 16 iterations. */
                (ustime()-start) > timelimit)
            {
                // ����������������� 16 �ı���
                // ���ұ�����ʱ�䳬���� timelimit
                // ��ô�Ͽ� timelimit_exit
                timelimit_exit = 1;
            }

            // �Ѿ���ʱ�ˣ�����
            if (timelimit_exit) return;

            /* We don't repeat the cycle if there are less than 25% of keys
             * found expired in the current DB. */
            // �����ɾ���Ĺ��ڼ�ռ��ǰ�����ݿ������ʱ��ļ������� 25 %��
            //��ô����������ֱ���ڸú�����ִ��ʱ�䳬��25ms(fastģʽ1ms����ͨģʽ25ms)���߱��������ѡ����20��KV�У����ڵ�С��5�������˳��ú���
        } while (expired > ACTIVE_EXPIRE_CYCLE_LOOKUPS_PER_LOOP/4); //�����ȡ20��K���������������5��KEY�����ˣ���������й��ڳ���ɾ��
    }
}

unsigned int getLRUClock(void) {
    return (mstime()/REDIS_LRU_CLOCK_RESOLUTION) & REDIS_LRU_CLOCK_MAX;
}

/* Add a sample to the operations per second array of samples. */
/*
serverCron�����е�trackOperationsPerSecond��������ÿ1 0 0����һ�ε�Ƶ��ִ�У���������Ĺ������Գ�������ķ�ʽ�����㲢��¼������
�����һ���Ӵ���������������������ֵ����ͨ��INFO status�鿴
*/
// ��������������ִ�д�����¼������������
void trackOperationsPerSecond(void) {

    // �������γ���֮���ʱ�䳤�ȣ������ʽ
    long long t = mstime() - server.ops_sec_last_sample_time;

    // �������γ���֮�䣬ִ���˶��ٸ����� //���Ǹ����ܵ�����ִ����total_commands_processed�������
    long long ops = server.stat_numcommands - server.ops_sec_last_sample_ops; 

    long long ops_sec;

    /* ������ƽ��ÿһ���봦���˶��ٸ���������Ȼ�����ƽ��ֵ����1 0 0 0����͵õ��˷�������һ�������ܴ�����ٸ���������Ĺ���ֵ */
    // ���������һ�γ���֮��ÿ��ִ�����������
    ops_sec = t > 0 ? (ops*1000/t) : 0;

    // ���������ִ�������������浽��������
    server.ops_sec_samples[server.ops_sec_idx] = ops_sec;
    // ���³������������
    server.ops_sec_idx = (server.ops_sec_idx+1) % REDIS_OPS_SEC_SAMPLES;
    // �������һ�γ�����ʱ��
    server.ops_sec_last_sample_time = mstime();
    // �������һ�γ���ʱ��ִ����������
    server.ops_sec_last_sample_ops = server.stat_numcommands;
}

/*
���ͻ���ִ��INFO����ʱ���������ͻ����getOperations PerSecond����������ops_sec_samples���������еĳ������
����getOperationsPerSecond�����Ķ�����Կ�����instantaneous_ops_per_sec���Ե�ֵ��ͨ���������REDIS_OPS_SEC_SAMPLES
��ȡ����ƽ��ֵ������ó��ģ���ֻ��һ������ֵ
*/
/* Return the mean of all the samples. */
// ��������ȡ����Ϣ��������������һ��ִ����������ƽ��ֵ
long long getOperationsPerSecond(void) { //���Ǹ����ܵ�����ִ����total_commands_processed��ƽ���ó�����
    int j;
    long long sum = 0;

    // ��������ȡ��ֵ���ܺ�
    for (j = 0; j < REDIS_OPS_SEC_SAMPLES; j++)
        sum += server.ops_sec_samples[j];

    // ����ȡ����ƽ��ֵ
    return sum / REDIS_OPS_SEC_SAMPLES;
}

/* Check for timeouts. Returns non-zero if the client was terminated */
// ���ͻ����Ƿ��Ѿ���ʱ�������ʱ�͹رտͻ��ˣ������� 1 ��
// ���򷵻� 0 ��
int clientsCronHandleTimeout(redisClient *c) {

    // ��ȡ��ǰʱ��
    time_t now = server.unixtime;

    // ������������ maxidletime ʱ��
    if (server.maxidletime &&
        // �������Ϊ�ӷ������Ŀͻ���
        !(c->flags & REDIS_SLAVE) &&    /* no timeout for slaves */
        // �������Ϊ���������Ŀͻ���
        !(c->flags & REDIS_MASTER) &&   /* no timeout for masters */
        // ����鱻�����Ŀͻ���
        !(c->flags & REDIS_BLOCKED) &&  /* no timeout for BLPOP */
        // ����鶩����Ƶ���Ŀͻ���
        dictSize(c->pubsub_channels) == 0 && /* no timeout for pubsub */
        // ����鶩����ģʽ�Ŀͻ���
        listLength(c->pubsub_patterns) == 0 &&
        // �ͻ������һ���������ͨѶ��ʱ���Ѿ������� maxidletime ʱ��
        (now - c->lastinteraction > server.maxidletime))
    {
        redisLog(REDIS_VERBOSE,"Closing idle client");
        // �رճ�ʱ�ͻ���
        freeClient(c, NGX_FUNC_LINE);
        return 1;
    } else if (c->flags & REDIS_BLOCKED) {

        /* Blocked OPS timeout is handled with milliseconds resolution.
         * However note that the actual resolution is limited by
         * server.hz. */
        // ��ȡ���µ�ϵͳʱ��
        mstime_t now_ms = mstime();

        // ��鱻 BLPOP �����������Ŀͻ��˵�����ʱ���Ƿ��Ѿ�����
        // ����ǵĻ���ȡ���ͻ��˵�����
        if (c->bpop.timeout != 0 && c->bpop.timeout < now_ms) {
            // ��ͻ��˷��ؿջظ�
            replyToBlockedClientTimedOut(c);
            // ȡ���ͻ��˵�����״̬
            unblockClient(c);
        }
    }

    // �ͻ���û�б��ر�
    return 0;
}

/* The client query buffer is an sds.c string that can end with a lot of
 * free space not used, this function reclaims space if needed.
 *
 * �����������С��ѯ�������Ĵ�С��
 *
 * The function always returns 0 as it never terminates the client. 
 *
 * �������Ƿ��� 0 ����Ϊ��������ֹ�ͻ��ˡ�
 */
int clientsCronResizeQueryBuffer(redisClient *c) {
    size_t querybuf_size = sdsAllocSize(c->querybuf);
    time_t idletime = server.unixtime - c->lastinteraction;

    /* There are two conditions to resize the query buffer:
     *
     * �����������������Ļ���ִ�д�С������
     *
     * 1) Query buffer is > BIG_ARG and too big for latest peak.
     *    ��ѯ�������Ĵ�С���� BIG_ARG �Լ� querybuf_peak
     *
     * 2) Client is inactive and the buffer is bigger than 1k. 
     *    �ͻ��˲���Ծ�����һ��������� 1k ��
     */
    if (((querybuf_size > REDIS_MBULK_BIG_ARG) &&
         (querybuf_size/(c->querybuf_peak+1)) > 2) ||
         (querybuf_size > 1024 && idletime > 2))
    {
        /* Only resize the query buffer if it is actually wasting space. */
        if (sdsavail(c->querybuf) > 1024) {
            c->querybuf = sdsRemoveFreeSpace(c->querybuf);
        }
    }

    /* Reset the peak again to capture the peak memory usage in the next
     * cycle. */
    // ���÷�ֵ
    c->querybuf_peak = 0;

    return 0;
}

void clientsCron(void) {
    /* Make sure to process at least 1/(server.hz*10) of clients per call.
     *
     * �������ÿ��ִ�ж��ᴦ������ 1/server.hz*10 ���ͻ��ˡ�
     *
     * Since this function is called server.hz times per second we are sure that
     * in the worst case we process all the clients in 10 seconds.
     *
     * ��Ϊ�������ÿ���ӻ���� server.hz �Σ�
     * �����������£���������Ҫʹ�� 10 �������������пͻ��ˡ�
     *
     * In normal conditions (a reasonable number of clients) we process
     * all the clients in a shorter time. 
     *
     * ��һ������£��������пͻ��������ʱ����ʵ���ж̺ܶࡣ
     */

    // �ͻ�������
    int numclients = listLength(server.clients);

    // Ҫ����Ŀͻ�������
    int iterations = numclients/(server.hz*10);

    // ����Ҫ���� 50 ���ͻ���
    if (iterations < 50)
        iterations = (numclients < 50) ? numclients : 50;

    while(listLength(server.clients) && iterations--) {
        redisClient *c;
        listNode *head;

        /* Rotate the list, take the current head, process.
         * This way if the client must be removed from the list it's the
         * first element and we don't incur into O(N) computation. */
        // ��ת�б�Ȼ��ȡ����ͷԪ�أ�����һ����һ��������Ŀͻ��˻ᱻ�ŵ���ͷ
        // ���⣬�������Ҫɾ����ǰ�ͻ��ˣ���ôֻҪɾ����ͷԪ�ؾͿ�����
        listRotate(server.clients);
        head = listFirst(server.clients);
        c = listNodeValue(head);
        /* The following functions do different service checks on the client.
         * The protocol is that they return non-zero if the client was
         * terminated. */
        // ���ͻ��ˣ����ڿͻ��˳�ʱʱ�ر���
        if (clientsCronHandleTimeout(c)) continue;
        // �����������С�ͻ��˲�ѯ�������Ĵ�С
        if (clientsCronResizeQueryBuffer(c)) continue;
    }
}

/* This function handles 'background' operations we are required to do
 * incrementally in Redis databases, such as active key expiring, resizing,
 * rehashing. */
// �����ݿ�ִ��ɾ�����ڼ���������С���Լ������ͽ���ʽ rehash
void databasesCron(void) {

    // �����ȴ����ݿ���ɾ�����ڼ���Ȼ���ٶ����ݿ�Ĵ�С�����޸�

    /* Expire keys by random sampling. Not required for slaves
     * as master will synthesize DELs for us. */
    // ������������Ǵӷ���������ôִ���������ڼ����
    if (server.active_expire_enabled && server.masterhost == NULL) //ֻ��master������ʱ����������
        // ���ģʽΪ CYCLE_SLOW �����ģʽ�ᾡ����������ڼ�
        activeExpireCycle(ACTIVE_EXPIRE_CYCLE_SLOW);

    /* Perform hash tables rehashing if needed, but only if there are no
     * other processes saving the DB on disk. Otherwise rehashing is bad
     * as will cause a lot of copy-on-write of memory pages. */
    // ��û�� BGSAVE ���� BGREWRITEAOF ִ��ʱ���Թ�ϣ����� rehash
    if (server.rdb_child_pid == -1 && server.aof_child_pid == -1) {
        /* We use global counters so if we stop the computation at a given
         * DB we'll be able to start from the successive in the next
         * cron loop iteration. */
        static unsigned int resize_db = 0;
        static unsigned int rehash_db = 0;
        unsigned int dbs_per_call = REDIS_DBCRON_DBS_PER_CALL;
        unsigned int j;

        /* Don't test more DBs than we have. */
        // �趨Ҫ���Ե����ݿ�����
        if (dbs_per_call > server.dbnum) dbs_per_call = server.dbnum;

        /* Resize */
        // �����ֵ�Ĵ�С
        for (j = 0; j < dbs_per_call; j++) {
            tryResizeHashTables(resize_db % server.dbnum);
            resize_db++;
        }

        /* Rehash */
        // ���ֵ���н���ʽ rehash
        if (server.activerehashing) {
            for (j = 0; j < dbs_per_call; j++) {
                int work_done = incrementallyRehash(rehash_db % server.dbnum);
                rehash_db++;
                if (work_done) {
                    /* If the function did some work, stop here, we'll do
                     * more at the next cron loop. */
                    break;
                }
            }
        }
    }
}

/* We take a cached value of the unix time in the global state because with
 * virtual memory and aging there is to store the current time in objects at
 * every object access, and accuracy is not needed. To access a global var is
 * a lot faster than calling time(NULL) */
void updateCachedTime(void) {
    server.unixtime = time(NULL);
    server.mstime = mstime();
}

/* This is our timer interrupt, called server.hz times per second.
 *
 * ���� Redis ��ʱ���ж�����ÿ����� server.hz �Ρ�
 *
 * Here is where we do a number of things that need to be done asynchronously.
 * For instance:
 *
 * ��������Ҫ�첽ִ�еĲ�����
 *
 * - Active expired keys collection (it is also performed in a lazy way on
 *   lookup).
 *   ����������ڼ���
 *
 * - Software watchdog.
 *   ������� watchdog ����Ϣ��
 *
 * - Update some statistic.
 *   ����ͳ����Ϣ��
 *
 * - Incremental rehashing of the DBs hash tables.
 *   �����ݿ���н���ʽ Rehash
 *
 * - Triggering BGSAVE / AOF rewrite, and handling of terminated children.
 *   ���� BGSAVE ���� AOF ��д��������֮���� BGSAVE �� AOF ��д�������ӽ���ֹͣ��
 *
 * - Clients timeout of different kinds.
 *   ����ͻ��˳�ʱ��
 *
 * - Replication reconnection.
 *   ��������
 *
 * - Many more...
 *   �ȵȡ�����
 *
 * Everything directly called here will be called server.hz times per second,
 * so in order to throttle execution of things we want to do less frequently
 * a macro is used: run_with_period(milliseconds) { .... }
 *
 * ��Ϊ serverCron �����е����д��붼��ÿ����� server.hz �Σ�
 * Ϊ�˶Բ��ִ���ĵ��ô����������ƣ�
 * ʹ����һ���� run_with_period(milliseconds) { ... } ��
 * �������Խ������������ִ�д�������Ϊÿ milliseconds ִ��һ�Ρ�
 */
    /* rehash������ù�ϵ���ù���:dictAddRaw->_dictKeyIndex->_dictExpandIfNeeded(��������Ƿ���Ҫ��չhash)->dictExpand 
    ����hash����:serverCron->tryResizeHashTables->dictResize(��������������Ͱ��)->dictExpand */

/*
Redis�ķ����������Բ�������serverCronĬ��ÿ��100����ͻ�ִ��һ�Σ��ú������ڶ��������еķ���������ά��������:��������һ�������
���saveѡ�������õı��������Ƿ��Ѿ����㣬�������Ļ�����ִ��BGSAVE���
*/ //serverCron��initServer->aeCreateTimeEvent�д�����Ȼ����aeMain(server.el); ��ִ�� 
//�ú�������ֵ����0��������¼�����ӵ���ʱ���У���processTimeEvents
int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    int j;
    REDIS_NOTUSED(eventLoop);
    REDIS_NOTUSED(id);
    REDIS_NOTUSED(clientData);

    /* Software watchdog: deliver the SIGALRM that will reach the signal
     * handler if we don't return here fast enough. */
    if (server.watchdog_period) watchdogScheduleSignal(server.watchdog_period);

    /* Update the time cache. */
    updateCachedTime();

    // ��¼������ִ������Ĵ���
    run_with_period(100) 
        trackOperationsPerSecond();

    /* We have just REDIS_LRU_BITS bits per object for LRU information.
     * So we use an (eventually wrapping) LRU clock.
     *
     * Note that even if the counter wraps it's not a big problem,
     * everything will still work but some object will appear younger
     * to Redis. However for this to happen a given object should never be
     * touched for all the time needed to the counter to wrap, which is
     * not likely.
     *
     * ��ʹ��������ʱ�����ձ� 1.5 �곤Ҳ����ν��
     * ����ϵͳ�Ի���������������һЩ������ܻ�ȷ����������ʱ�Ӹ����ᡣ
     * ������Ҫ��������� 1.5 ���ڶ�û�б����ʹ����Ż������������
     *
     * Note that you can change the resolution altering the
     * REDIS_LRU_CLOCK_RESOLUTION define.
     *
     * LRU ʱ��ľ��ȿ���ͨ���޸� REDIS_LRU_CLOCK_RESOLUTION �������ı䡣
     */
    server.lruclock = getLRUClock();

    /* Record the max memory used since the server was started. */
    // ��¼���������ڴ��ֵ
    if (zmalloc_used_memory() > server.stat_peak_memory)
        server.stat_peak_memory = zmalloc_used_memory();

    /* Sample the RSS here since this is a relatively slow call. */
    server.resident_set_size = zmalloc_get_rss();

    /* We received a SIGTERM, shutting down here in a safe way, as it is
     * not ok doing so inside the signal handler. */
    // �����������յ� SIGTERM �źţ��رշ�����
    if (server.shutdown_asap) {

        // ���Թرշ�����
        if (prepareForShutdown(0) == REDIS_OK) exit(0);

        // ����ر�ʧ�ܣ���ô��ӡ LOG �����Ƴ��رձ�ʶ
        redisLog(REDIS_WARNING,"SIGTERM received but errors trying to shut down the server, check the logs for more information");
        server.shutdown_asap = 0;
    }

    /* Show some info about non-empty databases */
    // ��ӡ���ݿ�ļ�ֵ����Ϣ
    run_with_period(5000) { //5s��
        for (j = 0; j < server.dbnum; j++) {
            long long size, used, vkeys;

            // ���ü�ֵ�Ե�����
            size = dictSlots(server.db[j].dict);
            // ���ü�ֵ�Ե�����
            used = dictSize(server.db[j].dict);
            // ���й���ʱ��ļ�ֵ������
            vkeys = dictSize(server.db[j].expires);

            // �� LOG ��ӡ����
            if (used || vkeys) {
                redisLog(REDIS_VERBOSE,"DB %d: %lld keys (%lld volatile) in %lld slots HT.",j,used,vkeys,size);
                /* dictPrintStats(server.dict); */
            }
        }
    }

    /* Show information about connected clients */
    // ���������û�������� SENTINEL ģʽ�£���ô��ӡ�ͻ��˵�������Ϣ
    if (!server.sentinel_mode) {
        run_with_period(5000) {
            redisLog(REDIS_VERBOSE,
                "%lu clients connected (%lu slaves), %zu bytes in use",
                listLength(server.clients)-listLength(server.slaves),
                listLength(server.slaves),
                zmalloc_used_memory());
        }
    }

    /* We need to do a few operations on clients asynchronously. */
    // ���ͻ��ˣ��رճ�ʱ�ͻ��ˣ����ͷſͻ��˶���Ļ�����
    clientsCron();

    /* Handle background operations on Redis databases. */
    // �����ݿ�ִ�и��ֲ���
    databasesCron();

    /* Start a scheduled AOF rewrite if this was requested by the user while
     * a BGSAVE was in progress. */
    // ��� BGSAVE �� BGREWRITEAOF ��û����ִ��
    // ������һ�� BGREWRITEAOF �ڵȴ�����ôִ�� BGREWRITEAOF
    if (server.rdb_child_pid == -1 && server.aof_child_pid == -1 &&
        server.aof_rewrite_scheduled)
    {
        rewriteAppendOnlyFileBackground();
    }

    /* Check if a background saving or AOF rewrite in progress terminated. */
    // ��� BGSAVE ���� BGREWRITEAOF �Ƿ��Ѿ�ִ�����
    if (server.rdb_child_pid != -1 || server.aof_child_pid != -1) {
        int statloc;
        pid_t pid;

        // �����ӽ��̷������źţ�������   ��rdbSaveBackground->exitFromChild��Ӧ
        if ((pid = wait3(&statloc,WNOHANG,NULL)) != 0) { //�źŷ�����exitFromChild
             /*
                WEXITSTATUS(status) ��WIFEXITED���ط���ֵʱ�����ǿ��������������ȡ�ӽ��̵ķ���ֵ������ӽ��̵���exit(5)�˳���
                WEXITSTATUS(status)�ͻ᷵��5������ӽ��̵���exit(7)��WEXITSTATUS(status)�ͻ᷵��7����ע�⣬������̲��������˳��ģ�
                Ҳ����˵��WIFEXITED����0�����ֵ�ͺ�������
               */
            int exitcode = WEXITSTATUS(statloc);
            int bysignal = 0;

           
           //WIFSIGNALED(status)��Ϊ�쳣�����ӽ��̷��ص�״̬����Ϊ��
            if (WIFSIGNALED(statloc)) 
                bysignal = WTERMSIG(statloc);//WTERMSIG(status)��ȡʹ�ӽ��̽������źű�š�

            // BGSAVE ִ�����
            if (pid == server.rdb_child_pid) {
                backgroundSaveDoneHandler(exitcode,bysignal);

            // BGREWRITEAOF ִ����� //�ӽ��̰�����ȫ��д��AOF��ʱ�ļ��󣬸ú�����д����ʱ�ļ�����ڼ������д�����������ʱ�ļ��У�Ȼ��rename���ƶ�aof�ļ�
            } else if (pid == server.aof_child_pid) {
                backgroundRewriteDoneHandler(exitcode,bysignal);

            } else {
                redisLog(REDIS_WARNING,
                    "Warning, detected child with unmatched pid: %ld",
                    (long)pid);
            }
            updateDictResizePolicy();
        }   
    } else {

        /* If there is not a background saving/rewrite in progress check if
         * we have to save/rewrite now */
        // ��Ȼû�� BGSAVE ���� BGREWRITEAOF ��ִ�У���ô����Ƿ���Ҫִ������

        // �������б������������Ƿ���Ҫִ�� BGSAVE ����
         for (j = 0; j < server.saveparamslen; j++) {
            struct saveparam *sp = server.saveparams+j;

            /* Save if we reached the given amount of changes,
             * the given amount of seconds, and if the latest bgsave was
             * successful or if, in case of an error, at least
             * REDIS_BGSAVE_RETRY_DELAY seconds already elapsed. */
            // ����Ƿ���ĳ�����������Ѿ�������
            if (server.dirty >= sp->changes &&
                server.unixtime-server.lastsave > sp->seconds &&
                (server.unixtime-server.lastbgsave_try >
                 REDIS_BGSAVE_RETRY_DELAY ||
                 server.lastbgsave_status == REDIS_OK))
            {
                redisLog(REDIS_NOTICE,"%d changes in %d seconds. Saving...",
                    sp->changes, (int)sp->seconds);
                // ִ�� BGSAVE
                rdbSaveBackground(server.rdb_filename);
                break;
            }
         }

        /*
            ÿ�θ��²������е�AOFд�������漰ͬ��Ƶ�ʣ�
            Rewrite��������auto-aof-rewrite-percentage��auto-aof-rewrite-min-sizeʱ�����Զ�����rewrite������
            Rewrite�����յ�bgrewriteaof�ͻ�������ʱ���������к���rewrite����
           */
         /* Trigger an AOF rewrite if needed */
        // ���� BGREWRITEAOF
         if (server.rdb_child_pid == -1 &&
             server.aof_child_pid == -1 &&
             server.aof_rewrite_perc &&
             // AOF �ļ��ĵ�ǰ��С����ִ�� BGREWRITEAOF �������С��С
             server.aof_current_size > server.aof_rewrite_min_size)
         {
            // ��һ����� AOF д��֮��AOF �ļ��Ĵ�С
            long long base = server.aof_rewrite_base_size ?
                            server.aof_rewrite_base_size : 1;

            // AOF �ļ���ǰ���������� base ������İٷֱ�
            long long growth = (server.aof_current_size*100/base) - 100;

            // �����������İٷֱȳ����� growth ����ôִ�� BGREWRITEAOF
            if (growth >= server.aof_rewrite_perc) {
                redisLog(REDIS_NOTICE,"Starting automatic rewriting of AOF on %lld%% growth",growth);
                // ִ�� BGREWRITEAOF
                rewriteAppendOnlyFileBackground();
            }
         }
    }

    // ���� AOF ���ߣ�
    // �����Ƿ���Ҫ�� AOF �������е�����д�뵽 AOF �ļ���
    /* AOF postponed flush: Try at every cron cycle if the slow fsync
     * completed. */
    if (server.aof_flush_postponed_start) flushAppendOnlyFile(0);

    /* AOF write errors: in this case we have a buffer to flush as well and
     * clear the AOF error in case of success to make the DB writable again,
     * however to try every second is enough in case of 'hz' is set to
     * an higher frequency. */
    run_with_period(1000) {
        if (server.aof_last_write_status == REDIS_ERR)
            flushAppendOnlyFile(0);
    }

    /* Close clients that need to be closed asynchronous */
    // �ر���Щ��Ҫ�첽�رյĿͻ���
    freeClientsInAsyncFreeQueue();

    /* Clear the paused clients flag if needed. */
    clientsArePaused(); /* Don't check return value, just use the side effect. */

    /* Replication cron function -- used to reconnect to master and
     * to detect transfer failures. */
    // ���ƺ���
    // ������������������������������ ACK ���ж����ݷ���ʧ��������Ͽ�����������ʱ�Ĵӷ��������ȵ�
    run_with_period(1000) replicationCron();

    /* Run the Redis Cluster cron. */
    // ��������������ڼ�Ⱥģʽ�£���ôִ�м�Ⱥ����
    run_with_period(100) {
        if (server.cluster_enabled) clusterCron();
    }

    /* Run the Sentinel timer if we are in sentinel mode. */
    // ��������������� sentinel ģʽ�£���ôִ�� SENTINEL ��������
    run_with_period(100) {
        if (server.sentinel_mode) sentinelTimer();
    }

    /* Cleanup expired MIGRATE cached sockets. */
    // ��Ⱥ������TODO
    run_with_period(1000) {
        migrateCloseTimedoutSockets();
    }

    // ���� loop ������
    server.cronloops++;

    return 1000/server.hz; //Ĭ��Ϊ1000/REDIS_DEFAULT_HZ����100
}

/* This function gets called every time Redis is entering the
 * main loop of the event driven library, that is, before to sleep
 * for ready file descriptors. */
// ÿ�δ����¼�֮ǰִ��  //��ֵΪbeforeSleep���ں���aeMain��ִ��
void beforeSleep(struct aeEventLoop *eventLoop) {
    REDIS_NOTUSED(eventLoop);

    /* Run a fast expire cycle (the called function will return
     * ASAP if a fast cycle is not needed). */
    // ִ��һ�ο��ٵ��������ڼ��
    if (server.active_expire_enabled && server.masterhost == NULL) //ֻ��master������ʱ����������
        activeExpireCycle(ACTIVE_EXPIRE_CYCLE_FAST);

    /* Send all the slaves an ACK request if at least one client blocked
     * during the previous event loop iteration. */
    if (server.get_ack_from_slaves) { //wait��������������������Ҫ���пͻ������·���REPLCONF ACK XX���㱨�Լ���ƫ����
        robj *argv[3];

        argv[0] = createStringObject("REPLCONF",8);
        argv[1] = createStringObject("GETACK",6);
        argv[2] = createStringObject("*",1); /* Not used argument. */
        replicationFeedSlaves(server.slaves, server.slaveseldb, argv, 3);
        decrRefCount(argv[0]);
        decrRefCount(argv[1]);
        decrRefCount(argv[2]);
        server.get_ack_from_slaves = 0;
    }

    /* Unblock all the clients blocked for synchronous replication
     * in WAIT. */
    if (listLength(server.clients_waiting_acks))
        processClientsWaitingReplicas();

    /* Try to process pending commands for clients that were just unblocked. */
    if (listLength(server.unblocked_clients))
        processUnblockedClients();

    /* Write the AOF buffer on disk */
    // �� AOF ������������д�뵽 AOF �ļ�
    flushAppendOnlyFile(0);

    /* Call the Redis Cluster before sleep function. */
    // �ڽ����¸��¼�ѭ��ǰ��ִ��һЩ��Ⱥ��β����
    if (server.cluster_enabled) clusterBeforeSleep();
}

/* =========================== Server initialization ======================== */

void createSharedObjects(void) { //��Ӧ��sharedObjectsStruct
    int j;

    // ���ûظ�
    shared.crlf = createObject(REDIS_STRING,sdsnew("\r\n"));
    shared.ok = createObject(REDIS_STRING,sdsnew("+OK\r\n"));
    shared.err = createObject(REDIS_STRING,sdsnew("-ERR\r\n"));
    shared.emptybulk = createObject(REDIS_STRING,sdsnew("$0\r\n\r\n"));
    shared.czero = createObject(REDIS_STRING,sdsnew(":0\r\n"));
    shared.cone = createObject(REDIS_STRING,sdsnew(":1\r\n"));
    shared.cnegone = createObject(REDIS_STRING,sdsnew(":-1\r\n"));
    shared.nullbulk = createObject(REDIS_STRING,sdsnew("$-1\r\n"));
    shared.nullmultibulk = createObject(REDIS_STRING,sdsnew("*-1\r\n"));
    shared.emptymultibulk = createObject(REDIS_STRING,sdsnew("*0\r\n"));
    shared.pong = createObject(REDIS_STRING,sdsnew("+PONG\r\n"));
    shared.queued = createObject(REDIS_STRING,sdsnew("+QUEUED\r\n"));
    shared.emptyscan = createObject(REDIS_STRING,sdsnew("*2\r\n$1\r\n0\r\n*0\r\n"));
    // ���ô���ظ�
    shared.wrongtypeerr = createObject(REDIS_STRING,sdsnew(
        "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"));
    shared.nokeyerr = createObject(REDIS_STRING,sdsnew(
        "-ERR no such key\r\n"));
    shared.syntaxerr = createObject(REDIS_STRING,sdsnew(
        "-ERR syntax error\r\n"));
    shared.sameobjecterr = createObject(REDIS_STRING,sdsnew(
        "-ERR source and destination objects are the same\r\n"));
    shared.outofrangeerr = createObject(REDIS_STRING,sdsnew(
        "-ERR index out of range\r\n"));
    shared.noscripterr = createObject(REDIS_STRING,sdsnew(
        "-NOSCRIPT No matching script. Please use EVAL.\r\n"));
    shared.loadingerr = createObject(REDIS_STRING,sdsnew(
        "-LOADING Redis is loading the dataset in memory\r\n"));
    shared.slowscripterr = createObject(REDIS_STRING,sdsnew(
        "-BUSY Redis is busy running a script. You can only call SCRIPT KILL or SHUTDOWN NOSAVE.\r\n"));
    shared.masterdownerr = createObject(REDIS_STRING,sdsnew(
        "-MASTERDOWN Link with MASTER is down and slave-serve-stale-data is set to 'no'.\r\n"));
    shared.bgsaveerr = createObject(REDIS_STRING,sdsnew(
        "-MISCONF Redis is configured to save RDB snapshots, but is currently not able to persist on disk. Commands that may modify the data set are disabled. Please check Redis logs for details about the error.\r\n"));
    shared.roslaveerr = createObject(REDIS_STRING,sdsnew(
        "-READONLY You can't write against a read only slave.\r\n"));
    shared.noautherr = createObject(REDIS_STRING,sdsnew(
        "-NOAUTH Authentication required.\r\n"));
    shared.oomerr = createObject(REDIS_STRING,sdsnew(
        "-OOM command not allowed when used memory > 'maxmemory'.\r\n"));
    shared.execaborterr = createObject(REDIS_STRING,sdsnew(
        "-EXECABORT Transaction discarded because of previous errors.\r\n"));
    shared.noreplicaserr = createObject(REDIS_STRING,sdsnew(
        "-NOREPLICAS Not enough good slaves to write.\r\n"));
    shared.busykeyerr = createObject(REDIS_STRING,sdsnew(
        "-BUSYKEY Target key name already exists.\r\n"));

    // �����ַ�
    shared.space = createObject(REDIS_STRING,sdsnew(" "));
    shared.colon = createObject(REDIS_STRING,sdsnew(":"));
    shared.plus = createObject(REDIS_STRING,sdsnew("+"));

    // ���� SELECT ����
    for (j = 0; j < REDIS_SHARED_SELECT_CMDS; j++) {
        char dictid_str[64];
        int dictid_len;

        dictid_len = ll2string(dictid_str,sizeof(dictid_str),j);
        shared.select[j] = createObject(REDIS_STRING,
            sdscatprintf(sdsempty(),
                "*2\r\n$6\r\nSELECT\r\n$%d\r\n%s\r\n",
                dictid_len, dictid_str));
    }

    // �����붩�ĵ��йػظ�
    shared.messagebulk = createStringObject("$7\r\nmessage\r\n",13);
    shared.pmessagebulk = createStringObject("$8\r\npmessage\r\n",14);
    shared.subscribebulk = createStringObject("$9\r\nsubscribe\r\n",15);
    shared.unsubscribebulk = createStringObject("$11\r\nunsubscribe\r\n",18);
    shared.psubscribebulk = createStringObject("$10\r\npsubscribe\r\n",17);
    shared.punsubscribebulk = createStringObject("$12\r\npunsubscribe\r\n",19);

    // ��������
    shared.del = createStringObject("DEL",3);
    shared.rpop = createStringObject("RPOP",4);
    shared.lpop = createStringObject("LPOP",4);
    shared.lpush = createStringObject("LPUSH",5);

    // ��������
    for (j = 0; j < REDIS_SHARED_INTEGERS; j++) {
        shared.integers[j] = createObject(REDIS_STRING,(void*)(long)j); //��Ӧ��ȡֵ�ο�getDecodedObject
        shared.integers[j]->encoding = REDIS_ENCODING_INT;
    }

    // ���ó��� bulk ���� multi bulk �ظ�
    for (j = 0; j < REDIS_SHARED_BULKHDR_LEN; j++) {
        shared.mbulkhdr[j] = createObject(REDIS_STRING,
            sdscatprintf(sdsempty(),"*%d\r\n",j));
        shared.bulkhdr[j] = createObject(REDIS_STRING,
            sdscatprintf(sdsempty(),"$%d\r\n",j));
    }
    /* The following two shared objects, minstring and maxstrings, are not
     * actually used for their value but as a special object meaning
     * respectively the minimum possible string and the maximum possible
     * string in string comparisons for the ZRANGEBYLEX command. */
    shared.minstring = createStringObject("minstring",9);
    shared.maxstring = createStringObject("maxstring",9);
}

void initServerConfig() { //��initServerConfig����loadServerConfig
    int j;

    // ������״̬

    // ���÷����������� ID
    getRandomHexChars(server.runid,REDIS_RUN_ID_SIZE);
    // ����Ĭ�������ļ�·��
    server.configfile = NULL;
    // ����Ĭ�Ϸ�����Ƶ��
    server.hz = REDIS_DEFAULT_HZ;
    // Ϊ���� ID ���Ͻ�β�ַ�
    server.runid[REDIS_RUN_ID_SIZE] = '\0';
    // ���÷����������мܹ�
    server.arch_bits = (sizeof(long) == 8) ? 64 : 32;
    // ����Ĭ�Ϸ������˿ں�
    server.port = REDIS_SERVERPORT;
    server.tcp_backlog = REDIS_TCP_BACKLOG;
    server.bindaddr_count = 0;
    server.unixsocket = NULL;
    server.unixsocketperm = REDIS_DEFAULT_UNIX_SOCKET_PERM;
    server.ipfd_count = 0;
    server.sofd = -1;
    server.dbnum = REDIS_DEFAULT_DBNUM;
    server.verbosity = REDIS_DEFAULT_VERBOSITY;
    server.maxidletime = REDIS_MAXIDLETIME;
    server.tcpkeepalive = REDIS_DEFAULT_TCP_KEEPALIVE;
    server.active_expire_enabled = 1;
    server.client_max_querybuf_len = REDIS_MAX_QUERYBUF_LEN;
    server.saveparams = NULL;
    server.loading = 0;
    server.logfile = zstrdup(REDIS_DEFAULT_LOGFILE);
    server.syslog_enabled = REDIS_DEFAULT_SYSLOG_ENABLED;
    server.syslog_ident = zstrdup(REDIS_DEFAULT_SYSLOG_IDENT);
    server.syslog_facility = LOG_LOCAL0;
    server.daemonize = REDIS_DEFAULT_DAEMONIZE;
    server.aof_state = REDIS_AOF_OFF;
    server.aof_fsync = REDIS_DEFAULT_AOF_FSYNC;
    server.aof_no_fsync_on_rewrite = REDIS_DEFAULT_AOF_NO_FSYNC_ON_REWRITE;
    server.aof_rewrite_perc = REDIS_AOF_REWRITE_PERC;
    server.aof_rewrite_min_size = REDIS_AOF_REWRITE_MIN_SIZE;
    server.aof_rewrite_base_size = 0;
    server.aof_rewrite_scheduled = 0;
    server.aof_last_fsync = time(NULL);
    server.aof_rewrite_time_last = -1;
    server.aof_rewrite_time_start = -1;
    server.aof_lastbgrewrite_status = REDIS_OK;
    server.aof_delayed_fsync = 0;
    server.aof_fd = -1;
    server.aof_selected_db = -1; /* Make sure the first time will not match */
    server.aof_flush_postponed_start = 0;
    server.aof_rewrite_incremental_fsync = REDIS_DEFAULT_AOF_REWRITE_INCREMENTAL_FSYNC;
    server.pidfile = zstrdup(REDIS_DEFAULT_PID_FILE);
    server.rdb_filename = zstrdup(REDIS_DEFAULT_RDB_FILENAME);
    server.aof_filename = zstrdup(REDIS_DEFAULT_AOF_FILENAME);
    server.requirepass = NULL;
    server.rdb_compression = REDIS_DEFAULT_RDB_COMPRESSION;
    server.rdb_checksum = REDIS_DEFAULT_RDB_CHECKSUM;
    server.stop_writes_on_bgsave_err = REDIS_DEFAULT_STOP_WRITES_ON_BGSAVE_ERROR;
    server.activerehashing = REDIS_DEFAULT_ACTIVE_REHASHING;
    server.notify_keyspace_events = 0;
    server.maxclients = REDIS_MAX_CLIENTS;
    server.bpop_blocked_clients = 0;
    server.maxmemory = REDIS_DEFAULT_MAXMEMORY;
    server.maxmemory_policy = REDIS_DEFAULT_MAXMEMORY_POLICY;
    server.maxmemory_samples = REDIS_DEFAULT_MAXMEMORY_SAMPLES;
    server.hash_max_ziplist_entries = REDIS_HASH_MAX_ZIPLIST_ENTRIES;
    server.hash_max_ziplist_value = REDIS_HASH_MAX_ZIPLIST_VALUE;
    server.list_max_ziplist_entries = REDIS_LIST_MAX_ZIPLIST_ENTRIES;
    server.list_max_ziplist_value = REDIS_LIST_MAX_ZIPLIST_VALUE;
    server.set_max_intset_entries = REDIS_SET_MAX_INTSET_ENTRIES;
    server.zset_max_ziplist_entries = REDIS_ZSET_MAX_ZIPLIST_ENTRIES;
    server.zset_max_ziplist_value = REDIS_ZSET_MAX_ZIPLIST_VALUE;
    server.hll_sparse_max_bytes = REDIS_DEFAULT_HLL_SPARSE_MAX_BYTES;
    server.shutdown_asap = 0;
    server.repl_ping_slave_period = REDIS_REPL_PING_SLAVE_PERIOD;
    server.repl_timeout = REDIS_REPL_TIMEOUT;
    server.repl_min_slaves_to_write = REDIS_DEFAULT_MIN_SLAVES_TO_WRITE;
    server.repl_min_slaves_max_lag = REDIS_DEFAULT_MIN_SLAVES_MAX_LAG;
    server.cluster_enabled = 0;
    server.cluster_node_timeout = REDIS_CLUSTER_DEFAULT_NODE_TIMEOUT;
    server.cluster_migration_barrier = REDIS_CLUSTER_DEFAULT_MIGRATION_BARRIER;
    server.cluster_configfile = zstrdup(REDIS_DEFAULT_CLUSTER_CONFIG_FILE);
    server.lua_caller = NULL;
    server.lua_time_limit = REDIS_LUA_TIME_LIMIT;
    server.lua_client = NULL;
    server.lua_timedout = 0;
    server.migrate_cached_sockets = dictCreate(&migrateCacheDictType,NULL);
    server.loading_process_events_interval_bytes = (1024*1024*2);

    // ��ʼ�� LRU ʱ��
    server.lruclock = getLRUClock();

    // ��ʼ�������ñ�������
    resetServerSaveParams();

    appendServerSaveParams(60*60,1);  /* save after 1 hour and 1 change */
    appendServerSaveParams(300,100);  /* save after 5 minutes and 100 changes */
    appendServerSaveParams(60,10000); /* save after 1 minute and 10000 changes */

    /* Replication related */
    // ��ʼ���͸�����ص�״̬
    server.masterauth = NULL;
    server.masterhost = NULL;
    server.masterport = 6379;
    server.master = NULL;
    server.cached_master = NULL;
    server.repl_master_initial_offset = -1;
    server.repl_state = REDIS_REPL_NONE;
    server.repl_syncio_timeout = REDIS_REPL_SYNCIO_TIMEOUT;
    server.repl_serve_stale_data = REDIS_DEFAULT_SLAVE_SERVE_STALE_DATA;
    server.repl_slave_ro = REDIS_DEFAULT_SLAVE_READ_ONLY;
    server.repl_down_since = 0; /* Never connected, repl is down since EVER. */
    server.repl_disable_tcp_nodelay = REDIS_DEFAULT_REPL_DISABLE_TCP_NODELAY;
    server.slave_priority = REDIS_DEFAULT_SLAVE_PRIORITY;
    server.master_repl_offset = 0;

    /* Replication partial resync backlog */
    // ��ʼ�� PSYNC ������ʹ�õ� backlog
    server.repl_backlog = NULL;
    server.repl_backlog_size = REDIS_DEFAULT_REPL_BACKLOG_SIZE;
    server.repl_backlog_histlen = 0;
    server.repl_backlog_idx = 0;
    server.repl_backlog_off = 0;
    server.repl_backlog_time_limit = REDIS_DEFAULT_REPL_BACKLOG_TIME_LIMIT;
    server.repl_no_slaves_since = time(NULL);

    /* Client output buffer limits */
    // ���ÿͻ��˵��������������
    for (j = 0; j < REDIS_CLIENT_LIMIT_NUM_CLASSES; j++)
        server.client_obuf_limits[j] = clientBufferLimitsDefaults[j];

    /* Double constants initialization */
    // ��ʼ�����㳣��
    R_Zero = 0.0;
    R_PosInf = 1.0/R_Zero;
    R_NegInf = -1.0/R_Zero;
    R_Nan = R_Zero/R_Zero;

    /* Command table -- we initiialize it here as it is part of the
     * initial configuration, since command names may be changed via
     * redis.conf using the rename-command directive. */
    // ��ʼ�������
    // �������ʼ������Ϊ��������ȡ .conf �ļ�ʱ���ܻ��õ���Щ����
    server.commands = dictCreate(&commandTableDictType,NULL);
    server.orig_commands = dictCreate(&commandTableDictType,NULL);
    populateCommandTable();
    server.delCommand = lookupCommandByCString("del");
    server.multiCommand = lookupCommandByCString("multi");
    server.lpushCommand = lookupCommandByCString("lpush");
    server.lpopCommand = lookupCommandByCString("lpop");
    server.rpopCommand = lookupCommandByCString("rpop");
    
    /* Slow log */
    // ��ʼ������ѯ��־
    server.slowlog_log_slower_than = REDIS_SLOWLOG_LOG_SLOWER_THAN;
    server.slowlog_max_len = REDIS_SLOWLOG_MAX_LEN;

    /* Debugging */
    // ��ʼ��������
    server.assert_failed = "<no assertion failed>";
    server.assert_file = "<no file>";
    server.assert_line = 0;
    server.bug_report_start = 0;
    server.watchdog_period = 0;
}

/* This function will try to raise the max number of open files accordingly to
 * the configured max number of clients. It also reserves a number of file
 * descriptors (REDIS_MIN_RESERVED_FDS) for extra operations of
 * persistence, listening sockets, log files and so forth.
 *
 * If it will not be possible to set the limit accordingly to the configured
 * max number of clients, the function will do the reverse setting
 * server.maxclients to the value that we can actually handle. */
void adjustOpenFilesLimit(void) {
    rlim_t maxfiles = server.maxclients+REDIS_MIN_RESERVED_FDS;
    struct rlimit limit;

    if (getrlimit(RLIMIT_NOFILE,&limit) == -1) {
        redisLog(REDIS_WARNING,"Unable to obtain the current NOFILE limit (%s), assuming 1024 and setting the max clients configuration accordingly.",
            strerror(errno));
        server.maxclients = 1024-REDIS_MIN_RESERVED_FDS;
    } else {
        rlim_t oldlimit = limit.rlim_cur;

        /* Set the max number of files if the current limit is not enough
         * for our needs. */
        if (oldlimit < maxfiles) {
            rlim_t f;
            int setrlimit_error = 0;

            /* Try to set the file limit to match 'maxfiles' or at least
             * to the higher value supported less than maxfiles. */
            f = maxfiles;
            while(f > oldlimit) {
                int decr_step = 16;

                limit.rlim_cur = f;
                limit.rlim_max = f;
                if (setrlimit(RLIMIT_NOFILE,&limit) != -1) break;
                setrlimit_error = errno;

                /* We failed to set file limit to 'f'. Try with a
                 * smaller limit decrementing by a few FDs per iteration. */
                if (f < decr_step) break;
                f -= decr_step;
            }

            /* Assume that the limit we get initially is still valid if
             * our last try was even lower. */
            if (f < oldlimit) f = oldlimit;

            if (f != maxfiles) {
                int old_maxclients = server.maxclients;
                server.maxclients = f-REDIS_MIN_RESERVED_FDS;
                if (server.maxclients < 1) {
                    redisLog(REDIS_WARNING,"Your current 'ulimit -n' "
                        "of %llu is not enough for Redis to start. "
                        "Please increase your open file limit to at least "
                        "%llu. Exiting.",
                        (unsigned long long) oldlimit,
                        (unsigned long long) maxfiles);
                    exit(1);
                }
                redisLog(REDIS_WARNING,"You requested maxclients of %d "
                    "requiring at least %llu max file descriptors.",
                    old_maxclients,
                    (unsigned long long) maxfiles);
                redisLog(REDIS_WARNING,"Redis can't set maximum open files "
                    "to %llu because of OS error: %s.",
                    (unsigned long long) maxfiles, strerror(setrlimit_error));
                redisLog(REDIS_WARNING,"Current maximum open files is %llu. "
                    "maxclients has been reduced to %d to compensate for "
                    "low ulimit. "
                    "If you need higher maxclients increase 'ulimit -n'.",
                    (unsigned long long) oldlimit, server.maxclients);
            } else {
                redisLog(REDIS_NOTICE,"Increased maximum number of open files "
                    "to %llu (it was originally set to %llu).",
                    (unsigned long long) maxfiles,
                    (unsigned long long) oldlimit);
            }
        }
    }
}

/* Initialize a set of file descriptors to listen to the specified 'port'
 * binding the addresses specified in the Redis server configuration.
 *
 * The listening file descriptors are stored in the integer array 'fds'
 * and their number is set in '*count'.
 *
 * The addresses to bind are specified in the global server.bindaddr array
 * and their number is server.bindaddr_count. If the server configuration
 * contains no specific addresses to bind, this function will try to
 * bind * (all addresses) for both the IPv4 and IPv6 protocols.
 *
 * On success the function returns REDIS_OK.
 *
 * On error the function returns REDIS_ERR. For the function to be on
 * error, at least one of the server.bindaddr addresses was
 * impossible to bind, or no bind addresses were specified in the server
 * configuration but the function is not able to bind * for at least
 * one of the IPv4 or IPv6 protocols. */
int listenToPort(int port, int *fds, int *count) {
    int j;

    /* Force binding of 0.0.0.0 if no bind address is specified, always
     * entering the loop if j == 0. */
    if (server.bindaddr_count == 0) server.bindaddr[0] = NULL;
    for (j = 0; j < server.bindaddr_count || j == 0; j++) {
        if (server.bindaddr[j] == NULL) {
            /* Bind * for both IPv6 and IPv4, we enter here only if
             * server.bindaddr_count == 0. */
            fds[*count] = anetTcp6Server(server.neterr,port,NULL,
                server.tcp_backlog);
            if (fds[*count] != ANET_ERR) {
                anetNonBlock(NULL,fds[*count]);
                (*count)++;
            }
            fds[*count] = anetTcpServer(server.neterr,port,NULL,
                server.tcp_backlog);
            if (fds[*count] != ANET_ERR) {
                anetNonBlock(NULL,fds[*count]);
                (*count)++;
            }
            /* Exit the loop if we were able to bind * on IPv4 or IPv6,
             * otherwise fds[*count] will be ANET_ERR and we'll print an
             * error and return to the caller with an error. */
            if (*count) break;
        } else if (strchr(server.bindaddr[j],':')) {
            /* Bind IPv6 address. */
            fds[*count] = anetTcp6Server(server.neterr,port,server.bindaddr[j],
                server.tcp_backlog);
        } else {
            /* Bind IPv4 address. */
            fds[*count] = anetTcpServer(server.neterr,port,server.bindaddr[j],
                server.tcp_backlog);
        }
        if (fds[*count] == ANET_ERR) {
            redisLog(REDIS_WARNING,
                "Creating Server TCP listening socket %s:%d: %s",
                server.bindaddr[j] ? server.bindaddr[j] : "*",
                port, server.neterr);
            return REDIS_ERR;
        }
        anetNonBlock(NULL,fds[*count]);
        (*count)++;
    }
    return REDIS_OK;
}

/* Resets the stats that we expose via INFO or other means that we want
 * to reset via CONFIG RESETSTAT. The function is also used in order to
 * initialize these fields in initServer() at server startup. */
void resetServerStats(void) {
    server.stat_numcommands = 0;
    server.stat_numconnections = 0;
    server.stat_expiredkeys = 0;
    server.stat_evictedkeys = 0;
    server.stat_keyspace_misses = 0;
    server.stat_keyspace_hits = 0;
    server.stat_fork_time = 0;
    server.stat_rejected_conn = 0;
    server.stat_sync_full = 0;
    server.stat_sync_partial_ok = 0;
    server.stat_sync_partial_err = 0;
    memset(server.ops_sec_samples,0,sizeof(server.ops_sec_samples));
    server.ops_sec_idx = 0;
    server.ops_sec_last_sample_time = mstime();
    server.ops_sec_last_sample_ops = 0;
}

void initServer() {
    int j;

    // �����źŴ�����
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    setupSignalHandlers();

    // ���� syslog
    if (server.syslog_enabled) {
        openlog(server.syslog_ident, LOG_PID | LOG_NDELAY | LOG_NOWAIT,
            server.syslog_facility);
    }

    // ��ʼ�����������ݽṹ
    server.current_client = NULL;
    server.clients = listCreate();
    server.clients_to_close = listCreate();
    server.slaves = listCreate();
    server.monitors = listCreate();
    server.slaveseldb = -1; /* Force to emit the first SELECT command. */
    server.unblocked_clients = listCreate();
    server.ready_keys = listCreate();
    server.clients_waiting_acks = listCreate();
    server.get_ack_from_slaves = 0;
    server.clients_paused = 0;

    // �����������
    createSharedObjects();
    adjustOpenFilesLimit();
    server.el = aeCreateEventLoop(server.maxclients+REDIS_EVENTLOOP_FDSET_INCR);
    server.db = zmalloc(sizeof(redisDb)*server.dbnum);

    /* Open the TCP listening socket for the user commands. */
    // �� TCP �����˿ڣ����ڵȴ��ͻ��˵���������
    if (server.port != 0 &&
        listenToPort(server.port,server.ipfd,&server.ipfd_count) == REDIS_ERR)
        exit(1);

    /* Open the listening Unix domain socket. */
    // �� UNIX ���ض˿�
    if (server.unixsocket != NULL) {
        unlink(server.unixsocket); /* don't care if this fails */
        server.sofd = anetUnixServer(server.neterr,server.unixsocket,
            server.unixsocketperm, server.tcp_backlog);
        if (server.sofd == ANET_ERR) {
            redisLog(REDIS_WARNING, "Opening socket: %s", server.neterr);
            exit(1);
        }
        anetNonBlock(NULL,server.sofd);
    }

    /* Abort if there are no listening sockets at all. */
    if (server.ipfd_count == 0 && server.sofd < 0) {
        redisLog(REDIS_WARNING, "Configured to not listen anywhere, exiting.");
        exit(1);
    }

    /* Create the Redis databases, and initialize other internal state. */
    // ��������ʼ�����ݿ�ṹ
    for (j = 0; j < server.dbnum; j++) {
        server.db[j].dict = dictCreate(&dbDictType,NULL);
        server.db[j].expires = dictCreate(&keyptrDictType,NULL);
        server.db[j].blocking_keys = dictCreate(&keylistDictType,NULL);
        server.db[j].ready_keys = dictCreate(&setDictType,NULL);
        server.db[j].watched_keys = dictCreate(&keylistDictType,NULL);
        server.db[j].eviction_pool = evictionPoolAlloc();
        server.db[j].id = j;
        server.db[j].avg_ttl = 0;
    }

    // ���� PUBSUB ��ؽṹ
    server.pubsub_channels = dictCreate(&keylistDictType,NULL);
    server.pubsub_patterns = listCreate();
    listSetFreeMethod(server.pubsub_patterns,freePubsubPattern);
    listSetMatchMethod(server.pubsub_patterns,listMatchPubsubPattern);

    server.cronloops = 0;
    server.rdb_child_pid = -1;
    server.aof_child_pid = -1;
    aofRewriteBufferReset();
    server.aof_buf = sdsempty();
    server.lastsave = time(NULL); /* At startup we consider the DB saved. */
    server.lastbgsave_try = 0;    /* At startup we never tried to BGSAVE. */
    server.rdb_save_time_last = -1;
    server.rdb_save_time_start = -1;
    server.dirty = 0;
    resetServerStats();
    /* A few stats we don't want to reset: server startup time, and peak mem. */
    server.stat_starttime = time(NULL);
    server.stat_peak_memory = 0;
    server.resident_set_size = 0;
    server.lastbgsave_status = REDIS_OK;
    server.aof_last_write_status = REDIS_OK;
    server.aof_last_write_errno = 0;
    server.repl_good_slaves_count = 0;
    updateCachedTime();

    /* Create the serverCron() time event, that's our main way to process
     * background operations. */
    // Ϊ serverCron() ����ʱ���¼�
    if(aeCreateTimeEvent(server.el, 1, serverCron, NULL, NULL) == AE_ERR) {
        redisPanic("Can't create the serverCron time event.");
        exit(1);
    }

    /* Create an event handler for accepting new connections in TCP and Unix
     * domain sockets. */
    // Ϊ TCP ���ӹ�������Ӧ��accept��������
    // ���ڽ��ܲ�Ӧ��ͻ��˵� connect() ����
    for (j = 0; j < server.ipfd_count; j++) {
        if (aeCreateFileEvent(server.el, server.ipfd[j], AE_READABLE,
            acceptTcpHandler,NULL) == AE_ERR)
            {
                redisPanic(
                    "Unrecoverable error creating server.ipfd file event.");
            }
    }

    // Ϊ�����׽��ֹ���Ӧ������
    if (server.sofd > 0 && aeCreateFileEvent(server.el,server.sofd,AE_READABLE,
        acceptUnixHandler,NULL) == AE_ERR) redisPanic("Unrecoverable error creating server.sofd file event.");

    /* Open the AOF file if needed. */
    // ��� AOF �־û������Ѿ��򿪣���ô�򿪻򴴽�һ�� AOF �ļ�
    if (server.aof_state == REDIS_AOF_ON) {
        server.aof_fd = open(server.aof_filename,
                               O_WRONLY|O_APPEND|O_CREAT,0644);
        if (server.aof_fd == -1) {
            redisLog(REDIS_WARNING, "Can't open the append-only file: %s",
                strerror(errno));
            exit(1);
        }
    }

    /* 32 bit instances are limited to 4GB of address space, so if there is
     * no explicit limit in the user provided configuration we set a limit
     * at 3 GB using maxmemory with 'noeviction' policy'. This avoids
     * useless crashes of the Redis instance for out of memory. */
    // ���� 32 λʵ����˵��Ĭ�Ͻ��������ڴ������� 3 GB
    if (server.arch_bits == 32 && server.maxmemory == 0) {
        redisLog(REDIS_WARNING,"Warning: 32 bit instance detected but no memory limit set. Setting 3 GB maxmemory limit with 'noeviction' policy now.");
        server.maxmemory = 3072LL*(1024*1024); /* 3 GB */
        server.maxmemory_policy = REDIS_MAXMEMORY_NO_EVICTION;
    }

    // ����������� cluster ģʽ�򿪣���ô��ʼ�� cluster
    if (server.cluster_enabled) clusterInit();

    // ��ʼ�����ƹ����йصĽű�����
    replicationScriptCacheInit();

    // ��ʼ���ű�ϵͳ
    scriptingInit();

    // ��ʼ������ѯ����
    slowlogInit();

    // ��ʼ�� BIO ϵͳ
    bioInit();
}

/* Populates the Redis Command Table starting from the hard coded list
 * we have on top of redis.c file. 
 *
 * ���� redis.c �ļ������������б����������
 */
void populateCommandTable(void) {
    int j;

    // ���������
    int numcommands = sizeof(redisCommandTable)/sizeof(struct redisCommand);

    for (j = 0; j < numcommands; j++) {
        
        // ָ������
        struct redisCommand *c = redisCommandTable+j;

        // ȡ���ַ��� FLAG
        char *f = c->sflags;

        int retval1, retval2;

        // �����ַ��� FLAG ����ʵ�� FLAG
        while(*f != '\0') {
            switch(*f) {
            case 'w': c->flags |= REDIS_CMD_WRITE; break;
            case 'r': c->flags |= REDIS_CMD_READONLY; break;
            case 'm': c->flags |= REDIS_CMD_DENYOOM; break;
            case 'a': c->flags |= REDIS_CMD_ADMIN; break;
            case 'p': c->flags |= REDIS_CMD_PUBSUB; break;
            case 's': c->flags |= REDIS_CMD_NOSCRIPT; break;
            case 'R': c->flags |= REDIS_CMD_RANDOM; break;
            case 'S': c->flags |= REDIS_CMD_SORT_FOR_SCRIPT; break;
            case 'l': c->flags |= REDIS_CMD_LOADING; break;
            case 't': c->flags |= REDIS_CMD_STALE; break;
            case 'M': c->flags |= REDIS_CMD_SKIP_MONITOR; break;
            case 'k': c->flags |= REDIS_CMD_ASKING; break;
            default: redisPanic("Unsupported command flag"); break;
            }
            f++;
        }

        // ����������������
        retval1 = dictAdd(server.commands, sdsnew(c->name), c);

        /* Populate an additional dictionary that will be unaffected
         * by rename-command statements in redis.conf. 
         *
         * ������Ҳ������ԭʼ�����
         *
         * ԭʼ��������� redis.conf �����������Ӱ��
         */
        retval2 = dictAdd(server.orig_commands, sdsnew(c->name), c);

        redisAssert(retval1 == DICT_OK && retval2 == DICT_OK);
    }
}

/*
 * ����������е�ͳ����Ϣ
 */
void resetCommandTableStats(void) {
    int numcommands = sizeof(redisCommandTable)/sizeof(struct redisCommand);
    int j;

    for (j = 0; j < numcommands; j++) {
        struct redisCommand *c = redisCommandTable+j;

        // ����ʱ��
        c->microseconds = 0;

        // ������ô���
        c->calls = 0;
    }
}

/* ========================== Redis OP Array API ============================ */

void redisOpArrayInit(redisOpArray *oa) {
    oa->ops = NULL;
    oa->numops = 0;
}

int redisOpArrayAppend(redisOpArray *oa, struct redisCommand *cmd, int dbid,
                       robj **argv, int argc, int target)
{
    redisOp *op;

    oa->ops = zrealloc(oa->ops,sizeof(redisOp)*(oa->numops+1));
    op = oa->ops+oa->numops;
    op->cmd = cmd;
    op->dbid = dbid;
    op->argv = argv;
    op->argc = argc;
    op->target = target;
    oa->numops++;
    return oa->numops;
}

void redisOpArrayFree(redisOpArray *oa) {
    while(oa->numops) {
        int j;
        redisOp *op;

        oa->numops--;
        op = oa->ops+oa->numops;
        for (j = 0; j < op->argc; j++)
            decrRefCount(op->argv[j]);
        zfree(op->argv);
    }
    zfree(oa->ops);
}

/* ====================== Commands lookup and execution ===================== */

/*
 * ���ݸ����������֣�SDS������������
 */
struct redisCommand *lookupCommand(sds name) {
    return dictFetchValue(server.commands, name);
}

/*
 * ���ݸ����������֣�C �ַ���������������
 */
struct redisCommand *lookupCommandByCString(char *s) {
    struct redisCommand *cmd;
    sds name = sdsnew(s);

    cmd = dictFetchValue(server.commands, name);
    sdsfree(name);
    return cmd;
}

/* Lookup the command in the current table, if not found also check in
 * the original table containing the original command names unaffected by
 * redis.conf rename-command statement.
 *
 * �ӵ�ǰ����� server.commands �в��Ҹ������֣�
 * ���û�ҵ��Ļ����ͳ��Դ� server.orig_commands �в���δ��������ԭʼ����
 * ԭʼ���е����������� redis.conf �����������Ӱ��
 *
 * This is used by functions rewriting the argument vector such as
 * rewriteClientCommandVector() in order to set client->cmd pointer
 * correctly even if the command was renamed. 
 *
 * ��������������������֮����Ȼ����д����ʱ�ó���ȷ�����֡�
 */
struct redisCommand *lookupCommandOrOriginal(sds name) {

    // ���ҵ�ǰ��
    struct redisCommand *cmd = dictFetchValue(server.commands, name);

    // �������Ҫ�Ļ�������ԭʼ��
    if (!cmd) cmd = dictFetchValue(server.orig_commands,name);

    return cmd;
}

/* Propagate the specified command (in the context of the specified database id)
 * to AOF and Slaves.
 *
 * ��ָ������Լ�ִ�и�����������ģ��������ݿ� id ����Ϣ�������� AOF �� slave ��
 *
 * flags are an xor between:
 * FLAG ���������±�ʶ�� xor ��
 *
 * + REDIS_PROPAGATE_NONE (no propagation of command at all)
 *   ������
 *
 * + REDIS_PROPAGATE_AOF (propagate into the AOF file if is enabled)
 *   ������ AOF
 *
 * + REDIS_PROPAGATE_REPL (propagate into the replication link)
 *   ������ slave
 */
void propagate(struct redisCommand *cmd, int dbid, robj **argv, int argc,
               int flags)
{
    // ������ AOF
    if (server.aof_state != REDIS_AOF_OFF && flags & REDIS_PROPAGATE_AOF)
        feedAppendOnlyFile(cmd,dbid,argv,argc);

    // ������ slave
    if (flags & REDIS_PROPAGATE_REPL)
        replicationFeedSlaves(server.slaves,dbid,argv,argc);
}

/* Used inside commands to schedule the propagation of additional commands
 * after the current command is propagated to AOF / Replication. */
void alsoPropagate(struct redisCommand *cmd, int dbid, robj **argv, int argc,
                   int target)
{
    redisOpArrayAppend(&server.also_propagate,cmd,dbid,argv,argc,target);
}

/* It is possible to call the function forceCommandPropagation() inside a
 * Redis command implementaiton in order to to force the propagation of a
 * specific command execution into AOF / Replication. */
void forceCommandPropagation(redisClient *c, int flags) {
    if (flags & REDIS_PROPAGATE_REPL) c->flags |= REDIS_FORCE_REPL;
    if (flags & REDIS_PROPAGATE_AOF) c->flags |= REDIS_FORCE_AOF;
}

/* Call() is the core of Redis execution of a command */
// ���������ʵ�ֺ�����ִ������
void call(redisClient *c, int flags) {
    // start ��¼���ʼִ�е�ʱ��
    long long dirty, start, duration;
    // ��¼���ʼִ��ǰ�� FLAG
    int client_old_flags = c->flags;

    /* Sent the command to clients in MONITOR mode, only if the commands are
     * not generated from reading an AOF. */
    // ������ԵĻ���������͵� MONITOR
    if (listLength(server.monitors) &&
        !server.loading &&
        !(c->cmd->flags & REDIS_CMD_SKIP_MONITOR))
    {
        replicationFeedMonitors(c,server.monitors,c->db->id,c->argv,c->argc);
    }

    /* Call the command. */
    c->flags &= ~(REDIS_FORCE_AOF|REDIS_FORCE_REPL);
    redisOpArrayInit(&server.also_propagate);
    // ������ dirty ������ֵ
    dirty = server.dirty;
    // �������ʼִ�е�ʱ��
    start = ustime();
  //��������Կ�����ִ������ص��󣬷��ظ��ͻ��˵�OK����Ϣ���ڸûص����淵�صģ�������ʱ�ڸú��������propagate
  //���͵�slave�ģ�������slave���ͳɹ�ʧ���ǲ���Ӱ��Կͻ��˵�Ӧ���
  
    // ִ��ʵ�ֺ���
    c->cmd->proc(c); 
    // ��������ִ�кķѵ�ʱ��
    duration = ustime()-start;
    // ��������ִ��֮��� dirty ֵ
    dirty = server.dirty-dirty;

    /* When EVAL is called loading the AOF we don't want commands called
     * from Lua to go into the slowlog or to populate statistics. */
    // ������ Lua �з������������ SLOWLOG ��Ҳ������ͳ��
    if (server.loading && c->flags & REDIS_LUA_CLIENT)
        flags &= ~(REDIS_CALL_SLOWLOG | REDIS_CALL_STATS);

    /* If the caller is Lua, we want to force the EVAL caller to propagate
     * the script if the command flag or client flag are forcing the
     * propagation. */
    // ����������� Lua ����ô�������� FLAG �Ϳͻ��� FLAG
    // �򿪴�����propagate)��־
    if (c->flags & REDIS_LUA_CLIENT && server.lua_caller) {
        if (c->flags & REDIS_FORCE_REPL)
            server.lua_caller->flags |= REDIS_FORCE_REPL;
        if (c->flags & REDIS_FORCE_AOF)
            server.lua_caller->flags |= REDIS_FORCE_AOF;
    }

    /* Log the command into the Slow log if needed, and populate the
     * per-command statistics that we show in INFO commandstats. */
    // �������Ҫ��������ŵ� SLOWLOG ����
    if (flags & REDIS_CALL_SLOWLOG && c->cmd->proc != execCommand)
        slowlogPushEntryIfNeeded(c->argv,c->argc,duration); //��־������¼���ڸú������棬����durationʱ����бȽ��ж�
    // ���������ͳ����Ϣ
    if (flags & REDIS_CALL_STATS) {
        c->cmd->microseconds += duration;
        c->cmd->calls++;
    }

    /* Propagate the command into the AOF and replication link */
    // ������Ƶ� AOF �� slave �ڵ�
    if (flags & REDIS_CALL_PROPAGATE) {
        int flags = REDIS_PROPAGATE_NONE;

        // ǿ�� REPL ����
        if (c->flags & REDIS_FORCE_REPL) flags |= REDIS_PROPAGATE_REPL;

        // ǿ�� AOF ����
        if (c->flags & REDIS_FORCE_AOF) flags |= REDIS_PROPAGATE_AOF;

        // ������ݿ��б��޸ģ���ô���� REPL �� AOF ����
        if (dirty)
            flags |= (REDIS_PROPAGATE_REPL | REDIS_PROPAGATE_AOF);

        if (flags != REDIS_PROPAGATE_NONE) //������aof����slave
            propagate(c->cmd,c->db->id,c->argv,c->argc,flags);
    }

    /* Restore the old FORCE_AOF/REPL flags, since call can be executed
     * recursively. */
    // ���ͻ��˵� FLAG �ָ�������ִ��֮ǰ
    // ��Ϊ call ���ܻ�ݹ�ִ��
    c->flags &= ~(REDIS_FORCE_AOF|REDIS_FORCE_REPL);
    c->flags |= client_old_flags & (REDIS_FORCE_AOF|REDIS_FORCE_REPL);

    /* Handle the alsoPropagate() API to handle commands that want to propagate
     * multiple separated commands. */
    // �������������
    if (server.also_propagate.numops) {
        int j;
        redisOp *rop;

        for (j = 0; j < server.also_propagate.numops; j++) {
            rop = &server.also_propagate.ops[j];
            propagate(rop->cmd, rop->dbid, rop->argv, rop->argc, rop->target);
        }
        redisOpArrayFree(&server.also_propagate);
    }
    server.stat_numcommands++;
}

/* If this function gets called we already read a whole
 * command, arguments are in the client argv/argc fields.
 * processCommand() execute the command or prepare the
 * server for a bulk read from the client.
 *
 * �������ִ��ʱ�������Ѿ�������һ������������ͻ��ˣ�
 * �����������ִ��������
 * ���߷�����׼���ӿͻ����н���һ�ζ�ȡ��
 *
 * If 1 is returned the client is still alive and valid and
 * other operations can be performed by the caller. Otherwise
 * if 0 is returned the client was destroyed (i.e. after QUIT). 
 *
 * �������������� 1 ����ô��ʾ�ͻ�����ִ������֮����Ȼ���ڣ�
 * �����߿��Լ���ִ������������
 * ������������������ 0 ����ô��ʾ�ͻ����Ѿ������١�
 */
int processCommand(redisClient *c) {
    /* The QUIT command is handled separately. Normal command procs will
     * go through checking for replication and QUIT will cause trouble
     * when FORCE_REPLICATION is enabled and would be implemented in
     * a regular command proc. */
    // �ر��� quit ����
    if (!strcasecmp(c->argv[0]->ptr,"quit")) {
        addReply(c,shared.ok);
        c->flags |= REDIS_CLOSE_AFTER_REPLY;
        return REDIS_ERR;
    }

    /* Now lookup the command and check ASAP about trivial error conditions
     * such as wrong arity, bad command name and so forth. */
    // �����������������Ϸ��Լ�飬�Լ���������������
    c->cmd = c->lastcmd = lookupCommand(c->argv[0]->ptr);
    if (!c->cmd) {
        // û�ҵ�ָ��������
        flagTransaction(c);
        addReplyErrorFormat(c,"unknown command '%s'",
            (char*)c->argv[0]->ptr);
        return REDIS_OK;
    } else if ((c->cmd->arity > 0 && c->cmd->arity != c->argc) ||
               (c->argc < -c->cmd->arity)) {
        // ������������
        flagTransaction(c);
        addReplyErrorFormat(c,"wrong number of arguments for '%s' command",
            c->cmd->name);
        return REDIS_OK;
    }

   //addReplyErrorFormat(c,"yang test ...new command:%s", c->cmd->name);
    

    /* Check if the user is authenticated */
    // �����֤��Ϣ
    if (server.requirepass && !c->authenticated && c->cmd->proc != authCommand)
    {
        flagTransaction(c);
        addReply(c,shared.noautherr);
        return REDIS_OK;
    }

    /* If cluster is enabled perform the cluster redirection here.
     *
     * ��������˼�Ⱥģʽ����ô���������ת�������
     *
     * However we don't perform the redirection if:
     *
     * ���������������������֣���ô�ڵ㲻����ת��
     *
     * 1) The sender of this command is our master.
     *    ����ķ������Ǳ��ڵ�����ڵ�
     *
     * 2) The command has no key arguments. 
     *    ����û�� key ����
     */
    if (server.cluster_enabled &&
        !(c->flags & REDIS_MASTER) &&
        !(c->cmd->getkeys_proc == NULL && c->cmd->firstkey == 0))
    {
        int hashslot;

        // ��Ⱥ������
        if (server.cluster->state != REDIS_CLUSTER_OK) {
            flagTransaction(c);
            addReplySds(c,sdsnew("-CLUSTERDOWN The cluster is down. Use CLUSTER INFO for more information\r\n"));
            return REDIS_OK;

        // ��Ⱥ��������
        } else {
            int error_code;
            clusterNode *n = getNodeByQuery(c,c->cmd,c->argv,c->argc,&hashslot,&error_code);
            // ����ִ�ж����������
            if (n == NULL) {
                flagTransaction(c);
                if (error_code == REDIS_CLUSTER_REDIR_CROSS_SLOT) {
                    addReplySds(c,sdsnew("-CROSSSLOT Keys in request don't hash to the same slot\r\n"));
                } else if (error_code == REDIS_CLUSTER_REDIR_UNSTABLE) {
                    /* The request spawns mutliple keys in the same slot,
                     * but the slot is not "stable" currently as there is
                     * a migration or import in progress. */
                    addReplySds(c,sdsnew("-TRYAGAIN Multiple keys request during rehashing of slot\r\n"));
                } else {
                    redisPanic("getNodeByQuery() unknown error.");
                }
                return REDIS_OK;

            // ������ԵĲۺͼ����Ǳ��ڵ㴦��ģ�����ת��
            } else if (n != server.cluster->myself) {
                flagTransaction(c);
                //��slot�Ѿ�����Ǩ����server.cluster->migrating_slots_to[slot]�ڵ����importing�ڵ�Ĺ����У������ĳ��key���͵��˱��ڵ㣬
                //����߶Է�ask server.cluster->migrating_slots_to[slot],Ҳ���Ǹ�key�Ĳ���Ӧ�÷��뵽����µ�Ŀ�Ľڵ��У���getNodeByQuery����ʾask xxxx


                //���������migrating�����У���key����ڵ��ˣ�����move xxx
                 
                // -<ASK or MOVED> <slot> <ip>:<port>
                // ���� -ASK 10086 127.0.0.1:12345
                addReplySds(c,sdscatprintf(sdsempty(),
                    "-%s %d %s:%d\r\n",
                    (error_code == REDIS_CLUSTER_REDIR_ASK) ? "ASK" : "MOVED",
                    hashslot,n->ip,n->port));

                return REDIS_OK;
            }

            // ���ִ�е����˵���� key ���ڵĲ��ɱ��ڵ㴦��
            // ���߿ͻ���ִ�е����޲�������
        }
    }

    /* Handle the maxmemory directive.
     *
     * First we try to free some memory if possible (if there are volatile
     * keys in the dataset). If there are not the only thing we can do
     * is returning an error. */
    // �������������ڴ棬��ô����ڴ��Ƿ񳬹����ƣ�������Ӧ�Ĳ���
    if (server.maxmemory) {
        // ����ڴ��ѳ������ƣ���ô����ͨ��ɾ�����ڼ����ͷ��ڴ�
        int retval = freeMemoryIfNeeded();//������õ���maxmemory-policy volatile-lru�����ǿ��ɾ��KV����ʹû�й���
        // �������Ҫִ�е��������ռ�ô����ڴ棨REDIS_CMD_DENYOOM��
        // ����ǰ����ڴ��ͷ�ʧ�ܵĻ�
        // ��ô��ͻ��˷����ڴ����
        if ((c->cmd->flags & REDIS_CMD_DENYOOM) && retval == REDIS_ERR) { //����ڴ����꣬��û�й��ڵģ���ֱ�ӱ���
        //���������̭����Ϊvolatile-lru���򲻻��ߵ������棬�϶�����̭KV�������µ�KV
            flagTransaction(c);
            addReply(c, shared.oomerr);
            return REDIS_OK;
        }
    }

    /* Don't accept write commands if there are problems persisting on disk
     * and if this is a master instance. */
    // �������һ�������������������������֮ǰִ�� BGSAVE ʱ�����˴���
    // ��ô��ִ��д����
    if (((server.stop_writes_on_bgsave_err &&
          server.saveparamslen > 0 &&
          server.lastbgsave_status == REDIS_ERR) ||
          server.aof_last_write_status == REDIS_ERR) &&
        server.masterhost == NULL &&
        (c->cmd->flags & REDIS_CMD_WRITE ||
         c->cmd->proc == pingCommand)) {//���rdb aofʧ�ܣ����κ����ʧЧ����������ͨ������//CONFIG SET SAVE ""��ʾ����rdb���ܰѸ�ʧ�ܽ�ֹ��
        flagTransaction(c);
        if (server.aof_last_write_status == REDIS_OK)
            addReply(c, shared.bgsaveerr);
        else
            addReplySds(c,
                sdscatprintf(sdsempty(),
                "-MISCONF Errors writing to the AOF file: %s\r\n",
                strerror(server.aof_last_write_errno)));
        return REDIS_OK;
    }

    /* Don't accept write commands if there are not enough good slaves and
     * user configured the min-slaves-to-write option. */
    // ���������û���㹻���״̬���÷�����
    // ���� min-slaves-to-write ѡ���Ѵ�
    if (server.repl_min_slaves_to_write &&
        server.repl_min_slaves_max_lag &&
        c->cmd->flags & REDIS_CMD_WRITE &&
        server.repl_good_slaves_count < server.repl_min_slaves_to_write)
    {
        flagTransaction(c);
        addReply(c, shared.noreplicaserr);
        return REDIS_OK;
    }

    /* Don't accept write commands if this is a read only slave. But
     * accept write commands if this is our master. */
    // ��������������һ��ֻ�� slave �Ļ�����ô�ܾ�ִ��д����
    if (server.masterhost && server.repl_slave_ro &&
        !(c->flags & REDIS_MASTER) &&
        c->cmd->flags & REDIS_CMD_WRITE)
    {
        addReply(c, shared.roslaveerr);
        return REDIS_OK;
    }

    /* Only allow SUBSCRIBE and UNSUBSCRIBE in the context of Pub/Sub */
    // �ڶ����ڷ���ģʽ���������У�ֻ��ִ�ж��ĺ��˶���ص�����
    if ((dictSize(c->pubsub_channels) > 0 || listLength(c->pubsub_patterns) > 0)
        &&
        c->cmd->proc != subscribeCommand &&
        c->cmd->proc != unsubscribeCommand &&
        c->cmd->proc != psubscribeCommand &&
        c->cmd->proc != punsubscribeCommand) {
        addReplyError(c,"only (P)SUBSCRIBE / (P)UNSUBSCRIBE / QUIT allowed in this context");
        return REDIS_OK;
    }

    /* Only allow INFO and SLAVEOF when slave-serve-stale-data is no and
     * we are a slave with a broken link with master. */
    if (server.masterhost && server.repl_state != REDIS_REPL_CONNECTED &&
        server.repl_serve_stale_data == 0 &&
        !(c->cmd->flags & REDIS_CMD_STALE))
    {
        flagTransaction(c);
        addReply(c, shared.masterdownerr);
        return REDIS_OK;
    }

    /* Loading DB? Return an error if the command has not the
     * REDIS_CMD_LOADING flag. */
    // ��������������������ݵ����ݿ⣬��ôִֻ�д��� REDIS_CMD_LOADING
    // ��ʶ��������򽫳���
    if (server.loading && !(c->cmd->flags & REDIS_CMD_LOADING)) {
        addReply(c, shared.loadingerr); //����������ݵĹ������յ������򷵻ظ�ֵloadingerr
        return REDIS_OK;
    }

    /* Lua script too slow? Only allow a limited number of commands. */
    // Lua �ű���ʱ��ֻ����ִ���޶��Ĳ��������� SHUTDOWN �� SCRIPT KILL
    if (server.lua_timedout &&
          c->cmd->proc != authCommand &&
          c->cmd->proc != replconfCommand &&
        !(c->cmd->proc == shutdownCommand &&
          c->argc == 2 &&
          tolower(((char*)c->argv[1]->ptr)[0]) == 'n') &&
        !(c->cmd->proc == scriptCommand &&
          c->argc == 2 &&
          tolower(((char*)c->argv[1]->ptr)[0]) == 'k'))
    {
        flagTransaction(c);
        addReply(c, shared.slowscripterr);
        return REDIS_OK;
    }

    /* Exec the command */
    if (c->flags & REDIS_MULTI &&
        c->cmd->proc != execCommand && c->cmd->proc != discardCommand &&
        c->cmd->proc != multiCommand && c->cmd->proc != watchCommand)
    {
        // ��������������
        // �� EXEC �� DISCARD �� MULTI �� WATCH ����֮��
        // ������������ᱻ��ӵ����������
        queueMultiCommand(c);
        addReply(c,shared.queued);
    } else {
        // ִ������
        call(c, REDIS_CALL_FULL);

        c->woff = server.master_repl_offset;
        
        // ������Щ����������ļ�
        if (listLength(server.ready_keys))
            handleClientsBlockedOnLists();
    }

    return REDIS_OK;
}

/*================================== Shutdown =============================== */

/* Close listening sockets. Also unlink the unix domain socket if
 * unlink_unix_socket is non-zero. */
// �رռ����׽���
void closeListeningSockets(int unlink_unix_socket) {
    int j;

    for (j = 0; j < server.ipfd_count; j++) close(server.ipfd[j]);

    if (server.sofd != -1) close(server.sofd);

    if (server.cluster_enabled)
        for (j = 0; j < server.cfd_count; j++) close(server.cfd[j]);

    if (unlink_unix_socket && server.unixsocket) {
        redisLog(REDIS_NOTICE,"Removing the unix socket file.");
        unlink(server.unixsocket); /* don't care if this fails */
    }
}

int prepareForShutdown(int flags) {
    int save = flags & REDIS_SHUTDOWN_SAVE;
    int nosave = flags & REDIS_SHUTDOWN_NOSAVE;

    redisLog(REDIS_WARNING,"User requested shutdown...");

    /* Kill the saving child if there is a background saving in progress.
       We want to avoid race conditions, for instance our saving child may
       overwrite the synchronous saving did by SHUTDOWN. */
    // ����� BGSAVE ����ִ�У���ôɱ���ӽ��̣����⾺������
    if (server.rdb_child_pid != -1) {
        redisLog(REDIS_WARNING,"There is a child saving an .rdb. Killing it!");
        kill(server.rdb_child_pid,SIGUSR1);
        rdbRemoveTempFile(server.rdb_child_pid);
    }

    // ͬ��ɱ������ִ�� BGREWRITEAOF ���ӽ���
    if (server.aof_state != REDIS_AOF_OFF) {
        /* Kill the AOF saving child as the AOF we already have may be longer
         * but contains the full dataset anyway. */
        if (server.aof_child_pid != -1) {
            redisLog(REDIS_WARNING,
                "There is a child rewriting the AOF. Killing it!");
            kill(server.aof_child_pid,SIGUSR1);
        }
        /* Append only file: fsync() the AOF and exit */
        redisLog(REDIS_NOTICE,"Calling fsync() on the AOF file.");
        // ��������������д�뵽Ӳ������
        aof_fsync(server.aof_fd);
    }

    // ����ͻ���ִ�е��� SHUTDOWN save ������ SAVE ���ܱ���
    // ��ôִ�� SAVE ����
    if ((server.saveparamslen > 0 && !nosave) || save) {
        redisLog(REDIS_NOTICE,"Saving the final RDB snapshot before exiting.");
        /* Snapshotting. Perform a SYNC SAVE and exit */
        if (rdbSave(server.rdb_filename) != REDIS_OK) {
            /* Ooops.. error saving! The best we can do is to continue
             * operating. Note that if there was a background saving process,
             * in the next cron() Redis will be notified that the background
             * saving aborted, handling special stuff like slaves pending for
             * synchronization... */
            redisLog(REDIS_WARNING,"Error trying to save the DB, can't exit.");
            return REDIS_ERR;
        }
    }

    // �Ƴ� pidfile �ļ�
    if (server.daemonize) {
        redisLog(REDIS_NOTICE,"Removing the pid file.");
        unlink(server.pidfile);
    }

    /* Close the listening sockets. Apparently this allows faster restarts. */
    // �رռ����׽��֣�������������ʱ����һ��
    closeListeningSockets(1);
    redisLog(REDIS_WARNING,"%s is now ready to exit, bye bye...",
        server.sentinel_mode ? "Sentinel" : "Redis");
    return REDIS_OK;
}

/*================================== Commands =============================== */

/* Return zero if strings are the same, non-zero if they are not.
 * The comparison is performed in a way that prevents an attacker to obtain
 * information about the nature of the strings just monitoring the execution
 * time of the function.
 *
 * Note that limiting the comparison length to strings up to 512 bytes we
 * can avoid leaking any information about the password length and any
 * possible branch misprediction related leak.
 */
int time_independent_strcmp(char *a, char *b) {
    char bufa[REDIS_AUTHPASS_MAX_LEN], bufb[REDIS_AUTHPASS_MAX_LEN];
    /* The above two strlen perform len(a) + len(b) operations where either
     * a or b are fixed (our password) length, and the difference is only
     * relative to the length of the user provided string, so no information
     * leak is possible in the following two lines of code. */
    int alen = strlen(a);
    int blen = strlen(b);
    int j;
    int diff = 0;

    /* We can't compare strings longer than our static buffers.
     * Note that this will never pass the first test in practical circumstances
     * so there is no info leak. */
    if (alen > sizeof(bufa) || blen > sizeof(bufb)) return 1;

    memset(bufa,0,sizeof(bufa));        /* Constant time. */
    memset(bufb,0,sizeof(bufb));        /* Constant time. */
    /* Again the time of the following two copies is proportional to
     * len(a) + len(b) so no info is leaked. */
    memcpy(bufa,a,alen);
    memcpy(bufb,b,blen);

    /* Always compare all the chars in the two buffers without
     * conditional expressions. */
    for (j = 0; j < sizeof(bufa); j++) {
        diff |= (bufa[j] ^ bufb[j]);
    }
    /* Length must be equal as well. */
    diff |= alen ^ blen;
    return diff; /* If zero strings are the same. */
}

//�ӷ���������AUTH xxx, syncWithMaster����
void authCommand(redisClient *c) {
    if (!server.requirepass) {
        addReplyError(c,"Client sent AUTH, but no password is set");
    } else if (!time_independent_strcmp(c->argv[1]->ptr, server.requirepass)) {
      c->authenticated = 1;
      addReply(c,shared.ok);
    } else {
      c->authenticated = 0;
      addReplyError(c,"invalid password");
    }
}

//�ӷ���������ping syncWithMaster����
void pingCommand(redisClient *c) {
    addReply(c,shared.pong);
}

void echoCommand(redisClient *c) {
    addReplyBulk(c,c->argv[1]);
}

void timeCommand(redisClient *c) {
    struct timeval tv;

    /* gettimeofday() can only fail if &tv is a bad address so we
     * don't check for errors. */
    gettimeofday(&tv,NULL);
    addReplyMultiBulkLen(c,2);
    addReplyBulkLongLong(c,tv.tv_sec);
    addReplyBulkLongLong(c,tv.tv_usec);
}

/* Convert an amount of bytes into a human readable string in the form
 * of 100B, 2G, 100M, 4K, and so forth. */ //�ֽڵ�K M Gת��
void bytesToHuman(char *s, unsigned long long n) {
    double d;

    if (n < 1024) {
        /* Bytes */
        sprintf(s,"%lluB",n);
        return;
    } else if (n < (1024*1024)) {
        d = (double)n/(1024);
        sprintf(s,"%.2fK",d);
    } else if (n < (1024LL*1024*1024)) {
        d = (double)n/(1024*1024);
        sprintf(s,"%.2fM",d);
    } else if (n < (1024LL*1024*1024*1024)) {
        d = (double)n/(1024LL*1024*1024);
        sprintf(s,"%.2fG",d);
    }
}

/* Create the string returned by the INFO command. This is decoupled
 * by the INFO command itself as we need to report the same information
 * on memory corruption problems. */ //info ���״̬�鿴genRedisInfoString
sds genRedisInfoString(char *section) {
    sds info = sdsempty();
    time_t uptime = server.unixtime-server.stat_starttime;
    int j, numcommands;
    struct rusage self_ru, c_ru;
    unsigned long lol, bib;
    int allsections = 0, defsections = 0;
    int sections = 0;

    if (section) {
        allsections = strcasecmp(section,"all") == 0;
        defsections = strcasecmp(section,"default") == 0;
    }

    getrusage(RUSAGE_SELF, &self_ru);
    getrusage(RUSAGE_CHILDREN, &c_ru);
    getClientsMaxBuffers(&lol,&bib);

    /* Server */
    if (allsections || defsections || !strcasecmp(section,"server")) {
        static int call_uname = 1;
        static struct utsname name;
        char *mode;

        if (server.cluster_enabled) mode = "cluster";
        else if (server.sentinel_mode) mode = "sentinel";
        else mode = "standalone";
    
        if (sections++) info = sdscat(info,"\r\n");

        if (call_uname) {
            /* Uname can be slow and is always the same output. Cache it. */
            uname(&name);
            call_uname = 0;
        }

        info = sdscatprintf(info,
            "# Server\r\n"
            "redis_version:%s\r\n"
            "redis_git_sha1:%s\r\n"
            "redis_git_dirty:%d\r\n"
            "redis_build_id:%llx\r\n"
            "redis_mode:%s\r\n"
            "os:%s %s %s\r\n"
            "arch_bits:%d\r\n"
            "multiplexing_api:%s\r\n"
            "gcc_version:%d.%d.%d\r\n"
            "process_id:%ld\r\n"
            "run_id:%s\r\n"
            "tcp_port:%d\r\n"
            "uptime_in_seconds:%jd\r\n"
            "uptime_in_days:%jd\r\n"
            "hz:%d\r\n"
            "lru_clock:%ld\r\n"
            "config_file:%s\r\n",
            REDIS_VERSION,
            redisGitSHA1(),
            strtol(redisGitDirty(),NULL,10) > 0,
            (unsigned long long) redisBuildId(),
            mode,
            name.sysname, name.release, name.machine,
            server.arch_bits,
            aeGetApiName(),
#ifdef __GNUC__
            __GNUC__,__GNUC_MINOR__,__GNUC_PATCHLEVEL__,
#else
            0,0,0,
#endif
            (long) getpid(),
            server.runid,
            server.port,
            (intmax_t)uptime,
            (intmax_t)(uptime/(3600*24)),
            server.hz,
            (unsigned long) server.lruclock,
            server.configfile ? server.configfile : "");
    }

    /* Clients */
    if (allsections || defsections || !strcasecmp(section,"clients")) {
        if (sections++) info = sdscat(info,"\r\n");
        info = sdscatprintf(info,
            "# Clients\r\n"
            "connected_clients:%lu\r\n"
            "client_longest_output_list:%lu\r\n"
            "client_biggest_input_buf:%lu\r\n"
            "blocked_clients:%d\r\n",
            listLength(server.clients)-listLength(server.slaves),
            lol, bib,
            server.bpop_blocked_clients);
    }

    /* Memory */
    if (allsections || defsections || !strcasecmp(section,"memory")) {
        char hmem[64];
        char peak_hmem[64];
        size_t zmalloc_used = zmalloc_used_memory();

        /* Peak memory is updated from time to time by serverCron() so it
         * may happen that the instantaneous value is slightly bigger than
         * the peak value. This may confuse users, so we update the peak
         * if found smaller than the current memory usage. */
        if (zmalloc_used > server.stat_peak_memory)
            server.stat_peak_memory = zmalloc_used;

        bytesToHuman(hmem,zmalloc_used);
        bytesToHuman(peak_hmem, server.stat_peak_memory);
        if (sections++) info = sdscat(info,"\r\n");
        info = sdscatprintf(info,
            "# Memory\r\n"
            "used_memory:%zu\r\n"
            "used_memory_human:%s\r\n"
            "used_memory_rss:%zu\r\n"
            "used_memory_peak:%zu\r\n"
            "used_memory_peak_human:%s\r\n"
            "used_memory_lua:%lld\r\n"
            "mem_fragmentation_ratio:%.2f\r\n"
            "mem_allocator:%s\r\n",
            zmalloc_used,
            hmem,
            server.resident_set_size,
            server.stat_peak_memory,
            peak_hmem,
            ((long long)lua_gc(server.lua,LUA_GCCOUNT,0))*1024LL,
            zmalloc_get_fragmentation_ratio(server.resident_set_size),
            ZMALLOC_LIB
            );
    }

    /* Persistence */
    if (allsections || defsections || !strcasecmp(section,"persistence")) {
        if (sections++) info = sdscat(info,"\r\n");
        info = sdscatprintf(info,
            "# Persistence\r\n"
            "loading:%d\r\n"
            "rdb_changes_since_last_save:%lld\r\n"
            "rdb_bgsave_in_progress:%d\r\n"
            "rdb_last_save_time:%jd\r\n"
            "rdb_last_bgsave_status:%s\r\n"
            "rdb_last_bgsave_time_sec:%jd\r\n"
            "rdb_current_bgsave_time_sec:%jd\r\n"
            "aof_enabled:%d\r\n"
            "aof_rewrite_in_progress:%d\r\n"
            "aof_rewrite_scheduled:%d\r\n"
            "aof_last_rewrite_time_sec:%jd\r\n"
            "aof_current_rewrite_time_sec:%jd\r\n"
            "aof_last_bgrewrite_status:%s\r\n"
            "aof_last_write_status:%s\r\n",
            server.loading,
            server.dirty,
            server.rdb_child_pid != -1,
            (intmax_t)server.lastsave,
            (server.lastbgsave_status == REDIS_OK) ? "ok" : "err",
            (intmax_t)server.rdb_save_time_last,
            (intmax_t)((server.rdb_child_pid == -1) ?
                -1 : time(NULL)-server.rdb_save_time_start),
            server.aof_state != REDIS_AOF_OFF,
            server.aof_child_pid != -1,
            server.aof_rewrite_scheduled,
            (intmax_t)server.aof_rewrite_time_last,
            (intmax_t)((server.aof_child_pid == -1) ?
                -1 : time(NULL)-server.aof_rewrite_time_start),
            (server.aof_lastbgrewrite_status == REDIS_OK) ? "ok" : "err",
            (server.aof_last_write_status == REDIS_OK) ? "ok" : "err");

        if (server.aof_state != REDIS_AOF_OFF) {
            info = sdscatprintf(info,
                "aof_current_size:%lld\r\n"
                "aof_base_size:%lld\r\n"
                "aof_pending_rewrite:%d\r\n"
                "aof_buffer_length:%zu\r\n"
                "aof_rewrite_buffer_length:%lu\r\n"
                "aof_pending_bio_fsync:%llu\r\n"
                "aof_delayed_fsync:%lu\r\n",
                (long long) server.aof_current_size,
                (long long) server.aof_rewrite_base_size,
                server.aof_rewrite_scheduled,
                sdslen(server.aof_buf),
                aofRewriteBufferSize(),
                bioPendingJobsOfType(REDIS_BIO_AOF_FSYNC),
                server.aof_delayed_fsync);
        }

        if (server.loading) {
            double perc;
            time_t eta, elapsed;
            off_t remaining_bytes = server.loading_total_bytes-
                                    server.loading_loaded_bytes;

            perc = ((double)server.loading_loaded_bytes /
                   server.loading_total_bytes) * 100;

            elapsed = server.unixtime-server.loading_start_time;
            if (elapsed == 0) {
                eta = 1; /* A fake 1 second figure if we don't have
                            enough info */
            } else {
                eta = (elapsed*remaining_bytes)/server.loading_loaded_bytes;
            }

            info = sdscatprintf(info,
                "loading_start_time:%jd\r\n"
                "loading_total_bytes:%llu\r\n"
                "loading_loaded_bytes:%llu\r\n"
                "loading_loaded_perc:%.2f\r\n"
                "loading_eta_seconds:%jd\r\n",
                (intmax_t) server.loading_start_time,
                (unsigned long long) server.loading_total_bytes,
                (unsigned long long) server.loading_loaded_bytes,
                perc,
                (intmax_t)eta
            );
        }
    }

    /* Stats */
    if (allsections || defsections || !strcasecmp(section,"stats")) {
        if (sections++) info = sdscat(info,"\r\n");
        info = sdscatprintf(info,
            "# Stats\r\n"
            "total_connections_received:%lld\r\n"
            "total_commands_processed:%lld\r\n"
            "instantaneous_ops_per_sec:%lld\r\n"
            "rejected_connections:%lld\r\n"
            "sync_full:%lld\r\n"
            "sync_partial_ok:%lld\r\n"
            "sync_partial_err:%lld\r\n"
            "expired_keys:%lld\r\n"
            "evicted_keys:%lld\r\n"
            "keyspace_hits:%lld\r\n"
            "keyspace_misses:%lld\r\n"
            "pubsub_channels:%ld\r\n"
            "pubsub_patterns:%lu\r\n"
            "latest_fork_usec:%lld\r\n"
            "migrate_cached_sockets:%ld\r\n",
            server.stat_numconnections,
            server.stat_numcommands,
            getOperationsPerSecond(),
            server.stat_rejected_conn,
            server.stat_sync_full,
            server.stat_sync_partial_ok,
            server.stat_sync_partial_err,
            server.stat_expiredkeys,
            server.stat_evictedkeys,
            server.stat_keyspace_hits,
            server.stat_keyspace_misses,
            dictSize(server.pubsub_channels),
            listLength(server.pubsub_patterns),
            server.stat_fork_time,
            dictSize(server.migrate_cached_sockets));
    }

    /* Replication */
    if (allsections || defsections || !strcasecmp(section,"replication")) {
        if (sections++) info = sdscat(info,"\r\n");
        info = sdscatprintf(info,
            "# Replication\r\n"
            "role:%s\r\n",
            server.masterhost == NULL ? "master" : "slave");
        if (server.masterhost) {
            long long slave_repl_offset = 1;

            if (server.master)
                slave_repl_offset = server.master->reploff;
            else if (server.cached_master)
                slave_repl_offset = server.cached_master->reploff;

            info = sdscatprintf(info,
                "master_host:%s\r\n"
                "master_port:%d\r\n"
                "master_link_status:%s\r\n"  //ֻ��������ͬ��������ʱ���ΪUP��������Ļ�ѹ������������Ҫ��������ͬ���������Ϊdown
                "master_last_io_seconds_ago:%d\r\n"
                "master_sync_in_progress:%d\r\n"
                "slave_repl_offset:%lld\r\n"
                ,server.masterhost,
                server.masterport,
                (server.repl_state == REDIS_REPL_CONNECTED) ?
                    "up" : "down",
                server.master ?   
                ((int)(server.unixtime-server.master->lastinteraction)) : -1,
                server.repl_state == REDIS_REPL_TRANSFER,
                slave_repl_offset
            );

            if (server.repl_state == REDIS_REPL_TRANSFER) {
                info = sdscatprintf(info,
                    "master_sync_left_bytes:%lld\r\n"
                    "master_sync_last_io_seconds_ago:%d\r\n"
                    , (long long)
                        (server.repl_transfer_size - server.repl_transfer_read),
                    (int)(server.unixtime-server.repl_transfer_lastio)
                );
            }

            if (server.repl_state != REDIS_REPL_CONNECTED) {
                info = sdscatprintf(info,
                    "master_link_down_since_seconds:%jd\r\n",
                    (intmax_t)server.unixtime-server.repl_down_since);
            }
            info = sdscatprintf(info,
                "slave_priority:%d\r\n"
                "slave_read_only:%d\r\n",
                server.slave_priority,
                server.repl_slave_ro);
        }

        info = sdscatprintf(info,
            "connected_slaves:%lu\r\n",
            listLength(server.slaves));

        /* If min-slaves-to-write is active, write the number of slaves
         * currently considered 'good'. */
        if (server.repl_min_slaves_to_write &&
            server.repl_min_slaves_max_lag) {
            info = sdscatprintf(info,
                "min_slaves_good_slaves:%d\r\n",
                server.repl_good_slaves_count);
        }

        if (listLength(server.slaves)) {
            int slaveid = 0;
            listNode *ln;
            listIter li;

            listRewind(server.slaves,&li);
            while((ln = listNext(&li))) {
                redisClient *slave = listNodeValue(ln);
                char *state = NULL;
                char ip[REDIS_IP_STR_LEN];
                int port;
                long lag = 0;

                if (anetPeerToString(slave->fd,ip,sizeof(ip),&port) == -1) continue;
                switch(slave->replstate) {
                case REDIS_REPL_WAIT_BGSAVE_START:
                case REDIS_REPL_WAIT_BGSAVE_END:
                    state = "wait_bgsave";
                    break;
                case REDIS_REPL_SEND_BULK:
                    state = "send_bulk";
                    break;
                case REDIS_REPL_ONLINE:
                    state = "online";
                    break;
                }
                if (state == NULL) continue;
                if (slave->replstate == REDIS_REPL_ONLINE)
                    lag = time(NULL) - slave->repl_ack_time;

                info = sdscatprintf(info,
                    "slave%d:ip=%s,port=%d,state=%s,"
                    "offset=%lld,lag=%ld\r\n",
                    slaveid,ip,slave->slave_listening_port,state,
                    slave->repl_ack_off, lag);
                slaveid++;
            }
        }
        info = sdscatprintf(info,
            "master_repl_offset:%lld\r\n"
            "repl_backlog_active:%d\r\n"
            "repl_backlog_size:%lld\r\n"
            "repl_backlog_first_byte_offset:%lld\r\n"
            "repl_backlog_histlen:%lld\r\n",
            server.master_repl_offset,
            server.repl_backlog != NULL,
            server.repl_backlog_size,
            server.repl_backlog_off,
            server.repl_backlog_histlen);
    }

    /* CPU */
    if (allsections || defsections || !strcasecmp(section,"cpu")) {
        if (sections++) info = sdscat(info,"\r\n");
        info = sdscatprintf(info,
        "# CPU\r\n"
        "used_cpu_sys:%.2f\r\n"
        "used_cpu_user:%.2f\r\n"
        "used_cpu_sys_children:%.2f\r\n"
        "used_cpu_user_children:%.2f\r\n",
        (float)self_ru.ru_stime.tv_sec+(float)self_ru.ru_stime.tv_usec/1000000,
        (float)self_ru.ru_utime.tv_sec+(float)self_ru.ru_utime.tv_usec/1000000,
        (float)c_ru.ru_stime.tv_sec+(float)c_ru.ru_stime.tv_usec/1000000,
        (float)c_ru.ru_utime.tv_sec+(float)c_ru.ru_utime.tv_usec/1000000);
    }

    /* cmdtime */
    if (allsections || !strcasecmp(section,"commandstats")) {
        if (sections++) info = sdscat(info,"\r\n");
        info = sdscatprintf(info, "# Commandstats\r\n");
        numcommands = sizeof(redisCommandTable)/sizeof(struct redisCommand);
        for (j = 0; j < numcommands; j++) {
            struct redisCommand *c = redisCommandTable+j;

            if (!c->calls) continue;
            info = sdscatprintf(info,
                "cmdstat_%s:calls=%lld,usec=%lld,usec_per_call=%.2f\r\n",
                c->name, c->calls, c->microseconds,
                (c->calls == 0) ? 0 : ((float)c->microseconds/c->calls));
        }
    }

    /* Cluster */
    if (allsections || defsections || !strcasecmp(section,"cluster")) {
        if (sections++) info = sdscat(info,"\r\n");
        info = sdscatprintf(info,
        "# Cluster\r\n"
        "cluster_enabled:%d\r\n",
        server.cluster_enabled);
    }

    /* Key space */
    if (allsections || defsections || !strcasecmp(section,"keyspace")) {
        if (sections++) info = sdscat(info,"\r\n");
        info = sdscatprintf(info, "# Keyspace\r\n");
        for (j = 0; j < server.dbnum; j++) {
            long long keys, vkeys;

            keys = dictSize(server.db[j].dict);
            vkeys = dictSize(server.db[j].expires);
            if (keys || vkeys) {
                info = sdscatprintf(info,
                    "db%d:keys=%lld,expires=%lld,avg_ttl=%lld\r\n",
                    j, keys, vkeys, server.db[j].avg_ttl);
            }
        }
    }
    return info;
}

void infoCommand(redisClient *c) {
    char *section = c->argc == 2 ? c->argv[1]->ptr : "default";

    if (c->argc > 2) {
        addReply(c,shared.syntaxerr);
        return;
    }
    sds info = genRedisInfoString(section);
    addReplySds(c,sdscatprintf(sdsempty(),"$%lu\r\n",
        (unsigned long)sdslen(info)));
    addReplySds(c,info);
    addReply(c,shared.crlf);
}

void monitorCommand(redisClient *c) {
    /* ignore MONITOR if already slave or in monitor mode */

    // ����ͻ����Ǵӷ������������Ѿ��Ǽ�����
    if (c->flags & REDIS_SLAVE) return;

    // �� SLAVE ��־�� MONITOR ��־
    c->flags |= (REDIS_SLAVE|REDIS_MONITOR);

    // ��ӿͻ��˵� monitors ����
    listAddNodeTail(server.monitors,c);

    // ���� OK
    addReply(c,shared.ok);
}

/* ============================ Maxmemory directive  ======================== */

/* freeMemoryIfNeeded() gets called when 'maxmemory' is set on the config
 * file to limit the max memory used by the server, before processing a
 * command.
 *
 * �˺����� maxmemory ѡ��򿪣������ڴ泬������ʱ���á�
 *
 * The goal of the function is to free enough memory to keep Redis under the
 * configured memory limit.
 *
 * �˺�����Ŀ�����ͷ� Redis ��ռ���ڴ��� maxmemory ѡ�����õ����ֵ֮�¡�
 *
 * The function starts calculating how many bytes should be freed to keep
 * Redis under the limit, and enters a loop selecting the best keys to
 * evict accordingly to the configured policy.
 *
 * �����ȼ������Ҫ�ͷŶ����ֽڲ��ܵ��� maxmemory ѡ�����õ����ֵ��
 * Ȼ�����ָ������̭�㷨��ѡ�����ʺϱ���̭�ļ������ͷš�
 *
 * If all the bytes needed to return back under the limit were freed the
 * function returns REDIS_OK, otherwise REDIS_ERR is returned, and the caller
 * should block the execution of commands that will result in more memory
 * used by the server.
 *
 * ����ɹ��ͷ��������������ڴ棬��ô�������� REDIS_OK �������������� REDIS_ERR ��
 * ����ִֹ���µ����
 *
 * ------------------------------------------------------------------------
 *
 * LRU approximation algorithm
 *
 * Redis uses an approximation of the LRU algorithm that runs in constant
 * memory. Every time there is a key to expire, we sample N keys (with
 * N very small, usually in around 5) to populate a pool of best keys to
 * evict of M keys (the pool size is defined by REDIS_EVICTION_POOL_SIZE).
 *
 * The N keys sampled are added in the pool of good keys to expire (the one
 * with an old access time) if they are better than one of the current keys
 * in the pool.
 *
 * After the pool is populated, the best key we have in the pool is expired.
 * However note that we don't remove keys from the pool when they are deleted
 * so the pool may contain keys that no longer exist.
 *
 * When we try to evict a key, and all the entries in the pool don't exist
 * we populate it again. This time we'll be sure that the pool has at least
 * one key that can be evicted, if there is at least one key that can be
 * evicted in the whole database. */

/* Create a new eviction pool. */
struct evictionPoolEntry *evictionPoolAlloc(void) {
    struct evictionPoolEntry *ep;
    int j;

    ep = zmalloc(sizeof(*ep)*REDIS_EVICTION_POOL_SIZE);
    for (j = 0; j < REDIS_EVICTION_POOL_SIZE; j++) {
        ep[j].idle = 0;
        ep[j].key = NULL;
    }
    return ep;
}

/* This is an helper function for freeMemoryIfNeeded(), it is used in order
 * to populate the evictionPool with a few entries every time we want to
 * expire a key. Keys with idle time smaller than one of the current
 * keys are added. Keys are always added if there are free entries.
 *
 * We insert keys on place in ascending order, so keys with the smaller
 * idle time are on the left, and keys with the higher idle time on the
 * right. */

#define EVICTION_SAMPLES_ARRAY_SIZE 16
void evictionPoolPopulate(dict *sampledict, dict *keydict, struct evictionPoolEntry *pool) {
    int j, k, count;
    dictEntry *_samples[EVICTION_SAMPLES_ARRAY_SIZE];
    dictEntry **samples;

    /* Try to use a static buffer: this function is a big hit...
     * Note: it was actually measured that this helps. */
    /*
    ����ʱ������û����õ�maxmemory-policy�����ʵ�������һ����LRU��TTL���������LRU��TTL���Բ��������
    redis������key�������������ļ��е�maxmemory-samples��key��Ϊ�����ؽ��г�������
    */
    //��������������ļ������õ�samplesС��16����ֱ��ʹ��EVICTION_SAMPLES_ARRAY_SIZE
    if (server.maxmemory_samples <= EVICTION_SAMPLES_ARRAY_SIZE) {
        samples = _samples;
    } else {
        samples = zmalloc(sizeof(samples[0])*server.maxmemory_samples);
    }

#if 1 /* Use bulk get by default. */
    //���������������ȡserver.maxmemory_samples������
    count = dictGetRandomKeys(sampledict,samples,server.maxmemory_samples);
#else
    count = server.maxmemory_samples;
    for (j = 0; j < count; j++) samples[j] = dictGetRandomKey(sampledict);
#endif

    for (j = 0; j < count; j++) {
        unsigned long long idle;
        sds key;
        robj *o;
        dictEntry *de;

        de = samples[j];
        key = dictGetKey(de);
        /* If the dictionary we are sampling from is not the main
         * dictionary (but the expires one) we need to lookup the key
         * again in the key dictionary to obtain the value object. */
        if (sampledict != keydict) de = dictFind(keydict, key);
        o = dictGetVal(de);
        //�����key�Ѿ����û�з�����
        idle = estimateObjectIdleTime(o);

        /* Insert the element inside the pool.
         * First, find the first empty bucket or the first populated
         * bucket that has an idle time smaller than our idle time. */
        k = 0;
        while (k < REDIS_EVICTION_POOL_SIZE &&
               pool[k].key &&
               pool[k].idle < idle) k++; //KV��pool�а���idle��С��������
        if (k == 0 && pool[REDIS_EVICTION_POOL_SIZE-1].key != NULL) {
            /* Can't insert if the element is < the worst element we have
             * and there are no empty buckets. */
            continue;
        } else if (k < REDIS_EVICTION_POOL_SIZE && pool[k].key == NULL) {
            /* Inserting into empty position. No setup needed before insert. */
        } else {
            /* Inserting in the middle. Now k points to the first element
             * greater than the element to insert.  */
            //�ƶ�Ԫ�أ�memmove,���пռ���Բ�����Ԫ��
            if (pool[REDIS_EVICTION_POOL_SIZE-1].key == NULL) {
                /* Free space on the right? Insert at k shifting
                 * all the elements from k to end to the right. */
                memmove(pool+k+1,pool+k,
                    sizeof(pool[0])*(REDIS_EVICTION_POOL_SIZE-k-1));
            } else { //�Ѿ�û�пռ������Ԫ��ʱ������һ��Ԫ��ɾ��
                /* No free space on right? Insert at k-1 */
                k--;
                /* Shift all elements on the left of k (included) to the
                 * left, so we discard the element with smaller idle time. */
                sdsfree(pool[0].key);
                memmove(pool,pool+1,sizeof(pool[0])*k);
            }
        }
        pool[k].key = sdsdup(key);
        pool[k].idle = idle;
    }
    if (samples != _samples) zfree(samples);
}

/*
redis �ڴ����ݼ���С������һ����С��ʱ�򣬾ͻ�ʩ��������̭���ԡ�redis �ṩ 6��������̭���ԣ�

�����volatile��ͷ�Ĺ��ڲ��ԣ����expires�ֵ��л�ȡKV�� allkeys�����Ǵ�dict�ֵ��л�ȡ
lruʵ���������ȡmaxmemory-samples��KV�������û�з��ʵģ�Ҳ����idleʱ������keyɾ��
ttl���ȡ����maxmemory_samples��KV��˭���ȹ��ھͽ�������
randomΪֱ��ȡһ��key��Ȼ���������ܹ���û����

volatile-lru���������ù���ʱ������ݼ���server.db[i].expires������ѡ�������ʹ�õ�������̭
volatile-ttl���������ù���ʱ������ݼ���server.db[i].expires������ѡ��Ҫ���ڵ�������̭
volatile-random���������ù���ʱ������ݼ���server.db[i].expires��������ѡ��������̭
allkeys-lru�������ݼ���server.db[i].dict������ѡ�������ʹ�õ�������̭
allkeys-random�������ݼ���server.db[i].dict��������ѡ��������̭
no-enviction�����𣩣���ֹ��������
redis ȷ������ĳ����ֵ�Ժ󣬻�ɾ��������ݲ�������������ݱ����Ϣ���������أ�AOF �־û����ʹӻ����������ӣ���
*/
//ע��activeExpireCycle(����ɾ��)��freeMemoryIfNeeded(�������������ڴ棬�������ڴ���)  expireIfNeeded(��������ɾ�����ɶԸü�������ʱ������ж��Ƿ�ʱ)������
int freeMemoryIfNeeded(void) {
    size_t mem_used, mem_tofree, mem_freed;
    int slaves = listLength(server.slaves);

    /* Remove the size of slaves output buffers and AOF buffer from the
     * count of used memory. */
    // ����� Redis Ŀǰռ�õ��ڴ���������������������ڴ治��������ڣ�
    // 1���ӷ�������������������ڴ�
    // 2��AOF ���������ڴ�
    mem_used = zmalloc_used_memory();
    if (slaves) {
        listIter li;
        listNode *ln;

        listRewind(server.slaves,&li);
        while((ln = listNext(&li))) {
            redisClient *slave = listNodeValue(ln);
            unsigned long obuf_bytes = getClientOutputBufferMemoryUsage(slave);
            if (obuf_bytes > mem_used)
                mem_used = 0;
            else
                mem_used -= obuf_bytes;
        }
    }
    if (server.aof_state != REDIS_AOF_OFF) {
        mem_used -= sdslen(server.aof_buf);
        mem_used -= aofRewriteBufferSize();
    }

    /* Check if we are over the memory limit. */
    // ���Ŀǰʹ�õ��ڴ��С�����õ� maxmemory ҪС����ô����ִ�н�һ������
    if (mem_used <= server.maxmemory) return REDIS_OK;

    // ���ռ���ڴ�� maxmemory Ҫ�󣬵��� maxmemory ����Ϊ����̭����ôֱ�ӷ���
    if (server.maxmemory_policy == REDIS_MAXMEMORY_NO_EVICTION)
        return REDIS_ERR; /* We need to free memory, but policy forbids. */

    /* Compute how much memory we need to free. */
    // ������Ҫ�ͷŶ����ֽڵ��ڴ�
    mem_tofree = mem_used - server.maxmemory;

    // ��ʼ�����ͷ��ڴ���ֽ���Ϊ 0
    mem_freed = 0;

    // ���� maxmemory ���ԣ�
    // �����ֵ䣬�ͷ��ڴ沢��¼���ͷ��ڴ���ֽ���
    while (mem_freed < mem_tofree) {
        int j, k, keys_freed = 0;

        // ���������ֵ�
        for (j = 0; j < server.dbnum; j++) {
            long bestval = 0; /* just to prevent warning */
            sds bestkey = NULL;
            dictEntry *de;
            redisDb *db = server.db+j;
            dict *dict;

            //�����volatile��ͷ�Ĺ��ڲ��ԣ����expires�ֵ��л�ȡKV�� allkeys�����Ǵ�dict�ֵ��л�ȡ
            if (server.maxmemory_policy == REDIS_MAXMEMORY_ALLKEYS_LRU ||
                server.maxmemory_policy == REDIS_MAXMEMORY_ALLKEYS_RANDOM)
            {
                // ��������� allkeys-lru ���� allkeys-random 
                // ��ô��̭��Ŀ��Ϊ�������ݿ��
                dict = server.db[j].dict;
            } else {
                // ��������� volatile-lru �� volatile-random ���� volatile-ttl 
                // ��ô��̭��Ŀ��Ϊ������ʱ������ݿ��
                dict = server.db[j].expires;
            }

            // �������ֵ�
            if (dictSize(dict) == 0) continue;

            /* volatile-random and allkeys-random policy */
            // ���ʹ�õ���������ԣ���ô��Ŀ���ֵ������ѡ����
            if (server.maxmemory_policy == REDIS_MAXMEMORY_ALLKEYS_RANDOM ||
                server.maxmemory_policy == REDIS_MAXMEMORY_VOLATILE_RANDOM)
            {
                de = dictGetRandomKey(dict);
                bestkey = dictGetKey(de);
            }

            /* volatile-lru and allkeys-lru policy */
            // ���ʹ�õ��� LRU ���ԣ�
            // ��ô��һ�� sample ����ѡ�� IDLE ʱ������Ǹ���
            else if (server.maxmemory_policy == REDIS_MAXMEMORY_ALLKEYS_LRU ||
                server.maxmemory_policy == REDIS_MAXMEMORY_VOLATILE_LRU)
            {
                struct evictionPoolEntry *pool = db->eviction_pool;

                while(bestkey == NULL) {
                    //ѡ�������ʽ��������������LRU�㷨ѡ����Ҫ��̭������
                    evictionPoolPopulate(dict, db->dict, db->eviction_pool); //�����maxmemory_samples��KV��db->eviction_pool��
                    /* Go backward from best to worst element to evict. */
                    for (k = REDIS_EVICTION_POOL_SIZE-1; k >= 0; k--) {
                        if (pool[k].key == NULL) continue;
                        de = dictFind(dict,pool[k].key);

                        /* Remove the entry from the pool. */
                        sdsfree(pool[k].key);
                        /* Shift all elements on its right to left. */
                        memmove(pool+k,pool+k+1,
                            sizeof(pool[0])*(REDIS_EVICTION_POOL_SIZE-k-1));
                        /* Clear the element on the right which is empty
                         * since we shifted one position to the left.  */
                        pool[REDIS_EVICTION_POOL_SIZE-1].key = NULL;
                        pool[REDIS_EVICTION_POOL_SIZE-1].idle = 0;

                        /* If the key exists, is our pick. Otherwise it is
                         * a ghost and we need to try the next element. */
                        if (de) {
                            bestkey = dictGetKey(de);
                            break;
                        } else {
                            /* Ghost... */
                            continue;
                        }
                    }
                }
            }

            /* volatile-ttl */
            // ����Ϊ volatile-ttl ����һ�� sample ����ѡ������ʱ����뵱ǰʱ����ӽ��ļ�
            else if (server.maxmemory_policy == REDIS_MAXMEMORY_VOLATILE_TTL) {
                for (k = 0; k < server.maxmemory_samples; k++) { 
                    sds thiskey;
                    long thisval;

                    de = dictGetRandomKey(dict);
                    thiskey = dictGetKey(de);
                    thisval = (long) dictGetVal(de);

                    /* Expire sooner (minor expire unix timestamp) is better
                     * candidate for deletion */
                    if (bestkey == NULL || thisval < bestval) { //���ȡ����maxmemory_samples��KV��˭���ȹ��ھͽ�������
                        bestkey = thiskey;
                        bestval = thisval;
                    }
                }
            }

            /* Finally remove the selected key. */
            // ɾ����ѡ�еļ�
            if (bestkey) {
                long long delta;

                robj *keyobj = createStringObject(bestkey,sdslen(bestkey));
                propagateExpire(db,keyobj);
                /* We compute the amount of memory freed by dbDelete() alone.
                 * It is possible that actually the memory needed to propagate
                 * the DEL in AOF and replication link is greater than the one
                 * we are freeing removing the key, but we can't account for
                 * that otherwise we would never exit the loop.
                 *
                 * AOF and Output buffer memory will be freed eventually so
                 * we only care about memory used by the key space. */
                // ����ɾ�������ͷŵ��ڴ�����
                delta = (long long) zmalloc_used_memory();
                dbDelete(db,keyobj);
                delta -= (long long) zmalloc_used_memory();
                mem_freed += delta;
                
                // ����̭���ļ�������һ
                server.stat_evictedkeys++;

                notifyKeyspaceEvent(REDIS_NOTIFY_EVICTED, "evicted",
                    keyobj, db->id);
                decrRefCount(keyobj);
                keys_freed++;

                /* When the memory to free starts to be big enough, we may
                 * start spending so much time here that is impossible to
                 * deliver data to the slaves fast enough, so we force the
                 * transmission here inside the loop. */
                if (slaves) flushSlavesOutputBuffers();
            }
        }

        if (!keys_freed) return REDIS_ERR; /* nothing to free... */
    }

    return REDIS_OK;
}

/* =================================== Main! ================================ */

#ifdef __linux__
int linuxOvercommitMemoryValue(void) {
    FILE *fp = fopen("/proc/sys/vm/overcommit_memory","r");
    char buf[64];

    if (!fp) return -1;
    if (fgets(buf,64,fp) == NULL) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    return atoi(buf);
}

//ע��:���/proc/sys/vm/overcommit_memory������Ϊ0������������rdb���¹��ܣ�����ڴ治�㣬��frok��ʱ���ʧ�ܣ��������redis�������ݣ�
//��ʧ�ܣ���ӡ MISCONF Redis is configured to save RDB snapshots, but is currently not able to persist on disk
//���/proc/sys/vm/overcommit_memory������Ϊ1���򲻹��ڴ湻��������forkʧ�ܣ�����������OOM������redisʵ���ᱻɱ����

void linuxOvercommitMemoryWarning(void) {
    if (linuxOvercommitMemoryValue() == 0) {
        redisLog(REDIS_WARNING,"WARNING overcommit_memory is set to 0! Background save may fail under low memory condition. To fix this issue add 'vm.overcommit_memory = 1' to /etc/sysctl.conf and then reboot or run the command 'sysctl vm.overcommit_memory=1' for this to take effect.");
    }
}
#endif /* __linux__ */

void createPidFile(void) {
    /* Try to write the pid file in a best-effort way. */
    FILE *fp = fopen(server.pidfile,"w");
    if (fp) {
        fprintf(fp,"%d\n",(int)getpid());
        fclose(fp);
    }
}

void daemonize(void) {
    int fd;

    if (fork() != 0) exit(0); /* parent exits */
    setsid(); /* create a new session */

    /* Every output goes to /dev/null. If Redis is daemonized but
     * the 'logfile' is set to 'stdout' in the configuration file
     * it will not log at all. */
    if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
}

void version() {
    printf("Redis server v=%s sha=%s:%d malloc=%s bits=%d build=%llx\n",
        REDIS_VERSION,
        redisGitSHA1(),
        atoi(redisGitDirty()) > 0,
        ZMALLOC_LIB,
        sizeof(long) == 4 ? 32 : 64,
        (unsigned long long) redisBuildId());
    exit(0);
}

void usage() {
    fprintf(stderr,"Usage: ./redis-server [/path/to/redis.conf] [options]\n");
    fprintf(stderr,"       ./redis-server - (read config from stdin)\n");
    fprintf(stderr,"       ./redis-server -v or --version\n");
    fprintf(stderr,"       ./redis-server -h or --help\n");
    fprintf(stderr,"       ./redis-server --test-memory <megabytes>\n\n");
    fprintf(stderr,"Examples:\n");
    fprintf(stderr,"       ./redis-server (run the server with default conf)\n");
    fprintf(stderr,"       ./redis-server /etc/redis/6379.conf\n");
    fprintf(stderr,"       ./redis-server --port 7777\n");
    fprintf(stderr,"       ./redis-server --port 7777 --slaveof 127.0.0.1 8888\n");
    fprintf(stderr,"       ./redis-server /etc/myredis.conf --loglevel verbose\n\n");
    fprintf(stderr,"Sentinel mode:\n");
    fprintf(stderr,"       ./redis-server /etc/sentinel.conf --sentinel\n");
    exit(1);
}

void redisAsciiArt(void) {
#include "asciilogo.h"
    char *buf = zmalloc(1024*16);
    char *mode = "stand alone";

    if (server.cluster_enabled) mode = "cluster";
    else if (server.sentinel_mode) mode = "sentinel";

    snprintf(buf,1024*16,ascii_logo,
        REDIS_VERSION,
        redisGitSHA1(),
        strtol(redisGitDirty(),NULL,10) > 0,
        (sizeof(long) == 8) ? "64" : "32",
        mode, server.port,
        (long) getpid()
    );
    redisLogRaw(REDIS_NOTICE|REDIS_LOG_RAW,buf);
    zfree(buf);
}

// SIGTERM �źŵĴ�����
static void sigtermHandler(int sig) {
    REDIS_NOTUSED(sig);

    redisLogFromHandler(REDIS_WARNING,"Received SIGTERM, scheduling shutdown...");
    
    // �򿪹رձ�ʶ
    server.shutdown_asap = 1;
}

void setupSignalHandlers(void) {
    struct sigaction act;

    /* When the SA_SIGINFO flag is set in sa_flags then sa_sigaction is used.
     * Otherwise, sa_handler is used. */
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = sigtermHandler;
    sigaction(SIGTERM, &act, NULL);

#ifdef HAVE_BACKTRACE
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
    act.sa_sigaction = sigsegvHandler;
    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGBUS, &act, NULL);
    sigaction(SIGFPE, &act, NULL);
    sigaction(SIGILL, &act, NULL);
#endif
    return;
}

void memtest(size_t megabytes, int passes);

/* Returns 1 if there is --sentinel among the arguments or if
 * argv[0] is exactly "redis-sentinel". */
 /*
 ����һ��sentinel�������������ַ�ʽ������һ����
 redis-server /path/to/your/sentinel.conf -sentinel  
 redis-sentinel /path/to/your/sentinel.conf
*/
int checkForSentinelMode(int argc, char **argv) {
    int j;

    if (strstr(argv[0],"redis-sentinel") != NULL) return 1;
    for (j = 1; j < argc; j++)
        if (!strcmp(argv[j],"--sentinel")) return 1;
    return 0;
}

/*
��ΪAOF�ļ��ĸ���Ƶ��ͨ����RDB�ļ��ĸ���Ƶ�ʸߣ����ԣ�
���������������AOF�־û����ܣ���ô������������ʹ��AOF�ļ�����ԭ������״̬��
ֻ����AOF�־û����ܴ��ڹر�״̬ʱ���������Ż�ʹ��RDB�ļ�����ԭ���ݿ�״̬��
*/
/* Function called at startup to load RDB or AOF file in memory. */
//���Ƚ���AOF���룬ֻ��AOFû�����õ�����²Ż��RDB������
void loadDataFromDisk(void) { //loadDataFromDisk��rdbSave��Ӧ����д��
    // ��¼��ʼʱ��
    long long start = ustime();

//rdbLoad��ֱ�Ӷ�ȡrdb�ļ������е�key-value����redisDb����loadAppendOnlyFileͨ��α�ͻ�����ִ�У���Ϊ��Ҫһ������һ������Ļָ�ִ��

    // AOF �־û��Ѵ򿪣�
    if (server.aof_state == REDIS_AOF_ON) {
        // �������� AOF �ļ�
        if (loadAppendOnlyFile(server.aof_filename) == REDIS_OK)
            // ��ӡ������Ϣ�������������ʱ����
            redisLog(REDIS_NOTICE,"DB loaded from append only file: %.3f seconds",(float)(ustime()-start)/1000000);
    // AOF �־û�δ��
    } else {
        // �������� RDB �ļ�
        if (rdbLoad(server.rdb_filename) == REDIS_OK) {
            // ��ӡ������Ϣ�������������ʱ����
            redisLog(REDIS_NOTICE,"DB loaded from disk: %.3f seconds",
                (float)(ustime()-start)/1000000);
        } else if (errno != ENOENT) {
            redisLog(REDIS_WARNING,"Fatal error loading the DB: %s. Exiting.",strerror(errno));
            exit(1);
        }
    }
}

void redisOutOfMemoryHandler(size_t allocation_size) {
    redisLog(REDIS_WARNING,"Out Of Memory allocating %zu bytes!",
        allocation_size);
    redisPanic("Redis aborting for OUT OF MEMORY");
}

void redisSetProcTitle(char *title) {
#ifdef USE_SETPROCTITLE
    char *server_mode = "";
    if (server.cluster_enabled) server_mode = " [cluster]";
    else if (server.sentinel_mode) server_mode = " [sentinel]";

    setproctitle("%s %s:%d%s",
        title,
        server.bindaddr_count ? server.bindaddr[0] : "*",
        server.port,
        server_mode);
#else
    REDIS_NOTUSED(title);
#endif
}

//
int main(int argc, char **argv) {
    struct timeval tv;

    /* We need to initialize our libraries, and the server configuration. */
    // ��ʼ����
#ifdef INIT_SETPROCTITLE_REPLACEMENT
    spt_init(argc, argv);
#endif
    setlocale(LC_COLLATE,"");
    zmalloc_enable_thread_safeness();
    zmalloc_set_oom_handler(redisOutOfMemoryHandler);
    srand(time(NULL)^getpid());
    gettimeofday(&tv,NULL);
    dictSetHashFunctionSeed(tv.tv_sec^tv.tv_usec^getpid());

    // ���������Ƿ��� Sentinel ģʽ����
    server.sentinel_mode = checkForSentinelMode(argc,argv);

    // ��ʼ��������
    initServerConfig();

    /* We need to init sentinel right now as parsing the configuration file
     * in sentinel mode will have the effect of populating the sentinel
     * data structures with master nodes to monitor. */
    // ����������� Sentinel ģʽ��������ô���� Sentinel ������صĳ�ʼ��
    // ��ΪҪ���ӵ�������������һЩ��Ӧ�����ݽṹ
    if (server.sentinel_mode) {
        initSentinelConfig();
        initSentinel();
    }

    // ����û��Ƿ�ָ���������ļ�����������ѡ��
    if (argc >= 2) {
        int j = 1; /* First option to parse in argv[] */
        sds options = sdsempty();
        char *configfile = NULL;

        /* Handle special options --help and --version */
        // ��������ѡ�� -h ��-v �� --test-memory
        if (strcmp(argv[1], "-v") == 0 ||
            strcmp(argv[1], "--version") == 0) version();
        if (strcmp(argv[1], "--help") == 0 ||
            strcmp(argv[1], "-h") == 0) usage();
        if (strcmp(argv[1], "--test-memory") == 0) {
            if (argc == 3) {
                memtest(atoi(argv[2]),50);
                exit(0);
            } else {
                fprintf(stderr,"Please specify the amount of memory to test in megabytes.\n");
                fprintf(stderr,"Example: ./redis-server --test-memory 4096\n\n");
                exit(1);
            }
        }

        /* First argument is the config file name? */
        // �����һ��������argv[1]�������� "--" ��ͷ
        // ��ô��Ӧ����һ�������ļ�
        if (argv[j][0] != '-' || argv[j][1] != '-')
            configfile = argv[j++];

        /* All the other options are parsed and conceptually appended to the
         * configuration file. For instance --port 6380 will generate the
         * string "port 6380\n" to be parsed after the actual file name
         * is parsed, if any. */
        // ���û�����������ѡ����з����������������õ��ַ���׷���Ժ�����������ļ�������֮��
        // ���� --port 6380 �ᱻ����Ϊ "port 6380\n"
        while(j != argc) {
            if (argv[j][0] == '-' && argv[j][1] == '-') {
                /* Option name */
                if (sdslen(options)) options = sdscat(options,"\n");
                options = sdscat(options,argv[j]+2);
                options = sdscat(options," ");
            } else {
                /* Option argument */
                options = sdscatrepr(options,argv[j],strlen(argv[j]));
                options = sdscat(options," ");
            }
            j++;
        }
        if (configfile) server.configfile = getAbsolutePath(configfile);
        // ���ñ�������
        resetServerSaveParams();

        // ���������ļ��� options ��ǰ��������ĸ���ѡ��
        loadServerConfig(configfile,options);
        sdsfree(options);

        // ��ȡ�����ļ��ľ���·��
        if (configfile) server.configfile = getAbsolutePath(configfile);
    } else {
        redisLog(REDIS_WARNING, "Warning: no config file specified, using the default config. In order to specify a config file use %s /path/to/%s.conf", argv[0], server.sentinel_mode ? "sentinel" : "redis");
    }

    // ������������Ϊ�ػ�����
    if (server.daemonize) daemonize();

    // ��������ʼ�����������ݽṹ
    initServer();

    // ������������ػ����̣���ô���� PID �ļ�
    if (server.daemonize) createPidFile();

    // Ϊ������������������
    redisSetProcTitle(argv[0]);

    // ��ӡ ASCII LOGO
    redisAsciiArt();

    // ������������������� SENTINEL ģʽ����ôִ�����´���
    if (!server.sentinel_mode) { //sentinel�ͼ�Ⱥֻ�ܶ�ѡ1
        /* Things not needed when running in Sentinel mode. */
        // ��ӡ�ʺ���
        redisLog(REDIS_WARNING,"Server started, Redis version " REDIS_VERSION);
    #ifdef __linux__
        // ��ӡ�ڴ澯��
        linuxOvercommitMemoryWarning();
    #endif
        // �� AOF �ļ����� RDB �ļ�����������
        loadDataFromDisk();
        // ������Ⱥ��
        if (server.cluster_enabled) { //�ڸú���ǰ��������cluster����nodes.conf����initServer->clusterInit;
            if (verifyClusterConfigWithData() == REDIS_ERR) {
                redisLog(REDIS_WARNING,
                    "You can't have keys in a DB different than DB 0 when in "
                    "Cluster mode. Exiting.");
                exit(1);
            }
        }
        // ��ӡ TCP �˿�
        if (server.ipfd_count > 0)
            redisLog(REDIS_NOTICE,"The server is now ready to accept connections on port %d", server.port);
        // ��ӡ�����׽��ֶ˿�
        if (server.sofd > 0)
            redisLog(REDIS_NOTICE,"The server is now ready to accept connections at %s", server.unixsocket);
    } else { //sentinel�ͼ�Ⱥֻ�ܶ�ѡ1
        sentinelIsRunning();
    }

    /* Warning the user about suspicious maxmemory setting. */
    // ��鲻������ maxmemory ����
    if (server.maxmemory > 0 && server.maxmemory < 1024*1024) {
        redisLog(REDIS_WARNING,"WARNING: You specified a maxmemory value that is less than 1MB (current value is %llu bytes). Are you sure this is what you really want?", server.maxmemory);
    }

    // �����¼���������һֱ���������ر�Ϊֹ
    aeSetBeforeSleepProc(server.el,beforeSleep);
    aeMain(server.el); 

    // �������رգ�ֹͣ�¼�ѭ��
    aeDeleteEventLoop(server.el);

    return 0;
}

/* The End */
