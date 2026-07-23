# cmake/sbom.cmake - wolfBoot wrapper around the vendored wolfGlass CMake helper.

if(NOT DEFINED WOLFBOOT_ROOT)
    set(WOLFBOOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
endif()

include(${WOLFBOOT_ROOT}/tools/sbom/build/sbom.cmake)

file(STRINGS ${WOLFBOOT_ROOT}/include/wolfboot/version.h _wolfboot_ver_line
     REGEX "LIBWOLFBOOT_VERSION_STRING")
string(REGEX REPLACE ".*LIBWOLFBOOT_VERSION_STRING[ \t]+\"([^\"]*)\".*"
       "\\1" _wolfboot_sbom_version "${_wolfboot_ver_line}")
if(_wolfboot_sbom_version STREQUAL "")
    message(FATAL_ERROR "sbom: could not read LIBWOLFBOOT_VERSION_STRING")
endif()

set(_sbom_targets wolfboot wolfboothal)
if(TARGET public_key)
    list(APPEND _sbom_targets public_key)
endif()
if(DEFINED WOLFSSL_TGT AND TARGET ${WOLFSSL_TGT})
    list(APPEND _sbom_targets ${WOLFSSL_TGT})
endif()

set(_sbom_defs ${WOLFBOOT_DEFS} ${WOLFBOOT_DEFS_PUBLIC} ${USER_SETTINGS} ${SIGN_OPTIONS})

set(_sbom_args
    NAME          wolfboot
    VERSION_FILE  ${WOLFBOOT_ROOT}/include/wolfboot/version.h
    VERSION_MACRO LIBWOLFBOOT_VERSION_STRING
    TARGETS       ${_sbom_targets}
    DEFS          ${_sbom_defs}
    LICENSE       ${WOLFBOOT_ROOT}/LICENSE
    ROOT          ${WOLFBOOT_ROOT}
    CDX_OUT       ${CMAKE_CURRENT_BINARY_DIR}/wolfboot-${_wolfboot_sbom_version}.cdx.json
    SPDX_OUT      ${CMAKE_CURRENT_BINARY_DIR}/wolfboot-${_wolfboot_sbom_version}.spdx.json
)

if(DEFINED SBOM_GEN AND NOT SBOM_GEN STREQUAL "")
    list(APPEND _sbom_args SBOM_GEN ${SBOM_GEN})
elseif(DEFINED GEN_SBOM AND NOT GEN_SBOM STREQUAL "")
    list(APPEND _sbom_args SBOM_GEN ${GEN_SBOM})
endif()
if(DEFINED HOSTCC AND NOT HOSTCC STREQUAL "")
    list(APPEND _sbom_args HOSTCC ${HOSTCC})
endif()

wolfglass_add_sbom(${_sbom_args})
