/* unit-t10xx-dts-memac.c
 *
 * Regression test for F-7052: the fsl,fman-memac loop in
 * hal_dts_fixup() (hal/nxp_t10xx.c) read the node's cell-index
 * straight from the device tree and used it to index the file-static
 * phydevs[5] with no bounds check - unlike the qman-portal loop right
 * above it. Standard NXP FMan device trees assign the 10G MACs
 * cell-index 8/9, so any such DTB read past the array and wrote the
 * result into the kernel device tree as a MAC address.
 *
 * The test compiles the real src/fdt.c (wolfBoot's libfdt subset) and
 * the real hal_dts_fixup() (extracted by the Makefile from
 * hal/nxp_t10xx.c, along with its liodn/qman static tables), feeds it
 * hand-built DTBs, and checks the resulting local-mac-address
 * properties.
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

/* Compile src/fdt.c (its product guard). */
#define MMU

#include "fdt.h"

/* ---- platform stubs used by the extracted hal_dts_fixup() ---- */

static int wolfBoot_printf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

static uint32_t hal_get_bus_clk(void)
{
    return 100000000U;
}
static uint32_t hal_get_plat_clk(void)
{
    return 200000000U;
}
static uint32_t hal_get_core_clk(void)
{
    return 500000000U;
}

/* The PCIe loop reads the DCFG device-disable register through get32;
 * return 0 so no node is removed. */
static uint32_t g_devdisr3;
#define DCFG_BASE 0UL
#define DCFG_DEVDISR3 ((volatile uint32_t*)(DCFG_BASE + 0x78UL))
static uint32_t get32(volatile uint32_t *addr)
{
    (void)addr;
    return g_devdisr3;
}

/* Linker-symbol the CPU loop would use (no cpu nodes in the test
 * DTBs, it only needs to link/compile). */
uint32_t _spin_table[8];
#ifndef ENTRY_SIZE
#define ENTRY_SIZE sizeof(uint32_t)
#endif

/* Registers referenced by the extracted liodn table (never read). */
#define DCFG_USB1LIODNR   ((volatile uint32_t*)0x01)
#define DCFG_USB2LIODNR   ((volatile uint32_t*)0x02)
#define DCFG_SDMMCLIODNR  ((volatile uint32_t*)0x03)
#define DCFG_SATALIODNR   ((volatile uint32_t*)0x04)
#define DCFG_TDMDMALIODNR ((volatile uint32_t*)0x05)
#define DCFG_QELIODNR     ((volatile uint32_t*)0x06)
#define DCFG_DMA1LIODNR   ((volatile uint32_t*)0x07)
#define DCFG_DMA2LIODNR   ((volatile uint32_t*)0x08)
#define QMAN_LIODNR       ((volatile uint32_t*)0x09)
#define BMAN_LIODNR       ((volatile uint32_t*)0x0A)
#define PCIE_LIODN(n)     ((volatile uint32_t*)(0x10 + (n)))

/* Constants the extracted function expects from the target config. */
#define QMAN_NUM_PORTALS      10
#define FMAN_DMA_LIODN        973
#define PCIE_MAX_CONTROLLERS  3
#define PCIE_BASE(i)          0x0UL
#define DDR_ADDRESS           0x0UL
#define DDR_SIZE              0x10000000UL
#define TIMEBASE_HZ           100000000U
#define CPU_NUMCORES          4

/* The MAC table the fixup loop reads (test-owned copy; the real one
 * is file-static in hal/nxp_t10xx.c). Must precede the extract, which
 * references it. */
enum phy_interface {
    PHY_INTERFACE_MODE_RGMII = 0,
    PHY_INTERFACE_MODE_SGMII,
    PHY_INTERFACE_MODE_XGMII
};
struct phy_device {
    uint8_t phyaddr;
    enum phy_interface interface;
    uint8_t mac_addr[6];
};
static struct phy_device phydevs[5];

/* The code under test: src/fdt.c (resolved via -I../../src) and the
 * extracted hal_dts_fixup() (plus its liodn/qman tables) from
 * hal/nxp_t10xx.c. */
#include "fdt.c"
#include "nxp_t10xx_fixup_extract.h"

/* ---- minimal raw DTB builder (wolfBoot's FDT dialect, see
 * include/fdt.h and src/fdt.c: node/property offsets are relative to
 * the start of the struct block; a property is the 12-byte
 * struct fdt_property {tag, len, nameoff, data[]} with the value
 * inline in the struct block; the string block is last) ---- */

#define DTB_BUF_SIZE (16 * 1024)

struct dtb {
    uint8_t buf[DTB_BUF_SIZE];
    uint32_t struct_off;   /* absolute */
    uint32_t struct_end;   /* relative to the struct block */
    uint32_t strings_off;  /* absolute */
    uint32_t strings_end;  /* relative to the string block */
    /* string table entries (offset, length) so lookups only match at
     * string boundaries */
    uint32_t str_off[64];
    uint32_t str_len[64];
    uint32_t str_count;
};

static uint32_t align4(uint32_t v)
{
    return (v + 3u) & ~3u;
}

static void dtb_init(struct dtb *d)
{
    memset(d, 0, sizeof(*d));
    d->struct_off  = 0x30;
    d->struct_end  = 0;
    d->strings_off = 0x400;
    d->strings_end = 1; /* offset 0 is the empty name */
}

static uint32_t dtb_add_string(struct dtb *d, const char *s)
{
    uint32_t len, off, i;

    len = (uint32_t)strlen(s);
    for (i = 0; i < d->str_count; i++) {
        if (d->str_len[i] == len &&
            memcmp(d->buf + d->strings_off + d->str_off[i], s, len) == 0)
            return d->str_off[i];
    }

    off = d->strings_end;
    d->strings_end += len + 1;
    if (d->strings_end > 0x1000)
        ck_abort_msg("dtb string block overflow");
    memcpy(d->buf + d->strings_off + d->strings_end - len - 1, s, len);
    d->buf[d->strings_off + d->strings_end - 1] = 0;
    if (d->str_count >= 64)
        ck_abort_msg("dtb string table overflow");
    d->str_off[d->str_count] = off;
    d->str_len[d->str_count] = len;
    d->str_count++;
    return off;
}

static void dtb_put_u32(struct dtb *d, uint32_t abs_off, uint32_t val)
{
    val = cpu_to_fdt32(val); /* the FDT wire format is big-endian */
    d->buf[abs_off] = (uint8_t)val;
    d->buf[abs_off + 1] = (uint8_t)(val >> 8);
    d->buf[abs_off + 2] = (uint8_t)(val >> 16);
    d->buf[abs_off + 3] = (uint8_t)(val >> 24);
}

static void dtb_put_u64(struct dtb *d, uint32_t abs_off, uint64_t val)
{
    val = cpu_to_fdt64(val);
    dtb_put_u32(d, abs_off, (uint32_t)(val >> 32));
    dtb_put_u32(d, abs_off + 4, (uint32_t)val);
}

static void dtb_begin_node(struct dtb *d, const char *name)
{
    uint32_t p = d->struct_off + d->struct_end;

    /* FDT_BEGIN_NODE word followed by the NUL-terminated node name;
     * the next tag must start 4-byte aligned, so pad after the name. */
    dtb_put_u32(d, p, (uint32_t)FDT_BEGIN_NODE);
    p += 4;
    do {
        d->buf[p++] = (uint8_t)*name;
    } while (*name++ != '\0');
    p = align4(p);
    d->struct_end = p - d->struct_off;
}

static void dtb_end_node(struct dtb *d)
{
    dtb_put_u32(d, d->struct_off + d->struct_end, (uint32_t)FDT_END_NODE);
    d->struct_end += 4;
}

static void dtb_prop_raw(struct dtb *d, const char *name,
    const void *val, uint32_t len)
{
    uint32_t p = d->struct_off + d->struct_end;
    uint32_t nameoff = dtb_add_string(d, name);

    /* struct fdt_property: {tag, len, nameoff, data[]} */
    dtb_put_u32(d, p, (uint32_t)FDT_PROP);
    dtb_put_u32(d, p + 4, len);
    dtb_put_u32(d, p + 8, nameoff);
    p += 12;
    if (len > 0) {
        memcpy(d->buf + p, val, len);
        p += align4(len);
    }
    d->struct_end = p - d->struct_off;
}

static void dtb_prop_u32(struct dtb *d, const char *name, uint32_t val)
{
    val = cpu_to_fdt32(val);
    dtb_prop_raw(d, name, &val, sizeof(val));
}

static void dtb_finalize(struct dtb *d)
{
    struct fdt_header *hdr = (struct fdt_header *)d->buf;

    /* FDT_END word terminates the struct block. */
    d->struct_end = align4(d->struct_end);
    dtb_put_u32(d, d->struct_off + d->struct_end, (uint32_t)FDT_END);
    d->struct_end += 4;

    /* The empty string at strings offset 0. */
    d->buf[d->strings_off] = 0;

    /* Header fields are big-endian on the wire. */
    hdr->magic = cpu_to_fdt32(0xd00dfeedU);
    /* totalsize includes headroom: hal_dts_fixup() adds 2048 and
     * fdt_setprop() grows the struct block in place. */
    hdr->totalsize = cpu_to_fdt32(0x2000);
    hdr->off_dt_struct = cpu_to_fdt32(d->struct_off);
    hdr->off_dt_strings = cpu_to_fdt32(d->strings_off);
    hdr->off_mem_rsvmap = cpu_to_fdt32(d->strings_off); /* no reservations */
    hdr->version = cpu_to_fdt32(17);
    hdr->last_comp_version = cpu_to_fdt32(16);
    hdr->boot_cpuid_phys = cpu_to_fdt32(0);
    hdr->size_dt_strings = cpu_to_fdt32(d->strings_end);
    hdr->size_dt_struct = cpu_to_fdt32(d->struct_end);
}

/* Build a DTB with one fsl,fman-memac node and the given cell-index. */
/* In this FDT dialect the compatible value is the NUL-terminated string
 * (or list) inline in the property, not a string-table offset. */
#define MEMAC_COMPAT "fsl,fman-memac"

static void dtb_build_memac(struct dtb *d, const char *node, uint32_t idx)
{
    uint32_t reg[4] = {cpu_to_fdt32(0), cpu_to_fdt32(0), cpu_to_fdt32(0),
        cpu_to_fdt32(0x10000000U)};

    dtb_init(d);
    /* root node */
    dtb_begin_node(d, "");
    /* memory node (the fixup rewrites its reg) */
    dtb_begin_node(d, "memory");
    dtb_prop_raw(d, "reg", reg, sizeof(reg));
    dtb_end_node(d);
    /* the memac node */
    dtb_begin_node(d, node);
    dtb_prop_raw(d, "compatible", MEMAC_COMPAT, strlen(MEMAC_COMPAT) + 1);
    dtb_prop_u32(d, "cell-index", idx);
    dtb_end_node(d);
    dtb_end_node(d); /* root */
    dtb_finalize(d);
}

/* Build a DTB with two fsl,fman-memac nodes. */
static void dtb_build_memac2(struct dtb *d, uint32_t idx0, uint32_t idx1)
{
    uint32_t reg[4] = {cpu_to_fdt32(0), cpu_to_fdt32(0), cpu_to_fdt32(0),
        cpu_to_fdt32(0x10000000U)};

    dtb_init(d);
    dtb_begin_node(d, "");
    dtb_begin_node(d, "memory");
    dtb_prop_raw(d, "reg", reg, sizeof(reg));
    dtb_end_node(d);
    dtb_begin_node(d, "memac0");
    dtb_prop_raw(d, "compatible", MEMAC_COMPAT, strlen(MEMAC_COMPAT) + 1);
    dtb_prop_u32(d, "cell-index", idx0);
    dtb_end_node(d);
    dtb_begin_node(d, "memac1");
    dtb_prop_raw(d, "compatible", MEMAC_COMPAT, strlen(MEMAC_COMPAT) + 1);
    dtb_prop_u32(d, "cell-index", idx1);
    dtb_end_node(d);
    dtb_end_node(d); /* root */
    dtb_finalize(d);
}

static int memac_off(void *fdt, const char *name)
{
    int off = -1;
    int n;
    const char *s;

    /* find the node by name: walk nodes via compatible then match name */
    off = fdt_node_offset_by_compatible(fdt, -1, "fsl,fman-memac");
    if (off == -FDT_ERR_NOTFOUND)
        return -1;
    if (strcmp(name, "memac0") == 0)
        return off;
    /* second node */
    n = fdt_node_offset_by_compatible(fdt, off, "fsl,fman-memac");
    return n;
}

static void setup(void)
{
    unsigned i;

    g_devdisr3 = 0;
    for (i = 0; i < 5; i++) {
        memset(&phydevs[i], 0, sizeof(phydevs[i]));
        /* distinguishable per-port MACs */
        phydevs[i].mac_addr[0] = 0x02;
        phydevs[i].mac_addr[5] = (uint8_t)(0x10 + i);
    }
}

static void teardown(void)
{
}

/* NXP's qoriq-fman3 dtsi gives the first 10G memac cell-index 8, and
 * phydevs holds that port at FM1_10GEC1 (slot 4). It must be mapped
 * there, not skipped: skipping silently drops the 10G MAC fixup. Pre
 * fix the loop read phydevs[8] out of bounds instead. */
START_TEST(test_memac_10g_cell_index_mapped)
{
    struct dtb d;
    int off, len;
    const void *mac;

    dtb_build_memac(&d, "memac0", 8);
    ck_assert_int_eq(hal_dts_fixup(d.buf), 0);

    off = fdt_node_offset_by_compatible(d.buf, -1, "fsl,fman-memac");
    ck_assert_int_gt(off, 0);
    mac = fdt_getprop(d.buf, off, "local-mac-address", &len);
    ck_assert_ptr_nonnull(mac);
    ck_assert_int_eq(len, 6);
    ck_assert_int_eq(((const uint8_t *)mac)[5], 0x14); /* phydevs[4] */
}
END_TEST

/* A cell-index with no phydevs slot at all (a second 10G port, or a
 * malformed DTB) must produce no local-mac-address. */
START_TEST(test_memac_unmapped_cell_index_skipped)
{
    struct dtb d;
    int off;
    const void *mac;

    dtb_build_memac(&d, "memac0", 9);
    ck_assert_int_eq(hal_dts_fixup(d.buf), 0);

    off = fdt_node_offset_by_compatible(d.buf, -1, "fsl,fman-memac");
    ck_assert_int_gt(off, 0);
    mac = fdt_getprop(d.buf, off, "local-mac-address", NULL);
    ck_assert_ptr_null(mac);
}
END_TEST

/* A valid cell-index still gets its MAC from the table. */
START_TEST(test_memac_valid_cell_index_fixed)
{
    struct dtb d;
    int off, len;
    const void *mac;

    dtb_build_memac(&d, "memac0", 1);
    ck_assert_int_eq(hal_dts_fixup(d.buf), 0);

    off = fdt_node_offset_by_compatible(d.buf, -1, "fsl,fman-memac");
    ck_assert_int_gt(off, 0);
    mac = fdt_getprop(d.buf, off, "local-mac-address", &len);
    ck_assert_ptr_nonnull(mac);
    ck_assert_int_eq(len, 6);
    ck_assert_int_eq(((const uint8_t *)mac)[5], 0x11); /* phydevs[1] */
}
END_TEST

/* An unmapped node must be skipped, not break the loop: the following
 * valid node still gets its MAC. */
START_TEST(test_memac_mixed_oob_then_valid)
{
    struct dtb d;
    int off0, off1, len;
    const void *mac;

    dtb_build_memac2(&d, 9, 2);
    ck_assert_int_eq(hal_dts_fixup(d.buf), 0);

    off0 = fdt_node_offset_by_compatible(d.buf, -1, "fsl,fman-memac");
    off1 = fdt_node_offset_by_compatible(d.buf, off0, "fsl,fman-memac");
    ck_assert_int_gt(off0, 0);
    ck_assert_int_gt(off1, 0);

    /* node 0 (index 9): no phydevs slot, skipped */
    mac = fdt_getprop(d.buf, off0, "local-mac-address", NULL);
    ck_assert_ptr_null(mac);

    /* node 1 (index 2): fixed from phydevs[2] */
    mac = fdt_getprop(d.buf, off1, "local-mac-address", &len);
    ck_assert_ptr_nonnull(mac);
    ck_assert_int_eq(len, 6);
    ck_assert_int_eq(((const uint8_t *)mac)[5], 0x12);
}
END_TEST

Suite *t10xx_dts_memac_suite(void)
{
    Suite *s = suite_create("t10xx-dts-memac");
    TCase *tc = tcase_create("memac");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_memac_10g_cell_index_mapped);
    tcase_add_test(tc, test_memac_unmapped_cell_index_skipped);
    tcase_add_test(tc, test_memac_valid_cell_index_fixed);
    tcase_add_test(tc, test_memac_mixed_oob_then_valid);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = t10xx_dts_memac_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
