/* wolfboot.h
 *
 * The wolfBoot API definitions.
 *
 * Copyright (C) 2019 wolfSSL Inc.
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


#ifndef WOLFBOOT_H
#define WOLFBOOT_H

#include <stdint.h>
#include "target.h"


#ifndef IMAGE_HEADER_SIZE
#  define IMAGE_HEADER_SIZE 256
#endif
#define IMAGE_HEADER_OFFSET (2 * sizeof(uint32_t))

#define WOLFBOOT_MAGIC          0x464C4F57 /* WOLF */
#define WOLFBOOT_MAGIC_TRAIL    0x544F4F42 /* BOOT */

#define HDR_END         0x00
#define HDR_VERSION     0x01
#define HDR_TIMESTAMP   0x02
#define HDR_SHA256      0x03
#define HDR_IMG_TYPE    0x04
#define HDR_PUBKEY      0x10
#define HDR_SIGNATURE   0x20
#define HDR_PADDING     0xFF

#define HDR_IMG_TYPE_AUTH_ED25519 0x0100
#define HDR_IMG_TYPE_AUTH_ECC256  0x0200
#define HDR_IMG_TYPE_AUTH_RSA2048 0x0300
#define HDR_IMG_TYPE_WOLFBOOT     0x0000
#define HDR_IMG_TYPE_APP          0x0001


#ifdef __WOLFBOOT
 #if defined(WOLFBOOT_SIGN_ED25519)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_ED25519
 #elif defined(WOLFBOOT_SIGN_ECC256)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_ECC256
 #elif defined(WOLFBOOT_SIGN_RSA2048)
 #   define HDR_IMG_TYPE_AUTH HDR_IMG_TYPE_AUTH_RSA2048
 #else
 #   error "no valid authentication mechanism selected. Please define WOLFBOOT_SIGN_ED25519 or WOLFBOOT_SIGN_ECC256 or WOLFBOOT_SIGN_RSA2048"
 #endif /* defined WOLFBOOT_SIGN_ECC256 || WOLFBOOT_SIGN_ED25519 */
#endif /* defined WOLFBOOT */

#define PART_BOOT   0
#define PART_UPDATE 1
#define PART_SWAP   2

#define IMG_STATE_NEW 0xFF
#define IMG_STATE_UPDATING 0x70
#define IMG_STATE_TESTING 0x10
#define IMG_STATE_SUCCESS 0x00


void wolfBoot_erase_partition(uint8_t part);
void wolfBoot_update_trigger(void);
void wolfBoot_success(void);
uint32_t wolfBoot_get_image_version(uint8_t part);
uint16_t wolfBoot_get_image_type(uint8_t part);
#define wolfBoot_current_firmware_version() wolfBoot_get_image_version(PART_BOOT)
#define wolfBoot_update_firmware_version() wolfBoot_get_image_version(PART_UPDATE)

#endif /* !WOLFBOOT_H */
