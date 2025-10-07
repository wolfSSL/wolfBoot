# Config Example Files

This directory contains example `.config` presets for various target devices.

## Make

The examples here can be copied directly to the project `.config` file to use with `make`.

## CMake

See the [CMakePresets.json](../../CMakePresets.json) file.

Config files can be added or updated to the `CMakePresets.json` like this:

```bash
python3 config2presets.py ./config/examples/stm32h7.config
```
