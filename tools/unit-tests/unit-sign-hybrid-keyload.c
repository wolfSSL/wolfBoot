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

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("sign-hybrid-keyload");
    TCase *tcase = tcase_create("load-key");

    tcase_add_test(tcase, test_load_key_clears_pubkey_when_file_missing);
    tcase_add_test(tcase, test_load_key_clears_pubkey_when_decode_fails);
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
