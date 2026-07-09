#!/bin/sh
# GNU objcopy command-line adapter over ARM fromelf, used when building with
# the ARM Compiler for Embedded (USE_ARMCLANG=1) so that the .bin/.hex/.srec
# Makefile recipes can be shared with the GNU toolchains.
#
# Only the invocation shapes used by the wolfBoot Makefiles are supported:
#   objcopy [--gap-fill <byte>] -O binary <in.elf> <out.bin>
#   objcopy -O ihex <in.elf> <out.hex>
#   objcopy -O srec <in.elf> <out.srec>
#
# The fromelf executable is taken from the FROMELF environment variable
# (default: fromelf).

set -e

FROMELF="${FROMELF:-fromelf}"
GAP_FILL=
FORMAT=
INPUT=
OUTPUT=

while [ $# -gt 0 ]; do
    case "$1" in
        --gap-fill)
            GAP_FILL="$2"
            shift 2
            ;;
        --gap-fill=*)
            GAP_FILL="${1#*=}"
            shift
            ;;
        -O)
            FORMAT="$2"
            shift 2
            ;;
        -*)
            echo "$0: unsupported objcopy option: $1" >&2
            exit 1
            ;;
        *)
            if [ -z "$INPUT" ]; then
                INPUT="$1"
            elif [ -z "$OUTPUT" ]; then
                OUTPUT="$1"
            else
                echo "$0: too many arguments: $1" >&2
                exit 1
            fi
            shift
            ;;
    esac
done

if [ -z "$FORMAT" ] || [ -z "$INPUT" ] || [ -z "$OUTPUT" ]; then
    echo "usage: $0 [--gap-fill <byte>] -O binary|ihex|srec <in.elf> <out>" >&2
    exit 1
fi

case "$FORMAT" in
    binary)
        # --bincombined merges all load regions into a single image, padding
        # the gaps between them (GNU objcopy -O binary behavior)
        set -- --bincombined
        if [ -n "$GAP_FILL" ]; then
            set -- "$@" "--bincombined_padding=1,$GAP_FILL"
        fi
        ;;
    ihex)
        set -- --i32combined
        ;;
    srec)
        set -- --m32combined
        ;;
    *)
        echo "$0: unsupported output format: $FORMAT" >&2
        exit 1
        ;;
esac

exec "$FROMELF" "$@" --output="$OUTPUT" "$INPUT"
