/* otp_keystore.h
 *
 * Helper for storing/retrieving Trust Anchor to/from OTP flash
 *
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


#ifndef OTP_KEYSTORE_H
#define OTP_KEYSTORE_H

#if defined(FLASH_OTP_KEYSTORE) && !defined(WOLFBOOT_NO_SIGN)
/* Specific includes for supported targets
 * (needed for OTP_SIZE)
 */
#ifdef TARGET_stm32h7
    #include "hal/stm32h7.h"
#elif defined TARGET_stm32h5
    #include "hal/stm32h5.h"
#endif

#include "keystore.h"

#define OTP_HDR_SIZE 16

#if (defined(__IAR_SYSTEMS_ICC__) && (__IAR_SYSTEMS_ICC__ > 8)) || \
        defined(__GNUC__)
    #define KEYSTORE_HDR_PACKED __attribute__((packed))
#else
    #define KEYSTORE_HDR_PACKED
#endif

struct KEYSTORE_HDR_PACKED wolfBoot_otp_hdr {
    char keystore_hdr_magic[8];
    uint16_t item_count;
    uint16_t flags;
    uint32_t version;
};

static const char KEYSTORE_HDR_MAGIC[8] = "WOLFBOOT";

#define KEYSTORE_MAX_PUBKEYS \
    ((OTP_SIZE - OTP_UDS_STORAGE_SIZE - OTP_HDR_SIZE) / SIZEOF_KEYSTORE_SLOT)

#define OTP_UDS_LEN 32
/* Reserve the upper 64 bytes of OTP for the attestation UDS. */
#define OTP_UDS_STORAGE_SIZE 64
#define OTP_UDS_OFFSET (OTP_SIZE - OTP_UDS_STORAGE_SIZE)

#if (OTP_SIZE == 0)
#error WRONG OTP SIZE
#endif

#if (KEYSTORE_MAX_PUBKEYS < 1)
    #error "No space for any keystores in OTP with current algorithm"
#endif

#if (OTP_UDS_OFFSET < OTP_HDR_SIZE)
    #error "OTP UDS offset overlaps OTP keystore header"
#endif

#endif /* FLASH_OTP_KEYSTORE */ 

#endif /* OTP_KEYSTORE_H */
