# wolfboot/cmake/stm32_hal_download.cmake
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

# If not found:
#   1) The CubeIDE
#   2) VisualGDB/EmbeddedBSPs/arm-eabi/com.sysprogs.arm.stm32
#   3) User-specified
#
# ... then download HAL files as needed:

# Ensure this file is only included and initialized once
if(CMAKE_VERSION VERSION_LESS 3.10)
    # Fallback path for older CMake, and anything else that wants to detect is loaded
    if(DEFINED STM32_HAL_DOWNLOAD_CMAKE_INCLUDED)
        return()
    endif()
else()
    include_guard(GLOBAL)
endif()


if(ENABLE_HAL_DOWNLOAD) # Entire file wrapper
    include(FetchContent)


    if(NOT FUNCTIONS_CMAKE_INCLUDED)
        include(cmake/functions.cmake)
    endif()

    # Accumulators for the DSL
    set(_DL_NAMES)
    set(_DL_URLS)
    set(_DL_TAGS)

    # Mini DSL
    function(add_download)
        cmake_parse_arguments(AD "" "" "NAME;URL;TAG" ${ARGN})
        if(NOT AD_NAME)
            message(FATAL_ERROR "add_download requires NAME")
        endif()
        if(NOT AD_URL)
            message(FATAL_ERROR "add_download requires URL")
        endif()
        if(NOT AD_TAG)
            set(AD_TAG "master")
        endif()

        list(APPEND _DL_NAMES "${AD_NAME}")
        list(APPEND _DL_URLS  "${AD_URL}")
        list(APPEND _DL_TAGS  "${AD_TAG}")

        set(_DL_NAMES "${_DL_NAMES}" PARENT_SCOPE)
        set(_DL_URLS  "${_DL_URLS}"  PARENT_SCOPE)
        set(_DL_TAGS  "${_DL_TAGS}"  PARENT_SCOPE)
    endfunction()

    set(DOWNLOADS_FOUND false)
    # If a downloads list is provided, include it
    if(DEFINED WOLFBOOT_DOWNLOADS_CMAKE)
        if(EXISTS "${WOLFBOOT_DOWNLOADS_CMAKE}")
            message(STATUS "Including downloads list: ${WOLFBOOT_DOWNLOADS_CMAKE}")
            include("${WOLFBOOT_DOWNLOADS_CMAKE}")
            set(DOWNLOADS_FOUND true)
        else()
            # If there's a defined download, the file specified needs to exist!
            message(FATAL_ERROR "WOLFBOOT_DOWNLOADS_CMAKE enabled but file now found: ${WOLFBOOT_DOWNLOADS_CMAKE}")
        endif()
    else()
        message(STATUS "No WOLFBOOT_DOWNLOADS_CMAKE and no builtin defaults for target: ${WOLFBOOT_TARGET}. Skipping auto downloads.")
    endif() # WOLFBOOT_DOWNLOADS_CMAKE

    # Fallback: The stm32l4 trio is known to be needed, so hard-coded here:
    if(WOLFBOOT_TARGET STREQUAL "stm32l4" AND (NOT DOWNLOADS_FOUND))
        message(STATUS "WARNING not downloads found for known target needing them: stm32l4" )

        add_download(
            NAME st_hal
            URL  https://github.com/STMicroelectronics/stm32l4xx_hal_driver.git
            TAG  v1.13.5
        )
        add_download(
            NAME cmsis_dev
            URL  https://github.com/STMicroelectronics/cmsis_device_l4.git
            TAG  v1.7.4
        )
        add_download(
            NAME cmsis_core
            URL  https://github.com/ARM-software/CMSIS_5.git
            TAG  5.9.0
        )
    endif()


    # Validate lists are aligned
    list(LENGTH _DL_NAMES _n1)
    list(LENGTH _DL_URLS  _n2)
    list(LENGTH _DL_TAGS  _n3)
    if(NOT (_n1 EQUAL _n2 AND _n1 EQUAL _n3))
        message(FATAL_ERROR "add_download internal list length mismatch: names=${_n1} urls=${_n2} tags=${_n3}")
    endif()

    # Nothing to do
    if(_n1 EQUAL 0)
        set(STM32_HAL_DOWNLOAD_CMAKE_INCLUDED TRUE)
        #---------------------------------------------------------------------------------------------
        message(STATUS "No files found needing to be downloaded. If needed, configure WOLFBOOT_DOWNLOADS_CMAKE")
        return()
        #---------------------------------------------------------------------------------------------
    endif()

    # Fetch loop
    set(FETCHCONTENT_QUIET OFF)
    set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")

    set(_ALL_NAMES)
    math(EXPR _last "${_n1} - 1")
    foreach(i RANGE 0 ${_last})
        list(GET _DL_NAMES ${i} _name)
        list(GET _DL_URLS  ${i} _url)
        list(GET _DL_TAGS  ${i} _tag)

        message(STATUS "Fetching ${_url} (tag ${_tag})")
        FetchContent_Declare(${_name}
            GIT_REPOSITORY "${_url}"
            GIT_TAG        "${_tag}"
            GIT_SHALLOW    TRUE
            GIT_PROGRESS   FALSE
        )
        list(APPEND _ALL_NAMES "${_name}")
    endforeach()

    if(_ALL_NAMES)
        FetchContent_MakeAvailable(${_ALL_NAMES})
    endif()

    # st_hal
    FetchContent_GetProperties(st_hal)  # ensures *_SOURCE_DIR vars are available
    if(DEFINED st_hal_SOURCE_DIR AND EXISTS "${st_hal_SOURCE_DIR}")
        set_and_echo_dir(HAL_BASE "${st_hal_SOURCE_DIR}")
        set_and_echo_dir(HAL_DRV  "${st_hal_SOURCE_DIR}")
    else()
        message(FATAL_ERROR "st_hal source dir not found; expected after FetchContent.")
    endif()

    # cmsis_dev
    FetchContent_GetProperties(cmsis_dev)
    if(DEFINED cmsis_dev_SOURCE_DIR AND EXISTS "${cmsis_dev_SOURCE_DIR}")
        set_and_echo_dir(HAL_CMSIS_DEV "${cmsis_dev_SOURCE_DIR}/Include")
    else()
        message(FATAL_ERROR "cmsis_dev source dir not found.")
    endif()

    # cmsis_core
    FetchContent_GetProperties(cmsis_core)
    if(DEFINED cmsis_core_SOURCE_DIR AND EXISTS "${cmsis_core_SOURCE_DIR}")
        set_and_echo_dir(HAL_CMSIS_CORE "${cmsis_core_SOURCE_DIR}/CMSIS/Core/Include")
    else()
        message(FATAL_ERROR "cmsis_core source dir not found.")
    endif()


    # Map include directories when known names are fetched
    # Adjust or extend this block if you add more components
    if(TARGET st_hal)
        set_and_echo_dir(HAL_BASE "${st_hal_SOURCE_DIR}")
        set_and_echo_dir(HAL_DRV  "${st_hal_SOURCE_DIR}")
    endif()

    if(TARGET cmsis_dev)
        set_and_echo_dir(HAL_CMSIS_DEV  "${cmsis_dev_SOURCE_DIR}/Include")
    endif()

    if(TARGET cmsis_core)
        set_and_echo_dir(HAL_CMSIS_CORE "${cmsis_core_SOURCE_DIR}/CMSIS/Core/Include")
    endif()

endif() #ENABLE_HAL_DOWNLOAD

set(STM32_HAL_DOWNLOAD_CMAKE_INCLUDED true)
