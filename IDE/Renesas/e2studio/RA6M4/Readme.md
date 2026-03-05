# wolfBoot for Renesas RA6M4

## 1. Overview

This example demonstrates simple secure firmware update by wolfBoot.
A sample application v1 is securely updated to v2. Both versions behave the same except displaying its version of v1 or v2.
They are compiled by e2Studio and running on the target board.

In this demo, you may download two versions of the application binary file by Renesas Flash Programmer.
You can download and execute wolfBoot by e2Studio debugger. Use a USB connection between PC and the board for the debugger and flash programmer.

Please see `Readme_wSCE.md` for Renesas SCE use case.

## 2. Components and Tools

|Item|Name/Version|Note|
|:--|:--|:--|
|Board|Renesas EK-RA6M4||
|Device|R7FA6M4AF3CFB||
|Toolchain|GCC ARM Embedded 13.2 (arm-none-eabi)|Included in e2studio / GCC for Renesas RA|
|FSP Version|6.1.0|Bundled with e2studio or download from Renesas site|
|IDE|e2studio 2024-01 or later|Download from Renesas site|
|Flash Writer|Renesas Flash Programmer v3|Download from Renesas site|
|Binary tool|arm-none-eabi-objcopy|Included in GCC ARM toolchain|
|Key tool|keygen and sign|Included in wolfBoot (`tools/keytools`)|
|RTT Viewer|J-Link RTT Viewer|Included in J-Link Software Pack (SEGGER)|


|FIT Components|Version|
|:--|:--|
|Board Support Package Common Files|v6.1.0|
|I/O Port|v6.1.0|
|Arm CMSIS Version 6 - Core (M)|v6.x+fsp.6.1.0|
|RA6M4-EK Board Support Files|v6.1.0|
|Board support package for R7FA6M4AF3CFB|v6.1.0|
|Board support package for RA6M4|v6.1.0|
|Board support package for RA6M4 - FSP Data|v6.1.0|


e2Studio Project:\
wolfBoot      IDE/Renesas/e2studio/RA6M4/wolfBoot\
Sample app    IDE/Renesas/e2studio/RA6M4/app_RA


Flash Allocation:
```
+---------------------------+------------------------+-----+
| B |H|                     |H|                      |     |
| o |e|   Primary           |e|   Update             |Swap |
| o |a|   Partition         |a|   Partition          |Sect |
| t |d|   (448 KB)          |d|   (448 KB)           |64KB |
+---------------------------+------------------------+-----+
0x00000000: wolfBoot (64 KB)
0x00010000: Primary partition (Header, IMAGE_HEADER_SIZE = 0x200)
0x00010200: Primary partition (Application image)
0x00080000: Update  partition (Header)
0x00080200: Update  partition (Application image)
0x000F0000: Swap sector (64 KB)
```

## 3. Key Architecture Notes

### FSP Usage Summary

Both the **wolfBoot** and **app_RA** projects use the Renesas FSP, but only for the following:

| FSP Role | wolfBoot | app_RA |
|:--|:--|:--|
| `SystemInit` (clock init, cache, early BSP) | ✓ | ✓ |
| `SystemRuntimeInit` (.data copy, .bss zero) | **✗ Disabled** (`BSP_CFG_C_RUNTIME_INIT=0`) | ✓ |
| Clock configuration (PLL, PCLK, ICLK via `bsp_clocks.c`) | ✓ | ✓ |
| IOPORT pin configuration (`R_IOPORT_Open`) | — | ✓ |
| **Flash driver (`g_flash0 Flash(r_flash_hp)`)** | **✗ Not used** | **✗ Not used** |

wolfBoot sets **C Runtime Initialization = Disabled** in the FSP Smart Configurator
(BSP tab → RA Common → C Runtime Initialization).
Instead, wolfBoot copies the `.ram_code_from_flash` / `.data` sections and zeros `.bss`
manually inside `hal_init()` → `copy_ram_sections()` (`hal/renesas-ra.c`).

The FSP provides the indispensable clock and startup infrastructure (`SystemInit`),
but RAM section initialisation and flash erase/write are handled by wolfBoot itself.

### Direct FACI HP Flash Access (No FSP Flash Driver)

wolfBoot accesses the RA6M4 code flash directly via FACI HP registers (`hal/renesas-ra.c`, `hal/renesas-ra.h`).
**The FSP `g_flash0 Flash(r_flash_hp)` driver is NOT used** — do not add it to either project.

Direct register access requires all flash erase/write functions to run from RAM (`RAMFUNCTION`).
The `RAMFUNCTION` macro is defined in `wolfBoot/user_settings.h`:

```c
#define RAMFUNCTION \
    __attribute__((used, noinline, section(".ram_code_from_flash"), long_call))
```

> **Important**: The `noinline` attribute is required. Without it, GCC `-O2` (`-finline-functions`)
> can inline RAMFUNCTION bodies into flash-resident callers, causing a HardFault when code flash
> is in P/E mode (instruction fetches from flash are prohibited during P/E).

### wolfBoot Warm Start in app_RA

When the application is launched by wolfBoot, wolfBoot's RAM functions occupy the lower part of RAM.
The application's own RAM functions (`__ram_from_flash$$` section) must be copied to RAM
**before** `SystemRuntimeInit()` calls `memcpy()` — otherwise `memcpy()` itself is not yet in place.

This is handled by `wolfboot_pre_init()` in `app_RA/src/wolfboot_startup.c`, called from
`R_BSP_WarmStart(BSP_WARM_START_RESET)` in `app_RA/src/hal_warmstart.c`.

## 4. How to Build and Use

### 1) Key Generation

Build and install the key tools on your host (Linux, Windows, or macOS).
See the wolfBoot user manual for toolchain prerequisites.

```
$ cd <wolfBoot>
$ export PATH=$PATH:<wolfBoot>/tools/keytools
$ keygen --rsa2048 -g ./pri-rsa2048.der
```

The `keygen` tool writes the public key into `src/keystore.c` for linking with wolfBoot.
Other supported algorithms: `--ed25519 --ed448 --ecc256 --ecc384 --ecc521 --rsa2048 --rsa3072 --rsa4096`

### 2) Build wolfBoot

Open the project `IDE/Renesas/e2studio/RA6M4/wolfBoot` in e2studio and build.

> **Note**: `configuration.xml` (FSP Smart Configurator project file) is not stored in the
> repository. You must generate the FSP files (`ra_gen/`, `ra_cfg/`) using a `dummy_library`
> project as described below.

#### 2-1) Create `dummy_library` to Generate FSP Files

+ Click **File** → **New** → **RA C/C++ Project**
+ Select `EK-RA6M4` from the board drop-down list
+ Check **Static Library**
+ Select **No RTOS**. Click Next
+ Check **Bare Metal Minimal**. Click Finish
+ Open Smart Configurator by clicking `configuration.xml` in the project
+ Go to **BSP** tab → **RA Common** on the Properties page:
  + Set **Main Stack Size (bytes)** to `0x2000`
  + Set **Heap Size (bytes)** to `0x10000`
  + Set **C Runtime Initialization** to `Disabled`
+ Save the `dummy_library` FSP configuration
+ Copy `configuration.xml` and `pincfg` from `dummy_library` into `wolfBoot`
+ Open Smart Configurator by clicking the copied `configuration.xml`
+ Click **Generate Project Content**
+ Build the `wolfBoot` project

Key BSP settings configured above (`wolfBoot/ra_cfg/fsp_cfg/bsp/bsp_cfg.h`):

|Setting|Value|Note|
|:--|:--|:--|
|`BSP_CFG_STACK_MAIN_BYTES`|`0x2000`|8 KB main stack|
|`BSP_CFG_HEAP_BYTES`|`0x10000`|64 KB heap (wolfCrypt)|
|`BSP_CFG_C_RUNTIME_INIT`|`0`|wolfBoot copies RAM sections manually in `hal_init()`|

wolfBoot initialises the FACI HP directly; **no FSP flash stack (`g_flash0`) is required**.

To enable debug output over UART (SCI7, P613=TXD7, J23 connector), edit `wolfBoot/user_settings.h`:
```c
#define DEBUG_UART
#define PRINTF_ENABLED
```

#### 2-2) Modify wolfBoot hal_entry.c

`wolfBoot/src/hal_entry.c` is **not stored in the repository** (Renesas copyright, FSP-generated).
After FSP generates the file, replace the `/* TODO */` body with the `loader_main()` call:

```c
#include <stdint.h>
#include "hal_data.h"

void hal_entry(void)
{
    loader_main(); /* wolfBoot entry */
#if BSP_TZ_SECURE_BUILD
    R_BSP_NonSecureEnter();
#endif
}
```

> **Important**: FSP regeneration (clicking **Generate Project Content**) may overwrite
> `hal_entry.c`. If that happens, restore the `loader_main()` call manually.
> Without it, wolfBoot will halt in `hal_entry()` without ever verifying or booting the application.

### 3) Build the Sample Application

Open the project `IDE/Renesas/e2studio/RA6M4/app_RA` in e2studio and build.

> **Note**: `configuration.xml` is not stored in the repository.
> You must generate the FSP files using a `dummy_application` project as described below.

#### 3-1) Create `dummy_application` to Generate FSP Files

+ Click **File** → **New** → **RA C/C++ Project**
+ Select `EK-RA6M4` from the board drop-down list
+ Check **Executable**
+ Select **No RTOS**. Click Next
+ Check **Bare Metal Minimal**. Click Finish
+ Open Smart Configurator by clicking `configuration.xml` in the project
+ Go to **BSP** tab → **RA Common**: leave default settings (C Runtime Initialization = Enabled)
+ Save the `dummy_application` FSP configuration
+ Copy `configuration.xml` and `pincfg` from `dummy_application` into `app_RA`
+ Open Smart Configurator by clicking the copied `configuration.xml`
+ Click **Generate Project Content**
+ Build the `app_RA` project

#### 3-2) Application Linker Script

`app_RA/script/fsp.ld` contains wolfBoot-specific overrides (already in the project):

```ld
// FLASH_START  = 0x00010200;   /* wolfBoot 64 KB + image header 512 B */
// FLASH_LENGTH = 0x000EFE00;
```

Uncomment these two lines only if you need to run the app **standalone** (without wolfBoot).
Leave them commented out for the normal wolfBoot-booted build.

#### 3-3) Required Source Files in app_RA

**Copy from the wolfBoot repository** into `app_RA/src/`:

|File|Source in repo|Purpose|
|:--|:--|:--|
|`wolfboot_startup.c`|`IDE/Renesas/e2studio/RA6M4/app_RA/src/wolfboot_startup.c`|Defines `wolfboot_pre_init()` — copies `__ram_from_flash$$` to RAM before `SystemRuntimeInit` runs|
|`app_RA.c`|`IDE/Renesas/e2studio/RA6M4/app_RA/src/app_RA.c`|Main application logic|

**Modify the FSP-generated `hal_entry.c`** (Renesas copyright, not stored in repo):

After FSP generates `hal_entry.c`, add the following:

1. At the top of the file, after `#include "hal_data.h"`, add:

```c
#include <stdint.h>

/* Enable flash P/E (required for wolfBoot_success() when running standalone). */
#define R_SYSTEM_FWEPROR    (*(volatile uint8_t *)0x4001E416UL)
#define FWEPROR_FLWE_ENABLE (0x01U)

void app_RA(void);
```

2. Inside `hal_entry()`, replace the `/* TODO */` comment with:

```c
void hal_entry(void)
{
    R_SYSTEM_FWEPROR = FWEPROR_FLWE_ENABLE;   /* <-- add: enable code flash P/E */
    app_RA();                                   /* <-- add: call application     */
#if BSP_TZ_SECURE_BUILD
    R_BSP_NonSecureEnter();
#endif
}
```

**Modify the FSP-generated `hal_warmstart.c`** (Renesas copyright, not stored in repo):

After FSP generates `hal_warmstart.c`, add the following two lines manually:

1. After `FSP_CPP_FOOTER`, add the forward declaration:

```c
/* wolfBoot-specific early startup — see wolfboot_startup.c */
extern void wolfboot_pre_init(void);
```

2. Inside the `BSP_WARM_START_RESET` block, call `wolfboot_pre_init()`:

```c
void R_BSP_WarmStart (bsp_warm_start_event_t event)
{
    if (BSP_WARM_START_RESET == event)
    {
        /* Copy APP RAM functions before SystemRuntimeInit runs.
         * Required when booted by wolfBoot — see wolfboot_startup.c for details. */
        wolfboot_pre_init();   /* <-- add this line */
    }

    if (BSP_WARM_START_POST_C == event)
    {
        R_IOPORT_Open(&IOPORT_CFG_CTRL, &IOPORT_CFG_NAME);
    }
}
```

**Why this is necessary**: wolfBoot's RAM functions occupy the lower RAM area when the app starts.
The app's own RAM functions (`__ram_from_flash$$` section) must be copied to their correct VMA
addresses *before* `SystemRuntimeInit()` calls `memcpy()` — because `memcpy()` itself is one
of those RAM functions. `wolfboot_pre_init()` performs this copy using a plain word loop that
requires no library functions.

#### 3-4) SEGGER RTT for Logging

  + Download J-Link Software from [SEGGER](https://www.segger.com/downloads/jlink) and choose `J-Link Software and Documentation Pack`
  + Copy the following files from `<JLink install>/Samples/RTT/` to `app_RA/src/SEGGER_RTT/`:

    ```
    SEGGER_RTT.c
    SEGGER_RTT.h
    SEGGER_RTT_Conf.h
    SEGGER_RTT_printf.c
    ```

  + Open `SEGGER_RTT_Conf.h` and set `SEGGER_RTT_MEMCPY_USE_BYTELOOP` to `1`

  + To find the RTT Control Block address, search for `_SEGGER_RTT` in the build's `.map` file after each build:

    ```
    grep _SEGGER_RTT app_RA/Debug/app_RA.map
    # Example output:
    #   0x20000eb0   _SEGGER_RTT
    ```

    In J-Link RTT Viewer, set **RTT Control Block** to **Address** `0x20000eb0`
    (the exact address changes per build and optimization level).

    Alternatively, use **Search Range** `0x20000000 0x10000` for automatic detection.

> **Note on optimization**: The RTT Control Block address shifts with `-O2` vs `-O0`/-O1`
> because code size affects `.bss` layout. Always re-check the map file after changing
> the optimization level.

#### 3-5) Optimization Level

The default build uses `-O0` or `-O1`. If you set `-O2`:

- Ensure `RAMFUNCTION` in `wolfBoot/user_settings.h` includes `noinline`:
  ```c
  #define RAMFUNCTION \
      __attribute__((used, noinline, section(".ram_code_from_flash"), long_call))
  ```
- Re-check the RTT Control Block address in the `.map` file.

### 4) Convert ELF to Binary

#### 4-1) Using the provided script (recommended)

`IDE/Renesas/e2studio/RA6M4/elf2hex.sh` automates steps 4–5 for both v1 and v2.
Run it from WSL or an equivalent Bash environment:

```bash
$ cd IDE/Renesas/e2studio/RA6M4
$ ./elf2hex.sh 0 <wolfBoot_dir> <arm-none-eabi-toolchain-bin-dir>
# e.g.:
$ ./elf2hex.sh 0 /mnt/c/workspace/wolfBoot /mnt/c/toolchain/gcc-arm/bin
```

This produces `app_RA_v1.0_signed.hex` and `app_RA_v2.0_signed.hex` ready for Renesas Flash Programmer.

#### 4-2) Manual Steps

The FSP linker uses `$$`-delimited section names. Use `arm-none-eabi-objcopy` with the
following sections to produce a flat binary:

```bash
$ arm-none-eabi-objcopy -O binary --gap-fill=0xff \
    -j '__flash_vectors$$'       \
    -j '__flash_readonly$$'      \
    -j '__flash_ctor$$'          \
    -j '__flash_preinit_array$$' \
    -j '__flash_.got$$'          \
    -j '__flash_init_array$$'    \
    -j '__flash_fini_array$$'    \
    -j '__flash_arm.extab$$'     \
    -j '__flash_arm.exidx$$'     \
    -j '__ram_from_flash$$'      \
    app_RA.elf app_RA.bin
```

> **Note**: Shell quoting of `$$` is required on Bash. Wrap each section name in
> single quotes when running interactively, or use the script above.

### 5) Sign and Generate Hex

```bash
$ cd <wolfBoot>
# Sign version 1
$ sign --rsa2048 app_RA.bin ./pri-rsa2048.der 1.0
# Output: app_RA_v1.0_signed.bin

# Convert to SREC (Primary partition starts at 0x00010000)
$ arm-none-eabi-objcopy -I binary -O srec \
    --change-addresses=0x00010000 \
    app_RA_v1.0_signed.bin app_RA_v1.0_signed.hex
```

### 6) Download app V1 and Execute Initial Boot

Flash `app_RA_v1.0_signed.hex` to the board using Renesas Flash Programmer (partition base `0x00010000`).

Then start wolfBoot via e2studio debugger:
1. Right-click the **wolfBoot** project → **Debug As** → **Renesas GDB Hardware Debugging**
2. Select **J-Link ARM** → OK
3. Select **R7FA6M4AF** → OK

Expected RTT output:

```
| ------------------------------------------------------------------- |
| Renesas RA User Application in BOOT partition started by wolfBoot   |
| ------------------------------------------------------------------- |

WOLFBOOT_PARTITION_SIZE:           0x00070000
WOLFBOOT_PARTITION_BOOT_ADDRESS:   0x00010000
WOLFBOOT_PARTITION_UPDATE_ADDRESS: 0x00080000

Application Entry Address:         0x00010200

=== Boot Partition[00010000] ===
Magic:    WOLF
Version:  01
Status:   FF
Trailer Mgc:

=== Update Partition[00080000] ===
Magic:
Version:  00
Status:   FF
Trailer Mgc:
Current Firmware Version : 1

Calling wolfBoot_success()
Called wolfBoot_success()
=== Boot Partition[00010000] ===
Magic:    WOLF
Version:  01
Status:   00
Trailer Mgc: BOOT

=== Update Partition[00080000] ===
Magic:
Version:  00
Status:   FF
Trailer Mgc:
```

State `00` = Success; Trailer Magic `BOOT` confirms the partition is marked good.
V1 also calls `wolfBoot_update_trigger()` so wolfBoot will look for a V2 image.
LEDs blink in sequence (reverse order for V1).

### 7) Sign and Download app V2

```bash
# Sign version 2
$ sign --rsa2048 app_RA.bin ./pri-rsa2048.der 2.0

# Convert to SREC (Update partition starts at 0x00080000)
$ arm-none-eabi-objcopy -I binary -O srec \
    --change-addresses=0x00080000 \
    app_RA_v2.0_signed.bin app_RA_v2.0_signed.hex
```

Flash `app_RA_v2.0_signed.hex` to the board at base `0x00080000`.

### 8) Re-boot and Secure Update to V2

Reset the board (or re-run the debugger). wolfBoot verifies V2, swaps the partitions, and boots V2.

Expected RTT output:

```
| ------------------------------------------------------------------- |
| Renesas RA User Application in BOOT partition started by wolfBoot   |
| ------------------------------------------------------------------- |

WOLFBOOT_PARTITION_SIZE:           0x00070000
WOLFBOOT_PARTITION_BOOT_ADDRESS:   0x00010000
WOLFBOOT_PARTITION_UPDATE_ADDRESS: 0x00080000

Application Entry Address:         0x00010200

=== Boot Partition[00010000] ===
Magic:    WOLF
Version:  02
Status:   00
Trailer Mgc: BOOT

=== Update Partition[00080000] ===
Magic:    WOLF
Version:  01
Status:   FF
Trailer Mgc:
Current Firmware Version : 2

Calling wolfBoot_success()
Called wolfBoot_success()
=== Boot Partition[00010000] ===
Magic:    WOLF
Version:  02
Status:   00
Trailer Mgc: BOOT

=== Update Partition[00080000] ===
Magic:    WOLF
Version:  01
Status:   FF
Trailer Mgc:
```

`Current Firmware Version : 2` confirms the secure update succeeded.
LEDs blink in forward order for V2.
