/* emu_hal.c
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
#include <string.h>
#include "target.h"
#include "hal.h"

#ifndef EMU_FLASH_SECTOR_SIZE
#define EMU_FLASH_SECTOR_SIZE WOLFBOOT_SECTOR_SIZE
#endif

#if defined(EMU_STM32)
#define FLASH_BASE        0x40022000u
#define FLASH_NSKEYR      (*(volatile uint32_t *)(FLASH_BASE + 0x004u))
#define FLASH_NSCR        (*(volatile uint32_t *)(FLASH_BASE + 0x028u))
#define FLASH_KEY1        0x45670123u
#define FLASH_KEY2        0xCDEF89ABu
#define FLASH_CR_LOCK     (1u << 0)
#define FLASH_CR_PG       (1u << 1)
#define FLASH_CR_SER      (1u << 2)
#define FLASH_CR_STRT     (1u << 5)
#define FLASH_CR_SNB_SHIFT 6
#define FLASH_CR_SNB_MASK (0x7fu << FLASH_CR_SNB_SHIFT)
#endif

#if defined(EMU_NRF5340)
#define NVMC_BASE         0x40039000u
#define NVMC_CONFIG       (*(volatile uint32_t *)(NVMC_BASE + 0x504u))
#endif

void hal_init(void)
{
}

void hal_prepare_boot(void)
{
}

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    if (data == 0 || len <= 0) {
        return 0;
    }
    memcpy((void *)address, data, (size_t)len);
    return 0;
}

int hal_flash_erase(uint32_t address, int len)
{
#if defined(EMU_NRF5340)
    (void)address;
    (void)len;
    return 0;
#else
    uint32_t end;
#if defined(EMU_STM32)
    uint32_t base = WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uint32_t sector = EMU_FLASH_SECTOR_SIZE;
    uint32_t start_sector;
    uint32_t end_sector;
    uint32_t snb;
    static uint32_t last_erase_sector = 0xFFFFFFFFu;
#endif
    if (len <= 0) {
        return 0;
    }
#if defined(EMU_STM32)
    if (sector == 0u) {
        return 0;
    }
    if (address < base) {
        return 0;
    }
    end = address + (uint32_t)len;
    start_sector = (address - base) / sector;
    end_sector = ((end - 1u) - base) / sector;
    for (snb = start_sector; snb <= end_sector; ++snb) {
        if (snb == last_erase_sector) {
            continue;
        }
        uint32_t cr = FLASH_NSCR & ~FLASH_CR_SNB_MASK;
        cr |= FLASH_CR_SER | (snb << FLASH_CR_SNB_SHIFT);
        FLASH_NSCR = cr;
        FLASH_NSCR = cr | FLASH_CR_STRT;
        last_erase_sector = snb;
    }
    FLASH_NSCR &= ~FLASH_CR_SER;
#else
    end = address + (uint32_t)len;
    memset((void *)address, 0xFF, (size_t)(end - address));
#endif
    return 0;
#endif
}

void hal_flash_unlock(void)
{
#if defined(EMU_STM32)
    if ((FLASH_NSCR & FLASH_CR_LOCK) != 0u) {
        FLASH_NSKEYR = FLASH_KEY1;
        FLASH_NSKEYR = FLASH_KEY2;
    }
    FLASH_NSCR |= FLASH_CR_PG;
#elif defined(EMU_NRF5340)
    NVMC_CONFIG = 1u;
#endif
}

void hal_flash_lock(void)
{
#if defined(EMU_STM32)
    FLASH_NSCR &= ~FLASH_CR_PG;
    FLASH_NSCR |= FLASH_CR_LOCK;
#elif defined(EMU_NRF5340)
    NVMC_CONFIG = 0u;
#endif
}
