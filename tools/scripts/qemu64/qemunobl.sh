#!/usr/bin/env bash
qemu-system-x86_64 -m 1G -machine q35 -serial mon:stdio -nographic \
    -kernel bzImage \
    -drive id=mydisk,format=raw,file=app.bin,if=none \
    -device ide-hd,drive=mydisk 

