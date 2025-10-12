/* uart_drv.h
 *
 * The HAL API definitions for UART driver extension.
 *
 * Copyright (C) 2021 wolfSSL Inc.
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

#ifndef H_HAL_UART_
#define H_HAL_UART_

#include <stdint.h>
int uart_tx(const uint8_t c);
int uart_rx(uint8_t *c);
int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop);

#ifdef DEBUG_UART
void uart_write(const char *buf, unsigned int len);
#endif

#endif /* H_HAL_UART_ */
