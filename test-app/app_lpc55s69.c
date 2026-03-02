/* app_lpc55s69.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include <stdint.h>
#include "fsl_clock.h"
#include "fsl_gpio.h"
#include "fsl_iocon.h"

#include "target.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"

#define RED_LED     6
#define GREEN_LED   7
#define BLUE_LED    4

extern void hal_init(void);

#define IOCON_PIO_FUNC0 0x00u         /*!<@brief Selects pin function 0 */
#define IOCON_PIO_MODE_PULLUP 0x20u   /*!<@brief Selects pull-up function */
#define IOCON_PIO_SLEW_STANDARD 0x00u /*!<@brief Standard mode, output slew rate control is enabled */
#define IOCON_PIO_INV_DI 0x00u        /*!<@brief Input function is not inverted */
#define IOCON_PIO_DIGITAL_EN 0x0100u  /*!<@brief Enables digital function */
#define IOCON_PIO_OPENDRAIN_DI 0x00u  /*!<@brief Open drain is disabled */

static void leds_init(void)
{
    CLOCK_EnableClock(kCLOCK_Iocon);
    CLOCK_EnableClock(kCLOCK_Gpio1);

    const gpio_pin_config_t LED_GPIOPIN_config = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = 1U   /* off */
    };
    GPIO_PinInit(GPIO, 1, 6, &LED_GPIOPIN_config);      /* red off */
    GPIO_PinInit(GPIO, 1, 7, &LED_GPIOPIN_config);      /* green off */
    GPIO_PinInit(GPIO, 1, 4, &LED_GPIOPIN_config);      /* blue off */

    const uint32_t LED_PINMUX_CONFIG = (/* Pin is configured as PIO */
                               IOCON_PIO_FUNC0 |
                               /* Selects pull-up function */
                               IOCON_PIO_MODE_PULLUP |
                               /* Standard mode, output slew rate control is enabled */
                               IOCON_PIO_SLEW_STANDARD |
                               /* Input function is not inverted */
                               IOCON_PIO_INV_DI |
                               /* Enables digital function */
                               IOCON_PIO_DIGITAL_EN |
                               /* Open drain is disabled */
                               IOCON_PIO_OPENDRAIN_DI);
    IOCON_PinMuxSet(IOCON, 1, 6, LED_PINMUX_CONFIG);    /* red */
    IOCON_PinMuxSet(IOCON, 1, 7, LED_PINMUX_CONFIG);    /* green */
    IOCON_PinMuxSet(IOCON, 1, 4, LED_PINMUX_CONFIG);    /* blue */
}

void main(void)
{
    uint32_t boot_ver;

    hal_init();
    leds_init();

#ifdef WOLFCRYPT_SECURE_MODE
    boot_ver = wolfBoot_nsc_current_firmware_version();
#else
    boot_ver = wolfBoot_current_firmware_version();
#endif

    wolfBoot_printf("Hello from firmware version %d\n", boot_ver);

    if (boot_ver == 1) {
        uint32_t update_ver;

        /* blue on */
        GPIO_PinWrite(GPIO, 1, BLUE_LED, 0);

#ifdef WOLFCRYPT_SECURE_MODE
        update_ver = wolfBoot_nsc_update_firmware_version();
#else
        update_ver = wolfBoot_update_firmware_version();
#endif

        if (update_ver != 0) {
            wolfBoot_printf("Update firmware detected, version: 0x%lx\n", update_ver);
            wolfBoot_printf("Triggering update...\n");
#ifdef WOLFCRYPT_SECURE_MODE
            wolfBoot_nsc_update_trigger();
#else
            wolfBoot_update_trigger();
#endif
            wolfBoot_printf("...done. Reboot to apply.\n");
        }
    }
    else {
        /* green on */
        GPIO_PinWrite(GPIO, 1, GREEN_LED, 0);

#ifdef WOLFCRYPT_SECURE_MODE
        wolfBoot_nsc_success();
#else
        wolfBoot_success();
#endif
    }

    while (1) {
        __asm__ volatile ("wfi");
    }
}


#include "sys/stat.h"
int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    return -1;
}

void _exit(int status)
{
    _kill(status, -1);
    while (1) {}
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return -1;
}

int _write(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    return len;
}

int _close(int file)
{
    (void)file;
    return -1;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}
