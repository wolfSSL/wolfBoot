/* mpfs250_snvm.h
 *
 * sNVM keystore layout and PUF KEK interface for hal/mpfs250_snvm.c.
 *
 * Copyright (C) 2026 wolfSSL Inc.
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

#ifndef MPFS250_SNVM_H
#define MPFS250_SNVM_H

#if (defined(SNVM_KEYSTORE) || defined(SNVM_KEYSTORE_PROVISION)) && \
        !defined(WOLFBOOT_NO_SIGN)

#include "keystore.h"
#include "hal/mpfs250.h"

#define SNVM_KEYSTORE_HDR_SIZE 16

#if defined(__GNUC__)
    #define SNVM_KEYSTORE_PACKED __attribute__((packed))
#else
    #define SNVM_KEYSTORE_PACKED
#endif

/* On-sNVM image: this header at logical offset 0, then item_count packed
 * keystore_slot entries.  Same layout family as the OTP keystore. */
struct SNVM_KEYSTORE_PACKED snvm_keystore_hdr {
    char     magic[8];
    uint16_t item_count;
    uint16_t flags;
    uint32_t version;
};

static const char SNVM_KEYSTORE_MAGIC[8] =
    {'W', 'O', 'L', 'F', 'B', 'O', 'O', 'T'};

/* First sNVM module holding the keystore image (override per board/config).
 * Defaults high to leave low modules for a future bootmode-2 sNVM boot image. */
#ifndef SNVM_KEYSTORE_MODULE
#define SNVM_KEYSTORE_MODULE 200
#endif

/* Modules the image may span (provisioning buffer bound).  One page holds two
 * ECC384 keys; an ML-DSA-87 key needs 11.  Every page in the range must be
 * runtime-writable (not marked ROM) in the Libero design for the on-device
 * provisioning writer; a production bitstream can instead ship them as ROM
 * pages, which SNVM_KEYSTORE_REQUIRE_ROM then insists on at boot. */
#ifndef SNVM_KEYSTORE_MAX_MODULES
#define SNVM_KEYSTORE_MAX_MODULES 1
#endif
#if (SNVM_KEYSTORE_MODULE < 0) || (SNVM_KEYSTORE_MAX_MODULES < 1) || \
    ((SNVM_KEYSTORE_MODULE + SNVM_KEYSTORE_MAX_MODULES) > MPFS_SNVM_MODULE_MAX)
#error "sNVM keystore module range must lie within modules 0..220"
#endif

/* Plaintext page payload.  Public keys are not secret; their integrity rests
 * on whatever protects the wolfBoot image itself.  Authenticated sNVM is a
 * future option. */
#define SNVM_KEYSTORE_PAGE_DATA MPFS_SNVM_PLAIN_DATA_LEN

/* Upper bound on a trusted item_count, from the usable keystore capacity.
 * SIZEOF_KEYSTORE_SLOT comes from keystore.h, which the user includes first. */
#define SNVM_KEYSTORE_MAX_PUBKEYS \
    ((((SNVM_KEYSTORE_MAX_MODULES) * (SNVM_KEYSTORE_PAGE_DATA)) - \
      (SNVM_KEYSTORE_HDR_SIZE)) / (SIZEOF_KEYSTORE_SLOT))

#ifdef SNVM_KEYSTORE_PROVISION
/* Write the compiled-in keystore (keystore.c) into sNVM.  Run once. */
int snvm_keystore_provision(void);
#endif

#endif /* SNVM_KEYSTORE || SNVM_KEYSTORE_PROVISION */

#ifdef SNVM_KEK

#include <stdint.h>
#include "hal/mpfs250.h"

/* Device-unique KEK: the first 256 bits of a SHA-384 digest. */
#define SNVM_KEK_LEN          32

/* AES-256 key wrapped with the PUF KEK: RFC 3394 adds 8 bytes of overhead. */
#define SNVM_KEK_AESKEY_LEN   32
#define SNVM_KEK_WRAPPED_LEN  (SNVM_KEK_AESKEY_LEN + 8)

/* sNVM module holding the wrapped image-encryption key (override per board).
 * Must not fall inside the keystore's module range, and must be runtime-writable
 * in the Libero design: on the Video Kit only pages 200 and 201 are. */
#ifndef SNVM_ENCKEY_MODULE
#define SNVM_ENCKEY_MODULE    201
#endif
#if (SNVM_ENCKEY_MODULE < 0) || (SNVM_ENCKEY_MODULE >= MPFS_SNVM_MODULE_MAX)
#error "SNVM_ENCKEY_MODULE is outside the sNVM module range 0..220"
#endif
#if defined(SNVM_KEYSTORE) || defined(SNVM_KEYSTORE_PROVISION)
#if (SNVM_ENCKEY_MODULE >= SNVM_KEYSTORE_MODULE) && \
    (SNVM_ENCKEY_MODULE < (SNVM_KEYSTORE_MODULE + SNVM_KEYSTORE_MAX_MODULES))
#error "SNVM_ENCKEY_MODULE overlaps the sNVM keystore module range"
#endif
#endif

/* Derive the 256-bit device-unique KEK from the System Controller SRAM-PUF:
 * KEK = SHA384(label || PUF(fixed-challenge))[0..31].  Returns 0 on success. */
int mpfs_puf_kek(uint8_t *kek);

/* AES key-wrap (RFC 3394) using the PUF KEK.  out needs keysz+8 bytes.
 * Returns the wrapped length (keysz+8) on success, < 0 on error. */
int snvm_kek_wrap(const uint8_t *key, uint32_t keysz, uint8_t *out,
    uint32_t outsz);

/* AES key-unwrap using the PUF KEK.  key needs insz-8 bytes.
 * Returns the unwrapped length (insz-8) on success, < 0 on error. */
int snvm_kek_unwrap(const uint8_t *in, uint32_t insz, uint8_t *key,
    uint32_t keysz);


#ifdef SNVM_ENCKEY_PROVISION
/* Provided by hal/mpfs250_snvm.c: wrap the compiled-in AES key with
 * the PUF KEK and store [wrapped key][nonce] in sNVM. */
int snvm_enckey_provision(void);
#endif

#endif /* SNVM_KEK */

#if defined(SNVM_ENCKEY_PROVISION) && !defined(SNVM_KEK)
#error "SNVM_ENCKEY_PROVISION needs SNVM_KEK (the key is wrapped with the PUF KEK)"
#endif

#endif /* MPFS250_SNVM_H */
