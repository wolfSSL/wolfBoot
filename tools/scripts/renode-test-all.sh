#!/usr/bin/env bash

TARGETS="stm32f4 stm32f7 nrf52840 hifive1"

if (test -e .config); then
    mv .config config.bak
fi

for cfg in $TARGETS; do
    rm -f .config
    echo testing on $cfg
    cp config/examples/$cfg.config .config || exit 1
    make renode-factory-all Q="@" || exit 2
done

if (test -e config.bak); then
    mv config.bak .config
fi
