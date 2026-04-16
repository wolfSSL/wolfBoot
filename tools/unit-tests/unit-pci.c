/* unit-pci.c
 *
 * Unit test for pci functions
 *
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>

#define MOCKED_BASE (2*1024*1024*1024ULL)
#define PCI_USE_ECAM
#define PCI_ECAM_BASE MOCKED_BASE

#include <pci.h>
#include <pci.c>

/*
 * Mock PCI topology infrastructure
 */

#define TEST_PCI_MAX_NODES 10
#define TEST_PCI_MAX_BARS  6
#define TEST_PCI_ROOT_BUS  (-1)
#define TEST_PCI_CFG_SIZE  256

#define TEST_PCI_BAR_MMIO   0x00
#define TEST_PCI_BAR_IO     0x01
#define TEST_PCI_BAR_64BIT  0x02
#define TEST_PCI_BAR_PF     0x04

#define PCI_CLASS_CODE_BYTE_OFFSET  0x0B
#define PCI_SUBCLASS_BYTE_OFFSET    0x0A

struct test_pci_bar_info {
    uint32_t size;        /* power-of-2 bytes, 0 = not implemented */
    uint8_t  is_io;       /* 1=IO, 0=MMIO */
    uint8_t  is_64bit;    /* 1=64-bit MMIO (consumes next BAR slot too) */
    uint8_t  is_prefetch; /* 1=prefetchable */
    uint8_t  io_hi16_zero;/* 1=IO BAR only decodes 16 bits (upper 16 of mask are 0) */
    uint32_t upper_mask;  /* 64-bit BARs: upper half probe mask (0 = use default 0xFFFFFFFF) */
};

struct test_pci_node {
    uint8_t  in_use;
    uint8_t  dev;        /* device slot on parent bus */
    uint8_t  func;       /* function number */
    uint8_t  is_bridge;
    int      parent_bus; /* TEST_PCI_ROOT_BUS for root, else index of parent bridge */
    uint16_t vendor_id;
    uint16_t device_id;
    struct test_pci_bar_info bars[TEST_PCI_MAX_BARS];
    uint8_t  bar_probed[TEST_PCI_MAX_BARS]; /* tracks which BARs had 0xFFFFFFFF written */
    uint8_t  cfg[TEST_PCI_CFG_SIZE]; /* PCI config register backing store */
};

struct test_pci_topology {
    struct test_pci_node nodes[TEST_PCI_MAX_NODES];
    int count;
};

static struct test_pci_topology *current_topology = NULL;

static void test_pci_init(struct test_pci_topology *t)
{
    memset(t, 0, sizeof(*t));
}

static int test_pci_add_node(struct test_pci_topology *t,
                             uint8_t dev, uint8_t func,
                             uint16_t vendor_id, uint16_t device_id,
                             int parent_bus, uint8_t is_bridge)
{
    int idx = t->count;
    ck_assert_msg(idx < TEST_PCI_MAX_NODES,
                  "topology full (%d nodes)", TEST_PCI_MAX_NODES);
    ck_assert_msg(parent_bus == TEST_PCI_ROOT_BUS ||
                  (parent_bus >= 0 && parent_bus < idx &&
                   t->nodes[parent_bus].is_bridge),
                  "invalid parent_bus %d", parent_bus);

    struct test_pci_node *n = &t->nodes[idx];
    n->in_use    = 1;
    n->dev       = dev;
    n->func      = func;
    n->vendor_id = vendor_id;
    n->device_id = device_id;
    n->is_bridge = is_bridge;
    n->parent_bus = parent_bus;
    t->count++;
    return idx;
}

static int test_pci_add_dev(struct test_pci_topology *t,
                            uint8_t dev, uint8_t func,
                            uint16_t vendor_id, uint16_t device_id,
                            int parent_bus)
{
    return test_pci_add_node(t, dev, func, vendor_id, device_id, parent_bus, 0);
}

static int test_pci_add_bridge(struct test_pci_topology *t,
                               uint8_t dev, uint8_t func,
                               uint16_t vendor_id, uint16_t device_id,
                               int parent_bus)
{
    return test_pci_add_node(t, dev, func, vendor_id, device_id, parent_bus, 1);
}

static void test_pci_dev_set_bar(struct test_pci_topology *t, int node_idx,
                                 int bar_idx, uint32_t size,
                                 unsigned int type)
{
    ck_assert(node_idx >= 0 && node_idx < t->count);
    ck_assert(bar_idx >= 0 && bar_idx < TEST_PCI_MAX_BARS);
    ck_assert_msg(size == 0 || (size & (size - 1)) == 0,
                  "BAR size must be power of 2");
    ck_assert_msg(!(type & TEST_PCI_BAR_IO) ||
                  !(type & (TEST_PCI_BAR_64BIT | TEST_PCI_BAR_PF)),
                  "IO BAR cannot be combined with 64BIT or PF flags");

    struct test_pci_bar_info *b = &t->nodes[node_idx].bars[bar_idx];
    b->size        = size;
    b->is_io       = (type & TEST_PCI_BAR_IO) != 0;
    b->is_64bit    = (type & TEST_PCI_BAR_64BIT) != 0;
    b->is_prefetch = (type & TEST_PCI_BAR_PF) != 0;
}

static void test_pci_commit(struct test_pci_topology *t)
{
    int i;
    for (i = 0; i < t->count; i++) {
        struct test_pci_node *n = &t->nodes[i];
        if (!n->in_use)
            continue;
        memset(n->cfg, 0, TEST_PCI_CFG_SIZE);
        memset(n->bar_probed, 0, TEST_PCI_MAX_BARS);
        memcpy(&n->cfg[PCI_VENDOR_ID_OFFSET], &n->vendor_id, 2);
        memcpy(&n->cfg[PCI_DEVICE_ID_OFFSET], &n->device_id, 2);
        n->cfg[PCI_HEADER_TYPE_OFFSET] = n->is_bridge ?
            PCI_HEADER_TYPE_BRIDGE : PCI_HEADER_TYPE_DEVICE;
        if (n->is_bridge) {
            n->cfg[PCI_CLASS_CODE_BYTE_OFFSET] = 0x06;
            n->cfg[PCI_SUBCLASS_BYTE_OFFSET] = 0x04;
        }
    }
    current_topology = t;
}

static void test_pci_cleanup(struct test_pci_topology *t)
{
    (void)t;
    current_topology = NULL;
}

static uint8_t test_pci_node_bus(struct test_pci_topology *t, int node_idx)
{
    struct test_pci_node *n = &t->nodes[node_idx];
    if (n->parent_bus == TEST_PCI_ROOT_BUS)
        return 0;
    /* Bus number is the parent bridge's secondary bus */
    return t->nodes[n->parent_bus].cfg[PCI_SECONDARY_BUS];
}

static int test_pci_is_bus_reachable(struct test_pci_topology *t,
                                     uint8_t target_bus)
{
    uint8_t current_bus = 0;
    int i;

    if (target_bus == 0)
        return 1;

    while (current_bus != target_bus) {
        int advance = 0;
        for (i = 0; i < t->count; i++) {
            struct test_pci_node *n = &t->nodes[i];
            uint8_t sec, sub;

            if (!n->in_use || !n->is_bridge)
                continue;
            if (test_pci_node_bus(t, i) != current_bus)
                continue;

            sec = n->cfg[PCI_SECONDARY_BUS];
            sub = n->cfg[PCI_SUB_SEC_BUS];

            if (sec == 0)
                continue; /* bridge not yet configured */
            if (sec <= target_bus && target_bus <= sub) {
                ck_assert_msg(sec > current_bus,
                              "bridge sec bus must be > current bus");
                current_bus = sec;
                advance = 1;
                break;
            }
        }
        if (!advance)
            return 0;
    }
    return 1;
}

static struct test_pci_node *test_pci_find_node(struct test_pci_topology *t,
                                                uint8_t bus, uint8_t dev,
                                                uint8_t func)
{
    int i;

    if (!test_pci_is_bus_reachable(t, bus))
        return NULL;

    for (i = 0; i < t->count; i++) {
        struct test_pci_node *n = &t->nodes[i];
        if (!n->in_use)
            continue;
        if (n->dev != dev || n->func != func)
            continue;
        if (bus == 0 && n->parent_bus == TEST_PCI_ROOT_BUS)
            return n;
        if (bus > 0 && n->parent_bus >= 0 &&
            test_pci_node_bus(t, i) == bus)
            return n;
    }
    return NULL;
}

static void ecam_decode(uintptr_t addr, uint8_t *bus, uint8_t *dev,
                        uint8_t *func, uint16_t *off)
{
    uintptr_t rel = addr - PCI_ECAM_BASE;
    *bus  = (rel >> 20) & 0xFF;
    *dev  = (rel >> 15) & 0x1F;
    *func = (rel >> 12) & 0x7;
    *off  = rel & 0xFFF;
}

static uint32_t test_pci_bar_probe_mask(struct test_pci_node *n, int bar_idx)
{
    int max_bars = n->is_bridge ? 2 : 6;
    struct test_pci_bar_info *b;

    if (bar_idx < 0 || bar_idx >= max_bars)
        return 0;

    b = &n->bars[bar_idx];
    if (b->size > 0) {
        uint32_t mask;
        if (b->is_io) {
            mask = (~(b->size - 1)) & 0xFFFFFFFC;
            if (b->io_hi16_zero)
                mask &= 0x0000FFFF;
            mask |= 0x1;
        } else {
            mask = (~(b->size - 1)) & 0xFFFFFFF0;
            if (b->is_64bit)
                mask |= 0x4;
            if (b->is_prefetch)
                mask |= 0x8;
        }
        return mask;
    }

    /* Check if this is the upper half of a 64-bit BAR */
    if (bar_idx > 0 &&
        n->bars[bar_idx - 1].is_64bit &&
        n->bars[bar_idx - 1].size > 0) {
        uint32_t um = n->bars[bar_idx - 1].upper_mask;
        return um ? um : 0xFFFFFFFF;
    }

    return 0; /* BAR not implemented */
}

/*
 * Mock functions
 */
void mmio_write32(uintptr_t address, uint32_t value)
{
    uint8_t bus, dev, func;
    uint16_t off;
    struct test_pci_node *n;
    int max_bars;
    int bar_idx;

    ck_assert_ptr_nonnull(current_topology);

    ecam_decode(address, &bus, &dev, &func, &off);
    n = test_pci_find_node(current_topology, bus, dev, func);
    if (n == NULL)
        return; /* write to void */
    if (off + 4 > TEST_PCI_CFG_SIZE)
        return;

    max_bars = n->is_bridge ? 2 : 6;

    /* BAR probing: writing 0xFFFFFFFF to a BAR offset */
    if (value == 0xFFFFFFFF) {
        if (off >= PCI_BAR0_OFFSET &&
            off < (uint16_t)(PCI_BAR0_OFFSET + max_bars * 4)) {
            uint32_t mask;
            bar_idx = (off - PCI_BAR0_OFFSET) / 4;
            n->bar_probed[bar_idx] = 1;

            /* Lower half of a 64-bit BAR pair: only return the full size
             * mask when the upper half has also been written with
             * 0xFFFFFFFF.  Real hardware requires both registers to be in
             * probe mode before either reports a valid size.  When only the
             * lower half is probed, return just the type bits so the caller
             * can still detect MMIO/64-bit/prefetch, but the size portion
             * is zero — exposing code that reads the mask too early. */
            if (n->bars[bar_idx].is_64bit && n->bars[bar_idx].size > 0) {
                if (!n->bar_probed[bar_idx + 1]) {
                    uint32_t type_bits = 0;
                    if (n->bars[bar_idx].is_64bit)
                        type_bits |= 0x4;
                    if (n->bars[bar_idx].is_prefetch)
                        type_bits |= 0x8;
                    memcpy(&n->cfg[off], &type_bits, 4);
                    return;
                }
            }

            /* Upper half of a 64-bit BAR pair: write its own mask and, if
             * the lower half was already probed, retroactively fix the
             * lower half's cfg entry with the correct full mask. */
            if (bar_idx > 0 &&
                n->bars[bar_idx - 1].is_64bit &&
                n->bars[bar_idx - 1].size > 0) {
                mask = test_pci_bar_probe_mask(n, bar_idx);
                memcpy(&n->cfg[off], &mask, 4);
                if (n->bar_probed[bar_idx - 1]) {
                    uint32_t low_mask = test_pci_bar_probe_mask(n, bar_idx - 1);
                    uint16_t low_off = PCI_BAR0_OFFSET + (bar_idx - 1) * 4;
                    memcpy(&n->cfg[low_off], &low_mask, 4);
                }
                return;
            }

            mask = test_pci_bar_probe_mask(n, bar_idx);
            memcpy(&n->cfg[off], &mask, 4);
            return;
        }
    }

    /* Normal write: clear probe state for BAR offsets */
    if (off >= PCI_BAR0_OFFSET &&
        off < (uint16_t)(PCI_BAR0_OFFSET + max_bars * 4)) {
        bar_idx = (off - PCI_BAR0_OFFSET) / 4;
        n->bar_probed[bar_idx] = 0;
    }

    memcpy(&n->cfg[off], &value, 4);
}

uint32_t mmio_read32(uintptr_t address)
{
    uint8_t bus, dev, func;
    uint16_t off;
    struct test_pci_node *n;
    uint32_t val;

    ck_assert_ptr_nonnull(current_topology);

    ecam_decode(address, &bus, &dev, &func, &off);
    n = test_pci_find_node(current_topology, bus, dev, func);
    if (n == NULL)
        return 0xFFFFFFFF;
    if (off + 4 > TEST_PCI_CFG_SIZE)
        return 0xFFFFFFFF;

    memcpy(&val, &n->cfg[off], 4);
    return val;
}

void panic(void)
{
    ck_abort_msg("panic!");
}

/*
 * Test cases
 */

/* Migrated: test_pci_config_write (uses topology instead of mmap) */

/* test_topology_build: verify topology construction */
START_TEST(test_topology_build)
{
    struct test_pci_topology t;
    int d0, d1, br, d2;

    test_pci_init(&t);
    d0 = test_pci_add_dev(&t, 0, 0, 0x1111, 0x2222, TEST_PCI_ROOT_BUS);
    d1 = test_pci_add_dev(&t, 1, 0, 0x3333, 0x4444, TEST_PCI_ROOT_BUS);
    br = test_pci_add_bridge(&t, 2, 0, 0x5555, 0x6666, TEST_PCI_ROOT_BUS);
    d2 = test_pci_add_dev(&t, 0, 0, 0x7777, 0x8888, br);

    ck_assert_int_eq(t.count, 4);

    /* d0 */
    ck_assert_uint_eq(t.nodes[d0].in_use, 1);
    ck_assert_uint_eq(t.nodes[d0].dev, 0);
    ck_assert_uint_eq(t.nodes[d0].func, 0);
    ck_assert_uint_eq(t.nodes[d0].is_bridge, 0);
    ck_assert_int_eq(t.nodes[d0].parent_bus, TEST_PCI_ROOT_BUS);
    ck_assert_uint_eq(t.nodes[d0].vendor_id, 0x1111);
    ck_assert_uint_eq(t.nodes[d0].device_id, 0x2222);

    /* d1 */
    ck_assert_uint_eq(t.nodes[d1].dev, 1);
    ck_assert_int_eq(t.nodes[d1].parent_bus, TEST_PCI_ROOT_BUS);

    /* bridge */
    ck_assert_uint_eq(t.nodes[br].is_bridge, 1);
    ck_assert_uint_eq(t.nodes[br].dev, 2);

    /* device behind bridge */
    ck_assert_uint_eq(t.nodes[d2].dev, 0);
    ck_assert_int_eq(t.nodes[d2].parent_bus, br);
    ck_assert_uint_eq(t.nodes[d2].is_bridge, 0);

    /* node_bus before commit: cfg is all zeros */
    test_pci_commit(&t);
    ck_assert_uint_eq(test_pci_node_bus(&t, d0), 0);
    ck_assert_uint_eq(test_pci_node_bus(&t, d1), 0);
    ck_assert_uint_eq(test_pci_node_bus(&t, br), 0);
    /* d2's bus = bridge's secondary_bus = 0 (not yet configured) */
    ck_assert_uint_eq(test_pci_node_bus(&t, d2), 0);

    test_pci_cleanup(&t);
}
END_TEST

/* test_topology_commit: verify cfg[] initialization */
START_TEST(test_topology_commit)
{
    struct test_pci_topology t;
    int ep, br;
    uint16_t vid, did;

    test_pci_init(&t);
    ep = test_pci_add_dev(&t, 3, 0, 0xAAAA, 0xBBBB, TEST_PCI_ROOT_BUS);
    br = test_pci_add_bridge(&t, 5, 0, 0xCCCC, 0xDDDD, TEST_PCI_ROOT_BUS);
    test_pci_commit(&t);

    /* Endpoint: vendor/device */
    vid = pci_config_read16(0, 3, 0, PCI_VENDOR_ID_OFFSET);
    did = pci_config_read16(0, 3, 0, PCI_DEVICE_ID_OFFSET);
    ck_assert_uint_eq(vid, 0xAAAA);
    ck_assert_uint_eq(did, 0xBBBB);
    /* Endpoint: header_type = 0x00 */
    ck_assert_uint_eq(pci_config_read8(0, 3, 0, 0x0E), 0x00);

    /* Bridge: vendor/device */
    vid = pci_config_read16(0, 5, 0, PCI_VENDOR_ID_OFFSET);
    did = pci_config_read16(0, 5, 0, PCI_DEVICE_ID_OFFSET);
    ck_assert_uint_eq(vid, 0xCCCC);
    ck_assert_uint_eq(did, 0xDDDD);
    /* Bridge: header_type = 0x01 */
    ck_assert_uint_eq(pci_config_read8(0, 5, 0, 0x0E), 0x01);
    /* Bridge: class=0x06, subclass=0x04 */
    ck_assert_uint_eq(pci_config_read8(0, 5, 0, 0x0B), 0x06);
    ck_assert_uint_eq(pci_config_read8(0, 5, 0, 0x0A), 0x04);

    test_pci_cleanup(&t);
}
END_TEST

/* test_find_node_root_bus: find devices on bus 0 */
START_TEST(test_find_node_root_bus)
{
    struct test_pci_topology t;
    struct test_pci_node *found;
    int d0, d2, d5;

    test_pci_init(&t);
    d0 = test_pci_add_dev(&t, 0, 0, 0x1000, 0x0001, TEST_PCI_ROOT_BUS);
    d2 = test_pci_add_dev(&t, 2, 0, 0x1000, 0x0002, TEST_PCI_ROOT_BUS);
    d5 = test_pci_add_dev(&t, 5, 0, 0x1000, 0x0005, TEST_PCI_ROOT_BUS);
    test_pci_commit(&t);

    /* Find existing devices */
    found = test_pci_find_node(&t, 0, 0, 0);
    ck_assert_ptr_nonnull(found);
    ck_assert_ptr_eq(found, &t.nodes[d0]);

    found = test_pci_find_node(&t, 0, 2, 0);
    ck_assert_ptr_nonnull(found);
    ck_assert_ptr_eq(found, &t.nodes[d2]);

    found = test_pci_find_node(&t, 0, 5, 0);
    ck_assert_ptr_nonnull(found);
    ck_assert_ptr_eq(found, &t.nodes[d5]);

    /* Non-existent devices */
    ck_assert_ptr_null(test_pci_find_node(&t, 0, 1, 0));
    ck_assert_ptr_null(test_pci_find_node(&t, 0, 3, 0));
    ck_assert_ptr_null(test_pci_find_node(&t, 0, 31, 0));
    /* Wrong function number */
    ck_assert_ptr_null(test_pci_find_node(&t, 0, 0, 1));

    test_pci_cleanup(&t);
}
END_TEST

/* test_find_node_behind_bridge: bus routing validation */
START_TEST(test_find_node_behind_bridge)
{
    struct test_pci_topology t;
    struct test_pci_node *found;
    int br, d_behind;

    test_pci_init(&t);
    br = test_pci_add_bridge(&t, 1, 0, 0xAAAA, 0xBBBB, TEST_PCI_ROOT_BUS);
    d_behind = test_pci_add_dev(&t, 0, 0, 0xCCCC, 0xDDDD, br);
    test_pci_commit(&t);

    /* Before configuring bridge: bus 1 not routable */
    found = test_pci_find_node(&t, 1, 0, 0);
    ck_assert_ptr_null(found);

    /* Configure bridge: primary=0, secondary=1, subordinate=1 */
    t.nodes[br].cfg[PCI_PRIMARY_BUS]   = 0;
    t.nodes[br].cfg[PCI_SECONDARY_BUS] = 1;
    t.nodes[br].cfg[PCI_SUB_SEC_BUS]   = 1;

    /* Now bus 1 is routable */
    found = test_pci_find_node(&t, 1, 0, 0);
    ck_assert_ptr_nonnull(found);
    ck_assert_ptr_eq(found, &t.nodes[d_behind]);

    /* Verify node_bus for the child */
    ck_assert_uint_eq(test_pci_node_bus(&t, d_behind), 1);

    /* Bus 2 is still unreachable (subordinate=1) */
    ck_assert_ptr_null(test_pci_find_node(&t, 2, 0, 0));

    test_pci_cleanup(&t);
}
END_TEST

/* test_find_node_nested_bridges: multi-level routing */
START_TEST(test_find_node_nested_bridges)
{
    struct test_pci_topology t;
    struct test_pci_node *found;
    int brA, brB, dev_leaf;

    test_pci_init(&t);
    brA = test_pci_add_bridge(&t, 0, 0, 0x1111, 0x2222, TEST_PCI_ROOT_BUS);
    brB = test_pci_add_bridge(&t, 0, 0, 0x3333, 0x4444, brA);
    dev_leaf = test_pci_add_dev(&t, 0, 0, 0x5555, 0x6666, brB);
    test_pci_commit(&t);

    /* Configure bridge A: primary=0, secondary=1, subordinate=2 */
    t.nodes[brA].cfg[PCI_PRIMARY_BUS]   = 0;
    t.nodes[brA].cfg[PCI_SECONDARY_BUS] = 1;
    t.nodes[brA].cfg[PCI_SUB_SEC_BUS]   = 2;

    /* Configure bridge B: primary=1, secondary=2, subordinate=2 */
    t.nodes[brB].cfg[PCI_PRIMARY_BUS]   = 1;
    t.nodes[brB].cfg[PCI_SECONDARY_BUS] = 2;
    t.nodes[brB].cfg[PCI_SUB_SEC_BUS]   = 2;

    /* Device on bus 2 is reachable */
    found = test_pci_find_node(&t, 2, 0, 0);
    ck_assert_ptr_nonnull(found);
    ck_assert_ptr_eq(found, &t.nodes[dev_leaf]);

    /* Break routing: set bridge A subordinate to 1 (doesn't include bus 2) */
    t.nodes[brA].cfg[PCI_SUB_SEC_BUS] = 1;
    found = test_pci_find_node(&t, 2, 0, 0);
    ck_assert_ptr_null(found);

    /* Fix bridge A, break bridge B: set secondary to 0 (not configured) */
    t.nodes[brA].cfg[PCI_SUB_SEC_BUS] = 2;
    t.nodes[brB].cfg[PCI_SECONDARY_BUS] = 0;
    found = test_pci_find_node(&t, 2, 0, 0);
    ck_assert_ptr_null(found);

    test_pci_cleanup(&t);
}
END_TEST

/* test_bar_probe_mask: BAR mask computation */
START_TEST(test_bar_probe_mask)
{
    struct test_pci_topology t;
    struct test_pci_node *n;
    int dev_node;
    uint32_t mask;

    test_pci_init(&t);
    dev_node = test_pci_add_dev(&t, 0, 0, 0x1234, 0x5678, TEST_PCI_ROOT_BUS);
    n = &t.nodes[dev_node];

    /* 32-bit MMIO 64KB */
    memset(n->bars, 0, sizeof(n->bars));
    n->bars[0].size = 0x10000;
    n->bars[0].is_io = 0;
    n->bars[0].is_64bit = 0;
    n->bars[0].is_prefetch = 0;
    mask = test_pci_bar_probe_mask(n, 0);
    ck_assert_uint_eq(mask, 0xFFFF0000);

    /* 32-bit MMIO 4KB */
    n->bars[0].size = 0x1000;
    mask = test_pci_bar_probe_mask(n, 0);
    ck_assert_uint_eq(mask, 0xFFFFF000);

    /* 64-bit prefetchable MMIO 1MB — lower half */
    memset(n->bars, 0, sizeof(n->bars));
    n->bars[0].size = 0x100000;
    n->bars[0].is_io = 0;
    n->bars[0].is_64bit = 1;
    n->bars[0].is_prefetch = 1;
    mask = test_pci_bar_probe_mask(n, 0);
    ck_assert_uint_eq(mask, 0xFFF0000C); /* ~(1MB-1) & 0xFFFFFFF0 | 0x4 | 0x8 */

    /* 64-bit BAR — upper half (bar index 1) */
    mask = test_pci_bar_probe_mask(n, 1);
    ck_assert_uint_eq(mask, 0xFFFFFFFF);

    /* IO BAR 256 bytes */
    memset(n->bars, 0, sizeof(n->bars));
    n->bars[2].size = 256;
    n->bars[2].is_io = 1;
    n->bars[2].is_64bit = 0;
    n->bars[2].is_prefetch = 0;
    mask = test_pci_bar_probe_mask(n, 2);
    ck_assert_uint_eq(mask, 0xFFFFFF01); /* ~(256-1) & 0xFFFFFFFC | 0x1 */

    /* Unimplemented BAR */
    mask = test_pci_bar_probe_mask(n, 3);
    ck_assert_uint_eq(mask, 0x00000000);

    /* Out of range BAR index */
    mask = test_pci_bar_probe_mask(n, 6);
    ck_assert_uint_eq(mask, 0x00000000);
    mask = test_pci_bar_probe_mask(n, -1);
    ck_assert_uint_eq(mask, 0x00000000);
}
END_TEST

START_TEST(test_mmio_mock_bar_probe)
{
    struct test_pci_topology t;
    uint32_t val;
    int dev_node;

    test_pci_init(&t);
    dev_node = test_pci_add_dev(&t, 3, 0, 0xAAAA, 0xBBBB, TEST_PCI_ROOT_BUS);
    test_pci_dev_set_bar(&t, dev_node, 0, 0x10000, TEST_PCI_BAR_MMIO); /* 64KB MMIO */
    test_pci_commit(&t);

    /* Probe BAR: write 0xFFFFFFFF, read back size mask */
    pci_config_write32(0, 3, 0, PCI_BAR0_OFFSET, 0xFFFFFFFF);
    val = pci_config_read32(0, 3, 0, PCI_BAR0_OFFSET);
    ck_assert_uint_eq(val, 0xFFFF0000);

    /* Write a normal address, read it back */
    pci_config_write32(0, 3, 0, PCI_BAR0_OFFSET, 0x80010000);
    val = pci_config_read32(0, 3, 0, PCI_BAR0_OFFSET);
    ck_assert_uint_eq(val, 0x80010000);

    /* Probe unimplemented BAR */
    pci_config_write32(0, 3, 0, PCI_BAR0_OFFSET + 4, 0xFFFFFFFF);
    val = pci_config_read32(0, 3, 0, PCI_BAR0_OFFSET + 4);
    ck_assert_uint_eq(val, 0x00000000);

    test_pci_cleanup(&t);
}
END_TEST

START_TEST(test_mmio_mock_unreachable)
{
    struct test_pci_topology t;
    uint32_t val;
    int br;

    test_pci_init(&t);
    br = test_pci_add_bridge(&t, 1, 0, 0x1111, 0x2222, TEST_PCI_ROOT_BUS);
    test_pci_add_dev(&t, 0, 0, 0x3333, 0x4444, br);
    test_pci_commit(&t);

    /* Device behind unconfigured bridge: 0xFFFFFFFF */
    val = pci_config_read32(1, 0, 0, PCI_VENDOR_ID_OFFSET);
    ck_assert_uint_eq(val, 0xFFFFFFFF);

    /* Non-existent device on bus 0 */
    val = pci_config_read32(0, 31, 0, PCI_VENDOR_ID_OFFSET);
    ck_assert_uint_eq(val, 0xFFFFFFFF);

    /* Non-existent bus entirely */
    val = pci_config_read32(5, 0, 0, PCI_VENDOR_ID_OFFSET);
    ck_assert_uint_eq(val, 0xFFFFFFFF);

    /* Bridge itself IS visible on bus 0 */
    val = pci_config_read32(0, 1, 0, PCI_VENDOR_ID_OFFSET);
    ck_assert_uint_ne(val, 0xFFFFFFFF);

    test_pci_cleanup(&t);
}
END_TEST

/*
 * Tests exercising actual pci.c code
 */

START_TEST(test_pci_program_bar_64bit)
{
    struct test_pci_topology t;
    struct pci_enum_info info;
    uint8_t is_64bit = 0;
    int dev_node;
    int ret;

    test_pci_init(&t);
    dev_node = test_pci_add_dev(&t, 0, 0, 0x1234, 0x5678, TEST_PCI_ROOT_BUS);
    /* 64-bit prefetchable MMIO BAR, 1MB */
    test_pci_dev_set_bar(&t, dev_node, 0, 0x100000, TEST_PCI_BAR_64BIT | TEST_PCI_BAR_PF);
    test_pci_commit(&t);

    memset(&info, 0, sizeof(info));
    info.mem_pf = 0x90000000;
    info.mem_pf_limit = 0xFFFFFFFF;
    info.mem = 0x80000000;
    info.mem_limit = 0x88000000;
    info.io = 0x2000;

    ret = pci_program_bar(0, 0, 0, 0, &info, &is_64bit);

    /* pci_program_bar must succeed and recognise this as a 64-bit BAR */
    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(is_64bit, 1);

    /* The prefetchable memory base must advance by the BAR size (1MB).
     * With the ordering bug the lower BAR mask is read before the upper
     * register is written with 0xFFFFFFFF, so the mock returns only type
     * bits (size portion = 0) and pci_program_bar computes length = 0,
     * leaving mem_pf unchanged. */
    ck_assert_uint_eq(info.mem_pf, 0x90000000 + 0x100000);

    test_pci_cleanup(&t);
}
END_TEST

/* test_pci_program_bar_restore: verify restore_bar on error path */
START_TEST(test_pci_program_bar_restore)
{
    struct test_pci_topology t;
    struct pci_enum_info info;
    uint8_t is_64bit = 0;
    int dev_node;
    int ret;
    uint32_t bar0_val, vid_did;

    test_pci_init(&t);
    dev_node = test_pci_add_dev(&t, 0, 0, 0x1234, 0x5678, TEST_PCI_ROOT_BUS);
    /* BAR0 is unimplemented (size=0) — probing will read back 0, triggering
     * the restore_bar path in pci_program_bar. */
    test_pci_commit(&t);

    /* Pre-fill BAR0 config register with a known value */
    {
        uint32_t orig = 0xDEAD0000;
        memcpy(&t.nodes[dev_node].cfg[PCI_BAR0_OFFSET], &orig, 4);
    }

    memset(&info, 0, sizeof(info));
    info.mem = 0x80000000;
    info.mem_limit = 0x88000000;
    info.mem_pf = 0x90000000;
    info.mem_pf_limit = 0xFFFFFFFF;
    info.io = 0x2000;

    ret = pci_program_bar(0, 0, 0, 0, &info, &is_64bit);
    /* pci_program_bar should return 0 */
    ck_assert_int_eq(ret, 0);

    /* BAR0 must be restored to the original value 0xDEAD0000. */
    bar0_val = pci_config_read32(0, 0, 0, PCI_BAR0_OFFSET);
    ck_assert_uint_eq(bar0_val, 0xDEAD0000);

    /* Also verify vendor/device ID was NOT corrupted */
    vid_did = pci_config_read32(0, 0, 0, PCI_VENDOR_ID_OFFSET);
    ck_assert_uint_ne(vid_did, 0xDEAD0000);

    test_pci_cleanup(&t);
}
END_TEST

/* test_program_bar_types: parameterized single-BAR programming */
START_TEST(test_program_bar_types)
{
    struct bar_type_case {
        const char *label;
        uint32_t size;
        int type;
        int io_hi16_zero;
        uint32_t exp_bar;          /* expected BAR value after programming */
        uint32_t exp_mem;          /* expected info.mem after */
        uint32_t exp_mem_pf;       /* expected info.mem_pf after */
        uint32_t exp_io;           /* expected info.io after */
    };
    static const struct bar_type_case cases[] = {
        {
            "32-bit MMIO 64KB", 0x10000, TEST_PCI_BAR_MMIO, 0,
            0x80000000,
            0x80000000 + 0x10000, 0x90000000, 0x2000
        },
        {
            "IO 256B", 256, TEST_PCI_BAR_IO, 0,
            0x2000,
            0x80000000, 0x90000000, 0x2000 + 256
        },
        {
            "IO 256B hi16_zero", 256, TEST_PCI_BAR_IO, 1,
            0x2000,
            0x80000000, 0x90000000, 0x2000 + 256
        },
    };
    int nc = sizeof(cases) / sizeof(cases[0]);
    int c;

    for (c = 0; c < nc; c++) {
        const struct bar_type_case *tc = &cases[c];
        struct test_pci_topology t;
        struct pci_enum_info info;
        int dev_node;
        int ret;
        uint32_t bar_val;

        test_pci_init(&t);
        dev_node = test_pci_add_dev(&t, 0, 0, 0x1234, 0x5678,
                                    TEST_PCI_ROOT_BUS);
        test_pci_dev_set_bar(&t, dev_node, 0, tc->size, tc->type);
        if (tc->io_hi16_zero)
            t.nodes[dev_node].bars[0].io_hi16_zero = 1;
        test_pci_commit(&t);

        memset(&info, 0, sizeof(info));
        info.mem = 0x80000000;
        info.mem_limit = 0x88000000;
        info.mem_pf = 0x90000000;
        info.mem_pf_limit = 0xFFFFFFFF;
        info.io = 0x2000;

        ret = pci_enum_bus(0, &info);
        ck_assert_msg(ret == 0, "%s: ret", tc->label);

        bar_val = pci_config_read32(0, 0, 0, PCI_BAR0_OFFSET);
        ck_assert_msg(bar_val == tc->exp_bar, "%s: bar", tc->label);
        ck_assert_msg(info.mem == tc->exp_mem, "%s: mem", tc->label);
        ck_assert_msg(info.mem_pf == tc->exp_mem_pf, "%s: mem_pf", tc->label);
        ck_assert_msg(info.io == tc->exp_io, "%s: io", tc->label);

        test_pci_cleanup(&t);
    }
}
END_TEST

/* test_program_bar_out_of_range: bar_idx >= 6 returns -1 */
START_TEST(test_program_bar_out_of_range)
{
    struct test_pci_topology t;
    struct pci_enum_info info;
    uint8_t is_64bit = 0;
    int ret;

    test_pci_init(&t);
    test_pci_add_dev(&t, 0, 0, 0x1234, 0x5678, TEST_PCI_ROOT_BUS);
    test_pci_commit(&t);

    memset(&info, 0, sizeof(info));
    ret = pci_program_bar(0, 0, 0, 6, &info, &is_64bit);
    ck_assert_int_eq(ret, -1);

    test_pci_cleanup(&t);
}
END_TEST

/* test_program_bar_64bit_upper_reject: upper half != 0xFFFFFFFF */
START_TEST(test_program_bar_64bit_upper_reject)
{
    struct test_pci_topology t;
    struct pci_enum_info info;
    uint8_t is_64bit = 0;
    int dev_node;
    int ret;
    uint32_t bar0_val, bar1_val;

    test_pci_init(&t);
    dev_node = test_pci_add_dev(&t, 0, 0, 0x1234, 0x5678, TEST_PCI_ROOT_BUS);
    /* 64-bit prefetchable MMIO BAR, 1MB, but upper mask = 0 (not 0xFFFFFFFF)
     * our implementation refuses to map so much address space for now */
    test_pci_dev_set_bar(&t, dev_node, 0, 0x100000, TEST_PCI_BAR_64BIT | TEST_PCI_BAR_PF);
    /* set it manually as test_pci_dev_set_bar only handle 32-bit size */
    t.nodes[dev_node].bars[0].upper_mask = 0x0000000F;
    test_pci_commit(&t);

    /* Pre-fill BAR0 and BAR1 with known values */
    {
        uint32_t orig0 = 0xAABB0000, orig1 = 0xCCDD0000;
        memcpy(&t.nodes[dev_node].cfg[PCI_BAR0_OFFSET], &orig0, 4);
        memcpy(&t.nodes[dev_node].cfg[PCI_BAR0_OFFSET + 4], &orig1, 4);
    }

    memset(&info, 0, sizeof(info));
    info.mem = 0x80000000;
    info.mem_limit = 0x88000000;
    info.mem_pf = 0x90000000;
    info.mem_pf_limit = 0xFFFFFFFF;
    info.io = 0x2000;

    ret = pci_program_bar(0, 0, 0, 0, &info, &is_64bit);
    /* Should return 0 (ret is initialized to 0, "too much memory" path
     * doesn't change ret before goto restore_bar) */
    ck_assert_int_eq(ret, 0);

    /* BAR0 must be restored, BAR1 (upper half) must be restored */
    bar0_val = pci_config_read32(0, 0, 0, PCI_BAR0_OFFSET);
    ck_assert_uint_eq(bar0_val, 0xAABB0000);
    bar1_val = pci_config_read32(0, 0, 0, PCI_BAR0_OFFSET + 4);
    ck_assert_uint_eq(bar1_val, 0xCCDD0000);

    /* is_64bit must be set so the caller skips the next BAR index */
    ck_assert_uint_eq(is_64bit, 1);

    /* Allocators unchanged */
    ck_assert_uint_eq(info.mem_pf, 0x90000000);

    test_pci_cleanup(&t);
}
END_TEST

/* test_program_bar_no_space: limit exceeded → restore_bar */
START_TEST(test_program_bar_no_space)
{
    struct test_pci_topology t;
    struct pci_enum_info info;
    uint8_t is_64bit = 0;
    int dev_node;
    int ret;
    uint32_t bar0_val;

    test_pci_init(&t);
    dev_node = test_pci_add_dev(&t, 0, 0, 0x1234, 0x5678, TEST_PCI_ROOT_BUS);
    test_pci_dev_set_bar(&t, dev_node, 0, 0x100000, TEST_PCI_BAR_MMIO); /* 1MB MMIO */
    test_pci_commit(&t);

    /* Pre-fill BAR0 */
    {
        uint32_t orig = 0xBEEF0000;
        memcpy(&t.nodes[dev_node].cfg[PCI_BAR0_OFFSET], &orig, 4);
    }

    memset(&info, 0, sizeof(info));
    /* mem not aligned to 1MB and limit too close → alignment overshoots */
    info.mem = 0x80080000;
    info.mem_limit = 0x80100000;
    info.mem_pf = 0x90000000;
    info.mem_pf_limit = 0xFFFFFFFF;
    info.io = 0x2000;

    ret = pci_program_bar(0, 0, 0, 0, &info, &is_64bit);
    ck_assert_int_ne(ret, 0);

    /* BAR0 restored */
    bar0_val = pci_config_read32(0, 0, 0, PCI_BAR0_OFFSET);
    ck_assert_uint_eq(bar0_val, 0xBEEF0000);

    /* Allocator unchanged */
    ck_assert_uint_eq(info.mem, 0x80080000);

    test_pci_cleanup(&t);
}
END_TEST

/* test_program_bars_iteration: full BAR iteration with mixed types */
START_TEST(test_program_bars_iteration)
{
    struct test_pci_topology t;
    struct pci_enum_info info;
    struct test_pci_node *n;
    int dev_node;
    uint32_t bar_val;
    uint16_t cmd_before, cmd_after;

    test_pci_init(&t);
    dev_node = test_pci_add_dev(&t, 0, 0, 0x1234, 0x5678, TEST_PCI_ROOT_BUS);
    /* BAR0: 32-bit MMIO non-prefetch 64KB */
    test_pci_dev_set_bar(&t, dev_node, 0, 0x10000, TEST_PCI_BAR_MMIO);
    /* BAR1: unimplemented */
    /* BAR2: 64-bit prefetchable MMIO 1MB (consumes BAR2+BAR3) */
    test_pci_dev_set_bar(&t, dev_node, 2, 0x100000, TEST_PCI_BAR_64BIT | TEST_PCI_BAR_PF);
    /* BAR4: IO 256 bytes */
    test_pci_dev_set_bar(&t, dev_node, 4, 256, TEST_PCI_BAR_IO);
    /* BAR5: unimplemented */
    test_pci_commit(&t);
    n = &t.nodes[dev_node];

    /* Set a known command register value */
    cmd_before = 0x0007;
    {
        uint16_t cmd = cmd_before;
        memcpy(&n->cfg[PCI_COMMAND_OFFSET], &cmd, 2);
    }

    memset(&info, 0, sizeof(info));
    info.mem = 0x80000000;
    info.mem_limit = 0x88000000;
    info.mem_pf = 0x90000000;
    info.mem_pf_limit = 0xFFFFFFFF;
    info.io = 0x2000;

    pci_enum_bus(0, &info);

    /* BAR0: programmed from mem */
    bar_val = pci_config_read32(0, 0, 0, PCI_BAR0_OFFSET);
    ck_assert_uint_eq(bar_val, 0x80000000);
    ck_assert_uint_eq(info.mem, 0x80000000 + 0x10000);

    /* BAR2: programmed from mem_pf (64-bit) */
    bar_val = pci_config_read32(0, 0, 0, PCI_BAR0_OFFSET + 2 * 4);
    ck_assert_uint_eq(bar_val, 0x90000000);
    /* BAR3 (upper half): should be 0 */
    bar_val = pci_config_read32(0, 0, 0, PCI_BAR0_OFFSET + 3 * 4);
    ck_assert_uint_eq(bar_val, 0x00000000);
    ck_assert_uint_eq(info.mem_pf, 0x90000000 + 0x100000);

    /* BAR4: programmed from io */
    bar_val = pci_config_read32(0, 0, 0, PCI_BAR0_OFFSET + 4 * 4);
    ck_assert_uint_eq(bar_val, 0x2000);
    ck_assert_uint_eq(info.io, 0x2000 + 256);

    /* Command register restored */
    cmd_after = pci_config_read16(0, 0, 0, PCI_COMMAND_OFFSET);
    ck_assert_uint_eq(cmd_after, cmd_before);

    test_pci_cleanup(&t);
}
END_TEST

/* test_program_bridge: parameterized bridge programming tests */

START_TEST(test_program_bridge)
{
    struct bar_spec {
        int idx;
        uint32_t size;
        int type;
    };
    struct bridge_case {
        const char *label;
        int num_bars;
        struct bar_spec bars[3];
        uint32_t exp_bars[3];
        uint16_t exp_mbase, exp_mlimit;
        uint16_t exp_pfbase, exp_pflimit;
        uint8_t exp_iobase, exp_iolimit;
        uint16_t exp_cmd;
    };
    static const struct bridge_case cases[] = {
        {
            "no devices", 0, {{0, 0, 0}}, {0},
            0xFFFF, 0x0000, 0xFFFF, 0x0000, 0xFF, 0x00, 0x0004
        },
        {
            "MMIO 64KB", 1,
            {{0, 0x10000, TEST_PCI_BAR_MMIO}},
            {0x80000000},
            0x8000, 0x800F, 0xFFFF, 0x0000, 0xFF, 0x00, 0x0006
        },
        {
            "PF 1MB 64bit", 1,
            {{0, 0x100000, TEST_PCI_BAR_64BIT | TEST_PCI_BAR_PF}},
            {0x90000000},
            0xFFFF, 0x0000, 0x9000, 0x900F, 0xFF, 0x00, 0x0006
        },
        {
            "IO 256B", 1,
            {{0, 256, TEST_PCI_BAR_IO}},
            {0x2000},
            0xFFFF, 0x0000, 0xFFFF, 0x0000, 0x20, 0x2F, 0x0005
        },
        {
            "all windows", 3,
            {{0, 0x10000, TEST_PCI_BAR_MMIO},
             {1, 0x10000, TEST_PCI_BAR_PF},
             {2, 256, TEST_PCI_BAR_IO}},
            {0x80000000, 0x90000000, 0x2000},
            0x8000, 0x800F, 0x9000, 0x900F, 0x20, 0x2F, 0x0007
        },
    };
    int nc = sizeof(cases) / sizeof(cases[0]);
    int c;

    for (c = 0; c < nc; c++) {
        const struct bridge_case *tc = &cases[c];
        struct test_pci_topology t;
        struct pci_enum_info info;
        int br, ep, i;
        uint8_t sec;

        test_pci_init(&t);
        br = test_pci_add_bridge(&t, 1, 0, 0xAAAA, 0xBBBB, TEST_PCI_ROOT_BUS);
        if (tc->num_bars > 0) {
            ep = test_pci_add_dev(&t, 0, 0, 0xCCCC, 0xDDDD, br);
            for (i = 0; i < tc->num_bars; i++)
                test_pci_dev_set_bar(&t, ep, tc->bars[i].idx,
                                     tc->bars[i].size, tc->bars[i].type);
        }
        test_pci_commit(&t);

        memset(&info, 0, sizeof(info));
        info.mem = 0x80000000;
        info.mem_limit = 0x88000000;
        info.mem_pf = 0x90000000;
        info.mem_pf_limit = 0xFFFFFFFF;
        info.io = 0x2000;
        info.curr_bus_number = 0;

        pci_enum_bus(0, &info);

        /* Bus numbering */
        ck_assert_msg(pci_config_read8(0, 1, 0, PCI_PRIMARY_BUS) == 0,
                      "%s: prim", tc->label);
        sec = pci_config_read8(0, 1, 0, PCI_SECONDARY_BUS);
        ck_assert_msg(sec != 0, "%s: sec", tc->label);
        ck_assert_msg(pci_config_read8(0, 1, 0, PCI_SUB_SEC_BUS) >= sec,
                      "%s: sub", tc->label);

        /* Endpoint BARs */
        for (i = 0; i < tc->num_bars; i++) {
            uint32_t bar_val = pci_config_read32(sec, 0, 0,
                                   PCI_BAR0_OFFSET + tc->bars[i].idx * 4);
            ck_assert_msg(bar_val == tc->exp_bars[i],
                          "%s: BAR%d", tc->label, tc->bars[i].idx);
        }

        /* Bridge windows */
        ck_assert_msg(pci_config_read16(0, 1, 0, PCI_MMIO_BASE_OFF)
                      == tc->exp_mbase, "%s: mbase", tc->label);
        ck_assert_msg(pci_config_read16(0, 1, 0, PCI_MMIO_LIMIT_OFF)
                      == tc->exp_mlimit, "%s: mlimit", tc->label);
        ck_assert_msg(pci_config_read16(0, 1, 0, PCI_PREFETCH_BASE_OFF)
                      == tc->exp_pfbase, "%s: pfbase", tc->label);
        ck_assert_msg(pci_config_read16(0, 1, 0, PCI_PREFETCH_LIMIT_OFF)
                      == tc->exp_pflimit, "%s: pflimit", tc->label);
        ck_assert_msg(pci_config_read8(0, 1, 0, PCI_IO_BASE_OFF)
                      == tc->exp_iobase, "%s: iobase", tc->label);
        ck_assert_msg(pci_config_read8(0, 1, 0, PCI_IO_LIMIT_OFF)
                      == tc->exp_iolimit, "%s: iolimit", tc->label);

        /* Command register */
        ck_assert_msg(pci_config_read16(0, 1, 0, PCI_COMMAND_OFFSET)
                      == tc->exp_cmd, "%s: cmd", tc->label);

        test_pci_cleanup(&t);
    }
}
END_TEST

/* test_program_bridge_oom_initial: initial alignment failures */
START_TEST(test_program_bridge_oom_initial)
{
    struct {
        const char *label;
        uint16_t cmd_before;
        struct pci_enum_info info;
    } cases[] = {
        {
            "pf: 1MB align wraps past 32-bit",
            0x0007,
            { .mem_pf = 0xFFF00001, .mem_pf_limit = 0xFFFFFFFF,
              .mem = 0x80000000, .mem_limit = 0x88000000,
              .io = 0x2000 }
        },
        {
            "mem: 1MB align wraps past 32-bit",
            0x0003,
            { .mem_pf = 0x90000000, .mem_pf_limit = 0xFFFFFFFF,
              .mem = 0xFFF00001, .mem_limit = 0xFFFFFFFF,
              .io = 0x2000 }
        },
        {
            "io: 4KB align wraps past 32-bit",
            0x0005,
            { .mem_pf = 0x90000000, .mem_pf_limit = 0xFFFFFFFF,
              .mem = 0x80000000, .mem_limit = 0x88000000,
              .io = 0xFFFFF001 }
        },
    };
    int i;

    for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        struct test_pci_topology t;
        struct pci_enum_info saved;
        int br, ret;

        test_pci_init(&t);
        br = test_pci_add_bridge(&t, 1, 0, 0xAAAA, 0xBBBB, TEST_PCI_ROOT_BUS);
        test_pci_commit(&t);
        memcpy(&t.nodes[br].cfg[PCI_COMMAND_OFFSET], &cases[i].cmd_before, 2);

        saved = cases[i].info;
        ret = pci_program_bridge(0, 1, 0, &cases[i].info);
        ck_assert_msg(ret == -1, "expected failure for: %s", cases[i].label);
        {
            uint16_t cmd = pci_config_read16(0, 1, 0, PCI_COMMAND_OFFSET);
            ck_assert_msg(cmd == cases[i].cmd_before,
                          "command register changed for: %s", cases[i].label);
        }
        /* info state must be fully restored */
        ck_assert_msg(cases[i].info.curr_bus_number == saved.curr_bus_number,
                      "%s: curr_bus_number not restored", cases[i].label);
        ck_assert_msg(cases[i].info.mem == saved.mem,
                      "%s: mem not restored", cases[i].label);
        ck_assert_msg(cases[i].info.mem_pf == saved.mem_pf,
                      "%s: mem_pf not restored", cases[i].label);
        ck_assert_msg(cases[i].info.io == saved.io,
                      "%s: io not restored", cases[i].label);
        /* bridge bus registers must be cleared */
        ck_assert_msg(pci_config_read8(0, 1, 0, PCI_SECONDARY_BUS) == 0,
                      "%s: secondary bus not cleared", cases[i].label);
        ck_assert_msg(pci_config_read8(0, 1, 0, PCI_SUB_SEC_BUS) == 0,
                      "%s: subordinate bus not cleared", cases[i].label);
        test_pci_cleanup(&t);
    }
}
END_TEST

/* test_program_bridge_oom_post_enum: pf/mem/io space exhausted after enum */
START_TEST(test_program_bridge_oom_post_enum)
{
    struct {
        const char *label;
        uint32_t bar_size;
        unsigned int bar_type;
        struct pci_enum_info info;
    } cases[] = {
        {
            "pf: post-enum 1MB align exceeds limit",
            0x10000, TEST_PCI_BAR_PF,
            { .mem = 0x80000000, .mem_limit = 0x88000000,
              .mem_pf = 0x90000000, .mem_pf_limit = 0x90100000,
              .io = 0x2000 }
        },
        {
            "mem: post-enum 1MB align exceeds limit",
            0x10000, TEST_PCI_BAR_MMIO,
            { .mem = 0x80000000, .mem_limit = 0x80100000,
              .mem_pf = 0x90000000, .mem_pf_limit = 0xFFFFFFFF,
              .io = 0x2000 }
        },
        {
            "io: post-enum 4KB align wraps 32-bit space",
            256, TEST_PCI_BAR_IO,
            { .mem = 0x80000000, .mem_limit = 0x88000000,
              .mem_pf = 0x90000000, .mem_pf_limit = 0xFFFFFFFF,
              .io = 0xFFFFF000 }
        },
    };
    int i;

    for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        struct test_pci_topology t;
        struct pci_enum_info saved;
        int br, ep, ret;
        uint16_t cmd_before = 0x0007;

        test_pci_init(&t);
        br = test_pci_add_bridge(&t, 1, 0, 0xAAAA, 0xBBBB, TEST_PCI_ROOT_BUS);
        ep = test_pci_add_dev(&t, 0, 0, 0xCCCC, 0xDDDD, br);
        test_pci_dev_set_bar(&t, ep, 0, cases[i].bar_size, cases[i].bar_type);
        test_pci_commit(&t);
        memcpy(&t.nodes[br].cfg[PCI_COMMAND_OFFSET], &cmd_before, 2);

        cases[i].info.curr_bus_number = 0;
        saved = cases[i].info;
        ret = pci_program_bridge(0, 1, 0, &cases[i].info);
        ck_assert_msg(ret == -1, "expected failure for: %s", cases[i].label);
        {
            uint16_t cmd = pci_config_read16(0, 1, 0, PCI_COMMAND_OFFSET);
            ck_assert_msg(cmd == cmd_before,
                          "command register changed for: %s", cases[i].label);
        }
        /* info state must be fully restored */
        ck_assert_msg(cases[i].info.curr_bus_number == saved.curr_bus_number,
                      "%s: curr_bus_number not restored", cases[i].label);
        ck_assert_msg(cases[i].info.mem == saved.mem,
                      "%s: mem not restored", cases[i].label);
        ck_assert_msg(cases[i].info.mem_pf == saved.mem_pf,
                      "%s: mem_pf not restored", cases[i].label);
        ck_assert_msg(cases[i].info.io == saved.io,
                      "%s: io not restored", cases[i].label);
        /* bridge bus registers must be cleared */
        ck_assert_msg(pci_config_read8(0, 1, 0, PCI_SECONDARY_BUS) == 0,
                      "%s: secondary bus not cleared", cases[i].label);
        ck_assert_msg(pci_config_read8(0, 1, 0, PCI_SUB_SEC_BUS) == 0,
                      "%s: subordinate bus not cleared", cases[i].label);
        test_pci_cleanup(&t);
    }
}
END_TEST

/* test_enum_bus_topology: device dispatch + multifunction handling */
START_TEST(test_enum_bus_topology)
{
    struct test_pci_topology t;
    struct pci_enum_info info;
    int d0, mf0, mf1;
    uint32_t bar_val;

    test_pci_init(&t);
    /* dev 0: single-function endpoint with 32-bit MMIO BAR */
    d0 = test_pci_add_dev(&t, 0, 0, 0x1111, 0x2222, TEST_PCI_ROOT_BUS);
    test_pci_dev_set_bar(&t, d0, 0, 0x10000, TEST_PCI_BAR_MMIO);
    /* dev 1 func 0: multifunction endpoint with IO BAR */
    mf0 = test_pci_add_dev(&t, 1, 0, 0x3333, 0x4444, TEST_PCI_ROOT_BUS);
    test_pci_dev_set_bar(&t, mf0, 0, 256, TEST_PCI_BAR_IO);
    /* dev 1 func 1: second function with IO BAR */
    mf1 = test_pci_add_dev(&t, 1, 1, 0x3333, 0x5555, TEST_PCI_ROOT_BUS);
    test_pci_dev_set_bar(&t, mf1, 0, 256, TEST_PCI_BAR_IO);
    test_pci_commit(&t);

    /* Mark dev 1 func 0 as multifunction */
    t.nodes[mf0].cfg[PCI_HEADER_TYPE_OFFSET] |= PCI_HEADER_TYPE_MULTIFUNC_MASK;

    memset(&info, 0, sizeof(info));
    info.mem = 0x80000000;
    info.mem_limit = 0x88000000;
    info.mem_pf = 0x90000000;
    info.mem_pf_limit = 0xFFFFFFFF;
    info.io = 0x2000;
    info.curr_bus_number = 0;

    pci_enum_bus(0, &info);

    /* dev 0 BAR0 should be programmed (MMIO) */
    bar_val = pci_config_read32(0, 0, 0, PCI_BAR0_OFFSET);
    ck_assert_uint_eq(bar_val, 0x80000000);

    /* dev 1 func 0 BAR0 should be programmed (IO) */
    bar_val = pci_config_read32(0, 1, 0, PCI_BAR0_OFFSET);
    ck_assert_uint_eq(bar_val, 0x2000);

    /* dev 1 func 1 BAR0 should also be programmed (multifunction).
     * IO base after func 0 is 0x2100, but 4KB alignment rounds up to 0x3000 */
    bar_val = pci_config_read32(0, 1, 1, PCI_BAR0_OFFSET);
    ck_assert_uint_eq(bar_val, 0x3000);

    test_pci_cleanup(&t);
}
END_TEST

/* test_enum_do_full: end-to-end via pci_enum_do */

START_TEST(test_enum_do_full)
{
    struct test_pci_topology t;
    int br, ep;
    uint32_t bar_val;
    uint8_t sec_bus;
    int ret;

    test_pci_init(&t);
    /* Bridge at 0:1.0 */
    br = test_pci_add_bridge(&t, 1, 0, 0x1234, 0x0002, TEST_PCI_ROOT_BUS);
    /* Endpoint behind bridge */
    ep = test_pci_add_dev(&t, 0, 0, 0x1234, 0x0003, br);
    test_pci_dev_set_bar(&t, ep, 0, 0x10000, TEST_PCI_BAR_MMIO); /* 64KB MMIO */
    test_pci_commit(&t);

    ret = pci_enum_do();
    ck_assert_int_eq(ret, 0);

    /* Bridge is on the root bus */
    ck_assert_uint_eq(pci_config_read8(0, 1, 0, PCI_PRIMARY_BUS), 0);

    /* Bridge should have bus numbers assigned */
    sec_bus = pci_config_read8(0, 1, 0, PCI_SECONDARY_BUS);
    ck_assert_uint_ne(sec_bus, 0);

    /* No other bridge behind this one */
    ck_assert_uint_ge(pci_config_read8(0, 1, 0, PCI_SUB_SEC_BUS), sec_bus);

    /* Endpoint BAR should be programmed */
    bar_val = pci_config_read32(sec_bus, 0, 0, PCI_BAR0_OFFSET);
    ck_assert_uint_ne(bar_val, 0);
    ck_assert_uint_ne(bar_val, 0xFFFFFFFF);

    /* TODO: check bridge windows */

    test_pci_cleanup(&t);
}
END_TEST

/* test_enum_do_nested_bridges: end-to-end nested bridge enumeration */

START_TEST(test_enum_do_nested_bridges)
{
    struct test_pci_topology t;
    int brA, brB, ep;
    uint32_t bar_val;
    uint8_t secA, subA, secB, subB;
    int ret;

    test_pci_init(&t);
    brA = test_pci_add_bridge(&t, 1, 0, 0x1111, 0x2222, TEST_PCI_ROOT_BUS);
    brB = test_pci_add_bridge(&t, 0, 0, 0x3333, 0x4444, brA);
    ep  = test_pci_add_dev(&t, 0, 0, 0x5555, 0x6666, brB);
    test_pci_dev_set_bar(&t, ep, 0, 0x10000, TEST_PCI_BAR_MMIO);
    test_pci_commit(&t);

    ret = pci_enum_do();
    ck_assert_int_eq(ret, 0);

    /* Bridge A: primary=0, secondary assigned, subordinate >= secondary */
    ck_assert_uint_eq(pci_config_read8(0, 1, 0, PCI_PRIMARY_BUS), 0);
    secA = pci_config_read8(0, 1, 0, PCI_SECONDARY_BUS);
    subA = pci_config_read8(0, 1, 0, PCI_SUB_SEC_BUS);
    ck_assert_uint_ne(secA, 0);
    ck_assert_uint_ge(subA, secA);

    /* Bridge B: primary=secA, secondary assigned > secA, subordinate >= secB */
    ck_assert_uint_eq(pci_config_read8(secA, 0, 0, PCI_PRIMARY_BUS), secA);
    secB = pci_config_read8(secA, 0, 0, PCI_SECONDARY_BUS);
    subB = pci_config_read8(secA, 0, 0, PCI_SUB_SEC_BUS);
    ck_assert_uint_gt(secB, secA);
    ck_assert_uint_ge(subB, secB);

    /* Bridge A subordinate must cover bridge B's range */
    ck_assert_uint_ge(subA, subB);

    /* Endpoint BAR on bus secB should be programmed */
    bar_val = pci_config_read32(secB, 0, 0, PCI_BAR0_OFFSET);
    ck_assert_uint_ne(bar_val, 0);
    ck_assert_uint_ne(bar_val, 0xFFFFFFFF);

    test_pci_cleanup(&t);
}
END_TEST

/* test_config_rw_8bit_all_positions: read8/write8 at all byte offsets */

START_TEST(test_config_rw_8bit_all_positions)
{
    struct test_pci_topology t;
    int dev_node;
    uint32_t base_off;
    int i;

    test_pci_init(&t);
    dev_node = test_pci_add_dev(&t, 0, 0, 0x1234, 0x5678, TEST_PCI_ROOT_BUS);
    test_pci_commit(&t);

    /* Use an offset in cfg space that won't conflict with vendor/device ID.
     * Write distinct values at each byte position within a dword. */
    base_off = 0x40; /* arbitrary config space offset */

    /* Zero the dword first */
    pci_config_write32(0, 0, 0, base_off, 0x00000000);

    for (i = 0; i < 4; i++) {
        uint8_t write_val = 0x10 * (i + 1); /* 0x10, 0x20, 0x30, 0x40 */
        uint8_t read_val;

        pci_config_write8(0, 0, 0, base_off + i, write_val);
        read_val = pci_config_read8(0, 0, 0, base_off + i);
        ck_assert_uint_eq(read_val, write_val);
    }

    /* Verify the full dword has all four bytes */
    {
        uint32_t full;
        full = pci_config_read32(0, 0, 0, base_off);
        ck_assert_uint_eq(full, 0x40302010);
    }

    test_pci_cleanup(&t);
}
END_TEST

/* test_enum_next_aligned_overflow: edge cases for alignment helper */

START_TEST(test_enum_next_aligned_overflow)
{
    uint32_t next;
    int ret;

    /* Already aligned: should return same address */
    ret = pci_enum_next_aligned32(0x80000000, &next, 0x1000, 0xFFFFFFFF);
    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(next, 0x80000000);

    /* Not aligned: should round up */
    ret = pci_enum_next_aligned32(0x80000001, &next, 0x1000, 0xFFFFFFFF);
    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(next, 0x80001000);

    /* Aligned result just below limit: should succeed */
    ret = pci_enum_next_aligned32(0x80000000, &next, 0x1000, 0x80001000);
    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(next, 0x80000000);

    /* addr >= limit: aligned address equals limit exactly */
    next = 0xDEAD;
    ret = pci_enum_next_aligned32(0x80000000, &next, 0x1000, 0x80000000);
    ck_assert_int_ne(ret, 0);
    ck_assert_uint_eq(next, 0xDEAD); /* *next unchanged on failure */

    /* addr >= limit: alignment pushes past limit */
    next = 0xDEAD;
    ret = pci_enum_next_aligned32(0x80000001, &next, 0x100000, 0x80100000);
    ck_assert_int_ne(ret, 0);
    ck_assert_uint_eq(next, 0xDEAD);

    /* addr > 0xFFFFFFFF: alignment overshoots 32-bit range
     * On 64-bit host: (0xFFFFF001 + 0xFFF) & ~0xFFF = 0x100000000
     * which is > 0xFFFFFFFF */
    next = 0xDEAD;
    ret = pci_enum_next_aligned32(0xFFFFF001, &next, 0x1000, 0xFFFFFFFF);
    ck_assert_int_ne(ret, 0);
    ck_assert_uint_eq(next, 0xDEAD);

    /* Larger alignment near top of address space */
    next = 0xDEAD;
    ret = pci_enum_next_aligned32(0xFF000001, &next, 0x1000000, 0xFFFFFFFF);
    ck_assert_int_ne(ret, 0);
    ck_assert_uint_eq(next, 0xDEAD);

    /* address = 0xFFFFFFFF with any alignment: always fails */
    next = 0xDEAD;
    ret = pci_enum_next_aligned32(0xFFFFFFFF, &next, 0x1000, 0xFFFFFFFF);
    ck_assert_int_ne(ret, 0);
    ck_assert_uint_eq(next, 0xDEAD);

    /* address = 0, align = 0x1000: trivial success */
    ret = pci_enum_next_aligned32(0x0, &next, 0x1000, 0xFFFFFFFF);
    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(next, 0x0);

    /* Large alignment, address well below: rounds up correctly */
    ret = pci_enum_next_aligned32(0x00100001, &next, 0x100000, 0xFFFFFFFF);
    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(next, 0x00200000);

    /* Exact top of 32-bit range that's still valid:
     * 0xFFFFF000 aligned to 0x1000 with limit > that → succeeds */
    ret = pci_enum_next_aligned32(0xFFFFF000, &next, 0x1000, 0xFFFFFFFF);
    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(next, 0xFFFFF000);
}
END_TEST

/* test_pci_align_check_up_overflow: edge cases for pci_align_check_up */
START_TEST(test_pci_align_check_up_overflow)
{
    uint32_t aligned;
    int ret;

    /* Normal case: already aligned */
    ret = pci_align_check_up(0x80000000, 0x100000, 0x90000000, &aligned);
    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(aligned, 0x80000000);

    /* Normal case: needs alignment */
    ret = pci_align_check_up(0x80000001, 0x100000, 0x90000000, &aligned);
    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(aligned, 0x80100000);

    /* At limit: aligned == limit → fail */
    ret = pci_align_check_up(0x80000000, 0x100000, 0x80000000, &aligned);
    ck_assert_int_ne(ret, 0);

    /* Exceeds limit after alignment */
    ret = pci_align_check_up(0x80000001, 0x100000, 0x80100000, &aligned);
    ck_assert_int_ne(ret, 0);

    /* Overflow: address near 0xFFFFFFFF, align_up wraps to 0 */
    ret = pci_align_check_up(0xFFF00001, 0x100000, 0xFFFFFFFF, &aligned);
    ck_assert_int_ne(ret, 0);

    /* Overflow: address is 0xFFFFFFFF */
    ret = pci_align_check_up(0xFFFFFFFF, 0x1000, 0xFFFFFFFF, &aligned);
    ck_assert_int_ne(ret, 0);

    /* Just below limit: should succeed */
    ret = pci_align_check_up(0x80000000, 0x100000, 0x80000001, &aligned);
    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(aligned, 0x80000000);
}
END_TEST

/*
 * Suite registration
 */

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-pci");

    TCase *tc_build = tcase_create("topology-build");
    tcase_add_test(tc_build, test_topology_build);
    suite_add_tcase(s, tc_build);

    TCase *tc_commit = tcase_create("topology-commit");
    tcase_add_test(tc_commit, test_topology_commit);
    suite_add_tcase(s, tc_commit);

    TCase *tc_find_root = tcase_create("find-node-root-bus");
    tcase_add_test(tc_find_root, test_find_node_root_bus);
    suite_add_tcase(s, tc_find_root);

    TCase *tc_find_bridge = tcase_create("find-node-behind-bridge");
    tcase_add_test(tc_find_bridge, test_find_node_behind_bridge);
    suite_add_tcase(s, tc_find_bridge);

    TCase *tc_find_nested = tcase_create("find-node-nested-bridges");
    tcase_add_test(tc_find_nested, test_find_node_nested_bridges);
    suite_add_tcase(s, tc_find_nested);

    TCase *tc_bar_mask = tcase_create("bar-probe-mask");
    tcase_add_test(tc_bar_mask, test_bar_probe_mask);
    suite_add_tcase(s, tc_bar_mask);

    TCase *tc_mock_probe = tcase_create("mmio-mock-bar-probe");
    tcase_add_test(tc_mock_probe, test_mmio_mock_bar_probe);
    suite_add_tcase(s, tc_mock_probe);

    TCase *tc_mock_unreach = tcase_create("mmio-mock-unreachable");
    tcase_add_test(tc_mock_unreach, test_mmio_mock_unreachable);
    suite_add_tcase(s, tc_mock_unreach);

    TCase *tc_bar64 = tcase_create("program-bar-64bit");
    tcase_add_test(tc_bar64, test_pci_program_bar_64bit);
    suite_add_tcase(s, tc_bar64);

    TCase *tc_bar_restore = tcase_create("program-bar-restore");
    tcase_add_test(tc_bar_restore, test_pci_program_bar_restore);
    suite_add_tcase(s, tc_bar_restore);

    TCase *tc_bar_types = tcase_create("program-bar-types");
    tcase_add_test(tc_bar_types, test_program_bar_types);
    suite_add_tcase(s, tc_bar_types);

    TCase *tc_bar_oor = tcase_create("program-bar-out-of-range");
    tcase_add_test(tc_bar_oor, test_program_bar_out_of_range);
    suite_add_tcase(s, tc_bar_oor);

    TCase *tc_bar_ureject = tcase_create("program-bar-64bit-upper-reject");
    tcase_add_test(tc_bar_ureject, test_program_bar_64bit_upper_reject);
    suite_add_tcase(s, tc_bar_ureject);

    TCase *tc_bar_nospace = tcase_create("program-bar-no-space");
    tcase_add_test(tc_bar_nospace, test_program_bar_no_space);
    suite_add_tcase(s, tc_bar_nospace);

    TCase *tc_bars_iter = tcase_create("program-bars-iteration");
    tcase_add_test(tc_bars_iter, test_program_bars_iteration);
    suite_add_tcase(s, tc_bars_iter);

    TCase *tc_bridge = tcase_create("program-bridge");
    tcase_add_test(tc_bridge, test_program_bridge);
    suite_add_tcase(s, tc_bridge);

    TCase *tc_oom_init = tcase_create("bridge-oom-initial");
    tcase_add_test(tc_oom_init, test_program_bridge_oom_initial);
    suite_add_tcase(s, tc_oom_init);

    TCase *tc_oom_post = tcase_create("bridge-oom-post-enum");
    tcase_add_test(tc_oom_post, test_program_bridge_oom_post_enum);
    suite_add_tcase(s, tc_oom_post);

    TCase *tc_enum_topo = tcase_create("enum-bus-topology");
    tcase_add_test(tc_enum_topo, test_enum_bus_topology);
    suite_add_tcase(s, tc_enum_topo);

    TCase *tc_enum_do = tcase_create("enum-do-full");
    tcase_add_test(tc_enum_do, test_enum_do_full);
    suite_add_tcase(s, tc_enum_do);

    TCase *tc_enum_nested = tcase_create("enum-do-nested-bridges");
    tcase_add_test(tc_enum_nested, test_enum_do_nested_bridges);
    suite_add_tcase(s, tc_enum_nested);

    TCase *tc_rw8 = tcase_create("config-rw-8bit-positions");
    tcase_add_test(tc_rw8, test_config_rw_8bit_all_positions);
    suite_add_tcase(s, tc_rw8);

    TCase *tc_align = tcase_create("enum-next-aligned-overflow");
    tcase_add_test(tc_align, test_enum_next_aligned_overflow);
    suite_add_tcase(s, tc_align);

    TCase *tc_align_check = tcase_create("align-check-up-overflow");
    tcase_add_test(tc_align_check, test_pci_align_check_up_overflow);
    suite_add_tcase(s, tc_align_check);

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
