/* boot_aarch64_cache.S - shared AArch64 set/way data cache maintenance.
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

/* Clean and invalidate all data cache levels to the PoC by set/way. Valid only
 * on a single core out of reset, immediately before the cache is turned off -
 * which does not write dirty lines back by itself. Its own translation unit so a
 * loader that links none of the rest of boot_aarch64_start.S can still call it. */
    .section ".text","ax"
    .globl invalidate_dcaches
invalidate_dcaches:
    dmb    ISH
    mrs    x0, CLIDR_EL1          /* x0 = CLIDR */
    ubfx   w2, w0, #24, #3        /* w2 = CLIDR.LoC */
    cmp    w2, #0                 /* LoC is 0? */
    b.eq   invalidatecaches_end   /* No cleaning required and enable MMU */
    mov    w1, #0                 /* w1 = level iterator */

invalidatecaches_flush_level:
    add    w3, w1, w1, lsl #1     /* w3 = w1 * 3 (right-shift for cache type) */
    lsr    w3, w0, w3             /* w3 = w0 >> w3 */
    ubfx   w3, w3, #0, #3         /* w3 = cache type of this level */
    cmp    w3, #2                 /* No cache at this level? */
    b.lt   invalidatecaches_next_level

    lsl    w4, w1, #1
    msr    CSSELR_EL1, x4         /* Select current cache level in CSSELR */
    isb                           /* ISB required to reflect new CSIDR */
    mrs    x4, CCSIDR_EL1         /* w4 = CSIDR */

    ubfx   w3, w4, #0, #3
    add    w3, w3, #2             /* w3 = log2(line size) */
    ubfx   w5, w4, #13, #15
    ubfx   w4, w4, #3, #10        /* w4 = Way number */
    clz    w6, w4                 /* w6 = 32 - log2(number of ways) */

invalidatecaches_flush_set:
    mov    w8, w4                 /* w8 = Way number */
invalidatecaches_flush_way:
    lsl    w7, w1, #1             /* Fill level field */
    lsl    w9, w5, w3
    orr    w7, w7, w9             /* Fill index field */
    lsl    w9, w8, w6
    orr    w7, w7, w9             /* Fill way field */
    dc     CISW, x7               /* Invalidate by set/way to point of coherency */
    subs   w8, w8, #1             /* Decrement way */
    b.ge   invalidatecaches_flush_way
    subs   w5, w5, #1             /* Descrement set */
    b.ge   invalidatecaches_flush_set

invalidatecaches_next_level:
    add    w1, w1, #1             /* Next level */
    cmp    w2, w1
    b.gt   invalidatecaches_flush_level

invalidatecaches_end:
    ret
