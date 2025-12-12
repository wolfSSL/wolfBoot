# wolfboot Visual Studio

## IDE

Project files can be found in [`[WOLFBOOT_ROOT]/IDE/VisualStudio`](../IDE/VisualStudio/README.md).


## CMake

Users of Visual Studio can open the `WOLFBOOT_ROOT` directory without the need for a project file.

Visual Studio is "cmake-aware" and recognizes the [CMakePresets.json](../../CMakePresets.json)

For the `Select Startup Item`, leave at default. Do not select `image`, wolfboot_name[], etc.

Select a device from the ribbon bar, shown here for the `stm32l4`

<img width="727" height="108" alt="image" src="https://github.com/user-attachments/assets/4d3e8300-e89f-4e7a-9e84-a32a284ad719" /><br /><br />

From `Solution Explorer`, right-click `CmakeLists.txt` and then select `Configure wolfBoot`.

<img width="688" height="592" alt="image" src="https://github.com/user-attachments/assets/41b11094-adbb-473a-9c5a-d004d9a7a91b" /><br /><br />

To build, follow the same steps to right click, and select `Build`.

View the CMake and Build messages in the `Output` Window. Note the drop-down to select view:

<img width="721" height="627" alt="image" src="https://github.com/user-attachments/assets/6a22bb23-a99c-45ec-a989-2d54360fc384" /><br /><br />


### Studio Command Prompt

Select `View` - `Terminal` from the menu bar.

* Configure: `cmake --preset <preset name>`
* Build: `cmake --build --preset <preset name>`

```bash
# delete build directory
rmdir /s /q build-stm32l4

# configure
cmake --preset stm32l4

# build
cmake --build --preset stm32l4
```

If there are no devices listed in the `Manage Configurations` drop-down, ensure the `CMakePresets.json` is valid.
A single json syntax error will spoil the entire project.

## Local Visual Studio Projects

Project files can be found in [`[WOLFBOOT_ROOT]/IDE/VisualStudio`](../../IDE/VisualStudio/README.md).

There are three projects to:

1. Generate a new signing key
2. Sign an image
3. Verify the signed image

Visual Studio `$(ProjectDir)` is typically `[WOLFBOOT_ROOT]\tools\keytools`.

-----

## Keytools Usage

See [keytools/README.md](../../keytools/README.md)

### Step 1: wolfBootKeyGenTool Visual Studio Project

Build the project. Generate a new signing key with `keygen.exe`.

```DOS
keygen.exe [ --ed25519 | --ed448 | --ecc256 | --ecc384 | --ecc521 | --rsa2048 | --rsa3072 | --rsa4096 ] ^
           [ -g privkey]     [ -i pubkey] [ -keystoreDir dir]  ^
           [ --id {list}]    [ --der]                        ^
           [ --exportpubkey] [ --nolocalkeys]
```

WARNING: Key Generation will *overwrite* any prior keystore files.

Right-click on `wolfBootKeygenTool` project, typically in:

```text
C:\workspace\wolfBoot-%USERNAME%\tools\keytools
```

Select: Properties - Configuration Properties - Debugging:

```text
Command:           $(TargetPath)
Command Arguments: --ed25519 -g $(ProjectDir)wolfboot_signing_private_key.der   -keystoreDir  $(ProjectDir)
Working Directory: $(ProjectDir)..\..\
```

Replace `$(ProjectDir)` with your desired `keystoreDir` path for keys and firmware locations.
Otherwise the private key will be created in the project directory `[WOLFBOOT_ROOT]\tools\keytools`.

Example:

```DOS
cd %WOLFBOOT_ROOT%\tools\keytools

:: cmd       sign     private key
:: ------- --------- -----------------------------------
keygen.exe --ed25519 -g wolfboot_signing_private_key.der
```

Expected output:

```text
wolfBoot KeyGen
Keystore size:        2608
Saving keystore file: C:\workspace\wolfBoot-gojimmypi\tools\keytools\/keystore.c
Selected Keytype:     ECC256
Generating key (type: ECC256)
Associated key file:  C:\workspace\wolfBoot-gojimmypi\tools\keytools\wolfboot_signing_private_key.der
Partition ids mask:   ffffffff
Key type:             ECC256
Public key slot:      0
Done.
```

-----

### Step 2: wolfBootSignTool Visual Studio Project

Build the project. Sign an image with `sign.exe  [OPTIONS]  IMAGE.BIN  KEY.DER  VERSION`.

Right-click on `wolfBootSignTool` project, typically in:

```text
C:\workspace\wolfBoot-%USERNAME%\tools\keytools
```

Select: Properties - Configuration Properties - Debugging:

```text
Command:           $(TargetPath)
Command Arguments: --ed25519 --sha256 "$(ProjectDir)test.bin"  "$(ProjectDir)wolfboot_signing_private_key.der"  1
Working Directory: $(ProjectDir)
```

The `$(ProjectDir)` will typically be something like this, where the `keystore.c` was generated in Step 1 (above):

Example:

```DOS
cd %WOLFBOOT_ROOT%\tools\keytools

:: cmd       sign     hash   input     private key                    [version]      [output]
:: ----- --------- -------- -------- -------------------------------- --------- ------------------
sign.exe --ed25519 --sha256 test.bin wolfboot_signing_private_key.der     1     test_v1_signed.bin
```

The last two parameters are optional:

- Version, default is `1`
- Output, default is `[input]_v1_signed.bin`, where the number after the `v` is the version.

Be sure the signing algorithm used here matches the one on the key generation!

Expected output:

```text
wolfBoot KeyTools (Compiled C version)
wolfBoot version 2060000
Update type:          Firmware
Input image:          C:\workspace\wolfBoot-<user>\tools\keytools\test.bin
Selected cipher:      ED25519
Selected hash:        SHA256
Private key:          C:\workspace\wolfBoot-<user>\tools\keytools\wolfboot_signing_private_key.der
Output  image:        C:\workspace\wolfBoot-<user>\tools\keytools\test_v1_signed.bin
Target partition id:  1
Manifest header size: 256
Found ED25519 key
Hashing primary pubkey, size: 32
Calculating SHA256 digest...
Signing the digest...
Sign: 0x01
Output image(s) successfully created.
```

-----

### Step 3. wolfBootTestlib Visual Studio Project

The `IS_TEST_LIB_APP` Macro is needed for the Visual Studio `wolfBootTestLib.vcproj` project file.
See also the related `wolfBootImage.props` file.

Other additional preprocessor macros defined in project file:

```text
__WOLFBOOT;
WOLFBOOT_NO_PARTITIONS;
WOLFBOOT_HASH_SHA256;
WOLFBOOT_SIGN_ECC256;
WOLFSSL_USER_SETTINGS;
WOLFSSL_HAVE_MIN;
WOLFSSL_HAVE_MAX;
```

Build the project. Verify an image with `sign.exe  [OPTIONS]  IMAGE.BIN  KEY.DER  VERSION`.

Right-click on `wolfBootSignTool` project, typically in:

```text
C:\workspace\wolfBoot-%USERNAME%\tools\keytools
```

Select: Properties - Configuration Properties - Debugging:

```text
Command:           $(TargetPath)
Command Arguments: test_v1_signed.bin
Working Directory: $(ProjectDir)
```

## Additional Configuration Defaults

See the [cmake/config_defaults.cmake](../../cmake/config_defaults.cmake) file. Of particular interest
are some environment configuration settings, in particular the `DETECT_VISUALGDB`:

```cmake
# Environments are detected in this order:
set(DETECT_VISUALGDB true)
set(DETECT_CUBEIDE true)
set(DETECT_VS2022 true)

# Enable HAL download only implemented for TMS devices at this time.
# See [WOLFBOOT_ROOT]/cmake/stm32_hal_download.cmake
# and [WOLFBOOT_ROOT]/cmake/downloads/stm32_hal_download.cmake
set(ENABLE_HAL_DOWNLOAD true)
set(FOUND_HAL_BASE false)

# optionally use .config files; See CMakePresets.json instead
set(USE_DOT_CONFIG false)
```

## Your own toolchain

Create a `CMakeUserPresets.json` (ignored by git, see and rename `cmake/preset-examples/CMakeUserPresets.json.sample` ):

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "my-arm-bin",
      "inherits": "stm32l4",
      "cacheVariables": {
        "ARM_GCC_BIN": "C:/Tools/arm-none-eabi-14.2/bin"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "my-arm-bin",
      "configurePreset": "my-arm-bin"
    }
  ]
}
```

For more details, see the [cmake/README](../../cmake/README.md) file.
