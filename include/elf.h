/* elf.h
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

#ifndef _WOLFBOOT_ELF_H
#define _WOLFBOOT_ELF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* header ident[0-3] */
#define ELF_IDENT_STR     "\x7F""ELF"

/* header ident[4] */
#define ELF_CLASS_32      (1)
#define ELF_CLASS_64      (2)

/* header ident[5] */
#define ELF_ENDIAN_LITTLE (1)
#define ELF_ENDIAN_BIG    (2)

/* header type */
#define ELF_HET_EXEC      (2)

/* section header type */
#define ELF_SHT_PROGBITS  (1)
#define ELF_SHT_STRTAB    (3)
#define ELF_SHT_NOBITS    (8)
/* section flags */
#define ELF_SHF_ALLOC     (0x2)

/* program header type */
#define ELF_PT_LOAD       (0x1)

typedef struct elf32_header {
    uint8_t  ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t ph_offset;
    uint32_t sh_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t ph_entry_size;
    uint16_t ph_entry_count;
    uint16_t sh_entry_size;
    uint16_t sh_entry_count;
    uint16_t sh_str_index;
} elf32_header;

typedef struct elf32_section_header {
    uint32_t name;
    uint32_t type;
    uint32_t flags;
    uint32_t addr;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    uint32_t addr_align;
    uint32_t entry_size;
} elf32_section_header;

typedef struct elf32_program_header {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t file_size;
    uint32_t mem_size;
    uint32_t flags;
    uint32_t align;
} elf32_program_header;


typedef struct elf64_header {
    uint8_t  ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t ph_offset;
    uint64_t sh_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t ph_entry_size;
    uint16_t ph_entry_count;
    uint16_t sh_entry_size;
    uint16_t sh_entry_count;
    uint16_t sh_str_index;
} elf64_header;

typedef struct elf64_section_header {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t addr;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t addr_align;
    uint64_t entry_size;
} elf64_section_header;

typedef struct elf64_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t file_size;
    uint64_t mem_size;
    uint64_t align;
} elf64_program_header;


typedef int (*elf_mmu_map_cb)(uint64_t, uint64_t, uint32_t);
int elf_load_image_mmu(uint8_t *image, uintptr_t *entry, elf_mmu_map_cb mmu_cb);
int elf_load_image(uint8_t *image, uintptr_t *entry);


#ifdef __cplusplus
}
#endif

#endif /* _WOLFBOOT_ELF_H */
