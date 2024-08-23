/* atsama5d3.c
 *
 * Copyright (C) 2024 wolfSSL Inc.
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
#include <string.h>
#include <target.h>
#include "image.h"
#ifndef ARCH_ARM
#   error "wolfBoot atsama5d3 HAL: wrong architecture selected. Please compile with ARCH=ARM."
#endif

#define TEST_ENCRYPT

/* Fixed addresses */
extern void *kernel_addr, *update_addr, *dts_addr;

void* hal_get_primary_address(void)
{
    return (void*)&kernel_addr;
}

void* hal_get_update_address(void)
{
  return (void*)&update_addr;
}

void* hal_get_dts_address(void)
{
  return (void*)&dts_addr;
}

void* hal_get_dts_update_address(void)
{
  return NULL; /* Not yet supported */
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


int RAMFUNCTION hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}


int RAMFUNCTION hal_flash_erase(uintptr_t address, int len)
{
    return 0;
}
