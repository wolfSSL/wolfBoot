#!/bin/sh
# Build and run the SUIT host unit test (tests/suit_test.c) against a host
# wolfSSL plus the lib/wolfCOSE submodule. ES256 / SHA-256 profile.
#
#   WOLFSSL_DIR=/usr/local ./tests/suit_host_test.sh
#
# Requires a host wolfSSL with ECC sign+verify and SHA-256, and the wolfCOSE
# submodule checked out: git submodule update --init lib/wolfCOSE
set -e

WOLFSSL_DIR=${WOLFSSL_DIR:-/usr/local}
CC=${CC:-cc}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-/tmp/suit_host_test}

cd "$ROOT"

# wolfCOSE is built lean for ES256 sign+verify only, matching a minimal host
# wolfSSL; the verify path itself is what wolfBoot uses on-target.
"$CC" -DWOLFBOOT_SUIT -DSUIT_INSTALL_DIRECTIVES \
    -DWOLFCOSE_NO_ENCRYPT0 -DWOLFCOSE_NO_ENCRYPT -DWOLFCOSE_NO_MAC0 \
    -DWOLFCOSE_NO_MAC -DWOLFCOSE_NO_SIGN -DWOLFCOSE_NO_RECIPIENTS \
    -DWOLFCOSE_NO_MLDSA -DWOLFCOSE_NO_KEY_ENCODE -DWOLFCOSE_NO_KEY_DECODE \
    -DWOLFCOSE_NO_EDDSA -DWOLFCOSE_NO_ED448 -DWOLFCOSE_NO_RSAPSS \
    -std=c99 -Wall -Wextra -include wolfssl/options.h \
    -I include -I lib/wolfCOSE/include -I lib/wolfCOSE/src \
    -isystem "$WOLFSSL_DIR/include" \
    tests/suit_test.c \
    src/suit/suit_parse.c src/suit/suit_verify.c src/suit/suit_process.c \
    lib/wolfCOSE/src/wolfcose.c lib/wolfCOSE/src/wolfcose_cbor.c \
    -L"$WOLFSSL_DIR/lib" -lwolfssl -o "$OUT"

LD_LIBRARY_PATH="$WOLFSSL_DIR/lib" "$OUT"
