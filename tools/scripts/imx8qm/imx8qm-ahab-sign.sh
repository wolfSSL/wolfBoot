#!/usr/bin/env bash
#
# imx8qm-ahab-sign.sh - AHAB-sign an i.MX 8QuadMax boot container.
#
# AHAB is the SoC's own secure boot: the SECO firmware authenticates the boot
# container (SCFW + ATF + wolfBoot-as-BL33) against a Super Root Key hash burnt
# into fuses, before any A-core runs. It is the layer *below* wolfBoot's own
# signature checking, and the two are independent:
#
#   AHAB  -> authenticates the container, i.e. wolfBoot itself
#   wolfBoot -> authenticates the OS/application image it goes on to boot
#
# A flash.bin built by imx8qm-mkflashbin.sh is unsigned ("open" lifecycle) and
# boots on a board whose SRK fuses are blank. Signing it is what makes it boot
# on a closed part.
#
# This script covers the signing step only. Burning the SRK hash and closing
# the part are deliberately NOT automated: both are one-way fuse operations
# that permanently reject any container not signed by the matching key, and a
# mistake bricks the board. See docs/Targets.md for that procedure.
#
# Usage:
#   CST_PATH=/path/to/cst-<ver> \
#   SRK_TABLE=/path/to/SRK_1_2_3_4_table.bin \
#   SRK_KEY=/path/to/SRK1_sha384_secp384r1_v3_ca_crt.pem \
#   CERT_KEY=/path/to/SGK1_sha384_secp384r1_v3_usr_crt.pem \
#     tools/scripts/imx8qm/imx8qm-ahab-sign.sh flash.bin [mkimage.log]
#
# The SRK table and keys come from the NXP Code Signing Tool's own PKI
# scripts (hab4_pki_tree.sh / srktool); this script does not generate them,
# because the key material must outlive any one build.
set -e

IMAGE="${1:-flash.bin}"
MKIMAGE_LOG="${2:-}"

for v in CST_PATH SRK_TABLE SRK_KEY CERT_KEY; do
    if [ -z "$(eval echo \"\$$v\")" ]; then
        echo "ERROR: $v is not set. See the header of this script." >&2
        exit 1
    fi
done
[ -f "$IMAGE" ]     || { echo "ERROR: no such image: $IMAGE" >&2; exit 1; }
[ -f "$SRK_TABLE" ] || { echo "ERROR: no such SRK table: $SRK_TABLE" >&2; exit 1; }

CST="$CST_PATH/linux64/bin/cst"
[ -x "$CST" ] || { echo "ERROR: cst not executable: $CST" >&2; exit 1; }

# imx-mkimage prints the container offsets the CSF has to reference, e.g.
#   CST: CONTAINER 0 offset: 0x400
#   CST: CONTAINER 0: Signature Block: offset is at 0x590
# Take them from a saved build log if one was given, otherwise fall back to
# the offsets the stock iMX8QM recipe produces.
if [ -n "$MKIMAGE_LOG" ] && [ -f "$MKIMAGE_LOG" ]; then
    CONTAINER_OFFSET=$(grep -m1 'CST: CONTAINER 0 offset:' "$MKIMAGE_LOG" \
        | grep -oE '0x[0-9a-fA-F]+')
    SIGBLK_OFFSET=$(grep -m1 'CST: CONTAINER 0: Signature Block: offset is at' \
        "$MKIMAGE_LOG" | grep -oE '0x[0-9a-fA-F]+')
fi
CONTAINER_OFFSET="${CONTAINER_OFFSET:-0x400}"
SIGBLK_OFFSET="${SIGBLK_OFFSET:-0x590}"

echo "Signing $IMAGE"
echo "  container offset:       $CONTAINER_OFFSET"
echo "  signature block offset: $SIGBLK_OFFSET"
echo "  SRK index:              ${SRK_INDEX:-0}"

CSF="$(mktemp -t imx8qm-ahab-XXXXXX.csf)"
trap 'rm -f "$CSF" "$CSF.tmp"' EXIT

cat > "$CSF" <<EOF
[Header]
    Target = AHAB
    Version = 1.0

[Install SRK]
    File = "$SRK_TABLE"
    Source = "$SRK_KEY"
    Source index = ${SRK_INDEX:-0}
    Source set = OEM
    Revocations = 0x0

[Authenticate Data]
    File = "$IMAGE"
    Offsets = $CONTAINER_OFFSET $SIGBLK_OFFSET
EOF

# The CST rewrites the image in place, so work on a copy: a failed signing run
# must not leave a half-written container behind.
cp "$IMAGE" "$IMAGE.signing"
# Not "sed -i": GNU takes a bare -i, BSD/macOS requires a backup suffix, so the
# portable form is a temp file and a move.
sed "s|File = \"$IMAGE\"|File = \"$IMAGE.signing\"|" "$CSF" > "$CSF.tmp"
mv "$CSF.tmp" "$CSF"
"$CST" -i "$CSF" -o "$IMAGE.signing"
mv "$IMAGE.signing" "${IMAGE%.bin}-signed.bin"

echo "Signed container: ${IMAGE%.bin}-signed.bin"
echo
echo "This boots on an open part exactly like the unsigned image. It only"
echo "becomes required once the SRK hash is fused and the part is closed -"
echo "see the AHAB section of docs/Targets.md before doing either."
