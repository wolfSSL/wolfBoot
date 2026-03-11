/* unit-tpm-blob.c
 *
 * Unit tests for TPM blob NV reads.
 */

#include <check.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
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
#include "tpm.h"

enum mock_mode {
    MOCK_OVERSIZE_PUB,
    MOCK_OVERSIZE_PRIV
};

static enum mock_mode current_mode;
static int nvread_calls;
static int oversized_pub_read_attempted;
static int oversized_priv_read_attempted;

int wolfBoot_printf(const char* fmt, ...)
{
    (void)fmt;
    return 0;
}

int wolfTPM2_SetAuthHandle(WOLFTPM2_DEV* dev, int index,
    const WOLFTPM2_HANDLE* handle)
{
    (void)dev;
    (void)index;
    (void)handle;
    return 0;
}

const char* TPM2_GetRCString(int rc)
{
    (void)rc;
    return "mock";
}

int TPM2_ParsePublic(TPM2B_PUBLIC* pub, byte* buf, word32 size, int* sizeUsed)
{
    (void)buf;
    *sizeUsed = 0;
    pub->size = (UINT16)size;
    return 0;
}

int wolfTPM2_NVReadAuth(WOLFTPM2_DEV* dev, WOLFTPM2_NV* nv,
    word32 nvIndex, byte* dataBuf, word32* pDataSz, word32 offset)
{
    (void)dev;
    (void)nv;
    (void)nvIndex;
    (void)offset;

    nvread_calls++;

    switch (nvread_calls) {
        case 1:
            if (current_mode == MOCK_OVERSIZE_PUB) {
                *(uint16_t*)dataBuf = (uint16_t)(sizeof(TPM2B_PUBLIC) + 1);
            }
            else {
                *(uint16_t*)dataBuf = 4;
            }
            *pDataSz = sizeof(uint16_t);
            return 0;
        case 2:
            if (current_mode == MOCK_OVERSIZE_PUB) {
                oversized_pub_read_attempted = 1;
                return -100;
            }
            memset(dataBuf, 0, *pDataSz);
            return 0;
        case 3:
            *(uint16_t*)dataBuf = (uint16_t)(sizeof(((WOLFTPM2_KEYBLOB*)0)->priv.buffer) + 1);
            *pDataSz = sizeof(uint16_t);
            return 0;
        case 4:
            oversized_priv_read_attempted = 1;
            return -101;
        default:
            ck_abort_msg("Unexpected NV read call %d", nvread_calls);
            return -102;
    }
}

#include "../../src/tpm.c"

static void setup(void)
{
    current_mode = MOCK_OVERSIZE_PUB;
    nvread_calls = 0;
    oversized_pub_read_attempted = 0;
    oversized_priv_read_attempted = 0;
}

START_TEST(test_wolfBoot_read_blob_rejects_oversized_public_area)
{
    WOLFTPM2_KEYBLOB blob;
    int rc;

    memset(&blob, 0, sizeof(blob));
    current_mode = MOCK_OVERSIZE_PUB;

    rc = wolfBoot_read_blob(0x01400300, &blob, NULL, 0);

    ck_assert_int_eq(rc, BUFFER_E);
    ck_assert_int_eq(nvread_calls, 1);
    ck_assert_int_eq(oversized_pub_read_attempted, 0);
}
END_TEST

START_TEST(test_wolfBoot_read_blob_rejects_oversized_private_area)
{
    WOLFTPM2_KEYBLOB blob;
    int rc;

    memset(&blob, 0, sizeof(blob));
    current_mode = MOCK_OVERSIZE_PRIV;
    blob.pub.size = 4;

    rc = wolfBoot_read_blob(0x01400300, &blob, NULL, 0);

    ck_assert_int_eq(rc, BUFFER_E);
    ck_assert_int_eq(nvread_calls, 3);
    ck_assert_int_eq(oversized_priv_read_attempted, 0);
}
END_TEST

static Suite *tpm_blob_suite(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("TPM Blob");
    tc = tcase_create("wolfBoot_read_blob");
    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_wolfBoot_read_blob_rejects_oversized_public_area);
    tcase_add_test(tc, test_wolfBoot_read_blob_rejects_oversized_private_area);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s;
    SRunner *sr;
    int failed;

    s = tpm_blob_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
