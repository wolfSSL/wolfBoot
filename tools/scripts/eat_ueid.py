#!/usr/bin/env python3
# Print the UEID claim (EAT key 256, 33-byte bstr) from a test-app console log
# that dumps the attestation token as hex after "attest: token size".
import re
import sys

UEID_CLAIM = bytes.fromhex("1901005821")
UEID_LEN = 33


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: eat_ueid.py <console.log>")
    data = bytearray()
    seen = False
    with open(sys.argv[1], errors="replace") as log:
        for line in log:
            if "attest: token size" in line:
                seen = True
                continue
            if seen and line.startswith("\t"):
                data += bytes.fromhex(" ".join(
                    re.findall(r"\b[0-9a-fA-F]{2}\b", line.split("|")[0])))
    i = data.find(UEID_CLAIM)
    if i < 0:
        sys.exit("UEID claim not found in " + sys.argv[1])
    start = i + len(UEID_CLAIM)
    ueid = data[start:start + UEID_LEN]
    if len(ueid) != UEID_LEN:
        sys.exit("UEID claim truncated in " + sys.argv[1])
    print(ueid.hex())


if __name__ == "__main__":
    main()
