/* stage2_params.h
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
#include <stage2_params.h>
#include <string.h>

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

void stage2_set_parameters(struct stage2_parameter *p, struct stage2_ptr_holder *holder)
{
    struct idt_descriptor idt;
    
    idt.limit = sizeof(holder->dummy_idt) - 1;
    idt.base = (uint32_t)&holder->dummy_idt;
    memset(holder->dummy_idt, 0, sizeof(holder->dummy_idt));
    holder->ptr = p;

    asm ("lidt %0\r\n" : : "m"(idt));
}

struct stage2_parameter *stage2_get_parameters()
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

struct stage2_parameter *stage2_get_parameters()
{
    return &_stage2_params;
}
#endif
