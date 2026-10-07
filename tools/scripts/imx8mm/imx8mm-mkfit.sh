#!/usr/bin/env bash
# Build and sign the FIT (kernel + DTB) that wolfBoot reads from eMMC p1 in
# BL33 mode. See "NXP i.MX 8M Mini" in docs/Targets.md.
#
# Usage: [SLOT=A|B] [VERSION=n] imx8mm-mkfit.sh Image DTB
# Env: ROOTDEV (root= in bootargs, default /dev/mmcblk2p2; bootargs is
#      added if the DTB has none), DRAM_SIZE (/memory added if the DTB has
#      none, default 0x80000000), MKIMAGE, DTC
set -e
# Paths from the caller are relative to the current directory
abspath() { case "$1" in /*) echo "$1" ;; *) echo "$PWD/$1" ;; esac; }
KERNEL="${1:+$(abspath "$1")}"
DTB="${2:+$(abspath "$2")}"
case "${MKIMAGE:-}" in */*) MKIMAGE="$(abspath "$MKIMAGE")" ;; esac
case "${DTC:-}" in */*) DTC="$(abspath "$DTC")" ;; esac
cd "$(dirname "$0")/../../.."

KERNEL_ADDR=0x44000000        # kernel load/entry address in the FIT

SLOT="${SLOT:-A}"
VERSION="${VERSION:-1}"
ROOTDEV="${ROOTDEV-/dev/mmcblk2p2}"
DRAM_SIZE="${DRAM_SIZE:-0x80000000}"
MKIMAGE="${MKIMAGE:-mkimage}"
DTC="${DTC:-dtc}"
KEY=wolfboot_signing_private_key.der
WORK=imx8mm_fit
OUT="fitImage_${SLOT}.itb"

[ -f "$KERNEL" ] && [ -f "$DTB" ] || {
    echo "Usage: $0 <kernel Image> <DTB>" >&2; exit 1; }
command -v "$DTC" >/dev/null && command -v dtc >/dev/null || {
    echo "ERROR: dtc not in PATH (add U-Boot's scripts/dtc)" >&2; exit 1; }
case "$SLOT" in A|B) ;; *) echo "ERROR: SLOT must be A or B" >&2; exit 1;; esac
[ -f .config ] || { echo "ERROR: no .config (copy the wolfBoot one)" >&2; exit 1; }
[ -f "$KEY" ] || { echo "ERROR: $KEY not found (build wolfBoot first)" >&2; exit 1; }

make keytools >/dev/null

rm -rf "$WORK" && mkdir -p "$WORK"
cp "$KERNEL" "$WORK/Image"
if [ -n "$ROOTDEV" ]; then
    "$DTC" -q -I dtb -O dts -o "$WORK/board.dts" "$DTB"
    if grep -q "^[[:space:]]*bootargs = " "$WORK/board.dts"; then
        sed -i "/^[[:space:]]*bootargs = /s#root=[^ \"]*#root=$ROOTDEV#" \
            "$WORK/board.dts"
    else
        # No bootargs (e.g. a kernel DTB): add one to /chosen
        sed -i "0,/^[[:space:]]*chosen {/s##&\n\t\tbootargs = \"console=ttymxc1,115200 root=$ROOTDEV rootwait rw\";#" \
            "$WORK/board.dts"
    fi
    # No /memory (U-Boot normally adds it): add DRAM_SIZE at 0x40000000
    if ! grep -q 'device_type = "memory"' "$WORK/board.dts"; then
        sz=$((DRAM_SIZE))
        printf '/ {\n\tmemory@40000000 {\n\t\tdevice_type = "memory";\n\t\treg = <0x0 0x40000000 0x%x 0x%x>;\n\t};\n};\n' \
            $((sz >> 32)) $((sz & 0xffffffff)) >> "$WORK/board.dts"
    fi
    grep -q "bootargs = .*root=$ROOTDEV[ \"]" "$WORK/board.dts" || \
        { echo "ERROR: cannot set root=$ROOTDEV in $DTB bootargs" >&2; exit 1; }
    "$DTC" -q -I dts -O dtb -o "$WORK/board.dtb" "$WORK/board.dts"
    grep "bootargs = " "$WORK/board.dts"
else
    cp "$DTB" "$WORK/board.dtb"
fi

cat > "$WORK/fitImage.its" <<EOF
/dts-v1/;

/ {
	description = "wolfBoot signed FIT for i.MX 8M Mini";
	#address-cells = <1>;

	images {
		kernel-1 {
			description = "Linux kernel";
			data = /incbin/("Image");
			type = "kernel";
			arch = "arm64";
			os = "linux";
			compression = "none";
			load = <$KERNEL_ADDR>;
			entry = <$KERNEL_ADDR>;
			hash-1 {
				algo = "sha256";
			};
		};
		fdt-1 {
			description = "Flattened Device Tree";
			data = /incbin/("board.dtb");
			type = "flat_dt";
			arch = "arm64";
			compression = "none";
			hash-1 {
				algo = "sha256";
			};
		};
	};

	configurations {
		default = "conf-1";
		conf-1 {
			description = "Linux kernel with FDT";
			kernel = "kernel-1";
			fdt = "fdt-1";
		};
	};
};
EOF

# Embedded data: wolfBoot reads the "data" properties
(cd "$WORK" && "$MKIMAGE" -f fitImage.its fitImage.itb)
tools/keytools/sign --rsa4096 --sha3 "$WORK/fitImage.itb" "$KEY" "$VERSION"
cp "$WORK/fitImage_v${VERSION}_signed.bin" "$OUT"

echo "$OUT: $(stat -c%s "$OUT") bytes, version $VERSION"
echo
echo "Copy it to eMMC p1 (FAT32) on the board, and sync before power-off:"
echo "  mount /dev/mmcblk2p1 /mnt && cp $OUT /mnt/ && sync && umount /mnt"
