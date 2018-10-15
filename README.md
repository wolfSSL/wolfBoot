# wolfBoot
wolfSSL Secure Bootloader

wolfBoot is a portable, OS-agnostic, secure bootloader solution for 32-bit microcontrollers, 
relying on wolfCrypt for firmware authentication, and a modified version of 
[mcuboot](https://www.mcuboot.com/)'s *bootutil* library to implement firmware upgrade mechanisms.

Due to the minimalist design of the bootloader and the tiny HAL API, wolfBoot is completely independent
from any OS or bare-metal application, and can be easily ported and integrated in existing embedded software
projects to provide a secure firmware upgrade mechanism.


## Features
   - Multi-slot partitioning of the flash device
   - Integrity verification of the firmware image(s)
   - Authenticity verification of the firmware image(s) using wolfCrypt's Digital Signature Algorithms (DSA)
   - Minimalist hardware abstraction layer (HAL) interface to facilitate portability across different vendors/MCUs
   - In-place chain-loading of the firmware image in the primary slot
   - Copy/swap images from secondary slots into the primary slots to consent firmware upgrade operations

## Components

This repository contains the following components:
   - the bootloader
   - Ed25519 key generator and image signing tools
   - Baremetal test applications 

### The bootloader

The bootloader is a memory-safe standalone bare-metal application, designed to run on a generic 32bit MCU,
with no dynamic memory allocation mechanism or linkage to any standard C library. 

The core application depends on the following libraries:
   - wolfCrypt, which is used to verify the Ed25519 signature of the images
   - A modified version of mcuboot's bootutil, to handle the firmware image slots and the upgrade state-machine
   - A minimalist Hardware Abstraction Layer, with an implementation provided for the supported target, which is in charge for IAP flash access and clock setting on the specific MCU

The goal of this application is to perform  image verification and/or requested firmware upgrade tasks 
before chain-loading the actual firmware from a specific location in flash.

Only ARM Cortex-M is supported at this stage. Support for more architectures and
microcontrollers will be added later.

## Integrating wolfBoot in an existing project

Requirements:

   - Provide a HAL implementation for the target platform (see [Hardware Abstraction Layer](docs/HAL.md))
   - Decide a flash partition strategy and modify `include/target.h` accordingly (see [Flash partitions](docs/flash_partitions.md))

The following steps are automated in the default `Makefile` target, using the baremetal test
application as an example to create the factory image:

   - Create a Ed25519 Key-pair using the `ed25519_keygen` tool
   - Compile the bootloader. The public key generated in the step above is included in the build
   - Compile the firmware image
   - Re-link the firmware to change the entry-point to the start address of the primary partition
   - Sign the firmware image using the `ed25519_sign` tool
   - Create a factory image by concatenating the bootloader and the firmware image
   - Flash the factory image to the target

For more detailed information about the firmware image format, see [Firmware image](docs/firmware_image.md)

## Upgrading the firmware

   - Compile the new firmware image, and link it so that its entry point is at the start address of the primary partition
   - Sign the firmware using the `ed25519_sign` tool and the private key generated for the factory image
   - Transfer the image using a secure connection, and store it to the secondary firmware slot
   - Trigger the image swap using bootutil's `boot_set_pending()` function
   - Reboot to let the bootloader begin the image swap

For more detailed information about firmware upgrade procedures, see [Firmware Upgrade](docs/firmware_upgrade.md)

