# CMakeLists.txt
#
# Copyright (C) 2022 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
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
#
# Usage:
# When building with CMake, you'll have to specify build parameters normally
# located in the .config file
#
# $ mkdir build
# $ cd build
# $ cmake -DWOLFBOOT_TARGET=<target> -DWOLFBOOT_PARTITION_BOOT_ADDRESS=<boot_address>
#   -DWOLFBOOT_PARTITION_SIZE=<size> -DWOLFBOOT_PARTITION_UPDATE_ADDRESS=<update_address>
#   -DWOLFBOOT_PARTITION_SWAP_ADDRESS=<swap_address> -DBUILD_TEST_APPS=yes ..
# $ cmake --build .

cmake_minimum_required(VERSION 3.16)

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(
        FATAL_ERROR
            "In-source builds are not allowed.\
     Run cmake from a separate directory from where CMakeLists.txt lives.\
     NOTE: cmake will now create CMakeCache.txt and CMakeFiles/*.\
     You must delete them, or cmake will refuse to work.")
endif()

project(wolfBoot)

include(cmake/utils.cmake)
include(cmake/functions.cmake)

if(NOT DEFINED WOLFBOOT_TARGET)
    message(FATAL_ERROR "WOLFBOOT_TARGET must be defined")
else()
    message(STATUS "Building for ${WOLFBOOT_TARGET}")
endif()

if(NOT DEFINED WOLFBOOT_SECTOR_SIZE)
    message(FATAL_ERROR "WOLFBOOT_SECTOR_SIZE must be defined")
endif()

if(NOT DEFINED ARM_TARGETS)
    list(APPEND ARM_TARGETS stm32h7 stm32l0 stm32f4 stm32u5)
    set(ARM_TARGETS
        "${ARM_TARGETS}"
        CACHE INTERNAL "")
endif()

# check if WOLFBOOT_TARGET is a cache variable
if(NOT DEFINED CACHE{WOLFBOOT_TARGET})
    set(WOLFBOOT_TARGET
        "${WOLFBOOT_TARGET}"
        CACHE INTERNAL "Target platform")
endif()

if(${WOLFBOOT_TARGET} IN_LIST ARM_TARGETS)
    set(ARCH ARM)
elseif(WOLFBOOT_TARGET STREQUAL "x86_64_efi")
    set(ARCH x86_64)
elseif(WOLFBOOT_TARGET STREQUAL "sim")
    set(ARCH sim)
else()
    message(FATAL_ERROR "Unable to configure ARCH for target ${WOLFBOOT_TARGET}")
endif()

add_option("ALLOW_DOWNGRADE" "Allow downgrading firmware (default: disabled)" "no" "yes;no")
add_option("DELTA_UPDATES" "Allow incremental updates (default: disabled)" "no" "yes;no")
add_option(
    "DISABLE_BACKUP"
    "Disable backup copy of running firmware upon update installation (default: disabled)" "no"
    "yes;no")
add_option("ENCRYPT" "Encrypt external flash (default: disabled)" "no" "yes;no")
add_option("ENCRYPT_WITH_AES128" "Encrypt external flash with AES128 (default: disabled)" "no"
           "yes;no")
add_option("ENCRYPT_WITH_AES256" "Encrypt external flash with AES256 (default: disabled)" "no"
           "yes;no")
add_option("ENCRYPT_WITH_CHACHA" "Encrypt external flash with CHACHA (default: disabled)" "no"
           "yes;no")
add_option("EXT_FLASH" "Enable optional support for external flash memory (default: disabled)" "no"
           "yes;no")
add_option(
    "FLAGS_HOME"
    "Store UPDATE partition flags in a sector in the BOOT partition (default: disabled)" "no"
    "yes;no")
add_option("HASH" "Set the hash algorithm (default: SHA256)" "SHA256" "SHA3;SHA256;SHA384")
add_option("NO_ASM" "Don't use algorithms implemented in assembly code (default: disabled)" "no"
           "yes;no")
add_option("NO_MPU" "Disable MPU code (default: disabled)" "no" "yes;no")
add_option("NO_XIP" "Disable execute-in-place (default: disabled)" "no" "yes;no")
add_option(
    "NVM_FLASH_WRITEONCE"
    "Enable the workaround for 'write once' internal flash (default: disabled)" "no" "yes;no")
add_option(
    "RAM_CODE"
    "Move all code accessing internal flash for writing into a section in RAM (default: disabled)"
    "no" "yes;no")
add_option("SIGN" "Configure Digital Signatures Algorithm (default: ECC256)" "ECC256"
           "ECC256;ECC384;ECC521;ED25519;ED448;NONE;RSA2048;RSA4096")
add_option("SPI_FLASH" "Use external SPI flash drivers (default: disabled)" "no" "yes;no")
add_option("QSPI_FLASH" "Use external QSPI flash drivers (default: disabled)" "no" "yes;no")
add_option("OCTOSPI_FLASH" "Use external OCTOSPI flash drivers (default: disabled)" "no" "yes;no")
add_option("SPMATH" "Use SP Math (default: disabled)" "no" "yes;no")
add_option("WOLFBOOT_TARGET" "Target platform to build for (default: stm32h7)" "stm32h7"
           "stm32f4;stm32h7;stm32l0;stm32u5;x86_64_efi;sim")
add_option("UART_FLASH" "Use external UART flash drivers (default: disabled)" "no" "yes;no")
add_option(
    "WOLFBOOT_SMALL_STACK"
    "Use a fixed-size memory pool created at compile time for cryptography implementation (default: disabled)"
    "no"
    "yes;no")
add_option("DEBUG_UART" "Enable trace debugging over a UART (default: disabled)" "no" "yes;no")
add_option("BUILD_TEST_APPS" "Build the wolfBoot test apps (default: disabled)" "no" "yes;no")
add_option("PYTHON_KEYTOOLS" "Use wolfCrypt-py for key generation and signing (default: disabled)"
           "no" "yes;no")
add_option(
    "PULL_LINKER_DEFINES"
    "Pull partition addresses from the linker script instead of defining fixed addresses in target.h (default: disabled)"
    "no"
    "yes;no")

# If PULL_LINKER_DEFINES is set, partition addresses and size info will come from the linker script,
# so no need to pass them on the command line. These variables do need to be set for building the
# test apps.
if(NOT DEFINED PULL_LINKER_DEFINES AND NOT DEFINED BUILD_TEST_APPS)
    if(NOT DEFINED WOLFBOOT_PARTITION_SIZE)
        message(FATAL_ERROR "WOLFBOOT_PARTITION_SIZE must be defined")
    endif()

    if(NOT DEFINED WOLFBOOT_PARTITION_BOOT_ADDRESS)
        message(FATAL_ERROR "WOLFBOOT_PARTITION_BOOT_ADDRESS must be defined")
    endif()

    if(NOT DEFINED WOLFBOOT_PARTITION_UPDATE_ADDRESS)
        message(FATAL_ERROR "WOLFBOOT_PARTITION_UPDATE_ADDRESS must be defined")
    endif()

    if(NOT DEFINED WOLFBOOT_PARTITION_SWAP_ADDRESS)
        message(FATAL_ERROR "WOLFBOOT_PARTITION_SWAP_ADDRESS must be defined")
    endif()
endif()

# unset cache variables Variables that need to be accessed by the gen_wolfboot_platform_target cmake
# function called from the parent cmake project are added to the cache so that they can be accessed
# anywhere in the project
unset(WOLFBOOT_DEFS CACHE)
unset(WOLFBOOT_SOURCES CACHE)
unset(WOLFBOOT_INCLUDE_DIRS CACHE)
unset(WOLFBOOT_COMPILE_OPTIONS CACHE)
unset(WOLFBOOT_LINK_OPTIONS CACHE)
unset(WOLFBOOT_PLATFORM_LD_SCRIPT CACHE)
unset(WOLFBOOT_SIGNING_PRIVATE_KEY CACHE)
unset(SIM_COMPILE_OPTIONS CACHE)
unset(SIGN_TOOL CACHE)
unset(SIGN_OPTIONS CACHE)
unset(KEYTOOL_OPTIONS CACHE)
unset(BINASSEMBLE CACHE)
unset(ARCH_FLASH_OFFSET CACHE)
unset(WOLFBOOT_VERSION CACHE)

if(EXTRA_DEFS)
    string(REPLACE " " ";" WOLFBOOT_EXTRA_DEFS ${EXTRA_DEFS})
    list(APPEND WOLFBOOT_DEFS ${WOLFBOOT_EXTRA_DEFS})
endif()

set(WOLFBOOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND WOLFBOOT_DEFS __WOLFBOOT)

set(WOLFBOOT_SOURCES include/loader.h include/image.h src/string.c src/image.c)

list(APPEND WOLFBOOT_SOURCES src/loader.c)

# build bin-assemble tool
set(BINASSEMBLE ${CMAKE_CURRENT_BINARY_DIR}/bin-assemble)
add_custom_command(
    OUTPUT ${BINASSEMBLE}
    COMMAND gcc tools/bin-assemble/bin-assemble.c -o ${BINASSEMBLE}
    WORKING_DIRECTORY ${WOLFBOOT_ROOT}
    COMMENT "Generating bin-assemble tool")

add_custom_target(binAssemble DEPENDS ${BINASSEMBLE})

# -----------------------------------------------------------------------------
# Toolchain Specifications
# -----------------------------------------------------------------------------

if(ARCH STREQUAL "ARM")
    include(cmake/toolchain_arm-none-eabi.cmake)
elseif(ARCH STREQUAL "AARCH64")
    include(cmake/toolchain_aarch64-none-elf.cmake)
endif()

# -----------------------------------------------------------------------------
# Architecture/CPU configuration
# -----------------------------------------------------------------------------
set(UPDATE_SOURCES src/update_flash.c)

# Default flash offset
if(NOT DEFINED ARCH_FLASH_OFFSET)
    set(ARCH_FLASH_OFFSET 0x0)
endif()

if(ARCH STREQUAL "x86_64")
    list(APPEND WOLFBOOT_SOURCES src/boot_x86_64.c)
    if(DEBUG)
        add_compile_definitions(WOLFBOOT_DEBUG_EFI=1)
    endif()
endif()

if(ARCH STREQUAL "ARM")
    list(APPEND WOLFBOOT_SOURCES src/boot_arm.c)
    list(APPEND WOLFBOOT_DEFS ARCH_ARM)
    list(APPEND WOLFBOOT_COMPILE_OPTIONS -ffreestanding -nostartfiles -fomit-frame-pointer)
    list(APPEND WOLFBOOT_LINK_OPTIONS -ffreestanding -nostartfiles -fomit-frame-pointer)

    if(${WOLFBOOT_TARGET} STREQUAL "stm32l0")
        set(FLAGS_INVERT ON)
    endif()

    if(${WOLFBOOT_TARGET} STREQUAL "stm32f4")
        set(ARCH_FLASH_OFFSET 0x08000000)
        set(WOLFBOOT_ORIGIN ${ARCH_FLASH_OFFSET})
    endif()

    if(${WOLFBOOT_TARGET} STREQUAL "stm32u5")
        set(ARCH_FLASH_OFFSET 0x08000000)
        set(WOLFBOOT_ORIGIN ${ARCH_FLASH_OFFSET})
    endif()

    if(${WOLFBOOT_TARGET} STREQUAL "stm32h7")
        set(ARCH_FLASH_OFFSET 0x08000000)
        set(WOLFBOOT_ORIGIN ${ARCH_FLASH_OFFSET})
    endif()
endif()

if(ARCH STREQUAL "AARCH64")
    list(APPEND WOLFBOOT_SOURCES src/boot_aarch64.c src/boot_aarch64_start.c src/update_ram.c)
    list(APPEND WOLFBOOT_DEFS ARCH_AARCH64 NO_QNX WOLFBOOT_DUALBOOT MMU)

    if(SPMATH)
        list(APPEND WOLFBOOT_SOURCES lib/wolfssl/wolfcrypt/src/sp_c32.c)
    endif()

endif()

list(APPEND WOLFBOOT_DEFS ARCH_FLASH_OFFSET=${ARCH_FLASH_OFFSET})

if(${WOLFBOOT_TARGET} STREQUAL "x86_64_efi")
    if(NOT DEFINED GNU_EFI_LIB_PATH)
        set(GNU_EFI_LIB_PATH /usr/lib)
    endif()

    set(GNU_EFI_CRT0 "${GNU_EFI_LIB_PATH}/crt0-efi-x86_64.c")
    set(GNU_EFI_LSCRIPT "${GNU_EFI_LIB_PATH}/elf_x86_64_efi.lds")
    include_directories("/usr/include/efi" "/usr/include/efi/x86_64")
    add_compile_definitions("PLATFORM_X86_64_EFI")
    set(CMAKE_EXE_LINKER_FLAGS "-shared -Bsymbolic -L/usr/lib -T${GNU_EFI_LSCRIPT}")
    set(LD_START_GROUP ${GNU_EFI_CRT0})
    set(LD_END_GROUP "-lgnuefi -lefi")
    set(UPDATE_SOURCES src/update_ram.c)
endif()

# -----------------------------------------------------------------------------
# DSA Settings
# -----------------------------------------------------------------------------
if(SIGN STREQUAL "NONE")
    list(APPEND KEYTOOL_OPTIONS --no-sign)
    message(STATUS "Image signing disabled")
    set(WOLFBOOT_SIGNING_PRIVATE_KEY
        ""
        CACHE INTERNAL "")
    set(STACK_USAGE 1216)
    list(APPEND SIGN_OPTIONS WOLFBOOT_NO_SIGN)
else()
    set(WOLFBOOT_SIGNING_PRIVATE_KEY ${CMAKE_CURRENT_BINARY_DIR}/wolfboot_signing_private_key.der)
endif()

if(NOT DEFINED IMAGE_HEADER_SIZE)
    set(IMAGE_HEADER_SIZE 256)
endif()

if(WOLFBOOT_SMALL_STACK)
    list(APPEND USER_SETTINGS WOLFBOOT_SMALL_STACK)
    list(APPEND WOLFBOOT_DEFS XMALLOC_USER)
    set(STACK_USAGE 4096)
    list(APPEND WOLFBOOT_SOURCES src/xmalloc.c)
endif()

if(SIGN STREQUAL "ECC256")
    message(STATUS "Signing image using ${SIGN}")
    set(DSA ecc256)
    list(APPEND KEYTOOL_OPTIONS --ecc256)

    list(APPEND SIGN_OPTIONS WOLFBOOT_SIGN_ECC256)

    if(WOLFBOOT_SMALL_STACK)
        set(STACK_USAGE 4096)
    elseif(NOT SPMATH)
        set(STACK_USAGE 5008)
    else()
        set(STACK_USAGE 3952)
    endif()

    if(${IMAGE_HEADER_SIZE} LESS 256)
        set(IMAGE_HEADER_SIZE 256)
    endif()
endif()

if(SIGN STREQUAL "ECC384")
    message(STATUS "Signing image using ${SIGN}")
    set(DSA ecc384)
    list(APPEND KEYTOOL_OPTIONS --ecc384)
    list(APPEND SIGN_OPTIONS WOLFBOOT_SIGN_ECC384)

    if(WOLFBOOT_SMALL_STACK)
        set(STACK_USAGE 5880)
    elseif(NOT SPMATH)
        set(STACK_USAGE 11248)
    else()
        set(STACK_USAGE 5880)
    endif()

    if(${IMAGE_HEADER_SIZE} LESS 512)
        set(IMAGE_HEADER_SIZE 512)
    endif()
endif()

if(SIGN STREQUAL "ECC521")
    message(STATUS "Signing image using ${SIGN}")
    set(DSA ecc521)
    list(APPEND KEYTOOL_OPTIONS --ecc521)
    list(APPEND SIGN_OPTIONS WOLFBOOT_SIGN_ECC521)

    if(WOLFBOOT_SMALL_STACK)
        set(STACK_USAGE 4096)
    elseif(NOT SPMATH)
        set(STACK_USAGE 7352)
    else()
        set(STACK_USAGE 3896)
    endif()

    if(${IMAGE_HEADER_SIZE} LESS 512)
        set(IMAGE_HEADER_SIZE 512)
    endif()
endif()

if(SIGN STREQUAL "ED25519")
    message(STATUS "Signing image using ${SIGN}")
    set(DSA ed25519)
    list(APPEND KEYTOOL_OPTIONS --ed25519)
    list(APPEND SIGN_OPTIONS WOLFBOOT_SIGN_ED25519)

    if(NOT DEFINED STACK_USAGE)
        set(STACK_USAGE 1180)
    endif()

    if(${IMAGE_HEADER_SIZE} LESS 256)
        set(IMAGE_HEADER_SIZE 256)
    endif()
endif()

if(SIGN STREQUAL "ED448")
    message(STATUS "Signing image using ${SIGN}")
    set(DSA ed448)
    list(APPEND KEYTOOL_OPTIONS --ed448)

    if(WOLFBOOT_SMALL_STACK)
        if(NOT DEFINED STACK_USAGE)
            set(STACK_USAGE 1024)
        endif()
    else()
        if(NOT DEFINED STACK_USAGE)
            set(STACK_USAGE 4376)
        endif()
    endif()

    list(APPEND SIGN_OPTIONS WOLFBOOT_SIGN_ED448)

    if(${IMAGE_HEADER_SIZE} LESS 512)
        set(IMAGE_HEADER_SIZE 512)
    endif()
endif()

if(SIGN STREQUAL "RSA2048")
    message(STATUS "Signing image using ${SIGN}")
    set(DSA rsa2048)
    list(APPEND KEYTOOL_OPTIONS --rsa2048)
    list(APPEND SIGN_OPTIONS WOLFBOOT_SIGN_RSA2048 ${RSA_EXTRA_CFLAGS})

    if(WOLFBOOT_SMALL_STACK)
        if(NOT SPMATH)
            set(STACK_USAGE 5008)
        else()
            set(STACK_USAGE 4096)
        endif()
    elseif(NOT SPMATH)
        set(STACK_USAGE 35952)
    else()
        set(STACK_USAGE 12288)
    endif()

    if(${IMAGE_HEADER_SIZE} LESS 512)
        set(IMAGE_HEADER_SIZE 512)
    endif()
endif()

if(SIGN STREQUAL "RSA4096")
    message(STATUS "Signing image using ${SIGN}")
    set(DSA rsa4096)
    list(APPEND KEYTOOL_OPTIONS --rsa4096)
    list(APPEND SIGN_OPTIONS WOLFBOOT_SIGN_RSA4096 ${RSA_EXTRA_CFLAGS})

    if(WOLFBOOT_SMALL_STACK)
        if(NOT SPMATH)
            set(STACK_USAGE 5888)
        else()
            set(STACK_USAGE 4096)
        endif()
    elseif(NOT SPMATH)
        set(STACK_USAGE 69232)
    else()
        set(STACK_USAGE 18064)
    endif()

    if(${IMAGE_HEADER_SIZE} LESS 1024)
        set(IMAGE_HEADER_SIZE 1024)
    endif()
endif()

list(APPEND WOLFBOOT_DEFS IMAGE_HEADER_SIZE=${IMAGE_HEADER_SIZE})

# Append sign options to compile definitions
list(APPEND WOLFBOOT_DEFS ${SIGN_OPTIONS})

list(APPEND WOLFBOOT_COMPILE_OPTIONS -Wstack-usage=${STACK_USAGE} -Wno-unused)

if(PULL_LINKER_DEFINES)
    list(APPEND WOLFBOOT_DEFS PULL_LINKER_DEFINES)
endif()

if(RAM_CODE)
    list(APPEND WOLFBOOT_DEFS RAM_CODE)
endif()

if(FLAGS_HOME)
    list(APPEND WOLFBOOT_DEFS FLAGS_HOME=1)
endif()

if(FLAGS_INVERT)
    list(APPEND WOLFBOOT_DEFS WOLFBOOT_FLAGS_INVERT=1)
endif()

if(SPI_FLASH)
    set(EXT_FLASH ON)
endif()

if(OCTOSPI_FLASH)
    set(QSPI_FLASH ON)
endif()

if(QSPI_FLASH)
    set(EXT_FLASH ON)
endif()

if(UART_FLASH)
    set(EXT_FLASH ON)
endif()

if(ENCRYPT)
    list(APPEND USER_SETTINGS EXT_ENCRYPTED=1)
    if(ENCRYPT_WITH_AES128)
        list(APPEND WOLFBOOT_DEFS ENCRYPT_WITH_AES128)
    elseif(ENCRYPT_WITH_AES256)
        list(APPEND WOLFBOOT_DEFS ENCRYPT_WITH_AES256)
    else()
        set(ENCRYPT_WITH_CHACHA ON)
        list(APPEND WOLFBOOT_DEFS ENCRYPT_WITH_CHACHA HAVE_CHACHA)
    endif()
endif()

if(EXT_FLASH)
    list(APPEND WOLFBOOT_DEFS EXT_FLASH=1 PART_UPDATE_EXT=1 PART_SWAP_EXT=1)
    if(NO_XIP)
        list(APPEND WOLFBOOT_DEFS PART_BOOT_EXT=1)
    endif()
endif()

if(ALLOW_DOWNGRADE)
    list(APPEND WOLFBOOT_DEFS ALLOW_DOWNGRADE)
endif()

if(NVM_FLASH_WRITEONCE)
    list(APPEND WOLFBOOT_DEFS NVM_FLASH_WRITEONCE)
endif()

if(DISABLE_BACKUP)
    list(APPEND WOLFBOOT_DEFS DISABLE_BACKUP)
endif()

if(NO_MPU)
    list(APPEND WOLFBOOT_DEFS WOLFBOOT_NO_MPU)
endif()

if(NOT DEFINED WOLFBOOT_VERSION)
    set(WOLFBOOT_VERSION 1)
endif()

list(APPEND WOLFBOOT_DEFS WOLFBOOT_VERSION=${WOLFBOOT_VERSION})

if(DELTA_UPDATES)
    list(APPEND WOLFBOOT_SOURCES src/delta.c)
    list(APPEND WOLFBOOT_DEFS DELTA_UPDATES)
    if(NOT DEFINED DELTA_BLOCK_SIZE)
        list(APPEND WOLFBOOT_DEFS DELTA_BLOCK_SIZE=${DELTA_BLOCK_SIZE})
    endif()
endif()

if(ARMORED)
    list(APPEND WOLFBOOT_DEFS WOLFBOOT_ARMORED)
endif()

list(APPEND WOLFBOOT_SOURCES ${UPDATE_SOURCES})

list(TRANSFORM WOLFBOOT_SOURCES PREPEND ${WOLFBOOT_ROOT}/)

# -----------------------------------------------------------------------------
# Hash settings
# -----------------------------------------------------------------------------
if(HASH STREQUAL "SHA256")
    list(APPEND WOLFBOOT_DEFS WOLFBOOT_HASH_SHA256)
    message(STATUS "Using SHA256 hash")
endif()

if(HASH STREQUAL "SHA384")
    list(APPEND WOLFBOOT_DEFS WOLFBOOT_HASH_SHA384)
    list(APPEND KEYTOOL_OPTIONS --sha384)
endif()

if(HASH STREQUAL "SHA3")
    list(APPEND WOLFBOOT_DEFS WOLFBOOT_HASH_SHA3_384)
    list(APPEND KEYTOOL_OPTIONS --sha3)
endif()

# -----------------------------------------------------------------------------
# wolfboot HAL
# -----------------------------------------------------------------------------

# Default SPI driver name
set(SPI_TARGET ${WOLFBOOT_TARGET})

# Default UART driver name
set(UART_TARGET ${WOLFBOOT_TARGET})

set(SPI_DRV_STM32_TARGETS
    "stm32l0"
    "stm32f4"
    "stm32l4"
    "stm32f7"
    "stm32h7"
    "stm32wb"
    "stm32u5")

if(${WOLFBOOT_TARGET} IN_LIST SPI_DRV_STM32_TARGETS)
    set(SPI_TARGET stm32)
endif()

if(DEBUG_UART)
    set(WOLFBOOT_UART_DRIVER ${WOLFBOOT_ROOT}/hal/uart/uart_drv_${UART_TARGET}.c)
    list(APPEND WOLFBOOT_DEFS DEBUG_UART)
endif()

if(SPI_FLASH)
    list(APPEND WOLFBOOT_DEFS SPI_FLASH)
    list(APPEND WOLFBOOT_FLASH_SOURCES hal/spi/spi_drv_${SPI_TARGET}.c src/spi_flash.c)
endif()

if(QSPI_FLASH)
    list(APPEND WOLFBOOT_DEFS QSPI_FLASH)
    list(APPEND WOLFBOOT_FLASH_SOURCES hal/spi/spi_drv_${SPI_TARGET}.c src/qspi_flash.c)
endif()

if(OCTOSPI_FLASH)
    list(APPEND WOLFBOOT_DEFS OCTOSPI_FLASH)
endif()

if(UART_FLASH)
    list(APPEND WOLFBOOT_DEFS UART_FLASH)
    list(APPEND WOLFBOOT_FLASH_SOURCES hal/uart/uart_drv_${UART_TARGET}.c src/uart_flash.c)
endif()

if(FLAGS_HOME)
    list(APPEND WOLFBOOT_DEFS FLAGS_HOME=1)
endif()

list(APPEND WOLFBOOT_DEFS PLATFORM_${WOLFBOOT_TARGET})

if(NOT SPMATH)
    list(APPEND USER_SETTINGS USE_FAST_MATH)
endif()

add_library(user_settings INTERFACE)
target_compile_definitions(user_settings INTERFACE ${USER_SETTINGS} ${SIGN_OPTIONS})

add_library(wolfboothal)
target_sources(wolfboothal PRIVATE include/hal.h hal/${WOLFBOOT_TARGET}.c ${WOLFBOOT_FLASH_SOURCES}
                                   ${PARTITION_SOURCE})
target_link_libraries(wolfboothal target user_settings)
target_compile_definitions(wolfboothal PRIVATE ${WOLFBOOT_DEFS})
target_include_directories(wolfboothal PRIVATE ${WOLFBOOT_ROOT} include)
target_compile_options(wolfboothal PRIVATE ${WOLFBOOT_COMPILE_OPTIONS} ${EXTRA_COMPILE_OPTIONS})

if(PYTHON_KEYTOOLS)
    message(STATUS "Using Python Keytools")
    set(SIGN_TOOL ${WOLFBOOT_ROOT}/tools/keytools/sign.py)
    set(KEYGEN_TOOL ${WOLFBOOT_ROOT}/tools/keytools/keygen.py)
else()
    message(STATUS "Using C Keytools")
    set(SIGN_TOOL ${CMAKE_CURRENT_BINARY_DIR}/sign)
    set(KEYGEN_TOOL ${CMAKE_CURRENT_BINARY_DIR}/keygen)
endif()

list(APPEND WOLFBOOT_INCLUDE_DIRS ${WOLFBOOT_ROOT} ${WOLFBOOT_ROOT}/include)

# set default linker script
set(WOLFBOOT_LSCRIPT_TEMPLATE hal/${WOLFBOOT_TARGET}.ld)

# wolfcrypt
add_subdirectory(lib)

if(BUILD_TEST_APPS)
    # test applications
    message(STATUS "Building wolfboot test apps")
    add_subdirectory(test-app)
endif()

set(WOLFBOOT_PLATFORM_LD_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/wolfboot_platform_${WOLFBOOT_TARGET}.ld)

# add variables to cache so they have global scope
set(WOLFBOOT_DEFS
    ${WOLFBOOT_DEFS}
    CACHE INTERNAL "")
set(WOLFBOOT_SOURCES
    ${WOLFBOOT_SOURCES}
    CACHE INTERNAL "")
set(WOLFBOOT_COMPILE_OPTIONS
    ${WOLFBOOT_COMPILE_OPTIONS}
    CACHE INTERNAL "")
set(WOLFBOOT_LINK_OPTIONS
    ${WOLFBOOT_LINK_OPTIONS}
    CACHE INTERNAL "")
set(WOLFBOOT_INCLUDE_DIRS
    ${WOLFBOOT_INCLUDE_DIRS}
    CACHE INTERNAL "")
set(WOLFBOOT_COMPILE_OPTIONS
    ${WOLFBOOT_COMPILE_OPTIONS}
    CACHE INTERNAL "")
set(WOLFBOOT_PLATFORM_LD_SCRIPT
    ${WOLFBOOT_PLATFORM_LD_SCRIPT}
    CACHE INTERNAL "")
set(WOLFBOOT_SIGNING_PRIVATE_KEY
    ${WOLFBOOT_SIGNING_PRIVATE_KEY}
    CACHE INTERNAL "")
set(SIGN_TOOL
    ${SIGN_TOOL}
    CACHE INTERNAL "")
set(SIGN_OPTIONS
    ${SIGN_OPTIONS}
    CACHE INTERNAL "")
set(KEYTOOL_OPTIONS
    ${KEYTOOL_OPTIONS}
    CACHE INTERNAL "")
set(BINASSEMBLE
    ${BINASSEMBLE}
    CACHE INTERNAL "")
set(ARCH_FLASH_OFFSET
    ${ARCH_FLASH_OFFSET}
    CACHE INTERNAL "")
set(WOLFBOOT_VERSION
    ${WOLFBOOT_VERSION}
    CACHE INTERNAL "")

# generate target.h
configure_file(include/target.h.in ${CMAKE_CURRENT_BINARY_DIR}/target.h @ONLY)

add_library(target INTERFACE)
target_compile_definitions(target INTERFACE ${WOLFBOOT_DEFS})
target_include_directories(target BEFORE INTERFACE ${CMAKE_CURRENT_BINARY_DIR})

set(KEYSTORE ${CMAKE_CURRENT_BINARY_DIR}/keystore.c)

if(NOT PYTHON_KEYTOOLS)
    list(
        APPEND
        KEYTOOL_SOURCES
        src/delta.c
        lib/wolfssl/wolfcrypt/src/asn.c
        lib/wolfssl/wolfcrypt/src/aes.c
        lib/wolfssl/wolfcrypt/src/ecc.c
        lib/wolfssl/wolfcrypt/src/coding.c
        lib/wolfssl/wolfcrypt/src/chacha.c
        lib/wolfssl/wolfcrypt/src/ed25519.c
        lib/wolfssl/wolfcrypt/src/ed448.c
        lib/wolfssl/wolfcrypt/src/fe_operations.c
        lib/wolfssl/wolfcrypt/src/ge_operations.c
        lib/wolfssl/wolfcrypt/src/fe_448.c
        lib/wolfssl/wolfcrypt/src/ge_448.c
        lib/wolfssl/wolfcrypt/src/hash.c
        lib/wolfssl/wolfcrypt/src/logging.c
        lib/wolfssl/wolfcrypt/src/memory.c
        lib/wolfssl/wolfcrypt/src/random.c
        lib/wolfssl/wolfcrypt/src/rsa.c
        lib/wolfssl/wolfcrypt/src/sp_int.c
        lib/wolfssl/wolfcrypt/src/sp_c32.c
        lib/wolfssl/wolfcrypt/src/sp_c64.c
        lib/wolfssl/wolfcrypt/src/sha3.c
        lib/wolfssl/wolfcrypt/src/sha256.c
        lib/wolfssl/wolfcrypt/src/sha512.c
        lib/wolfssl/wolfcrypt/src/tfm.c
        lib/wolfssl/wolfcrypt/src/wc_port.c
        lib/wolfssl/wolfcrypt/src/wolfmath.c)

    list(
        APPEND
        KEYTOOL_FLAGS
        -Wall
        -Wextra
        -Werror
        -Itools/keytools
        -DWOLFSSL_USER_SETTINGS
        -Ilib/wolfssl/
        -Iinclude
        -I${CMAKE_CURRENT_BINARY_DIR}
        -DWOLFBOOT_KEYTOOLS
        -O2
        -DIMAGE_HEADER_SIZE=${IMAGE_HEADER_SIZE}
        -DDELTA_UPDATES)

    add_custom_command(
        OUTPUT ${SIGN_TOOL}
        COMMAND gcc -o ${CMAKE_CURRENT_BINARY_DIR}/sign tools/keytools/sign.c ${KEYTOOL_SOURCES}
                ${KEYTOOL_FLAGS}
        WORKING_DIRECTORY ${WOLFBOOT_ROOT}
        COMMENT "Building signing tool")

    add_custom_command(
        OUTPUT ${KEYGEN_TOOL}
        COMMAND gcc -o ${CMAKE_CURRENT_BINARY_DIR}/keygen tools/keytools/keygen.c ${KEYTOOL_SOURCES}
                ${KEYTOOL_FLAGS}
        WORKING_DIRECTORY ${WOLFBOOT_ROOT}
        COMMENT "Building keygen tool")

    add_custom_target(keytools ALL DEPENDS ${SIGN_TOOL} ${KEYGEN_TOOL})
endif()

if(NOT SIGN STREQUAL "NONE")
    if(PYTHON_KEYTOOLS)
        add_custom_target(keystore DEPENDS ${KEYSTORE})
    else()
        add_custom_target(keystore DEPENDS ${SIGN_TOOL} ${KEYGEN_TOOL} ${KEYSTORE})
    endif()

    # generate keystore if it does not already exist
    if(NOT EXISTS ${KEYSTORE})
        add_custom_command(
            OUTPUT ${KEYSTORE} ${WOLFBOOT_SIGNING_PRIVATE_KEY}
            COMMAND ${KEYGEN_TOOL} ${KEYTOOL_OPTIONS} -g ${WOLFBOOT_SIGNING_PRIVATE_KEY}
                    -keystoreDir ${CMAKE_CURRENT_BINARY_DIR}
            WORKING_DIRECTORY ${WOLFBOOT_ROOT}
            COMMENT "Generating keystore.c and signing private key")

        if(NOT PYTHON_KEYTOOLS)
            add_custom_command(
                OUTPUT ${KEYSTORE} ${WOLFBOOT_SIGNING_PRIVATE_KEY}
                DEPENDS ${KEYGEN_TOOL}
                APPEND)
        endif()
    endif()

    add_library(public_key)
    target_sources(public_key PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/keystore.c)
    target_compile_definitions(public_key PRIVATE ${WOLFBOOT_DEFS})
    target_include_directories(public_key PRIVATE include)
    target_link_libraries(public_key target)
endif()

# generate libwolfboot
add_library(wolfboot)
target_sources(wolfboot PRIVATE src/libwolfboot.c ${WOLFBOOT_FLASH_SOURCES})
target_compile_definitions(wolfboot PUBLIC ${WOLFBOOT_DEFS})
target_compile_options(wolfboot PUBLIC ${EXTRA_COMPILE_OPTIONS})
target_include_directories(wolfboot PUBLIC ${WOLFBOOT_INCLUDE_DIRS})
target_link_libraries(wolfboot wolfboothal target wolfcrypt)

# dont warn on unused code
target_compile_options(wolfboot PRIVATE -Wno-unused ${SIM_COMPILE_OPTIONS})
