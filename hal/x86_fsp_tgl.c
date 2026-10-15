/* x86_fsp_tgl.c
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

#include "wolfboot/wolfboot.h"
#include <stdint.h>

#ifdef __WOLFBOOT

#include "uart_drv.h"
#include "printf.h"
#include "loader.h"
#include "hal.h"
#include "pci.h"

#ifdef WOLFBOOT_FSP
#include "x86/ahci.h"
#include "x86/ata.h"
#include "x86/common.h"
#include "x86/tgl_fsp.h"
#endif

#if defined(TARGET_kontron_vx3060_s2) || defined(TARGET_nai_68int6)
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
    sata_bar = 0;
#endif
    (void)drv;
}

int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf)
{
    return ata_drive_read(drv, start, count, buf);
}

int disk_write(int drv, uint64_t start, uint32_t count, const uint8_t *buf)
{
    return ata_drive_write(drv, start, count, buf);
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

#ifdef WOLFBOOT_TGL
#define SPI_PCI_DEV 31
#define SPI_PCI_FUN 5
#define SPI_BAR_OFF 0x10
/* Tiger Lake SPI controller register offsets, memory-mapped at the
 * BAR0 base. FREG1 holds the BIOS flash region base/limit (region 0
 * is the flash descriptor); FPR0 is the protected range register
 * with the same base/limit layout. Offsets per the Intel PCH SPI
 * register map (see drivers/spi/spi-intel.c in the Linux kernel):
 * FDATA0-15 at 0x10-0x4C, FRACC at 0x50, FREG0-7 at 0x54-0x74,
 * FPR0-4 at 0x84-0x9C. */
#define SPI_FREG1 0x58
#define SPI_FREG_BASE_MASK (0x7fffU << 0)
#define SPI_FREG_LIMIT_MASK (0x7fffU << 16)
#define SPI_FREG_LIMIT_SHIFT (16)
#define SPI_FREG_ADDR_SHIFT (12)
#define SPI_FPR0 (0x84)
#define SPI_FPR_WPE (1U << 31)
#define SPI_FPR_RPE (1U << 15)
#define SPI_BIOS_HSFSTS_CTL (0x4)
#define SPI_FLOCKDN (1U << 15)

int tgl_lock_bios_region(void)
{
    uint32_t spi_bar, spi_cmd;
    uint32_t reg;
    int ret = 0;

#if defined(DEBUG)
    uint32_t bios_reg_base, bios_reg_lim;
#endif

    spi_bar = pci_config_read32(0, SPI_PCI_DEV, SPI_PCI_FUN, PCI_BAR0_OFFSET);
    spi_bar &= PCI_BAR_MASK;
    spi_cmd =
        pci_config_read32(0, SPI_PCI_DEV, SPI_PCI_FUN, PCI_COMMAND_OFFSET);
    pci_config_write32(0, SPI_PCI_DEV, SPI_PCI_FUN, PCI_COMMAND_OFFSET,
                       spi_cmd | PCI_COMMAND_MEM_SPACE);

    /* The Flash Protected Range register has the same base/limit
     * layout as the Flash Region register: take the BIOS region
     * (FREG1, flash region 1) and enable read and write protection
     * on it. The SPI registers live in the BAR's memory-mapped
     * space, not in PCI configuration space. */
    reg = mmio_read32(spi_bar + SPI_FREG1);
#if defined(DEBUG)
    bios_reg_base = (reg & SPI_FREG_BASE_MASK) << SPI_FREG_ADDR_SHIFT;
    bios_reg_lim = ((reg & SPI_FREG_LIMIT_MASK) >> SPI_FREG_LIMIT_SHIFT)
                   << SPI_FREG_ADDR_SHIFT;
    wolfBoot_printf("Bios reg base: 0x%x lim: 0x%x\r\n", bios_reg_base,
                    bios_reg_lim);
#endif
    reg |= (SPI_FPR_RPE) | (SPI_FPR_WPE);
    mmio_write32(spi_bar + SPI_FPR0, reg);
    if ((mmio_read32(spi_bar + SPI_FPR0) &
         (SPI_FPR_RPE | SPI_FPR_WPE)) != (SPI_FPR_RPE | SPI_FPR_WPE)) {
        ret = -1;
    }

    /* lock down BIOS register configuration */
    reg = mmio_read32(spi_bar + SPI_BIOS_HSFSTS_CTL);
    reg |= SPI_FLOCKDN;
    mmio_write32(spi_bar + SPI_BIOS_HSFSTS_CTL, reg);
    if ((mmio_read32(spi_bar + SPI_BIOS_HSFSTS_CTL) & SPI_FLOCKDN) == 0) {
        ret = -1;
    }

    /* restore original cmd */
    pci_config_write32(0, SPI_PCI_DEV, SPI_PCI_FUN, PCI_COMMAND_OFFSET, spi_cmd);
    return ret;
}

int hal_flash_protect(haladdr_t address, int len)
{
    (void)address;
    (void)len;

    /* The TGL BIOS region covers the bootloader partition, so the
     * hook's address/len are the same range FREG1 describes. */
    return tgl_lock_bios_region();
}
#endif /* WOLFBOOT_TGL */
#endif

