/* s32g2.h
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

/* NXP S32G2 (S32G274A) definitions for the S32G-VNP-GLDBOX / S32G274A-RDB2.
 * boot_aarch64_start.S includes this, so keep assembly off the C-only parts. */

#ifndef _S32G2_H_
#define _S32G2_H_

/* Simple AArch64 startup: wolfBoot is entered by the Boot ROM at EL3 in
 * SRAM, with the SRAM initialisation added in front of the common path. */
#define USE_BUILTIN_STARTUP
#define USE_SIMPLE_STARTUP

/* wolfBoot runs with its identity MMU and caches on: build the D-cache
 * maintenance by address range that the SDMA path needs. */
#define WOLFBOOT_AARCH64_DCACHE_RANGE

/* System SRAM, and its two controllers (4 MB each). An ECC read-modify-write
 * on SRAM the controller has not initialised faults, so the startup code runs
 * the controller over everything it uses beyond what the Boot ROM copied. The
 * initialisation address of a byte offset is ((off >> 9) << 2) | ((off >> 4) & 3). */
#define S32G2_SRAM_BASE             0x34000000
#define S32G2_SRAM_SIZE             0x00800000
#define S32G2_SRAMC0_BASE           0x4019C000  /* 0x34000000-0x343FFFFF */
#define SRAMC_PRAMCR                0x00
#define SRAMC_PRAMIAS               0x04
#define SRAMC_PRAMIAE               0x08
#define SRAMC_PRAMSR                0x0C
#define SRAMC_PRAMCR_INITREQ        1
#define SRAMC_PRAMSR_IDONE          1

/* Reset Generation Module: destructive and functional reset-cause flags,
 * write-1-to-clear. */
#define S32G2_MC_RGM_BASE           0x40078000
#define MC_RGM_DES                  0x00
#define MC_RGM_FES                  0x08

/* SIUL2_0: chip identification and the pad muxing for UART0. */
#define S32G2_SIUL2_0_MSCR_BASE     0x4009C240  /* MSCR n at + 4 * n */
#define S32G2_SIUL2_0_IMCR_BASE     0x4009CA40  /* IMCR n at + 4 * (n - 512) */
#define S32G2_UART0_TX_MSCR         41          /* PC09 */
#define S32G2_UART0_RX_MSCR         42          /* PC10 */
#define S32G2_UART0_RX_IMCR         512
/* ALT1 | OBE | SRE 133 MHz; ALT0 | IBE | SRE; IMCR source ALT2 */
#define S32G2_UART0_TX_MSCR_VAL     0x00218001
#define S32G2_UART0_RX_MSCR_VAL     0x00098000
#define S32G2_UART0_RX_IMCR_VAL     0x2
/* uSDHC0 pads 46-56 (MSCR) and input muxes 515-524 (IMCR): ALT1 with the
 * 150 MHz slew; CLK pull-down, CMD/DATA/VSELECT pull-up, RST output only. */
#define S32G2_SD_CLK_MSCR_VAL       0x00296001
#define S32G2_SD_DATA_MSCR_VAL      0x00297001
#define S32G2_SD_RST_MSCR_VAL       0x00216001
#define S32G2_SD_IMCR_VAL           0x2

/* QuadSPI (S32G2_QSPI): controller, the 64 MB NOR on bus A chip select 0,
 * pads 85-93 (data, DQS) 96-99 (clock) 100-101 (chip selects) with input
 * muxes 540-548; ALT1 on every pad with the fastest slew, data pulled down
 * and chip selects pulled up. QSPI_2X_CLK is MC_CGM0 mux 12 from PERIPH
 * DFS port 0; the serial clock is half of it. */
#define S32G2_QSPI_BASE             0x40134000
#define S32G2_QSPI_AHB_BASE         0x00000000
#define S32G2_QSPI_FLASH_SIZE       0x04000000
#define S32G2_QSPI_DATA_MSCR_VAL    0x00282001
#define S32G2_QSPI_CLK_MSCR_VAL     0x00200001
#define S32G2_QSPI_CS_MSCR_VAL      0x00203001
#define S32G2_QSPI_IMCR_VAL         0x2
#ifndef S32G2_QSPI_2X_DIV
#define S32G2_QSPI_2X_DIV           8   /* 800 MHz / 8 = 100 MHz QSPI_2X, 50 MHz SCK */
#endif

/* Out of reset the LIN clock is the 48 MHz FIRC, until the clock tree is
 * programmed: 48e6 / (16 * 115200) = 26.04. */
#define S32G2_LINIBRR_115200_FIRC   26
#define S32G2_LINFBRR_115200_FIRC   1

/* Clock generation: crystal oscillator, PLLs, DFS fractional dividers, the
 * clock-generation modules and the mode-entry module's partition gating. */
#define S32G2_FXOSC_BASE            0x40050000
#define S32G2_ARM_PLL_BASE          0x40038000
#define S32G2_PERIPH_PLL_BASE       0x4003C000
#define S32G2_DDR_PLL_BASE          0x40044000
#define S32G2_ARM_DFS_BASE          0x40054000
#define S32G2_PERIPH_DFS_BASE       0x40058000
#define S32G2_MC_CGM0_BASE          0x40030000
#define S32G2_MC_CGM1_BASE          0x40034000
#define S32G2_MC_CGM5_BASE          0x40068000
#define S32G2_MC_ME_BASE            0x40088000
#define S32G2_MC_ME_USDHC_BLOCK     0   /* partition 0, COFB block 0 */
#define S32G2_MC_ME_DDR_BLOCK       1   /* partition 0, COFB block 1 */
/* The system counter runs at FXOSC / 8 once the crystal is up. */
#define S32G2_CNTFRQ_HZ             5000000

/* I2C4 and the VR5510 PMIC behind it. The controller clock is XBAR / 2 / 3. */
#define S32G2_I2C4_BASE             0x402DC000
/* XBAR_DIV3: XBAR_2X / 2 / 3, so 8 MHz on FIRC and 133 MHz once the tree is up */
#define S32G2_I2C_CLK_FIRC_HZ       8000000
#define S32G2_I2C_SPEED_HZ          100000
#define S32G2_VR5510_FSU_ADDR       0x21
#define S32G2_SIUL2_1_IMCR_BASE     0x44010C1C  /* IMCR n at + 4 * (n - 631) */
#define S32G2_SIUL2_1_IMCR_START    631
#define S32G2_I2C4_SDA_MSCR         33          /* PC01 */
#define S32G2_I2C4_SCL_MSCR         34          /* PC02 */
#define S32G2_I2C4_SDA_IMCR         724
#define S32G2_I2C4_SCL_IMCR         723
/* ALT1/ALT2 | OBE | ODE | IBE | SRE 133 MHz; IMCR source ALT3 */
#define S32G2_I2C4_SDA_MSCR_VAL     0x00398001
#define S32G2_I2C4_SCL_MSCR_VAL     0x00398002
#define S32G2_I2C4_IMCR_VAL         0x3

/* Interconnect, interrupt router and GICv3. */
#define S32G2_NCORE_BASE            0x50400000
#define S32G2_NCORE_CAIUTC          0x50400000  /* CAIU0 transaction control */
#define S32G2_NCORE_CAIUTC_ISOLEN   (1U << 1)
#define S32G2_MSCM_BASE             0x40198000
#define S32G2_MSCM_IRSPRC           0x880       /* 16-bit routing per SPI */
#define S32G2_MSCM_IRSPRC_NUM       240
#define S32G2_GICD_BASE             0x50800000
#define S32G2_GICR_BASE             0x50880000
#define S32G2_GICR_STRIDE           0x20000
#ifndef __ASSEMBLER__   /* src/boot_aarch64_start.S has its own GICD names */
#define GICD_CTLR                   0x000
#define GICD_TYPER                  0x004
#define GICD_IGROUPR                0x080
#define GICD_IGROUPMODR             0xD00
#define GICD_CTLR_ENGRP0            (1U << 0)
#define GICD_CTLR_ENGRP1NS          (1U << 1)
#define GICD_CTLR_ENGRP1S           (1U << 2)
#define GICD_CTLR_ARE_S             (1U << 4)
#define GICD_CTLR_ARE_NS            (1U << 5)
#define GICR_TYPER                  0x008
#define GICR_WAKER                  0x014
#define GICR_WAKER_PROCESSORSLEEP   (1U << 1)
#define GICR_WAKER_CHILDRENASLEEP   (1U << 2)
#define GICR_SGI_BASE               0x10000
#define GICR_IGROUPR0               0x080
#define GICR_ISENABLER0             0x100
#define GICR_IGROUPMODR0            0xD00
#endif

/* Spin table for the secondary cores, in the SRAM page below the image, one
 * 64-byte slot per linear core id (aff1 * 2 + aff0): entry at +0, status at
 * +8. The kernel's cpu-release-addr points at the entry word. The secondary
 * stacks sit above the table; the whole 32 KB is ECC-initialised at entry. */
#define S32G2_NUM_CORES             4
#define S32G2_SPIN_TABLE_BASE       0x34000000
#define S32G2_SPIN_TABLE_STRIDE     0x40
#define S32G2_SPIN_TABLE_STATUS     8
#define S32G2_SECONDARY_STACK_TOP   0x34004000  /* 2 KB per core, downwards */
#define S32G2_SECONDARY_STACK_SIZE  0x800
#define S32G2_SRAM_LOW_INIT_END     0x34008000

#ifndef __ASSEMBLER__
int s32g2_cgm_mux_set(uintptr_t cgm, uint32_t mux, uint32_t src);
uint32_t s32g2_cgm_mux_get(uintptr_t cgm, uint32_t mux);
int s32g2_ddr_init(void);
int s32g2_ddr_polling_needed(void);
void hal_delay_us(uint32_t us);
#endif

/* DRAM bank 0 at 2 GB, bank 1 at 34 GB. The sizes are the GoldBox's 4 GB
 * LPDDR4 and must match what the DDR blob trains. */
#define S32G2_DRAM_BASE             0x80000000
#define S32G2_DRAM_BANK1_BASE       0x880000000ULL
#ifndef S32G2_DRAM_BANK0_SIZE
#define S32G2_DRAM_BANK0_SIZE       0x80000000ULL
#endif
#ifndef S32G2_DRAM_BANK1_SIZE
#define S32G2_DRAM_BANK1_SIZE       0x80000000ULL
#endif
#define S32G2_DRAM_SIZE             (S32G2_DRAM_BANK0_SIZE + S32G2_DRAM_BANK1_SIZE)

/* DRAM staging addresses for the address hooks of builds without fixed
 * partitions. */
#define S32G2_RAM_BOOT_ADDR         0x90000000
#define S32G2_RAM_UPDATE_ADDR       0x98000000

/* Generic timer fallback if firmware left CNTFRQ_EL0 unprogrammed; the S32G2
 * system counter runs at FXOSC/8 = 5 MHz. */
#ifndef TIMER_CLK_FREQ
#define TIMER_CLK_FREQ              5000000
#endif

/* Console LINFlexD_0 (ttyLF0): stdout-path in s32g274a-rdb2.dts, pins PC09/PC10,
 * 115200 8N1. LINFlexD_1 at 0x401CC000 is the second FTDI port. */
#define S32G2_LINFLEX0_BASE         0x401C8000
#define LINFLEX_LINCR1              0x00
#define LINFLEX_LINSR               0x08
#define LINFLEX_UARTCR              0x10
#define LINFLEX_UARTSR              0x14
#define LINFLEX_LINFBRR             0x24
#define LINFLEX_LINIBRR             0x28
#define LINFLEX_BDRL                0x38
#define LINFLEX_UARTPTO             0x50

#define LINFLEX_LINCR1_INIT         (1 << 0)
#define LINFLEX_LINCR1_MME          (1 << 4)
#define LINFLEX_LINSR_LINS_MASK     (0xF << 12)
#define LINFLEX_LINSR_LINS_INIT     (0x1 << 12)
#define LINFLEX_UARTCR_UART         (1 << 0)
/* UART | WL0 | PC0 | TXEN | RXEN | PC1 | TFBM | RFBM: 8N1, FIFO mode on both
 * directions, which is also how U-Boot and TF-A leave the port. */
#define LINFLEX_UARTCR_8N1_FIFO     0x37B
#define LINFLEX_UARTSR_DTF          (1 << 1)    /* TX FIFO full (FIFO mode) */

/* LIN_BAUD clock as the earlier stage programs it (PERIPH_PLL_PHI3), and the
 * 115200 divisor from it: 125e6 / (16 * 115200) = 67.8 -> 67 + 13/16. */
#define S32G2_LINIBRR_115200        67
#define S32G2_LINFBRR_115200        13

/* uSDHC0: the only SD/MMC host on S32G2. The GoldBox routes it to the microSD
 * slot by default and to the on-board eMMC through a board mux. */
#define S32G2_USDHC0_BASE           0x402F0000

/* uSDHC registers and the shim interface (hal/imx_usdhc_sdhci.c) */
#include "imx_usdhc_sdhci.h"

/* uSDHC per clock as the earlier stage programs it (PERIPH_DFS3 / 2). */
#ifndef S32G2_USDHC_PERCLK_HZ
#define S32G2_USDHC_PERCLK_HZ       400000000
#endif
#define IMX_USDHC_BASE              S32G2_USDHC0_BASE
#define IMX_USDHC_PERCLK_HZ         S32G2_USDHC_PERCLK_HZ
#ifdef S32G2_USDHC_MAX_CLK_KHZ
#define IMX_USDHC_MAX_CLK_KHZ       S32G2_USDHC_MAX_CLK_KHZ
#endif

#endif /* _S32G2_H_ */
