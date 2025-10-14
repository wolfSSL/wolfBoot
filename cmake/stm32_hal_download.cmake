# wolfboot/cmake/stm32_hal_download.cmake
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

# If not found:
#   The CubeIDE
#   VisualGDB/EmbeddedBSPs/arm-eabi/com.sysprogs.arm.stm32
#
# ... then download HAL files as needed

if(NOT functions_cmake_loaded)
    include(cmake/functions.cmake)
endif()

if(WOLFBOOT_TARGET STREQUAL "stm32l4")
    if(FOUND_STM32L4_LIB)
        message(STATUS "stm32_hal_download.cmake skipped, already found STM32 HAL lib.")
    else()
        include(FetchContent)
        # TIP: Always pin a real tag/commit; avoid main/master.

        # Make behavior explicit & chatty while debugging
        set(FETCHCONTENT_QUIET OFF)
        set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")

        # HAL driver
        message(STATUS "Fetching https://github.com/STMicroelectronics/stm32l4xx_hal_driver.git")
        FetchContent_Declare(st_hal
          GIT_REPOSITORY https://github.com/STMicroelectronics/stm32l4xx_hal_driver.git
          # Pick a tag you want to lock to:
          GIT_TAG        v1.13.5
          GIT_SHALLOW    TRUE
          GIT_PROGRESS   FALSE
        )

        # CMSIS device headers for L4
        message(STATUS "Fetching https://github.com/STMicroelectronics/cmsis_device_l4.git")
        FetchContent_Declare(cmsis_dev
          GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis_device_l4.git
          GIT_TAG        v1.7.4
          GIT_SHALLOW    TRUE
          GIT_PROGRESS   FALSE
        )

        # CMSIS Core headers
        message(STATUS "Fetching https://github.com/ARM-software/CMSIS_5.git")
        FetchContent_Declare(cmsis_core
          GIT_REPOSITORY https://github.com/ARM-software/CMSIS_5.git
          GIT_TAG        5.9.0
          GIT_SHALLOW    TRUE
          GIT_PROGRESS   FALSE
        )

        FetchContent_MakeAvailable(st_hal cmsis_dev cmsis_core)

        # Map to the include structures of the fetched repos
        message("stm32_hal_download.cmake setting hal directories:")
        set_and_echo_dir(HAL_BASE       "${st_hal_SOURCE_DIR}")
        set_and_echo_dir(HAL_DRV        "${st_hal_SOURCE_DIR}")                                   # Inc/, Src/
        set_and_echo_dir(HAL_CMSIS_DEV  "${cmsis_dev_SOURCE_DIR}/Include")                        # device
        set_and_echo_dir(HAL_CMSIS_CORE "${cmsis_core_SOURCE_DIR}/CMSIS/Core/Include")            # core
    endif()
endif()
