## wolfBoot for Renesas RX72N

## 1. Overview


This example for `Renesas RX72N` demonstrates simple secure firmware update by wolfBoot. A sample application v1 is
securely updated to v2. Both versions behave the same except displaying its version of v1 or v2.
They are compiled by e2Studio and running on the target board.

In this demo, you may download two versions of application binary file by Renesas Flash Programmer.
You can download and execute wolfBoot by e2Studio debugger. Use a USB connection between PC and the
board for the debugger and flash programmer.

## 2. Components and Tools

|Item|Name/Version|Note|
|:--|:--|:--|
|Board|RX72N/Envision Kit||
|MCU|Renesas RX72N|R5F572NNxFB|
|IDE|e2studio 2022-07|Download from Renesas site|
|Compiler|CCRX v3.04.00||
|FIT Module||Download from Renesas site|
|Flash Writer|Renesas Flash Programmer v3|Download from Renesas site|
|Key tools|keygen and sign|Included in wolfBoot|
|rx-elf-objcopy|GCC for Renesas RX 8.3.0.202202-GNURX-ELF|Included in GCC for Renesas RX|


FIT Module
|Module|Version|Note|
|:--|:--|:--|
|r_bsp|v7.20|#define BSP_CFG_USTACK_BYTES            (0x2000)|
||key size uses rsa-3072, please sets to (0x3000)|
|r_flash_rx|v4.90||


Flash Allocation:
```
+---------------------------+------------------------+-----+
| B |H|                     |H|                      |     |
| o |e|   Primary           |e|   Update             |Swap |
| o |a|   Partition         |a|   Partition          |Sect |
| t |d|                     |d|                      |     |
+---------------------------+------------------------+-----+
0xffc00000: wolfBoot
0xffc10000: Primary partition (Header)
0xffc10100: Primary partition (Application image) /* When it uses IMAGE_HEADER_SIZE 256, e.g. ED25519, EC256, EC384 or EC512 */
0xffc10200: Primary partition (Application image) /* When it uses IMAGE_HEADER_SIZE 512, e.g. RSA2048, RSA3072 */
0xffdf8000: Update  partition (Header)
0xffdf8100: Update  partition (Application image) /* When it uses IMAGE_HEADER_SIZE 256, e.g. ED25519, EC256, EC384 or EC512 */
0xffdf8200: Update  partition (Application image) /* When it uses IMAGE_HEADER_SIZE 512, e.g. RSA2048, RSA3072 */
0xfffe0000: Swap sector
```

Note : Depending on IMAGE_HEADER_SIZE, it needs to change the address of Power Reset vector by Linker section.
Application default is set to 0xffc10200. It means that you need to change it when you use 256 IMAGE_HEADER_SIZE.

To resolve the "RPFRAM" section not assigned / overflow follow the RX Family Flash Module guide section 5.3.1.1 or 5.3.12 for adding that section.
https://www.renesas.com/us/en/document/apn/rx-family-flash-module-using-firmware-integration-technology


## 3. How to build and use
It has key tools running under the host environment such as Linux, Windows or MacOS.
For compiling the tools, follow the instruction described in the user manual.

It demonstrates simple secure firmware update by wolfBoot. A sample application v1 is
securely updated to v2. Both versions behave the same except displaying its version of v1 or v2.
They are compiled by e2Studio and running on the target board.

In this demo, you may download two versions of application binary file by Renesas Flash Programmer.
You can download and execute wolfBoot by e2Studio debugger. Use a USB connection between PC and the
board for the debugger and flash programmer.


### 3-1 Key generation

```
$ cd <wolfBoot>
$ make keytools
$ ./tools/keytools/keygen --ecc256 -g ./pri-ecc256.der
```

This generates a pair of private and public keys with -g option. The private key is stored
in the specified file. The public key is stored in a key store as a C source code
in "src/keystore.c" soo that it can be compiled and linked with wolfBoot.
If you have an existing key pair, you can use -i option to import the public
key to the store.

You can specify various signature algorithms such as

```
--ed25519 --ed448 --ecc256 --ecc384 --ecc521 --rsa2048 --rsa3072
```

### 3-2 Compile wolfBoot

Open project under IDE/Renesas/e2studio/RX72N/wolfBoot with e2Studio, and build the project.

Project properties are preset for the demo.

Open the wolfBoot.scfg from e2Studio and allow it to download/install the required FIT modules for the RX72N and `r_flash_rx`.

```
Smart Configurator
Flash Driver: r_flash_rx

Include Paths
../../include : <wolfBoot>/IDE/Renesas/e2studio/RX72N/include
../../../../../../include : <wolfBoot>/include
../../../../../../lib/wolfssl/ : <wolfBoot>/lib/wolfssl

Pre-Include
../../include/user_settings.h : <wolfBoot>/IDE/Renesas/e2studio/RX72N/include/user_settings.h
../../include/target.h : <wolfBoot>/IDE/Renesas/e2studio/RX72N/include/target.h

Pre-defined Pre-processor Macro
__WOLFBOOT
PRINTF_ENABLED
```

`PRINTF_ENABLED` is for debug information about partitions.
Eliminate them for operational use.


### 3-4 Compile the sample application

Open project under IDE/Renesas/e2studio/RX72N/app_RenesasRx01 with e2Studio, and build the project.


Project properties are preset for the demo.

```
Smart Configurator
Flash Driver: r_flash_rx

Include Paths
Include Paths
../../include : <wolfBoot>/IDE/Renesas/e2studio/RX72N/include
../../../../../../include : <wolfBoot>/include

Pre-Include
../../include/user_settings.h : <wolfBoot>/IDE/Renesas/e2studio/RX72N/include/user_settings.h
../../include/target.h : <wolfBoot>/IDE/Renesas/e2studio/RX72N/include/target.h

Code Origin and entry point (PResetPRG) is "0xffc10200" (See Section Viewer of Linker Section).
```

`app_RenesasRx01.x` in ELF is generated under HardwareDebug (`IDE/Renesas/e2studio/RX72N/app_RenesasRX01/HardwareDebug`).

Build tools are typically located here:
`C:\ProgramData\GCC for Renesas RX 8.3.0.202305-GNURX-ELF\rx-elf\rx-elf\bin` or `/c/ProgramData/GCC\ for\ Renesas\ RX\ 8.3.0.202305-GNURX-ELF/rx-elf/rx-elf/bin`

You can derive the binary file `app_RenesasRx01.bin` by using `rx-elf-objcopy.exe` as follows:

```
$ rx-elf-objcopy.exe -O binary\
  -R '$ADDR_C_FE7F5D00' -R '$ADDR_C_FE7F5D10' -R '$ADDR_C_FE7F5D20' -R '$ADDR_C_FE7F5D30'\
  -R '$ADDR_C_FE7F5D40' -R '$ADDR_C_FE7F5D48' -R '$ADDR_C_FE7F5D50' -R '$ADDR_C_FE7F5D64'\
  -R '$ADDR_C_FE7F5D70' -R EXCEPTVECT -R RESETVECT app_RenesasRx01.x app_RenesasRx01.bin
```

Note `-R` is for eliminate unnecessary sections.

### 3-5 Generate Signature for app V1

The sign tool (`tools/keytools/sign`) generates a signature for the binary with a specified version.
It generates a file containing a partition header and application image. The partition header
includes the generated signature and other control fields. Output file name is made up from
the input file name and version like `app_RenesasRx01_v1.0_signed.bin`.

```
$ ./tools/keytools/sign --ecc256 IDE/Renesas/e2studio/RX72N/app_RenesasRX01/HardwareDebug/app_RenesasRx01.bin pri-ecc256.der 1.0 ecc256.der 1.0
wolfBoot KeyTools (Compiled C version)
wolfBoot version 10B0000
Update type:          Firmware
Input image:          app_RenesasRx.bin
Selected cipher:      ECC256
Selected hash  :      SHA256
Public key:           ecc256.der
Output  image:        app_RenesasRx_v1.0_signed.bin
Target partition id : 1
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
```

### 3-6 Download the app V1

You can convert the binary file to hex format and download it to the board by Renesas Flash Programmer tool. Use E2 emulator lite and FINE interface.
The partition starts at "0xffc10000".

```
$ rx-elf-objcopy.exe -I binary -O srec --change-addresses=0xffc10000 IDE/Renesas/e2studio/RX72N/app_RenesasRX01/HardwareDebug/app_RenesasRx01_v1.0_signed.bin IDE/Renesas/e2studio/RX72N/app_RenesasRX01/HardwareDebug/app_RenesasRx01_v1.0_signed.hex
```


### 3-7 Execute initial boot

Now, you can download and start wolfBoot program by e2Studio debugger.
After starting the program, you can see the partition information as follows.
If the boot program succeeds integrity and authenticity check, it initiate the
application V1.


```
| ------------------------------------------------------------------- |
| Renesas RX User Application in BOOT partition started by wolfBoot   |
| ------------------------------------------------------------------- |


=== Boot Partition[ffc10000] ===
Magic:    WOLF
Version:  01
Status:   ff (New)
Tail Mgc: ����

=== Update Partition[ffdf8000] ===
Magic:    ����
Version:  00
Status:   ff (New)
Tail Mgc: ����

Current Firmware Version: 1
Hit any key to call wolfBoot_success the firmware.
```

After hitting any key, the application calls wolfBoot_success() to set boot partition
state and wait for any key again.

```
=== Boot Partition[ffc10000] ===
Magic:    WOLF
Version:  01
Status:   00 (Success)
Tail Mgc: BOOT

=== Update Partition[ffdf8000] ===
Magic:    ����
Version:  00
Status:   00 (Success)
Tail Mgc: BOOT

```
You can see the state is Success("00").

### 3-8 Generate Signed app V2 and download it

Similar to V1, you can sign and generate a binary of V2. The update partition starts at "0xffdf8000".
You can download it by the flash programmer.


```
$ sign --ecc256 app_RenesasRx01.bin ../../../../../pri-ecc256.der 2.0
rx-elf-objcopy.exe -I binary -O srec --change-addresses=0xffdf8000 app_RenesasRx01_v2.0_signed.bin app_RenesasRx01_v2.0_signed.hex
```


### 3-9 Re-boot and secure update to V2

Now the image is downloaded but note that the partition status is not changed yet.
When it is re-boot, it checks integrity and authenticity of V1 and initiate V1 as in
step 8.

```
| ------------------------------------------------------------------- |
| Renesas RX User Application in BOOT partition started by wolfBoot   |
| ------------------------------------------------------------------- |

Current Firmware Version: 1
....
Hit any key to update the firmware.
```

After you see the message, hit any key so that the application calls
wolfBoot_update_trigger() which changes the partition status and triggers
updating the firmware. You will see the following messages.

```
Firmware Update is triggered
=== Boot Partition[ffc10000] ===
Magic:    WOLF
Version:  01
Status:   00 (Success)
Tail Mgc: BOOT

=== Update Partition[ffdf8000] ===
Magic:    ����
Version:  00
Status:   70 (Updating)
Tail Mgc: BOOT
```

Since this is just a trigger, the application can continue the process.
In the demo application it outputs a message "Firmware Update is triggered" and enters
a infinite loop of nop.

Now you can reboot it by start wolfBoot by e2studio debugger. The boot
program checks integrity and authenticity of V2 now, swap the partition
safely and initiates V2. You will see following message after the partition
information.


```
| ------------------------------------------------------------------- |
| Renesas RX User Application in BOOT partition started by wolfBoot   |
| ------------------------------------------------------------------- |


=== Boot Partition[ffc10000] ===
Magic:    WOLF
Version:  02
Status:   10
Tail Mgc: BOOT

=== Update Partition[ffdf8000] ===
Magic:    WOLF
Version:  01
Status:   30
Tail Mgc: BOOT

Current Firmware Version: 2
```

Note That application behavior is almost identical but the Version is "2" this time.
