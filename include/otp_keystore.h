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
#if defined(TARGET_stm32h7)
    #include "hal/stm32h7.h"
#elif defined(TARGET_stm32h5)
    #include "hal/stm32h5.h"
#elif defined(TARGET_sim)
    #include "hal_host_sim_stub.h"
#else
    #error "Define a target"
#endif

#include "keystore.h"

#define OTP_HDR_SIZE 16

#if (defined(__IAR_SYSTEMS_ICC__) && (__IAR_SYSTEMS_ICC__ > 8)) || \
        defined(__GNUC__)
    #define KEYSTORE_HDR_PACKED __attribute__((packed))
#else
    #define KEYSTORE_HDR_PACKED
#endif

#if !defined(OTP_SIZE) || (OTP_SIZE <= 0)
    /* See TARGET_[device] */
    #error "WRONG OTP SIZE"
#endif

#ifndef SIZEOF_KEYSTORE_SLOT
    #error "SIZEOF_KEYSTORE_SLOT must be defined"
#endif

#if (OTP_HDR_SIZE >= OTP_SIZE)
    #error "Bad OTP_HDR_SIZE or OTP_SIZE"
#endif

#define KEYSTORE_MAX_PUBKEYS ((OTP_SIZE - OTP_HDR_SIZE) / SIZEOF_KEYSTORE_SLOT)

#if (KEYSTORE_MAX_PUBKEYS < 1)
    #error "No space for any keystores in OTP with current algorithm"
#endif

struct KEYSTORE_HDR_PACKED wolfBoot_otp_hdr {
    char keystore_hdr_magic[8];
    uint16_t item_count;
    uint16_t flags;
    uint32_t version;
};

/* KEYSTORE_HDR_MAGIC = "WOLFBOOT" exactly 8 bytes, no nul terminator */
static const char KEYSTORE_HDR_MAGIC[8] = { 'W','O','L','F','B','O','O','T' };

#endif /* FLASH_OTP_KEYSTORE */

#endif /* OTP_KEYSTORE_H */
