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

include(../cmake/wolfboot.cmake)

set(PLATFORM_NAME ${WOLFBOOT_TARGET})

if(NOT DEFINED WOLFBOOT_TARGET)
    set(WOLFBOOT_TARGET "none")
endif()

if("${SIGN}" STREQUAL "RSA2048")
    set(IMAGE_HEADER_SIZE "512")
endif()

if("${SIGN}" STREQUAL "RSA4096")
    set(IMAGE_HEADER_SIZE "1024")
endif()

set(APP_SOURCES app_${WOLFBOOT_TARGET}.c led.c system.c timer.c)

if(DEBUG_UART)
    list(APPEND APP_SOURCES ../src/string.c)
endif()

if(ARCH STREQUAL "ARM")
    list(APPEND APP_SOURCES startup_arm.c)
    list(APPEND TEST_APP_COMPILE_DEFINITIONS STM32)
endif()

if(ENCRYPT)
    list(APPEND TEST_APP_COMPILE_DEFINITIONS EXT_ENCRYPTED=1)
endif()

if("${WOLFBOOT_TARGET}" STREQUAL "stm32h7")
    set(APP_LSCRIPT_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/ARM-stm32h7.ld)
elseif("${WOLFBOOT_TARGET}" STREQUAL "stm32u5")
    set(APP_LSCRIPT_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/ARM-stm32u5.ld)
elseif("${WOLFBOOT_TARGET}" STREQUAL "stm32h5")
    if(TZEN)
        set(APP_LSCRIPT_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/ARM-stm32h5-ns.ld)
    else()
        set(APP_LSCRIPT_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/ARM-stm32h5.ld)
    endif()
elseif("${WOLFBOOT_TARGET}" STREQUAL "stm32l5")
    if(TZEN)
        set(APP_LSCRIPT_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/ARM-stm32l5-ns.ld)
    else()
        set(APP_LSCRIPT_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/ARM-stm32l5.ld)
    endif()
else()
    set(APP_LSCRIPT_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/${ARCH}.ld)
endif()

if(SPI_FLASH)
    list(APPEND TEST_APP_COMPILE_DEFINITIONS SPI_FLASH)
    list(APPEND APP_SOURCES ../hal/spi/spi_drv_${SPI_TARGET}.c ../src/spi_flash.c)
endif()
if(OCTOSPI_FLASH)
    set(QSPI_FLASH ON)
    list(APPEND TEST_APP_COMPILE_DEFINITIONS OCTOSPI_FLASH)
endif()
if(QSPI_FLASH)
    list(APPEND TEST_APP_COMPILE_DEFINITIONS QSPI_FLASH)
    list(APPEND APP_SOURCES ../hal/spi/spi_drv_${SPI_TARGET}.c ../src/qspi_flash.c)
endif()

math(EXPR WOLFBOOT_TEST_APP_ADDRESS "${WOLFBOOT_PARTITION_BOOT_ADDRESS} + ${IMAGE_HEADER_SIZE}"
     OUTPUT_FORMAT HEXADECIMAL)
math(EXPR WOLFBOOT_TEST_APP_SIZE "${WOLFBOOT_PARTITION_SIZE} - ${IMAGE_HEADER_SIZE}"
     OUTPUT_FORMAT HEXADECIMAL)

# determine size of bootloader partition
if(NOT DEFINED BOOTLOADER_PARTITION_SIZE)
    math(EXPR BOOTLOADER_PARTITION_SIZE "${WOLFBOOT_PARTITION_BOOT_ADDRESS} - ${ARCH_FLASH_OFFSET}"
         OUTPUT_FORMAT HEXADECIMAL)
endif()

get_filename_component(WOLFBOOT_LSCRIPT ${CMAKE_SOURCE_DIR}/${WOLFBOOT_LSCRIPT_TEMPLATE} NAME)
set(WOLFBOOT_LSCRIPT ${CMAKE_CURRENT_BINARY_DIR}/${WOLFBOOT_LSCRIPT})

get_filename_component(APP_LSCRIPT ${APP_LSCRIPT_TEMPLATE} NAME)
set(APP_LSCRIPT ${CMAKE_CURRENT_BINARY_DIR}/${APP_LSCRIPT})

# generate linker script for bootloader
configure_file(${CMAKE_SOURCE_DIR}/${WOLFBOOT_LSCRIPT_TEMPLATE} ${WOLFBOOT_LSCRIPT})

# generate linker script for app
configure_file(${APP_LSCRIPT_TEMPLATE} ${APP_LSCRIPT})

if(WOLFBOOT_TARGET STREQUAL "sim")
    # create bootloader for platform
    gen_wolfboot_platform_target(${PLATFORM_NAME} "")
else()
    add_library(bootloader_linker_script INTERFACE)
    target_link_options(bootloader_linker_script INTERFACE -T${WOLFBOOT_LSCRIPT} -Wl,-Map=wolfboot.map)

    # create bootloader for platform
    gen_wolfboot_platform_target(${PLATFORM_NAME} bootloader_linker_script)
endif()

if(BUILD_TEST_APPS)
    message(STATUS "Building wolfBoot test apps")
    add_executable(image)

    target_sources(image PRIVATE ${APP_SOURCES})

    # stm32h5-specific sources
    if("${WOLFBOOT_TARGET}" STREQUAL "stm32h5")
        set_source_files_properties(${CMAKE_BINARY_DIR}/keystore.c PROPERTIES GENERATED TRUE)
        target_sources(image PRIVATE
            ../hal/uart/uart_drv_stm32h5.c
            ${CMAKE_BINARY_DIR}/keystore.c
        )
        add_dependencies(image keystore)
        target_compile_definitions(image PRIVATE
            APP_HAS_SYSTICK
            RAMFUNCTION=__attribute__\(\(used,section\(".ramcode"\),long_call\)\)
        )
        target_compile_options(image PRIVATE
            -ffunction-sections -fdata-sections -fno-common -mlong-calls
        )
        if(TZEN)
            target_sources(image PRIVATE
                wcs/wolfcrypt_secure.c
            )
            if(WOLFCRYPT_TZ)
                target_sources(image PRIVATE
                    ../lib/wolfssl/wolfcrypt/src/logging.c
                    ../lib/wolfssl/wolfcrypt/test/test.c
                    ../lib/wolfssl/wolfcrypt/benchmark/benchmark.c
                )
            endif()
        endif()
    endif()

    # stm32l5-specific sources
    if("${WOLFBOOT_TARGET}" STREQUAL "stm32l5")
        target_sources(image PRIVATE
            ../hal/uart/uart_drv_stm32l5.c
        )
        target_compile_options(image PRIVATE
            -ffunction-sections -fdata-sections -fno-common
        )
    endif()

    target_include_directories(image PRIVATE
        ../
        ../include
        ${CMAKE_CURRENT_BINARY_DIR})

    if(TZEN)
        target_include_directories(image PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/wcs)
    endif()

    if(WOLFCRYPT_TZ_PKCS11)
        target_include_directories(image PRIVATE ../lib/wolfPKCS11)
    endif()

    # For TrustZone builds, avoid linking the bootloader lib (it defines NSC stubs).
    if(TZEN AND WOLFCRYPT_TZ)
        target_sources(image PRIVATE ../src/libwolfboot.c)
        target_link_libraries(image PRIVATE wolfboothal target)
    else()
        target_link_libraries(image PRIVATE wolfboot wolfboothal public_key target)
    endif()

    # For TrustZone builds, the test app is a non-secure application
    if(TZEN AND WOLFCRYPT_TZ)
        list(APPEND TEST_APP_COMPILE_DEFINITIONS NONSECURE_APP WOLFBOOT_SECURE_CALLS)
        add_dependencies(image wolfboot_${PLATFORM_NAME})
        target_link_libraries(image PRIVATE ${CMAKE_BINARY_DIR}/wc_secure_calls.o)
    endif()

    if(WOLFCRYPT_TZ_PKCS11)
        list(APPEND TEST_APP_COMPILE_DEFINITIONS WOLFBOOT_PKCS11_APP SECURE_PKCS11)
        target_sources(image PRIVATE
            wcs/pkcs11_stub.c
            wcs/pkcs11_test_ecc.c
            ../lib/wolfssl/wolfcrypt/src/ecc.c
            ../lib/wolfssl/wolfcrypt/src/rsa.c
            ../lib/wolfssl/wolfcrypt/src/asn.c
            ../lib/wolfssl/wolfcrypt/src/aes.c
            ../lib/wolfssl/wolfcrypt/src/hmac.c
            ../lib/wolfssl/wolfcrypt/src/pwdbased.c
            ../lib/wolfssl/wolfcrypt/src/hash.c
            ../lib/wolfssl/wolfcrypt/src/sha256.c
            ../lib/wolfssl/wolfcrypt/src/sha512.c
            ../lib/wolfssl/wolfcrypt/src/sha3.c
            ../lib/wolfssl/wolfcrypt/src/integer.c
            ../lib/wolfssl/wolfcrypt/src/tfm.c
            ../lib/wolfssl/wolfcrypt/src/sp_c32.c
            ../lib/wolfssl/wolfcrypt/src/sp_int.c
            ../lib/wolfssl/wolfcrypt/src/cryptocb.c
            ../lib/wolfssl/wolfcrypt/src/wc_pkcs11.c
            ../lib/wolfssl/wolfcrypt/src/memory.c
            ../lib/wolfssl/wolfcrypt/src/wolfmath.c
            ../lib/wolfssl/wolfcrypt/src/dh.c
            ../lib/wolfssl/wolfcrypt/src/random.c
            ../lib/wolfssl/wolfcrypt/src/coding.c
            ../lib/wolfssl/wolfcrypt/src/wc_encrypt.c
            ../lib/wolfssl/wolfcrypt/src/wc_port.c
        )
        if(SPMATH AND NOT NO_ASM)
            list(APPEND TEST_APP_COMPILE_DEFINITIONS WOLFSSL_HAVE_SP_RSA WOLFSSL_HAVE_SP_ECC WOLFSSL_SP_ARM_CORTEX_M_ASM)
            target_sources(image PRIVATE ../lib/wolfssl/wolfcrypt/src/sp_cortexm.c)
        endif()
    endif()

    target_compile_definitions(image PRIVATE TARGET_${WOLFBOOT_TARGET}
                                                ${TEST_APP_COMPILE_DEFINITIONS} ${WOLFBOOT_DEFS_PUBLIC})

    target_compile_options(image PRIVATE -Wall -Wstack-usage=1024 -ffreestanding -Wno-unused -fomit-frame-pointer
                                            -nostartfiles)

    if(WOLFBOOT_TARGET STREQUAL "sim")
        target_link_options(image PRIVATE -Wl,-gc-sections -Wl,-Map=image.map)
    else()
        target_link_options(image PRIVATE -T${APP_LSCRIPT} -Wl,-gc-sections -Wl,-Map=image.map -ffreestanding -nostartfiles)
    endif()

    if(WOLFBOOT_TARGET IN_LIST ARM_TARGETS)
        message(STATUS "Binary output products will be generated")
        gen_bin_target_outputs(image)

        # add boot address to cache
        unset(${PLATFORM_NAME}_BOOT_ADDRESS CACHE)
        set(${PLATFORM_NAME}_BOOT_ADDRESS ${WOLFBOOT_PARTITION_BOOT_ADDRESS} CACHE INTERNAL "")

        gen_wolfboot_factory_image(${PLATFORM_NAME} image)
    else()
        gen_wolfboot_signed_image(image)
    endif()
endif()
