/* app_max32666.c
 *
 * Test application for wolfBoot on MAX32666FTHR
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"
#include "hal/max32666.h"

#define LED_RED_PIN     (1UL << 29)
#define LED_BLUE_PIN    (1UL << 30)
#define LED_GREEN_PIN   (1UL << 31)

void main(void)
{
    uint32_t version;

    hal_init();


    version = wolfBoot_current_firmware_version();

    if (version == 1) {
        /* Turn on blue LED */
        GPIO0_EN0_SET = LED_BLUE_PIN;    /* configure as GPIO */
        GPIO0_OUT_EN |= LED_BLUE_PIN;    /* enable output */
        GPIO0_OUT_CLR = LED_BLUE_PIN;    /* drive low (LED on) */
    } else {
        /* Turn on green LED */
        GPIO0_EN0_SET = LED_GREEN_PIN;   /* configure as GPIO */
        GPIO0_OUT_EN |= LED_GREEN_PIN;   /* enable output */
        GPIO0_OUT_CLR = LED_GREEN_PIN;   /* drive low (LED on) */
    }

    wolfBoot_printf("MAX32666 Test App v%lu\n", (unsigned long)version);

    /* Mark boot successful to prevent rollback */
    wolfBoot_success();

    wolfBoot_printf("Boot success marked. Version: %lu\n",
        (unsigned long)version);

    /* Main loop */
    while (1) {
        __asm__ volatile ("nop");
    }
}
