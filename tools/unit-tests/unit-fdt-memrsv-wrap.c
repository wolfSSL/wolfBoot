/* unit-fdt-memrsv-wrap.c
 *
 * Regression test for F-11045 and for the layout validation that now
 * stands in front of it.
 *
 * The original defect: fdt_add_mem_rsv() added the 32-bit string-block
 * offset and size without overflow checks. A wrapped end bypassed the
 * capacity check and the same wrapped expression derived the memmove
 * length, so a malformed (or attacker-supplied) DTB produced a huge
 * memmove - broad boot-time memory corruption. Raw-DTB callers reached
 * that code with inconsistent layout fields because the header check of
 * the day validated only magic and version.
 *
 * Since the parser rewrite the inconsistent layouts are rejected by
 * fdt_open() before any code can act on them, so the first two cases
 * assert on fdt_open(). The insertion case then exercises the real
 * fdt_add_mem_rsv() on a valid tree, including the bound that the whole
 * insert is checked against the caller's window.
 *
 * This links the real src/fdt.c rather than sed-extracting one function
 * out of it.
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

#include "fdt.h"

void wolfBoot_printf(const char *fmt, ...)
{
    (void)fmt;
}

#define BUF_SZ 128
static uint8_t g_buf[BUF_SZ] __attribute__((aligned(4)));

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t rd64(const uint8_t *p)
{
    uint64_t v = 0;
    int i;

    for (i = 0; i < 8; i++) {
        v = (v << 8) | (uint64_t)p[i];
    }
    return v;
}


/* Craft a header with the given layout fields, over a minimal but
 * well-formed structure block. Block contents are written only where
 * they fit: the malformed-layout cases carry out-of-buffer offsets and
 * must be rejected before anything touches the blocks. */
static void make_fdt(uint32_t total, uint32_t off_rsv, uint32_t off_dt,
                     uint32_t size_dt, uint32_t off_str, uint32_t size_str)
{
    memset(g_buf, 0x77, sizeof(g_buf));

    be32(g_buf + FDT_H_MAGIC, (uint32_t)FDT_MAGIC);
    be32(g_buf + FDT_H_TOTALSIZE, total);
    be32(g_buf + FDT_H_OFF_STRUCT, off_dt);
    be32(g_buf + FDT_H_OFF_STRINGS, off_str);
    be32(g_buf + FDT_H_OFF_RSVMAP, off_rsv);
    be32(g_buf + FDT_H_VERSION, 17);
    be32(g_buf + FDT_H_LAST_COMP, 16);
    be32(g_buf + 28, 0); /* boot_cpuid_phys */
    be32(g_buf + FDT_H_SIZE_STRINGS, size_str);
    be32(g_buf + FDT_H_SIZE_STRUCT, size_dt);

    if (off_rsv + FDT_RSV_ENTRY_SIZE <= BUF_SZ) {
        memset(g_buf + off_rsv, 0, FDT_RSV_ENTRY_SIZE);
    }
    if (off_dt + 16 <= BUF_SZ) {
        /* root node, empty name, closed, then FDT_END */
        be32(g_buf + off_dt + 0, FDT_BEGIN_NODE);
        be32(g_buf + off_dt + 4, 0);
        be32(g_buf + off_dt + 8, FDT_END_NODE);
        be32(g_buf + off_dt + 12, FDT_END);
    }
    if (off_str + 2 <= BUF_SZ) {
        g_buf[off_str] = 'x';
        g_buf[off_str + 1] = 0;
    }
}

/* off_str + size_str wraps past 2^32 and the wrapped end lands below
 * off_dt. Pre-fix this produced a ~2^32 memmove length; now the layout
 * never gets past fdt_open(). */
START_TEST (test_wrap_past_off_dt_rejected)
{
    fdt_ctx ctx;

    /* off_str + size_str = 0x100000010 (wraps to 0x10 in 32-bit). */
    make_fdt(0x2000, 40, 0x100, 16, 0xFFFFFFF0u, 0x100u);
    ck_assert_int_lt(fdt_open(&ctx, g_buf, BUF_SZ), 0);
}
END_TEST

/* The wrapped end lands inside [off_dt, total): pre-fix this passed the
 * capacity check, moved 0 bytes, and still published shifted header
 * offsets - a silently corrupted FDT. */
START_TEST (test_wrap_inside_range_rejected)
{
    fdt_ctx ctx;

    /* off_str + size_str = 0x100000100 (wraps to 0x100), equal to off_dt. */
    make_fdt(0x2000, 40, 0x100, 16, 0xFFFFFF00u, 0x200u);
    ck_assert_int_lt(fdt_open(&ctx, g_buf, BUF_SZ), 0);

    /* the header must be left exactly as it was found */
    ck_assert_uint_eq(rd32(g_buf + FDT_H_OFF_STRINGS), 0xFFFFFF00u);
    ck_assert_uint_eq(rd32(g_buf + FDT_H_OFF_STRUCT), 0x100u);
}
END_TEST

/* A totalsize larger than the window is rejected even though the block
 * layout is internally consistent. */
START_TEST (test_totalsize_past_window_rejected)
{
    fdt_ctx ctx;

    make_fdt(0x2000, 40, 56, 16, 72, 1);
    ck_assert_int_lt(fdt_open(&ctx, g_buf, BUF_SZ), 0);
}
END_TEST

/* A consistent layout still accepts the insertion: the entry goes in
 * where the terminator was, the terminator moves down one, structure
 * and string blocks shift by 16, and the header offsets follow. */
START_TEST (test_valid_insertion_works)
{
    fdt_ctx ctx;

    /* header [0,40), rsv map [40,56) terminator, struct [56,72),
     * strings [72,73); the rest of the buffer is slack for the shift. */
    make_fdt(73, 40, 56, 16, 72, 1);
    g_buf[72] = 0; /* strings block must end in a NUL */

    ck_assert_int_eq(fdt_open(&ctx, g_buf, BUF_SZ), 0);
    ck_assert_int_eq(fdt_add_mem_rsv(&ctx, 0x80000000ULL, 0x400000ULL), 0);

    /* New entry where the terminator was, then the terminator. */
    ck_assert_uint_eq(rd64(g_buf + 40), 0x80000000ULL);
    ck_assert_uint_eq(rd64(g_buf + 48), 0x400000ULL);
    ck_assert_uint_eq(rd64(g_buf + 56), 0ULL);
    ck_assert_uint_eq(rd64(g_buf + 64), 0ULL);

    /* Header offsets shifted by one reserve entry (16 bytes). */
    ck_assert_uint_eq(rd32(g_buf + FDT_H_OFF_STRUCT), 56 + 16);
    ck_assert_uint_eq(rd32(g_buf + FDT_H_OFF_STRINGS), 72 + 16);

    /* The structure block moved intact and the tree still parses. */
    ck_assert_uint_eq(rd32(g_buf + 72), FDT_BEGIN_NODE);
    ck_assert_uint_eq(rd32(g_buf + 84), FDT_END);
    ck_assert_int_eq(fdt_open(&ctx, g_buf, BUF_SZ), 0);
    ck_assert_int_eq(fdt_path_offset(&ctx, "/"), 0);
}
END_TEST

/* The T2080 boot path inserts three entries back to back, so the second
 * and third have to walk past already-filled slots to find the
 * terminator. Only the empty-map case was covered before. */
START_TEST (test_repeated_insertion_walks_filled_slots)
{
    fdt_ctx ctx;
    int i;

    make_fdt(73, 40, 56, 16, 72, 1);
    g_buf[72] = 0;

    ck_assert_int_eq(fdt_open(&ctx, g_buf, BUF_SZ), 0);
    for (i = 0; i < 3; i++) {
        ck_assert_int_eq(fdt_add_mem_rsv(&ctx,
            0x80000000ULL + (uint64_t)i * 0x1000ULL, 0x1000ULL), 0);
    }

    /* three entries at consecutive slots, then the terminator */
    for (i = 0; i < 3; i++) {
        ck_assert_uint_eq(rd64(g_buf + 40 + (i * 16)),
            0x80000000ULL + (uint64_t)i * 0x1000ULL);
        ck_assert_uint_eq(rd64(g_buf + 48 + (i * 16)), 0x1000ULL);
    }
    ck_assert_uint_eq(rd64(g_buf + 40 + (3 * 16)), 0ULL);
    ck_assert_uint_eq(rd64(g_buf + 48 + (3 * 16)), 0ULL);

    /* the blocks moved down by exactly three entries, and it still parses */
    ck_assert_uint_eq(rd32(g_buf + FDT_H_OFF_STRUCT), 56 + (3 * 16));
    ck_assert_uint_eq(rd32(g_buf + FDT_H_OFF_STRINGS), 72 + (3 * 16));
    ck_assert_int_eq(fdt_open(&ctx, g_buf, BUF_SZ), 0);
    ck_assert_int_eq(fdt_path_offset(&ctx, "/"), 0);
}
END_TEST

/* The insert is bounded by the caller's window, not by the blob's own
 * totalsize: with no room for the 16-byte shift it must fail closed. */
START_TEST (test_insertion_without_room_rejected)
{
    fdt_ctx ctx;

    make_fdt(73, 40, 56, 16, 72, 1);
    g_buf[72] = 0;

    /* window ends exactly at the tree's content */
    ck_assert_int_eq(fdt_open(&ctx, g_buf, 73), 0);
    ck_assert_int_eq(fdt_add_mem_rsv(&ctx, 0x80000000ULL, 0x400000ULL),
        -FDT_ERR_NOSPACE);

    /* nothing moved */
    ck_assert_uint_eq(rd32(g_buf + FDT_H_OFF_STRUCT), 56);
    ck_assert_uint_eq(rd32(g_buf + FDT_H_OFF_STRINGS), 72);
}
END_TEST

Suite *fdt_memrsv_wrap_suite(void)
{
    Suite *s  = suite_create("fdt memrsv wrap");
    TCase *tc = tcase_create("layout-validation");

    tcase_add_test(tc, test_wrap_past_off_dt_rejected);
    tcase_add_test(tc, test_wrap_inside_range_rejected);
    tcase_add_test(tc, test_totalsize_past_window_rejected);
    tcase_add_test(tc, test_valid_insertion_works);
    tcase_add_test(tc, test_repeated_insertion_walks_filled_slots);
    tcase_add_test(tc, test_insertion_without_room_rejected);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = fdt_memrsv_wrap_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
