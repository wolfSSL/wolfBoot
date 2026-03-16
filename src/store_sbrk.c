/* store_sbrk.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#include <stddef.h>
#include <limits.h>

#include "store_sbrk.h"

void *wolfboot_store_sbrk(unsigned int incr, uint8_t **heap,
    uint8_t *heap_base, uint32_t heap_size)
{
    uint8_t *heap_limit = heap_base + heap_size;
    void *old_heap = *heap;

    if ((incr & 3U) != 0U) {
        if (incr > (UINT_MAX - 3U))
            return (void *)-1;
        incr = (incr + 3U) & ~3U;
    }

    if (*heap == NULL) {
        *heap = heap_base;
        old_heap = *heap;
    }

    if ((uint32_t)(heap_limit - *heap) < incr)
        return (void *)-1;

    *heap += incr;

    return old_heap;
}
