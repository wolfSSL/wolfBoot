#!/bin/bash
#
# Build the 'nai' FSP set from a 68INT6 BIOS image.
#
# The public Intel TGL_IOT FSP does not train this board's memory (FspMemInit
# returns EFI_DEVICE_ERROR); the FSP in NAI's own BIOS does, though the two
# share a version and UPD defaults. This lifts FSP-T/M/S and the newest
# microcode out of a BIOS image, rebases them to the .config addresses with
# edk2's SplitFspBin.py, and writes them into the tree. Snapshot afterwards
# with 'select_fsp.sh save nai'. The UPD headers under include/x86/fsp are the
# public ones and are unchanged (identical layouts).
#
# Usage:  tools/scripts/x86_fsp/tgl/nai_extract_fsp.sh <68INT6_bios_image.bin>

set -eu

IMG=${1:?usage: $0 <nai_bios_image.bin>}
WORK_DIR=/tmp/tgl_fsp
EDK2_COMMIT_ID=df25a5457f04ec465dce97428cfee96f462676e7
FSP_TOOL_URL=https://github.com/tianocore/edk2/raw/${EDK2_COMMIT_ID}/IntelFsp2Pkg/Tools/SplitFspBin.py
SCRIPT_DIR=$(readlink -f "$(dirname "$0")")
WOLFBOOT_DIR="${SCRIPT_DIR}/../../../.."
CFG="${WOLFBOOT_DIR}/.config"

[ -f "$IMG" ] || { echo "error: image not found: $IMG" >&2; exit 1; }
[ -f "$CFG" ] || { echo "error: no .config in ${WOLFBOOT_DIR}; copy config/examples/nai_68int6.config there first" >&2; exit 1; }

cfgval() { grep -E "^$1=" "$CFG" | tail -1 | cut -d= -f2 | tr -d ' '; }
FSP_T_BASE=$(cfgval FSP_T_BASE)
FSP_M_BASE=$(cfgval FSP_M_BASE)
FSP_S_LOAD_BASE=$(cfgval FSP_S_LOAD_BASE)
for v in FSP_T_BASE FSP_M_BASE FSP_S_LOAD_BASE; do
    [ -n "${!v}" ] || { echo "error: $v not set in .config" >&2; exit 1; }
done

mkdir -p "$WORK_DIR"
if [ ! -f "$WORK_DIR/SplitFspBin.py" ]; then
    echo "fetching SplitFspBin.py (edk2 ${EDK2_COMMIT_ID:0:12})"
    curl -sL -o "$WORK_DIR/SplitFspBin.py" "$FSP_TOOL_URL"
fi

# Locate FSP-T/M/S by their FSP_INFO_HEADER (type = ComponentAttribute bits
# 15:12; the FV starts 0x94 before the header, inner FV for FSP-S).
python3 - "$IMG" "$WORK_DIR" <<'PY'
import struct, sys
img = open(sys.argv[1], 'rb').read()
work = sys.argv[2]
names = {1: 'T', 2: 'M', 3: 'S'}
found = {}
i = 0
while True:
    i = img.find(b'FSPH', i)
    if i < 0:
        break
    hlen = struct.unpack_from('<I', img, i + 4)[0]
    if hlen in (0x48, 0x4C, 0x50):
        imgrev, imgid, imgsize, imgbase, attr, cattr = struct.unpack_from('<I8sIIHH', img, i + 0x0C)
        kind = names.get((cattr >> 12) & 0xF)
        if kind and imgid.strip(b'\0') and 0 < imgsize < 0x200000:
            fv = i - 0x94
            found.setdefault(kind, (fv, imgsize, imgid, imgbase, imgrev))
    i += 4
for k in 'TMS':
    if k not in found:
        print("error: FSP-%s not found in image" % k); sys.exit(1)
    fv, size, imgid, base, rev = found[k]
    open("%s/NAI_FSP_%s.fd" % (work, k), 'wb').write(img[fv:fv + size])
    print("  FSP-%s  %-9r rev %X.%X.%X.%X  at 0x%08X  size 0x%06X  linked at 0x%08X"
          % (k, imgid, (rev >> 24) & 0xFF, (rev >> 16) & 0xFF, (rev >> 8) & 0xFF, rev & 0xFF, fv, size, base))

# Newest microcode for this CPU from the same image.
best = None
for off in range(0, len(img) - 48, 16):
    if struct.unpack_from('<I', img, off)[0] != 1:
        continue
    hv, urev, date, sig, cks, ldr, pf, dsz, tsz = struct.unpack_from('<9I', img, off)
    if ldr != 1 or sig != 0x000806C1 or tsz == 0 or tsz > 0x40000 or off + tsz > len(img):
        continue
    blob = img[off:off + tsz]
    if sum(struct.unpack_from('<%dI' % (tsz // 4), blob)) & 0xFFFFFFFF:
        continue
    if best is None or urev > best[0]:
        best = (urev, date, blob)
if best is None:
    print("error: no valid 06-8c-01 microcode found in image"); sys.exit(1)
urev, date, blob = best
open("%s/NAI_ucode0.bin" % work, 'wb').write(blob)
print("  ucode  06-8c-01 rev 0x%02X date %08X  %d bytes" % (urev, date, len(blob)))
PY

rebase() {
    local c=$1 base=$2
    (cd "$WORK_DIR" && python3 SplitFspBin.py rebase -f "NAI_FSP_${c}.fd" -b "$base" \
        -c "${c,,}" -o . -n "NAI_FSP_${c}_rebased.fd" >/dev/null)
    cp "$WORK_DIR/NAI_FSP_${c}_rebased.fd" "${WOLFBOOT_DIR}/src/x86/fsp_${c,,}.bin"
    echo "  FSP-$c rebased to $base -> src/x86/fsp_${c,,}.bin"
}
rebase T "$FSP_T_BASE"
rebase M "$FSP_M_BASE"
rebase S "$FSP_S_LOAD_BASE"
cp "$WORK_DIR/NAI_ucode0.bin" "${WOLFBOOT_DIR}/src/x86/ucode0.bin"
echo "  microcode -> src/x86/ucode0.bin"
echo
echo "Done. Snapshot it: tools/scripts/x86_fsp/select_fsp.sh save nai"
