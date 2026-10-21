/* psoc6.h
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

#ifndef WOLFBOOT_HAL_PSOC6_H
#define WOLFBOOT_HAL_PSOC6_H

#define PSOC6_CPU_FREQ          100000000UL
#define PSOC6_CLK_PERI_HZ       PSOC6_CPU_FREQ

#define PSOC6_FLASH_BASE        0x10000000UL
#define PSOC6_FLASH_SIZE        0x00200000UL
#define PSOC6_WFLASH_BASE       0x14000000UL
#define PSOC6_WFLASH_SIZE       0x00008000UL
#define PSOC6_FLASH_ROW_SIZE    512U
#define PSOC6_FLASH_BANK2_BASE  0x12000000UL
#define PSOC6_FLASH_BANK_SIZE   (PSOC6_FLASH_SIZE / 2U)

/* CM0+ vector table: 16 system exceptions + 32 interrupts */
#define PSOC6_CM0P_SYS_VECTORS  16U
#define PSOC6_CM0P_VECTORS      48U
#define PSOC6_HARDFAULT_VECTOR  3U

/* The CM4 VTOR needs 1 KB alignment for its 16 + 168 vectors */
#define PSOC6_CM4_VTOR_ALIGN    0x400U

#define PSOC6_UART_BAUD         115200U
#define PSOC6_UART_OVERSAMPLE   16U
/* 16.5-bit fractional divider: (int + 1) + frac / 32 */
#define PSOC6_UART_DIV_X32 \
    ((((PSOC6_CLK_PERI_HZ) * 32UL) + \
      (((PSOC6_UART_BAUD) * (PSOC6_UART_OVERSAMPLE)) / 2UL)) / \
     ((PSOC6_UART_BAUD) * (PSOC6_UART_OVERSAMPLE)))
#define PSOC6_UART_DIV_INT      ((PSOC6_UART_DIV_X32 / 32UL) - 1UL)
#define PSOC6_UART_DIV_FRAC     (PSOC6_UART_DIV_X32 % 32UL)
#define PSOC6_UART_DIV_NUM      0U

#define PSOC6_TIMER_HZ          1000000UL
#define PSOC6_TIMER_DIV         ((PSOC6_CLK_PERI_HZ / PSOC6_TIMER_HZ) - 1UL)
#define PSOC6_TIMER_DIV_NUM     0U
#define PSOC6_TIMER_TCPWM       TCPWM0
#define PSOC6_TIMER_CNT         0U
#define PSOC6_TIMER_PCLK        PCLK_TCPWM0_CLOCKS0

#if defined(PSOC6_BOARD_CY8CEVAL_062S2)
    #define PSOC6_UART_SCB      SCB0
    #define PSOC6_UART_PCLK     PCLK_SCB0_CLOCK
    #define PSOC6_UART_PORT     GPIO_PRT0
    #define PSOC6_UART_RX_PIN   2U
    #define PSOC6_UART_TX_PIN   3U
    #define PSOC6_UART_RX_MUX   P0_2_SCB0_UART_RX
    #define PSOC6_UART_TX_MUX   P0_3_SCB0_UART_TX
    #define PSOC6_LED_PORT      GPIO_PRT11
    #define PSOC6_LED_PIN       1U
#elif defined(PSOC6_BOARD_CY8CKIT_062S2_43012)
    #define PSOC6_UART_SCB      SCB5
    #define PSOC6_UART_PCLK     PCLK_SCB5_CLOCK
    #define PSOC6_UART_PORT     GPIO_PRT5
    #define PSOC6_UART_RX_PIN   0U
    #define PSOC6_UART_TX_PIN   1U
    #define PSOC6_UART_RX_MUX   P5_0_SCB5_UART_RX
    #define PSOC6_UART_TX_MUX   P5_1_SCB5_UART_TX
    #define PSOC6_LED_PORT      GPIO_PRT1
    #define PSOC6_LED_PIN       5U
#else
    #error "Set PSOC6_BOARD to CY8CEVAL_062S2 or CY8CKIT_062S2_43012"
#endif

/* Both kits drive the user LEDs active low */
#define PSOC6_LED_ON            0U
#define PSOC6_LED_OFF           1U

/* Host unit tests point flash reads at a model */
#ifndef PSOC6_FLASH_PTR
#define PSOC6_FLASH_PTR(a) ((const void *)(uintptr_t)(a))
#endif

int psoc6_flash_range_ok(uint32_t address, uint32_t len);

#endif /* WOLFBOOT_HAL_PSOC6_H */
