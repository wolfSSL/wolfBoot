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
#if defined(SNVM_KEYSTORE_PROVISION) || defined(SNVM_ENCKEY_PROVISION) || \
    defined(MPFS_SBI_VENDOR) || defined(MPFS_SNVM_DUMP)
#include "mpfs250_snvm.h"
#endif
#if defined(MPFS_SBI_VENDOR) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
#include "riscv_sbi.h"
#include <wolfssl/wolfcrypt/memory.h> /* wc_ForceZero */
#endif

#ifdef MPFS_ATHENA
#include <wolfssl/wolfcrypt/cryptocb.h>
#include "mpfs250_cal.h"        /* drives the Athena F5200 */
#endif

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

#ifdef MPFS_ENVM_MANIFEST
/* Last eNVM page: the signed manifest of this image, written in after the
 * link by `make wolfboot_envm_signed.bin` and checked by the boot mode 2
 * stub (hal/mpfs250_ubl.c).  Erased-state fill until then. */
const uint8_t __attribute__((section(".envm_manifest"), used))
    mpfs_envm_manifest[MPFS_ENVM_MANIFEST_SIZE] =
        { [0 ... MPFS_ENVM_MANIFEST_SIZE - 1] = 0xFF };
#endif

#ifdef WOLFBOOT_RISCV_MMODE
static const unsigned long mpfs_wdt_bases[MPFS_NUM_HARTS] = {
    MSS_WDT_E51_BASE, MSS_WDT_U54_1_BASE, MSS_WDT_U54_2_BASE,
    MSS_WDT_U54_3_BASE, MSS_WDT_U54_4_BASE
};
#endif

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

/* Watchdog policy.  The five MSS watchdogs are configured by the bitstream at
 * design time and are already counting when wolfBoot starts; software cannot
 * stop them and cannot usefully retime them.  On the Video Kit they read
 * TIME 0xFFFFF0, MVRP 0x989680, TRIGGER 0x3E8, CONTROL 0x02 (timeout
 * interrupt enabled, forbidden window off), which is the longest window the
 * 24-bit counter allows: the counter runs at the APB clock divided by 256, so
 * a reload lasts about 28.6 s and the TRIGGER crossing arrives just before
 * that.  wolfBoot's whole job is therefore to refresh them, here and from the
 * parked E51 after hand-off (hal_smode_boot).
 *
 * Retiming is not available even in principle: the first write to any of
 * offsets 0x00-0x0C locks TRIGGER, MVRP and TIME until the next system reset
 * (mss_watchdog.c MSS_WD_configure), and a refresh is such a write.  So is
 * disarming: there is no enable bit, and CONTROL bit 5 is not DEVRST - DEVRST
 * is STATUS bit 5, read-only.  WATCHDOG therefore has no timeout knob on this
 * target; it selects only whether wolfBoot refreshes during its own run. */

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
 * They live in the E51 DTIM, not in L2-scratch BSS: a cacheable store to
 * the scratchpad can be lost on cache-line eviction, so a cross-hart
 * mailbox written by a U54 would never become visible.  The DTIM is
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
 * This path must stay free of UART access: the secondaries reach it via
 * the kernel's HSM hart_start IPI and have to respond inside the kernel's
 * 1 s online window, which spinning on LSR can overrun. */
void secondary_hart_entry(unsigned long hartid, HLS_DATA* hls)
{
    (void)hls;

    while (1) {
#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
        /* Check the hand-off context before sleeping: the eNVM wake path
         * has already consumed the release IPI (MSIP cleared), so a
         * wfi-first loop would sleep through a staged hand-off. */
        if (hartid < (unsigned long)MPFS_NUM_HARTS &&
            mpfs_kernel_handoff[hartid].marker
                == MPFS_KERNEL_HANDOFF_MARKER) {
            unsigned long kentry;
            unsigned long opq;
            /* Acquire fence, paired with the writer's release fence:
             * without it RVWMO permits stale entry / opaque values read
             * before marker was published. */
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
#ifndef MPFS_WDT_NODES_KEEP
    unsigned int w;
#endif

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

    /* Park the E51 as the platform monitor, taking over HSS's
     * watchdog-service role: doze on the machine timer and refresh all five
     * MSS watchdogs every cycle.  They count and reset the chip on timeout
     * (~28 s at the boot-time settings) even with CONTROL=0, so something
     * has to refresh them while the OS boots and runs; the dtb fixup
     * disables the OS watchdog driver so it cannot also claim them, and
     * with MVRP at maximum a refresh is always permitted.
     *
     * Only MTIE is enabled and mstatus.MIE stays clear, so the pending
     * timer wakes WFI without vectoring into the trap path. */
    __asm__ volatile("csrw mie, %0" :: "r"(0x80UL)); /* MTIE */
    while (1) {
#ifndef MPFS_WDT_NODES_KEEP
        for (w = 0; w < 5U; w++) {
            MSS_WDT_REFRESH(mpfs_wdt_bases[w]) = 0xDEADC0DEU;
        }
#endif
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

#ifdef MPFS_ATHENA
/* Ungate and release the Athena F5200 (sequence per HSS opensbi_crypto_ecall.c;
 * CRYPTO_CR_INFO.MSS_MODE is informational, software does the un-reset).
 * Idempotent: ATHENA_CR_RESET after CALIni() silently kills AES, not hashing.
 * The SCA stall countermeasure (Security UG Table 7-7/7-8) is always on at
 * rate 0 (1 in 8); DPA resistance is what the "S" part is for.  rdcycle, not
 * mcycle: an mcycle read traps in the S-mode builds. */
static void mpfs_athena_enable(void)
{
    static int athena_enabled;
    uint64_t cyc;

    if (athena_enabled) {
        return;
    }

    SYSREG_SUBBLK_CLOCK_CR |= MSS_PERIPH_ATHENA;
    SYSREG_SOFT_RESET_CR &= ~MSS_PERIPH_ATHENA;
    __asm__ volatile("rdcycle %0" : "=r"(cyc));
    ATHENA_STALL_CR = (uint32_t)(cyc ^ (cyc >> 32));
    ATHENA_CR = ATHENA_CR_RESET | ATHENA_CR_RINGOSCON;
    ATHENA_CR = ATHENA_CR_RINGOSCON | ATHENA_CR_STALL_EN;
    athena_enabled = 1;
}

/* CAL finds the engine through this global: the User Crypto base in the Libero
 * design.  The HSS build has it compiled in and ignores this symbol. */
uint32_t g_user_crypto_base_addr = 0x22000000UL;

/* Hash context handed to CAL; sized for SATRESCONTEXT and checked below so a
 * CAL update cannot silently overflow it. */
#define MPFS_ATHENA_CTX_SIZE 256
typedef char athena_ctx_size_check[
    (MPFS_ATHENA_CTX_SIZE >= (int)sizeof(SATRESCONTEXT)) ? 1 : -1];

static int mpfs_athena_engine_init(void)
{
    /* CALIni() must follow the un-reset, and the core must not be reset after
     * it: that silently kills AES (writes nothing, returns success). */
    mpfs_athena_enable();
    if (CALIni() != SATR_SUCCESS) {
        return -1;
    }
    return 0;
}

/* CAL status of the last call that failed, reported by the self-check. */
static int athena_last_cal_rc;

static int mpfs_athena_sha384_init(void *ctx)
{
    SATR rc = CALHashCtxIni((SATRESCONTEXTPTR)ctx, SATHASHTYPE_SHA384);

    if (rc != SATR_SUCCESS) {
        athena_last_cal_rc = (int)rc;
        return -1;
    }
    return 0;
}

/* len must be a whole number of blocks; a partial non-final chunk is rejected
 * by the engine with SATR_BADHASHLEN. */
static int mpfs_athena_sha384_update(void *ctx, const void *in, uint32_t len)
{
    SATR rc = CALHashCtx((SATRESHANDLE)0, (SATRESCONTEXTPTR)ctx, in,
        (SATUINT32_t)len, NULL, SAT_FALSE);

    if (rc != SATR_SUCCESS) {
        athena_last_cal_rc = (int)rc;
        return -1;
    }
    return 0;
}

/* The final call may carry any remaining length, including zero. */
static int mpfs_athena_sha384_final(void *ctx, const void *in, uint32_t len,
    void *digest)
{
    SATR rc = CALHashCtx((SATRESHANDLE)0, (SATRESCONTEXTPTR)ctx, in,
        (SATUINT32_t)len, digest, SAT_TRUE);

    if (rc != SATR_SUCCESS) {
        athena_last_cal_rc = (int)rc;
        return -1;
    }
    return 0;
}

#ifdef MPFS_ATHENA_AES
/* AES-256-CTR. CTR is symmetric, so this serves encrypt and decrypt alike.
 * iv is advanced in place by the engine, so a caller may chain calls. */
static int mpfs_athena_aes256_ctr(const void *key, void *iv, const void *in,
    void *out, uint32_t len)
{
    if (CALSymEncrypt(SATSYMTYPE_AES256, (const SATUINT32_t *)key,
            SATSYMMODE_CTR, iv, SAT_TRUE, in, out, (SATUINT32_t)len)
            != SATR_SUCCESS) {
        return -1;
    }
    /* CALSymEncrypt only starts the transfer.  Without this wait the output
     * buffer is never written while both calls still report SATR_SUCCESS. */
    if (CALSymTrfRes(SAT_TRUE) != SATR_SUCCESS) {
        return -1;
    }
    return 0;
}
#endif /* MPFS_ATHENA_AES */
#endif /* MPFS_ATHENA */

#ifdef MPFS_ATHENA
/* SHA-384 + AES-256-CTR via the plain-C mpfs_athena_* API (CAL headers cannot
 * share a TU with wolfSSL's).  Undocumented CAL contract: a non-final update
 * must be whole 128-byte blocks, so short tails need the buffer below. */
#define ATHENA_SHA384_BLOCK 128
#define ATHENA_HASH_SLOTS   2

struct athena_hash_slot {
    void *owner;                        /* wc_Sha384* that owns this slot */
    uint8_t ctx[MPFS_ATHENA_CTX_SIZE];  /* opaque CAL hash context */
    uint8_t part[ATHENA_SHA384_BLOCK];  /* partial block awaiting more data */
    uint32_t part_len;
    uint8_t failed;                     /* an update faulted: no digest from
                                         * this context can be trusted */
};

static struct athena_hash_slot athena_hash_slots[ATHENA_HASH_SLOTS];

/* Set once the self-check has passed: from then on a SHA-384 request the
 * engine cannot take is a hardware error, never a silent software fallback. */
static int athena_ready;

/* Counts callback entries actually serviced by the engine, so a correct
 * digest cannot be mistaken for hardware use when the callback never ran. */
static uint32_t athena_cb_calls;

/* Keyed by wc_Sha384 address; the cryptocb has no free event, so a context
 * freed without Final strands its slot and a later one at that address would
 * resume stale CAL state (wrong digest).  Only reachable via ONESHOT today. */
static struct athena_hash_slot *athena_slot_find(void *owner)
{
    int i;

    for (i = 0; i < ATHENA_HASH_SLOTS; i++) {
        if (athena_hash_slots[i].owner == owner) {
            return &athena_hash_slots[i];
        }
    }
    return NULL;
}

/* Feed whole blocks from the slot buffer plus the caller's data.  Anything
 * short of a block is retained for the next call or the final. */
static int athena_hash_feed(struct athena_hash_slot *slot, const uint8_t *in,
    uint32_t len)
{
    uint32_t take;

    if (slot->part_len > 0) {
        take = ATHENA_SHA384_BLOCK - slot->part_len;
        if (take > len) {
            take = len;
        }
        memcpy(&slot->part[slot->part_len], in, take);
        slot->part_len += take;
        in += take;
        len -= take;
        if (slot->part_len < ATHENA_SHA384_BLOCK) {
            return 0;               /* still short of a block */
        }
        if (mpfs_athena_sha384_update(slot->ctx, slot->part,
                ATHENA_SHA384_BLOCK) != 0) {
            return -1;
        }
        slot->part_len = 0;
    }

    take = len - (len % ATHENA_SHA384_BLOCK);
    if (take > 0) {
        if (mpfs_athena_sha384_update(slot->ctx, in, take) != 0) {
            return -1;
        }
        in += take;
        len -= take;
    }

    if (len > 0) {
        memcpy(slot->part, in, len);
        slot->part_len = len;
    }
    return 0;
}

#if defined(MPFS_ATHENA_AES) && defined(WOLFSSL_AES_COUNTER) && !defined(NO_AES)
/* AES-256-CTR, one path for both directions.  Key from aes->devKey, not the
 * WOLF_CRYPTO_CB_SETKEY hook: succeeding there strands the software fallback.
 * All bail-outs precede any data entering the engine (counter would desync). */
static int athena_aesctr(wc_CryptoInfo *info)
{
    Aes *aes;
    word32 sz;
    word32 whole;

    if (info->cipher.type != WC_CIPHER_AES_CTR) {
        return CRYPTOCB_UNAVAILABLE;
    }
    aes = info->cipher.aesctr.aes;
    if (aes == NULL) {
        return CRYPTOCB_UNAVAILABLE;
    }
    /* AES-256 only; the engine supports 128/192 but wolfBoot's encrypted
     * images are AES-256 and an untested path is worse than none. */
    if (aes->keylen != 32) {
        return CRYPTOCB_UNAVAILABLE;
    }
    /* aes->left != 0 means wolfCrypt holds keystream from a previous partial
     * block; picking up mid-keystream would desync the counter. */
    if (aes->left != 0) {
        return CRYPTOCB_UNAVAILABLE;
    }
    sz = info->cipher.aesctr.sz;
    if (sz == 0) {
        return 0;
    }

    /* The engine advances the counter in place, so aes->reg stays correct for
     * any subsequent call on the same context. */
    whole = sz - (sz % WC_AES_BLOCK_SIZE);
    if (whole > 0) {
        if (mpfs_athena_aes256_ctr(aes->devKey, aes->reg,
                info->cipher.aesctr.in, info->cipher.aesctr.out,
                whole) != 0) {
            return WC_HW_E;
        }
    }
    if (whole < sz) {
        /* Trailing partial block: take one keystream block from the engine
         * and leave the unused bytes where wolfCrypt keeps its own (aes->tmp
         * with aes->left counting them), so the next call on this context
         * continues the stream whichever path serves it. */
        uint8_t ks[WC_AES_BLOCK_SIZE];
        word32 i;

        memset(ks, 0, sizeof(ks));
        if (mpfs_athena_aes256_ctr(aes->devKey, aes->reg, ks, ks,
                WC_AES_BLOCK_SIZE) != 0) {
            return WC_HW_E;
        }
        for (i = whole; i < sz; i++) {
            info->cipher.aesctr.out[i] = info->cipher.aesctr.in[i] ^
                ks[i - whole];
        }
        memcpy(aes->tmp, ks, WC_AES_BLOCK_SIZE);
        aes->left = WC_AES_BLOCK_SIZE - (sz - whole);
        memset(ks, 0, sizeof(ks));
    }
    athena_cb_calls++;
    return 0;
}
#endif /* MPFS_ATHENA_AES && WOLFSSL_AES_COUNTER && !NO_AES */

static int mpfs_athena_cryptocb(int devIdArg, wc_CryptoInfo *info, void *ctx)
{
    struct athena_hash_slot *slot;

    (void)devIdArg;
    (void)ctx;

    if (info == NULL) {
        return CRYPTOCB_UNAVAILABLE;
    }

#if defined(MPFS_ATHENA_AES) && defined(WOLFSSL_AES_COUNTER) && !defined(NO_AES)
    if (info->algo_type == WC_ALGO_TYPE_CIPHER) {
        return athena_aesctr(info);
    }
#endif

    if (info->algo_type != WC_ALGO_TYPE_HASH ||
            info->hash.type != WC_HASH_TYPE_SHA384) {
        return CRYPTOCB_UNAVAILABLE;
    }

    slot = athena_slot_find(info->hash.sha384);
    if ((slot != NULL) && (info->hash.sha384->devCtx != slot)) {
        /* Same address, but a context initialized after the one that held
         * the slot was abandoned without a final: start over. */
        slot->owner = NULL;
        slot->failed = 0;
        slot = NULL;
    }
    if ((slot != NULL) && (slot->failed != 0)) {
        /* An earlier update on this context faulted, so the engine holds a
         * partial digest.  Starting a fresh one here would digest only the
         * data that follows the fault; callers that ignore an update's
         * return code would then act on it. */
        return WC_HW_E;
    }
    if (slot == NULL) {
        /* First call: the only safe point to decline, since afterwards
         * wolfCrypt's own state is incomplete and the digest would be wrong. */
        slot = athena_slot_find(NULL);
        if (slot == NULL) {
            return athena_ready ? WC_HW_E : CRYPTOCB_UNAVAILABLE;
        }
        if (mpfs_athena_sha384_init(slot->ctx) != 0) {
            return athena_ready ? WC_HW_E : CRYPTOCB_UNAVAILABLE;
        }
        slot->owner = info->hash.sha384;
        info->hash.sha384->devCtx = slot;
        slot->part_len = 0;
        slot->failed = 0;
    }

    if (info->hash.in != NULL && info->hash.inSz > 0) {
        if (athena_hash_feed(slot, info->hash.in, info->hash.inSz) != 0) {
            slot->failed = 1;
            return WC_HW_E;
        }
    }

    athena_cb_calls++;

    if (info->hash.digest != NULL) {
        int ret = mpfs_athena_sha384_final(slot->ctx, slot->part,
            slot->part_len, info->hash.digest);
        slot->owner = NULL;         /* release for reuse */
        slot->part_len = 0;
        slot->failed = 0;
        if (ret != 0) {
            return WC_HW_E;
        }
    }

    return 0;
}

/* Known answers for the self-check below.  SHA-384 of the bytes 0x00..0xFF:
 * a multi-block vector, hashed in chunks that straddle the 128-byte block so
 * the intermediate state has to be carried across calls. */
static const uint8_t athena_kat_sha384_256b[48] = {
    0xff, 0xda, 0xeb, 0xff, 0x65, 0xed, 0x05, 0xcf,
    0x40, 0x0f, 0x02, 0x21, 0xc4, 0xcc, 0xfb, 0x4b,
    0x21, 0x04, 0xfb, 0x6a, 0x51, 0xf8, 0x7e, 0x40,
    0xbe, 0x6c, 0x43, 0x09, 0x38, 0x6b, 0xfd, 0xec,
    0x28, 0x92, 0xe9, 0x17, 0x9b, 0x34, 0x63, 0x23,
    0x31, 0xa5, 0x95, 0x92, 0x73, 0x7d, 0xb5, 0xc5
};
#ifdef MPFS_ATHENA_AES
/* AES-256-CTR of the bytes 0x00..0x1F under key 0x00..0x1F, counter 0x00..0x0F. */
static const uint8_t athena_kat_aesctr[32] = {
    0x5a, 0x6f, 0x06, 0x54, 0x0c, 0xfe, 0x77, 0x91,
    0xf8, 0x27, 0x5f, 0x36, 0x0e, 0xce, 0xa8, 0x9d,
    0x70, 0xe2, 0x02, 0xc6, 0xd7, 0x90, 0x4e, 0x4a,
    0x4d, 0x0f, 0xe1, 0x4a, 0x6e, 0xf8, 0x3e, 0xd0
};
#endif

/* Run the offload through the wolfCrypt callback path once and compare with
 * the known answers.  The callback entry count is part of the check: wolfCrypt
 * falls back to software silently when no device serves a call, and a correct
 * result alone would not show that the hardware ran. */
/* What the self-check tripped on, for the panic message. */
static const char *athena_fail_what;
static int athena_fail_rc;

#ifdef MPFS_ATHENA_AES
/* One CTR run from a fresh key schedule, split into one or two calls so the
 * self-check can prove the counter carries across them. */
static int athena_ctr_run(const uint8_t *key, const uint8_t *ctr,
    const uint8_t *in, uint8_t *out, uint32_t first, uint32_t second)
{
    Aes aes;
    int ret;

    ret = wc_AesInit(&aes, NULL, WOLFBOOT_DEVID_CRYPT);
    if (ret == 0) {
        ret = wc_AesSetKeyDirect(&aes, key, 32, ctr, AES_ENCRYPTION);
    }
    if (ret == 0) {
        ret = wc_AesCtrEncrypt(&aes, out, in, first);
    }
    if ((ret == 0) && (second > 0u)) {
        ret = wc_AesCtrEncrypt(&aes, out + first, in + first, second);
    }
    wc_AesFree(&aes);
    return ret;
}
#endif

static int mpfs_athena_selfcheck(void)
{
    wc_Sha384 sha;
    uint8_t buf[256];
    uint8_t digest[48];
#ifdef MPFS_ATHENA_AES
    uint8_t key[32], ctr[16], out[32], out2[32];
#endif
    int ret;
    int i;

    athena_fail_what = NULL;
    athena_fail_rc = 0;
    athena_last_cal_rc = 0;

    for (i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = (uint8_t)i;
    }
    athena_cb_calls = 0;
    ret = wc_InitSha384_ex(&sha, NULL, WOLFBOOT_DEVID_HASH);
    if (ret != 0) {
        athena_fail_what = "SHA-384 init";
    }
    if (ret == 0) {
        ret = wc_Sha384Update(&sha, buf, 100);
    }
    if (ret == 0) {
        ret = wc_Sha384Update(&sha, &buf[100], 100);
    }
    if (ret == 0) {
        ret = wc_Sha384Update(&sha, &buf[200], 56);
    }
    if ((ret != 0) && (athena_fail_what == NULL)) {
        athena_fail_what = "SHA-384 update";
    }
    if (ret == 0) {
        ret = wc_Sha384Final(&sha, digest);
        if (ret != 0) {
            athena_fail_what = "SHA-384 final";
        }
    }
    wc_Sha384Free(&sha);
    if (ret != 0) {
        athena_fail_rc = ret;
        return -1;
    }
    if (athena_cb_calls == 0) {
        athena_fail_what = "SHA-384 callback never serviced (software fallback)";
        return -1;
    }
    if (memcmp(digest, athena_kat_sha384_256b, sizeof(digest)) != 0) {
        athena_fail_what = "SHA-384 digest mismatch";
        return -1;
    }

#ifdef MPFS_ATHENA_AES
    for (i = 0; i < 32; i++) {
        key[i] = (uint8_t)i;
    }
    for (i = 0; i < 16; i++) {
        ctr[i] = (uint8_t)i;
    }
    athena_cb_calls = 0;
    ret = athena_ctr_run(key, ctr, buf, out, 32, 0);
    if (ret != 0) {
        athena_fail_what = "AES-256-CTR";
        athena_fail_rc = ret;
        return -1;
    }
    if (athena_cb_calls == 0) {
        athena_fail_what = "AES-256-CTR callback never serviced (software "
                           "fallback)";
        return -1;
    }
    if (memcmp(out, athena_kat_aesctr, sizeof(out)) != 0) {
        athena_fail_what = "AES-256-CTR known-answer mismatch";
        return -1;
    }
    /* The disk decrypt path calls CTR once per chunk, so the counter has to
     * carry across calls: two 16-byte calls must equal one 32-byte call, and
     * a split that is not a whole number of blocks must too. */
    ret = athena_ctr_run(key, ctr, buf, out2, 16, 16);
    if (ret == 0) {
        if (memcmp(out, out2, sizeof(out)) != 0) {
            athena_fail_what = "AES-256-CTR counter does not carry across "
                               "calls";
            return -1;
        }
        ret = athena_ctr_run(key, ctr, buf, out2, 20, 12);
    }
    if (ret != 0) {
        athena_fail_what = "AES-256-CTR split run";
        athena_fail_rc = ret;
        return -1;
    }
    if (memcmp(out2, athena_kat_aesctr, sizeof(out2)) != 0) {
        athena_fail_what = "AES-256-CTR partial block mismatch";
        return -1;
    }
#endif
    return 0;
}

/* A build that asks for the hardware must not fall through to software
 * crypto unnoticed: every failure here halts. */
static void mpfs_athena_init(void)
{
    int i;

    for (i = 0; i < ATHENA_HASH_SLOTS; i++) {
        athena_hash_slots[i].owner = NULL;
        athena_hash_slots[i].part_len = 0;
    }

    /* Free slots hold INVALID_DEVID (-2), not 0, so registration returns
     * BUFFER_E until the zero-initialised device table is initialised. */
    wc_CryptoCb_Init();

    if (mpfs_athena_engine_init() != 0) {
        wolfBoot_printf("Athena: engine init failed\n");
        wolfBoot_panic();
    }
    if (wc_CryptoCb_RegisterDevice(WOLFBOOT_DEVID_HASH, mpfs_athena_cryptocb,
            NULL) != 0) {
        wolfBoot_printf("Athena: RegisterDevice failed\n");
        wolfBoot_panic();
    }
#if defined(WOLFBOOT_DEVID_CRYPT) && (WOLFBOOT_DEVID_CRYPT != WOLFBOOT_DEVID_HASH)
    if (wc_CryptoCb_RegisterDevice(WOLFBOOT_DEVID_CRYPT, mpfs_athena_cryptocb,
            NULL) != 0) {
        wolfBoot_printf("Athena: cipher RegisterDevice failed\n");
        wolfBoot_panic();
    }
#endif
    if (mpfs_athena_selfcheck() != 0) {
        wolfBoot_printf("Athena: self-check failed: %s (rc %d, CAL rc %d, "
            "callbacks %u)\n", athena_fail_what, athena_fail_rc,
            athena_last_cal_rc, (unsigned)athena_cb_calls);
        wolfBoot_panic();
    }
    athena_ready = 1;
#ifdef MPFS_ATHENA_AES
    wolfBoot_printf("Athena: SHA-384 + AES-256-CTR offload active\n");
#else
    wolfBoot_printf("Athena: SHA-384 offload active\n");
#endif
}
#endif /* MPFS_ATHENA */


#ifdef WOLFBOOT_RISCV_MMODE
/* Snapshot the boot ROM watchdog state and the reset cause for printing once
 * the UART is up, then set the watchdogs up for the life of wolfBoot. */
static void mpfs_wdt_boot_config(void)
{
    volatile uint32_t *wdt_e51 = (volatile uint32_t *)MSS_WDT_E51_BASE;
    volatile uint32_t *reset_sr = (volatile uint32_t *)MPFS_SYSREG_RESET_SR;
    unsigned int i;

    for (i = 0; i < sizeof(mpfs_boot_wdt_snap) / sizeof(uint32_t); i++) {
        mpfs_boot_wdt_snap[i] = wdt_e51[i];
    }
    /* Read, then zero, so the next reset is captured: this register is not
     * write-1-to-clear, and writing the value back would set those bits. */
    mpfs_boot_reset_sr_snap = *reset_sr;
    *reset_sr = 0U;

    /* Start every watchdog's window from full.  The design-time window is far
     * longer than the ~5 s ECDSA P-384 verify, so nothing has to refresh
     * mid-signature; hal_prepare_boot() refreshes again before the hand-off. */
    for (i = 0; i < MPFS_NUM_HARTS; i++) {
        MSS_WDT_REFRESH(mpfs_wdt_bases[i]) = 0xDEADC0DEU;
    }
}

#ifdef MPFS_DDR_INIT
/* Bring up LPDDR4, or halt.  Each mpfs_ddr_init() pulses the SYSREG DDRC soft
 * reset, which is the only way a rejected training recovers.  There is no safe
 * path past a failure: WOLFBOOT_LOAD_ADDRESS is in DDR, so a later load would
 * write to a dead controller and hang inside the AXI master. */
static void mpfs_ddr_bringup(void)
{
    unsigned int retry;

    for (retry = 0; retry < MPFS_DDR_MAX_OUTER_RETRY; retry++) {
        if (retry > 0) {
            wolfBoot_printf("DDR: Outer retry %u/%u (full DDRC re-init)\n",
                retry, MPFS_DDR_MAX_OUTER_RETRY);
        }
        if (mpfs_ddr_init(retry) == 0) {
            return;
        }
    }
    wolfBoot_printf(
        "DDR: Init FAILED after %u outer retries -- halting.\n"
        "DDR: Power-cycle the board and retry.\n",
        MPFS_DDR_MAX_OUTER_RETRY);
    while (1) {
        /* spin */
    }
}
#endif /* MPFS_DDR_INIT */
#endif /* WOLFBOOT_RISCV_MMODE */

void hal_init(void)
{
#ifdef WOLFBOOT_RISCV_MMODE
    int h;
#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
    volatile uint32_t *dtim = (volatile uint32_t *)0x01000000UL;
    unsigned int k;
#endif

    /* Park every hart's machine-timer comparator at maximum.  CLINT MTIME
     * is 0 (the RTC time base is not running yet) and mtimecmp resets to 0,
     * so MTIP is pending on every hart out of reset.  A pending interrupt
     * makes WFI return immediately, which would leave the parked secondary
     * harts spinning on their eNVM wait loop for the whole boot. */
    for (h = 0; h < MPFS_NUM_HARTS; h++) {
        CLINT_MTIMECMP_REG(h) = ~(uint64_t)0;
    }
    __asm__ volatile("fence iorw, iorw" ::: "memory");

#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
    /* Clear the DTIM-resident cross-hart state (start mailboxes + SBI
     * shared block): DTIM content is undefined at power-on. */
    for (k = 0; k < (MPFS_DTIM_BOOT_CLEAR_SIZE / sizeof(uint32_t)); k++) {
        dtim[k] = 0;
    }
    __asm__ volatile("fence iorw, iorw" ::: "memory");
#endif

    mpfs_wdt_boot_config();

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

#ifdef MPFS_ATHENA
    mpfs_athena_init();
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
    mpfs_ddr_bringup();
#endif
#endif

#ifdef MPFS_SNVM_DUMP
    mpfs_snvm_dump(MPFS_SNVM_DUMP_FIRST, MPFS_SNVM_DUMP_COUNT);
#endif

#ifdef SNVM_KEYSTORE_PROVISION
    /* One-time: write the compiled-in trust anchor into sNVM so a subsequent
     * SNVM_KEYSTORE build serves its keys from sNVM.  Stop on failure so a
     * provisioning run cannot look successful. */
    if (snvm_keystore_provision() != 0) {
        wolfBoot_printf("snvm provision: FAILED\n");
        wolfBoot_panic();
    }
#endif

#ifdef SNVM_ENCKEY_PROVISION
    /* One-time: store the PUF-wrapped image-encryption key in sNVM. */
    if (snvm_enckey_provision() != 0) {
        wolfBoot_printf("enckey provision: FAILED\n");
        wolfBoot_panic();
    }
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

/* Once the OS runs, the mailbox is driven from ecalls, where a console
 * write could block on a UART the OS has reconfigured. */
static int mpfs_scb_quiet;

/* Raise a System Controller service.  The request bytes go to mailbox word
 * 0; index is the SERVICES_CR mailbox-offset field, which carries the SPI
 * directory index for the IAP services.  Returns 0 once the request is
 * accepted, or a negative transport error. */
static int mpfs_scb_start(uint8_t opcode, uint32_t index, const uint8_t *req,
    uint32_t req_len)
{
    uint32_t cmd, words, rem, i, v;
    int timeout;

    if (mpfs_scb_mailbox_busy()) {
        if (!mpfs_scb_quiet) {
            wolfBoot_printf("SCB mailbox busy\n");
        }
        return -2;
    }

    /* Write request words into the mailbox (RMW for a non-word-aligned tail). */
    words = req_len / 4u;
    for (i = 0; i < words; i++) {
        v  =  (uint32_t)req[(i * 4u) + 0u];
        v |= ((uint32_t)req[(i * 4u) + 1u]) << 8;
        v |= ((uint32_t)req[(i * 4u) + 2u]) << 16;
        v |= ((uint32_t)req[(i * 4u) + 3u]) << 24;
        SCBMBOX_REG(i * 4u) = v;
    }
    rem = req_len - (words * 4u);
    if (rem > 0u) {
        v = SCBMBOX_REG(words * 4u);
        for (i = 0; i < rem; i++) {
            v &= ~(((uint32_t)0xFFu) << (i * 8u));
            v |= ((uint32_t)req[(words * 4u) + i]) << (i * 8u);
        }
        SCBMBOX_REG(words * 4u) = v;
    }

    /* Ensure mailbox writes land before the request is raised. */
    __asm__ volatile("fence w,w" ::: "memory");

    /* Command: opcode in [22:16] (SERVICES_CR_COMMAND_SHIFT), the mailbox
     * offset / SPI index in [15:7], REQ in bit 0. */
    cmd = (((uint32_t)(opcode & 0x7Fu)) << SERVICES_CR_COMMAND_SHIFT) |
          ((index & 0x1FFu) << SERVICES_CR_MBOX_SHIFT) |
          SERVICES_CR_REQ_MASK;
    SCBCTRL_REG(SERVICES_CR_OFFSET) = cmd;

    /* Wait for request bit to clear (command accepted). */
    timeout = MPFS_SCB_TIMEOUT;
    while ((SCBCTRL_REG(SERVICES_CR_OFFSET) & SERVICES_CR_REQ_MASK) &&
           (timeout > 0)) {
        timeout--;
    }
    if (timeout == 0) {
        if (!mpfs_scb_quiet) {
            wolfBoot_printf("SCB request timeout\n");
        }
        return -3;
    }
    return 0;
}

/* The 16-bit status of the service in progress. */
static int mpfs_scb_status(void)
{
    return (int)((SCBCTRL_REG(SERVICES_SR_OFFSET) >> SERVICES_SR_STATUS_SHIFT)
                 & 0xFFFFu);
}

/* Wait for the service to complete.  Returns its status (0 = success) or a
 * negative transport error.  Uses the larger completion bound: nonce/PUF
 * take much longer than serial/sNVM-read. */
static int mpfs_scb_wait(void)
{
    int timeout = MPFS_SCB_BUSY_TIMEOUT;

    while (mpfs_scb_mailbox_busy() && (timeout > 0)) {
        timeout--;
    }
    if (timeout == 0) {
        if (!mpfs_scb_quiet) {
            wolfBoot_printf("SCB busy timeout\n");
        }
        return -4;
    }
    return mpfs_scb_status();
}

/* System Controller service, polling mode, mailbox word offset 0.  Returns the
 * 16-bit service status (0 = success) or a negative transport error. */
static int mpfs_scb_request(uint8_t opcode, const uint8_t *req,
    uint32_t req_len)
{
    int ret = mpfs_scb_start(opcode, 0, req, req_len);

    if (ret != 0) {
        return ret;
    }
    return mpfs_scb_wait();
}

/* Read len response bytes from the mailbox at byte offset off (mb word 0). */
static void mpfs_scb_read(uint8_t *out, uint32_t off, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        out[i] = SCBMBOX_BYTE(off + i);
    }
}

#if defined(MPFS_SBI_VENDOR) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
/* SBI vendor extension (include/riscv_sbi.h) served to the OS.  Everything
 * here runs inside an ecall on one hart with interrupts off: the System
 * Controller services that take seconds are started here and polled by the
 * OS through SC_STATUS, the short ones complete in place. */

/* The mailbox is one resource for all harts.  Taken for the life of a
 * started service, released when SC_STATUS reports it complete. */
static volatile uint32_t sbi_scb_lock;
/* Set while a started (asynchronous) service owns the lock, so SC_STATUS
 * releases only that one and never a lock a synchronous call is holding. */
static volatile uint32_t sbi_scb_started;
/* GET_KEY purposes already handed out this boot, one bit each. */
static uint32_t sbi_key_served;

static int sbi_scb_trylock(void)
{
    return (__atomic_exchange_n(&sbi_scb_lock, 1U, __ATOMIC_ACQ_REL) == 0U);
}

static void sbi_scb_unlock(void)
{
    __atomic_store_n(&sbi_scb_lock, 0U, __ATOMIC_RELEASE);
}

/* The challenge seed and hash label behind each purpose.  The OS names a
 * purpose, never a challenge, so the PUF cannot be asked for the KEK. */
static const struct {
    unsigned long purpose;
    const char *seed;
    const char *label;
} sbi_key_purposes[] = {
    { SBI_WOLF_KEY_MRAM_LOG, "wolfBoot-mram-log-v1", "wolfBoot-mram-log-v1" },
    { SBI_WOLF_KEY_VARLOG,   "varlog",               "wolfLUKS-key-v1:varlog" },
};

/* Give a claimed serving back, so a request that never handed the key over
 * does not consume it. */
static void sbi_key_unclaim(unsigned int i)
{
    (void)__atomic_fetch_and(&sbi_key_served, ~(1U << i), __ATOMIC_ACQ_REL);
}

static long sbi_vendor_get_key(unsigned long purpose, unsigned long buf,
    unsigned long len)
{
    uint8_t key[SBI_WOLF_KEY_LEN];
    unsigned int i;
    int ret;

    for (i = 0; i < sizeof(sbi_key_purposes) / sizeof(sbi_key_purposes[0]);
            i++) {
        if (sbi_key_purposes[i].purpose == purpose) {
            break;
        }
    }
    if ((i == sizeof(sbi_key_purposes) / sizeof(sbi_key_purposes[0])) ||
            (len != SBI_WOLF_KEY_LEN)) {
        return SBI_ERR_INVALID_PARAM;
    }
    if (!sbi_smode_range_ok(buf, len)) {
        return SBI_ERR_INVALID_ADDRESS;
    }
    /* Claim the serving before deriving anything.  A plain check here and a
     * set at the end would let two harts asking for the same purpose at once
     * both be served; the claim is given back below if the key never reaches
     * the caller. */
    if ((__atomic_fetch_or(&sbi_key_served, 1U << i, __ATOMIC_ACQ_REL)
            & (1U << i)) != 0U) {
        return SBI_ERR_DENIED;
    }
    if (!sbi_scb_trylock()) {
        sbi_key_unclaim(i);
        return SBI_ERR_FAILED;
    }
    ret = mpfs_puf_purpose_key(sbi_key_purposes[i].seed,
        sbi_key_purposes[i].label, key);
    sbi_scb_unlock();
    if (ret != 0) {
        wc_ForceZero(key, sizeof(key));
        sbi_key_unclaim(i);
        return SBI_ERR_FAILED;
    }
    ret = sbi_copy_to_smode(buf, key, sizeof(key));
    wc_ForceZero(key, sizeof(key));
    if (ret != 0) {
        /* the caller never saw the key, so its one serving still stands */
        sbi_key_unclaim(i);
        return SBI_ERR_INVALID_ADDRESS;
    }
    return SBI_SUCCESS;
}

/* Services that reach the System Controller are served only when the OS
 * cannot drive the mailbox itself: unfenced, its own driver would race
 * these requests. */
#ifdef MPFS_SCB_SMODE_DENY
#ifdef MPFS_SBI_SC_FLASH
/* The System Controller's SPI flash for the OS.  The fence that keeps the
 * OS off the mailbox also covers the flash controller (0x37020100), so under
 * it this is the OS's only way to stage a bitstream.  The first erase block
 * holds the SPI directory the System Controller boots from: its golden entry
 * is never written, and the block changes only through SET_IMAGE, which
 * edits the upgrade or blank entry in a copy and writes the rest back as it
 * was.  Every other offset is the OS's.  An erase call takes one block and a
 * write or read at most SBI_SMODE_BUF_MAX bytes, so an ecall stays well
 * under a second with the hart's interrupts masked.  The mailbox lock is
 * held throughout: the System Controller reads this flash for its own
 * services. */
int qspi_init(void);

#define SBI_SC_FLASH_UPGRADE_INDEX 1UL
#define SBI_SC_FLASH_BLANK_INDEX   2UL

static int sbi_sc_flash_ready;
static uint8_t sbi_sc_flash_buf[SBI_SMODE_BUF_MAX];
static uint32_t sbi_sc_flash_block[FLASH_SECTOR_SIZE / sizeof(uint32_t)];

static long sbi_sc_flash_take(void)
{
    if (!sbi_scb_trylock()) {
        return SBI_ERR_FAILED;
    }
    if (!sbi_sc_flash_ready) {
        if (qspi_init() != 0) {
            sbi_scb_unlock();
            return SBI_ERR_FAILED;
        }
        sbi_sc_flash_ready = 1;
    }
    return SBI_SUCCESS;
}

/* Inside the flash and clear of the directory block. */
static int sbi_sc_flash_range_ok(unsigned long off, unsigned long len)
{
    return (len != 0UL) && (off >= (unsigned long)FLASH_SECTOR_SIZE) &&
           (off <= (unsigned long)FLASH_DEVICE_SIZE) &&
           (len <= (unsigned long)FLASH_DEVICE_SIZE - off);
}

static long sbi_vendor_sc_flash_erase(unsigned long off, unsigned long len)
{
    long err;

    if (!sbi_sc_flash_range_ok(off, len) ||
        (len != (unsigned long)FLASH_SECTOR_SIZE) ||
        ((off & ((unsigned long)FLASH_SECTOR_SIZE - 1UL)) != 0UL)) {
        return SBI_ERR_INVALID_PARAM;
    }
    err = sbi_sc_flash_take();
    if (err != SBI_SUCCESS) {
        return err;
    }
    if (ext_flash_erase(off, (int)len) != (int)len) {
        err = SBI_ERR_FAILED;
    }
    sbi_scb_unlock();
    return err;
}

static long sbi_vendor_sc_flash_write(unsigned long off, unsigned long buf,
    unsigned long len)
{
    long err;

    if (!sbi_sc_flash_range_ok(off, len) || (len > SBI_SMODE_BUF_MAX)) {
        return SBI_ERR_INVALID_PARAM;
    }
    if (!sbi_smode_range_ok(buf, len)) {
        return SBI_ERR_INVALID_ADDRESS;
    }
    err = sbi_sc_flash_take();
    if (err != SBI_SUCCESS) {
        return err;
    }
    /* the bounce buffer is shared between harts: copy under the lock */
    if (sbi_copy_from_smode(sbi_sc_flash_buf, buf, len) != 0) {
        err = SBI_ERR_INVALID_ADDRESS;
    }
    else if (ext_flash_write(off, sbi_sc_flash_buf, (int)len) != (int)len) {
        err = SBI_ERR_FAILED;
    }
    sbi_scb_unlock();
    return err;
}

static long sbi_vendor_sc_flash_read(unsigned long off, unsigned long buf,
    unsigned long len)
{
    long err;

    if ((len == 0UL) || (len > SBI_SMODE_BUF_MAX) ||
        (off > (unsigned long)FLASH_DEVICE_SIZE) ||
        (len > (unsigned long)FLASH_DEVICE_SIZE - off)) {
        return SBI_ERR_INVALID_PARAM;
    }
    if (!sbi_smode_range_ok(buf, len)) {
        return SBI_ERR_INVALID_ADDRESS;
    }
    err = sbi_sc_flash_take();
    if (err != SBI_SUCCESS) {
        return err;
    }
    if (ext_flash_read(off, sbi_sc_flash_buf, (int)len) != (int)len) {
        err = SBI_ERR_FAILED;
    }
    else if (sbi_copy_to_smode(buf, sbi_sc_flash_buf, len) != 0) {
        err = SBI_ERR_INVALID_ADDRESS;
    }
    sbi_scb_unlock();
    return err;
}

/* Point directory entry 1 (upgrade) or 2 (blank) at an image offset.
 * Setting the upgrade entry also zeroes the blank one, as the OS driver
 * does, so a single-image System Controller does not object.  Nothing is
 * written when the entries already read that way. */
static long sbi_vendor_sc_flash_set_image(unsigned long index,
    unsigned long address)
{
    uint8_t *block = (uint8_t *)sbi_sc_flash_block;
    long err;

    if (((index != SBI_SC_FLASH_UPGRADE_INDEX) &&
         (index != SBI_SC_FLASH_BLANK_INDEX)) ||
        (address > (unsigned long)FLASH_DEVICE_SIZE)) {
        return SBI_ERR_INVALID_PARAM;
    }
    err = sbi_sc_flash_take();
    if (err != SBI_SUCCESS) {
        return err;
    }
    if (ext_flash_read(0, block, FLASH_SECTOR_SIZE) != FLASH_SECTOR_SIZE) {
        sbi_scb_unlock();
        return SBI_ERR_FAILED;
    }
    if ((sbi_sc_flash_block[index] == (uint32_t)address) &&
        ((index != SBI_SC_FLASH_UPGRADE_INDEX) ||
         (sbi_sc_flash_block[SBI_SC_FLASH_BLANK_INDEX] == 0U))) {
        sbi_scb_unlock();
        return SBI_SUCCESS;
    }
    sbi_sc_flash_block[index] = (uint32_t)address;
    if (index == SBI_SC_FLASH_UPGRADE_INDEX) {
        sbi_sc_flash_block[SBI_SC_FLASH_BLANK_INDEX] = 0U;
    }
    if ((ext_flash_erase(0, FLASH_SECTOR_SIZE) != FLASH_SECTOR_SIZE) ||
        (ext_flash_write(0, block, FLASH_SECTOR_SIZE) != FLASH_SECTOR_SIZE)) {
        err = SBI_ERR_FAILED;
    }
    sbi_scb_unlock();
    return err;
}
#endif /* MPFS_SBI_SC_FLASH */

static long sbi_vendor_sc_start(uint8_t opcode, uint32_t index,
    const uint8_t *req, uint32_t req_len)
{
    if (!sbi_scb_trylock()) {
        return SBI_ERR_FAILED;
    }
    if (mpfs_scb_start(opcode, index, req, req_len) != 0) {
        sbi_scb_unlock();
        return SBI_ERR_FAILED;
    }
    __atomic_store_n(&sbi_scb_started, 1U, __ATOMIC_RELEASE);
    return SBI_SUCCESS;
}

/* The IAP program and auto-update services restart the device on success,
 * so a return from here is the failure status. */
static long sbi_vendor_sc_run(uint8_t opcode, uint32_t index,
    unsigned long *val)
{
    long err = sbi_vendor_sc_start(opcode, index, NULL, 0);

    if (err != SBI_SUCCESS) {
        return err;
    }
    *val = (unsigned long)mpfs_scb_wait();
    __atomic_store_n(&sbi_scb_started, 0U, __ATOMIC_RELEASE);
    sbi_scb_unlock();
    return SBI_ERR_FAILED;
}
#endif

long sbi_hal_vendor_ecall(unsigned long fid, unsigned long *regs,
    unsigned long *val)
{
    unsigned long a0 = regs[SBI_REG_A0];
    unsigned long a1 = regs[SBI_REG_A1];
    unsigned long a2 = regs[SBI_REG_A2];
    long err;

    mpfs_scb_quiet = 1;
    switch (fid) {
    case SBI_WOLF_FID_GET_VERSION:
        *val = SBI_WOLF_VERSION;
        err = SBI_SUCCESS;
        break;
    case SBI_MCHP_FID_HSS_REBOOT:
        SYSREG_MSS_RESET_CR = 0xDEAD;
        while (1) { }
    case SBI_WOLF_FID_GET_SERIAL: {
        uint8_t serial[DEVICE_SERIAL_NUMBER_SIZE];
        int ret;

        if (a1 < sizeof(serial)) {
            err = SBI_ERR_INVALID_PARAM;
            break;
        }
        if (!sbi_smode_range_ok(a0, sizeof(serial))) {
            err = SBI_ERR_INVALID_ADDRESS;
            break;
        }
        if (!sbi_scb_trylock()) {
            err = SBI_ERR_FAILED;
            break;
        }
        ret = mpfs_read_serial_number(serial);
        sbi_scb_unlock();
        if (ret != 0) {
            err = SBI_ERR_FAILED;
            break;
        }
        err = (sbi_copy_to_smode(a0, serial, sizeof(serial)) == 0) ?
            SBI_SUCCESS : SBI_ERR_INVALID_ADDRESS;
        break;
    }
    case SBI_WOLF_FID_GET_KEY:
        err = sbi_vendor_get_key(a0, a1, a2);
        break;
#ifdef MPFS_SCB_SMODE_DENY
    case SBI_WOLF_FID_DIGEST_CHECK: {
        uint8_t req[4];

        req[0] = (uint8_t)a0;
        req[1] = (uint8_t)(a0 >> 8);
        req[2] = (uint8_t)(a0 >> 16);
        req[3] = (uint8_t)(a0 >> 24);
        err = sbi_vendor_sc_start(SYS_SERV_CMD_DIGEST_CHECK, 0, req,
            sizeof(req));
        break;
    }
    case SBI_WOLF_FID_IAP_AUTHENTICATE:
        err = sbi_vendor_sc_start(SYS_SERV_CMD_IAP_IMAGE_AUTH,
            (uint32_t)a0, NULL, 0);
        break;
    case SBI_WOLF_FID_IAP_VERIFY:
        err = sbi_vendor_sc_start(SYS_SERV_CMD_IAP_VERIFY_IDX,
            (uint32_t)a0, NULL, 0);
        break;
    case SBI_WOLF_FID_IAP_PROGRAM:
        err = sbi_vendor_sc_run(SYS_SERV_CMD_IAP_PROGRAM_IDX, (uint32_t)a0,
            val);
        break;
    case SBI_WOLF_FID_AUTO_UPDATE:
        err = sbi_vendor_sc_run(SYS_SERV_CMD_AUTO_UPDATE, 0, val);
        break;
#ifdef MPFS_SBI_SC_FLASH
    case SBI_WOLF_FID_SC_FLASH_ERASE:
        err = sbi_vendor_sc_flash_erase(a0, a1);
        break;
    case SBI_WOLF_FID_SC_FLASH_WRITE:
        err = sbi_vendor_sc_flash_write(a0, a1, a2);
        break;
    case SBI_WOLF_FID_SC_FLASH_READ:
        err = sbi_vendor_sc_flash_read(a0, a1, a2);
        break;
    case SBI_WOLF_FID_SC_FLASH_SET_IMAGE:
        err = sbi_vendor_sc_flash_set_image(a0, a1);
        break;
#endif
    case SBI_WOLF_FID_SC_STATUS:
        if (mpfs_scb_mailbox_busy()) {
            err = SBI_ERR_ALREADY_STARTED;
            break;
        }
        *val = (unsigned long)mpfs_scb_status() |
            ((unsigned long)SCBMBOX_REG(4) << 32);
        /* A synchronous service also leaves the mailbox idle between its
         * own requests; release only the lock a started service holds. */
        if (__atomic_exchange_n(&sbi_scb_started, 0U, __ATOMIC_ACQ_REL)
                != 0U) {
            sbi_scb_unlock();
        }
        err = SBI_SUCCESS;
        break;
#else
    case SBI_WOLF_FID_DIGEST_CHECK:
    case SBI_WOLF_FID_IAP_AUTHENTICATE:
    case SBI_WOLF_FID_IAP_VERIFY:
    case SBI_WOLF_FID_IAP_PROGRAM:
    case SBI_WOLF_FID_AUTO_UPDATE:
    case SBI_WOLF_FID_SC_FLASH_ERASE:
    case SBI_WOLF_FID_SC_FLASH_WRITE:
    case SBI_WOLF_FID_SC_FLASH_READ:
    case SBI_WOLF_FID_SC_FLASH_SET_IMAGE:
    case SBI_WOLF_FID_SC_STATUS:
        err = SBI_ERR_DENIED;
        break;
#endif
    default:
        err = SBI_ERR_NOT_SUPPORTED;
        break;
    }
    mpfs_scb_quiet = 0;
    (void)a2;
    return err;
}
#endif /* MPFS_SBI_VENDOR && WOLFBOOT_MMODE_SMODE_BOOT */

/* Read 16-byte device serial number via SCB system service (opcode 0x00). */
int mpfs_read_serial_number(uint8_t *serial)
{
    int ret;

    if (serial == NULL) {
        return -1;
    }
    ret = mpfs_scb_request(SYS_SERV_CMD_SERIAL_NUMBER, NULL, 0);
    if (ret == 0) {
        mpfs_scb_read(serial, 0, DEVICE_SERIAL_NUMBER_SIZE);
    }
    return ret;
}

int mpfs_snvm_read(uint8_t module, const uint8_t *usk, uint8_t *admin,
    uint8_t *data, uint16_t data_len)
{
    uint8_t frame[16];
    int ret, i;

    if ((data == NULL) || (module >= MPFS_SNVM_MODULE_MAX)) {
        return -1;
    }
    if ((data_len != MPFS_SNVM_AUTH_DATA_LEN) &&
        (data_len != MPFS_SNVM_PLAIN_DATA_LEN)) {
        return -1;
    }
    if ((data_len == MPFS_SNVM_AUTH_DATA_LEN) && (usk == NULL)) {
        return -1;
    }

    for (i = 0; i < (int)sizeof(frame); i++) {
        frame[i] = 0;
    }
    frame[0] = module;                       /* bytes 1..3 reserved (0) */
    if (data_len == MPFS_SNVM_AUTH_DATA_LEN) {
        for (i = 0; i < MPFS_SNVM_USK_LEN; i++) {
            frame[4 + i] = usk[i];
        }
    }

    ret = mpfs_scb_request(SYS_SERV_CMD_SNVM_READ, frame, sizeof(frame));
    if (ret != 0) {
        /* The service still returns the page admin word on a refusal, and
         * it is the only way to tell a page held as a client from one that
         * was never configured: the latter reads back as zero. */
        if (admin != NULL) {
            mpfs_scb_read(admin, MPFS_SNVM_READ_RET_OFFSET,
                MPFS_SNVM_ADMIN_LEN);
        }
        return ret;
    }

    /* Response: 4 admin bytes then data_len data bytes at READ_RET_OFFSET. */
    if (admin != NULL) {
        mpfs_scb_read(admin, MPFS_SNVM_READ_RET_OFFSET, MPFS_SNVM_ADMIN_LEN);
    }
    mpfs_scb_read(data, MPFS_SNVM_READ_RET_OFFSET + MPFS_SNVM_ADMIN_LEN,
        data_len);
    return 0;
}

int mpfs_snvm_write(uint8_t format, uint8_t module, const uint8_t *data,
    const uint8_t *usk)
{
    uint8_t frame[256];
    uint32_t datalen, total;
    int i;

    if ((data == NULL) || (module >= MPFS_SNVM_MODULE_MAX)) {
        return -1;
    }
    if (format == SYS_SERV_CMD_SNVM_WRITE_PLAIN) {
        datalen = MPFS_SNVM_PLAIN_DATA_LEN;
        total = 4u + MPFS_SNVM_PLAIN_DATA_LEN;                 /* 256 */
    }
    else if ((format == SYS_SERV_CMD_SNVM_WRITE_AUTH) ||
             (format == SYS_SERV_CMD_SNVM_WRITE_CIPHER)) {
        if (usk == NULL) {
            return -1;
        }
        datalen = MPFS_SNVM_AUTH_DATA_LEN;
        total = 4u + MPFS_SNVM_AUTH_DATA_LEN + MPFS_SNVM_USK_LEN; /* 252 */
    }
    else {
        return -1;
    }

    for (i = 0; i < (int)sizeof(frame); i++) {
        frame[i] = 0;
    }
    frame[0] = module;                       /* bytes 1..3 reserved (0) */
    for (i = 0; i < (int)datalen; i++) {
        frame[4 + i] = data[i];
    }
    if (datalen == MPFS_SNVM_AUTH_DATA_LEN) {
        for (i = 0; i < MPFS_SNVM_USK_LEN; i++) {
            frame[4 + MPFS_SNVM_AUTH_DATA_LEN + i] = usk[i];
        }
    }

    return mpfs_scb_request(format, frame, total);
}

int mpfs_puf_emulation(const uint8_t *challenge, uint8_t op_type,
    uint8_t *response)
{
    uint8_t frame[20];
    int ret, i;

    if ((challenge == NULL) || (response == NULL)) {
        return -1;
    }
    for (i = 0; i < (int)sizeof(frame); i++) {
        frame[i] = 0;
    }
    frame[0] = op_type;                      /* bytes 1..3 reserved (0) */
    for (i = 0; i < MPFS_PUF_CHALLENGE_LEN; i++) {
        frame[4 + i] = challenge[i];
    }

    ret = mpfs_scb_request(SYS_SERV_CMD_PUF_EMULATION, frame, sizeof(frame));
    if (ret == 0) {
        mpfs_scb_read(response, MPFS_PUF_RET_OFFSET, MPFS_PUF_RESPONSE_LEN);
    }
    return ret;
}

int mpfs_nonce(uint8_t *nonce)
{
    int ret;

    if (nonce == NULL) {
        return -1;
    }
    ret = mpfs_scb_request(SYS_SERV_CMD_NONCE, NULL, 0);
    if (ret == 0) {
        mpfs_scb_read(nonce, 0, MPFS_NONCE_LEN);
    }
    return ret;
}


/* Linux kernel command line arguments */
/* Must stay below the fdt.h include: the LINUX_BOOTARGS_OVERRIDE default
 * there keys off build-supplied macros only, not this fallback. */
#ifndef LINUX_BOOTARGS
#ifndef LINUX_BOOTARGS_ROOT
/* wolfBoot SD layout (docs/PolarFire.md): p1=boot FIT,
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

static int mpfs_dts_fixup_inplace(void* dts_addr, uint32_t capacity)
{
    fdt_ctx ctx;
    int off, ret;
    uint8_t device_serial_number[DEVICE_SERIAL_NUMBER_SIZE];
    uint8_t mac_addr[6];
#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT) && \
    (!defined(MPFS_WDT_NODES_KEEP) || defined(MPFS_SCB_SMODE_DENY))
    /* Nodes disabled below.  Watchdogs: the MSS WDTs always count; the OS
     * driver arms them at probe but nothing pings them (no userspace
     * watchdog daemon in the default image), so the system would reset
     * ~28 s into boot.  Disabled, they only latch a harmless tripped
     * status and the parked E51 monitor keeps them refreshed.  Re-enable
     * when an OS-side petting story exists. */
    static const char *const cpu_off[] = {
#ifndef MPFS_WDT_NODES_KEEP
        "watchdog@20001000", "watchdog@20101000", "watchdog@20103000",
        "watchdog@20105000", "watchdog@20107000",
#endif
#ifdef MPFS_SCB_SMODE_DENY
        /* PMP fences S-mode off the System Controller mailbox: the OS
         * drivers for it would only fault. */
        "mailbox@37020800", "spi@37020100", "syscontroller",
#endif
    };
    unsigned int i;
#endif

    /* Validate the blob against the window it actually occupies. */
    ret = fdt_open(&ctx, dts_addr, capacity);
    if (ret != 0) {
        wolfBoot_printf("FDT: Invalid header! %d\n", ret);
        return ret;
    }

    wolfBoot_printf("FDT: Size %d\n", (int)fdt_size(&ctx));

    /* Reserve free space to allow adding/modifying properties.
     * Sizing comes from WOLFBOOT_FDT_FIXUP_HEADROOM in include/fdt.h. */
    ret = fdt_grow(&ctx, WOLFBOOT_FDT_FIXUP_HEADROOM);
    if (ret != 0) {
        wolfBoot_printf("FDT: No headroom for fixups (%d)\n", ret);
        return ret;
    }

    /* Later fixups (watchdog disable, serial number) must run even if the
     * bootargs fixup fails, so log and continue rather than return. */
    ret = fdt_fixup_bootargs(&ctx, LINUX_BOOTARGS, LINUX_BOOTARGS_OVERRIDE);
    if (ret < 0) {
        wolfBoot_printf("FDT: Failed to set bootargs (%d)\n", ret);
    }
#ifdef WOLFBOOT_UPDATE_DISK
    (void)wolfBoot_disk_dts_fixup(&ctx);
#endif

#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT) && \
    (!defined(MPFS_WDT_NODES_KEEP) || defined(MPFS_SCB_SMODE_DENY))
    /* Disable the MSS watchdog dtb nodes before the serial-number read, so
     * this still runs if that read fails: otherwise the OS watchdog driver
     * arms them at probe while the parked E51 monitor (hal_smode_boot) also
     * refreshes all five, and the conflict can trip a watchdog reset.
     *
     * The stock /memory nodes are left alone.  The cached DDR window at
     * 0x80000000 is 1 GB wide and 0xC0000000 up is a non-cached alias onto
     * the same DDR, so describing 2 GB at 0x80000000 would present the alias
     * to the kernel as extra RAM.  The stock Video Kit DTB is already right.
     *
     * cpu@2..cpu@4 stay enabled for SMP: the SBI HSM hart_start backend
     * releases the parked harts when the kernel asks.  cpu@0 (E51) is
     * already disabled in the Yocto DTB; cpu@1 is where Linux boots. */
    for (i = 0; i < sizeof(cpu_off) / sizeof(cpu_off[0]); i++) {
        off = fdt_find_node_offset(&ctx, -1, cpu_off[i]);
        if (off >= 0) {
            ret = fdt_fixup_str(&ctx, off, cpu_off[i], "status",
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
    off = fdt_find_node_offset(&ctx, -1, "ethernet@20110000");
    if (off >= 0) {
        ret = fdt_setprop(&ctx, off, "local-mac-address", mac_addr, 6);
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

    off = fdt_find_node_offset(&ctx, -1, "ethernet@20112000");
    if (off >= 0) {
        ret = fdt_setprop(&ctx, off, "local-mac-address", mac_addr, 6);
        if (ret != 0) {
            wolfBoot_printf("FDT: Failed to set mac1 address (%d)\n", ret);
        }
    }
    else {
        wolfBoot_printf("FDT: ethernet@20112000 not found\n");
    }

    return 0;
}

/* Overrides the weak no-op hal_dts_fixup so FDT-enabled builds get the
 * fixups above. */
int hal_dts_fixup(void* dts_addr, uint32_t capacity)
{
    if (dts_addr == NULL) {
        return -1;
    }
    return mpfs_dts_fixup_inplace(dts_addr, capacity);
}

void hal_prepare_boot(void)
{
#ifdef WOLFBOOT_RISCV_MMODE
#ifndef WOLFBOOT_MMODE_SMODE_BOOT
    /* Hand the application a full window.  Nothing else needs restoring:
     * TIME, MVRP and TRIGGER keep their design-time values throughout. */
    MSS_WDT_REFRESH(MSS_WDT_E51_BASE) = 0xDEADC0DEU;
#else
    /* Booting an S-mode OS: give every hart's watchdog one final refresh so
     * the OS inherits a full window (the
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

#if defined(EXT_FLASH) || defined(MPFS_SBI_SC_FLASH)
/* MSS QSPI (0x21000000) and SC QSPI (0x37020100) are both CoreQSPI v2 with
 * identical register layouts; MPFS_SC_SPI picks one by changing QSPI_BASE. */

/* Microsecond delay using RISC-V time CSR (1 MHz tick rate) */
#ifndef WOLFBOOT_RISCV_MMODE
static void udelay(uint32_t us)
{
    uint64_t start = csr_read(time);
    while ((uint64_t)(csr_read(time) - start) < us)
        ;
}
#endif
static int qspi_transfer_block(uint8_t read_mode, const uint8_t *cmd,
                                uint32_t cmd_len, uint8_t *data,
                                uint32_t data_len, uint8_t dummy_cycles);
static int qspi_read_id(uint8_t *id_buf);
static int qspi_enter_4byte_mode(void);

/* Spin until a STATUS bit is set.  Call it through QSPI_WAIT, which keeps the
 * stage names out of the binary unless DEBUG_QSPI is on. */
static int qspi_wait_status(uint32_t bit, uint32_t tries, const char *what)
{
    while (!(QSPI_STATUS & bit) && --tries)
        ;
    if (tries == 0) {
#ifdef DEBUG_QSPI
        wolfBoot_printf("QSPI: %s timeout, status=0x%x\n", what, QSPI_STATUS);
#else
        (void)what;
#endif
        return -1;
    }
    return 0;
}

#ifdef DEBUG_QSPI
#define QSPI_WAIT(bit, tries, what) qspi_wait_status((bit), (tries), (what))
#else
#define QSPI_WAIT(bit, tries, what) qspi_wait_status((bit), (tries), NULL)
#endif

static void qspi_flash_wakeup(void)
{
    uint8_t cmd = 0xAB;  /* Release from Deep Power-Down */
    qspi_transfer_block(QSPI_MODE_WRITE, &cmd, 1, NULL, 0, 0);
    udelay(10); /* tRES1, 3us typ */
}

int qspi_init(void)
{
    uint8_t id[3];

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

    /* The System Controller may have left XIP enabled */
    QSPI_DIRECT = 0;
#else
    wolfBoot_printf("QSPI: Using MSS QSPI Controller (0x%x)\n", QSPI_BASE);

    SYSREG_SUBBLK_CLOCK_CR |= SYSREG_SUBBLK_CLOCK_CR_QSPI;
    udelay(1);
    SYSREG_SOFT_RESET_CR &= ~SYSREG_SOFT_RESET_CR_QSPI;
    udelay(10);
#endif

    QSPI_CONTROL = 0;
    QSPI_IEN = 0;
    /* CLKIDLE + SAMPLE_SCK is SPI mode 3 */
    QSPI_CONTROL =
        (MPFS_QSPI_CLK_DIV << QSPI_CTRL_CLKRATE_OFFSET) |
        QSPI_CTRL_CLKIDLE |
        QSPI_CTRL_SAMPLE_SCK |
        QSPI_CTRL_EN;

    if (QSPI_WAIT(QSPI_STATUS_READY, QSPI_TIMEOUT_TRIES, "init") != 0) {
        wolfBoot_printf("QSPI: Controller not ready\n");
        return -1;
    }

    qspi_flash_wakeup();

    if (qspi_read_id(id) == 0) {
        wolfBoot_printf("QSPI: Flash ID = 0x%02x 0x%02x 0x%02x\n",
            id[0], id[1], id[2]);
    }

    /* 4-byte addressing for flash larger than 16 MB */
    qspi_enter_4byte_mode();

    return 0;
}

/* Polled transfer, after Microchip's MSS_QSPI_polled_transfer_block.
 * cmd holds the opcode and any address bytes; dummy_cycles are the idle
 * cycles the controller inserts between the command and data phases. */
static int qspi_transfer_block(uint8_t read_mode, const uint8_t *cmd,
                               uint32_t cmd_len, uint8_t *data,
                               uint32_t data_len, uint8_t dummy_cycles)
{
    uint32_t total_bytes = cmd_len + data_len;
    uint32_t frames;
    uint32_t i;
    uint32_t step;
    uint32_t timeout;
    uint32_t frame_cmd;
    uint32_t rx_word;
    int word_read;

    if (QSPI_WAIT(QSPI_STATUS_READY, QSPI_TIMEOUT_TRIES, "READY") != 0) {
        return -1;
    }

    /* Discard anything a previous transfer left in the RX FIFO. */
    timeout = QSPI_TIMEOUT_TRIES;
    while ((QSPI_STATUS & QSPI_STATUS_RXAVAIL) && --timeout) {
        (void)QSPI_RX_DATA;
    }

    /* Writes put the whole transfer in the command phase (CMDBYTES =
     * TOTALBYTES): the CoreQSPI data phase shifts TX FIFO output by a fixed
     * offset on writes, which rotates the programmed page.  The flash takes
     * the command/data boundary from the opcode, not the controller phase. */
    frame_cmd = read_mode ? cmd_len : total_bytes;
    /* FBYTE counts RXAVAIL/TXAVAIL in bytes, FWORD in 32-bit words, so a
     * word-mode read polls STATUS once per four bytes and takes them through
     * QSPI_X4_RX_DATA in one access.  The polling, not SCK, caps the read
     * rate.  FWORD needs a whole number of words in the data phase. */
    word_read = (read_mode && data_len >= 4u && (data_len & 3u) == 0u) ? 1 : 0;
    frames = ((total_bytes & 0xFFFF) << QSPI_FRAMES_TOTALBYTES_OFFSET) |
                ((frame_cmd & 0x1FF) << QSPI_FRAMES_CMDBYTES_OFFSET) |
                ((dummy_cycles & 0xF) << QSPI_FRAMES_IDLE_OFFSET) |
                (word_read ? (1u << QSPI_FRAMES_FWORD_OFFSET)
                           : (1u << QSPI_FRAMES_FBYTE_OFFSET));

    QSPI_FRAMES = frames;

    /* TXAVAIL is the only FIFO-space flag: CoreQSPI v2 has no TXFULL bit.
     * The fence after each write keeps RVWMO from leaving the store posted,
     * which would make the next STATUS read stale and overflow the FIFO. */
    for (i = 0; i < cmd_len; i++) {
        if (QSPI_WAIT(QSPI_STATUS_TXAVAIL, QSPI_TIMEOUT_TRIES,
                "TX cmd") != 0) {
            return -2;
        }
        QSPI_TX_DATA = cmd[i];
        QSPI_IO_FENCE();
    }

    if (read_mode) {
        step = word_read ? 4u : 1u;
        for (i = 0; i < data_len; i += step) {
            if (QSPI_WAIT(QSPI_STATUS_RXAVAIL, QSPI_RX_TIMEOUT_TRIES,
                    "RX data") != 0) {
                return -3;
            }
            if (word_read) {
                /* data is not guaranteed word-aligned, so store bytewise;
                 * the saving is in the polling and the FIFO accesses. */
                rx_word = QSPI_X4_RX_DATA;
                data[i]     = (uint8_t)(rx_word & 0xFFu);
                data[i + 1] = (uint8_t)((rx_word >> 8) & 0xFFu);
                data[i + 2] = (uint8_t)((rx_word >> 16) & 0xFFu);
                data[i + 3] = (uint8_t)((rx_word >> 24) & 0xFFu);
            }
            else {
                data[i] = QSPI_RX_DATA;
            }
        }
        if (QSPI_WAIT(QSPI_STATUS_RXDONE, QSPI_RX_TIMEOUT_TRIES,
                "RXDONE") != 0) {
            return -5;
        }
    }
    else {
        /* Any gap here underflows the FIFO: CoreQSPI keeps clocking. */
        for (i = 0; i < data_len; i++) {
            if (QSPI_WAIT(QSPI_STATUS_TXAVAIL, QSPI_TIMEOUT_TRIES,
                    "TX data") != 0) {
                return -4;
            }
            QSPI_TX_DATA = data[i];
            QSPI_IO_FENCE();
        }
        if (QSPI_WAIT(QSPI_STATUS_TXDONE, QSPI_TIMEOUT_TRIES,
                "TXDONE") != 0) {
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
    /* total_bytes is 16-bit and the command takes 5 of it; round down to a
     * whole number of words so a bulk read stays on the word-mode FIFO path
     * in qspi_transfer_block instead of falling back to byte mode. */
    const uint32_t max_chunk = (0xFFFFu - 5u) & ~3u;
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

#else /* !EXT_FLASH && !MPFS_SBI_SC_FLASH */

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

/* Pet all five MSS watchdogs during the SDHCI read loops.
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

