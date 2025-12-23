/* exceptions.c
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
 *
 */

#include <x86/common.h>
#include <x86/exceptions.h>
#include <stdint.h>

#define EXCEPTION_NUM 35
#define INTERRUPT_GATE_TYPE (0xe)
#define SEGMENT_SELECTOR (0x18)
#define TYPE_FLAG (INTERRUPT_GATE_TYPE << 8 | 1 << 15)

#define LAPIC_DIV_CONF_REG 0xfee003e0
#define LAPIC_INITIAL_CNT 0xfee00380
#define LAPIC_CURRENT_CNT 0xfee00390
#define LAPIC_LVT_TIMER_REG 0xfee00320
#define LAPIC_EOI 0xfee000b0
#define LAPIC_SVR 0xfee000f0
#define LAPIC_SVR_ENABLE (1 << 8)
#define LAPIC_DIV_CONF_128 (1 << 1 | 1 << 3)
#define TIMER_SPURIOUS_NUMBER (33)
#define TIMER_VECTOR_NUMBER (34)
#define TIMER_CNT 0xffffff
#define IA32_APIC_BASE_MSR (0x1b)
#define IA32_APIC_BASE_MSR_ENABLE (0x800)

#define DEBUG_EXCEPTIONS
#if defined(DEBUG_EXCEPTIONS)
#include <printf.h>
#define EXCEPTIONS_DEBUG_PRINTF(...) wolfBoot_printf(__VA_ARGS__)
#else
#define EXCEPTIONS_DEBUG_PRINTF(...) do {} while(0)
#endif

struct interrupt_gate {
    uint16_t offset_0_15;
    uint16_t seg_sel;
    uint16_t type_flags;
    uint16_t offset_31_16;
    uint32_t offset_63_32;
    uint32_t reserved;
} __attribute__((packed));

struct idt_descriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct interrupt_gate idt_table[EXCEPTION_NUM];
static struct idt_descriptor idt_descriptor;

#define EXCEPTION_TRAMPOLINE(X)                                                \
    __attribute__((naked)) void exception_trampoline_##X() {                   \
      asm volatile("cli\r\n"                                                   \
                   "mov %0, %%rdi\r\n"                                         \
                   "sti\r\n"                                                   \
                   "call common_exception_handler\r\n"                         \
                   "iretq\r\n"                                                 \
                   :                                                           \
                   : "Z"(X));                                                  \
    }

__attribute__((used)) static void common_exception_handler(uint64_t vector_number)
{
    EXCEPTIONS_DEBUG_PRINTF("CPU exception: %d\r\n", (int)vector_number);
}

EXCEPTION_TRAMPOLINE(0);
EXCEPTION_TRAMPOLINE(1);
EXCEPTION_TRAMPOLINE(2);
EXCEPTION_TRAMPOLINE(3);
EXCEPTION_TRAMPOLINE(4);
EXCEPTION_TRAMPOLINE(5);
EXCEPTION_TRAMPOLINE(6);
EXCEPTION_TRAMPOLINE(7);
EXCEPTION_TRAMPOLINE(8);
EXCEPTION_TRAMPOLINE(9);
EXCEPTION_TRAMPOLINE(10);
EXCEPTION_TRAMPOLINE(11);
EXCEPTION_TRAMPOLINE(12)
EXCEPTION_TRAMPOLINE(13);
EXCEPTION_TRAMPOLINE(14);
EXCEPTION_TRAMPOLINE(15);
EXCEPTION_TRAMPOLINE(16);
EXCEPTION_TRAMPOLINE(17);
EXCEPTION_TRAMPOLINE(18);
EXCEPTION_TRAMPOLINE(19);
EXCEPTION_TRAMPOLINE(20);
EXCEPTION_TRAMPOLINE(21);
EXCEPTION_TRAMPOLINE(22);
EXCEPTION_TRAMPOLINE(23);
EXCEPTION_TRAMPOLINE(24);
EXCEPTION_TRAMPOLINE(25);
EXCEPTION_TRAMPOLINE(26);
EXCEPTION_TRAMPOLINE(27);
EXCEPTION_TRAMPOLINE(28);
EXCEPTION_TRAMPOLINE(29);
EXCEPTION_TRAMPOLINE(30);
EXCEPTION_TRAMPOLINE(31);
EXCEPTION_TRAMPOLINE(32);
/* lapic spurious vector */
EXCEPTION_TRAMPOLINE(33);

int setup_interrupt_gate(int vnum, uintptr_t handler)
{
    struct interrupt_gate *ig;

    ig = &idt_table[vnum];
    ig->offset_0_15 = (uint16_t)(handler & 0xffff);
    ig->offset_31_16 = (uint16_t)((handler >> 16) & 0xffff);
    ig->offset_63_32 = (uint32_t)((handler) >> 32);
    ig->type_flags = TYPE_FLAG;
    ig->seg_sel = SEGMENT_SELECTOR;
    ig->reserved = 0x0;
}

__attribute__((used)) static void _timer_handler()
{
    EXCEPTIONS_DEBUG_PRINTF("In the timer handler\r\n");
}

static void __attribute__((__naked__)) timer_handler() {
    asm volatile("cli\r\n"
                 "call _timer_handler\r\n"
                 "sti\r\n"
                 "mov %0, %%eax\r\n"
                 "movl $0, (%%eax)\r\n"
                 "iretq\r\n"::"i"((uint32_t)LAPIC_EOI));
}

static void setup_apic_timer()
{
    mmio_write32(LAPIC_SVR, (LAPIC_SVR_ENABLE));
    mmio_write32(LAPIC_DIV_CONF_REG, LAPIC_DIV_CONF_128);
    setup_interrupt_gate(TIMER_VECTOR_NUMBER, (uintptr_t)timer_handler);
    mmio_write32(LAPIC_LVT_TIMER_REG, TIMER_VECTOR_NUMBER);
}

int setup_interrupts()
{
    setup_interrupt_gate(0,(uintptr_t) exception_trampoline_0);
    setup_interrupt_gate(1,(uintptr_t) exception_trampoline_1);
    setup_interrupt_gate(2,(uintptr_t) exception_trampoline_2);
    setup_interrupt_gate(3,(uintptr_t) exception_trampoline_3);
    setup_interrupt_gate(4,(uintptr_t) exception_trampoline_4);
    setup_interrupt_gate(5,(uintptr_t) exception_trampoline_5);
    setup_interrupt_gate(6,(uintptr_t) exception_trampoline_6);
    setup_interrupt_gate(7,(uintptr_t) exception_trampoline_7);
    setup_interrupt_gate(8,(uintptr_t) exception_trampoline_8);
    setup_interrupt_gate(9,(uintptr_t) exception_trampoline_9);
    setup_interrupt_gate(10,(uintptr_t) exception_trampoline_10);
    setup_interrupt_gate(11,(uintptr_t) exception_trampoline_11);
    setup_interrupt_gate(12,(uintptr_t) exception_trampoline_12);
    setup_interrupt_gate(13,(uintptr_t) exception_trampoline_13);
    setup_interrupt_gate(14,(uintptr_t) exception_trampoline_14);
    setup_interrupt_gate(15,(uintptr_t) exception_trampoline_15);
    setup_interrupt_gate(16,(uintptr_t) exception_trampoline_16);
    setup_interrupt_gate(17,(uintptr_t) exception_trampoline_17);
    setup_interrupt_gate(18,(uintptr_t) exception_trampoline_18);
    setup_interrupt_gate(19,(uintptr_t) exception_trampoline_19);
    setup_interrupt_gate(20,(uintptr_t) exception_trampoline_20);
    setup_interrupt_gate(21,(uintptr_t) exception_trampoline_21);
    setup_interrupt_gate(22,(uintptr_t) exception_trampoline_22);
    setup_interrupt_gate(23,(uintptr_t) exception_trampoline_23);
    setup_interrupt_gate(24,(uintptr_t) exception_trampoline_24);
    setup_interrupt_gate(25,(uintptr_t) exception_trampoline_25);
    setup_interrupt_gate(26,(uintptr_t) exception_trampoline_26);
    setup_interrupt_gate(27,(uintptr_t) exception_trampoline_27);
    setup_interrupt_gate(28,(uintptr_t) exception_trampoline_28);
    setup_interrupt_gate(29,(uintptr_t) exception_trampoline_29);
    setup_interrupt_gate(30,(uintptr_t) exception_trampoline_30);
    setup_interrupt_gate(31,(uintptr_t) exception_trampoline_31);

    setup_apic_timer();
    idt_descriptor.base = (uintptr_t)&idt_table;
    idt_descriptor.limit = sizeof(idt_table) -1;

    asm ("lidt %0\r\n" : : "m"(idt_descriptor));
    asm ("sti\r\n");

    return 0;
}

void deinit_interrupts()
{
    idt_descriptor.base = (uintptr_t)NULL;
    idt_descriptor.limit = 0xffff;
    asm ("cli\r\n");
    asm ("lidt %0\r\n" : : "m"(idt_descriptor));
}

void wfi()
{
    setup_apic_timer();
    mmio_write32(LAPIC_INITIAL_CNT, TIMER_CNT);
    hlt();
}
