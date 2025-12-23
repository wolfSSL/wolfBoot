/* stage2_params.c
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
 */
#include <stage2_params.h>
#include <string.h>

/*
* The storage of the stage2_params variable during memory initialization
* involves several changes of locations. Initially, before memory becomes
* available, it is stored inside the Cache-As-RAM, more precisely as the stack
* variable `temp_params` in `boot_x86_fsp.c:start()`. Once memory initialization
* occurs in stage1, the stage2_params is copied into memory. In stage2, when
* writable data sections are available, stage2_params resides in the .data
* section.
*
* The function `stage2_get_parameters()` is utilized throughout the code to
* obtain the correct address of stage2_params. It's important to note that
* whenever the location changes, the structure is copied verbatim. References to
* the struct must be updated manually. Additionally, during the transition to
* stage2, all references to function pointers will not be valid anymore.
*
* Internals:
* During stage1, the pointer to the parameter is stored just before the IDT
* (Interrupt Descriptor Table) table, and it can be recovered using the sidt
* instruction. This necessitates the presence of a dummy table with a single
* NULL descriptor. In stage2, stage2_get_parameter() serves as a wrapper
* function that simply returns the address of the _stage2_parameter global
* variable.
*/

#if defined(WOLFBOOT_TPM_SEAL)
int stage2_get_tpm_policy(const uint8_t **policy, uint16_t *policy_sz)
{
#if defined(WOLFBOOT_FSP)
    struct stage2_parameter *p;
    p = stage2_get_parameters();
    *policy = (const uint8_t*)(uintptr_t)p->tpm_policy;
    *policy_sz = p->tpm_policy_size;
    return 0;
#else
#error "wolfBoot_get_tpm_policy is not implemented"
#endif
}
#endif /* WOLFBOOT_TPM_SEAL */
#if defined(BUILD_LOADER_STAGE1)
extern uint8_t _stage2_params[];

struct idt_descriptor {
    uint16_t limit;
    uint32_t base;
} __attribute__ ((packed));

/**
 * @brief Set the stage 2 parameter pointer during stage 1.
 *
 * @param p Pointer to the stage 2 parameter structure.
 * @param holder Pointer to the stage 2 parameter holder structure.
 *
 * This function sets the stage 2 parameter pointer during stage 1. The pointer
 * is stored just before a dummy IDTR table, that is defined inside the holder
 * struct.
*/
void stage2_set_parameters(struct stage2_parameter *p, struct stage2_ptr_holder *holder)
{
    struct idt_descriptor idt;

    idt.limit = sizeof(holder->dummy_idt) - 1;
    idt.base = (uint32_t)&holder->dummy_idt;
    memset(holder->dummy_idt, 0, sizeof(holder->dummy_idt));
    holder->ptr = p;

    asm ("lidt %0\r\n" : : "m"(idt));
}

/**
 * @brief Get the stage 2 parameter pointer during stage 1.
 *
 * This function gets the stage 2 parameter pointer during stage 1. The pointer
 * is stored just before a dummy IDTR table, stored in the IDT register.
 *
 * @return Pointer to the stage 2 parameter structure.
 */
struct stage2_parameter *stage2_get_parameters(void)
{
    struct stage2_parameter **ptr;
    struct idt_descriptor idt;

    asm ("sidt %0\r\n" : "=m"(idt) : );

    ptr = (struct stage2_parameter**)(uintptr_t)(idt.base - sizeof(struct stage2_parameter*));
    return *ptr;
}

/*!
 * \brief Set the stage 2 parameter for the WolfBoot bootloader.
 *
 * This static function sets the stage 2 parameter for the WolfBoot bootloader,
 * which will be used by the bootloader during its execution.
 *
 * \param p Pointer to the stage 2 parameter structure.
 */
void stage2_copy_parameter(struct stage2_parameter *p)
{
    memcpy((uint8_t*)&_stage2_params, (uint8_t*)p, sizeof(*p));
}
#else
/* must be global so the linker will export the symbol. It's used from loader 1
 * to fill the parameters */
struct stage2_parameter _stage2_params;

struct stage2_parameter *stage2_get_parameters(void)
{
    return &_stage2_params;
}
#endif
