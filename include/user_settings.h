/* user_settings.h
 *
 * Custom configuration for wolfCrypt/wolfSSL.
 * Enabled via WOLFSSL_USER_SETTINGS.
 *
 * This is a thin shim that dispatches to fragment headers in
 * include/user_settings/. Each fragment is responsible for one
 * concern (a SIGN family, a HASH algorithm, a wolfBoot feature, or
 * the final reconciliation); fragments only #define things, and the
 * order of inclusion (cascade -> base -> sign -> hash -> features ->
 * finalize) is the contract between them.
 *
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
#ifndef _WOLFBOOT_USER_SETTINGS_H_
#define _WOLFBOOT_USER_SETTINGS_H_

#ifdef WOLFBOOT_PKCS11_APP
# include "test-app/wcs/user_settings.h"
#else

#include <target.h>

/* 1. Cascade: lift Make-side feature implications into header-side cascades
 *    (e.g. WOLFBOOT_TPM_KEYSTORE/SEAL/VERIFY/MEASURED_BOOT -> WOLFBOOT_TPM,
 *    WOLFCRYPT_TZ_PSA/WOLFBOOT_TZ_FWTPM -> NEEDS_CMAC/NEEDS_KDF). */
#include "user_settings/cascade.h"

/* 2. Foundation: alignment, threading, stdlib types, basic sizing. */
#include "user_settings/base.h"

/* 3. SIGN-family fragments (one or two per SIGN/SIGN_SECONDARY). */
#include "user_settings/sign_dispatch.h"

/* 4. HASH-family fragments (SHA-256 is the foundation default and needs
 *    no fragment; SHA3-384 / SHA-384 each have one). */
#include "user_settings/hash_dispatch.h"

/* 5. Optional feature fragments. Each is a no-op unless its WOLFBOOT_*
 *    or wolfCrypt-side gate is set. */
#include "user_settings/encrypt.h"
#include "user_settings/trustzone.h"
#include "user_settings/tpm.h"
#include "user_settings/wolfhsm.h"
#include "user_settings/cert_chain.h"
#include "user_settings/renesas.h"
#include "user_settings/platform.h"
#include "user_settings/test_bench.h"

/* 6. Reconciliation: NEEDS_* markers -> NO_* / WC_NO_* defaults,
 *    plus the always-on global wolfCrypt "off" block and memory model.
 *    Must be last so every fragment has had a chance to declare needs. */
#include "user_settings/finalize.h"

#endif /* WOLFBOOT_PKCS11_APP */
#endif /* _WOLFBOOT_USER_SETTINGS_H_ */
