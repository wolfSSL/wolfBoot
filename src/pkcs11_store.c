/* pkcs11_store.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
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
#include "store_sbrk.h"

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
    return wolfboot_store_sbrk(incr, &heap, (uint8_t *)&_start_heap, heapsize);
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
    uint32_t size;  /* live object size; the flash node is updated at commit */
    void     *buffer;
    struct obj_hdr *hdr;
    uint32_t in_buffer_offset;
};

#define STORE_FLAGS_OPEN (1 << 0)
#define STORE_FLAGS_READONLY (1 << 1)

static struct store_handle openstores_handles[MAX_OPEN_STORES] = {};

/*
 * Sector cache: batches flash traffic within a Store_Open/Store_Close
 * window. Sectors accumulate modifications in RAM and are committed
 * together by cache_flush_all(). The header sector (offset 0) is
 * always committed last, so a committed header is the atomic commit
 * point of the whole batch: power failure during a flush leaves the
 * flash in either the pre-batch or the post-batch state, never a mix.
 */
#define PKCS11_STORE_MAX_SECTORS \
    ((KEYVAULT_OBJ_SIZE + WOLFBOOT_SECTOR_SIZE - 1) / WOLFBOOT_SECTOR_SIZE \
        + 2)

#ifndef WOLFBOOT_PKCS11_STORE_CACHE_SECTORS
    #define WOLFBOOT_PKCS11_STORE_CACHE_SECTORS PKCS11_STORE_MAX_SECTORS
#endif
#if (WOLFBOOT_PKCS11_STORE_CACHE_SECTORS > PKCS11_STORE_MAX_SECTORS)
    #error WOLFBOOT_PKCS11_STORE_CACHE_SECTORS exceeds worst case
#endif

struct cache_entry {
    uint8_t  *sector;  /* NULL when the slot is free */
    uint32_t  offset;  /* vault offset of the sector */
    uint32_t  lru;     /* last use tick */
};

static uint8_t cache_sector_mem
    [WOLFBOOT_PKCS11_STORE_CACHE_SECTORS][WOLFBOOT_SECTOR_SIZE];
static struct cache_entry store_cache[WOLFBOOT_PKCS11_STORE_CACHE_SECTORS];
static uint32_t cache_lru_tick;

static uint8_t *cache_get_sector(uint32_t offset);
static void cache_flush_all(void);
static uint8_t *sector_ptr(uint32_t offset);
static uint8_t *sector0_ptr(void);

/* Optional flash-activity instrumentation (PKCS11_STORE_STATS, not
 * enabled by any shipping config): counts sector commits, erases and
 * programs so a host test can quantify the store's flash traffic. */
#ifdef PKCS11_STORE_STATS
static uint32_t stats_commits;
static uint32_t stats_erases;
static uint32_t stats_programs;

void wolfPKCS11_Store_GetStats(uint32_t *commits, uint32_t *erases,
        uint32_t *programs)
{
    *commits = stats_commits;
    *erases = stats_erases;
    *programs = stats_programs;
}

void wolfPKCS11_Store_ResetStats(void)
{
    stats_commits = 0;
    stats_erases = 0;
    stats_programs = 0;
}
#endif

static void bitmap_put(uint32_t pos, int val)
{
    uint32_t octet = pos / 8;
    uint32_t bit = pos % 8;
    uint8_t *bitmap;

    /* Reject out-of-range positions (e.g. a power-fault-corrupted hdr->pos
     * left as erased flash) to avoid an out-of-bounds write past the
     * bitmap, which lives within the header sector. */
    if (pos >= KEYVAULT_MAX_ITEMS) {
        return;
    }

    bitmap = cache_get_sector(0) + sizeof(uint32_t);
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
    uint8_t *bitmap = sector0_ptr() + sizeof(uint32_t);
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

static struct cache_entry *cache_find(uint32_t offset)
{
    int i;

    for (i = 0; i < WOLFBOOT_PKCS11_STORE_CACHE_SECTORS; i++) {
        if ((store_cache[i].sector != NULL) &&
                (store_cache[i].offset == offset)) {
            return &store_cache[i];
        }
    }
    return NULL;
}

static void cache_commit_entry(struct cache_entry *entry)
{
    hal_flash_unlock();

    /* Write backup sector first */
    hal_flash_erase((uintptr_t)BACKUP_SECTOR_ADDRESS, WOLFBOOT_SECTOR_SIZE);
    hal_flash_write((uintptr_t)BACKUP_SECTOR_ADDRESS, entry->sector,
        WOLFBOOT_SECTOR_SIZE);

    /* Erase + write actual destination sector */
    hal_flash_erase((uintptr_t)vault_base + entry->offset,
        WOLFBOOT_SECTOR_SIZE);
    hal_flash_write((uintptr_t)vault_base + entry->offset, entry->sector,
        WOLFBOOT_SECTOR_SIZE);

    hal_flash_lock();
#ifdef PKCS11_STORE_STATS
    stats_commits++;
    stats_erases += 2;
    stats_programs += 2;
#endif
}

/*
 * Get a RAM copy of the vault sector at the given offset. Modifications
 * stay in RAM until cache_flush_all() (or LRU eviction) commits them.
 */
static uint8_t *cache_get_sector(uint32_t offset)
{
    struct cache_entry *entry;
    int i;
    int free_slot = -1;

    entry = cache_find(offset);
    if (entry != NULL) {
        entry->lru = ++cache_lru_tick;
        return entry->sector;
    }

    for (i = 0; i < WOLFBOOT_PKCS11_STORE_CACHE_SECTORS; i++) {
        if (store_cache[i].sector == NULL) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) {
        /* No free slot: commit the least recently used entry */
        int oldest = 0;

        for (i = 1; i < WOLFBOOT_PKCS11_STORE_CACHE_SECTORS; i++) {
            if (store_cache[i].lru < store_cache[oldest].lru) {
                oldest = i;
            }
        }
        cache_commit_entry(&store_cache[oldest]);
        store_cache[oldest].sector = NULL;
        free_slot = oldest;
    }

    entry = &store_cache[free_slot];
    entry->sector = &cache_sector_mem[free_slot][0];
    entry->offset = offset;
    entry->lru = ++cache_lru_tick;
    memcpy(entry->sector, vault_base + offset, WOLFBOOT_SECTOR_SIZE);
    return entry->sector;
}

/*
 * Commit all cached sectors. Payload sectors first, the header sector
 * (offset 0) last, so the header is the atomic commit point of the
 * batch.
 */
static void cache_flush_all(void)
{
    int i;
    int pass;

    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < WOLFBOOT_PKCS11_STORE_CACHE_SECTORS; i++) {
            if (store_cache[i].sector == NULL) {
                continue;
            }
            if ((pass == 0) == (store_cache[i].offset == 0)) {
                continue;
            }
            cache_commit_entry(&store_cache[i]);
            store_cache[i].sector = NULL;
        }
    }
}

/*
 * Read access to a vault sector: the RAM copy when the sector is
 * cached, flash otherwise. Writes must go through cache_get_sector().
 */
static uint8_t *sector_ptr(uint32_t offset)
{
    struct cache_entry *entry = cache_find(offset);

    if (entry != NULL) {
        return entry->sector;
    }
    return vault_base + offset;
}

static uint8_t *sector0_ptr(void)
{
    return sector_ptr(0);
}

static void restore_backup(uint32_t offset)
{
    hal_flash_unlock();
    /* Erase + copy from backup */
    hal_flash_erase((uintptr_t)vault_base + offset, WOLFBOOT_SECTOR_SIZE);
    hal_flash_write((uintptr_t)vault_base + offset, BACKUP_SECTOR_ADDRESS,
            WOLFBOOT_SECTOR_SIZE);
    hal_flash_lock();
#ifdef PKCS11_STORE_STATS
    stats_erases++;
    stats_programs++;
#endif
}

static void check_vault(void)
{
    uint32_t *magic;
    uint32_t *backup_magic;
    uint8_t *s0 = NULL;
    uint32_t total_vault_size = KEYVAULT_MAX_ITEMS * KEYVAULT_OBJ_SIZE;

    /* The cache is shared across all open windows: commit any pending
     * sectors before (re)validating instead of dropping them, or a
     * still-open window would silently lose its writes. The flush is
     * atomic (header last), so this only moves that window's commit
     * point earlier, it never mixes batches. */
    cache_flush_all();

    if ((total_vault_size % WOLFBOOT_SECTOR_SIZE) != 0)
        total_vault_size = (total_vault_size / WOLFBOOT_SECTOR_SIZE) * WOLFBOOT_SECTOR_SIZE + WOLFBOOT_SECTOR_SIZE;

    magic = (uint32_t *)vault_base;
    if (*magic != VAULT_HEADER_MAGIC) {
        backup_magic = (uint32_t *)BACKUP_SECTOR_ADDRESS;
        if (*backup_magic == VAULT_HEADER_MAGIC) {
            restore_backup(0);
            return;
        }
        s0 = cache_get_sector(0);
        memset(s0, 0xFF, WOLFBOOT_SECTOR_SIZE);
        magic = (uint32_t *)s0;
        *magic = VAULT_HEADER_MAGIC;
        memset(s0 + sizeof(uint32_t), 0x00, BITMAP_SIZE);
        cache_flush_all();
        hal_flash_unlock();
        hal_flash_erase((uintptr_t)vault_base + WOLFBOOT_SECTOR_SIZE * 2, total_vault_size);
        hal_flash_lock();
#ifdef PKCS11_STORE_STATS
        stats_erases += total_vault_size / WOLFBOOT_SECTOR_SIZE;
#endif
    }
}

static void delete_object(int32_t type, uint32_t tok_id, uint32_t obj_id)
{
    struct obj_hdr *hdr;
    uint8_t *s0;

    /* Deletions are durable on return, like the historical per-write
     * commits: validate the vault (commits pending sectors) and commit
     * the whole batch before returning. */
    check_vault();
    s0 = cache_get_sector(0);
    hdr = (struct obj_hdr *)(s0 + STORE_PRIV_HDR_OFFSET);

    while ((uintptr_t)hdr < ((uintptr_t)s0 + WOLFBOOT_SECTOR_SIZE)) {
        if ((hdr->token_id == tok_id) && (hdr->object_id == obj_id) &&
                (hdr->type == type)) {
            hdr->token_id = PKCS11_INVALID_ID;
            hdr->object_id = PKCS11_INVALID_ID;
            bitmap_put(hdr->pos, 0);
            cache_flush_all();
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
    struct obj_hdr *hdr;
    uint32_t *tok_obj_stored = NULL;
    uint8_t *s0 = sector0_ptr();

    hdr = (struct obj_hdr *)(s0 + STORE_PRIV_HDR_OFFSET);
    while ((uintptr_t)hdr < ((uintptr_t)s0 + WOLFBOOT_SECTOR_SIZE)) {
        if ((hdr->token_id == tok_id) && (hdr->object_id == obj_id)
                && (hdr->type == type)) {
            uint32_t obj_off = 2 * WOLFBOOT_SECTOR_SIZE +
                hdr->pos * KEYVAULT_OBJ_SIZE;
            uint32_t in_sector_off = obj_off % WOLFBOOT_SECTOR_SIZE;
            uint32_t sector_base = obj_off - in_sector_off;

            tok_obj_stored = (uint32_t *)(sector_ptr(sector_base) +
                in_sector_off);
            if ((tok_obj_stored[0] != tok_id) || (tok_obj_stored[1] != obj_id)) {
                /* Id's don't match. Try backup sector. */
                tok_obj_stored = (uint32_t *)(BACKUP_SECTOR_ADDRESS +
                    in_sector_off);
                if ((tok_obj_stored[0] == tok_id) &&
                        (tok_obj_stored[1] == obj_id)) {
                    /* Found backup! restoring... */
                    restore_backup(sector_base);
                } else {
                    delete_object(type, tok_id, obj_id);
                    return NULL; /* Cannot recover object payload */
                }
            }
            /* Object is now OK */
            return vault_base + obj_off;
        }
        hdr++;
    }
    return NULL; /* object not found */
}

static struct obj_hdr *find_object_header(int32_t type, uint32_t tok_id,
        uint32_t obj_id)
{
    struct obj_hdr *hdr;
    uint8_t *s0 = sector0_ptr();

    hdr = (struct obj_hdr *)(s0 + STORE_PRIV_HDR_OFFSET);
    while ((uintptr_t)hdr < ((uintptr_t)s0 + WOLFBOOT_SECTOR_SIZE)) {
        if ((hdr->token_id == tok_id) && (hdr->object_id == obj_id)
                && (hdr->type == type)) {
            /* Return the flash address of the node */
            return (struct obj_hdr *)(vault_base +
                ((uint8_t *)hdr - (uint8_t *)s0));
        }
        hdr++;
    }
    return NULL; /* object not found */
}

static struct obj_hdr *create_object(int32_t type, uint32_t tok_id, uint32_t obj_id)
{
    struct obj_hdr *hdr = NULL;
    uint32_t *tok_obj_id;
    uint8_t *s0;
    uint8_t *pay;
    uint32_t sector_base, in_sector_off;
    /* Refuse to create an object that's already in store */
    if (find_object_buffer(type, tok_id, obj_id) != NULL) {
        return NULL;
    }

    /* Caching sector 0 */
    s0 = cache_get_sector(0);
    hdr = (struct obj_hdr *)(s0 + STORE_PRIV_HDR_OFFSET);
    while ((uintptr_t)hdr < ((uintptr_t)s0 + WOLFBOOT_SECTOR_SIZE)) {
        if (hdr->token_id == PKCS11_INVALID_ID) {
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
            /* Mark the beginning of the object in the sector,
             * write the tok/obj ids. Stays in the cache until the
             * window is closed.
             */
            pay = cache_get_sector(sector_base);
            tok_obj_id = (uint32_t *)(pay + in_sector_off);
            tok_obj_id[0] = tok_id;
            tok_obj_id[1] = obj_id;
            /* Return the address of the header in flash */
            return (struct obj_hdr *)(vault_base +
                ((uint8_t *)hdr - (uint8_t *)s0));
        }
        hdr++;
    }
    return NULL; /* No space left in the nodes table */
}

static void update_store_size(struct store_handle *handle,
        struct obj_hdr *hdr, uint32_t size)
{
    uint32_t off;
    uint8_t *s0;
    struct obj_hdr *hdr_mem;

    if (((uint8_t *)hdr) < vault_base ||
        ((uint8_t *)hdr > vault_base + WOLFBOOT_SECTOR_SIZE)) {
        return;
    }
    off = (uintptr_t)hdr - (uintptr_t)vault_base;
    s0 = cache_get_sector(0);
    hdr_mem = (struct obj_hdr *)(s0 + off);
    hdr_mem->size = size;
    handle->size = size;
}

static void erase_object_payload(uint8_t *buf)
{
    uint32_t erase_off;
    uint32_t erase_end;
    uint32_t sector_base;

    erase_off = (uint32_t)((uintptr_t)buf - (uintptr_t)vault_base) +
        (2U * sizeof(uint32_t));
    erase_end = (uint32_t)((uintptr_t)buf - (uintptr_t)vault_base) +
        KEYVAULT_OBJ_SIZE;
    sector_base = erase_off - (erase_off % WOLFBOOT_SECTOR_SIZE);

    while (sector_base < erase_end) {
        uint32_t erase_start = erase_off;
        uint32_t erase_stop = sector_base + WOLFBOOT_SECTOR_SIZE;
        uint8_t *s;

        if (erase_start < sector_base) {
            erase_start = sector_base;
        }
        if (erase_stop > erase_end) {
            erase_stop = erase_end;
        }

        s = cache_get_sector(sector_base);
        memset(s + (erase_start - sector_base), 0xFF,
            erase_stop - erase_start);
        sector_base += WOLFBOOT_SECTOR_SIZE;
    }
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
    uint32_t hdr_off;
    int is_new = 0;

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
        is_new = 1;
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
    if (read) {
        handle->flags |= STORE_FLAGS_READONLY;
        /* Live size from the (possibly cached) header sector */
        hdr_off = (uintptr_t)handle->hdr - (uintptr_t)vault_base;
        handle->size = ((struct obj_hdr *)(sector0_ptr() + hdr_off))->size;
    } else {
        handle->flags &= ~STORE_FLAGS_READONLY;
        /* Truncate the slot when opening in write mode */
        update_store_size(handle, handle->hdr, 2 * sizeof(uint32_t));
        /* Erase object data sectors to clear residual key material from a
         * prior (longer) payload. New objects are already in a fresh sector
         * from create_object(), so only do this for existing objects. */
        if (!is_new) {
            erase_object_payload(buf);
        }
    }


    /* Set start of the buffer after the tok/obj id fields */
    handle->in_buffer_offset = (2 * sizeof(uint32_t));
    *store = handle;
    return 0;
}

void wolfPKCS11_Store_Close(void* store)
{
    struct store_handle *handle = store;
    /* Commit all pending sectors: the header sector last, so the header
     * is the atomic commit point of the window. */
    cache_flush_all();
    memset(handle, 0, sizeof(*handle));
}

int wolfPKCS11_Store_Read(void* store, unsigned char* buffer, int len)
{
    struct store_handle *handle = store;
    uint32_t obj_size = 0;
    uint32_t src_off;
    uint32_t remaining;
    if ((handle == NULL) || (handle->hdr == NULL) || (handle->buffer == NULL))
       return -1;

    obj_size = handle->size;
    if (obj_size > KEYVAULT_OBJ_SIZE)
        return -1;

    if (handle->in_buffer_offset >= obj_size)
        return 0; /* "EOF" */

    /* Truncate len to actual available bytes */
    if (handle->in_buffer_offset + len > obj_size)
        len = (obj_size - handle->in_buffer_offset);

    if (len > 0) {
        /* Read through sector_ptr() like every other read in this file:
         * the RAM copy when the sector is cached, flash otherwise, so a
         * cached (not yet committed) sector can never be read stale. */
        src_off = (uint32_t)((uintptr_t)handle->buffer +
            handle->in_buffer_offset - (uintptr_t)vault_base);
        remaining = (uint32_t)len;
        while (remaining > 0) {
            uint32_t in_sector = src_off % WOLFBOOT_SECTOR_SIZE;
            uint32_t chunk = WOLFBOOT_SECTOR_SIZE - in_sector;
            uint8_t *s;

            if (chunk > remaining) {
                chunk = remaining;
            }
            s = sector_ptr(src_off - in_sector);
            memcpy(buffer, s + in_sector, chunk);
            buffer += chunk;
            src_off += chunk;
            remaining -= chunk;
        }
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
    uint8_t *s;
    int written = 0;


    if ((handle == NULL) || (handle->hdr == NULL) || (handle->buffer == NULL))
       return -1;
    if ((handle->flags & STORE_FLAGS_READONLY) != 0)
        return -1;

    obj_size = handle->size;
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
        if (in_sector_len > (uint32_t)(len - written))
            in_sector_len = len - written;

        /* Copy the write into the sector cache; the sector is committed
         * at Store_Close (or on LRU eviction). */
        s = cache_get_sector(
            (uint32_t)((uintptr_t)sector_base - (uintptr_t)vault_base));
        memcpy(s + in_sector_offset, buffer + written, in_sector_len);
        /* Adjust in_buffer position for the handle accordingly */
        handle->in_buffer_offset += in_sector_len;
        written += in_sector_len;
    }
    obj_size += written;
    update_store_size(handle, handle->hdr, obj_size);
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
