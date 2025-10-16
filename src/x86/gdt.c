/* gdt.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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
 *
 */
#include <stdint.h>
#include <x86/gdt.h>

struct segment_descriptor {
    uint16_t seg_limit_15_0;
    uint16_t base_addr_15_0;
    uint8_t base_addr_23_16;
    uint8_t type_s_dpl_p;
    uint8_t seg_limt_19_16_avl_flags;
    uint8_t base_addr_31_24;
} __attribute__((packed));

struct gdtr_32 {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct gdtr_64 {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

#define SEGMENT_DESCRIPTOR_INIT(base, limit, type, s, dpl, p, avl, l, db, g) { \
    .seg_limit_15_0 = (limit) & 0xffff, \
    .base_addr_15_0 = (base) & 0xffff, \
    .base_addr_23_16 = (((base) >> 16) & 0xff), \
    .type_s_dpl_p = ((type & 0xf) | ((s & 0x1) << 4) | ((dpl & 0x3) << 5) |  ((p & 0x1) << 7)), \
    .seg_limt_19_16_avl_flags = ((((limit) >> 16) & 0xf) | ((avl & 0x1) << 4) | ((l & 0x1) << 5) | ((db & 0x1) << 6) | ((g & 0x1) << 7)), \
    .base_addr_31_24 = ((base) >> 24) & 0xff, \
}

#define GDT_NUM_ENTRIES 5

struct segment_descriptor gdt[GDT_NUM_ENTRIES] =  {
    /* NULL */
    SEGMENT_DESCRIPTOR_INIT(0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    /* Data */
    SEGMENT_DESCRIPTOR_INIT(0, 0xffffffff, 0x3, 1, 0, 1, 0, 0, 1, 1),
    /* Code (32bit) */
    SEGMENT_DESCRIPTOR_INIT(0, 0xffffffff, 0xb, 1, 0, 1, 0, 0, 1, 1),
    /* Code (64bit) */
    SEGMENT_DESCRIPTOR_INIT(0, 0xffffffff, 0xb, 1, 0, 1, 0, 1, 0, 1),
    /* Code (64bit) compat mode */
    SEGMENT_DESCRIPTOR_INIT(0, 0xffffffff, 0xb, 1, 0, 1, 0, 0, 1, 1),
};

int gdt_setup_table(void)
{
    struct gdtr_64 gdtr;
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint64_t)(uintptr_t)gdt;
    __asm__ volatile ("lgdt %0" : : "m" (gdtr));
    return 0;
}

int gdt_update_segments(void)
{
    __asm__ volatile (
        "mov %0, %%ax\r\n"
        "mov %%ax, %%ds\r\n"
        "mov %%ax, %%es\r\n"
        "mov %%ax, %%fs\r\n"
        "mov %%ax, %%gs\r\n"
        "mov %%ax, %%ss\r\n"
        "push %1\r\n"
        "lea (seg_cs), %%rax\r\n"
        "push %%rax\r\n"
        "retfq\r\n"
        "seg_cs:\r\n"
        :
        : "i"(GDT_DS), "i" (GDT_CS_64BIT) 
        : "rax"
    );
    return 0;
}
