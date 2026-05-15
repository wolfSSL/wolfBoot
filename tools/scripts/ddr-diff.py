#!/usr/bin/env python3
"""Diff two DDRC register dumps in HSS DEBUG_DDR_DDRCFG format.

Line format expected (with optional leading "NNN:" line-number prefix):
  Register, 0x0000000020082400  ,Value, 0x00000000

Usage: ddr-diff.py <hss_dump> <wolfboot_dump> [hss_regs_header]

If a third argument is given, parses it (HSS mss_ddr_sgmii_regs.h)
to label each diff address with the HSS struct field name.
"""
import re
import sys


LINE_RE = re.compile(
    r"^(?:\d+:)?\s*Register,\s*(0x[0-9A-Fa-f]+)\s*,\s*Value,\s*(0x[0-9A-Fa-f]+)"
)

DDRCFG_BASE = 0x20080000

# Top-level DDR_CSR_APB_TypeDef sub-struct bases (offsets within DDRCFG).
TOP_BASES = [
    ("MC_BASE3",       0x2800),
    ("MC_BASE1",       0x3C00),
    ("MC_BASE2",       0x4000),
    ("MEM_TEST",       0x4400),
    ("MPFE",           0x4C00),
    ("REORDER",        0x5000),
    ("RMW",            0x5400),
    ("ECC",            0x5800),
    ("READ_CAPT",      0x5C00),
    ("MTA",            0x6400),
    ("DYN_WIDTH_ADJ",  0x7C00),
    ("CA_PAR_ERR",     0x8000),
    ("DFI",           0x10000),
    ("AXI_IF",        0x12C00),
]


def find_struct(off):
    """Return (name, base) for the sub-struct containing offset off."""
    last = TOP_BASES[0]
    for name, base in TOP_BASES:
        if off < base:
            return last
        last = (name, base)
    return last


# Match struct field lines:
#   __IO  DDR_CSR_APB_<TYPE>_TypeDef <FIELD>;        /*!< Offset: 0xNN  */
# or "__I" / arrays / etc.
FIELD_RE = re.compile(
    r"^\s*__I[O]?\s+\S+\s+(\w+)\s*(?:\[\d+\])?\s*;\s*/\*!<\s*Offset:\s*(0x[0-9A-Fa-f]+)"
)
TYPEDEF_BLOCK_RE = re.compile(
    r"^/\*-+\s*(\w+)\s+register bundle definition\s*-+\*/"
)


def parse_hss_header(path):
    """Parse mss_ddr_sgmii_regs.h to build address->name map.

    Walks each "/*------ <BUNDLE> register bundle definition ------*/"
    block, collects (field, in_struct_offset) pairs until the closing
    typedef, then resolves to absolute DDRCFG offset using TOP_BASES.
    """
    addr_name = {}
    current_bundle = None
    in_struct = False
    fields = []
    with open(path) as f:
        for line in f:
            m = TYPEDEF_BLOCK_RE.match(line)
            if m:
                current_bundle = m.group(1)
                in_struct = False
                fields = []
                continue
            if current_bundle and line.lstrip().startswith("typedef struct"):
                in_struct = True
                fields = []
                continue
            if in_struct and line.startswith("}"):
                in_struct = False
                base = None
                for name, off in TOP_BASES:
                    if name == current_bundle:
                        base = off
                        break
                if base is not None:
                    for field, struct_off in fields:
                        addr = DDRCFG_BASE + base + struct_off
                        addr_name[addr] = "%s.%s" % (current_bundle, field)
                current_bundle = None
                continue
            if in_struct:
                m = FIELD_RE.match(line)
                if m:
                    field = m.group(1)
                    if field.startswith("UNUSED_SPACE"):
                        continue
                    fields.append((field, int(m.group(2), 16)))
    return addr_name


def load(path):
    regs = {}
    with open(path) as f:
        for line in f:
            m = LINE_RE.match(line)
            if not m:
                continue
            regs[int(m.group(1), 16)] = int(m.group(2), 16)
    return regs


def diff_bits_str(a, b):
    x = a ^ b
    if x == 0:
        return ""
    bits = [str(i) for i in range(32) if (x >> i) & 1]
    return "diff_bits=" + ",".join(bits)


def annotate(addr, names):
    if addr in names:
        return names[addr]
    off = addr - DDRCFG_BASE
    name, base = find_struct(off)
    return "%s+0x%X" % (name, off - base)


def main():
    argc = len(sys.argv)
    if argc < 3:
        print("usage: ddr-diff.py <hss_dump> <wolfboot_dump> [hss_regs_header]",
              file=sys.stderr)
        sys.exit(2)
    hss = load(sys.argv[1])
    wb = load(sys.argv[2])
    names = {}
    if argc >= 4:
        names = parse_hss_header(sys.argv[3])

    common = sorted(set(hss.keys()) & set(wb.keys()))
    only_hss = sorted(set(hss.keys()) - set(wb.keys()))
    only_wb = sorted(set(wb.keys()) - set(hss.keys()))
    diffs = [(a, hss[a], wb[a]) for a in common if hss[a] != wb[a]]

    print("# DDR register diff")
    print("# HSS:", sys.argv[1])
    print("# WB :", sys.argv[2])
    if names:
        print("# names: %s (%d entries)" % (sys.argv[3], len(names)))
    print("# common addrs : %d" % len(common))
    print("# only in HSS  : %d" % len(only_hss))
    print("# only in WB   : %d" % len(only_wb))
    print("# diffs        : %d" % len(diffs))
    print("# format: ADDR  HSS=val  WB=val  NAME  [diff_bits]")
    print()
    for a, h, w in diffs:
        label = annotate(a, names)
        print("0x%08X  HSS=0x%08X  WB=0x%08X  %-45s  %s"
              % (a, h, w, label, diff_bits_str(h, w)))


if __name__ == "__main__":
    main()
