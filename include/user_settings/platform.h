/* user_settings/platform.h
 *
 * Platform-specific bits that aren't tied to a wolfBoot feature: the
 * SP-math word-size selection (driven by SP_ECC / SP_RSA having been
 * enabled by a SIGN fragment), QNX, and STM32 PKA.
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
#ifndef _WOLFBOOT_USER_SETTINGS_PLATFORM_H_
#define _WOLFBOOT_USER_SETTINGS_PLATFORM_H_

/* If SP math is enabled determine word size */
#if defined(WOLFSSL_HAVE_SP_ECC) || defined(WOLFSSL_HAVE_SP_RSA)
#  ifdef __aarch64__
#    define HAVE___UINT128_T
#    define WOLFSSL_SP_ARM64_ASM
#    define SP_WORD_SIZE 64
#  elif defined(ARCH_RISCV64)
#    define HAVE___UINT128_T
#    define SP_WORD_SIZE 64
#  elif defined(ARCH_x86_64) && !defined(FORCE_32BIT)
#    define HAVE___UINT128_T
#    define SP_WORD_SIZE 64
#    if !defined(NO_ASM)
#      define WOLFSSL_SP_X86_64_ASM
#    endif
#  else
#    define SP_WORD_SIZE 32
#  endif

   /* SP Math needs to understand long long */
#  ifndef ULLONG_MAX
#    define ULLONG_MAX 18446744073709551615ULL
#  endif
#endif

#ifdef __QNX__
#  define WOLFSSL_HAVE_MIN
#  define WOLFSSL_HAVE_MAX
#endif

#ifdef WOLFSSL_STM32_PKA
#  define HAVE_UINTPTR_T /* make sure stdint.h is included */
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_PLATFORM_H_ */
