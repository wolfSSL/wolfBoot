# Config Example Files

This directory contains example `.config` presets for various target devices.

## Make

The examples here can be copied directly to the project `.config` file to use with `make`.

## CMake

See the [CMakePresets.json](../../CMakePresets.json) file.

Config files can be added or updated to the `CMakePresets.json` like this:

```bash
python3 ./tools/scripts/config2presets.py ./config/examples/stm32h7.config

# then test it:

./tools/scripts/wolfboot_cmake_full_build.sh --target stm32h7
```

## Troubleshooting

The wrong toolchain is being used, or a target was not specified

```
Error: no such instruction: `isb'
```
