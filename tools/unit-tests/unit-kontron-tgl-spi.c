/* unit-kontron-tgl-spi.c
 *
 * Regression test for F-12104: the Kontron VX3060 S2 (Tiger Lake)
 * SPI BIOS-region lock was never applied - no hal_flash_protect()
 * override existed, so the weak no-op default ran before handoff -
 * and the only helper, tgl_lock_bios_region(), wrote the protected
 * range and lock values through PCI configuration space instead of
 * the SPI BAR's memory-mapped register space, using 0x48 for FPR0
 * (a FDATA scratch register in the SPI map, FPR0 lives at 0x84).
 *
 * The real function is extracted by the Makefile and run against
 * mocked PCI config space and an MMIO array at the BAR address.
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

#include <check.h>
#include <pci.h>
#include <stdint.h>
#include <string.h>

typedef uintptr_t haladdr_t;

#define MMIO_BASE 0xFED40000UL

/* SPI PCI device constants + register offsets from
 * hal/kontron_vx3060_s2.c (extracted by the Makefile). */
#include "kontron_spi_extract.h"

/* TGL register offsets used by the model. */
#define TGL_FREG0_OFF 0x54
#define TGL_FREG1_OFF 0x58
#define TGL_FPR0_OFF 0x84
#define TGL_SFSTS_CTL_OFF 0x04

/* FREG1: BIOS region (Intel flash region 1; region 0 is the flash
 * descriptor), base/limit fields (15 bits each, shifted 12). FREG0:
 * a different range, so a test that compares the FPR0 value against
 * FREG1 also proves FREG0 was not the source. */
#define FREG0_INIT 0x7FFF7400U
#define FREG1_INIT 0x3FFF0000U

static uint32_t g_pci_cfg[16]; /* 64 bytes of config space */
static uint32_t g_mmio[64]; /* 256 bytes of SPI BAR MMIO, 32-bit regs */
static uint8_t g_cfg_write_off[16];
static int g_cfg_write_count;
static int g_fail_fpr_readback;
static int g_fail_lockdn_readback;

static void sim_reset(void)
{
    memset(g_mmio, 0, sizeof(g_mmio));
    memset(g_pci_cfg, 0, sizeof(g_pci_cfg));
    g_pci_cfg[PCI_COMMAND_OFFSET / 4] = 0x0001; /* IO space only */
    /* memory BAR, read-write (type bit 0 set), 32-bit: the code's
     * PCI_BAR_MASK strips the type bits */
    g_pci_cfg[PCI_BAR0_OFFSET / 4] = (uint32_t)(MMIO_BASE) | 0x1;
    g_mmio[TGL_FREG0_OFF / 4] = FREG0_INIT;
    g_mmio[TGL_FREG1_OFF / 4] = FREG1_INIT;
    g_cfg_write_count = 0;
    g_fail_fpr_readback = 0;
    g_fail_lockdn_readback = 0;
}

/* Mocks for the PCI config accessors (src/pci.c). */
uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fun,
                           uint8_t off)
{
    (void)bus;
    (void)dev;
    (void)fun;

    return g_pci_cfg[off / 4];
}

void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t fun,
                        uint8_t off, uint32_t val)
{
    (void)bus;
    (void)dev;
    (void)fun;

    if (g_cfg_write_count < (int)(sizeof(g_cfg_write_off) /
                                  sizeof(g_cfg_write_off[0])))
        g_cfg_write_off[g_cfg_write_count] = off;
    g_cfg_write_count++;
    if (off == PCI_COMMAND_OFFSET)
        g_pci_cfg[off / 4] = val;
}

/* Mocks for the MMIO accessors (src/x86/common.c). */
static void mmio_write32(uintptr_t address, uint32_t value)
{
    g_mmio[(address - MMIO_BASE) / 4] = value;
}

static uint32_t mmio_read32(uintptr_t address)
{
    uint32_t val = g_mmio[(address - MMIO_BASE) / 4];

    if (g_fail_fpr_readback && (address - MMIO_BASE) == TGL_FPR0_OFF)
        val &= ~SPI_FPR_WPE;
    if (g_fail_lockdn_readback &&
        (address - MMIO_BASE) == TGL_SFSTS_CTL_OFF)
        val &= ~SPI_FLOCKDN;
    return val;
}

/* The real tgl_lock_bios_region() + hal_flash_protect() from
 * hal/kontron_vx3060_s2.c (extracted). */
#include "kontron_spi_fn_extract.h"

/* The lock must land in the MMIO space: FPR0 carries the FREG1
 * (BIOS region) base/limit with RPE/WPE set, FLOCKDN is set in
 * BIOS/H SFSTS/CTL, and PCI config space sees only the COMMAND
 * enable/restore. Pre-fix the values went to PCI config offsets
 * 0x48 and 0x04 and FPR0 was never written. */
START_TEST (test_lock_written_to_mmio){
    uint32_t expected_fpr0 = FREG1_INIT | SPI_FPR_RPE | SPI_FPR_WPE;
    int i, ret;
    int cmd_restored = 1;

    sim_reset();
    ret = tgl_lock_bios_region();

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(g_mmio[TGL_FPR0_OFF / 4], expected_fpr0);
    ck_assert_uint_eq(g_mmio[TGL_SFSTS_CTL_OFF / 4] &
                      SPI_FLOCKDN, SPI_FLOCKDN);
    /* FREG1 itself is not modified */
    ck_assert_uint_eq(g_mmio[TGL_FREG1_OFF / 4], FREG1_INIT);
    /* config space: only the COMMAND register is written, and the
     * final write restores the original value */
    for (i = 0; i < g_cfg_write_count; i++)
        ck_assert_int_eq(g_cfg_write_off[i], PCI_COMMAND_OFFSET);
    if (g_cfg_write_count > 0)
        cmd_restored = (g_pci_cfg[PCI_COMMAND_OFFSET / 4] == 0x0001);
    ck_assert_int_eq(cmd_restored, 1);
}
END_TEST

/* The hal_flash_protect() override must route to the TGL lock with
 * the hook's address/len, so the update paths'
 * hal_flash_protect(WOLFBOOT_ORIGIN, BOOTLOADER_PARTITION_SIZE)
 * call actually establishes protection. */
START_TEST(test_hal_flash_protect_wires_lock)
{
    uint32_t expected_fpr0 = FREG1_INIT | SPI_FPR_RPE | SPI_FPR_WPE;
    int ret;

    sim_reset();
    ret = hal_flash_protect(0xFFF00000, 0x600000);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(g_mmio[TGL_FPR0_OFF / 4], expected_fpr0);
    ck_assert_uint_eq(g_mmio[TGL_SFSTS_CTL_OFF / 4] &
                      SPI_FLOCKDN, SPI_FLOCKDN);
}
END_TEST

/* A protected range that does not stick (readback missing WPE)
 * must be reported as an error, not silently accepted. */
START_TEST(test_fpr_readback_mismatch)
{
    int ret;

    sim_reset();
    g_fail_fpr_readback = 1;
    ret = tgl_lock_bios_region();

    ck_assert_int_lt(ret, 0);
}
END_TEST

/* Same for the FLOCKDN bit. */
START_TEST(test_flockdn_readback_mismatch)
{
    int ret;

    sim_reset();
    g_fail_lockdn_readback = 1;
    ret = tgl_lock_bios_region();

    ck_assert_int_lt(ret, 0);
}
END_TEST

Suite *kontron_tgl_spi_suite(void)
{
    Suite *s  = suite_create("kontron tgl spi");
    TCase *tc = tcase_create("bios-region lock");

    tcase_add_test(tc, test_lock_written_to_mmio);
    tcase_add_test(tc, test_hal_flash_protect_wires_lock);
    tcase_add_test(tc, test_fpr_readback_mismatch);
    tcase_add_test(tc, test_flockdn_readback_mismatch);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = kontron_tgl_spi_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
