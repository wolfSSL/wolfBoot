﻿## wolfBoot for STM32 devices

The default [Makefile](../Makefile) needs at least the `gcc-arm-none-eabi`.

```bash
sudo apt update
sudo apt install -y build-essential gcc-arm-none-eabi binutils-arm-none-eabi
# optional (often handy): gdb-multiarch or gdb-arm-none-eabi
arm-none-eabi-gcc --version   # should print the version
```

The device manufacturer toolchain _also_ needs to be installed. For example without the [STM32CubeIDE Software](https://www.st.com/en/development-tools/stm32cubeide.html),
errors like this will otherwise be encountered:

```
        [CC ARM] hal/stm32l4.o
hal/stm32l4.c:24:10: fatal error: stm32l4xx_hal.h: No such file or directory
   24 | #include "stm32l4xx_hal.h"
      |          ^~~~~~~~~~~~~~~~~
```

## Quick Start

```bash
git clone https://github.com/wolfssl/wolfBoot.git
cd wolfBoot
git submodule update --init

## Use make
# edit your .config or copy from config/examples
make

## OR ##

# use cmake via wolfbuild.sh script:

./tools/scripts/wolfboot_build.sh --CLEAN
./tools/scripts/wolfboot_build.sh --CLEAN  stm32h7
./tools/scripts/wolfboot_build.sh --target stm32h7
```

### VS Code

Windows users may need one of these:

- [Visual Studio 2022](https://visualstudio.microsoft.com/)
- [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/). See `C:\Program Files(x86)\Windows kits`.

#### Launch Stand-alone VS Code

The MSVC kit may be needed if VS 2022 is not installed.

Select `View` - `Command Palette`, search for CMake: Select a Compiler

See also: CMake: Delete Cache and Reconfigure


#### Launch VS Code from VS 2022 Command prompt.

Delete any existing `build` or `build-[target]` directories as needed.

Open a VS 2022 Developer command prompt.

From the command prompt, open the `wolfBoot.code-workspace` VS Code workspace:

```dos
cd c:\workspace\wolfboot-%USERNAME%
code ./IDE/VSCode/wolfBoot.code-workspace
```

### Visual Studio IDE

For the `Select Startup Item`, leave at default. Do not select `image`, wolfboot_name[], etc.

Right click on `CMakeLists.txt` and select `Delete Cache and Reconfigure`.

Right click on `CMakeLists.txt` and select `Build`.

### Visual Studio Command Prompt

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

## Your own toolchain

Create a `CMakeUserPresets.json` (ignored by git, see and rename `cmake/CMakeUserPresets.json.sample` ):

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
