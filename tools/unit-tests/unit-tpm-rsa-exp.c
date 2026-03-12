/* unit-tpm-rsa-exp.c
 *
 * Unit tests for TPM RSA public-key loading.
 */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef SPI_CS_TPM
#define SPI_CS_TPM 1
#endif
#ifndef WOLFBOOT_SHA_DIGEST_SIZE
#define WOLFBOOT_SHA_DIGEST_SIZE 32
#endif
#ifndef WOLFBOOT_TPM_HASH_ALG
#define WOLFBOOT_TPM_HASH_ALG TPM_ALG_SHA256
#endif
#define WOLFBOOT_TPM_KEYSTORE

#include "wolfboot/wolfboot.h"
#include "keystore.h"
#include "tpm.h"

static uint8_t test_hdr[16];
static uint8_t test_modulus[256];
static uint8_t test_exponent_der[] = { 0xAA, 0x01, 0x00, 0x01, 0x7B };
static uint8_t test_nv_digest[WOLFBOOT_SHA_DIGEST_SIZE];
static uint32_t captured_exponent;
static int forbidden_memcmp_calls;

int keyslot_id_by_sha(const uint8_t* pubkey_hint)
{
    (void)pubkey_hint;
    return 0;
}

uint32_t keystore_get_key_type(int id)
{
    ck_assert_int_eq(id, 0);
    return AUTH_KEY_RSA2048;
}

uint8_t *keystore_get_buffer(int id)
{
    ck_assert_int_eq(id, 0);
    return test_hdr;
}

int keystore_get_size(int id)
{
    ck_assert_int_eq(id, 0);
    return (int)sizeof(test_hdr);
}

int wc_RsaPublicKeyDecode_ex(const byte* input, word32* inOutIdx, word32 inSz,
    const byte** n, word32* nSz, const byte** e, word32* eSz)
{
    (void)input;
    (void)inSz;

    *inOutIdx = 0;
    *n = test_modulus;
    *nSz = sizeof(test_modulus);
    *e = &test_exponent_der[1];
    *eSz = 3;
    return 0;
}

int wolfTPM2_LoadRsaPublicKey_ex(WOLFTPM2_DEV* dev, WOLFTPM2_KEY* key,
    const byte* rsaPub, word32 rsaPubSz, word32 exponent,
    TPM_ALG_ID scheme, TPMI_ALG_HASH hashAlg)
{
    (void)dev;
    (void)key;
    (void)rsaPub;
    (void)rsaPubSz;
    (void)scheme;
    (void)hashAlg;

    captured_exponent = exponent;
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

int wolfTPM2_SetAuthSession(WOLFTPM2_DEV* dev, int index,
    WOLFTPM2_SESSION* tpmSession, TPMA_SESSION sessionAttributes)
{
    (void)dev;
    (void)index;
    (void)tpmSession;
    (void)sessionAttributes;
    return 0;
}

int wolfTPM2_UnsetAuth(WOLFTPM2_DEV* dev, int index)
{
    (void)dev;
    (void)index;
    return 0;
}

int wolfTPM2_UnsetAuthSession(WOLFTPM2_DEV* dev, int index,
    WOLFTPM2_SESSION* tpmSession)
{
    (void)dev;
    (void)index;
    (void)tpmSession;
    return 0;
}

int wolfTPM2_NVReadAuth(WOLFTPM2_DEV* dev, WOLFTPM2_NV* nv,
    word32 nvIndex, byte* dataBuf, word32* pDataSz, word32 offset)
{
    (void)dev;
    (void)nv;
    (void)nvIndex;
    (void)offset;
    ck_assert_uint_eq(*pDataSz, WOLFBOOT_SHA_DIGEST_SIZE);
    memcpy(dataBuf, test_nv_digest, WOLFBOOT_SHA_DIGEST_SIZE);
    *pDataSz = WOLFBOOT_SHA_DIGEST_SIZE;
    return 0;
}

const char* wolfTPM2_GetRCString(int rc)
{
    (void)rc;
    return "mock";
}

int ConstantCompare(const byte* a, const byte* b, int length)
{
    int diff = 0;
    int i;

    for (i = 0; i < length; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff;
}

static int forbidden_memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *lhs = (const uint8_t *)a;
    const uint8_t *rhs = (const uint8_t *)b;
    size_t i;

    forbidden_memcmp_calls++;
    for (i = 0; i < n; i++) {
        if (lhs[i] != rhs[i])
            return (int)lhs[i] - (int)rhs[i];
    }
    return 0;
}

#define memcmp forbidden_memcmp
#include "../../src/tpm.c"
#undef memcmp

static void setup(void)
{
    memset(test_hdr, 0x42, sizeof(test_hdr));
    memset(test_modulus, 0x5A, sizeof(test_modulus));
    memset(test_nv_digest, 0x7C, sizeof(test_nv_digest));
    captured_exponent = 0;
    forbidden_memcmp_calls = 0;
}

START_TEST(test_wolfBoot_load_pubkey_decodes_der_exponent_bytes)
{
    uint8_t hint[WOLFBOOT_SHA_DIGEST_SIZE] = { 0 };
    WOLFTPM2_KEY key;
    TPM_ALG_ID alg = TPM_ALG_NULL;
    int rc;

    memset(&key, 0, sizeof(key));

    rc = wolfBoot_load_pubkey(hint, &key, &alg);

    ck_assert_int_eq(rc, 0);
    ck_assert_int_eq(alg, TPM_ALG_RSA);
    ck_assert_uint_eq(captured_exponent, 65537U);
}
END_TEST

START_TEST(test_wolfBoot_check_rot_avoids_memcmp_on_digest_compare)
{
    uint8_t hint[WOLFBOOT_SHA_DIGEST_SIZE];
    int rc;

    memcpy(hint, test_nv_digest, sizeof(hint));

    rc = wolfBoot_check_rot(0, hint);

    ck_assert_int_eq(rc, 0);
    ck_assert_int_eq(forbidden_memcmp_calls, 0);
}
END_TEST

static Suite *tpm_suite(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("TPM RSA");
    tc = tcase_create("wolfBoot_load_pubkey");
    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_wolfBoot_load_pubkey_decodes_der_exponent_bytes);
    tcase_add_test(tc, test_wolfBoot_check_rot_avoids_memcmp_on_digest_compare);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s;
    SRunner *sr;
    int failed;

    s = tpm_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
