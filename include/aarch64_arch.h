/* aarch64_arch.h
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

/* Architectural AArch64 helpers that carry no SoC-specific state: the generic
 * timer and set/way data-cache maintenance. Several HALs each grew their own
 * copy of these; this is the shared home for them. Inline, so including it
 * changes nothing for a HAL that does not call them.
 *
 * Define TIMER_CLK_FREQ before including for the rate to assume when firmware
 * left CNTFRQ_EL0 unprogrammed. */

#ifndef _AARCH64_ARCH_H_
#define _AARCH64_ARCH_H_

#include <stdint.h>

#ifndef TIMER_CLK_FREQ
#error "TIMER_CLK_FREQ must be defined before including aarch64_arch.h"
#endif

/* CNTPCT_EL0, the architectural counter. */
static inline uint64_t timer_get_count(void)
{
    uint64_t cntpct;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r" (cntpct));
    return cntpct;
}

static inline uint64_t timer_get_freq(void)
{
    uint64_t cntfrq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r" (cntfrq));
    return cntfrq ? cntfrq : TIMER_CLK_FREQ;
}

/* Deadline helpers for polling loops: one division at setup, none in the
 * loop itself. */
static inline uint64_t timer_deadline_us(uint32_t us)
{
    return timer_get_count() + (((uint64_t)us * timer_get_freq()) / 1000000ULL);
}

/* Strictly greater, not >=, and deliberately so. A deadline is computed from
 * a count taken at an arbitrary point within a tick, so the first tick is
 * already partly spent. Exiting at == would let hal_delay_us() return early
 * by up to one tick, which on a 1 MHz generic timer is the whole of a
 * requested 1 us delay. Waiting one extra tick is the safe direction for a
 * delay and costs a timeout nothing. */
static inline int timer_expired(uint64_t deadline)
{
    return timer_get_count() > deadline;
}

/* Data-cache maintenance by set/way over every level to the point of
 * coherency. clean != 0 -> clean+invalidate, else invalidate only. */
static inline void aarch64_dcache_maint(int clean)
{
    uint64_t clidr, ccsidr;
    unsigned int level, loc, ctype, linesize, ways, sets, way, set, wayshift;

    __asm__ volatile("dsb sy");
    __asm__ volatile("mrs %0, clidr_el1" : "=r"(clidr));
    loc = (unsigned int)((clidr >> 24) & 0x7);
    for (level = 0; level < loc; level++) {
        ctype = (unsigned int)((clidr >> (level * 3)) & 0x7);
        if (ctype < 2)          /* no data or unified cache at this level */
            continue;
        __asm__ volatile("msr csselr_el1, %0" :: "r"((uint64_t)(level << 1)));
        __asm__ volatile("isb");
        __asm__ volatile("mrs %0, ccsidr_el1" : "=r"(ccsidr));
        linesize = (unsigned int)(ccsidr & 0x7) + 4;
        ways     = (unsigned int)((ccsidr >> 3) & 0x3FF);
        sets     = (unsigned int)((ccsidr >> 13) & 0x7FFF);
        /* __builtin_clz(0) is undefined; a direct-mapped cache never uses the
         * way field, so the shift amount does not matter there. */
        wayshift = (ways == 0) ? 32u : (unsigned int)__builtin_clz(ways);
        for (set = 0; set <= sets; set++) {
            for (way = 0; way <= ways; way++) {
                uint64_t val = ((uint64_t)(level << 1))
                    | ((uint64_t)way << wayshift)
                    | ((uint64_t)set << linesize);
                if (clean)
                    __asm__ volatile("dc cisw, %0" :: "r"(val));
                else
                    __asm__ volatile("dc isw, %0" :: "r"(val));
            }
        }
    }
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}

#endif /* _AARCH64_ARCH_H_ */
