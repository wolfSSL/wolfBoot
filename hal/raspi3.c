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

#define TEST_ENCRYPT


#define CORTEXA53_0_CPU_CLK_FREQ_HZ 1099989014
#define CORTEXA53_0_TIMESTAMP_CLK_FREQ 99998999

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

#ifdef EXT_FLASH
int ext_flash_read(unsigned long address, uint8_t *data, int len)
{
    XMEMCPY(data, (void *)address, len);
    return len;
}

int ext_flash_erase(unsigned long address, int len)
{
    XMEMSET((void *)address, 0xFF, len);
    return len;
}

int ext_flash_write(unsigned long address, const uint8_t *data, int len)
{
    XMEMCPY((void *)address, data, len);
    return len;
}

void ext_flash_lock(void)
{
}

void ext_flash_unlock(void)
{
}

#endif


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
    #if defined(TEST_ENCRYPT) && defined (EXT_ENCRYPTED)
    char enc_key[] = "0123456789abcdef0123456789abcdef"
		 "0123456789abcdef";
    wolfBoot_set_encrypt_key((uint8_t *)enc_key,(uint8_t *)(enc_key +  32));
    #endif

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
