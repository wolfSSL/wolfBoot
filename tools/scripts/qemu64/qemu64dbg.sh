#!/bin/bash
qemu-system-x86_64 -m 8G -machine q35 -serial mon:stdio -nographic  \
    -pflash loader.bin -drive id=mydisk,format=raw,file=app.bin,if=none \
    -device ide-hd,drive=mydisk -S -s

