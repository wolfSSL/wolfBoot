#!/bin/bash

WORK_DIR=/tmp/qemu_fsp
EDKII_TAG=edk2-stable202011
EDKII_REPO=https://github.com/tianocore/edk2.git
SBL_COMMIT_ID=c80d8d592cf127616daca5df03ac7731e78ffcc1
SBL_PATCH_URL=https://github.com/slimbootloader/slimbootloader/raw/${SBL_COMMIT_ID}/Silicon/QemuSocPkg/FspBin/Patches/0001-Build-QEMU-FSP-2.0-binaries.patch
SCRIPT_DIR=$(readlink -f "$(dirname "$0")")
WOLFBOOT_DIR="${SCRIPT_DIR}/../../.."
FSP_NAME=QEMU_FSP_DEBUG
CONFIG_FILE=${CONFIG_FILE:-"${WOLFBOOT_DIR}/.config"}

set -e

if [ ! -d "$WORK_DIR" ]; then
  mkdir -p "$WORK_DIR"
fi

if [ -f "${CONFIG_FILE}" ]
then
    FSP_T_BASE=$(grep -Eo '^FSP_T_BASE=.*' ${CONFIG_FILE} | cut -d "=" -f 2)
    FSP_M_BASE=$(grep -Eo '^FSP_M_BASE=.*' ${CONFIG_FILE} | cut -d "=" -f 2)
    FSP_S_LOAD_BASE=$(grep -Eo '^FSP_S_LOAD_BASE=.*' ${CONFIG_FILE} | cut -d "=" -f 2)
else
    echo "Error: ${CONFIG_FILE} file not found in current directory"
    exit
fi

download_edkii() {
    (cd "$WORK_DIR" &&
         git clone "${EDKII_REPO}" edk2 &&
         cd edk2 &&
         git checkout "${EDKII_TAG}")
}

download_sbl_patch_and_patch_edkii() {
    (cd "$WORK_DIR/edk2" &&
         curl -L -o 0001-Build-QEMU-FSP-2.0-binaries.patch ${SBL_PATCH_URL};
         git am --keep-cr --whitespace=nowarn 0001-Build-QEMU-FSP-2.0-binaries.patch)
    (cd "$WORK_DIR/edk2" &&
         git am --keep-cr --whitespace=nowarn "${SCRIPT_DIR}"/0002-add-W-no-warnings-to-compile-with-gcc-12.patch "${SCRIPT_DIR}"/0003-disable-optmizaion-patch-for-edk2.patch "${SCRIPT_DIR}"/0004-fix-PatchFv-make-regex-match-both-8-and-16-length-ad.patch;
         mkdir -p MdeModulePkg/Library/BrotliCustomDecompressLib/brotli/c/include/)
}

build_qemu_fsp() {
    (cd "$WORK_DIR/edk2" &&
         python ./BuildFsp.py &&
         cd BuildFsp &&
         python ../IntelFsp2Pkg/Tools/SplitFspBin.py split -f QEMU_FSP_DEBUG.fd)
}

rebase_fsp_component() {
    component=$1
    new_base=$2
    if [ -n "${new_base}" ]
    then
        (cd "${WORK_DIR}/edk2/BuildFsp" &&
             python ../IntelFsp2Pkg/Tools/SplitFspBin.py rebase -f "${FSP_NAME}_${component^^}.fd" -b ${new_base} -c ${component,,})
    fi
}

copy_fsp_component() {
    component=$1
    base=$2

    if [ -n "${base}" ]
    then
        base_no_prefix=${base#0x}
        suffix=${component^^}_${base_no_prefix^^}
    else
        suffix=${component^^}
    fi

    cp "${WORK_DIR}/edk2/BuildFsp/${FSP_NAME}_${suffix}.fd" "src/x86/fsp_${component,,}.bin"
}

copy_fsp_headers() {
    (cd "$WORK_DIR"/edk2/BuildFsp && patch -p1 < "${SCRIPT_DIR}"/fsp-headers-patch.patch)
    (cp "$WORK_DIR/edk2/BuildFsp/FspUpd.h" "$WOLFBOOT_DIR/include/x86/fsp/")
    (cp "$WORK_DIR/edk2/BuildFsp/FsptUpd.h" "$WOLFBOOT_DIR/include/x86/fsp/")
    (cp "$WORK_DIR/edk2/BuildFsp/FspmUpd.h" "$WOLFBOOT_DIR/include/x86/fsp/")
    (cp "$WORK_DIR/edk2/BuildFsp/FspsUpd.h" "$WOLFBOOT_DIR/include/x86/fsp/")
}


download_edkii
download_sbl_patch_and_patch_edkii
build_qemu_fsp
rebase_fsp_component "T" ${FSP_T_BASE}
rebase_fsp_component "M" ${FSP_M_BASE}
rebase_fsp_component "S" ${FSP_S_LOAD_BASE}
copy_fsp_component "T" ${FSP_T_BASE}
copy_fsp_component "M" ${FSP_M_BASE}
copy_fsp_component "S" ${FSP_S_LOAD_BASE}
copy_fsp_headers
