/* kontron_vx3060_s2.c
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

#include <wolfboot/wolfboot.h>
#include <stdint.h>
#include <uart_drv.h>
#include <printf.h>
#include <pci.h>
#include <x86/gdt.h>
#include <x86/fsp.h>
#include <x86/common.h>

#ifdef __WOLFBOOT

#define SPI_PCI_DEV 31
#define SPI_PCI_FUN 5
#define SPI_BAR_OFF 0x10
#define SPI_FREG1 0x58
#define SPI_FREG_BASE_MASK (0x7fffU << 0)
#define SPI_FREG_LIMIT_MASK (0x7fffU << 16)
#define SPI_FREG_LIMIT_SHIFT (16)
#define SPI_FREG_ADDR_SHIFT (12)
#define SPI_FPR0 (0x48)
#define SPI_FPR_WPE (1U << 31)
#define SPI_FPR_RPE (1U << 15)
#define SPI_BIOS_HSFSTS_CTL (0x4)
#define SPI_FLOCKDN (1U << 15)

int tgl_lock_bios_region()
{
    uint32_t spi_bar, spi_cmd;
    uint32_t reg;

#if defined(DEBUG)
    uint32_t bios_reg_base, bios_reg_lim;
#endif

    spi_bar = pci_config_read32(0, SPI_PCI_DEV, SPI_PCI_FUN, PCI_BAR0_OFFSET);
    spi_bar &= PCI_BAR_MASK;
    spi_cmd =
        pci_config_read32(0, SPI_PCI_DEV, SPI_PCI_FUN, PCI_COMMAND_OFFSET);
    pci_config_write32(0, SPI_PCI_DEV, SPI_PCI_FUN, PCI_COMMAND_OFFSET,
                       spi_cmd | PCI_COMMAND_MEM_SPACE);

    reg = mmio_read32(spi_bar + SPI_FREG1);
#if defined(DEBUG)
    bios_reg_base = (reg & SPI_FREG_BASE_MASK) << SPI_FREG_ADDR_SHIFT;
    bios_reg_lim = ((reg & SPI_FREG_LIMIT_MASK) >> SPI_FREG_LIMIT_SHIFT)
                   << SPI_FREG_ADDR_SHIFT;
    wolfBoot_printf("Bios reg base: 0x%x lim: 0x%x\r\n", bios_reg_base,
                    bios_reg_lim);
#endif
    /* Flash Protected Range register has very similar layout of the Flash
     * Region Register, so we can reuse it and just enable read and write
     * protection
     */
    reg |= (SPI_FPR_RPE) | (SPI_FPR_WPE);
    pci_config_write32(0, SPI_PCI_DEV, SPI_PCI_FUN, SPI_FPR0, reg);

    /* lock down BIOS register configuration */
    reg = pci_config_read32(0, SPI_PCI_DEV, SPI_PCI_FUN, SPI_BIOS_HSFSTS_CTL);
    reg |= SPI_FLOCKDN;
    pci_config_write32(0, SPI_PCI_DEV, SPI_PCI_FUN, SPI_BIOS_HSFSTS_CTL, reg);

    /* restore original cmd */
    pci_config_write32(0, SPI_PCI_DEV, SPI_PCI_FUN, PCI_COMMAND_OFFSET, spi_cmd);
    return 0;
}

void hal_init(void)
{
    gdt_setup_table();
    gdt_update_segments();
    fsp_init_silicon();
}

void hal_prepare_boot(void)
{
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
