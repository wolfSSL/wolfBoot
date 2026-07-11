/* cm4.h
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

/* Raspberry Pi Compute Module 4 (CM4) - Broadcom BCM2711, Cortex-A72.
 * Hardware register map (bases, UART, EMMC2/SDHCI) shared by the HAL and the
 * test application. This header is also included from boot_aarch64_start.S, so
 * pointer-cast accessors are guarded with __ASSEMBLER__. */

#ifndef _CM4_H_
#define _CM4_H_

/* Select the compact AArch64 startup path in boot_aarch64_start.S */
#define USE_BUILTIN_STARTUP
#define USE_SIMPLE_STARTUP

/* BCM2711 peripheral base addresses (low-peripheral 0xFE000000 view).
 * Integer literals - safe to include from assembly. */
#define BCM2711_MMIO_BASE       0xFE000000
#define BCM2711_GPIO_BASE       (BCM2711_MMIO_BASE + 0x200000)
#define BCM2711_UART0_BASE      (BCM2711_GPIO_BASE + 0x1000)   /* PL011 */
#define BCM2711_EMMC2_BASE      (BCM2711_MMIO_BASE + 0x340000) /* Arasan SDHCI */
#define BCM2711_RNG_BASE        (BCM2711_MMIO_BASE + 0x104000) /* RNG200 TRNG */

/* RNG200 (iproc-rng200) register offsets */
#define RNG200_CTRL             0x00  /* bit0 RBGEN = enable */
#define RNG200_SOFT_RESET       0x04
#define RNG200_RBG_SOFT_RESET   0x08
#define RNG200_INT_STATUS       0x18
#define RNG200_FIFO_DATA        0x20
#define RNG200_FIFO_COUNT       0x24  /* [7:0] = words available */
#define RNG200_CTRL_RBGEN       0x00000001

/* BCM2711 system counter frequency (fallback if CNTFRQ_EL0 reads 0) */
#define BCM2711_TIMER_CLK_FREQ  54000000ULL

/* SDHCI register offsets used by the EMMC2 glue. The generic SDHCI driver
 * addresses registers with Cadence-style SRS offsets (0x200 + std); the glue
 * translates to the standard Arasan layout below. Integers, assembler-safe. */
#define CADENCE_SRS_OFFSET      0x200
#define STD_SDHCI_SDMA_ADDR     0x00  /* SDMA System Address (32-bit) */
#define STD_SDHCI_HOST_CTRL1    0x28
#define STD_SDHCI_POWER_CTRL    0x29
#define STD_SDHCI_BLKGAP_CTRL   0x2A
#define STD_SDHCI_WAKEUP_CTRL   0x2B
#define STD_SDHCI_CLK_CTRL      0x2C
#define STD_SDHCI_TIMEOUT_CTRL  0x2E
#define STD_SDHCI_SW_RESET      0x2F
#define STD_SDHCI_HOST_CTRL2    0x3C
#define STD_SDHCI_SRA           0x01  /* Software Reset for All */

#ifndef __ASSEMBLER__
/* PL011 UART0 register accessors */
#define UART0_DR    ((volatile unsigned int*)(BCM2711_UART0_BASE+0x00))
#define UART0_FR    ((volatile unsigned int*)(BCM2711_UART0_BASE+0x18))
#define UART0_IBRD  ((volatile unsigned int*)(BCM2711_UART0_BASE+0x24))
#define UART0_FBRD  ((volatile unsigned int*)(BCM2711_UART0_BASE+0x28))
#define UART0_LCRH  ((volatile unsigned int*)(BCM2711_UART0_BASE+0x2C))
#define UART0_CR    ((volatile unsigned int*)(BCM2711_UART0_BASE+0x30))
#define UART0_ICR   ((volatile unsigned int*)(BCM2711_UART0_BASE+0x44))

/* RNG200 hardware TRNG register accessors */
#define RNG_CTRL        ((volatile unsigned int*)(BCM2711_RNG_BASE+RNG200_CTRL))
#define RNG_SOFT_RESET  ((volatile unsigned int*)(BCM2711_RNG_BASE+RNG200_SOFT_RESET))
#define RNG_RBG_SOFT_RESET ((volatile unsigned int*)(BCM2711_RNG_BASE+RNG200_RBG_SOFT_RESET))
#define RNG_INT_STATUS  ((volatile unsigned int*)(BCM2711_RNG_BASE+RNG200_INT_STATUS))
#define RNG_FIFO_DATA   ((volatile unsigned int*)(BCM2711_RNG_BASE+RNG200_FIFO_DATA))
#define RNG_FIFO_COUNT  ((volatile unsigned int*)(BCM2711_RNG_BASE+RNG200_FIFO_COUNT))
#endif /* !__ASSEMBLER__ */

#endif /* _CM4_H_ */
