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
#
# Windows builds: Never use NODEFAULTLIB; problems with CRT auto-linking and cause mainCRTStartup to go missing.

message(STATUS "Begin [WOLFBOOT_ROOT]/CmakeLists.txt")

message(STATUS "wolfBoot target: ${WOLFBOOT_TARGET}")
message(STATUS "CMake version:   ${CMAKE_VERSION}")
message(STATUS "Generator:       ${CMAKE_GENERATOR}")
message(STATUS "Source dir:      ${CMAKE_SOURCE_DIR}")
message(STATUS "Binary dir:      ${CMAKE_BINARY_DIR}")

cmake_minimum_required(VERSION 3.16)

include(cmake/config_defaults.cmake)

#---------------------------------------------------------------------------------------------
# Initial environment checks
#---------------------------------------------------------------------------------------------
if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(
        FATAL_ERROR
            "In-source builds are not allowed for wolfBoot.\
     Run cmake from a separate directory from where CMakeLists.txt lives.\
     NOTE: cmake will now create CMakeCache.txt and CMakeFiles/*.\
     You must delete them, or cmake will refuse to work.")
endif()

# This must appear before project(wolfBoot)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED WOLFBOOT_TARGET AND
            NOT WOLFBOOT_TARGET STREQUAL "x86_64_efi" AND
            NOT WOLFBOOT_TARGET STREQUAL "sim")
        set(CMAKE_TOOLCHAIN_FILE
        "${CMAKE_CURRENT_LIST_DIR}/cmake/toolchain_arm-none-eabi.cmake"
        CACHE FILEPATH "" FORCE)
    endif()
endif()

if(NOT DEFINED PREFERRED_HOST_CC_NAME_LIST OR PREFERRED_HOST_CC_NAME_LIST STREQUAL "")
    # set(PREFERRED_HOST_CC_NAME_LIST gcc clang cl)
    if(CMAKE_HOST_WIN32)
        set(PREFERRED_HOST_CC_NAME_LIST gcc clang cl)
    else()
        set(PREFERRED_HOST_CC_NAME_LIST gcc clang cl)
    endif()
    message(STATUS "Setting preferred order for HOST_CC:${PREFERRED_HOST_CC_NAME_LIST}")
else()
    message(STATUS "Found preferred order for HOST_CC:${PREFERRED_HOST_CC_NAME_LIST}")
endif()

#---------------------------------------------------------------------------------------------
#---------------------------------------------------------------------------------------------
project(wolfBoot)
#---------------------------------------------------------------------------------------------
#---------------------------------------------------------------------------------------------

# Simulator target is POSIX-only (uses sys/mman.h, etc.)
if(MSVC AND (DEFINED WOLFBOOT_TARGET) AND (WOLFBOOT_TARGET STREQUAL "sim"))
    message(FATAL_ERROR "TARGET_sim is POSIX-only. Use a device preset (e.g. stm32h7) or build the sim on Linux/WSL/MinGW.")
endif()

include(cmake/functions.cmake)
include(cmake/utils.cmake)

# Windows users may prefer VisualGDB
if(DETECT_VISUALGDB)
    message(STATUS "VisualGDB detection active: cmake/visualgdb_config.cmake")
    include(cmake/visualgdb_config.cmake)
endif()

# Some OS-specific checks and configs
if(CMAKE_HOST_WIN32 AND DETECT_VS2022)
    message(STATUS "Visual Studio 2022 detection active: make/vs2022_config.cmake")
    include(cmake/vs2022_config.cmake)
endif()

# If not VisualGDB, perhaps ST CubeIDE?
if(DETECT_CUBEIDE AND NOT FOUND_HAL_BASE)
    include(cmake/cube_ide_config.cmake)
endif()

# If still not found, download:
if(NOT FOUND_HAL_BASE AND ENABLE_HAL_DOWNLOAD)
    if(WOLFBOOT_TARGET MATCHES "^stm32")
        include(cmake/stm32_hal_download.cmake)
    else()
        message(STATUS "WARNING: HAL not found and download not available for ${WOLFBOOT_TARGET}")
    endif()
endif()

if(USE_DOT_CONFIG)
    message(STATUS "USE_DOT_CONFIG is enabled")
    include(cmake/load_dot_config.cmake)
else()
    message(STATUS "No .config files will be read; USE_DOT_CONFIG is disabled")
endif()

# Edit to stop CMake from appending any "standard" C include paths that it thinks your toolchain/platform needs
# Brute force, not recommended, ymmv.
if(false)
    set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES "" CACHE STRING "" FORCE)
endif()

#------------------- Host compiler (for native tools only) -----------------------------------
#---------------------------------------------------------------------------------------------
# Build-time tools (bin-assemble/sign/keygen) must compile for the HOST.
if (CMAKE_HOST_WIN32)
    # Prefer gcc/clang on Windows so POSIX-y headers (unistd.h) are available.
    message(STATUS "Tip: If find_program cannot find HOST_CC (not in path?), try launching from VS2022 dev prompt or edit path.")
    message(STATUS "ARM_GCC_BIN=${ARM_GCC_BIN}")
    if(DEFINED HOST_CC AND EXISTS "${HOST_CC}")
        message(STATUS "HOST_CC already defined, not using HOST_CC_HINT_DIRECTORIES")
        message(STATUS "Using preset HOST_CC: ${HOST_CC}")
    else()
        unset(HOST_CC CACHE)
        message(STATUS "HOST_CC not defined, will attempt to find...")
        message(STATUS "HOST_CC_HINT_DIRECTORIES contents:")
        foreach(_hint_item IN LISTS HOST_CC_HINT_DIRECTORIES)
            if(IS_DIRECTORY "${_hint_item}")
                set(_hint_status "(ok)")
            else()
                set(_hint_status "NOT FOUND:")
            endif()
            message("-- ${_hint_status} ${_hint_item}")
        endforeach()
        find_program(HOST_CC
                     NAMES ${PREFERRED_HOST_CC_NAME_LIST} REQUIRED
                     HINTS ${HOST_CC_HINT_DIRECTORIES}
        )
    endif()
    message(STATUS "HOST_CC=${HOST_CC}")
    get_filename_component(_host_name "${HOST_CC}" NAME)
    message(STATUS "Detected HOST_CC executable: ${_host_name}")
    message(STATUS "CMAKE_C_COMPILER=${CMAKE_C_COMPILER}")

    # Not with LLVM, nor GCC:
    set(HOST_LINK_FLAG "")

    if (HOST_CC MATCHES [[(^|[/\\])cl(\.exe)?$]] OR (HOST_CC MATCHES [[(^|[/\\])clang-cl(\.exe)?$]]))
        # DOS/Windows detected by ".exe" extension, not to be confused with WSL and/or WinGW
        message(STATUS "Found CMAKE_HOST_WIN32 and .exe in HOST_CC: Setting HOST_IS_MSVC")
        #-------------------------------------------------------------------------------------
        set(HOST_IS_MSVC TRUE)
        #-------------------------------------------------------------------------------------
        # Are we running in Visual Studio 2022 or VSCode from VS2022 command prompt?
        print_env(VSCMD_VER)
        print_env(VCToolsInstallDir)
        print_env(VCINSTALLDIR)
        print_env(WindowsSdkDir)
        set(HOST_EXE ".exe")
        set(HOST_O2  /O2)
        set(HOST_I   /I)
        set(HOST_D   /D)
        set(HOST_OUT /Fe:)
        # Put .obj files in a private folder to avoid collisions & root litter:
        set(HOST_OBJDIR     "${CMAKE_CURRENT_BINARY_DIR}/obj")
        file(MAKE_DIRECTORY "${HOST_OBJDIR}")
        set(HOST_FO         "/Fo$<SHELL_PATH:${HOST_OBJDIR}/>")
        set(HOST_WARN       "")   # MSVC warnings already noisy; keep simple
        # Detect pointer size -> host arch name (x64/x86)
        if (HOST_CC MATCHES [[Hostx64[/\\]x64]] OR CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
            set(HOST_ARCH x64)
            set(HOST_MACHINE_FLAG /MACHINE:X64)
        else()
            set(HOST_ARCH x86)
            set(HOST_MACHINE_FLAG /MACHINE:X86)
        endif()
        message(STATUS "HOST_CC suggests HOST_ARCH=${HOST_ARCH}")
        if(USE_32BIT_LIBS AND NOT ("${HOST_ARCH}" STREQUAL "x86"))
            message(STATUS "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
            MESSAGE(STATUS "USE_32BIT_LIBS set to true but HOST_ARCH=${HOST_ARCH}")
            message(STATUS "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
        endif()
        if(USE_64BIT_LIBS AND NOT ("${HOST_ARCH}" STREQUAL "x64"))
            message(STATUS "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
            MESSAGE(STATUS "USE_64BIT_LIBS set to true but HOST_ARCH=${HOST_ARCH}")
            message(STATUS "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
        endif()

        # Gather Windows SDK & MSVC libpaths from env (vcvarsall sets these)
        set(HOST_WINSDK_UCRT "")
        set(HOST_WINSDK_UM   "")
        set(HOST_MSVC_LIB    "")

        if (DEFINED ENV{WindowsSdkDir} AND DEFINED ENV{WindowsSDKLibVersion})
            set(HOST_WINSDK_UCRT "$ENV{WindowsSdkDir}/Lib/$ENV{WindowsSDKLibVersion}/ucrt/${HOST_ARCH}")
            set(HOST_WINSDK_UM   "$ENV{WindowsSdkDir}/Lib/$ENV{WindowsSDKLibVersion}/um/${HOST_ARCH}")
        endif()

        if (DEFINED ENV{VCToolsInstallDir})
            set(HOST_MSVC_LIB "$ENV{VCToolsInstallDir}/lib/${HOST_ARCH}")
        endif()

        # Prefer dynamic CRT explicitly (avoid stale tool defaults)
        set(HOST_RUNTIME_FLAG /MD)   # or /MT if you want static CRT

        # Set link flag only for Microsoft compilers
        set(HOST_LINK_FLAG /link)

        # wolfSSL random on Windows needs Advapi32:
        set(HOST_LINK_LIBS Advapi32.lib)
    else()
        # gcc or clang
        #-------------------------------------------------------------------------------------
        set(HOST_IS_MSVC FALSE)
        #-------------------------------------------------------------------------------------
        set(HOST_EXE  "")
        set(HOST_O2   -O2)
        set(HOST_I    -I)
        set(HOST_D    -D)
        set(HOST_OUT  -o)
        set(HOST_FO   "")     # gcc/clang handle objs internally here
        set(HOST_WARN -Wall -Wextra -Werror)
        # set(HOST_LINK_LIBS "")  # not needed with MinGW/clang on Windows
        set(HOST_LINK_LIBS -ladvapi32)
        set(HOST_RUNTIME_FLAG "")
    endif()

    # On Windows, binaries should end with .exe regardless of flag style
    set(HOST_EXE ".exe")
else()
    # POSIX hosts
    foreach(_hint_item IN LISTS HOST_CC_HINT_DIRECTORIES)
        message("-- ${_hint_item}")
    endforeach()
    message(STATUS "PREFERRED_HOST_CC_NAME_LIST=${PREFERRED_HOST_CC_NAME_LIST}")
    find_program(HOST_CC NAMES ${PREFERRED_HOST_CC_NAME_LIST} REQUIRED)
    set(HOST_IS_MSVC FALSE)
    set(HOST_EXE     "")
    set(HOST_O2      -O2)
    set(HOST_I       -I)
    set(HOST_D       -D)
    set(HOST_OUT     -o)
    set(HOST_FO      "")
    set(HOST_WARN   -Wall -Wextra -Werror)

    set(HOST_LINK_FLAG "")
    set(HOST_LINK_LIBS "")
    set(HOST_RUNTIME_FLAG "")
endif()

message(STATUS "Host CC: ${HOST_CC}")
message(STATUS "Host compiler treated as MSVC: ${HOST_IS_MSVC}")

if(WOLFBOOT_TARGET STREQUAL "imx_rt")
    # This target depends on the NXP MCUXpresso SDK for EVKB-IMXRT1040.
    # Without it, the build will fail anyway on missing fsl_* headers.
    set(IMXRT_SDK_ROOT "" CACHE PATH "Path to MCUXpresso SDK for EVKB-IMXRT1040")

    if(NOT IMXRT_SDK_ROOT OR NOT EXISTS "${IMXRT_SDK_ROOT}")
        message(FATAL_ERROR
            "WOLFBOOT_TARGET=imx_rt requires IMXRT_SDK_ROOT to point to a valid "
            "MCUXpresso SDK for EVKB-IMXRT1040 (fsl_cache.h, etc.). "
            "You currently do not have this SDK installed."
        )
    endif()
endif()

#---------------------------------------------------------------------------------------------
# Common includes/defines for host tools
#---------------------------------------------------------------------------------------------
set(HOST_INCLUDES
    ${HOST_I}$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/tools/keytools>
    ${HOST_I}$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/lib/wolfssl>
    ${HOST_I}$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/include>
    ${HOST_I}$<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}> # Reminder target.h typically gets generated here
)

if(NOT DEFINED IMAGE_HEADER_SIZE)
    set(IMAGE_HEADER_SIZE 256)
endif()

set(HOST_DEFS
    ${HOST_D}WOLFSSL_USER_SETTINGS
    ${HOST_D}IMAGE_HEADER_SIZE=${IMAGE_HEADER_SIZE}
    ${HOST_D}DELTA_UPDATES
)

# --- Hard-pin the algorithms the host tools must support (only for host tools) ---
# This guarantees sign.c compiles with these on MSVC even if SIGN_OPTIONS plumbing misses.

list(APPEND HOST_DEFS
     ${HOST_D}WOLFBOOT_SIGN_ED25519
     ${HOST_D}WOLFBOOT_HASH_SHA256)


# --- Windows unistd.h shim for MSVC host builds ---
if(CMAKE_HOST_WIN32 AND  (HOST_IS_MSVC OR HOST_CC MATCHES [[(^|[/\\])clang(\.exe)?$]]))
    set(HOST_SHIM_DIR "${CMAKE_CURRENT_BINARY_DIR}/host_shims")
    file(MAKE_DIRECTORY "${HOST_SHIM_DIR}")

    # Minimal shims; extend if bin-assemble needs more.
    file(WRITE "${HOST_SHIM_DIR}/unistd.h" [=[
#ifndef _WIN32
#  error "This shim is for Windows/MSVC only"
#endif
#include <io.h>
#include <process.h>
#include <direct.h>
#include <stdlib.h>

#ifndef ssize_t
#  ifdef _WIN64
     typedef long long ssize_t;
#  else
     typedef int ssize_t;
#  endif
#endif

#ifndef unlink
#  define unlink _unlink
#endif
#ifndef close
#  define close _close
#endif
#ifndef read
#  define read _read
#endif
#ifndef write
#  define write _write
#endif
#ifndef access
#  define access _access
#endif
]=])

    # Prepend shim include so it is found first
    list(INSERT HOST_INCLUDES 0 ${HOST_I}$<SHELL_PATH:${HOST_SHIM_DIR}>)
endif() # Windows Host Shim

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/wolfssl
)

if ("${WOLFBOOT_TARGET}" STREQUAL "")
    message(STATUS "Setting WOLFBOOT_TARGET from TARGET=${TARGET}")
    set(WOLFBOOT_TARGET "${TARGET}")
endif()

if(NOT DEFINED WOLFBOOT_TARGET)
    message(FATAL_ERROR "WOLFBOOT_TARGET must be defined")
else()
    message(STATUS "Building for ${WOLFBOOT_TARGET}")
endif()

if(NOT DEFINED WOLFBOOT_SECTOR_SIZE)
    message(FATAL_ERROR "WOLFBOOT_SECTOR_SIZE must be defined")
endif()

if(NOT DEFINED ARM_TARGETS)
    list(APPEND ARM_TARGETS cypsoc6
                            imx
                            imx_rt
                            kinetis
                            lpc54606j512
                            mcxa
                            mcxw
                            nrf52
                            nrf52840
                            nrf5340
                            nrf5340_net
                            rp2350
                            sama5d3
                            same51
                            stm32c0
                            stm32f1
                            stm32f4
                            stm32f7
                            stm32g0
                            stm32h5
                            stm32h7
                            stm32l0
                            stm32l4
                            stm32l5
                            stm32u5
                            stm32wb
                            ti
                            zynqmp
    )
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

if("${WOLFBOOT_TARGET}" IN_LIST ARM_TARGETS)
    set(ARCH ARM)
elseif("${WOLFBOOT_TARGET}" STREQUAL "x86_64_efi")
    set(ARCH x86_64)
elseif("${WOLFBOOT_TARGET}" STREQUAL "sim")
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
add_option("SPMATHALL" "Use SP Math All (sp_int.c) (default: disabled)" "no" "yes;no")
add_option("WOLFBOOT_TARGET" "Target platform to build for (default: stm32h7)" "stm32h7"
    "${ARM_TARGETS};x86_64_efi;sim")
add_option("UART_FLASH" "Use external UART flash drivers (default: disabled)" "no" "yes;no")
add_option(
    "WOLFBOOT_SMALL_STACK"
    "Use a fixed-size memory pool created at compile time for cryptography implementation (default: disabled)"
    "no"
    "yes;no")
add_option("DEBUG_UART" "Enable trace debugging over a UART (default: disabled)" "no" "yes;no")
add_option("BUILD_TEST_APPS" "Build the wolfBoot image and test apps (default: disabled)" "no" "yes;no")
add_option("BUILD_IMAGE" "Build the wolfBoot image (default: disabled)" "no" "yes;no")
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

    if((NOT DEFINED WOLFBOOT_PARTITION_BOOT_ADDRESS) OR ("${WOLFBOOT_PARTITION_BOOT_ADDRESS}" STREQUAL ""))
        message(FATAL_ERROR "WOLFBOOT_PARTITION_BOOT_ADDRESS must be defined")
    endif()

    if(NOT DEFINED WOLFBOOT_PARTITION_UPDATE_ADDRESS)
        message(FATAL_ERROR "WOLFBOOT_PARTITION_UPDATE_ADDRESS must be defined")
    endif()

    if(NOT DEFINED WOLFBOOT_PARTITION_SWAP_ADDRESS)
        message(FATAL_ERROR "WOLFBOOT_PARTITION_SWAP_ADDRESS must be defined")
    endif()
endif()

# unset cache variables variables that need to be accessed by the gen_wolfboot_platform_target cmake
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
unset(WOLFBOOT_VERSION CACHE)
unset(ARCH_FLASH_OFFSET CACHE)

if(EXTRA_DEFS)
    string(REPLACE " " ";" WOLFBOOT_EXTRA_DEFS ${EXTRA_DEFS})
    list(APPEND WOLFBOOT_DEFS ${WOLFBOOT_EXTRA_DEFS})
endif()

# Ensure WOLFBOOT_ROOT is set. Prefer env var, else default to "."
# WOLFBOOT_ROOT is typically a lib directory in wolfBoot and contains:
#                   wolfHSM, wolfPKCS11, wolfssl, wolfTPM, and perhaps others.
if(NOT DEFINED WOLFBOOT_ROOT OR WOLFBOOT_ROOT STREQUAL "")
    if(DEFINED ENV{WOLFBOOT_ROOT} AND NOT "$ENV{WOLFBOOT_ROOT}" STREQUAL "")
        message(STATUS "Setting WOLFBOOT_ROOT from environment variable")
        set(WOLFBOOT_ROOT "$ENV{WOLFBOOT_ROOT}" CACHE PATH "Path to wolfBoot lib root")
    else()
        message(STATUS "Setting WOLFBOOT_ROOT to default current directory.")
        set(WOLFBOOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Path to wolfBoot lib root")
    endif()
endif()
message(STATUS "WOLFBOOT_ROOT=${WOLFBOOT_ROOT}")
set(SIZE_SCRIPT "${WOLFBOOT_ROOT}/cmake/write_size.cmake")

list(APPEND WOLFBOOT_DEFS __WOLFBOOT)

set(WOLFBOOT_SOURCES "include/loader.h"
                     "include/image.h"
                     "src/string.c"
                     "src/image.c"
                     "src/loader.c")

# build bin-assemble tool Windows
set(BINASSEMBLE ${CMAKE_CURRENT_BINARY_DIR}/bin-assemble${HOST_EXE})
set(BINASSEMBLE_OBJDIR "${CMAKE_CURRENT_BINARY_DIR}/obj_bin_assemble")

add_custom_command(
    OUTPUT "${BINASSEMBLE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory ${BINASSEMBLE_OBJDIR}
    COMMAND "${HOST_CC}" ${HOST_O2} ${HOST_WARN} ${HOST_RUNTIME_FLAG}
            ${HOST_INCLUDES}            # <-- needed for unistd.h shim
            ${HOST_FO} # /Fo$<SHELL_PATH:${BINASSEMBLE_OBJDIR}/>  # isolate objs
            ${HOST_OUT}$<SHELL_PATH:${BINASSEMBLE}>
            $<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/tools/bin-assemble/bin-assemble.c>
            ${HOST_LINK_FLAG} ${HOST_LINK_LIBS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Building bin-assemble tool"
)

add_custom_target(binAssemble DEPENDS ${BINASSEMBLE})

#-----------------------------------------------------------------------------------------
# Toolchain Specifications
#-----------------------------------------------------------------------------------------
if(NOT CMAKE_C_COMPILER)
    # Ensure include only once
    if(ARCH STREQUAL "ARM")
        include(cmake/toolchain_arm-none-eabi.cmake)
    elseif(ARCH STREQUAL "AARCH64")
        include(cmake/toolchain_aarch64-none-elf.cmake)
    endif()
endif()


#---------------------------------------------------------------------------------------------
# Architecture/CPU configuration
#---------------------------------------------------------------------------------------------
set(UPDATE_SOURCES src/update_flash.c)

# Default flash offset
if(NOT DEFINED ARCH_FLASH_OFFSET)
    message(STATUS "WARNING: ARCH_FLASH_OFFSET not defined, setting to default 0x0")
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

        if(NOT DEFINED CLOCK_SPEED)
            message(FATAL_ERROR "CLOCK_SPEED must be defined")
        endif()
        if(NOT DEFINED STM32_PLLM)
            message(FATAL_ERROR "STM32_PLLM must be defined")
        endif()
        if(NOT DEFINED STM32_PLLN)
            message(FATAL_ERROR "STM32_PLLN must be defined")
        endif()
        if(NOT DEFINED STM32_PLLP)
            message(FATAL_ERROR "STM32_PLLP must be defined")
        endif()
        if(NOT DEFINED STM32_PLLQ)
            message(FATAL_ERROR "STM32_PLLQ must be defined")
        endif()
        add_compile_definitions(
            CLOCK_SPEED=${CLOCK_SPEED}
            STM32_PLLM=${STM32_PLLM}
            STM32_PLLN=${STM32_PLLN}
            STM32_PLLP=${STM32_PLLP}
            STM32_PLLQ=${STM32_PLLQ}
        )
    endif()

    # TODO move these to presets
    if(${WOLFBOOT_TARGET} STREQUAL "stm32g0")
        set(ARCH_FLASH_OFFSET 0x08000000)
        set(WOLFBOOT_ORIGIN ${ARCH_FLASH_OFFSET})
    endif()

    if(${WOLFBOOT_TARGET} STREQUAL "stm32h7")
        set(ARCH_FLASH_OFFSET 0x08000000)
        set(WOLFBOOT_ORIGIN ${ARCH_FLASH_OFFSET})
    endif()
    if(${WOLFBOOT_TARGET} STREQUAL "stm32u5")
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
    add_compile_definitions("TARGET_X86_64_EFI")
    set(CMAKE_EXE_LINKER_FLAGS "-shared -Bsymbolic -L/usr/lib -T${GNU_EFI_LSCRIPT}")
    set(LD_START_GROUP ${GNU_EFI_CRT0})
    set(LD_END_GROUP "-lgnuefi -lefi")
    set(UPDATE_SOURCES src/update_ram.c)
endif()

#---------------------------------------------------------------------------------------------
# DSA Settings
#---------------------------------------------------------------------------------------------
if(SIGN STREQUAL "NONE")
    list(APPEND KEYTOOL_OPTIONS --no-sign)
    message(STATUS "Image signing disabled")
    set(WOLFBOOT_SIGNING_PRIVATE_KEY
        ""
        CACHE INTERNAL "")
    if(HASH STREQUAL "SHA384")
        set(STACK_USAGE 3760)
    else()
        set(STACK_USAGE 1216)
    endif()
    list(APPEND SIGN_OPTIONS WOLFBOOT_NO_SIGN)
else()
    set(WOLFBOOT_SIGNING_PRIVATE_KEY ${CMAKE_CURRENT_BINARY_DIR}/wolfboot_signing_private_key.der)
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
        set(STACK_USAGE 5264)
    else()
        set(STACK_USAGE 7632)
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
        set(STACK_USAGE 11216)
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
        set(STACK_USAGE 5000)
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
        set(STACK_USAGE 17568)
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
            set(STACK_USAGE 5768)
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

# Turn SIGN_OPTIONS (plain symbols) into real compiler -D flags for host tools
set(SIGN_DEFS "")
foreach(_opt IN LISTS SIGN_OPTIONS)
    list(APPEND SIGN_DEFS ${HOST_D}${_opt})
endforeach()

#---------------------------------------------------------------------------------------------
# Settings
#---------------------------------------------------------------------------------------------

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

#---------------------------------------------------------------------------------------------
# Hash settings
#---------------------------------------------------------------------------------------------
if(HASH STREQUAL "SHA256")
    list(APPEND WOLFBOOT_DEFS  WOLFBOOT_HASH_SHA256)
    list(APPEND SIGN_OPTIONS   WOLFBOOT_HASH_SHA256)
    message(STATUS "Using SHA256 hash")
endif()

if(HASH STREQUAL "SHA384")
    list(APPEND WOLFBOOT_DEFS  WOLFBOOT_HASH_SHA384)
    list(APPEND SIGN_OPTIONS   WOLFBOOT_HASH_SHA384)
    # Note: the keygen tool in this tree does not accept a --sha384 CLI flag.
    # Hash selection is controlled via WOLFBOOT_HASH_SHA384 in the image header,
    # so do not append a hash option to KEYTOOL_OPTIONS here.
    # list(APPEND KEYTOOL_OPTIONS --sha384)
    message(STATUS "Using SHA384 hash")
endif()

if(HASH STREQUAL "SHA3")
    list(APPEND WOLFBOOT_DEFS  WOLFBOOT_HASH_SHA3_384)
    list(APPEND SIGN_OPTIONS   WOLFBOOT_HASH_SHA3_384)
    # Note: the keygen tool in this tree does not accept a --sha3 CLI flag.
    # Hash selection is controlled via WOLFBOOT_HASH_SHA3_384 in the image header,
    # so do not append a hash option to KEYTOOL_OPTIONS here.
    # list(APPEND KEYTOOL_OPTIONS --sha3)
    message(STATUS "Using SHA3_384 hash")
endif()

#---------------------------------------------------------------------------------------------
# wolfboot HAL
#---------------------------------------------------------------------------------------------

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

list(APPEND WOLFBOOT_DEFS TARGET_${WOLFBOOT_TARGET})

if(SPMATHALL)
    list(APPEND USER_SETTINGS WOLFSSL_SP_MATH_ALL)
endif()
if(NOT SPMATH AND NOT SPMATHALL)
    list(APPEND USER_SETTINGS USE_FAST_MATH)
endif()

list(APPEND WOLFBOOT_DEFS WOLFSSL_USER_SETTINGS)

add_library(user_settings INTERFACE)
target_compile_definitions(user_settings INTERFACE ${USER_SETTINGS} ${SIGN_OPTIONS})

add_library(wolfboothal)
target_sources(wolfboothal PRIVATE include/hal.h hal/${WOLFBOOT_TARGET}.c ${WOLFBOOT_FLASH_SOURCES}
                                   ${PARTITION_SOURCE})


#---------------------------------------------------------------------------------------------
# --- HAL for STM32L4 (only the pieces we need) ---
#---------------------------------------------------------------------------------------------
# TODO move this to preset and/or cmake dir
if(WOLFBOOT_TARGET STREQUAL "stm32l4")
    message(STATUS "HAL_DRV=${HAL_DRV}")
    add_library(stm32l4_hal STATIC
        ${HAL_DRV}/Src/stm32l4xx_hal.c
        ${HAL_DRV}/Src/stm32l4xx_hal_flash.c
        ${HAL_DRV}/Src/stm32l4xx_hal_flash_ex.c
        ${HAL_DRV}/Src/stm32l4xx_hal_cortex.c
        # add more modules later if you get missing symbols, e.g. RCC/GPIO/etc:
        # ${HAL_DRV}/Src/stm32l4xx_hal_rcc.c
        # ${HAL_DRV}/Src/stm32l4xx_hal_gpio.c
    )

    target_include_directories(stm32l4_hal BEFORE PUBLIC
        ${WOLFBOOT_ROOT}/hal
        ${HAL_DRV}/Inc
        ${HAL_CMSIS_DEV}
        ${HAL_CMSIS_CORE}
        ${HAL_TEMPLATE_INC}
    )

    target_compile_definitions(stm32l4_hal PUBLIC
        USE_HAL_DRIVER
        STM32L475xx
        # If your stm32l4xx_hal_conf.h doesn't enable FLASH, you can force it:
        # HAL_FLASH_MODULE_ENABLED
    )

    # Link HAL into the HAL wrapper lib so the final image pulls symbols from a single archive
    target_link_libraries(wolfboothal PUBLIC target user_settings stm32l4_hal)
else()
    target_link_libraries(wolfboothal target user_settings)
endif()

target_compile_definitions(wolfboothal PRIVATE ${WOLFBOOT_DEFS})
target_include_directories(wolfboothal PRIVATE ${WOLFBOOT_ROOT} include)
target_compile_options(wolfboothal PRIVATE ${WOLFBOOT_COMPILE_OPTIONS} ${EXTRA_COMPILE_OPTIONS})

message(STATUS "Using C Keytools")

#---------------------------------------------------------------------------------------------
# define sources/flags BEFORE the custom commands that use them
#---------------------------------------------------------------------------------------------
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
    lib/wolfssl/wolfcrypt/src/wolfmath.c
    lib/wolfssl/wolfcrypt/src/dilithium.c
    lib/wolfssl/wolfcrypt/src/wc_lms.c
    lib/wolfssl/wolfcrypt/src/wc_lms_impl.c
    lib/wolfssl/wolfcrypt/src/wc_xmss.c
    lib/wolfssl/wolfcrypt/src/wc_xmss_impl.c
)

set(KEYTOOL_SOURCES_ABS ${KEYTOOL_SOURCES})
list(TRANSFORM KEYTOOL_SOURCES_ABS PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)

list(APPEND KEYTOOL_FLAGS
    -Wall -Wextra -Werror
    -I$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/tools/keytools>
    -DWOLFSSL_USER_SETTINGS
    -I$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/lib/wolfssl>
    -I$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/include>
    -I$<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}>
    -O2
    -DIMAGE_HEADER_SIZE=${IMAGE_HEADER_SIZE}
    -DDELTA_UPDATES
)


#---------------------------------------------------------------------------------------------
# sign tool
#---------------------------------------------------------------------------------------------
set(SIGN_TOOL ${CMAKE_CURRENT_BINARY_DIR}/sign${HOST_EXE})

# Give VS a debuggable, runnable target that points to the produced exe
add_executable(sign_host IMPORTED GLOBAL)
set_target_properties(sign_host PROPERTIES
    IMPORTED_LOCATION           "${SIGN_TOOL}"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    # VS_DEBUGGER_COMMAND_ARGUMENTS "test.bin wolfboot_signing_private_key.der 1 --ed25519 --sha256"
)

# Ensure the exe is (re)built before F5
add_dependencies(sign_host keytools)

# Optional: make F5 pick this by default
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT sign_host)

set(SIGN_OBJDIR "${CMAKE_CURRENT_BINARY_DIR}/obj_sign")
if(HOST_IS_MSVC)
    # isolate objs only for Windows (.exe)
    set(HOST_FO_SIGN "/Fo$<SHELL_PATH:${SIGN_OBJDIR}/>")
    set(HOST_UNDEF /Umin /Umax /USIGN /UHASH /TC)   # /TC = compile as C
else()
    set(HOST_FO_SIGN "") # No /Fo for most environments

    # Optional strict mode (-std=c11) fails on WSL, missing include files
    # set(HOST_UNDEF -Umin -Umax -USIGN -UHASH -std=c11)
    message(STATUS "Setting -std=gnu11 for HOST_CC=${HOST_CC}")
    set(HOST_UNDEF -Umin -Umax -USIGN -UHASH -std=gnu11)
endif()

# After you set HOST_IS_MSVC / HOST_CC, add:
if(true)
    if (HOST_IS_MSVC)
        set(HOST_UNDEF /Umin /Umax /USIGN /UHASH /TC)   # /TC = compile as C
    else()
        # Optional strict mode (-std=c11) fails on WSL, missing include files
        # set(HOST_UNDEF -Umin -Umax -USIGN -UHASH -std=c11)
        message(STATUS "Setting -std=gnu11 for HOST_CC=${HOST_CC}")
        set(HOST_UNDEF -Umin -Umax -USIGN -UHASH -std=gnu11)
    endif()
endif()
    add_custom_command(
        OUTPUT ${SIGN_TOOL}
        COMMAND "${CMAKE_COMMAND}" -E make_directory ${SIGN_OBJDIR}
        COMMAND "${HOST_CC}" ${HOST_O2} ${HOST_WARN} ${HOST_RUNTIME_FLAG}
                ${HOST_INCLUDES} ${HOST_DEFS} ${HOST_UNDEF} ${SIGN_DEFS}
                ${HOST_FO_SIGN}
                $<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/tools/keytools/sign.c>
                ${KEYTOOL_SOURCES_ABS}
                ${HOST_OUT}$<SHELL_PATH:${SIGN_TOOL}>
                ${HOST_LINK_FLAG} ${HOST_LINK_LIBS}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Building signing tool"
    )

add_custom_target(keytools ALL DEPENDS ${SIGN_TOOL} ${KEYGEN_TOOL})

set_target_properties(sign_host PROPERTIES
    IMPORTED_LOCATION_DEBUG   "${SIGN_TOOL}"
    IMPORTED_LOCATION_RELEASE "${SIGN_TOOL}"
)


set(SIGN_TOOL ${SIGN_TOOL} CACHE INTERNAL "")

#---------------------------------------------------------------------------------------------
# keygen
#---------------------------------------------------------------------------------------------
set(KEYGEN_TOOL ${CMAKE_CURRENT_BINARY_DIR}/keygen${HOST_EXE})
set(KEYGEN_OBJDIR "${CMAKE_CURRENT_BINARY_DIR}/obj_keygen")
if (HOST_IS_MSVC)
    set(HOST_FO_KEYGEN "/Fo$<SHELL_PATH:${KEYGEN_OBJDIR}/>")
else()
    set(HOST_FO_KEYGEN "")
endif()

add_custom_command(
    OUTPUT ${KEYGEN_TOOL}
    COMMAND "${CMAKE_COMMAND}" -E make_directory ${KEYGEN_OBJDIR}
    COMMAND "${HOST_CC}" ${HOST_O2} ${HOST_WARN} ${HOST_RUNTIME_FLAG}
            ${HOST_INCLUDES} ${HOST_DEFS} ${HOST_UNDEF} ${SIGN_DEFS}
            ${HOST_FO_KEYGEN}
            $<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/tools/keytools/keygen.c>
            ${KEYTOOL_SOURCES_ABS}
            ${HOST_OUT}$<SHELL_PATH:${KEYGEN_TOOL}>
            ${HOST_LINK_FLAG} ${HOST_LINK_LIBS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Building keygen tool"
)

add_custom_target(keygen_build DEPENDS ${KEYGEN_TOOL})
add_dependencies(keytools keygen_build)
set(KEYGEN_TOOL ${KEYGEN_TOOL} CACHE INTERNAL "")

list(APPEND WOLFBOOT_INCLUDE_DIRS ${WOLFBOOT_ROOT} ${WOLFBOOT_ROOT}/include)

# set default linker script
set(WOLFBOOT_LSCRIPT_TEMPLATE hal/${WOLFBOOT_TARGET}.ld)

#---------------------------------------------------------------------------------------------
# wolfcrypt
#---------------------------------------------------------------------------------------------
# ---- MUST be placed before add_subdirectory(lib) ----
# Globally sanitize MSVC builds so wolfSSL sources don't inherit bad macros.
# ---- MUST be placed before add_subdirectory(lib) ----
# Make each option a SEPARATE list element; one genex per option.
if (MSVC)
  add_compile_options(
    $<$<C_COMPILER_ID:MSVC>:/Zc:preprocessor>
    $<$<C_COMPILER_ID:MSVC>:/Umin>
    $<$<C_COMPILER_ID:MSVC>:/Umax>
    $<$<C_COMPILER_ID:MSVC>:/USIGN>
    $<$<C_COMPILER_ID:MSVC>:/UHASH>
  )
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ------------------------------------------------------

add_subdirectory(lib)

if(TARGET wolfcrypt)
    set(WOLFSSL_TGT wolfcrypt)
elseif(TARGET wolfssl)
    set(WOLFSSL_TGT wolfssl)
else()
    message(FATAL_ERROR "wolfSSL submodule did not define a 'wolfcrypt' or 'wolfssl' target.")
endif()
message(STATUS "Using wolfSSL target: ${WOLFSSL_TGT}")


if(TARGET ${WOLFSSL_TGT})
    target_compile_definitions(${WOLFSSL_TGT} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(${WOLFSSL_TGT} PRIVATE
                            $<$<C_COMPILER_ID:MSVC>:/Umin>
                            $<$<C_COMPILER_ID:MSVC>:/Umax>
                            $<$<C_COMPILER_ID:MSVC>:/USIGN>
                            $<$<C_COMPILER_ID:MSVC>:/UHASH>
                            $<$<C_COMPILER_ID:MSVC>:/Zc:preprocessor>
    )
endif() # TARGET ${WOLFSSL_TGT}

if(BUILD_TEST_APPS OR BUILD_IMAGE)
    message(STATUS "Building wolfBoot image")
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
target_include_directories(target BEFORE INTERFACE
                                    ${CMAKE_CURRENT_BINARY_DIR}
                                    ${CMAKE_CURRENT_SOURCE_DIR}/lib/wolfssl)


#---------------------------------------------------------------------------------------------
# keystore
#---------------------------------------------------------------------------------------------
set(KEYSTORE ${CMAKE_CURRENT_BINARY_DIR}/keystore.c)
if(NOT SIGN STREQUAL "NONE")
    add_custom_target(keystore DEPENDS ${KEYGEN_TOOL} ${KEYSTORE})

    # generate keystore.c if it does not already exist
    message(STATUS "Will generate fresh: ${KEYSTORE}")
    message(STATUS "  with KEYGEN_TOOL:  ${KEYGEN_TOOL}")
    message(STATUS "  and options:       ${KEYTOOL_OPTIONS}")
    message(STATUS "  with key file:     ${WOLFBOOT_SIGNING_PRIVATE_KEY}")

    add_custom_command(
        OUTPUT ${KEYSTORE} ${WOLFBOOT_SIGNING_PRIVATE_KEY}
        COMMAND "${KEYGEN_TOOL}" ${KEYTOOL_OPTIONS}
                --no-overwrite
                -g ${WOLFBOOT_SIGNING_PRIVATE_KEY}
                -keystoreDir ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${KEYGEN_TOOL}
        WORKING_DIRECTORY ${WOLFBOOT_ROOT}
        COMMENT "Generating keystore.c and signing private key"
        VERBATIM
    )

    add_library(public_key)
    target_sources(public_key PRIVATE ${KEYSTORE})
    target_compile_definitions(public_key PRIVATE ${WOLFBOOT_DEFS})
    target_include_directories(public_key PRIVATE include)
    target_link_libraries(public_key target)

    # Ensure the tool exists before anything tries to build public_key,
    # and also make the explicit custom target a dependency if you like:
    add_dependencies(public_key keygen_build keystore)
endif()

# generate libwolfboot
add_library(wolfboot)
target_sources(wolfboot PRIVATE src/libwolfboot.c ${WOLFBOOT_FLASH_SOURCES})
target_compile_definitions(wolfboot PUBLIC ${WOLFBOOT_DEFS})
target_compile_options(wolfboot PUBLIC ${EXTRA_COMPILE_OPTIONS})
target_include_directories(wolfboot PUBLIC ${WOLFBOOT_INCLUDE_DIRS})
target_link_libraries(wolfboot wolfboothal target ${WOLFSSL_TGT})

if(HOST_IS_MSVC) # Some VS2022 helpers
    message(STATUS "==============================================================================")
    message(STATUS "Begin sign host")
    message(STATUS "==============================================================================")
    foreach(_t IN ITEMS wolfssl wolfcrypt)
      if (TARGET ${_t})
        set_target_properties(${_t} PROPERTIES
          ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
          ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
          ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
          DEBUG_POSTFIX ""
        )
      endif()
    endforeach()

    message(STATUS "WOLFSSL_TGT=${WOLFSSL_TGT}")
    add_dependencies(sign_host ${WOLFSSL_TGT})
    add_dependencies(sign_host keytools)

    if(MSVC)
        message(STATUS "Found MSVC")
    else()
        message(STATUS "Did not find MSVC")
    endif()
    message(STATUS "CMAKE_C_COMPILER_ID=${CMAKE_C_COMPILER_ID}")
    message(STATUS "CMAKE_CXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}")
    message(STATUS "CMAKE_C_COMPILER_FRONTEND_VARIANT=${CMAKE_C_COMPILER_FRONTEND_VARIANT}")
    message(STATUS "CMAKE_CXX_COMPILER_FRONTEND_VARIANT=${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}")
    message(STATUS "MSVC var = ${MSVC}")

    target_compile_options(wolfboot PRIVATE  ${SIM_COMPILE_OPTIONS})

    set_property(TARGET sign_host PROPERTY VS_DEBUGGER_COMMAND_ARGUMENTS
                        "test.bin wolfboot_signing_private_key.der 1 --ed25519 --sha256")
    set_property(TARGET sign_host PROPERTY VS_DEBUGGER_WORKING_DIRECTORY
                        "${CMAKE_CURRENT_BINARY_DIR}")
endif() # HOST_IS_MSVC VS2022 helpers

message(STATUS "End [WOLFBOOT_ROOT]/CmakeLists.txt")
