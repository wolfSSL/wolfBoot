/* unit-flash-write-cc26x2.c
 *
 * Regression test for F-6869: hal_flash_write() and hal_flash_erase() in
 * hal/cc26x2.c discarded the Fapi status returned by FlashProgram() and
 * FlashSectorErase() and unconditionally returned 0, so a program or erase
 * that the flash controller rejected was reported to wolfBoot as a success.
 * Both now return -1 on any status other than FAPI_STATUS_SUCCESS.
 *
 * hal/cc26x2.c has no in-repo build target, so this test doubles as the
 * only compile coverage the file gets: it includes it directly, with
 * cc26x2_ti_stub/ standing in for the (not vendored) TI CC26x2 SDK headers.
 *
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
#include <string.h>

#include "ti-lib.h"
#include "image.h"

#define FLASH_BASE      0x00010000UL
#define ERASE_LOG_MAX   16

/* Injected Fapi status for the next FlashProgram() call. */
static uint32_t program_status;
/* Injected Fapi status for the Nth FlashSectorErase() call (1-based); every
 * other call returns FAPI_STATUS_SUCCESS. 0 disables the injection. */
static int erase_fail_on;

static int program_calls;
static uint32_t program_addr;
static uint32_t program_len;
static const uint8_t *program_src;

static int erase_calls;
static uint32_t erase_addr[ERASE_LOG_MAX];

uint32_t FlashProgram(uint8_t *pui8DataBuffer, uint32_t ui32Address,
        uint32_t ui32Count)
{
    program_calls++;
    program_src = pui8DataBuffer;
    program_addr = ui32Address;
    program_len = ui32Count;
    return program_status;
}

uint32_t FlashSectorErase(uint32_t ui32SectorAddress)
{
    erase_calls++;
    if (erase_calls <= ERASE_LOG_MAX)
        erase_addr[erase_calls - 1] = ui32SectorAddress;
    if (erase_fail_on != 0 && erase_calls == erase_fail_on)
        return FAPI_STATUS_FSM_ERROR;
    return FAPI_STATUS_SUCCESS;
}

/* The FSM is always idle on the host: the driver spins on this until it
 * reports ready. */
uint32_t FlashCheckFsmForReady(void)
{
    return FAPI_STATUS_FSM_READY;
}

/* Referenced by uart_read()/hal_init(), neither of which this test calls. */
int32_t UARTCharGet(uint32_t ui32Base) { (void)ui32Base; return 0; }
int32_t UARTCharGetNonBlocking(uint32_t ui32Base) { (void)ui32Base; return -1; }
void clock_init(void) { }

#include "../../hal/cc26x2.c"

static void setup(void)
{
    program_status = FAPI_STATUS_SUCCESS;
    erase_fail_on = 0;
    program_calls = 0;
    program_addr = 0;
    program_len = 0;
    program_src = NULL;
    erase_calls = 0;
    memset(erase_addr, 0, sizeof(erase_addr));
}

static void teardown(void)
{
}

/* A successful program still returns 0, and the arguments reach driverlib
 * unchanged. */
START_TEST(test_write_success)
{
    uint8_t data[8];
    int i;

    for (i = 0; i < 8; i++)
        data[i] = (uint8_t)(i + 1);

    ck_assert_int_eq(hal_flash_write(FLASH_BASE, data, sizeof(data)), 0);
    ck_assert_int_eq(program_calls, 1);
    ck_assert_uint_eq(program_addr, FLASH_BASE);
    ck_assert_uint_eq(program_len, sizeof(data));
    ck_assert_ptr_eq(program_src, data);
}
END_TEST

/* A rejected program must be reported as a failure. Before the fix this
 * returned 0 and wolfBoot went on to treat the unwritten page as valid. */
START_TEST(test_write_fsm_error)
{
    uint8_t data[8];

    memset(data, 0xA5, sizeof(data));
    program_status = FAPI_STATUS_FSM_ERROR;

    ck_assert_int_eq(hal_flash_write(FLASH_BASE, data, sizeof(data)), -1);
    ck_assert_int_eq(program_calls, 1);
}
END_TEST

/* Every other non-success status is a failure too, not just FSM_ERROR. */
START_TEST(test_write_fsm_busy)
{
    uint8_t data[4];

    memset(data, 0x5A, sizeof(data));
    program_status = FAPI_STATUS_FSM_BUSY;

    ck_assert_int_eq(hal_flash_write(FLASH_BASE, data, sizeof(data)), -1);
}
END_TEST

/* A multi-sector erase walks the sectors in order and returns 0. */
START_TEST(test_erase_success_multi_sector)
{
    int i;

    ck_assert_int_eq(hal_flash_erase(FLASH_BASE, 3 * WOLFBOOT_SECTOR_SIZE), 0);
    ck_assert_int_eq(erase_calls, 3);
    for (i = 0; i < 3; i++)
        ck_assert_uint_eq(erase_addr[i],
                FLASH_BASE + (uint32_t)(WOLFBOOT_SECTOR_SIZE * i));
}
END_TEST

/* A length shorter than one sector still erases exactly one sector. */
START_TEST(test_erase_success_partial_sector)
{
    ck_assert_int_eq(hal_flash_erase(FLASH_BASE, WOLFBOOT_SECTOR_SIZE / 2), 0);
    ck_assert_int_eq(erase_calls, 1);
    ck_assert_uint_eq(erase_addr[0], FLASH_BASE);
}
END_TEST

/* A rejected sector erase fails the whole call and stops immediately, rather
 * than erasing on and returning 0 as it did before the fix. */
START_TEST(test_erase_fsm_error_stops)
{
    erase_fail_on = 2;

    ck_assert_int_eq(hal_flash_erase(FLASH_BASE, 4 * WOLFBOOT_SECTOR_SIZE), -1);
    ck_assert_int_eq(erase_calls, 2);
}
END_TEST

/* The very first sector failing must not be mistaken for "nothing to do". */
START_TEST(test_erase_fsm_error_first_sector)
{
    erase_fail_on = 1;

    ck_assert_int_eq(hal_flash_erase(FLASH_BASE, WOLFBOOT_SECTOR_SIZE), -1);
    ck_assert_int_eq(erase_calls, 1);
}
END_TEST

Suite *flash_write_suite(void)
{
    Suite *s = suite_create("flash-write-cc26x2");
    TCase *tc = tcase_create("flash-write-cc26x2");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_write_success);
    tcase_add_test(tc, test_write_fsm_error);
    tcase_add_test(tc, test_write_fsm_busy);
    tcase_add_test(tc, test_erase_success_multi_sector);
    tcase_add_test(tc, test_erase_success_partial_sector);
    tcase_add_test(tc, test_erase_fsm_error_stops);
    tcase_add_test(tc, test_erase_fsm_error_first_sector);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = flash_write_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
