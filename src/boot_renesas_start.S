/* boot_renesas_start.S
 *
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

/* Reference startup code based on from Renesas RX BSP start.c */

    .list
    .section .text

    .extern _data
    .extern _mdata
    .extern _ebss
    .extern _bss
    .extern _edata
    .extern _ustack
    .extern _istack
    .extern _rvectors
#ifdef NO_LEADING_UNDERSCORE
    .extern main
#else
    .extern _main
#endif

    .global _PowerON_Reset         /* global Start routine */
    .type   _PowerON_Reset, @function
_PowerON_Reset :
/* initialise user stack pointer */
    mvtc    #_ustack,USP

/* initialise interrupt stack pointer */
    mvtc    #_istack,ISP

#ifdef __RXv2__
/* setup exception vector */
    mvtc   #_ExceptVectors, extb   /* EXCEPTION VECTOR ADDRESS    */
#endif
/* setup intb */
    mvtc   #_rvectors_start, intb  /* INTERRUPT VECTOR ADDRESS  definition    */

/* setup FPSW */
    mvtc   #100h, fpsw

/* load data section from ROM to RAM */
    mov    #_mdata, r2     /* src ROM address of data section in R2 */
    mov    #_data, r1      /* dest start RAM address of data section in R1 */
    mov    #_edata, r3     /* end RAM address of data section in R3 */
    sub    r1, r3          /* size of data section in R3 (R3=R3-R1) */
#ifdef __RX_ALLOW_STRING_INSNS__
    smovf                  /* block copy R3 bytes from R2 to R1 */
#else
    cmp    #0, r3
    beq    2f

1:  mov.b  [r2+], r5
    mov.b  r5, [r1+]
    sub    #1, r3
    bne    1b
2:
#endif

/* bss initialisation : zero out bss */
    mov    #00h,r2         /* load R2 reg with zero */
    mov    #_ebss, r3      /* store the end address of bss in R3 */
    mov    #_bss, r1       /* store the start address of bss in R1 */
#ifdef __RX_ALLOW_STRING_INSNS__
    sub    r1, r3          /* size of bss section in R3 (R3=R3-R1) */
    sstr.b
#else
1:  cmp    r1, r3
    beq    2f
    mov.b  r2, [r1+]
    bra    1b
2:
#endif

#ifdef RX_PSW_USER_MODE
/* check and skip PSW setup if already in "user mode" */
    mvfc   psw, r1
    btst   #20, r1 /* Processor Mode: 0=Supervisor, 1=User */
    bne    skip_psw_setup

/* change PSW Processor Mode (PM) to user-mode */
    mvtc   #10000h, psw   /* Set Interupt Enable bit */
    or     #00100000h, r1 /* Set PM = 1 (user mode) */
    push.l r1
    mvfc   pc, r1
    add    #10, r1
    push.l r1
    rte
    nop
    nop
skip_psw_setup:
#endif

/* start user program */
#ifdef NO_LEADING_UNDERSCORE
    mov    #main, r7
#else
    mov    #_main, r7
#endif
    jsr    r7
    mov    #_exit, r7
    jsr    r7


    .global __rx_fini
    .type   __rx_fini, @function
__rx_fini:
    rts

    .global _exit
    .type   _exit, @function
/* call to exit */
_exit:
    mov    #0, r2
#ifndef NO_LEADING_UNDERSCORE
    mov    #___call_exitprocs, r7
#endif
    jsr    r7
_loop_here:
    bra    _loop_here

    .text
    .end
