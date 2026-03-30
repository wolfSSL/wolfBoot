#!/bin/bash
#
# Sunnyday update test with cryptocb dispatch verification.
# stdout is redirected to sim_cryptocb.log which contains both
# sim_cryptocb output ("sim-cryptocb: ...") and the test-app
# version number. Version is extracted by filtering out crypto lines.
#
# Usage: sim-cryptocb-sunnyday-update.sh <expected_hash> [expected_pk] [expected_cipher]
# Example: sim-cryptocb-sunnyday-update.sh "SHA-256" "RSA"
# Example: sim-cryptocb-sunnyday-update.sh "SHA-256" "ED25519-verify" "AES-CTR"
#

LOGFILE="sim_cryptocb.log"

if [ $# -lt 1 ]; then
    echo "usage: $0 <expected_hash> [expected_pk] [expected_cipher]"
    exit 1
fi

EXPECTED_HASH=$1
EXPECTED_PK=${2:-}
EXPECTED_CIPHER=${3:-}

# First boot: update_trigger + get_version (stdout -> log)
./wolfboot.elf update_trigger get_version > $LOGFILE 2>/dev/null
V=$(grep -v "^sim-cryptocb:" $LOGFILE | tail -1)
if [ "x$V" != "x1" ]; then
    echo "Failed first boot with update_trigger (V: $V)"
    cat $LOGFILE
    exit 1
fi

# Second boot: success + get_version (stdout -> log)
./wolfboot.elf success get_version > $LOGFILE 2>/dev/null
V=$(grep -v "^sim-cryptocb:" $LOGFILE | tail -1)
if [ "x$V" != "x2" ]; then
    echo "Failed update (V: $V)"
    cat $LOGFILE
    exit 1
fi

# Verify crypto callback log entries
if ! grep -q "sim-cryptocb: hash $EXPECTED_HASH" $LOGFILE; then
    echo "Error: expected 'sim-cryptocb: hash $EXPECTED_HASH' not found"
    cat $LOGFILE
    exit 1
fi
echo "Verified: hash $EXPECTED_HASH dispatched through cryptocb"

# Optional PK verification (skip for ECC which bypasses cryptocb PK dispatch)
if [ -n "$EXPECTED_PK" ]; then
    if ! grep -q "sim-cryptocb: pk $EXPECTED_PK" $LOGFILE; then
        echo "Error: expected 'sim-cryptocb: pk $EXPECTED_PK' not found"
        cat $LOGFILE
        exit 1
    fi
    echo "Verified: pk $EXPECTED_PK dispatched through cryptocb"
fi

# Optional Cipher verification (for encrypted partition tests)
if [ -n "$EXPECTED_CIPHER" ]; then
    if ! grep -q "sim-cryptocb: cipher $EXPECTED_CIPHER" $LOGFILE; then
        echo "Error: expected 'sim-cryptocb: cipher $EXPECTED_CIPHER' not found"
        cat $LOGFILE
        exit 1
    fi
    echo "Verified: cipher $EXPECTED_CIPHER dispatched through cryptocb"
fi

echo Test successful.
exit 0
