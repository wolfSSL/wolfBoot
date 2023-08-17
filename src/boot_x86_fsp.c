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
const uint8_t __attribute__((section(".sig_fsp_s")))
    empty_sig_fsp_s[HEADER_SIZE] = {};
const uint8_t __attribute__((section(".sig_wolfboot_raw")))
    empty_sig_wolfboot_raw[HEADER_SIZE] = {};
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
extern uint8_t _start_fsp_m[];
extern uint8_t _fsp_s_hdr[];
extern uint8_t _end_fsp_m[];
extern uint8_t _end_fsp_s[];
extern uint8_t _wolfboot_flash_start[];
extern uint8_t _wolfboot_flash_end[];
extern uint8_t wb_end_bss[], wb_start_bss[];
extern int main(void);

/*!
 * \brief Get the top address from the EFI HOB (Hand-Off Block) list.
 *
 * This function retrieves the top address from the EFI Hand-Off Block (HOB) list
 * and stores it in the 'top' parameter.
 *
 * \param top Pointer to a variable where the top address will be stored.
 * \param hoblist Pointer to the EFI HOB list.
 * \return 0 if the top address is successfully retrieved, -1 otherwise.
 */
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

/*!
 * \brief Change the stack and invoke a function with the new stack.
 *
 * This function changes the stack to the specified 'new_stack' value and then
 * calls the function pointed to by 'other_func', passing the 'ptr' parameter as an argument.
 *
 * \param new_stack The new stack address.
 * \param other_func Pointer to the function to be invoked with the new stack.
 * \param ptr Pointer to the parameter to be passed to the invoked function.
 */
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
/*!
 * \brief Load the WolfBoot bootloader into memory.
 *
 * This static function loads the WolfBoot bootloader into memory at the specified
 * address (WOLFBOOT_LOAD_BASE) from the flash memory.
 */
static void load_wolfboot(void)
{
    size_t wolfboot_size, bss_size;
    wolfBoot_printf("loading wolfboot at %x..." ENDLINE,
                    (uint32_t)WOLFBOOT_LOAD_BASE - IMAGE_HEADER_SIZE);
    wolfboot_size = _wolfboot_flash_end - _wolfboot_flash_start;
    memcpy((uint8_t*)WOLFBOOT_LOAD_BASE - IMAGE_HEADER_SIZE,
            _wolfboot_flash_start, wolfboot_size);
    bss_size = wb_end_bss - wb_start_bss;
    memset(wb_start_bss, 0, bss_size);
    wolfBoot_printf("load wolfboot end" ENDLINE);
}

static void load_fsp_s_to_ram(void)
{
    size_t fsp_s_size;
    wolfBoot_printf("loading FSP_S at %x..." ENDLINE,
                    (uint32_t)(FSP_S_LOAD_BASE - IMAGE_HEADER_SIZE));
    fsp_s_size = _end_fsp_s - _fsp_s_hdr;
    memcpy((uint8_t*)FSP_S_LOAD_BASE - IMAGE_HEADER_SIZE,
            _fsp_s_hdr, fsp_s_size);
}

extern uint8_t _stage2_params[];

/*!
 * \brief Set the stage 2 parameter for the WolfBoot bootloader.
 *
 * This static function sets the stage 2 parameter for the WolfBoot bootloader,
 * which will be used by the bootloader during its execution.
 *
 * \param p Pointer to the stage 2 parameter structure.
 */
static void set_stage2_parameter(struct stage2_parameter *p)
{
    memcpy((uint8_t*)_stage2_params, (uint8_t*)p, sizeof(*p));
}

#ifdef WOLFBOOT_64BIT
/*!
 * \brief Jump into the WolfBoot bootloader.
 *
 * This static function transfers control to the WolfBoot bootloader by calling
 * the main() function or switch_to_long_mode() for 64-bit systems.
 */
static void jump_into_wolfboot(void)
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
/*!
 * \brief Check if the payload is valid.
 *
 * This static function checks if the given payload is valid by verifying
 * its signature.
 *
 * \param base_addr Pointer to the payload
 * \return 0 if the payload is successfully retrieved, -1 otherwise.
 */
static inline int verify_payload(uint8_t *base_addr)
{
    int ret = -1;
    struct wolfBoot_image wb_img;
    memset(&wb_img, 0, sizeof(struct wolfBoot_image));
    ret = wolfBoot_open_image_address(&wb_img, base_addr);
    if (ret < 0) {
        wolfBoot_printf("verify_payload: Failed to open image" ENDLINE);
        panic();
    }
    wolfBoot_printf("verify_payload: image open successfully." ENDLINE);
    ret = wolfBoot_verify_integrity(&wb_img);
    if (ret < 0) {
        wolfBoot_printf("verify_payload: Failed integrity check" ENDLINE);
        panic();
    }
    wolfBoot_printf("verify_payload: integrity OK. Checking signature." ENDLINE);
    ret = wolfBoot_verify_authenticity(&wb_img);
    if (ret < 0) {
        wolfBoot_printf("verify_payload: Failed signature check" ENDLINE);
        panic();
    }
    return ret;
}
/*!
 * \brief Entry point after memory initialization.
 *
 * This static function serves as the entry point for further execution after the
 * memory initialization is completed.
 *
 * \param ptr Pointer to a parameter structure.
 */
static void memory_ready_entry(void *ptr)
{
    struct stage2_parameter *stage2_params = (struct stage2_parameter *)ptr;
    uint8_t silicon_init_parameter[FSP_S_PARAM_SIZE];
    struct fsp_info_header *fsp_info_header;
    temp_ram_exit_cb TempRamExit;
    silicon_init_cb SiliconInit;
    notify_phase_cb notifyPhase;
    NOTIFY_PHASE_PARAMS param;
    uint32_t info[4];
    uint32_t status;
    unsigned int i;
    int ret;
    uint8_t *fsp_s_base;
    uint8_t *fsp_m_base;

    fsp_m_base = _start_fsp_m;
    fsp_s_base = (uint8_t *)(FSP_S_LOAD_BASE);

    fsp_info_header =
        (struct fsp_info_header *)(fsp_m_base + FSP_INFO_HEADER_OFFSET);
    TempRamExit = (temp_ram_exit_cb)(fsp_m_base +
                                     fsp_info_header->TempRamExitEntryOffset);
    status = TempRamExit(NULL);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("temp ram exit failed" ENDLINE);
        panic();
    }

    /* Load FSP_S to RAM */
    load_fsp_s_to_ram();

#ifdef STAGE1_AUTH
    /* Verify FSP_S */
    wolfBoot_printf("Authenticating FSP_S at %x..." ENDLINE,
            fsp_s_base - IMAGE_HEADER_SIZE);

    if (verify_payload(fsp_s_base - IMAGE_HEADER_SIZE) == 0)
        wolfBoot_printf("FSP_S: verified OK." ENDLINE);
    else {
        panic();
    }
#endif

    memcpy(silicon_init_parameter, fsp_s_base + fsp_info_header->CfgRegionOffset,
            FSP_S_PARAM_SIZE);
    status = fsp_machine_update_s_parameters(silicon_init_parameter);

    fsp_info_header =
        (struct fsp_info_header *)(fsp_s_base + FSP_INFO_HEADER_OFFSET);
    SiliconInit = (silicon_init_cb)(fsp_s_base +
                                    fsp_info_header->FspSiliconInitEntryOffset);

    wolfBoot_printf("call silicon..." ENDLINE);
    status = SiliconInit(silicon_init_parameter);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("failed %x\n", status);
        panic();
    }
    wolfBoot_printf("success" ENDLINE);
    pci_enum_do();
    notifyPhase = (notify_phase_cb)(fsp_s_base +
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
    cpuid(0, &info[0], &info[1], &info[2], NULL);
    wolfBoot_printf("CPUID(0):%x %x %x\r\n", info[0], info[1], info[2]);
    load_wolfboot();

#ifdef STAGE1_AUTH
    /* Verify wolfBoot */
    wolfBoot_printf("Authenticating wolfboot at %x..." ENDLINE,
            WOLFBOOT_LOAD_BASE);
    if (verify_payload((uint8_t *)WOLFBOOT_LOAD_BASE - IMAGE_HEADER_SIZE) == 0)
        wolfBoot_printf("wolfBoot: verified OK." ENDLINE);
    else {
        panic();
    }
#endif
    set_stage2_parameter(stage2_params);
    jump_into_wolfboot();
}

/*!
 * \brief Check if the FSP info header is valid.
 *
 * This static function checks if the given FSP info header is valid by verifying
 * its signature.
 *
 * \param hdr Pointer to the FSP info header structure.
 * \return 1 if the FSP info header is valid, 0 otherwise.
 */
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

/*!
 * \brief Entry point for the FSP-M (Firmware Support Package - Memory) module.
 *
 * This function serves as the entry point for the FSP-M module, which is executed
 * during the boot process. It takes the stack base, stack top, timestamp, and BIST
 * (Built-In Self Test) as input arguments.
 *
 * \param stack_base The base address of the stack.
 * \param stack_top The top address of the stack.
 * \param timestamp A timestamp value.
 * \param bist Built-In Self Test value.
 */
void start(uint32_t stack_base, uint32_t stack_top, uint64_t timestamp,
           uint32_t bist)
{
    uint8_t udp_m_parameter[FSP_M_UDP_MAX_SIZE], *udp_m_default;
    struct fsp_info_header *fsp_m_info_header;
    struct stage2_parameter *stage2_params;
    uint8_t *fsp_m_base, done = 0;
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
    fsp_m_base = (uint8_t *)(_start_fsp_m);


    status = post_temp_ram_init_cb();
    if (status != 0) {
        wolfBoot_printf("post temp ram init cb failed" ENDLINE);
        panic();
    }
    wolfBoot_printf("Cache-as-RAM initialized" ENDLINE);

    fsp_m_info_header =
        (struct fsp_info_header *)(fsp_m_base + FSP_INFO_HEADER_OFFSET);
    udp_m_default = fsp_m_base + fsp_m_info_header->CfgRegionOffset;
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
    MemoryInit = (memory_init_cb)(fsp_m_base +
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
