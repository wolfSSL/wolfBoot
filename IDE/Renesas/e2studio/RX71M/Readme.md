## wolfBoot for Renesas RX71M (CC-RX, e2 studio)

## 1. Overview

This example builds wolfBoot and a sample application for the Renesas Starter
Kit+ for RX71M with the CC-RX compiler in e2 studio. The application v1 is
updated to v2 by wolfBoot. Both versions are the same program; they print
their version and the state of the two partitions.

- Signature ECC256, hash SHA256, 256-byte image header, software crypto (the
  RX71M has no TSIP).
- The flash layout, image header and partition flags are the same as in the
  GCC build (`config/examples/renesas-rx71m.config`). Build wolfBoot and the
  application with the same compiler: the GCC wolfBoot calls the address
  stored at the application start, while the CC-RX wolfBoot jumps to the
  application start. See
  [docs/Targets.md](../../../../docs/Targets.md#renesas-rx71m).
- `printf` and `getchar` use the BSP stdio, which goes to the e2 studio
  **Renesas Debug Virtual Console**. It works only while an e2 studio debug
  session is connected: without one, wolfBoot waits at its first `printf`.
  For console output without the debugger, use the GCC build with
  `DEBUG_UART=1` (SCI7).

| Region | Address | Size |
| --- | --- | --- |
| Update partition | 0xFFC00000 | 0x1D0000 |
| Boot partition (application) | 0xFFE00000 | 0x1D0000 |
| Swap sector | 0xFFFD0000 | 0x8000 |
| wolfBoot, with the vector tables | 0xFFFF0000 | 0x10000 |

## 2. Components and Tools

| Item | Name/Version | Note |
| :-- | :-- | :-- |
| Board | Renesas Starter Kit+ for RX71M | R5F571MLCDFC, 24 MHz crystal |
| IDE | e2 studio 2026-10 | |
| Compiler | CC-RX V3.08.00 | |
| BSP | r_bsp 7.71 | Downloaded by the Smart Configurator |
| Flash programmer | Renesas Flash Programmer V3 (GUI or `rfp-cli`) | E2 emulator Lite |
| rx-elf-objcopy | GCC for Renesas RX | Used by `make_images.sh` |
| Key tools | keygen and sign | Included in wolfBoot |

Board settings: SW4 pins 1 and 2 OFF (single-chip mode). Power the board from
its 5V jack; in the debug configuration set "Power Target From The Emulator"
to No.

## 3. How to build and use

### 3-1 Key generation

```
$ cd <wolfBoot>
$ make keytools
$ ./tools/keytools/keygen --ecc256 -g ./wolfboot_signing_private_key.der
```

The public key is written to `src/keystore.c`, which the wolfBoot project
compiles. Rebuild wolfBoot after generating a new key, and sign the
application with the same private key.

### 3-2 Import the projects and generate the BSP code

1. In e2 studio, File > Import > General > Existing Projects into Workspace,
   select `<wolfBoot>/IDE/Renesas/e2studio/RX71M`, and import `wolfBoot` and
   `app_RenesasRX01`. Do not tick "Copy projects into workspace": the
   projects link to the wolfBoot sources by relative path.
2. Open `wolfBoot.scfg`, let the Smart Configurator download r_bsp 7.71 if
   asked, and press "Generate Code". Do the same with
   `app_RenesasRX01.scfg`. The code goes to `src/smc_gen` of each project.

The Smart Configurator settings are already made:

| Setting | wolfBoot | app_RenesasRX01 |
| :-- | :-- | :-- |
| Clocks | Main 24 MHz, PLL x10: ICLK 240 MHz, PCLKA 120 MHz, PCLKB/C/D 60 MHz, FCLK 60 MHz | same |
| `BSP_CFG_USER_STACK_ENABLE` | 0 | 1 (`0x1000` bytes) |
| `BSP_CFG_ISTACK_BYTES` | `0x2000` | `0x400` |
| `BSP_CFG_HEAP_BYTES` | `0xF000` | `0x400` |
| `BSP_CFG_IO_LIB_ENABLE` | 1 | 1 |

### 3-3 Build

Build both projects (configuration HardwareDebug). The project settings are
preset:

| Setting | wolfBoot | app_RenesasRX01 |
| :-- | :-- | :-- |
| Macros | `__WOLFBOOT`, `TARGET_rx71m`, `__RX__`, `WOLFSSL_USER_SETTINGS` | `WOLFBOOT_RENESAS_APP`, `TARGET_rx71m`, `__RX__`, `WOLFSSL_USER_SETTINGS`, `XMEMCPY=memcpy` |
| Pre-include | `../../include/user_settings.h`, `../../include/target.h` | same |
| Compiler options | `-branch=32`, `-goptimize` | same |
| Linker optimization | `-optimize=symbol_delete` (unreferenced functions and variables are removed) | same |
| ROM to RAM mapped sections | `D=R`, `D_1=R_1`, `D_2=R_2`, `PFRAM=RPFRAM` | same |
| Code start | `0xFFFF0000` | `0xFFE00100` (after the 256-byte header) |

The flash driver in `hal/renesas-rx.c` and the flash update code run from RAM:
they are placed in section `PFRAM` (`#pragma section FRAM`), linked at
`RPFRAM` in the RAM section group that starts at `0x1000`, and copied there by
`hal_flash_init()`, which prints the `RPFRAM` address range.

An application of your own that calls `wolfBoot_success()` or
`wolfBoot_update_trigger()` needs the same: compile `hal/renesas-rx.c`,
`src/libwolfboot.c` and `src/string.c` into it, map `PFRAM=RPFRAM`, place
`RPFRAM` in RAM in the `-start` option, and call `hal_init()` (which calls
`hal_flash_init()`) before either function.

The outputs are `wolfBoot.x` and `app_RenesasRX01.x` under each project's
`HardwareDebug` folder.

### 3-4 Make the images

```
$ ./IDE/Renesas/e2studio/RX71M/make_images.sh <RX_ELF_BIN_DIR> ./wolfboot_signing_private_key.der <OUTPUT_DIR>
```

`<RX_ELF_BIN_DIR>` is the folder containing `rx-elf-objcopy`. The script
writes:

| File | Address |
| :-- | :-- |
| `wolfboot.bin` | 0xFFFF0000 (64 KB, wolfBoot and the vector tables) |
| `app_RenesasRX01_v1_signed.bin` | 0xFFE00000 (boot partition) |
| `app_RenesasRX01_v2_signed.bin` | 0xFFC00000 (update partition) |

The images contain code flash only. The BSP also places the option-setting
memory (`0x00120040`, OFS0/OFS1/MDE) in every CC-RX output; the script leaves
it out, so it is never written by accident.

### 3-5 Program the board

With Renesas Flash Programmer, program the three binary files at the
addresses above, for example:

```
rfp-cli -if uart -t e2l -device RX71x -pv -auth id FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF \
    -bin FFFF0000 wolfboot.bin \
    -bin FFE00000 app_RenesasRX01_v1_signed.bin \
    -bin FFC00000 app_RenesasRX01_v2_signed.bin
```

`-pv` erases only the blocks that are written. Do not program the `.mot` or
`.abs` files: they include the option-setting memory.

### 3-6 Run with the e2 studio debugger

Create a "Renesas GDB Hardware Debugging" configuration:

- Main: C/C++ Application `wolfBoot/HardwareDebug/wolfBoot.x`.
- Debugger: E2 Lite, target R5F571ML, JTAG connection, EXTAL 24 MHz,
  "Power Target From The Emulator" No.
- Startup: in "Load image and symbols", set the `wolfBoot.x` entry to
  **Symbols only**. Downloading the image would also write the option-setting
  memory; the board already holds the programmed images.

Start the debug session, open Window > Show View > Renesas Debug Virtual
Console, and resume. wolfBoot verifies and starts v1:

```
RAM Function start = 0x1a748
RAM Function end   = 0x1b504
RAM Function size  = 3516
Boot partition: ffe00000 (sz 17618, ver 0x1, type 0x201)
Update partition: ffc00000 (sz 17618, ver 0x2, type 0x201)
Boot partition: ffe00000 (sz 17618, ver 0x1, type 0x201)
Booting version: 0x1
Checking integrity...done
Verifying signature...done
app_offset 0xffe00100
RAM Function start = 0xaf18
RAM Function end   = 0xb5cd
RAM Function size  = 1717
| ------------------------------------------------------------------- |
| Renesas RX User Application in BOOT partition started by wolfBoot   |
| ------------------------------------------------------------------- |


=== Boot Partition[ffe00000] ===
Magic:    WOLF
Version:  01
Status:   ff (New)
Trailer Mgc: none

=== Update Partition[ffc00000] ===
Magic:    WOLF
Version:  02
Status:   ff (New)
Trailer Mgc: none

Current Firmware Version: 1
Hit any key to call wolfBoot_success the firmware.
```

Type a key in the virtual console. The application calls
`wolfBoot_success()` (boot partition `00 (Success)`), then waits for a second
key and calls `wolfBoot_update_trigger()` (update partition `70 (Updating)`).
Breakpoints in the application make its hash check fail; to debug the
application, load `app_RenesasRX01.x` as symbols only and use hardware
breakpoints.

### 3-7 Update to v2

Reset and resume in e2 studio. wolfBoot swaps the partitions and starts v2:

```
Starting Update (fallback allowed 0)
Versions: Current 0x1, Update 0x2
Copy sector 0 (part 1->2)
Copy sector 0 (part 0->1)
Copy sector 0 (part 2->0)
Erasing remainder of partition (55 sectors)...
Copy sector 55 (part 0->2)
Copied boot sector to swap
Booting version: 0x2
Checking integrity...done
Verifying signature...done
...
=== Boot Partition[ffe00000] ===
Magic:    WOLF
Version:  02
Status:   10 (Testing)
Trailer Mgc: BOOT
```

Type a key to call `wolfBoot_success()`. If v2 is reset before that,
wolfBoot rolls back to v1.

## 4. Notes

- Code flash is rated for 1,000 erase cycles per block, and an update erases
  the flag sectors several times per image sector. See
  [docs/Targets.md](../../../../docs/Targets.md#renesas-rx71m).
- `include/user_settings.h` sets the include guard of the wolfBoot
  `include/user_settings.h`, which CC-RX could otherwise find next to the
  wolfBoot headers and add to this configuration.
