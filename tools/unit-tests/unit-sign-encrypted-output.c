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

#define WOLFBOOT_HASH_SHA256
#define IMAGE_HEADER_SIZE 512

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

#include "../../src/libwolfboot.c"

static int locked;

void hal_init(void)
{
}

int hal_flash_write(haladdr_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int hal_flash_erase(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

void hal_flash_unlock(void)
{
    ck_assert_msg(locked, "Double unlock detected\n");
    locked--;
}

void hal_flash_lock(void)
{
    ck_assert_msg(!locked, "Double lock detected\n");
    locked++;
}

void hal_prepare_boot(void)
{
}

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

static int read_file(const char *path, uint8_t **buf, size_t *len)
{
    FILE *f;
    struct stat st;
    size_t read_len;

    if (stat(path, &st) != 0) {
        return -1;
    }

    *buf = malloc((size_t)st.st_size);
    if (*buf == NULL) {
        return -1;
    }

    f = fopen(path, "rb");
    if (f == NULL) {
        free(*buf);
        *buf = NULL;
        return -1;
    }

    read_len = fread(*buf, 1, (size_t)st.st_size, f);
    fclose(f);
    if (read_len != (size_t)st.st_size) {
        free(*buf);
        *buf = NULL;
        return -1;
    }

    *len = read_len;
    return 0;
}

static void reset_cmd_defaults(void)
{
    memset(&CMD, 0, sizeof(CMD));
    CMD.sign = NO_SIGN;
    CMD.hash_algo = HASH_SHA256;
    CMD.partition_id = HDR_IMG_TYPE_APP;
    CMD.header_sz = IMAGE_HEADER_SIZE;
    CMD.fw_version = "7";
    CMD.no_ts = 1;
}

static void free_custom_tlv_buffers(void)
{
    uint32_t i;

    for (i = 0; i < MAX_CUSTOM_TLVS; i++) {
        free(CMD.custom_tlv[i].buffer);
        CMD.custom_tlv[i].buffer = NULL;
    }
}

static void assert_header_bytes(const uint8_t *image, uint16_t tag,
    const uint8_t *expected, uint16_t expected_len)
{
    uint8_t *value = NULL;
    uint16_t len = wolfBoot_find_header((uint8_t *)image + IMAGE_HEADER_OFFSET,
        tag, &value);

    ck_assert_uint_eq(len, expected_len);
    ck_assert_ptr_nonnull(value);
    ck_assert_msg(memcmp(value, expected, expected_len) == 0,
        "Tag 0x%04x mismatch", tag);
}

static uint16_t find_exact_fill_custom_len(void)
{
    uint16_t len;

    for (len = 1; len < IMAGE_HEADER_SIZE; len++) {
        CMD.custom_tlvs = 1;
        CMD.custom_tlv[0].len = len;
        if (header_required_size(0, 0, 0) == CMD.header_sz) {
            return len;
        }
    }

    return 0;
}

static uint32_t find_cert_chain_len_for_required_size(int hash_algo,
    uint32_t required_size, uint32_t secondary_key_sz)
{
    uint32_t len;

    reset_cmd_defaults();
    CMD.hash_algo = hash_algo;
    CMD.hybrid = 1;
    CMD.secondary_sign = SIGN_ED25519;

    for (len = 1; len < IMAGE_HEADER_SIZE; len++) {
        if (header_required_size(0, len, secondary_key_sz) == required_size) {
            return len;
        }
    }

    return 0;
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

    reset_cmd_defaults();
    CMD.encrypt = ENC_AES128;
    CMD.header_sz = 256;
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

    reset_cmd_defaults();
    CMD.header_sz = 256;

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

START_TEST(test_make_header_ex_grows_header_for_cert_chain_and_digest_tlvs)
{
    char tempdir[] = "/tmp/wolfboot-sign-XXXXXX";
    char image_path[PATH_MAX];
    char output_path[PATH_MAX];
    char cert_chain_path[PATH_MAX];
    uint8_t *output_buf = NULL;
    uint8_t *value = NULL;
    uint8_t image_buf[] = { 0x01, 0x02, 0x03, 0x04 };
    uint8_t cert_chain_buf[200];
    uint8_t pubkey[] = { 0xA5 };
    struct stat st;
    size_t output_len;
    int ret;

    ck_assert_ptr_nonnull(mkdtemp(tempdir));

    snprintf(image_path, sizeof(image_path), "%s/image.bin", tempdir);
    snprintf(output_path, sizeof(output_path), "%s/output.bin", tempdir);
    snprintf(cert_chain_path, sizeof(cert_chain_path), "%s/cert-chain.bin",
        tempdir);

    memset(cert_chain_buf, 0xC3, sizeof(cert_chain_buf));
    ck_assert_int_eq(write_file(image_path, image_buf, sizeof(image_buf)), 0);
    ck_assert_int_eq(write_file(cert_chain_path, cert_chain_buf,
        sizeof(cert_chain_buf)), 0);

    reset_cmd_defaults();
    CMD.header_sz = 256;
    CMD.cert_chain_file = cert_chain_path;

    reset_mocks(NULL, 0);
    ret = make_header_ex(0, pubkey, sizeof(pubkey), image_path, output_path,
        0, 0, 0, 0, NULL, 0, NULL, 0);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(CMD.header_sz, 512);
    ck_assert_int_eq(stat(output_path, &st), 0);
    ck_assert_uint_eq((uint32_t)st.st_size, CMD.header_sz + sizeof(image_buf));
    ck_assert_int_eq(mock_null_fwrite_calls, 0);
    ck_assert_int_eq(mock_null_fread_calls, 0);
    ck_assert_int_eq(mock_null_fclose_calls, 0);
    ck_assert_int_eq(read_file(output_path, &output_buf, &output_len), 0);
    ck_assert_uint_eq(output_len, CMD.header_sz + sizeof(image_buf));
    assert_header_bytes(output_buf, HDR_CERT_CHAIN, cert_chain_buf,
        sizeof(cert_chain_buf));
    ck_assert_uint_eq(wolfBoot_find_header(output_buf + IMAGE_HEADER_OFFSET,
        HDR_PUBKEY, &value), HDR_SHA256_LEN);
    ck_assert_uint_eq(wolfBoot_find_header(output_buf + IMAGE_HEADER_OFFSET,
        HDR_SHA256, &value), HDR_SHA256_LEN);

    free(output_buf);
    unlink(output_path);
    unlink(cert_chain_path);
    unlink(image_path);
    rmdir(tempdir);
}
END_TEST

START_TEST(test_make_header_ex_roundtrip_custom_tlvs_via_wolfboot_parser)
{
    char tempdir[] = "/tmp/wolfboot-sign-XXXXXX";
    char image_path[PATH_MAX];
    char output_path[PATH_MAX];
    uint8_t *output_buf = NULL;
    uint8_t *value = NULL;
    uint8_t image_buf[] = { 0x10, 0x20, 0x30, 0x40, 0x50 };
    uint8_t pubkey[] = { 0xA5, 0x5A, 0x33, 0xCC };
    uint8_t tlv_one[] = { 0xAB };
    uint8_t tlv_two[] = { 0x10, 0x11, 0x12, 0x13, 0x14 };
    uint8_t tlv_three[] = { 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28 };
    uint16_t image_type;
    uint32_t version = 7;
    size_t output_len;
    int ret;

    ck_assert_ptr_nonnull(mkdtemp(tempdir));

    snprintf(image_path, sizeof(image_path), "%s/image.bin", tempdir);
    snprintf(output_path, sizeof(output_path), "%s/output.bin", tempdir);
    ck_assert_int_eq(write_file(image_path, image_buf, sizeof(image_buf)), 0);

    reset_cmd_defaults();
    image_type = (uint16_t)((CMD.sign & HDR_IMG_TYPE_AUTH_MASK) |
        CMD.partition_id);
    CMD.custom_tlvs = 3;
    CMD.custom_tlv[0].tag = 0x30;
    CMD.custom_tlv[0].len = sizeof(tlv_one);
    CMD.custom_tlv[0].buffer = malloc(sizeof(tlv_one));
    memcpy(CMD.custom_tlv[0].buffer, tlv_one, sizeof(tlv_one));
    CMD.custom_tlv[1].tag = 0x31;
    CMD.custom_tlv[1].len = sizeof(tlv_two);
    CMD.custom_tlv[1].buffer = malloc(sizeof(tlv_two));
    memcpy(CMD.custom_tlv[1].buffer, tlv_two, sizeof(tlv_two));
    CMD.custom_tlv[2].tag = 0x32;
    CMD.custom_tlv[2].len = sizeof(tlv_three);
    CMD.custom_tlv[2].buffer = malloc(sizeof(tlv_three));
    memcpy(CMD.custom_tlv[2].buffer, tlv_three, sizeof(tlv_three));

    reset_mocks(NULL, 0);
    ret = make_header_ex(0, pubkey, sizeof(pubkey), image_path, output_path,
        0, 0, 0, 0, NULL, 0, NULL, 0);

    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(read_file(output_path, &output_buf, &output_len), 0);
    ck_assert_uint_eq(output_len, CMD.header_sz + sizeof(image_buf));
    assert_header_bytes(output_buf, HDR_VERSION, (uint8_t *)&version,
        sizeof(version));
    assert_header_bytes(output_buf, HDR_IMG_TYPE, (uint8_t *)&image_type,
        sizeof(image_type));
    assert_header_bytes(output_buf, 0x30, tlv_one, sizeof(tlv_one));
    assert_header_bytes(output_buf, 0x31, tlv_two, sizeof(tlv_two));
    assert_header_bytes(output_buf, 0x32, tlv_three, sizeof(tlv_three));
    ck_assert_uint_eq(wolfBoot_find_header(output_buf + IMAGE_HEADER_OFFSET,
        HDR_PUBKEY, &value), HDR_SHA256_LEN);
    ck_assert_ptr_nonnull(value);
    ck_assert_uint_eq((uintptr_t)value % 8U, 0);
    ck_assert_uint_eq(wolfBoot_find_header(output_buf + IMAGE_HEADER_OFFSET,
        0x30, &value), sizeof(tlv_one));
    ck_assert_uint_eq((uintptr_t)value % 8U, 0);
    ck_assert_uint_eq(wolfBoot_find_header(output_buf + IMAGE_HEADER_OFFSET,
        0x31, &value), sizeof(tlv_two));
    ck_assert_uint_eq((uintptr_t)value % 8U, 0);
    ck_assert_uint_eq(wolfBoot_find_header(output_buf + IMAGE_HEADER_OFFSET,
        0x32, &value), sizeof(tlv_three));
    ck_assert_uint_eq((uintptr_t)value % 8U, 0);
    ck_assert_uint_eq(wolfBoot_find_header(output_buf + IMAGE_HEADER_OFFSET,
        HDR_SHA256, &value), HDR_SHA256_LEN);

    free(output_buf);
    free_custom_tlv_buffers();
    unlink(output_path);
    unlink(image_path);
    rmdir(tempdir);
}
END_TEST

START_TEST(test_make_header_ex_roundtrip_finds_tlv_that_exactly_fills_header)
{
    char tempdir[] = "/tmp/wolfboot-sign-XXXXXX";
    char image_path[PATH_MAX];
    char output_path[PATH_MAX];
    uint8_t *output_buf = NULL;
    uint8_t image_buf[] = { 0x61, 0x62, 0x63, 0x64 };
    uint8_t pubkey[] = { 0x01, 0x02 };
    uint8_t *custom_buf = NULL;
    uint8_t *value = NULL;
    size_t output_len;
    uint16_t exact_len;
    int ret;

    ck_assert_ptr_nonnull(mkdtemp(tempdir));

    snprintf(image_path, sizeof(image_path), "%s/image.bin", tempdir);
    snprintf(output_path, sizeof(output_path), "%s/output.bin", tempdir);
    ck_assert_int_eq(write_file(image_path, image_buf, sizeof(image_buf)), 0);

    reset_cmd_defaults();
    exact_len = find_exact_fill_custom_len();
    ck_assert_uint_ne(exact_len, 0);
    custom_buf = malloc(exact_len);
    ck_assert_ptr_nonnull(custom_buf);
    memset(custom_buf, 0x6C, exact_len);

    CMD.custom_tlvs = 1;
    CMD.custom_tlv[0].tag = 0x40;
    CMD.custom_tlv[0].len = exact_len;
    CMD.custom_tlv[0].buffer = custom_buf;
    ck_assert_uint_eq(header_required_size(0, 0, 0), CMD.header_sz);

    reset_mocks(NULL, 0);
    ret = make_header_ex(0, pubkey, sizeof(pubkey), image_path, output_path,
        0, 0, 0, 0, NULL, 0, NULL, 0);

    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(read_file(output_path, &output_buf, &output_len), 0);
    ck_assert_uint_eq(output_len, CMD.header_sz + sizeof(image_buf));
    ck_assert_uint_eq(wolfBoot_find_header(output_buf + IMAGE_HEADER_OFFSET,
        0x40, &value), exact_len);
    ck_assert_ptr_nonnull(value);
    ck_assert_msg(memcmp(value, custom_buf, exact_len) == 0,
        "Exact-fit TLV did not roundtrip");

    free(output_buf);
    free_custom_tlv_buffers();
    unlink(output_path);
    unlink(image_path);
    rmdir(tempdir);
}
END_TEST

START_TEST(test_make_header_ex_keeps_boundary_header_for_sha384_sha3_hybrid_cert_chain)
{
    static const int hash_algos[] = { HASH_SHA384, HASH_SHA3 };
    char tempdir[] = "/tmp/wolfboot-sign-XXXXXX";
    char image_path[PATH_MAX];
    char output_path[PATH_MAX];
    char cert_chain_path[PATH_MAX];
    uint8_t image_buf[] = { 0x71, 0x72, 0x73, 0x74 };
    uint8_t pubkey[] = { 0xA5, 0x5A, 0x33, 0xCC };
    uint8_t secondary_key[] = { 0x11, 0x22, 0x33, 0x44 };
    uint8_t *cert_chain_buf = NULL;
    struct stat st;
    size_t i;
    int ret;

    ck_assert_ptr_nonnull(mkdtemp(tempdir));

    snprintf(image_path, sizeof(image_path), "%s/image.bin", tempdir);
    snprintf(output_path, sizeof(output_path), "%s/output.bin", tempdir);
    snprintf(cert_chain_path, sizeof(cert_chain_path), "%s/cert-chain.bin",
        tempdir);
    ck_assert_int_eq(write_file(image_path, image_buf, sizeof(image_buf)), 0);

    for (i = 0; i < sizeof(hash_algos) / sizeof(hash_algos[0]); i++) {
        uint32_t cert_chain_len = find_cert_chain_len_for_required_size(
            hash_algos[i], IMAGE_HEADER_SIZE, sizeof(secondary_key));

        ck_assert_uint_ne(cert_chain_len, 0);
        cert_chain_buf = realloc(cert_chain_buf, cert_chain_len);
        ck_assert_ptr_nonnull(cert_chain_buf);
        memset(cert_chain_buf, 0xC3 + (int)i, cert_chain_len);
        ck_assert_int_eq(write_file(cert_chain_path, cert_chain_buf,
            cert_chain_len), 0);

        reset_cmd_defaults();
        CMD.hash_algo = hash_algos[i];
        CMD.hybrid = 1;
        CMD.secondary_sign = SIGN_ED25519;
        CMD.header_sz = IMAGE_HEADER_SIZE;
        CMD.cert_chain_file = cert_chain_path;

        reset_mocks(NULL, 0);
        ret = make_header_ex(0, pubkey, sizeof(pubkey), image_path, output_path,
            0, 0, 0, 0, secondary_key, sizeof(secondary_key), NULL, 0);

        ck_assert_int_eq(ret, 0);
        ck_assert_uint_eq(CMD.header_sz, IMAGE_HEADER_SIZE);
        ck_assert_int_eq(stat(output_path, &st), 0);
        ck_assert_uint_eq((uint32_t)st.st_size,
            IMAGE_HEADER_SIZE + sizeof(image_buf));
        unlink(output_path);
        unlink(cert_chain_path);
    }

    free(cert_chain_buf);
    unlink(image_path);
    rmdir(tempdir);
}
END_TEST

START_TEST(test_make_header_ex_rejects_cert_chain_tlv_length_overflow)
{
    char tempdir[] = "/tmp/wolfboot-sign-XXXXXX";
    char image_path[PATH_MAX];
    char output_path[PATH_MAX];
    char cert_chain_path[PATH_MAX];
    uint8_t image_buf[] = { 0x41, 0x42, 0x43, 0x44 };
    uint8_t pubkey[] = { 0xA5 };
    uint8_t *cert_chain_buf = NULL;
    const uint32_t cert_chain_len = 65536U;
    int ret;

    ck_assert_ptr_nonnull(mkdtemp(tempdir));

    snprintf(image_path, sizeof(image_path), "%s/image.bin", tempdir);
    snprintf(output_path, sizeof(output_path), "%s/output.bin", tempdir);
    snprintf(cert_chain_path, sizeof(cert_chain_path), "%s/cert-chain.bin",
        tempdir);

    cert_chain_buf = malloc(cert_chain_len);
    ck_assert_ptr_nonnull(cert_chain_buf);
    memset(cert_chain_buf, 0xC7, cert_chain_len);

    ck_assert_int_eq(write_file(image_path, image_buf, sizeof(image_buf)), 0);
    ck_assert_int_eq(write_file(cert_chain_path, cert_chain_buf,
        cert_chain_len), 0);

    reset_cmd_defaults();
    CMD.cert_chain_file = cert_chain_path;

    reset_mocks(NULL, 0);
    ret = make_header_ex(0, pubkey, sizeof(pubkey), image_path, output_path,
        0, 0, 0, 0, NULL, 0, NULL, 0);

    ck_assert_int_ne(ret, 0);

    free(cert_chain_buf);
    unlink(output_path);
    unlink(cert_chain_path);
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
    tcase_add_test(tcase,
        test_make_header_ex_grows_header_for_cert_chain_and_digest_tlvs);
    tcase_add_test(tcase,
        test_make_header_ex_roundtrip_custom_tlvs_via_wolfboot_parser);
    tcase_add_test(tcase,
        test_make_header_ex_roundtrip_finds_tlv_that_exactly_fills_header);
    tcase_add_test(tcase,
        test_make_header_ex_keeps_boundary_header_for_sha384_sha3_hybrid_cert_chain);
    tcase_add_test(tcase,
        test_make_header_ex_rejects_cert_chain_tlv_length_overflow);
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
