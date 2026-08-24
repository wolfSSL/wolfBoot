/* unit-fdt-memrsv-wrap.c
 *
 * Regression test for F-11045: fdt_add_mem_rsv() in src/fdt.c added the
 * 32-bit string-block offset and size without overflow checks. A
 * wrapped data_end bypassed the capacity check, and the same wrapped
 * expression derived the memmove length, so a malformed (or
 * attacker-supplied) DTB produced a huge memmove - broad boot-time
 * memory corruption. fdt_check_header validates only magic and
 * version, so raw-DTB callers reach this code with inconsistent
 * layout fields.
 *
 * The fix uses 64-bit arithmetic for the block end, validates the
 * ordering of the reserve map / structure / string blocks, and
 * derives the move length only after validation.
 *
 * The real fdt_add_mem_rsv() (plus the byte-order helpers) is
 * extracted by the Makefile; the FDT under test is a crafted header
 * plus block layout in a static buffer.
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

#define wolfBoot_printf(...) ((void)0)

/* Byte-order helpers and the code under test, from src/fdt.c
 * (extracted by the Makefile). */
#include "fdt_memrsv_extract.h"

#define FDT_MAGIC_V 0xD00DFEEDu
#define BUF_SZ 96
static uint8_t g_buf[BUF_SZ];

/* Craft a header with the given layout fields. The blocks are laid
 * out as: 40-byte header, reserve map (16-byte terminator), a 4-byte
 * structure block, a 4-byte string block, trailing slack. */
static void make_fdt(uint32_t total, uint32_t off_rsv, uint32_t off_dt,
                     uint32_t off_str, uint32_t size_str)
{
    struct fdt_header *h = (struct fdt_header *)g_buf;

    memset(g_buf, 0x77, sizeof(g_buf));

    h->magic           = cpu_to_fdt32(FDT_MAGIC_V);
    h->totalsize       = cpu_to_fdt32(total);
    h->off_dt_struct   = cpu_to_fdt32(off_dt);
    h->off_dt_strings  = cpu_to_fdt32(off_str);
    h->off_mem_rsvmap  = cpu_to_fdt32(off_rsv);
    h->version         = cpu_to_fdt32(17);
    h->last_comp_version = cpu_to_fdt32(16);
    h->boot_cpuid_phys = cpu_to_fdt32(0);
    h->size_dt_strings = cpu_to_fdt32(size_str);
    h->size_dt_struct  = cpu_to_fdt32(0);

    /* Block contents only where they fit: the malformed-layout tests
     * carry out-of-buffer offsets, and the code under test must reject
     * them before touching the blocks. */
    if (off_rsv + 16 <= BUF_SZ) {
        memset(g_buf + off_rsv, 0, 16);
    }
    if (off_dt + 4 <= BUF_SZ) {
        memset(g_buf + off_dt, 0, 4);
    }
    if (off_str + 2 <= BUF_SZ) {
        g_buf[off_str] = 'x';
        g_buf[off_str + 1] = 0;
    }
}

/* off_str + size_str wraps past 2^32 and the wrapped end lands below
 * off_dt: pre-fix the memmove length wraps to ~2^32 (crash/corruption),
 * post-fix the 64-bit end is far past totalsize and rejected. */
START_TEST (test_wrap_past_off_dt_rejected)
{
    int ret;

    /* off_str + size_str = 0x100000010 (wraps to 0x10 in 32-bit). */
    make_fdt(0x2000, 40, 0x100, 0xFFFFFFF0u, 0x100u);

    ret = fdt_add_mem_rsv(g_buf, 0x1000, 0x400);
    ck_assert_int_ne(ret, 0);
}
END_TEST

/* The wrapped end lands inside [off_dt, total): pre-fix this passed
 * the capacity check, moved 0 bytes, and still published shifted
 * header offsets (a silently corrupted FDT). Post-fix it is rejected. */
START_TEST (test_wrap_inside_range_rejected)
{
    uint32_t off_str_before;
    int ret;

    /* off_str + size_str = 0x100000100 (wraps to 0x100 in 32-bit)
     * which equals off_dt. */
    make_fdt(0x2000, 40, 0x100, 0xFFFFFF00u, 0x200u);
    off_str_before = fdt_off_dt_strings(g_buf);

    ret = fdt_add_mem_rsv(g_buf, 0x1000, 0x400);
    ck_assert_int_ne(ret, 0);
    ck_assert_uint_eq(fdt_off_dt_strings(g_buf), off_str_before);
    ck_assert_uint_eq(fdt_off_dt_struct(g_buf), 0x100u);
}
END_TEST

/* A consistent layout still accepts the insertion: the entry goes in
 * where the terminator was, the terminator moves down one, structure
 * and string blocks shift by 16, and the header offsets follow. */
START_TEST (test_valid_insertion_works)
{
    struct fdt_reserve_entry *rsv;
    int i;
    int ret;

    /* Layout: header [0,40), rsv map [40,56) terminator, struct
     * [56,60), strings [60,64); 32 bytes of slack for the shift. */
    make_fdt(96, 40, 56, 60, 4);

    ret = fdt_add_mem_rsv(g_buf, 0x80000000ULL, 0x400000ULL);
    ck_assert_int_eq(ret, 0);

    /* New entry where the terminator was, then the terminator. */
    rsv = (struct fdt_reserve_entry *)(g_buf + 40);
    ck_assert_uint_eq(fdt64_to_cpu(rsv[0].address), 0x80000000ULL);
    ck_assert_uint_eq(fdt64_to_cpu(rsv[0].size), 0x400000ULL);
    ck_assert_uint_eq(fdt64_to_cpu(rsv[1].address), 0ULL);
    ck_assert_uint_eq(fdt64_to_cpu(rsv[1].size), 0ULL);

    /* Header offsets shifted by one reserve entry (16 bytes). */
    ck_assert_uint_eq(fdt_off_dt_struct(g_buf), 56 + 16);
    ck_assert_uint_eq(fdt_off_dt_strings(g_buf), 60 + 16);

    /* Structure block (4-byte end marker) and string block moved intact. */
    for (i = 72; i < 76; i++)
        ck_assert_uint_eq(g_buf[i], 0);
    ck_assert_uint_eq(g_buf[76], 'x');
    ck_assert_uint_eq(g_buf[77], 0);
}
END_TEST

Suite *fdt_memrsv_wrap_suite(void)
{
    Suite *s  = suite_create("fdt memrsv wrap");
    TCase *tc = tcase_create("layout-validation");

    tcase_add_test(tc, test_wrap_past_off_dt_rejected);
    tcase_add_test(tc, test_wrap_inside_range_rejected);
    tcase_add_test(tc, test_valid_insertion_works);
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
