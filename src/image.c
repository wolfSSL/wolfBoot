/* image.c
 *
 * Copyright (C) 2019 wolfSSL Inc.
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

#include "loader.h"
#include "image.h"
#include "hal.h"

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>

#ifdef WOLFBOOT_SIGN_ED25519
#include <wolfssl/wolfcrypt/ed25519.h>

static int wolfBoot_verify_signature(uint8_t *hash, uint8_t *sig)
{
    int ret, res;
    ed25519_key ed;
    ret = wc_ed25519_init(&ed);
    if (ret < 0) {
        /* Failed to initialize key */
        return -1;
    }
    ret = wc_ed25519_import_public(KEY_BUFFER, KEY_LEN, &ed);
    if (ret < 0) {
        /* Failed to import ed25519 key */
        return -1;
    }
    ret = wc_ed25519_verify_msg(sig, IMAGE_SIGNATURE_SIZE, hash, SHA256_DIGEST_SIZE, &res, &ed);
    if ((ret < 0) || (res == 0)) {
        return -1;
    }
    return 0;
}
#endif /* WOLFBOOT_SIGN_ED25519 */

#ifdef WOLFBOOT_SIGN_ECC256
#include <wolfssl/wolfcrypt/ecc.h>
#define ECC_KEY_SIZE  32
#define ECC_SIG_SIZE  64

static int wolfBoot_verify_signature(uint8_t *hash, uint8_t *sig)
{
    int ret, res;
    mp_int r, s;
    ecc_key ecc;
    ret = wc_ecc_init(&ecc);
    if (ret < 0) {
        /* Failed to initialize key */
        return -1;
    }
    /* Import public key */
    ret = wc_ecc_import_unsigned(&ecc, (byte*)KEY_BUFFER, (byte*)(KEY_BUFFER + 32), NULL, ECC_SECP256R1);
    if ((ret < 0) || ecc.type != ECC_PUBLICKEY) {
        /* Failed to import ecc key */
        return -1;
    }

    /* Import signature into r,s */
    mp_init(&r);
    mp_init(&s);
    mp_read_unsigned_bin(&r, sig, ECC_KEY_SIZE);
    mp_read_unsigned_bin(&s, sig + ECC_KEY_SIZE, ECC_KEY_SIZE);
    ret = wc_ecc_verify_hash_ex(&r, &s, hash, SHA256_DIGEST_SIZE, &res, &ecc);
    if ((ret < 0) || (res == 0)) {
        return -1;
    }
    return 0;
}
#endif /* WOLFBOOT_SIGN_ECC256 */

#ifdef WOLFBOOT_SIGN_RSA2048
#include <wolfssl/wolfcrypt/rsa.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#define RSA_MAX_KEY_SIZE 256
#define RSA_SIG_SIZE 256

static int wolfBoot_verify_signature(uint8_t *hash, uint8_t *sig)
{
    int ret, res = 0;
    struct RsaKey rsa;
    uint8_t digest_out[IMAGE_SIGNATURE_SIZE];
    word32 in_out = 0;


    ret = wc_InitRsaKey(&rsa, NULL);
    if (ret < 0) {
        /* Failed to initialize key */
        return -1;
    }
    /* Import public key */
    ret = wc_RsaPublicKeyDecode((byte*)KEY_BUFFER, &in_out, &rsa, KEY_LEN);
    if (ret < 0) {
        /* Failed to import rsa key */
        return -1;
    }
    ret = wc_RsaSSL_Verify(sig, RSA_SIG_SIZE, digest_out, RSA_SIG_SIZE, &rsa); 
    if (ret == SHA256_DIGEST_SIZE) {
        if (memcmp(digest_out, hash, ret) == 0)
            return 0;
    }
    return -1;
}
#endif /* WOLFBOOT_SIGN_RSA2048 */

static uint8_t get_header_ext(struct wolfBoot_image *img, uint16_t type, uint8_t **ptr);

static uint16_t get_header(struct wolfBoot_image *img, uint16_t type, uint8_t **ptr)
{
#if defined(PART_UPDATE_EXT)
    if(img->part == PART_UPDATE)
        return get_header_ext(img, type, ptr);
#endif
    return wolfBoot_find_header(img->hdr + IMAGE_HEADER_OFFSET, type, ptr);
}

static uint8_t ext_hash_block[SHA256_BLOCK_SIZE];

static uint8_t *get_sha_block(struct wolfBoot_image *img, uint32_t offset)
{
    if (offset > img->fw_size)
        return NULL;
#ifdef PART_UPDATE_EXT
    if (img->part == PART_UPDATE) {
        ext_flash_read((uint32_t)(img->fw_base) + offset, ext_hash_block, SHA256_BLOCK_SIZE);
        return ext_hash_block;
    }
#endif
    return (uint8_t *)(img->fw_base + offset);
}

static uint8_t digest[SHA256_DIGEST_SIZE];
static uint8_t verification[IMAGE_SIGNATURE_SIZE];
#ifdef EXT_FLASH

static uint8_t hdr_cpy[IMAGE_HEADER_SIZE];
static int hdr_cpy_done = 0;

static uint8_t *fetch_hdr_cpy(struct wolfBoot_image *img)
{
    if (!hdr_cpy_done) {
        ext_flash_read((uint32_t)img->hdr, hdr_cpy, IMAGE_HEADER_SIZE);
        hdr_cpy_done = 1;
    }
    return hdr_cpy;
}

static uint16_t get_header_ext(struct wolfBoot_image *img, uint16_t type, uint8_t **ptr)
{
    return wolfBoot_find_header(fetch_hdr_cpy(img) + IMAGE_HEADER_OFFSET, type, ptr);
}

#endif

static uint8_t *get_img_hdr(struct wolfBoot_image *img)
{
#ifdef PART_UPDATE_EXT
    if (img->part == PART_UPDATE) {
        return fetch_hdr_cpy(img);
    }
#endif
    return (uint8_t *)(img->hdr);
}

static int image_hash(struct wolfBoot_image *img, uint8_t *hash)
{
    uint8_t *stored_sha, *end_sha;
    uint16_t stored_sha_len;
    uint8_t *p;
    int blksz;
    uint32_t position = 0;
    wc_Sha256 sha256_ctx;
    if (!img)
        return -1;
    p = get_img_hdr(img);
    stored_sha_len = get_header(img, HDR_SHA256, &stored_sha);
    if (stored_sha_len != SHA256_DIGEST_SIZE)
        return -1;
    wc_InitSha256(&sha256_ctx);
    end_sha = stored_sha - 4;
    while (p < end_sha) {
        blksz = SHA256_BLOCK_SIZE;
        if (end_sha - p < blksz)
            blksz = end_sha - p;
        wc_Sha256Update(&sha256_ctx, p, blksz);
        p += blksz;
    }
    do {
        p = get_sha_block(img, position);
        if (p == NULL)
            break;
        blksz = SHA256_BLOCK_SIZE;
        if (position + blksz > img->fw_size)
            blksz = img->fw_size - position;
        wc_Sha256Update(&sha256_ctx, p, blksz);
        position += blksz;
    } while(position < img->fw_size);

    wc_Sha256Final(&sha256_ctx, hash);
    return 0;
}

static void key_hash(uint8_t *hash)
{
    int blksz;
    unsigned int i = 0;
    wc_Sha256 sha256_ctx;
    wc_InitSha256(&sha256_ctx);
    while(i < KEY_LEN)
    {
        blksz = SHA256_BLOCK_SIZE;
        if ((i + blksz) > KEY_LEN)
            blksz = KEY_LEN - i;
        wc_Sha256Update(&sha256_ctx, (KEY_BUFFER + i), blksz);
        i += blksz;
    }
    wc_Sha256Final(&sha256_ctx, hash);
}




int wolfBoot_open_image(struct wolfBoot_image *img, uint8_t part)
{
    uint32_t *magic;
    uint32_t *size;
    uint8_t *image;
    if (!img)
        return -1;
    memset(img, 0, sizeof(struct wolfBoot_image));
    img->part = part;
    if (part == PART_SWAP) {
        img->part = PART_SWAP;
        img->hdr = (void *)WOLFBOOT_PARTITION_SWAP_ADDRESS;
        img->fw_base = img->hdr;
        img->fw_size = WOLFBOOT_SECTOR_SIZE;
        return 0;
    }
    if (part == PART_BOOT) {
        img->hdr = (void *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
        image = (uint8_t *)img->hdr;
    } else if (part == PART_UPDATE) {
        img->hdr = (void *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
#ifdef PART_UPDATE_EXT
        image = fetch_hdr_cpy(img);
#else
        image = (uint8_t *)img->hdr;
#endif
    } else
        return -1;
    magic = (uint32_t *)(image);
    if (*magic != WOLFBOOT_MAGIC)
        return -1;
    size = (uint32_t *)(image + sizeof (uint32_t));

    if (*size >= WOLFBOOT_PARTITION_SIZE)
       return -1;
    img->hdr_ok = 1;
    img->fw_size = *size;
    img->fw_base = img->hdr + IMAGE_HEADER_SIZE;
    img->trailer = img->hdr + WOLFBOOT_PARTITION_SIZE;
    return 0;
}

int wolfBoot_verify_integrity(struct wolfBoot_image *img)
{
    uint8_t *stored_sha;
    uint16_t stored_sha_len;
    stored_sha_len = get_header(img, HDR_SHA256, &stored_sha);
    if (stored_sha_len != SHA256_DIGEST_SIZE)
        return -1;
    if (image_hash(img, digest) != 0)
        return -1;
    if (memcmp(digest, stored_sha, stored_sha_len) != 0)
        return -1;
    img->sha_ok = 1;
    return 0;
}

int wolfBoot_verify_authenticity(struct wolfBoot_image *img)
{
    uint8_t *stored_signature;
    uint16_t stored_signature_size;
    uint8_t *pubkey_hint;
    uint16_t pubkey_hint_size;
    uint8_t *image_type_buf;
    uint16_t image_type;
    uint16_t image_type_size;

    stored_signature_size = get_header(img, HDR_SIGNATURE, &stored_signature);
    if (stored_signature_size != IMAGE_SIGNATURE_SIZE)
       return -1;
    pubkey_hint_size = get_header(img, HDR_PUBKEY, &pubkey_hint);
    if (pubkey_hint_size == SHA256_DIGEST_SIZE) {
        key_hash(digest);
        if (memcmp(digest, pubkey_hint, SHA256_DIGEST_SIZE) != 0)
            return -1;
    }
    image_type_size = get_header(img, HDR_IMG_TYPE, &image_type_buf);
    if (image_type_size != sizeof(uint16_t))
            return -1;
    image_type = (uint16_t)(image_type_buf[0] + (image_type_buf[1] << 8));

    if ((image_type & 0xFF00) != HDR_IMG_TYPE_AUTH)
        return -1;


    if (image_hash(img, digest) != 0)
        return -1;
    if (wolfBoot_verify_signature(digest, stored_signature) != 0)
        return -1;
    img->signature_ok = 1;
    return 0;
}
