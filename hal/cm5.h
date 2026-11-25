/* cm5.h
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

/* Raspberry Pi Compute Module 5 (CM5) - Broadcom BCM2712, Cortex-A76.
 * Hardware register map shared by the HAL and the test application. This
 * header is also included from boot_aarch64_start.S, so pointer-cast accessors
 * are guarded with __ASSEMBLER__. */

#ifndef _CM5_H_
#define _CM5_H_

/* Select the compact AArch64 startup path in boot_aarch64_start.S */
#define USE_BUILTIN_STARTUP
#define USE_SIMPLE_STARTUP

/* BCM2712 peripherals sit above 4 GB: the "soc" bus is at CPU physical
 * 0x10_7c000000 and the SD/eMMC controllers on the "axi" bus at 0x10_00000000.
 * RP1 (40-pin header UARTs, Ethernet, USB) is behind PCIe and is not used. */
/* Overridable so host unit tests can retarget the SDHCI glue at a mock buffer */
#ifndef BCM2712_SDIO1_BASE
#define BCM2712_SDIO1_BASE      0x1000fff000 /* SDHCI v3.0 (brcmstb), eMMC */
#endif
#ifndef BCM2712_SDIO1_CFG_BASE
#define BCM2712_SDIO1_CFG_BASE  0x1000fff400 /* SDIO_CFG block */
#endif
#define BCM2712_UART10_BASE     0x107d001000 /* PL011, 3-pin debug connector */
#define BCM2712_PM_BASE         0x107d200000 /* PM / watchdog */
#define BCM2712_RNG_BASE        0x107d208000 /* RNG200 TRNG */

/* BCM2712 system counter frequency (fallback if CNTFRQ_EL0 reads 0) */
#define BCM2712_TIMER_CLK_FREQ  54000000ULL

/* The debug PL011 is fed by a fixed 9.216 MHz clock (clk_uart in
 * bcm2712.dtsi): 9216000 / (16 * 115200) = 5 exactly. */
#define BCM2712_UART10_IBRD_115200  5
#define BCM2712_UART10_FBRD_115200  0

/* SDIO_CFG block (sdhci-brcmstb). SDCD_N_TEST forces the card-detect input,
 * which is not wired for the soldered-down eMMC. */
#define SDIO_CFG_CTRL                   0x00
#define SDIO_CFG_CTRL_SDCD_N_TEST_EN    0x80000000
#define SDIO_CFG_CTRL_SDCD_N_TEST_LEV   0x40000000
#define SDIO_CFG_SD_PIN_SEL             0x44
#define SDIO_CFG_SD_PIN_SEL_MASK        0x3
#define SDIO_CFG_SD_PIN_SEL_MMC         0x1

/* Names used by the code shared with the CM4 (hal/rpi_common.c) */
#define RPI_TAG                 "cm5"
#define RPI_BOARD               "CM5"
#define RPI_SDHCI_BASE          BCM2712_SDIO1_BASE
#define RPI_SDHCI_CFG_BASE      BCM2712_SDIO1_CFG_BASE
#define RPI_RNG_BASE            BCM2712_RNG_BASE
#define RPI_TIMER_CLK_FREQ      BCM2712_TIMER_CLK_FREQ

/* Linux console/earlycon (no root=): the debug PL011 is ttyAMA10. */
#define LINUX_BOOTARGS_BASE \
    "earlycon=pl011,mmio32,0x107d001000 console=ttyAMA10,115200"

#include "hal/rpi_common.h"

/* PM watchdog (bcm2835-pm layout): ticks of 1/65536 s, 20-bit count */
#define BCM2712_PM_RSTC         (BCM2712_PM_BASE + 0x1c)
#define BCM2712_PM_WDOG         (BCM2712_PM_BASE + 0x24)
#define PM_PASSWORD             0x5a000000
#define PM_RSTC_WRCFG_MASK      0x00000030
#define PM_RSTC_WRCFG_FULL_RST  0x00000020
#define PM_RSTC_RESET           0x00000102
#define PM_WDOG_TIME_MASK       0x000fffff

/* Optional DDR log ring (CM5_LOG_RING): mirrors the console into RAM that
 * survives a warm reset, so the boot log can be read back from Linux through
 * /dev/mem. The CM5 IO board has no debug UART connector. */
#ifndef CM5_LOG_RING_BASE
#define CM5_LOG_RING_BASE     0x7f000000UL
#endif
#define CM5_LOG_RING_HDR      16UL
#define CM5_LOG_RING_REGION   0x00010000UL
#define CM5_LOG_RING_SIZE     (CM5_LOG_RING_REGION - CM5_LOG_RING_HDR)
#define CM5_LOG_RING_MAGIC    0x4E4F4357UL   /* "WCON" */

#ifndef __ASSEMBLER__
#define UART10_REG(off) \
    ((volatile unsigned int*)(BCM2712_UART10_BASE + (off)))
#define UART10_DR    UART10_REG(0x00)
#define UART10_FR    UART10_REG(0x18)
#define UART10_IBRD  UART10_REG(0x24)
#define UART10_FBRD  UART10_REG(0x28)
#define UART10_LCRH  UART10_REG(0x2C)
#define UART10_CR    UART10_REG(0x30)
#define UART10_ICR   UART10_REG(0x44)
#define PL011_FR_TXFF   0x20  /* transmit FIFO full */
#define PL011_CR_UARTEN 0x001
#define PL011_CR_TXE    0x100
#define PL011_CR_RXE    0x200
#endif /* !__ASSEMBLER__ */

#endif /* _CM5_H_ */
