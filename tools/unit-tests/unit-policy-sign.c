#include <check.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolftpm/tpm2_wrap.h>
#include "tpm.h"

#ifdef wc_InitRng
#undef wc_InitRng
#endif
#ifdef wc_FreeRng
#undef wc_FreeRng
#endif

static int policy_pcr_make_calls;
static int policy_ref_make_calls;
static int hash_digest_size_calls;
static int rng_init_calls;

int wolfTPM2_PolicyPCRMake(TPM_ALG_ID pcrAlg, byte* pcrArray, word32 pcrArraySz,
    const byte* pcrDigest, word32 pcrDigestSz, byte* digest, word32* digestSz)
{
    (void)pcrAlg;
    (void)pcrArray;
    (void)pcrArraySz;
    (void)pcrDigest;
    (void)pcrDigestSz;
    (void)digest;
    (void)digestSz;
    policy_pcr_make_calls++;
    return -200;
}

int wolfTPM2_PolicyRefMake(TPM_ALG_ID pcrAlg, byte* digest, word32* digestSz,
    const byte* policyRef, word32 policyRefSz)
{
    (void)pcrAlg;
    (void)digest;
    (void)digestSz;
    (void)policyRef;
    (void)policyRefSz;
    policy_ref_make_calls++;
    return -201;
}

int TPM2_GetHashDigestSize(TPMI_ALG_HASH hashAlg)
{
    (void)hashAlg;
    hash_digest_size_calls++;
    return 32;
}

const char* TPM2_GetAlgName(TPM_ALG_ID alg)
{
    (void)alg;
    return "SHA256";
}

const char* wolfTPM2_GetRCString(int rc)
{
    (void)rc;
    return "stub";
}

int wc_InitRng(WC_RNG* rng)
{
    (void)rng;
    rng_init_calls++;
    return -202;
}

int wc_FreeRng(WC_RNG* rng)
{
    (void)rng;
    return 0;
}

int wc_ecc_init(ecc_key* key)
{
    (void)key;
    return 0;
}

int wc_ecc_free(ecc_key* key)
{
    (void)key;
    return 0;
}

int wc_ecc_import_unsigned(ecc_key* key, const byte* qx, const byte* qy,
    const byte* d, int curve_id)
{
    (void)key;
    (void)qx;
    (void)qy;
    (void)d;
    (void)curve_id;
    return 0;
}

int wc_ecc_sign_hash_ex(const byte* in, word32 inlen, WC_RNG* rng, ecc_key* key,
    mp_int* r, mp_int* s)
{
    (void)in;
    (void)inlen;
    (void)rng;
    (void)key;
    (void)r;
    (void)s;
    return 0;
}

int mp_init_multi(mp_int* mp1, mp_int* mp2, mp_int* mp3, mp_int* mp4,
    mp_int* mp5, mp_int* mp6)
{
    (void)mp1;
    (void)mp2;
    (void)mp3;
    (void)mp4;
    (void)mp5;
    (void)mp6;
    return 0;
}

int sp_unsigned_bin_size(const sp_int* a)
{
    (void)a;
    return 0;
}

int sp_to_unsigned_bin(const sp_int* a, byte* out)
{
    (void)a;
    (void)out;
    return 0;
}

void mp_clear(mp_int* a)
{
    (void)a;
}

#define main policy_sign_tool_main
#include "../tpm/policy_sign.c"
#undef main

static void setup(void)
{
    policy_pcr_make_calls = 0;
    policy_ref_make_calls = 0;
    hash_digest_size_calls = 0;
    rng_init_calls = 0;
}

static void make_oversized_hex_arg(char* dst, size_t dst_sz, const char* prefix)
{
    size_t prefix_len = strlen(prefix);
    size_t hex_len = (WC_MAX_DIGEST_SIZE * 2U) + 2U;

    ck_assert_uint_gt(dst_sz, prefix_len + hex_len);
    memcpy(dst, prefix, prefix_len);
    memset(dst + prefix_len, 'a', hex_len);
    dst[prefix_len + hex_len] = '\0';
}

START_TEST(test_policy_sign_rejects_oversized_pcr_digest)
{
    char arg[sizeof("-pcrdigest=") + (WC_MAX_DIGEST_SIZE * 2) + 3];
    char* argv[] = { (char*)"policy_sign", arg };
    int rc;

    make_oversized_hex_arg(arg, sizeof(arg), "-pcrdigest=");
    rc = policy_sign(2, argv);

    ck_assert_int_eq(rc, -1);
    ck_assert_int_eq(hash_digest_size_calls, 0);
    ck_assert_int_eq(policy_pcr_make_calls, 0);
    ck_assert_int_eq(policy_ref_make_calls, 0);
    ck_assert_int_eq(rng_init_calls, 0);
}
END_TEST

START_TEST(test_policy_sign_rejects_invalid_policy_digest_hex)
{
    char arg[] = "-policydigest=zz";
    char* argv[] = { (char*)"policy_sign", arg };
    int rc;

    rc = policy_sign(2, argv);

    ck_assert_int_eq(rc, -1);
    ck_assert_int_eq(hash_digest_size_calls, 0);
    ck_assert_int_eq(policy_pcr_make_calls, 0);
    ck_assert_int_eq(policy_ref_make_calls, 0);
    ck_assert_int_eq(rng_init_calls, 0);
}
END_TEST

static Suite* policy_sign_suite(void)
{
    Suite* s;
    TCase* tc;

    s = suite_create("policy_sign");
    tc = tcase_create("argument_validation");
    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_policy_sign_rejects_oversized_pcr_digest);
    tcase_add_test(tc, test_policy_sign_rejects_invalid_policy_digest_hex);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite* s;
    SRunner* sr;
    int failed;

    s = policy_sign_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
