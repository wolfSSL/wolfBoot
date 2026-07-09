#!/bin/bash
# Sign wolfboot.efi for UEFI Secure Boot and generate the PK/KEK/db enrollment
# material, so the platform firmware itself verifies wolfBoot before launching
# it (closing the code-signing gap: firmware -> wolfboot.efi -> kernel).
#
# This is the UEFI Secure Boot layer of the wolfBoot root of trust. It is
# independent of wolfBoot's own wolfCrypt verification of the payload, and of
# any NVIDIA fuse provisioning (documented separately in docs/Targets.md).
#
# Requires: sbsigntool (sbsign, sbverify) and efitools (cert-to-efi-sig-list,
# sign-efi-sig-list) and openssl. On Debian/Ubuntu:
#   sudo apt install sbsigntool efitools openssl uuid-runtime
#
# Usage:
#   ./tools/scripts/sign-efi-secureboot.sh [wolfboot.efi]
# Overrides (env):
#   KEYDIR   directory for the generated keys/certs (default tools/efi-secureboot-keys)
#   EFI_BIN  the PE image to sign (default: first arg, else wolfboot.efi)
#   CN       common-name prefix for the generated certs (default "wolfBoot")
#
# SECURITY: the generated PK/KEK/db private keys are UNENCRYPTED (openssl
# -nodes) and long-lived -- intended for lab/dev enrollment. They are created
# owner-only (umask 077 below); keep them off shared machines. For production,
# protect the signing keys (HSM or an encrypted key store) and treat this
# script only as the enrollment mechanism.
set -e

# Restrict permissions on everything this script creates: it writes private
# keys (PK/KEK/db .key), which must not be group/world-readable.
umask 077

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

KEYDIR="${KEYDIR:-$ROOT/tools/efi-secureboot-keys}"
EFI_BIN="${EFI_BIN:-${1:-$ROOT/wolfboot.efi}}"
CN="${CN:-wolfBoot}"

for tool in openssl sbsign sbverify cert-to-efi-sig-list sign-efi-sig-list; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "ERROR: required tool '$tool' not found." >&2
        echo "       install: sudo apt install sbsigntool efitools openssl uuid-runtime" >&2
        exit 1
    }
done

if [ ! -f "$EFI_BIN" ]; then
    echo "ERROR: EFI image '$EFI_BIN' not found (build wolfboot.efi first)." >&2
    exit 1
fi
# Resolve to an absolute path: we cd into KEYDIR below, after which a relative
# image path would no longer resolve.
EFI_BIN="$(cd "$(dirname "$EFI_BIN")" && pwd)/$(basename "$EFI_BIN")"

mkdir -p "$KEYDIR"
# Resolve to an absolute path: we cd into KEYDIR below but still reference
# "$KEYDIR/db.key" etc. afterward, which would break for a relative KEYDIR.
KEYDIR="$(cd "$KEYDIR" && pwd)"
cd "$KEYDIR"

# A stable GUID identifies the owner of the enrolled variables; persist it so
# repeated runs reuse the same identity.
if [ ! -f guid.txt ]; then
    if command -v uuidgen >/dev/null 2>&1; then
        uuidgen > guid.txt
    else
        # Fallback: derive a GUID from openssl if uuid-runtime is absent.
        openssl rand -hex 16 | sed 's/\(........\)\(....\)\(....\)\(....\)\(............\)/\1-\2-\3-\4-\5/' > guid.txt
    fi
fi
GUID="$(cat guid.txt)"
echo "== UEFI Secure Boot key owner GUID: $GUID =="

# Generate the Platform Key (PK), Key Exchange Key (KEK) and signature
# database (db) key/cert if they do not already exist. Self-signed X.509,
# 2048-bit RSA, long validity for lab/dev use.
gen_key() {
    name="$1"; subject="$2"
    if [ ! -f "$name.key" ] || [ ! -f "$name.crt" ]; then
        echo "== generating $name key/cert ($subject) =="
        echo "   WARNING: $name.key is an UNENCRYPTED private key (dev/lab use)" >&2
        openssl req -new -x509 -newkey rsa:2048 -nodes -sha256 -days 3650 \
            -subj "/CN=$subject/" -keyout "$name.key" -out "$name.crt"
    fi
    # ESL (EFI signature list) form of the cert, tagged with our owner GUID.
    cert-to-efi-sig-list -g "$GUID" "$name.crt" "$name.esl"
}

gen_key PK  "$CN PK"
gen_key KEK "$CN KEK"
gen_key db  "$CN db"

# Build the signed variable updates (.auth) for enrollment:
#   PK  is signed by PK  (self)
#   KEK is signed by PK
#   db  is signed by KEK
echo "== building signed enrollment variables (.auth) =="
sign-efi-sig-list -g "$GUID" -k PK.key  -c PK.crt  PK  PK.esl  PK.auth
sign-efi-sig-list -g "$GUID" -k PK.key  -c PK.crt  KEK KEK.esl KEK.auth
sign-efi-sig-list -g "$GUID" -k KEK.key -c KEK.crt db  db.esl  db.auth

# Sign the wolfBoot PE image with the db key so the firmware accepts it.
SIGNED="${EFI_BIN}.signed"
echo "== signing $EFI_BIN with db key -> $SIGNED =="
sbsign --key "$KEYDIR/db.key" --cert "$KEYDIR/db.crt" \
    --output "$SIGNED" "$EFI_BIN"
sbverify --cert "$KEYDIR/db.crt" "$SIGNED"

echo "== done =="
echo "Signed image:   $SIGNED"
echo "Enroll on the target (UEFI setup / KeyTool / QEMU AAVMF vars) in order:"
echo "  db  <- $KEYDIR/db.auth"
echo "  KEK <- $KEYDIR/KEK.auth"
echo "  PK  <- $KEYDIR/PK.auth   (enabling PK turns Secure Boot enforcing)"
echo "Then deploy $SIGNED as \\EFI\\BOOT\\BOOTAA64.EFI (or your boot path)."
