#!/bin/bash
qemu-system-i386 -m 1G -machine q35 -serial mon:stdio -nographic \
    -pflash stage1/loader_stage1.bin -drive id=mydisk,format=raw,file=app.bin,if=none \g
    -device ide-hd,drive=mydisk \
    -S -s 

