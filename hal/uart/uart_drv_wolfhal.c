/* uart_drv_wolfhal.c
 *
 * A generic UART driver using the wolfHAL API module.
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#ifdef TARGET_wolfhal

#include <stdint.h>
#include <wolfHAL/wolfHAL.h>

extern whal_Uart g_whalUart;

int uart_tx(const uint8_t c)
{
    whal_Error err;
    err = whal_Uart_Send(&g_whalUart, &c, 1);
    if (err) {
        return err;
    }
    return 1;
}

int uart_rx(uint8_t *c)
{
    whal_Error err;
    err = whal_Uart_Recv(&g_whalUart, c, 1);
    if (err) {
        return err;
    }
    return 1;
}

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    /* Handle these configure options in the wolfHAL UART config */
    (void)bitrate;
    (void)data;
    (void)parity;
    (void)stop;

    whal_Error err;
    err = whal_Uart_Init(&g_whalUart);
    if (err) {
        return err;
    }

    return 0;
}

#ifdef DEBUG_UART
void uart_write(const char *buf, unsigned int len)
{
    whal_Uart_Send(&g_whalUart, (uint8_t *)buf, len);
}
#endif

#endif /* TARGET_wolfhal */
