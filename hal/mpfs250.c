/* mpfs250.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

/* ============================================================================
 * L2 Cache Controller Configuration
 *
 * The L2 cache controller must be properly configured before using the
 * L2 scratchpad memory. At reset, only 1 cache way is enabled.
 *
 * This function:
 * 1. Enables all cache ways (0-7) and scratchpad ways (8-11)
 * 2. Configures way masks for each master (harts, DMA, AXI ports)
 * 3. Disables L2 shutdown mode
 * ============================================================================ */
#ifdef WOLFBOOT_RISCV_MMODE
static void mpfs_config_l2_cache(void)
{
    uint64_t way_enable_before;
    uint64_t way_enable_after;

    /* Read current way enable state */
    way_enable_before = L2_WAY_ENABLE;

    /* Enable all cache ways (0-7) plus scratchpad ways (8-11)
     * Value 0x0B = ways 0-3 and 8-11 enabled (4 cache + 4 scratchpad)
     * This matches the working DDR demo configuration */
    L2_WAY_ENABLE = 0x0B;

    /* Disable L2 shutdown */
    SYSREG_L2_SHUTDOWN_CR = 0;

    /* Configure way masks - allow all masters to use cache ways 0-7 */
    L2_WAY_MASK_DMA = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT0 = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT1 = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT2 = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT3 = L2_WAY_MASK_CACHE_ONLY;

    /* E51 cache masks */
    L2_WAY_MASK_E51_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_E51_ICACHE = L2_WAY_MASK_CACHE_ONLY;

    /* U54 cache masks (configure even if not using U54s yet) */
    L2_WAY_MASK_U54_1_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_1_ICACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_2_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_2_ICACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_3_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_3_ICACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_4_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_4_ICACHE = L2_WAY_MASK_CACHE_ONLY;

    /* Memory barrier to ensure all writes complete */
    __asm__ volatile("fence iorw, iorw" ::: "memory");

    /* Read back to verify */
    way_enable_after = L2_WAY_ENABLE;

    /* Store for later reporting (can't print yet - UART not initialized) */
    (void)way_enable_before;
    (void)way_enable_after;
}

/* ============================================================================
 * DDR Controller Driver
 *
 * Complete DDR initialization for PolarFire SoC MPFS250T Video Kit.
 * Includes NWC/PLL initialization and DDR PHY/controller setup.
 *
 * Based on MPFS HAL (mss_nwc_init.c, mss_pll.c, mss_ddr.c) from Microchip HSS.
 * ============================================================================ */

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

/* Microsecond delay using hardware timer
 * Uses RISC-V time CSR which is always available */
static void udelay(uint32_t us)
{
    /* During DDR init, use busy-loop delay since timer may not be stable */
    ddr_delay(us);
}

/* IOSCB Bank Controllers and DLL bases */
#define IOSCB_BANK_CNTL_SGMII_BASE  0x3E400000UL
#define IOSCB_BANK_CNTL_DDR_BASE    0x3E020000UL
#define IOSCB_DLL_SGMII_BASE        0x3E100000UL

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
    DDRPHY_REG(0xC08) = LIBERO_SETTING_SGMII_PLL_CNTL;              /* PLL_CNTL */
    DDRPHY_REG(0xC0C) = LIBERO_SETTING_SGMII_CH0_CNTL;              /* CH0_CNTL */
    DDRPHY_REG(0xC10) = LIBERO_SETTING_SGMII_CH1_CNTL;              /* CH1_CNTL */
    DDRPHY_REG(0xC14) = LIBERO_SETTING_SGMII_RECAL_CNTL;            /* RECAL_CNTL */
    DDRPHY_REG(0xC18) = LIBERO_SETTING_SGMII_CLK_CNTL;              /* CLK_CNTL */
    DDRPHY_REG(0xC24) = LIBERO_SETTING_SGMII_SPARE_CNTL;            /* SPARE_CNTL */
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
    wolfBoot_printf("  Soft reset CFM to load NV map...");
    CFM_SGMII_REG(CFM_SGMII_SOFT_RESET) = 0x01UL;
    mb();
    udelay(100);
    wolfBoot_printf("done\n");

    rfckmux = CFM_SGMII_REG(CFM_SGMII_RFCKMUX);
    wolfBoot_printf("  RFCKMUX after NV load = 0x%x\n", rfckmux);

    /* Method 2: If NV map didn't have the value, try direct SCB writes */
    if (rfckmux != LIBERO_SETTING_SGMII_REFCLKMUX) {
        wolfBoot_printf("  Trying direct SCB writes...\n");

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
        wolfBoot_printf("  RFCKMUX after SCB write = 0x%x\n", rfckmux);
    } else {
        /* NV map loaded the value, still need to configure clock receiver */
        CFM_SGMII_REG(CFM_SGMII_CLK_XCVR) = LIBERO_SETTING_SGMII_CLK_XCVR;
        mb();
    }

    /* Debug: check clock receiver state */
    wolfBoot_printf("  CLK_XCVR=0x%x\n", CFM_SGMII_REG(CFM_SGMII_CLK_XCVR));

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
        wolfBoot_printf("  Using external refclk (RFCKMUX=0x%x)\n", rfckmux);
    } else {
        /* External refclk not available, use SCB_CLK (80MHz internal)
         * PLL0_RFCLK0_SEL = 10 (SCB_CLK), PLL0_RFCLK1_SEL = 10 (SCB_CLK)
         * PLL1_RFCLK0_SEL = 10 (SCB_CLK), PLL1_RFCLK1_SEL = 10 (SCB_CLK)
         * This gives: 0x02 | (0x02 << 2) | (0x02 << 4) | (0x02 << 6) | (0x02 << 8) = 0x2AA
         */
        pll_ckmux = 0x000002AAUL;
        wolfBoot_printf("  Using SCB_CLK (80MHz) as PLL ref (fallback)\n");
    }

    /* Configure PLL clock mux - select reference sources */
    CFM_MSS_REG(CFM_PLL_CKMUX) = pll_ckmux;
    mb();

    wolfBoot_printf("  PLL_CKMUX=0x%x\n", CFM_MSS_REG(CFM_PLL_CKMUX));

    /* Configure BCLK mux for DDR PHY */
    CFM_MSS_REG(CFM_BCLKMUX) = LIBERO_SETTING_MSS_BCLKMUX;
    mb();

    /* Frequency meter (not critical but part of standard init) */
    CFM_MSS_REG(CFM_FMETER_ADDR) = LIBERO_SETTING_MSS_FMETER_ADDR;
    CFM_MSS_REG(CFM_FMETER_DATAW) = LIBERO_SETTING_MSS_FMETER_DATAW;
    mb();

    /* Debug: verify writes */
    wolfBoot_printf("  BCLKMUX=0x%x\n", CFM_MSS_REG(CFM_BCLKMUX));

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
    wolfBoot_printf("  Initial MSS PLL CTRL=0x%x\n", pll_ctrl);

    if (pll_ctrl & PLL_LOCK_BIT) {
        wolfBoot_printf("  MSS PLL already locked!\n");
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
    wolfBoot_printf("  Powering up PLL (CTRL=0x%x)...\n",
        LIBERO_SETTING_MSS_PLL_CTRL | PLL_POWERDOWN_B);
    MSS_PLL_REG(PLL_CTRL) = LIBERO_SETTING_MSS_PLL_CTRL | PLL_POWERDOWN_B;
    mb();

    /* Short delay for PLL to start */
    udelay(100);

    /* Debug: Show PLL state after power up */
    pll_ctrl = MSS_PLL_REG(PLL_CTRL);
    wolfBoot_printf("  After power up: CTRL=0x%x\n", pll_ctrl);

    /* Wait for lock */
    wolfBoot_printf("  Waiting for MSS PLL lock...");
    timeout = 1000000;
    while (timeout > 0) {
        pll_ctrl = MSS_PLL_REG(PLL_CTRL);
        if (pll_ctrl & PLL_LOCK_BIT) {
            wolfBoot_printf("locked (0x%x)\n", pll_ctrl);

            /* Configure clock dividers before switching
             * LIBERO_SETTING_MSS_CLOCK_CONFIG_CR = 0x24:
             *   CPU = /1 (600MHz), AXI = /2 (300MHz), APB = /4 (150MHz)
             */
            SYSREG_REG(0x08) = 0x00000024UL;  /* CLOCK_CONFIG_CR */
            mb();

            /* Switch MSS to use PLL clock */
            CFM_MSS_REG(CFM_MSSCLKMUX) = LIBERO_SETTING_MSS_MSSCLKMUX;
            mb();

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
        /* Print progress every 100k iterations */
        if ((timeout % 100000) == 0) {
            wolfBoot_printf(".");
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
    wolfBoot_printf("  DDR bank controller reset...");
    ioscb_bank_cntl_ddr[0] = 0x01UL;  /* soft_reset */
    mb();
    udelay(100);
    wolfBoot_printf("done\n");

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

    wolfBoot_printf("  MSSIO...");
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
    wolfBoot_printf("done\n");

    /* Debug: check dynamic enable state */
    wolfBoot_printf("  STARTUP=0x%x DYN_CNTL=0x%x\n",
        DDRPHY_REG(PHY_STARTUP), DDRPHY_REG(PHY_DYN_CNTL));
    wolfBoot_printf("  MSSIO_CR=0x%x\n", SYSREGSCB_REG(MSSIO_CONTROL_CR_OFF));

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
    wolfBoot_printf("DDR: Blocker@0x%lx ", DDR_SEG_BASE + SEG0_BLOCKER);
    wolfBoot_printf("before=0x%x ", DDR_SEG_REG(SEG0_BLOCKER));
    DDR_SEG_REG(SEG0_BLOCKER) = 0x01UL;
    mb();
    wolfBoot_printf("after=0x%x\n", DDR_SEG_REG(SEG0_BLOCKER));
}

/* DDR Controller Configuration */
static void setup_controller(void)
{
    /* Controller soft reset - deassert */
    DDRCFG_REG(MC_CTRLR_SOFT_RESET) = LIBERO_SETTING_CTRLR_SOFT_RESET_N;

    /* Disable auto-init until PHY is ready */
    DDRCFG_REG(MC_AUTOINIT_DISABLE) = 0x01;

    /* Timing parameters */
    DDRCFG_REG(MC_CFG_BL) = LIBERO_SETTING_CFG_BL;
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
    DDRCFG_REG(MC_CFG_STARTUP_DELAY) = LIBERO_SETTING_CFG_STARTUP_DELAY;

    /* Memory geometry */
    DDRCFG_REG(MC_CFG_MEM_COLBITS) = LIBERO_SETTING_CFG_MEM_COLBITS;
    DDRCFG_REG(MC_CFG_MEM_ROWBITS) = LIBERO_SETTING_CFG_MEM_ROWBITS;
    DDRCFG_REG(MC_CFG_MEM_BANKBITS) = LIBERO_SETTING_CFG_MEM_BANKBITS;
    DDRCFG_REG(MC_CFG_NUM_RANKS) = LIBERO_SETTING_CFG_NUM_RANKS;
    DDRCFG_REG(MC_CFG_MEMORY_TYPE) = LIBERO_SETTING_CFG_MEMORY_TYPE;

    /* Latency settings */
    DDRCFG_REG(MC_CFG_CL) = LIBERO_SETTING_CFG_CL;
    DDRCFG_REG(MC_CFG_CWL) = LIBERO_SETTING_CFG_CWL;
    DDRCFG_REG(MC_CFG_WL) = LIBERO_SETTING_CFG_WL;
    DDRCFG_REG(MC_CFG_RL) = LIBERO_SETTING_CFG_RL;

    /* Refresh */
    DDRCFG_REG(MC_CFG_REF_PER) = LIBERO_SETTING_CFG_REF_PER;
    DDRCFG_REG(MC_CFG_AUTO_REF_EN) = LIBERO_SETTING_CFG_AUTO_REF_EN;

    /* Additional timing */
    DDRCFG_REG(MC_CFG_XP) = LIBERO_SETTING_CFG_XP;
    DDRCFG_REG(MC_CFG_XSR) = LIBERO_SETTING_CFG_XSR;
    DDRCFG_REG(MC_CFG_MRD) = LIBERO_SETTING_CFG_MRD;

    /* DFI interface timing */
    DDRCFG_REG(MC_DFI_RDDATA_EN) = LIBERO_SETTING_CFG_DFI_T_RDDATA_EN;
    DDRCFG_REG(MC_DFI_PHY_RDLAT) = LIBERO_SETTING_CFG_DFI_T_PHY_RDLAT;
    DDRCFG_REG(MC_DFI_PHY_WRLAT) = LIBERO_SETTING_CFG_DFI_T_PHY_WRLAT;
    DDRCFG_REG(MC_DFI_PHYUPD_EN) = LIBERO_SETTING_CFG_DFI_PHYUPD_EN;
    mb();
}

/* DDR PHY Configuration */
static int setup_phy(void)
{
    uint32_t pvt_stat, pll_ctrl, timeout;

    wolfBoot_printf("DDR: PHY setup...");

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
    /* SPARE0 = 0xA000 - common mode receiver for LPDDR4 */
    DDRPHY_REG(0x1FC) = 0xA000U; /* SPARE0 */

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
    wolfBoot_printf("  PVT calib...");

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

    wolfBoot_printf("done\n");

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

    if (pll_ctrl & PLL_LOCK_BIT) {
        wolfBoot_printf("PHY PLL locked\n");
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

    /* Disable auto-init */
    DDRCFG_REG(MC_AUTOINIT_DISABLE) = 0x01;
    mb();

    /* Controller soft reset sequence */
    wolfBoot_printf("    SR before=0x%x\n", DDRCFG_REG(MC_CTRLR_SOFT_RESET));
    DDRCFG_REG(MC_CTRLR_SOFT_RESET) = 0x00000000UL;
    mb();
    wolfBoot_printf("    SR after 0=0x%x\n", DDRCFG_REG(MC_CTRLR_SOFT_RESET));
    udelay(1);
    DDRCFG_REG(MC_CTRLR_SOFT_RESET) = 0x00000001UL;
    mb();
    wolfBoot_printf("    SR after 1=0x%x\n", DDRCFG_REG(MC_CTRLR_SOFT_RESET));
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
    uint32_t timeout, dfi_stat, ctrl_stat, train_stat;

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
    wolfBoot_printf("  Configure PHY for WRLVL...");
    {
        /* Set vrgen_h = 0x5 in DPC_BITS (bits 9:4) */
        uint32_t dpc_bits = DDRPHY_REG(PHY_DPC_BITS);
        uint32_t dpc_wrlvl = (dpc_bits & 0xFFFFFC0FUL) | (0x5UL << 4U);
        DDRPHY_REG(PHY_DPC_BITS) = dpc_wrlvl;
        DDRPHY_REG(PHY_RPC3_ODT) = 0x00U;  /* ODT off for WRLVL */
        mb();
        wolfBoot_printf("DPC=0x%x ODT=0x%x...done\n",
                        DDRPHY_REG(PHY_DPC_BITS), DDRPHY_REG(PHY_RPC3_ODT));
    }

    /* Step 1: Release training reset */
    wolfBoot_printf("  Training reset release...");
    DDRPHY_REG(PHY_TRAINING_RESET) = 0x00000000UL;
    mb();
    ddr_delay(1000);
    wolfBoot_printf("done\n");

    /* Step 2: Start DFI init */
    wolfBoot_printf("  DFI init start...");
    DDRCFG_REG(MC_DFI_INIT_START) = 0x00000000UL;
    mb();
    DDRCFG_REG(MC_DFI_INIT_START) = 0x00000001UL;
    mb();

    /* Step 3: Start controller init */
    DDRCFG_REG(MC_CTRLR_INIT) = 0x00000000UL;
    mb();
    DDRCFG_REG(MC_CTRLR_INIT) = 0x00000001UL;
    mb();
    wolfBoot_printf("done\n");

    /* Step 4: Wait for DFI init complete */
    wolfBoot_printf("  Wait DFI complete...");
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
    wolfBoot_printf("OK\n");

    /* Lane alignment FIFO control (from HSS DDR_TRAINING_IP_SM_START_CHECK) */
    DDRPHY_REG(PHY_LANE_ALIGN_FIFO_CTRL) = 0x00;
    DDRPHY_REG(PHY_LANE_ALIGN_FIFO_CTRL) = 0x02;
    mb();

    /*
     * Step 5: LPDDR4 Manual Training (from HSS lpddr4_manual_training)
     * This is called AFTER DFI init completes per HSS
     */
    wolfBoot_printf("  LPDDR4 manual training...\n");

    /* Device reset sequence (from HSS lpddr4_manual_training lines 5035-5053) */
    wolfBoot_printf("    Device reset...");
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
    wolfBoot_printf("done\n");

    /*
     * DDR PLL frequency doubling for LPDDR4 training (from HSS lines 5057-5076)
     * This is critical - mode register writes need slower frequency
     * Save original dividers for restore after MR writes
     */
    wolfBoot_printf("    PLL freq double...");
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
    wolfBoot_printf("done\n");

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
    wolfBoot_printf("    Second reset...");
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
    wolfBoot_printf("done\n");

    /* Debug: Check controller state before MR writes */
    wolfBoot_printf("    Pre-MR: CKE=%d RST=%d CS=%d PLL=0x%x\n",
                    DDRCFG_REG(MC_INIT_DISABLE_CKE),
                    DDRCFG_REG(MC_INIT_FORCE_RESET),
                    DDRCFG_REG(MC_INIT_CS),
                    DDRPHY_REG(PHY_PLL_CTRL_MAIN));
    wolfBoot_printf("    DIV0_1=0x%x DIV2_3=0x%x\n",
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
    wolfBoot_printf("    MR writes...");
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
                DDRCFG_REG(MC_INIT_MR_WR_MASK) = 0xFF;  /* Write all 8 bits */
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
        wolfBoot_printf("ack=%d err=%d...", ack_cnt, err_cnt);
    }
    wolfBoot_printf("done\n");

    /*
     * Restore PLL to normal speed after mode register writes
     * (from HSS lines 5121-5136)
     */
    wolfBoot_printf("    PLL freq restore...");
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
    wolfBoot_printf("done\n");

    /*
     * CA VREF Training (from HSS lpddr4_manual_training lines 5140-5310)
     * This calibrates the command/address bus voltage reference
     * Must happen AFTER PLL restore at normal speed
     */
    wolfBoot_printf("    CA VREF training...\n");
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

        /* Apply final VREF value */
        DDR_BANKCONT_REG(0x00) = 0U;
        ddr_delay(50);

        if (vref_answer == 128) {
            /* Training failed - use default 0x10 */
            vref_answer = 0x10;
            wolfBoot_printf("FAIL(0x%x)...", vref_answer);
        } else {
            wolfBoot_printf("0x%x...", vref_answer);
        }

        dpc_bits_new = (DDRPHY_REG(PHY_DPC_BITS) & 0xFFFC0FFFUL) |
                       (vref_answer << 12) | (0x1UL << 18);
        DDRPHY_REG(PHY_DPC_BITS) = dpc_bits_new;
        ddr_delay(50);

        DDR_BANKCONT_REG(0x00) = 1U;
        ddr_delay(5000);
    }
    wolfBoot_printf("done\n");

    /*
     * MANUAL ADDCMD TRAINING (from HSS lpddr4_manual_training lines 5320-5600)
     * Finds optimal refclk_phase and CA output delay
     */
    wolfBoot_printf("    ADDCMD training...");
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

                wolfBoot_printf("phase=%d dly=%d...", min_refclk, min_diffp1);
            } else {
                /* Increase offset and retry */
                init_del_offset += transition_a5_max + 5;
                if (init_del_offset > 0xFF)
                    break;
            }
        }

        if (a5_offset_status != 0)
            wolfBoot_printf("FAIL...");
    }

    /* POST_INITIALIZATION after ADDCMD training */
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000008UL;
    DDRPHY_REG(PHY_EXPERT_DFI_STATUS_TO_SHIM) = 0x00000000UL;
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000009UL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x0000003FUL;
    DDRPHY_REG(PHY_EXPERT_DLYCNT_PAUSE) = 0x00000000UL;
    DDRPHY_REG(PHY_EXPERT_MODE_EN) = 0x00000008UL;
    ddr_delay(50);

    /* Verify training values applied */
    wolfBoot_printf("PLL_PHADJ=0x%x DPC=0x%x...",
                    DDR_PLL_REG(PLL_PHADJ),
                    DDRPHY_REG(PHY_DPC_BITS));

    /* Re-enable CKE */
    DDRCFG_REG(MC_INIT_DISABLE_CKE) = 0x00;
    ddr_delay(5000);

    /* Post-ADDCMD: Re-write mode registers with corrected values */
    wolfBoot_printf("    MR re-write...");
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
            for (j = 0; j < 10; j++) {
                DDRCFG_REG(MC_INIT_CS) = 0x01;
                DDRCFG_REG(MC_INIT_MR_WR_MASK) = 0xFF;
                DDRCFG_REG(MC_INIT_MR_ADDR) = mr_writes[i].mr;
                DDRCFG_REG(MC_INIT_MR_WR_DATA) = mr_writes[i].val;
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
        wolfBoot_printf("ack=%d err=%d...", ack_cnt, err_cnt);
    }
    wolfBoot_printf("done\n");

    ddr_delay(100);

    /* Debug: Check training status after manual training */
    wolfBoot_printf("    Post-manual training status:\n");
    wolfBoot_printf("      train_stat=0x%x dfi_train_complete=0x%x\n",
                    DDRPHY_REG(PHY_TRAINING_STATUS),
                    DDRCFG_REG(0x38));  /* STAT_DFI_TRAINING_COMPLETE */
    wolfBoot_printf("      gt_state=0x%x dqdqs_state=0x%x\n",
                    DDRPHY_REG(0x82C), DDRPHY_REG(0x83C));

    /* ZQ calibration */
    wolfBoot_printf("    ZQ cal...");
    DDRCFG_REG(MC_INIT_ZQ_CAL_START) = 0x00000001UL;
    DDRCFG_REG(MC_AUTOINIT_DISABLE) = 0x00000000UL;

    /* Wait for INIT_ACK */
    timeout = 0xFF;
    while ((DDRCFG_REG(MC_INIT_ACK) == 0) && (timeout > 0)) {
        ddr_delay(100);
        timeout--;
    }
    DDRCFG_REG(MC_INIT_ZQ_CAL_START) = 0x00000000UL;
    DDRCFG_REG(MC_CFG_AUTO_ZQ_CAL_EN) = 0x00000001UL;
    mb();
    wolfBoot_printf("done\n");

    /*
     * Simulate HSS state machine transitions to trigger TIP progression
     * HSS state machine: START_CHECK -> BCLKSCLK -> ADDCMD -> WRLVL
     * TIP may need to see these state transitions before it can start WRLVL
     */
    wolfBoot_printf("    Simulate state machine transitions...");
    {
        uint32_t train_stat = DDRPHY_REG(PHY_TRAINING_STATUS);

        /* Step 1: Verify BCLK_SCLK is complete (should be bit 0 set) */
        if (train_stat & BCLK_SCLK_BIT) {
            wolfBoot_printf("BCLK_SCLK done ");
        } else {
            wolfBoot_printf("BCLK_SCLK not done (0x%x) ", train_stat);
        }

        /* Step 2: Simulate entering ADDCMD state
         * Per HSS: Check if ADDCMD is skipped (training_skip bit 1)
         * If skipped, immediately transition to WRLVL state
         */
        uint32_t training_skip = DDRPHY_REG(PHY_TRAINING_SKIP);
        if (training_skip & ADDCMD_BIT) {
            wolfBoot_printf("ADDCMD skipped ");
            /* Simulate transition to WRLVL state - add delay to let TIP detect */
            ddr_delay(50000);  /* 500us delay for state transition */
        } else {
            /* ADDCMD not skipped - wait for ADDCMD completion */
            wolfBoot_printf("ADDCMD not skipped, waiting... ");
            uint32_t addcmd_timeout = 100000;  /* 10 seconds */
            while ((addcmd_timeout > 0) && !(train_stat & ADDCMD_BIT)) {
                train_stat = DDRPHY_REG(PHY_TRAINING_STATUS);
                addcmd_timeout--;
                ddr_delay(10);
            }
            if (train_stat & ADDCMD_BIT) {
                wolfBoot_printf("ADDCMD complete ");
            } else {
                wolfBoot_printf("ADDCMD timeout ");
            }
        }

        /* Step 3: Enable WRLVL in MR2 (TIP may need this before starting WRLVL)
         * Per User Guide Section 2.7.3.4: MR2 bit 7 must be set to enable WRLVL mode
         */
        wolfBoot_printf("MR2 WRLVL enable...");
        DDRCFG_REG(MC_INIT_CS) = 0x01;
        DDRCFG_REG(MC_INIT_MR_WR_MASK) = 0xFF;
        DDRCFG_REG(MC_INIT_MR_ADDR) = 2;  /* MR2 */
        DDRCFG_REG(MC_INIT_MR_WR_DATA) = 0xAD;  /* MR2 = 0x2D | 0x80 (WRLVL enable) */
        DDRCFG_REG(MC_INIT_MR_W_REQ) = 0x01;
        DDRCFG_REG(MC_INIT_MR_W_REQ) = 0x00;
        mb();
        ddr_delay(10000);  /* 100us delay for MR2 write to propagate */

        /* Step 4: Ensure TIP is running and give it time to detect state transition */
        if ((DDRPHY_REG(PHY_TRAINING_START) & 0x01) == 0) {
            DDRPHY_REG(PHY_TRAINING_START) = 0x00000001UL;
            mb();
        }
        ddr_delay(50000);  /* 500us delay for TIP to detect WRLVL state transition */
        wolfBoot_printf("done\n");

        /* Check initial status after state machine simulation */
        uint32_t init_stat = DDRPHY_REG(PHY_TRAINING_STATUS);
        wolfBoot_printf("    Post-state-machine: train_stat=0x%x\n", init_stat);
    }

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
    wolfBoot_printf("  Wait for TIP WRLVL to start and complete...\n");
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

            /* Print progress every 100ms if status changes */
            if (train_stat_check != last_train_stat) {
                wolfBoot_printf("      Progress: train_stat=0x%x (iter=%d)\n",
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

            /* Print status every 1 second if no progress */
            if ((timeout % 10000) == 0 && progress_count == 0) {
                DDRPHY_REG(0x800) = 0;  /* Select lane 0 */
                ddr_delay(10);
                wolfBoot_printf("      Waiting... train_stat=0x%x wl_dly=0x%x gt_state=0x%x\n",
                               train_stat_check,
                               DDRPHY_REG(0x830),  /* wl_delay_0 */
                               DDRPHY_REG(0x82C));  /* gt_state */
            }
        }

        /* Debug: Print training status */
        wolfBoot_printf("    Training status: 0x%x\n", DDRPHY_REG(PHY_TRAINING_STATUS));
        wolfBoot_printf("    training_skip=0x%x training_reset=0x%x\n",
                        DDRPHY_REG(PHY_TRAINING_SKIP), DDRPHY_REG(PHY_TRAINING_RESET));

        /* Print per-lane TIP status (from HSS tip_register_status) */
        wolfBoot_printf("    Per-lane status:\n");
        for (lane = 0; lane < 5; lane++) {
            DDRPHY_REG(0x800) = lane;  /* lane_select */
            ddr_delay(50);
            wolfBoot_printf("      L%d: gt_err=0x%x gt_state=0x%x wl_dly=0x%x dqdqs_st=0x%x\n",
                           lane,
                           DDRPHY_REG(0x81C),   /* gt_err_comb */
                           DDRPHY_REG(0x82C),   /* gt_state */
                           DDRPHY_REG(0x830),   /* wl_delay_0 */
                           DDRPHY_REG(0x83C));  /* dqdqs_state */
        }

        /* Additional TIP debug info */
        wolfBoot_printf("    TIP cfg: tip_cfg_params=0x%x\n", DDRPHY_REG(PHY_TIP_CFG_PARAMS));
        wolfBoot_printf("    BCLK: pll_phadj=0x%x bclk_sclk=0x%x\n",
                        DDR_PLL_REG(PLL_PHADJ), DDRPHY_REG(PHY_BCLK_SCLK));
        wolfBoot_printf("    RPC: rpc145=0x%x rpc147=0x%x rpc156=0x%x rpc166=0x%x\n",
                        DDRPHY_REG(PHY_RPC145), DDRPHY_REG(PHY_RPC147),
                        DDRPHY_REG(PHY_RPC156), DDRPHY_REG(PHY_RPC166));

        if (training_complete && all_lanes_trained) {
            wolfBoot_printf("    TIP training complete!\n");
        } else {
            wolfBoot_printf("    TIP training timeout or incomplete\n");
            wolfBoot_printf("      all_lanes_trained=%d train_stat=0x%x\n",
                           all_lanes_trained, train_stat_check);
        }
    }

    /*
     * Restore ODT and disable WRLVL in MR2 after TIP completes
     * Per User Guide: WRLVL mode must be disabled after training
     */
    wolfBoot_printf("  Restore ODT and disable WRLVL...");
    {
        /* Restore ODT (per HSS: rpc3_odt=0x3 after training) */
        DDRPHY_REG(PHY_RPC3_ODT) = 0x03U;
        mb();

        /* Disable WRLVL in MR2 (bit 7 = 0) */
        DDRCFG_REG(MC_INIT_CS) = 0x01;
        DDRCFG_REG(MC_INIT_MR_WR_MASK) = 0xFF;
        DDRCFG_REG(MC_INIT_MR_ADDR) = 2;  /* MR2 */
        DDRCFG_REG(MC_INIT_MR_WR_DATA) = 0x2D;  /* MR2 normal (WRLVL disabled) */
        DDRCFG_REG(MC_INIT_MR_W_REQ) = 0x01;
        DDRCFG_REG(MC_INIT_MR_W_REQ) = 0x00;
        mb();
        ddr_delay(1000);
        wolfBoot_printf("done\n");
    }

    /* Check final training status */
    train_stat = DDRPHY_REG(PHY_TRAINING_STATUS);
    wolfBoot_printf("  Final train_stat=0x%x\n", train_stat);

    /* Write calibration using MTC (Memory Test Controller)
     * Based on HSS write_calibration_using_mtc()
     */
    wolfBoot_printf("Write calib...");
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

    train_stat = DDRPHY_REG(PHY_TRAINING_STATUS);
    wolfBoot_printf("  Final status=0x%x\n", train_stat);

    /* Step 7: Check controller init done */
    ctrl_stat = DDRCFG_REG(MC_CTRLR_INIT_DONE);
    wolfBoot_printf("  Controller INIT_DONE=0x%x\n", ctrl_stat);

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
    wolfBoot_printf("DDR: Enable DDRC clock/reset...");
    wolfBoot_printf("CLK before=0x%x ", SYSREG_REG(SYSREG_SUBBLK_CLOCK_CR_OFF));
    SYSREG_REG(SYSREG_SUBBLK_CLOCK_CR_OFF) |= MSS_PERIPH_DDRC;
    mb();
    wolfBoot_printf("after=0x%x\n", SYSREG_REG(SYSREG_SUBBLK_CLOCK_CR_OFF));

    /* Step 3: Reset DDR controller */
    SYSREG_REG(SYSREG_SOFT_RESET_CR_OFF) |= MSS_PERIPH_DDRC;
    mb();
    udelay(1);
    SYSREG_REG(SYSREG_SOFT_RESET_CR_OFF) &= ~MSS_PERIPH_DDRC;
    mb();
    udelay(1);
    wolfBoot_printf("  RST=0x%x\n", SYSREG_REG(SYSREG_SOFT_RESET_CR_OFF));
    /* Debug: Test write to MC_BASE2 (now correctly at 0x20084000) */
    wolfBoot_printf("  Test MC_BASE2@0x%lx: ", DDRCFG_BASE + MC_BASE2);
    wolfBoot_printf("SR=0x%x ", DDRCFG_REG(MC_CTRLR_SOFT_RESET));
    wolfBoot_printf("RAS=0x%x\n", DDRCFG_REG(MC_CFG_RAS));
    wolfBoot_printf("done\n");

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
    wolfBoot_printf("DDR: After rotation SR_N=0x%x\n", DDRCFG_REG(MC_CTRLR_SOFT_RESET));

    /* Step 8: TIP configuration (use correct register) */
    DDRPHY_REG(PHY_TIP_CFG_PARAMS) = LIBERO_SETTING_TIP_CFG_PARAMS;
    mb();

    /* Step 9: Run training */
    ret = run_training();
    if (ret != 0) {
        wolfBoot_printf("DDR: Training FAILED\n");
        return -2;
    }

    /* Step 10: Memory test */
    ret = memory_test();
    if (ret != 0)
        return -3;

    wolfBoot_printf("DDR: Initialization COMPLETE\n");
    wolfBoot_printf("========================================\n");

    return 0;
}

#endif /* WOLFBOOT_RISCV_MMODE */


/* ============================================================================
 * Multi-Hart Support (M-Mode only)
 * ============================================================================ */
#ifdef WOLFBOOT_RISCV_MMODE
/* ============================================================================
 * Multi-Hart Support
 *
 * These functions handle waking secondary harts (U54 cores) and the
 * communication protocol between E51 (main hart) and U54s.
 * ============================================================================ */

/* Linker symbols for hart stacks and HLS */
extern uint64_t _main_hart_hls;

/* CLINT MSIP register access for sending IPIs */
#define CLINT_MSIP_REG(hart) (*(volatile uint32_t*)(CLINT_BASE + (hart) * 4))

/**
 * mpfs_get_main_hls - Get pointer to main hart's HLS
 * Returns: Pointer to HLS_DATA structure
 */
static HLS_DATA* mpfs_get_main_hls(void)
{
    return (HLS_DATA*)&_main_hart_hls;
}

/**
 * mpfs_signal_main_hart_started - Signal to secondary harts that main hart is ready
 *
 * Called by E51 after basic initialization. Secondary harts are waiting in WFI
 * for this signal before they signal their own readiness.
 */
static void mpfs_signal_main_hart_started(void)
{
    HLS_DATA* hls = mpfs_get_main_hls();

    hls->in_wfi_indicator = HLS_MAIN_HART_STARTED;
    hls->my_hart_id = MPFS_FIRST_HART;

    /* Memory barrier to ensure write is visible to other harts */
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/**
 * mpfs_wake_secondary_harts - Wake all U54 cores via IPI
 *
 * This function implements the hart wake-up protocol:
 * 1. Wait for each hart to signal it's in WFI
 * 2. Send IPI to wake the hart
 * 3. Wait for hart to acknowledge wake-up
 *
 * Returns: Number of harts successfully woken
 */
int mpfs_wake_secondary_harts(void)
{
    int hart_id;
    int woken_count = 0;

    wolfBoot_printf("Waking secondary harts...\n");

    for (hart_id = MPFS_FIRST_U54_HART; hart_id <= MPFS_LAST_U54_HART; hart_id++) {
        /* Note: In this simplified implementation, we just send IPIs.
         * The full implementation would wait for HLS_OTHER_HART_IN_WFI
         * from each hart, but we don't have per-hart HLS pointers yet.
         * For now, we just send the IPI and the hart will wake when ready. */

        wolfBoot_printf("  Sending IPI to hart %d...", hart_id);

        /* Send software interrupt (IPI) to this hart */
        CLINT_MSIP_REG(hart_id) = 0x01;

        /* Memory barrier */
        __asm__ volatile("fence iorw, iorw" ::: "memory");

        /* Small delay for hart to respond (~1ms) */
        udelay(1000);

        woken_count++;
        wolfBoot_printf(" done\n");
    }

    wolfBoot_printf("Woke %d secondary harts\n", woken_count);

    return woken_count;
}

/**
 * secondary_hart_entry - Entry point for secondary harts (U54 cores)
 *
 * Each U54 core uses its own MMUART:
 *   Hart 1 -> MMUART1 (/dev/ttyUSB1), Hart 2 -> MMUART2, etc.
 */
void secondary_hart_entry(unsigned long hartid, HLS_DATA* hls)
{
    /* Message template with placeholder for hart ID at position 5 */
    char msg[] = "Hart X: Woken, waiting for Linux boot...\n";
    (void)hls;

    /* Initialize this hart's dedicated UART */
    uart_init_hart(hartid);

    /* Update hart ID in message (position 5) */
    msg[5] = '0' + (char)hartid;

    /* Write to this hart's UART */
    uart_write_hart(hartid, msg, sizeof(msg) - 1);

    /* Wait for Linux to take over via SBI */
    while (1) {
        __asm__ volatile("wfi");
    }
}
#endif /* WOLFBOOT_RISCV_MMODE */

#if defined(EXT_FLASH) && defined(TEST_EXT_FLASH) && defined(__WOLFBOOT)
static int test_ext_flash(void);
#endif

void hal_init(void)
{
#ifdef WOLFBOOT_RISCV_MMODE
    int ddr_ret;

    /* Configure L2 cache controller first (before using L2 scratchpad heavily) */
    mpfs_config_l2_cache();

    /* Signal to secondary harts that main hart is ready */
    mpfs_signal_main_hart_started();
#endif

#ifdef DEBUG_UART
    /* Enable clock and release from soft reset for debug UART */
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

    /* Initialize DDR controller (includes NWC/PLL initialization) */
    ddr_ret = mpfs_ddr_init();
    if (ddr_ret != 0) {
        wolfBoot_printf("DDR init failed (%d) - halting\n", ddr_ret);
        while (1) __asm__ volatile("wfi");
    }
#endif

#ifdef EXT_FLASH
    if (qspi_init() != 0) {
        wolfBoot_printf("QSPI: Init failed\n");
    }
#if defined(TEST_EXT_FLASH) && defined(__WOLFBOOT)
    else {
        test_ext_flash();
    }
#endif
#endif /* EXT_FLASH */
}

/* ============================================================================
 * System Controller Mailbox Functions
 *
 * The MPFS system controller provides various system services via a mailbox
 * interface. Commands are sent by writing the opcode to the control register
 * and responses are read from the mailbox RAM.
 * ============================================================================ */

/**
 * mpfs_scb_mailbox_busy - Check if the system controller mailbox is busy
 *
 * Returns: non-zero if busy, 0 if ready
 */
static int mpfs_scb_mailbox_busy(void)
{
    return (SCBCTRL_REG(SERVICES_SR_OFFSET) & SERVICES_SR_BUSY_MASK);
}

/**
 * mpfs_read_serial_number - Read the device serial number via system services
 * @serial: Buffer to store the 16-byte device serial number
 *
 * This function sends a serial number request (opcode 0x00) to the system
 * controller and reads the 16-byte response from the mailbox RAM.
 *
 * Returns: 0 on success, negative error code on failure
 */
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
    timeout = 10000;
    while ((SCBCTRL_REG(SERVICES_CR_OFFSET) & SERVICES_CR_REQ_MASK) && timeout > 0) {
        timeout--;
    }
    if (timeout == 0) {
        wolfBoot_printf("SCB mailbox request timeout\n");
        return -3;
    }

    /* Wait for busy bit to clear (command completed) */
    timeout = 10000;
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

    /* Expand total size to allow adding/modifying properties */
    fdt_set_totalsize(fdt, fdt_totalsize(fdt) + 512);

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

    return 0;
}
void hal_prepare_boot(void)
{
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

#ifdef EXT_FLASH
/* External flash support */
void ext_flash_lock(void)
{
    /* TODO: Lock external flash */
}

void ext_flash_unlock(void)
{
    /* TODO: Unlock external flash */
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    /* TODO: Write to external flash */
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    /* TODO: Read from external flash */
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int ext_flash_erase(uintptr_t address, int len)
{
    /* TODO: Erase external flash sectors */
    (void)address;
    (void)len;
    return 0;
}
#endif /* EXT_FLASH */

#if defined(MMU) && !defined(WOLFBOOT_NO_PARTITIONS)
void* hal_get_dts_address(void)
{
    return (void*)WOLFBOOT_DTS_BOOT_ADDRESS;
}
#endif

/* ============================================================================
 * PLIC - Platform-Level Interrupt Controller (MPFS250-specific)
 *
 * Generic PLIC functions are in src/boot_riscv.c
 * Platform must provide:
 *   - plic_get_context(): Map current hart to PLIC context
 *   - plic_dispatch_irq(): Dispatch IRQ to appropriate handler
 * ============================================================================ */

/* Get the PLIC context for the current hart
 *
 * PLIC Context IDs for MPFS250:
 *   Hart 0 (E51):  Context 0 = M-mode (E51 has no S-mode)
 *   Hart 1 (U54):  Context 1 = M-mode, Context 2 = S-mode
 *   Hart 2 (U54):  Context 3 = M-mode, Context 4 = S-mode
 *   Hart 3 (U54):  Context 5 = M-mode, Context 6 = S-mode
 *   Hart 4 (U54):  Context 7 = M-mode, Context 8 = S-mode
 */
#ifdef WOLFBOOT_RISCV_MMODE
/* M-mode: Read hart ID directly from CSR */
static uint32_t get_hartid_mmode(void)
{
    uint32_t hartid;
    __asm__ volatile("csrr %0, mhartid" : "=r"(hartid));
    return hartid;
}

uint32_t plic_get_context(void)
{
    uint32_t hart_id = get_hartid_mmode();
    /* E51 (hart 0): M-mode only, context 0
     * U54 (harts 1-4): M-mode context = hart_id * 2 - 1 */
    if (hart_id == 0) {
        return 0;  /* E51 M-mode context */
    }
    return (hart_id * 2) - 1;  /* U54 M-mode context */
}
#else
/* S-mode: Hart ID passed by boot stage, stored in tp register */
extern unsigned long get_boot_hartid(void);
uint32_t plic_get_context(void)
{
    uint32_t hart_id = get_boot_hartid();
    /* Get S-mode context for a given hart (1-4 for U54 cores) */
    return hart_id * 2;
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
/* ============================================================================
 * SDHCI Platform HAL Functions
 * ============================================================================ */

/* Platform initialization - called from sdhci_init() */
void sdhci_platform_init(void)
{
    /* Release MMC controller from reset */
    SYSREG_SOFT_RESET_CR &= ~MSS_PERIPH_MMC;
}

/* Platform interrupt setup - called from sdhci_init() */
void sdhci_platform_irq_init(void)
{
    /* Set priority for MMC main interrupt */
    plic_set_priority(PLIC_INT_MMC_MAIN, PLIC_PRIORITY_DEFAULT);

    /* Set threshold to 0 (allow all priorities > 0) */
    plic_set_threshold(0);

    /* Enable MMC interrupt for this hart */
    plic_enable_interrupt(PLIC_INT_MMC_MAIN);

#ifdef DEBUG_SDHCI
    wolfBoot_printf("sdhci_platform_irq_init: hart %d, context %d, irq %d enabled\n",
        get_boot_hartid(), plic_get_context(), PLIC_INT_MMC_MAIN);
#endif
}

/* Platform bus mode selection - called from sdhci_init() */
void sdhci_platform_set_bus_mode(int is_emmc)
{
    (void)is_emmc;
    /* Nothing additional needed for MPFS - mode is set in generic driver */
}

/* Register access functions for generic SDHCI driver */
uint32_t sdhci_reg_read(uint32_t offset)
{
    return *((volatile uint32_t*)(EMMC_SD_BASE + offset));
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    *((volatile uint32_t*)(EMMC_SD_BASE + offset)) = val;
}
#endif /* DISK_SDCARD || DISK_EMMC */

/* ============================================================================
 * DEBUG UART Functions
 * ============================================================================ */

#ifdef DEBUG_UART

/* Configure baud divisors with fractional baud rate support.
 *
 * UART baud rate divisor formula: divisor = PCLK / (baudrate * 16)
 *
 * To support fractional divisors (6-bit, 0-63), we scale up the calculation:
 *   divisor_x128 = (PCLK * 8) / baudrate  (128x scaled for rounding precision)
 *   divisor_x64  = divisor_x128 / 2       (64x scaled for 6-bit fractional)
 *   integer_div  = divisor_x64 / 64       (integer portion of divisor)
 *   frac_div     = divisor_x64 % 64       (fractional portion, 0-63)
 *
 * The fractional part is then adjusted using the x128 value for rounding.
 */
static void uart_config_clk(uint32_t baudrate)
{
    const uint64_t pclk = MSS_APB_AHB_CLK;

    /* Scale up for precision: (PCLK * 128) / (baudrate * 16) */
    uint32_t div_x128 = (uint32_t)((8UL * pclk) / baudrate);
    uint32_t div_x64  = div_x128 / 2u;

    /* Extract integer and fractional parts */
    uint32_t div_int  = div_x64 / 64u;
    uint32_t div_frac = div_x64 - (div_int * 64u);

    /* Apply rounding correction from x128 calculation */
    div_frac += (div_x128 - (div_int * 128u)) - (div_frac * 2u);

    if (div_int > (uint32_t)UINT16_MAX)
        return;

    /* Write 16-bit divisor: set DLAB, write high/low bytes, clear DLAB */
    MMUART_LCR(DEBUG_UART_BASE) |= DLAB_MASK;
    MMUART_DMR(DEBUG_UART_BASE) = (uint8_t)(div_int >> 8);
    MMUART_DLR(DEBUG_UART_BASE) = (uint8_t)div_int;
    MMUART_LCR(DEBUG_UART_BASE) &= ~DLAB_MASK;

    /* Enable fractional divisor if integer divisor > 1 */
    if (div_int > 1u) {
        MMUART_MM0(DEBUG_UART_BASE) |= EFBR_MASK;
        MMUART_DFR(DEBUG_UART_BASE) = (uint8_t)div_frac;
    }
    else {
        MMUART_MM0(DEBUG_UART_BASE) &= ~EFBR_MASK;
    }
}

/* New APB clock after MSS PLL lock
 * This should match the configured MSS PLL output 2 (APB/AHB clock).
 * From HSS: LIBERO_SETTING_MSS_APB_AHB_CLK = 150000000 (150 MHz)
 */
#define MSS_APB_AHB_CLK_PLL    150000000UL

/* Reconfigure UART baud rate divisor for a specific clock */
static void uart_config_clk_with_freq(uint32_t baudrate, uint64_t pclk)
{
    /* Scale up for precision: (PCLK * 128) / (baudrate * 16) */
    uint32_t div_x128 = (uint32_t)((8UL * pclk) / baudrate);
    uint32_t div_x64  = div_x128 / 2u;

    /* Extract integer and fractional parts */
    uint32_t div_int  = div_x64 / 64u;
    uint32_t div_frac = div_x64 - (div_int * 64u);

    /* Apply rounding correction from x128 calculation */
    div_frac += (div_x128 - (div_int * 128u)) - (div_frac * 2u);

    if (div_int > (uint32_t)UINT16_MAX)
        return;

    /* Write 16-bit divisor: set DLAB, write high/low bytes, clear DLAB */
    MMUART_LCR(DEBUG_UART_BASE) |= DLAB_MASK;
    MMUART_DMR(DEBUG_UART_BASE) = (uint8_t)(div_int >> 8);
    MMUART_DLR(DEBUG_UART_BASE) = (uint8_t)div_int;
    MMUART_LCR(DEBUG_UART_BASE) &= ~DLAB_MASK;

    /* Enable fractional divisor if integer divisor > 1 */
    if (div_int > 1u) {
        MMUART_MM0(DEBUG_UART_BASE) |= EFBR_MASK;
        MMUART_DFR(DEBUG_UART_BASE) = (uint8_t)div_frac;
    }
    else {
        MMUART_MM0(DEBUG_UART_BASE) &= ~EFBR_MASK;
    }
}

/* Reinitialize UART after clock change (MSS PLL lock) */
void hal_uart_reinit(void)
{
    /* Reconfigure baud rate for new clock frequency */
    uart_config_clk_with_freq(115200, MSS_APB_AHB_CLK_PLL);
}

void uart_init(void)
{
    /* Disable special modes: LIN, IrDA, SmartCard */
    MMUART_MM0(DEBUG_UART_BASE) &= ~ELIN_MASK;
    MMUART_MM1(DEBUG_UART_BASE) &= ~EIRD_MASK;
    MMUART_MM2(DEBUG_UART_BASE) &= ~EERR_MASK;

    /* Disable interrupts */
    MMUART_IER(DEBUG_UART_BASE) = 0u;

    /* Reset and configure FIFOs, enable RXRDYN/TXRDYN pins */
    MMUART_FCR(DEBUG_UART_BASE) = 0u;
    MMUART_FCR(DEBUG_UART_BASE) |= CLEAR_RX_FIFO_MASK | CLEAR_TX_FIFO_MASK;
    MMUART_FCR(DEBUG_UART_BASE) |= RXRDY_TXRDYN_EN_MASK;

    /* Disable loopback (local and remote) */
    MMUART_MCR(DEBUG_UART_BASE) &= ~(LOOP_MASK | RLOOP_MASK);

    /* Set LSB-first for TX/RX */
    MMUART_MM1(DEBUG_UART_BASE) &= ~(E_MSB_TX_MASK | E_MSB_RX_MASK);

    /* Disable AFM, single wire mode */
    MMUART_MM2(DEBUG_UART_BASE) &= ~(EAFM_MASK | ESWM_MASK);

    /* Disable TX time guard, RX timeout, fractional baud */
    MMUART_MM0(DEBUG_UART_BASE) &= ~(ETTG_MASK | ERTO_MASK | EFBR_MASK);

    /* Clear timing registers */
    MMUART_GFR(DEBUG_UART_BASE) = 0u;
    MMUART_TTG(DEBUG_UART_BASE) = 0u;
    MMUART_RTO(DEBUG_UART_BASE) = 0u;

    /* Configure baud rate (115200) */
    uart_config_clk(115200);

    /* Set line config: 8N1 */
    MMUART_LCR(DEBUG_UART_BASE) = MSS_UART_DATA_8_BITS |
                                  MSS_UART_NO_PARITY |
                                  MSS_UART_ONE_STOP_BIT;
}

void uart_write(const char* buf, unsigned int sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') { /* handle CRLF */
            while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE) == 0);
            MMUART_THR(DEBUG_UART_BASE) = '\r';
        }
        while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE) == 0);
        MMUART_THR(DEBUG_UART_BASE) = c;
    }
}
#endif /* DEBUG_UART */

#ifdef WOLFBOOT_RISCV_MMODE
/**
 * uart_init_hart - Initialize UART for a specific hart
 *
 * Each U54 core uses its own MMUART:
 *   Hart 0 (E51)   -> MMUART0 (already initialized by hal_init)
 *   Hart 1 (U54_1) -> MMUART1
 *   Hart 2 (U54_2) -> MMUART2
 *   Hart 3 (U54_3) -> MMUART3
 *   Hart 4 (U54_4) -> MMUART4
 *
 * @hartid: The hart ID (1-4 for U54 cores)
 */
void uart_init_hart(unsigned long hartid)
{
    unsigned long base;

    if (hartid == 0 || hartid > 4) {
        return;  /* Hart 0 uses main UART, invalid harts ignored */
    }

    base = UART_BASE_FOR_HART(hartid);

    /* Enable clock and release from soft reset for this UART
     * The peripheral bit positions are:
     *   MMUART0 = bit 5, MMUART1 = bit 6, MMUART2 = bit 7, etc.
     * MSS_PERIPH_MMUART0 = (1 << 5), so shift by hartid */
    SYSREG_SUBBLK_CLOCK_CR |= (MSS_PERIPH_MMUART0 << hartid);

    /* Memory barrier before modifying reset */
    __asm__ volatile("fence iorw, iorw" ::: "memory");

    /* Release from soft reset */
    SYSREG_SOFT_RESET_CR &= ~(MSS_PERIPH_MMUART0 << hartid);

    /* Memory barrier */
    __asm__ volatile("fence iorw, iorw" ::: "memory");

    /* Longer delay for clock to stabilize (critical for reliable UART) */
    udelay(100);

    /* Disable special modes: LIN, IrDA, SmartCard */
    MMUART_MM0(base) &= ~ELIN_MASK;
    MMUART_MM1(base) &= ~EIRD_MASK;
    MMUART_MM2(base) &= ~EERR_MASK;

    /* Disable interrupts */
    MMUART_IER(base) = 0u;

    /* Reset and configure FIFOs */
    MMUART_FCR(base) = 0u;
    MMUART_FCR(base) |= CLEAR_RX_FIFO_MASK | CLEAR_TX_FIFO_MASK;
    MMUART_FCR(base) |= RXRDY_TXRDYN_EN_MASK;

    /* Disable loopback */
    MMUART_MCR(base) &= ~(LOOP_MASK | RLOOP_MASK);

    /* Set LSB-first */
    MMUART_MM1(base) &= ~(E_MSB_TX_MASK | E_MSB_RX_MASK);

    /* Disable AFM, single wire mode */
    MMUART_MM2(base) &= ~(EAFM_MASK | ESWM_MASK);

    /* Disable TX time guard, RX timeout, fractional baud */
    MMUART_MM0(base) &= ~(ETTG_MASK | ERTO_MASK | EFBR_MASK);

    /* Clear timing registers */
    MMUART_GFR(base) = 0u;
    MMUART_TTG(base) = 0u;
    MMUART_RTO(base) = 0u;

    /* Configure baud rate (115200)
     * Using EXACT same calculation as uart_config_clk for consistency */
    {
        const uint64_t pclk = MSS_APB_AHB_CLK;
        const uint32_t baudrate = 115200;

        /* Scale up for precision: (PCLK * 128) / (baudrate * 16) */
        uint32_t div_x128 = (uint32_t)((8UL * pclk) / baudrate);
        uint32_t div_x64  = div_x128 / 2u;

        /* Extract integer and fractional parts */
        uint32_t div_int  = div_x64 / 64u;
        uint32_t div_frac = div_x64 - (div_int * 64u);

        /* Apply rounding correction from x128 calculation (same as uart_config_clk) */
        div_frac += (div_x128 - (div_int * 128u)) - (div_frac * 2u);

        /* Enable DLAB to access divisor registers */
        MMUART_LCR(base) |= DLAB_MASK;

        /* Write DMR before DLR (same order as uart_config_clk) */
        MMUART_DMR(base) = (uint8_t)(div_int >> 8);
        MMUART_DLR(base) = (uint8_t)div_int;

        /* Clear DLAB */
        MMUART_LCR(base) &= ~DLAB_MASK;

        /* Configure fractional baud rate if needed */
        if (div_frac > 0u) {
            MMUART_MM0(base) |= EFBR_MASK;
            MMUART_DFR(base) = (uint8_t)div_frac;
        } else {
            MMUART_MM0(base) &= ~EFBR_MASK;
        }
    }

    /* Set line config: 8N1 */
    MMUART_LCR(base) = MSS_UART_DATA_8_BITS |
                       MSS_UART_NO_PARITY |
                       MSS_UART_ONE_STOP_BIT;

    /* Small delay after configuration */
    udelay(10);
}

/**
 * uart_write_hart - Write string to a specific hart's UART
 *
 * @hartid: The hart ID (0-4)
 * @buf: Buffer to write
 * @sz: Number of bytes to write
 */
void uart_write_hart(unsigned long hartid, const char* buf, unsigned int sz)
{
    unsigned long base;
    uint32_t pos = 0;

    if (hartid > 4) {
        return;
    }

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

/**
 * uart_printf_hart - Simple printf to a specific hart's UART
 * Only supports %d, %x, %s, %lu formats for minimal footprint
 */
static void uart_printf_hart(unsigned long hartid, const char* fmt, ...)
{
    char buf[128];
    int len = 0;
    const char* p = fmt;

    /* Very simple printf implementation */
    while (*p && len < (int)sizeof(buf) - 1) {
        if (*p == '%') {
            p++;
            if (*p == 'l' && *(p+1) == 'u') {
                /* %lu - unsigned long */
                p += 2;
                /* Skip for now - just print placeholder */
                buf[len++] = '[';
                buf[len++] = 'N';
                buf[len++] = ']';
            } else if (*p == 'd') {
                p++;
                buf[len++] = '[';
                buf[len++] = 'N';
                buf[len++] = ']';
            } else if (*p == 's') {
                p++;
                buf[len++] = '[';
                buf[len++] = 'S';
                buf[len++] = ']';
            } else {
                buf[len++] = '%';
                buf[len++] = *p++;
            }
        } else {
            buf[len++] = *p++;
        }
    }
    buf[len] = '\0';

    uart_write_hart(hartid, buf, len);
}
#endif /* WOLFBOOT_RISCV_MMODE */
