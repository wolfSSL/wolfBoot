/* unit-psa_store.c
 *
 * Unit test for PSA storage module
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

#define WOLFBOOT_HASH_SHA256
#define EXT_FLASH
#define PART_UPDATE_EXT
#define NVM_FLASH_WRITEONCE

#define KEYSTORE_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_ECC256

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define XMALLOC_OVERRIDE
#define XMALLOC(n,h,t) malloc(n)
#define XFREE(p,h,t) free(p)

#include "user_settings.h"
#include "wolfssl/wolfcrypt/sha.h"
#include "wolfssl/wolfcrypt/error-crypt.h"
#include "wolfboot/wolfboot.h"
#include "wolfpsa/psa_store.h"
#include "hal.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define MOCK_ADDRESS 0xCF000000
uint8_t *vault_base = (uint8_t *)MOCK_ADDRESS;
#include "psa_store.c"
const uint32_t keyvault_size = KEYVAULT_OBJ_SIZE * KEYVAULT_MAX_ITEMS + 2 * WOLFBOOT_SECTOR_SIZE;
#include "unit-mock-flash.c"

START_TEST(test_cross_sector_write_preserves_length)
{
    enum { type = WOLFPSA_STORE_KEY };
    const unsigned long id1 = 7;
    const unsigned long id2 = 9;
    void *store = NULL;
    unsigned char *payload;
    struct store_handle *handle;
    int ret;

    payload = malloc(WOLFBOOT_SECTOR_SIZE);
    ck_assert_ptr_nonnull(payload);

    for (ret = 0; ret < WOLFBOOT_SECTOR_SIZE; ret++)
        payload[ret] = (unsigned char)(ret & 0xFF);

    ret = mmap_file("/tmp/wolfboot-unit-psa-keyvault.bin", vault_base,
        keyvault_size, NULL);
    ck_assert_int_eq(ret, 0);
    memset(vault_base, 0xEE, keyvault_size);

    ret = wolfPSA_Store_Open(type, id1, id2, 0, &store);
    ck_assert_int_eq(ret, 0);
    ck_assert_ptr_nonnull(store);

    ret = wolfPSA_Store_Write(store, payload, WOLFBOOT_SECTOR_SIZE);
    ck_assert_int_eq(ret, WOLFBOOT_SECTOR_SIZE);
    handle = store;
    ck_assert_uint_eq(handle->in_buffer_offset,
        2 * sizeof(uint32_t) + WOLFBOOT_SECTOR_SIZE);
    ck_assert_uint_eq(handle->hdr->size,
        2 * sizeof(uint32_t) + WOLFBOOT_SECTOR_SIZE);
    wolfPSA_Store_Close(store);

    free(payload);
}
END_TEST

START_TEST(test_close_clears_handle_state)
{
    enum { type = WOLFPSA_STORE_KEY };
    const unsigned long id1 = 17;
    const unsigned long id2 = 21;
    void *store = NULL;
    struct store_handle *handle;
    int ret;

    ret = mmap_file("/tmp/wolfboot-unit-psa-keyvault.bin", vault_base,
        keyvault_size, NULL);
    ck_assert_int_eq(ret, 0);
    memset(vault_base, 0xEE, keyvault_size);

    ret = wolfPSA_Store_Open(type, id1, id2, 0, &store);
    ck_assert_int_eq(ret, 0);
    ck_assert_ptr_nonnull(store);

    handle = store;
    ck_assert_ptr_nonnull(handle->buffer);
    ck_assert_ptr_nonnull(handle->hdr);
    ck_assert_uint_ne(handle->in_buffer_offset, 0);

    wolfPSA_Store_Close(store);

    ck_assert_uint_eq(handle->flags, 0);
    ck_assert_uint_eq(handle->pos, 0);
    ck_assert_ptr_null(handle->buffer);
    ck_assert_ptr_null(handle->hdr);
    ck_assert_uint_eq(handle->in_buffer_offset, 0);
}
END_TEST

START_TEST(test_delete_object_ignores_metadata_prefix)
{
    enum { type = WOLFPSA_STORE_KEY };
    const uint32_t tok_id = VAULT_HEADER_MAGIC;
    const uint32_t obj_id = 0x01020308U;
    uint32_t *words;
    uint8_t bitmap_before[BITMAP_SIZE];
    int ret;

    ret = mmap_file("/tmp/wolfboot-unit-psa-keyvault.bin", vault_base,
        keyvault_size, NULL);
    ck_assert_int_eq(ret, 0);
    memset(vault_base, 0xFF, keyvault_size);

    words = (uint32_t *)vault_base;
    words[0] = VAULT_HEADER_MAGIC;
    words[1] = obj_id;
    words[2] = (uint32_t)type;
    words[3] = 0;
    words[4] = 0;

    memcpy(bitmap_before, vault_base + sizeof(uint32_t), BITMAP_SIZE);

    delete_object(type, tok_id, obj_id);

    ck_assert_mem_eq(vault_base + sizeof(uint32_t), bitmap_before, BITMAP_SIZE);
    ck_assert_uint_eq(((uint32_t *)vault_base)[0], VAULT_HEADER_MAGIC);
    ck_assert_uint_eq(((uint32_t *)vault_base)[1], obj_id);
}
END_TEST

START_TEST(test_delete_object_corrupted_pos_no_oob)
{
    enum { type = WOLFPSA_STORE_KEY };
    const uint32_t tok_id = 0x0A0B0C0DU;
    const uint32_t obj_id = 0x10203040U;
    struct obj_hdr *hdr;
    int ret;

    ret = mmap_file("/tmp/wolfboot-unit-psa-keyvault.bin", vault_base,
        keyvault_size, NULL);
    ck_assert_int_eq(ret, 0);
    memset(vault_base, 0xFF, keyvault_size);

    /* Valid header magic and zeroed bitmap so check_vault() accepts the
     * sector without restoring/reinitializing it. */
    ((uint32_t *)vault_base)[0] = VAULT_HEADER_MAGIC;
    memset(vault_base + sizeof(uint32_t), 0x00, BITMAP_SIZE);

    /* Simulate a power-fault-corrupted node: valid tok/obj/type but the
     * 'pos' field was never written and is left as erased flash
     * (WOLFPSA_INVALID_ID). delete_object() must not turn this into an
     * out-of-bounds bitmap_put(0xFFFFFFFF, 0). */
    hdr = NODES_TABLE;
    hdr->token_id = tok_id;
    hdr->object_id = obj_id;
    hdr->type = type;
    hdr->pos = WOLFPSA_INVALID_ID;
    hdr->size = 2 * sizeof(uint32_t);

    delete_object(type, tok_id, obj_id);

    /* If we get here without a crash, the OOB write was avoided. The node
     * should also have been invalidated. */
    ck_assert_uint_eq(NODES_TABLE->token_id, WOLFPSA_INVALID_ID);
    ck_assert_uint_eq(NODES_TABLE->object_id, WOLFPSA_INVALID_ID);
}
END_TEST

START_TEST(test_find_object_search_stops_at_header_sector)
{
    enum { type = WOLFPSA_STORE_KEY };
    const uint32_t tok_id = 0x11223344U;
    const uint32_t obj_id = 0x55667788U;
    struct obj_hdr *backup_hdr;
    uint32_t *payload_ids;
    int ret;

    ret = mmap_file("/tmp/wolfboot-unit-psa-keyvault.bin", vault_base,
        keyvault_size, NULL);
    ck_assert_int_eq(ret, 0);
    memset(vault_base, 0xFF, keyvault_size);

    backup_hdr = (struct obj_hdr *)(vault_base + WOLFBOOT_SECTOR_SIZE);
    backup_hdr->token_id = tok_id;
    backup_hdr->object_id = obj_id;
    backup_hdr->type = type;
    backup_hdr->pos = 0;
    backup_hdr->size = 2 * sizeof(uint32_t);

    payload_ids = (uint32_t *)(vault_base + 2 * WOLFBOOT_SECTOR_SIZE);
    payload_ids[0] = tok_id;
    payload_ids[1] = obj_id;

    ck_assert_ptr_null(find_object_header(type, tok_id, obj_id));
    ck_assert_ptr_null(find_object_buffer(type, tok_id, obj_id));
}
END_TEST

START_TEST(test_shorter_overwrite_clears_tail)
{
    enum { type = WOLFPSA_STORE_KEY };
    const unsigned long id1 = 3;
    const unsigned long id2 = 4;
    void *store = NULL;
    unsigned char long_key[200];
    unsigned char short_key[32];
    uint8_t *obj_buf;
    int ret, i;

    ret = mmap_file("/tmp/wolfboot-unit-psa-keyvault.bin", vault_base,
        keyvault_size, NULL);
    ck_assert_int_eq(ret, 0);
    memset(vault_base, 0xFF, keyvault_size);

    memset(long_key,  0xAA, sizeof(long_key));
    memset(short_key, 0xBB, sizeof(short_key));

    /* First write: 200-byte key */
    ret = wolfPSA_Store_Open(type, id1, id2, 0, &store);
    ck_assert_int_eq(ret, 0);
    ret = wolfPSA_Store_Write(store, long_key, 200);
    ck_assert_int_eq(ret, 200);
    wolfPSA_Store_Close(store);

    /* Second write: 32-byte key (shorter than original) */
    ret = wolfPSA_Store_Open(type, id1, id2, 0, &store);
    ck_assert_int_eq(ret, 0);
    ret = wolfPSA_Store_Write(store, short_key, 32);
    ck_assert_int_eq(ret, 32);
    wolfPSA_Store_Close(store);

    /* Tail bytes (beyond the new key) must NOT contain the old 0xAA pattern */
    obj_buf = find_object_buffer(type, id1, id2);
    ck_assert_ptr_nonnull(obj_buf);
    for (i = 2 * (int)sizeof(uint32_t) + 32;
         i < 2 * (int)sizeof(uint32_t) + 200; i++) {
        ck_assert_msg(obj_buf[i] != 0xAA,
            "Old key material survives at offset %d: 0x%02x", i, obj_buf[i]);
    }
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot-psa-store");
    TCase *tcase_write = tcase_create("cross_sector_write");
    TCase *tcase_close = tcase_create("close_state");
    TCase *tcase_delete = tcase_create("delete_object");
    TCase *tcase_delete_corrupted = tcase_create("delete_corrupted_pos");
    TCase *tcase_find_bounds = tcase_create("find_bounds");
    TCase *tcase_tail = tcase_create("shorter_overwrite_clears_tail");

    tcase_add_test(tcase_write, test_cross_sector_write_preserves_length);
    tcase_add_test(tcase_close, test_close_clears_handle_state);
    tcase_add_test(tcase_delete, test_delete_object_ignores_metadata_prefix);
    tcase_add_test(tcase_delete_corrupted, test_delete_object_corrupted_pos_no_oob);
    tcase_add_test(tcase_find_bounds, test_find_object_search_stops_at_header_sector);
    tcase_add_test(tcase_tail, test_shorter_overwrite_clears_tail);
    suite_add_tcase(s, tcase_write);
    suite_add_tcase(s, tcase_close);
    suite_add_tcase(s, tcase_delete);
    suite_add_tcase(s, tcase_delete_corrupted);
    suite_add_tcase(s, tcase_find_bounds);
    suite_add_tcase(s, tcase_tail);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
