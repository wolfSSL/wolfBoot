/* agilex5.h
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1335, USA
 */

#ifndef WOLFSSL_AGILEX5_H
#define WOLFSSL_AGILEX5_H

#ifndef EL3_SECURE
#define EL3_SECURE      0
#endif
#ifndef EL2_HYPERVISOR
#define EL2_HYPERVISOR  1
#endif
#ifndef EL1_NONSECURE
#define EL1_NONSECURE   1
#endif
#ifndef HYP_GUEST
#define HYP_GUEST       0
#endif
#ifndef FPU_TRAP
#define FPU_TRAP        0
#endif
#ifndef SKIP_GIC_INIT
#define SKIP_GIC_INIT   1
#endif

/* Preserve EL2 for Linux/KVM, matching the normal TF-A -> U-Boot handoff. */
#if !defined(BOOT_EL1) && !defined(BOOT_EL2)
#define BOOT_EL2 1
#endif
#if defined(BOOT_EL1) && defined(BOOT_EL2)
#error "BOOT_EL1 and BOOT_EL2 are mutually exclusive"
#endif

#define AGILEX5_DDR_BASE       0x80000000UL
/* The GSRD U-Boot device tree starts HPS DDR at 0x80000000. U-Boot normally
 * writes the measured size into the DTB; wolfBoot supplies the 013B's
 * 1792 MiB because it hands the DTB directly to Linux. */
#ifndef AGILEX5_DDR_SIZE
#define AGILEX5_DDR_SIZE       0x70000000UL
#endif

/* Agilex 5 HPS UART0 and UART1 MMIO bases. The GSRD device tree maps these as
 * serial@10c02000 and serial@10c02100. The selected value is also emitted in
 * Linux's earlycon argument, so keep the address macros free of a C suffix. */
#define AGILEX5_UART0_BASE     0x10C02000
#define AGILEX5_UART1_BASE     0x10C02100
#ifndef DEBUG_UART_BASE
    #if defined(DEBUG_UART_NUM) && DEBUG_UART_NUM == 1
        #define DEBUG_UART_BASE AGILEX5_UART1_BASE
    #else
        #define DEBUG_UART_BASE AGILEX5_UART0_BASE
    #endif
#endif

/* HPS SDMMC controller base from the Agilex 5 HPS memory map. */
#define AGILEX5_SDHCI_BASE     0x10808000UL
/* The Cortex-A55 data cache uses 64-byte lines. */
#define CACHE_LINE_SIZE        64UL

/* TF-A programs the architectural counter to 100 MHz on the GSRD boot path. */
#ifndef TIMER_CLK_FREQ
#define TIMER_CLK_FREQ         100000000UL
#endif

/* Cadence SD6HC host and PHY register offsets from the GSRD U-Boot driver. */
#define AGILEX5_SDHCI_HRS05                  0x014U
#define AGILEX5_SDHCI_HRS07                  0x01CU
#define AGILEX5_SDHCI_HRS09                  0x024U
#define AGILEX5_SDHCI_HRS10                  0x028U
#define AGILEX5_SDHCI_HRS16                  0x040U
#define AGILEX5_SDHCI_PHY_DQ_TIMING          0x2000U
#define AGILEX5_SDHCI_PHY_DQS_TIMING         0x2004U
#define AGILEX5_SDHCI_PHY_GATE_LPBK_CTRL     0x2008U
#define AGILEX5_SDHCI_PHY_DLL_MASTER_CTRL    0x200CU
#define AGILEX5_SDHCI_PHY_DLL_SLAVE_CTRL     0x2010U
#define AGILEX5_SDHCI_PHY_INIT_COMPLETE      (1U << 1)
#define AGILEX5_SDHCI_PHY_SW_RESET           (1U << 0)
#define AGILEX5_SDHCI_PHY_INIT_TIMEOUT       1000000U

/* wolfBoot occupies the GSRD BL33 region at 0x80200000. The DTB is staged at
 * 0x8f000000, matching agilex5.its, and the signed image uses the next
 * 256 MiB at 0x90000000 so neither buffer overlaps wolfBoot or the DTB. */
#ifndef WOLFBOOT_LOAD_ADDRESS
#define WOLFBOOT_LOAD_ADDRESS  0x90000000UL
#endif
#ifndef WOLFBOOT_LOAD_DTS_ADDRESS
#define WOLFBOOT_LOAD_DTS_ADDRESS 0x8F000000UL
#endif
#ifndef WOLFBOOT_RAMBOOT_MAX_SIZE
#define WOLFBOOT_RAMBOOT_MAX_SIZE 0x10000000UL
#endif

#endif /* WOLFSSL_AGILEX5_H */
