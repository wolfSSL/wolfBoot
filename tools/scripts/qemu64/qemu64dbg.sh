#!/usr/bin/env bash
qemu-system-x86_64 -m 1G -machine q35 -serial mon:stdio -nographic  \
    -pflash wolfboot_stage1.bin -drive id=mydisk,format=raw,file=app.bin,if=none \
    -device ide-hd,drive=mydisk -S -s

