/* unit-pkcs11-nsc-zeroize.c
 *
 * Unit test for the PKCS#11 non-secure-callable bounce buffers.
 *
 * The NSC veneers deep-copy every non-secure CK_ATTRIBUTE value and every
 * CK_MECHANISM parameter into secure-world heap before calling wolfPKCS11.
 * For key import (C_CreateObject with CKA_VALUE, or the RSA private
 * components) and for password-based derivation (CKM_PKCS5_PBKD2 pPassword)
 * those bounce buffers hold plaintext secrets, so they must be scrubbed before
 * being released back to the secure heap.
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
#include <stdio.h>
#include <string.h>

/*
 * Secure-world heap stand-in: a bump allocator whose backing store is never
 * reused or cleared, so whatever a veneer leaves behind on XFREE() is still
 * observable afterwards. That is exactly what a released heap block looks like
 * to the next allocation that recycles it, to a secure-world debugger, or to a
 * cold-boot dump.
 */
static uint8_t sec_pool[4096];
static size_t sec_pool_used;
static int malloc_fail_next;
static void *freed_ptrs[64];
static int freed_count;

static void *sec_malloc(size_t n)
{
    void *p;

    if (malloc_fail_next) {
        malloc_fail_next = 0;
        return NULL;
    }
    if (n == 0)
        n = 1;
    n = (n + 7U) & ~(size_t)7U;          /* keep allocations aligned */
    if (sec_pool_used + n > sizeof(sec_pool))
        return NULL;
    p = &sec_pool[sec_pool_used];
    sec_pool_used += n;
    return p;
}

#define XMALLOC_OVERRIDE
#define XMALLOC(n, h, t) sec_malloc((size_t)(n))
#define XFREE(p, h, t)   do { if ((p) != NULL && freed_count < 64) freed_ptrs[freed_count++] = (void *)(p); } while (0)
#define XREALLOC(p, n, h, t) NULL

#include "user_settings.h"
#include "wolfboot/wc_secure.h"
#include "wolfpkcs11/pkcs11.h"
#include "wolfboot/wcs_pkcs11.h"

/*
 * Simulated non-secure RAM. Only pointers fully inside this object pass the
 * CMSE attribution stub, like real NS memory on a TrustZone-M part.
 */
static union {
    uint8_t bytes[1024];
    CK_ATTRIBUTE tmpl[8];
    struct {
        CK_MECHANISM mech;
        CK_PKCS5_PBKD2_PARAMS2 params;
        struct C_DeriveKey_nsc_args args;
        CK_OBJECT_HANDLE hKey;
        CK_UTF8CHAR password[16];
    } derive;
} ns_mem;

void *cmse_check_address_range(void *ptr, size_t size, int flags)
{
    uint8_t *start = (uint8_t *)ptr;

    (void)flags;
    if (start == NULL)
        return NULL;
    if (size == 0)
        size = 1;
    if (start < ns_mem.bytes ||
            start + size > ns_mem.bytes + sizeof(ns_mem.bytes))
        return NULL;
    return ptr;
}

/* The 32-byte AES key the non-secure client imports. */
static const uint8_t secret_key[32] = {
    0x53, 0x45, 0x43, 0x52, 0x45, 0x54, 0x4b, 0x30,
    0x53, 0x45, 0x43, 0x52, 0x45, 0x54, 0x4b, 0x31,
    0x53, 0x45, 0x43, 0x52, 0x45, 0x54, 0x4b, 0x32,
    0x53, 0x45, 0x43, 0x52, 0x45, 0x54, 0x4b, 0x33
};

/* The PBKDF2 password the non-secure client derives from. */
static const uint8_t secret_pwd[16] = {
    0x50, 0x41, 0x53, 0x53, 0x77, 0x30, 0x72, 0x64,
    0x50, 0x41, 0x53, 0x53, 0x77, 0x30, 0x72, 0x65
};

/* Set when the wolfPKCS11 stub actually saw the secret in the secure copy. */
static int stub_saw_secret;

CK_RV C_CreateObject(CK_SESSION_HANDLE hSession, CK_ATTRIBUTE_PTR pTemplate,
        CK_ULONG ulCount, CK_OBJECT_HANDLE_PTR phObject)
{
    CK_ULONG i;

    (void)hSession;
    for (i = 0; i < ulCount; i++) {
        if (pTemplate[i].type == CKA_VALUE &&
                pTemplate[i].ulValueLen == sizeof(secret_key) &&
                memcmp(pTemplate[i].pValue, secret_key,
                    sizeof(secret_key)) == 0) {
            stub_saw_secret = 1;
        }
    }
    if (phObject != NULL)
        *phObject = 1;
    return CKR_OK;
}

CK_RV C_DeriveKey(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism,
        CK_OBJECT_HANDLE hBaseKey, CK_ATTRIBUTE_PTR pTemplate,
        CK_ULONG ulAttributeCount, CK_OBJECT_HANDLE_PTR phKey)
{
    CK_PKCS5_PBKD2_PARAMS2 *p;

    (void)hSession;
    (void)hBaseKey;
    (void)pTemplate;
    (void)ulAttributeCount;
    p = (CK_PKCS5_PBKD2_PARAMS2 *)pMechanism->pParameter;
    if (p->ulPasswordLen == sizeof(secret_pwd) &&
            memcmp(p->pPassword, secret_pwd, sizeof(secret_pwd)) == 0) {
        stub_saw_secret = 1;
    }
    if (phKey != NULL)
        *phKey = 1;
    return CKR_OK;
}

#include "../../src/pkcs11_callable.c"

static void reset_state(void)
{
    memset(sec_pool, 0, sizeof(sec_pool));
    sec_pool_used = 0;
    malloc_fail_next = 0;
    memset(freed_ptrs, 0, sizeof(freed_ptrs));
    freed_count = 0;
    stub_saw_secret = 0;
    memset(&ns_mem, 0, sizeof(ns_mem));
}

/* Return 1 if the released secure heap still contains 'secret'. */
static int pool_has(const uint8_t *secret, size_t len)
{
    size_t i;

    for (i = 0; i + len <= sec_pool_used; i++) {
        if (memcmp(&sec_pool[i], secret, len) == 0)
            return 1;
    }
    return 0;
}

/*
 * A non-secure client imports a symmetric key with C_CreateObject. The veneer
 * bounce-buffers CKA_VALUE into the secure heap; once the call completes that
 * buffer is freed and must no longer hold the key bytes.
 */
START_TEST(test_create_object_value_zeroized)
{
    CK_ATTRIBUTE *tmpl = ns_mem.tmpl;
    uint8_t *nsKey = ns_mem.bytes + 3 * sizeof(CK_ATTRIBUTE);
    CK_OBJECT_HANDLE *nsHandle;
    CK_OBJECT_CLASS *nsClass;
    CK_KEY_TYPE *nsType;
    CK_RV rv;

    reset_state();
    memcpy(nsKey, secret_key, sizeof(secret_key));
    nsClass = (CK_OBJECT_CLASS *)(nsKey + sizeof(secret_key));
    nsType = (CK_KEY_TYPE *)(nsClass + 1);
    nsHandle = (CK_OBJECT_HANDLE *)(nsType + 1);
    *nsClass = CKO_SECRET_KEY;
    *nsType = CKK_AES;

    tmpl[0].type = CKA_CLASS;
    tmpl[0].pValue = nsClass;
    tmpl[0].ulValueLen = sizeof(*nsClass);
    tmpl[1].type = CKA_KEY_TYPE;
    tmpl[1].pValue = nsType;
    tmpl[1].ulValueLen = sizeof(*nsType);
    tmpl[2].type = CKA_VALUE;
    tmpl[2].pValue = nsKey;
    tmpl[2].ulValueLen = sizeof(secret_key);

    rv = C_CreateObject_nsc_call(1, tmpl, 3, nsHandle);
    ck_assert_int_eq((int)rv, (int)CKR_OK);
    /* The secure copy really was made, so the pool did hold the key... */
    ck_assert_int_eq(stub_saw_secret, 1);
    /* ...and it must not survive the free. */
    ck_assert_int_eq(pool_has(secret_key, sizeof(secret_key)), 0);
}
END_TEST

/*
 * Same for the mechanism parameter path: CKM_PKCS5_PBKD2 carries the caller's
 * password, which nsc_mech_prepare() copies into its own secure buffer.
 */
START_TEST(test_mech_password_zeroized)
{
    CK_RV rv;

    reset_state();
    memcpy(ns_mem.derive.password, secret_pwd, sizeof(secret_pwd));
    ns_mem.derive.params.saltSource = CKZ_DATA_SPECIFIED;
    ns_mem.derive.params.iterations = 1000;
    ns_mem.derive.params.prf = CKP_PKCS5_PBKD2_HMAC_SHA256;
    ns_mem.derive.params.pPassword = ns_mem.derive.password;
    ns_mem.derive.params.ulPasswordLen = sizeof(secret_pwd);
    ns_mem.derive.mech.mechanism = CKM_PKCS5_PBKD2;
    ns_mem.derive.mech.pParameter = &ns_mem.derive.params;
    ns_mem.derive.mech.ulParameterLen = sizeof(ns_mem.derive.params);
    ns_mem.derive.args.hSession = 1;
    ns_mem.derive.args.pMechanism = &ns_mem.derive.mech;
    ns_mem.derive.args.phKey = &ns_mem.derive.hKey;

    rv = C_DeriveKey_nsc_call(&ns_mem.derive.args);
    ck_assert_int_eq((int)rv, (int)CKR_OK);
    ck_assert_int_eq(stub_saw_secret, 1);
    ck_assert_int_eq(pool_has(secret_pwd, sizeof(secret_pwd)), 0);
}
END_TEST

/*
 * Partial allocation failure: the pool is pre-filled with 0xDE so any block
 * the allocator hands out holds non-NULL garbage. If the snapshot
 * allocation fails while the work allocation succeeds, the cleanup must not
 * pass the indeterminate work[].pValue pointers to XFREE: every released
 * pointer must be one this allocator actually handed out.
 */
START_TEST(test_tmpl_partial_alloc_no_garbage_free)
{
    CK_ATTRIBUTE *tmpl = ns_mem.tmpl;
    uint8_t *nsKey = ns_mem.bytes + 3 * sizeof(CK_ATTRIBUTE);
    CK_OBJECT_HANDLE *nsHandle;
    CK_OBJECT_CLASS *nsClass;
    CK_KEY_TYPE *nsType;
    CK_RV rv;
    int i;

    reset_state();
    memset(sec_pool, 0xDE, sizeof(sec_pool));
    malloc_fail_next = 1;                       /* fail the snap alloc */
    memcpy(nsKey, secret_key, sizeof(secret_key));
    nsClass = (CK_OBJECT_CLASS *)(nsKey + sizeof(secret_key));
    nsType = (CK_KEY_TYPE *)(nsClass + 1);
    nsHandle = (CK_OBJECT_HANDLE *)(nsType + 1);
    *nsClass = CKO_SECRET_KEY;
    *nsType = CKK_AES;
    tmpl[0].type = CKA_CLASS;
    tmpl[0].pValue = nsClass;
    tmpl[0].ulValueLen = sizeof(*nsClass);
    tmpl[1].type = CKA_KEY_TYPE;
    tmpl[1].pValue = nsType;
    tmpl[1].ulValueLen = sizeof(*nsType);
    tmpl[2].type = CKA_VALUE;
    tmpl[2].pValue = nsKey;
    tmpl[2].ulValueLen = sizeof(secret_key);

    rv = C_CreateObject_nsc_call(1, tmpl, 3, nsHandle);
    ck_assert_int_eq((int)rv, (int)CKR_HOST_MEMORY);
    for (i = 0; i < freed_count; i++) {
        uint8_t *p = freed_ptrs[i];
        ck_assert_msg(p >= sec_pool && p < sec_pool + sizeof(sec_pool),
            "XFREE got indeterminate pointer %p", (void *)p);
    }
}
END_TEST

Suite *pkcs11_nsc_suite(void)
{
    Suite *s = suite_create("pkcs11-nsc-zeroize");
    TCase *tc = tcase_create("bounce-buffers");

    tcase_add_test(tc, test_create_object_value_zeroized);
    tcase_add_test(tc, test_mech_password_zeroized);
    tcase_add_test(tc, test_tmpl_partial_alloc_no_garbage_free);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    SRunner *sr = srunner_create(pkcs11_nsc_suite());

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (fails == 0) ? 0 : 1;
}
