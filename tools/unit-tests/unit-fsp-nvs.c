/* unit-fsp-nvs.c
 *
 * Unit tests for the FSP-M memory training data cache (src/x86/fsp_nvs.c):
 * the record check that decides whether saved data is handed back to
 * FspMemInit, and the stage 1 hand-off itself.
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

#define AREA_SIZE 0x1000U
static uint8_t g_area[AREA_SIZE] __attribute__((aligned(16)));

#define FSP_NVS_BASE ((uintptr_t)g_area)
#define FSP_NVS_SIZE AREA_SIZE

#include "stage2_params.h"

static struct stage2_parameter g_params;

struct stage2_parameter *stage2_get_parameters(void)
{
    return &g_params;
}

static uint8_t g_cmos[128];

uint8_t cmos_read(uint8_t index)
{
    return g_cmos[index & 0x7F];
}

void cmos_write(uint8_t index, uint8_t value)
{
    g_cmos[index & 0x7F] = value;
}

/* FSP_NVS_BASE is a RAM buffer here, not a flash address */
#undef BOOTLOADER_PARTITION_SIZE
#include "../../src/x86/fsp_nvs.c"

#define UDP_SIZE 0x100U
#define FSPM_SIZE 0x200U

static uint8_t g_udp[UDP_SIZE];
static uint8_t g_fspm[FSPM_SIZE];

static FSPM_ARCH_UPD *arch(void)
{
    return (FSPM_ARCH_UPD *)(g_udp + sizeof(FSP_UPD_HEADER));
}

static uint32_t expected_key(void)
{
    uint32_t key = fsp_nvs_crc32(0, g_fspm, FSPM_SIZE);

    return fsp_nvs_crc32(key, g_udp, UDP_SIZE);
}

static struct fsp_nvs_record *write_record(uint32_t key, uint32_t len)
{
    struct fsp_nvs_record *rec = (struct fsp_nvs_record *)g_area;
    uint32_t i;

    memset(g_area, 0xff, sizeof(g_area));
    rec->magic = FSP_NVS_MAGIC;
    rec->key = key;
    rec->len = len;
    for (i = 0; i < len; i++)
        rec->data[i] = (uint8_t)(i * 7 + 1);
    rec->crc = fsp_nvs_crc32(0, rec->data, len);
    return rec;
}

/* fresh UPDs, as each boot starts from the FSP-M defaults; CMOS persists */
static void next_boot(void)
{
    uint32_t i;

    for (i = 0; i < UDP_SIZE; i++)
        g_udp[i] = (uint8_t)i;
    for (i = 0; i < FSPM_SIZE; i++)
        g_fspm[i] = (uint8_t)(i * 3);
    arch()->NvsBufferPtr = 0;
    arch()->BootMode = BOOT_WITH_FULL_CONFIGURATION;
    memset(&g_params, 0, sizeof(g_params));
}

static void reset_udp(void)
{
    next_boot();
    memset(g_cmos, 0, sizeof(g_cmos));
}

START_TEST(test_crc32_check_value)
{
    ck_assert_uint_eq(fsp_nvs_crc32(0, (const uint8_t *)"123456789", 9),
                      0xCBF43926U);
    /* chaining two halves equals one pass */
    ck_assert_uint_eq(fsp_nvs_crc32(fsp_nvs_crc32(0,
                          (const uint8_t *)"1234", 4),
                          (const uint8_t *)"56789", 5), 0xCBF43926U);
}
END_TEST

START_TEST(test_record_valid)
{
    struct fsp_nvs_record *rec = write_record(0x1234, 100);

    ck_assert_int_eq(fsp_nvs_record_valid(rec, 0x1234, AREA_SIZE), 1);
    ck_assert_int_eq(fsp_nvs_record_valid(rec, 0x1235, AREA_SIZE), 0);
}
END_TEST

START_TEST(test_record_rejects_bad_fields)
{
    struct fsp_nvs_record *rec;

    rec = write_record(1, 100);
    rec->magic ^= 1;
    ck_assert_int_eq(fsp_nvs_record_valid(rec, 1, AREA_SIZE), 0);

    rec = write_record(1, 100);
    rec->data[50] ^= 0x80;
    ck_assert_int_eq(fsp_nvs_record_valid(rec, 1, AREA_SIZE), 0);

    rec = write_record(1, 100);
    rec->len = 0;
    ck_assert_int_eq(fsp_nvs_record_valid(rec, 1, AREA_SIZE), 0);

    /* data exactly filling the area is accepted, one byte more is not */
    rec = write_record(1, AREA_SIZE - sizeof(*rec));
    ck_assert_int_eq(fsp_nvs_record_valid(rec, 1, AREA_SIZE), 1);
    rec->len = AREA_SIZE - sizeof(*rec) + 1;
    ck_assert_int_eq(fsp_nvs_record_valid(rec, 1, AREA_SIZE), 0);
    rec->len = 0xffffffffU;
    ck_assert_int_eq(fsp_nvs_record_valid(rec, 1, AREA_SIZE), 0);

    /* an area too small for the header is never valid */
    rec = write_record(1, 4);
    ck_assert_int_eq(fsp_nvs_record_valid(rec, 1, sizeof(*rec)), 0);
}
END_TEST

START_TEST(test_apply_without_record)
{
    reset_udp();
    memset(g_area, 0xff, sizeof(g_area));
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 0);
    ck_assert_uint_eq(arch()->NvsBufferPtr, 0);
    ck_assert_uint_eq(arch()->BootMode, BOOT_WITH_FULL_CONFIGURATION);
    ck_assert_uint_eq(g_params.fsp_nvs_key, expected_key());
}
END_TEST

START_TEST(test_apply_with_matching_record)
{
    struct fsp_nvs_record *rec;

    reset_udp();
    rec = write_record(expected_key(), 200);
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 1);
    ck_assert_uint_eq(arch()->NvsBufferPtr,
                      (uint32_t)(uintptr_t)rec->data);
    ck_assert_uint_eq(arch()->BootMode,
                      BOOT_ASSUMING_NO_CONFIGURATION_CHANGES);
}
END_TEST

START_TEST(test_apply_skips_a_rejected_record_until_cleared)
{
    /* after FspMemInit failed with the record, boots train from scratch,
     * across FSP's reset requests, until a boot gets through FspSiliconInit */
    reset_udp();
    write_record(expected_key(), 200);
    fsp_nvs_reject();
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 0);
    ck_assert_uint_eq(arch()->NvsBufferPtr, 0);
    ck_assert_uint_eq(arch()->BootMode, BOOT_WITH_FULL_CONFIGURATION);
    ck_assert_uint_eq(g_params.fsp_nvs_key, expected_key());
    fsp_nvs_before_reset();
    next_boot();
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 0);

    fsp_nvs_boot_ok();
    next_boot();
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 1);
}
END_TEST

START_TEST(test_unfinished_boot_with_record_retrains)
{
    /* a boot with the record that crashed or hung never cleared IN_USE */
    reset_udp();
    write_record(expected_key(), 200);
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 1);
    next_boot();
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 0);
    ck_assert_uint_eq(arch()->NvsBufferPtr, 0);
    ck_assert_uint_eq(arch()->BootMode, BOOT_WITH_FULL_CONFIGURATION);

    /* and the retraining boots keep skipping it across FSP resets */
    fsp_nvs_before_reset();
    next_boot();
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 0);
    fsp_nvs_boot_ok();
    next_boot();
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 1);
}
END_TEST

START_TEST(test_fsp_reset_keeps_using_the_record)
{
    /* a reset FSP asks for during a boot with the record is not a failure */
    reset_udp();
    write_record(expected_key(), 200);
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 1);
    fsp_nvs_before_reset();
    next_boot();
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 1);
    fsp_nvs_boot_ok();
    next_boot();
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 1);
}
END_TEST

START_TEST(test_apply_rejects_changed_config)
{
    reset_udp();
    write_record(expected_key(), 200);

    /* a different FSP-M build invalidates the record, even with the same
     * revision in its header */
    g_fspm[FSPM_SIZE / 2] ^= 1;
    fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE);
    ck_assert_uint_eq(arch()->NvsBufferPtr, 0);

    /* so does any change to the UPDs */
    reset_udp();
    g_udp[UDP_SIZE - 1] ^= 1;
    fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE);
    ck_assert_uint_eq(arch()->NvsBufferPtr, 0);
    ck_assert_uint_eq(arch()->BootMode, BOOT_WITH_FULL_CONFIGURATION);
}
END_TEST

static Suite *fsp_nvs_suite(void)
{
    Suite *s = suite_create("fsp-nvs");
    TCase *tc = tcase_create("fsp-nvs");

    tcase_add_test(tc, test_crc32_check_value);
    tcase_add_test(tc, test_record_valid);
    tcase_add_test(tc, test_record_rejects_bad_fields);
    tcase_add_test(tc, test_apply_without_record);
    tcase_add_test(tc, test_apply_with_matching_record);
    tcase_add_test(tc, test_apply_rejects_changed_config);
    tcase_add_test(tc, test_apply_skips_a_rejected_record_until_cleared);
    tcase_add_test(tc, test_unfinished_boot_with_record_retrains);
    tcase_add_test(tc, test_fsp_reset_keeps_using_the_record);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = fsp_nvs_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
