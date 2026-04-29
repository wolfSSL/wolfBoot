/* board.h
 *
 * wolfHAL board header for the STM32WB55 Nucleo.
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

#include <wolfHAL/clock/stm32wb_rcc.h>
#include <wolfHAL/flash/stm32wb_flash.h>
#include <wolfHAL/gpio/stm32wb_gpio.h>
#include <wolfHAL/uart/stm32wb_uart.h>

/* GPIO pin indices (matches pin array in board.c) */
enum {
    BOARD_LED_PIN,
    BOARD_UART_TX_PIN,
    BOARD_UART_RX_PIN,
    BOARD_PIN_COUNT,
};

#endif /* WOLFHAL_BOARD_H */
