/* flash_otp_keystore.c
 *
 * Implementation for Flash based OTP keystore used as trust anchor
 *
 *
 * Copyright (C) 2024 wolfSSL Inc.
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

#include <stdint.h>
#include <string.h>
#include "wolfboot/wolfboot.h"
#include "keystore.h"
#include "hal.h"
#include "otp_keystore.h"

#if defined(FLASH_OTP_ROT) && !defined(WOLFBOOT_NO_SIGN)

int keystore_num_pubkeys(void)
{
    uint8_t otp_header[OTP_HDR_SIZE];
    struct wolfBoot_otp_hdr *hdr = (struct wolfBoot_otp_hdr *)otp_header;
    if (hal_flash_otp_read(FLASH_OTP_BASE, (void *)otp_header, OTP_HDR_SIZE) != 0)
        return 0;
    if (memcmp(hdr->keystore_hdr_magic, KEYSTORE_HDR_MAGIC, 8) != 0) {
        return 0;
    }
    if (hdr->item_count > KEYSTORE_MAX_PUBKEYS)
        return 0;
    return hdr->item_count;
}

static uint8_t otp_slot_item_cache[SIZEOF_KEYSTORE_SLOT];

uint8_t *keystore_get_buffer(int id)
{
    struct keystore_slot *slot;
    if (id >= keystore_num_pubkeys())
        return (uint8_t *)0;
    if (hal_flash_otp_read(FLASH_OTP_BASE +
                OTP_HDR_SIZE + id * SIZEOF_KEYSTORE_SLOT, otp_slot_item_cache,
                SIZEOF_KEYSTORE_SLOT) != 0)
        return (uint8_t *)0;
    slot = (struct keystore_slot *)otp_slot_item_cache;
    return slot->pubkey;
}

int keystore_get_size(int id)
{
    struct keystore_slot *slot;
    if (id >= keystore_num_pubkeys())
        return -1;
    if (hal_flash_otp_read(FLASH_OTP_BASE +
                OTP_HDR_SIZE + id * SIZEOF_KEYSTORE_SLOT, otp_slot_item_cache,
                SIZEOF_KEYSTORE_SLOT) != 0)
        return -1;
    slot = (struct keystore_slot *)otp_slot_item_cache;
    return slot->pubkey_size;
}

uint32_t keystore_get_mask(int id)
{
    struct keystore_slot *slot;
    if (id >= keystore_num_pubkeys())
        return 0;
    if (hal_flash_otp_read(FLASH_OTP_BASE +
                OTP_HDR_SIZE + id * SIZEOF_KEYSTORE_SLOT, otp_slot_item_cache,
                SIZEOF_KEYSTORE_SLOT) != 0)
        return 0;
    slot = (struct keystore_slot *)otp_slot_item_cache;
    return slot->part_id_mask;
}

uint32_t keystore_get_key_type(int id)
{
    struct keystore_slot *slot;
    if (id >= keystore_num_pubkeys())
        return -1;
    if (hal_flash_otp_read(FLASH_OTP_BASE +
                OTP_HDR_SIZE + id * SIZEOF_KEYSTORE_SLOT, otp_slot_item_cache,
                SIZEOF_KEYSTORE_SLOT) != 0)
        return -1;
    slot = (struct keystore_slot *)otp_slot_item_cache;
    return slot->key_type;
}


#endif /* FLASH_OTP_ROT && !WOLFBOOT_NO_SIGN */
