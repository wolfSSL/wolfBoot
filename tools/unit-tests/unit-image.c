/* unit-image.c
 *
 * Unit test for parser functions in image.c
 *
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

/* Option to enable sign tool debugging */
/* Must also define DEBUG_WOLFSSL in user_settings.h */
#define WOLFBOOT_HASH_SHA256
#define EXT_FLASH
#define PART_UPDATE_EXT
#define NVM_FLASH_WRITEONCE

#if defined(ENCRYPT_WITH_AES256) || defined(ENCRYPT_WITH_AES128)
    #define WOLFSSL_AES_COUNTER
    #define WOLFSSL_AES_DIRECT
#endif
#if defined(ENCRYPT_WITH_AES256)
    #define WOLFSSL_AES_256
#endif
#if defined(ENCRYPT_WITH_CHACHA)
    #define HAVE_CHACHA
#endif
#define ECC_TIMING_RESISTANT

#define ENCRYPT_KEY "123456789abcdef0123456789abcdef0123456789abcdef"
#if !defined(WOLFBOOT_SIGN_ED25519) && !defined(WOLFBOOT_SIGN_ED448) && \
    !defined(WOLFBOOT_SIGN_RSA2048) && !defined(WOLFBOOT_SIGN_RSA3072) && \
    !defined(WOLFBOOT_SIGN_RSA4096) && !defined(WOLFBOOT_SIGN_RSA2048ENC) && \
    !defined(WOLFBOOT_SIGN_RSA3072ENC) && !defined(WOLFBOOT_SIGN_RSA4096ENC) && \
    !defined(WOLFBOOT_SIGN_ECC256) && !defined(WOLFBOOT_SIGN_ECC384) && \
    !defined(WOLFBOOT_SIGN_ECC521) && !defined(WOLFBOOT_SIGN_LMS) && \
    !defined(WOLFBOOT_SIGN_XMSS) && !defined(WOLFBOOT_SIGN_ML_DSA)
#define WOLFBOOT_SIGN_ECC256
#endif

#include <stdio.h>
#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "user_settings.h"
#include "wolfssl/wolfcrypt/sha.h"
#include "wolfboot/wolfboot.h"

#include "unit-keystore.c"

#include "image.c"

const uint8_t a;

static int ecc_init_fail = 1;
static int ecc_import_fail = 1;

static int verify_called = 0;

static int find_header_fail = 0;
static int find_header_called = 0;
static int find_header_mocked = 1;

#if defined(WOLFBOOT_SIGN_ECC256)
static const unsigned char pubkey_digest[SHA256_DIGEST_SIZE] = {
  0x17, 0x20, 0xa5, 0x9b, 0xe0, 0x9b, 0x80, 0x0c, 0xaa, 0xc4, 0xf5, 0x3f,
  0xae, 0xe5, 0x72, 0x4f, 0xf2, 0x1f, 0x33, 0x53, 0xd1, 0xd4, 0xcd, 0x8b,
  0x5c, 0xc3, 0x4e, 0xda, 0xea, 0xc8, 0x4a, 0x68
};
#endif


uint32_t wolfBoot_get_blob_version(uint8_t *blob)
{
    (void)blob;
    return 1;
}


static unsigned char test_img_v200000000_signed_bin[] = {
      0x57, 0x4f, 0x4c, 0x46, 0x13, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00,
  0x00, 0xc2, 0xeb, 0x0b, 0xff, 0xff, 0xff, 0xff, 0x02, 0x00, 0x08, 0x00,
  0x77, 0x33, 0x29, 0x65, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00,
  0x01, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0x20, 0x00,
  0xda, 0x9c, 0xee, 0x7e, 0x12, 0xcf, 0xa0, 0xe1, 0xda, 0xa1, 0xb4, 0x23,
  0xbf, 0x31, 0xe5, 0xdd, 0x6f, 0x58, 0xfe, 0xd9, 0x8d, 0xb7, 0x7b, 0x31,
  0x6f, 0x7b, 0x01, 0x84, 0xe0, 0x63, 0x5e, 0xe9, 0x10, 0x00, 0x20, 0x00,
  0x17, 0x20, 0xa5, 0x9b, 0xe0, 0x9b, 0x80, 0x0c, 0xaa, 0xc4, 0xf5, 0x3f,
  0xae, 0xe5, 0x72, 0x4f, 0xf2, 0x1f, 0x33, 0x53, 0xd1, 0xd4, 0xcd, 0x8b,
  0x5c, 0xc3, 0x4e, 0xda, 0xea, 0xc8, 0x4a, 0x68, 0x20, 0x00, 0x40, 0x00,
  0xb0, 0x22, 0xb3, 0x91, 0xf7, 0x4e, 0xe1, 0x37, 0x6c, 0xb5, 0x64, 0x2e,
  0xe6, 0x80, 0x4b, 0xcb, 0xa7, 0x1d, 0xa1, 0xa7, 0x16, 0x2e, 0x4b, 0xa5,
  0xee, 0x67, 0xd2, 0x02, 0xff, 0x1b, 0xd3, 0x4c, 0xc6, 0x09, 0x62, 0x66,
  0x08, 0x4c, 0xfc, 0x32, 0x4b, 0x47, 0x56, 0xe0, 0x9b, 0x98, 0xd9, 0xa4,
  0x2a, 0x5e, 0x53, 0xd3, 0xb4, 0xde, 0x80, 0xe1, 0x9a, 0x95, 0x2a, 0x58,
  0xc9, 0xd6, 0x9a, 0x2a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0x54, 0x65, 0x73, 0x74, 0x20, 0x69, 0x6d, 0x61,
  0x67, 0x65, 0x20, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x0a
};


static const unsigned char test_img_v200000000_wrong_signature_bin[] = {
      0x57, 0x4f, 0x4c, 0x46, 0x13, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00,
  0x00, 0xc2, 0xeb, 0x0b, 0xff, 0xff, 0xff, 0xff, 0x02, 0x00, 0x08, 0x00,
  0x77, 0x33, 0x29, 0x65, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00,
  0x01, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0x20, 0x00,
  0xda, 0x9c, 0xee, 0x7e, 0x12, 0xcf, 0xa0, 0xe1, 0xda, 0xa1, 0xb4, 0x23,
  0xbf, 0x31, 0xe5, 0xdd, 0x6f, 0x58, 0xfe, 0xd9, 0x8d, 0xb7, 0x7b, 0x31,
  0x6f, 0x7b, 0x01, 0x84, 0xe0, 0x63, 0x5e, 0xe9, 0x10, 0x00, 0x20, 0x00,
  0x17, 0x20, 0xa5, 0x9b, 0xe0, 0x9b, 0x80, 0x0c, 0xaa, 0xc4, 0xf5, 0x3f,
  0xae, 0xe5, 0x72, 0x4f, 0xf2, 0x1f, 0x33, 0x53, 0xd1, 0xd4, 0xcd, 0x8b,
  0x5c, 0xc3, 0x4e, 0xda, 0xea, 0xc8, 0x4a, 0x68, 0x20, 0x00, 0x40, 0x00,
  0xb0, 0x22, 0xb3, 0x91, 0xf7, 0x4e, 0xe1, 0x37, 0x6c, 0xb5, 0x64, 0x2f,
  0xe6, 0x80, 0x4b, 0xcb, 0xa7, 0x1d, 0xa1, 0xa7, 0x16, 0x2e, 0x4b, 0xa5,
  0xee, 0x67, 0xd2, 0x02, 0xff, 0x1b, 0xd3, 0x4c, 0xc6, 0x09, 0x62, 0x66,
  0x08, 0x4c, 0xfc, 0x32, 0x4b, 0x47, 0x56, 0xe0, 0x9b, 0x98, 0xd9, 0xa4,
  0x2a, 0x5e, 0x53, 0xd3, 0xb4, 0xde, 0x80, 0xe1, 0x9a, 0x95, 0x2a, 0x58,
  0xc9, 0xd6, 0x9a, 0x2a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0x54, 0x65, 0x73, 0x74, 0x20, 0x69, 0x6d, 0x61,
  0x67, 0x65, 0x20, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x0a
};

static const unsigned char test_img_v200000000_wrong_pubkey_bin[] = {
      0x57, 0x4f, 0x4c, 0x46, 0x13, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00,
  0x00, 0xc2, 0xeb, 0x0b, 0xff, 0xff, 0xff, 0xff, 0x02, 0x00, 0x08, 0x00,
  0x77, 0x33, 0x29, 0x65, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00,
  0x01, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0x20, 0x00,
  0xda, 0x9c, 0xee, 0x7e, 0x12, 0xcf, 0xa0, 0xe1, 0xda, 0xa1, 0xb4, 0x24,
  0xbf, 0x31, 0xe5, 0xdd, 0x6f, 0x58, 0xfe, 0xd9, 0x8d, 0xb7, 0x7b, 0x31,
  0x6f, 0x7b, 0x01, 0x84, 0xe0, 0x63, 0x5e, 0xe9, 0x10, 0x00, 0x20, 0x00,
  0x17, 0x20, 0xa5, 0x9b, 0xe0, 0x9b, 0x80, 0x0c, 0xaa, 0xc4, 0xf5, 0x3f,
  0xae, 0xe5, 0x72, 0x4f, 0xf2, 0x1f, 0x33, 0x53, 0xd1, 0xd4, 0xcd, 0x8b,
  0x5c, 0xc3, 0x4e, 0xda, 0xea, 0xc8, 0x4a, 0x68, 0x20, 0x00, 0x40, 0x00,
  0xb0, 0x22, 0xb3, 0x91, 0xf7, 0x4e, 0xe1, 0x37, 0x6c, 0xb5, 0x64, 0x2e,
  0xe6, 0x80, 0x4b, 0xcb, 0xa7, 0x1d, 0xa1, 0xa7, 0x16, 0x2e, 0x4b, 0xa5,
  0xee, 0x67, 0xd2, 0x02, 0xff, 0x1b, 0xd3, 0x4c, 0xc6, 0x09, 0x62, 0x66,
  0x08, 0x4c, 0xfc, 0x32, 0x4b, 0x47, 0x56, 0xe0, 0x9b, 0x98, 0xd9, 0xa4,
  0x2a, 0x5e, 0x53, 0xd3, 0xb4, 0xde, 0x80, 0xe1, 0x9a, 0x95, 0x2a, 0x58,
  0xc9, 0xd6, 0x9a, 0x2a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0x54, 0x65, 0x73, 0x74, 0x20, 0x69, 0x6d, 0x61,
  0x67, 0x65, 0x20, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x0a
};

static uint16_t _find_header(uint8_t *haystack, uint16_t type, uint8_t **ptr);

static void patch_pubkey_hint(uint8_t *img, uint32_t img_len)
{
    uint8_t *ptr = NULL;
    uint16_t len;
    uint8_t hash[SHA256_DIGEST_SIZE];

    (void)img_len;
    len = _find_header(img + IMAGE_HEADER_OFFSET, HDR_PUBKEY, &ptr);
    ck_assert_int_eq(len, WOLFBOOT_SHA_DIGEST_SIZE);
    key_sha256(0, hash);
    memcpy(ptr, hash, WOLFBOOT_SHA_DIGEST_SIZE);
}

static void patch_signature_len(uint8_t *img, uint32_t img_len, uint16_t new_len)
{
    uint8_t *ptr = NULL;
    uint16_t len;

    (void)img_len;
    len = _find_header(img + IMAGE_HEADER_OFFSET, HDR_SIGNATURE, &ptr);
    ck_assert_int_ne(len, 0);
    ptr[-2] = (uint8_t)(new_len & 0xFF);
    ptr[-1] = (uint8_t)(new_len >> 8);
}

static void patch_image_type_auth(uint8_t *img, uint32_t img_len)
{
    uint8_t *ptr = NULL;
    uint16_t len;
    uint16_t type;

    (void)img_len;
    len = _find_header(img + IMAGE_HEADER_OFFSET, HDR_IMG_TYPE, &ptr);
    ck_assert_int_eq(len, sizeof(uint16_t));
    type = (uint16_t)(ptr[0] | (ptr[1] << 8));
    type = (uint16_t)((type & ~HDR_IMG_TYPE_AUTH_MASK) | HDR_IMG_TYPE_AUTH);
    ptr[0] = (uint8_t)(type & 0xFF);
    ptr[1] = (uint8_t)(type >> 8);
}
static const unsigned int test_img_len = 275;


unsigned char test_img_v123_signed_bin[] = {
      0x57, 0x4f, 0x4c, 0x46, 0x13, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00,
  0x7b, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x02, 0x00, 0x08, 0x00,
  0x77, 0x33, 0x29, 0x65, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00,
  0x01, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0x20, 0x00,
  0x89, 0xbd, 0x89, 0x01, 0xb9, 0xaf, 0xa9, 0xbd, 0x78, 0x88, 0xba, 0xd1,
  0x97, 0xc1, 0x6f, 0xd0, 0x7f, 0x11, 0xbd, 0x29, 0x97, 0x4a, 0x10, 0x27,
  0xa0, 0x53, 0x8c, 0x32, 0x3d, 0xfc, 0xc9, 0x9b, 0x10, 0x00, 0x20, 0x00,
  0x17, 0x20, 0xa5, 0x9b, 0xe0, 0x9b, 0x80, 0x0c, 0xaa, 0xc4, 0xf5, 0x3f,
  0xae, 0xe5, 0x72, 0x4f, 0xf2, 0x1f, 0x33, 0x53, 0xd1, 0xd4, 0xcd, 0x8b,
  0x5c, 0xc3, 0x4e, 0xda, 0xea, 0xc8, 0x4a, 0x68, 0x20, 0x00, 0x40, 0x00,
  0xfc, 0x1d, 0x02, 0x10, 0xb7, 0x60, 0x63, 0x7b, 0x55, 0xe0, 0x0e, 0xd5,
  0xb0, 0x64, 0xcd, 0x14, 0x9c, 0x1c, 0x80, 0x5f, 0x02, 0xb5, 0x54, 0x67,
  0x54, 0x93, 0x6d, 0xaf, 0x72, 0x74, 0x7b, 0x96, 0x94, 0x5c, 0x62, 0xb2,
  0x6d, 0x0f, 0xc9, 0xf4, 0x9f, 0x82, 0xa7, 0xd4, 0x28, 0xb9, 0x4c, 0x64,
  0x01, 0x5d, 0x03, 0x0f, 0x81, 0x05, 0x13, 0xf1, 0xe0, 0xbd, 0xdc, 0xe2,
  0x17, 0x84, 0xa3, 0x25, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0x54, 0x65, 0x73, 0x74, 0x20, 0x69, 0x6d, 0x61,
  0x67, 0x65, 0x20, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x0a
};
unsigned int test_img_v123_signed_bin_len = 275;


static uint16_t _find_header(uint8_t *haystack, uint16_t type, uint8_t **ptr)
{
    uint8_t *p = haystack;
    uint16_t len;
    const volatile uint8_t *max_p = (haystack - IMAGE_HEADER_OFFSET) +
                                                    IMAGE_HEADER_SIZE;
    *ptr = NULL;
    if (p > max_p) {
        printf("Illegal address (too high)\n");
        return 0;
    }
    while ((p + 4) < max_p) {
        if ((p[0] == 0) && (p[1] == 0)) {
            printf("Explicit end of options reached\n");
            break;
        }
        if (*p == HDR_PADDING) {
            /* Padding byte (skip one position) */
            p++;
            continue;
        }
        /* Sanity check to prevent dereferencing unaligned half-words */
        if ((((unsigned long)p) & 0x01) != 0) {
            p++;
            continue;
        }
        len = p[2] | (p[3] << 8);
        if ((4 + len) > (uint16_t)(IMAGE_HEADER_SIZE - IMAGE_HEADER_OFFSET)) {
            printf("This field is too large (bigger than the space available "
                     "in the current header)\n");
            printf("%d %d %d\n", len, IMAGE_HEADER_SIZE, IMAGE_HEADER_OFFSET);
            break;
        }
        if (p + 4 + len > max_p) {
            printf("This field is too large and would overflow the image "
                     "header\n");
            break;
        }
        if ((p[0] | (p[1] << 8)) == type) {
            *ptr = (p + 4);
            return len;
        }
        p += 4 + len;
    }
    return 0;
}

uint16_t wolfBoot_get_blob_type(uint8_t *addr)
{
    return HDR_IMG_TYPE_APP;
}

uint16_t wolfBoot_find_header(uint8_t *haystack, uint16_t type, uint8_t **ptr)
{
    find_header_called++;
    if (find_header_mocked) {
        if (find_header_fail) {
            return -1;
        } else {
            return sizeof(uint16_t);
        }
    } else {
        return _find_header(haystack, type, ptr);
    }
}


int wc_ecc_init(ecc_key* key) {
    if (ecc_init_fail)
        return -1;
    return 0;
}

int wc_ecc_free(ecc_key *key) {
    return 0;
}

int wc_ecc_import_unsigned(ecc_key* key, const byte* qx, const byte* qy,
                   const byte* d, int curve_id)
{
    if (ecc_import_fail)
        return -1;

    key->type = ECC_PUBLICKEY;
    return 0;
}


int wc_ecc_verify_hash_ex(mp_int *r, mp_int *s, const byte* hash,
                    word32 hashlen, int* res, ecc_key* key)
{
    verify_called++;
    *res = 1;
    return 0;
}

START_TEST(test_verify_signature)
{
    uint8_t pubkey[32];
    struct wolfBoot_image test_img;

    test_img.part = PART_UPDATE;
    test_img.fw_size = test_img_len;
    test_img.fw_base = 0;

    wolfBoot_verify_signature_ecc(0, NULL, NULL);
    ck_assert_int_eq(verify_called, 0);

    ecc_init_fail = 1;
    wolfBoot_verify_signature_ecc(0, NULL, pubkey);
    ck_assert_int_eq(verify_called, 0);

    ecc_init_fail = 0;
    verify_called = 0;
    ecc_import_fail = 1;
    wolfBoot_verify_signature_ecc(0, NULL, pubkey);
    ck_assert_int_eq(verify_called, 0);

    ecc_init_fail = 0;
    ecc_import_fail = 0;
    verify_called = 0;
    find_header_mocked = 0;
    ext_flash_erase(0, 2 * WOLFBOOT_SECTOR_SIZE);
    ext_flash_write(0, test_img_v200000000_signed_bin,
            test_img_len);
    wolfBoot_verify_signature_ecc(0, &test_img, pubkey);
    ck_assert_int_eq(verify_called, 1);
}
END_TEST


START_TEST(test_sha_ops)
{
    uint8_t hash[SHA256_DIGEST_SIZE];
    static uint8_t FlashImg[32 * 1024];
    uint8_t *retp = NULL;
    struct wolfBoot_image test_img;
    uint32_t offset;
    uint32_t sz = 0;
    find_header_mocked = 1;
    memset(&test_img, 0, sizeof(struct wolfBoot_image));
    test_img.part = PART_BOOT;
    test_img.fw_size = 0x1000;
    test_img.fw_base = FlashImg;

    /* Test get_sha_block */
    offset = 0x2000;
    retp = get_sha_block(&test_img, offset);
    ck_assert_ptr_null(retp);

    offset = 0x100;
    retp = get_sha_block(&test_img, offset);
    ck_assert_ptr_eq(retp, FlashImg + offset);

    test_img.part = PART_UPDATE;
    test_img.fw_size = 0x1000;
    test_img.fw_base = 0x0000;

    offset = 0x2000;
    retp = get_sha_block(&test_img, offset);
    ck_assert_ptr_null(retp);

    offset = 0x100;
    retp = get_sha_block(&test_img, offset);
    ck_assert_ptr_eq(retp, ext_hash_block);

    /* Test wolfBoot_peek_image */
    hdr_cpy_done = 0;
    offset = 0x100;
    retp = get_sha_block(&test_img, offset);
    ck_assert_ptr_eq(retp, ext_hash_block);
    retp = wolfBoot_peek_image(&test_img, offset, NULL);
    ck_assert_ptr_eq(retp, ext_hash_block);
    retp = wolfBoot_peek_image(&test_img, offset, &sz);
    ck_assert_ptr_eq(retp, ext_hash_block);
    ck_assert_uint_eq(sz, WOLFBOOT_SHA_BLOCK_SIZE);

    /* Test image_sha256 */

    /* NULL img */
    ck_assert_int_lt(image_sha256(NULL, hash), 0);

    /* Too short, internal partition field */
    test_img.part = PART_BOOT;
    test_img.fw_size = 0x1000;
    ck_assert_int_lt(image_sha256(&test_img, hash), 0);

    /* Ext partition with a valid SHA */
    find_header_mocked = 0;
    find_header_fail = 0;
    hdr_cpy_done = 0;
    ext_flash_write(0, test_img_v200000000_signed_bin,
            test_img_len);
    test_img.part = PART_UPDATE;
    test_img.fw_base = 0;
    test_img.fw_size = test_img_len;
    ck_assert_int_eq(image_sha256(&test_img, hash), 0);

    /* key_sha256 */
    key_sha256(0, hash);
#if defined(WOLFBOOT_SIGN_ECC256)
    ck_assert_mem_eq(hash, pubkey_digest, SHA256_DIGEST_SIZE);
#else
    /* For non-ECC256 configurations we do not have a fixed expected digest. */
    (void)hash;
#endif
}
END_TEST

START_TEST(test_headers)
{
    struct wolfBoot_image img;
    uint16_t type;
    void *ptr;
    uint16_t ret;
    uint32_t sz;
    memset(&img, 0, sizeof(struct wolfBoot_image));


    /* Test get_header() */
    img.part = PART_BOOT;
    find_header_fail = 1;
    find_header_called = 0;
    ret = get_header(&img, type, (void *)&ptr);
    ck_assert_uint_eq(ret, 0xFFFF);
    ck_assert_int_eq(find_header_called, 1);

    img.part = PART_BOOT;
    find_header_fail = 0;
    find_header_called = 0;
    ret = get_header(&img, type, (void *)&ptr);
    ck_assert_uint_ne(ret, 0xFFFF);
    ck_assert_int_eq(find_header_called, 1);

    img.part = PART_UPDATE;
    find_header_fail = 1;
    find_header_called = 0;
    ret = get_header(&img, type, (void *)&ptr);
    ck_assert_uint_eq(ret, 0xFFFF);
    ck_assert_int_eq(find_header_called, 1);

    img.part = PART_UPDATE;
    find_header_fail = 0;
    find_header_called = 0;
    ret = get_header(&img, type, (void *)&ptr);
    ck_assert_uint_ne(ret, 0xFFFF);
    ck_assert_int_eq(find_header_called, 1);

    /* Test get_img_hdr */
    img.part = PART_BOOT;
    img.hdr = (void *)0xAABBCCDD;
    ptr = get_img_hdr(&img);
    ck_assert_ptr_eq(ptr, img.hdr);

    img.part = PART_UPDATE;
    img.hdr = 0;
    ptr = get_img_hdr(&img);
    ck_assert_ptr_eq(ptr, hdr_cpy);

    /* Test image_size */
    sz = wolfBoot_image_size((void *)(uintptr_t)test_img_v200000000_signed_bin);
    ck_assert_uint_eq(sz, test_img_len - 256);
}

START_TEST(test_verify_authenticity)
{
    struct wolfBoot_image test_img;
    int ret;
    memset(&test_img, 0, sizeof(struct wolfBoot_image));
    test_img.part = PART_UPDATE;

    /* Wrong sha field */
    find_header_mocked = 1;
    ret = wolfBoot_verify_authenticity(&test_img);
    ck_assert_int_eq(ret, -1);

    /* Wrong pubkey  */
    find_header_mocked = 0;
    hdr_cpy_done = 0;
    ext_flash_write(0, test_img_v200000000_wrong_pubkey_bin,
            test_img_len);
    ret = wolfBoot_verify_authenticity(&test_img);
    ck_assert_int_lt(ret, 0);

    /* Wrong signature  */
    find_header_mocked = 0;
    find_header_fail = 0;
    hdr_cpy_done = 0;
    ext_flash_write(0, test_img_v200000000_wrong_signature_bin,
            test_img_len);
    ret = wolfBoot_verify_authenticity(&test_img);
    ck_assert_int_lt(ret, 0);

    /* Correct image  */
    find_header_mocked = 0;
    ecc_import_fail = 0;
    ecc_init_fail = 0;
    hdr_cpy_done = 0;
    ext_flash_erase(0, 2 * WOLFBOOT_SECTOR_SIZE);
    ext_flash_write(0, test_img_v123_signed_bin,
            test_img_v123_signed_bin_len);
    test_img.signature_ok = 1; /* mock for VERIFY_FN */
    ret = wolfBoot_verify_authenticity(&test_img);
    ck_assert_int_eq(ret, 0);

}
END_TEST

START_TEST(test_verify_authenticity_bad_siglen)
{
    struct wolfBoot_image test_img;
    uint8_t buf[sizeof(test_img_v200000000_signed_bin)];
    int ret;

    memcpy(buf, test_img_v200000000_signed_bin, sizeof(buf));
    patch_image_type_auth(buf, sizeof(buf));
    patch_pubkey_hint(buf, sizeof(buf));
    patch_signature_len(buf, sizeof(buf), 1);

    find_header_mocked = 0;
    find_header_fail = 0;
    hdr_cpy_done = 0;
    ext_flash_write(0, buf, sizeof(buf));

    memset(&test_img, 0, sizeof(struct wolfBoot_image));
    test_img.part = PART_UPDATE;
    ret = wolfBoot_verify_authenticity(&test_img);
    ck_assert_int_eq(ret, -1);
}
END_TEST

START_TEST(test_verify_integrity)
{
    struct wolfBoot_image test_img;
    int ret;
    /* Wrong sha field */
    find_header_mocked = 1;
    ret = wolfBoot_verify_integrity(&test_img);
    ck_assert_int_eq(ret, -1);

    /* Correct image  */
    find_header_mocked = 0;
    find_header_fail = 0;
    hdr_cpy_done = 0;
    ecc_import_fail = 0;
    ecc_init_fail = 0;
    memset(&test_img, 0, sizeof(struct wolfBoot_image));
    ext_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_SECTOR_SIZE);
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS,
            test_img_v123_signed_bin,
            test_img_v123_signed_bin_len);
    ret = wolfBoot_open_image(&test_img, PART_UPDATE);
    ck_assert_int_eq(ret, 0);
    ck_assert_ptr_eq(test_img.hdr, (void*)WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    ret = wolfBoot_verify_integrity(&test_img);
    ck_assert_int_eq(ret, 0);
}
END_TEST

START_TEST(test_open_image)
{
    struct wolfBoot_image img;
    int ret;


    /* invalid argument */
    ret = wolfBoot_open_image(NULL, PART_UPDATE);
    ck_assert_int_eq(ret, -1);

    /* Empty flash */
    find_header_mocked = 0;
    hdr_cpy_done = 0;
    ext_flash_erase(0, WOLFBOOT_SECTOR_SIZE);
    ret = wolfBoot_open_image(&img, PART_UPDATE);
    ck_assert_int_eq(ret, -1);

    /* Swap partition */
    ret = wolfBoot_open_image(&img, PART_SWAP);
    ck_assert_uint_eq(img.hdr_ok, 1);
    ck_assert_ptr_eq(img.hdr, (void *)WOLFBOOT_PARTITION_SWAP_ADDRESS);
    ck_assert_ptr_eq(img.hdr, img.fw_base);
    ck_assert_uint_eq(img.fw_size, WOLFBOOT_SECTOR_SIZE);

    /* Valid image */
    hdr_cpy_done = 0;
    ext_flash_write(0, test_img_v200000000_signed_bin,
            test_img_len);
    ret = wolfBoot_open_image(&img, PART_UPDATE);
    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(img.hdr_ok, 1);
    ck_assert_ptr_eq(img.hdr, WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    ck_assert_ptr_eq(img.fw_base, (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS
            + 256);

    /* External helper should accept the same mapped header pointer */
    ret = wolfBoot_open_image_external(NULL, PART_UPDATE,
            (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    ck_assert_int_eq(ret, -1);

    memset(&img, 0, sizeof(img));
    hdr_cpy_done = 0;
    ret = wolfBoot_open_image_external(&img, PART_UPDATE,
            (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(img.hdr_ok, 1);
    ck_assert_ptr_eq(img.hdr, (void *)WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    ck_assert_ptr_eq(img.fw_base, (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS
            + 256);
}
END_TEST


Suite *wolfboot_suite(void)
{
    /* Suite initialization */
    Suite *s = suite_create("wolfBoot");

    TCase* tcase_verify_signature = tcase_create("verify_signature");
    tcase_set_timeout(tcase_verify_signature, 20);
    tcase_add_test(tcase_verify_signature, test_verify_signature);
    suite_add_tcase(s, tcase_verify_signature);

    TCase* tcase_sha_ops = tcase_create("sha_ops");
    tcase_set_timeout(tcase_sha_ops, 20);
    tcase_add_test(tcase_sha_ops, test_sha_ops);
    suite_add_tcase(s, tcase_sha_ops);

    TCase* tcase_headers = tcase_create("headers");
    tcase_set_timeout(tcase_headers, 20);
    tcase_add_test(tcase_headers, test_headers);
    suite_add_tcase(s, tcase_headers);

    TCase* tcase_verify_authenticity = tcase_create("verify_authenticity");
    tcase_set_timeout(tcase_verify_authenticity, 20);
    tcase_add_test(tcase_verify_authenticity, test_verify_authenticity);
    tcase_add_test(tcase_verify_authenticity, test_verify_authenticity_bad_siglen);
    suite_add_tcase(s, tcase_verify_authenticity);

    TCase* tcase_verify_integrity = tcase_create("verify_integrity");
    tcase_set_timeout(tcase_verify_integrity, 20);
    tcase_add_test(tcase_verify_integrity, test_verify_integrity);
    suite_add_tcase(s, tcase_verify_integrity);

    TCase* tcase_open_image = tcase_create("open_image");
    tcase_set_timeout(tcase_open_image, 20);
    tcase_add_test(tcase_open_image, test_open_image);
    suite_add_tcase(s, tcase_open_image);
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
