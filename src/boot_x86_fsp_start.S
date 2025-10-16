;; boot_x86_fsp_start.S
;;
;; Copyright (C) 2025 wolfSSL Inc.
;;
;; This file is part of wolfBoot.
;;
;; wolfBoot is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 3 of the License, or
;; (at your option) any later version.
;;
;; wolfBoot is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with this program; if not, write to the Free Software
;; Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

%define ADDR_16(X) (_off_boot + X - $$)

; PE | MP | NE (PE is Protected Mode Enabled)
%define CR0_PROTECTED_MODE 0x00000023

%define SEG_DESC_PRESENT_FLAG (1 << 7)
%define SEG_DESC_SYSTEM_FLAG (1 << 4)
; read/write
%define SEG_DESC_DATA_TYPE 3
; read/execute
%define SEG_DESC_CODE_TYPE 0xb
%define SEG_DESC_GRANULARITY_FLAG (1 << 7)
%define SEG_DESC_DEF_SIZE32BIT (1 << 6)
%define SEG_DESC_L_BIT (1 << 5)
%define SEC_DEFAULT_CR0_MASK  0x40000000
%define SEC_DEFAULT_CR4       0x640
; present, dpl=0, code/data, type
%define SEG_DESC_CODE_8_15 \
	SEG_DESC_PRESENT_FLAG | SEG_DESC_SYSTEM_FLAG | SEG_DESC_CODE_TYPE
%define SEG_DESC_DATA_8_15 \
	SEG_DESC_PRESENT_FLAG | SEG_DESC_SYSTEM_FLAG | SEG_DESC_DATA_TYPE
; granularity, default operand size 32bit, no code 64bit, 0xf limit 19:16
%define SEG_DESC_16_23 \
	SEG_DESC_GRANULARITY_FLAG | SEG_DESC_DEF_SIZE32BIT | 0xf
%define SEG_DESC_16_23_LONG \
	SEG_DESC_GRANULARITY_FLAG | SEG_DESC_L_BIT | 0xf

%define FSP_TEMP_RAM_INIT_OFF (0x94 + 48)
extern _off_boot
extern _start_fsp_t
extern TempRamInitParams
extern start
[section .jmpto32]
;; If the offset to the segment selector code_sel_long is changed, make sure to
;; update the corresponding code in src/x86/common.c accordingly.
gdt:
	;; null descriptor
	dw	0	     ; limit 15:0
	dw	0	     ; base 15:0
	db	0	     ; base 23:16
	db	0	     ; sys flag, dpl, type
	db	0	     ; limit 19:16, flags
	db	0	     ; base 31:24
	;; linear data segment descriptor
	data_sel    equ $-gdt
	dw	0xffff	     ; limit 15:0
	dw	0	     ; base 15:0
	db	0	     ; base 23:16
	db	SEG_DESC_DATA_8_15
	db	SEG_DESC_16_23
	db	0	     ; base 31:24

	;; linear code segment descriptor
	code_sel    equ $-gdt
	dw	0xffff	     ; limit 15:0
	dw	0	     ; base 15:0
	db	0	     ; base 23:16
	db	SEG_DESC_CODE_8_15
	db	SEG_DESC_16_23
	db	0	     ; base 31:24

	;; linear code segment descriptor longmode
	code_sel_long    equ $-gdt
	dw	0xffff	     ; limit 15:0
	dw	0	     ; base 15:0
	db	0	     ; base 23:16
	db	SEG_DESC_CODE_8_15
	db	SEG_DESC_16_23_LONG
	db	0	     ; base 31:24

	gdt_end equ $
gdtr:
	dw	gdt_end - gdt - 1
	dd	gdt

TempRamInitStack:
	dd  TempRamInitRetAddr
	dd  TempRamInitParams
reset:
BITS 16
	cli
	mov bx, ADDR_16(gdtr)
	o32 lgdt[cs:bx]
	mov ebx, eax ; save BIST to EBX
	mov eax, cr0
        and eax, SEC_DEFAULT_CR0_MASK
	or  eax, CR0_PROTECTED_MODE
	mov cr0, eax
	jmp code_sel:dword jumpTo32BitAndLandHere
BITS	32
jumpTo32BitAndLandHere:
        mov     eax, SEC_DEFAULT_CR4
        mov     cr4, eax
	mov ax, data_sel
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	movd mm0, ebx  ; save BIST to MM0
	rdtsc
	mov esi, eax
	mov edi, edx
	mov esp, TempRamInitStack
	mov eax, [_start_fsp_t + FSP_TEMP_RAM_INIT_OFF]
	add eax, _start_fsp_t
	jmp eax
TempRamInitRetAddr:
	;; TODO: handle 8000000Eh (EFI_NOT_FOUND used for UCODE not found)

	cmp eax, 0
	jmp TempRamInitSuccess
	; panic
	jmp $

TempRamInitSuccess:
;; setup stack
	mov esp, edx
;; setup args
	movd eax, mm0
	push eax ; CpuBist
	push edi ;timestamp
	push esi
	push edx ;stack top
	push ecx ;stack base

	call start		
	; panic
	jmp $

[section .reset_vector]
BITS 16
ALIGN 16
global reset_vector
reset_vector:
	nop
	nop
	jmp reset
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
