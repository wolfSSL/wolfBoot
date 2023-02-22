/* stm32h7.c
 *
 * Test bare-metal application.
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

/** --------------------------------------------------------------------------------------------------------------------
 *  WOLFBOOT APPLICATION FOR NUCLEO-H753ZI BOARD ONLY!                                                                 |
 *                                                                                                                     |
 *  The following application runs on the above mentioned board.                                                       |
 *  It contains setup of LD1, LD2 and LD3.                                                                             |
 *  USART serial communication defaults to using USART3 on pins PD8 (TX) and PD9 (RX) (VCOM Port).                     |
 *    To use USART2 set UART_PORT=2 to use pins PD5 (TX) and PD6 (RX).                                                 |
 *  --------------------------------------------------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "system.h"
#include "hal.h"
#include "wolfboot/wolfboot.h"

#define SET_BIT(REG, BIT)   ((REG) |= (BIT))
#define CLEAR_BIT(REG, BIT) ((REG) &= ~(BIT))
#define READ_BIT(REG, BIT)  ((REG) & (BIT))
#define CLEAR_REG(REG)      ((REG) = (0x00))
#define WRITE_REG(REG, VAL) ((REG) = (VAL))
#define READ_REG(REG)       ((REG))
#define UNUSED(x)           ((void)(x))

/* ASSEMBLY HELPERS */
/* ====================================================================== */
/* It ensures that all explicit memory accesses are observed. */
#ifndef DMB
#define DMB() __asm__ volatile("dmb") /* DMB: Data Memory Barrier */
#endif

#ifndef ISB
#define ISB() __asm__ volatile("isb") /* ISB: Instruction Synchronization Barrier */
#endif

#ifndef DSB
#define DSB() __asm__ volatile("dsb") /* DSB: Data Synchronization Barrier */
#endif

/* GENERAL DEFINITIONS */
/* ====================================================================== */
#define FIRMWARE_A  1
#define FIRMWARE_B  0

#define LED_OFF     0
#define LED_INIT    1
#define LED_ON      2

/* USER LED */
/* ====================================================================== */
/* Defining LED pin numbers in the corresponding GPIO group */
#define LD1_PIN     (0)  /* Nucleo LD1 - Green Led */
#define LD2_PIN     (4)  /* Nucleo LD2 - Yellow Led */
#define LD3_PIN     (14) /* Nucleo LD3 - Red Led */

/* GPIO GROUP B */
#define GPIOB_BASE  0x58020400
#define GPIOB_MODE  (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_PUPD  (*(volatile uint32_t *)(GPIOB_BASE + 0x0c))
#define GPIOB_BSRR  (*(volatile uint32_t *)(GPIOB_BASE + 0x18))
#define GPIOB_AFL   (*(volatile uint32_t *)(GPIOB_BASE + 0x20))
#define GPIOB_AFH   (*(volatile uint32_t *)(GPIOB_BASE + 0x24))
#define GPIOB_AHB4_CLOCK_ER (1 << 1)

#define GPIOD_BASE  (0x58020C00)

/* GPIO GROUP E */
#define GPIOE_BASE  0x58021000
#define GPIOE_MODE  (*(volatile uint32_t *)(GPIOE_BASE + 0x00))
#define GPIOE_PUPD  (*(volatile uint32_t *)(GPIOE_BASE + 0x0c))
#define GPIOE_BSRR  (*(volatile uint32_t *)(GPIOE_BASE + 0x18))
#define GPIOE_AFL   (*(volatile uint32_t *)(GPIOE_BASE + 0x20))
#define GPIOE_AFH   (*(volatile uint32_t *)(GPIOE_BASE + 0x24))
#define GPIOE_AHB4_CLOCK_ER (1 << 4)

/* UART SETUP */
/* ====================================================================== */
#ifndef UART_PORT
#define UART_PORT 3 /* default to Nucleo VCOM port */
#endif

#if UART_PORT == 3
/* USART3 Base address (connected to ST virtual com port on Nucleo board) */
#define UART_BASE   (0x40004800)
#define UART_TX_PIN 8 /* PD8, USART Transmit pin */
#define UART_RX_PIN 9 /* PD9, USART Receive pin */
#else
/* USART2 Base address (chosen because of its pin layout on Nucleo board) */
#define UART_BASE   (0x40004400)
#define UART_TX_PIN 5 /* PD5, USART Transmit pin */
#define UART_RX_PIN 6 /* PD6, USART Receive pin */
#endif

#define UART_PIN_AF 7 /* AF stands for Alternate Function. USART TX/RX */

/* UART/USART: Defining register start addresses. */
#define UART_CR1    (*(volatile uint32_t *)(UART_BASE + 0x00))
#define UART_CR2    (*(volatile uint32_t *)(UART_BASE + 0x04))
#define UART_BRR    (*(volatile uint32_t *)(UART_BASE + 0x0C))
#define UART_ISR    (*(volatile uint32_t *)(UART_BASE + 0x1C))
#define UART_RDR    (*(volatile uint32_t *)(UART_BASE + 0x24))
#define UART_TDR    (*(volatile uint32_t *)(UART_BASE + 0x28))
#define UART_RQR    (*(volatile uint32_t *)(UART_BASE + 0x18))

/* RCC: Defining register start addresses. */
#define RCC_BASE    (0x58024400)
#define RCC_D2CCIP2R (*(volatile uint32_t *)(RCC_BASE + 0x54))
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0xD8))
#define RCC_AHB4ENR (*(volatile uint32_t *)(RCC_BASE + 0xE0))
#define RCC_APB1ENR (*(volatile uint32_t *)(RCC_BASE + 0xE8))
#define RCC_APB2ENR (*(volatile uint32_t *)(RCC_BASE + 0xF0))

/* GPIO: Defining register start addresses. */
#define GPIOD_MODE  (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_BSRR  (*(volatile uint32_t *)(GPIOD_BASE + 0x18))
#define GPIOD_AFRL  (*(volatile uint32_t *)(GPIOD_BASE + 0x20))
#define GPIOD_AFRH  (*(volatile uint32_t *)(GPIOD_BASE + 0x24))

/* UART/USART: Defining register bit placement for CR1 and ISR register for readabilty. */
#define UART_CR1_UART_ENABLE                (1 << 0)
#define UART_CR1_TX_ENABLE                  (1 << 3)
#define UART_CR1_RX_ENABLE                  (1 << 2)
#define UART_CR1_SYMBOL_LEN                 (1 << 28)
#define UART_CR1_PARITY_ENABLED             (1 << 10)
#define UART_CR1_PARITY_ODD                 (1 << 9)
#define UART_ISR_TX_FIFO_NOT_FULL           (1 << 7)
#define UART_ISR_RX_FIFO_NOT_EMPTY          (1 << 5)
#define UART_ISR_TRANSMISSION_COMPLETE      (1 << 6)
#define UART_ISR_TX_DATA_REGISTER_EMPTY     (1 << 7)

/* RCC: Defining register bit placement for APB1, APB2, AHB1 and AHB4 register for readabilty. */
#define RCC_APB1_USART2_EN                  (1 << 17)
#define RCC_APB1_USART3_EN                  (1 << 18)
#define RCC_APB1_UART4_EN                   (1 << 19)
#define RCC_APB1_UART5_EN                   (1 << 20)
#define RCC_APB1_UART7_EN                   (1 << 30)
#define RCC_APB1_UART8_EN                   (1 << 31)
#define RCC_APB2_USART1_EN                  (1 << 4)
#define RCC_APB2_USART6_EN                  (1 << 5)
#define RCC_AHB1_DMA1_EN                    (1 << 0)
#define RCC_AHB1_DMA2_EN                    (1 << 1)
#define RCC_AHB4_GPIOD_EN                   (1 << 3)

/* HSI Clock speed */
#ifndef CLOCK_SPEED
#define CLOCK_SPEED 64000000
#endif

/* Marking the update partition as ready to be swapped and executed. */
#define UPDATE_PARTITION_BASE (0x08060000)
/* The four character is expected to write: W O L F (0x57 0x4F 0x4C 0x46) */
/* Character = W. Hex = 57. BIN = 0101 0111, DEC = 87 */
#define UPDATE_CHARACTER_1  (*(volatile uint32_t *)(UPDATE_PARTITION_BASE + 0x00))
/* Character = O. Hex = 4F. BIN = 0100 1111 */
#define UPDATE_CHARACTER_2  (*(volatile uint32_t *)(UPDATE_PARTITION_BASE + 0x01))
/* Character = L. Hex = 4C. BIN = 0100 1100 */
#define UPDATE_CHARACTER_3  (*(volatile uint32_t *)(UPDATE_PARTITION_BASE + 0x02))
/* Character = F. Hex = 46. BIN = 0100 0110 */
#define UPDATE_CHARACTER_4  (*(volatile uint32_t *)(UPDATE_PARTITION_BASE + 0x03))

/* Marking the update partition as ready to be swapped and executed. */
#define UPDATE_PARTITION_MAGIC_BASE (0x0809FFFC)
#define UPDATE_MAGIC_1      (*(volatile uint32_t *)(UPDATE_PARTITION_MAGIC_BASE + 0x00))
#define UPDATE_MAGIC_2      (*(volatile uint32_t *)(UPDATE_PARTITION_MAGIC_BASE + 0x01))
#define UPDATE_MAGIC_3      (*(volatile uint32_t *)(UPDATE_PARTITION_MAGIC_BASE + 0x02))
#define UPDATE_MAGIC_4      (*(volatile uint32_t *)(UPDATE_PARTITION_MAGIC_BASE + 0x03))

static void ld1_write(uint8_t led_status)
{
    if (led_status == 0) {
        SET_BIT(GPIOB_BSRR, (1 << (LD1_PIN + 16)));
    }
    else if (led_status == 2) {
        SET_BIT(GPIOB_BSRR, (1 << LD1_PIN));
    }
    else if (led_status == 1) {
        uint32_t reg;
        uint32_t pin = LD1_PIN;
        SET_BIT(RCC_AHB4ENR, GPIOB_AHB4_CLOCK_ER);
        reg = GPIOB_MODE & ~(0x03 << (pin * 2));
        GPIOB_MODE = reg | (1 << (pin * 2));
        reg = GPIOB_PUPD & ~(0x03 << (pin * 2));
        GPIOB_PUPD = reg | (1 << (pin * 2));
        GPIOB_BSRR |= (1 << pin);
    }
}

static void ld2_write(uint8_t led_status)
{
    if (led_status == 0) {
        GPIOE_BSRR |= (1 << (LD2_PIN + 16));
    }
    else if (led_status == 2) {
        SET_BIT(GPIOE_BSRR, (1 << LD2_PIN));
    }
    else if (led_status == 1) {
        uint32_t reg;
        uint32_t pin = LD2_PIN;
        RCC_AHB4ENR |= GPIOE_AHB4_CLOCK_ER;
        reg = GPIOE_MODE & ~(0x03 << (pin * 2));
        GPIOE_MODE = reg | (1 << (pin * 2));
        reg = GPIOE_PUPD & ~(0x03 << (pin * 2));
        GPIOE_PUPD = reg | (1 << (pin * 2));
        GPIOE_BSRR |= (1 << pin);
    }
}

static void ld3_write(uint8_t led_status)
{
    if (led_status == 0) {
        GPIOB_BSRR |= (1 << (LD3_PIN + 16));
    }
    else if (led_status == 2) {
        SET_BIT(GPIOB_BSRR, (1 << LD3_PIN));
    }
    else if (led_status == 1) {
        uint32_t reg;
        uint32_t pin = LD3_PIN;
        RCC_AHB4ENR |= GPIOB_AHB4_CLOCK_ER;
        reg = GPIOB_MODE & ~(0x03 << (pin * 2));
        GPIOB_MODE = reg | (1 << (pin * 2));
        reg = GPIOB_PUPD & ~(0x03 << (pin * 2));
        GPIOB_PUPD = reg | (1 << (pin * 2));
        GPIOB_BSRR |= (1 << pin);
    }
}

int uart_setup(uint32_t bitrate)
{
    uint32_t reg;

    if ((bitrate != 9600) && (bitrate != 115200))
        return -1; /* Bitrate not accepted */

    /* Enable UART pins */
    SET_BIT(RCC_AHB4ENR, RCC_AHB4_GPIOD_EN);

    /* Set mode = AF. The PORT D I/O pin is first reset and then set to AF
     * (bit config 10:Alternate function mode) */
    reg = GPIOD_MODE & ~(0x03 << (UART_TX_PIN * 2));
    GPIOD_MODE = reg | (2 << (UART_TX_PIN * 2));
    reg = GPIOD_MODE & ~(0x03 << (UART_RX_PIN * 2));
    GPIOD_MODE = reg | (2 << (UART_RX_PIN * 2));

    /* Alternate function. Use AFLR for pins 0-7 and AFHR for pins 8-15 */
#if UART_TX_PIN >= 8
    reg = GPIOD_AFRH & ~(0xf << ((UART_TX_PIN & 0x07)*4));
    GPIOD_AFRH = reg | (UART_PIN_AF << ((UART_TX_PIN & 0x07)*4));
#else
    reg = GPIOD_AFRL & ~(0xf << ((UART_TX_PIN)*4));
    GPIOD_AFRL = reg | (UART_PIN_AF << ((UART_TX_PIN)*4));
#endif
#if UART_RX_PIN >= 8
    reg = GPIOD_AFRH & ~(0xf << ((UART_RX_PIN & 0x07)*4));
    GPIOD_AFRH = reg | (UART_PIN_AF << ((UART_RX_PIN & 0x07)*4));
#else
    reg = GPIOD_AFRL & ~(0xf << ((UART_RX_PIN)*4));
    GPIOD_AFRL = reg | (UART_PIN_AF << ((UART_RX_PIN)*4));
#endif

    /* Disable UART to enable settings to be written into the registers. */
    if (READ_BIT(UART_CR1, UART_CR1_UART_ENABLE) == 1) {
        CLEAR_BIT(UART_CR1, UART_CR1_UART_ENABLE);
    }

    /* Set general UART clock source (all uarts but nr 1 and 6).
     * 011 = HSI Clock Source */
    SET_BIT(RCC_D2CCIP2R, (1 << 0));
    SET_BIT(RCC_D2CCIP2R, (1 << 1));
    CLEAR_BIT(RCC_D2CCIP2R, (1 << 2));

#if UART_PORT == 3
    /* Enable clock for USART_3 */
    SET_BIT(RCC_APB1ENR, RCC_APB1_USART3_EN);
#else
    /* Enable clock for USART_2 */
    SET_BIT(RCC_APB1ENR, RCC_APB1_USART2_EN);
#endif

    /* Enable FIFO mode */
    SET_BIT(UART_CR1, (1 << 29));

    /* Configure the M bits (word length) */
    CLEAR_BIT(UART_CR1, (1 << 28)); /* Word length is 8 bits by default */
    CLEAR_BIT(UART_CR1, (1 << 12)); /* Word length is 8 bits by default */

    /* Configure clock (speed/bitrate). Requires UE = 0. */
    WRITE_REG(UART_BRR, (CLOCK_SPEED / bitrate));

    /* Configure stop bits (00: 1 stop bit / 10: 2 stop bits.) */
    CLEAR_BIT(UART_CR2, (1 << 12));
    CLEAR_BIT(UART_CR2, (1 << 13));

    /* Set the TE bit in USART_CR1 to send an idle frame as first transmission */
    SET_BIT(UART_CR1, UART_CR1_TX_ENABLE);
    SET_BIT(UART_CR1, UART_CR1_RX_ENABLE);

    /* Configure parity bits, disabled */
    CLEAR_BIT(UART_CR1, UART_CR1_PARITY_ENABLED);
    CLEAR_BIT(UART_CR1, UART_CR1_PARITY_ODD);

    ISB();
    DSB();

    /* Turn on UART. USART_CR1 Register, Bit 0. */
    SET_BIT(UART_CR1, UART_CR1_UART_ENABLE);

    if (READ_BIT(UART_CR1, UART_CR1_UART_ENABLE) == 1)
        return 0;
    else
        return -1;
}

static void uart_write(const char c)
{
    /* USART transmit data register(TDR), bit 0-8 contains the data character
     * to be transmitted.
     * The register mus be written only when TXE/TXFNF = 1;
     * TXE   :  Set by hardware when the content of the USART_TDR register has
     *          been transferred into the shift register.
     * TXFNF :  Set by hardware when TXFIFO is not full meaning that data can
     *          be written in the USART_TDR.
     */
    while (READ_BIT(UART_CR1, UART_CR1_TX_ENABLE) == 0)
        ;
    while (READ_BIT(UART_ISR, UART_ISR_TX_FIFO_NOT_FULL) == 0)
        ;

    UART_TDR = c;
}

void uart_print(const char *s)
{
    int i = 0;
    while (s[i]) {
        uart_write(s[i++]);
    }
}

void main(void)
{
    uint8_t firmware_version = 0;
    char char_temp;

    hal_init();

    /* LED Indicator of Firmware Type A/B. A = ON, B = OFF */
    if (FIRMWARE_A)
        ld3_write(LED_INIT);

    /* LED Indicator of successful UART initialization. SUCCESS = ON, FAIL = OFF */
    if (uart_setup(115200) < 0)
        ld2_write(LED_OFF);
    else
        ld2_write(LED_INIT);

    /* The same as: wolfBoot_get_image_version(PART_BOOT); */
    firmware_version = wolfBoot_current_firmware_version();

    /* LED Indicator of version number lower than 1. */
    if (firmware_version <= 0)
        ld1_write(LED_OFF);
    else
        ld1_write(LED_INIT);

    uart_print(" \n\r");
    uart_print("| ------------------------------------------------------------------- |\n\r");
    uart_print("| STM32H753 User Application in BOOT partition started by wolfBoot    |\n\r");
    uart_print("| ------------------------------------------------------------------- |\n\n\r");

    if (FIRMWARE_A)
        uart_print("\tUSER APPLICATION: A\n\n\r");
    if (FIRMWARE_B)
        uart_print("\tUSER APPLICATION: B\n\n\r");

    uart_print("\tFIRMWARE VERSION: ");
    if (firmware_version <= 9) {
        uart_write(firmware_version + '0');
        uart_print(" \n\n\r");
    }
    else {
        uart_print("Version higher than 9, extend print method!\n\n\r");
    }

    if ((firmware_version > 1) && FIRMWARE_B) {
        uart_print("[INFO] Executing API function call wolfBoot_success()\n\r");
        wolfBoot_success();
        uart_print("[INFO] BOOT partition marked with: IMG_STATE_SUCCESS\n\r");
    }

    char_temp = READ_REG(UPDATE_CHARACTER_1);
    uart_print("[DATA] Content of 0x08060000 (1 byte): ");
    uart_write(char_temp);
    uart_print("\n\r");

    if ((char_temp == 'W') && FIRMWARE_A) {
        uart_print("[INFO] Executing API function call wolfBoot_update_trigger()\n\r");
        wolfBoot_update_trigger();
    }
    else if (FIRMWARE_B) {
        uart_print("[INFO] User application B is running and update cannot be triggered\n\r");
    }

    /* busy wait */
    while (1)
        ;
}
