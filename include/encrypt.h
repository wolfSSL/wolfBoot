/* encrypt.h
 *
 * Functions to encrypt/decrypt external flash content
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

#ifndef ENCRYPT_H_INCLUDED
#define ENCRYPT_H_INCLUDED

#if defined(__WOLFBOOT) || defined(UNIT_TEST) || defined(MMU)

#include <stdint.h>
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/sha256.h"

#include "target.h"
#include "wolfboot/wolfboot.h"

#ifdef ENCRYPT_WITH_CHACHA
    #include "wolfssl/wolfcrypt/chacha.h"
#else
    #include "wolfssl/wolfcrypt/aes.h"
#endif

#include "wolfssl/wolfcrypt/pwdbased.h"

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

#elif defined(ENCRYPT_PKCS11)
#include "wolfboot/wcs_pkcs11.h"

int pkcs11_crypto_init(void);
void pkcs11_crypto_set_iv(uint8_t *nonce, uint32_t iv_ctr);
int pkcs11_crypto_encrypt(uint8_t *out, uint8_t *in, size_t size);
int pkcs11_crypto_decrypt(uint8_t *out, uint8_t *in, size_t size);
void pkcs11_crypto_deinit(void);

#define crypto_init() pkcs11_crypto_init()
#define crypto_encrypt(eb,b,sz) pkcs11_crypto_encrypt(eb, b, sz)
#define crypto_decrypt(db,b,sz) pkcs11_crypto_decrypt(db, b, sz)
#define crypto_set_iv(n,a) pkcs11_crypto_set_iv(n, a)

#endif /* ENCRYPT_WITH_CHACHA */

/* external flash encryption read/write functions */
int ext_flash_encrypt_write(uintptr_t address, const uint8_t *data, int len);
int ext_flash_decrypt_read(uintptr_t address, uint8_t *data, int len);

#ifdef EXT_ENCRYPTED
int wolfBoot_enable_fallback_iv(int enable);
void wolfBoot_crypto_set_iv(const uint8_t *nonce, uint32_t iv_counter);
#endif

#endif /* __WOLFBOOT || UNIT_TEST || MMU */
#endif /* ENCRYPT_H_INCLUDED */
