/* unit-diagnostics.c
 *
 * Unit tests for the persistent failure diagnostics store.
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
#define WOLFBOOT_HASH_SHA256
#define IMAGE_HEADER_SIZE 256
#define WOLFBOOT_PERSIST_FAILURE_STATUS
#define WOLFBOOT_DIAGNOSTICS_ADDRESS 0xD0000000
#define WOLFBOOT_DIAGNOSTICS_SECTORS 2
#define WC_RSA_BLINDING
#define ECC_TIMING_RESISTANT
#include <stdio.h>
#include "libwolfboot.c"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <check.h>

#include "unit-mock-flash.c"

Suite *wolfboot_suite(void);

#define DIAG_REGION_SIZE (WOLFBOOT_DIAGNOSTICS_SECTORS * WOLFBOOT_SECTOR_SIZE)

static void diag_mmap(const char *path)
{
    int ret = mmap_file(path, (void *)(uintptr_t)WOLFBOOT_DIAGNOSTICS_ADDRESS,
            DIAG_REGION_SIZE, NULL);
    ck_assert(ret >= 0);
}

static void record_one(uint8_t phase, uint8_t cause, uint8_t part, uint32_t ver)
{
    int ret = wolfBoot_record_failure(phase, cause, part, ver);
    ck_assert_int_eq(ret, 0);
}

START_TEST(test_record_and_read_newest_first)
{
    struct wolfBoot_failure_record rec, oldest;

    diag_mmap("/tmp/wolfboot-unit-diag-basic.bin");
    ck_assert_int_eq(wolfBoot_clear_failures(), 0);

    /* empty store */
    ck_assert_int_eq(wolfBoot_get_failure_count(), 0);
    ck_assert_int_eq(wolfBoot_get_failure(0, &rec), -1);

    record_one(WOLFBOOT_FAILURE_PHASE_UPDATE,
            WOLFBOOT_FAILURE_CAUSE_HASH, PART_UPDATE, 11);
    record_one(WOLFBOOT_FAILURE_PHASE_BOOT,
            WOLFBOOT_FAILURE_CAUSE_SIGNATURE, PART_BOOT, 22);
    record_one(WOLFBOOT_FAILURE_PHASE_ROLLBACK,
            WOLFBOOT_FAILURE_CAUSE_NOT_CONFIRMED, PART_BOOT, 33);

    ck_assert_int_eq(wolfBoot_get_failure_count(), 3);

    /* index 0 is the most recent record */
    ck_assert_int_eq(wolfBoot_get_failure(0, &rec), 0);
    ck_assert_uint_eq(rec.phase, WOLFBOOT_FAILURE_PHASE_ROLLBACK);
    ck_assert_uint_eq(rec.cause, WOLFBOOT_FAILURE_CAUSE_NOT_CONFIRMED);
    ck_assert_uint_eq(rec.partition, PART_BOOT);
    ck_assert_uint_eq(rec.fw_version, 33);

    ck_assert_int_eq(wolfBoot_get_failure(1, &rec), 0);
    ck_assert_uint_eq(rec.phase, WOLFBOOT_FAILURE_PHASE_BOOT);
    ck_assert_uint_eq(rec.cause, WOLFBOOT_FAILURE_CAUSE_SIGNATURE);
    ck_assert_uint_eq(rec.fw_version, 22);

    ck_assert_int_eq(wolfBoot_get_failure(2, &oldest), 0);
    ck_assert_uint_eq(oldest.phase, WOLFBOOT_FAILURE_PHASE_UPDATE);
    ck_assert_uint_eq(oldest.cause, WOLFBOOT_FAILURE_CAUSE_HASH);
    ck_assert_uint_eq(oldest.partition, PART_UPDATE);
    ck_assert_uint_eq(oldest.fw_version, 11);

    /* sequence numbers increase from oldest to newest */
    wolfBoot_get_failure(0, &rec);
    ck_assert_uint_gt(rec.seq, oldest.seq);

    /* invalid arguments */
    ck_assert_int_eq(wolfBoot_get_failure(3, &rec), -1);
    ck_assert_int_eq(wolfBoot_get_failure(-1, &rec), -1);
    ck_assert_int_eq(wolfBoot_get_failure(0, NULL), -1);
}
END_TEST

START_TEST(test_ring_wrap_and_ordering)
{
    struct wolfBoot_failure_record rec;
    int slots = (int)DIAG_SLOTS_PER_SECTOR;
    int total = 2 * slots + 1; /* force a full wrap back onto the first sector */
    int count, i;

    diag_mmap("/tmp/wolfboot-unit-diag-wrap.bin");
    ck_assert_int_eq(wolfBoot_clear_failures(), 0);

    for (i = 1; i <= total; i++)
        record_one(WOLFBOOT_FAILURE_PHASE_UPDATE,
                WOLFBOOT_FAILURE_CAUSE_HASH, PART_UPDATE, (uint32_t)i);

    /* With two sectors, after wrapping we keep the most recent full sector plus
     * the one record written into the freshly recycled sector. */
    count = wolfBoot_get_failure_count();
    ck_assert_int_eq(count, slots + 1);

    /* newest is the last record written */
    ck_assert_int_eq(wolfBoot_get_failure(0, &rec), 0);
    ck_assert_uint_eq(rec.fw_version, (uint32_t)total);

    /* oldest retained record */
    ck_assert_int_eq(wolfBoot_get_failure(count - 1, &rec), 0);
    ck_assert_uint_eq(rec.fw_version, (uint32_t)(total - count + 1));

    /* records are returned strictly newest-first and contiguous */
    for (i = 0; i < count; i++) {
        ck_assert_int_eq(wolfBoot_get_failure(i, &rec), 0);
        ck_assert_uint_eq(rec.fw_version, (uint32_t)(total - i));
    }
}
END_TEST

START_TEST(test_crc_rejection)
{
    struct wolfBoot_failure_record rec;
    uint8_t *p;

    diag_mmap("/tmp/wolfboot-unit-diag-crc.bin");
    ck_assert_int_eq(wolfBoot_clear_failures(), 0);

    record_one(WOLFBOOT_FAILURE_PHASE_UPDATE,
            WOLFBOOT_FAILURE_CAUSE_HASH, PART_UPDATE, 1);
    record_one(WOLFBOOT_FAILURE_PHASE_UPDATE,
            WOLFBOOT_FAILURE_CAUSE_HASH, PART_UPDATE, 2);
    record_one(WOLFBOOT_FAILURE_PHASE_UPDATE,
            WOLFBOOT_FAILURE_CAUSE_HASH, PART_UPDATE, 3);
    record_one(WOLFBOOT_FAILURE_PHASE_UPDATE,
            WOLFBOOT_FAILURE_CAUSE_HASH, PART_UPDATE, 4);
    record_one(WOLFBOOT_FAILURE_PHASE_UPDATE,
            WOLFBOOT_FAILURE_CAUSE_HASH, PART_UPDATE, 5);
    ck_assert_int_eq(wolfBoot_get_failure_count(), 5);

    /* Corrupt the 3rd record (slot 2) of the active sector. Records are read
     * contiguously, so the count truncates at the first invalid slot. */
    p = (uint8_t *)(uintptr_t)(DIAG_SECTOR_ADDR(0) + DIAG_HDR_SIZE +
            2 * DIAG_RECORD_SIZE);
    p[0] ^= 0xFF;
    ck_assert_int_eq(wolfBoot_get_failure_count(), 2);
    ck_assert_int_eq(wolfBoot_get_failure(0, &rec), 0);
    ck_assert_uint_eq(rec.fw_version, 2);

    /* Corrupt the sector header: the whole sector is rejected. */
    p = (uint8_t *)(uintptr_t)DIAG_SECTOR_ADDR(0);
    p[0] ^= 0xFF;
    ck_assert_int_eq(wolfBoot_get_failure_count(), 0);
    ck_assert_int_eq(wolfBoot_get_failure(0, &rec), -1);
}
END_TEST

START_TEST(test_clear)
{
    struct wolfBoot_failure_record rec;

    diag_mmap("/tmp/wolfboot-unit-diag-clear.bin");
    ck_assert_int_eq(wolfBoot_clear_failures(), 0);

    record_one(WOLFBOOT_FAILURE_PHASE_BOOT,
            WOLFBOOT_FAILURE_CAUSE_HEADER, PART_BOOT, 1);
    record_one(WOLFBOOT_FAILURE_PHASE_BOOT,
            WOLFBOOT_FAILURE_CAUSE_HASH, PART_BOOT, 2);
    ck_assert_int_eq(wolfBoot_get_failure_count(), 2);

    ck_assert_int_eq(wolfBoot_clear_failures(), 0);
    ck_assert_int_eq(wolfBoot_get_failure_count(), 0);
    ck_assert_int_eq(wolfBoot_get_failure(0, &rec), -1);

    /* recording works again after a clear; the sequence restarts */
    record_one(WOLFBOOT_FAILURE_PHASE_UPDATE,
            WOLFBOOT_FAILURE_CAUSE_SIGNATURE, PART_UPDATE, 7);
    ck_assert_int_eq(wolfBoot_get_failure_count(), 1);
    ck_assert_int_eq(wolfBoot_get_failure(0, &rec), 0);
    ck_assert_uint_eq(rec.fw_version, 7);
    ck_assert_uint_eq(rec.seq, 1);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-diagnostics");
    TCase *diag = tcase_create("Failure diagnostics");
    tcase_add_test(diag, test_record_and_read_newest_first);
    tcase_add_test(diag, test_ring_wrap_and_ordering);
    tcase_add_test(diag, test_crc_rejection);
    tcase_add_test(diag, test_clear);
    suite_add_tcase(s, diag);
    return s;
}

int main(int argc, char *argv[])
{
    int fails;
    argv0 = strdup(argv[0]);
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
