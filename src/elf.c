/* elf.c
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
 *
 * Simple elf32 or elf64 loader support
 */

#ifdef WOLFBOOT_ELF

#include "wolfboot/wolfboot.h"
#include "printf.h"
#include "string.h"
#include "elf.h"

#ifdef ARCH_PPC
#include "hal/nxp_ppc.h"
#endif

/* support for elf parsing debug printf */
#if defined(DEBUG) || defined(ELF_PARSER)
#if DEBUG_ELF == 0
#undef DEBUG_ELF
#endif
#endif

/* support byte swapping if testing/reading an elf with different endianess */
#if defined(ELF_PARSER) || defined(ELF_ENDIAN_SUPPORT)
  #ifdef BIG_ENDIAN_ORDER
    #define GET16(x) (( is_le) ? __builtin_bswap16(x) : (x))
    #define GET32(x) (( is_le) ? __builtin_bswap32(x) : (x))
    #define GET64(x) (( is_le) ? __builtin_bswap64(x) : (x))
  #else
    #define GET16(x) ((!is_le) ? __builtin_bswap16(x) : (x))
    #define GET32(x) ((!is_le) ? __builtin_bswap32(x) : (x))
    #define GET64(x) ((!is_le) ? __builtin_bswap64(x) : (x))
  #endif
#else
    #define GET16(x) (x)
    #define GET32(x) (x)
    #define GET64(x) (x)
#endif

#define GET_H64(name) (is_elf32 ? GET32(h32->name) : GET64(h64->name))
#define GET_H32(name) (is_elf32 ? GET32(h32->name) : GET32(h64->name))
#define GET_H16(name) (is_elf32 ? GET16(h32->name) : GET16(h64->name))
#define GET_E64(name) (is_elf32 ? GET32(e32->name) : GET64(e64->name))
#define GET_E32(name) (is_elf32 ? GET32(e32->name) : GET32(e64->name))

/* Loader for elf32 or elf64 format program headers
 * Returns the entry point function
 */
int elf_load_image_mmu(uint8_t *image, uintptr_t *entry, elf_mmu_map_cb mmu_cb)
{
    elf32_header* h32 = (elf32_header*)image;
    elf64_header* h64 = (elf64_header*)image;
    uint16_t entry_count, entry_size;
    uint8_t *entry_off;
    int is_elf32, is_le, i;

#ifdef DEBUG_ELF
    wolfBoot_printf("Loading elf at %p\n", (void*)image);
#endif

    /* Verify ELF header */
    if (memcmp(h32->ident, ELF_IDENT_STR, 4) != 0) {
        return -1; /* not valid header identifier */
    }

    /* Load class and endianess */
    is_elf32 = (h32->ident[4] == ELF_CLASS_32);
    is_le = (h32->ident[5] == ELF_ENDIAN_LITTLE);

    /* Verify this is an executable */
    if (GET_H16(type) != ELF_HET_EXEC) {
        return -2; /* not executable */
    }

#ifdef DEBUG_ELF
    wolfBoot_printf("Found valid elf%d (%s endian)\n",
        is_elf32 ? 32 : 64, is_le ? "little" : "big");
#endif

    /* programs */
    entry_off = image + GET_H32(ph_offset);
    entry_size = GET_H16(ph_entry_size);
    entry_count = GET_H16(ph_entry_count);
#ifdef DEBUG_ELF
    wolfBoot_printf("Program Headers %d (size %d)\n", entry_count, entry_size);
#endif
    for (i = 0; i < entry_count; i++) {
        uint8_t *ptr = ((uint8_t*)entry_off) + (i * entry_size);
        elf32_program_header* e32 = (elf32_program_header*)ptr;
        elf64_program_header* e64 = (elf64_program_header*)ptr;
        uint32_t type = GET_E32(type);
        uintptr_t paddr = GET_E64(paddr);
        uintptr_t vaddr = GET_E64(vaddr);
        uintptr_t mem_size = GET_E64(mem_size);
        uintptr_t offset = GET_E64(offset);
        uintptr_t file_size = GET_E64(file_size);

        if (type != ELF_PT_LOAD || mem_size == 0) {
            continue;
        }

#ifdef DEBUG_ELF
        if (file_size > 0) {
            wolfBoot_printf(
                "\tLoad %u bytes (offset %p) to %p (p %p)\n",
                (uint32_t)mem_size, (void*)offset, (void*)vaddr, (void*)paddr);
        }
        if (mem_size > file_size) {
            wolfBoot_printf(
                "\tClear %u bytes at %p (p %p)\n",
                (uint32_t)(mem_size - file_size), (void*)vaddr, (void*)paddr);
        }
#endif

#ifndef ELF_PARSER
        if (mmu_cb != NULL) {
            if (mmu_cb(vaddr, paddr, mem_size) != 0) {
#ifdef DEBUG_ELF
            wolfBoot_printf(
                "\tFail to map %u bytes to %p (p %p)\n",
                (uint32_t)mem_size, (void*)vaddr, (void*)paddr);
#endif
            continue;
            }
        }

        memcpy((void*)(uintptr_t)vaddr, image + offset, file_size);
        if (mem_size > file_size) {
            memset((void*)(uintptr_t)(vaddr + file_size), 0,
                   mem_size - file_size);
        }
    #ifdef ARCH_PPC
        flush_cache(paddr, mem_size);
    #endif
#endif
    }

    *entry = GET_H64(entry);
#ifdef DEBUG_ELF
    wolfBoot_printf("Entry point %p\n", (void*)*entry);
#endif

    return 0;
}

int elf_load_image(uint8_t *image, uintptr_t *entry)
{

    return elf_load_image_mmu(image, entry, NULL);
}

#endif /* WOLFBOOT_ELF */
