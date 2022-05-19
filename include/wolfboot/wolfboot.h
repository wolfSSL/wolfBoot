/* wolfboot.h
 *
 * The wolfBoot API definitions.
 *
 * Copyright (C) 2021 wolfSSL Inc.
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


#ifndef WOLFBOOT_H
#define WOLFBOOT_H

#include <stdint.h>
#include "target.h"
#include "wolfboot/version.h"


#ifndef IMAGE_HEADER_SIZE
#  define IMAGE_HEADER_SIZE 256
#endif
#define IMAGE_HEADER_OFFSET (2 * sizeof(uint32_t))

#ifndef FLASHBUFFER_SIZE
#    ifdef NVM_FLASH_WRITEONCE
#        define FLASHBUFFER_SIZE WOLFBOOT_SECTOR_SIZE
#    else
#        define FLASHBUFFER_SIZE IMAGE_HEADER_SIZE
#    endif
#endif

#ifdef BIG_ENDIAN_ORDER
#    define WOLFBOOT_MAGIC          0X574F4C46 /* WOLF */
#    define WOLFBOOT_MAGIC_TRAIL    0x424F4F54 /* BOOT */
#else
#    define WOLFBOOT_MAGIC          0x464C4F57 /* WOLF */
#    define WOLFBOOT_MAGIC_TRAIL    0x544F4F42 /* BOOT */
#endif

#define HDR_END                     0x00
#define HDR_VERSION                 0x01
#define HDR_TIMESTAMP               0x02
#define HDR_SHA256                  0x03
#define HDR_IMG_TYPE                0x04
#define HDR_IMG_DELTA_BASE          0x05
#define HDR_IMG_DELTA_SIZE          0x06
#define HDR_PUBKEY                  0x10
#define HDR_SHA3_384                0x13
#define HDR_SHA384                  0x14
#define HDR_IMG_DELTA_INVERSE       0x15
#define HDR_IMG_DELTA_INVERSE_SIZE  0x16
#define HDR_SIGNATURE               0x20
#define HDR_PADDING                 0xFF

#define HDR_IMG_TYPE_AUTH_NONE    0xFF00
#define HDR_IMG_TYPE_AUTH_ED25519 0x0100
#define HDR_IMG_TYPE_AUTH_ECC256  0x0200
#define HDR_IMG_TYPE_AUTH_RSA2048 0x0300
#define HDR_IMG_TYPE_AUTH_RSA4096 0x0400
#define HDR_IMG_TYPE_AUTH_ED448   0x0500
#define HDR_IMG_TYPE_AUTH_ECC384  0x0600
#define HDR_IMG_TYPE_AUTH_ECC521  0x0700
#define HDR_IMG_TYPE_WOLFBOOT     0x0000
#define HDR_IMG_TYPE_APP          0x0001
#define HDR_IMG_TYPE_DIFF         0x00D0


#ifdef __WOLFBOOT
 #if defined(WOLFBOOT_NO_SIGN)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_NONE
 #elif defined(WOLFBOOT_SIGN_ED25519)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_ED25519
 #elif defined(WOLFBOOT_SIGN_ED448)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_ED448
 #elif defined(WOLFBOOT_SIGN_ECC256)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_ECC256
 #elif defined(WOLFBOOT_SIGN_ECC384)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_ECC384
 #elif defined(WOLFBOOT_SIGN_ECC521)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_ECC521
 #   error "ECC521 curves not yet supported in this version of wolfBoot. Please select a valid SIGN= option."
 #elif defined(WOLFBOOT_SIGN_RSA2048)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_RSA2048
 #elif defined(WOLFBOOT_SIGN_RSA4096)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_RSA4096
 #else
 #   error "no valid authentication mechanism selected. Please select a valid SIGN= option."
 #endif /* defined WOLFBOOT_SIGN_ECC256 || WOLFBOOT_SIGN_ED25519 */
#endif /* defined WOLFBOOT */

#ifdef WOLFBOOT_FIXED_PARTITIONS
#define PART_BOOT   0
#define PART_UPDATE 1
#define PART_SWAP   2
#define PART_NONE   0xFF

#define PART_DTS (0x10)
#define PART_DTS_BOOT       (PART_DTS | PART_BOOT)
#define PART_DTS_UPDATE     (PART_DTS | PART_UPDATE)
#endif /* WOLFBOOT_FIXED_PARTITIONS */

#ifndef WOLFBOOT_FLAGS_INVERT
#define IMG_STATE_NEW       0xFF
#define IMG_STATE_UPDATING  0x70
#define IMG_STATE_TESTING   0x10
#define IMG_STATE_SUCCESS   0x00
#else
#define IMG_STATE_NEW       0x00
#define IMG_STATE_UPDATING  0x8F
#define IMG_STATE_TESTING   0xEF
#define IMG_STATE_SUCCESS   0xFF
#endif

void wolfBoot_update_trigger(void);
void wolfBoot_success(void);
uint32_t wolfBoot_image_size(uint8_t *image);
uint32_t wolfBoot_get_blob_version(uint8_t *blob);
uint32_t wolfBoot_get_blob_type(uint8_t *blob);
uint32_t wolfBoot_get_blob_diffbase_version(uint8_t *blob);

#ifdef WOLFBOOT_FIXED_PARTITIONS
void wolfBoot_erase_partition(uint8_t part);
uint32_t wolfBoot_get_image_version(uint8_t part);
uint16_t wolfBoot_get_image_type(uint8_t part);
uint32_t wolfBoot_get_diffbase_version(uint8_t part);
#define wolfBoot_current_firmware_version() wolfBoot_get_image_version(PART_BOOT)
#define wolfBoot_update_firmware_version() wolfBoot_get_image_version(PART_UPDATE)
#endif

int wolfBoot_fallback_is_possible(void);
int wolfBoot_dualboot_candidate(void);

int wolfBoot_dualboot_candidate_addr(void**);

/* Hashing function configuration */
#if defined(WOLFBOOT_HASH_SHA256)
#   define WOLFBOOT_SHA_BLOCK_SIZE (256)
#   define WOLFBOOT_SHA_HDR HDR_SHA256
#   define WOLFBOOT_SHA_DIGEST_SIZE (32)
#   define image_hash image_sha256
#   define key_hash key_sha256
#elif defined(WOLFBOOT_HASH_SHA384)
#   define WOLFBOOT_SHA_BLOCK_SIZE (256)
#   define WOLFBOOT_SHA_HDR HDR_SHA384
#   define WOLFBOOT_SHA_DIGEST_SIZE (48)
#   define image_hash image_sha384
#   define key_hash key_sha384
#elif defined(WOLFBOOT_HASH_SHA3_384)
#   define WOLFBOOT_SHA_BLOCK_SIZE (128)
#   define WOLFBOOT_SHA_HDR HDR_SHA3_384
#   define WOLFBOOT_SHA_DIGEST_SIZE (48)
#   define image_hash image_sha3_384
#   define key_hash key_sha3_384
#else
#   error "No valid hash algorithm defined!"
#endif


#ifdef EXT_ENCRYPTED
/* Encryption support */
#if defined(ENCRYPT_WITH_CHACHA)
    #define ENCRYPT_BLOCK_SIZE 64
    #define ENCRYPT_KEY_SIZE 32 /* Chacha20 - 256bit */
    #define ENCRYPT_NONCE_SIZE 12 /* 96 bit*/
#elif defined(ENCRYPT_WITH_AES128)
    #define ENCRYPT_BLOCK_SIZE 16
    #define ENCRYPT_KEY_SIZE 16 /* AES128  */
    #define ENCRYPT_NONCE_SIZE 16 /* AES IV size */
#elif defined(ENCRYPT_WITH_AES256)
    #define ENCRYPT_BLOCK_SIZE 16
    #define ENCRYPT_KEY_SIZE 32 /* AES256 */
    #define ENCRYPT_NONCE_SIZE 16 /* AES IV size */
#else
#   error "Encryption ON, but no encryption algorithm selected."
#endif

#endif /* EXT_ENCRYPTED */

#ifdef DELTA_UPDATES
int wolfBoot_get_diffbase_hdr(uint8_t part, uint8_t **ptr);
#endif

int wolfBoot_set_encrypt_key(const uint8_t *key, const uint8_t *nonce);
int wolfBoot_get_encrypt_key(uint8_t *key, uint8_t *nonce);
int wolfBoot_erase_encrypt_key(void);

#endif /* !WOLFBOOT_H */
