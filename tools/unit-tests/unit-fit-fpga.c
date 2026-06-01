/* unit-fit-fpga.c
 *
 * Unit tests for the FIT-image FPGA-subimage discovery added to
 * fit_find_images() and fit_get_compatible() in src/fdt.c. The tests
 * build minimal FIT blobs in memory (a tiny DTB writer below) and check
 * that the fpga node is found via the configuration "fpga" property, via
 * the type=="fpga" fallback, and that it is NULL when absent.
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

#include "../../include/fdt.h"

/* fdt.c calls wolfBoot_printf; supply a silent stub. */
void wolfBoot_printf(const char *fmt, ...)
{
    (void)fmt;
}

/* Pull in the production code under test (gated on WOLFBOOT_FDT). */
#include "../../src/fdt.c"

/* ------------------------------------------------------------------------- */
/* Minimal big-endian DTB writer (FDT_* tokens come from fdt.h)              */
/* ------------------------------------------------------------------------- */
static uint8_t  g_struct[4096];
static uint32_t g_struct_len;
static char     g_strings[1024];
static uint32_t g_strings_len;
static uint8_t  g_blob[8192];

static void be32_put(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static void struct_u32(uint32_t v)
{
    be32_put(&g_struct[g_struct_len], v);
    g_struct_len += 4;
}

/* Append name+nul, 4-byte aligned (used by node names). */
static void struct_str(const char* s)
{
    uint32_t n = (uint32_t)strlen(s) + 1U;
    memcpy(&g_struct[g_struct_len], s, n);
    g_struct_len += n;
    while (g_struct_len & 3U) {
        g_struct[g_struct_len++] = 0;
    }
}

/* Intern a property name into the strings block, return its offset. */
static uint32_t strings_off(const char* name)
{
    uint32_t n = (uint32_t)strlen(name) + 1U;
    uint32_t off = g_strings_len;
    memcpy(&g_strings[g_strings_len], name, n);
    g_strings_len += n;
    return off;
}

static void node_begin(const char* name)
{
    struct_u32(FDT_BEGIN_NODE);
    struct_str(name);
}

static void node_end(void)
{
    struct_u32(FDT_END_NODE);
}

static void prop_str(const char* name, const char* val)
{
    uint32_t vlen = (uint32_t)strlen(val) + 1U;
    struct_u32(FDT_PROP);
    struct_u32(vlen);
    struct_u32(strings_off(name));
    memcpy(&g_struct[g_struct_len], val, vlen);
    g_struct_len += vlen;
    while (g_struct_len & 3U) {
        g_struct[g_struct_len++] = 0;
    }
}

/* Assemble the header + reserve map + struct + strings into g_blob. */
static void* fit_finish(void)
{
    uint32_t off_rsv = 40;            /* header is 40 bytes (v17) */
    uint32_t off_struct = off_rsv + 16; /* one terminating rsv entry */
    uint32_t off_strings = off_struct + g_struct_len;
    uint32_t total = off_strings + g_strings_len;
    uint8_t* h = g_blob;

    memset(g_blob, 0, sizeof(g_blob));
    be32_put(h + 0,  0xD00DFEEDU);   /* magic */
    be32_put(h + 4,  total);         /* totalsize */
    be32_put(h + 8,  off_struct);    /* off_dt_struct */
    be32_put(h + 12, off_strings);   /* off_dt_strings */
    be32_put(h + 16, off_rsv);       /* off_mem_rsvmap */
    be32_put(h + 20, 17);            /* version */
    be32_put(h + 24, 16);            /* last_comp_version */
    be32_put(h + 28, 0);             /* boot_cpuid_phys */
    be32_put(h + 32, g_strings_len); /* size_dt_strings */
    be32_put(h + 36, g_struct_len);  /* size_dt_struct */
    /* reserve map terminator already zeroed */
    memcpy(g_blob + off_struct, g_struct, g_struct_len);
    memcpy(g_blob + off_strings, g_strings, g_strings_len);
    return g_blob;
}

static void fit_reset(void)
{
    g_struct_len = 0;
    g_strings_len = 0;
    memset(g_struct, 0, sizeof(g_struct));
    memset(g_strings, 0, sizeof(g_strings));
}

/* ------------------------------------------------------------------------- */
/* Tests                                                                     */
/* ------------------------------------------------------------------------- */

/* Configuration node carries an explicit "fpga" reference. */
START_TEST(test_fit_fpga_via_config)
{
    const char *kernel = NULL, *flat_dt = NULL, *ramdisk = NULL, *fpga = NULL;
    const char* comp;
    void* fit;

    fit_reset();
    struct_u32(FDT_BEGIN_NODE); struct_str("");      /* root */
        node_begin("images");
            node_begin("fpga-1");
                prop_str("type", "fpga");
                prop_str("compatible", "u-boot,zynqmp-fpga-ddrauth");
            node_end();
        node_end();
        node_begin("configurations");
            prop_str("default", "conf1");
            node_begin("conf1");
                prop_str("kernel", "kernel-1");
                prop_str("fpga", "fpga-1");
            node_end();
        node_end();
    node_end();
    struct_u32(FDT_END);
    fit = fit_finish();

    fit_find_images(fit, &kernel, &flat_dt, &ramdisk, &fpga);
    ck_assert_ptr_nonnull(fpga);
    ck_assert_str_eq(fpga, "fpga-1");

    comp = fit_get_compatible(fit, fpga);
    ck_assert_ptr_nonnull(comp);
    ck_assert_str_eq(comp, "u-boot,zynqmp-fpga-ddrauth");
}
END_TEST

/* No "fpga" config property: fall back to a node with type=="fpga". */
START_TEST(test_fit_fpga_via_type_fallback)
{
    const char *fpga = NULL;
    void* fit;

    fit_reset();
    struct_u32(FDT_BEGIN_NODE); struct_str("");
        node_begin("images");
            node_begin("the-bitstream");
                prop_str("type", "fpga");
            node_end();
        node_end();
    node_end();
    struct_u32(FDT_END);
    fit = fit_finish();

    fit_find_images(fit, NULL, NULL, NULL, &fpga);
    ck_assert_ptr_nonnull(fpga);
    ck_assert_str_eq(fpga, "the-bitstream");
}
END_TEST

/* No fpga subimage at all: pfpga must be left NULL. */
START_TEST(test_fit_fpga_absent)
{
    const char *fpga = (const char*)0x1; /* poison */
    void* fit;

    fit_reset();
    struct_u32(FDT_BEGIN_NODE); struct_str("");
        node_begin("images");
            node_begin("kernel-1");
                prop_str("type", "kernel");
            node_end();
        node_end();
    node_end();
    struct_u32(FDT_END);
    fit = fit_finish();

    fit_find_images(fit, NULL, NULL, NULL, &fpga);
    ck_assert_ptr_null(fpga);
}
END_TEST

/* fit_get_compatible returns NULL when the property is absent. */
START_TEST(test_fit_compatible_absent)
{
    const char* comp;
    void* fit;

    fit_reset();
    struct_u32(FDT_BEGIN_NODE); struct_str("");
        node_begin("images");
            node_begin("fpga-1");
                prop_str("type", "fpga");
            node_end();
        node_end();
    node_end();
    struct_u32(FDT_END);
    fit = fit_finish();

    comp = fit_get_compatible(fit, "fpga-1");
    ck_assert_ptr_null(comp);
}
END_TEST

static Suite* fit_fpga_suite(void)
{
    Suite* s = suite_create("fit-fpga");
    TCase* tc = tcase_create("discovery");
    tcase_add_test(tc, test_fit_fpga_via_config);
    tcase_add_test(tc, test_fit_fpga_via_type_fallback);
    tcase_add_test(tc, test_fit_fpga_absent);
    tcase_add_test(tc, test_fit_compatible_absent);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int failed;
    SRunner* sr = srunner_create(fit_fpga_suite());
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? 0 : 1;
}
