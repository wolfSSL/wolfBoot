# ELF Parser Tool

This tool demonstrates parsing ELF32/ELF64, showing the clear/load sections and entry point.

## Building elf-parser

From root: `make elf-parser`
OR
From `tools/elf-parser` use `make clean && make`

## Example Output

```sh
% ./tools/elf-parser/elf-parser wolfboot.elf
ELF Parser:
Loading elf at 0x7f9a18008000
Found valid elf32 (little endian)
Program Headers 2 (size 32)
	Load 18340 bytes (offset 0x10000) to 0x8000000 (p 0x8000000)
	Clear 72 bytes at 0x20000000 (p 0x80047a4)
Entry point 0x8000000
Return 0, Load 0x8000000
```
