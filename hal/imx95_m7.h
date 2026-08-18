/* imx95_m7.h
 *
 * Shared definitions for the Cortex-M7 on the NXP i.MX95.
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

/* The shared-memory layout below is read by the A55 cluster running Linux, so
 * it is described in exactly one place: the HAL, the console driver and the
 * test app all include this header rather than repeating the addresses. */

#ifndef IMX95_M7_H
#define IMX95_M7_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Memory map
 *
 * The M7's reserved DDR window is memory@80000000, 16 MiB. wolfBoot's
 * partitions sit in the lower part of it (BOOT 0x80100000, UPDATE 0x80500000,
 * SWAP 0x80900000); the shared window below occupies the top of it, clear of
 * the RPMsg reservations at 0x88000000 which belong to the transport.
 *
 *   0x80F00000  console header (16 B) + ring (65520 B)
 *   0x80F10000  wolfBoot status block  (4 words)
 *   0x80F10010  test-app status block  (2 words)
 * ------------------------------------------------------------------------- */

#define M7_DDR_BASE 0x80000000UL
#define M7_DDR_SIZE 0x01000000UL

/* Console ring. CONSOLE_REGION is the whole 64 KiB page; the ring is what is
 * left of it once the header is accounted for, so the console cannot run into
 * the status block above it. Cortex-M7 has a hardware UDIV, so the
 * non-power-of-two modulo in uart_tx() costs a few cycles, not a helper call. */
#define CONSOLE_BASE     0x80F00000UL
#define CONSOLE_HDR_SIZE 16UL
#define CONSOLE_REGION   0x00010000UL
#define CONSOLE_SIZE     (CONSOLE_REGION - CONSOLE_HDR_SIZE)
/* Stored bytes are 57 43 4F 4E, so a hexdump of the ring reads "WCON". */
#define CONSOLE_MAGIC    0x4E4F4357UL

/* Status blocks. These are read one 32-bit word at a time with devmem, which
 * prints the word rather than its bytes, so the magics are spelled to read
 * correctly in that view ("WBOT", "APP1") - the opposite convention from
 * CONSOLE_MAGIC above, which is read from a hexdump. */
#define WOLFBOOT_STATUS_ADDR  0x80F10000UL
#define WOLFBOOT_STATUS_WORDS 4U
#define WOLFBOOT_STATUS_MAGIC 0x57424F54UL /* "WBOT" */

#define APP_STATUS_ADDR  (WOLFBOOT_STATUS_ADDR + (WOLFBOOT_STATUS_WORDS * 4UL))
#define APP_STATUS_WORDS 2U
#define APP_STATUS_MAGIC 0x41505031UL /* "APP1" */

/* The ring and the status block used to overlap by exactly the header size.
 * Fail the build rather than let the two drift apart again. */
typedef char imx95_console_fits_below_status[
    ((CONSOLE_BASE + CONSOLE_HDR_SIZE + CONSOLE_SIZE) <= WOLFBOOT_STATUS_ADDR)
    ? 1 : -1];

/* ---------------------------------------------------------------------------
 * Barriers and cache maintenance
 *
 * Both L1 caches are off out of reset. Once they are on, note that "volatile"
 * only binds the compiler: it places no barrier on the core, and the A55 is a
 * genuinely separate observer of the shared window, so publish protocols need
 * a real DMB.
 *
 * The ARMv7-M default memory map makes 0x80000000-0x9FFFFFFF Normal
 * write-through, which is why M7 stores to the shared window reach DDR at all
 * without maintenance. The explicit cleans below cost close to nothing on a
 * write-through line and stop that inherited attribute from being load-bearing
 * if a future MPU region, NO_MPU=0 or a different carveout base changes it.
 * ------------------------------------------------------------------------- */

#define DMB() __asm__ volatile ("dmb 0xF" ::: "memory")
#define DSB() __asm__ volatile ("dsb 0xF" ::: "memory")
#define ISB() __asm__ volatile ("isb 0xF" ::: "memory")

#define SCB_CCR     (*(volatile uint32_t *)0xE000ED14UL)
#define SCB_CCSIDR  (*(volatile uint32_t *)0xE000ED80UL)
#define SCB_CSSELR  (*(volatile uint32_t *)0xE000ED84UL)
#define SCB_ICIALLU (*(volatile uint32_t *)0xE000EF50UL)
#define SCB_DCISW   (*(volatile uint32_t *)0xE000EF60UL)
#define SCB_DCCMVAC (*(volatile uint32_t *)0xE000EF68UL)
#define SCB_DCCSW   (*(volatile uint32_t *)0xE000EF6CUL)
#define CCR_IC      (1UL << 17)
#define CCR_DC      (1UL << 16)

/* Cortex-M7 L1 line size. Also derived from CCSIDR where the set/way loops
 * need it; this constant is only for maintenance by address. */
#define IMX95_CACHE_LINE 32UL

static inline void imx95_dcache_clean(const void *addr, uint32_t len)
{
    uint32_t line;
    uint32_t end;

    if (len == 0)
        return;
    line = ((uint32_t)(uintptr_t)addr) & ~(IMX95_CACHE_LINE - 1UL);
    end = (uint32_t)(uintptr_t)addr + len;
    for (; line < end; line += IMX95_CACHE_LINE) {
        SCB_DCCMVAC = line;
    }
    DSB();
}

/* ---------------------------------------------------------------------------
 * DWT cycle counter
 *
 * With no console UART routed on this carrier the cycle counter is how boot
 * cost gets measured: exact, free to read, and needing no peripheral.
 * ------------------------------------------------------------------------- */

#define CORE_DEMCR    (*(volatile uint32_t *)0xE000EDFCUL)
#define DEMCR_TRCENA  (1UL << 24)
#define DWT_CTRL      (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNTENA (1UL << 0)
#define DWT_CYCCNT    (*(volatile uint32_t *)0xE0001004UL)

/* Idempotent: a second caller must not restart a counter someone else is
 * already extending to 64 bits. */
static inline void imx95_dwt_init(void)
{
    if ((DWT_CTRL & DWT_CYCCNTENA) != 0UL)
        return;
    CORE_DEMCR |= DEMCR_TRCENA;
    DWT_CYCCNT = 0;
    DWT_CTRL |= DWT_CYCCNTENA;
}

/* Bounded spin. Wrap-safe for any delay shorter than a full 32-bit period
 * (~5.4 s at 800 MHz) because the comparison is done on the difference. */
static inline void imx95_delay_cycles(uint32_t cycles)
{
    uint32_t start = DWT_CYCCNT;

    while ((uint32_t)(DWT_CYCCNT - start) < cycles)
        ;
}

/* ---------------------------------------------------------------------------
 * SysTick (test app only; wolfBoot itself runs with interrupts off)
 * ------------------------------------------------------------------------- */

#define SYST_CSR (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018UL)
#define SYST_CSR_ENABLE    (1UL << 0)
#define SYST_CSR_TICKINT   (1UL << 1)
#define SYST_CSR_CLKSOURCE (1UL << 2)
#define SYST_RVR_MAX       0x00FFFFFFUL

#endif /* IMX95_M7_H */
