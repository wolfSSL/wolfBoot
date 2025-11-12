#!/usr/bin/env python3
# Convert wolfBoot raw/public-key container to standard SPKI DER/PEM, next to input.
# Usage:
#
#   ./tools/scripts/wolfboot-ecc-der-to-spki.py ./tools/keytools/keystore.der
#
# Optional:
#   --curve p256|p384|p521  (only needed if auto-detect by length is not possible)
#
# Example (from [WOLFBOOT_ROOT]):
#   ./tools/scripts/wolfboot-ecc-der-to-spki.py ./tools/keytools/keystore.der
#
import argparse
import sys
from pathlib import Path

def main():
    ap = argparse.ArgumentParser(
        description="Convert a wolfBoot public key file to SPKI DER/PEM next to the input. "
                    "Understands SPKI DER, raw X||Y (64/96/132), SEC1 0x04||X||Y (65/97/133), "
                    "and wolfBoot 16+X||Y containers (80/112/148)."
    )
    ap.add_argument("input", help="Path to input public key file")
    ap.add_argument("--curve", choices=["p256", "p384", "p521"], default=None,
                    help="Curve override if auto-detect by size is not possible")
    args = ap.parse_args()

    in_path = Path(args.input).resolve()
    if not in_path.is_file():
        print("ERROR: input path does not exist or is not a file:", in_path, file=sys.stderr)
        sys.exit(2)
    raw = in_path.read_bytes()
    ln = len(raw)

    # Try SPKI DER first
    key_obj = None
    try:
        from cryptography.hazmat.primitives import serialization
        key_obj = serialization.load_der_public_key(raw)
        # Success: already SPKI DER
    except Exception:
        key_obj = None

    if key_obj is None:
        # Not SPKI DER; normalize into SEC1 uncompressed, then import with curve
        # Cases:
        #  1) raw X||Y (64/96/132)
        #  2) SEC1 0x04||X||Y (65/97/133)
        #  3) wolfBoot 16+X||Y (80/112/148)
        data = raw
        is_sec1 = False

        # Case 2: SEC1 uncompressed (leading 0x04, lengths 65/97/133)
        if ln in (65, 97, 133) and raw[0] == 0x04:
            sec1 = raw
            is_sec1 = True
            xy_len = ln - 1
        # Case 3: wolfBoot container 16+X||Y
        elif ln in (80, 112, 148):
            # Strip the first 16 bytes, keep last 64/96/132
            data = raw[16:]
            if len(data) not in (64, 96, 132):
                print("ERROR: Unexpected container size after stripping 16 bytes:", len(data), file=sys.stderr)
                sys.exit(3)
            sec1 = b"\x04" + data
            is_sec1 = True
            xy_len = len(data)
        # Case 1: raw X||Y
        elif ln in (64, 96, 132):
            sec1 = b"\x04" + raw
            is_sec1 = True
            xy_len = ln
        else:
            print("ERROR: Unrecognized input size:", ln, file=sys.stderr)
            print("       Expected one of: SPKI DER, 64/96/132 (X||Y), 65/97/133 (SEC1), 80/112/148 (16+X||Y).", file=sys.stderr)
            sys.exit(3)

        # Pick curve by X||Y size if not specified
        curve = args.curve
        if curve is None:
            if xy_len == 64:
                curve = "p256"
            elif xy_len == 96:
                curve = "p384"
            elif xy_len == 132:
                curve = "p521"
            else:
                print("ERROR: Cannot infer curve from length:", xy_len, file=sys.stderr)
                sys.exit(4)

        from cryptography.hazmat.primitives.asymmetric import ec
        if curve == "p256":
            crv = ec.SECP256R1()
        elif curve == "p384":
            crv = ec.SECP384R1()
        else:
            crv = ec.SECP521R1()

        try:
            key_obj = ec.EllipticCurvePublicKey.from_encoded_point(crv, sec1)
        except Exception as e:
            print("ERROR: cannot wrap/parse key as SEC1/SPKI:", e, file=sys.stderr)
            sys.exit(5)

    # Write SPKI next to input
    out_der = in_path.with_name(in_path.stem + "_spki.der")
    out_pem = in_path.with_name(in_path.stem + "_spki.pem")

    from cryptography.hazmat.primitives import serialization
    der = key_obj.public_bytes(
        serialization.Encoding.DER,
        serialization.PublicFormat.SubjectPublicKeyInfo
    )
    pem = key_obj.public_bytes(
        serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo
    )
    out_der.write_bytes(der)
    out_pem.write_bytes(pem)

    # Print SPKI SHA-256 for pubkey-hint comparison
    try:
        import hashlib, binascii
        h = hashlib.sha256(der).digest()
        print("Wrote:", out_der)
        print("Wrote:", out_pem)
        print("SPKI SHA-256 (hex):", binascii.hexlify(h).decode("ascii"))
    except Exception:
        print("Wrote:", out_der)
        print("Wrote:", out_pem)

if __name__ == "__main__":
    main()
