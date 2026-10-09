/* unit-mmio.h
 *
 * Maps host memory at a fixed MMIO address, for vendor SDK inlines that
 * access absolute register addresses (e.g. the NXP MCX MRCC clock gates).
 *
 * Copyright (C) 2026 wolfSSL Inc.
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

#ifndef UNIT_MMIO_H
#define UNIT_MMIO_H

#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0
#endif

/* Returns the mapping, or NULL if base is taken: never replaces a mapping */
static void *unit_mmio_map(uintptr_t base, size_t len)
{
    void *p = mmap((void *)base, len, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (p == MAP_FAILED)
        return NULL;
    if ((uintptr_t)p != base) {
        munmap(p, len);
        return NULL;
    }
    return p;
}

#endif /* UNIT_MMIO_H */
