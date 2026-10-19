/* unit-elf-store-ecc.c
 *
 * Unit tests for the ELF scatter store in src/image.c, extracted by the
 * Makefile. The flash mock accepts only whole erased write units and
 * whole-sector erases, and segments that share a sector or a unit must still
 * land. With WOLFBOOT_ELF_FLASH_SCATTER_PRESERVE_SECTORS the rest of each
 * sector must keep its old data, and erased units must stay erased.
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
#include <string.h>

#define WOLFBOOT_SECTOR_SIZE      0x400
#ifndef WOLFBOOT_FLASH_WRITE_UNIT
#define WOLFBOOT_FLASH_WRITE_UNIT 32
#endif
#define FLASHBUFFER_SIZE          256
#ifdef WOLFBOOT_FLAGS_INVERT
#define FLASH_BYTE_ERASED         0x00
#else
#define FLASH_BYTE_ERASED         0xFF
#endif
#define XALIGNED(x)               __attribute__((aligned(x)))
#define wolfBoot_printf           printf

#define FLASH_SIZE (8 * WOLFBOOT_SECTOR_SIZE)
#define UNIT       WOLFBOOT_FLASH_WRITE_UNIT
#define UNITS      (FLASH_SIZE / UNIT)

static uint8_t g_flash[FLASH_SIZE] XALIGNED(WOLFBOOT_SECTOR_SIZE);
static uint8_t g_programmed[UNITS];
static int g_wseq[UNITS]; /* write call that programmed each unit */
static uint8_t g_src[FLASH_SIZE];
static int g_violations, g_erases, g_writes, g_is_erased_calls;

#define BASE ((uintptr_t)g_flash)

#ifdef NVM_FLASH_WRITEONCE
uint8_t NVM_CACHE[WOLFBOOT_SECTOR_SIZE];
#endif

int hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    uintptr_t off = address - BASE;
    int u;

    if ((address < BASE) || (off + len > FLASH_SIZE) ||
            (off % UNIT) || (len % UNIT)) {
        g_violations++;
        return -1;
    }
    for (u = 0; u < len / UNIT; u++) {
        if (g_programmed[off / UNIT + u]) {
            g_violations++;
            return -1;
        }
    }
    memcpy(g_flash + off, data, len);
    g_writes++;
    for (u = 0; u < len / UNIT; u++) {
        g_programmed[off / UNIT + u] = 1;
        g_wseq[off / UNIT + u] = g_writes;
    }
    return 0;
}

int hal_flash_erase(uintptr_t address, int len)
{
    uintptr_t off = address - BASE;

    if ((address < BASE) || (off + len > FLASH_SIZE) ||
            (off % WOLFBOOT_SECTOR_SIZE) || (len % WOLFBOOT_SECTOR_SIZE)) {
        g_violations++;
        return -1;
    }
    memset(g_flash + off, FLASH_BYTE_ERASED, len);
    memset(g_programmed + off / UNIT, 0, len / UNIT);
    g_erases += len / WOLFBOOT_SECTOR_SIZE;
    return 0;
}

int hal_flash_is_erased(uintptr_t address, int len)
{
    uintptr_t off = address - BASE;
    uintptr_t u;

    g_is_erased_calls++;
    if ((address < BASE) || (off + len > FLASH_SIZE) || (len <= 0)) {
        g_violations++;
        return -1;
    }
    for (u = off / UNIT; u <= (off + len - 1) / UNIT; u++) {
        if (g_programmed[u])
            return 0;
    }
    return 1;
}

void hal_flash_unlock(void) {}
void hal_flash_lock(void) {}

/* The code under test (extracted by the Makefile) */
#include "elf_store_extract.h"

static void setup(void)
{
    int i;
    /* Old contents everywhere, programmed */
    memset(g_flash, 0xA5, sizeof(g_flash));
    memset(g_programmed, 1, sizeof(g_programmed));
    memset(g_wseq, 0, sizeof(g_wseq));
    for (i = 0; i < FLASH_SIZE; i++)
        g_src[i] = (uint8_t)(i * 7 + 3);
    g_violations = g_erases = g_writes = g_is_erased_calls = 0;
}

static void teardown(void)
{
}

/* Erase whole units in [off, off + len). A read of erased flash may return
 * garbage, so they hold garbage. */
static void mark_erased(uint32_t off, uint32_t len)
{
    memset(g_flash + off, 0x3C, len);
    memset(g_programmed + off / UNIT, 0, len / UNIT);
}

struct seg {
    uint32_t dst; /* offset in g_flash */
    uint32_t src; /* offset in g_src */
    uint32_t len;
};

static int store(const struct seg *s, int n)
{
    struct elf_store st;
    int i;

    memset(&st, 0, sizeof(st));
    for (i = 0; i < n; i++) {
        if (elf_store_segment(&st, (uintptr_t)(g_src + s[i].src),
                BASE + s[i].dst, s[i].len, 0) != 0)
            return -1;
    }
    return elf_store_flush(&st);
}

/* Each segment holds its source bytes. The rest of each touched sector reads
 * as erased, or keeps its old data with
 * WOLFBOOT_ELF_FLASH_SCATTER_PRESERVE_SECTORS. */
static void check(const struct seg *s, int n)
{
    uint32_t off, sec_start, sec_end;
    int i, in_seg;

    sec_start = (s[0].dst / WOLFBOOT_SECTOR_SIZE) * WOLFBOOT_SECTOR_SIZE;
    sec_end = ((s[n - 1].dst + s[n - 1].len + WOLFBOOT_SECTOR_SIZE - 1) /
        WOLFBOOT_SECTOR_SIZE) * WOLFBOOT_SECTOR_SIZE;
    for (off = 0; off < FLASH_SIZE; off++) {
        in_seg = -1;
        for (i = 0; i < n; i++) {
            if ((off >= s[i].dst) && (off < s[i].dst + s[i].len))
                in_seg = i;
        }
        if (in_seg >= 0) {
            ck_assert_msg(g_flash[off] ==
                g_src[s[in_seg].src + off - s[in_seg].dst],
                "segment byte at 0x%x", off);
        }
#ifndef WOLFBOOT_ELF_FLASH_SCATTER_PRESERVE_SECTORS
        else if ((off >= sec_start) && (off < sec_end)) {
            ck_assert_msg(g_flash[off] == FLASH_BYTE_ERASED,
                "gap byte at 0x%x", off);
        }
#endif
        else {
            ck_assert_msg(g_flash[off] == 0xA5, "outside byte at 0x%x", off);
        }
    }
    ck_assert_int_eq(g_violations, 0);
}

/* Two segments share one write unit and one sector */
START_TEST(test_shared_unit)
{
    struct seg s[] = {
        { 0x410, 0x000, 0x32 },  /* ends inside unit 0x440 */
        { 0x450, 0x100, 0x64 },  /* starts in the same unit */
    };
    ck_assert_int_eq(store(s, 2), 0);
    check(s, 2);
    ck_assert_int_eq(g_erases, 1);
}
END_TEST

/* A segment crosses into the sector where the next one starts. That sector
 * is erased once, before the first write to it. */
START_TEST(test_shared_sector)
{
    struct seg s[] = {
        { 0x0300, 0x000, 0x1F0 }, /* sectors 0 and 1 */
        { 0x0500, 0x400, 0x300 }, /* sectors 1 and 2 */
        { 0x0800, 0x800, 0x020 }, /* sector 2, unit aligned */
        { 0x0821, 0x900, 0x005 }, /* sector 2, inside one unit */
    };
    ck_assert_int_eq(store(s, 4), 0);
    check(s, 4);
    ck_assert_int_eq(g_erases, 3);
}
END_TEST

/* Long aligned segment goes through the buffer in chunks */
START_TEST(test_long_segment)
{
    struct seg s[] = {
        { 0x400, 0x000, 0x7E0 },
    };
    ck_assert_int_eq(store(s, 1), 0);
    check(s, 1);
    ck_assert_int_eq(g_erases, 2);
}
END_TEST

/* Segments out of address order are refused, not written over */
START_TEST(test_out_of_order)
{
    struct seg s[] = {
        { 0x800, 0x000, 0x40 },
        { 0x400, 0x100, 0x40 },
    };
    ck_assert_int_eq(store(s, 2), -1);
    ck_assert_int_eq(g_violations, 0);
}
END_TEST

/* Zero-size segments are skipped */
START_TEST(test_empty_segment)
{
    struct seg s[] = {
        { 0x400, 0x000, 0x10 },
        { 0x410, 0x000, 0x00 },
        { 0x420, 0x100, 0x10 },
    };
    ck_assert_int_eq(store(s, 3), 0);
    ck_assert_int_eq(g_violations, 0);
    ck_assert_int_eq(g_erases, 1);
    ck_assert_uint_eq(g_flash[0x400], g_src[0]);
    ck_assert_uint_eq(g_flash[0x420], g_src[0x100]);
}
END_TEST

#ifdef WOLFBOOT_ELF_FLASH_SCATTER_PRESERVE_SECTORS
/* Erased units stay erased unless a segment writes into them, and garbage
 * read from them is never written back */
START_TEST(test_preserve_erased_units_stay_erased)
{
    struct seg s[] = {
        { 0x410, 0x000, 0x20 },  /* ends inside unit 0x420 */
    };
    uint32_t off;

    mark_erased(0x420, 0x20);
    mark_erased(0x600, 0x20);
    ck_assert_int_eq(store(s, 1), 0);
    ck_assert_int_eq(g_violations, 0);
    ck_assert_int_eq(g_erases, 1);
    for (off = 0x400; off < 0x410; off++)
        ck_assert_uint_eq(g_flash[off], 0xA5);
    for (off = 0x410; off < 0x430; off++)
        ck_assert_uint_eq(g_flash[off], g_src[off - 0x410]);
    for (off = 0x430; off < 0x440; off++)
        ck_assert_uint_eq(g_flash[off], FLASH_BYTE_ERASED);
    for (off = 0x600; off < 0x620; off++) {
        ck_assert_uint_eq(g_flash[off], FLASH_BYTE_ERASED);
        ck_assert_int_eq(g_programmed[off / UNIT], 0);
    }
    ck_assert_uint_eq(g_flash[0x620], 0xA5);
}
END_TEST

/* After the erase, units that held data are written before units that only
 * have segment data */
START_TEST(test_preserve_old_units_first)
{
    struct seg s[] = {
        { 0x500, 0x000, 0x100 },
    };
    int u, last_old = 0, first_new = 0x7FFFFFFF;

    mark_erased(0x500, 0x100);
    ck_assert_int_eq(store(s, 1), 0);
    check(s, 1);
    for (u = 0x400 / UNIT; u < 0x800 / UNIT; u++) {
        ck_assert_int_ne(g_wseq[u], 0);
        if ((u >= 0x500 / UNIT) && (u < 0x600 / UNIT)) {
            if (g_wseq[u] < first_new)
                first_new = g_wseq[u];
        }
        else if (g_wseq[u] > last_old) {
            last_old = g_wseq[u];
        }
    }
    ck_assert_int_lt(last_old, first_new);
}
END_TEST

/* A sector that one segment covers completely is not read or checked */
START_TEST(test_preserve_covered_sector_not_read)
{
    struct seg s[] = {
        { 0x400, 0x000, 0x800 }, /* sectors 1 and 2 */
    };
    ck_assert_int_eq(store(s, 1), 0);
    check(s, 1);
    ck_assert_int_eq(g_erases, 2);
    ck_assert_int_eq(g_is_erased_calls, 0);
}
END_TEST

/* An erased sector is checked once, and only the segment is written */
START_TEST(test_preserve_erased_sector)
{
    struct seg s[] = {
        { 0x410, 0x000, 0x20 },
    };
    uint32_t off;

    mark_erased(0x400, WOLFBOOT_SECTOR_SIZE);
    ck_assert_int_eq(store(s, 1), 0);
    ck_assert_int_eq(g_violations, 0);
    ck_assert_int_eq(g_is_erased_calls, 1);
    for (off = 0x410; off < 0x430; off++)
        ck_assert_uint_eq(g_flash[off], g_src[off - 0x410]);
    for (off = 0x440; off < 0x800; off++) {
        ck_assert_uint_eq(g_flash[off], FLASH_BYTE_ERASED);
        ck_assert_int_eq(g_programmed[off / UNIT], 0);
    }
}
END_TEST

#ifdef NVM_FLASH_WRITEONCE
/* With NVM_FLASH_WRITEONCE the sector is staged in NVM_CACHE */
START_TEST(test_preserve_uses_nvm_cache)
{
    struct seg s[] = {
        { 0x410, 0x000, 0x20 },
    };
    memset(NVM_CACHE, 0, sizeof(NVM_CACHE));
    ck_assert_int_eq(store(s, 1), 0);
    check(s, 1);
    ck_assert_int_eq(memcmp(NVM_CACHE, g_flash + 0x400, WOLFBOOT_SECTOR_SIZE),
        0);
}
END_TEST
#endif
#endif /* WOLFBOOT_ELF_FLASH_SCATTER_PRESERVE_SECTORS */

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot-elf-store-ecc");
    TCase *tc = tcase_create("elf-store-ecc");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_shared_unit);
    tcase_add_test(tc, test_shared_sector);
    tcase_add_test(tc, test_long_segment);
    tcase_add_test(tc, test_out_of_order);
    tcase_add_test(tc, test_empty_segment);
#ifdef WOLFBOOT_ELF_FLASH_SCATTER_PRESERVE_SECTORS
    tcase_add_test(tc, test_preserve_erased_units_stay_erased);
    tcase_add_test(tc, test_preserve_old_units_first);
    tcase_add_test(tc, test_preserve_covered_sector_not_read);
    tcase_add_test(tc, test_preserve_erased_sector);
#ifdef NVM_FLASH_WRITEONCE
    tcase_add_test(tc, test_preserve_uses_nvm_cache);
#endif
#endif
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
