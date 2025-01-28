cmake_minimum_required(VERSION 3.13)
set(WOLFBOOT_PATH ../../../../)
set(CMAKE_CXX_COMPILER arm-none-eabi-gcc)
set(LIB_PICO_RUNTIME_INIT=0)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

set(PICOTOOL_FETCH_FROM_GIT_PATH ../wolfboot/build/picotool)
set(BOOT_STAGE2_FILE ${CMAKE_CURRENT_LIST_DIR}/boot2_empty.S)
set(PICO_NO_RUNTIME 1)

project(blink)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()


add_executable(blink
    blink.c
    runtime.c
)

target_compile_options(blink PRIVATE
    -DPICO_RUNTIME_NO_INIT_BOOTROM_RESET=1
    -DPICO_RUNTIME_NO_INIT_CLOCKS=1
    -DPICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1
)
target_compile_definitions(blink PRIVATE PICO_NO_RUNTIME=1)

pico_set_linker_script(blink ../../../../../hal/rp2350-app.ld)
target_link_libraries(blink pico_stdlib)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(blink)

