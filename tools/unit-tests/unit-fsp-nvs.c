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

/* A TPM behind the FIFO interface stage 1 reads the reference hash through:
 * it answers Startup, NV_ReadPublic and NV_Read for one index. */
#define TPM_BASE 0xFED40000U
static uint8_t g_tpm_cmd[64], g_tpm_rsp[96];
static uint32_t g_tpm_cmd_len, g_tpm_rsp_len, g_tpm_rsp_pos;
static uint32_t g_tpm_startup_rc, g_tpm_nv_attr;
static int g_tpm_nv_defined, g_tpm_nv_reads;
static uint8_t g_tpm_nv_hash[32];

static void tpm_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void tpm_respond(uint32_t rc, uint32_t len)
{
    g_tpm_rsp[0] = 0x80;
    g_tpm_rsp[1] = 0x01;
    tpm_put32(g_tpm_rsp + 2, rc != 0 ? 10 : len);
    tpm_put32(g_tpm_rsp + 6, rc);
    g_tpm_rsp_len = rc != 0 ? 10 : len;
    g_tpm_rsp_pos = 0;
}

static void tpm_execute(void)
{
    uint32_t cc = ((uint32_t)g_tpm_cmd[6] << 24) |
        ((uint32_t)g_tpm_cmd[7] << 16) | ((uint32_t)g_tpm_cmd[8] << 8) |
        g_tpm_cmd[9];

    memset(g_tpm_rsp, 0, sizeof(g_tpm_rsp));
    if (cc == 0x144) { /* Startup */
        tpm_respond(g_tpm_startup_rc, 10);
    }
    else if (!g_tpm_nv_defined) {
        tpm_respond(0x18B, 10); /* TPM_RC_HANDLE */
    }
    else if (cc == 0x169) { /* NV_ReadPublic */
        g_tpm_rsp[11] = 14;
        memcpy(g_tpm_rsp + 12, g_tpm_cmd + 10, 4);
        g_tpm_rsp[17] = 0x0b;
        tpm_put32(g_tpm_rsp + 18, g_tpm_nv_attr);
        g_tpm_rsp[25] = 32;
        tpm_respond(0, 62);
    }
    else if (cc == 0x14E) { /* NV_Read */
        g_tpm_nv_reads++;
        g_tpm_rsp[13] = 34;
        g_tpm_rsp[15] = 32;
        memcpy(g_tpm_rsp + 16, g_tpm_nv_hash, 32);
        tpm_respond(0, 53);
    }
    else {
        tpm_respond(0x143, 10); /* TPM_RC_COMMAND_CODE */
    }
}

uint8_t mmio_read8(uintptr_t address)
{
    if (address == TPM_BASE + 0x00)
        return 0x80 | 0x20;
    if (address == TPM_BASE + 0x18)
        return 0x80 | 0x40 |
            (g_tpm_rsp_pos < g_tpm_rsp_len ? 0x10 : 0);
    if (address == TPM_BASE + 0x24 && g_tpm_rsp_pos < g_tpm_rsp_len)
        return g_tpm_rsp[g_tpm_rsp_pos++];
    return 0xff;
}

uint32_t mmio_read32(uintptr_t address)
{
    if (address == TPM_BASE + 0x18)
        return (8U << 8) | mmio_read8(address);
    return 0xffffffffU;
}

void mmio_write8(uintptr_t address, uint8_t value)
{
    if (address == TPM_BASE + 0x18 && value == 0x40) {
        g_tpm_cmd_len = 0;
        g_tpm_rsp_len = g_tpm_rsp_pos = 0;
    }
    else if (address == TPM_BASE + 0x18 && value == 0x20) {
        tpm_execute();
    }
    else if (address == TPM_BASE + 0x24 &&
             g_tpm_cmd_len < sizeof(g_tpm_cmd)) {
        g_tpm_cmd[g_tpm_cmd_len++] = value;
    }
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
    /* as stage 2 leaves it: the TPM holds the hash of this record */
    g_tpm_nv_defined = 1;
    g_tpm_nv_attr = 0x42040001U;
    g_tpm_startup_rc = 0;
    g_tpm_nv_reads = 0;
    fsp_nvs_record_hash(rec, g_tpm_nv_hash);
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

START_TEST(test_apply_needs_the_tpm_reference_hash)
{
    struct fsp_nvs_record *rec;

    /* a TPM another stage already started is fine */
    reset_udp();
    write_record(expected_key(), 200);
    g_tpm_startup_rc = 0x100;
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 1);
    ck_assert_int_eq(g_tpm_nv_reads, 1);

    /* a record changed in flash, with its CRC fixed up, is not used */
    reset_udp();
    rec = write_record(expected_key(), 200);
    rec->data[10] ^= 1;
    rec->crc = fsp_nvs_crc32(0, rec->data, rec->len);
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 0);
    ck_assert_uint_eq(arch()->NvsBufferPtr, 0);
    ck_assert_uint_eq(arch()->BootMode, BOOT_WITH_FULL_CONFIGURATION);
    ck_assert_uint_eq(cmos_read(FSP_NVS_STATE_CMOS_IDX), 0);

    /* nor is one with no reference in the TPM */
    reset_udp();
    write_record(expected_key(), 200);
    g_tpm_nv_defined = 0;
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 0);

    /* or with the TPM failing to start */
    reset_udp();
    write_record(expected_key(), 200);
    g_tpm_startup_rc = 0x101;
    ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE), 0);
}
END_TEST

START_TEST(test_apply_rejects_an_index_the_os_can_write)
{
    static const uint32_t attrs[] = {
        0x02040002U, /* owner-created, owner write */
        0x42040003U, /* platform-created, but owner write too */
        0x42040005U, /* auth write */
        0x42040009U, /* policy write */
        0x42040000U  /* no platform write */
    };
    unsigned int i;

    for (i = 0; i < sizeof(attrs) / sizeof(attrs[0]); i++) {
        reset_udp();
        write_record(expected_key(), 200);
        g_tpm_nv_attr = attrs[i];
        ck_assert_int_eq(fsp_nvs_apply(g_udp, UDP_SIZE, g_fspm, FSPM_SIZE),
                         0);
        ck_assert_int_eq(g_tpm_nv_reads, 0);
        ck_assert_uint_eq(arch()->NvsBufferPtr, 0);
    }
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
    tcase_add_test(tc, test_apply_needs_the_tpm_reference_hash);
    tcase_add_test(tc, test_apply_rejects_an_index_the_os_can_write);
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
