/* s32k1xx.h
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
 *
 * Hardware register definitions for NXP S32K1xx (S32K142, S32K144, S32K146, S32K148)
 */

#ifndef S32K1XX_H
#define S32K1XX_H

#include <stdint.h>

/*
 * Clock configuration
 * S32K142 supports:
 *   - FIRC: 48 MHz Fast Internal RC
 *   - SIRC: 8 MHz Slow Internal RC
 *   - SOSC: 8-40 MHz System Oscillator (external crystal)
 *   - SPLL: System PLL (up to 160 MHz VCO, /2 for SPLL_CLK)
 *
 * Run modes:
 *   - RUN: Up to 80 MHz core clock (requires SPLL, currently using FIRC at 48 MHz)
 *   - HSRUN: Up to 112 MHz core clock (requires SOSC + SPLL, not implemented yet)
 *
 * Default: RUN mode with FIRC at 48 MHz (no external crystal required)
 * To enable HSRUN mode (112 MHz), define S32K1XX_CLOCK_HSRUN (requires SOSC + SPLL)
 */

/* ============== ARM Cortex-M4 System Registers ============== */

/* System Control Block (SCB) */
#define SCB_BASE            (0xE000ED00UL)
#define SCB_CPUID           (*(volatile uint32_t *)(SCB_BASE + 0x00UL))
#define SCB_ICSR            (*(volatile uint32_t *)(SCB_BASE + 0x04UL))
#define SCB_VTOR            (*(volatile uint32_t *)(SCB_BASE + 0x08UL))
#define SCB_AIRCR           (*(volatile uint32_t *)(SCB_BASE + 0x0CUL))
#define SCB_SCR             (*(volatile uint32_t *)(SCB_BASE + 0x10UL))
#define SCB_CCR             (*(volatile uint32_t *)(SCB_BASE + 0x14UL))

/* AIRCR - Application Interrupt and Reset Control Register */
#define AIRCR_VECTKEY       (0x05FAUL << 16)
#define AIRCR_SYSRESETREQ   (1UL << 2)

/* SysTick Timer */
#define SYST_BASE           (0xE000E010UL)
#define SYST_CSR            (*(volatile uint32_t *)(SYST_BASE + 0x00UL))
#define SYST_RVR            (*(volatile uint32_t *)(SYST_BASE + 0x04UL))
#define SYST_CVR            (*(volatile uint32_t *)(SYST_BASE + 0x08UL))
#define SYST_CALIB          (*(volatile uint32_t *)(SYST_BASE + 0x0CUL))

#define SYST_CSR_ENABLE     (1UL << 0)
#define SYST_CSR_TICKINT    (1UL << 1)
#define SYST_CSR_CLKSOURCE  (1UL << 2)
#define SYST_CSR_COUNTFLAG  (1UL << 16)

/* Clock speed (FIRC = 48 MHz) */
#ifndef CLOCK_SPEED
#define CLOCK_SPEED         48000000UL
#endif

/* ============== NVIC - Nested Vectored Interrupt Controller ============== */
#define NVIC_BASE           (0xE000E100UL)
#define NVIC_ISER(n)        (*(volatile uint32_t *)(NVIC_BASE + 0x000UL + 4*(n)))  /* Interrupt Set Enable */
#define NVIC_ICER(n)        (*(volatile uint32_t *)(NVIC_BASE + 0x080UL + 4*(n)))  /* Interrupt Clear Enable */
#define NVIC_ISPR(n)        (*(volatile uint32_t *)(NVIC_BASE + 0x100UL + 4*(n)))  /* Interrupt Set Pending */
#define NVIC_ICPR(n)        (*(volatile uint32_t *)(NVIC_BASE + 0x180UL + 4*(n)))  /* Interrupt Clear Pending */
#define NVIC_IPR(n)         (*(volatile uint32_t *)(NVIC_BASE + 0x300UL + 4*(n)))  /* Interrupt Priority */

/* S32K142 LPUART IRQ numbers */
#define LPUART0_IRQn        31
#define LPUART1_IRQn        33
#define LPUART2_IRQn        35

/* NVIC helper macros */
#define NVIC_EnableIRQ(irq)     NVIC_ISER((irq) >> 5) = (1UL << ((irq) & 0x1F))
#define NVIC_DisableIRQ(irq)    NVIC_ICER((irq) >> 5) = (1UL << ((irq) & 0x1F))
#define NVIC_SetPriority(irq, prio)  \
    do { \
        uint32_t _idx = (irq) >> 2; \
        uint32_t _shift = (((irq) & 0x3) << 3) + 4; \
        NVIC_IPR(_idx) = (NVIC_IPR(_idx) & ~(0xFUL << _shift)) | (((prio) & 0xF) << _shift); \
    } while(0)

/* ============== System Control Registers ============== */

/* SCG - System Clock Generator */
#define SCG_BASE            (0x40064000UL)
#define SCG_CSR             (*(volatile uint32_t *)(SCG_BASE + 0x010UL)) /* Clock Status Register */
#define SCG_RCCR            (*(volatile uint32_t *)(SCG_BASE + 0x014UL)) /* Run Clock Control Register */
#define SCG_VCCR            (*(volatile uint32_t *)(SCG_BASE + 0x018UL)) /* VLPR Clock Control Register */
#define SCG_HCCR            (*(volatile uint32_t *)(SCG_BASE + 0x01CUL)) /* HSRUN Clock Control Register */
#define SCG_CLKOUTCNFG      (*(volatile uint32_t *)(SCG_BASE + 0x020UL)) /* Clock Out Configuration */

/* SOSC - System OSC */
#define SCG_SOSCCSR         (*(volatile uint32_t *)(SCG_BASE + 0x100UL))
#define SCG_SOSCDIV         (*(volatile uint32_t *)(SCG_BASE + 0x104UL))
#define SCG_SOSCCFG         (*(volatile uint32_t *)(SCG_BASE + 0x108UL))

/* SIRC - Slow IRC */
#define SCG_SIRCCSR         (*(volatile uint32_t *)(SCG_BASE + 0x200UL))
#define SCG_SIRCDIV         (*(volatile uint32_t *)(SCG_BASE + 0x204UL))
#define SCG_SIRCCFG         (*(volatile uint32_t *)(SCG_BASE + 0x208UL))

/* FIRC - Fast IRC */
#define SCG_FIRCCSR         (*(volatile uint32_t *)(SCG_BASE + 0x300UL))
#define SCG_FIRCDIV         (*(volatile uint32_t *)(SCG_BASE + 0x304UL))
#define SCG_FIRCCFG         (*(volatile uint32_t *)(SCG_BASE + 0x308UL))

/* SPLL - System PLL */
#define SCG_SPLLCSR         (*(volatile uint32_t *)(SCG_BASE + 0x600UL))
#define SCG_SPLLDIV         (*(volatile uint32_t *)(SCG_BASE + 0x604UL))
#define SCG_SPLLCFG         (*(volatile uint32_t *)(SCG_BASE + 0x608UL))

/* SCG CSR fields */
#define SCG_CSR_SCS_SHIFT       24
#define SCG_CSR_SCS_MASK        (0xFUL << SCG_CSR_SCS_SHIFT)
#define SCG_CSR_SCS_FIRC        (3UL << SCG_CSR_SCS_SHIFT)
#define SCG_CSR_SCS_SPLL        (6UL << SCG_CSR_SCS_SHIFT)

/* SCG xCCR fields */
#define SCG_xCCR_SCS_SHIFT      24
#define SCG_xCCR_SCS_FIRC       (3UL << SCG_xCCR_SCS_SHIFT)
#define SCG_xCCR_SCS_SPLL       (6UL << SCG_xCCR_SCS_SHIFT)
#define SCG_xCCR_DIVCORE_SHIFT  16
#define SCG_xCCR_DIVBUS_SHIFT   4
#define SCG_xCCR_DIVSLOW_SHIFT  0

/* FIRC CSR fields */
#define SCG_FIRCCSR_FIRCEN      (1UL << 0)
#define SCG_FIRCCSR_FIRCVLD     (1UL << 24)

/* SPLL CSR fields */
#define SCG_SPLLCSR_SPLLEN      (1UL << 0)
#define SCG_SPLLCSR_SPLLVLD     (1UL << 24)

/* SPLL CFG fields */
#define SCG_SPLLCFG_MULT_SHIFT  16
#define SCG_SPLLCFG_PREDIV_SHIFT 8

/* SMC - System Mode Controller */
#define SMC_BASE            (0x4007E000UL)
#define SMC_PMPROT          (*(volatile uint32_t *)(SMC_BASE + 0x000UL))
#define SMC_PMCTRL          (*(volatile uint32_t *)(SMC_BASE + 0x004UL))
#define SMC_PMSTAT          (*(volatile uint32_t *)(SMC_BASE + 0x008UL))

#define SMC_PMPROT_AHSRUN   (1UL << 7)  /* Allow HSRUN */
#define SMC_PMCTRL_RUNM_SHIFT 5
#define SMC_PMCTRL_RUNM_RUN   (0UL << SMC_PMCTRL_RUNM_SHIFT)
#define SMC_PMCTRL_RUNM_HSRUN (3UL << SMC_PMCTRL_RUNM_SHIFT)
#define SMC_PMSTAT_HSRUN    (0x80UL)
#define SMC_PMSTAT_RUN      (0x01UL)

/* PCC - Peripheral Clock Controller */
#define PCC_BASE            (0x40065000UL)
#define PCC_PORTA           (*(volatile uint32_t *)(PCC_BASE + 0x124UL))
#define PCC_PORTB           (*(volatile uint32_t *)(PCC_BASE + 0x128UL))
#define PCC_PORTC           (*(volatile uint32_t *)(PCC_BASE + 0x12CUL))
#define PCC_LPUART0         (*(volatile uint32_t *)(PCC_BASE + 0x1A8UL))
#define PCC_LPUART1         (*(volatile uint32_t *)(PCC_BASE + 0x1ACUL))
#define PCC_LPUART2         (*(volatile uint32_t *)(PCC_BASE + 0x1B0UL))
#define PCC_FTFC            (*(volatile uint32_t *)(PCC_BASE + 0x0B0UL))

#define PCC_CGC             (1UL << 30)  /* Clock Gate Control */
#define PCC_PCS_SHIFT       24
#define PCC_PCS_FIRC        (3UL << PCC_PCS_SHIFT)  /* FIRC 48MHz */
#define PCC_PCS_SPLLDIV2    (6UL << PCC_PCS_SHIFT)  /* SPLL DIV2 */

/* ============== GPIO / Port Registers ============== */

/* PCC for GPIO ports */
#define PCC_PORTD           (*(volatile uint32_t *)(PCC_BASE + 0x130UL))
#define PCC_PORTE           (*(volatile uint32_t *)(PCC_BASE + 0x134UL))

/* Port A - LPUART0/LPUART2 alternate pins */
#define PORTA_BASE          (0x40049000UL)
#define PORTA_PCR(n)        (*(volatile uint32_t *)(PORTA_BASE + ((n) * 4)))
#define PORTA_PCR2          (*(volatile uint32_t *)(PORTA_BASE + 0x008UL))  /* LPUART0_RX (ALT6) */
#define PORTA_PCR3          (*(volatile uint32_t *)(PORTA_BASE + 0x00CUL))  /* LPUART0_TX (ALT6) */
#define PORTA_PCR8          (*(volatile uint32_t *)(PORTA_BASE + 0x020UL))  /* LPUART2_RX (ALT6) */
#define PORTA_PCR9          (*(volatile uint32_t *)(PORTA_BASE + 0x024UL))  /* LPUART2_TX (ALT6) */

/* Port B - LPUART0/LPUART1 alternate pins */
#define PORTB_BASE          (0x4004A000UL)
#define PORTB_PCR(n)        (*(volatile uint32_t *)(PORTB_BASE + ((n) * 4)))
#define PORTB_PCR0          (*(volatile uint32_t *)(PORTB_BASE + 0x000UL))  /* LPUART0_RX (ALT2) */
#define PORTB_PCR1          (*(volatile uint32_t *)(PORTB_BASE + 0x004UL))  /* LPUART0_TX (ALT2) */

/* Port C - LPUART1/LPUART2 pins */
#define PORTC_BASE          (0x4004B000UL)
#define PORTC_PCR(n)        (*(volatile uint32_t *)(PORTC_BASE + ((n) * 4)))
#define PORTC_PCR6          (*(volatile uint32_t *)(PORTC_BASE + 0x018UL))  /* LPUART1_RX (ALT2) */
#define PORTC_PCR7          (*(volatile uint32_t *)(PORTC_BASE + 0x01CUL))  /* LPUART1_TX (ALT2) */
#define PORTC_PCR8          (*(volatile uint32_t *)(PORTC_BASE + 0x020UL))  /* LPUART1_RX (ALT2) alt */
#define PORTC_PCR9          (*(volatile uint32_t *)(PORTC_BASE + 0x024UL))  /* LPUART1_TX (ALT2) alt */

/* Port D - LED pins on S32K142EVB, LPUART2 alternate pins */
#define PORTD_BASE          (0x4004C000UL)
#define PORTD_PCR(n)        (*(volatile uint32_t *)(PORTD_BASE + ((n) * 4)))
#define PORTD_PCR0          (*(volatile uint32_t *)(PORTD_BASE + 0x000UL))  /* Blue LED */
#define PORTD_PCR6          (*(volatile uint32_t *)(PORTD_BASE + 0x018UL))  /* LPUART2_RX (ALT2) */
#define PORTD_PCR7          (*(volatile uint32_t *)(PORTD_BASE + 0x01CUL))  /* LPUART2_TX (ALT2) */
#define PORTD_PCR15         (*(volatile uint32_t *)(PORTD_BASE + 0x03CUL))  /* Red LED */
#define PORTD_PCR16         (*(volatile uint32_t *)(PORTD_BASE + 0x040UL))  /* Green LED */

/* Port E - additional peripheral pins */
#define PORTE_BASE          (0x4004D000UL)
#define PORTE_PCR(n)        (*(volatile uint32_t *)(PORTE_BASE + ((n) * 4)))

/* GPIO D registers */
#define GPIOD_BASE          (0x400FF0C0UL)
#define GPIOD_PDOR          (*(volatile uint32_t *)(GPIOD_BASE + 0x00UL))  /* Data Output */
#define GPIOD_PSOR          (*(volatile uint32_t *)(GPIOD_BASE + 0x04UL))  /* Set Output */
#define GPIOD_PCOR          (*(volatile uint32_t *)(GPIOD_BASE + 0x08UL))  /* Clear Output */
#define GPIOD_PTOR          (*(volatile uint32_t *)(GPIOD_BASE + 0x0CUL))  /* Toggle Output */
#define GPIOD_PDIR          (*(volatile uint32_t *)(GPIOD_BASE + 0x10UL))  /* Data Input */
#define GPIOD_PDDR          (*(volatile uint32_t *)(GPIOD_BASE + 0x14UL))  /* Data Direction */

/* Port Control Register fields */
#define PORT_PCR_MUX_SHIFT  8
#define PORT_PCR_MUX_MASK   (7UL << PORT_PCR_MUX_SHIFT)
#define PORT_PCR_MUX_GPIO   (1UL << PORT_PCR_MUX_SHIFT)  /* GPIO mode */
#define PORT_PCR_MUX_ALT2   (2UL << PORT_PCR_MUX_SHIFT)  /* Alternate function 2 */
#define PORT_PCR_MUX_ALT3   (3UL << PORT_PCR_MUX_SHIFT)  /* Alternate function 3 */
#define PORT_PCR_MUX_ALT4   (4UL << PORT_PCR_MUX_SHIFT)  /* Alternate function 4 */
#define PORT_PCR_MUX_ALT5   (5UL << PORT_PCR_MUX_SHIFT)  /* Alternate function 5 */
#define PORT_PCR_MUX_ALT6   (6UL << PORT_PCR_MUX_SHIFT)  /* Alternate function 6 */
#define PORT_PCR_MUX_ALT7   (7UL << PORT_PCR_MUX_SHIFT)  /* Alternate function 7 */

/* S32K142EVB LED pins (accent RGB LED accent accent accent accent LED accent accent accent-low accent LED) */
#define LED_PIN_BLUE        0   /* PTD0 - Blue LED (active low) */
#define LED_PIN_RED         15  /* PTD15 - Red LED (active low) */
#define LED_PIN_GREEN       16  /* PTD16 - Green LED (active low) */

/* ============== LPUART Registers ============== */

/* LPUART base addresses */
#define LPUART0_BASE        (0x4006A000UL)
#define LPUART1_BASE        (0x4006B000UL)
#define LPUART2_BASE        (0x4006C000UL)

/* LPUART register offsets */
#define LPUART_VERID_OFF    0x000UL
#define LPUART_PARAM_OFF    0x004UL
#define LPUART_GLOBAL_OFF   0x008UL
#define LPUART_BAUD_OFF     0x010UL
#define LPUART_STAT_OFF     0x014UL
#define LPUART_CTRL_OFF     0x018UL
#define LPUART_DATA_OFF     0x01CUL

/* LPUART0 registers */
#define LPUART0_VERID       (*(volatile uint32_t *)(LPUART0_BASE + LPUART_VERID_OFF))
#define LPUART0_PARAM       (*(volatile uint32_t *)(LPUART0_BASE + LPUART_PARAM_OFF))
#define LPUART0_GLOBAL      (*(volatile uint32_t *)(LPUART0_BASE + LPUART_GLOBAL_OFF))
#define LPUART0_BAUD        (*(volatile uint32_t *)(LPUART0_BASE + LPUART_BAUD_OFF))
#define LPUART0_STAT        (*(volatile uint32_t *)(LPUART0_BASE + LPUART_STAT_OFF))
#define LPUART0_CTRL        (*(volatile uint32_t *)(LPUART0_BASE + LPUART_CTRL_OFF))
#define LPUART0_DATA        (*(volatile uint32_t *)(LPUART0_BASE + LPUART_DATA_OFF))

/* LPUART1 registers */
#define LPUART1_VERID       (*(volatile uint32_t *)(LPUART1_BASE + LPUART_VERID_OFF))
#define LPUART1_PARAM       (*(volatile uint32_t *)(LPUART1_BASE + LPUART_PARAM_OFF))
#define LPUART1_GLOBAL      (*(volatile uint32_t *)(LPUART1_BASE + LPUART_GLOBAL_OFF))
#define LPUART1_BAUD        (*(volatile uint32_t *)(LPUART1_BASE + LPUART_BAUD_OFF))
#define LPUART1_STAT        (*(volatile uint32_t *)(LPUART1_BASE + LPUART_STAT_OFF))
#define LPUART1_CTRL        (*(volatile uint32_t *)(LPUART1_BASE + LPUART_CTRL_OFF))
#define LPUART1_DATA        (*(volatile uint32_t *)(LPUART1_BASE + LPUART_DATA_OFF))

/* LPUART2 registers */
#define LPUART2_VERID       (*(volatile uint32_t *)(LPUART2_BASE + LPUART_VERID_OFF))
#define LPUART2_PARAM       (*(volatile uint32_t *)(LPUART2_BASE + LPUART_PARAM_OFF))
#define LPUART2_GLOBAL      (*(volatile uint32_t *)(LPUART2_BASE + LPUART_GLOBAL_OFF))
#define LPUART2_BAUD        (*(volatile uint32_t *)(LPUART2_BASE + LPUART_BAUD_OFF))
#define LPUART2_STAT        (*(volatile uint32_t *)(LPUART2_BASE + LPUART_STAT_OFF))
#define LPUART2_CTRL        (*(volatile uint32_t *)(LPUART2_BASE + LPUART_CTRL_OFF))
#define LPUART2_DATA        (*(volatile uint32_t *)(LPUART2_BASE + LPUART_DATA_OFF))

/* LPUART register field definitions */
#define LPUART_BAUD_OSR_SHIFT   24
#define LPUART_BAUD_SBR_SHIFT   0
#define LPUART_CTRL_TE          (1UL << 19)  /* Transmitter Enable */
#define LPUART_CTRL_RE          (1UL << 18)  /* Receiver Enable */
#define LPUART_CTRL_RIE         (1UL << 21)  /* Receiver Interrupt Enable */
#define LPUART_STAT_TDRE        (1UL << 23)  /* Transmit Data Register Empty */
#define LPUART_STAT_TC          (1UL << 22)  /* Transmission Complete */
#define LPUART_STAT_RDRF        (1UL << 21)  /* Receive Data Register Full */
#define LPUART_STAT_OR          (1UL << 19)  /* Overrun Flag */
#define LPUART_STAT_NF          (1UL << 18)  /* Noise Flag */
#define LPUART_STAT_FE          (1UL << 17)  /* Framing Error */
#define LPUART_STAT_PF          (1UL << 16)  /* Parity Error */

/* ============== LPUART Build-Time Configuration ============== */
/*
 * Select LPUART instance and pins at build time using these defines:
 *
 *   DEBUG_UART_NUM: LPUART instance (0, 1, or 2). Default: 1
 *
 *   DEBUG_UART_TX_PORT: Port for TX pin (S32K_PORT_A/B/C/D/E). Default depends on LPUART
 *   DEBUG_UART_TX_PIN:  Pin number for TX. Default depends on LPUART
 *   DEBUG_UART_TX_MUX:  Pin mux function. Default: PORT_PCR_MUX_ALT2
 *
 *   DEBUG_UART_RX_PORT: Port for RX pin (S32K_PORT_A/B/C/D/E). Default depends on LPUART
 *   DEBUG_UART_RX_PIN:  Pin number for RX. Default depends on LPUART
 *   DEBUG_UART_RX_MUX:  Pin mux function. Default: PORT_PCR_MUX_ALT2
 *
 * Example pin mappings for S32K1xx:
 *
 *   LPUART0:
 *     - PTB0 (RX, ALT2), PTB1 (TX, ALT2)  - Default
 *     - PTA2 (RX, ALT6), PTA3 (TX, ALT6)
 *
 *   LPUART1:
 *     - PTC6 (RX, ALT2), PTC7 (TX, ALT2)  - Default (S32K142EVB OpenSDA)
 *     - PTC8 (RX, ALT2), PTC9 (TX, ALT2)
 *
 *   LPUART2:
 *     - PTA8 (RX, ALT6), PTA9 (TX, ALT6)
 *     - PTD6 (RX, ALT2), PTD7 (TX, ALT2)  - Default
 *
 * Usage in .config file:
 *   CFLAGS_EXTRA+=-DDEBUG_UART_NUM=0
 *   CFLAGS_EXTRA+=-DDEBUG_UART_TX_PORT=S32K_PORT_B -DDEBUG_UART_TX_PIN=1
 *   CFLAGS_EXTRA+=-DDEBUG_UART_RX_PORT=S32K_PORT_B -DDEBUG_UART_RX_PIN=0
 */

/* Default LPUART instance */
#ifndef DEBUG_UART_NUM
#define DEBUG_UART_NUM  1
#endif

/* Map to selected LPUART registers based on DEBUG_UART_NUM */
#if DEBUG_UART_NUM == 0
    #define LPUART_BAUD     LPUART0_BAUD
    #define LPUART_STAT     LPUART0_STAT
    #define LPUART_CTRL     LPUART0_CTRL
    #define LPUART_DATA     LPUART0_DATA
    #define PCC_LPUART      PCC_LPUART0
#elif DEBUG_UART_NUM == 2
    #define LPUART_BAUD     LPUART2_BAUD
    #define LPUART_STAT     LPUART2_STAT
    #define LPUART_CTRL     LPUART2_CTRL
    #define LPUART_DATA     LPUART2_DATA
    #define PCC_LPUART      PCC_LPUART2
#else /* DEBUG_UART_NUM == 1 (default) */
    #define LPUART_BAUD     LPUART1_BAUD
    #define LPUART_STAT     LPUART1_STAT
    #define LPUART_CTRL     LPUART1_CTRL
    #define LPUART_DATA     LPUART1_DATA
    #define PCC_LPUART      PCC_LPUART1
#endif

/* Port identifier values for preprocessor comparisons */
#define S32K_PORT_A     0
#define S32K_PORT_B     1
#define S32K_PORT_C     2
#define S32K_PORT_D     3
#define S32K_PORT_E     4

/* Default pin configuration based on selected LPUART */
#if DEBUG_UART_NUM == 0
    /* LPUART0 defaults: PTB0 (RX), PTB1 (TX) */
    #ifndef DEBUG_UART_TX_PORT
    #define DEBUG_UART_TX_PORT  S32K_PORT_B
    #endif
    #ifndef DEBUG_UART_TX_PIN
    #define DEBUG_UART_TX_PIN   1
    #endif
    #ifndef DEBUG_UART_RX_PORT
    #define DEBUG_UART_RX_PORT  S32K_PORT_B
    #endif
    #ifndef DEBUG_UART_RX_PIN
    #define DEBUG_UART_RX_PIN   0
    #endif
#elif DEBUG_UART_NUM == 2
    /* LPUART2 defaults: PTD6 (RX), PTD7 (TX) */
    #ifndef DEBUG_UART_TX_PORT
    #define DEBUG_UART_TX_PORT  S32K_PORT_D
    #endif
    #ifndef DEBUG_UART_TX_PIN
    #define DEBUG_UART_TX_PIN   7
    #endif
    #ifndef DEBUG_UART_RX_PORT
    #define DEBUG_UART_RX_PORT  S32K_PORT_D
    #endif
    #ifndef DEBUG_UART_RX_PIN
    #define DEBUG_UART_RX_PIN   6
    #endif
#else /* DEBUG_UART_NUM == 1 (default) */
    /* LPUART1 defaults: PTC6 (RX), PTC7 (TX) - S32K142EVB OpenSDA */
    #ifndef DEBUG_UART_TX_PORT
    #define DEBUG_UART_TX_PORT  S32K_PORT_C
    #endif
    #ifndef DEBUG_UART_TX_PIN
    #define DEBUG_UART_TX_PIN   7
    #endif
    #ifndef DEBUG_UART_RX_PORT
    #define DEBUG_UART_RX_PORT  S32K_PORT_C
    #endif
    #ifndef DEBUG_UART_RX_PIN
    #define DEBUG_UART_RX_PIN   6
    #endif
#endif

/* Default pin mux - ALT2 for most LPUART pins */
#ifndef DEBUG_UART_TX_MUX
#define DEBUG_UART_TX_MUX   PORT_PCR_MUX_ALT2
#endif
#ifndef DEBUG_UART_RX_MUX
#define DEBUG_UART_RX_MUX   PORT_PCR_MUX_ALT2
#endif

/* Map TX port/pin to PCR register and PCC */
#if DEBUG_UART_TX_PORT == S32K_PORT_A
    #define DEBUG_UART_TX_PCC_PORT  PCC_PORTA
    #define DEBUG_UART_TX_PCR       PORTA_PCR(DEBUG_UART_TX_PIN)
#elif DEBUG_UART_TX_PORT == S32K_PORT_B
    #define DEBUG_UART_TX_PCC_PORT  PCC_PORTB
    #define DEBUG_UART_TX_PCR       PORTB_PCR(DEBUG_UART_TX_PIN)
#elif DEBUG_UART_TX_PORT == S32K_PORT_D
    #define DEBUG_UART_TX_PCC_PORT  PCC_PORTD
    #define DEBUG_UART_TX_PCR       PORTD_PCR(DEBUG_UART_TX_PIN)
#elif DEBUG_UART_TX_PORT == S32K_PORT_E
    #define DEBUG_UART_TX_PCC_PORT  PCC_PORTE
    #define DEBUG_UART_TX_PCR       PORTE_PCR(DEBUG_UART_TX_PIN)
#else /* S32K_PORT_C (default) */
    #define DEBUG_UART_TX_PCC_PORT  PCC_PORTC
    #define DEBUG_UART_TX_PCR       PORTC_PCR(DEBUG_UART_TX_PIN)
#endif

/* Map RX port/pin to PCR register and PCC */
#if DEBUG_UART_RX_PORT == S32K_PORT_A
    #define DEBUG_UART_RX_PCC_PORT  PCC_PORTA
    #define DEBUG_UART_RX_PCR       PORTA_PCR(DEBUG_UART_RX_PIN)
#elif DEBUG_UART_RX_PORT == S32K_PORT_B
    #define DEBUG_UART_RX_PCC_PORT  PCC_PORTB
    #define DEBUG_UART_RX_PCR       PORTB_PCR(DEBUG_UART_RX_PIN)
#elif DEBUG_UART_RX_PORT == S32K_PORT_D
    #define DEBUG_UART_RX_PCC_PORT  PCC_PORTD
    #define DEBUG_UART_RX_PCR       PORTD_PCR(DEBUG_UART_RX_PIN)
#elif DEBUG_UART_RX_PORT == S32K_PORT_E
    #define DEBUG_UART_RX_PCC_PORT  PCC_PORTE
    #define DEBUG_UART_RX_PCR       PORTE_PCR(DEBUG_UART_RX_PIN)
#else /* S32K_PORT_C (default) */
    #define DEBUG_UART_RX_PCC_PORT  PCC_PORTC
    #define DEBUG_UART_RX_PCR       PORTC_PCR(DEBUG_UART_RX_PIN)
#endif

/* Check if TX and RX use the same port (for clock enable optimization) */
#if DEBUG_UART_TX_PORT == DEBUG_UART_RX_PORT
    #define DEBUG_UART_SAME_PORT    1
#else
    #define DEBUG_UART_SAME_PORT    0
#endif

/* ============== Flash (FTFC) Registers ============== */

#define FTFC_BASE           (0x40020000UL)
#define FTFC_FSTAT          (*(volatile uint8_t *)(FTFC_BASE + 0x000UL))
#define FTFC_FCNFG          (*(volatile uint8_t *)(FTFC_BASE + 0x001UL))
#define FTFC_FSEC           (*(volatile uint8_t *)(FTFC_BASE + 0x002UL))
#define FTFC_FOPT           (*(volatile uint8_t *)(FTFC_BASE + 0x003UL))
#define FTFC_FCCOB3         (*(volatile uint8_t *)(FTFC_BASE + 0x004UL))
#define FTFC_FCCOB2         (*(volatile uint8_t *)(FTFC_BASE + 0x005UL))
#define FTFC_FCCOB1         (*(volatile uint8_t *)(FTFC_BASE + 0x006UL))
#define FTFC_FCCOB0         (*(volatile uint8_t *)(FTFC_BASE + 0x007UL))
#define FTFC_FCCOB7         (*(volatile uint8_t *)(FTFC_BASE + 0x008UL))
#define FTFC_FCCOB6         (*(volatile uint8_t *)(FTFC_BASE + 0x009UL))
#define FTFC_FCCOB5         (*(volatile uint8_t *)(FTFC_BASE + 0x00AUL))
#define FTFC_FCCOB4         (*(volatile uint8_t *)(FTFC_BASE + 0x00BUL))
#define FTFC_FCCOBB         (*(volatile uint8_t *)(FTFC_BASE + 0x00CUL))
#define FTFC_FCCOBA         (*(volatile uint8_t *)(FTFC_BASE + 0x00DUL))
#define FTFC_FCCOB9         (*(volatile uint8_t *)(FTFC_BASE + 0x00EUL))
#define FTFC_FCCOB8         (*(volatile uint8_t *)(FTFC_BASE + 0x00FUL))

/* FTFC Commands */
#define FTFC_CMD_PROGRAM_PHRASE     0x07  /* Program 8 bytes (phrase) */
#define FTFC_CMD_ERASE_SECTOR       0x09  /* Erase flash sector (2KB) */
#define FTFC_CMD_READ_RESOURCE      0x03  /* Read resource */

/* FTFC_FSTAT bits */
#define FTFC_FSTAT_CCIF             (1U << 7)  /* Command Complete */
#define FTFC_FSTAT_RDCOLERR         (1U << 6)  /* Read Collision Error */
#define FTFC_FSTAT_ACCERR           (1U << 5)  /* Access Error */
#define FTFC_FSTAT_FPVIOL           (1U << 4)  /* Protection Violation */
#define FTFC_FSTAT_MGSTAT0          (1U << 0)  /* Command Failure */

/* Flash programming unit: 8 bytes (double-word / phrase) */
#define FLASH_PHRASE_SIZE           8

/* ============== S32K1xx Variant Flash Sizes ============== */
/* Define the appropriate variant or use automatic detection based on WOLFBOOT config
 *
 * S32K142: 256KB Flash (0x00000 - 0x3FFFF), 32KB SRAM, 2KB sectors
 * S32K144: 512KB Flash (0x00000 - 0x7FFFF), 64KB SRAM, 4KB sectors
 * S32K146: 1MB Flash   (0x00000 - 0xFFFFF), 128KB SRAM, 4KB sectors
 * S32K148: 2MB Flash   (0x00000 - 0x1FFFFF), 256KB SRAM, 4KB sectors
 *
 * IMPORTANT: Flash sector size depends on total flash size:
 *   - 256KB Flash (S32K142): 2KB sectors
 *   - 512KB+ Flash (S32K144/146/148): 4KB sectors
 *
 * All variants use 8-byte phrase programming.
 */
#if defined(S32K148)
    #define FLASH_SIZE              (2048 * 1024)   /* 2MB */
    #define SRAM_SIZE               (256 * 1024)    /* 256KB */
    #define FLASH_SECTOR_SIZE       4096            /* 4KB sectors */
#elif defined(S32K146)
    #define FLASH_SIZE              (1024 * 1024)   /* 1MB */
    #define SRAM_SIZE               (128 * 1024)    /* 128KB */
    #define FLASH_SECTOR_SIZE       4096            /* 4KB sectors */
#elif defined(S32K144)
    #define FLASH_SIZE              (512 * 1024)    /* 512KB */
    #define SRAM_SIZE               (64 * 1024)     /* 64KB */
    #define FLASH_SECTOR_SIZE       4096            /* 4KB sectors */
#else /* S32K142 (default) */
    #define FLASH_SIZE              (256 * 1024)    /* 256KB */
    #define SRAM_SIZE               (32 * 1024)     /* 32KB */
    #define FLASH_SECTOR_SIZE       2048            /* 2KB sectors */
#endif

/* Flash base address */
#define FLASH_BASE_ADDR             0x00000000

/* Flash Configuration Field (FCF) region - MUST NOT be modified at runtime!
 * Writing incorrect values here can permanently lock the device.
 * Address range: 0x400 - 0x40F (16 bytes)
 */
#define FCF_START_ADDR  0x400
#define FCF_END_ADDR    0x410

/* SRAM base address (all S32K1xx variants) */
#define SRAM_BASE_ADDR              0x1FFF8000  /* Lower SRAM */
#define SRAM_UPPER_ADDR             0x20000000  /* Upper SRAM */

/* ============== Watchdog (WDOG) Registers ============== */
/* S32K1xx has a software-controlled watchdog timer */

#define WDOG_BASE               (0x40052000UL)
#define WDOG_CS                 (*(volatile uint32_t *)(WDOG_BASE + 0x00UL)) /* Control and Status */
#define WDOG_CNT                (*(volatile uint32_t *)(WDOG_BASE + 0x04UL)) /* Counter */
#define WDOG_TOVAL              (*(volatile uint32_t *)(WDOG_BASE + 0x08UL)) /* Timeout Value */
#define WDOG_WIN                (*(volatile uint32_t *)(WDOG_BASE + 0x0CUL)) /* Window */

/* WDOG CS Register Bits */
#define WDOG_CS_STOP            (1UL << 0)   /* Stop enable */
#define WDOG_CS_WAIT            (1UL << 1)   /* Wait enable */
#define WDOG_CS_DBG             (1UL << 2)   /* Debug enable */
#define WDOG_CS_TST_SHIFT       3
#define WDOG_CS_TST_MASK        (3UL << WDOG_CS_TST_SHIFT) /* Test mode */
#define WDOG_CS_UPDATE          (1UL << 5)   /* Allow updates */
#define WDOG_CS_INT             (1UL << 6)   /* Interrupt enable */
#define WDOG_CS_EN              (1UL << 7)   /* Watchdog enable */
#define WDOG_CS_CLK_SHIFT       8
#define WDOG_CS_CLK_MASK        (3UL << WDOG_CS_CLK_SHIFT) /* Clock source */
#define WDOG_CS_CLK_BUS         (0UL << WDOG_CS_CLK_SHIFT) /* Bus clock */
#define WDOG_CS_CLK_LPO         (1UL << WDOG_CS_CLK_SHIFT) /* LPO clock (128kHz) */
#define WDOG_CS_CLK_SOSC        (2UL << WDOG_CS_CLK_SHIFT) /* SOSC clock */
#define WDOG_CS_CLK_SIRC        (3UL << WDOG_CS_CLK_SHIFT) /* SIRC clock */
#define WDOG_CS_RCS             (1UL << 10)  /* Reconfiguration success */
#define WDOG_CS_ULK             (1UL << 11)  /* Unlock status */
#define WDOG_CS_PRES            (1UL << 12)  /* Prescaler (256 divider) */
#define WDOG_CS_CMD32EN         (1UL << 13)  /* 32-bit command support */
#define WDOG_CS_FLG             (1UL << 14)  /* Interrupt flag */
#define WDOG_CS_WIN             (1UL << 15)  /* Window mode enable */

/* WDOG Unlock Key - write to CNT register to unlock */
#define WDOG_CNT_UNLOCK         (0xD928C520UL)

/* WDOG Refresh Keys - write in sequence to CNT register to refresh */
#define WDOG_CNT_REFRESH_HI     (0xB480UL)
#define WDOG_CNT_REFRESH_LO     (0xA602UL)
#define WDOG_CNT_REFRESH        (0xB480A602UL) /* For CMD32EN mode */

/* Default WDOG timeout value (max = 0xFFFF) */
#define WDOG_TOVAL_DEFAULT      (0xFFFFUL)

/* Watchdog disable configuration:
 * - EN=0 (disabled), UPDATE=1, CMD32EN=1, CLK=LPO
 */
#define WDOG_CS_DISABLE_CFG     (WDOG_CS_UPDATE | WDOG_CS_CMD32EN | WDOG_CS_CLK_LPO)

/* Watchdog enable configuration:
 * - EN=1, UPDATE=1, CMD32EN=1, CLK=LPO
 * LPO is 128kHz, with PRES=0 (no 256 divider)
 * Timeout = TOVAL / 128kHz = TOVAL * 7.8125us
 * For ~1 second timeout: TOVAL = 128000
 */
#define WDOG_CS_ENABLE_CFG      (WDOG_CS_EN | WDOG_CS_UPDATE | WDOG_CS_CMD32EN | \
                                 WDOG_CS_CLK_LPO)


/* Default 1 second timeout */
#ifndef WATCHDOG_TIMEOUT_MS
#define WATCHDOG_TIMEOUT_MS 1000
#endif

/* ============== UART Function Declarations ============== */
/* These functions are implemented in hal/s32k1xx.c when DEBUG_UART is defined.
 * They use the LPUART instance and pins configured by the DEBUG_UART_* macros.
 */

#ifdef DEBUG_UART
/* Initialize the UART with configured LPUART and pins */
void uart_init(void);

/* Write data to UART (blocking, with automatic LF -> CRLF conversion) */
void uart_write(const char* buf, unsigned int sz);

/* Read a single character from UART (non-blocking)
 * Returns: 1 if character read, 0 if no data available, -1 on error
 */
int uart_read(char* c);

#endif /* DEBUG_UART */

#endif /* S32K1XX_H */

