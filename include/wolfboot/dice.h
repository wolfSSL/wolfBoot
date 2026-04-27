/* dice.h
 *
 * DICE helpers and PSA attestation token builder.
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

#ifndef WOLFBOOT_DICE_H
#define WOLFBOOT_DICE_H

#include <stddef.h>
#include <stdint.h>

/* Component name used in the DICE claims for the wolfBoot measurement.
 * Shared between dice.c (producer) and HAL (consumer) to keep them in sync. */
#define WOLFBOOT_DICE_COMPONENT_WOLFBOOT  "wolfboot"
#define WOLFBOOT_DICE_COMPONENT_BOOTIMAGE "boot-image"

#ifdef __cplusplus
extern "C" {
#endif

int wolfBoot_dice_get_token(const uint8_t *challenge,
                            size_t challenge_size,
                            uint8_t *token_buf,
                            size_t token_buf_size,
                            size_t *token_size);

int wolfBoot_dice_get_token_size(size_t challenge_size, size_t *token_size);

/* Retrieve the IAK public key as a 65-byte X9.63 uncompressed point
 * (0x04 || X[32] || Y[32]).  Must be called after wolfBoot_dice_get_token().
 * The internal copy is zeroized after this call (read-once).
 * Not available when WOLFBOOT_ATTESTATION_IAK is set (provisioned key is used). */
#ifndef WOLFBOOT_ATTESTATION_IAK
int wolfBoot_dice_get_attest_pubkey(uint8_t *buf, size_t *len);
#endif /* !WOLFBOOT_ATTESTATION_IAK */

#ifdef __cplusplus
}
#endif

#endif /* WOLFBOOT_DICE_H */
