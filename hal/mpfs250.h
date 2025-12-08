/* mpfs250.h
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

/* PolarFire SoC MPFS250T board specific configuration */

#define MSS_APB_AHB_CLK    150000000

/* Hardware Base Address */
#define SYSREG_BASE 0x20002000

/* Write "0xDEAD" to cause a full MSS reset*/
#define SYSREG_MSS_RESET_CR (*((volatile uint32_t*)(SYSREG_BASE + 0x18)))

/* TODO: Add support for machine mode wolfBoot */
#if 1
    #define WOLFBOOT_RISCV_SMODE /* supervisor mode */
#else
    #define WOLFBOOT_RISCV_MMODE /* machine mode */
#endif

/* size of each trap frame register */
#define REGBYTES (1 << 3)

/* Machine Information Registers */
#define CSR_MVENDORID    0xf11
#define CSR_MARCHID      0xf12
#define CSR_MIMPID       0xf13
#define CSR_MHARTID      0xf14

/* Initial stack pointer address (stack grows downward from here) */
#ifdef WOLFBOOT_RISCV_SMODE
#define WOLFBOOT_STACK_TOP 0x80200000
#else
#define WOLFBOOT_STACK_TOP 0x80000000
#endif

#ifdef WOLFBOOT_RISCV_SMODE
#define MODE_PREFIX(__suffix)    s##__suffix
#else
#define MODE_PREFIX(__suffix)    m##__suffix
#endif

/* SIE (Interrupt Enable) and SIP (Interrupt Pending) flags */
#define IRQ_U_SOFT   0
#define IRQ_S_SOFT   1
#define IRQ_M_SOFT   3
#define IRQ_U_TIMER  4
#define IRQ_S_TIMER  5
#define IRQ_M_TIMER  7
#define IRQ_U_EXT    8
#define IRQ_S_EXT    9
#define IRQ_M_EXT    11
#define MIE_MSIE     (1 << IRQ_M_SOFT)
#define SIE_SSIE     (1 << IRQ_S_SOFT)
#define SIE_STIE     (1 << IRQ_S_TIMER)
#define SIE_SEIE     (1 << IRQ_S_EXT)



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

#define ELIN_MASK                   (0x01u << 3u)      /* Enable LIN header detection */
#define EIRD_MASK                   (0x01u << 2u)      /* Enable IrDA modem */
#define EERR_MASK                   (0x01u << 0u)      /* Enable ERR / NACK during stop time */

#define RXRDY_TXRDYN_EN_MASK        (0x01u << 0u)      /* Enable TXRDY and RXRDY signals */
#define CLEAR_RX_FIFO_MASK          (0x01u << 1u)      /* Clear receiver FIFO */
#define CLEAR_TX_FIFO_MASK          (0x01u << 2u)      /* Clear transmitter FIFO */

#define LOOP_MASK                   (0x01u << 4u)      /* Local loopback */
#define RLOOP_MASK                  (0x01u << 5u)      /* Remote loopback & Automatic echo*/

#define E_MSB_RX_MASK               (0x01u << 0u)      /* MSB / LSB first for receiver */
#define E_MSB_TX_MASK               (0x01u << 1u)      /* MSB / LSB first for transmitter */

#define EAFM_MASK                   (0x01u << 1u)      /* Enable 9-bit address flag mode */
#define ETTG_MASK                   (0x01u << 5u)      /* Enable transmitter time guard */
#define ERTO_MASK                   (0x01u << 6u)      /* Enable receiver time-out */
#define ESWM_MASK                   (0x01u << 3u)      /* Enable single wire half-duplex mode */
#define EFBR_MASK                   (0x01u << 7u)      /* Enable fractional baud rate mode */

/* Line Control register bit masks */
#define SB_MASK                     (0x01u << 6)       /* Set break */
#define DLAB_MASK                   (0x01u << 7)       /* Divisor latch access bit */


#endif /* MPFS250_DEF_INCLUDED */

