/* unit-fdt.c
 *
 * Unit tests for the flattened device tree parser.
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

#include "../../include/fdt.h"

void wolfBoot_printf(const char *fmt, ...)
{
    (void)fmt;
}

/* ------------------------------------------------------------------ */
/* Blob construction helpers                                           */
/* ------------------------------------------------------------------ */


static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void hdr_set(uint8_t *buf, uint32_t field, uint32_t v)
{
    be32(buf + field, v);
}

/* Lay out a well-formed v17 blob in buf: header, an empty reservation
 * block, the caller's structure block and the caller's strings block.
 * Returns the total size written. */
static uint32_t build_fdt(uint8_t *buf, uint32_t bufsz,
    const uint8_t *sblk, uint32_t slen,
    const char *strblk, uint32_t strsz)
{
    uint32_t off_rsv = FDT_HEADER_SIZE;
    uint32_t off_struct = off_rsv + FDT_RSV_ENTRY_SIZE;
    uint32_t off_strings = off_struct + slen;
    uint32_t total = off_strings + strsz;

    ck_assert_uint_le(total, bufsz);
    memset(buf, 0, bufsz);

    hdr_set(buf, FDT_H_MAGIC, (uint32_t)FDT_MAGIC);
    hdr_set(buf, FDT_H_TOTALSIZE, total);
    hdr_set(buf, FDT_H_OFF_STRUCT, off_struct);
    hdr_set(buf, FDT_H_OFF_STRINGS, off_strings);
    hdr_set(buf, FDT_H_OFF_RSVMAP, off_rsv);
    hdr_set(buf, FDT_H_VERSION, 17);
    hdr_set(buf, FDT_H_LAST_COMP, 16);
    hdr_set(buf, FDT_H_SIZE_STRUCT, slen);
    hdr_set(buf, FDT_H_SIZE_STRINGS, strsz);
    memcpy(buf + off_struct, sblk, slen);
    memcpy(buf + off_strings, strblk, strsz);
    return total;
}

/* Structure block for a root node carrying one property: the raw `len`
 * bytes at `val`, named by string offset `nameoff`. */
static uint32_t build_root_prop_struct(uint8_t *sblk, uint32_t nameoff,
    const uint8_t *val, uint32_t len)
{
    uint32_t pad = FDT_TAGALIGN(len);
    uint32_t i = 0;

    be32(sblk + i, FDT_BEGIN_NODE); i += 4;
    be32(sblk + i, 0);              i += 4; /* empty root name */
    be32(sblk + i, FDT_PROP);       i += 4;
    be32(sblk + i, len);            i += 4;
    be32(sblk + i, nameoff);        i += 4;
    memset(sblk + i, 0, pad);
    memcpy(sblk + i, val, len);     i += pad;
    be32(sblk + i, FDT_END_NODE);   i += 4;
    be32(sblk + i, FDT_END);        i += 4;
    return i;
}

/* A root node whose `compatible` property holds the raw bytes val/len. */
static uint32_t build_compat_fdt(uint8_t *buf, uint32_t bufsz,
    const uint8_t *val, uint32_t len)
{
    uint8_t sblk[128];
    uint32_t slen = build_root_prop_struct(sblk, 0, val, len);

    return build_fdt(buf, bufsz, sblk, slen,
        "compatible", (uint32_t)sizeof("compatible"));
}

/* ------------------------------------------------------------------ */
/* fdt_open: the capacity bound                                        */
/* ------------------------------------------------------------------ */

/* The check that did not exist before the rewrite: a blob may not
 * declare itself larger than the buffer it lives in. Every other bound
 * in the parser is derived from this one. */
START_TEST(test_fdt_open_rejects_totalsize_beyond_capacity)
{
    static uint8_t buf[0x200];
    uint32_t total;
    fdt_ctx ctx;

    total = build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);

    /* honest capacity: accepted */
    ck_assert_int_eq(fdt_open(&ctx, buf, total), 0);
    ck_assert_uint_eq(fdt_size(&ctx), total);

    /* one byte short of what the header claims: rejected */
    ck_assert_int_lt(fdt_open(&ctx, buf, total - 1), 0);
    ck_assert_ptr_null(ctx.blob);

    /* header claims far more than the caller can address */
    hdr_set(buf, FDT_H_TOTALSIZE, 0x10000000);
    ck_assert_int_lt(fdt_open(&ctx, buf, total), 0);
}
END_TEST

START_TEST(test_fdt_open_rejects_unaligned_blob)
{
    static uint8_t buf[0x200 + 4];
    uint32_t total;
    fdt_ctx ctx;

    total = build_compat_fdt(buf, sizeof(buf) - 4,
        (const uint8_t *)"abc\0", 4);
    /* the parser makes aligned 32-bit loads, so it must refuse a base
     * it cannot make them from */
    memmove(buf + 1, buf, total);
    ck_assert_int_lt(fdt_open(&ctx, buf + 1, total), 0);
}
END_TEST

/* A structure block whose string table lies past the declared end of
 * the blob must be rejected. */
START_TEST(test_fdt_open_rejects_unbounded_string_area)
{
    static uint8_t buf[0x200];
    uint32_t total;
    fdt_ctx ctx;

    total = build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);
    hdr_set(buf, FDT_H_OFF_STRINGS, total);

    ck_assert_int_lt(fdt_open(&ctx, buf, total), 0);
}
END_TEST

/* The structure block must not overlap the string table. */
START_TEST(test_fdt_open_rejects_overlapping_areas)
{
    static uint8_t buf[0x200];
    uint32_t total;
    fdt_ctx ctx;

    total = build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);
    hdr_set(buf, FDT_H_SIZE_STRUCT, total);

    ck_assert_int_lt(fdt_open(&ctx, buf, total), 0);
}
END_TEST

/* off_dt_strings + size_dt_strings must not wrap uint32_t. */
START_TEST(test_fdt_open_rejects_dt_strings_area_overflow)
{
    static uint8_t buf[0x200];
    uint32_t total;
    fdt_ctx ctx;

    total = build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);
    hdr_set(buf, FDT_H_SIZE_STRINGS, 0xFFFFFFFC);

    ck_assert_int_lt(fdt_open(&ctx, buf, total), 0);
}
END_TEST

/* A property length that wraps the cursor arithmetic must be caught by
 * the structural walk, not handed to a caller as a negative int that
 * becomes a ~4GB memcpy size. */
START_TEST(test_fdt_open_rejects_oversized_prop_len)
{
    static uint8_t buf[0x200];
    uint32_t total;
    fdt_ctx ctx;

    total = build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);
    /* the property record starts 8 bytes into the structure block, so
     * its length field is at +12 */
    be32(buf + rd_be32(buf + FDT_H_OFF_STRUCT) + 12, 0xFFFFFFFF);

    ck_assert_int_lt(fdt_open(&ctx, buf, total), 0);
}
END_TEST

/* A tree with no properties has an empty strings block, which is legal
 * and must be accepted - the parser's "strings block ends in a NUL"
 * invariant only applies when there are strings. */
START_TEST(test_fdt_open_accepts_empty_strings_block)
{
    static uint8_t buf[0x200];
    uint8_t sblk[4 * 4];
    uint32_t i = 0, total;
    fdt_ctx ctx;

    be32(sblk + i, FDT_BEGIN_NODE); i += 4;
    be32(sblk + i, 0);              i += 4;
    be32(sblk + i, FDT_END_NODE);   i += 4;
    be32(sblk + i, FDT_END);        i += 4;

    total = build_fdt(buf, sizeof(buf), sblk, i, "", 0);
    ck_assert_int_eq(fdt_open(&ctx, buf, total), 0);
    ck_assert_int_eq(fdt_path_offset(&ctx, "/"), 0);
    /* with no strings, every string offset is out of range */
    ck_assert_ptr_null(fdt_get_string(&ctx, 0, NULL));
    ck_assert_ptr_null(fdt_getprop(&ctx, 0, "compatible", NULL));
}
END_TEST

/* Nesting must balance and there must be exactly one root. */
START_TEST(test_fdt_open_rejects_two_roots)
{
    static uint8_t buf[0x200];
    uint8_t sblk[7 * 4];
    uint32_t i = 0, total;
    fdt_ctx ctx;

    be32(sblk + i, FDT_BEGIN_NODE); i += 4;
    be32(sblk + i, 0);              i += 4;
    be32(sblk + i, FDT_END_NODE);   i += 4;
    be32(sblk + i, FDT_BEGIN_NODE); i += 4; /* a second root */
    be32(sblk + i, 0);              i += 4;
    be32(sblk + i, FDT_END_NODE);   i += 4;
    be32(sblk + i, FDT_END);        i += 4;

    total = build_fdt(buf, sizeof(buf), sblk, i, "", 1);
    ck_assert_int_lt(fdt_open(&ctx, buf, total), 0);
}
END_TEST

/* ------------------------------------------------------------------ */
/* fdt_get_string                                                      */
/* ------------------------------------------------------------------ */

static uint32_t build_strings_fdt(uint8_t *buf, uint32_t bufsz)
{
    uint8_t sblk[64];
    uint32_t i = 0;

    be32(sblk + i, FDT_BEGIN_NODE); i += 4;
    be32(sblk + i, 0);              i += 4;
    be32(sblk + i, FDT_END_NODE);   i += 4;
    be32(sblk + i, FDT_END);        i += 4;

    return build_fdt(buf, bufsz, sblk, i, "serial\0console", 15);
}

START_TEST(test_fdt_get_string_rejects_out_of_range_offset)
{
    static uint8_t buf[0x200];
    uint32_t total;
    fdt_ctx ctx;
    int len = 1234;
    const char *s;

    total = build_strings_fdt(buf, sizeof(buf));
    ck_assert_int_eq(fdt_open(&ctx, buf, total), 0);

    s = fdt_get_string(&ctx, 15, &len); /* exactly one past the block */
    ck_assert_ptr_null(s);
    ck_assert_int_eq(len, -FDT_ERR_BADOFFSET);

    s = fdt_get_string(&ctx, -1, &len);
    ck_assert_ptr_null(s);
    ck_assert_int_lt(len, 0);
}
END_TEST

START_TEST(test_fdt_get_string_returns_string_with_valid_offset)
{
    static uint8_t buf[0x200];
    uint32_t total;
    fdt_ctx ctx;
    int len = -1;
    const char *s;

    total = build_strings_fdt(buf, sizeof(buf));
    ck_assert_int_eq(fdt_open(&ctx, buf, total), 0);

    s = fdt_get_string(&ctx, 7, &len);
    ck_assert_ptr_nonnull(s);
    ck_assert_str_eq(s, "console");
    ck_assert_int_eq(len, 7);
}
END_TEST

/* ------------------------------------------------------------------ */
/* compatible string-list matching                                     */
/* ------------------------------------------------------------------ */

/* An entry whose declared length equals the search string leaves no
 * room for a NUL, so it must not match: the comparison must not read
 * the alignment padding as if it were the terminator. */
START_TEST(test_fdt_compatible_unterminated_exact_len_no_match)
{
    static uint8_t buf[0x200];
    uint32_t total;
    fdt_ctx ctx;

    total = build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc", 3);
    ck_assert_int_eq(fdt_open(&ctx, buf, total), 0);
    ck_assert_int_lt(fdt_node_offset_by_compatible(&ctx, -1, "abc"), 0);
}
END_TEST

/* A properly terminated entry of exactly the search length matches. */
START_TEST(test_fdt_compatible_terminated_exact_len_match)
{
    static uint8_t buf[0x200];
    uint32_t total;
    fdt_ctx ctx;

    total = build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);
    ck_assert_int_eq(fdt_open(&ctx, buf, total), 0);
    ck_assert_int_eq(fdt_node_offset_by_compatible(&ctx, -1, "abc"), 0);
}
END_TEST

/* Multi-string lists keep working: the second entry matches. */
START_TEST(test_fdt_compatible_multi_string_list_match)
{
    static uint8_t buf[0x200];
    uint32_t total;
    fdt_ctx ctx;

    total = build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"xy\0abc\0", 8);
    ck_assert_int_eq(fdt_open(&ctx, buf, total), 0);
    ck_assert_int_eq(fdt_node_offset_by_compatible(&ctx, -1, "abc"), 0);
}
END_TEST

/* A terminated entry that merely starts with the search string does not
 * match - the entry is longer. */
START_TEST(test_fdt_compatible_prefix_entry_no_match)
{
    static uint8_t buf[0x200];
    uint32_t total;
    fdt_ctx ctx;

    total = build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abcd\0", 5);
    ck_assert_int_eq(fdt_open(&ctx, buf, total), 0);
    ck_assert_int_lt(fdt_node_offset_by_compatible(&ctx, -1, "abc"), 0);
}
END_TEST

/* A value with no NUL inside its declared length is a legal property
 * (values are opaque byte arrays), so the blob is accepted - but the
 * string-list walk must terminate rather than run past the property. */
START_TEST(test_fdt_node_offset_by_compatible_terminates_on_unterminated_prop)
{
    static uint8_t buf[0x200];
    uint32_t total;
    fdt_ctx ctx;

    total = build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"AAAA", 4);
    ck_assert_int_eq(fdt_open(&ctx, buf, total), 0);
    ck_assert_int_lt(fdt_node_offset_by_compatible(&ctx, -1, "foo"), 0);
}
END_TEST

/* ------------------------------------------------------------------ */
/* Path lookup                                                         */
/* ------------------------------------------------------------------ */

/* Build /soc/serial@100 plus a decoy node also named "serial@100" at the
 * top level, so a name-anywhere search and a path search disagree. */
static uint32_t build_path_fdt(uint8_t *buf, uint32_t bufsz)
{
    uint8_t sblk[128];
    uint32_t i = 0;

    be32(sblk + i, FDT_BEGIN_NODE); i += 4;
    be32(sblk + i, 0);              i += 4;          /* root */

    be32(sblk + i, FDT_BEGIN_NODE); i += 4;
    memcpy(sblk + i, "serial@100\0\0", 12); i += 12; /* decoy at /serial@100 */
    be32(sblk + i, FDT_END_NODE);   i += 4;

    be32(sblk + i, FDT_BEGIN_NODE); i += 4;
    memcpy(sblk + i, "soc\0", 4);   i += 4;
    be32(sblk + i, FDT_BEGIN_NODE); i += 4;
    memcpy(sblk + i, "serial@100\0\0", 12); i += 12; /* /soc/serial@100 */
    be32(sblk + i, FDT_END_NODE);   i += 4;
    be32(sblk + i, FDT_END_NODE);   i += 4;          /* close soc */

    be32(sblk + i, FDT_END_NODE);   i += 4;          /* close root */
    be32(sblk + i, FDT_END);        i += 4;

    return build_fdt(buf, bufsz, sblk, i, "", 1);
}

START_TEST(test_fdt_path_offset_resolves_by_path)
{
    static uint8_t buf[0x200];
    uint32_t total;
    fdt_ctx ctx;
    int by_path, by_name, soc;

    total = build_path_fdt(buf, sizeof(buf));
    ck_assert_int_eq(fdt_open(&ctx, buf, total), 0);

    ck_assert_int_eq(fdt_path_offset(&ctx, "/"), 0);

    by_path = fdt_path_offset(&ctx, "/soc/serial@100");
    ck_assert_int_gt(by_path, 0);

    /* The tree-wide name search finds the decoy first; the path lookup
     * does not. That difference is the point of having both. */
    by_name = fdt_find_node_offset(&ctx, -1, "serial@100");
    ck_assert_int_gt(by_name, 0);
    ck_assert_int_ne(by_path, by_name);

    /* a unit address may be omitted from a path component */
    ck_assert_int_eq(fdt_path_offset(&ctx, "/soc/serial"), by_path);

    /* the public direct-child lookup agrees with the path lookup */
    soc = fdt_path_offset(&ctx, "/soc");
    ck_assert_int_gt(soc, 0);
    ck_assert_int_eq(fdt_subnode_offset(&ctx, soc, "serial"), by_path);
    ck_assert_int_eq(fdt_subnode_offset(&ctx, soc, "serial@100"), by_path);
    ck_assert_int_eq(fdt_subnode_offset(&ctx, 0, "missing"),
        -FDT_ERR_NOTFOUND);
    ck_assert_int_eq(fdt_subnode_offset(&ctx, 0, NULL), -FDT_ERR_BADARG);
    /* asked at the root it finds the decoy, not the /soc one - a direct
     * child lookup never descends */
    ck_assert_int_eq(fdt_subnode_offset(&ctx, 0, "serial"), by_name);

    ck_assert_int_lt(fdt_path_offset(&ctx, "/soc/nope"), 0);
    ck_assert_int_lt(fdt_path_offset(&ctx, "soc"), 0); /* must be absolute */

    /* A component far longer than any name in the tree must not match,
     * and must not compare past the end of the name it is tested
     * against. The lookup names come from attacker-influenced places
     * (a FIT's `default` string, for one), so the length is not bounded
     * by anything in the blob. */
    ck_assert_int_lt(fdt_path_offset(&ctx,
        "/soc/serial@100aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), 0);
    ck_assert_int_lt(fdt_find_node_offset(&ctx,
        -1, "serial@100aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), 0);
}
END_TEST

/* ------------------------------------------------------------------ */
/* Writing is bounded by the capacity, not by the header               */
/* ------------------------------------------------------------------ */

START_TEST(test_fdt_setprop_bounded_by_capacity)
{
    static uint8_t buf[0x400];
    uint8_t big[0x400];
    uint32_t total;
    fdt_ctx ctx;
    int len = 0;
    const void *val;

    total = build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);

    /* Capacity exactly the blob: there is no room to grow, so adding a
     * property must fail closed rather than write past the end. */
    ck_assert_int_eq(fdt_open(&ctx, buf, total), 0);
    ck_assert_int_eq(fdt_setprop(&ctx, 0, "status", "okay", 5),
        -FDT_ERR_NOSPACE);

    /* Same blob, honest larger window: the write now fits. */
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    ck_assert_int_eq(fdt_setprop(&ctx, 0, "status", "okay", 5), 0);

    val = fdt_getprop(&ctx, 0, "status", &len);
    ck_assert_ptr_nonnull(val);
    ck_assert_int_eq(len, 5);
    ck_assert_str_eq((const char *)val, "okay");

    /* The original property is still readable and unchanged. */
    val = fdt_getprop(&ctx, 0, "compatible", &len);
    ck_assert_ptr_nonnull(val);
    ck_assert_int_eq(len, 4);
    ck_assert_str_eq((const char *)val, "abc");

    /* A value that cannot fit in the window fails closed too. */
    memset(big, 'x', sizeof(big));
    ck_assert_int_lt(fdt_setprop(&ctx, 0, "blob", big, (int)sizeof(big)), 0);

    /* ...and the tree is still intact after the refusal. */
    val = fdt_getprop(&ctx, 0, "status", &len);
    ck_assert_ptr_nonnull(val);
    ck_assert_int_eq(len, 5);
}
END_TEST

/* fdt_grow() must refuse headroom the window cannot hold, where the old
 * blind totalsize bump would have silently promised it. */
START_TEST(test_fdt_grow_bounded_by_capacity)
{
    static uint8_t buf[0x400];
    uint32_t total;
    fdt_ctx ctx;

    total = build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);

    ck_assert_int_eq(fdt_open(&ctx, buf, total), 0);
    ck_assert_int_eq(fdt_grow(&ctx, 1024), -FDT_ERR_NOSPACE);
    ck_assert_uint_eq(fdt_size(&ctx), total);

    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    ck_assert_int_eq(fdt_grow(&ctx, 64), 0);
    ck_assert_uint_eq(fdt_size(&ctx), total + 64);
    ck_assert_int_eq(fdt_shrink(&ctx), 0);
    ck_assert_uint_eq(fdt_size(&ctx), total);
}
END_TEST


/* ------------------------------------------------------------------ */
/* Node insertion and removal                                          */
/* ------------------------------------------------------------------ */

/* Root with two children, "keep" (carrying a property) and "drop". */
static uint32_t build_two_child_fdt(uint8_t *buf, uint32_t bufsz)
{
    uint8_t sblk[128];
    uint32_t i = 0;

    be32(sblk + i, FDT_BEGIN_NODE); i += 4;
    be32(sblk + i, 0);              i += 4;          /* root */

    be32(sblk + i, FDT_BEGIN_NODE); i += 4;
    memcpy(sblk + i, "keep\0\0\0\0", 8); i += 8;
    be32(sblk + i, FDT_PROP);       i += 4;
    be32(sblk + i, 4);              i += 4;          /* len */
    be32(sblk + i, 0);              i += 4;          /* nameoff "compatible" */
    memcpy(sblk + i, "abc\0", 4);   i += 4;
    be32(sblk + i, FDT_END_NODE);   i += 4;

    be32(sblk + i, FDT_BEGIN_NODE); i += 4;
    memcpy(sblk + i, "drop\0\0\0\0", 8); i += 8;
    be32(sblk + i, FDT_END_NODE);   i += 4;

    be32(sblk + i, FDT_END_NODE);   i += 4;          /* close root */
    be32(sblk + i, FDT_END);        i += 4;

    return build_fdt(buf, bufsz, sblk, i, "compatible",
        (uint32_t)sizeof("compatible"));
}

START_TEST(test_fdt_del_node_removes_subtree)
{
    static uint8_t buf[0x400];
    uint32_t total;
    fdt_ctx ctx;
    int drop, keep, len = 0;
    const void *val;

    total = build_two_child_fdt(buf, sizeof(buf));
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);

    drop = fdt_path_offset(&ctx, "/drop");
    ck_assert_int_gt(drop, 0);
    ck_assert_int_eq(fdt_del_node(&ctx, drop), 0);

    /* the blob is still well formed, and only that node is gone */
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    ck_assert_int_lt(fdt_path_offset(&ctx, "/drop"), 0);
    ck_assert_int_lt(fdt_find_node_offset(&ctx, -1, "drop"), 0);

    keep = fdt_path_offset(&ctx, "/keep");
    ck_assert_int_gt(keep, 0);
    val = fdt_getprop(&ctx, keep, "compatible", &len);
    ck_assert_ptr_nonnull(val);
    ck_assert_int_eq(len, 4);
    ck_assert_str_eq((const char *)val, "abc");

    /* the content shrank; totalsize only follows once asked to, since
     * mutations never pull it below what a consumer was promised */
    ck_assert_uint_eq(fdt_size(&ctx), total);
    ck_assert_int_eq(fdt_shrink(&ctx), 0);
    ck_assert_uint_lt(fdt_size(&ctx), total);
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    ck_assert_int_gt(fdt_path_offset(&ctx, "/keep"), 0);
}
END_TEST

START_TEST(test_fdt_del_node_rejects_bad_offset)
{
    static uint8_t buf[0x400];
    fdt_ctx ctx;
    int keep;

    (void)build_two_child_fdt(buf, sizeof(buf));
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);

    keep = fdt_path_offset(&ctx, "/keep");
    ck_assert_int_gt(keep, 0);

    /* not a node token, and a wildly out-of-range offset */
    ck_assert_int_lt(fdt_del_node(&ctx, keep + 4), 0);
    ck_assert_int_lt(fdt_del_node(&ctx, 0x7000), 0);
    /* nothing was disturbed */
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    ck_assert_int_gt(fdt_path_offset(&ctx, "/keep"), 0);
}
END_TEST

/* fdt_add_subnode() is on the common boot path: every HAL that inserts
 * /chosen into a DTB that lacks one goes through it. */
START_TEST(test_fdt_add_subnode)
{
    static uint8_t buf[0x400];
    fdt_ctx ctx;
    int off, again, len = 0;
    const void *val;

    (void)build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    ck_assert_int_lt(fdt_path_offset(&ctx, "/chosen"), 0);

    off = fdt_add_subnode(&ctx, 0, "chosen");
    ck_assert_int_gt(off, 0);

    /* the new node is reachable both ways, and takes properties */
    ck_assert_int_eq(fdt_path_offset(&ctx, "/chosen"), off);
    ck_assert_int_eq(fdt_subnode_offset(&ctx, 0, "chosen"), off);
    ck_assert_int_eq(fdt_setprop(&ctx, off, "bootargs", "console=ttyS0", 14),
        0);

    /* adding it twice is refused, and the tree is unchanged */
    again = fdt_add_subnode(&ctx, 0, "chosen");
    ck_assert_int_eq(again, -FDT_ERR_EXISTS);

    /* still parses, and the root's original property survived */
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    off = fdt_path_offset(&ctx, "/chosen");
    ck_assert_int_gt(off, 0);
    val = fdt_getprop(&ctx, off, "bootargs", &len);
    ck_assert_ptr_nonnull(val);
    ck_assert_str_eq((const char *)val, "console=ttyS0");
    val = fdt_getprop(&ctx, 0, "compatible", &len);
    ck_assert_ptr_nonnull(val);
    ck_assert_str_eq((const char *)val, "abc");
}
END_TEST

START_TEST(test_fdt_add_subnode_bounded_by_capacity)
{
    static uint8_t buf[0x400];
    uint32_t total;
    fdt_ctx ctx;

    total = build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);

    /* no room to grow: must fail closed and leave the tree intact */
    ck_assert_int_eq(fdt_open(&ctx, buf, total), 0);
    ck_assert_int_eq(fdt_add_subnode(&ctx, 0, "chosen"), -FDT_ERR_NOSPACE);
    ck_assert_int_eq(fdt_open(&ctx, buf, total), 0);
    ck_assert_int_lt(fdt_path_offset(&ctx, "/chosen"), 0);
    ck_assert_uint_eq(fdt_size(&ctx), total);
}
END_TEST

/* ------------------------------------------------------------------ */
/* Property resize                                                     */
/* ------------------------------------------------------------------ */

/* Overwriting an existing property is the branch that runs on every real
 * boot (a DTB that already ships bootargs, T10xx rewriting reg/status).
 * Exercise both directions, since grow and shrink use the same splice
 * arithmetic with opposite signs. */
START_TEST(test_fdt_setprop_resizes_existing_property)
{
    static uint8_t buf[0x400];
    fdt_ctx ctx;
    int len = 0;
    const void *val;

    (void)build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    ck_assert_int_eq(fdt_setprop(&ctx, 0, "status", "okay", 5), 0);

    /* grow `compatible` past its original length */
    ck_assert_int_eq(fdt_setprop(&ctx, 0, "compatible",
        "a-much-longer-compatible-string", 32), 0);
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    val = fdt_getprop(&ctx, 0, "compatible", &len);
    ck_assert_ptr_nonnull(val);
    ck_assert_int_eq(len, 32);
    ck_assert_str_eq((const char *)val, "a-much-longer-compatible-string");
    /* the sibling property moved intact */
    val = fdt_getprop(&ctx, 0, "status", &len);
    ck_assert_ptr_nonnull(val);
    ck_assert_int_eq(len, 5);
    ck_assert_str_eq((const char *)val, "okay");

    /* now shrink it back */
    ck_assert_int_eq(fdt_setprop(&ctx, 0, "compatible", "xy", 3), 0);
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    val = fdt_getprop(&ctx, 0, "compatible", &len);
    ck_assert_ptr_nonnull(val);
    ck_assert_int_eq(len, 3);
    ck_assert_str_eq((const char *)val, "xy");
    val = fdt_getprop(&ctx, 0, "status", &len);
    ck_assert_ptr_nonnull(val);
    ck_assert_int_eq(len, 5);
    ck_assert_str_eq((const char *)val, "okay");

    /* a zero-length property is legal */
    ck_assert_int_eq(fdt_setprop(&ctx, 0, "compatible", NULL, 0), 0);
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    val = fdt_getprop(&ctx, 0, "compatible", &len);
    ck_assert_ptr_nonnull(val);
    ck_assert_int_eq(len, 0);
}
END_TEST

/* ------------------------------------------------------------------ */
/* initrd fixup                                                        */
/* ------------------------------------------------------------------ */

static uint64_t rd_be64(const uint8_t *p)
{
    uint64_t v = 0;
    int i;

    for (i = 0; i < 8; i++) {
        v = (v << 8) | (uint64_t)p[i];
    }
    return v;
}

START_TEST(test_fdt_fixup_initrd)
{
    static uint8_t buf[0x400];
    fdt_ctx ctx;
    int off, len = 0;
    const void *val;

    /* /chosen absent: it must be created */
    (void)build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    ck_assert_int_eq(fdt_fixup_initrd(&ctx, 0x10000000ULL, 0x2000ULL), 0);

    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    off = fdt_path_offset(&ctx, "/chosen");
    ck_assert_int_gt(off, 0);

    val = fdt_getprop(&ctx, off, "linux,initrd-start", &len);
    ck_assert_ptr_nonnull(val);
    ck_assert_int_eq(len, 8);
    ck_assert_uint_eq(rd_be64((const uint8_t *)val), 0x10000000ULL);

    val = fdt_getprop(&ctx, off, "linux,initrd-end", &len);
    ck_assert_ptr_nonnull(val);
    ck_assert_int_eq(len, 8);
    ck_assert_uint_eq(rd_be64((const uint8_t *)val), 0x10002000ULL);

    /* /chosen already present: the values are replaced in place */
    ck_assert_int_eq(fdt_fixup_initrd(&ctx, 0x20000000ULL, 0x100ULL), 0);
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    off = fdt_path_offset(&ctx, "/chosen");
    ck_assert_int_gt(off, 0);
    val = fdt_getprop(&ctx, off, "linux,initrd-end", &len);
    ck_assert_ptr_nonnull(val);
    ck_assert_uint_eq(rd_be64((const uint8_t *)val), 0x20000100ULL);
}
END_TEST

/* A start+size that wraps must be rejected: linux,initrd-end would
 * otherwise precede linux,initrd-start. */
START_TEST(test_fdt_fixup_initrd_rejects_wrapped_end)
{
    static uint8_t buf[0x800];
    fdt_ctx ctx;

    (void)build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);
    ck_assert_int_eq(fdt_open(&ctx, buf, (uint32_t)sizeof(buf)), 0);
    ck_assert_int_lt(fdt_fixup_initrd(&ctx, ~0ULL - 1U, 0x1000ULL), 0);
}
END_TEST

/* ------------------------------------------------------------------ */
/* fdt_peek_size                                                       */
/* ------------------------------------------------------------------ */

START_TEST(test_fdt_peek_size_header_only)
{
    static uint8_t buf[0x200];
    uint32_t total, peeked = 0;

    total = build_compat_fdt(buf, sizeof(buf), (const uint8_t *)"abc\0", 4);

    /* Reports the declared size from the header alone - the caller has
     * not fetched the body yet. */
    ck_assert_int_eq(fdt_peek_size(buf, FDT_HEADER_SIZE, &peeked), 0);
    ck_assert_uint_eq(peeked, total);

    /* Fewer bytes than a header is not enough to answer. */
    ck_assert_int_lt(fdt_peek_size(buf, FDT_HEADER_SIZE - 1, &peeked), 0);

    /* Bad magic and out-of-range sizes are still rejected. */
    hdr_set(buf, FDT_H_TOTALSIZE, WOLFBOOT_DTS_MAX_SIZE + 1);
    ck_assert_int_lt(fdt_peek_size(buf, FDT_HEADER_SIZE, &peeked), 0);
    hdr_set(buf, FDT_H_TOTALSIZE, total);
    hdr_set(buf, FDT_H_MAGIC, 0xDEADBEEF);
    ck_assert_int_lt(fdt_peek_size(buf, FDT_HEADER_SIZE, &peeked), 0);
}
END_TEST

/* ------------------------------------------------------------------ */
/* FIT                                                                 */
/* ------------------------------------------------------------------ */

/* FIT whose configuration `kernel` property is not NUL-terminated
 * within its declared length: fit_find_images() must still honor the
 * valid `default` but reject the malformed image name instead of
 * passing it on as a C string. */
static const uint8_t fit_cfg_unterminated_kernel[] = {
    /* header */
    0xd0, 0x0d, 0xfe, 0xed, /* magic */
    0x00, 0x00, 0x00, 0xa7, /* totalsize = 167 */
    0x00, 0x00, 0x00, 0x38, /* off_dt_struct = 56 */
    0x00, 0x00, 0x00, 0x98, /* off_dt_strings = 152 */
    0x00, 0x00, 0x00, 0x28, /* off_mem_rsvmap = 40 */
    0x00, 0x00, 0x00, 0x11, /* version = 17 */
    0x00, 0x00, 0x00, 0x10, /* last_comp_version = 16 */
    0x00, 0x00, 0x00, 0x00, /* boot_cpuid_phys */
    0x00, 0x00, 0x00, 0x0f, /* size_dt_strings = 15 */
    0x00, 0x00, 0x00, 0x60, /* size_dt_struct = 96 */
    /* mem_rsvmap terminator (offset 40) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* struct block (offset 56) */
    0x00, 0x00, 0x00, 0x01,                         /* BEGIN_NODE root */
    0x00, 0x00, 0x00, 0x00,                         /* "" */
    0x00, 0x00, 0x00, 0x01,                         /* BEGIN_NODE */
    0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x75, 0x72,
    0x61, 0x74, 0x69, 0x6f, 0x6e, 0x73, 0x00, 0x00, /* "configurations\0" */
    0x00, 0x00, 0x00, 0x03,                         /* FDT_PROP */
    0x00, 0x00, 0x00, 0x07,                         /* len = 7 */
    0x00, 0x00, 0x00, 0x00,                         /* nameoff = 0 ("default") */
    0x63, 0x6f, 0x6e, 0x66, 0x2d, 0x31, 0x00, 0x00, /* "conf-1\0" */
    0x00, 0x00, 0x00, 0x01,                         /* BEGIN_NODE */
    0x63, 0x6f, 0x6e, 0x66, 0x2d, 0x31, 0x00, 0x00, /* "conf-1\0" */
    0x00, 0x00, 0x00, 0x03,                         /* FDT_PROP */
    0x00, 0x00, 0x00, 0x08,                         /* len = 8 */
    0x00, 0x00, 0x00, 0x08,                         /* nameoff = 8 ("kernel") */
    0x6b, 0x65, 0x72, 0x6e, 0x65, 0x6c, 0x2d, 0x31, /* "kernel-1" -- no NUL */
    0x00, 0x00, 0x00, 0x02,                         /* END_NODE conf-1 */
    0x00, 0x00, 0x00, 0x02,                         /* END_NODE configurations */
    0x00, 0x00, 0x00, 0x02,                         /* END_NODE root */
    0x00, 0x00, 0x00, 0x09,                         /* FDT_END */
    /* strings block (offset 152) */
    0x64, 0x65, 0x66, 0x61, 0x75, 0x6c, 0x74, 0x00, /* "default\0" */
    0x6b, 0x65, 0x72, 0x6e, 0x65, 0x6c, 0x00,       /* "kernel\0" */
};

START_TEST(test_fit_find_images_rejects_unterminated_image_name)
{
    static uint8_t fit_scratch[sizeof(fit_cfg_unterminated_kernel)]
        __attribute__((aligned(4)));
    const char *conf = NULL, *kern = NULL, *dt = NULL;
    const char *rd = NULL, *fpga = NULL;
    fdt_ctx ctx;

    memcpy(fit_scratch, fit_cfg_unterminated_kernel, sizeof(fit_scratch));
    ck_assert_int_eq(fdt_open(&ctx, fit_scratch,
        (uint32_t)sizeof(fit_scratch)), 0);

    conf = fit_find_images(&ctx, &kern, &dt, &rd, &fpga);

    /* The valid `default` is still honored... */
    ck_assert_ptr_nonnull(conf);
    ck_assert_str_eq(conf, "conf-1");
    /* ...but the config's `kernel` property is not NUL-terminated
     * within its declared length, so it must be rejected rather than
     * passed on to a lookup that would strlen() it. */
    ck_assert_ptr_null(kern);
    ck_assert_ptr_null(dt);
    ck_assert_ptr_null(rd);
    ck_assert_ptr_null(fpga);
}
END_TEST

static Suite *fdt_suite(void)
{
    Suite *s = suite_create("fdt");
    TCase *tc = tcase_create("fdt");
    /* Separate case with a hard timeout so a non-terminating walk is
     * reported as a failure rather than hanging the suite. */
    TCase *tc_dos = tcase_create("fdt-dos");

    tcase_add_test(tc, test_fdt_open_rejects_totalsize_beyond_capacity);
    tcase_add_test(tc, test_fdt_open_rejects_unaligned_blob);
    tcase_add_test(tc, test_fdt_open_rejects_unbounded_string_area);
    tcase_add_test(tc, test_fdt_open_rejects_overlapping_areas);
    tcase_add_test(tc, test_fdt_open_rejects_dt_strings_area_overflow);
    tcase_add_test(tc, test_fdt_open_rejects_oversized_prop_len);
    tcase_add_test(tc, test_fdt_open_accepts_empty_strings_block);
    tcase_add_test(tc, test_fdt_open_rejects_two_roots);
    tcase_add_test(tc, test_fdt_get_string_rejects_out_of_range_offset);
    tcase_add_test(tc, test_fdt_get_string_returns_string_with_valid_offset);
    tcase_add_test(tc, test_fdt_compatible_unterminated_exact_len_no_match);
    tcase_add_test(tc, test_fdt_compatible_terminated_exact_len_match);
    tcase_add_test(tc, test_fdt_compatible_multi_string_list_match);
    tcase_add_test(tc, test_fdt_compatible_prefix_entry_no_match);
    tcase_add_test(tc, test_fdt_path_offset_resolves_by_path);
    tcase_add_test(tc, test_fdt_setprop_bounded_by_capacity);
    tcase_add_test(tc, test_fdt_grow_bounded_by_capacity);
    tcase_add_test(tc, test_fdt_del_node_removes_subtree);
    tcase_add_test(tc, test_fdt_del_node_rejects_bad_offset);
    tcase_add_test(tc, test_fdt_add_subnode);
    tcase_add_test(tc, test_fdt_add_subnode_bounded_by_capacity);
    tcase_add_test(tc, test_fdt_setprop_resizes_existing_property);
    tcase_add_test(tc, test_fdt_fixup_initrd);
    tcase_add_test(tc, test_fdt_fixup_initrd_rejects_wrapped_end);
    tcase_add_test(tc, test_fdt_peek_size_header_only);
    tcase_add_test(tc, test_fit_find_images_rejects_unterminated_image_name);
    suite_add_tcase(s, tc);

    tcase_set_timeout(tc_dos, 5);
    tcase_add_test(tc_dos,
        test_fdt_node_offset_by_compatible_terminates_on_unterminated_prop);
    suite_add_tcase(s, tc_dos);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = fdt_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
