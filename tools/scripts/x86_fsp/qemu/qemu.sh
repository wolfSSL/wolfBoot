#!/bin/bash


#By default, the script will run without waiting for GDB. If you use the
# -w parameter, it will wait for GDB to connect.

# Usage:


# For DEBUG_STAGE1 without waiting for GDB: ./qemu.sh -d DEBUG_STAGE1
# For DEBUG_STAGE2 without waiting for GDB (default): ./qemu.sh
# For DEBUG_STAGE1 with waiting for GDB: ./qemu.sh -d DEBUG_STAGE1 -w
# For DEBUG_STAGE2 with waiting for GDB: ./qemu.sh -w


# To DEBUG_STAGE1
# $ gdb stage1/loader_stage1.elf
# target remote :1234
# b start
# c

# To DEBUG_STAGE2
# $ gdb wolfboot.elf
# target remote :1234
# b main
# c


## TPM emulator:
# https://github.com/stefanberger/swtpm
# sudo apt install swtpm

# Default values
DEBUG_STAGE="DEBUG_STAGE2"
WAIT_FOR_GDB=false

echo "Running wolfBoot on QEMU"

set -e
set -x

# Parse command line options
while getopts "d:wpt" opt; do
    case "$opt" in
        d)
            DEBUG_STAGE="$OPTARG"
            ;;
        w)
            WAIT_FOR_GDB=true
            ;;
        p)
            CREATE_PIPE=true
            ;;
        t)  ENABLE_TPM=true
            ;;
        *)
            echo "Usage: $0 [-d DEBUG_STAGE1 | DEBUG_STAGE2] [-w] [-p]"
            echo "-p : create /tmp/qemu_mon.in and /tmp/qemu_mon.out pipes for monitor qemu"
            echo "-w : wait for GDB to connect to the QEMU gdb server"
            echo "-t : enable TPM emulation (requires swtpm)"
            exit 1
            ;;
    esac
done

if (test -z $OVMF_PATH); then
    if (test -f /usr/share/edk2-ovmf/x64/OVMF.fd); then
        OVMF_PATH=/usr/share/edk2-ovmf/x64
    elif (test -f /usr/share/qemu/OVMF.fd); then
        OVMF_PATH=/usr/share/qemu
    else
        OVMF_PATH=/
    fi
fi

QEMU_TPM_OPTIONS=" \
    -chardev socket,id=chrtpm,path=/tmp/swtpm/swtpm-sock \
    -tpmdev emulator,id=tpm0,chardev=chrtpm \
    -device tpm-tis,tpmdev=tpm0 \
    "

QEMU_OPTIONS=" \
    -m 1G -machine q35 -nographic \
    -pflash wolfboot_stage1.bin -drive id=mydisk,format=raw,file=app.bin,if=none \
    -device ide-hd,drive=mydisk \
    "

if [ "$CREATE_PIPE" = true ]; then
    rm /tmp/qemu_mon.in /tmp/qemu_mon.out || true &> /dev/null
    mkfifo /tmp/qemu_mon.in /tmp/qemu_mon.out
    QEMU_OPTIONS+="\
       -chardev pipe,id=qemu_mon,path=/tmp/qemu_mon \
       -mon chardev=qemu_mon \
       -serial stdio \
    "
fi

# If waiting for GDB is true, append options to QEMU_OPTIONS
if [ "$WAIT_FOR_GDB" = true ]; then
    QEMU_OPTIONS="${QEMU_OPTIONS} -S -s"
fi

if [ "$DEBUG_STAGE" = "DEBUG_STAGE1" ]; then
    QEMU=qemu-system-i386
else
    QEMU=qemu-system-x86_64
fi

if [ "$ENABLE_TPM" = true ]; then
    killall swtpm || true
    sleep 1
    echo TPM Emulation ON
    mkdir -p /tmp/swtpm
    swtpm socket --tpm2 --tpmstate dir=/tmp/swtpm \
        --ctrl type=unixio,path=/tmp/swtpm/swtpm-sock --log level=20 &
    sleep .5
    QEMU_OPTIONS+=$QEMU_TPM_OPTIONS
fi

echo Running QEMU...
echo "$QEMU $QEMU_OPTIONS"
$QEMU $QEMU_OPTIONS
