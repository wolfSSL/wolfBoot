/* cm5.c
 *
 * HAL for the Raspberry Pi Compute Module 5 (CM5): Broadcom BCM2712,
 * quad-core Cortex-A76 (ARMv8.2-A).
 *
 * The SPI EEPROM bootloader loads wolfBoot (an ARM64 image carrying the Linux
 * image header) to 0x200000 and its built-in BL31 enters it at EL2; wolfBoot
 * verifies the signed payload and boots it from RAM. hal_flash_* are no-ops
 * (no in-place flash in this mode).
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
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <target.h>
#include "image.h"
#include "printf.h"
#include "hal/cm5.h"
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "sdhci.h"
#endif
#ifndef ARCH_AARCH64
#   error "wolfBoot cm5 HAL: wrong architecture selected. Please compile with ARCH=AARCH64."
#endif

/* Fixed addresses (provided by the linker script) */
extern void *kernel_addr, *update_addr, *dts_addr;

/* The plain RAM-boot build runs with the MMU off. FIPS and the disk path need
 * Normal cacheable memory: optimized code and the SDHCI block-buffer copies
 * fault on the Device memory an MMU-off EL2 sees. */
#if (defined(HAVE_FIPS) || defined(DISK_SDCARD) || defined(DISK_EMMC)) \
    && defined(__aarch64__)
#define CM5_USE_MMU
void cm5_mmu_enable(void);
void cm5_mmu_disable(void);
#endif

#if defined(DEBUG_UART)
static void uart_tx(char c)
{
    while (*UART10_FR & PL011_FR_TXFF)
        ;
    *UART10_DR = (unsigned int)(unsigned char)c;
}

void uart_init(void)
{
    /* The EEPROM bootloader leaves the debug PL011 running at 115200 8N1 when
     * it logs there (BOOT_UART=1 / uart_2ndstage=1); keep that setup. Program
     * it only when the UART was handed over disabled. */
    if ((*UART10_CR & PL011_CR_UARTEN) != 0)
        return;
    *UART10_CR = 0;
    *UART10_ICR = 0x7FF;
    *UART10_IBRD = BCM2712_UART10_IBRD_115200;
    *UART10_FBRD = BCM2712_UART10_FBRD_115200;
    *UART10_LCRH = (1 << 4) | (1 << 5) | (1 << 6); /* FIFO, 8-bit */
    *UART10_CR = PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE;
}

#ifdef CM5_LOG_RING
static void log_ring_putc(char c)
{
    static int ring_ready;
    volatile uint32_t *hdr = (volatile uint32_t *)(uintptr_t)CM5_LOG_RING_BASE;
    volatile uint8_t *data =
        (volatile uint8_t *)(uintptr_t)(CM5_LOG_RING_BASE + CM5_LOG_RING_HDR);
    uint32_t wr;

    if (!ring_ready) {
        /* Publish the magic last: until it is set a reader treats the region
         * as absent rather than reading a stale count. */
        hdr[0] = 0;
        hdr[1] = 0;
        hdr[2] = (uint32_t)CM5_LOG_RING_SIZE;
        hdr[3] = 0;
        hdr[0] = (uint32_t)CM5_LOG_RING_MAGIC;
        ring_ready = 1;
    }
    wr = hdr[1];
    data[wr % (uint32_t)CM5_LOG_RING_SIZE] = (uint8_t)c;
    hdr[1] = wr + 1;   /* monotonic: the reader derives wrap from it */
}

/* The ring is only readable after the next boot into Linux, so a hang must end
 * in a warm reset: run the PM watchdog for the life of the bootloader. */
static void log_ring_wdt(int enable)
{
    volatile uint32_t *rstc = (volatile uint32_t *)BCM2712_PM_RSTC;
    volatile uint32_t *wdog = (volatile uint32_t *)BCM2712_PM_WDOG;

    if (enable) {
        *wdog = PM_PASSWORD | PM_WDOG_TIME_MASK;
        *rstc = PM_PASSWORD | (*rstc & ~PM_RSTC_WRCFG_MASK & ~PM_PASSWORD) |
            PM_RSTC_WRCFG_FULL_RST;
    }
    else {
        *rstc = PM_PASSWORD | PM_RSTC_RESET;
    }
}
#endif /* CM5_LOG_RING */

void uart_write(const char* buf, uint32_t sz)
{
    while (sz-- > 0 && *buf) {
#ifdef CM5_LOG_RING
        log_ring_putc(*buf);
#endif
        uart_tx(*buf++);
    }
}
#endif /* DEBUG_UART */

void* hal_get_primary_address(void)
{
    return (void*)&kernel_addr;
}

void* hal_get_update_address(void)
{
    return (void*)&update_addr;
}

void* hal_get_dts_address(void)
{
    return (void*)&dts_addr;
}

void* hal_get_dts_update_address(void)
{
    return NULL; /* Not yet supported */
}

#ifdef EXT_FLASH
int ext_flash_read(unsigned long address, uint8_t *data, int len)
{
    XMEMCPY(data, (void *)address, len);
    return len;
}

int ext_flash_erase(unsigned long address, int len)
{
    XMEMSET((void *)address, 0xFF, len);
    return len;
}

int ext_flash_write(unsigned long address, const uint8_t *data, int len)
{
    XMEMCPY((void *)address, data, len);
    return len;
}

void ext_flash_lock(void)
{
}

void ext_flash_unlock(void)
{
}
#endif /* EXT_FLASH */

void hal_init(void)
{
#if defined(DEBUG_UART)
    unsigned long el;
    /* Printed before cm5_mmu_enable() on purpose, as on the CM4: safe only
     * because wolfBoot_printf is built -mstrict-align; a libc printf would
     * fault on the MMU-off Device memory. */
    uart_init();
#ifdef CM5_LOG_RING
    log_ring_wdt(1);
#endif
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    wolfBoot_printf("wolfBoot CM5 (BCM2712 Cortex-A76) hal_init, EL%d\n",
        (int)((el >> 2) & 0x3));
#endif
#if defined(CM5_USE_MMU)
    cm5_mmu_enable();
#endif
}

void hal_prepare_boot(void)
{
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
    /* Hand the OS a clean controller. Must run before the MMU is torn down. */
    sdhci_shutdown();
#endif
#if defined(DEBUG_UART) && defined(CM5_LOG_RING)
    log_ring_wdt(0);
#endif
#if defined(CM5_USE_MMU)
    cm5_mmu_disable();
#endif
}

#if defined(CM5_USE_MMU)
#ifndef TIMER_CLK_FREQ
#define TIMER_CLK_FREQ BCM2712_TIMER_CLK_FREQ
#endif
#include "aarch64_arch.h"

/* Identity map with 1 GB blocks at translation level 1. BCM2712 RAM starts at
 * 0 and the peripherals sit at 0x10_00000000 and up, so the map needs a 39-bit
 * VA (T0SZ=25) and a 40-bit PA: entries 0-3 are RAM, entry 0x40 holds the
 * SD/eMMC controllers and entry 0x41 the UART, PM, RNG and GIC. */
#define MMU_BLOCK_NORMAL  0x0000000000000701ULL /* block, AttrIdx0, AF, SH inner */
#define MMU_BLOCK_DEVICE  (0x0000000000000405ULL | (1ULL << 54) | (1ULL << 53))
#define MMU_BLOCK_NORMAL_NC 0x0000000000000709ULL /* AttrIdx2 */
#define CM5_L1_RAM_BLOCKS 4
#define CM5_L1_AXI_BLOCK  0x40 /* 0x10_00000000 */
#define CM5_L1_SOC_BLOCK  0x41 /* 0x10_40000000 */

static volatile uint64_t cm5_l1_table[512] __attribute__((aligned(4096)));

static void cm5_require_el2(void)
{
    unsigned long el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    if (((el >> 2) & 0x3) != 2) {
#if defined(DEBUG_UART)
        wolfBoot_printf("cm5: MMU setup requires EL2 (running at EL%d); halting\n",
            (int)((el >> 2) & 0x3));
#endif
        while (1)
            __asm__ volatile("wfi");
    }
}

void cm5_mmu_enable(void)
{
    unsigned long sctlr;
    int i;

    cm5_require_el2();
    for (i = 0; i < CM5_L1_RAM_BLOCKS; i++)
        cm5_l1_table[i] = ((uint64_t)i << 30) | MMU_BLOCK_NORMAL;
#ifdef CM5_LOG_RING
    /* The ring must reach DRAM before a watchdog reset: no caching there. */
    cm5_l1_table[CM5_LOG_RING_BASE >> 30] =
        ((uint64_t)(CM5_LOG_RING_BASE >> 30) << 30) | MMU_BLOCK_NORMAL_NC;
#endif
    cm5_l1_table[CM5_L1_AXI_BLOCK] =
        ((uint64_t)CM5_L1_AXI_BLOCK << 30) | MMU_BLOCK_DEVICE;
    cm5_l1_table[CM5_L1_SOC_BLOCK] =
        ((uint64_t)CM5_L1_SOC_BLOCK << 30) | MMU_BLOCK_DEVICE;
    /* MAIR: Attr0 = Normal WB, Attr1 = Device-nGnRnE, Attr2 = Normal NC. */
    __asm__ volatile("msr mair_el2, %0" :: "r"(0x00000000004400FFUL));
    __asm__ volatile("msr ttbr0_el2, %0"
        :: "r"((uint64_t)(uintptr_t)cm5_l1_table));
    /* TCR_EL2: T0SZ=25, 4KB granule, WB cacheable inner-shareable table
     * walks, PS=010 (40-bit PA). Bits 31 and 23 are RES1. */
    __asm__ volatile("msr tcr_el2, %0"
        :: "r"(0x0000000000023519UL | (1UL << 31) | (1UL << 23)));
    __asm__ volatile("isb");
    __asm__ volatile("tlbi alle2");
    __asm__ volatile("dsb sy");
    aarch64_dcache_maint(0);
    __asm__ volatile("ic iallu");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
    __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    sctlr |= (1UL << 0) | (1UL << 2) | (1UL << 12); /* M, C, I */
    __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
}

void cm5_mmu_disable(void)
{
    unsigned long sctlr;

    cm5_require_el2();
    /* Flush while the D-cache is still on: this function's own stack frame is
     * in dirty lines, so clearing SCTLR.C first would return to stale DRAM. */
    aarch64_dcache_maint(1);
    __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    sctlr &= ~((1UL << 0) | (1UL << 2) | (1UL << 12));
    __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
    __asm__ volatile("ic iallu");
    __asm__ volatile("tlbi alle2");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}
#endif /* CM5_USE_MMU */

int RAMFUNCTION hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address; (void)data; (void)len;
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_erase(uintptr_t address, int len)
{
    (void)address; (void)len;
    return 0;
}
