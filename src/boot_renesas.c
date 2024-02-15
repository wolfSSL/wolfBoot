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

#if !defined(_RENESAS_RA_) && !defined(_RENESAS_RZN_)
#pragma inline_asm longJump
static void longJump(const uint32_t *app_offset)
{
    jmp   r1;
}
#endif

void do_boot(const uint32_t *app_offset)
{
     void (*app_entry)(void);
     uint32_t app_sp;
     (void) app_offset;
     (void) app_sp;
     (void) app_entry;
#if defined(__CCRX__)
     longJump(app_offset);
#elif defined(_RENESAS_RA_)
     app_sp = VECTOR_SP;

     __asm__ ("ldr r3, [%0]" ::"r"(app_sp));
     __asm__ ("mov sp, r3");

   /*
    * address of Reset Handler is stored in Vector table[] that is defined in startup.c.
    * The vector for Reset Handler is placed right after Initial Stack Pointer.
    * The application assumes to start from 0x10200.
    *
    */
     app_entry = (void(*)(void))(*VECTOR_Reset_Handler);
     (*app_entry)();
#elif defined(_RENESAS_RZN_)
     app_entry = (void(*))(0x10010000);
     /* Jump to the application project */
     app_entry();
#endif
}

