#!/bin/bash

V=`./wolfboot.elf update_trigger get_version 2>/dev/null`
if [ "x$V" != "x1" ]; then
    echo "Failed first boot with update_trigger"
    exit 1
fi


V=`./wolfboot.elf success get_version emergency 2>/dev/null`
if [ "x$V" != "x1" ]; then
    echo "Failed fallback (V: $V)"
    exit 1
fi

echo Test successful.
exit 0
