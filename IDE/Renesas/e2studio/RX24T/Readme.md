## wolfBoot for Renesas RX24T (CC-RX, e2 studio)

## 1. Overview

This example builds wolfBoot and a sample application for the Renesas Starter
Kit for RX24T with the CC-RX compiler in e2 studio. The application v1 is
updated to v2 by wolfBoot. Both versions are the same program; they print
their version and the state of the two partitions.

- Signature ECC256, hash SHA256, 256-byte image header, software crypto.
- The flash layout, image header and partition flags are the same as in the
  GCC build (`config/examples/renesas-rx24t.config`). Build wolfBoot and the
  application with the same compiler: the GCC wolfBoot calls the address
  stored at the application start, while the CC-RX wolfBoot jumps to the
  application start. See
  [docs/Targets.md](../../../../docs/Targets.md#renesas-rx24t).
- The console is SCI1 (PD3/PD5) on the board's USB serial port at 115200 8N1,
  driven by wolfBoot's own UART code (`DEBUG_UART`). The BSP stdio is not
  used, so no debugger is needed to see the output.

| Region | Address | Size |
| --- | --- | --- |
| Boot partition (application) | 0xFFFC0000 | 0x18000 |
| Update partition | 0xFFFD8000 | 0x18000 |
| Swap sector | 0xFFFF0000 | 0x800 |
| wolfBoot, with the vector tables, OFS and ID code | 0xFFFF8000 | 0x8000 |

## 2. Components and Tools

| Item | Name/Version | Note |
| :-- | :-- | :-- |
| Board | Renesas Starter Kit for RX24T | R5F524TAADFP |
| IDE | e2 studio 2026-10 | |
| Compiler | CC-RX V3.08.00 | |
| BSP | r_bsp 7.71 | Downloaded by the Smart Configurator |
| Flash programmer | Renesas Flash Programmer V3 (GUI or `rfp-cli`) | E2 emulator Lite, FINE interface |
| rx-elf-objcopy | GCC for Renesas RX | Used by `make_images.sh` |
| Key tools | keygen and sign | Included in wolfBoot |

Board settings: leave J8 open (single-chip mode). Power the board from its
5V jack.

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
   select `<wolfBoot>/IDE/Renesas/e2studio/RX24T`, and import `wolfBoot` and
   `app_RenesasRX01`. Do not tick "Copy projects into workspace": the
   projects link to the wolfBoot sources by relative path.
2. Open `wolfBoot.scfg`, let the Smart Configurator download r_bsp 7.71 if
   asked, and press "Generate Code". Do the same with
   `app_RenesasRX01.scfg`. The code goes to `src/smc_gen` of each project.

The Smart Configurator settings are already made:

| Setting | wolfBoot | app_RenesasRX01 |
| :-- | :-- | :-- |
| Clocks | HOCO 32 MHz, PLL /4 x10: ICLK 80 MHz, PCLKA 80 MHz, PCLKB/D 40 MHz, FCLK 20 MHz | same |
| `BSP_CFG_USER_STACK_ENABLE` | 0 | 0 |
| `BSP_CFG_ISTACK_BYTES` | `0x1400` | `0x1400` |
| `BSP_CFG_HEAP_BYTES` | `0` | `0` |
| `BSP_CFG_IO_LIB_ENABLE` | 0 | 0 |
| `BSP_CFG_ID_CODE_LONG_1`-`4` | `0x45010203`, `0x04050607`, `0x08090A0B`, `0x0C0D0E0F` | not used (left out of the image) |
| `BSP_CFG_ROM_CODE_PROTECT_VALUE` | `0xFFFFFFFF` | not used |

The wolfBoot clocks must stay as listed: the flash driver and the UART take
FCLK and PCLKB from these settings, and its delays assume ICLK 80 MHz at most.

### 3-3 The boot mode ID code

The BSP writes the ID code to `0xFFFFFFA0`, inside the wolfBoot image.
This project has the example ID to enable the boot protection. See
[docs/Targets.md](../../../../docs/Targets.md#rx24t-boot-mode-id-code).

### 3-4 Build

Build both projects (configuration HardwareDebug). The project settings are
preset:

| Setting | wolfBoot | app_RenesasRX01 |
| :-- | :-- | :-- |
| Macros | `__WOLFBOOT`, `TARGET_rx24t`, `__RX__`, `WOLFSSL_USER_SETTINGS` | `WOLFBOOT_RENESAS_APP`, `TARGET_rx24t`, `__RX__`, `WOLFSSL_USER_SETTINGS`, `XMEMCPY=memcpy` |
| Pre-include | `../../include/user_settings.h` (sets `DEBUG_UART`), `../../include/target.h` | same |
| Compiler options | `-branch=32`, `-goptimize` | same |
| Linker optimization | `-optimize=symbol_delete` (unreferenced functions and variables are removed) | same |
| Standard library | `string.h` only, no `stdio.h` | same |
| ROM to RAM mapped sections | `D=R`, `D_1=R_1`, `D_2=R_2`, `PFRAM=RPFRAM` | same |
| Code start | `0xFFFF8000` | `0xFFFC0100` (after the 256-byte header) |

The outputs are `wolfBoot.x` and `app_RenesasRX01.x` under each project's
`HardwareDebug` folder.

### 3-5 Make the images

```
$ ./IDE/Renesas/e2studio/RX24T/make_images.sh <RX_ELF_BIN_DIR> ./wolfboot_signing_private_key.der <OUTPUT_DIR>
```

`<RX_ELF_BIN_DIR>` is the folder containing `rx-elf-objcopy`. The script
writes:

| File | Address |
| :-- | :-- |
| `wolfboot.bin` | 0xFFFF8000 (32 KB, wolfBoot, the vector tables, OFS and ID code) |
| `app_RenesasRX01_v1_signed.bin` | 0xFFFC0000 (boot partition) |
| `app_RenesasRX01_v2_signed.bin` | 0xFFFD8000 (update partition) |

### 3-6 Program the board

With Renesas Flash Programmer, program the three binary files at the
addresses above, for example:

```
rfp-cli -if fine -t e2l -device RX200 -pv -auth id 450102030405060708090A0B0C0D0E0F \
    -bin FFFF8000 wolfboot.bin \
    -bin FFFC0000 app_RenesasRX01_v1_signed.bin \
    -bin FFFD8000 app_RenesasRX01_v2_signed.bin \
    -run
```

Do not program the `.mot` or `.abs` files: the application's ones carry an
all-`FF` ID code, which replaces wolfBoot's and makes the next connection
erase the flash.

### 3-7 Run

Open the board's USB serial port at 115200 8N1 and reset the board (or use
`-run` above). wolfBoot verifies and starts v1:

```
wolfBoot HAL Init
RAM Function start = 0x1D94
RAM Function end   = 0x2C0F
RAM Function size  = 3707
Boot partition: 0xFFFC0000 (sz 6993, ver 0x1, type 0x201)
Partition 1 header magic 0xFFFFFFFF invalid at 0xFFFD8000
Boot partition: 0xFFFC0000 (sz 6993, ver 0x1, type 0x201)
Booting version: 0x1
Checking integrity...done
Verifying signature...done
wolfBoot HAL Init
RAM Function start = 0x1D74
RAM Function end   = 0x242B
RAM Function size  = 1719
| ------------------------------------------------------------------- |
| Renesas RX User Application in BOOT partition started by wolfBoot   |
| ------------------------------------------------------------------- |


=== Boot Partition[FFFC0000] ===
Magic:    WOLF
Version:  01
Status:   FF (New)
Trailer Mgc: none

=== Update Partition[FFFD8000] ===
Magic:    ����
Version:  00
Status:   FF (New)
Trailer Mgc: none

Current Firmware Version: 1
Hit any key to call wolfBoot_success the firmware.
```

This output is from a board programmed without v2. Type a key in the
terminal. The application calls `wolfBoot_success()` (boot partition
`00 (Success)`), then waits for a second key and calls
`wolfBoot_update_trigger()` (update partition `70 (Updating)`).

### 3-8 Update to v2

With v2 in the update partition, reset the board after the trigger.
wolfBoot swaps the partitions and starts v2:

```
Starting Update (fallback allowed 0)
Update partition: 0xFFFD8000 (sz 6993, ver 0x2, type 0x201)
Boot partition: 0xFFFC0000 (sz 6993, ver 0x1, type 0x201)
Versions: Current 0x1, Update 0x2
Copy sector 0 (part 1->2)
Copy sector 0 (part 0->1)
Copy sector 0 (part 2->0)
...
Erasing remainder of partition (42 sectors)...
...
Copied boot sector to swap
Boot partition: 0xFFFC0000 (sz 6993, ver 0x2, type 0x201)
Booting version: 0x2
Checking integrity...done
Verifying signature...done
...
=== Boot Partition[FFFC0000] ===
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
  [docs/Targets.md](../../../../docs/Targets.md#renesas-rx24t).
- wolfBoot uses about 11 KB of the 16 KB RAM, including the 5 KB stack
  (`BSP_CFG_ISTACK_BYTES`); the deepest path, an update, uses about 3.7 KB
  of it.
- An e2 studio debug session on the RX24T can rewrite code flash, including
  the ID code. After a session, program `wolfboot.bin` again before any
  other `rfp-cli` connection.
- `include/user_settings.h` sets the include guard of the wolfBoot
  `include/user_settings.h`, which CC-RX could otherwise find next to the
  wolfBoot headers and add to this configuration.
