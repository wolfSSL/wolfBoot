# WolfBoot on Infineon AURIX TC3xx

This example demonstrates using wolfBoot on the Infineon AURIX TC3xx family of microcontrollers. The example is based on the TC375 Lite-Kit V2, but should be easily adaptable to other TC3xx devices. This README assumes basic familiarity with the TC375 SoC, the AURIX IDE, and Lauterbach Trace32 debugger.

## Overview

- [WolfBoot on Infineon AURIX TC3xx](#wolfboot-on-infineon-aurix-tc3xx)
  - [Overview](#overview)
  - [Important notes](#important-notes)
  - [Flash Partitioning](#flash-partitioning)
    - [Standard wolfBoot images](#standard-wolfboot-images)
    - [ELF files](#elf-files)
    - [Cert Chain Verification](#cert-chain-verification)
  - [Configuration and the wolfBoot AURIX tool (wbaurixtool.sh)](#configuration-and-the-wolfboot-aurix-tool-wbaurixtoolsh)
  - [Building and running the wolfBoot demo](#building-and-running-the-wolfboot-demo)
    - [Prerequisites](#prerequisites)
    - [Important notes](#important-notes-1)
    - [Clone wolfBoot](#clone-wolfboot)
    - [Build wolfBoot keytools and generate keys](#build-wolfboot-keytools-and-generate-keys)
    - [Install the Infineon TC3xx SDK into the wolfBoot project](#install-the-infineon-tc3xx-sdk-into-the-wolfboot-project)
    - [Build wolfBoot](#build-wolfboot)
    - [Connect the Lauterbach to the TC375 Device in TRACE32](#connect-the-lauterbach-to-the-tc375-device-in-trace32)
    - [Update the start address in UCBs using TRACE32](#update-the-start-address-in-ucbs-using-trace32)
    - [Load and run the wolfBoot demo in TRACE32](#load-and-run-the-wolfboot-demo-in-trace32)
  - [wolfHSM Compatibility](#wolfhsm-compatibility)
    - [Building wolfBoot with wolfHSM](#building-wolfboot-with-wolfhsm)
    - [Building wolfBoot with wolfHSM and cert chain verification](#building-wolfboot-with-wolfhsm-and-cert-chain-verification)
    - [Custom Certificate Chain](#custom-certificate-chain)
    - [Dummy Certificate Chain](#dummy-certificate-chain)
  - [Building: Command Sequence](#building-command-sequence)
  - [Troubleshooting](#troubleshooting)
    - [WSL "bad interpreter" error](#wsl-bad-interpreter-error)
    - [Post Quantum: ML-DSA](#post-quantum-ml-dsa)
      - [ML-DSA Keytools](#ml-dsa-keytools)

The example contains two projects: `wolfBoot-tc3xx` and `test-app`. The `wolfBoot-tc3xx` project contains the wolfBoot bootloader, and the `test-app` project contains a simple firmware application that will be loaded and executed by wolfBoot. The `test-app` project is a simple blinky application that blinks LED1 on the TC375 Lite-Kit V2 once per second when running the base image, and rapidly (~3x/sec) when running the update image. The test app determines if it is a base or update image by inspecting the firmware version (obtained through the wolfBoot API). The firmware version is set in the image header by the wolfBoot keytools when signing the test app binaries. The same test app binary is used for both the base and update images, with the only difference being the firmware version set by the keytools.

This example supports loading the test application firmware as standard wolfBoot images or as an ELF file. Refer to the [firmware_update.md](../docs/firmware_update.md) documentation for more information on how the wolfBoot flash loader supports loading ELF files.

## Important notes

- In the TC375 UCBs, BMDHx.STAD must point to the wolfBoot entrypoint `0xA00A_0000`. You can modify this in the `UCB` section of the TRACE32 IDE as described in the steps later in this document. Please refer to the TRACE32 manual and the TC37xx user manual for more information on the UCBs.
- Because TC3xx PFLASH ECC prevents reading from erased flash, the `EXT_FLASH` option is used to redirect flash reads to the `ext_flash_read()` HAL API, where the flash pages requested to be read can be blank-checked by hardware before reading.
- TC3xx PFLASH is write-once (`NVM_FLASH_WRITEONCE`), however wolfBoot `NVM_FLASH_WRITEONCE` does not support `EXT_FLASH`. Therefore the write-once functionality is re-implemented in the `HAL` layer.
- This demo app is only compatible with the GCC toolchain build configurations shipped with the AURIX IDE. The TASKING compiler build configurations are not yet supported.
- When detailing commands to be run, square brackets `[]` are used to indicate optional arguments. Do not include the square brackets when running the commands.

## Flash Partitioning

The TC3xx AURIX port of wolfBoot places all images in PFLASH, and uses both PFLASH0 and PFLASH1 banks. The wolfBoot executable code and the image swap sector are located in PFLASH0, with the remainder available for use. The layout of PFLASH1 depends on whether the test application is being loaded as a standard wolfBoot image or an ELF file.

### Standard wolfBoot images

When configured to load standard wolfBoot images, the demo application divides PFLASH1 approximately in half, with the first half holding the BOOT partition and the second half holding the UPDATE partition. User firmware images are directly executed in place from the BOOT partition in PFLASH1, and so must be linked to execute within this address space, with an offset of `IMAGE_HEADER_SIZE` to account for the wolfBoot image header. The last sector of PFLASH1 is reserved for the SWAP area.

```
+==========+
| PFLASH0  |
+----------+ <-- 0x8000_0000
| Unused   |        640K
+==========+ <-- 0x800A_0000
| wolfBoot |        172K
+----------+ <-- 0x8002_B000
| Unused   |       ~2.8M
+----------+ <-- 0x8030_0000

+==========+
| PFLASH1  |
+==========+ <-- 0x8030_0000
| BOOT     |        1.5M (0x17E000)
+----------+ <-- 0x8047_E000
| UPDATE   |        1.5M (0x17E000)
+----------+ <-- 0x805F_C000
| SWAP     |        16K (0x4000)
+----------+ <-- 0x8060_0000
```

### ELF files

When loading the test app as an ELF file, PFLASH1 is divided into three sections. Approximately the first half of PFLASH1 is reserved for application use. This is the region that the application should be linked to execute from, and should contain all loadable segments in the ELF file. The second half of PFLASH1 is divided equally between the BOOT and UPDATE partitions as with standard wolfBoot images, with the last sector reserved for SWAP.

```
+==========+
| PFLASH1  |
+==========+ <-- 0x8030_0000
| APP      |        ~1.5M (0x17_C000)
+----------+ <-- 0x8047_C000
| BOOT     |        ~0.75M (0xC_0000)
+----------+ <-- 0x8053_C000
| UPDATE   |        ~0.75M (0xC_0000)
+----------+ <-- 0x805F_C000
| SWAP     |        16K (0x4000)
+----------+ <-- 0x8060_0000
```

Different linker script templates are used to configure the memory layout via `wbaurixtool.sh` (see next section) depending on whether standard wolfBoot images or ELF files are being loaded. Please refer to the following linker script templates for the exact memory configuration:

- [wolfBoot](wolfBoot-tc3xx/Lcf_Gnu_Tricore_Tc.lsl.in)
- [test-app (standard wolfBoot images)](test-app/Lcf_Gnu_Tricore_Tc.lsl.in)
- [test-app (ELF files)](test-app/Lcf_Gnu_Tricore_elf.lsl.in)

### Cert Chain Verification

wolfBoot on AURIX supports verifying firmware images using certificate chains. For more information on how this wolfBoot feature works, refer to [Signing.md](../../docs/Signing.md), [firmware_update.md](../docs/firmware_update.md), and [wolfBoot-wolfHSM](../../IDE/AURIX/wolfBoot-wolfHSM). Currently this feature can only be used in conjunction with wolfHSM. Instructions for using this feature are detailed below in the [Building wolfBoot with wolfHSM](#building-wolfboot-with-wolfhsm) section.

## Configuration and the wolfBoot AURIX tool (wbaurixtool.sh)

wolfBoot relies extensively on its build system in order to properly set configuration macros, linker addresses, and other target-specific items. Because wolfBoot on AURIX uses the AURIX studio IDE to build, changing things like the signature algorithm would require manually editing IDE settings, linker scripts, etc. which is error prone and tedious. The `wbaurixtool.sh` script provides a single tool that automates the generation of all configurable items required for building wolfBoot and the test application on AURIX given the chosen signature and hashing algorithms, including managing all configuration macros, linker scripts, as well as handling the actual key generation and image signing process. `wbaurixtool.sh` can also generate wolfHSM NVM images containing the generated image signing key when used in conjunction with wolfHSM.

The general usage of `wbaurixtool.sh` is as follows:

```
$ ./wbaurixtool.sh [global options] <subcommand> [subcommand options]
```

where `[global options]` are:

- `--hsm`: Use the wolfHSM AURIX projects instead of the standard projects (e.g. `wolfBoot-tc3xx-wolfHSM` and `test-app-wolfHSM`)
- `--elf`: loads the test application as an ELF file instead of the standard wolfBoot image. If you wish to load the test application as an ELF file, all invocations of `wbaurixtool.sh` must use the `--elf` option.

and where `<subcommand>` is one of the following:

- `keygen`: Generate a new signing key pair
- `sign`: Sign a firmware image
- `target`: Generate the `target.h` header file for the tc375 flash configuration
- `macros`: Generate the `wolfBoot_macros.txt` file from the `wolfBoot_macros.in` template in the `wolfBoot-tc3xx` directory
- `nvm`: Generate a wolfHSM NVM image containing the signing key, based on the nvm configuration file `tools/scripts/tc3xx/wolfBoot-wolfHSM-keys.nvminit`
- `lcf`: Generate the `Lcf_Gnu_Tricore_Tc.lsl` file from the `Lcf_Gnu_Tricore_Tc.lcf.in` template in the `test-app` directory
- `clean`: Remove all generated keys and images

The `keygen`, `sign`, `macros` and `lcf` commands will use `ecc256` as the default signature algorithm, and `sha256` as the default hashing algorithm. Each subcommand can take the `--sign-algo` and `--hash-algo` options to specify different algorithms.

Subcommands can be chained together, and can inherit options from previous commands. For example, `./wbaurixtool.sh keygen --sign-algo ecc256 macros lcf` will generate a new signing key pair using ECC 256, generate the `wolfBoot_macros.txt` file from the `wolfBoot_macros.in` template in the `wolfBoot-tc3xx` directory, and generate the `Lcf_Gnu_Tricore_Tc.lsl` file from the `Lcf_Gnu_Tricore_Tc.lcf.in` template in the `test-app` directory. If the global option `--hsm` is specified, then the subsequent subcommands will apply to the wolfHSM AURIX projects instead of the standard projects (e.g. `wolfBoot-tc3xx-wolfHSM` and `test-app-wolfHSM`).

`wbaurixtool.sh` can also be used to generate wolfHSM NVM images containing the generated image signing key via the `nvm` subcommand when used in conjunction with wolfHSM. See the [wolfHSM Compatibility](#wolfhsm-compatibility) section for more information.

For more information on the `wbaurixtool.sh` script, run `./wbaurixtool.sh --help` for a full list of options and subcommands.

## Building and running the wolfBoot demo

### Prerequisites

- A Windows 10 computer with the Infineon AURIX IDE installed
- A WSL2 distro (tested on Ubuntu 22.04) with the `build-essential` package installed (`sudo apt install build-essential`)
- A TC375 AURIX Lite-Kit V2

### Important notes

- If you wish to load the test application as an ELF file, all invocations of `wbaurixtool.sh` must use the global `--elf` option before any subcommand. Otherwise, the invocation to `wbaurixtool.sh` will execute its commands as if the test application was a standard wolfBoot image. Mixing and matching `--elf` invocations with non-`--elf` invocations to `wbaurixtool.sh` will result in difficult to diagnose build or runtime errors. When in doubt, clean all build artifacts and start fresh.

### Clone wolfBoot

1. Clone the wolfBoot repository and initialize the repository submodules (`git submodule update --init`)

### Build wolfBoot keytools and generate keys

1. Open a WSL2 terminal and navigate to the top level `wolfBoot` directory
2. Compile the keytools by running `make keytools`
3. Use the helper script to generate a new signing key pair using ECC 256
    1. Navigate to `wolfBoot/tools/scripts/tc3xx`
    2. Run `./wbaurixtool.sh [--elf] keygen --sign-algo ecc256 macros lcf`. This:
        - Generates the signing private key `wolfBoot/priv.der` and adds the public key to the wolfBoot keystore (see [keygen](https://github.com/wolfSSL/wolfBoot/blob/aurix-tc3xx-support/docs/Signing.md) for more information)
        - Generates the `wolfBoot_macros.txt` file from the `wolfBoot_macros.in` template in the `wolfBoot-tc3xx` directory, which sets the appropriate wolfBoot preprocessor macros based on the hash and signature algorithms. The `wolfBoot_macros.txt` file is then passed to the compiler in the AURIX project
        - Generates the `Lcf_Gnu_Tricore_Tc.lsl` file from either the `Lcf_Gnu_Tricore_Tc.lcf.in` template in the `test-app` directory or the `Lcf_Gnu_Tricore_elf.lcf.in` template in the `test-app` directory, which sets appropriate values for Linker addresses (e.g. wolfBoot header size) based on the selected signature algorithm. The `Lcf_Gnu_Tricore_Tc.lsl` file is then passed to the linker in the AURIX project
        - Note that if you already have generated keys, you can use `./wbaurixtool.sh clean` to remove them first

```
$ ./wbaurixtool.sh keygen --sign-algo ecc256 macros lcf
Generating keys with algorithm: ecc256
Keytype: ECC256
Generating key (type: ECC256)
Associated key file:   priv.der
Partition ids mask:   ffffffff
Key type   :           ECC256
Public key slot:       0
Done.
Generating macros file with sign_algo=ecc256, hash_algo=sha256
Generating LCF file with header_size=256
```

### Install the Infineon TC3xx SDK into the wolfBoot project

Because of repository size constraints and differing licenses, the required Infineon low level drivers ("iLLD") and auto-generated SDK configuration code that are usually included in AURIX projects are not included in this demo app. It is therefore required to locate them in your AURIX install and extract them to the location that the wolfBoot AURIX projects expect them to be at. The remainder of these instructions will use variables to reference the following three paths:

- `$AURIX_INSTALL`: The AURIX IDE installation location. This is usually `C:\Infineon\AURIX-Studio-<version>`
- `$SDK_ARCHIVE`: The zip archive of the iLLD SDK. This is usually at `$AURIX_INSTALL\build_system\bundled-artefacts-repo\project-initializer\tricore-tc3xx\<version>\iLLDs\Full_Set\iLLD_<version>__TC37A.zip`
- `$SDK_CONFIG`: The directory containing the iLLD SDK configuration for the specific chip. This is usually at `$AURIX_INSTALL\build_system\bundled-artefacts-repo\project-initializer\tricore-tc3xx\<version>\ProjectTemplates\TC37A\TriCore\Configurations`

Perform the following two steps to add the iLLD SDK drivers to the wolfBoot project:

1. Extract the iLLD package for the TC375TP from `$SDK_ARCHIVE` into the `wolfBoot/IDE/AURIX/SDK` directory. The contents of the `wolfBoot/IDE/AURIX/SDK` directory should now be:

```
wolfBoot/IDE/AURIX/SDK
├── Infra/
├── Service/
├── iLLD/
└── placeholder.txt
```

2. Copy the SDK configuration sources from `$SDK_CONFIG` into the `wolfBoot/IDE/AURIX/Configurations` directory. The contents of the `wolfBoot/IDE/AURIX/Configurations` directory should now be:

```
wolfBoot/IDE/AURIX/Configurations/
├── Debug
├── Ifx_Cfg.h
├── Ifx_Cfg_Ssw.c
├── Ifx_Cfg_Ssw.h
├── Ifx_Cfg_SswBmhd.c
└── placeholder.txt
```

### Build wolfBoot
1. Generate the 'target.h` header file for the tc375 flash configuration
    1. Open a WSL terminal and navigate to `wolfBoot/tools/scripts/tc3xx`
    2. Run `./wbaurixtool.sh [--elf] target`
2. Open the AURIX IDE and create a new workspace directory, if you do not already have a workspace you wish to use
3. Import the wolfBoot project
    1. Click "File" -> Open Projects From File System"
    2. Click "Directory" to select an import source, and choose the wolfBoot/IDE/AURIX/wolfBoot-tc3xx directory in the system file explorer
    3. Click "Finish" to import the project
4. Build the wolfBoot Project
    1. Right-click the wolfBoot-tc3xx project and choose "Set active project"
    2. Right-click the wolfBoot-tc3xx project, and from the "Build Configurations" -> "Set Active" menu, select either the "TriCore Debug (GCC)" or "TriCore Release (GCC)" build configuration
    3. Click the hammer icon to build the active project. This will compile wolfBoot.
5. Import the test-app project using the same procedure as in step (3), except using `wolfBoot/IDE/AURIX/test-app` as the directory
6. Build the test-app project using the same procedure as in step (4), except choosing the `test-app` eclipse project. Note that the build process contains a custom post-build step that converts the application `elf` file into a `.bin` file using `tricore-elf-objcopy`, which can then be signed by the wolfBoot key tools in the following step
7. If intending to build and load elf files, compile the squashelf tool by running `make` in the `tools/squashelf` directory. This step can be skipped if you only wish to build and load standard wolfBoot images.
8. Sign the generated test-app binary using the wolfBoot keytools
    1. Open a WSL terminal and navigate to `wolfBoot/tools/scripts/tc3xx`
    2. Run `./wbaurixtool.sh [--elf] sign --debug` or `./wbaurixtool.sh [--elf] sign` to sign either the debug or release build, respectively. This creates the signed image files `test-app_v1_signed.bin` and `test-app_v2_signed.bin` in the test-app output build directory. The v1 image is the initial image that will be loaded to the `BOOT` partition, and the v2 image is the update image that will be loaded to the `UPDATE` partition. If `--elf` is specified, `wbaurixtool.sh` will first preprocess the test-app ELF file with [squashelf](../../tools/squashelf) and then sign the resulting ELF file which contains only loadable segments.

```
$ ./wbaurixtool.sh sign
Signing binaries with ecc256 and sha256
wolfBoot KeyTools (Compiled C version)
wolfBoot version 2020000
Update type:          Firmware
Input image:          ../../../IDE/AURIX/test-app/TriCore Release (GCC)/test-app.bin
Selected cipher:      ECC256
Selected hash  :      SHA256
Public key:           ../../../priv.der
Output  image:        ../../../IDE/AURIX/test-app/TriCore Release (GCC)/test-app_v1_signed.bin
Target partition id : 1
image header size calculated at runtime (256 bytes)
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
wolfBoot KeyTools (Compiled C version)
wolfBoot version 2020000
Update type:          Firmware
Input image:          ../../../IDE/AURIX/test-app/TriCore Release (GCC)/test-app.bin
Selected cipher:      ECC256
Selected hash  :      SHA256
Public key:           ../../../priv.der
Output  image:        ../../../IDE/AURIX/test-app/TriCore Release (GCC)/test-app_v2_signed.bin
Target partition id : 2
image header size calculated at runtime (256 bytes)
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
```

### Connect the Lauterbach to the TC375 Device in TRACE32

1. Ensure the Lauterbach probe is connected to the debug port of the tc375 LiteKit
2. Open Trace32 Power View for Tricore
3. Open the SYStem menu and click "DETECT" to detect the tc375 device. Click "CONTINUE" in the pop-up window, and then choose "Set TC375xx" when the device is detected

### Update the start address in UCBs using TRACE32

The default Boot Mode Header (BMHD) start address on a new TC375 `0xA0000000` but the wolfBoot application has a start address of `0xA00A0000`. We must therefore update the BMHD UCBs with the correct entry point such that it can boot wolfBoot out of reset.

1. Select the TC37x dropdown menu and click UCBs
2. Expand `BMHD0_COPY`
3. Click "Edit"
4. Set the `STAD` to `0xA00A0000`
5. Click "Update" to recompute the CRC
6. Click "Check" to verify the new CRC
7. Click "Write" to update the UCB in flash
8. Perform the same operations (2-7) on the `BMHD0_ORIG` UCB

The device is now configured to boot from `0xA00A0000` out of reset.

### Load and run the wolfBoot demo in TRACE32

We can now load wolfBoot and the firmware application images to the tc3xx device using Trace32 and a Lauterbach probe

1. Click "File" -> "ChangeDir and Run Script" and choose the `wolfBoot/tools/scripts/tc3xx/wolfBoot-loadAll-$BUILD.cmm` script, where $BUILD should be either "debug" or "release" depending on your build type from earlier. There are also corresponding ELF load scripts for loading the test app as an ELF image.

wolfBoot and the demo applications are now loaded into flash, and core0 will be halted at the wolfBoot entry point (`core0_main()`).

2. Run the application by clicking "Go" to release the core. This will run wolfBoot which will eventually boot into the application in the `BOOT` partition. You should see LED2 on the board blink once per second.

3. Reset the application to trigger the firmware update. Click "System Down", "System Up", then "Go" to reset the tc3xx. If the device halts again at `core0_main`, click "Go" one more time to release the core. You should see LED2 turn on for ~5sec while wolfBoot swaps the images between `UPDATE` and `BOOT` partitions, then you should see LED2 blink rapidly (~3x/sec) indicating that the firmware update was successful and the new image has booted. Subsequent resets should continue to boot into to the new image.

To rerun the demo, simply rerun the loader script in Trace32 and repeat the above steps

## wolfHSM Compatibility

wolfBoot has full support for wolfHSM on the AURIX TC3xx platform. The wolfBoot application functions as the HSM client, and all cryptographic operations required to verify application images are offloaded to the HSM. When used in tandem with wolfHSM, wolfBoot can be configured to use keys stored on the HSM for cryptographic operations, or to store keys in the default keystore and send them on-demand to the HSM for usage. The former option is the default configuration, and is recommended for most use cases, as key material will never leave the secure boundary of the HSM. The latter option is useful for development and testing, before keys have been preloaded onto the HSM.

Note that information regarding the AURIX TC3xx HSM core is restricted by NDA with Infineon. Source code for the wolfHSM TC3xx platform port is therefore not publicly available and cannot be included for distribution in wolfBoot. Instructions to build wolfBoot with wolfHSM compatibility are provided here, but the wolfHSM TC3xx port must be obtained separately from wolfSSL. To obtain the wolfHSM TC3xx port, please contact wolfSSL at [facts@wolfssl.com](mailto:facts@wolfssl.com).

### Building wolfBoot with wolfHSM

Steps to build wolfBoot on TC3xx with wolfHSM are largely similar to the non-HSM case, with a few key differences.

1. Obtain the wolfHSM release for the AURIX TC3xx from wolfSSL
2. Extract the contents of the `infineon/tc3xx` directory from the wolfHSM TC3xx release you obtained from wolfSSL into the [wolfBoot/IDE/AURIX/wolfHSM-infineon-tc3xx](./wolfHSM-infineon-tc3xx/) directory. The contents of this directory should now be:

```
IDE/AURIX/wolfHSM-infineon-tc3xx/
├── README.md
├── T32
├── placeholder.txt
├── port
├── tchsm-client
├── tchsm-server
├── wolfHSM
└── wolfssl
```

3. Build the wolfHSM server application and load it onto the HSM core, following the instructions provided in the release you obtained from wolfSSL. You do not need to build or load the demo client application,  as wolfBoot will act as the client.
4. Follow all of the steps in [Building and Running the wolfBoot Demo](#building-and-running-the-wolfboot-demo) for the non-HSM enabled case, but with the following key differences:
   1. The [wolfBoot-tc3xx-wolfHSM](./wolfBoot-tc3xx-wolfHSM/) AURIX Studio project should be used instead of `wolfBoot-tc3xx`
   2. Use the `wolfBoot-wolfHSM-loadAll-XXX.cmm` lauterbach scripts instead of `wolfBoot-loadAll-XXX.cmm` to load the wolfBoot and test-app images in the TRACE32 GUI
   3. Provide the `--hsm` global option to the `wbaurixtool.sh` script when invoking it so that the wolfHSM projects are used instead of the standard wolfBoot projects
   4. If using the default build options in [wolfBoot-tc3xx-wolfHSM](./wolfBoot-tc3xx-wolfHSM/), wolfBoot will expect the public key for image verification to be stored at a specific keyId for the wolfBoot client ID. You can use [whnvmtool](https://github.com/wolfSSL/wolfHSM/tree/main/tools/whnvmtool) to generate a loadable NVM image that contains the required keys automatically via `wbaurixtool.sh` through the `nvm` subcommand. This generates an NVM image containing the generated image signing key based on the [wolfBoot-wolfHSM-keys.nvminit](../../tools/scripts/tc3xx/wolfBoot-wolfHSM-keys.nvminit) configuration file, which can then be loaded to the device via a flash programming tool. Before using the `nvm` subcommand of `wbaurixtool.sh`, first compile `whnvmtool` by running `make` in the `lib/wolfHSM/tools/whnvmtool` directory. See the `whnvmtool` documentation and the documentation included in your wolfHSM AURIX release for more details. Note: if you want to use the standard wolfBoot keystore functionality in conjunction with wolfHSM for testing purposes (doesn't require pre-loading keys on the HSM) you can configure wolfBoot to send the keys to the HSM on-the-fly as ephemeral keys. To do this, ensure `WOLFBOOT_USE_WOLFHSM_PUBKEY_ID` is **NOT** defined, and add the `--localkeys` argument to then `./wbaurixtool.sh keygen` command, which invokes the `keygen` tool without the default `--nolocalkeys` option.

### Building wolfBoot with wolfHSM and cert chain verification

wolfBoot with wolfHSM supports certificate chain verification for firmware images. This feature allows wolfBoot to verify that firmware images are signed with certificates that can be traced back to a trusted root certificate authority through a certificate chain.

The `wbaurixtool.sh` script provides two options for applicable commands that enable automation of configuring the build for certificate chain verification:

- `--certchain <file>`: Use a user-provided certificate chain file
- `--dummy-certchain`: Use a "dummy" certificate chain that is created from the key pair generated by the wolfBoot keytools for development/testing purposes.

Both options require the `--hsm` global option and can be used with the `keygen`, `sign`, `macros`, `lcf`, and `nvm` subcommands.

### Custom Certificate Chain

If you want to use a custom certificate chain for verification, you can provide the `--certchain <certchain file>` option to the `macros`, `lcf`, and `sign` subcommands. This ensures that your certificate chain will be included in the generated firmware image and that wolfBoot will verify the chain on boot, then verify the signature of the firmware image using the public key corresponding to the leaf certificate in the chain. Note that the user is responsible for properly constructing the certificate chain and ensuring that the leaf certificate is bound to the firmware signing key pair, as well as properly provisioning the HSM with the root CA certificate at the expected NVM object ID specified by the HAL.

### Dummy Certificate Chain

If you just want to test out the certificate verifcation functionality, `wbaurixtool.sh` can generate and use a dummy certificate chain by   providing the `--dummy-certchain` option to the `keygen`, `sign`, `macros`, `lcf`, and `nvm` subcommands. This will cause the keygen step to generate a dummy root CA at `test-dummy-ca/root-ca.der`, well as a dummy certificate chain at `test-dummy-ca/raw-chain.der` using the specified algorithm. The generated chain consists of a single intermediate as well as a leaf (signing) certificate whose identity is bound to the generated firmware signing key pair. The subsequent commands will then ensure this generated chain is used by wolfBoot. Additionally, the `nvm` subcommand will create an NVM image containing the generated root CA that can be loaded into HSM NVM.

**Note: The `--dummy-certchain` option is intended for development and testing. For production use, generate and use your own certificate chain.**

## Building: Command Sequence

The following pseudo command sequence shows a brief overview of the commands needed to build wolfBoot on AURIX (optionally with wolfHSM). The signature and hashing algorithms used in the example are ECC 256 and SHA 256 and specified explicitly for clarity. Note that these algorithms are the default, so do not need to be explicitly specified. Optional arguments are shown in square brackets (e.g. if targeting wolfHSM, the `--hsm` option must be provided as a global option to `wbaurixtool.sh`).

```sh
# Navigate to wolfBoot directory
WOLFBOOT_DIR=/path/to/wolfBoot
SCRIPTS_DIR=$WOLFBOOT_DIR/tools/scripts/tc3xx
cd $WOLFBOOT_DIR

# Copy source files to appropriate location as listed in the steps above
# ...

# Start with a clean build
make clean && make keysclean && cd $WOLFBOOT_DIR/tools/keytools && make clean
cd $SCRIPTS_DIR && ./wbaurixtool.sh clean
# Delete any build artifacts in wolfBoot-tc3xx (or wolfBoot-tc3xx-wolfHSM) and test-app (or test-app-wolfHSM) AURIX Studio projects
# ...

# Make keytools (NOTE: THIS OVERRIDES TARGET.H WITH SIM VALUES)
cd $WOLFBOOT_DIR
make keytools


# Generate target.h
cd $SCRIPTS_DIR
./wbaurixtool.sh target

# Generate keys, as well as configuration macros and linker script based on the selected signature algorithm
./wbaurixtool.sh [--hsm] keygen --sign-algo ecc256 --hash-algo sha256 macros lcf

# If using wolfHSM, generate key NVM image (make sure you have compiled whnvmtool before running this command)
./wbaurixtool.sh nvm
# Load NVM image hexfile to the device
# ...

# Build wolfHSM AURIX Studio project
# ....

# Build test-app AURIX Studio project
# ....

# Sign test app
./wbaurixtool.sh [--hsm] sign --sign-algo ecc256 --hash-algo sha256 [--debug]

# Load wolfBoot + app in Lauterbach using tools/scripts/tc3xx/wolfBoot-loadAll-XXX.cmm
# ...
```

## Troubleshooting

### WSL "bad interpreter" error

When running a shell script in WSL, you may see the following error:

```
$ ./wbaurixtool.sh:
/bin/bash^M: bad interpreter: No such file or directory
```

This occurs because your local git repository is configured with the default `core.autocrlf true` configuration, meaning that git is checking out files with Windows-style CRLF line endings which the bash interpreter cannot handle. To fix this, you need to either configure git to not checkout windows line endings for shell scripts ([GitHub docs](https://docs.github.com/en/get-started/getting-started-with-git/configuring-git-to-handle-line-endings#about-line-endings)), or you can run the `dos2unix` (`sudo apt install dos2unix`) utility on the script before running it.

### Post Quantum: ML-DSA

#### ML-DSA Keytools

When compiling wolfBoot to use wolfHSM with ML-DSA for verification, you must ensure to compile the keytools with a .config file specifying the ML-DSA parameters. Otherwise, the image header size will be incorrect, and attempts to boot the application will fail in unpredictable ways. Before compiling the keytools in the steps above, copy [config/examples/sim-wolfHSM-mldsa.config] to `.config`. You can now run `make keytools` and the keytools will be able to handle ML-DSA keys.
