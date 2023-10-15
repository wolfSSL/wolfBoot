#!/usr/bin/env bash
#

## TPM emulator:
# https://github.com/stefanberger/swtpm

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
    -device tpm-tis,tpmdev=tpm0"
QEMU_OPTIONS=" \
    -m 1G -machine q35 -serial mon:stdio -nographic \
    -pflash wolfboot_stage1.bin -drive id=mydisk,format=raw,file=app.bin,if=none \
    -device ide-hd,drive=mydisk \
    -S -gdb tcp::3333"

#killall swtpm
#sleep 1
echo TPM Emulation ON
mkdir -p /tmp/swtpm
swtpm socket --tpm2 --tpmstate dir=/tmp/swtpm \
    --ctrl type=unixio,path=/tmp/swtpm/swtpm-sock --log level=20 &
sleep .5
echo Running QEMU...

qemu-system-i386 $QEMU_OPTIONS $QEMU_TPM_OPTIONS

