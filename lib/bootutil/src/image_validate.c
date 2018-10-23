/* image_validate.c
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

#include <stddef.h>
#include <inttypes.h>
#include <string.h>

#include "hal.h"

#include "bootutil/image.h"
#include "bootutil/sign_key.h"
#include "wolfssl/wolfcrypt/sha256.h"

#include "wolfssl/ssl.h"

#include "bootutil_priv.h"

/*
 * Compute SHA256 over the image.
 */
static int
bootutil_img_hash(struct image_header *hdr, const struct flash_area *fap,
                  uint8_t *tmp_buf, uint32_t tmp_buf_sz,
                  uint8_t *hash_result, uint8_t *seed, int seed_len)
{
    wc_Sha256 sha256_ctx;
    uint32_t blk_sz;
    uint32_t size;
    uint32_t off;
    int rc;

    wc_InitSha256(&sha256_ctx);

    /* in some cases (split image) the hash is seeded with data from
     * the loader image */
    if (seed && (seed_len > 0)) {
        wc_Sha256Update(&sha256_ctx, seed, seed_len);
    }

    /*
     * Hash is computed over image header and image itself. No TLV is
     * included ATM.
     */
    size = hdr->ih_img_size + hdr->ih_hdr_size;
    for (off = 0; off < size; off += blk_sz) {
        blk_sz = size - off;
        if (blk_sz > tmp_buf_sz) {
            blk_sz = tmp_buf_sz;
        }
        rc = flash_area_read(fap, off, tmp_buf, blk_sz);
        if (rc) {
            return rc;
        }
        wc_Sha256Update(&sha256_ctx, tmp_buf, blk_sz);
    }
    wc_Sha256Final(&sha256_ctx, hash_result);

    return 0;
}

/*
 * Currently, we only support being able to verify one type of
 * signature, because there is a single verification function that we
 * call.  List the type of TLV we are expecting.  If we aren't
 * configured for any signature, don't define this macro.
 */
#if defined(BOOT_SIGN_RSA)
#    define EXPECTED_SIG_TLV IMAGE_TLV_RSA2048_PSS
#    define EXPECTED_SIG_LEN(x) ((x) == 256) /* 2048 bits */
#    if defined(BOOT_SIGN_EC) || defined(BOOT_SIGN_EC256)
#        error "Multiple signature types not yet supported"
#    endif
#elif defined(BOOT_SIGN_EC)
#    define EXPECTED_SIG_TLV IMAGE_TLV_ECDSA224
#    define EXPECTED_SIG_LEN(x) ((x) >= 64) /* oids + 2 * 28 bytes */
#    if defined(BOOT_SIGN_EC256)
#        error "Multiple signature types not yet supported"
#    endif
#elif defined(BOOT_SIGN_EC256)
#    define EXPECTED_SIG_TLV IMAGE_TLV_ECDSA256
#    define EXPECTED_SIG_LEN(x) ((x) >= 72) /* oids + 2 * 32 bytes */
#elif defined(BOOT_SIGN_ED25519)
#    define EXPECTED_SIG_TLV IMAGE_TLV_ED25519
#    define EXPECTED_SIG_LEN(x)  ((x) == 64)
#endif

#ifdef EXPECTED_SIG_TLV
extern void boot_panic_unless(int);
static int
bootutil_find_key(uint8_t *keyhash, uint8_t keyhash_len)
{
    wc_Sha256 sha256_ctx;
    int i;
    const struct bootutil_key *key;
    uint8_t hash[32];

    boot_panic_unless(keyhash_len <= 32);

    for (i = 0; i < bootutil_key_cnt; i++) {
        key = &bootutil_keys[i];
        wc_InitSha256(&sha256_ctx);
        wc_Sha256Update(&sha256_ctx, key->key, *key->len);
        wc_Sha256Final(&sha256_ctx, hash);
        if (!memcmp(hash, keyhash, keyhash_len)) {
            return i;
        }
    }
    return -1;
}
#endif

/*
 * Verify the integrity of the image.
 * Return non-zero if image could not be validated/does not validate.
 */
int
bootutil_img_validate(struct image_header *hdr, const struct flash_area *fap,
                      uint8_t *tmp_buf, uint32_t tmp_buf_sz,
                      uint8_t *seed, int seed_len, uint8_t *out_hash)
{
    uint32_t off;
    uint32_t end;
    int sha256_valid = 0;
    struct image_tlv_info info;
#ifdef EXPECTED_SIG_TLV
    int valid_signature = 0;
    int key_id = -1;
#endif
    struct image_tlv tlv;
    uint8_t buf[256];
    uint8_t hash[32];
    int rc;

    rc = bootutil_img_hash(hdr, fap, tmp_buf, tmp_buf_sz, hash,
                           seed, seed_len);
    if (rc) {
        return rc;
    }

    if (out_hash) {
        memcpy(out_hash, hash, 32);
    }

    /* The TLVs come after the image. */
    /* After image there are TLVs. */
    off = hdr->ih_img_size + hdr->ih_hdr_size;

    rc = flash_area_read(fap, off, &info, sizeof(info));
    if (rc) {
        return rc;
    }
    if (info.it_magic != IMAGE_TLV_INFO_MAGIC) {
        return -1;
    }
    end = off + info.it_tlv_tot;
    off += sizeof(info);

    /*
     * Traverse through all of the TLVs, performing any checks we know
     * and are able to do.
     */
    for (; off < end; off += sizeof(tlv) + tlv.it_len) {
        rc = flash_area_read(fap, off, &tlv, sizeof tlv);
        if (rc) {
            return rc;
        }

        if (tlv.it_type == IMAGE_TLV_SHA256) {
            /*
             * Verify the SHA256 image hash.  This must always be
             * present.
             */
            if (tlv.it_len != sizeof(hash)) {
                return -1;
            }
            rc = flash_area_read(fap, off + sizeof(tlv), buf, sizeof hash);
            if (rc) {
                return rc;
            }
            if (memcmp(hash, buf, sizeof(hash))) {
                return -1;
            }

            sha256_valid = 1;
#ifdef EXPECTED_SIG_TLV
        } else if (tlv.it_type == IMAGE_TLV_KEYHASH) {
            /*
             * Determine which key we should be checking.
             */
            if (tlv.it_len > 32) {
                return -1;
            }
            rc = flash_area_read(fap, off + sizeof tlv, buf, tlv.it_len);
            if (rc) {
                return rc;
            }
            key_id = bootutil_find_key(buf, tlv.it_len);
            /*
             * The key may not be found, which is acceptable.  There
             * can be multiple signatures, each preceded by a key.
             */
        } else if (tlv.it_type == EXPECTED_SIG_TLV) {
            /* Ignore this signature if it is out of bounds. */
            if (key_id < 0 || key_id >= bootutil_key_cnt) {
                key_id = -1;
                continue;
            }
            if (!EXPECTED_SIG_LEN(tlv.it_len) || tlv.it_len > sizeof(buf)) {
                return -1;
            }
            rc = flash_area_read(fap, off + sizeof(tlv), buf, tlv.it_len);
            if (rc) {
                return -1;
            }
            rc = bootutil_verify_sig(hash, sizeof(hash), buf, tlv.it_len, key_id);
            if (rc == 0) {
                valid_signature = 1;
            }
            key_id = -1;
#endif
        }
    }

    if (!sha256_valid) {
        return -1;
    }

#ifdef EXPECTED_SIG_TLV
    if (!valid_signature) {
        return -1;
    }
#endif

    return 0;
}
