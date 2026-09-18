/* imx_rt7xx — wolfBoot HAL for the NXP i.MX RT700 (MIMXRT798S, Cortex-M33).
 *
 * Copyright (C) 2026 wolfSSL Inc.
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include "image.h"
#include "hal.h"
#include "imx_rt7xx.h"

/* The BootROM configures the compute-domain clocks and the XSPI0 XIP window
 * from the flash config block before handing control to this image, so no
 * clock or XSPI bring-up is required here for the initial boot path. Register
 * level XSPI0 program/erase for firmware update lands in a later slice; until
 * then the write side reports failure rather than faking success. */

void hal_init(void)
{
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return -1;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return -1;
}

int hal_flash_protect(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

void hal_prepare_boot(void)
{
}
