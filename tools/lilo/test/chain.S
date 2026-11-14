/* chain.S
 *
 * Partition boot sector for the x86_64_lilo QEMU test: checks what
 * wolfBoot hands a chain-loaded sector (DL = boot drive, DS:SI = its
 * partition entry), reports on COM1 and exits QEMU via isa-debug-exit.
 *
 * Copyright (C) 2026 wolfSSL Inc.
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

#define PART_TYPE 0xDA

    .code16
    .globl _start
_start:
    cmpb    $0x80, %dl
    jne     bad
    cmpb    $PART_TYPE, 4(%si)
    jne     bad
    cmpb    $0x80, (%si)
    jne     bad
    movw    $ok_msg, %si
    jmp     print
bad:
    movw    $bad_msg, %si
print:
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    $0x3F8, %dx
1:
    lodsb
    testb   %al, %al
    jz      2f
    outb    %al, %dx
    jmp     1b
2:
    movw    $0xF4, %dx
    movb    $0x10, %al
    outb    %al, %dx
3:
    hlt
    jmp     3b

ok_msg:
    .asciz  "\r\nWOLFBOOT-LILO-TEST: CHAIN-OK\r\n"
bad_msg:
    .asciz  "\r\nWOLFBOOT-LILO-TEST: CHAIN-BAD\r\n"

    .org    510
    .byte   0x55, 0xAA
