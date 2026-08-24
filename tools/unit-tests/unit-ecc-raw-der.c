/* unit-ecc-raw-der.c
 *
 * Regression test for F-11024: the wolfHSM verify path of
 * wolfBoot_verify_signature_ecc() (src/image.c) converts the fixed-width
 * raw R||S signature to DER with wc_ecc_rs_raw_to_sig(). It passed the
 * minimal field sizes (mp_unsigned_bin_size) while leaving the pointers
 * at the start of each fixed-width field, so whenever R or S had a
 * leading zero byte the conversion encoded a zero-padded integer with
 * the low bytes truncated, and wc_ecc_verify_hash() rejected an
 * otherwise valid signature.
 *
 * The fix passes the full-width fields (point_sz for both) and drops
 * the now-unused multiprecision sizing. This test runs the real
 * wolfCrypt conversion and verification: it signs until a signature
 * with a leading zero shows up, then asserts that the minimal-size
 * pattern rejects it while the full-width pattern accepts the same
 * signature. Both patterns must also accept leading-zero-free
 * signatures, so the fix does not change the common case.
 *
 * Notes on the build: WOLFCRYPT_TEST pulls in the full ECC (sign and
 * verify) and the deterministic-k mode (WOLFSSL_ECDSA_SET_K), so the
 * digest varies per iteration to get independent signatures, and
 * wc_ecc_sign_hash() emits DER here and is decoded back to the
 * fixed-width raw layout wolfBoot stores in the image header. The
 * check harness does not keep globals between tests, so the search
 * runs in every setup.
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

#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/random.h>

/* The test build config wires the RNG block generator to this hook
 * (include/user_settings.h, CUSTOM_RAND_GENERATE_BLOCK); it only feeds
 * the DRBG seed, never the signature bytes. */
int my_rng_seed_gen(unsigned char* output, unsigned int sz)
{
    int i;

    for (i = 0; i < (int)sz; i++)
        output[i] = (unsigned char)(i * 37 + 11);
    return 0;
}

#define POINT_SZ  32
#define DIGEST_SZ 32
#define MAX_SIGS  1000

static WC_RNG g_rng;
static ecc_key g_ecc;
static byte g_digest[DIGEST_SZ];
static byte g_raw_sig[2 * POINT_SZ];
static byte g_search_digest[DIGEST_SZ];
static int g_have_leading_zero_sig;

static void find_leading_zero_sig(void);
static int sign_raw(const byte* digest);
static int leading_zeros(const byte* field);
static int convert_and_verify(const byte* raw, const byte* digest,
                              int full_width, int* valid);

/* Sign the digest and pack the result as fixed-width raw R||S, the
 * layout wolfBoot stores in the image header. wc_ecc_sign_hash() emits
 * DER in this build, so the components are decoded and re-padded. */
static int sign_raw(const byte* digest)
{
    byte der[255];
    byte r[POINT_SZ];
    byte s[POINT_SZ];
    word32 derSz = sizeof(der);
    word32 rSz = sizeof(r);
    word32 sSz = sizeof(s);
    int ret;

    ret = wc_ecc_sign_hash(digest, DIGEST_SZ, der, &derSz, &g_rng, &g_ecc);
    if (ret != 0)
        return ret;
    ret = wc_ecc_sig_to_rs(der, derSz, r, &rSz, s, &sSz);
    if (ret != 0)
        return ret;
    memset(g_raw_sig, 0, sizeof(g_raw_sig));
    memcpy(g_raw_sig + (POINT_SZ - rSz), r, rSz);
    memcpy(g_raw_sig + POINT_SZ + (POINT_SZ - sSz), s, sSz);
    return 0;
}

/* Number of leading zero bytes of a fixed-width field. */
static int leading_zeros(const byte* field)
{
    int n = 0;

    while ((n < POINT_SZ) && (field[n] == 0))
        n++;
    return n;
}

/* Convert raw R||S to DER and verify it against the digest the
 * signature was made over.
 * full_width: pass point_sz for both fields (the fix);
 * otherwise pass the minimal sizes with the field-start pointers
 * (the pre-fix pattern). */
static int convert_and_verify(const byte* raw, const byte* digest,
                              int full_width, int* valid)
{
    int ret;
    int res = 0;
    word32 rSz = POINT_SZ;
    word32 sSz = POINT_SZ;
    word32 derSz = 255;
    byte der[255];

    if (!full_width) {
        rSz = (word32)(POINT_SZ - leading_zeros(raw));
        sSz = (word32)(POINT_SZ - leading_zeros(raw + POINT_SZ));
    }
    ret = wc_ecc_rs_raw_to_sig(raw, rSz, raw + POINT_SZ, sSz, der, &derSz);
    if (ret != 0)
        return ret;

    ret = wc_ecc_verify_hash(der, derSz, digest, DIGEST_SZ, &res, &g_ecc);
    *valid = res;
    return ret;
}

/* Sign until a signature has a leading zero in R or S (the trigger for
 * the pre-fix bug); keep the signature in g_raw_sig and its digest in
 * g_search_digest. The build uses deterministic k (WOLFSSL_ECDSA_SET_K),
 * so the digest varies per iteration to get independent signatures.
 * With a ~1/128 chance per signature, 1000 iterations come up short
 * ~0.04% of the time; the dependent test skips itself in that case. */
static void find_leading_zero_sig(void)
{
    byte digest[DIGEST_SZ];
    int i;

    g_have_leading_zero_sig = 0;
    for (i = 0; i < MAX_SIGS; i++) {
        memcpy(digest, g_digest, sizeof(digest));
        digest[DIGEST_SZ - 1] = (byte)i;
        if (sign_raw(digest) != 0)
            return;
        if ((g_raw_sig[0] == 0) || (g_raw_sig[POINT_SZ] == 0)) {
            g_have_leading_zero_sig = 1;
            memcpy(g_search_digest, digest, sizeof(digest));
            return;
        }
    }
}

static void setup(void)
{
    int i;

    for (i = 0; i < DIGEST_SZ; i++)
        g_digest[i] = (byte)(i + 1);

    ck_assert_int_eq(wc_InitRng(&g_rng), 0);
    ck_assert_int_eq(wc_ecc_init(&g_ecc), 0);
    ck_assert_int_eq(wc_ecc_make_key(&g_rng, POINT_SZ, &g_ecc), 0);
    find_leading_zero_sig();
}

static void teardown(void)
{
    wc_ecc_free(&g_ecc);
    wc_FreeRng(&g_rng);
}

/* The full-width conversion verifies every signature, including the
 * leading-zero one: the fix is correct. */
START_TEST(test_full_width_always_verifies)
{
    int i;
    int valid;

    /* First: the search signature from setup, before the loop below
     * overwrites g_raw_sig. */
    if (g_have_leading_zero_sig) {
        ck_assert_int_eq(convert_and_verify(g_raw_sig, g_search_digest, 1,
                                            &valid), 0);
        ck_assert_int_eq(valid, 1);
    }

    for (i = 0; i < 16; i++) {
        ck_assert_int_eq(sign_raw(g_digest), 0);
        ck_assert_int_eq(convert_and_verify(g_raw_sig, g_digest, 1,
                                            &valid), 0);
        ck_assert_int_eq(valid, 1);
    }
}
END_TEST

/* The pre-fix pattern (minimal sizes, field-start pointers) rejects
 * the same leading-zero signature that the full-width pattern
 * accepts: the signature is valid, the conversion was wrong. */
START_TEST(test_minimal_pattern_rejects_leading_zero_sig)
{
    int valid;

    if (!g_have_leading_zero_sig) {
        printf("no leading-zero signature found, skipping\n");
        return;
    }

    ck_assert_int_eq(convert_and_verify(g_raw_sig, g_search_digest, 1,
                                        &valid), 0);
    ck_assert_int_eq(valid, 1);

    ck_assert_int_eq(convert_and_verify(g_raw_sig, g_search_digest, 0,
                                        &valid), 0);
    ck_assert_int_eq(valid, 0);
}
END_TEST

/* Without leading zeros both patterns agree and the signature
 * verifies: the common case is untouched by the fix. */
START_TEST(test_no_leading_zero_both_patterns_agree)
{
    byte digest[DIGEST_SZ];
    int valid;
    int i;
    int checked;

    for (i = 0, checked = 0; i < 8 && checked < 4; i++) {
        memcpy(digest, g_digest, sizeof(digest));
        digest[DIGEST_SZ - 1] = (byte)(0xC0 + i);
        ck_assert_int_eq(sign_raw(digest), 0);
        if ((g_raw_sig[0] == 0) || (g_raw_sig[POINT_SZ] == 0))
            continue;

        ck_assert_int_eq(convert_and_verify(g_raw_sig, digest, 1,
                                            &valid), 0);
        ck_assert_int_eq(valid, 1);
        ck_assert_int_eq(convert_and_verify(g_raw_sig, digest, 0,
                                            &valid), 0);
        ck_assert_int_eq(valid, 1);
        checked++;
    }
    ck_assert_int_gt(checked, 0);
}
END_TEST

Suite *ecc_raw_der_suite(void)
{
    Suite *s = suite_create("ecc-raw-der");
    TCase *tc = tcase_create("ecc-raw-der");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_full_width_always_verifies);
    tcase_add_test(tc, test_minimal_pattern_rejects_leading_zero_sig);
    tcase_add_test(tc, test_no_leading_zero_both_patterns_agree);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = ecc_raw_der_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
