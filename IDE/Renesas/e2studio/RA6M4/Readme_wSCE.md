# wolfBoot for Renesas RA6M4 with SCE

## 1. Overview

It demonstrates simple secure firmware update by wolfBoot and uses Renesas SCE. A sample application v1 is
securely updated to v2. Both versions behave the same except displaying its version of v1 or v2.
They are compiled by e2Studio and running on the target board.

In this demo, you may download two versions of application binary file by Renesas Flash Programmer.
You can download and excute wolfBoot by e2Studio debugger. Use a USB connection between PC and the
board for the debugger and flash programmer.It is only available RSA with Renesas SCE now.

## 2. Components and Tools


|Item|Name/Version|Note|
|:--|:--|:--|
|Board|Renesas EK-RA6M4||
|Device|R7FA6M4AF3CFB||
|Toolchain|GCC ARM Embedded 10.3.1.20210824|Included in GCC for Renesas RA|
|FSP Version|3.6.0|Download from Renesas site|
|IDE|e2studio 2022-01|Download from Renesas site|
|Flash Writer|Renesas Flash Programmer v3|Download from Renesas site|
|Binary tool|aarch64-none-elf-objcopy 10.3-2021.07|Download from GNU site|
|Key tool|keygen and sign|Included in wolfBoot|



|FIT Components|Version|
|:--|:--|
|Board Support Package Common Files|v3.6.0|
|I/O Port|v3.6.0|
|Arm CMSIS Version 5 - Core (M)|v5.8.0+fsp.3.6.0|
|RA6M4-EK Board Support Files|v3.6.0|
|Board support package for R7FA6M4AF3CFB|v3.6.0|
|Board support package for RA6M4|v3.6.0|
|Board support package for RA6M4 - FSP Data|v3.6.0|
|Flash Memory High Performance|v3.6.0|
|Secure Cryptography Engine on RA6 Protected Mode (CAVP Certified) | v1.0.0+fsp.3.6.0 |


e2Studio Project:\
wolfBoot      IDE/Renesas/e2studio/RA6M4/wolfBoot\
Sample app    IDE/Renesas/e2studio/RA6M4/app_RA


Flash Allocation:
```
+---------------------------+------------------------+-----+
| B |H|                     |H|                      |     |
| o |e|   Primary           |e|   Update             |Swap |
| o |a|   Partition         |a|   Partition          |Sect |
| t |d|                     |d|                      |     |
+---------------------------+------------------------+-----+
0x00000000: wolfBoot
0x00020000: Primary partition (Header)
0x00020200: Primary partition (Application image)
0x00090000: Update  partition (Header)
0x00090200: Update  partition (Application image)
0x000F0000: Swap sector
0x08010000: Wrapped Key
```

## 2. How to build and use
This section describes about how to build wolfBoot and application and use them.

### 1) Key generation
It has key tools running under the host environment such as Linux, Windows or MacOS.
For comiling the tools, follow the instruction described in the user manual.


```
$ cd <wolfBoot>
$ make keytools RENESAS_KEY=1
$ export PATH=$PATH:<wolfBoot>/tools/keytools
$ keygen --rsa2048 -g ./pri-rsa2048.der  # RSA2048
```

It generates a pair of private and public key with -g option. The private key is stored 
in the specified file. The public key is stored in a key store as a C source code 
in "src/keystore.c" so that it can be compiled and linked with wolfBoot.
If you have an existing key pair, you can use -i option to import the pablic
key to the store.


### 2) Compile wolfBoot

Open project under IDE/Renesas/e2studio/RA6M4/wolfBoot with e2Studio, and build the project.
Project properties are preset for the demo.\

WOLFBOOT_PARTION_INFO is for debug information about partitions.
Eliminate them for operational use.

Enabled `WOLFBOOT_RENESAS_SCEPROTECT` expects to use Renesas SCE.

### 3) Compile the sample application

Open project under IDE/Renesas/e2studio/RA6M4/app_RA with e2Studio. Open `script` folder and copy orignal `fsp.ld` to `fsp.ld.org`. Copy `fsp_wsce.ld` to `fsp.ld`, and then build the project.
Project properties are preset for the demo.

 #### 3-1). Prepare SEGGER_RTT for logging
  + Download J-Link software from [Segger](https://www.segger.com/downloads/jlink)
  + Choose `J-Link Software and Documentation Pack`
  + Copy sample program files below from `Installed SEGGER` folder, `e.g C:\Program Files\SEGGER\JLink\Samples\RTT`, to /path/to/wolfBoot/IDE/Reenesas/e2studio/RA6M4/app_RA/src/SEGGER_RTT\

    SEGGER_RTT.c\
    SEGGER_RTT.h\
    SEGGER_RTT_Conf.h\
    SEGGER_RTT_printf.c
  + Open `SEGGER_RTT_Conf.h` and Set `SEGGER_RTT_MEMCPY_USE_BYTELOOP` to `1`
  + To connect RTT block, you can configure RTT viewer configuration based on where RTT block is in map file\

  e.g.[app_RA.map]

    ```
    .bss._SEGGER_RTT
                0x2000094c       0xa8 ./src/SEGGER_RTT/SEGGER_RTT.o
                0x2000094c                _SEGGER_RTT
    ````
    
    you can specify "RTT control block" to 0x2000094c by Address
      OR
    you can specify "RTT control block" to 0x20000000 0x1000 by Search Range
    

Need to set:
#define BSP_FEATURE_FLASH_SUPPORTS_ACCESS_WINDOW          (1)\

Code Origin and entry point is "0x00010200". app_RA.elf is gnerated under Debug. 

### 4) Generate Wrapped Key for SCE

SCE needs to have a wrapped key for sign verification installed in advance.
This section describes how to use wolfBoot with SCE. 
Current version supports RSA2048. RSA Signature supports #PKCS 1, v1.5.
You can generate a RSA key pair by wolfBoot "keygen" command along with Renesas Security Key Management Tool "skmt".

"skmt" command wraps the RAW key and generates C language initial data and a header file for an application program with SCE. 
Please refer SCE User Manual for generating product provisioning.

```
$ export PATH:$PATH:<wolfBoot>/tools/keytools
$ export PATH:$PATH:<skmt>
$ cd <wolfBoot>
$ keygen --rsa2048 -g ./rsa-pri2048.der
$ openssl rsa -in rsa-pri2048.der -pubout -out rsa-pub2058.pem
$ skmt.exe /genkey /ufpk file=./ufpk.key /wufpk file=./ufpk.key_enc.key -key file=./pub-rsa2048.pem -mcu RA-SCE9 -keytype RSA-2048-public /output rsa_pub2048.rkey /filetype "rfp"
```

Please refer Renesas Manual to generate ufpk.key and upfk.key_enc.key.
After generating "rfp" format key, you can download it to flash data area by using Renesas flash programmer.

### 4) Generate Signature for app V1
You can derive bair binary file (app_RA.bin) by objcopy command as follows.

```
$ aarch64-none-elf-objcopy.exe -O binary -j .text -j .data app_RA.elf app_RA.bin
```

"sign" command under tools/keytools benerates a signature for the binary with a specified version.
It generates a file contain a partition header and application image. The partition header
contain generated signature and other control fields. Output file name is made up from
the input file name and version like app_RenesasRx01_v1.0_signed.bin. It needs to specify `--rsa2048enc` option to sign the image because SCE assumes to have DigestInfo structure before hashed data.

```
$ sign --rsa2048enc app_RA.bin ../../../../../pri-rsa2048.der 1.0
wolfBoot KeyTools (Compiled C version)
wolfBoot version 10E0000
Update type:          Firmware
Input image:          app_RA.bin
Selected cipher:      RSA2048
Selected hash  :      SHA256
Public key:           ./pri-rsa2048.der
Output  image:        app_RA_v1.0_signed.bin
Target partition id : 1
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
```

### 5) Download the app V1

You can convert the binary file to hex format and download it to the board by Flash Programmer.
The partition starts at "0x00020000".

```
$ aarch64-none-elf-objcopy.exe -I binary -O srec --change-addresses=0x00020000 app_RA_v1.0_signed.bin app_RA_v1.0_signed.hex
```


### 6) Execute inital boot

Now, you can download and start wolfBoot program by e2Studio debugger.
After starting the program, you can see the partition information as follows.
If the boot program succeeds integlity and authenticity check, it initiate the
application V1.

```
| ----------------------------------------------------------------------- |
| Renesas RA SCE User Application in BOOT partition started by wolfBoot   |
| ----------------------------------------------------------------------- |


WOLFBOOT_PARTITION_SIZE:           0x00060000
WOLFBOOT_PARTITION_BOOT_ADDRESS:   0x00020000
WOLFBOOT_PARTITION_UPDATE_ADDRESS: 0x00090000

Application Entry Address:         0x00020200

=== Boot Partition[00020000] ===
Magic:    WOLF
Version:  01
Status:   FF
Tail Mgc: 

=== Update Partition[00090000] ===
Magic:    
Version:  00
Status:   FF
Tail Mgc: 
Current Firmware Version : 1

Calling wolfBoot_success()

```

The application is calling wolfBoot_success() to set boot partition state.


```
Called wolfBoot_success()
=== Boot Partition[00020000] ===
Magic:    WOLF
Version:  01
Status:   00
Tail Mgc: BOOT

=== Update Partition[00090000] ===
Magic:    
Version:  00
Status:   FF
Tail Mgc:
```
You can see the state is Success("00") and Tail Magic number becomes "BOOT". You can also see flashing each LED light in 1 second.
Notable things about V1 application, it will also call wolfBoot_update_trigger() so that it tells wolfBoot that new version exists. 
We are going to generate and download V2 application into "Update pertition".

### 7) Generate Signed app V2 and download it

Similar to V1, you can signe and generate a binary of V2. The update partition starts at "0x00090000".
You can download it by the flash programmer.

Updtate partition:
-change-addresses=0x00090000

```
$ sign --rsa2048enc app_RA.bin ../../../../../pri-rsa2048.der 2.0
$ aarch64-none-elf-objcopy.exe -I binary -O srec --change-addresses=0x00090000 app_RA_v2.0_signed.bin app_RA_v2.0_signed.hex
```


### 8) Re-boot and secure update to V2

The boot program checks integlity and authenticity of V2, swap the partition
safely and initiates V2. You will see following message after the partition
information.

```
| ----------------------------------------------------------------------- |
| Renesas RA SCE User Application in BOOT partition started by wolfBoot   |
| ----------------------------------------------------------------------- |


WOLFBOOT_PARTITION_SIZE:           0x00060000
WOLFBOOT_PARTITION_BOOT_ADDRESS:   0x00020000
WOLFBOOT_PARTITION_UPDATE_ADDRESS: 0x00090000

Application Entry Address:         0x00020200

=== Boot Partition[00020000] ===
Magic:    WOLF
Version:  02
Status:   10
Tail Mgc: BOOT

=== Update Partition[00090000] ===
Magic:    WOLF
Version:  01
Status:   FF
Tail Mgc: 
Current Firmware Version : 2

Calling wolfBoot_success()
Called wolfBoot_success()
=== Boot Partition[00020000] ===
Magic:    WOLF
Version:  02
Status:   00
Tail Mgc: BOOT

=== Update Partition[00090000] ===
Magic:    WOLF
Version:  01
Status:   70
Tail Mgc: BOOT
```
You can see "Current Firmware Version : 2". The state is Success("00") and Tail Magic number becomes "BOOT". 
You can also see flashing each LED light in 5 second at this new version.


