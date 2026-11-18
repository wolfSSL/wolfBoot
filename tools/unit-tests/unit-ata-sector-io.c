/* unit-ata-sector-io.c
 *
 * Unit tests for the AHCI sector read and write commands in src/x86/ata.c:
 * a write at or past LBA 2^24 uses the 48-bit WRITE DMA EXT command, so its
 * upper LBA bits reach the drive, and a command that ends in a task-file error is
 * reported to the caller, including the read or write-back of a partial
 * sector.
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
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <x86/ahci.h>
#include <x86/ata.h>

#define TEST_AHCI_BASE 0x10000

/* Port registers: every command completes at once, except the
 * mock_fail_issue-th one (counting from 1), which stays pending in CI while
 * IS reports a task-file error. */
static int mock_fail_issue;
static int mock_issues;
static int mock_port_error;
static uint32_t mock_ci;

uint32_t mmio_read32(uintptr_t address)
{
    if (address == AHCI_PxCI(TEST_AHCI_BASE, 0))
        return mock_ci;
    if (address == AHCI_PxIS(TEST_AHCI_BASE, 0))
        return mock_port_error ? AHCI_PORT_IS_TFES : 0;
    return 0;
}

void mmio_write32(uintptr_t address, uint32_t value)
{
    if (address == AHCI_PxCI(TEST_AHCI_BASE, 0)) {
        if (++mock_issues == mock_fail_issue) {
            mock_port_error = 1;
            mock_ci = value;
        }
        else {
            mock_port_error = 0;
            mock_ci = 0;
        }
    }
}

void panic(void)
{
    ck_abort_msg("panic!");
}

#include "../../src/x86/ata.c"

/* struct ata_drive keeps 32-bit DMA addresses, so the command list and
 * table must sit below 4 GB on a 64-bit test host. */
static uint8_t *clb_mem;
static uint8_t *ctable_mem;
static uint8_t sector[1024];

static void setup(void)
{
    mock_fail_issue = 0;
    mock_issues = 0;
    mock_port_error = 0;
    mock_ci = 0;
    clb_mem = mmap(NULL, sizeof(struct hba_cmd_header) * 32,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    ck_assert_ptr_ne(clb_mem, MAP_FAILED);
    ctable_mem = mmap(NULL, sizeof(struct hba_cmd_table) * 32,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    ck_assert_ptr_ne(ctable_mem, MAP_FAILED);

    memset(&ATA_Drv[0], 0, sizeof(ATA_Drv[0]));
    ATA_Drv[0].ahci_base = TEST_AHCI_BASE;
    ATA_Drv[0].ahci_port = 0;
    ATA_Drv[0].clb_port = (uint32_t)(uintptr_t)clb_mem;
    ATA_Drv[0].ctable_port = (uint32_t)(uintptr_t)ctable_mem;
    ATA_Drv[0].sector_size_shift = 9;
    ATA_Drv[0].cached = CACHE_INVALID;
    ata_drive_count = 0;
}

static void teardown(void)
{
    munmap(clb_mem, sizeof(struct hba_cmd_header) * 32);
    munmap(ctable_mem, sizeof(struct hba_cmd_table) * 32);
}

static const struct fis_reg_h2d *issued_fis(void)
{
    const struct hba_cmd_header *cmd = (const struct hba_cmd_header *)clb_mem;
    const struct hba_cmd_table *tbl =
        (const struct hba_cmd_table *)(uintptr_t)cmd->ctba;

    return (const struct fis_reg_h2d *)(&tbl->cfis);
}

START_TEST(test_write_past_lba24_uses_48bit_command)
{
    const struct fis_reg_h2d *fis;

    /* LBA 0x10123456: a 28-bit command takes bits 27:24 from the device
     * field (0x40) and the write would land at 0x123456 */
    ck_assert_int_eq(ata_drive_write_sector(0, 0x10123456ULL, 1, sector),
                     512);
    fis = issued_fis();
    ck_assert_uint_eq(fis->command, ATA_CMD_WRITE_DMA_EX);
    ck_assert_uint_eq(fis->lba0, 0x56);
    ck_assert_uint_eq(fis->lba1, 0x34);
    ck_assert_uint_eq(fis->lba2, 0x12);
    ck_assert_uint_eq(fis->lba3, 0x10);
    ck_assert_uint_eq(fis->count, 1);
}
END_TEST

START_TEST(test_read_uses_48bit_command)
{
    ck_assert_int_eq(ata_drive_read_sector(0, 0x10123456ULL, 1, sector), 512);
    ck_assert_uint_eq(issued_fis()->command, ATA_CMD_READ_DMA_EX);
    ck_assert_uint_eq(issued_fis()->lba3, 0x10);
}
END_TEST

START_TEST(test_port_error_is_reported)
{
    mock_fail_issue = 1;
    ck_assert_int_lt(ata_drive_write_sector(0, 100, 1, sector), 0);

    mock_issues = 0;
    ck_assert_int_lt(ata_drive_read_sector(0, 100, 1, sector), 0);
}
END_TEST

START_TEST(test_partial_sector_write_error_is_reported)
{
    /* a partial sector is read into the cache (issue 1) and written back
     * (issue 2); a failed write-back fails the call */
    mock_fail_issue = 2;
    ck_assert_int_lt(ata_drive_write(0, 512 * 100 + 16, 32, sector), 0);
    ck_assert(ATA_Drv[0].cached == CACHE_INVALID);

    /* a trailing partial sector after whole sectors: issue 1 writes the
     * whole sector, 2 reads the tail sector, 3 writes it back */
    mock_issues = 0;
    mock_fail_issue = 3;
    ck_assert_int_lt(ata_drive_write(0, 512 * 100, 512 + 32, sector), 0);

    mock_issues = 0;
    mock_fail_issue = 0;
    ck_assert_int_eq(ata_drive_write(0, 512 * 100 + 16, 32, sector), 32);
}
END_TEST

START_TEST(test_partial_sector_read_error_is_reported)
{
    /* a sector is cached, then a read of one whole sector (issue 1) plus a
     * trailing partial sector (issue 2) fails on the partial sector */
    ck_assert_int_eq(ata_drive_read(0, 512 * 50 + 16, 32, sector), 32);
    mock_issues = 0;
    mock_fail_issue = 2;
    ck_assert_int_lt(ata_drive_read(0, 512 * 100, 512 + 32, sector), 0);

    /* and a leading partial sector */
    mock_issues = 0;
    mock_fail_issue = 1;
    ck_assert_int_lt(ata_drive_read(0, 512 * 200 + 16, 32, sector), 0);
}
END_TEST

static Suite *ata_sector_io_suite(void)
{
    Suite *s = suite_create("ata-sector-io");
    TCase *tc = tcase_create("ata-sector-io");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_write_past_lba24_uses_48bit_command);
    tcase_add_test(tc, test_read_uses_48bit_command);
    tcase_add_test(tc, test_port_error_is_reported);
    tcase_add_test(tc, test_partial_sector_write_error_is_reported);
    tcase_add_test(tc, test_partial_sector_read_error_is_reported);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = ata_sector_io_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
