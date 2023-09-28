/* common.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
/**
 * @file common.c
 *
 * @brief Common functions and macros for wolfBoot
 *
 * This file contains common functions and macros used in wolfBoot for
 * x86 architecture. These functions include memory-mapped I/O access,
 * I/O port access, CPUID instruction, and reset control functions.
 */
#ifndef COMMON_H_
#define COMMON_H_

#include <printf.h>
#include <stdint.h>

#include <x86/common.h>

#define barrier()   __asm__ __volatile__ ("":::"memory");

#define RESET_CONTROL_IO_PORT 0xcf9
#define RESET_CONTROL_COLD           0x0E
#define RESET_CONTROL_WARM           0x06
#define RESET_CONTROL_HARDSTARTSTATE 0x02

#define CR4_PAE_BIT (1 << 5)
#define CR0_PG_BIT (1 << 31)
#define IA32_EFER_LME (1 << 8)

#ifndef NULL
#define NULL 0
#endif

/**
 * @brief Memory-mapped write access to a 32-bit register.
 *
 * @param address The memory address of the register to write to.
 * @param value The 32-bit value to write.
 */
void mmio_write32(uintptr_t address, uint32_t value)
{
    volatile uint32_t *_addr = (uint32_t*)address;
    *_addr = value;
    barrier();
}


/**
 * @brief Memory-mapped read access to a 32-bit register.
 *
 * @param address The memory address of the register to read from.
 * @return The 32-bit value read from the register.
 */
uint32_t mmio_read32(uintptr_t address)
{
    volatile uint32_t *_addr = (uint32_t*)address;
    uint32_t ret;

    ret = *_addr;
    barrier();
    return ret;
}

/**
 * @brief Memory-mapped write access to a 16-bit register.
 *
 * @param address The memory address of the register to write to.
 * @param value The 16-bit value to write.
 */
void mmio_write16(uintptr_t address, uint16_t value)
{
    volatile uint16_t *_addr = (uint16_t*)address;

    *_addr = value;
    barrier();
}

/**
 * @brief Memory-mapped read access to a 16-bit register.
 *
 * @param address The memory address of the register to read from.
 * @return The 16-bit value read from the register.
 */
uint16_t mmio_read16(uintptr_t address)
{
    volatile uint16_t *_addr = (uint16_t*)address;
    uint16_t ret;

    ret = *_addr;
    barrier();
    return ret;
}

/**
 * @brief Memory-mapped write access to an 8-bit register.
 *
 * @param address The memory address of the register to write to.
 * @param value The 8-bit value to write.
 */
void mmio_write8(uintptr_t address, uint8_t value)
{
    volatile uint8_t *_addr = (uint8_t*)address;

    *_addr = value;
    barrier();
}

/**
 * @brief Memory-mapped read access to an 8-bit register.
 *
 * @param address The memory address of the register to read from.
 * @return The 8-bit value read from the register.
 */
uint8_t mmio_read8(uintptr_t address)
{
    volatile uint8_t *_addr = (uint8_t*)address;
    uint8_t ret;

    ret = *_addr;
    barrier();
    return ret;
}

/**
 * @brief I/O port write access to an 8-bit register.
 *
 * @param port The I/O port address of the register to write to.
 * @param value The 8-bit value to write.
 */
void io_write8(uint16_t port, uint8_t value)
{
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

/**
 * @brief I/O port write access to a 32-bit register.
 *
 * @param port The I/O port address of the register to write to.
 * @param value The 32-bit value to write.
 */
void io_write32(uint16_t port, uint32_t value)
{
    __asm__ __volatile__ ("outl %0,%w1": :"a" (value), "Nd" (port));
}

/**
 * @brief I/O port read access to a 32-bit register.
 *
 * @param port The I/O port address of the register to read from.
 * @return The 32-bit value read from the register.
 */
uint32_t io_read32(uint16_t port)
{
    unsigned int _v;

    __asm__ __volatile__ ("inl %w1,%0":"=a" (_v):"Nd" (port));
    return _v;
}

/**
 * @brief I/O port write access to a 16-bit register.
 *
 * @param port The I/O port address of the register to write to.
 * @param value The 16-bit value to write.
 */
void io_write16(uint16_t port, uint16_t value)
{
    __asm__ __volatile__ ("outw %0,%w1": :"a" (value), "Nd" (port));
}

/**
 * @brief I/O port read access to a 16-bit register.
 *
 * @param port The I/O port address of the register to read from.
 * @return The 16-bit value read from the register.
 */
uint16_t io_read16(uint16_t port)
{
    uint16_t _v;

    __asm__ __volatile__ ("inw %w1,%0":"=a" (_v):"Nd" (port));
    return _v;
}

/**
 * @brief Memory-mapped OR operation with a 32-bit register.
 *
 * Reads the 32-bit register at the given address, performs a bitwise OR operation
 * with the provided value, and writes the result back to the register.
 *
 * @param address The memory address of the register.
 * @param value The 32-bit value to OR with the register contents.
 * @return The updated 32-bit value of the register after the OR operation.
 */
uint32_t mmio_or32(uintptr_t address, uint32_t value)
{
    uint32_t reg;

    reg = mmio_read32(address);
    reg |= value;
    mmio_write32(address, reg);
    return reg;
}

uint8_t io_read8(uint16_t port)
{
    uint8_t v;

	asm volatile("inb %1, %0" : "=a" (v) : "dN" (port));
    return v;
}

/**
 * @brief Reset the system.
 *
 * @param warm Set to 1 for a warm reset, 0 for a cold reset.
 */
void reset(uint8_t warm)
{
    uint8_t value;

    value = (warm) ? RESET_CONTROL_WARM : RESET_CONTROL_COLD;
    io_write8(RESET_CONTROL_IO_PORT, RESET_CONTROL_HARDSTARTSTATE);
    io_write8(RESET_CONTROL_IO_PORT, value);
    while(1){};
}

/**
 * @brief Delay the execution for a specified number of milliseconds.
 *
 * @param msec The number of milliseconds to delay.
 */
void delay(int msec)
{
    int i;
    for (i = 0; i < msec * 100; i++)
        io_write8(0x80, 0x41);
}

/**
 * @brief Enter an infinite loop, causing a panic state.
 *
 * This function is used for error handling when the system encounters an unrecoverable issue.
 * It enters an infinite loop, causing a panic state.
 */
void panic()
{
    while (1) {
        delay(1);
    }
}

/**
 * @brief Execute the CPUID instruction and retrieve the result.
 *
 * @param eax_param The value to set in the EAX register before executing CPUID.
 * @param eax Pointer to store the output value of the EAX register.
 * @param ebx Pointer to store the output value of the EBX register.
 * @param ecx Pointer to store the output value of the ECX register.
 * @param edx Pointer to store the output value of the EDX register.
 */
void cpuid(uint32_t eax_param,
           uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    uint32_t _eax, _ebx, _ecx, _edx;
    __asm__(
        "movl %4, %%eax\n"
        "cpuid\n"
        "movl %%eax, %0\n"
        "movl %%ebx, %1\n"
        "movl %%ecx, %2\n"
        "movl %%edx, %3\n"
        : "=g"(_eax), "=g"(_ebx), "=g"(_ecx), "=g"(_edx)
        : "g"(eax_param)
        : "%eax", "%ebx", "%ecx", "%edx"
    );
    if (eax != NULL)
        *eax = _eax;
    if (ebx != NULL)
        *ebx = _ebx;
    if (ecx != NULL)
        *ecx = _ecx;
    if (edx != NULL)
        *edx = _edx;
}

/**
 * @brief Check if 1GB page is supported by the CPU.
 *
 * @return 1 if 1GB page is supported, 0 otherwise.
 */
int cpuid_is_1gb_page_supported()
{
    uint32_t edx;
    cpuid(CPUID_EXTFEAT_LEAF01, NULL, NULL, NULL, &edx);
    return (edx & CPUID_EDX_1GB_PAGE_SUPPORTED) != 0;
}

#ifdef BUILD_LOADER_STAGE1
/* Needs to match the code_sel_long offset inside the GDT. The GDT is populated
 * in src/x86 */
#define CODE_SEL_LONG 0x18
/**
 * @brief Switch the CPU to long mode.
 *
 * This function switches the CPU to long mode with the provided entry point
 * and page table.
 *
 * @param entry The entry point of the long mode code.
 * @param page_table The page table address to use in long mode.
 */
void switch_to_long_mode(uint64_t *entry, uint32_t page_table)
{
    /* refer to Intel Software Developer's Manual Vol 3 sec 9.8.5*/
    __asm__ (
             "mov %%cr4, %%eax\r\n"
             "or %0, %%eax\r\n"
             "mov %%eax, %%cr4\r\n"
             "mov %4, %%eax\r\n"
             "mov %%eax, %%cr3\r\n"
             "mov $0xc0000080, %%ecx\r\n"
             "rdmsr\r\n"
             "or %1, %%eax\r\n"
             "mov $0xc0000080, %%ecx\r\n"
             "wrmsr\r\n"
             "mov %%cr0, %%eax\r\n"
             "or %2, %%eax\r\n"
             "mov %%eax, %%cr0\r\n"
             "mov %3, %%eax\r\n"
             "ljmp %5,$_setcs\r\n"
             "_setcs:\r\n"
             "jmp *%%eax\r\n"
             :
             : "i"(CR4_PAE_BIT), "i"(IA32_EFER_LME), "i"(CR0_PG_BIT), "g"(entry), "g"(page_table), "i"(CODE_SEL_LONG)
             : "%eax", "%ecx", "%edx", "memory"
             );
}
#else
void switch_to_long_mode(uint64_t *entry, uint32_t page_table)
{
    void (*_entry)() = (void(*)())entry;
    (void)page_table;
    _entry();
}
#endif /* BUILD_LOADER_STAGE1 */

void x86_log_memory_load(uint32_t start, uint32_t end, const char *name)
{
    wolfBoot_printf("mem: [ 0x%x, 0x%x ] - %s (0x%x) \n\r", start, end, name, end - start);
}
#endif /* COMMON_H_ */
