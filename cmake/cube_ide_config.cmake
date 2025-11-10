# wolfboot/cmake/cube_ide_config.cmake
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

# Some logic to find the ST Cube IDE in various directories on various systems
# See also https://www.st.com/resource/en/application_note/an5952-how-to-use-cmake-in-stm32cubeide-stmicroelectronics.pdf

# Usage:
#   set(STM32CUBEIDE_DIR "C:/ST/STM32CubeIDE_1.15.0" CACHE PATH "Hint to STM32CubeIDE root")
#   find_package(STM32CubeIDE REQUIRED)
#   message(STATUS "STM32CubeIDE: ${STM32CUBEIDE_EXECUTABLE} (root: ${STM32CUBEIDE_ROOT}, ver: ${STM32CUBEIDE_VERSION})")

# Ensure this file is only included and initialized once
if(CMAKE_VERSION VERSION_LESS 3.10)
    # Fallback path for older CMake
    if(DEFINED CUBE_IDE_CONFIG_CMAKE_INCLUDED)
        return()
    endif()
else()
    include_guard(GLOBAL)
endif()

if(DEFINED FUNCTIONS_CMAKE_INCLUDED)
    message(STATUS "Found required functions.cmake")
else()
    message(FATL_ERROR "Missing required functions.cmake")
endif()

# Exclude entire file unless DETECT_CUBEIDE is set to true
if(DETECT_CUBEIDE)

message(STATUS "Begin cube_ide_config.cmake")
unset(STM32CUBEIDE_ROOT       CACHE)
unset(STM32CUBEIDE_FOUND      CACHE)
unset(STM32CUBEIDE_VERSION    CACHE)
unset(STM32CUBEIDE_EXECUTABLE CACHE)

function(_stm32cubeide_set_from_exec PARAM_EXE)
    if(NOT EXISTS "${PARAM_EXE}")
        return()
    endif()
    set(STM32CUBEIDE_EXECUTABLE "${PARAM_EXE}" PARENT_SCOPE)
    # Root: up two dirs works for Linux default; handle macOS bundle separately below.
    get_filename_component(_dir "${PARAM_EXE}" DIRECTORY)
    if(CMAKE_HOST_APPLE AND _dir MATCHES "\\.app/Contents/MacOS$")
        get_filename_component(_root "${_dir}/../.." REALPATH)
    else()
        get_filename_component(_root "${_dir}/.." REALPATH)
    endif()

    message(STATUS "Found STM32CUBEIDE_ROOT=${_root}")
    set(STM32CUBEIDE_ROOT "${_root}" PARENT_SCOPE)

    # Version extract from directory names like STM32CubeIDE_1.15.0
    file(TO_CMAKE_PATH "${_root}" _root_norm)
    get_filename_component(_leaf "${_root_norm}" NAME)  # e.g. "STM32CubeIDE_1.14.1"

    set(_ver "")
    set(_mark "STM32CubeIDE_")
    string(FIND "${_leaf}" "${_mark}" _pos)
    if(NOT _pos EQUAL -1)
        string(LENGTH "${_mark}" _mlen)
        math(EXPR _start "${_pos} + ${_mlen}")
        string(SUBSTRING "${_leaf}" ${_start} -1 _ver_raw)
        string(STRIP "${_ver_raw}" _ver)
    endif()

    if(_ver) # e.g. "1.14.1"
        # set both locally and in parent scope for immediate logging + export
        set(STM32CUBEIDE_VERSION "${_ver}")
        set(STM32CUBEIDE_VERSION "${_ver}" PARENT_SCOPE)
        message(NOTICE "Found STM32CUBEIDE_VERSION=${_ver}")
    else()
        message(VERBOSE "Could not derive version (leaf='${_leaf}', root='${_root_norm}')")
    endif()
endfunction()

# Finds the newest STM32Cube L4 firmware folder under the standard Repository path.
# Usage:
#     find_newest_stm32cube_fw_l4(OUT_DIR OUT_VER)
# After the call:
#     OUT_DIR = full path to the newest STM32Cube_FW_L4_Vx.y.z directory
#     OUT_VER = version string x.y.z
#
# Optional inputs that you may predefine before calling:
#     CURRENT_USER          Used only on Windows if USERPROFILE is not set
#     STM32CUBE_REPO_HINT   Override the Repository root folder if you know it already
#
# Examples:
#     find_newest_stm32cube_fw_l4(STM32CUBE_L4_ROOT STM32CUBE_L4_VERSION)
#     message(STATUS "STM32Cube L4 root: ${STM32CUBE_L4_ROOT} (version ${STM32CUBE_L4_VERSION})")
function(find_newest_stm32cube_fw_l4 OUT_DIR OUT_VER)
    set(_repo_root "")

    # 1) If the caller provided a direct hint, use it
    if(DEFINED STM32CUBE_REPO_HINT AND EXISTS "${STM32CUBE_REPO_HINT}")
        set(_repo_root "${STM32CUBE_REPO_HINT}")
    else()
        # 2) Build the default path based on platform
        if(CMAKE_HOST_WIN32)
            # Prefer USERPROFILE if available
            set(_userprofile "$ENV{USERPROFILE}")
            if(_userprofile STREQUAL "")
                # Fallback to C:/Users/<CURRENT_USER>
                if(NOT DEFINED CURRENT_USER OR CURRENT_USER STREQUAL "")
                    set(_env_user "$ENV{USERNAME}")
                    if(NOT _env_user STREQUAL "")
                        set(CURRENT_USER "${_env_user}")
                    endif()
                endif()
                if(DEFINED CURRENT_USER AND NOT CURRENT_USER STREQUAL "")
                    set(_repo_root "C:/Users/${CURRENT_USER}/STM32Cube/Repository")
                endif()
            else()
                # Convert backslashes to forward slashes for CMake path sanity
                file(TO_CMAKE_PATH "${_userprofile}" _userprofile_cmake)
                set(_repo_root "${_userprofile_cmake}/STM32Cube/Repository")
            endif()
        else()
            # macOS and Linux
            set(_home "$ENV{HOME}")
            if(NOT _home STREQUAL "")
                file(TO_CMAKE_PATH "${_home}" _home_cmake)
                set(_repo_root "${_home_cmake}/STM32Cube/Repository")
            endif()
        endif()
    endif()

    # Validate we have a repository root
    if(_repo_root STREQUAL "" OR NOT EXISTS "${_repo_root}")
        set(${OUT_DIR} "" PARENT_SCOPE)
        set(${OUT_VER} "" PARENT_SCOPE)
        message(STATUS "STM32Cube Repository not found. Checked: ${_repo_root}")
        return()
    endif()

    # 3) Glob STM32Cube L4 folders
    file(GLOB _candidates
        LIST_DIRECTORIES true
        "${_repo_root}/STM32Cube_FW_L4_V*"
    )

    if(_candidates STREQUAL "")
        set(${OUT_DIR} "" PARENT_SCOPE)
        set(${OUT_VER} "" PARENT_SCOPE)
        message(STATUS "No STM32Cube L4 packages found under: ${_repo_root}")
        return()
    endif()

    # 4) Pick the highest semantic version using CMake's VERSION comparison
    set(_best_dir "")
    set(_best_ver "")

    foreach(_dir IN LISTS _candidates)
        get_filename_component(_name "${_dir}" NAME)
        # Expect names like STM32Cube_FW_L4_V1.17.2
        # Extract the numeric version after the V
        string(REGEX MATCH "STM32Cube_FW_L4_V([0-9]+\\.[0-9]+\\.[0-9]+)" _m "${_name}")
        if(_m)
            # Capture group 1 is the version x.y.z
            string(REGEX REPLACE "STM32Cube_FW_L4_V" "" _ver "${_m}")
            if(_best_ver STREQUAL "" OR _best_ver VERSION_LESS _ver)
                set(_best_ver "${_ver}")
                set(_best_dir "${_dir}")
            endif()
        endif()
    endforeach()

    if(_best_dir STREQUAL "")
        set(${OUT_DIR} "" PARENT_SCOPE)
        set(${OUT_VER} "" PARENT_SCOPE)
        message(STATUS "STM32Cube L4 directories found but no valid version pattern matched under: ${_repo_root}")
        return()
    endif()

    # 5) Return results
    set(${OUT_DIR} "${_best_dir}" PARENT_SCOPE)
    set(${OUT_VER} "${_best_ver}" PARENT_SCOPE)
    message(STATUS "Found newest STM32Cube L4: ${_best_dir} (version ${_best_ver})")
endfunction() # find_newest_stm32cube_fw_l4


# 1) Hints from environment or cache
set(_HINTS "")
if(DEFINED ENV{STM32CUBEIDE_DIR})
    message(STATUS "Found env STM32CUBEIDE_DIR=$ENV{STM32CUBEIDE_DIR}")
    list(APPEND _HINTS "$ENV{STM32CUBEIDE_DIR}")
endif()

if(DEFINED STM32CUBEIDE_DIR)
    message(STATUS "Found STM32CUBEIDE_DIR=${STM32CUBEIDE_DIR}")
    list(APPEND _HINTS "${STM32CUBEIDE_DIR}")
endif()

if(DEFINED ENV{STM32CUBEIDE_ROOT})
    message(STATUS "Found env STM32CUBEIDE_ROOT=$ENV{STM32CUBEIDE_ROOT}")
    list(APPEND _HINTS "$ENV{STM32CUBEIDE_ROOT}")
endif()

if(DEFINED STM32CUBEIDE_ROOT)
    message(STATUS "Found STM32CUBEIDE_ROOT=${STM32CUBEIDE_ROOT}")
    list(APPEND _HINTS "${STM32CUBEIDE_ROOT}")
endif()

foreach(h ${_HINTS})
    message(STATUS "Looking for STM32CubeIDE.exe in ${h}")
    if(CMAKE_HOST_WIN32)
        if(EXISTS "${h}/STM32CubeIDE.exe")
            _stm32cubeide_set_from_exec("${h}/STM32CubeIDE.exe")
        endif()
    elseif(CMAKE_HOST_APPLE)
        if(EXISTS "${h}/STM32CubeIDE.app/Contents/MacOS/STM32CubeIDE")
            _stm32cubeide_set_from_exec("${h}/STM32CubeIDE.app/Contents/MacOS/STM32CubeIDE")
        elseif(EXISTS "${h}/Contents/MacOS/STM32CubeIDE")
            _stm32cubeide_set_from_exec("${h}/Contents/MacOS/STM32CubeIDE")
        endif()
    else()
        if(EXISTS "${h}/stm32cubeide")
            _stm32cubeide_set_from_exec("${h}/stm32cubeide")
        endif()
    endif()
endforeach()

# 2) PATH search
if(NOT STM32CUBEIDE_EXECUTABLE)
    if(CMAKE_HOST_WIN32)
        find_program(_CUBE_EXE NAMES "STM32CubeIDE.exe")
    elseif(CMAKE_HOST_APPLE OR CMAKE_HOST_UNIX)
        find_program(_CUBE_EXE NAMES "stm32cubeide")
    endif()
    if(_CUBE_EXE)
        _stm32cubeide_set_from_exec("${_CUBE_EXE}")
    endif()
endif()

# 3) OS-specific probing
if(NOT STM32CUBEIDE_EXECUTABLE)
    if(CMAKE_HOST_WIN32)
        # Try Registry: uninstall entries often expose InstallLocation
        # 64-bit and 32-bit views
        foreach(_HK
               "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
               "HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall")
            execute_process(COMMAND reg query ${_HK} /f STM32CubeIDE /s
                            OUTPUT_VARIABLE _reg
                            ERROR_VARIABLE _reg_err
                            RESULT_VARIABLE _reg_rc)
            if(_reg_rc EQUAL 0 AND _reg MATCHES "InstallLocation\\s+REG_SZ\\s+([^\r\n]+)")
                string(REGEX REPLACE ".*InstallLocation\\s+REG_SZ\\s+([^\r\n]+).*" "\\1" _loc "${_reg}")
                string(REPLACE "\\" "/" _loc "${_loc}")
                if(EXISTS "${_loc}/STM32CubeIDE.exe")
                    _stm32cubeide_set_from_exec("${_loc}/STM32CubeIDE.exe")
                endif()
            endif()
        endforeach()

        # Common default roots
        if(NOT STM32CUBEIDE_EXECUTABLE)
            file(GLOB _candidates
                      "C:/ST/STM32CubeIDE_*"
                      "C:/Program Files/STMicroelectronics/STM32CubeIDE*"
                      "C:/Program Files (x86)/STMicroelectronics/STM32CubeIDE*")

            if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.7")
                list(SORT _candidates COMPARE NATURAL ORDER DESCENDING)
            else()
                list(SORT _candidates)
                list(REVERSE _candidates)
            endif()

            foreach(_this_c ${_candidates})
                message(STATUS "Looking at ${_this_c}")
                if(EXISTS "${_this_c}/STM32CubeIDE.exe")
                    message(STATUS "Found ${_this_c}/STM32CubeIDE.exe")
                    _stm32cubeide_set_from_exec("${_this_c}/STM32CubeIDE.exe")
                    break()
                endif()

                if(EXISTS "${_this_c}/STM32CubeIDE/STM32CubeIDE.exe")
                    message(STATUS "Found ${_this_c}/STM32CubeIDE/STM32CubeIDE.exe")
                    _stm32cubeide_set_from_exec("${_this_c}/STM32CubeIDE/STM32CubeIDE.exe")
                    break()
                endif()
            endforeach()
        endif()

    elseif(CMAKE_HOST_APPLE)
        # Standard Applications folder
        if(EXISTS "/Applications/STM32CubeIDE.app/Contents/MacOS/STM32CubeIDE")
            _stm32cubeide_set_from_exec("/Applications/STM32CubeIDE.app/Contents/MacOS/STM32CubeIDE")
        else()
            # Fall back: scan *.app names
            file(GLOB _apps "/Applications/STM32CubeIDE*.app")

            if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.7")
                list(SORT _apps COMPARE NATURAL ORDER DESCENDING)
            else()
                list(SORT _apps)
                list(REVERSE _apps)
            endif()

            foreach(app ${_apps})
                if(EXISTS "${app}/Contents/MacOS/STM32CubeIDE")
                    _stm32cubeide_set_from_exec("${app}/Contents/MacOS/STM32CubeIDE")
                    break()
                endif()
            endforeach()

            # Spotlight as last resort
            if(NOT STM32CUBEIDE_EXECUTABLE)
                execute_process(COMMAND mdfind "kMDItemCFBundleIdentifier == com.st.stm32cubeide"
                                OUTPUT_VARIABLE _mdfind RESULT_VARIABLE _mdrc)
                if(_mdrc EQUAL 0 AND _mdfind)
                    string(REGEX MATCH ".*\\.app" _app "${_mdfind}")
                    if(_app AND EXISTS "${_app}/Contents/MacOS/STM32CubeIDE")
                        _stm32cubeide_set_from_exec("${_app}/Contents/MacOS/STM32CubeIDE")
                    endif()
                endif()
            endif()
        endif()

    else() # Linux
        # Desktop file -> Exec path
        if(EXISTS "/usr/share/applications/stm32cubeide.desktop")
            file(READ "/usr/share/applications/stm32cubeide.desktop" _desk)
            string(REGEX MATCH "Exec=([^ \n\r]+)" _m "${_desk}")
            if(_m)
                string(REGEX REPLACE "Exec=([^ \n\r]+).*" "\\1" _exec "${_desk}")
                # Resolve symlink if any
                execute_process(COMMAND bash -lc "readlink -f \"${_exec}\"" OUTPUT_VARIABLE _rl RESULT_VARIABLE _rc)
                if(_rc EQUAL 0)
                    string(STRIP "${_rl}" _rls)
                    if(EXISTS "${_rls}")
                        _stm32cubeide_set_from_exec("${_rls}")
                    endif()
                elseif(EXISTS "${_exec}")
                    _stm32cubeide_set_from_exec("${_exec}")
                endif()
            endif()
        endif()

        # Typical install roots under /opt
        if(NOT STM32CUBEIDE_EXECUTABLE)
            file(GLOB _candidates "/opt/st/stm32cubeide_*")

            if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.7")
                list(SORT _candidates COMPARE NATURAL ORDER DESCENDING)
            else()
                list(SORT _candidates)
                list(REVERSE _candidates)
            endif()

            foreach(c ${_candidates})
                if(EXISTS "${c}/stm32cubeide")
                    _stm32cubeide_set_from_exec("${c}/stm32cubeide")
                    break()
                endif()
            endforeach()
        endif()
    endif() # Windows or Mac else Linux
endif() # !STM32CUBEIDE_EXECUTABLE


include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(STM32CubeIDE
                                  REQUIRED_VARS STM32CUBEIDE_EXECUTABLE STM32CUBEIDE_ROOT
                                  FAIL_MESSAGE "STM32CubeIDE not found. Set STM32CUBEIDE_DIR or add it to PATH."
)

if(STM32CUBEIDE_EXECUTABLE)
    message(STATUS "Found STM32 CubeIDE: ${STM32CUBEIDE_EXECUTABLE}")
    set(STM32CUBEIDE_FOUND TRUE)
else()
    message(STATUS "Not found: STM32 CubeIDE")
endif()

# The CubeIDE version likely does not match FW version:
# C:\Users\${CURRENT_USER}\STM32Cube\Repository\STM32Cube_FW_L4_V1.18.0\Drivers\STM32L4xx_HAL_Driver
# C:/Users/${CURRENT_USER}/STM32Cube/Repository/STM32Cube_FW_L4_V1.14.1/Drivers/

message(STATUS "CubeIDE Config WOLFBOOT_TARGET=${WOLFBOOT_TARGET}")
string(TOLOWER "${WOLFBOOT_TARGET}" _wb_target_lc)
string(FIND "${_wb_target_lc}" "stm32l4" _pos)
message(STATUS "Checking if the HAL and CMSIS libraries needed")
if(_pos EQUAL 0)
    # Only do this for the L4!
    find_newest_stm32cube_fw_l4(STM32CUBE_L4_ROOT STM32CUBE_L4_VERSION)
    set(STM32_HAL_DIR "${STM32CUBE_L4_ROOT}/Drivers/STM32L4xx_HAL_Driver")
    set(CMSIS_DIR     "${STM32CUBE_L4_ROOT}/Drivers/CMSIS")

    if(STM32CUBE_L4_VERSION)
        set(HAL_BASE "${STM32CUBE_L4_ROOT}")
        if(IS_DIRECTORY "${HAL_BASE}")
            message(STATUS "Found HAL_BASE=${HAL_BASE}")
            set(FOUND_HAL_BASE true)
                # CubeIDE
                set_and_echo_dir(HAL_DRV          "${HAL_BASE}/Drivers/STM32L4xx_HAL_Driver")
                set_and_echo_dir(HAL_CMSIS_DEV    "${HAL_BASE}/Drivers/CMSIS/Device/ST/STM32L4xx/Include")
                set_and_echo_dir(HAL_CMSIS_CORE   "${HAL_BASE}/Drivers/CMSIS/Include")
                set_and_echo_dir(HAL_TEMPLATE_INC "${HAL_BASE}/Projects/B-L475E-IOT01A/Templates/Inc")
        else()
            message(STATUS "Not found expected HAL_BASE=${HAL_BASE}")
        endif()
    endif()
else()
    message(STATUS "WOLFBOOT_TARGET=${WOLFBOOT_TARGET}, not loading HAL and CMSIS libraries.")
endif() # #STM32L4 detection

string(FIND "${_wb_target_lc}" "stm32g0" _pos)
if(_pos EQUAL 0)

endif()

mark_as_advanced(STM32CUBEIDE_EXECUTABLE STM32CUBEIDE_ROOT STM32CUBEIDE_VERSION)

set(CUBE_IDE_CONFIG_CMAKE_INCLUDED TRUE)
message(STATUS "End cube_ide_config.cmake")

endif() # DETECT_CUBEIDE
