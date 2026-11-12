/* s32g2.c
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

/* NXP S32G2 (S32G274A) GoldBox / RDB2: wolfBoot is the Boot ROM's image in
 * SRAM at EL3, boots a signed FIT and stays resident as PSCI firmware. See
 * docs/Targets.md. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <target.h>

#if defined(DEBUG_UART)
    #define PRINTF_ENABLED
#endif

#include "image.h"
#include "loader.h"
#include "printf.h"
#include "psci.h"
#include "hal/s32g2.h"
#ifdef S32G2_QSPI_PROGRAM_FILE
#include "disk.h"
#include "disk_fs.h"
#ifndef BOOT_PART_A
#define BOOT_PART_A 2
#endif
#define S32G2_QSPI_PROGRAM_MAX  0x1000000   /* 16 MB, the NOR partition size */
#endif
#include "fdt.h"
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "sdhci.h"
#endif

#ifndef ARCH_AARCH64
#   error "wolfBoot s32g2 HAL: wrong architecture. Compile with ARCH=AARCH64."
#endif

/* MMIO is Device-nGnRnE, with the MMU off and in the identity map, so
 * accesses are strongly ordered by the memory type and need no barriers. */
static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t*)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t*)a = v; }
static inline void wr8(uintptr_t a, uint8_t v) { *(volatile uint8_t*)a = v; }
static inline uint8_t rd8(uintptr_t a) { return *(volatile uint8_t*)a; }
static inline void wr16(uintptr_t a, uint16_t v) { *(volatile uint16_t*)a = v; }
static inline uint64_t rd64(uintptr_t a) { return *(volatile uint64_t*)a; }

#include "aarch64_arch.h"

uint64_t hal_get_timer_us(void)
{
    return (timer_get_count() * 1000000ULL) / timer_get_freq();
}

void hal_delay_us(uint32_t us)
{
    uint64_t deadline = timer_deadline_us(us);

    while (!timer_expired(deadline))
        ;
}

/* Clock tree. Out of reset everything runs from the 48 MHz FIRC; this takes
 * the A53 cluster, the XBAR, the LIN, SDHC, QuadSPI and DDR clocks to the
 * rates the NXP BSP runs them at. */
/* MC_CGM: clock source select and dividers, one 0x40 block per mux. */
#define CGM_MUX_CSC(cgm, mux)       ((cgm) + 0x300 + (mux) * 0x40)
#define CGM_MUX_CSS(cgm, mux)       ((cgm) + 0x304 + (mux) * 0x40)
#define CGM_MUX_DC(cgm, mux, n)     ((cgm) + 0x308 + (mux) * 0x40 + (n) * 4)
#define CGM_MUX_DIV_UPD_STAT(cgm, mux) ((cgm) + 0x33C + (mux) * 0x40)
#define CGM_CSC_SELCTL_MASK         0x3F000000
#define CGM_CSC_SELCTL(src)         (((uint32_t)(src) << 24) & CGM_CSC_SELCTL_MASK)
#define CGM_CSC_CLK_SW              (1U << 2)
#define CGM_CSS_SELSTAT(css)        (((css) >> 24) & 0x3F)
#define CGM_CSS_SWTRG(css)          (((css) >> 17) & 0x7)
#define CGM_CSS_SWTRG_SUCCESS       1
#define CGM_CSS_SWIP                (1U << 16)
#define CGM_DC_DE                   (1U << 31)
#define CGM_DC_DIV(dc)              ((((uint32_t)(dc) - 1) << 16) & 0x00FF0000)

/* Mux source IDs */
#define CGM_SRC_FIRC                0
#define CGM_SRC_ARM_PLL_PHI0        4
#define CGM_SRC_ARM_DFS1            12
#define CGM_SRC_PERIPH_PLL_PHI3     21
#define CGM_SRC_PERIPH_DFS1         26
#define CGM_SRC_PERIPH_DFS3         28
#define CGM_SRC_DDR_PLL_PHI0        36

/* PLLDIG */
#define PLL_PLLCR                   0x00
#define PLL_PLLSR                   0x04
#define PLL_PLLDV                   0x08
#define PLL_PLLFD                   0x10
#define PLL_PLLCLKMUX               0x20
#define PLL_PLLODIV(n)              (0x80 + (n) * 4)
#define PLL_PLLCR_PLLPD             (1U << 31)
#define PLL_PLLSR_LOCK              (1U << 2)
#define PLL_PLLDV_RDIV(r)           (((uint32_t)(r) & 0x7) << 12)
#define PLL_PLLDV_MFI(m)            ((uint32_t)(m) & 0xFF)
#define PLL_PLLFD_SMDEN             (1U << 30)
#define PLL_PLLFD_MFN(m)            ((uint32_t)(m) & 0x7FFF)
#define PLL_PLLCLKMUX_FXOSC         1
#define PLL_PLLODIV_DE              (1U << 31)
#define PLL_PLLODIV_DIV(dc)         ((((uint32_t)(dc) - 1) << 16) & 0x00FF0000)
/* Output dividers per PLL: the register blocks end right after the last one. */
#define ARM_PLL_NODIVS              2
#define PERIPH_PLL_NODIVS           8
#define DDR_PLL_NODIVS              1

/* DFS */
#define DFS_PORTSR                  0x0C
#define DFS_PORTOLSR                0x10
#define DFS_PORTRESET               0x14
#define DFS_CTL                     0x18
#define DFS_DVPORT(n)               (0x1C + (n) * 4)
#define DFS_CTL_RESET               (1U << 1)
#define DFS_PORTS_MASK              0x3F
#define DFS_DVPORT_VAL(mfi, mfn)    ((((uint32_t)(mfi) & 0xFF) << 8) | ((uint32_t)(mfn) & 0x3F))

/* FXOSC */
#define FXOSC_CTRL                  0x00
#define FXOSC_STAT                  0x04
#define FXOSC_CTRL_OSCON            (1U << 0)
#define FXOSC_CTRL_GM_SEL(s)        (((uint32_t)(s) & 0xF) << 4)
#define FXOSC_CTRL_EOCV(v)          (((uint32_t)(v) & 0xFF) << 16)
#define FXOSC_CTRL_COMP_EN          (1U << 24)
#define FXOSC_STAT_OSC_STAT         (1U << 31)

/* MC_ME partition registers */
#define MC_ME_CTL_KEY               0x000
#define MC_ME_CTL_KEY_KEY           0x5AF0
#define MC_ME_CTL_KEY_INVKEY        0xA50F
#define MC_ME_PRTN_PCONF(n)         (0x100 + (n) * 0x200)
#define MC_ME_PRTN_PUPD(n)          (0x104 + (n) * 0x200)
#define MC_ME_PRTN_STAT(n)          (0x108 + (n) * 0x200)
#define MC_ME_PRTN_COFB0_STAT(n)    (0x110 + (n) * 0x200)
#define MC_ME_PRTN_COFB0_CLKEN(n)   (0x130 + (n) * 0x200)
#define MC_ME_PRTN_PCONF_PCE        (1U << 0)
#define MC_ME_PRTN_PUPD_PCUD        (1U << 0)
#define MC_ME_PRTN_STAT_PCS         (1U << 0)

/* Bound on hardware polls, so a wedge is reported instead of hanging. */
#define POLL_MAX                    2000000U

static int poll_set(uintptr_t a, uint32_t mask)
{
    uint32_t n;

    for (n = 0; n < POLL_MAX; n++) {
        if ((rd32(a) & mask) != 0)
            return 0;
    }
    return -1;
}

static int poll_clear(uintptr_t a, uint32_t mask)
{
    uint32_t n;

    for (n = 0; n < POLL_MAX; n++) {
        if ((rd32(a) & mask) == 0)
            return 0;
    }
    return -1;
}

/* Currently selected source of a clock mux */
uint32_t s32g2_cgm_mux_get(uintptr_t cgm, uint32_t mux)
{
    return CGM_CSS_SELSTAT(rd32(CGM_MUX_CSS(cgm, mux)));
}

int s32g2_cgm_mux_set(uintptr_t cgm, uint32_t mux, uint32_t src)
{
    uint32_t css, csc;

    css = rd32(CGM_MUX_CSS(cgm, mux));
    if (CGM_CSS_SELSTAT(css) == src && CGM_CSS_SWTRG(css) == CGM_CSS_SWTRG_SUCCESS &&
            (css & CGM_CSS_SWIP) == 0)
        return 0;
    if (poll_clear(CGM_MUX_CSS(cgm, mux), CGM_CSS_SWIP) != 0)
        return -1;
    csc = rd32(CGM_MUX_CSC(cgm, mux)) & ~CGM_CSC_SELCTL_MASK;
    csc |= CGM_CSC_SELCTL(src) | CGM_CSC_CLK_SW;
    wr32(CGM_MUX_CSC(cgm, mux), csc);
    if (poll_clear(CGM_MUX_CSC(cgm, mux), CGM_CSC_CLK_SW) != 0)
        return -1;
    if (poll_clear(CGM_MUX_CSS(cgm, mux), CGM_CSS_SWIP) != 0)
        return -1;
    css = rd32(CGM_MUX_CSS(cgm, mux));
    if (CGM_CSS_SWTRG(css) != CGM_CSS_SWTRG_SUCCESS || CGM_CSS_SELSTAT(css) != src)
        return -1;
    return 0;
}

static int s32g2_cgm_div_set(uintptr_t cgm, uint32_t mux, uint32_t n, uint32_t dc)
{
    uint32_t val = CGM_DC_DE | CGM_DC_DIV(dc);

    if ((rd32(CGM_MUX_DC(cgm, mux, n)) & (CGM_DC_DE | 0x00FF0000)) == val)
        return 0;
    wr32(CGM_MUX_DC(cgm, mux, n), val);
    return poll_clear(CGM_MUX_DIV_UPD_STAT(cgm, mux), 1);
}

static int fxosc_enable(void)
{
    uintptr_t b = S32G2_FXOSC_BASE;
    uint32_t ctrl;

    /* The Boot ROM may have started it already; its inputs must not change
     * while it runs. */
    if ((rd32(b + FXOSC_CTRL) & FXOSC_CTRL_OSCON) != 0)
        return 0;
    ctrl = FXOSC_CTRL_COMP_EN | FXOSC_CTRL_EOCV(1) | FXOSC_CTRL_GM_SEL(7);
    wr32(b + FXOSC_CTRL, ctrl);
    wr32(b + FXOSC_CTRL, ctrl | FXOSC_CTRL_OSCON);
    return poll_set(b + FXOSC_STAT, FXOSC_STAT_OSC_STAT);
}

/* VCO = FXOSC * MFI / RDIV with RDIV 1, fractional part off; output dividers
 * are disabled around the reprogramming as the PLL is powered down. */
static int pll_program(uintptr_t pll, uint32_t mfi, uint32_t nodivs)
{
    uint32_t n, dv;

    for (n = 0; n < nodivs; n++)
        wr32(pll + PLL_PLLODIV(n), rd32(pll + PLL_PLLODIV(n)) & ~PLL_PLLODIV_DE);
    wr32(pll + PLL_PLLCR, PLL_PLLCR_PLLPD);
    wr32(pll + PLL_PLLCLKMUX, PLL_PLLCLKMUX_FXOSC);
    dv = rd32(pll + PLL_PLLDV) & ~(PLL_PLLDV_RDIV(7) | PLL_PLLDV_MFI(0xFF));
    wr32(pll + PLL_PLLDV, dv | PLL_PLLDV_RDIV(1) | PLL_PLLDV_MFI(mfi));
    wr32(pll + PLL_PLLFD, PLL_PLLFD_SMDEN | PLL_PLLFD_MFN(0));
    wr32(pll + PLL_PLLCR, 0);
    return poll_set(pll + PLL_PLLSR, PLL_PLLSR_LOCK);
}

static void pll_odiv_set(uintptr_t pll, uint32_t n, uint32_t dc)
{
    wr32(pll + PLL_PLLODIV(n), PLL_PLLODIV_DIV(dc));
    wr32(pll + PLL_PLLODIV(n), PLL_PLLODIV_DIV(dc) | PLL_PLLODIV_DE);
}

/* DFS output = VCO / (2 * (MFI + MFN / 36)). Ports are reset all together
 * the first time the DFS is touched, singly afterwards. */
static int dfs_port_set(uintptr_t dfs, uint32_t port, uint32_t mfi, uint32_t mfn)
{
    uint32_t portsr, mask;
    int first;

    portsr = rd32(dfs + DFS_PORTSR);
    if ((portsr & (1U << port)) != 0 &&
            (rd32(dfs + DFS_PORTOLSR) & (1U << port)) == 0 &&
            rd32(dfs + DFS_DVPORT(port)) == DFS_DVPORT_VAL(mfi, mfn))
        return 0;
    first = (portsr == 0);
    mask = first ? DFS_PORTS_MASK : (1U << port);
    wr32(dfs + DFS_PORTOLSR, mask);
    wr32(dfs + DFS_PORTRESET, mask);
    if (poll_clear(dfs + DFS_PORTSR, mask) != 0)
        return -1;
    if (first)
        wr32(dfs + DFS_CTL, DFS_CTL_RESET);
    wr32(dfs + DFS_DVPORT(port), DFS_DVPORT_VAL(mfi, mfn));
    if (first)
        wr32(dfs + DFS_CTL, rd32(dfs + DFS_CTL) & ~DFS_CTL_RESET);
    wr32(dfs + DFS_PORTRESET, rd32(dfs + DFS_PORTRESET) & ~(1U << port));
    if (poll_set(dfs + DFS_PORTSR, 1U << port) != 0)
        return -1;
    if ((rd32(dfs + DFS_PORTOLSR) & (1U << port)) != 0)
        return -1;
    return 0;
}

/* A mode entry update takes effect on the key and inverted key. */
static void mc_me_apply(void)
{
    wr32(S32G2_MC_ME_BASE + MC_ME_CTL_KEY, MC_ME_CTL_KEY_KEY);
    wr32(S32G2_MC_ME_BASE + MC_ME_CTL_KEY, MC_ME_CTL_KEY_INVKEY);
}

/* Clock a block of an MC_ME partition: COFB enable, partition clock enable,
 * then the keyed process update. */
static int s32g2_mc_me_block_enable(uint32_t part, uint32_t block)
{
    uintptr_t me = S32G2_MC_ME_BASE;

    wr32(me + MC_ME_PRTN_COFB0_CLKEN(part),
        rd32(me + MC_ME_PRTN_COFB0_CLKEN(part)) | (1U << block));
    wr32(me + MC_ME_PRTN_PCONF(part),
        rd32(me + MC_ME_PRTN_PCONF(part)) | MC_ME_PRTN_PCONF_PCE);
    wr32(me + MC_ME_PRTN_PUPD(part),
        rd32(me + MC_ME_PRTN_PUPD(part)) | MC_ME_PRTN_PUPD_PCUD);
    mc_me_apply();
    if (poll_set(me + MC_ME_PRTN_STAT(part), MC_ME_PRTN_STAT_PCS) != 0)
        return -1;
    return poll_set(me + MC_ME_PRTN_COFB0_STAT(part), 1U << block);
}

/* A53 at 1 GHz, XBAR_2X 800 MHz, LIN 125 MHz, SDHC 400 MHz, DDR 800 MHz.
 * Returns the number of the step that failed, 0 on success. The console
 * divisor is reprogrammed here once the LIN clock has moved. */
static void s32g2_linflex_setup(uint32_t ibrr, uint32_t fbrr);

static int s32g2_clk_init(void)
{
    /* Park the XBAR on FIRC while its PLL and DFS are reprogrammed. */
    if (s32g2_cgm_mux_set(S32G2_MC_CGM0_BASE, 0, CGM_SRC_FIRC) != 0)
        return 1;
    if (fxosc_enable() != 0)
        return 2;

    /* ARM PLL: 40 MHz * 50 = 2000 MHz VCO, PHI0 = /2 = 1000 MHz -> A53. The
     * Boot ROM already runs the cluster from this PLL, so park the cluster
     * on FIRC first or powering the PLL down stops the core. */
    if (s32g2_cgm_mux_set(S32G2_MC_CGM1_BASE, 0, CGM_SRC_FIRC) != 0)
        return 3;
    if (pll_program(S32G2_ARM_PLL_BASE, 50, ARM_PLL_NODIVS) != 0)
        return 4;
    pll_odiv_set(S32G2_ARM_PLL_BASE, 0, 2);
    if (s32g2_cgm_mux_set(S32G2_MC_CGM1_BASE, 0, CGM_SRC_ARM_PLL_PHI0) != 0)
        return 5;

    /* ARM DFS port 0 (DFS1): 2000 / (2 * 1.25) = 800 MHz -> XBAR_2X. */
    if (dfs_port_set(S32G2_ARM_DFS_BASE, 0, 1, 9) != 0)
        return 6;
    if (s32g2_cgm_mux_set(S32G2_MC_CGM0_BASE, 0, CGM_SRC_ARM_DFS1) != 0)
        return 7;

    /* PERIPH PLL: 2000 MHz VCO, PHI3 = /16 = 125 MHz -> LIN_BAUD. Same
     * precaution for the LIN mux, which the console hangs off. */
    if (s32g2_cgm_mux_set(S32G2_MC_CGM0_BASE, 8, CGM_SRC_FIRC) != 0)
        return 8;
    if (pll_program(S32G2_PERIPH_PLL_BASE, 50, PERIPH_PLL_NODIVS) != 0)
        return 9;
    pll_odiv_set(S32G2_PERIPH_PLL_BASE, 3, 16);
    if (s32g2_cgm_mux_set(S32G2_MC_CGM0_BASE, 8, CGM_SRC_PERIPH_PLL_PHI3) != 0)
        return 10;
    s32g2_linflex_setup(S32G2_LINIBRR_115200, S32G2_LINFBRR_115200);

    /* PERIPH DFS port 2 (DFS3): 800 MHz, /2 at the mux -> SDHC 400 MHz. */
    if (dfs_port_set(S32G2_PERIPH_DFS_BASE, 2, 1, 9) != 0)
        return 11;
    if (s32g2_cgm_div_set(S32G2_MC_CGM0_BASE, 14, 0, 2) != 0)
        return 12;
    if (s32g2_cgm_mux_set(S32G2_MC_CGM0_BASE, 14, CGM_SRC_PERIPH_DFS3) != 0)
        return 13;
    if (s32g2_mc_me_block_enable(0, S32G2_MC_ME_USDHC_BLOCK) != 0)
        return 14;

    /* DDR PLL: 40 MHz * 40 = 1600 MHz VCO, PHI0 = /2 = 800 MHz -> DDR. */
    if (pll_program(S32G2_DDR_PLL_BASE, 40, DDR_PLL_NODIVS) != 0)
        return 15;
    pll_odiv_set(S32G2_DDR_PLL_BASE, 0, 2);
    if (s32g2_cgm_mux_set(S32G2_MC_CGM5_BASE, 0, CGM_SRC_DDR_PLL_PHI0) != 0)
        return 16;
    if (s32g2_mc_me_block_enable(0, S32G2_MC_ME_DDR_BLOCK) != 0)
        return 17;
    /* PERIPH DFS port 0 (DFS1): 800 MHz, divided at the mux to QSPI_2X;
     * the serial clock is half of that (/16 = 25 MHz, /8 = 50 MHz). Set up
     * in every build: the OS QuadSPI driver expects this source selected. */
    if (dfs_port_set(S32G2_PERIPH_DFS_BASE, 0, 1, 9) != 0)
        return 18;
    if (s32g2_cgm_div_set(S32G2_MC_CGM0_BASE, 12, 0, S32G2_QSPI_2X_DIV) != 0)
        return 19;
    if (s32g2_cgm_mux_set(S32G2_MC_CGM0_BASE, 12, CGM_SRC_PERIPH_DFS1) != 0)
        return 20;
    return 0;
}

/* VR5510 PMIC on I2C4: take the fail-safe unit out of its INIT_FS watchdog
 * expectations so the board is not reset once the challenger window elapses.
 * The I2C controller is the byte-register (IBFD/IBCR/IBSR/IBDR) type, driven
 * in polled master mode. */
#define I2C_IBFD        0x1
#define I2C_IBCR        0x2
#define I2C_IBSR        0x3
#define I2C_IBDR        0x4
#define IBCR_MDIS       (1U << 7)
#define IBCR_MSSL       (1U << 5)
#define IBCR_TX         (1U << 4)
#define IBCR_NOACK      (1U << 3)
#define IBCR_RSTA       (1U << 2)
#define IBSR_IBB        (1U << 5)
#define IBSR_IBAL       (1U << 4)
#define IBSR_IBIF       (1U << 1)
#define IBSR_RXAK       (1U << 0)

#define I2C_POLL_MAX    200000U

/* The PMIC is set up before the clock tree, with the controller clock
 * (XBAR / 2 / 3) still at 8 MHz from FIRC: divider 80 gives 100 kHz. */
#define I2C_IBFD_DIV80  0x14

static void i2c_init(uintptr_t b)
{
    wr8(b + I2C_IBFD, I2C_IBFD_DIV80);
    wr8(b + I2C_IBCR, IBCR_MDIS);
    wr8(b + I2C_IBSR, 0);
}

static int i2c_wait_idle(uintptr_t b)
{
    uint32_t n;

    for (n = 0; n < I2C_POLL_MAX; n++) {
        if ((rd8(b + I2C_IBSR) & IBSR_IBB) == 0)
            return 0;
    }
    return -1;
}

static int i2c_wait_done(uintptr_t b)
{
    uint32_t n;
    uint8_t sr;

    for (n = 0; n < I2C_POLL_MAX; n++) {
        sr = rd8(b + I2C_IBSR);
        if ((sr & IBSR_IBAL) != 0) {
            wr8(b + I2C_IBSR, IBSR_IBAL);
            return -1;
        }
        if ((sr & IBSR_IBIF) != 0) {
            wr8(b + I2C_IBSR, IBSR_IBIF);
            return 0;
        }
    }
    return -1;
}

static int i2c_tx_byte(uintptr_t b, uint8_t v)
{
    wr8(b + I2C_IBDR, v);
    if (i2c_wait_done(b) != 0)
        return -1;
    return ((rd8(b + I2C_IBSR) & IBSR_RXAK) != 0) ? -1 : 0;
}

static int i2c_stop(uintptr_t b)
{
    wr8(b + I2C_IBCR, rd8(b + I2C_IBCR) & ~IBCR_MSSL);
    return i2c_wait_idle(b);
}

/* START, chip address in write mode, register address. */
static int i2c_start(uintptr_t b, uint8_t chip, uint8_t reg)
{
    wr8(b + I2C_IBCR, IBCR_MDIS);
    wr8(b + I2C_IBSR, 0);
    wr8(b + I2C_IBCR, 0);
    wr8(b + I2C_IBSR, IBSR_IBIF);
    if (i2c_wait_idle(b) != 0)
        return -1;
    wr8(b + I2C_IBCR, rd8(b + I2C_IBCR) | IBCR_MSSL);
    wr8(b + I2C_IBCR, rd8(b + I2C_IBCR) | IBCR_TX | IBCR_NOACK);
    if (i2c_tx_byte(b, (uint8_t)(chip << 1)) != 0)
        return -1;
    return i2c_tx_byte(b, reg);
}

static int i2c_write(uintptr_t b, uint8_t chip, uint8_t reg,
    const uint8_t* buf, int len)
{
    int i, ret = 0;

    if (i2c_start(b, chip, reg) != 0) {
        i2c_stop(b);
        return -1;
    }
    for (i = 0; i < len; i++) {
        ret = i2c_tx_byte(b, buf[i]);
        if (ret != 0)
            break;
    }
    i2c_stop(b);
    return ret;
}

static int i2c_read(uintptr_t b, uint8_t chip, uint8_t reg, uint8_t* buf, int len)
{
    int i;
    uint8_t cr;

    if (i2c_start(b, chip, reg) != 0) {
        i2c_stop(b);
        return -1;
    }
    /* Repeated START, then the chip address in read mode. */
    wr8(b + I2C_IBCR, rd8(b + I2C_IBCR) | IBCR_RSTA);
    if (i2c_tx_byte(b, (uint8_t)((chip << 1) | 1)) != 0) {
        i2c_stop(b);
        return -1;
    }
    cr = rd8(b + I2C_IBCR) & ~IBCR_TX;
    if (len == 1)
        cr |= IBCR_NOACK;
    else
        cr &= ~IBCR_NOACK;
    wr8(b + I2C_IBCR, cr);
    (void)rd8(b + I2C_IBDR);    /* dummy read starts the receive */
    for (i = 0; i < len; i++) {
        if (i2c_wait_done(b) != 0) {
            i2c_stop(b);
            return -1;
        }
        if (i == len - 2)
            wr8(b + I2C_IBCR, rd8(b + I2C_IBCR) | IBCR_NOACK);
        else if (i == len - 1)
            wr8(b + I2C_IBCR, rd8(b + I2C_IBCR) & ~IBCR_MSSL);
        buf[i] = rd8(b + I2C_IBDR);
    }
    i2c_stop(b);
    return 0;
}

/* VR5510 register access: 16-bit registers, big-endian on the wire, followed
 * by a CRC-8 (poly 0x1D, seed 0xFF) over {chip<<1|rw, reg, data_hi, data_lo}. */
#define VR5510_CRC_POLY         0x1D
#define VR5510_CRC_SEED         0xFF

static uint8_t vr5510_crc(const uint8_t* d, int len)
{
    uint8_t crc = VR5510_CRC_SEED;
    int i, bit;

    for (i = 0; i < len; i++) {
        crc ^= d[i];
        for (bit = 0; bit < 8; bit++) {
            crc = (uint8_t)((crc & 0x80) ? ((crc << 1) ^ VR5510_CRC_POLY) : (crc << 1));
        }
    }
    return crc;
}

static int vr5510_read(uint8_t reg, uint16_t* val)
{
    const uint8_t chip = S32G2_VR5510_FSU_ADDR;
    uint8_t frame[5];

    if (i2c_read(S32G2_I2C4_BASE, chip, reg, &frame[2], 3) != 0)
        return -1;
    frame[0] = (uint8_t)((chip << 1) | 1);
    frame[1] = reg;
    if (vr5510_crc(frame, 4) != frame[4])
        return -2;
    *val = (uint16_t)(((uint16_t)frame[2] << 8) | frame[3]);
    return 0;
}

static int vr5510_write(uint8_t reg, uint16_t val)
{
    const uint8_t chip = S32G2_VR5510_FSU_ADDR;
    uint8_t frame[5];

    frame[0] = (uint8_t)(chip << 1);
    frame[1] = reg;
    frame[2] = (uint8_t)(val >> 8);
    frame[3] = (uint8_t)val;
    frame[4] = vr5510_crc(frame, 4);
    return i2c_write(S32G2_I2C4_BASE, chip, reg, &frame[2], 3);
}

/* Fail-safe unit registers */
#define VR5510_FS_I_WD_CFG          7
#define VR5510_FS_I_SAFE_INPUTS     9
#define VR5510_FS_I_NOT_SAFE_INPUTS 10
#define VR5510_FS_WD_WINDOW         15
#define VR5510_FS_NOT_WD_WINDOW     16
#define VR5510_FS_WD_SEED           17
#define VR5510_FS_WD_ANSWER         18
#define VR5510_FS_DIAG_SAFETY       22
#define VR5510_FS_STATES            24
#define VR5510_STATE_MASK           0x1F
#define VR5510_STATE_INIT_FS        6
#define VR5510_WD_WINDOW_MASK       0xF000
#define VR5510_FCCU_CFG_MASK        (3U << 14)
#define VR5510_ABIST1_OK            (1U << 10)
#define VR5510_ERR_CNT_MASK         0xF

/* Answer the challenger watchdog once: the seed is transformed as the
 * datasheet prescribes and the error counter must read zero afterwards. */
static int vr5510_wd_refresh(void)
{
    uint16_t reg;
    uint32_t answer;

    if (vr5510_read(VR5510_FS_WD_SEED, &reg) != 0)
        return -1;
    answer = ((~((uint32_t)reg * 4 + 2)) / 4) & 0xFFFFU;
    if (vr5510_write(VR5510_FS_WD_ANSWER, (uint16_t)answer) != 0)
        return -2;
    if (vr5510_read(VR5510_FS_I_WD_CFG, &reg) != 0)
        return -3;
    return ((reg & VR5510_ERR_CNT_MASK) != 0) ? -4 : 0;
}

static int s32g2_pmic_init(void)
{
    uint16_t reg;
    int ret;

    /* I2C4 pads (PC01 SDA, PC02 SCL): open drain, both directions. */
    wr32(S32G2_SIUL2_0_MSCR_BASE + 4 * S32G2_I2C4_SDA_MSCR, S32G2_I2C4_SDA_MSCR_VAL);
    wr32(S32G2_SIUL2_0_MSCR_BASE + 4 * S32G2_I2C4_SCL_MSCR, S32G2_I2C4_SCL_MSCR_VAL);
    wr32(S32G2_SIUL2_1_IMCR_BASE + 4 * (S32G2_I2C4_SDA_IMCR - S32G2_SIUL2_1_IMCR_START),
        S32G2_I2C4_IMCR_VAL);
    wr32(S32G2_SIUL2_1_IMCR_BASE + 4 * (S32G2_I2C4_SCL_IMCR - S32G2_SIUL2_1_IMCR_START),
        S32G2_I2C4_IMCR_VAL);
    i2c_init(S32G2_I2C4_BASE);

    ret = vr5510_read(VR5510_FS_STATES, &reg);
    if (ret != 0) {
        wolfBoot_printf("s32g2: VR5510 FS_STATES read failed (%d)\n", ret);
        return -1;
    }
    if ((reg & VR5510_STATE_MASK) != VR5510_STATE_INIT_FS)
        return 0;   /* an earlier stage already took it out of INIT_FS */

    /* Watchdog window off, written with its complement as the FSU requires. */
    if (vr5510_read(VR5510_FS_WD_WINDOW, &reg) != 0)
        return -2;
    reg &= (uint16_t)~VR5510_WD_WINDOW_MASK;
    if (vr5510_write(VR5510_FS_WD_WINDOW, reg) != 0 ||
            vr5510_write(VR5510_FS_NOT_WD_WINDOW, (uint16_t)~reg) != 0)
        return -3;

    if (vr5510_read(VR5510_FS_DIAG_SAFETY, &reg) != 0)
        return -4;
    if ((reg & VR5510_ABIST1_OK) == 0) {
        wolfBoot_printf("s32g2: VR5510 ABIST1 not OK (0x%04x)\n", reg);
        return -5;
    }

    /* FCCU monitoring off, same complement write. */
    if (vr5510_read(VR5510_FS_I_SAFE_INPUTS, &reg) != 0)
        return -6;
    reg &= (uint16_t)~VR5510_FCCU_CFG_MASK;
    if (vr5510_write(VR5510_FS_I_SAFE_INPUTS, reg) != 0 ||
            vr5510_write(VR5510_FS_I_NOT_SAFE_INPUTS, (uint16_t)~reg) != 0)
        return -7;

    ret = vr5510_wd_refresh();
    if (ret != 0) {
        wolfBoot_printf("s32g2: VR5510 watchdog refresh failed (%d)\n", ret);
        return -8;
    }
    return 0;
}

/* What the OS expects an EL3 firmware to have done for a four-core A53
 * system: the Ncore interconnect and the MSCM interrupt router, the GICv3
 * secure-side setup on each core, and the three secondary cores released
 * and parked at EL3 until the kernel turns them on through PSCI. */

/* Ncore coherent interconnect: directory snoop filters online, then the
 * cluster's CAIU admitted to snooping and DVM. */
#define NCORE_CSR               (S32G2_NCORE_BASE + 0xFF000)
#define NCORE_CSADSER           (NCORE_CSR + 0x40)
#define NCORE_CSUIDR            (NCORE_CSR + 0xFF8)
#define NCORE_CSIDR             (NCORE_CSR + 0xFFC)
#define NCORE_DIRU(n)           (S32G2_NCORE_BASE + 0x80000 + (n) * 0x1000)
#define NCORE_DIRUSFER(n)       (NCORE_DIRU(n) + 0x10)
#define NCORE_DIRUCASER(n)      (NCORE_DIRU(n) + 0x40)
#define NCORE_DIRUSFMCR(n)      (NCORE_DIRU(n) + 0x80)
#define NCORE_DIRUSFMAR(n)      (NCORE_DIRU(n) + 0x84)
#define NCORE_CAIUIDR(n)        (S32G2_NCORE_BASE + (n) * 0x1000 + 0xFFC)
#define CSUIDR_NUMDIRUS(v)      (((v) >> 16) & 0x3F)
#define CSIDR_NUMSFS(v)         (((v) >> 18) & 0x1F)
#define CAIUIDR_TYPE_MASK       (0xFU << 16)
#define CAIUIDR_TYPE_ACE_DVM    (0x0U << 16)

static void ncore_caiu_online(uint32_t caiu)
{
    uint32_t numdirus = CSUIDR_NUMDIRUS(rd32(NCORE_CSUIDR));
    uint32_t diru;

    for (diru = 0; diru < numdirus; diru++)
        wr32(NCORE_DIRUCASER(diru), rd32(NCORE_DIRUCASER(diru)) | (1U << caiu));
    if ((rd32(NCORE_CAIUIDR(caiu)) & CAIUIDR_TYPE_MASK) == CAIUIDR_TYPE_ACE_DVM)
        wr32(NCORE_CSADSER, rd32(NCORE_CSADSER) | (1U << caiu));
}

static int s32g2_ncore_init(void)
{
    uint32_t numdirus = CSUIDR_NUMDIRUS(rd32(NCORE_CSUIDR));
    uint32_t numsfs = CSIDR_NUMSFS(rd32(NCORE_CSIDR)) + 1;
    uint32_t diru, sf;

    /* The ROM isolated cluster 0 from the interconnect; drop that first. */
    wr32(S32G2_NCORE_CAIUTC, rd32(S32G2_NCORE_CAIUTC) & ~S32G2_NCORE_CAIUTC_ISOLEN);
    for (diru = 0; diru < numdirus; diru++) {
        for (sf = 0; sf < numsfs; sf++) {
            wr32(NCORE_DIRUSFMCR(diru), sf << 16);   /* initialise all entries */
            if (poll_clear(NCORE_DIRUSFMAR(diru), 1U) != 0)
                return -1;
            wr32(NCORE_DIRUSFER(diru), rd32(NCORE_DIRUSFER(diru)) | (1U << sf));
        }
    }
    /* Cluster 1's port joins when its cores are released: a port admitted
     * to DVM with its cores in reset never answers a DVM sync, and the DSB
     * after a TLB invalidate then hangs. */
    ncore_caiu_online(0);
    return 0;
}

/* MSCM interrupt router: every shared peripheral interrupt to all four cores. */
static void s32g2_mscm_init(void)
{
    uint32_t i;

    for (i = 0; i < S32G2_MSCM_IRSPRC_NUM; i++)
        wr16(S32G2_MSCM_BASE + S32G2_MSCM_IRSPRC + i * 2, 0xF);
}

/* GICv3 distributor: affinity routing on, every SPI in Group 1 non-secure. */
static void s32g2_gic_init_dist(void)
{
    uintptr_t d = S32G2_GICD_BASE;
    uint32_t lines = rd32(d + GICD_TYPER) & 0x1F;
    uint32_t i;

    wr32(d + GICD_CTLR, GICD_CTLR_ARE_NS | GICD_CTLR_ARE_S | GICD_CTLR_ENGRP1S |
        GICD_CTLR_ENGRP1NS | GICD_CTLR_ENGRP0);
    for (i = 1; i <= lines; i++) {
        wr32(d + GICD_IGROUPR + i * 4, 0xFFFFFFFFU);
        wr32(d + GICD_IGROUPMODR + i * 4, 0);
    }
}

/* GICv3 redistributor and CPU interface of the calling core, at EL3. */
static uintptr_t s32g2_gicr_self(void)
{
    uintptr_t r = S32G2_GICR_BASE;
    uint64_t mpidr, aff;
    uint32_t n;

    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    aff = ((mpidr >> 32) & 0xFF) << 24 | (mpidr & 0x00FFFFFF);
    for (n = 0; n < 8; n++, r += S32G2_GICR_STRIDE) {
        if ((rd64(r + GICR_TYPER) >> 32) == aff)
            return r;
    }
    return 0;
}

static void s32g2_gic_init_cpu(void)
{
    uintptr_t r = s32g2_gicr_self();

    if (r == 0)
        return;
    wr32(r + GICR_WAKER, rd32(r + GICR_WAKER) & ~GICR_WAKER_PROCESSORSLEEP);
    __asm__ volatile("dsb st; isb");
    if (poll_clear(r + GICR_WAKER, GICR_WAKER_CHILDRENASLEEP) != 0) {
        /* The SGI and PPI frame must not be touched while it sleeps. */
        wolfBoot_printf("s32g2: GIC redistributor did not wake\n");
        wolfBoot_panic();
    }
    wr32(r + GICR_SGI_BASE + GICR_IGROUPR0, 0xFFFFFFFFU);
    wr32(r + GICR_SGI_BASE + GICR_IGROUPMODR0, 0);
    wr32(r + GICR_SGI_BASE + GICR_ISENABLER0, 1);

    __asm__ volatile(
        "mrs x0, icc_sre_el3\n"
        "orr x0, x0, #0xf\n"
        "msr icc_sre_el3, x0\n"
        "isb\n"
        "mov x0, #0x3\n"
        "msr icc_igrpen1_el3, x0\n"
        "isb\n"
        "msr icc_ctlr_el3, xzr\n"
        "isb\n"
        "mrs x0, icc_sre_el2\n"
        "orr x0, x0, #0xf\n"
        "msr icc_sre_el2, x0\n"
        "isb\n"
        "msr icc_ctlr_el1, xzr\n"
        "isb\n"
        "mov x0, #0x80\n"
        "msr icc_pmr_el1, x0\n"
        "isb\n"
        ::: "x0", "memory");
}

/* The calling core is going off: no interrupt groups, redistributor asleep.
 * s32g2_gic_init_cpu() undoes it when the core is turned on again. */
static void s32g2_gic_sleep_cpu(void)
{
    uintptr_t r = s32g2_gicr_self();

    __asm__ volatile(
        "msr icc_igrpen1_el3, xzr\n"
        "msr icc_igrpen0_el1, xzr\n"
        "isb\n");
    if (r == 0)
        return;
    wr32(r + GICR_WAKER, rd32(r + GICR_WAKER) | GICR_WAKER_PROCESSORSLEEP);
    (void)poll_set(r + GICR_WAKER, GICR_WAKER_CHILDRENASLEEP);
}

static void s32g2_el3_mmu_on(void);

/* Called by each secondary core from the startup code, on its own SRAM
 * stack, before it parks at EL3 (s32g2_psci_wait). */
void s32g2_secondary_main(void);
void s32g2_secondary_main(void)
{
    __asm__ volatile("msr cntfrq_el0, %0" :: "r"((uint64_t)S32G2_CNTFRQ_HZ));
    s32g2_gic_init_cpu();
    /* The PSCI state is shared between the cores and locked with exclusive
     * accesses, which need the caches. */
    s32g2_el3_mmu_on();
}

/* PSCI platform side (src/psci.c). The SMC vector and the parking loop's
 * frame are in src/boot_aarch64_start.S. */
#if WOLFBOOT_PSCI_NUM_CPUS != S32G2_NUM_CORES
#error "WOLFBOOT_PSCI_NUM_CPUS must match the S32G2 core count"
#endif

struct s32g2_psci_start {
    uint64_t entry;
    uint64_t context;
};

void s32g2_psci_park(void) __attribute__((noreturn));

/* A parked core waits here, at EL3 on its own stack, until CPU_ON names it;
 * the startup code then enters the kernel at EL2. */
struct s32g2_psci_start s32g2_psci_wait(uint32_t id);
struct s32g2_psci_start s32g2_psci_wait(uint32_t id)
{
    struct s32g2_psci_start start;

    start.entry = 0;
    start.context = 0;
    while (!wolfBoot_psci_cpu_take(id, &start.entry, &start.context))
        __asm__ volatile("wfe");
    s32g2_gic_init_cpu();
    return start;
}

int hal_psci_mpidr_to_cpu(uint64_t mpidr)
{
    /* two clusters of two cores: aff1 and aff0 are each 0 or 1 */
    if ((mpidr & ~0x101ULL) != 0)
        return -1;
    return (int)(((mpidr >> 8) & 1) * 2 + (mpidr & 1));
}

unsigned int hal_psci_this_cpu(void)
{
    uint64_t mpidr;

    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return (unsigned int)(((mpidr >> 8) & 0xFF) * 2 + (mpidr & 0x3));
}

int hal_psci_entry_ok(uint64_t entry)
{
    if ((entry & 3) != 0)
        return 0;
    if (entry >= S32G2_DRAM_BASE &&
            entry - S32G2_DRAM_BASE < S32G2_DRAM_BANK0_SIZE)
        return 1;
    return (entry >= S32G2_DRAM_BANK1_BASE &&
        entry - S32G2_DRAM_BANK1_BASE < S32G2_DRAM_BANK1_SIZE);
}

void hal_psci_cpu_wake(unsigned int cpu)
{
    /* every parked core wakes and looks at its own state */
    (void)cpu;
    __asm__ volatile("dsb sy; sev");
}

void hal_psci_cpu_off(void)
{
    s32g2_gic_sleep_cpu();
    /* The kernel's timer event stream would wake the parked core thousands
     * of times a second; the kernel sets it up again on the way back. */
    __asm__ volatile(
        "mrs x0, cnthctl_el2\n"
        "bic x0, x0, #(1 << 2)\n"
        "msr cnthctl_el2, x0\n"
        "mrs x0, cntkctl_el1\n"
        "bic x0, x0, #(1 << 2)\n"
        "msr cntkctl_el1, x0\n"
        "isb\n"
        ::: "x0");
    s32g2_psci_park();
}

/* Functional reset of the SoC through the mode entry module: the Boot ROM
 * runs again and reloads wolfBoot. */
#define MC_ME_MODE_CONF             0x004
#define MC_ME_MODE_UPD              0x008
#define MC_ME_MODE_CONF_FUNC_RST    (1U << 1)
#define MC_ME_MODE_UPD_UPD          (1U << 0)
void hal_psci_system_reset(void)
{
    wr32(S32G2_MC_ME_BASE + MC_ME_MODE_CONF, MC_ME_MODE_CONF_FUNC_RST);
    wr32(S32G2_MC_ME_BASE + MC_ME_MODE_UPD, MC_ME_MODE_UPD_UPD);
    mc_me_apply();
    for (;;)
        __asm__ volatile("wfi");
}

/* The PMIC is left on: the calling core stops here, and the kernel has
 * already stopped the others. */
void hal_psci_system_off(void)
{
    for (;;)
        __asm__ volatile("wfi");
}

/* MC_ME core enable and MC_RGM reset release for the secondaries. The core
 * start address is this image's entry; the startup code sends secondaries
 * to s32g2_secondary_entry. Cluster 1 (cores 2, 3) first needs its CAIU. */
#define MC_ME_PRTN_CORE(n, m)       (S32G2_MC_ME_BASE + 0x140 + (n) * 0x200 + (m) * 0x20)
#define MC_ME_CORE_PCONF            0x0
#define MC_ME_CORE_PUPD             0x4
#define MC_ME_CORE_STAT             0x8
#define MC_ME_CORE_ADDR             0xC
#define MC_ME_CORE_CCE              1U
#define MC_ME_CORE_CCUPD            1U
#define MC_ME_CORE_CCS              1U
#define MC_ME_A53_PART              1
#define MC_RGM_PRST(g)              (S32G2_MC_RGM_BASE + 0x40 + (g) * 8)
#define MC_RGM_PSTAT(g)             (S32G2_MC_RGM_BASE + 0x140 + (g) * 8)
#define MC_RGM_A53_GROUP            1

static int s32g2_core_release(uint32_t core, uintptr_t entry)
{
    uintptr_t cl = MC_ME_PRTN_CORE(MC_ME_A53_PART, core & ~1U);
    uint32_t bit = 1U << (core + 1);

    wr32(MC_ME_PRTN_CORE(MC_ME_A53_PART, core) + MC_ME_CORE_ADDR,
        (uint32_t)entry & 0xFFFFFFFCU);
    /* Clock enable is per cluster, defined on its first core's registers. */
    if ((rd32(cl + MC_ME_CORE_STAT) & MC_ME_CORE_CCS) == 0) {
        wr32(cl + MC_ME_CORE_PCONF, MC_ME_CORE_CCE);
        wr32(cl + MC_ME_CORE_PUPD, MC_ME_CORE_CCUPD);
        mc_me_apply();
        if (poll_set(cl + MC_ME_CORE_STAT, MC_ME_CORE_CCS) != 0)
            return -1;
    }
    wr32(MC_RGM_PRST(MC_RGM_A53_GROUP), rd32(MC_RGM_PRST(MC_RGM_A53_GROUP)) & ~bit);
    return (poll_clear(MC_RGM_PSTAT(MC_RGM_A53_GROUP), bit) != 0) ? -2 : 0;
}

static int s32g2_smp_release(uintptr_t entry)
{
    uint32_t core, n;
    int ret;

    wolfBoot_psci_init(0);

    wr32(S32G2_MC_ME_BASE + MC_ME_PRTN_PCONF(MC_ME_A53_PART), 1U);
    wr32(S32G2_MC_ME_BASE + MC_ME_PRTN_PUPD(MC_ME_A53_PART), 1U);
    mc_me_apply();

    for (core = 1; core < S32G2_NUM_CORES; core++) {
        if (core == 2)
            ncore_caiu_online(1);
        ret = s32g2_core_release(core, entry);
        if (ret != 0) {
            wolfBoot_printf("s32g2: core %u release failed (%d)\n", core, ret);
            return ret;
        }
        for (n = 0; n < POLL_MAX; n++) {
            if (wolfBoot_psci_cpu_alive(core))
                break;
        }
        wolfBoot_printf("s32g2: core %u %s\n", core,
            (n == POLL_MAX) ? "did not check in" : "parked");
    }
    return 0;
}

/* QuadSPI controller driving the GoldBox's 64 MB Macronix MX25UW51245G NOR
 * in plain SPI mode (one data line, SDR, 4-byte addresses). Single commands
 * are IP commands described by a LUT sequence; bulk reads go through the
 * memory-mapped AHB window. */
#ifdef S32G2_QSPI

#define QSPI_MCR                    0x000
#define QSPI_MCR_SWRSTSD            (1U << 0)
#define QSPI_MCR_SWRSTHD            (1U << 1)
#define QSPI_MCR_END_CFG            (3U << 2)
#define QSPI_MCR_DQS_EN             (1U << 6)
#define QSPI_MCR_CLR_RXF            (1U << 10)
#define QSPI_MCR_CLR_TXF            (1U << 11)
#define QSPI_MCR_MDIS               (1U << 14)
#define QSPI_MCR_RESERVED           (0xFU << 16)
#define QSPI_MCR_DQS_INT_PAD        (1U << 24)
#define QSPI_IPCR                   0x008
#define QSPI_IPCR_SEQID(s)          ((uint32_t)(s) << 24)
#define QSPI_FLSHCR                 0x00C
#define QSPI_FLSHCR_TCSS(n)         ((uint32_t)(n) << 0)
#define QSPI_FLSHCR_TCSH(n)         ((uint32_t)(n) << 8)
#define QSPI_BUF3CR                 0x01C
#define QSPI_BUF3CR_ALLMST          (1U << 31)
#define QSPI_BUF3CR_ADATSZ(n)       ((uint32_t)(n) << 8)
#define QSPI_BFGENCR                0x020
#define QSPI_BFGENCR_SEQID(s)       ((uint32_t)(s) << 12)
#define QSPI_BUF0IND                0x030
#define QSPI_BUF1IND                0x034
#define QSPI_BUF2IND                0x038
#define QSPI_DLLCRA                 0x060
#define QSPI_DLLCR_SLV_UPD_EN       (1U << 0)
#define QSPI_DLLCR_SLV_BYPASS_EN    (1U << 1)
#define QSPI_DLLCR_SLV_EN           (1U << 2)
#define QSPI_SFAR                   0x100
#define QSPI_SFACR                  0x104
#define QSPI_SMPR                   0x108
#define QSPI_SMPR_FSPHS             (1U << 5)
#define QSPI_RBCT                   0x110
#define QSPI_RBCT_WMRK_MASK         0x1FU
#define QSPI_RBCT_RXBRD_USEIPS      (1U << 8)
#define QSPI_DLLSR                  0x12C
#define QSPI_DLLSR_SLVA_LOCK        (1U << 14)
#define QSPI_TBSR                   0x150
#define QSPI_TBSR_TRCTR(v)          ((v) >> 16)
#define QSPI_TBSR_TRBFL(v)          ((v) & 0xFFU)
#define QSPI_TBDR                   0x154
#define QSPI_SR                     0x15C
#define QSPI_SR_BUSY                (1U << 0)
#define QSPI_SR_IP_ACC              (1U << 1)
#define QSPI_SR_AHB_ACC             (1U << 2)
#define QSPI_SPTRCLR                0x16C
#define QSPI_SPTRCLR_BFPTRC         (1U << 0)
#define QSPI_SPTRCLR_IPPTRC         (1U << 8)
#define QSPI_SFA1AD                 0x180
#define QSPI_SFA2AD                 0x184
#define QSPI_SFB1AD                 0x188
#define QSPI_SFB2AD                 0x18C
#define QSPI_RBDR(n)                (0x200 + 4 * (n))
#define QSPI_LUTKEY                 0x300
#define QSPI_LUTKEY_VALUE           0x5AF05AF0U
#define QSPI_LCKCR                  0x304
#define QSPI_LCKCR_LOCK             1U
#define QSPI_LCKCR_UNLOCK           2U
#define QSPI_LUT_BASE               0x310
#define QSPI_LUT_WORDS              5   /* words per sequence on S32 */
#define QSPI_LUT(seq, i)            (QSPI_LUT_BASE + ((seq) * QSPI_LUT_WORDS + (i)) * 4)
#define QSPI_SEQ_IP                 15
#define QSPI_SEQ_AHB                14

#define LUT_STOP                    0
#define LUT_CMD                     1
#define LUT_ADDR                    2
#define LUT_DUMMY                   3
#define LUT_READ                    7
#define LUT_WRITE                   8
#define LUT_CMD_DDR                 17
/* instruction[15:10] | pad[9:8] (0 = one line, 3 = eight) | operand[7:0] */
#define LUT_ENTRY(ins, opr)         ((uint16_t)(((ins) << 10) | ((opr) & 0xFF)))
#define LUT_ENTRY_OCTAL(ins, opr)   ((uint16_t)(((ins) << 10) | (3 << 8) | ((opr) & 0xFF)))
#define QSPI_MCR_DDR_EN             (1U << 7)
#define QSPI_FLSHCR_TDH(n)          ((uint32_t)(n) << 16)

/* The RX buffer holds 128 bytes; one IP command moves at most that much. */
#define QSPI_XFER_MAX               128U

#define FLASH_ID_MX25UW51245G       { 0xC2, 0x81, 0x3A }
#define FLASH_CMD_RSTEN             0x66
#define FLASH_CMD_RST               0x99
#define FLASH_CMD_RDID              0x9F
#define FLASH_CMD_RDSR              0x05
#define FLASH_CMD_WREN              0x06
#define FLASH_CMD_READ4B            0x0C    /* fast read, 8 dummy cycles */
#define FLASH_CMD_PP4B              0x12
#define FLASH_CMD_SE4B              0x21    /* 4 KB sector */
#define FLASH_SR_WIP                0x01
#define FLASH_PAGE_SIZE             256U
#define FLASH_SECTOR_SIZE           4096U

#define FLASH_WIP_POLL_MAX          200000U    /* sector erase takes up to ~400 ms */

static void qspi_ahb_setup(void);

static int qspi_wait_idle(void)
{
    return poll_clear(S32G2_QSPI_BASE + QSPI_SR,
        QSPI_SR_BUSY | QSPI_SR_IP_ACC | QSPI_SR_AHB_ACC);
}

static void qspi_lut_program(uint32_t seq, const uint16_t* e, uint32_t n)
{
    uintptr_t b = S32G2_QSPI_BASE;
    uint32_t i, w;

    wr32(b + QSPI_LUTKEY, QSPI_LUTKEY_VALUE);
    wr32(b + QSPI_LCKCR, QSPI_LCKCR_UNLOCK);
    for (i = 0; i < QSPI_LUT_WORDS; i++) {
        w = 0;
        if (2 * i < n)
            w |= e[2 * i];
        if (2 * i + 1 < n)
            w |= (uint32_t)e[2 * i + 1] << 16;
        wr32(b + QSPI_LUT(seq, i), w);
    }
    wr32(b + QSPI_LUTKEY, QSPI_LUTKEY_VALUE);
    wr32(b + QSPI_LCKCR, QSPI_LCKCR_LOCK);
}

/* One IP command: opcode, optional 32-bit address, dummy cycles, then up to
 * QSPI_XFER_MAX bytes out of tx or into rx. */
static int qspi_op(uint8_t cmd, int has_addr, uint32_t addr, uint8_t dummy,
    const uint8_t* tx, uint8_t* rx, uint32_t len)
{
    uintptr_t b = S32G2_QSPI_BASE;
    uint16_t e[6];
    uint32_t n = 0, i, words, v, spin;

    if (len > QSPI_XFER_MAX)
        return -1;
    e[n++] = LUT_ENTRY(LUT_CMD, cmd);
    if (has_addr)
        e[n++] = LUT_ENTRY(LUT_ADDR, 0x20);     /* 32 address bits */
    if (dummy != 0)
        e[n++] = LUT_ENTRY(LUT_DUMMY, dummy);
    if (len != 0)
        e[n++] = LUT_ENTRY(rx != NULL ? LUT_READ : LUT_WRITE, len);
    e[n++] = LUT_ENTRY(LUT_STOP, 0);

    if (qspi_wait_idle() != 0)
        return -1;
    wr32(b + QSPI_SFAR, addr);
    wr32(b + QSPI_MCR, rd32(b + QSPI_MCR) | QSPI_MCR_CLR_RXF | QSPI_MCR_CLR_TXF);
    wr32(b + QSPI_SPTRCLR, QSPI_SPTRCLR_BFPTRC | QSPI_SPTRCLR_IPPTRC);
    qspi_lut_program(QSPI_SEQ_IP, e, n);
    wr32(b + QSPI_RBCT, QSPI_RBCT_WMRK_MASK | QSPI_RBCT_RXBRD_USEIPS);

    words = (len + 3) / 4;
    if (tx != NULL && len != 0) {
        if (poll_clear(b + QSPI_TBSR, QSPI_TBSR_TRBFL(~0U)) != 0)
            return -1;
        for (i = 0; i < words; i++) {
            v = 0;
            memcpy(&v, tx + 4 * i, (len - 4 * i < 4) ? (len - 4 * i) : 4);
            wr32(b + QSPI_TBDR, v);
        }
    }
    wr32(b + QSPI_IPCR, len | QSPI_IPCR_SEQID(QSPI_SEQ_IP));
    if (tx != NULL && len != 0) {
        for (spin = 0; spin < POLL_MAX; spin++) {
            v = rd32(b + QSPI_TBSR);
            if (QSPI_TBSR_TRCTR(v) == words && QSPI_TBSR_TRBFL(v) == 0)
                break;
        }
        if (spin == POLL_MAX)
            return -1;
    }
    if (qspi_wait_idle() != 0)
        return -1;
    if (rx != NULL && len != 0) {
        for (i = 0; i < words; i++) {
            v = rd32(b + QSPI_RBDR(i));
            memcpy(rx + 4 * i, &v, (len - 4 * i < 4) ? (len - 4 * i) : 4);
        }
    }
    return 0;
}

/* Reset both domains so a later AHB access does not see stale buffers. */
static void qspi_invalidate(void)
{
    uintptr_t b = S32G2_QSPI_BASE;
    uint32_t mcr = rd32(b + QSPI_MCR) & ~QSPI_MCR_MDIS;

    wr32(b + QSPI_MCR, mcr);
    wr32(b + QSPI_MCR, mcr | QSPI_MCR_SWRSTHD | QSPI_MCR_SWRSTSD);
    wr32(b + QSPI_MCR, mcr | QSPI_MCR_SWRSTHD | QSPI_MCR_SWRSTSD | QSPI_MCR_MDIS);
    wr32(b + QSPI_MCR, mcr | QSPI_MCR_MDIS);
    wr32(b + QSPI_MCR, mcr);
}

/* The DLL slave in bypass mode, as the SDR configuration needs it. */
static int qspi_dll_bypass(void)
{
    uintptr_t b = S32G2_QSPI_BASE;
    uint32_t v = QSPI_DLLCR_SLV_EN | QSPI_DLLCR_SLV_BYPASS_EN;
    int ret;

    wr32(b + QSPI_DLLCRA, v);
    wr32(b + QSPI_DLLCRA, v | QSPI_DLLCR_SLV_UPD_EN);
    ret = poll_set(b + QSPI_DLLSR, QSPI_DLLSR_SLVA_LOCK);
    wr32(b + QSPI_DLLCRA, v);
    return ret;
}

/* A Boot ROM that loaded this image from the NOR with the stock parameters
 * left the flash in octal DTR mode (volatile CR2). Software reset through
 * two 8-line DDR commands puts it back into SPI mode. */
static int qspi_octal_reset(void)
{
    uintptr_t b = S32G2_QSPI_BASE;
    uint32_t mcr = rd32(b + QSPI_MCR);
    uint16_t e[3];
    volatile uint32_t spin;
    int ret;

    wr32(b + QSPI_FLSHCR, QSPI_FLSHCR_TCSS(3) | QSPI_FLSHCR_TCSH(3) | QSPI_FLSHCR_TDH(1));
    wr32(b + QSPI_MCR, mcr | QSPI_MCR_DDR_EN);
    /* Macronix OPI commands are the opcode followed by its complement. */
    e[0] = LUT_ENTRY_OCTAL(LUT_CMD_DDR, FLASH_CMD_RSTEN);
    e[1] = LUT_ENTRY_OCTAL(LUT_CMD_DDR, (uint8_t)~FLASH_CMD_RSTEN);
    e[2] = LUT_ENTRY(LUT_STOP, 0);
    ret = qspi_wait_idle();
    if (ret == 0) {
        wr32(b + QSPI_SFAR, 0);
        wr32(b + QSPI_SPTRCLR, QSPI_SPTRCLR_BFPTRC | QSPI_SPTRCLR_IPPTRC);
        qspi_lut_program(QSPI_SEQ_IP, e, 3);
        wr32(b + QSPI_IPCR, QSPI_IPCR_SEQID(QSPI_SEQ_IP));
        ret = qspi_wait_idle();
    }
    if (ret == 0) {
        e[0] = LUT_ENTRY_OCTAL(LUT_CMD_DDR, FLASH_CMD_RST);
        e[1] = LUT_ENTRY_OCTAL(LUT_CMD_DDR, (uint8_t)~FLASH_CMD_RST);
        wr32(b + QSPI_SPTRCLR, QSPI_SPTRCLR_BFPTRC | QSPI_SPTRCLR_IPPTRC);
        qspi_lut_program(QSPI_SEQ_IP, e, 3);
        wr32(b + QSPI_IPCR, QSPI_IPCR_SEQID(QSPI_SEQ_IP));
        ret = qspi_wait_idle();
    }
    wr32(b + QSPI_MCR, mcr);
    wr32(b + QSPI_FLSHCR, QSPI_FLSHCR_TCSS(3) | QSPI_FLSHCR_TCSH(3));
    /* tREADY after a software reset is well under a millisecond. */
    for (spin = 0; spin < 2000000U; spin++)
        ;
    qspi_invalidate();
    return ret;
}

static int s32g2_qspi_init(void)
{
    uintptr_t b = S32G2_QSPI_BASE;
    volatile uint32_t spin;
    static const uint8_t expect[3] = FLASH_ID_MX25UW51245G;
    uint8_t id[3] = { 0, 0, 0 };
    int ret;

    wr32(b + QSPI_MCR, QSPI_MCR_SWRSTSD | QSPI_MCR_SWRSTHD);
    for (spin = 0; spin < 1000; spin++)
        ;
    wr32(b + QSPI_MCR, QSPI_MCR_MDIS | QSPI_MCR_RESERVED);
    /* SDR timing: no data hold, three cycles of chip-select setup/hold. */
    wr32(b + QSPI_FLSHCR, QSPI_FLSHCR_TCSS(3) | QSPI_FLSHCR_TCSH(3));
    wr32(b + QSPI_SMPR, QSPI_SMPR_FSPHS);
    wr32(b + QSPI_BUF0IND, 0);
    wr32(b + QSPI_BUF1IND, 0);
    wr32(b + QSPI_BUF2IND, 0);
    wr32(b + QSPI_BFGENCR, QSPI_BFGENCR_SEQID(QSPI_SEQ_IP));
    wr32(b + QSPI_RBCT, QSPI_RBCT_WMRK_MASK);
    wr32(b + QSPI_BUF3CR, QSPI_BUF3CR_ALLMST | QSPI_BUF3CR_ADATSZ(1024 / 8));
    /* One device on bus A, chip select 0. */
    wr32(b + QSPI_SFA1AD, S32G2_QSPI_FLASH_SIZE);
    wr32(b + QSPI_SFA2AD, S32G2_QSPI_FLASH_SIZE);
    wr32(b + QSPI_SFB1AD, S32G2_QSPI_FLASH_SIZE);
    wr32(b + QSPI_SFB2AD, S32G2_QSPI_FLASH_SIZE);
    wr32(b + QSPI_SFACR, 0);
    wr32(b + QSPI_MCR, QSPI_MCR_RESERVED | QSPI_MCR_END_CFG |
        QSPI_MCR_DQS_INT_PAD | QSPI_MCR_DQS_EN);
    ret = qspi_dll_bypass();
    if (ret != 0)
        return ret;
    qspi_ahb_setup();
    /* The flash answers RDID only in SPI mode; otherwise it was left in
     * octal DTR by the Boot ROM, so reset it and ask again. */
    if (qspi_op(FLASH_CMD_RDID, 0, 0, 0, NULL, id, 3) == 0 && memcmp(id, expect, 3) == 0)
        return 0;
    wolfBoot_printf("qspi: id %02x %02x %02x, resetting the flash out of octal mode\n",
        id[0], id[1], id[2]);
    if (qspi_octal_reset() != 0)
        return -2;
    if (qspi_op(FLASH_CMD_RDID, 0, 0, 0, NULL, id, 3) != 0 ||
            memcmp(id, expect, 3) != 0)
        return -3;
    return 0;
}

/* Memory-mapped reads through the AHB window: the controller fetches with
 * the sequence in BFGENCR into its 1 KB buffer. Only whole words, as the
 * window is Device memory while the MMU is off. */
static void qspi_ahb_setup(void)
{
    uint16_t e[5];

    e[0] = LUT_ENTRY(LUT_CMD, FLASH_CMD_READ4B);
    e[1] = LUT_ENTRY(LUT_ADDR, 0x20);
    e[2] = LUT_ENTRY(LUT_DUMMY, 8);
    e[3] = LUT_ENTRY(LUT_READ, QSPI_XFER_MAX);
    e[4] = LUT_ENTRY(LUT_STOP, 0);
    qspi_lut_program(QSPI_SEQ_AHB, e, 5);
    wr32(S32G2_QSPI_BASE + QSPI_BFGENCR, QSPI_BFGENCR_SEQID(QSPI_SEQ_AHB));
}

static int qspi_ahb_read(uint32_t addr, uint8_t* buf, uint32_t len)
{
    uintptr_t win = (uintptr_t)S32G2_QSPI_AHB_BASE + addr;
    const volatile uint64_t* src;
    uint64_t* dst = (uint64_t*)buf;
    uint32_t i, n = len / 8;

    /* The window starts at address 0. Hide the value from the compiler,
     * which may otherwise treat a read through a null pointer as
     * unreachable. */
    __asm__ volatile("" : "+r"(win));
    src = (const volatile uint64_t*)win;
    if (qspi_wait_idle() != 0)
        return -1;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
    return 0;
}

static int s32g2_qspi_read(uint32_t addr, uint8_t* buf, uint32_t len)
{
    uint32_t n;

    /* Bulk through the window when source and destination are 8-byte
     * aligned; the IP path takes the remaining tail, or the whole read
     * otherwise. */
    if (len >= 1024 && (addr & 7) == 0 && (((uintptr_t)buf) & 7) == 0) {
        n = len & ~7U;
        if (qspi_ahb_read(addr, buf, n) != 0)
            return -1;
        addr += n;
        buf += n;
        len -= n;
    }
    while (len > 0) {
        n = (len > QSPI_XFER_MAX) ? QSPI_XFER_MAX : len;
        if (qspi_op(FLASH_CMD_READ4B, 1, addr, 8, NULL, buf, n) != 0)
            return -1;
        addr += n;
        buf += n;
        len -= n;
    }
    return 0;
}

static int flash_wait_ready(void)
{
    uint8_t sr;
    uint32_t spin;

    for (spin = 0; spin < FLASH_WIP_POLL_MAX; spin++) {
        if (qspi_op(FLASH_CMD_RDSR, 0, 0, 0, NULL, &sr, 1) != 0)
            return -1;
        if ((sr & FLASH_SR_WIP) == 0)
            return 0;
    }
    return -1;
}

static int flash_write_enable(void)
{
    return qspi_op(FLASH_CMD_WREN, 0, 0, 0, NULL, NULL, 0);
}

static int s32g2_qspi_write(uint32_t addr, const uint8_t* buf, uint32_t len)
{
    uint32_t n, page_left;

    while (len > 0) {
        page_left = FLASH_PAGE_SIZE - (addr & (FLASH_PAGE_SIZE - 1));
        n = (len > page_left) ? page_left : len;
        if (n > QSPI_XFER_MAX)
            n = QSPI_XFER_MAX;
        if (flash_write_enable() != 0 ||
                qspi_op(FLASH_CMD_PP4B, 1, addr, 0, buf, NULL, n) != 0 ||
                flash_wait_ready() != 0)
            return -1;
        addr += n;
        buf += n;
        len -= n;
    }
    qspi_invalidate();
    return 0;
}

/* Erase whole 4 KB sectors covering [addr, addr + len). */
static int s32g2_qspi_erase(uint32_t addr, uint32_t len)
{
    uint32_t end = addr + len;

    if (len == 0)
        return 0;
    addr &= ~(FLASH_SECTOR_SIZE - 1);
    while (addr < end) {
        if (flash_write_enable() != 0 ||
                qspi_op(FLASH_CMD_SE4B, 1, addr, 0, NULL, NULL, 0) != 0 ||
                flash_wait_ready() != 0)
            return -1;
        addr += FLASH_SECTOR_SIZE;
    }
    qspi_invalidate();
    return 0;
}

#ifdef EXT_FLASH
/* A length from an image header is not trusted yet: keep every access
 * inside the device. */
static int qspi_range_ok(uintptr_t address, int len)
{
    if (len < 0 || (uint64_t)address > (uint64_t)S32G2_QSPI_FLASH_SIZE ||
            (uint64_t)len > (uint64_t)S32G2_QSPI_FLASH_SIZE -
                (uint64_t)address)
        return 0;
    return 1;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    if (!qspi_range_ok(address, len) ||
            s32g2_qspi_read((uint32_t)address, data, (uint32_t)len) != 0)
        return -1;
    return len;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    if (!qspi_range_ok(address, len) ||
            s32g2_qspi_write((uint32_t)address, data, (uint32_t)len) != 0)
        return -1;
    return 0;
}

int ext_flash_erase(uintptr_t address, int len)
{
    if (!qspi_range_ok(address, len))
        return -1;
    return s32g2_qspi_erase((uint32_t)address, (uint32_t)len);
}

void ext_flash_lock(void)
{
}

void ext_flash_unlock(void)
{
}
#endif /* EXT_FLASH */


#endif /* S32G2_QSPI */

/* Console LINFlexD_0. The pads and the UART are set up whether or not
 * wolfBoot prints: the kernel's console driver takes both as it finds them. */

#define LINFLEX_INIT_POLL_MAX   1000000U
/* Program LINFlexD_0 for 8N1 FIFO mode with the given divisor. */
static void s32g2_linflex_setup(uint32_t ibrr, uint32_t fbrr)
{
    uintptr_t b = S32G2_LINFLEX0_BASE;
    uint32_t n;

    /* Enter init mode (INIT, then MME|INIT) and wait for LINS to report it. */
    wr32(b + LINFLEX_LINCR1, LINFLEX_LINCR1_INIT);
    wr32(b + LINFLEX_LINCR1, LINFLEX_LINCR1_MME | LINFLEX_LINCR1_INIT);
    for (n = 0; n < LINFLEX_INIT_POLL_MAX; n++) {
        if ((rd32(b + LINFLEX_LINSR) & LINFLEX_LINSR_LINS_MASK) ==
                LINFLEX_LINSR_LINS_INIT)
            break;
    }
    /* UART mode must be selected before the other UARTCR fields are written. */
    wr32(b + LINFLEX_UARTCR, LINFLEX_UARTCR_UART);
    wr32(b + LINFLEX_LINIBRR, ibrr);
    wr32(b + LINFLEX_LINFBRR, fbrr);
    wr32(b + LINFLEX_UARTPTO, 0xF);
    wr32(b + LINFLEX_UARTCR, LINFLEX_UARTCR_8N1_FIFO);
    wr32(b + LINFLEX_LINCR1, LINFLEX_LINCR1_MME);
}

static void s32g2_console_init(void)
{
    /* The Boot ROM leaves the pads unrouted and the LIN clock on FIRC. */
    wr32(S32G2_SIUL2_0_MSCR_BASE + 4 * S32G2_UART0_TX_MSCR,
        S32G2_UART0_TX_MSCR_VAL);
    wr32(S32G2_SIUL2_0_MSCR_BASE + 4 * S32G2_UART0_RX_MSCR,
        S32G2_UART0_RX_MSCR_VAL);
    wr32(S32G2_SIUL2_0_IMCR_BASE + 4 * (S32G2_UART0_RX_IMCR - 512),
        S32G2_UART0_RX_IMCR_VAL);
    s32g2_linflex_setup(S32G2_LINIBRR_115200_FIRC, S32G2_LINFBRR_115200_FIRC);
}

#if defined(DEBUG_UART)
static void linflex_putc(char c)
{
    uintptr_t b = S32G2_LINFLEX0_BASE;
    /* The generic timer is not guaranteed to count until FXOSC is up, so bound
     * the wait with a plain spin instead of a timer deadline. */
    uint32_t spin = 0;

    while ((rd32(b + LINFLEX_UARTSR) & LINFLEX_UARTSR_DTF) != 0) {
        if (++spin > 1000000U)
            return;
    }
    /* Byte-wide write: in FIFO mode the access size is the byte count sent. */
    wr8(b + LINFLEX_BDRL, (uint8_t)c);
}

void uart_write(const char* buf, unsigned int sz)
{
    unsigned int i;

    for (i = 0; i < sz; i++) {
        if (buf[i] == '\n')
            linflex_putc('\r');
        linflex_putc(buf[i]);
    }
}
#endif /* DEBUG_UART */

/* The device tree always comes from the signed FIT. */
void* hal_get_dts_address(void)
{
    return NULL;
}

void* hal_get_dts_update_address(void)
{
    return NULL;
}

#if defined(DEBUG) && defined(DEBUG_UART)
/* EL3 exception report, called from s32g2_el3_fault_common
 * (src/boot_aarch64_start.S); the vector halts in wfi after this. */

extern uint8_t _start_text[];
extern uint8_t _end[];

void s32g2_el3_fault_handler(unsigned long esr, unsigned long elr,
    unsigned long far, unsigned long vector);
void s32g2_el3_fault_handler(unsigned long esr, unsigned long elr,
    unsigned long far, unsigned long vector)
{
    wolfBoot_printf("\n*** S32G2 EL3 EXCEPTION ***\n");
    wolfBoot_printf("vector=%d ESR=0x%08x EC=0x%02x ISS=0x%06x\n", (int)vector,
        (uint32_t)esr, (uint32_t)((esr >> 26) & 0x3F), (uint32_t)(esr & 0x1FFFFFFUL));
    wolfBoot_printf("ELR=0x%08x%08x FAR=0x%08x%08x\n",
        (uint32_t)(elr >> 32), (uint32_t)(elr & 0xFFFFFFFFUL),
        (uint32_t)(far >> 32), (uint32_t)(far & 0xFFFFFFFFUL));
    if (elr >= (uintptr_t)_start_text && elr < (uintptr_t)_end) {
        wolfBoot_printf("ELR is inside wolfBoot (+0x%x)\n",
            (uint32_t)(elr - (uintptr_t)_start_text));
    }
}
#endif /* DEBUG && DEBUG_UART */

#if defined(MMU) && defined(WOLFBOOT_FDT)

/* Console LINFlexD_0 (ttyLF0); root is the stock card's second partition. */
#ifndef LINUX_BOOTARGS
#ifndef LINUX_BOOTARGS_ROOT
#define LINUX_BOOTARGS_ROOT "/dev/mmcblk0p2"
#endif
#define LINUX_BOOTARGS \
    "console=ttyLF0,115200 earlycon root=" LINUX_BOOTARGS_ROOT \
    " rootwait rw"
#endif

/* Describe the two DDR banks and the PSCI firmware, and make PSCI each cpu
 * node's enable-method. */
static int s32g2_dts_fixup_cpus_memory(fdt_ctx* ctx)
{
    static const char psci_compat[] = "arm,psci-1.0\0arm,psci-0.2";
    uint64_t reg[4];
    int off, ret;

    /* First: a node added here moves the offsets of everything after it. */
    off = fdt_subnode_offset(ctx, 0, "psci");
    if (off == -FDT_ERR_NOTFOUND)
        off = fdt_add_subnode(ctx, 0, "psci");
    if (off < 0)
        return off;
    ret = fdt_setprop(ctx, off, "compatible", psci_compat,
        (int)sizeof(psci_compat));
    if (ret == 0)
        ret = fdt_setprop(ctx, off, "method", "smc", 4);
    if (ret != 0)
        return ret;

    off = fdt_find_devtype(ctx, 0, "memory");
    if (off < 0) {
        off = fdt_add_subnode(ctx, 0, "memory@80000000");
        if (off < 0)
            return off;
        ret = fdt_setprop(ctx, off, "device_type", "memory", 7);
        if (ret != 0)
            return ret;
    }
    reg[0] = cpu_to_fdt64((uint64_t)S32G2_DRAM_BASE);
    reg[1] = cpu_to_fdt64((uint64_t)S32G2_DRAM_BANK0_SIZE);
    reg[2] = cpu_to_fdt64((uint64_t)S32G2_DRAM_BANK1_BASE);
    reg[3] = cpu_to_fdt64((uint64_t)S32G2_DRAM_BANK1_SIZE);
    ret = fdt_setprop(ctx, off, "reg", reg, (int)sizeof(reg));
    if (ret != 0)
        return ret;

    off = fdt_path_offset(ctx, "/cpus");
    if (off < 0)
        return off;
    off = fdt_find_devtype(ctx, off, "cpu");
    while (off >= 0) {
        ret = fdt_setprop(ctx, off, "enable-method", "psci", 5);
        if (ret != 0)
            return ret;
        off = fdt_find_devtype(ctx, off, "cpu");
    }
    wolfBoot_printf("FDT: memory and PSCI set\n");
    return 0;
}

static int s32g2_dts_fixup(void* dts_addr, uint32_t capacity)
{
    fdt_ctx ctx;
    int off;
    int ret;

    ret = fdt_open(&ctx, dts_addr, capacity);
    if (ret == 0)
        ret = fdt_grow(&ctx, WOLFBOOT_FDT_FIXUP_HEADROOM);
    /* Before the /chosen edits, since inserting nodes moves the offsets
     * after them. */
    if (ret == 0)
        ret = s32g2_dts_fixup_cpus_memory(&ctx);
    if (ret != 0)
        return ret;
    /* ERR050543: with derating left off, the kernel's DDR driver has to
     * poll MR4, and it only probes an enabled node. */
    if (s32g2_ddr_polling_needed()) {
        off = fdt_node_offset_by_compatible(&ctx, -1, "fsl,s32gen1-ddr");
        if (off < 0)
            wolfBoot_printf("FDT: no DDR node for erratum polling (%d)\n",
                off);
        else
            ret = fdt_fixup_str(&ctx, off, "ddr", "status", "okay");
    }
    if (ret == 0)
        ret = fdt_fixup_bootargs(&ctx, LINUX_BOOTARGS, 1);
    /* Zero the stock DTB's initrd range unless the FIT supplied one: it is
     * unverified memory and overlaps the kernel load address. */
#ifdef WOLFBOOT_FIT_RAMDISK
    if (ret == 0 && !fit_ramdisk_loaded())
#else
    if (ret == 0)
#endif
        ret = fdt_fixup_initrd(&ctx, 0, 0);
#ifdef WOLFBOOT_UPDATE_DISK
    /* Tell the OS which slot it booted from, as the default fixup does. */
    if (ret == 0)
        ret = wolfBoot_disk_dts_fixup(&ctx);
#endif
    return ret;
}

int hal_dts_fixup(void* dts_addr, uint32_t capacity)
{
    int ret;

    if (dts_addr == NULL)
        return -1;
    ret = s32g2_dts_fixup(dts_addr, capacity);
    if (ret != 0) {
        /* Never hand the kernel a tree left half fixed, from any boot
         * source. */
        wolfBoot_printf("s32g2: device tree fixup failed (%d)\n", ret);
        wolfBoot_panic();
    }
    return ret;
}
#endif /* MMU && WOLFBOOT_FDT */

static int s32g2_ddr_test(uintptr_t base, uint32_t len)
{
    volatile uint64_t* p = (volatile uint64_t*)base;
    uint32_t n = len / sizeof(uint64_t);
    uint32_t i;
    uint64_t v;

    for (i = 0; i < n; i++)
        p[i] = ((uint64_t)(base + i * 8) * 0x9E3779B97F4A7C15ULL) ^ (uint64_t)i;
    for (i = 0; i < n; i++) {
        v = ((uint64_t)(base + i * 8) * 0x9E3779B97F4A7C15ULL) ^ (uint64_t)i;
        if (p[i] != v) {
            wolfBoot_printf("s32g2: DDR mismatch at 0x%08x%08x: 0x%08x%08x != 0x%08x%08x\n",
                (uint32_t)((uint64_t)(uintptr_t)&p[i] >> 32), (uint32_t)(uintptr_t)&p[i],
                (uint32_t)(p[i] >> 32), (uint32_t)p[i], (uint32_t)(v >> 32), (uint32_t)v);
            return -1;
        }
    }
    return 0;
}

/* Let the 4-entry TX FIFO drain before the LIN clock under it changes. Run
 * with the MMU off, where one pass costs several microseconds: at least
 * 15 ms. */
static void linflex_drain(void)
{
    volatile uint32_t spin;

    for (spin = 0; spin < 4000U; spin++)
        ;
}


/* uSDHC0 pads: CLK, CMD/DATA0-7/VSELECT with pull-ups, RST, and the input
 * muxes; the clock and MC_ME block come up with the clock tree. Routed
 * whether or not wolfBoot reads the card itself, as the OS expects the boot
 * firmware to have done it. */
static void s32g2_sd_pads(void)
{
    uint32_t v;

    wr32(S32G2_SIUL2_0_MSCR_BASE + 4 * 46, S32G2_SD_CLK_MSCR_VAL);
    for (v = 47; v <= 55; v++)
        wr32(S32G2_SIUL2_0_MSCR_BASE + 4 * v, S32G2_SD_DATA_MSCR_VAL);
    wr32(S32G2_SIUL2_0_MSCR_BASE + 4 * 56, S32G2_SD_RST_MSCR_VAL);
    for (v = 515; v <= 524; v++)
        wr32(S32G2_SIUL2_0_IMCR_BASE + 4 * (v - 512), S32G2_SD_IMCR_VAL);
}

/* EL3 identity map, 4 KB granule, 36-bit VA. SRAM and trained DRAM are
 * Normal write-back, everything else Device: with the MMU off all data is
 * Device and the hash and inflate take tens of seconds. Only wolfBoot's text
 * is executable, and it is read-only. The map stays on after the handoff for
 * the PSCI runtime, with DRAM removed from it. */
#define EL3_XN             (1ULL << 54)
#define EL3_RO             (1ULL << 7)
#define EL3_BLOCK_NORMAL   (0x0000000000000701ULL | EL3_XN)
#define EL3_BLOCK_DEVICE   (0x0000000000000405ULL | EL3_XN | (1ULL << 53))
#define EL3_PAGE_DATA      (0x0000000000000703ULL | EL3_XN)
#define EL3_PAGE_TEXT      (0x0000000000000703ULL | EL3_RO)
#define EL3_TABLE          0x3ULL
#define EL3_SCTLR_WXN      (1ULL << 19)
static uint64_t s32g2_el3_l1[64] __attribute__((aligned(4096)));
static uint64_t s32g2_el3_l2_gb0[512] __attribute__((aligned(4096)));
static uint64_t s32g2_el3_l3_img[512] __attribute__((aligned(4096)));

/* Page-aligned bounds of the image's code and read-only data (hal/s32g2.ld) */
extern uint8_t _start_text[];
extern uint8_t _end_text[];

static void s32g2_el3_mmu_enable(void)
{
    uint64_t a;
    uint64_t text = (uint64_t)(uintptr_t)_start_text;
    uint64_t text_end = (uint64_t)(uintptr_t)_end_text;
    uint64_t img = text & ~0x1FFFFFULL;
    uint32_t i;

    for (i = 0; i < 512; i++) {
        a = (uint64_t)i << 21;
        s32g2_el3_l2_gb0[i] = a | ((a >= S32G2_SRAM_BASE &&
            a < S32G2_SRAM_BASE + S32G2_SRAM_SIZE) ? EL3_BLOCK_NORMAL : EL3_BLOCK_DEVICE);
    }
    for (i = 0; i < 512; i++) {
        a = img + ((uint64_t)i << 12);
        s32g2_el3_l3_img[i] = a |
            ((a >= text && a < text_end) ? EL3_PAGE_TEXT : EL3_PAGE_DATA);
    }
    s32g2_el3_l2_gb0[img >> 21] =
        (uint64_t)(uintptr_t)s32g2_el3_l3_img | EL3_TABLE;
    for (i = 0; i < 64; i++)
        s32g2_el3_l1[i] = 0;
    s32g2_el3_l1[0] = (uint64_t)(uintptr_t)s32g2_el3_l2_gb0 | EL3_TABLE;
    s32g2_el3_l1[1] = (1ULL << 30) | EL3_BLOCK_DEVICE;
    /* DRAM stays unmapped until it is trained (s32g2_el3_mmu_map_dram), so
     * nothing can speculate into it. */
    __asm__ volatile("dsb sy");

    aarch64_dcache_maint(0);
    s32g2_el3_mmu_on();
}

/* Per core: point it at the tables and turn the MMU and caches on. */
static void s32g2_el3_mmu_on(void)
{
    uint64_t sctlr;

    /* Attr0 Normal WB/WA, Attr1 Device-nGnRnE; T0SZ 28, inner-shareable
     * cacheable walks, 40-bit PA, bits 23 and 31 RES1. */
    __asm__ volatile("msr mair_el3, %0" :: "r"(0x00000000000000FFULL));
    __asm__ volatile("msr ttbr0_el3, %0" :: "r"((uint64_t)(uintptr_t)s32g2_el3_l1));
    __asm__ volatile("msr tcr_el3, %0" :: "r"(0x000000008082351CULL));
    __asm__ volatile("isb");
    __asm__ volatile("tlbi alle3");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
    __asm__ volatile("ic iallu");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
    __asm__ volatile("mrs %0, sctlr_el3" : "=r"(sctlr));
    sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12) | EL3_SCTLR_WXN;
    __asm__ volatile("msr sctlr_el3, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
}

/* Both DRAM banks with the given block attributes: Device for the uncached
 * read-back check after training, Normal write-back once it has passed, and
 * 0 to take DRAM out of the map again, which the handoff
 * (src/boot_aarch64_start.S) does before it cleans the caches. */
void s32g2_el3_mmu_map_dram(uint64_t attr);
void s32g2_el3_mmu_map_dram(uint64_t attr)
{
    uint64_t i;
    int pass;

    /* Break before make: the entries are first made invalid and flushed
     * from the TLB, and only then written with the new attributes. Nothing
     * touches DRAM in between; this code and its stack are in SRAM. */
    for (pass = 0; pass < 2; pass++) {
        for (i = (uint64_t)S32G2_DRAM_BASE >> 30; i < 64 && (i << 30) <
                (uint64_t)S32G2_DRAM_BASE + S32G2_DRAM_BANK0_SIZE; i++)
            s32g2_el3_l1[i] = (pass == 0 || attr == 0) ? 0 : ((i << 30) | attr);
        for (i = S32G2_DRAM_BANK1_BASE >> 30; i < 64 && (i << 30) <
                S32G2_DRAM_BANK1_BASE + S32G2_DRAM_BANK1_SIZE; i++)
            s32g2_el3_l1[i] = (pass == 0 || attr == 0) ? 0 : ((i << 30) | attr);
        __asm__ volatile("dsb sy");
        __asm__ volatile("tlbi alle3is");
        __asm__ volatile("dsb sy");
        __asm__ volatile("isb");
    }
}

/* LPDDR4 training, then a read-back check through a Device mapping so a
 * failed training cannot pass on cached data; only then is DRAM cacheable. */
static void s32g2_dram_init(void)
{
    int ret;

    ret = s32g2_ddr_init();
    if (ret != 0) {
        wolfBoot_printf("s32g2: DDR init failed (%d)\n", ret);
        wolfBoot_panic();
    }
    s32g2_el3_mmu_map_dram(EL3_BLOCK_DEVICE);
    if (s32g2_ddr_test((uintptr_t)S32G2_DRAM_BASE, 0x400000) != 0 ||
            s32g2_ddr_test((uintptr_t)WOLFBOOT_LOAD_ADDRESS, 0x400000) != 0 ||
            s32g2_ddr_test((uintptr_t)S32G2_DRAM_BANK1_BASE, 0x400000) != 0) {
        wolfBoot_printf("s32g2: DDR read-back test FAILED\n");
        wolfBoot_panic();
    }
    s32g2_el3_mmu_map_dram(EL3_BLOCK_NORMAL);
    wolfBoot_printf("s32g2: DDR up, %u MB, erratum polling %d\n",
        (unsigned)(S32G2_DRAM_SIZE >> 20), s32g2_ddr_polling_needed());
}

#ifdef S32G2_QSPI
static void s32g2_qspi_pads(void)
{
    uint32_t v;

    for (v = 85; v <= 93; v++)
        wr32(S32G2_SIUL2_0_MSCR_BASE + 4 * v, S32G2_QSPI_DATA_MSCR_VAL);
    for (v = 96; v <= 99; v++)
        wr32(S32G2_SIUL2_0_MSCR_BASE + 4 * v, S32G2_QSPI_CLK_MSCR_VAL);
    wr32(S32G2_SIUL2_0_MSCR_BASE + 4 * 100, S32G2_QSPI_CS_MSCR_VAL);
    wr32(S32G2_SIUL2_0_MSCR_BASE + 4 * 101, S32G2_QSPI_CS_MSCR_VAL);
    for (v = 540; v <= 548; v++)
        wr32(S32G2_SIUL2_0_IMCR_BASE + 4 * (v - 512), S32G2_QSPI_IMCR_VAL);
}
#endif

#ifdef S32G2_QSPI_PROGRAM_FILE
/* NOR programmer build: read an image file from the FAT partition of the SD
 * card or eMMC into DDR, erase and program it at S32G2_QSPI_PROGRAM_OFFSET,
 * read it back, then halt. */
static void s32g2_nor_program(int qspi_ret)
{
    static struct fs_volume vol;
    static struct fs_file f;
    uint8_t* img = (uint8_t*)(uintptr_t)WOLFBOOT_LOAD_ADDRESS;
    uint8_t* chk = img + S32G2_QSPI_PROGRAM_MAX;
    uint32_t n = 0, off, len;
    int ret;

    /* Nothing is erased unless the flash was identified. */
    ret = qspi_ret;
    if (ret == 0)
        ret = disk_init(0);
    if (ret >= 0)
        ret = disk_open(0);
    if (ret >= 0)
        ret = fs_mount(&vol, 0, BOOT_PART_A);
    /* An unrecognised filesystem mounts as a raw volume whose "file" is the
     * whole partition, whatever the name: only a real file is programmed. */
    if (ret == 0 && vol.type == FS_TYPE_RAW)
        ret = -3;
    if (ret == 0)
        ret = fs_open(&vol, &f, S32G2_QSPI_PROGRAM_FILE,
            S32G2_QSPI_PROGRAM_MAX);
    if (ret == 0 && (f.size == 0 || f.size > S32G2_QSPI_PROGRAM_MAX ||
            (uint64_t)S32G2_QSPI_PROGRAM_OFFSET >=
                (uint64_t)S32G2_QSPI_FLASH_SIZE ||
            f.size > (uint64_t)S32G2_QSPI_FLASH_SIZE -
                (uint64_t)S32G2_QSPI_PROGRAM_OFFSET))
        ret = -4;
    if (ret == 0) {
        for (off = 0; off < (uint32_t)f.size; off += 0x10000) {
            len = (uint32_t)f.size - off;
            if (len > 0x10000)
                len = 0x10000;
            if (fs_read(&f, off, len, img + off) != (int)len) {
                ret = -1;
                break;
            }
        }
        if (ret == 0)
            n = (uint32_t)f.size;
    }
    wolfBoot_printf("s32g2: NOR image %s: %d, %u bytes -> 0x%08x\n",
        S32G2_QSPI_PROGRAM_FILE, ret, (unsigned)n,
        (uint32_t)S32G2_QSPI_PROGRAM_OFFSET);
    if (n != 0) {
        ret = s32g2_qspi_erase(S32G2_QSPI_PROGRAM_OFFSET, n);
        if (ret == 0)
            ret = s32g2_qspi_write(S32G2_QSPI_PROGRAM_OFFSET, img, n);
        if (ret == 0)
            ret = s32g2_qspi_read(S32G2_QSPI_PROGRAM_OFFSET, chk, n);
        if (ret == 0 && memcmp(img, chk, n) != 0)
            ret = -2;
        wolfBoot_printf("s32g2: NOR program and read back: %s (%d)\n",
            (ret == 0) ? "OK" : "FAILED", ret);
    }
    wolfBoot_printf("s32g2: NOR programmer done, halting\n");
    for (;;)
        __asm__ volatile("wfi");
}
#endif

static void s32g2_soc_init(void)
{
    uintptr_t rgm = S32G2_MC_RGM_BASE;
    int ret;

    /* Clear the reset-cause flags (write-1-to-clear) */
    wr32(rgm + MC_RGM_DES, rd32(rgm + MC_RGM_DES));
    wr32(rgm + MC_RGM_FES, rd32(rgm + MC_RGM_FES));

    /* PMIC fail-safe watchdog and FCCU monitoring, before any clock moves:
     * the VR5510 power-cycles the board on an FCCU error-pin event, and the
     * console is still on FIRC so a failure here is reported. */
    ret = s32g2_pmic_init();
    if (ret != 0) {
        wolfBoot_printf("s32g2: PMIC init failed (%d)\n", ret);
        wolfBoot_panic();
    }

    /* The console moves with the LIN clock, so let it empty first. */
    linflex_drain();
    ret = s32g2_clk_init();
    if (ret != 0) {
        wolfBoot_printf("s32g2: clock init failed at step %d\n", ret);
        wolfBoot_panic();
    }
    /* The counter ticks from FXOSC now; publish its rate. */
    __asm__ volatile("msr cntfrq_el0, %0" :: "r"((uint64_t)S32G2_CNTFRQ_HZ));

    /* Caches on for everything from here, DDR training included. The
     * coherent interconnect must be up before this core's caches. */
    ret = s32g2_ncore_init();
    if (ret != 0) {
        wolfBoot_printf("s32g2: Ncore init failed (%d)\n", ret);
        wolfBoot_panic();
    }
    s32g2_el3_mmu_enable();

    s32g2_dram_init();

    /* Interrupt routing, the GIC for this core, and the three secondary
     * cores parked at EL3 for PSCI. */
    s32g2_mscm_init();
    s32g2_gic_init_dist();
    s32g2_gic_init_cpu();
    if (s32g2_smp_release((uintptr_t)WOLFBOOT_ORIGIN) != 0)
        wolfBoot_panic();

    s32g2_sd_pads();
#ifdef S32G2_QSPI
    s32g2_qspi_pads();
    ret = s32g2_qspi_init();
    if (ret != 0) {
        wolfBoot_printf("s32g2: QuadSPI init failed (%d)\n", ret);
#ifdef EXT_FLASH
        /* The image partitions are in this flash. */
        wolfBoot_panic();
#endif
    }
#ifdef S32G2_QSPI_PROGRAM_FILE
    s32g2_nor_program(ret);
#endif
#endif
}

void hal_init(void)
{
    s32g2_console_init();
    wolfBoot_printf("wolfBoot S32G2 (EL%d, running from SRAM)\n",
        (int)current_el());
    s32g2_soc_init();
}

void hal_prepare_boot(void)
{
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
    /* Quiesce the controller so the OS does not inherit a uSDHC this
     * firmware left clocked and running. */
    sdhci_shutdown();
#endif
    /* The BSP Linux QuadSPI driver assumes a 200 MHz serial clock and never
     * changes this divider; at a slower clock its DLL lock wait never ends. */
    (void)s32g2_cgm_div_set(S32G2_MC_CGM0_BASE, 12, 0, S32G2_QSPI_2X_OS_DIV);
}

/* No internal flash: wolfBoot runs from SRAM. Provide the required no-op
 * HAL surface. */
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
