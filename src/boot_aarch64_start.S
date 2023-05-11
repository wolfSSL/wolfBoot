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


#define GICD_BASE       0xF9010000
#define GICD_CTLR       0x0000
#define GICD_TYPER      0x0004
#define GICD_SGIR       0x0F00
#define GICD_IGROUPRn   0x0080

#define GICC_BASE       0xF9020000
#define GICC_PMR        0x0004

.equ TZPCDECPROT0_SET_BASE, 0x02200804
.equ TZPCDECPROT1_SET_BASE, 0x02200810
.equ OCRAM_TZPC_ADDR      , 0x02200000

#ifndef USE_BUILTIN_STARTUP
.section ".boot", "ax"
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

#ifdef CPU_A72
    bl  init_A72
#endif
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


.global invalidate_ivac
invalidate_ivac:
    ldr x0, =_OCRAM_ADDRESS
    ldr x1, =_MEMORY_SIZE
    add x1, x1, x0
    mrs x2, ctr_el0
    ubfx x4, x2, #16, #4
    mov x3, #4
    lsl x3, x3, x4
    sub x4, x3, #1
    bic x4, x0, x4
    inval_loop:
    dc ivac, x4
    add x4, x4, x3
    cmp x4, x1
    blt inval_loop
    dsb sy
    ret

.global disable_mmu
disable_mmu:  
    mrs x0, sctlr_el3
    bic x0, x0, x1
    msr sctlr_el3, x0
    isb
    dsb sy
    ret

.global switch_el3_to_el2
switch_el3_to_el2:
    mov x0, #0x531
    msr scr_el3, x0
    msr cptr_el3, xzr            /* Disable el3 traps */
    mov x0, #0x33ff
    msr cptr_el2, x0             /* Disable el2 traps */
    mrs x0, sctlr_el2
    mov x1, #(1 << 0) | (1 << 2) | (1 << 12)
    bic x0, x0, x1
    msr sctlr_el2, x0
    mrs x0, sctlr_el3
    bic x0, x0, x1
    msr sctlr_el3, x0
    bl invalidate_ivac
    ldp x29, x30, [sp]
    mrs x0, vbar_el3
    msr vbar_el2, x0
    mov x0, #0x3c9
    msr spsr_el3, x0
    msr elr_el3, x30
    ret

.global cortex_a72_erratta
cortex_a72_erratta:


/* Initalization code for NXP LS1028a (A72) */
.global init_A72
init_A72:
    ldr x1, =_vector_table_el3  /* Initalize vec table */
    msr vbar_el3, x1

el3_state: 
    mrs x0, scr_el3             /* scr_el3 config */
    bic x0, x0, #(1 << 13)      /* Trap WFE instruciton to EL3 off */
    bic x0, x0, #(1 << 12)      /* Traps TWI ins to EL3 off */
    bic x0, x0, #(1 << 11)      /* Traps EL1 access to physical secure timer to EL3 on */
    orr x0, x0, #(1 << 10)      /* Next lower level is AArch64 */
    bic x0, x0, #(1 << 9)       /* Secure state instuction fetches from non-secure memory are permitted */
    bic x0, x0, #(1 << 8)       /* Hypervisor Call instruction disabled */
    bic x0, x0, #(1 << 7)       /* Secure Monitor Call enabled */
    orr x0, x0, #0xf            /* IRQ|FIQ|EA to EL3 */
    msr scr_el3, x0

    mrs x0, sctlr_el3           /* sctlr_el3 config */
    bic x0, x0, #(1 << 19)      /* Disable EL3 translation XN */ 
    bic x0, x0, #(1 << 12)      /* Disable I cache */
    bic x0, x0, #(1 << 3)       /* Disable SP Alignment check */
    bic x0, x0, #(1 << 2)       /* Disable D cache */
    bic x0, x0, #(1 << 1)       /* Disable Alignmet check */
    bic x0, x0, #(1 << 0)       /* Disable MMU */
    msr sctlr_el3, x0
    isb

invalidate_cache:
    msr csselr_el1, x0
    mrs x4, ccsidr_el1          /* read cache size */
    and x1, x4, #0x7
    and x1, x1, #0x4            /* cache line size */
    ldr x3, =0x7ff
    and x2, x3, x4, lsr #13     /* number of cache sets */
    ldr x3, =0x3ff
    and x3, x3, x4, lsr #3      /* cache associativity number */
    clz w4, w3
    mov x5, #0                
way_loop:
    mov x6, #0                
set_loop:
    lsl x7, x5, x4
    orr x7, x0, x7
    lsl x8, x6, x1
    orr x7, x7, x8
    dc cisw, x7                  /* invalidate cache */
    add x6, x6, #1      
    cmp x6, x2
    ble set_loop                 /* loop until all sets are invalidated */
    add x5, x5, #1
    cmp x5, x3
    ble way_loop                 /* loop until all ways are invalidated */ 
    msr cptr_el3, xzr

init_stack:
    ldr x0, =_stack_base        /* Set and align stack */
    sub x0, x0, #16
    and x0, x0, #-16
    mov sp, x0
    ldr x1, =STACK_SIZE
    msr sp_el2, x0
    msr sp_el1, x0
    msr sp_el0, x0
    mov x29, 0                  /* Setup an initial dummy frame with saved fp=0 and saved lr=0 */
    stp x29, x29, [sp, #-16]!
    mov x29, sp

    bl invalidate_ivac
    b boot_entry_C

.global mmu_enable
mmu_enable:
    tlbi alle3                    /* Invalidate table entries */
    dsb sy
    isb
 
    /* Set tcr reg */
    ldr x0, =0x0
    orr x0, x0, #24               /* Size of the memory region */ 
    orr x0, x0, #(1 << 17)        /* PS 40 bit */
    orr x0, x0, #(1 << 16)        /* TG0 4KB */
    orr x0, x0, #(2 << 12)        /* SH0 Outer Shareable */
    orr x0, x0, #(1 << 10)        /* normal outer WBWA cacheable */
    orr x0, x0, #(1 << 8)         /* normal inner WBWA cacheable */
    msr tcr_el3, x0

    ldr x1, =0x44E048E000098AA4 //0xFF440C0400
    msr mair_el3, x1

    ldr x0, =ttb0_base       
    msr ttbr0_el3, x0

    mrs x0, S3_1_c15_c2_1
    orr x0, x0, #(1 << 6)        /* Must set SPMEN */
    msr S3_1_c15_c2_1, x0
    isb

    /* Set sctlr reg */
    mrs x0, sctlr_el3
    orr x1, x0, #(1 << 12)       /* I - instruction cache enable */
    orr x1, x0, #(1 << 2)        /* C - data & unified cache enable */
    orr x1, x0, #(1 << 0)        /* M - MMU enable */
    msr sctlr_el3, x1

    dsb sy
    isb
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


/* Memory Table Macros */
.macro PUT_64BIT_WORD high, low
    .word \low
    .word \high
.endm

.macro TABLE_ENTRY PA, attributes
PUT_64BIT_WORD \attributes, \PA + 0x3
.endm

.macro BLOCK_1GB PA, attr_hi, attr_lo
PUT_64BIT_WORD \attr_hi, ((\PA) & 0xc0000000) | \attr_lo | 0x1
.endm

.macro BLOCK_2MB PA, attr_hi, attr_lo
PUT_64BIT_WORD \attr_hi, ((\PA) & 0xffe00000) | \attr_lo | 0x1
.endm

/* Note: In EL3/2 has direct physical to virutal mapping */
.align 12
.global ttb0_base
ttb0_base:
TABLE_ENTRY level1_pagetable, 0
BLOCK_1GB 0x80000000, 0, 0x740
BLOCK_1GB 0xC0000000, 0, 0x740

.align 12
.global level1_pagetable
level1_pagetable:
.set ADDR, 0x0
.rept 0x200
BLOCK_2MB (ADDR << 20), 0, 0x74c
.set ADDR, ADDR + 2
.endr
