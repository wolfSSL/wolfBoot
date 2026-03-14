# wolfHAL Integration

wolfBoot supports [wolfHAL](https://github.com/wolfSSL/wolfHAL) as an alternative hardware abstraction layer backend. wolfHAL provides portable, vtable-dispatched drivers for common MCU peripherals (clock, flash, GPIO, UART, SPI, etc.) with a consistent API across platforms.

## Overview

The wolfHAL integration consists of three parts:

1. **HAL implementation** (`hal/wolfhal_<family>.c`) — implements the wolfBoot HAL API (`hal_init`, `hal_flash_write`, etc.) by calling wolfHAL driver functions. One file per MCU family (e.g. STM32WB).

2. **Board configuration** (`hal/boards/<board>.c`) — defines the wolfHAL device instances (flash, clock, and optionally GPIO/UART for `DEBUG_UART`) with board-specific register addresses, clock parameters, and flash geometry. One file per board.

3. **wolfHAL library** (`lib/wolfHAL/`) — the wolfHAL submodule containing the platform drivers.

## Configuration

A wolfHAL-based config requires two variables beyond the standard wolfBoot settings:

```
TARGET=wolfhal_stm32wb
BOARD=stm32wb_nucleo
```

- `TARGET` selects the MCU family HAL implementation (`hal/wolfhal_stm32wb.c`)
- `BOARD` selects the board configuration file (`hal/boards/stm32wb_nucleo.c`)

## Adding a New Board

To add a new board for an existing MCU family (e.g. a custom STM32WB board):

1. Create `hal/boards/<board_name>.c` with the wolfHAL device configuration:

```c
#include <wolfHAL/clock/stm32wb_rcc.h>
#include <wolfHAL/flash/stm32wb_flash.h>
#ifdef DEBUG_UART
#include <wolfHAL/gpio/stm32wb_gpio.h>
#include <wolfHAL/uart/stm32wb_uart.h>
#endif
#include <wolfHAL/platform/st/stm32wb55xx.h>

/* Forward declarations for circular references */
whal_Flash g_wbFlash;
whal_Clock g_wbClock;

/* PLL, clock, and flash configuration for your board */
static whal_Stm32wbRcc_PllClkCfg pllCfg = { ... };
static whal_Stm32wbRcc_Cfg rccCfg = { .flash = &g_wbFlash, ... };
static whal_Stm32wbFlash_Cfg flashCfg = { .clkCtrl = &g_wbClock, ... };

/* Minimal clock driver vtable: Enable/Disable are used by the flash, GPIO, and
 * UART drivers for peripheral clock gating. GetRate is only needed by the UART
 * driver for baud rate calculation. */
static const whal_ClockDriver clockDriver = {
    .Enable = whal_Stm32wbRcc_Enable,
    .Disable = whal_Stm32wbRcc_Disable,
#ifdef DEBUG_UART
    .GetRate = whal_Stm32wbRccPll_GetRate,
#endif
};

/* Device definitions */
whal_Clock g_wbClock = {
    .regmap = { .base = 0x58000000, .size = 0x400 },
    .driver = &clockDriver,
    .cfg = &rccCfg,
};

whal_Flash g_wbFlash = {
    .regmap = { .base = 0x58004000, .size = 0x400 },
    .cfg = &flashCfg,
};

#ifdef DEBUG_UART
/* GPIO — UART1 TX/RX pins */
static whal_Stm32wbGpio_PinCfg gpioPins[] = { ... };
static whal_Stm32wbGpio_Cfg gpioCfg = { .clkCtrl = &g_wbClock, ... };

whal_Gpio g_wbGpio = {
    .regmap = { .base = 0x48000000, .size = 0x400 },
    .cfg = &gpioCfg,
};

/* UART1 at 115200 baud */
static whal_Stm32wbUart_Cfg uartCfg = { .clkCtrl = &g_wbClock, .baud = 115200 };

whal_Uart g_wbUart = {
    .regmap = { .base = 0x40013800, .size = 0x400 },
    .cfg = &uartCfg,
};
#endif /* DEBUG_UART */
```

The board file exports `g_wbFlash` and `g_wbClock` as non-static globals (and `g_wbGpio`/`g_wbUart` when `DEBUG_UART` is enabled). The HAL implementation references these via `extern`.

2. Create `config/examples/wolfhal_<target>_<board>.config`:

```
TARGET=wolfhal_stm32wb
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

Adjust partition addresses and sizes for your board's flash layout.

Optionally add `DEBUG_UART=1` to enable UART debug output via wolfHAL.

### RAM_CODE

When `RAM_CODE=1` is set, wolfBoot's core flash update functions are placed in RAM
via the `RAMFUNCTION` attribute. For wolfHAL targets, the wolfHAL flash driver
(`stm32wb_flash.o`) is also placed in RAM using `EXCLUDE_FILE` directives in the
linker script. This ensures all flash operations execute from RAM, which is required
on some MCUs that stall or fault when code executes from the same flash bank being
programmed.

```
RAM_CODE=1
```

The linker script (`hal/stm32wb.ld`) uses `@WOLFHAL_FLASH_EXCLUDE_TEXT@`,
`@WOLFHAL_FLASH_EXCLUDE_RODATA@`, and `@WOLFHAL_FLASH_RAM_SECTIONS@` placeholders
that are substituted at build time. When `RAM_CODE=1`, these expand to
`EXCLUDE_FILE(*stm32wb_flash.o)` rules that move the flash driver's `.text` and
`.rodata` sections from flash into the `.data` section (loaded to RAM at startup).
When `RAM_CODE` is not set, all code remains in flash as normal.

## Adding a New MCU Family

To add support for a new MCU family (e.g. STM32H7):

1. Create `hal/wolfhal_<family>.c` implementing the wolfBoot HAL functions. Use the appropriate wolfHAL driver headers and call driver functions directly for minimal overhead:

```c
#include <stdint.h>
#include "hal.h"
#include <wolfHAL/clock/<family>_rcc.h>
#include <wolfHAL/flash/<family>_flash.h>
#ifdef DEBUG_UART
#include <wolfHAL/gpio/<family>_gpio.h>
#include <wolfHAL/uart/<family>_uart.h>
#endif

extern whal_Flash g_wbFlash;
extern whal_Clock g_wbClock;
#ifdef DEBUG_UART
extern whal_Gpio g_wbGpio;
extern whal_Uart g_wbUart;
#endif

void hal_init(void)
{
    /* Family-specific clock and flash initialization */
#ifdef DEBUG_UART
    /* GPIO and UART initialization */
#endif
}

void hal_prepare_boot(void)
{
#ifdef DEBUG_UART
    /* UART and GPIO deinitialization */
#endif
    /* Family-specific flash and clock deinitialization */
}

/* hal_flash_unlock, hal_flash_lock, hal_flash_write, hal_flash_erase */

#ifdef DEBUG_UART
void uart_write(const char *buf, unsigned int len)
{
    /* Send via wolfHAL UART driver */
}
#endif
```

2. Add the target block in `arch.mk`:

```makefile
ifeq ($(TARGET),wolfhal_<family>)
    ARCH_FLASH_OFFSET=0x08000000
    LSCRIPT_IN=hal/<family>.ld
    WOLFHAL_ROOT?=$(WOLFBOOT_ROOT)/lib/wolfHAL
    CFLAGS+=-I$(WOLFHAL_ROOT) -DWHAL_CFG_DIRECT_CALLBACKS
    OBJS+=./hal/boards/$(BOARD).o
    OBJS+=$(WOLFHAL_ROOT)/src/<device>/<driver>.o
endif
```

3. Create at least one board configuration file in `hal/boards/`.

## Test Application

The test application (`test-app/app_wolfhal_stm32wb.c`) demonstrates using wolfHAL peripherals beyond what the bootloader needs. It initializes GPIO (LED on PB5) and UART (UART1 at 115200 baud) via wolfHAL, then exercises the wolfBoot update mechanism.

The test app accesses the board's clock instance via `extern whal_Clock g_wbClock` for peripheral clock gating.

The test-app compiles its own copy of the board file (`board_<board>.o`) with
`-DDEBUG_UART` always defined, since the app needs `GetRate` in the clock vtable
for UART baud rate calculation. This is independent of the bootloader's
`DEBUG_UART` setting — the bootloader's board object only includes `GetRate` when
`DEBUG_UART=1` is in the config.
