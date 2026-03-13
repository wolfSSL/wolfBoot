/* stm32n6.h
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

#ifndef STM32N6_DEF_INCLUDED
#define STM32N6_DEF_INCLUDED

/* Assembly helpers */
#ifndef DMB
#define DMB() __asm__ volatile ("dmb")
#endif
#ifndef ISB
#define ISB() __asm__ volatile ("isb")
#endif
#ifndef DSB
#define DSB() __asm__ volatile ("dsb")
#endif

/*** RCC (Reset and Clock Control) — base 0x56028000 (secure) ***/
#define RCC_BASE                (0x56028000UL)

/* RCC_CR: control register (read), CSR: set register, CCR: clear register */
#define RCC_CR                  (*(volatile uint32_t *)(RCC_BASE + 0x000))
#define RCC_CSR                 (*(volatile uint32_t *)(RCC_BASE + 0x800))
#define RCC_CCR                 (*(volatile uint32_t *)(RCC_BASE + 0x1000))
#define RCC_CR_LSION            (1 << 0)
#define RCC_CR_LSEON            (1 << 1)
#define RCC_CR_MSION            (1 << 2)
#define RCC_CR_HSION            (1 << 3)
#define RCC_CR_HSEON            (1 << 4)
#define RCC_CR_PLL1ON           (1 << 8)
#define RCC_CR_PLL2ON           (1 << 9)
#define RCC_CR_PLL3ON           (1 << 10)
#define RCC_CR_PLL4ON           (1 << 11)

/* RCC_SR: status register — ready flags */
#define RCC_SR                  (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_SR_LSIRDY           (1 << 0)
#define RCC_SR_LSERDY           (1 << 1)
#define RCC_SR_MSIRDY           (1 << 2)
#define RCC_SR_HSIRDY           (1 << 3)
#define RCC_SR_HSERDY           (1 << 4)
#define RCC_SR_PLL1RDY          (1 << 8)
#define RCC_SR_PLL2RDY          (1 << 9)
#define RCC_SR_PLL3RDY          (1 << 10)
#define RCC_SR_PLL4RDY          (1 << 11)

/* RCC_CFGR1: clock switching */
#define RCC_CFGR1               (*(volatile uint32_t *)(RCC_BASE + 0x20))
#define RCC_CFGR1_CPUSW_SHIFT   (16)
#define RCC_CFGR1_CPUSW_MASK    (0x3 << 16)
#define RCC_CFGR1_CPUSWS_SHIFT  (20)
#define RCC_CFGR1_CPUSWS_MASK   (0x3 << 20)
#define RCC_CFGR1_SYSSW_SHIFT   (24)
#define RCC_CFGR1_SYSSW_MASK    (0x3 << 24)
#define RCC_CFGR1_SYSSWS_SHIFT  (28)
#define RCC_CFGR1_SYSSWS_MASK   (0x3 << 28)

/* RCC_CFGR2: AHB/APB prescalers */
#define RCC_CFGR2               (*(volatile uint32_t *)(RCC_BASE + 0x24))
#define RCC_CFGR2_PPRE1_SHIFT   (0)
#define RCC_CFGR2_PPRE1_MASK    (0x7 << 0)
#define RCC_CFGR2_PPRE2_SHIFT   (4)
#define RCC_CFGR2_PPRE2_MASK    (0x7 << 4)
#define RCC_CFGR2_PPRE4_SHIFT   (12)
#define RCC_CFGR2_PPRE4_MASK    (0x7 << 12)
#define RCC_CFGR2_PPRE5_SHIFT   (16)
#define RCC_CFGR2_PPRE5_MASK    (0x7 << 16)
#define RCC_CFGR2_HPRE_SHIFT    (20)
#define RCC_CFGR2_HPRE_MASK     (0x7 << 20)

/* PLL1 Configuration registers */
#define RCC_PLL1CFGR1           (*(volatile uint32_t *)(RCC_BASE + 0x80))
#define RCC_PLL1CFGR1_DIVN_SHIFT  (8)   /* bits [19:8]: VCO multiplication */
#define RCC_PLL1CFGR1_DIVN_MASK   (0xFFF << 8)
#define RCC_PLL1CFGR1_DIVM_SHIFT  (20)  /* bits [25:20]: reference divider */
#define RCC_PLL1CFGR1_DIVM_MASK   (0x3F << 20)
#define RCC_PLL1CFGR1_SEL_SHIFT   (28)  /* bits [30:28]: PLL source */
#define RCC_PLL1CFGR1_SEL_MASK    (0x7 << 28)
#define RCC_PLL1CFGR1_BYP         (1 << 27)    /* PLL bypass (ref clock passthrough) */
#define RCC_PLL1CFGR1_SEL_HSI     (0x0 << 28)
#define RCC_PLL1CFGR1_SEL_HSE     (0x1 << 28)
#define RCC_PLL1CFGR1_SEL_MSI     (0x2 << 28)

#define RCC_PLL1CFGR2           (*(volatile uint32_t *)(RCC_BASE + 0x84))
/* PLL1CFGR2: fractional DIVN only (bits [23:0]) */

#define RCC_PLL1CFGR3           (*(volatile uint32_t *)(RCC_BASE + 0x88))
#define RCC_PLL1CFGR3_PDIV2_SHIFT (24)   /* bits [26:24]: output post-divider 2 (1-7) */
#define RCC_PLL1CFGR3_PDIV2_MASK  (0x7 << 24)
#define RCC_PLL1CFGR3_PDIV1_SHIFT (27)   /* bits [29:27]: output post-divider 1 (1-7) */
#define RCC_PLL1CFGR3_PDIV1_MASK  (0x7 << 27)
#define RCC_PLL1CFGR3_MODSSRST   (1 << 0)  /* spread spectrum modulation reset */
#define RCC_PLL1CFGR3_MODSSDIS   (1 << 2)  /* spread spectrum disable */
#define RCC_PLL1CFGR3_PDIVEN     (1 << 30) /* post-divider + PLL output enable */

/* IC (Interconnect Clock) dividers */
#define RCC_IC1CFGR             (*(volatile uint32_t *)(RCC_BASE + 0xC4))
#define RCC_IC2CFGR             (*(volatile uint32_t *)(RCC_BASE + 0xC8))
#define RCC_IC3CFGR             (*(volatile uint32_t *)(RCC_BASE + 0xCC))
#define RCC_IC4CFGR             (*(volatile uint32_t *)(RCC_BASE + 0xD0))
#define RCC_IC5CFGR             (*(volatile uint32_t *)(RCC_BASE + 0xD4))
#define RCC_IC6CFGR             (*(volatile uint32_t *)(RCC_BASE + 0xD8))
#define RCC_IC7CFGR             (*(volatile uint32_t *)(RCC_BASE + 0xDC))
#define RCC_IC8CFGR             (*(volatile uint32_t *)(RCC_BASE + 0xE0))
#define RCC_IC9CFGR             (*(volatile uint32_t *)(RCC_BASE + 0xE4))
#define RCC_IC10CFGR            (*(volatile uint32_t *)(RCC_BASE + 0xE8))
#define RCC_IC11CFGR            (*(volatile uint32_t *)(RCC_BASE + 0xEC))

/* IC divider register fields:
 * ICxINT [23:16] = integer division factor
 * ICxSEL [29:28] = source: 0=PLL1, 1=PLL2, 2=PLL3, 3=PLL4
 */
#define RCC_ICCFGR_INT_SHIFT    (16)
#define RCC_ICCFGR_INT_MASK     (0xFF << 16)
#define RCC_ICCFGR_SEL_SHIFT    (28)
#define RCC_ICCFGR_SEL_MASK     (0x3 << 28)
#define RCC_ICCFGR_SEL_PLL1     (0x0 << 28)
#define RCC_ICCFGR_SEL_PLL2     (0x1 << 28)
#define RCC_ICCFGR_SEL_PLL3     (0x2 << 28)
#define RCC_ICCFGR_SEL_PLL4     (0x3 << 28)

/* Divider enable: direct, set (+0x800), clear (+0x1000) */
#define RCC_DIVENR              (*(volatile uint32_t *)(RCC_BASE + 0x240))
#define RCC_DIVENSR             (*(volatile uint32_t *)(RCC_BASE + 0xA40))
#define RCC_DIVENCR             (*(volatile uint32_t *)(RCC_BASE + 0x1240))
#define RCC_DIVENR_IC1EN        (1 << 0)
#define RCC_DIVENR_IC2EN        (1 << 1)
#define RCC_DIVENR_IC3EN        (1 << 2)
#define RCC_DIVENR_IC4EN        (1 << 3)
#define RCC_DIVENR_IC5EN        (1 << 4)
#define RCC_DIVENR_IC6EN        (1 << 5)
#define RCC_DIVENR_IC11EN       (1 << 10)

/* Clock enable registers */
#define RCC_MISCENR             (*(volatile uint32_t *)(RCC_BASE + 0x248))
#define RCC_AHB1ENR             (*(volatile uint32_t *)(RCC_BASE + 0x250))
#define RCC_AHB2ENR             (*(volatile uint32_t *)(RCC_BASE + 0x254))
#define RCC_AHB3ENR             (*(volatile uint32_t *)(RCC_BASE + 0x258))
#define RCC_AHB4ENR             (*(volatile uint32_t *)(RCC_BASE + 0x25C))
#define RCC_AHB5ENR             (*(volatile uint32_t *)(RCC_BASE + 0x260))
#define RCC_APB1ENR1            (*(volatile uint32_t *)(RCC_BASE + 0x264))
#define RCC_APB1ENR2            (*(volatile uint32_t *)(RCC_BASE + 0x268))
#define RCC_APB2ENR             (*(volatile uint32_t *)(RCC_BASE + 0x26C))
#define RCC_APB4ENR1            (*(volatile uint32_t *)(RCC_BASE + 0x274))
#define RCC_APB5ENR             (*(volatile uint32_t *)(RCC_BASE + 0x27C))

/* GPIO clock enable bits in RCC_AHB4ENR */
#define RCC_AHB4ENR_GPIOAEN    (1 << 0)
#define RCC_AHB4ENR_GPIOBEN    (1 << 1)
#define RCC_AHB4ENR_GPIOCEN    (1 << 2)
#define RCC_AHB4ENR_GPIODEN    (1 << 3)
#define RCC_AHB4ENR_GPIOEEN    (1 << 4)
#define RCC_AHB4ENR_GPIOFEN    (1 << 5)
#define RCC_AHB4ENR_GPIOGEN    (1 << 6)
#define RCC_AHB4ENR_GPIOHEN    (1 << 7)
#define RCC_AHB4ENR_GPIONEN    (1 << 13)
#define RCC_AHB4ENR_GPIOOEN    (1 << 14)
#define RCC_AHB4ENR_GPIOPEN    (1 << 15)
#define RCC_AHB4ENR_GPIOQEN    (1 << 16)
#define RCC_AHB4ENR_PWREN      (1 << 18)

/* XSPI clock enable in RCC_AHB5ENR */
#define RCC_AHB5ENR_XSPI1EN    (1 << 5)
#define RCC_AHB5ENR_XSPI2EN    (1 << 12)
#define RCC_AHB5ENR_XSPIMEN    (1 << 13)

/* XSPI PHY compensation clock in RCC_MISCENR */
#define RCC_MISCENR_XSPIPHYCOMPEN  (1 << 3)

/* USART clock enable */
#define RCC_APB2ENR_USART1EN   (1 << 4)


/*** PWR (Power Control) — base 0x56024800 (secure) ***/
#define PWR_BASE                (0x56024800UL)

#define PWR_CR1                 (*(volatile uint32_t *)(PWR_BASE + 0x00))
#define PWR_CR2                 (*(volatile uint32_t *)(PWR_BASE + 0x04))
#define PWR_CR3                 (*(volatile uint32_t *)(PWR_BASE + 0x08))
#define PWR_CR4                 (*(volatile uint32_t *)(PWR_BASE + 0x0C))
#define PWR_VOSCR               (*(volatile uint32_t *)(PWR_BASE + 0x20))

/* PWR_VOSCR: Voltage scaling controls max CPU frequency.
 * Scale 1 (VOS=0, default): up to 600 MHz CPU.
 * Scale 0 (VOS=1):          up to 800 MHz CPU. */
#define PWR_VOSCR_VOS           (1 << 0)
#define PWR_VOSCR_VOSRDY        (1 << 1)

/* PWR Supply Voltage Monitoring Control Registers */
#define PWR_SVMCR1              (*(volatile uint32_t *)(PWR_BASE + 0x34))
#define PWR_SVMCR2              (*(volatile uint32_t *)(PWR_BASE + 0x38))
#define PWR_SVMCR3              (*(volatile uint32_t *)(PWR_BASE + 0x3C))

/* SVMCR1: VDDIO4 supply valid (bit 8) */
#define PWR_SVMCR1_VDDIO4SV    (1 << 8)
/* SVMCR2: VDDIO5 supply valid (bit 8) */
#define PWR_SVMCR2_VDDIO5SV    (1 << 8)
/* SVMCR3: VDDIO2 supply valid (bit 8), VDDIO3 supply valid (bit 9) */
#define PWR_SVMCR3_VDDIO2SV    (1 << 8)
#define PWR_SVMCR3_VDDIO3SV    (1 << 9)


/*** GPIO ***/
#define GPIOA_BASE              (0x56020000UL)
#define GPIOB_BASE              (0x56020400UL)
#define GPIOC_BASE              (0x56020800UL)
#define GPIOD_BASE              (0x56020C00UL)
#define GPIOE_BASE              (0x56021000UL)
#define GPIOF_BASE              (0x56021400UL)
#define GPIOG_BASE              (0x56021800UL)
#define GPIOH_BASE              (0x56021C00UL)
#define GPION_BASE              (0x56023400UL)
#define GPIOO_BASE              (0x56023800UL)
#define GPIOP_BASE              (0x56023C00UL)
#define GPIOQ_BASE              (0x56024000UL)

/* GPIO register offsets — GPIO_ODR, GPIO_BSRR, GPIO_MODE_* come from
 * spi_drv_stm32.h. Define the rest that aren't in the shared header. */
#define GPIO_MODER(base)        (*(volatile uint32_t *)((base) + 0x00))
#define GPIO_OTYPER(base)       (*(volatile uint32_t *)((base) + 0x04))
#define GPIO_OSPEEDR(base)      (*(volatile uint32_t *)((base) + 0x08))
#define GPIO_PUPDR(base)        (*(volatile uint32_t *)((base) + 0x0C))
#define GPIO_IDR(base)          (*(volatile uint32_t *)((base) + 0x10))
#define GPIO_AFRL(base)         (*(volatile uint32_t *)((base) + 0x20))
#define GPIO_AFRH(base)         (*(volatile uint32_t *)((base) + 0x24))

/* GPIO speed values */
#define GPIO_SPEED_LOW          0x0
#define GPIO_SPEED_MEDIUM       0x1
#define GPIO_SPEED_HIGH         0x2
#define GPIO_SPEED_VERY_HIGH    0x3


/*** OCTOSPI (XSPI2) — register definitions from hal/spi/spi_drv_stm32.h ***/
/* Set base address before including shared OCTOSPI register macros.
 * STM32N6 XSPI2 uses the same IP block as OCTOSPI (identical register layout).
 */
#define OCTOSPI_BASE            (0x5802A000UL)
#define OCTOSPI_MEM_BASE        (0x70000000UL) /* XSPI2 memory-mapped region */

#include "hal/spi/spi_drv_stm32.h"

/* OCTOSPI bits not defined in spi_drv_stm32.h */
#define OCTOSPI_CR_FMODE_MMAP   OCTOSPI_CR_FMODE(3) /* Memory-mapped mode */
#define OCTOSPI_SR_TEF          (1 << 0)  /* Transfer Error Flag */
#define OCTOSPI_FCR_CTEF        (1 << 0)  /* Clear Transfer Error Flag */
#define OCTOSPI_FCR_CTCF        (1 << 1)  /* Clear Transfer Complete Flag */
#define OCTOSPI_FCR_CSMF        (1 << 3)  /* Clear Status Match Flag */
#define OCTOSPI_DCR1_DLYBYP     (1 << 3)  /* Bypass delay block (N6-specific) */

/*** XSPIM (XSPI I/O Manager) ***/
#define XSPIM_BASE              (0x5802B400UL)
#define XSPIM_CR                (*(volatile uint32_t *)(XSPIM_BASE + 0x00))

/*** NOR Flash Commands (Macronix MX25UM51245G) ***/
/* Same commands as src/qspi_flash.c, using 4-byte address variants
 * (0x0C/0x12/0x21) instead of entering 4-byte address mode. */
#define WRITE_ENABLE_CMD        0x06U
#define READ_SR_CMD             0x05U
#define FAST_READ_4B_CMD        0x0CU
#define PAGE_PROG_4B_CMD        0x12U
#define SEC_ERASE_4B_CMD        0x21U   /* 4KB sector erase, 4-byte addr */
#define RESET_ENABLE_CMD        0x66U
#define RESET_MEMORY_CMD        0x99U

/* NOR flash status register bits */
#define FLASH_SR_BUSY           (1 << 0)    /* Write-in-progress */
#define FLASH_SR_WRITE_EN       (1 << 1)    /* Write enable latch */

/* NOR flash geometry */
#define FLASH_PAGE_SIZE         256
#define FLASH_SECTOR_SIZE       0x1000      /* 4KB */
#define FLASH_DEVICE_SIZE       (64 * 1024 * 1024) /* 64MB */
#define FLASH_DEVICE_SIZE_LOG2  26          /* DEVSIZE: 2^26 = 64MB */


/*** USART — parameterized by base address ***/
#define USART1_BASE             (0x52001000UL)

#define UART_CR1(base)          (*(volatile uint32_t *)((base) + 0x00))
#define UART_CR2(base)          (*(volatile uint32_t *)((base) + 0x04))
#define UART_CR3(base)          (*(volatile uint32_t *)((base) + 0x08))
#define UART_BRR(base)          (*(volatile uint32_t *)((base) + 0x0C))
#define UART_ISR(base)          (*(volatile uint32_t *)((base) + 0x1C))
#define UART_ICR(base)          (*(volatile uint32_t *)((base) + 0x20))
#define UART_RDR(base)          (*(volatile uint32_t *)((base) + 0x24))
#define UART_TDR(base)          (*(volatile uint32_t *)((base) + 0x28))

#define USART_CR1_UE            (1 << 0)
#define USART_CR1_RE            (1 << 2)
#define USART_CR1_TE            (1 << 3)
#define USART_CR1_OVER8         (1 << 15)
#define USART_ISR_TXE           (1 << 7)
#define USART_ISR_RXNE          (1 << 5)
#define USART_ISR_TC            (1 << 6)


/*** SCB (System Control Block) — Cortex-M55 cache control ***/
#define SCB_BASE                (0xE000ED00UL)
#define SCB_CCR                 (*(volatile uint32_t *)(SCB_BASE + 0x14))
#define SCB_CCR_IC              (1 << 17)
#define SCB_CCR_DC              (1 << 16)

/* Cache maintenance (Cortex-M55 uses standard ARM CMSIS-like registers) */
#define SCB_ICIALLU             (*(volatile uint32_t *)(0xE000EF50UL))
#define SCB_DCIMVAC             (*(volatile uint32_t *)(0xE000EF5CUL))
#define SCB_DCISW               (*(volatile uint32_t *)(0xE000EF60UL))
#define SCB_DCCMVAU             (*(volatile uint32_t *)(0xE000EF64UL))
#define SCB_DCCMVAC             (*(volatile uint32_t *)(0xE000EF68UL))
#define SCB_DCCSW               (*(volatile uint32_t *)(0xE000EF6CUL))
#define SCB_DCCIMVAC            (*(volatile uint32_t *)(0xE000EF70UL))
#define SCB_DCCISW              (*(volatile uint32_t *)(0xE000EF74UL))

/* Cache size ID registers */
#define CCSIDR                  (*(volatile uint32_t *)(0xE000ED80UL))
#define CSSELR                  (*(volatile uint32_t *)(0xE000ED84UL))

/*** AIRCR (Application Interrupt and Reset Control) ***/
#define AIRCR                   (*(volatile uint32_t *)(0xE000ED0CUL))
#define AIRCR_VKEY              (0x05FA << 16)
#define AIRCR_SYSRESETREQ      (1 << 2)

/*** SysTick ***/
#define SYSTICK_BASE            (0xE000E010UL)
#define SYSTICK_CSR             (*(volatile uint32_t *)(SYSTICK_BASE + 0x00))
#define SYSTICK_RVR             (*(volatile uint32_t *)(SYSTICK_BASE + 0x04))
#define SYSTICK_CVR             (*(volatile uint32_t *)(SYSTICK_BASE + 0x08))


/*** SRAM regions ***/
#define AXISRAM1_BASE           (0x34000000UL)
#define AXISRAM2_BASE           (0x34180400UL)
#define AXISRAM3_BASE           (0x34200000UL)
#define AXISRAM4_BASE           (0x34270000UL)
#define AXISRAM5_BASE           (0x342E0000UL)
#define AXISRAM6_BASE           (0x34350000UL)


#endif /* STM32N6_DEF_INCLUDED */
