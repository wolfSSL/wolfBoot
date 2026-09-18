/* secure_handoff.h
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef WOLFBOOT_SECURE_HANDOFF_H
#define WOLFBOOT_SECURE_HANDOFF_H

#include <stddef.h>
#include <stdint.h>

/* The record fixes the measurement to SHA-256, so consumers include this
 * header freely. Only the bootloader build that copies boot->sha_hash must
 * have wolfBoot configured for SHA-256. */
#if defined(WOLFBOOT_SECURE_APP) && !defined(WOLFBOOT_HASH_SHA256)
    #error "WOLFBOOT_SECURE_APP handoff currently requires SHA-256"
#endif

#if defined(WOLFBOOT_SECURE_APP) && !defined(WOLFBOOT_SECURE_HANDOFF_ADDRESS)
    #error "WOLFBOOT_SECURE_APP requires WOLFBOOT_SECURE_HANDOFF_ADDRESS (the secure-RAM address the port reserves for the boot handoff record)"
#endif

#define WOLFBOOT_SECURE_HANDOFF_MAGIC        0x5742484Fu
#define WOLFBOOT_SECURE_HANDOFF_VERSION      1u
#define WOLFBOOT_SECURE_HANDOFF_HASH_SHA256  1u
#define WOLFBOOT_SECURE_HANDOFF_DIGEST_SIZE  32u

#define WOLFBOOT_SECURE_HANDOFF_LIFECYCLE_UNKNOWN              0x0000u
#define WOLFBOOT_SECURE_HANDOFF_LIFECYCLE_ASSEMBLY_AND_TEST    0x1000u
#define WOLFBOOT_SECURE_HANDOFF_LIFECYCLE_PSA_ROT_PROVISIONING 0x2000u
#define WOLFBOOT_SECURE_HANDOFF_LIFECYCLE_SECURED              0x3000u
#define WOLFBOOT_SECURE_HANDOFF_LIFECYCLE_NON_PSA_ROT_DEBUG    0x4000u
#define WOLFBOOT_SECURE_HANDOFF_LIFECYCLE_RECOVERABLE_DEBUG    0x5000u

typedef struct wolfBoot_secure_handoff {
    uint32_t magic;
    uint32_t magic_inverse;
    uint16_t version;
    uint16_t size;
    uint32_t lifecycle;
    uint32_t image_version;
    uint16_t hash_algorithm;
    uint16_t measurement_size;
    uint8_t measurement[WOLFBOOT_SECURE_HANDOFF_DIGEST_SIZE];
} wolfBoot_secure_handoff_t;

static inline int wolfBoot_secure_handoff_build(
    volatile wolfBoot_secure_handoff_t* handoff,
    const uint8_t* measurement, uint32_t imageVersion, uint32_t lifecycle)
{
    uint32_t i;

    if ((handoff == NULL) || (measurement == NULL)) {
        return -1;
    }

    handoff->magic = 0u;
    handoff->magic_inverse = UINT32_MAX;
    handoff->version = WOLFBOOT_SECURE_HANDOFF_VERSION;
    handoff->size = (uint16_t)sizeof(*handoff);
    handoff->lifecycle = lifecycle;
    handoff->image_version = imageVersion;
    handoff->hash_algorithm = WOLFBOOT_SECURE_HANDOFF_HASH_SHA256;
    handoff->measurement_size = WOLFBOOT_SECURE_HANDOFF_DIGEST_SIZE;
    for (i = 0u; i < WOLFBOOT_SECURE_HANDOFF_DIGEST_SIZE; ++i) {
        handoff->measurement[i] = measurement[i];
    }
#if defined(__arm__) || defined(__thumb__)
    __asm volatile("dmb" ::: "memory");
#else
    __asm volatile("" ::: "memory");
#endif
    handoff->magic_inverse = ~WOLFBOOT_SECURE_HANDOFF_MAGIC;
    handoff->magic = WOLFBOOT_SECURE_HANDOFF_MAGIC;
#if defined(__arm__) || defined(__thumb__)
    __asm volatile("dsb" ::: "memory");
#else
    __asm volatile("" ::: "memory");
#endif

    return 0;
}

#endif /* WOLFBOOT_SECURE_HANDOFF_H */
