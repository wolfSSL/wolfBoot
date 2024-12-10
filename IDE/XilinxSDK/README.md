# Xilinx SDK wolfBoot Project

To use this example project:
1. Copy `.cproject` and `.project` into the wolfBoot root.
2. From the Xilinx SDK Import wolfBoot using "Import" -> "Existing Projects into Workspace".

## Xilinx SDK BSP

This project uses a BSP named `standalone_bsp_0`, which must be configured to use "hypervisor guest" in the BSP configuration settings, which is edited by opening the `platform.spr` file under "standalone on psa_cortexa53_0" -> "Board Support Package" -> "Modify BSP Settings".

This will enable the EL-1 support required with Bl31 (ARM Trusted Firmware). The BSP generates a `include/bspconfig.h`, which should have these defines set:

```
#define EL1_NONSECURE 1
#define HYP_GUEST 1
```

You may need to adjust/add the following project settings under Properties -> C/C++ General:

1) Platform bspInclude path: "Paths and Symbols" -> "Includes" -> "GNU C" -> "Add" -> Workspace Path for platform (example: `/zcu102/export/zcu102/sw/zcu102/standalone_domain/bspinclude/include`).

2) Platform BSP Library path: See "Library Paths" -> "Add" (example: `/zcu102/psu_cortexa53_0/standalone_domain/bsp/psu_cortexa53_0/lib`).

## wolfBoot Configuration

A build settings template for Zynq UltraScale+ can be found here `./config/examples/zynqmp.config`. This file can be copied to wolfBoot root as `.config` for building from the command line.

```sh
$ cp ./config/examples/zynqmp.config .config
$ make keytools
```

These template settings are also in this `.cproject` as preprocessor macros. These settings are loaded into the `target.h.in` template by the wolfBoot `make`. If not using the built-in make then the following defines will need to be manually created in `target.h`:

```
#define WOLFBOOT_SECTOR_SIZE                 0x20000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x800000
#define WOLFBOOT_LOAD_ADDRESS                0x10000000
#define WOLFBOOT_PARTITION_SIZE              0x2A00000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x3A00000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x63E0000

#define WOLFBOOT_DTS_BOOT_ADDRESS            0x7E0000
#define WOLFBOOT_DTS_UPDATE_ADDRESS          0x39E0000
#define WOLFBOOT_LOAD_DTS_ADDRESS            0x11800000
```

The default .cproject build symbols are:

```
ARCH_AARCH64
ARCH_FLASH_OFFSET=0x0
CORTEX_A53
DEBUG_ZYNQ=1
EXT_FLASH=1
FILL_BYTE=0xFF
IMAGE_HEADER_SIZE=1024
MMU
NO_QNX
NO_XIP
PART_BOOT_EXT=1
PART_SWAP_EXT=1
PART_UPDATE_EXT=1
TARGET_zynq
WC_HASH_DATA_ALIGNMENT=8
WOLFBOOT_ARCH_AARCH64
WOLFBOOT_DUALBOOT
WOLFBOOT_ELF
WOLFBOOT_HASH_SHA3_384
WOLFBOOT_ORIGIN=0x0
WOLFBOOT_SHA_BLOCK_SIZE=4096
WOLFBOOT_SIGN_RSA4096
WOLFBOOT_UBOOT_LEGACY
```

Note: If not using Position Independent Code (PIC) the linker script `ldscript.ld` must have the start address offset to match the `WOLFBOOT_LOAD_ADDRESS`.


## Zynq UltraScale+ ARMv8 Crypto Extensions

To enable ARM assembly speedups for SHA:

1) Add these build symbols:

```
WOLFSSL_ARMASM
WOLFSSL_ARMASM_INLINE
```

2) Add these compiler misc flags: `-mcpu=generic+crypto -mstrict-align -DWOLFSSL_AARCH64_NO_SQRMLSH`


## Generate signing key

The keygen tool creates an RSA 4096-bit private key (`wolfboot_signing_private_key.der`) and exports the public key to `src/keystore.c` for wolfBoot to use at compile-time as the default root-of-trust.

```sh
$ ./tools/keytools/keygen --rsa4096 -g wolfboot_signing_private_key.der
Keytype: RSA4096
Generating key (type: RSA4096)
RSA public key len: 550 bytes
Associated key file:   wolfboot_signing_private_key.der
Partition ids mask:   ffffffff
Key type   :           RSA4096
Public key slot:       0
Done.
```

## Signing Example

```sh
$ ./tools/keytools/sign --rsa4096 --sha3 ../hello_world/Debug/hello_world.elf ./wolfboot_signing_private_key.der 1
wolfBoot KeyTools (Compiled C version)
wolfBoot version 2020000
Update type:          Firmware
Input image:          ../hello_world/Debug/hello_world.elf
Selected cipher:      RSA4096
Selected hash  :      SHA3
Public key:           ./wolfboot_signing_private_key.der
Output  image:        ../hello_world/Debug/hello_world_v1_signed.bin
Target partition id : 1
Found RSA512 key
image header size calculated at runtime (1024 bytes)
Calculating SHA3 digest...
Signing the digest...
Output image(s) successfully created.
```

## Bootgen

Xilinx uses a `bootgen` tool for generating a boot binary image that has Xilinx headers, which the FSBL (First Stage Boot Loader) understands. See the `boot.bif` and `boot_auth.bif` as examples.

* Use "partition_owner=uboot" to prevent a partition from being loaded into RAM.
* Use "offset=" option to place the application into a specific location in flash.
* Use "load=" option to have FSBL load into specific location in RAM.

Default install locations for bootgen tools:
* Linux: `/tools/Xilinx/Vitis/2022.1/bin`
* Windows: `C:\Xilinx\Vitis\2022.1\bin`

Open the Vitis Shell from the IDE by using file menu "Xilinx" -> "Vitis Shell".

Generating a boot.bin (from boot.bif).
Example boot.bif in workspace root:

```
// Boot BIF example for wolfBoot with signed Hello World
// Note: "partition_owner=uboot" prevents partition from being loaded to RAM
the_ROM_image:
{
	[bootloader, destination_cpu=a53-0] zcu102\zynqmp_fsbl\fsbl_a53.elf
	[destination_cpu=a53-0, exception_level=el-2] wolfboot\Debug\wolfboot.elf
	[destination_cpu=a53-0, partition_owner=uboot, offset=0x800000] hello_world\Debug\hello_world_v1_signed.bin
}
```

You can also use exception level 3 or 1 depending on your needs.

From the workspace root:

```sh
bootgen -image boot.bif -arch zynqmp -w -o BOOT.bin

****** Xilinx Bootgen v2022.1
  **** Build date : Apr 18 2022-16:02:32
    ** Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.

[INFO]   : Bootimage generated successfully
```

## Running Boot.bin

* QSPI: Flash using Vitis -> Xilinx (menu) -> Program Flash
* SD: or copy boot.bin to SDCARD

| Boot Mode | MODE Pins 3:0 | Mode SW6[4:1]  |
| --------- | ------------- | -------------- |
| JTAG      | 0 0 0 0       | on, on, on, on |
| QSPI32    | 0 0 1 0       | on, on, off,on |
| SD        | 1 1 1 0       | off,off,off,on |

## Example boot output

```
wolfBoot Secure Boot
Read FlashID Lower: Ret 0, 20 BB 20
Read FlashID Upper: Ret 0, 20 BB 20
Versions: Boot 1, Update 0
Trying Boot partition at 800000
Boot partition: 800000 (size 226024, version 0x1)
info: LMS wolfBoot_verify_signature
info: using LMS parameters: L2-H5-W8
info: wc_LmsKey_Verify returned OK
Successfully selected image in part: 0
Firmware Valid
Loading flash image from 8014A8 to RAM at 10000000 (226024 bytes)
Loading elf at 10000000
Found valid elf64 (little endian)
Program Headers 2 (size 56)
Load 57536 bytes (offset 10000) to 0 (p 0)
Clear 20600 bytes at 0 (p 0)
Entry point 0
DTB boot partition: 7B0000
Failed parsing DTB to load
Booting at 0
Hello World

Successfully ran Hello World application
```


### Adding RSA Authentication

1. Generate keys:
    * `bootgen.exe -generate_keys auth pem -arch zynqmp -image boot.bif`
2. Create hash for primary key:
    * `bootgen.exe -image boot.bif -arch zynqmp -w -o i BOOT.BIN -efuseppkbits ppkf_hash.txt`
3. Import example project for programming eFuses:
    * New BSP project (program efuses , ZCU102_hw_platform, standalone, CPU: PSU_cortexa53_0)
    * Goto Xilinx Board Support Packet Settings.
    * Scroll down to Supported Libraries and Check the xiskey library
    * In the system.mss pane, scroll down to Libraries and click Import Examples.
    * Check the xilskey_esfuseps_zynqmp_example
4. Edit `xilskey_efuseps_zynqmp_input.h`
    * 433 `#define XSK_EFUSEPS_WRITE_PPK0_HASH  TRUE`
    * 453 `#define XSK_EFUSEPS_PPK0_IS_SHA3     TRUE`
    * 454 `#define XSK_EFUSEPS_PPK0_HASH "0000000000000000000000000000000000000000000000000000000000000000" /* from ppkf_hash.txt */``
5. Update boot.bif (see boot_auth.bif)

    ```
    [auth_params] ppk_select=0; spk_id=0x00000000
    [pskfile] pskf.pem
    [sskfile] sskf.pem
    authentication=rsa
    ```

6. Build “boot.bin” image:
    * `bootgen -image boot.bif -arch zynqmp -o i BOOT.BIN -w`

Note: To generate a report of a boot.bin use the `bootgen_utility` or after 2022.1 use `bootgen -read`:
`bootgen -arch zynqmp -read BOOT.BIN`

## Post Quantum

### PQ XMSS

1) Add these build symbols to the Xilinx project:
Note: Make sure and remove the existing `WOLFBOOT_SIGN_*`, `WOLFBOOT_HASH_*` and `IMAGE_HEADER_SIZE`

```
WOLFBOOT_SIGN_XMSS
WOLFBOOT_HASH_SHA256
WOLFSSL_HAVE_XMSS
WOLFSSL_WC_XMSS
WOLFSSL_WC_XMSS_SMALL
WOLFBOOT_XMSS_PARAMS='\"XMSS-SHA2_10_256\"'
WOLFSSL_XMSS_VERIFY_ONLY
WOLFSSL_XMSS_MAX_HEIGHT=32
WOLFBOOT_SHA_BLOCK_SIZE=4096
IMAGE_SIGNATURE_SIZE=2500
XMSS_IMAGE_SIGNATURE_SIZE=2500
IMAGE_HEADER_SIZE=5000
```

2) Create and sign image:

```sh
$ ./tools/keytools/keygen --xmss -g wolfboot_signing_private_key.der
Keytype: XMSS
Generating key (type: XMSS)
info: using XMSS parameters: XMSS-SHA2_10_256
Associated key file:   wolfboot_signing_private_key.der
Partition ids mask:   ffffffff
Key type   :           XMSS
Public key slot:       0
Done.

$ ./tools/keytools/sign --xmss ../hello_world/Debug/hello_world.elf wolfboot_signing_private_key.der 1
wolfBoot KeyTools (Compiled C version)
wolfBoot version 2020000
Update type:          Firmware
Input image:          ../hello_world/Debug/hello_world.elf
Selected cipher:      XMSS
Selected hash  :      SHA256
Public key:           wolfboot_signing_private_key.der
Output  image:        ../hello_world/Debug/hello_world_v1_signed.bin
Target partition id : 1
info: using XMSS parameters: XMSS-SHA2_10_256
info: XMSS signature size: 2500
info: xmss sk len: 1343
info: xmss pk len: 68
Found XMSS key
image header size calculated at runtime (5000 bytes)
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
```

### PQ LMS

1) Add these build symbols to the Xilinx project:
Note: Make sure and remove the existing `WOLFBOOT_SIGN_*`, `WOLFBOOT_HASH_*` and `IMAGE_HEADER_SIZE`

```
WOLFBOOT_SIGN_LMS
WOLFBOOT_HASH_SHA256
WOLFSSL_HAVE_LMS
WOLFSSL_WC_LMS
WOLFSSL_WC_LMS_SMALL
WOLFSSL_LMS_VERIFY_ONLY
WOLFSSL_LMS_MAX_LEVELS=2
WOLFSSL_LMS_MAX_HEIGHT=5
LMS_LEVELS=2
LMS_HEIGHT=5
LMS_WINTERNITZ=8
IMAGE_SIGNATURE_SIZE=2644
IMAGE_HEADER_SIZE=5288
```
2) Create and sign image:

```sh
$ ./tools/keytools/keygen --lms -g wolfboot_signing_private_key.der
Keytype: LMS
Generating key (type: LMS)
info: using LMS parameters: L2-H5-W8
Associated key file:   wolfboot_signing_private_key.der
Partition ids mask:   ffffffff
Key type   :           LMS
Public key slot:       0
Done.

$ ./tools/keytools/sign --lms ../hello_world/Debug/hello_world.elf wolfboot_signing_private_key.der 1
wolfBoot KeyTools (Compiled C version)
wolfBoot version 2020000
Update type:          Firmware
Input image:          ../hello_world/Debug/hello_world.elf
Selected cipher:      LMS
Selected hash  :      SHA256
Public key:           wolfboot_signing_private_key.der
Output  image:        ../hello_world/Debug/hello_world_v1_signed.bin
Target partition id : 1
info: using LMS parameters: L2-H5-W8
info: LMS signature size: 2644
Found LMS key
image header size calculated at runtime (5288 bytes)
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
```


### References:
* [ZAPP1319](https://www.xilinx.com/support/documentation/application_notes/xapp1319-zynq-usp-prog-nvm.pdf): Programming BBRAM and eFUSEs
* [UG1283](https://www.xilinx.com/support/documentation/sw_manuals/xilinx2018_2/ug1283-bootgen-user-guide.pdf): Bootgen User Guide
* [Using Cryptography in Zynq UltraScale MPSoC](https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/18842541/Using+Cryptography+in+Zynq+UltraScale+MPSoC)
