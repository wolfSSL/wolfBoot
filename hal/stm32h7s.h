/* stm32h7s.h
 *
 * Register definitions for STM32H7S / STM32H7R (STM32H7RS series).
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

#ifndef _STM32H7S_DEF_INCLUDED
#define _STM32H7S_DEF_INCLUDED

#include <stdint.h>

#define DMB() __asm__ volatile ("dmb")
#define ISB() __asm__ volatile ("isb")
#define DSB() __asm__ volatile ("dsb")

/* Peripheral base addresses (RM0477) */
#define APB1PERIPH_BASE         (0x40000000UL)
#define AHB5PERIPH_BASE         (0x52000000UL)
#define AHB4PERIPH_BASE         (0x58020000UL)

/*** RCC ***/
#define RCC_BASE                (AHB4PERIPH_BASE + 0x4400UL)
#define RCC_CR                  (*(volatile uint32_t *)(RCC_BASE + 0x000))
#define RCC_CFGR                (*(volatile uint32_t *)(RCC_BASE + 0x010))
#define RCC_AHB5RSTR            (*(volatile uint32_t *)(RCC_BASE + 0x07C))
#define RCC_AHB5ENR             (*(volatile uint32_t *)(RCC_BASE + 0x134))
#define RCC_AHB4ENR             (*(volatile uint32_t *)(RCC_BASE + 0x140))
#define RCC_APB1ENR1            (*(volatile uint32_t *)(RCC_BASE + 0x148))

#define RCC_CDCFGR              (*(volatile uint32_t *)(RCC_BASE + 0x018))
#define RCC_BMCFGR              (*(volatile uint32_t *)(RCC_BASE + 0x01C))
#define RCC_APBCFGR             (*(volatile uint32_t *)(RCC_BASE + 0x020))
#define RCC_PLLCKSELR           (*(volatile uint32_t *)(RCC_BASE + 0x028))
#define RCC_PLLCFGR             (*(volatile uint32_t *)(RCC_BASE + 0x02C))
#define RCC_PLL1DIVR1           (*(volatile uint32_t *)(RCC_BASE + 0x030))
#define RCC_PLL2DIVR1           (*(volatile uint32_t *)(RCC_BASE + 0x038))
#define RCC_CCIPR1              (*(volatile uint32_t *)(RCC_BASE + 0x04C))
#define RCC_PLL1DIVR2           (*(volatile uint32_t *)(RCC_BASE + 0x0C0))
#define RCC_PLL2DIVR2           (*(volatile uint32_t *)(RCC_BASE + 0x0C4))

#define RCC_CR_HSION            (1 << 0)
#define RCC_CR_HSIRDY           (1 << 2)
#define RCC_CR_PLL1ON           (1 << 24)
#define RCC_CR_PLL1RDY          (1 << 25)
#define RCC_CR_PLL2ON           (1 << 26)
#define RCC_CR_PLL2RDY          (1 << 27)

#define RCC_CFGR_SW_MASK        (0x7 << 0)
#define RCC_CFGR_SW_PLL1        (0x3 << 0)
#define RCC_CFGR_SWS_MASK       (0x7 << 3)
#define RCC_CFGR_SWS_PLL1       (0x3 << 3)

/* PLL source and the two M pre-dividers. DIVM holds the divider value
 * itself, unlike the N/P/Q/R/S/T fields which hold value - 1. */
#define RCC_PLLCKSELR_PLLSRC_HSI (0x0 << 0)
#define RCC_PLLCKSELR_DIVM1(m)  (((m) & 0x3F) << 4)
#define RCC_PLLCKSELR_DIVM2(m)  (((m) & 0x3F) << 12)

#define RCC_PLLCFGR_PLL1VCOSEL  (1 << 1)
#define RCC_PLLCFGR_PLL1RGE(r)  (((r) & 0x3) << 3)
#define RCC_PLLCFGR_PLL1PEN     (1 << 5)
#define RCC_PLLCFGR_PLL1FRACEN  (1 << 0)
#define RCC_PLLCFGR_PLL2VCOSEL  (1 << 12)
#define RCC_PLLCFGR_PLL2RGE(r)  (((r) & 0x3) << 14)
#define RCC_PLLCFGR_PLL2SEN     (1 << 19)
#define RCC_PLLCFGR_PLL2FRACEN  (1 << 11)

/* VCO input range selector: 0 = 1-2 MHz, 1 = 2-4, 2 = 4-8, 3 = 8-16 */
#define RCC_PLL_RGE_1_2MHZ      0
#define RCC_PLL_RGE_2_4MHZ      1
#define RCC_PLL_RGE_4_8MHZ      2
#define RCC_PLL_RGE_8_16MHZ     3

#define RCC_PLLDIVR1_DIVN(n)    ((((n) - 1) & 0x1FF) << 0)
#define RCC_PLLDIVR1_DIVP(p)    ((((p) - 1) & 0x7F) << 9)
#define RCC_PLLDIVR1_DIVQ(q)    ((((q) - 1) & 0x7F) << 16)
#define RCC_PLLDIVR1_DIVR(r)    ((((r) - 1) & 0x7F) << 24)
#define RCC_PLLDIVR2_DIVS(s)    ((((s) - 1) & 0x7) << 0)
#define RCC_PLLDIVR2_DIVT(t)    ((((t) - 1) & 0x7) << 8)

/* Bus prescalers. The encoding is 0 for /1, then 0x8 for /2, 0x9 for /4
 * and so on for the AHB/CPU fields, and 0x4 for /2 on the APB fields. */
#define RCC_CDCFGR_CPRE(d)      (((d) & 0xF) << 0)
#define RCC_BMCFGR_BMPRE(d)     (((d) & 0xF) << 0)
#define RCC_PRE_DIV1            0x0
#define RCC_PRE_DIV2            0x8
#define RCC_APBCFGR_PPRE1(d)    (((d) & 0x7) << 0)
#define RCC_APBCFGR_PPRE2(d)    (((d) & 0x7) << 4)
#define RCC_APBCFGR_PPRE4(d)    (((d) & 0x7) << 8)
#define RCC_APBCFGR_PPRE5(d)    (((d) & 0x7) << 12)
#define RCC_PPRE_DIV1           0x0
#define RCC_PPRE_DIV2           0x4

/* XSPI2 kernel clock select. The reset value is HCLK, which is why the
 * kernel clock has to be retargeted whenever the bus clock moves. */
#define RCC_CCIPR1_XSPI2SEL_MASK (0x3 << 6)
#define RCC_CCIPR1_XSPI2SEL_HCLK (0x0 << 6)
#define RCC_CCIPR1_XSPI2SEL_PLL2S (0x1 << 6)

#define RCC_AHB5ENR_XSPI1EN     (1 << 5)
#define RCC_AHB5ENR_XSPI2EN     (1 << 12)
#define RCC_AHB5ENR_XSPIMEN     (1 << 14)
#define RCC_AHB5RSTR_XSPI2RST   (1 << 12)

#define RCC_AHB4ENR_GPIOAEN     (1 << 0)
#define RCC_AHB4ENR_GPIODEN     (1 << 3)
#define RCC_AHB4ENR_GPIONEN     (1 << 13)

#define RCC_APB1ENR1_USART3EN   (1 << 18)

/*** PWR ***/
/* The PWR block is not clock gated on this family, so there is no
 * enable bit to set before touching it. */
#define PWR_BASE                (AHB4PERIPH_BASE + 0x4800UL)
#define PWR_SR1                 (*(volatile uint32_t *)(PWR_BASE + 0x04))
#define PWR_CSR2                (*(volatile uint32_t *)(PWR_BASE + 0x0C))
#define PWR_CSR4                (*(volatile uint32_t *)(PWR_BASE + 0x14))

#define PWR_SR1_ACTVOSRDY       (1 << 1)

#define PWR_CSR4_VOS            (1 << 0) /* set = range 0, highest freq */
#define PWR_CSR4_VOSRDY         (1 << 1)

/* Core supply topology. These five bits are write-once after a power-on
 * reset; their reset state has both SDEN and LDOEN set, which means "not
 * yet selected" rather than "SMPS and LDO both on". */
#define PWR_CSR2_BYPASS         (1 << 0)
#define PWR_CSR2_LDOEN          (1 << 1)
#define PWR_CSR2_SDEN           (1 << 2)
#define PWR_CSR2_SMPSEXTHP      (1 << 3)
#define PWR_CSR2_SDHILEVEL      (1 << 4)
#define PWR_CSR2_SUPPLY_MASK    (PWR_CSR2_SDHILEVEL | PWR_CSR2_SMPSEXTHP | \
                                 PWR_CSR2_SDEN | PWR_CSR2_LDOEN | \
                                 PWR_CSR2_BYPASS)
#define PWR_CSR2_SUPPLY_UNSET   (PWR_CSR2_SDEN | PWR_CSR2_LDOEN)
/* The NUCLEO-H7S3L8 feeds the core domains from the internal LDO. A board
 * using the SMPS needs this changed. */
#ifndef PWR_SUPPLY_CONFIG
#define PWR_SUPPLY_CONFIG       PWR_CSR2_LDOEN
#endif

/* Bound on PWR status polls. A regulator that never reports ready then
 * leaves the core on HSI instead of hanging the bootloader. */
#ifndef PWR_READY_TRIES
#define PWR_READY_TRIES         (1000000UL)
#endif

/* XSPI I/O supply enables. Both are off after a power-on reset and the
 * core supply must be stable before either is switched on. */
#define PWR_CSR2_EN_XSPIM1      (1 << 14)
#define PWR_CSR2_EN_XSPIM2      (1 << 15)
#ifndef PWR_CSR2_EN_XSPIM
#define PWR_CSR2_EN_XSPIM       PWR_CSR2_EN_XSPIM2
#endif

/*** Clock profile ***
 *
 * Matches ST's reference configuration for this part (the Boot project of
 * Templates_Board for the NUCLEO-H7S3L8):
 *
 *   PLL1: HSI 64 MHz / M=32 -> 2 MHz ref, x N=300 -> 600 MHz VCO, P=1
 *         -> sysclk 600 MHz, CPU prescaler /1
 *   HCLK (AXI/AHB) = sysclk / 2 = 300 MHz
 *   APB1/2/4/5     = HCLK / 2   = 150 MHz
 *   Flash latency 7 with WRHIGHFREQ, and voltage range 0
 *
 *   PLL2: HSI 64 MHz / M=4 -> 16 MHz ref, x N=25 -> 400 MHz VCO, S=2
 *         -> 200 MHz, selected as the XSPI2 kernel clock
 *
 * PLL2 exists because the XSPI2 kernel clock defaults to HCLK. Leaving it
 * there would tie the NOR interface rate to the bus clock, so raising the
 * core would overclock the flash. Driving it from PLL2 keeps the two
 * independent.
 */
#ifndef STM32H7S_HSI_ONLY
#define STM32H7S_HSI_ONLY       0
#endif

#define HSI_HZ                  (64000000UL)

#define PLL1_M                  32
#define PLL1_N                  300
#define PLL1_P                  1
#define PLL1_Q                  2
#define PLL1_R                  2
#define PLL1_S                  2
#define PLL1_T                  2

#define PLL2_M                  4
#define PLL2_N                  25
#define PLL2_P                  2
#define PLL2_Q                  2
#define PLL2_R                  2
#define PLL2_S                  2
#define PLL2_T                  2

#define PLL1_VCO_HZ             ((HSI_HZ / PLL1_M) * PLL1_N)
#define PLL2_VCO_HZ             ((HSI_HZ / PLL2_M) * PLL2_N)

#if (STM32H7S_HSI_ONLY == 1)
    #define SYSCLK_HZ           HSI_HZ
    #define HCLK_HZ             HSI_HZ
    #define PCLK_HZ             HSI_HZ
    /* XSPI2 stays on its reset source, HCLK */
    #define OCTOSPI_KERNEL_HZ   HCLK_HZ
#else
    #define SYSCLK_HZ           (PLL1_VCO_HZ / PLL1_P)
    #define HCLK_HZ             (SYSCLK_HZ / 2)
    #define PCLK_HZ             (HCLK_HZ / 2)
    #define OCTOSPI_KERNEL_HZ   (PLL2_VCO_HZ / PLL2_S)
#endif

/*** FLASH access control ***/
#define FLASH_ACR_LATENCY(l)    (((l) & 0xF) << 0)
#define FLASH_ACR_LATENCY_MASK  (0xF << 0)
#define FLASH_ACR_WRHIGHFREQ(w) (((w) & 0x3) << 4)
/* Seven wait states, which is what ST uses at 600 MHz / range 0 */
#define FLASH_LATENCY_WS        7
#define FLASH_LATENCY_HIGH      (FLASH_ACR_LATENCY(FLASH_LATENCY_WS) | \
                                 FLASH_ACR_WRHIGHFREQ(3))

/*** GPIO ***/
#define GPIOA_BASE              (AHB4PERIPH_BASE + 0x0000UL)
#define GPIOD_BASE              (AHB4PERIPH_BASE + 0x0C00UL)
#define GPION_BASE              (AHB4PERIPH_BASE + 0x3400UL)

/* GPIO_MODE()/OTYPE()/OSPD()/PUPD()/AFL()/AFH() and GPIO_MODE_AF come
 * from hal/spi/spi_drv_stm32.h, included below. */
#define GPIO_SPEED_VERY_HIGH    3

/*** Internal FLASH ***/
/* Single bank, 64 KB, eight 8 KB sectors. Program granularity is a
 * 128-bit (16-byte) quad-word. Writes shorter than a quad-word are
 * completed by reading the remaining bytes back from flash so the write
 * buffer is always full; CR.FW, which would commit a partially filled
 * buffer instead, is deliberately not used. The CR.FW and SR.WBNE bits
 * below are defined to document the register, not because the write
 * path uses them. */
#define FLASHMEM_ADDRESS_SPACE  (0x08000000UL)
#define FLASH_BASE_ADDR         (AHB5PERIPH_BASE + 0x2000UL)

#define FLASH_ACR               (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x00))
#define FLASH_KEYR              (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x04))
#define FLASH_OPTKEYR           (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x08))
#define FLASH_CR                (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x10))
#define FLASH_SR                (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x14))
#define FLASH_IER               (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x18))
#define FLASH_ISR               (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x1C))
#define FLASH_ICR               (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x20))

#define FLASH_KEY1              (0x45670123UL)
#define FLASH_KEY2              (0xCDEF89ABUL)

#define FLASH_CR_LOCK           (1 << 0)
#define FLASH_CR_PG             (1 << 1)
#define FLASH_CR_SER            (1 << 2)
#define FLASH_CR_BER            (1 << 3)
#define FLASH_CR_FW             (1 << 4)
#define FLASH_CR_START          (1 << 5)
#define FLASH_CR_SSN_SHIFT      6
#define FLASH_CR_SSN_MASK       0x7
#define FLASH_CR_PG_OTP         (1 << 16)
#define FLASH_CR_ALL_BANKS      (1 << 24)

#define FLASH_SR_BSY            (1 << 0)
#define FLASH_SR_WBNE           (1 << 1)
#define FLASH_SR_QW             (1 << 2)

#define FLASH_ISR_EOPF          (1 << 16)
#define FLASH_ISR_WRPERRF       (1 << 17)
#define FLASH_ISR_PGSERRF       (1 << 18)
#define FLASH_ISR_STRBERRF      (1 << 19)
#define FLASH_ISR_OBLERRF       (1 << 20)
#define FLASH_ISR_INCERRF       (1 << 21)
#define FLASH_ISR_RDSERRF       (1 << 24)
#define FLASH_ISR_SNECCERRF     (1 << 25)
#define FLASH_ISR_DBECCERRF     (1 << 26)

#define FLASH_ISR_ERRORS        (FLASH_ISR_WRPERRF | FLASH_ISR_PGSERRF | \
                                 FLASH_ISR_STRBERRF | FLASH_ISR_INCERRF | \
                                 FLASH_ISR_SNECCERRF | FLASH_ISR_DBECCERRF)

#define STM32H7S_WORD_SIZE      (16)
#define STM32H7S_SECTOR_SIZE    (0x2000)
#define STM32H7S_SECTOR_COUNT   (8)
#define STM32H7S_FLASH_SIZE     (STM32H7S_SECTOR_SIZE * STM32H7S_SECTOR_COUNT)

/*** XSPI2: external Octo-SPI NOR ***/
/* The register block of the instance the NOR hangs off. XSPI1 is at
 * 0x52005000 with its memory-mapped window at 0x90000000; XSPI2 is at
 * 0x5200A000 with its window at 0x70000000.
 *
 * Only XSPI2 is supported. Overriding these moves the register and window
 * addresses but not the rest: octospi_init() enables and resets XSPI2
 * specifically, and clock_config() selects the XSPI2 kernel clock, so a
 * board wiring the NOR to XSPI1 needs those changed too. */
#ifndef OCTOSPI_BASE
#define OCTOSPI_BASE            (AHB5PERIPH_BASE + 0xA000UL)
#endif
#ifndef OCTOSPI_MEM_BASE
#define OCTOSPI_MEM_BASE        (0x70000000UL)
#endif
#ifndef OCTOSPI_MEM_SIZE
#define OCTOSPI_MEM_SIZE        (0x10000000UL) /* 256 MB mmap window */
#endif

#define XSPIM_BASE              (AHB5PERIPH_BASE + 0xB400UL)
#define XSPIM_CR                (*(volatile uint32_t *)(XSPIM_BASE + 0x00))

#define XSPI1_CR                (*(volatile uint32_t *)(AHB5PERIPH_BASE + \
                                    0x5000UL))
#define XSPI_CR_EN              (1 << 0)

/* Pulls in the shared OCTOSPI register and bit macros. The block is not
 * inside a TARGET_ guard and keys off OCTOSPI_BASE, set above. */
#include "hal/spi/spi_drv_stm32.h"

/* OCTOSPI bits the shared header does not define */
#define OCTOSPI_CR_FMODE_MMAP   OCTOSPI_CR_FMODE(3)
#define OCTOSPI_SR_TEF          (1 << 0)
#define OCTOSPI_FCR_CTEF        (1 << 0)
#define OCTOSPI_FCR_CTCF        (1 << 1)
#define OCTOSPI_FCR_CSMF        (1 << 3)

/* NOR command set. These are the 4-byte-address single-SPI opcodes, which
 * the device accepts from its power-on default mode, so wolfBoot never has
 * to change a persistent device mode. 3-byte addressing would not reach
 * past 16 MB. */
#define WRITE_ENABLE_CMD        0x06U
#define READ_SR_CMD             0x05U
#define READ_ID_CMD             0x9FU
#define FAST_READ_4B_CMD        0x0CU
#define PAGE_PROG_4B_CMD        0x12U
#define SEC_ERASE_4B_CMD        0x21U /* 4 KB subsector erase, 4-byte addr */
#define RESET_ENABLE_CMD        0x66U
#define RESET_MEMORY_CMD        0x99U

/* External NOR status register. Deliberately not FLASH_SR_*: those names
 * belong to the internal flash status register above, and NOR WIP happens
 * to occupy the same bit as the internal FLASH_SR_BSY. */
#define NOR_SR_WIP              (1 << 0) /* write in progress */
#define NOR_SR_WEL              (1 << 1) /* write enable latch */

/* Defaults describe the NUCLEO-H7S3L8, which carries a Macronix
 * MX25UW25645G (256 Mbit / 32 MB) on XSPI2: 256-byte pages and a 4 KB
 * erase subsector. Override for a board with a different device.
 * DEVSIZE programmed into XSPI DCR1 is log2(bytes) - 1. */
#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE         256
#endif
#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE       0x1000
#endif
#ifndef FLASH_DEVICE_SIZE_LOG2
#define FLASH_DEVICE_SIZE_LOG2  25 /* 2^25 = 32 MB */
#endif
#define FLASH_DEVICE_SIZE       (1UL << FLASH_DEVICE_SIZE_LOG2)

/* Interface rate the NOR is driven at. 50 MHz is a conservative
 * single-SPI rate that does not depend on the VDDIO_HSLV / XSPI2_HSLV
 * option bits being programmed; raise it only once they are. */
#ifndef OCTOSPI_MAX_HZ
#define OCTOSPI_MAX_HZ          (50000000UL)
#endif

/* Derived from the kernel clock rather than hardcoded, so the NOR stays
 * inside its rated rate if the clock tree above is retuned. DCR2 holds
 * the divider minus one, which OCTOSPI_DCR2_PRESCALER() applies. */
#ifndef OCTOSPI_PRESCALER
#define OCTOSPI_PRESCALER       ((OCTOSPI_KERNEL_HZ + OCTOSPI_MAX_HZ - 1) \
                                 / OCTOSPI_MAX_HZ)
#endif

/* Bound on status-register polls, so a device that never reports ready
 * fails instead of hanging the bootloader. */
#ifndef OCTOSPI_READY_TRIES
#define OCTOSPI_READY_TRIES     (1000000UL)
#endif

/* Dummy cycles for FAST_READ_4B in single-SPI mode */
#ifndef OCTOSPI_READ_DUMMY
#define OCTOSPI_READ_DUMMY      8
#endif

/* XSPI2 signals on the NUCLEO-H7S3L8 are all GPION AF9 (XSPIM_P2):
 * PN0 DQS, PN1 nCS, PN2..PN5 IO0..IO3, PN6 CLK, PN8..PN11 IO4..IO7. */
#ifndef OCTOSPI_GPIO_BASE
#define OCTOSPI_GPIO_BASE       GPION_BASE
#endif
#ifndef OCTOSPI_GPIO_RCC_EN
#define OCTOSPI_GPIO_RCC_EN     RCC_AHB4ENR_GPIONEN
#endif
#ifndef OCTOSPI_GPIO_PIN_MASK
#define OCTOSPI_GPIO_PIN_MASK   0x0F7FU /* PN0-PN6, PN8-PN11 */
#endif
#ifndef OCTOSPI_GPIO_AF
#define OCTOSPI_GPIO_AF         9
#endif

/*** USART3: ST-LINK VCP on the NUCLEO-H7S3L8 (PD8 TX, PD9 RX) ***/
#ifndef UART_BASE
#define UART_BASE               (APB1PERIPH_BASE + 0x4800UL)
#endif
/* USART3 takes its kernel clock from APB1 at reset, so this has to
 * follow the bus clock the HAL actually configures. */
#ifndef UART_PCLK
#define UART_PCLK               PCLK_HZ
#endif
#ifndef UART_BAUD
#define UART_BAUD               (115200)
#endif

#define UART_CR1                (*(volatile uint32_t *)(UART_BASE + 0x00))
#define UART_CR2                (*(volatile uint32_t *)(UART_BASE + 0x04))
#define UART_CR3                (*(volatile uint32_t *)(UART_BASE + 0x08))
#define UART_BRR                (*(volatile uint32_t *)(UART_BASE + 0x0C))
#define UART_ISR                (*(volatile uint32_t *)(UART_BASE + 0x1C))
#define UART_TDR                (*(volatile uint32_t *)(UART_BASE + 0x28))

#define UART_CR1_UE             (1 << 0)
#define UART_CR1_RE             (1 << 2)
#define UART_CR1_TE             (1 << 3)
#define UART_ISR_TXE            (1 << 7)
#define UART_ISR_TC             (1 << 6)

#define UART_GPIO_BASE          GPIOD_BASE
#define UART_GPIO_RCC_EN        RCC_AHB4ENR_GPIODEN
#define UART_TX_PIN             8
#define UART_RX_PIN             9
#define UART_PIN_AF             7

/*** Cortex-M7 cache maintenance ***/
#define SCB_BASE                (0xE000ED00UL)
#define SCB_CCR                 (*(volatile uint32_t *)(SCB_BASE + 0x14))
#define SCB_CCR_IC              (1 << 17)
#define SCB_CCR_DC              (1 << 16)
#define SCB_ICIALLU             (*(volatile uint32_t *)(0xE000EF50UL))
#define SCB_DCISW               (*(volatile uint32_t *)(0xE000EF60UL))
#define SCB_DCCIMVAC            (*(volatile uint32_t *)(0xE000EF70UL))
#define SCB_DCCISW              (*(volatile uint32_t *)(0xE000EF74UL))
#define CCSIDR                  (*(volatile uint32_t *)(0xE000ED80UL))
#define CSSELR                  (*(volatile uint32_t *)(0xE000ED84UL))

#define CACHE_LINE_SIZE         32

#endif /* _STM32H7S_DEF_INCLUDED */
