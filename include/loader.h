/* loader.h
 *
 * Public key information for the signed images
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

#ifndef LOADER_H
#define LOADER_H

#if defined(WOLFBOOT_SIGN_ED25519)
    extern const unsigned char ed25519_pub_key[];
    extern unsigned int ed25519_pub_key_len;
#   define KEY_BUFFER  ed25519_pub_key
#   define KEY_LEN     ed25519_pub_key_len
#   define IMAGE_SIGNATURE_SIZE (64)
#elif defined(WOLFBOOT_SIGN_ECC256)
    extern const unsigned char ecc256_pub_key[];
    extern unsigned int ecc256_pub_key_len;
#   define KEY_BUFFER  ecc256_pub_key
#   define KEY_LEN     ecc256_pub_key_len
#   define IMAGE_SIGNATURE_SIZE (64)
#elif defined(WOLFBOOT_SIGN_RSA2048)
    extern const unsigned char rsa2048_pub_key[];
    extern unsigned int rsa2048_pub_key_len;
#   define KEY_BUFFER  rsa2048_pub_key
#   define KEY_LEN     rsa2048_pub_key_len
#   define IMAGE_SIGNATURE_SIZE (256)
#elif defined(WOLFBOOT_SIGN_RSA4096)
    extern const unsigned char rsa4096_pub_key[];
    extern unsigned int rsa4096_pub_key_len;
#   define KEY_BUFFER  rsa4096_pub_key
#   define KEY_LEN     rsa4096_pub_key_len
#   define IMAGE_SIGNATURE_SIZE (512)
#else
#   error "No public key available for given signing algorithm."
#endif /* Algorithm selection */

#ifdef WOLFBOOT_TPM
    int wolfBoot_tpm2_init(void);
#endif

void wolfBoot_start(void);
#endif /* LOADER_H */
