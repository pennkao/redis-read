/* rio.c is a simple stream-oriented I/O abstraction that provides an interface
 * to write code that can consume/produce data using different concrete input
 * and output devices. 
 *
 * RIO ��һ�������������������ڶԶ��ֲ�ͬ������
 * ��Ŀǰ���ļ����ڴ��ֽڣ����б�̵ĳ���
 *
 * For instance the same rdb.c code using the rio
 * abstraction can be used to read and write the RDB format using in-memory
 * buffers or files.
 *
 * ����˵��RIO ����ͬʱ���ڴ���ļ��е� RDB ��ʽ���ж�д��
 *
 * A rio object provides the following methods:
 *
 * һ�� RIO �����ṩ���·�����
 *
 *  read: read from stream.
 *        �����ж�ȡ
 *
 *  write: write to stream.
 *         д�뵽����
 *
 *  tell: get the current offset.
 *        ��ȡ��ǰ��ƫ����
 *
 * It is also possible to set a 'checksum' method that is used by rio.c in order
 * to compute a checksum of the data written or read, or to query the rio object
 * for the current checksum.
 *
 * ������ͨ������ checksum ����������д����ȡ���ݵ�У��ͣ�
 * ����Ϊ��ǰ��У��Ͳ�ѯ rio ����
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
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


#include "fmacros.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "rio.h"
#include "util.h"
#include "crc64.h"
#include "config.h"
#include "redis.h"

/* Returns 1 or 0 for success/failure. 
 *
 * ���������� buf ׷�ӵ������У�����Ϊ len ��
 *
 * �ɹ����� 1 ��ʧ�ܷ��� 0 ��
 */
static size_t rioBufferWrite(rio *r, const void *buf, size_t len) {

    // ׷��
    r->io.buffer.ptr = sdscatlen(r->io.buffer.ptr,(char*)buf,len);

    // ����ƫ����
    r->io.buffer.pos += len;

    return 1;
}

/* Returns 1 or 0 for success/failure. 
 *
 * �� r �ж�ȡ����Ϊ len �����ݵ� buf �С�
 *
 * ��ȡ�ɹ����� 1 �����򷵻� 0 ��
 */
static size_t rioBufferRead(rio *r, void *buf, size_t len) {

    // r �е����ݵĳ��Ȳ��� len 
    if (sdslen(r->io.buffer.ptr)-r->io.buffer.pos < len)
        return 0; /* not enough buffer to return len bytes. */
    
    // ���� r �е����ݵ� buf
    memcpy(buf,r->io.buffer.ptr+r->io.buffer.pos,len);
    r->io.buffer.pos += len;

    return 1;
}

/* Returns read/write position in buffer. 
 *
 * ���ػ���ĵ�ǰƫ����
 */
static off_t rioBufferTell(rio *r) {
    return r->io.buffer.pos;
}

/* Returns 1 or 0 for success/failure. 
 *
 * ������Ϊ len ������ buf д�뵽�ļ� r �С�
 *
 * �ɹ����� 1 ��ʧ�ܷ��� 0 ��
 */
static size_t rioFileWrite(rio *r, const void *buf, size_t len) {
    size_t retval;

    retval = fwrite(buf,len,1,r->io.file.fp);
    r->io.file.buffered += len;

    // ���д����ֽ��������Ƿ���Ҫִ���Զ� sync
    if (r->io.file.autosync &&
        r->io.file.buffered >= r->io.file.autosync)
    {
        fflush(r->io.file.fp);
        aof_fsync(fileno(r->io.file.fp));
        r->io.file.buffered = 0;
    }

    return retval;
}

/* Returns 1 or 0 for success/failure. */
/*
 * ���ļ� r �ж�ȡ len �ֽڵ� buf �С�
 *
 * ����ֵΪ��ȡ���ֽ�����
 */
static size_t rioFileRead(rio *r, void *buf, size_t len) {
    return fread(buf,len,1,r->io.file.fp);
}

/* Returns read/write position in file. 
 *
 * �����ļ���ǰ��ƫ����
 */
static off_t rioFileTell(rio *r) {
    return ftello(r->io.file.fp);
}

/*
 * ��Ϊ�ڴ�ʱ��ʹ�õĽṹ
 */ //rdb aof�ļ�����д���ʼ��ΪrioFileIO      DUMP, RESTORE and MIGRATE��ʱ����rioBufferIO
static const rio rioBufferIO = {
    // ������
    rioBufferRead,
    // д����
    rioBufferWrite,
    // ƫ��������
    rioBufferTell,
    NULL,           /* update_checksum */
    0,              /* current checksum */
    0,              /* bytes read or written */
    0,              /* read/write chunk size */
    { { NULL, 0 } } /* union for io-specific vars */
};

/*
 * ��Ϊ�ļ�ʱ��ʹ�õĽṹ
 */ //rdb aof�ļ�����д���ʼ��ΪrioFileIO      DUMP, RESTORE and MIGRATE��ʱ����rioBufferIO
static const rio rioFileIO = {
    // ������
    rioFileRead,
    // д����
    rioFileWrite,
    // ƫ��������
    rioFileTell,
    NULL,           /* update_checksum */
    0,              /* current checksum */
    0,              /* bytes read or written */
    0,              /* read/write chunk size */
    { { NULL, 0 } } /* union for io-specific vars */
};

/*
 * ��ʼ���ļ���
 */
void rioInitWithFile(rio *r, FILE *fp) {
    *r = rioFileIO;
    r->io.file.fp = fp;
    r->io.file.buffered = 0;
    r->io.file.autosync = 0;
}

/*
 * ��ʼ���ڴ���
 */
void rioInitWithBuffer(rio *r, sds s) {
    *r = rioBufferIO;
    r->io.buffer.ptr = s; //rioInitWithBuffer(&cmd,sdsempty());
    r->io.buffer.pos = 0;
}

/* This function can be installed both in memory and file streams when checksum
 * computation is needed. */
/*
 * ͨ��У��ͼ��㺯��
 */
void rioGenericUpdateChecksum(rio *r, const void *buf, size_t len) {
    r->cksum = crc64(r->cksum,buf,len);
}

/* Set the file-based rio object to auto-fsync every 'bytes' file written.
 *
 * ÿ��ͨ�� rio д�� bytes ָ�����ֽ�����ʱ��ִ��һ���Զ��� fsync ��
 *
 * By default this is set to zero that means no automatic file sync is
 * performed.
 *
 * Ĭ������£� bytes ����Ϊ 0 ����ʾ��ִ���Զ� fsync �� 
 *
 * This feature is useful in a few contexts since when we rely on OS write
 * buffers sometimes the OS buffers way too much, resulting in too many
 * disk I/O concentrated in very little time. When we fsync in an explicit
 * way instead the I/O pressure is more distributed across time. 
 *
 * ���������Ϊ�˷�ֹһ��д��������ݶ����õġ�
 *
 * ͨ����ʾ�ء�����Եص��� fsync ��
 * ���Խ�д��� I/O ѹ���ֵ������ fsync �����С�
 */
void rioSetAutoSync(rio *r, off_t bytes) {
    redisAssert(r->read == rioFileIO.read);
    r->io.file.autosync = bytes;
}

/* ------------------------------ Higher level interface ---------------------------
 * The following higher level functions use lower level rio.c functions to help
 * generating the Redis protocol for the Append Only File.
 *
 * ���¸߲㺯��ͨ������ǰ��ĵײ㺯�������� AOF �ļ������Э��
 */

/* Write multi bulk count in the format: "*<count>\r\n". */
/*
 * �Դ� '\r\n' ��׺����ʽд���ַ�����ʾ�� count �� RIO 
 *
 * �ɹ�����д���������ʧ�ܷ��� 0 ��
 */
size_t rioWriteBulkCount(rio *r, char prefix, int count) {
    char cbuf[128];
    int clen;

    // cbuf = prefix ++ count ++ '\r\n'
    // ���磺 *123\r\n
    cbuf[0] = prefix;
    clen = 1+ll2string(cbuf+1,sizeof(cbuf)-1,count);
    cbuf[clen++] = '\r';
    cbuf[clen++] = '\n';

    // д��
    if (rioWrite(r,cbuf,clen) == 0) return 0;

    // ����д���ֽ���
    return clen;
}

/* Write binary-safe string in the format: "$<count>\r\n<payload>\r\n". 
 *
 * �� "$<count>\r\n<payload>\r\n" ����ʽд������ư�ȫ�ַ�
 *
 * ���� $3\r\nSET\r\n
 */
size_t rioWriteBulkString(rio *r, const char *buf, size_t len) {
    size_t nwritten;

    // д�� $<count>\r\n
    if ((nwritten = rioWriteBulkCount(r,'$',len)) == 0) return 0;

    // д�� <payload>
    if (len > 0 && rioWrite(r,buf,len) == 0) return 0;

    // д�� \r\n
    if (rioWrite(r,"\r\n",2) == 0) return 0;

    // ����д������
    return nwritten+len+2;
}

/* Write a long long value in format: "$<count>\r\n<payload>\r\n". 
 *
 * �� "$<count>\r\n<payload>\r\n" �ĸ�ʽд�� long long ֵ
 */
size_t rioWriteBulkLongLong(rio *r, long long l) {
    char lbuf[32];
    unsigned int llen;

    // ȡ�� long long ֵ���ַ�����ʽ
    // ��������ַ����ĳ���
    llen = ll2string(lbuf,sizeof(lbuf),l);

    // д�� $llen\r\nlbuf\r\n
    return rioWriteBulkString(r,lbuf,llen);
}

/* Write a double value in the format: "$<count>\r\n<payload>\r\n" 
 *
 * �� "$<count>\r\n<payload>\r\n" �ĸ�ʽд�� double ֵ
 */
size_t rioWriteBulkDouble(rio *r, double d) {
    char dbuf[128];
    unsigned int dlen;

    // ȡ�� double ֵ���ַ�����ʾ��С�����ֻ���� 17 λ��
    // �������ַ����ĳ���
    dlen = snprintf(dbuf,sizeof(dbuf),"%.17g",d);

    // д�� $dlen\r\ndbuf\r\n
    return rioWriteBulkString(r,dbuf,dlen);
}
