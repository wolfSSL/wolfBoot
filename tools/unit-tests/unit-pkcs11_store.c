/* unit-pkcs11_store.c
 *
 * Unit test for PKCS11 storage module
 *
 *
 * Copyright (C) 2024 wolfSSL Inc.
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

/* Option to enable sign tool debugging */
/* Must also define DEBUG_WOLFSSL in user_settings.h */
#define WOLFBOOT_HASH_SHA256
#define EXT_FLASH
#define PART_UPDATE_EXT
#define NVM_FLASH_WRITEONCE

#if defined(ENCRYPT_WITH_AES256) || defined(ENCRYPT_WITH_AES128)
    #define WOLFSSL_AES_COUNTER
    #define WOLFSSL_AES_DIRECT
#endif
#if defined(ENCRYPT_WITH_AES256)
    #define WOLFSSL_AES_256
#endif
#if defined(ENCRYPT_WITH_CHACHA)
    #define HAVE_CHACHA
#endif
#define ECC_TIMING_RESISTANT

#define KEYSTORE_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_ECC256


#include <stdio.h>
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
#include "wolfpkcs11/pkcs11.h"
#include "hal.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define MOCK_ADDRESS 0xCF000000
uint8_t *vault_base = (uint8_t *)MOCK_ADDRESS;
#include "unit-keystore.c"
#include "pkcs11_store.c"
const uint32_t keyvault_size = KEYVAULT_OBJ_SIZE * KEYVAULT_MAX_ITEMS + 2 * WOLFBOOT_SECTOR_SIZE;
#include "unit-mock-flash.c"

#include "txt_filler.h"

char dante_filler[KEYVAULT_OBJ_SIZE] = DANTE_FILLER;

START_TEST (test_store_and_load_objs) {
    CK_ULONG id_tok, id_obj;
    int type;
    int ret, readonly;
    void *store = NULL;
    char secret1[] = "Everyone gets Friday off.";
    char secret2[] = "This is just a test string.";
    char short_string[] = "Short string";
    char secret_rd[KEYVAULT_OBJ_SIZE];

    type = DYNAMIC_TYPE_ECC;
    id_tok = 1;
    id_obj = 12;
    readonly = 0;
    ret = mmap_file("/tmp/wolfboot-unit-keyvault.bin", vault_base,
            keyvault_size, NULL);
    ck_assert(ret == 0);
    memset(vault_base, 0xEE, keyvault_size);
    /* Open the vault, create the object */
    fprintf(stderr, "Opening the vault\n");
    printf("Flash Keyvault: %p\n", vault_base);
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret == 0, "Failed to open the vault: %d", ret);
    ck_assert_msg(store != NULL, "Did not receive a store address");
    fprintf(stderr, "open successful\n");

    /* Test two subsequent writes */
    ret = wolfPKCS11_Store_Write(store, secret1, strlen(secret1) + 1);
    ck_assert_int_eq(ret, strlen(secret1) + 1);
    ret = wolfPKCS11_Store_Write(store, secret2, strlen(secret2) + 1);
    ck_assert_int_eq(ret, strlen(secret2) + 1);
    wolfPKCS11_Store_Close(store);
    printf("Closed vault. Reopening in RO mode\n");

    /* Reopen for reading */
    readonly = 1;
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret == 0, "Failed to reopen the vault in read-only mode: %d", ret);

    /* Read out the content */
    ret = wolfPKCS11_Store_Read(store, secret_rd, 128);
    ck_assert(ret == strlen(secret1) + strlen(secret2) + 2);
    ck_assert(strcmp(secret1, secret_rd) == 0);
    ck_assert(strcmp(secret2, secret_rd + 1 + strlen(secret1)) == 0);
    wolfPKCS11_Store_Close(store);

    /* Create a second object with same Ids, different type*/
    type = DYNAMIC_TYPE_RSA;
    readonly = 0;
    fprintf(stderr, "Opening the second vault\n");
    printf("Flash Keyvault: %p\n", vault_base);
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret == 0, "Failed to open the 2nd vault: %d", ret);
    ck_assert_msg(store != NULL, "Did not receive a store address for 2nd vault");
    fprintf(stderr, "open 2 successful\n");
    ret = wolfPKCS11_Store_Write(store, secret2, strlen(secret2) + 1);
    wolfPKCS11_Store_Close(store);

    /* Reopen for reading */
    readonly = 1;
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret == 0, "Failed to reopen the vault in read-only mode: %d", ret);
    /* Read out the content */
    ret = wolfPKCS11_Store_Read(store, secret_rd, 128);
    ck_assert(ret == strlen(secret2) + 1);
    ck_assert(strcmp(secret2, secret_rd) == 0);
    wolfPKCS11_Store_Close(store);

    /* Create more similar objects, different secret */
    type = DYNAMIC_TYPE_RSA;
    id_tok = 2;
    id_obj = 22;
    readonly = 0;
    fprintf(stderr, "Creating one more vault\n");
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret == 0, "Failed to create vault: %d", ret);
    ck_assert_msg(store != NULL, "Did not receive a store address for vault");
    fprintf(stderr, "open 2 successful\n");
    ret = wolfPKCS11_Store_Write(store, secret1, strlen(secret1) + 1);

    id_tok = 3;
    id_obj = 23;
    readonly = 0;
    fprintf(stderr, "Creating one more vault\n");
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret == 0, "Failed to create vault: %d", ret);
    ck_assert_msg(store != NULL, "Did not receive a store address for vault");
    fprintf(stderr, "open 2 successful\n");
    ret = wolfPKCS11_Store_Write(store, secret1, strlen(secret1) + 1);
    wolfPKCS11_Store_Close(store);

    /* Reopen for reading */
    id_tok = 1;
    id_obj = 12;
    readonly = 1;
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret == 0, "Failed to reopen the vault in read-only mode: %d", ret);
    /* Read out the content */
    ret = wolfPKCS11_Store_Read(store, secret_rd, 128);
    ck_assert(ret == strlen(secret2) + 1);
    ck_assert(strcmp(secret2, secret_rd) == 0);
    wolfPKCS11_Store_Close(store);

    /* Open non-existing vaults */
    id_tok = 5;
    readonly = 1;
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret != 0, "Returned with success with invalid id_tok %d", id_tok);
    id_tok = 2;
    id_obj = 0;
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret != 0, "Returned with success with invalid id_obj %d", id_obj);
    type = 0xFF;
    id_tok = 2;
    id_obj = 23;
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret != 0, "Returned with success with invalid type %d", type);

    /* Test backup recovery for allocation table */
    memset(vault_base, 0xEE, WOLFBOOT_SECTOR_SIZE);
    type = DYNAMIC_TYPE_RSA;
    id_tok = 1;
    id_obj = 12;
    readonly = 1;
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret == 0, "Failed to reopen the vault recovering from alloc table backup: %d", ret);
    /* Read out the content */
    ret = wolfPKCS11_Store_Read(store, secret_rd, 128);
    ck_assert(ret == strlen(secret2) + 1);
    ck_assert(strcmp(secret2, secret_rd) == 0);
    wolfPKCS11_Store_Close(store);

    /* Test backup recovery for object sector */
    printf("Test recovery sector...\n");
    memcpy(vault_base + WOLFBOOT_SECTOR_SIZE, vault_base + 0x1800,
            WOLFBOOT_SECTOR_SIZE);
    memset(vault_base + 0x1800, 0xEE, WOLFBOOT_SECTOR_SIZE);
    id_tok = 1;
    id_obj = 12;
    readonly = 1;
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret == 0, "Failed to reopen the vault recovering from object sector backup: %d", ret);
    /* Read out the content */
    ret = wolfPKCS11_Store_Read(store, secret_rd, 128);
    ck_assert(ret == strlen(secret2) + 1);
    ck_assert(strcmp(secret2, secret_rd) == 0);
    wolfPKCS11_Store_Close(store);

    /* Test with very large payload */
    type = DYNAMIC_TYPE_RSA;
    id_tok = 3;
    id_obj = 33;
    readonly = 0;
    fprintf(stderr, "Creating one BIG vault\n");
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret == 0, "Failed to create vault: %d", ret);
    ck_assert_msg(store != NULL, "Did not receive a store address for vault");
    fprintf(stderr, "open 3.33 successful\n");
    ret = wolfPKCS11_Store_Write(store, dante_filler, strlen(dante_filler) + 1);
    wolfPKCS11_Store_Close(store);

    /* Reopen for reading */
    readonly = 1;
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret == 0, "Failed to reopen the vault in read-only mode: %d", ret);
    /* Read out the content */
    memset(secret_rd, 0, KEYVAULT_OBJ_SIZE);
    ret = wolfPKCS11_Store_Read(store, secret_rd, KEYVAULT_OBJ_SIZE);
    ck_assert(ret == KEYVAULT_OBJ_SIZE - 8);
    ck_assert(strncmp(dante_filler, secret_rd, KEYVAULT_OBJ_SIZE - 8) == 0);
    wolfPKCS11_Store_Close(store);

    /* Reopen for writing, test truncate */
    readonly = 0;
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret == 0, "Failed to create vault: %d", ret);
    ck_assert_msg(store != NULL, "Did not receive a store address for vault");
    fprintf(stderr, "open 3.33 successful\n");
    ret = wolfPKCS11_Store_Write(store, short_string, strlen(short_string) + 1);
    wolfPKCS11_Store_Close(store);

    /* Reopen for reading */
    readonly = 1;
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_msg(ret == 0, "Failed to reopen the vault in read-only mode: %d", ret);
    /* Read out the content */
    memset(secret_rd, 0, KEYVAULT_OBJ_SIZE);
    ret = wolfPKCS11_Store_Read(store, secret_rd, KEYVAULT_OBJ_SIZE);
    ck_assert(ret == strlen(short_string) + 1);
    ck_assert(strcmp(short_string, secret_rd) == 0);
    wolfPKCS11_Store_Close(store);

    /* Remove the object and confirm it is no longer addressable */
    ret = wolfPKCS11_Store_Remove(type, id_tok, id_obj);
    ck_assert_msg(ret == 0, "Failed to delete vault: %d", ret);

    readonly = 1;
    ret = wolfPKCS11_Store_Open(type, id_tok, id_obj, readonly, &store);
    ck_assert_int_eq(ret, NOT_AVAILABLE_E);

    /* Second removal attempt should report the object is already gone */
    ret = wolfPKCS11_Store_Remove(type, id_tok, id_obj);
    ck_assert_int_eq(ret, NOT_AVAILABLE_E);
}
END_TEST

Suite *wolfboot_suite(void)
{
    /* Suite initialization */
    Suite *s = suite_create("wolfBoot-pkcs11-store");

    TCase* tcase_store_and_load_objs = tcase_create("store_and_load_objs");
    tcase_add_test(tcase_store_and_load_objs, test_store_and_load_objs);
    suite_add_tcase(s, tcase_store_and_load_objs);
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
