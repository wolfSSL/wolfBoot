/* app_kinetis_kl26.c
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "fsl_common.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "fsl_clock.h"
#include "target.h"
#include "wolfboot/wolfboot.h"

/* FRDM-KL26Z onboard RGB LED (all active-low common-anode):
 *   blue  = D5
 *   green = E31
 *   red   = E29
 */
#define LED_BLUE_GPIO   GPIOD
#define LED_BLUE_PORT   PORTD
#define LED_BLUE_CLOCK  kCLOCK_PortD
#define LED_BLUE_PIN    5U

#define LED_GREEN_GPIO  GPIOE
#define LED_GREEN_PORT  PORTE
#define LED_GREEN_CLOCK kCLOCK_PortE
#define LED_GREEN_PIN   31U

void main(void)
{
    gpio_pin_config_t led_config = { kGPIO_DigitalOutput, 1 };
    uint32_t version = wolfBoot_current_firmware_version();

    CLOCK_EnableClock(LED_BLUE_CLOCK);
    CLOCK_EnableClock(LED_GREEN_CLOCK);
    PORT_SetPinMux(LED_BLUE_PORT,  LED_BLUE_PIN,  kPORT_MuxAsGpio);
    PORT_SetPinMux(LED_GREEN_PORT, LED_GREEN_PIN, kPORT_MuxAsGpio);
    GPIO_PinInit(LED_BLUE_GPIO,  LED_BLUE_PIN,  &led_config);
    GPIO_PinInit(LED_GREEN_GPIO, LED_GREEN_PIN, &led_config);
    /* Pins are active-low */
    GPIO_SetPinsOutput(LED_BLUE_GPIO,  1U << LED_BLUE_PIN);
    GPIO_SetPinsOutput(LED_GREEN_GPIO, 1U << LED_GREEN_PIN);

    if (version == 1) {
        GPIO_ClearPinsOutput(LED_BLUE_GPIO,  1U << LED_BLUE_PIN);
    }
    else {
        GPIO_ClearPinsOutput(LED_GREEN_GPIO, 1U << LED_GREEN_PIN);
        wolfBoot_success();
    }

    while (1)
        __WFI();
}
