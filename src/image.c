/* image.c
 *
 * Copyright (C) 2018 wolfSSL Inc.
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
#include <image.h>
#include <hal.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/ssl.h>
#include <loader.h>

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
#endif


static uint8_t get_header(struct wolfBoot_image *img, uint8_t type, uint8_t **ptr)
{
    uint8_t *p = img->hdr + IMAGE_HEADER_OFFSET;
    while (*p != 0) {
        if (*p == HDR_PADDING) {
            p++;
            continue;
        }
        if (*p == type) {
            p++;
            *ptr = (p + 1);
            return *p;
        } 
        p++;
        p += (*p + 1);
    }
    *ptr = NULL;
    return 0;
}

int wolfBoot_open_image(struct wolfBoot_image *img, uint8_t part)
{
    uint32_t *magic;
    uint32_t *size;
    if (!img)
        return -1;
    memset(img, 0, sizeof(struct wolfBoot_image));

    if (part == PART_BOOT)
        img->hdr = (void *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    else if (part == PART_UPDATE)
        img->hdr = (void *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    else
        return -1;
    magic = (uint32_t *)(img->hdr);
    if (*magic != WOLFBOOT_MAGIC)
        return -1;
    size = (uint32_t *)(img->hdr + sizeof (uint32_t));
    
    if (*size >= WOLFBOOT_PARTITION_SIZE)
       return -1; 
    img->part = part;
    img->hdr_ok = 1;
    img->fw_size = *size;
    img->fw_base = img->hdr + IMAGE_HEADER_SIZE;
    img->trailer = img->hdr + WOLFBOOT_PARTITION_SIZE;
    return 0;
}

static int image_hash(struct wolfBoot_image *img, uint8_t *hash)
{
    uint8_t *stored_sha, *end_sha;
    uint8_t stored_sha_len;
    uint8_t *p = img->hdr;
    uint8_t *fw_end = img->fw_base + img->fw_size;
    int blksz;
    wc_Sha256 sha256_ctx;
    if (!img || !img->hdr)
        return -1;
    
    stored_sha_len = get_header(img, HDR_SHA256, &stored_sha);
    if (stored_sha_len != SHA256_DIGEST_SIZE)
        return -1;
    wc_InitSha256(&sha256_ctx);
    end_sha = stored_sha - 2;
    while (p < end_sha) {
        blksz = SHA256_BLOCK_SIZE;
        if (end_sha - p < blksz)
            blksz = end_sha - p;
        wc_Sha256Update(&sha256_ctx, p, blksz);
        p += blksz;
    }
    p = img->fw_base;
    while(p < fw_end) {
        blksz = SHA256_BLOCK_SIZE;
        if (fw_end - p < blksz)
            blksz = fw_end - p;
        wc_Sha256Update(&sha256_ctx, p, blksz);
        p += blksz;
    }
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

static uint8_t digest[SHA256_DIGEST_SIZE];
int wolfBoot_verify_integrity(struct wolfBoot_image *img)
{
    uint8_t *stored_sha;
    uint8_t stored_sha_len;
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

static uint8_t verification[IMAGE_SIGNATURE_SIZE];
int wolfBoot_verify_authenticity(struct wolfBoot_image *img)
{
    uint8_t *stored_signature;
    uint8_t stored_signature_size;
    uint8_t *pubkey_hint;
    uint8_t pubkey_hint_size;

    stored_signature_size = get_header(img, HDR_SIGNATURE, &stored_signature);
    if (stored_signature_size != IMAGE_SIGNATURE_SIZE)
       return -1;
    pubkey_hint_size = get_header(img, HDR_PUBKEY, &pubkey_hint);
    if (pubkey_hint_size == SHA256_DIGEST_SIZE) {
        key_hash(digest);
        if (memcmp(digest, pubkey_hint, SHA256_DIGEST_SIZE) != 0)
            return -1;
    }

    if (image_hash(img, digest) != 0)
        return -1;
    if (wolfBoot_verify_signature(digest, stored_signature) != 0)
        return -1;
    img->signature_ok = 1;
    return 0;
}


int wolfBoot_copy(uint32_t src, uint32_t dst, uint32_t size)
{
    uint32_t *content;
    uint32_t pos = 0;
    if (src == dst)
        return 0;
    if ((src & 0x03) || (dst & 0x03))
        return -1;
    while (pos < size) {
        content = (uint32_t *)(src + pos);
        hal_flash_write(dst + pos, (void *)content, sizeof(uint32_t));
        pos += sizeof(uint32_t);
    }
    return pos;
}
