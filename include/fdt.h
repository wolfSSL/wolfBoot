/* fdt.h
 *
 * Functions to help with flattened device tree (DTB) parsing
 *
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
 */

#ifndef FDT_H
#define FDT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define FDT_MAGIC     0xD00DFEEDUL
#define FDT_SW_MAGIC  (uint32_t)(~FDT_MAGIC) /* marker for run-time creation/edit of FDT */

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

struct fdt_reserve_entry {
    uint64_t address;
    uint64_t size;
};

struct fdt_prop {
    uint32_t len;
    uint32_t nameoff;
};

struct fdt_node_header {
    uint32_t tag;
    char name[0];
};

struct fdt_property {
    uint32_t tag;
    uint32_t len;
    uint32_t nameoff;
    char data[0];
};

#define FDT_TAGSIZE     sizeof(uint32_t)
#define FDT_ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define FDT_TAGALIGN(x) (FDT_ALIGN((x), FDT_TAGSIZE))

#define FDT_FIRST_SUPPORTED_VERSION 0x10
#define FDT_LAST_SUPPORTED_VERSION  0x11

#define FDT_BEGIN_NODE 0x00000001UL
#define FDT_END_NODE   0x00000002UL
#define FDT_PROP       0x00000003UL
#define FDT_NOP        0x00000004UL
#define FDT_END        0x00000009UL

#define FDT_ERR_BADMAGIC     1
#define FDT_ERR_BADVERSION   2
#define FDT_ERR_BADSTRUCTURE 3
#define FDT_ERR_BADOFFSET    4
#define FDT_ERR_BADSTATE     5
#define FDT_ERR_NOTFOUND     6
#define FDT_ERR_NOSPACE      7
#define FDT_ERR_TRUNCATED    8
#define FDT_ERR_INTERNAL     9
#define FDT_ERR_EXISTS       10

#define FDT_PCI_PREFETCH    (0x40000000)
#define FDT_PCI_MEM32       (0x02000000)
#define FDT_PCI_IO          (0x01000000)
#define FDT_PCI_MEM64       (0x03000000)

uint32_t cpu_to_fdt32(uint32_t x);
uint64_t cpu_to_fdt64(uint64_t x);
uint32_t fdt32_to_cpu(uint32_t x);
uint64_t fdt64_to_cpu(uint64_t x);

#define fdt_get_header(fdt, field) (fdt32_to_cpu(((const struct fdt_header *)(fdt))->field))
#define fdt_magic(fdt)             (fdt_get_header(fdt, magic))
#define fdt_totalsize(fdt)         (fdt_get_header(fdt, totalsize))
#define fdt_off_dt_struct(fdt)     (fdt_get_header(fdt, off_dt_struct))
#define fdt_off_dt_strings(fdt)    (fdt_get_header(fdt, off_dt_strings))
#define fdt_off_mem_rsvmap(fdt)    (fdt_get_header(fdt, off_mem_rsvmap))
#define fdt_version(fdt)           (fdt_get_header(fdt, version))
#define fdt_last_comp_version(fdt) (fdt_get_header(fdt, last_comp_version))
#define fdt_boot_cpuid_phys(fdt)   (fdt_get_header(fdt, boot_cpuid_phys))
#define fdt_size_dt_strings(fdt)   (fdt_get_header(fdt, size_dt_strings))
#define fdt_size_dt_struct(fdt)    (fdt_get_header(fdt, size_dt_struct))

#define fdt_set_header(fdt, field, val)     (((struct fdt_header *)fdt)->field = cpu_to_fdt32(val))
#define fdt_set_magic(fdt, val)             (fdt_set_header(fdt, magic, (val)))
#define fdt_set_totalsize(fdt, val)         (fdt_set_header(fdt, totalsize, (val)))
#define fdt_set_off_dt_struct(fdt, val)     (fdt_set_header(fdt, off_dt_struct, (val)))
#define fdt_set_off_dt_strings(fdt, val)    (fdt_set_header(fdt, off_dt_strings, (val)))
#define fdt_set_off_mem_rsvmap(fdt, val)    (fdt_set_header(fdt, off_mem_rsvmap, (val)))
#define fdt_set_version(fdt, val)           (fdt_set_header(fdt, version, (val)))
#define fdt_set_last_comp_version(fdt, val) (fdt_set_header(fdt, last_comp_version, (val)))
#define fdt_set_boot_cpuid_phys(fdt, val)   (fdt_set_header(fdt, boot_cpuid_phys, (val)))
#define fdt_set_size_dt_strings(fdt, val)   (fdt_set_header(fdt, size_dt_strings, (val)))
#define fdt_set_size_dt_struct(fdt, val)    (fdt_set_header(fdt, size_dt_struct, (val)))

int fdt_check_header(const void *fdt);
int fdt_next_node(const void *fdt, int offset, int *depth);
int fdt_first_property_offset(const void *fdt, int nodeoffset);
int fdt_next_property_offset(const void *fdt, int offset);
const struct fdt_property *fdt_get_property_by_offset(const void *fdt, int offset, int *lenp);

const char* fdt_get_name(const void *fdt, int nodeoffset, int *len);
const char* fdt_get_string(const void *fdt, int stroffset, int *lenp);

const void *fdt_getprop(const void *fdt, int nodeoffset, const char *name, int *lenp);
int fdt_setprop(void *fdt, int nodeoffset, const char *name, const void *val, int len);

int fdt_find_devtype(void* fdt, int startoff, const char* node);
int fdt_node_check_compatible(const void *fdt, int nodeoffset, const char *compatible);
int fdt_node_offset_by_compatible(const void *fdt, int startoffset, const char *compatible);
int fdt_add_subnode(void* fdt, int parentoff, const char* name);

/* helpers to fix/append a property to a node */
int fdt_fixup_str(void* fdt, int off, const char* node, const char* name, const char* str);
int fdt_fixup_val(void* fdt, int off, const char* node, const char* name, uint32_t val);
int fdt_fixup_val64(void* fdt, int off, const char* node, const char* name, uint64_t val);

int fdt_shrink(void* fdt);

#ifdef __cplusplus
}
#endif

#endif /* !FDT_H */
