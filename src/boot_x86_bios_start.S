/* boot_x86_bios_start.S
 *
 * Real-mode entry, protected-mode switch and BIOS call thunk for the
 * x86_64_lilo target.
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

/* wolfBoot runs in flat 32-bit protected mode with interrupts off. Each BIOS
 * service is reached by dropping back to real mode (bios_int), and the final
 * handoff to a Linux real-mode setup or a chain-loaded boot sector is a one
 * way trip to real mode (bios_realmode_jump). The real-mode pieces live in
 * .text16/.data16, which the linker keeps below 64 KB so they are reachable
 * with zero segments. */

#define CODE32_SEL      0x08
#define DATA32_SEL      0x10
#define CODE16_SEL      0x18
#define DATA16_SEL      0x20

/* Real-mode stack: below the MBR, which is no longer needed */
#define RM_STACK_TOP    0x7000

/* struct bios_regs, see include/x86/bios.h */
#define R_EAX           0
#define R_EBX           4
#define R_ECX           8
#define R_EDX           12
#define R_ESI           16
#define R_EDI           20
#define R_EBP           24
#define R_DS            28
#define R_ES            30
#define R_FLAGS         32
#define BIOS_REGS_SIZE  36

/* struct bios_jump */
#define J_CS            0
#define J_IP            2
#define J_DS            4
#define J_SS            6
#define J_SP            8
#define J_DX            10
#define J_SI            12
#define BIOS_JUMP_SIZE  16

#define MBR_DESC        (0x7C00 + 0x190)
#define MBR_DESC_SIZE   24

    .code16
    .section .text16, "ax"
    .globl _stage_entry
_stage_entry:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $RM_STACK_TOP, %sp
    cld
    movb    %dl, bios_boot_drive
    movw    $MBR_DESC, %si
    movw    $bios_mbr_desc, %di
    movw    $MBR_DESC_SIZE, %cx
    rep movsb

    /* A20: ask the BIOS, then the fast A20 gate for good measure */
    movw    $0x2401, %ax
    int     $0x15
    inb     $0x92, %al
    orb     $0x02, %al
    andb    $0xFE, %al
    outb    %al, $0x92

    cli
    lgdtl   gdt_desc
    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    ljmpl   $CODE32_SEL, $pm_entry

/* Second half of bios_int, entered from 16-bit protected mode */
rm_thunk16:
    movw    $DATA16_SEL, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    %cr0, %eax
    andl    $0xFFFFFFFE, %eax
    movl    %eax, %cr0
    ljmp    $0, $rm_thunk_real
rm_thunk_real:
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %ss
    movw    $RM_STACK_TOP, %sp
    lidtl   rm_idt_desc
    movl    rm_regs + R_EBX, %ebx
    movl    rm_regs + R_ECX, %ecx
    movl    rm_regs + R_EDX, %edx
    movl    rm_regs + R_ESI, %esi
    movl    rm_regs + R_EDI, %edi
    movl    rm_regs + R_EBP, %ebp
    movw    rm_regs + R_ES, %es
    movw    %ax, %fs
    movw    %ax, %gs
    pushw   rm_regs + R_DS
    movl    rm_regs + R_EAX, %eax
    popw    %ds
    sti
    pushfw
    lcallw  *%cs:rm_vector
    cli
    movl    %eax, %cs:rm_regs + R_EAX
    movl    %ebx, %cs:rm_regs + R_EBX
    movl    %ecx, %cs:rm_regs + R_ECX
    movl    %edx, %cs:rm_regs + R_EDX
    movl    %esi, %cs:rm_regs + R_ESI
    movl    %edi, %cs:rm_regs + R_EDI
    movl    %ebp, %cs:rm_regs + R_EBP
    movw    %ds, %cs:rm_regs + R_DS
    movw    %es, %cs:rm_regs + R_ES
    pushfw
    popw    %cs:rm_regs + R_FLAGS
    xorw    %ax, %ax
    movw    %ax, %ds
    lgdtl   gdt_desc
    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    ljmpl   $CODE32_SEL, $pm_return

/* Second half of bios_realmode_jump */
rmj_thunk16:
    movw    $DATA16_SEL, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    %cr0, %eax
    andl    $0xFFFFFFFE, %eax
    movl    %eax, %cr0
    ljmp    $0, $rmj_real
rmj_real:
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %ss
    movw    $RM_STACK_TOP, %sp
    lidtl   rm_idt_desc
    movw    rm_jump + J_DX, %dx
    movw    rm_jump + J_SI, %si
    movw    rm_jump + J_CS, %cx
    movw    rm_jump + J_IP, %di
    movw    rm_jump + J_DS, %bx
    movw    rm_jump + J_SP, %bp
    movw    rm_jump + J_SS, %ax
    movw    %ax, %ss
    movw    %bp, %sp
    pushw   %cx
    pushw   %di
    movw    %bx, %ds
    movw    %bx, %es
    movw    %bx, %fs
    movw    %bx, %gs
    xorl    %eax, %eax
    xorl    %ebx, %ebx
    xorl    %ecx, %ecx
    xorl    %edi, %edi
    xorl    %ebp, %ebp
    lretw

    .code32
    .text
pm_entry:
    movw    $DATA32_SEL, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    $_stack_top, %esp
    movl    $_start_bss, %edi
    movl    $_end_bss, %ecx
    subl    %edi, %ecx
    xorl    %eax, %eax
    rep stosb
    call    main
1:
    hlt
    jmp     1b

/* void bios_int(uint8_t intno, struct bios_regs *regs) */
    .globl bios_int
bios_int:
    pushl   %ebp
    pushl   %ebx
    pushl   %esi
    pushl   %edi
    movzbl  20(%esp), %eax
    movl    (,%eax,4), %eax
    movl    %eax, rm_vector
    movl    24(%esp), %esi
    movl    $rm_regs, %edi
    movl    $BIOS_REGS_SIZE, %ecx
    cld
    rep movsb
    movl    %esp, saved_esp
    ljmp    $CODE16_SEL, $rm_thunk16
pm_return:
    movw    $DATA32_SEL, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    saved_esp, %esp
    movl    24(%esp), %edi
    movl    $rm_regs, %esi
    movl    $BIOS_REGS_SIZE, %ecx
    cld
    rep movsb
    popl    %edi
    popl    %esi
    popl    %ebx
    popl    %ebp
    ret

/* void bios_realmode_jump(const struct bios_jump *j) */
    .globl bios_realmode_jump
bios_realmode_jump:
    cli
    movl    4(%esp), %esi
    movl    $rm_jump, %edi
    movl    $BIOS_JUMP_SIZE, %ecx
    cld
    rep movsb
    ljmp    $CODE16_SEL, $rmj_thunk16

    .section .data16, "aw"
    .p2align 3
gdt:
    .quad   0
    .quad   0x00CF9A000000FFFF  /* 0x08: code, 32-bit, flat */
    .quad   0x00CF92000000FFFF  /* 0x10: data, 32-bit, flat */
    .quad   0x00009A000000FFFF  /* 0x18: code, 16-bit, 64 KB */
    .quad   0x000092000000FFFF  /* 0x20: data, 16-bit, 64 KB */
gdt_end:

gdt_desc:
    .word   gdt_end - gdt - 1
    .long   gdt

rm_idt_desc:
    .word   0x3FF
    .long   0

    .p2align 2
rm_vector:
    .long   0
saved_esp:
    .long   0
rm_regs:
    .space  BIOS_REGS_SIZE
rm_jump:
    .space  BIOS_JUMP_SIZE

    .globl bios_boot_drive
bios_boot_drive:
    .byte   0x80
    .globl bios_mbr_desc
bios_mbr_desc:
    .space  MBR_DESC_SIZE
