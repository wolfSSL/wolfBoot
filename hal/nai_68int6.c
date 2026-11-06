/* nai_68int6.c
 *
 * HAL for the NAI 68INT6 3U OpenVPX SBC (Intel Tiger Lake UP3).
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include <wolfboot/wolfboot.h>
#include <hal.h>
#include <stdint.h>
#include <uart_drv.h>
#include <printf.h>
#include <pci.h>
#include <x86/gdt.h>
#include <x86/fsp.h>
#include <x86/common.h>

#ifdef __WOLFBOOT

void hal_init(void)
{
    gdt_setup_table();
    gdt_update_segments();
    fsp_init_silicon();
}

/* Reset Control (0xCF9) FULL_RST: written alone it starts no reset, but makes
 * any later reset a full one, as the stock BIOS does before the OS loader
 * (its OS Reset Select default, Cold Reset). That covers SYS_RESET# from the
 * IPMC, which is otherwise a warm reset. */
#define RST_CNT_PORT 0xcf9
#define RST_CNT_FULL_RST (1 << 3)

void hal_prepare_boot(void)
{
    io_write8(RST_CNT_PORT, RST_CNT_FULL_RST);
}
#endif /* __WOLFBOOT */

int hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int hal_flash_erase(uintptr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

void hal_flash_unlock(void)
{
}

void hal_flash_lock(void)
{
}

/* No automatic fallback: a verification failure is terminal on this
 * target rather than silently reverting. */
int wolfBoot_fallback_is_possible(void)
{
    return 0;
}

int wolfBoot_dualboot_candidate(void)
{
    return PART_BOOT;
}

void* hal_get_primary_address(void)
{
    return (void*)0;
}

void* hal_get_update_address(void)
{
    return (void*)0;
}

void *hal_get_dts_address(void)
{
    return 0;
}

void *hal_get_dts_update_address(void)
{
    return 0;
}
