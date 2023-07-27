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
#include <printf.h>
#include <string.h>

#include <x86/fsp/FspCommon.h>
#include <x86/common.h>
#include <uart_drv.h>
#include <x86/hob.h>
#include <x86/paging.h>
#include <pci.h>
#include <target.h>
#include <stage1.h>

#include "wolfboot/wolfboot.h"
#include "image.h"

#define WOLFBOOT_X86_STACK_SIZE 0x10000


#ifndef STAGE1_AUTH
/* When STAGE1_AUTH is disabled, create dummy images to fill
 * the space used by wolfBoot manifest headers to authenticate FSPs
 */
#define HEADER_SIZE IMAGE_HEADER_SIZE
const uint8_t __attribute__((section(".sig_fsp_m"))) empty_sig_fsp_m[HEADER_SIZE];
const uint8_t __attribute__((section(".sig_fsp_s"))) empty_sig_fsp_s[HEADER_SIZE];
#endif

/* info can be retrieved from the CfgRegionSize of FSP info header. we need to
 * know this at compile time because to make things simpler we want to use the
 * stack to store the parameters and we don't want to include machine specific
 * code here */
#ifndef FSP_M_UDP_MAX_SIZE

/* #define FSP_M_UDP_MAX_SIZE 0x80 */
#define FSP_M_UDP_MAX_SIZE 0x978
#endif

#ifndef FSP_S_PARAM_SIZE
#define FSP_S_PARAM_SIZE 0xee0
#endif

/* Amount of car memory to provide to FSP-M, machine dependent, find the value
 * in the integration guide */
#ifndef FSP_M_CAR_MEM_SIZE
#define FSP_M_CAR_MEM_SIZE 0x20000
#endif

/* offset of the header from the base image  */
#define FSP_INFO_HEADER_OFFSET 0x94
#define EFI_SUCCESS 0x0
#define FSP_STATUS_RESET_REQUIRED_COLD  0x40000001
#define FSP_STATUS_RESET_REQUIRED_WARM  0x40000002
#define MEMORY_4GB (4ULL * 1024 * 1024 * 1024)
#define ENDLINE "\r\n"

typedef uint32_t (*memory_init_cb)(void *udp, struct efi_hob **HobList);
typedef uint32_t (*temp_ram_exit_cb)(void *udp);
typedef uint32_t (*silicon_init_cb)(void *udp);
typedef uint32_t (*notify_phase_cb)(NOTIFY_PHASE_PARAMS *p);

/* need to be implemented by machine dependent code */
int fsp_machine_update_m_parameters(uint8_t *default_m_params,
                                    uint32_t mem_base, uint32_t mem_size);
int fsp_machine_update_s_parameters(uint8_t *default_s_params);
int post_temp_ram_init_cb(void);

/* from the linker */
extern uint8_t _start_fsp_t[];
extern uint8_t _start_fsp_m[];
extern uint8_t _start_fsp_s[];
extern uint8_t _fsp_t_hdr[];
extern uint8_t _fsp_m_hdr[];
extern uint8_t _fsp_s_hdr[];
extern uint8_t _wolfboot_flash_start[];
extern uint8_t _wolfboot_flash_end[];
extern uint8_t wb_end_bss[], wb_start_bss[];
extern int main(void);

static int get_top_address(uint64_t *top, struct efi_hob *hoblist)
{
    struct efi_hob_resource_descriptor *fsp_reserved;

    fsp_reserved = hob_find_fsp_reserved(hoblist);
    if (fsp_reserved == NULL)
        return -1;

    *top = fsp_reserved->physical_start;
    wolfBoot_printf("top reserved %x_%xh" ENDLINE, (uint32_t)(*top>>32),
                    (uint32_t)*top);
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

static void load_wolfboot()
{
    size_t wolfboot_size, bss_size;

    wolfBoot_printf("loading wolfboot at %x..." ENDLINE,
                    (uint32_t)WOLFBOOT_LOAD_BASE);
    wolfboot_size = _wolfboot_flash_end - _wolfboot_flash_start;
    memcpy((uint8_t*)WOLFBOOT_LOAD_BASE,
            _wolfboot_flash_start, wolfboot_size);
    bss_size = wb_end_bss - wb_start_bss;
    memset(wb_start_bss, 0, bss_size);
    wolfBoot_printf("load wolfboot end" ENDLINE);
}

extern uint8_t _stage2_params[];

static void set_stage2_parameter(struct stage2_parameter *p)
{
    memcpy((uint8_t*)_stage2_params, (uint8_t*)p, sizeof(*p));
}

#ifdef WOLFBOOT_64BIT
static void jump_into_wolfboot()
{
    struct stage2_parameter *params = (struct stage2_parameter*)_stage2_params;
    uint32_t cr3;
    int ret;

    wolfBoot_printf("building identity map at %x...\r\n",
                    (uint32_t)params->page_table);
    ret = x86_paging_build_identity_mapping(MEMORY_4GB,
                                            (uint8_t*)(uintptr_t)params->page_table);
    if (ret != 0) {
        wolfBoot_printf("can't build identity mapping\r\n");
        panic();
    }
    wolfBoot_printf("starting wolfboot 64bit\r\n");
    switch_to_long_mode((uint64_t*)&main, params->page_table);
    panic();
}
#else
static void jump_into_wolfboot()
{
    main();
}
#endif /* WOLFBOOT_64BIT */

static void memory_ready_entry(void *ptr)
{
    struct stage2_parameter *stage2_params = (struct stage2_parameter *)ptr;
    uint8_t silicon_init_parameter[FSP_S_PARAM_SIZE];
    struct fsp_info_header *fsp_info_header;
    temp_ram_exit_cb TempRamExit;
    silicon_init_cb SiliconInit;
    notify_phase_cb notifyPhase;
    NOTIFY_PHASE_PARAMS param;
    uint32_t status;
    unsigned int i;
    int ret;
#ifdef STAGE1_AUTH
    struct wolfBoot_image fsp_s;
#endif

    fsp_info_header =
        (struct fsp_info_header *)(_start_fsp_m + FSP_INFO_HEADER_OFFSET);
    TempRamExit = (temp_ram_exit_cb)(_start_fsp_m +
                                     fsp_info_header->TempRamExitEntryOffset);
    status = TempRamExit(NULL);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("temp ram exit failed" ENDLINE);
        panic();
    }

#ifdef STAGE1_AUTH
    /* Verify FSP_S */
    ret = wolfBoot_open_image_address(&fsp_s, _fsp_s_hdr);
    if (ret < 0) {
        wolfBoot_printf("Failed to open FSP_S image" ENDLINE);
        panic();
    }
    wolfBoot_printf("FSP_S open successfully." ENDLINE);
    ret = wolfBoot_verify_integrity(&fsp_s);
    if (ret < 0) {
        wolfBoot_printf("Failed integrity check on FSP_S" ENDLINE);
        panic();
    }
    wolfBoot_printf("FSP_S is valid. Checking signature." ENDLINE);
    ret = wolfBoot_verify_authenticity(&fsp_s);
    if (ret < 0) {
        wolfBoot_printf("Failed signature check on FSP_S" ENDLINE);
        panic();
    }
    wolfBoot_printf("FSP_S: verified OK." ENDLINE);
#endif

    memcpy(silicon_init_parameter, _start_fsp_s + fsp_info_header->CfgRegionOffset,
            FSP_S_PARAM_SIZE);
    status = fsp_machine_update_s_parameters(silicon_init_parameter);

    fsp_info_header =
        (struct fsp_info_header *)(_start_fsp_s + FSP_INFO_HEADER_OFFSET);
    SiliconInit = (silicon_init_cb)(_start_fsp_s +
                                    fsp_info_header->FspSiliconInitEntryOffset);

    wolfBoot_printf("call silicon..." ENDLINE);
    status = SiliconInit(silicon_init_parameter);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("failed %x\n", status);
        panic();
    }
    wolfBoot_printf("success" ENDLINE);
    pci_enum_do();
    notifyPhase = (notify_phase_cb)(_start_fsp_s +
                                        fsp_info_header->NotifyPhaseEntryOffset);
    param.Phase = EnumInitPhaseAfterPciEnumeration;
    status = notifyPhase(&param);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("failed %d: %x\n", __LINE__, status);
        panic();
    }
    param.Phase = EnumInitPhaseReadyToBoot;
    status = notifyPhase(&param);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("failed %d: %x\n", __LINE__, status);
        panic();
    }
    param.Phase = EnumInitPhaseEndOfFirmware;
    status = notifyPhase(&param);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("failed %d: %x\n", __LINE__, status);
        panic();
    }
    uint32_t info[4];
    cpuid(0, &info[0], &info[1], &info[2], NULL);
    wolfBoot_printf("CPUID(0):%x %x %x\r\n", info[0], info[1], info[2]);
    load_wolfboot();
    set_stage2_parameter(stage2_params);
    jump_into_wolfboot();
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
    uint16_t type;
    uint32_t esp;

#ifdef STAGE1_AUTH
    int ret;
    struct wolfBoot_image fsp_m;
#endif

    (void)stack_top;
    (void)timestamp;
    (void)bist;

    status = post_temp_ram_init_cb();
    if (status != 0) {
        wolfBoot_printf("post temp ram init cb failed" ENDLINE);
        panic();
    }
    wolfBoot_printf("Cache-as-RAM initialized" ENDLINE);

#ifdef STAGE1_AUTH
    /* Verify FSP_M */
    ret = wolfBoot_open_image_address(&fsp_m, _fsp_m_hdr);
    if (ret < 0) {
        wolfBoot_printf("Failed to open FSP_M image" ENDLINE);
        panic();
    }
    wolfBoot_printf("FSP_M open successfully." ENDLINE);
    ret = wolfBoot_verify_integrity(&fsp_m);
    if (ret < 0) {
        wolfBoot_printf("Failed integrity check on FSP_M" ENDLINE);
        panic();
    }
    wolfBoot_printf("FSP_M is valid. Checking signature." ENDLINE);
    ret = wolfBoot_verify_authenticity(&fsp_m);
    if (ret < 0) {
        wolfBoot_printf("Failed signature check on FSP_M" ENDLINE);
        panic();
    }
    wolfBoot_printf("FSP_M: verified OK." ENDLINE);
#endif

    fsp_m_info_header =
        (struct fsp_info_header *)(_start_fsp_m + FSP_INFO_HEADER_OFFSET);
    udp_m_default = _start_fsp_m + fsp_m_info_header->CfgRegionOffset;
    if (!fsp_info_header_is_ok(fsp_m_info_header)) {
        wolfBoot_printf("invalid FSP_INFO_HEADER" ENDLINE);
        panic();
    }

    if (fsp_m_info_header->CfgRegionSize > sizeof(udp_m_parameter)) {
        wolfBoot_printf("FSP-M UDP size is bigger than FSP_M_UDP_MAX_SIZE" ENDLINE);
        panic();
    }

    memcpy(udp_m_parameter, udp_m_default, fsp_m_info_header->CfgRegionSize);
    status = fsp_machine_update_m_parameters(udp_m_parameter, stack_base + 0x4,
                                             FSP_M_CAR_MEM_SIZE);
    if (status != 0) {
        panic();
    }

    wolfBoot_printf("calling FspMemInit..." ENDLINE);
    MemoryInit = (memory_init_cb)(_start_fsp_m +
                                  fsp_m_info_header->FspMemoryInitEntryOffset);
    status = MemoryInit((void *)udp_m_parameter, &hobList);
    if (status == FSP_STATUS_RESET_REQUIRED_WARM) {
        wolfBoot_printf("warm reset required" ENDLINE);
        reset(1);
    }
    else if (status == FSP_STATUS_RESET_REQUIRED_COLD) {
        wolfBoot_printf("cold reset required" ENDLINE);
        reset(0);
    }
    else if (status != EFI_SUCCESS) {
        wolfBoot_printf("failed: 0x%x" ENDLINE, status);
        panic();
    }
    wolfBoot_printf("success" ENDLINE);

    status = get_top_address(&top_address, hobList);
    if (status != 0) {
        panic();
    }

    if (top_address > MEMORY_4GB) {
        panic();
    }

    new_stack = top_address;
    top_address =
        new_stack - WOLFBOOT_X86_STACK_SIZE - sizeof(struct stage2_parameter);
    stage2_params = (struct stage2_parameter *)(uint32_t)top_address;
    memset((uint8_t *)stage2_params, 0, sizeof(struct stage2_parameter));
    wolfBoot_printf("hoblist@0x%x" ENDLINE, (uint32_t)hobList);
    stage2_params->hobList = (uint32_t)hobList;


#ifdef WOLFBOOT_64BIT
    stage2_params->page_table = ((uint32_t)(stage2_params) -
        x86_paging_get_page_table_size());
    stage2_params->page_table = (((uint32_t)stage2_params->page_table) & ~((1 << 12) - 1));
    memset((uint8_t*)stage2_params->page_table, 0, x86_paging_get_page_table_size());
#endif /* WOLFBOOT_64BIT */

    change_stack_and_invoke(new_stack, memory_ready_entry,
                            (void*)stage2_params);

    wolfBoot_printf("FAIL" ENDLINE);
    panic();
}
