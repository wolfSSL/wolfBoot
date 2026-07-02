#!/usr/bin/env bash
# Package wolfboot.elf into a RealTek RTL8735B (AmebaPro2) flash image.
#
# wolfboot.elf already embeds the RealTek RAM start-table (hal/rtl8735b.c), so
# unlike the zephyr_shim flow there is no separate shim.elf and no DDR blob:
# wolfBoot is SRAM-only and loads the application from external SPI NOR itself.
# This runs RealTek's elf2bin convert (wolfBoot -> PT_FW1 firmware.bin) and
# combine (reusing the vendor PARTBL/certs/boot.bin) to produce flash_ntz.bin.
#
# Usage: amebapro2_package.sh [wolfboot.elf]
#
# Override via environment:
#   AMEBA_SDK   RealTek ameba-rtos-pro2 checkout
#   OUTDIR      work/output dir (default /tmp/wolfboot_amebapro2_pkg)
# (No toolchain needed here -- packaging runs the prebuilt elf2bin/checksum.)
set -e

WOLFBOOT_ELF="${1:-wolfboot.elf}"
AMEBA_SDK="${AMEBA_SDK:-$HOME/GitHub/ameba-rtos-pro2}"
OUTDIR="${OUTDIR:-/tmp/wolfboot_amebapro2_pkg}"

SDK="$AMEBA_SDK/project/realtek_amebapro2_v0_example"
MP="$SDK/GCC-RELEASE/mp"
B="$SDK/GCC-RELEASE/build"

if [ ! -r "$WOLFBOOT_ELF" ]; then
    echo "error: cannot read $WOLFBOOT_ELF" >&2
    exit 1
fi
if [ ! -d "$MP" ]; then
    echo "error: RealTek SDK MP dir not found: $MP" >&2
    echo "       set AMEBA_SDK to your ameba-rtos-pro2 checkout" >&2
    exit 1
fi

WOLFBOOT_ELF_ABS="$(readlink -f "$WOLFBOOT_ELF")"

# Guard the recursive delete below: OUTDIR is removed wholesale, so refuse a
# mis-set value (empty, relative, "/", or a shallow path) that could wipe an
# unintended directory. The default (/tmp/wolfboot_amebapro2_pkg) is safe.
case "$OUTDIR" in
    /*) ;;
    *) echo "error: OUTDIR must be an absolute path: '$OUTDIR'" >&2; exit 1;;
esac
if [ "$OUTDIR" = "/" ] || \
   [ "$(printf '%s' "$OUTDIR" | tr -cd / | wc -c)" -lt 2 ]; then
    echo "error: refusing to 'rm -rf' unsafe/shallow OUTDIR '$OUTDIR'" >&2
    exit 1
fi

rm -rf "$OUTDIR"
mkdir -p "$OUTDIR"
cd "$OUTDIR"

# Tooling + vendor partition/cert/boot artifacts (reused as-is).
cp "$MP/elf2bin.linux" "$MP/checksum.linux" .
cp "$MP"/*.json .
cp "$WOLFBOOT_ELF_ABS" wolfboot.elf
cp "$B/partition.bin" "$B/certable.bin" "$B/certificate.bin" \
   "$B/boot.bin" "$B/boot_fcs.bin" "$B/firmware_isp_iq.bin" .
# VOE blob the bootloader loads alongside FW1 (referenced by the firmware json).
VOE=$(find "$AMEBA_SDK" -name 'voe.bin' -path '*video*' 2>/dev/null | head -1)
[ -n "$VOE" ] && cp "$VOE" .
chmod +x elf2bin.linux checksum.linux

# Build the firmware json: wolfBoot SRAM-only image, entry at the start-table.
python3 - <<'PY'
import json
j = json.load(open('amebapro2_firmware_ntz.json'))
j['FW'] = {
    "source": "wolfboot.elf",
    "header": {"type": "IMG_FWHS_S", "entry": "__ram_start_table_start__"},
    "blocks": ["sram"],
    "sram": {
        "type": "SIMG_SRAM",
        "sections": [
            ".ram.img.signature",
            ".ram.func.table",
            ".ram.code_text",
            ".data"
        ]
    }
}
# Keep the IQ_SENSOR + VOE images the RealTek bootloader expects after loading
# FW1 (omitting them makes the bootloader fail "SET BL4VOE DATA" and reset);
# only the FW image is replaced with wolfBoot.
json.dump(j, open('wolfboot_fw.json', 'w'), indent=2)
PY

./elf2bin.linux convert wolfboot_fw.json FIRMWARE firmware.bin \
    > "$OUTDIR/convert.log" 2>&1

./elf2bin.linux combine amebapro2_partitiontable.json flash_ntz.bin \
    PT_PT=partition.bin,CER_TBL=certable.bin,KEY_CER1=certificate.bin,PT_BL_PRI=boot.bin,PT_FW1=firmware.bin,PT_ISP_IQ=firmware_isp_iq.bin,PT_FCSDATA=boot_fcs.bin \
    > "$OUTDIR/combine.log" 2>&1

echo "built: $OUTDIR/flash_ntz.bin ($(stat -c %s flash_ntz.bin) bytes)"
echo "note:  flash the signed app separately into the BOOT partition offset"
echo "       (WOLFBOOT_PARTITION_BOOT_ADDRESS) via uartfwburn."
