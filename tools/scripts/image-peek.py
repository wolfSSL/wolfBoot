#!/usr/bin/env python3
#
# Usage:
#   usage: image-peek.py [-h] [--header-size HEADER_SIZE] [--dump-payload OUT] [--verify-hash] [--verify-sig PUBKEY] [--alg {ecdsa-p256,ed25519}] image
#
# Example:
#   ./tools/scripts/image-peek.py ./test_v1_signed.bin --verify-sig ./keystore_spki.der --alg ecdsa-p256

import argparse, struct, hashlib, sys, datetime
from pathlib import Path

TYPE_NAMES = {
    0x0001: "version",
    0x0002: "timestamp",
    0x0003: "hash",
    0x0004: "attr",
    0x0010: "pubkey_hint",
    0x0020: "signature",
}

def read_file(path: Path) -> bytes:
    return path.read_bytes()

def parse_header(data: bytes, header_size: int = 0x100):
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
    d = {}
    for (t, l, v) in tlvs:
        d.setdefault(t, []).append((l, v))
    return d

# add this helper near the top-level functions, e.g., after tlv_dict()
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
    ts = struct.unpack("<Q", v)[0]
    try:
        utc = datetime.datetime.utcfromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S UTC")
    except Exception:
        utc = "out-of-range"
    return ts, utc

def hash_name_for_len(n: int):
    if n == 32: return "sha256"
    if n == 48: return "sha384"
    if n == 64: return "sha512"
    return None

def compute_hash(payload: bytes, name: str):
    import hashlib
    h = hashlib.new(name)
    h.update(payload)
    return h.digest()

def try_load_public_key(pubkey_path: Path):
    try:
        from cryptography.hazmat.primitives import serialization
        data = read_file(pubkey_path)
        try:
            key = serialization.load_pem_public_key(data)
            return key
        except ValueError:
            key = serialization.load_der_public_key(data)
            return key
    except Exception as e:
        return e

def verify_signature(pubkey, alg: str, firmware_hash: bytes, signature: bytes):
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

def main():
    ap = argparse.ArgumentParser(description="wolfBoot image parser/validator")
    ap.add_argument("image", help="Signed image file")
    ap.add_argument("--header-size", type=lambda x: int(x, 0), default="0x100", help="Header size (default 0x100)")
    ap.add_argument("--dump-payload", metavar="OUT", help="Write payload to this file")
    ap.add_argument("--verify-hash", action="store_true", help="Compute and compare payload hash against the header")
    ap.add_argument("--verify-sig", metavar="PUBKEY", help="Verify signature using a PEM/DER public key")
    ap.add_argument("--alg", choices=["ecdsa-p256", "ed25519"], help="Signature algorithm (try to infer if omitted)")
    args = ap.parse_args()

    img_path = Path(args.image)
    data = read_file(img_path)

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
    payload = data[header_size : header_size + size]

    if args.dump_payload:
        out = Path(args.dump_payload)
        out.write_bytes(payload)
        print(f"Wrote payload to: {out}")

    if args.verify_hash:
        if hash_bytes is None:
            print("[HASH] No hash TLV found (type 0x0003)")
        else:
            # locate the actual SHA TLV and compute header_prefix || payload
            sha_info = find_tlv(data, header_size, 0x0003)
            if sha_info is None:
                print("[HASH] Could not locate SHA TLV in header")
            else:
                sha_val_off, sha_len, sha_tlv_hdr = sha_info
                # The header portion includes everything from start of image up to (but not including) Type+Len
                header_prefix_end = sha_tlv_hdr  # exclude Type(2)+Len(2)
                header_prefix = data[0:header_prefix_end]

                # Payload is the declared 'size' bytes after the header
                payload = data[header_size: header_size + size]

                # pick hash by length
                hname = hash_name_for_len(sha_len)
                if not hname:
                    print(f"[HASH] Unsupported hash length {sha_len}")
                else:
                    import hashlib
                    h = hashlib.new(hname)
                    h.update(header_prefix)
                    h.update(payload)
                    calc = h.digest()
                    ok = (calc == hash_bytes)
                    print(f"[HASH] Algorithm: {hname}  ->  {'OK' if ok else 'MISMATCH'}")
                    if not ok:
                        print(f"[HASH] expected: {hash_bytes.hex()}")
                        print(f"[HASH] computed: {calc.hex()}")


    if args.verify_sig:
        if sig is None:
            print("[SIG] No signature TLV found (type 0x0020)")
        elif hash_bytes is None:
            print("[SIG] Cannot verify without hash TLV (type 0x0003)")
        else:
            pubkey = try_load_public_key(Path(args.verify_sig))
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

if __name__ == '__main__':
    main()
