/* delta.c
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
#include <stdint.h>
#include <string.h>
#include "delta.h"


#define ESC 0x7f

struct __attribute__((packed)) block_hdr {
    uint8_t esc;
    uint8_t off[3];
    uint8_t sz[2];
};

#define BLOCK_HDR_SIZE (sizeof (struct block_hdr))
#define MAX_SRC_SIZE (1 << 24)

#ifndef WOLFBOOT_SECTOR_SIZE
#   define WOLFBOOT_SECTOR_SIZE 0x1000
#endif

int wb_patch_init(WB_PATCH_CTX *bm, uint8_t *src, uint32_t ssz, uint8_t *patch, uint32_t psz)

{
    if (!bm || ssz == 0 || psz == 0) {
        return -1;
    }
    memset(bm, 0, sizeof(WB_PATCH_CTX));
    bm->src_base = src;
    bm->src_size = ssz;
    bm->patch_base = patch;
    bm->patch_size = psz;
    return 0;
}

int wb_patch(WB_PATCH_CTX *ctx, uint8_t *dst, uint32_t len)
{
    struct block_hdr *hdr;
    uint32_t dst_off = 0;
    uint32_t src_off;
    if (!ctx)
        return -1;
    if (len < BLOCK_HDR_SIZE)
        return -1;
    if (ctx->p_off >= ctx->patch_size)
        return 0;
    while ( (ctx->p_off < ctx->patch_size) && (dst_off < len)) {
        uint8_t *pp = (ctx->patch_base + ctx->p_off);
        if (*pp == ESC) {
            if (*(pp + 1) == ESC) {
                *(dst + dst_off) = ESC;
                 /* Two bytes of the patch have been consumed to produce ESC */
                ctx->p_off += 2;
                dst_off++;
                continue;
            } else {
                uint16_t sz;
                uint32_t copy_sz;
                hdr = (struct block_hdr *)pp;
                src_off = (hdr->off[0] << 16) + (hdr->off[1] << 8) +
                    hdr->off[2];
                sz = (hdr->sz[0] << 8) + hdr->sz[1];
                if ((src_off + sz) > ctx->src_size) {
                    return -1;
                }
                if (ctx->matching) {
                    /* Continue from previous block */
                    src_off += ctx->blk_off;
                    sz -= ctx->blk_off;
                }
                ctx->matching = 1;
                if (sz > len - dst_off) {
                    copy_sz = len - dst_off;
                    ctx->blk_off += copy_sz;
                } else {
                    copy_sz = sz;
                }
                memcpy(dst + dst_off, ctx->src_base + src_off, copy_sz);
                if (sz == copy_sz) {
                    /* End of the block, reset counters and matching state */
                    ctx->matching = 0;
                    ctx->blk_off = 0;
                    ctx->p_off += BLOCK_HDR_SIZE;
                }
                dst_off += copy_sz;
            }
        } else {
            *(dst + dst_off) = *(ctx->patch_base + ctx->p_off);
            dst_off++;
            ctx->p_off++;
        }
    }
    while (dst_off < len) {
        *(dst + dst_off) = *(ctx->patch_base + ctx->p_off);
        dst_off++;
        ctx->p_off++;
    }
    return dst_off;
}


int wb_diff_init(WB_DIFF_CTX *ctx, uint8_t *src_a, uint32_t len_a, uint8_t *src_b, uint32_t len_b)
{
    if (!ctx || (len_a == 0) || (len_b == 0))
        return -1;
    memset(ctx, 0, sizeof(WB_DIFF_CTX));
    ctx->src_a = src_a;
    ctx->src_b = src_b;
    ctx->size_a = len_a;
    ctx->size_b = len_b;
    return 0;
}

int wb_diff(WB_DIFF_CTX *ctx, uint8_t *patch, uint32_t len)
{
    struct block_hdr hdr;
    int found;
    uint8_t *pa;
    uint16_t match_len;
    uint32_t blk_start;
    uint32_t p_off = 0;
    if (ctx->off_b >= ctx->size_b)
        return 0;
    if (len < BLOCK_HDR_SIZE)
        return -1;
    while ((ctx->off_b < ctx->size_b) && (len > p_off + BLOCK_HDR_SIZE)) {
        uint32_t page_start = ctx->off_b / WOLFBOOT_SECTOR_SIZE;
        pa = ctx->src_a + page_start;
        found = 0;
        if (p_off + BLOCK_HDR_SIZE >  len)
            return p_off;
        while (((uint32_t)(pa - ctx->src_a) < ctx->size_a) && (p_off < len)) {
            if ((ctx->size_b - ctx->off_b) < BLOCK_HDR_SIZE)
                break;
            if ((memcmp(pa, (ctx->src_b + ctx->off_b), BLOCK_HDR_SIZE) == 0)) {
                match_len = BLOCK_HDR_SIZE;
                blk_start = pa - ctx->src_a;
                pa+= BLOCK_HDR_SIZE;
                ctx->off_b += BLOCK_HDR_SIZE;
                while (*pa == *(ctx->src_b + ctx->off_b)) {
                    match_len++;
                    pa++;
                    ctx->off_b++;
                    if ((uint32_t)(pa - ctx->src_a) >= ctx->size_a)
                        break;
                }
                hdr.esc = ESC;
                hdr.off[0] = ((blk_start >> 16) & 0x000000FF);
                hdr.off[1] = ((blk_start >> 8) & 0x000000FF);
                hdr.off[2] = ((blk_start) & 0x000000FF);
                hdr.sz[0] = ((match_len >> 8) & 0x00FF);
                hdr.sz[1] = ((match_len) & 0x00FF);
                memcpy(patch + p_off, &hdr, sizeof(hdr));
                p_off += BLOCK_HDR_SIZE;
                found = 1;
                break;
            } else pa++;
        }
        if (!found) {
            if (*(ctx->src_b + ctx->off_b) == ESC) {
                *(patch + p_off++) = ESC;
                *(patch + p_off++) = ESC;
            } else {
                *(patch + p_off++) = *(ctx->src_b + ctx->off_b);
            }
            ctx->off_b++;
        }
    }
    while ((p_off < len - BLOCK_HDR_SIZE) && ctx->off_b < ctx->size_b) {
        if (*(ctx->src_b + ctx->off_b) == ESC) {
            *(patch + p_off++) = ESC;
            *(patch + p_off++) = ESC;
        } else {
            *(patch + p_off++) = *(ctx->src_b + ctx->off_b);
        }
        ctx->off_b++;
    }
    return (p_off);
}


#ifdef BM_TEST_MAIN
#define MODE_DIFF 0
#define MODE_PATCH 1
#define BUF_SIZE 4096
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/mman.h>

int main(int argc, char *argv[])
{
    int mode;
    int fd1, fd2, fd3;
    int len1, len2, len3;
    struct stat st;
    void *base;
    void *buffer;
    uint8_t dest[64];
    uint8_t ff = 0xff;
    if (strcmp(basename(argv[0]), "bmdiff") == 0) {
        mode = MODE_DIFF;
    } else if (strcmp(basename(argv[0]), "bmpatch") == 0) {
        mode = MODE_PATCH;
    } else {
        return 244;
    }
    if (argc != 4) {
        if (mode == MODE_DIFF) {
            printf("Usage: %s file1 file2 patch\n");
        } else {
            printf("Usage: %s file patch destination\n");
        }
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

    fd1 = open(argv[1], O_RDONLY);
    if (fd1 < 0) {
        printf("Cannot open file %s\n", argv[1]);
        exit(3);
    }
    base = mmap(NULL, len1, PROT_READ, MAP_SHARED, fd1, 0);
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
    fd3 = open(argv[3], O_RDWR|O_CREAT|O_TRUNC, 0660);
    if (fd3 < 0) {
        printf("Cannot open file %s for writing\n", argv[3]);
        exit(3);
    }
    if (mode == MODE_DIFF) {
        int r;
        uint32_t blksz = 64;
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
        uint32_t blksz = 64;
        WB_PATCH_CTX px;
        if (len2 <= 0)
            exit(0);
        printf("Patching\n");
        lseek(fd3, MAX_SRC_SIZE -1, SEEK_SET);
        write(fd3, &ff, 1);
        lseek(fd3, 0, SEEK_SET);
        len3 = 0;
        if (wb_patch_init(&px, base, MAX_SRC_SIZE, buffer, len2) != 0) {
            exit(6);
        }
        do {
            r = wb_patch(&px, dest, blksz);
            if (r < 0)
                exit(5);
            if (r > 0)
                write(fd3, dest, r);
            len3 += r;
        } while (r > 0);
        ftruncate(fd3, len3);
    }
    return 0;
}
#endif
