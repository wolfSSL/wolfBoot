# Targets

This README describes configuration of supported targets.

## STM32-F407

Example 512KB partitioning on STM32-F407

The example firmware provided in the `test-app` is configured to boot from the primary partition
starting at address 0x20000. The flash layout is provided by the default example using the following
configuration in `target.h`:

```C
#define WOLFBOOT_SECTOR_SIZE              0x20000
#define WOLFBOOT_PARTITION_SIZE           0x20000

#define WOLFBOOT_PARTITION_BOOT_ADDRESS   0x20000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS 0x40000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS   0x60000
```

This results in the following partition configuration:

![example partitions](png/example_partitions.png)

This configuration demonstrates one of the possible layouts, with the slots
aligned to the beginning of the physical sector on the flash.

The entry point for all the runnable firmware images on this target will be `0x20100`,
256 Bytes after the beginning of the first flash partition. This is due to the presence
of the firmware image header at the beginning of the partition, as explained more in details
in [Firmware image](firmware_image.md)

In this particular case, due to the flash geometry, the swap space must be as big as 64KB, to account for proper sector swapping between the two images.

On other systems, the SWAP space can be as small as 512B, if multiple smaller flash blocks are used.

More information about the geometry of the flash and in-application programming (IAP) can be found in the manufacturer manual of each target device.


## STM32L0x3

Example 192KB partitioning on STM32-L073

This device is capable of erasing single flash pages (256B each).

However, we choose to use a logic sector size of 4KB for the swaps, to limit the amount of
writes to the swap partition.

The proposed geometry in this example `target.h` uses 32KB for wolfBoot, and two
partitions of 64KB each, leaving room for up to 8KB to use for swap (4K are being used here).

```C
#define WOLFBOOT_SECTOR_SIZE                 0x1000   /* 4 KB */
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x8000
#define WOLFBOOT_PARTITION_SIZE              0x10000 /* 64 KB */
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x18000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x28000
```

### Building

Use `make TARGET=stm32l0`. The option `CORTEX_M0` is automatically selected for this target.

## STM32G0x0/STM32G0x1

Example 128KB partitioning on STM32-G070:

- Sector size: 2KB
- Wolfboot partition size: 32KB
- Application partition size: 45 KB

```C
#define WOLFBOOT_SECTOR_SIZE                 0x800   /* 2 KB */
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x8000
#define WOLFBOOT_PARTITION_SIZE              0xB000 /* 45 KB */
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x13000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x1E000
```

### Building

Use `make TARGET=stm32g0`. The option `CORTEX_M0` is automatically selected for this target.
The option `NVM_FLASH_WRITEONCE=1` is mandatory on this target, since the IAP driver does not support
multiple writes after each erase operation.

Compile with:

`make TARGET=stm32g0 NVM_FLASH_WRITEONCE=1`


## STM32WB55

Example partitioning on Nucleo-68 board:

- Sector size: 4KB
- Wolfboot partition size: 32 KB
- Application partition size: 128 KB

```C
#define WOLFBOOT_SECTOR_SIZE                 0x1000   /* 4 KB */
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x8000
#define WOLFBOOT_PARTITION_SIZE              0x20000 /* 128 KB */
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x28000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x48000
```

### Building

Use `make TARGET=stm32wb`.

The option `NVM_FLASH_WRITEONCE=1` is mandatory on this target, since the IAP driver does not support
multiple writes after each erase operation.

Compile with:

`make TARGET=stm32wb NVM_FLASH_WRITEONCE=1`

### Loading the firmware

`openocd --file ./config/openocd/openocd_stm32wbx.cfg`

```
telnet localhost 4444
flash write_image unlock erase wolfboot.bin 0x08000000
flash verify_bank 0 wolfboot.bin
flash write_image unlock erase test-app/image_v1_signed.bin 0x080008000
flash verify_bank 0 test-app/image_v1_signed.bin 0x080008000
reset
```

### Debugging

Use `make DEBUG=1` and reload firmware.

```
arm-none-eabi-gdb wolfboot.elf -ex "set remotetimeout 240" -ex "target extended-remote localhost:3333"
(gdb) add-symbol-file test-app/image.elf 0x8000
(gdb) add-symbol-file wolfboot.elf 0x0
```


## SiFive HiFive1 RISC-V

### Features
* E31 RISC-V 320MHz 32-bit processor
* Onboard 16KB scratchpad RAM
* External 4MB QSPI Flash

### Default Linker Settings
* FLASH: Address 0x20000000, Len 0x6a120 (424 KB)
* RAM:   Address 0x80000000, Len 0x4000  (16 KB)

### Stock bootloader
Start Address: 0x20000000 is 64KB. Provides a "double tap" reset feature to halt boot and allow debugger to attach for reprogramming. Press reset button, when green light comes on press reset button again, then board will flash red.

### Application Code
Start Address: 0x20010000

### wolfBoot configuration

The default wolfBoot configuration will add a second stage bootloader, leaving the stock "double tap" bootloader as a fallback for recovery. Your production implementation should replace this and partition addresses in `target.h` will need updated, so they are `0x10000` less.

For testing wolfBoot here are the changes required:

1. Makefile arguments:
    * ARCH=RISCV
    * TARGET=hifive1

    ```
    make ARCH=RISCV TARGET=hifive1 RAM_CODE=1 clean
    make ARCH=RISCV TARGET=hifive1 RAM_CODE=1
    ```

    If using the `riscv64-unknown-elf-` cross compiler you can add `CROSS_COMPILE=riscv64-unknown-elf-` to your `make` or modify `arch.mk` as follows:

    ```
     ifeq ($(ARCH),RISCV)
    -  CROSS_COMPILE:=riscv32-unknown-elf-
    +  CROSS_COMPILE:=riscv64-unknown-elf-
    ```


2. `include/target.h`

Bootloader Size: 0x10000 (64KB)
Application Size 0x40000 (256KB)
Swap Sector Size: 0x1000 (4KB)

```c
#define WOLFBOOT_SECTOR_SIZE                 0x1000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x20020000

#define WOLFBOOT_PARTITION_SIZE              0x40000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x20060000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x200A0000
```

### Build Options

* To use ECC instead of ED25519 use make argument `SIGN=ECC256`
* To output wolfboot as hex for loading with JLink use make argument `wolfboot.hex`

### Loading

Loading with JLink:

```
JLinkExe -device FE310 -if JTAG -speed 4000 -jtagconf -1,-1 -autoconnect 1
loadbin factory.bin 0x20010000
rnh
```

### Debugging

Debugging with JLink:

In one terminal:
`JLinkGDBServer -device FE310 -port 3333`

In another terminal:
```
riscv64-unknown-elf-gdb wolfboot.elf -ex "set remotetimeout 240" -ex "target extended-remote localhost:3333"
add-symbol-file test-app/image.elf 0x20020100
```


## STM32-F769

The STM32-F76x and F77x offer dual-bank hardware-assisted swapping.
The flash geometry must be defined beforehand, and wolfBoot can be compiled to use hardware
assisted bank-swapping to perform updates.


Example 2MB partitioning on STM32-F769:

- Dual-bank configuration

BANK A: 0x08000000 to 0x080FFFFFF (1MB)
BANK B: 0x08100000 to 0x081FFFFFF (1MB)

- WolfBoot executes from BANK A after reboot (address: 0x08000000)
- Boot partition @ BANK A + 0x20000 = 0x08020000
- Update partition @ BANK B + 0x20000 = 0x08120000
- Application entry point: 0x08020100

```C
#define WOLFBOOT_SECTOR_SIZE              0x20000
#define WOLFBOOT_PARTITION_SIZE           0x40000

#define WOLFBOOT_PARTITION_BOOT_ADDRESS   0x08020000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS 0x08120000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS   0x0   /* Unused, swap is hw-assisted */
```

### Build Options

To activate the dual-bank hardware-assisted swap feature on STM32F76x/77x, use the
`DUALBANK_SWAP=1` compile time option. Some code requires to run in RAM during the swapping
of the images, so the compile-time option `RAMCODE=1` is also required in this case.

Dual-bank STM32F7 build can be built using:

```
make TARGET=stm32f7 DUALBANK_SWAP=1 RAM_CODE=1
```


### Loading the firmware

To switch between single-bank (1x2MB) and dual-bank (2 x 1MB) mode mapping, this [stm32f7-dualbank-tool](https://github.com/danielinux/stm32f7-dualbank-tool)
can be used.
Before starting openocd, switch the flash mode to dualbank (e.g. via `make dualbank` using the dualbank tool).

OpenOCD configuration for flashing/debugging, can be copied into `openocd.cfg` in your working directory:

```
source [find interface/stlink.cfg]
source [find board/stm32f7discovery.cfg]
$_TARGETNAME configure -event reset-init {
    mmw 0xe0042004 0x7 0x0
}
init
reset
halt
```

OpenOCD can be either run in background (to allow remote GDB and monitor terminal connections), or
directly from command line, to execute terminal scripts.

If OpenOCD is running, local TCP port 4444 can be used to access an interactive terminal prompt. `telnet localhost 4444`

Using the following openocd commands, the initial images for wolfBoot and the test application
are loaded to flash in bank 0:

```
flash write_image unlock erase wolfboot.bin 0x08000000
flash verify_bank 0 wolfboot.bin
flash write_image unlock erase test-app/image_v1_signed.bin 0x08020000
flash verify_bank 0 test-app/image_v1_signed.bin 0x20000
reset
resume 0x0000001
```

To sign the same application image as new version (2), use the python script `sign.py` provided:

```
tools/keytools/sign.py test-app/image.bin ed25519.der 2
```

From OpenOCD, the updated image (version 2) can be flashed to the second bank:
```
flash write_image unlock erase test-app/image_v2_signed.bin 0x08120000
flash verify_bank 0 test-app/image_v1_signed.bin 0x20000
```

Upon reboot, wolfboot will elect the best candidate (version 2 in this case) and authenticate the image.
If the accepted candidate image resides on BANK B (like in this case), wolfBoot will perform one bank swap before
booting.

The bank-swap operation is immediate and a SWAP image is not required  in this case. Fallback mechanism can rely on
a second choice (older firmware) in the other bank.


### Debugging

Debugging with OpenOCD:

Use the OpenOCD configuration from the previous section to run OpenOCD.

From another console, connect using gdb, e.g.:

```
arm-none-eabi-gdb
(gdb) target remote:3333
```


## STM32H7

The STM32H7 flash geometry must be defined beforehand.

Use the "make config" operation to generate a .config file or copy the template
using `cp ./config/examples/stm32h7.config .config`.

Example 2MB partitioning on STM32-H753:

```
WOLFBOOT_SECTOR_SIZE?=0x20000
WOLFBOOT_PARTITION_SIZE?=0xD0000
WOLFBOOT_PARTITION_BOOT_ADDRESS?=0x8020000
WOLFBOOT_PARTITION_UPDATE_ADDRESS?=0x80F0000
WOLFBOOT_PARTITION_SWAP_ADDRESS?=0x81C0000
```

### Build Options

The STM32H7 build can be built using:

```
make TARGET=stm32h7 SIGN=ECC256
```

### Loading the firmware

OpenOCD configuration for flashing/debugging, can be copied into `openocd.cfg` in your working directory:
Note: May require OpenOCD 0.10.0 or greater for the STM32H7x support.

```
source [find interface/stlink.cfg]
source [find target/stm32h7x.cfg]
$_CHIPNAME.cpu0 configure -event reset-init {
    mmw 0xe0042004 0x7 0x0
}
init
reset
halt
```

`openocd --file openocd.cfg`

OpenOCD can be either run in background (to allow remote GDB and monitor terminal connections), or
directly from command line, to execute terminal scripts.

If OpenOCD is running, local TCP port 4444 can be used to access an interactive terminal prompt.

Using the following openocd commands, the initial images for wolfBoot and the test application
are loaded to flash in bank 0:

```
telnet localhost 4444
flash write_image unlock erase wolfboot.bin 0x08000000
flash verify_bank 0 wolfboot.bin
flash write_image unlock erase test-app/image_v1_signed.bin 0x08020000
flash verify_bank 0 test-app/image_v1_signed.bin 0x08020000
reset
```

To sign the same application image as new version (2), use the python script `sign.py` provided:

```
tools/keytools/sign.py test-app/image.bin ecc256.der 2
```

From OpenOCD, the updated image (version 2) can be flashed to the second bank:
```
flash write_image unlock erase test-app/image_v2_signed.bin 0x08120000
flash verify_bank 0 test-app/image_v1_signed.bin 0x20000
```

Upon reboot, wolfboot will elect the best candidate (version 2 in this case) and authenticate the image.
If the accepted candidate image resides on BANK B (like in this case), wolfBoot will perform one bank swap before
booting.


## LPC54606

### Build Options

The LPC54xxx build can be obtained by specifying the CPU type and the MCUXpresso SDK path at compile time.

The following configuration has been tested against LPC54606J512BD208:

```
make TARGET=lpc SIGN=ECC256 MCUXPRESSO?=/path/to/LPC54606J512/SDK
    MCUXPRESSO_CPU?=LPC54606J512BD208 \
    MCUXPRESSO_DRIVERS?=$(MCUXPRESSO)/devices/LPC54606 \
    MCUXPRESSO_CMSIS?=$(MCUXPRESSO)/CMSIS
```

### Loading the firmware

Loading with JLink (example: LPC54606J512)

```
JLinkExe -device LPC606J512 -if SWD -speed 4000
erase
loadbin factory.bin 0
r
h
```

### Debugging

Debugging with JLink:
```
JLinkGDBServer -device LPC606J512 -if SWD -speed 4000 -port 3333
```

Then, from another console:

```
arm-none-eabi-gdb wolfboot.elf -ex "target remote localhost:3333"
(gdb) add-symbol-file test-app/image.elf 0x0000a100
```
