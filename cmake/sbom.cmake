# cmake/sbom.cmake - CMake entry point for wolfBoot SBOM generation.
#
# This is the CMake counterpart of the Makefile `sbom` target.  It reuses the
# same engine (tools/scripts/wolfboot-sbom.sh -> wolfSSL gen-sbom) so a CMake
# build and a Make build of the same configuration produce byte-comparable
# CycloneDX 1.6 and SPDX 2.3 SBOMs.  It covers the CMake presets / dot-config
# builds and the Pico SDK build (which is CMake underneath).
#
# Usage:
#   cmake --build <build-dir> --target sbom
#
# Overrides:
#   -DGEN_SBOM=/path/to/wolfssl/scripts/gen-sbom   (default: lib/wolfssl copy)
#   -DHOSTCC=cc                                     host cc for macro capture
#   -DSBOM_PYTHON=python3
#
# NOTE: the driver is a POSIX shell script; on Windows run this target from a
# shell environment (WSL/MSYS/Git-Bash) or use the Makefile path.

if(NOT DEFINED WOLFBOOT_ROOT)
    set(WOLFBOOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
endif()

set(SBOM_DRIVER ${WOLFBOOT_ROOT}/tools/scripts/wolfboot-sbom.sh)

# gen-sbom: prefer an explicit override, else the vendored wolfssl submodule.
if(NOT DEFINED GEN_SBOM OR GEN_SBOM STREQUAL "")
    set(GEN_SBOM ${WOLFBOOT_ROOT}/lib/wolfssl/scripts/gen-sbom)
endif()
if(NOT DEFINED HOSTCC OR HOSTCC STREQUAL "")
    set(HOSTCC cc)
endif()
if(NOT DEFINED SBOM_PYTHON OR SBOM_PYTHON STREQUAL "")
    set(SBOM_PYTHON python3)
endif()

# Read the wolfBoot version string from the header so CMake and Make agree.
set(_sbom_version "")
if(EXISTS ${WOLFBOOT_ROOT}/include/wolfboot/version.h)
    file(STRINGS ${WOLFBOOT_ROOT}/include/wolfboot/version.h _sbom_ver_line
         REGEX "LIBWOLFBOOT_VERSION_STRING")
    if(_sbom_ver_line)
        string(REGEX REPLACE ".*LIBWOLFBOOT_VERSION_STRING[ \t]+\"([^\"]*)\".*"
               "\\1" _sbom_version "${_sbom_ver_line}")
    endif()
endif()
if(_sbom_version STREQUAL "")
    message(WARNING "sbom: could not read LIBWOLFBOOT_VERSION_STRING; using 0.0")
    set(_sbom_version "0.0")
endif()

# Collect the compiled-in source set from the wolfBoot library targets.  These
# are the same sources arch.mk folds into OBJS: core wolfBoot + HAL + keystore
# + wolfcrypt.
set(_sbom_targets wolfboot wolfboothal)
if(TARGET public_key)
    list(APPEND _sbom_targets public_key)
endif()
if(DEFINED WOLFSSL_TGT AND TARGET ${WOLFSSL_TGT})
    list(APPEND _sbom_targets ${WOLFSSL_TGT})
endif()

set(_sbom_srcs "")
foreach(_t IN LISTS _sbom_targets)
    get_target_property(_t_srcs ${_t} SOURCES)
    get_target_property(_t_dir ${_t} SOURCE_DIR)
    if(_t_srcs)
        foreach(_s IN LISTS _t_srcs)
            # Skip generator expressions (e.g. $<TARGET_OBJECTS:...>) which are
            # not plain file paths; wolfBoot's targets use plain sources.
            # Also skip headers so the SBOM lists only compiled translation
            # units, matching the Make path (OBJS -> .c/.S only).
            if(NOT _s MATCHES "\\$<" AND _s MATCHES "\\.(c|cc|cpp|cxx|s|S|asm)$")
                if(IS_ABSOLUTE "${_s}")
                    list(APPEND _sbom_srcs "${_s}")
                else()
                    list(APPEND _sbom_srcs "${_t_dir}/${_s}")
                endif()
            endif()
        endforeach()
    endif()
endforeach()
list(REMOVE_DUPLICATES _sbom_srcs)

# Write the source list for the driver (one path per line).
set(_sbom_srcs_file ${CMAKE_CURRENT_BINARY_DIR}/wolfboot-sbom-srcs.txt)
string(REPLACE ";" "\n" _sbom_srcs_nl "${_sbom_srcs}")
file(GENERATE OUTPUT ${_sbom_srcs_file} CONTENT "${_sbom_srcs_nl}\n")

# Assemble the effective configuration as -D flags.  The driver runs these
# through the HOST compiler's -dM -E, exactly like the Make path does with
# CFLAGS, so the captured macro set (and therefore the SBOM) is identical.
set(_sbom_defs ${WOLFBOOT_DEFS} ${WOLFBOOT_DEFS_PUBLIC} ${USER_SETTINGS} ${SIGN_OPTIONS})
list(REMOVE_DUPLICATES _sbom_defs)
set(_sbom_cflags "")
foreach(_d IN LISTS _sbom_defs)
    if(NOT _d STREQUAL "")
        string(REGEX REPLACE "^-D" "" _d "${_d}")
        set(_sbom_cflags "${_sbom_cflags} -D${_d}")
    endif()
endforeach()

set(_sbom_cdx ${CMAKE_CURRENT_BINARY_DIR}/wolfboot-${_sbom_version}.cdx.json)
set(_sbom_spdx ${CMAKE_CURRENT_BINARY_DIR}/wolfboot-${_sbom_version}.spdx.json)

add_custom_target(sbom
    COMMAND ${CMAKE_COMMAND} -E env HOSTCC=${HOSTCC}
            ${SBOM_DRIVER}
            --srcs-file ${_sbom_srcs_file}
            --cflags ${_sbom_cflags}
            --name wolfboot
            --version ${_sbom_version}
            --license-file ${WOLFBOOT_ROOT}/LICENSE
            --gen-sbom ${GEN_SBOM}
            --python ${SBOM_PYTHON}
            --hostcc ${HOSTCC}
            --root ${WOLFBOOT_ROOT}
            --skip-missing
            --cdx-out ${_sbom_cdx}
            --spdx-out ${_sbom_spdx}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
    COMMENT "Generating wolfBoot SBOM (CycloneDX 1.6 + SPDX 2.3)"
)
