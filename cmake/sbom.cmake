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

# The product description below must stay in step with the SBOM_* block of the
# Makefile. Both routes describe the same bootloader, so a customer must not
# get a different document depending on which build system they generated from.

# wolfCrypt sources are compiled into the image rather than linked, so they are
# listed as wolfBoot's own sources. Declaring wolfssl as a dependency component
# as well is what lets a scanner match wolfSSL advisories against this firmware.
if(NOT DEFINED SBOM_DEP_WOLFSSL)
    set(SBOM_DEP_WOLFSSL yes)
endif()

# A cross build has no pkg-config for the submodule, so the version has to come
# from the header. Without it the dependency component carries no version, and
# therefore no PURL and no CPE for a scanner to match.
if(NOT DEFINED SBOM_WOLFSSL_VERSION OR SBOM_WOLFSSL_VERSION STREQUAL "")
    set(_wolfssl_ver_header ${WOLFBOOT_ROOT}/lib/wolfssl/wolfssl/version.h)
    if(EXISTS ${_wolfssl_ver_header})
        file(STRINGS ${_wolfssl_ver_header} _wolfssl_ver_line
             REGEX "LIBWOLFSSL_VERSION_STRING")
        list(GET _wolfssl_ver_line 0 _wolfssl_ver_line)
        string(REGEX REPLACE ".*LIBWOLFSSL_VERSION_STRING[ \t]+\"([^\"]*)\".*"
               "\\1" SBOM_WOLFSSL_VERSION "${_wolfssl_ver_line}")
    endif()
endif()

set(_sbom_args
    NAME          wolfboot
    VERSION_FILE  ${WOLFBOOT_ROOT}/include/wolfboot/version.h
    VERSION_MACRO LIBWOLFBOOT_VERSION_STRING
    TARGETS       ${_sbom_targets}
    DEFS          ${_sbom_defs}
    LICENSE       ${WOLFBOOT_ROOT}/LICENSE
    ROOT          ${WOLFBOOT_ROOT}
    # wolfBoot is a bootloader flashed as an image, not a library linked into one.
    COMPONENT_TYPE firmware
    # LICENSE is the verbatim GPLv3, which says nothing about how wolfBoot
    # licenses under it, so inference falls back to GPL-3.0-only and understates
    # the grant. Every GPL-headered source says "either version 3 ... or (at your
    # option) any later version".
    LICENSE_OVERRIDE GPL-3.0-or-later
    DEP_WOLFSSL   ${SBOM_DEP_WOLFSSL}
    CDX_OUT       ${CMAKE_CURRENT_BINARY_DIR}/wolfboot-${_wolfboot_sbom_version}.cdx.json
    SPDX_OUT      ${CMAKE_CURRENT_BINARY_DIR}/wolfboot-${_wolfboot_sbom_version}.spdx.json
)

if(SBOM_WOLFSSL_VERSION AND NOT SBOM_WOLFSSL_VERSION STREQUAL "")
    list(APPEND _sbom_args DEP_VERSION wolfssl=${SBOM_WOLFSSL_VERSION})
endif()

if(DEFINED SBOM_GEN AND NOT SBOM_GEN STREQUAL "")
    list(APPEND _sbom_args SBOM_GEN ${SBOM_GEN})
elseif(DEFINED GEN_SBOM AND NOT GEN_SBOM STREQUAL "")
    list(APPEND _sbom_args SBOM_GEN ${GEN_SBOM})
endif()
if(DEFINED HOSTCC AND NOT HOSTCC STREQUAL "")
    list(APPEND _sbom_args HOSTCC ${HOSTCC})
endif()

wolfglass_add_sbom(${_sbom_args})
