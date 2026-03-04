/* wolfboot.h
 *
 * The wolfBoot API definitions.
 *
 * Copyright (C) 2025 wolfSSL Inc.
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


#ifndef WOLFBOOT_H
#define WOLFBOOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#ifdef __WOLFBOOT
/* Either hand-craft a device target.h file in [WOLFBOOT_ROOT]/include
 * or let build process auto-create one from .config file or cmake presets.
 *
 * See template: [WOLFBOOT_ROOT]/include/target.h.in
 * or unit test: [WOLFBOOT_ROOT]/tools/unit-tests/target.h
 */
#include "target.h"
#endif
#include "wolfboot/version.h"
#include "wolfboot/wc_secure.h"


#ifndef RAMFUNCTION
#  if defined(__WOLFBOOT) && defined(RAM_CODE)
#    if defined(ARCH_ARM)
#      define RAMFUNCTION __attribute__((used,section(".ramcode"),long_call))
#    elif defined(ARCH_PPC)
#      define RAMFUNCTION __attribute__((used,section(".ramcode"),longcall))
#    else
#      define RAMFUNCTION __attribute__((used,section(".ramcode")))
#    endif
#  else
#   define RAMFUNCTION
#endif
#endif

#ifndef WEAKFUNCTION
#  if defined(__GNUC__) || defined(__CC_ARM)
#    define WEAKFUNCTION __attribute__((weak))
#  else
#    define WEAKFUNCTION
#  endif
#endif

#ifndef UNUSEDFUNCTION
#  if defined(__GNUC__) || defined(__CC_ARM)
#    define UNUSEDFUNCTION __attribute__((unused))
#  else
#    define UNUSEDFUNCTION
#  endif
#endif


/* Helpers for memory alignment */
#ifndef XALIGNED
    #if defined(__GNUC__) || defined(__llvm__) || \
            defined(__IAR_SYSTEMS_ICC__)
        #define XALIGNED(x) __attribute__ ( (aligned (x)))
    #elif defined(__KEIL__)
        #define XALIGNED(x) __align(x)
    #elif defined(_MSC_VER)
        /* disable align warning, we want alignment ! */
        #pragma warning(disable: 4324)
        #define XALIGNED(x) __declspec (align (x))
    #else
        #define XALIGNED(x) /* null expansion */
    #endif
#endif

#ifndef XALIGNED_STACK
    /* Don't enforce stack alignment on IAR */
    #if defined(__IAR_SYSTEMS_ICC__)
        #define XALIGNED_STACK(x)
    #else
        #define XALIGNED_STACK(x) XALIGNED(x)
    #endif
#endif


#ifndef IMAGE_HEADER_SIZE
/* Largest cases first */
#   if defined(WOLFBOOT_SIGN_RSA4096)
#       define IMAGE_HEADER_SIZE 1024

    /* RSA3072 + strong hash */
#   elif (defined(WOLFBOOT_SIGN_RSA3072) && \
          (defined(WOLFBOOT_HASH_SHA384) || defined(WOLFBOOT_HASH_SHA3_384)))
#       define IMAGE_HEADER_SIZE 1024

    /* RSA2048 + SHA256 */
#   elif defined(WOLFBOOT_SIGN_RSA2048) && defined(WOLFBOOT_HASH_SHA256)
#       define IMAGE_HEADER_SIZE 512

    /* ECC384 requires 512 with SHA256 */
#   elif defined(WOLFBOOT_SIGN_ECC384) && defined(WOLFBOOT_HASH_SHA256)
#       define IMAGE_HEADER_SIZE 512

    /* ED25519 + any 384-bit or SHA3 hash */
#   elif defined(WOLFBOOT_SIGN_ED25519) && \
        (defined(WOLFBOOT_HASH_SHA384) || \
         defined(WOLFBOOT_HASH_SHA3)   || \
         defined(WOLFBOOT_HASH_SHA3_384))
#       define IMAGE_HEADER_SIZE 256

    /* ECC256 + any 384-bit hash */
#   elif defined(WOLFBOOT_SIGN_ECC256) && \
        (defined(WOLFBOOT_HASH_SHA384) || defined(WOLFBOOT_HASH_SHA3_384))
#       define IMAGE_HEADER_SIZE 256

    /* Secondary 512-byte fallbacks */
#   elif defined(WOLFBOOT_SIGN_RSA3072) || \
          defined(WOLFBOOT_SIGN_ECC521) || \
          defined(WOLFBOOT_SIGN_ED448)  || \
          defined(WOLFBOOT_HASH_SHA384) || \
          defined(WOLFBOOT_HASH_SHA3_384)
#       define IMAGE_HEADER_SIZE 512

    /* Default header size */
#   else
#       define IMAGE_HEADER_SIZE 256
#   endif

#endif /* IMAGE_HEADER_SIZE */
#define IMAGE_HEADER_OFFSET (2 * sizeof(uint32_t))

#ifndef FLASHBUFFER_SIZE
#    ifdef NVM_FLASH_WRITEONCE
#        define FLASHBUFFER_SIZE WOLFBOOT_SECTOR_SIZE
#    else
#        define FLASHBUFFER_SIZE IMAGE_HEADER_SIZE
#    endif
#endif

#ifdef WOLFBOOT_SELF_HEADER
#ifndef WOLFBOOT_SELF_HEADER_SIZE
#define WOLFBOOT_SELF_HEADER_SIZE IMAGE_HEADER_SIZE
#endif
#if (WOLFBOOT_SELF_HEADER_SIZE < IMAGE_HEADER_SIZE)
#error "WOLFBOOT_SELF_HEADER_SIZE must be at least IMAGE_HEADER_SIZE"
#endif
#ifdef __WOLFBOOT
#if !defined(WOLFBOOT_PART_USE_ARCH_OFFSET)
#if (WOLFBOOT_PARTITION_SELF_HEADER_ADDRESS % WOLFBOOT_SECTOR_SIZE)
#error "WOLFBOOT_PARTITION_SELF_HEADER_ADDRESS must be sector aligned"
#endif
#endif
#endif
#ifdef WOLFBOOT_SELF_HEADER_EXT
#ifndef EXT_FLASH
#error "WOLFBOOT_SELF_HEADER_EXT requires EXT_FLASH"
#endif
#endif
#endif /* WOLFBOOT_SELF_HEADER */

#ifdef BIG_ENDIAN_ORDER
#    define WOLFBOOT_MAGIC          0x574F4C46 /* WOLF */
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
#define HDR_IMG_DELTA_BASE_HASH     0x07
#define HDR_PUBKEY                  0x10
#define HDR_SECONDARY_CIPHER        0x11
#define HDR_SECONDARY_PUBKEY        0x12
#define HDR_SHA3_384                0x13
#define HDR_SHA384                  0x14
#define HDR_IMG_DELTA_INVERSE       0x15
#define HDR_IMG_DELTA_INVERSE_SIZE  0x16
#define HDR_SIGNATURE               0x20
#define HDR_POLICY_SIGNATURE        0x21
#define HDR_SECONDARY_SIGNATURE     0x22
#define HDR_CERT_CHAIN              0x23
#define HDR_PADDING                 0xFF

/* Auth Key types */
#define AUTH_KEY_ED25519 0x01
#define AUTH_KEY_ECC256  0x02
#define AUTH_KEY_RSA2048 0x03
#define AUTH_KEY_RSA4096 0x04
#define AUTH_KEY_ED448   0x05
#define AUTH_KEY_ECC384  0x06
#define AUTH_KEY_ECC521  0x07
#define AUTH_KEY_RSA3072 0x08
#define AUTH_KEY_LMS     0x09
                        /* 0x0A...0x0F reserved */
#define AUTH_KEY_XMSS    0x10
#define AUTH_KEY_ML_DSA  0x11

/*
 * 8 bits: auth type
 * 4 bits: extra features
 * 4 bits: partition id  (0 = bootloader)
 *
 */
#define HDR_IMG_TYPE_AUTH_MASK    0xFF00
#define HDR_IMG_TYPE_AUTH_NONE    0xFF00
#define HDR_IMG_TYPE_AUTH_ED25519 (AUTH_KEY_ED25519 << 8)
#define HDR_IMG_TYPE_AUTH_ECC256  (AUTH_KEY_ECC256  << 8)
#define HDR_IMG_TYPE_AUTH_RSA2048 (AUTH_KEY_RSA2048 << 8)
#define HDR_IMG_TYPE_AUTH_RSA4096 (AUTH_KEY_RSA4096 << 8)
#define HDR_IMG_TYPE_AUTH_ED448   (AUTH_KEY_ED448   << 8)
#define HDR_IMG_TYPE_AUTH_ECC384  (AUTH_KEY_ECC384  << 8)
#define HDR_IMG_TYPE_AUTH_ECC521  (AUTH_KEY_ECC521  << 8)
#define HDR_IMG_TYPE_AUTH_RSA3072 (AUTH_KEY_RSA3072 << 8)
#define HDR_IMG_TYPE_AUTH_LMS     (AUTH_KEY_LMS     << 8)
#define HDR_IMG_TYPE_AUTH_XMSS    (AUTH_KEY_XMSS    << 8)
#define HDR_IMG_TYPE_AUTH_ML_DSA  (AUTH_KEY_ML_DSA  << 8)

#define HDR_IMG_TYPE_DIFF         0x00D0

#define HDR_IMG_TYPE_PART_MASK    0x000F
#define HDR_IMG_TYPE_WOLFBOOT     0x0000
#ifndef HDR_IMG_TYPE_APP
#define HDR_IMG_TYPE_APP          0x0001
#endif

#define KEYSTORE_PUBKEY_SIZE_NONE    0
#define KEYSTORE_PUBKEY_SIZE_ED25519 32
#define KEYSTORE_PUBKEY_SIZE_ED448   57
#define KEYSTORE_PUBKEY_SIZE_ECC256  64
#define KEYSTORE_PUBKEY_SIZE_ECC384  96
#define KEYSTORE_PUBKEY_SIZE_ECC521  132
#define KEYSTORE_PUBKEY_SIZE_RSA2048 320
#define KEYSTORE_PUBKEY_SIZE_RSA3072 448
#define KEYSTORE_PUBKEY_SIZE_RSA4096 576
#define KEYSTORE_PUBKEY_SIZE_LMS     60
#define KEYSTORE_PUBKEY_SIZE_XMSS    68

/* ML-DSA pub key size is a function of parameters. */
#define ML_DSA_L2_PUBKEY_SIZE   1312
#define ML_DSA_L3_PUBKEY_SIZE   1952
#define ML_DSA_L5_PUBKEY_SIZE   2592

/* Configure using ML_DSA_LEVEL: Default is security category 2. */
#ifndef ML_DSA_LEVEL
#define ML_DSA_LEVEL 2
#endif

#if ML_DSA_LEVEL == 2
    #define KEYSTORE_PUBKEY_SIZE_ML_DSA ML_DSA_L2_PUBKEY_SIZE
#elif ML_DSA_LEVEL == 3
    #define KEYSTORE_PUBKEY_SIZE_ML_DSA ML_DSA_L3_PUBKEY_SIZE
#elif ML_DSA_LEVEL == 5
    #define KEYSTORE_PUBKEY_SIZE_ML_DSA ML_DSA_L5_PUBKEY_SIZE
#endif

/* Mask for key permissions */
#define KEY_VERIFY_ALL         (0xFFFFFFFFU)
#define KEY_VERIFY_ONLY_ID(X)  (1U << X)
#define KEY_VERIFY_SELF_ONLY   KEY_VERIFY_ONLY_ID(0)
#define KEY_VERIFY_APP_ONLY   KEY_VERIFY_ONLY_ID(1)

#if defined(__WOLFBOOT) || defined(UNIT_TEST_AUTH)

#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/visibility.h"
#include "wolfssl/wolfcrypt/wc_port.h"
#include "wolfssl/wolfcrypt/types.h"

#ifdef WOLFBOOT_RENESAS_TSIP
    /* Include these before any algorithm headers */
    #include "mcu/all/r_bsp_common.h"
    #include "r_bsp_config.h"
    #include "r_tsip_rx_if.h"
    #include "wolfssl/wolfcrypt/port/Renesas/renesas_tsip_types.h"
#endif


/* Hashing configuration */
#if defined(WOLFBOOT_HASH_SHA256)
#   ifdef WOLFBOOT_HASH_SHA384
#       error "Found WOLFBOOT_HASH_SHA384 with WOLFBOOT_HASH_SHA256. Pick one"
#   endif
#   ifdef WOLFBOOT_HASH_SHA3_384
#       error "Found WOLFBOOT_HASH_SHA3_384 with WOLFBOOT_HASH_SHA256. Pick one"
#   endif

    #include "wolfssl/wolfcrypt/sha256.h"
#   ifndef WOLFBOOT_SHA_BLOCK_SIZE
#     define WOLFBOOT_SHA_BLOCK_SIZE (256)
#   endif
#   define WOLFBOOT_SHA_HDR HDR_SHA256
#   define WOLFBOOT_SHA_DIGEST_SIZE (32)
#   define image_hash image_sha256
#   define header_hash header_sha256
#   define update_hash wc_Sha256Update
#   define key_hash key_sha256
#   define self_hash self_sha256
#   define final_hash wc_Sha256Final
    typedef wc_Sha256 wolfBoot_hash_t;
#   define HDR_HASH HDR_SHA256
#elif defined(WOLFBOOT_HASH_SHA384)
#   ifdef WOLFBOOT_HASH_SHA256
#       error "Found WOLFBOOT_HASH_SHA256 with WOLFBOOT_HASH_SHA384. Pick one"
#   endif
#   ifdef WOLFBOOT_HASH_SHA3_384
#       error "Found WOLFBOOT_HASH_SHA3_384 with WOLFBOOT_HASH_SHA384. Pick one"
#   endif

    #include "wolfssl/wolfcrypt/sha512.h"
#   ifndef WOLFBOOT_SHA_BLOCK_SIZE
#     define WOLFBOOT_SHA_BLOCK_SIZE (256)
#   endif
#   define WOLFBOOT_SHA_HDR HDR_SHA384
#   define WOLFBOOT_SHA_DIGEST_SIZE (48)
#   define image_hash image_sha384
#   define header_hash header_sha384
#   define update_hash wc_Sha384Update
#   define key_hash key_sha384
#   define self_hash self_sha384
#   define final_hash wc_Sha384Final
    typedef wc_Sha384 wolfBoot_hash_t;
#   define HDR_HASH HDR_SHA384
#elif defined(WOLFBOOT_HASH_SHA3_384)
#   ifdef WOLFBOOT_HASH_SHA256
#       error "Found WOLFBOOT_HASH_SHA256 with WOLFBOOT_HASH_SHA3_384. Pick one"
#   endif
#   ifdef WOLFBOOT_HASH_SHA384
#       error "Found WOLFBOOT_HASH_SHA384 with WOLFBOOT_HASH_SHA3_384. Pick one"
#   endif

    #include "wolfssl/wolfcrypt/sha3.h"
#   ifndef WOLFBOOT_SHA_BLOCK_SIZE
#     define WOLFBOOT_SHA_BLOCK_SIZE (256)
#   endif
#   define WOLFBOOT_SHA_HDR HDR_SHA3_384
#   define WOLFBOOT_SHA_DIGEST_SIZE (48)
#   define image_hash image_sha3_384
#   define header_hash header_sha3_384
#   define update_hash wc_Sha3Update
#   define final_hash wc_Sha3Final
#   define key_hash key_sha3_384
    typedef wc_Sha3 wolfBoot_hash_t;
#   define HDR_HASH HDR_SHA3_384
#else
#   error "No valid hash algorithm defined!"
#endif

#ifdef WOLFBOOT_TPM
    #if defined(WOLFBOOT_HASH_SHA256)
        #define WOLFBOOT_TPM_HASH_ALG TPM_ALG_SHA256
    #elif defined(WOLFBOOT_HASH_SHA384)
        #define WOLFBOOT_TPM_HASH_ALG TPM_ALG_SHA384
    #else
        #error TPM does not support hash algorithm selection
    #endif
#endif

#endif

#if defined(__WOLFBOOT) || defined (__FLASH_OTP_PRIMER) || \
    defined (UNIT_TEST_AUTH) || defined(WOLFBOOT_TPM)

 /* Authentication configuration */
 #if defined(WOLFBOOT_NO_SIGN)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_NONE
 #   ifndef WOLFBOOT_UNIVERSAL_KEYSTORE
 #     define KEYSTORE_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_NONE
 #   endif
 #elif defined(WOLFBOOT_SIGN_ED25519)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_ED25519
 #   ifndef WOLFBOOT_UNIVERSAL_KEYSTORE
 #     define KEYSTORE_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_ED25519
 #   endif
 #elif defined(WOLFBOOT_SIGN_ED448)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_ED448
 #   ifndef WOLFBOOT_UNIVERSAL_KEYSTORE
 #     define KEYSTORE_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_ED448
 #   endif
 #elif defined(WOLFBOOT_SIGN_ECC256)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_ECC256
 #   ifndef WOLFBOOT_UNIVERSAL_KEYSTORE
 #     define KEYSTORE_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_ECC256
 #   endif
 #elif defined(WOLFBOOT_SIGN_ECC384)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_ECC384
 #   ifndef WOLFBOOT_UNIVERSAL_KEYSTORE
 #     define KEYSTORE_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_ECC384
 #   endif
 #elif defined(WOLFBOOT_SIGN_ECC521)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_ECC521
 #   ifndef WOLFBOOT_UNIVERSAL_KEYSTORE
 #     define KEYSTORE_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_ECC521
 #   endif
 #elif defined(WOLFBOOT_SIGN_RSA2048) || defined(WOLFBOOT_SIGN_RSA2048ENC)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_RSA2048
 #   ifndef WOLFBOOT_UNIVERSAL_KEYSTORE
 #     define KEYSTORE_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_RSA2048
 #   endif
 #elif defined(WOLFBOOT_SIGN_RSA3072) || defined(WOLFBOOT_SIGN_RSA3072ENC)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_RSA3072
 #   ifndef WOLFBOOT_UNIVERSAL_KEYSTORE
 #     define KEYSTORE_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_RSA3072
 #   endif
 #elif defined(WOLFBOOT_SIGN_RSA4096) || defined(WOLFBOOT_SIGN_RSA4096ENC)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_RSA4096
 #   ifndef WOLFBOOT_UNIVERSAL_KEYSTORE
 #     define KEYSTORE_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_RSA4096
 #   endif
 #elif defined(WOLFBOOT_SIGN_LMS)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_LMS
 #   ifndef WOLFBOOT_UNIVERSAL_KEYSTORE
 #     define KEYSTORE_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_LMS
 #   endif
 #elif defined(WOLFBOOT_SIGN_XMSS)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_XMSS
 #   ifndef WOLFBOOT_UNIVERSAL_KEYSTORE
 #     define KEYSTORE_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_XMSS
 #   endif
 #elif defined(WOLFBOOT_SIGN_ML_DSA)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_ML_DSA
 #   ifndef WOLFBOOT_UNIVERSAL_KEYSTORE
 #     define KEYSTORE_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_ML_DSA
 #   endif
 #else
 #   error "No valid authentication mechanism selected. " \
           "Please select a valid SIGN= option."
 #endif /* authentication options */

 #include "keystore.h"

#endif /* defined WOLFBOOT */

#define PART_BOOT   0
#define PART_UPDATE 1
#define PART_SWAP   2
#define PART_SELF   3
#define PART_NONE   0xFF

#define PART_DTS (0x10)
#define PART_DTS_BOOT       (PART_DTS | PART_BOOT)
#define PART_DTS_UPDATE     (PART_DTS | PART_UPDATE)

#ifndef WOLFBOOT_FLAGS_INVERT
#define IMG_STATE_NEW       0xFF
#define IMG_STATE_UPDATING  0x70
/* now just an intermediary state, update state will always be either new or
 * updating before the application boots*/
#define IMG_STATE_FINAL_FLAGS 0x30
/* ELF loading state - only valid on boot partition so doesn't conflict with
 * IMAGE_STATE_UPDATING */
#define IMG_STATE_TESTING   0x10
#define IMG_STATE_SUCCESS   0x00
#define FLASH_BYTE_ERASED   0xFF
#define FLASH_WORD_ERASED   0xFFFFFFFFUL
#else
#define IMG_STATE_NEW       0x00
#define IMG_STATE_UPDATING  0x8F
#define IMG_STATE_TESTING   0xEF
#define IMG_STATE_FINAL_FLAGS 0xBF
#define IMG_STATE_SUCCESS   0xFF
#define FLASH_BYTE_ERASED   0x00
#define FLASH_WORD_ERASED   0x00000000UL
#endif

#ifdef __WOLFBOOT
    /* include after PART_* are defined */
    /* for wolfBoot_verify_integrity and wolfBoot_verify_authenticity */
    #include "image.h"
#endif

void wolfBoot_update_trigger(void);
void wolfBoot_success(void);
uint32_t wolfBoot_image_size(uint8_t *image);
uint32_t wolfBoot_get_blob_version(uint8_t *blob);
uint16_t wolfBoot_get_blob_type(uint8_t *blob);
uint32_t wolfBoot_get_blob_diffbase_version(uint8_t *blob);
#ifdef WOLFBOOT_SELF_HEADER
uint8_t* wolfBoot_get_self_header(void);
uint32_t wolfBoot_get_self_version(void);
#endif

uint16_t wolfBoot_find_header(uint8_t *haystack, uint16_t type, uint8_t **ptr);

/* Get partition ID from manifest header */
static inline uint8_t wolfBoot_get_blob_partition_id(uint8_t *blob) {
    return wolfBoot_get_blob_type(blob) & HDR_IMG_TYPE_PART_MASK;
}

#ifdef WOLFBOOT_FIXED_PARTITIONS
void wolfBoot_erase_partition(uint8_t part);
uint32_t wolfBoot_get_image_version(uint8_t part);
uint16_t wolfBoot_get_image_type(uint8_t part);
uint32_t wolfBoot_get_diffbase_version(uint8_t part);
#define wolfBoot_current_firmware_version() \
    wolfBoot_get_image_version(PART_BOOT)
#define wolfBoot_update_firmware_version() \
    wolfBoot_get_image_version(PART_UPDATE)
#endif

int wolfBoot_fallback_is_possible(void);
int wolfBoot_dualboot_candidate(void);

int wolfBoot_dualboot_candidate_addr(void**);
int wolfBoot_get_partition_state(uint8_t part, uint8_t *st);


/* Encryption algorithm constants - always available for tools */
#define ENCRYPT_BLOCK_SIZE_CHACHA  64
#define ENCRYPT_BLOCK_SIZE_AES     16

#define ENCRYPT_KEY_SIZE_CHACHA    32  /* ChaCha20 - 256bit */
#define ENCRYPT_KEY_SIZE_AES128    16  /* AES128 */
#define ENCRYPT_KEY_SIZE_AES256    32  /* AES256 */

#define ENCRYPT_NONCE_SIZE_CHACHA  12  /* 96 bit */
#define ENCRYPT_NONCE_SIZE_AES     16  /* AES IV size */


#ifdef EXT_ENCRYPTED
/* Encryption support - compile-time algorithm selection */

#if defined(ENCRYPT_WITH_CHACHA)
    #define ENCRYPT_BLOCK_SIZE ENCRYPT_BLOCK_SIZE_CHACHA
    #define ENCRYPT_KEY_SIZE   ENCRYPT_KEY_SIZE_CHACHA
    #define ENCRYPT_NONCE_SIZE ENCRYPT_NONCE_SIZE_CHACHA
#elif defined(ENCRYPT_WITH_AES128)
    #define ENCRYPT_BLOCK_SIZE ENCRYPT_BLOCK_SIZE_AES
    #define ENCRYPT_KEY_SIZE   ENCRYPT_KEY_SIZE_AES128
    #define ENCRYPT_NONCE_SIZE ENCRYPT_NONCE_SIZE_AES
#elif defined(ENCRYPT_WITH_AES256)
    #define ENCRYPT_BLOCK_SIZE ENCRYPT_BLOCK_SIZE_AES
    #define ENCRYPT_KEY_SIZE   ENCRYPT_KEY_SIZE_AES256
    #define ENCRYPT_NONCE_SIZE ENCRYPT_NONCE_SIZE_AES
#elif defined(ENCRYPT_PKCS11)
    #define ENCRYPT_BLOCK_SIZE ENCRYPT_PKCS11_BLOCK_SIZE
    /* In this case, the key ID is stored in flash rather than the key itself */
    #define ENCRYPT_KEY_SIZE   ENCRYPT_PKCS11_KEY_ID_SIZE
    #define ENCRYPT_NONCE_SIZE ENCRYPT_PKCS11_NONCE_SIZE
#else
#   error "Encryption ON, but no encryption algorithm selected."
#endif

#endif /* EXT_ENCRYPTED */

#if defined(EXT_ENCRYPTED) && defined(MMU)
int wolfBoot_ram_decrypt(uint8_t *src, uint8_t *dst);
#endif

#ifdef DELTA_UPDATES
int wolfBoot_get_diffbase_hdr(uint8_t part, uint8_t **ptr);
#endif

int wolfBoot_initialize_encryption(void);
int wolfBoot_set_encrypt_key(const uint8_t *key, const uint8_t *nonce);
int wolfBoot_get_encrypt_key(uint8_t *key, uint8_t *nonce);
int wolfBoot_erase_encrypt_key(void);

#if !defined(__WOLFBOOT) && defined(WOLFCRYPT_SECURE_MODE)

/* Applications can access update success/trigger and flash erase/write
 * via non-secure callable, to facilitate updates
 */

/* Call wolfBoot_success from non-secure application */
CSME_NSE_API
void wolfBoot_nsc_success(void);

/* Call wolfBoot_update_trigger from non-secure application */
CSME_NSE_API
void wolfBoot_nsc_update_trigger(void);

/* Call wolfBoot_get_image_version from non-secure application */
CSME_NSE_API
uint32_t wolfBoot_nsc_get_image_version(uint8_t part);
#define wolfBoot_nsc_current_firmware_version() wolfBoot_nsc_get_image_version(PART_BOOT)
#define wolfBoot_nsc_update_firmware_version() wolfBoot_nsc_get_image_version(PART_UPDATE)

/* Call wolfBoot_get_partition_state from non-secure application */
CSME_NSE_API
int wolfBoot_nsc_get_partition_state(uint8_t part, uint8_t *st);

/* Erase one or more sectors in the update partition.
 * - address: offset within the update partition ('0' corresponds to PARTITION_UPDATE_ADDRESS)
 * - len: size, in bytes
 */
CSME_NSE_API
int wolfBoot_nsc_erase_update(uint32_t address, uint32_t len);

/* Write the content of buffer `buf` and size `len` to the update partition,
 *  at offset address, from non-secure application
 * - address: offset within the update partition ('0' corresponds to PARTITION_UPDATE_ADDRESS)
 * - len: size, in bytes
 */
CSME_NSE_API
int wolfBoot_nsc_write_update(uint32_t address, const uint8_t *buf, uint32_t len);

#endif /* !__WOLFBOOT && WOLFCRYPT_SECURE_MODE */


#ifdef __cplusplus
}
#endif

#endif /* !WOLFBOOT_H */
