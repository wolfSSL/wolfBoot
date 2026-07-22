#!/bin/sh
# wolfboot-sbom.sh - canonical wolfBoot SBOM driver.
#
# One engine, reused by every wolfBoot build system (plain Make/arch.mk, the
# vendor-SDK Make wrappers, CMake, the Pico SDK, and the IDE extractors) so the
# generated CycloneDX 1.6 + SPDX 2.3 SBOM is identical no matter how wolfBoot
# was built.  Each build system only has to produce two things and hand them to
# this script:
#
#   1. the list of source files actually compiled into the image (--srcs-file)
#   2. the effective build configuration, either as raw build CFLAGS (--cflags)
#      or as a pre-expanded flat #define header (--options-h)
#
# The heavy lifting (config-macro normalization, hashing, schema-shaped output)
# is done by wolfSSL's product-agnostic gen-sbom.  The macro capture runs the
# HOST compiler (never the cross-compiler), so the SBOM is byte-reproducible
# across toolchains: gcc, clang/LLVM, IAR, armcl, CCRX and XC32 all converge to
# the same document for the same configuration.
#
# Reproducibility: captured macros are scrubbed of absolute host paths before
# they reach gen-sbom (e.g. -DPICO_SDK_PATH=/home/you/pico-sdk from arch.mk).
# An unscrubbed path makes the SBOM machine-specific and leaks the local file
# system into a published artifact.  Use --no-scrub to disable (debug only).
#
# PORTABILITY NOTE
# This script is product-neutral by design (name, version, root, gen-sbom and
# license are all arguments; nothing wolfBoot-specific is hard-coded).  The same
# driver can therefore be reused by other products; only the caller passes
# different --root/--name values, and the logic is unchanged.
#
# Exit status is non-zero on any error.
set -e

usage() {
    cat >&2 <<'EOF'
Usage: wolfboot-sbom.sh --srcs-file PATH (--cflags "..." | --options-h PATH) [options]

Required:
  --srcs-file PATH   File listing compiled-in sources, one path per line
                     (blank lines and lines starting with # are ignored).

Configuration source (exactly one):
  --cflags "..."     Build CFLAGS. -D tokens are extracted and expanded through
                     $HOSTCC -dM -E to capture the effective wolfBoot/wolfCrypt
                     configuration.
  --options-h PATH   A pre-expanded flat #define header (e.g. output of
                     `$CC -dM -E`). Used verbatim; HOSTCC is not invoked.
  --source-only      Produce a source-inventory SBOM with no build-config
                     macros. Use for artifacts whose configuration is not a
                     `-D` set (e.g. the Zephyr module, which is Kconfig-driven).

Options:
  --name NAME        Package name recorded in the SBOM (default: wolfboot).
  --version VER      Package version (default: read from
                     include/wolfboot/version.h under --root).
  --license-file P   LICENSE file for SPDX id detection (default: <root>/LICENSE).
  --cdx-out PATH     CycloneDX output (default: <name>-<version>.cdx.json).
  --spdx-out PATH    SPDX output (default: <name>-<version>.spdx.json).
  --gen-sbom PATH    Path to wolfSSL scripts/gen-sbom
                     (default: <root>/lib/wolfssl/scripts/gen-sbom).
  --python BIN       Python interpreter (default: python3).
  --hostcc BIN       Host C compiler for macro capture (default: cc).
  --root PATH        wolfBoot root (default: derived from this script location).
  --skip-missing     Drop source paths that do not exist on disk (with a
                     warning) instead of failing.  Mirrors the Make path, where
                     $(wildcard) silently omits not-yet-generated files such as
                     keystore.c.
  --no-scrub         Do not redact absolute host paths from the captured macro
                     header.  Debug only; the output is then not reproducible.
  -h, --help         Show this help.
EOF
}

# Defaults.
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
SRCS_FILE=""
CFLAGS_IN=""
OPTIONS_H=""
NAME="wolfboot"
VERSION=""
LICENSE_FILE=""
CDX_OUT=""
SPDX_OUT=""
GEN_SBOM=""
PYTHON="${CRA_PYTHON:-python3}"
HOSTCC="${HOSTCC:-cc}"
SKIP_MISSING=0
SOURCE_ONLY=0
SCRUB=1

while [ $# -gt 0 ]; do
    case "$1" in
        --srcs-file)   SRCS_FILE="$2"; shift 2 ;;
        --cflags)      CFLAGS_IN="$2"; shift 2 ;;
        --options-h)   OPTIONS_H="$2"; shift 2 ;;
        --source-only) SOURCE_ONLY=1; shift ;;
        --name)        NAME="$2"; shift 2 ;;
        --version)     VERSION="$2"; shift 2 ;;
        --license-file) LICENSE_FILE="$2"; shift 2 ;;
        --cdx-out)     CDX_OUT="$2"; shift 2 ;;
        --spdx-out)    SPDX_OUT="$2"; shift 2 ;;
        --gen-sbom)    GEN_SBOM="$2"; shift 2 ;;
        --python)      PYTHON="$2"; shift 2 ;;
        --hostcc)      HOSTCC="$2"; shift 2 ;;
        --root)        ROOT=$(CDPATH= cd -- "$2" && pwd); shift 2 ;;
        --skip-missing) SKIP_MISSING=1; shift ;;
        --no-scrub)    SCRUB=0; shift ;;
        -h|--help)     usage; exit 0 ;;
        *) echo "ERROR: unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

# Resolve remaining defaults now that --root is known.
[ -n "$LICENSE_FILE" ] || LICENSE_FILE="$ROOT/LICENSE"
[ -n "$GEN_SBOM" ] || GEN_SBOM="$ROOT/lib/wolfssl/scripts/gen-sbom"

if [ -z "$VERSION" ]; then
    VERSION=$(sed -n \
        's/.*LIBWOLFBOOT_VERSION_STRING[[:space:]]*"\([^"]*\)".*/\1/p' \
        "$ROOT/include/wolfboot/version.h" 2>/dev/null || true)
fi

# Validate inputs.
if [ -z "$SRCS_FILE" ]; then
    echo "ERROR: --srcs-file is required." >&2
    usage
    exit 2
fi
if [ ! -f "$SRCS_FILE" ]; then
    echo "ERROR: --srcs-file '$SRCS_FILE' does not exist." >&2
    exit 1
fi
if [ "$SOURCE_ONLY" -eq 1 ]; then
    if [ -n "$CFLAGS_IN" ] || [ -n "$OPTIONS_H" ]; then
        echo "ERROR: --source-only cannot be combined with --cflags/--options-h." >&2
        exit 2
    fi
else
    if [ -n "$CFLAGS_IN" ] && [ -n "$OPTIONS_H" ]; then
        echo "ERROR: pass only one of --cflags or --options-h." >&2
        exit 2
    fi
    if [ -z "$CFLAGS_IN" ] && [ -z "$OPTIONS_H" ]; then
        echo "ERROR: pass one of --cflags, --options-h, or --source-only." >&2
        exit 2
    fi
fi
if [ -z "$VERSION" ]; then
    echo "ERROR: could not determine version; pass --version or check" >&2
    echo "       $ROOT/include/wolfboot/version.h" >&2
    exit 1
fi
if [ ! -f "$GEN_SBOM" ]; then
    echo "ERROR: gen-sbom not found at '$GEN_SBOM'." >&2
    echo "       Initialize the submodule: git submodule update --init lib/wolfssl" >&2
    echo "       or pass --gen-sbom /path/to/wolfssl/scripts/gen-sbom" >&2
    exit 1
fi

# Default output names follow the component name so multiple artifacts
# (wolfboot, wolfboot-hal-<target>, wolfboot-zephyr, ...) don't collide.
[ -n "$CDX_OUT" ]  || CDX_OUT="$NAME-$VERSION.cdx.json"
[ -n "$SPDX_OUT" ] || SPDX_OUT="$NAME-$VERSION.spdx.json"

# Temp files we own (cleaned up on exit).  The trap captures and re-returns the
# real exit status so cleanup never masks it (a bare "[ -n x ] && rm" as the
# last statement would make a successful run exit non-zero).
_TMP_DH=""
_TMP_SF=""
_TMP_SCRUB=""
cleanup() {
    _rc=$?
    for _f in "$_TMP_DH" "$_TMP_SF" "$_TMP_SCRUB"; do
        [ -n "$_f" ] && rm -f "$_f"
    done
    return $_rc
}
trap cleanup EXIT INT TERM HUP

# Redact absolute-path tokens from a flat #define header.  A macro whose value
# is (or contains) an absolute path -- e.g. `#define PICO_SDK_PATH /home/x/sdk`
# from arch.mk's -DPICO_SDK_PATH=$(PICO_SDK_PATH) -- would otherwise make the
# SBOM machine-specific and leak the local file system.  The redacted marker is
# constant, so the same configuration yields the same SBOM on every host.
# $1 = input header, $2 = output header.
scrub_defines() {
    awk '
    /^#define / {
        name = $2
        val = ""
        for (i = 3; i <= NF; i++) val = (val == "" ? $i : val " " $i)
        if (val == "") { print; next }
        m = split(val, toks, " ")
        nv = ""
        for (i = 1; i <= m; i++) {
            t = toks[i]
            core = t
            gsub(/"/, "", core)
            # Absolute path: starts with "/" followed by at least one more char.
            if (core ~ /^\/[^ ]+/) t = "<redacted-path>"
            nv = (nv == "" ? t : nv " " t)
        }
        print "#define " name " " nv
        next
    }
    { print }
    ' "$1" > "$2"
}

# Optionally drop sources that do not exist on disk, matching the Make path's
# $(wildcard) behavior (e.g. keystore.c before it has been generated).
if [ "$SKIP_MISSING" -eq 1 ]; then
    _TMP_SF=$(mktemp "${TMPDIR:-/tmp}/wolfboot-sbom-present.XXXXXX")
    _dropped=0
    while IFS= read -r _line || [ -n "$_line" ]; do
        case "$_line" in
            ''|\#*) continue ;;
        esac
        if [ -f "$_line" ]; then
            printf '%s\n' "$_line" >>"$_TMP_SF"
        else
            echo "WARNING: skipping missing source: $_line" >&2
            _dropped=$((_dropped + 1))
        fi
    done < "$SRCS_FILE"
    if [ ! -s "$_TMP_SF" ]; then
        echo "ERROR: no existing source files remain after --skip-missing." >&2
        exit 1
    fi
    [ "$_dropped" -gt 0 ] && echo "  (--skip-missing dropped $_dropped file(s))" >&2
    SRCS_FILE="$_TMP_SF"
fi

if [ "$SOURCE_ONLY" -eq 1 ]; then
    # No build-config macros: hand gen-sbom an empty define header.
    _TMP_DH=$(mktemp "${TMPDIR:-/tmp}/wolfboot-sbom-defines.XXXXXX")
    DEFINES_H="$_TMP_DH"
elif [ -n "$OPTIONS_H" ]; then
    if [ ! -f "$OPTIONS_H" ]; then
        echo "ERROR: --options-h '$OPTIONS_H' does not exist." >&2
        exit 1
    fi
    DEFINES_H="$OPTIONS_H"
else
    # Extract -D tokens from CFLAGS and expand through the HOST compiler so the
    # captured macro set reflects the effective configuration, independent of
    # the (possibly cross) target toolchain.
    _defs=""
    for _t in $CFLAGS_IN; do
        case "$_t" in
            -D*) _defs="$_defs $_t" ;;
        esac
    done
    _TMP_DH=$(mktemp "${TMPDIR:-/tmp}/wolfboot-sbom-defines.XXXXXX")
    # shellcheck disable=SC2086
    if ! $HOSTCC -dM -E -DWOLFSSL_USER_SETTINGS $_defs -x c /dev/null >"$_TMP_DH" 2>/dev/null; then
        echo "ERROR: '$HOSTCC -dM -E' failed; install a host C compiler or set HOSTCC." >&2
        exit 1
    fi
    DEFINES_H="$_TMP_DH"
fi

# Scrub absolute host paths out of the captured macros (unless disabled) so the
# SBOM is reproducible and does not leak the local file system.  Applies to both
# the CFLAGS-derived header and a caller-supplied --options-h.
if [ "$SCRUB" -eq 1 ] && [ "$SOURCE_ONLY" -ne 1 ]; then
    _TMP_SCRUB=$(mktemp "${TMPDIR:-/tmp}/wolfboot-sbom-scrub.XXXXXX")
    scrub_defines "$DEFINES_H" "$_TMP_SCRUB"
    DEFINES_H="$_TMP_SCRUB"
fi

echo "wolfBoot SBOM: name=$NAME version=$VERSION"
echo "  sources: $SRCS_FILE"
echo "  outputs: $CDX_OUT  $SPDX_OUT"

"$PYTHON" "$GEN_SBOM" \
    --name "$NAME" \
    --version "$VERSION" \
    --supplier "wolfSSL Inc." \
    --license-file "$LICENSE_FILE" \
    --options-h "$DEFINES_H" \
    --srcs-file "$SRCS_FILE" \
    --cdx-out "$CDX_OUT" \
    --spdx-out "$SPDX_OUT"

echo "SBOM written: $CDX_OUT  $SPDX_OUT"
