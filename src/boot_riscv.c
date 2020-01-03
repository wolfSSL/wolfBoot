/* boot_riscv.c
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
void RAMFUNCTION reloc_iv(const uint32_t *address)
{
    asm volatile("csrw mtvec, %0":: "r"(address + 1));
}

void __attribute__((naked,section(".init"))) _reset(void) {
    register uint32_t *src, *dst;
    asm volatile("la gp, _global_pointer");
    asm volatile("la sp, _end_stack");

    /* Set up vectored interrupt, with IV starting at offset 0x100 */
    asm volatile("csrw mtvec, %0":: "r"((uint8_t *)(&_start_vector) + 1));

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
#if 1
    /* workaround for long jump */
    asm volatile("la    a2, reloc_iv;" \
                 "jalr  a2" ::: "a2");
#else
    reloc_iv(app_offset);
#endif
    asm volatile("jr %0":: "r"((uint8_t *)(app_offset)));
}

void isr_empty(void)
{

}

#ifdef RAM_CODE

#define AON_WDOGCFG  *(volatile uint32_t *)(0x10000000UL)
#define AON_WDOGKEY  *(volatile uint32_t *)(0x1000001CUL)
#define AON_WDOGFEED *(volatile uint32_t *)(0x10000018UL)
#define AON_WDOGCMP  *(volatile uint32_t *)(0x10000020UL)

#define AON_WDOGKEY_VALUE       0x0051F15E
#define AON_WDOGCFG_SCALE       0x0000000F
#define AON_WDOGCFG_RSTEN       0x00000100
#define AON_WDOGCFG_ZEROCMP     0x00000200
#define AON_WDOGCFG_ENALWAYS    0x00001000


void RAMFUNCTION arch_reboot(void)
{
    AON_WDOGKEY = AON_WDOGKEY_VALUE;
    AON_WDOGCMP = 0;
    //wdogconfig: : wdogrsten | enablealways | reset to 0 | max scale
    AON_WDOGKEY = AON_WDOGKEY_VALUE;
    AON_WDOGCFG |= (AON_WDOGCFG_RSTEN | AON_WDOGCFG_ENALWAYS | AON_WDOGCFG_ZEROCMP | AON_WDOGCFG_SCALE) ;
    AON_WDOGKEY = AON_WDOGKEY_VALUE;
    AON_WDOGFEED = 1;
    while(1)
        ;
}

#endif /* RAM_CODE */
