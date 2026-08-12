/* unit-tpm-mfgid-eh-zeroize.c
 *
 * Regression test for wolfBoot_tpm2_get_timestamp() in src/tpm.c leaving the
 * endorsement-hierarchy authValue in its stack-local WOLFTPM2_HANDLE when it
 * returns.  In derive mode (WOLFBOOT_TPM_MFG_AUTH_DERIVE) that value is the
 * per-device secret computed by wolfTPM2_SetIdentityAuth() from the reel
 * master secret, and it authorises use of the endorsement hierarchy.  The
 * function already scrubs the master secret from the stack and clears the
 * copies wolfTPM keeps in the device session slots (wolfTPM2_UnsetAuth()),
 * but the handle holding the derived value is left untouched, so it survives
 * in Secure stack SRAM after the non-secure entry veneer returns.
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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef SPI_CS_TPM
#define SPI_CS_TPM 1
#endif

#include "tpm.h"

/* Size and value of the authValue wolfTPM2_SetIdentityAuth() derives: the low
 * 16 bytes of SHA-256(TPM serial || master), see lib/wolfTPM/src/tpm2_wrap.c */
#define EH_AUTH_SZ 16
static const uint8_t derived_eh_auth[EH_AUTH_SZ] = {
    0x5A, 0xC3, 0x11, 0x92, 0x7E, 0x40, 0xB6, 0x08,
    0xD1, 0x2F, 0x63, 0xAA, 0x0C, 0x74, 0xE9, 0x35
};

/* The stack-local handle wolfBoot_tpm2_get_timestamp() derives into, captured
 * through the wolfTPM entry point it hands the handle to. */
static WOLFTPM2_HANDLE* captured_eh;
static int              gettime_rc;

/* Snapshot of the (now dead) frame, taken by the tests with an inline
 * volatile copy loop so no intervening call can reuse the stack first. */
static uint8_t snapshot_eh[sizeof(WOLFTPM2_HANDLE)];

void *cmse_check_address_range(void *ptr, size_t size, int flags)
{
    (void)size;
    (void)flags;
    return ptr;
}

int wolfBoot_printf(const char* fmt, ...)
{
    (void)fmt;
    return 0;
}

/* Stand-in for wolfTPM2_SetIdentityAuth(): the real one hashes the TPM serial
 * number with the master secret and stores the low 16 bytes of the digest in
 * the handle's auth field. */
int wolfTPM2_SetIdentityAuth(WOLFTPM2_DEV* dev, WOLFTPM2_HANDLE* handle,
    uint8_t* masterPassword, uint16_t masterPasswordSz)
{
    (void)dev;
    (void)masterPassword;
    (void)masterPasswordSz;

    captured_eh = handle;
    handle->auth.size = EH_AUTH_SZ;
    memcpy(handle->auth.buffer, derived_eh_auth, EH_AUTH_SZ);
    return 0;
}

int wolfTPM2_SetAuthHandle(WOLFTPM2_DEV* dev, int index,
    const WOLFTPM2_HANDLE* handle)
{
    (void)dev;
    (void)index;
    (void)handle;
    return 0;
}

int wolfTPM2_UnsetAuth(WOLFTPM2_DEV* dev, int index)
{
    if (dev == NULL || index < 0 || index >= MAX_SESSION_NUM) {
        return BAD_FUNC_ARG;
    }
    memset(&dev->session[index], 0, sizeof(dev->session[index]));
    return 0;
}

int wolfTPM2_GetTime(WOLFTPM2_KEY* aikKey, GetTime_Out* getTimeOut)
{
    (void)aikKey;
    (void)getTimeOut;
    return gettime_rc;
}

void TPM2_ForceZero(void* mem, word32 len)
{
    volatile uint8_t* p = (volatile uint8_t*)mem;
    word32 i;

    for (i = 0; i < len; i++)
        p[i] = 0;
}

#include "../../src/tpm.c"

/* Copy the dead frame without calling anything (a memcpy() or a helper
 * function would push its own frame over the bytes under test). */
#define SNAPSHOT_EH()                                                      \
    do {                                                                   \
        volatile const uint8_t* _h = (volatile const uint8_t*)captured_eh; \
        unsigned _i;                                                       \
        for (_i = 0; _i < sizeof(snapshot_eh); _i++) {                     \
            snapshot_eh[_i] = _h[_i];                                      \
        }                                                                  \
    } while (0)

static void assert_no_residue(void)
{
    unsigned i, j;

    for (i = 0; i + EH_AUTH_SZ <= sizeof(snapshot_eh); i++) {
        for (j = 0; j < EH_AUTH_SZ; j++) {
            if (snapshot_eh[i + j] != derived_eh_auth[j])
                break;
        }
        ck_assert_msg(j != EH_AUTH_SZ,
            "eh_handle still holds the derived EH authValue at offset %u", i);
    }
}

static void setup(void)
{
    captured_eh = NULL;
    gettime_rc = 0;
    memset(snapshot_eh, 0xFF, sizeof(snapshot_eh));
    memset(&wolftpm_dev, 0, sizeof(wolftpm_dev));
}

/* Normal return: the derived EH authValue must not survive the veneer. */
START_TEST(test_get_timestamp_wipes_eh_auth)
{
    WOLFTPM2_KEY aik;
    GetTime_Out getTime;
    int rc;

    memset(&aik, 0, sizeof(aik));
    rc = wolfBoot_tpm2_get_timestamp(&aik, &getTime);
    SNAPSHOT_EH();

    ck_assert_int_eq(rc, 0);
    ck_assert_ptr_ne(captured_eh, NULL);
    assert_no_residue();
}
END_TEST

/* Error return: TPM2_GetTime fails after the authValue was derived, so the
 * handle still holds it on the way out. */
START_TEST(test_get_timestamp_wipes_eh_auth_on_error)
{
    WOLFTPM2_KEY aik;
    GetTime_Out getTime;
    int rc;

    memset(&aik, 0, sizeof(aik));
    gettime_rc = TPM_RC_FAILURE;
    rc = wolfBoot_tpm2_get_timestamp(&aik, &getTime);
    SNAPSHOT_EH();

    ck_assert_int_ne(rc, 0);
    ck_assert_ptr_ne(captured_eh, NULL);
    assert_no_residue();
}
END_TEST

static Suite *tpm_mfgid_eh_zeroize_suite(void)
{
    Suite *s = suite_create("tpm_mfgid_eh_zeroize");
    TCase *tc = tcase_create("zeroize");
    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_get_timestamp_wipes_eh_auth);
    tcase_add_test(tc, test_get_timestamp_wipes_eh_auth_on_error);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int failed;
    Suite *s = tpm_mfgid_eh_zeroize_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
