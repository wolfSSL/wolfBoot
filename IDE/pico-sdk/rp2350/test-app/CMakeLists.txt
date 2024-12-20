cmake_minimum_required(VERSION 3.13)
set(WOLFBOOT_PATH ../../../../)
set(CMAKE_CXX_COMPILER arm-none-eabi-gcc)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

set(PICOTOOL_FETCH_FROM_GIT_PATH ../wolfboot/build/picotool)
set(BOOT_STAGE2_FILE ${CMAKE_CURRENT_LIST_DIR}/boot2_empty.S)

project(blink)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()


add_executable(blink
    blink.c
)

pico_set_linker_script(blink ../../../../../hal/rp2350-app.ld)
target_link_libraries(blink pico_stdlib)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(blink)

