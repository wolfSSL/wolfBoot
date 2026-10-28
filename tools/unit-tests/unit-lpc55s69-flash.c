/* unit-lpc55s69-flash.c
 *
 * Unit tests for the LPC55S69 flash driver in hal/lpc55s69.c, against a model
 * of the ROM flash API with the part's 512-byte ECC pages.
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

/* lpc55s69.config layout, in place of target.h */
#define H_TARGETS_TARGET_
#define WOLFBOOT_FIXED_PARTITIONS
#define WOLFBOOT_SECTOR_SIZE                 0x200
#define WOLFBOOT_PARTITION_SIZE              0x30000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x10000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x40000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x70000
#define ARCH_FLASH_OFFSET                    0x0
/* Bootloader side of the HAL: hal_init also fills the bootloader region */
#define __WOLFBOOT
#define WOLFBOOT_HASH_SHA256
#define WOLFBOOT_SIGN_ECC256

#include "fsl_common.h"

const uint8_t *mock_flash_ptr(uint32_t address);
#define LPC55S69_FLASH_PTR(a) mock_flash_ptr(a)
int mock_word_read(uint32_t offset, uint32_t *data);
#define LPC55S69_FLASH_WORD_READ(off, data) mock_word_read(off, data)

#include "../../hal/lpc55s69.c"

#define PAGE             LPC55S69_FLASH_PAGE_SIZE
#define FLASH_PAGES      (LPC55S69_FLASH_SIZE / PAGE)
#define S_ALIAS          LPC55S69_FLASH_SECURE_ALIAS
#define BOOT             WOLFBOOT_PARTITION_BOOT_ADDRESS
#define MOCK_MAX_OPS     64

uint32_t mock_primask;
int mock_barriers;
NVIC_Type mock_nvic;
SysTick_Type mock_systick;

static uint8_t mock_flash[LPC55S69_FLASH_SIZE];
/* 0: erased and never programmed, so a read faults on the ECC check */
static uint8_t programmed[FLASH_PAGES];
/* Bit n set: word n of the page reads with ECC_ERR (bad: every time,
 * transient: once); garbage: and the data returned is not what is stored */
static uint32_t ecc_bad[FLASH_PAGES];
static uint32_t ecc_transient[FLASH_PAGES];
static uint32_t ecc_garbage[FLASH_PAGES];
static int word_read_calls;
static int word_read_ret;
static status_t init_ret;
static int init_calls;
static int erase_calls;
static int program_calls;
static int ptr_calls;
static int fail_erase_at;
static int fail_program_at;
static int irq_enabled_during_rom;
static char op_kind[MOCK_MAX_OPS];
static uint32_t op_off[MOCK_MAX_OPS];
static int n_ops;

static void rom_call(char kind, uint32_t start, uint32_t len)
{
    ck_assert_uint_eq(start % PAGE, 0);
    ck_assert_uint_eq(len, PAGE);
    ck_assert_uint_lt(start, LPC55S69_FLASH_SIZE);
    if (mock_primask == 0U)
        irq_enabled_during_rom = 1;
    if ((kind != 'V') && (n_ops < MOCK_MAX_OPS)) {
        op_kind[n_ops] = kind;
        op_off[n_ops] = start;
        n_ops++;
    }
}

status_t FLASH_Init(flash_config_t *config)
{
    init_calls++;
    config->PFlashTotalSize = LPC55S69_FLASH_SIZE;
    config->PFlashPageSize = PAGE;
    return init_ret;
}

status_t FFR_Init(flash_config_t *config)
{
    (void)config;
    return kStatus_Success;
}

status_t FLASH_Erase(flash_config_t *config, uint32_t start,
        uint32_t lengthInBytes, uint32_t key)
{
    ck_assert_ptr_eq(config, &pflash);
    ck_assert_uint_eq(key, kFLASH_ApiEraseKey);
    rom_call('E', start, lengthInBytes);
    if (++erase_calls == fail_erase_at)
        return kStatus_Fail;
    memset(&mock_flash[start], 0xFF, PAGE);
    programmed[start / PAGE] = 0;
    ecc_bad[start / PAGE] = ecc_transient[start / PAGE] = 0;
    ecc_garbage[start / PAGE] = 0;
    return kStatus_FLASH_Success;
}

status_t FLASH_Program(flash_config_t *config, uint32_t start,
        const uint8_t *src, uint32_t lengthInBytes)
{
    ck_assert_ptr_eq(config, &pflash);
    rom_call('P', start, lengthInBytes);
    if (++program_calls == fail_program_at)
        return kStatus_Fail;
    ck_assert_msg(!programmed[start / PAGE],
            "page 0x%x programmed twice without an erase", start);
    memcpy(&mock_flash[start], src, PAGE);
    programmed[start / PAGE] = 1;
    ecc_bad[start / PAGE] = ecc_transient[start / PAGE] = 0;
    ecc_garbage[start / PAGE] = 0;
    return kStatus_FLASH_Success;
}

/* READ_SINGLE_WORD through the controller: a blank or flagged word reports
 * ECC_ERR instead of faulting */
int mock_word_read(uint32_t offset, uint32_t *data)
{
    uint32_t page = offset / PAGE;
    uint32_t bit = 1U << ((offset % PAGE) / LPC55S69_FLASH_WORD_SIZE);
    uint32_t k;
    int st = 0;

    ck_assert_uint_eq(offset % LPC55S69_FLASH_WORD_SIZE, 0);
    ck_assert_uint_lt(offset, LPC55S69_FLASH_SIZE);
    if (mock_primask == 0U)
        irq_enabled_during_rom = 1;
    word_read_calls++;
    memcpy(data, &mock_flash[offset], LPC55S69_FLASH_WORD_SIZE);
    if (word_read_ret != 0)
        return word_read_ret;
    if (!programmed[page] || (ecc_bad[page] & bit) ||
            (ecc_transient[page] & bit)) {
        st = FLASH_INT_STATUS_ECC_ERR_MASK;
        ecc_transient[page] &= ~bit;
    }
    if (ecc_garbage[page] & bit) {
        for (k = 0; k < 4; k++)
            data[k] = 0xBAD00000U + (offset / 4) + k;
    }
    return st;
}

status_t FLASH_VerifyErase(flash_config_t *config, uint32_t start,
        uint32_t lengthInBytes)
{
    ck_assert_ptr_eq(config, &pflash);
    rom_call('V', start, lengthInBytes);
    return programmed[start / PAGE] ? kStatus_FLASH_CommandFailure :
        kStatus_FLASH_Success;
}

const uint8_t *mock_flash_ptr(uint32_t address)
{
    uint32_t off = (address >= S_ALIAS) ? address - S_ALIAS : address;

    ptr_calls++;
    ck_assert_uint_lt(off, LPC55S69_FLASH_SIZE);
    ck_assert_msg(programmed[off / PAGE],
            "read of erased page 0x%x faults (ECC)", off);
    ck_assert_msg(ecc_bad[off / PAGE] == 0,
            "read of ECC-corrupt page 0x%x faults", off);
    return &mock_flash[off];
}

void BOARD_BootClockFROHF96M(void)
{
}

int wolfCrypt_Init(void)
{
    return 0;
}

static void put_page(uint32_t off, uint8_t fill)
{
    memset(&mock_flash[off], fill, PAGE);
    programmed[off / PAGE] = 1;
}

static void wipe_page(uint32_t off)
{
    memset(&mock_flash[off], 0xFF, PAGE);
    programmed[off / PAGE] = 0;
}

/* A page whose program was cut: every word fails ECC and reads as garbage */
static void torn_page(uint32_t off, uint8_t fill)
{
    put_page(off, fill);
    ecc_bad[off / PAGE] = 0xFFFFFFFFU;
    ecc_garbage[off / PAGE] = 0xFFFFFFFFU;
}

/* A correctable error: the word is flagged, the data comes back intact */
static void weak_page(uint32_t off, uint32_t word)
{
    ecc_bad[off / PAGE] |= 1U << word;
}

static void reset_counters(void)
{
    erase_calls = program_calls = ptr_calls = 0;
    fail_erase_at = fail_program_at = 0;
    irq_enabled_during_rom = 0;
    word_read_calls = 0;
    word_read_ret = 0;
    n_ops = 0;
}

static void setup(void)
{
    memset(mock_flash, 0xFF, sizeof(mock_flash));
    memset(programmed, 0, sizeof(programmed));
    memset(ecc_bad, 0, sizeof(ecc_bad));
    memset(ecc_transient, 0, sizeof(ecc_transient));
    memset(ecc_garbage, 0, sizeof(ecc_garbage));
    init_ret = kStatus_FLASH_Success;
    init_calls = 0;
    flash_ready = 0;
    mock_primask = 0;
    reset_counters();
    hal_init();
    ck_assert_int_eq(erase_calls, 0);
    reset_counters();
}

START_TEST(test_init_fills_pages_wolfboot_reads)
{
    uint32_t off;

    ck_assert_int_eq(flash_ready, 1);
    for (off = 0; off < BOOT; off += PAGE)
        ck_assert_msg(programmed[off / PAGE], "bootloader page 0x%x", off);
    for (off = BOOT; off < BOOT + WOLFBOOT_PARTITION_SIZE; off += PAGE)
        ck_assert_msg(programmed[off / PAGE], "boot page 0x%x", off);
    for (off = WOLFBOOT_PARTITION_UPDATE_ADDRESS;
            off < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE;
            off += PAGE)
        ck_assert_msg(programmed[off / PAGE], "update page 0x%x", off);
    ck_assert(programmed[WOLFBOOT_PARTITION_SWAP_ADDRESS / PAGE]);
    for (off = 0; off < WOLFBOOT_PARTITION_SWAP_ADDRESS + PAGE; off += PAGE)
        ck_assert_uint_eq(mock_flash[off], 0xFF);
    /* Nothing else, such as the HWPUF record, is touched */
    ck_assert(!programmed[(WOLFBOOT_PARTITION_SWAP_ADDRESS + PAGE) / PAGE]);
    ck_assert(!programmed[0x80000 / PAGE]);
}
END_TEST

START_TEST(test_init_keeps_programmed_pages)
{
    put_page(BOOT, 0x5A);
    flash_ready = 0;
    hal_init();
    ck_assert_int_eq(erase_calls, 0);
    ck_assert_int_eq(program_calls, 0);
    ck_assert_uint_eq(mock_flash[BOOT + 7], 0x5A);
}
END_TEST

START_TEST(test_init_fill_stops_at_failure)
{
    memset(programmed, 0, sizeof(programmed));
    fail_program_at = 1;
    flash_ready = 0;
    hal_init();
    ck_assert_int_eq(program_calls, 1);
}
END_TEST

START_TEST(test_init_failure_then_retry)
{
    uint8_t b = 0x11;

    init_ret = kStatus_Fail;
    flash_ready = 0;
    hal_init();
    ck_assert_int_eq(flash_ready, 0);
    ck_assert_int_eq(hal_flash_write(BOOT, &b, 1), -1);
    ck_assert_int_eq(hal_flash_erase(BOOT, PAGE), -1);
    ck_assert_int_eq(n_ops, 0);

    init_ret = kStatus_FLASH_Success;
    hal_init();
    ck_assert_int_eq(flash_ready, 1);
    ck_assert_int_eq(hal_flash_write(S_ALIAS + BOOT, &b, 1), 0);
}
END_TEST

START_TEST(test_init_repairs_torn_page)
{
    uint32_t trailer = WOLFBOOT_PARTITION_UPDATE_ADDRESS +
        WOLFBOOT_PARTITION_SIZE - PAGE;
    uint32_t i;

    torn_page(BOOT, 0x5A);
    torn_page(trailer, 0x33);
    flash_ready = 0;
    hal_init();
    ck_assert_int_eq(flash_check, 1);
    ck_assert_int_eq(n_ops, 4);
    ck_assert_int_eq(op_kind[0], 'E');
    ck_assert_uint_eq(op_off[0], BOOT);
    ck_assert_int_eq(op_kind[1], 'P');
    ck_assert_uint_eq(op_off[1], BOOT);
    ck_assert_int_eq(op_kind[2], 'E');
    ck_assert_uint_eq(op_off[2], trailer);
    ck_assert_int_eq(op_kind[3], 'P');
    ck_assert_uint_eq(op_off[3], trailer);
    for (i = 0; i < PAGE; i++) {
        ck_assert_uint_eq(mock_flash[BOOT + i], 0xFF);
        ck_assert_uint_eq(mock_flash[trailer + i], 0xFF);
    }
    ck_assert_uint_eq(ecc_bad[BOOT / PAGE], 0);
    ck_assert_uint_eq(mock_flash_ptr(S_ALIAS + BOOT)[0], 0xFF);
}
END_TEST

START_TEST(test_init_keeps_correctable_page)
{
    uint32_t i;

    put_page(BOOT + PAGE, 0xA5);
    weak_page(BOOT + PAGE, 7);
    flash_ready = 0;
    hal_init();
    ck_assert_int_eq(n_ops, 2);
    ck_assert_int_eq(op_kind[0], 'E');
    ck_assert_uint_eq(op_off[0], BOOT + PAGE);
    ck_assert_int_eq(op_kind[1], 'P');
    ck_assert_uint_eq(op_off[1], BOOT + PAGE);
    for (i = 0; i < PAGE; i++)
        ck_assert_uint_eq(mock_flash[BOOT + PAGE + i], 0xA5);
    ck_assert_uint_eq(ecc_bad[(BOOT + PAGE) / PAGE], 0);
}
END_TEST

START_TEST(test_init_single_bad_word_keeps_neighbours)
{
    uint32_t i;

    put_page(BOOT, 0xA5);
    weak_page(BOOT, 3);
    ecc_garbage[BOOT / PAGE] = 1U << 3;
    flash_ready = 0;
    hal_init();
    ck_assert_int_eq(n_ops, 2);
    for (i = 0; i < PAGE; i++) {
        if (i >= 48 && i < 64)
            ck_assert_uint_ne(mock_flash[BOOT + i], 0xA5);
        else
            ck_assert_uint_eq(mock_flash[BOOT + i], 0xA5);
    }
    ck_assert_uint_eq(ecc_bad[BOOT / PAGE], 0);
}
END_TEST

START_TEST(test_init_two_bad_words_fill_ff)
{
    uint32_t i;

    put_page(BOOT, 0xA5);
    weak_page(BOOT, 0);
    weak_page(BOOT, 31);
    flash_ready = 0;
    hal_init();
    ck_assert_int_eq(n_ops, 2);
    for (i = 0; i < PAGE; i++)
        ck_assert_uint_eq(mock_flash[BOOT + i], 0xFF);
}
END_TEST

START_TEST(test_init_transient_flag_not_repaired)
{
    put_page(BOOT, 0xA5);
    ecc_transient[BOOT / PAGE] = 1U << 5;
    flash_ready = 0;
    hal_init();
    ck_assert_int_eq(n_ops, 0);
    ck_assert_uint_eq(mock_flash[BOOT + 80], 0xA5);
    ck_assert_int_eq(flash_check, 1);
}
END_TEST

START_TEST(test_init_check_unavailable_keeps_pages)
{
    torn_page(BOOT, 0x5A);
    wipe_page(BOOT + PAGE);
    word_read_ret = FLASH_INT_STATUS_ERR_MASK;
    flash_ready = 0;
    hal_init();
    ck_assert_int_eq(flash_check, 0);
    ck_assert_int_eq(n_ops, 1);
    ck_assert_int_eq(op_kind[0], 'P');
    ck_assert_uint_eq(op_off[0], BOOT + PAGE);
    ck_assert_uint_eq(mock_flash[BOOT], 0x5A);
    ck_assert_uint_ne(ecc_bad[BOOT / PAGE], 0);

    word_read_ret = -1;
    flash_ready = 0;
    hal_init();
    ck_assert_int_eq(flash_check, 0);
    ck_assert_int_eq(n_ops, 1);
    ck_assert_uint_eq(mock_flash[BOOT], 0x5A);
}
END_TEST

START_TEST(test_init_selftest_disables_repair)
{
    weak_page(0, 0);
    torn_page(BOOT, 0x5A);
    flash_ready = 0;
    hal_init();
    ck_assert_int_eq(flash_check, 0);
    ck_assert_int_eq(n_ops, 0);
    ck_assert_uint_eq(mock_flash[BOOT], 0x5A);
}
END_TEST

START_TEST(test_init_check_skips_bootloader_region)
{
    torn_page(0x1000, 0x5A);
    flash_ready = 0;
    hal_init();
    ck_assert_int_eq(flash_check, 1);
    ck_assert_int_eq(n_ops, 0);
    ck_assert_uint_eq(mock_flash[0x1000], 0x5A);
}
END_TEST

START_TEST(test_init_repair_failure_stops)
{
    torn_page(BOOT, 0x5A);
    torn_page(BOOT + PAGE, 0x5A);
    fail_erase_at = 1;
    flash_ready = 0;
    hal_init();
    ck_assert_int_eq(erase_calls, 1);
    ck_assert_int_eq(program_calls, 0);
    ck_assert_uint_ne(ecc_bad[BOOT / PAGE], 0);
    ck_assert_uint_ne(ecc_bad[(BOOT + PAGE) / PAGE], 0);
}
END_TEST

START_TEST(test_partial_write_keeps_neighbours)
{
    static const uint8_t data[16] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    uint32_t i;

    put_page(BOOT, 0xA5);
    ck_assert_int_eq(hal_flash_write(S_ALIAS + BOOT + 100, data,
                sizeof(data)), 0);
    for (i = 0; i < PAGE; i++) {
        if (i >= 100 && i < 116)
            ck_assert_uint_eq(mock_flash[BOOT + i], data[i - 100]);
        else
            ck_assert_uint_eq(mock_flash[BOOT + i], 0xA5);
    }
    ck_assert_int_eq(n_ops, 2);
    ck_assert_int_eq(op_kind[0], 'E');
    ck_assert_int_eq(op_kind[1], 'P');
}
END_TEST

START_TEST(test_write_to_blank_page_skips_erase)
{
    uint8_t data[8];
    uint32_t i;

    memset(data, 0x3C, sizeof(data));
    wipe_page(BOOT + PAGE);
    ck_assert_int_eq(hal_flash_write(S_ALIAS + BOOT + PAGE + 8, data,
                sizeof(data)), 0);
    ck_assert_int_eq(erase_calls, 0);
    ck_assert_int_eq(program_calls, 1);
    ck_assert_int_eq(ptr_calls, 0);
    for (i = 0; i < PAGE; i++)
        ck_assert_uint_eq(mock_flash[BOOT + PAGE + i],
                (i >= 8 && i < 16) ? 0x3C : 0xFF);
}
END_TEST

START_TEST(test_write_spanning_pages)
{
    uint8_t data[600];
    uint32_t i;

    for (i = 0; i < sizeof(data); i++)
        data[i] = (uint8_t)i;
    put_page(BOOT, 0x00);
    put_page(BOOT + PAGE, 0x00);
    put_page(BOOT + 2 * PAGE, 0x77);
    ck_assert_int_eq(hal_flash_write(S_ALIAS + BOOT + 400, data,
                sizeof(data)), 0);
    for (i = 0; i < 400; i++)
        ck_assert_uint_eq(mock_flash[BOOT + i], 0x00);
    for (i = 0; i < sizeof(data); i++)
        ck_assert_uint_eq(mock_flash[BOOT + 400 + i], (uint8_t)i);
    for (i = 400 + sizeof(data); i < 2 * PAGE; i++)
        ck_assert_uint_eq(mock_flash[BOOT + i], 0x00);
    ck_assert_uint_eq(mock_flash[BOOT + 2 * PAGE], 0x77);
    ck_assert_int_eq(erase_calls, 2);
    ck_assert_int_eq(program_calls, 2);
}
END_TEST

START_TEST(test_full_page_write_reads_nothing)
{
    uint8_t data[PAGE];

    memset(data, 0x42, sizeof(data));
    put_page(BOOT, 0x99);
    /* The non-secure alias works too; a full page needs no merge read */
    ck_assert_int_eq(hal_flash_write(BOOT, data, sizeof(data)), 0);
    ck_assert_int_eq(ptr_calls, 0);
    ck_assert_uint_eq(mock_flash[BOOT], 0x42);
    ck_assert_uint_eq(mock_flash[BOOT + PAGE - 1], 0x42);
    ck_assert_int_eq(n_ops, 2);
    ck_assert_uint_eq(op_off[0], BOOT);
    ck_assert_uint_eq(op_off[1], BOOT);
}
END_TEST

START_TEST(test_secure_and_nonsecure_alias_match)
{
    uint8_t b = 0xE1;

    ck_assert_int_eq(hal_flash_erase(S_ALIAS + BOOT, PAGE), 0);
    ck_assert_int_eq(hal_flash_erase(BOOT, PAGE), 0);
    ck_assert_uint_eq(op_off[0], BOOT);
    ck_assert_uint_eq(op_off[2], BOOT);
    ck_assert_int_eq(hal_flash_write(S_ALIAS + BOOT + 3, &b, 1), 0);
    ck_assert_uint_eq(mock_flash[BOOT + 3], 0xE1);
}
END_TEST

START_TEST(test_write_rejects_bad_arguments)
{
    uint8_t data[32];

    memset(data, 0, sizeof(data));
    ck_assert_int_eq(hal_flash_write(BOOT, data, 0), 0);
    ck_assert_int_eq(hal_flash_write(BOOT, data, -1), -1);
    ck_assert_int_eq(hal_flash_write(BOOT, NULL, 4), -1);
    ck_assert_int_eq(hal_flash_write(LPC55S69_FLASH_SIZE, data, 4), -1);
    ck_assert_int_eq(hal_flash_write(LPC55S69_FLASH_SIZE - 16, data, 32), -1);
    ck_assert_int_eq(hal_flash_write(S_ALIAS + LPC55S69_FLASH_SIZE, data, 4),
            -1);
    ck_assert_int_eq(hal_flash_write(0x20000000U, data, 4), -1);
    ck_assert_int_eq(n_ops, 0);
}
END_TEST

START_TEST(test_write_program_failure_stops)
{
    uint8_t data[2 * PAGE];

    memset(data, 0x12, sizeof(data));
    wipe_page(BOOT);
    wipe_page(BOOT + PAGE);
    fail_program_at = 1;
    ck_assert_int_eq(hal_flash_write(BOOT, data, sizeof(data)), -1);
    ck_assert_int_eq(program_calls, 1);
    ck_assert(!programmed[(BOOT + PAGE) / PAGE]);
}
END_TEST

START_TEST(test_write_erase_failure_stops)
{
    uint8_t b = 0x34;

    put_page(BOOT, 0x00);
    fail_erase_at = 1;
    ck_assert_int_eq(hal_flash_write(S_ALIAS + BOOT + 1, &b, 1), -1);
    ck_assert_int_eq(program_calls, 0);
}
END_TEST

START_TEST(test_erase_leaves_readable_ff)
{
    uint32_t i;

    put_page(BOOT, 0x00);
    put_page(BOOT + PAGE, 0x00);
    ck_assert_int_eq(hal_flash_erase(BOOT, 2 * PAGE), 0);
    ck_assert_int_eq(n_ops, 4);
    ck_assert_int_eq(op_kind[0], 'E');
    ck_assert_int_eq(op_kind[1], 'P');
    ck_assert_int_eq(op_kind[2], 'E');
    ck_assert_int_eq(op_kind[3], 'P');
    for (i = 0; i < 2 * PAGE; i++)
        ck_assert_uint_eq(mock_flash[BOOT + i], 0xFF);
    ck_assert(programmed[BOOT / PAGE]);
    ck_assert(programmed[(BOOT + PAGE) / PAGE]);
}
END_TEST

START_TEST(test_erase_rejects_bad_arguments)
{
    ck_assert_int_eq(hal_flash_erase(BOOT, 0), 0);
    ck_assert_int_eq(hal_flash_erase(BOOT + 1, PAGE), -1);
    ck_assert_int_eq(hal_flash_erase(BOOT, PAGE + 1), -1);
    ck_assert_int_eq(hal_flash_erase(BOOT, -(int)PAGE), -1);
    ck_assert_int_eq(hal_flash_erase(0, -(int)PAGE), -1);
    ck_assert_int_eq(hal_flash_erase(LPC55S69_FLASH_SIZE, PAGE), -1);
    ck_assert_int_eq(hal_flash_erase(LPC55S69_FLASH_SIZE - PAGE, 2 * PAGE),
            -1);
    ck_assert_int_eq(n_ops, 0);
}
END_TEST

START_TEST(test_erase_failure_stops)
{
    fail_erase_at = 2;
    ck_assert_int_eq(hal_flash_erase(BOOT, 3 * PAGE), -1);
    ck_assert_int_eq(erase_calls, 2);
    ck_assert_int_eq(program_calls, 1);
}
END_TEST

START_TEST(test_irqs_masked_and_primask_restored)
{
    uint8_t b = 0x56;

    mock_primask = 0;
    ck_assert_int_eq(hal_flash_write(S_ALIAS + BOOT + 9, &b, 1), 0);
    ck_assert_int_eq(hal_flash_erase(BOOT + PAGE, PAGE), 0);
    ck_assert_uint_eq(mock_primask, 0);
    ck_assert_int_eq(irq_enabled_during_rom, 0);

    mock_primask = 1;
    ck_assert_int_eq(hal_flash_write(S_ALIAS + BOOT + 10, &b, 1), 0);
    ck_assert_uint_eq(mock_primask, 1);
    ck_assert_int_eq(irq_enabled_during_rom, 0);

    torn_page(BOOT + 2 * PAGE, 0x5A);
    reset_counters();
    mock_primask = 0;
    flash_ready = 0;
    hal_init();
    ck_assert_int_eq(n_ops, 2);
    ck_assert_uint_eq(mock_primask, 0);
    ck_assert_int_eq(irq_enabled_during_rom, 0);
}
END_TEST

static Suite *lpc55s69_flash_suite(void)
{
    Suite *s = suite_create("lpc55s69-flash");
    TCase *tc = tcase_create("lpc55s69-flash");

    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_init_fills_pages_wolfboot_reads);
    tcase_add_test(tc, test_init_keeps_programmed_pages);
    tcase_add_test(tc, test_init_fill_stops_at_failure);
    tcase_add_test(tc, test_init_failure_then_retry);
    tcase_add_test(tc, test_init_repairs_torn_page);
    tcase_add_test(tc, test_init_keeps_correctable_page);
    tcase_add_test(tc, test_init_single_bad_word_keeps_neighbours);
    tcase_add_test(tc, test_init_two_bad_words_fill_ff);
    tcase_add_test(tc, test_init_transient_flag_not_repaired);
    tcase_add_test(tc, test_init_check_unavailable_keeps_pages);
    tcase_add_test(tc, test_init_selftest_disables_repair);
    tcase_add_test(tc, test_init_check_skips_bootloader_region);
    tcase_add_test(tc, test_init_repair_failure_stops);
    tcase_add_test(tc, test_partial_write_keeps_neighbours);
    tcase_add_test(tc, test_write_to_blank_page_skips_erase);
    tcase_add_test(tc, test_write_spanning_pages);
    tcase_add_test(tc, test_full_page_write_reads_nothing);
    tcase_add_test(tc, test_secure_and_nonsecure_alias_match);
    tcase_add_test(tc, test_write_rejects_bad_arguments);
    tcase_add_test(tc, test_write_program_failure_stops);
    tcase_add_test(tc, test_write_erase_failure_stops);
    tcase_add_test(tc, test_erase_leaves_readable_ff);
    tcase_add_test(tc, test_erase_rejects_bad_arguments);
    tcase_add_test(tc, test_erase_failure_stops);
    tcase_add_test(tc, test_irqs_masked_and_primask_restored);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = lpc55s69_flash_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
