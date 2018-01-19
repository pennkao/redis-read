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
#include <math.h> /* isnan(), isinf() */

/*-----------------------------------------------------------------------------
 * String Commands
 *----------------------------------------------------------------------------*/

/*
 * �������ַ������� len �Ƿ񳬹�����ֵ 512 MB
 *
 * �������� REDIS_ERR ��δ�������� REDIS_OK
 *
 * T = O(1)
 */
static int checkStringLength(redisClient *c, long long size) {

    if (size > 512*1024*1024) {
        addReplyError(c,"string exceeds maximum allowed size (512MB)");
        return REDIS_ERR;
    }

    return REDIS_OK;
}

/* The setGenericCommand() function implements the SET operation with different
 * options and variants. This function is called in order to implement the
 * following commands: SET, SETEX, PSETEX, SETNX.
 *
 * setGenericCommand() ����ʵ���� SET �� SETEX �� PSETEX �� SETNX ���
 *
 * 'flags' changes the behavior of the command (NX or XX, see belove).
 *
 * flags ������ֵ������ NX �� XX �����ǵ�����������ġ�
 *
 * 'expire' represents an expire to set in form of a Redis object as passed
 * by the user. It is interpreted according to the specified 'unit'.
 *
 * expire ������ Redis ����Ĺ���ʱ�䡣
 *
 * ���������ʱ��ĸ�ʽ�� unit ����ָ����
 *
 * 'ok_reply' and 'abort_reply' is what the function will reply to the client
 * if the operation is performed, or when it is not because of NX or
 * XX flags.
 *
 * ok_reply �� abort_reply ����������ظ������ݣ�
 * NX ������ XX ����Ҳ��ı�ظ���
 *
 * If ok_reply is NULL "+OK" is used.
 * If abort_reply is NULL, "$-1" is used. 
 *
 * ��� ok_reply Ϊ NULL ����ô "+OK" �����ء�
 * ��� abort_reply Ϊ NULL ����ô "$-1" �����ء�
 */

#define REDIS_SET_NO_FLAGS 0
#define REDIS_SET_NX (1<<0)     /* Set if key not exists. */
#define REDIS_SET_XX (1<<1)     /* Set if key exists. */

void setGenericCommand(redisClient *c, int flags, robj *key, robj *val, robj *expire, int unit, robj *ok_reply, robj *abort_reply) {

    long long milliseconds = 0; /* initialized to avoid any harmness warning */

    // ȡ������ʱ��
    if (expire) {

        // ȡ�� expire ������ֵ
        // T = O(N)
        if (getLongLongFromObjectOrReply(c, expire, &milliseconds, NULL) != REDIS_OK)
            return;

        // expire ������ֵ����ȷʱ����
        if (milliseconds <= 0) {
            addReplyError(c,"invalid expire time in SETEX");
            return;
        }

        // ��������Ĺ���ʱ�����뻹�Ǻ���
        // Redis ʵ�ʶ��Ժ������ʽ�������ʱ��
        // �������Ĺ���ʱ��Ϊ�룬��ô����ת��Ϊ����
        if (unit == UNIT_SECONDS) milliseconds *= 1000;
    }

    // ��������� NX ���� XX ��������ô��������Ƿ񲻷�������������
    // ������������ʱ��������������� abort_reply ��������
    if ((flags & REDIS_SET_NX && lookupKeyWrite(c->db,key) != NULL) ||
        (flags & REDIS_SET_XX && lookupKeyWrite(c->db,key) == NULL))
    {
        addReply(c, abort_reply ? abort_reply : shared.nullbulk);
        return;
    }

    // ����ֵ���������ݿ�
    setKey(c->db,key,val);

    // �����ݿ���Ϊ��
    server.dirty++;

    // Ϊ�����ù���ʱ��
    if (expire) setExpire(c->db,key,mstime()+milliseconds);

    // �����¼�֪ͨ
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"set",key,c->db->id);

    // �����¼�֪ͨ
    if (expire) notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,
        "expire",key,c->db->id);

    // ���óɹ�����ͻ��˷��ͻظ�
    // �ظ��������� ok_reply ����
    addReply(c, ok_reply ? ok_reply : shared.ok);
}

/*
SET?

SET key value [EX seconds] [PX milliseconds] [NX|XX]

���ַ���ֵ value ������ key ��

��� key �Ѿ���������ֵ�� SET �͸�д��ֵ���������͡�

����ĳ��ԭ����������ʱ�䣨TTL���ļ���˵�� �� SET ����ɹ����������ִ��ʱ�� �����ԭ�е� TTL ���������

��ѡ����

�� Redis 2.6.12 �汾��ʼ�� SET �������Ϊ����ͨ��һϵ�в������޸ģ�
?EX second �����ü��Ĺ���ʱ��Ϊ second �롣 SET key value EX second Ч����ͬ�� SETEX key second value ��
?PX millisecond �����ü��Ĺ���ʱ��Ϊ millisecond ���롣 SET key value PX millisecond Ч����ͬ�� PSETEX key millisecond value ��
?NX ��ֻ�ڼ�������ʱ���ŶԼ��������ò����� SET key value NX Ч����ͬ�� SETNX key value ��
?XX ��ֻ�ڼ��Ѿ�����ʱ���ŶԼ��������ò�����



��Ϊ SET �������ͨ��������ʵ�ֺ� SETNX �� SETEX �� PSETEX ���������Ч�������Խ����� Redis �汾���ܻ�����������Ƴ� SETNX �� SETEX �� PSETEX ���������
 ���ð汾��>= 1.0.0ʱ�临�Ӷȣ�O(1)����ֵ��
�� Redis 2.6.12 �汾��ǰ�� SET �������Ƿ��� OK ��


�� Redis 2.6.12 �汾��ʼ�� SET �����ò����ɹ����ʱ���ŷ��� OK ��

��������� NX ���� XX ������Ϊ����û�ﵽ��������ò���δִ�У���ô����ؿ������ظ���NULL Bulk Reply����


# �Բ����ڵļ���������

redis 127.0.0.1:6379> SET key "value"
OK

redis 127.0.0.1:6379> GET key
"value"


# ���Ѵ��ڵļ���������

redis 127.0.0.1:6379> SET key "new-value"
OK

redis 127.0.0.1:6379> GET key
"new-value"


# ʹ�� EX ѡ��

redis 127.0.0.1:6379> SET key-with-expire-time "hello" EX 10086
OK

redis 127.0.0.1:6379> GET key-with-expire-time
"hello"

redis 127.0.0.1:6379> TTL key-with-expire-time
(integer) 10069


# ʹ�� PX ѡ��

redis 127.0.0.1:6379> SET key-with-pexpire-time "moto" PX 123321
OK

redis 127.0.0.1:6379> GET key-with-pexpire-time
"moto"

redis 127.0.0.1:6379> PTTL key-with-pexpire-time
(integer) 111939


# ʹ�� NX ѡ��

redis 127.0.0.1:6379> SET not-exists-key "value" NX
OK      # �������ڣ����óɹ�

redis 127.0.0.1:6379> GET not-exists-key
"value"

redis 127.0.0.1:6379> SET not-exists-key "new-value" NX
(nil)   # ���Ѿ����ڣ�����ʧ��

redis 127.0.0.1:6379> GEt not-exists-key
"value" # ά��ԭֵ����


# ʹ�� XX ѡ��

redis 127.0.0.1:6379> EXISTS exists-key
(integer) 0

redis 127.0.0.1:6379> SET exists-key "value" XX
(nil)   # ��Ϊ�������ڣ�����ʧ��

redis 127.0.0.1:6379> SET exists-key "value"
OK      # �ȸ�������һ��ֵ

redis 127.0.0.1:6379> SET exists-key "new-value" XX
OK      # ������ֵ�ɹ�

redis 127.0.0.1:6379> GET exists-key
"new-value"


# NX �� XX ���Ժ� EX ���� PX ���ʹ��

redis 127.0.0.1:6379> SET key-with-expire-and-NX "hello" EX 10086 NX
OK

redis 127.0.0.1:6379> GET key-with-expire-and-NX
"hello"

redis 127.0.0.1:6379> TTL key-with-expire-and-NX
(integer) 10063

redis 127.0.0.1:6379> SET key-with-pexpire-and-XX "old value"
OK

redis 127.0.0.1:6379> SET key-with-pexpire-and-XX "new value" PX 123321
OK

redis 127.0.0.1:6379> GET key-with-pexpire-and-XX
"new value"

redis 127.0.0.1:6379> PTTL key-with-pexpire-and-XX
(integer) 112999


# EX �� PX ����ͬʱ���֣������������ѡ��Ḳ��ǰ�������ѡ��

redis 127.0.0.1:6379> SET key "value" EX 1000 PX 5000000
OK

redis 127.0.0.1:6379> TTL key
(integer) 4993  # ���� PX �������õ�ֵ

redis 127.0.0.1:6379> SET another-key "value" PX 5000000 EX 1000
OK

redis 127.0.0.1:6379> TTL another-key
(integer) 997   # ���� EX �������õ�ֵ



ʹ��ģʽ

���� SET resource-name anystring NX EX max-lock-time ��һ���� Redis ��ʵ�����ļ򵥷�����

�ͻ���ִ�����ϵ����
?������������� OK ����ô����ͻ��˻������
?������������� NIL ����ô�ͻ��˻�ȡ��ʧ�ܣ��������Ժ������ԡ�

���õĹ���ʱ�䵽��֮�������Զ��ͷš�

����ͨ�������޸ģ��������ʵ�ָ���׳��
?��ʹ�ù̶����ַ�����Ϊ����ֵ����������һ�����ɲ²⣨non-guessable���ĳ�����ַ�������Ϊ�����token����
?��ʹ�� DEL �������ͷ��������Ƿ���һ�� Lua �ű�������ű�ֻ�ڿͻ��˴����ֵ�ͼ��Ŀ����ƥ��ʱ���ŶԼ�����ɾ����

�������Ķ����Է�ֹ���й������Ŀͻ�����ɾ��������������֡�

������һ���򵥵Ľ����ű�ʾ����


if redis.call("get",KEYS[1]) == ARGV[1]
then
    return redis.call("del",KEYS[1])
else
    return 0
end


����ű�����ͨ�� EVAL ...script... 1 resource-name token-value ���������á�

*/
/* SET key value [NX] [XX] [EX <seconds>] [PX <milliseconds>] */
void setCommand(redisClient *c) {
    int j;
    robj *expire = NULL;
    int unit = UNIT_SECONDS;
    int flags = REDIS_SET_NO_FLAGS;

    // ����ѡ�����
    for (j = 3; j < c->argc; j++) {
        char *a = c->argv[j]->ptr;
        robj *next = (j == c->argc-1) ? NULL : c->argv[j+1];

        if ((a[0] == 'n' || a[0] == 'N') &&
            (a[1] == 'x' || a[1] == 'X') && a[2] == '\0') {
            flags |= REDIS_SET_NX;
        } else if ((a[0] == 'x' || a[0] == 'X') &&
                   (a[1] == 'x' || a[1] == 'X') && a[2] == '\0') {
            flags |= REDIS_SET_XX;
        } else if ((a[0] == 'e' || a[0] == 'E') &&
                   (a[1] == 'x' || a[1] == 'X') && a[2] == '\0' && next) {
            unit = UNIT_SECONDS;
            expire = next;
            j++;
        } else if ((a[0] == 'p' || a[0] == 'P') &&
                   (a[1] == 'x' || a[1] == 'X') && a[2] == '\0' && next) {
            unit = UNIT_MILLISECONDS;
            expire = next;
            j++;
        } else {
            addReply(c,shared.syntaxerr);
            return;
        }
    }

    // ���Զ�ֵ������б���
    c->argv[2] = tryObjectEncoding(c->argv[2]);

    setGenericCommand(c,flags,c->argv[1],c->argv[2],expire,unit,NULL,NULL);
}

void setnxCommand(redisClient *c) {
    c->argv[2] = tryObjectEncoding(c->argv[2]);
    setGenericCommand(c,REDIS_SET_NX,c->argv[1],c->argv[2],NULL,0,shared.cone,shared.czero);
}

void setexCommand(redisClient *c) {
    c->argv[3] = tryObjectEncoding(c->argv[3]);
    setGenericCommand(c,REDIS_SET_NO_FLAGS,c->argv[1],c->argv[3],c->argv[2],UNIT_SECONDS,NULL,NULL);
}

void psetexCommand(redisClient *c) {
    c->argv[3] = tryObjectEncoding(c->argv[3]);
    setGenericCommand(c,REDIS_SET_NO_FLAGS,c->argv[1],c->argv[3],c->argv[2],UNIT_MILLISECONDS,NULL,NULL);
}

int getGenericCommand(redisClient *c) {
    robj *o;

    // ���Դ����ݿ���ȡ���� c->argv[1] ��Ӧ��ֵ����
    // �����������ʱ����ͻ��˷��ͻظ���Ϣ�������� NULL
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL)
        return REDIS_OK;

    // ֵ������ڣ������������
    if (o->type != REDIS_STRING) {
        // ���ʹ���
        addReply(c,shared.wrongtypeerr);
        return REDIS_ERR;
    } else {
        // ������ȷ����ͻ��˷��ض����ֵ
        addReplyBulk(c,o);
        return REDIS_OK;
    }
}

void getCommand(redisClient *c) {
    getGenericCommand(c);
}

void getsetCommand(redisClient *c) {

    // ȡ�������ؼ���ֵ����
    if (getGenericCommand(c) == REDIS_ERR) return;

    // ���������ֵ c->argv[2]
    c->argv[2] = tryObjectEncoding(c->argv[2]);

    // �����ݿ��й����� c->argv[1] ����ֵ���� c->argv[2]
    setKey(c->db,c->argv[1],c->argv[2]);

    // �����¼�֪ͨ
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"set",c->argv[1],c->db->id);

    // ����������Ϊ��
    server.dirty++;
}

void setrangeCommand(redisClient *c) {
    robj *o;
    long offset;

    sds value = c->argv[3]->ptr;

    // ȡ�� offset ����
    if (getLongFromObjectOrReply(c,c->argv[2],&offset,NULL) != REDIS_OK)
        return;

    // ��� offset ����
    if (offset < 0) {
        addReplyError(c,"offset is out of range");
        return;
    }

    // ȡ�������ڵ�ֵ����
    o = lookupKeyWrite(c->db,c->argv[1]);
    if (o == NULL) {

        // �������������ݿ��С�����

        /* Return 0 when setting nothing on a non-existing string */
        // value Ϊ�գ�û��ʲô�����õģ���ͻ��˷��� 0
        if (sdslen(value) == 0) {
            addReply(c,shared.czero);
            return;
        }

        /* Return when the resulting string exceeds allowed size */
        // ������ú�ĳ��Ȼᳬ�� Redis �����ƵĻ�
        // ��ô�������ã���ͻ��˷���һ������ظ�
        if (checkStringLength(c,offset+sdslen(value)) != REDIS_OK)
            return;

        // ��� value û�����⣬�������ã���ô����һ�����ַ���ֵ����
        // �������ݿ��й����� c->argv[1] ��������ַ�������
        o = createObject(REDIS_STRING,sdsempty());
        dbAdd(c->db,c->argv[1],o);
    } else {
        size_t olen;

        // ֵ������ڡ�����

        /* Key exists, check type */
        // ���ֵ���������
        if (checkType(c,o,REDIS_STRING))
            return;

        /* Return existing string length when setting nothing */
        // ȡ��ԭ���ַ����ĳ���
        olen = stringObjectLen(o);

        // value Ϊ�գ�û��ʲô�����õģ���ͻ��˷��� 0
        if (sdslen(value) == 0) {
            addReplyLongLong(c,olen);
            return;
        }

        /* Return when the resulting string exceeds allowed size */
        // ������ú�ĳ��Ȼᳬ�� Redis �����ƵĻ�
        // ��ô�������ã���ͻ��˷���һ������ظ�
        if (checkStringLength(c,offset+sdslen(value)) != REDIS_OK)
            return;

        /* Create a copy when the object is shared or encoded. */
        o = dbUnshareStringValue(c->db,c->argv[1],o);
    }

    // ����� sdslen(value) > 0 ��ʵ����ȥ��
    // ǰ���Ѿ����˼����
    if (sdslen(value) > 0) {
        // ��չ�ַ���ֵ����
        o->ptr = sdsgrowzero(o->ptr,offset+sdslen(value));
        // �� value ���Ƶ��ַ����е�ָ����λ��
        memcpy((char*)o->ptr+offset,value,sdslen(value));

        // �����ݿⷢ�ͼ����޸ĵ��ź�
        signalModifiedKey(c->db,c->argv[1]);

        // �����¼�֪ͨ
        notifyKeyspaceEvent(REDIS_NOTIFY_STRING,
            "setrange",c->argv[1],c->db->id);

        // ����������Ϊ��
        server.dirty++;
    }

    // ���óɹ��������µ��ַ���ֵ���ͻ���
    addReplyLongLong(c,sdslen(o->ptr));
}

void getrangeCommand(redisClient *c) {
    robj *o;
    long start, end;
    char *str, llbuf[32];
    size_t strlen;

    // ȡ�� start ����
    if (getLongFromObjectOrReply(c,c->argv[2],&start,NULL) != REDIS_OK)
        return;

    // ȡ�� end ����
    if (getLongFromObjectOrReply(c,c->argv[3],&end,NULL) != REDIS_OK)
        return;

    // �����ݿ��в��Ҽ� c->argv[1] 
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptybulk)) == NULL ||
        checkType(c,o,REDIS_STRING)) return;

    // ���ݱ��룬�Զ����ֵ���д���
    if (o->encoding == REDIS_ENCODING_INT) {
        str = llbuf;
        strlen = ll2string(llbuf,sizeof(llbuf),(long)o->ptr);
    } else {
        str = o->ptr;
        strlen = sdslen(str);
    }

    /* Convert negative indexes */
    // ����������ת��Ϊ��������
    if (start < 0) start = strlen+start;
    if (end < 0) end = strlen+end;
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if ((unsigned)end >= strlen) end = strlen-1;

    /* Precondition: end >= 0 && end < strlen, so the only condition where
     * nothing can be returned is: start > end. */
    if (start > end) {
        // ����������ΧΪ�յ����
        addReply(c,shared.emptybulk);
    } else {
        // ��ͻ��˷��ظ�����Χ�ڵ��ַ�������
        addReplyBulkCBuffer(c,(char*)str+start,end-start+1);
    }
}

void mgetCommand(redisClient *c) {
    int j;

    addReplyMultiBulkLen(c,c->argc-1);
    // ���Ҳ����������������ֵ
    for (j = 1; j < c->argc; j++) {
        // ���Ҽ� c->argc[j] ��ֵ
        robj *o = lookupKeyRead(c->db,c->argv[j]);
        if (o == NULL) {
            // ֵ�����ڣ���ͻ��˷��Ϳջظ�
            addReply(c,shared.nullbulk);
        } else {
            if (o->type != REDIS_STRING) {
                // ֵ���ڣ��������ַ�������
                addReply(c,shared.nullbulk);
            } else {
                // ֵ���ڣ��������ַ���
                addReplyBulk(c,o);
            }
        }
    }
}

void msetGenericCommand(redisClient *c, int nx) {
    int j, busykeys = 0;

    // ��ֵ�������ǳ���ɶԳ��ֵģ���ʽ����ȷ
    if ((c->argc % 2) == 0) {
        addReplyError(c,"wrong number of arguments for MSET");
        return;
    }
    /* Handle the NX flag. The MSETNX semantic is to return zero and don't
     * set nothing at all if at least one already key exists. */
    // ��� nx ����Ϊ�棬��ô�����������������ݿ����Ƿ����
    // ֻҪ��һ�����Ǵ��ڵģ���ô����ͻ��˷��Ϳջظ�
    // ������ִ�н����������ò���
    if (nx) {
        for (j = 1; j < c->argc; j += 2) {
            if (lookupKeyWrite(c->db,c->argv[j]) != NULL) {
                busykeys++;
            }
        }
        // ������
        // ���Ϳհ׻ظ���������ִ�н����������ò���
        if (busykeys) {
            addReply(c, shared.czero);
            return;
        }
    }

    // �������м�ֵ��
    for (j = 1; j < c->argc; j += 2) {

        // ��ֵ������н���
        c->argv[j+1] = tryObjectEncoding(c->argv[j+1]);

        // ����ֵ�Թ��������ݿ�
        // c->argc[j] Ϊ��
        // c->argc[j+1] Ϊֵ
        setKey(c->db,c->argv[j],c->argv[j+1]);

        // �����¼�֪ͨ
        notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"set",c->argv[j],c->db->id);
    }

    // ����������Ϊ��
    server.dirty += (c->argc-1)/2;

    // ���óɹ�
    // MSET ���� OK ���� MSETNX ���� 1
    addReply(c, nx ? shared.cone : shared.ok);
}

void msetCommand(redisClient *c) {
    msetGenericCommand(c,0);
}

void msetnxCommand(redisClient *c) {
    msetGenericCommand(c,1);
}

void incrDecrCommand(redisClient *c, long long incr) {
    long long value, oldvalue;
    robj *o, *new;

    // ȡ��ֵ����
    o = lookupKeyWrite(c->db,c->argv[1]);

    // �������Ƿ���ڣ��Լ������Ƿ���ȷ
    if (o != NULL && checkType(c,o,REDIS_STRING)) return;

    // ȡ�����������ֵ�������浽 value ������
    if (getLongLongFromObjectOrReply(c,o,&value,NULL) != REDIS_OK) return;

    // ���ӷ�����ִ��֮��ֵ�ͷŻ����
    // ����ǵĻ�������ͻ��˷���һ������ظ������������ò���
    oldvalue = value;
    if ((incr < 0 && oldvalue < 0 && incr < (LLONG_MIN-oldvalue)) ||
        (incr > 0 && oldvalue > 0 && incr > (LLONG_MAX-oldvalue))) {
        addReplyError(c,"increment or decrement would overflow");
        return;
    }

    // ���мӷ����㣬����ֵ���浽�µ�ֵ������
    // Ȼ�����µ�ֵ�����滻ԭ����ֵ����
    value += incr;
    new = createStringObjectFromLongLong(value);
    if (o)
        dbOverwrite(c->db,c->argv[1],new);
    else
        dbAdd(c->db,c->argv[1],new);

    // �����ݿⷢ�ͼ����޸ĵ��ź�
    signalModifiedKey(c->db,c->argv[1]);

    // �����¼�֪ͨ
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"incrby",c->argv[1],c->db->id);

    // ����������Ϊ��
    server.dirty++;

    // ���ػظ�
    addReply(c,shared.colon);
    addReply(c,new);
    addReply(c,shared.crlf);
}

void incrCommand(redisClient *c) {
    incrDecrCommand(c,1);
}

void decrCommand(redisClient *c) {
    incrDecrCommand(c,-1);
}

void incrbyCommand(redisClient *c) {
    long long incr;

    if (getLongLongFromObjectOrReply(c, c->argv[2], &incr, NULL) != REDIS_OK) return;
    incrDecrCommand(c,incr);
}

void decrbyCommand(redisClient *c) {
    long long incr;

    if (getLongLongFromObjectOrReply(c, c->argv[2], &incr, NULL) != REDIS_OK) return;
    incrDecrCommand(c,-incr);
}

void incrbyfloatCommand(redisClient *c) {
    long double incr, value;
    robj *o, *new, *aux;

    // ȡ��ֵ����
    o = lookupKeyWrite(c->db,c->argv[1]);

    // �������Ƿ���ڣ��Լ������Ƿ���ȷ
    if (o != NULL && checkType(c,o,REDIS_STRING)) return;

    // �����������ֵ���浽 value ������
    // ��ȡ�� incr ������ֵ
    if (getLongDoubleFromObjectOrReply(c,o,&value,NULL) != REDIS_OK ||
        getLongDoubleFromObjectOrReply(c,c->argv[2],&incr,NULL) != REDIS_OK)
        return;

    // ���мӷ����㣬������Ƿ����
    value += incr;
    if (isnan(value) || isinf(value)) {
        addReplyError(c,"increment would produce NaN or Infinity");
        return;
    }

    // ��һ��������ֵ���¶����滻���е�ֵ����
    new = createStringObjectFromLongDouble(value);
    if (o)
        dbOverwrite(c->db,c->argv[1],new);
    else
        dbAdd(c->db,c->argv[1],new);

    // �����ݿⷢ�ͼ����޸ĵ��ź�
    signalModifiedKey(c->db,c->argv[1]);

    // �����¼�֪ͨ
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"incrbyfloat",c->argv[1],c->db->id);

    // ����������Ϊ��
    server.dirty++;

    // �ظ�
    addReplyBulk(c,new);

    /* Always replicate INCRBYFLOAT as a SET command with the final value
     * in order to make sure that differences in float precision or formatting
     * will not create differences in replicas or after an AOF restart. */
    // �ڴ��� INCRBYFLOAT ����ʱ�������� SET �������滻 INCRBYFLOAT ����
    // �Ӷ���ֹ��Ϊ��ͬ�ĸ��㾫�Ⱥ͸�ʽ����� AOF ����ʱ�����ݲ�һ��
    aux = createStringObject("SET",3);
    rewriteClientCommandArgument(c,0,aux);
    decrRefCount(aux);
    rewriteClientCommandArgument(c,2,new);
}

void appendCommand(redisClient *c) {
    size_t totlen;
    robj *o, *append;

    // ȡ������Ӧ��ֵ����
    o = lookupKeyWrite(c->db,c->argv[1]);
    if (o == NULL) {

        // ��ֵ�Բ����ڡ�����

        /* Create the key */
        // ��ֵ�Բ����ڣ�����һ���µ�
        c->argv[2] = tryObjectEncoding(c->argv[2]);
        dbAdd(c->db,c->argv[1],c->argv[2]);
        incrRefCount(c->argv[2]);
        totlen = stringObjectLen(c->argv[2]);
    } else {

        // ��ֵ�Դ��ڡ�����

        /* Key exists, check type */
        // �������
        if (checkType(c,o,REDIS_STRING))
            return;

        /* "append" is an argument, so always an sds */
        // ���׷�Ӳ���֮���ַ����ĳ����Ƿ���� Redis ������
        append = c->argv[2];
        totlen = stringObjectLen(o)+sdslen(append->ptr);
        if (checkStringLength(c,totlen) != REDIS_OK)
            return;

        /* Append the value */
        // ִ��׷�Ӳ���
        o = dbUnshareStringValue(c->db,c->argv[1],o);
        o->ptr = sdscatlen(o->ptr,append->ptr,sdslen(append->ptr));
        totlen = sdslen(o->ptr);
    }

    // �����ݿⷢ�ͼ����޸ĵ��ź�
    signalModifiedKey(c->db,c->argv[1]);

    // �����¼�֪ͨ
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"append",c->argv[1],c->db->id);

    // ����������Ϊ��
    server.dirty++;

    // ���ͻظ�
    addReplyLongLong(c,totlen);
}

void strlenCommand(redisClient *c) {
    robj *o;

    // ȡ��ֵ���󣬲��������ͼ��
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,REDIS_STRING)) return;

    // �����ַ���ֵ�ĳ���
    addReplyLongLong(c,stringObjectLen(o));
}
