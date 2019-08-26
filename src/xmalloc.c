/* xmalloc.h
 *
 * Implementations of minimal malloc/free
 *
 *
 * Copyright (C) 2019 wolfSSL Inc.
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

#ifndef NULL
#   define NULL ((void *)(0))
#endif

/* Allow one single sp_point to be allocated at one time */
#ifdef WOLFSSL_SP_ASM
#   define SP_POINT_SIZE (196)
#   define SCRATCHBOARD_SIZE (512)
#   define SP_DIGITS_SIZE (320)
#   define MAX_POINTS (4)
#   define MULTIPOINT_SIZE (16)
#else
#   define SP_POINT_SIZE (244)
#   define SCRATCHBOARD_SIZE (640)
#   define SP_DIGITS_SIZE (400)
#   define MAX_POINTS (2)
#   define MULTIPOINT_SIZE (3)
#endif
#define TMP_BUFFER_SIZE (124)
#define SP_NORMALIZER_SIZE (128)

static uint8_t sp_scratchboard[SCRATCHBOARD_SIZE];
static int sp_scratchboard_in_use = 0;

static int sp_point_in_use[MAX_POINTS] = { };
static uint8_t sp_point_buffer[MAX_POINTS][SP_POINT_SIZE];

static uint8_t tmp_buffer[TMP_BUFFER_SIZE];
static uint8_t sp_multipoint[SP_POINT_SIZE * MULTIPOINT_SIZE];
static uint8_t sp_digits[SP_DIGITS_SIZE];
static uint8_t sp_normalizer[SP_NORMALIZER_SIZE];

static int tmp_buffer_in_use = 0;
static int sp_multipoint_in_use = 0;
static int sp_digits_in_use = 0;
static int sp_normalizer_in_use = 0;

static void* xmalloc_sp_point(void)
{
    int i;
    for (i = 0; i < MAX_POINTS; i++) {
        if (sp_point_in_use[i] == 0) {
            sp_point_in_use[i]++;
            return sp_point_buffer[i];
        }
    }
    return NULL;
}

static void* xmalloc_sp_scratchboard(void)
{
    if (sp_scratchboard_in_use)
            return NULL;
    sp_scratchboard_in_use++;
    return sp_scratchboard;
}

static void* xmalloc_sp_tmpbuffer(void)
{
    if (tmp_buffer_in_use)
            return NULL;
    tmp_buffer_in_use++;
    return tmp_buffer;
}

static void* xmalloc_sp_multipoint(void)
{
    if (sp_multipoint_in_use)
            return NULL;
    sp_multipoint_in_use++;
    return sp_multipoint;
}

static void* xmalloc_sp_digits(void)
{
    if (sp_digits_in_use)
            return NULL;
    sp_digits_in_use++;
    return sp_digits;
}

static void* xmalloc_sp_normalizer(void)
{
    if (sp_normalizer_in_use)
            return NULL;
    sp_normalizer_in_use++;
    return sp_normalizer;
}


void* XMALLOC(size_t n, void* heap, int type)
{
    if (n == SP_POINT_SIZE)
        return xmalloc_sp_point();
    if (n == SCRATCHBOARD_SIZE)
        return xmalloc_sp_scratchboard();
    if (n == TMP_BUFFER_SIZE)
        return xmalloc_sp_tmpbuffer();
    if (n == MULTIPOINT_SIZE * SP_POINT_SIZE)
        return xmalloc_sp_multipoint();
    if (n == SP_DIGITS_SIZE)
        return xmalloc_sp_digits();
    if (n == SP_NORMALIZER_SIZE)
        return xmalloc_sp_normalizer();
    return NULL;
}

void XFREE(void *ptr)
{
    int i;
    if (ptr == sp_scratchboard)
        sp_scratchboard_in_use = 0;
    if (ptr == tmp_buffer)
        tmp_buffer_in_use = 0;
    if (ptr == sp_multipoint)
        sp_multipoint_in_use = 0;
    if (ptr == sp_digits)
        sp_digits_in_use = 0;
    if (ptr == sp_normalizer)
        sp_normalizer_in_use = 0;
    for (i = 0; i < MAX_POINTS; i++) {
        if (ptr == sp_point_buffer[i]) {
            sp_point_in_use[i] = 0;
            return;
        }
    }
}
