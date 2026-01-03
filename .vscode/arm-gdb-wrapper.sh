#!/bin/bash
# Wrapper script for arm-none-eabi-gdb that skips .gdbinit
# This is needed because cortex-debug doesn't support passing --nx to GDB

exec /opt/arm-gnu-toolchain-14.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-gdb --nx "$@"

