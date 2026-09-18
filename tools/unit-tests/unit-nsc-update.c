/* unit-nsc-update.c
 *
 * F-13636: unit target for the TrustZone NSC update-partition bounds checks.
 * wolfBoot_nsc_erase_update() / wolfBoot_nsc_write_update() take (address,
 * len) from an untrusted non-secure caller and turn them into a secure-world
 * erase/write at address + WOLFBOOT_PARTITION_UPDATE_ADDRESS. The only guard
 * is the pair of range checks; no unit target defined TZEN, so neither bound
 * (nor the WOLFBOOT_NSC_NS_RW NULL check) was exercised. This compiles
 * libwolfboot.c with __WOLFBOOT + TZEN (non-CMSE: WOLFBOOT_NSC_NS_RW is a
 * pass-through, so the range checks are testable on x86) against the mock
 * flash, and checks the accept/reject sides of both bounds.
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

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "user_settings.h"
#include "wolfboot/wolfboot.h"
#include "libwolfboot.c"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <check.h>
#include "unit-mock-flash.c"

const char *argv0;

/* Per-run backing file: a fixed path is rewritten (O_TRUNC|MAP_SHARED) by
 * every concurrent run, so two suites in parallel would share one mapping.
 * Set once in main() before Check forks, so the child that creates the file
 * and the parent that unlinks it agree on the name. */
static char update_flash_file[PATH_MAX];

static void prepare_update_flash(void)
{
    int ret;

    ret = mmap_file(update_flash_file,
                    (void *)WOLFBOOT_PARTITION_UPDATE_ADDRESS,
                    WOLFBOOT_PARTITION_SIZE,
                    NULL);
    ck_assert(ret >= 0);
    hal_flash_unlock();
    hal_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS,
                    WOLFBOOT_PARTITION_SIZE);
    hal_flash_lock();
}

START_TEST (test_nsc_erase_update_bounds){
    prepare_update_flash();

    /* Accept: the whole partition from 0. */
    ck_assert_int_eq(wolfBoot_nsc_erase_update(0,
                                               WOLFBOOT_PARTITION_UPDATE_SIZE),
                     0);
    /* Reject: address one past the partition. */
    ck_assert_int_eq(wolfBoot_nsc_erase_update(WOLFBOOT_PARTITION_UPDATE_SIZE +
                                               1, 0), -1);
    /* Reject: len one past the end. */
    ck_assert_int_eq(wolfBoot_nsc_erase_update(0,
                                               WOLFBOOT_PARTITION_UPDATE_SIZE +
                                               1), -1);
    /* Reject: straddles the partition end (4 bytes left, 8 asked). */
    ck_assert_int_eq(wolfBoot_nsc_erase_update(WOLFBOOT_PARTITION_UPDATE_SIZE -
                                               4, 8), -1);
}
END_TEST

START_TEST(test_nsc_write_update_bounds)
{
    uint8_t *buf;

    prepare_update_flash();

    buf = malloc(WOLFBOOT_PARTITION_UPDATE_SIZE);
    ck_assert_ptr_nonnull(buf);
    memset(buf, 0xAB, WOLFBOOT_PARTITION_UPDATE_SIZE);

    /* Accept: the whole partition from 0. */
    ck_assert_int_eq(wolfBoot_nsc_write_update(0, buf,
                                               WOLFBOOT_PARTITION_UPDATE_SIZE),
                     0);
    /* Reject: len one past the end. */
    ck_assert_int_eq(wolfBoot_nsc_write_update(0, buf,
                                               WOLFBOOT_PARTITION_UPDATE_SIZE +
                                               1), -1);
    /* Reject: address one past the partition. */
    ck_assert_int_eq(wolfBoot_nsc_write_update(WOLFBOOT_PARTITION_UPDATE_SIZE +
                                               1, buf, 0), -1);
    /* Reject: straddles the partition end (4 bytes left, 8 asked). */
    ck_assert_int_eq(wolfBoot_nsc_write_update(WOLFBOOT_PARTITION_UPDATE_SIZE -
                                               4, buf, 8), -1);

    free(buf);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("nsc-update");
    TCase *tcase = tcase_create("nsc-update-bounds");

    tcase_add_test(tcase, test_nsc_erase_update_bounds);
    tcase_add_test(tcase, test_nsc_write_update_bounds);
    suite_add_tcase(s, tcase);

    return s;
}

int main(int argc, char *argv[])
{
    int fails;
    Suite *s;
    SRunner *sr;

    argv0 = strdup(argv[0]);
    snprintf(update_flash_file, sizeof(update_flash_file),
             "/tmp/wolfboot-unit-nsc-update-%d.bin", (int)getpid());
    s = wolfboot_suite();
    sr = srunner_create(s);
#if (NO_FORK == 1)
    srunner_set_fork_status(sr, CK_NOFORK);
#endif
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    unlink(update_flash_file);
    return fails;
}
