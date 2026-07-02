/* imx_rt.h
 *
 * Support routines for the i.MX RT HAL, kept free of NXP MCUXpresso SDK
 * dependencies so they can be exercised directly in the host unit tests.
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
#ifndef IMX_RT_H
#define IMX_RT_H

#include <stdint.h>

/* Flash is memory mapped, so after a program/erase, the affected range must
 * be invalidated in the data cache to ensure coherency. The cache line size
 * is 32 bytes, so both the address and length passed to
 * DCACHE_InvalidateByRange() must be 32-byte aligned: the address is rounded
 * down, and the length is rounded up by the same down-alignment offset plus
 * "len", so that the invalidated range always fully covers
 * [address, address + len). */
static inline void hal_flash_cache_align_range(uint32_t address, uint32_t len,
        uint32_t *aligned_address, uint32_t *aligned_len)
{
    uint32_t start = address - (address % 32);
    uint32_t unaligned_len = len + (address - start);

    *aligned_address = start;
    *aligned_len = unaligned_len + ((32 - (unaligned_len % 32)) % 32);
}

#endif /* IMX_RT_H */
