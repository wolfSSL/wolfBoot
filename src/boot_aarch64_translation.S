/**
 * Aarch64 bootup
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

#ifndef USE_BUILTIN_STARTUP

/*
* translation_table.S contains a static page table required by MMU for
* cortex-A53. This translation table is flat mapped (input address = output
* address) with default memory attributes defined for zynq ultrascale+
* architecture. It utilizes translation granual size of 4KB with 2MB section
* size for initial 4GB memory and 1GB section size for memory after 4GB.
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
*| Reserved              | 0x00F9200000 - 0x00FCFFFFFF | Unassigned               |
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

/* Code is adapted from the default AMD/Xilinx translation_table.S */

#ifdef TARGET_zynq
#include "hal/zynq.h"
#endif

.globl  MMUTableL0
.globl  MMUTableL1
.globl  MMUTableL2

.set reserved,  0x0                             /* Fault*/
#if EL1_NONSECURE
.set Memory,    0x405 | (2 << 8) | (0x0)        /* normal writeback write allocate outer shared read write */
#else
.set Memory,    0x405 | (3 << 8) | (0x0)        /* normal writeback write allocate inner shared read write */
#endif
.set Device,    0x409 | (1 << 53) | (1 << 54) | (0x0) /* strongly ordered read write non executable*/
.section .mmu_tbl0,"a"

MMUTableL0:

.set SECT, MMUTableL1        /* 0x0000_0000 -  0x7F_FFFF_FFFF */
.8byte    SECT + 0x3
.set SECT, MMUTableL1+0x1000    /* 0x80_0000_0000 - 0xFF_FFFF_FFFF */
.8byte    SECT + 0x3

.section .mmu_tbl1,"a"

MMUTableL1:

.set SECT, MMUTableL2        /* 0x0000_0000 - 0x3FFF_FFFF */
.8byte    SECT + 0x3        /* 1GB DDR */

.rept    0x3            /* 0x4000_0000 - 0xFFFF_FFFF */
.set SECT, SECT + 0x1000    /*1GB DDR, 1GB PL, 2GB other devices n memory */
.8byte    SECT + 0x3
.endr

.set SECT,0x100000000
.rept    0xC            /* 0x0001_0000_0000 - 0x0003_FFFF_FFFF */
.8byte    SECT + reserved        /* 12GB Reserved */
.set SECT, SECT + 0x40000000
.endr

.rept    0x10            /* 0x0004_0000_0000 - 0x0007_FFFF_FFFF */
.8byte    SECT + Device        /* 8GB PL, 8GB PCIe */
.set SECT, SECT + 0x40000000
.endr


#ifdef XPAR_PSU_DDR_1_S_AXI_BASEADDR
.set DDR_1_START, XPAR_PSU_DDR_1_S_AXI_BASEADDR
.set DDR_1_END, XPAR_PSU_DDR_1_S_AXI_HIGHADDR
.set DDR_1_SIZE, (DDR_1_END - DDR_1_START)+1
.if DDR_1_SIZE > 0x800000000
/* If DDR size is larger than 32GB, truncate to 32GB */
.set DDR_1_REG, 0x20
.else
.set DDR_1_REG, DDR_1_SIZE/0x40000000
.endif
#else
.set DDR_1_REG, 0
#warning "There's no DDR_1 in the HW design. MMU translation table marks 32 GB DDR address space as undefined"
#endif

.set UNDEF_1_REG, 0x20 - DDR_1_REG

.rept    DDR_1_REG            /* DDR based on size in hdf*/
.8byte    SECT + Memory
.set    SECT, SECT+0x40000000
.endr

.rept    UNDEF_1_REG        /* reserved for region where ddr is absent */
.8byte    SECT + reserved
.set    SECT, SECT+0x40000000
.endr

.rept    0x1C0            /* 0x0010_0000_0000 - 0x007F_FFFF_FFFF */
.8byte    SECT + Device        /* 448 GB PL */
.set SECT, SECT + 0x40000000
.endr


.rept    0x100            /* 0x0080_0000_0000 - 0x00BF_FFFF_FFFF */
.8byte    SECT + Device        /* 256GB PCIe */
.set SECT, SECT + 0x40000000
.endr


.rept    0x100            /* 0x00C0_0000_0000 - 0x00FF_FFFF_FFFF */
.8byte    SECT + reserved        /* 256GB reserved */
.set SECT, SECT + 0x40000000
.endr


.section .mmu_tbl2,"a"

MMUTableL2:

.set SECT, 0

#ifdef XPAR_PSU_DDR_0_S_AXI_BASEADDR
.set DDR_0_START, XPAR_PSU_DDR_0_S_AXI_BASEADDR
.set DDR_0_END, XPAR_PSU_DDR_0_S_AXI_HIGHADDR
.set DDR_0_SIZE, (DDR_0_END - DDR_0_START)+1
.if DDR_0_SIZE > 0x80000000
/* If DDR size is larger than 2GB, truncate to 2GB */
.set DDR_0_REG, 0x400
.else
.set DDR_0_REG, DDR_0_SIZE/0x200000
.endif
#else
.set DDR_0_REG, 0
#warning "There's no DDR_0 in the HW design. MMU translation table marks 2 GB DDR address space as undefined"
#endif

.set UNDEF_0_REG, 0x400 - DDR_0_REG

.rept    DDR_0_REG            /* DDR based on size in hdf*/
.8byte    SECT + Memory
.set    SECT, SECT+0x200000
.endr

.rept    UNDEF_0_REG        /* reserved for region where ddr is absent */
.8byte    SECT + reserved
.set    SECT, SECT+0x200000
.endr

.rept    0x0200            /* 0x8000_0000 - 0xBFFF_FFFF */
.8byte    SECT + Device        /* 1GB lower PL */
.set    SECT, SECT+0x200000
.endr

.rept    0x0100            /* 0xC000_0000 - 0xDFFF_FFFF */
.8byte    SECT + Device        /* 512MB QSPI */
.set    SECT, SECT+0x200000
.endr

.rept    0x080            /* 0xE000_0000 - 0xEFFF_FFFF */
.8byte    SECT + Device        /* 256MB lower PCIe */
.set    SECT, SECT+0x200000
.endr

.rept    0x040            /* 0xF000_0000 - 0xF7FF_FFFF */
.8byte    SECT + reserved        /* 128MB Reserved */
.set    SECT, SECT+0x200000
.endr

.rept    0x8            /* 0xF800_0000 - 0xF8FF_FFFF */
.8byte    SECT + Device        /* 16MB coresight */
.set    SECT, SECT+0x200000
.endr

/* 1MB RPU LLP is marked for 2MB region as the minimum block size in
translation table is 2MB and adjacent 63MB reserved region is
converted to 62MB */

.rept    0x1            /* 0xF900_0000 - 0xF91F_FFFF */
.8byte    SECT + Device        /* 2MB RPU low latency port */
.set    SECT, SECT+0x200000
.endr

.rept    0x1F            /* 0xF920_0000 - 0xFCFF_FFFF */
.8byte    SECT + reserved        /* 62MB Reserved */
.set    SECT, SECT+0x200000
.endr

.rept    0x8            /* 0xFD00_0000 - 0xFDFF_FFFF */
.8byte    SECT + Device        /* 16MB FPS */
.set    SECT, SECT+0x200000
.endr

.rept    0xE            /* 0xFE00_0000 -  0xFFBF_FFFF */
.8byte    SECT + Device        /* 28MB LPS */
.set    SECT, SECT+0x200000
.endr

            /* 0xFFC0_0000 - 0xFFDF_FFFF */
.8byte    SECT + Device         /*2MB PMU/CSU */

.set    SECT, SECT+0x200000    /* 0xFFE0_0000 - 0xFFFF_FFFF*/
.8byte  SECT + Memory        /*2MB OCM/TCM*/

.end

#endif /* !USE_BUILTIN_STARTUP */
