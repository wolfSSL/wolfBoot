#include <check.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const char *mock_xmss_param;
static int mock_exit_code;
static jmp_buf mock_exit_env;

static void mock_exit(int code);

#define main wolfboot_keygen_main
#define exit mock_exit
#define wc_XmssKey_Init mock_wc_XmssKey_Init
#define wc_XmssKey_SetParamStr mock_wc_XmssKey_SetParamStr
#define wc_XmssKey_SetWriteCb mock_wc_XmssKey_SetWriteCb
#define wc_XmssKey_SetReadCb mock_wc_XmssKey_SetReadCb
#define wc_XmssKey_SetContext mock_wc_XmssKey_SetContext
#define wc_XmssKey_MakeKey mock_wc_XmssKey_MakeKey
#define wc_XmssKey_GetPrivLen mock_wc_XmssKey_GetPrivLen
#define wc_XmssKey_ExportPubRaw mock_wc_XmssKey_ExportPubRaw
#define wc_XmssKey_Free mock_wc_XmssKey_Free
#define wc_ForceZero mock_wc_ForceZero
#include "../keytools/keygen.c"
#undef wc_ForceZero
#undef wc_XmssKey_Free
#undef wc_XmssKey_ExportPubRaw
#undef wc_XmssKey_GetPrivLen
#undef wc_XmssKey_MakeKey
#undef wc_XmssKey_SetContext
#undef wc_XmssKey_SetReadCb
#undef wc_XmssKey_SetWriteCb
#undef wc_XmssKey_SetParamStr
#undef wc_XmssKey_Init
#undef exit
#undef main

static void mock_exit(int code)
{
    mock_exit_code = code;
    longjmp(mock_exit_env, 1);
}

int mock_wc_XmssKey_Init(XmssKey *key, void *heap, int devId)
{
    (void)key;
    (void)heap;
    (void)devId;

    return 0;
}

int mock_wc_XmssKey_SetParamStr(XmssKey *key, const char *str)
{
    (void)key;
    mock_xmss_param = str;
    return 0;
}

int mock_wc_XmssKey_SetWriteCb(XmssKey *key, wc_xmss_write_private_key_cb write_cb)
{
    (void)key;
    (void)write_cb;

    return -1;
}

int mock_wc_XmssKey_SetReadCb(XmssKey *key, wc_xmss_read_private_key_cb read_cb)
{
    (void)key;
    (void)read_cb;

    return 0;
}

int mock_wc_XmssKey_SetContext(XmssKey *key, void *context)
{
    (void)key;
    (void)context;

    return 0;
}

int mock_wc_XmssKey_MakeKey(XmssKey *key, WC_RNG *rng)
{
    (void)key;
    (void)rng;

    return 0;
}

int mock_wc_XmssKey_GetPrivLen(const XmssKey *key, word32 *len)
{
    (void)key;
    *len = 0;
    return 0;
}

int mock_wc_XmssKey_ExportPubRaw(const XmssKey *key, byte *out, word32 *outLen)
{
    (void)key;
    (void)out;
    (void)outLen;

    return 0;
}

void mock_wc_XmssKey_Free(XmssKey *key)
{
    (void)key;
}

void mock_wc_ForceZero(void *mem, size_t len)
{
    (void)mem;
    (void)len;
}

static void setup(void)
{
    mock_xmss_param = NULL;
    mock_exit_code = 0;
    unsetenv("XMSS_PARAMS");
}

static void teardown(void)
{
    unsetenv("XMSS_PARAMS");
}

static void run_keygen_xmss(void)
{
    int jumped;

    jumped = setjmp(mock_exit_env);
    if (jumped == 0) {
        keygen_xmss("ignored.xmss", 0);
    }

    ck_assert_int_eq(jumped, 1);
    ck_assert_int_eq(mock_exit_code, 1);
}

START_TEST(test_keygen_xmss_uses_env_param_when_set)
{
    const char *expected = "XMSSMT-SHA2_20/2_256";

    ck_assert_int_eq(setenv("XMSS_PARAMS", expected, 1), 0);

    run_keygen_xmss();

    ck_assert_ptr_nonnull(mock_xmss_param);
    ck_assert_str_eq(mock_xmss_param, expected);
}
END_TEST

START_TEST(test_keygen_xmss_uses_default_param_when_env_unset)
{
    run_keygen_xmss();

    ck_assert_ptr_nonnull(mock_xmss_param);
    ck_assert_str_eq(mock_xmss_param, WOLFBOOT_XMSS_PARAMS);
}
END_TEST

static Suite *keygen_xmss_suite(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("keygen_xmss");
    tc = tcase_create("xmss_params");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_keygen_xmss_uses_env_param_when_set);
    tcase_add_test(tc, test_keygen_xmss_uses_default_param_when_env_unset);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    Suite *s;
    SRunner *sr;
    int failed;

    s = keygen_xmss_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return failed == 0 ? 0 : 1;
}
