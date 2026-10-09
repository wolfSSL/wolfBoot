#!/bin/sh
# fetch-mcuxsdk-headers.sh -- fetch the NXP MCUXpresso SDK sources the NXP HAL
# unit tests build against, laid out like a west checkout, at pinned commits.
#
# usage: tools/ci/fetch-mcuxsdk-headers.sh <new-dir>
#        make -C tools/unit-tests MCUXSDK=<new-dir>

set -e

OUT="${1:?usage: fetch-mcuxsdk-headers.sh <new-dir>}"
NXP=https://github.com/nxp-mcuxpresso

# fetch <dest> <repo> <commit> <paths...>: sparse, depth 1, blobs on demand
fetch() {
    dest="$OUT${1:+/$1}"; url="$NXP/$2"; rev="$3"
    shift 3
    git init -q "$dest"
    git -C "$dest" remote add origin "$url"
    git -C "$dest" sparse-checkout set --no-cone "$@"
    git -C "$dest" fetch -q --depth 1 --filter=blob:none origin "$rev"
    git -C "$dest" checkout -q FETCH_HEAD
}

if [ -e "$OUT" ]; then
    echo "$OUT already exists" >&2
    exit 1
fi

fetch "" mcuxsdk-core c6f4223f45fdab59509f25ee3ea63a71bd29d8aa \
    /drivers/common/ /drivers/trng/ /drivers/mcx_spc/ /drivers/flexcomm/ \
    /drivers/port/ /drivers/lpuart/ /drivers/tstmr/ /drivers/elemu/
fetch devices/Wireless mcux-devices-wireless \
    0d2c97770475558c72f5b6947f468bf97282df79 /RW/periph/ /RW/RW612/
fetch devices/MCX mcux-devices-mcx \
    7c0e68e9094e3943edbe1c9bf68305556adaa2c8 /MCXA/periph/ /MCXA/MCXA153/ \
    /MCXW/periph2/ /MCXW/MCXW716C/
fetch middleware/secure-subsystem mcux-secure-subsystem \
    1cce4ff3093af3c1e79cf70891e96d13e3416b58 /inc/ /src/sscp/
