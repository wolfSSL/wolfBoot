# TMS750LC43xx

Create key or copy to direcotry `IDE/CCS/TMS570LC43xx/` if you already have one.

```
make -C ./tools/keytools
./tools/keytools/keygen.exe IDE/CCS/TMS570LC43xx/ed25519_pub_key.c
```


## build
```
make CCS_ROOT=/c/ti/ccs1031/ccs/tools/compiler/ti-cgt-arm_20.2.4.LTS F021_DIR=/c/ti/Hercules/F021\ Flash\ API/02.01.01
```

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
c:\ti\ccs1031\ccs\ccs_base\scripting\examples\uniflash\cmdLine\uniflash.bat -ccxml "..\temp-flash\flashHercules.ccxml"  -setOptions FlashEraseSelection="Necessary Sectors Only (for Program Load)" -programBin factory.bin 0
```

[dss reference](http://software-dl.ti.com/ccs/esd/documents/users_guide/sdto_dss_handbook.html)

