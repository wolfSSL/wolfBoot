# wolfboot/cmake/load_dot_config.cmake
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
#   include(cmake/load_dot_config.cmake)
#   load_dot_config("${CMAKE_SOURCE_DIR}/.config")                 # set normal CMake vars
#   # or cache them so GUIs (e.g. Visual Studio) can see/edit them:
#   load_dot_config("${CMAKE_SOURCE_DIR}/.config" CACHE_VARS)

# Ensure this file is only included and initialized once
if(CMAKE_VERSION VERSION_LESS 3.10)
    # Fallback path for older CMake, and anything else that wants to detect is loaded
    if(DEFINED LOAD_DOT_CONFIG_CMAKE_INCLUDED)
        return()
    endif()
else()
    include_guard(GLOBAL)
endif()

function(load_dot_config CONFIG_PATH)
    set(_USE_CACHE OFF)
    foreach(_arg IN LISTS ARGN)
        if(_arg STREQUAL "CACHE_VARS")
            set(_USE_CACHE ON)
        endif()
    endforeach()

    message(STATUS "Reading config file: ${CONFIG_PATH}")
    if(NOT EXISTS "${CONFIG_PATH}")
        message(FATAL_ERROR "load_dot_config: File not found: ${CONFIG_PATH}")
    endif()

    # Read the entire file, normalize newlines to \n, then split into a CMake list.
    file(READ "${CONFIG_PATH}" _cfg_raw)
    # Normalize CRLF and CR to LF
    string(REPLACE "\r\n" "\n" _cfg_raw "${_cfg_raw}")
    string(REPLACE "\r"   "\n" _cfg_raw "${_cfg_raw}")
    # Split into a list where each element is one line
    string(REPLACE "\n"   ";"  _cfg_lines "${_cfg_raw}")

    message(STATUS "-- Parsing lines from config file...")
    foreach(_line IN LISTS _cfg_lines)
        # Strip comments and whitespace
        string(REGEX REPLACE "\\s*#.*$" "" _line "${_line}")
        string(STRIP "${_line}" _line)
        if(_line STREQUAL "")
            message(STATUS "-- Skipping blank line")
            continue()
        endif()
        message(STATUS "-- Found line: ${_line}")

        # KEY[?]=VALUE
        # CMAKE_MATCH_1 = KEY, CMAKE_MATCH_2 = "?" or "", CMAKE_MATCH_3 = VALUE
        # Visual guide (ASCII only):
        #   Group 1: Key name  (...........1..........)
        #   Optional Space                             [ \t]*
        #   Group 2: Operand                                 ( 2  )
        #   Literal equals                                         =
        #   Optional Space                                          [ \t]
        #   Group 3: Value                                                ( 3)
        if(NOT _line MATCHES "^([A-Za-z_][A-Za-z0-9_]*)[ \t]*([?]?)=[ \t]*(.*)$")
            message(WARNING "load_dot_config: Skipping unrecognized line: ${_line}")
            continue()
        endif()
        set(_key "${CMAKE_MATCH_1}")  # Setting name
        set(_op  "${CMAKE_MATCH_2}")  # operand "?" or ""
        set(_val "${CMAKE_MATCH_3}")  # the value to
        message(STATUS "-- Parsed key: ${_key}")
        message(STATUS "-- Parsed op:  ${_op}")
        message(STATUS "-- Parsed val: ${_val}")

        # Trim value spaces
        string(STRIP "${_val}" _val)

        # Remove value surrounding double quotes if present
        if(_val MATCHES "^\"(.*)\"$")
            set(_val "${CMAKE_MATCH_1}")
        endif()

        # Expand Make-style $(VAR) to CMake env form $ENV{VAR}
        # We keep $ENV{VAR} literal in the set() call so it expands now.
        # Do multiple replacements if many occurrences exist.
        while(_val MATCHES "\\$\\(([A-Za-z_][A-Za-z0-9_]*)\\)")
            string(REGEX REPLACE "\\$\\(([A-Za-z_][A-Za-z0-9_]*)\\)" "\$ENV{\\1}" _val "${_val}")
        endwhile()

        # After replacing with $ENV{...}, expand it to its actual value now.
        # The "configure" trick expands env refs without touching other text.
        set(_expanded "${_val}")
        string(CONFIGURE "${_expanded}" _expanded @ONLY)

        # Detect prior definition
        set(_already_defined FALSE)
        if(DEFINED ${_key})
            set(_already_defined TRUE)
            message(STATUS "-- Already defined: ${_key}=${_val}")
        else()
            # Check cache
            get_property(_cache_type CACHE "${_key}" PROPERTY TYPE SET)
            if(_cache_type)
                set(_already_defined TRUE)
                message(STATUS "-- Already defined (cache) ${_key}=${_val}")
            endif()
        endif()

        # Respect ?= (only set if not already defined)
        set(_should_set TRUE)
        if(_op STREQUAL "?" AND _already_defined)
            set(_should_set FALSE)
        endif()

        if(_should_set)
            if(_USE_CACHE)
                # Use STRING so values like "0x1000" stay as text; FORCE to mirror Make's "="
                # For "?=", do not FORCE to preserve user edits from cache/GUI.
                if(_op STREQUAL "?")
                    message(STATUS "-- Cache Conditional Assignment: ${_key}=${_expanded} from ${CONFIG_PATH}")
                    set(${_key} "${_expanded}" CACHE STRING "Imported from ${CONFIG_PATH}")
                else()
                    message(STATUS "-- Cache Assignment: ${_key}=${_expanded} from ${CONFIG_PATH}")
                    set(${_key} "${_expanded}" CACHE STRING "Imported from ${CONFIG_PATH}" FORCE)
                endif()
            else()
                # Set variable in parent scope so caller can see value.
                message(STATUS "-- Assignment: ${_key}=${_val}")
                set(${_key} "${_expanded}" PARENT_SCOPE)
            endif()
        else()
            message(STATUS "-- Skipping assignment: ${_key}=${_val}")
        endif()
    endforeach()
    message(STATUS "-- Done processing ${CONFIG_PATH}")
endfunction()

set(LOAD_DOT_CONFIG_CMAKE_INCLUDED TRUE)
