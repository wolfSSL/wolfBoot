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

A build settings template for Zynq UltraScale+ can be found here `./config/examples/zynqmp.config`. This file can be copied to wolfBoot root as `.config` for building from the command line. These template settings are also in this `.cproject` as preprocessor macros. These settings are loaded into the `target.h.in` template by the wolfBoot `make`. If not using the built-in make then the following defines will need to be manually created in `target.h`:

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

Note: If not using Position Independent Code (PIC) the linker script `ldscript.ld` must have the start address offset to match the `WOLFBOOT_LOAD_ADDRESS`.

## Signing Example

```sh
$ make keytools
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

Generating a boot.bin (from boot.bif).
Run the Xilinx -> Vitis Shell and cd into the workspace root.

Example boot.bif in workspace root:

```
// Boot BIF example for wolfBoot with signed Hello World
// Note: "partition_owner=uboot" prevents partition from being loaded to RAM
the_ROM_image:
{
	[bootloader, destination_cpu=a53-0] zcu102\zynqmp_fsbl\fsbl_a53.elf
	[destination_cpu=a53-0, exception_level=el-1] wolfboot\Debug\wolfboot.elf
	[destination_cpu=a53-0, partition_owner=uboot, offset=0x800000] hello_world\Debug\hello_world_v1_signed.bin
}
```

```sh
bootgen -image boot.bif -arch zynqmp -o BOOT.bin

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
    * `bootgen.exe -image boot.bif -arch zynqmp -o i BOOT.BIN -w`

Note: To generate a report of a boot.bin use the `bootgen_utility`:
`bootgen_utility -arch zynqmp -bin boot.bin -out boot.bin.txt`

### References:
* [ZAPP1319](https://www.xilinx.com/support/documentation/application_notes/xapp1319-zynq-usp-prog-nvm.pdf): Programming BBRAM and eFUSEs
* [UG1283](https://www.xilinx.com/support/documentation/sw_manuals/xilinx2018_2/ug1283-bootgen-user-guide.pdf): Bootgen User Guide
