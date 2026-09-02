/* unit-pkcs11-pin-zeroize.c
 *
 * Unit test for the PKCS#11 login credential lifetime (F-12114).
 *
 * pkcs11_pin is a file-scope copy of the credential supplied to
 * C_Login() for the token holding the firmware-decryption key.
 * pkcs11_crypto_deinit() runs on the pre-handoff path and must
 * erase that copy: the bootloader memory is retained after the
 * handoff, and a live credential there lets an attacker
 * authenticate to the token.
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>

#include "target.h"

#define EXT_ENCRYPTED
#define ENCRYPT_PKCS11 1
#define ENCRYPT_PKCS11_PIN "w0lfboot-pin"
#define ENCRYPT_PKCS11_MECHANISM CKM_AES_CTR
#define ENCRYPT_PKCS11_BLOCK_SIZE 16
#define ENCRYPT_PKCS11_KEY_ID_SIZE 16
#define ENCRYPT_PKCS11_NONCE_SIZE 16

#include "user_settings.h"
#include "wolfboot/wolfboot.h"
/* pulls in the PKCS#11 types (via wcs_pkcs11.h) before the stubs */
#include "encrypt.h"

/* the key id object the UNIT_TEST build points key_id at; the nonce
 * lives directly after the key id, as in the real layout */
static uint8_t test_encrypt_key[ENCRYPT_PKCS11_KEY_ID_SIZE +
                                ENCRYPT_PKCS11_NONCE_SIZE];
#define ENCRYPT_KEY test_encrypt_key

/* ---- PKCS#11 stubs ---- */

static int stub_close_session_calls;

static CK_RV stub_C_Initialize(CK_VOID_PTR pInitArgs)
{
    (void)pInitArgs;
    return CKR_OK;
}

static CK_RV stub_C_Finalize(CK_VOID_PTR pReserved)
{
    (void)pReserved;
    return CKR_OK;
}

static CK_RV stub_C_OpenSession(CK_SLOT_ID slotID, CK_FLAGS flags,
                                CK_VOID_PTR pApplication,
                                CK_NOTIFY Notify,
                                CK_SESSION_HANDLE_PTR phSession)
{
    (void)slotID;
    (void)flags;
    (void)pApplication;
    (void)Notify;
    *phSession = 1;
    return CKR_OK;
}

static CK_RV stub_C_CloseSession(CK_SESSION_HANDLE hSession)
{
    (void)hSession;
    stub_close_session_calls++;
    return CKR_OK;
}

static CK_RV stub_C_Login(CK_SESSION_HANDLE hSession, CK_USER_TYPE userType,
                          CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen)
{
    (void)hSession;
    (void)userType;
    (void)pPin;
    (void)ulPinLen;
    return CKR_OK;
}

static CK_RV stub_C_Logout(CK_SESSION_HANDLE hSession)
{
    (void)hSession;
    return CKR_OK;
}

static CK_RV stub_C_FindObjectsInit(CK_SESSION_HANDLE hSession,
                                    CK_ATTRIBUTE_PTR pTemplate,
                                    CK_ULONG ulCount)
{
    (void)hSession;
    (void)pTemplate;
    (void)ulCount;
    return CKR_OK;
}

static CK_RV stub_C_FindObjects(CK_SESSION_HANDLE hSession,
                                CK_OBJECT_HANDLE_PTR phObject,
                                CK_ULONG maxObjectSize,
                                CK_ULONG_PTR pulObjectCount)
{
    (void)hSession;
    (void)phObject;
    (void)maxObjectSize;
    *pulObjectCount = 1;
    return CKR_OK;
}

static CK_RV stub_C_FindObjectsFinal(CK_SESSION_HANDLE hSession)
{
    (void)hSession;
    return CKR_OK;
}

static CK_FUNCTION_LIST stub_function_list;

static void init_stub_function_list(void)
{
    memset(&stub_function_list, 0, sizeof(stub_function_list));
    stub_function_list.C_Initialize = stub_C_Initialize;
    stub_function_list.C_Finalize = stub_C_Finalize;
    stub_function_list.C_OpenSession = stub_C_OpenSession;
    stub_function_list.C_CloseSession = stub_C_CloseSession;
    stub_function_list.C_Login = stub_C_Login;
    stub_function_list.C_Logout = stub_C_Logout;
    stub_function_list.C_FindObjectsInit = stub_C_FindObjectsInit;
    stub_function_list.C_FindObjects = stub_C_FindObjects;
    stub_function_list.C_FindObjectsFinal = stub_C_FindObjectsFinal;
}

CK_RV C_GetFunctionList(CK_FUNCTION_LIST_PTR *ppFunctionList)
{
    *ppFunctionList = &stub_function_list;
    return CKR_OK;
}

void hal_trng_init(void)
{
}

void panic(void)
{
    ck_abort_msg("panic!");
}

#include "libwolfboot.c"

static void reset_stub_state(void)
{
    stub_close_session_calls = 0;
}

/* F-12114: the pre-handoff deinitializer must erase the PKCS#11
 * login credential.  The pin copy starts with the compile-time
 * credential; after a full init/deinit cycle every byte of it must
 * be zero, and the session must have been closed. */
START_TEST(test_pkcs11_pin_wiped_on_deinit){
    int ret;
    size_t i;

    reset_stub_state();
    ck_assert_mem_eq(pkcs11_pin, ENCRYPT_PKCS11_PIN,
                     sizeof(ENCRYPT_PKCS11_PIN));

    ret = pkcs11_crypto_init();
    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(encrypt_initialized, 1);

    pkcs11_crypto_deinit();

    ck_assert_int_eq(encrypt_initialized, 0);
    ck_assert_int_eq(stub_close_session_calls, 1);
    for (i = 0; i < sizeof(pkcs11_pin); i++) {
        ck_assert_msg(pkcs11_pin[i] == 0,
                      "pkcs11_pin byte %zu not wiped", i);
    }
}
END_TEST

/* deinit without an established session must not touch the token
 * (no C_CloseSession), is safe to call repeatedly, and still wipes
 * the pre-populated credential copy. */
START_TEST(test_pkcs11_deinit_no_session)
{
    size_t i;

    reset_stub_state();
    encrypt_initialized = 0;
    memcpy(pkcs11_pin, ENCRYPT_PKCS11_PIN, sizeof(ENCRYPT_PKCS11_PIN));

    pkcs11_crypto_deinit();
    pkcs11_crypto_deinit();

    ck_assert_int_eq(encrypt_initialized, 0);
    ck_assert_int_eq(stub_close_session_calls, 0);
    for (i = 0; i < sizeof(pkcs11_pin); i++) {
        ck_assert_msg(pkcs11_pin[i] == 0,
                      "pkcs11_pin byte %zu not wiped", i);
    }
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-pkcs11-pin");
    TCase *tc = tcase_create("pkcs11-pin-zeroize");

    tcase_add_test(tc, test_pkcs11_pin_wiped_on_deinit);
    tcase_add_test(tc, test_pkcs11_deinit_no_session);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s;
    SRunner *sr;

    init_stub_function_list();
    s = wolfboot_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
