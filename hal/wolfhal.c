/* wolfhal.c
 *
 * Generic wolfHAL port for wolfBoot.
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
#include "board.h"

/* Board-provided device instances */
extern whal_Flash g_wbFlash;
#ifdef DEBUG_UART
extern whal_Uart g_wbUart;
#endif

void RAMFUNCTION hal_flash_unlock(void)
{
    whal_Flash_Unlock(&g_wbFlash, 0, 0);
}

void RAMFUNCTION hal_flash_lock(void)
{
    whal_Flash_Lock(&g_wbFlash, 0, 0);
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    return whal_Flash_Write(&g_wbFlash, (size_t)address, data, (size_t)len);
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    return whal_Flash_Erase(&g_wbFlash, (size_t)address, (size_t)len);
}

#ifdef DEBUG_UART
void uart_write(const char *buf, unsigned int len)
{
    whal_Uart_Send(&g_wbUart, (const uint8_t *)buf, (size_t)len);
}
#endif
