/* delta.h
 *
 * Implementation of wolfBoot diff/patch functions,
 *
 * The format of the patch is based on the mechanism suggested
 * by Bentley/McIlroy, which is particularly effective
 * to generate small binary patches.
 *
 *
 * Compile with DELTA_UPDATES=1
 *
 * Use tools/sign.py or tool/sign.c on the host to provide small
 * secure update packages containing only binary difference, using the
 * --delta option.
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
#ifndef WOLFBOOT_DELTA_H
#define WOLFBOOT_DELTA_H
#include "target.h"

#define DELTA_PATCH_BLOCK_SIZE 1024

struct wb_patch_ctx {
    uint8_t *src_base;
    uint32_t src_size;
    uint8_t *patch_base;
    uint32_t patch_size;
    uint32_t p_off;
    int matching;
    uint32_t blk_sz;
    uint32_t blk_off;
#ifdef EXT_FLASH
    uint8_t patch_cache[DELTA_PATCH_BLOCK_SIZE];
    uint32_t patch_cache_start;
#endif
};

struct wb_diff_ctx {
    uint8_t *src_a;
    uint8_t *src_b;
    uint32_t size_a, size_b, off_b;
};


typedef struct wb_patch_ctx WB_PATCH_CTX;
typedef struct wb_diff_ctx WB_DIFF_CTX;

int wb_diff_init(WB_DIFF_CTX *ctx, uint8_t *src_a, uint32_t len_a, uint8_t *src_b, uint32_t len_b);
int wb_diff(WB_DIFF_CTX *ctx, uint8_t *patch, uint32_t len);
int wb_patch_init(WB_PATCH_CTX *bm, uint8_t *src, uint32_t ssz, uint8_t *patch, uint32_t psz);
int wb_patch(WB_PATCH_CTX *ctx, uint8_t *dst, uint32_t len);
int wolfBoot_get_delta_info(uint8_t part, int inverse, uint32_t **img_offset, uint16_t **img_size);

#endif

