# Overview

This example demonstrates using wolfBoot on the Infineon AURIX TC3xx family of microcontrollers. The example is based on the TC375 Lite-Kit V2, but should be easily adaptable to other TC3xx devices. This README assumes basic familiarity with the TC375 SoC, the AURIX IDE, and Lauterbach Trace32 debugger.

The example contains two projects: `wolfBoot-tc3xx` and `test-app`. The `wolfBoot-tc3xx` project contains the wolfBoot bootloader, and the `test-app` project contains a simple firmware application that will be loaded and executed by wolfBoot. The `test-app` project is a simple blinky application that blinks LED2 on the TC375 Lite-Kit V2 once per second when running the base image, and rapidly (~3x/sec) when running the update image. The test app determines if it is a base or update image by inspecting the firmware version (obtained through the wolfBoot API). The firmware version is set in the image header by the wolfBoot keytools when signing the test app binaries. The same test app binary is used for both the base and update images, with the only difference being the firmware version set by the keytools.

## Important notes

- In the TC375 UCBs, BMDHx.STAD must point to the wolfBoot entrypoint `0xA000_0000`. This is the default value of the TC375 and so need not be changed unless it has already been modified or you wish to rearrange the memory map.
- Because TC3xx PFLASH ECC prevents reading from erased flash, the `EXT_FLASH` option is used to redirect flash reads to the `ext_flash_read()` HAL API, where the flash pages requested to be read can be blank-checked by hardware before reading.
- TC3xx PFLASH is write-once (`NVM_FLASH_WRITEONCE`), however wolfBoot `NVM_FLASH_WRITEONCE` does not support `EXT_FLASH`. Therefore the write-once functionality is re-implemented in the `HAL` layer.
- This demo app is only compatible with the GCC toolchain build configurations shipped with the AURIX IDE. The TASKING compiler build configurations are not yet supported.

## Flash Partitioning

The TC3xx AURIX port of wolfBoot places all images in PFLASH, and uses both PFLASH0 and PFLASH1 banks. The wolfBoot executable code and the image swap sector are located in PFLASH0, with the remainder available for use. PFLASH1 is divided in half, with the first half holding the BOOT partition and the second half holding the UPDATE partition. User firmware images are directly executed in place from the BOOT partition in PFLASH1, and so must be linked to execute within this address space, with an offset of `IMAGE_HEADER_SIZE` to account for the wolfBoot image header.

```
+==========+
| PFLASH0  |
+==========+ <-- 0x8000_0000
| wolfBoot |        128K
+----------+ <-- 0x8002_0000
| SWAP     |        16K
+----------+ <-- 0x8002_4000
| Unused   |        ~2.86M
+----------+ <-- 0x8030_0000

+==========+
| PFLASH1  |
+==========+ <-- 0x8030_0000
| BOOT     |        1.5M
+----------+ <-- 0x8048_0000
| UPDATE   |        1.5M
+----------+ <-- 0x8060_0000
```

Please refer to the [wolfBoot](wolfBoot-tc3xx/Lcf_Gnu_Tricore_Tc.lsl) and [test-app](test-app/Lcf_Gnu_Tricore_Tc.lsl) linker scripts for the exact memory configuration.


## Building and running the wolfBoot demo

### Prerequisites

- A Windows 10 computer with the Infineon AURIX IDE installed
- A WSL2 distro (tested on Ubuntu 22.04) with the `build-essential` package installed (`sudo apt install build-essential`)
- A TC375 AURIX Lite-Kit V2

### Clone wolfBoot

1. Clone the wolfBoot repository and initialize the repository submodules (`git submodule update --init`)

### Build wolfBoot keytools and generate keys

1. Open a WSL2 terminal and navigate to the top level `wolfBoot` directory
2. Compile the keytools by running `make keytools`
3. Use the helper script to generate a new signing key pair using RSA 4096
    1. Navigate to `wolfBoot/tools/scripts/tc3xx`
    2. Run `./gen-tc3xx-keys.sh`. This generates the signing private key `wolfBoot/priv.der` and adds the public key to the wolfBoot keystore (see [keygen](https://github.com/wolfSSL/wolfBoot/blob/aurix-tc3xx-support/docs/Signing.md) for more information). If you already have generated a key, you will be prompted to overwrite it.

```
$ ./gen-tc3xx-keys.sh
+ cd ../../../
+ ./tools/keytools/keygen -g priv.der --rsa4096
Keytype: RSA4096
Generating key (type: RSA4096)
RSA public key len: 550 bytes
Associated key file:   priv.der
Partition ids mask:   ffffffff
Key type   :           RSA4096
Public key slot:       0
Done.
```

### Install the Infineon TC3xx SDK into the wolfBoot project

Because of repository size constraints and differing licenses, the required Infineon low level drivers ("iLLD") and auto-generated SDK configuration code that are usually included in AURIX projects are not included in this demo app. It is therefore required to locate them in your AURIX install and extract them to the location that the wolfBoot AURIX projects expect them to be at. The remainder of these instructions will use variables to reference the following three paths:

- `$AURIX_INSTALL`: The AURIX IDE installation location. This is usually `C:\Infineon\AURIX-Studio-<version>`.
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
    2. Run `./gen-tc3xx-target.sh`
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
7. Sign the generated test-app binary using the wolfBoot keytools
    1. Open a WSL terminal and navigate to `wolfBoot/tools/scripts/tc3xx`
    2. Run `./gen-tc3xx-signed-test-apps-debug.sh` or `gen-tc3xx-signed-test-apps-release.sh` to sign either the debug or release build, respectively. This creates the signed image files `test-app_v1_signed.bin` and `test-app_v2_signed.bin` in the test-app output build directory. The v1 image is the initial image that will be loaded to the `BOOT` partition, and the v2 image is the update image that will be loaded to the `UPDATE` partition.

```
$ ./gen-tc3xx-signed-test-apps-release.sh
+ ../../keytools/sign --rsa4096 --sha256 '../../../IDE/AURIX/test-app/TriCore Release (GCC)/test-app.bin' ../../../priv.der 1
wolfBoot KeyTools (Compiled C version)
wolfBoot version 2010000
Update type:          Firmware
Input image:          ../../../IDE/AURIX/test-app/TriCore Release (GCC)/test-app.bin
Selected cipher:      RSA4096
Selected hash  :      SHA256
Public key:           ../../../priv.der
Output  image:        ../../../IDE/AURIX/test-app/TriCore Release (GCC)/test-app_v1_signed.bin
Target partition id : 1
Found RSA512 key
image header size calculated at runtime (1024 bytes)
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
+ ../../keytools/sign --rsa4096 --sha256 '../../../IDE/AURIX/test-app/TriCore Release (GCC)/test-app.bin' ../../../priv.der 2
wolfBoot KeyTools (Compiled C version)
wolfBoot version 2010000
Update type:          Firmware
Input image:          ../../../IDE/AURIX/test-app/TriCore Release (GCC)/test-app.bin
Selected cipher:      RSA4096
Selected hash  :      SHA256
Public key:           ../../../priv.der
Output  image:        ../../../IDE/AURIX/test-app/TriCore Release (GCC)/test-app_v2_signed.bin
Target partition id : 1
Found RSA512 key
image header size calculated at runtime (1024 bytes)
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
```

### Load and run the wolfBoot demo

1. Load wolfBoot and the firmware application images to the tc3xx device using Trace32 and a Lauterbach probe
    1. Ensure the Lauterbach probe is connected to the debug port of the tc375 LiteKit
    2. Open Trace32 Power View for Tricore
    3. Open the SYStem menu and click "DETECT" to detect the tc375 device. Click "CONTINUE" in the pop-up window, and then choose "Set TC375xx" when the device is detected
    4. Click "File" -> "ChangeDir and Run Script" and choose the `wolfBoot/tools/scripts/tc3xx/wolfBoot-loadAll-$BUILD.cmm` script, where $BUILD should be either "debug" or "release" depending on your build type in (4) and (6).

wolfBoot and the demo applications are now loaded into flash, and core0 will be halted at the wolfBoot entry point (`core0_main()`).

2. Run the application by clicking "Go" to release the core. This will run wolfBoot which will eventually boot into the application in the `BOOT` partition. You should see LED2 on the board blink once per second.

3. Reset the application to trigger the firmware update. Click "System Down", "System Up", then "Go" to reset the tc3xx. If the device halts again at `core0_main`, click "Go" one more time to release the core. You should see LED2 turn on for ~5sec while wolfBoot swaps the images between `UPDATE` and `BOOT` partitions, then you should see LED2 blink rapidly (~3x/sec) indicating that the firmware update was successful and the new image has booted. Subsequent resets should continue to boot into to the new image.

To rerun the demo, simply rerun the loader script in Trace32 and repeat the above steps


## Troubleshooting

### WSL "bad interpreter" error 

When running a shell script in WSL, you may see the following error:

```
$ ./gen-tc3xx-target.sh: 
/bin/bash^M: bad interpreter: No such file or directory
```

This occurs because your local git repository is configured with the default `core.autocrlf true` configuration, meaning that git is checking out files with Windows-style CRLF line endings which the bash interpreter cannot handle. To fix this, you need to either configure git to not checkout windows line endings for shell scripts ([GitHub docs](https://docs.github.com/en/get-started/getting-started-with-git/configuring-git-to-handle-line-endings#about-line-endings)), or you can run the `dos2unix` (`sudo apt install dos2unix`) utility on the script before running it.
