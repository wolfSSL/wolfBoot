/* boot_arm64_start.S
 *
 * AArch64 (64-bit ARM) boot startup code for test applications
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

.section .text.startup, "ax"
.global _start
.type _start, @function

_start:
    /* Set up stack pointer */
    ldr x0, =__stack
    mov sp, x0

    /* Copy data section from flash to RAM if needed */
    ldr x0, =_stored_data
    ldr x1, =_start_data
    ldr x2, =_end_data
    cmp x1, x2
    b.ge 2f
    cmp x0, x1
    b.eq 2f  /* Skip if data already in place */
1:
    ldr x3, [x0], #8
    str x3, [x1], #8
    cmp x1, x2
    b.lt 1b
2:

    /* Clear BSS */
    ldr x0, =__bss_start__
    ldr x1, =__bss_end__
    cmp x0, x1
    b.ge 4f
3:
    str xzr, [x0], #8
    cmp x0, x1
    b.lt 3b
4:

    /* Jump to main - never returns */
    bl main

    /* If main returns, loop forever */
5:
    wfi
    b 5b

.size _start, . - _start

/* Provide _exit stub for bare-metal builds (required by some standard library code) */
.section .text, "ax"
.global _exit
.type _exit, @function

_exit:
    /* Loop forever - bare-metal applications don't exit */
6:
    wfi
    b 6b

.size _exit, . - _exit
