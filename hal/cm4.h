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
/* The debug cable on GPIO14/15 is the BCM2711 mini-UART (AUX / 8250-style,
 * Linux ttyS0), NOT the PL011. The firmware leaves it enabled with a stable
 * baud (enable_uart=1 fixes core_freq), so wolfBoot inherits it. */
#define BCM2711_AUX_BASE        (BCM2711_GPIO_BASE + 0x15000)  /* AUX (mini-UART) */
/* Overridable so host unit tests can retarget the SDHCI glue at a mock buffer */
#ifndef BCM2711_EMMC2_BASE
#define BCM2711_EMMC2_BASE      (BCM2711_MMIO_BASE + 0x340000) /* Arasan SDHCI */
#endif
#define BCM2711_RNG_BASE        (BCM2711_MMIO_BASE + 0x104000) /* RNG200 TRNG */

/* BCM2711 system counter frequency (fallback if CNTFRQ_EL0 reads 0) */
#define BCM2711_TIMER_CLK_FREQ  54000000ULL

/* Names used by the code shared with the CM5 (hal/rpi_common.c) */
#define RPI_TAG                 "cm4"
#define RPI_BOARD               "CM4"
#define RPI_SDHCI_BASE          BCM2711_EMMC2_BASE
#define RPI_RNG_BASE            BCM2711_RNG_BASE
#define RPI_TIMER_CLK_FREQ      BCM2711_TIMER_CLK_FREQ

/* The cm4 build options keep their CM4_ names */
#ifdef CM4_FIRMWARE_DTB
#define RPI_FIRMWARE_DTB
#endif
#ifdef CM4_RAUC_AB
#define RPI_RAUC_AB
#endif
#ifdef CM4_FDT_MAX_SIZE
#define RPI_FDT_MAX_SIZE        CM4_FDT_MAX_SIZE
#endif
#ifdef CM4_FIPS_HEAP_SIZE
#define RPI_FIPS_HEAP_SIZE      CM4_FIPS_HEAP_SIZE
#endif
#ifdef CM4_UBOOT_ENV_PART
#define RPI_UBOOT_ENV_PART      CM4_UBOOT_ENV_PART
#endif
#ifdef CM4_ROOT_A
#define RPI_ROOT_A              CM4_ROOT_A
#endif
#ifdef CM4_ROOT_B
#define RPI_ROOT_B              CM4_ROOT_B
#endif
#ifdef CM4_SLOT_A_NAME
#define RPI_SLOT_A_NAME         CM4_SLOT_A_NAME
#endif
#ifdef CM4_SLOT_B_NAME
#define RPI_SLOT_B_NAME         CM4_SLOT_B_NAME
#endif

/* Linux console/earlycon (no root=). It must match the wolfBoot UART build:
 *   CM4_UART_PL011 (+ dtoverlay=disable-bt) -> PL011 on GPIO14/15 = ttyAMA0. The
 *     PL011 registers cleanly in Linux; the mini-UART (bcm2835-aux) does not on
 *     this DTB ("unable to register 8250 port").
 *   default (mini-UART) -> serial0/ttyS0. */
#if defined(CM4_UART_PL011)
#define LINUX_BOOTARGS_BASE \
    "earlycon=pl011,mmio32,0xfe201000 console=ttyAMA0,115200"
#else
/* earlycon=uart8250,mmio32,0xfe215040: the mini-UART is 8250-driven with a 4-byte
 * register stride, so LSR lands at 0xfe215054 - prints from MMIO before the dtb
 * console driver is up. serial0 aliases the mini-UART (ttyS0) at runtime. */
#define LINUX_BOOTARGS_BASE \
    "earlycon=uart8250,mmio32,0xfe215040 console=serial0,115200"
#endif

#include "hal/rpi_common.h"

#ifndef __ASSEMBLER__
/* PL011 UART0 register accessors (not the debug console on this board) */
#define UART0_DR    ((volatile unsigned int*)(BCM2711_UART0_BASE+0x00))
#define UART0_FR    ((volatile unsigned int*)(BCM2711_UART0_BASE+0x18))
#define UART0_IBRD  ((volatile unsigned int*)(BCM2711_UART0_BASE+0x24))
#define UART0_FBRD  ((volatile unsigned int*)(BCM2711_UART0_BASE+0x28))
#define UART0_LCRH  ((volatile unsigned int*)(BCM2711_UART0_BASE+0x2C))
#define UART0_CR    ((volatile unsigned int*)(BCM2711_UART0_BASE+0x30))
#define UART0_ICR   ((volatile unsigned int*)(BCM2711_UART0_BASE+0x44))

/* Mini-UART (AUX) register accessors - the actual debug console (Linux ttyS0).
 * 8250/16550-style: IO at +0x40 (data), LSR at +0x54 (bit5 = TX can accept). */
#define MU_IO       ((volatile unsigned int*)(BCM2711_AUX_BASE+0x40))
#define MU_LSR      ((volatile unsigned int*)(BCM2711_AUX_BASE+0x54))
#define MU_LSR_TXFF_EMPTY  0x20  /* transmit FIFO can accept a byte */

#endif /* !__ASSEMBLER__ */

#endif /* _CM4_H_ */
