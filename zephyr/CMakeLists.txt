# CMakeLists.txt
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

if(NOT CONFIG_WOLFBOOT_TEE)
  return()
endif()

set(WOLFBOOT_MODULE_DIR ${CMAKE_CURRENT_LIST_DIR}/.. CACHE PATH "wolfBoot module root")

zephyr_library()

zephyr_library_sources(
  ${CMAKE_CURRENT_LIST_DIR}/src/arm_tee_ns_interface.c
  ${CMAKE_CURRENT_LIST_DIR}/src/arm_tee_crypto_api.c
  ${CMAKE_CURRENT_LIST_DIR}/src/arm_tee_ps_api.c
  ${CMAKE_CURRENT_LIST_DIR}/src/arm_tee_attest_api.c
  ${CMAKE_CURRENT_LIST_DIR}/src/wolfboot_psa_ns_api.c
  ${CMAKE_CURRENT_LIST_DIR}/src/wolfboot_veneers_stub.c
)

zephyr_library_include_directories(
  ${CMAKE_CURRENT_LIST_DIR}/include
  ${CMAKE_CURRENT_LIST_DIR}/include/crypto_keys
  ${WOLFBOOT_MODULE_DIR}/include
)

set(WOLFBOOT_CMSE_VENEERS ${WOLFBOOT_MODULE_DIR}/src/wc_secure_calls.o)
if(EXISTS ${WOLFBOOT_CMSE_VENEERS})
  zephyr_library_link_libraries(${WOLFBOOT_CMSE_VENEERS})
else()
  message(WARNING "wolfBoot CMSE veneers not found at ${WOLFBOOT_CMSE_VENEERS}; PSA calls will fall back to stubs.")
endif()
