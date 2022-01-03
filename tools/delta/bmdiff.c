/* bmdiff.c
 *
 * diff/patch tool for wolfBoot
 *
 *
 * Copyright (C) 2021 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */
#define MODE_DIFF 0
#define MODE_PATCH 1
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include "delta.h"

#define MAX_SRC_SIZE (1 << 24)
#define PATCH_BLOCK_SIZE WOLFBOOT_SECTOR_SIZE

int main(int argc, char *argv[])
{
    int mode;
    int fd1, fd2, fd3;
    int len1, len2, len3;
    struct stat st;
    void *base;
    void *buffer;
    uint8_t dest[PATCH_BLOCK_SIZE];
    uint8_t ff = 0xff;
    if (strcmp(basename(argv[0]), "bmdiff") == 0) {
        mode = MODE_DIFF;
    } else if (strcmp(basename(argv[0]), "bmpatch") == 0) {
        mode = MODE_PATCH;
    } else {
        return 244;
    }
    if ((argc != 4) && (mode == MODE_DIFF)) {
            printf("Usage: %s file1 file2 patch\n", argv[0]);
            exit(2);
    }
    if ((argc != 3) && (mode == MODE_PATCH)) {
        printf("Usage: %s file patch (WARNING: patching is done in place and it"
                "will overwrite the original source.)\n", argv[0]);
        exit(2);
    }

    /* Get source file size */
    if (stat(argv[1], &st) < 0) {
        printf("Cannot stat %s\n", argv[1]);
        exit(3);
    }
    len1 = st.st_size;

    if (len1 > MAX_SRC_SIZE) {
        printf("%s: file too large\n", argv[1]);
        exit(3);
    }

    fd1 = open(argv[1], O_RDWR);
    if (fd1 < 0) {
        printf("Cannot open file %s\n", argv[1]);
        exit(3);
    }
    base = mmap(NULL, len1, PROT_READ|PROT_WRITE, MAP_SHARED, fd1, 0);
    if (base == (void *)(-1)) {
        perror("mmap");
        exit(3);
    }
    fd2 = open(argv[2], O_RDONLY);
    if (fd2 < 0) {
        printf("Cannot open file %s\n", argv[2]);
        exit(3);
    }
    /* Get second file size */
    if (stat(argv[2], &st) < 0) {
        printf("Cannot stat %s\n", argv[2]);
        exit(3);
    }
    len2 = st.st_size;
    buffer = mmap(NULL, len2, PROT_READ, MAP_SHARED, fd2, 0);
    if (base == (void *)(-1)) {
        perror("mmap");
        exit(3);
    }
    if (mode == MODE_DIFF) {
        int r;
        uint32_t blksz = PATCH_BLOCK_SIZE;
        fd3 = open(argv[3], O_RDWR|O_CREAT|O_TRUNC, 0660);
        if (fd3 < 0) {
            printf("Cannot open file %s for writing\n", argv[3]);
            exit(3);
        }
        WB_DIFF_CTX dx;
        if (len2 <= 0) {
            exit(0);
        }
        lseek(fd3, MAX_SRC_SIZE -1, SEEK_SET);
        write(fd3, &ff, 1);
        lseek(fd3, 0, SEEK_SET);
        len3 = 0;
        if (wb_diff_init(&dx, base, len1, buffer, len2) < 0) {
            exit(6);
        }
        do {
            r = wb_diff(&dx, dest, blksz);
            if (r < 0)
                exit(4);
            write(fd3, dest, r);
            len3 += r;
        } while (r > 0);
        ftruncate(fd3, len3);
    }
    if (mode == MODE_PATCH) {
        int r;
        uint32_t blksz = PATCH_BLOCK_SIZE;
        WB_PATCH_CTX px;
        if (len2 <= 0)
            exit(0);
        len3 = 0;
        if (wb_patch_init(&px, base, len1, buffer, len2) != 0) {
            exit(6);
        }
        do {
            r = wb_patch(&px, dest, blksz);
            if (r < 0)
                exit(5);
            if (r > 0) {
                memcpy(base + len3, dest, r);
                len3 += r;
            }
        } while (r > 0);
        munmap(base, len1);
        lseek(fd1, 0, SEEK_SET);
        ftruncate(fd1, len3);
        close(fd1);
    }
    return 0;
}
