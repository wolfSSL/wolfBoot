#!/bin/bash
V=`./wolfboot.elf update_trigger get_version 2>/dev/null`
if [ "x$V" != "x1" ]; then
    echo "Failed first boot with update_trigger"
    exit 1
fi

./wolfboot.elf powerfail C0020000 get_version 2>/dev/null
./wolfboot.elf powerfail C0022000 get_version 2>/dev/null
./wolfboot.elf powerfail C0024000 get_version 2>/dev/null
# fail on the last sector to stop the encrypt key save and state update
./wolfboot.elf powerfail C005F000 get_version 2>/dev/null

V=`./wolfboot.elf get_version 2>/dev/null`
if [ "x$V" != "x2" ]; then
    echo "Failed update (V: $V)"
    exit 1
fi

./wolfboot.elf powerfail C0021000 get_version 2>/dev/null
./wolfboot.elf powerfail C0023000 get_version 2>/dev/null
./wolfboot.elf powerfail C0025000 get_version 2>/dev/null
./wolfboot.elf powerfail C005F000 get_version 2>/dev/null

V=`./wolfboot.elf get_version 2>/dev/null`
if [ "x$V" != "x1" ]; then
    echo "Failed fallback (V: $V)"
    exit 1
fi

echo Test successful.
exit 0
