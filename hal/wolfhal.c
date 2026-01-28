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

extern whal_Clock wbClock;
extern whal_Gpio wbGpio;
extern whal_Uart wbUart;
extern whal_Flash wbFlash;

#ifdef WOLFHAL_INIT_HOOKS
extern void hal_pre_init();
extern void hal_post_prepare_boot();
#endif /* WOLFHAL_INIT_HOOKS */

void RAMFUNCTION hal_flash_unlock(void)
{
    whal_Flash_Unlock(&wbFlash,
                      0, WOLFHAL_FLASH_SIZE);
}

void RAMFUNCTION hal_flash_lock(void)
{
    whal_Flash_Lock(&wbFlash,
                    0, WOLFHAL_FLASH_SIZE);
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    whal_Flash_Write(&wbFlash, address, data, len);
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    if (len == 0)
        return -1;

    whal_Flash_Erase(&wbFlash, address, len);
    return 0;
}

void hal_init(void)
{
#ifdef WOLFHAL_INIT_HOOKS
    hal_pre_init();
#endif /* WOLFHAL_INIT_HOOKS */

    whal_Clock_Init(&wbClock);
    whal_Clock_Enable(&wbClock);

    whal_Gpio_Init(&wbGpio);
    whal_Uart_Init(&wbUart);
    whal_Flash_Init(&wbFlash);
}

void hal_prepare_boot(void)
{
    whal_Flash_Deinit(&wbFlash);
    whal_Uart_Deinit(&wbUart);
    whal_Gpio_Deinit(&wbGpio);

    whal_Clock_Disable(&wbClock);
    whal_Clock_Deinit(&wbClock);

#ifdef WOLFHAL_INIT_HOOKS
    hal_post_prepare_boot();
#endif /* WOLFHAL_INIT_HOOKS */
}
