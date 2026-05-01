/* user_settings/test_bench.h
 *
 * wolfCrypt configuration when WOLFCRYPT_TEST or WOLFCRYPT_BENCHMARK is
 * active. These flags re-enable features that the production wolfBoot
 * build strips for code size.
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
#ifndef _WOLFBOOT_USER_SETTINGS_TEST_BENCH_H_
#define _WOLFBOOT_USER_SETTINGS_TEST_BENCH_H_

#ifdef WOLFCRYPT_TEST
#  ifdef WOLFSSL_NXP_LPC55S69_WITH_HWACCEL
     /* lpc55s69 hashcrypt hw does not support interleaving */
#    define NO_WOLFSSL_SHA256_INTERLEAVE
#  endif
   /* Skip extended tests to save memory */
#  define NO_CRYPT_TEST_EXTENDED
   /* Use smaller certificate buffers */
#  define USE_CERT_BUFFERS_256
   /* Override default NO_CRYPT_TEST */
#  undef NO_CRYPT_TEST
#else
#  define NO_CRYPT_TEST
#endif

#ifdef WOLFCRYPT_BENCHMARK
   /* Embedded benchmark mode */
#  ifndef BENCH_EMBEDDED
#    define BENCH_EMBEDDED
#  endif
   /* Override default NO_CRYPT_BENCHMARK */
#  undef NO_CRYPT_BENCHMARK
#else
#  define NO_CRYPT_BENCHMARK
#endif

/* Common optimizations when test/benchmark enabled */
#if defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
#  define NO_WRITE_TEMP_FILES

   /* Use printf for wolfSSL logging (redirected to UART via syscalls.c) */
#  define WOLFSSL_LOG_PRINTF

   /* Use static memory pool to avoid system malloc dependency.
    * benchmark.c provides gBenchMemory static buffer.
    * Default is 50KB with BENCH_EMBEDDED, override for smaller targets */
#  ifndef WOLFSSL_STATIC_MEMORY
#    define WOLFSSL_STATIC_MEMORY
#  endif
#  ifndef WOLFSSL_STATIC_MEMORY_TEST_SZ
#    define WOLFSSL_STATIC_MEMORY_TEST_SZ (10 * 1024)
#  endif
#  define WOLFSSL_STATIC_MEMORY_LEAN

   /* Enable SP math digit operations */
#  define WOLFSSL_SP_MUL_D

   /* User time functions provided */
#  define WOLFSSL_USER_CURRTIME
#  define XTIME my_time
   extern unsigned long my_time(unsigned long* timer);
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_TEST_BENCH_H_ */
