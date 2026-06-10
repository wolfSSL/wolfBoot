/* unit-fwtpm-nv-oob.c
 *
 * Regression test: fwtpm_nv_read/write/erase use > instead of >= in the
 * offset guard, so offset == WCS_FWTPM_NV_SIZE with size == 0 passes the
 * guard and returns TPM_RC_SUCCESS instead of BAD_FUNC_ARG.
 */

#include <check.h>
#include <stdint.h>
#include <string.h>

#define WOLFBOOT_TZ_FWTPM

/* Minimal types/macros that fwtpm_callable.c uses from wolftpm headers.
 * Poison the include guards of the heavy fwtpm headers so they become no-ops;
 * we define the handful of types/symbols we actually need ourselves. */
#define _FWTPM_H_
#define _FWTPM_COMMAND_H_
#define _FWTPM_NV_H_

/* tpm2_types.h only needs these guards poisoned to avoid the full wolfcrypt
 * pull-in; the macros it actually provides (XMEMCPY/XMEMSET etc.) come from
 * the guards below. */
#define WOLFTPM2_NO_WOLFCRYPT

typedef uint8_t  byte;
typedef uint32_t word32;

#define BAD_FUNC_ARG    (-173)
#define TPM_RC_SUCCESS   0x000
#define TPM_RC_FAILURE   0x101
#define TPM_RC_INITIALIZE 0x100

#define XMEMCPY(d,s,l)  memcpy((d),(s),(l))
#define XMEMSET(b,c,l)  memset((b),(c),(l))

#define TPM2_HEADER_SIZE 10

/* Minimal FWTPM_NV_HAL type matching the nested struct in the real FWTPM_CTX.
 * Field layout must match so the static initialiser in fwtpm_callable.c works. */
typedef struct FWTPM_NV_HAL_S {
    int (*read)(void *ctx, word32 offset, byte *buf, word32 size);
    int (*write)(void *ctx, word32 offset, const byte *buf, word32 size);
    int (*erase)(void *ctx, word32 offset, word32 size);
    void *ctx;
    word32 maxSize;
} FWTPM_NV_HAL;

/* Minimal FWTPM_CTX — only the field wcs_fwtpm_init/transmit actually touch */
typedef struct {
    int wasStarted;
} FWTPM_CTX;

/* Stub symbols referenced by fwtpm_callable.c */
unsigned int _start_heap;
unsigned int _heap_size;

void *wolfboot_store_sbrk(unsigned int incr, uint8_t **heap,
    uint8_t *start, uint32_t size)
{
    (void)incr; (void)heap; (void)start; (void)size;
    return NULL;
}

int FWTPM_Init(FWTPM_CTX *ctx) { (void)ctx; return 0; }
int FWTPM_NV_SetHAL(FWTPM_CTX *ctx, FWTPM_NV_HAL *hal)
    { (void)ctx; (void)hal; return 0; }
int FWTPM_ProcessCommand(FWTPM_CTX *ctx, const byte *cmdBuf, int cmdSize,
    byte *rspBuf, int *rspSize, int locality)
{
    (void)ctx; (void)cmdBuf; (void)cmdSize;
    (void)rspBuf; (void)rspSize; (void)locality;
    return TPM_RC_SUCCESS;
}

/* Bring in the code under test as part of this translation unit so we can
 * reach the static fwtpm_nv_hal struct and the static NV callbacks. */
#include "../../src/fwtpm_callable.c"

/* ── tests ──────────────────────────────────────────────────────────────── */

START_TEST(nv_read_rejects_offset_at_boundary)
{
    byte buf[8] = {0};
    ck_assert_int_eq(
        fwtpm_nv_hal.read(fwtpm_nv_hal.ctx, WCS_FWTPM_NV_SIZE, buf, 0),
        BAD_FUNC_ARG);
}
END_TEST

START_TEST(nv_write_rejects_offset_at_boundary)
{
    byte buf[8] = {0};
    ck_assert_int_eq(
        fwtpm_nv_hal.write(fwtpm_nv_hal.ctx, WCS_FWTPM_NV_SIZE, buf, 0),
        BAD_FUNC_ARG);
}
END_TEST

START_TEST(nv_erase_rejects_offset_at_boundary)
{
    ck_assert_int_eq(
        fwtpm_nv_hal.erase(fwtpm_nv_hal.ctx, WCS_FWTPM_NV_SIZE, 0),
        BAD_FUNC_ARG);
}
END_TEST

/* Sanity: valid access at last byte must succeed */
START_TEST(nv_read_accepts_last_valid_offset)
{
    byte buf[1] = {0};
    ck_assert_int_eq(
        fwtpm_nv_hal.read(fwtpm_nv_hal.ctx, WCS_FWTPM_NV_SIZE - 1, buf, 1),
        TPM_RC_SUCCESS);
}
END_TEST

static Suite *fwtpm_nv_oob_suite(void)
{
    Suite *s = suite_create("fwtpm_nv_oob");
    TCase *tc = tcase_create("boundary_guard");
    tcase_add_test(tc, nv_read_rejects_offset_at_boundary);
    tcase_add_test(tc, nv_write_rejects_offset_at_boundary);
    tcase_add_test(tc, nv_erase_rejects_offset_at_boundary);
    tcase_add_test(tc, nv_read_accepts_last_valid_offset);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = fwtpm_nv_oob_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails == 0 ? 0 : 1;
}
