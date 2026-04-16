/* unit-spi-flash.c
 *
 * Unit tests for spi_flash.c.
 *
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

#define SPI_FLASH

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "spi_flash.c"

enum spi_phase {
    SPI_PHASE_IDLE = 0,
    SPI_PHASE_CMD,
    SPI_PHASE_ADDR,
    SPI_PHASE_DATA,
    SPI_PHASE_OTHER
};

static int cs_asserted;
static enum spi_phase phase;
static uint8_t current_cmd;
static int addr_bytes;
static uint32_t current_addr;
static uint32_t addr_accum;
static int data_writes;
static int expected_data_writes;
static int chip_erase_count;
static int sector_erase_count;
static uint32_t last_sector_erase_addr;
static int rdsr_reads;
static uint8_t manuf_id;
static uint8_t product_id;
static int read_armed;
static int spi_release_called;

#define MOCK_FLASH_SIZE (4 * 1024)
static uint8_t mock_flash[MOCK_FLASH_SIZE];

static void reset_spi_mock(int expected_len)
{
    cs_asserted = 0;
    phase = SPI_PHASE_IDLE;
    current_cmd = 0;
    addr_bytes = 0;
    current_addr = 0;
    addr_accum = 0;
    data_writes = 0;
    expected_data_writes = expected_len;
    chip_erase_count = 0;
    sector_erase_count = 0;
    last_sector_erase_addr = 0;
    rdsr_reads = 0;
    manuf_id = 0xEF;
    product_id = 0x00;
    read_armed = 0;
    spi_release_called = 0;
    memset(mock_flash, 0xFF, sizeof(mock_flash));
}

void spi_init(int polarity, int phase_in)
{
    (void)polarity;
    (void)phase_in;
}

void spi_release(void)
{
    spi_release_called++;
}

void spi_cs_on(uint32_t base, int pin)
{
    (void)base;
    (void)pin;
    cs_asserted = 1;
    phase = SPI_PHASE_CMD;
    current_cmd = 0;
    addr_bytes = 0;
    addr_accum = 0;
    current_addr = 0;
    rdsr_reads = 0;
    read_armed = 0;
}

void spi_cs_off(uint32_t base, int pin)
{
    (void)base;
    (void)pin;
    cs_asserted = 0;
    phase = SPI_PHASE_IDLE;
    current_cmd = 0;
    addr_bytes = 0;
}

void spi_write(const char byte)
{
    ck_assert_msg(cs_asserted, "SPI write without CS asserted");

    if (phase == SPI_PHASE_CMD) {
        current_cmd = (uint8_t)byte;
        if (current_cmd == BYTE_WRITE || current_cmd == BYTE_READ ||
                current_cmd == SECTOR_ERASE) {
            phase = SPI_PHASE_ADDR;
            addr_bytes = 0;
            addr_accum = 0;
            return;
        }
        if (current_cmd == CHIP_ERASE) {
            chip_erase_count++;
        }
        phase = SPI_PHASE_OTHER;
        return;
    }

    if (phase == SPI_PHASE_ADDR) {
        addr_accum = (addr_accum << 8) | (uint8_t)byte;
        addr_bytes++;
        if (addr_bytes >= 3) {
            current_addr = addr_accum;
            if (current_cmd == SECTOR_ERASE) {
                sector_erase_count++;
                last_sector_erase_addr = current_addr;
                phase = SPI_PHASE_OTHER;
                return;
            }
            phase = SPI_PHASE_DATA;
        }
        return;
    }

    if (phase == SPI_PHASE_DATA) {
        if (current_cmd == BYTE_WRITE) {
            ck_assert_msg(data_writes < expected_data_writes,
                "SPI write exceeded expected data length");
            ck_assert_msg(current_addr < MOCK_FLASH_SIZE,
                "SPI write exceeded mock flash");
            mock_flash[current_addr++] = (uint8_t)byte;
            data_writes++;
        } else if (current_cmd == BYTE_READ) {
            read_armed = 1;
        }
        return;
    }
}

uint8_t spi_read(void)
{
    if (!cs_asserted) {
        return 0;
    }

    if (phase == SPI_PHASE_OTHER && current_cmd == MDID) {
        rdsr_reads++;
        if (rdsr_reads == 1) {
            return 0;
        }
        if (rdsr_reads == 2) {
            return manuf_id;
        }
        return product_id;
    }

    if (phase == SPI_PHASE_OTHER && current_cmd == RDSR) {
        rdsr_reads++;
        if (rdsr_reads == 1) {
            return 0;
        }
        return ST_WEL;
    }

    if (phase == SPI_PHASE_DATA && current_cmd == BYTE_READ && read_armed) {
        read_armed = 0;
        ck_assert_msg(current_addr < MOCK_FLASH_SIZE,
            "SPI read exceeded mock flash");
        return mock_flash[current_addr++];
    }

    return 0;
}

START_TEST(test_write_page_midpage_short_len)
{
    uint8_t buf[10];
    uint32_t address = 0x80;
    int ret;

    memset(buf, 0xA5, sizeof(buf));
    reset_spi_mock(sizeof(buf));

    ret = spi_flash_write(address, buf, sizeof(buf));

    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(data_writes, (int)sizeof(buf));
}
END_TEST

START_TEST(test_write_page_cross_page)
{
    uint8_t buf[32];
    uint32_t address = SPI_FLASH_PAGE_SIZE - 16;
    int ret;

    memset(buf, 0x3C, sizeof(buf));
    reset_spi_mock(sizeof(buf));

    ret = spi_flash_write(address, buf, sizeof(buf));

    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(data_writes, (int)sizeof(buf));
    ck_assert_int_eq(0, memcmp(&mock_flash[address], buf, sizeof(buf)));
}
END_TEST

START_TEST(test_write_page_exact_to_boundary)
{
    uint8_t buf[128];
    uint32_t address = 0x80;
    int ret;

    memset(buf, 0x5A, sizeof(buf));
    reset_spi_mock(sizeof(buf));

    ret = spi_flash_write(address, buf, sizeof(buf));

    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(data_writes, (int)sizeof(buf));
    ck_assert_int_eq(0, memcmp(&mock_flash[address], buf, sizeof(buf)));
}
END_TEST

START_TEST(test_write_len_zero_returns_error)
{
    uint8_t buf[4];
    int ret;

    memset(buf, 0x00, sizeof(buf));
    reset_spi_mock(0);

    ret = spi_flash_write(0x00, buf, 0);

    ck_assert_int_eq(ret, -1);
    ck_assert_int_eq(data_writes, 0);
}
END_TEST

START_TEST(test_write_singlebyte_mode)
{
    uint8_t buf[3] = { 0x11, 0x22, 0x33 };
    uint32_t address = 0x10;
    int ret;

    reset_spi_mock(sizeof(buf));
    manuf_id = 0xBF;
    product_id = 0x01;

    spi_flash_probe();
    ret = spi_flash_write(address, buf, sizeof(buf));

    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(0, memcmp(&mock_flash[address], buf, sizeof(buf)));
}
END_TEST

START_TEST(test_read_basic)
{
    uint8_t out[16];
    uint8_t expected[16];
    uint32_t address = 0x40;
    int ret;
    int i;

    reset_spi_mock(0);
    for (i = 0; i < (int)sizeof(expected); i++) {
        expected[i] = (uint8_t)(0xA0 + i);
        mock_flash[address + i] = expected[i];
    }

    ret = spi_flash_read(address, out, sizeof(out));

    ck_assert_int_eq(ret, (int)sizeof(out));
    ck_assert_int_eq(0, memcmp(out, expected, sizeof(out)));
}
END_TEST

START_TEST(test_sector_erase_aligns_address)
{
    uint32_t address = 0x1234;
    uint32_t expected_addr = address & ~(SPI_FLASH_SECTOR_SIZE - 1);
    int ret;

    reset_spi_mock(0);

    ret = spi_flash_sector_erase(address);

    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(sector_erase_count, 1);
    ck_assert_int_eq(last_sector_erase_addr, expected_addr);
}
END_TEST

START_TEST(test_chip_erase_command)
{
    int ret;

    reset_spi_mock(0);
    ret = spi_flash_chip_erase();

    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(chip_erase_count, 1);
}
END_TEST

START_TEST(test_spi_flash_release_calls_spi_release)
{
    reset_spi_mock(0);

    spi_flash_release();

    ck_assert_int_eq(spi_release_called, 1);
}
END_TEST

Suite *spi_flash_suite(void)
{
    Suite *s = suite_create("SPI Flash");
    TCase *tcase_write = tcase_create("Write");
    TCase *tcase_read = tcase_create("Read");
    TCase *tcase_erase = tcase_create("Erase");
    TCase *tcase_errors = tcase_create("Errors");
    TCase *tcase_misc = tcase_create("Misc");

    tcase_add_test(tcase_write, test_write_page_midpage_short_len);
    tcase_add_test(tcase_write, test_write_page_cross_page);
    tcase_add_test(tcase_write, test_write_page_exact_to_boundary);
    tcase_add_test(tcase_write, test_write_singlebyte_mode);
    tcase_add_test(tcase_read, test_read_basic);
    tcase_add_test(tcase_erase, test_sector_erase_aligns_address);
    tcase_add_test(tcase_erase, test_chip_erase_command);
    tcase_add_test(tcase_errors, test_write_len_zero_returns_error);
    tcase_add_test(tcase_misc, test_spi_flash_release_calls_spi_release);

    suite_add_tcase(s, tcase_write);
    suite_add_tcase(s, tcase_read);
    suite_add_tcase(s, tcase_erase);
    suite_add_tcase(s, tcase_errors);
    suite_add_tcase(s, tcase_misc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = spi_flash_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
