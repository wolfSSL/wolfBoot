/* board.h
 *
 * STM32WBA55CG Nucleo wolfHAL board header. Provides the WHAL_CFG_*_DEV
 * initializer macros that the upstream chip drivers use to instantiate the
 * device singletons (in their own .c file), plus the BOARD_X_DEV handles
 * that the wolfBoot adapter (hal/wolfhal.c) and board.c pass into the
 * wolfHAL API.
 *
 * The STM32WBA GPIO/UART peripherals are register-compatible with the
 * STM32WB; the upstream stm32wba_{gpio,uart}.c TUs simply include the
 * stm32wb implementation. The alias headers bridge the names: the GPIO
 * alias maps WHAL_CFG_STM32WB_GPIO_DEV onto the WBA-named macro below,
 * while the UART driver consumes WHAL_CFG_STM32WB_UART_DEV directly (the
 * UART alias header bridges only the singleton symbol). Flash is a native
 * STM32WBA driver and uses the WBA-named macro.
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

#ifndef WOLFHAL_BOARD_H
#define WOLFHAL_BOARD_H

#include <wolfHAL/wolfHAL.h>
#include <wolfHAL/platform/st/stm32wba55cg.h>
#include <wolfHAL/clock/stm32wba_rcc.h>
#include <wolfHAL/flash/stm32wba_flash.h>
#include <wolfHAL/gpio/stm32wba_gpio.h>
#include <wolfHAL/uart/stm32wba_uart.h>

/* GPIO pin indices used by the runtime-addressable pinCfg table. */
enum {
    BOARD_LED_PIN,
    BOARD_UART_TX_PIN,
    BOARD_UART_RX_PIN,
    BOARD_PIN_COUNT,
};

/* Singletons owned by the upstream driver .c files. Declared here so a
 * handle could take their address in the future; the SINGLE_INSTANCE /
 * DIRECT_API_MAPPING drivers read their static singleton directly and
 * ignore the passed dev pointer. (whal_Stm32wba_{Gpio,Uart}_Dev are
 * macro-aliased onto the whal_Stm32wb_* symbols by the alias headers.) */
extern const whal_Flash whal_Stm32wba_Flash_Dev;
extern const whal_Gpio  whal_Stm32wba_Gpio_Dev;
extern const whal_Uart  whal_Stm32wba_Uart_Dev;

/* Device handles passed into the wolfHAL API. The direct-mapped /
 * single-instance drivers ignore the dev pointer and read from their
 * static singleton, so the INTERNAL_DEV sentinel is sufficient. */
#define BOARD_GPIO_DEV   WHAL_INTERNAL_DEV
#define BOARD_UART_DEV   WHAL_INTERNAL_DEV
#define BOARD_FLASH_DEV  WHAL_INTERNAL_DEV

/* Flash singleton initializer — instantiated by stm32wba_flash.c.
 * STM32WBA55: 1 MB flash at 0x08000000, 8 KB pages, 128-bit writes. */
#define WHAL_CFG_STM32WBA_FLASH_DEV { \
    .base = WHAL_STM32WBA55_FLASH_BASE, \
    .cfg = (void *)&(const whal_Stm32wba_Flash_Cfg){ \
        .startAddr = 0x08000000, \
        .size      = 0x100000, \
    }, \
}

/* GPIO singleton initializer — consumed by the stm32wb gpio driver via
 * the WHAL_CFG_STM32WB_GPIO_DEV alias in stm32wba_gpio.h. */
#define WHAL_CFG_STM32WBA_GPIO_DEV { \
    .base = WHAL_STM32WBA55_GPIO_BASE, \
    .cfg = (void *)&(const whal_Stm32wba_Gpio_Cfg){ \
        .pinCfg = (const whal_Stm32wba_Gpio_PinCfg[BOARD_PIN_COUNT]){ \
            /* LED: PA9 (LD2, green), output push-pull, low speed, pull-up */ \
            [BOARD_LED_PIN] = WHAL_STM32WBA_GPIO_PIN( \
                WHAL_STM32WBA_GPIO_PORT_A, 9, WHAL_STM32WBA_GPIO_MODE_OUT, \
                WHAL_STM32WBA_GPIO_OUTTYPE_PUSHPULL, WHAL_STM32WBA_GPIO_SPEED_LOW, \
                WHAL_STM32WBA_GPIO_PULL_UP, 0), \
            /* USART1 TX: PB12, AF7 */ \
            [BOARD_UART_TX_PIN] = WHAL_STM32WBA_GPIO_PIN( \
                WHAL_STM32WBA_GPIO_PORT_B, 12, WHAL_STM32WBA_GPIO_MODE_ALTFN, \
                WHAL_STM32WBA_GPIO_OUTTYPE_PUSHPULL, WHAL_STM32WBA_GPIO_SPEED_FAST, \
                WHAL_STM32WBA_GPIO_PULL_UP, 7), \
            /* USART1 RX: PA8, AF7 */ \
            [BOARD_UART_RX_PIN] = WHAL_STM32WBA_GPIO_PIN( \
                WHAL_STM32WBA_GPIO_PORT_A, 8, WHAL_STM32WBA_GPIO_MODE_ALTFN, \
                WHAL_STM32WBA_GPIO_OUTTYPE_PUSHPULL, WHAL_STM32WBA_GPIO_SPEED_FAST, \
                WHAL_STM32WBA_GPIO_PULL_UP, 7), \
        }, \
        .pinCount = BOARD_PIN_COUNT, \
    }, \
}

/* UART singleton initializer — instantiated by stm32wb_uart.c (included by
 * stm32wba_uart.c) when WHAL_CFG_STM32WB_UART_SINGLE_INSTANCE is defined.
 * The UART alias header does not bridge the CFG macro, so the WB name is
 * used directly here. USART1 is clocked from SYSCLK (PLL1 = 100 MHz). */
#define WHAL_CFG_STM32WB_UART_DEV { \
    .base = WHAL_STM32WBA55_USART1_BASE, \
    .cfg = (void *)&(const whal_Stm32wba_Uart_Cfg){ \
        .brr = WHAL_STM32WBA_UART_BRR(100000000, 115200), \
    }, \
}

#endif /* WOLFHAL_BOARD_H */
