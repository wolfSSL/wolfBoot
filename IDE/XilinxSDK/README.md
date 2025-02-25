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

By default the ARM assembly speedups for SHA will be enabled. This uses inline assembly in wolfcrypt/src/port/arm/ and the armb8 crypto extensions. To disable set `NO_ARM_ASM=1`.


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

You can use exception level 3, 2 or 1 depending on your needs. See hal/zynq.h options EL3_SECURE, EL2_HYPERVISOR and EL1_NONSECURE for enabled/disabling entry support for each. Default is support for EL2.

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
    * `bootgen.exe -generate_keys auth pem -arch zynqmp -image boot_auth.bif`
2. Create hash for primary key:
    * `bootgen.exe -image boot_auth.bif -arch zynqmp -w -o i BOOT.BIN -efuseppkbits ppkf_hash.txt`
3. Import example project for programming eFuses:
    * New BSP project (program efuses , ZCU102_hw_platform, standalone, CPU: PSU_cortexa53_0)
    * Goto Xilinx Board Support Packet Settings.
    * Scroll down to Supported Libraries and Check the xiskey library
    * In the system.mss pane, scroll down to Libraries and click Import Examples.
    * Check the xilskey_esfuseps_zynqmp_example
4. Edit `xilskey_efuseps_zynqmp_input.h`
    * 433 `#define XSK_EFUSEPS_WRITE_PPK0_HASH  TRUE`
    * 453 `#define XSK_EFUSEPS_PPK0_IS_SHA3     TRUE`
    * 454 `#define XSK_EFUSEPS_PPK0_HASH "0000000000000000000000000000000000000000000000000000000000000000" /* from ppkf_hash.txt */`
5. Update boot.bif (see boot_auth.bif)

    ```
    [auth_params] ppk_select=0; spk_id=0x00000000
    [pskfile] pskf.pem
    [sskfile] sskf.pem
    authentication=rsa
    ```

6. Build “boot.bin” image:
    * `bootgen -image boot_auth.bif -arch zynqmp -o i BOOT.BIN -w`

Note: During testing add `[fsbl_config] bh_auth_enable` to allow skipping of the eFuse check of the PPK hash. In production the RSA_EN eFuses must be blown to force checking of the PPK hash.

Note: To generate a report of a boot.bin use the `bootgen_utility` or after 2022.1 use `bootgen -read`:
`bootgen -arch zynqmp -read BOOT.BIN`


## CSU Support

The Configuration Security Unit (CSU) is a dedicate core that contains security functions like PUF, SHA3, RSA, Tamper Protection. These registers can only be accessed through the PMU, which is a separate dedicated core. If operating from LE2 or lower the calls must be done through the BL31 (TF-A) SIP service to elevate privledges.

Access to most CSU registers can be done by setting the `-DSECURE_ACCESS_VAL=1` build option.

In PetaLinux menuconfig under PMU Configuration add compiler flag `-DSECURE_ACCESS_VAL=1`.

```sh
petalinux-build -c pmufw
```

### CSU PUF

The PUF (Physically Unclonable Function) provides a way to generate a unique key for encryption specific to the device. It is useful for wrapping other keys to pair/bind them and allows external storage of the encrypted key.

This feature is enabled with `CFLAGS_EXTRA+=-DCSU_PUF_ROT`.

For PUF functionality a patch must be applied to the PMUFW to enable access to the PUF registers. See `pm_mmio_access.c` patch below:

```
+	/* CSU PUF Registers */
+	{
+		.startAddr = ( ( CSU_BASEADDR ) + 0X00004000 ),
+		.endAddr = ( ( CSU_BASEADDR ) + 0X00004018 ),
+		.access = MMIO_ACCESS_RW(IPI_PMU_0_IER_APU_MASK |
+					 IPI_PMU_0_IER_RPU_0_MASK |
+					 IPI_PMU_0_IER_RPU_1_MASK),
+	},
```

Example PUF Generation Output:

```
wolfBoot Secure Boot
Current EL: 2
QSPI Init: Ref=300MHz, Div=8, Bus=37500000, IO=DMA
Read FlashID Lower: Ret 0, 20 BB 22
Read FlashID Upper: Ret 0, 20 BB 22
PMUFW Ver: 1.1
CSU ID 0x24738093, Ver 0x00000003
PUF Status 0x00000002
eFuse SEC_CTRL 0x00000000
eFuse PUF CHASH 0x00000000, AUX 0x00000000
CSU Puf Register
Ret 0, Syndrome 1544, CHASH 0x9B7A8C30, AUX 0x005AE021
4662F39BEC998700E2D299E15B30F1AF563FC596A3854ACE05FDA4FBC2AD32DF9B66E2081A55D8CA0CB84B88735B005548C0671BD561AE7A9FBB266E228368F6D1FD916C8D172572094E210826106B8C80AB1E8910647283DB22076560FC5E10C02C614F4EF80001B218501AD9D07580C2DB47E487940DB24615509EBE85B037AB2FCDE820661EAB45C345863735F64689AECCEAE783DC413052E615B231931E265EC00C15D8FCD2D83E9F8FC836178C0415587E683D48A7ADADC1B53E17743859CA5984A314D1CC85AF58226376E705AC6C2973ACCE05FF2263DBE951A2D0FFD9D218C57F1D1398640E5A3D03BB9478530035952642C40258AFEB196F0A6D130BC8C2E064C622AC9FA0827BBFCF35FCBAFC9593085D1D296F8446C3550784DECF964B4A65249E9A002A7CEE9886DBB7F33DC3A7404C66497218FF7762E4C81B048986BF9257968FF92D6F7D1AACB1A8CDE48E8ECDD8B7A74C3C16FF10FD00D119E3B5F4DB19854AF60C2B063CB26A3740FC8658425EB44F27B52CDD6C1A16E3C45394BA9629B0A8A0410979CA0ED488E787F36B4BC05E1CEBD8F5CFF6DAF3D3F8B885E6F0A616675DA0B748F3C4E334545920C3062113230741795AA97D4A32B7E1BC4953E588902C7915CD362B75DA33BB941B523932B172589E27759A924A857CD66EE0E02BE697188776E4A646F7A73203E769285BF9FD09562A67CC67E2EB6CC7D8F15C138991AC177030D9057750A70E0C4F6144A8412F436A8B952597D664BADF3997DC0249DB4ABF6355DEB09B7A8C300000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000009B7A8C30005AE021
CSU Register PUF 0: 281ms
Regen: PUF Status 0x05AE0218
CSU Regen PUF 0: 8ms
Red Key 32
64F03AFD7D0C70D2591CDF34305F7B8A5BA4593C0A0E1B8C5ECDFF9F5900192C
Black IV 16
D142AC7C560F158BA95A213100000000
Black Key 32
2599B8619240E98E264A0CE3CD42C58A9E3457F1982D1DEEE1FC75A1A1284C72
```

Example .bif that includes the PUF helper data and black key/iv. This enables the CSU boot ROM to load up the black AES key for use with the CSU AES engine.

```
the_ROM_image:
{
	// Boot Header Authentication Enable
	[fsbl_config] a53_x64, bh_auth_enable, puf4kmode, shutter=0x0100005E, pufhd_bh
	[keysrc_encryption] bh_blk_key
	[puf_file] helperdata.txt
	[bh_key_iv] black_iv.txt
	[bh_keyfile] black_key.txt

	// Use the primary public key 0 and secondary public key id 0
	[auth_params] ppk_select=0; spk_id=0x00000000

	// primary and secondary secret (private) keys
	[pskfile] pskf.pem
	[sskfile] sskf.pem

	[bootloader, authentication=rsa, encryption=aes, destination_cpu=a53-0] zynqmp_fsbl.elf
	[destination_cpu=pmu, authentication=rsa] pmufw.elf
	[destination_device=pl, authentication=rsa] system.bit
	[destination_cpu=a53-0, authentication=rsa, exception_level=el-3, trustzone] bl31.elf
	[destination_cpu=a53-0, authentication=rsa, load=0x00100000] system.dtb
	[destination_cpu=a53-0, authentication=rsa, exception_level=el-2] wolfboot.elf
	[destination_cpu=a53-0, partition_owner=uboot, offset=0x800000] hello_world_v1_signed.bin
}
```

Generated BOOT.BIN using: `bootgen -image bootgen.bif -arch zynqmp -o BOOT.BIN -w -p xzcu9eg`

This will create an encryption key file `zynqmp_fsbl.nky`.


### CSU JTAG Enable

When RSA authentication is enabled the JTAG feature is disabled in the PMU. To re-enable it (assuming eFuse allows it) build with `CFLAGS_EXTRA+=-DDEBUG_CSU=2` and apply the PMUFW patches below.

To patch the PMUFW from PetaLinux use the following steps (for 2022.1 or later):

Based on instructions from: https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/2587197506/Zynq+UltraScale+MPSoC+JTAG+Enable+in+U-Boot

`pim/project-spec/meta-user/recipes-bsp/embeddedsw/pmu-firmware_%.bbappend`:

```
# Patch for PMUFW

SRC_URI_append = " file://0001-csu-regs.patch"
FILESEXTRAPATHS_prepend := "${THISDIR}/files:"
```

`pim/project-spec/meta-user/recipes-bsp/embeddedsw/files/0001-csu-regs.patch`:
```
diff --git a/lib/sw_apps/zynqmp_pmufw/src/pm_mmio_access.c b/lib/sw_apps/zynqmp_pmufw/src/pm_mmio_access.c
index 73066576a5..ce9490916d 100644
--- a/lib/sw_apps/zynqmp_pmufw/src/pm_mmio_access.c
+++ b/lib/sw_apps/zynqmp_pmufw/src/pm_mmio_access.c
@@ -99,6 +99,22 @@ static const PmAccessRegion pmAccessTable[] = {
 					 IPI_PMU_0_IER_RPU_1_MASK),
 	},

+	/* WOLF: Adding DBG_LPD_CTRL and RST_LPD_DBG to support JTAG Enable in u-boot */
+	{
+		.startAddr = CRL_APB_DBG_LPD_CTRL,
+		.endAddr = CRL_APB_DBG_LPD_CTRL,
+		.access = MMIO_ACCESS_RW(IPI_PMU_0_IER_APU_MASK |
+					 IPI_PMU_0_IER_RPU_0_MASK |
+					 IPI_PMU_0_IER_RPU_1_MASK),
+	},
+	{
+		.startAddr = CRL_APB_RST_LPD_DBG,
+		.endAddr = CRL_APB_RST_LPD_DBG,
+		.access = MMIO_ACCESS_RW(IPI_PMU_0_IER_APU_MASK |
+					 IPI_PMU_0_IER_RPU_0_MASK |
+					 IPI_PMU_0_IER_RPU_1_MASK),
+	},
+
 	/* PMU's global Power Status register*/
 	{
 		.startAddr = PMU_GLOBAL_PWR_STATE,
@@ -415,15 +431,24 @@ static const PmAccessRegion pmAccessTable[] = {
 					 IPI_PMU_0_IER_RPU_1_MASK),
 	},

+	/* WOLF: separate CSU_JTAG_CHAIN_CFG so it can be made RW */
 	/* CSU ier register*/
 	{
 		.startAddr = CSU_IER,
-		.endAddr = CSU_JTAG_CHAIN_CFG,
+		.endAddr = CSU_IDR,
 		.access = MMIO_ACCESS_WO(IPI_PMU_0_IER_APU_MASK |
 					 IPI_PMU_0_IER_RPU_0_MASK |
 					 IPI_PMU_0_IER_RPU_1_MASK),
 	},

+	{
+		.startAddr = CSU_JTAG_CHAIN_CFG,
+		.endAddr = CSU_JTAG_CHAIN_CFG,
+		.access = MMIO_ACCESS_RW(IPI_PMU_0_IER_APU_MASK |
+					 IPI_PMU_0_IER_RPU_0_MASK |
+					 IPI_PMU_0_IER_RPU_1_MASK),
+	},
+
 	/* CSU idr register*/
 	{
 		.startAddr = CSU_IDR,
@@ -504,6 +529,15 @@ static const PmAccessRegion pmAccessTable[] = {
 					 IPI_PMU_0_IER_RPU_1_MASK),
 	},

+	/* CSU PUF Registers */
+	{
+		.startAddr = ( ( CSU_BASEADDR ) + 0X00004000 ),
+		.endAddr = ( ( CSU_BASEADDR ) + 0X00004018 ),
+		.access = MMIO_ACCESS_RW(IPI_PMU_0_IER_APU_MASK |
+					 IPI_PMU_0_IER_RPU_0_MASK |
+					 IPI_PMU_0_IER_RPU_1_MASK),
+	},
+
 	/*CSU tamper-status register */
 	{
 		.startAddr = CSU_TAMPER_STATUS,
```

Then rebuild PMUFW: `petalinux-build -c pmufw`


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
