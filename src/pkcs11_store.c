/* pkcs11_store.c
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


#include <stdint.h>
#include <string.h>

#include "hal.h"

#ifdef SECURE_PKCS11

#include "wolfpkcs11/pkcs11.h"
#include "wolfpkcs11/store.h"

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/types.h>

#ifndef KEYVAULT_OBJ_SIZE
    #define KEYVAULT_OBJ_SIZE 0x1000 /* 4KB per object */
#endif

#ifndef KEYVAULT_MAX_ITEMS
    #define KEYVAULT_MAX_ITEMS 20 /* Total memory: 0x16000 (20 items) + 2 sector overhead = 0x18000 */
#endif

/* Internal errors from wolfPKCS11 */
#define PIN_INVALID_E                  -1
#define PIN_NOT_SET_E                  -2
#define READ_ONLY_E                    -3
#define NOT_AVAILABLE_E                -4
#define FIND_FULL_E                    -5
#define FIND_NO_MORE_E                 -6
#define SESSION_EXISTS_E               -7
#define SESSION_COUNT_E                -8
#define LOGGED_IN_E                    -9
#define OBJ_COUNT_E                    -10


#ifndef UNIT_TEST

/* From linker script: origin and size of vault flash */
extern uint32_t _flash_keyvault;
extern uint32_t _flash_keyvault_size;

#define vault_base ((uint8_t*)&_flash_keyvault)
#define vault_size ((uint32_t)&_flash_keyvault_size)


/* Back-end for malloc, used by wolfPKCS11 */
extern unsigned int _start_heap; /* From linker script: heap memory */
extern unsigned int _heap_size;  /* From linker script: heap limit */

void * _sbrk(unsigned int incr)
{
    static uint8_t *heap = NULL;
    static uint32_t heapsize = (uint32_t)&_heap_size;
    void *old_heap = heap;
    (void)heapsize;
    if (((incr >> 2) << 2) != incr)
        incr = ((incr >> 2) + 1) << 2;

    if (heap == NULL) {
        heap = (uint8_t*)&_start_heap;
        old_heap = heap;
    } else
        heap += incr;
    return old_heap;
}
#endif

struct obj_hdr
{
    uint32_t token_id;
    uint32_t object_id;
    int32_t  type;
    uint32_t pos;
    uint32_t size;
    uint32_t __pad[3];
};
#define STORE_PRIV_HDR_SIZE 0x20
#define STORE_PRIV_HDR_OFFSET 0x80
#define PKCS11_INVALID_ID 0xFFFFFFFF

#define BITMAP_OFFSET (4)
#define BITMAP_SIZE (KEYVAULT_MAX_ITEMS / 8 + 1)

#if (BITMAP_SIZE > (STORE_PRIV_HDR_OFFSET - 4))
    #error Too many keyvault items
#endif

/* This spells "PKCS" */
#ifndef BIG_ENDIAN_ORDER
    #define VAULT_HEADER_MAGIC 0x53434B50
#else
    #define VAULT_HEADER_MAGIC 0x504B4353
#endif

#define MAX_OPEN_STORES 16

struct store_handle {
    uint32_t flags;
    uint32_t pos;
    void     *buffer;
    struct obj_hdr *hdr;
    uint32_t in_buffer_offset;
};

#define STORE_FLAGS_OPEN (1 << 0)
#define STORE_FLAGS_READONLY (1 << 1)

static struct store_handle openstores_handles[MAX_OPEN_STORES] = {};

static uint8_t cached_sector[WOLFBOOT_SECTOR_SIZE];

static void bitmap_put(uint32_t pos, int val)
{
    uint32_t octet = pos / 8;
    uint32_t bit = pos % 8;
    uint8_t *bitmap = cached_sector + sizeof(uint32_t);

    if (val != 0) {
        bitmap[octet] |= (1 << bit);
    } else {
        bitmap[octet] &= ~(1 << bit);
    }
}

static int bitmap_get(uint32_t pos)
{
    uint32_t octet = pos / 8;
    uint32_t bit = pos % 8;
    uint8_t *bitmap = vault_base + sizeof(uint32_t);
    return (bitmap[octet] & (1 << bit)) >> bit;
}

static int bitmap_find_free_pos(void)
{
    int i;
    for (i = 0; i < KEYVAULT_MAX_ITEMS; i++) {
        if (bitmap_get(i) == 0)
            return i;
    }
    return -1;
}

/* A table with nodes is stored at the beginning of the keyvault
 *   - 4 B: Magic (spells "PKCS" when the vault is initialized)
 *   - N B: bitmap (N = KEYVAULT_MAX_ITEMS / 8 + 1)
 *
 *   At byte 0x80:
 *    - Start of the obj hdr structures array
 */

#define NODES_TABLE ( (struct obj_hdr *)(vault_base + STORE_PRIV_HDR_OFFSET) )

/* A backup sector immediately after the header sector */

#define BACKUP_SECTOR_ADDRESS (vault_base + WOLFBOOT_SECTOR_SIZE)

static void cache_commit(uint32_t offset)
{
    hal_flash_unlock();

    /* Write backup sector first */
    hal_flash_erase((uintptr_t)BACKUP_SECTOR_ADDRESS, WOLFBOOT_SECTOR_SIZE);
    hal_flash_write((uintptr_t)BACKUP_SECTOR_ADDRESS, cached_sector, WOLFBOOT_SECTOR_SIZE);

    /* Erase + write actual destination sector */
    hal_flash_erase((uintptr_t)vault_base + offset, WOLFBOOT_SECTOR_SIZE);
    hal_flash_write((uintptr_t)vault_base + offset, cached_sector, WOLFBOOT_SECTOR_SIZE);

    hal_flash_lock();
}

static void restore_backup(uint32_t offset)
{
    hal_flash_unlock();
    /* Erase + copy from backup */
    hal_flash_erase((uintptr_t)vault_base + offset, WOLFBOOT_SECTOR_SIZE);
    hal_flash_write((uintptr_t)vault_base + offset, BACKUP_SECTOR_ADDRESS,
            WOLFBOOT_SECTOR_SIZE);
    hal_flash_lock();
}

static void check_vault(void)
{
    uint32_t *magic = (uint32_t *)vault_base;
    uint32_t total_vault_size = KEYVAULT_MAX_ITEMS * KEYVAULT_OBJ_SIZE;

    if ((total_vault_size % WOLFBOOT_SECTOR_SIZE) != 0)
        total_vault_size = (total_vault_size / WOLFBOOT_SECTOR_SIZE) * WOLFBOOT_SECTOR_SIZE + WOLFBOOT_SECTOR_SIZE;

    if (*magic != VAULT_HEADER_MAGIC) {
        uint32_t *magic = (uint32_t *)BACKUP_SECTOR_ADDRESS;
        if (*magic == VAULT_HEADER_MAGIC) {
            restore_backup(0);
            return;
        }
        memset(cached_sector, 0xFF, WOLFBOOT_SECTOR_SIZE);
        magic = (uint32_t *)cached_sector;
        *magic = VAULT_HEADER_MAGIC;
        memset(cached_sector + sizeof(uint32_t), 0x00, BITMAP_SIZE);
        cache_commit(0);
        hal_flash_unlock();
        hal_flash_erase((uintptr_t)vault_base + WOLFBOOT_SECTOR_SIZE * 2, total_vault_size);
        hal_flash_lock();
    }
}

static void delete_object(int32_t type, uint32_t tok_id, uint32_t obj_id)
{
    struct obj_hdr *hdr = (struct obj_hdr *)cached_sector;
    check_vault();
    memcpy(cached_sector, vault_base, WOLFBOOT_SECTOR_SIZE);

    while ((uintptr_t)hdr < ((uintptr_t)cached_sector + WOLFBOOT_SECTOR_SIZE)) {
        if ((hdr->token_id == tok_id) && (hdr->object_id == obj_id) &&
                (hdr->type == type)) {
            hdr->token_id = PKCS11_INVALID_ID;
            hdr->object_id = PKCS11_INVALID_ID;
            bitmap_put(hdr->pos, 0);
            cache_commit(0);
            return;
        }
        hdr++;
    }
}

/* Returns a pointer to the selected object in flash.
 * NULL is OK here as error return value, even if the keystore
 * started at physical 0x0000 0000, the buffers are stored from sector
 * 2 onwards.
 */
static uint8_t *find_object_buffer(int32_t type, uint32_t tok_id, uint32_t obj_id)
{
    struct obj_hdr *hdr = NODES_TABLE;
    uint32_t *tok_obj_stored = NULL;
    while ((uintptr_t)hdr < ((uintptr_t)NODES_TABLE + WOLFBOOT_SECTOR_SIZE)) {
        if ((hdr->token_id == tok_id) && (hdr->object_id == obj_id)
                && (hdr->type == type)) {
            tok_obj_stored = (uint32_t *) (vault_base + (2 * WOLFBOOT_SECTOR_SIZE) + (hdr->pos * KEYVAULT_OBJ_SIZE));
            if ((tok_obj_stored[0] != tok_id) || (tok_obj_stored[1] != obj_id)) {
                /* Id's don't match. Try backup sector. */
                uint32_t in_sector_off = (hdr->pos * KEYVAULT_OBJ_SIZE) %
                    WOLFBOOT_SECTOR_SIZE;
                uint32_t sector_base = hdr->pos * KEYVAULT_OBJ_SIZE +
                    2 * WOLFBOOT_SECTOR_SIZE - in_sector_off;
                tok_obj_stored = (uint32_t *)((BACKUP_SECTOR_ADDRESS + in_sector_off));
                if ((tok_obj_stored[0] == tok_id) && (tok_obj_stored[1] == obj_id)) {
                    /* Found backup! restoring... */
                    restore_backup(sector_base);
                } else {
                    delete_object(type, tok_id, obj_id);
                    return NULL; /* Cannot recover object payload */
                }
            }
            /* Object is now OK */
            return vault_base + 2 * WOLFBOOT_SECTOR_SIZE + hdr->pos * KEYVAULT_OBJ_SIZE;
        }
        hdr++;
    }
    return NULL; /* object not found */
}

static struct obj_hdr *find_object_header(int32_t type, uint32_t tok_id,
        uint32_t obj_id)
{
    struct obj_hdr *hdr = NODES_TABLE;
    while ((uintptr_t)hdr < ((uintptr_t)NODES_TABLE + WOLFBOOT_SECTOR_SIZE)) {
        if ((hdr->token_id == tok_id) && (hdr->object_id == obj_id)
                && (hdr->type == type)) {
            return hdr;
        }
        hdr++;
    }
    return NULL;
}

static struct obj_hdr *create_object(int32_t type, uint32_t tok_id, uint32_t obj_id)
{
    struct obj_hdr *hdr = NULL;
    uint32_t *tok_obj_id;
    /* Refuse to create an object that's already in store */
    if (find_object_buffer(type, tok_id, obj_id) != NULL) {
        return NULL;
    }

    /* Caching sector 0 */
    memcpy(cached_sector, vault_base , WOLFBOOT_SECTOR_SIZE);
    hdr = (struct obj_hdr *)(cached_sector + STORE_PRIV_HDR_OFFSET);
    while ((uintptr_t)hdr < ((uintptr_t)cached_sector + WOLFBOOT_SECTOR_SIZE)) {
        if (hdr->token_id == PKCS11_INVALID_ID) {
            uint32_t sector_base, in_sector_off;
            int pos = bitmap_find_free_pos();
            if (pos < 0) {
                return NULL;
            }
            hdr->pos = (unsigned)pos;
            in_sector_off = (hdr->pos * KEYVAULT_OBJ_SIZE) %
                WOLFBOOT_SECTOR_SIZE;
            sector_base = hdr->pos * KEYVAULT_OBJ_SIZE +
                2 * WOLFBOOT_SECTOR_SIZE - in_sector_off;
            /* Claim the spot in the table */
            hdr->token_id = tok_id;
            hdr->object_id = obj_id;
            hdr->type = type;
            /* Set vault initial size to eight bytes (this includes the
             * tok/obj id at the beginning of the buffer, before the
             * payload). When an object is opened, the initial 'in_buffer_offset'
             * is set to 8 as well.
            */
            hdr->size = 2 * sizeof(uint32_t);
            /* Set the bit to claim the position in flash */
            bitmap_put(hdr->pos, 1);
            cache_commit(0);
            /* Mark the beginning of the object in the sector,
             * write the tok/obj ids
             */
            memcpy(cached_sector, vault_base + sector_base,
                    WOLFBOOT_SECTOR_SIZE);
            tok_obj_id = (void*)(cached_sector + in_sector_off);
            tok_obj_id[0] = tok_id;
            tok_obj_id[1] = obj_id;
            cache_commit(sector_base);
            /* Return the address of the header in flash */
            return (struct obj_hdr *)(vault_base + ((uint8_t *)hdr - (uint8_t *)cached_sector));
        }
        hdr++;
    }
    return NULL; /* No space left in the nodes table */
}

static void update_store_size(struct obj_hdr *hdr, uint32_t size)
{
    uint32_t off;
    struct obj_hdr *hdr_mem;
    if (((uint8_t *)hdr) < vault_base ||
        ((uint8_t *)hdr > vault_base + WOLFBOOT_SECTOR_SIZE))
        return;
    check_vault();
    off = (uintptr_t)hdr - (uintptr_t)vault_base;
    memcpy(cached_sector, vault_base, WOLFBOOT_SECTOR_SIZE);
    hdr_mem = (struct obj_hdr *)(cached_sector + off);
    hdr_mem->size = size;
    cache_commit(0);
}

/* Find a free handle in openstores_handles[] array
 * to manage the interaction with the API.
 *
 * A maximum of MAX_OPEN_STORES objects can be opened
 * at the same time.
 */
static struct store_handle *find_free_handle(void)
{
    int i;
    for (i = 0; i < MAX_OPEN_STORES; i++) {
        if ((openstores_handles[i].flags & STORE_FLAGS_OPEN) == 0)
            return &openstores_handles[i];
    }
    return NULL;
}

int wolfPKCS11_Store_Open(int type, CK_ULONG id1, CK_ULONG id2, int read,
    void** store)
{
    struct store_handle *handle;
    uint8_t *buf;

    /* Check if there is one handle available to open the slot */
    handle = find_free_handle();
    if (!handle) {
        *store = NULL;
        return SESSION_COUNT_E;
    }

    /* Check if the target object exists */
    check_vault();
    buf = find_object_buffer(type, id1, id2);
    if ((buf == NULL) && read) {
        *store = NULL;
        return NOT_AVAILABLE_E;
    }

    if ((buf == NULL) && (!read)) {
        handle->hdr = create_object(type, id1, id2);
        if (handle->hdr == NULL) {
            *store = NULL;
            return FIND_FULL_E;

        }
        buf = find_object_buffer(type, id1, id2);
        if (!buf) {
            *store = NULL;
            return NOT_AVAILABLE_E;
        }
    } else { /* buf != NULL, readonly */
        handle->hdr = find_object_header(type, id1, id2);
        if (!handle->hdr) {
            *store = NULL;
            return NOT_AVAILABLE_E;
        }
    }

    /* Set the position of the buffer in the handle */
    handle->buffer = buf;
    handle->pos = (((uintptr_t)buf) - (uintptr_t)vault_base) / KEYVAULT_OBJ_SIZE;
    /* Set the 'open' flag */
    handle->flags |= STORE_FLAGS_OPEN;

    /* Set the 'readonly' flag in this handle if open with 'r' */
    if (read)
        handle->flags |= STORE_FLAGS_READONLY;
    else {
        handle->flags &= ~STORE_FLAGS_READONLY;
        /* Truncate the slot when opening in write mode */
        update_store_size(handle->hdr, 2 * sizeof(uint32_t));
    }


    /* Set start of the buffer after the tok/obj id fields */
    handle->in_buffer_offset = (2 * sizeof(uint32_t));
    *store = handle;
    return 0;
}

void wolfPKCS11_Store_Close(void* store)
{
    struct store_handle *handle = store;
    /* This removes all flags (including STORE_FLAGS_OPEN) */
    handle->flags = 0;
    handle->hdr = NULL;
}

int wolfPKCS11_Store_Read(void* store, unsigned char* buffer, int len)
{
    struct store_handle *handle = store;
    uint32_t obj_size = 0;
    if ((handle == NULL) || (handle->hdr == NULL) || (handle->buffer == NULL))
       return -1;

    obj_size = handle->hdr->size;
    if (obj_size > KEYVAULT_OBJ_SIZE)
        return -1;

    if (handle->in_buffer_offset >= obj_size)
        return 0; /* "EOF" */

    /* Truncate len to actual available bytes */
    if (handle->in_buffer_offset + len > obj_size)
        len = (obj_size - handle->in_buffer_offset);

    if (len > 0) {
        memcpy(buffer, (uint8_t *)(handle->buffer) + handle->in_buffer_offset, len);
        handle->in_buffer_offset += len;
    }
    return len;
}

int wolfPKCS11_Store_Write(void* store, unsigned char* buffer, int len)
{
    struct store_handle *handle = store;
    uint32_t obj_size = 0;
    uint32_t in_sector_offset = 0;
    uint32_t in_sector_len = 0;
    uint32_t sector_base = 0;
    int written = 0;


    if ((handle == NULL) || (handle->hdr == NULL) || (handle->buffer == NULL))
       return -1;
    if ((handle->flags & STORE_FLAGS_READONLY) != 0)
        return -1;

    obj_size = handle->hdr->size;
    if (obj_size > KEYVAULT_OBJ_SIZE)
        return -1;

    if (len + handle->in_buffer_offset > KEYVAULT_OBJ_SIZE)
        len = KEYVAULT_OBJ_SIZE - handle->in_buffer_offset;

    if (len < 0)
        return -1;


    while (written < len) {
        in_sector_offset = ((uintptr_t)(handle->buffer) + handle->in_buffer_offset)
           % WOLFBOOT_SECTOR_SIZE;
        sector_base = (uintptr_t)handle->buffer + handle->in_buffer_offset - in_sector_offset;
        in_sector_len = WOLFBOOT_SECTOR_SIZE - in_sector_offset;
        if (in_sector_len > (uint32_t)len)
            in_sector_len = len;

        /* Cache the corresponding sector */
        memcpy(cached_sector, (void *)(uintptr_t)sector_base, WOLFBOOT_SECTOR_SIZE);
        /* Write content into cache */
        memcpy(cached_sector + in_sector_offset, buffer + written, in_sector_len);
        /* Adjust in_buffer position for the handle accordingly */
        handle->in_buffer_offset += in_sector_len;
        written += in_sector_len;
        /* Write sector to flash */
        cache_commit((uintptr_t)sector_base - (uintptr_t)vault_base);
    }
    obj_size += written;
    update_store_size(handle->hdr, obj_size);
    return len;
}

int wolfPKCS11_Store_Remove(int type, CK_ULONG id1, CK_ULONG id2)
{
    uint8_t* buf;

    check_vault();
    buf = find_object_buffer((int32_t)type, (uint32_t)id1, (uint32_t)id2);
    if (buf == NULL)
        return NOT_AVAILABLE_E;

    delete_object((int32_t)type, (uint32_t)id1, (uint32_t)id2);
    return 0;
}

#endif /* SECURE_PKCS11 */
