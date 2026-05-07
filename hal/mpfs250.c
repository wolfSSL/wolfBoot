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
            unsigned long kentry = (unsigned long)mpfs_kernel_handoff.kernel_entry;
            unsigned long dtb = (unsigned long)mpfs_kernel_handoff.dtb_addr;
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

/* Simple busy-loop delay
 * Approximately us microseconds at ~40MHz (early boot clock)
 * This is used before the timer is fully reliable */
static void ddr_delay(uint32_t us)
{
    volatile uint32_t i;
    /* At ~40MHz, ~10 loop iterations per microsecond */
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

            /* Reinitialize UART for new clock frequency */
            hal_uart_reinit();
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

/* DDR Segment Configuration */
static void setup_segments(void)
{
    /* Cached access segments */
    DDR_SEG_REG(SEG0_0) = LIBERO_SETTING_SEG0_0 & 0x7FFFUL;
    DDR_SEG_REG(SEG0_1) = LIBERO_SETTING_SEG0_1 & 0x7FFFUL;
    DDR_SEG_REG(SEG0_2) = LIBERO_SETTING_SEG0_2 & 0x7FFFUL;
    DDR_SEG_REG(SEG0_3) = LIBERO_SETTING_SEG0_3 & 0x7FFFUL;
    DDR_SEG_REG(SEG0_4) = LIBERO_SETTING_SEG0_4 & 0x7FFFUL;
    DDR_SEG_REG(SEG0_5) = LIBERO_SETTING_SEG0_5 & 0x7FFFUL;
    DDR_SEG_REG(SEG0_6) = LIBERO_SETTING_SEG0_6 & 0x7FFFUL;

    /* Non-cached access segments */
    DDR_SEG_REG(SEG1_0) = LIBERO_SETTING_SEG1_0 & 0x7FFFUL;
    DDR_SEG_REG(SEG1_1) = LIBERO_SETTING_SEG1_1 & 0x7FFFUL;
    DDR_SEG_REG(SEG1_2) = LIBERO_SETTING_SEG1_2 & 0x7FFFUL;
    DDR_SEG_REG(SEG1_3) = LIBERO_SETTING_SEG1_3 & 0x7FFFUL;
    DDR_SEG_REG(SEG1_4) = LIBERO_SETTING_SEG1_4 & 0x7FFFUL;
    DDR_SEG_REG(SEG1_5) = LIBERO_SETTING_SEG1_5 & 0x7FFFUL;
    DDR_SEG_REG(SEG1_6) = LIBERO_SETTING_SEG1_6 & 0x7FFFUL;
    DDR_SEG_REG(SEG1_7) = LIBERO_SETTING_SEG1_7 & 0x7FFFUL;
    mb();

    /* Disable DDR blocker - critical!
     * SEG0.CFG[7] = 1 allows L2 cache controller to access DDR
     */
    DBG_DDR("DDR: Blocker@0x%lx ", DDR_SEG_BASE + SEG0_BLOCKER);
    DBG_DDR("before=0x%x ", DDR_SEG_REG(SEG0_BLOCKER));
    DDR_SEG_REG(SEG0_BLOCKER) = 0x01UL;
    mb();
    DBG_DDR("after=0x%x\n", DDR_SEG_REG(SEG0_BLOCKER));
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
    DDRCFG_REG(0x3CA0) = LIBERO_SETTING_CFG_NIBBLE_DEVICES;
    DDRCFG_REG(0x3CA4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS0_0;
    DDRCFG_REG(0x3CA8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS0_1;
    DDRCFG_REG(0x3CAC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS1_0;
    DDRCFG_REG(0x3CB0) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS1_1;
    DDRCFG_REG(0x3CB4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS2_0;
    DDRCFG_REG(0x3CB8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS2_1;
    DDRCFG_REG(0x3CBC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS3_0;
    DDRCFG_REG(0x3CC0) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS3_1;
    DDRCFG_REG(0x3CC4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS4_0;
    DDRCFG_REG(0x3CC8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS4_1;
    DDRCFG_REG(0x3CCC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS5_0;
    DDRCFG_REG(0x3CD0) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS5_1;
    DDRCFG_REG(0x3CD4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS6_0;
    DDRCFG_REG(0x3CD8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS6_1;
    DDRCFG_REG(0x3CDC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS7_0;
    DDRCFG_REG(0x3CE0) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS7_1;
    DDRCFG_REG(0x3CE4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS8_0;
    DDRCFG_REG(0x3CE8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS8_1;
    DDRCFG_REG(0x3CEC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS9_0;
    DDRCFG_REG(0x3CF0) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS9_1;
    DDRCFG_REG(0x3CF4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS10_0;
    DDRCFG_REG(0x3CF8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS10_1;
    DDRCFG_REG(0x3CFC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS11_0;
    DDRCFG_REG(0x3D00) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS11_1;
    DDRCFG_REG(0x3D04) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS12_0;
    DDRCFG_REG(0x3D08) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS12_1;
    DDRCFG_REG(0x3D0C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS13_0;
    DDRCFG_REG(0x3D10) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS13_1;
    DDRCFG_REG(0x3D14) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS14_0;
    DDRCFG_REG(0x3D18) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS14_1;
    DDRCFG_REG(0x3D1C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS15_0;
    DDRCFG_REG(0x3D20) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS15_1;
    DDRCFG_REG(0x3D24) = LIBERO_SETTING_CFG_NUM_LOGICAL_RANKS_PER_3DS;
    DDRCFG_REG(0x3D28) = LIBERO_SETTING_CFG_RFC_DLR1;
    DDRCFG_REG(0x3D2C) = LIBERO_SETTING_CFG_RFC_DLR2;
    DDRCFG_REG(0x3D30) = LIBERO_SETTING_CFG_RFC_DLR4;
    DDRCFG_REG(0x3D34) = LIBERO_SETTING_CFG_RRD_DLR;
    DDRCFG_REG(0x3D38) = LIBERO_SETTING_CFG_FAW_DLR;
    DDRCFG_REG(0x3D3C) = LIBERO_SETTING_CFG_ADVANCE_ACTIVATE_READY;

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
    DDRCFG_REG(0x5000) = LIBERO_SETTING_CFG_REORDER_EN;
    DDRCFG_REG(0x5004) = LIBERO_SETTING_CFG_REORDER_QUEUE_EN;
    DDRCFG_REG(0x5008) = LIBERO_SETTING_CFG_INTRAPORT_REORDER_EN;
    DDRCFG_REG(0x5010) = LIBERO_SETTING_CFG_MAINTAIN_COHERENCY;
    DDRCFG_REG(0x5014) = LIBERO_SETTING_CFG_Q_AGE_LIMIT;
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
    DDRCFG_REG(0x6400) = LIBERO_SETTING_CFG_ERROR_GROUP_SEL;
    DDRCFG_REG(0x6404) = LIBERO_SETTING_CFG_DATA_SEL;
    DDRCFG_REG(0x6408) = LIBERO_SETTING_CFG_TRIG_MODE;
    DDRCFG_REG(0x640C) = LIBERO_SETTING_CFG_POST_TRIG_CYCS;
    DDRCFG_REG(0x6410) = LIBERO_SETTING_CFG_TRIG_MASK;
    DDRCFG_REG(0x6414) = LIBERO_SETTING_CFG_EN_MASK;
    DDRCFG_REG(0x6418) = LIBERO_SETTING_MTC_ACQ_ADDR;
    DDRCFG_REG(0x641C) = LIBERO_SETTING_CFG_TRIG_MT_ADDR_0;
    DDRCFG_REG(0x6420) = LIBERO_SETTING_CFG_TRIG_MT_ADDR_1;
    DDRCFG_REG(0x6424) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_0;
    DDRCFG_REG(0x6428) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_1;
    DDRCFG_REG(0x642C) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_2;
    DDRCFG_REG(0x6430) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_3;
    DDRCFG_REG(0x6434) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_4;
    DDRCFG_REG(0x6438) = LIBERO_SETTING_MTC_ACQ_WR_DATA_0;
    DDRCFG_REG(0x643C) = LIBERO_SETTING_MTC_ACQ_WR_DATA_1;
    DDRCFG_REG(0x6440) = LIBERO_SETTING_MTC_ACQ_WR_DATA_2;
    DDRCFG_REG(0x6444) = LIBERO_SETTING_CFG_PRE_TRIG_CYCS;
    DDRCFG_REG(0x6448) = LIBERO_SETTING_CFG_DATA_SEL_FIRST_ERROR;

    /* DYN_WIDTH_ADJ block (HSS:4304-4306) */
    DDRCFG_REG(0x7C00) = LIBERO_SETTING_CFG_DQ_WIDTH;
    DDRCFG_REG(0x7C04) = LIBERO_SETTING_CFG_ACTIVE_DQ_SEL;

    /* CA_PAR_ERR block (HSS:4308-4310) */
    DDRCFG_REG(0x8000) = LIBERO_SETTING_INIT_CA_PARITY_ERROR_GEN_REQ;
    DDRCFG_REG(0x8004) = LIBERO_SETTING_INIT_CA_PARITY_ERROR_GEN_CMD;

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
    DDRCFG_REG(0x3CA0) = LIBERO_SETTING_CFG_NIBBLE_DEVICES;
    DDRCFG_REG(0x3CA4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS0_0;
    DDRCFG_REG(0x3CA8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS0_1;
    DDRCFG_REG(0x3CAC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS1_0;
    DDRCFG_REG(0x3CB0) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS1_1;
    DDRCFG_REG(0x3CB4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS2_0;
    DDRCFG_REG(0x3CB8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS2_1;
    DDRCFG_REG(0x3CBC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS3_0;
    DDRCFG_REG(0x3CC0) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS3_1;
    DDRCFG_REG(0x3CC4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS4_0;
    DDRCFG_REG(0x3CC8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS4_1;
    DDRCFG_REG(0x3CCC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS5_0;
    DDRCFG_REG(0x3CD0) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS5_1;
    DDRCFG_REG(0x3CD4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS6_0;
    DDRCFG_REG(0x3CD8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS6_1;
    DDRCFG_REG(0x3CDC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS7_0;
    DDRCFG_REG(0x3CE0) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS7_1;
    DDRCFG_REG(0x3CE4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS8_0;
    DDRCFG_REG(0x3CE8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS8_1;
    DDRCFG_REG(0x3CEC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS9_0;
    DDRCFG_REG(0x3CF0) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS9_1;
    DDRCFG_REG(0x3CF4) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS10_0;
    DDRCFG_REG(0x3CF8) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS10_1;
    DDRCFG_REG(0x3CFC) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS11_0;
    DDRCFG_REG(0x3D00) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS11_1;
    DDRCFG_REG(0x3D04) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS12_0;
    DDRCFG_REG(0x3D08) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS12_1;
    DDRCFG_REG(0x3D0C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS13_0;
    DDRCFG_REG(0x3D10) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS13_1;
    DDRCFG_REG(0x3D14) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS14_0;
    DDRCFG_REG(0x3D18) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS14_1;
    DDRCFG_REG(0x3D1C) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS15_0;
    DDRCFG_REG(0x3D20) = LIBERO_SETTING_CFG_BIT_MAP_INDEX_CS15_1;
    DDRCFG_REG(0x3D24) = LIBERO_SETTING_CFG_NUM_LOGICAL_RANKS_PER_3DS;
    DDRCFG_REG(0x3D28) = LIBERO_SETTING_CFG_RFC_DLR1;
    DDRCFG_REG(0x3D2C) = LIBERO_SETTING_CFG_RFC_DLR2;
    DDRCFG_REG(0x3D30) = LIBERO_SETTING_CFG_RFC_DLR4;
    DDRCFG_REG(0x3D34) = LIBERO_SETTING_CFG_RRD_DLR;
    DDRCFG_REG(0x3D38) = LIBERO_SETTING_CFG_FAW_DLR;
    DDRCFG_REG(0x3D3C) = LIBERO_SETTING_CFG_ADVANCE_ACTIVATE_READY;

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
    DDRCFG_REG(0x5000) = LIBERO_SETTING_CFG_REORDER_EN;
    DDRCFG_REG(0x5004) = LIBERO_SETTING_CFG_REORDER_QUEUE_EN;
    DDRCFG_REG(0x5008) = LIBERO_SETTING_CFG_INTRAPORT_REORDER_EN;
    DDRCFG_REG(0x5010) = LIBERO_SETTING_CFG_MAINTAIN_COHERENCY;
    DDRCFG_REG(0x5014) = LIBERO_SETTING_CFG_Q_AGE_LIMIT;
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
    DDRCFG_REG(0x6400) = LIBERO_SETTING_CFG_ERROR_GROUP_SEL;
    DDRCFG_REG(0x6404) = LIBERO_SETTING_CFG_DATA_SEL;
    DDRCFG_REG(0x6408) = LIBERO_SETTING_CFG_TRIG_MODE;
    DDRCFG_REG(0x640C) = LIBERO_SETTING_CFG_POST_TRIG_CYCS;
    DDRCFG_REG(0x6410) = LIBERO_SETTING_CFG_TRIG_MASK;
    DDRCFG_REG(0x6414) = LIBERO_SETTING_CFG_EN_MASK;
    DDRCFG_REG(0x6418) = LIBERO_SETTING_MTC_ACQ_ADDR;
    DDRCFG_REG(0x641C) = LIBERO_SETTING_CFG_TRIG_MT_ADDR_0;
    DDRCFG_REG(0x6420) = LIBERO_SETTING_CFG_TRIG_MT_ADDR_1;
    DDRCFG_REG(0x6424) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_0;
    DDRCFG_REG(0x6428) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_1;
    DDRCFG_REG(0x642C) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_2;
    DDRCFG_REG(0x6430) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_3;
    DDRCFG_REG(0x6434) = LIBERO_SETTING_CFG_TRIG_ERR_MASK_4;
    DDRCFG_REG(0x6438) = LIBERO_SETTING_MTC_ACQ_WR_DATA_0;
    DDRCFG_REG(0x643C) = LIBERO_SETTING_MTC_ACQ_WR_DATA_1;
    DDRCFG_REG(0x6440) = LIBERO_SETTING_MTC_ACQ_WR_DATA_2;
    DDRCFG_REG(0x6444) = LIBERO_SETTING_CFG_PRE_TRIG_CYCS;
    DDRCFG_REG(0x6448) = LIBERO_SETTING_CFG_DATA_SEL_FIRST_ERROR;

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
     * Note: HSS sets rpc3_ODT=0 here but immediately overwrites it in
     * set_ddr_rpc_regs() with LIBERO_SETTING_RPC_ODT_DQ (0x3). We skip
     * the intermediate set to 0 since it has no effect.
     */
    {
        uint32_t dpc_wrlvl = (LIBERO_SETTING_DPC_BITS & 0xFFFFFC0FUL) | 0x50UL;
        DDRPHY_REG(PHY_DPC_BITS) = dpc_wrlvl;
        /* rpc3_ODT will be set to 0x03 in RPC config below, matching HSS */
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

    /* LPDDR4-specific configuration */
    DDRPHY_REG(0x588) = 0x04U;   /* rpc98 @ 0x588 - ibufmd_dqs setting */
    DDRPHY_REG(0x5C8) = 0x14U;   /* rpc226 @ 0x5C8 */
    /* SPARE0 = 0xA000 - common mode receiver for LPDDR4
     * NOTE: this writes to offset 0x1FC, which per HSS struct defs
     * is `UNUSED_SPACE3[27]` rather than the real SPARE0 (offset
     * 0x290).  Phase 3.10.3 tried "fixing" the offset to 0x290 and
     * adding UNUSED_SPACE0[0]=0xA000 alongside (matching HSS); the
     * 5-boot variance went from ~1200 mismatches at this address to
     * ~4700 mismatches at the corrected addresses.  Reverted -- the
     * 0x1FC write happens to be benign / mildly-helpful on this
     * Video Kit, while the "correct" addresses actively interfere.
     * Leave as-is until we understand why.  HSS line for reference:
     * mss_ddr.c:2526 `CFG_DDR_SGMII_PHY->SPARE0.SPARE0 = 0xA000U`. */
    DDRPHY_REG(0x1FC) = 0xA000U; /* "SPARE0" -- actually UNUSED_SPACE3[27]; see above */

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
    DDRPHY_REG(PHY_RPC156) = 0x00000006UL;  /* Trained: 0x06 */
    DDRPHY_REG(PHY_RPC166) = 0x00000002UL;  /* Trained: 0x02 */
    DDRPHY_REG(PHY_RPC168) = 0x00000000UL;  /* Trained: 0x00 */
    DDRPHY_REG(PHY_RPC220) = 0x0000000CUL;  /* Trained: 0x0C */
    DDRPHY_REG(PHY_BCLK_SCLK) = LIBERO_SETTING_TIP_CONFIG_PARAMS_BCLK_VCOPHS_OFFSET;

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

    /* AUTOINIT remains enabled (Libero default).
     * Previously we wrote 0x01 here to disable, but AUTOINIT must run
     * during DFI init complete to issue the LPDDR4 MR programming
     * sequence to the DRAM. */
    DDRCFG_REG(MC_AUTOINIT_DISABLE) = 0x00;
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
    DDRPHY_REG(PHY_EXPERT_DFI_STATUS) = 0x06UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = 0xFFFFFFFFUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x0FUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = 0x00000000UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x00000000UL;

    DDRPHY_REG(PHY_EXPERT_DFI_STATUS) = 0x04UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = 0xFFFFFFFFUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x0FUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = 0x00000000UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD1) = 0x00000000UL;

    DDRPHY_REG(PHY_EXPERT_DFI_STATUS) = 0x00UL;
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

/* DDR Training */
static int run_training(void)
{
    uint32_t timeout, dfi_stat, train_stat;

    /* Configure training skip - skip ADDCMD only (we do it manually for LPDDR4)
     * 0x02 = skip ADDCMD, TIP runs: BCLK_SCLK, WRLVL, RDGATE, DQ_DQS */
    DDRPHY_REG(PHY_TRAINING_SKIP) = LIBERO_SETTING_TRAINING_SKIP_SETTING;
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
     * Apply BCLK phase from Libero settings
     * The PHADJ register controls clock phase alignment:
     * - Bits 2-4: REG_OUT0_PHSINIT (BCLK phase)
     * - Bits 5-7: REG_OUT1_PHSINIT
     * - Bits 8-10: REG_OUT2_PHSINIT
     * - Bits 11-13: REG_OUT3_PHSINIT (BCLK90 phase)
     * - Bit 14: REG_LOADPHS_B (load phase, must toggle)
     */
    wolfBoot_printf("DDR: BCLK phase...");
    {
        /* Use Libero-generated value, toggle LOADPHS to apply */
        uint32_t pll_phadj = LIBERO_SETTING_DDR_PLL_PHADJ | 0x4000UL;
        DDR_PLL_REG(PLL_PHADJ) = pll_phadj;
        mb();
        DDR_PLL_REG(PLL_PHADJ) = pll_phadj & ~0x4000UL;
        mb();
        DDR_PLL_REG(PLL_PHADJ) = pll_phadj;
        mb();
        wolfBoot_printf("0x%x\n", pll_phadj);
    }

    ddr_delay(1000);

    /*
     * LPDDR4 Training Sequence (corrected based on HSS)
     * HSS sequence: Configure WRLVL -> DFI init -> wait for DFI complete -> lpddr4_manual_training -> wait for TIP
     */
    wolfBoot_printf("DDR: Starting TIP training...\n");

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
        uint32_t div0 = div0_1_orig & 0x3F00UL;
        uint32_t div1 = div0_1_orig & 0x3F000000UL;
        uint32_t div2 = div2_3_orig & 0x3F00UL;
        uint32_t div3 = div2_3_orig & 0x3F000000UL;
        uint32_t mult = 2;

        /* Double the dividers for MR writes */
        DDR_PLL_REG(PLL_DIV_0_1) = (div0 | div1) * mult;
        DDR_PLL_REG(PLL_DIV_2_3) = (div2 | div3) * mult;

        /* Wait for PHY PLL to lock */
        while ((DDRPHY_REG(PHY_PLL_CTRL_MAIN) & 0x2000000UL) == 0) {}
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
     * MR3 = 0xF1  : PDDS=RZQ/6 (40ohm), DBI-RD/WR disabled
     * MR11 = 0x31 : DQ_ODT=RZQ2 (bits 2:0=001), CA_ODT=RZQ4 (bits 6:4=011)
     * MR12 = 0x32 : CA VREF=50 (from Libero LPDDR4_VREF_CA=50)
     * MR13 = 0x00 : FSP-OP=0, FSP-WR=0, DMI enabled, VRCG normal
     * MR14 = 0x0F : DQ VREF=15 (from Libero LPDDR4_VREF_DATA=15)
     * MR22 = 0x06 : SOC_ODT=RZQ6 (40ohm, from Libero LPDDR4_SOC_ODT=RZQ6)
     */
    DBG_DDR("    MR writes...");
    {
        struct mr_write_s {
            uint8_t mr;
            uint8_t val;
        };
        struct mr_write_s mr_writes[] = {
            {1, 0x56}, {2, 0x2D}, {3, 0xF1}, {11, 0x31},
            {12, 0x32}, {13, 0x00}, {14, 0x0F}, {22, 0x06}
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

    /*
     * Restore PLL to normal speed after mode register writes
     * (from HSS lines 5121-5136)
     */
    DBG_DDR("    PLL freq restore...");
    DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x01;
    ddr_delay(500);

    DDR_PLL_REG(PLL_DIV_0_1) = div0_1_orig;
    DDR_PLL_REG(PLL_DIV_2_3) = div2_3_orig;

    /* Wait for PHY PLL to lock */
    while ((DDRPHY_REG(PHY_PLL_CTRL_MAIN) & 0x2000000UL) == 0) {}
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
        /* Force VREF to match HSS training result (0x10) instead of our sweep (0x0C) */
        uint32_t vref_answer = 0x10;  /* Use HSS value directly */
#if 0  /* Disable sweep temporarily */
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
#endif  /* Skip VREF sweep - use HSS value directly */

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

            /* Calculate differences and find minimum */
            uint32_t min_diff = 0xFF;
            uint32_t min_diffp1 = 0xFF;
            uint32_t min_refclk = 0;

            if (transition_a5_max < 0x20) {  /* Threshold for LPDDR4 */
                a5_offset_status = 1;  /* FAIL */
            }

            for (j = 0; j < 8; j++) {
                if (transition_a5_max >= transition_ck_array[j])
                    difference[j] = transition_a5_max - transition_ck_array[j];
                else
                    difference[j] = 0xFF;
            }

            for (j = 0; j < 8; j++) {
                if (difference[j] < min_diff) {
                    min_refclk = j;
                    min_diff = difference[j];
                    min_diffp1 = difference[(j + 1) & 0x7];
                }
            }

            if (min_diff == 0xFF)
                a5_offset_status = 1;

            if (a5_offset_status == 0) {
                /* Apply optimal phase and delay */
                refclk_phase = (min_refclk & 0x7) << 2;
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

                /* Move to optimal delay */
                for (j = 0; j < min_diffp1 && j < 128; j++) {
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_MOVE1) = 0x0UL;
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_MOVE1) = 0x180000UL;
                    DDRPHY_REG(PHY_EXPERT_DLYCNT_MOVE1) = 0x0UL;
                }

                DDRPHY_REG(PHY_EXPERT_DLYCNT_DIR1) = 0x000000UL;
                DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000000UL;

                DBG_DDR("phase=%d dly=%d...", min_refclk, min_diffp1);
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

    ddr_delay(100);

    DBG_DDR("    Post-manual training status:\n");
    DBG_DDR("      train_stat=0x%x dfi_train_complete=0x%x\n",
            DDRPHY_REG(PHY_TRAINING_STATUS),
            DDRCFG_REG(0x38));  /* STAT_DFI_TRAINING_COMPLETE */
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
    /* Phase 3.10.3 iter 4 tried LIBERO_SETTING_CFG_AUTO_ZQ_CAL_EN (=0)
     * here to match HSS exactly; made pattern test slightly worse and
     * train_stat unchanged.  Reverted to hardcoded 1 -- this is one
     * of the many divergences that doesn't matter for the TIP-stuck
     * symptom we're chasing. */
    DDRCFG_REG(MC_CFG_AUTO_ZQ_CAL_EN) = 0x00000001UL;
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
    DBG_DDR("    Restore DPC_BITS / RPC3_ODT to Libero...\n");
    DDRPHY_REG(PHY_DPC_BITS) = LIBERO_SETTING_DPC_BITS;
    DDRPHY_REG(PHY_RPC3_ODT) = LIBERO_SETTING_RPC_ODT_DQ;
    mb();

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
    wolfBoot_printf("    DFI_init_complete=0x%x INIT_DONE=0x%x\n",
        DDRCFG_REG(MC_DFI_INIT_COMPLETE),
        DDRCFG_REG(MC_INIT_AUTOINIT_DISABLE));
    {
        uint32_t lane;
        for (lane = 0; lane < 4; lane++) {
            DDRPHY_REG(0x800) = lane;
            ddr_delay(10);
            wolfBoot_printf("    L%d: gt_state=0x%x wl_dly=0x%x dqdqs_st=0x%x\n",
                lane,
                DDRPHY_REG(0x82C),
                DDRPHY_REG(0x830),
                DDRPHY_REG(0x83C));
        }
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
        while (timeout > 0 && !training_complete) {
            /* Check training status register */
            train_stat_check = DDRPHY_REG(PHY_TRAINING_STATUS);

            if (train_stat_check != last_train_stat) {
                DBG_DDR("      Progress: train_stat=0x%x (iter=%d)\n",
                        train_stat_check, 1000000 - timeout);
                last_train_stat = train_stat_check;
                progress_count++;
            }

            /* Check if all lanes have non-zero write leveling delays (primary indicator) */
            all_lanes_trained = 1;
            for (lane = 0; lane < 4; lane++) {
                DDRPHY_REG(0x800) = lane;  /* lane_select */
                ddr_delay(10);
                if (DDRPHY_REG(0x830) == 0) {  /* wl_delay_0 */
                    all_lanes_trained = 0;
                    break;
                }
            }

            /* Training complete when:
             * 1. All lanes have WL delays (WRLVL done)
             * 2. Training status shows WRLVL+RDGATE+DQ_DQS bits set (0x1C or 0x1D)
             * Note: gt_state=0xB is normal per HSS logs, not an error
             */
            if (all_lanes_trained && (train_stat_check & (WRLVL_BIT | RDGATE_BIT | DQ_DQS_BIT))) {
                training_complete = 1;
                break;
            }

            timeout--;
            ddr_delay(10);  /* 100us per iteration */

            if ((timeout % 10000) == 0 && progress_count == 0) {
                DDRPHY_REG(0x800) = 0;  /* Select lane 0 */
                ddr_delay(10);
                DBG_DDR("      Waiting... train_stat=0x%x wl_dly=0x%x gt_state=0x%x\n",
                        train_stat_check,
                        DDRPHY_REG(0x830),  /* wl_delay_0 */
                        DDRPHY_REG(0x82C));  /* gt_state */
            }
        }

        DBG_DDR("    Training status: 0x%x\n", DDRPHY_REG(PHY_TRAINING_STATUS));
        DBG_DDR("    training_skip=0x%x training_reset=0x%x\n",
                DDRPHY_REG(PHY_TRAINING_SKIP), DDRPHY_REG(PHY_TRAINING_RESET));

        DBG_DDR("    Per-lane status:\n");
        for (lane = 0; lane < 5; lane++) {
            DDRPHY_REG(0x800) = lane;  /* lane_select */
            ddr_delay(50);
            DBG_DDR("      L%d: gt_err=0x%x gt_state=0x%x wl_dly=0x%x dqdqs_st=0x%x\n",
                    lane,
                    DDRPHY_REG(0x81C),   /* gt_err_comb */
                    DDRPHY_REG(0x82C),   /* gt_state */
                    DDRPHY_REG(0x830),   /* wl_delay_0 */
                    DDRPHY_REG(0x83C));  /* dqdqs_state */
        }

        DBG_DDR("    TIP cfg: tip_cfg_params=0x%x\n", DDRPHY_REG(PHY_TIP_CFG_PARAMS));
        DBG_DDR("    BCLK: pll_phadj=0x%x bclk_sclk=0x%x\n",
                DDR_PLL_REG(PLL_PHADJ), DDRPHY_REG(PHY_BCLK_SCLK));
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
    }

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

    /* Check final training status */
    train_stat = DDRPHY_REG(PHY_TRAINING_STATUS);
    wolfBoot_printf("  Final train_stat=0x%x\n", train_stat);

    /* Phase 3.10.3 D-3 v3 (2026-05-05): the legacy lane-mask WRCALIB
     * sweep below DOES run after the MR2 fix, but its outcome (often
     * "FAIL lanes=0x4" with only lane 2 calibrated) regresses the
     * DDR-TEST mismatch count and zeros parts of memory.  Skip it
     * when train_stat=0x1D -- TIP did its own WRCALIB during
     * autonomous training. */
    train_stat = DDRPHY_REG(PHY_TRAINING_STATUS);
    if ((train_stat & TRAINING_MASK) == (BCLK_SCLK_BIT | WRLVL_BIT |
        RDGATE_BIT | DQ_DQS_BIT)) {
        wolfBoot_printf("  Write calib...skipped (TIP train_stat=0x%x)\n",
            train_stat);
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

    return 0;
}

/* DDR Memory Test */
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
    uint32_t train_stat, blocker;

    uint32_t ctrl_done;

    /* Check if training is complete enough */
    train_stat = DDRPHY_REG(PHY_TRAINING_STATUS);
    blocker = DDR_SEG_REG(SEG0_BLOCKER);
    ctrl_done = DDRCFG_REG(MC_CTRLR_INIT_DONE);
    wolfBoot_printf("DDR: Memory test @ 0x80000000...\n");
    wolfBoot_printf("  Training=0x%x Blocker=0x%x INIT_DONE=0x%x\n",
                    train_stat, blocker, ctrl_done);

    if (!(blocker & 0x01)) {
        wolfBoot_printf("  ERROR: DDR blocker not disabled!\n");
        return -1;
    }

    if (!ctrl_done) {
        wolfBoot_printf("  WARNING: Controller INIT_DONE not set\n");
        /* Try memory test anyway to see if it works */
    }

    /* Phase 3.10.3 (A) probes.  Test non-cached FIRST -- if that
     * works but cached hangs, the L2 cache controller is what's
     * deadlocking. */
    wolfBoot_printf("  PRE_DDR_PROBE\n");
    {
        volatile uint32_t *probe_nc = (volatile uint32_t *)0xC0000000UL;
        volatile uint32_t *probe_c  = (volatile uint32_t *)0x80000000UL;
        wolfBoot_printf("  [nc] non-cached write @0xC0000000...");
        *probe_nc = 0xDEADBEEF;
        mb();
        wolfBoot_printf("done; readback=0x%x\n", *probe_nc);
        wolfBoot_printf("  [c] cached write @0x80000000...");
        *probe_c = 0xCAFEBABE;
        mb();
        wolfBoot_printf("done; readback=0x%x\n", *probe_c);
    }

    for (i = 0; i < 4; i++) {
        wolfBoot_printf("  [%d] Write 0x%x...", i, patterns[i]);
        ddr[i] = patterns[i];
        mb();
        readback = ddr[i];
        wolfBoot_printf("Read 0x%x ", readback);
        if (readback != patterns[i]) {
            wolfBoot_printf("FAIL\n");
            errors++;
        } else {
            wolfBoot_printf("OK\n");
        }
    }

    if (errors == 0) {
        wolfBoot_printf("  PASSED\n");
        return 0;
    }

    wolfBoot_printf("FAILED (%d/4)\n", errors);
    return -1;
}

/* Main DDR Initialization Entry Point */
int mpfs_ddr_init(void)
{
    int ret;

    wolfBoot_printf("\n========================================\n");
    wolfBoot_printf("MPFS DDR Init (Video Kit LPDDR4 2GB)\n");
    wolfBoot_printf("MT53D512M32D2DS-053 x32 @ 1600 Mbps\n");
    wolfBoot_printf("========================================\n");

    /* Step 1: NWC/PLL initialization */
    ret = nwc_init();
    if (ret != 0) {
        wolfBoot_printf("DDR: NWC init FAILED\n");
        return -1;
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

    /* Step 5: Configure controller timing */
    setup_controller();

    /* Step 6: Configure PHY */
    ret = setup_phy();
    if (ret != 0)
        wolfBoot_printf("DDR: PHY setup warning\n");

    /* Step 7: Training reset and clock rotation */
    training_reset_and_rotate();
    DBG_DDR("DDR: After rotation SR_N=0x%x\n", DDRCFG_REG(MC_CTRLR_SOFT_RESET));

    /* Step 8: TIP configuration (use correct register) */
    DDRPHY_REG(PHY_TIP_CFG_PARAMS) = LIBERO_SETTING_TIP_CFG_PARAMS;
    mb();

    /* Step 9: Run training, with retry-on-failure mirroring HSS's
     * DDR_TRAINING_FAIL state machine (mss_ddr.c:512).  HSS retries
     * up to MAX_RETRY_COUNT times: on each retry it resets CKE,
     * forces controller reset, clears DFI/CTRLR_INIT, then re-runs
     * training (which selects a different refclk_offset internally).
     *
     * Phase 3.10.3 (2): we don't yet have the refclk_offset sweep
     * inside our run_training, but trying just the basic retry
     * (controller reset + re-init) might surface whether
     * train_stat advances on a fresh attempt.
     */
    {
        uint32_t retry_count = 0;
        const uint32_t MAX_RETRY = 3;
        uint32_t train_stat_now;

        ret = run_training();
        train_stat_now = DDRPHY_REG(PHY_TRAINING_STATUS);
        while ((train_stat_now & TRAINING_MASK) != (BCLK_SCLK_BIT | WRLVL_BIT |
               RDGATE_BIT | DQ_DQS_BIT) && retry_count < MAX_RETRY) {
            wolfBoot_printf("DDR: Training retry %lu (train_stat=0x%x)\n",
                (unsigned long)retry_count, train_stat_now);
            /* HSS DDR_TRAINING_FAIL reset sequence (mss_ddr.c:519-538) */
            DDRCFG_REG(MC_INIT_CS) = 0x1;
            DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x1;
            ddr_delay(500);  /* DELAY_CYCLES_5_MICRO */
            DDRCFG_REG(MC_INIT_FORCE_RESET) = 0x1;
            ddr_delay(200000);  /* DELAY_CYCLES_2MS */
            retry_count++;
            DDRCFG_REG(MC_DFI_INIT_START) = 0x0;
            DDRCFG_REG(MC_CTRLR_INIT) = 0x0;
            DDRPHY_REG(PHY_TRAINING_START) = 0x0;
            mb();
            /* Re-run training */
            ret = run_training();
            train_stat_now = DDRPHY_REG(PHY_TRAINING_STATUS);
        }
        wolfBoot_printf("DDR: Final train_stat=0x%x after %lu retries\n",
            train_stat_now, (unsigned long)retry_count);
    }
    if (ret != 0) {
        wolfBoot_printf("DDR: Training FAILED\n");
        return -2;
    }

    /* Phase 3.10.3 (D-3 v2): HSS post-training sequence.
     *
     * After train_stat=0x1D, HSS does these critical steps before any
     * CPU access (mss_ddr.c DDR_TRAINING_WRITE_CALIBRATION + after):
     *
     *   (a) Set rpc220 = 0xC (LPDDR4 default DQ delay center)
     *   (b) load_dq(lane) for each of 4 lanes -- per-lane DQ delay load
     *   (c) write_calibration_using_mtc() -- HSS's MTC sweep (validates
     *       DDR via the on-chip MTC engine, no CPU bus involved)
     *   (d) MTC_test counting + pseudo-random patterns (DDR_FULL_MTC_CHECK)
     *   (e) Then the CPU access at 0xC0000000 / 0x80000000 succeeds.
     *
     * Without (a)-(d), the first CPU write to DDR hangs (we observe this
     * for both cached 0x80000000 and non-cached 0xC0000000).  The MTC
     * activity exercises the DDR controller and seems to "wake up" the
     * data path / drain any lingering training state.
     */
    wolfBoot_printf("DDR: Post-training sequence...\n");

    /* DDR_TRAINING_SET_FINAL_MODE (HSS mss_ddr.c:1619): rewrite
     * DDRPHY_MODE with LIBERO setting after training success.  This
     * transitions the PHY from training mode to operational mode. */
    DDRPHY_REG(PHY_MODE) = LIBERO_SETTING_DDRPHY_MODE;
    mb();
    wolfBoot_printf("  DDRPHY_MODE -> 0x%x (final)\n",
        DDRPHY_REG(PHY_MODE));

    /* (a) rpc220 = 0xC for LPDDR4 -- centers DQ/DQS sampling */
    DDRPHY_REG(PHY_RPC220) = 0x0CUL;
    mb();

    /* (b) load_dq(lane) for each of 4 lanes (HSS mss_ddr.c:2916).
     * Per-lane sequence: clear move, set DFI override + expert mode,
     * pulse load, restore expert mode. */
    {
        uint32_t lane;
        for (lane = 0; lane < 4; lane++) {
            DDRPHY_REG(PHY_EXPERT_DLYCNT_MOVE0) = 0x00UL;
            DDRPHY_REG(PHY_EXPERT_DFI_STATUS_TO_SHIM) = 0x07UL;
            DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x21UL;
            DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = (0xFFUL << (lane * 8UL));
            DDRPHY_REG(PHY_EXPERT_DLYCNT_LOAD0) = 0x00UL;
            DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x08UL;
        }
        mb();
        wolfBoot_printf("  load_dq done for 4 lanes\n");
    }

    /* Pre-MTC diagnostic snapshot */
    wolfBoot_printf("  CTRLR_INIT_DONE=0x%x AUTOINIT_DIS=0x%x train_stat=0x%x\n",
        DDRCFG_REG(MC_CTRLR_INIT_DONE),
        DDRCFG_REG(MC_INIT_AUTOINIT_DISABLE),
        DDRPHY_REG(PHY_TRAINING_STATUS));

    /* (c)+(d) MTC sanity check.  Smallest region (size=8 -> 2^8 = 256 B).
     * Per HSS MTC_test sequence: MT_EN=0, MT_RD_ONLY=0, MT_WR_ONLY=0, ... */
    {
        uint32_t mtc_to;
        uint32_t mtc_err;
        DDRCFG_REG(MT_EN) = 0;
        DDRCFG_REG(MT_EN_SINGLE) = 0;
        DDRCFG_REG(MT_STOP_ON_ERROR) = 0;
        DDRCFG_REG(0x440C) = 0;           /* MT_RD_ONLY = 0 (normal RW) */
        DDRCFG_REG(0x4410) = 0;           /* MT_WR_ONLY = 0 */
        DDRCFG_REG(MT_DATA_PATTERN) = 0;  /* counting pattern */
        DDRCFG_REG(MT_ADDR_PATTERN) = 0;  /* sequential */
        DDRCFG_REG(MT_START_ADDR_0) = 0;
        DDRCFG_REG(MT_START_ADDR_1) = 0;
        DDRCFG_REG(MT_ADDR_BITS) = 8;     /* 2^8 = 256 bytes */
        DDRCFG_REG(MT_ERROR_MASK_0) = 0xFFFFFFFFUL;
        DDRCFG_REG(MT_ERROR_MASK_1) = 0xFFFFFFFFUL;
        DDRCFG_REG(MT_ERROR_MASK_2) = 0xFFFFFFFFUL;
        DDRCFG_REG(MT_ERROR_MASK_3) = 0xFFFFFFFFUL;
        DDRCFG_REG(MT_ERROR_MASK_4) = 0xFFFFFFFFUL;
        DDRCFG_REG(MT_EN_SINGLE) = 0;
        DDRCFG_REG(MT_EN_SINGLE) = 1;     /* Run */
        mtc_to = 0xFFFFFFUL;
        while ((DDRCFG_REG(MT_DONE_ACK) & 0x1UL) == 0 && mtc_to > 0) {
            mtc_to--;
        }
        if (mtc_to == 0) {
            wolfBoot_printf("  MTC 256B TIMEOUT (DONE_ACK=0x%x ERR_STS=0x%x)\n",
                DDRCFG_REG(MT_DONE_ACK), DDRCFG_REG(MT_ERROR_STS));
        } else {
            mtc_err = DDRCFG_REG(MT_ERROR_STS) & 0x1UL;
            wolfBoot_printf("  MTC 256B %s (err_sts=0x%x to_used=0x%x)\n",
                mtc_err == 0 ? "PASS" : "FAIL",
                DDRCFG_REG(MT_ERROR_STS),
                (unsigned int)(0xFFFFFFUL - mtc_to));
        }
    }

    /* DDR pre-fill is currently disabled because both PDMA-based and
     * CPU-based fills > 256 KB cause the boot to trap (cause=2 epc=0,
     * stack zeroed) shortly after mpfs_ddr_init returns.  Hypothesis:
     * sustained AXI write traffic from CPU/PDMA generates L2-cache
     * pressure that corrupts L2 Scratch contents (where our stack +
     * code live).  Without fill, single-write SDHCI loads will see
     * ~1.8% bad words but the boot completes.
     *
     * TODO: investigate L2 cache way isolation -- ensure DDR cache
     * evictions never displace scratch SRAM contents.
     */

    /* L2 cache flush (HSS clear_bootup_cache_ways phase 2).  Evicts
     * any stale cache line tagged for the DDR window, in case the
     * controller-init / training activity left old tags. */
    {
        volatile uint32_t *flush64 = (volatile uint32_t *)0x02010200UL;
        uint32_t addr;
        for (addr = 0x80000000UL; addr < 0x80200000UL; addr += 64UL) {
            *flush64 = addr;
        }
        mb();
        wolfBoot_printf("  L2 flush done (2 MB @ 0x80000000)\n");
    }

    /* Step 10: Memory test */
    ret = memory_test();
    if (ret != 0)
        return -3;

    wolfBoot_printf("DDR: Initialization COMPLETE\n");
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
        wolfBoot_printf("DDR-TEST: HALT\n");
        while (1) { ; }
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

#ifndef WATCHDOG
    /* WATCHDOG=0 (default): disable WDT for the duration of wolfBoot.
     * It will be re-enabled in hal_prepare_boot() before do_boot. */
    MSS_WDT_CONTROL(MSS_WDT_E51_BASE) &= ~MSS_WDT_CTRL_ENABLE;
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

#ifdef MPFS_DDR_INIT
    /* Bring up LPDDR4 before any DDR-resident operations */
    if (mpfs_ddr_init() != 0) {
        wolfBoot_printf("DDR: Init FAILED - continuing with L2 only\n");
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

int hal_dts_fixup(void* dts_addr)
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
           addr + size <= WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE) ||
          (addr >= WOLFBOOT_PARTITION_UPDATE_ADDRESS &&
           addr + size <= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE))) {
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

static void mpfs_mpu_init_mmc(void)
{
    volatile uint64_t *pmp = (volatile uint64_t *)MSS_MPU_MMC_BASE;
    pmp[0] = LIBERO_SETTING_MMC_MPU_CFG_PMP0;
    pmp[1] = LIBERO_SETTING_MMC_MPU_CFG_PMP1;
    pmp[2] = LIBERO_SETTING_MMC_MPU_CFG_PMP2;
    pmp[3] = LIBERO_SETTING_MMC_MPU_CFG_PMP3;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
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
    mpfs_mpu_init_mmc();
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
/* Reinitialize UART baud divisor for post-PLL 150 MHz APB clock.
 * Called after mss_pll_init() locks the MSS PLL - the compile-time
 * MSS_APB_AHB_CLK (40 MHz pre-PLL) becomes invalid. */
void hal_uart_reinit(void)
{
    const uint64_t pclk = 150000000UL;
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
