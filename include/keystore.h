/* keystore.h
 *
 * API's for key store
 *
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

#ifndef KEYSTORE_H
#define KEYSTORE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef KEYSTORE_PUBKEY_SIZE
    /* allow building version for external API use */
    #define KEYSTORE_ANY
    #define KEYSTORE_PUBKEY_SIZE 576 /* Max is RSA 4096 */
#endif

struct keystore_slot {
    uint32_t slot_id;
    uint32_t key_type;
    uint32_t part_id_mask;
    uint32_t pubkey_size;
    uint8_t  pubkey[KEYSTORE_PUBKEY_SIZE];
};

/* KeyStore API */
int keystore_num_pubkeys(void);
#if defined(WOLFBOOT_RENESAS_SCEPROTECT) ||\
    defined(WOLFBOOT_RENESAS_TSIP) ||\
    defined(WOLFBOOT_RENESAS_RSIP)
    uint32_t *keystore_get_buffer(int id);
#else
    uint8_t  *keystore_get_buffer(int id);
#endif
int keystore_get_size(int id);
uint32_t keystore_get_key_type(int id);
uint32_t keystore_get_mask(int id);


#ifdef __cplusplus
}
#endif

#endif /* KEYSTORE_H */
