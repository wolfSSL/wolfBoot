/* nrf5340_uart.c
 *
 * CRLF line conversion for the nRF5340 debug UART, split out of
 * hal/nrf5340.c so the newline handling can be unit-tested on the host
 * without the nrfx register access the rest of that HAL needs.
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
 * along with wolfBoot; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1335, USA
 */

#ifdef DEBUG_UART

#include <string.h>

/* Emit buf[0..sz) via "sink" with every '\n' rendered as CRLF. */
void nrf5340_uart_crlf(const char* buf, unsigned int sz,
        void (*sink)(const char*, unsigned int))
{
    const char* line;
    unsigned int lineSz;
    do {
        /* find '\n' */
        line = memchr(buf, '\n', sz);
        if (line == NULL) {
            sink(buf, sz);
            break;
        }
        lineSz = (unsigned int)(line - buf);
        if (lineSz > sz - 1)
            lineSz = sz - 1;

        sink(buf, lineSz);
        sink("\r\n", 2); /* handle CRLF */

        buf = line + 1; /* advance past the emitted newline */
        sz -= lineSz + 1;
    } while ((int)sz > 0);
}

#endif /* DEBUG_UART */
