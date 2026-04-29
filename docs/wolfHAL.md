# wolfHAL Integration

wolfBoot supports [wolfHAL](https://github.com/wolfSSL/wolfHAL) as an alternative
hardware abstraction layer backend. wolfHAL provides portable drivers for common MCU
peripherals (clock, flash, GPIO, UART, SPI, etc.) with a consistent API across
platforms.

## Overview

The wolfHAL integration uses a single generic `TARGET=wolfhal` with a per-board
abstraction layer. All board-specific details — device instances, driver bindings,
build flags, and linker scripts — live in a self-contained board directory. Adding
support for a new board or MCU family requires no changes to the core build system or
HAL shim.

The integration uses wolfHAL's **direct API mapping** feature. Each platform
driver source provides an optional `#ifdef` block that renames its driver
functions to the top-level API names. When the corresponding
`WHAL_CFG_<TYPE>_API_MAPPING_<VARIANT>` flag is defined, the driver file
itself provides the definition of the top-level API — no wrapper, no vtable
indirection, no runtime null-check. Calling `whal_Flash_Write(&dev, ...)` links
directly to the platform driver's implementation.

The integration consists of four parts:

1. **Generic HAL shim** (`hal/wolfhal.c`) — implements the wolfBoot HAL API
   (`hal_flash_write`, `hal_flash_erase`, etc.) by calling the top-level wolfHAL
   API (`whal_Flash_Write`, `whal_Uart_Send`, etc.). This file is shared across
   all wolfHAL boards.

2. **Board directory** (`hal/boards/<board>/`) — contains three files that fully
   describe a board:
   - `board.h` — includes the chip-specific wolfHAL driver headers and defines
     any board-level pin/peripheral enums.
   - `board.c` — device instances (clock, flash, GPIO, UART), configuration
     structs, and `hal_init`/`hal_prepare_boot` implementations.
   - `board.mk` — build variables (`ARCH_FLASH_OFFSET`, `LSCRIPT_IN`, the
     `WHAL_CFG_*_API_MAPPING_*` flags, wolfHAL driver objects, `RAM_CODE`
     linker rules).

3. **Generic test application** (`test-app/app_wolfhal.c`) — demonstrates using
   wolfHAL peripherals (GPIO, UART) beyond what the bootloader needs, using the
   same top-level wolfHAL API.

4. **wolfHAL library** (`lib/wolfHAL/`) — the wolfHAL submodule containing the
   platform drivers.

### How It Fits Together

```
config/examples/wolfhal_<board>.config
  └─ TARGET=wolfhal  BOARD=<board>

arch.mk
  └─ Sets WOLFHAL_ROOT, CFLAGS += -Ihal/boards/$(BOARD)

Makefile
  └─ OBJS += hal/boards/$(BOARD)/board.o
  └─ include hal/boards/$(BOARD)/board.mk

hal/wolfhal.c  (generic — calls whal_Flash_Write, whal_Uart_Send, etc.)
  └─ #include "board.h"  (resolved via -I to the board directory)

hal/boards/<board>/
  ├─ board.h   (includes wolfHAL driver headers, pin enums)
  ├─ board.c   (device instances, hal_init, hal_prepare_boot)
  └─ board.mk  (WHAL_CFG_*_API_MAPPING_*, driver objects, RAM_CODE rules)
```

The `WHAL_CFG_*_API_MAPPING_*` flags cause each wolfHAL driver source to emit
its functions under the top-level API name. Since only one driver source per
device type is compiled, there is no conflict — and since no dispatch source
(e.g., `src/flash/flash.c`) is included, there is no vtable indirection. The
linker can garbage-collect any unused symbols with `-Wl,--gc-sections`.

## Configuration

A wolfHAL-based config requires two variables beyond the standard wolfBoot settings:

```
TARGET=wolfhal
BOARD=stm32wb_nucleo
```

- `TARGET=wolfhal` selects the generic wolfHAL HAL shim and build path.
- `BOARD` selects the board directory under `hal/boards/`.

See `config/examples/wolfhal_*.config` for complete examples.

## Adding a New Board

To add a new board, create a directory `hal/boards/<board_name>/` with three files:

### 1. `board.h` — Driver Headers and Pin Enums

Include the chip-specific wolfHAL driver headers and declare any board-level
enums (pin indices, peripheral identifiers):

```c
#ifndef WOLFHAL_BOARD_H
#define WOLFHAL_BOARD_H

#include <wolfHAL/clock/<family>_rcc.h>
#include <wolfHAL/flash/<family>_flash.h>
#include <wolfHAL/gpio/<family>_gpio.h>
#include <wolfHAL/uart/<family>_uart.h>

/* GPIO pin indices (matches pin array in board.c) */
enum {
    BOARD_LED_PIN,
    BOARD_UART_TX_PIN,
    BOARD_UART_RX_PIN,
    BOARD_PIN_COUNT,
};

#endif /* WOLFHAL_BOARD_H */
```

### 2. `board.c` — Device Instances and Initialization

Define the wolfHAL device instances and implement `hal_init` and `hal_prepare_boot`
using the top-level wolfHAL API. The file must export `g_wbFlash` (and `g_wbUart`
when `DEBUG_UART` is enabled) as non-static globals — these are referenced by
`hal/wolfhal.c` via `extern`.

```c
#include "hal.h"
#include "board.h"

/* Clock controller */
whal_Clock g_wbClock = {
    .regmap = { .base = ..., .size = 0x400 },
    .cfg = &(whal_<Family>Rcc_Cfg) { ... },
};

/* Flash */
whal_Flash g_wbFlash = {
    .regmap = { .base = ..., .size = 0x400 },
    .cfg = &(whal_<Family>Flash_Cfg) {
        .startAddr = 0x08000000,
        .size = ...,
    },
};

#ifdef DEBUG_UART
whal_Gpio g_wbGpio = { ... };
whal_Uart g_wbUart = { ... };
#endif

void hal_init(void)
{
    whal_Clock_Init(&g_wbClock);
    whal_Flash_Init(&g_wbFlash);
#ifdef DEBUG_UART
    whal_Gpio_Init(&g_wbGpio);
    whal_Uart_Init(&g_wbUart);
#endif
}

void hal_prepare_boot(void)
{
#ifdef DEBUG_UART
    whal_Uart_Deinit(&g_wbUart);
    whal_Gpio_Deinit(&g_wbGpio);
#endif
    whal_Flash_Deinit(&g_wbFlash);
    whal_Clock_Deinit(&g_wbClock);
}
```

Note: the device instance's `.driver` field is intentionally left unset. With
API mapping, the top-level `whal_*_Init`/etc. symbols are the driver functions
themselves — there is no dispatch through a vtable.

### 3. `board.mk` — Build Variables

Provide the build-time configuration: API mapping flags, flash offset, linker
script, and the wolfHAL driver objects needed for your MCU family. **Do not
compile the dispatch source (`src/<type>/<type>.c`)** — it would provide a
duplicate definition of the top-level API symbols.

```makefile
ARCH_FLASH_OFFSET=0x08000000
LSCRIPT_IN=hal/<family>.ld

# Bind wolfHAL driver sources directly to the top-level API symbols.
CFLAGS+=-DWHAL_CFG_CLOCK_API_MAPPING_<FAMILY>
CFLAGS+=-DWHAL_CFG_FLASH_API_MAPPING_<FAMILY>
CFLAGS+=-DWHAL_CFG_GPIO_API_MAPPING_<FAMILY>
CFLAGS+=-DWHAL_CFG_UART_API_MAPPING_<FAMILY>

WOLFHAL_OBJS+=$(WOLFHAL_ROOT)/src/clock/<family>_rcc.o
WOLFHAL_OBJS+=$(WOLFHAL_ROOT)/src/flash/<family>_flash.o
ifeq ($(DEBUG_UART),1)
  WOLFHAL_OBJS+=$(WOLFHAL_ROOT)/src/gpio/<family>_gpio.o
  WOLFHAL_OBJS+=$(WOLFHAL_ROOT)/src/uart/<family>_uart.o
endif

OBJS+=$(WOLFHAL_OBJS)
APP_OBJS+=$(WOLFHAL_OBJS)

ifeq ($(RAM_CODE),1)
  WOLFHAL_FLASH_EXCLUDE_TEXT=*(EXCLUDE_FILE(*<family>_flash.o) .text*)
  WOLFHAL_FLASH_EXCLUDE_RODATA=*(EXCLUDE_FILE(*<family>_flash.o) .rodata*)
  WOLFHAL_FLASH_RAM_SECTIONS=*<family>_flash.o(.text* .rodata*)
endif
```

Only one API mapping flag may be active per device type per build.

### 4. Config File

Create `config/examples/wolfhal_<board_name>.config`:

```
TARGET=wolfhal
BOARD=<board_name>
SIGN=ECC256
HASH=SHA256
WOLFBOOT_SECTOR_SIZE=0x1000
WOLFBOOT_PARTITION_SIZE=0x20000
WOLFBOOT_PARTITION_BOOT_ADDRESS=0x08008000
WOLFBOOT_PARTITION_UPDATE_ADDRESS=0x08028000
WOLFBOOT_PARTITION_SWAP_ADDRESS=0x08048000
NVM_FLASH_WRITEONCE=1
```

Adjust partition addresses and sector sizes for your board's flash layout. Optionally
add `DEBUG_UART=1` to enable UART debug output.

## RAM_CODE

When `RAM_CODE=1` is set, wolfBoot's core flash update functions are placed in RAM
via the `RAMFUNCTION` attribute. For wolfHAL boards, the `board.mk` defines
`EXCLUDE_FILE` rules that also place the wolfHAL flash driver into RAM. This ensures
all flash operations execute from RAM, which is required on MCUs that stall or fault
when code executes from the same flash bank being programmed.

The linker script uses `@WOLFHAL_FLASH_EXCLUDE_TEXT@`,
`@WOLFHAL_FLASH_EXCLUDE_RODATA@`, and `@WOLFHAL_FLASH_RAM_SECTIONS@` placeholders
that are substituted at build time. When `RAM_CODE=1`, these expand to
`EXCLUDE_FILE` rules that move the flash driver's `.text` and `.rodata` sections from
flash into the `.data` section (loaded to RAM at startup). When `RAM_CODE` is not
set, all code remains in flash as normal.

## Test Application

The generic test application (`test-app/app_wolfhal.c`) demonstrates using wolfHAL
peripherals beyond what the bootloader needs. It accesses the board-provided GPIO and
UART instances (`g_wbGpio`, `g_wbUart`) via `extern`, using the top-level wolfHAL
API (`whal_Gpio_Set`, `whal_Uart_Send`) to toggle an LED and send serial output,
then exercises the wolfBoot update mechanism.

The test-app Makefile compiles its own copy of the board file (`board_<board>.o`)
with `DEBUG_UART=1` always defined, since the app needs UART and GPIO regardless of
the bootloader's `DEBUG_UART` setting.
