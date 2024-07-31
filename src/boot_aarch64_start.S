/**
 * Aarch64 bootup
 * Copyright (C) 2021 wolfSSL Inc.
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


/* Include target-specific defines here to override any AA64 defaults */
#if defined(TARGET_LS1028A)
#include "../hal/nxp_ls1028a.h"
#endif

/* AARCH64 default configurations */
#if !defined(AA64_TARGET_EL)
#define AA64_TARGET_EL 2
#endif

#if !defined(AA64GIC_VERSION)
#define AA64GIC_VERSION 2
#endif

#if (AA64GIC_VERSION==2)
  #if !defined(AA64GICV2_GICD_BASE)
    #define AA64GICV2_GICD_BASE     0xF9010000
  #endif
  #if !defined(AA64_GICC_BASE)
    #define AA64GICV2_GICC_BASE     0xF9020000
  #endif
#endif


/* CURRENT_EL ARMv8 Current Exception Level Register */
#define CURRENT_EL_MASK (0x3 << 2)         /* Current EL */
#define CURRENT_EL_EL0 0x0
#define CURRENT_EL_EL1 0x4
#define CURRENT_EL_EL2 0x8
#define CURRENT_EL_EL3 0xC

/* ID_AA64PFR0_EL1 ARMv8 Processor Feature Register 0*/
#define ID_AA64PFRO_EL3_MASK (0xF<<12)     /* EL3 is implemented: 0x0000 no */
                                           /* 0x1000 AA64, 0x2000 AA64+AA32 */
#define ID_AA64PFRO_EL2_MASK (0xF<<8)      /* EL2 is implemented: 0x000 no */
                                           /* 0x100 AA64, 0x200 AA64+AA32 */
#define ID_AA64PFRO_EL1_MASK (0xF<<4)      /* EL1 is implemented: */
                                           /* 0x10 AA64, 0x20 AA64+AA32 */
#define ID_AA64PFRO_EL0_MASK (0xF<<0)      /* EL0 is implemented: */
                                           /* 0x1 AA64, 0x2 AA64+AA32 */
#define ID_AA64PFRO_FGT_MASK (0xFull<<56)  /* Fine Grained Traps: */
                                           /* 0x0 no, !0x0: yes */


/* GICv2 Register Offsets */
#define GICD_CTLR       0x0000
#define GICD_TYPER      0x0004
#define GICD_SGIR       0x0F00
#define GICD_IGROUPRn   0x0080
#define GICC_PMR        0x0004

#ifndef USE_BUILTIN_STARTUP
/* This is the entry function.  If this is the start of a cold boot, the CPU
 * will be at the highest exception level (EL) and the CPU must be configured
 * for each of the levels down to the target EL: either EL2 for a hypervisor
 * or EL1 for a standard OS.
 *
 * Configuration only enables secure EL3 and forces all lower levels NS.
 *
 * AA64_TARGET_EL: 1 or 2
 * AA64_GICVERSION: 0- no external GIC, 2: GICv2, 3: GICv3
 * AA64_ENABLE_EL3_SMC: Enable SMC call handling in EL3
 * AA64_ENABLE_EL3_PM: Enable handling of power management (TWE, TWI)
 */

.section ".boot", "ax"
.global _vector_table
_vector_table:
    /* If we are booted as a Linux direct boot, then X0 will have FDT */
    mov     x21, x0             /* save ATAG/FDT address */



    /* Get highest EL implemented in this CPU */
    bl      aa64_get_highest_el
    mov     x19, x0             /* save highest EL in x19 */

    /* Get current EL */
    bl      aa64_get_current_el
    mov     x20, x0             /* save current EL in x20 */

    cmp     x19, x20            /* EL is at highest? */
    bne     3f
    bl      aa64_setup_el_highest

3:  cmp     x20, #0x3           /* at EL3? */
    bne     2f
    bl      aa64_setup_el3

2:  cmp     x20, #0x1           /* EL == 1? */
    beq     1f

    /* EL2 Setup */
    mov     x2, #3 << 20
    msr     cptr_el2, x2        /* Disable FP/SIMD traps for EL2 */
    b 0f

    /* EL1 Setup */
1:  mov     x0, #3 << 20
    msr     cpacr_el1, x0       /* Disable FP/SIMD traps for EL1 */
    msr     sp_el1, x1

    /* Suspend slave CPUs */
0:  mrs     x3, mpidr_el1       /* read MPIDR_EL1 */
    and     x3, x3, #3          /* CPUID = MPIDR_EL1 & 0x03 */
    cbz     x3, 8f              /* if 0, branch forward */
7:  wfi                         /* infinite sleep */
    b       7b

8:  ldr     x1, =_vector_table  /* ??? get start of .text in x1 */
    mov     sp, x1              /* XXX set stack pointer */

#ifdef CORTEX_A72
    bl      init_A72
#endif
    bl      boot_entry_C       /* boot_entry_C never returns */
    b       7b                 /* go to sleep anyhow in case. */
#endif /* USE_BUILTIN_STARTUP */

/* Return the highest EL implemented on this CPU in x0
 * No stack usage. No clobbers. */
.global aa64_get_highest_el
.type aa64_get_highest_el, @function
aa64_get_highest_el:
    mrs     x0, ID_AA64PFR0_EL1
    tst     x0, ID_AA64PFR0_EL3_MASK
    cbz     2f                  /* Highest is not EL3? */
    mov     x0, #0x3
    ret
2:  tst     x0, ID_AA64PFR0_EL2_MASK
    cbz     1f                  /* Highest is not EL2? */
    mov     x0, #0x2
    ret
1:  mov     x0, #0x1            /* Highest is EL1  */
    ret

/* Return the current EL on this CPU in x0
 * No stack usage. No clobbers. */
.global aa64_get_current_el
.type aa64_get_current_el, @function
aa64_get_current_el:
    mrs     x0, CURRENT_EL
    tst     x0, CURRENT_EL3_MASK
    cbz     2f                  /* Current is not EL3? */
    mov     x0, #0x3
    ret
2:  tst     x0, CURRENT_EL2_MASK
    cbz     1f                  /* Current is not EL2? */
    mov     x0, #0x2
    ret
1:  tst     x0, CURRENT_EL1_MASK
    cbz     0f                  /* Current is not EL1? */
    mov     x0, #0x1
    ret
0:  mov     x0, #0x0            /* Current is EL0  */
    ret

/* Perform chip setup when at the highest EL
 * No stack. Clobbers: x0 */
.global aa64_setup_el_highest
.type aa64_setup_el_highest, @function
aa64_setup_el_highest
#if defined(AA64_CNTFRQ)
    /* Set the counter-timer frequency to AA64_CNTFRQ*/
    mov     x0, AA64_CNTFRQ
    msr     cntfrq_el0, x0
#endif
    ret

/* Perform chip setup when at the EL3
 * No stack. Clobbers: x0 */
.global aa64_setup_el3
.type aa64_setup_el3, @function
aa64_setup_el3
    mrs     x0, scr_el3         /* Get Secure Config Reg scr_el3 */
    bic     x0, x0, #(1 << 18)  /* EEL2 Disable Secure EL2 */
#if !defined (AA64_ENABLE_EL3_PM)
    bic     x0, x0, #(1 << 13)  /* TWE Disable trap WFE to EL3 */
    bic     x0, x0, #(1 << 12)  /* TWI Disable trap WFI to EL3 */
#else
    orr     x0, x0, #(1 << 13)  /* TWE Enable trap WFE to EL3 */
    orr     x0, x0, #(1 << 12)  /* TWI Enable trap WFI to EL3 */
#endif
    orr     x0, x0, #(1 << 11)  /* ST Disable trap SEL1 acc CNTPS to EL3 */
    orr     x0, x0, #(1 << 10)  /* RW Next lower level is AArch64 */
    orr     x0, x0, #(1 << 9)   /* SIF Disable Sec Ins Fetch from NS mem */
#if defined(AA64_TARGET_EL) && (AA64_TARGET_EL==2)
    orr     x0, x0, #(1 << 8)   /* HCE Enable Hypervisor Call HVC */
#else
    bic     x0, x0, #(1 << 8)   /* HCE Disable Hypervisor Call HVC */
#endif
#if !defined(AA64_ENABLE_EL3_SMC)
    orr     x0, x0, #(1 << 7)   /* SMD Disable Secure Monitor Call SMC */
#else
    bic     x0, x0, #(1 << 7)   /* SMD Enable Secure Monitor Call SMC */
#endif
    bic     x0, x0, #(1 << 3)   /* EA Disable EA and SError to EL3 */
    bic     x0, x0, #(1 << 2)   /* FIQ Disable FIQ to EL3 */
    bic     x0, x0, #(1 << 1)   /* IRQ Disable IRQ to EL3 */
    orr     x0, x0, #(1 << 0)   /* NS EL0, EL1, and EL2 are NS */
    msr     scr_el3, x0         /* Set scr_el3 */

    mrs     x0, cptr_el3        /* Get EL3 Feature Trap Reg CPTR_EL3 */
    bic     x0, x0, #(1 << 31)  /* TCPAC Disable config traps to EL3 */
    bic     x0, x0, #(1 << 30)  /* TAM Disable AM traps to EL3 */
    bic     x0, x0, #(1 << 20)  /* TTA Disable trace traps to EL3 */
    bic     x0, x0, #(1 << 12)  /* ESM Disable SVCR traps to EL3 */
    bic     x0, x0, #(1 << 10)  /* TFP Disable FP/SIMD traps to EL3 */
    bic     x0, x0, #(1 << 20)  /* EZ Disable ZCR traps to EL3 */
    msr     cptr_el3, x0        /* Set cptr_el3 */

#if defined(AA64_TARGET_EL) && (AA64_TARGET_EL==2)
    orr     x0, x0, #(1 << 8)   /* HCE Enable Hypervisor Call HVC */
#else
    bic     x0, x0, #(1 << 8)   /* HCE Disable Hypervisor Call HVC */
#endif

    ret


/* Initialize GIC 400 (GICv2) */
.global gicv2_init_secure
gicv2_init_secure:
    ldr  x0, =AA64_GICD_BASE
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

    ldr  x1, =AA64_GICC_BASE      /* GICC_CTLR */
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
    mrs x0, scr_el3             /* Get scr_el3 */
    bic x0, x0, #(1 << 18)      /* EEL2 Disable Secure EL2 */
#if !defined (AA64_ENABLE_EL3_PM)
    bic x0, x0, #(1 << 13)      /* TWE Disable trap WFE to EL3 */
    bic x0, x0, #(1 << 12)      /* TWI Disable trap WFI to EL3 */
#else
    orr x0, x0, #(1 << 13)      /* TWE Enable trap WFE to EL3 */
    orr x0, x0, #(1 << 12)      /* TWI Enable trap WFI to EL3 */
#endif
    orr x0, x0, #(1 << 11)      /* ST Disable trap SEL1 access CNTPS to EL3 */
    orr x0, x0, #(1 << 10)      /* RW Next lower level is AArch64 */
    orr x0, x0, #(1 << 9)       /* SIF Disable secure ins. fetches from NS */
#if defined(AA64_TARGET_EL) && (AA64_TARGET_EL==2)
    orr x0, x0, #(1 << 8)       /* HCE Enable Hypervisor Call HVC */
#else
    bic x0, x0, #(1 << 8)       /* HCE Disable Hypervisor Call HVC */
#endif
#if !defined(AA64_ENABLE_EL3_SMC)
    orr x0, x0, #(1 << 7)       /* SMD Disable Secure Monitor Call SMC */
#else
    bic x0, x0, #(1 << 7)       /* SMD Enable Secure Monitor Call SMC */
#endif
    orr x0, x0, #(1 << 3)       /* EA Enable EA and SError to EL3 for now */
    orr x0, x0, #(1 << 2)       /* FIQ Enable FIQ to EL3 for now */
    orr x0, x0, #(1 << 1)       /* IRQ Enable IRQ to EL3 for now */
    orr x0, x0, #(1 << 0)       /* NS EL0, EL1, and EL2 are NS */
    msr scr_el3, x0             /* Set scr_el3 */

    mrs x0, sctlr_el3           /* sctlr_el3 config */
    bic x0, x0, #(1 << 19)      /* Disable EL3 translation XN */
    bic x0, x0, #(1 << 12)      /* Disable I cache */
    bic x0, x0, #(1 << 3)       /* Disable SP Alignment check */
    bic x0, x0, #(1 << 2)       /* Disable D cache */
    bic x0, x0, #(1 << 1)       /* Disable Alignment check */
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
