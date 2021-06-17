# TMS750LC43xx

# Build

## Build from command line

```
make CCS_ROOT=/c/ti/ccs1031/ccs/tools/compiler/ti-cgt-arm_20.2.4.LTS F021_DIR=/c/ti/Hercules/F021\ Flash\ API/02.01.01
```

## Build using Code Composer Studio (CCS)

Create key or copy to direcotry `IDE/CCS/TMS570LC43xx/` if you already have one.

```
make -C ./tools/keytools
./tools/keytools/keygen.exe --ecc256 src/ecc256_pub_key.c
```

Use IDE to build and load wolfBoot bootloader

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


# Details

 * R5 vector table can only be be at 0 or 0xFFFF0000
   * A possible strategy is to have simple handlers that check
     if a RAM overload is available. This requires shared state
     between a bootloader and application.

# Implemenation notes
 * ASM must be self contained. See SPNU151V - ARM Optimizing C/C++ Compiler v20.2.0.LTS January 1998–Revised February 2020

> The __asm statement does not provide any way to refer to local
> variables. If your assembly code needs to refer to local variables,
> you will need to write the entire function in assembly code.
