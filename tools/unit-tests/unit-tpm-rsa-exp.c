/* unit-tpm-rsa-exp.c
 *
 * Unit tests for TPM RSA public-key loading.
 */

#include <check.h>
#include <stdint.h>
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

#include "wolfboot/wolfboot.h"
#include "keystore.h"
#include "tpm.h"

static uint8_t test_hdr[16];
static uint8_t test_modulus[256];
static uint8_t test_exponent_der[] = { 0xAA, 0x01, 0x00, 0x01, 0x7B };
static uint32_t captured_exponent;

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

#include "../../src/tpm.c"

static void setup(void)
{
    memset(test_hdr, 0x42, sizeof(test_hdr));
    memset(test_modulus, 0x5A, sizeof(test_modulus));
    captured_exponent = 0;
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

static Suite *tpm_suite(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("TPM RSA");
    tc = tcase_create("wolfBoot_load_pubkey");
    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_wolfBoot_load_pubkey_decodes_der_exponent_bytes);
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
