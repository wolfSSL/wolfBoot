# wolfboot/cmake/vs2022_config.cmake
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

# Ensure this file is only included and initialized once
if(CMAKE_VERSION VERSION_LESS 3.10)
    # Fallback path for older CMake
    if(DEFINED VS2022_CONFIG_CMAKE_INCLUDED)
        return()
    endif()
else()
    include_guard(GLOBAL)
endif()

# See cmake/config_defaults.cmake for environment config and detection preferences.
if(DETECT_VS2022)

if(USE_32BIT_LIBS)
# Raw inputs copied from your Developer Prompt
set(WIN_DEV_PATH_RAW [=[
C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\bin\HostX86\x86;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\VC\VCPackages;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\TestWindow;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\bin\Roslyn;C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.8 Tools\;C:\Program Files (x86)\HTML Help Workshop;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\FSharp\Tools;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Team Tools\DiagnosticsHub\Collector;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\Extensions\Microsoft\CodeCoverage.Console;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\\x86;C:\Program Files (x86)\Windows Kits\10\bin\\x86;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\\MSBuild\Current\Bin\amd64;C:\Windows\Microsoft.NET\Framework\v4.0.30319;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\;C:\Program Files (x86)\VMware\VMware Workstation\bin\;C:\Program Files\Microsoft\jdk-11.0.16.101-hotspot\bin;C:\WINDOWS\system32;C:\WINDOWS;C:\WINDOWS\System32\Wbem;C:\WINDOWS\System32\WindowsPowerShell\v1.0\;C:\WINDOWS\System32\OpenSSH\;C:\Program Files\dotnet\;C:\Program Files\Microsoft SQL Server\Client SDK\ODBC\170\Tools\Binn\;C:\Program Files\Microsoft SQL Server\150\Tools\Binn\;C:\Program Files\Git\cmd;C:\SysGCC\esp32-master\tools\riscv32-esp-elf\esp-15.2.0_20250920\riscv32-esp-elf\bin;C:\SysGCC\esp32-master\tools\xtensa-esp-elf\esp-15.2.0_20250920\xtensa-esp-elf\bin;C:\Program Files (x86)\VMware\VMware Workstation\bin\;C:\Program Files\Microsoft\jdk-11.0.16.101-hotspot\bin;C:\WINDOWS\system32;C:\WINDOWS;C:\WINDOWS\System32\Wbem;C:\WINDOWS\System32\WindowsPowerShell\v1.0\;C:\WINDOWS\System32\OpenSSH\;C:\Program Files\dotnet\;C:\Program Files\Git\cmd;C:\Users\%USERNAME%\AppData\Local\Microsoft\WindowsApps;C:\Users\%USERNAME%\AppData\Local\Programs\Microsoft VS Code\bin;C:\ST\STM32CubeIDE_1.14.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.1.100.202311100844\tools\bin;C:\Program Files\Git\usr\bin\;C:\Users\%USERNAME%\.dotnet\tools;C:\SysGCC\esp32-master\tools\riscv32-esp-elf\esp-13.2.0_20240530\riscv32-esp-elf\bin;C:\Users\%USERNAME%\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\VC\Linux\bin\ConnectionManagerExe
]=])

set(WIN_DEV_INCLUDE_RAW [=[
C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\include;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\ATLMFC\include;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\VS\include;C:\Program Files (x86)\Windows Kits\10\include\10.0.26100.0\ucrt;C:\Program Files (x86)\Windows Kits\10\\include\10.0.26100.0\\um;C:\Program Files (x86)\Windows Kits\10\\include\10.0.26100.0\\shared;C:\Program Files (x86)\Windows Kits\10\\include\10.0.26100.0\\winrt;C:\Program Files (x86)\Windows Kits\10\\include\10.0.26100.0\\cppwinrt;C:\Program Files (x86)\Windows Kits\NETFXSDK\4.8\include\um
]=])

set(WIN_DEV_LIB_RAW [=[
C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\ATLMFC\lib\x86;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\lib\x86;C:\Program Files (x86)\Windows Kits\NETFXSDK\4.8\lib\um\x86;C:\Program Files (x86)\Windows Kits\10\lib\10.0.26100.0\ucrt\x86;C:\Program Files (x86)\Windows Kits\10\\lib\10.0.26100.0\\um\x86
]=])
endif()


if(USE_64BIT_LIBS)
set(WIN_DEV_PATH_RAW [=[
C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\bin\HostX64\x64;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\VC\VCPackages;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\TestWindow;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\bin\Roslyn;C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.8 Tools\x64\;C:\Program Files (x86)\HTML Help Workshop;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\FSharp\Tools;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Team Tools\DiagnosticsHub\Collector;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\Extensions\Microsoft\CodeCoverage.Console;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\\x64;C:\Program Files (x86)\Windows Kits\10\bin\\x64;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\\MSBuild\Current\Bin\amd64;C:\Windows\Microsoft.NET\Framework64\v4.0.30319;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\;C:\Program Files (x86)\VMware\VMware Workstation\bin\;C:\Program Files\Microsoft\jdk-11.0.16.101-hotspot\bin;C:\WINDOWS\system32;C:\WINDOWS;C:\WINDOWS\System32\Wbem;C:\WINDOWS\System32\WindowsPowerShell\v1.0\;C:\WINDOWS\System32\OpenSSH\;C:\Program Files\dotnet\;C:\Program Files\Microsoft SQL Server\Client SDK\ODBC\170\Tools\Binn\;C:\Program Files\Microsoft SQL Server\150\Tools\Binn\;C:\Program Files\Git\cmd;C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\;C:\SysGCC\esp32-master\tools\riscv32-esp-elf\esp-15.2.0_20250920\riscv32-esp-elf\bin;C:\SysGCC\esp32-master\tools\xtensa-esp-elf\esp-15.2.0_20250920\xtensa-esp-elf\bin;C:\Program Files (x86)\VMware\VMware Workstation\bin\;C:\Program Files\Microsoft\jdk-11.0.16.101-hotspot\bin;C:\WINDOWS\system32;C:\WINDOWS;C:\WINDOWS\System32\Wbem;C:\WINDOWS\System32\WindowsPowerShell\v1.0\;C:\WINDOWS\System32\OpenSSH\;C:\Program Files\dotnet\;C:\Program Files\Git\cmd;C:\Users\%USERNAME%\AppData\Local\Microsoft\WindowsApps;C:\Users\%USERNAME%\AppData\Local\Programs\Microsoft VS Code\bin;C:\ST\STM32CubeIDE_1.14.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.1.100.202311100844\tools\bin;C:\Program Files\Git\usr\bin\;C:\Users\%USERNAME%\.dotnet\tools;C:\SysGCC\esp32-master\tools\riscv32-esp-elf\esp-13.2.0_20240530\riscv32-esp-elf\bin;C:\Users\%USERNAME%\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\VC\Linux\bin\ConnectionManagerExe;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\vcpkg
]=])

set(WIN_DEV_INCLUDE_RAW [=[
C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\include;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\ATLMFC\include;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\VS\include;C:\Program Files (x86)\Windows Kits\10\include\10.0.26100.0\ucrt;C:\Program Files (x86)\Windows Kits\10\\include\10.0.26100.0\\um;C:\Program Files (x86)\Windows Kits\10\\include\10.0.26100.0\\shared;C:\Program Files (x86)\Windows Kits\10\\include\10.0.26100.0\\winrt;C:\Program Files (x86)\Windows Kits\10\\include\10.0.26100.0\\cppwinrt;C:\Program Files (x86)\Windows Kits\NETFXSDK\4.8\include\um
]=])

set(WIN_DEV_LIB_RAW [=[
C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\ATLMFC\lib\x64;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\lib\x64;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\lib\x86\store\references;C:\Program Files (x86)\Windows Kits\10\UnionMetadata\10.0.26100.0;C:\Program Files (x86)\Windows Kits\10\References\10.0.26100.0;C:\Windows\Microsoft.NET\Framework64\v4.0.30319
]=])
endif()


# Normalize a raw path token: strip quotes/whitespace, convert to CMake-style slashes
function(_normalize_path _out _in)
    set(_p "${_in}")
    string(REPLACE "\"" "" _p "${_p}")
    string(STRIP "${_p}" _p)

    # Convert backslashes to forward slashes for CMake
    file(TO_CMAKE_PATH "${_p}" _p)

    # Drop a single trailing slash to stabilize dedupe
    if(_p MATCHES ".+/$")
        string(REGEX REPLACE "/$" "" _p "${_p}")
    endif()
    set(${_out} "${_p}" PARENT_SCOPE)
endfunction()

# Build a clean env-style variable from a raw ;-separated list
# Usage:
#   build_env_from_dirs(PATH     <raw list...>)
#   build_env_from_dirs(INCLUDE  <raw list...>)
#   build_env_from_dirs(LIB      <raw list...>)
#
# Produces:
#   <NAME>_LIST   -> CMake list of existing, deduplicated, normalized dirs
#   <NAME>_STRING -> Same, joined with ';' suitable for an env var
function(build_env_from_dirs NAME)
    set(_seen  )
    set(_final )
    set(_ok_to_add false)
    message(STATUS "[${NAME}] build_env_from_dirs; USE_32BIT_LIBS=${USE_32BIT_LIBS}, USE_64BIT_LIBS=${USE_64BIT_LIBS}")
    foreach(_raw IN LISTS ARGN)
        if(_raw STREQUAL "")
            continue()
        endif()

        _normalize_path(_p "${_raw}")
        if(_p STREQUAL "")
            continue()
        endif()

        if(IS_DIRECTORY "${_p}")
            list(FIND _seen "${_p}" _idx)
            if(_idx EQUAL -1)
                # Not seen, check for x86 exclusions
                string(FIND "${_p}" "/x86" _pos)
                if(_pos GREATER -1)
                    # Known 32 bit names
                    if(USE_32BIT_LIBS)
                        set(_ok_to_add true)
                    else()
                        message(STATUS "-- [${NAME}] skipping 32 bit lib search path: ${_p}")
                        set(_ok_to_add false)
                    endif()
                else()
                    # If not a known 32 bit name, it must be 64 bit
                    if(USE_64BIT_LIBS)
                        set(_ok_to_add true)
                    else()
                        message(STATUS "-- [${NAME}] skipping non-32 bit lib search path: ${_p}")
                        set(_ok_to_add false)
                    endif()
                endif()
                if(_ok_to_add)
                    message(STATUS "-- [${NAME}] appending search path: ${_p}")

                    list(APPEND _final "${_p}")
                    list(APPEND _seen  "${_p}")
                endif()
            endif()
        else()
            # Uncomment for troubleshooting
            # message(STATUS "[${NAME}] Skipping missing: ${_p}")
        endif()
    endforeach()

    list(JOIN _final ";" _joined)
    set(${NAME}_LIST   "${_final}"  PARENT_SCOPE)
    set(${NAME}_STRING "${_joined}" PARENT_SCOPE)
endfunction() # build_env_from_dirs

# Only helpful if Visual Studio is installed
# Note VS2022 is installed by default in GitHub workflow `runs-on: windows-latest`
if(IS_DIRECTORY  "C:/Program Files/Microsoft Visual Studio/")
    message(STATUS "Found C:/Program Files/Microsoft Visual Studio/")
    # Build the CMake equivalents
    build_env_from_dirs(PATH    ${WIN_DEV_PATH_RAW})
    build_env_from_dirs(INCLUDE ${WIN_DEV_INCLUDE_RAW})
    build_env_from_dirs(LIB     ${WIN_DEV_LIB_RAW})
else()
    message(STATUS "Visual Studio not found, skipping VS2022_config.cmake")
endif()

# Results:
#   PATH_LIST / PATH_STRING
#   INCLUDE_LIST / INCLUDE_STRING
#   LIB_LIST / LIB_STRING

# Optional: export to the environment for tools launched by CMake
# set(ENV{Path}    "${PATH_STRING}")
# set(ENV{INCLUDE} "${INCLUDE_STRING}")
# set(ENV{LIB}     "${LIB_STRING}")

# Optional: integrate with CMake search variables
# list(PREPEND CMAKE_PREFIX_PATH ${PATH_LIST})
# list(PREPEND CMAKE_PROGRAM_PATH ${PATH_LIST})
# list(PREPEND CMAKE_INCLUDE_PATH ${INCLUDE_LIST})
# list(PREPEND CMAKE_LIBRARY_PATH ${LIB_LIST})

endif() # DETECT_VS2022

set(VS2022_CONFIG_CMAKE_INCLUDED true)
