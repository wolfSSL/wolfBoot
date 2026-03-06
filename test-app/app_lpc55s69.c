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
#ifndef TZEN
    CLOCK_EnableClock(kCLOCK_Iocon);
    CLOCK_EnableClock(kCLOCK_Gpio1);
#endif

    /* red output off */
    GPIO->SET[1] = 1UL << RED_LED;
    GPIO->DIR[1] |= 1UL << RED_LED;
    /* green output off */
    GPIO->SET[1] = 1UL << GREEN_LED;
    GPIO->DIR[1] |= 1UL << GREEN_LED;
    /* blue output off */
    GPIO->SET[1] = 1UL << BLUE_LED;
    GPIO->DIR[1] |= 1UL << BLUE_LED;

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
    IOCON_PinMuxSet(IOCON, 1, RED_LED, LED_PINMUX_CONFIG);
    IOCON_PinMuxSet(IOCON, 1, GREEN_LED, LED_PINMUX_CONFIG);
    IOCON_PinMuxSet(IOCON, 1, BLUE_LED, LED_PINMUX_CONFIG);
}

static void check_parts(
    uint32_t *pboot_ver, uint32_t *pupdate_ver,
    uint8_t *pboot_state, uint8_t *pupdate_state
)
{
#ifdef WOLFCRYPT_SECURE_MODE
    *pboot_ver = wolfBoot_nsc_current_firmware_version();
    *pupdate_ver = wolfBoot_nsc_update_firmware_version();
    if (wolfBoot_nsc_get_partition_state(PART_BOOT, pboot_state) != 0)
        *pboot_state = IMG_STATE_NEW;
    if (wolfBoot_nsc_get_partition_state(PART_UPDATE, pupdate_state) != 0)
        *pupdate_state = IMG_STATE_NEW;
#else
    *pboot_ver = wolfBoot_current_firmware_version();
    *pupdate_ver = wolfBoot_update_firmware_version();
    if (wolfBoot_get_partition_state(PART_BOOT, pboot_state) != 0)
        *pboot_state = IMG_STATE_NEW;
    if (wolfBoot_get_partition_state(PART_UPDATE, pupdate_state) != 0)
        *pupdate_state = IMG_STATE_NEW;
#endif

    wolfBoot_printf("    boot:   ver=0x%lx state=0x%02x\n", *pboot_ver, *pboot_state);
    wolfBoot_printf("    update: ver=0x%lx state=0x%02x\n", *pupdate_ver, *pupdate_state);
}

void main(void)
{
    uint32_t boot_ver, update_ver;
    uint8_t boot_state, update_state;

    hal_init();
    leds_init();

    check_parts(&boot_ver, &update_ver, &boot_state, &update_state);

    if (
        boot_ver != 0 &&
        (boot_state == IMG_STATE_TESTING || boot_state == IMG_STATE_NEW)
    )
    {
#ifdef WOLFCRYPT_SECURE_MODE
        wolfBoot_printf("Calling wolfBoot_nsc_success()\n");
        wolfBoot_nsc_success();
#else
        wolfBoot_printf("Calling wolfBoot_success()\n");
        wolfBoot_success();
#endif
        check_parts(&boot_ver, &update_ver, &boot_state, &update_state);
    }

    if (boot_ver == 1)
    {
        /* blue on */
        GPIO_PinWrite(GPIO, 1, BLUE_LED, 0);

        if (update_ver != 0) {
            wolfBoot_printf("Update detected, version: 0x%lx\n", update_ver);
            wolfBoot_printf("Triggering update...\n");
#ifdef WOLFCRYPT_SECURE_MODE
            wolfBoot_nsc_update_trigger();
#else
            wolfBoot_update_trigger();
#endif
            check_parts(&boot_ver, &update_ver, &boot_state, &update_state);
            wolfBoot_printf("...done. Reboot to apply.\n");
        }
    }
    else {
        /* green on */
        GPIO_PinWrite(GPIO, 1, GREEN_LED, 0);
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
