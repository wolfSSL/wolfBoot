/* uart_flash.h
 *
 * Generic implementation of the read/write/erase
 * functionalities, on top of the uart_drv_*.h HAL.
 *
 * Compile with UART_FLASH=1
 *
 * Use tools/ufserver on the host to provide a remote
 * emulated non-volatile image via UART.
 *
 *
 * Copyright (C) 2020 wolfSSL Inc.
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

#ifndef UART_FLASH_DRI_H
#define UART_FLASH_DRI_H
#include <stdint.h>

#ifdef UART_FLASH
    #ifndef UART_FLASH_BITRATE
      #define UART_FLASH_BITRATE 460800
    #endif
    int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop);
    void uart_send_current_version(void);
#else
    #define uart_init(...) (-1)
    #define uart_send_current_version() do{}while(0)
#endif /* UART_FLASH */

#endif /* !UART_FLASH_DRI_H */
