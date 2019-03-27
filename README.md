# wolfBoot
wolfSSL Secure Bootloader ([Home page](https://www.wolfssl.com/products/wolfboot/))

wolfBoot is a portable, OS-agnostic, secure bootloader solution for 32-bit microcontrollers, 
relying on wolfCrypt for firmware authentication, providing firmware update mechanisms.

Due to the minimalist design of the bootloader and the tiny HAL API, wolfBoot is completely independent
from any OS or bare-metal application, and can be easily ported and integrated in existing embedded software
projects to provide a secure firmware update mechanism.


## Features
   - Multi-slot partitioning of the flash device
   - Integrity verification of the firmware image(s)
   - Authenticity verification of the firmware image(s) using wolfCrypt's Digital Signature Algorithms (DSA)
   - Minimalist hardware abstraction layer (HAL) interface to facilitate portability across different vendors/MCUs
   - Copy/swap images from secondary slots into the primary slots to consent firmware update operations
   - In-place chain-loading of the firmware image in the primary slot

## Components

This repository contains the following components:
   - the wolfBoot bootloader
   - Ed25519 key generator and image signing tools
   - Ecc256 key generator and image signing tools
   - Baremetal test applications 

### wolfBoot bootloader

wolfBoot is a memory-safe standalone bare-metal application, designed to run on a generic microcontroller,
with no dynamic memory allocation mechanism or linkage to any standard C library besides wolfCrypt.

The bootloader consists of the following components:
   - wolfCrypt, which is used to verify the signature of the images
   - A minimalist Hardware Abstraction Layer, with an implementation provided for the supported target, which is in charge for IAP flash access and clock setting on the specific MCU
   - The core bootloader 
   - A small application library used by the application to interact with the bootloader [src/libwolfboot.c](src/libwolfboot.c)

Only ARM Cortex-M boot mechanism is supported at this stage. Support for more architectures and
microcontrollers will be added later. Relocating the interrupt vector can be disabled if needed.

## Integrating wolfBoot in an existing project

### Required steps

   - Provide a HAL implementation for the target platform (see [Hardware Abstraction Layer](docs/HAL.md))
   - Decide a flash partition strategy and modify `include/target.h` accordingly (see [Flash partitions](docs/flash_partitions.md))
   - Change the entry point of the firmware image to account for bootloader presence
   - Equip the application with the [wolfBoot library](docs/API.md) to interact with the bootloader
   - [Configure and compile](docs/compile.md) a bootable image with a single "make" command

### Examples provided

The following steps are automated in the default `Makefile` target, using the baremetal test
application as an example to create the factory image. By running `make`, the build system will:

   - Create a Ed25519 Key-pair using the `ed25519_keygen` tool
   - Compile the bootloader. The public key generated in the step above is included in the build
   - Compile the firmware image from the test application in [test\_app](test-app/)
   - Re-link the firmware to change the entry-point to the start address of the primary partition
   - Sign the firmware image using the `ed25519_sign` tool
   - Create a factory image by concatenating the bootloader and the firmware image

The factory image can be flashed to the target device. It contains the bootloader and the signed initial
firmware at the specified address on the flash.

The `ed25519_sign` tool transforms a bootable firmware image to comply with the firmware image format required by the bootloader.

For detailed information about the firmware image format, see [Firmware image](docs/firmware_image.md)

For detailed information about the configuration options for the target system, see [Compiling wolfBoot](docs/compile.md)

### Upgrading the firmware

   - Compile the new firmware image, and link it so that its entry point is at the start address of the primary partition
   - Sign the firmware using the `ed25519_sign` tool and the private key generated for the factory image
   - Transfer the image using a secure connection, and store it to the secondary firmware slot
   - Trigger the image swap using libwolfboot `wolfBoot_update()` function. See [wolfBoot library API](docs/API.md) for a description of the operation
   - Reboot to let the bootloader begin the image swap
   - Confirm the success of the update using libwolfboot `wolfBoot_success()` function. See [wolfBoot library API](docs/API.md) for a description of the operation

For more detailed information about firmware update implementation, see [Firmware Update](docs/firmware_update.md)

## Release Notes

### v1.0 (2018-12-04)
 * Initial release with fail-safe update, HAL support for STM32 and nRF52 

### V1.1 (2019-03-27)
 * Added support for ECC-256 DSA
 * Added support for external (e.g. SPI) flash for Update/swap
 * Anti-rollback protection via version number
 * Hardware support
    * Added compile options for Cortex-M0
    * new HAL: Atmel SamR21
    * new HAL: TI cc26x2
    * new HAL: NXP/Freescale Kinetis SDK
 * Improved sign/update tools compatibility (windows)
