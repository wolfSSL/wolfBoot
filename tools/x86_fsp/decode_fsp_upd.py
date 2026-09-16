#!/usr/bin/env python3
"""Decode an Intel FSP UPD block into named fields.

The UPD region is a flat C struct whose only authoritative layout is the
FspmUpd.h / FspsUpd.h that ships with the FSP, where each member is annotated
with its byte offset. This parses that header and uses it to decode either the
hex block WOLFBOOT_DUMP_FSP_UPD prints over the UART, or a UPD region in a
flash image. --diff reports only the fields that differ between two captures.

Examples:
  decode_fsp_upd.py --header include/x86/fsp/FspmUpd.h --log boot.log
  decode_fsp_upd.py --header ... --bin image.bin --at 0x1F3A384
  decode_fsp_upd.py --header ... --diff ours.txt intel.txt
"""

import argparse
import re
import struct
import sys

# Offsets in these headers are relative to the start of the outer UPD struct
# (FSPM_UPD), not to the inner config struct, so a single flat table works.
# The declaration must immediately follow its comment, or a struct-typed member
# in between (FSPM_UPD has three) mis-pairs the offset with the next scalar.
DECL = re.compile(
    r"/\*\*\s*Offset\s+(0x[0-9A-Fa-f]{4})\s*-?\s*([^\n*]*?)\s*\n"   # offset + title
    r"(.*?)"                                                        # body
    r"\*\*/[ \t]*\n"
    r"[ \t]*(\w+)[ \t]+(\w+)[ \t]*(?:\[\s*(\d+)\s*\])?[ \t]*;",
    re.S)

WIDTH = {"UINT8": 1, "UINT16": 2, "UINT32": 4, "UINT64": 8,
         "INT8": 1, "INT16": 2, "INT32": 4, "INT64": 8}
FMT = {1: "B", 2: "<H", 4: "<I", 8: "<Q"}

# The scalar UPD fields, with offsets relative to the outer UPD struct, live in
# the FSP_M_CONFIG / FSP_S_CONFIG struct. Scanning the whole header would also
# pick up members of nested/wrapper typedefs (FSPM_ARCH_UPD, FSPM_UPD, ...)
# whose offsets are relative to their own struct, mis-attributing them. Scope
# the scan to the config struct body so only the authoritative fields are used.
CONFIG_STRUCT = re.compile(r"typedef\s+struct\s*\{(.*?)\}\s*FSP_[MS]_CONFIG\s*;",
                           re.S)


def parse_header(path):
    """Return [(offset, ctype, name, count, title)] sorted by offset."""
    text = open(path, "r", errors="replace").read()
    cfg = CONFIG_STRUCT.search(text)
    if cfg is not None:
        text = cfg.group(1)
    else:
        sys.stderr.write("warning: no FSP_[MS]_CONFIG struct found; scanning the "
                         "whole header (nested-struct offsets may mis-parse)\n")
    fields = []
    for m in DECL.finditer(text):
        off = int(m.group(1), 16)
        title = " ".join(m.group(2).split())
        ctype = m.group(4)
        name = m.group(5)
        count = int(m.group(6)) if m.group(6) else 1
        if ctype not in WIDTH:
            # A struct-typed member (FSP_UPD_HEADER, FSPM_ARCH_UPD, ...). Its
            # own fields are declared in another header; skip it rather than
            # attributing this offset to whatever scalar comes next.
            continue
        fields.append((off, ctype, name, count, title))
    fields.sort(key=lambda f: f[0])
    return fields


def check_layout(fields):
    """Warn if the parsed table does not tile the struct (catches mis-parses)."""
    problems = []
    for i, (off, ctype, name, count, _) in enumerate(fields[:-1]):
        end = off + WIDTH[ctype] * count
        nxt = fields[i + 1][0]
        if end > nxt:
            problems.append("  overlap: %s ends 0x%04X, next (%s) starts 0x%04X"
                            % (name, end, fields[i + 1][2], nxt))
    return problems


def hex_from_log(path):
    """Pull every UPD hex dump out of a wolfBoot console capture."""
    blocks, cur = [], []
    for raw in open(path, "r", errors="replace"):
        line = raw.strip()
        if re.fullmatch(r"[0-9A-Fa-f]{2,32}", line) and len(line) % 2 == 0:
            cur.append(line)
        else:
            if cur:
                blocks.append("".join(cur))
                cur = []
    if cur:
        blocks.append("".join(cur))
    # A stray hex-looking log line is not a dump; keep only plausible ones.
    return [bytes.fromhex(b) for b in blocks if len(b) >= 256]


def value_of(data, off, ctype, count):
    w = WIDTH[ctype]
    if off + w * count > len(data):
        return None
    if count == 1:
        return struct.unpack_from(FMT[w], data, off)[0]
    return [struct.unpack_from(FMT[w], data, off + i * w)[0] for i in range(count)]


def fmt_value(v):
    if isinstance(v, list):
        if len(v) > 16:
            return "[" + ", ".join(str(x) for x in v[:16]) + ", ...]"
        return "[" + ", ".join(str(x) for x in v) + "]"
    return str(v)


def decode(fields, data, show_all):
    out = []
    for off, ctype, name, count, title in fields:
        v = value_of(data, off, ctype, count)
        if v is None:
            continue
        if not show_all and name.startswith(("UnusedUpdSpace", "Reserved")):
            continue
        out.append("0x%04X  %-40s %s" % (off, name, fmt_value(v)))
    return out


def load_decoded(path):
    """Read a previously written decode for --diff."""
    vals = {}
    for line in open(path, "r", errors="replace"):
        m = re.match(r"0x([0-9A-Fa-f]{4})\s+(\S+)\s+(.*)", line.rstrip())
        if m:
            vals[m.group(2)] = (m.group(1), m.group(3))
    return vals


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--header", help="FspmUpd.h / FspsUpd.h to take the layout from")
    ap.add_argument("--log", help="wolfBoot console capture containing a hex dump")
    ap.add_argument("--bin", help="binary file holding a UPD region")
    ap.add_argument("--at", help="offset of the UPD inside --bin (hex or decimal)")
    ap.add_argument("--which", type=int, default=-1,
                    help="which dump from --log to decode (default: the last)")
    ap.add_argument("--all", action="store_true",
                    help="include Reserved/UnusedUpdSpace padding")
    ap.add_argument("--diff", nargs=2, metavar=("A", "B"),
                    help="diff two files previously produced by this tool")
    args = ap.parse_args()

    if args.diff:
        a, b = load_decoded(args.diff[0]), load_decoded(args.diff[1])
        common = [n for n in a if n in b]
        names = [n for n in common if a[n][1] != b[n][1]]
        only = [n for n in a if n not in b] + [n for n in b if n not in a]
        print("%-40s %-28s %s" % ("FIELD", args.diff[0], args.diff[1]))
        for n in sorted(names, key=lambda n: a[n][0]):
            print("%-40s %-28s %s" % (n, a[n][1], b[n][1]))
        print("\n%d of %d common fields differ" % (len(names), len(common)))
        if only:
            print("fields present in only one side: %s" % ", ".join(sorted(only)))
        return 0

    if not args.header:
        ap.error("--header is required unless --diff is used")
    fields = parse_header(args.header)
    if not fields:
        print("no offset-annotated fields found in %s" % args.header, file=sys.stderr)
        return 1
    problems = check_layout(fields)
    if problems:
        print("WARNING: parsed layout is not self-consistent:", file=sys.stderr)
        for p in problems[:10]:
            print(p, file=sys.stderr)

    if args.log:
        blocks = hex_from_log(args.log)
        if not blocks:
            print("no UPD hex dump found in %s" % args.log, file=sys.stderr)
            return 1
        idx = args.which % len(blocks)
        print("# %s: %d dump(s) found, decoding #%d (%d bytes)"
              % (args.log, len(blocks), idx, len(blocks[idx])))
        data = blocks[idx]
    elif args.bin:
        raw = open(args.bin, "rb").read()
        at = int(args.at, 0) if args.at else 0
        data = raw[at:at + 0x1000]
        print("# %s at 0x%X" % (args.bin, at))
    else:
        ap.error("one of --log, --bin or --diff is required")

    sig = data[:8].decode("ascii", "replace")
    print("# signature %r  size %d bytes" % (sig, len(data)))
    for line in decode(fields, data, args.all):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
