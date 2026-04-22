/* unit-ftpm-stub.c
 *
 * Unit tests for the fTPM non-secure TIS callback shim.
 */

#include <check.h>
#include <stdint.h>

#define WOLFBOOT_TZ_FTPM

int wcs_ftpm_transmit(const uint8_t *cmd, uint32_t cmdSz, uint8_t *rsp,
        uint32_t *rspSz)
{
    (void)cmd;
    (void)cmdSz;
    (void)rsp;
    (void)rspSz;
    return -1;
}

#include "../../test-app/wcs/ftpm_stub.c"

START_TEST(ftpm_tis_rejects_address_below_window)
{
    BYTE buf[4] = {0};

    ck_assert_int_eq(TPM2_IoCb_FtpmNsc(NULL, 1, FTPM_TIS_BASE - 1U,
        buf, sizeof(buf), NULL), BAD_FUNC_ARG);
}
END_TEST

START_TEST(ftpm_tis_rejects_address_above_window)
{
    BYTE buf[4] = {0};

    ck_assert_int_eq(TPM2_IoCb_FtpmNsc(NULL, 1, FTPM_TIS_BASE + 0x1000U,
        buf, sizeof(buf), NULL), BAD_FUNC_ARG);
}
END_TEST

START_TEST(ftpm_tis_accepts_valid_window_register)
{
    BYTE buf[4] = {0};

    ck_assert_int_eq(TPM2_IoCb_FtpmNsc(NULL, 1,
        FTPM_TIS_BASE + FTPM_TIS_DID_VID, buf, sizeof(buf), NULL),
        TPM_RC_SUCCESS);
    ck_assert_uint_eq(buf[0], 0x4EU);
    ck_assert_uint_eq(buf[1], 0x1BU);
    ck_assert_uint_eq(buf[2], 0x01U);
    ck_assert_uint_eq(buf[3], 0x00U);
}
END_TEST

static Suite *ftpm_stub_suite(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("ftpm_stub");
    tc = tcase_create("tis_window");
    tcase_add_test(tc, ftpm_tis_rejects_address_below_window);
    tcase_add_test(tc, ftpm_tis_rejects_address_above_window);
    tcase_add_test(tc, ftpm_tis_accepts_valid_window_register);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s;
    SRunner *sr;

    s = ftpm_stub_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails == 0 ? 0 : 1;
}
