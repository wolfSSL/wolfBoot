## wolfBoot for Renesas RX72N

#define BSP_CFG_USTACK_BYTES            (0x2000)

```
MCU:          Renesas RX72N
Board:        RX72N/Envision Kit
IDE:          e2Studio
Compiler:     CCRX
FIT Module:   r_flash_rx


e2Studio Project:
wolfBoot      IDE/Renesas/e2studio_CCRX/wolfBoot
Sample app    IDE/Renesas/e2studio_CCRX/app_RenesasRX01

Other Tools:
- Key tool
    Key generation    tools/keytools/keygen
    Signature         tools/keytools/sign
        Included in wolfBoot with source code

- Flash Wirter
    Renesas Flash Programmer v3
        Download from Renesas site

- Binary tool: 
    rx-elf-objcopy.exe
        Included in GCC for Renesas RX


Flash Allocation:
+---------------------------+------------------------+-----+
| B |H|                     |H|                      |     |
| o |e|   Primary           |e|   Update             |Swap |
| o |a|   Partition         |a|   Partition          |Sect |
| t |d|                     |d|                      |     |
+---------------------------+------------------------+-----+
0xffc00000: wolfBoot
0xffc10000: Primary partition (Header)
0xffc10100: Primary partition (Application image)
0xffdf0000: Update  partition (Header)
0xffdf0100: Update  partition (Application image)
0xfffd0000: Swap sector
```

### Decription
It has key tools running under the host environment such as Linux, Windows or MacOS.
For comiling the tools, follow the instruction described in the user manual.

It demonstrates simple secure firmware update by wolfBoot. A sample application v1 is
cerurely updated to v2. Both versions behave the same except displaying its version of v1 or v2.
They are compiled by e2Studio and running on the target board.

In this demo, you may download two versions of application binary file by Renesas Flash Programmer.
You can download and excute wolfBoot by e2Studio debugger. Use a USB connection between PC and the
board for the debugger and flash programmer.


### 1) Key generation

```
$ cd <wolfBoot>
$ export PATH:$PATH:<wolfBoot>/tools/keytools
$ keygen --ecc256 -g ./pri-ecc256.der
```

It generates a pair of private and public key with -g option. The private key is stored 
in the specified file. The public key is stored in a key store as a C source code 
in "src/keystore.c" soo that it can be compiled and linked with wolfBoot.
If you have an existing key pair, you can use -i option to import the pablic
key to the store.

You can specify various signature algorithms such as 

```
--ed25519 --ed448 --ecc256 --ecc384 --ecc521 --rsa2048 --rsa3072 --rsa4096
```

### 2) Compile wolfBoot

Open project under IDE/Renesas/e2studio_CCRX/wolfBoot with e2Studio, and build the project.

Project properties are preset for the demo.

```
Smart Configurator
Flash Driver: r_flash_rx

Include Paths
"C:..\..\..\..\..\IDE/Renesas/e2Studio_CCRX/include
"C:..\..\..\..\..\wolfBoot\wolfboot/include"
"C:..\..\..\..\..\wolfBoot\wolfboot\include"

Pre-Include
../../../../../include/user_settings.h

Pre-defined Pre-processor Macro
__WOLFBOOT
WOLFBOOT_PARTIION_INFO
PRINTF_ENABLED

```

WOLFBOOT_PARTION_INFO, PRINTF_ENABLED are for debug information about partitions.
Eliminate them for operational use.


### 3) Compile the sample application

Open project under IDE/Renesas/e2studio_CCRX/app_RenesasRx01 with e2Studio, and build the project.


Project properties are preset for the demo.

```
Smart Configurator
Flash Driver: r_flash_rx

Include Paths
"C:..\..\..\..\..\IDE/Renesas/e2Studio_CCRX/include
"C:..\..\..\..\..\wolfBoot\wolfboot/include"
"C:..\..\..\..\..\wolfBoot\wolfboot\include"

Pre-Include
../../include/user_settings.h
../../include/terget.h

Code Origin and entry point (PResetPRG) is "0xffc10100" (See Section Viewer of Linker Section).
```

app_RenesasRx01.x in ELF is gnerated under HardwareDebug. You can derive bair binary file 
(app_RenesasRx01.bin) by rx-elf-objcopy.exe command as follows. -R are for eliminate unnecessary
secrions.

```
$ rx-elf-objcopy.exe -O binary\
  -R '$ADDR_C_FE7F5D00' -R '$ADDR_C_FE7F5D10' -R '$ADDR_C_FE7F5D20' -R '$ADDR_C_FE7F5D30'\
  -R '$ADDR_C_FE7F5D40' -R '$ADDR_C_FE7F5D48' -R '$ADDR_C_FE7F5D50' -R '$ADDR_C_FE7F5D64'\
  -R '$ADDR_C_FE7F5D70' -R EXCEPTVECT -R RESETVECT app_RenesasRx01.x app_RenesasRx01.bin
```

### 4) Generate Signature for app V1

"sign" command under tools/keytools benerates a signature for the binary with a specified version.
It generates a file contain a partition header and application image. The partition header
contain generated signature and other control fields. Output file name is made up from
the input file name and version like app_RenesasRx01_v1.0_signed.bin.

```
$ sign --ecc256 app_RenesasRx01.bin ../../../../../pri-ecc256.der 1.0 ecc256.der 1.0
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

### 5) Download the app V1

You can convert the binary file to hex format and download it to the board by Flash Programmer.
The partition starts at "0xffc10000".

```
$ rx-elf-objcopy.exe -I binary -O srec --change-addresses=0xffc10000 app_RenesasRx01_v1.0_signed.bin app_RenesasRx01_v1.0_signed.hex
```


### 6) Execute inital boot

Now, you can download and start wolfBoot program by e2Studio debugger.
After starting the program, you can see the partition information as follows.
If the boot program succeeds integlity and authenticity check, it initiate the
application V1. 


```
=== Boot Partition[ffc10000] ===
Magic:    WOLF
Version:  01
Status:   ff
Tail Mgc: ����


=== Update Partition[ffdf0000] ===
Magic:    ����
Version:  ff
Status:   ff
Tail Mgc: ����

| ------------------------------------------------------------------- |
| Renesas RX User Application in BOOT partition started by wolfBoot   |
| ------------------------------------------------------------------- |

Current Firmware Version: 1
Hit any key to update the firmware.
```
The application calls wolfBoot_success() to set boot partition
state and wait for any key. if you re-start the boot program at this moment, 
after checking the integlity and authenticity, it jumps to the application.
You can see the state is Success("00").

```
=== Boot Partition[ffc10000] ===
Magic:    WOLF
Version:  01
Status:   00
Tail Mgc: BOOT
```

### 7) Generate Signed app V2 and download it

Similar to V1, you can signe and generate a binary of V2. The update partition starts at "0xffdf0000".
You can download it by the flash programmer.


```
$ sign --ecc256 app_RenesasRx01.bin ../../../../../pri-ecc256.der 2.0
rx-elf-objcopy.exe -I binary -O srec --change-addresses=0xffdf0000 app_RenesasRx01_v2.0_signed.bin app_RenesasRx01_v2.0_signed.hex
```


### 8) Re-boot and secure update to V2

Now the image is downloaded but note that the partition status is not changed yet.
When it is re-boot, it checks integlity and authenticity of V1 and initiate V1 as in
step 6. 

```
| ------------------------------------------------------------------- |
| Renesas RX User Application in BOOT partition started by wolfBoot   |
| ------------------------------------------------------------------- |

Current Firmware Version: 1
Hit any key to update the firmware.
```

After you see the message, hit any key so that the application calls
wolfBoot_update_trigger() whcih changes the partition status and triggers
updating the firmware.

Since this is just a trigger, the application can continue the process.
In the demo application it outputs a message "Firmware Update is triggered" and enters
a infinit loop of nop.

Now you can re-boot it by start wolfBoot by e2Studion debugger. The boot
program checks integlity and authenticity of V2 now, swap the partition
safely and initiates V2. You will see following message after the partition
information.


```
| ------------------------------------------------------------------- |
| Renesas RX User Application in BOOT partition started by wolfBoot   |
| ------------------------------------------------------------------- |

Current Firmware Version: 2
Hit any key to update the firmware.
```

Not the application behavior is almost identical but the Version is "2" this time.



## Creating an application project from scratch

