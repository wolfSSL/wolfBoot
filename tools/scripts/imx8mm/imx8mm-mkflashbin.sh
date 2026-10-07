#!/usr/bin/env bash
# Build flash.bin for i.MX 8M Mini with wolfBoot as BL33 (replaces U-Boot
# proper). See "NXP i.MX 8M Mini" in docs/Targets.md.
#
# Usage: IMX_MKIMAGE=<imx-mkimage> imx8mm-mkflashbin.sh
#        DISK_EMMC=0 IMX_MKIMAGE=<imx-mkimage> imx8mm-mkflashbin.sh Image DTB
# Env: CROSS_COMPILE, CONFIG, MAKE_ARGS, MKIMAGE, UBOOT_DTB, VERSION
# Overwrites .config and wolfboot.bin; uses the existing signing key.
set -e
# Paths from the caller are relative to the current directory
abspath() { case "$1" in /*) echo "$1" ;; *) echo "$PWD/$1" ;; esac; }
KERNEL="${1:+$(abspath "$1")}"
DTB="${2:+$(abspath "$2")}"
[ -z "${IMX_MKIMAGE:-}" ] || IMX_MKIMAGE="$(abspath "$IMX_MKIMAGE")"
[ -z "${CONFIG:-}" ] || CONFIG="$(abspath "$CONFIG")"
case "${MKIMAGE:-}" in */*) MKIMAGE="$(abspath "$MKIMAGE")" ;; esac
cd "$(dirname "$0")/../../.."

BL33_BASE=0x40200000          # WOLFBOOT_LOAD_BASE with IMX8MM_BL33=1
KERNEL_ADDR=0x48000000        # IMX8MM_KERNEL_ADDR with IMX8MM_BL33=1
DTB_ADDR=0x43000000           # dts_addr (hal/imx8mm.ld)
ATF_ADDR=0x00920000           # BL31 entry
SPL_LOAD_ADDR=0x7E1000        # U-Boot SPL on i.MX 8M Mini
FIT_OFFSET=0x60000            # SPL reads the FIT from eMMC sector 0x300

CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
CONFIG="${CONFIG:-config/examples/imx8mm-pico.config}"
MAKE_ARGS="${MAKE_ARGS:-}"
MKIMAGE="${MKIMAGE:-mkimage}"
UBOOT_DTB="${UBOOT_DTB:-imx8mm-pico-pi.dtb}"
DISK_EMMC="${DISK_EMMC:-1}"
VERSION="${VERSION:-1}"
WORK=imx8mm_flashbin
OUT=flash.bin

[ -n "${IMX_MKIMAGE:-}" ] || { echo "ERROR: set IMX_MKIMAGE" >&2; exit 1; }
command -v dtc >/dev/null || {
    echo "ERROR: dtc not in PATH (add U-Boot's scripts/dtc)" >&2; exit 1; }
SOC_DIR="$(cd "$IMX_MKIMAGE/iMX8M" && pwd)"
for f in u-boot-spl-ddr.bin bl31.bin mkimage_imx8 "$UBOOT_DTB"; do
    [ -f "$SOC_DIR/$f" ] || { echo "ERROR: missing $SOC_DIR/$f" >&2; exit 1; }
done
if [ "$DISK_EMMC" != "1" ]; then
    [ -f "$KERNEL" ] && [ -f "$DTB" ] || {
        echo "ERROR: DISK_EMMC=0 needs the kernel Image and the DTB" >&2; exit 1; }
fi

cp "$CONFIG" .config
make clean >/dev/null
# DEBUG=0: -Os
# shellcheck disable=SC2086  # MAKE_ARGS must word-split into separate options
make CROSS_COMPILE="$CROSS_COMPILE" DEBUG="${DEBUG:-0}" IMX8MM_BL33=1 \
    DISK_EMMC="$DISK_EMMC" $MAKE_ARGS wolfboot.bin
"${CROSS_COMPILE}readelf" -h wolfboot.elf | \
    grep -q "Entry point address: *$BL33_BASE" || \
    { echo "ERROR: wolfboot.elf entry is not $BL33_BASE" >&2; exit 1; }

rm -rf "$WORK" && mkdir -p "$WORK"
cp wolfboot.bin "$WORK/"
cp "$SOC_DIR/bl31.bin" "$SOC_DIR/u-boot-spl-ddr.bin" "$WORK/"
cp "$SOC_DIR/$UBOOT_DTB" "$WORK/uboot.dtb"

LINUX_IMAGES=""
LOADABLES='"atf-1"'
if [ "$DISK_EMMC" != "1" ]; then
    make keytools >/dev/null
    tools/keytools/sign --rsa4096 --sha3 --dts "$DTB" "$KERNEL" \
        wolfboot_signing_private_key.der "$VERSION"
    SIGNED="${KERNEL%.*}_v${VERSION}_signed.bin"
    cp "$SIGNED" "$WORK/kernel_signed.bin"
    cp "$DTB" "$WORK/linux.dtb"
    LINUX_IMAGES="
		kernel-1 {
			description = \"wolfBoot signed Linux kernel\";
			data = /incbin/(\"kernel_signed.bin\");
			type = \"firmware\";
			arch = \"arm64\";
			compression = \"none\";
			load = <$KERNEL_ADDR>;
		};
		linuxdtb-1 {
			description = \"Linux DTB\";
			data = /incbin/(\"linux.dtb\");
			type = \"firmware\";
			arch = \"arm64\";
			compression = \"none\";
			load = <$DTB_ADDR>;
		};"
    LOADABLES='"atf-1", "kernel-1", "linuxdtb-1"'
fi

cat > "$WORK/wolfboot.its" <<EOF
/dts-v1/;

/ {
	description = "ATF + wolfBoot (BL33)";

	images {
		uboot-1 {
			description = "wolfBoot (BL33)";
			data = /incbin/("wolfboot.bin");
			type = "standalone";
			arch = "arm64";
			compression = "none";
			load = <$BL33_BASE>;
		};
		fdt-1 {
			description = "U-Boot DTB (required by SPL, unused)";
			data = /incbin/("uboot.dtb");
			type = "flat_dt";
			compression = "none";
		};
		atf-1 {
			description = "ARM Trusted Firmware";
			data = /incbin/("bl31.bin");
			type = "firmware";
			arch = "arm64";
			compression = "none";
			load = <$ATF_ADDR>;
			entry = <$ATF_ADDR>;
		};
$LINUX_IMAGES
	};

	configurations {
		default = "config-1";
		config-1 {
			description = "wolfBoot";
			firmware = "uboot-1";
			loadables = $LOADABLES;
			fdt = "fdt-1";
		};
	};
};
EOF

# External data at 0x5000, clear of the HAB IVT/CSF that mkimage_imx8 adds
(cd "$WORK" && "$MKIMAGE" -E -p 0x5000 -f wolfboot.its wolfboot.itb)

# SPL + DDR firmware, then the FIT at FIT_OFFSET
(cd "$WORK" && "$SOC_DIR/mkimage_imx8" -version v1 -fit \
    -loader u-boot-spl-ddr.bin "$SPL_LOAD_ADDR" \
    -second_loader wolfboot.itb "$BL33_BASE" "$FIT_OFFSET" -out "../$OUT")

echo "$OUT: $(stat -c%s "$OUT") bytes"
echo
if [ "$DISK_EMMC" = "1" ]; then
    echo "Write it to the eMMC user area at 33 KB, e.g. from Linux on the board:"
    echo "  dd if=$OUT of=/dev/mmcblk2 bs=512 seek=66 conv=fsync"
    echo "or boot it over USB (serial download mode): uuu -b spl $OUT"
else
    echo "Boot it over USB (serial download mode): uuu -b spl $OUT"
fi
