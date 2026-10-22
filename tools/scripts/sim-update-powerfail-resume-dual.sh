#!/bin/bash
# Power-fail resume test for NVM_FLASH_JOURNAL_DUAL. The trailer takes the last
# two sectors (0x3e000 and 0x3f000), so the staging sector is 0x3d000. The
# trailer sectors take turns, so a reset at one of them may or may not hit an
# erase. Each step therefore runs its resets until the expected version boots.

UPDATE_CUTS="0 15000 18000 1a000 3d000 3e000 3f000"
FALLBACK_CUTS="1000 11000 14000 1e000 3d000 3e000 3f000"

# Run the resets in $2 until version $1 boots, then boot once more if needed
cut_until() {
    local want=$1 addr
    for addr in $2; do
        V=`./wolfboot.elf powerfail $addr $3 get_version 2>/dev/null`
        if [ "x$V" == "x$want" ]; then
            return 0
        fi
    done
    V=`./wolfboot.elf $3 get_version 2>/dev/null`
    [ "x$V" == "x$want" ]
}

# Boot until get_version reports $1 on two boots in a row, at most 6 boots
settle() {
    local last="" n
    for n in 1 2 3 4 5 6; do
        V=`./wolfboot.elf get_version 2>/dev/null`
        if [ "x$V" == "x$1" ] && [ "x$last" == "x$1" ]; then
            return 0
        fi
        last=$V
    done
    return 1
}

V=`./wolfboot.elf update_trigger get_version 2>/dev/null`
if [ "x$V" != "x1" ]; then
    echo "Failed first boot with update_trigger"
    exit 1
fi

# Update, with resets in the swap, in the staging sector and in the trailer
if ! cut_until 2 "$UPDATE_CUTS"; then
    echo "Failed update (V: $V)"
    exit 1
fi

# v2 is not confirmed, so it falls back to v1 with resets. A reset in the
# trailer clear of the fallback keeps the TESTING state of v2, and the next
# boot then swaps once more, so v1 is only expected once it is stable.
cut_until 1 "$FALLBACK_CUTS"
if ! settle 1; then
    echo "Error: failed fallback (V: $V)"
    exit 1
fi

# Update again after the fallback. Each boot confirms its image.
V=`./wolfboot.elf update_trigger get_version 2>/dev/null`
if [ "x$V" != "x1" ]; then
    echo "Failed update_trigger after fallback (V: $V)"
    exit 1
fi
cut_until 2 "$UPDATE_CUTS" success
if ! settle 2; then
    echo "Failed second update (V: $V)"
    exit 1
fi

echo Test successful.
exit 0
