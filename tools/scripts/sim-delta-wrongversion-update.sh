#!/bin/bash

V=`./wolfboot.elf update_trigger get_version 2>/dev/null`
if [ "x$V" != "x1" ]; then
    echo "Failed first boot with update_trigger"
    exit 1
fi

# First boot: attempt update, should be rejected
V=`./wolfboot.elf success get_version 2>/dev/null`
if [ "x$V" != "x1" ]; then
    echo "Error: Delta update with wrong image reported as successful."
    exit 1
fi

# Second boot to verify system is alive
V=`./wolfboot.elf success get_version 2>/dev/null`
if [ "x$V" != "x1" ]; then
    echo "Error: System is possibly unrecoverable"
    exit 1
fi
echo "Update successfully rejected (V: $V)"

echo Test successful.
exit 0


