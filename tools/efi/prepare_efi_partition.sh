#!/bin/sh

dd if=/dev/zero of=/tmp/efi.disk bs=256M count=1
sudo mkfs.vfat /tmp/efi.disk
mkdir -p /tmp/efi
sudo mount /tmp/efi.disk /tmp/efi -oloop
cat <<EOF > /tmp/startup.nsh
@echo -off
echo Starting wolfBoot EFI...
fs0:
wolfboot.efi
EOF
sudo mv /tmp/startup.nsh /tmp/efi/ 2>/dev/null
sudo umount /tmp/efi
