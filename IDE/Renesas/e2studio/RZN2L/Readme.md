# wolfBoot for Renesas RZN2L

## 1. Overview

This example demonstrates simple secure firmware boot from extarnal flash by wolfBoot.
A sample application v1 is securely loaded into internal RAM if there is not higher version in update region. A sample application v2 will be loaded when it is in update region.Both versions behave the same except blinking LED Red(v1) or Yello(v2). They are compiled by e2Studio and running on the target board.

In this demo, you may download two versions of the application binary file.
You can download and execute wolfBoot by e2Studio debugger. Use a USB connection between PC and the board for the debugger and flash programmer.

## 2. Components, Tools and Board Settings

### 2-1. Tools
|Item|Name/Version|Note|
|:--|:--|:--|
|Board|Renesas RZN2L RSK||
|Device|R9A07G084M04GBG||
|Toolchain|GCC ARM Embedded 10.3.1.20210824|Included in GCC for Renesas RZ|
|FSP Version|1.3.0|Download from Renesas site|
|IDE|e2studio 2024-01.1 (24.1.1)|Download from Renesas site|
|SEGGER J-Link|J-Link Commander V7.94j |Download from J-Link|
|Key tool|keygen and sign|Included in wolfBoot|


|FIT Components|Version|
|:--|:--|
|Board Support Package Common Files|v1.3.0|
|I/O Port|v1.3.0|
|Arm CMSIS Version 5 - Core (M)|v5.7.0+renesas.1|
|Board support package for R9A07G084M04GBG|v1.3.0|
|Board support package for RZN2L|v1.3.0|
|Board support package for RZN2L - FSP Data|v1.3.0|
|RSK+RZN2L Board Support Files (xSPI0 x1 boot mode)|v1.3.0|
|SDRAM on Bus State Controller|v1.3.0|


### 2-2. Project folders
e2Studio Project:\
wolfBoot      IDE/Renesas/e2studio/RZN2L/wolfBoot\
Sample app    IDE/Renesas/e2studio/RZN2L/app_RZ\
Flash Simple Loader IDE/Renesas/e2studio/RZN2L/flash_app


### 2-3. Board Settings
The switch and jumber settings required to run the sample program from external flash are shown below. For details on each setting, see the Renesas Starter Kit+ for RZN2L User's Manual.

|Project|SW4-1|SW4-2|SW4-3|SW4-4|SW4-7|
|:--|:--|:--|:--|:--|:--|
|xSPI0 boot mode|ON|ON|ON|ON|OFF|

|Project|CN8|CN24|
|:--|:--|:--|
|xSPI0 boot mode|Short 2-3|Short2-3|

## 3. Operation Overview
On this boot mode, the loader program, which is wolfBoot, is copied to the internal RAM(B-TCM). wolfBoot copies the application program from external flash memory to RAM(System RAM) if its integrity and authenticity are OK. As final step of wolfBoot the entry point of the copied applicatin program is called.

![Operation Overview](./doc/image1.png)

## 4. How to build and use
This section describes about how to build wolfBoot and application and use them.

### 4-1) Key generation
It has key tools running under the host environment such as Linux, Windows or MacOS.
For compiling the tools, follow the instruction described in the user manual.


```
$ cd <wolfBoot>
$ export PATH=$PATH:<wolfBoot>/tools/keytools
$ keygen --ecc256 -g ./pri-ecc256.der    # ECC256
$ keygen --rsa2048 -g ./pri-rsa2048.der  # RSA2048
```

The `keygen` tool generates a pair of private and public key with -g option.
The private key is stored in the specified file.
The public key is stored in a key store as a C source code in "src/keystore.c" so that it can be compiled and linked with wolfBoot.
If you have an existing key pair, you can use -i option to import the public key to the store.

You can specify various signature algorithms such as

```les
--ed25519 --ed448 --ecc256 --ecc384 --ecc521 --rsa2048 --rsa3072 --rsa4096
```

### 5) Compile wolfBoot

Open project under IDE/Renesas/e2studio/RZN2L/wolfBoot with e2Studio, and build the project.

#### 5-1) Create `dummy_loader` application
+ Click File->New->`RZ/N C/C++ FSP Project`.
+ Select `RSK+RZN2L (xSPI0 x1 boot mode)` from Drop-down list.
+ Check `Executable`.
+ Select `No RTOS` from RTOS selection. Click Next.
+ Check `Bare Metal Minimal`. Click Finish.
+ Open Smart Configurator by clicking configuration.xml in the project
+ Go to `BSP` tab and increase LDR_SIZE_NML under `RSK+RZN2L(xSIP0x1 boot mode)` on Properties page, e.g. 0x00009000
+ Go to `BSP` tab and increase SVC Stack Size under `RZN2L stack size` on Properties page, e.g. 0x2000
+ Go to `BSP` tab and increase Heap Size under `RZN2L` on Properties page, e.g. 0x10000

+ Save `dummy_loader` FSP configuration
+ Copy <u>configuration.xml</u> and pincfg under `dummy_loader` to `wolfBoot`
+ Open Smart Configurator by clicking copied configuration.xml
+ Click `Generate Project Content` on Smart Configurator
+ Righ click on the project and Open property of the project
+ Go to Cross ARM Lincer
+ Change Script files(-T) from `fsp_xspi0_boot.ld` to `fsp_xspi0_boot_loader.ld`
+ Add/Modify FSP generated code :
+ fsp/src/bsp/cmsis/Device/RENESAS/Source/startup.c

ORIGINAL
```
BSP_TARGET_ARM BSP_ATTRIBUTE_STACKLESS void __Vectors (void)
{
    __asm volatile (
        "    ldr pc,=Reset_Handler            \n"
```
==>

MODIFIED
```
BSP_TARGET_ARM BSP_ATTRIBUTE_STACKLESS void __Vectors (void)
{
    /* This software loops are only needed when debugging. */
    __asm volatile (
        "    mov   r0, #0                         \n"
        "    movw  r1, #0xf07f                    \n"
        "    movt  r1, #0x2fa                     \n"
        "software_loop:                           \n"
        "    adds  r0, #1                         \n"
        "    cmp   r0, r1                         \n"
        "    bne   software_loop                  \n"
        ::: "memory");
    __asm volatile (
#if 0
        "    ldr pc,=Reset_Handler            \n"
#else
        "    ldr pc,=system_init              \n"
#endif
```

ORIGINAL
```
BSP_TARGET_ARM void mpu_cache_init (void)
{
...
#if BSP_CFG_C_RUNTIME_INIT

    /* Copy the loader data from external Flash to internal RAM. */
    bsp_loader_data_init();
...
#if !(BSP_CFG_RAM_EXECUTION)

    /* Copy the application program from external Flash to internal RAM. */
    bsp_copy_to_ram();
...
}
```

==>

MODIFIED
```
BSP_TARGET_ARM void mpu_cache_init (void)
{
...
if BSP_CFG_C_RUNTIME_INIT && !defined(EXTERNAL_LOADER)

    /* Copy the loader data from external Flash to internal RAM. */
    bsp_loader_data_init();
....

#if !(BSP_CFG_RAM_EXECUTION) && !defined(EXTERNAL_LOADER)

    /* Copy the application program from external Flash to internal RAM. */
    bsp_copy_to_ram();
...
}
```


+ Build `wolfBoot` project
### 6) Compile the sample application

Open project under IDE/Renesas/e2studio/RZN2L/app_RZ with e2Studio, and build the project.

 #### 6-1). Create `dummy_application`
+ Click File->New->`RZ/N C/C++ FSP Project`.
+ Select `RSK+RZN2L (xSPI0 x1 boot mode)` from Drop-down list.
+ Check `Executable`.
+ Select `No RTOS` from RTOS selection. Click Next.
+ Check `Bare Metal Minimal`. Click Finish.
+ Open Smart Configurator by clicking configuration.xml in the project
+ Open Interrupts tab
+ Select `INTCPU0` interrupts from `New User Event` -> `ICU`
+ Enter `intcpu0_handler` as interruption name

+ Save `dummy_application` FSP configuration
+ Copy <u>configuration.xml</u> and pincfg under `dummy_application` to `app_RZ`
+ Open Smart Configurator by clicking copied configuration.xml
+ Click `Generate Project Content` on Smart Configurator
+ Righ click on the project and Open property of the project
+ Go to Cross ARM Lincer
+ Change Script files(-T) from `fsp_xspi0_boot.ld` to `fsp_xspi0_boot_app.ld`
+ Add/Modify FSP generated code :
+ fsp/src/bsp/cmsis/Device/RENESAS/Source/startup.c

ORIGINAL
```
BSP_TARGET_ARM BSP_ATTRIBUTE_STACKLESS void __Vectors (void)
{
    __asm volatile (
```

==>

MODIFIED
```
BSP_TARGET_ARM BSP_ATTRIBUTE_STACKLESS void __Vectors (void)
{
    __asm volatile (
#if 0
        "    ldr pc,=Reset_Handler            \n"
#else
        "    ldr pc,=local_system_init        \n"
#endif
```


ORIGINAL
```
BSP_TARGET_ARM void mpu_cache_init (void)
{
...
#if BSP_CFG_C_RUNTIME_INIT

    /* Copy the loader data from external Flash to internal RAM. */
    bsp_loader_data_init();
...
#if !(BSP_CFG_RAM_EXECUTION)

    /* Copy the application program from external Flash to internal RAM. */
    bsp_copy_to_ram();
...
}
```

==>

MODIFIED
```
BSP_TARGET_ARM void mpu_cache_init (void)
{
...
if BSP_CFG_C_RUNTIME_INIT && !defined(EXTERNAL_LOADER_APP)

    /* Copy the loader data from external Flash to internal RAM. */
    bsp_loader_data_init();
....

#if !(BSP_CFG_RAM_EXECUTION) && !defined(EXTERNAL_LOADER_APP)

    /* Copy the application program from external Flash to internal RAM. */
    bsp_copy_to_ram();
...
}
```

+ Build `app_RZ` project

Code Origin and entry point is "0x10010000". app_RZ.bin is generated under Debug.


### 7) Generate Signature for app V1

The sign tool (`tools/keytools/sign`) generates a signature for the binary with a specified version.
It generates a file contain a partition header and application image.
The partition header contain generated signature and other control fields.
Output file name is made up from the input file name and version like app_RenesasRx01_v1.0_signed.bin.

```
# export PATH=$PATH:/path/to/wolfBoot-root/tools/keytools
$ cd /IDE/Renesas/e2studio/RZN2L/app_RZ/Debug/
$ sign --rsa2048 app_RZ.bin ../../../../../../pri-rsa2048.der 1.0
wolfBoot KeyTools (Compiled C version)
wolfBoot version 2000000
Update type:          Firmware
Input image:          app_RZ.bin
Selected cipher:      RSA2048
Selected hash  :      SHA256
Public key:           ../../../../../../pri-rsa2048.der
Output  image:        app_RZ_v1.0_signed.bin
Target partition id : 1
image header size calculated at runtime (512 bytes)
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
```

### 8) Download the app V1

To download the app V1 to external flash, you can use `flash_simple_loader` application which is located in /IDE/Renesas/e2studio/RZN2L/flash_app

Open project under IDE/Renesas/e2studio/RZN2L/flash_app with e2Studio, and build the project.

+ Copy <u>configuration.xml</u> and pincfg under `dummy_application` to `flash_simple_loader`
+ Open Smart Configurator by clicking configuration.xml in the project
+ Open Interrupts tab
+ Select `INTCPU0` interrupts and remove it
+ Click `Generate Project Content` on Smart Configurator
+ Go to `BSP` tab and disable C Runtime Initialization under `RZN2L` on Properties page
+ Righ click on the project and Open property of the project
+ Go to Cross ARM Lincer
+ Change Script files(-T) from `fsp_xspi0_boot.ld` to `fsp_xspi0_boot_loader.ld`
+ Build `flash_simple_loader` project

To run the application,

+ Right-Click the Project name.
+ Select `Debug As` -> `Renesas GDB Hardware Debugging`
+ Select `J-Link ARM`. Click OK.
+ Select `R9A07G084M04`. Click OK.

This simple application jsut downloads binaly files defiend in `Flash_section.s` and `Flash_updaet.s` through J-Link Flash Downloader. `Flash_update.s` doesn't includes `the app v2` initially.

Flash_update.s

```
/* To download the app v2, please remove comment out.  */
/* .incbin "../../app_RZ/Debug/app_RZ_v2.0_signed.bin" */
```

### 9) Execute initial boot

Now, you can download and start wolfBoot program by e2Studio debugger.
After starting the program, you can see the partition information as follows.
If the boot program succeeds successfully and authenticity check then start application V1. To initially run `wolfBoot` project,
1.) Right-Click the Project name.
2.) Select `Debug As` -> `Renesas GDB Hardware Debugging`
3.) Select `J-Link ARM`. Click OK.
4.) Select `R9A07G084M04`. Click OK.

You can see RED LED blinking on the board.

### 10) Generate Signed app V2 and download it

Similar to V1, you can sign and generate a binary of V2. The update partition starts at "0x60180000".

```
$ sign --rsa2048 app_RA.bin ../../../../../pri-rsa2048.der 2.0
```

To download the app V2, please remove comment out line in `Flash_update.s` of `flash_simple_loader`
Flash_update.s

```
/* To download the app v2, please remove comment out.  */
.incbin "../../app_RZ/Debug/app_RZ_v2.0_signed.bin"
```

And then clean and build `flash_simple_loader`, run it


### 11) Re-boot and execute the app V2

The boot program compares version number of download images in external flash memory. It simply chooses hight version number from two images to run.
The boot program downloads the selected image from flash to System Ram.

You can see now YELLOW LED blinking.


### 12) Erase flash memory
If you want to erase flash memory for initialization, you can use J-Link Commander tool.

Launch J-Link Commander tool.

```
SEGGER J-Link Commander V7.94j (Compiled Feb 14 2024 15:37:25)
DLL version V7.94j, compiled Feb 14 2024 15:36:06

Connecting to J-Link via USB...O.K.
Firmware: J-Link OB-S124 compiled Dec 13 2023 14:39:54
Hardware version: V1.00
J-Link uptime (since boot): 0d 01h 14m 48s
S/N: 831910878
USB speed mode: Full speed (12 MBit/s)
VTref=3.300V


Type "connect" to establish a target connection, '?' for help
J-Link>connect
Please specify device / core. <Default>: R9A07G084M04
Type '?' for selection dialog
Device>R9A07G084M04GBG
Please specify target interface:
  J) JTAG (Default)
  S) SWD
  T) cJTAG
TIF>S
Specify target interface speed [kHz]. <Default>: 4000 kHz
Speed>
Device "R9A07G084M04" selected.
...
J-Link>exec EnableEraseAllFlashBanks
```
e.g. erase all flash
```
erase 0x60100000,0x603FFFFF
```

e.g. erase BOOT PARTITION

```
erase 0x60100000,0x60180000
```

e.g. erase UPDATE PARTITION

```
erase 0x601800000,0x601E0000
```

### 13. References

For board settings:

[RZN2L FSP Getting Started](https://www.renesas.com/us/en/document/qsg/rzt2rzn2-getting-started-flexible-software-package)

Example Programs:

[RZ/T2, RZ/N2 Group Device Setup Guide for Flash boot Sample program](https://www.renesas.com/jp/ja/document/scd/rzt2-and-rzn2-group-device-setup-guide-flash-boot-sample-program?language=ja&r=1622651)

[RZ/N2L Group Example of separating loader program and application programprojects](https://www.renesas.com/us/en/document/scd/rzn2l-group-example-separating-loader-program-and-application-program-projects?r=1622651)
