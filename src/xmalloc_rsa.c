/* xmalloc_rsa.c
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

#include <stdlib.h>
#include <stdint.h>

/* Allow one single sp_point to be allocated at one time */
#define SP_DIGIT_SIZE (1800)
static uint8_t sp_digit[SP_DIGIT_SIZE];
static int sp_digit_in_use = 0;

static void* xmalloc_sp_digit(void)
{
    if (sp_digit_in_use)
            return NULL;
    sp_digit_in_use++;
    return sp_digit;
}


void* XMALLOC(size_t n, void* heap, int type)
{
    if (n == SP_DIGIT_SIZE)
        return xmalloc_sp_digit();
    return NULL;
}

void XFREE(void *ptr)
{
    int i;
    if (ptr == sp_digit)
        sp_digit_in_use = 0;
}
