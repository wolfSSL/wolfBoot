/* nai_68int6.c
 *
 * HAL for the NAI 68INT6 3U OpenVPX SBC (Intel Core i7-118xGRE, Tiger Lake UP3).
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
#include <uart_drv.h>
#include <printf.h>
#include <pci.h>
#include <x86/gdt.h>
#include <x86/fsp.h>
#include <x86/common.h>
#include "nai_68int6.h"

#ifdef __WOLFBOOT

/* Read/write-protect the BIOS region (FPR0 from the FREG1 bounds) and set
 * FLOCKDN so it holds until the next platform reset. Returns 0, or -1 if a
 * protection register does not read back as set. Not called by default; wire
 * it into hal_flash_protect() for a hardened deployment. */
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

    /* FPR0 has the same base/limit layout as the Flash Region register, so the
     * BIOS region (FREG1) can be reused with the read/write protect bits set.
     * These registers are memory-mapped behind the BAR, not in PCI config. */
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
    if ((mmio_read32(spi_bar + SPI_FPR0) & (SPI_FPR_RPE | SPI_FPR_WPE))
            != (SPI_FPR_RPE | SPI_FPR_WPE)) {
        ret = -1;
    }

    reg = mmio_read32(spi_bar + SPI_HSFSTS_CTL);
    reg |= SPI_FLOCKDN;
    mmio_write32(spi_bar + SPI_HSFSTS_CTL, reg);
    if ((mmio_read32(spi_bar + SPI_HSFSTS_CTL) & SPI_FLOCKDN) == 0) {
        ret = -1;
    }

    pci_config_write32(0, SPI_PCI_DEV, SPI_PCI_FUN, PCI_COMMAND_OFFSET, spi_cmd);
    return ret;
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

void hal_hold_in_reset(void)
{
    /* Halt with interrupts disabled: no untrusted image boots and the platform
     * does not reset into a retry loop. Asserting a physical reset needs the
     * PRC watchdog-passthrough register (BAR 1), a follow-on once enumerated. */
    __asm__ volatile("cli");
    while (1) {
        __asm__ volatile("hlt");
    }
}

int hal_recovery_requested(void)
{
    /* Not implemented: the board documents no recovery strap, and the likely
     * TTL GPIO is behind PCIe BAR 1 (stage2 only). 0 keeps recovery
     * unreachable rather than triggered by a floating pin. */
    return 0;
}
#endif /* __WOLFBOOT */

static uint32_t spi_get_bar(void)
{
    uint32_t bar, cmd;

    bar = pci_config_read32(0, SPI_PCI_DEV, SPI_PCI_FUN, PCI_BAR0_OFFSET);
    bar &= PCI_BAR_MASK;
    cmd = pci_config_read32(0, SPI_PCI_DEV, SPI_PCI_FUN, PCI_COMMAND_OFFSET);
    if ((cmd & PCI_COMMAND_MEM_SPACE) == 0) {
        pci_config_write32(0, SPI_PCI_DEV, SPI_PCI_FUN, PCI_COMMAND_OFFSET,
                           cmd | PCI_COMMAND_MEM_SPACE);
    }
    return bar;
}

/* Run one hardware-sequencing cycle. For a write the caller loads FDATA first.
 * Returns 0 on completion, -1 on cycle error or timeout. */
static int spi_cycle(uint32_t bar, uint32_t faddr, uint32_t fcycle,
                     uint32_t nbytes)
{
    uint32_t ctl, sts;
    uint32_t i;

    for (i = 0; i < SPI_WAIT_MAX; i++) {
        if ((mmio_read32(bar + SPI_HSFSTS_CTL) & SPI_HSFSTS_SCIP) == 0)
            break;
    }
    if (i == SPI_WAIT_MAX)
        return -1;

    mmio_write32(bar + SPI_HSFSTS_CTL,
                 SPI_HSFSTS_FDONE | SPI_HSFSTS_FCERR | SPI_HSFSTS_HAEL);
    mmio_write32(bar + SPI_FADDR, faddr);
    ctl = SPI_HSFCTL_FGO
        | ((fcycle << SPI_HSFCTL_FCYCLE_SH) & SPI_HSFCTL_FCYCLE_MK);
    if (nbytes > 0) {
        ctl |= (((nbytes - 1) << SPI_HSFCTL_FDBC_SH) & SPI_HSFCTL_FDBC_MK);
    }
    mmio_write32(bar + SPI_HSFSTS_CTL, ctl);

    for (i = 0; i < SPI_WAIT_MAX; i++) {
        sts = mmio_read32(bar + SPI_HSFSTS_CTL);
        if (sts & (SPI_HSFSTS_FCERR | SPI_HSFSTS_HAEL))
            return -1;
        if (sts & SPI_HSFSTS_FDONE)
            return 0;
    }
    return -1;
}

/* Program up to 64 bytes (within one 256-byte page) at a flash linear offset. */
static int spi_write_chunk(uint32_t bar, uint32_t off, const uint8_t *buf,
                           uint32_t n)
{
    uint32_t i, w, c;

    for (i = 0; i < n; i += 4) {
        w = 0xFFFFFFFFU;
        c = (n - i >= 4) ? 4 : (n - i);
        memcpy(&w, buf + i, c);
        mmio_write32(bar + SPI_FDATA0 + i, w);
    }
    return spi_cycle(bar, off, SPI_FCYCLE_WRITE, n);
}

int hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    uint32_t bar = spi_get_bar();
    uint32_t off = FLASH_CPU_TO_OFFSET(address);
    int done = 0;
    uint32_t chunk, page_left;

    while (done < len) {
        chunk = SPI_HWSEQ_MAX;
        if (chunk > (uint32_t)(len - done))
            chunk = (uint32_t)(len - done);
        page_left = 0x100U - ((off + (uint32_t)done) & 0xFFU);
        if (chunk > page_left)
            chunk = page_left;
        if (spi_write_chunk(bar, off + (uint32_t)done, data + done, chunk) != 0)
            return -1;
        done += (int)chunk;
    }
    return 0;
}

int hal_flash_erase(uintptr_t address, int len)
{
    uint32_t bar = spi_get_bar();
    uint32_t foff = FLASH_CPU_TO_OFFSET(address);
    uint32_t off = foff & ~(SPI_SECTOR_SIZE - 1U);
    uint32_t end = foff + (uint32_t)len;

    while (off < end) {
        if (spi_cycle(bar, off, SPI_FCYCLE_ERASE4K, 0) != 0)
            return -1;
        off += SPI_SECTOR_SIZE;
    }
    return 0;
}

void hal_flash_unlock(void)
{
}

void hal_flash_lock(void)
{
}

/* Automatic fallback to the previous image after a failed update is
 * deliberately disabled on this target: the boot policy requires that a
 * verification failure is terminal rather than silently reverting. */
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
