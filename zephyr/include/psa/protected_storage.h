/* protected_storage.h
 *
 * Minimal PSA protected storage definitions for wolfBoot Zephyr integration.
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef WOLFBOOT_PSA_PROTECTED_STORAGE_H_
#define WOLFBOOT_PSA_PROTECTED_STORAGE_H_

#include <stddef.h>
#include <stdint.h>
#include "psa/error.h"

typedef uint64_t psa_storage_uid_t;
typedef uint32_t psa_storage_create_flags_t;

struct psa_storage_info_t {
    size_t capacity;
    size_t size;
    psa_storage_create_flags_t flags;
};

#ifndef PSA_STORAGE_FLAG_WRITE_ONCE
#define PSA_STORAGE_FLAG_WRITE_ONCE ((psa_storage_create_flags_t)0x00000001)
#endif

psa_status_t psa_ps_set(psa_storage_uid_t uid,
                        size_t data_length,
                        const void *p_data,
                        psa_storage_create_flags_t create_flags);
psa_status_t psa_ps_get(psa_storage_uid_t uid,
                        size_t data_offset,
                        size_t data_size,
                        void *p_data,
                        size_t *p_data_length);
psa_status_t psa_ps_get_info(psa_storage_uid_t uid,
                             struct psa_storage_info_t *p_info);
psa_status_t psa_ps_remove(psa_storage_uid_t uid);
uint32_t psa_ps_get_support(void);

#endif /* WOLFBOOT_PSA_PROTECTED_STORAGE_H_ */
