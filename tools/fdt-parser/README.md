# Flattened Device Tree (FDT) Parser

This tool uses our internal FDT (fdt.c) parsing code to dump the device tree.

Use `-i` to parse a Flattened uImage Tree (FIT) image.

There is also a `-t` option that tests making several updates to the device tree (useful with the nxp_t1024.dtb).

## Building fdt-parser

From root: `make fdt-parser`
OR
From `tools/fdt-parser` use `make clean && make`

## Example FDT Output

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

## Example FIT Output

```sh
% ./tools/fdt-parser/fdt-parser -i ./tools/fdt-parser/lynx-test-arm.srp
FDT Parser (./tools/fdt-parser/lynx-test-arm.srp):
FDT Version 17, Size 164232633
FIT: Found 'conf@1' configuration
        description (len 46): LynxSecure 2024.06.0-96ce6f31a0 SRP (aarch64)
Kernel Image: kernel@1
        description (len 46): LynxSecure 2024.06.0-96ce6f31a0 SRP (aarch64)
        type (len 7): kernel
        os (len 6): linux
        arch (len 6): arm64
        compression (len 5): none
        load (len 4):
        entry (len 4):
        data (len 164186944): not rendering
FDT Image: fdt@1
        description (len 77): Flattened Device Tree blob for LynxSecure 2024.06.0-96ce6f31a0 SRP (aarch64)
        type (len 8): flat_dt
        arch (len 6): arm64
        compression (len 5): none
        padding (len 8):
        data (len 44770): not rendering
Return 0
```
