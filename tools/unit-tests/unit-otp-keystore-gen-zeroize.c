/* unit-otp-keystore-gen-zeroize.c
 *
 * Regression test for the OTP keystore generator (host tool) leaving the
 * device root UDS behind in a freed heap chunk and in the `uds` stack
 * buffer instead of wiping both before the buffer is released / the
 * process exits.
 *
 * Interposes read() (to learn where the `uds` stack array lives) and
 * free() (called synchronously from inside the still-live gen_main()
 * frame, right before the OTP heap buffer is released) so the snapshot
 * of both buffers happens while they are still valid memory -- no
 * use-after-free/return required.
 */

#define _GNU_SOURCE
#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>

#define main gen_main
#include "../keytools/otp/otp-keystore-gen.c"
#undef main

static uint8_t *captured_uds_ptr;
static size_t captured_uds_len;
static void *captured_otp_buf_ptr;
static int capture_next_malloc;

static uint8_t otp_buf_snapshot[OTP_SIZE];
static uint8_t uds_snapshot[OTP_UDS_LEN];
static int free_call_count;

ssize_t read(int fd, void *buf, size_t count)
{
    static ssize_t (*real_read)(int, void *, size_t) = NULL;
    if (real_read == NULL)
        real_read = dlsym(RTLD_NEXT, "read");

    if (count == OTP_UDS_LEN && captured_uds_ptr == NULL) {
        captured_uds_ptr = (uint8_t *)buf;
        captured_uds_len = count;
    }
    return real_read(fd, buf, count);
}

void *malloc(size_t size)
{
    static void *(*real_malloc)(size_t) = NULL;
    void *ptr;
    if (real_malloc == NULL)
        real_malloc = dlsym(RTLD_NEXT, "malloc");

    ptr = real_malloc(size);
    /* gen_main() calls malloc() exactly once, for the OTP_SIZE image
     * buffer; capture that single allocation by call order, not by
     * size, so it cannot collide with an unrelated same-size alloc. */
    if (capture_next_malloc && captured_otp_buf_ptr == NULL) {
        captured_otp_buf_ptr = ptr;
        capture_next_malloc = 0;
    }
    return ptr;
}

void free(void *ptr)
{
    static void (*real_free)(void *) = NULL;
    if (real_free == NULL)
        real_free = dlsym(RTLD_NEXT, "free");

    /* Snapshot the OTP heap buffer and the (still live, since gen_main()
     * hasn't returned yet) `uds` stack array before the real free() runs. */
    if (ptr != NULL && ptr == captured_otp_buf_ptr) {
        free_call_count++;
        memcpy(otp_buf_snapshot, ptr, OTP_SIZE);
        if (captured_uds_ptr != NULL && captured_uds_len == OTP_UDS_LEN)
            memcpy(uds_snapshot, captured_uds_ptr, OTP_UDS_LEN);
    }
    real_free(ptr);
}

static void reset_capture(void)
{
    captured_uds_ptr = NULL;
    captured_uds_len = 0;
    captured_otp_buf_ptr = NULL;
    capture_next_malloc = 1;
    free_call_count = 0;
    memset(otp_buf_snapshot, 0, sizeof(otp_buf_snapshot));
    memset(uds_snapshot, 0, sizeof(uds_snapshot));
}

START_TEST(test_success_path_zeroizes_uds_before_free)
{
    int r;
    int i;
    int all_zero;
    char tempdir[] = "/tmp/wolfboot-otp-gen-XXXXXX";
    char *dir = mkdtemp(tempdir);
    char cwd[4096];

    ck_assert_ptr_nonnull(dir);
    ck_assert_ptr_nonnull(getcwd(cwd, sizeof(cwd)));
    ck_assert_int_eq(chdir(dir), 0);

    reset_capture();
    r = gen_main();
    ck_assert_int_eq(r, 0);
    ck_assert_int_eq(free_call_count, 1);
    ck_assert_ptr_nonnull(captured_uds_ptr);

    /* Root UDS must not remain in the freed heap chunk at the OTP UDS
     * offset. */
    all_zero = 1;
    for (i = 0; i < OTP_UDS_LEN; i++) {
        if (otp_buf_snapshot[OTP_UDS_OFFSET + i] != 0)
            all_zero = 0;
    }
    ck_assert_msg(all_zero,
        "UDS bytes still present in freed otp_buf heap chunk at free() time");

    /* Root UDS must not remain in the `uds` stack buffer either. */
    all_zero = 1;
    for (i = 0; i < OTP_UDS_LEN; i++) {
        if (uds_snapshot[i] != 0)
            all_zero = 0;
    }
    ck_assert_msg(all_zero,
        "UDS bytes still present on the stack (`uds`) at free() time");

    ck_assert_int_eq(chdir(cwd), 0);
    unlink("otp.bin");
    rmdir(dir);
}
END_TEST

static Suite *otp_keystore_gen_zeroize_suite(void)
{
    Suite *s = suite_create("otp_keystore_gen_zeroize");
    TCase *tc = tcase_create("uds_zeroize");
    tcase_add_test(tc, test_success_path_zeroizes_uds_before_free);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = otp_keystore_gen_zeroize_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
