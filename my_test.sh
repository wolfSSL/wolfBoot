#!/bin/bash

rm -rf ./build-windows-stm32l4
rm -rf ./build-linux-stm32l4

cmake --preset linux-stm32l4
cmake --build --preset linux-stm32l4
