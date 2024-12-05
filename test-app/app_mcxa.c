/* app_mcxa.c
 *
 * Test bare-metal boot-led-on application
 *
 * Copyright (C) 2024 wolfSSL Inc.
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

#include "wolfboot/wolfboot.h"
#include "target.h"

extern void hal_init(void);

/* init gpio for port 3 */
void gpio_port3_init(int pin)
{
    const port_pin_config_t GPIO_OUT_LED = {
        kPORT_PullDisable,          /* Internal pull-up/down resistor is disabled */
        kPORT_LowPullResistor,      /* Low internal pull resistor value is selected. */
        kPORT_FastSlewRate,         /* Fast slew rate is configured */
        kPORT_PassiveFilterDisable, /* Passive input filter is disabled */
        kPORT_OpenDrainDisable,     /* Open drain output is disabled */
        kPORT_LowDriveStrength,     /* Low drive strength is configured */
        kPORT_NormalDriveStrength,  /* Normal drive strength is configured */
        kPORT_MuxAlt0,              /* Configure as GPIO */
        kPORT_InputBufferEnable,    /* Digital input enabled */
        kPORT_InputNormal,          /* Digital input is not inverted */
        kPORT_UnlockRegister        /* Pin Control Register fields [15:0] are not locked */
    };
    const gpio_pin_config_t GPIO_OUT_LED_config = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = 0U
    };

    /* Enable GPIO port 3 clocks */
    CLOCK_EnableClock(kCLOCK_GateGPIO3);                 /* Write to GPIO3: Peripheral clock is enabled */
    CLOCK_EnableClock(kCLOCK_GatePORT3);                 /* Write to PORT3: Peripheral clock is enabled */
    RESET_ReleasePeripheralReset(kGPIO3_RST_SHIFT_RSTn); /* GPIO3 peripheral is released from reset */
    RESET_ReleasePeripheralReset(kPORT3_RST_SHIFT_RSTn); /* PORT3 peripheral is released from reset */

    /* Initialize GPIO functionality on pin */
    GPIO_PinInit(GPIO3, pin, &GPIO_OUT_LED_config);
    PORT_SetPinConfig(PORT3, pin, &GPIO_OUT_LED);
}

void main(void)
{
    int i = 0;
    uint8_t* bootPart = (uint8_t*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uint32_t bootVer = wolfBoot_get_blob_version(bootPart);

    hal_init();

    /* If application version 1 then GREEN, else BLUE */
    /* RGB LED D15 (RED=P3_12, GREEN=P3_13, BLUE=P3_0) */
    if (bootVer == 1) {
        gpio_port3_init(13);
        GPIO_PinWrite(GPIO3, 13, 0);
    }
    else {
        gpio_port3_init(0);
        GPIO_PinWrite(GPIO3, 0, 0);
    }

    /* mark boot successful */
    wolfBoot_success();

    /* busy wait */
    while (1) {
        __WFI();
    }
}
