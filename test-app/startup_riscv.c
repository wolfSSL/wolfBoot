/* startup_riscv.c
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
#include "hal/riscv.h"
#include "printf.h"

extern void trap_entry(void);
extern void trap_exit(void);

extern uint32_t  _start_vector;
extern uint32_t  _stored_data;
extern uint32_t  _start_data;
extern uint32_t  _end_data;
extern uint32_t  _start_bss;
extern uint32_t  _end_bss;
extern uint32_t  _end_stack;
extern uint32_t  _start_heap;
extern uint32_t  _global_pointer;
extern void (* const IV[])(void);

extern void main(void);
void __attribute__((naked,section(".init"))) _reset(void) {
    register uint32_t *src, *dst;
    asm volatile("la gp, _global_pointer");
    asm volatile("la sp, _end_stack");

    /* Set up vectored interrupt table. The +1 sets MODE=1 (vectored). */
#ifdef WOLFBOOT_RISCV_MMODE
    asm volatile("csrw mtvec, %0":: "r"((uint8_t *)(&_start_vector) + 1));
#else
    asm volatile("csrw stvec, %0":: "r"((uint8_t *)(&_start_vector) + 1));
#endif

    src = (uint32_t *) &_stored_data;
    dst = (uint32_t *) &_start_data;
    /* Copy the .data section from flash to RAM. */
    while (dst < (uint32_t *)&_end_data) {
        *dst = *src;
        dst++;
        src++;
    }

    /* Initialize the BSS section to 0 */
    dst = &_start_bss;
    while (dst < (uint32_t *)&_end_bss) {
        *dst = 0U;
        dst++;
    }

    /* Run wolfboot */
    main();
    while(1)
        ;
}

void do_boot(const uint32_t *app_offset)
{

}

static uint32_t synctrap_cause = 0;
#ifdef WOLFBOOT_RISCV_MMODE
/* The vector saved the caller's registers, so this can be plain C. Report the
 * fault and stop rather than resume at the faulting instruction, which would
 * trap again for ever with nothing on the console. Hardware clears mstatus.MIE
 * on trap entry, so the timer interrupt that pets the MSS watchdogs cannot run
 * here and they reset the board a few seconds after the message. */
void isr_synctrap(void)
{
    unsigned long cause, epc, tval;
    asm volatile("csrr %0, mcause" : "=r"(cause));
    asm volatile("csrr %0, mepc" : "=r"(epc));
    asm volatile("csrr %0, mtval" : "=r"(tval));
    synctrap_cause = (uint32_t)cause;
    wolfBoot_printf("TRAP: cause=%lx epc=%lx tval=%lx\r\n", cause, epc, tval);
    while (1) {
        /* halt */
    }
}
#else
void __attribute__((naked)) isr_synctrap(void)
{
    asm volatile("csrr %0, scause" : "=r"(synctrap_cause));
    asm volatile("sret");
}
#endif

/* Machine timer interrupt; a target that uses it defines its own. */
void __attribute__((weak)) isr_mtimer(void)
{
}

void isr_empty(void)
{

}
