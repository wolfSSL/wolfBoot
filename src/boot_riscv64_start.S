/**
 * RISC-V 64-bit bootup
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

#ifdef TARGET_mpfs250
#include "hal/mpfs250.h"
#endif

.section .init
.globl _reset
_reset:
#ifdef WOLFBOOT_RISCV_MMODE
    /* get hart id */
    csrr  a0, CSR_MHARTID
#endif

   /*
    * Save hart id and dtb pointer. The thread pointer register is not
    * modified by C code. It is used by secondary_hart_loop.
    */
    mv    tp, a0
    mv    s1, a1

    /*
     * Set the global data pointer to a known value in case we get a very
     * early trap. The global data pointer will be set its actual value only
     * after it has been initialized.
     */
    mv    gp, zero

    /* Setup trap handler */
    la    t0, IV
    csrw  MODE_PREFIX(tvec), t0          # initialize machine trap vector address

    /*
     * Mask all interrupts. Interrupts are disabled globally (in m/sstatus)
     * for U-Boot, but we will need to read m/sip to determine if we get an
     * IPI
     */
    csrw  MODE_PREFIX(ie), zero

    /* set xSIE bit to receive IPIs */
#ifdef WOLFBOOT_RISCV_SMODE
    li    t0, SIE_SSIE
#else
    li    t0, MIE_MSIE
#endif
    csrs  MODE_PREFIX(ie), t0

    /* Set stackpointer */
    li    t0, -16
    li    t1, SYS_INIT_SP_ADDR
    and   sp, t1, t0        /* force 16 byte alignment */
    mv    s0, sp

    mv    gp, s0

    # pass HLS address
    mv    a0, tp
    j     main
