/* elf.c
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
 *
 * Simple elf32 or elf64 loader support
 */

#ifdef WOLFBOOT_ELF

#include "wolfboot/wolfboot.h"
#include "printf.h"
#include "string.h"
#include "elf.h"
#include "hal.h"

#ifdef ARCH_PPC
#include "hal/nxp_ppc.h"
#endif

#ifdef ELF_SCATTERED
#include "image.h"
#endif

/* support for elf parsing debug printf */
#if defined(DEBUG) || defined(ELF_PARSER)
#if defined(DEBUG_ELF) && DEBUG_ELF == 0
#undef DEBUG_ELF
#else
#undef  DEBUG_ELF
#define DEBUG_ELF
#endif
#endif


#if defined(MMU) || defined (WOLFBOOT_FSP) || defined (ARCH_PPC)
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
    wolfBoot_printf("Loading elf at %p\r\n", (void*)image);
#endif

    /* Verify ELF header */
    if (memcmp(h32->ident, ELF_IDENT_STR, 4) != 0) {
        return -1; /* not valid header identifier */
    }

    /* Load class and endianess */
    is_elf32 = (h32->ident[4] == ELF_CLASS_32);
    is_le = (h32->ident[5] == ELF_ENDIAN_LITTLE);
    (void)is_le;

    /* Verify this is an executable */
    if (GET_H16(type) != ELF_HET_EXEC) {
        return -2; /* not executable */
    }

#ifdef DEBUG_ELF
    wolfBoot_printf("Found valid elf%d (%s endian)\r\n",
        is_elf32 ? 32 : 64, is_le ? "little" : "big");
#endif

    /* programs */
    entry_off = image + GET_H32(ph_offset);
    entry_size = GET_H16(ph_entry_size);
    entry_count = GET_H16(ph_entry_count);
#ifdef DEBUG_ELF
    wolfBoot_printf("Program Headers %d (size %d)\r\n", entry_count, entry_size);
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
                "Load %u bytes (offset %p) to %p (p %p)\r\n",
                (uint32_t)mem_size, (void*)offset, (void*)vaddr, (void*)paddr);
        }
        if (mem_size > file_size) {
            wolfBoot_printf(
                "Clear %u bytes at %p (p %p)\r\n",
                (uint32_t)(mem_size - file_size), (void*)vaddr, (void*)paddr);
        }
#endif

#ifndef ELF_PARSER
        if (mmu_cb != NULL) {
            if (mmu_cb(vaddr, paddr, mem_size) != 0) {
#ifdef DEBUG_ELF
            wolfBoot_printf(
                "Fail to map %u bytes to %p (p %p)\r\n",
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
    wolfBoot_printf("Entry point %p\r\n", (void*)*entry);
#endif

    return 0;
}
#endif /* MMU */

int elf_open(const unsigned char *ehdr, int *is_elf32)
{
    const unsigned char *ident = ehdr;
    /* Verify ELF header */
    if (memcmp(ident, ELF_IDENT_STR, 4) != 0) {
        return -1; /* not valid header identifier */
    }
    wolfBoot_printf("ELF image found\n");
    *is_elf32 = !!(ident[ELF_CLASS_OFF] == ELF_CLASS_32);
    return 0;

}

int elf_hdr_size(const unsigned char *ehdr)
{
    int sz = 0;
    int is_elf32;
    if (elf_open(ehdr, &is_elf32) != 0)
        return -1;
    if (is_elf32) {
        const elf32_header *elf32_hdr = (const elf32_header *)ehdr;
        sz = sizeof(elf32_header);
        sz += elf32_hdr->ph_entry_count * sizeof(elf32_program_header);
    } else {
        const elf64_header *elf64_hdr = (const elf64_header *)ehdr;
        sz = sizeof(elf64_header);
        sz += elf64_hdr->ph_entry_count * sizeof(elf64_program_header);
    }
    return sz;
}
#if !defined(MMU) && !defined(WOLFBOOT_FSP) && !defined(ARCH_PPC)
int elf_store_image_scattered(const unsigned char *hdr, unsigned long *entry_out, int ext_flash) {
    const unsigned char *image;
    int is_elf32;
    unsigned short entry_count;
    unsigned short entry_size;
    unsigned long entry_off;
    int i;
    image = hdr + IMAGE_HEADER_SIZE;
    if (elf_open(image, &is_elf32) != 0) {
        return -1;
    }
    if (is_elf32) {
        const elf32_header *eh;
        const elf32_program_header *ph;
        wolfBoot_printf("ELF image is 32 bit\n");

        eh = (const elf32_header *)image;
        entry_count = eh->ph_entry_count;
        entry_size = eh->ph_entry_size;
        entry_off = eh->ph_offset;
        *entry_out = (unsigned long)eh->entry;

        ph = (const elf32_program_header *)(image + entry_off);
        for (i = 0; i < entry_count; ++i) {
            unsigned long paddr;
            unsigned long filesz;
            unsigned long offset;

            if (ph[i].type != ELF_PT_LOAD)
                continue;
            paddr = (unsigned long)ph[i].paddr;
            offset = (unsigned long)ph[i].offset;
            filesz = (unsigned long)ph[i].file_size;
            printf("Writing section at address %lx offset %lx\n", paddr, offset);
#ifdef EXT_FLASH
            if (ext_flash) {
                ext_flash_unlock();
                ext_flash_erase(paddr + ARCH_FLASH_OFFSET, filesz);
                ext_flash_write(paddr + ARCH_FLASH_OFFSET, image + offset, filesz);
                ext_flash_lock();
            }
            else
#endif
            {
                hal_flash_unlock();
                hal_flash_erase(paddr + ARCH_FLASH_OFFSET, filesz);
                hal_flash_write(paddr + ARCH_FLASH_OFFSET, image + offset, filesz);
                hal_flash_lock();
            }
        }
    } else { /* 32 bit ELF */
        const elf64_header *eh;
        const elf64_program_header *ph;
        wolfBoot_printf("ELF image is 64 bit\n");

        eh = (const elf64_header *)image;
        entry_count = eh->ph_entry_count;
        entry_size = eh->ph_entry_size;
        entry_off = eh->ph_offset;
        *entry_out = (unsigned long)eh->entry;

        ph = (const elf64_program_header *)(image + entry_off);
        for (i = 0; i < entry_count; ++i) {
            unsigned long paddr;
            unsigned long filesz;
            unsigned long offset;

            if (ph[i].type != ELF_PT_LOAD)
                continue;
            paddr = (unsigned long)ph[i].paddr;
            offset = (unsigned long)ph[i].offset;
            filesz = (unsigned long)ph[i].file_size;
            printf("Writing section at address %lx offset %lx\n", paddr, offset);
#ifdef EXT_FLASH
            if (ext_flash) {
                ext_flash_unlock();
                ext_flash_erase(paddr + ARCH_FLASH_OFFSET, filesz);
                ext_flash_write(paddr + ARCH_FLASH_OFFSET, image + offset, filesz);
                ext_flash_lock();
            }
            else
#endif
            {
                hal_flash_unlock();
                hal_flash_erase(paddr + ARCH_FLASH_OFFSET, filesz);
                hal_flash_write(paddr + ARCH_FLASH_OFFSET, image + offset, filesz);
                hal_flash_lock();
            }
        }
    }
    return 0;
}
#endif


int elf_load_image(uint8_t *image, uintptr_t *entry, int ext_flash)
{
#ifdef MMU
    return elf_load_image_mmu(image, entry, NULL);
#else
    return elf_store_image_scattered(image, (unsigned long *)entry, ext_flash);
#endif
}

#endif /* WOLFBOOT_ELF */
