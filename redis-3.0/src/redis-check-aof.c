/*
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "config.h"

#define ERROR(...) { \
    char __buf[1024]; \
    sprintf(__buf, __VA_ARGS__); \
    sprintf(error, "0x%16llx: %s", (long long)epos, __buf); \
}

// ���������Ϣ
static char error[1024];

// �ļ���ȡ�ĵ�ǰƫ����
static off_t epos;

/*
 * ȷ�� buf ���� \r\n ��β�����С�
 *
 * ȷ�ϳɹ����� 1 ������ʧ�ܷ��� 0 ������ӡ������Ϣ��
 */
int consumeNewline(char *buf) {
    if (strncmp(buf,"\r\n",2) != 0) {
        ERROR("Expected \\r\\n, got: %02x%02x",buf[0],buf[1]);
        return 0;
    }
    return 1;
}

/*
 * �� fp �ж���һ���� prefix Ϊǰ׺�� long ֵ�����������浽 *target �С�
 *
 * ����ɹ����� 1 ����������� 0 ������ӡ������Ϣ��
 */
int readLong(FILE *fp, char prefix, long *target) {
    char buf[128], *eptr;

    epos = ftello(fp);

    // ������
    if (fgets(buf,sizeof(buf),fp) == NULL) {
        return 0;
    }

    // ȷ��ǰ׺��ͬ
    if (buf[0] != prefix) {
        ERROR("Expected prefix '%c', got: '%c'",buf[0],prefix);
        return 0;
    }

    // ���ַ���ת���� long ֵ
    *target = strtol(buf+1,&eptr,10);

    return consumeNewline(eptr);
}

/*
 * �� fp �ж�ȡָ�����ֽڣ�����ֵ���浽 *target �С�
 *
 * �����ȡ������ length ��������ͬ����ô���� 0 ������ӡ������Ϣ��
 * ��ȡ�ɹ��򷵻� 1 ��
 */
int readBytes(FILE *fp, char *target, long length) {
    long real;

    epos = ftello(fp);

    real = fread(target,1,length,fp);
    if (real != length) {
        ERROR("Expected to read %ld bytes, got %ld bytes",length,real);
        return 0;
    }

    return 1;
}

/*
 * ��ȡ�ַ���
 *
 * ��ȡ�ɹ��������� 1 ������ֵ������ target ָ���С�
 * ʧ�ܷ��� 0 ��
 */
int readString(FILE *fp, char** target) {

    // ��ȡ�ַ����ĳ���
    long len;
    *target = NULL;
    if (!readLong(fp,'$',&len)) {
        return 0;
    }

    /* Increase length to also consume \r\n */
    len += 2;

    // Ϊ�ַ�������ռ�
    *target = (char*)malloc(len);

    // ��ȡ����
    if (!readBytes(fp,*target,len)) {
        return 0;
    }

    // ȷ�� \r\n
    if (!consumeNewline(*target+len-2)) {
        return 0;
    }

    (*target)[len-2] = '\0';

    return 1;
}

/*
 * ��ȡ��������
 *
 * ��ȡ�ɹ��������� 1 �����������������浽 target �С�
 * ��ȡʧ�ܷ��� 0 ��
 */
int readArgc(FILE *fp, long *target) {
    return readLong(fp,'*',target);
}

/*
 * ����һ��ƫ���������ƫ���������ǣ�
 *
 * 1���ļ���ĩβ
 * 2���ļ��״γ��ֶ������ĵط�
 * 3���ļ���һ��û�� EXEC ƥ��� MULTI ��λ��
 */
off_t process(FILE *fp) {
    long argc;
    off_t pos = 0;
    int i, multi = 0;
    char *str;

    while(1) {

        // ��λ�����һ�� MULTI ���ֵ�ƫ����
        if (!multi) pos = ftello(fp);

        // ��ȡ�����ĸ���
        if (!readArgc(fp, &argc)) break;

        // ������������
        // �������������Լ��������
        // ���� SET key value 
        // SET ���ǵ�һ���������� key �� value ���ǵڶ��͵���������
        for (i = 0; i < argc; i++) {

            // ��ȡ����
            if (!readString(fp,&str)) break;

            // ��������Ƿ� MULTI ���� EXEC
            if (i == 0) {
                if (strcasecmp(str, "multi") == 0) {
                    // ��¼һ�� MULTI 
                    // ���ǰ���Ѿ���һ�� MULTI ����ô����MULTI ��Ӧ��Ƕ�ף�
                    if (multi++) {
                        ERROR("Unexpected MULTI");
                        break;
                    }
                } else if (strcasecmp(str, "exec") == 0) {
                    // ���һ�� MULTI ��¼
                    // ���ǰ��û�� MULTI ����ô����MULTI �� EXEC Ӧ��һ�ԶԳ��֣�
                    if (--multi) {
                        ERROR("Unexpected EXEC");
                        break;
                    }
                }
            }

            // �ͷ�
            free(str);
        }

        /* Stop if the loop did not finish 
         *
         * ��� for ѭ��û��������������ô���� while
         */
        if (i < argc) {
            if (str) free(str);
            break;
        }
    }

    // �ļ���ȡ���ˣ�����û���ҵ��� MULTI ��Ӧ�� EXEC
    if (feof(fp) && multi && strlen(error) == 0) {
        ERROR("Reached EOF before reading EXEC for MULTI");
    }

    // ����д�����֣���ô��ӡ����
    if (strlen(error) > 0) {
        printf("%s\n", error);
    }

    // ����ƫ����
    return pos;
}

int main(int argc, char **argv) {
    char *filename;
    int fix = 0;

    // ѡ�������� --fix ��ֻ��飬�������޸�
    if (argc < 2) {
        printf("Usage: %s [--fix] <file.aof>\n", argv[0]);
        exit(1);
    } else if (argc == 2) {
        filename = argv[1];
    } else if (argc == 3) {
        if (strcmp(argv[1],"--fix") != 0) {
            printf("Invalid argument: %s\n", argv[1]);
            exit(1);
        }
        filename = argv[2];
        fix = 1;
    } else {
        printf("Invalid arguments\n");
        exit(1);
    }

    // ��ָ���ļ�
    FILE *fp = fopen(filename,"r+");
    if (fp == NULL) {
        printf("Cannot open file: %s\n", filename);
        exit(1);
    }

    // ��ȡ�ļ���Ϣ
    struct redis_stat sb;
    if (redis_fstat(fileno(fp),&sb) == -1) {
        printf("Cannot stat file: %s\n", filename);
        exit(1);
    }

    // ȡ���ļ��Ĵ�С
    off_t size = sb.st_size;
    if (size == 0) {
        printf("Empty file: %s\n", filename);
        exit(1);
    }

    // ����ļ�������ô���ƫ����ָ��
    // 1�� ��һ�������ϸ�ʽ��λ��
    // 2�� ��һ��û�� EXEC ��Ӧ�� MULTI ��λ��
    // ����ļ�û�г�����ô���ƫ����ָ��
    // 3�� �ļ�ĩβ
    off_t pos = process(fp);
    // ����ƫ���������ļ�ĩβ�ж�Զ
    off_t diff = size-pos;
    printf("AOF analyzed: size=%lld, ok_up_to=%lld, diff=%lld\n",
        (long long) size, (long long) pos, (long long) diff);

    // ���� 0 ��ʾδ�����ļ�ĩβ������
    if (diff > 0) {

        // fix ģʽ�������޸��ļ�
        if (fix) {

            // ���Դӳ����λ�ÿ�ʼ��һֱɾ�����ļ���ĩβ
            char buf[2];
            printf("This will shrink the AOF from %lld bytes, with %lld bytes, to %lld bytes\n",(long long)size,(long long)diff,(long long)pos);
            printf("Continue? [y/N]: ");
            if (fgets(buf,sizeof(buf),stdin) == NULL ||
                strncasecmp(buf,"y",1) != 0) {
                    printf("Aborting...\n");
                    exit(1);
            }

            // ɾ������ȷ������
            if (ftruncate(fileno(fp), pos) == -1) {
                printf("Failed to truncate AOF\n");
                exit(1);
            } else {
                printf("Successfully truncated AOF\n");
            }

        // �� fix ģʽ��ֻ�����ļ����Ϸ�
        } else {
            printf("AOF is not valid\n");
            exit(1);
        }

    // ���� 0 ��ʾ�ļ��Ѿ�˳�����꣬�޴�
    } else {
        printf("AOF is valid\n");
    }

    // �ر��ļ�
    fclose(fp);

    return 0;
}
