#!/usr/bin/env bash

set -e

#qemu-system-x86_64 -m 256M -machine q35 -serial mon:stdio -nographic -pflash \
#                ../wolfBoot/x86_qemu_flash.bin -gdb tcp::3333 -S \
#                -drive file=test_img.bin,if=none,id=SATADRV \
#                -device ich9-ahci,id=ahci \
#                -device ide-hd,drive=SATADRV,bus=ahci.0

# qemu-system-i386 -m 256M -machine q35 -serial mon:stdio -nographic -pflash \
#                  x86_qemu_flash.bin -S -s \
#                 -drive file=test_img.bin,if=none,id=SATADRV,format=raw \
#                 -device ich9-ahci,id=ahci \
#                 -device ide-hd,drive=SATADRV,bus=ahci.0


# qemu-system-x86_64 -m 1G -machine q35 -serial mon:stdio -nographic \
#   -drive id=mydisk,format=raw,file=app.bin,if=none \
#   -device ide-hd,drive=mydisk \
#   -kernel /home/arch/br-linux-wolfboot/output/images/bzImage \
#   -append "acpi=off"

#qemu-system-x86_64 -m 256M -machine q35 -serial mon:stdio -nographic \
qemu-system-x86_64 -m 256M -machine q35 \
  -drive id=mydisk,format=raw,file=app.bin,if=none \
  -device ide-hd,drive=mydisk \
  -pflash loader.bin
  # -kernel /home/arch/br-linux-wolfboot/output/images/bzImage \
  # -append "acpi=off loglevel=7 libata.noacpi=1 libata.force=6.0:noncq libata.dma=0" \

  # -S -s
