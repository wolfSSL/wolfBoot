/* keys.c
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

#include <bootutil/sign_key.h>

#if defined(BOOT_SIGN_RSA)
#define HAVE_KEYS
extern const unsigned char rsa_pub_key[];
extern unsigned int rsa_pub_key_len;
#elif defined(BOOT_SIGN_EC256)
#define HAVE_KEYS
extern const unsigned char ecdsa_pub_key[];
extern unsigned int ecdsa_pub_key_len;
#elif defined(BOOT_SIGN_ED25519)
#define HAVE_KEYS
extern const unsigned char ed25519_pub_key[];
extern unsigned int ed25519_pub_key_len;
#else
#error "No public key available for given signing algorithm."
#endif

#if defined(HAVE_KEYS)
const struct bootutil_key bootutil_keys[] = {
    {
#if defined(BOOT_SIGN_RSA)
        .key = rsa_pub_key,
        .len = &rsa_pub_key_len,
#elif defined(BOOT_SIGN_EC256)
        .key = ecdsa_pub_key,
        .len = &ecdsa_pub_key_len,
#elif defined(BOOT_SIGN_ED25519)
        .key = ed25519_pub_key,
        .len = &ed25519_pub_key_len,
#endif
    },
};
const int bootutil_key_cnt = 1;
#endif
