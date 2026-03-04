/* board.h
 *
 * STM32WB55 Nucleo wolfHAL board header. Provides the WHAL_CFG_STM32WB_X_DEV
 * initializer macros that the upstream chip drivers use to instantiate the
 * whal_Stm32wb_X_Dev singletons (in their own .c file), plus the BOARD_X_DEV
 * handles that the wolfBoot adapter (hal/wolfhal.c) and board.c pass into
 * the wolfHAL API.
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
#include <wolfHAL/platform/st/stm32wb55xx.h>
#include <wolfHAL/clock/stm32wb_rcc.h>
#include <wolfHAL/flash/stm32wb_flash.h>
#include <wolfHAL/gpio/stm32wb_gpio.h>
#include <wolfHAL/uart/stm32wb_uart.h>

/* GPIO pin indices used by the runtime-addressable pinCfg table. */
enum {
    BOARD_LED_PIN,
    BOARD_UART_TX_PIN,
    BOARD_UART_RX_PIN,
    BOARD_PIN_COUNT,
};

/* Singletons owned by the upstream driver .c files. Declared here so
 * BOARD_FLASH_DEV can take its address. */
extern const whal_Gpio  whal_Stm32wb_Gpio_Dev;
extern const whal_Flash whal_Stm32wb_Flash_Dev;
extern const whal_Uart  whal_Stm32wb_Uart_Dev;

/* Device handles passed into the wolfHAL API. GPIO/UART use the
 * INTERNAL_DEV sentinel (the SINGLE_INSTANCE drivers ignore the dev
 * pointer and read from their static singleton); FLASH takes the
 * address so it could coexist with another flash driver in the future
 * without API churn. */
#define BOARD_GPIO_DEV   WHAL_INTERNAL_DEV
#define BOARD_UART_DEV   WHAL_INTERNAL_DEV
#define BOARD_FLASH_DEV  WHAL_INTERNAL_DEV

/* GPIO singleton initializer — instantiated by stm32wb_gpio.c. */
#define WHAL_CFG_STM32WB_GPIO_DEV { \
    .base = WHAL_STM32WB55_GPIO_BASE, \
    .cfg = (void *)&(const whal_Stm32wb_Gpio_Cfg){ \
        .pinCfg = (const whal_Stm32wb_Gpio_PinCfg[BOARD_PIN_COUNT]){ \
            [BOARD_LED_PIN] = WHAL_STM32WB_GPIO_PIN( \
                WHAL_STM32WB_GPIO_PORT_B, 5, WHAL_STM32WB_GPIO_MODE_OUT, \
                WHAL_STM32WB_GPIO_OUTTYPE_PUSHPULL, WHAL_STM32WB_GPIO_SPEED_LOW, \
                WHAL_STM32WB_GPIO_PULL_UP, 0), \
            [BOARD_UART_TX_PIN] = WHAL_STM32WB_GPIO_PIN( \
                WHAL_STM32WB_GPIO_PORT_B, 6, WHAL_STM32WB_GPIO_MODE_ALTFN, \
                WHAL_STM32WB_GPIO_OUTTYPE_PUSHPULL, WHAL_STM32WB_GPIO_SPEED_FAST, \
                WHAL_STM32WB_GPIO_PULL_UP, 7), \
            [BOARD_UART_RX_PIN] = WHAL_STM32WB_GPIO_PIN( \
                WHAL_STM32WB_GPIO_PORT_B, 7, WHAL_STM32WB_GPIO_MODE_ALTFN, \
                WHAL_STM32WB_GPIO_OUTTYPE_PUSHPULL, WHAL_STM32WB_GPIO_SPEED_FAST, \
                WHAL_STM32WB_GPIO_PULL_UP, 7), \
        }, \
        .pinCount = BOARD_PIN_COUNT, \
    }, \
}

/* Flash singleton initializer — instantiated by stm32wb_flash.c. */
#define WHAL_CFG_STM32WB_FLASH_DEV { \
    .base = WHAL_STM32WB55_FLASH_BASE, \
    .cfg = (void *)&(const whal_Stm32wb_Flash_Cfg){ \
        .startAddr = 0x08000000, \
        .size      = 0x100000, \
    }, \
}

/* UART singleton initializer — instantiated by stm32wb_uart.c when
 * WHAL_CFG_STM32WB_UART_SINGLE_INSTANCE is defined. */
#define WHAL_CFG_STM32WB_UART_DEV { \
    .base = WHAL_STM32WB55_UART1_BASE, \
    .cfg = (void *)&(const whal_Stm32wb_Uart_Cfg){ \
        .brr = WHAL_STM32WB_UART_BRR(64000000, 115200), \
    }, \
}

#endif /* WOLFHAL_BOARD_H */
