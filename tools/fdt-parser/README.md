# Flattened Device Tree (FDT) Parser

This tool uses our internal FDT (fdt.c) parsing code to dump the device tree. There is also a `-t` option that tests making several updates to the device tree.

## Building fdt-parser

From root: `make fdt-parser`
OR
From `tools/fdt-parser` use `make clean && make`

## Example Output

```sh
% ./tools/fdt-parser/fdt-parser ./tools/fdt-parser/nxp_t1024.dtb
FDT Parser (./tools/fdt-parser/nxp_t1024.dtb):
FDT Version 17, Size 31102
root (node offset 0, depth 1, len 0):
	compatible (prop offset 8, len 13): fsl,T1024RDB
	#address-cells (prop offset 36, len 4): ....| 00 00 00 02
	#size-cells (prop offset 52, len 4): ....| 00 00 00 02
	interrupt-parent (prop offset 68, len 4): ....| 00 00 00 01
	model (prop offset 84, len 13): fsl,T1024RDB
	cpus (node offset 112, depth 2, len 4):
		power-isa-version (prop offset 124, len 5): 2.06
		power-isa-b (prop offset 144, len 0): NULL
		power-isa-e (prop offset 156, len 0): NULL
		power-isa-atb (prop offset 168, len 0): NULL
		power-isa-cs (prop offset 180, len 0): NULL
...
```
