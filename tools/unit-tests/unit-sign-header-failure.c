/* unit-sign-header-failure.c
 *
 * Unit test for sign tool exit status when the manifest cannot be created.
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

static const char missing_image[] = "/nonexistent/wolfboot-image.bin";

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

/* The sign tool must report a failure to the caller (make, CI) when
 * make_header() could not produce the output image. */
START_TEST(test_sign_main_fails_when_image_missing)
{
    char tempdir[] = "/tmp/wolfboot-sign-XXXXXX";
    char key_path[PATH_MAX];
    uint8_t raw_pubkey[64]; /* ECC256 raw Qx + Qy */
    char *argv[6];

    ck_assert_ptr_nonnull(mkdtemp(tempdir));
    snprintf(key_path, sizeof(key_path), "%s/ecc256.raw", tempdir);

    memset(raw_pubkey, 0x11, sizeof(raw_pubkey));
    ck_assert_int_eq(write_file(key_path, raw_pubkey, sizeof(raw_pubkey)), 0);

    argv[0] = "sign";
    argv[1] = "--sha-only";
    argv[2] = "--ecc256";
    argv[3] = (char *)missing_image;
    argv[4] = key_path;
    argv[5] = "1";

    ck_assert_int_ne(wolfboot_sign_main(6, argv), 0);

    unlink(key_path);
    rmdir(tempdir);
}
END_TEST

/* Same contract for the hybrid path, which uses make_hybrid_header(). */
START_TEST(test_sign_main_fails_when_image_missing_hybrid)
{
    char tempdir[] = "/tmp/wolfboot-sign-XXXXXX";
    char key_path[PATH_MAX];
    char key2_path[PATH_MAX];
    uint8_t raw_pubkey[64];  /* ECC256 raw Qx + Qy */
    uint8_t raw_pubkey2[32]; /* ED25519 raw public key */
    char *argv[8];

    ck_assert_ptr_nonnull(mkdtemp(tempdir));
    snprintf(key_path, sizeof(key_path), "%s/ecc256.raw", tempdir);
    snprintf(key2_path, sizeof(key2_path), "%s/ed25519.raw", tempdir);

    memset(raw_pubkey, 0x11, sizeof(raw_pubkey));
    memset(raw_pubkey2, 0x22, sizeof(raw_pubkey2));
    ck_assert_int_eq(write_file(key_path, raw_pubkey, sizeof(raw_pubkey)), 0);
    ck_assert_int_eq(write_file(key2_path, raw_pubkey2, sizeof(raw_pubkey2)),
        0);

    argv[0] = "sign";
    argv[1] = "--sha-only";
    argv[2] = "--ecc256";
    argv[3] = "--ed25519";
    argv[4] = (char *)missing_image;
    argv[5] = key_path;
    argv[6] = key2_path;
    argv[7] = "1";

    ck_assert_int_ne(wolfboot_sign_main(8, argv), 0);

    unlink(key_path);
    unlink(key2_path);
    rmdir(tempdir);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("sign-header-failure");
    TCase *tcase = tcase_create("make-header");

    tcase_add_test(tcase, test_sign_main_fails_when_image_missing);
    tcase_add_test(tcase, test_sign_main_fails_when_image_missing_hybrid);
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
