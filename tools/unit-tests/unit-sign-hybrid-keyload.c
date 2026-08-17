/* unit-sign-hybrid-keyload.c
 *
 * Unit test for sign tool secondary (hybrid) key load error handling.
 */

#include <check.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WOLFBOOT_HASH_SHA256
#define IMAGE_HEADER_SIZE 512

#define main wolfboot_sign_main
#include "../keytools/sign.c"
#undef main

static const char missing_key[] = "/nonexistent/wolfboot-secondary-key.der";

static int write_file(const char *path, const void *buf, size_t len)
{
    FILE *f = fopen(path, "wb");
    size_t written;

    if (f == NULL) {
        return -1;
    }

    written = fwrite(buf, 1, len, f);
    fclose(f);

    return written == len ? 0 : -1;
}

static void reset_cmd_defaults(void)
{
    memset(&CMD, 0, sizeof(CMD));
    CMD.sign = NO_SIGN;
    CMD.hash_algo = HASH_SHA256;
    CMD.partition_id = HDR_IMG_TYPE_APP;
    CMD.header_sz = IMAGE_HEADER_SIZE;
    CMD.fw_version = "1";
    CMD.no_ts = 1;
}

/* load_key() must not hand back an unset public key when the key file
 * cannot be opened at all. */
START_TEST(test_load_key_clears_pubkey_when_file_missing)
{
    uint8_t *key_buffer = NULL;
    uint32_t key_buffer_sz = 0;
    uint8_t sentinel = 0xA5;
    uint8_t *pubkey = &sentinel;
    uint32_t pubkey_sz = 0xDEADBEEFU;

    reset_cmd_defaults();
    CMD.hybrid = 1;
    CMD.secondary_sign = SIGN_ML_DSA;
    CMD.secondary_key_file = missing_key;

    ck_assert_ptr_null(load_key(&key_buffer, &key_buffer_sz, &pubkey,
        &pubkey_sz, 1));
    ck_assert_ptr_null(pubkey);
    ck_assert_uint_eq(pubkey_sz, 0);
}
END_TEST

/* load_key() must not hand back a dangling public key pointer when the key
 * file is readable but cannot be decoded. */
START_TEST(test_load_key_clears_pubkey_when_decode_fails)
{
    char tempdir[] = "/tmp/wolfboot-sign-XXXXXX";
    char key_path[PATH_MAX];
    uint8_t garbage[7];
    uint8_t *key_buffer = NULL;
    uint32_t key_buffer_sz = 0;
    uint8_t *pubkey = NULL;
    uint32_t pubkey_sz = 0;

    ck_assert_ptr_nonnull(mkdtemp(tempdir));
    snprintf(key_path, sizeof(key_path), "%s/secondary.der", tempdir);

    memset(garbage, 0x5A, sizeof(garbage));
    ck_assert_int_eq(write_file(key_path, garbage, sizeof(garbage)), 0);

    reset_cmd_defaults();
    CMD.hybrid = 1;
    CMD.secondary_sign = SIGN_ED25519;
    CMD.secondary_key_file = key_path;

    ck_assert_ptr_null(load_key(&key_buffer, &key_buffer_sz, &pubkey,
        &pubkey_sz, 1));
    ck_assert_ptr_null(pubkey);
    ck_assert_uint_eq(pubkey_sz, 0);

    unlink(key_path);
    rmdir(tempdir);
}
END_TEST

/* The sign tool must fail when the hybrid secondary key cannot be loaded,
 * instead of building a manifest out of an unset secondary public key. */
START_TEST(test_sign_main_fails_when_secondary_key_missing)
{
    char tempdir[] = "/tmp/wolfboot-sign-XXXXXX";
    char image_path[PATH_MAX];
    char key_path[PATH_MAX];
    uint8_t image_buf[] = { 0x01, 0x02, 0x03, 0x04 };
    uint8_t raw_pubkey[64]; /* ECC256 raw Qx + Qy */
    char *argv[8];

    ck_assert_ptr_nonnull(mkdtemp(tempdir));
    snprintf(image_path, sizeof(image_path), "%s/image.bin", tempdir);
    snprintf(key_path, sizeof(key_path), "%s/ecc256.raw", tempdir);

    memset(raw_pubkey, 0x11, sizeof(raw_pubkey));
    ck_assert_int_eq(write_file(image_path, image_buf, sizeof(image_buf)), 0);
    ck_assert_int_eq(write_file(key_path, raw_pubkey, sizeof(raw_pubkey)), 0);

    argv[0] = "sign";
    argv[1] = "--sha-only";
    argv[2] = "--ecc256";
    argv[3] = "--ml_dsa";
    argv[4] = image_path;
    argv[5] = key_path;
    argv[6] = (char *)missing_key;
    argv[7] = "1";

    exit(wolfboot_sign_main(8, argv));
}
END_TEST

/* Export a freshly generated ECC key as the raw Qx || Qy || d blob accepted
 * by load_key(). */
static int make_raw_ecc_key(int curve_id, int curve_sz, uint8_t *raw)
{
    WC_RNG rng;
    ecc_key ek;
    word32 qxSz = curve_sz, qySz = curve_sz, dSz = curve_sz;
    int ret;

    if (wc_InitRng(&rng) != 0) {
        return -1;
    }
    if (wc_ecc_init(&ek) != 0) {
        wc_FreeRng(&rng);
        return -1;
    }
    ret = wc_ecc_make_key_ex(&rng, curve_sz, &ek, curve_id);
    if (ret == 0) {
        ret = wc_ecc_export_private_raw(&ek, raw, &qxSz, raw + curve_sz, &qySz,
            raw + (curve_sz * 2), &dSz);
    }
    wc_ecc_free(&ek);
    wc_FreeRng(&rng);

    return ret;
}

/* Check a raw r || s signature against a raw Qx || Qy public key. */
static int verify_raw_ecc(int curve_id, int curve_sz, const uint8_t *pubkey,
    const uint8_t *signature, const uint8_t *digest, uint32_t digest_sz)
{
    ecc_key vk;
    mp_int r, s;
    int res = 0;
    int ret;

    if (wc_ecc_init(&vk) != 0) {
        return -1;
    }
    ret = wc_ecc_import_unsigned(&vk, (byte*)pubkey, (byte*)pubkey + curve_sz,
        NULL, curve_id);
    if (ret == 0) {
        mp_init(&r);
        mp_init(&s);
        mp_read_unsigned_bin(&r, signature, curve_sz);
        mp_read_unsigned_bin(&s, signature + curve_sz, curve_sz);
        ret = wc_ecc_verify_hash_ex(&r, &s, digest, digest_sz, &res, &vk);
        mp_clear(&r);
        mp_clear(&s);
    }
    wc_ecc_free(&vk);

    if (ret != 0) {
        return -1;
    }
    return res;
}

/* Hybrid signing loads both private keys before either signature is made, so
 * the secondary key must not land on top of the decoded primary key. */
START_TEST(test_hybrid_secondary_key_does_not_clobber_primary)
{
    char tempdir[] = "/tmp/wolfboot-sign-XXXXXX";
    char primary_path[PATH_MAX];
    char secondary_path[PATH_MAX];
    uint8_t primary_raw[66 * 3];   /* ECC521 Qx + Qy + d */
    uint8_t secondary_raw[32 * 3]; /* ECC256 Qx + Qy + d */
    uint8_t *kbuf = NULL, *kbuf2 = NULL;
    uint32_t kbuf_sz = 0, kbuf2_sz = 0;
    uint8_t *pubkey = NULL, *pubkey2 = NULL;
    uint32_t pubkey_sz = 0, pubkey_sz2 = 0;
    uint8_t digest[32];
    uint8_t signature[132];
    uint8_t signature2[64];
    uint32_t signature_sz = sizeof(signature);
    uint32_t signature_sz2 = sizeof(signature2);

    ck_assert_int_eq(make_raw_ecc_key(ECC_SECP521R1, 66, primary_raw), 0);
    ck_assert_int_eq(make_raw_ecc_key(ECC_SECP256R1, 32, secondary_raw), 0);

    ck_assert_ptr_nonnull(mkdtemp(tempdir));
    snprintf(primary_path, sizeof(primary_path), "%s/ecc521.raw", tempdir);
    snprintf(secondary_path, sizeof(secondary_path), "%s/ecc256.raw", tempdir);
    ck_assert_int_eq(write_file(primary_path, primary_raw,
        sizeof(primary_raw)), 0);
    ck_assert_int_eq(write_file(secondary_path, secondary_raw,
        sizeof(secondary_raw)), 0);

    reset_cmd_defaults();
    CMD.sign = SIGN_ECC521;
    CMD.key_file = primary_path;
    CMD.hybrid = 1;
    CMD.secondary_sign = SIGN_ECC256;
    CMD.secondary_key_file = secondary_path;

    ck_assert_ptr_nonnull(load_key(&kbuf, &kbuf_sz, &pubkey, &pubkey_sz, 0));
    ck_assert_ptr_nonnull(load_key(&kbuf2, &kbuf2_sz, &pubkey2, &pubkey_sz2,
        1));

    memset(digest, 0x5C, sizeof(digest));
    ck_assert_int_eq(sign_digest(CMD.sign, CMD.hash_algo, signature,
        &signature_sz, digest, sizeof(digest), 0), 0);
    ck_assert_int_eq(sign_digest(CMD.secondary_sign, CMD.hash_algo, signature2,
        &signature_sz2, digest, sizeof(digest), 1), 0);

    ck_assert_int_eq(verify_raw_ecc(ECC_SECP521R1, 66, pubkey, signature,
        digest, sizeof(digest)), 1);
    ck_assert_int_eq(verify_raw_ecc(ECC_SECP256R1, 32, pubkey2, signature2,
        digest, sizeof(digest)), 1);

    unlink(primary_path);
    unlink(secondary_path);
    rmdir(tempdir);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("sign-hybrid-keyload");
    TCase *tcase = tcase_create("load-key");

    tcase_add_test(tcase, test_load_key_clears_pubkey_when_file_missing);
    tcase_add_test(tcase, test_load_key_clears_pubkey_when_decode_fails);
    tcase_add_test(tcase, test_hybrid_secondary_key_does_not_clobber_primary);
    tcase_add_exit_test(tcase, test_sign_main_fails_when_secondary_key_missing,
        1);
    suite_add_tcase(s, tcase);

    return s;
}

int main(void)
{
    int failed;
    Suite *s = wolfboot_suite();
    SRunner *runner = srunner_create(s);

    srunner_run_all(runner, CK_NORMAL);
    failed = srunner_ntests_failed(runner);
    srunner_free(runner);

    return failed == 0 ? 0 : 1;
}
