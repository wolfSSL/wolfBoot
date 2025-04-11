# squashelf

`squashelf` is a command-line utility that processes ELF (Executable and Linkable Format) files. It extracts `PT_LOAD` segments, optionally filters them based on specified Load Memory Address (LMA) ranges, sorts them by LMA, and writes them to a new, reorganized ELF file. The output ELF file contains only the selected `PT_LOAD` segments and their corresponding data, potentially omitting the Section Header Table (SHT).

## Purpose

This tool can be useful for:

*   Creating stripped-down ELF files containing only loadable code and data segments.
*   Preparing ELF files for specific bootloaders or embedded systems environments that primarily work with `PT_LOAD` segments.

## Usage

```bash
squashelf [options] <input.elf> <output.elf>
```

## Options

*   `-n`, `--nosht`:
    Omit the Section Header Table (SHT) from the output ELF. By default, a minimal SHT with a single NULL section is created. Omitting the SHT shouldn't have any effect on loaders that only use PT_LOAD segments, but may cause tools like readelf to complain. Leave it in for max compatibility, or remove it for the smallest possible elf file.

*   `-r <min>-<max>[,<min>-<max>...]`, `--range <min>-<max>[,<min>-<max>...]`:
    Specify one or more LMA ranges. Only `PT_LOAD` segments fully contained within any of these ranges (inclusive of `min`, exclusive of `max`) will be included in the output. Addresses can be provided in decimal or hexadecimal (using `0x` prefix).
    Multiple ranges can be specified by separating them with commas.
    Example: `-r 0x10000-0x20000,0x30000-0x40000` or `-r 65536-131072,196608-262144`.

*   `-v`, `--verbose`:
    Enable verbose output, providing detailed information about the processing steps, segment selection, and file operations.

*   `-z`, `--zero-size-segments`:
    Include segments with zero file size in the output. By default, these segments are excluded.

*   `-h`, `--help`:
    Display a help message with detailed information about all available options and examples.

## Examples

*   Extract all `PT_LOAD` segments from `input.elf`, sort them by LMA, and write them to `output_all.elf` with a minimal SHT:
    ```bash
    squashelf input.elf output_all.elf
    ```

*   Extract `PT_LOAD` segments from `input.elf` that fall within the LMA range `0x80000000` to `0x8FFFFFFF`, omit the SHT, and write the result to `output_filtered.elf`:
    ```bash
    squashelf --nosht --range 0x80000000-0x8FFFFFFF input.elf output_filtered.elf
    ```

*   Extract segments from multiple memory regions with verbose output:
    ```bash
    squashelf -v --range 0x10000000-0x20000000,0x30000000-0x40000000 input.elf output_multi.elf
    ```

*   Include zero-size segments and show detailed processing information:
    ```bash
    squashelf -v -z --range 0x10000000-0x20000000 input.elf output_with_zeros.elf
    ```

*   Display help message with all options and examples:
    ```bash
    squashelf --help
    ```

## Dependencies

`squashelf` depends on `libelf`. You can typically install the development package for `libelf` using your system's package manager.

*   **Debian/Ubuntu:** `sudo apt-get install libelf-dev`

