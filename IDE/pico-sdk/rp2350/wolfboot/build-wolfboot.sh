#!/bin/bash
#cd ../../../.. && make keytools && make src/keystore.c && cd -
cd ../../../.. && make include/target.h && cd -
mkdir -p build
cd build
cmake .. -DPICO_SDK_PATH=$PICO_SDK_PATH -DPICO_PLATFORM=rp2350
make clean && make
cd ..
JLinkExe -Device RP2350_M33_0 -If swd -Speed 4000 -CommanderScript erase.jlink
JLinkExe -Device RP2350_M33_0 -If swd -Speed 4000 -CommanderScript flash_wolfboot.jlink
