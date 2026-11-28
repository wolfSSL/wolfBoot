/* cm4.c
 *
 * HAL for the Raspberry Pi Compute Module 4 (CM4): Broadcom BCM2711,
 * quad-core Cortex-A72 (ARMv8-A).
 *
 * The VideoCore GPU firmware loads wolfBoot (an ARM64 kernel8.img carrying the
 * Linux image header) to 0x200000 and enters it at EL2; wolfBoot verifies the
 * signed payload and boots it from RAM, or from eMMC/SD A/B via the generic
 * SDHCI driver (glue in hal/rpi_common.c). hal_flash_* are no-ops (no in-place
 * flash in this mode).
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
#include "hal/cm4.h"
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "sdhci.h"   /* sdhci_shutdown(), called from hal_prepare_boot() */
#endif
#ifndef ARCH_AARCH64
#   error "wolfBoot cm4 HAL: wrong architecture selected. Please compile with ARCH=AARCH64."
#endif

/* Hardware register map (bases, UART, EMMC2/SDHCI) is in hal/cm4.h */

/* Fixed addresses (provided by the linker script) */
extern void *kernel_addr, *update_addr, *dts_addr;

/* Enable the identity MMU + caches when the build does more than the trivial
 * RAM-boot: FIPS (unaligned/SIMD in the module), or the disk path (optimized
 * code + SDHCI block-buffer memcpy fault on MMU-off Device memory). Normal
 * cacheable memory permits those accesses and speeds up crypto/disk reads. */
#if (defined(HAVE_FIPS) || defined(DISK_SDCARD) || defined(DISK_EMMC)) \
    && defined(__aarch64__)
#define CM4_USE_MMU
#endif

#if defined(CM4_USE_MMU)
void cm4_mmu_enable(void);  /* defined below; called from hal_init */
void cm4_mmu_disable(void); /* defined below; called from hal_prepare_boot */
#endif

#if defined(DEBUG_UART)
/* Console UART select. On this bench CM4 the debug cable on GPIO14/15 is the
 * BCM2711 mini-UART (AUX, Linux ttyS0), so that is the default. Boards where
 * dtoverlay=disable-bt actually routes the PL011 onto GPIO14/15 can build with
 * CM4_UART_PL011 to use the PL011 (0xFE201000) instead. */
#if defined(CM4_UART_PL011)
static void uart_tx(char c)
{
    while (*UART0_FR & 0x20) /* TXFF: wait while FIFO full */
        ;
    *UART0_DR = (unsigned int)(unsigned char)c;
}

void uart_init(void)
{
    /* Route GPIO14 (TXD0) and GPIO15 (RXD0) to the PL011 via ALT0, so wolfBoot's
     * console reaches the 40-pin debug header even when the firmware has NOT
     * applied dtoverlay=disable-bt (which needs overlays/disable-bt.dtbo on the
     * boot FAT). Without this the PL011 stays wired to the Bluetooth pins and
     * wolfBoot is silent on the header. GPFSEL1 (GPIO_BASE+0x04) holds FSEL10-19:
     * FSEL14 = bits [14:12], FSEL15 = bits [17:15]; ALT0 = 0b100. */
    volatile unsigned int *gpfsel1 =
        (volatile unsigned int *)(BCM2711_GPIO_BASE + 0x04);
    unsigned int fsel;

    fsel = *gpfsel1;
    fsel &= ~((7u << 12) | (7u << 15));
    fsel |=  ((4u << 12) | (4u << 15));
    *gpfsel1 = fsel;

    /* Program the PL011 for 115200 8N1 assuming a 48MHz UARTCLK
     * (init_uart_clock=48000000): BAUDDIV = 48e6/(16*115200) -> IBRD 26 FBRD 3. */
    *UART0_CR = 0;
    *UART0_ICR = 0x7FF;
    *UART0_IBRD = 26;
    *UART0_FBRD = 3;
    *UART0_LCRH = (1 << 4) | (1 << 5) | (1 << 6); /* FIFO, 8-bit */
    *UART0_CR = (1 << 0) | (1 << 8) | (1 << 9);   /* enable UART, TX, RX */
}
#else /* mini-UART (default) */
static void uart_tx(char c)
{
    while ((*MU_LSR & MU_LSR_TXFF_EMPTY) == 0) /* wait until TX can accept */
        ;
    *MU_IO = (unsigned int)(unsigned char)c;
}

void uart_init(void)
{
    /* The firmware has already enabled the mini-UART at a stable baud
     * (enable_uart=1 fixes core_freq), so - like the Linux 8250 console with
     * "skip-init" - wolfBoot inherits that setup and just writes AUX_MU_IO.
     * Reprogramming the baud here is unnecessary (and error-prone: the mini-UART
     * clock is core_freq-derived, not a fixed rate). */
}
#endif /* CM4_UART_PL011 */

void uart_write(const char* buf, uint32_t sz)
{
    while (sz-- > 0 && *buf)
        uart_tx(*buf++);
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
    /* The banner is emitted before cm4_mmu_enable() on purpose - it is the
     * earliest bring-up signal. This is safe because wolfBoot's uart_printf
     * (src/string.c) is built -mstrict-align and performs no unaligned / SIMD
     * access; do NOT route the banner through a libc printf, which would fault
     * on the MMU-off Device memory this runs on. */
    uart_init();
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    wolfBoot_printf("wolfBoot CM4 (BCM2711 Cortex-A72) hal_init, EL%d\n",
        (int)((el >> 2) & 0x3));
#endif
#if defined(CM4_USE_MMU)
    /* Bring up Normal cacheable memory before any code that uses unaligned /
     * SIMD accesses (FIPS module, optimized disk path) which the MMU-off
     * Device memory rejects. */
    cm4_mmu_enable();
#endif
}

void hal_prepare_boot(void)
{
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
    /* Hand the OS a clean SDHCI controller: wolfBoot just used it (PIO, clocked)
     * for the image + env I/O, and the leftover state makes the OS driver's
     * re-init / tuning intermittently flake. Must run BEFORE the MMU is torn
     * down (the shutdown touches the controller through Normal-cacheable MMIO).
     * disk_close() on this target is a no-op, so the reset happens here. */
    sdhci_shutdown();
#endif
#if defined(CM4_USE_MMU)
    /* Undo cm4_mmu_enable() before handoff: flush the loaded image out of the
     * D-cache and return to the MMU-off state the application expects. */
    cm4_mmu_disable();
#endif
}


#if defined(CM4_USE_MMU)
/* Set/way cache maintenance comes from the shared AArch64 helpers. The
 * include sits inside this guard because the host unit test compiles this
 * whole file, where the header's TIMER_CLK_FREQ requirement does not apply;
 * hal/rpi_common.c has its own hal_get_timer_us(), which needs 128-bit math. */
#ifndef TIMER_CLK_FREQ
#define TIMER_CLK_FREQ BCM2711_TIMER_CLK_FREQ
#endif
#include "aarch64_arch.h"

/* Minimal identity-mapped MMU + caches for the CM4. wolfBoot's simple startup
 * runs with the MMU off, so all memory is Device-nGnRnE, which faults on the
 * unaligned / 128-bit SIMD accesses that the FIPS module, newlib printf, and
 * the optimized disk/SDHCI code paths perform. Mapping DDR as Normal
 * (cacheable) permits those accesses and speeds up crypto/disk reads; the
 * peripheral region (incl. 0xFE000000) stays Device. Four 1GB block
 * descriptors cover the 32-bit VA space at translation level 1. */
#define MMU_BLOCK_NORMAL  0x0000000000000701ULL /* block, AttrIdx0, AF, SH inner */
/* Device MMIO: mark execute-never (UXN bit 54 | PXN bit 53). Instruction fetch
 * from Device memory is CONSTRAINED UNPREDICTABLE and several BCM2711 registers
 * are read-destructive, so a speculative fetch there is a real hazard. Matches
 * the non-executable Device attributes in boot_aarch64_start.S. */
#define MMU_BLOCK_DEVICE  (0x0000000000000405ULL | (1ULL << 54) | (1ULL << 53))

static volatile uint64_t cm4_l1_table[512] __attribute__((aligned(4096)));


/* MMU/cache setup uses EL2 system registers; wolfBoot enters at EL2 on the CM4.
 * Guard against an EL1 entry (a custom armstub) so the msr *_el2 below do not
 * trap silently before anything can be reported. */
static void cm4_require_el2(void)
{
    unsigned long el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    if (((el >> 2) & 0x3) != 2) {
#if defined(DEBUG_UART)
        wolfBoot_printf("cm4: MMU setup requires EL2 (running at EL%d); halting\n",
            (int)((el >> 2) & 0x3));
#endif
        while (1)
            __asm__ volatile("wfi");
    }
}

void cm4_mmu_enable(void)
{
    unsigned long sctlr;
    int i;

    cm4_require_el2();
    /* 0-3GB DDR -> Normal; 3-4GB peripherals (0xFE000000) -> Device. */
    for (i = 0; i < 4; i++) {
        uint64_t base = (uint64_t)i << 30;
        cm4_l1_table[i] = base | ((i == 3) ? MMU_BLOCK_DEVICE : MMU_BLOCK_NORMAL);
    }
    /* MAIR: Attr0 = 0xFF Normal WB write-alloc, Attr1 = 0x00 Device-nGnRnE. */
    __asm__ volatile("msr mair_el2, %0" :: "r"(0x00000000000000FFUL));
    __asm__ volatile("msr ttbr0_el2, %0"
        :: "r"((uint64_t)(uintptr_t)cm4_l1_table));
    /* TCR_EL2: T0SZ=32 (32-bit VA), 4KB granule, WB cacheable inner-shareable
     * table walks, 36-bit PA. Bits 31 and 23 are RES1 for TCR_EL2 (E2H==0) and
     * must be written as 1. */
    __asm__ volatile("msr tcr_el2, %0"
        :: "r"(0x0000000000013520UL | (1UL << 31) | (1UL << 23)));
    __asm__ volatile("isb");
    __asm__ volatile("tlbi alle2");
    __asm__ volatile("dsb sy");
    /* Invalidate the D-cache (and I-cache) before enabling them, so no stale
     * lines left by an earlier boot stage surface once caching is on. */
    aarch64_dcache_maint(0);
    __asm__ volatile("ic iallu");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
    /* SCTLR_EL2: enable MMU (M), data cache (C), instruction cache (I). */
    __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    sctlr |= (1UL << 0) | (1UL << 2) | (1UL << 12);
    __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
}

/* Tear down the MMU/caches before boot handoff: clean the freshly-copied app
 * out of the D-cache to memory, disable the MMU and caches, and invalidate the
 * I-cache/TLB. Returns the CPU to the MMU-off state the application (and the
 * ARM64 Linux boot protocol) expects. */
void cm4_mmu_disable(void)
{
    unsigned long sctlr;

    cm4_require_el2();
    /* Flush the loaded app to DRAM WHILE the D-cache is still enabled, then
     * disable M/C/I together. The "textbook" order (clear SCTLR.C first, then
     * flush) is UNSAFE here: aarch64_dcache_maint() and this function use the stack,
     * and once C is cleared, stack reads bypass the cache and return stale DRAM
     * (the dirty lines - including this function's spilled return address - are
     * not yet written back), so the function would return to garbage. That
     * order is only safe in a pure-asm flush with no stack use (U-Boot). What
     * must be coherent for the application is the loaded image, and it is fully
     * flushed here with caches on. */
    aarch64_dcache_maint(1); /* clean+invalidate: flush the app to memory */
    __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    sctlr &= ~((1UL << 0) | (1UL << 2) | (1UL << 12)); /* clear M, C, I */
    __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
    __asm__ volatile("ic iallu");
    __asm__ volatile("tlbi alle2");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}
#endif /* CM4_USE_MMU */

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
