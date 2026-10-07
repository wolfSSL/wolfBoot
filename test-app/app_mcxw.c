/* app_mcxw.c
 *
 * Test bare-metal application for the NXP MCX W71
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

#include "fsl_common.h"
#include "fsl_clock.h"
#include "fsl_port.h"
#include "fsl_gpio.h"

#include "target.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"
#include "hal/mcxw.h"

#ifdef WOLFCRYPT_SECURE_MODE
#include "wolfssl/wolfcrypt/types.h"
#include "wolfssl/wolfcrypt/random.h"
#endif

static void leds_init(void)
{
#ifndef TZEN
    /* With TrustZone, wolfBoot sets up the clock and mux before boot */
    CLOCK_EnableClock(kCLOCK_GpioA);
    CLOCK_EnableClock(kCLOCK_PortA);
    PORT_SetPinMux(PORTA, MCXW_LED_GREEN_PIN, kPORT_MuxAsGpio);
    PORT_SetPinMux(PORTA, MCXW_LED_BLUE_PIN, kPORT_MuxAsGpio);
    PORT_SetPinMux(PORTA, MCXW_LED_RED_PIN, kPORT_MuxAsGpio);
#endif
    GPIO_PortSet(GPIOA, MCXW_LED_PINS_MASK);
    GPIOA->PDDR |= MCXW_LED_PINS_MASK;
}

#ifdef WOLFCRYPT_SECURE_MODE
static void print_random_number(void)
{
    uint8_t rnd;
    int ret;

    ret = wcs_get_random(&rnd, sizeof(rnd));
    if (ret != 0)
        wolfBoot_printf("Random number: generate failed (%d)\n", ret);
    else
        wolfBoot_printf("Today's lucky number: 0x%02x\n", rnd);
}
#endif

void main(void)
{
    uint32_t boot_ver;

    leds_init();

#ifdef TZEN
    boot_ver = wolfBoot_nsc_current_firmware_version();
#else
    boot_ver = wolfBoot_current_firmware_version();
#endif

    wolfBoot_printf("Hello from MCXW71 firmware version %d\n", boot_ver);

#ifdef WOLFCRYPT_SECURE_MODE
    print_random_number();
#endif

    if (boot_ver == 1) {
        GPIO_PortClear(GPIOA, 1U << MCXW_LED_BLUE_PIN);
    }
    else {
#ifdef TZEN
        wolfBoot_nsc_success();
#else
        wolfBoot_success();
#endif
        wolfBoot_printf("Update successful, firmware version %d confirmed\n",
                boot_ver);
        GPIO_PortClear(GPIOA, 1U << MCXW_LED_GREEN_PIN);
    }

    while (1) {
        __WFI();
    }
}
