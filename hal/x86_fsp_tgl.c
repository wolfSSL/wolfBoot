/* fsp_tgl.c
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
#include <uart_drv.h>
#include <printf.h>
#include <x86/ahci.h>
#include <x86/ata.h>
#include <x86/common.h>

#ifdef __WOLFBOOT

#define PCI_AHCI_BUS 0
#define PCI_AHCI_DEV 0x17
#define PCI_AHCI_FUN 0

void x86_fsp_tgl_init_sata(void)
{
    uint32_t sata_bar;
    uint32_t version;
    sata_bar = ahci_enable(PCI_AHCI_BUS, PCI_AHCI_DEV, PCI_AHCI_FUN);
    version = mmio_read32(AHCI_HBA_VS(sata_bar));
    if (version < 0x10000) {
        wolfBoot_printf("bad version\r\n");
        panic();
    }
    sata_enable(sata_bar);
}

#endif
