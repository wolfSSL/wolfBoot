#!/bin/bash
V=`./wolfboot.elf update_trigger get_version 2>/dev/null`
if [ "x$V" != "x1" ]; then
    echo "Failed first boot with update_trigger"
    exit 1
fi

./wolfboot.elf powerfail 0 get_version 2>/dev/null
./wolfboot.elf powerfail 15000 get_version 2>/dev/null
./wolfboot.elf powerfail 18000 get_version 2>/dev/null
./wolfboot.elf powerfail 1a000 get_version 2>/dev/null

V=`./wolfboot.elf get_version 2>/dev/null`
if [ "x$V" != "x2" ]; then
    echo "Failed update (V: $V)"
    exit 1
fi

./wolfboot.elf powerfail 1000 get_version 2>/dev/null
./wolfboot.elf powerfail 11000 get_version 2>/dev/null
./wolfboot.elf powerfail 14000 get_version 2>/dev/null
./wolfboot.elf powerfail 1e000 get_version 2>/dev/null

V=`./wolfboot.elf get_version 2>/dev/null`
if [ "x$V" != "x1" ]; then
    echo "Failed fallback (V: $V)"
    exit 1
fi

echo Test successful.
exit 0
