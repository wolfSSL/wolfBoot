# CMakeLists.txt
#
# Copyright (C) 2025 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfBoot is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

# check for FASTMATH or SP_MATH
if(SPMATH)
    list(APPEND MATH_SOURCES wolfssl/wolfcrypt/src/sp_int.c)
else()
    message(STATUS "Using fast math")
    list(APPEND WOLFCRYPT_DEFS USE_FAST_MATH)
    list(APPEND MATH_SOURCES wolfssl/wolfcrypt/src/integer.c wolfssl/wolfcrypt/src/tfm.c)
endif()

if(ARCH STREQUAL "ARM")
    # Cortex-M CPU
    if(${WOLFBOOT_TARGET} STREQUAL "stm32l0")
        if(SPMATH)
            if(NO_ASM)
                list(APPEND MATH_SOURCES wolfssl/wolfcrypt/src/sp_c32.c)
            else()
                list(APPEND WOLFCRYPT_DEFS WOLFSSL_SP_ASM WOLFSSL_SP_ARM_THUMB_ASM)
                list(APPEND MATH_SOURCES wolfssl/wolfcrypt/src/sp_armthumb.c)
            endif()
        endif()
    else()
        if(NO_ASM)
            if(SPMATH)
                list(APPEND MATH_SOURCES wolfssl/wolfcrypt/src/sp_c32.c)
            endif()
        else()
            if(SPMATH)
                list(APPEND WOLFCRYPT_DEFS WOLFSSL_SP_ASM WOLFSSL_SP_ARM_CORTEX_M_ASM)
                list(APPEND MATH_SOURCES wolfssl/wolfcrypt/src/sp_cortexm.c)
            endif()
        endif()
    endif()
endif()

if(SIGN STREQUAL "ECC256")
    list(
        APPEND
        WOLFCRYPT_SOURCES
        ${MATH_SOURCES}
        wolfssl/wolfcrypt/src/ecc.c
        wolfssl/wolfcrypt/src/memory.c
        wolfssl/wolfcrypt/src/wc_port.c
        wolfssl/wolfcrypt/src/wolfmath.c
        wolfssl/wolfcrypt/src/hash.c)
endif()

if(SIGN STREQUAL "ECC384")
    list(
        APPEND
        WOLFCRYPT_SOURCES
        ${MATH_SOURCES}
        wolfssl/wolfcrypt/src/ecc.c
        wolfssl/wolfcrypt/src/memory.c
        wolfssl/wolfcrypt/src/wc_port.c
        wolfssl/wolfcrypt/src/wolfmath.c
        wolfssl/wolfcrypt/src/hash.c)
endif()

if(SIGN STREQUAL "ECC521")
    list(
        APPEND
        WOLFCRYPT_SOURCES
        ${MATH_SOURCES}
        wolfssl/wolfcrypt/src/ecc.c
        wolfssl/wolfcrypt/src/memory.c
        wolfssl/wolfcrypt/src/wc_port.c
        wolfssl/wolfcrypt/src/wolfmath.c
        wolfssl/wolfcrypt/src/hash.c)
endif()

if(SIGN STREQUAL "ED25519")
    list(
        APPEND
        WOLFCRYPT_SOURCES
        wolfssl/wolfcrypt/src/sha512.c
        wolfssl/wolfcrypt/src/ed25519.c
        wolfssl/wolfcrypt/src/ge_low_mem.c
        wolfssl/wolfcrypt/src/hash.c
        wolfssl/wolfcrypt/src/wolfmath.c
        wolfssl/wolfcrypt/src/wc_port.c
        wolfssl/wolfcrypt/src/fe_low_mem.c)
endif()

if(SIGN STREQUAL "ED448")
    list(
        APPEND
        WOLFCRYPT_SOURCES
        wolfssl/wolfcrypt/src/ed448.c
        wolfssl/wolfcrypt/src/ge_low_mem.c
        wolfssl/wolfcrypt/src/ge_448.c
        wolfssl/wolfcrypt/src/fe_448.c
        wolfssl/wolfcrypt/src/hash.c
        wolfssl/wolfcrypt/src/wolfmath.c
        wolfssl/wolfcrypt/src/wc_port.c
        wolfssl/wolfcrypt/src/fe_low_mem.c)

    if(NOT HASH STREQUAL "SHA3")
        list(APPEND WOLFCRYPT_SOURCES wolfssl/wolfcrypt/src/sha3.c)
    endif()
endif()

if(SIGN STREQUAL "RSA2048")
    list(
        APPEND
        WOLFCRYPT_SOURCES
        ${RSA_EXTRA_SOURCES}
        ${MATH_SOURCES}
        wolfssl/wolfcrypt/src/rsa.c
        wolfssl/wolfcrypt/src/asn.c
        wolfssl/wolfcrypt/src/hash.c
        wolfssl/wolfcrypt/src/wc_port.c
        wolfssl/wolfcrypt/src/wolfmath.c)
endif()

if(SIGN STREQUAL "RSA4096")
    list(
        APPEND
        WOLFCRYPT_SOURCES
        ${RSA_EXTRA_SOURCES}
        ${MATH_SOURCES}
        wolfssl/wolfcrypt/src/rsa.c
        wolfssl/wolfcrypt/src/asn.c
        wolfssl/wolfcrypt/src/hash.c
        wolfssl/wolfcrypt/src/wc_port.c
        wolfssl/wolfcrypt/src/wolfmath.c)
endif()

if(ENCRYPT)
    if(ENCRYPT_WITH_AES128)
        list(APPEND WOLFSSL_DEFS WOLFSSL_AES_COUNTER WOLFSSL_AES_DIRECT WOLFSSL_AES_128)
        list(APPEND WOLFCRYPT_SOURCES wolfssl/wolfcrypt/src/aes.c)
    elseif(ENCRYPT_WITH_AES256)
        list(APPEND WOLFSSL_DEFS WOLFSSL_AES_COUNTER WOLFSSL_AES_DIRECT WOLFSSL_AES_256)
        list(APPEND WOLFCRYPT_SOURCES wolfssl/wolfcrypt/src/aes.c)
    else()
        set(ENCRYPT_WITH_CHACHA ON)
        list(APPEND WOLFCRYPT_SOURCES wolfssl/wolfcrypt/src/chacha.c)
        list(APPEND WOLFSSL_DEFS HAVE_CHACHA)
    endif()
endif()

if(HASH STREQUAL "SHA384")
    if(NOT SIGN STREQUAL "ED25519")
        list(APPEND WOLFCRYPT_SOURCES wolfssl/wolfcrypt/src/sha512.c)
    endif()
endif()

if(HASH STREQUAL "SHA3")
    list(APPEND WOLFCRYPT_SOURCES wolfssl/wolfcrypt/src/sha3.c)
endif()

if(NOT WOLFBOOT_SMALL_STACK AND WOLFBOOT_TARGET STREQUAL "unit_test")
    list(APPEND WOLFCRYPT_SOURCES wolfssl/wolfcrypt/src/memory.c)
    list(REMOVE_DUPLICATES WOLFCRYPT_SOURCES)
endif()

# Include SHA256 module because it's implicitly needed by RSA
list(APPEND WOLFCRYPT_SOURCES wolfssl/wolfcrypt/src/sha256.c)

list(APPEND WOLFCRYPT_INCLUDE_DIRS PUBLIC ${CMAKE_CURRENT_LIST_DIR}/wolfssl
     ${WOLFBOOT_ROOT}/include)

# generate target for wolfcrypt
add_library(wolfcrypt)
target_sources(wolfcrypt PRIVATE ${WOLFCRYPT_SOURCES})
target_include_directories(wolfcrypt PUBLIC ${WOLFCRYPT_INCLUDE_DIRS})
target_link_libraries(wolfcrypt target user_settings)

target_compile_definitions(
    wolfcrypt
    PUBLIC WOLFSSL_USER_SETTINGS
    PRIVATE ${WOLFCRYPT_DEFS} ${SIGN_OPTIONS})

if(WOLFBOOT_SMALL_STACK)
    target_compile_definitions(wolfcrypt PRIVATE WOLFBOOT_SMALL_STACK XMALLOC_USER)
endif()

target_compile_options(wolfcrypt PRIVATE -Wno-unused -Wno-array-bounds)
