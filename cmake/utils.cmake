# utils.cmake
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

# --------------------------------------------------------------------------------------------------
# Utility for properly installing a file output regardless of if the current configuration is multi
# config or not
# --------------------------------------------------------------------------------------------------

macro(multiConfigFileInstall FILE_OUT DESTINATION)
    # check for multi-config
    get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(isMultiConfig)
        install(
            FILES ${CMAKE_CURRENT_BINARY_DIR}/Debug/${FILE_OUT}
            CONFIGURATIONS Debug
            DESTINATION ${DESTINATION})
        install(
            FILES ${CMAKE_CURRENT_BINARY_DIR}/RelWithDebInfo/${FILE_OUT}
            CONFIGURATIONS RelWithDebInfo
            DESTINATION ${DESTINATION})
        install(
            FILES ${CMAKE_CURRENT_BINARY_DIR}/Release/${FILE_OUT}
            CONFIGURATIONS Release
            DESTINATION ${DESTINATION})
        install(
            FILES ${CMAKE_CURRENT_BINARY_DIR}/MinSizeRel/${FILE_OUT}
            CONFIGURATIONS MinSizeRel
            DESTINATION ${DESTINATION})
    else()
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${FILE_OUT} DESTINATION ${DESTINATION})
    endif()
endmacro()

# --------------------------------------------------------------------------------------------------
# Utility for creating MCU binary outputs
# --------------------------------------------------------------------------------------------------
macro(gen_bin_target_outputs TARGET)
    get_filename_component(FILENAME ${TARGET} NAME_WE)

    # Create bin from elf target
    add_custom_command(
        OUTPUT ${FILENAME}.bin
        DEPENDS ${TARGET}
        COMMAND ${TOOLCHAIN_OBJCOPY} -O binary $<TARGET_FILE:${TARGET}>
                $<TARGET_FILE:${TARGET}>.bin)
    list(APPEND TARGET_OUTPUTS ${FILENAME}.bin)

    # Print size of bin target
    add_custom_command(
        OUTPUT ${FILENAME}.size
        DEPENDS ${TARGET}
        COMMAND ${TOOLCHAIN_SIZE} $<TARGET_FILE:${TARGET}> | tee $<TARGET_FILE:${TARGET}>.size)
    list(APPEND TARGET_OUTPUTS ${FILENAME}.size)

    # Add top level target for all MCU standard outputs
    add_custom_target(${FILENAME}_outputs ALL DEPENDS ${TARGET_OUTPUTS})
endmacro()
