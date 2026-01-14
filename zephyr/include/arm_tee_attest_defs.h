/* arm_tee_attest_defs.h
 *
 * ARM TEE attestation message IDs.
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

#ifndef WOLFBOOT_ARM_TEE_ATTEST_DEFS_H_
#define WOLFBOOT_ARM_TEE_ATTEST_DEFS_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Initial Attestation message types that distinguish Attest services. */
#define ARM_TEE_ATTEST_GET_TOKEN       1001
#define ARM_TEE_ATTEST_GET_TOKEN_SIZE  1002

#ifdef __cplusplus
}
#endif

#endif /* WOLFBOOT_ARM_TEE_ATTEST_DEFS_H_ */
