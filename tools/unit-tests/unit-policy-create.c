#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolftpm/tpm2_wrap.h>
#include "tpm.h"

static int policy_pcr_make_calls;
static int policy_ref_make_calls;
static int hash_digest_size_calls;

#define TPM2_IoCb NULL
#define XSTRTOL strtol

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

int wolfTPM2_Init(WOLFTPM2_DEV* dev, TPM2HalIoCb ioCb, void* userCtx)
{
    (void)dev;
    (void)ioCb;
    (void)userCtx;
    return 0;
}

int wolfTPM2_PCRGetDigest(WOLFTPM2_DEV* dev, TPM_ALG_ID pcrAlg, byte* pcrArray,
    word32 pcrArraySz, byte* pcrDigest, word32* pcrDigestSz)
{
    (void)dev;
    (void)pcrAlg;
    (void)pcrArray;
    (void)pcrArraySz;
    (void)pcrDigest;
    (void)pcrDigestSz;
    return -202;
}

int wolfTPM2_Cleanup(WOLFTPM2_DEV* dev)
{
    (void)dev;
    return 0;
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

#define main policy_create_tool_main
#include "../tpm/policy_create.c"
#undef main

static void setup(void)
{
    policy_pcr_make_calls = 0;
    policy_ref_make_calls = 0;
    hash_digest_size_calls = 0;
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

START_TEST(test_policy_create_rejects_oversized_pcr_digest)
{
    char arg[sizeof("-pcrdigest=") + (WC_MAX_DIGEST_SIZE * 2) + 3];
    char* argv[] = { (char*)"policy_create", arg };
    int rc;

    make_oversized_hex_arg(arg, sizeof(arg), "-pcrdigest=");
    rc = policy_create_tool_main(2, argv);

    ck_assert_int_eq(rc, -1);
    ck_assert_int_eq(hash_digest_size_calls, 0);
    ck_assert_int_eq(policy_pcr_make_calls, 0);
    ck_assert_int_eq(policy_ref_make_calls, 0);
}
END_TEST

START_TEST(test_policy_create_rejects_invalid_pcr_digest_hex)
{
    char arg[] = "-pcrdigest=zz";
    char* argv[] = { (char*)"policy_create", arg };
    int rc;

    rc = policy_create_tool_main(2, argv);

    ck_assert_int_eq(rc, -1);
    ck_assert_int_eq(hash_digest_size_calls, 0);
    ck_assert_int_eq(policy_pcr_make_calls, 0);
    ck_assert_int_eq(policy_ref_make_calls, 0);
}
END_TEST

static Suite* policy_create_suite(void)
{
    Suite* s;
    TCase* tc;

    s = suite_create("policy_create");
    tc = tcase_create("argument_validation");
    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_policy_create_rejects_oversized_pcr_digest);
    tcase_add_test(tc, test_policy_create_rejects_invalid_pcr_digest_hex);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite* s;
    SRunner* sr;
    int failed;

    s = policy_create_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
