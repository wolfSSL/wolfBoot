/* imx8mm.h
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

#ifndef _IMX8MM_H_
#define _IMX8MM_H_

#define USE_BUILTIN_STARTUP
#define USE_SIMPLE_STARTUP

/* Entered at EL2; do_boot() turns the MMU and caches off before Linux */
#ifndef EL2_HYPERVISOR
#define EL2_HYPERVISOR 1
#endif

/* Fallback only: ATF sets CNTFRQ_EL0 to 8 MHz */
#define TIMER_CLK_FREQ              8000000

/* DRAM (PICO-IMX8MM: 2 GB), 1 GB aligned for the BL33 MMU */
#ifndef IMX8MM_DRAM_BASE
#define IMX8MM_DRAM_BASE            0x40000000ULL
#endif
#ifndef IMX8MM_DRAM_END
#define IMX8MM_DRAM_END             0xC0000000ULL
#endif

/* UART (console). Default: UART2, the PICO-PI console (ttymxc1). */
#ifndef IMX8MM_UART_BASE
#define IMX8MM_UART_BASE            0x30890000UL
#endif
#define IMX8MM_UART_UTXD            0x40    /* Transmitter register */
#define IMX8MM_UART_UCR1            0x80    /* Control register 1 */
#define IMX8MM_UART_USR2            0x98    /* Status register 2 */
#define IMX8MM_UART_UTS             0xB4    /* Test register */
#define UCR1_UARTEN                 (1U << 0)
#define USR2_TXDC                   (1U << 3)   /* Transmission complete */
#define UTS_TXFULL                  (1U << 4)   /* TxFIFO full */

/* Clock controller (CCM) */
#define IMX8MM_CCM_BASE             0x30380000
#define CCM_CCGR_SET(n)             (IMX8MM_CCM_BASE + 0x4004 + ((n) * 0x10))
#define CCM_CCGR_CLR(n)             (IMX8MM_CCM_BASE + 0x4008 + ((n) * 0x10))
#define CCM_CCGR_CLK_ON             0x3
#define CCM_TARGET_ROOT(n)          (IMX8MM_CCM_BASE + 0x8000 + ((n) * 0x80))
#define CCM_TARGET_ROOT_ENABLE      (1 << 28)
#define CCM_TARGET_ROOT_MUX(n)      (((n) & 0x7) << 24)
#define CCM_CCGR_USDHC3             94
#define CCM_ROOT_USDHC3             121     /* source 1 = SYS_PLL1_400M */

/* Pin mux controller (IOMUXC) */
#define IMX8MM_IOMUXC_BASE          0x30330000
#define IOMUXC_MUX_SION             (1 << 4)
/* USDHC pad setting used by U-Boot: DSE6 | HYS | PUE | PE | FSEL2 */
#define IOMUXC_PAD_USDHC            0x1D6

/* uSDHC3: eMMC (8-bit) on PICO-IMX8MM */
#define IMX8MM_USDHC3_BASE          0x30B60000
#ifndef IMX8MM_USDHC_PERCLK_HZ
#define IMX8MM_USDHC_PERCLK_HZ      400000000   /* SYS_PLL1_400M, divider 1 */
#endif

/* uSDHC registers (same IP as i.MX8QM, see hal/imx8qm.h) */
#define USDHC_DS_ADDR               0x00
#define USDHC_BLK_ATT               0x04
#define USDHC_CMD_ARG               0x08
#define USDHC_CMD_XFR_TYP           0x0C
#define USDHC_CMD_RSP0              0x10
#define USDHC_DATA_BUFF_ACC_PORT    0x20
#define USDHC_PRES_STATE            0x24
#define USDHC_PROT_CTRL             0x28
#define USDHC_SYS_CTRL              0x2C
#define USDHC_INT_STATUS            0x30
#define USDHC_INT_STATUS_EN         0x34
#define USDHC_HOST_CTRL_CAP         0x40
#define USDHC_WTMK_LVL              0x44
#define USDHC_MIX_CTRL              0x48
#define USDHC_VEND_SPEC             0xC0

#define USDHC_PRES_CIHB             (1 << 0)    /* command inhibit (CMD) */
#define USDHC_PRES_SDSTB            (1 << 3)    /* SD clock stable */
#define USDHC_PRES_DLSL_DAT0        (1 << 24)   /* DAT0 line signal level */

#define USDHC_PROT_DTW_MASK         (0x3 << 1)
#define USDHC_PROT_DTW_1BIT         (0x0 << 1)
#define USDHC_PROT_DTW_4BIT         (0x1 << 1)
#define USDHC_PROT_DTW_8BIT         (0x2 << 1)
#define USDHC_PROT_D3CD             (1 << 3)
#define USDHC_PROT_EMODE_MASK       (0x3 << 4)
#define USDHC_PROT_EMODE_LE         (0x2 << 4)  /* little-endian data port */
#define USDHC_PROT_DMASEL_MASK      (0x3 << 8)
#define USDHC_PROT_DMASEL_SIMPLE    (0x0 << 8)

#define USDHC_SYS_DVS_SHIFT         4
#define USDHC_SYS_DVS_MASK          (0xF << 4)
#define USDHC_SYS_SDCLKFS_SHIFT     8
#define USDHC_SYS_SDCLKFS_MASK      (0xFF << 8)
#define USDHC_SYS_DTOCV_SHIFT       16
#define USDHC_SYS_DTOCV_MASK        (0xF << 16)
#define USDHC_SYS_RSTA              (1 << 24)   /* reset all */
#define USDHC_SYS_RSTC              (1 << 25)   /* reset command line */
#define USDHC_SYS_RSTD              (1 << 26)   /* reset data line */
#define USDHC_SYS_INITA             (1 << 27)   /* send 80 init clocks */

#define USDHC_XFR_RSPTYP_MASK       (0x3 << 16)
#define USDHC_XFR_RSPTYP_48         (0x2 << 16)
#define USDHC_XFR_RSPTYP_48B        (0x3 << 16)
#define USDHC_XFR_DPSEL             (1 << 21)   /* data present */

#define USDHC_MIX_CTRL_XFER_MASK    0x3F

#define USDHC_CAP_VS33              (1 << 24)
#define USDHC_CAP_VS30              (1 << 25)
#define USDHC_CAP_VS18              (1 << 26)

#define USDHC_WTMK_RD_SHIFT         0
#define USDHC_WTMK_WR_SHIFT         16
#define USDHC_WTMK_BLOCK_WORDS      0x80

#define USDHC_VEND_SPEC_VSELECT     (1 << 1)    /* 1.8V signaling */

#endif /* _IMX8MM_H_ */
