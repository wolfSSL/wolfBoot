/* encrypt.h
 *
 * Functions to encrypt/decrypt external flash content
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

#ifndef ENCRYPT_H_INCLUDED
#define ENCRYPT_H_INCLUDED
#ifdef __WOLFBOOT
#include <stdint.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>

#include "target.h"
#include "wolfboot/wolfboot.h"

#ifdef ENCRYPT_WITH_CHACHA
#include <wolfssl/wolfcrypt/chacha.h>
#else
#include <wolfssl/wolfcrypt/aes.h>
#endif

#include <wolfssl/wolfcrypt/pwdbased.h>

#ifdef ENCRYPT_WITH_CHACHA

extern ChaCha chacha;

#define crypto_init() chacha_init()
#define crypto_encrypt(eb,b,sz) wc_Chacha_Process(&chacha, eb, b, sz)
#define crypto_decrypt(db,b,sz) wc_Chacha_Process(&chacha, db, b, sz)
#define crypto_set_iv(n, iv) wc_Chacha_SetIV(&chacha, n, iv)

int chacha_init(void);

#elif defined(ENCRYPT_WITH_AES128) || defined(ENCRYPT_WITH_AES256)

extern Aes aes_dec, aes_enc;

#define crypto_init() aes_init()
#define crypto_encrypt(eb,b,sz) wc_AesCtrEncrypt(&aes_enc, eb, b, sz)
#define crypto_decrypt(db,b,sz) wc_AesCtrEncrypt(&aes_dec, db, b, sz)
#define crypto_set_iv(n,a) aes_set_iv(n, a)

int aes_init(void);
void aes_set_iv(uint8_t *nonce, uint32_t address);
#endif /* ENCRYPT_WITH_CHACHA */

/* Internal read/write functions (not exported in the libwolfboot API) */
int ext_flash_encrypt_write(uintptr_t address, const uint8_t *data, int len);
int ext_flash_decrypt_read(uintptr_t address, uint8_t *data, int len);

#endif /* __WOLFBOOT */
#endif /* ENCRYPT_H_INCLUDED */
