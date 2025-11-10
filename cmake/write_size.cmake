# write_size.cmake
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


# Args: -DINPUT=... -DOUT=... -DTOOLCHAIN_SIZE=...
execute_process(
    COMMAND "${TOOLCHAIN_SIZE}" "${INPUT}"
    OUTPUT_VARIABLE SIZE_OUT
    RESULT_VARIABLE RC
)
if(NOT RC EQUAL 0)
    message(FATAL_ERROR "size failed with code ${RC}")
endif()

# Echo to console (so you still see it in the build log)
message("${SIZE_OUT}")

# Save to file
file(WRITE "${OUT}" "${SIZE_OUT}")
