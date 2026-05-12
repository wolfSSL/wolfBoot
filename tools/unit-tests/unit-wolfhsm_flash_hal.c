/* unit-wolfhsm_flash_hal.c
 *
 * Unit tests for the wolfHSM whFlashCb adapter (src/wolfhsm_flash_hal.c).
 */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "user_settings.h"
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/misc.h"

#include "hal.h"
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_flash.h"
#include "wolfboot/wolfhsm_flash_hal.h"

#define MOCK_FLASH_BASE     0xCF000000U
#define MOCK_FLASH_SECTOR   (8U * 1024U)
#define MOCK_FLASH_SECTORS  14U
#define MOCK_FLASH_SIZE     (MOCK_FLASH_SECTORS * MOCK_FLASH_SECTOR)

static int g_flash_locked     = 1;
static int g_flash_write_fail = 0;
static int g_flash_erase_fail = 0;

void hal_flash_unlock(void) { g_flash_locked = 0; }
void hal_flash_lock(void)   { g_flash_locked = 1; }

int hal_flash_erase(haladdr_t addr, int len)
{
    if (g_flash_locked || g_flash_erase_fail) {
        return -1;
    }
    if (addr < MOCK_FLASH_BASE ||
        addr + (uintptr_t)len > MOCK_FLASH_BASE + MOCK_FLASH_SIZE) {
        return -1;
    }
    memset((void *)addr, 0xFF, (size_t)len);
    return 0;
}

int hal_flash_write(haladdr_t addr, const uint8_t *data, int len)
{
    if (g_flash_locked || g_flash_write_fail) {
        return -1;
    }
    if (addr < MOCK_FLASH_BASE ||
        addr + (uintptr_t)len > MOCK_FLASH_BASE + MOCK_FLASH_SIZE) {
        return -1;
    }
    memcpy((void *)addr, data, (size_t)len);
    return 0;
}

#include "../../src/wolfhsm_flash_hal.c"

static void mock_flash_init(void)
{
    /* Prefer MAP_FIXED_NOREPLACE so the kernel refuses to clobber an
     * existing mapping at MOCK_FLASH_BASE (Linux >= 4.17). Older kernels
     * fall back to plain MAP_FIXED. */
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_FIXED_NOREPLACE
    flags |= MAP_FIXED_NOREPLACE;
#else
    flags |= MAP_FIXED;
#endif
    void *p = mmap((void *)(uintptr_t)MOCK_FLASH_BASE, MOCK_FLASH_SIZE,
                   PROT_READ | PROT_WRITE, flags, -1, 0);
    ck_assert_ptr_eq(p, (void *)(uintptr_t)MOCK_FLASH_BASE);
    memset((void *)(uintptr_t)MOCK_FLASH_BASE, 0xFF, MOCK_FLASH_SIZE);
    g_flash_locked     = 1;
    g_flash_write_fail = 0;
    g_flash_erase_fail = 0;
}

static void mock_flash_fini(void)
{
    munmap((void *)(uintptr_t)MOCK_FLASH_BASE, MOCK_FLASH_SIZE);
}

START_TEST(test_init_rejects_null)
{
    whFlashH5Ctx ctx;
    whFlashH5Ctx cfg;

    cfg.base           = MOCK_FLASH_BASE;
    cfg.size           = MOCK_FLASH_SIZE;
    cfg.partition_size = MOCK_FLASH_SIZE / 2U;

    ck_assert_int_eq(whFlashH5_Cb.Init(NULL, &cfg), WH_ERROR_BADARGS);
    ck_assert_int_eq(whFlashH5_Cb.Init(&ctx, NULL), WH_ERROR_BADARGS);
}
END_TEST

START_TEST(test_init_rejects_bad_config)
{
    whFlashH5Ctx ctx;
    whFlashH5Ctx cfg;

    cfg.base           = 0;
    cfg.size           = MOCK_FLASH_SIZE;
    cfg.partition_size = MOCK_FLASH_SIZE / 2U;
    ck_assert_int_eq(whFlashH5_Cb.Init(&ctx, &cfg), WH_ERROR_BADARGS);

    cfg.base = MOCK_FLASH_BASE;
    cfg.size = 0;
    ck_assert_int_eq(whFlashH5_Cb.Init(&ctx, &cfg), WH_ERROR_BADARGS);

    cfg.size           = MOCK_FLASH_SIZE;
    cfg.partition_size = 0;
    ck_assert_int_eq(whFlashH5_Cb.Init(&ctx, &cfg), WH_ERROR_BADARGS);

    cfg.base           = MOCK_FLASH_BASE + 4U;
    cfg.partition_size = MOCK_FLASH_SIZE / 2U;
    ck_assert_int_eq(whFlashH5_Cb.Init(&ctx, &cfg), WH_ERROR_BADARGS);

    cfg.base = MOCK_FLASH_BASE;
    cfg.size = MOCK_FLASH_SIZE + 4U;
    ck_assert_int_eq(whFlashH5_Cb.Init(&ctx, &cfg), WH_ERROR_BADARGS);

    cfg.size           = MOCK_FLASH_SIZE;
    cfg.partition_size = 100U;
    ck_assert_int_eq(whFlashH5_Cb.Init(&ctx, &cfg), WH_ERROR_BADARGS);

    cfg.partition_size = MOCK_FLASH_SIZE;
    ck_assert_int_eq(whFlashH5_Cb.Init(&ctx, &cfg), WH_ERROR_BADARGS);
}
END_TEST

START_TEST(test_init_accepts_valid)
{
    whFlashH5Ctx ctx;
    whFlashH5Ctx cfg;

    cfg.base           = MOCK_FLASH_BASE;
    cfg.size           = MOCK_FLASH_SIZE;
    cfg.partition_size = MOCK_FLASH_SIZE / 2U;
    ck_assert_int_eq(whFlashH5_Cb.Init(&ctx, &cfg), WH_ERROR_OK);
    ck_assert_uint_eq(whFlashH5_Cb.PartitionSize(&ctx), MOCK_FLASH_SIZE / 2U);
}
END_TEST

START_TEST(test_read_bounds)
{
    whFlashH5Ctx ctx;
    uint8_t      buf[16];

    ctx.base           = MOCK_FLASH_BASE;
    ctx.size           = MOCK_FLASH_SIZE;
    ctx.partition_size = MOCK_FLASH_SIZE / 2U;

    ck_assert_int_eq(whFlashH5_Cb.Read(&ctx, MOCK_FLASH_SIZE + 1U, 0U, buf),
                     WH_ERROR_BADARGS);
    ck_assert_int_eq(whFlashH5_Cb.Read(&ctx, 0U, MOCK_FLASH_SIZE + 1U, buf),
                     WH_ERROR_BADARGS);
    ck_assert_int_eq(whFlashH5_Cb.Read(&ctx, 0U, 0U, NULL), WH_ERROR_OK);
}
END_TEST

START_TEST(test_program_single_partial_preserves_neighbours)
{
    whFlashH5Ctx ctx;
    uint8_t      data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t      readback[8];

    mock_flash_init();
    ctx.base           = MOCK_FLASH_BASE;
    ctx.size           = MOCK_FLASH_SIZE;
    ctx.partition_size = MOCK_FLASH_SIZE / 2U;

    ck_assert_int_eq(whFlashH5_Cb.Program(&ctx, 0U, sizeof(data), data),
                     WH_ERROR_OK);
    memcpy(readback, (void *)(uintptr_t)MOCK_FLASH_BASE, sizeof(readback));
    ck_assert_mem_eq(readback, data, sizeof(data));
    ck_assert_uint_eq(((uint8_t *)(uintptr_t)MOCK_FLASH_BASE)[8], 0xFFU);
    ck_assert_uint_eq(((uint8_t *)(uintptr_t)MOCK_FLASH_BASE)[MOCK_FLASH_SECTOR - 1U],
                      0xFFU);
    mock_flash_fini();
}
END_TEST

START_TEST(test_program_crosses_sector_boundary)
{
    whFlashH5Ctx   ctx;
    const uint32_t off = MOCK_FLASH_SECTOR - 8U;
    uint8_t        data[16];
    uint8_t        readback[16];
    int            i;

    for (i = 0; i < (int)sizeof(data); i++) {
        data[i] = (uint8_t)(0x10 + i);
    }
    mock_flash_init();
    ctx.base           = MOCK_FLASH_BASE;
    ctx.size           = MOCK_FLASH_SIZE;
    ctx.partition_size = MOCK_FLASH_SIZE / 2U;

    ck_assert_int_eq(whFlashH5_Cb.Program(&ctx, off, sizeof(data), data),
                     WH_ERROR_OK);
    memcpy(readback, (void *)(uintptr_t)(MOCK_FLASH_BASE + off),
           sizeof(readback));
    ck_assert_mem_eq(readback, data, sizeof(data));
    ck_assert_uint_eq(((uint8_t *)(uintptr_t)MOCK_FLASH_BASE)[off - 1U], 0xFFU);
    ck_assert_uint_eq(((uint8_t *)(uintptr_t)MOCK_FLASH_BASE)[off + 16U], 0xFFU);
    mock_flash_fini();
}
END_TEST

START_TEST(test_program_propagates_write_failure)
{
    whFlashH5Ctx ctx;
    uint8_t      data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    mock_flash_init();
    ctx.base           = MOCK_FLASH_BASE;
    ctx.size           = MOCK_FLASH_SIZE;
    ctx.partition_size = MOCK_FLASH_SIZE / 2U;

    g_flash_write_fail = 1;
    ck_assert_int_eq(whFlashH5_Cb.Program(&ctx, 0U, sizeof(data), data),
                     WH_ERROR_ABORTED);
    g_flash_write_fail = 0;
    mock_flash_fini();
}
END_TEST

START_TEST(test_erase_alignment)
{
    whFlashH5Ctx ctx;

    mock_flash_init();
    ctx.base           = MOCK_FLASH_BASE;
    ctx.size           = MOCK_FLASH_SIZE;
    ctx.partition_size = MOCK_FLASH_SIZE / 2U;

    ck_assert_int_eq(whFlashH5_Cb.Erase(&ctx, 100U, MOCK_FLASH_SECTOR),
                     WH_ERROR_BADARGS);
    ck_assert_int_eq(whFlashH5_Cb.Erase(&ctx, 0U, 100U), WH_ERROR_BADARGS);
    ck_assert_int_eq(whFlashH5_Cb.Erase(&ctx, 0U, MOCK_FLASH_SECTOR),
                     WH_ERROR_OK);
    mock_flash_fini();
}
END_TEST

START_TEST(test_verify)
{
    whFlashH5Ctx ctx;
    uint8_t      data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t      bad[8]  = { 0 };

    mock_flash_init();
    ctx.base           = MOCK_FLASH_BASE;
    ctx.size           = MOCK_FLASH_SIZE;
    ctx.partition_size = MOCK_FLASH_SIZE / 2U;

    ck_assert_int_eq(whFlashH5_Cb.Program(&ctx, 0U, sizeof(data), data),
                     WH_ERROR_OK);
    ck_assert_int_eq(whFlashH5_Cb.Verify(&ctx, 0U, sizeof(data), data),
                     WH_ERROR_OK);
    ck_assert_int_eq(whFlashH5_Cb.Verify(&ctx, 0U, sizeof(bad), bad),
                     WH_ERROR_NOTVERIFIED);
    mock_flash_fini();
}
END_TEST

START_TEST(test_blank_check)
{
    whFlashH5Ctx ctx;
    uint8_t      data[8] = { 1, 0, 0, 0, 0, 0, 0, 0 };

    mock_flash_init();
    ctx.base           = MOCK_FLASH_BASE;
    ctx.size           = MOCK_FLASH_SIZE;
    ctx.partition_size = MOCK_FLASH_SIZE / 2U;

    ck_assert_int_eq(whFlashH5_Cb.BlankCheck(&ctx, 0U, MOCK_FLASH_SIZE),
                     WH_ERROR_OK);
    ck_assert_int_eq(whFlashH5_Cb.Program(&ctx, 0U, sizeof(data), data),
                     WH_ERROR_OK);
    ck_assert_int_eq(whFlashH5_Cb.BlankCheck(&ctx, 0U, sizeof(data)),
                     WH_ERROR_NOTBLANK);
    mock_flash_fini();
}
END_TEST

START_TEST(test_cleanup)
{
    whFlashH5Ctx ctx;

    ck_assert_int_eq(whFlashH5_Cb.Cleanup(NULL), WH_ERROR_BADARGS);
    ck_assert_int_eq(whFlashH5_Cb.Cleanup(&ctx), WH_ERROR_OK);
}
END_TEST

START_TEST(test_program_propagates_erase_failure)
{
    whFlashH5Ctx ctx;
    uint8_t      data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    mock_flash_init();
    ctx.base           = MOCK_FLASH_BASE;
    ctx.size           = MOCK_FLASH_SIZE;
    ctx.partition_size = MOCK_FLASH_SIZE / 2U;

    g_flash_erase_fail = 1;
    ck_assert_int_eq(whFlashH5_Cb.Program(&ctx, 0U, sizeof(data), data),
                     WH_ERROR_ABORTED);
    g_flash_erase_fail = 0;
    mock_flash_fini();
}
END_TEST

START_TEST(test_read_returns_flash_contents)
{
    whFlashH5Ctx ctx;
    uint8_t      data[16];
    uint8_t      buf[16];
    int          i;

    for (i = 0; i < (int)sizeof(data); i++) {
        data[i] = (uint8_t)(0xA0 + i);
    }
    mock_flash_init();
    ctx.base           = MOCK_FLASH_BASE;
    ctx.size           = MOCK_FLASH_SIZE;
    ctx.partition_size = MOCK_FLASH_SIZE / 2U;

    ck_assert_int_eq(whFlashH5_Cb.Program(&ctx, 0U, sizeof(data), data),
                     WH_ERROR_OK);
    memset(buf, 0, sizeof(buf));
    ck_assert_int_eq(whFlashH5_Cb.Read(&ctx, 0U, sizeof(buf), buf),
                     WH_ERROR_OK);
    ck_assert_mem_eq(buf, data, sizeof(data));
    mock_flash_fini();
}
END_TEST

START_TEST(test_callbacks_reject_null_context)
{
    uint8_t data[8] = { 0 };

    ck_assert_int_eq(whFlashH5_Cb.Read(NULL, 0U, sizeof(data), data),
                     WH_ERROR_BADARGS);
    ck_assert_int_eq(whFlashH5_Cb.Program(NULL, 0U, sizeof(data), data),
                     WH_ERROR_BADARGS);
    ck_assert_int_eq(whFlashH5_Cb.Erase(NULL, 0U, MOCK_FLASH_SECTOR),
                     WH_ERROR_BADARGS);
    ck_assert_int_eq(whFlashH5_Cb.Verify(NULL, 0U, sizeof(data), data),
                     WH_ERROR_BADARGS);
    ck_assert_int_eq(whFlashH5_Cb.BlankCheck(NULL, 0U, sizeof(data)),
                     WH_ERROR_BADARGS);
    ck_assert_uint_eq(whFlashH5_Cb.PartitionSize(NULL), 0U);
}
END_TEST

START_TEST(test_callbacks_reject_null_data)
{
    whFlashH5Ctx ctx;

    ctx.base           = MOCK_FLASH_BASE;
    ctx.size           = MOCK_FLASH_SIZE;
    ctx.partition_size = MOCK_FLASH_SIZE / 2U;

    ck_assert_int_eq(whFlashH5_Cb.Program(&ctx, 0U, 8U, NULL),
                     WH_ERROR_BADARGS);
    ck_assert_int_eq(whFlashH5_Cb.Verify(&ctx, 0U, 8U, NULL),
                     WH_ERROR_BADARGS);
}
END_TEST

START_TEST(test_program_spans_three_sectors)
{
    whFlashH5Ctx   ctx;
    const uint32_t off = MOCK_FLASH_SECTOR - 4U;
    const uint32_t sz  = (2U * MOCK_FLASH_SECTOR) + 8U;
    uint8_t        data[(2U * MOCK_FLASH_SECTOR) + 8U];
    uint8_t        readback[sizeof(data)];
    uint32_t       i;

    for (i = 0U; i < sizeof(data); i++) {
        data[i] = (uint8_t)(i & 0xFFU);
    }
    mock_flash_init();
    ctx.base           = MOCK_FLASH_BASE;
    ctx.size           = MOCK_FLASH_SIZE;
    ctx.partition_size = MOCK_FLASH_SIZE / 2U;

    ck_assert_int_eq(whFlashH5_Cb.Program(&ctx, off, sz, data), WH_ERROR_OK);
    memcpy(readback, (void *)(uintptr_t)(MOCK_FLASH_BASE + off), sz);
    ck_assert_mem_eq(readback, data, sz);
    ck_assert_uint_eq(((uint8_t *)(uintptr_t)MOCK_FLASH_BASE)[off - 1U], 0xFFU);
    ck_assert_uint_eq(((uint8_t *)(uintptr_t)MOCK_FLASH_BASE)[off + sz], 0xFFU);
    mock_flash_fini();
}
END_TEST

START_TEST(test_writelock_writeunlock_noop)
{
    whFlashH5Ctx ctx;

    ctx.base           = MOCK_FLASH_BASE;
    ctx.size           = MOCK_FLASH_SIZE;
    ctx.partition_size = MOCK_FLASH_SIZE / 2U;

    ck_assert_int_eq(whFlashH5_Cb.WriteLock(&ctx, 0U, MOCK_FLASH_SECTOR),
                     WH_ERROR_OK);
    ck_assert_int_eq(whFlashH5_Cb.WriteUnlock(&ctx, 0U, MOCK_FLASH_SECTOR),
                     WH_ERROR_OK);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s  = suite_create("wolfHSM-flash-hal");
    TCase *tc = tcase_create("flash-hal");

    tcase_add_test(tc, test_init_rejects_null);
    tcase_add_test(tc, test_init_rejects_bad_config);
    tcase_add_test(tc, test_init_accepts_valid);
    tcase_add_test(tc, test_read_bounds);
    tcase_add_test(tc, test_program_single_partial_preserves_neighbours);
    tcase_add_test(tc, test_program_crosses_sector_boundary);
    tcase_add_test(tc, test_program_propagates_write_failure);
    tcase_add_test(tc, test_erase_alignment);
    tcase_add_test(tc, test_verify);
    tcase_add_test(tc, test_blank_check);
    tcase_add_test(tc, test_cleanup);
    tcase_add_test(tc, test_program_propagates_erase_failure);
    tcase_add_test(tc, test_read_returns_flash_contents);
    tcase_add_test(tc, test_callbacks_reject_null_context);
    tcase_add_test(tc, test_callbacks_reject_null_data);
    tcase_add_test(tc, test_program_spans_three_sectors);
    tcase_add_test(tc, test_writelock_writeunlock_noop);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int      fails;
    Suite   *s  = wolfboot_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
