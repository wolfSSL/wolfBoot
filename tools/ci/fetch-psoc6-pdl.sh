#!/bin/sh
# fetch-psoc6-pdl.sh -- fetch the Infineon libraries the PSoC 6 port builds
# against (mtb-pdl-cat1, mtb-template-cat1, core-lib, cmsis) at pinned
# releases, into <dir>/<repo>, where the psoc6 configs expect them in lib/.
#
# usage: tools/ci/fetch-psoc6-pdl.sh [dir]   (default: lib)
#        make -C tools/unit-tests PSOC6_LIBS=<dir>

set -e

OUT="${1:-lib}"

fetch() {
    if [ -e "$OUT/$1" ]; then
        echo "$OUT/$1 already exists" >&2
        exit 1
    fi
    git -c advice.detachedHead=false clone -q --depth 1 --branch "$2" \
        "https://github.com/Infineon/$1" "$OUT/$1"
}

mkdir -p "$OUT"
fetch mtb-pdl-cat1 release-v3.24.0
fetch mtb-template-cat1 release-v1.12.0
fetch core-lib release-v1.8.0
fetch cmsis release-v6.1.1
