/* psoc6.c
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

#include <stdint.h>
#include <target.h>
#include "image.h"

#include "cy_device_headers.h"

#include "cy_flash.h"
#include "cy_syspm.h"
#include "cy_ipc_drv.h"

#ifndef NVM_FLASH_WRITEONCE
#   error "wolfBoot psoc6 HAL: no WRITEONCE support detected. Please define NVM_FLASH_WRITEONCE"
#endif


#ifdef __WOLFBOOT
void hal_init(void)
{
    /* TODO: how to set clock full speed? */
}

void hal_prepare_boot(void)
{
    /* TODO: how to restore boot-default clock speed? */
}

#endif

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    if (len != WOLFBOOT_SECTOR_SIZE)
        return -1;
    Cy_Flash_WriteRow(address,(const uint32_t *) data);
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int start = -1, end = -1;
    uint32_t end_address;
    uint32_t p;
    if (len == 0)
        return -1;
    end_address = address + len - 1;
    for (p = address; p < end_address; p += WOLFBOOT_SECTOR_SIZE) {
        Cy_Flash_EraseRow(p);
    }
    return 0;
}

