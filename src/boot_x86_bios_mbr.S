/* boot_x86_bios_mbr.S
 *
 * MBR boot sector for the x86_64_lilo target.
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

/* Loaded by the BIOS at 0x7C00 with the boot drive in DL. Reads
 * desc_stage_sectors sectors from desc_stage_lba (both patched in by
 * tools/lilo) to 0x7E00 through the INT 13h extensions and jumps there.
 *
 * Progress follows LILO: "L" once running, "LI" once the stage is loaded.
 * On failure the BIOS status (or E0: not installed, E1: no LBA support) is
 * printed after the letters and the machine halts. */

#define STAGE_SEG       0x07E0
#define MAX_XFER        64      /* sectors per INT 13h call: 32 KB */

    .code16
    .section .mbr, "ax"
    .globl _mbr_start
_mbr_start:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7C00, %sp
    ljmp    $0, $1f
1:
    sti
    cld
    movb    %dl, boot_drive
    movb    $'L', %al
    call    putc

    movb    $0x41, %ah
    movw    $0x55AA, %bx
    movb    boot_drive, %dl
    int     $0x13
    movb    $0xE1, %ah
    jc      fail
    cmpw    $0xAA55, %bx
    jne     fail
    testb   $1, %cl
    jz      fail

    movl    desc_stage_lba, %eax
    movl    %eax, dap_lba
    movl    desc_stage_lba + 4, %edx
    movl    %edx, dap_lba + 4
    orl     %edx, %eax
    movb    $0xE0, %ah
    jz      fail
    movw    $STAGE_SEG, dap_seg
    movw    desc_stage_sectors, %di

load_loop:
    testw   %di, %di
    jz      loaded
    movw    %di, %ax
    cmpw    $MAX_XFER, %ax
    jbe     2f
    movw    $MAX_XFER, %ax
2:
    movw    %ax, dap_count
    movw    $dap, %si
    movb    boot_drive, %dl
    movb    $0x42, %ah
    int     $0x13
    jc      fail
    movzwl  dap_count, %eax
    subw    %ax, %di
    addl    %eax, dap_lba
    adcl    $0, dap_lba + 4
    shlw    $5, %ax
    addw    %ax, dap_seg
    jmp     load_loop

loaded:
    movb    $'I', %al
    call    putc
    movb    boot_drive, %dl
    ljmp    $0, $_stage_entry

/* AH = error code */
fail:
    movb    %ah, %bl
    movb    $' ', %al
    call    putc
    movb    %bl, %al
    shrb    $4, %al
    call    putnibble
    movb    %bl, %al
    call    putnibble
3:
    hlt
    jmp     3b

putnibble:
    andb    $0x0F, %al
    addb    $'0', %al
    cmpb    $'9', %al
    jbe     putc
    addb    $7, %al
/* Echoed to COM1 as well, so a headless machine shows the same letters. */
putc:
    pushw   %bx
    pushw   %dx
    movw    $0x3F8, %dx
    outb    %al, %dx
    movb    $0x0E, %ah
    movw    $0x0007, %bx
    int     $0x10
    popw    %dx
    popw    %bx
    ret

boot_drive:
    .byte   0

    .p2align 2
dap:
    .byte   16, 0
dap_count:
    .word   0
dap_off:
    .word   0
dap_seg:
    .word   0
dap_lba:
    .quad   0

/* struct lilo_mbr_desc, see include/lilo_map.h */
    .org    0x190
desc_magic:
    .long   0x4C49574C
desc_stage_sectors:
    .word   _stage_sectors
desc_map_sectors:
    .word   0
desc_stage_lba:
    .quad   0
desc_map_lba:
    .quad   0

    .org    0x1FE
    .byte   0x55, 0xAA
