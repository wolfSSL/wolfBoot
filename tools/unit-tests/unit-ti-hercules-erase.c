/* unit-ti-hercules-erase.c
 *
 * Regression test: hal_flash_erase() in hal/ti_hercules.c selected the
 * bank geometry from the starting address only and iterated that bank's
 * sectors. A range extending past the end of the starting bank was
 * partially erased and reported as success. Cross-bank requests must be
 * rejected before anything is erased.
 *
 * The HAL needs the TI FAPI vendor headers and cannot be built on the
 * host, so the Makefile extracts f021_lookup_bank(),
 * hal_flash_unlock_helper() and hal_flash_erase() verbatim into
 * ti_hercules_erase_extract.h; the FAPI calls are emulated with a
 * two-bank geometry (bank0 [0x000000, 0x200000), bank1
 * [0x200000, 0x400000), two 0x100000 sectors per bank).
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The extracted functions are RAMFUNCTION; on the host that is nothing. */
#define RAMFUNCTION

/* FAPI stubs: two banks, two 0x100000 sectors each. */
typedef int Fapi_FlashBankType;
#define Fapi_FlashBank0 0
#define Fapi_FlashBank1 1
typedef int Fapi_StatusType;
#define Fapi_Status_Success 0
#define Fapi_Status_FsmReady 0
#define FAPI_CHECK_FSM_READY_BUSY fapi_check_fsm()

#define G_SECTORS_PER_BANK 2

typedef struct {
    uint32_t u32BankStartAddress;
    uint32_t u32NumberOfSectors;
    uint16_t au16SectorSizes[G_SECTORS_PER_BANK];
} Fapi_FlashBankSectorsType;

static int fapi_check_fsm(void)
{
    return Fapi_Status_FsmReady;
}

static Fapi_StatusType Fapi_getBankSectors(Fapi_FlashBankType bank,
    Fapi_FlashBankSectorsType *sectors)
{
    sectors->u32BankStartAddress = (bank == Fapi_FlashBank1) ? 0x200000 : 0;
    sectors->u32NumberOfSectors = G_SECTORS_PER_BANK;
    sectors->au16SectorSizes[0] = 0x100000 / 1024;
    sectors->au16SectorSizes[1] = 0x100000 / 1024;
    return Fapi_Status_Success;
}

static int Fapi_setActiveFlashBank(Fapi_FlashBankType bank)
{
    (void)bank;
    return 0;
}

static int Fapi_enableMainBankSectors(uint16_t en)
{
    (void)en;
    return 0;
}

/* Erase calls recorded by the stub. */
#define MAX_ERASES 8
static uint32_t g_erases[MAX_ERASES];
static int g_erases_n;
static int g_erase_fail;

static inline int f021_flash_erase(uint32_t address)
{
    if (g_erases_n < MAX_ERASES) {
        g_erases[g_erases_n] = address;
        g_erases_n++;
    }
    if (g_erase_fail) {
        return -1;
    }
    return 0;
}

void wolfBoot_printf(const char *format, ...)
{
    (void)format;
}

/* The real functions from hal/ti_hercules.c (extracted by the Makefile). */
#include "ti_hercules_erase_extract.h"

static void setup(void)
{
    memset(g_erases, 0, sizeof(g_erases));
    g_erases_n = 0;
    g_erase_fail = 0;
}

static void teardown(void)
{
}

/* An erase aligned to one bank sector: success, exactly one erase. */
START_TEST(test_erase_within_bank0)
{
    ck_assert_int_eq(hal_flash_erase(0x100000, 0x100000), 0);
    ck_assert_int_eq(g_erases_n, 1);
    ck_assert_uint_eq(g_erases[0], 0x100000);
}
END_TEST

/* An erase in the second bank: the lookup picks bank1 and the request
 * succeeds. */
START_TEST(test_erase_within_bank1)
{
    ck_assert_int_eq(hal_flash_erase(0x200000, 0x100000), 0);
    ck_assert_int_eq(g_erases_n, 1);
    ck_assert_uint_eq(g_erases[0], 0x200000);
}
END_TEST

/* The case: a range crossing the bank0/bank1 boundary must be rejected
 * before anything is erased. */
START_TEST(test_erase_crossing_bank_boundary_rejected)
{
    ck_assert_int_eq(hal_flash_erase(0x100000, 0x100001), -1);
    ck_assert_int_eq(g_erases_n, 0);
}
END_TEST

/* An erase of the whole bank0: success, both sectors erased. */
START_TEST(test_erase_whole_bank0)
{
    ck_assert_int_eq(hal_flash_erase(0, 0x200000), 0);
    ck_assert_int_eq(g_erases_n, 2);
    ck_assert_uint_eq(g_erases[0], 0);
    ck_assert_uint_eq(g_erases[1], 0x100000);
}
END_TEST

Suite *ti_hercules_erase_suite(void)
{
    Suite *s = suite_create("ti-hercules-erase");
    TCase *tc = tcase_create("ti-hercules-erase");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_erase_within_bank0);
    tcase_add_test(tc, test_erase_within_bank1);
    tcase_add_test(tc, test_erase_crossing_bank_boundary_rejected);
    tcase_add_test(tc, test_erase_whole_bank0);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = ti_hercules_erase_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
