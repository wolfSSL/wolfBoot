/* stm32h7.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
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

#include <stdint.h>
#include "image.h"
#include "hal.h"

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define ISB() __asm__ volatile ("isb")
#define DSB() __asm__ volatile ("dsb")

/* STM32 H7 register configuration */
/*** RCC ***/
#define RCC_BASE            (0x58024400) /* RM0433 - Table 8 */
#define RCC_CR              (*(volatile uint32_t *)(RCC_BASE + 0x00))  /* RM0433 - 7.7.2 */
#define RCC_PLLCKSELR       (*(volatile uint32_t *)(RCC_BASE + 0x28))  /* RM0433 - 7.7.11 */
#define RCC_PLLCFGR         (*(volatile uint32_t *)(RCC_BASE + 0x2C))  /* RM0433 - 7.7.12 */
#define RCC_PLL1DIVR        (*(volatile uint32_t *)(RCC_BASE + 0x30))  /* RM0433 - 7.7.13 */

#define RCC_CFGR            (*(volatile uint32_t *)(RCC_BASE + 0x10))  /* RM0433 - 7.7.7 */
#define RCC_D1CFGR          (*(volatile uint32_t *)(RCC_BASE + 0x18))  /* RM0433 - 7.7.8 */
#define RCC_D2CFGR          (*(volatile uint32_t *)(RCC_BASE + 0x1C))  /* RM0433 - 7.7.8 */
#define RCC_D3CFGR          (*(volatile uint32_t *)(RCC_BASE + 0x20))  /* RM0433 - 7.7.9 */

#define RCC_D2CCIP2R        (*(volatile uint32_t *)(RCC_BASE + 0x54))  /* RM0433 - 8.7.21 */

#define APB1_CLOCK_LRST     (*(volatile uint32_t *)(RCC_BASE + 0x90))  /* RM0433 - 8.7.33 - RCC_APB1LRSTR */

#define AHB4_CLOCK_ENR      (*(volatile uint32_t *)(RCC_BASE + 0xE0))  /* RM0433 - 8.7.43 */
#define APB1_CLOCK_LER      (*(volatile uint32_t *)(RCC_BASE + 0xE8))  /* RM0433 - 8.7.45 - RCC_APB1LENR */
#define APB1_CLOCK_HER      (*(volatile uint32_t *)(RCC_BASE + 0xEC))  /* RM0433 - 8.7.46 */
#define APB2_CLOCK_ER       (*(volatile uint32_t *)(RCC_BASE + 0xF0))  /* RM0433 - 8.7.47 */
#define AHB3_CLOCK_ER       (*(volatile uint32_t *)(RCC_BASE + 0xD4))  /* RM0433 - 8.7.40 */

#define RCC_CR_PLL1RDY              (1 << 25)
#define RCC_CR_PLL1ON               (1 << 24)
#define RCC_CR_HSEBYP               (1 << 18)
#define RCC_CR_HSERDY               (1 << 17)
#define RCC_CR_HSEON                (1 << 16)
#define RCC_CR_HSIRDY               (1 << 2)
#define RCC_CR_HSION                (1 << 0)

#define RCC_CFGR_SW_HSISYS          0x0
#define RCC_CFGR_SW_PLL             0x3
#define RCC_PLLCFGR_DIVR1EN        (1 << 18)
#define RCC_PLLCFGR_DIVQ1EN        (1 << 17)
#define RCC_PLLCFGR_DIVP1EN        (1 << 16)

#define RCC_PLLCFGR_PLL1VCOSEL     (1 << 1)

#define RCC_PLLCFGR_PLL1RGE_2_4    0x1
#define RCC_PLLCFGR_PLL1RGE_SHIFT  0x2

#define RCC_PLLCKSELR_DIVM1         (1 << 4)
#define RCC_PLLCKSELR_PLLSRC_HSI     0x0
#define RCC_PLLCKSELR_PLLSRC_HSE     0x2

#define RCC_PLLCKSELR_DIVM1_NONE     0x1

/*** GPIO ***/
#define GPIOA_BASE (0x58020000)
#define GPIOB_BASE (0x58020400)
#define GPIOC_BASE (0x58020800)
#define GPIOD_BASE (0x58020C00)
#define GPIOE_BASE (0x58021000)
#define GPIOF_BASE (0x58021400)
#define GPIOG_BASE (0x58021800)

#define GPIO_MODE(base)  (*(volatile uint32_t *)((base) + 0x00))
#define GPIO_OTYPE(base) (*(volatile uint32_t *)((base) + 0x04))
#define GPIO_OSPD(base)  (*(volatile uint32_t *)((base) + 0x08))
#define GPIO_PUPD(base)  (*(volatile uint32_t *)((base) + 0x0c))
#define GPIO_ODR(base)   (*(volatile uint32_t *)((base) + 0x14))
#define GPIO_BSRR(base)  (*(volatile uint32_t *)((base) + 0x18))
#define GPIO_AFRL(base)  (*(volatile uint32_t *)((base) + 0x20))
#define GPIO_AFRH(base)  (*(volatile uint32_t *)((base) + 0x24))


/*** PWR ***/
#define PWR_BASE             (0x58024800) /* RM0433 - Table 8 */
#define PWR_CSR1             (*(volatile uint32_t *)(PWR_BASE + 0x04))  /* RM0433 - 5.8.x */
#define PWR_CSR1_ACTVOSRDY   (1 << 13)
#define PWR_CR3              (*(volatile uint32_t *)(PWR_BASE + 0x0C))  /* RM0433 - 5.8.4 */
#define PWR_CR3_SCUEN        (1 << 2)
#define PWR_CR3_LDOEN        (1 << 1)
#define PWR_D3CR             (*(volatile uint32_t *)(PWR_BASE + 0x18))  /* RM0433 - 5.8.6 */
#define PWR_D3CR_VOSRDY      (1 << 13)
#define PWR_D3CR_VOS_SHIFT   (14)
#define PWR_D3CR_VOS_SCALE_1 (3)

#define SYSCFG_BASE          (0x58000400) /* RM0433 - Table 8 */
#define SYSCFG_PMCR          (*(volatile uint32_t *)(SYSCFG_BASE + 0x04))  /* RM0433 - 5.8.4 */
#define SYSCFG_PWRCR         (*(volatile uint32_t *)(SYSCFG_BASE + 0x04))  /* RM0433 - 5.8.4 */
#define SYSCFG_UR0           (*(volatile uint32_t *)(SYSCFG_BASE + 0x300))  /* RM0433 - 12.3.1.2 */
#define SYSCFG_PWRCR_ODEN    (1 << 0)
#define SYSCFG_UR0_BKS       (1 << 0)   /* bank swap */

/*** APB PRESCALER ***/
#define RCC_PRESCALER_DIV_NONE 0
#define RCC_PRESCALER_DIV_2 8


/*** UART ***/
#ifndef CLOCK_SPEED
#define CLOCK_SPEED 64000000UL /* 120MHz pclk1, 64MHz HSI */
#endif
#ifndef BAUD_RATE
#define BAUD_RATE   115200
#endif
#ifndef UART_PORT
#define UART_PORT   3 /* default to Nucleo VCOM port */
#endif

#if UART_PORT == 3
/* USART3 Base address (connected to ST virtual com port on Nucleo board) */
#define UART_BASE      (0x40004800)
#define UART_GPIO_BASE GPIOD_BASE
#define UART_TX_PIN    8 /* PD8, USART Transmit pin */
#define UART_RX_PIN    9 /* PD9, USART Receive pin */
#define UART_PIN_AF    7 /* AF stands for Alternate Function. USART TX/RX */
#elif UART_PORT == 5
/* USART5 Base address */
#define UART_BASE      (0x40005000)
#define UART_GPIO_BASE GPIOB_BASE
#define UART_TX_PIN    13 /* PB13, USART Transmit pin */
#define UART_RX_PIN    12 /* PB12, USART Receive pin */
#define UART_PIN_AF    14 /* AF stands for Alternate Function. USART TX/RX */
#else
/* USART2 Base address (chosen because of its pin layout on Nucleo board) */
#define UART_BASE      (0x40004400)
#define UART_GPIO_BASE GPIOD_BASE
#define UART_TX_PIN    5 /* PD5, USART Transmit pin */
#define UART_RX_PIN    6 /* PD6, USART Receive pin */
#define UART_PIN_AF    7 /* AF stands for Alternate Function. USART TX/RX */
#endif

/* UART/USART: Defining register start addresses. */
#define UART_CR1(base)    (*(volatile uint32_t *)((base) + 0x00))
#define UART_CR2(base)    (*(volatile uint32_t *)((base) + 0x04))
#define UART_CR3(base)    (*(volatile uint32_t *)((base) + 0x08))
#define UART_BRR(base)    (*(volatile uint32_t *)((base) + 0x0C))
#define UART_RQR(base)    (*(volatile uint32_t *)((base) + 0x18))
#define UART_ISR(base)    (*(volatile uint32_t *)((base) + 0x1C))
#define UART_ICR(base)    (*(volatile uint32_t *)((base) + 0x20))
#define UART_RDR(base)    (*(volatile uint32_t *)((base) + 0x24))
#define UART_TDR(base)    (*(volatile uint32_t *)((base) + 0x28))
#define UART_PRESC(base)  (*(volatile uint32_t *)((base) + 0x2C))

/* UART/USART: Defining register bit placement for CR1 and ISR register for readability. */
#define UART_CR1_UART_ENABLE                (1 << 0)
#define UART_CR1_TX_ENABLE                  (1 << 3)
#define UART_CR1_RX_ENABLE                  (1 << 2)
#define UART_CR1_M1                         (1 << 28)
#define UART_CR1_M0                         (1 << 12)
#define UART_CR1_PARITY_ENABLED             (1 << 10)
#define UART_CR1_PARITY_ODD                 (1 << 9)
#define UART_CR1_FIFOEN                     (1 << 29)
#define UART_CR1_OVER8                      (1 << 15)

#define UART_CR2_STOP_MASK                  (0x3 << 12)
#define UART_CR2_STOP(bits)                 (((bits) & 0x3) << 12)
#define UART_CR2_LINEN                      (1 << 14)
#define UART_CR2_CLKEN                      (1 << 11)

#define UART_CR3_SCEN                       (1 << 5)
#define UART_CR3_HDSEL                      (1 << 3)
#define UART_CR3_IREN                       (1 << 1)

#define UART_ISR_TX_FIFO_NOT_FULL           (1 << 7) /* Transmit Data Empty (TXE) or TX FIFO Not Full (TXFNF) */
#define UART_ISR_RX_FIFO_NOT_EMPTY          (1 << 5)
#define UART_ISR_TRANSMISSION_COMPLETE      (1 << 6)

/* RCC: Defining register bit placement for APB1, APB2, AHB1 and AHB4 register for readability. */
#define RCC_APB1_USART2_EN                  (1 << 17)
#define RCC_APB1_USART3_EN                  (1 << 18)
#define RCC_APB1_UART4_EN                   (1 << 19)
#define RCC_APB1_UART5_EN                   (1 << 20)
#define RCC_APB1_UART7_EN                   (1 << 30)
#define RCC_APB1_UART8_EN                   (1 << 31)
#define RCC_APB2_USART1_EN                  (1 << 4)
#define RCC_APB2_USART6_EN                  (1 << 5)

#define RCC_AHB4_GPIOB_EN                   (1 << 1)
#define RCC_AHB4_GPIOD_EN                   (1 << 3)

/*** QSPI ***/
/* See hal/spi/spi_drv_stm32.c */

/*** FLASH ***/
#define SYSCFG_APB4_CLOCK_ER_VAL    (1 << 0) /* RM0433 - 7.7.48 - RCC_APB4ENR - SYSCFGEN */

#define FLASH_BASE          (0x52002000)   /* RM0433 - Table 8 */
#define FLASH_ACR           (*(volatile uint32_t *)(FLASH_BASE + 0x00)) /* RM0433 - 3.9.1 - FLASH_ACR */
#define FLASH_OPTSR_CUR     (*(volatile uint32_t *)(FLASH_BASE + 0x1C))

/* Bank 1 */
#define FLASH_KEYR1         (*(volatile uint32_t *)(FLASH_BASE + 0x04)) /* RM0433 - 3.9.2 - FLASH_KEYR 1 */
#define FLASH_SR1           (*(volatile uint32_t *)(FLASH_BASE + 0x10)) /* RM0433 - 3.9.5 - FLASH_SR 1 */
#define FLASH_CR1           (*(volatile uint32_t *)(FLASH_BASE + 0x0C)) /* RM0433 - 3.9.4 - FLASH_CR 1 */

/* Bank 2 */
#define FLASH_KEYR2         (*(volatile uint32_t *)(FLASH_BASE + 0x104)) /* RM0433 - 3.9.24 - FLASH_KEYR 2 */
#define FLASH_SR2           (*(volatile uint32_t *)(FLASH_BASE + 0x110)) /* RM0433 - 3.9.26 - FLASH_SR 2 */
#define FLASH_CR2           (*(volatile uint32_t *)(FLASH_BASE + 0x10C)) /* RM0433 - 3.9.25 - FLASH_CR 2 */

/* Flash Configuration */
#define FLASHMEM_ADDRESS_SPACE    (0x08000000UL)
#define FLASH_PAGE_SIZE           (0x20000) /* 128KB */
#define FLASH_BANK2_BASE          (0x08100000UL) /*!< Base address of : (up to 1 MB) Flash Bank2 accessible over AXI */
#define FLASH_BANK2_BASE_REL      (FLASH_BANK2_BASE - FLASHMEM_ADDRESS_SPACE)
#define FLASH_TOP                 (0x081FFFFFUL) /*!< FLASH end address  */

/* Register values */
#define FLASH_ACR_LATENCY_MASK              (0x07)
#define FLASH_SR_BSY                        (1 << 0)
#define FLASH_SR_WBNE                       (1 << 1)
#define FLASH_SR_QW                         (1 << 2)
#define FLASH_SR_WRPERR                     (1 << 17)
#define FLASH_SR_PGSERR                     (1 << 18)
#define FLASH_SR_STRBERR                    (1 << 19)
#define FLASH_SR_INCERR                     (1 << 21)
#define FLASH_SR_OPERR                      (1 << 22)
#define FLASH_SR_RDPERR                     (1 << 23)
#define FLASH_SR_RDSERR                     (1 << 24)
#define FLASH_SR_SNECCERR                   (1 << 25)
#define FLASH_SR_DBECCERR                   (1 << 26)
#define FLASH_SR_EOP                        (1 << 16)

#define FLASH_CR_LOCK                       (1 << 0) /* RM0433 - 3.7.5 - FLASH_CR */
#define FLASH_CR_STRT                       (1 << 7)
#define FLASH_CR_PSIZE                      (1 << 4)
#define FLASH_CR_BER                        (1 << 3)
#define FLASH_CR_SER                        (1 << 2)
#define FLASH_CR_PG                         (1 << 1)
#define FLASH_CR2_SPSS2                     (1 << 14)

#define FLASH_OPTSR_CUR_BSY                 (1 << 0)

#define FLASH_CR_SNB_SHIFT                  8     /* SNB bits 10:8 */
#define FLASH_CR_SNB_MASK                   0x7   /* SNB bits 10:8 - 3 bits */

#define FLASH_KEY1                          (0x45670123)
#define FLASH_KEY2                          (0xCDEF89AB)


/* STM32H7: Due to ECC functionality, it is not possible to write partition/sector
 * flags and signature more than once. This flags_cache is used to intercept write operations and
 * ensures that the sector is always erased before each write.
 */

#define STM32H7_SECTOR_SIZE 0x20000

#if defined(WOLFBOOT_PARTITION_SIZE) && \
    (WOLFBOOT_PARTITION_SIZE < (2 * STM32H7_SECTOR_SIZE))
#   error "Please use a bigger WOLFBOOT_PARTITION_SIZE, since the last 128KB on each partition will be reserved for bootloader flags"
#endif

#define STM32H7_PART_BOOT_END   (WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE)
#define STM32H7_PART_UPDATE_END (WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE)
#define STM32H7_WORD_SIZE (32)
#define STM32H7_PART_BOOT_FLAGS_PAGE_ADDRESS \
    (((STM32H7_PART_BOOT_END - 1)   / STM32H7_SECTOR_SIZE) * STM32H7_SECTOR_SIZE)
#define STM32H7_PART_UPDATE_FLAGS_PAGE_ADDRESS \
    (((STM32H7_PART_UPDATE_END - 1) / STM32H7_SECTOR_SIZE) * STM32H7_SECTOR_SIZE)
#define STM32H7_BOOT_FLAGS_PAGE(x) \
    ((x >= STM32H7_PART_BOOT_FLAGS_PAGE_ADDRESS)   && (x < STM32H7_PART_BOOT_END))
#define STM32H7_UPDATE_FLAGS_PAGE(x) \
    ((x >= STM32H7_PART_UPDATE_FLAGS_PAGE_ADDRESS) && (x < STM32H7_PART_UPDATE_END))

static uint32_t stm32h7_cache[STM32H7_WORD_SIZE / sizeof(uint32_t)];

static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    if ((reg & FLASH_ACR_LATENCY_MASK) != waitstates)
        FLASH_ACR = (reg & ~FLASH_ACR_LATENCY_MASK) | waitstates;
}

static RAMFUNCTION void flash_wait_last(void)
{
    while ((FLASH_OPTSR_CUR & FLASH_OPTSR_CUR_BSY))
        ;
}

static RAMFUNCTION void flash_wait_complete(uint8_t bank)
{
    if (bank == 0) {
        while ((FLASH_SR1 & FLASH_SR_QW) == FLASH_SR_QW);
    }
    else {
        while ((FLASH_SR2 & FLASH_SR_QW) == FLASH_SR_QW);
    }
}

static void RAMFUNCTION flash_clear_errors(uint8_t bank)
{
    if (bank == 0) {
        FLASH_SR1 |= (FLASH_SR_WRPERR | FLASH_SR_PGSERR | FLASH_SR_STRBERR |
                      FLASH_SR_INCERR | FLASH_SR_OPERR | FLASH_SR_RDPERR |
                      FLASH_SR_RDSERR | FLASH_SR_SNECCERR | FLASH_SR_DBECCERR);
    }
    else {
        FLASH_SR2 |= (FLASH_SR_WRPERR | FLASH_SR_PGSERR | FLASH_SR_STRBERR |
                      FLASH_SR_INCERR | FLASH_SR_OPERR | FLASH_SR_RDPERR |
                      FLASH_SR_RDSERR | FLASH_SR_SNECCERR | FLASH_SR_DBECCERR);
    }
}

static void RAMFUNCTION flash_program_on(uint8_t bank)
{
    if (bank == 0) {
        FLASH_CR1 |= FLASH_CR_PG;
        while ((FLASH_CR1 & FLASH_CR_PG) == 0)
            ;
    }
    else {
        FLASH_CR2 |= FLASH_CR_PG;
        while ((FLASH_CR2 & FLASH_CR_PG) == 0)
            ;
    }
}

static void RAMFUNCTION flash_program_off(uint8_t bank)
{
    if (bank == 0) {
        FLASH_CR1 &= ~FLASH_CR_PG;
    }
    else {
        FLASH_CR2 &= ~FLASH_CR_PG;
    }
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0, ii =0;
    uint32_t *src, *dst;
    uint8_t bank=0;
    uint8_t *vbytes = (uint8_t *)(stm32h7_cache);
    int off = (address + i) - (((address + i) >> 5) << 5);
    uint32_t base_addr = (address + i) & (~0x1F); /* aligned to 256 bit */

    if ((address & FLASH_BANK2_BASE_REL) != 0) {
        bank = 1;
    }

    while (i < len) {
        if ((len - i > 32) && ((((address + i) & 0x1F) == 0) &&
            ((((uint32_t)data) + i) & 0x1F) == 0))
        {
            flash_wait_last();
            flash_clear_errors(0);
            flash_clear_errors(1);
            flash_program_on(bank);
            flash_wait_complete(bank);
            src = (uint32_t *)(data + i);
            dst = (uint32_t *)(address + i);
            for (ii = 0; ii < 8; ii++) {
                dst[ii] = src[ii];
            }
            i+=32;
        }
        else {
            int off = (address + i) - (((address + i) >> 5) << 5);
            uint32_t base_addr = (address + i) & (~0x1F); /* aligned to 256 bit */
            dst = (uint32_t *)(base_addr);
            for (ii = 0; ii < 8; ii++) {
                stm32h7_cache[ii] = dst[ii];
            }
            /* Check if flags page */
            if (STM32H7_BOOT_FLAGS_PAGE(address)) {
                if (base_addr != STM32H7_PART_BOOT_END - STM32H7_WORD_SIZE)
                    return -1;
                hal_flash_erase(STM32H7_PART_BOOT_FLAGS_PAGE_ADDRESS,
                    STM32H7_SECTOR_SIZE);
            }
            else if (STM32H7_UPDATE_FLAGS_PAGE(address)) {
                if (base_addr != STM32H7_PART_UPDATE_END - STM32H7_WORD_SIZE)
                    return -1;
                hal_flash_erase(STM32H7_PART_UPDATE_FLAGS_PAGE_ADDRESS,
                    STM32H7_SECTOR_SIZE);
            }
            /* Replace bytes in cache */
            while ((off < STM32H7_WORD_SIZE) && (i < len)) {
                vbytes[off++] = data[i++];
            }

            /* Actual write from cache to FLASH */
            flash_wait_last();
            flash_clear_errors(0);
            flash_clear_errors(1);
            flash_program_on(bank);
            flash_wait_complete(bank);
            ISB();
            DSB();
            for (ii = 0; ii < 8; ii++) {
                dst[ii] = stm32h7_cache[ii];
            }
            ISB();
            DSB();
        }
        flash_wait_complete(bank);
        flash_program_off(bank);
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flash_wait_complete(1);
    if ((FLASH_CR1 & FLASH_CR_LOCK) != 0) {
        FLASH_KEYR1 = FLASH_KEY1;
        DMB();
        FLASH_KEYR1 = FLASH_KEY2;
        DMB();
        while ((FLASH_CR1 & FLASH_CR_LOCK) != 0)
            ;
    }

    flash_wait_complete(2);
    if ((FLASH_CR2 & FLASH_CR_LOCK) != 0) {
        FLASH_KEYR2 = FLASH_KEY1;
        DMB();
        FLASH_KEYR2 = FLASH_KEY2;
        DMB();
        while ((FLASH_CR2 & FLASH_CR_LOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
    flash_wait_complete(1);
    if ((FLASH_CR1 & FLASH_CR_LOCK) == 0)
        FLASH_CR1 |= FLASH_CR_LOCK;

    flash_wait_complete(2);
    if ((FLASH_CR2 & FLASH_CR_LOCK) == 0)
        FLASH_CR2 |= FLASH_CR_LOCK;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end_address;
    uint32_t p;

    if (len == 0)
        return -1;
    end_address = (address - FLASHMEM_ADDRESS_SPACE) + len - 1;
    for (p = (address - FLASHMEM_ADDRESS_SPACE);
         p < end_address;
         p += FLASH_PAGE_SIZE)
    {
        if (p < FLASH_BANK2_BASE_REL) {
            uint32_t reg = FLASH_CR1 &
                (~((FLASH_CR_SNB_MASK << FLASH_CR_SNB_SHIFT) | FLASH_CR_PSIZE));
            FLASH_CR1 = reg |
                (((p >> 17) << FLASH_CR_SNB_SHIFT) | FLASH_CR_SER | 0x00);
            DMB();
            FLASH_CR1 |= FLASH_CR_STRT;
            flash_wait_complete(1);
        }
        if ((p>= FLASH_BANK2_BASE_REL) &&
            (p <= (FLASH_TOP - FLASHMEM_ADDRESS_SPACE))) {
            uint32_t reg = FLASH_CR2 &
                (~((FLASH_CR_SNB_MASK << FLASH_CR_SNB_SHIFT) | FLASH_CR_PSIZE));
            p-= (FLASH_BANK2_BASE);
            FLASH_CR2 = reg |
                (((p >> 17) << FLASH_CR_SNB_SHIFT) | FLASH_CR_SER | 0x00);
            DMB();
            FLASH_CR2 |= FLASH_CR_STRT;
            flash_wait_complete(2);
        }
    }
    return 0;
}

#ifdef DEBUG_UART
static int uart_init(void)
{
    uint32_t reg;

    /* Set general UART clock source (all uarts but nr 1 and 6) */
    /* USART234578SEL bits 2:0 */
    RCC_D2CCIP2R &= ~(0x7 << 0);
    RCC_D2CCIP2R |=  (0x3 << 0); /* 000 = pclk1 (120MHz), 011 = hsi (64MHz) */

#if UART_PORT == 3
    /* Enable clock for USART_3 and reset */
    APB1_CLOCK_LER |= RCC_APB1_USART3_EN;

    APB1_CLOCK_LRST |= RCC_APB1_USART3_EN;
    APB1_CLOCK_LRST &= ~RCC_APB1_USART3_EN;
#elif UART_PORT == 5
    /* Enable clock for USART_5 and reset */
    APB1_CLOCK_LER |= RCC_APB1_UART5_EN;

    APB1_CLOCK_LRST |= RCC_APB1_UART5_EN;
    APB1_CLOCK_LRST &= ~RCC_APB1_UART5_EN;
#else
    /* Enable clock for USART_2 and reset */
    APB1_CLOCK_LER |= RCC_APB1_USART2_EN;

    APB1_CLOCK_LRST |= RCC_APB1_USART2_EN;
    APB1_CLOCK_LRST &= ~RCC_APB1_USART2_EN;
#endif

    /* Enable UART pins */
#if UART_PORT == 5
    AHB4_CLOCK_ENR |= RCC_AHB4_GPIOB_EN;
#else
    AHB4_CLOCK_ENR |= RCC_AHB4_GPIOD_EN;
#endif

    /* Set mode = AF. The PORT D I/O pin is first reset and then set to AF
     * (bit config 10:Alternate function mode) */
    reg = GPIO_MODE(UART_GPIO_BASE) & ~(0x03 << (UART_TX_PIN * 2));
    GPIO_MODE(UART_GPIO_BASE) = reg | (2 << (UART_TX_PIN * 2));
    reg = GPIO_MODE(UART_GPIO_BASE) & ~(0x03 << (UART_RX_PIN * 2));
    GPIO_MODE(UART_GPIO_BASE) = reg | (2 << (UART_RX_PIN * 2));

    /* Alternate function. Use AFLR for pins 0-7 and AFHR for pins 8-15 */
#if UART_TX_PIN < 8
    reg = GPIO_AFRL(UART_GPIO_BASE) & ~(0xf << ((UART_TX_PIN) * 4));
    GPIO_AFRL(UART_GPIO_BASE) = reg | (UART_PIN_AF << ((UART_TX_PIN) * 4));
#else
    reg = GPIO_AFRH(UART_GPIO_BASE) & ~(0xf << ((UART_TX_PIN - 8) * 4));
    GPIO_AFRH(UART_GPIO_BASE) = reg | (UART_PIN_AF << ((UART_TX_PIN - 8) * 4));
#endif
#if UART_RX_PIN < 8
    reg = GPIO_AFRL(UART_GPIO_BASE) & ~(0xf << ((UART_RX_PIN)*4));
    GPIO_AFRL(UART_GPIO_BASE) = reg | (UART_PIN_AF << ((UART_RX_PIN)*4));
#else
    reg = GPIO_AFRH(UART_GPIO_BASE) & ~(0xf << ((UART_RX_PIN - 8) * 4));
    GPIO_AFRH(UART_GPIO_BASE) = reg | (UART_PIN_AF << ((UART_RX_PIN - 8) * 4));
#endif

    /* Disable UART to enable settings to be written into the registers. */
    if (UART_CR1(UART_BASE) & UART_CR1_UART_ENABLE) {
        UART_CR1(UART_BASE) &= ~UART_CR1_UART_ENABLE;
    }

    /* Clock Prescaler */
    UART_PRESC(UART_BASE) = 0; /* no div (div=1) */

    /* Configure clock (speed/bitrate). Requires UE = 0. */
    UART_BRR(UART_BASE) = (uint16_t)(CLOCK_SPEED / BAUD_RATE);

    /* Enable FIFO mode */
    UART_CR1(UART_BASE) |= UART_CR1_FIFOEN;

    /* Enable 16-bit oversampling */
    UART_CR1(UART_BASE) &= ~UART_CR1_OVER8;

    /* Configure the M bits (word length) */
    /* Word length is 8 bits by default (0=1 start, 8 data, 0 stop) */
    UART_CR1(UART_BASE) &= ~(UART_CR1_M0 | UART_CR1_M1);

    /* Configure stop bits (00: 1 stop bit / 10: 2 stop bits.) */
    UART_CR2(UART_BASE) &= ~UART_CR2_STOP_MASK;
    UART_CR2(UART_BASE) |= UART_CR2_STOP(0);

    /* Configure parity bits, disabled */
    UART_CR1(UART_BASE) &= ~(UART_CR1_PARITY_ENABLED | UART_CR1_PARITY_ODD);

    /* In asynchronous mode, the following bits must be kept cleared:
     * - LINEN and CLKEN bits in the UART_CR2 register,
     * - SCEN, HDSEL and IREN  bits in the UART_CR3 register.*/
    UART_CR2(UART_BASE) &= ~(UART_CR2_LINEN | UART_CR2_CLKEN);
    UART_CR3(UART_BASE) &= ~(UART_CR3_SCEN | UART_CR3_HDSEL | UART_CR3_IREN);

    /* Turn on UART */
    UART_CR1(UART_BASE) |= (UART_CR1_TX_ENABLE | UART_CR1_RX_ENABLE |
        UART_CR1_UART_ENABLE);

    return 0;
}

void uart_write(const char* buf, unsigned int sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        while ((UART_ISR(UART_BASE) & UART_ISR_TX_FIFO_NOT_FULL) == 0);

        UART_TDR(UART_BASE) = buf[pos++];
    }
}
#endif /* DEBUG_UART */

static void clock_pll_off(void)
{
    uint32_t reg32;

    /* Select HSI as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 2) |(1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSISYS);
    DMB();
    /* Turn off PLL */
    RCC_CR &= ~RCC_CR_PLL1ON;
    DMB();
}

/* This implementation will setup HSI RC 16 MHz as PLL Source Mux, PLLCLK
 * as System Clock Source */
static void clock_pll_on(int powersave)
{
    uint32_t reg32;
    uint32_t cpu_freq, plln, pllm, pllq, pllp, pllr, hpre, d1cpre, d1ppre;
    uint32_t d2ppre1, d2ppre2, d3ppre, flash_waitstates;

    PWR_CR3 |= PWR_CR3_LDOEN;
    while ((PWR_CSR1 & PWR_CSR1_ACTVOSRDY) == 0) {};

    PWR_D3CR |= (PWR_D3CR_VOS_SCALE_1 << PWR_D3CR_VOS_SHIFT);
    /* Delay after setting the voltage scaling */
    reg32 = PWR_D3CR;
    SYSCFG_PWRCR |= SYSCFG_PWRCR_ODEN;
    /* Delay after setting the voltage scaling */
    reg32 = PWR_D3CR;
    while ((PWR_D3CR & PWR_D3CR_VOSRDY) == 0) {};

    /* Select clock parameters (CPU Speed = 480MHz) */
    pllm = 1;
    plln = 120;
    pllp = 2;
    pllq = 20;
    pllr = 2;
    d1cpre =   RCC_PRESCALER_DIV_NONE;
    hpre  =    RCC_PRESCALER_DIV_2;
    d1ppre =  (RCC_PRESCALER_DIV_2 >> 1);
    d2ppre1 = (RCC_PRESCALER_DIV_2 >> 1);
    d2ppre2 = (RCC_PRESCALER_DIV_2 >> 1);
    d3ppre =  (RCC_PRESCALER_DIV_2 >> 1);
    flash_waitstates = 4;

    flash_set_waitstates(flash_waitstates);

    /* Enable internal high-speed oscillator. */
    RCC_CR |= RCC_CR_HSION;
    DMB();
    while ((RCC_CR & RCC_CR_HSIRDY) == 0) {};

    /* Select HSI as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 2) |(1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSISYS);
    DMB();

    /* Enable external high-speed oscillator. */
    reg32 = RCC_CR;
    reg32 |= RCC_CR_HSEBYP;
    RCC_CR = (reg32 | RCC_CR_HSEON);
    DMB();
    while ((RCC_CR & RCC_CR_HSERDY) == 0) {};

    /*
     * Set prescalers for D1: D1CPRE, D1PPRE, HPRE
     */
    RCC_D1CFGR |= (hpre << 0); /* RM0433 - 7.7.8- RCC_CFGR */
    DMB();

    reg32 = RCC_D1CFGR;
    reg32 &= ~(0xF0); /* don't change bits [0-3] that were previously set */
    RCC_D1CFGR = (reg32 | (d1ppre << 4));  /* RM0433 - 7.7.8- RCC_CFGR */
    DMB();

    reg32 = RCC_D1CFGR;
    reg32 &= ~(0x100); /* don't change bits [0-7] */
    RCC_D1CFGR = (reg32 | (d1cpre << 8));  /* RM0433 - 7.7.8- RCC_CFGR */
    DMB();

    /*
     * Set prescalers for D2: D2PPRE1, D2PPRE2
     */
    reg32 = RCC_D2CFGR;
    reg32 &= ~(0xF0); /* don't change bits [0-3] */
    RCC_D2CFGR = (reg32 | (d2ppre1 << 4));  /* RM0433 - 7.7.8- RCC_CFGR */
    DMB();

    reg32 = RCC_D2CFGR;
    reg32 &= ~(0x100); /* don't change bits [0-7] */
    RCC_D2CFGR = (reg32 | (d2ppre2 << 8));  /* RM0433 - 7.7.8- RCC_CFGR */
    DMB();

    /*
     * Set prescalers for D3: D3PPRE
     */
    reg32 = RCC_D3CFGR;
    RCC_D3CFGR = (reg32 | (d3ppre << 4));  /* RM0433 - 7.7.8- RCC_CFGR */
    DMB();


    /*
     * Set PLL config
     */

    /* PLL Clock source selection + DIVM1 */
    reg32 = RCC_PLLCKSELR;
    reg32 |= RCC_PLLCKSELR_PLLSRC_HSE;
    reg32 |= ((pllm) << 4);
    RCC_PLLCKSELR = reg32;
    DMB();

    reg32 = RCC_PLL1DIVR;
    reg32 |= (plln -1);
    reg32 |= ((pllp - 1) << 9);
    reg32 |= ((pllq - 1) << 16);
    reg32 |= ((pllr - 1) << 24);
    RCC_PLL1DIVR = reg32;
    DMB();

    RCC_PLLCFGR |= (RCC_PLLCFGR_PLL1RGE_2_4 << RCC_PLLCFGR_PLL1RGE_SHIFT);
    RCC_PLLCFGR |= RCC_PLLCFGR_DIVP1EN;
    RCC_PLLCFGR |= RCC_PLLCFGR_DIVQ1EN;
    RCC_PLLCFGR |= RCC_PLLCFGR_DIVR1EN;

    RCC_CR |= RCC_CR_PLL1ON;
    DMB();
    while ((RCC_CR & RCC_CR_PLL1RDY) == 0) {};

    /* Select PLL as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 2) |(1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_PLL);
    DMB();

    /* Wait for PLL clock to be selected. */
    while ((RCC_CFGR & ((1 << 2) | (1 << 1) | (1 << 0))) != RCC_CFGR_SW_PLL) {};
}

void RAMFUNCTION hal_flash_dualbank_swap(void)
{
    hal_flash_unlock();
    DMB();
    ISB();
    if (SYSCFG_UR0 & SYSCFG_UR0_BKS)
        SYSCFG_UR0 &= ~SYSCFG_UR0_BKS;
    else
        SYSCFG_UR0 |= SYSCFG_UR0_BKS;
    DMB();
    hal_flash_lock();
}

void hal_init(void)
{
    clock_pll_on(0);

#ifdef DEBUG_UART
    uart_init();
    uart_write("wolfBoot Init\n", 14);
#endif
}

void hal_prepare_boot(void)
{
#ifdef SPI_FLASH
    spi_flash_release();
#endif
    clock_pll_off();
}
