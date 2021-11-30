#!/bin/sh

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

QEMU_OPTIONS=" \
    -m 256M \
    -net none \
    -bios ${OVMF_PATH}/OVMF.fd \
    -drive file=/tmp/efi.disk,index=0,media=disk,format=raw \
    -vga none \
    -serial stdio \
    -display none"


QEMU_TPM_OPTIONS=" \
    -chardev socket,id=chrtpm,path=/tmp/swtpm/swtpm-sock \
    -tpmdev emulator,id=tpm0,chardev=chrtpm \
    -device tpm-tis,tpmdev=tpm0"

if (which swtpm); then
    killall swtpm
    sleep 1
    QEMU_EXTRA=$QEMU_TPM_OPTIONS
    echo TPM Emulation ON
    mkdir -p /tmp/swtpm
    swtpm socket --tpm2 --tpmstate dir=/tmp/swtpm \
        --ctrl type=unixio,path=/tmp/swtpm/swtpm-sock --log level=20 &
else
    echo TPM Emulation OFF
fi

mkdir -p /tmp/efi
sudo mount /tmp/efi.disk /tmp/efi
sudo cp wolfboot.efi /tmp/efi
sudo umount /tmp/efi

echo $QEMU_OPTIONS $QEMU_EXTRA

qemu-system-x86_64 $QEMU_OPTIONS $QEMU_EXTRA
