/**
 * Aarch64 bootup
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

/* Code is adapted from the default AMD/Xilinx boot.S, translation_table.S and
 * asm_vectors.S*/

#ifdef TARGET_zynq
#include "hal/zynq.h"
#endif

#ifdef TARGET_nxp_ls1028a
#include "hal/nxp_ls1028a.h"
#endif

#ifdef TARGET_raspi3
#include "hal/raspi3.h"
#endif

/* GICv2 Register Offsets */
#ifndef GICD_BASE
#define GICD_BASE       0xF9010000
#endif
#define GICD_CTLR       0x0000
#define GICD_TYPER      0x0004
#define GICD_SGIR       0x0F00
#define GICD_IGROUPRn   0x0080

#ifndef GICC_BASE
#define GICC_BASE       0xF9020000
#endif
#define GICC_PMR        0x0004


#ifndef USE_BUILTIN_STARTUP

.globl MMUTableL0
.globl MMUTableL1
.globl MMUTableL2
.global _prestart
.global _boot

.global __el3_stack
.global __el2_stack
.global __el1_stack
.global __el0_stack
.global _vector_table

.globl FIQInterrupt
.globl IRQInterrupt
.globl SErrorInterrupt
.globl SynchronousInterrupt
.globl FPUStatus

.set EL3_stack,   __el3_stack
.set EL2_stack,   __el2_stack
.set EL1_stack,   __el1_stack
.set EL0_stack,   __el0_stack

.set L0Table,     MMUTableL0
.set L1Table,     MMUTableL1
.set L2Table,     MMUTableL2
.set vector_base, _vector_table
.set rvbar_base,  0xFD5C0040

# Cortex-A53 timestamp clock frequency
.set counterfreq, 99990005

.set MODE_EL1,    0x5
.set DAIF_BIT,    0x1C0


.section .boot,"ax"
_boot:
    mov    x0, #0
    mov    x1, #0
    mov    x2, #0
    mov    x3, #0
    mov    x4, #0
    mov    x5, #0
    mov    x6, #0
    mov    x7, #0
    mov    x8, #0
    mov    x9, #0
    mov    x10, #0
    mov    x11, #0
    mov    x12, #0
    mov    x13, #0
    mov    x14, #0
    mov    x15, #0
    mov    x16, #0
    mov    x17, #0
    mov    x18, #0
    mov    x19, #0
    mov    x20, #0
    mov    x21, #0
    mov    x22, #0
    mov    x23, #0
    mov    x24, #0
    mov    x25, #0
    mov    x26, #0
    mov    x27, #0
    mov    x28, #0
    mov    x29, #0
    mov    x30, #0

    /* Init Exception Level */
    mrs    x0, currentEL
    cmp    x0, #0xC
    beq    InitEL3

    cmp    x0, #0x8
    beq    InitEL2

    cmp    x0, #0x4
    beq    InitEL1

    /* go to error if current exception level is not EL1-3 */
    b      error

InitEL3:
#if defined(EL3_SECURE) && EL3_SECURE == 1
    /* Set vector table base address */
    ldr    x1, =vector_base
    msr    VBAR_EL3,x1

    /* Set reset vector address */
    /* Get the cpu ID */
    mrs    x0, MPIDR_EL1
    and    x0, x0, #0xFF
    mov    w0, w0
    ldr    w2, =rvbar_base
    /* calculate the RVBAR base address for particular CPU core */
    mov    w3, #0x8
    mul    w0, w0, w3
    add    w2, w2, w0
    /* store vector base address to RVBAR */
    str    x1, [x2]

    /* Define stack pointer for current exception level */
    ldr    x2,=EL3_stack
    mov    sp,x2

    /* Enable Trapping of SIMD/FPU register for standalone BSP */
    mov    x0, #0
#if defined(FPU_TRAP) && FPU_TRAP == 1
    orr    x0, x0, #(0x1 << 10)
#endif
    msr    CPTR_EL3, x0
    isb

    /* Clear FPUStatus variable to make sure that it contains current
     * status of FPU i.e. disabled. In case of a warm restart execution
     * when bss sections are not cleared, it may contain previously updated
     * value which does not hold true now.
     */
#if defined(FPU_TRAP) && FPU_TRAP == 1
     ldr   x0,=FPUStatus
     str   xzr, [x0]
#endif
    /* Configure SCR_EL3 */
    mov    w1, #0              /* Initial value of register is unknown */
    orr    w1, w1, #(1 << 11)  /* Set ST bit (Secure EL1 can access CNTPS_TVAL_EL1, CNTPS_CTL_EL1 & CNTPS_CVAL_EL1) */
    orr    w1, w1, #(1 << 10)  /* Set RW bit (EL1 is AArch64, as this is the Secure world) */
    orr    w1, w1, #(1 << 3)   /* Set EA bit (SError routed to EL3) */
    orr    w1, w1, #(1 << 2)   /* Set FIQ bit (FIQs routed to EL3) */
    orr    w1, w1, #(1 << 1)   /* Set IRQ bit (IRQs routed to EL3) */
    msr    SCR_EL3, x1

    /* Configure cpu auxiliary control register EL1 */
    ldr    x0,=0x80CA000          /* L1 Data prefetch control - 5, Enable device split throttle, 2 independent data prefetch streams */
#if defined(CONFIG_ARM_ERRATA_855873) && CONFIG_ARM_ERRATA_855873
    /* Set ENDCCASCI bit in CPUACTLR_EL1 register, to execute data
     * cache clean operations as data cache clean and invalidate
     */
    orr    x0, x0, #(1 << 44)     /* Set ENDCCASCI bit */
#endif
    msr    S3_1_C15_C2_0, x0      /* CPUACTLR_EL1 */

    /* Program the counter frequency */
    ldr    x0,=counterfreq
    msr    CNTFRQ_EL0, x0

    /* Enable hardware coherency between cores */
    mrs    x0, S3_1_c15_c2_1      /* Read EL1 CPU Extended Control Register */
    orr    x0, x0, #(1 << 6)      /* Set the SMPEN bit */
    msr    S3_1_c15_c2_1, x0      /* Write EL1 CPU Extended Control Register */
    isb

    tlbi   ALLE3
    ic     IALLU                  /* Invalidate ICache to PoU */
    bl     invalidate_dcaches
    dsb    sy
    isb

    ldr    x1, =L0Table           /* Get address of level 0 for TTBR0_EL3 */
    msr    TTBR0_EL3, x1          /* Set TTBR0_EL3 */

    /**********************************************
    * Set up memory attributes
    * This equates to:
    * 0 = b01000100 = Normal, Inner/Outer Non-Cacheable
    * 1 = b11111111 = Normal, Inner/Outer WB/WA/RA
    * 2 = b00000000 = Device-nGnRnE
    * 3 = b00000100 = Device-nGnRE
    * 4 = b10111011 = Normal, Inner/Outer WT/WA/RA
    **********************************************/
    ldr    x1, =0x000000BB0400FF44
    msr    MAIR_EL3, x1

    /**********************************************
    * Set up TCR_EL3
    * Physical Address Size PS =  010 -> 40bits 1TB
    * Granual Size TG0 = 00 -> 4KB
    * size offset of the memory region T0SZ = 24 -> (region size 2^(64-24) = 2^40)
    ***************************************************/
    ldr    x1,=0x80823518

    msr    TCR_EL3, x1
    isb

    /* Enable SError Exception for asynchronous abort */
    mrs    x1,DAIF
    bic    x1,x1,#(0x1<<8)
    msr    DAIF,x1

    /* Configure SCTLR_EL3 */
    mov    x1, #0                 /* Most of the SCTLR_EL3 bits are unknown at reset */
    orr    x1, x1, #(1 << 12)     /* Enable I cache */
    orr    x1, x1, #(1 << 3)      /* Enable SP alignment check */
    orr    x1, x1, #(1 << 2)      /* Enable caches */
    orr    x1, x1, #(1 << 0)      /* Enable MMU */
    msr    SCTLR_EL3, x1
    dsb    sy
    isb

    bl     boot_entry_C           /* jump to start */
#else
    /* present exception level and selected exception level mismatch */
    b      error
#endif

InitEL2:
#if defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
    /* Set vector table base address */
    ldr    x1, =vector_base
    msr    VBAR_EL2, x1

    /* Define stack pointer for current exception level */
    ldr    x2,=EL2_stack
    mov    sp,x2

    mov    x0, #0x33ff
    msr    CPTR_EL2, x0   /* Enable FP/SIMD */

    /* Invalidate TLB */
    tlbi    alle2
    /* Invalidate ICache */
    ic     ialluis
    isb    sy
    /* Invalidate DCache */
    bl     invalidate_dcaches
    dsb    sy
    isb

    ldr    x1, =L0Table           /* Get address of level 0 for TTBR0_EL2 */
    msr    TTBR0_EL2, x1          /* Set TTBR0_EL2 */

    /**********************************************
    * Set up memory attributes
    * This equates to:
    * 0 = b01000100 = Normal, Inner/Outer Non-Cacheable
    * 1 = b11111111 = Normal, Inner/Outer WB/WA/RA
    * 2 = b00000000 = Device-nGnRnE
    * 3 = b00000100 = Device-nGnRE
    * 4 = b10111011 = Normal, Inner/Outer WT/WA/RA
    **********************************************/
    ldr    x1, =0x000000BB0400FF44
    msr    MAIR_EL2, x1

    /**********************************************
    * Set up TCR_EL2
    * Physical Address Size PS =  010 -> 40bits 1TB
    * Granual Size TG0 = 00 -> 4KB
    * size offset of the memory region T0SZ = 24 -> (region size 2^(64-24) = 2^40)
    ***************************************************/
    ldr    x1,=0x80823518

    msr    TCR_EL2, x1
    isb

    /* Enable ICache */
    mrs    x1, SCTLR_EL2
    orr    x1, x1, #(1 << 12)     /* Enable ICache */
    orr    x1, x1, #(1 << 3)      /* Enable SP alignment check */
    orr    x1, x1, #(1 << 2)      /* Enable DCaches */
    orr    x1, x1, #(1 << 0)      /* Enable MMU */
    msr    SCTLR_EL2, x1
    dsb    sy
    isb

    bl     boot_entry_C           /* jump to start */
#else
    /* present exception level and selected exception level mismatch */
    b      error
#endif

InitEL1:
#if defined(EL1_NONSECURE) && EL1_NONSECURE == 1
    /* Set vector table base address */
    ldr    x1, =vector_base
    msr    VBAR_EL1,x1

    /* Trap floating point access only in case of standalone BSP */
#if defined(FPU_TRAP) && FPU_TRAP == 0
    mrs    x0, CPACR_EL1
    orr    x0, x0, #(0x3 << 20)
    msr    CPACR_EL1, x0
#else
    mrs    x0, CPACR_EL1
    bic    x0, x0, #(0x3 << 20)
    msr    CPACR_EL1, x0
#endif
    isb

    /* Clear FPUStatus variable to make sure that it contains current
     * status of FPU i.e. disabled. In case of a warm restart execution
     * when bss sections are not cleared, it may contain previously updated
     * value which does not hold true now.
     */
#if defined(FPU_TRAP) && FPU_TRAP == 1
     ldr   x0,=FPUStatus
     str   xzr, [x0]
#endif
    /* Define stack pointer for current exception level */
    ldr    x2,=EL1_stack
    mov    sp,x2

    /* Disable MMU first */
    mov    x1,#0x0
    msr    SCTLR_EL1, x1
    isb

    tlbi   VMALLE1
    ic     IALLU                  /* Invalidate I cache to PoU */
    bl     invalidate_dcaches
    dsb    sy
    isb

    ldr    x1, =L0Table           /* Get address of level 0 for TTBR0_EL1 */
    msr    TTBR0_EL1, x1          /* Set TTBR0_EL1 */

    /**********************************************
    * Set up memory attributes
    * This equates to:
    * 0 = b01000100 = Normal, Inner/Outer Non-Cacheable
    * 1 = b11111111 = Normal, Inner/Outer WB/WA/RA
    * 2 = b00000000 = Device-nGnRnE
    * 3 = b00000100 = Device-nGnRE
    * 4 = b10111011 = Normal, Inner/Outer WT/WA/RA
    **********************************************/
    ldr    x1, =0x000000BB0400FF44
    msr    MAIR_EL1, x1

    /**********************************************
    * Set up TCR_EL1
    * Physical Address Size PS =  010 -> 44bits 16TB
    * Granual Size TG0 = 00 -> 4KB
    * size offset of the memory region T0SZ = 24 -> (region size 2^(64-24) = 2^40)
    ***************************************************/
    ldr    x1,=0x285800518

    msr    TCR_EL1, x1
    isb

    /* Enable SError Exception for asynchronous abort */
    mrs    x1,DAIF
    bic    x1,x1,#(0x1<<8)
    msr    DAIF,x1

    /* Enable MMU */
    mov    x1,#0x0
    orr    x1, x1, #(1 << 18)     /* Set WFE non trapping */
    orr    x1, x1, #(1 << 17)     /* Set WFI non trapping */
    orr    x1, x1, #(1 << 5)      /* Set CP15 barrier enabled */
    orr    x1, x1, #(1 << 12)     /* Set I bit */
    orr    x1, x1, #(1 << 2)      /* Set C bit */
    orr    x1, x1, #(1 << 0)      /* Set M bit */
    msr    SCTLR_EL1, x1
    isb

    bl     boot_entry_C           /* jump to start */
#else
    /* present exception level and selected exception level mismatch */
    b      error
#endif

/* Assembly startup error handler */
error:
    b      error


invalidate_dcaches:
    dmb    ISH
    mrs    x0, CLIDR_EL1          /* x0 = CLIDR */
    ubfx   w2, w0, #24, #3        /* w2 = CLIDR.LoC */
    cmp    w2, #0                 /* LoC is 0? */
    b.eq   invalidatecaches_end   /* No cleaning required and enable MMU */
    mov    w1, #0                 /* w1 = level iterator */

invalidatecaches_flush_level:
    add    w3, w1, w1, lsl #1     /* w3 = w1 * 3 (right-shift for cache type) */
    lsr    w3, w0, w3             /* w3 = w0 >> w3 */
    ubfx   w3, w3, #0, #3         /* w3 = cache type of this level */
    cmp    w3, #2                 /* No cache at this level? */
    b.lt   invalidatecaches_next_level

    lsl    w4, w1, #1
    msr    CSSELR_EL1, x4         /* Select current cache level in CSSELR */
    isb                           /* ISB required to reflect new CSIDR */
    mrs    x4, CCSIDR_EL1         /* w4 = CSIDR */

    ubfx   w3, w4, #0, #3
    add    w3, w3, #2             /* w3 = log2(line size) */
    ubfx   w5, w4, #13, #15
    ubfx   w4, w4, #3, #10        /* w4 = Way number */
    clz    w6, w4                 /* w6 = 32 - log2(number of ways) */

invalidatecaches_flush_set:
    mov    w8, w4                 /* w8 = Way number */
invalidatecaches_flush_way:
    lsl    w7, w1, #1             /* Fill level field */
    lsl    w9, w5, w3
    orr    w7, w7, w9             /* Fill index field */
    lsl    w9, w8, w6
    orr    w7, w7, w9             /* Fill way field */
    dc     CISW, x7               /* Invalidate by set/way to point of coherency */
    subs   w8, w8, #1             /* Decrement way */
    b.ge   invalidatecaches_flush_way
    subs   w5, w5, #1             /* Descrement set */
    b.ge   invalidatecaches_flush_set

invalidatecaches_next_level:
    add    w1, w1, #1             /* Next level */
    cmp    w2, w1
    b.gt   invalidatecaches_flush_level

invalidatecaches_end:
    ret



/*
* Below is the static translation page table required by MMU for Cortex-A53.
* The translation table is flat mapped (input address = output address) with
* default memory attributes defined for Zynq Ultrascale+ architecture.
* It utilizes translation granual size of 4KB with 2MB section size for
* initial 4GB memory and 1GB section size for memory after 4GB.
* The overview of translation table memory attributes is described below.
*
*|                       | Memory Range                | Definition in Translation Table   |
*|-----------------------|-----------------------------|-----------------------------------|
*| DDR                   | 0x0000000000 - 0x007FFFFFFF | Normal write-back Cacheable       |
*| PL                    | 0x0080000000 - 0x00BFFFFFFF | Strongly Ordered                  |
*| QSPI, lower PCIe      | 0x00C0000000 - 0x00EFFFFFFF | Strongly Ordere                   |
*| Reserved              | 0x00F0000000 - 0x00F7FFFFFF | Unassigned                        |
*| STM Coresight         | 0x00F8000000 - 0x00F8FFFFFF | Strongly Ordered                  |
*| GIC                   | 0x00F9000000 - 0x00F91FFFFF | Strongly Ordered                  |
*| Reserved              | 0x00F9200000 - 0x00FCFFFFFF | Unassigned                        |
*| FPS, LPS slaves       | 0x00FD000000 - 0x00FFBFFFFF | Strongly Ordered                  |
*| CSU, PMU              | 0x00FFC00000 - 0x00FFDFFFFF | Strongly Ordered                  |
*| TCM, OCM              | 0x00FFE00000 - 0x00FFFFFFFF | Normal inner write-back cacheable |
*| Reserved              | 0x0100000000 - 0x03FFFFFFFF | Unassigned                        |
*| PL, PCIe              | 0x0400000000 - 0x07FFFFFFFF | Strongly Ordered                  |
*| DDR                   | 0x0800000000 - 0x0FFFFFFFFF | Normal inner write-back cacheable |
*| PL, PCIe              | 0x1000000000 - 0xBFFFFFFFFF | Strongly Ordered                  |
*| Reserved              | 0xC000000000 - 0xFFFFFFFFFF | Unassigned                        |
*
* For DDR region 0x0000000000 - 0x007FFFFFFF, a system where DDR is less than
* 2GB, region after DDR and before PL is marked as undefined/reserved in
* translation table. Region 0xF9100000 - 0xF91FFFFF is reserved memory in
* 0x00F9000000 - 0x00F91FFFFF range, but it is marked as strongly ordered
* because minimum section size in translation table section is 2MB. Region
* 0x00FFC00000 - 0x00FFDFFFFF contains CSU and PMU memory which are marked as
* Device since it is less than 1MB and falls in a region with device memory.
*/

.set reserved,  0x0                             /* Fault */
#if defined(EL1_NONSECURE) && EL1_NONSECURE == 1
.set Memory,    0x405 | (2 << 8) | (0x0)        /* normal writeback write allocate outer shared read write */
#else
.set Memory,    0x405 | (3 << 8) | (0x0)        /* normal writeback write allocate inner shared read write */
#endif
.set Device,    0x409 | (1 << 53) | (1 << 54) | (0x0) /* strongly ordered read write non executable*/
.section .mmu_tbl0,"a"

MMUTableL0:

.set   SECT, MMUTableL1        /* 0x0000_0000 -  0x7F_FFFF_FFFF */
.8byte SECT + 0x3
.set   SECT, MMUTableL1+0x1000 /* 0x80_0000_0000 - 0xFF_FFFF_FFFF */
.8byte SECT + 0x3

.section .mmu_tbl1,"a"

MMUTableL1:

.set  SECT, MMUTableL2         /* 0x0000_0000 - 0x3FFF_FFFF */
.8byte SECT + 0x3              /* 1GB DDR */

.rept  0x3                     /* 0x4000_0000 - 0xFFFF_FFFF */
.set   SECT, SECT + 0x1000     /*1GB DDR, 1GB PL, 2GB other devices n memory */
.8byte SECT + 0x3
.endr

.set   SECT,0x100000000
.rept  0xC                     /* 0x0001_0000_0000 - 0x0003_FFFF_FFFF */
.8byte SECT + reserved         /* 12GB Reserved */
.set   SECT, SECT + 0x40000000
.endr

.rept   0x10                   /* 0x0004_0000_0000 - 0x0007_FFFF_FFFF */
.8byte SECT + Device           /* 8GB PL, 8GB PCIe */
.set   SECT, SECT + 0x40000000
.endr


#ifdef XPAR_PSU_DDR_1_S_AXI_BASEADDR
.set DDR_1_START, XPAR_PSU_DDR_1_S_AXI_BASEADDR
.set DDR_1_END, XPAR_PSU_DDR_1_S_AXI_HIGHADDR
.set DDR_1_SIZE, (DDR_1_END - DDR_1_START)+1
#if defined(DDR_1_SIZE) && DDR_1_SIZE > 0x800000000
/* If DDR size is larger than 32GB, truncate to 32GB */
.set DDR_1_REG, 0x20
#else
.set DDR_1_REG, DDR_1_SIZE/0x40000000
#endif
#else
.set DDR_1_REG, 0
#endif

.set UNDEF_1_REG, 0x20 - DDR_1_REG

.rept   DDR_1_REG              /* DDR based on size in hdf*/
.8byte  SECT + Memory
.set    SECT, SECT+0x40000000
.endr

.rept   UNDEF_1_REG           /* reserved for region where ddr is absent */
.8byte  SECT + reserved
.set    SECT, SECT+0x40000000
.endr

.rept   0x1C0                  /* 0x0010_0000_0000 - 0x007F_FFFF_FFFF */
.8byte  SECT + Device          /* 448 GB PL */
.set    SECT, SECT + 0x40000000
.endr


.rept   0x100                  /* 0x0080_0000_0000 - 0x00BF_FFFF_FFFF */
.8byte  SECT + Device          /* 256GB PCIe */
.set    SECT, SECT + 0x40000000
.endr


.rept   0x100                  /* 0x00C0_0000_0000 - 0x00FF_FFFF_FFFF */
.8byte  SECT + reserved        /* 256GB reserved */
.set    SECT, SECT + 0x40000000
.endr


.section .mmu_tbl2,"a"

MMUTableL2:

.set SECT, 0

#ifdef XPAR_PSU_DDR_0_S_AXI_BASEADDR
.set DDR_0_START, XPAR_PSU_DDR_0_S_AXI_BASEADDR
.set DDR_0_END, XPAR_PSU_DDR_0_S_AXI_HIGHADDR
.set DDR_0_SIZE, (DDR_0_END - DDR_0_START)+1
#if defined(DDR_0_SIZE) && DDR_0_SIZE > 0x80000000
/* If DDR size is larger than 2GB, truncate to 2GB */
.set DDR_0_REG, 0x400
#else
.set DDR_0_REG, DDR_0_SIZE/0x200000
#endif
#else
.set DDR_0_REG, 0
#endif

.set UNDEF_0_REG, 0x400 - DDR_0_REG

.rept   DDR_0_REG              /* DDR based on size in hdf*/
.8byte  SECT + Memory
.set    SECT, SECT+0x200000
.endr

.rept   UNDEF_0_REG            /* reserved for region where ddr is absent */
.8byte  SECT + reserved
.set    SECT, SECT+0x200000
.endr

.rept   0x0200                 /* 0x8000_0000 - 0xBFFF_FFFF */
.8byte  SECT + Device          /* 1GB lower PL */
.set    SECT, SECT+0x200000
.endr

.rept   0x0100                 /* 0xC000_0000 - 0xDFFF_FFFF */
.8byte  SECT + Device          /* 512MB QSPI */
.set    SECT, SECT+0x200000
.endr

.rept   0x080                  /* 0xE000_0000 - 0xEFFF_FFFF */
.8byte  SECT + Device          /* 256MB lower PCIe */
.set    SECT, SECT+0x200000
.endr

.rept   0x040                  /* 0xF000_0000 - 0xF7FF_FFFF */
.8byte  SECT + reserved        /* 128MB Reserved */
.set    SECT, SECT+0x200000
.endr

.rept   0x8                    /* 0xF800_0000 - 0xF8FF_FFFF */
.8byte  SECT + Device          /* 16MB coresight */
.set    SECT, SECT+0x200000
.endr

/* 1MB RPU LLP is marked for 2MB region as the minimum block size in
translation table is 2MB and adjacent 63MB reserved region is
converted to 62MB */

.rept   0x1                    /* 0xF900_0000 - 0xF91F_FFFF */
.8byte  SECT + Device          /* 2MB RPU low latency port */
.set    SECT, SECT+0x200000
.endr

.rept   0x1F                   /* 0xF920_0000 - 0xFCFF_FFFF */
.8byte  SECT + reserved        /* 62MB Reserved */
.set    SECT, SECT+0x200000
.endr

.rept   0x8                    /* 0xFD00_0000 - 0xFDFF_FFFF */
.8byte  SECT + Device          /* 16MB FPS */
.set    SECT, SECT+0x200000
.endr

.rept   0xE                    /* 0xFE00_0000 -  0xFFBF_FFFF */
.8byte  SECT + Device          /* 28MB LPS */
.set    SECT, SECT+0x200000
.endr

/* 0xFFC0_0000 - 0xFFDF_FFFF */
.8byte  SECT + Device          /*2MB PMU/CSU */

.set    SECT, SECT+0x200000    /* 0xFFE0_0000 - 0xFFFF_FFFF*/
.8byte  SECT + Memory          /* 2MB OCM/TCM */


/*
 * FPUContextSize is the size of the array where floating point registers are
 * stored when required. The default size corresponds to the case when there is no
 * nested interrupt. If there are nested interrupts in application which are using
 * floating point operation, the size of FPUContextSize need to be increased as per
 * requirement
 */

.set FPUContextSize, 528

.macro saveregister
    stp    X0,X1, [sp,#-0x10]!
    stp    X2,X3, [sp,#-0x10]!
    stp    X4,X5, [sp,#-0x10]!
    stp    X6,X7, [sp,#-0x10]!
    stp    X8,X9, [sp,#-0x10]!
    stp    X10,X11, [sp,#-0x10]!
    stp    X12,X13, [sp,#-0x10]!
    stp    X14,X15, [sp,#-0x10]!
    stp    X16,X17, [sp,#-0x10]!
    stp    X18,X19, [sp,#-0x10]!
    stp    X29,X30, [sp,#-0x10]!
.endm

.macro restoreregister
    ldp    X29,X30, [sp], #0x10
    ldp    X18,X19, [sp], #0x10
    ldp    X16,X17, [sp], #0x10
    ldp    X14,X15, [sp], #0x10
    ldp    X12,X13, [sp], #0x10
    ldp    X10,X11, [sp], #0x10
    ldp    X8,X9, [sp], #0x10
    ldp    X6,X7, [sp], #0x10
    ldp    X4,X5, [sp], #0x10
    ldp    X2,X3, [sp], #0x10
    ldp    X0,X1, [sp], #0x10
.endm

.macro savefloatregister

/* Load the floating point context array address from FPUContextBase */
    ldr    x1,=FPUContextBase
    ldr    x0, [x1]

/* Save all the floating point register to the array */
    stp    q0,q1, [x0], #0x20
    stp    q2,q3, [x0], #0x20
    stp    q4,q5, [x0], #0x20
    stp    q6,q7, [x0], #0x20
    stp    q8,q9, [x0], #0x20
    stp    q10,q11, [x0], #0x20
    stp    q12,q13, [x0], #0x20
    stp    q14,q15, [x0], #0x20
    stp    q16,q17, [x0], #0x20
    stp    q18,q19, [x0], #0x20
    stp    q20,q21, [x0], #0x20
    stp    q22,q23, [x0], #0x20
    stp    q24,q25, [x0], #0x20
    stp    q26,q27, [x0], #0x20
    stp    q28,q29, [x0], #0x20
    stp    q30,q31, [x0], #0x20
    mrs    x2, FPCR
    mrs    x3, FPSR
    stp    x2, x3, [x0], #0x10

/* Save current address of floating point context array to FPUContextBase */
    str    x0, [x1]
.endm

.macro restorefloatregister

/* Restore the address of floating point context array from FPUContextBase */
    ldr    x1,=FPUContextBase
    ldr    x0, [x1]

/* Restore all the floating point register from the array */
    ldp    x2, x3, [x0,#-0x10]!
    msr    FPCR, x2
    msr    FPSR, x3
    ldp    q30,q31, [x0,#-0x20]!
    ldp    q28,q29, [x0,#-0x20]!
    ldp    q26,q27, [x0,#-0x20]!
    ldp    q24,q25, [x0,#-0x20]!
    ldp    q22,q23, [x0,#-0x20]!
    ldp    q20,q21, [x0,#-0x20]!
    ldp    q18,q19, [x0,#-0x20]!
    ldp    q16,q17, [x0,#-0x20]!
    ldp    q14,q15, [x0,#-0x20]!
    ldp    q12,q13, [x0,#-0x20]!
    ldp    q10,q11, [x0,#-0x20]!
    ldp    q8,q9, [x0,#-0x20]!
    ldp    q6,q7, [x0,#-0x20]!
    ldp    q4,q5, [x0,#-0x20]!
    ldp    q2,q3, [x0,#-0x20]!
    ldp    q0,q1, [x0,#-0x20]!

/* Save current address of floating point context array to FPUContextBase */
    str    x0, [x1]
.endm

.macro exception_return
    eret
#ifdef TARGET_versal
    dsb    nsh
    isb
#endif
.endm


.section .vectors, "a"

_vector_table:
.set VBAR, _vector_table
.org VBAR

/*
 * if application is built for XEN GUEST as EL1 Non-secure following image
 * header is required by XEN.
 */
#if defined(HYP_GUEST) && HYP_GUEST == 1
    /* Valid Image header */
    /* HW reset vector  */
    ldr    x16, =_boot
    br     x16
#ifdef TARGET_versal
    dsb    nsh
    isb
#endif
    /* text offset.  */
    .dword 0
    /* image size.  */
    .dword 0
    /* flags.  */
    .dword 8
    /* RES0  */
    .dword 0
    .dword 0
    .dword 0

    /* magic  */
    .dword 0x644d5241
    /* RES0  */
    .dword 0
    /* End of Image header.  */
#endif
    b      _boot

.org (VBAR + 0x200)
    b      SynchronousInterruptHandler

.org (VBAR + 0x280)
    b      IRQInterruptHandler

.org (VBAR + 0x300)
    b      FIQInterruptHandler

.org (VBAR + 0x380)
    b      SErrorInterruptHandler


SynchronousInterruptHandler:
    saveregister

/* Check if the Synchronous abort is occurred due to floating point access. */
#if defined(EL3_SECURE) && EL3_SECURE == 1
    mrs    x0, ESR_EL3
#else
    mrs    x0, ESR_EL1
#endif
    and    x0, x0, #(0x3F << 26)
    mov    x1, #(0x7 << 26)
    cmp    x0, x1
/* If exception is not due to floating point access go to synchronous handler */
    bne    synchronoushandler

/*
 * If excpetion occurred due to floating point access, Enable the floating point
 * access i.e. do not trap floating point instruction
 */
 #if defined(EL3_SECURE) && EL3_SECURE == 1
    mrs    x1,CPTR_EL3
    bic    x1, x1, #(0x1<<10)
    msr    CPTR_EL3, x1
#else
    mrs    x1,CPACR_EL1
    orr    x1, x1, #(0x1<<20)
    msr    CPACR_EL1, x1
#endif
    isb

/* If the floating point access was previously enabled, store FPU context
 * registers(storefloat).
 */
    ldr    x0, =FPUStatus
    ldrb   w1,[x0]
    cbnz   w1, storefloat
/*
 * If the floating point access was not enabled previously, save the status of
 * floating point accessibility i.e. enabled and store floating point context
 * array address(FPUContext) to FPUContextBase.
 */
    mov    w1, #0x1
    strb   w1, [x0]
    ldr    x0, =FPUContext
    ldr    x1, =FPUContextBase
    str    x0,[x1]
    b      restorecontext
storefloat:
    savefloatregister
    b      restorecontext
synchronoushandler:
    bl      SynchronousInterrupt
restorecontext:
    restoreregister
    exception_return

IRQInterruptHandler:

    saveregister
/* Save the status of SPSR, ELR and CPTR to stack */
#if defined(EL3_SECURE) && EL3_SECURE == 1
    mrs    x0, CPTR_EL3
    mrs    x1, ELR_EL3
    mrs    x2, SPSR_EL3
#else
    mrs    x0, CPACR_EL1
    mrs    x1, ELR_EL1
    mrs    x2, SPSR_EL1
#endif
    stp    x0, x1, [sp,#-0x10]!
    str    x2, [sp,#-0x10]!

/* Trap floating point access */
#if defined(EL3_SECURE) && EL3_SECURE == 1
    mrs    x1,CPTR_EL3
    orr    x1, x1, #(0x1<<10)
    msr    CPTR_EL3, x1
#else
    mrs    x1,CPACR_EL1
    bic    x1, x1, #(0x1<<20)
    msr    CPACR_EL1, x1
#endif
    isb

    bl     IRQInterrupt
/*
 * If floating point access is enabled during interrupt handling,
 * restore floating point registers.
 */

#if defined(EL3_SECURE) && EL3_SECURE == 1
    mrs    x0, CPTR_EL3
    ands   x0, x0, #(0x1<<10)
    bne    RestorePrevState
#else
    mrs    x0,CPACR_EL1
    ands   x0, x0, #(0x1<<20)
    beq    RestorePrevState
#endif

    restorefloatregister

/* Restore the status of SPSR, ELR and CPTR from stack */
RestorePrevState:
    ldr    x2,[sp],0x10
    ldp    x0, x1, [sp],0x10
#if defined(EL3_SECURE) && EL3_SECURE == 1
    msr    CPTR_EL3, x0
    msr    ELR_EL3, x1
    msr    SPSR_EL3, x2
#else
    msr    CPACR_EL1, x0
    msr    ELR_EL1, x1
    msr    SPSR_EL1, x2
#endif
    restoreregister
    exception_return

FIQInterruptHandler:

    saveregister
/* Save the status of SPSR, ELR and CPTR to stack */
#if defined(EL3_SECURE) && EL3_SECURE == 1
    mrs    x0, CPTR_EL3
    mrs    x1, ELR_EL3
    mrs    x2, SPSR_EL3
#else
    mrs    x0, CPACR_EL1
    mrs    x1, ELR_EL1
    mrs    x2, SPSR_EL1
#endif
    stp    x0, x1, [sp,#-0x10]!
    str    x2, [sp,#-0x10]!

/* Trap floating point access */
#if defined(EL3_SECURE) && EL3_SECURE == 1
    mrs    x1,CPTR_EL3
    orr    x1, x1, #(0x1<<10)
    msr    CPTR_EL3, x1
#else
    mrs    x1,CPACR_EL1
    bic    x1, x1, #(0x1<<20)
    msr    CPACR_EL1, x1
#endif
    isb
    bl     FIQInterrupt
/*
 * If floating point access is enabled during interrupt handling,
 * restore floating point registers.
 */

#if defined(EL3_SECURE) && EL3_SECURE == 1
    mrs    x0, CPTR_EL3
    ands   x0, x0, #(0x1<<10)
    bne    RestorePrevStatefiq
#else
    mrs    x0,CPACR_EL1
    ands   x0, x0, #(0x1<<20)
    beq    RestorePrevStatefiq
#endif

    restorefloatregister

    /* Restore the status of SPSR, ELR and CPTR from stack */
RestorePrevStatefiq:
    ldr    x2,[sp],0x10
    ldp    x0, x1, [sp],0x10
 #ifdef EL3_SECURE
    msr    CPTR_EL3, x0
    msr    ELR_EL3, x1
    msr    SPSR_EL3, x2
#else
    msr    CPACR_EL1, x0
    msr    ELR_EL1, x1
    msr    SPSR_EL1, x2
#endif
    restoreregister
    exception_return

SErrorInterruptHandler:

    saveregister
    bl     SErrorInterrupt
    restoreregister
    exception_return


.align 8
/* Array to store floating point registers */
FPUContext:
    .skip FPUContextSize
/* Stores address for floating point context array */
FPUContextBase:
    .skip 8
FPUStatus:
    .skip 1

.align 8
#elif defined(USE_SIMPLE_STARTUP)
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
#if 0
.section ".boot"
.global _vector_table
_vector_table:
    bl boot_entry_C   // boot_entry_C never returns
#endif
#endif /* !USE_BUILTIN_STARTUP */


/*
 * void flush_dcache_range(start, end)
 *
 * clean & invalidate data cache in the range
 *
 * x0: start address
 * x1: end address
 */
.global flush_dcache_range
flush_dcache_range:
    mrs    x3, ctr_el0
    lsr    x3, x3, #16
    and    x3, x3, #0xf
    mov    x2, #4
    lsl    x2, x2, x3  /* cache line size */

    /* x2 <- minimal cache line size in cache system */
    sub    x3, x2, #1
    bic    x0, x0, x3
1:  dc     civac, x0 /* clean & invalidate data or unified cache */
    add    x0, x0, x2
    cmp    x0, x1
    b.lo   1b
    dsb    sy
    ret


/* Initialize GIC 400 (GICv2) */
.global gicv2_init_secure
gicv2_init_secure:
    ldr    x0, =GICD_BASE
    mov    w9, #0x3               /* EnableGrp0 | EnableGrp1 */
    str    w9, [x0, GICD_CTLR]    /* Secure GICD_CTLR */
    ldr    w9, [x0, GICD_TYPER]
    and    w10, w9, #0x1f         /* ITLinesNumber */
    cbz    w10, 1f                /* No SPIs */
    add    x11, x0, GICD_IGROUPRn
    mov    w9, #~0                /* Config SPIs as Grp1 */
    str    w9, [x11], #0x4
0:  str    w9, [x11], #0x4
    sub    w10, w10, #0x1
    cbnz   w10, 0b

    ldr    x1, =GICC_BASE         /* GICC_CTLR */
    mov    w0, #3                 /* EnableGrp0 | EnableGrp1 */
    str    w0, [x1]

    mov    w0, #1 << 7            /* Allow NS access to GICC_PMR */
    str    w0, [x1, #4]           /* GICC_PMR */
1:
    ret

.end
