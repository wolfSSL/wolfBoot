# VS Code wolfBoot Project

CMake presets are support in VS Code. See also details in the [cmake/README.md](../../cmake/README).

Open the `WOLFBOOT_ROOT`[wolfBoot.code-workspace](../../wolfBoot.code-workspace) in VSCode.

For example, select `STM32L4` wait for CMake to finish, then click `build`. The `output` pane might be not visible. Grab frame to expand up:

<img width="848" height="217" alt="image" src="https://github.com/user-attachments/assets/39a67930-18f6-4ce2-b221-a475956ca672" /><br /><br />

-----

## Quick Start

If all of the prerequisites (see below) are already installed:

```bash
git clone https://github.com/wolfssl/wolfBoot.git
cd wolfBoot/IDE/VSCode
git submodule update --init

code wolfBoot.code-workspace
```

If all of the prerequisites are NOT installed, see the [install.sh](./install.sh) script.

## Windows

Windows users may need one of these:

- [Visual Studio 2022](https://visualstudio.microsoft.com/)
- [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/). See `C:\Program Files(x86)\Windows kits`.

The MSVC kit may be needed if VS 2022 is not installed.

Select `View` - `Command Palette`, search for CMake: Select a Compiler

See also: CMake: Delete Cache and Reconfigure

### Launch VS Code from VS 2022 Command prompt.

Delete any existing `build` or `build-[target]` directories as needed.

Open a VS 2022 Developer command prompt.

From the command prompt, open the `wolfBoot.code-workspace` VS Code workspace:

```dos
cd c:\workspace\wolfboot-%USERNAME%
code ./IDE/VSCode/wolfBoot.code-workspace
```

-----

## Additional Settings

See the [cmake/config_defaults.cmake](../../cmake/config_defaults.cmake) file. Of particular interest
are some environment configuration settings:

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

-----

## Requirements

### VS Code extensions

- [CMake Tools (ms-vscode.cmake-tools)](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
- C/C++ (ms-vscode.cpptools)
- Cortex-Debug (marus25.cortex-debug)

### Build tools

#### WSL path:

cmake, ninja-build, gcc-arm-none-eabi, openocd

#### Windows path:

Windows path: CMake, Ninja, Arm GNU Toolchain, OpenOCD (or STâ€™s OpenOCD)

Install via PowerShell (will need to restart VSCode):

```ps
winget install --id Ninja-build.Ninja -e


# winget install -e --id Arm.GnuArmEmbeddedToolchain

winget install -e --id Arm.GnuArmEmbeddedToolchain --override "/S /D=C:\Tools\arm-gnu-toolchain-14.2.rel1"
# reopen VS / terminal so PATH refreshes

# Confirm
ninja --version

Get-Command arm-none-eabi-gcc
```

If already installed, uninstall:

```
winget uninstall -e --id Arm.GnuArmEmbeddedToolchain
```
