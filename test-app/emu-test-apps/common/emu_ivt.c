/* emu_ivt.c
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

extern void Reset_Handler(void);
extern unsigned long _estack;

static void default_handler(void)
{
    while (1) {
    }
}

void NMI_Handler(void)            __attribute__((weak, alias("default_handler")));
void HardFault_Handler(void)      __attribute__((weak, alias("default_handler")));
void MemManage_Handler(void)      __attribute__((weak, alias("default_handler")));
void BusFault_Handler(void)       __attribute__((weak, alias("default_handler")));
void UsageFault_Handler(void)     __attribute__((weak, alias("default_handler")));
void SVC_Handler(void)            __attribute__((weak, alias("default_handler")));
void DebugMon_Handler(void)       __attribute__((weak, alias("default_handler")));
void PendSV_Handler(void)         __attribute__((weak, alias("default_handler")));
void SysTick_Handler(void)        __attribute__((weak, alias("default_handler")));

__attribute__((section(".isr_vector")))
const uint32_t vector_table[16 + 48] = {
    [0] = (uint32_t)&_estack,
    [1] = (uint32_t)&Reset_Handler,
    [2] = (uint32_t)&NMI_Handler,
    [3] = (uint32_t)&HardFault_Handler,
    [4] = (uint32_t)&MemManage_Handler,
    [5] = (uint32_t)&BusFault_Handler,
    [6] = (uint32_t)&UsageFault_Handler,
    [7] = 0, [8] = 0, [9] = 0, [10] = 0,
    [11] = (uint32_t)&SVC_Handler,
    [12] = (uint32_t)&DebugMon_Handler,
    [13] = 0,
    [14] = (uint32_t)&PendSV_Handler,
    [15] = (uint32_t)&SysTick_Handler,
    [16 ... 63] = (uint32_t)&default_handler
};
