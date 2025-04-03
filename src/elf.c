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

/* support for elf parsing debug printf */
#if defined(DEBUG) || defined(ELF_PARSER)
#if defined(DEBUG_ELF) && DEBUG_ELF == 0
#undef DEBUG_ELF
#else
#undef  DEBUG_ELF
#define DEBUG_ELF
#endif
#endif


#ifdef MMU
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

#if 0
/**
 * @brief Compute the scattered hash by hashing PT_LOAD segments at their XIP
 * addresses. Note: This function assumes that the destination addresses for elf
 * loading have the same access patterns as the memory represented by img.
 * (e.g. if BOOT partition is external, then reads/writes to the load address
 * will use ext_flash_read/ext_flash_write.
 *
 * @param img Pointer to the wolfBoot image
 * @param hash Buffer to store the computed hash (must be at least
 * WOLFBOOT_SHA_DIGEST_SIZE bytes)
 * @return 0 on success, negative value on error
 */
static int wolfBoot_compute_scattered_hash(struct wolfBoot_image *img, uint8_t *hash)
{
    uint8_t elf_header_buf[sizeof(elf64_header)];
    uint8_t program_header_buf[sizeof(elf64_program_header)];
    elf32_header* h32;
    elf64_header* h64;
    uint16_t entry_count, entry_size;
    uint32_t ph_offset;
    int is_elf32, is_le, i;
#if defined(WOLFBOOT_HASH_SHA256)
    wc_Sha256 sha256_ctx;
#elif defined(WOLFBOOT_HASH_SHA384)
    wc_Sha384 sha384_ctx;
#elif defined(WOLFBOOT_HASH_SHA3_384)
    wc_Sha3 sha3_384_ctx;
#endif

#ifdef EXT_FLASH
    if (PART_IS_EXT(img)) {
        /* Read ELF header from external flash */
        ext_flash_check_read((uintptr_t)(img->fw_base), elf_header_buf, sizeof(elf64_header));
    } else
#endif
    {
        memcpy(elf_header_buf, (void*)(img->fw_base), sizeof(elf64_header));
    }

    h32 = (elf32_header*)elf_header_buf;
    h64 = (elf64_header*)elf_header_buf;

    /* Verify ELF header */
    if (memcmp(h32->ident, ELF_IDENT_STR, 4) != 0) {
        return -1; /* not valid header identifier */
    }

    /* Load class and endianess */
    is_elf32 = (h32->ident[4] == ELF_CLASS_32);
    is_le = (h32->ident[5] == ELF_ENDIAN_LITTLE);
    (void)is_le;

    /* Initialize hash context */
#if defined(WOLFBOOT_HASH_SHA256)
    wc_InitSha256(&sha256_ctx);
#elif defined(WOLFBOOT_HASH_SHA384)
    wc_InitSha384(&sha384_ctx);
#elif defined(WOLFBOOT_HASH_SHA3_384)
    wc_Sha3_384_Init(&sha3_384_ctx, NULL, INVALID_DEVID);
#endif

    /* Get program headers info */
    ph_offset = is_elf32 ? GET32(h32->ph_offset) : GET32(h64->ph_offset);
    entry_size = is_elf32 ? GET16(h32->ph_entry_size) : GET16(h64->ph_entry_size);
    entry_count = is_elf32 ? GET16(h32->ph_entry_count) : GET16(h64->ph_entry_count);

    /* Hash each loadable segment directly from its physical address */
    for (i = 0; i < entry_count; i++) {
        elf32_program_header* phdr32;
        elf64_program_header* phdr64;
        uint32_t type;
        uintptr_t paddr;
        uintptr_t file_size;

        /* Read program header into buffer */
#ifdef EXT_FLASH
        if (PART_IS_EXT(img)) {
            ext_flash_check_read((uintptr_t)(img->fw_base) + ph_offset + (i * entry_size),
                program_header_buf, entry_size);
        } else
#endif
        {
            memcpy(program_header_buf,
                   (uint8_t*)(img->fw_base) + ph_offset + (i * entry_size),
                   entry_size);
        }


        phdr32 = (elf32_program_header*)program_header_buf;
        phdr64 = (elf64_program_header*)program_header_buf;
        type = (is_elf32 ? GET32(phdr32->type) : GET32(phdr64->type));
        paddr = (is_elf32 ? GET32(phdr32->paddr) : GET64(phdr64->paddr));
        file_size = (is_elf32 ? GET32(phdr32->file_size) : GET64(phdr64->file_size));

        /* Only hash PT_LOAD segments with non-zero size */
        if (type == ELF_PT_LOAD && file_size > 0) {
#ifdef DEBUG_ELF
            wolfBoot_printf("Hashing segment at %p (%d bytes)\r\n", (void*)paddr, (uint32_t)file_size);
#endif
            /* Hash the segment data from physical address in blocks */
            uint32_t pos = 0;
            while (pos < file_size) {
                uint8_t *block;
                uint32_t blksz = WOLFBOOT_SHA_BLOCK_SIZE;

                if (pos + blksz > file_size) {
                    blksz = file_size - pos;
                }

                block = get_sha_block_ptr(img, (const uint8_t *)(paddr + pos));
                if (block == NULL) {
                    return -1;
                }

#if defined(WOLFBOOT_HASH_SHA256)
                wc_Sha256Update(&sha256_ctx, block, blksz);
#elif defined(WOLFBOOT_HASH_SHA384)
                wc_Sha384Update(&sha384_ctx, block, blksz);
#elif defined(WOLFBOOT_HASH_SHA3_384)
                wc_Sha3_384_Update(&sha3_384_ctx, block, blksz);
#endif
                pos += blksz;
            }
        }
    }

    /* Finalize hash */
#if defined(WOLFBOOT_HASH_SHA256)
    wc_Sha256Final(&sha256_ctx, hash);
#elif defined(WOLFBOOT_HASH_SHA384)
    wc_Sha384Final(&sha384_ctx, hash);
#elif defined(WOLFBOOT_HASH_SHA3_384)
    wc_Sha3_384_Final(&sha3_384_ctx, hash);
#endif

    return 0;
}
#endif

int elf_store_image_scattered(const unsigned char *image, unsigned long *entry_out, int ext_flash) {
    const unsigned char *ident;
    int is_elf32;
    unsigned short entry_count;
    unsigned short entry_size;
    unsigned long entry_off;
    int i;

    ident = image;


    /* Verify ELF header */
    if (memcmp(ident, ELF_IDENT_STR, 4) != 0) {
        return -1; /* not valid header identifier */
    }

    is_elf32 = (ident[ELF_CLASS_OFF] == ELF_CLASS_32);

    if (is_elf32) {
        const elf32_header *eh;
        const elf32_program_header *ph;

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
#if 0
#ifdef EXT_FLASH
            if (ext_flash) {
                ext_flash_unlock();
                ext_flash_erase(paddr, filesz);
                ext_flash_write(paddr, image + offset, filesz);
                ext_flash_lock();
            }
            else
#endif
            {
                hal_flash_unlock();
                hal_flash_erase(paddr, filesz);
                hal_flash_write(paddr, image + offset, filesz);
                hal_flash_lock();
            }
#endif
        }
    } else if (ident[ELF_CLASS_OFF] == ELF_CLASS_64) {
        const elf64_header *eh;
        const elf64_program_header *ph;

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
#if 0
#ifdef EXT_FLASH
            if (ext_flash) {
                ext_flash_unlock();
                ext_flash_erase(paddr, filesz);
                ext_flash_write(paddr, image + offset, filesz);
                ext_flash_lock();
            }
            else
#endif
            {
                hal_flash_unlock();
                hal_flash_erase(paddr, filesz);
                hal_flash_write(paddr, image + offset, filesz);
                hal_flash_lock();
            }
#endif
        }
    } else {
        /* Invalid elf header. */
        return -1;
    }

    return 0;
}


int elf_load_image(uint8_t *image, uintptr_t *entry, int ext_flash)
{
#ifdef MMU
    return elf_load_image_mmu(image, entry, NULL);
#else
    return elf_store_image_scattered(image, entry, ext_flash);
#endif
}

#endif /* WOLFBOOT_ELF */
