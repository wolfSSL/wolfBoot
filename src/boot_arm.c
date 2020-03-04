/* boot_arm.c
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

#include <stdint.h>

#include "image.h"
#include "loader.h"
#include "wolfboot/wolfboot.h"

extern unsigned int _stored_data;
extern unsigned int _start_data;
extern unsigned int _end_data;
extern unsigned int _start_bss;
extern unsigned int _end_bss;

extern uint32_t *END_STACK;

extern void main(void);

void isr_reset(void) {
    register unsigned int *src, *dst;
#if defined(PLATFORM_kinetis)
    /* Immediately disable Watchdog after boot */
    /*  Write Keys to unlock register */
    *((volatile unsigned short *)0x4005200E) = 0xC520;
    *((volatile unsigned short *)0x4005200E) = 0xD928;
    /* disable watchdog via STCTRLH register */
    *((volatile unsigned short *)0x40052000) = 0x01D2u;
#endif
    /* Copy the .data section from flash to RAM. */
    src = (unsigned int *) &_stored_data;
    dst = (unsigned int *) &_start_data;
    while (dst < (unsigned int *)&_end_data) {
        *dst = *src;
        dst++;
        src++;
    }

    /* Initialize the BSS section to 0 */
    dst = &_start_bss;
    while (dst < (unsigned int *)&_end_bss) {
        *dst = 0U;
        dst++;
    }

    /* Run the program! */
    main();
}
void isr_fault(void)
{
    /* Panic. */
    while(1) ;;
}

void isr_empty(void)
{
    /* Ignore unmapped event and continue */
}

#define VTOR (*(volatile uint32_t *)(0xE000ED08))

/* This is the main loop for the bootloader.
 *
 * It performs the following actions:
 *  - globally disable interrutps
 *  - update the Interrupt Vector using the address of the app
 *  - Set the initial stack pointer and the offset of the app
 *  - Change the stack pointer
 *  - Call the application entry point
 *
 */
static void  *app_entry;
static uint32_t app_end_stack;


void RAMFUNCTION do_boot(const uint32_t *app_offset)
{

#ifndef NO_VTOR
    /* Disable interrupts */
    asm volatile("cpsid i");
    /* Update IV */
    VTOR = ((uint32_t)app_offset);
#endif

    /* Get stack pointer, entry point */
    app_end_stack = (*((uint32_t *)(app_offset)));
    app_entry = (void *)(*((uint32_t *)(app_offset + 1)));

    /* Update stack pointer */
    asm volatile("msr msp, %0" ::"r"(app_end_stack));
#ifndef NO_VTOR
    asm volatile("cpsie i");
#endif
    /* Unconditionally jump to app_entry */
    asm volatile("mov pc, %0" ::"r"(app_entry));
}

__attribute__ ((section(".isr_vector")))
void (* const IV[])(void) =
{
	(void (*)(void))(&END_STACK),
	isr_reset,                   // Reset
	isr_fault,                   // NMI
	isr_fault,                   // HardFault
	isr_fault,                   // MemFault
	isr_fault,                   // BusFault
	isr_fault,                   // UsageFault
	0, 0, 0, 0,                  // 4x reserved
	isr_empty,                   // SVC
	isr_empty,                   // DebugMonitor
	0,                           // reserved
	isr_empty,                   // PendSV
	isr_empty,                   // SysTick

    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
};

#ifdef RAM_CODE

#define AIRCR *(volatile uint32_t *)(0xE000ED0C)
#define AIRCR_VKEY (0x05FA << 16)
#   define AIRCR_SYSRESETREQ (1 << 2)

void RAMFUNCTION arch_reboot(void)
{
    AIRCR = AIRCR_SYSRESETREQ | AIRCR_VKEY;
    while(1)
        ;

}
#endif
