/* unit-lpc55s69-puf.c
 *
 * Unit test for the LPC55S69 HW PUF provisioning record
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

#define H_TARGETS_TARGET_
#define WOLFBOOT_FIXED_PARTITIONS
#define WOLFBOOT_SECTOR_SIZE                 0x200
#define WOLFBOOT_PARTITION_SIZE              0x20000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x40000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x60000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x81000
#define WOLFBOOT_KEYVAULT_ADDRESS            0x10082000
#define WOLFBOOT_KEYVAULT_SIZE               0x16000
#define ARCH_FLASH_OFFSET                    0x0

#define __WOLFBOOT
#define WOLFBOOT_HASH_SHA256
#define WOLFBOOT_SIGN_ECC256
#define WOLFSSL_NXP_HWPUF
#define WOLFBOOT_HWPUF_PROVISION

#include "fsl_common.h"

const uint8_t *mock_flash_ptr(uint32_t address);
#define LPC55S69_FLASH_PTR(a) mock_flash_ptr(a)

#include "../../hal/lpc55s69.c"

#define PAGE             LPC55S69_FLASH_PAGE_SIZE
#define FLASH_PAGES      (LPC55S69_FLASH_SIZE / PAGE)
#define S_ALIAS          LPC55S69_FLASH_SECURE_ALIAS
#define REC              WOLFBOOT_HWPUF_PROV_ADDRESS
#define COMMIT           HWPUF_PROV_COMMIT_ADDRESS
#define NO_FAIL          0xFFFFFFFFU

uint32_t mock_primask;
int mock_barriers;
NVIC_Type mock_nvic;
SysTick_Type mock_systick;

static uint8_t mock_flash[LPC55S69_FLASH_SIZE];
/* 0: erased and never programmed, so a read faults on the ECC check */
static uint8_t programmed[FLASH_PAGES];
/* Fail the Nth program of one page: the first is the 0xFF fill after erase */
static uint32_t fail_program_off;
static int fail_program_hit;
static int fail_program_hits;
static int enroll_calls;
static int genkey_calls;
static int puf_inits;
static int puf_deinits;
static uint8_t enrolled_ac[PUF_ACTIVATION_CODE_SIZE];

status_t FLASH_Init(flash_config_t *config)
{
    config->PFlashTotalSize = LPC55S69_FLASH_SIZE;
    config->PFlashPageSize = PAGE;
    return kStatus_FLASH_Success;
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
    ck_assert_uint_eq(lengthInBytes, PAGE);
    ck_assert_uint_lt(start, LPC55S69_FLASH_SIZE);
    memset(&mock_flash[start], 0xFF, PAGE);
    programmed[start / PAGE] = 0;
    return kStatus_FLASH_Success;
}

status_t FLASH_Program(flash_config_t *config, uint32_t start,
        const uint8_t *src, uint32_t lengthInBytes)
{
    ck_assert_ptr_eq(config, &pflash);
    ck_assert_uint_eq(lengthInBytes, PAGE);
    ck_assert_uint_lt(start, LPC55S69_FLASH_SIZE);
    if ((start == fail_program_off) && (++fail_program_hits == fail_program_hit))
        return kStatus_Fail;
    ck_assert_msg(!programmed[start / PAGE],
            "page 0x%x programmed twice without an erase", start);
    memcpy(&mock_flash[start], src, PAGE);
    programmed[start / PAGE] = 1;
    return kStatus_FLASH_Success;
}

status_t FLASH_VerifyErase(flash_config_t *config, uint32_t start,
        uint32_t lengthInBytes)
{
    ck_assert_ptr_eq(config, &pflash);
    ck_assert_uint_eq(lengthInBytes, PAGE);
    return programmed[start / PAGE] ? kStatus_FLASH_CommandFailure :
        kStatus_FLASH_Success;
}

/* An erased page fails the compare instead of faulting, like the ROM */
status_t FLASH_VerifyProgram(flash_config_t *config, uint32_t start,
        uint32_t lengthInBytes, const uint8_t *expectedData,
        uint32_t *failedAddress, uint32_t *failedData)
{
    ck_assert_ptr_eq(config, &pflash);
    ck_assert_uint_eq(lengthInBytes, PAGE);
    *failedAddress = start;
    *failedData = 0;
    if (!programmed[start / PAGE])
        return kStatus_FLASH_EccError;
    if (memcmp(&mock_flash[start], expectedData, PAGE) != 0)
        return kStatus_FLASH_CommandFailure;
    return kStatus_FLASH_Success;
}

const uint8_t *mock_flash_ptr(uint32_t address)
{
    uint32_t off = (address >= S_ALIAS) ? address - S_ALIAS : address;

    ck_assert_uint_lt(off, LPC55S69_FLASH_SIZE);
    ck_assert_msg(programmed[off / PAGE],
            "read of erased page 0x%x faults (ECC)", off);
    return &mock_flash[off];
}

void BOARD_BootClockFROHF96M(void)
{
}

int wolfCrypt_Init(void)
{
    return 0;
}

int nxp_hwpuf_Init(void)
{
    puf_inits++;
    return 0;
}

int nxp_hwpuf_Deinit(void)
{
    puf_deinits++;
    return 0;
}

int nxp_hwpuf_Enroll(byte *actCode, word32 actCodeSz)
{
    word32 i;

    ck_assert_uint_eq(actCodeSz, sizeof(enrolled_ac));
    ck_assert_uint_eq(((uintptr_t)actCode) % 4U, 0);
    enroll_calls++;
    for (i = 0; i < actCodeSz; i++)
        actCode[i] = (byte)(0xA5U + i + (word32)enroll_calls);
    memcpy(enrolled_ac, actCode, actCodeSz);
    return 0;
}

int nxp_hwpuf_Start(byte *actCode, word32 actCodeSz)
{
    ck_assert_uint_eq(actCodeSz, sizeof(enrolled_ac));
    ck_assert_uint_eq(((uintptr_t)actCode) % 4U, 0);
    return (memcmp(actCode, enrolled_ac, actCodeSz) == 0) ? 0 : -1;
}

int nxp_hwpuf_GenerateKey(byte keyIdx, word32 keySz, byte *keyCode,
        word32 keyCodeSz)
{
    word32 i;

    ck_assert_uint_eq(keyIdx, HWPUF_PROV_UDS_KEY_INDEX);
    ck_assert_uint_eq(keySz, HWPUF_PROV_UDS_KEY_SIZE);
    ck_assert_uint_eq(((uintptr_t)keyCode) % 4U, 0);
    genkey_calls++;
    for (i = 0; i < keyCodeSz; i++)
        keyCode[i] = (byte)(0x5AU + i);
    return 0;
}

int nxp_hwpuf_GetKey(byte *keyCode, word32 keyCodeSz, byte *key, word32 keySz)
{
    (void)keyCode;
    (void)keyCodeSz;
    memset(key, 0x11, keySz);
    return 0;
}

int nxp_hwpuf_Zeroize(void)
{
    return 0;
}

static void put_page(uint32_t off, uint8_t fill)
{
    memset(&mock_flash[off], fill, PAGE);
    programmed[off / PAGE] = 1;
}

static void put_word(uint32_t off, uint32_t word)
{
    put_page(off, 0xFF);
    memcpy(&mock_flash[off], &word, sizeof(word));
}

static void put_record(void)
{
    uint32_t off;

    for (off = REC; off < REC + sizeof(hwpuf_prov); off += PAGE)
        put_page(off, 0x33);
}

static void setup(void)
{
    memset(mock_flash, 0xFF, sizeof(mock_flash));
    memset(programmed, 0, sizeof(programmed));
    memset(enrolled_ac, 0, sizeof(enrolled_ac));
    fail_program_off = NO_FAIL;
    fail_program_hit = fail_program_hits = 0;
    enroll_calls = genkey_calls = puf_inits = puf_deinits = 0;
    flash_ready = 0;
    mock_primask = 0;
    flashInit();
    ck_assert_int_eq(flash_ready, 1);
}

START_TEST(test_record_is_word_aligned)
{
    ck_assert_uint_eq(((uintptr_t)&prov) % 4U, 0);
    ck_assert_uint_eq(((uintptr_t)prov.ac) % 4U, 0);
    ck_assert_uint_eq(((uintptr_t)prov.uds_kc) % 4U, 0);
    ck_assert_uint_eq(REC % PAGE, 0);
    ck_assert_uint_eq(COMMIT % PAGE, 0);
    ck_assert_uint_le(sizeof(hwpuf_prov), COMMIT - REC);
}
END_TEST

START_TEST(test_erased_region_provisions_once)
{
    hwpuf_prov rec;

    ck_assert_int_eq(hwpuf_provision_get(&rec), HWPUF_PROV_BLANK);
    ck_assert_int_eq(hwpuf_provision_set(), 0);
    ck_assert_int_eq(enroll_calls, 1);
    ck_assert_int_eq(genkey_calls, 1);
    ck_assert_int_eq(puf_inits, puf_deinits);
    ck_assert_uint_eq(hwpuf_commit_word(), HWPUF_PROV_MAGIC);

    ck_assert_int_eq(hwpuf_provision_get(&rec), 0);
    ck_assert_int_eq(memcmp(rec.ac, enrolled_ac, sizeof(rec.ac)), 0);
    ck_assert_uint_eq(rec.uds_kc[0], 0x5AU);

    /* A committed record is reused, never re-enrolled */
    ck_assert_int_eq(hwpuf_provision_set(), 0);
    ck_assert_int_eq(enroll_calls, 1);
    ck_assert_int_eq(hwpuf_provision_get(&rec), 0);
    ck_assert_int_eq(memcmp(rec.ac, enrolled_ac, sizeof(rec.ac)), 0);
}
END_TEST

START_TEST(test_commit_page_of_ff_is_blank)
{
    hwpuf_prov rec;

    put_record();
    put_page(COMMIT, 0xFF);
    ck_assert_int_eq(hwpuf_provision_get(&rec), HWPUF_PROV_BLANK);
    ck_assert_int_eq(hwpuf_provision_set(), 0);
    ck_assert_int_eq(enroll_calls, 1);
    ck_assert_uint_eq(hwpuf_commit_word(), HWPUF_PROV_MAGIC);
}
END_TEST

START_TEST(test_other_commit_word_is_never_replaced)
{
    hwpuf_prov rec;

    put_record();
    put_word(COMMIT, 0x12345678U);
    ck_assert_int_eq(hwpuf_provision_get(&rec), -1);
    ck_assert_int_eq(hwpuf_provision_set(), -1);
    ck_assert_int_eq(enroll_calls, 0);
    ck_assert_uint_eq(hwpuf_commit_word(), 0x12345678U);
    ck_assert_uint_eq(mock_flash[REC], 0x33U);
}
END_TEST

START_TEST(test_committed_with_erased_payload_fails)
{
    hwpuf_prov rec;

    put_record();
    put_word(COMMIT, HWPUF_PROV_MAGIC);
    programmed[(REC + PAGE) / PAGE] = 0;
    ck_assert_int_eq(hwpuf_provision_get(&rec), -1);
    ck_assert_int_eq(hwpuf_provision_set(), -1);
    ck_assert_int_eq(enroll_calls, 0);
}
END_TEST

START_TEST(test_interrupted_commit_is_redone)
{
    hwpuf_prov rec;

    fail_program_off = COMMIT;
    fail_program_hit = 2;
    ck_assert_int_eq(hwpuf_provision_set(), -1);
    ck_assert_int_eq(enroll_calls, 1);
    ck_assert_int_eq(puf_inits, puf_deinits);
    ck_assert_uint_eq(mock_flash[REC], enrolled_ac[0]);

    /* Next boot: no commit word, so the record is written again */
    fail_program_off = NO_FAIL;
    ck_assert_int_eq(hwpuf_provision_get(&rec), HWPUF_PROV_BLANK);
    ck_assert_int_eq(hwpuf_provision_set(), 0);
    ck_assert_int_eq(enroll_calls, 2);
    ck_assert_int_eq(hwpuf_provision_get(&rec), 0);
    ck_assert_int_eq(memcmp(rec.ac, enrolled_ac, sizeof(rec.ac)), 0);
}
END_TEST

START_TEST(test_interrupted_payload_is_redone)
{
    hwpuf_prov rec;

    fail_program_off = REC + PAGE;
    fail_program_hit = 2;
    ck_assert_int_eq(hwpuf_provision_set(), -1);
    ck_assert_uint_eq(mock_flash[REC], enrolled_ac[0]);
    ck_assert_uint_eq(programmed[COMMIT / PAGE], 1);
    ck_assert_uint_eq(hwpuf_commit_word(), 0xFFFFFFFFU);

    fail_program_off = NO_FAIL;
    ck_assert_int_eq(hwpuf_provision_get(&rec), HWPUF_PROV_BLANK);
    ck_assert_int_eq(hwpuf_provision_set(), 0);
    ck_assert_int_eq(hwpuf_provision_get(&rec), 0);
}
END_TEST

static Suite *lpc55s69_puf_suite(void)
{
    Suite *s = suite_create("lpc55s69-puf");
    TCase *tc = tcase_create("provisioning record");

    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_record_is_word_aligned);
    tcase_add_test(tc, test_erased_region_provisions_once);
    tcase_add_test(tc, test_commit_page_of_ff_is_blank);
    tcase_add_test(tc, test_other_commit_word_is_never_replaced);
    tcase_add_test(tc, test_committed_with_erased_payload_fails);
    tcase_add_test(tc, test_interrupted_commit_is_redone);
    tcase_add_test(tc, test_interrupted_payload_is_redone);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = lpc55s69_puf_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
