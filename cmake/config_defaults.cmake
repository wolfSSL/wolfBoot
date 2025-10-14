# wolfboot/cmake/config_defaults.cmake
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
#

# This is NOT a place for device-specific project settings. For that, see CMakePresets.json

set(FOUND_STM32L4_LIB false)

include(cmake/current_user.cmake)

get_current_user(CURRENT_USER)
message(STATUS "Current user detected: ${CURRENT_USER}")


# The ST CubeIDE location is searched in cmake/cube_ide_config.cmake
# Want to specify your specific STCubeIDE? Uncomment and set it here:
#   set(STM32CUBEIDE_DIR "/your/path")

if(false)
    # TODO need to be more generic, in presets?
    if(IS_DIRECTORY  "C:/Users/${CURRENT_USER}/AppData/Local/VisualGDB")
        set(LIB_STM32L4_WINDOWS "C:/Users/${CURRENT_USER}/AppData/Local/VisualGDB/EmbeddedBSPs/arm-eabi/com.sysprogs.arm.stm32/STM32L4xxxx")
    endif()

    if(IS_DIRECTORY  "/mnt/c/Users/${CURRENT_USER}/AppData/Local/VisualGDB")
        set(LIB_STM32L4_WSL "/mnt/c/Users/${CURRENT_USER}/AppData/Local/VisualGDB/EmbeddedBSPs/arm-eabi/com.sysprogs.arm.stm32/STM32L4xxxx")
    endif()

    if(IS_DIRECTORY "${LIB_STM32L4_WINDOWS}")
        set(FOUND_STM32L4_LIB true)
        message(STATUS "LIB_STM32L4_WINDOWS found: ${LIB_STM32L4_WINDOWS}")
    endif()

    if(IS_DIRECTORY "${LIB_STM32L4_WSL}")
        set(FOUND_STM32L4_LIB true)
        message(STATUS "LIB_STM32L4_WSL found: ${LIB_STM32L4_WSL}")
    endif()
endif()

# set(ARM_GCC_BIN "")


message(STATUS "config.defaults:")
message(STATUS "-- HAL_DRV:       ${HAL_DRV}")
message(STATUS "-- HAL_CMSIS_DEV: ${HAL_CMSIS_DEV}")
message(STATUS "-- HAL_CMSIS_CORE:${HAL_CMSIS_CORE}")
