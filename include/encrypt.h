/* encrypt.h
 *
 * Functions to encrypt/decrypt external flash content
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

#ifndef ENCRYPT_H_INCLUDED
#define ENCRYPT_H_INCLUDED
#include <stdint.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>

#include "target.h"
#include "wolfboot/wolfboot.h"

#include <wolfssl/wolfcrypt/chacha.h>
#include <wolfssl/wolfcrypt/pwdbased.h>

#define ENCRYPT_BLOCK_SIZE 16 
#define ENCRYPT_KEY_SIZE 32 /* Chacha20-256 */

int ext_flash_set_encrypt_key(const uint8_t *key, int len);
int ext_flash_set_encrypt_password(const uint8_t *pwd, int len);
int ext_flash_encrypt_write(uintptr_t address, const uint8_t *data, int len);
int ext_flash_decrypt_read(uintptr_t address, uint8_t *data, int len);

#endif /* ENCRYPT_H_INCLUDED */
