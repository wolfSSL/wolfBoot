/* boot_x86_fsp.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
#include <stage2_params.h>

#include "wolfboot/wolfboot.h"
#include "image.h"
#ifdef WOLFBOOT_TPM
#include <loader.h>
#include <tpm.h>
#endif


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
#define FSP_M_CAR_MEM_SIZE 0x50000
#endif

/* offset of the header from the base image  */
#define FSP_INFO_HEADER_OFFSET 0x94
#define EFI_SUCCESS 0x0
#define FSP_STATUS_RESET_REQUIRED_COLD  0x40000001
#define FSP_STATUS_RESET_REQUIRED_WARM  0x40000002
#define MEMORY_4GB (4ULL * 1024 * 1024 * 1024)
#define ENDLINE "\r\n"

#define PCI_DEVICE_CONTROLLER_TO_PEX 0x6
#define PCIE_TRAINING_TIMEOUT_MS (100)

typedef uint32_t (*memory_init_cb)(void *udp, struct efi_hob **HobList);
typedef uint32_t (*temp_ram_exit_cb)(void *udp);
typedef uint32_t (*silicon_init_cb)(void *udp);
typedef uint32_t (*notify_phase_cb)(NOTIFY_PHASE_PARAMS *p);

/* need to be implemented by machine dependent code */
int fsp_machine_update_m_parameters(uint8_t *default_m_params,
                                    uint32_t mem_base, uint32_t mem_size);
int fsp_machine_update_s_parameters(uint8_t *default_s_params);
int post_temp_ram_init_cb(void);
int fsp_pre_mem_init_cb(void);
int fsp_pre_silicon_init_cb(void);

/* from the linker */
extern uint8_t _start_fsp_t[];
extern uint8_t _start_fsp_m[];
extern uint8_t _fsp_s_hdr[];
extern uint8_t _end_fsp_m[];
extern uint8_t _end_fsp_s[];
extern uint8_t _wolfboot_flash_start[];
extern uint8_t _wolfboot_flash_end[];
extern uint8_t wb_end_bss[], wb_start_bss[];
extern uint8_t _stored_data[], _start_data[], _end_data[];
extern uint8_t _start_bss[], _end_bss[];
extern const uint8_t _start_policy[], _end_policy[];
extern const uint32_t _policy_size_u32[];
extern const uint8_t _start_keystore[];

/* wolfboot symbol */
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
                                    void (*other_func)(void))
{
    __asm__ volatile("movl %0, %%eax\n"
                     "mov %%eax, %%esp\n"
                     "call *%1\n"
                     :
                     : "r"(new_stack), "r"(other_func)
                     : "%eax");
}
static int range_overlaps(uint32_t start1, uint32_t end1, uint32_t start2,
                          uint32_t end2)
{
    return !(end1 <= start2 || end2 <= start1);
}

static int check_memory_ranges()
{
    uint32_t wb_start, wb_end;

    wb_start = (uint32_t)WOLFBOOT_LOAD_BASE - IMAGE_HEADER_SIZE;
    wb_end = wb_start + (_wolfboot_flash_end - _wolfboot_flash_start);
    if (range_overlaps(wb_start, wb_end, (uint32_t)_start_data,
                       (uint32_t)_end_data))
        return -1;
    if (range_overlaps(wb_start, wb_end, (uint32_t)_start_bss,
                       (uint32_t)_end_bss))
        return -1;
    if (range_overlaps((uint32_t)wb_start_bss,
                       (uint32_t)wb_end_bss,
                       (uint32_t)_start_data,
                       (uint32_t)_end_data))
        return -1;
    if (range_overlaps((uint32_t)wb_start_bss,
                       (uint32_t)wb_end_bss,
                       (uint32_t)_start_bss,
                       (uint32_t)_end_bss))
        return -1;
    return 0;
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
    uint32_t wolfboot_start;

    if (check_memory_ranges() != 0) {
        wolfBoot_printf("wolfboot overlaps with loader data...stop" ENDLINE);
        panic();
    }

    wolfboot_start = (uint32_t)WOLFBOOT_LOAD_BASE - IMAGE_HEADER_SIZE;
    wolfboot_size = _wolfboot_flash_end - _wolfboot_flash_start;
    x86_log_memory_load(wolfboot_start, wolfboot_start + wolfboot_size,
                        "wolfboot");
    memcpy((uint8_t*)wolfboot_start,_wolfboot_flash_start, wolfboot_size);
    bss_size = wb_end_bss - wb_start_bss;
    x86_log_memory_load((uint32_t)(uintptr_t)wb_start_bss,
                        (uint32_t)(uintptr_t)(wb_start_bss + bss_size),
                        "wolfboot .bss");
    memset(wb_start_bss, 0, bss_size);
    wolfBoot_printf("load wolfboot end" ENDLINE);
}

static void load_fsp_s_to_ram(void)
{
    size_t fsp_s_size;
    uint32_t fsp_start;
    fsp_start = FSP_S_LOAD_BASE - IMAGE_HEADER_SIZE;
    fsp_s_size = _end_fsp_s - _fsp_s_hdr;
    x86_log_memory_load(fsp_start, fsp_start + fsp_s_size, "FSPS");
    memcpy((uint8_t*)fsp_start, _fsp_s_hdr, fsp_s_size);
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
    struct stage2_parameter *params;
    uint32_t cr3;
    int ret;

    params = stage2_get_parameters();
    ret = x86_paging_build_identity_mapping(MEMORY_4GB,
                                            (uint8_t*)(uintptr_t)params->page_table);
    if (ret != 0) {
        wolfBoot_printf("can't build identity mapping\r\n");
        panic();
    }

    stage2_copy_parameter(params);
    wolfBoot_printf("starting wolfboot 64bit\r\n");
    switch_to_long_mode((uint64_t*)&main, params->page_table);
    panic();
}
#else
static void jump_into_wolfboot(void)
{
    struct stage2_parameter *params = stage2_get_parameters();
    stage2_copy_parameter(params);
    main();
}
#endif /* WOLFBOOT_64BIT */

#if defined(WOLFBOOT_MEASURED_BOOT)
/* The image needs to be already verified */
int wolfBoot_image_measure(uint8_t *image)
{
    uint16_t hash_len;
    uint8_t *hash;

    hash_len = wolfBoot_find_header(image + IMAGE_HEADER_OFFSET,
                                    WOLFBOOT_SHA_HDR, &hash);
    wolfBoot_print_hexstr(hash, hash_len, 0);
    return wolfBoot_tpm2_extend(WOLFBOOT_MEASURED_PCR_A, hash, __LINE__);
}
#endif /* WOLFBOOT_MEASURED_BOOT */

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
 * \brief Initialization of .data and .bss sections after memory initialization.
 *
 * This static function copies initial values for .data to the corresponding
 * section in the linker script, and initializes the .bss section to zero.
 *
 * This function is called after memory initialization is completed and the stack
 * has been remapped.
 *
 */
static inline void memory_init_data_bss(void)
{
    uint32_t *datamem_p;
    uint32_t *dataflash_p;
    x86_log_memory_load((uint32_t)(uintptr_t)_start_data,
                         (uint32_t)(uintptr_t)_end_data, "stage1 .data");
    datamem_p = (uint32_t *)_start_data;
    dataflash_p = (uint32_t *)_stored_data;
    while(datamem_p < (uint32_t *)_end_data) {
        *(datamem_p++) = *(dataflash_p++);
    }
    x86_log_memory_load((uint32_t)(uintptr_t)_start_bss,
                          (uint32_t)(uintptr_t)_end_bss, "stage1 .bss");
    memset(_start_bss, 0, (_end_bss - _start_bss));
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

static int fsp_get_image_revision(struct fsp_info_header *h, int *build,
                                  int *rev, int *maj, int *min)
{
    uint16_t ext_revision;
    uint32_t revision;

    if (!fsp_info_header_is_ok(h)) {
        wolfBoot_printf("Wrong FSP Header\r\n");
        return -1;
    }

    revision = h->ImageRevision;

    *build = revision & 0xff;
    *rev = (revision >> 8) & 0xff;
    *min = (revision >> 16) & 0xff;
    *maj = (revision >> 24) & 0xff;

    if (h->HeaderRevision >= 6) {
        *build = *build | ((h->ExtendedImageRevision & 0xff) << 8);
        *rev = *rev | (h->ExtendedImageRevision & 0xff00);
    }

    return 0;
}

static void print_fsp_image_revision(struct fsp_info_header *h)
{
    int build, rev, maj, min;
    int r;
    r = fsp_get_image_revision(h, &build, &rev, &maj, &min);
    if (r != 0) {
        wolfBoot_printf("failed to get fsp image revision\r\n");
        return;
    }
    wolfBoot_printf("%x.%x.%x build %x\r\n", maj, min, rev, build);
}

static int pci_get_capability(uint8_t bus, uint8_t dev, uint8_t fun,
                              uint8_t cap_id, uint8_t *cap_off)
{
    uint8_t r8, id;
    uint32_t r32;

    r32 = pci_config_read16(bus, dev, fun, PCI_STATUS_OFFSET);
    if (!(r32 & PCI_STATUS_CAP_LIST))
        return -1;
    r8 = pci_config_read8(bus, dev, fun, PCI_CAP_OFFSET);
    while (r8 != 0) {
        id = pci_config_read8(bus, dev, fun, r8);
        if (id == cap_id) {
            *cap_off = r8;
            return 0;
        }
        r8 = pci_config_read8(bus, dev, fun, r8 + 1);
    }
    return -1;
}

int pcie_retraining_link(uint8_t bus, uint8_t dev, uint8_t fun)
{
    uint16_t link_status, link_control, vid;
    uint8_t pcie_cap_off;
    int ret, tries;

    vid = pci_config_read16(bus, dev, 0, PCI_VENDOR_ID_OFFSET);
    if (vid == 0xffff) {
        return -1;
    }
    
    ret = pci_get_capability(bus, dev, fun, PCI_PCIE_CAP_ID, &pcie_cap_off);
    if (ret != 0) {
        return -1;
    }

    link_status = pci_config_read16(bus, dev, fun,
                                    pcie_cap_off + PCIE_LINK_STATUS_OFF);
    if (link_status & PCIE_LINK_STATUS_TRAINING) {
        delay(PCIE_TRAINING_TIMEOUT_MS);
        link_status = pci_config_read16(bus, dev, fun,
                                        pcie_cap_off + PCIE_LINK_STATUS_OFF);
        if (link_status & PCIE_LINK_STATUS_TRAINING) {
            return -1;
        }
    }

    link_control = pci_config_read16(bus, dev, fun,
                                         pcie_cap_off + PCIE_LINK_CONTROL_OFF);
    link_control |= PCIE_LINK_CONTROL_RETRAINING;
    pci_config_write16(bus, dev, fun, pcie_cap_off + PCIE_LINK_CONTROL_OFF,
                       link_control);
    tries = PCIE_TRAINING_TIMEOUT_MS / 10;
    do {
        link_status = pci_config_read16(bus, dev, fun,
                                        pcie_cap_off + PCIE_LINK_STATUS_OFF);
        if (!(link_status & PCIE_LINK_STATUS_TRAINING))
            break;
        delay(10);
    } while(tries--);

    if ((link_status & PCIE_LINK_STATUS_TRAINING)) {
        return -1;
    }

    return 0;
}

/*!
 * \brief Staging of FSP_S after verification
 *
 * Setpu the parameters and call FSP Silicon Initialization.
 *
 * \param fsp_info FSP information header
 * \param fsp_s_base the area in RAM where FSP_S has been loaded and verified
 * \return EFI_SUCCESS in case of success, -1 otherwise
 */
static int fsp_silicon_init(struct fsp_info_header *fsp_info, uint8_t *fsp_s_base)
{
    uint8_t silicon_init_parameter[FSP_S_PARAM_SIZE];
    silicon_init_cb SiliconInit;
    notify_phase_cb notifyPhase;
    NOTIFY_PHASE_PARAMS param;
    uint32_t status;
    unsigned int i;
    int ret;

    memcpy(silicon_init_parameter, fsp_s_base + fsp_info->CfgRegionOffset,
            FSP_S_PARAM_SIZE);
    status = fsp_machine_update_s_parameters(silicon_init_parameter);
    SiliconInit = (silicon_init_cb)(fsp_s_base + fsp_info->FspSiliconInitEntryOffset);

#if defined(WOLFBOOT_DUMP_FSP_UPD)
    wolfBoot_printf("Dumping fsps upd (%d bytes)" ENDLINE, (int)fsp_info->CfgRegionSize);
    wolfBoot_print_hexstr(silicon_init_parameter, fsp_info->CfgRegionSize, 16);
#endif
    status = fsp_pre_silicon_init_cb();
    if (status != 0) {
        wolfBoot_printf("pre silicon init cb returns %d", status);
        panic();
    }
    wolfBoot_printf("call silicon..." ENDLINE);
    status = SiliconInit(silicon_init_parameter);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("failed %x\n", status);
        return -1;
    }
    wolfBoot_printf("success" ENDLINE);
    status = pcie_retraining_link(0, PCI_DEVICE_CONTROLLER_TO_PEX, 0);
    if (status != 0)
        wolfBoot_printf("pcie retraining failed %x\n", status);

    pci_enum_do();
    pci_dump_config_space();
    notifyPhase = (notify_phase_cb)(fsp_s_base +
                                        fsp_info->NotifyPhaseEntryOffset);
    param.Phase = EnumInitPhaseAfterPciEnumeration;
    status = notifyPhase(&param);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("failed %d: %x\n", __LINE__, status);
        return -1;
    }
    param.Phase = EnumInitPhaseReadyToBoot;
    status = notifyPhase(&param);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("failed %d: %x\n", __LINE__, status);
        return -1;
    }
    param.Phase = EnumInitPhaseEndOfFirmware;
    status = notifyPhase(&param);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("failed %d: %x\n", __LINE__, status);
        return -1;
    }
    return EFI_SUCCESS;
}

#if defined(TARGET_x86_fsp_qemu) && defined(WOLFBOOT_MEASURED_BOOT)
/*!
 * \brief Extend the PCR with stage1 compoments
 *
 * This function calculates the SHA-256 hash differents compoment of the
 * bootloader: keystore, stage1 code, reset vector, FSP_T, FSP_M and FSP_S. The
 * layout of these components in the flash is consecutive, it start at keystore
 * up to the end of the flash, that is at 4GB.
 *
 *
 * \return 0 on success, error code on failure
 */
static int self_extend_pcr(void)
{
    uint8_t hash[SHA256_DIGEST_SIZE];
    uint32_t blksz, position = 0;
    wc_Sha256 sha256_ctx;
    uint32_t sz;
    uintptr_t p;

    p  = (uintptr_t)_start_keystore;
    /* The flash is memory mapped so that it ends at 4GB */
    sz = ((MEMORY_4GB) - (uint64_t)p);
    wc_InitSha256(&sha256_ctx);
    do {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if (position + blksz > sz)
            blksz = sz - position;
        wc_Sha256Update(&sha256_ctx, (uint8_t*)p, blksz);
        position += blksz;
        p += blksz;
    } while (position < sz);
    wc_Sha256Final(&sha256_ctx, hash);
    wolfBoot_print_hexstr(hash, SHA256_DIGEST_SIZE, 0);
    return wolfBoot_tpm2_extend(WOLFBOOT_MEASURED_PCR_A, hash, __LINE__);
}
#endif

/*!
 * \brief Entry point after memory initialization.
 *
 * This static function serves as the entry point for further execution after the
 * memory initialization is completed and the stack has been remapped.
 *
 */
static void memory_ready_entry(void)
{
    struct fsp_info_header *fsp_info;
    temp_ram_exit_cb TempRamExit;
    uint8_t *fsp_s_base;
    uint8_t *fsp_m_base;
    uint32_t cpu_info[4];
    uint32_t status;
    int ret;

    /* FSP_M is located in flash */
    fsp_m_base = _start_fsp_m;
    /* fsp_s is loaded to RAM for validation */
    fsp_s_base = (uint8_t *)(FSP_S_LOAD_BASE);
    fsp_info =
        (struct fsp_info_header *)(fsp_m_base + FSP_INFO_HEADER_OFFSET);
    TempRamExit = (temp_ram_exit_cb)(fsp_m_base +
            fsp_info->TempRamExitEntryOffset);
    status = TempRamExit(NULL);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("temp ram exit failed" ENDLINE);
        panic();
    }
    /* Confirmed memory initialization complete.
     * TempRamExit was successful.
     *
     * Copy .data section to RAM and initialize .bss
     */
    memory_init_data_bss();

#if (defined(WOLFBOOT_MEASURED_BOOT)) || \
    (defined(STAGE1_AUTH) && defined (WOLFBOOT_TPM) && defined(WOLFBOOT_TPM_VERIFY))
    wolfBoot_printf("Initializing WOLFBOOT_TPM" ENDLINE);
    ret = wolfBoot_tpm2_init();
    if (ret != 0) {
        wolfBoot_printf("tpm init failed" ENDLINE);
        panic();
    }

    ret = wolfBoot_tpm_self_test();
    if (ret != 0) {
        wolfBoot_printf("tpm self test failed" ENDLINE);
        panic();
    }
#endif

#if (defined(TARGET_x86_fsp_qemu) && defined(WOLFBOOT_MEASURED_BOOT))
    ret = self_extend_pcr();
    if (ret != 0)
        wolfBoot_printf("fail to extend PCR" ENDLINE);
#endif

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

#if defined(WOLFBOOT_MEASURED_BOOT)
    ret = wolfBoot_image_measure((uint8_t*)fsp_s_base - IMAGE_HEADER_SIZE);
    if (ret != 0) {
        wolfBoot_printf("Fail to measure FSP_S image\r\n");
        panic();
    }
#endif /* WOLFBOOT_MEASURED_BOOT */

    /* Call FSP_S initialization */
    fsp_info =
        (struct fsp_info_header *)(fsp_s_base + FSP_INFO_HEADER_OFFSET);
    wolfBoot_printf("FSP-S:");
    print_fsp_image_revision((struct fsp_info_header *)fsp_info);
    if (fsp_silicon_init(fsp_info, fsp_s_base) != EFI_SUCCESS)
        panic();
    /* Get CPUID */
    cpuid(0, &cpu_info[0], &cpu_info[1], &cpu_info[2], NULL);
    wolfBoot_printf("CPUID(0):%x %x %x\r\n", cpu_info[0], cpu_info[1], cpu_info[2]);
    /* Load stage2 wolfBoot to RAM */
    load_wolfboot();
#ifdef STAGE1_AUTH
    /* Verify stage2 wolfBoot */
    wolfBoot_printf("Authenticating wolfboot at %x..." ENDLINE,
            WOLFBOOT_LOAD_BASE);
    if (verify_payload((uint8_t *)WOLFBOOT_LOAD_BASE - IMAGE_HEADER_SIZE) == 0)
        wolfBoot_printf("wolfBoot: verified OK." ENDLINE);
    else {
        panic();
    }
#endif

#if defined(WOLFBOOT_MEASURED_BOOT)
    ret = wolfBoot_image_measure((uint8_t*)WOLFBOOT_LOAD_BASE
                                 - IMAGE_HEADER_SIZE);
    if (ret != 0) {
        wolfBoot_printf("Fail to measure WOLFBOOT image\r\n");
        panic();
    }
#endif /* WOLFBOOT_MEASURED_BOOT */

#if (defined(WOLFBOOT_MEASURED_BOOT)) || \
    (defined(STAGE1_AUTH) && defined (WOLFBOOT_TPM) && defined(WOLFBOOT_TPM_VERIFY))
    wolfBoot_tpm2_deinit();
#endif
    /* Finalize staging to stage2 */
    jump_into_wolfboot();
}

static void print_ucode_revision(void)
{
#if !defined(TARGET_x86_fsp_qemu)
    /* incomplete */
    struct ucode_header {
        uint32_t header_version;
        uint32_t update_revision;
        uint32_t date;
        /* other fields not needed */
    } __attribute__((packed));
    struct ucode_header *h;

    h = (struct ucode_header *)UCODE0_ADDRESS;
    wolfBoot_printf("microcode revision: %x, date: %x-%x-%x\r\n",
                    (int)h->update_revision,
                    (int)((h->date >> 24) & 0xff), /* month */
                    (int)((h->date >> 16) & 0xff), /* day */
                    (int)(h->date & 0xffff)); /* year */
#else
    wolfBoot_printf("no microcode for QEMU target\r\n");
#endif
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
    struct stage2_ptr_holder *mem_stage2_holder;
    struct fsp_info_header *fsp_m_info_header;
    struct stage2_parameter *stage2_params;
    struct stage2_ptr_holder stage2_holder;
    struct stage2_parameter temp_params;
    uint8_t *fsp_m_base, done = 0;
    struct efi_hob *hobList, *it;
    memory_init_cb MemoryInit;
    uint64_t top_address = MEMORY_4GB;
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
    memset(&temp_params, 0, sizeof(temp_params));

    stage2_set_parameters(&temp_params, &stage2_holder);
    wolfBoot_printf("Cache-as-RAM initialized" ENDLINE);

    wolfBoot_printf("FSP-T:");
    print_fsp_image_revision((struct fsp_info_header *)
                             (_start_fsp_t + FSP_INFO_HEADER_OFFSET));
    wolfBoot_printf("FSP-M:");
    print_fsp_image_revision((struct fsp_info_header *)
                             (_start_fsp_m + FSP_INFO_HEADER_OFFSET));

    print_ucode_revision();

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

#if defined(WOLFBOOT_DUMP_FSP_UPD)
    wolfBoot_printf("Dumping fspm udp (%d bytes)" ENDLINE, (int)fsp_m_info_header->CfgRegionSize);
    wolfBoot_print_hexstr(udp_m_parameter, fsp_m_info_header->CfgRegionSize, 16);
#endif
    status = fsp_pre_mem_init_cb();
    if (status != 0) {
        wolfBoot_printf("pre mem init cb returns %d", status);
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

#ifdef DEBUG
    hob_dump_memory_map(hobList);
#endif /* DEBUG */

    if (top_address >= MEMORY_4GB) {
        panic();
    }

    new_stack = top_address;
    x86_log_memory_load(new_stack - WOLFBOOT_X86_STACK_SIZE, new_stack, "stack");
    x86_log_memory_load(new_stack - WOLFBOOT_X86_STACK_SIZE - sizeof(struct stage2_parameter), 
                        new_stack - WOLFBOOT_X86_STACK_SIZE, "stage2 parameter");
    top_address =
        new_stack - WOLFBOOT_X86_STACK_SIZE - sizeof(struct stage2_parameter);
    stage2_params = (struct stage2_parameter *)(uint32_t)top_address;
    memcpy((uint8_t *)stage2_params, (uint8_t*)&temp_params, sizeof(struct stage2_parameter));
    wolfBoot_printf("hoblist@0x%x" ENDLINE, (uint32_t)hobList);
    stage2_params->hobList = (uint32_t)hobList;

#ifdef WOLFBOOT_64BIT
    stage2_params->page_table = ((uint32_t)(top_address) -
        x86_paging_get_page_table_size());
    stage2_params->page_table = (((uint32_t)stage2_params->page_table) & ~((1 << 12) - 1));
    x86_log_memory_load(stage2_params->page_table, top_address, "page tables");
    memset((uint8_t*)stage2_params->page_table, 0, x86_paging_get_page_table_size());
    wolfBoot_printf("page table @ 0x%x [length: %x]" ENDLINE, (uint32_t)stage2_params->page_table, x86_paging_get_page_table_size());
    top_address = stage2_params->page_table;
#endif /* WOLFBOOT_64BIT */
    x86_log_memory_load(top_address - sizeof(struct stage2_ptr_holder), top_address, "stage2 ptr holder");
    top_address = top_address - sizeof(struct stage2_ptr_holder);
    mem_stage2_holder = (struct stage2_ptr_holder*)(uintptr_t)top_address;

    stage2_params->tolum = top_address;

#ifdef WOLFBOOT_TPM_SEAL
    stage2_params->tpm_policy = (uint32_t)_start_policy;

    stage2_params->tpm_policy_size = *_policy_size_u32;
    if (stage2_params->tpm_policy_size > _end_policy - _start_policy)
        stage2_params->tpm_policy_size = 0;
    wolfBoot_printf("setting policy @%x (%d bytes)\r\n",
                    (uint32_t)(uintptr_t)stage2_params->tpm_policy,
                    stage2_params->tpm_policy_size);
#endif

    stage2_set_parameters(stage2_params, mem_stage2_holder);
    wolfBoot_printf("TOLUM: 0x%x\r\n", stage2_params->tolum);
    /* change_stack_and_invoke() never returns.
     *
     * Execution here is eventually transferred to memory_ready_entry
     * after the stack has been remapped.
     */
    change_stack_and_invoke(new_stack, memory_ready_entry);

    /* Returning from change_stack_and_invoke() implies a fatal error
     * while attempting to remap the stack.
     */
    wolfBoot_printf("FAIL" ENDLINE);
    panic();
}
