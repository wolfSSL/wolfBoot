/* x86_fsp_qemu.c
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

#include <wolfboot/wolfboot.h>
#include <stdint.h>
#include <string.h>
#include <uart_drv.h>

#ifdef __WOLFBOOT
#include <printf.h>
#include <x86/common.h>
#include <x86/ahci.h>
#include <x86/ata.h>
#include <x86/gpt.h>
#include <pci.h>

extern uint8_t* _stage2_params[];

#define PCI_AHCI_BUS 0
#define PCI_AHCI_DEV 31
#define PCI_AHCI_FUN 2

static uint32_t sata_bar = 0;

static void init_sata(void)
{
    uint32_t version;
    sata_bar = ahci_enable(PCI_AHCI_BUS, PCI_AHCI_DEV, PCI_AHCI_FUN);
    version = mmio_read32(AHCI_HBA_VS(sata_bar));
    if (version < 0x10000) {
        wolfBoot_printf("bad version\r\n");
        panic();
    }
    sata_enable(sata_bar);
}

void hal_init(void)
{
    init_sata();
}

void hal_prepare_boot(void)
{
    sata_disable(sata_bar);
}
#endif

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    return 0;
}

void hal_flash_unlock(void)
{
}

void hal_flash_lock(void)
{
}

int hal_flash_erase(uint32_t address, int len)
{
    return 0;
}

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
