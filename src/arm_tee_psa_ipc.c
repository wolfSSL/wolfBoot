/* arm_tee_psa_ipc.c
 *
 * PSA IPC hooks for ARM TEE style veneers.
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#include <psa/crypto.h>
#include <psa/error.h>

#include <wolfssl/wolfcrypt/types.h>
#include <wolfboot/arm_tee_api.h>

/* Service IDs/handles aligned with ARM TEE defaults. */
#define ARM_TEE_CRYPTO_SID   (0x00000080U)
#define ARM_TEE_CRYPTO_HANDLE (1U)
#define ARM_TEE_PROTECTED_STORAGE_SID    (0x00000060U)
#define ARM_TEE_PROTECTED_STORAGE_HANDLE (2U)
#define ARM_TEE_ATTESTATION_SID          (0x00000020U)
#define ARM_TEE_ATTESTATION_HANDLE       (4U)

/* Minimal ARM TEE crypto pack definitions (subset used by wolfBoot). */
#define ARM_TEE_CRYPTO_GENERATE_RANDOM_SID          (0x0100U)
#define ARM_TEE_CRYPTO_GET_KEY_ATTRIBUTES_SID       (0x0200U)
#define ARM_TEE_CRYPTO_OPEN_KEY_SID                 (0x0201U)
#define ARM_TEE_CRYPTO_CLOSE_KEY_SID                (0x0202U)
#define ARM_TEE_CRYPTO_IMPORT_KEY_SID               (0x0203U)
#define ARM_TEE_CRYPTO_DESTROY_KEY_SID              (0x0204U)
#define ARM_TEE_CRYPTO_EXPORT_KEY_SID               (0x0205U)
#define ARM_TEE_CRYPTO_EXPORT_PUBLIC_KEY_SID        (0x0206U)
#define ARM_TEE_CRYPTO_GENERATE_KEY_SID             (0x0209U)
#define ARM_TEE_CRYPTO_HASH_COMPUTE_SID             (0x0300U)
#define ARM_TEE_CRYPTO_HASH_SETUP_SID               (0x0302U)
#define ARM_TEE_CRYPTO_HASH_UPDATE_SID              (0x0303U)
#define ARM_TEE_CRYPTO_HASH_FINISH_SID              (0x0305U)
#define ARM_TEE_CRYPTO_HASH_ABORT_SID               (0x0307U)
#define ARM_TEE_CRYPTO_ASYMMETRIC_SIGN_HASH_SID     (0x0702U)
#define ARM_TEE_CRYPTO_ASYMMETRIC_VERIFY_HASH_SID   (0x0703U)

/* ARM TEE Protected Storage message types. */
#define ARM_TEE_PS_SET         1001
#define ARM_TEE_PS_GET         1002
#define ARM_TEE_PS_GET_INFO    1003
#define ARM_TEE_PS_REMOVE      1004
#define ARM_TEE_PS_GET_SUPPORT 1005

/* ARM TEE Attestation message types. */
#define ARM_TEE_ATTEST_GET_TOKEN       1001
#define ARM_TEE_ATTEST_GET_TOKEN_SIZE  1002

#ifndef PSA_STORAGE_FLAG_WRITE_ONCE
#define PSA_STORAGE_FLAG_WRITE_ONCE ((psa_storage_create_flags_t)0x00000001)
#endif

typedef uint64_t psa_storage_uid_t;
typedef uint32_t psa_storage_create_flags_t;
typedef size_t rot_size_t;

struct psa_storage_info_t {
    size_t capacity;
    size_t size;
    psa_storage_create_flags_t flags;
};

struct arm_tee_crypto_aead_pack_input {
    uint8_t nonce[16];
    uint32_t nonce_length;
};

struct arm_tee_crypto_pack_iovec {
    psa_key_id_t key_id;
    psa_algorithm_t alg;
    uint32_t op_handle;
    uint32_t ad_length;
    uint32_t plaintext_length;
    struct arm_tee_crypto_aead_pack_input aead_in;
    uint16_t function_id;
    uint16_t step;
    union {
        uint32_t capacity;
        uint64_t value;
    };
};

struct wolfboot_hash_slot {
    uint32_t handle;
    psa_hash_operation_t op;
    int in_use;
};

#ifndef WOLFBOOT_ARM_TEE_HASH_SLOTS
#define WOLFBOOT_ARM_TEE_HASH_SLOTS 4
#endif

static struct wolfboot_hash_slot g_hash_slots[WOLFBOOT_ARM_TEE_HASH_SLOTS];
static uint32_t g_hash_next_handle = 1;

#ifndef WOLFBOOT_PS_MAX_DATA
#define WOLFBOOT_PS_MAX_DATA 512
#endif
#ifndef WOLFBOOT_PS_MAX_ENTRIES
#define WOLFBOOT_PS_MAX_ENTRIES 4
#endif

struct wolfboot_ps_entry {
    psa_storage_uid_t uid;
    size_t size;
    psa_storage_create_flags_t flags;
    uint8_t data[WOLFBOOT_PS_MAX_DATA];
    int in_use;
};

static struct wolfboot_ps_entry g_ps_entries[WOLFBOOT_PS_MAX_ENTRIES];

/* Minimal newlib syscall stubs to avoid link errors in bare-metal builds. */
#ifndef WOLFBOOT_NO_SYSCALL_STUBS
#if defined(__GNUC__)
#define WOLFBOOT_WEAK_SYSCALL __attribute__((weak))
#else
#define WOLFBOOT_WEAK_SYSCALL
#endif

WOLFBOOT_WEAK_SYSCALL int _write(int fd, const void *buf, unsigned int count)
{
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

WOLFBOOT_WEAK_SYSCALL int _read(int fd, void *buf, unsigned int count)
{
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

WOLFBOOT_WEAK_SYSCALL int _close(int fd)
{
    (void)fd;
    return -1;
}

WOLFBOOT_WEAK_SYSCALL int _lseek(int fd, int offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;
    return -1;
}

WOLFBOOT_WEAK_SYSCALL int _fstat(int fd, struct stat *st)
{
    (void)fd;
    if (st != NULL) {
        st->st_mode = S_IFCHR;
    }
    return 0;
}

WOLFBOOT_WEAK_SYSCALL int _isatty(int fd)
{
    (void)fd;
    return 1;
}
#endif /* WOLFBOOT_NO_SYSCALL_STUBS */

static struct wolfboot_ps_entry *wolfboot_ps_find(psa_storage_uid_t uid)
{
    for (size_t i = 0; i < WOLFBOOT_PS_MAX_ENTRIES; i++) {
        if (g_ps_entries[i].in_use && g_ps_entries[i].uid == uid) {
            return &g_ps_entries[i];
        }
    }
    return NULL;
}

static struct wolfboot_ps_entry *wolfboot_ps_alloc(psa_storage_uid_t uid)
{
    for (size_t i = 0; i < WOLFBOOT_PS_MAX_ENTRIES; i++) {
        if (!g_ps_entries[i].in_use) {
            g_ps_entries[i].in_use = 1;
            g_ps_entries[i].uid = uid;
            g_ps_entries[i].size = 0;
            g_ps_entries[i].flags = 0;
            return &g_ps_entries[i];
        }
    }
    return NULL;
}

static psa_status_t wolfboot_psa_open_key(psa_key_id_t id, psa_key_id_t *key)
{
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_status_t status = psa_get_key_attributes(id, &attr);
    psa_reset_key_attributes(&attr);
    if (status != PSA_SUCCESS) {
        return status;
    }
    if (key != NULL) {
        *key = id;
    }
    return PSA_SUCCESS;
}

static psa_status_t wolfboot_psa_close_key(psa_key_id_t key)
{
    (void)key;
    return PSA_SUCCESS;
}

static struct wolfboot_hash_slot *wolfboot_hash_find(uint32_t handle)
{
    for (size_t i = 0; i < WOLFBOOT_ARM_TEE_HASH_SLOTS; i++) {
        if (g_hash_slots[i].in_use && g_hash_slots[i].handle == handle) {
            return &g_hash_slots[i];
        }
    }
    return NULL;
}

static struct wolfboot_hash_slot *wolfboot_hash_alloc(uint32_t *handle)
{
    for (size_t i = 0; i < WOLFBOOT_ARM_TEE_HASH_SLOTS; i++) {
        if (!g_hash_slots[i].in_use) {
            g_hash_slots[i].in_use = 1;
            g_hash_slots[i].handle = g_hash_next_handle++;
            g_hash_slots[i].op = psa_hash_operation_init();
            *handle = g_hash_slots[i].handle;
            return &g_hash_slots[i];
        }
    }
    return NULL;
}

static void wolfboot_hash_free(uint32_t handle)
{
    struct wolfboot_hash_slot *slot = wolfboot_hash_find(handle);
    if (slot != NULL) {
        (void)psa_hash_abort(&slot->op);
        slot->in_use = 0;
        slot->handle = 0;
    }
}

static psa_status_t wolfboot_crypto_dispatch(const psa_invec *in_vec,
                                             size_t in_len,
                                             psa_outvec *out_vec,
                                             size_t out_len)
{
    const struct arm_tee_crypto_pack_iovec *iov;
    psa_status_t init_status;

    if (in_vec == NULL || in_len == 0) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    init_status = psa_crypto_init();
    if (init_status != PSA_SUCCESS) {
        return init_status;
    }

    iov = (const struct arm_tee_crypto_pack_iovec *)in_vec[0].base;
    if (iov == NULL || in_vec[0].len < sizeof(struct arm_tee_crypto_pack_iovec)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    switch (iov->function_id) {
    case ARM_TEE_CRYPTO_GENERATE_RANDOM_SID:
        if (out_vec == NULL || out_len < 1) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        if (out_vec[0].len == 0) {
            return PSA_SUCCESS;
        }
        return psa_generate_random((uint8_t *)out_vec[0].base, out_vec[0].len);

    case ARM_TEE_CRYPTO_OPEN_KEY_SID:
        if (out_vec == NULL || out_len < 1) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        return wolfboot_psa_open_key(iov->key_id,
                                     (psa_key_id_t *)out_vec[0].base);

    case ARM_TEE_CRYPTO_CLOSE_KEY_SID:
        return wolfboot_psa_close_key(iov->key_id);

    case ARM_TEE_CRYPTO_IMPORT_KEY_SID:
        if (in_len < 3 || out_vec == NULL || out_len < 1) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        {
            psa_key_attributes_t attr = *(const psa_key_attributes_t *)in_vec[1].base;
            /* Fallback to volatile storage if persistent storage is unavailable. */
            if (!PSA_KEY_LIFETIME_IS_VOLATILE(attr.lifetime)) {
                attr.lifetime = PSA_KEY_LIFETIME_VOLATILE;
            }
            return psa_import_key(&attr,
                                  (const uint8_t *)in_vec[2].base,
                                  in_vec[2].len,
                                  (psa_key_id_t *)out_vec[0].base);
        }

    case ARM_TEE_CRYPTO_GENERATE_KEY_SID:
        if (in_len < 2 || out_vec == NULL || out_len < 1) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        {
            psa_key_attributes_t attr = *(const psa_key_attributes_t *)in_vec[1].base;
            /* Fallback to volatile storage if persistent storage is unavailable. */
            if (!PSA_KEY_LIFETIME_IS_VOLATILE(attr.lifetime)) {
                attr.lifetime = PSA_KEY_LIFETIME_VOLATILE;
            }
            return psa_generate_key(&attr,
                                    (psa_key_id_t *)out_vec[0].base);
        }

    case ARM_TEE_CRYPTO_DESTROY_KEY_SID:
        return psa_destroy_key(iov->key_id);

    case ARM_TEE_CRYPTO_EXPORT_KEY_SID: {
        size_t data_len = 0;
        psa_status_t status;
        if (out_vec == NULL || out_len < 1) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        status = psa_export_key(iov->key_id,
                                (uint8_t *)out_vec[0].base,
                                out_vec[0].len,
                                &data_len);
        if (status == PSA_SUCCESS) {
            out_vec[0].len = data_len;
        }
        return status;
    }

    case ARM_TEE_CRYPTO_EXPORT_PUBLIC_KEY_SID: {
        size_t data_len = 0;
        psa_status_t status;
        if (out_vec == NULL || out_len < 1) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        status = psa_export_public_key(iov->key_id,
                                       (uint8_t *)out_vec[0].base,
                                       out_vec[0].len,
                                       &data_len);
        if (status == PSA_SUCCESS) {
            out_vec[0].len = data_len;
        }
        return status;
    }

    case ARM_TEE_CRYPTO_GET_KEY_ATTRIBUTES_SID:
        if (out_vec == NULL || out_len < 1) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        return psa_get_key_attributes(iov->key_id,
                                      (psa_key_attributes_t *)out_vec[0].base);

    case ARM_TEE_CRYPTO_HASH_COMPUTE_SID: {
        size_t hash_len = 0;
        psa_status_t status;
        if (in_len < 2 || out_vec == NULL || out_len < 1) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        status = psa_hash_compute(iov->alg,
                                  (const uint8_t *)in_vec[1].base,
                                  in_vec[1].len,
                                  (uint8_t *)out_vec[0].base,
                                  out_vec[0].len,
                                  &hash_len);
        if (status == PSA_SUCCESS) {
            out_vec[0].len = hash_len;
        }
        return status;
    }

    case ARM_TEE_CRYPTO_HASH_SETUP_SID: {
        struct wolfboot_hash_slot *slot;
        uint32_t handle = 0;
        psa_status_t status;
        if (out_vec == NULL || out_len < 1) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        slot = wolfboot_hash_alloc(&handle);
        if (slot == NULL) {
            return PSA_ERROR_INSUFFICIENT_MEMORY;
        }
        status = psa_hash_setup(&slot->op, iov->alg);
        if (status != PSA_SUCCESS) {
            wolfboot_hash_free(handle);
            return status;
        }
        *(uint32_t *)out_vec[0].base = handle;
        out_vec[0].len = sizeof(uint32_t);
        return PSA_SUCCESS;
    }

    case ARM_TEE_CRYPTO_HASH_UPDATE_SID: {
        struct wolfboot_hash_slot *slot;
        if (in_len < 2) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        slot = wolfboot_hash_find(iov->op_handle);
        if (slot == NULL) {
            return PSA_ERROR_BAD_STATE;
        }
        return psa_hash_update(&slot->op,
                               (const uint8_t *)in_vec[1].base,
                               in_vec[1].len);
    }

    case ARM_TEE_CRYPTO_HASH_FINISH_SID: {
        struct wolfboot_hash_slot *slot;
        size_t hash_len = 0;
        psa_status_t status;
        if (out_vec == NULL || out_len < 2) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        slot = wolfboot_hash_find(iov->op_handle);
        if (slot == NULL) {
            return PSA_ERROR_BAD_STATE;
        }
        status = psa_hash_finish(&slot->op,
                                 (uint8_t *)out_vec[1].base,
                                 out_vec[1].len,
                                 &hash_len);
        if (status == PSA_SUCCESS) {
            out_vec[0].len = sizeof(uint32_t);
            *(uint32_t *)out_vec[0].base = 0;
            out_vec[1].len = hash_len;
            wolfboot_hash_free(iov->op_handle);
        }
        return status;
    }

    case ARM_TEE_CRYPTO_HASH_ABORT_SID: {
        struct wolfboot_hash_slot *slot;
        if (out_vec == NULL || out_len < 1) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        slot = wolfboot_hash_find(iov->op_handle);
        if (slot == NULL) {
            *(uint32_t *)out_vec[0].base = 0;
            out_vec[0].len = sizeof(uint32_t);
            return PSA_SUCCESS;
        }
        (void)psa_hash_abort(&slot->op);
        wolfboot_hash_free(iov->op_handle);
        *(uint32_t *)out_vec[0].base = 0;
        out_vec[0].len = sizeof(uint32_t);
        return PSA_SUCCESS;
    }

    case ARM_TEE_CRYPTO_ASYMMETRIC_SIGN_HASH_SID: {
        size_t sig_len = 0;
        psa_status_t status;
        if (in_len < 2 || out_vec == NULL || out_len < 1) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        status = psa_sign_hash(iov->key_id,
                               iov->alg,
                               (const uint8_t *)in_vec[1].base,
                               in_vec[1].len,
                               (uint8_t *)out_vec[0].base,
                               out_vec[0].len,
                               &sig_len);
        if (status == PSA_SUCCESS) {
            out_vec[0].len = sig_len;
        }
        return status;
    }

    case ARM_TEE_CRYPTO_ASYMMETRIC_VERIFY_HASH_SID:
        if (in_len < 3) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        return psa_verify_hash(iov->key_id,
                               iov->alg,
                               (const uint8_t *)in_vec[1].base,
                               in_vec[1].len,
                               (const uint8_t *)in_vec[2].base,
                               in_vec[2].len);

    default:
        return PSA_ERROR_NOT_SUPPORTED;
    }
}

uint32_t arm_tee_psa_framework_version(void)
{
    return 1U;
}

uint32_t arm_tee_psa_version(uint32_t sid)
{
    if (sid == ARM_TEE_CRYPTO_SID) {
        return 1U;
    }
    return 0U;
}

psa_handle_t arm_tee_psa_connect(uint32_t sid, uint32_t version)
{
    (void)version;
    if (sid == ARM_TEE_CRYPTO_SID) {
        return (psa_handle_t)ARM_TEE_CRYPTO_HANDLE;
    }
    return (psa_handle_t)PSA_ERROR_CONNECTION_REFUSED;
}

int32_t arm_tee_psa_call(psa_handle_t handle, int32_t type,
    const psa_invec *in_vec, size_t in_len,
    psa_outvec *out_vec, size_t out_len)
{
    (void)type;

    if (handle == (psa_handle_t)ARM_TEE_CRYPTO_HANDLE) {
        return wolfboot_crypto_dispatch(in_vec, in_len, out_vec, out_len);
    }

    if (handle == (psa_handle_t)ARM_TEE_PROTECTED_STORAGE_HANDLE) {
        if (type == ARM_TEE_PS_SET) {
            const psa_storage_uid_t *uid;
            const void *data;
            const psa_storage_create_flags_t *flags;
            struct wolfboot_ps_entry *entry;
            if (in_vec == NULL || in_len < 3) {
                return PSA_ERROR_INVALID_ARGUMENT;
            }
            uid = (const psa_storage_uid_t *)in_vec[0].base;
            data = in_vec[1].base;
            flags = (const psa_storage_create_flags_t *)in_vec[2].base;
            if (uid == NULL || flags == NULL) {
                return PSA_ERROR_INVALID_ARGUMENT;
            }
            if (in_vec[1].len > WOLFBOOT_PS_MAX_DATA) {
                return PSA_ERROR_INSUFFICIENT_STORAGE;
            }
            entry = wolfboot_ps_find(*uid);
            if (entry == NULL) {
                entry = wolfboot_ps_alloc(*uid);
                if (entry == NULL) {
                    return PSA_ERROR_INSUFFICIENT_STORAGE;
                }
            } else if ((entry->flags & PSA_STORAGE_FLAG_WRITE_ONCE) != 0U) {
                return PSA_ERROR_NOT_PERMITTED;
            }
            if (in_vec[1].len > 0 && data == NULL) {
                return PSA_ERROR_INVALID_ARGUMENT;
            }
            if (in_vec[1].len > 0) {
                XMEMCPY(entry->data, data, in_vec[1].len);
            }
            entry->size = in_vec[1].len;
            entry->flags = *flags;
            return PSA_SUCCESS;
        }
        if (type == ARM_TEE_PS_GET) {
            const psa_storage_uid_t *uid;
            const rot_size_t *offset;
            struct wolfboot_ps_entry *entry;
            size_t read_len;
            if (in_vec == NULL || in_len < 2 || out_vec == NULL || out_len < 1) {
                return PSA_ERROR_INVALID_ARGUMENT;
            }
            uid = (const psa_storage_uid_t *)in_vec[0].base;
            offset = (const rot_size_t *)in_vec[1].base;
            if (uid == NULL || offset == NULL) {
                return PSA_ERROR_INVALID_ARGUMENT;
            }
            entry = wolfboot_ps_find(*uid);
            if (entry == NULL) {
                return PSA_ERROR_DOES_NOT_EXIST;
            }
            if (*offset > entry->size) {
                return PSA_ERROR_INVALID_ARGUMENT;
            }
            read_len = entry->size - *offset;
            if (read_len > out_vec[0].len) {
                read_len = out_vec[0].len;
            }
            if (read_len > 0 && out_vec[0].base != NULL) {
                XMEMCPY(out_vec[0].base, entry->data + *offset, read_len);
            }
            out_vec[0].len = read_len;
            return PSA_SUCCESS;
        }
        if (type == ARM_TEE_PS_GET_INFO) {
            const psa_storage_uid_t *uid;
            struct wolfboot_ps_entry *entry;
            if (in_vec == NULL || in_len < 1 || out_vec == NULL || out_len < 1) {
                return PSA_ERROR_INVALID_ARGUMENT;
            }
            uid = (const psa_storage_uid_t *)in_vec[0].base;
            if (uid == NULL) {
                return PSA_ERROR_INVALID_ARGUMENT;
            }
            entry = wolfboot_ps_find(*uid);
            if (entry == NULL) {
                return PSA_ERROR_DOES_NOT_EXIST;
            }
            {
                struct psa_storage_info_t info;
                info.capacity = WOLFBOOT_PS_MAX_DATA;
                info.size = entry->size;
                info.flags = entry->flags;
                if (out_vec[0].len < sizeof(info)) {
                    return PSA_ERROR_BUFFER_TOO_SMALL;
                }
                XMEMCPY(out_vec[0].base, &info, sizeof(info));
                out_vec[0].len = sizeof(info);
            }
            return PSA_SUCCESS;
        }
        if (type == ARM_TEE_PS_REMOVE) {
            const psa_storage_uid_t *uid;
            struct wolfboot_ps_entry *entry;
            if (in_vec == NULL || in_len < 1) {
                return PSA_ERROR_INVALID_ARGUMENT;
            }
            uid = (const psa_storage_uid_t *)in_vec[0].base;
            if (uid == NULL) {
                return PSA_ERROR_INVALID_ARGUMENT;
            }
            entry = wolfboot_ps_find(*uid);
            if (entry == NULL) {
                return PSA_ERROR_DOES_NOT_EXIST;
            }
            entry->in_use = 0;
            entry->uid = 0;
            entry->size = 0;
            entry->flags = 0;
            return PSA_SUCCESS;
        }
        if (type == ARM_TEE_PS_GET_SUPPORT) {
            if (out_vec != NULL && out_len >= 1 && out_vec[0].base != NULL) {
                uint32_t support = 0;
                XMEMCPY(out_vec[0].base, &support, sizeof(support));
                out_vec[0].len = sizeof(support);
            }
            return PSA_SUCCESS;
        }
        return PSA_ERROR_NOT_SUPPORTED;
    }

    if (handle == (psa_handle_t)ARM_TEE_ATTESTATION_HANDLE) {
        if (type == ARM_TEE_ATTEST_GET_TOKEN) {
            if (out_vec == NULL || out_len < 1) {
                return PSA_ERROR_INVALID_ARGUMENT;
            }
            out_vec[0].len = 0;
            return PSA_SUCCESS;
        }
        if (type == ARM_TEE_ATTEST_GET_TOKEN_SIZE) {
            if (out_vec == NULL || out_len < 1 || out_vec[0].base == NULL ||
                out_vec[0].len < sizeof(rot_size_t)) {
                return PSA_ERROR_INVALID_ARGUMENT;
            }
            {
                rot_size_t token_size = 0;
                XMEMCPY(out_vec[0].base, &token_size, sizeof(token_size));
                out_vec[0].len = sizeof(token_size);
            }
            return PSA_SUCCESS;
        }
        return PSA_ERROR_NOT_SUPPORTED;
    }

    return PSA_ERROR_NOT_SUPPORTED;
}

void arm_tee_psa_close(psa_handle_t handle)
{
    (void)handle;
}
