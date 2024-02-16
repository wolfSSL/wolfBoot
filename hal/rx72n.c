/* rx72n.c
 *
 * Stubs for custom HAL implementation. Defines the
 * functions used by wolfboot for a specific target.
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "user_settings.h"

#include <target.h>
#include "hal.h"

#ifdef DEBUG_UART
/* UART Stubs */
void uart_init(void)
{

}
void uart_write(const char* buf, uint32_t sz)
{

}
#endif /* DEBUG_UART */

/* HAL Stubs */
void hal_init(void)
{
#ifdef DEBUG_UART
    uart_init();
    uart_write("wolfBoot HAL Init\n", 18);
#endif
    return;
}
int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}
int hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}
void hal_flash_unlock(void)
{
    return;
}
void hal_flash_lock(void)
{
    return;
}
void hal_prepare_boot(void)
{
    return;
}
