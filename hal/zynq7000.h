/* zynq7000.h
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

/* Xilinx Zynq-7000 (Cortex-A9, ARMv7-A 32-bit) HAL register map.
 * Reference: UG585 (Zynq-7000 TRM), UG821 (Zynq-7000 SW Dev Guide).
 * Target board: ZC702 Evaluation Kit (XC7Z020).
 */

#ifndef _ZYNQ7000_H_
#define _ZYNQ7000_H_

#include <stdint.h>

/* DDR memory range (PS DDR3 on ZC702: 1 GB) */
#define Z7_DDR_BASE     0x00000000UL
#define Z7_DDR_HIGH     0x3FFFFFFFUL

/* On-chip memory (OCM, 256 KB at high alias when remapped) */
#define Z7_OCM_BASE     0xFFFC0000UL

/* SLCR (System Level Control Registers) - UG585 ch.4 */
#define Z7_SLCR_BASE        0xF8000000UL
#define Z7_SLCR_UNLOCK      (*((volatile uint32_t*)(Z7_SLCR_BASE + 0x008)))
#define Z7_SLCR_LOCK        (*((volatile uint32_t*)(Z7_SLCR_BASE + 0x004)))
#define Z7_SLCR_UART_RST    (*((volatile uint32_t*)(Z7_SLCR_BASE + 0x228)))
#define Z7_SLCR_LQSPI_RST   (*((volatile uint32_t*)(Z7_SLCR_BASE + 0x204)))
#define Z7_SLCR_UART_CLK    (*((volatile uint32_t*)(Z7_SLCR_BASE + 0x154)))
#define Z7_SLCR_LQSPI_CLK   (*((volatile uint32_t*)(Z7_SLCR_BASE + 0x14C)))
#define Z7_SLCR_UNLOCK_KEY  0x0000DF0DUL
#define Z7_SLCR_LOCK_KEY    0x0000767BUL

/* UART (XUartPs) - UG585 ch.19. Same IP as ZynqMP, different base. */
#define Z7_UART0_BASE       0xE0000000UL
#define Z7_UART1_BASE       0xE0001000UL

#if defined(DEBUG_UART_NUM) && DEBUG_UART_NUM == 0
    #define DEBUG_UART_BASE     Z7_UART0_BASE
#elif defined(DEBUG_UART_NUM) && DEBUG_UART_NUM == 1
    #define DEBUG_UART_BASE     Z7_UART1_BASE
#endif
#ifndef DEBUG_UART_BASE
    /* ZC702 console is wired to UART1 (MIO48/49) */
    #define DEBUG_UART_BASE     Z7_UART1_BASE
#endif

#define Z7_UART_CR          (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x00)))
#define Z7_UART_MR          (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x04)))
#define Z7_UART_IDR         (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x0C)))
#define Z7_UART_ISR         (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x14)))
#define Z7_UART_BR_GEN      (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x18)))
#define Z7_UART_RXTOUT      (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x1C)))
#define Z7_UART_RXWM        (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x20)))
#define Z7_UART_SR          (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x2C)))
#define Z7_UART_FIFO        (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x30)))
#define Z7_UART_BR_DIV      (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x34)))
#define Z7_UART_TXWM        (*((volatile uint32_t*)(DEBUG_UART_BASE + 0x44)))

#define Z7_UART_CR_TX_DIS   0x00000020U
#define Z7_UART_CR_TX_EN    0x00000010U
#define Z7_UART_CR_RX_DIS   0x00000008U
#define Z7_UART_CR_RX_EN    0x00000004U
#define Z7_UART_CR_TXRST    0x00000002U
#define Z7_UART_CR_RXRST    0x00000001U
#define Z7_UART_ISR_MASK    0x00003FFFU
#define Z7_UART_MR_8N1      0x00000020U  /* parity none, 8 data, 1 stop */
#define Z7_UART_SR_TXFULL   0x00000010U
#define Z7_UART_SR_TXEMPTY  0x00000008U

/* PS UART_REF_CLK on ZC702 is 50 MHz (IO_PLL / 20).
 * BR_GEN = ref / (baud * (BR_DIV + 1)). For 115200 with BR_DIV=6 -> BR_GEN=62.
 */
#ifndef UART_CLK_REF
    #define UART_CLK_REF    50000000U
#endif
#ifndef DEBUG_UART_BAUD
    #define DEBUG_UART_BAUD 115200U
    #define DEBUG_UART_DIV  6U
#endif

/* QSPI controller (XQspiPs - the older "Linear/Static" QSPI on Z7,
 * NOT the GQSPI on ZynqMP). UG585 ch.12. */
#define Z7_QSPI_BASE        0xE000D000UL
#define Z7_QSPI_LINEAR_BASE 0xFC000000UL  /* XIP window for linear-mode reads */

#define Z7_QSPI_CR          (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x00)))
#define Z7_QSPI_ISR         (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x04)))
#define Z7_QSPI_IER         (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x08)))
#define Z7_QSPI_IDR         (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x0C)))
#define Z7_QSPI_IMR         (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x10)))
#define Z7_QSPI_EN          (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x14)))
#define Z7_QSPI_DELAY       (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x18)))
#define Z7_QSPI_TXD0        (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x1C)))
#define Z7_QSPI_RXD         (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x20)))
#define Z7_QSPI_SICR        (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x24)))
#define Z7_QSPI_TXTHR       (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x28)))
#define Z7_QSPI_RXTHR       (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x2C)))
#define Z7_QSPI_GPIO        (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x30)))
#define Z7_QSPI_LPBK        (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x38)))
#define Z7_QSPI_TXD1        (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x80)))
#define Z7_QSPI_TXD2        (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x84)))
#define Z7_QSPI_TXD3        (*((volatile uint32_t*)(Z7_QSPI_BASE + 0x88)))
#define Z7_QSPI_LQSPI_CR    (*((volatile uint32_t*)(Z7_QSPI_BASE + 0xA0)))
#define Z7_QSPI_LQSPI_STS   (*((volatile uint32_t*)(Z7_QSPI_BASE + 0xA4)))
#define Z7_QSPI_MODID       (*((volatile uint32_t*)(Z7_QSPI_BASE + 0xFC)))

/* QSPI Config Register (CR) bits.
 * PCS is a 4-bit slave-select decode field [13:10]: 0xF = all CS deasserted,
 * 0xE = CS0 active. We mask the whole 4-bit field, not just bit 10.
 */
#define Z7_QSPI_CR_IFMODE       0x80000000U  /* flash mem interface mode */
#define Z7_QSPI_CR_HOLD_B       0x00080000U  /* drive HOLD high */
#define Z7_QSPI_CR_MANSTRT      0x00010000U  /* manual start command (kick) */
#define Z7_QSPI_CR_MANSTRTEN    0x00008000U  /* manual start enable */
#define Z7_QSPI_CR_SSFORCE      0x00004000U  /* manual CS control */
#define Z7_QSPI_CR_PCS_MASK     0x00003C00U  /* PCS field [13:10] */
#define Z7_QSPI_CR_PCS_NONE     0x00003C00U  /* all CS deasserted (0xF<<10) */
#define Z7_QSPI_CR_PCS_CS0      0x00003800U  /* CS0 asserted (0xE<<10) */
#define Z7_QSPI_CR_REF_CLK      0x00000100U
#define Z7_QSPI_CR_FIFO_WIDTH   0x000000C0U  /* must be 11 (32-bit) */
#define Z7_QSPI_CR_BAUD_DIV_MSK 0x00000038U
/* BAUDDIV field is value N in bits[5:3]; clock = ref_clk / 2^(N+1).
 * N=1 -> /4, N=2 -> /8, N=3 -> /16. */
#define Z7_QSPI_CR_BAUD_DIV_4   0x00000008U  /* /4  (BAUDDIV=1) */
#define Z7_QSPI_CR_BAUD_DIV_8   0x00000010U  /* /8  (BAUDDIV=2) */
#define Z7_QSPI_CR_BAUD_DIV_16  0x00000018U  /* /16 (BAUDDIV=3) */
#define Z7_QSPI_CR_CPHA         0x00000004U
#define Z7_QSPI_CR_CPOL         0x00000002U
#define Z7_QSPI_CR_MSTREN       0x00000001U

/* QSPI Interrupt Status Register (ISR) bits */
#define Z7_QSPI_ISR_TXUF        0x00000040U  /* TX underflow */
#define Z7_QSPI_ISR_RXFULL      0x00000020U  /* RX FIFO full */
#define Z7_QSPI_ISR_RXNEMPTY    0x00000010U  /* RX FIFO not empty */
#define Z7_QSPI_ISR_TXFULL      0x00000008U  /* TX FIFO full */
#define Z7_QSPI_ISR_TXNFULL     0x00000004U  /* TX FIFO threshold */
#define Z7_QSPI_ISR_RXOVR       0x00000001U  /* RX overrun */
#define Z7_QSPI_ISR_MASK        0x0000007DU

#define Z7_QSPI_EN_VAL          0x00000001U  /* enable controller */

/* SLCR clock/reset for QSPI (FSBL normally pre-configures these) */
#define Z7_SLCR_LQSPI_CLK_DIV_MSK   0x00003F00U
#define Z7_SLCR_LQSPI_CLK_DIV_5     0x00000500U
#define Z7_SLCR_LQSPI_CLK_SRCSEL_M  0x00000030U
#define Z7_SLCR_LQSPI_CLK_CLKACT0   0x00000001U
#define Z7_SLCR_LQSPI_RST_REF       0x00000002U
#define Z7_SLCR_LQSPI_RST_CPU       0x00000001U

/* SDIO (Arasan SDHCI v2.0). UG585 ch.10. */
#define Z7_SDIO0_BASE       0xE0100000UL
#define Z7_SDIO1_BASE       0xE0101000UL

/* SDIO clock/reset via SLCR. UG585 ch.4. */
#define Z7_SLCR_SDIO_CLK    (*((volatile uint32_t*)(Z7_SLCR_BASE + 0x150)))
#define Z7_SLCR_SDIO_RST    (*((volatile uint32_t*)(Z7_SLCR_BASE + 0x218)))
#define Z7_SLCR_APER_CLK    (*((volatile uint32_t*)(Z7_SLCR_BASE + 0x12C)))
/* SDIO_CLK_CTRL: CLKACT0/1 (bits 0/1), SRCSEL (bits 5:4 = 00 IO_PLL),
 * DIVISOR (bits 13:8). For 50 MHz SDIO ref from 1 GHz IO_PLL, DIVISOR=20. */
#define Z7_SLCR_SDIO_CLK_ACT0   0x00000001U
#define Z7_SLCR_SDIO_CLK_ACT1   0x00000002U
#define Z7_SLCR_SDIO_CLK_DIV_SH 8
#define Z7_SLCR_SDIO_CLK_DIV_MSK 0x00003F00U
#define Z7_SLCR_SDIO_RST_REF0   0x00000010U
#define Z7_SLCR_SDIO_RST_REF1   0x00000020U
#define Z7_SLCR_SDIO_RST_CPU0   0x00000001U
#define Z7_SLCR_SDIO_RST_CPU1   0x00000002U
#define Z7_SLCR_APER_SDIO0      0x00000400U  /* SDIO0 AMBA APER clock enable */
#define Z7_SLCR_APER_SDIO1      0x00000800U

/* Cortex-A9 Global Timer (64-bit, increments at PERIPHCLK = CPU_3x2x).
 * UG585 ch.3.5.4. */
#define Z7_GTIMER_LO        (*((volatile uint32_t*)(Z7_GTIMER_BASE + 0x00)))
#define Z7_GTIMER_HI        (*((volatile uint32_t*)(Z7_GTIMER_BASE + 0x04)))
#define Z7_GTIMER_CTRL      (*((volatile uint32_t*)(Z7_GTIMER_BASE + 0x08)))
#define Z7_GTIMER_CTRL_EN   0x00000001U
/* The Cortex-A9 Global Timer runs at PERIPHCLK, which on Zynq-7000 is the
 * CPU_3x2x clock = CPU_6x4x / 2. With the default ZC702 FSBL clock plan
 * (ARM_PLL = 1.333 GHz, CPU_6x4x = ARM_PLL/2 = 666.67 MHz), PERIPHCLK is
 * 333.33 MHz. Override at compile time if you reclock the CPU. */
#ifndef Z7_GTIMER_FREQ_HZ
#define Z7_GTIMER_FREQ_HZ   333333333UL
#endif

/* DevC (Device Configuration: AES + bitstream loader). UG585 ch.6. */
#define Z7_DEVC_BASE        0xF8007000UL

/* GIC (PL390 / GIC-400 v1) - per-CPU interface and distributor. */
#define Z7_GIC_CPUIF_BASE   0xF8F00100UL
#define Z7_GIC_DIST_BASE    0xF8F01000UL

/* PL310 L2 cache controller. UG585 ch.3. */
#define Z7_PL310_BASE       0xF8F02000UL

/* SCU + private timer/watchdog. UG585 ch.3. */
#define Z7_SCU_BASE         0xF8F00000UL
#define Z7_GTIMER_BASE      0xF8F00200UL
#define Z7_PTIMER_BASE      0xF8F00600UL

#endif /* _ZYNQ7000_H_ */
