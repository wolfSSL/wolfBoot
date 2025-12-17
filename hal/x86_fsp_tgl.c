/* fsp_tgl.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#include "wolfboot/wolfboot.h"
#include <stdint.h>

#ifdef __WOLFBOOT

#include "uart_drv.h"
#include "printf.h"
#include "loader.h"

#ifdef WOLFBOOT_FSP
#include "x86/ahci.h"
#include "x86/ata.h"
#include "x86/common.h"
#include "x86/tgl_fsp.h"
#endif

#if defined(TARGET_kontron_vx3060_s2)
#define PCI_AHCI_BUS 0
#define PCI_AHCI_DEV 0x17
#define PCI_AHCI_FUN 0
#elif defined(TARGET_x86_fsp_qemu)
#define PCI_AHCI_BUS 0
#define PCI_AHCI_DEV 31
#define PCI_AHCI_FUN 2
#endif

#ifdef WOLFBOOT_FSP
static uint32_t sata_bar;
#endif

int disk_init(int drv)
{
    int ret = 0;
#ifdef WOLFBOOT_FSP
    ret = x86_fsp_tgl_init_sata(&sata_bar);
    if (ret != 0)
        wolfBoot_panic();
#ifdef WOLFBOOT_ATA_DISK_LOCK
    ret = sata_unlock_disk(drv, 1);
    if (ret != 0)
        wolfBoot_panic();
#endif
#endif /* WOLFBOOT_FSP */
    (void)drv;
    return ret;
}

void disk_close(int drv)
{
#ifdef WOLFBOOT_FSP
    sata_disable(sata_bar);
#endif
    (void)drv;
}

int disk_read(int drv, uint64_t start, uint32_t count, uint32_t *buf)
{
    return ata_drive_read(drv, start, count, (uint8_t*)buf);
}

int disk_write(int drv, uint64_t start, uint32_t count, const uint32_t *buf)
{
    return ata_drive_write(drv, start, count, (const uint8_t*)buf);
}

/*!
 * \brief Initializes the SATA controller.
 *
 * \param bar Pointer to store the SATA BAR.
 * \return 0 on success, -1 if the AHCI version is invalid.
 */
int x86_fsp_tgl_init_sata(uint32_t *bar)
{
    uint32_t sata_bar;
    uint32_t version;
    sata_bar = ahci_enable(PCI_AHCI_BUS, PCI_AHCI_DEV, PCI_AHCI_FUN);
    version = mmio_read32(AHCI_HBA_VS(sata_bar));
    if (version < 0x10000) {
        wolfBoot_printf("SATA: bad version: %d\r\n", (int)version);
        return -1;
    }
    sata_enable(sata_bar);
    if (bar != NULL)
        *bar = sata_bar;
    return 0;
}
#endif

