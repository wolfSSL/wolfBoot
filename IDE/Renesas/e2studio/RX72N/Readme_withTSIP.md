## wolfBoot for Renesas RX72N with TSIP

## 1. Overview

This example demonstrates simple secure firmware update by wolfBoot and uses Renesas TSIP. A sample application v1 is
securely updated to v2. Both versions behave the same except displaying its version of v1 or v2.
They are compiled by e2Studio and running on the target board.

In this demo, you may download two versions of application binary file by Renesas Flash Programmer.
You can download and execute wolfBoot by e2Studio debugger. Use a USB connection between PC and the
board for the debugger and flash programmer. It is only available RSA with Renesas TSIP now.

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
|r_tsip_rx|v1.18.J|Library or Source Version. Please contact Renesas to get source code version.|


Flash Allocation (TSIP Library version use):
```
+---------------------------+------------------------+-----+
| B |H|                     |H|                      |     |
| o |e|   Primary           |e|   Update             |Swap |
| o |a|   Partition         |a|   Partition          |Sect |
| t |d|                     |d|                      |     |
+---------------------------+------------------------+-----+
0xffc00000: wolfBoot
0xffc70000: Primary partition (Header)
0xffc70200: Primary partition (Application image) /* When it uses IMAGE_HEADER_SIZE 512, e.g. RSA2048, RSA3072 */
0xffe20000: Update  partition (Header)
0xffe20200: Update  partition (Application image)
0xfffd0000: Swap sector
```

Note : Depending on IMAGE_HEADER_SIZE, it needs to change the address of Power Reset vector by Linker section.
Application default is set to 0xffc70200. It means that you need to change it when you use 256 IMAGE_HEADER_SIZE.


Flash Allocation(TSIP source code version use):
```
+---------------------------+------------------------+-----+
| B |H|                     |H|                      |     |
| o |e|   Primary           |e|   Update             |Swap |
| o |a|   Partition         |a|   Partition          |Sect |
| t |d|                     |d|                      |     |
+---------------------------+------------------------+-----+
0xffc00000: wolfBoot
0xffc10000: Primary partition (Header)
0xffc10200: Primary partition (Application image) /* When it uses IMAGE_HEADER_SIZE 512, e.g. RSA2048, RSA3072 */
0xffdf0000: Update  partition (Header)
0xffdf0200: Update  partition (Application image)
0xfffd0000: Swap sector

```

Note : By enabling `WOLFBOOT_RENESAS_TSIP_SRCVERSION`, this example uses the allocation above.


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
$ make keytools RENESAS_KEY=2
$ ./tools/keytools/keygen --rsa2048 -g ./pri-rsa2048.der
Keytype: RSA2048
Gen ./pri-rsa2048.der
Generating key (type: RSA2048)
RSA public key len: 294 bytes
Associated key file:   ./pri-rsa2048.der
Key type   :           RSA2048
Public key slot:       0
Done.
```

This generates a pair of private and public keys with -g option. The private key is stored
in the specified file. The public key is stored in a key store as a C source code
in "src/keystore.c" soo that it can be compiled and linked with wolfBoot.
If you have an existing key pair, you can use -i option to import the public
key to the store.

### 3-2 Compile wolfBoot

Open project under IDE/Renesas/e2studio/RX72N/wolfBoot with e2Studio, and build the project.

Project properties are preset for the demo.

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
```

To enable TSIP use, it needs commenting out `WOLFBOOT_RENESAS_TSIP`.
To enable TSIP Source code version, please comment out `WOLFBOOT_RENESAS_TSIP_SRCVERSION`.

`PRINTF_ENABLED` are for debug information about partitions.
Eliminate them for operational use.

When you use TSIP source code version, taking the following two steps  optimize TSIP driver size.

* Set unnecessary ciphers to "No using XXXX" by Smart Configuration. Only select
* Check "Deletes variables/functions that are not referenced)" Liker optimization option, [Project]->[Properties]->[C/C++ Build]->[Settings]->[Linker]->[Optimization]


### 3-3 Compile the sample application

Open project under IDE/Renesas/e2studio/RX72N/app_RenesasRx01 with e2Studio, and build the project.


Project properties are preset for the demo.

```
Smart Configurator
Flash Driver: r_flash_rx
TSIP Driver: r_tsip_rx

Include Paths
../../include : <wolfBoot>/IDE/Renesas/e2studio/RX72N/include
../../../../../../include : <wolfBoot>/include

Pre-Include
../../include/user_settings.h : <wolfBoot>/IDE/Renesas/e2studio/RX72N/include/user_settings.h
../../include/target.h : <wolfBoot>/IDE/Renesas/e2studio/RX72N/include/target.h

Code Origin and entry point (PResetPRG) is "0xffc70200" (See Section Viewer of Linker Section).
Code Origin and entry point (PResetPRG) is "0xffc10200" (TSIP source code version and optimization steps above are done.)
You would need updating section settings depending on either TSIP version use.
```

app_RenesasRx01.x in ELF is generated under HardwareDebug. You can derive the binary file
(app_RenesasRx01.bin) by rx-elf-objcopy.exe command as follows. -R are for eliminate unnecessary
sections.

```
$ rx-elf-objcopy.exe -O binary\
  -R '$ADDR_C_FE7F5D00' -R '$ADDR_C_FE7F5D10' -R '$ADDR_C_FE7F5D20' -R '$ADDR_C_FE7F5D30'\
  -R '$ADDR_C_FE7F5D40' -R '$ADDR_C_FE7F5D48' -R '$ADDR_C_FE7F5D50' -R '$ADDR_C_FE7F5D64'\
  -R '$ADDR_C_FE7F5D70' -R EXCEPTVECT -R RESETVECT app_RenesasRx01.x app_RenesasRx01.bin
```

### 3-3 Generate Encrypted Key for TSIP

TSIP requires to have a encrypted public key for sign verification to generate its own Renesas TSIP Key.
This example supports RSA2048.

You can generate a RSA key pair by wolfBoot "keygen" command along with Renesas Security Key Management Tool "skmt".
"skmt" command creates the encrypted Renesas key, generating Motorola hex file for writing it to flash memory and also generates a c header and source file for an application program with TSIP.
This example uses a encrypted RSA key which is installed into Flash memory in advance.

To generate a RSA key for sign and verify, you can follow the following commands:
```
$ export PATH:$PATH:<wolfBoot>/tools/keytools
$ export PATH:$PATH:<skmt>
$ cd <wolfBoot>
$ keygen --rsa2048 -g ./pri-rsa2048.der
$ openssl rsa -inform der -in ./pri-rsa2048.der -pubout -out ./pub-rsa2048.pem
```

Generate a c header and source file from PEM file :
```
$ export PATH="$PATH:C:\Renesas\SecurityKeyManagementTool\cli"
$ skmt.exe -genkey -ufpk file=./sample.key -wufpk file=./sample.key_enc.key -key file=./pub-rsa2048.pem -mcu RX-TSIP -keytype RSA-2048-public -output key_data.c -filetype csource -keyname enc_pub_key
```

The value of option `-keyname` becomes structure name and macro definition defined in key_data.h. Therefore, please specify `enc_pub_key` like above unless there is some particular reason.

Copy a generated c header file, which is `key_data.h` to `<wolfBoot>/include/` folder

Generate Motorola hex file to write it to flash memory from PEM file :
```
$ skmt.exe -genkey -ufpk file=./sample.key -wufpk file=./sample.key_enc.key -key file=./pub-rsa2048.pem -mcu RX-TSIP -keytype RSA-2048-public -output rsa_pub2048.mot -filetype "mot" -address "FFFF0000"
```
The generated `mot` key is written to `0xFFFF0000` address. The flash memory address is set by macro, which is `RENESAS_TSIP_INSTALLEDKEY_ADDR` in `user_settings.h`
After generating "mot" format key, you can download it to flash data area by using Renesas flash programmer.

Please refer Renesas Manual to generate sample.key, sample.key_enc.key and to use `skmt` command in detail.


### 3-4 Generate Signature for app V1

The `sign` command under tools/keytools generates a signature for the binary with a specified version.
It generates a file containing a partition header and application image. The partition header
includes the generated signature and other control fields. Output file name is made up from
the input file name and version like app_RenesasRx01_v1.0_signed.bin.

```
$ sign --ras2048enc app_RenesasRx01.bin ../../../../../pri-rsa2048.der 1.0
sign app_RenesasRx01.bin for version 1
wolfBoot KeyTools (Compiled C version)
wolfBoot version 10F0000
Update type:          Firmware
Input image:          app_RenesasRx01.bin
Selected cipher:      RSA2048ENC
Selected hash  :      SHA256
Public key:           ./pri-rsa2048.der
Output  image:        app_RenesasRx01_v1.0_signed.bin
Target partition id : 1
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
```

### 3-6 Download the app V1

You can convert the binary file to hex format and download it to the board by Flash Programmer.
The partition starts at "0xffc70000"(TSIP library version) or "0xffc10000"(TSIP source code version).

```
For TSIP Library version
$ rx-elf-objcopy.exe -I binary -O srec --change-addresses=0xffc70000 app_RenesasRx01_v1.0_signed.bin app_RenesasRx01_v1.0_signed.hex
or
For TSIP Source version
$ rx-elf-objcopy.exe -I binary -O srec --change-addresses=0xffc10000 app_RenesasRx01_v1.0_signed.bin app_RenesasRx01_v1.0_signed.hex
```


### 3-7 Execute initial boot

Now, you can download and start wolfBoot program by e2Studio debugger.
After starting the program, you can see the partition information as follows.
If the boot program succeeds integrity and authenticity check, it initiate the
application V1.


```
| -------------------------------------------------------------------------------- |
| Renesas RX w/ TSIP(LIB) User Application in BOOT partition started by wolfBoot   |
| -------------------------------------------------------------------------------- |


=== Boot Partition[ffc70000] ===
Magic:    WOLF
Version:  01
Status:   ff
Trailer Magic: ����

=== Update Partition[ffe20000] ===
Magic:    WOLF
Version:  02
Status:   ff
Trailer Magic: ����

Current Firmware Version: 1
Hit any key to call wolfBoot_success the firmware.
```

After hitting any key, the application calls wolfBoot_success() to set boot partition
state and wait for any key again.

If you re-start the boot program at this moment,
after checking the integrity and authenticity, it jumps to the application.
You can see the state is Success("00").

```
=== Boot Partition[ffc70000] ===
Magic:    WOLF
Version:  01
Status:   00
Trailer Magic: BOOT

=== Update Partition[ffe20000] ===
Magic:    WOLF
Version:  02
Status:   ff
Trailer Magic: ����

Hit any key to update the firmware.
```

### 3-8 Generate Signed app V2 and download it

Similar to V1, you can sign and generate a binary of V2. The update partition starts at "0xffdf0000".
You can download it by the flash programmer.


```
For TSIP Library version
$ sign --ras2048 app_RenesasRx01.bin ../../../../../pri-rsa2048.der 2.0
$ rx-elf-objcopy.exe -I binary -O srec --change-addresses=0xffe20000 app_RenesasRx01_v2.0_signed.bin app_RenesasRx01_v2.0_signed.hex
or
For TSIP Source version
$ rx-elf-objcopy.exe -I binary -O srec --change-addresses=0xffdf0000 app_RenesasRx01_v2.0_signed.bin app_RenesasRx01_v2.0_signed.hex
```


### 3-9 Re-boot and secure update to V2

Now the image is downloaded but note that the partition status is not changed yet.
When it is re-boot, it checks integrity and authenticity of V1 and initiate V1 as in
step 6.

```
| -------------------------------------------------------------------------------- |
| Renesas RX w/ TSIP(LIB) User Application in BOOT partition started by wolfBoot   |
| -------------------------------------------------------------------------------- |

Current Firmware Version: 1
Hit any key to update the firmware.
Firmware Update is triggered
```

After you see the message, hit any key so that the application calls
wolfBoot_update_trigger(), which changes the partition status and triggers
updating the firmware.

Since this is just a trigger, the application can continue the process.
In the demo application it outputs a message "Firmware Update is triggered" and enters
a infinite loop of nop.

Now you can reboot it by start wolfBoot by e2studio debugger. The boot
program checks integrity and authenticity of V2 now, swap the partition
safely and initiates V2. You will see following message after the partition
information.


```
| -------------------------------------------------------------------------------- |
| Renesas RX w/ TSIP(LIB) User Application in BOOT partition started by wolfBoot   |
| -------------------------------------------------------------------------------- |


=== Boot Partition[ffc70000] ===
Magic:    WOLF
Version:  02
Status:   10
Trailer Magic: BOOT

=== Update Partition[ffe20000] ===
Magic:    ����
Version:  00
Status:   ff
Trailer Magic: ����

Current Firmware Version: 2
```

Note That application behavior is almost identical but the Version is "2" this time.
