/* raspi3.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
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
#include <string.h>
#include <target.h>
#include "image.h"
#ifndef ARCH_AARCH64
#   error "wolfBoot raspi3 HAL: wrong architecture selected. Please compile with ARCH=AARCH64."
#endif


#define CORTEXA53_0_CPU_CLK_FREQ_HZ 1099989014
#define CORTEXA53_0_TIMESTAMP_CLK_FREQ 99998999

#ifndef WOLFBOOT_UPDATE_ADDRESS
#   define WOLFBOOT_UPDATE_ADDRESS 0x00000
#endif

static const void* kernel_addr = (void*)WOLFBOOT_LOAD_ADDRESS;
static const void* update_addr = (void*)WOLFBOOT_UPDATE_ADDRESS;

void* hal_get_primary_address(void)
{
    return (void*)kernel_addr;
}

void* hal_get_update_address(void)
{
  return (void*)update_addr;
}



/* QSPI functions */
void qspi_init(uint32_t cpu_clock, uint32_t flash_freq)
{
}


void zynq_init(uint32_t cpu_clock)
{
}



/* public HAL functions */
void hal_init(void)
{
}

void hal_prepare_boot(void)
{
}


int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
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
    return 0;
}
