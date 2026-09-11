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

#if defined(EMU_M2354)
/* M2354 flash is programmed through the FMC ISP engine, never by storing to
 * the mapped address. Only reached with TZEN=0: the non-secure app goes
 * through the wolfBoot_nsc_* veneers instead, as FMC stays secure. */
#define FMC_BASE          0x4000C000u
#define FMC_ISPCTL        (*(volatile uint32_t *)(FMC_BASE + 0x00u))
#define FMC_ISPADDR       (*(volatile uint32_t *)(FMC_BASE + 0x04u))
#define FMC_ISPDAT        (*(volatile uint32_t *)(FMC_BASE + 0x08u))
#define FMC_ISPCMD        (*(volatile uint32_t *)(FMC_BASE + 0x0Cu))
#define FMC_ISPTRG        (*(volatile uint32_t *)(FMC_BASE + 0x10u))
#define FMC_ISPCTL_ISPEN  (1u << 0)
#define FMC_ISPCTL_APUEN  (1u << 3)
#define FMC_ISPCTL_ISPFF  (1u << 6)
#define FMC_ISPTRG_ISPGO  (1u << 0)
#define FMC_ISPCMD_PROGRAM     0x21u
#define FMC_ISPCMD_PAGE_ERASE  0x22u
#define M2354_NS_OFFSET   0x10000000u

#define SYS_BASE          0x40000000u
#define SYS_REGLCTL       (*(volatile uint32_t *)(SYS_BASE + 0x100u))

static int m2354_isp(uint32_t cmd, uint32_t addr, uint32_t data)
{
    /* The ISP engine addresses physical flash; drop the alias bit. */
    FMC_ISPCMD = cmd;
    FMC_ISPADDR = addr & ~M2354_NS_OFFSET;
    FMC_ISPDAT = data;
    FMC_ISPTRG = FMC_ISPTRG_ISPGO;
    while ((FMC_ISPTRG & FMC_ISPTRG_ISPGO) != 0u) {
    }
    if ((FMC_ISPCTL & FMC_ISPCTL_ISPFF) != 0u) {
        FMC_ISPCTL |= FMC_ISPCTL_ISPFF;   /* write-1-to-clear */
        return -1;
    }
    return 0;
}
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
#if defined(EMU_M2354)
    {
        int i = 0;
        while (i < len) {
            uint32_t here = address + (uint32_t)i;
            uint32_t word_addr = here & ~0x3u;
            uint32_t off = here - word_addr;
            uint32_t word = *(volatile uint32_t *)word_addr;
            uint8_t *b = (uint8_t *)&word;
            while (off < 4u && i < len) {
                b[off] = data[i];
                off++;
                i++;
            }
            if (m2354_isp(FMC_ISPCMD_PROGRAM, word_addr, word) != 0) {
                return -1;
            }
        }
    }
#else
    memcpy((void *)address, data, (size_t)len);
#endif
    return 0;
}

int hal_flash_erase(uint32_t address, int len)
{
#if defined(EMU_M2354)
    uint32_t p;
    uint32_t end;
    if (len <= 0) {
        return 0;
    }
    end = address + (uint32_t)len;
    for (p = address & ~(EMU_FLASH_SECTOR_SIZE - 1u); p < end;
            p += EMU_FLASH_SECTOR_SIZE) {
        if (m2354_isp(FMC_ISPCMD_PAGE_ERASE, p, 0u) != 0) {
            return -1;
        }
    }
    return 0;
#elif defined(EMU_NRF5340)
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
#if defined(EMU_M2354)
    /* SYS register write protection guards ISPCTL. */
    SYS_REGLCTL = 0x59u;
    SYS_REGLCTL = 0x16u;
    SYS_REGLCTL = 0x88u;
    FMC_ISPCTL |= FMC_ISPCTL_ISPEN | FMC_ISPCTL_APUEN;
#elif defined(EMU_STM32)
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
#if defined(EMU_M2354)
    FMC_ISPCTL &= ~(FMC_ISPCTL_ISPEN | FMC_ISPCTL_APUEN);
    SYS_REGLCTL = 0u;
#elif defined(EMU_STM32)
    FLASH_NSCR &= ~FLASH_CR_PG;
    FLASH_NSCR |= FLASH_CR_LOCK;
#elif defined(EMU_NRF5340)
    NVMC_CONFIG = 0u;
#endif
}
