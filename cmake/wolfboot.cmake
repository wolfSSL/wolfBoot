# wolfboot.cmake
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

include(${CMAKE_CURRENT_LIST_DIR}/utils.cmake)

set(VERSION ${WOLFBOOT_VERSION})

# gen_wolfboot_platform_target is a function instead of a macro because it uses configure_file to
# generate linker scripts based on CMake variables, and the arguments to a macro are not true CMake
# variables. See https://cmake.org/cmake/help/latest/command/macro.html#macro-vs-function.

# generate a wolfboot executable with the flash partition addresses for the given target
function(gen_wolfboot_platform_target PLATFORM_NAME LINKER_SCRIPT_TARGET)

    # generate target for bootloader
    add_executable(wolfboot_${PLATFORM_NAME})
    target_sources(wolfboot_${PLATFORM_NAME} PRIVATE ${WOLFBOOT_SOURCES})

    # set include directories and compile definitions for bootloader
    target_compile_definitions(wolfboot_${PLATFORM_NAME} PRIVATE ${WOLFBOOT_DEFS})
    target_include_directories(wolfboot_${PLATFORM_NAME} PRIVATE ${WOLFBOOT_INCLUDE_DIRS})

    # link with cryptography library, set linker options
    target_link_libraries(wolfboot_${PLATFORM_NAME} wolfcrypt target wolfboot
                          ${LINKER_SCRIPT_TARGET})

    # link with public key if signing is enabled
    if(NOT SIGN STREQUAL "NONE")
        target_link_libraries(wolfboot_${PLATFORM_NAME} public_key)
    endif()

    # set compiler options
    target_compile_options(wolfboot_${PLATFORM_NAME} PRIVATE ${WOLFBOOT_COMPILE_OPTIONS}
                                                             ${EXTRA_COMPILE_OPTIONS})
    target_link_options(wolfboot_${PLATFORM_NAME} PRIVATE ${WOLFBOOT_LINK_OPTIONS})

    if(WOLFBOOT_TARGET IN_LIST ARM_TARGETS)
        # generate .bin file for bootloader
        gen_bin_target_outputs(wolfboot_${PLATFORM_NAME})
    endif()

    install(TARGETS wolfboot_${PLATFORM_NAME} RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wolfboot_${PLATFORM_NAME}.bin DESTINATION bin)
endfunction()

function(gen_wolfboot_signed_image TARGET)
    if(NOT DEFINED SIGN_TOOL)
        message(FATAL_ERROR "SIGN_TOOL is not defined")
    endif()

    # for arm targets, sign the .bin produced by objcopy. For x86 targets, sign the executable
    # produced by the compiler
    if(WOLFBOOT_TARGET IN_LIST ARM_TARGETS)
        set(INPUT_IMAGE $<TARGET_FILE:${TARGET}>.bin)
    else()
        set(INPUT_IMAGE $<TARGET_FILE:${TARGET}>)
    endif()

    # generate signed image
    add_custom_command(
        OUTPUT ${TARGET}_v${VERSION}_signed.bin
        DEPENDS ${INPUT_IMAGE} ${WOLFBOOT_SIGNING_PRIVATE_KEY} ${SIGN_TOOL}
        COMMAND ${SIGN_TOOL} ${KEYTOOL_OPTIONS} ${INPUT_IMAGE} ${WOLFBOOT_SIGNING_PRIVATE_KEY} ${VERSION}
        COMMENT "Signing ${TARGET}"
    )

    add_custom_target(${TARGET}_signed ALL DEPENDS ${TARGET}_v${VERSION}_signed.bin)

    multiconfigfileinstall(${TARGET}_v${VERSION}_signed.bin bin)
endfunction()

function(gen_wolfboot_factory_image PLATFORM_NAME TARGET)
    get_filename_component(FILENAME ${TARGET} NAME)

    if(NOT DEFINED ${PLATFORM_NAME}_BOOT_ADDRESS)
        message(FATAL_ERROR "${PLATFORM_NAME}_BOOT_ADDRESS is not defined")
    endif()

    if(NOT DEFINED BINASSEMBLE)
        message(FATAL_ERROR "BINASSEMBLE is not defined")
    endif()

    if(NOT DEFINED ARCH_FLASH_OFFSET)
        message(FATAL_ERROR "ARCH_FLASH_OFFSET is not defined")
    endif()

    gen_wolfboot_signed_image(${TARGET})

    set(BOOT_ADDRESS ${${PLATFORM_NAME}_BOOT_ADDRESS})

    # merge images
    add_custom_command(
        OUTPUT ${FILENAME}_factory.bin
        DEPENDS $<TARGET_FILE:wolfboot_${PLATFORM_NAME}>.bin ${FILENAME}_v${VERSION}_signed.bin
                ${WOLFBOOT_SIGNING_PRIVATE_KEY} binAssemble
        COMMAND ${BINASSEMBLE} ${FILENAME}_factory.bin ${ARCH_FLASH_OFFSET}
        $<TARGET_FILE:wolfboot_${PLATFORM_NAME}>.bin ${BOOT_ADDRESS} ${TARGET}_v${VERSION}_signed.bin
        COMMENT "Assembling ${FILENAME} factory image")
    list(APPEND BOOTLOADER_OUTPUTS ${FILENAME}_factory.bin)

    add_custom_target(${FILENAME}_boot ALL DEPENDS ${BOOTLOADER_OUTPUTS} ${TARGET}_signed)

    multiconfigfileinstall(${BOOTLOADER_OUTPUTS} bin)
endfunction()
