/* boot_riscv64.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

/* RISC-V 64-bit boot code */

#include <stdint.h>

#include "image.h"
#include "loader.h"

#ifdef TARGET_mpfs250
#include "hal/mpfs250.h"
#endif

extern void trap_entry(void);
extern void trap_exit(void);

extern uint64_t  _start_vector;
extern uint64_t  _stored_data;
extern uint64_t  _start_data;
extern uint64_t  _end_data;
extern uint64_t  _start_bss;
extern uint64_t  _end_bss;
extern uint64_t  _end_stack;
extern uint64_t  _start_heap;
extern uint64_t  _global_pointer;
extern void (* const trap_vector_table[])(void);

/* reloc_trap_vector is implemented in boot_riscv64_start.S */
extern void reloc_trap_vector(const uint32_t *address);

static uint64_t last_cause = 0;
static uint64_t last_epc = 0;
static uint64_t last_tval = 0;

unsigned long WEAKFUNCTION handle_trap(unsigned long cause, unsigned long epc, unsigned long tval)
{
    last_cause = cause;
    last_epc = epc;
    last_tval = tval;
    return epc;
}

#ifdef MMU
int WEAKFUNCTION hal_dts_fixup(void* dts_addr)
{
    (void)dts_addr;
    return 0;
}
#endif

#ifdef MMU
void do_boot(const uint32_t *app_offset, const uint32_t* dts_offset)
#else
void do_boot(const uint32_t *app_offset)
#endif
{
#ifdef MMU
    hal_dts_fixup((uint32_t*)dts_offset);
#endif

    /* Relocate trap vector table to application */
    reloc_trap_vector(app_offset);

    /* Jump to application entry point */
    asm volatile("jr %0":: "r"((uint8_t *)(app_offset)));
}

void isr_empty(void)
{
    /* Empty interrupt handler */
}

void WEAKFUNCTION arch_reboot(void)
{
#ifdef TARGET_mpfs250
    SYSREG_MSS_RESET_CR = 0xDEAD;
#endif

    while(1)
        ;
    wolfBoot_panic();
}
