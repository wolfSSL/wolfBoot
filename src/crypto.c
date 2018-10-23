/* crypto.c
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
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <bootutil.h>
#include <bootutil/sign_key.h>
#include <bootutil/image.h>
#include <wolfssl/ssl.h>

#if defined BOOT_SIGN_RSA
#   error RSA signature not supported yet on standalone
#elif defined BOOT_SIGN_EC256
#include <wolfssl/wolfcrypt/ecc.h>

#define ECC_KEY_SIZE  32
#define ECC_KEY_CURVE ECC_SECP256R1

int
bootutil_verify_sig(uint8_t *hash, uint32_t hlen, uint8_t *sig, size_t slen,
  uint8_t key_id)
{
    int rc;
    uint8_t *pubkey, *end;
    ecc_key ec;
    int res;
    pubkey = (uint8_t *)bootutil_keys[key_id].key;
    end = pubkey + *bootutil_keys[key_id].len;
    rc = wc_ecc_init(&ec);
    if (rc < 0) {
        /* Failed to initialize key */
        return -1;
    }
    rc = wc_ecc_import_x963_ex(pubkey, ED25519_KEY_SIZE, &ec, ECC_KEY_CURVE);
    if (rc < 0) {
        /* Failed to import ECC key */
        return -1;
    }
    rc = wc_ecc_verify_hash(sig, slen, hash, hlen, &res, &ec);
    if ((rc < 0) || (res == 0)) {
        return -1;
    }
    return 0;
}
#elif defined BOOT_SIGN_ED25519
#include <wolfssl/wolfcrypt/ed25519.h>
int
bootutil_verify_sig(uint8_t *hash, uint32_t hlen, uint8_t *sig, size_t slen,
  uint8_t key_id)
{
    int rc;
    uint8_t *pubkey, *end;
    ed25519_key ed;
    int res;
    pubkey = (uint8_t *)bootutil_keys[key_id].key;
    end = pubkey + *bootutil_keys[key_id].len;
    rc = wc_ed25519_init(&ed);
    if (rc < 0) {
        /* Failed to initialize key */
        return -1;
    }
    rc = wc_ed25519_import_public(pubkey, ED25519_KEY_SIZE, &ed);
    if (rc < 0) {
        /* Failed to import ed25519 key */
        return -1;
    }
    rc = wc_ed25519_verify_msg(sig, slen, hash, hlen, &res, &ed);
    if ((rc < 0) || (res == 0)) {
        return -1;
    }
    return 0;
}

#endif

