/* boot_x86_fsp.c
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
 */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

#include <x86/fsp.h>
#include <x86/hob.h>
#include <uart_drv.h>
#include <printf.h>
#include <loader.h>

#define WOLFBOOT_X86_STACK_SIZE 0x10000

/* info can be retrieved from the CfgRegionSize of FSP info header. we need to
 * know this at compile time because to make things simpler we want to use the
 * stack to store the parameters and we don't want to include machine specific
 * code here */
#ifndef FSP_M_UDP_MAX_SIZE
#define FSP_M_UDP_MAX_SIZE 0x80
#endif

/* Amount of car memory to provide to FSP-M, machine dependent, find the value
 * in the integration guide */
#ifndef FSP_M_CAR_MEM_SIZE
#define FSP_M_CAR_MEM_SIZE 0x10000
#endif

/* offset of the header from the base image  */
#define FSP_INFO_HEADER_OFFSET 0x94
#define EFI_SUCCESS 0x0
#define MEMORY_4GB (4ULL * 1024 * 1024 * 1024)
typedef uint32_t (*memory_init_cb)(void *udp, struct efi_hob **HobList);
typedef uint32_t (*temp_ram_exit_cb)(void *udp);
typedef uint32_t (*silicon_init_cb)(void *udp);

/* need to be implemented by machine dependent code */
int fsp_machine_update_m_parameters(uint8_t *default_m_params,
                                    uint32_t mem_base, uint32_t mem_size);

extern uint8_t _start_fsp_t[];
extern uint8_t _start_fsp_m[];
extern uint8_t _start_fsp_s[];
extern uint8_t _start_wolfboot[];
extern uint8_t _end_wolfboot[];
extern uint8_t _wolfboot_load_address[];
extern uint8_t _end_bss[], _start_bss[];
extern int main(void);

static size_t _strlen(const char *s)
{
    size_t i = 0;

    while (s[i] != 0)
        i++;

    return i;
}


#ifdef DEBUG_UART
static void early_print(const char *str)
{
    uart_write((const uint8_t*)str, _strlen(str));
}
#else
static inline void early_print(const char *str)
{
    (void)str;
}
#endif /* DEBUG_UART */

static void panic()
{
    while (1) {}
}

static int get_top_address(uint64_t *top, struct efi_hob *hoblist)
{
    struct efi_hob_resource_descriptor *fsp_reserved;

    fsp_reserved = hob_find_fsp_reserved(hoblist);
    if (fsp_reserved == NULL)
        return -1;

    *top = fsp_reserved->physical_start;
    return 0;
}

static void change_stack_and_invoke(uint32_t new_stack,
                                    void (*other_func)(void *), void *ptr)
{
    __asm__ volatile("movl %0, %%eax\n"
                     "subl $4, %%eax\n"
                     "movl %1, (%%eax)\n"
                     "mov %%eax, %%esp\n"
                     "call *%2\n"
                     :
                     : "r"(new_stack), "r"(ptr), "r"(other_func)
                     : "%eax");
}

static void _memcpy(uint8_t *dst, const uint8_t *src, size_t n)
{
    unsigned int i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
}

static void _memset(uint8_t *dst, char c, size_t n)
{
    unsigned int i;
    for (i = 0; i < n; i++)
        dst[i] = c;
}

static void load_wolfboot()
{
    size_t wolfboot_size, bss_size;

    wolfboot_size = _end_wolfboot - _start_wolfboot;
    _memcpy(_start_wolfboot, _wolfboot_load_address, wolfboot_size);
    bss_size = _end_bss - _start_bss;
    _memset(_start_bss, 0, bss_size);
}

static void memory_ready_entry(void *ptr)
{
    struct stage2_parameter *stage2_params = (struct stage2_parameter *)ptr;
    struct fsp_info_header *fsp_info_header;
    temp_ram_exit_cb TempRamExit;
    silicon_init_cb SiliconInit;
    uint32_t status;
    int ret;

    fsp_info_header =
        (struct fsp_info_header *)(_start_fsp_m + FSP_INFO_HEADER_OFFSET);
    TempRamExit = (temp_ram_exit_cb)(_start_fsp_m +
                                     fsp_info_header->TempRamExitEntryOffset);

    /* TODO: provide a way for machine specific code to update parameters */
    status = TempRamExit(NULL);
    if (status != EFI_SUCCESS) {
        early_print("temp ram exit failed\n");
        panic();
    }

    /* TODO: provide a way for machine specific code to update parameters */
    fsp_info_header =
        (struct fsp_info_header *)(_start_fsp_s + FSP_INFO_HEADER_OFFSET);
    SiliconInit = (silicon_init_cb)(_start_fsp_s +
                                    fsp_info_header->FspSiliconInitEntryOffset);
    status = SiliconInit(NULL);
    if (status != EFI_SUCCESS) {
        early_print("silicon init failed\n");
        panic();
    }

    load_wolfboot();
    set_stage2_parameter(stage2_params);
    main();
}

static int fsp_info_header_is_ok(struct fsp_info_header *hdr)
{
    uint8_t *raw_signature;

    raw_signature = (uint8_t *)&hdr->Signature;
    if (raw_signature[0] != 'F' || raw_signature[1] != 'S' ||
        raw_signature[2] != 'P' || raw_signature[3] != 'H') {
        return 0;
    }
    return 1;
}

void start(uint32_t stack_base, uint32_t stack_top, uint64_t timestamp,
           uint32_t bist)
{
    uint8_t udp_m_parameter[FSP_M_UDP_MAX_SIZE], *udp_m_default;
    struct fsp_info_header *fsp_m_info_header;
    struct stage2_parameter *stage2_params;
    uint8_t *_fsp_m_base, done = 0;
    struct efi_hob *hobList, *it;
    memory_init_cb MemoryInit;
    uint64_t top_address;
    uint32_t new_stack;
    uint32_t status;
    uint8_t *tolud;
    uint16_t type;

    (void)stack_top;
    (void)timestamp;
    (void)bist;

    uart_init(115200, 8, 'N', 0);

    fsp_m_info_header =
        (struct fsp_info_header *)(_start_fsp_m + FSP_INFO_HEADER_OFFSET);
    udp_m_default = _start_fsp_m + fsp_m_info_header->CfgRegionOffset;
    if (!fsp_info_header_is_ok(fsp_m_info_header)) {
        early_print("invalid FSP_INFO_HEADER");
        panic();
    }

    if (fsp_m_info_header->CfgRegionSize > sizeof(udp_m_parameter)) {
        early_print("FSP-M UDP size is bigger than FSP_M_UDP_MAX_SIZE\n");
        panic();
    }

    _memcpy(udp_m_parameter, udp_m_default, fsp_m_info_header->CfgRegionSize);
    status = fsp_machine_update_m_parameters(udp_m_parameter, stack_base + 0x4,
                                             FSP_M_CAR_MEM_SIZE);
    if (status != 0) {
        early_print("fsp_machine_update_m_parameters returns a non-zero value");
        panic();
    }

    MemoryInit = (memory_init_cb)(_start_fsp_m +
                                  fsp_m_info_header->FspMemoryInitEntryOffset);
    status = MemoryInit((void *)udp_m_parameter, &hobList);
    if (status != EFI_SUCCESS) {
        early_print("FSP MemoryInit API failed\n");
        panic();
    }

    status = get_top_address(&top_address, hobList);
    if (status != 0) {
        early_print("can't find the top available address in memory\n");
        panic();
    }

    if (top_address > MEMORY_4GB) {
        early_print("detected top usable low memory is above 4gb, unsupported");
        panic();
    }

    new_stack = top_address;
    top_address =
        new_stack - WOLFBOOT_X86_STACK_SIZE - sizeof(struct stage2_parameter);
    stage2_params = (struct stage2_parameter *)(uint32_t)top_address;
    _memset((uint8_t *)stage2_params, 0, sizeof(struct stage2_parameter));
    stage2_params->hobList = hobList;
    change_stack_and_invoke(new_stack, memory_ready_entry,
                            (void *)stage2_params);

    early_print("memory_ready_entry returns\n");
    panic();
}
