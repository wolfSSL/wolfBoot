# wolfBoot Loaders / Updaters

## loader.c

The default wolfBoot loader entry point that starts the wolfBoot secure boot process and leverages one of the `*_updater.c` implementations.

## loader_stage1.c

A first stage loader whose purpose is to load wolfBoot from flash to ram and jump to it. This is required on platforms where flash is not memory mapped (XIP). For example on PowerPC e500v2 where external NAND flash is used for boot only a small 4KB region is available, so wolfBoot must be loaded to RAM and then run.

Example: `make WOLFBOOT_STAGE1_LOAD_ADDR=0x1000 stage1`

* `WOLFBOOT_STAGE1_SIZE`: Maximum size of wolfBoot stage 1 loader
* `WOLFBOOT_STAGE1_FLASH_ADDR`: Location in Flash for stage 1 loader (XIP from boot ROM)
* `WOLFBOOT_STAGE1_BASE_ADDR`: Address in RAM to load stage 1 loader to
* `WOLFBOOT_STAGE1_LOAD_ADDR`: Address in RAM to load wolfBoot to
* `WOLFBOOT_LOAD_ADDRESS`: Address in RAM to load application partition


## update_ram.c

Implementation for RAM based updater

## update_flash.c

Implementation for Flash based updater

## update_flash_hwswap.c

Implementation for hardware assisted updater
