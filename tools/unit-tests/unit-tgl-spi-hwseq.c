/* unit-tgl-spi-hwseq.c
 *
 * Unit tests for the Tiger Lake SPI hardware-sequencing erase/write used to
 * save the FSP memory training data (hal/x86_fsp_tgl.c, extracted by the
 * Makefile): BIOS-region address translation, 64-byte cycle splitting, the
 * BIOS_CNTL write-enable handling and the error paths, against a model of
 * the SPI controller registers and a RAM-backed flash window.
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

#include <check.h>
#include <pci.h>
#include <stdint.h>
#include <string.h>

#define MMIO_BASE 0xFED40000UL
#define SPI_BIOS_CNTL_EXPECTED 0xdc

#include "kontron_spi_extract.h"

/* BIOS region: linear 0x01400000-0x01FFFFFF, the top 12 MB of 32 MB */
#define FREG1_INIT 0x1FFF1400U
#define REG_HSFSTS 0x04
#define REG_FADDR 0x08
#define REG_FDATA0 0x10
#define REG_FREG1 0x58

/* Modeled flash window: linear 0x019E0000 + 128 KB */
#define WIN_LINEAR 0x019E0000U
#define WIN_SIZE 0x20000U
#define WIN_MMIO 0xFF9E0000U

static uint32_t g_mmio[64];
static uint32_t g_pci_cmd;
static uint8_t g_bios_cntl;
static int g_wpd_stuck_low;
static int g_fail_cycle;
static int g_never_done;
static uint8_t g_flash[WIN_SIZE];
static struct {
    uint32_t cycle, faddr, len;
} g_log[64];
static int g_ncycles;

static void sim_reset(void)
{
    memset(g_mmio, 0, sizeof(g_mmio));
    g_mmio[REG_FREG1 / 4] = FREG1_INIT;
    g_pci_cmd = 0x0001;
    g_bios_cntl = 0;
    g_wpd_stuck_low = g_fail_cycle = g_never_done = 0;
    g_ncycles = 0;
    memset(g_flash, 0x5a, sizeof(g_flash));
}

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off)
{
    (void)bus; (void)dev; (void)fun;
    if (off == PCI_BAR0_OFFSET)
        return (uint32_t)MMIO_BASE;
    if (off == PCI_COMMAND_OFFSET)
        return g_pci_cmd;
    return 0;
}

void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off,
                        uint32_t val)
{
    (void)bus; (void)dev; (void)fun;
    if (off == PCI_COMMAND_OFFSET)
        g_pci_cmd = val;
}

uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off)
{
    (void)bus; (void)dev; (void)fun;
    ck_assert_uint_eq(off, SPI_BIOS_CNTL_EXPECTED);
    return g_bios_cntl;
}

void pci_config_write8(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off,
                       uint8_t val)
{
    (void)bus; (void)dev; (void)fun;
    ck_assert_uint_eq(off, SPI_BIOS_CNTL_EXPECTED);
    g_bios_cntl = g_wpd_stuck_low ? (uint8_t)(val & ~1U) : val;
}

/* A TSC that advances on every read, one tick per ms */
static uint64_t g_tsc;

uint64_t tgl_rdtsc(void)
{
    return ++g_tsc;
}

uint32_t tgl_tsc_per_ms(void)
{
    return 1;
}

static void run_cycle(uint32_t ctl)
{
    uint32_t cycle = (ctl >> 17) & 0xf;
    uint32_t len = ((ctl >> 24) & 0x3f) + 1;
    uint32_t faddr = g_mmio[REG_FADDR / 4];
    uint32_t i;

    /* a cycle only runs with flash writes enabled */
    ck_assert_uint_eq(g_bios_cntl & 1U, 1U);
    if (g_ncycles < (int)(sizeof(g_log) / sizeof(g_log[0]))) {
        g_log[g_ncycles].cycle = cycle;
        g_log[g_ncycles].faddr = faddr;
        g_log[g_ncycles].len = len;
    }
    g_ncycles++;
    if (faddr >= WIN_LINEAR && faddr < WIN_LINEAR + WIN_SIZE) {
        if (cycle == 3)
            memset(g_flash + (faddr - WIN_LINEAR), 0xff, 0x1000);
        else if (cycle == 2)
            for (i = 0; i < len; i++)
                g_flash[faddr - WIN_LINEAR + i] =
                    ((uint8_t *)&g_mmio[REG_FDATA0 / 4])[i];
    }
    if (g_never_done)
        return;
    g_mmio[REG_HSFSTS / 4] |= (g_fail_cycle ? 0x2U : 0x1U);
}

static void mmio_write32(uintptr_t address, uint32_t value)
{
    uint32_t off = (uint32_t)(address - MMIO_BASE);

    if (off == REG_HSFSTS) {
        /* status bits are write-1-to-clear */
        g_mmio[REG_HSFSTS / 4] &= ~(value & 0x7U);
        if (value & (1U << 16))
            run_cycle(value);
        return;
    }
    g_mmio[off / 4] = value;
}

static uint32_t mmio_read32(uintptr_t address)
{
    return g_mmio[(address - MMIO_BASE) / 4];
}

#include "tgl_spi_hwseq_extract.h"

START_TEST(test_linear_mapping)
{
    sim_reset();
    ck_assert_int_eq(tgl_spi_linear(MMIO_BASE, WIN_MMIO, 0x1000),
                     WIN_LINEAR);
    ck_assert_int_eq(tgl_spi_linear(MMIO_BASE, 0xFFFFFFFFU, 1),
                     0x01FFFFFF);
    ck_assert_int_eq(tgl_spi_linear(MMIO_BASE, 0xFF400000U, 0x1000),
                     0x01400000);
    /* below the BIOS region, past its top, or empty */
    ck_assert_int_eq(tgl_spi_linear(MMIO_BASE, 0xFF3FF000U, 0x1000), -1);
    ck_assert_int_eq(tgl_spi_linear(MMIO_BASE, 0xFFFFF000U, 0x2000), -1);
    ck_assert_int_eq(tgl_spi_linear(MMIO_BASE, WIN_MMIO, 0), -1);
}
END_TEST

START_TEST(test_erase_sectors)
{
    sim_reset();
    ck_assert_int_eq(tgl_spi_flash_erase(WIN_MMIO, 0x2000), 0);
    ck_assert_int_eq(g_ncycles, 2);
    ck_assert_uint_eq(g_log[0].cycle, 3);
    ck_assert_uint_eq(g_log[0].faddr, WIN_LINEAR);
    ck_assert_uint_eq(g_log[1].faddr, WIN_LINEAR + 0x1000);
    ck_assert_uint_eq(g_flash[0], 0xff);
    ck_assert_uint_eq(g_flash[0x1fff], 0xff);
    ck_assert_uint_eq(g_flash[0x2000], 0x5a);
    /* write enable dropped and PCI command restored afterwards */
    ck_assert_uint_eq(g_bios_cntl & 1U, 0);
    ck_assert_uint_eq(g_pci_cmd, 0x0001);
}
END_TEST

START_TEST(test_erase_rejects_unaligned)
{
    sim_reset();
    ck_assert_int_eq(tgl_spi_flash_erase(WIN_MMIO + 0x10, 0x1000), -1);
    ck_assert_int_eq(tgl_spi_flash_erase(WIN_MMIO, 0x800), -1);
    ck_assert_int_eq(g_ncycles, 0);
}
END_TEST

START_TEST(test_write_splits_on_64_byte_boundaries)
{
    uint8_t buf[150];
    uint32_t i;

    sim_reset();
    for (i = 0; i < sizeof(buf); i++)
        buf[i] = (uint8_t)(i + 1);
    ck_assert_int_eq(tgl_spi_flash_write(WIN_MMIO + 0x10, buf, sizeof(buf)),
                     0);
    ck_assert_int_eq(g_ncycles, 3);
    ck_assert_uint_eq(g_log[0].faddr, WIN_LINEAR + 0x10);
    ck_assert_uint_eq(g_log[0].len, 48);
    ck_assert_uint_eq(g_log[1].faddr, WIN_LINEAR + 0x40);
    ck_assert_uint_eq(g_log[1].len, 64);
    ck_assert_uint_eq(g_log[2].faddr, WIN_LINEAR + 0x80);
    ck_assert_uint_eq(g_log[2].len, 38);
    ck_assert_uint_eq(g_log[0].cycle, 2);
    ck_assert_int_eq(memcmp(g_flash + 0x10, buf, sizeof(buf)), 0);
    ck_assert_uint_eq(g_flash[0x10 + sizeof(buf)], 0x5a);
    ck_assert_uint_eq(g_bios_cntl & 1U, 0);
}
END_TEST

START_TEST(test_write_outside_bios_region)
{
    uint8_t buf[4] = { 0 };

    sim_reset();
    ck_assert_int_eq(tgl_spi_flash_write(0xFF000000U, buf, sizeof(buf)), -1);
    ck_assert_int_eq(g_ncycles, 0);
    ck_assert_uint_eq(g_bios_cntl & 1U, 0);
}
END_TEST

START_TEST(test_cycle_error_and_timeout)
{
    uint8_t buf[8] = { 0 };

    sim_reset();
    g_fail_cycle = 1;
    ck_assert_int_eq(tgl_spi_flash_write(WIN_MMIO, buf, sizeof(buf)), -1);

    sim_reset();
    g_never_done = 1;
    ck_assert_int_eq(tgl_spi_flash_erase(WIN_MMIO, 0x1000), -1);
}
END_TEST

START_TEST(test_write_enable_refused)
{
    uint8_t buf[8] = { 0 };

    sim_reset();
    g_wpd_stuck_low = 1;
    ck_assert_int_eq(tgl_spi_flash_write(WIN_MMIO, buf, sizeof(buf)), -1);
    ck_assert_int_eq(tgl_spi_flash_erase(WIN_MMIO, 0x1000), -1);
    ck_assert_int_eq(g_ncycles, 0);
}
END_TEST

static Suite *hwseq_suite(void)
{
    Suite *s = suite_create("tgl-spi-hwseq");
    TCase *tc = tcase_create("tgl-spi-hwseq");

    tcase_add_test(tc, test_linear_mapping);
    tcase_add_test(tc, test_erase_sectors);
    tcase_add_test(tc, test_erase_rejects_unaligned);
    tcase_add_test(tc, test_write_splits_on_64_byte_boundaries);
    tcase_add_test(tc, test_write_outside_bios_region);
    tcase_add_test(tc, test_cycle_error_and_timeout);
    tcase_add_test(tc, test_write_enable_refused);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = hwseq_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
