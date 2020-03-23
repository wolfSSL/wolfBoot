/* hal.h
 *
 * The HAL API definitions.
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

#ifndef H_HAL_
#define H_HAL_

#include <inttypes.h>

#include "target.h"

/* Architecture specific calls */
#ifdef MMU
extern void do_boot(const uint32_t *app_offset, const uint32_t* dts_offset);
#else
extern void do_boot(const uint32_t *app_offset);
#endif
extern void arch_reboot(void);


void hal_init(void);
int hal_flash_write(uint32_t address, const uint8_t *data, int len);
int hal_flash_erase(uint32_t address, int len);
void hal_flash_unlock(void);
void hal_flash_lock(void);
void hal_prepare_boot(void);

#ifdef DUALBANK_SWAP
    void hal_flash_dualbank_swap(void);
#endif

#ifndef SPI_FLASH
    /* user supplied external flash interfaces */
    int  ext_flash_write(uintptr_t address, const uint8_t *data, int len);
    int  ext_flash_read(uintptr_t address, uint8_t *data, int len);
    int  ext_flash_erase(uintptr_t address, int len);
    void ext_flash_lock(void);
    void ext_flash_unlock(void);
#else
    #include "spi_flash.h"
    #define ext_flash_lock() do{}while(0)
    #define ext_flash_unlock() do{}while(0)
    #define ext_flash_read spi_flash_read
    #define ext_flash_write spi_flash_write

    static int ext_flash_erase(uintptr_t address, int len)
    {
        uint32_t end = address + len - 1;
        uint32_t p;
        for (p = address; p <= end; p += SPI_FLASH_SECTOR_SIZE)
            spi_flash_sector_erase(p);
        return 0;
    }
#endif /* !SPI_FLASH */

#endif /* H_HAL_FLASH_ */
