/**
 * Aarch64 bootup
 * Copyright (C) 2021 wolfSSL Inc.
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


/* Current Support: 
 * Cortex-A53, Cortex-A72, Cortex-A73 
 */

#define GICD_BASE       0xF9010000
#define GICD_CTLR       0x0000
#define GICD_TYPER      0x0004
#define GICD_SGIR       0x0F00
#define GICD_IGROUPRn   0x0080

#define GICC_BASE       0xF9020000
#define GICC_PMR        0x0004

.macro PUT_64B addr_hi, addr_lo
    .dword \addr_lo
    .dword \addr_hi
.endm

/* Create table entry */
.macro TABLE_ENTRY pa, attr
    PUT_64B \attr, (\pa) + 0x3
.endm

/* Create 2MB block */
.macro BLOCK_2MB pa, addr_hi, addr_lo
    PUT_64B \addr_hi , ((\pa) & 0xffe00000) | \addr_lo | 0x1
.endm

/* Create 1GB block */
.macro BLOCK_1GB pa, addr_hi, addr_lo
    PUT_64B \addr_hi , ((\pa) & 0xc0000000) | \addr_lo | 0x1
.endm

/* Translation table layout */
.data
.align 12
ttb0_base:
    TABLE_ENTRY level2_pagetable, 0
    BLOCK_1GB 0x80000000, 0, 0x740
    BLOCK_1GB 0xC0000000, 0, 0x740

.align 12
level2_pagetable:
    .set ADDR, 0x00
    .rept 0x200
    BLOCK_2MB (ADDR << 20), 0, 0x74c
    .set ADDR, ADDR + 2
    .endr


#ifndef USE_BUILTIN_STARTUP 
.section .boot, "ax"
.global _vector_table
_vector_table:
   mov x21, x0        // read ATAG/FDT address

   /* Initalize vec tables */
   ldr x1, = _vector_table_el3
   msr vbar_el3, x1
   ldr x1, = _vector_table_el2
   msr vbar_el2, x1
   ldr x1, = _vector_table_el1
   msr vbar_el1, x1

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


8: 
/* Enable exception interrupts route to EL3 */
init_exceptions:
    mrs x0, scr_el3
    orr x0, x0, #(1<<3) /* EA bit */ 
    orr x0, x0, #(1<<1) /* IRQ bit */
    orr x0, x0, #(1<<2) /* FIQ bit */
    msr scr_el3, x0

/* Initalize register bank */
init_registers: 
    mov x0, xzr
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
    mov x6, xzr
    mov x7, xzr
    mov x8, xzr
    mov x9, xzr
    mov x10, xzr
    mov x11, xzr
    mov x12, xzr
    mov x13, xzr
    mov x14, xzr
    mov x15, xzr
    mov x16, xzr
    mov x17, xzr
    mov x18, xzr
    mov x19, xzr
    mov x20, xzr
    mov x21, xzr
    mov x22, xzr
    mov x23, xzr
    mov x24, xzr
    mov x25, xzr
    mov x26, xzr
    mov x27, xzr
    mov x28, xzr
    mov x29, xzr
    mov x30, xzr

/* Initialize stack pointer */
init_stackpointer:
    adr x1, _stack
    mrs x2, mpidr_el1        /* Get CPU ID */
    and x2, x2, #0xff
    ldr x3, =STACK_SIZE
    mul x3, x2, x3           /* Stack offset */
    sub x1, x1, x3
    mov sp, x1               /* Set stack pointer */

    msr sp_el2, x1
    msr sp_el1, x1
    msr sp_el0, x1

/* Initalize system control registers */
init_systemctl:
    msr hcr_el2, xzr
    ldr x1, =0x30C50838     /* SA,C,A,M,EE and WXN bits */ 
    msr sctlr_el2, x1
    msr sctlr_el1, x1
    msr sctlr_el3, x1

/* Initalize MMU and Caches */
init_mmu:
    bl hal_ddr_init

    /* Diable and invalidate caches */
    mrs x0, sctlr_el3       
    bic x0, x0, #(0x1 << 2)
    msr sctlr_el3, x0
    mov x0, #0x0
    msr csselr_el1, x0
    mrs x4, ccsidr_el1        /* read cache size */
    and x1, x4, #0x7
    and x1, x1, #0x4          /* cache line size */
    ldr x3, =0x7ff
    and x2, x3, x4, lsr #13   /* number of cache sets */
    ldr x3, =0x3ff
    and x3, x3, x4, lsr #3    /* cache associativity number */
    clz w4, w3
    mov x5, #0                /* loop counter */
    way_loop:
    mov x6, #0                /* loop counter */
    set_loop:
    lsl x7, x5, x4
    orr x7, x0, x7
    lsl x8, x6, x1
    orr x7, x7, x8
    dc cisw, x7               /* invalidate cache */
    add x6, x6, #1      
    cmp x6, x2
    ble set_loop              /* loop until all sets are invalidated */
    add x5, x5, #1
    cmp x5, x3
    ble way_loop              /* loop until all ways are invalidated */

    /* Set up MMU, TCR, TTBRn */
    ldr x1, =0x3520           /* 4GB, 4KB granularity, cacheable */
    msr tcr_el3, x1
    msr tcr_el1, x1

    ldr x1, =0xff440400       /* ATTr0-3 */
    msr mair_el3, x1
    msr mair_el1, x1

    adr x0, ttb0_base
    msr ttbr0_el3, x0
    msr ttbr0_el1, x0

    /* Translation tables */
    ldr x1, =level2_pagetable /* first entry points to l2 */
    ldr x2, =0xfffff000
    and x2, x1, x2
    orr x2, x2, 0x3
    str x2, [x0], #8

    /* Entry 2 & 3 */
    ldr x2, =0x80000741    /* Second entry 1GB from 0x80000000 */
    str x2, [x0], #8
    ldr x2, =0xC0000741    /* Third entry 1GB from 0xC0000000 */
    str x2, [x0], #8

    ldr x0, =level2_pagetable
    ldr x2, =0x0000074d
    mov x4, #512            /* 512 entreis */
    ldr x5, =0x00200000     /* 2MB size */
    loop:
    str x2, [x0], #8        /* 2 words per entry */
    add x2, x2, x5
    subs x4, x4, #1
    bne loop

    /* Enable MMU */
    mrs x0, S3_1_c15_c2_1
    orr x0, x0, #(0x1 << 6) /* SMP bit */
    msr S3_1_c15_c2_1, x0
 
    mrs x0, sctlr_el3
    orr x0, x0, #(0x1 << 2)  /* C bit D cache */
    orr x0, x0, #(0x1 << 12) /* I bit I cache */
    orr x0, x0, #0x1         /* M bit MMU */
    msr sctlr_el3, x0  
    dsb sy
    isb

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


/* Exception Vector Table EL3 */
.balign 0x800
.global _vector_table_el3
_vector_table_el3: 
el3_sp0_sync:     
    eret

.balign 0x80
el3_sp0_irq:
    eret

.balign 0x80
el3_spi_fiq:
    eret

.balign 0x80
el3_sp0_serror:
    eret

.balign 0x80
el3_spx_sync:
    eret

.balign 0x80
el3_spx_irq:
    eret

.balign 0x80
el3_spx_fiq:
    eret

.balign 0x80
el3_spx_serror:
    eret

.balign 0x80
lower_el3_aarch64_sync:
    eret

.balign 0x80
lower_el3_aarch64_irq:
    eret

.balign 0x80
lower_el3_aarch64_fiq:
    eret

.balign 0x80
lower_el3_aarch64_serror:
    eret

/* EL2 table */
.balign 0x800
.global _vector_table_el2
_vector_table_el2: 
el2_sp0_sync:        
    eret 

.balign 0x80
el2_sp0_irq:
    eret

.balign 0x80
el2_spi_fiq:
    eret

.balign 0x80
el2_sp0_serror:
    eret

.balign 0x80
el2_spx_sync:
    eret

.balign 0x80
el2_spx_irq:
    eret

.balign 0x80
el2_spx_fiq:
    eret

.balign 0x80
el2_spx_serror:
    eret

.balign 0x80
lower_el2_aarch64_sync:
    eret

.balign 0x80
lower_el2_aarch64_irq:
    eret

.balign 0x80
lower_el2_aarch64_fiq:
    eret

.balign 0x80
lower_el2_aarch64_serror:
    eret

/* EL1 Table  */
.balign 0x800
.global _vector_table_el1
_vector_table_el1: 
el1_sp0_sync:     
    eret

.balign 0x80
el1_sp0_irq:
    eret

.balign 0x80
el1_spi_fiq:
    eret

.balign 0x80
el1_sp0_serror:
    eret

.balign 0x80
el1_spx_sync:
    eret

.balign 0x80
el1_spx_irq:
    eret

.balign 0x80
el1_spx_fiq:
    eret

.balign 0x80
el1_spx_serror:
    eret

.balign 0x80
lower_el1_aarch64_sync:
    eret

.balign 0x80
lower_el1_aarch64_irq:
    eret

.balign 0x80
lower_el1_aarch64_fiq:
    eret

.balign 0x80
lower_el1_aarch64_serror:
    eret

