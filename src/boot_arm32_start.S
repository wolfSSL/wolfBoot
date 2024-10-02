/**
 * Arm32 (32bit Cortex-A) boot up 
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
.section start
	.text

/* startup entry point */
	.globl reset_vector_entry
	.align 4
reset_vector_entry:
/* Exception vectors (should be a branch to be detected as a valid code by the rom */
_exception_vectors:
	b 	isr_reset    /* reset */
	b 	isr_empty 	/* Undefined Instruction */
	b 	isr_swi   	/* Software Interrupt */
	b 	isr_pabt  	/* Prefetch Abort */
	b 	dabt_vector  	/* Data Abort */
.word		_romsize	/* Size of the binary for ROMCode loading */
	b 	isr_irq	/* IRQ : read the AIC */
	b 	isr_fiq      /* FIQ */

isr_empty:
	b 	isr_empty
isr_swi:
	b 	isr_swi
isr_pabt:
	b 	isr_pabt
dabt_vector:
	subs	pc, r14, #4	/* return */
	nop
isr_rsvd:
	b 	isr_rsvd
isr_irq:
	b 	isr_irq
isr_fiq:
	b 	isr_fiq


/* Reset handler procedure. Prepare the memory and call main() */
isr_reset:
    /* Initialize the stack pointer */
    ldr	sp,=_stack_top
    /* Save BootROM supplied boot source information to stack */
    push	{r4}

    /* Copy the data section */
    ldr      r2, =_lp_data
    ldmia    r2, {r1, r3, r4}
    1:
    cmp      r3, r4
    ldrcc    r2, [r1], #4
    strcc    r2, [r3], #4
    bcc      1b

    /* Zero bss area */
    adr    r2, _lp_bss
    ldmia  r2, {r3, r4}
    mov    r2, #0
    1:
    cmp    r3, r4
    strcc  r2, [r3], #4
    bcc    1b

    /* Jump to main() */
    ldr     r4, = main
    mov     lr, pc
    bx      r4
    
    /* main() should never return */
_panic:
    b       _panic

.align
_lp_data:
.word      _start_data
.word      _end_data

_lp_bss:
.word      _start_bss
.word      _end_bss

