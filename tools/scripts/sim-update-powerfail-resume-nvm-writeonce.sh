#!/bin/bash
V=`./wolfboot.elf update_trigger get_version 2>/dev/null`
if [ "x$V" != "x1" ]; then
    echo "Failed first boot with update_trigger"
    exit 1
fi

./wolfboot.elf powerfail C0020000 get_version 2>/dev/null

# for NVM_FLASH_WRITEONCE the final 2 boot sectors are erased multiple times
# need to test powerfail as the erase address progresses instead of specific
# addresses
x=0
while [ $x -le 300 ]
do
    ./wolfboot.elf powerfail_progressive 2>/dev/null
    x=$(( $x + 1 ))
done

V=`./wolfboot.elf get_version 2>/dev/null`
if [ "x$V" != "x2" ]; then
    echo "Failed update (V: $V)"
    exit 1
fi

./wolfboot.elf powerfail C0020000 get_version 2>/dev/null

x=0
while [ $x -le 300 ]
do
    ./wolfboot.elf powerfail_progressive 2>/dev/null
    x=$(( $x + 1 ))
done

V=`./wolfboot.elf get_version 2>/dev/null`
if [ "x$V" != "x1" ]; then
    echo "Failed fallback (V: $V)"
    exit 1
fi

echo Test successful.
exit 0
