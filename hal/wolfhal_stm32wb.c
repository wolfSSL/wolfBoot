/* wolfhal_stm32wb.c
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

#include <stdint.h>
#include "hal.h"
#include "wolfboot/wolfboot.h"
#include <wolfHAL/clock/stm32wb_rcc.h>
#include <wolfHAL/flash/stm32wb_flash.h>
#ifdef DEBUG_UART
#include <wolfHAL/gpio/stm32wb_gpio.h>
#include <wolfHAL/uart/stm32wb_uart.h>
#endif

/* Board-provided device instances */
extern whal_Flash g_wbFlash;
extern whal_Clock g_wbClock;
#ifdef DEBUG_UART
extern whal_Gpio g_wbGpio;
extern whal_Uart g_wbUart;
#endif

void hal_init(void)
{
    whal_Stm32wbRccPll_Init(&g_wbClock);
    whal_Stm32wbFlash_Init(&g_wbFlash);
#ifdef DEBUG_UART
    whal_Stm32wbGpio_Init(&g_wbGpio);
    whal_Stm32wbUart_Init(&g_wbUart);
#endif
}

void hal_prepare_boot(void)
{
#ifdef DEBUG_UART
    whal_Stm32wbUart_Deinit(&g_wbUart);
    whal_Stm32wbGpio_Deinit(&g_wbGpio);
#endif
    whal_Stm32wbFlash_Deinit(&g_wbFlash);
    whal_Stm32wbRccPll_Deinit(&g_wbClock);
}

void RAMFUNCTION hal_flash_unlock(void)
{
    whal_Stm32wbFlash_Unlock(&g_wbFlash, 0, 0);
}

void RAMFUNCTION hal_flash_lock(void)
{
    whal_Stm32wbFlash_Lock(&g_wbFlash, 0, 0);
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    return whal_Stm32wbFlash_Write(&g_wbFlash, (size_t)address, data, (size_t)len);
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    return whal_Stm32wbFlash_Erase(&g_wbFlash, (size_t)address, (size_t)len);
}

#ifdef DEBUG_UART
void uart_write(const char *buf, unsigned int len)
{
    whal_Stm32wbUart_Send(&g_wbUart, (const uint8_t *)buf, (size_t)len);
}
#endif
