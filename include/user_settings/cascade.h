/* user_settings/cascade.h
 *
 * Lift Make-side feature implications into preprocessor cascades so an
 * IDE/CMake-only build (which sets only the high-level WOLFBOOT_* flags)
 * sees the same derived flags that options.mk would set.
 *
 * Idempotent: every #define is #ifndef-guarded, so it's a no-op when
 * options.mk has already emitted the same -D flag.
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
#ifndef _WOLFBOOT_USER_SETTINGS_CASCADE_H_
#define _WOLFBOOT_USER_SETTINGS_CASCADE_H_

/* Any feature that requires a hardware TPM 2.0 implies WOLFBOOT_TPM.
 * Mirrors options.mk:34-92 where the same Make variables force WOLFTPM:=1. */
#if defined(WOLFBOOT_TPM_VERIFY)   || \
    defined(WOLFBOOT_MEASURED_BOOT) || \
    defined(WOLFBOOT_TPM_KEYSTORE) || \
    defined(WOLFBOOT_TPM_SEAL)
#  ifndef WOLFBOOT_TPM
#    define WOLFBOOT_TPM
#  endif
#endif

/* WOLFBOOT_NEEDS_* declarations -- positive intent markers reconciled by
 * user_settings/finalize.h. Fragments may also set these from their own
 * headers; cascade.h handles the cases that today live as #undef blocks
 * scattered through user_settings.h. */

/* WOLFCRYPT_TZ_PSA and WOLFBOOT_TZ_FWTPM both keep CMAC and KDF enabled
 * (today by `#undef NO_CMAC` / `#undef NO_KDF` after the always-on block).
 * Lift those to positive intent so finalize.h can simply skip the
 * `#define NO_CMAC` / `#define NO_KDF`. */
#if defined(WOLFCRYPT_TZ_PSA) || defined(WOLFBOOT_TZ_FWTPM)
#  ifndef WOLFBOOT_NEEDS_CMAC
#    define WOLFBOOT_NEEDS_CMAC
#  endif
#  ifndef WOLFBOOT_NEEDS_KDF
#    define WOLFBOOT_NEEDS_KDF
#  endif
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_CASCADE_H_ */
