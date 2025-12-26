#!/usr/bin/env python3
"""
wolfBoot Utility Tool

A unified utility for managing wolfBoot images, boot status, and keystores.

Usage:
    boot_status.py status get <partition> --file <file> --config <config>
    boot_status.py status set <partition> <value> --file <file> --config <config>
    boot_status.py image inspect <image> [--header-size SIZE]
    boot_status.py image verify <image> --pubkey <key> [--alg ALG] [--verify-hash]
    boot_status.py image dump <image> <output> [--header-size SIZE]
    boot_status.py keystore convert <input> [--curve CURVE]

Examples:
    # Boot status management
    boot_status.py status get BOOT --file internal_flash.dd --config .config
    boot_status.py status set UPDATE SUCCESS --file internal_flash.dd --config .config

    # Image inspection and verification
    boot_status.py image inspect test_v1_signed.bin
    boot_status.py image verify test_v1_signed.bin --pubkey keystore_spki.der --alg ecdsa-p256
    boot_status.py image dump test_v1_signed.bin payload.bin

    # Keystore conversion to SPKI format
    boot_status.py keystore convert keystore.der
"""

import argparse
import sys
import struct
import hashlib
import datetime
from pathlib import Path

# ============================================================================
# BOOT STATUS MANAGEMENT (original functionality)
# ============================================================================

config_vars: dict[str, int] = {}

def set_status(status_file: str, partition: str, value: str) -> None:
    """Set boot status for a partition."""
    with open(status_file, "r+b") as f:
        if partition == "BOOT":
            addr = config_vars["WOLFBOOT_PARTITION_BOOT_ADDRESS"]
        elif partition == "UPDATE":
            addr = config_vars["WOLFBOOT_PARTITION_UPDATE_ADDRESS"]
        else:
            print(f"Error: Invalid partition: {partition}")
            sys.exit(1)
        f.seek(addr + config_vars["WOLFBOOT_PARTITION_SIZE"] - 4)
        magic: bytes = f.read(4)
        if magic != b"BOOT":
            f.seek(addr + config_vars["WOLFBOOT_PARTITION_SIZE"] - 4)
            f.write(b"BOOT")
        if value == "NEW":
            status_byte = b"\xFF"
        elif value == "UPDATING":
            status_byte = b"\x70"
        elif value == "SUCCESS":
            status_byte = b"\x00"
        else:
            print(f"Error: Invalid value: {value}")
            sys.exit(1)
        # Write status byte at correct address
        f.seek(addr + config_vars["WOLFBOOT_PARTITION_SIZE"] - 5)
        f.write(status_byte)

def get_status(status_file: str, partition: str) -> None:
    """Get boot status for a partition."""
    with open(status_file, "rb") as f:
        if partition == "BOOT":
            addr = config_vars["WOLFBOOT_PARTITION_BOOT_ADDRESS"]
        elif partition == "UPDATE":
            addr = config_vars["WOLFBOOT_PARTITION_UPDATE_ADDRESS"]
        else:
            print(f"Error: Invalid partition: {partition}")
            sys.exit(1)
        f.seek(addr + config_vars["WOLFBOOT_PARTITION_SIZE"] - 4)
        magic: bytes = f.read(4)
        if magic != b"BOOT":
            print(f"Error: Missing magic at expected address {hex(addr)}")
            sys.exit(1)
        f.seek(addr + config_vars["WOLFBOOT_PARTITION_SIZE"] - 5)
        status_byte: bytes = f.read(1)
        if status_byte == b"\xFF":
            print("NEW")
        elif status_byte == b"\x70":
            print("UPDATING")
        elif status_byte == b"\x00":
            print("SUCCESS")
        else:
            print("INVALID")

def read_config(config_path: str) -> dict[str, str]:
    """
    Reads a config file and returns a dictionary of variables.
    Supports lines of the form KEY=VALUE, KEY:=VALUE, KEY::=VALUE, KEY:::=VALUE, and KEY?=VALUE.
    Ignores comments and blank lines.
    """
    config: dict[str, str]  = {}
    assignment_ops = [":::= ", "::=", ":=", "="]

    with open(config_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            for op in assignment_ops:
                if op in line:
                    parts = line.split(op, 1)
                    if len(parts) == 2:
                        key = parts[0].rstrip("?").strip()
                        value = parts[1].strip()
                        config[key] = value
                        break  # Stop after first matching operator
    return config

# ============================================================================
# IMAGE INSPECTION (from image-peek.py)
# ============================================================================

TYPE_NAMES = {
    0x0001: "version",
    0x0002: "timestamp",
    0x0003: "hash",
    0x0004: "attr",
    0x0010: "pubkey_hint",
    0x0020: "signature",
}

def parse_header(data: bytes, header_size: int = 0x100):
    """Parse wolfBoot image header and TLVs."""
    if len(data) < 8:
        raise ValueError("Input too small to contain header")
    magic = data[0:4]
    size_le = struct.unpack("<I", data[4:8])[0]
    off = 8
    tlvs = []
    while off < header_size:
        while off < header_size and data[off] == 0xFF:
            off += 1
        if off + 4 > header_size:
            break
        t = struct.unpack("<H", data[off:off+2])[0]
        l = struct.unpack("<H", data[off+2:off+4])[0]
        off += 4
        if off + l > header_size:
            break
        v = data[off:off+l]
        off += l
        tlvs.append((t, l, v))
    return {"magic": magic, "size": size_le, "header_size": header_size, "tlvs": tlvs}

def tlv_dict(tlvs):
    """Convert TLV list to dictionary."""
    d = {}
    for (t, l, v) in tlvs:
        d.setdefault(t, []).append((l, v))
    return d

def find_tlv(data: bytes, header_size: int, ttype: int):
    """
    Scan the header TLV area and return (value_offset, value_len, tlv_start_offset)
    for the first TLV matching 'ttype'. Returns None if not found.
    """
    off = 8  # skip magic(4) + size(4)
    while off + 4 <= header_size:
        # skip padding bytes 0xFF
        while off < header_size and data[off] == 0xFF:
            off += 1
        if off + 4 > header_size:
            break
        t = int.from_bytes(data[off:off+2], "little")
        l = int.from_bytes(data[off+2:off+4], "little")
        tlv_hdr = off
        off += 4
        if off + l > header_size:
            break
        if t == ttype:
            return (off, l, tlv_hdr)  # value starts at 'off'
        off += l
    return None

def decode_timestamp(v: bytes):
    """Decode wolfBoot timestamp TLV."""
    ts = struct.unpack("<Q", v)[0]
    try:
        utc = datetime.datetime.utcfromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S UTC")
    except Exception:
        utc = "out-of-range"
    return ts, utc

def hash_name_for_len(n: int):
    """Determine hash algorithm from hash length."""
    if n == 32: return "sha256"
    if n == 48: return "sha384"
    if n == 64: return "sha512"
    return None

def try_load_public_key(pubkey_path: Path):
    """Load a public key from PEM or DER format."""
    try:
        from cryptography.hazmat.primitives import serialization
        data = pubkey_path.read_bytes()
        try:
            key = serialization.load_pem_public_key(data)
            return key
        except ValueError:
            key = serialization.load_der_public_key(data)
            return key
    except Exception as e:
        return e

def verify_signature(pubkey, alg: str, firmware_hash: bytes, signature: bytes):
    """Verify firmware signature using public key."""
    try:
        from cryptography.hazmat.primitives.asymmetric import ec, ed25519, utils
        from cryptography.hazmat.primitives import hashes
        from cryptography.exceptions import InvalidSignature
    except Exception as e:
        return False, f"cryptography not available: {e}"

    if alg == "ecdsa-p256":
        if not hasattr(pubkey, "verify"):
            return False, "Public key object is not ECDSA-capable"
        if len(signature) != 64:
            return False, f"Expected 64-byte r||s, got {len(signature)} bytes"
        r = int.from_bytes(signature[:32], "big")
        s = int.from_bytes(signature[32:], "big")
        from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature
        sig_der = encode_dss_signature(r, s)
        hash_algo = {32: hashes.SHA256(), 48: hashes.SHA384(), 64: hashes.SHA512()}.get(len(firmware_hash))
        if hash_algo is None:
            return False, f"Unsupported hash length {len(firmware_hash)}"
        try:
            pubkey.verify(sig_der, firmware_hash, ec.ECDSA(utils.Prehashed(hash_algo)))
            return True, "Signature OK (ECDSA)"
        except InvalidSignature:
            return False, "Invalid signature (ECDSA)"
        except Exception as e:
            return False, f"ECDSA verify error: {e}"

    if alg == "ed25519":
        try:
            if not hasattr(pubkey, "verify"):
                return False, "Public key object is not Ed25519-capable"
            pubkey.verify(signature, firmware_hash)
            return True, "Signature OK (Ed25519 over stored digest)"
        except Exception as e:
            return False, f"Ed25519 verify error: {e}"

    return False, f"Unknown alg '{alg}'"

def cmd_image_inspect(args):
    """Inspect a wolfBoot signed image."""
    img_path = Path(args.image)
    if not img_path.is_file():
        print(f"Error: Image file not found: {img_path}", file=sys.stderr)
        sys.exit(1)

    data = img_path.read_bytes()
    hdr = parse_header(data, header_size=args.header_size)
    magic = hdr["magic"]; size = hdr["size"]; header_size = hdr["header_size"]; tlist = hdr["tlvs"]
    d = tlv_dict(tlist)

    print(f"Magic: {magic.decode('ascii', 'replace')} (raw: {magic.hex()})")
    print(f"Payload size: {size} (0x{size:08X})")
    print(f"Header size: {header_size} (0x{header_size:X})")

    version = d.get(0x0001, [(None, None)])[0][1]
    if version is not None:
        print(f"Version: {struct.unpack('<I', version)[0]}")
    if 0x0002 in d:
        ts_val = d[0x0002][0][1]
        ts, utc = decode_timestamp(ts_val)
        print(f"Timestamp: {ts} ({utc})")
    hash_bytes = d.get(0x0003, [(None, None)])[0][1]
    if hash_bytes is not None:
        print(f"Hash ({len(hash_bytes)} bytes): {hash_bytes.hex()}")
    if 0x0010 in d:
        hint = d[0x0010][0][1].hex()
        print(f"Pubkey hint: {hint}")
    sig = d.get(0x0020, [(None, None)])[0][1]
    if sig is not None:
        print(f"Signature ({len(sig)} bytes): {sig[:8].hex()}...{sig[-8:].hex()}")

    if len(data) < header_size + size:
        print(f"[WARN] File shorter ({len(data)} bytes) than header+payload ({header_size+size}). Hash/signature verification may fail.")

def cmd_image_verify(args):
    """Verify a wolfBoot signed image."""
    img_path = Path(args.image)
    if not img_path.is_file():
        print(f"Error: Image file not found: {img_path}", file=sys.stderr)
        sys.exit(1)

    data = img_path.read_bytes()
    hdr = parse_header(data, header_size=args.header_size)
    magic = hdr["magic"]; size = hdr["size"]; header_size = hdr["header_size"]; tlist = hdr["tlvs"]
    d = tlv_dict(tlist)

    # Show basic info
    print(f"Magic: {magic.decode('ascii', 'replace')} (raw: {magic.hex()})")
    print(f"Payload size: {size} (0x{size:08X})")

    hash_bytes = d.get(0x0003, [(None, None)])[0][1]
    sig = d.get(0x0020, [(None, None)])[0][1]

    # Verify hash if requested
    if args.verify_hash:
        if hash_bytes is None:
            print("[HASH] No hash TLV found (type 0x0003)")
        else:
            sha_info = find_tlv(data, header_size, 0x0003)
            if sha_info is None:
                print("[HASH] Could not locate SHA TLV in header")
            else:
                sha_val_off, sha_len, sha_tlv_hdr = sha_info
                header_prefix_end = sha_tlv_hdr
                header_prefix = data[0:header_prefix_end]
                payload = data[header_size: header_size + size]

                hname = hash_name_for_len(sha_len)
                if not hname:
                    print(f"[HASH] Unsupported hash length {sha_len}")
                else:
                    h = hashlib.new(hname)
                    h.update(header_prefix)
                    h.update(payload)
                    calc = h.digest()
                    ok = (calc == hash_bytes)
                    print(f"[HASH] Algorithm: {hname}  ->  {'OK' if ok else 'MISMATCH'}")
                    if not ok:
                        print(f"[HASH] expected: {hash_bytes.hex()}")
                        print(f"[HASH] computed: {calc.hex()}")

    # Verify signature if pubkey provided
    if args.pubkey:
        if sig is None:
            print("[SIG] No signature TLV found (type 0x0020)")
        elif hash_bytes is None:
            print("[SIG] Cannot verify without hash TLV (type 0x0003)")
        else:
            pubkey = try_load_public_key(Path(args.pubkey))
            if isinstance(pubkey, Exception):
                print(f"[SIG] Failed to load public key: {pubkey}")
            else:
                alg = args.alg
                if not alg:
                    if len(sig) == 64 and len(hash_bytes) in (32,48,64):
                        alg = "ecdsa-p256"
                    else:
                        print(f"[SIG] Cannot infer algorithm (sig={len(sig)} bytes, hash={len(hash_bytes) if hash_bytes else 0})")
                        alg = "ecdsa-p256"
                ok, msg = verify_signature(pubkey, alg, hash_bytes, sig)
                print(f"[SIG] {msg} (alg={alg})")

def cmd_image_dump(args):
    """Dump payload from a wolfBoot signed image."""
    img_path = Path(args.image)
    if not img_path.is_file():
        print(f"Error: Image file not found: {img_path}", file=sys.stderr)
        sys.exit(1)

    data = img_path.read_bytes()
    hdr = parse_header(data, header_size=args.header_size)
    size = hdr["size"]
    header_size = hdr["header_size"]

    if len(data) < header_size + size:
        print(f"[WARN] File shorter ({len(data)} bytes) than header+payload ({header_size+size}).", file=sys.stderr)

    payload = data[header_size : header_size + size]
    out = Path(args.output)
    out.write_bytes(payload)
    print(f"Wrote {len(payload)} bytes to: {out}")

# ============================================================================
# KEYSTORE CONVERSION (from keystore-to-spki.py)
# ============================================================================

def cmd_keystore_convert(args):
    """Convert wolfBoot keystore to SPKI DER/PEM format."""
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
        import binascii
        h = hashlib.sha256(der).digest()
        print("Wrote:", out_der)
        print("Wrote:", out_pem)
        print("SPKI SHA-256 (hex):", binascii.hexlify(h).decode("ascii"))
    except Exception:
        print("Wrote:", out_der)
        print("Wrote:", out_pem)

# ============================================================================
# MAIN CLI
# ============================================================================

def main():
    """Main CLI entry point."""
    parser = argparse.ArgumentParser(
        description="wolfBoot utility tool for managing images, boot status, and keystores",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )

    subparsers = parser.add_subparsers(dest="command", required=True, help="Command to execute")

    # ========================================================================
    # STATUS SUBCOMMAND
    # ========================================================================
    status_parser = subparsers.add_parser("status", help="Manage boot partition status")
    status_parser.add_argument("--file", required=True, help="Path to the boot status file")
    status_parser.add_argument("--config", required=True, help="Path to the .config file")

    status_subparsers = status_parser.add_subparsers(dest="status_cmd", required=True)

    # status get
    status_get = status_subparsers.add_parser("get", help="Get partition status")
    status_get.add_argument("partition", choices=["BOOT", "UPDATE"], help="Partition to query")

    # status set
    status_set = status_subparsers.add_parser("set", help="Set partition status")
    status_set.add_argument("partition", choices=["BOOT", "UPDATE"], help="Partition to modify")
    status_set.add_argument("value", choices=["SUCCESS", "UPDATING", "NEW"], help="Status value")

    # ========================================================================
    # IMAGE SUBCOMMAND
    # ========================================================================
    image_parser = subparsers.add_parser("image", help="Inspect and verify wolfBoot images")
    image_subparsers = image_parser.add_subparsers(dest="image_cmd", required=True)

    # image inspect
    img_inspect = image_subparsers.add_parser("inspect", help="Inspect image header and metadata")
    img_inspect.add_argument("image", help="Signed image file")
    img_inspect.add_argument("--header-size", type=lambda x: int(x, 0), default=0x100,
                             help="Header size (default 0x100)")

    # image verify
    img_verify = image_subparsers.add_parser("verify", help="Verify image hash and signature")
    img_verify.add_argument("image", help="Signed image file")
    img_verify.add_argument("--header-size", type=lambda x: int(x, 0), default=0x100,
                            help="Header size (default 0x100)")
    img_verify.add_argument("--pubkey", metavar="KEY", help="Public key file (PEM/DER) for signature verification")
    img_verify.add_argument("--alg", choices=["ecdsa-p256", "ed25519"], help="Signature algorithm (auto-detect if omitted)")
    img_verify.add_argument("--verify-hash", action="store_true", help="Verify payload hash")

    # image dump
    img_dump = image_subparsers.add_parser("dump", help="Dump payload to file")
    img_dump.add_argument("image", help="Signed image file")
    img_dump.add_argument("output", help="Output file for payload")
    img_dump.add_argument("--header-size", type=lambda x: int(x, 0), default=0x100,
                          help="Header size (default 0x100)")

    # ========================================================================
    # KEYSTORE SUBCOMMAND
    # ========================================================================
    keystore_parser = subparsers.add_parser("keystore", help="Convert keystore formats")
    keystore_subparsers = keystore_parser.add_subparsers(dest="keystore_cmd", required=True)

    # keystore convert
    ks_convert = keystore_subparsers.add_parser("convert", help="Convert keystore to SPKI DER/PEM")
    ks_convert.add_argument("input", help="Input keystore file (keystore.der)")
    ks_convert.add_argument("--curve", choices=["p256", "p384", "p521"],
                            help="ECC curve (auto-detect if omitted)")

    # ========================================================================
    # PARSE AND DISPATCH
    # ========================================================================
    args = parser.parse_args()

    if args.command == "status":
        # Load config for status commands
        read_vars = read_config(args.config)
        required_vars = [
            "WOLFBOOT_PARTITION_SIZE",
            "WOLFBOOT_PARTITION_BOOT_ADDRESS",
            "WOLFBOOT_PARTITION_UPDATE_ADDRESS",
        ]
        for var in required_vars:
            if var not in read_vars:
                print(f"Error: Missing required config variable: {var}")
                sys.exit(1)
            try:
                config_vars[var] = int(read_vars[var], 16)
            except ValueError:
                print(f"Error: Config variable {var} value '{read_vars[var]}' is not a valid hex number")
                sys.exit(1)

        if args.status_cmd == "get":
            get_status(args.file, args.partition)
        elif args.status_cmd == "set":
            set_status(args.file, args.partition, args.value)

    elif args.command == "image":
        if args.image_cmd == "inspect":
            cmd_image_inspect(args)
        elif args.image_cmd == "verify":
            cmd_image_verify(args)
        elif args.image_cmd == "dump":
            cmd_image_dump(args)

    elif args.command == "keystore":
        if args.keystore_cmd == "convert":
            cmd_keystore_convert(args)

if __name__ == "__main__":
    main()
