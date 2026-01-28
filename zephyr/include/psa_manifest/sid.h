/* sid.h
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

#ifndef PSA_MANIFEST_SID_H_
#define PSA_MANIFEST_SID_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Service IDs and handles aligned with ARM TEE defaults. */
#define ARM_TEE_ATTESTATION_SERVICE_SID                 (0x00000020U)
#define ARM_TEE_ATTESTATION_SERVICE_VERSION             (1U)
#define ARM_TEE_ATTESTATION_SERVICE_HANDLE              (4U)

#define ARM_TEE_PLATFORM_SERVICE_SID                    (0x00000040U)
#define ARM_TEE_PLATFORM_SERVICE_VERSION                (1U)
#define ARM_TEE_PLATFORM_SERVICE_HANDLE                 (6U)

#define ARM_TEE_PROTECTED_STORAGE_SERVICE_SID           (0x00000060U)
#define ARM_TEE_PROTECTED_STORAGE_SERVICE_VERSION       (1U)
#define ARM_TEE_PROTECTED_STORAGE_SERVICE_HANDLE        (2U)

#define ARM_TEE_INTERNAL_TRUSTED_STORAGE_SERVICE_SID    (0x00000070U)
#define ARM_TEE_INTERNAL_TRUSTED_STORAGE_SERVICE_VERSION (1U)
#define ARM_TEE_INTERNAL_TRUSTED_STORAGE_SERVICE_HANDLE (3U)

#define ARM_TEE_CRYPTO_SID                              (0x00000080U)
#define ARM_TEE_CRYPTO_VERSION                          (1U)
#define ARM_TEE_CRYPTO_HANDLE                           (1U)

#ifdef __cplusplus
}
#endif

#endif /* PSA_MANIFEST_SID_H_ */
