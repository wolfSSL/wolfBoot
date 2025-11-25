# wolfBoot CMake

See the [`WOLFBOOT_ROOT`/cmake/README.md](../cmake/README.md) file.

## Important: No in-source builds.

One of the first checks in the `[WOLFBOOT_ROOT]/CMakeLists.txt` is whether `CMAKE_SOURCE_DIR` == `CMAKE_BINARY_DIR`.
In-source builds are not supported. The provided wolfSSL presets will typically prevent this.
Beware when integrating with existing projects or creating custom presets.

### CMake - Presets

This section explains how to build wolfBoot using CMake Presets.
Presets let you keep repeatable build settings in a single JSON file ([`[WOLFBOOT_ROOT]/CMakePresets.json`](../CMakePresets.json)) so
you can configure and build with short, memorable commands like:

```
cmake --list-presets
cmake --preset stm32l4
cmake --build --preset stm32l4
```

See the `WOLFBOOT_ROOT`/[config_defaults.cmake](./config_defaults.cmake) file.

#### Convert existing `.config` to CMake Presets

The [`[WOLFBOOT_ROOT]`/tools/scripts/config2presets.py](../tools/scripts/config2presets.py) script cam
convert existing [config/examples](../config/examples) to CMake presets.

For example:

```python
python3 ./tools/scripts/config2presets.py ./config/examples/stm32h7.config
```

### Build with cmake using `.config` files

Presets are preferred instead of `.config`, see below.

To use `.config` files instead of presets,

```bash
# cd your [WOLFBOOT_ROOT]

# Backup current config
mv ./.config ./.config.bak

# Get an example config
cp ./config/examples/stm32h7.config ./.config

# Call cmake with -DUSE_DOT_CONFIG=ON
cmake -S . -B build-stm32h7 -DUSE_DOT_CONFIG=ON

# Sample build
cmake --build build-stm32h7 -j
```

The output should look contain text like this:

```text
-- Found a .config file, will parse
-- Config mode: dot (.config cache)
-- Loading config from: /mnt/c/workspace/wolfBoot-gojimmypi
-- Reading config file: /mnt/c/workspace/wolfBoot-gojimmypi/.config
-- -- Parsing lines from config file...
-- -- Found line: ARCH?=ARM
-- -- Parsed key: ARCH
-- -- Parsed op:  ?
-- -- Parsed val: ARM
-- -- Assignment: ARCH=ARM
-- -- Found line: TARGET?=stm32h7
-- -- Parsed key: TARGET
-- -- Parsed op:  ?
-- -- Parsed val: stm32h7
-- -- Assignment: TARGET=stm32h7
-- -- Found line: SIGN?=ECC256
-- -- Parsed key: SIGN
-- -- Parsed op:  ?
-- -- Parsed val: ECC256
  ...etc...
```

Calling `cmake` with an existing `.config` file will default to dot-config mode.

```bash
ls .config
cmake -S . -B build-stm32h7
```

Specify additional directories, for example the STM32L4:

```bash
cmake -S . -B build-stm32l4 -DUSE_DOT_CONFIG=ON \
  -DHAL_DRV="${VG_BASE}/Drivers/STM32L4xx_HAL_Driver" \
  -DHAL_CMSIS_DEV="${VG_BASE}/Drivers/CMSIS/Device/ST/STM32L4xx/Include" \
  -DHAL_CMSIS_CORE="${VG_BASE}/Drivers/CMSIS/Include" \
  -DHAL_TEMPLATE_INC="${VG_BASE}/Drivers/STM32L4xx_HAL_Driver/Inc"

cmake --build build-stm32l4 -j
```

### Build presets

Each configure preset has a matching build preset with jobs=4, verbose=true, and targets=["all"].

Example commands:

```bash
cmake --preset stm32l4
cmake --build --preset stm32l4

cmake --preset stm32h7
cmake --build --preset stm32h7
```

### CMake User Presets.

See the [preset-examples/CMakeUserPresets.json.sample(./preset-examples/CMakeUserPresets.json.sample).
Copy the file to `WOLFBOOT_ROOT` and remove the`.sample` suffix: `CMakeUserPresets.json`.

It is critically important that none the names of a user preset do not conflict with regular presets.

For instance, the sample extends and overrides some of the `stm32l4` settings,
using LLVM clang on Windows, and prefixes ALL the names with `my-`:

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "my-stm32l4",
      "displayName": "my STM32L4",
      "inherits": [
        "stm32l4"
      ],
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build-my-stm32l4",
      "cacheVariables": {
        "ARM_GCC_BIN": "C:/SysGCC/arm-eabi/bin",
        "HOST_CC": "C:/Program Files/LLVM/bin/clang.exe"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "my-stm32l4",
      "configurePreset": "my-stm32l4"
    }
  ]
}
```


From the [docs for CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html):

>"Added in version 3.19.
>
>One problem that CMake users often face is sharing settings with other people for common ways to configure
a project. This may be done to support CI builds, or for users who frequently use the same build. CMake
supports two main files, `CMakePresets.json` and `CMakeUserPresets.json`, that allow users to specify common
configure options and share them with others. CMake also supports files included with the include field.
>
>`CMakePresets.json` and` CMakeUserPresets.json` live in the project's root directory. They both have
exactly the same format, and both are optional (though at least one must be present if `--preset` is
specified). `CMakePresets.json` is meant to specify project-wide build details, while `CMakeUserPresets.json`
is meant for developers to specify their own local build details.
>
>CMakePresets.json may be checked into a version control system, and `CMakeUserPresets.json` should NOT be
checked in. For example, if a project is using Git, `CMakePresets.json` may be tracked, and
`CMakeUserPresets.json` should be added to the .gitignore."

## Troubleshooting

The wrong toolchain is being used, or a target was not specified:

```
Error: no such instruction: `isb'
```

### Other log files

Windows users may find cmake log files in this directory helpful:

```
C:\Users\%USERNAME%\AppData\Local\CMakeTools
```

#### Tips & Gotchas

Out-of-source enforced: wolfBoot's CMakeLists.txt blocks in-source builds;
presets default to `build-${presetName}` anyway.

Toolchain auto-select: If `WOLFBOOT_TARGET` is not x86_64_efi or sim,
CMAKE_TOOLCHAIN_FILE defaults to `cmake/toolchain_arm-none-eabi.cmake`.

Windows host tools: When HOST_CC is `cl.exe`, CMakeLists.txt creates a
lightweight `unistd.h` shim and adjusts flags-no manual changes needed.

`$penv` vs `$env`: Use `$penv{VAR}` in environment to append to the existing
process environment (keeps your PATH). `$env{VAR}` replaces it.

Visual Studio / VS Code: Both detect presets automatically;
select the preset from the status bar or CMake menu, then build.

`--fresh`: Re-configure from scratch without deleting the build directory.

For further details, see the [cmake/README](../cmake/README.md)

### CMake - Read .config file

See [cmake/README](../cmake/README.md#build-with-cmake-using-config-files).

### CMake - Command-line Settings

To build using CMake, create a `build` directory and run `cmake` with the target platform as well as values for the partition
size and address variables. To build the test-apps, run with `-DBUILD_TEST_APPS=yes`. To use the wolfCrypt-py keytools, run
with `-DPYTHON_KEYTOOLS=yes`.

For example, to build for the stm32h7 platform:
```
$ mkdir build
$ cd build
$ cmake -DWOLFBOOT_TARGET=stm32h7 -DBUILD_TEST_APPS=yes -DWOLFBOOT_PARTITION_BOOT_ADDRESS=0x8020000 -DWOLFBOOT_SECTOR_SIZE=0x20000 -DWOLFBOOT_PARTITION_SIZE=0xD0000 -DWOLFBOOT_PARTITION_UPDATE_ADDRESS=0x80F0000 -DWOLFBOOT_PARTITION_SWAP_ADDRESS=0x81C0000 ..
$ make
```

The output should look something like:
```
Scanning dependencies of target keystore
[  2%] Building signing tool
[  4%] Building keygen tool
[  7%] Generating keystore.c and signing private key
Keytype: ECC256
Gen /home/user/wolfBoot/build/wolfboot_signing_private_key.der
Generating key (type: ECC256)
Associated key file:   /home/user/wolfBoot/build/wolfboot_signing_private_key.der
Key type   :           ECC256
Public key slot:       0
Done.
[  7%] Built target keystore
Scanning dependencies of target public_key
[  9%] Building C object CMakeFiles/public_key.dir/keystore.c.o
[ 11%] Linking C static library libpublic_key.a
[ 14%] Built target public_key
Scanning dependencies of target wolfboothal
[ 16%] Building C object CMakeFiles/wolfboothal.dir/hal/stm32h7.c.o
[ 19%] Linking C static library libwolfboothal.a
[ 19%] Built target wolfboothal
Scanning dependencies of target wolfcrypt
[ 21%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/integer.c.o
[ 23%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/tfm.c.o
[ 26%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/ecc.c.o
[ 28%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/memory.c.o
[ 30%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/wc_port.c.o
[ 33%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/wolfmath.c.o
[ 35%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/hash.c.o
[ 38%] Building C object lib/CMakeFiles/wolfcrypt.dir/wolfssl/wolfcrypt/src/sha256.c.o
[ 40%] Linking C static library libwolfcrypt.a
[ 40%] Built target wolfcrypt
Scanning dependencies of target wolfboot
[ 42%] Building C object CMakeFiles/wolfboot.dir/src/libwolfboot.c.o
[ 45%] Linking C static library libwolfboot.a
[ 45%] Built target wolfboot
Scanning dependencies of target image
[ 47%] Building C object test-app/CMakeFiles/image.dir/app_stm32h7.c.o
[ 50%] Building C object test-app/CMakeFiles/image.dir/led.c.o
[ 52%] Building C object test-app/CMakeFiles/image.dir/system.c.o
[ 54%] Building C object test-app/CMakeFiles/image.dir/timer.c.o
[ 57%] Building C object test-app/CMakeFiles/image.dir/startup_arm.c.o
[ 59%] Linking C executable image
[ 59%] Built target image
Scanning dependencies of target image_signed
[ 61%] Generating image.bin
[ 64%] Signing  image
wolfBoot KeyTools (Compiled C version)
wolfBoot version 10C0000
Update type:          Firmware
Input image:          /home/user/wolfBoot/build/test-app/image.bin
Selected cipher:      ECC256
Selected hash  :      SHA256
Public key:           /home/user/wolfBoot/build/wolfboot_signing_private_key.der
Output  image:        /home/user/wolfBoot/build/test-app/image_v1_signed.bin
Target partition id : 1
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
[ 64%] Built target image_signed
Scanning dependencies of target image_outputs
[ 66%] Generating image.size
   text	   data	    bss	    dec	    hex	filename
   5284	    108	     44	   5436	   153c	/home/user/wolfBoot/build/test-app/image
[ 69%] Built target image_outputs
Scanning dependencies of target wolfboot_stm32h7
[ 71%] Building C object test-app/CMakeFiles/wolfboot_stm32h7.dir/__/src/string.c.o
[ 73%] Building C object test-app/CMakeFiles/wolfboot_stm32h7.dir/__/src/image.c.o
[ 76%] Building C object test-app/CMakeFiles/wolfboot_stm32h7.dir/__/src/loader.c.o
[ 78%] Building C object test-app/CMakeFiles/wolfboot_stm32h7.dir/__/src/boot_arm.c.o
[ 80%] Building C object test-app/CMakeFiles/wolfboot_stm32h7.dir/__/src/update_flash.c.o
[ 83%] Linking C executable wolfboot_stm32h7
[ 83%] Built target wolfboot_stm32h7
Scanning dependencies of target binAssemble
[ 85%] Generating bin-assemble tool
[ 85%] Built target binAssemble
Scanning dependencies of target image_boot
[ 88%] Generating wolfboot_stm32h7.bin
[ 90%] Signing  image
wolfBoot KeyTools (Compiled C version)
wolfBoot version 10C0000
Update type:          Firmware
Input image:          /home/user/wolfBoot/build/test-app/image.bin
Selected cipher:      ECC256
Selected hash  :      SHA256
Public key:           /home/user/wolfBoot/build/wolfboot_signing_private_key.der
Output  image:        /home/user/wolfBoot/build/test-app/image_v1_signed.bin
Target partition id : 1
Calculating SHA256 digest...
Signing the digest...
Output image(s) successfully created.
[ 92%] Assembling image factory image
[ 95%] Built target image_boot
Scanning dependencies of target wolfboot_stm32h7_outputs
[ 97%] Generating wolfboot_stm32h7.size
   text	   data	    bss	    dec	    hex	filename
  42172	      0	     76	  42248	   a508	/home/user/wolfBoot/build/test-app/wolfboot_stm32h7
[100%] Built target wolfboot_stm32h7_outputs
```

Signing and hashing algorithms can be specified with `-DSIGN=<alg>` and `-DHASH=<alg>`. To view additional
options to configuring wolfBoot, add `-LAH` to your cmake command, along with the partition specifications.
```
$ cmake -DWOLFBOOT_TARGET=stm32h7 -DWOLFBOOT_PARTITION_BOOT_ADDRESS=0x8020000 -DWOLFBOOT_SECTOR_SIZE=0x20000 -DWOLFBOOT_PARTITION_SIZE=0xD0000 -DWOLFBOOT_PARTITION_UPDATE_ADDRESS=0x80F0000 -DWOLFBOOT_PARTITION_SWAP_ADDRESS=0x81C0000 -LAH ..
```

#### stm32f4
```
$ cmake -DWOLFBOOT_TARGET=stm32f4 -DWOLFBOOT_PARTITION_SIZE=0x20000 -DWOLFBOOT_SECTOR_SIZE=0x20000 -DWOLFBOOT_PARTITION_BOOT_ADDRESS=0x08020000 -DWOLFBOOT_PARTITION_UPDATE_ADDRESS=0x08040000 -DWOLFBOOT_PARTITION_SWAP_ADDRESS=0x08060000 ..
```

#### stm32u5
```
$ cmake -DWOLFBOOT_TARGET=stm32u5 -DBUILD_TEST_APPS=yes -DWOLFBOOT_PARTITION_BOOT_ADDRESS=0x08100000 -DWOLFBOOT_SECTOR_SIZE=0x2000 -DWOLFBOOT_PARTITION_SIZE=0x20000 -DWOLFBOOT_PARTITION_UPDATE_ADDRESS=0x817F000 -DWOLFBOOT_PARTITION_SWAP_ADDRESS=0x81FE000 -DNO_MPU=yes ..
```

##### stm32l0
```
$ cmake -DWOLFBOOT_TARGET=stm32l0 -DWOLFBOOT_PARTITION_BOOT_ADDRESS=0x8000 -DWOLFBOOT_SECTOR_SIZE=0x1000 -DWOLFBOOT_PARTITION_SIZE=0x10000 -DWOLFBOOT_PARTITION_UPDATE_ADDRESS=0x18000 -DWOLFBOOT_PARTITION_SWAP_ADDRESS=0x28000 -DNVM_FLASH_WRITEONCE=yes ..
```
