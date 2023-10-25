/* stage1.h
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
#include <stage1.h>

/* must be global so the linker will export the symbol. It's used from loader 1
 * to fill the parameters */
struct stage2_parameter _stage2_params;

struct stage2_parameter *stage2_get_parameters()
{
    return &_stage2_params;
}

#if defined(WOLFBOOT_TPM_SEAL)
int stage2_get_tpm_policy(const uint8_t **policy, uint16_t *policy_sz)
{
#if defined(WOLFBOOT_FSP) && !defined(BUILD_LOADER_STAGE1)
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
