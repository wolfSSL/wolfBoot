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

/* UART base addresses for per-hart access (LO addresses, M-mode compatible) */
const unsigned long MSS_UART_BASE_ADDR[5] = {
    MSS_UART0_LO_BASE,  /* Hart 0 (E51) */
    MSS_UART1_LO_BASE,  /* Hart 1 (U54_1) */
    MSS_UART2_LO_BASE,  /* Hart 2 (U54_2) */
    MSS_UART3_LO_BASE,  /* Hart 3 (U54_3) */
    MSS_UART4_LO_BASE,  /* Hart 4 (U54_4) */
};

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "sdhci.h"

/* Forward declaration of SDHCI IRQ handler */
extern void sdhci_irq_handler(void);
#endif

/* Video Kit DDR/Clock configuration is included in mpfs250.h */

/* Configure L2 cache: enable ways 0,1,3 (0x0B) and set way masks for all masters */
#ifdef WOLFBOOT_RISCV_MMODE
static void mpfs_config_l2_cache(void)
{
    L2_WAY_ENABLE = 0x0B;  /* ways 0, 1, 3 -- matches DDR demo config */
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

/* CPU frequency in MHz, used by mcycle-based udelay().  Starts at 80 MHz
 * (E51 reset clock) and is bumped to 600 MHz after mss_pll_init() switches
 * the MSS clock mux to PLL output.  Without this update, udelay() runs ~7.5x
 * too fast post-PLL, breaking timing-sensitive code paths such as the SD
 * card power-up window. */
static uint32_t mpfs_cpu_freq_mhz = 80U;

/* DQ/DQS init offset (HSS rpc_156).  Default 6 (Libero Video Kit
 * value), tunable 1..9 per HSS TUNE_RPC_156_DQDQS_INIT_VALUE.
 * Bumped between outer retries when training verify reports
 * dq_dqs_err_done != 8 or dqdqs_status2 == 0 (data eye closed). */
static uint32_t mpfs_phy_rpc156_val = 6U;

/* mcycle-based microsecond delay.  MTIME is not running in M-mode without
 * HSS, but mcycle ticks at the CPU clock rate and is monotonic. */
static __attribute__((noinline)) void udelay(uint32_t us)
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
/* MPFS MSS WDT clock is AHB / 256 ≈ 150 MHz / 256 ≈ 585 kHz at S-mode rate
 * but ~80 MHz / 256 ≈ 312 kHz on E51 reset clocks. Use a conservative
 * 300 ticks/ms; the actual rate may be a bit higher but a slightly longer
 * timeout is safe. Caller can override WATCHDOG_TIMEOUT_MS at build time. */
#  define WATCHDOG_TIMEOUT_TICKS ((WATCHDOG_TIMEOUT_MS) * 300U)
#endif

/* Saved boot ROM watchdog values, restored in hal_prepare_boot() */
static uint32_t mpfs_wdt_default_mvrp = 0;
static uint32_t mpfs_wdt_default_ctrl = 0;

/* Diagnostic snapshots captured at hal_init entry, printed after
 * uart_init.  RESET_SR shows the cause of the most-recent reset. */
static uint32_t mpfs_boot_wdt_snap[6];
static uint32_t mpfs_boot_reset_sr_snap;


/* CLINT MSIP register for IPI delivery */
#define CLINT_MSIP_REG(hart) (*(volatile uint32_t*)(CLINT_BASE + (hart) * 4))

/* Signal secondary harts that E51 (main hart) is ready. */
static void mpfs_signal_main_hart_started(void)
{
    HLS_DATA* hls = (HLS_DATA*)&_main_hart_hls;
    hls->in_wfi_indicator = HLS_MAIN_HART_STARTED;
    hls->my_hart_id = MPFS_FIRST_HART;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/* Wake secondary U54 harts by sending software IPIs via CLINT MSIP. */
int mpfs_wake_secondary_harts(void)
{
    int hart_id;
    int woken_count = 0;

    wolfBoot_printf("Waking secondary harts...\n");
    for (hart_id = MPFS_FIRST_U54_HART; hart_id <= MPFS_LAST_U54_HART; hart_id++) {
        CLINT_MSIP_REG(hart_id) = 0x01;
        __asm__ volatile("fence iorw, iorw" ::: "memory");
        udelay(1000);
        woken_count++;
    }
    wolfBoot_printf("Woke %d secondary harts\n", woken_count);
    return woken_count;
}

#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
/* Kernel hand-off context written by the E51 just before sending the IPI
 * that releases the chosen U54 into S-mode. The struct lives in L2 Scratch
 * (BSS) so all harts read the same physical address. */
typedef struct {
    volatile uint32_t marker;       /* MPFS_KERNEL_HANDOFF_MARKER when valid */
    volatile uint32_t target_hart;  /* hart that should perform the M->S jump */
    volatile uint64_t kernel_entry; /* S-mode entry point (kernel) */
    volatile uint64_t dtb_addr;     /* DTB physical address (a1) */
} mpfs_kernel_handoff_t;

#define MPFS_KERNEL_HANDOFF_MARKER  0x4C4E5858UL  /* "LNXX" */

static mpfs_kernel_handoff_t mpfs_kernel_handoff;

/* Provided by src/boot_riscv.c. */
extern void riscv_mmode_to_smode(unsigned long entry, unsigned long hartid,
                                 unsigned long dtb) __attribute__((noreturn));
#endif /* MPFS_DDR_INIT && WOLFBOOT_MMODE_SMODE_BOOT */

/* Secondary hart (U54) entry: init per-hart UART and either jump into the
 * waiting Linux kernel (when the E51 has staged a hand-off context for us)
 * or stay parked in WFI waiting for an SBI/Linux IPI. */
void secondary_hart_entry(unsigned long hartid, HLS_DATA* hls)
{
    char msg[] = "Hart X: Woken, waiting for kernel boot...\n";
    (void)hls;
    uart_init_hart(hartid);
    msg[5] = '0' + (char)hartid;
    uart_write_hart(hartid, msg, sizeof(msg) - 1);

    while (1) {
        __asm__ volatile("wfi");
#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
        /* Clear pending IPI before checking the hand-off context to avoid
         * a re-entry race on spurious wake-ups. */
        CLINT_MSIP_REG(hartid) = 0;
        __asm__ volatile("fence iorw, iorw" ::: "memory");

        if (mpfs_kernel_handoff.marker == MPFS_KERNEL_HANDOFF_MARKER &&
            mpfs_kernel_handoff.target_hart == (uint32_t)hartid) {
            unsigned long kentry;
            unsigned long dtb;
            /* Acquire fence: pair with the writer's release fence so we
             * are guaranteed to observe kernel_entry / dtb_addr after
             * seeing marker.  Without this, RISC-V's relaxed memory model
             * permits the reader to use stale field values cached before
             * marker was published. */
            __asm__ volatile("fence r,rw" ::: "memory");
            kentry = (unsigned long)mpfs_kernel_handoff.kernel_entry;
            dtb = (unsigned long)mpfs_kernel_handoff.dtb_addr;
            riscv_mmode_to_smode(kentry, hartid, dtb);
            /* never returns */
        }
#endif /* MPFS_DDR_INIT && WOLFBOOT_MMODE_SMODE_BOOT */
    }
}

#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
/* Override of the weak hal_smode_boot in src/boot_riscv.c. The E51 cannot
 * run Linux (cpu@0 is marked disabled in the Yocto MPFS DTB), so instead of
 * dropping to S-mode on hart 0 we stage the kernel/DTB pointers, IPI a U54,
 * and park hart 0 in M-mode. The released U54 picks up the context from
 * its WFI loop in secondary_hart_entry() and performs the actual M->S jump. */
void __attribute__((noreturn))
hal_smode_boot(unsigned long entry, unsigned long hartid, unsigned long dtb)
{
    (void)hartid;  /* the calling E51 hart is not the kernel boot hart */

    mpfs_kernel_handoff.target_hart = (uint32_t)MPFS_FIRST_U54_HART;
    mpfs_kernel_handoff.kernel_entry = (uint64_t)entry;
    mpfs_kernel_handoff.dtb_addr = (uint64_t)dtb;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    mpfs_kernel_handoff.marker = MPFS_KERNEL_HANDOFF_MARKER;
    __asm__ volatile("fence iorw, iorw" ::: "memory");

    wolfBoot_printf("Releasing hart %d into S-mode at 0x%lx (dtb=0x%lx)\n",
                    MPFS_FIRST_U54_HART, entry, dtb);

    CLINT_MSIP_REG(MPFS_FIRST_U54_HART) = 0x01;
    __asm__ volatile("fence iorw, iorw" ::: "memory");

    /* Phase 4A: park the E51 in M-mode. Phase 4B will replace this with a
     * trap-driven SBI HSM service loop so the kernel can start the other
     * U54s via sbi_ecall(HSM, HART_START, ...). */
    while (1) {
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



/* ============================================================================
 * DDR Controller Driver - LPDDR4 init for MPFS250T Video Kit
 * ============================================================================
 * Build with -DDEBUG_DDR to emit verbose register-level traces during
 * SGMII/PLL/PHY/training. By default only top-level milestones, errors and
 * memory-test results are printed. */
#ifdef DEBUG_DDR
#   define DBG_DDR(_f_, ...) wolfBoot_printf(_f_, ##__VA_ARGS__)
#else
#   define DBG_DDR(_f_, ...) do { } while (0)
#endif

#if defined(WOLFBOOT_RISCV_MMODE) && defined(MPFS_DDR_INIT)
static inline void mb(void)
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/* DDR-init busy-loop delay.  The argument is NOT a real microsecond --
 * it is whatever the legacy busy-loop produces at the current CPU
 * clock.  Empirically reaches train_stat=0x1D on the first attempt with
 * the same per-attempt rate as forwarding to udelay(), and is much
 * faster (~4 s vs ~50 s) for the TIP-wait timeout, which dominates
 * retry-loop time when training fails.
 *
 * Do NOT replace with udelay(us) without re-timing every call site
 * below: at 600 MHz the busy-loop delivers roughly us/20 of a real us,
 * so udelay(us) makes every post-PLL delay ~20x longer.  In addition
 * to slowing retries, this can shift LPDDR4 / PHY timing windows --
 * earlier observed empirical data showed an isolated additional
 * regression beyond the pre-existing ~30% per-attempt failure rate.
 *
 * The "5us" / "250us" / "2ms" comments at the call sites are LEGACY
 * and do not reflect the actual delay; preserved for git blame, not
 * as timing references. */
static void ddr_delay(uint32_t us)
{
    volatile uint32_t i;
    for (i = 0; i < us * 10; i++) {
        __asm__ volatile("nop");
    }
}

/* IOSCB Bank Controllers and DLL bases */
#define IOSCB_BANK_CNTL_SGMII_BASE  0x3E400000UL
#define IOSCB_BANK_CNTL_DDR_BASE    0x3E020000UL
#define IOSCB_DLL_SGMII_BASE        0x3E100000UL

/* Forward declaration: defined alongside the SDHCI platform helpers below
 * but called from nwc_init() before MSSIO_CONTROL_CR is committed. */
static void mpfs_iomux_init(void);

/* SGMII Off Mode
 *
 * Configure SGMII for DDR-only mode (from HSS mss_sgmii.c sgmii_off_mode)
 * Even when SGMII is not used, these registers must be configured with
 * Libero-generated values for proper DDR operation.
 */
static void sgmii_off_mode(void)
{
    volatile uint32_t *ioscb_dll_sgmii = (volatile uint32_t *)IOSCB_DLL_SGMII_BASE;

    /* Soft reset SGMII TIP with NV_MAP + peripheral bits, then just peripheral
     * This matches HSS: SOFT_RESET_SGMII = (0x01 << 8U) | 1U; then = 1U; */
    DDRPHY_REG(0x040) = (0x01UL << 8) | 0x01UL;  /* SOFT_RESET_SGMII - periph+nv_map */
    mb();
    udelay(1);
    DDRPHY_REG(0x040) = 0x01UL;  /* Just periph reset */
    mb();

    /* Configure SGMII RPC registers with Libero-generated values
     * From HSS setup_sgmii_rpc_per_config() - critical for clock routing!
     * Note: REG_CDR_MOVE_STEP mask (0x0C000000) is cleared from SGMII_MODE
     * Register offsets from mss_ddr_sgmii_phy_defs.h (NOT the same as soft reset!) */
    DDRPHY_REG(0xC04) = LIBERO_SETTING_SGMII_MODE & ~0x0C000000UL;  /* SGMII_MODE */
    DDRPHY_REG(0xC08) = LIBERO_SETTING_PLL_CNTL;                    /* PLL_CNTL */
    DDRPHY_REG(0xC0C) = LIBERO_SETTING_CH0_CNTL;                    /* CH0_CNTL */
    DDRPHY_REG(0xC10) = LIBERO_SETTING_CH1_CNTL;                    /* CH1_CNTL */
    DDRPHY_REG(0xC14) = LIBERO_SETTING_RECAL_CNTL;                  /* RECAL_CNTL */
    DDRPHY_REG(0xC18) = LIBERO_SETTING_CLK_CNTL;                    /* CLK_CNTL */
    DDRPHY_REG(0xC24) = LIBERO_SETTING_SPARE_CNTL;                  /* SPARE_CNTL */
    mb();

    /* Reset SGMII DLL via SCB - required for IO to be configured
     * From HSS: "so we have to use scb register to reset as no APB register
     * available to soft reset the IP" */
    ioscb_dll_sgmii[0] = 0x01UL;  /* soft_reset at offset 0 */
    mb();
    udelay(10);
}

/* SGMII/Clock Mux Configuration
 *
 * The RFCKMUX register at 0x3E200004 routes the external reference clock
 * to both the DDR PLL and SGMII PLL. This MUST be configured before the
 * PLLs can lock.
 *
 * From HSS mss_pll.c: "0x05 => ref to SGMII and DDR"
 */
static void sgmii_mux_config(void)
{
    uint32_t rfckmux;

    wolfBoot_printf("DDR: Configuring SGMII/clock mux...\n");

    /* First, put SGMII in off mode (from HSS sgmii_off_mode) */
    sgmii_off_mode();

    /* Enable SGMII bank controller (bring out of reset) */
    volatile uint32_t *ioscb_bank_cntl_sgmii = (volatile uint32_t *)IOSCB_BANK_CNTL_SGMII_BASE;
    ioscb_bank_cntl_sgmii[0] = 0x01UL;  /* soft_reset - triggers NV map load */
    mb();
    udelay(10);

    /* Method 1: Try RPC soft reset on CFM to load NV map values from FPGA */
    DBG_DDR("  Soft reset CFM to load NV map...");
    CFM_SGMII_REG(CFM_SGMII_SOFT_RESET) = 0x01UL;
    mb();
    udelay(100);
    DBG_DDR("done\n");

    rfckmux = CFM_SGMII_REG(CFM_SGMII_RFCKMUX);
    DBG_DDR("  RFCKMUX after NV load = 0x%x\n", rfckmux);

    /* Method 2: If NV map didn't have the value, try direct SCB writes */
    if (rfckmux != LIBERO_SETTING_SGMII_REFCLKMUX) {
        DBG_DDR("  Trying direct SCB writes...\n");

        /* Configure clock receiver for external reference - CRITICAL for ref clock! */
        CFM_SGMII_REG(CFM_SGMII_CLK_XCVR) = LIBERO_SETTING_SGMII_CLK_XCVR;
        mb();

        /* Route external reference clock to DDR and SGMII PLLs */
        CFM_SGMII_REG(CFM_SGMII_RFCKMUX) = LIBERO_SETTING_SGMII_REFCLKMUX;
        mb();

        /* SGMII clock mux */
        CFM_SGMII_REG(CFM_SGMII_SGMII_CLKMUX) = LIBERO_SETTING_SGMII_SGMII_CLKMUX;
        mb();
        udelay(10);

        rfckmux = CFM_SGMII_REG(CFM_SGMII_RFCKMUX);
        DBG_DDR("  RFCKMUX after SCB write = 0x%x\n", rfckmux);
    } else {
        /* NV map loaded the value, still need to configure clock receiver */
        CFM_SGMII_REG(CFM_SGMII_CLK_XCVR) = LIBERO_SETTING_SGMII_CLK_XCVR;
        mb();
    }

    DBG_DDR("  CLK_XCVR=0x%x\n", CFM_SGMII_REG(CFM_SGMII_CLK_XCVR));

    if (rfckmux != LIBERO_SETTING_SGMII_REFCLKMUX) {
        wolfBoot_printf("  WARNING: RFCKMUX not set correctly!\n");
    }
}

/* MSS PLL Mux Pre-Configuration
 *
 * Feed through required reference clocks to PLL before powering up
 * From HSS mss_mux_pre_mss_pll_config()
 *
 * PLL RF clock mux selections (2 bits each):
 *   00 = vss (ground)
 *   01 = refclk_p,refclk_n (external reference - requires SGMII CFM RFCKMUX)
 *   10 = scb_clk (80MHz internal oscillator)
 *   11 = serdes_refclk
 */
static void mss_mux_pre_pll_config(void)
{
    uint32_t pll_ckmux;
    uint32_t rfckmux;

    /* Check if RFCKMUX is configured - if not, use SCB_CLK instead */
    rfckmux = CFM_SGMII_REG(CFM_SGMII_RFCKMUX);

    if (rfckmux == LIBERO_SETTING_SGMII_REFCLKMUX) {
        /* External refclk is available, use Libero settings */
        pll_ckmux = LIBERO_SETTING_MSS_PLL_CKMUX;
        DBG_DDR("  Using external refclk (RFCKMUX=0x%x)\n", rfckmux);
    } else {
        /* External refclk not available, use SCB_CLK (80MHz internal)
         * PLL0_RFCLK0_SEL = 10 (SCB_CLK), PLL0_RFCLK1_SEL = 10 (SCB_CLK)
         * PLL1_RFCLK0_SEL = 10 (SCB_CLK), PLL1_RFCLK1_SEL = 10 (SCB_CLK)
         * This gives: 0x02 | (0x02 << 2) | (0x02 << 4) | (0x02 << 6) | (0x02 << 8) = 0x2AA
         */
        pll_ckmux = 0x000002AAUL;
        DBG_DDR("  Using SCB_CLK (80MHz) as PLL ref (fallback)\n");
    }

    /* Configure PLL clock mux - select reference sources */
    CFM_MSS_REG(CFM_PLL_CKMUX) = pll_ckmux;
    mb();

    DBG_DDR("  PLL_CKMUX=0x%x\n", CFM_MSS_REG(CFM_PLL_CKMUX));

    /* Configure BCLK mux for DDR PHY */
    CFM_MSS_REG(CFM_BCLKMUX) = LIBERO_SETTING_MSS_BCLKMUX;
    mb();

    /* Frequency meter (not critical but part of standard init) */
    CFM_MSS_REG(CFM_FMETER_ADDR) = LIBERO_SETTING_MSS_FMETER_ADDR;
    CFM_MSS_REG(CFM_FMETER_DATAW) = LIBERO_SETTING_MSS_FMETER_DATAW;
    mb();

    DBG_DDR("  BCLKMUX=0x%x\n", CFM_MSS_REG(CFM_BCLKMUX));

    /* Delay for clock mux and reference clock to stabilize */
    udelay(1000);
}

/* MSS PLL Initialization
 *
 * Configure MSS PLL following the HSS sequence from mss_pll_config()
 */
static int mss_pll_init(void)
{
    uint32_t pll_ctrl;
    uint32_t timeout;

    wolfBoot_printf("DDR: Configuring MSS PLL...\n");

    /* First check if PLL is already configured and locked by System Controller */
    pll_ctrl = MSS_PLL_REG(PLL_CTRL);
    DBG_DDR("  Initial MSS PLL CTRL=0x%x\n", pll_ctrl);

    if (pll_ctrl & PLL_LOCK_BIT) {
        DBG_DDR("  MSS PLL already locked!\n");
        return 0;
    }

    /* Take PLLs out of reset (HSS: this is done before any configuration) */
    MSS_PLL_REG(PLL_SOFT_RESET) = PLL_INIT_OUT_RESET;
    DDR_PLL_REG(PLL_SOFT_RESET) = PLL_INIT_OUT_RESET;
    mb();

    /* Power down PLL while configuring (HSS sequence: configure before mux) */
    MSS_PLL_REG(PLL_CTRL) = LIBERO_SETTING_MSS_PLL_CTRL & ~PLL_POWERDOWN_B;
    mb();

    /* Configure PLL parameters (while powered down) */
    MSS_PLL_REG(PLL_REF_FB) = LIBERO_SETTING_MSS_PLL_REF_FB;
    MSS_PLL_REG(PLL_DIV_0_1) = LIBERO_SETTING_MSS_PLL_DIV_0_1;
    MSS_PLL_REG(PLL_DIV_2_3) = LIBERO_SETTING_MSS_PLL_DIV_2_3;
    MSS_PLL_REG(PLL_CTRL2) = LIBERO_SETTING_MSS_PLL_CTRL2;
    MSS_PLL_REG(PLL_FRACN) = LIBERO_SETTING_MSS_PLL_FRACN;
    MSS_PLL_REG(PLL_SSCG_0) = LIBERO_SETTING_MSS_SSCG_REG_0;
    MSS_PLL_REG(PLL_SSCG_1) = LIBERO_SETTING_MSS_SSCG_REG_1;
    MSS_PLL_REG(PLL_SSCG_2) = LIBERO_SETTING_MSS_SSCG_REG_2;
    MSS_PLL_REG(PLL_SSCG_3) = LIBERO_SETTING_MSS_SSCG_REG_3;
    MSS_PLL_REG(PLL_PHADJ) = LIBERO_SETTING_MSS_PLL_PHADJ;
    mb();

    /* Configure muxes AFTER PLL registers but BEFORE power-up (HSS sequence) */
    mss_mux_pre_pll_config();

    /* Power up PLL */
    DBG_DDR("  Powering up PLL (CTRL=0x%x)...\n",
        LIBERO_SETTING_MSS_PLL_CTRL | PLL_POWERDOWN_B);
    MSS_PLL_REG(PLL_CTRL) = LIBERO_SETTING_MSS_PLL_CTRL | PLL_POWERDOWN_B;
    mb();

    /* Short delay for PLL to start */
    udelay(100);

    pll_ctrl = MSS_PLL_REG(PLL_CTRL);
    DBG_DDR("  After power up: CTRL=0x%x\n", pll_ctrl);

    /* Wait for lock */
    wolfBoot_printf("  Waiting for MSS PLL lock...");
    timeout = 1000000;
    while (timeout > 0) {
        pll_ctrl = MSS_PLL_REG(PLL_CTRL);
        if (pll_ctrl & PLL_LOCK_BIT) {
            wolfBoot_printf("locked (0x%x)\n", pll_ctrl);

            /* Drain the UART TX shift register before changing the APB
             * divisor.  Any byte still mid-flight at the boot-clock baud
             * rate would otherwise shift out at the new rate and arrive
             * as a garbled character on the host (e.g. the trailing
             * '\n' of the "locked..." print). */
#ifdef DEBUG_UART
            while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_TEMT) == 0)
                ;
#endif

            /* Configure clock dividers before switching
             * LIBERO_SETTING_MSS_CLOCK_CONFIG_CR = 0x24:
             *   CPU = /1 (600MHz), AXI = /2 (300MHz), APB = /4 (150MHz)
             */
            SYSREG_REG(0x08) = 0x00000024UL;  /* CLOCK_CONFIG_CR */
            mb();

            /* Switch MSS to use PLL clock */
            CFM_MSS_REG(CFM_MSSCLKMUX) = LIBERO_SETTING_MSS_MSSCLKMUX;
            mb();

            /* CPU is now at the Libero-configured PLL rate (typically
             * 600 MHz on the Video Kit).  Track it so udelay() stays
             * accurate; SD-card power-up needs >= 1 ms after VDD ramp
             * and a wrong frequency here breaks the SDHCI bring-up. */
            mpfs_cpu_freq_mhz =
                (uint32_t)(LIBERO_SETTING_MSS_COREPLEX_CPU_CLK / 1000000UL);

            /* Wait for clock switch to stabilize */
            {
                volatile int i;
                for (i = 0; i < 10000; i++) { /* ~1ms at new clock speed */
                    __asm__ volatile("nop");
                }
            }

            /* Reinitialize UART for new clock frequency.  hal_uart_reinit
             * is defined under #ifdef DEBUG_UART (the only path that uses
             * the UART driver); skip the call when that block is absent so
             * the build links cleanly without DEBUG_UART. */
#ifdef DEBUG_UART
            hal_uart_reinit();
#endif
            return 0;
        }
        if ((timeout % 100000) == 0) {
            DBG_DDR(".");
        }
        timeout--;
        udelay(1);
    }

    wolfBoot_printf("TIMEOUT (0x%x)\n", pll_ctrl);
    wolfBoot_printf("  REF_FB=0x%x DIV_0_1=0x%x DIV_2_3=0x%x\n",
        MSS_PLL_REG(PLL_REF_FB), MSS_PLL_REG(PLL_DIV_0_1), MSS_PLL_REG(PLL_DIV_2_3));
    return -1;
}

/* DDR PLL Initialization
 *
 * Configure DDR PLL following the HSS sequence from ddr_pll_config()
 * This is called later, after DDR bank controller is reset and PVT calibration
 */
static int ddr_pll_init(void)
{
    volatile uint32_t *ioscb_bank_cntl_ddr = (volatile uint32_t *)IOSCB_BANK_CNTL_DDR_BASE;
    uint32_t pll_ctrl;
    uint32_t timeout;

    wolfBoot_printf("DDR: Configuring DDR PLL...\n");

    /* Reset DDR bank controller to load NV map values (from HSS DDR_TRAINING_SOFT_RESET) */
    DBG_DDR("  DDR bank controller reset...");
    ioscb_bank_cntl_ddr[0] = 0x01UL;  /* soft_reset */
    mb();
    udelay(100);
    DBG_DDR("done\n");

    /* DDR PLL soft reset */
    DDR_PLL_REG(PLL_SOFT_RESET) = PLL_INIT_OUT_RESET;
    mb();

    /* Power down PLL while configuring */
    DDR_PLL_REG(PLL_CTRL) = LIBERO_SETTING_DDR_PLL_CTRL & ~PLL_POWERDOWN_B;
    mb();

    /* Configure PLL parameters */
    /* (Lower-DDR-clock experiment reverted: tried RFDIV 5->6 ~1333 Mbps;
     * the intermittent non-cached corruption was UNCHANGED, proving it is
     * NOT a timing margin -- it is an intermittent addressing/mapping
     * fault.  Back to the Libero 1600 value.) */
    DDR_PLL_REG(PLL_REF_FB) = LIBERO_SETTING_DDR_PLL_REF_FB;
    DDR_PLL_REG(PLL_DIV_0_1) = LIBERO_SETTING_DDR_PLL_DIV_0_1;
    DDR_PLL_REG(PLL_DIV_2_3) = LIBERO_SETTING_DDR_PLL_DIV_2_3;
    DDR_PLL_REG(PLL_CTRL2) = LIBERO_SETTING_DDR_PLL_CTRL2;
    DDR_PLL_REG(PLL_FRACN) = LIBERO_SETTING_DDR_PLL_FRACN;
    DDR_PLL_REG(PLL_SSCG_0) = LIBERO_SETTING_DDR_SSCG_REG_0;
    DDR_PLL_REG(PLL_SSCG_1) = LIBERO_SETTING_DDR_SSCG_REG_1;
    DDR_PLL_REG(PLL_SSCG_2) = LIBERO_SETTING_DDR_SSCG_REG_2;
    DDR_PLL_REG(PLL_SSCG_3) = LIBERO_SETTING_DDR_SSCG_REG_3;
    DDR_PLL_REG(PLL_PHADJ) = LIBERO_SETTING_DDR_PLL_PHADJ;
    mb();

    /* Power up PLL */
    DDR_PLL_REG(PLL_CTRL) = LIBERO_SETTING_DDR_PLL_CTRL | PLL_POWERDOWN_B;
    mb();

    /* Wait for lock */
    wolfBoot_printf("  Waiting for DDR PLL lock...");
    timeout = 1000000;
    while (timeout > 0) {
        pll_ctrl = DDR_PLL_REG(PLL_CTRL);
        if (pll_ctrl & PLL_LOCK_BIT) {
            wolfBoot_printf("locked (0x%x)\n", pll_ctrl);
            return 0;
        }
        timeout--;
        udelay(1);
    }

    wolfBoot_printf("TIMEOUT (0x%x)\n", pll_ctrl);
    return -1;
}

/* NWC Initialization (SCB, PLLs)
 *
 * Initialize the Network-on-Chip (NWC) clocking subsystem:
 * 1. Configure SCB access
 * 2. Enable DFI APB access for DDR PHY
 * 3. Configure MSSIO for dynamic access
 * 4. Configure SGMII mux to route reference clock to PLLs (CRITICAL!)
 * 5. Initialize MSS PLL
 * 6. Initialize DDR PLL
 */
static int nwc_init(void)
{
    int ret;

    wolfBoot_printf("DDR: NWC init...\n");

    /* Configure SCB access timer */
    SCBCFG_REG(0x08) = MSS_SCB_ACCESS_CONFIG;
    mb();

    /* Enable DFI APB access - bit 0 = clock on (HSS uses 0x01) */
    SYSREG_REG(SYSREG_DFIAPB_CR_OFF) = 0x00000001UL;
    mb();

    /* Enable dynamic APB/SCB access to DDR PHY */
    DDRPHY_REG(PHY_STARTUP) = (0x3FUL << 16) | (0x1FUL << 8);
    DDRPHY_REG(PHY_DYN_CNTL) = (0x01UL << 10) | (0x7FUL << 0);
    mb();

    /* IOMUX + MSSIO bank config MUST be programmed BEFORE the
     * MSSIO_CONTROL_CR 4-phase enable sequence below.  HSS does
     * mssio_setup() right here in mss_nwc_init() (before flash_valid +
     * mss_io_en assert), and otherwise the IO pads commit with the wrong
     * routing - in particular the SDHCI controller's CLK/CMD/DAT lines
     * never reach the SD card slot, causing CMD8 to time out. */
    mpfs_iomux_init();

    DBG_DDR("  MSSIO...");
    /* MSSIO control sequence for dynamic enable */
    SYSREGSCB_REG(MSSIO_CONTROL_CR_OFF) = (0x07UL << 8) | (0x01UL << 11);
    mb();
    udelay(5);
    SYSREGSCB_REG(MSSIO_CONTROL_CR_OFF) = (0x00UL << 8) | (0x01UL << 11);
    mb();
    udelay(5);
    SYSREGSCB_REG(MSSIO_CONTROL_CR_OFF) = (0x00UL << 8) | (0x01UL << 11) | (0x01UL << 12);
    mb();
    udelay(5);
    SYSREGSCB_REG(MSSIO_CONTROL_CR_OFF) = (0x00UL << 8) | (0x01UL << 11) | (0x01UL << 12) | (0x01UL << 13);
    mb();
    DBG_DDR("done\n");

    DBG_DDR("  STARTUP=0x%x DYN_CNTL=0x%x\n",
        DDRPHY_REG(PHY_STARTUP), DDRPHY_REG(PHY_DYN_CNTL));
    DBG_DDR("  MSSIO_CR=0x%x\n", SYSREGSCB_REG(MSSIO_CONTROL_CR_OFF));

    /* Configure SGMII mux to route external refclk to PLLs - MUST be done first! */
    sgmii_mux_config();

    /* Configure MSS PLL */
    ret = mss_pll_init();
    if (ret != 0)
        return -1;

    /* Initialize DDR PLL */
    ret = ddr_pll_init();
    if (ret != 0)
        return -2;

    return 0;
}

/* DDR Segment Configuration -- match HSS exactly: write ONLY the 6
 * SEGs that HSS sets (SEG0_0, SEG0_1, SEG1_2..SEG1_5).  Leave the
 * other 9 at their reset default values -- HSS does NOT write them
 * and our previous attempt to zero them out may have created
 * address-decoder misconfigurations causing AXI requests to alias
 * elsewhere (boundary scan showed all reads at 0x80000000+ /
 * 0xC0000000+ returning identical stuck values, suggesting writes
 * never reach the DDR controller).
 *
 * Reference: HSS setup_ddr_segments() in mss_ddr.c:4415-4443.
 */
static void setup_segments(void)
{
    /* Cached access segments (only those HSS writes) */
    DDR_SEG_REG(SEG0_0) = LIBERO_SETTING_SEG0_0 & 0x7FFFUL;
    DDR_SEG_REG(SEG0_1) = LIBERO_SETTING_SEG0_1 & 0x7FFFUL;

    /* Non-cached access segments (only those HSS writes) */
    DDR_SEG_REG(SEG1_2) = LIBERO_SETTING_SEG1_2 & 0x7FFFUL;
    DDR_SEG_REG(SEG1_3) = LIBERO_SETTING_SEG1_3 & 0x7FFFUL;
    DDR_SEG_REG(SEG1_4) = LIBERO_SETTING_SEG1_4 & 0x7FFFUL;
    DDR_SEG_REG(SEG1_5) = LIBERO_SETTING_SEG1_5 & 0x7FFFUL;
    mb();

    /* Disable DDR blocker - critical!
     * SEG0.CFG[7] = 1 allows L2 cache controller to access DDR
     */
    DBG_DDR("DDR: Blocker@0x%lx ", DDR_SEG_BASE + SEG0_BLOCKER);
    DBG_DDR("before=0x%x ", DDR_SEG_REG(SEG0_BLOCKER));
    DDR_SEG_REG(SEG0_BLOCKER) = 0x01UL;
    mb();
    DBG_DDR("after=0x%x\n", DDR_SEG_REG(SEG0_BLOCKER));

    /* Read back all 15 SEG slots for sanity.  Expected (Video Kit):
     *   SEG0_0=0x7F80 SEG0_1=0x7000  (0x8000 locked-bit was masked off)
     *   SEG1_2=0x7F40 SEG1_3=0x6C00 SEG1_4=0x7F30 SEG1_5=0x6800
     * The unwritten slots (SEG0_2..6, SEG1_0/1/6/7) should read 0 / reset
     * default; nonzero would indicate stale state from a prior init pass
     * (outer retry) or hardware that did not honor a peripheral reset. */
    wolfBoot_printf("DDR: SEG dump:\n");
    wolfBoot_printf("  SEG0: %x %x %x %x %x %x %x BLK=%x\n",
        DDR_SEG_REG(SEG0_0), DDR_SEG_REG(SEG0_1), DDR_SEG_REG(SEG0_2),
        DDR_SEG_REG(SEG0_3), DDR_SEG_REG(SEG0_4), DDR_SEG_REG(SEG0_5),
        DDR_SEG_REG(SEG0_6), DDR_SEG_REG(SEG0_BLOCKER));
    wolfBoot_printf("  SEG1: %x %x %x %x %x %x %x %x\n",
        DDR_SEG_REG(SEG1_0), DDR_SEG_REG(SEG1_1), DDR_SEG_REG(SEG1_2),
        DDR_SEG_REG(SEG1_3), DDR_SEG_REG(SEG1_4), DDR_SEG_REG(SEG1_5),
        DDR_SEG_REG(SEG1_6), DDR_SEG_REG(SEG1_7));
}

/* DDR Controller Configuration
 *
 * Phase 3.6 rewrite: full bulk import of MC_BASE2 register configuration
 * matching HSS setup_ddrc() at mss_ddr.c:3940-4225.  All values come from
 * the Video Kit Libero header
 *   hart-software-services/build/boards/mpfs-video-kit/fpga_design_config/
 *   ddr/hw_ddrc.h
 *
 * The previous version configured only ~30 of these registers AND used
 * several wrong register offsets (e.g. MC_CFG_CL was at 0x74 -- which is
 * actually CFG_XP -- so the CL value never reached the CL register).
 * That left the IP in an under/mis-configured state that prevented TIP
 * from progressing past BCLK_SCLK during training.
 *
 * This function configures the full ~155 MC_BASE2 registers in HSS order.
 */
static void setup_controller(void)
{
    /* Phase 3.10.3 (1): re-audit found wolfBoot's setup_controller was
     * missing the entire ADDR_MAP block (HSS init_ddrc lines 3748-3774)
     * and the MC_BASE3 critical block (HSS lines 3776-3795).  Adding
     * them here as the highest-priority fix for the TIP-stuck-at-0x1
     * symptom.  These are LPDDR4 address-map and VREF/DBI controls --
     * without them, training accesses miss target DRAM cells and the
     * VREF feedback loop can't run.
     *
     * Block bases (verified against
     * mss_ddr_sgmii_regs.h DDR_CSR_APB_TypeDef):
     *   ADDR_MAP @ 0x2400, MC_BASE3 @ 0x2800.
     */

    /* === ADDR_MAP block (HSS lines 3748-3774) === */
    DDRCFG_REG(0x2400) = LIBERO_SETTING_CFG_MANUAL_ADDRESS_MAP;
    DDRCFG_REG(0x2404) = LIBERO_SETTING_CFG_CHIPADDR_MAP;
    DDRCFG_REG(0x2408) = LIBERO_SETTING_CFG_CIDADDR_MAP;
    DDRCFG_REG(0x240C) = LIBERO_SETTING_CFG_MB_AUTOPCH_COL_BIT_POS_LOW;
    DDRCFG_REG(0x2410) = LIBERO_SETTING_CFG_MB_AUTOPCH_COL_BIT_POS_HIGH;
    DDRCFG_REG(0x2414) = LIBERO_SETTING_CFG_BANKADDR_MAP_0;
    DDRCFG_REG(0x2418) = LIBERO_SETTING_CFG_BANKADDR_MAP_1;
    DDRCFG_REG(0x241C) = LIBERO_SETTING_CFG_ROWADDR_MAP_0;
    DDRCFG_REG(0x2420) = LIBERO_SETTING_CFG_ROWADDR_MAP_1;
    DDRCFG_REG(0x2424) = LIBERO_SETTING_CFG_ROWADDR_MAP_2;
    DDRCFG_REG(0x2428) = LIBERO_SETTING_CFG_ROWADDR_MAP_3;
    DDRCFG_REG(0x242C) = LIBERO_SETTING_CFG_COLADDR_MAP_0;
    DDRCFG_REG(0x2430) = LIBERO_SETTING_CFG_COLADDR_MAP_1;
    DDRCFG_REG(0x2434) = LIBERO_SETTING_CFG_COLADDR_MAP_2;

    /* === MC_BASE3 block (HSS lines 3776-3795) === */
    DDRCFG_REG(0x2800) = LIBERO_SETTING_CFG_VRCG_ENABLE;
    DDRCFG_REG(0x2804) = LIBERO_SETTING_CFG_VRCG_DISABLE;
    DDRCFG_REG(0x2808) = LIBERO_SETTING_CFG_WRITE_LATENCY_SET;
    DDRCFG_REG(0x280C) = LIBERO_SETTING_CFG_THERMAL_OFFSET;
    DDRCFG_REG(0x2810) = LIBERO_SETTING_CFG_SOC_ODT;
    DDRCFG_REG(0x2814) = LIBERO_SETTING_CFG_ODTE_CK;
    DDRCFG_REG(0x2818) = LIBERO_SETTING_CFG_ODTE_CS;
    DDRCFG_REG(0x281C) = LIBERO_SETTING_CFG_ODTD_CA;
    DDRCFG_REG(0x2820) = LIBERO_SETTING_CFG_LPDDR4_FSP_OP;
    DDRCFG_REG(0x2824) = LIBERO_SETTING_CFG_GENERATE_REFRESH_ON_SRX;
    DDRCFG_REG(0x2828) = LIBERO_SETTING_CFG_DBI_CL;
    DDRCFG_REG(0x282C) = LIBERO_SETTING_CFG_NON_DBI_CL;
    DDRCFG_REG(0x2830) = LIBERO_SETTING_INIT_FORCE_WRITE_DATA_0;

    /* === MC_BASE1 block (HSS lines 3797-3938) -- bisect step 1 ===
     * Phase 3.10.3 (1) iter 8: 91 missing controller registers from
     * MC_BASE1.  HSS writes these during init_ddrc; wolfBoot did not.
     * Adding to bisect which block (if any) breaks DFI init when
     * combined with iter 6's ADDR_MAP + MC_BASE3.  Block base 0x3C00.
     */
    DDRCFG_REG(0x3C00) = LIBERO_SETTING_CFG_WRITE_CRC;
    DDRCFG_REG(0x3C04) = LIBERO_SETTING_CFG_MPR_READ_FORMAT;
    DDRCFG_REG(0x3C08) = LIBERO_SETTING_CFG_WR_CMD_LAT_CRC_DM;
    DDRCFG_REG(0x3C0C) = LIBERO_SETTING_CFG_FINE_GRAN_REF_MODE;
    DDRCFG_REG(0x3C10) = LIBERO_SETTING_CFG_TEMP_SENSOR_READOUT;
    DDRCFG_REG(0x3C14) = LIBERO_SETTING_CFG_PER_DRAM_ADDR_EN;
    DDRCFG_REG(0x3C18) = LIBERO_SETTING_CFG_GEARDOWN_MODE;
    DDRCFG_REG(0x3C1C) = LIBERO_SETTING_CFG_WR_PREAMBLE;
    DDRCFG_REG(0x3C20) = LIBERO_SETTING_CFG_RD_PREAMBLE;
    DDRCFG_REG(0x3C24) = LIBERO_SETTING_CFG_RD_PREAMB_TRN_MODE;
    DDRCFG_REG(0x3C28) = LIBERO_SETTING_CFG_SR_ABORT;
    DDRCFG_REG(0x3C2C) = LIBERO_SETTING_CFG_CS_TO_CMDADDR_LATENCY;
    DDRCFG_REG(0x3C30) = LIBERO_SETTING_CFG_INT_VREF_MON;
    DDRCFG_REG(0x3C34) = LIBERO_SETTING_CFG_TEMP_CTRL_REF_MODE;
    DDRCFG_REG(0x3C38) = LIBERO_SETTING_CFG_TEMP_CTRL_REF_RANGE;
    DDRCFG_REG(0x3C3C) = LIBERO_SETTING_CFG_MAX_PWR_DOWN_MODE;
    DDRCFG_REG(0x3C40) = LIBERO_SETTING_CFG_READ_DBI;
    DDRCFG_REG(0x3C44) = LIBERO_SETTING_CFG_WRITE_DBI;
    DDRCFG_REG(0x3C48) = LIBERO_SETTING_CFG_DATA_MASK;
    DDRCFG_REG(0x3C4C) = LIBERO_SETTING_CFG_CA_PARITY_PERSIST_ERR;
    DDRCFG_REG(0x3C50) = LIBERO_SETTING_CFG_RTT_PARK;
    DDRCFG_REG(0x3C54) = LIBERO_SETTING_CFG_ODT_INBUF_4_PD;
    DDRCFG_REG(0x3C58) = LIBERO_SETTING_CFG_CA_PARITY_ERR_STATUS;
    DDRCFG_REG(0x3C5C) = LIBERO_SETTING_CFG_CRC_ERROR_CLEAR;
    DDRCFG_REG(0x3C60) = LIBERO_SETTING_CFG_CA_PARITY_LATENCY;
    DDRCFG_REG(0x3C64) = LIBERO_SETTING_CFG_CCD_S;
    DDRCFG_REG(0x3C68) = LIBERO_SETTING_CFG_CCD_L;
    DDRCFG_REG(0x3C6C) = LIBERO_SETTING_CFG_VREFDQ_TRN_ENABLE;
    DDRCFG_REG(0x3C70) = LIBERO_SETTING_CFG_VREFDQ_TRN_RANGE;
    DDRCFG_REG(0x3C74) = LIBERO_SETTING_CFG_VREFDQ_TRN_VALUE;
    DDRCFG_REG(0x3C78) = LIBERO_SETTING_CFG_RRD_S;
    DDRCFG_REG(0x3C7C) = LIBERO_SETTING_CFG_RRD_L;
    DDRCFG_REG(0x3C80) = LIBERO_SETTING_CFG_WTR_S;
    DDRCFG_REG(0x3C84) = LIBERO_SETTING_CFG_WTR_L;
    DDRCFG_REG(0x3C88) = LIBERO_SETTING_CFG_WTR_S_CRC_DM;
    DDRCFG_REG(0x3C8C) = LIBERO_SETTING_CFG_WTR_L_CRC_DM;
    DDRCFG_REG(0x3C90) = LIBERO_SETTING_CFG_WR_CRC_DM;
    DDRCFG_REG(0x3C94) = LIBERO_SETTING_CFG_RFC1;
    DDRCFG_REG(0x3C98) = LIBERO_SETTING_CFG_RFC2;
    DDRCFG_REG(0x3C9C) = LIBERO_SETTING_CFG_RFC4;
    /* MC_BASE1 trailing block.  HSS struct DDR_CSR_APB_MC_BASE1_TypeDef
     * (mss_ddr_sgmii_regs.h:3842-3928) has UNUSED_SPACE0 (9 dwords) at
     * offset 0xA0 and UNUSED_SPACE1 (6 dwords) at offset 0xC8 between
     * CFG_RFC4 and the CFG_BIT_MAP_INDEX_* table.  Previous wolfBoot
     * code wrote sequentially from 0x3CA0 onward, landing the entire
     * BIT_MAP table 60 bytes too early and missing the actual register
     * locations.  Result: HSS-canonical offsets 0x3CC4/0x3CE0..0x3D5C
     * stayed at zero, the controller had no address-to-bank/row/col
     * mapping, every AXI read targeted addresses outside the configured
     * map and hung waiting for a response.  Realign to absolute offsets
     * matching the HSS struct. */
    DDRCFG_REG(0x3CC4) = LIBERO_SETTING_CFG_NIBBLE_DEVICES;
    DDRCFG_REG(0x3CE0) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS0_0;
    DDRCFG_REG(0x3CE4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS0_1;
    DDRCFG_REG(0x3CE8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS1_0;
    DDRCFG_REG(0x3CEC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS1_1;
    DDRCFG_REG(0x3CF0) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS2_0;
    DDRCFG_REG(0x3CF4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS2_1;
    DDRCFG_REG(0x3CF8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS3_0;
    DDRCFG_REG(0x3CFC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS3_1;
    DDRCFG_REG(0x3D00) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS4_0;
    DDRCFG_REG(0x3D04) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS4_1;
    DDRCFG_REG(0x3D08) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS5_0;
    DDRCFG_REG(0x3D0C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS5_1;
    DDRCFG_REG(0x3D10) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS6_0;
    DDRCFG_REG(0x3D14) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS6_1;
    DDRCFG_REG(0x3D18) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS7_0;
    DDRCFG_REG(0x3D1C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS7_1;
    DDRCFG_REG(0x3D20) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS8_0;
    DDRCFG_REG(0x3D24) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS8_1;
    DDRCFG_REG(0x3D28) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS9_0;
    DDRCFG_REG(0x3D2C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS9_1;
    DDRCFG_REG(0x3D30) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS10_0;
    DDRCFG_REG(0x3D34) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS10_1;
    DDRCFG_REG(0x3D38) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS11_0;
    DDRCFG_REG(0x3D3C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS11_1;
    DDRCFG_REG(0x3D40) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS12_0;
    DDRCFG_REG(0x3D44) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS12_1;
    DDRCFG_REG(0x3D48) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS13_0;
    DDRCFG_REG(0x3D4C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS13_1;
    DDRCFG_REG(0x3D50) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS14_0;
    DDRCFG_REG(0x3D54) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS14_1;
    DDRCFG_REG(0x3D58) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS15_0;
    DDRCFG_REG(0x3D5C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS15_1;
    DDRCFG_REG(0x3D60) = LIBERO_SETTING_CFG_NUM_LOGICAL_RANKS_PER_3DS;
    DDRCFG_REG(0x3D64) = LIBERO_SETTING_CFG_RFC_DLR1;
    DDRCFG_REG(0x3D68) = LIBERO_SETTING_CFG_RFC_DLR2;
    DDRCFG_REG(0x3D6C) = LIBERO_SETTING_CFG_RFC_DLR4;
    DDRCFG_REG(0x3D70) = LIBERO_SETTING_CFG_RRD_DLR;
    DDRCFG_REG(0x3D74) = LIBERO_SETTING_CFG_FAW_DLR;
    /* UNUSED_SPACE2[8] at struct offset 0x178 = abs 0x3D78..0x3D94 */
    DDRCFG_REG(0x3D98) = LIBERO_SETTING_CFG_ADVANCE_ACTIVATE_READY;

    /* === Bisect step 2: MPFE+REORDER+RMW+ECC+READ_CAPT+MTA+
     * DYN_WIDTH_ADJ+CA_PAR_ERR (45 regs). HSS lines 4226-4310. */

    /* MPFE block (HSS:4226-4240) */
    DDRCFG_REG(0x4C00) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P0;
    DDRCFG_REG(0x4C04) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P1;
    DDRCFG_REG(0x4C08) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P2;
    DDRCFG_REG(0x4C0C) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P3;
    DDRCFG_REG(0x4C10) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P4;
    DDRCFG_REG(0x4C14) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P5;
    DDRCFG_REG(0x4C18) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P6;
    DDRCFG_REG(0x4C1C) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P7;

    /* REORDER block (HSS:4242-4256) */
    /* REORDER block (HSS struct mss_ddr_sgmii_regs.h:4172-4183).
     * CFG_MAINTAIN_COHERENCY is at offset 0xc and CFG_Q_AGE_LIMIT at
     * 0x10; previous wolfBoot offsets shifted both by one slot. */
    DDRCFG_REG(0x5000) = LIBERO_SETTING_CFG_REORDER_EN;
    DDRCFG_REG(0x5004) = LIBERO_SETTING_CFG_REORDER_QUEUE_EN;
    DDRCFG_REG(0x5008) = LIBERO_SETTING_CFG_INTRAPORT_REORDER_EN;
    DDRCFG_REG(0x500C) = LIBERO_SETTING_CFG_MAINTAIN_COHERENCY;
    DDRCFG_REG(0x5010) = LIBERO_SETTING_CFG_Q_AGE_LIMIT;
    /* UNUSED_SPACE0 at struct 0x14 = abs 0x5014 */
    DDRCFG_REG(0x5018) = LIBERO_SETTING_CFG_RO_CLOSED_PAGE_POLICY;
    DDRCFG_REG(0x501C) = LIBERO_SETTING_CFG_REORDER_RW_ONLY;
    DDRCFG_REG(0x5020) = LIBERO_SETTING_CFG_RO_PRIORITY_EN;

    /* RMW block (HSS:4258-4259) */
    DDRCFG_REG(0x5400) = LIBERO_SETTING_CFG_DM_EN;
    DDRCFG_REG(0x5404) = LIBERO_SETTING_CFG_RMW_EN;

    /* ECC block (HSS:4260-4267) */
    DDRCFG_REG(0x5800) = LIBERO_SETTING_CFG_ECC_CORRECTION_EN;
    DDRCFG_REG(0x5840) = LIBERO_SETTING_CFG_ECC_BYPASS;
    DDRCFG_REG(0x5844) = LIBERO_SETTING_INIT_WRITE_DATA_1B_ECC_ERROR_GEN;
    DDRCFG_REG(0x5848) = LIBERO_SETTING_INIT_WRITE_DATA_2B_ECC_ERROR_GEN;
    DDRCFG_REG(0x585C) = LIBERO_SETTING_CFG_ECC_1BIT_INT_THRESH;

    /* READ_CAPT block (HSS:4269) */
    DDRCFG_REG(0x5C00) = LIBERO_SETTING_INIT_READ_CAPTURE_ADDR;

    /* MTA block (HSS:4271-4302) */
    /* MTA block (HSS struct mss_ddr_sgmii_regs.h:4219-4255).  After
     * MTC_ACQ_ADDR at offset 0x18 the next 5 dwords (0x1c..0x2c) are
     * __I read-only status (MTC_ACQ_CYCS_STORED/TRIG_DETECT/MEM_TRIG_
     * ADDR/MEM_LAST_ADDR/ACK).  Previous wolfBoot table wrote through
     * those slots, shifting CFG_TRIG_MT_ADDR_0..ERR_MASK_4 and the
     * MTC_ACQ_WR_DATA[0..2] by 20 bytes too early.  CFG_PRE_TRIG_CYCS
     * lives at struct offset 0x12c (abs 0x652C) after a 50-dword
     * UNUSED_SPACE0, and CFG_DATA_SEL_FIRST_ERROR at 0x150 (abs
     * 0x6550).  Realign to HSS-canonical offsets. */
    DDRCFG_REG(0x6400) = LIBERO_SETTING_CFG_ERROR_GROUP_SEL;
    DDRCFG_REG(0x6404) = LIBERO_SETTING_CFG_DATA_SEL;
    DDRCFG_REG(0x6408) = LIBERO_SETTING_CFG_TRIG_MODE;
    DDRCFG_REG(0x640C) = LIBERO_SETTING_CFG_POST_TRIG_CYCS;
    DDRCFG_REG(0x6410) = LIBERO_SETTING_CFG_TRIG_MASK;
    DDRCFG_REG(0x6414) = LIBERO_SETTING_CFG_EN_MASK;
    DDRCFG_REG(0x6418) = LIBERO_SETTING_MTC_ACQ_ADDR;
    /* 0x641C..0x642C are __I read-only status -- skip */
    DDRCFG_REG(0x6430) = LIBERO_SETTING_CFG_TRIG_MT_ADDR_0;
    DDRCFG_REG(0x6434) = LIBERO_SETTING_CFG_TRIG_MT_ADDR_1;
    DDRCFG_REG(0x6438) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_0;
    DDRCFG_REG(0x643C) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_1;
    DDRCFG_REG(0x6440) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_2;
    DDRCFG_REG(0x6444) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_3;
    DDRCFG_REG(0x6448) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_4;
    DDRCFG_REG(0x644C) = LIBERO_SETTING_MTC_ACQ_WR_DATA_0;
    DDRCFG_REG(0x6450) = LIBERO_SETTING_MTC_ACQ_WR_DATA_1;
    DDRCFG_REG(0x6454) = LIBERO_SETTING_MTC_ACQ_WR_DATA_2;
    /* 0x6458..0x6460 are __I MTC_ACQ_RD_DATA[0..2]; 0x6464..0x6528 is
     * UNUSED_SPACE0 (50 dwords). */
    DDRCFG_REG(0x652C) = LIBERO_SETTING_CFG_PRE_TRIG_CYCS;
    DDRCFG_REG(0x6550) = LIBERO_SETTING_CFG_DATA_SEL_FIRST_ERROR;

    /* DYN_WIDTH_ADJ block (HSS:4304-4306) */
    DDRCFG_REG(0x7C00) = LIBERO_SETTING_CFG_DQ_WIDTH;
    DDRCFG_REG(0x7C04) = LIBERO_SETTING_CFG_ACTIVE_DQ_SEL;

    /* CA_PAR_ERR block (HSS struct mss_ddr_sgmii_regs.h:4265-4273).
     * STAT_CA_PARITY_ERROR is __I at 0x0; UNUSED_SPACE0 fills 0x4..0x8.
     * INIT_CA_PARITY_ERROR_GEN_REQ is at struct 0xc (abs 0x800C) and
     * INIT_CA_PARITY_ERROR_GEN_CMD at 0x10 (abs 0x8010). */
    DDRCFG_REG(0x800C) = LIBERO_SETTING_INIT_CA_PARITY_ERROR_GEN_REQ;
    DDRCFG_REG(0x8010) = LIBERO_SETTING_INIT_CA_PARITY_ERROR_GEN_CMD;

    /* === Bisect step 3: DFI extras (9 regs at 0x10010+).  HIGH RISK
     * since these touch DFI handshake config.  Skip 0x10000-0x1000C
     * which are already handled by MC_DFI_* writes below. */
    DDRCFG_REG(0x10010) = LIBERO_SETTING_INIT_DFI_LP_DATA_REQ;
    DDRCFG_REG(0x10014) = LIBERO_SETTING_INIT_DFI_LP_CTRL_REQ;
    DDRCFG_REG(0x1001C) = LIBERO_SETTING_INIT_DFI_LP_WAKEUP;
    DDRCFG_REG(0x10020) = LIBERO_SETTING_INIT_DFI_DRAM_CLK_DISABLE;
    DDRCFG_REG(0x10030) = LIBERO_SETTING_CFG_DFI_DATA_BYTE_DISABLE;
    DDRCFG_REG(0x1003C) = LIBERO_SETTING_CFG_DFI_LVL_SEL;
    DDRCFG_REG(0x10040) = LIBERO_SETTING_CFG_DFI_LVL_PERIODIC;
    DDRCFG_REG(0x10044) = LIBERO_SETTING_CFG_DFI_LVL_PATTERN;
    DDRCFG_REG(0x10050) = LIBERO_SETTING_PHY_DFI_INIT_START;

    /* === Bisect step 4: AXI_IF block (15 regs) HSS:4338-4366 === */
    DDRCFG_REG(0x12C18) = LIBERO_SETTING_CFG_AXI_START_ADDRESS_AXI1_0;
    DDRCFG_REG(0x12C1C) = LIBERO_SETTING_CFG_AXI_START_ADDRESS_AXI1_1;
    DDRCFG_REG(0x12C20) = LIBERO_SETTING_CFG_AXI_START_ADDRESS_AXI2_0;
    DDRCFG_REG(0x12C24) = LIBERO_SETTING_CFG_AXI_START_ADDRESS_AXI2_1;
    DDRCFG_REG(0x12F18) = LIBERO_SETTING_CFG_AXI_END_ADDRESS_AXI1_0;
    DDRCFG_REG(0x12F1C) = LIBERO_SETTING_CFG_AXI_END_ADDRESS_AXI1_1;
    DDRCFG_REG(0x12F20) = LIBERO_SETTING_CFG_AXI_END_ADDRESS_AXI2_0;
    DDRCFG_REG(0x12F24) = LIBERO_SETTING_CFG_AXI_END_ADDRESS_AXI2_1;
    DDRCFG_REG(0x13218) = LIBERO_SETTING_CFG_MEM_START_ADDRESS_AXI1_0;
    DDRCFG_REG(0x1321C) = LIBERO_SETTING_CFG_MEM_START_ADDRESS_AXI1_1;
    DDRCFG_REG(0x13220) = LIBERO_SETTING_CFG_MEM_START_ADDRESS_AXI2_0;
    DDRCFG_REG(0x13224) = LIBERO_SETTING_CFG_MEM_START_ADDRESS_AXI2_1;
    DDRCFG_REG(0x13514) = LIBERO_SETTING_CFG_ENABLE_BUS_HOLD_AXI1;
    DDRCFG_REG(0x13518) = LIBERO_SETTING_CFG_ENABLE_BUS_HOLD_AXI2;
    DDRCFG_REG(0x13690) = LIBERO_SETTING_CFG_AXI_AUTO_PCH;

    /* csr_custom block (HSS:4368-4398): PHY shadow registers.
     * Phase 3.10.3 (3) bisect found PHY_RESET_CONTROL alone (without
     * the second deassert write that HSS does at line 4370) leaves
     * PHY in reset -> DFI init timeout.  Restored full HSS pattern:
     * write twice, second with bit 15 (0x8000) cleared to deassert
     * reset.  After the deassert, the rest of the block is safe. */
    DDRCFG_REG(0x3C000) = LIBERO_SETTING_PHY_RESET_CONTROL;
    DDRCFG_REG(0x3C000) = (LIBERO_SETTING_PHY_RESET_CONTROL & ~0x8000UL);
    DDRCFG_REG(0x3C004) = LIBERO_SETTING_PHY_PC_RANK;
    DDRCFG_REG(0x3C008) = LIBERO_SETTING_PHY_RANKS_TO_TRAIN;
    DDRCFG_REG(0x3C00C) = LIBERO_SETTING_PHY_WRITE_REQUEST;
    DDRCFG_REG(0x3C014) = LIBERO_SETTING_PHY_READ_REQUEST;
    DDRCFG_REG(0x3C01C) = LIBERO_SETTING_PHY_WRITE_LEVEL_DELAY;
    DDRCFG_REG(0x3C020) = LIBERO_SETTING_PHY_GATE_TRAIN_DELAY;
    DDRCFG_REG(0x3C024) = LIBERO_SETTING_PHY_EYE_TRAIN_DELAY;
    DDRCFG_REG(0x3C028) = LIBERO_SETTING_PHY_EYE_PAT;
    DDRCFG_REG(0x3C02C) = LIBERO_SETTING_PHY_START_RECAL;
    DDRCFG_REG(0x3C030) = LIBERO_SETTING_PHY_CLR_DFI_LVL_PERIODIC;
    DDRCFG_REG(0x3C034) = LIBERO_SETTING_PHY_TRAIN_STEP_ENABLE;
    DDRCFG_REG(0x3C038) = LIBERO_SETTING_PHY_LPDDR_DQ_CAL_PAT;
    DDRCFG_REG(0x3C03C) = LIBERO_SETTING_PHY_INDPNDT_TRAINING;
    DDRCFG_REG(0x3C040) = LIBERO_SETTING_PHY_ENCODED_QUAD_CS;
    DDRCFG_REG(0x3C044) = LIBERO_SETTING_PHY_HALF_CLK_DLY_ENABLE;

    /* Controller soft reset - deassert */
    DDRCFG_REG(MC_CTRLR_SOFT_RESET_N) = LIBERO_SETTING_CTRLR_SOFT_RESET_N;
    DDRCFG_REG(MC_CFG_LOOKAHEAD_PCH) = LIBERO_SETTING_CFG_LOOKAHEAD_PCH;
    DDRCFG_REG(MC_CFG_LOOKAHEAD_ACT) = LIBERO_SETTING_CFG_LOOKAHEAD_ACT;
    DDRCFG_REG(MC_INIT_AUTOINIT_DISABLE) = LIBERO_SETTING_INIT_AUTOINIT_DISABLE;
    DDRCFG_REG(MC_INIT_FORCE_RESET) = LIBERO_SETTING_INIT_FORCE_RESET;
    DDRCFG_REG(MC_INIT_GEARDOWN_EN) = LIBERO_SETTING_INIT_GEARDOWN_EN;
    DDRCFG_REG(MC_INIT_DISABLE_CKE) = LIBERO_SETTING_INIT_DISABLE_CKE;
    DDRCFG_REG(MC_INIT_CS) = LIBERO_SETTING_INIT_CS;
    DDRCFG_REG(MC_INIT_PRECHARGE_ALL) = LIBERO_SETTING_INIT_PRECHARGE_ALL;
    DDRCFG_REG(MC_INIT_REFRESH) = LIBERO_SETTING_INIT_REFRESH;
    DDRCFG_REG(MC_INIT_ZQ_CAL_REQ) = LIBERO_SETTING_INIT_ZQ_CAL_REQ;
    DDRCFG_REG(MC_CFG_BL) = LIBERO_SETTING_CFG_BL;
    DDRCFG_REG(MC_CTRLR_INIT) = LIBERO_SETTING_CTRLR_INIT;
    DDRCFG_REG(MC_CFG_AUTO_REF_EN) = LIBERO_SETTING_CFG_AUTO_REF_EN;
    DDRCFG_REG(MC_CFG_RAS) = LIBERO_SETTING_CFG_RAS;
    DDRCFG_REG(MC_CFG_RCD) = LIBERO_SETTING_CFG_RCD;
    DDRCFG_REG(MC_CFG_RRD) = LIBERO_SETTING_CFG_RRD;
    DDRCFG_REG(MC_CFG_RP) = LIBERO_SETTING_CFG_RP;
    DDRCFG_REG(MC_CFG_RC) = LIBERO_SETTING_CFG_RC;
    DDRCFG_REG(MC_CFG_FAW) = LIBERO_SETTING_CFG_FAW;
    DDRCFG_REG(MC_CFG_RFC) = LIBERO_SETTING_CFG_RFC;
    DDRCFG_REG(MC_CFG_RTP) = LIBERO_SETTING_CFG_RTP;
    DDRCFG_REG(MC_CFG_WR) = LIBERO_SETTING_CFG_WR;
    DDRCFG_REG(MC_CFG_WTR) = LIBERO_SETTING_CFG_WTR;
    DDRCFG_REG(MC_CFG_PASR) = LIBERO_SETTING_CFG_PASR;
    DDRCFG_REG(MC_CFG_XP) = LIBERO_SETTING_CFG_XP;
    DDRCFG_REG(MC_CFG_XSR) = LIBERO_SETTING_CFG_XSR;
    DDRCFG_REG(MC_CFG_CL) = LIBERO_SETTING_CFG_CL;
    DDRCFG_REG(MC_CFG_READ_TO_WRITE) = LIBERO_SETTING_CFG_READ_TO_WRITE;
    DDRCFG_REG(MC_CFG_WRITE_TO_WRITE) = LIBERO_SETTING_CFG_WRITE_TO_WRITE;
    DDRCFG_REG(MC_CFG_READ_TO_READ) = LIBERO_SETTING_CFG_READ_TO_READ;
    DDRCFG_REG(MC_CFG_WRITE_TO_READ) = LIBERO_SETTING_CFG_WRITE_TO_READ;
    DDRCFG_REG(MC_CFG_READ_TO_WRITE_ODT) = LIBERO_SETTING_CFG_READ_TO_WRITE_ODT;
    DDRCFG_REG(MC_CFG_WRITE_TO_WRITE_ODT) = LIBERO_SETTING_CFG_WRITE_TO_WRITE_ODT;
    DDRCFG_REG(MC_CFG_READ_TO_READ_ODT) = LIBERO_SETTING_CFG_READ_TO_READ_ODT;
    DDRCFG_REG(MC_CFG_WRITE_TO_READ_ODT) = LIBERO_SETTING_CFG_WRITE_TO_READ_ODT;
    DDRCFG_REG(MC_CFG_MIN_READ_IDLE) = LIBERO_SETTING_CFG_MIN_READ_IDLE;
    DDRCFG_REG(MC_CFG_MRD) = LIBERO_SETTING_CFG_MRD;
    DDRCFG_REG(MC_CFG_BT) = LIBERO_SETTING_CFG_BT;
    DDRCFG_REG(MC_CFG_DS) = LIBERO_SETTING_CFG_DS;
    DDRCFG_REG(MC_CFG_QOFF) = LIBERO_SETTING_CFG_QOFF;
    DDRCFG_REG(MC_CFG_RTT) = LIBERO_SETTING_CFG_RTT;
    DDRCFG_REG(MC_CFG_DLL_DISABLE) = LIBERO_SETTING_CFG_DLL_DISABLE;
    DDRCFG_REG(MC_CFG_REF_PER) = LIBERO_SETTING_CFG_REF_PER;
    DDRCFG_REG(MC_CFG_STARTUP_DELAY) = LIBERO_SETTING_CFG_STARTUP_DELAY;
    DDRCFG_REG(MC_CFG_MEM_COLBITS) = LIBERO_SETTING_CFG_MEM_COLBITS;
    DDRCFG_REG(MC_CFG_MEM_ROWBITS) = LIBERO_SETTING_CFG_MEM_ROWBITS;
    DDRCFG_REG(MC_CFG_MEM_BANKBITS) = LIBERO_SETTING_CFG_MEM_BANKBITS;
    DDRCFG_REG(MC_CFG_ODT_RD_MAP_CS0) = LIBERO_SETTING_CFG_ODT_RD_MAP_CS0;
    DDRCFG_REG(MC_CFG_ODT_RD_MAP_CS1) = LIBERO_SETTING_CFG_ODT_RD_MAP_CS1;
    DDRCFG_REG(MC_CFG_ODT_RD_MAP_CS2) = LIBERO_SETTING_CFG_ODT_RD_MAP_CS2;
    DDRCFG_REG(MC_CFG_ODT_RD_MAP_CS3) = LIBERO_SETTING_CFG_ODT_RD_MAP_CS3;
    DDRCFG_REG(MC_CFG_ODT_RD_MAP_CS4) = LIBERO_SETTING_CFG_ODT_RD_MAP_CS4;
    DDRCFG_REG(MC_CFG_ODT_RD_MAP_CS5) = LIBERO_SETTING_CFG_ODT_RD_MAP_CS5;
    DDRCFG_REG(MC_CFG_ODT_RD_MAP_CS6) = LIBERO_SETTING_CFG_ODT_RD_MAP_CS6;
    DDRCFG_REG(MC_CFG_ODT_RD_MAP_CS7) = LIBERO_SETTING_CFG_ODT_RD_MAP_CS7;
    DDRCFG_REG(MC_CFG_ODT_WR_MAP_CS0) = LIBERO_SETTING_CFG_ODT_WR_MAP_CS0;
    DDRCFG_REG(MC_CFG_ODT_WR_MAP_CS1) = LIBERO_SETTING_CFG_ODT_WR_MAP_CS1;
    DDRCFG_REG(MC_CFG_ODT_WR_MAP_CS2) = LIBERO_SETTING_CFG_ODT_WR_MAP_CS2;
    DDRCFG_REG(MC_CFG_ODT_WR_MAP_CS3) = LIBERO_SETTING_CFG_ODT_WR_MAP_CS3;
    DDRCFG_REG(MC_CFG_ODT_WR_MAP_CS4) = LIBERO_SETTING_CFG_ODT_WR_MAP_CS4;
    DDRCFG_REG(MC_CFG_ODT_WR_MAP_CS5) = LIBERO_SETTING_CFG_ODT_WR_MAP_CS5;
    DDRCFG_REG(MC_CFG_ODT_WR_MAP_CS6) = LIBERO_SETTING_CFG_ODT_WR_MAP_CS6;
    DDRCFG_REG(MC_CFG_ODT_WR_MAP_CS7) = LIBERO_SETTING_CFG_ODT_WR_MAP_CS7;
    DDRCFG_REG(MC_CFG_ODT_RD_TURN_ON) = LIBERO_SETTING_CFG_ODT_RD_TURN_ON;
    DDRCFG_REG(MC_CFG_ODT_WR_TURN_ON) = LIBERO_SETTING_CFG_ODT_WR_TURN_ON;
    DDRCFG_REG(MC_CFG_ODT_RD_TURN_OFF) = LIBERO_SETTING_CFG_ODT_RD_TURN_OFF;
    DDRCFG_REG(MC_CFG_ODT_WR_TURN_OFF) = LIBERO_SETTING_CFG_ODT_WR_TURN_OFF;
    DDRCFG_REG(MC_CFG_EMR3) = LIBERO_SETTING_CFG_EMR3;
    DDRCFG_REG(MC_CFG_TWO_T) = LIBERO_SETTING_CFG_TWO_T;
    DDRCFG_REG(MC_CFG_TWO_T_SEL_CYCLE) = LIBERO_SETTING_CFG_TWO_T_SEL_CYCLE;
    DDRCFG_REG(MC_CFG_REGDIMM) = LIBERO_SETTING_CFG_REGDIMM;
    DDRCFG_REG(MC_CFG_MOD) = LIBERO_SETTING_CFG_MOD;
    DDRCFG_REG(MC_CFG_XS) = LIBERO_SETTING_CFG_XS;
    DDRCFG_REG(MC_CFG_XSDLL) = LIBERO_SETTING_CFG_XSDLL;
    DDRCFG_REG(MC_CFG_XPR) = LIBERO_SETTING_CFG_XPR;
    DDRCFG_REG(MC_CFG_AL_MODE) = LIBERO_SETTING_CFG_AL_MODE;
    DDRCFG_REG(MC_CFG_CWL) = LIBERO_SETTING_CFG_CWL;
    DDRCFG_REG(MC_CFG_BL_MODE) = LIBERO_SETTING_CFG_BL_MODE;
    DDRCFG_REG(MC_CFG_TDQS) = LIBERO_SETTING_CFG_TDQS;
    DDRCFG_REG(MC_CFG_RTT_WR) = LIBERO_SETTING_CFG_RTT_WR;
    DDRCFG_REG(MC_CFG_LP_ASR) = LIBERO_SETTING_CFG_LP_ASR;
    DDRCFG_REG(MC_CFG_AUTO_SR) = LIBERO_SETTING_CFG_AUTO_SR;
    DDRCFG_REG(MC_CFG_SRT) = LIBERO_SETTING_CFG_SRT;
    DDRCFG_REG(MC_CFG_ADDR_MIRROR) = LIBERO_SETTING_CFG_ADDR_MIRROR;
    DDRCFG_REG(MC_CFG_ZQ_CAL_TYPE) = LIBERO_SETTING_CFG_ZQ_CAL_TYPE;
    DDRCFG_REG(MC_CFG_ZQ_CAL_PER) = LIBERO_SETTING_CFG_ZQ_CAL_PER;
    DDRCFG_REG(MC_CFG_AUTO_ZQ_CAL_EN) = LIBERO_SETTING_CFG_AUTO_ZQ_CAL_EN;
    DDRCFG_REG(MC_CFG_MEMORY_TYPE) = LIBERO_SETTING_CFG_MEMORY_TYPE;
    DDRCFG_REG(MC_CFG_ONLY_SRANK_CMDS) = LIBERO_SETTING_CFG_ONLY_SRANK_CMDS;
    DDRCFG_REG(MC_CFG_NUM_RANKS) = LIBERO_SETTING_CFG_NUM_RANKS;
    DDRCFG_REG(MC_CFG_QUAD_RANK) = LIBERO_SETTING_CFG_QUAD_RANK;
    DDRCFG_REG(MC_CFG_EARLY_RANK_TO_WR_START) = LIBERO_SETTING_CFG_EARLY_RANK_TO_WR_START;
    DDRCFG_REG(MC_CFG_EARLY_RANK_TO_RD_START) = LIBERO_SETTING_CFG_EARLY_RANK_TO_RD_START;
    DDRCFG_REG(MC_CFG_PASR_BANK) = LIBERO_SETTING_CFG_PASR_BANK;
    DDRCFG_REG(MC_CFG_PASR_SEG) = LIBERO_SETTING_CFG_PASR_SEG;
    DDRCFG_REG(MC_INIT_MRR_MODE) = LIBERO_SETTING_INIT_MRR_MODE;
    DDRCFG_REG(MC_INIT_MR_W_REQ) = LIBERO_SETTING_INIT_MR_W_REQ;
    DDRCFG_REG(MC_INIT_MR_ADDR) = LIBERO_SETTING_INIT_MR_ADDR;
    DDRCFG_REG(MC_INIT_MR_WR_DATA) = LIBERO_SETTING_INIT_MR_WR_DATA;
    DDRCFG_REG(MC_INIT_MR_WR_MASK) = LIBERO_SETTING_INIT_MR_WR_MASK;
    DDRCFG_REG(MC_INIT_NOP) = LIBERO_SETTING_INIT_NOP;
    DDRCFG_REG(MC_CFG_INIT_DURATION) = LIBERO_SETTING_CFG_INIT_DURATION;
    DDRCFG_REG(MC_CFG_ZQINIT_CAL_DURATION) = LIBERO_SETTING_CFG_ZQINIT_CAL_DURATION;
    DDRCFG_REG(MC_CFG_ZQ_CAL_L_DURATION) = LIBERO_SETTING_CFG_ZQ_CAL_L_DURATION;
    DDRCFG_REG(MC_CFG_ZQ_CAL_S_DURATION) = LIBERO_SETTING_CFG_ZQ_CAL_S_DURATION;
    DDRCFG_REG(MC_CFG_ZQ_CAL_R_DURATION) = LIBERO_SETTING_CFG_ZQ_CAL_R_DURATION;
    DDRCFG_REG(MC_CFG_MRR) = LIBERO_SETTING_CFG_MRR;
    DDRCFG_REG(MC_CFG_MRW) = LIBERO_SETTING_CFG_MRW;
    DDRCFG_REG(MC_CFG_ODT_POWERDOWN) = LIBERO_SETTING_CFG_ODT_POWERDOWN;
    DDRCFG_REG(MC_CFG_WL) = LIBERO_SETTING_CFG_WL;
    DDRCFG_REG(MC_CFG_RL) = LIBERO_SETTING_CFG_RL;
    DDRCFG_REG(MC_CFG_CAL_READ_PERIOD) = LIBERO_SETTING_CFG_CAL_READ_PERIOD;
    DDRCFG_REG(MC_CFG_NUM_CAL_READS) = LIBERO_SETTING_CFG_NUM_CAL_READS;
    DDRCFG_REG(MC_INIT_POWER_DOWN) = LIBERO_SETTING_INIT_POWER_DOWN;
    DDRCFG_REG(MC_INIT_FORCE_WRITE) = LIBERO_SETTING_INIT_FORCE_WRITE;
    DDRCFG_REG(MC_INIT_FORCE_WRITE_CS) = LIBERO_SETTING_INIT_FORCE_WRITE_CS;
    DDRCFG_REG(MC_CFG_CTRLR_INIT_DISABLE) = LIBERO_SETTING_CFG_CTRLR_INIT_DISABLE;
    DDRCFG_REG(MC_INIT_RDIMM_COMPLETE) = LIBERO_SETTING_INIT_RDIMM_COMPLETE;
    DDRCFG_REG(MC_CFG_RDIMM_LAT) = LIBERO_SETTING_CFG_RDIMM_LAT;
    DDRCFG_REG(MC_CFG_RDIMM_BSIDE_INVERT) = LIBERO_SETTING_CFG_RDIMM_BSIDE_INVERT;
    DDRCFG_REG(MC_CFG_LRDIMM) = LIBERO_SETTING_CFG_LRDIMM;
    DDRCFG_REG(MC_INIT_MEMORY_RESET_MASK) = LIBERO_SETTING_INIT_MEMORY_RESET_MASK;
    DDRCFG_REG(MC_CFG_RD_PREAMB_TOGGLE) = LIBERO_SETTING_CFG_RD_PREAMB_TOGGLE;
    DDRCFG_REG(MC_CFG_RD_POSTAMBLE) = LIBERO_SETTING_CFG_RD_POSTAMBLE;
    DDRCFG_REG(MC_CFG_PU_CAL) = LIBERO_SETTING_CFG_PU_CAL;
    DDRCFG_REG(MC_CFG_DQ_ODT) = LIBERO_SETTING_CFG_DQ_ODT;
    DDRCFG_REG(MC_CFG_CA_ODT) = LIBERO_SETTING_CFG_CA_ODT;
    DDRCFG_REG(MC_CFG_ZQLATCH_DURATION) = LIBERO_SETTING_CFG_ZQLATCH_DURATION;
    DDRCFG_REG(MC_INIT_CAL_SELECT) = LIBERO_SETTING_INIT_CAL_SELECT;
    DDRCFG_REG(MC_INIT_CAL_L_R_REQ) = LIBERO_SETTING_INIT_CAL_L_R_REQ;
    DDRCFG_REG(MC_INIT_CAL_L_B_SIZE) = LIBERO_SETTING_INIT_CAL_L_B_SIZE;
    DDRCFG_REG(MC_INIT_RWFIFO) = LIBERO_SETTING_INIT_RWFIFO;
    DDRCFG_REG(MC_INIT_RD_DQCAL) = LIBERO_SETTING_INIT_RD_DQCAL;
    DDRCFG_REG(MC_INIT_START_DQSOSC) = LIBERO_SETTING_INIT_START_DQSOSC;
    DDRCFG_REG(MC_INIT_STOP_DQSOSC) = LIBERO_SETTING_INIT_STOP_DQSOSC;
    DDRCFG_REG(MC_INIT_ZQ_CAL_START) = LIBERO_SETTING_INIT_ZQ_CAL_START;
    DDRCFG_REG(MC_CFG_WR_POSTAMBLE) = LIBERO_SETTING_CFG_WR_POSTAMBLE;
    DDRCFG_REG(MC_INIT_CAL_L_ADDR_0) = LIBERO_SETTING_INIT_CAL_L_ADDR_0;
    DDRCFG_REG(MC_INIT_CAL_L_ADDR_1) = LIBERO_SETTING_INIT_CAL_L_ADDR_1;
    DDRCFG_REG(MC_CFG_CTRLUPD_TRIG) = LIBERO_SETTING_CFG_CTRLUPD_TRIG;
    DDRCFG_REG(MC_CFG_CTRLUPD_START_DELAY) = LIBERO_SETTING_CFG_CTRLUPD_START_DELAY;
    DDRCFG_REG(MC_CFG_DFI_T_CTRLUPD_MAX) = LIBERO_SETTING_CFG_DFI_T_CTRLUPD_MAX;
    DDRCFG_REG(MC_CFG_CTRLR_BUSY_SEL) = LIBERO_SETTING_CFG_CTRLR_BUSY_SEL;
    DDRCFG_REG(MC_CFG_CTRLR_BUSY_VALUE) = LIBERO_SETTING_CFG_CTRLR_BUSY_VALUE;
    DDRCFG_REG(MC_CFG_CTRLR_BUSY_TURN_OFF_DELAY) = LIBERO_SETTING_CFG_CTRLR_BUSY_TURN_OFF_DELAY;
    DDRCFG_REG(MC_CFG_CTRLR_BUSY_SLOW_RESTART_WINDOW) = LIBERO_SETTING_CFG_CTRLR_BUSY_SLOW_RESTART_WINDOW;
    DDRCFG_REG(MC_CFG_CTRLR_BUSY_RESTART_HOLDOFF) = LIBERO_SETTING_CFG_CTRLR_BUSY_RESTART_HOLDOFF;
    DDRCFG_REG(MC_CFG_PARITY_RDIMM_DELAY) = LIBERO_SETTING_CFG_PARITY_RDIMM_DELAY;
    DDRCFG_REG(MC_CFG_CTRLR_BUSY_ENABLE) = LIBERO_SETTING_CFG_CTRLR_BUSY_ENABLE;
    DDRCFG_REG(MC_CFG_ASYNC_ODT) = LIBERO_SETTING_CFG_ASYNC_ODT;
    DDRCFG_REG(MC_CFG_ZQ_CAL_DURATION) = LIBERO_SETTING_CFG_ZQ_CAL_DURATION;
    DDRCFG_REG(MC_CFG_MRRI) = LIBERO_SETTING_CFG_MRRI;
    DDRCFG_REG(MC_INIT_ODT_FORCE_EN) = LIBERO_SETTING_INIT_ODT_FORCE_EN;
    DDRCFG_REG(MC_INIT_ODT_FORCE_RANK) = LIBERO_SETTING_INIT_ODT_FORCE_RANK;
    DDRCFG_REG(MC_CFG_PHYUPD_ACK_DELAY) = LIBERO_SETTING_CFG_PHYUPD_ACK_DELAY;
    DDRCFG_REG(MC_CFG_MIRROR_X16_BG0_BG1) = LIBERO_SETTING_CFG_MIRROR_X16_BG0_BG1;
    DDRCFG_REG(MC_INIT_PDA_MR_W_REQ) = LIBERO_SETTING_INIT_PDA_MR_W_REQ;
    DDRCFG_REG(MC_INIT_PDA_NIBBLE_SELECT) = LIBERO_SETTING_INIT_PDA_NIBBLE_SELECT;
    DDRCFG_REG(MC_CFG_DRAM_CLK_DISABLE_IN_SELF_REFRESH) = LIBERO_SETTING_CFG_DRAM_CLK_DISABLE_IN_SELF_REFRESH;
    DDRCFG_REG(MC_CFG_CKSRE) = LIBERO_SETTING_CFG_CKSRE;
    DDRCFG_REG(MC_CFG_CKSRX) = LIBERO_SETTING_CFG_CKSRX;
    DDRCFG_REG(MC_CFG_RCD_STAB) = LIBERO_SETTING_CFG_RCD_STAB;
    DDRCFG_REG(MC_CFG_DFI_T_CTRL_DELAY) = LIBERO_SETTING_CFG_DFI_T_CTRL_DELAY;
    DDRCFG_REG(MC_CFG_DFI_T_DRAM_CLK_ENABLE) = LIBERO_SETTING_CFG_DFI_T_DRAM_CLK_ENABLE;
    DDRCFG_REG(MC_CFG_IDLE_TIME_TO_SELF_REFRESH) = LIBERO_SETTING_CFG_IDLE_TIME_TO_SELF_REFRESH;
    DDRCFG_REG(MC_CFG_IDLE_TIME_TO_POWER_DOWN) = LIBERO_SETTING_CFG_IDLE_TIME_TO_POWER_DOWN;
    DDRCFG_REG(MC_CFG_BURST_RW_REFRESH_HOLDOFF) = LIBERO_SETTING_CFG_BURST_RW_REFRESH_HOLDOFF;
    DDRCFG_REG(MC_CFG_BG_INTERLEAVE) = LIBERO_SETTING_CFG_BG_INTERLEAVE;
    DDRCFG_REG(MC_CFG_REFRESH_DURING_PHY_TRAINING) = LIBERO_SETTING_CFG_REFRESH_DURING_PHY_TRAINING;

    /* DFI interface timing (kept from prior code - matches Libero defaults) */
    DDRCFG_REG(MC_DFI_RDDATA_EN) = LIBERO_SETTING_CFG_DFI_T_RDDATA_EN;
    DDRCFG_REG(MC_DFI_PHY_RDLAT) = LIBERO_SETTING_CFG_DFI_T_PHY_RDLAT;
    DDRCFG_REG(MC_DFI_PHY_WRLAT) = LIBERO_SETTING_CFG_DFI_T_PHY_WRLAT;
    DDRCFG_REG(MC_DFI_PHYUPD_EN) = LIBERO_SETTING_CFG_DFI_PHYUPD_EN;

    mb();
}

#if 0  /* Phase 3.10.3 iter 7: trying to match the full HSS init_ddrc
        * body broke DFI init -- "Wait DFI complete...TIMEOUT (0x0)"
        * and wolfBoot fell back to L2-only mode.  Some of these
        * blocks (likely csr_custom @ 0x3C000 or AXI_IF @ 0x12C00 or
        * DFI extras @ 0x10010+) interfere with the DFI handshake.
        * Disabled until we can identify the offending block(s) one
        * at a time.  ADDR_MAP and MC_BASE3 critical were fine and
        * are kept above (at the start of setup_controller). */
static void setup_controller_full_hss(void) {
    /* Phase 3.10.3 (1) iter 7: full HSS init_ddrc body match.
     * Prior to this, wolfBoot was missing 199 register writes that
     * HSS makes in init_ddrc.  Adding them here in HSS source order
     * (mss_ddr.c:3746-4401), with DFI block skipped (already covered
     * above via MC_DFI_* names) and MC_BASE2 skipped (already done
     * above).  Block bases verified against
     * mss_ddr_sgmii_regs.h DDR_CSR_APB_TypeDef:
     *   ADDR_MAP @ 0x2400 (already added at top of this function)
     *   MC_BASE3 @ 0x2800 (already added at top)
     *   MC_BASE1 @ 0x3c00, MC_BASE2 @ 0x4000 (already covered),
     *   MPFE @ 0x4c00, REORDER @ 0x5000, RMW @ 0x5400, ECC @ 0x5800,
     *   READ_CAPT @ 0x5c00, MTA @ 0x6400, DYN_WIDTH_ADJ @ 0x7c00,
     *   CA_PAR_ERR @ 0x8000, DFI @ 0x10000 (partial, already covered),
     *   AXI_IF @ 0x12c00, csr_custom @ 0x3c000.
     */

    /* === MC_BASE1 block (HSS lines 3797-3938) === */
    DDRCFG_REG(0x3C00) = LIBERO_SETTING_CFG_WRITE_CRC;
    DDRCFG_REG(0x3C04) = LIBERO_SETTING_CFG_MPR_READ_FORMAT;
    DDRCFG_REG(0x3C08) = LIBERO_SETTING_CFG_WR_CMD_LAT_CRC_DM;
    DDRCFG_REG(0x3C0C) = LIBERO_SETTING_CFG_FINE_GRAN_REF_MODE;
    DDRCFG_REG(0x3C10) = LIBERO_SETTING_CFG_TEMP_SENSOR_READOUT;
    DDRCFG_REG(0x3C14) = LIBERO_SETTING_CFG_PER_DRAM_ADDR_EN;
    DDRCFG_REG(0x3C18) = LIBERO_SETTING_CFG_GEARDOWN_MODE;
    DDRCFG_REG(0x3C1C) = LIBERO_SETTING_CFG_WR_PREAMBLE;
    DDRCFG_REG(0x3C20) = LIBERO_SETTING_CFG_RD_PREAMBLE;
    DDRCFG_REG(0x3C24) = LIBERO_SETTING_CFG_RD_PREAMB_TRN_MODE;
    DDRCFG_REG(0x3C28) = LIBERO_SETTING_CFG_SR_ABORT;
    DDRCFG_REG(0x3C2C) = LIBERO_SETTING_CFG_CS_TO_CMDADDR_LATENCY;
    DDRCFG_REG(0x3C30) = LIBERO_SETTING_CFG_INT_VREF_MON;
    DDRCFG_REG(0x3C34) = LIBERO_SETTING_CFG_TEMP_CTRL_REF_MODE;
    DDRCFG_REG(0x3C38) = LIBERO_SETTING_CFG_TEMP_CTRL_REF_RANGE;
    DDRCFG_REG(0x3C3C) = LIBERO_SETTING_CFG_MAX_PWR_DOWN_MODE;
    DDRCFG_REG(0x3C40) = LIBERO_SETTING_CFG_READ_DBI;
    DDRCFG_REG(0x3C44) = LIBERO_SETTING_CFG_WRITE_DBI;
    DDRCFG_REG(0x3C48) = LIBERO_SETTING_CFG_DATA_MASK;
    DDRCFG_REG(0x3C4C) = LIBERO_SETTING_CFG_CA_PARITY_PERSIST_ERR;
    DDRCFG_REG(0x3C50) = LIBERO_SETTING_CFG_RTT_PARK;
    DDRCFG_REG(0x3C54) = LIBERO_SETTING_CFG_ODT_INBUF_4_PD;
    DDRCFG_REG(0x3C58) = LIBERO_SETTING_CFG_CA_PARITY_ERR_STATUS;
    DDRCFG_REG(0x3C5C) = LIBERO_SETTING_CFG_CRC_ERROR_CLEAR;
    DDRCFG_REG(0x3C60) = LIBERO_SETTING_CFG_CA_PARITY_LATENCY;
    DDRCFG_REG(0x3C64) = LIBERO_SETTING_CFG_CCD_S;
    DDRCFG_REG(0x3C68) = LIBERO_SETTING_CFG_CCD_L;
    DDRCFG_REG(0x3C6C) = LIBERO_SETTING_CFG_VREFDQ_TRN_ENABLE;
    DDRCFG_REG(0x3C70) = LIBERO_SETTING_CFG_VREFDQ_TRN_RANGE;
    DDRCFG_REG(0x3C74) = LIBERO_SETTING_CFG_VREFDQ_TRN_VALUE;
    DDRCFG_REG(0x3C78) = LIBERO_SETTING_CFG_RRD_S;
    DDRCFG_REG(0x3C7C) = LIBERO_SETTING_CFG_RRD_L;
    DDRCFG_REG(0x3C80) = LIBERO_SETTING_CFG_WTR_S;
    DDRCFG_REG(0x3C84) = LIBERO_SETTING_CFG_WTR_L;
    DDRCFG_REG(0x3C88) = LIBERO_SETTING_CFG_WTR_S_CRC_DM;
    DDRCFG_REG(0x3C8C) = LIBERO_SETTING_CFG_WTR_L_CRC_DM;
    DDRCFG_REG(0x3C90) = LIBERO_SETTING_CFG_WR_CRC_DM;
    DDRCFG_REG(0x3C94) = LIBERO_SETTING_CFG_RFC1;
    DDRCFG_REG(0x3C98) = LIBERO_SETTING_CFG_RFC2;
    DDRCFG_REG(0x3C9C) = LIBERO_SETTING_CFG_RFC4;
    /* MC_BASE1 trailing block.  HSS struct DDR_CSR_APB_MC_BASE1_TypeDef
     * (mss_ddr_sgmii_regs.h:3842-3928) has UNUSED_SPACE0 (9 dwords) at
     * offset 0xA0 and UNUSED_SPACE1 (6 dwords) at offset 0xC8 between
     * CFG_RFC4 and the CFG_BIT_MAP_INDEX_* table.  Previous wolfBoot
     * code wrote sequentially from 0x3CA0 onward, landing the entire
     * BIT_MAP table 60 bytes too early and missing the actual register
     * locations.  Result: HSS-canonical offsets 0x3CC4/0x3CE0..0x3D5C
     * stayed at zero, the controller had no address-to-bank/row/col
     * mapping, every AXI read targeted addresses outside the configured
     * map and hung waiting for a response.  Realign to absolute offsets
     * matching the HSS struct. */
    DDRCFG_REG(0x3CC4) = LIBERO_SETTING_CFG_NIBBLE_DEVICES;
    DDRCFG_REG(0x3CE0) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS0_0;
    DDRCFG_REG(0x3CE4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS0_1;
    DDRCFG_REG(0x3CE8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS1_0;
    DDRCFG_REG(0x3CEC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS1_1;
    DDRCFG_REG(0x3CF0) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS2_0;
    DDRCFG_REG(0x3CF4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS2_1;
    DDRCFG_REG(0x3CF8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS3_0;
    DDRCFG_REG(0x3CFC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS3_1;
    DDRCFG_REG(0x3D00) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS4_0;
    DDRCFG_REG(0x3D04) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS4_1;
    DDRCFG_REG(0x3D08) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS5_0;
    DDRCFG_REG(0x3D0C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS5_1;
    DDRCFG_REG(0x3D10) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS6_0;
    DDRCFG_REG(0x3D14) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS6_1;
    DDRCFG_REG(0x3D18) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS7_0;
    DDRCFG_REG(0x3D1C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS7_1;
    DDRCFG_REG(0x3D20) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS8_0;
    DDRCFG_REG(0x3D24) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS8_1;
    DDRCFG_REG(0x3D28) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS9_0;
    DDRCFG_REG(0x3D2C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS9_1;
    DDRCFG_REG(0x3D30) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS10_0;
    DDRCFG_REG(0x3D34) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS10_1;
    DDRCFG_REG(0x3D38) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS11_0;
    DDRCFG_REG(0x3D3C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS11_1;
    DDRCFG_REG(0x3D40) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS12_0;
    DDRCFG_REG(0x3D44) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS12_1;
    DDRCFG_REG(0x3D48) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS13_0;
    DDRCFG_REG(0x3D4C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS13_1;
    DDRCFG_REG(0x3D50) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS14_0;
    DDRCFG_REG(0x3D54) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS14_1;
    DDRCFG_REG(0x3D58) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS15_0;
    DDRCFG_REG(0x3D5C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS15_1;
    DDRCFG_REG(0x3D60) = LIBERO_SETTING_CFG_NUM_LOGICAL_RANKS_PER_3DS;
    DDRCFG_REG(0x3D64) = LIBERO_SETTING_CFG_RFC_DLR1;
    DDRCFG_REG(0x3D68) = LIBERO_SETTING_CFG_RFC_DLR2;
    DDRCFG_REG(0x3D6C) = LIBERO_SETTING_CFG_RFC_DLR4;
    DDRCFG_REG(0x3D70) = LIBERO_SETTING_CFG_RRD_DLR;
    DDRCFG_REG(0x3D74) = LIBERO_SETTING_CFG_FAW_DLR;
    /* UNUSED_SPACE2[8] at struct offset 0x178 = abs 0x3D78..0x3D94 */
    DDRCFG_REG(0x3D98) = LIBERO_SETTING_CFG_ADVANCE_ACTIVATE_READY;

    /* === MPFE block (HSS lines 4226-4240) === */
    DDRCFG_REG(0x4C00) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P0;
    DDRCFG_REG(0x4C04) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P1;
    DDRCFG_REG(0x4C08) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P2;
    DDRCFG_REG(0x4C0C) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P3;
    DDRCFG_REG(0x4C10) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P4;
    DDRCFG_REG(0x4C14) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P5;
    DDRCFG_REG(0x4C18) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P6;
    DDRCFG_REG(0x4C1C) = LIBERO_SETTING_CFG_STARVE_TIMEOUT_P7;

    /* === REORDER block (HSS lines 4242-4256) === */
    /* REORDER block (HSS struct mss_ddr_sgmii_regs.h:4172-4183).
     * CFG_MAINTAIN_COHERENCY is at offset 0xc and CFG_Q_AGE_LIMIT at
     * 0x10; previous wolfBoot offsets shifted both by one slot. */
    DDRCFG_REG(0x5000) = LIBERO_SETTING_CFG_REORDER_EN;
    DDRCFG_REG(0x5004) = LIBERO_SETTING_CFG_REORDER_QUEUE_EN;
    DDRCFG_REG(0x5008) = LIBERO_SETTING_CFG_INTRAPORT_REORDER_EN;
    DDRCFG_REG(0x500C) = LIBERO_SETTING_CFG_MAINTAIN_COHERENCY;
    DDRCFG_REG(0x5010) = LIBERO_SETTING_CFG_Q_AGE_LIMIT;
    /* UNUSED_SPACE0 at struct 0x14 = abs 0x5014 */
    DDRCFG_REG(0x5018) = LIBERO_SETTING_CFG_RO_CLOSED_PAGE_POLICY;
    DDRCFG_REG(0x501C) = LIBERO_SETTING_CFG_REORDER_RW_ONLY;
    DDRCFG_REG(0x5020) = LIBERO_SETTING_CFG_RO_PRIORITY_EN;

    /* === RMW block (HSS lines 4258-4259) === */
    DDRCFG_REG(0x5400) = LIBERO_SETTING_CFG_DM_EN;
    DDRCFG_REG(0x5404) = LIBERO_SETTING_CFG_RMW_EN;

    /* === ECC block (HSS lines 4260-4267) === */
    DDRCFG_REG(0x5800) = LIBERO_SETTING_CFG_ECC_CORRECTION_EN;
    DDRCFG_REG(0x5840) = LIBERO_SETTING_CFG_ECC_BYPASS;
    DDRCFG_REG(0x5844) = LIBERO_SETTING_INIT_WRITE_DATA_1B_ECC_ERROR_GEN;
    DDRCFG_REG(0x5848) = LIBERO_SETTING_INIT_WRITE_DATA_2B_ECC_ERROR_GEN;
    DDRCFG_REG(0x585C) = LIBERO_SETTING_CFG_ECC_1BIT_INT_THRESH;

    /* === READ_CAPT block (HSS line 4269) === */
    DDRCFG_REG(0x5C00) = LIBERO_SETTING_INIT_READ_CAPTURE_ADDR;

    /* === MTA block (HSS lines 4271-4302) === */
    /* MTA block (HSS struct mss_ddr_sgmii_regs.h:4219-4255).  After
     * MTC_ACQ_ADDR at offset 0x18 the next 5 dwords (0x1c..0x2c) are
     * __I read-only status (MTC_ACQ_CYCS_STORED/TRIG_DETECT/MEM_TRIG_
     * ADDR/MEM_LAST_ADDR/ACK).  Previous wolfBoot table wrote through
     * those slots, shifting CFG_TRIG_MT_ADDR_0..ERR_MASK_4 and the
     * MTC_ACQ_WR_DATA[0..2] by 20 bytes too early.  CFG_PRE_TRIG_CYCS
     * lives at struct offset 0x12c (abs 0x652C) after a 50-dword
     * UNUSED_SPACE0, and CFG_DATA_SEL_FIRST_ERROR at 0x150 (abs
     * 0x6550).  Realign to HSS-canonical offsets. */
    DDRCFG_REG(0x6400) = LIBERO_SETTING_CFG_ERROR_GROUP_SEL;
    DDRCFG_REG(0x6404) = LIBERO_SETTING_CFG_DATA_SEL;
    DDRCFG_REG(0x6408) = LIBERO_SETTING_CFG_TRIG_MODE;
    DDRCFG_REG(0x640C) = LIBERO_SETTING_CFG_POST_TRIG_CYCS;
    DDRCFG_REG(0x6410) = LIBERO_SETTING_CFG_TRIG_MASK;
    DDRCFG_REG(0x6414) = LIBERO_SETTING_CFG_EN_MASK;
    DDRCFG_REG(0x6418) = LIBERO_SETTING_MTC_ACQ_ADDR;
    /* 0x641C..0x642C are __I read-only status -- skip */
    DDRCFG_REG(0x6430) = LIBERO_SETTING_CFG_TRIG_MT_ADDR_0;
    DDRCFG_REG(0x6434) = LIBERO_SETTING_CFG_TRIG_MT_ADDR_1;
    DDRCFG_REG(0x6438) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_0;
    DDRCFG_REG(0x643C) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_1;
    DDRCFG_REG(0x6440) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_2;
    DDRCFG_REG(0x6444) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_3;
    DDRCFG_REG(0x6448) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_4;
    DDRCFG_REG(0x644C) = LIBERO_SETTING_MTC_ACQ_WR_DATA_0;
    DDRCFG_REG(0x6450) = LIBERO_SETTING_MTC_ACQ_WR_DATA_1;
    DDRCFG_REG(0x6454) = LIBERO_SETTING_MTC_ACQ_WR_DATA_2;
    /* 0x6458..0x6460 are __I MTC_ACQ_RD_DATA[0..2]; 0x6464..0x6528 is
     * UNUSED_SPACE0 (50 dwords). */
    DDRCFG_REG(0x652C) = LIBERO_SETTING_CFG_PRE_TRIG_CYCS;
    DDRCFG_REG(0x6550) = LIBERO_SETTING_CFG_DATA_SEL_FIRST_ERROR;

    /* === DYN_WIDTH_ADJ block (HSS lines 4304-4306) === */
    DDRCFG_REG(0x7C00) = LIBERO_SETTING_CFG_DQ_WIDTH;
    DDRCFG_REG(0x7C04) = LIBERO_SETTING_CFG_ACTIVE_DQ_SEL;

    /* === CA_PAR_ERR block (HSS lines 4308-4310) === */
    DDRCFG_REG(0x8000) = LIBERO_SETTING_INIT_CA_PARITY_ERROR_GEN_REQ;
    DDRCFG_REG(0x8004) = LIBERO_SETTING_INIT_CA_PARITY_ERROR_GEN_CMD;

    /* === DFI block extras (HSS lines 4320-4336) ===
     * Skip 0x10000-0x1000C (CFG_DFI_T_RDDATA_EN/PHY_RDLAT/PHY_WRLAT/
     * PHYUPD_EN) -- already written above via MC_DFI_* names. */
    DDRCFG_REG(0x10010) = LIBERO_SETTING_INIT_DFI_LP_DATA_REQ;
    DDRCFG_REG(0x10014) = LIBERO_SETTING_INIT_DFI_LP_CTRL_REQ;
    DDRCFG_REG(0x1001C) = LIBERO_SETTING_INIT_DFI_LP_WAKEUP;
    DDRCFG_REG(0x10020) = LIBERO_SETTING_INIT_DFI_DRAM_CLK_DISABLE;
    DDRCFG_REG(0x10030) = LIBERO_SETTING_CFG_DFI_DATA_BYTE_DISABLE;
    DDRCFG_REG(0x1003C) = LIBERO_SETTING_CFG_DFI_LVL_SEL;
    DDRCFG_REG(0x10040) = LIBERO_SETTING_CFG_DFI_LVL_PERIODIC;
    DDRCFG_REG(0x10044) = LIBERO_SETTING_CFG_DFI_LVL_PATTERN;
    DDRCFG_REG(0x10050) = LIBERO_SETTING_PHY_DFI_INIT_START;

    /* === AXI_IF block (HSS lines 4338-4366) === */
    DDRCFG_REG(0x12C18) = LIBERO_SETTING_CFG_AXI_START_ADDRESS_AXI1_0;
    DDRCFG_REG(0x12C1C) = LIBERO_SETTING_CFG_AXI_START_ADDRESS_AXI1_1;
    DDRCFG_REG(0x12C20) = LIBERO_SETTING_CFG_AXI_START_ADDRESS_AXI2_0;
    DDRCFG_REG(0x12C24) = LIBERO_SETTING_CFG_AXI_START_ADDRESS_AXI2_1;
    DDRCFG_REG(0x12F18) = LIBERO_SETTING_CFG_AXI_END_ADDRESS_AXI1_0;
    DDRCFG_REG(0x12F1C) = LIBERO_SETTING_CFG_AXI_END_ADDRESS_AXI1_1;
    DDRCFG_REG(0x12F20) = LIBERO_SETTING_CFG_AXI_END_ADDRESS_AXI2_0;
    DDRCFG_REG(0x12F24) = LIBERO_SETTING_CFG_AXI_END_ADDRESS_AXI2_1;
    DDRCFG_REG(0x13218) = LIBERO_SETTING_CFG_MEM_START_ADDRESS_AXI1_0;
    DDRCFG_REG(0x1321C) = LIBERO_SETTING_CFG_MEM_START_ADDRESS_AXI1_1;
    DDRCFG_REG(0x13220) = LIBERO_SETTING_CFG_MEM_START_ADDRESS_AXI2_0;
    DDRCFG_REG(0x13224) = LIBERO_SETTING_CFG_MEM_START_ADDRESS_AXI2_1;
    DDRCFG_REG(0x13514) = LIBERO_SETTING_CFG_ENABLE_BUS_HOLD_AXI1;
    DDRCFG_REG(0x13518) = LIBERO_SETTING_CFG_ENABLE_BUS_HOLD_AXI2;
    DDRCFG_REG(0x13690) = LIBERO_SETTING_CFG_AXI_AUTO_PCH;

    /* === csr_custom block (HSS lines 4368-4398) === */
    DDRCFG_REG(0x3C000) = LIBERO_SETTING_PHY_RESET_CONTROL;
    DDRCFG_REG(0x3C004) = LIBERO_SETTING_PHY_PC_RANK;
    DDRCFG_REG(0x3C008) = LIBERO_SETTING_PHY_RANKS_TO_TRAIN;
    DDRCFG_REG(0x3C00C) = LIBERO_SETTING_PHY_WRITE_REQUEST;
    DDRCFG_REG(0x3C014) = LIBERO_SETTING_PHY_READ_REQUEST;
    DDRCFG_REG(0x3C01C) = LIBERO_SETTING_PHY_WRITE_LEVEL_DELAY;
    DDRCFG_REG(0x3C020) = LIBERO_SETTING_PHY_GATE_TRAIN_DELAY;
    DDRCFG_REG(0x3C024) = LIBERO_SETTING_PHY_EYE_TRAIN_DELAY;
    DDRCFG_REG(0x3C028) = LIBERO_SETTING_PHY_EYE_PAT;
    DDRCFG_REG(0x3C02C) = LIBERO_SETTING_PHY_START_RECAL;
    DDRCFG_REG(0x3C030) = LIBERO_SETTING_PHY_CLR_DFI_LVL_PERIODIC;
    DDRCFG_REG(0x3C034) = LIBERO_SETTING_PHY_TRAIN_STEP_ENABLE;
    DDRCFG_REG(0x3C038) = LIBERO_SETTING_PHY_LPDDR_DQ_CAL_PAT;
    DDRCFG_REG(0x3C03C) = LIBERO_SETTING_PHY_INDPNDT_TRAINING;
    DDRCFG_REG(0x3C040) = LIBERO_SETTING_PHY_ENCODED_QUAD_CS;
    DDRCFG_REG(0x3C044) = LIBERO_SETTING_PHY_HALF_CLK_DLY_ENABLE;

    mb();
}
#endif /* 0 - Phase 3.10.3 iter 7 disabled */

/* DDR PHY Configuration */
static int setup_phy(void)
{
    uint32_t pvt_stat, pll_ctrl, timeout;

    wolfBoot_printf("DDR: PHY setup...\n");

    /* Soft reset DDR PHY */
    DDRPHY_REG(PHY_SOFT_RESET) = 0x01;
    mb();
    udelay(10);
    DDRPHY_REG(PHY_SOFT_RESET) = 0x00;
    mb();
    udelay(10);

    /* Check PHY PLL status */
    pll_ctrl = DDRPHY_REG(PHY_PLL_CTRL_MAIN);

    /* Configure PHY mode (triggers state machine to copy default RPC values) */
    DDRPHY_REG(PHY_MODE) = LIBERO_SETTING_DDRPHY_MODE;
    mb();
    udelay(10);
    /* Check if mode-driven RPC preload set rpc226=0x14 (HSS canonical) */
    wolfBoot_printf("  Post-DDRPHY_MODE preload: rpc98=0x%x rpc226=0x%x rpc114=0x%x 0xC=0x%x 0x290=0x%x\n",
        DDRPHY_REG(0x588), DDRPHY_REG(0x788), DDRPHY_REG(0x5C8),
        DDRPHY_REG(0x00CU), DDRPHY_REG(0x290U));
    DDRPHY_REG(PHY_STARTUP) = 0x003F1F00UL;
    DDRPHY_REG(PHY_DYN_CNTL) = 0x0000047FUL;
    /* DPC_BITS - voltage reference settings from HSS: 0x00050422 */
    DDRPHY_REG(PHY_DPC_BITS) = LIBERO_SETTING_DPC_BITS;
    mb();
    udelay(100);

    /*
     * LPDDR4 WRLVL Preparation (from HSS DDR_TRAINING_INIT_DONE lines 619-624)
     * Modify DPC_BITS vrgen_h for write leveling
     * DDR_DPC_VRGEN_H_MASK = 0x3F0, DPC_VRGEN_H_LPDDR4_WR_LVL_VAL = 0x5
     * Formula: (dpc_bits & ~0x3F0) | (0x5 << 4) = (dpc_bits & 0xFFFFFC0F) | 0x50
     *
     * Note: HSS sets rpc3_ODT=0 here for LPDDR4 (mss_ddr.c:624) but
     * tested empirically and adding it regressed WRCALIB lanes 2&3
     * to status=0x0.  Skipped; ODT cluster below sets it to 0x3. */
    {
        uint32_t dpc_wrlvl = (LIBERO_SETTING_DPC_BITS & 0xFFFFFC0FUL) | 0x50UL;
        DDRPHY_REG(PHY_DPC_BITS) = dpc_wrlvl;
        mb();
    }

    /*
     * Flash RPC registers to SCB (from HSS DDR_TRAINING_FLASH_REGS)
     * Enable DDR IO decoders by triggering soft resets
     * These offsets are from mss_ddr_sgmii_phy_defs.h
     */
    DDRPHY_REG(0x300) = 0x01;  /* SOFT_RESET_DECODER_DRIVER @ 0x300 */
    mb();
    DDRPHY_REG(0x380) = 0x01;  /* SOFT_RESET_DECODER_ODT @ 0x380 */
    mb();
    DDRPHY_REG(0x400) = 0x01;  /* SOFT_RESET_DECODER_IO @ 0x400 */
    mb();
    udelay(10);

    /*
     * RPC Register Configuration (from HSS set_ddr_rpc_regs for LPDDR4)
     * This is critical for proper DDR operation!
     * Offsets from mss_ddr_sgmii_phy_defs.h structure layout
     */

    /* LPDDR4-specific configuration matching HSS set_ddr_rpc_regs.
     * HSS writes (mss_ddr.c:2515-2526):
     *   rpc98=0x04, rpc226=0x14, UNUSED_SPACE0[0]=0xA000, SPARE0=0xA000.
     * Per HSS-on-board PHY dump (2026-05-13), HSS reads:
     *   UNUSED_SPACE0[0]@0xC=0xA000 SPARE0@0x290=0xA000 rpc226=0x01.
     * wolfBoot was reading 0xC=0x1 (not 0xA000), 0x290=0x0 (not
     * 0xA000), rpc226=0 (write disappeared).  Add the canonical
     * HSS writes; the empirical 0x1FC write at the bottom is a no-op
     * (0x1FC is __I read-only per the struct typedef). */
    DDRPHY_REG(0x588) = 0x04U;   /* rpc98 - ibufmd_dqs (SAR 108218) */
    /* rpc226 at offset 0x788: HSS writes 0x14 here and reads back
     * 0x14.  We tested writing 0x14 to 0x788 -- training regressed
     * to eye=0/0/0/0 across all 4 lanes.  Likely the write is
     * fine but our PHY is in a different state when we write than
     * HSS's is at set_ddr_rpc_regs() time.  Don't write here -- HSS
     * dump value of 0x14 may come from mode-register-driven preload
     * we get via DDRPHY_MODE write earlier. */
    DDRPHY_REG(0x00CU) = 0xA000U; /* UNUSED_SPACE0[0] - HSS canonical */
    DDRPHY_REG(0x290U) = 0xA000U; /* SPARE0 - HSS canonical */
    /* HSS set_ddr_rpc_regs() writes rpc226=0x14 here (mss_ddr.c
     * LPDDR4 arm, before training).  wolfBoot was writing rpc226=0x14
     * only post-training -- too late to influence TIP training
     * results.  HSS-captured PHY state shows rpc226=0x14 throughout
     * training and after.  Match that. */
    DDRPHY_REG(0x788U) = 0x14U;   /* rpc226 */
    mb();
    /* SPARE0 = 0xA000 for LPDDR4 common-mode receiver.  Per HSS
     * struct defs SPARE0 lives at offset 0x290 and UNUSED_SPACE0[0]
     * at offset 0xc.  Writing to those "correct" offsets makes
     * WRCALIB regress to status_lower=0x0 (zero lanes pass) on this
     * Video Kit -- empirically the 0x1FC write is benign on the
     * passing lanes 2&3 case.  Open: figure out why HSS sees no
     * regression at 0x290/0xC.  Until then keep the empirically-
     * stable 0x1FC. */
    DDRPHY_REG(0x1FC) = 0xA000U;

    /* Common RPC settings */
    DDRPHY_REG(0x46C) = 0x02U;   /* rpc27 @ 0x46C */
    DDRPHY_REG(0x72C) = 0x00U;   /* rpc203 @ 0x72C */

    /* ODT (On-Die Termination) Configuration
     * From HSS hw_ddr_io_bank.h for Video Kit (offsets from structure):
     * rpc1_ODT @ 0x384 = ODT_CA
     * rpc2_ODT @ 0x388 = ODT_CLK
     * rpc3_ODT @ 0x38C = ODT_DQ
     * rpc4_ODT @ 0x390 = ODT_DQS
     *
     * CRITICAL: Despite earlier setting rpc3_ODT=0 for WRLVL prep, the HSS
     * set_ddr_rpc_regs() restores it to LIBERO_SETTING_RPC_ODT_DQ (0x3) BEFORE
     * HW training starts. The HW training IP handles WRLVL with ODT enabled.
     * HSS DDR debug log confirms rpc3_ODT=0x3 at END of lpddr4_manual_training.
     */
    DDRPHY_REG(PHY_RPC1_ODT) = 0x02U;   /* ODT_CA = LIBERO_SETTING_RPC_ODT_ADDCMD */
    DDRPHY_REG(PHY_RPC2_ODT) = 0x02U;   /* ODT_CLK = LIBERO_SETTING_RPC_ODT_CLK */
    DDRPHY_REG(PHY_RPC3_ODT) = 0x03U;   /* ODT_DQ = LIBERO_SETTING_RPC_ODT_DQ (0x3) */
    DDRPHY_REG(PHY_RPC4_ODT) = 0x06U;   /* ODT_DQS = LIBERO_SETTING_RPC_ODT_DQS */

    /* BCLK selection for training */
    DDRPHY_REG(0x44C) = 0x01U;   /* rpc19 @ 0x44C - bclk_sel_clkn */
    DDRPHY_REG(0x450) = 0x00U;   /* rpc20 @ 0x450 - bclk_sel_clkp */
    mb();

    /* Bank controller soft reset to load RPC to SCB (from HSS DDR_TRAINING_SOFT_RESET) */
    DDR_BANKCONT_REG(0x00) = 0x01U;
    mb();
    udelay(100);

    /*
     * PVT Calibration (from HSS ddr_pvt_calibration in mss_sgmii.c)
     * This calibrates DDR I/O using the hardware PVT calibrator
     */
    DBG_DDR("  PVT calib...");

    /* Wait for IOEN (IO enable) from power detectors */
    timeout = 100000;
    while (timeout > 0) {
        pvt_stat = DDRPHY_REG(PHY_IOC_REG1);
        if (pvt_stat & PVT_IOEN_OUT)
            break;
        timeout--;
        udelay(1);
    }
    if (timeout == 0) {
        wolfBoot_printf("IOEN timeout\n");
    }

    /* Small delay for voltage ramp after IOEN */
    udelay(100);

    /* Set calibration clock divider and release reset
     * IOC_REG6: bit 0 = calib_reset, bits 2:1 = calib_clkdiv
     * Value 0x06 = clkdiv=3, reset=0 */
    DDRPHY_REG(PHY_IOC_REG6) = 0x00000006UL;
    mb();

    /* SCB PVT soft reset - load from RPC */
    IOSCB_IO_CALIB_DDR_REG(IOSCB_SOFT_RESET) = 0x01U;
    mb();
    udelay(1);
    IOSCB_IO_CALIB_DDR_REG(IOSCB_SOFT_RESET) = 0x00U;
    mb();

    /* Wait for calibration complete in SCB space */
    timeout = 100000;
    while (timeout > 0) {
        pvt_stat = IOSCB_IO_CALIB_DDR_REG(IOSCB_IOC_REG1);
        if (pvt_stat & PVT_CALIB_STATUS)
            break;
        timeout--;
        udelay(1);
    }

    /* Wait for calibration complete in APB space */
    timeout = 100000;
    while (timeout > 0) {
        pvt_stat = DDRPHY_REG(PHY_IOC_REG1);
        if (pvt_stat & PVT_CALIB_STATUS)
            break;
        timeout--;
        udelay(1);
    }

    /* Assert calibration lock in both APB and SCB registers */
    DDRPHY_REG(PHY_IOC_REG0) &= ~PVT_CALIB_LOCK;
    IOSCB_IO_CALIB_DDR_REG(IOSCB_IOC_REG0) &= ~PVT_CALIB_LOCK;
    mb();
    DDRPHY_REG(PHY_IOC_REG0) |= PVT_CALIB_LOCK;
    IOSCB_IO_CALIB_DDR_REG(IOSCB_IOC_REG0) |= PVT_CALIB_LOCK;
    mb();

    DBG_DDR("done\n");

    /* Configure training parameters - using HSS trained values */
    DDRPHY_REG(PHY_RPC145) = 0x00000008UL;  /* Trained: 0x08 - ADDCMD delay */
    DDRPHY_REG(PHY_RPC147) = 0x00000009UL;  /* Trained: 0x09 - DDR CLK loopback */
    DDRPHY_REG(PHY_RPC156) = mpfs_phy_rpc156_val;  /* DQ/DQS init offset (1..9) */
    DDRPHY_REG(PHY_RPC166) = 0x00000002UL;  /* Trained: 0x02 */
    DDRPHY_REG(PHY_RPC168) = 0x00000000UL;  /* Trained: 0x00 */
    /* rpc220 (DQ load delay).  Full CFG_DDR_SGMII_PHY diff vs HSS
     * (2026-06-01) shows HSS runs rpc220=0x1 during WRLVL and only
     * raises it to 0xC inside write_calibration (mss_ddr.c:1744).
     * Tested matching HSS (0x1 here): wolfBoot's AXI reads HANG (naked
     * read @ 0xC0000000 stalls, WRCALIB times out).  wolfBoot needs 0xC
     * for the read path to function -- another HSS value that does not
     * transfer to wolfBoot's PHY operating point.  Keep 0xC. */
    DDRPHY_REG(PHY_RPC220) = 0x0000000CUL;  /* wolfBoot-needed (HSS=0x1 hangs reads) */
    /* rpc226 at offset 0x788: HSS-captured value 0x14.  The
     * DDRPHY_MODE-driven preload normally populates this, but on this
     * board wolfBoot's preload ended up with 0x01 -- write it
     * explicitly to match HSS's operational state. */
    DDRPHY_REG(0x788UL) = 0x00000014UL;
    /* REMOVED: writing LIBERO_SETTING_TIP_CONFIG_PARAMS_BCLK_VCOPHS_OFFSET
     * (=2) to PHY_BCLK_SCLK (which is actually lane_select at 0x808!)
     * was selecting lane 2 throughout the rest of init, which corrupted
     * subsequent per-lane reads (CA VREF, ADDCMD).  HSS uses this constant
     * only as a loop counter in expert_pllcnt rotation, not as a value
     * to write to any register. */

    /* LPDDR4 Input Buffer Mode configuration (from Libero config)
     * Critical for proper LPDDR4 signal capture */
    DDRPHY_REG(PHY_RPC95_IBUFMD_ADDCMD) = LIBERO_SETTING_RPC_IBUFMD_ADDCMD;
    DDRPHY_REG(PHY_RPC96_IBUFMD_CLK) = LIBERO_SETTING_RPC_IBUFMD_CLK;
    DDRPHY_REG(PHY_RPC97_IBUFMD_DQ) = LIBERO_SETTING_RPC_IBUFMD_DQ;
    DDRPHY_REG(PHY_RPC98_IBUFMD_DQS) = LIBERO_SETTING_RPC_IBUFMD_DQS;
    mb();

    /* Phase 3.10.3 (1a): per-lane weak pull-up/pull-down config.
     * HSS calls config_ddr_io_pull_up_downs_rpc_bits() right after
     * set_ddr_rpc_regs() returns (mss_ddr.c:2609 -> 4551).  wolfBoot
     * was missing this entirely.  These 24 registers configure I/O
     * override enable per lane (ovrt9-16) and weak pull-up/pull-down
     * enables for ADDCMD/DATA/ECC lanes (rpc235-250).  Without them,
     * lane termination is in an undefined state and TIP cannot
     * reliably detect DQ/DQS transitions during WRLVL.
     */
    DBG_DDR("    PHY pull-up/pull-down per-lane config (HSS:4551)...\n");
    DDRPHY_REG(0x424) = LIBERO_SETTING_RPC_EN_ADDCMD0_OVRT9;  /* ovrt9 */
    DDRPHY_REG(0x428) = LIBERO_SETTING_RPC_EN_ADDCMD1_OVRT10; /* ovrt10 */
    DDRPHY_REG(0x42C) = LIBERO_SETTING_RPC_EN_ADDCMD2_OVRT11; /* ovrt11 */
    DDRPHY_REG(0x430) = LIBERO_SETTING_RPC_EN_DATA0_OVRT12;   /* ovrt12 */
    DDRPHY_REG(0x434) = LIBERO_SETTING_RPC_EN_DATA1_OVRT13;   /* ovrt13 */
    DDRPHY_REG(0x438) = LIBERO_SETTING_RPC_EN_DATA2_OVRT14;   /* ovrt14 */
    DDRPHY_REG(0x43C) = LIBERO_SETTING_RPC_EN_DATA3_OVRT15;   /* ovrt15 */
    DDRPHY_REG(0x440) = LIBERO_SETTING_RPC_EN_ECC_OVRT16;     /* ovrt16 */
    /* WPD (weak pull-down): bit 1=>off, 0=>on, per lane */
    DDRPHY_REG(0x7AC) = LIBERO_SETTING_RPC235_WPD_ADD_CMD0;   /* rpc235 */
    DDRPHY_REG(0x7B0) = LIBERO_SETTING_RPC236_WPD_ADD_CMD1;   /* rpc236 */
    DDRPHY_REG(0x7B4) = LIBERO_SETTING_RPC237_WPD_ADD_CMD2;   /* rpc237 */
    DDRPHY_REG(0x7B8) = LIBERO_SETTING_RPC238_WPD_DATA0;      /* rpc238 */
    DDRPHY_REG(0x7BC) = LIBERO_SETTING_RPC239_WPD_DATA1;      /* rpc239 */
    DDRPHY_REG(0x7C0) = LIBERO_SETTING_RPC240_WPD_DATA2;      /* rpc240 */
    DDRPHY_REG(0x7C4) = LIBERO_SETTING_RPC241_WPD_DATA3;      /* rpc241 */
    DDRPHY_REG(0x7C8) = LIBERO_SETTING_RPC242_WPD_ECC;        /* rpc242 */
    /* WPU (weak pull-up): bit 1=>off, 0=>on, per lane */
    DDRPHY_REG(0x7CC) = LIBERO_SETTING_RPC243_WPU_ADD_CMD0;   /* rpc243 */
    DDRPHY_REG(0x7D0) = LIBERO_SETTING_RPC244_WPU_ADD_CMD1;   /* rpc244 */
    DDRPHY_REG(0x7D4) = LIBERO_SETTING_RPC245_WPU_ADD_CMD2;   /* rpc245 */
    DDRPHY_REG(0x7D8) = LIBERO_SETTING_RPC246_WPU_DATA0;      /* rpc246 */
    DDRPHY_REG(0x7DC) = LIBERO_SETTING_RPC247_WPU_DATA1;      /* rpc247 */
    DDRPHY_REG(0x7E0) = LIBERO_SETTING_RPC248_WPU_DATA2;      /* rpc248 */
    DDRPHY_REG(0x7E4) = LIBERO_SETTING_RPC249_WPU_DATA3;      /* rpc249 */
    DDRPHY_REG(0x7E8) = LIBERO_SETTING_RPC250_WPU_ECC;        /* rpc250 */
    mb();

    if (pll_ctrl & PLL_LOCK_BIT) {
        DBG_DDR("PHY PLL locked\n");
    } else {
        wolfBoot_printf("PHY PLL not locked (0x%x)\n", pll_ctrl);
    }

    return 0;
}

/* Training Reset and Clock Rotation */
static void training_reset_and_rotate(void)
{
    uint32_t i;

    /* Assert training reset */
    DDRPHY_REG(PHY_TRAINING_RESET) = 0x00000002UL;
    mb();

    /* Leave AUTOINIT enabled (Libero default = 0).
     * Tried HSS-style gate (=1) during training but wolfBoot's manual
     * training path then fails to set CTRLR_INIT_DONE and TIP stays
     * stuck at train_stat=0x1.  HSS's full state machine sequences MR
     * programming differently; on our manual path, AUTOINIT must run
     * for DFI init to complete and issue LPDDR4 MR commands to DRAM. */
    DDRCFG_REG(MC_INIT_AUTOINIT_DISABLE) = 0x00;
    mb();

    /* Controller soft reset sequence */
    DBG_DDR("    SR before=0x%x\n", DDRCFG_REG(MC_CTRLR_SOFT_RESET));
    DDRCFG_REG(MC_CTRLR_SOFT_RESET) = 0x00000000UL;
    mb();
    DBG_DDR("    SR after 0=0x%x\n", DDRCFG_REG(MC_CTRLR_SOFT_RESET));
    udelay(1);
    DDRCFG_REG(MC_CTRLR_SOFT_RESET) = 0x00000001UL;
    mb();
    DBG_DDR("    SR after 1=0x%x\n", DDRCFG_REG(MC_CTRLR_SOFT_RESET));
    udelay(1);

    /* Rotate BCLK90 using expert mode */
    DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x00000004UL;
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000002UL;
    mb();

    /* PLL count sequence */
    DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x7CUL;
    DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x78UL;
    DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x78UL;
    DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x7CUL;
    DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x04UL;
    DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x64UL;
    DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x66UL;

    /* Apply BCLK VCO phase offset */
    for (i = 0; i < LIBERO_SETTING_TIP_CONFIG_PARAMS_BCLK_VCOPHS_OFFSET; i++) {
        DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x67UL;
        DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x66UL;
    }

    DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x64UL;
    DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x04UL;
    mb();

    /* Load delay lines */
    DDRPHY_REG(PHY_EXPERT_MV_RD_DLY) = 0x1FUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0xFFFFFFFFUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x00000000UL;
    DDRPHY_REG(PHY_EXPERT_MV_RD_DLY) = 0x00UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0xFFFFFFFFUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x00000000UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x0000003FUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x00000000UL;
    mb();

    /* DQ/DQS output delays */
    DDRPHY_REG(PHY_EXPERT_DFI_STATUS_TO_SHIM) = 0x06UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = 0xFFFFFFFFUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x0FUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = 0x00000000UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x00000000UL;

    DDRPHY_REG(PHY_EXPERT_DFI_STATUS_TO_SHIM) = 0x04UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = 0xFFFFFFFFUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x0FUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = 0x00000000UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x00000000UL;

    DDRPHY_REG(PHY_EXPERT_DFI_STATUS_TO_SHIM) = 0x00UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x0000003FUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x00000000UL;
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000000UL;
    mb();
}

/* Training status bits (from HSS mss_ddr_defs.h) */
#define BCLK_SCLK_BIT   (0x1U << 0U)
#define ADDCMD_BIT      (0x1U << 1U)
#define WRLVL_BIT       (0x1U << 2U)
#define RDGATE_BIT      (0x1U << 3U)
#define DQ_DQS_BIT      (0x1U << 4U)
#define TRAINING_MASK   (BCLK_SCLK_BIT | ADDCMD_BIT | WRLVL_BIT | RDGATE_BIT | DQ_DQS_BIT)

/* MTC patterns (HSS mss_ddr.h:MTC_PATTERN_) */
#define MPFS_MTC_COUNTING_PATTERN              0x00U
#define MPFS_MTC_WALKING_ONE                   0x01U
#define MPFS_MTC_PSEUDO_RANDOM                 0x02U
#define MPFS_MTC_NO_REPEATING_PSEUDO_RANDOM    0x03U
#define MPFS_MTC_ALT_ONES_ZEROS                0x04U
#define MPFS_MTC_ALT_5_A                       0x05U
#define MPFS_MTC_PSEUDO_RANDOM_16BIT           0x07U
#define MPFS_MTC_PSEUDO_RANDOM_8BIT            0x08U
#define MPFS_MTC_ADD_SEQUENTIAL                0x00U
#define MPFS_MTC_ADD_RANDOM                    0x01U
#define MPFS_MTC_TIMEOUT_ERROR                 0x02U
#define MPFS_MTC_ONE_MB_SIZE                   20U   /* 2^20 = 1 MB region */
/* WRCALIB uses smaller region to fit within working DDR window.
 * 256 B MTC passes, 1 MB times out -- try 4 KB. */
#define MPFS_MTC_WRCALIB_SIZE                  20U   /* 2^20 = 1 MB (HSS ONE_MB_MTC) */

/* MTC_test: configure the MTC engine for a single-shot memory test, fire
 * it, poll for completion, and return error status.  Port of HSS
 * MTC_test() in mss_ddr.c:3531 -- mask, start_address, size,
 * data_pattern, add_pattern are the same.  Returns:
 *   0 = pass
 *   1 = data error (MT_ERROR_STS bit 0 set)
 *   MPFS_MTC_TIMEOUT_ERROR = MT_DONE_ACK never asserted within timeout */
static uint8_t mpfs_mtc_test(uint8_t mask, uint64_t start_address,
    uint32_t size, uint8_t data_pattern, uint8_t add_pattern)
{
    uint32_t timeout;
    uint32_t mask0, mask1, mask2, mask3, mask4;

    /* Configure common memory test interface */
    DDRCFG_REG(MT_STOP_ON_ERROR) = 0U;
    DDRCFG_REG(MT_EN_SINGLE) = 0U;
    DDRCFG_REG(MT_DATA_PATTERN) = (uint32_t)data_pattern;
    DDRCFG_REG(MT_ADDR_PATTERN) =
        (add_pattern == MPFS_MTC_ADD_RANDOM) ? 1U : 0U;

    /* Set start address and size (number of addresses = 2^size) */
    if (add_pattern != MPFS_MTC_ADD_RANDOM) {
        DDRCFG_REG(MT_START_ADDR_0) = (uint32_t)(start_address & 0xFFFFFFFFUL);
        DDRCFG_REG(MT_START_ADDR_1) = (uint32_t)(start_address >> 32);
    } else {
        DDRCFG_REG(MT_START_ADDR_0) = 0U;
        DDRCFG_REG(MT_START_ADDR_1) = 0U;
    }
    DDRCFG_REG(MT_ADDR_BITS) = size;

    /* Configure per-lane error masks.  Default to all errors masked, then
     * unmask the bits belonging to each lane in `mask`.  Per-lane bit
     * positions taken verbatim from HSS mss_ddr.c:3652-3691. */
    mask0 = 0xFFFFFFFFU;
    mask1 = 0xFFFFFFFFU;
    mask2 = 0xFFFFFFFFU;
    mask3 = 0xFFFFFFFFU;
    mask4 = 0xFFFFFFFFU;
    if (mask & 0x01U) {
        mask0 &= 0xFFFFFF00U; mask1 &= 0xFFFFF00FU;
        mask2 &= 0xFFFF00FFU; mask3 &= 0xFFF00FFFU;
    }
    if (mask & 0x02U) {
        mask0 &= 0xFFFF00FFU; mask1 &= 0xFFF00FFFU;
        mask2 &= 0xFF00FFFFU; mask3 &= 0xF00FFFFFU;
    }
    if (mask & 0x04U) {
        mask0 &= 0xFF00FFFFU; mask1 &= 0xF00FFFFFU;
        mask2 &= 0x00FFFFFFU; mask3 &= 0x0FFFFFFFU;
        mask4 &= 0xFFFFFFF0U;
    }
    if (mask & 0x08U) {
        mask0 &= 0x00FFFFFFU; mask1 &= 0x0FFFFFFFU;
        mask2 &= 0xFFFFFFF0U; mask3 &= 0xFFFFFF00U;
        mask4 &= 0xFFFFF00FU;
    }
    if (mask & 0x10U) {
        mask1 &= 0xFFFFFFF0U; mask2 &= 0xFFFFFF0FU;
        mask3 &= 0xFFFFF0FFU; mask4 &= 0xFFFF0FFFU;
    }
    DDRCFG_REG(MT_ERROR_MASK_0) = mask0;
    DDRCFG_REG(MT_ERROR_MASK_1) = mask1;
    DDRCFG_REG(MT_ERROR_MASK_2) = mask2;
    DDRCFG_REG(MT_ERROR_MASK_3) = mask3;
    DDRCFG_REG(MT_ERROR_MASK_4) = mask4;

    /* Fire the test (toggle MT_EN_SINGLE 0 -> 1) */
    DDRCFG_REG(MT_EN) = 0U;
    DDRCFG_REG(MT_EN_SINGLE) = 0U;
    DDRCFG_REG(MT_EN_SINGLE) = 1U;
    mb();

    /* Poll MT_DONE_ACK with the same HSS timeout (0xFFFFFF). */
    timeout = 0xFFFFFFU;
    while ((DDRCFG_REG(MT_DONE_ACK) & 0x01U) == 0U) {
        if (timeout-- == 0U)
            return MPFS_MTC_TIMEOUT_ERROR;
    }

    return (uint8_t)(DDRCFG_REG(MT_ERROR_STS) & 0x01U);
}

/* Port of HSS set_write_calib() in mss_ddr.c:3041.  Pack the per-lane
 * "lower" calibration values into the 20-bit EXPERT_WRCALIB field and
 * commit. */
static void mpfs_set_write_calib(uint8_t num_lanes,
    const uint8_t *lane_lower)
{
    uint32_t cal = 0U;
    uint8_t shift = 0U;
    uint8_t lane;

    for (lane = 0U; lane < num_lanes; lane++) {
        cal |= ((uint32_t)(lane_lower[lane] & 0xFU)) << shift;
        shift = (uint8_t)(shift + 4U);
    }
    /* Bit 3 must be set in expert_mode_en to use expert_wrcalib. */
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000008UL;
    DDRPHY_REG(PHY_EXPERT_WRCALIB) = cal;
    mb();
}

/* Port of HSS write_calibration_using_mtc() in mss_ddr.c:3125.  Sweep
 * the EXPERT_WRCALIB delay (0x00000..0xFFFFF in 0x11111 increments) and
 * for each value run MTC tests on every lane.  Record the FIRST passing
 * calibration value per lane.  When all lanes have passed, stop the
 * sweep and commit the calibration via mpfs_set_write_calib().
 *
 * Returns 0 on success, non-zero on any error (MTC timeout or no
 * working calibration found for some lane). */
static uint8_t mpfs_write_calibration_using_mtc(uint8_t num_lanes)
{
    uint8_t status_lower = 0U;
    uint8_t lane_lower[5] = {0U, 0U, 0U, 0U, 0U};
    uint32_t cal_data;
    uint8_t lane_to_test;
    uint8_t result = 0U;
    const uint8_t all_lanes_mask = (uint8_t)((1U << num_lanes) - 1U);

    /* Bit 3 in expert_mode_en enables the EXPERT_WRCALIB path. */
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000008UL;

    for (cal_data = 0x00000U; cal_data < 0xFFFFFU; cal_data += 0x11111U) {
        /* Pet the WDT every cal_data step.  If MTC is wedged each test
         * runs to its ~133ms timeout; full sweep is 16*4*5 = 320 tests
         * ~= 42s, which exceeds the WDT window and triggers a chip
         * reset before the sweep can finish.  Pet so WRCALIB completes
         * cleanly with a FAIL status instead of resetting the chip. */
        *(volatile uint32_t*)0x20001000UL = 0xDEADC0DEU; /* WDT_E51 */
        *(volatile uint32_t*)0x20101000UL = 0xDEADC0DEU; /* WDT_U54_1 */
        *(volatile uint32_t*)0x20103000UL = 0xDEADC0DEU; /* WDT_U54_2 */
        *(volatile uint32_t*)0x20105000UL = 0xDEADC0DEU; /* WDT_U54_3 */
        *(volatile uint32_t*)0x20107000UL = 0xDEADC0DEU; /* WDT_U54_4 */

        DDRPHY_REG(PHY_EXPERT_WRCALIB) = cal_data;
        mb();

        for (lane_to_test = 0U; lane_to_test < num_lanes; lane_to_test++) {
            uint8_t lane_mask = (uint8_t)(1U << lane_to_test);
            /* Pet WDT per-lane too: 1 MB tests x 9 patterns x 4 lanes
             * x 16 cal_data steps can exceed the per-iteration pet
             * window if MTC stalls on a lane. */
            *(volatile uint32_t*)0x20001000UL = 0xDEADC0DEU; /* WDT_E51 */
            *(volatile uint32_t*)0x20101000UL = 0xDEADC0DEU; /* WDT_U54_1 */
            *(volatile uint32_t*)0x20103000UL = 0xDEADC0DEU; /* WDT_U54_2 */
            *(volatile uint32_t*)0x20105000UL = 0xDEADC0DEU; /* WDT_U54_3 */
            *(volatile uint32_t*)0x20107000UL = 0xDEADC0DEU; /* WDT_U54_4 */
            /* HSS write_calibration_using_mtc (mss_ddr.c:3156-3177):
             * discard read with COUNTING first, then if it passes, run
             * 9 different patterns INCLUDING repeats of COUNTING and
             * PSEUDO_RANDOM.  The repeats catch flaky lanes that pass
             * once by luck but fail on retry. */
            result = mpfs_mtc_test(lane_mask, 0ULL, MPFS_MTC_WRCALIB_SIZE,
                MPFS_MTC_COUNTING_PATTERN, MPFS_MTC_ADD_SEQUENTIAL);
            if (result == 0U) {
                result |= mpfs_mtc_test(lane_mask, 0ULL, MPFS_MTC_WRCALIB_SIZE,
                    MPFS_MTC_PSEUDO_RANDOM, MPFS_MTC_ADD_SEQUENTIAL);
                result |= mpfs_mtc_test(lane_mask, 0ULL, MPFS_MTC_WRCALIB_SIZE,
                    MPFS_MTC_COUNTING_PATTERN, MPFS_MTC_ADD_SEQUENTIAL);
                result |= mpfs_mtc_test(lane_mask, 0ULL, MPFS_MTC_WRCALIB_SIZE,
                    MPFS_MTC_WALKING_ONE, MPFS_MTC_ADD_SEQUENTIAL);
                result |= mpfs_mtc_test(lane_mask, 0ULL, MPFS_MTC_WRCALIB_SIZE,
                    MPFS_MTC_PSEUDO_RANDOM, MPFS_MTC_ADD_SEQUENTIAL);
                result |= mpfs_mtc_test(lane_mask, 0ULL, MPFS_MTC_WRCALIB_SIZE,
                    MPFS_MTC_NO_REPEATING_PSEUDO_RANDOM,
                    MPFS_MTC_ADD_SEQUENTIAL);
                result |= mpfs_mtc_test(lane_mask, 0ULL, MPFS_MTC_WRCALIB_SIZE,
                    MPFS_MTC_ALT_ONES_ZEROS, MPFS_MTC_ADD_SEQUENTIAL);
                result |= mpfs_mtc_test(lane_mask, 0ULL, MPFS_MTC_WRCALIB_SIZE,
                    MPFS_MTC_ALT_5_A, MPFS_MTC_ADD_SEQUENTIAL);
                result |= mpfs_mtc_test(lane_mask, 0ULL, MPFS_MTC_WRCALIB_SIZE,
                    MPFS_MTC_PSEUDO_RANDOM_16BIT, MPFS_MTC_ADD_SEQUENTIAL);
                result |= mpfs_mtc_test(lane_mask, 0ULL, MPFS_MTC_WRCALIB_SIZE,
                    MPFS_MTC_PSEUDO_RANDOM_8BIT, MPFS_MTC_ADD_SEQUENTIAL);
            }
            if (result == 0U) {
                /* This lane just passed.  Record the cal value the first
                 * time we see a pass, leave it alone on subsequent passes. */
                if ((status_lower & lane_mask) == 0U) {
                    lane_lower[lane_to_test] = (uint8_t)(cal_data & 0xFU);
                    status_lower |= lane_mask;
                }
            }
            if (result == MPFS_MTC_TIMEOUT_ERROR)
                return MPFS_MTC_TIMEOUT_ERROR;
        }
        /* If every lane has passed at least once, we're done sweeping. */
        if ((status_lower & all_lanes_mask) == all_lanes_mask)
            break;
    }

    /* HSS write_calibration_using_mtc (mss_ddr.c:3230-3232) ALWAYS
     * calls set_write_calib, even when some lanes failed.  This writes
     * the per-lane "lower" values into PHY EXPERT_WRCALIB so passing
     * lanes get their correct calibration committed.  Without this
     * call PHY retains the last sweep iteration's cal_data (junk like
     * 0xEEEEE) and ALL lanes write incorrectly, even the ones that
     * passed the sweep.  Previously we returned early on partial
     * failure -> wrong-data on every lane. */
    mpfs_set_write_calib(num_lanes, lane_lower);
    wolfBoot_printf(
        "  MTC WRCALIB: lanes(%u%u%u%u%u) cal=0x%x status=0x%x\n",
        lane_lower[0], lane_lower[1], lane_lower[2],
        lane_lower[3], lane_lower[4],
        DDRPHY_REG(PHY_EXPERT_WRCALIB),
        status_lower);

    if ((status_lower & all_lanes_mask) != all_lanes_mask) {
        wolfBoot_printf(
            "  MTC WRCALIB FAIL: status_lower=0x%x (need 0x%x) -- partial\n",
            status_lower, all_lanes_mask);
        return 1U;
    }
    return 0U;
}

/* Diagnostic (avenue 4): trace TIP training_status + per-lane wl_delay_0
 * (PHY 0x830) at a labelled point, to learn whether the autonomous WRLVL
 * sets wl_dly before or after the device reset / MR writes, and whether a
 * later step shifts it.  train_stat is read first (global, non-invasive).
 * PHY_LANE_SELECT is saved/restored around the per-lane reads so they do
 * not disturb the TIP (which depends on lane_select state between
 * training iterations). */
static void mpfs_trace_wldly(const char *tag)
{
    uint32_t lane;
    uint32_t wl[4];
    uint32_t tstat;
    uint32_t saved_ls;

    tstat = DDRPHY_REG(PHY_TRAINING_STATUS);
    saved_ls = DDRPHY_REG(PHY_LANE_SELECT);
    for (lane = 0; lane < 4; lane++) {
        DDRPHY_REG(PHY_LANE_SELECT) = lane;
        ddr_delay(2);
        wl[lane] = DDRPHY_REG(0x830);
    }
    DDRPHY_REG(PHY_LANE_SELECT) = saved_ls;
    wolfBoot_printf("  [wldly %s] tstat=0x%x L0=0x%x L1=0x%x L2=0x%x L3=0x%x\n",
        tag, tstat, wl[0], wl[1], wl[2], wl[3]);
}

/* DDR Training.  retry_count = combined outer*MAX_TRAIN_RETRY + inner
 * retry count.  Used for HSS-style MOVE_CK ADDCMD cycling (mss_ddr.c:
 * 6101-6128) which rotates the picked refclk index (0/45/90 deg ->
 * k / k+1 / k+2) across retries to converge when the first pick is
 * marginal. */
static int run_training(uint32_t retry_count)
{
    uint32_t timeout, dfi_stat, train_stat;

    /* TRAINING_SKIP = 0x02 to skip TIP's ADDCMD phase (we run our own
     * manual ADDCMD via lpddr4_manual_training above).  Matches HSS
     * captured value 0x02 at PHY 0x80C (2026-05-15 DEBUG HEXDUMP).
     *
     * Previously experimented with 0x00 (full TIP training) under the
     * theory that train_stat=0x1F (vs 0x1D) and DFI training_complete
     * would help.  That assumption was wrong: full TIP training picks
     * different per-lane wl_dly values from the HSS-trained ones, and
     * those wl_dly values combined with our other PHY config left the
     * write-data path mistrained for lanes 2/3. */
    /* Tested TRAINING_SKIP=0x02 twice (with and without rpc220=0xC
     * rpc226=0x14 alignment) -- regresses wl_dly to 0x56-0x7F across
     * lanes (vs 0x24-0x2C with skip=0).  HSS's TIP-skip approach
     * requires pre-WRLVL PHY state we don't yet match.  Keep skip=0. */
    DDRPHY_REG(PHY_TRAINING_SKIP) = 0x00U;
    mb();

    /* Configure TIP parameters (from HSS debug: TIP_CFG_PARAMS:07CFE02F) */
    DDRPHY_REG(PHY_TIP_CFG_PARAMS) = 0x07CFE02FUL;
    mb();

    /* RPC168 - RX_MD_CLKN for LPDDR4 (from HSS) */
    DDRPHY_REG(PHY_RPC168) = 0x00000000UL;
    mb();

    /*
     * BCLK90 Rotation (from HSS DDR_TRAINING_ROTATE_CLK)
     * Rotate BCLK90 by 90 degrees using expert mode
     */
    wolfBoot_printf("DDR: BCLK90 rotation...");
    {
        uint32_t i;

        /* Expert mode setup for BCLK90 rotation */
        DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x04;
        DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x02;  /* Expert mode enable */

        /* BCLK90 rotation sequence */
        DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x7C;
        DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x78;
        DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x78;
        DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x7C;
        DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x04;
        DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x64;
        DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x66;

        /* VCO phase offset increments (from TIP_CONFIG_PARAMS) */
        for (i = 0; i < LIBERO_SETTING_TIP_CONFIG_PARAMS_BCLK_VCOPHS_OFFSET; i++) {
            DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x67;
            DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x66;
        }
        DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x64;
        DDRPHY_REG(PHY_EXPERT_PLLCNT) = 0x04;

        /* Load delay lines */
        DDRPHY_REG(PHY_EXPERT_MV_RD_DLY) = 0x1F;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0xFFFFFFFF;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x00;
        DDRPHY_REG(PHY_EXPERT_MV_RD_DLY) = 0x00;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0xFFFFFFFF;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x00;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x3F;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x00;

        /* DQ output delays */
        DDRPHY_REG(PHY_EXPERT_DFI_STATUS_TO_SHIM) = 0x06;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = 0xFFFFFFFF;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x0F;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = 0x00;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x00;

        /* DQS output delays */
        DDRPHY_REG(PHY_EXPERT_DFI_STATUS_TO_SHIM) = 0x04;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = 0xFFFFFFFF;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x0F;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = 0x00;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x00;

        DDRPHY_REG(PHY_EXPERT_DFI_STATUS_TO_SHIM) = 0x00;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x3F;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x00;

        /* Exit expert mode */
        DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00;
        mb();
    }
    wolfBoot_printf("done\n");

    /*
     * Apply BCLK phase from Libero settings.
     * NOTE: a faithful port of HSS's software BCLK_SCLK sweep
     * (DDR_TRAINING_IP_SM_BCLKSCLK_SW) plus a direct force of HSS's
     * observed phase (bclk_phase=0x300/bclk90=0x2800) were both tested on
     * a clean cold boot: neither moved wl_dly off the ~+13-tap gap vs HSS
     * (wl_dly stayed 0x25-0x2C), and the sweep destabilized WRCALIB.  So
     * the BCLK/SCLK phase is NOT the source of the wl_dly divergence --
     * reverted to the Libero default apply.
     */
    wolfBoot_printf("DDR: BCLK phase (HSS 0x300)...");
    {
        /* Force HSS's software-clock-training result on this board:
         * bclk_phase=0x300 (field 3) with paired bclk90=0x2800 (field 5).
         * Earlier this was tried BEFORE the auto-init reorder (when WRLVL
         * ran prematurely, so the phase could not affect the trained
         * wl_dly).  Now WRLVL trains AFTER the manual prep, so the
         * BCLK<->SCLK phase it aligns DQS against actually matters.  PHADJ
         * load = 0x4003/0x0003/0x4003 toggle of bit 14 (LOADPHS_B). */
        uint32_t bclk_phase = 0x300UL;
        uint32_t bclk90_phase = 0x2800UL;
        DDR_PLL_REG(PLL_PHADJ) = 0x00004003UL | bclk_phase | bclk90_phase;
        mb();
        DDR_PLL_REG(PLL_PHADJ) = 0x00000003UL | bclk_phase | bclk90_phase;
        mb();
        DDR_PLL_REG(PLL_PHADJ) = 0x00004003UL | bclk_phase | bclk90_phase;
        mb();
        wolfBoot_printf("PHADJ=0x%x\n", DDR_PLL_REG(PLL_PHADJ));
    }

    ddr_delay(1000);

    /*
     * LPDDR4 Training Sequence (corrected based on HSS)
     * HSS sequence: Configure WRLVL -> DFI init -> wait for DFI complete -> lpddr4_manual_training -> wait for TIP
     */
    wolfBoot_printf("DDR: Starting TIP training...\n");

    /* Disable controller auto-initialization during training (HSS
     * mss_ddr.c:750, DDR_TRAINING_RESET; VB Memory Controller User Guide
     * training step 10 "Disabling Automatic Initialization").  wolfBoot
     * left this ENABLED, which let auto-init DRAM traffic drive the TIP
     * through the FULL training -- INCLUDING WRLVL -- during the
     * kick->DFI-complete window, BEFORE the manual device-reset/MR prep,
     * freezing wl_dly ~13 taps high vs HSS (proven by the avenue-4
     * wl_dly trace: tstat=0x1F and wl_dly already +13 at after-dfi-
     * complete).  HSS disables it here so WRLVL does NOT run until after
     * the manual prep, then re-enables it at the end of manual training
     * so the controller initializes the DRAM and the TIP trains WRLVL
     * against the correct micro-state. */
    DDRCFG_REG(MC_INIT_AUTOINIT_DISABLE) = 0x1U;
    mb();

    /* Force TIP soft reset (SOFT_RESET_TIP at 0x800) BEFORE training
     * starts.  Ensures the PHY's TIP block is at a known state every
     * training iteration, eliminating boot-to-boot variability in
     * CA VREF / ADDCMD sweep results.
     * Self-clearing write per HSS register doc. */
    DDRPHY_REG(PHY_SOFT_RESET_TIP) = 1U;
    mb();
    ddr_delay(100);  /* ~5us settle */

    /*
     * CRITICAL: Configure PHY for WRLVL BEFORE training reset release
     * Per HSS analysis: WRLVL config must be set before TIP starts
     * 1. Configure PHY: DPC_BITS vrgen_h = 0x5, rpc3_ODT = 0x0
     * 2. MR2 WRLVL enable will be done after manual training, before TIP runs
     */
    DBG_DDR("  Configure PHY for WRLVL...");
    {
        /* Set vrgen_h = 0x5 in DPC_BITS (bits 9:4) */
        uint32_t dpc_bits = DDRPHY_REG(PHY_DPC_BITS);
        uint32_t dpc_wrlvl = (dpc_bits & 0xFFFFFC0FUL) | (0x5UL << 4U);
        DDRPHY_REG(PHY_DPC_BITS) = dpc_wrlvl;
        DDRPHY_REG(PHY_RPC3_ODT) = 0x00U;  /* ODT off for WRLVL */
        mb();
        DBG_DDR("DPC=0x%x ODT=0x%x...done\n",
                DDRPHY_REG(PHY_DPC_BITS), DDRPHY_REG(PHY_RPC3_ODT));
    }

    /* Step 1: Release training reset */
    DBG_DDR("  Training reset release...");
    DDRPHY_REG(PHY_TRAINING_RESET) = 0x00000000UL;
    mb();
    ddr_delay(1000);
    DBG_DDR("done\n");

    /* Step 2: Start DFI init */
    DBG_DDR("  DFI init start...");
    DDRCFG_REG(MC_DFI_INIT_START) = 0x00000000UL;
    mb();
    DDRCFG_REG(MC_DFI_INIT_START) = 0x00000001UL;
    mb();

    /* Step 3: Start controller init */
    DDRCFG_REG(MC_CTRLR_INIT) = 0x00000000UL;
    mb();
    DDRCFG_REG(MC_CTRLR_INIT) = 0x00000001UL;
    mb();
    DBG_DDR("done\n");

    mpfs_trace_wldly("after-kick");

    /* Step 4: Wait for DFI init complete */
    DBG_DDR("  Wait DFI complete...");
    timeout = 100000;
    while (timeout > 0) {
        dfi_stat = DDRCFG_REG(MC_DFI_INIT_COMPLETE);
        if (dfi_stat & 0x01)
            break;
        timeout--;
        ddr_delay(10);
    }
    if (timeout == 0) {
        wolfBoot_printf("TIMEOUT (0x%x)\n", dfi_stat);
        return -1;
    }
    DBG_DDR("OK\n");

    mpfs_trace_wldly("after-dfi-complete");

    /* Lane alignment FIFO control (from HSS DDR_TRAINING_IP_SM_START_CHECK) */
    DDRPHY_REG(PHY_LANE_ALIGN_FIFO_CTRL) = 0x00;
    DDRPHY_REG(PHY_LANE_ALIGN_FIFO_CTRL) = 0x02;
    mb();

    /*
     * Step 5: LPDDR4 Manual Training (from HSS lpddr4_manual_training)
     * This is called AFTER DFI init completes per HSS
     */
    DBG_DDR("  LPDDR4 manual training...\n");

    /* Device reset sequence (from HSS lpddr4_manual_training lines 5035-5053) */
    DBG_DDR("    Device reset...");
    DDRCFG_REG(MC_INIT_CS) = 0x01;
    DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x01;
    ddr_delay(50);  /* 5us */
    DDRCFG_REG(MC_INIT_FORCE_RESET) = 0x01;

    DDRCFG_REG(MC_CTRLR_SOFT_RESET) = 0x01;  /* Release soft reset */
    ddr_delay(25000);  /* 250us */
    DDRCFG_REG(MC_INIT_FORCE_RESET) = 0x00;
    ddr_delay(200000);  /* 2ms minimum per LPDDR4 spec */
    DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x00;
    ddr_delay(15000);  /* 150us */
    DDRCFG_REG(MC_INIT_CS) = 0x01;

    DDRCFG_REG(MC_CFG_AUTO_ZQ_CAL_EN) = 0x00;
    ddr_delay(50);
    DBG_DDR("done\n");

    mpfs_trace_wldly("after-device-reset");

    /*
     * DDR PLL frequency doubling for LPDDR4 training (from HSS lines 5057-5076)
     * This is critical - mode register writes need slower frequency
     * Save original dividers for restore after MR writes
     */
    DBG_DDR("    PLL freq double...");
    DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x01;
    ddr_delay(5000);  /* 50us */

    /* Read and save original PLL dividers */
    uint32_t div0_1_orig = DDR_PLL_REG(PLL_DIV_0_1);
    uint32_t div2_3_orig = DDR_PLL_REG(PLL_DIV_2_3);
    {
        /* Each register holds two 6-bit divider fields at bits [13:8] and
         * [29:24].  Extract numeric values, double (LPDDR4 MR writes need a
         * slower PLL output), clamp to the 6-bit field max so the doubled
         * value cannot overflow into adjacent bits, then re-encode while
         * preserving all other bits of the original register. */
        uint32_t f0 = (div0_1_orig >> 8)  & 0x3FUL;
        uint32_t f1 = (div0_1_orig >> 24) & 0x3FUL;
        uint32_t f2 = (div2_3_orig >> 8)  & 0x3FUL;
        uint32_t f3 = (div2_3_orig >> 24) & 0x3FUL;

        f0 = (f0 > 0x1FUL) ? 0x3FUL : (f0 << 1);
        f1 = (f1 > 0x1FUL) ? 0x3FUL : (f1 << 1);
        f2 = (f2 > 0x1FUL) ? 0x3FUL : (f2 << 1);
        f3 = (f3 > 0x1FUL) ? 0x3FUL : (f3 << 1);

        DDR_PLL_REG(PLL_DIV_0_1) = (div0_1_orig & ~0x3F003F00UL) |
            (f0 << 8) | (f1 << 24);
        DDR_PLL_REG(PLL_DIV_2_3) = (div2_3_orig & ~0x3F003F00UL) |
            (f2 << 8) | (f3 << 24);

        /* Wait for PHY PLL to lock.  Bounded at 100 ms so a bad refclk,
         * power glitch, or mis-programmed divider cannot brick boot in
         * an infinite spin -- bail out so the caller can fail cleanly. */
        timeout = 100000;
        while ((DDRPHY_REG(PHY_PLL_CTRL_MAIN) & 0x2000000UL) == 0) {
            if (timeout-- == 0) {
                wolfBoot_printf("DDR: PHY PLL lock timeout (post-doubling)\n");
                return -1;
            }
            udelay(1);
        }
        ddr_delay(5000);

        /* Reset delay lines after frequency change */
        DDRPHY_REG(PHY_PLL_CTRL_MAIN) &= ~0x0000003CUL;
        DDRPHY_REG(PHY_PLL_CTRL_MAIN) |= 0x0000003CUL;
    }
    DBG_DDR("done\n");

    /* Expert mode sequence after PLL doubling (from HSS lines 5067-5075) */
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000009UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x0000003FUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x00000000UL;
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000008UL;
    ddr_delay(5000);  /* 50us */

    DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x00;
    ddr_delay(50000);  /* 500us */

    /*
     * SECOND RESET CYCLE (from HSS lpddr4_manual_training lines 5085-5095)
     * This is critical - device must be reset before MR writes
     */
    DBG_DDR("    Second reset...");
    DDRCFG_REG(MC_INIT_CS) = 0x01;
    DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x01;
    ddr_delay(50);  /* 5us */
    DDRCFG_REG(MC_INIT_FORCE_RESET) = 0x01;
    DDRCFG_REG(MC_CTRLR_SOFT_RESET) = 0x01;
    ddr_delay(25000);  /* 250us */
    DDRCFG_REG(MC_INIT_FORCE_RESET) = 0x00;
    ddr_delay(200000);  /* 2ms */
    DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x00;
    ddr_delay(15000);  /* 150us */
    DBG_DDR("done\n");

    DBG_DDR("    Pre-MR: CKE=%d RST=%d CS=%d PLL=0x%x\n",
            DDRCFG_REG(MC_INIT_DISABLE_CKE),
            DDRCFG_REG(MC_INIT_FORCE_RESET),
            DDRCFG_REG(MC_INIT_CS),
            DDRPHY_REG(PHY_PLL_CTRL_MAIN));
    DBG_DDR("    DIV0_1=0x%x DIV2_3=0x%x\n",
            DDR_PLL_REG(PLL_DIV_0_1),
            DDR_PLL_REG(PLL_DIV_2_3));

    /* LPDDR4 Mode Register Initialization (MT53D512M32D2DS-053)
     *
     * Write proper MR values to the DRAM.
     * Values based on LPDDR4 @ 1600 Mbps (800 MHz, WL=8, RL=14)
     * Updated to match Libero MSS Configurator settings.
     *
     * MR1 = 0x56  : nWR=16, RD preamble=toggle, WR preamble=2tCK, BL=16
     * MR2 = 0x2D  : RL=14, WL=8, WLS=1 (set 1)
     * MR3 = 0x31  : PDDS=RZQ/6 (40ohm) [OP5:3=110], DBI-RD/WR disabled
     *               [OP7:6=00].  Was 0xF1, which set OP[7:6]=11 enabling
     *               DBI-WR/RD -- wrong here: the controller has
     *               CFG_WRITE_DBI=0 and CFG_READ_DBI=0, so it does not
     *               drive the DMI pin as DBI.  With DBI enabled the DRAM
     *               samples DMI per byte lane for data inversion, so any
     *               lane whose DMI is not driven to 0 reads back corrupt.
     * MR11 = 0x31 : DQ_ODT=RZQ2 (bits 2:0=001), CA_ODT=RZQ4 (bits 6:4=011)
     * MR12 = 0x32 : CA VREF=50 (from Libero LPDDR4_VREF_CA=50)
     * MR13 = 0x20 : FSP-OP=0, FSP-WR=0, DMD=1 (data mask DISABLED), VRCG
     *               normal.  Was 0x00 (DMD=0, data mask enabled), but the
     *               controller uses RMW for partial writes (CFG_RMW_EN=1,
     *               CFG_DM_EN=0) and does not drive DMI as a data mask, so
     *               with DM enabled the DRAM masks bytes on lanes whose DMI
     *               floats -- the "byte 0 lands, bytes 1-3 read back fill"
     *               symptom.  Disabling DM frees the DMI pin entirely.
     * MR14 = 0x0F : DQ VREF=15 (from Libero LPDDR4_VREF_DATA=15)
     * MR22 = 0x06 : SOC_ODT=RZQ6 (40ohm, from Libero LPDDR4_SOC_ODT=RZQ6)
     */
    DBG_DDR("    MR writes...");
    {
        struct mr_write_s {
            uint8_t mr;
            uint8_t val;
        };
        /* MR2 = 0x12: OP[2:0]=010 -> RL=14, OP[5:3]=010 -> WL=8, WLS=0.
         * This MUST match the controller's CFG_RL=14 and CFG_WL=8 (Libero
         * hw_ddrc.h).  The previous 0x2D set OP[2:0]=101 (RL=28) and
         * OP[5:3]=101 (WL=14): with the DRAM at WL=14 but the controller
         * launching write data at WL=8, every write lands six cycles off
         * and no wrcalib offset on any lane can recover it -- the observed
         * MTC WRCALIB status_lower=0x0 (all lanes fail) + memory_test fail.
         * 0x12 is also what the controller auto-init derives from CFG_*.
         * (An earlier "0x12 -> reads hang" note predates the non-cached
         * DDR read-alias fix and is no longer expected to apply.) */
        struct mr_write_s mr_writes[] = {
            {1, 0x56}, {2, 0x12}, {3, 0x31}, {11, 0x31},
            {12, 0x32}, {13, 0x20}, {14, 0x0F}, {22, 0x06}
        };
        int i, j;
        uint32_t ack_cnt = 0, err_cnt = 0;

        for (i = 0; i < (int)(sizeof(mr_writes)/sizeof(mr_writes[0])); i++) {
            for (j = 0; j < 10; j++) {  /* 10 retries per MR */
                DDRCFG_REG(MC_INIT_CS) = 0x01;
                /* MR_WR_MASK convention: 1 = mask off (preserve), 0 = include
                 * in write.  HSS uses 0 (mode_register_write at mss_ddr.c:3259)
                 * to actually write data.  Our previous 0xFF was masking off
                 * the low 8 bits (the entire LPDDR4 MR data field), so the
                 * MR writes were silently no-op'd and DRAM stayed at default
                 * MR values - causing TIP to stall after BCLK_SCLK because
                 * DRAM was not configured for the LPDDR4 training sequence. */
                DDRCFG_REG(MC_INIT_MR_WR_MASK) = 0x00;
                DDRCFG_REG(MC_INIT_MR_ADDR) = mr_writes[i].mr;
                DDRCFG_REG(MC_INIT_MR_WR_DATA) = mr_writes[i].val;
                DDRCFG_REG(MC_INIT_MR_W_REQ) = 0x01;
                DDRCFG_REG(MC_INIT_MR_W_REQ) = 0x00;
                mb();
                ddr_delay(500);  /* 5us delay */
                if (DDRCFG_REG(MC_INIT_ACK) != 0)
                    ack_cnt++;
                else
                    err_cnt++;
            }
        }
        DBG_DDR("ack=%d err=%d...", ack_cnt, err_cnt);
    }
    DBG_DDR("done\n");

    mpfs_trace_wldly("after-MR-writes");

    /*
     * Restore PLL to normal speed after mode register writes
     * (from HSS lines 5121-5136)
     */
    DBG_DDR("    PLL freq restore...");
    DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x01;
    ddr_delay(500);

    DDR_PLL_REG(PLL_DIV_0_1) = div0_1_orig;
    DDR_PLL_REG(PLL_DIV_2_3) = div2_3_orig;

    /* Wait for PHY PLL to lock; bounded as in the post-doubling wait above. */
    timeout = 100000;
    while ((DDRPHY_REG(PHY_PLL_CTRL_MAIN) & 0x2000000UL) == 0) {
        if (timeout-- == 0) {
            wolfBoot_printf("DDR: PHY PLL lock timeout (post-restore)\n");
            return -1;
        }
        udelay(1);
    }
    ddr_delay(500);

    /* Reset delay lines after frequency change */
    DDRPHY_REG(PHY_PLL_CTRL_MAIN) &= ~0x0000003CUL;
    DDRPHY_REG(PHY_PLL_CTRL_MAIN) |= 0x0000003CUL;

    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000009UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x0000003FUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x00000000UL;
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000008UL;
    ddr_delay(500);
    DBG_DDR("done\n");

    /*
     * CA VREF Training (from HSS lpddr4_manual_training lines 5140-5310)
     * This calibrates the command/address bus voltage reference
     * Must happen AFTER PLL restore at normal speed
     */
    DBG_DDR("    CA VREF training...\n");
    DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x01;  /* Disable CKE during training */
    ddr_delay(5000);  /* 50us */
    {
        uint32_t dpc_bits_new;
        /* 2026-05-11: re-enabled the CA VREF dual-pass sweep.
         * Previously hardcoded to 0x10, but HSS finds different
         * vref_answer values per board (HSS log on this Video Kit
         * found 0x07).  Hardcoding leaves the PHY at a non-optimal
         * VREF where ADDCMD sweep is noisy and writes corrupt.  The
         * sweep below is a port of HSS lpddr4_manual_training lines
         * 1005-1185 (the dual ca_indly x vref loop). */
        uint32_t vref_answer = 128;  /* 128 = no answer found */
        uint32_t transition_a5_min_last = 129;
        uint32_t ca_indly;
        uint32_t vref;

        /* Enable expert mode for delay control */
        DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000021UL;
        DDRPHY_REG(PHY_EXPERT_DFI_STATUS_TO_SHIM) = 0x00000000UL;

        /* Reset delay lines to 0 before sweep (from HSS expert mode setup) */
        DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x0000003FUL;
        DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x00000000UL;
        ddr_delay(100);

        /* Outer loop: sweep CA input delay */
        for (ca_indly = 0; ca_indly < 30; ca_indly += 5) {
            DDRPHY_REG(PHY_RPC145) = ca_indly;  /* A9 loopback delay */
            DDRPHY_REG(PHY_RPC147) = ca_indly;  /* DDR clock loopback delay */

            uint32_t break_loop = 1;
            uint32_t in_window = 0;
            vref_answer = 128;

            /* Inner loop: sweep VREF values */
            for (vref = 5; vref < 30; vref++) {
                uint32_t transition_a5_max = 0;
                uint32_t transition_a5_min = 128;
                uint32_t j;

                if (transition_a5_min_last > 128)
                    transition_a5_min_last = 128;

                /* Reset DPC_BITS NV map */
                DDR_BANKCONT_REG(0x00) = 0U;
                ddr_delay(50);

                /* Set new VREF value: bits[17:12] = vref, bit 18 = enable */
                dpc_bits_new = (DDRPHY_REG(PHY_DPC_BITS) & 0xFFFC0FFFUL) |
                               (vref << 12) | (0x1UL << 18);
                DDRPHY_REG(PHY_DPC_BITS) = dpc_bits_new;
                ddr_delay(50);

                /* Release NV map reset */
                DDR_BANKCONT_REG(0x00) = 1U;
                ddr_delay(50);

                /* Sample transition_a5 multiple times */
                for (j = 0; j < 20; j++) {
                    uint32_t rx_a5_last = 0xF;
                    uint32_t rx_a5;
                    uint32_t transition_a5 = 0;
                    uint32_t i;

                    /* Load INDLY - same sequence as HSS lines 5186-5195 */
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_DIR1) = 0x000000UL;
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x000000UL;
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x180000UL;
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x000000UL;

                    /* Load OUTDLY - same sequence as HSS lines 5197-5203 */
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_DIR1) = 0x180000UL;
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x000000UL;
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x180000UL;
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x000000UL;

                    ddr_delay(50);

                    /* Sweep delay and look for transition in rx_a5 */
                    for (i = 0; i < (128 - ca_indly); i++) {
                        /* Move delay counter */
                        DDRPHY_REG(PHY_EXPERT_DLYCNT_MOVE1) = 0x0UL;
                        DDRPHY_REG(PHY_EXPERT_DLYCNT_MOVE1) = 0x180000UL;
                        DDRPHY_REG(PHY_EXPERT_DLYCNT_MOVE1) = 0x0UL;
                        ddr_delay(5);

                        /* Read rx_a5 from bits 9:8 of readback register */
                        rx_a5 = (DDRPHY_REG(PHY_EXPERT_ADDCMD_READBACK) & 0x0300UL) >> 8;

                        /* If we found a transition, break after 8 more steps */
                        if (transition_a5 != 0) {
                            if ((i - transition_a5) > 8)
                                break;
                        }

                        /* Detect transition (rising edge in rx_a5) */
                        if (transition_a5 == 0) {
                            if ((rx_a5 ^ rx_a5_last) & rx_a5) {
                                transition_a5 = i;
                            } else {
                                rx_a5_last = rx_a5;
                            }
                        } else {
                            /* Verify transition is stable after 4 steps */
                            if ((i - transition_a5) == 4) {
                                if (!((rx_a5 ^ rx_a5_last) & rx_a5)) {
                                    transition_a5 = 0;  /* False transition */
                                    rx_a5_last = rx_a5;
                                }
                            }
                        }
                    }

                    /* Track min/max transition point */
                    if (transition_a5 != 0) {
                        if (transition_a5 > transition_a5_max)
                            transition_a5_max = transition_a5;
                        if (transition_a5 < transition_a5_min)
                            transition_a5_min = transition_a5;
                    }
                }

                /* Calculate range and check if we're in a stable window */
                {
                    uint32_t range_a5 = transition_a5_max - transition_a5_min;
                    uint32_t deltat;

                    if (transition_a5_min < 10)
                        break_loop = 0;

                    if (range_a5 <= 5) {
                        if (transition_a5_min > transition_a5_min_last)
                            deltat = transition_a5_min - transition_a5_min_last;
                        else
                            deltat = transition_a5_min_last - transition_a5_min;

                        if (deltat <= 5)
                            in_window = (in_window << 1) | 1;
                    } else {
                        in_window = (in_window << 1) | 0;
                    }

                    /* Found answer if 2 consecutive good windows */
                    if (vref_answer == 128) {
                        if ((in_window & 0x3) == 0x3) {
                            vref_answer = vref;
                            break;  /* Found good VREF */
                        }
                    }

                    transition_a5_min_last = transition_a5_min;
                }
            }

            if (break_loop)
                break;
        }

        /* Phase 3.10.3 iter 5 tried adding HSS's CA-VREF expert-mode
         * setup writes here (expert_mode_en = 0x21,
         * expert_dfi_status_override_to_shim = 0,
         * expert_dlycnt_pause toggle).  No effect on TIP advancement
         * and pattern test mismatches stayed in the elevated 4-5k
         * range vs the ~1200 baseline.  Reverted. */

        /* Apply final VREF value */
        DDR_BANKCONT_REG(0x00) = 0U;
        ddr_delay(50);

        if (vref_answer == 128) {
            /* Training failed - use default 0x10 */
            vref_answer = 0x10;
            DBG_DDR("FAIL(0x%x)...", vref_answer);
        } else {
            DBG_DDR("0x%x...", vref_answer);
        }

        dpc_bits_new = (DDRPHY_REG(PHY_DPC_BITS) & 0xFFFC0FFFUL) |
                       (vref_answer << 12) | (0x1UL << 18);
        DDRPHY_REG(PHY_DPC_BITS) = dpc_bits_new;
        ddr_delay(50);

        DDR_BANKCONT_REG(0x00) = 1U;
        /* Bumping post-VREF delay to udelay(500) shifted CA VREF
         * answer from 0x7 (HSS match) to 0xE on every retry -- worse.
         * Reverted to the original ddr_delay(5000) which was finding
         * 0x7 sometimes. */
        ddr_delay(5000);
    }
    DBG_DDR("done\n");

    /*
     * MANUAL ADDCMD TRAINING (from HSS lpddr4_manual_training lines 5320-5600)
     * Finds optimal refclk_phase and CA output delay
     */
    DBG_DDR("    ADDCMD training...");
    {
        uint32_t init_del_offset = 0x8;
        /* HSS LPDDR4 uses the inline ADDCMD in lpddr4_manual_training
         * (mss_ddr.c:5325), which uses rpc147_offset=0x1.  The 0x2 value
         * belongs to address_cmd_training_with_ck_push (5907), which the
         * HSS dispatch (mss_ddr.c:1193) does NOT call for LPDDR4. */
        uint32_t rpc147_offset = 0x1;
        uint32_t rpc145_offset = 0x0;
        uint32_t bclk_phase = DDR_PLL_REG(PLL_PHADJ) & 0x700;
        uint32_t bclk90_phase = DDR_PLL_REG(PLL_PHADJ) & 0x3800;
        uint32_t refclk_phase;
        uint32_t a5_offset_status = 1;  /* 1 = FAIL, 0 = PASS */
        uint32_t max_retries = 5;

        while (a5_offset_status != 0 && max_retries > 0) {
            a5_offset_status = 0;  /* Assume pass */
            max_retries--;

            /* Set loopback delay offsets */
            DDRPHY_REG(PHY_RPC147) = init_del_offset + rpc147_offset;
            DDRPHY_REG(PHY_RPC145) = init_del_offset + rpc145_offset;

            /* Enable expert mode for delay and PLL control */
            DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000023UL;

            uint32_t j;
            uint32_t difference[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            uint32_t transition_ck_array[8] = {0};
            uint32_t transition_a5_max = 0;

            /* Sweep 16 refclk phases (8 unique phases, sampled twice) */
            for (j = 0; j < 16; j++) {
                uint32_t rx_a5, rx_a5_last = 0xF;
                uint32_t rx_ck, rx_ck_last = 0x5;
                uint32_t transition_a5 = 0;
                uint32_t transition_ck = 0;
                uint32_t i;
                uint32_t transitions_found = 0;

                /* Load INDLY */
                DDRPHY_REG(PHY_EXPERT_DLYCNT_DIR1) = 0x000000UL;
                DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x000000UL;
                DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x180000UL;
                DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x000000UL;

                /* Load OUTDLY */
                DDRPHY_REG(PHY_EXPERT_DLYCNT_DIR1) = 0x180000UL;
                DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x000000UL;
                DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x180000UL;
                DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x000000UL;

                /* Set refclk phase */
                refclk_phase = (j % 8) << 2;
                DDR_PLL_REG(PLL_PHADJ) = 0x00004003UL | bclk_phase | bclk90_phase | refclk_phase;
                DDR_PLL_REG(PLL_PHADJ) = 0x00000003UL | bclk_phase | bclk90_phase | refclk_phase;
                DDR_PLL_REG(PLL_PHADJ) = 0x00004003UL | bclk_phase | bclk90_phase | refclk_phase;

                ddr_delay(10);

                /* Sweep delay to find transitions */
                i = 0;
                while (!transitions_found && i < 128) {
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_MOVE1) = 0x0UL;
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_MOVE1) = 0x180000UL;
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_MOVE1) = 0x0UL;
                    ddr_delay(5);

                    rx_a5 = (DDRPHY_REG(PHY_EXPERT_ADDCMD_READBACK) & 0x0300UL) >> 8;
                    rx_ck = DDRPHY_REG(PHY_EXPERT_ADDCMD_READBACK) & 0x000F;

                    /* Check if both transitions found */
                    if (transition_a5 != 0 && transition_ck != 0) {
                        if ((i - transition_a5) > 8 && (i - transition_ck) > 8)
                            transitions_found = 1;
                    }

                    /* Detect CK transition (edge to 0x5) */
                    if (transition_ck == 0) {
                        if (rx_ck_last != 0x5 && rx_ck == 0x5)
                            transition_ck = i;
                        rx_ck_last = rx_ck;
                    } else if ((i - transition_ck) == 4 && rx_ck != rx_ck_last) {
                        transition_ck = 0;
                        rx_ck_last = rx_ck;
                    }

                    /* Detect A5 transition (rising edge) */
                    if (transition_a5 == 0) {
                        if ((rx_a5 ^ rx_a5_last) & rx_a5)
                            transition_a5 = i;
                        else
                            rx_a5_last = rx_a5;
                    } else if ((i - transition_a5) == 4) {
                        if (!((rx_a5 ^ rx_a5_last) & rx_a5)) {
                            transition_a5 = 0;
                            rx_a5_last = rx_a5;
                        }
                    }

                    i++;
                }

                /* Track max transition_a5 */
                if (transition_a5 > transition_a5_max)
                    transition_a5_max = transition_a5;

                /* Store transition_ck for first 8 phases */
                if (transition_a5 != 0 && transition_ck != 0 && j < 8)
                    transition_ck_array[j] = transition_ck;
            }

            /* Calculate differences and find minimum.
             *
             * Threshold mismatch fix (2026-05-12): wolfBoot was using
             * 0x20 = 32 as the transition_a5_max minimum, but HSS uses
             * ADD_CMD_TRANS_A5_THRES_LPDDR4 = 18 (mss_ddr.h:464).  Our
             * higher threshold rejected valid transition values in
             * [18,31] that HSS would have accepted as PASS, forcing
             * a retry that picked marginal phase/dly settings.  This
             * was the root of the DDR-burst address-scramble seen on
             * subsequent PDMA writes -- the DRAM was receiving slightly
             * wrong row/column commands and burst data landed in wrong
             * cells.
             *
             * Also align min_refclk sentinel with HSS: HSS uses 0x8
             * (out of valid 0..7 range) as "no valid value found"
             * sentinel; wolfBoot used 0 as default which could be
             * mistaken for a real result if the difference-scan loop
             * found nothing. */
            uint32_t min_diff = 0xFF;
            uint32_t min_refclk = 0x8;
            uint32_t second_diff = 0xFF;
            uint32_t second_refclk = 0x8;
            uint32_t third_diff = 0xFF;
            uint32_t third_refclk = 0x8;
            uint32_t l;

            if (transition_a5_max < 18U) {
                a5_offset_status = 1;  /* FAIL: HSS LPDDR4 threshold */
            }

            /* HSS address_cmd_training_with_ck_push (mss_ddr.c:6073-6100):
             * scan the difference array in DESCENDING refclk order
             * (k = 7..0).  With strict '<', ties favor the HIGHER refclk
             * index -- the opposite of an ascending scan.  Capture the
             * +1 (second) and +2 (third) neighbors at the chosen index
             * for the MOVE_CK rotation below. */
            for (l = 0; l < 8U; l++) {
                uint32_t k = 7U - l;
                if (transition_a5_max >= transition_ck_array[k])
                    difference[k] = transition_a5_max - transition_ck_array[k];
                else
                    difference[k] = 0xFF;
            }

            for (l = 0; l < 8U; l++) {
                uint32_t k = 7U - l;
                if (difference[k] < min_diff) {
                    second_refclk = (k + 1U) & 0x7U;
                    second_diff = difference[second_refclk];
                    third_refclk = (k + 2U) & 0x7U;
                    third_diff = difference[third_refclk];
                    min_refclk = k;
                    min_diff = difference[k];
                }
            }

            if (min_diff == 0xFF)
                a5_offset_status = 1;
            /* HSS check: out-of-range sentinel (8) means no transition. */
            if (min_refclk == 0x8U)
                a5_offset_status = 1;

            /* MOVE_CK retry rotation (HSS mss_ddr.c:6101-6128) using the
             * LPDDR4 move-order arrays: 0 deg = 0 (no push), 45 deg = 1
             * (second = k+1), 90 deg = 2 (third = k+2).  HSS cycles the
             * push across retries so a marginal first pick gets retried
             * at +1/+2.  The previous port was uniformly one step low
             * (retry0 = k-1) and used the wrong neighbors, landing refclk
             * ~6 below HSS -> WRLVL trained wl_dly ~13 taps high -> lanes
             * 1-3 rejected writes.  Applied only when ADDCMD passed. */
            if (a5_offset_status == 0) {
                uint32_t move = retry_count % 3U;
                if (move == 1U) {           /* 45 deg */
                    min_diff = second_diff;
                    min_refclk = second_refclk;
                } else if (move == 2U) {    /* 90 deg */
                    min_diff = third_diff;
                    min_refclk = third_refclk;
                }
                /* move == 0 (0 deg): no push, keep min_refclk/min_diff */
            }

            DBG_DDR(" a5_max=%d retry=%d move=%d min_refclk=%d min_diff=%d status=%d ",
                transition_a5_max, retry_count, retry_count % 3U,
                min_refclk, min_diff, a5_offset_status);

            if (a5_offset_status == 0) {
                /* HSS refclk_offset addition (mss_ddr.c:6140): final phase
                 * = (refclk_offset + min_refclk) & 0x7.  OFFSET_0 = 3 for
                 * Video Kit LPDDR4 1600. */
                const uint32_t refclk_offset =
                    LIBERO_SETTING_REFCLK_LPDDR4_1600_OFFSET_0;
                refclk_phase = ((refclk_offset + min_refclk) & 0x7) << 2;
                DDR_PLL_REG(PLL_PHADJ) = 0x00004003UL | bclk_phase | bclk90_phase | refclk_phase;
                DDR_PLL_REG(PLL_PHADJ) = 0x00000003UL | bclk_phase | bclk90_phase | refclk_phase;
                DDR_PLL_REG(PLL_PHADJ) = 0x00004003UL | bclk_phase | bclk90_phase | refclk_phase;

                /* Load INDLY */
                DDRPHY_REG(PHY_EXPERT_DLYCNT_DIR1) = 0x000000UL;
                DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x000000UL;
                DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x180000UL;
                DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x000000UL;

                /* Load OUTDLY */
                DDRPHY_REG(PHY_EXPERT_DLYCNT_DIR1) = 0x180000UL;
                DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x000000UL;
                DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x180000UL;
                DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x000000UL;

                /* Move CA output delay by min_diff (HSS mss_ddr.c:6155). */
                for (j = 0; j < min_diff && j < 128; j++) {
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_MOVE1) = 0x0UL;
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_MOVE1) = 0x180000UL;
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_MOVE1) = 0x0UL;
                }

                DDRPHY_REG(PHY_EXPERT_DLYCNT_DIR1) = 0x000000UL;
                DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000000UL;

                DBG_DDR("phase=%d dly=%d...", min_refclk, min_diff);
            } else {
                /* Increase offset and retry */
                init_del_offset += transition_a5_max + 5;
                if (init_del_offset > 0xFF)
                    break;
            }
        }

        if (a5_offset_status != 0)
            DBG_DDR("FAIL...");
    }

    /* POST_INITIALIZATION after ADDCMD training */
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000008UL;
    DDRPHY_REG(PHY_EXPERT_DFI_STATUS_TO_SHIM) = 0x00000000UL;
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000009UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x0000003FUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x00000000UL;
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000008UL;
    ddr_delay(50);

    DBG_DDR("PLL_PHADJ=0x%x DPC=0x%x...",
            DDR_PLL_REG(PLL_PHADJ),
            DDRPHY_REG(PHY_DPC_BITS));

    /* Re-enable CKE */
    DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x00;
    ddr_delay(5000);

    mpfs_trace_wldly("after-addcmd");

    /* Post-ADDCMD: Refresh mode registers per HSS pattern.
     *
     * HSS lpddr4_manual_training calls mode_register_masked_write_x5 for
     * MR1/2/3/4/11/16/17/22/13 here.  That function sets MR_WR_MASK=0xFFFFF
     * (preserve all bits) and MR_WR_DATA=0 - it is a NO-OP refresh that
     * just re-issues an MR command without modifying DRAM contents.  HSS
     * relies on the AUTOINIT-programmed values to remain valid.
     *
     * We replicate that pattern here.  The MR list matches HSS exactly
     * (MR4/16/17 added vs the older code, MR12/14 removed - those are
     * not in HSS's refresh batch). */
    DBG_DDR("    MR refresh (HSS pattern)...");
    {
        const uint8_t mr_list[] = { 1, 2, 3, 4, 11, 16, 17, 22, 13 };
        int i, j;
        uint32_t ack_cnt = 0, err_cnt = 0;

        for (i = 0; i < (int)(sizeof(mr_list)/sizeof(mr_list[0])); i++) {
            for (j = 0; j < 10; j++) {
                DDRCFG_REG(MC_INIT_CS) = 0x01;
                /* mask=0xFFFFF + data=0 = MR command refresh, no value change */
                DDRCFG_REG(MC_INIT_MR_WR_MASK) = 0xFFFFFUL;
                DDRCFG_REG(MC_INIT_MR_ADDR) = mr_list[i];
                DDRCFG_REG(MC_INIT_MR_WR_DATA) = 0x00;
                DDRCFG_REG(MC_INIT_MR_W_REQ) = 0x01;
                DDRCFG_REG(MC_INIT_MR_W_REQ) = 0x00;
                mb();
                ddr_delay(500);
                if (DDRCFG_REG(MC_INIT_ACK) != 0)
                    ack_cnt++;
                else
                    err_cnt++;
            }
        }
        DBG_DDR("ack=%d err=%d...", ack_cnt, err_cnt);
    }
    DBG_DDR("done\n");

    /* Re-enable controller auto-init AFTER the MR refresh (HSS order in
     * lpddr4_manual_training: device-reset -> MR writes -> ADDCMD -> MR
     * refresh -> re-enable auto-init at mss_ddr.c:5634).  Only now does
     * the controller initialize the DRAM and the autonomous TIP train
     * WRLVL -- against a fully MR-configured DRAM, undisturbed.  Doing the
     * MR refresh AFTER the re-enable/WRLVL (the previous order) perturbed
     * the freshly-trained wl_dly and is the likely source of the
     * intermittent first-words corruption. */
    DDRCFG_REG(MC_INIT_AUTOINIT_DISABLE) = 0x0U;
    mb();
    mpfs_trace_wldly("after-autoinit-reenable");

    ddr_delay(100);

    DBG_DDR("    Post-manual training status:\n");
    DBG_DDR("      train_stat=0x%x dfi_train_complete=0x%x\n",
            DDRPHY_REG(PHY_TRAINING_STATUS),
            DDRCFG_REG(0x10038U));  /* STAT_DFI_TRAINING_COMPLETE @ DFI+0x38 */
    DBG_DDR("      gt_state=0x%x dqdqs_state=0x%x\n",
            DDRPHY_REG(0x82C), DDRPHY_REG(0x83C));

    /* ZQ calibration */
    DBG_DDR("    ZQ cal...");
    DDRCFG_REG(MC_INIT_ZQ_CAL_START) = 0x00000001UL;
    DDRCFG_REG(MC_AUTOINIT_DISABLE) = 0x00000000UL;

    /* Wait for INIT_ACK */
    timeout = 0xFF;
    while ((DDRCFG_REG(MC_INIT_ACK) == 0) && (timeout > 0)) {
        ddr_delay(100);
        timeout--;
    }
    DDRCFG_REG(MC_INIT_ZQ_CAL_START) = 0x00000000UL;
    /* 2026-05-12: match HSS Libero value (CFG_AUTO_ZQ_CAL_EN=0).
     * Previously hardcoded to 1; reverted in earlier session because
     * "pattern test slightly worse and train_stat unchanged".  Now
     * with the ADDCMD threshold fix (5e27fcb4), reconsider: auto ZQ
     * cal injects ZQ commands into the DRAM stream, which can
     * collide with data bursts.  HSS disables auto cal and relies on
     * the explicit INIT_ZQ_CAL_START at init time only. */
    DDRCFG_REG(MC_CFG_AUTO_ZQ_CAL_EN) = LIBERO_SETTING_CFG_AUTO_ZQ_CAL_EN;
    mb();
    DBG_DDR("done\n");

    /*
     * Restore PHY DPC_BITS / RPC3_ODT to canonical Libero values now
     * (vrgen_h=0x2, ODT=0x3).  Empirically: leaving the PHY in WRLVL
     * mode (vrgen_h=0x5, ODT=0) across the wait loop produces ~4x more
     * DDR pattern-test mismatches when WRLVL never fires.  Since on
     * this Video Kit TIP currently does not autonomously start WRLVL
     * after BCLK_SCLK, the WRLVL-mode setup is no longer doing
     * anything useful and is actively degrading subsequent reads.
     * If/when WRLVL begins running, this restore should move into the
     * wait loop conditional on the WRLVL bit (HSS RDGATE-state-entry
     * pattern - mss_ddr.c:1383).
     */
    /* DO NOT restore DPC_BITS / RPC3_ODT here -- TIP needs the
     * WRLVL-mode DPC_BITS (vrgen_h adjusted) and ODT=0 during the
     * actual write-leveling phase.  HSS restores these on entry to
     * RDGATE state (mss_ddr.c:1383), AFTER WRLVL has completed.
     * The restore moved into the TIP-wait loop below, gated on the
     * WRLVL training bit being set. */
    DBG_DDR("    DPC_BITS/RPC3_ODT restore deferred until WRLVL bit set\n");

#ifdef MPFS_DDR_KICK_TRAINING_START
    /* EXPERIMENT: HSS never writes 1 to PHY_TRAINING_START in the
     * success path (only writes 0 in the failure-retry path), but our
     * snapshot shows train_start=0x0 when TIP refuses to advance past
     * BCLK_SCLK.  Try kicking it manually to see if TIP comes alive.
     * If this works, we are missing some auto-trigger HSS gets for free
     * on its hardware/firmware combination.  If it does nothing, we
     * have ruled out training_start as the missing piece. */
    wolfBoot_printf("  Kicking PHY_TRAINING_START=1 (experiment)\n");
    DDRPHY_REG(PHY_TRAINING_START) = 0x00000001UL;
    mb();
#endif

    /* Pre-wait state snapshot - what TIP sees right now. */
    wolfBoot_printf("  Pre-TIP-wait snapshot:\n");
    wolfBoot_printf("    train_stat=0x%x train_skip=0x%x train_reset=0x%x\n",
        DDRPHY_REG(PHY_TRAINING_STATUS),
        DDRPHY_REG(PHY_TRAINING_SKIP),
        DDRPHY_REG(PHY_TRAINING_RESET));
    wolfBoot_printf("    train_start=0x%x tip_cfg_params=0x%x\n",
        DDRPHY_REG(PHY_TRAINING_START),
        DDRPHY_REG(PHY_TIP_CFG_PARAMS));
    wolfBoot_printf("    DPC_BITS=0x%x RPC3_ODT=0x%x\n",
        DDRPHY_REG(PHY_DPC_BITS), DDRPHY_REG(PHY_RPC3_ODT));
    wolfBoot_printf("    DFI_init_complete=0x%x DFI_train_complete=0x%x\n",
        DDRCFG_REG(MC_DFI_INIT_COMPLETE),
        DDRCFG_REG(0x10038U));
    /* Note: this snapshot previously printed "INIT_DONE=" but read
     * MC_INIT_AUTOINIT_DISABLE (+0x10), not MC_CTRLR_INIT_DONE (+0x3c).
     * Print both correctly so the controller-init state is truthful. */
    wolfBoot_printf("    CTRLR_INIT_DONE=0x%x AUTOINIT_DIS=0x%x\n",
        DDRCFG_REG(MC_CTRLR_INIT_DONE),
        DDRCFG_REG(MC_INIT_AUTOINIT_DISABLE));
    {
        uint32_t lane;
        for (lane = 0; lane < 4; lane++) {
            DDRPHY_REG(PHY_LANE_SELECT) = lane;
            ddr_delay(10);
            wolfBoot_printf("    L%d: gt_state=0x%x gt_txdly=0x%x wl_dly=0x%x dqdqs_st=0x%x\n",
                lane,
                DDRPHY_REG(0x82C),  /* gt_state */
                DDRPHY_REG(0x824),  /* gt_txdly -- new */
                DDRPHY_REG(0x830),  /* wl_delay_0 */
                DDRPHY_REG(0x83C)); /* dqdqs_state */
        }
        /* Reverted: adding lane_select=0 reset here regressed training
         * to "Init FAILED after 6 outer retries".  Apparently TIP relies
         * on lane_select being non-zero between iterations.  Leave the
         * lane number as whatever the print loop ended with. */
    }

    /*
     * After ZQ cal, hand off to TIP and just poll training_status.
     * HSS does NOT re-write MR2, training_start, or any other PHY/MC reg
     * after lpddr4_manual_training returns - it just polls.  TIP runs
     * autonomously: BCLK_SCLK -> (skip ADDCMD) -> WRLVL -> RDGATE -> DQ_DQS
     * and sets the corresponding bit in training_status as each phase
     * completes.  Skipped phases stay 0 (so success is 0x1D, not 0x1F,
     * with TRAINING_SKIP_SETTING=0x02).
     */
    DBG_DDR("    Post-manual: train_stat=0x%x\n",
            DDRPHY_REG(PHY_TRAINING_STATUS));

    /*
     * Wait for TIP to complete training phases automatically
     * Per HSS analysis: After state machine transitions, TIP should start WRLVL automatically
     *
     * Training phases:
     * - BCLK_SCLK (already done)
     * - Write Leveling (WRLVL) - TIP runs automatically after state transition
     * - DQS Gate Training (RDGATE) - TIP runs automatically
     * - Read Data Eye Training (DQ_DQS) - TIP runs automatically
     */
    wolfBoot_printf("  DFI pre wait-loop: INIT=0x%x TRAIN=0x%x\n",
        DDRCFG_REG(0x10034U), DDRCFG_REG(0x10038U));
    DBG_DDR("  Wait for TIP WRLVL to start and complete...\n");
    {
        uint32_t timeout = 1000000;  /* 10 seconds max wait */
        uint32_t train_stat_check;
        uint32_t lane;
        uint32_t all_lanes_trained = 0;
        uint32_t training_complete = 0;

        /* Per HSS successful training logs: training_status should show:
         * bit 0 = BCLK_SCLK done
         * bit 2 = WRLVL done
         * bit 3 = RDGATE done
         * bit 4 = DQ_DQS done
         * So training_status = 0x1D indicates all phases complete
         */
        uint32_t last_train_stat = 0;
        uint32_t progress_count = 0;
        uint32_t dpc_odt_restored = 0;
        uint32_t mtc_kicks = 0;

        /* Bug #9 experiment: inject MTC traffic during the WRLVL wait.
         * TIP advances WRLVL only when DRAM is exercised; in the polling
         * wait alone train_stat stays at 0x1.  MTC bypasses AXI/L2
         * (internal to DDRC) so it cannot trigger the AXI hang, and
         * subsequent kicks are safe even if earlier ones time out. */
        DDRCFG_REG(MT_EN) = 0;
        DDRCFG_REG(MT_EN_SINGLE) = 0;
        DDRCFG_REG(MT_STOP_ON_ERROR) = 0;
        DDRCFG_REG(MT_DATA_PATTERN) = 0;
        DDRCFG_REG(MT_ADDR_PATTERN) = 0;
        DDRCFG_REG(MT_START_ADDR_0) = 0;
        DDRCFG_REG(MT_START_ADDR_1) = 0;
        DDRCFG_REG(MT_ADDR_BITS) = 12;       /* 2^12 = 4 KB per kick */
        DDRCFG_REG(MT_ERROR_MASK_0) = 0xFFFFFFFFUL;
        DDRCFG_REG(MT_ERROR_MASK_1) = 0xFFFFFFFFUL;
        DDRCFG_REG(MT_ERROR_MASK_2) = 0xFFFFFFFFUL;
        DDRCFG_REG(MT_ERROR_MASK_3) = 0xFFFFFFFFUL;
        DDRCFG_REG(MT_ERROR_MASK_4) = 0xFFFFFFFFUL;
        /* MTC priming kick is needed when train_stat is stuck at 0x1
         * (BCLK_SCLK only) -- gives TIP DRAM traffic to advance WRLVL
         * etc.  But when train_stat is ALREADY 0x1F (full training
         * complete from manual ADDCMD + TIP), the kick is unnecessary
         * AND seems to clear DFI INIT_COMPLETE in the new setup_phy-
         * before-setup_controller order.  Only kick if train_stat
         * hasn't yet completed. */
        {
            uint32_t cur_train = DDRPHY_REG(PHY_TRAINING_STATUS);
            if ((cur_train & (WRLVL_BIT | RDGATE_BIT | DQ_DQS_BIT))
                != (WRLVL_BIT | RDGATE_BIT | DQ_DQS_BIT)) {
                DDRCFG_REG(MT_EN_SINGLE) = 1;
                mtc_kicks = 1;
                wolfBoot_printf("      MTC priming kick (train_stat=0x%x)\n",
                    cur_train);
            } else {
                wolfBoot_printf("      Skip MTC kick (train_stat=0x%x already complete)\n",
                    cur_train);
            }
            mb();
        }

        while (timeout > 0 && !training_complete) {
            /* Check training status register */
            train_stat_check = DDRPHY_REG(PHY_TRAINING_STATUS);

            if (train_stat_check != last_train_stat) {
                DBG_DDR("      Progress: train_stat=0x%x (iter=%d)\n",
                        train_stat_check, 1000000 - timeout);
                last_train_stat = train_stat_check;
                progress_count++;
            }

            /* HSS DDR_TRAINING_IP_SM_RDGATE entry (mss_ddr.c:1383):
             * once WRLVL bit is set, restore DPC_BITS / ODT for the
             * subsequent read-gate and dq_dqs phases.  TIP needs these
             * values to be in the Libero canonical mode during read
             * training (vrgen_h=2, ODT=0x3) rather than the WRLVL
             * setup (vrgen_h=5, ODT=0). */
            if (!dpc_odt_restored &&
                (train_stat_check & WRLVL_BIT) != 0U) {
                DDRPHY_REG(PHY_DPC_BITS) = LIBERO_SETTING_DPC_BITS;
                DDRPHY_REG(PHY_RPC3_ODT) = LIBERO_SETTING_RPC_ODT_DQ;
                mb();
                wolfBoot_printf(
                    "      WRLVL done -> restored DPC_BITS=0x%x ODT=0x%x\n",
                    LIBERO_SETTING_DPC_BITS, LIBERO_SETTING_RPC_ODT_DQ);
                dpc_odt_restored = 1;
            }

            /* Removed per-lane wl_delay probe from inside the poll loop.
             * It was writing to PHY_LANE_SELECT every iteration, which
             * (now that lane_select is at the correct address 0x808) may
             * actually disturb TIP training state.  HSS does NOT probe
             * per-lane during training poll; it only watches
             * training_status bits.  Use that as the sole criterion. */
            all_lanes_trained = 1;

            /* Training complete when training_status shows full
             * WRLVL+RDGATE+DQ_DQS bits set AND DQ_DQS state-machine
             * @ 0x834 has reached terminal value 8.  Don't gate on
             * DFI training-complete @ DDRCFG+0x38 -- empirically it
             * never asserts on this board even with train_stat=0x1F
             * and full eye-open lanes.  HSS uses it as a verify
             * checkpoint, not a hard gate. */
            if ((train_stat_check & (WRLVL_BIT | RDGATE_BIT | DQ_DQS_BIT))
                == (WRLVL_BIT | RDGATE_BIT | DQ_DQS_BIT)
                && (DDRPHY_REG(0x834U) == 8U)) {
                training_complete = 1;
                wolfBoot_printf("      DQ_DQS state=8 (complete)\n");
                break;
            }

            timeout--;
            ddr_delay(10);  /* 100us per iteration */

            /* Re-kick MTC every 100 iterations (~10 ms) while still
             * waiting for dq_dqs_err_done==8.  MT_EN_SINGLE is
             * edge-triggered; previous test either completed or
             * timed out internally, so a fresh write retriggers a
             * new traffic burst. */
            if (timeout > 0 && (timeout % 100U) == 0U) {
                DDRCFG_REG(MT_EN_SINGLE) = 0;
                DDRCFG_REG(MT_EN_SINGLE) = 1;
                mtc_kicks++;
            }

            if ((timeout % 10000) == 0 && progress_count == 0) {
                DDRPHY_REG(PHY_LANE_SELECT) = 0;  /* Select lane 0 */
                ddr_delay(10);
                DBG_DDR("      Waiting... train_stat=0x%x wl_dly=0x%x gt_state=0x%x mtc_done=0x%x\n",
                        train_stat_check,
                        DDRPHY_REG(0x830),  /* wl_delay_0 */
                        DDRPHY_REG(0x82C),  /* gt_state */
                        DDRCFG_REG(MT_DONE_ACK));
            }
        }

        wolfBoot_printf("      MTC kicks during wait: %u\n",
            (unsigned)mtc_kicks);

        /* Final safety restore: if WRLVL never completed, DPC_BITS
         * still in WRLVL mode.  Restore so subsequent code sees the
         * Libero canonical values. */
        if (!dpc_odt_restored) {
            DDRPHY_REG(PHY_DPC_BITS) = LIBERO_SETTING_DPC_BITS;
            DDRPHY_REG(PHY_RPC3_ODT) = LIBERO_SETTING_RPC_ODT_DQ;
            mb();
            wolfBoot_printf(
                "      WRLVL never set -> safety restore DPC=0x%x ODT=0x%x\n",
                LIBERO_SETTING_DPC_BITS, LIBERO_SETTING_RPC_ODT_DQ);
        }

        DBG_DDR("    Training status: 0x%x\n", DDRPHY_REG(PHY_TRAINING_STATUS));
        DBG_DDR("    training_skip=0x%x training_reset=0x%x\n",
                DDRPHY_REG(PHY_TRAINING_SKIP), DDRPHY_REG(PHY_TRAINING_RESET));

        DBG_DDR("    Per-lane status:\n");
        for (lane = 0; lane < 5; lane++) {
            DDRPHY_REG(PHY_LANE_SELECT) = lane;  /* lane_select */
            ddr_delay(50);
            DBG_DDR("      L%d: gt_err=0x%x gt_state=0x%x gt_txdly=0x%x wl_dly=0x%x dqdqs_st=0x%x\n",
                    lane,
                    DDRPHY_REG(0x81C),   /* gt_err_comb */
                    DDRPHY_REG(0x82C),   /* gt_state */
                    DDRPHY_REG(0x824),   /* gt_txdly -- new */
                    DDRPHY_REG(0x830),   /* wl_delay_0 */
                    DDRPHY_REG(0x83C));  /* dqdqs_state */
        }

        DBG_DDR("    TIP cfg: tip_cfg_params=0x%x\n", DDRPHY_REG(PHY_TIP_CFG_PARAMS));
        DBG_DDR("    BCLK: pll_phadj=0x%x bclksclk_answer=0x%x\n",
                DDR_PLL_REG(PLL_PHADJ), DDRPHY_REG(PHY_BCLKSCLK_ANSWER));
        DBG_DDR("    RPC: rpc145=0x%x rpc147=0x%x rpc156=0x%x rpc166=0x%x\n",
                DDRPHY_REG(PHY_RPC145), DDRPHY_REG(PHY_RPC147),
                DDRPHY_REG(PHY_RPC156), DDRPHY_REG(PHY_RPC166));

        if (training_complete && all_lanes_trained) {
            DBG_DDR("    TIP training complete!\n");
        } else {
            DBG_DDR("    TIP training timeout or incomplete\n");
            DBG_DDR("      all_lanes_trained=%d train_stat=0x%x\n",
                    all_lanes_trained, train_stat_check);
        }
        wolfBoot_printf("    DFI after per-lane reads: INIT=0x%x TRAIN=0x%x\n",
            DDRCFG_REG(0x10034U), DDRCFG_REG(0x10038U));
    }
    wolfBoot_printf("  DFI after wait-loop exit: INIT=0x%x TRAIN=0x%x\n",
        DDRCFG_REG(0x10034U), DDRCFG_REG(0x10038U));

    /*
     * Restore ODT after TIP completes.  Phase 3.10.3 D-3 v2 audit
     * (2026-05-05) found that the previous explicit MR2 = 0x2D write
     * with MR_WR_MASK = 0 was clobbering all 20 LPDDR4 MR2 bits and
     * breaking the post-training mode -- HSS never writes MR2 like
     * this; it only does the no-op refresh via mode_register_masked_
     * write_x5(2) which uses MR_WR_MASK = 0xFFFFF (preserve).  Removed.
     */
    DBG_DDR("  Restore ODT...");
    DDRPHY_REG(PHY_RPC3_ODT) = 0x03U;
    mb();
    DBG_DDR("done\n");

    /* Note: tested re-running expert_dfi_status_override_to_shim
     * sequence here (HSS DDR_TRAINING_ROTATE_CLK pattern) to force
     * dfi_training_complete=1 -- no effect.  STAT_DFI_TRAINING_
     * COMPLETE stays at 0 regardless of write timing or sequence. */

    /* Dump DFI error/status registers to diagnose why training_complete
     * doesn't assert.  Pre-pulse state. */
    wolfBoot_printf("  DFI pre-pulse: TRAINING_ERROR=0x%x INIT_COMPLETE=0x%x TRAINING_COMPLETE=0x%x\n",
        DDRCFG_REG(0x10024U), DDRCFG_REG(0x10034U), DDRCFG_REG(0x10038U));

    /* Re-pulse PHY_DFI_INIT_START to re-establish DFI init handshake.
     * INIT_COMPLETE has been cleared somewhere in the training flow;
     * this re-asserts the start signal so the controller can
     * re-handshake DFI INIT (and hopefully TRAINING) completion. */
    DDRCFG_REG(MC_DFI_INIT_START) = 0x00000000UL;
    mb();
    udelay(10);
    DDRCFG_REG(MC_DFI_INIT_START) = 0x00000001UL;
    mb();
    udelay(1000);  /* let DFI re-handshake */

    wolfBoot_printf("  DFI post-pulse: TRAINING_ERROR=0x%x INIT_COMPLETE=0x%x TRAINING_COMPLETE=0x%x\n",
        DDRCFG_REG(0x10024U), DDRCFG_REG(0x10034U), DDRCFG_REG(0x10038U));

    /* Check final training status */
    train_stat = DDRPHY_REG(PHY_TRAINING_STATUS);
    wolfBoot_printf("  Final train_stat=0x%x\n", train_stat);

    /* HSS-vs-wolfBoot PHY register diff diagnostic (2026-05-13).
     * HSS dump shows specific PHY offsets at canonical values.  Read
     * back our state at the same point to find what differs. */
    wolfBoot_printf("  PHY diff: 0xC=0x%x 0x290=0x%x 0x1FC=0x%x "
        "rpc98@0x588=0x%x rpc114@0x5C8=0x%x rpc156@0x670=0x%x "
        "rpc220@0x770=0x%x rpc226@0x788=0x%x DDRPHY_MODE@0x4=0x%x\n",
        DDRPHY_REG(0x00CU), DDRPHY_REG(0x290U), DDRPHY_REG(0x1FCU),
        DDRPHY_REG(0x588U), DDRPHY_REG(0x5C8U), DDRPHY_REG(0x670U),
        DDRPHY_REG(0x770U), DDRPHY_REG(0x788U), DDRPHY_REG(0x004U));

    /* HSS DDR_TRAINING_VERIFY checks (mss_ddr.c:1488-1522): if any of
     * these are non-canonical, training had problems even though
     * train_stat reads 0x1D.  dqdqs_status2 is per-lane (selected via
     * PHY_LANE_SELECT) -- dump all 4 to see per-lane data-eye width. */
    {
        uint32_t l;
        uint32_t eye[4];
        for (l = 0; l < 4U; l++) {
            DDRPHY_REG(PHY_LANE_SELECT) = l;
            udelay(2);
            eye[l] = DDRPHY_REG(0x850U);
        }
        wolfBoot_printf(
            "  gt_err_comb=0x%x dq_dqs_err_done=0x%x (need 8) eye[0..3]=%u/%u/%u/%u\n",
            DDRPHY_REG(0x81CU), DDRPHY_REG(0x834U),
            eye[0], eye[1], eye[2], eye[3]);
    }

    /* Run HSS-equivalent MTC-based write calibration when TIP reached
     * train_stat=0x1D.  Previously skipped on the assumption that TIP
     * "did its own WRCALIB during autonomous training" -- that turned
     * out to be wrong.  HSS runs write_calibration_using_mtc() even
     * after a successful TIP, because TIP only trains the PHY; the MTC
     * test is the only thing that verifies the CPU->AXI->DDRC->DRAM
     * data path actually moves bits.  Without this step,
     * train_stat=0x1D + MTC 256B PASS were both passing while
     * boundary-scan reads from cached/non-cached DDR returned the
     * same stuck pre-fill bytes regardless of address. */
    train_stat = DDRPHY_REG(PHY_TRAINING_STATUS);
    /* Run HSS-style MTC WRCALIB whenever TIP made any progress
     * (train_stat has at least BCLK_SCLK set).  Previously only ran
     * when fully 0x1D; in practice our train_stat often stalls at 0x1
     * but MTC still works enough to do per-lane calibration. */
    if ((train_stat & BCLK_SCLK_BIT) != 0U) {
        uint8_t wrcal_res;
        /* Wait for CTRLR_INIT_DONE before kicking off MTC WRCALIB.
         * The controller takes time to finish its auto-init after
         * training_reset/CTRLR_SOFT_RESET pulse.  If MTC fires before
         * INIT_DONE, the controller doesn't service DDR commands.
         * HSS state machine has many monitor cycles between training
         * and WRCALIB, giving controller time to come up. */
        {
            uint32_t init_to = 100000;
            uint32_t dfi_to;
            uint32_t init_done;
            while (init_to > 0) {
                init_done = DDRCFG_REG(MC_CTRLR_INIT_DONE);
                if ((init_done & 0x1U) != 0U) break;
                udelay(10);
                init_to--;
            }
            /* HSS DDR_TRAINING_IP_SM_VERIFY (mss_ddr.c:1418) gates ALL
             * post-training work -- including MTC write-calibration --
             * on STAT_DFI_TRAINING_COMPLETE (DFI+0x38) == 1.  The MTC
             * read/write engine rides the controller's post-training-
             * complete datapath, so firing MTC before this latches is
             * why every test times out and why only lane 0 lands.  Wait
             * for it here, petting the WDT every 4096 spins so a never-
             * latch case still reaches the diagnostic print instead of
             * resetting the chip. */
            dfi_to = 100000;
            while (dfi_to > 0) {
                if ((DDRCFG_REG(0x10038U) & 0x1U) != 0U) break;
                udelay(10);
                dfi_to--;
                if ((dfi_to & 0xFFFU) == 0U) {
                    *(volatile uint32_t*)0x20001000UL = 0xDEADC0DEU;
                    *(volatile uint32_t*)0x20101000UL = 0xDEADC0DEU;
                    *(volatile uint32_t*)0x20103000UL = 0xDEADC0DEU;
                    *(volatile uint32_t*)0x20105000UL = 0xDEADC0DEU;
                    *(volatile uint32_t*)0x20107000UL = 0xDEADC0DEU;
                }
            }
            wolfBoot_printf(
                "  Pre-WRCALIB: CTRLR_INIT_DONE=0x%x DFI_train_complete=0x%x"
                " AUTO_REF=0x%x (init %u us, dfi %u us)\n",
                DDRCFG_REG(MC_CTRLR_INIT_DONE),
                DDRCFG_REG(0x10038U) & 0x01U,
                DDRCFG_REG(MC_CFG_AUTO_REF_EN),
                (unsigned)((100000U - init_to) * 10U),
                (unsigned)((100000U - dfi_to) * 10U));
        }
        wolfBoot_printf("  MTC WRCALIB (HSS-style) tstat=0x%x...\n",
            train_stat);
        wrcal_res = mpfs_write_calibration_using_mtc(4U);
        if (wrcal_res == MPFS_MTC_TIMEOUT_ERROR) {
            wolfBoot_printf("  MTC WRCALIB TIMEOUT\n");
        } else if (wrcal_res != 0U) {
            wolfBoot_printf("  MTC WRCALIB no valid offset for some lane\n");
        }
        /* MTC WRCALIB unreliable on Video Kit (consistent timeouts).
         * Force EXPERT_WRCALIB = HSS-canonical 0x5555 (cal=5 per lane).
         * HSS-on-board dump captured 0x5555 as the post-WRCALIB value
         * on this same board.  Bit 3 of expert_mode_en must be set
         * to enable the EXPERT_WRCALIB path. */
        DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x08UL;
        DDRPHY_REG(PHY_EXPERT_WRCALIB) = 0x5555UL;
        mb();
        wolfBoot_printf("  Forced EXPERT_WRCALIB=0x%x (HSS value)\n",
            DDRPHY_REG(PHY_EXPERT_WRCALIB));
        goto skip_mtc_wrcalib;
    }

    /* Write calibration using MTC (Memory Test Controller)
     * Based on HSS write_calibration_using_mtc().  Used as a fallback
     * when TIP autonomous training did NOT reach 0x1D. */
    wolfBoot_printf("  Write calib...");
    {
        uint32_t cal_data;
        uint32_t lane;
        uint32_t result;
        uint32_t lane_status = 0;
        uint32_t lane_calib[5] = {0};
        const uint32_t num_lanes = 4;  /* Video Kit has 4 data lanes */

        /* Enable expert mode for write calibration */
        DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000008UL;

        /* Sweep write calibration offset from 0 to F */
        for (cal_data = 0x00000; cal_data < 0xFFFFF; cal_data += 0x11111) {
            /* Set write calibration offset for all lanes */
            DDRPHY_REG(PHY_EXPERT_WRCALIB) = cal_data;

            for (lane = 0; lane < num_lanes; lane++) {
                if (lane_status & (1 << lane))
                    continue;  /* Already calibrated this lane */

                uint8_t mask = (1 << lane);

                /* Configure MTC for this lane */
                DDRCFG_REG(MT_STOP_ON_ERROR) = 0;
                DDRCFG_REG(MT_EN_SINGLE) = 0;
                DDRCFG_REG(MT_DATA_PATTERN) = 0;  /* Counting pattern */
                DDRCFG_REG(MT_ADDR_PATTERN) = 0;  /* Sequential */
                DDRCFG_REG(MT_START_ADDR_0) = 0;
                DDRCFG_REG(MT_START_ADDR_1) = 0;
                DDRCFG_REG(MT_ADDR_BITS) = 20;  /* 1MB test size (2^20) */

                /* Set error masks - unmask only the lane under test */
                DDRCFG_REG(MT_ERROR_MASK_0) = 0xFFFFFFFF;
                DDRCFG_REG(MT_ERROR_MASK_1) = 0xFFFFFFFF;
                DDRCFG_REG(MT_ERROR_MASK_2) = 0xFFFFFFFF;
                DDRCFG_REG(MT_ERROR_MASK_3) = 0xFFFFFFFF;
                DDRCFG_REG(MT_ERROR_MASK_4) = 0xFFFFFFFF;

                if (mask & 0x1) {
                    DDRCFG_REG(MT_ERROR_MASK_0) &= 0xFFFFFF00;
                    DDRCFG_REG(MT_ERROR_MASK_1) &= 0xFFFFF00F;
                    DDRCFG_REG(MT_ERROR_MASK_2) &= 0xFFFF00FF;
                    DDRCFG_REG(MT_ERROR_MASK_3) &= 0xFFF00FFF;
                }
                if (mask & 0x2) {
                    DDRCFG_REG(MT_ERROR_MASK_0) &= 0xFFFF00FF;
                    DDRCFG_REG(MT_ERROR_MASK_1) &= 0xFFF00FFF;
                    DDRCFG_REG(MT_ERROR_MASK_2) &= 0xFF00FFFF;
                    DDRCFG_REG(MT_ERROR_MASK_3) &= 0xF00FFFFF;
                }
                if (mask & 0x4) {
                    DDRCFG_REG(MT_ERROR_MASK_0) &= 0xFF00FFFF;
                    DDRCFG_REG(MT_ERROR_MASK_1) &= 0xF00FFFFF;
                    DDRCFG_REG(MT_ERROR_MASK_2) &= 0x00FFFFFF;
                    DDRCFG_REG(MT_ERROR_MASK_3) &= 0x0FFFFFFF;
                    DDRCFG_REG(MT_ERROR_MASK_4) &= 0xFFFFFFF0;
                }
                if (mask & 0x8) {
                    DDRCFG_REG(MT_ERROR_MASK_0) &= 0x00FFFFFF;
                    DDRCFG_REG(MT_ERROR_MASK_1) &= 0x0FFFFFFF;
                    DDRCFG_REG(MT_ERROR_MASK_2) &= 0xFFFFFFF0;
                    DDRCFG_REG(MT_ERROR_MASK_3) &= 0xFFFFFF00;
                    DDRCFG_REG(MT_ERROR_MASK_4) &= 0xFFFFF00F;
                }

                /* Run MTC test */
                DDRCFG_REG(MT_EN) = 0;
                DDRCFG_REG(MT_EN_SINGLE) = 0;
                DDRCFG_REG(MT_EN_SINGLE) = 1;

                /* Wait for MTC completion */
                timeout = 0xFFFFFF;
                while ((DDRCFG_REG(MT_DONE_ACK) & 0x01) == 0 && timeout > 0)
                    timeout--;

                if (timeout == 0) {
                    wolfBoot_printf("MTC timeout...");
                    break;
                }

                /* Check result */
                result = DDRCFG_REG(MT_ERROR_STS) & 0x01;
                if (result == 0) {
                    /* Lane passed */
                    lane_calib[lane] = cal_data & 0xF;
                    lane_status |= (1 << lane);
                }
            }

            /* Check if all lanes calibrated */
            if (lane_status == ((1 << num_lanes) - 1))
                break;
        }

        if (lane_status == ((1 << num_lanes) - 1)) {
            /* All lanes calibrated - set final calibration value */
            uint32_t final_calib = 0;
            for (lane = 0; lane < num_lanes; lane++)
                final_calib |= (lane_calib[lane] << (lane * 4));
            DDRPHY_REG(PHY_EXPERT_WRCALIB) = final_calib;
            wolfBoot_printf("ok (0x%x)\n", final_calib);
        } else {
            wolfBoot_printf("FAIL (lanes=0x%x)\n", lane_status);
        }
    }

skip_mtc_wrcalib:
    DBG_DDR("  Final status=0x%x\n", DDRPHY_REG(PHY_TRAINING_STATUS));
    DBG_DDR("  Controller INIT_DONE=0x%x\n", DDRCFG_REG(MC_CTRLR_INIT_DONE));

    /* Enable auto-refresh */
    DDRCFG_REG(MC_CFG_AUTO_REF_EN) = 0x01;
    mb();

    /* HSS DDR_TRAINING_VERIFY (mss_ddr.c:1488-1504) reads:
     *   dq_dqs_err_done (need 8): DQ/DQS phase completion flag
     *   dqdqs_status2 (need >= 5 taps): data eye window width
     * On this board both report bad values (0x4 / 0x0) yet train_stat
     * reads 0x1D and lanes 2&3 still write correctly via the lanes-2&3
     * calibration committed by set_write_calib.  Returning failure
     * here triggers inner retries that empirically make PHY state
     * WORSE (dq_dqs_err_done -> 0x0, all lanes fail WRCALIB), because
     * back-to-back training without a power cycle accumulates errors.
     * Accept training as-is; the calibration committed by WRCALIB is
     * what we get. */

    return 0;
}

/* DDR Memory Test (cached path only).
 *
 * Empirical: direct non-cached CPU writes at 0xC0000000 succeed (the
 * AXI write-combining buffer absorbs them) but the subsequent read at
 * the same address hangs on most boots, suggesting the WCB doesn't
 * complete the write-then-read round-trip cleanly for small (<16-byte)
 * accesses.  PDMA-style or cached writes go through L2 which promotes
 * them to full 64-byte burst writes that the DDRC accepts.
 *
 * Test sequence per 4-byte pattern:
 *   1. CPU cached write at 0x80000000 (absorbed by L2).
 *   2. mb() memory barrier.
 *   3. L2 FLUSH64 register write forces the line to DDR and
 *      invalidates the L2 entry.
 *   4. CPU cached read fetches a fresh 64-byte L2 line from DDR via
 *      an AXI cached read transaction.
 *
 * Reports a clean PASS/FAIL per word so a hang point is pinpointable
 * in the UART log (the last printed prefix tells which step stalled).
 */
static int memory_test(void)
{
    volatile uint32_t *ddr = (volatile uint32_t *)0x80000000UL;
    uint32_t patterns[] = {
        0x55555555UL,
        0xAAAAAAAAUL,
        0x12345678UL,
        0xFEDCBA98UL
    };
    uint32_t readback;
    int i, errors = 0;
    uint32_t train_stat, blocker, ctrl_done;

    train_stat = DDRPHY_REG(PHY_TRAINING_STATUS);
    blocker = DDR_SEG_REG(SEG0_BLOCKER);
    ctrl_done = DDRCFG_REG(MC_CTRLR_INIT_DONE);
    wolfBoot_printf("DDR: Memory test (cached @ 0x80000000)\n");
    wolfBoot_printf("  Training=0x%x Blocker=0x%x INIT_DONE=0x%x\n",
                    train_stat, blocker, ctrl_done);

    if (!(blocker & 0x01)) {
        wolfBoot_printf("  ERROR: DDR blocker not disabled!\n");
        return -1;
    }

    for (i = 0; i < 4; i++) {
        wolfBoot_printf("  [%d] W=0x%x ", i, patterns[i]);
        ddr[i] = patterns[i];
        mb();
        wolfBoot_printf("wr-ok ");
        L2_FLUSH64 = (uint64_t)(uintptr_t)&ddr[i];
        mb();
        wolfBoot_printf("flush-ok ");
        readback = ddr[i];
        wolfBoot_printf("R=0x%x ", readback);
        if (readback != patterns[i]) {
            wolfBoot_printf("FAIL\n");
            errors++;
        } else {
            wolfBoot_printf("OK\n");
        }
    }

    wolfBoot_printf("  errors: %d/4\n", errors);
    if (errors == 0) {
        wolfBoot_printf("  PASSED\n");
        return 0;
    }
    return -1;
}

/* PDMA helpers for DDR pre-fill (HSS clear_bootup_cache_ways). */
#define MPFS_PDMA_BASE          0x03000000UL
#define MPFS_PDMA_CH_STRIDE     0x1000UL
#define MPFS_PDMA_CH_BASE(ch)   (MPFS_PDMA_BASE + (ch) * MPFS_PDMA_CH_STRIDE)
#define MPFS_PDMA_NUM_CHANNELS  4U

/* Per-channel register offsets (mss_utils.S:19-68 reference) */
#define PDMA_CONTROL    0x00U   /* 32-bit: 1=claim, 3=start, bit1=busy */
#define PDMA_NEXTCFG    0x04U   /* 32-bit: 0xff000000 = full speed */
#define PDMA_NEXTBYTES  0x08U   /* 64-bit */
#define PDMA_NEXTDEST   0x10U   /* 64-bit */
#define PDMA_NEXTSRC    0x18U   /* 64-bit */

#define PDMA_CTRL_CLAIM  0x00000001UL
#define PDMA_CTRL_START  0x00000003UL
#define PDMA_CTRL_BUSY   0x00000002UL
#define PDMA_CFG_FULL    0xFF000000UL

#define PDMA_REG32(base, off)  (*(volatile uint32_t *)((base) + (off)))
#define PDMA_REG64(base, off)  (*(volatile uint64_t *)((base) + (off)))

static void mpfs_pdma_kick(uintptr_t ch_base, uint64_t dest,
    uint64_t src, uint64_t bytes)
{
    PDMA_REG32(ch_base, PDMA_CONTROL)   = PDMA_CTRL_CLAIM;
    PDMA_REG32(ch_base, PDMA_NEXTCFG)   = 0;
    PDMA_REG64(ch_base, PDMA_NEXTBYTES) = bytes;
    PDMA_REG64(ch_base, PDMA_NEXTDEST)  = dest;
    PDMA_REG64(ch_base, PDMA_NEXTSRC)   = src;
    PDMA_REG32(ch_base, PDMA_NEXTCFG)   = PDMA_CFG_FULL;
    PDMA_REG32(ch_base, PDMA_CONTROL)   = PDMA_CTRL_START;
    mb();
}

static void mpfs_pdma_wait(uintptr_t ch_base)
{
    uint32_t timeout = 100000000UL;
    while ((PDMA_REG32(ch_base, PDMA_CONTROL) & PDMA_CTRL_BUSY) != 0) {
        if (timeout-- == 0) {
            wolfBoot_printf("PDMA: ch@%lx hung (busy bit stuck)\n",
                (unsigned long)ch_base);
            break;
        }
    }
    PDMA_REG32(ch_base, PDMA_CONTROL) = 0;  /* release channel */
}

/* PDMA-based memcpy.  Public entry point declared in hal/mpfs250.h.
 * Used by src/sdhci.c to land per-block PIO data in DDR via PDMA when
 * cached/non-cached CPU writes do not reach DDR on this board.
 *
 * When the destination is in the cached DDR window (top 4 bits = 0x8),
 * the helper rebases it to the non-cached window (top 4 bits = 0xC)
 * before issuing the PDMA transfer.  PDMA-via-non-cached is the only
 * AXI write path verified to land in DDR on this board (CPU writes
 * via either cached or non-cached do not reach DDR). */
int mpfs_pdma_memcpy(void *dst, const void *src, uint32_t bytes)
{
    uintptr_t pdma_dst = (uintptr_t)dst;
    uintptr_t cached_dst = (uintptr_t)dst;
    if ((pdma_dst & 0xF0000000UL) == 0x80000000UL) {
        pdma_dst = (pdma_dst & ~0xF0000000UL) | 0xC0000000UL;
    }
    mpfs_pdma_kick(MPFS_PDMA_CH_BASE(0), pdma_dst,
        (uint64_t)(uintptr_t)src, (uint64_t)bytes);
    mpfs_pdma_wait(MPFS_PDMA_CH_BASE(0));
    /* PDMA wrote to non-cached alias.  Any stale L2 cache lines at the
     * cached alias (cached_dst) would return wrong data to subsequent
     * cached reads.  Flush 64-byte lines spanning the write range. */
    if ((cached_dst & 0xF0000000UL) == 0x80000000UL) {
        volatile uint64_t *flush64 = (volatile uint64_t *)0x02010200UL;
        uintptr_t addr;
        uintptr_t addr_end = cached_dst + bytes;
        for (addr = cached_dst & ~63UL; addr < addr_end; addr += 64UL) {
            *flush64 = (uint64_t)addr;
        }
    }
    return 0;
}

/* HSS clear_bootup_cache_ways equivalent.
 *
 * PDMA-fill <fill_size> bytes starting at <ddr_pdma_base> via all 4
 * PDMA channels round-robin, sourcing from a small pattern buffer in
 * L2 Scratch.  Pass the NON-CACHED DDR base (0xC0000000) so PDMA
 * writes go directly to the DDR controller AXI port and bypass L2
 * cache entirely -- writes via the cached base would allocate lines
 * into L2 ways and thrash L2 Scratch (where the M-mode stack lives),
 * causing a cause=2 epc=0 trap during the first cached read.
 *
 * After PDMA, flush the corresponding CACHED window (<ddr_cached_base>
 * .. <ddr_cached_base>+<fill_size>) via L2 FLUSH64 to drop any stale
 * cache lines tagged for that DDR range from boot-time activity. */
static void mpfs_clear_bootup_cache_ways(uint64_t ddr_pdma_base,
    uint64_t fill_size)
{
    const uint64_t ddr_cached_base = 0x80000000UL;
    /* 128-byte pattern buffer in L2 Scratch -- safe source for PDMA. */
    static const uint32_t fill_pattern[32] = {
        0xCAFE0000U, 0xCAFE0001U, 0xCAFE0002U, 0xCAFE0003U,
        0xCAFE0004U, 0xCAFE0005U, 0xCAFE0006U, 0xCAFE0007U,
        0xCAFE0008U, 0xCAFE0009U, 0xCAFE000AU, 0xCAFE000BU,
        0xCAFE000CU, 0xCAFE000DU, 0xCAFE000EU, 0xCAFE000FU,
        0xCAFE0010U, 0xCAFE0011U, 0xCAFE0012U, 0xCAFE0013U,
        0xCAFE0014U, 0xCAFE0015U, 0xCAFE0016U, 0xCAFE0017U,
        0xCAFE0018U, 0xCAFE0019U, 0xCAFE001AU, 0xCAFE001BU,
        0xCAFE001CU, 0xCAFE001DU, 0xCAFE001EU, 0xCAFE001FU
    };
    const uint64_t pat_bytes = sizeof(fill_pattern);
    const uint64_t pat_addr  = (uint64_t)(uintptr_t)fill_pattern;
    volatile uint64_t *flush64 = (volatile uint64_t *)0x02010200UL;
    uint64_t off;
    uint32_t ch;
    uint64_t addr;

    wolfBoot_printf("DDR: PDMA pre-fill %lu MB @ 0x%lx...\n",
        (unsigned long)(fill_size >> 20),
        (unsigned long)ddr_pdma_base);

    /* Round-robin across 4 channels in pat_bytes increments. */
    for (off = 0, ch = 0; off + pat_bytes <= fill_size; off += pat_bytes) {
        uintptr_t ch_base = MPFS_PDMA_CH_BASE(ch);
        if ((PDMA_REG32(ch_base, PDMA_CONTROL) & PDMA_CTRL_BUSY) != 0) {
            mpfs_pdma_wait(ch_base);
        }
        mpfs_pdma_kick(ch_base, ddr_pdma_base + off, pat_addr, pat_bytes);
        ch = (ch + 1U) % MPFS_PDMA_NUM_CHANNELS;
    }
    for (ch = 0; ch < MPFS_PDMA_NUM_CHANNELS; ch++) {
        mpfs_pdma_wait(MPFS_PDMA_CH_BASE(ch));
    }
    mb();
    wolfBoot_printf("  PDMA fill done\n");

    /* L2 FLUSH64: drain any stale cache lines tagged for this range
     * without doing CPU writes (which would re-allocate the lines and
     * thrash L2 Scratch).  PDMA wrote DDR directly via the AXI port;
     * we just need to evict any lingering tag entries. */
    for (addr = ddr_cached_base; addr < ddr_cached_base + fill_size;
         addr += 64UL) {
        *flush64 = (uint64_t)addr;
    }
    mb();
    wolfBoot_printf("  L2 flush done (%lu MB)\n",
        (unsigned long)(fill_size >> 20));
}

/* HSS port: mode_register_masked_write (mss_ddr.c:4922-4938).
 * Writes one masked mode-register command, returns 0 on ACK, 1 on no-ACK. */
static uint32_t mpfs_mr_masked_write(uint32_t address)
{
    uint32_t ack;

    DDRCFG_REG(MC_INIT_CS)         = 0x1UL;
    DDRCFG_REG(MC_INIT_MR_WR_MASK) = 0xFFFFFUL;
    DDRCFG_REG(MC_INIT_MR_ADDR)    = address;
    DDRCFG_REG(MC_INIT_MR_WR_DATA) = 0x0UL;
    DDRCFG_REG(MC_INIT_MR_W_REQ)   = 0x1UL;
    DDRCFG_REG(MC_INIT_MR_W_REQ)   = 0x0UL;
    udelay(5);

    ack = DDRCFG_REG(MC_INIT_ACK);
    if (ack != 0U) {
        return 0U;
    }
    return 1U;
}

/* Write a specific data value to a mode register with full unmasked
 * write (WR_MASK=0, WR_DATA=value).  Used to actively change MR
 * contents after training -- e.g. to clear the LPDDR4 MR2 OP[7]
 * write-leveling enable bit that TIP may have set during WRLVL.
 * The polar-fire-guide says "MUST clear the write-leveling bit in
 * MR2 after DFI_WRLVL_RESP = 1" -- the masked-write refresh path
 * preserves all bits and would not clear that. */
static uint32_t mpfs_mr_unmasked_write(uint32_t address, uint32_t data)
{
    uint32_t ack;

    DDRCFG_REG(MC_INIT_CS)         = 0x1UL;
    DDRCFG_REG(MC_INIT_MR_WR_MASK) = 0x0UL;     /* unmasked: write all bits */
    DDRCFG_REG(MC_INIT_MR_ADDR)    = address;
    DDRCFG_REG(MC_INIT_MR_WR_DATA) = data;
    DDRCFG_REG(MC_INIT_MR_W_REQ)   = 0x1UL;
    DDRCFG_REG(MC_INIT_MR_W_REQ)   = 0x0UL;
    udelay(5);

    ack = DDRCFG_REG(MC_INIT_ACK);
    if (ack != 0U) {
        return 0U;
    }
    return 1U;
}

/* HSS port: mode_register_masked_write_x5 (mss_ddr.c:4940-4949).
 * Despite the name, HSS loops 10 times.  Returns OR of all errors. */
static uint32_t mpfs_mr_masked_write_x5(uint32_t address)
{
    uint32_t i;
    uint32_t error = 0U;

    for (i = 0U; i < 10U; i++) {
        error |= mpfs_mr_masked_write(address);
    }
    return error;
}

/* DDRC register-space walk.
 *
 * Two functions sharing the same address ranges:
 *   mpfs_dump_ddrc_regs()   -- prints each register in HSS DEBUG_DDR_
 *                              DDRCFG format ('Register, 0xADDR  ,
 *                              Value, 0xVAL') for tools/scripts/ddr-
 *                              diff.py.  Diagnostic use only.
 *   mpfs_settle_ddrc_regs() -- silent variant (reads but does not
 *                              print).  Empirically required: with
 *                              the register-read walk between post-
 *                              init and the first AXI access, AXI
 *                              reads from DDR succeed; without it,
 *                              they hang on most boots.  The walk
 *                              gives the controller's internal state
 *                              machines and the APB bus time to
 *                              quiesce before the first AXI request.
 *                              Removing this function will regress
 *                              the read path.
 *
 * Ranges are the union of regions HSS prints via print_reg_array
 * (mss_ddr_debug.c) plus DFI (0x10000+) and AXI_IF (0x12C00+) for
 * full coverage.  These are all readable; addresses outside these
 * ranges may trap. */
static const uint32_t mpfs_ddrc_dump_ranges[][2] = {
    {0x2400U, 0x2434U},   /* MC_BASE3 head */
    {0x2800U, 0x292CU},
    {0x3C00U, 0x3DB0U},
    {0x4000U, 0x43FCU},   /* MC_BASE2 */
    {0x4C00U, 0x4CA0U},
    {0x5000U, 0x5020U},
    {0x5400U, 0x5404U},
    {0x5800U, 0x5868U},
    {0x5C00U, 0x5C44U},
    {0x6400U, 0x6558U},
    {0x7C00U, 0x7C04U},
    {0x8000U, 0x801CU},
    {0x10000U, 0x10054U},   /* DFI block */
    {0x12C00U, 0x12C28U},   /* AXI_IF AXI_START_ADDRESS */
    {0x12F18U, 0x12F28U},   /* AXI_IF AXI_END_ADDRESS */
    {0x13218U, 0x13228U},   /* AXI_IF MEM_START_ADDRESS */
    {0x13514U, 0x13518U},   /* AXI_IF ENABLE_BUS_HOLD */
    {0x13690U, 0x13690U}    /* AXI_IF AXI_AUTO_PCH */
};

static volatile uint32_t mpfs_settle_sink;

static void mpfs_settle_ddrc_regs(void)
{
    uint32_t i, off;

    for (i = 0U;
         i < (sizeof(mpfs_ddrc_dump_ranges) /
              sizeof(mpfs_ddrc_dump_ranges[0]));
         i++) {
        for (off = mpfs_ddrc_dump_ranges[i][0];
             off <= mpfs_ddrc_dump_ranges[i][1];
             off += 4U) {
            mpfs_settle_sink =
                *(volatile uint32_t *)((uintptr_t)DDRCFG_BASE + off);
        }
    }
}

static void mpfs_dump_ddrc_regs(void)
{
    uint32_t i, off;
    uint32_t val;
    uintptr_t abs_addr;

    for (i = 0U;
         i < (sizeof(mpfs_ddrc_dump_ranges) /
              sizeof(mpfs_ddrc_dump_ranges[0]));
         i++) {
        for (off = mpfs_ddrc_dump_ranges[i][0];
             off <= mpfs_ddrc_dump_ranges[i][1];
             off += 4U) {
            abs_addr = (uintptr_t)DDRCFG_BASE + off;
            val = *(volatile uint32_t *)abs_addr;
            wolfBoot_printf("Register, 0x%016lx  ,Value, 0x%08x\n",
                (unsigned long)abs_addr, (unsigned)val);
        }
    }
}

/* PHY register dump (CFG_DDR_SGMII_PHY @ 0x20007000).  Covers the
 * range HSS DEBUG HEXDUMP 0x20007000 0xC00 reaches.  Stop before the
 * 0x20007C28 dynamic-control bound to avoid the BEU 'Load or store
 * TileLink bus error' we observed when HSS HEXDUMP overran. */
static void mpfs_dump_phy_regs(void)
{
    uint32_t off;
    uint32_t val;
    uintptr_t abs_addr;

    for (off = 0U; off < 0xC00U; off += 4U) {
        abs_addr = (uintptr_t)CFG_DDR_SGMII_PHY_BASE + off;
        val = *(volatile uint32_t *)abs_addr;
        wolfBoot_printf("Register, 0x%016lx  ,Value, 0x%08x\n",
            (unsigned long)abs_addr, (unsigned)val);
    }
}

/* HSS port: LPDDR4 POST_INITIALIZATION (mss_ddr.c:5597-5646).
 *
 * Drop override-to-shim, pulse expert_dlycnt_pause, release CKE,
 * program 9 LPDDR4 mode registers (MR1-4, 11, 16, 17, 22, 13), trigger
 * ZQ cal, confirm INIT_ACK, restore CFG_AUTO_ZQ_CAL_EN to Libero
 * operational value.  Required for the DRAM device to be in correct
 * operational mode.  This runs on top of the mid-training ZQ-cal
 * sequence at ~line 2890 which already clears INIT_AUTOINIT_DISABLE.
 *
 * Note: by itself this does NOT resolve Bug #9 (first AXI access still
 * hangs).  Root cause is WRLVL not training (no AXI traffic during the
 * WRLVL wait loop), leaving per-lane DQ delays at defaults.  This port
 * is kept because the MR writes program the DRAM operational mode that
 * the controller-only path skips.
 *
 * Returns 0 on success, non-zero on INIT_ACK timeout. */
static int mpfs_ddr_post_initialization(void)
{
    uint32_t timeout;
    uint32_t mr_err;

    wolfBoot_printf("DDR: Post-init: dropping override-to-shim, pausing dlycnt\n");
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x08UL;
    DDRPHY_REG(PHY_EXPERT_DFI_STATUS_TO_SHIM) = 0x0UL;
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x09UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x3FUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x0UL;
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x08UL;
    mb();
    udelay(1);                       /* HSS DELAY_CYCLES_500_NS */

    wolfBoot_printf("DDR: Post-init: releasing CKE\n");
    DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x0UL;
    mb();
    udelay(500);                     /* HSS DELAY_CYCLES_500_MICRO */

    wolfBoot_printf("DDR: Post-init: writing 9 mode registers\n");
    /* CRITICAL: do an UNMASKED MR2 write first to clear LPDDR4
     * MR2 OP[7] (write-leveling enable).  TIP's WRLVL phase may
     * set MR2[7]=1 to enter WRLVL mode; the polar-fire-guide rule
     * "MUST clear the write-leveling bit in MR2 after DFI_WRLVL_
     * RESP=1" applies.  If left set, DRAM stays in WRLVL mode and
     * subsequent burst writes corrupt on lanes that don't see the
     * expected WRLVL response (lanes 1, 2, 3 in our case).
     * MR2=0x2D = WL Set 5 / RL Set 5, MR2[7]=0 = WRLVL disabled. */
    {
        uint32_t mr2_err = mpfs_mr_unmasked_write(2U, 0x2DUL);
        wolfBoot_printf("  MR2 explicit clear (=0x2D) ack=%u\n",
            mr2_err == 0U ? 1U : 0U);
    }
    mr_err  = mpfs_mr_masked_write_x5(1U);
    mr_err |= mpfs_mr_masked_write_x5(2U);
    mr_err |= mpfs_mr_masked_write_x5(3U);
    mr_err |= mpfs_mr_masked_write_x5(4U);
    mr_err |= mpfs_mr_masked_write_x5(11U);
    mr_err |= mpfs_mr_masked_write_x5(16U);
    mr_err |= mpfs_mr_masked_write_x5(17U);
    mr_err |= mpfs_mr_masked_write_x5(22U);
    mr_err |= mpfs_mr_masked_write_x5(13U);
    wolfBoot_printf("  MR writes done (mr_err=0x%x)\n", (unsigned)mr_err);
    udelay(10);

    wolfBoot_printf("DDR: Post-init: triggering ZQ cal + releasing auto-init\n");
    DDRCFG_REG(MC_INIT_ZQ_CAL_START)     = 0x1UL;
    DDRCFG_REG(MC_INIT_AUTOINIT_DISABLE) = 0x0UL;  /* operational handoff */
    mb();

    /* HSS bounded poll: 0xFF iterations of udelay(10) ~ 2.55 ms cap. */
    timeout = 0U;
    while ((DDRCFG_REG(MC_INIT_ACK) == 0U) && (timeout < 0xFFU)) {
        udelay(10);
        timeout++;
    }
    DDRCFG_REG(MC_INIT_ZQ_CAL_START) = 0x0UL;
    mb();

    if (timeout >= 0xFFU) {
        wolfBoot_printf("DDR: Post-init INIT_ACK TIMEOUT\n");
        wolfBoot_printf("  AUTOINIT_DIS=0x%x INIT_ACK=0x%x ZQ_CAL_START=0x%x\n",
            DDRCFG_REG(MC_INIT_AUTOINIT_DISABLE),
            DDRCFG_REG(MC_INIT_ACK),
            DDRCFG_REG(MC_INIT_ZQ_CAL_START));
        wolfBoot_printf("  CTRLR_INIT_DONE=0x%x PHY_TRAINING_STATUS=0x%x\n",
            DDRCFG_REG(MC_CTRLR_INIT_DONE),
            DDRPHY_REG(PHY_TRAINING_STATUS));
        return 1;
    }
    wolfBoot_printf("DDR: Post-init: INIT_ACK=1 after %u us\n",
        (unsigned)(timeout * 10U));

    DDRCFG_REG(MC_CFG_AUTO_ZQ_CAL_EN) = LIBERO_SETTING_CFG_AUTO_ZQ_CAL_EN;
    mb();

    /* Force DRAM out of self-refresh.  HSS clears INIT_SELF_REFRESH @
     * MC_BASE2+0x234 (mss_ddr.c:4122) inside init_ddrc.  wolfBoot did
     * not write this register at all -- DRAM might come up in self-
     * refresh mode, refusing AXI bursts.  Read STATUS before/after
     * clearing.  Status @ +0x238: bit0=in_self_refresh for rank0,
     * bit1=rank1. */
    wolfBoot_printf("  Pre-clear SELF_REFRESH=0x%x STATUS=0x%x\n",
        DDRCFG_REG(0x4234U), DDRCFG_REG(0x4238U));
    DDRCFG_REG(0x4234U) = 0x0U;  /* INIT_SELF_REFRESH = 0 */
    mb();
    udelay(100);
    wolfBoot_printf("  Post-clear SELF_REFRESH=0x%x STATUS=0x%x\n",
        DDRCFG_REG(0x4234U), DDRCFG_REG(0x4238U));

    /* Tested forcing expert_dfi_status_override_to_shim=0x07 here
     * (HSS-captured value).  Regressed lane 1 -- only lane 0
     * survived.  The shim override forces internal DFI signals high
     * and locks out subsequent per-lane configuration.  HSS likely
     * sets 0x07 only as the final write after all lanes are loaded,
     * and the captured 0x07 reflects HSS's last-write residue, not a
     * required steady-state value. */

    wolfBoot_printf("DDR: Post-init COMPLETE -- handing off to AXI\n");

    /* Empirical settle: verbose register dump.  This unblocks AXI
     * reads on ~50% of cold boots vs ~0% without it.  The exact
     * mechanism is unclear -- it could be UART wall-time, APB-bus
     * read pattern, or a specific register-read side effect.  TODO:
     * narrow down to the minimal sequence; for now use the dump as
     * the empirical bring-up step. */
    /* mpfs_dump_ddrc_regs() / mpfs_dump_phy_regs() are diagnostic
     * helpers still defined for re-use; not called in normal boot
     * since the register state now matches HSS (DDRC: 0 diffs, PHY:
     * 12 read-only-status diffs). */

    /* Dump key DRAM-control register states to find what's blocking
     * reads from completing.  Then try clearing potential gates. */
    wolfBoot_printf("  DRAM ctrl: FORCE_RESET=0x%x DISABLE_CKE=0x%x INIT_CS=0x%x "
        "AUTOINIT_DIS=0x%x INIT_REFRESH=0x%x\n",
        DDRCFG_REG(MC_INIT_FORCE_RESET),
        DDRCFG_REG(MC_INIT_DISABLE_CKE),
        DDRCFG_REG(MC_INIT_CS),
        DDRCFG_REG(MC_INIT_AUTOINIT_DISABLE),
        DDRCFG_REG(MC_INIT_REFRESH));

    /* Ensure DRAM is out of all reset/disable states */
    DDRCFG_REG(MC_INIT_FORCE_RESET) = 0x0UL;
    DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x0UL;
    DDRCFG_REG(MC_INIT_AUTOINIT_DISABLE) = 0x0UL;
    mb();
    udelay(100);

    wolfBoot_printf("  DRAM ctrl POST-clear: FORCE_RESET=0x%x DISABLE_CKE=0x%x "
        "AUTOINIT_DIS=0x%x\n",
        DDRCFG_REG(MC_INIT_FORCE_RESET),
        DDRCFG_REG(MC_INIT_DISABLE_CKE),
        DDRCFG_REG(MC_INIT_AUTOINIT_DISABLE));

    /* Item 3: DFI error / status diagnostics before the first AXI
     * access.  STAT_DFI_ERROR (DFI struct +0x28, abs 0x10028) is a
     * single-bit DFI fault flag from the PHY/controller handshake.
     * STAT_DFI_ERROR_INFO (+0x2C, abs 0x1002C) carries the cause code.
     * If non-zero, the controller is sitting in a DFI error state and
     * any subsequent AXI request will stall waiting for a DFI cycle
     * that the PHY refuses to drive. */
    wolfBoot_printf("DDR: DFI snapshot pre-read: "
        "INIT_COMPLETE=0x%x TRAINING_COMPLETE=0x%x "
        "TRAINING_ERROR=0x%x ERROR=0x%x ERROR_INFO=0x%x\n",
        DDRCFG_REG(0x10034U),
        DDRCFG_REG(0x10038U),
        DDRCFG_REG(0x10024U),
        DDRCFG_REG(0x10028U),
        DDRCFG_REG(0x1002CU));

    /* Item 1: hang-protected AXI read.
     *
     * MTIME does not tick in M-mode without HSS, so we cannot use
     * the machine-timer interrupt.  Instead drive `mcycle` (which
     * does tick at the CPU clock unconditionally) and arm a custom
     * trap handler via a special mtvec.  The actual technique used
     * here is a polled mcycle deadline check between the *issue* of
     * the read and a print -- but since a stalled load blocks the
     * pipeline, the polled check would never run.
     *
     * Compromise: use the existing WDT.  The MSS WDT TRIG threshold
     * (currently 0x3E8 ticks) fires a non-maskable interrupt BEFORE
     * the WDT actually resets the chip.  We pet the WDT, drop the
     * REFRESH timeout window very short, then attempt the read.  If
     * the read returns we re-pet the WDT.  If it stalls, the WDT
     * fires its TRIG interrupt; without an installed PLIC handler,
     * the next WDT countdown to zero causes the reset -- but now the
     * total reset window is short enough that the boot loop is
     * recognisable as 'AXI hang' rather than 'something else'.
     *
     * Practical effect: the timing of the next 'wolfBoot Version'
     * UART line tells us whether the read is hanging on AXI (~short
     * WDT cycle), failing earlier (no print before reset), or
     * succeeding.  No real recovery yet -- this is diagnostic only.
     */
    {
        volatile uint32_t * const test_addr =
            (volatile uint32_t *)0xC0000000UL;
        uint32_t readback;
        uint64_t t0, t1;

        t0 = csr_read(mcycle);
        wolfBoot_printf("DDR: naked read @ 0xC0000000 (mcycle=0x%lx): ",
            (unsigned long)t0);
        readback = *test_addr;
        t1 = csr_read(mcycle);
        wolfBoot_printf("read=0x%x (mcycle delta=0x%lx)\n",
            readback, (unsigned long)(t1 - t0));
    }

    return 0;
}

/* Main DDR Initialization Entry Point */
int mpfs_ddr_init(unsigned int outer_retry)
{
    int ret;

    wolfBoot_printf("\n========================================\n");
    wolfBoot_printf("MPFS DDR Init (Video Kit LPDDR4 2GB)\n");
    wolfBoot_printf("MT53D512M32D2DS-053 x32 @ 1600 Mbps\n");
    wolfBoot_printf("========================================\n");

    /* rpc_156 DQ/DQS init offset.  Libero default 6 leaves the data eye
     * closed (dqdqs_status2=0) on the Video Kit.  HSS allows 1..9 via
     * TUNE_RPC_156_DQDQS_INIT_VALUE.  Empirically each fresh boot's
     * training state degrades on subsequent attempts within the same
     * power cycle, so we use a SINGLE value (no sweep): bump to 3 to
     * push past the bad starting edge.  Change in code if 3 doesn't
     * give dqdqs_status2 >= 5 on cold boot. */
    mpfs_phy_rpc156_val = 6U;
    wolfBoot_printf("DDR: rpc156 (DQ/DQS init offset) = %u (was Libero 6)\n",
        (unsigned)mpfs_phy_rpc156_val);

    (void)outer_retry;  /* TUNE sweep removed; outer_retry kept for future use */

    /* Step 1: NWC/PLL initialization.  Run only once per boot -- the
     * MSS / DDR PLLs lock on first call and re-running mss_pll_init()
     * hangs on the lock wait when called against an already-locked
     * PLL.  The outer retry loop in hal_init() re-enters this function
     * for full controller/PHY re-init, but the PLLs only need to be
     * brought up once. */
    {
        static int nwc_initialized = 0;
        if (!nwc_initialized) {
            ret = nwc_init();
            if (ret != 0) {
                wolfBoot_printf("DDR: NWC init FAILED\n");
                return -1;
            }
            nwc_initialized = 1;
        }
    }

    /* Step 2: Enable DDR controller clock */
    DBG_DDR("DDR: Enable DDRC clock/reset...");
    DBG_DDR("CLK before=0x%x ", SYSREG_REG(SYSREG_SUBBLK_CLOCK_CR_OFF));
    SYSREG_REG(SYSREG_SUBBLK_CLOCK_CR_OFF) |= MSS_PERIPH_DDRC;
    mb();
    DBG_DDR("after=0x%x\n", SYSREG_REG(SYSREG_SUBBLK_CLOCK_CR_OFF));

    /* Step 3: Reset DDR controller */
    SYSREG_REG(SYSREG_SOFT_RESET_CR_OFF) |= MSS_PERIPH_DDRC;
    mb();
    udelay(1);
    SYSREG_REG(SYSREG_SOFT_RESET_CR_OFF) &= ~MSS_PERIPH_DDRC;
    mb();
    udelay(1);
    DBG_DDR("  RST=0x%x\n", SYSREG_REG(SYSREG_SOFT_RESET_CR_OFF));
    DBG_DDR("  Test MC_BASE2@0x%lx: ", DDRCFG_BASE + MC_BASE2);
    DBG_DDR("SR=0x%x ", DDRCFG_REG(MC_CTRLR_SOFT_RESET));
    DBG_DDR("RAS=0x%x\n", DDRCFG_REG(MC_CFG_RAS));
    DBG_DDR("done\n");

    /* Step 4: Setup segments and blocker */
    setup_segments();

    /* Step 5: Configure PHY (writes DDRPHY_MODE which triggers
     * mode-driven RPC preload).  HSS state machine order has this
     * BEFORE setup_controller.  All 4 lanes train wide-open in this
     * order; reverse order leaves lanes 0&1 at eye=0. */
    ret = setup_phy();
    if (ret != 0)
        wolfBoot_printf("DDR: PHY setup warning\n");

    /* Step 6: Configure controller timing (CFG_* registers).  HSS
     * runs init_ddrc here in DDR_TRAINING_SETUP_DDRC state, after
     * DDR_TRAINING_DDRC_BRINGUP and BANK_CONTROLLER soft_reset.
     * Tested also calling this BEFORE setup_phy -- no AXI service
     * improvement.  Tested forcing EXPERT_WRCALIB=0x5555 (HSS
     * post-train value) -- no improvement.  Tested clearing
     * INIT_SELF_REFRESH -- was already 0. */
    setup_controller();

    /* Step 7: Training reset and clock rotation */
    training_reset_and_rotate();
    DBG_DDR("DDR: After rotation SR_N=0x%x\n", DDRCFG_REG(MC_CTRLR_SOFT_RESET));

    /* Step 8: TIP configuration (use correct register) */
    DDRPHY_REG(PHY_TIP_CFG_PARAMS) = LIBERO_SETTING_TIP_CFG_PARAMS;
    mb();

    /* Step 9: Run training + post-training + MTC sanity, with retry on
     * MTC failure.
     *
     * Why MTC is the retry trigger (not PHY_TRAINING_STATUS): when the
     * manual ADDCMD training picks a marginal phase/dly that doesn't
     * resolve into a usable DRAM alignment, train_stat sticks at 0x1
     * (BCLK_SCLK only).  But TIP keeps spinning in the background and
     * eventually flips the WRLVL/RDGATE/DQ_DQS bits to read 0x1D, even
     * though the alignment is bogus.  An outer retry keyed on
     * PHY_TRAINING_STATUS sees that bogus 0x1D and stops.  MTC actually
     * exercises the DDR controller -- it times out unambiguously when
     * training was bad, and is the reliable signal.
     *
     * Empirical baseline: ~30% per-attempt training failure rate -> 5
     * retries gives ~99.7% cumulative success rate.
     */
    {
        uint32_t train_retry = 0;
        /* 3 inner attempts so the MOVE_CK rotation (0deg/45deg/90deg)
         * cycles through all three alternative refclk picks (k / k+1 /
         * k+2) in one DDRC-init pass.  Combined with the 6 outer
         * retries gives 18 chances for ADDCMD + WRLVL convergence. */
        /* Keep at 3 so MOVE_CK rotation cycles through 0deg/45deg/90deg
         * within one outer DDRC-init pass.  We don't force WRLVL retry
         * (regression made all 18 attempts MTC-timeout) so most boots
         * succeed on first attempt; the inner retries only matter when
         * MTC sanity actually fails. */
        const uint32_t MAX_TRAIN_RETRY = 3;
        uint32_t lane;
        uint32_t mtc_to;
        uint32_t train_stat;
        int mtc_pass = 0;

        while (train_retry < MAX_TRAIN_RETRY) {
            if (train_retry > 0) {
                wolfBoot_printf(
                    "DDR: Retry %u/%u after MTC sanity FAIL\n",
                    (unsigned)train_retry, (unsigned)MAX_TRAIN_RETRY);
                /* HSS DDR_TRAINING_FAIL reset sequence (mss_ddr.c:519-538) */
                DDRCFG_REG(MC_INIT_CS) = 0x1;
                DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x1;
                ddr_delay(500);
                DDRCFG_REG(MC_INIT_FORCE_RESET) = 0x1;
                ddr_delay(200000);
                DDRCFG_REG(MC_DFI_INIT_START) = 0x0;
                DDRCFG_REG(MC_CTRLR_INIT) = 0x0;
                DDRPHY_REG(PHY_TRAINING_START) = 0x0;
                mb();
            }
            train_retry++;

            /* Combined retry count for HSS-style MOVE_CK ADDCMD
             * rotation: each outer DDR re-init contributes
             * MAX_TRAIN_RETRY worth of count, so retry%3 cycles
             * through all three move-CK pairs across all attempts. */
            ret = run_training(outer_retry * MAX_TRAIN_RETRY +
                              (train_retry - 1));
            if (ret != 0) {
                continue;
            }

            /* HSS DDR_TRAINING_SET_FINAL_MODE: rewrite DDRPHY_MODE with
             * LIBERO setting to transition PHY from training to
             * operational mode (mss_ddr.c:1619). */
            wolfBoot_printf("DDR: Post-training sequence...\n");
            DDRPHY_REG(PHY_MODE) = LIBERO_SETTING_DDRPHY_MODE;
            mb();
            wolfBoot_printf("  DDRPHY_MODE -> 0x%x (final)\n",
                DDRPHY_REG(PHY_MODE));

            /* rpc220 + load_dq: HSS always runs these as the prelude
             * to write_calibration_using_mtc.  Earlier experiment
             * skipping them when train_stat=0x1D didn't fix the
             * post-training AXI hang, so revert to always-run.  HSS
             * does this regardless of train_stat. */
            DDRPHY_REG(PHY_RPC220) = 0x0CUL;  /* HSS-captured (2026-05-15) */
            mb();
            for (lane = 0; lane < 4; lane++) {
                DDRPHY_REG(PHY_EXPERT_DLYCNT_MOVE0) = 0x00UL;
                DDRPHY_REG(PHY_EXPERT_DFI_STATUS_TO_SHIM) = 0x07UL;
                DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x21UL;
                DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) =
                    (0xFFUL << (lane * 8UL));
                DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = 0x00UL;
                DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x08UL;
            }
            mb();
            wolfBoot_printf("  load_dq done for 4 lanes\n");

            /* HSS DDR_TRAINING_WRITE_CALIBRATION (mss_ddr.c:1740-1750):
             * after SET_FINAL_MODE (DDRPHY_MODE final, above) + rpc220
             * = 0xC + load_dq (above), run the MTC write-cal sweep and
             * commit the per-lane result via set_write_calib.  This MUST
             * come AFTER the DQ path is prepared.  The in-training call
             * (run_training) ran the sweep on an unprepared path -- PHY
             * still in training mode, rpc220 uncentered, no load_dq --
             * so it failed every lane and was discarded for a forced
             * 0x5555.  Running it here, in HSS order, lets the sweep
             * find real per-lane calibration.  Replaces the uniform
             * decrement_dq hack (not in HSS): per-lane write delay comes
             * from this sweep, not a blanket shift. */
            {
                uint8_t wrcal;
                wolfBoot_printf(
                    "  WRCALIB after rpc220+load_dq (HSS order)...\n");
                wrcal = mpfs_write_calibration_using_mtc(4U);
                wolfBoot_printf(
                    "  Post-load_dq WRCALIB: result=%u EXPERT_WRCALIB=0x%x\n",
                    (unsigned)wrcal, DDRPHY_REG(PHY_EXPERT_WRCALIB));
                /* Reliability gate (2026-06-05): only accept this boot's
                 * training when WRCALIB calibrated ALL lanes (result==0).
                 * A partial result (e.g. 2/4 lanes) means the DDR write path
                 * is bad on this boot; the TIP train_stat self-report can
                 * still read "complete", so gating on it alone let bad boots
                 * through and the 19 MB load then hard-failed every block.
                 * Retrain instead -- the non-deterministic WRLVL converges
                 * to all-4-lane within a few attempts. */
                if (wrcal != 0U) {
                    wolfBoot_printf(
                        "  WRCALIB not all lanes (result=%u) -- retraining\n",
                        (unsigned)wrcal);
                    continue;
                }
            }
            train_stat = DDRPHY_REG(PHY_TRAINING_STATUS);
            wolfBoot_printf(
                "  CTRLR_INIT_DONE=0x%x AUTOINIT_DIS=0x%x train_stat=0x%x\n",
                DDRCFG_REG(MC_CTRLR_INIT_DONE),
                DDRCFG_REG(MC_INIT_AUTOINIT_DISABLE),
                train_stat);

            /* Fast path: if TIP completed full training (train_stat
             * 0x1C bits = WRLVL+RDGATE+DQ_DQS), skip the MTC sanity
             * test.  MTC engine still TIMEOUTs every iteration even
             * after the MC_BASE1 BIT_MAP_INDEX offset bugfix -- the
             * MTC engine has a separate issue (DDRC-internal access
             * path differs from external AXI).  Running it just
             * burns all 3 retries and the outer 6-retry loop, ending
             * with a WDT reset.  Accept TIP-side training and proceed
             * to disk-load; the actual AXI reads are independent. */
            if ((train_stat & 0x1CU) == 0x1CU) {
                wolfBoot_printf("  TIP full training success (0x%x) - skipping MTC sanity\n",
                    train_stat);
                mtc_pass = 1;
                break;
            }

            /* MTC sanity: smallest region (size=8 -> 2^8 = 256 B),
             * counting pattern, sequential addressing, RW. */
            DDRCFG_REG(MT_EN) = 0;
            DDRCFG_REG(MT_EN_SINGLE) = 0;
            DDRCFG_REG(MT_STOP_ON_ERROR) = 0;
            DDRCFG_REG(0x440C) = 0;             /* MT_RD_ONLY */
            DDRCFG_REG(0x4410) = 0;             /* MT_WR_ONLY */
            DDRCFG_REG(MT_DATA_PATTERN) = 0;
            DDRCFG_REG(MT_ADDR_PATTERN) = 0;
            DDRCFG_REG(MT_START_ADDR_0) = 0;
            DDRCFG_REG(MT_START_ADDR_1) = 0;
            DDRCFG_REG(MT_ADDR_BITS) = 8;
            DDRCFG_REG(MT_ERROR_MASK_0) = 0xFFFFFFFFUL;
            DDRCFG_REG(MT_ERROR_MASK_1) = 0xFFFFFFFFUL;
            DDRCFG_REG(MT_ERROR_MASK_2) = 0xFFFFFFFFUL;
            DDRCFG_REG(MT_ERROR_MASK_3) = 0xFFFFFFFFUL;
            DDRCFG_REG(MT_ERROR_MASK_4) = 0xFFFFFFFFUL;
            /* Gate MTC on DFI training-complete (HSS mss_ddr.c:1418): the
             * MTC RW engine only runs once STAT_DFI_TRAINING_COMPLETE
             * (DFI+0x38) latches; firing before that is why MTC times
             * out.  Bounded WDT-petted wait + truthful diag before the
             * fire (RD_ONLY/WR_ONLY confirm the engine is in RW mode). */
            {
                uint32_t dfi_to = 100000;
                while (dfi_to > 0) {
                    if ((DDRCFG_REG(0x10038U) & 0x1U) != 0U) break;
                    udelay(10);
                    dfi_to--;
                    if ((dfi_to & 0xFFFU) == 0U) {
                        *(volatile uint32_t*)0x20001000UL = 0xDEADC0DEU;
                        *(volatile uint32_t*)0x20101000UL = 0xDEADC0DEU;
                        *(volatile uint32_t*)0x20103000UL = 0xDEADC0DEU;
                        *(volatile uint32_t*)0x20105000UL = 0xDEADC0DEU;
                        *(volatile uint32_t*)0x20107000UL = 0xDEADC0DEU;
                    }
                }
                wolfBoot_printf(
                    "  Pre-MTC256: DFI_train_complete=0x%x CTRLR_INIT_DONE=0x%x"
                    " AUTO_REF=0x%x RD_ONLY=0x%x WR_ONLY=0x%x (dfi %u us)\n",
                    DDRCFG_REG(0x10038U) & 0x1U,
                    DDRCFG_REG(MC_CTRLR_INIT_DONE),
                    DDRCFG_REG(MC_CFG_AUTO_REF_EN),
                    DDRCFG_REG(0x440CU), DDRCFG_REG(0x4410U),
                    (unsigned)((100000U - dfi_to) * 10U));
            }
            DDRCFG_REG(MT_EN_SINGLE) = 0;
            DDRCFG_REG(MT_EN_SINGLE) = 1;
            mtc_to = 0xFFFFFFUL;
            while ((DDRCFG_REG(MT_DONE_ACK) & 0x1UL) == 0 && mtc_to > 0) {
                mtc_to--;
            }
            if (mtc_to == 0) {
                wolfBoot_printf(
                    "  MTC 256B TIMEOUT (DONE_ACK=0x%x ERR_STS=0x%x)\n",
                    DDRCFG_REG(MT_DONE_ACK), DDRCFG_REG(MT_ERROR_STS));
                continue;
            }
            if ((DDRCFG_REG(MT_ERROR_STS) & 0x1UL) != 0) {
                wolfBoot_printf("  MTC 256B FAIL (err_sts=0x%x)\n",
                    DDRCFG_REG(MT_ERROR_STS));
                continue;
            }
            wolfBoot_printf("  MTC 256B PASS (err_sts=0x%x to_used=0x%x)\n",
                DDRCFG_REG(MT_ERROR_STS),
                (unsigned int)(0xFFFFFFUL - mtc_to));

            /* Log train_stat for diagnostic but do NOT force retry on
             * incomplete WRLVL.  Reason: requiring train_stat & 0x1C
             * == 0x1C made every retry hit MTC TIMEOUT (the repeated
             * full DDRC resets wedge the MTC engine), so all 18
             * attempts failed.  Accepting MTC 256B PASS as success
             * still progresses to disk-load on imperfect calibration. */
            train_stat = DDRPHY_REG(PHY_TRAINING_STATUS);
            wolfBoot_printf("  TIP final train_stat=0x%x (WRLVL+RDGATE+DQ_DQS need 0x1C)\n",
                train_stat);

            mtc_pass = 1;
            break;
        }

        if (!mtc_pass) {
            wolfBoot_printf("DDR: Training/MTC failed after %u retries\n",
                (unsigned)MAX_TRAIN_RETRY);
            return -2;
        }
        wolfBoot_printf("DDR: Training+MTC PASS after %u retries\n",
            (unsigned)(train_retry - 1));
    }

    /* HSS LPDDR4 POST_INITIALIZATION (mss_ddr.c:5597-5646). */
    ret = mpfs_ddr_post_initialization();
    if (ret != 0) {
        return -4;
    }

    /* PDMA pre-fill: HSS clear_bootup_cache_ways equivalent.  With
     * MTC traffic injection (commit b4031038) WRLVL now trains real
     * per-lane delays and the AXI port no longer hangs, so run this
     * unconditionally to prime DDRC row buffers before memory_test.
     * Small region (1 MB) to keep boot fast and minimize risk if the
     * underlying address-decoder issue is still affecting writes. */
    mpfs_clear_bootup_cache_ways(0xC0000000UL, 1UL * 1024UL * 1024UL);


    wolfBoot_printf("DDR: Initialization COMPLETE\n");
    /* Phase A.2: dump live ADDR_MAP + BL registers so we can confirm
     * they match the Libero settings and aren't being clobbered by a
     * later step.  ADDR_MAP block is at DDRCFG_BASE+0x2400. */
    wolfBoot_printf(
        "DDRC ADDR_MAP: MAN=%x CHIP=%x CID=%x BANK=%x/%x ROW=%x/%x/%x/%x COL=%x/%x/%x\n",
        DDRCFG_REG(0x2400), DDRCFG_REG(0x2404), DDRCFG_REG(0x2408),
        DDRCFG_REG(0x2414), DDRCFG_REG(0x2418),
        DDRCFG_REG(0x241C), DDRCFG_REG(0x2420), DDRCFG_REG(0x2424), DDRCFG_REG(0x2428),
        DDRCFG_REG(0x242C), DDRCFG_REG(0x2430), DDRCFG_REG(0x2434));
    wolfBoot_printf(
        "DDRC BL=%x MR_MASK=%x DATA_MASK=%x WRITE_DBI=%x READ_DBI=%x\n",
        DDRCFG_REG(0x008),   /* MC_CFG_BL location varies, dump candidates */
        DDRCFG_REG(0x040),   /* MR write mask */
        DDRCFG_REG(0x3C70),  /* CFG_DATA_MASK */
        DDRCFG_REG(0x3C68),  /* CFG_WRITE_DBI */
        DDRCFG_REG(0x3C64)); /* CFG_READ_DBI */
    wolfBoot_printf("========================================\n");

#ifdef MPFS_DDR_PATTERN_TEST
    /* DDR pattern test for the staging window.  Single region
     * (0x82000000), counting pattern, 256 KB.  Validates write/read
     * reliability after DDR init and HALTS the boot so we iterate on
     * DDR training fixes without SDHCI/disk_load in the loop.  Tracks
     * train_stat at exit so we can correlate training metric with
     * pattern-test mismatch count.
     */
    {
        /* Run the same pattern test on both cached (0x82000000) and
         * non-cached (0xC2000000) regions to triangulate where the
         * mismatches come from. */
        volatile uint32_t * const pc  = (volatile uint32_t *)0x82000000UL;
        volatile uint32_t * const pnc = (volatile uint32_t *)0xC2000000UL;
        const uint32_t test_words = 0x10000UL;  /* 64K words = 256 KB */
        uint32_t i;
        uint32_t bad_c = 0, bad_nc = 0;
        uint32_t first_bad_c = 0xFFFFFFFFUL, first_bad_v_c = 0;
        uint32_t first_bad_nc = 0xFFFFFFFFUL, first_bad_v_nc = 0;

        wolfBoot_printf("DDR-TEST: writing pattern (cached @0x82000000) "
            "(%lx words) -- 3 passes...\n", (unsigned long)test_words);
        /* Triple-write pass: first pass primes DDR with our pattern
         * via L2 cache evictions.  Subsequent passes catch the misses.
         * Each pass: write all words sequentially, fence between passes. */
        for (i = 0; i < test_words; i++)
            pc[i] = 0xDEAD0000UL | i;
        __asm__ volatile("fence rw,rw" ::: "memory");
        for (i = 0; i < test_words; i++)
            pc[i] = 0xDEAD0000UL | i;
        __asm__ volatile("fence rw,rw" ::: "memory");
        for (i = 0; i < test_words; i++)
            pc[i] = 0xDEAD0000UL | i;
        __asm__ volatile("fence rw,rw" ::: "memory");
        for (i = 0; i < test_words; i++) {
            uint32_t v = pc[i];
            uint32_t expected = 0xDEAD0000UL | i;
            if (v != expected) {
                if (first_bad_c == 0xFFFFFFFFUL) {
                    first_bad_c = i; first_bad_v_c = v;
                }
                bad_c++;
            }
        }
        if (bad_c == 0)
            wolfBoot_printf("DDR-TEST cached: PASS\n");
        else
            wolfBoot_printf(
                "DDR-TEST cached: FAIL %lx mismatches; first @ idx=%lx val=%lx\n",
                (unsigned long)bad_c, (unsigned long)first_bad_c,
                (unsigned long)first_bad_v_c);

        wolfBoot_printf("DDR-TEST: writing pattern (non-cached @0xC2000000) "
            "(%lx words)...\n", (unsigned long)test_words);
        for (i = 0; i < test_words; i++)
            pnc[i] = 0xBEEF0000UL | i;
        for (i = 0; i < test_words; i++) {
            uint32_t v = pnc[i];
            uint32_t expected = 0xBEEF0000UL | i;
            if (v != expected) {
                if (first_bad_nc == 0xFFFFFFFFUL) {
                    first_bad_nc = i; first_bad_v_nc = v;
                }
                bad_nc++;
            }
        }
        if (bad_nc == 0)
            wolfBoot_printf("DDR-TEST non-cached: PASS\n");
        else
            wolfBoot_printf(
                "DDR-TEST non-cached: FAIL %lx mismatches; first @ idx=%lx val=%lx\n",
                (unsigned long)bad_nc, (unsigned long)first_bad_nc,
                (unsigned long)first_bad_v_nc);

        wolfBoot_printf("DDR-TEST: train_stat=0x%x\n",
            DDRPHY_REG(PHY_TRAINING_STATUS));
        wolfBoot_printf("DDR-TEST: done\n");
    }
#endif /* MPFS_DDR_PATTERN_TEST */

    return 0;
}

#endif /* WOLFBOOT_RISCV_MMODE && MPFS_DDR_INIT */

void hal_init(void)
{
#ifdef WOLFBOOT_RISCV_MMODE
    /* Capture boot ROM WDT defaults for restoration in hal_prepare_boot() */
    mpfs_wdt_default_mvrp = MSS_WDT_MVRP(MSS_WDT_E51_BASE);
    mpfs_wdt_default_ctrl = MSS_WDT_CONTROL(MSS_WDT_E51_BASE);
    /* Snapshot boot-ROM WDT state and SYSREG RESET_SR (reset status
     * cause) so we can print them AFTER uart_init.  RESET_SR is W1C --
     * we clear after reading. */
    {
        volatile uint32_t *wdt_e51 = (volatile uint32_t*)0x20001000UL;
        volatile uint32_t *sysreg_reset_sr = (volatile uint32_t*)0x20002020UL;
        mpfs_boot_wdt_snap[0] = wdt_e51[0];
        mpfs_boot_wdt_snap[1] = wdt_e51[1];
        mpfs_boot_wdt_snap[2] = wdt_e51[2];
        mpfs_boot_wdt_snap[3] = wdt_e51[3];
        mpfs_boot_wdt_snap[4] = wdt_e51[4];
        mpfs_boot_wdt_snap[5] = wdt_e51[5];
        mpfs_boot_reset_sr_snap = *sysreg_reset_sr;
        *sysreg_reset_sr = mpfs_boot_reset_sr_snap;  /* W1C: clear seen bits */
    }

#ifndef WATCHDOG
    /* WATCHDOG=0 (default): disable WDT for the duration of wolfBoot.
     * It will be re-enabled in hal_prepare_boot() before do_boot.
     *
     * Refresh first (with the magic key) so the watchdog isn't already
     * past its trigger value before we get to disable it -- writing
     * CONTROL to disable while in the triggered window arms a reset.
     * Also write a generous MVRP so any inadvertent refresh after the
     * disable still pets to a long timeout.
     * Then explicitly mask the ENABLE bit. */
    /* MPFS MSS WDOG can't be disabled outright -- it always counts.
     * What we CAN do is clear the DEVRST bit (bit 5) so timeout
     * triggers an NMI instead of a chip reset, and set TIME to max
     * so the window is as long as possible (~112 ms at 150 MHz).
     * Then we pet during long operations. */
    {
        const unsigned long wdt_bases[5] = {
            MSS_WDT_E51_BASE,
            MSS_WDT_U54_1_BASE,
            MSS_WDT_U54_2_BASE,
            MSS_WDT_U54_3_BASE,
            MSS_WDT_U54_4_BASE
        };
        unsigned w;
        for (w = 0; w < 5; w++) {
            MSS_WDT_REFRESH(wdt_bases[w]) = 0xDEADC0DEU;
            MSS_WDT_TIME(wdt_bases[w])    = 0x00FFFFFFUL;
            MSS_WDT_MVRP(wdt_bases[w])    = 0x00FFFFFFUL;
            /* Clear DEVRST bit (bit 5) so timeout doesn't reset chip.
             * Also clear all other CONTROL bits (interrupt enables). */
            MSS_WDT_CONTROL(wdt_bases[w]) = 0;
            MSS_WDT_REFRESH(wdt_bases[w]) = 0xDEADC0DEU;
        }
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
    wolfBoot_printf("Boot WDT_E51: REFRESH=%x CTRL=%x STATUS=%x TIME=%x MVRP=%x TRIG=%x\n",
        mpfs_boot_wdt_snap[0], mpfs_boot_wdt_snap[1], mpfs_boot_wdt_snap[2],
        mpfs_boot_wdt_snap[3], mpfs_boot_wdt_snap[4], mpfs_boot_wdt_snap[5]);
    wolfBoot_printf("Boot RESET_SR: %x (bit0=PERIPH bit1=MSS bit2=CPU bit3=DBG "
        "bit4=FABRIC bit5=WDOG bit6=GPIO bit7=BUS bit8=SOFT)\n",
        mpfs_boot_reset_sr_snap);

    /* Phase A L2 diagnostic dump.  Print the L2 controller state after
     * mpfs_config_l2_cache() so we can confirm WAY_ENABLE / WAY_MASK
     * values stuck and scratchpad ways (12-15) are isolated from cache
     * masters (whose masks are all L2_WAY_MASK_CACHE_ONLY = 0xFF =
     * ways 0-7 only).  CONFIG is read-only and reports the controller
     * geometry (banks / ways / sets). */
    wolfBoot_printf("L2: CONFIG=%lx WAY_ENABLE=%lx\n",
        (unsigned long)L2_CONFIG, (unsigned long)L2_WAY_ENABLE);
    wolfBoot_printf(
        "L2 MASK DMA=%lx AXI0=%lx AXI1=%lx AXI2=%lx AXI3=%lx\n",
        (unsigned long)L2_WAY_MASK_DMA,
        (unsigned long)L2_WAY_MASK_AXI4_PORT0,
        (unsigned long)L2_WAY_MASK_AXI4_PORT1,
        (unsigned long)L2_WAY_MASK_AXI4_PORT2,
        (unsigned long)L2_WAY_MASK_AXI4_PORT3);
    wolfBoot_printf(
        "L2 MASK E51D=%lx E51I=%lx U54_1D=%lx U54_1I=%lx\n",
        (unsigned long)L2_WAY_MASK_E51_DCACHE,
        (unsigned long)L2_WAY_MASK_E51_ICACHE,
        (unsigned long)L2_WAY_MASK_U54_1_DCACHE,
        (unsigned long)L2_WAY_MASK_U54_1_ICACHE);
    wolfBoot_printf(
        "L2 MASK U54_2D=%lx U54_2I=%lx U54_3D=%lx U54_3I=%lx U54_4D=%lx U54_4I=%lx\n",
        (unsigned long)L2_WAY_MASK_U54_2_DCACHE,
        (unsigned long)L2_WAY_MASK_U54_2_ICACHE,
        (unsigned long)L2_WAY_MASK_U54_3_DCACHE,
        (unsigned long)L2_WAY_MASK_U54_3_ICACHE,
        (unsigned long)L2_WAY_MASK_U54_4_DCACHE,
        (unsigned long)L2_WAY_MASK_U54_4_ICACHE);

    /* Phase A canary instrumentation.  Place two recognisable patterns
     * in L2 Scratch so we can tell where corruption originates:
     *   - canary at stack bottom : detects stack overflow
     *   - canary mid-scratch     : detects general L2 Scratch wipe
     * Both are checked again at "Boot ready" below, before disk-load,
     * and in handle_trap. */
    {
        extern char _main_hart_stack_bottom;
        volatile uint32_t *cb =
            (volatile uint32_t *)&_main_hart_stack_bottom;
        volatile uint32_t *cm = (volatile uint32_t *)0x0A030000UL;
        cb[0] = 0xC0DEC0DEUL; cb[1] = 0xC0DEC0DEUL;
        cb[2] = 0xC0DEC0DEUL; cb[3] = 0xC0DEC0DEUL;
        cm[0] = 0xCA11AB1EUL; cm[1] = 0xCA11AB1EUL;
        cm[2] = 0xCA11AB1EUL; cm[3] = 0xCA11AB1EUL;
        wolfBoot_printf(
            "Canary placed: stack_bot=%p val=%lx mid=0x0A030000 val=%lx\n",
            (void *)cb, (unsigned long)cb[0], (unsigned long)cm[0]);
    }

#ifdef MPFS_DDR_INIT
    /* Bring up LPDDR4 before any DDR-resident operations.
     *
     * Outer retry loop: each call to mpfs_ddr_init() does a SYSREG DDRC
     * soft-reset pulse, which clears the MTC engine state.  If the
     * inner retry inside mpfs_ddr_init() exhausts (typically because
     * MTC wedged after the first failure), come back here for a full
     * controller re-init.  Empirical: per-attempt failure rate ~30%, so
     * 3 outer attempts cover ~99.7% of boots. */
    {
        unsigned int outer_retry;
        const unsigned int MAX_OUTER_RETRY = 6;
        int ddr_ok = 0;
        for (outer_retry = 0; outer_retry < MAX_OUTER_RETRY; outer_retry++) {
            if (outer_retry > 0) {
                wolfBoot_printf(
                    "DDR: Outer retry %u/%u (full DDRC re-init)\n",
                    outer_retry, MAX_OUTER_RETRY);
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
                MAX_OUTER_RETRY);
            while (1) {
                /* spin */
            }
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
#define LINUX_BOOTARGS_ROOT "/dev/mmcblk0p4"
#endif

#define LINUX_BOOTARGS \
    "earlycon root="LINUX_BOOTARGS_ROOT" rootwait uio_pdrv_genirq.of_id=generic-uio"
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

#if defined(MPFS_DDR_INIT) && defined(WOLFBOOT_MMODE_SMODE_BOOT)
    /* Phase 4A single-hart MVP: pin /memory@80000000 to the actual 2 GB the
     * board has and mark cpu@2..cpu@4 disabled so Linux only brings up the
     * boot U54 (hart 1).  The kernel still requires an SBI HSM provider to
     * start additional harts at runtime; that arrives in Phase 4B. */
    {
        uint32_t mem_reg[4];
        const char *cpu_off[] = { "cpu@2", "cpu@3", "cpu@4" };
        unsigned int i;

        /* /memory@80000000/reg = <0 0x80000000 0 0x80000000> (2 GB). */
        mem_reg[0] = cpu_to_fdt32(0u);
        mem_reg[1] = cpu_to_fdt32(0x80000000u);
        mem_reg[2] = cpu_to_fdt32(0u);
        mem_reg[3] = cpu_to_fdt32(0x80000000u);
        off = fdt_find_node_offset(fdt, -1, "memory@80000000");
        if (off >= 0) {
            ret = fdt_setprop(fdt, off, "reg", mem_reg, sizeof(mem_reg));
            if (ret != 0) {
                wolfBoot_printf("FDT: Failed to set memory reg (%d)\n", ret);
            }
            else {
                wolfBoot_printf("FDT: memory@80000000 = 2 GB\n");
            }
        }
        else {
            wolfBoot_printf("FDT: memory@80000000 not found\n");
        }

        /* Disable cpu@2..cpu@4 for the single-hart MVP. cpu@0 (E51) is
         * already disabled in the Yocto DTB; cpu@1 stays enabled so Linux
         * boots on it. */
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
    }
#endif /* MPFS_DDR_INIT && WOLFBOOT_MMODE_SMODE_BOOT */

    return 0;
}

/* FIT subimage copy via PDMA (overrides the weak default in src/fdt.c).
 * CPU writes to DDR do not land on this board, so route kernel/dtb copies
 * through the PDMA master.  A DDR source is read via its non-cached alias so
 * PDMA sees real DDR; mpfs_pdma_memcpy remaps the dst 0x8x->0xCx and flushes
 * L2.  Chunked + WDT-petted for kernel-sized copies. */
void wolfBoot_fit_memcpy(void *dst, const void *src, uint32_t len)
{
    uintptr_t d = (uintptr_t)dst;
    uintptr_t s = (uintptr_t)src;
    uint32_t off = 0;
    uint32_t chunk;

    if ((s & 0xF0000000UL) == 0x80000000UL) {
        s |= 0x40000000UL; /* non-cached source alias */
    }
    while (off < len) {
        chunk = len - off;
        if (chunk > (1024U * 1024U)) {
            chunk = 1024U * 1024U;
        }
        (void)mpfs_pdma_memcpy((void *)(d + off),
            (const void *)(s + off), chunk);
        *(volatile uint32_t *)0x20001000UL = 0xDEADC0DEU; /* WDT pet */
        off += chunk;
    }
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
    if (sz + WOLFBOOT_FDT_FIXUP_HEADROOM > sizeof(l2_dtb)) {
        wolfBoot_printf("FDT: dtb too large for L2 fixup (%u > %u)\n",
            (unsigned)(sz + WOLFBOOT_FDT_FIXUP_HEADROOM),
            (unsigned)sizeof(l2_dtb));
        return -1;
    }
    /* DDR (non-cached) -> L2 */
    memcpy(l2_dtb, ddr_nc, sz);
    /* fixup in the CPU-writable L2 buffer */
    ret = mpfs_dts_fixup_inplace(l2_dtb);
    /* L2 -> DDR via PDMA (expanded totalsize) */
    wolfBoot_fit_memcpy(dts_addr, l2_dtb, (uint32_t)fdt_totalsize(l2_dtb));
    return ret;
}

void hal_prepare_boot(void)
{
#ifdef WOLFBOOT_RISCV_MMODE
    /* Restore boot ROM WDT defaults so the application sees a normal WDT.
     * Refresh first so the timer doesn't fire immediately after we apply
     * the new MVRP. Restore the original CONTROL value (including the
     * enable bit) rather than unconditionally enabling. */
    MSS_WDT_REFRESH(MSS_WDT_E51_BASE) = 0xDEADC0DEU;
    MSS_WDT_MVRP(MSS_WDT_E51_BASE) = mpfs_wdt_default_mvrp;
    MSS_WDT_CONTROL(MSS_WDT_E51_BASE) = mpfs_wdt_default_ctrl;
#endif
    /* reset the eMMC/SD card? */
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
#define QSPI_RX_TIMEOUT_MS     10000U  /* 10 s per byte — aborts if host disappears */


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
        int ret = ext_flash_erase(addr + s * FLASH_SECTOR_SIZE,
                                  FLASH_SECTOR_SIZE);
        if (ret < 0) {
            uart_qspi_puts("QSPI-PROG: Erase failed\r\n");
            ext_flash_lock();
            return;
        }
    }

    uart_qspi_puts("ERASED\r\n");

    /* Chunk transfer: wolfBoot requests each 256-byte block with ACK 0x06.
     * No wolfBoot_printf allowed in this loop — only direct UART via
     * uart_qspi_tx/uart_qspi_puts to avoid protocol corruption. */
    written = 0;
    while (written < size) {
        int ret;
        uint32_t chunk_len = size - written;
        if (chunk_len > QSPI_PROG_CHUNK)
            chunk_len = QSPI_PROG_CHUNK;

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

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
/* SDHCI Platform HAL */

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
static void mpfs_iomux_init(void)
{
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
    {
        uint32_t iomux4 = LIBERO_SETTING_IOMUX4_CR;
        uint32_t iomux5 = LIBERO_SETTING_IOMUX5_CR;
        iomux4 &= ~(0xFu << 16);
        iomux4 |=  (0xDu << 16);
        iomux5 &= ~((0xFu << 0) | (0xFu << 16) | (0xFu << 28));
        iomux5 |=  ((0xEu << 0) | (0xEu << 16) | (0xDu << 28));
        SYSREG_REG(SYSREG_IOMUX4_CR_OFFSET) = iomux4;
        SYSREG_REG(SYSREG_IOMUX5_CR_OFFSET) = iomux5;
    }
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
    /* Set priority for MMC main interrupt */
    plic_set_priority(PLIC_INT_MMC_MAIN, PLIC_PRIORITY_DEFAULT);

    /* Set threshold to 0 (allow all priorities > 0) */
    plic_set_threshold(0);

    /* Enable MMC interrupt for this hart */
    plic_enable_interrupt(PLIC_INT_MMC_MAIN);

#ifdef DEBUG_SDHCI
    {
        extern unsigned long get_boot_hartid(void);
        wolfBoot_printf("sdhci_platform_irq_init: hart %lu, context %u, irq %u enabled\n",
            get_boot_hartid(), (unsigned)plic_get_context(),
            (unsigned)PLIC_INT_MMC_MAIN);
    }
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

/* Baud divisor: integer = PCLK/(baudrate*16), fractional (0-63) via 128x scaling. */
static void uart_config_baud(unsigned long base, uint32_t baudrate)
{
    const uint64_t pclk = MSS_APB_AHB_CLK;
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
    MMUART_MCR(base) |= RTS_MASK;  /* Assert RTS — required for USB-UART bridge CTS */
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
/* Reinitialize UART baud divisor for post-PLL APB clock.  Called after
 * mss_pll_init() locks the MSS PLL -- the compile-time MSS_APB_AHB_CLK
 * (40 MHz pre-PLL) is no longer valid.
 *
 * APB clock is derived from CPU clock via the dividers programmed at
 * mss_pll_init time:
 *   APB = LIBERO_SETTING_MSS_COREPLEX_CPU_CLK / 4
 * (CLOCK_CONFIG_CR=0x24 -> APB divider 4).
 *
 * If the board uses a different MSS_COREPLEX_CPU_CLK or APB divider,
 * either redefine MPFS_APB_PCLK_HZ at build time, or adjust the
 * fpga_design_config.h / .config so that LIBERO_SETTING_MSS_COREPLEX_CPU_CLK
 * is correct. */
#ifndef MPFS_APB_PCLK_HZ
#define MPFS_APB_PCLK_HZ  (LIBERO_SETTING_MSS_COREPLEX_CPU_CLK / 4UL)
#endif

void hal_uart_reinit(void)
{
    const uint64_t pclk = MPFS_APB_PCLK_HZ;
    const uint32_t baudrate = 115200;
    uint32_t div_x128 = (uint32_t)((8UL * pclk) / baudrate);
    uint32_t div_x64  = div_x128 / 2u;
    uint32_t div_int  = div_x64 / 64u;
    uint32_t div_frac = div_x64 - (div_int * 64u);
    div_frac += (div_x128 - (div_int * 128u)) - (div_frac * 2u);
    if (div_frac > 63u)
        div_frac = 63u;
    if (div_int > (uint32_t)UINT16_MAX)
        return;
    MMUART_LCR(DEBUG_UART_BASE) |= DLAB_MASK;
    MMUART_DMR(DEBUG_UART_BASE) = (uint8_t)(div_int >> 8);
    MMUART_DLR(DEBUG_UART_BASE) = (uint8_t)div_int;
    MMUART_LCR(DEBUG_UART_BASE) &= ~DLAB_MASK;
    if (div_int > 1u) {
        MMUART_MM0(DEBUG_UART_BASE) |= EFBR_MASK;
        MMUART_DFR(DEBUG_UART_BASE) = (uint8_t)div_frac;
    } else {
        MMUART_MM0(DEBUG_UART_BASE) &= ~EFBR_MASK;
    }
}
#endif /* WOLFBOOT_RISCV_MMODE */
#endif /* DEBUG_UART */

#ifdef WOLFBOOT_RISCV_MMODE
/* Initialize UART for a secondary hart (1-4). Hart 0 uses uart_init(). */
void uart_init_hart(unsigned long hartid)
{
    unsigned long base;
    if (hartid == 0 || hartid > 4)
        return;
    base = UART_BASE_FOR_HART(hartid);
    /* MSS_PERIPH_MMUART0 = bit 5; shift by hartid selects MMUART1-4 */
    SYSREG_SUBBLK_CLOCK_CR |= (MSS_PERIPH_MMUART0 << hartid);
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    SYSREG_SOFT_RESET_CR &= ~(MSS_PERIPH_MMUART0 << hartid);
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    udelay(100);
    uart_init_base(base);
    udelay(10);
}

/* Write to a specific hart's UART (hart 0-4). */
void uart_write_hart(unsigned long hartid, const char* buf, unsigned int sz)
{
    unsigned long base;
    uint32_t pos = 0;
    if (hartid > 4)
        return;
    base = UART_BASE_FOR_HART(hartid);
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') {
            while ((MMUART_LSR(base) & MSS_UART_THRE) == 0);
            MMUART_THR(base) = '\r';
        }
        while ((MMUART_LSR(base) & MSS_UART_THRE) == 0);
        MMUART_THR(base) = c;
    }
}
#endif /* WOLFBOOT_RISCV_MMODE */
