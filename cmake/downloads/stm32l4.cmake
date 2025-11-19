# wolfboot/cmake/downloads/stm32l4.cmake
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

# The STM32L4 is known to need additional HAL source files:
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
