/* image.h
 *
 * Functions to help with wolfBoot image header
 *
 *
 * Copyright (C) 2020 wolfSSL Inc.
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

#ifndef IMAGE_H
#define IMAGE_H
#include <stdint.h>

#include "target.h"
#include "wolfboot/wolfboot.h"


#ifndef RAMFUNCTION
#if defined(__WOLFBOOT) && defined(RAM_CODE)
#  if defined(ARCH_ARM)
#    define RAMFUNCTION __attribute__((used,section(".ramcode"),long_call))
#  else
#    define RAMFUNCTION __attribute__((used,section(".ramcode")))
#  endif
#else
# define RAMFUNCTION
#endif
#endif

#ifndef WOLFBOOT_FLAGS_INVERT
#define SECT_FLAG_NEW      0x0F
#define SECT_FLAG_SWAPPING 0x07
#define SECT_FLAG_BACKUP   0x03
#define SECT_FLAG_UPDATED  0x00
#else
#define SECT_FLAG_NEW       0x00
#define SECT_FLAG_SWAPPING  0x08
#define SECT_FLAG_BACKUP    0x0c
#define SECT_FLAG_UPDATED   0x0f
#endif


struct wolfBoot_image {
    uint8_t *hdr;
    uint8_t *trailer;
    uint8_t *sha_hash;
    uint8_t *fw_base;
    uint32_t fw_size;
    uint8_t part;
    uint8_t hdr_ok : 1;
    uint8_t signature_ok : 1;
    uint8_t sha_ok : 1;
};


int wolfBoot_open_image(struct wolfBoot_image *img, uint8_t part);
int wolfBoot_verify_integrity(struct wolfBoot_image *img);
int wolfBoot_verify_authenticity(struct wolfBoot_image *img);
int wolfBoot_get_partition_state(uint8_t part, uint8_t *st);
int wolfBoot_set_partition_state(uint8_t part, uint8_t newst);
int wolfBoot_get_update_sector_flag(uint16_t sector, uint8_t *flag);
int wolfBoot_set_update_sector_flag(uint16_t sector, uint8_t newflag);

uint8_t* wolfBoot_peek_image(struct wolfBoot_image *img, uint32_t offset, uint32_t* sz);

/* Defined in libwolfboot */
uint16_t wolfBoot_find_header(uint8_t *haystack, uint16_t type, uint8_t **ptr);

#ifdef EXT_FLASH
# ifdef PART_BOOT_EXT
#  define BOOT_EXT 1
# else
#  define BOOT_EXT 0
# endif
# ifdef PART_UPDATE_EXT
#  define UPDATE_EXT 1
# else
#  define UPDATE_EXT 0
# endif
# ifdef PART_SWAP_EXT
#  define SWAP_EXT 1
# else
#  define SWAP_EXT 0
# endif
# define PARTN_IS_EXT(pn) \
    ((pn == PART_BOOT || pn == PART_DTS_BOOT)?BOOT_EXT: \
        ((pn == PART_UPDATE || pn == PART_DTS_UPDATE)?UPDATE_EXT: \
            ((pn == PART_SWAP)?SWAP_EXT:0)))
# define PART_IS_EXT(x)  PARTN_IS_EXT(((x)->part))
#include "hal.h"


#if defined(EXT_ENCRYPTED) && defined(__WOLFBOOT)
#include "encrypt.h"
#define ext_flash_check_write ext_flash_encrypt_write
#define ext_flash_check_read ext_flash_decrypt_read
#else
#define ext_flash_check_write ext_flash_write
#define ext_flash_check_read ext_flash_read
#endif

static inline int wb_flash_erase(struct wolfBoot_image *img, uint32_t off, uint32_t size)
{
    if (PART_IS_EXT(img))
        return ext_flash_erase((uintptr_t)(img->hdr) + off, size);
    else
        return hal_flash_erase((uintptr_t)(img->hdr) + off, size);
}

static inline int wb_flash_write(struct wolfBoot_image *img, uint32_t off, const void *data, uint32_t size)
{
    if (PART_IS_EXT(img))
        return ext_flash_check_write((uintptr_t)(img->hdr) + off, data, size);
    else
        return hal_flash_write((uintptr_t)(img->hdr) + off, data, size);
}

static inline int wb_flash_write_verify_word(struct wolfBoot_image *img, uint32_t off, uint32_t word)
{
    int ret;
    volatile uint32_t copy;
    if (PART_IS_EXT(img))
    {
        ext_flash_check_read((uintptr_t)(img->hdr) + off, (void *)&copy, sizeof(uint32_t));
        while (copy != word) {
            ret = ext_flash_check_write((uintptr_t)(img->hdr) + off, (void *)&word, sizeof(uint32_t));
            if (ret < 0)
                return ret;
            ext_flash_check_read((uintptr_t)(img->hdr) + off, (void *)&copy, sizeof(uint32_t));
        }
    } else {
        volatile uint32_t *pcopy = (volatile uint32_t*)(img->hdr + off);
        while(*pcopy != word) {
            hal_flash_write((uintptr_t)pcopy, (void *)&word, sizeof(uint32_t));
        }
    }
    return 0;
}


#else

# define PART_IS_EXT(x) (0)
# define PARTN_IS_EXT(x) (0)
# define wb_flash_erase(im, of, siz)  hal_flash_erase(((uintptr_t)(((im)->hdr)) + of), siz)
# define wb_flash_write(im, of, dat, siz)  hal_flash_write(((uintptr_t)((im)->hdr)) + of, dat, siz)

#endif /* EXT_FLASH */

/* -- Image Formats -- */
/* Legacy U-Boot Image */
#define UBOOT_IMG_HDR_MAGIC 0x56190527UL
#define UBOOT_IMG_HDR_SZ    64

/* --- Flattened Device Tree Blob */
#define UBOOT_FDT_MAGIC	    0xEDFE0DD0UL

#ifndef EXT_ENCRYPTED
#define WOLFBOOT_MAX_SPACE (WOLFBOOT_PARTITION_SIZE - (TRAILER_SKIP + sizeof(uint32_t) + (WOLFBOOT_PARTITION_SIZE + 1 / (WOLFBOOT_SECTOR_SIZE * 8))))
#else
#include "encrypt.h"
#define WOLFBOOT_MAX_SPACE (WOLFBOOT_PARTITION_SIZE - ENCRYPT_TMP_SECRET_OFFSET)
#endif

#endif /* !IMAGE_H */
