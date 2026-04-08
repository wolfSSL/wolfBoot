/* unit-sign-encrypted-output.c
 *
 * Unit test for sign tool encrypted output error handling.
 */

#include <check.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *mock_fail_open_path;
static int mock_fail_open_on_call;
static int mock_open_call_count;
static int mock_null_fwrite_calls;
static int mock_null_fread_calls;
static int mock_null_fclose_calls;
static char mock_last_error[512];

static FILE *mock_fopen(const char *path, const char *mode);
static size_t mock_fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
static size_t mock_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
static int mock_fclose(FILE *stream);
static int mock_fprintf(FILE *stream, const char *format, ...);

#define main wolfboot_sign_main
#define fopen mock_fopen
#define fread mock_fread
#define fwrite mock_fwrite
#define fclose mock_fclose
#define fprintf mock_fprintf
#include "../keytools/sign.c"
#undef fprintf
#undef fclose
#undef fwrite
#undef fread
#undef fopen
#undef main

static void reset_mocks(const char *fail_open_path, int fail_open_on_call)
{
    mock_fail_open_path = fail_open_path;
    mock_fail_open_on_call = fail_open_on_call;
    mock_open_call_count = 0;
    mock_null_fwrite_calls = 0;
    mock_null_fread_calls = 0;
    mock_null_fclose_calls = 0;
    mock_last_error[0] = '\0';
}

static FILE *mock_fopen(const char *path, const char *mode)
{
    if (mock_fail_open_path != NULL && strcmp(path, mock_fail_open_path) == 0) {
        mock_open_call_count++;
    }

    if (mock_fail_open_path != NULL && strcmp(path, mock_fail_open_path) == 0 &&
        mock_open_call_count == mock_fail_open_on_call) {
        errno = EACCES;
        return NULL;
    }

    return fopen(path, mode);
}

static size_t mock_fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (stream == NULL) {
        mock_null_fread_calls++;
        return 0;
    }

    return fread(ptr, size, nmemb, stream);
}

static size_t mock_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (stream == NULL) {
        mock_null_fwrite_calls++;
        return 0;
    }

    return fwrite(ptr, size, nmemb, stream);
}

static int mock_fclose(FILE *stream)
{
    if (stream == NULL) {
        mock_null_fclose_calls++;
        return EOF;
    }

    return fclose(stream);
}

static int mock_fprintf(FILE *stream, const char *format, ...)
{
    int ret;
    va_list args;

    va_start(args, format);
    ret = vsnprintf(mock_last_error, sizeof(mock_last_error), format, args);
    va_end(args);

    va_start(args, format);
    ret = vfprintf(stream, format, args);
    va_end(args);

    return ret;
}

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

START_TEST(test_make_header_ex_fails_when_encrypted_output_open_fails)
{
    char tempdir[] = "/tmp/wolfboot-sign-XXXXXX";
    char image_path[PATH_MAX];
    char key_path[PATH_MAX];
    char output_path[PATH_MAX];
    char encrypted_output_path[PATH_MAX];
    uint8_t image_buf[] = { 0x01, 0x02, 0x03, 0x04 };
    uint8_t key_buf[ENCRYPT_KEY_SIZE_AES128 + ENCRYPT_NONCE_SIZE_AES];
    uint8_t pubkey[] = { 0xA5 };
    int ret;

    ck_assert_ptr_nonnull(mkdtemp(tempdir));

    snprintf(image_path, sizeof(image_path), "%s/image.bin", tempdir);
    snprintf(key_path, sizeof(key_path), "%s/encrypt.key", tempdir);
    snprintf(output_path, sizeof(output_path), "%s/output.bin", tempdir);
    snprintf(encrypted_output_path, sizeof(encrypted_output_path),
        "%s/encrypted-output.bin", tempdir);

    memset(key_buf, 0x5A, sizeof(key_buf));
    ck_assert_int_eq(write_file(image_path, image_buf, sizeof(image_buf)), 0);
    ck_assert_int_eq(write_file(key_path, key_buf, sizeof(key_buf)), 0);

    memset(&CMD, 0, sizeof(CMD));
    CMD.sign = NO_SIGN;
    CMD.encrypt = ENC_AES128;
    CMD.hash_algo = HASH_SHA256;
    CMD.partition_id = HDR_IMG_TYPE_APP;
    CMD.header_sz = 256;
    CMD.fw_version = "7";
    CMD.no_ts = 1;
    CMD.encrypt_key_file = key_path;
    snprintf(CMD.output_encrypted_image_file,
        sizeof(CMD.output_encrypted_image_file), "%s", encrypted_output_path);

    reset_mocks(encrypted_output_path, 1);
    ret = make_header_ex(0, pubkey, sizeof(pubkey), image_path, output_path,
        0, 0, 0, 0, NULL, 0, NULL, 0);

    ck_assert_int_ne(ret, 0);
    ck_assert_int_eq(mock_null_fwrite_calls, 0);
    ck_assert_int_eq(mock_null_fread_calls, 0);
    ck_assert_int_eq(mock_null_fclose_calls, 0);
    ck_assert_ptr_nonnull(strstr(mock_last_error, encrypted_output_path));
    ck_assert_ptr_null(strstr(mock_last_error, key_path));

    unlink(output_path);
    unlink(key_path);
    unlink(image_path);
    rmdir(tempdir);
}
END_TEST

START_TEST(test_make_header_ex_fails_when_image_reopen_fails)
{
    char tempdir[] = "/tmp/wolfboot-sign-XXXXXX";
    char image_path[PATH_MAX];
    char output_path[PATH_MAX];
    uint8_t image_buf[] = { 0x01, 0x02, 0x03, 0x04 };
    uint8_t pubkey[] = { 0xA5 };
    int ret;

    ck_assert_ptr_nonnull(mkdtemp(tempdir));

    snprintf(image_path, sizeof(image_path), "%s/image.bin", tempdir);
    snprintf(output_path, sizeof(output_path), "%s/output.bin", tempdir);

    ck_assert_int_eq(write_file(image_path, image_buf, sizeof(image_buf)), 0);

    memset(&CMD, 0, sizeof(CMD));
    CMD.sign = NO_SIGN;
    CMD.hash_algo = HASH_SHA256;
    CMD.partition_id = HDR_IMG_TYPE_APP;
    CMD.header_sz = 256;
    CMD.fw_version = "7";
    CMD.no_ts = 1;

    reset_mocks(image_path, 2);
    ret = make_header_ex(0, pubkey, sizeof(pubkey), image_path, output_path,
        0, 0, 0, 0, NULL, 0, NULL, 0);

    ck_assert_int_ne(ret, 0);
    ck_assert_int_eq(mock_null_fwrite_calls, 0);
    ck_assert_int_eq(mock_null_fread_calls, 0);
    ck_assert_int_eq(mock_null_fclose_calls, 0);

    unlink(output_path);
    unlink(image_path);
    rmdir(tempdir);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("sign-encrypted-output");
    TCase *tcase = tcase_create("make-header-ex");

    tcase_add_test(tcase, test_make_header_ex_fails_when_encrypted_output_open_fails);
    tcase_add_test(tcase, test_make_header_ex_fails_when_image_reopen_fails);
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
