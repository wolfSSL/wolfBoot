/**
 * Aarch64 bootup
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


#define GICD_BASE       0xF9010000
#define GICD_CTLR       0x0000
#define GICD_TYPER      0x0004
#define GICD_SGIR       0x0F00
#define GICD_IGROUPRn   0x0080

#define GICC_BASE       0xF9020000
#define GICC_PMR        0x0004

#ifndef USE_BUILTIN_STARTUP
.section ".boot"
.global _vector_table
_vector_table:
   mov x21, x0        // read ATAG/FDT address

4: ldr x1, =_vector_table // get start of .text in x1
    // Read current EL
    mrs     x0, CurrentEL
    and     x0, x0, #0x0C

    // EL == 3?
    cmp     x0, #12
    bne     2f
3:  mrs     x2, scr_el3
    orr     x2, x2, 0x0F    // scr_el3 |= NS|IRQ|FIQ|EA
    msr     scr_el3, x2

    msr cptr_el3, xzr       // enable FP/SIMD

    // EL == 1?
2:  cmp     x0, #4
    beq     1f

    // EL == 2?
    mov x2, #3 << 20
    msr cptr_el2, x2           /* Enable FP/SIMD */
    b 0f

1:  mov x0, #3 << 20
    msr cpacr_el1, x0           // Enable FP/SIMD for EL1
    msr     sp_el1, x1

   /* Suspend slave CPUs */
0: mrs x3, mpidr_el1  // read MPIDR_EL1
   and x3, x3, #3     // CPUID = MPIDR_EL1 & 0x03
   cbz x3, 8f         // if 0, branch forward
7: wfi                // infinite sleep
   b    7b

8:  mov sp, x1         // set stack pointer
    bl boot_entry_C   // boot_entry_C never returns
    b   7b            // go to sleep anyhow in case.
#endif /* USE_BUILTIN_STARTUP */


/* Initialize GIC 400 (GICv2) */
.global gicv2_init_secure
gicv2_init_secure:
    ldr  x0, =GICD_BASE
    mov  w9, #0x3            /* EnableGrp0 | EnableGrp1 */
    str  w9, [x0, GICD_CTLR] /* Secure GICD_CTLR */
    ldr  w9, [x0, GICD_TYPER]
    and  w10, w9, #0x1f      /* ITLinesNumber */
    cbz  w10, 1f             /* No SPIs */
    add  x11, x0, GICD_IGROUPRn
    mov  w9, #~0             /* Config SPIs as Grp1 */
    str  w9, [x11], #0x4
0:	str  w9, [x11], #0x4
    sub	 w10, w10, #0x1
    cbnz w10, 0b

    ldr  x1, =GICC_BASE      /* GICC_CTLR */
    mov	 w0, #3              /* EnableGrp0 | EnableGrp1 */
    str	 w0, [x1]

    mov	 w0, #1 << 7         /* Allow NS access to GICC_PMR */
    str	 w0, [x1, #4]        /* GICC_PMR */
1:
	ret
