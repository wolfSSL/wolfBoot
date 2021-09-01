# TMS750LC43xx

# Build

## Build from command line (msys, cygwin, etc)

```
make CCS_ROOT=/c/ti/ccs1031/ccs/tools/compiler/ti-cgt-arm_20.2.4.LTS F021_DIR=/c/ti/Hercules/F021\ Flash\ API/02.01.01
```

## Build using Code Composer Studio (CCS)

Create key or copy if you already have one.

### Create key

```
make -C ./tools/keytools
./tools/keytools/keygen.exe --ecc256 src/ecc256_pub_key.c
```

Use IDE to build and load wolfBoot bootloader

# Running minimal example

## Full flash

[uniflash](https://www.ti.com/tool/UNIFLASH#downloads) can be used to flash the binary from command line.

```
c:\ti\ccs1031\ccs\ccs_base\scripting\examples\uniflash\cmdLine\uniflash.bat -ccxml "flashHercules.ccxml" -programBin "factory.bin" 0
```

```
***** Texas Instruments Universal Flash Programmer *****

<START: 12:45:03 GMT-0700 (PDT)>

> Configuring the Flash Programmer with the given configuration ...

Loaded FPGA Image: C:\ti\ccs1031\ccs\ccs_base\common\uscif\dtc_top.jbc
> Flash Manager is configured for the following part: TMS570LC43xx

> Connecting to the target for Flash operations ...

CortexR5: GEL Output:   Memory Map Setup for Flash @ Address 0x0
> Connected.

> Loading Binary file: factory.bin
CortexR5: GEL Output:   Memory Map Setup for Flash @ Address 0x0 due to System Reset

CortexR5: Writing Flash @ Address 0x00000000 of Length 0x00007ff0

...
```


### Only erase necessary sectors

```
c:\ti\ccs1031\ccs\ccs_base\scripting\examples\uniflash\cmdLine\uniflash.bat -ccxml "IDE\CCS\TMS570LC43xx\flashHercules.ccxml"  -setOptions FlashEraseSelection="Necessary Sectors Only (for Program Load)" -programBin factory.bin 0
```

### Just update image

```
c:\ti\ccs1031\ccs\ccs_base\scripting\examples\uniflash\cmdLine\uniflash.bat -ccxml "IDE\CCS\TMS570LC43xx\flashHercules.ccxml"  -setOptions FlashEraseSelection="Necessary Sectors Only (for Program Load)" -programBin test-app/image_v1_signed.bin 0x20000
```

[dss reference](http://software-dl.ti.com/ccs/esd/documents/users_guide/sdto_dss_handbook.html)

# Running other program

In order to load a program built

## Application Memory Map

Using Code Composer Studio (CCS) you will need to manually update the
`cmd` file. The VECTORS must be at `WOLFBOOT_PARTITION_BOOT_ADDRESS +
IMAGE_HEADER_SIZE` and all flash sections (VECTORS, KERNEL, FLASH*)
must end before `WOLFBOOT_PARTITION_UPDATE_ADDRESS`

```
MEMORY
{
    VECTORS (X)  : origin=0x00020100 length=0x00000020
    KERNEL  (RX) : origin=0x00020120 length=0x00007ee0
    FLASH0  (RX) : origin=0x00028000 length=(0x100000-0x28000)
    STACKS  (RW) : origin=0x08000000 length=0x00008000
    KRAM    (RW) : origin=0x08008000 length=0x00000800
    RAM     (RW) : origin=(0x08008000+0x00000800) length=(0x00100000 - 0x00008800)
}
```

## Sign output from CCS

In order to generate a signed image, the application is copied to a binary format and signed with the following commands
```
"c:\ti\ccs1031\ccs\tools\compiler\ti-cgt-arm_20.2.4.LTS\bin\armobjcopy.exe" -O binary application.out application.bin
".\tools\keytools\sign.exe" --ecc256 --sha256 application.bin ecc256.der 1
```

Output should resemble:
```
Update type:          Firmware
Input image:          application.bin
Selected cipher:      ECC256
Selected hash  :      SHA256
Public key:           ecc256.der
Output  image:        application_v1_signed.bin
Calculating SHA256 digest...
Signing the firmware...
Output image(s) successfully created.
```

To flash the signed image the following command can be used.

```
"c:\ti\ccs1031\ccs\ccs_base\scripting\examples\uniflash\cmdLine\uniflash.bat" -ccxml "IDE\CCS\TMS570LC43xx\flashHercules.ccxml"  -setOptions FlashEraseSelection="Necessary Sectors Only (for Program Load)" -programBin application_v1_signed.bin 0x20000 
```

# Implementation notes

 * R5 vector table can only be be at 0x00000000 or 0xFFFF0000
   * The wolfBoot exception handling forwards these to the Application

 * ASM must be self contained. See SPNU151V - ARM Optimizing C/C++ Compiler v20.2.0.LTS January 1998–Revised February 2020

> The __asm statement does not provide any way to refer to local
> variables. If your assembly code needs to refer to local variables,
> you will need to write the entire function in assembly code.
