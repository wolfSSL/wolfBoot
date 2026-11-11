/* unit-fsp-nvs-save.c
 *
 * Unit tests for the stage 2 half of the FSP-M memory training data cache
 * (src/x86/fsp_nvs.c): finding the non-volatile storage HOB, deciding
 * whether the saved copy is current, and the erase/write sequence, against
 * a RAM-backed flash area and a synthetic HOB list.
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

#define AREA_SIZE 0x2000U
static uint8_t g_area[AREA_SIZE] __attribute__((aligned(4096)));

#define FSP_NVS_BASE ((uintptr_t)g_area)
#define FSP_NVS_SIZE AREA_SIZE
#define FSP_NVS_CACHE_FLUSH() do { } while (0)

#include "stage2_params.h"

static struct stage2_parameter g_params;
static int g_erase_calls, g_write_calls, g_fail_write_at;

struct stage2_parameter *stage2_get_parameters(void)
{
    return &g_params;
}

int tgl_spi_flash_erase(uint32_t address, uint32_t len)
{
    uintptr_t off = (uintptr_t)address - (uintptr_t)(uint32_t)FSP_NVS_BASE;

    g_erase_calls++;
    ck_assert_uint_eq(address & 0xfff, 0);
    ck_assert_uint_eq(len & 0xfff, 0);
    ck_assert_uint_le(off + len, AREA_SIZE);
    memset(g_area + off, 0xff, len);
    return 0;
}

int tgl_spi_flash_write(uint32_t address, const uint8_t *buf, uint32_t len)
{
    uintptr_t off = (uintptr_t)address - (uintptr_t)(uint32_t)FSP_NVS_BASE;

    g_write_calls++;
    if (g_fail_write_at == g_write_calls)
        return -1;
    ck_assert_uint_le(off + len, AREA_SIZE);
    memcpy(g_area + off, buf, len);
    return 0;
}

uint8_t cmos_read(uint8_t index)
{
    (void)index;
    return 0;
}

void cmos_write(uint8_t index, uint8_t value)
{
    (void)index;
    (void)value;
}

#include "../../src/x86/hob.c"
/* FSP_NVS_BASE is a RAM buffer here, not a flash address */
#undef BOOTLOADER_PARTITION_SIZE
#include "../../src/x86/fsp_nvs.c"

/* HOB list in static storage: the non-PIE build keeps it below 4 GB, as
 * stage2_parameter.hobList is 32-bit. */
static uint8_t g_hobs[0x4000] __attribute__((aligned(8)));

static uint8_t *add_guid_hob(uint8_t *p, const struct efi_guid *guid,
                             uint32_t data_len, uint8_t seed)
{
    struct efi_hob_guid_type *h = (struct efi_hob_guid_type *)p;
    uint32_t i, total = (uint32_t)sizeof(*h) + data_len;

    total = (total + 7U) & ~7U;
    h->header.hob_type = EFI_HOB_TYPE_GUID_EXTENSION;
    h->header.hob_length = (uint16_t)total;
    h->header.reserved = 0;
    memcpy(&h->name, guid, sizeof(*guid));
    for (i = 0; i < data_len; i++)
        p[sizeof(*h) + i] = (uint8_t)(seed + i);
    return p + total;
}

static void end_hobs(uint8_t *p)
{
    struct efi_hob_generic_header *h = (struct efi_hob_generic_header *)p;

    h->hob_type = EFI_HOB_TYPE_END_OF_HOB_LIST;
    h->hob_length = sizeof(*h);
    h->reserved = 0;
}

static const struct efi_guid other_guid = {
    0x11111111, 0x2222, 0x3333, { 1, 2, 3, 4, 5, 6, 7, 8 }
};

/* An unrelated GUID HOB, then the NVS HOB with data_len bytes. */
static void build_hobs(uint32_t data_len, uint8_t seed)
{
    uint8_t *p = g_hobs;

    memset(g_hobs, 0, sizeof(g_hobs));
    p = add_guid_hob(p, &other_guid, 24, 0x55);
    p = add_guid_hob(p, &fsp_nvs_hob_guid, data_len, seed);
    end_hobs(p);
}

static void setup(uint32_t key)
{
    memset(&g_params, 0, sizeof(g_params));
    g_params.hobList = (uint32_t)(uintptr_t)g_hobs;
    g_params.fsp_nvs_key = key;
    g_erase_calls = g_write_calls = g_fail_write_at = 0;
}

static const struct fsp_nvs_record *area(void)
{
    return (const struct fsp_nvs_record *)g_area;
}

START_TEST(test_save_into_empty_area)
{
    ck_assert_uint_lt((uintptr_t)g_hobs, 0x100000000ULL);
    memset(g_area, 0xff, sizeof(g_area));
    setup(0xabcd);
    build_hobs(1000, 3);
    fsp_nvs_save();
    ck_assert_int_eq(g_erase_calls, 1);
    ck_assert_int_eq(fsp_nvs_record_valid(area(), 0xabcd, AREA_SIZE), 1);
    ck_assert_uint_eq(area()->len, 1000);
    /* the unrelated HOB is 48 bytes, then the NVS HOB header */
    ck_assert_int_eq(memcmp(area()->data,
                     g_hobs + 48 + sizeof(struct efi_hob_guid_type), 1000), 0);
}
END_TEST

START_TEST(test_current_copy_is_not_rewritten)
{
    memset(g_area, 0xff, sizeof(g_area));
    setup(0xabcd);
    build_hobs(1000, 3);
    fsp_nvs_save();
    setup(0xabcd);
    fsp_nvs_save();
    ck_assert_int_eq(g_erase_calls, 0);
    ck_assert_int_eq(g_write_calls, 0);
}
END_TEST

START_TEST(test_changed_data_or_key_is_rewritten)
{
    memset(g_area, 0xff, sizeof(g_area));
    setup(0xabcd);
    build_hobs(1000, 3);
    fsp_nvs_save();

    setup(0xabcd);
    build_hobs(1000, 4);
    fsp_nvs_save();
    ck_assert_int_eq(g_erase_calls, 1);
    ck_assert_uint_eq(area()->data[0], 4);

    setup(0x1234);
    fsp_nvs_save();
    ck_assert_int_eq(g_erase_calls, 1);
    ck_assert_int_eq(fsp_nvs_record_valid(area(), 0x1234, AREA_SIZE), 1);
}
END_TEST

START_TEST(test_missing_or_oversized_hob)
{
    uint8_t *p;

    memset(g_area, 0xff, sizeof(g_area));
    setup(1);
    memset(g_hobs, 0, sizeof(g_hobs));
    p = add_guid_hob(g_hobs, &other_guid, 24, 0);
    end_hobs(p);
    fsp_nvs_save();
    ck_assert_int_eq(g_erase_calls, 0);

    setup(1);
    build_hobs(AREA_SIZE - sizeof(struct fsp_nvs_record) + 1, 0);
    fsp_nvs_save();
    ck_assert_int_eq(g_erase_calls, 0);
}
END_TEST

START_TEST(test_zero_length_hob_stops_the_walk)
{
    struct efi_hob_generic_header *h =
        (struct efi_hob_generic_header *)g_hobs;

    memset(g_area, 0xff, sizeof(g_area));
    setup(1);
    memset(g_hobs, 0, sizeof(g_hobs));
    h->hob_type = EFI_HOB_TYPE_GUID_EXTENSION;
    h->hob_length = 0;
    fsp_nvs_save();
    ck_assert_int_eq(g_erase_calls, 0);
}
END_TEST

START_TEST(test_failed_header_write_leaves_no_record)
{
    memset(g_area, 0xff, sizeof(g_area));
    setup(7);
    build_hobs(1000, 3);
    /* the data is written first, then the header; fail the header */
    g_fail_write_at = 2;
    fsp_nvs_save();
    ck_assert_int_eq(fsp_nvs_record_valid(area(), 7, AREA_SIZE), 0);
}
END_TEST

static Suite *fsp_nvs_save_suite(void)
{
    Suite *s = suite_create("fsp-nvs-save");
    TCase *tc = tcase_create("fsp-nvs-save");

    tcase_add_test(tc, test_save_into_empty_area);
    tcase_add_test(tc, test_current_copy_is_not_rewritten);
    tcase_add_test(tc, test_changed_data_or_key_is_rewritten);
    tcase_add_test(tc, test_missing_or_oversized_hob);
    tcase_add_test(tc, test_zero_length_hob_stops_the_walk);
    tcase_add_test(tc, test_failed_header_write_leaves_no_record);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = fsp_nvs_save_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
