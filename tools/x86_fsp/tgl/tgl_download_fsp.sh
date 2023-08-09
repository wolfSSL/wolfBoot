#!/bin/bash

WORK_DIR=/tmp/tgl_fsp
EDK2_COMMIT_ID=df25a5457f04ec465dce97428cfee96f462676e7
FSP_TOOL_URL=https://github.com/tianocore/edk2/raw/${EDK2_COMMIT_ID}/IntelFsp2Pkg/Tools/SplitFspBin.py
FSP_COMMIT_ID=6f5ae9679e662353bc5570a1bc89e137e262155f
FSP_URL=https://github.com/intel/FSP/raw/${FSP_COMMIT_ID}/TigerLakeFspBinPkg/TGL_IOT/Fsp.fd
FSP_REPO=https://github.com/intel/FSP.git
UCODE_URL=https://github.com/intel/Intel-Linux-Processor-Microcode-Data-Files/raw/6f36ebde4519f8a21a047c3433b80a3fb41361e1/intel-ucode/06-8c-01
SCRIPT_DIR=$(readlink -f "$(dirname "$0")")
WOLFBOOT_DIR="${SCRIPT_DIR}/../../.."
FSP_PREFIX=Fsp
CONFIG_FILE=${CONFIG_FILE:-"${WOLFBOOT_DIR}/.config"}
PATCH="${SCRIPT_DIR}/0001-FSP-wolfboot-patch.patch"

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

clone_repo() {
    (mkdir "$WORK_DIR"/FSP)
    (cd "$WORK_DIR"/FSP/ && git init && git remote add origin ${FSP_REPO})
    (cd "$WORK_DIR"/FSP/ && git fetch --depth 1 origin ${FSP_COMMIT_ID})
    (cd "$WORK_DIR"/FSP/ && git checkout FETCH_HEAD)
}

copy_tgl_fsp() {
    (cp "$WORK_DIR"/FSP/TigerLakeFspBinPkg/TGL_IOT/Fsp.fd "$WORK_DIR")
}

split_fsp() {
    (cd "$WORK_DIR" && python SplitFspBin.py split -f ${FSP_PREFIX}.fd)
}

download_split_tool() {
    (cd "$WORK_DIR" && curl -L -o SplitFspBin.py ${FSP_TOOL_URL})
}

patch_tgl_fsp() {
    (cd "$WORK_DIR"/FSP && git am --keep-cr "${PATCH}")
}

rebase_fsp_component() {
    component=$1
    new_base=$2
    if [ -n "${new_base}" ]
    then
        (cd "${WORK_DIR}" &&
             python SplitFspBin.py rebase -f "${FSP_PREFIX}_${component^^}.fd" -b ${new_base} -c ${component,,})
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

    cp "${WORK_DIR}/${FSP_PREFIX}_${suffix}.fd" "src/x86/fsp_${component,,}.bin"
}

copy_fsp_headers() {
    (cp "$WORK_DIR/FSP/TigerLakeFspBinPkg/TGL_IOT/Include/FspUpd.h" "$WOLFBOOT_DIR/include/x86/fsp/")
    (cp "$WORK_DIR/FSP/TigerLakeFspBinPkg/TGL_IOT/Include/FsptUpd.h" "$WOLFBOOT_DIR/include/x86/fsp/")
    (cp "$WORK_DIR/FSP/TigerLakeFspBinPkg/TGL_IOT/Include/FspmUpd.h" "$WOLFBOOT_DIR/include/x86/fsp/")
    (cp "$WORK_DIR/FSP/TigerLakeFspBinPkg/TGL_IOT/Include/FspsUpd.h" "$WOLFBOOT_DIR/include/x86/fsp/")
    (cp "$WORK_DIR/FSP/TigerLakeFspBinPkg/TGL_IOT/Include/MemInfoHob.h" "$WOLFBOOT_DIR/include/x86/fsp/")
}

download_ucode() {
    curl -L -o src/x86/ucode0.bin ${UCODE_URL}
}

clone_repo
copy_tgl_fsp
download_split_tool
split_fsp
rebase_fsp_component "T" ${FSP_T_BASE}
rebase_fsp_component "M" ${FSP_M_BASE}
rebase_fsp_component "S" ${FSP_S_LOAD_BASE}
copy_fsp_component "T" ${FSP_T_BASE}
copy_fsp_component "M" ${FSP_M_BASE}
copy_fsp_component "S" ${FSP_S_LOAD_BASE}
patch_tgl_fsp
copy_fsp_headers
download_ucode
