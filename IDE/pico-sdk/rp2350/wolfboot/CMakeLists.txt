cmake_minimum_required(VERSION 3.13)
set(WOLFBOOT_PATH ../../../..)
set(CMAKE_CXX_COMPILER arm-none-eabi-gcc)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

set(PICOTOOL_FETCH_FROM_GIT_PATH build/picotool)

project(wolfboot)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()


add_executable(wolfboot 
    ${WOLFBOOT_PATH}/src/image.c
    ${WOLFBOOT_PATH}/src/loader.c
    ${WOLFBOOT_PATH}/src/update_flash.c
    ${WOLFBOOT_PATH}/src/keystore.c
    ${WOLFBOOT_PATH}/src/libwolfboot.c
    ${WOLFBOOT_PATH}/src/boot_arm.c

    ${WOLFBOOT_PATH}/hal/rp2350.c

    ${WOLFBOOT_PATH}/lib/wolfssl/wolfcrypt/src/sp_int.c
    ${WOLFBOOT_PATH}/lib/wolfssl/wolfcrypt/src/sp_cortexm.c
    ${WOLFBOOT_PATH}/lib/wolfssl/wolfcrypt/src/memory.c
    ${WOLFBOOT_PATH}/lib/wolfssl/wolfcrypt/src/random.c
    ${WOLFBOOT_PATH}/lib/wolfssl/wolfcrypt/src/sha256.c
    ${WOLFBOOT_PATH}/lib/wolfssl/wolfcrypt/src/sha512.c
    ${WOLFBOOT_PATH}/lib/wolfssl/wolfcrypt/src/aes.c
    ${WOLFBOOT_PATH}/lib/wolfssl/wolfcrypt/src/ecc.c
)

# Add cflags
target_compile_options(wolfboot PRIVATE
    -D__WOLFBOOT
    -DWOLFSSL_USER_SETTINGS
    -mcpu=cortex-m33
    -DCORTEX_M33
    -DTZEN
    -mcmse
    -DWOLFSSL_SP_ASM
    -DWOLFSSL_SP_ARM_CORTEX_M_ASM
    -DWOLFSSL_ARM_ARCH=8
    -DARCH_FLASH_OFFSET=0x10000000
    -DWOLFBOOT_ORIGIN=0x10000000
    -DBOOTLOADER_PARTITION_SIZE=0x40000
    -DWOLFBOOT_ARCH_ARM
    -DTARGET_rp2350
    -DWOLFBOOT_SIGN_ECC256
    -DRAM_CODE
    -DFILL_BYTE=0xFF
    -Os
    -DWOLFBOOT_NO_MPU
    -DWOLFBOOT_HASH_SHA256
    -DIMAGE_HEADER_SIZE=1024
    -Wstack-usage=7632
)


target_include_directories(wolfboot PRIVATE 
    ${WOLFBOOT_PATH}/include
    ${WOLFBOOT_PATH}/lib/wolfssl
)

target_link_libraries(wolfboot pico_stdlib hardware_flash)
pico_set_linker_script(wolfboot ../../../../../hal/rp2350.ld)

pico_enable_stdio_usb(wolfboot 1)
pico_enable_stdio_uart(wolfboot 0)


pico_add_extra_outputs(wolfboot)
