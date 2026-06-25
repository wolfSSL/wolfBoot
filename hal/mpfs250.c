/* mpfs250.c
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

/* Microchip PolarFire SoC MPFS250T HAL for wolfBoot */
/* Supports:
 *   RISC-V 64-bit architecture
 *   External flash operations
 *   UART communication
 *   System initialization
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "target.h"

#include "mpfs250.h"
#include "riscv.h"
#include "image.h"
#ifndef ARCH_RISCV64
#   error "wolfBoot mpfs250 HAL: wrong architecture selected. Please compile with ARCH=RISCV64."
#endif

#include "printf.h"
#include "loader.h"
#include "hal.h"
#include "gpt.h"
#include "fdt.h"


#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "sdhci.h"

/* Forward declaration of SDHCI IRQ handler */
extern void sdhci_irq_handler(void);
#endif

/* Video Kit DDR/Clock configuration is included in mpfs250.h */

/* ------------------------------------------------------------------------
 * File-scope globals
 * ---------------------------------------------------------------------- */
/* APB (PCLK) frequency for UART baud divisors.  Starts at the mode's
 * compile-time value (40 MHz E51 reset clock in M-mode, 150 MHz under
 * HSS) and is updated when M-mode wolfBoot raises the MSS PLL. */
uint32_t mpfs_apb_clk_hz = MSS_APB_AHB_CLK;

#ifdef WOLFBOOT_RISCV_MMODE
/* CPU frequency in MHz for mcycle-based udelay(); seeded at the E51 reset
 * clock and bumped to the PLL rate after mss_pll_init() (hal/mpfs250_ddr.c).
 * A wrong value here skews timing-sensitive paths (e.g. SD power-up). */
uint32_t mpfs_cpu_freq_mhz = MPFS_CPU_FREQ_RESET_MHZ;

/* Saved boot ROM watchdog values, restored in hal_prepare_boot(). */
static uint32_t mpfs_wdt_default_mvrp = 0;
static uint32_t mpfs_wdt_default_ctrl = 0;

/* Snapshots captured at hal_init entry, printed after uart_init;
 * RESET_SR shows the cause of the most-recent reset. */
static uint32_t mpfs_boot_wdt_snap[6];
static uint32_t mpfs_boot_reset_sr_snap;

/* Configure L2 cache: enable ways 0,1,3 (0x0B) and set way masks for all masters */
static void mpfs_config_l2_cache(void)
{
    L2_WAY_ENABLE = 0x0B;  /* WayEnable INDEX (not a mask): ways 0..11 are
                            * cache-capable; the masters' way masks (0xFF)
                            * restrict cache fills to ways 0-7, leaving
                            * 8-11 as the scratchpad carve-out.  Matches
                            * HSS/Libero (LIBERO_SETTING_WAY_ENABLE=0xB). */
    SYSREG_L2_SHUTDOWN_CR = 0;
    L2_WAY_MASK_DMA        = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT0 = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT1 = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT2 = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT3 = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_E51_DCACHE  = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_E51_ICACHE  = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_1_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_1_ICACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_2_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_2_ICACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_3_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_3_ICACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_4_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_4_ICACHE = L2_WAY_MASK_CACHE_ONLY;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/* mcycle-based microsecond delay.  MTIME is not running in M-mode without
 * HSS, but mcycle ticks at the CPU clock rate and is monotonic. */
__attribute__((noinline)) void udelay(uint32_t us)
{
    uint64_t start, now, target;
    __asm__ volatile("rdcycle %0" : "=r"(start));
    target = (uint64_t)us * (uint64_t)mpfs_cpu_freq_mhz;
    do {
        __asm__ volatile("rdcycle %0" : "=r"(now));
    } while ((now - start) < target);
}

#endif /* WOLFBOOT_RISCV_MMODE */


/* Multi-Hart Support */
#ifdef WOLFBOOT_RISCV_MMODE

extern uint8_t _main_hart_hls; /* linker-provided address symbol; typed as uint8_t to avoid size confusion */

/* Watchdog timeout configuration.
 *   WATCHDOG=0 (default): WDT disabled in hal_init() then restored to boot
 *                         ROM defaults in hal_prepare_boot() before do_boot.
 *   WATCHDOG=1: WDT kept enabled with WATCHDOG_TIMEOUT_MS during wolfBoot.
 * Verify is bounded at ~5s; default 30s leaves ample headroom and avoids
 * the need to pet the WDT during the long ECDSA verify call. */
#ifdef WATCHDOG
#  ifndef WATCHDOG_TIMEOUT_MS
#    define WATCHDOG_TIMEOUT_MS  30000U
#  endif
/* MPFS MSS WDT clock is AHB / 256 ~= 150 MHz / 256 ~= 585 kHz at S-mode rate
 * but ~80 MHz / 256 ~= 312 kHz on E51 reset clocks. Use a conservative
 * 300 ticks/ms; the actual rate may be a bit higher but a slightly longer
 * timeout is safe. Caller can override WATCHDOG_TIMEOUT_MS at build time. */
#  define WATCHDOG_TIMEOUT_TICKS ((WATCHDOG_TIMEOUT_MS) * 300U)
#endif

/* CLINT MSIP register for IPI delivery */
#define CLINT_MSIP_REG(hart) (*(volatile uint32_t*)(CLINT_BASE + (hart) * 4))
/* CLINT machine-timer comparator (per hart) and MTIME counter */
#define CLINT_MTIMECMP_REG(hart) \
    (*(volatile uint64_t*)(CLINT_BASE + 0x4000UL + (hart) * 8UL))
#define CLINT_MTIME_REG (*(volatile uint64_t*)(CLINT_BASE + 0xBFF8UL))

/* Signal secondary harts that E51 (main hart) is ready. */
static void mpfs_signal_main_hart_started(void)
{
    HLS_DATA* hls = (HLS_DATA*)&_main_hart_hls;
    hls->in_wfi_indicator = HLS_MAIN_HART_STARTED;
    hls->my_hart_id = MPFS_FIRST_HART;
    /* The eNVM secondary-hart gate polls the DTIM copy of this flag, not
     * the L2-scratch HLS above: a cacheable store to the scratchpad can
     * be lost on dirty-line eviction (layout-dependent), which parked
     * the secondaries until the kernel's hart_start IPI -- too late for
     * its 1s online window.  DTIM is uncached and visible to all harts. */
    *(volatile uint32_t *)MPFS_DTIM_MAIN_STARTED_ADDR =
        (uint32_t)HLS_MAIN_HART_STARTED;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
/* Per-hart S-mode start mailboxes, written by the E51 (boot hart release)
 * or by the SBI HSM hart_start backend (on the calling U54), and consumed
 * by the target hart's park loop in secondary_hart_entry().
 *
 * These live in the E51 DTIM, NOT in L2-scratch BSS: cacheable stores to
 * the scratchpad can be lost on cache-line eviction, so cross-hart
 * mailboxes written by a U54 would silently vanish (observed: the SBI
 * HSM hart-state writes never became visible).  The DTIM is small,
 * uncached and coherent for every hart.  The SBI shared state occupies
 * DTIM+0x000 (see src/riscv_sbi.c); the mailboxes sit at +0x100. */
typedef struct {
    volatile uint32_t marker;  /* MPFS_KERNEL_HANDOFF_MARKER when valid */
    volatile uint64_t entry;   /* S-mode entry point */
    volatile uint64_t opaque;  /* a1 at entry (dtb for the boot hart) */
} mpfs_kernel_handoff_t;

#define MPFS_KERNEL_HANDOFF_MARKER  0x4C4E5858UL  /* "LNXX" */

#define mpfs_kernel_handoff \
    ((mpfs_kernel_handoff_t *)(0x01000000UL + 0x100UL))

/* Provided by src/boot_riscv.c. */
extern void riscv_mmode_to_smode(unsigned long entry, unsigned long hartid,
                                 unsigned long dtb) __attribute__((noreturn));
#endif /* MPFS_DDR_INIT && WOLFBOOT_MMODE_SMODE_BOOT */

/* Secondary hart (U54) entry: jump into the waiting Linux kernel (when a
 * hand-off context has been staged for us) or park in WFI waiting for an
 * SBI/Linux IPI.
 *
 * Keep this path FAST and free of UART access: the secondaries reach it
 * via the kernel's HSM hart_start IPI, inside the kernel's 1s online
 * window.  A per-hart UART banner here once spent multiple seconds
 * spinning on LSR (layout-dependent), making harts miss that window. */
void secondary_hart_entry(unsigned long hartid, HLS_DATA* hls)
{
    (void)hls;

    while (1) {
#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
        /* Check the hand-off context BEFORE sleeping: the release IPI was
         * already consumed (MSIP cleared) by the eNVM wake path, so a
         * wfi-first loop sleeps through an already-staged hand-off.  The
         * old wfi-first order only appeared to work because mtimecmp's
         * reset value of 0 left MTIP permanently pending, making the wfi
         * fall through; parking the comparators exposed it. */
        if (hartid < (unsigned long)MPFS_NUM_HARTS &&
            mpfs_kernel_handoff[hartid].marker
                == MPFS_KERNEL_HANDOFF_MARKER) {
            unsigned long kentry;
            unsigned long opq;
            /* Acquire fence: pair with the writer's release fence so we
             * are guaranteed to observe entry / opaque after seeing
             * marker.  Without this, RISC-V's relaxed memory model
             * permits the reader to use stale field values cached before
             * marker was published. */
            __asm__ volatile("fence r,rw" ::: "memory");
            kentry = (unsigned long)mpfs_kernel_handoff[hartid].entry;
            opq = (unsigned long)mpfs_kernel_handoff[hartid].opaque;
            riscv_mmode_to_smode(kentry, hartid, opq);
            /* never returns */
        }
        /* Sleep until the next IPI (e.g. a future SBI HSM hart_start),
         * then clear it and re-check the mailbox. */
        __asm__ volatile("wfi");
        CLINT_MSIP_REG(hartid) = 0;
        __asm__ volatile("fence iorw, iorw" ::: "memory");
#else
        __asm__ volatile("wfi");
#endif /* MPFS_DDR_INIT && WOLFBOOT_MMODE_SMODE_BOOT */
    }
}

#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
/* Enable the CLINT MTIME counter via the SYSREG RTC/time-base clock divider
 * (HSS set_RTC_divisor equivalent).  Without this MTIME never advances and an
 * S-mode OS has no time source for its scheduler tick. */
static void mpfs_enable_mtime(void)
{
    volatile uint32_t *rtc_cr = (volatile uint32_t *)(SYSREG_BASE + 0x0CUL);
    uint32_t div;
#ifdef LIBERO_SETTING_MSS_RTC_CLOCK_CR
    div = (uint32_t)LIBERO_SETTING_MSS_RTC_CLOCK_CR & 0xFFFU;
#else
    div = 125U; /* 125 MHz reference / 1 MHz RTC */
#endif
    *rtc_cr = div;             /* program divider (bits 11:0), enable off */
    *rtc_cr |= (1UL << 16);    /* enable RTC/time-base clock */
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/* SBI HSM hart_start backend (called from src/riscv_sbi.c on the boot
 * hart): stage the target hart's start mailbox and ring its MSIP; the
 * parked hart consumes it in secondary_hart_entry() and enters S-mode at
 * saddr with a0=hartid, a1=opaque. */
int sbi_hal_hart_start(unsigned long hartid, unsigned long saddr,
    unsigned long opaque)
{
    if (hartid < (unsigned long)MPFS_FIRST_U54_HART ||
        hartid > (unsigned long)MPFS_LAST_U54_HART) {
        return -1;
    }
    mpfs_kernel_handoff[hartid].entry = (uint64_t)saddr;
    mpfs_kernel_handoff[hartid].opaque = (uint64_t)opaque;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    mpfs_kernel_handoff[hartid].marker = MPFS_KERNEL_HANDOFF_MARKER;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    CLINT_MSIP_REG(hartid) = 0x01;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    return 0;
}

/* Override of the weak hal_smode_boot in src/boot_riscv.c. The E51 cannot
 * run Linux (cpu@0 is marked disabled in the Yocto MPFS DTB), so instead of
 * dropping to S-mode on hart 0 we stage the kernel/DTB pointers, IPI a U54,
 * and park hart 0 in M-mode. The released U54 picks up the context from
 * its WFI loop in secondary_hart_entry() and performs the actual M->S jump. */
void __attribute__((noreturn))
hal_smode_boot(unsigned long entry, unsigned long hartid, unsigned long dtb)
{
    static const unsigned long park_wdt_bases[5] = {
        MSS_WDT_E51_BASE, MSS_WDT_U54_1_BASE, MSS_WDT_U54_2_BASE,
        MSS_WDT_U54_3_BASE, MSS_WDT_U54_4_BASE
    };
    unsigned int w;

    (void)hartid;  /* the calling E51 hart is not the kernel boot hart */

    /* Bring up the MTIME time base before releasing the U54 into S-mode. */
    mpfs_enable_mtime();

    /* Enable the clocks and release the soft resets of MMUART1-4 for the
     * OS: the kernel's mpfs clock driver gates SUBBLK_CLOCK_CR but does
     * not release peripheral soft resets (HSS normally does), so without
     * this the serial console (MMUART1) stays dead.  Done here on the
     * E51, single-threaded, so no SYSREG read-modify-write races. */
    SYSREG_SUBBLK_CLOCK_CR |= (MSS_PERIPH_MMUART0 << 1) |
                              (MSS_PERIPH_MMUART0 << 2) |
                              (MSS_PERIPH_MMUART0 << 3) |
                              (MSS_PERIPH_MMUART0 << 4);
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    SYSREG_SOFT_RESET_CR &= ~((MSS_PERIPH_MMUART0 << 1) |
                              (MSS_PERIPH_MMUART0 << 2) |
                              (MSS_PERIPH_MMUART0 << 3) |
                              (MSS_PERIPH_MMUART0 << 4));
    __asm__ volatile("fence iorw, iorw" ::: "memory");

    mpfs_kernel_handoff[MPFS_FIRST_U54_HART].entry = (uint64_t)entry;
    mpfs_kernel_handoff[MPFS_FIRST_U54_HART].opaque = (uint64_t)dtb;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    mpfs_kernel_handoff[MPFS_FIRST_U54_HART].marker =
        MPFS_KERNEL_HANDOFF_MARKER;
    __asm__ volatile("fence iorw, iorw" ::: "memory");

    wolfBoot_printf("Releasing hart %d into S-mode at 0x%lx (dtb=0x%lx)\n",
                    MPFS_FIRST_U54_HART, entry, dtb);

    CLINT_MSIP_REG(MPFS_FIRST_U54_HART) = 0x01;
    __asm__ volatile("fence iorw, iorw" ::: "memory");

    /* Park the E51 as the platform monitor (HSS's watchdog-service role):
     * doze on the machine timer and pet all watchdogs every few seconds.
     * The MSS watchdogs always count and reset the chip on timeout
     * (~28 s at the boot-time settings), so the parked E51 must keep
     * them refreshed while the OS boots and runs.
     *
     * Wake only on the machine timer; mstatus.MIE stays clear so the
     * pending timer wakes WFI without vectoring into the trap path.
     * Pet ALL watchdogs (HSS's WDog-service role): the MSS WDTs always
     * count and RESET the chip on timeout even with CONTROL=0, and the
     * OS watchdog driver is disabled in the dtb fixup (when it owned
     * them, its refresh-forbidden window made our blind refreshes trip
     * it).  With MVRP at maximum a refresh is always permitted. */
    __asm__ volatile("csrw mie, %0" :: "r"(0x80UL)); /* MTIE */
    while (1) {
        for (w = 0; w < 5U; w++) {
            MSS_WDT_REFRESH(park_wdt_bases[w]) = 0xDEADC0DEU;
        }
        CLINT_MTIMECMP_REG(MPFS_FIRST_HART) =
            CLINT_MTIME_REG + (5UL * RTC_CLOCK_FREQ);
        __asm__ volatile("wfi");
    }
    __builtin_unreachable();
}
#endif /* MPFS_DDR_INIT && WOLFBOOT_MMODE_SMODE_BOOT */

#endif /* WOLFBOOT_RISCV_MMODE */

#if defined(EXT_FLASH) && defined(TEST_EXT_FLASH) && defined(__WOLFBOOT)
static int test_ext_flash(void);
#endif
#if defined(EXT_FLASH) && defined(UART_QSPI_PROGRAM) && defined(__WOLFBOOT)
static void qspi_uart_program(void);
#endif

void hal_init(void)
{
#ifdef WOLFBOOT_RISCV_MMODE
    volatile uint32_t *wdt_e51 = (volatile uint32_t *)0x20001000UL;
    volatile uint32_t *sysreg_reset_sr = (volatile uint32_t *)0x20002020UL;
    int h;
#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
    volatile uint32_t *dtim = (volatile uint32_t *)0x01000000UL;
    unsigned int k;
#endif
#ifndef WATCHDOG
    static const unsigned long wdt_bases[5] = {
        MSS_WDT_E51_BASE, MSS_WDT_U54_1_BASE, MSS_WDT_U54_2_BASE,
        MSS_WDT_U54_3_BASE, MSS_WDT_U54_4_BASE
    };
    unsigned int w;
#endif
#ifdef MPFS_DDR_INIT
    unsigned int outer_retry;
    int ddr_ok = 0;
#endif

    /* Park every hart's machine-timer comparator at maximum.  CLINT MTIME
     * is 0 (the RTC time base is not running yet) and mtimecmp resets to 0,
     * so MTIP is pending on every hart out of reset.  A pending interrupt
     * makes WFI return immediately, so the parked secondary harts' eNVM
     * wait loop SPINS continuously (fetching from eNVM for the entire
     * boot) instead of sleeping.  Parking the comparators clears MTIP so
     * WFI really waits. */
    for (h = 0; h < MPFS_NUM_HARTS; h++) {
        CLINT_MTIMECMP_REG(h) = ~(uint64_t)0;
    }
    __asm__ volatile("fence iorw, iorw" ::: "memory");

#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
    /* Clear the DTIM-resident cross-hart state (start mailboxes + SBI
     * shared block): DTIM content is undefined at power-on. */
    for (k = 0; k < (0x200U / sizeof(uint32_t)); k++) {
        dtim[k] = 0;
    }
    __asm__ volatile("fence iorw, iorw" ::: "memory");
#endif

    /* Capture boot ROM WDT defaults for restoration in hal_prepare_boot() */
    mpfs_wdt_default_mvrp = MSS_WDT_MVRP(MSS_WDT_E51_BASE);
    mpfs_wdt_default_ctrl = MSS_WDT_CONTROL(MSS_WDT_E51_BASE);
    /* Snapshot boot-ROM WDT state and SYSREG RESET_SR (reset status
     * cause) so we can print them AFTER uart_init.  RESET_SR is W1C --
     * we clear after reading. */
    mpfs_boot_wdt_snap[0] = wdt_e51[0];
    mpfs_boot_wdt_snap[1] = wdt_e51[1];
    mpfs_boot_wdt_snap[2] = wdt_e51[2];
    mpfs_boot_wdt_snap[3] = wdt_e51[3];
    mpfs_boot_wdt_snap[4] = wdt_e51[4];
    mpfs_boot_wdt_snap[5] = wdt_e51[5];
    mpfs_boot_reset_sr_snap = *sysreg_reset_sr;
    *sysreg_reset_sr = mpfs_boot_reset_sr_snap;  /* W1C: clear seen bits */

#ifndef WATCHDOG
    /* WATCHDOG=0 (default): disable WDT for the duration of wolfBoot; it is
     * re-enabled in hal_prepare_boot() before do_boot.  The MPFS MSS WDOG
     * cannot be disabled outright (it always counts), so clear DEVRST
     * (bit 5) -- timeout then raises an NMI instead of a chip reset -- set
     * TIME/MVRP to max for the longest window, and refresh with the magic
     * key (a refresh inside the triggered window would arm a reset). */
    for (w = 0; w < 5; w++) {
        MSS_WDT_REFRESH(wdt_bases[w]) = 0xDEADC0DEU;
        MSS_WDT_TIME(wdt_bases[w])    = 0x00FFFFFFUL;
        MSS_WDT_MVRP(wdt_bases[w])    = 0x00FFFFFFUL;
        MSS_WDT_CONTROL(wdt_bases[w]) = 0;
        MSS_WDT_REFRESH(wdt_bases[w]) = 0xDEADC0DEU;
    }
#else
    /* WATCHDOG=1: keep WDT enabled with a generous timeout for crypto.
     * Verify is bounded at ~5s; configure a much larger timeout so we
     * never have to pet the WDT during ECDSA verify. */
    MSS_WDT_REFRESH(MSS_WDT_E51_BASE) = 0xDEADC0DEU;
    MSS_WDT_MVRP(MSS_WDT_E51_BASE) = WATCHDOG_TIMEOUT_TICKS;
    MSS_WDT_CONTROL(MSS_WDT_E51_BASE) |= MSS_WDT_CTRL_ENABLE;
#endif

    mpfs_config_l2_cache();
    mpfs_signal_main_hart_started();
#endif

#ifdef DEBUG_UART
    SYSREG_SUBBLK_CLOCK_CR |= (MSS_PERIPH_MMUART0 << DEBUG_UART_PORT);
    SYSREG_SOFT_RESET_CR &= ~(MSS_PERIPH_MMUART0 << DEBUG_UART_PORT);
    uart_init();
#endif

#ifdef WOLFBOOT_REPRODUCIBLE_BUILD
    wolfBoot_printf("wolfBoot Version: %s\n", LIBWOLFBOOT_VERSION_STRING);
#else
    wolfBoot_printf("wolfBoot Version: %s (%s %s)\n",
        LIBWOLFBOOT_VERSION_STRING, __DATE__, __TIME__);
#endif

#ifdef WOLFBOOT_RISCV_MMODE
    wolfBoot_printf("Running on E51 (hart 0) in M-mode\n");
    DBG_DDR("Boot WDT_E51: REFRESH=%x CTRL=%x STATUS=%x TIME=%x MVRP=%x TRIG=%x\n",
        mpfs_boot_wdt_snap[0], mpfs_boot_wdt_snap[1], mpfs_boot_wdt_snap[2],
        mpfs_boot_wdt_snap[3], mpfs_boot_wdt_snap[4], mpfs_boot_wdt_snap[5]);
    wolfBoot_printf("Boot RESET_SR: %x (bit0=PERIPH bit1=MSS bit2=CPU bit3=DBG "
        "bit4=FABRIC bit5=WDOG bit6=GPIO bit7=BUS bit8=SOFT)\n",
        mpfs_boot_reset_sr_snap);

#ifdef MPFS_DDR_INIT
    /* Bring up LPDDR4 before any DDR-resident operations.
     *
     * Outer retry loop: each call to mpfs_ddr_init() does a SYSREG DDRC
     * soft-reset pulse, which clears the MTC engine state.  If the
     * inner retry inside mpfs_ddr_init() exhausts (typically because
     * MTC wedged after the first failure), come back here for a full
     * controller re-init.  Empirical: per-attempt failure rate ~30%, so
     * MPFS_DDR_MAX_OUTER_RETRY (6) outer attempts cover ~99.9% of boots. */
    for (outer_retry = 0; outer_retry < MPFS_DDR_MAX_OUTER_RETRY;
         outer_retry++) {
        if (outer_retry > 0) {
            wolfBoot_printf(
                "DDR: Outer retry %u/%u (full DDRC re-init)\n",
                outer_retry, MPFS_DDR_MAX_OUTER_RETRY);
        }
        if (mpfs_ddr_init(outer_retry) == 0) {
            ddr_ok = 1;
            break;
        }
    }
    if (!ddr_ok) {
        /* No safe path forward: WOLFBOOT_LOAD_ADDRESS is in DDR, so a
         * subsequent disk-load would write to a non-functional
         * controller and hang silently inside the AXI master.  Halt
         * with a clear message so the operator can power-cycle. */
        wolfBoot_printf(
            "DDR: Init FAILED after %u outer retries -- halting.\n"
            "DDR: Power-cycle the board and retry.\n",
            MPFS_DDR_MAX_OUTER_RETRY);
        while (1) {
            /* spin */
        }
    }
#endif
#endif

#ifdef EXT_FLASH
    if (qspi_init() != 0) {
        wolfBoot_printf("QSPI: Init failed\n");
    } else {
#if defined(TEST_EXT_FLASH) && defined(__WOLFBOOT)
        test_ext_flash();
#endif
#if defined(UART_QSPI_PROGRAM) && defined(__WOLFBOOT)
        qspi_uart_program();
#endif
    }
#endif /* EXT_FLASH */
}

/* System Controller Mailbox */

static int mpfs_scb_mailbox_busy(void)
{
    return (SCBCTRL_REG(SERVICES_SR_OFFSET) & SERVICES_SR_BUSY_MASK);
}

/* Read 16-byte device serial number via SCB system service (opcode 0x00). */
int mpfs_read_serial_number(uint8_t *serial)
{
    uint32_t cmd, status;
    int i, timeout;

    if (serial == NULL) {
        return -1;
    }

    /* Check if mailbox is busy */
    if (mpfs_scb_mailbox_busy()) {
        wolfBoot_printf("SCB mailbox busy\n");
        return -2;
    }

    /* Send serial number request command (opcode 0x00)
     * Command format: [31:16] = opcode, [0] = request bit */
    cmd = (SYS_SERV_CMD_SERIAL_NUMBER << SERVICES_CR_COMMAND_SHIFT) |
          SERVICES_CR_REQ_MASK;
    SCBCTRL_REG(SERVICES_CR_OFFSET) = cmd;

    /* Wait for request bit to clear (command accepted) */
    timeout = MPFS_SCB_TIMEOUT;
    while ((SCBCTRL_REG(SERVICES_CR_OFFSET) & SERVICES_CR_REQ_MASK) && timeout > 0) {
        timeout--;
    }
    if (timeout == 0) {
        wolfBoot_printf("SCB mailbox request timeout\n");
        return -3;
    }

    /* Wait for busy bit to clear (command completed) */
    timeout = MPFS_SCB_TIMEOUT;
    while (mpfs_scb_mailbox_busy() && timeout > 0) {
        timeout--;
    }
    if (timeout == 0) {
        wolfBoot_printf("SCB mailbox busy timeout\n");
        return -4;
    }

    /* Check status (upper 16 bits of status register) */
    status = (SCBCTRL_REG(SERVICES_SR_OFFSET) >> SERVICES_SR_STATUS_SHIFT) & 0xFFFF;
    if (status != 0) {
        wolfBoot_printf("SCB mailbox error: 0x%x\n", status);
        return -5;
    }

    /* Read serial number from mailbox RAM (16 bytes) */
    for (i = 0; i < DEVICE_SERIAL_NUMBER_SIZE; i++) {
        serial[i] = SCBMBOX_BYTE(i);
    }

    return 0;
}

/* Linux kernel command line arguments */
#ifndef LINUX_BOOTARGS
#ifndef LINUX_BOOTARGS_ROOT
/* wolfBoot SD layout (tools/scripts/program-sdcard.sh): p1=boot FIT,
 * p2=update, p3=rootfs. */
#define LINUX_BOOTARGS_ROOT "/dev/mmcblk0p3"
#endif

#define LINUX_BOOTARGS \
    "earlycon=sbi root="LINUX_BOOTARGS_ROOT" rootwait uio_pdrv_genirq.of_id=generic-uio"
#endif

/* Microchip OUI (Organizationally Unique Identifier) for MAC address */
#define MICROCHIP_OUI_0 0x00
#define MICROCHIP_OUI_1 0x04
#define MICROCHIP_OUI_2 0xA3

static int mpfs_dts_fixup_inplace(void* dts_addr)
{
    int off, ret;
    struct fdt_header *fdt = (struct fdt_header *)dts_addr;
    uint8_t device_serial_number[DEVICE_SERIAL_NUMBER_SIZE];
    uint8_t mac_addr[6];
#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
    /* Nodes disabled below.  Watchdogs: the MSS WDTs always count; the OS
     * driver arms them at probe but nothing pings them (no userspace
     * watchdog daemon in the default image), so the system would reset
     * ~28 s into boot.  Disabled, they only latch a harmless tripped
     * status and the parked E51 monitor keeps them refreshed.  Re-enable
     * when an OS-side petting story exists. */
    static const char *const cpu_off[] = {
        "watchdog@20001000", "watchdog@20101000", "watchdog@20103000",
        "watchdog@20105000", "watchdog@20107000" };
    unsigned int i;
#endif

    /* Verify FDT header */
    ret = fdt_check_header(dts_addr);
    if (ret != 0) {
        wolfBoot_printf("FDT: Invalid header! %d\n", ret);
        return ret;
    }

    wolfBoot_printf("FDT: Version %d, Size %d\n",
        fdt_version(fdt), fdt_totalsize(fdt));

    /* Expand total size to allow adding/modifying properties.
     * Sizing comes from WOLFBOOT_FDT_FIXUP_HEADROOM in include/fdt.h. */
    fdt_set_totalsize(fdt,
        fdt_totalsize(fdt) + WOLFBOOT_FDT_FIXUP_HEADROOM);

    /* Find /chosen node */
    off = fdt_find_node_offset(fdt, -1, "chosen");
    if (off < 0) {
        /* Create /chosen node if it doesn't exist */
        off = fdt_add_subnode(fdt, 0, "chosen");
    }

    if (off >= 0) {
        /* Set bootargs property */
        fdt_fixup_str(fdt, off, "chosen", "bootargs", LINUX_BOOTARGS);
    }

#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
    /* Disable the MSS watchdog dtb nodes BEFORE the serial-number read: this
     * does not depend on the serial number and MUST run even if the SC serial
     * read below fails.  Otherwise the OS watchdog driver would arm them at
     * probe while the parked E51 monitor (hal_smode_boot) also refreshes all
     * five every cycle -- a conflict that can trip a watchdog-driven reset.
     *
     * Do NOT override the stock /memory nodes: the 32-bit cached DDR window at
     * 0x80000000 is only 1 GB wide (0xC0000000 up is the NON-CACHED alias onto
     * the same DDR) and the stock Video Kit DTB already describes the full
     * 2 GB; an earlier fixup that forced memory@80000000 to 2 GB made the
     * kernel treat the alias window as extra RAM (self-aliasing corruption;
     * boot hung at the first deep memblock allocation).
     *
     * cpu@2..cpu@4 stay ENABLED: the SBI HSM hart_start backend releases the
     * parked harts on the kernel's request (SMP).  cpu@0 (E51) is already
     * disabled in the Yocto DTB; cpu@1 stays enabled so Linux boots on it. */
    for (i = 0; i < sizeof(cpu_off) / sizeof(cpu_off[0]); i++) {
        off = fdt_find_node_offset(fdt, -1, cpu_off[i]);
        if (off >= 0) {
            ret = fdt_fixup_str(fdt, off, cpu_off[i], "status",
                                "disabled");
            if (ret != 0) {
                wolfBoot_printf("FDT: Failed to disable %s (%d)\n",
                                cpu_off[i], ret);
            }
        }
        else {
            wolfBoot_printf("FDT: %s not found\n", cpu_off[i]);
        }
    }
#endif /* MPFS_DDR_INIT && WOLFBOOT_MMODE_SMODE_BOOT */

    /* Read device serial number from system controller */
    ret = mpfs_read_serial_number(device_serial_number);
    if (ret != 0) {
        wolfBoot_printf("FDT: Failed to read serial number (%d)\n", ret);
        /* Continue without setting MAC addresses */
        return 0;
    }

    wolfBoot_printf("FDT: Device serial: %02x%02x%02x%02x-%02x%02x%02x%02x-"
                    "%02x%02x%02x%02x-%02x%02x%02x%02x\n",
        device_serial_number[15], device_serial_number[14],
        device_serial_number[13], device_serial_number[12],
        device_serial_number[11], device_serial_number[10],
        device_serial_number[9],  device_serial_number[8],
        device_serial_number[7],  device_serial_number[6],
        device_serial_number[5],  device_serial_number[4],
        device_serial_number[3],  device_serial_number[2],
        device_serial_number[1],  device_serial_number[0]);

    /* Build MAC address: Microchip OUI + lower 3 bytes of serial number
     * Format: {0x00, 0x04, 0xA3, serial[2], serial[1], serial[0]} */
    mac_addr[0] = MICROCHIP_OUI_0;
    mac_addr[1] = MICROCHIP_OUI_1;
    mac_addr[2] = MICROCHIP_OUI_2;
    mac_addr[3] = device_serial_number[2];
    mac_addr[4] = device_serial_number[1];
    mac_addr[5] = device_serial_number[0];

    wolfBoot_printf("FDT: MAC0 = %02x:%02x:%02x:%02x:%02x:%02x\n",
        mac_addr[0], mac_addr[1], mac_addr[2],
        mac_addr[3], mac_addr[4], mac_addr[5]);

    /* Set local-mac-address for ethernet@20110000 (mac0) */
    off = fdt_find_node_offset(fdt, -1, "ethernet@20110000");
    if (off >= 0) {
        ret = fdt_setprop(fdt, off, "local-mac-address", mac_addr, 6);
        if (ret != 0) {
            wolfBoot_printf("FDT: Failed to set mac0 address (%d)\n", ret);
        }
    }
    else {
        wolfBoot_printf("FDT: ethernet@20110000 not found\n");
    }

    /* Set local-mac-address for ethernet@20112000 (mac1)
     * Use MAC address + 1 for the second interface */
    mac_addr[5] = device_serial_number[0] + 1;

    wolfBoot_printf("FDT: MAC1 = %02x:%02x:%02x:%02x:%02x:%02x\n",
        mac_addr[0], mac_addr[1], mac_addr[2],
        mac_addr[3], mac_addr[4], mac_addr[5]);

    off = fdt_find_node_offset(fdt, -1, "ethernet@20112000");
    if (off >= 0) {
        ret = fdt_setprop(fdt, off, "local-mac-address", mac_addr, 6);
        if (ret != 0) {
            wolfBoot_printf("FDT: Failed to set mac1 address (%d)\n", ret);
        }
    }
    else {
        wolfBoot_printf("FDT: ethernet@20112000 not found\n");
    }

    return 0;
}

#if defined(WOLFBOOT_RISCV_MMODE) && defined(MPFS_DDR_INIT)
/* FIT subimage copy via PDMA (overrides the weak default in src/fdt.c).
 * CPU writes to DDR do not land on this board, so route kernel/dtb copies
 * through the PDMA master.  A DDR source is read via its non-cached alias so
 * PDMA sees real DDR; mpfs_pdma_memcpy remaps the dst 0x8x->0xCx and flushes
 * L2.  Chunked + WDT-petted for kernel-sized copies. */
int wolfBoot_fit_memcpy(void *dst, const void *src, uint32_t len)
{
    uintptr_t d = (uintptr_t)dst;
    uintptr_t s = (uintptr_t)src;
    volatile const uint8_t *ncd;
    const uint8_t *ncs;
    uint32_t off = 0;
    uint32_t chunk;
    uint32_t k;
    int retry;
    int mism;
    int rc = 0;

    if ((s & 0xF0000000UL) == 0x80000000UL) {
        s |= 0x40000000UL; /* non-cached source alias */
    }
    while (off < len) {
        chunk = len - off;
        if (chunk > (1024U * 1024U)) {
            chunk = 1024U * 1024U;
        }
        /* mpfs_pdma_memcpy always returns 0, so the read-back verify below is
         * the authoritative success check for this chunk.  The PDMA->DDR write
         * intermittently drops a block, so re-PDMA on a mismatch (same pattern
         * as sdhci_platform_block_copy).  A DDR destination (0x8xxxxxxx) is
         * read back through its non-cached alias (| 0x40000000) so we compare
         * what actually landed in DDR, not stale L2; this makes the caller's
         * fail-closed rc real for the signature-uncovered kernel/dtb copies.
         * A non-DDR destination (e.g. an L2 scratch buffer) lands directly, so
         * a single copy suffices. */
        mism = 1;
        for (retry = 0; retry < 8 && mism != 0; retry++) {
            (void)mpfs_pdma_memcpy((void *)(d + off),
                (const void *)(s + off), chunk);
            /* Refresh all five MSS watchdogs (they always count and reset the
             * chip and cannot be disabled) during the multi-MB kernel copy
             * and its read-back verify. */
            MSS_WDT_REFRESH(MSS_WDT_E51_BASE)   = 0xDEADC0DEU;
            MSS_WDT_REFRESH(MSS_WDT_U54_1_BASE) = 0xDEADC0DEU;
            MSS_WDT_REFRESH(MSS_WDT_U54_2_BASE) = 0xDEADC0DEU;
            MSS_WDT_REFRESH(MSS_WDT_U54_3_BASE) = 0xDEADC0DEU;
            MSS_WDT_REFRESH(MSS_WDT_U54_4_BASE) = 0xDEADC0DEU;
            if ((d & 0xF0000000UL) != 0x80000000UL) {
                mism = 0; /* non-DDR dst lands on the first copy */
                break;
            }
            __asm__ volatile("fence iorw,iorw" ::: "memory");
            ncd = (volatile const uint8_t *)((d + off) | 0x40000000UL);
            ncs = (const uint8_t *)(s + off);
            mism = 0;
            for (k = 0; k < chunk; k++) {
                if (ncd[k] != ncs[k]) {
                    mism = 1;
                    break;
                }
            }
        }
        if (mism != 0) {
            /* Copy could not be verified within the retry budget; remember the
             * failure so the caller fails closed rather than boot corrupt,
             * no-longer-signature-covered data. */
            rc = -1;
        }
        off += chunk;
    }
    return rc;
}

/* L2 round-trip wrapper around mpfs_dts_fixup_inplace().  The dtb lives in DDR
 * (WOLFBOOT_LOAD_DTS_ADDRESS) but CPU writes to DDR do not land here, so copy
 * it (non-cached read) into an L2 scratch buffer, run the FDT fixups there
 * (CPU L2 writes work), then PDMA the result back to DDR. */
int hal_dts_fixup(void* dts_addr)
{
    static uint8_t l2_dtb[64 * 1024] __attribute__((aligned(8)));
    const uint8_t *ddr_nc;
    uint32_t sz;
    int ret;

    if (dts_addr == NULL) {
        return -1;
    }
    ddr_nc = (const uint8_t *)((uintptr_t)dts_addr | 0x40000000UL);
    if (fdt_check_header((void *)ddr_nc) != 0) {
        wolfBoot_printf("FDT: invalid header at %p\n", dts_addr);
        return -1;
    }
    sz = (uint32_t)fdt_totalsize((void *)ddr_nc);
    /* Overflow-safe bound: compare without adding.  A near-UINT32_MAX
     * totalsize (fdt_check_header does not bound it) would make
     * sz + WOLFBOOT_FDT_FIXUP_HEADROOM wrap to a small value that passes the
     * check, after which memcpy(l2_dtb, ., sz) overruns the 64 KB buffer.
     * sizeof(l2_dtb) (64 KB) is always greater than the headroom. */
    if (sz > sizeof(l2_dtb) - WOLFBOOT_FDT_FIXUP_HEADROOM) {
        wolfBoot_printf("FDT: dtb too large for L2 fixup (%u > %u)\n",
            (unsigned)sz,
            (unsigned)(sizeof(l2_dtb) - WOLFBOOT_FDT_FIXUP_HEADROOM));
        return -1;
    }
    /* DDR (non-cached) -> L2 */
    memcpy(l2_dtb, ddr_nc, sz);
    /* fixup in the CPU-writable L2 buffer */
    ret = mpfs_dts_fixup_inplace(l2_dtb);
    /* L2 -> DDR via PDMA (expanded totalsize) */
    if (wolfBoot_fit_memcpy(dts_addr, l2_dtb,
            (uint32_t)fdt_totalsize(l2_dtb)) != 0) {
        wolfBoot_printf("FDT: dtb copy-back to DDR failed\n");
        return -1;
    }
    return ret;
}
#else
/* Without the M-mode DDR constraints the dtb buffer is CPU-writable, so
 * run the fixups directly in place (the original behavior, kept so
 * FDT-enabled non-DDR builds do not silently fall back to the weak
 * no-op hal_dts_fixup). */
int hal_dts_fixup(void* dts_addr)
{
    if (dts_addr == NULL) {
        return -1;
    }
    return mpfs_dts_fixup_inplace(dts_addr);
}
#endif /* WOLFBOOT_RISCV_MMODE && MPFS_DDR_INIT */

void hal_prepare_boot(void)
{
#ifdef WOLFBOOT_RISCV_MMODE
#ifndef WOLFBOOT_MMODE_SMODE_BOOT
    /* Restore boot ROM WDT defaults so the application sees a normal WDT.
     * Refresh first so the timer doesn't fire immediately after we apply
     * the new MVRP. Restore the original CONTROL value (including the
     * enable bit) rather than unconditionally enabling. */
    MSS_WDT_REFRESH(MSS_WDT_E51_BASE) = 0xDEADC0DEU;
    MSS_WDT_MVRP(MSS_WDT_E51_BASE) = mpfs_wdt_default_mvrp;
    MSS_WDT_CONTROL(MSS_WDT_E51_BASE) = mpfs_wdt_default_ctrl;
#else
    /* Booting an S-mode OS: keep the watchdogs in the safe state set in
     * hal_init (no device reset, maximum window) and give every hart's
     * watchdog one final refresh so the OS inherits a full window (the
     * OS watchdog driver hangs at probe if it finds an already-tripped
     * watchdog).  The OS watchdog nodes are disabled in the dtb, so after
     * hand-off the parked E51 monitor loop in hal_smode_boot refreshes all
     * five each cycle for the life of the OS. */
    MSS_WDT_REFRESH(MSS_WDT_E51_BASE) = 0xDEADC0DEU;
    MSS_WDT_REFRESH(MSS_WDT_U54_1_BASE) = 0xDEADC0DEU;
    MSS_WDT_REFRESH(MSS_WDT_U54_2_BASE) = 0xDEADC0DEU;
    MSS_WDT_REFRESH(MSS_WDT_U54_3_BASE) = 0xDEADC0DEU;
    MSS_WDT_REFRESH(MSS_WDT_U54_4_BASE) = 0xDEADC0DEU;

    /* Hand the OS a clean SD controller: wolfBoot just used it for the
     * image load, and the leftover state makes the OS driver's re-init
     * and tuning intermittently fail ("Waiting for root device").  Guarded
     * on the disk backend: an S-mode-boot config that loads the kernel from
     * a non-SDHCI source (e.g. QSPI) pulls in no sdhci.h declaration. */
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
    sdhci_shutdown();
#endif
#endif
#endif
}

void RAMFUNCTION hal_flash_unlock(void)
{

}

void RAMFUNCTION hal_flash_lock(void)
{

}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}


/* Wait for SCB register bits to clear, with timeout */
static int mpfs_scb_wait_clear(uint32_t reg_offset, uint32_t mask,
    uint32_t timeout)
{
    while ((SCBCTRL_REG(reg_offset) & mask) && --timeout)
        ;
    return (timeout == 0) ? -1 : 0;
}

#ifdef EXT_FLASH
/* ==========================================================================
 * QSPI Flash Controller Implementation
 *
 * Both MSS QSPI (0x21000000) and SC QSPI (0x37020100) use CoreQSPI v2
 * with identical register layouts. The controller is selected at build
 * time via MPFS_SC_SPI, which changes QSPI_BASE in the header.
 * ========================================================================== */

/* Microsecond delay using RISC-V time CSR (1 MHz tick rate) */
#ifndef WOLFBOOT_RISCV_MMODE
static void udelay(uint32_t us)
{
    uint64_t start = csr_read(time);
    while ((uint64_t)(csr_read(time) - start) < us)
        ;
}
#endif
/* Forward declarations */
static int qspi_transfer_block(uint8_t read_mode, const uint8_t *cmd,
                                uint32_t cmd_len, uint8_t *data,
                                uint32_t data_len, uint8_t dummy_cycles);
static int qspi_read_id(uint8_t *id_buf);
static int qspi_enter_4byte_mode(void);

/* Send Release from Deep Power-Down / Wake up command */
static void qspi_flash_wakeup(void)
{
    uint8_t cmd = 0xAB;  /* Release from Deep Power-Down */
    qspi_transfer_block(QSPI_MODE_WRITE, &cmd, 1, NULL, 0, 0);
    /* Flash needs tRES1 (3us typ) to wake up */
    udelay(10);
}

int qspi_init(void)
{
    uint8_t id[3];
    uint32_t timeout;

#ifdef MPFS_SC_SPI
    wolfBoot_printf("QSPI: Using SC QSPI Controller (0x%x)\n", QSPI_BASE);

    /* Wait for system controller to finish any pending operations before
     * taking direct control of the SC QSPI peripheral */
    mpfs_scb_wait_clear(SERVICES_SR_OFFSET, SERVICES_SR_BUSY_MASK,
        QSPI_TIMEOUT_TRIES);

#ifdef DEBUG_QSPI
    wolfBoot_printf("QSPI: Initial CTRL=0x%x, STATUS=0x%x, DIRECT=0x%x\n",
        QSPI_CONTROL, QSPI_STATUS, QSPI_DIRECT);
#endif

    /* Disable direct access / XIP mode (SC may have left it enabled) */
    QSPI_DIRECT = 0;
#else
    wolfBoot_printf("QSPI: Using MSS QSPI Controller (0x%x)\n", QSPI_BASE);

    /* Enable QSPI peripheral clock (MSS only) */
    SYSREG_SUBBLK_CLOCK_CR |= SYSREG_SUBBLK_CLOCK_CR_QSPI;
    udelay(1);

    /* Release MSS QSPI from reset (MSS only) */
    SYSREG_SOFT_RESET_CR &= ~SYSREG_SOFT_RESET_CR_QSPI;
    udelay(10);
#endif

    /* Disable controller before configuration */
    QSPI_CONTROL = 0;

    /* Disable all interrupts */
    QSPI_IEN = 0;

    /* Configure QSPI Control Register:
     * - Clock divider for ~5MHz (conservative)
     * - CPOL=1 (clock idle high) for SPI Mode 3
     * - Sample on SCK edge
     * - Enable controller
     */
    QSPI_CONTROL =
        (QSPI_CLK_DIV_30 << QSPI_CTRL_CLKRATE_OFFSET) |
        QSPI_CTRL_CLKIDLE |
        QSPI_CTRL_SAMPLE_SCK |
        QSPI_CTRL_EN;

    /* Wait for controller to be ready */
    timeout = QSPI_TIMEOUT_TRIES;
    while (!(QSPI_STATUS & QSPI_STATUS_READY) && --timeout);
    if (timeout == 0) {
        wolfBoot_printf("QSPI: Controller not ready\n");
        return -1;
    }

    /* Wake up flash from deep power-down (if applicable) */
    qspi_flash_wakeup();

    /* Read and display JEDEC ID for verification */
    if (qspi_read_id(id) == 0) {
        wolfBoot_printf("QSPI: Flash ID = 0x%02x 0x%02x 0x%02x\n",
            id[0], id[1], id[2]);
    }

    /* Enter 4-byte addressing mode for >16MB flash */
    qspi_enter_4byte_mode();

    return 0;
}

/* QSPI Block Transfer Function
 * Modeled after Microchip's MSS_QSPI_polled_transfer_block reference driver.
 *
 * read_mode: 0=write (QSPI_MODE_WRITE), 1=read (QSPI_MODE_READ)
 * cmd: Command buffer (opcode + address bytes)
 * cmd_len: Length of command (opcode + address, NOT including opcode separately)
 * data: Data buffer for read/write
 * data_len: Length of data phase
 * dummy_cycles: Number of idle cycles between command and data phase
 */
static int qspi_transfer_block(uint8_t read_mode, const uint8_t *cmd,
                               uint32_t cmd_len, uint8_t *data,
                               uint32_t data_len, uint8_t dummy_cycles)
{
    uint32_t total_bytes = cmd_len + data_len;
    uint32_t frames;
    uint32_t i;
    uint32_t timeout;
    uint32_t frame_cmd;

    /* Wait for controller to be ready before starting */
    timeout = QSPI_TIMEOUT_TRIES;
    while (!(QSPI_STATUS & QSPI_STATUS_READY) && --timeout);
    if (timeout == 0) {
    #ifdef DEBUG_QSPI
        wolfBoot_printf("QSPI: Timeout waiting for READY\n");
    #endif
        return -1;
    }

    /* Drain RX FIFO of any stale data from previous transfers. */
    timeout = QSPI_TIMEOUT_TRIES;
    while ((QSPI_STATUS & QSPI_STATUS_RXAVAIL) && --timeout) {
        (void)QSPI_RX_DATA;
    }
#ifdef DEBUG_QSPI
    if (timeout == 0) {
        /* log warning and continue trying to transfer data */
        wolfBoot_printf("QSPI: Timeout draining RX FIFO\n");
    }
#endif

    /* Configure FRAMES register:
     * - Total bytes: command + data (idle cycles handled by hardware)
     * - Command bytes: TX-only bytes before data phase
     * - Idle cycles: inserted by hardware between command and data
     * - FBYTE: status flags (RXAVAIL/TXAVAIL) refer to individual bytes
     *
     * For write-mode transfers, set CMDBYTES = TOTALBYTES so the entire
     * transfer occurs in the command phase (TX-only). The CoreQSPI data
     * phase shifts TX FIFO output by a fixed offset on writes, causing
     * data rotation in the programmed page. Keeping everything in the
     * command phase avoids this. The flash determines command vs data
     * boundaries from the opcode, not the controller's phase. */
    frame_cmd = read_mode ? cmd_len : total_bytes;
    frames = ((total_bytes & 0xFFFF) << QSPI_FRAMES_TOTALBYTES_OFFSET) |
                ((frame_cmd & 0x1FF) << QSPI_FRAMES_CMDBYTES_OFFSET) |
                ((dummy_cycles & 0xF) << QSPI_FRAMES_IDLE_OFFSET) |
                (1u << QSPI_FRAMES_FBYTE_OFFSET);

    QSPI_FRAMES = frames;

    /* Send command bytes (opcode + address).
     * Use TXAVAIL (bit 3) to check for FIFO space -- CoreQSPI v2 does NOT
     * have a TXFULL status bit (bit 5 is reserved/always 0).
     * A fence (iorw, iorw) after each TX write ensures the store reaches the
     * peripheral before we read STATUS again (RISC-V RVWMO allows posted
     * stores that could cause stale TXAVAIL reads and FIFO overflow). */
    for (i = 0; i < cmd_len; i++) {
        timeout = QSPI_TIMEOUT_TRIES;
        while (!(QSPI_STATUS & QSPI_STATUS_TXAVAIL) && --timeout);
        if (timeout == 0) {
        #ifdef DEBUG_QSPI
            wolfBoot_printf("QSPI: TX FIFO full timeout\n");
        #endif
            return -2;
        }
        QSPI_TX_DATA = cmd[i];
        QSPI_IO_FENCE();
    }

    if (read_mode) {
        /* Read mode: poll RXAVAIL for each data byte. */
        for (i = 0; i < data_len; i++) {
            timeout = QSPI_RX_TIMEOUT_TRIES;
            while (!(QSPI_STATUS & QSPI_STATUS_RXAVAIL) && --timeout);
            if (timeout == 0) {
            #ifdef DEBUG_QSPI
                wolfBoot_printf("QSPI: RX timeout at byte %d, status=0x%x\n",
                    i, QSPI_STATUS);
            #endif
                return -3;
            }
            data[i] = QSPI_RX_DATA;
        }
        /* Wait for receive complete */
        timeout = QSPI_RX_TIMEOUT_TRIES;
        while (!(QSPI_STATUS & QSPI_STATUS_RXDONE) && --timeout);
        if (timeout == 0) {
        #ifdef DEBUG_QSPI
            wolfBoot_printf("QSPI: RXDONE timeout\n");
        #endif
            return -5;
        }
    } else {
        /* Write mode: send data bytes.
         * Must push bytes without delay -- any gap causes FIFO underflow
         * since CoreQSPI continues clocking with empty FIFO.
         * Fence (iorw, iorw) after each write ensures the store reaches the
         * FIFO before we re-read STATUS (prevents FIFO overflow from posted
         * stores). */
        if (data && data_len > 0) {
            for (i = 0; i < data_len; i++) {
                timeout = QSPI_TIMEOUT_TRIES;
                while (!(QSPI_STATUS & QSPI_STATUS_TXAVAIL) && --timeout);
                if (timeout == 0) {
                #ifdef DEBUG_QSPI
                    wolfBoot_printf("QSPI: TX data timeout\n");
                #endif
                    return -4;
                }
                QSPI_TX_DATA = data[i];
                QSPI_IO_FENCE();
            }
        }
        /* Wait for transmit complete */
        timeout = QSPI_TIMEOUT_TRIES;
        while (!(QSPI_STATUS & QSPI_STATUS_TXDONE) && --timeout);
        if (timeout == 0) {
        #ifdef DEBUG_QSPI
            wolfBoot_printf("QSPI: TXDONE timeout, status=0x%x\n",
                QSPI_STATUS);
        #endif
            return -5;
        }
    }

#ifdef DEBUG_QSPI
    wolfBoot_printf("QSPI: cmd[0]=0x%x, cmd_len=%d, data_len=%d, frames=0x%x\n",
        cmd[0], cmd_len, data_len, frames);
#endif

    return 0;
}

/* Read JEDEC ID from flash */
static int qspi_read_id(uint8_t *id_buf)
{
    uint8_t cmd = QSPI_CMD_READ_ID_OPCODE;
    return qspi_transfer_block(QSPI_MODE_READ, &cmd, 1, id_buf, 3, 0);
}

/* Send Write Enable command */
static int qspi_write_enable(void)
{
    uint8_t cmd = QSPI_CMD_WRITE_ENABLE_OPCODE;
    return qspi_transfer_block(QSPI_MODE_WRITE, &cmd, 1, NULL, 0, 0);
}

/* Wait for flash to be ready (poll status register) */
static int qspi_wait_ready(uint32_t timeout_ms)
{
    uint8_t cmd = QSPI_CMD_READ_STATUS_OPCODE;
    uint8_t status;
    uint32_t count = 0;
    uint32_t max_count = timeout_ms * 1000;  /* Rough timing */
    int ret;

    do {
        ret = qspi_transfer_block(QSPI_MODE_READ, &cmd, 1, &status, 1, 0);
        if (ret != 0) {
            return ret;  /* Propagate transfer error */
        }
        if (!(status & 0x01)) {  /* Bit 0 = WIP (Write In Progress) */
            return 0;  /* Ready */
        }
        count++;
    } while (count < max_count);

    return -1;  /* Timeout */
}

/* Enter 4-byte addressing mode (required for >32MB flash) */
static int qspi_enter_4byte_mode(void)
{
    uint8_t cmd = QSPI_CMD_ENTER_4BYTE_MODE;
    return qspi_transfer_block(QSPI_MODE_WRITE, &cmd, 1, NULL, 0, 0);
}

/* Read from QSPI flash (4-byte addressing) */
static int qspi_flash_read(uint32_t address, uint8_t *data, uint32_t len)
{
    const uint32_t max_chunk = 0xFFFF - 5; /* total_bytes is 16-bit, cmd is 5 */
    uint8_t cmd[5];
    uint32_t remaining = len;
    uint32_t chunk_len;
    int ret;

    while (remaining > 0) {
        chunk_len = (remaining > max_chunk) ? max_chunk : remaining;

        /* Build 4-byte read command */
        cmd[0] = QSPI_CMD_4BYTE_READ_OPCODE;
        cmd[1] = (address >> 24) & 0xFF;
        cmd[2] = (address >> 16) & 0xFF;
        cmd[3] = (address >> 8) & 0xFF;
        cmd[4] = address & 0xFF;

        ret = qspi_transfer_block(QSPI_MODE_READ, cmd, 5, data, chunk_len, 0);
        if (ret != 0) {
            return ret;
        }

        address += chunk_len;
        data += chunk_len;
        remaining -= chunk_len;
    }

    return (int)len;
}

/* Write to QSPI flash - single page (max 256 bytes) */
static int qspi_flash_write_page(uint32_t address, const uint8_t *data, uint32_t len)
{
    uint8_t cmd[5];
    int ret;

    /* Ensure page alignment and length */
    if (len > FLASH_PAGE_SIZE) {
        len = FLASH_PAGE_SIZE;
    }

    /* Enable write */
    ret = qspi_write_enable();
    if (ret != 0) {
        return ret;
    }

    /* Build 4-byte page program command */
    cmd[0] = QSPI_CMD_4BYTE_PAGE_PROG_OPCODE;
    cmd[1] = (address >> 24) & 0xFF;
    cmd[2] = (address >> 16) & 0xFF;
    cmd[3] = (address >> 8) & 0xFF;
    cmd[4] = address & 0xFF;

    /* Send command + data */
    ret = qspi_transfer_block(QSPI_MODE_WRITE, cmd, 5, (uint8_t *)data, len, 0);
    if (ret != 0) {
        return ret;
    }

    /* Wait for write to complete */
    return qspi_wait_ready(1000);  /* 1 second timeout */
}

/* Erase 64KB sector */
static int qspi_flash_sector_erase(uint32_t address)
{
    uint8_t cmd[5];
    int ret;

    /* Enable write */
    ret = qspi_write_enable();
    if (ret != 0) {
        return ret;
    }

    /* Build 4-byte sector erase command */
    cmd[0] = QSPI_CMD_4BYTE_SECTOR_ERASE;
    cmd[1] = (address >> 24) & 0xFF;
    cmd[2] = (address >> 16) & 0xFF;
    cmd[3] = (address >> 8) & 0xFF;
    cmd[4] = address & 0xFF;

    ret = qspi_transfer_block(QSPI_MODE_WRITE, cmd, 5, NULL, 0, 0);
    if (ret != 0) {
        return ret;
    }

    /* Wait for erase to complete (64KB erase can take several seconds) */
    return qspi_wait_ready(10000);  /* 10 second timeout */
}

/* ==========================================================================
 * External Flash API Implementation
 * ========================================================================== */
void ext_flash_lock(void)
{
    /* Optional: Could implement write protection here */
}

void ext_flash_unlock(void)
{
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    uint32_t page_offset;
    uint32_t chunk_len;
    int ret;
    int remaining = len;
    int total = len;

    #ifdef DEBUG_QSPI
    wolfBoot_printf("QSPI: Write 0x%x, len %d\n", (uint32_t)address, len);
    #endif

    /* Write data page by page */
    while (remaining > 0) {
        /* Calculate bytes to write in this page */
        page_offset = address & (FLASH_PAGE_SIZE - 1);
        chunk_len = FLASH_PAGE_SIZE - page_offset;
        if (chunk_len > (uint32_t)remaining) {
            chunk_len = remaining;
        }

        /* Write page */
        ret = qspi_flash_write_page(address, data, chunk_len);
        if (ret != 0) {
            return ret;
        }

        /* Update pointers */
        address += chunk_len;
        data += chunk_len;
        remaining -= chunk_len;
    }

    return total;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    #ifdef DEBUG_QSPI
    wolfBoot_printf("QSPI: Read 0x%x -> 0x%lx, len %d\n",
        (uint32_t)address, (unsigned long)data, len);
    #endif
    return qspi_flash_read((uint32_t)address, data, (uint32_t)len);
}

int ext_flash_erase(uintptr_t address, int len)
{
    uint32_t sector_addr;
    uint32_t end_addr;
    int ret;
    int total = len;

    #ifdef DEBUG_QSPI
    wolfBoot_printf("QSPI: Erase 0x%x, len %d\n", (uint32_t)address, len);
    #endif

    /* Check for invalid length or integer overflow */
    if (len <= 0 || (uint32_t)len > UINT32_MAX - (uint32_t)address) {
        return -1;
    }

    /* Align to sector boundaries */
    sector_addr = address & ~(FLASH_SECTOR_SIZE - 1);
    end_addr = (uint32_t)address + (uint32_t)len;

    /* Erase sectors */
    while (sector_addr < end_addr) {
        #ifdef DEBUG_QSPI
        wolfBoot_printf("QSPI: Erasing sector at 0x%08X\n", sector_addr);
        #endif

        ret = qspi_flash_sector_erase(sector_addr);
        if (ret != 0) {
            wolfBoot_printf("QSPI: Erase failed\n");
            return ret;
        }

        sector_addr += FLASH_SECTOR_SIZE;
    }

    return total;
}

/* ============================================================================
 * UART QSPI Programmer
 *
 * Allows programming the QSPI flash over the debug UART without a JTAG/Libero
 * tool. Enabled at build time with UART_QSPI_PROGRAM=1 in the .config.
 *
 * Protocol (after wolfBoot prints the "QSPI-PROG" prompt):
 *   1. Host sends 'P' within the timeout window to enter programming mode
 *   2. wolfBoot sends "READY\r\n"
 *   3. Host sends [4-byte LE QSPI address][4-byte LE data length]
 *   4. wolfBoot erases required sectors, sends "ERASED\r\n"
 *   5. For each 256-byte chunk:
 *        wolfBoot sends ACK byte (0x06) -> host sends chunk -> wolfBoot writes
 *   6. wolfBoot sends "DONE\r\n" and continues normal boot
 *
 * Host side: tools/scripts/mpfs_qspi_prog.py
 * ============================================================================ */
#if defined(UART_QSPI_PROGRAM) && defined(__WOLFBOOT)

#define QSPI_PROG_CHUNK        256
#define QSPI_PROG_ACK          0x06
#define QSPI_RX_TIMEOUT_MS     10000U  /* 10 s per byte -- aborts if host disappears */


/* Returns 0-255 on success, -1 on timeout (so the boot path is never deadlocked). */
static int uart_qspi_rx(void)
{
    uint32_t t;
    for (t = 0; t < QSPI_RX_TIMEOUT_MS; t++) {
        if (MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_DR)
            return (int)(uint8_t)MMUART_RBR(DEBUG_UART_BASE);
        udelay(1000);
    }
    return -1; /* timeout */
}

static void uart_qspi_tx(uint8_t c)
{
    while (!(MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE))
        ;
    MMUART_THR(DEBUG_UART_BASE) = c;
}

static void uart_qspi_puts(const char *s)
{
    while (*s)
        uart_qspi_tx((uint8_t)*s++);
}

static void qspi_uart_program(void)
{
    uint8_t ch = 0;
    uint32_t addr, size, n_sectors, written, t;
    uint32_t i, s;
    uint8_t chunk[QSPI_PROG_CHUNK];

    /* Use uart_qspi_puts (direct UART) for ALL programmer output.
     * wolfBoot_printf uses uart_write which adds \r before \n and may
     * leave stale bytes in the UART TX pipeline that corrupt the
     * binary ACK/data protocol after ERASED. */
    uart_qspi_puts("QSPI-PROG: Press 'P' within 3s to program flash\r\n");

    /* Drain any stale RX bytes before opening the window */
    while (MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_DR)
        (void)MMUART_RBR(DEBUG_UART_BASE);

    /* Wait up to 3s: 3000 iterations of 1ms each */
    for (t = 0; t < 3000U; t++) {
        udelay(1000);
        if (MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_DR) {
            ch = MMUART_RBR(DEBUG_UART_BASE);
            break;
        }
    }

    if (ch != 'P' && ch != 'p') {
        uart_qspi_puts("QSPI-PROG: No trigger, booting\r\n");
        return;
    }

    uart_qspi_puts("READY\r\n");

    /* Receive destination address then data length (4 bytes LE each) */
    addr = 0;
    for (i = 0; i < 4; i++) {
        int b = uart_qspi_rx();
        if (b < 0) {
            uart_qspi_puts("QSPI-PROG: RX timeout (addr)\r\n");
            return;
        }
        addr |= ((uint32_t)(uint8_t)b << (i * 8));
    }
    size = 0;
    for (i = 0; i < 4; i++) {
        int b = uart_qspi_rx();
        if (b < 0) {
            uart_qspi_puts("QSPI-PROG: RX timeout (size)\r\n");
            return;
        }
        size |= ((uint32_t)(uint8_t)b << (i * 8));
    }

    if (size == 0 || size > 0x200000U) {
        uart_qspi_puts("QSPI-PROG: Invalid size\r\n");
        return;
    }

    /* Reject writes to unaligned or out-of-partition addresses */
    if ((addr & (FLASH_SECTOR_SIZE - 1U)) != 0U) {
        uart_qspi_puts("QSPI-PROG: Not sector-aligned\r\n");
        return;
    }
    if (!((addr >= WOLFBOOT_PARTITION_BOOT_ADDRESS &&
           addr < WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE &&
           size <= WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - addr) ||
          (addr >= WOLFBOOT_PARTITION_UPDATE_ADDRESS &&
           addr < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE &&
           size <= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE - addr))) {
        uart_qspi_puts("QSPI-PROG: Outside partition\r\n");
        return;
    }

    /* Erase all required sectors */
    n_sectors = (size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    uart_qspi_puts("QSPI-PROG: Erasing...\r\n");
    ext_flash_unlock();
    for (s = 0; s < n_sectors; s++) {
        int ret;
        /* The MSS WDTs always count and reset the chip at timeout (~28.6s
         * at the reset divisor) and cannot be disabled, so a transfer of
         * more than a few tens of KB outlives the period: refresh all
         * five per sector here and per chunk below. */
        MSS_WDT_REFRESH(MSS_WDT_E51_BASE)   = 0xDEADC0DEU;
        MSS_WDT_REFRESH(MSS_WDT_U54_1_BASE) = 0xDEADC0DEU;
        MSS_WDT_REFRESH(MSS_WDT_U54_2_BASE) = 0xDEADC0DEU;
        MSS_WDT_REFRESH(MSS_WDT_U54_3_BASE) = 0xDEADC0DEU;
        MSS_WDT_REFRESH(MSS_WDT_U54_4_BASE) = 0xDEADC0DEU;
        ret = ext_flash_erase(addr + s * FLASH_SECTOR_SIZE,
                              FLASH_SECTOR_SIZE);
        if (ret < 0) {
            uart_qspi_puts("QSPI-PROG: Erase failed\r\n");
            ext_flash_lock();
            return;
        }
    }

    uart_qspi_puts("ERASED\r\n");

    /* Chunk transfer: wolfBoot requests each 256-byte block with ACK 0x06.
     * No wolfBoot_printf allowed in this loop -- only direct UART via
     * uart_qspi_tx/uart_qspi_puts to avoid protocol corruption. */
    written = 0;
    while (written < size) {
        int ret;
        uint32_t chunk_len = size - written;
        if (chunk_len > QSPI_PROG_CHUNK)
            chunk_len = QSPI_PROG_CHUNK;

        MSS_WDT_REFRESH(MSS_WDT_E51_BASE)   = 0xDEADC0DEU;
        MSS_WDT_REFRESH(MSS_WDT_U54_1_BASE) = 0xDEADC0DEU;
        MSS_WDT_REFRESH(MSS_WDT_U54_2_BASE) = 0xDEADC0DEU;
        MSS_WDT_REFRESH(MSS_WDT_U54_3_BASE) = 0xDEADC0DEU;
        MSS_WDT_REFRESH(MSS_WDT_U54_4_BASE) = 0xDEADC0DEU;

        uart_qspi_tx(QSPI_PROG_ACK);          /* request next chunk */

        for (i = 0; i < chunk_len; i++) {
            int b = uart_qspi_rx();
            if (b < 0) {
                uart_qspi_puts("QSPI-PROG: RX timeout\r\n");
                ext_flash_lock();
                return;
            }
            chunk[i] = (uint8_t)b;
        }

        ret = ext_flash_write(addr + written, chunk, (int)chunk_len);
        if (ret < 0) {
            uart_qspi_puts("QSPI-PROG: Write failed\r\n");
            ext_flash_lock();
            return;
        }
        written += chunk_len;
    }
    ext_flash_lock();

    uart_qspi_puts("DONE\r\n");
}

#endif /* UART_QSPI_PROGRAM */

/* Test for external QSPI flash erase/write/read */
#ifdef TEST_EXT_FLASH

#ifndef TEST_EXT_ADDRESS
    #define TEST_EXT_ADDRESS WOLFBOOT_PARTITION_UPDATE_ADDRESS
#endif

static int test_ext_flash(void)
{
    int ret;
    uint32_t i;
    uint8_t pageData[FLASH_PAGE_SIZE];

    wolfBoot_printf("Ext Flash Test at 0x%x\n", TEST_EXT_ADDRESS);

#ifndef TEST_FLASH_READONLY
    /* Erase sector */
    ret = ext_flash_erase(TEST_EXT_ADDRESS, FLASH_SECTOR_SIZE);
    wolfBoot_printf("Sector Erase: Ret %d\n", ret);
    if (ret < 0)
        return ret;

    /* Verify erase (should be all 0xFF) */
    memset(pageData, 0, sizeof(pageData));
    ret = ext_flash_read(TEST_EXT_ADDRESS, pageData, sizeof(pageData));
    if (ret < 0) {
        wolfBoot_printf("Erase verify read failed: Ret %d\n", ret);
        return ret;
    }
    wolfBoot_printf("Erase verify: ");
    for (i = 0; i < 16; i++) {
        wolfBoot_printf("%02x ", pageData[i]);
    }
    wolfBoot_printf("\n");

    /* Write Page */
    for (i = 0; i < sizeof(pageData); i++) {
        pageData[i] = (i & 0xff);
    }
    ret = ext_flash_write(TEST_EXT_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Page Write: Ret %d\n", ret);
    if (ret < 0)
        return ret;
#endif /* !TEST_FLASH_READONLY */

    /* Read page */
    memset(pageData, 0, sizeof(pageData));
    ret = ext_flash_read(TEST_EXT_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Page Read: Ret %d\n", ret);
    if (ret < 0)
        return ret;

    /* Check data */
    for (i = 0; i < sizeof(pageData); i++) {
        if (pageData[i] != (i & 0xff)) {
            wolfBoot_printf("Check Data @ %d failed (0x%02x != 0x%02x)\n",
                i, pageData[i], (i & 0xff));
            wolfBoot_printf("First 16 bytes: ");
            for (i = 0; i < 16; i++) {
                wolfBoot_printf("%02x ", pageData[i]);
            }
            wolfBoot_printf("\n");
            return -1;
        }
    }

    wolfBoot_printf("Ext Flash Test Passed\n");
    return 0;
}
#endif /* TEST_EXT_FLASH */

#else /* !EXT_FLASH */

/* Stubs for when QSPI is disabled */
void ext_flash_lock(void)
{
}

void ext_flash_unlock(void)
{
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int ext_flash_erase(uintptr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

#endif /* EXT_FLASH */

#if defined(MMU) && !defined(WOLFBOOT_NO_PARTITIONS)
void* hal_get_dts_address(void)
{
#if defined(EXT_FLASH) && defined(NO_XIP)
    /* Flash is not memory-mapped when using NO_XIP with external flash
     * (e.g. SC SPI). DTS must be loaded via ext_flash_read, not direct
     * dereference. Return NULL so the caller skips the direct-access path. */
    return NULL;
#else
    return (void*)WOLFBOOT_DTS_BOOT_ADDRESS;
#endif
}
#endif

/* PLIC: E51(hart 0)->ctx 0 (M-mode only); U54(1-4)->ctx hart*2-1 (M), hart*2 (S) */
#ifdef WOLFBOOT_RISCV_MMODE
uint32_t plic_get_context(void)
{
    uint32_t hart_id;
    __asm__ volatile("csrr %0, mhartid" : "=r"(hart_id));
    return (hart_id == 0) ? 0 : (hart_id * 2) - 1;
}
#else
extern unsigned long get_boot_hartid(void);
uint32_t plic_get_context(void)
{
    return (uint32_t)get_boot_hartid() * 2;
}
#endif

/* Dispatch IRQ to appropriate platform handler */
void plic_dispatch_irq(uint32_t irq)
{
    switch (irq) {
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
        case PLIC_INT_MMC_MAIN:
            sdhci_irq_handler();
            break;
#endif
        default:
            /* Unknown interrupt - ignore */
            break;
    }
}

/* MSSIO IOMUX + bank-config register offsets.
 * IOMUX0..IOMUX6_CR and the per-pad IO_CFG_*_*_CR registers live in SYSREG
 * (base 0x20002000).  The two MSSIO_BANK*_CFG_CR registers that set
 * bank-wide pcode/ncode/voltage live in the *SCB* register space
 * (SYSREGSCB_BASE 0x20003000) at offsets 0x1C4/0x1C8 - HSS writes them via
 * SCB_REGS, not SYSREG. */
#define SYSREG_IOMUX0_CR_OFFSET             0x200u
#define SYSREG_IOMUX1_CR_OFFSET             0x204u
#define SYSREG_IOMUX2_CR_OFFSET             0x208u
#define SYSREG_IOMUX3_CR_OFFSET             0x20Cu
#define SYSREG_IOMUX4_CR_OFFSET             0x210u
#define SYSREG_IOMUX5_CR_OFFSET             0x214u
#define SYSREG_IOMUX6_CR_OFFSET             0x218u
#define SYSREG_MSSIO_BANK4_IO_CFG_0_1_CR    0x234u  /* +4 each pair */
#define SYSREG_MSSIO_BANK2_IO_CFG_0_1_CR    0x254u  /* +4 each pair */
#define SCB_MSSIO_BANK2_CFG_CR_OFFSET       0x1C4u
#define SCB_MSSIO_BANK4_CFG_CR_OFFSET       0x1C8u

/* Configure the MSSIO IOMUX so the SDHCI controller's pins are actually
 * routed to the SD/eMMC slot pads.  In S-mode builds HSS does this as
 * part of mssio_setup() during nwc init; M-mode wolfBoot has to do it
 * itself or the controller talks to floating pads and SD command
 * responses come back as garbage (CMD_INDEX_ERR + CMD_END_BIT_ERR).
 *
 * All values come straight from the Libero/HSS-generated
 * fpga_design_config.h that LIBERO_FPGA_CONFIG_DIR points at. */
#if defined(WOLFBOOT_RISCV_MMODE) && defined(MPFS_DDR_INIT)
void mpfs_iomux_init(void)
{
    uint32_t iomux4, iomux5;

    SYSREG_REG(SYSREG_IOMUX0_CR_OFFSET) = LIBERO_SETTING_IOMUX0_CR;
    SYSREG_REG(SYSREG_IOMUX1_CR_OFFSET) = LIBERO_SETTING_IOMUX1_CR;
    SYSREG_REG(SYSREG_IOMUX2_CR_OFFSET) = LIBERO_SETTING_IOMUX2_CR;
    SYSREG_REG(SYSREG_IOMUX3_CR_OFFSET) = LIBERO_SETTING_IOMUX3_CR;

    /* IOMUX4 + IOMUX5 need MPFS-Video-Kit-specific GPIO drive overrides
     * on top of the Libero values to steer the board's external SD/eMMC
     * demux mux into SD-card mode and pull a USB pin low.  This logic
     * is implemented in HSS as a board hook in
     * boards/mpfs-video-kit/hss_board_init.c::switch_demux_using_fabric_ip
     * and is NOT visible from the generic Libero IOMUX_CR values alone.
     * Without this, the SDHCI controller talks to bank4 pads but those
     * signals never reach the SD card slot, producing the "CMD8 timeout
     * regardless of card insertion" symptom we observed for many runs.
     *
     *   IOMUX4 bits[19:16] (USB pin): 0xD = drive logic 0
     *   IOMUX5 bits[3:0]   (pad 30):  0xE = drive logic 1
     *   IOMUX5 bits[19:16] (pad 34):  0xE = drive logic 1
     *   IOMUX5 bits[31:28] (pad 37):  0xD = drive logic 0
     */
    iomux4 = LIBERO_SETTING_IOMUX4_CR;
    iomux5 = LIBERO_SETTING_IOMUX5_CR;
    iomux4 &= ~(0xFu << 16);
    iomux4 |=  (0xDu << 16);
    iomux5 &= ~((0xFu << 0) | (0xFu << 16) | (0xFu << 28));
    iomux5 |=  ((0xEu << 0) | (0xEu << 16) | (0xDu << 28));
    SYSREG_REG(SYSREG_IOMUX4_CR_OFFSET) = iomux4;
    SYSREG_REG(SYSREG_IOMUX5_CR_OFFSET) = iomux5;
    SYSREG_REG(SYSREG_IOMUX6_CR_OFFSET) = LIBERO_SETTING_IOMUX6_CR;

    /* Bank-wide config goes via SCB; per-pad IO_CFG goes via SYSREG. */
    SYSREGSCB_REG(SCB_MSSIO_BANK4_CFG_CR_OFFSET) =
        LIBERO_SETTING_MSSIO_BANK4_CFG_CR;
    SYSREG_REG(SYSREG_MSSIO_BANK4_IO_CFG_0_1_CR + 0x00u) =
        LIBERO_SETTING_MSSIO_BANK4_IO_CFG_0_1_CR;
    SYSREG_REG(SYSREG_MSSIO_BANK4_IO_CFG_0_1_CR + 0x04u) =
        LIBERO_SETTING_MSSIO_BANK4_IO_CFG_2_3_CR;
    SYSREG_REG(SYSREG_MSSIO_BANK4_IO_CFG_0_1_CR + 0x08u) =
        LIBERO_SETTING_MSSIO_BANK4_IO_CFG_4_5_CR;
    SYSREG_REG(SYSREG_MSSIO_BANK4_IO_CFG_0_1_CR + 0x0Cu) =
        LIBERO_SETTING_MSSIO_BANK4_IO_CFG_6_7_CR;
    SYSREG_REG(SYSREG_MSSIO_BANK4_IO_CFG_0_1_CR + 0x10u) =
        LIBERO_SETTING_MSSIO_BANK4_IO_CFG_8_9_CR;
    SYSREG_REG(SYSREG_MSSIO_BANK4_IO_CFG_0_1_CR + 0x14u) =
        LIBERO_SETTING_MSSIO_BANK4_IO_CFG_10_11_CR;
    SYSREG_REG(SYSREG_MSSIO_BANK4_IO_CFG_0_1_CR + 0x18u) =
        LIBERO_SETTING_MSSIO_BANK4_IO_CFG_12_13_CR;

    SYSREGSCB_REG(SCB_MSSIO_BANK2_CFG_CR_OFFSET) =
        LIBERO_SETTING_MSSIO_BANK2_CFG_CR;
    SYSREG_REG(SYSREG_MSSIO_BANK2_IO_CFG_0_1_CR + 0x00u) =
        LIBERO_SETTING_MSSIO_BANK2_IO_CFG_0_1_CR;
    SYSREG_REG(SYSREG_MSSIO_BANK2_IO_CFG_0_1_CR + 0x04u) =
        LIBERO_SETTING_MSSIO_BANK2_IO_CFG_2_3_CR;
    SYSREG_REG(SYSREG_MSSIO_BANK2_IO_CFG_0_1_CR + 0x08u) =
        LIBERO_SETTING_MSSIO_BANK2_IO_CFG_4_5_CR;
    SYSREG_REG(SYSREG_MSSIO_BANK2_IO_CFG_0_1_CR + 0x0Cu) =
        LIBERO_SETTING_MSSIO_BANK2_IO_CFG_6_7_CR;
    SYSREG_REG(SYSREG_MSSIO_BANK2_IO_CFG_0_1_CR + 0x10u) =
        LIBERO_SETTING_MSSIO_BANK2_IO_CFG_8_9_CR;
    SYSREG_REG(SYSREG_MSSIO_BANK2_IO_CFG_0_1_CR + 0x14u) =
        LIBERO_SETTING_MSSIO_BANK2_IO_CFG_10_11_CR;
    SYSREG_REG(SYSREG_MSSIO_BANK2_IO_CFG_0_1_CR + 0x18u) =
        LIBERO_SETTING_MSSIO_BANK2_IO_CFG_12_13_CR;

    __asm__ volatile("fence iorw, iorw" ::: "memory");
}
#endif /* WOLFBOOT_RISCV_MMODE && MPFS_DDR_INIT */

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
/* SDHCI Platform HAL */


/* MSS MPU base + per-master offset.  Each AXI master (FIC0/1/2, CRYPTO,
 * GEM0/1, USB, MMC, SCB, TRACE) has 16 PMPCFG entries (uint64_t each) at
 * 0x20005000 + (master_index << 8).  HSS calls mpu_configure() during early
 * boot to load these from LIBERO_SETTING_*_MPU_CFG_PMP* defaults; without
 * that, the master may be locked out of memory regions it needs and stalls
 * silently mid-transaction.  MMC is master index 7. */
#define MSS_MPU_BASE                0x20005000UL
#define MSS_MPU_MMC_BASE            (MSS_MPU_BASE + (7UL << 8))

#ifdef MPFS_DDR_INIT
/* Only available when LIBERO_FPGA_CONFIG_DIR is set (which also enables
 * MPFS_DDR_INIT in arch.mk).  HSS already configures these PMP entries
 * during its own boot, so non-DDR / HSS-loaded builds don't need this. */
static void mpfs_mpu_init_mmc(void)
{
    volatile uint64_t *pmp = (volatile uint64_t *)MSS_MPU_MMC_BASE;
    pmp[0] = LIBERO_SETTING_MMC_MPU_CFG_PMP0;
    pmp[1] = LIBERO_SETTING_MMC_MPU_CFG_PMP1;
    pmp[2] = LIBERO_SETTING_MMC_MPU_CFG_PMP2;
    pmp[3] = LIBERO_SETTING_MMC_MPU_CFG_PMP3;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}
#endif /* MPFS_DDR_INIT */

#ifdef SDHCI_BLOCK_VIA_PDMA
/* Pet all five MSS watchdogs during the (long) per-block SDHCI read loop.
 * Overrides the weak no-op in src/sdhci.c.  The MSS watchdogs always count
 * and reset the chip at timeout and cannot be disabled, so the multi-second
 * load of a large image must keep refreshing them. */
void sdhci_platform_wdt_pet(void)
{
    MSS_WDT_REFRESH(MSS_WDT_E51_BASE)   = 0xDEADC0DEU;
    MSS_WDT_REFRESH(MSS_WDT_U54_1_BASE) = 0xDEADC0DEU;
    MSS_WDT_REFRESH(MSS_WDT_U54_2_BASE) = 0xDEADC0DEU;
    MSS_WDT_REFRESH(MSS_WDT_U54_3_BASE) = 0xDEADC0DEU;
    MSS_WDT_REFRESH(MSS_WDT_U54_4_BASE) = 0xDEADC0DEU;
}

/* Copy a staged SDHCI block to its final destination (overrides the weak
 * memcpy default in src/sdhci.c).  Direct CPU writes to DDR do not land on
 * this board, so a DDR destination (0x8xxxxxxx) is written through the PDMA
 * master and verified via its non-cached alias (| 0x40000000), re-PDMA'ing on
 * a drop (the PDMA->DDR write intermittently drops a block when interleaved
 * with SDHCI reads).  A non-DDR destination (L2 header/GPT buffers) is a plain
 * CPU copy, which lands.  Returns 0 on success, -1 if a DDR write cannot be
 * verified within the retry budget. */
int sdhci_platform_block_copy(void *dst, const void *src, uint32_t len)
{
    volatile const uint8_t *ncv;
    const uint8_t *s = (const uint8_t *)src;
    int retry;
    int mism;
    uint32_t k;

    if (((uintptr_t)dst & 0xF0000000UL) != 0x80000000UL) {
        memcpy(dst, src, len);
        return 0;
    }
    ncv = (volatile const uint8_t *)((uintptr_t)dst | 0x40000000UL);
    mism = 1;
    for (retry = 0; retry < 8 && mism != 0; retry++) {
        /* The read-back verify below is the authoritative success check, so
         * a PDMA-engine error is caught there and retried like any drop. */
        (void)mpfs_pdma_memcpy(dst, src, len);
        sdhci_platform_wdt_pet();
        __asm__ volatile("fence iorw,iorw" ::: "memory");
        mism = 0;
        for (k = 0; k < len; k++) {
            if (ncv[k] != s[k]) {
                mism = 1;
                break;
            }
        }
    }
    if (mism != 0) {
        return -1;
    }
    return 0;
}
#endif /* SDHCI_BLOCK_VIA_PDMA */

void sdhci_platform_init(void)
{
    /* IOMUX/MSSIO routing was already programmed in nwc_init() before the
     * MSSIO_CONTROL_CR 4-phase sequence committed the pad config.  Here:
     *   1. Configure the MMC AXI master MPU so the controller can access
     *      the regions Libero expects (default state may be all-deny).
     *   2. Enable the MMC peripheral clock.
     *   3. Deassert the MMC soft reset.
     * Mirrors the DDRC sequence in mpfs_ddr_init(). */
    wolfBoot_printf("SDHCI: platform init\n");
#ifdef MPFS_DDR_INIT
    /* MMC AXI master MPU config requires LIBERO_SETTING_MMC_MPU_CFG_*
     * which is only defined when LIBERO_FPGA_CONFIG_DIR is set.  When
     * not set, this build is intended to run UNDER HSS (which has
     * already configured the MPU), so this step is a no-op. */
    mpfs_mpu_init_mmc();
#endif
    SYSREG_REG(SYSREG_SUBBLK_CLOCK_CR_OFF) |= MSS_PERIPH_MMC;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    SYSREG_SOFT_RESET_CR &= ~MSS_PERIPH_MMC;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

void sdhci_platform_irq_init(void)
{
#ifdef DEBUG_SDHCI
    extern unsigned long get_boot_hartid(void);
#endif

    /* Set priority for MMC main interrupt */
    plic_set_priority(PLIC_INT_MMC_MAIN, PLIC_PRIORITY_DEFAULT);

    /* Set threshold to 0 (allow all priorities > 0) */
    plic_set_threshold(0);

    /* Enable MMC interrupt for this hart */
    plic_enable_interrupt(PLIC_INT_MMC_MAIN);

#ifdef DEBUG_SDHCI
    wolfBoot_printf("sdhci_platform_irq_init: hart %lu, context %u, irq %u enabled\n",
        get_boot_hartid(), (unsigned)plic_get_context(),
        (unsigned)PLIC_INT_MMC_MAIN);
#endif
}

void sdhci_platform_set_bus_mode(int is_emmc)
{
    (void)is_emmc;
}

uint32_t sdhci_reg_read(uint32_t offset)
{
    return *((volatile uint32_t*)(EMMC_SD_BASE + offset));
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    *((volatile uint32_t*)(EMMC_SD_BASE + offset)) = val;
}
#endif /* DISK_SDCARD || DISK_EMMC */

/* DEBUG UART */
#ifdef DEBUG_UART

/* Baud divisor: integer = PCLK/(baudrate*16), fractional (0-63) via 128x
 * scaling.  Uses the RUNTIME APB clock so divisors computed after the MSS
 * PLL raise stay correct (the compile-time MSS_APB_AHB_CLK garbled every
 * post-raise reinit). */
static void uart_config_baud(unsigned long base, uint32_t baudrate)
{
    const uint64_t pclk = mpfs_apb_clk_hz;
    uint32_t div_x128 = (uint32_t)((8UL * pclk) / baudrate);
    uint32_t div_x64  = div_x128 / 2u;
    uint32_t div_int  = div_x64 / 64u;
    uint32_t div_frac = div_x64 - (div_int * 64u);
    div_frac += (div_x128 - (div_int * 128u)) - (div_frac * 2u);
    if (div_frac > 63u)
        div_frac = 63u;
    if (div_int > (uint32_t)UINT16_MAX)
        return;
    MMUART_LCR(base) |= DLAB_MASK;
    MMUART_DMR(base) = (uint8_t)(div_int >> 8);
    MMUART_DLR(base) = (uint8_t)div_int;
    MMUART_LCR(base) &= ~DLAB_MASK;
    if (div_int > 1u) {
        MMUART_MM0(base) |= EFBR_MASK;
        MMUART_DFR(base) = (uint8_t)div_frac;
    } else {
        MMUART_MM0(base) &= ~EFBR_MASK;
    }
}

static void uart_init_base(unsigned long base)
{
    MMUART_MM0(base) &= ~ELIN_MASK;
    MMUART_MM1(base) &= ~EIRD_MASK;
    MMUART_MM2(base) &= ~EERR_MASK;
    MMUART_IER(base)  = 0u;
    MMUART_FCR(base)  = CLEAR_RX_FIFO_MASK | CLEAR_TX_FIFO_MASK | RXRDY_TXRDYN_EN_MASK;
    MMUART_MCR(base) &= ~(LOOP_MASK | RLOOP_MASK);
    MMUART_MCR(base) |= RTS_MASK;  /* Assert RTS -- required for USB-UART bridge CTS */
    MMUART_MM1(base) &= ~(E_MSB_TX_MASK | E_MSB_RX_MASK);
    MMUART_MM2(base) &= ~(EAFM_MASK | ESWM_MASK);
    MMUART_MM0(base) &= ~(ETTG_MASK | ERTO_MASK | EFBR_MASK);
    MMUART_GFR(base)  = 0u;
    MMUART_TTG(base)  = 0u;
    MMUART_RTO(base)  = 0u;
    uart_config_baud(base, 115200);
    MMUART_LCR(base)  = MSS_UART_DATA_8_BITS | MSS_UART_NO_PARITY | MSS_UART_ONE_STOP_BIT;
}

void uart_init(void)
{
    uart_init_base(DEBUG_UART_BASE);
}

void uart_write(const char* buf, unsigned int sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') {
            while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE) == 0);
            MMUART_THR(DEBUG_UART_BASE) = '\r';
        }
        while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE) == 0);
        MMUART_THR(DEBUG_UART_BASE) = c;
    }
}

#ifdef WOLFBOOT_RISCV_MMODE
/* Reinitialize the UART baud divisor after mss_pll_init() raises the
 * APB clock (the divisor was computed for the 40 MHz boot clock). */
void hal_uart_reinit(void)
{
    /* mpfs_apb_clk_hz was updated by mss_pll_init; just reprogram the
     * divisor (uart_config_baud reads the runtime APB clock). */
    uart_config_baud(DEBUG_UART_BASE, 115200);
}
#endif /* WOLFBOOT_RISCV_MMODE */
#endif /* DEBUG_UART */

