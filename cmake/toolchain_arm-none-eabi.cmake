# wolfboot/cmake/toolchain_arm-none-eabi.cmake
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

# Ensure this file is only included and initialized once
if(CMAKE_VERSION VERSION_LESS 3.10)
    # Fallback path for older CMake
    if(DEFINED TOOLCHAIN_ARM_NONE_EABI_CMAKE_INCLUDED)
        return()
    endif()
else()
    include_guard(GLOBAL)
endif()

set(CMAKE_SYSTEM_NAME Generic)

# Keep try-compile from attempting to run target binaries
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ARM_GCC_BIN WOLFBOOT_TARGET)

# There needs to be a default platform or the `project()` command will fail.
if(NOT DEFINED WOLFBOOT_TARGET)
    message(STATUS "Select a target, e.g. 'cmake --preset stm32l4'")
    message(FATAL_ERROR "WOLFBOOT_TARGET not set")
    # set(WOLFBOOT_TARGET "stm32h7")
endif()

# Cortex-M CPU
# TODO move to presets
if(WOLFBOOT_TARGET STREQUAL "stm32l0")
    set(CMAKE_SYSTEM_PROCESSOR cortex-m0)
    set(MCPU_FLAGS "-mcpu=cortex-m0 -mthumb -mlittle-endian -mthumb-interwork ")
elseif(WOLFBOOT_TARGET STREQUAL "stm32u5" OR WOLFBOOT_TARGET STREQUAL "stm32h5" OR
       WOLFBOOT_TARGET STREQUAL "stm32l5")
    set(CMAKE_SYSTEM_PROCESSOR cortex-m33)
    set(MCPU_FLAGS "-mcpu=cortex-m33 -mthumb -mlittle-endian -mthumb-interwork -Ihal -DCORTEX_M33")
elseif(WOLFBOOT_TARGET STREQUAL "stm32h7")
    set(CMAKE_SYSTEM_PROCESSOR cortex-m7)
    set(MCPU_FLAGS "-mcpu=cortex-m7 -mthumb -mlittle-endian -mthumb-interwork")
elseif(WOLFBOOT_TARGET STREQUAL "stm32l4")
    set(CMAKE_SYSTEM_PROCESSOR cortex-m4)
    # L4 has FPU (single-precision). Let the toolchain pick the right libs.
    set(MCPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
else()
    set(CMAKE_SYSTEM_PROCESSOR cortex-m3)
    set(MCPU_FLAGS "-mcpu=cortex-m3 -mthumb -mlittle-endian -mthumb-interwork ")
endif()

# ----- Select compilers (works on WSL/Linux and Windows) -----
# Optional: allow an explicit bin dir
set(ARM_GCC_BIN "" CACHE PATH "Path to Arm GNU Toolchai       'bin' directory")

if(CMAKE_HOST_WIN32)
    message(STATUS "toolchain_arm-none-eabi.cmake is CMAKE_HOST_WIN32 mode")
    if(ARM_GCC_BIN)
        file(TO_CMAKE_PATH "${ARM_GCC_BIN}" _BIN)
        set(CMAKE_C_COMPILER   "${_BIN}/arm-none-eabi-gcc.exe"   CACHE FILEPATH "" FORCE)
        set(CMAKE_CXX_COMPILER "${_BIN}/arm-none-eabi-g++.exe"   CACHE FILEPATH "" FORCE)
        set(CMAKE_ASM_COMPILER "${_BIN}/arm-none-eabi-gcc.exe"   CACHE FILEPATH "" FORCE)
    else()
        # Try PATH
        find_program(CMAKE_C_COMPILER   NAMES arm-none-eabi-gcc.exe
                     HINTS
                           "C:/Program Files/Ninja"
                           "C:/SysGCC/arm-eabi/bin"
                           "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin"
                           "C:/ST/STM32CubeIDE_1.14.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.11.3.rel1.win32_1.1.100.202309141235/tools/bin"
                    )
        find_program(CMAKE_CXX_COMPILER NAMES arm-none-eabi-g++.exe
                     HINTS
                           "C:/Program Files/Ninja"
                           "C:/SysGCC/arm-eabi/bin"
                           "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin"
                           "C:/ST/STM32CubeIDE_1.14.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.11.3.rel1.win32_1.1.100.202309141235/tools/bin"
                    )

        set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}" CACHE FILEPATH "" FORCE)
    endif()
else()
    message(STATUS "toolchain_arm-none-eabi.cmake checking for arm compiler")
    if(ARM_GCC_BIN)
        file(TO_CMAKE_PATH "${ARM_GCC_BIN}" _BIN)
        set(CMAKE_C_COMPILER   "${_BIN}/arm-none-eabi-gcc" CACHE FILEPATH "" FORCE)
        set(CMAKE_CXX_COMPILER "${_BIN}/arm-none-eabi-g++" CACHE FILEPATH "" FORCE)
        set(CMAKE_ASM_COMPILER "${_BIN}/arm-none-eabi-gcc" CACHE FILEPATH "" FORCE)
    else()
        # Assume Mac / Linux is in path. No hints.
        find_program(CMAKE_C_COMPILER   NAMES arm-none-eabi-gcc)
        find_program(CMAKE_CXX_COMPILER NAMES arm-none-eabi-g++)
        set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}" CACHE FILEPATH "" FORCE)
    endif()
endif()
message(STATUS "Found CMAKE_C_COMPILER=${CMAKE_C_COMPILER}")

# Use the compiler's own include dir (Homebrew GCC may have no sysroot)
execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=include
    OUTPUT_VARIABLE GCC_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(GCC_INCLUDE_DIR AND EXISTS "${GCC_INCLUDE_DIR}")
    include_directories(SYSTEM "${GCC_INCLUDE_DIR}")
endif()

# get toolchain version. CMAKE_C_COMPILER_VERSION cannot be used here since its not defined until
# `project()` is run in the top-level cmake. The toolchain has to be setup before the `project` call
execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-sysroot
    OUTPUT_VARIABLE GCC_SYSROOT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(GCC_SYSROOT)
    set(CMAKE_SYSROOT "${GCC_SYSROOT}")
endif()


# Some sanity checks on compiler and target OS
# TODO remove OS specific presets
if(NOT CMAKE_C_COMPILER OR NOT CMAKE_CXX_COMPILER)
    if("${TARGET_OS}" STREQUAL "")
        message(STATUS "Warning: cmake presets should define TARGET_OS = [WINDOWS | LINUX]")
    endif()
    if(CMAKE_HOST_WIN32)
        if("${TARGET_OS}" STREQUAL "LINUX")
            message(FATAL_ERROR "Linux presets are not supported in Windows. Choose a different preset.")
        endif()
    else()
        if("${TARGET_OS}" STREQUAL "Windows")
            message(FATAL_ERROR "Windows presets are only supported on Windows. Choose a different preset.")
        endif()
    endif()
    message(FATAL_ERROR "arm-none-eabi toolchain not found. Set ARM_GCC_BIN or add to PATH.")
endif()

#---------------------------------------------------------------------------------------------
# Set compiler/linker flags
#---------------------------------------------------------------------------------------------
set(OBJECT_GEN_FLAGS
    "${MCPU_FLAGS} -Wall -Wextra -Wno-main -ffreestanding -Wno-unused -ffunction-sections -fdata-sections"
)

# NOTE: Use CMAKE_*_STANDARD instead of -std=
set(CMAKE_C_FLAGS   "${OBJECT_GEN_FLAGS}" CACHE INTERNAL "C Compiler options")
set(CMAKE_ASM_FLAGS "${OBJECT_GEN_FLAGS}" CACHE INTERNAL "ASM Compiler options")
set(CMAKE_EXE_LINKER_FLAGS "${MCPU_FLAGS} ${LD_FLAGS} -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs" CACHE INTERNAL "Linker options")

#---------------------------------------------------------------------------------------------
# Set compilers and toolchain utilities
#---------------------------------------------------------------------------------------------
#---------------------------------------------------------------------------------------------
# Derive toolchain helper paths from the chosen compiler
#---------------------------------------------------------------------------------------------
get_filename_component(_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
if(CMAKE_HOST_WIN32)
    set(_EXE ".exe")
else()
    set(_EXE "")
endif()


set(TOOLCHAIN_AR      "${_BIN_DIR}/arm-none-eabi-ar${_EXE}"      CACHE INTERNAL "")
set(TOOLCHAIN_OBJCOPY "${_BIN_DIR}/arm-none-eabi-objcopy${_EXE}" CACHE INTERNAL "")
set(TOOLCHAIN_OBJDUMP "${_BIN_DIR}/arm-none-eabi-objdump${_EXE}" CACHE INTERNAL "")
set(TOOLCHAIN_SIZE    "${_BIN_DIR}/arm-none-eabi-size${_EXE}"    CACHE INTERNAL "")


# set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_PREFIX}/${${TOOLCHAIN}} ${CMAKE_PREFIX_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

message(STATUS "Cross-compiling using GNU arm-none-eabi toolchain")

# Options for DEBUG build
# -Og   Enables optimizations that do not interfere with debugging.
# -g    Produce debugging information in the operating system's native format.
set(CMAKE_C_FLAGS_DEBUG         "-Og -g"    CACHE INTERNAL "C Compiler options for debug build type")
set(CMAKE_CXX_FLAGS_DEBUG       "-Og -g"    CACHE INTERNAL "C++ Compiler options for debug build type")
set(CMAKE_ASM_FLAGS_DEBUG       "-g"        CACHE INTERNAL "ASM Compiler options for debug build type")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG ""         CACHE INTERNAL "Linker options for debug build type")

# Options for RELEASE build
# -Os   Optimize for size. -Os enables all -O2 optimizations.
# -DNDEBUG ensure assertions are disabled
set(CMAKE_C_FLAGS_RELEASE   "-Os -g -DNDEBUG"     CACHE INTERNAL "C Compiler options for release build type")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g -DNDEBUG"     CACHE INTERNAL "C++ Compiler options for release build type")
set(CMAKE_ASM_FLAGS_RELEASE ""           CACHE INTERNAL "ASM Compiler options for release build type")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE ""    CACHE INTERNAL "Linker options for release build type")

# Options for RELWITHDEBINFO build
set(CMAKE_C_FLAGS_RELWITHDEBINFO   "-Os -g -DNDEBUG"  CACHE INTERNAL "C Compiler options for release with debug symbols build type")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-Os -g -DNDEBUG"  CACHE INTERNAL "C++ Compiler options for release with debug symbols build type")
set(CMAKE_ASM_FLAGS_RELWITHDEBINFO ""        CACHE INTERNAL "ASM Compiler options for release with debug symbols build type")
set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "" CACHE INTERNAL "Linker options for release with debug symbols build type")

# Options for MINSIZEREL build
# -flto, -Wl,-flto   Link Time Optimization. Allows size optimizations to occur across compilation units at link time
set(CMAKE_C_FLAGS_MINSIZEREL   "-Os -DNDEBUG -flto -Wl,-flto"     CACHE INTERNAL "C Compiler options for minimum size release build type")
set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -DNDEBUG -flto -Wl,-flto"     CACHE INTERNAL "C++ Compiler options for minimum size release build type")
set(CMAKE_ASM_FLAGS_MINSIZEREL ""        CACHE INTERNAL "ASM Compiler options for minimum size release build type")
set(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "-flto -Wl,-flto" CACHE INTERNAL "Linker options for minimum size release build type")


# Locate the GNU Arm bin dir from the compiler path
get_filename_component(_gcc_dir "${CMAKE_C_COMPILER}" DIRECTORY)

# Prefer the tool right next to the compiler
find_program(CMAKE_SIZE
    NAMES arm-none-eabi-size
    HINTS "${_gcc_dir}"
    NO_DEFAULT_PATH
)
# Fallback: PATH search
if(NOT CMAKE_SIZE)
    find_program(CMAKE_SIZE NAMES arm-none-eabi-size)
endif()

# Make it visible to all dirs and saved in CMakeCache.txt
if(CMAKE_SIZE)
    set(CMAKE_SIZE "${CMAKE_SIZE}" CACHE FILEPATH "Path to arm-none-eabi-size")
else()
    message(STATUS "CMAKE_SIZE arm-none-eabi-size not found; add your ARM GCC bin dir to PATH or fix the toolchain hints.")
endif()

set(TOOLCHAIN_ARM_NONE_EABI_CMAKE_INCLUDED true)
