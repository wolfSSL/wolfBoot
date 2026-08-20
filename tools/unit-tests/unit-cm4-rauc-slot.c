/* unit-cm4-rauc-slot.c
 *
 * Unit tests for the CM4 RAUC A/B slot arbiter hal_boot_slot_select() in
 * hal/cm4.c. The state machine itself (uboot_env_select_slot) is covered by
 * unit-ubootenv.c; this exercises the HAL wrapper that binds it to disk I/O,
 * and in particular the writeback gating that anti-rollback failover depends
 * on: a decrement must be persisted for a good env, and must NOT be persisted
 * when the env could not be read or failed CRC (writing the wiped buffer back
 * would destroy the last-good on-disk RAUC state).
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mock of the EMMC2 register window (hal/cm4.c casts this base); the RAUC path
 * never touches it, but the SDHCI glue in the same file needs a valid target. */
static uint8_t cm4_mock_emmc[0x100];
#define BCM2711_EMMC2_BASE ((uintptr_t)cm4_mock_emmc)

/* Compile the CM4 HAL for the host with the RAUC A/B arbiter enabled.
 * DISK_SDCARD satisfies the file's outer disk-block guard, and MMU is the
 * enclosing guard for the boot-slot/DTB block that holds the arbiter.
 * (CM4_USE_MMU stays off on the host: it also requires __aarch64__.) */
#define ARCH_AARCH64
#define DISK_SDCARD 1
#define MMU 1
#define CM4_RAUC_AB 1

#include "../../hal/cm4.c"

/* ---- mock disk backing the env partition -------------------------------- */

static uint8_t mock_part[UBOOT_ENV_SIZE];
/* Bytes the HAL is allowed to read; < UBOOT_ENV_SIZE simulates a short read. */
static int     mock_read_len;
/* Bytes a write is allowed to store; < UBOOT_ENV_SIZE simulates a failure. */
static int     mock_write_len;
static int     mock_write_calls;

int disk_part_read(int drv, int part, uint64_t off, uint64_t sz, uint8_t *buf)
{
    (void)drv; (void)part; (void)off;
    if (mock_read_len < 0)
        return -1;
    if ((uint64_t)mock_read_len < sz)
        sz = (uint64_t)mock_read_len;
    memcpy(buf, mock_part + off, (size_t)sz);
    return (int)sz;
}

int disk_part_write(int drv, int part, uint64_t off, uint64_t sz,
    const uint8_t *buf)
{
    (void)drv; (void)part; (void)off;
    mock_write_calls++;
    if (mock_write_len < 0)
        return -1;
    if ((uint64_t)mock_write_len < sz)
        sz = (uint64_t)mock_write_len;
    memcpy(mock_part + off, buf, (size_t)sz);
    return (int)sz;
}

/* ---- helpers ------------------------------------------------------------- */

/* Build a CRC-valid env in mock_part with BOOT_ORDER plus the given counters.
 * A negative count omits that slot's BOOT_<name>_LEFT key entirely. */
static void build_env(const char *order, int a_left, int b_left)
{
    char val[UBOOT_ENV_VAL_MAX];

    memset(mock_part, 0, sizeof(mock_part));
    uboot_env_reseal(mock_part, UBOOT_ENV_SIZE);
    ck_assert_int_eq(uboot_env_set(mock_part, UBOOT_ENV_SIZE, "BOOT_ORDER",
        order), 0);
    if (a_left >= 0) {
        snprintf(val, sizeof(val), "%d", a_left);
        ck_assert_int_eq(uboot_env_set(mock_part, UBOOT_ENV_SIZE,
            "BOOT_A_LEFT", val), 0);
    }
    if (b_left >= 0) {
        snprintf(val, sizeof(val), "%d", b_left);
        ck_assert_int_eq(uboot_env_set(mock_part, UBOOT_ENV_SIZE,
            "BOOT_B_LEFT", val), 0);
    }
    uboot_env_reseal(mock_part, UBOOT_ENV_SIZE);
    ck_assert_int_eq(uboot_env_verify(mock_part, UBOOT_ENV_SIZE), 1);
}

static void reset_mocks(void)
{
    mock_read_len   = UBOOT_ENV_SIZE;
    mock_write_len  = UBOOT_ENV_SIZE;
    mock_write_calls = 0;
    cm4_rauc_selected = 0;
    cm4_rauc_root = NULL;
    memset(cm4_rauc_slot_name, 0, sizeof(cm4_rauc_slot_name));
    memset(cm4_uboot_env, 0, sizeof(cm4_uboot_env));
}

/* Read a counter straight out of the mock partition (post-writeback state). */
static long part_counter(const char *key)
{
    char val[UBOOT_ENV_VAL_MAX];
    if (uboot_env_get(mock_part, UBOOT_ENV_SIZE, key, val, sizeof(val)) < 0)
        return -1;
    return atol(val);
}

/* ---- tests --------------------------------------------------------------- */

/* (a) Good env, valid CRC: slot A selected, counter decremented AND persisted. */
START_TEST(test_good_env_decrements_and_persists)
{
    reset_mocks();
    build_env("A B", 3, 3);

    ck_assert_int_eq(hal_boot_slot_select(), 0);
    ck_assert_int_eq(cm4_rauc_selected, 1);
    ck_assert_str_eq(cm4_rauc_slot_name, "A");
    ck_assert_str_eq(cm4_rauc_root, CM4_ROOT_A);
    ck_assert_int_eq(mock_write_calls, 1);
    /* The decrement must be on disk, not just in RAM. */
    ck_assert_int_eq((int)part_counter("BOOT_A_LEFT"), 2);
    ck_assert_int_eq((int)part_counter("BOOT_B_LEFT"), 3);
}
END_TEST

/* Slot B is chosen when BOOT_ORDER asks for it, and maps to CM4_ROOT_B. */
START_TEST(test_order_selects_slot_b)
{
    reset_mocks();
    build_env("B A", 3, 3);

    ck_assert_int_eq(hal_boot_slot_select(), 0);
    ck_assert_int_eq(cm4_rauc_selected, 1);
    ck_assert_str_eq(cm4_rauc_slot_name, "B");
    ck_assert_str_eq(cm4_rauc_root, CM4_ROOT_B);
    ck_assert_int_eq((int)part_counter("BOOT_B_LEFT"), 2);
}
END_TEST

/* An exhausted slot is skipped in favour of one that still has tries. */
START_TEST(test_exhausted_slot_is_skipped)
{
    reset_mocks();
    build_env("A B", 0, 3);

    ck_assert_int_eq(hal_boot_slot_select(), 0);
    ck_assert_int_eq(cm4_rauc_selected, 1);
    ck_assert_str_eq(cm4_rauc_slot_name, "B");
    ck_assert_str_eq(cm4_rauc_root, CM4_ROOT_B);
}
END_TEST

/* (e) All slots exhausted: counters are re-armed and a slot is selected on the
 * second pass, so the device still boots rather than bricking. */
START_TEST(test_all_exhausted_rearms_and_selects)
{
    reset_mocks();
    build_env("A B", 0, 0);

    ck_assert_int_eq(hal_boot_slot_select(), 0);
    ck_assert_int_eq(cm4_rauc_selected, 1);
    ck_assert_str_eq(cm4_rauc_slot_name, "A");
    /* Re-armed then decremented for this boot. */
    ck_assert_int_eq((int)part_counter("BOOT_A_LEFT"),
        UBOOT_ENV_DEFAULT_TRIES - 1);
}
END_TEST

/* (b) Short read: the env is not trusted, so no slot is selected and - most
 * importantly - nothing is written back over the on-disk RAUC state. */
START_TEST(test_short_read_no_writeback)
{
    reset_mocks();
    build_env("A B", 3, 3);
    mock_read_len = UBOOT_ENV_SIZE - 1; /* partition smaller than the env */

    ck_assert_int_eq(hal_boot_slot_select(), 0);
    ck_assert_int_eq(mock_write_calls, 0);
    /* On-disk counters untouched. */
    ck_assert_int_eq((int)part_counter("BOOT_A_LEFT"), 3);
}
END_TEST

/* A hard read error behaves the same way: no writeback. */
START_TEST(test_read_error_no_writeback)
{
    reset_mocks();
    build_env("A B", 3, 3);
    mock_read_len = -1;

    ck_assert_int_eq(hal_boot_slot_select(), 0);
    ck_assert_int_eq(mock_write_calls, 0);
    ck_assert_int_eq((int)part_counter("BOOT_A_LEFT"), 3);
}
END_TEST

/* (c) Full read but corrupt CRC: the state machine reinitializes in RAM. The
 * HAL must NOT persist that wiped buffer - doing so would erase the last-good
 * RAUC state and permanently defeat failover. */
START_TEST(test_bad_crc_does_not_persist_reset)
{
    reset_mocks();
    build_env("B A", 3, 3);
    mock_part[8] ^= 0xFF; /* corrupt a payload byte, leaving the CRC stale */
    ck_assert_int_eq(uboot_env_verify(mock_part, UBOOT_ENV_SIZE), 0);

    ck_assert_int_eq(hal_boot_slot_select(), 0);
    ck_assert_int_eq(mock_write_calls, 0);
}
END_TEST

/* (d) Writeback fails: the counter is not durably decremented, so the HAL must
 * fall back to the static cmdline rather than boot a slot whose try count did
 * not stick (which would retry a hung slot forever). */
START_TEST(test_write_failure_falls_back_to_static_cmdline)
{
    reset_mocks();
    build_env("A B", 3, 3);
    mock_write_len = -1;

    ck_assert_int_eq(hal_boot_slot_select(), 0);
    ck_assert_int_eq(mock_write_calls, 1);
    ck_assert_int_eq(cm4_rauc_selected, 0); /* -> static LINUX_BOOTARGS */
}
END_TEST

/* A short (partial) write is a failure too - not a success. */
START_TEST(test_partial_write_falls_back)
{
    reset_mocks();
    build_env("A B", 3, 3);
    mock_write_len = UBOOT_ENV_SIZE / 2;

    ck_assert_int_eq(hal_boot_slot_select(), 0);
    ck_assert_int_eq(cm4_rauc_selected, 0);
}
END_TEST

/* An unrecognised bootname must fail loudly (static cmdline), not be coerced
 * into slot A/B by a prefix match. */
START_TEST(test_unknown_slot_name_falls_back)
{
    reset_mocks();
    build_env("system0 system1", -1, -1);

    ck_assert_int_eq(hal_boot_slot_select(), 0);
    ck_assert_int_eq(cm4_rauc_selected, 0);
}
END_TEST

/* A selected slot leaves cm4_rauc_build_bootargs() able to render the line;
 * with no slot it must refuse so the caller uses the static bootargs. */
START_TEST(test_bootargs_follow_selection)
{
    char buf[256];

    reset_mocks();
    build_env("A B", 3, 3);
    ck_assert_int_eq(hal_boot_slot_select(), 0);
    ck_assert_int_eq(cm4_rauc_build_bootargs(buf, sizeof(buf)), 0);
    ck_assert(strstr(buf, "root=" CM4_ROOT_A) != NULL);
    ck_assert(strstr(buf, "rauc.slot=A") != NULL);

    /* No selection -> refuse. */
    cm4_rauc_selected = 0;
    ck_assert_int_eq(cm4_rauc_build_bootargs(buf, sizeof(buf)), -1);
}
END_TEST

/* Truncation must be reported, never silently emit a half command line. */
START_TEST(test_bootargs_truncation_refused)
{
    char small[8];

    reset_mocks();
    build_env("A B", 3, 3);
    ck_assert_int_eq(hal_boot_slot_select(), 0);
    ck_assert_int_eq(cm4_rauc_build_bootargs(small, sizeof(small)), -1);
}
END_TEST

Suite *cm4_rauc_suite(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("cm4-rauc-slot");
    tc = tcase_create("hal_boot_slot_select");
    tcase_add_test(tc, test_good_env_decrements_and_persists);
    tcase_add_test(tc, test_order_selects_slot_b);
    tcase_add_test(tc, test_exhausted_slot_is_skipped);
    tcase_add_test(tc, test_all_exhausted_rearms_and_selects);
    tcase_add_test(tc, test_short_read_no_writeback);
    tcase_add_test(tc, test_read_error_no_writeback);
    tcase_add_test(tc, test_bad_crc_does_not_persist_reset);
    tcase_add_test(tc, test_write_failure_falls_back_to_static_cmdline);
    tcase_add_test(tc, test_partial_write_falls_back);
    tcase_add_test(tc, test_unknown_slot_name_falls_back);
    tcase_add_test(tc, test_bootargs_follow_selection);
    tcase_add_test(tc, test_bootargs_truncation_refused);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    SRunner *sr;

    sr = srunner_create(cm4_rauc_suite());
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (fails == 0) ? 0 : 1;
}
