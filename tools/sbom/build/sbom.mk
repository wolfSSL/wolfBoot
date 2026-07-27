# sbom.mk - shared plain-Make fragment for wolfGlass SBOM generation.
#
# One driver does the work; each product describes itself with a few variables
# and includes this fragment to get an `sbom` target. It is the plain-Make
# counterpart of sbom.am (autotools) and sbom.cmake (CMake). All three call the
# same driver, so the three build systems emit byte-comparable SBOMs.
#
# The including Makefile MUST set, before `include .../build/sbom.mk`:
#   SBOM_NAME        Product name recorded in the SBOM (e.g. wolfboot).
#
# Composition - set at least one:
#   SBOM_SRCS        Source files compiled into the artifact (tier E), e.g.:
#                      SBOM_SRCS := $(patsubst %.o,%.c,$(OBJS))
#   SBOM_LIB         Path to the built library to hash (tier R/L/S).
#
# Config - set one:
#   SBOM_CFLAGS      Build CFLAGS whose -D tokens describe the config.
#                    NOTE: the driver keeps ONLY -D tokens from SBOM_CFLAGS.
#                    -I / -include / other flags are dropped. Products whose
#                    config comes from -include'ing a settings header (e.g.
#                    wolfHSM's wh_settings.h) MUST capture with
#                    `$(HOSTCC) -dM -E ... -include ...` themselves and pass
#                    the dump via SBOM_OPTIONS_H, not SBOM_CFLAGS.
#   SBOM_OPTIONS_H   A pre-expanded flat #define header (verbatim, scrubbed).
#   SBOM_USER_SETTINGS  A user_settings.h.
#   SBOM_SOURCE_ONLY = 1   Source-inventory SBOM with no build-config macros.
#
# Version - set one:
#   SBOM_VERSION         Literal version string, OR
#   SBOM_VERSION_FILE +  Header to read and the macro to read from it, e.g.:
#   SBOM_VERSION_MACRO     SBOM_VERSION_FILE  = include/wolfboot/version.h
#                          SBOM_VERSION_MACRO = LIBWOLFBOOT_VERSION_STRING
#
# Optional (defaults shown):
#   SBOM_ROOT             Product root. Default: current directory.
#   SBOM_LICENSE_FILE     License file. Default: $(SBOM_ROOT)/LICENSE.
#   SBOM_GEN              Path to gen-sbom. Default: driver auto-discovery.
#   GEN_SBOM              Legacy alias for SBOM_GEN.
#   SBOM_NO_ARTIFACT_HASH = 1   As-built FIPS/kernel: do not re-hash.
#   SBOM_DEP_WOLFSSL      yes/no - record wolfSSL as a dependency.
#   SBOM_DEP_OPENSSL      yes/no - record OpenSSL as a dependency.
#   SBOM_WOLFSSL_VERSION  Explicit wolfSSL version for --dep-version. When
#                         unset and SBOM_DEP_WOLFSSL=yes, falls back to
#                         $(WOLFSSL_DIR)/wolfssl/version.h
#                         (LIBWOLFSSL_VERSION_STRING), matching sbom.am.
#   SBOM_DEP_VERSION      Extra --dep-version KEY=VER tokens (space-separated).
#   HOSTCC                Host C compiler for macro capture. Default: cc.
#   CRA_PYTHON            Python interpreter. Default: python3.
#
# The driver path is derived from this fragment's own location, so a product
# that vendors share/ into tools/sbom/ needs no path configuration.
#
# Source-list staging uses $(CURDIR)/.<target>-wolfglass-srcs.txt (not mktemp).
# GNU Make expands $${TMPDIR:-/tmp} as an empty Make variable named
# "TMPDIR:-/tmp", which produced "/wolfglass-srcs.XXXXXX" and broke every
# host. The CURDIR file is .gitignore'd; avoid parallel make -j of the *same*
# SBOM target (two recipes would share one staging file). Distinct targets
# (sbom vs sbom-hal) use distinct filenames via $(1).
#
# Shell variables inside wolfglass_sbom_rule need $$$$name (not $$name):
# $(call)/$(eval) expands the define once, then the recipe expands again.
# $$name becomes $n + ame (empty single-letter Make var) after that double
# expansion; $$$$name survives as $name for the shell.
#
# To instantiate a second target, set another variable prefix and call:
#   $(eval $(call wolfglass_sbom_rule,sbom-hal,SBOM_HAL_))
# using SBOM_HAL_NAME, SBOM_HAL_SRCS, SBOM_HAL_CFLAGS, and so on.

SBOM_MK_DIR   := $(dir $(lastword $(MAKEFILE_LIST)))
SBOM_DRIVER   ?= $(abspath $(SBOM_MK_DIR)/../sbom-driver)

SBOM_ROOT     ?= $(CURDIR)
SBOM_LICENSE_FILE ?= $(SBOM_ROOT)/LICENSE
HOSTCC        ?= cc

define wolfglass_sbom_rule
.PHONY: $(1)
$(1):
	@test -n "$($(2)NAME)" || { echo "ERROR: set $(2)NAME"; exit 1; }
	@test -n "$(strip $($(2)SRCS))$($(2)LIB)" || \
	    { echo "ERROR: set $(2)SRCS or $(2)LIB"; exit 1; }
	@set -e; \
	if [ -n "$(strip $($(2)SRCS))" ]; then \
	    trap 'rm -f "$(CURDIR)/.$(1)-wolfglass-srcs.txt"' EXIT INT TERM HUP; \
	    printf '%s\n' $($(2)SRCS) > "$(CURDIR)/.$(1)-wolfglass-srcs.txt"; \
	fi; \
	dep_ver=""; \
	if [ -n "$($(2)DEP_VERSION)" ]; then \
	    for dv in $($(2)DEP_VERSION); do \
	        dep_ver="$$$$dep_ver --dep-version $$$$dv"; \
	    done; \
	fi; \
	if [ "$($(2)DEP_WOLFSSL)" = "yes" ] || [ "$($(2)DEP_WOLFSSL)" = "1" ]; then \
	    wv="$($(2)WOLFSSL_VERSION)"; \
	    if [ -z "$$$$wv" ] && [ -n "$(WOLFSSL_DIR)" ] && \
	        [ -f "$(WOLFSSL_DIR)/wolfssl/version.h" ]; then \
	        wv=`sed -n 's/.*LIBWOLFSSL_VERSION_STRING[[:space:]]*"\([^"]*\)".*/\1/p' \
	            "$(WOLFSSL_DIR)/wolfssl/version.h" | head -1`; \
	    fi; \
	    if [ -n "$$$$wv" ]; then \
	        dep_ver="$$$$dep_ver --dep-version wolfssl=$$$$wv"; \
	    fi; \
	fi; \
	CRA_PYTHON="$(CRA_PYTHON)" HOSTCC="$(or $($(2)HOSTCC),$(HOSTCC))" \
	"$(or $($(2)DRIVER),$(SBOM_DRIVER))" \
	    --name "$($(2)NAME)" \
	    --root "$(or $($(2)ROOT),$(SBOM_ROOT))" \
	    --license-file "$(or $($(2)LICENSE_FILE),$(SBOM_LICENSE_FILE))" \
	    --skip-missing \
	    $(if $(strip $($(2)SRCS)),--srcs-file "$(CURDIR)/.$(1)-wolfglass-srcs.txt") \
	    $(if $($(2)LIB),--lib "$($(2)LIB)") \
	    $(if $(filter 1,$($(2)NO_ARTIFACT_HASH)),--no-artifact-hash) \
	    $(if $(filter 1,$($(2)SOURCE_ONLY)),--source-only) \
	    $(if $($(2)CFLAGS),--cflags="$($(2)CFLAGS)") \
	    $(if $($(2)OPTIONS_H),--options-h "$($(2)OPTIONS_H)") \
	    $(if $($(2)USER_SETTINGS),--user-settings "$($(2)USER_SETTINGS)") \
	    $(if $($(2)VERSION),--version "$($(2)VERSION)") \
	    $(if $($(2)VERSION_FILE),--version-file "$($(2)VERSION_FILE)") \
	    $(if $($(2)VERSION_MACRO),--version-macro "$($(2)VERSION_MACRO)") \
	    $(if $($(2)DEP_WOLFSSL),--dep-wolfssl "$($(2)DEP_WOLFSSL)") \
	    $(if $($(2)DEP_OPENSSL),--dep-openssl "$($(2)DEP_OPENSSL)") \
	    $$$$dep_ver \
	    $(if $(or $($(2)GEN),$(GEN_SBOM)),--gen-sbom "$(or $($(2)GEN),$(GEN_SBOM))") \
	    $(if $($(2)CDX_OUT),--cdx-out "$($(2)CDX_OUT)") \
	    $(if $($(2)SPDX_OUT),--spdx-out "$($(2)SPDX_OUT)")
endef

SBOM_TARGET ?= sbom
$(eval $(call wolfglass_sbom_rule,$(SBOM_TARGET),SBOM_))
