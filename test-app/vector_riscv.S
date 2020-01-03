/**
 * RISC-V bootup
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
 *
 */

.macro trap_entry
    addi sp, sp, -64
    sw x1,  0(sp)
    sw x5,  4(sp)
    sw x6,  8(sp)
    sw x7,  12(sp)
    sw x10, 16(sp)
    sw x11, 20(sp)
    sw x12, 24(sp)
    sw x13, 28(sp)
    sw x14, 32(sp)
    sw x15, 36(sp)
    sw x16, 40(sp)
    sw x17, 44(sp)
    sw x28, 48(sp)
    sw x29, 52(sp)
    sw x30, 56(sp)
    sw x31, 60(sp)
.endm


.macro trap_exit
  lw x1,  0(sp)
  lw x5,  4(sp)
  lw x6,  8(sp)
  lw x7,  12(sp)
  lw x10, 16(sp)
  lw x11, 20(sp)
  lw x12, 24(sp)
  lw x13, 28(sp)
  lw x14, 32(sp)
  lw x15, 36(sp)
  lw x16, 40(sp)
  lw x17, 44(sp)
  lw x28, 48(sp)
  lw x29, 52(sp)
  lw x30, 56(sp)
  lw x31, 60(sp)
  addi sp, sp, 64
  mret
.endm

.section .isr_vector
.align 8

.global IV

IV:
    j _synctrap
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2
    j trap_empty
    .align 2

_synctrap:
  trap_entry
  jal isr_synctrap
  trap_exit

trap_empty:
    nop
