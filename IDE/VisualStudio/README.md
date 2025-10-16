# wolfboot Visual Studio

Users of Visual Studio can open the `WOLFBOOT_ROOT` directory without the need for a project file.

Visual Studio is "cmake-aware" and recognizes the [CMakePresets.json](../../CMakePresets.json)

Select a device from the ribbon bar, shown here for the `stm32l4`

<img width="727" height="108" alt="image" src="https://github.com/user-attachments/assets/4d3e8300-e89f-4e7a-9e84-a32a284ad719" /><br /><br />


From `Solution Explorer`, right-click `CmakeLists.txt` and then select `Configure wolfBoot`.

<img width="688" height="592" alt="image" src="https://github.com/user-attachments/assets/41b11094-adbb-473a-9c5a-d004d9a7a91b" /><br /><br />

To build, follow the same steps to right click, and select `Build`.

View the CMake and Build messages in the `Output` Window. Noter the dropdown to select view:

<img width="721" height="627" alt="image" src="https://github.com/user-attachments/assets/6a22bb23-a99c-45ec-a989-2d54360fc384" /><br /><br />

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


For more details, see the [cmake/README](../../cmake/README.md) file.
