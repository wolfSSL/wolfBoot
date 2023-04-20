/* boot_renesas.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

#include <stdint.h>
#include <wolfssl/wolfcrypt/settings.h>
#include "wolfboot/wolfboot.h"
#include "hal.h"

/* This is the main loop for the bootloader.
 *
 * It performs the following actions:
 *  - globally disable interrupts
 *  - update the Interrupt Vector using the address of the app
 *  - Set the initial stack pointer and the offset of the app
 *  - Change the stack pointer
 *  - Call the application entry point
 *
 */

#define VECTOR_SP            ((uint32_t)(0x0))
#define VECTOR_Reset_Handler ((uint32_t *)(0x10204))

void do_boot(const uint32_t *app_offset)
{
     void (*app_entry)(void);
     uint32_t app_sp;
     (void) app_offset;

     app_sp = VECTOR_SP;

     __asm__ ("ldr r3, [%0]" ::"r"(app_sp));
     __asm__ ("mov sp, r3");

   /*
    * address of Reset Hander is stored in Vector table[] that is defined in startup.c.
    * The vector for Reset Handler is placed right after Initial Stack Pointer.
    * The application assumes to start from 0x10200.
    *
    */
     app_entry = (void(*)(void))(*VECTOR_Reset_Handler);
     (*app_entry)();
}

