# toolchain_aarch64-none-elf.cmake
#
# Copyright (C) 2022 wolfSSL Inc.
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


set(CMAKE_SYSTEM_NAME Generic)

# There needs to be a default platform or the `project()` command will fail.
if(NOT DEFINED WOLFBOOT_TARGET)
    set(WOLFBOOT_TARGET "stm32h7")
endif()

# MARCH flag
set(MARCH_FLAGS "-march=armv8-a")

# -----------------------------------------------------------------------------
# Set toolchain paths
# -----------------------------------------------------------------------------
set(TOOLCHAIN aarch64-none-elf)
set(CMAKE_CXX_STANDARD 20)

find_program(TOOLCHAIN_GCC_PATH
  NAMES ${TOOLCHAIN}-gcc
  HINTS $ENV{PATH}
)

# get toolchain version. CMAKE_C_COMPILER_VERSION cannot be used here since its not defined until
# `project()` is run in the top-level cmake. The toolchain has to be setup before the `project` call
execute_process(
    COMMAND ${TOOLCHAIN}-gcc -dumpversion
    OUTPUT_VARIABLE TOOLCHAIN_GCC_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE)

get_filename_component(TOOLCHAIN_BIN_DIR ${TOOLCHAIN_GCC_PATH} DIRECTORY)
get_filename_component(TOOLCHAIN_ROOT_DIR "${TOOLCHAIN_BIN_DIR}/../" DIRECTORY ABSOLUTE)
set(CMAKE_SYSROOT ${TOOLCHAIN_ROOT_DIR}/${TOOLCHAIN})


# -----------------------------------------------------------------------------
# Set compiler/linker flags
#-----------------------------------------------------------------------------
set(OBJECT_GEN_FLAGS
    "${MARCH_FLAGS} -Wall -Wextra -Wno-main -ffreestanding -Wno-unused -ffunction-sections -fdata-sections"
)

# NOTE: Use CMAKE_*_STANDARD instead of -std=
set(CMAKE_C_FLAGS   "${OBJECT_GEN_FLAGS}" CACHE INTERNAL "C Compiler options")
set(CMAKE_ASM_FLAGS "${OBJECT_GEN_FLAGS}" CACHE INTERNAL "ASM Compiler options")
set(CMAKE_EXE_LINKER_FLAGS "${MCPU_FLAGS} ${LD_FLAGS} -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs" CACHE INTERNAL "Linker options")

#---------------------------------------------------------------------------------------
# Set compilers and toolchain utilities
#---------------------------------------------------------------------------------------
set(CMAKE_C_COMPILER    ${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN}-gcc       CACHE INTERNAL "C Compiler")
set(CMAKE_CXX_COMPILER  ${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN}-g++       CACHE INTERNAL "C++ Compiler")
set(CMAKE_ASM_COMPILER  ${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN}-gcc       CACHE INTERNAL "ASM Compiler")
set(TOOLCHAIN_LD        ${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN}-ld        CACHE INTERNAL "Toolchain linker")
set(TOOLCHAIN_AR        ${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN}-gcc-ar    CACHE INTERNAL "Toolchain archive tool")
set(TOOLCHAIN_OBJCOPY   ${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN}-objcopy   CACHE INTERNAL "Toolchain objcopy tool")
set(TOOLCHAIN_OBJDUMP   ${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN}-objdump   CACHE INTERNAL "Toolchain objdump tool")
set(TOOLCHAIN_SIZE      ${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN}-size      CACHE INTERNAL "Toolchain object size tool")

set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_PREFIX}/${${TOOLCHAIN}} ${CMAKE_PREFIX_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

message(STATUS "Cross-compiling using GNU aarch64-none-elf toolchain")

# Options for DEBUG build
# -Og   Enables optimizations that do not interfere with debugging.
# -g    Produce debugging information in the operating system’s native format.
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
