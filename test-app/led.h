/* led.h
 *
 * Test bare-metal blinking led application
 *
 * Copyright (C) 2020 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#ifndef GPIO_H_INCLUDED
#define GPIO_H_INCLUDED

int led_setup(void);
void led_on(void);
void led_off(void);
void led_toggle(void);
void led_pwm_setup(void);
void boot_led_on(void);
void boot_led_off(void);

#endif /* !GPIO_H_INCLUDED */
