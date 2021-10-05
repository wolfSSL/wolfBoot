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
#include "image.h"


#define ESC 0x7f

#if (defined(__IAR_SYSTEMS_ICC__) && (__IAR_SYSTEMS_ICC__ > 8)) || \
    defined(__GNUC__)
#define BLOCK_HDR_PACKED __attribute__ ((packed))
#else
#define BLOCK_HDR_PACKED
#endif

struct BLOCK_HDR_PACKED block_hdr {
    uint8_t esc;
    uint8_t off[3];
    uint8_t sz[2];
};

#define BLOCK_HDR_SIZE (sizeof (struct block_hdr))

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

#ifdef EXT_FLASH

static inline uint8_t *patch_read_cache(WB_PATCH_CTX *ctx)
{
    ext_flash_check_read(
            (uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS + ctx->p_off,
            ctx->patch_cache, DELTA_PATCH_BLOCK_SIZE);
    ctx->patch_cache_start = ctx->p_off;
    return ctx->patch_cache;
}

static inline void store_patched(void *address, const uint8_t *data, int len)
{
    (void)ext_flash_check_write((uintptr_t)address, data, len);
}

#else

static inline uint8_t *patch_read_cache(WB_PATCH_CTX *ctx)
{
    return ctx->patch_base + ctx->p_off;
}

static inline void store_patched(void *address, const uint8_t *data, int len)
{
    memcpy(address, data, len);
}

#endif

int wb_patch(WB_PATCH_CTX *ctx, uint8_t *dst, uint32_t len)
{
    struct block_hdr *hdr;
    uint32_t dst_off = 0;
    uint32_t src_off;
    uint16_t sz;
    uint32_t copy_sz;
    if (!ctx)
        return -1;
    if (len < BLOCK_HDR_SIZE)
        return -1;

    while ( ( (ctx->matching != 0) || (ctx->p_off < ctx->patch_size)) && (dst_off < len)) {
#ifdef EXT_FLASH
        uint8_t *pp = patch_read_cache(ctx);
#else
        uint8_t *pp = (ctx->patch_base + ctx->p_off);
#endif
        if (ctx->matching) {
            /* Resume matching block from previous sector */
            sz = ctx->blk_sz;
            if (sz > len)
                sz = len;
            store_patched((void *)(dst + dst_off), ctx->src_base + ctx->blk_off, sz);
            if (ctx->blk_sz > len) {
                ctx->blk_sz -= len;
                ctx->blk_off += len;
            } else {
                ctx->blk_off = 0;
                ctx->blk_sz = 0;
                ctx->matching = 0;
            }
            dst_off += sz;
            continue;
        }
        if (*pp == ESC) {
            if (*(pp + 1) == ESC) {
                store_patched((void *)(dst + dst_off), pp + 1, 1);
                /* Two bytes of the patch have been consumed to produce ESC */
                ctx->p_off += 2;
                dst_off++;
                continue;
            } else {
                hdr = (struct block_hdr *)pp;
                src_off = (hdr->off[0] << 16) + (hdr->off[1] << 8) +
                    hdr->off[2];
                sz = (hdr->sz[0] << 8) + hdr->sz[1];
                ctx->matching = 1;
                if (sz > (len - dst_off)) {
                    copy_sz = len - dst_off;
                    ctx->blk_off = src_off + copy_sz;
                    ctx->blk_sz = sz - copy_sz;
                } else {
                    copy_sz = sz;
                }
                store_patched(dst + dst_off, ctx->src_base + src_off, copy_sz);
                if (sz == copy_sz) {
                    /* End of the block, reset counters and matching state */
                    ctx->matching = 0;
                    ctx->blk_off = 0;
                }
                ctx->p_off += BLOCK_HDR_SIZE;
                dst_off += copy_sz;
            }
        } else {
            store_patched((void *)(dst + dst_off), pp, 1);
            dst_off++;
            ctx->p_off++;
        }
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
    uint8_t *pa, *pb;
    uint16_t match_len;
    uint32_t blk_start;
    uint32_t p_off = 0;
    if (ctx->off_b >= ctx->size_b)
        return 0;
    if (len < BLOCK_HDR_SIZE)
        return -1;

    while ((ctx->off_b + BLOCK_HDR_SIZE < ctx->size_b) && (len > p_off + BLOCK_HDR_SIZE)) {
        uint32_t page_start = ctx->off_b / WOLFBOOT_SECTOR_SIZE;
        uint32_t pa_start;
        found = 0;
        if (p_off + BLOCK_HDR_SIZE >  len)
            return p_off;

        /* 'A' Patch base is valid for addresses in blocks ahead.
         * For matching previous blocks, 'B' is used as base instead.
         *
         * This mechanism ensures that the patch can refer to lower position
         * in a FLASH memory that is being patched, using the destination as
         * base for the sectors that have already been updated.
         */

        pa_start = (WOLFBOOT_SECTOR_SIZE + 1) * page_start;
        pa = ctx->src_a + pa_start;
        while (((uint32_t)(pa - ctx->src_a) < ctx->size_a ) && (p_off < len)) {
            if ((uint32_t)(ctx->size_a - (pa - ctx->src_a)) < BLOCK_HDR_SIZE)
                break;
            if ((ctx->size_b - ctx->off_b) < BLOCK_HDR_SIZE)
                break;
            if ((WOLFBOOT_SECTOR_SIZE - (ctx->off_b % WOLFBOOT_SECTOR_SIZE)) < BLOCK_HDR_SIZE)
                break;
            if ((memcmp(pa, (ctx->src_b + ctx->off_b), BLOCK_HDR_SIZE) == 0)) {
                uint32_t b_start;
                match_len = BLOCK_HDR_SIZE;
                blk_start = pa - ctx->src_a;
                b_start = ctx->off_b;
                pa+= BLOCK_HDR_SIZE;
                ctx->off_b += BLOCK_HDR_SIZE;
                while (*pa == *(ctx->src_b + ctx->off_b)) {
                    match_len++;
                    pa++;
                    ctx->off_b++;
                    if ((uint32_t)(pa - ctx->src_a) >= ctx->size_a)
                        break;
                    if ((b_start / WOLFBOOT_SECTOR_SIZE) < (ctx->off_b / WOLFBOOT_SECTOR_SIZE)) {
                        break;
                    }
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
            /* Try matching an earlier section in the resulting image */
            uint32_t pb_end = page_start * WOLFBOOT_SECTOR_SIZE;
            pb = ctx->src_b;
            while (((uint32_t)(pb - ctx->src_b) < pb_end) && (p_off < len)) {
                if ((ctx->size_b - ctx->off_b) < BLOCK_HDR_SIZE)
                    break;
                if ((uint32_t)(ctx->size_b - (pb - ctx->src_b)) < BLOCK_HDR_SIZE)
                    break;
                if (WOLFBOOT_SECTOR_SIZE > (pb - ctx->src_b) - (page_start * WOLFBOOT_SECTOR_SIZE))
                    break;
                if ((memcmp(pb, (ctx->src_b + ctx->off_b), BLOCK_HDR_SIZE) == 0)) {
                    match_len = BLOCK_HDR_SIZE;
                    blk_start = pb - ctx->src_b;
                    pb+= BLOCK_HDR_SIZE;
                    ctx->off_b += BLOCK_HDR_SIZE;
                    while (*pb == *(ctx->src_b + ctx->off_b)) {
                        match_len++;
                        pb++;
                        ctx->off_b++;
                        if ((uint32_t)(pb - ctx->src_b) >= pb_end)
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
                } else pb++;
            }
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


