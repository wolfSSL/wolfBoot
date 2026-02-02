/* stm32wb.c
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

#include <stdint.h>
#include <wolfHAL/wolfHAL.h>
#include "image.h"

extern whal_Clock wbClockController;
#ifndef WOLFHAL_NO_GPIO
extern whal_Gpio wbGpio;
#endif
extern whal_Flash wbFlash;
#if defined(DEBUG_UART) || defined(UART_FLASH)
extern whal_Uart wbUart;
#endif /* DEBUG_UART || UART_FLASH */

void hal_init(void)
{
    whal_Error err;

    err = whal_Clock_Init(&wbClockController);
    if (err) {
        return;
    }

#ifndef WOLFHAL_NO_GPIO
    err = whal_Gpio_Init(&wbGpio);
    if (err) {
        return;
    }
#endif

    err = whal_Flash_Init(&wbFlash);
    if (err) {
        return;
    }

#if defined(DEBUG_UART) || defined(UART_FLASH)
    err = whal_Uart_Init(&wbUart);
    if (err) {
        return;
    }
#endif /* DEBUG_UART || UART_FLASH */

}

void hal_prepare_boot(void)
{
#if defined(DEBUG_UART) || defined(UART_FLASH)
    whal_Uart_Deinit(&wbUart);
#endif /* DEBUG_UART || UART_FLASH */

    whal_Flash_Deinit(&wbFlash);

#ifndef WOLFHAL_NO_GPIO
    whal_Gpio_Deinit(&wbGpio);
#endif

    whal_Clock_Deinit(&wbClockController);
}
