# wolfboot/cmake/current_user.cmake
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
# get_current_user(<OUT_VAR>)
# Sets <OUT_VAR> to the best guess of the current user across Windows, Linux, macOS, and WSL.

# Example usage
#   get_current_user(CURRENT_USER)
#   message(STATUS "Current user detected: ${CURRENT_USER}")

# Ensure this file is only included and initialized once
if(CMAKE_VERSION VERSION_LESS 3.10)
    # Fallback path for older CMake, and anything else that wants to detect is loaded
    if(DEFINED CURRENT_USER_CMAKE_INCLUDED)
        return()
    endif()
else()
    include_guard(GLOBAL)
endif()

function(get_current_user OUT_VAR)
  set(_user "")

  # Fast path from environment
  foreach(var USER USERNAME LOGNAME)
    if(DEFINED ENV{${var}} AND NOT "$ENV{${var}}" STREQUAL "")
      set(_user "$ENV{${var}}")
      break()
    endif()
  endforeach()

  # Windows specific fallbacks (native Win or WSL)
  if(_user STREQUAL "")
    if(WIN32 OR DEFINED ENV{WSL_DISTRO_NAME})
      # Try PowerShell first
      execute_process(
        COMMAND powershell -NoProfile -Command "$env:USERNAME"
        OUTPUT_VARIABLE _user
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
      )
      if(_user STREQUAL "")
        # Fallback to cmd.exe
        execute_process(
          COMMAND cmd.exe /c echo %USERNAME%
          OUTPUT_VARIABLE _user
          ERROR_QUIET
        )
        string(REPLACE "\r" "" _user "${_user}")
        string(STRIP "${_user}" _user)
      endif()
    endif()
  endif()

  # POSIX fallbacks
  if(_user STREQUAL "")
    execute_process(
      COMMAND id -un
      OUTPUT_VARIABLE _user
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
  endif()
  if(_user STREQUAL "")
    execute_process(
      COMMAND whoami
      OUTPUT_VARIABLE _user
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
  endif()

  # Last resort: CI hints or placeholder
  if(_user STREQUAL "")
    foreach(var GITHUB_ACTOR BUILD_USER USERNAME USER LOGNAME)
      if(DEFINED ENV{${var}} AND NOT "$ENV{${var}}" STREQUAL "")
        set(_user "$ENV{${var}}")
        break()
      endif()
    endforeach()
  endif()
  if(_user STREQUAL "")
    set(_user "unknown")
  endif()

  set(${OUT_VAR} "${_user}" PARENT_SCOPE)
endfunction()

set(CURRENT_USER_CMAKE_INCLUDED true)
