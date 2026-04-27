/* initial_attestation.h
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

#ifndef PSA_INITIAL_ATTESTATION_H
#define PSA_INITIAL_ATTESTATION_H

#include <stddef.h>
#include <stdint.h>

#include "psa/error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PSA_INITIAL_ATTEST_API_VERSION_MAJOR (1)
#define PSA_INITIAL_ATTEST_API_VERSION_MINOR (0)

#define PSA_INITIAL_ATTEST_CHALLENGE_SIZE_32 (32u)
#define PSA_INITIAL_ATTEST_CHALLENGE_SIZE_48 (48u)
#define PSA_INITIAL_ATTEST_CHALLENGE_SIZE_64 (64u)

psa_status_t psa_initial_attest_get_token(const uint8_t *auth_challenge,
					  size_t challenge_size,
					  uint8_t *token_buf,
					  size_t token_buf_size,
					  size_t *token_size);

psa_status_t psa_initial_attest_get_token_size(size_t challenge_size,
					       size_t *token_size);

/* This function retrieves the public key for the IAK generated during
 * the most recent psa_initial_attest_get_token call. The public key is
 * returned in X9.63 uncompressed format (0x04 || X || Y). The internal
 * copy of the public key is zeroized after this call (read-once).
 * Not available when WOLFBOOT_ATTESTATION_IAK is set (provisioned key is used). */
#ifndef WOLFBOOT_ATTESTATION_IAK
psa_status_t psa_initial_attest_get_iak_pubkey(uint8_t *buf,
						size_t buf_size,
						size_t *key_len);
#endif /* !WOLFBOOT_ATTESTATION_IAK */

#ifdef __cplusplus
}
#endif

#endif /* PSA_INITIAL_ATTESTATION_H */
