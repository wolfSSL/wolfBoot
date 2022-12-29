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
   - Support of Trusted Platform Module(TPM)
   - Measured boot support, storing of the firmware image hash into a TPM Platform Configuration Register(PCR)

## Components

This repository contains the following components:
   - the wolfBoot bootloader
   - key generator and image signing tools (requires python 3.x and wolfcrypt-py https://github.com/wolfSSL/wolfcrypt-py)
   - Baremetal test applications

### wolfBoot bootloader

wolfBoot is a memory-safe standalone bare-metal application, designed to run on a generic microcontroller,
with no dynamic memory allocation mechanism or linkage to any standard C library besides wolfCrypt.

The bootloader consists of the following components:
   - wolfCrypt, which is used to verify the signature of the images
   - A minimalist Hardware Abstraction Layer, with an implementation provided for the supported target, which is in charge for IAP flash access and clock setting on the specific MCU
   - The core bootloader
   - A small application library used by the application to interact with the bootloader [src/libwolfboot.c](src/libwolfboot.c)

## Integrating wolfBoot in an existing project

### Required steps

   - See `docs/Targets.md` for reference implementation examples.
   - Provide a HAL implementation for the target platform (see [Hardware Abstraction Layer](docs/HAL.md))
   - Decide a flash partition strategy and modify `include/target.h` accordingly (see [Flash partitions](docs/flash_partitions.md))
   - Change the entry point of the firmware image to account for bootloader presence
   - Equip the application with the [wolfBoot library](docs/API.md) to interact with the bootloader
   - [Configure and compile](docs/compile.md) a bootable image with a single "make" command
   - For help signing firmware see [wolfBoot Signing](docs/Signing.md)
   - For enabling measured boot see [wolfBoot measured boot](docs/measured_boot.md)

### Examples provided

Additional examples available on our GitHub wolfBoot-examples repository [here](https://github.com/wolfSSL/wolfBoot-examples).

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

The `sign.py` tool transforms a bootable firmware image to comply with the firmware image format required by the bootloader.

For detailed information about the firmware image format, see [Firmware image](docs/firmware_image.md)

For detailed information about the configuration options for the target system, see [Compiling wolfBoot](docs/compile.md)

### Upgrading the firmware

   - Compile the new firmware image, and link it so that its entry point is at the start address of the primary partition
   - Sign the firmware using the `sign.py` tool and the private key generated for the factory image
   - Transfer the image using a secure connection, and store it to the secondary firmware slot
   - Trigger the image swap using libwolfboot `wolfBoot_update_trigger()` function. See [wolfBoot library API](docs/API.md) for a description of the operation
   - Reboot to let the bootloader begin the image swap
   - Confirm the success of the update using libwolfboot `wolfBoot_success()` function. See [wolfBoot library API](docs/API.md) for a description of the operation

For more detailed information about firmware update implementation, see [Firmware Update](docs/firmware_update.md)


### Additional features
   - [Remote external flash interface](docs/remote_flash.md)
   - [External encrypted partitions](docs/encrypted_partitions.md)

## Building

### Makefile

To build using the Makefile, create a `.config` file with your build specifications in the wolfBoot root directory. You can find a
number of examples that you can use inside [config/examples](config/examples). Then run `make keytools` to generate the signing
and key generation tools. If you have wolfCrypt-py installed and would like to use it, you can skip this step.

Documentation for the flash configuration options used in `.config` can be found in [docs/compile.md](docs/compile.md).

For example, to build using our provided `stm32h7.config`:

```
cp config/examples/stm32h7.config .config
make keytools
make
```

### CMake

To build using CMake, create a `build` directory and run `cmake` with the target platform as well as values for the partition
size and address variables. To build the test-apps, run with `-DBUILD_TEST_APPS=yes`. To use the wolfCrypt-py keytools, run
with `-DPYTHON_KEYTOOLS=yes`.

For example, to build for the stm32h7 platform:
```
$ mkdir build
$ cd build
$ cmake -DWOLFBOOT_TARGET=stm32h7 -DBUILD_TEST_APPS=yes -DWOLFBOOT_PARTITION_BOOT_ADDRESS=0x8020000 -DWOLFBOOT_SECTOR_SIZE=0x20000 -DWOLFBOOT_PARTITION_SIZE=0xD0000 -DWOLFBOOT_PARTITION_UPDATE_ADDRESS=0x80F0000 -DWOLFBOOT_PARTITION_SWAP_ADDRESS=0x81C0000 ..
$ make
```

The output should look something like:
```
Scanning dependencies of target keystore
[  2%] Building signing tool
[  4%] Building keygen tool
[  7%] Generating keystore.c and signing private key
Keytype: ECC256
Gen /home/user/wolfBoot/build/wolfboot_signing_private_key.der
Generating key (type: ECC256)
Associated key file:   /home/user/wolfBoot/build/wolfboot_signing_private_key.der
Key type   :           ECC256
Public key slot:       0
Done.
[  7%] Built target keystore
Scanning dependencies of target public_key
[  9%] Building C object CMakeFiles/public_key.dir/keystore.c.o
[ 11%] Linking C static library libpublic_key.a
[ 14%] Built target public_key
Scanning dependencies of target wolfboothal
[ 16%] Building C object CMakeFiles/wolfboothal.dir/hal/stm32h7.c.o
[ 19%] Linking C static library libwolfboothal.a
[ 19%] Built target wolfboothal
Scanning dependencies of target wolfcrypt
[ 21%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/integer.c.o
[ 23%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/tfm.c.o
[ 26%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/ecc.c.o
[ 28%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/memory.c.o
[ 30%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/wc_port.c.o
[ 33%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/wolfmath.c.o
[ 35%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/hash.c.o
[ 38%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/sha256.c.o
[ 40%] Linking C static library libwolfcrypt.a
[ 40%] Built target wolfcrypt
Scanning dependencies of target wolfboot
[ 42%] Building C object CMakeFiles/wolfboot.dir/src/libwolfboot.c.o
[ 45%] Linking C static library libwolfboot.a
[ 45%] Built target wolfboot
Scanning dependencies of target image
[ 47%] Building C object test-app/CMakeFiles/image.dir/app_stm32h7.c.o
[ 50%] Building C object test-app/CMakeFiles/image.dir/led.c.o
[ 52%] Building C object test-app/CMakeFiles/image.dir/system.c.o
[ 54%] Building C object test-app/CMakeFiles/image.dir/timer.c.o
[ 57%] Building C object test-app/CMakeFiles/image.dir/startup_arm.c.o
[ 59%] Linking C executable image
[ 59%] Built target image
Scanning dependencies of target image_signed
[ 61%] Generating image.bin
[ 64%] Signing  image
wolfBoot KeyTools (Compiled C version)
wolfBoot version 10C0000
Update type:          Firmware
Input image:          /home/user/wolfBoot/build/test-app/image.bin
Selected cipher:      ECC256
Selected hash  :      SHA256
Public key:           /home/user/wolfBoot/build/wolfboot_signing_private_key.der
Output  image:        /home/user/wolfBoot/build/test-app/image_v1_signed.bin
Target partition id : 1
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
[ 64%] Built target image_signed
Scanning dependencies of target image_outputs
[ 66%] Generating image.size
   text	   data	    bss	    dec	    hex	filename
   5284	    108	     44	   5436	   153c	/home/user/wolfBoot/build/test-app/image
[ 69%] Built target image_outputs
Scanning dependencies of target wolfboot_stm32h7
[ 71%] Building C object test-app/CMakeFiles/wolfboot_stm32h7.dir/__/src/string.c.o
[ 73%] Building C object test-app/CMakeFiles/wolfboot_stm32h7.dir/__/src/image.c.o
[ 76%] Building C object test-app/CMakeFiles/wolfboot_stm32h7.dir/__/src/loader.c.o
[ 78%] Building C object test-app/CMakeFiles/wolfboot_stm32h7.dir/__/src/boot_arm.c.o
[ 80%] Building C object test-app/CMakeFiles/wolfboot_stm32h7.dir/__/src/update_flash.c.o
[ 83%] Linking C executable wolfboot_stm32h7
[ 83%] Built target wolfboot_stm32h7
Scanning dependencies of target binAssemble
[ 85%] Generating bin-assemble tool
[ 85%] Built target binAssemble
Scanning dependencies of target image_boot
[ 88%] Generating wolfboot_stm32h7.bin
[ 90%] Signing  image
wolfBoot KeyTools (Compiled C version)
wolfBoot version 10C0000
Update type:          Firmware
Input image:          /home/user/wolfBoot/build/test-app/image.bin
Selected cipher:      ECC256
Selected hash  :      SHA256
Public key:           /home/user/wolfBoot/build/wolfboot_signing_private_key.der
Output  image:        /home/user/wolfBoot/build/test-app/image_v1_signed.bin
Target partition id : 1
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
[ 92%] Assembling image factory image
[ 95%] Built target image_boot
Scanning dependencies of target wolfboot_stm32h7_outputs
[ 97%] Generating wolfboot_stm32h7.size
   text	   data	    bss	    dec	    hex	filename
  42172	      0	     76	  42248	   a508	/home/user/wolfBoot/build/test-app/wolfboot_stm32h7
[100%] Built target wolfboot_stm32h7_outputs
```

Signing and hashing algorithms can be specified with `-DSIGN=<alg>` and `-DHASH=<alg>`. To view additional
options to configuring wolfBoot, add `-LAH` to your cmake command, along with the partition specifications.
```
$ cmake -DWOLFBOOT_TARGET=stm32h7 -DWOLFBOOT_PARTITION_BOOT_ADDRESS=0x8020000 -DWOLFBOOT_SECTOR_SIZE=0x20000 -DWOLFBOOT_PARTITION_SIZE=0xD0000 -DWOLFBOOT_PARTITION_UPDATE_ADDRESS=0x80F0000 -DWOLFBOOT_PARTITION_SWAP_ADDRESS=0x81C0000 -LAH ..
```

##### stm32f4
```
$ cmake -DWOLFBOOT_TARGET=stm32f4 -DWOLFBOOT_PARTITION_SIZE=0x20000 -DWOLFBOOT_SECTOR_SIZE=0x20000 -DWOLFBOOT_PARTITION_BOOT_ADDRESS=0x08020000 -DWOLFBOOT_PARTITION_UPDATE_ADDRESS=0x08040000 -DWOLFBOOT_PARTITION_SWAP_ADDRESS=0x08060000 ..
```

##### stm32u5
```
$ cmake -DWOLFBOOT_TARGET=stm32u5 -DBUILD_TEST_APPS=yes -DWOLFBOOT_PARTITION_BOOT_ADDRESS=0x08100000 -DWOLFBOOT_SECTOR_SIZE=0x2000 -DWOLFBOOT_PARTITION_SIZE=0x20000 -DWOLFBOOT_PARTITION_UPDATE_ADDRESS=0x817F000 -DWOLFBOOT_PARTITION_SWAP_ADDRESS=0x81FE000 -DNO_MPU=yes ..
```

##### stm32l0
```
$ cmake -DWOLFBOOT_TARGET=stm32l0 -DWOLFBOOT_PARTITION_BOOT_ADDRESS=0x8000 -DWOLFBOOT_SECTOR_SIZE=0x1000 -DWOLFBOOT_PARTITION_SIZE=0x10000 -DWOLFBOOT_PARTITION_UPDATE_ADDRESS=0x18000 -DWOLFBOOT_PARTITION_SWAP_ADDRESS=0x28000 -DNVM_FLASH_WRITEONCE=yes ..
```


## Troubleshooting

1. Python errors when signing a key:

```
Traceback (most recent call last):
  File "tools/keytools/keygen.py", line 135, in <module>
    rsa = ciphers.RsaPrivate.make_key(2048)
AttributeError: type object 'RsaPrivate' has no attribute 'make_key'
```

```
Traceback (most recent call last):
  File "tools/keytools/sign.py", line 189, in <module>
    r, s = ecc.sign_raw(digest)
AttributeError: 'EccPrivate' object has no attribute 'sign_raw'
```

You need to install the latest wolfcrypt-py here: https://github.com/wolfSSL/wolfcrypt-py

Use `pip3 install wolfcrypt`.

Or to install based on a local wolfSSL installation use:

```sh
cd wolfssl
./configure --enable-keygen --enable-rsa --enable-ecc --enable-ed25519 --enable-des3 CFLAGS="-DFP_MAX_BITS=8192 -DWOLFSSL_PUBLIC_MP"
make
sudo make install

cd wolfcrypt-py
USE_LOCAL_WOLFSSL=/usr/local pip3 install .
```

2. Key algorithm mismatch:

The error `Key algorithm mismatch. Remove old keys via 'make distclean'` indicates the current `.config` `SIGN` algorithm does not match what is in the generated `src/keystore.c` file.
Use `make keysclean` or `make distclean` to delete keys and regenerate.


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

### V1.2 (2019-07-30)
 * Added support for multiple architectures
 * key generation and signing tools rewritten in python for portability
 * Added compile-time option to move flash-writing functions to RAM
 * Introduced the possibility for the bootloader to update itself
 * Fixed compile issues on macOS and WSL
 * Hardware support
    * Added RV32 RISC-V architecture
    * Added hardware-assisted dual-bank support on STM32F76x/77x
    * new HAL: RV32 FE310 (SiFive HiFive-1)
    * new HAL: STM32L0
    * new HAL: STM32G0
    * new HAL: STM32F7

### V1.3 (2019-11-13)
 * New configuration mechanism based on `make config`, helps creating and storing target-specific configurations
    * Configuration examples provided for a number of existing platforms
 * fix bug in self-update mechanism when SPI flash is in use
 * Introduced support for hardware-assisted signature verification, using public-key hardware accelerators
    * Added support for STM32 PKA (e.g. STM32WB55)
    * Added support for Kinetis/Freescale PKHA (e.g. Kinetis K82F)

### V1.4 (2020-01-06)
 * TPM2.0 support
   * Integration with wolfTPM
   * Extended STM32 SPI driver to support dual TPM/FLASH communication
   * Tested on STM32 with Infineon 9670
 * RSA 2048 bit digital signature verification
 * Hardware support
   * New HAL: STM32H7

### V1.5 (2020-04-28)
 * RSA 4096 bit digital signature verification
 * SHA3
 * Portable C key management tools
 * Improved integration with Microsoft Windows
   * Visual Studio solution for key management tools
 * Support to compile with IAR
   * Fixed incompatible code
   * added IAR example project
 * New architecture: ARMv8 (64-bit)
   * ARM Cortex-A boot code compatible with TrustZone
   * Linux staging and device tree support
 * External flash abstraction
   * remote update partition accessed via UART
 * Hardware support
   * New HAL: raspberry-pi
   * New HAL: Xilinx Zynq+
   * New HAL: NXP LPC54xx

### V1.6 (2020-08-25)
 * Support for encryption of external partitions
 * Support for MPU on ARM Cortex-M platforms
 * Support for using an RSA signature that includes ASN.1 encoded header
 * Support for bootloader updates from external flash: SPI functions can run from RAM
 * Added TPM RSA verify support
 * Added option to use software SHA in combination with TPM
 * Fix logic in emergency updates
 * Fix loop logic in bootloader update
 * Fix manifest header boundary checks (prevents parser overflows)
 * Improve sanity checks for aligned fields in manifest header
 * Add unit tests against manifest header parser
 * Fix Ed25519 signing tool
 * Fix RSA keygen tool
 * wolfTPM integration: improvements and bugfixes
 * Fix configuration and documentation for STM32WB
 * Fix alignment of trailers in NVM_WRITEONCE mode
 * Fix uint16_t index overflow on platforms with very small flash pages
 * Fix for building C key tools on windows (Cygwin/MinGW/Visual Studio)
 * Fix in LPC driver: correct page alignment in flash write
 * Hardware support
   * New HAL: Cypress psoc6
   * Support for psoc6 Hardware crypto accelerator
   * SPI driver: Nordic nRF52

### V1.7.1 (2021-02-03)
 * Added support for measured boot via TPM
 * Support for TZEN on Cortex-m33
 * Added option to disable backup/fallback
 * Added option FLAGS_HOME to store UPDATE flags in the BOOT partition
 * Zynq: added support for eFuse
 * Zynq: improved debugging
 * Xilinx: support for BSP QSPI driver
 * Updated user documentation
 * Extend coverage of automatic non-regression tests running on Jenkins
 * Fix wolfTPM integration: use custom settings
 * Fix Fallback operations when encryption is enabled
 * Fix DUALBANK mode on STM32L5xx
 * Fix maximum image size check
 * Fix in STM32H7 driver: workaround for error correction in flash writing
 * Hardware support
   * New ARCH: ARMv8-m (Cortex-m33)
   * New HAL: STM32L5xx
   * New HAL: NXP iMX-RT1060
   * SPI driver: STM32L0x3
   * Uart driver: STM32L0x3

### V1.8 (2021-07-19)
 * Use SP math for RSA4096
 * Updated RSA to use inline operation and disable OAEP padding
 * Memory model: removed dependency on XMALLOC/XFREE for ECC and RSA operations
 * Added option WOLFBOOT_SMALL_STACK with hardcoded compile-time buffers
 * Added option SIGN=NONE to disable secure boot at compile time
 * Fix self-update documentation
 * Added test cases for configuration option combinations
 * Hardware support
   * New ARCH: PowerPC
   * New ARCH: ARM Cortex-R
   * New HAL: NXP T2080
   * New HAL: TI TMS570LC435
   * STM32H7: Correct BANK2 offset

### V1.9 (2021-11-09)
 * Delta/incremental updates
 * Fixes for key tools
 * Updates IAR IDE project
 * Documentation updates and fixes
   * API function names to match code
   * STM32L5 updates
 * Hardware support
   * New HAL: STM32L4
   * TMS570LC43xx: Use `NVM_FLASH_WRITEONCE` for update progress and
                   fix stack pointer initialization

### V1.10 (2022-01-10)
 * Delta updates: expanded documentation + bug fixes
 * Support Ed448 for signature verification
 * Hardware support:
   * Secure memory mode for STM32G0
   * Fix for STM32L5 in dual-bank mode
   * UEFI support: wolfBoot as EFI application on x86_64
   * Fixed self-update in Cortex-R5
   * Fixed HW support regressions in PSOC-6 build

### V1.11 (2022-04-05)
 * Mitigation against fault-injections and glitching attacks
    (https://www.wolfssl.com/secure-boot-glitching-attacks/)
 * Support AES128 and AES256 for update encryption
 * Support ECC384 signature verification
 * Support SHA2-384 for image hash
 * Fixed alignment of delta update fields in manifest
 * Image size propagated to sign tools
 * Added test automation based on renode.io and github actions
 * Hardware support
   * New HAL: STM32U5
   * New HAL: NXP i.MX-RT1050
   * Fix risc-V 32bit port (missing include)
   * Fix STM32L4 (VTOR alignments; clock setting clash in libwolfboot)
   * STM32H7: improve HAL and documentation

### V1.12 (2022-07-26)
 * Encrypted delta updates
 * Support RSA3072 signature verification
 * Partition ID support to include custom additional images
 * New format to store multiple public keys, using keystore
 * Several fixes to keytools and IDE support
 * Added new test cases
 * Hardware support
   * New HAL: Simulated target for rapid tests

### V1.13 (2022-11-08)
 * Fixed IAR sign script
 * Added support for encrypted self-update
 * Support for NAII 68PPC2 with NXP T2080 on DEOS
 * Fixed Xilinx QSPI support
 * Fixed API usage in external flash support for SPI/UART
 * Fixed bug in encrypted delta updates
 * Updated wolfCrypt to wolfSSL submodule v5.5.3

