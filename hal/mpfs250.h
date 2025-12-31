/* mpfs250.h
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#ifndef MPFS250_DEF_INCLUDED
#define MPFS250_DEF_INCLUDED

/* Generic RISC-V definitions are included at the end of this file
 * (after PLIC_BASE is defined) to enable PLIC function declarations */

/* PolarFire SoC MPFS250T board specific configuration */

/* APB/AHB Clock Frequency */
#define MSS_APB_AHB_CLK    150000000

/* Hardware Base Address */
#define SYSREG_BASE 0x20002000

/* Write "0xDEAD" to cause a full MSS reset*/
#define SYSREG_MSS_RESET_CR (*((volatile uint32_t*)(SYSREG_BASE + 0x18)))

/* Peripheral Soft Reset Control Register */
#define SYSREG_SOFT_RESET_CR (*((volatile uint32_t*)(SYSREG_BASE + 0x88)))
#define SYSREG_SOFT_RESET_CR_ENVM (1U << 0)
#define SYSREG_SOFT_RESET_CR_MMC  (1U << 3)
#define SYSREG_SOFT_RESET_CR_MMUART0 (1U << 5)
#define SYSREG_SOFT_RESET_CR_MMUART1 (1U << 6)
#define SYSREG_SOFT_RESET_CR_MMUART2 (1U << 7)
#define SYSREG_SOFT_RESET_CR_MMUART3 (1U << 8)
#define SYSREG_SOFT_RESET_CR_MMUART4 (1U << 9)
#define SYSREG_SOFT_RESET_CR_SPI0 (1U << 10)
#define SYSREG_SOFT_RESET_CR_SPI1 (1U << 11)
#define SYSREG_SOFT_RESET_CR_QSPI (1U << 19)
#define SYSREG_SOFT_RESET_CR_GPIO0 (1U << 20)
#define SYSREG_SOFT_RESET_CR_GPIO1 (1U << 21)
#define SYSREG_SOFT_RESET_CR_GPIO2 (1U << 22)
#define SYSREG_SOFT_RESET_CR_DDRC (1U << 23)
#define SYSREG_SOFT_RESET_CR_ATHENA (1U << 28) /* Crypto hardware accelerator */


/* UART */
#define MSS_UART0_LO_BASE  0x20000000UL
#define MSS_UART1_LO_BASE  0x20100000UL
#define MSS_UART2_LO_BASE  0x20102000UL
#define MSS_UART3_LO_BASE  0x20104000UL
#define MSS_UART4_LO_BASE  0x20106000UL

#define MSS_UART0_HI_BASE  0x28000000UL
#define MSS_UART1_HI_BASE  0x28100000UL
#define MSS_UART2_HI_BASE  0x28102000UL
#define MSS_UART3_HI_BASE  0x28104000UL
#define MSS_UART4_HI_BASE  0x28106000UL

#define MMUART_RBR(base) *((volatile uint8_t*)((base)) + 0x00) /* Receiver buffer register */
#define MMUART_IER(base) *((volatile uint8_t*)((base)) + 0x04) /* Interrupt enable register */
#define MMUART_IIR(base) *((volatile uint8_t*)((base)) + 0x08) /* Interrupt ID register */
#define MMUART_LCR(base) *((volatile uint8_t*)((base)) + 0x0C) /* Line control register */
#define MMUART_MCR(base) *((volatile uint8_t*)((base)) + 0x10) /* Modem control register */
#define MMUART_LSR(base) *((volatile uint8_t*)((base)) + 0x14) /* Line status register */
#define MMUART_MSR(base) *((volatile uint8_t*)((base)) + 0x18) /* Modem status register */
#define MMUART_SCR(base) *((volatile uint8_t*)((base)) + 0x1C) /* Scratch register */
#define MMUART_IEM(base) *((volatile uint8_t*)((base)) + 0x24) /* Interrupt enable mask */
#define MMUART_IIM(base) *((volatile uint8_t*)((base)) + 0x28) /* multi-mode Interrupt ID register */
#define MMUART_MM0(base) *((volatile uint8_t*)((base)) + 0x30) /* Mode register 0 */
#define MMUART_MM1(base) *((volatile uint8_t*)((base)) + 0x34) /* Mode register 1 */
#define MMUART_MM2(base) *((volatile uint8_t*)((base)) + 0x38) /* Mode register 2 */
#define MMUART_DFR(base) *((volatile uint8_t*)((base)) + 0x3C) /* Data frame register */
#define MMUART_GFR(base) *((volatile uint8_t*)((base)) + 0x44) /* Global filter register */
#define MMUART_TTG(base) *((volatile uint8_t*)((base)) + 0x48) /* TX time guard register */
#define MMUART_RTO(base) *((volatile uint8_t*)((base)) + 0x4C) /* RX timeout register */
#define MMUART_ADR(base) *((volatile uint8_t*)((base)) + 0x50) /* Address register */
#define MMUART_DLR(base) *((volatile uint8_t*)((base)) + 0x80) /* Divisor latch register */
#define MMUART_DMR(base) *((volatile uint8_t*)((base)) + 0x84) /* Divisor mode register */
#define MMUART_THR(base) *((volatile uint8_t*)((base)) + 0x100) /* Transmitter holding register */
#define MMUART_FCR(base) *((volatile uint8_t*)((base)) + 0x104) /* FIFO control register */


/* LCR (Line Control Register) */
#define MSS_UART_DATA_8_BITS        ((uint8_t)0x03)
#define MSS_UART_NO_PARITY          ((uint8_t)0x00)
#define MSS_UART_ONE_STOP_BIT       ((uint8_t)0x00)

/* LSR (Line Status Register) */
#define MSS_UART_THRE               ((uint8_t)0x20)    /* Transmitter holding register empty */
#define MSS_UART_TEMT               ((uint8_t)0x40)    /* Transmit empty */

#define ELIN_MASK            (1U << 3) /* Enable LIN header detection */
#define EIRD_MASK            (1U << 2) /* Enable IrDA modem */
#define EERR_MASK            (1U << 0) /* Enable ERR / NACK during stop time */

#define RXRDY_TXRDYN_EN_MASK (1U << 0) /* Enable TXRDY and RXRDY signals */
#define CLEAR_RX_FIFO_MASK   (1U << 1) /* Clear receiver FIFO */
#define CLEAR_TX_FIFO_MASK   (1U << 2) /* Clear transmitter FIFO */

#define LOOP_MASK            (1U << 4) /* Local loopback */
#define RLOOP_MASK           (1U << 5) /* Remote loopback & Automatic echo*/

#define E_MSB_RX_MASK        (1U << 0) /* MSB / LSB first for receiver */
#define E_MSB_TX_MASK        (1U << 1) /* MSB / LSB first for transmitter */

#define EAFM_MASK            (1U << 1) /* Enable 9-bit address flag mode */
#define ETTG_MASK            (1U << 5) /* Enable transmitter time guard */
#define ERTO_MASK            (1U << 6) /* Enable receiver time-out */
#define ESWM_MASK            (1U << 3) /* Enable single wire half-duplex mode */
#define EFBR_MASK            (1U << 7) /* Enable fractional baud rate mode */

/* Line Control register bit masks */
#define SB_MASK              (1U << 6) /* Set break */
#define DLAB_MASK            (1U << 7) /* Divisor latch access bit */

/* ============================================================================
 * EMMC/SD Card Controller (Cadence SD4HC)
 * Base Address: 0x20008000
 * ============================================================================ */
#define EMMC_SD_BASE 0x20008000UL


/* Crypto Engine: Athena F5200 TeraFire Crypto Processor (1x), 200 MHz */
#define ATHENA_BASE (SYSREG_BASE + 0x125000)



/* ============================================================================
 * PLIC - Platform-Level Interrupt Controller (MPFS250-specific configuration)
 * Base Address: 0x0c000000, Size: 64MB
 *
 * Generic PLIC register access is provided by hal/riscv.h
 * ============================================================================ */
#define PLIC_BASE               0x0C000000UL
#define PLIC_SIZE               0x04000000UL  /* 64MB */

/* Number of interrupt sources and contexts */
#define PLIC_NUM_SOURCES        186           /* riscv,ndev = 0xBA = 186 */
#define PLIC_NUM_HARTS          5             /* 1x E51 + 4x U54 */
#define PLIC_NUM_CONTEXTS       10            /* 2 contexts per hart (M-mode + S-mode) */

/* MSS Global Interrupt offset - PLIC interrupts 0-12 are local, 13+ are MSS */
#define OFFSET_TO_MSS_GLOBAL_INTS   13

/* PLIC Interrupt Sources (PLIC IRQ numbers) */
#define PLIC_INT_MMC_MAIN       88            /* MMC/SD controller main interrupt */
#define PLIC_INT_MMC_WAKEUP     89            /* MMC/SD controller wakeup interrupt */

/* PLIC Context IDs for each hart
 * Hart 0 (E51):  Context 0 = M-mode (no S-mode on E51)
 * Hart 1 (U54):  Context 1 = M-mode, Context 2 = S-mode
 * Hart 2 (U54):  Context 3 = M-mode, Context 4 = S-mode
 * Hart 3 (U54):  Context 5 = M-mode, Context 6 = S-mode
 * Hart 4 (U54):  Context 7 = M-mode, Context 8 = S-mode
 */
#define PLIC_CONTEXT_E51_M      0
#define PLIC_CONTEXT_U54_1_M    1
#define PLIC_CONTEXT_U54_1_S    2
#define PLIC_CONTEXT_U54_2_M    3
#define PLIC_CONTEXT_U54_2_S    4
#define PLIC_CONTEXT_U54_3_M    5
#define PLIC_CONTEXT_U54_3_S    6
#define PLIC_CONTEXT_U54_4_M    7
#define PLIC_CONTEXT_U54_4_S    8


#endif /* MPFS250_DEF_INCLUDED */

