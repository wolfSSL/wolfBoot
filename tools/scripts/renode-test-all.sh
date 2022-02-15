#!/bin/bash

EXTRA_TARGETS="stm32f7 nrf52840 hifive1"

if (test -e .config); then
    mv .config config.bak
fi

echo testing on stm32f4
make renode-factory-all || exit 2

for cfg in $EXTRA_TARGETS; do
    rm -f .config
    echo testing on $cfg
    cp config/examples/$cfg.config .config || exit 1
    make renode-factory-all || exit 2
done

if (test -e config.bak); then
    mv config.bak .config
fi
