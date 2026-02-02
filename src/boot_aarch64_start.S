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

#ifdef TARGET_versal
#include "hal/versal.h"
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

#ifdef TARGET_versal
/* Versal: RVBAR is handled by PLM, not accessible from APU in JTAG mode */
/* Cortex-A72 timestamp clock frequency (from Versal HW) */
.set counterfreq, 100000000
#else
.set rvbar_base,  0xFD5C0040
/* Cortex-A53 timestamp clock frequency */
.set counterfreq, 99990005
#endif

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
    /* Note: On Versal, RVBAR is handled by PLM and APU_DUAL_CSR is not
     * accessible in JTAG boot mode. Skip RVBAR write for Versal. */
#if !defined(SKIP_RVBAR) || SKIP_RVBAR == 0
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
#endif

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

    /* Configure CPUACTLR_EL1 - CPU auxiliary control register
     * Read-modify-write to preserve any pre-configured bits and add prefetch settings.
     * Note: On Versal, BL31 will later apply additional errata workarounds after EL3 init:
     *       859971, 1319367, CVE-2017-5715, CVE-2018-3639, CVE-2022-23960 */
    mrs    x0, S3_1_C15_C2_0      /* Read current CPUACTLR_EL1 */
    ldr    x1,=0x80CA000          /* L1 Data prefetch control - 5, Enable device split throttle, 2 independent data prefetch streams */
    orr    x0, x0, x1              /* Merge with existing value */
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

#ifndef NO_MMU
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
    * Granual Size TG0 = 00 -> 4KB
    ***************************************************/
#ifdef TARGET_versal
    /* Versal: Physical Address Size PS = 100 -> 44bits 16TB
     *         T0SZ = 20 -> (region size 2^(64-20) = 2^44) */
    ldr    x1,=0x80843514
#else
    /* ZynqMP: Physical Address Size PS = 010 -> 40bits 1TB
     *         T0SZ = 24 -> (region size 2^(64-24) = 2^40) */
    ldr    x1,=0x80823518
#endif

    msr    TCR_EL3, x1
    isb
#endif /* !NO_MMU */

    /* Enable SError Exception for asynchronous abort */
    mrs    x1,DAIF
    bic    x1,x1,#(0x1<<8)
    msr    DAIF,x1

    /* Configure SCTLR_EL3 */
    mov    x1, #0                 /* Most of the SCTLR_EL3 bits are unknown at reset */
#ifndef NO_MMU
    orr    x1, x1, #(1 << 12)     /* Enable I cache */
    orr    x1, x1, #(1 << 3)      /* Enable SP alignment check */
    orr    x1, x1, #(1 << 2)      /* Enable caches */
    orr    x1, x1, #(1 << 0)      /* Enable MMU */
#else
    orr    x1, x1, #(1 << 3)      /* Enable SP alignment check */
#endif
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

#ifndef NO_MMU
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
    * Granual Size TG0 = 00 -> 4KB
    ***************************************************/
#ifdef TARGET_versal
    /* Versal: Physical Address Size PS = 100 -> 44bits 16TB
     *         T0SZ = 20 -> (region size 2^(64-20) = 2^44) */
    ldr    x1,=0x80843514
#else
    /* ZynqMP: Physical Address Size PS = 010 -> 40bits 1TB
     *         T0SZ = 24 -> (region size 2^(64-24) = 2^40) */
    ldr    x1,=0x80823518
#endif

    msr    TCR_EL2, x1
    isb

    /* Configure SCTLR_EL2 */
    mrs    x1, SCTLR_EL2
    orr    x1, x1, #(1 << 12)     /* Enable ICache */
    orr    x1, x1, #(1 << 3)      /* Enable SP alignment check */
    orr    x1, x1, #(1 << 2)      /* Enable DCaches */
    orr    x1, x1, #(1 << 0)      /* Enable MMU */
#else
    /* Configure SCTLR_EL2 - no MMU/cache */
    mov    x1, #0
    orr    x1, x1, #(1 << 3)      /* Enable SP alignment check */
#endif
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

#ifndef NO_MMU
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
    * Granual Size TG0 = 00 -> 4KB
    ***************************************************/
#ifdef TARGET_versal
    /* Versal: Physical Address Size PS = 100 -> 44bits 16TB
     *         T0SZ = 20 -> (region size 2^(64-20) = 2^44) */
    ldr    x1,=0x485800514
#else
    /* ZynqMP: Physical Address Size PS = 010 -> 40bits 1TB
     *         T0SZ = 24 -> (region size 2^(64-24) = 2^40) */
    ldr    x1,=0x285800518
#endif

    msr    TCR_EL1, x1
    isb
#endif /* !NO_MMU */

    /* Enable SError Exception for asynchronous abort */
    mrs    x1,DAIF
    bic    x1,x1,#(0x1<<8)
    msr    DAIF,x1

    /* Configure SCTLR_EL1 */
    mov    x1,#0x0
    orr    x1, x1, #(1 << 18)     /* Set WFE non trapping */
    orr    x1, x1, #(1 << 17)     /* Set WFI non trapping */
    orr    x1, x1, #(1 << 5)      /* Set CP15 barrier enabled */
#ifndef NO_MMU
    orr    x1, x1, #(1 << 12)     /* Set I bit (ICache) */
    orr    x1, x1, #(1 << 2)      /* Set C bit (DCache) */
    orr    x1, x1, #(1 << 0)      /* Set M bit (MMU) */
#endif
    msr    SCTLR_EL1, x1
    isb

    bl     boot_entry_C           /* jump to start */
#else
    /* present exception level and selected exception level mismatch */
    b      error
#endif

/* Assembly startup error handler */
error:
    wfi
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

#ifdef TARGET_versal
/* Versal: LPD/PMC peripherals at 0xF0000000 - 0xF7FFFFFF (includes QSPI @ 0xF1030000) */
.rept   0x040                  /* 0xF000_0000 - 0xF7FF_FFFF */
.8byte  SECT + Device          /* 128MB LPD peripherals (QSPI, I2C, etc) */
.set    SECT, SECT+0x200000
.endr
#else
/* ZynqMP: This region is reserved */
.rept   0x040                  /* 0xF000_0000 - 0xF7FF_FFFF */
.8byte  SECT + reserved        /* 128MB Reserved */
.set    SECT, SECT+0x200000
.endr
#endif

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
#elif defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
    mrs    x0, ESR_EL2
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
#elif defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
    mrs    x1,CPTR_EL2
    orr    x1, x1, #(0x3 << 20)   /* Clear TFP bit to enable FP/SIMD */
    msr    CPTR_EL2, x1
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
#elif defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
    mrs    x0, CPTR_EL2
    mrs    x1, ELR_EL2
    mrs    x2, SPSR_EL2
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
#elif defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
    mrs    x1,CPTR_EL2
    bic    x1, x1, #(0x3<<20)    /* Set TFP bits to trap FP/SIMD */
    msr    CPTR_EL2, x1
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
#elif defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
    mrs    x0, CPTR_EL2
    ands   x0, x0, #(0x3<<20)
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
#elif defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
    msr    CPTR_EL2, x0
    msr    ELR_EL2, x1
    msr    SPSR_EL2, x2
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
#elif defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
    mrs    x0, CPTR_EL2
    mrs    x1, ELR_EL2
    mrs    x2, SPSR_EL2
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
#elif defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
    mrs    x1,CPTR_EL2
    bic    x1, x1, #(0x3<<20)
    msr    CPTR_EL2, x1
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
#elif defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
    mrs    x0, CPTR_EL2
    ands   x0, x0, #(0x3<<20)
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
#if defined(EL3_SECURE) && EL3_SECURE == 1
    msr    CPTR_EL3, x0
    msr    ELR_EL3, x1
    msr    SPSR_EL3, x2
#elif defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
    msr    CPTR_EL2, x0
    msr    ELR_EL2, x1
    msr    SPSR_EL2, x2
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

#if defined(BOOT_EL1) && defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
/*
 * Transition from EL2 to EL1 and jump to application
 *
 * Parameters:
 *   x0: entry_point - Address to jump to in EL1
 *   x1: dts_addr - Device tree address (passed in x0 to application)
 *
 * This function configures the necessary system registers for EL1 operation
 * and performs an exception return (ERET) to drop from EL2 to EL1.
 *
 * Based on ARM Architecture Reference Manual and U-Boot implementation.
 */
.global el2_to_el1_boot
el2_to_el1_boot:
    /* Save parameters - x0=entry_point, x1=dts_addr */
    mov    x19, x0                /* Save entry_point in x19 */
    mov    x20, x1                /* Save dts_addr in x20 */

    /* 1. Configure timer access for EL1 */
    mrs    x0, CNTHCTL_EL2
    orr    x0, x0, #3             /* EL1PCEN | EL1PCTEN - enable EL1 timer access */
    msr    CNTHCTL_EL2, x0
    msr    CNTVOFF_EL2, xzr       /* Clear virtual timer offset */

    /* 2. Configure virtual processor ID */
    mrs    x0, MIDR_EL1
    msr    VPIDR_EL2, x0
    mrs    x0, MPIDR_EL1
    msr    VMPIDR_EL2, x0

    /* 3. Disable coprocessor traps to EL2 */
    mov    x0, #0x33ff            /* CPTR_EL2: RES1 bits, no traps */
    msr    CPTR_EL2, x0
    msr    HSTR_EL2, xzr          /* No traps to EL2 on system registers */
    mov    x0, #(3 << 20)         /* CPACR_EL1: Full FP/SIMD access */
    msr    CPACR_EL1, x0

    /* 4. Initialize SCTLR_EL1 with safe defaults (RES1 bits, MMU/cache off) */
    /* RES1 bits: 29,28,23,22,20,11 = 0x30d00800 */
    movz   x0, #0x800
    movk   x0, #0x30d0, lsl #16
    msr    SCTLR_EL1, x0

    /* 5. Migrate stack pointer and vector base to EL1 */
    /* SP_EL1 must be 16-byte aligned per ARM spec */
    mov    x0, sp
    bic    x0, x0, #0xF            /* Ensure 16-byte alignment */
    msr    SP_EL1, x0
    mrs    x0, VBAR_EL2
    msr    VBAR_EL1, x0
    dsb    sy                     /* Ensure SP_EL1 and VBAR_EL1 writes complete */
    isb                            /* Ensure writes take effect */

    /* 6. Configure HCR_EL2 - EL1 is AArch64, no hypervisor calls */
    /* Check if PAuth (Pointer Authentication) is supported */
    mrs    x0, ID_AA64ISAR1_EL1   /* Read ISA feature register */
    mov    x1, #(0xF << 28)       /* GPI mask */
    orr    x1, x1, #(0xF << 24)   /* GPA mask */
    orr    x1, x1, #(0xF << 8)    /* API mask */
    orr    x1, x1, #(0xF << 4)    /* APA mask */
    tst    x0, x1                 /* Test if PAuth supported (Z=1 if not supported) */
    mov    x0, #(1 << 31)         /* RW: EL1 is AArch64 */
    orr    x0, x0, #(1 << 29)     /* HCD: Disable HVC instruction */
    mov    x1, x0                 /* Copy base value */
    orr    x1, x1, #(1 << 41)     /* API: Trap PAuth instructions */
    orr    x1, x1, #(1 << 40)     /* APK: Trap PAuth key access */
    csel   x0, x0, x1, eq         /* If PAuth not supported (eq), use x0 (base), else x1 (with traps) */
    msr    HCR_EL2, x0
    dsb    sy                     /* Ensure HCR_EL2 write completes */
    isb                            /* Ensure HCR_EL2 takes effect */

    /* 7. Set up SPSR_EL2 for return to EL1h with all interrupts masked */
    /* M[3:0] = 0101 = EL1h (EL1 with SP_EL1) - NOT 0100 which is EL1t! */
    /* M[4] = 0 = AArch64 mode (bit 4 must be 0 for AArch64, 1 for AArch32) */
    /* DAIF = 0xF = all interrupts masked */
    /* Value: 0x3C5 = (0xF << 6) | 0x5 */
    movz   x0, #0x3C5             /* DAIF=0xF (bits 9:6), M[3:0]=0x5 (EL1h) */
    msr    SPSR_EL2, x0
    dsb    sy                     /* Ensure SPSR_EL2 write completes */
    isb                            /* Ensure SPSR_EL2 takes effect */

    /* 8. Set exception return address and DTB pointer, then ERET */
    /* Critical: All register writes must complete before eret */
    msr    ELR_EL2, x19           /* Entry point in ELR_EL2 */
    mov    x0, x20                 /* DTB address in x0 (first arg) */
    mov    x1, xzr                 /* Zero remaining argument registers */
    mov    x2, xzr
    mov    x3, xzr
    dsb    sy                     /* Ensure all writes complete */
    isb                            /* Ensure all effects are visible */
    eret                           /* Exception return to EL1 */

    /* Should never reach here */
    b      .
#endif /* BOOT_EL1 && EL2_HYPERVISOR */

.end
