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
static int g_erase_calls, g_write_calls, g_fail_write_at, g_fail_erase;

struct stage2_parameter *stage2_get_parameters(void)
{
    return &g_params;
}

int tgl_spi_flash_erase(uint32_t address, uint32_t len)
{
    uintptr_t off = (uintptr_t)address - (uintptr_t)(uint32_t)FSP_NVS_BASE;

    g_erase_calls++;
    if (g_fail_erase)
        return -1;
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

static uint8_t g_cmos;

uint8_t cmos_read(uint8_t index)
{
    (void)index;
    return g_cmos;
}

void cmos_write(uint8_t index, uint8_t value)
{
    (void)index;
    g_cmos = value;
}

/* wolfTPM as fsp_nvs_tpm_sync() uses it, over one NV index */
#include "tpm.h"
#include <wolftpm/tpm2_wrap.h>

WOLFTPM2_DEV wolftpm_dev;
static int g_nv_defined, g_nv_written, g_nv_creates, g_nv_writes;
static word32 g_nv_attr;
static uint8_t g_nv_data[32];

int wolfTPM2_NVOpen(WOLFTPM2_DEV *dev, WOLFTPM2_NV *nv, word32 nvIndex,
                    const byte *auth, word32 authSz)
{
    (void)dev; (void)nvIndex; (void)auth; (void)authSz;
    if (!g_nv_defined)
        return TPM_RC_HANDLE | RC_FMT1;
    nv->attributes = g_nv_attr;
    return 0;
}

int wolfTPM2_NVReadAuth(WOLFTPM2_DEV *dev, WOLFTPM2_NV *nv, word32 nvIndex,
                        byte *dataBuf, word32 *pDataSz, word32 offset)
{
    (void)dev; (void)nv; (void)nvIndex; (void)offset;
    if (!g_nv_written)
        return TPM_RC_NV_UNINITIALIZED;
    memcpy(dataBuf, g_nv_data, sizeof(g_nv_data));
    *pDataSz = sizeof(g_nv_data);
    return 0;
}

int wolfTPM2_NVCreateAuth(WOLFTPM2_DEV *dev, WOLFTPM2_HANDLE *parent,
                          WOLFTPM2_NV *nv, word32 nvIndex, word32 nvAttributes,
                          word32 maxSize, const byte *auth, int authSz)
{
    (void)dev; (void)nv; (void)nvIndex; (void)auth; (void)authSz;
    ck_assert_uint_eq(parent->hndl, TPM_RH_PLATFORM);
    ck_assert_uint_eq(maxSize, sizeof(g_nv_data));
    if (g_nv_defined)
        return TPM_RC_NV_DEFINED;
    g_nv_creates++;
    g_nv_defined = 1;
    g_nv_attr = nvAttributes;
    return 0;
}

TPM_RC TPM2_NV_Write(NV_Write_In *in)
{
    ck_assert_uint_eq(in->authHandle, TPM_RH_PLATFORM);
    ck_assert_uint_eq(in->data.size, sizeof(g_nv_data));
    g_nv_writes++;
    g_nv_written = 1;
    memcpy(g_nv_data, in->data.buffer, sizeof(g_nv_data));
    return 0;
}

int wolfTPM2_GetNvAttributesTemplate(TPM_HANDLE auth, word32 *nvAttributes)
{
    ck_assert_uint_eq(auth, TPM_RH_PLATFORM);
    *nvAttributes = TPMA_NV_PPWRITE | TPMA_NV_PPREAD | TPMA_NV_AUTHWRITE |
        TPMA_NV_AUTHREAD | TPMA_NV_PLATFORMCREATE | TPMA_NV_NO_DA;
    return 0;
}

int wolfTPM2_SetAuthHandle(WOLFTPM2_DEV *dev, int index,
                           const WOLFTPM2_HANDLE *handle)
{
    (void)dev; (void)index; (void)handle;
    return 0;
}

int wolfTPM2_SetAuthPassword(WOLFTPM2_DEV *dev, int index,
                             const TPM2B_AUTH *auth)
{
    (void)dev; (void)index; (void)auth;
    return 0;
}

int wolfTPM2_UnsetAuth(WOLFTPM2_DEV *dev, int index)
{
    (void)dev; (void)index;
    return 0;
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
    g_erase_calls = g_write_calls = g_fail_write_at = g_fail_erase = 0;
    g_cmos = 0;
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

static void nv_reset(void)
{
    g_nv_defined = g_nv_written = g_nv_creates = g_nv_writes = 0;
    g_nv_attr = 0;
    memset(g_nv_data, 0, sizeof(g_nv_data));
}

START_TEST(test_skip_is_kept_until_the_record_is_replaced)
{
    /* stage 1 skipped the record, and the save of the retrained data fails
     * at the erase, so the skipped record is still valid in flash */
    memset(g_area, 0xff, sizeof(g_area));
    setup(0xabcd);
    build_hobs(300, 0x10);
    fsp_nvs_save();
    build_hobs(300, 0x20);
    g_cmos = 0xA5;
    g_fail_erase = 1;
    fsp_nvs_save();
    ck_assert_int_eq(fsp_nvs_record_valid(area(), 0xabcd, AREA_SIZE), 1);
    fsp_nvs_boot_ok();
    ck_assert_uint_eq(g_cmos, 0xA5);

    /* a boot that used the record marks it good whatever the save did */
    g_cmos = 0x5A;
    fsp_nvs_save();
    fsp_nvs_boot_ok();
    ck_assert_uint_eq(g_cmos, 0);

    /* a failed write leaves no valid record, so nothing is left to skip */
    g_cmos = 0xA5;
    g_fail_erase = 0;
    g_write_calls = 0;
    g_fail_write_at = 1;
    fsp_nvs_save();
    ck_assert_int_eq(fsp_nvs_record_valid(area(), 0xabcd, AREA_SIZE), 0);
    fsp_nvs_boot_ok();
    ck_assert_uint_eq(g_cmos, 0);

    /* and a save that replaces the record ends the skipping */
    memset(g_area, 0xff, sizeof(g_area));
    g_fail_write_at = 0;
    build_hobs(300, 0x10);
    fsp_nvs_save();
    build_hobs(300, 0x20);
    g_cmos = 0xA5;
    g_fail_erase = 1;
    fsp_nvs_save();
    fsp_nvs_boot_ok();
    ck_assert_uint_eq(g_cmos, 0xA5);
    g_fail_erase = 0;
    fsp_nvs_save();
    fsp_nvs_boot_ok();
    ck_assert_uint_eq(g_cmos, 0);
}
END_TEST

START_TEST(test_tpm_reference_follows_the_record)
{
    uint8_t hash[32];

    /* no record: nothing to bless */
    nv_reset();
    memset(g_area, 0xff, sizeof(g_area));
    setup(0xabcd);
    fsp_nvs_tpm_sync();
    ck_assert_int_eq(g_nv_creates, 0);
    ck_assert_int_eq(g_nv_writes, 0);

    /* first record: the index is created for the platform alone */
    build_hobs(300, 0x10);
    fsp_nvs_save();
    fsp_nvs_tpm_sync();
    ck_assert_int_eq(g_nv_creates, 1);
    ck_assert_int_eq(g_nv_writes, 1);
    ck_assert_uint_eq(g_nv_attr & FSP_NVS_NV_ATTR_MASK, FSP_NVS_NV_ATTR);
    ck_assert_int_eq(fsp_nvs_record_hash(area(), hash), 0);
    ck_assert_mem_eq(g_nv_data, hash, sizeof(hash));

    /* unchanged record: no TPM write */
    fsp_nvs_tpm_sync();
    ck_assert_int_eq(g_nv_writes, 1);

    /* new record: the hash follows it */
    build_hobs(300, 0x20);
    fsp_nvs_save();
    fsp_nvs_tpm_sync();
    ck_assert_int_eq(g_nv_creates, 1);
    ck_assert_int_eq(g_nv_writes, 2);
    ck_assert_int_eq(fsp_nvs_record_hash(area(), hash), 0);
    ck_assert_mem_eq(g_nv_data, hash, sizeof(hash));

    /* a record for another FSP-M or UPD set is not blessed */
    g_params.fsp_nvs_key = 0x1234;
    g_nv_data[0] ^= 1;
    fsp_nvs_tpm_sync();
    ck_assert_int_eq(g_nv_writes, 2);
}
END_TEST

START_TEST(test_tpm_reference_not_updated_after_a_failed_save)
{
    uint8_t first[32];

    nv_reset();
    memset(g_area, 0xff, sizeof(g_area));
    setup(0xabcd);
    build_hobs(300, 0x10);
    fsp_nvs_save();
    fsp_nvs_tpm_sync();
    ck_assert_int_eq(g_nv_writes, 1);
    memcpy(first, g_nv_data, sizeof(first));

    /* the erase fails: the older record is still valid in flash, but it is
     * not this boot's data, so the TPM hash must not be touched for it */
    build_hobs(300, 0x20);
    g_fail_erase = 1;
    fsp_nvs_save();
    ck_assert_int_eq(fsp_nvs_record_valid(area(), 0xabcd, AREA_SIZE), 1);
    g_nv_data[0] ^= 1;
    fsp_nvs_tpm_sync();
    ck_assert_int_eq(g_nv_writes, 1);
    g_nv_data[0] ^= 1;
    ck_assert_mem_eq(g_nv_data, first, sizeof(first));

    /* a write failure leaves no valid record: nothing to bless either */
    g_fail_erase = 0;
    g_write_calls = 0;
    g_fail_write_at = 1;
    fsp_nvs_save();
    fsp_nvs_tpm_sync();
    ck_assert_int_eq(g_nv_writes, 1);

    /* no training data HOB at all */
    g_fail_write_at = 0;
    end_hobs(g_hobs);
    fsp_nvs_tpm_sync();
    ck_assert_int_eq(g_nv_writes, 1);
}
END_TEST

START_TEST(test_tpm_reference_index_must_be_platform_owned)
{
    nv_reset();
    memset(g_area, 0xff, sizeof(g_area));
    setup(0xabcd);
    build_hobs(300, 0x10);
    fsp_nvs_save();
    g_nv_defined = g_nv_written = 1;
    g_nv_attr = TPMA_NV_OWNERWRITE | TPMA_NV_AUTHREAD;
    fsp_nvs_tpm_sync();
    ck_assert_int_eq(g_nv_creates, 0);
    ck_assert_int_eq(g_nv_writes, 0);
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
    tcase_add_test(tc, test_skip_is_kept_until_the_record_is_replaced);
    tcase_add_test(tc, test_tpm_reference_follows_the_record);
    tcase_add_test(tc, test_tpm_reference_not_updated_after_a_failed_save);
    tcase_add_test(tc, test_tpm_reference_index_must_be_platform_owned);
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
