/* imx_usdhc_sdhci.h
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

/* i.MX uSDHC registers, for the uSDHC -> SDHCI shim in hal/imx_usdhc_sdhci.c
 * (i.MX 8QuadMax, i.MX 8M Mini). The SoC header sets IMX_USDHC_BASE and
 * IMX_USDHC_PERCLK_HZ, and optionally IMX_USDHC_MAX_CLK_KHZ. Defines only:
 * boot_aarch64_start.S includes the SoC headers. */

#ifndef _IMX_USDHC_SDHCI_H_
#define _IMX_USDHC_SDHCI_H_

/* uSDHC keeps eSDHC's register layout and adds MIX_CTRL, which holds the
 * transfer-mode half standard SDHCI keeps in the command register. */
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

/* PRES_STATE */
#define USDHC_PRES_CIHB             (1 << 0)    /* command inhibit (CMD) */
#define USDHC_PRES_SDSTB            (1 << 3)    /* SD clock stable */
#define USDHC_PRES_DLSL_DAT0        (1 << 24)   /* DAT0 line signal level */

/* PROT_CTRL */
#define USDHC_PROT_DTW_MASK         (0x3 << 1)
#define USDHC_PROT_DTW_1BIT         (0x0 << 1)
#define USDHC_PROT_DTW_4BIT         (0x1 << 1)
#define USDHC_PROT_DTW_8BIT         (0x2 << 1)
#define USDHC_PROT_D3CD             (1 << 3)
#define USDHC_PROT_EMODE_MASK       (0x3 << 4)
#define USDHC_PROT_EMODE_LE         (0x2 << 4)  /* little-endian data port */
#define USDHC_PROT_DMASEL_MASK      (0x3 << 8)
#define USDHC_PROT_DMASEL_SIMPLE    (0x0 << 8)

/* SYS_CTRL. Reset and data-timeout sit at the same bits as SDHCI's SRS11 and pass
 * through the shim; the DVS/SDCLKFS clock divider does not and is programmed directly. */
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

/* CMD_XFR_TYP (upper half is bit-identical to standard SDHCI's command half) */
#define USDHC_XFR_RSPTYP_MASK       (0x3 << 16)
#define USDHC_XFR_RSPTYP_48         (0x2 << 16)
#define USDHC_XFR_RSPTYP_48B        (0x3 << 16)
#define USDHC_XFR_DPSEL             (1 << 21)   /* data present */

/* MIX_CTRL, the transfer-mode half. Bits 0,1,2,4,5 match standard SDHCI; the tuning
 * and DDR bits above them have no equivalent and must be preserved. */
#define USDHC_MIX_CTRL_XFER_MASK    0x3F

/* HOST_CTRL_CAP: the voltage-support bits are at the standard positions. */
#define USDHC_CAP_VS33              (1 << 24)
#define USDHC_CAP_VS30              (1 << 25)
#define USDHC_CAP_VS18              (1 << 26)

/* WTMK_LVL burst watermarks, in 4-byte words. A full 512-byte block so the PIO loop
 * sees one Buffer Read Ready per block rather than one per 32 bytes. */
#define USDHC_WTMK_RD_SHIFT         0
#define USDHC_WTMK_WR_SHIFT         16
#define USDHC_WTMK_BLOCK_WORDS      0x80

/* VEND_SPEC */
#define USDHC_VEND_SPEC_VSELECT     (1 << 1)    /* 1.8V signaling */

/* Shim internals (hal/imx_usdhc_sdhci.c). CADENCE_SRS_OFFSET is not exported
 * by include/sdhci.h; every in-tree SDHCI platform defines it locally. */
#ifndef CADENCE_SRS_OFFSET
#define CADENCE_SRS_OFFSET  0x200
#endif

#ifndef USDHC_RESET_TIMEOUT_US
#define USDHC_RESET_TIMEOUT_US   1000000
#endif
#ifndef USDHC_INIT_TIMEOUT_US
#define USDHC_INIT_TIMEOUT_US     100000
#endif
#ifndef USDHC_CLK_STABLE_TIMEOUT_US
#define USDHC_CLK_STABLE_TIMEOUT_US 100000
#endif

/* Standard-SDHCI offsets, as seen after the Cadence SRS base is removed. */
#define STD_BLK             0x04
#define STD_CMD             0x0C
#define STD_PRES_STATE      0x24
#define STD_HOST_CTRL1      0x28
#define STD_CLOCK_CTRL      0x2C
#define STD_INT_STATUS      0x30
#define STD_INT_STATUS_EN   0x34
#define STD_INT_SIGNAL_EN   0x38
#define STD_HOST_CTRL2      0x3C
#define STD_CAPS1           0x40
#define STD_CAPS2           0x44
#define STD_MAX_CURRENT     0x48
#define STD_ADMA_ADDR_LO    0x58
#define STD_ADMA_ADDR_HI    0x5C

/* uSDHC-only error bits with no standard-SDHCI position. */
#define USDHC_INT_DMAE      (1U << 28)
#define USDHC_INT_TNE       (1U << 26)

/* SDMA buffers should be aligned to this (Cortex-A53/A72 D-cache line) */
#ifndef IMX_USDHC_DMA_ALIGN
#define IMX_USDHC_DMA_ALIGN 64
#endif

#ifndef __ASSEMBLER__
#include <stdint.h>

/* Called first by sdhci_platform_init(): SoC clock and pad setup, if any.
 * The default does nothing. */
void imx_usdhc_board_init(void);

/* SD clock rate last set by sdhci_platform_set_clock(), in kHz */
uint32_t imx_usdhc_get_clock_khz(void);
#endif

#endif /* _IMX_USDHC_SDHCI_H_ */
