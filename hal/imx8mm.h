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

/* BL33 identity MMU (el2_idmap_enable, src/boot_aarch64_start.S) */
#if defined(IMX8MM_BL33) && defined(IMX8MM_MMU)
#define WOLFBOOT_AARCH64_EL2_IDMAP
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
#define IMX8MM_UART_REG(off) \
    (*(volatile uint32_t *)(uintptr_t)(IMX8MM_UART_BASE + (off)))

/* Clock controller (CCM) */
#define IMX8MM_CCM_BASE             0x30380000
#define CCM_CCGR_SET(n)             (IMX8MM_CCM_BASE + 0x4004 + ((n) * 0x10))
#define CCM_CCGR_CLR(n)             (IMX8MM_CCM_BASE + 0x4008 + ((n) * 0x10))
#define CCM_CCGR_CLK_ON             0x3
#define CCM_TARGET_ROOT(n)          (IMX8MM_CCM_BASE + 0x8000 + ((n) * 0x80))
#define CCM_TARGET_ROOT_ENABLE      (1 << 28)
#define CCM_TARGET_ROOT_MUX(n)      (((n) & 0x7) << 24)
#define CCM_CCGR_USDHC3             94
#define CCM_ROOT_USDHC3             121     /* source 1 = SYSTEM_PLL1_DIV2 */

/* Pin mux controller (IOMUXC) */
#define IMX8MM_IOMUXC_BASE          0x30330000
#define IOMUXC_MUX_SION             (1 << 4)
/* USDHC pad setting used by U-Boot: DSE6 | HYS | PUE | PE | FSEL2 */
#define IOMUXC_PAD_USDHC            0x1D6

/* uSDHC3: eMMC (8-bit) on PICO-IMX8MM */
#define IMX8MM_USDHC3_BASE          0x30B60000
#ifndef IMX8MM_USDHC_PERCLK_HZ
#define IMX8MM_USDHC_PERCLK_HZ      400000000   /* SYSTEM_PLL1_DIV2, divider 1 */
#endif

/* uSDHC registers and the shim interface (hal/imx_usdhc_sdhci.c) */
#include "imx_usdhc_sdhci.h"
#define IMX8MM_USDHC_BASE           IMX8MM_USDHC3_BASE
#define IMX_USDHC_BASE              IMX8MM_USDHC_BASE
#define IMX_USDHC_PERCLK_HZ         IMX8MM_USDHC_PERCLK_HZ
#ifdef IMX8MM_USDHC_MAX_CLK_KHZ
#define IMX_USDHC_MAX_CLK_KHZ       IMX8MM_USDHC_MAX_CLK_KHZ
#endif

#endif /* _IMX8MM_H_ */
