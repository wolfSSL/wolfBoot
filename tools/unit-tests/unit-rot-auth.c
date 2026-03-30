/* unit-rot-auth.c
 *
 * Unit tests for TPM ROT auth validation.
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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <wolfssl/wolfcrypt/settings.h>
#include <wolftpm/tpm2_wrap.h>
#include "tpm.h"

static uint8_t test_pubkey[32];
static int symmetric_corrupted;

#define TPM2_IoCb NULL
#define XSTRTOL strtol

int wolfTPM2_Init(WOLFTPM2_DEV* dev, TPM2HalIoCb ioCb, void* userCtx)
{
    (void)dev;
    (void)ioCb;
    (void)userCtx;
    return TPM_RC_SUCCESS;
}

int wolfTPM2_StartSession(WOLFTPM2_DEV* dev, WOLFTPM2_SESSION* session,
    WOLFTPM2_KEY* tpmKey, WOLFTPM2_HANDLE* bind, TPM_SE sesType,
    int encDecAlg)
{
    (void)dev;
    (void)tpmKey;
    (void)bind;
    (void)sesType;
    (void)encDecAlg;
    session->handle.hndl = 1;
    return 0;
}

int wolfTPM2_SetAuthSession(WOLFTPM2_DEV* dev, int index,
    WOLFTPM2_SESSION* session, TPMA_SESSION sessionAttributes)
{
    (void)dev;
    (void)index;
    (void)session;
    (void)sessionAttributes;
    return 0;
}

int wolfTPM2_UnloadHandle(WOLFTPM2_DEV* dev, WOLFTPM2_HANDLE* handle)
{
    (void)dev;
    (void)handle;
    return 0;
}

int wolfTPM2_Cleanup(WOLFTPM2_DEV* dev)
{
    (void)dev;
    return 0;
}

int wolfTPM2_NVReadPublic(WOLFTPM2_DEV* dev, TPM_HANDLE nvIndex,
    TPMS_NV_PUBLIC* nvPublic)
{
    (void)dev;
    (void)nvIndex;
    memset(nvPublic, 0, sizeof(*nvPublic));
    nvPublic->dataSize = 32;
    return 0;
}

int wolfTPM2_NVReadAuth(WOLFTPM2_DEV* dev, WOLFTPM2_NV* nv, TPM_HANDLE nvIndex,
    uint8_t* dataBuf, uint32_t* dataSz, uint32_t offset)
{
    TPMT_SYM_DEF zero_sym;

    (void)dev;
    (void)nv;
    (void)nvIndex;
    (void)offset;
    memset(&zero_sym, 0, sizeof(zero_sym));
    symmetric_corrupted =
        memcmp(&nv->handle.symmetric, &zero_sym, sizeof(zero_sym)) != 0;
    memset(dataBuf, 0xA5, *dataSz);
    return 0;
}

int wolfTPM2_GetNvAttributesTemplate(TPMI_RH_NV_AUTH authHandle,
    word32* nvAttributes)
{
    (void)authHandle;
    *nvAttributes = 0;
    return 0;
}

int wolfTPM2_NVCreateAuth(WOLFTPM2_DEV* dev, WOLFTPM2_HANDLE* parent,
    WOLFTPM2_NV* nv, TPM_HANDLE nvIndex, word32 nvAttributes, word32 nvSize,
    const uint8_t* auth, int authSz)
{
    (void)dev;
    (void)parent;
    (void)nv;
    (void)nvIndex;
    (void)nvAttributes;
    (void)nvSize;
    (void)auth;
    (void)authSz;
    return 0;
}

int wolfTPM2_NVWriteAuth(WOLFTPM2_DEV* dev, WOLFTPM2_NV* nv, TPM_HANDLE nvIndex,
    uint8_t* dataBuf, word32 dataSz, word32 offset)
{
    (void)dev;
    (void)nv;
    (void)nvIndex;
    (void)dataBuf;
    (void)dataSz;
    (void)offset;
    return 0;
}

int wolfTPM2_NVWriteLock(WOLFTPM2_DEV* dev, WOLFTPM2_NV* nv)
{
    (void)dev;
    (void)nv;
    return 0;
}

const char* TPM2_GetAlgName(TPM_ALG_ID alg)
{
    (void)alg;
    return "stub";
}

const char* wolfTPM2_GetRCString(int rc)
{
    (void)rc;
    return "stub";
}

void TPM2_PrintBin(const uint8_t* buffer, uint32_t length)
{
    (void)buffer;
    (void)length;
}

int keystore_num_pubkeys(void)
{
    return 1;
}

uint32_t keystore_get_key_type(int id)
{
    (void)id;
    return 0;
}

int keystore_get_size(int id)
{
    (void)id;
    return (int)sizeof(test_pubkey);
}

uint8_t* keystore_get_buffer(int id)
{
    (void)id;
    return test_pubkey;
}

int wc_HashGetDigestSize(enum wc_HashType hash_type)
{
    (void)hash_type;
    return 32;
}

int wc_Hash(enum wc_HashType hash_type, const byte* data, word32 len, byte* hash,
    word32 hash_len)
{
    (void)hash_type;
    (void)data;
    (void)len;
    memset(hash, 0x5A, hash_len);
    return 0;
}

int printf(const char* fmt, ...)
{
    (void)fmt;
    return 0;
}

#define main rot_tool_main
#include "../tpm/rot.c"
#undef main

START_TEST(test_rot_rejects_oversized_auth)
{
    char auth[sizeof(((WOLFTPM2_NV*)0)->handle.auth.buffer) + 2];
    int rc;

    memset(test_pubkey, 0x11, sizeof(test_pubkey));
    symmetric_corrupted = 0;
    memset(auth, 'A', sizeof(auth) - 1);
    auth[sizeof(auth) - 1] = '\0';

    rc = TPM2_Boot_SecureROT_Example(TPM_RH_PLATFORM,
        WOLFBOOT_TPM_KEYSTORE_NV_BASE, WC_HASH_TYPE_SHA256, 0, 0, auth,
        (int)strlen(auth));

    ck_assert_int_eq(symmetric_corrupted, 0);
    ck_assert_int_eq(rc, BAD_FUNC_ARG);
}
END_TEST

static Suite* rot_auth_suite(void)
{
    Suite* s;
    TCase* tc;

    s = suite_create("rot_auth");
    tc = tcase_create("auth_validation");
    tcase_add_test(tc, test_rot_rejects_oversized_auth);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite* s;
    SRunner* sr;
    int failures;

    s = rot_auth_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failures = srunner_ntests_failed(sr);
    srunner_free(sr);

    return failures == 0 ? 0 : 1;
}
