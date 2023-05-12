/* pkcs11_store.c
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



#include <stdint.h>
#include <string.h>
#include "wolfpkcs11/pkcs11.h"
#include "wolfpkcs11/store.h"
#include "hal.h"

extern uint32_t *_flash_keyvault; /* From linker script: origin of vault flash */
extern uint32_t *_flash_keyvault_size; /* From linker script: size of vault */

#define KEYVAULT_OBJ_SIZE 0x1000 /* 4KB per object */
#define KEYVAULT_MAX_ITEMS 0x18 /* Total memory: 0x18000, 24 items */

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

static uint8_t *vault_base = (uint8_t *)&_flash_keyvault;
static int vault_idx = -1;


struct obj_hdr
{
    uint32_t token_id;
    uint32_t object_id;
    int type;
    uint32_t size;
};
#define STORE_PRIV_HDR_SIZE 16

struct store_object
{
    struct obj_hdr hdr;
    int vault_idx;
    int read;
};

static struct store_object *vault_descriptors[KEYVAULT_MAX_ITEMS];


int wolfPKCS11_Store_Open(int type, CK_ULONG id1, CK_ULONG id2, int read,
    void** store)
{
    unsigned int i;
    int found = -1;
    struct obj_hdr *hdr;
    struct store_object *obj;

    for (i = 0; i < KEYVAULT_MAX_ITEMS; i++) {
        hdr = (struct obj_hdr*)(vault_base + i * KEYVAULT_OBJ_SIZE);
        if ((type == hdr->type) && (id1 == hdr->token_id) &&
                (id2 == hdr->object_id)) {
            found = i;
            break;
        }
    }
    if (!found && read) {
        *store = NULL;
        return NOT_AVAILABLE_E;
    } else if (found) {
        *store = vault_descriptors[found];
        obj = vault_descriptors[found];
        memcpy(&obj->hdr, vault_base + found * KEYVAULT_OBJ_SIZE, sizeof(struct obj_hdr));
        obj->vault_idx = found;
        obj->read = read;
    } else if ((!found) && (!read)) {
        if (vault_idx++ >= KEYVAULT_MAX_ITEMS) {
            vault_idx--;
            *store = NULL;
            return FIND_FULL_E;
        }
        obj = vault_descriptors[vault_idx];
        obj->vault_idx = vault_idx;
        obj->hdr.type = type;
        obj->hdr.token_id = id1;
        obj->hdr.object_id = id2;
        obj->hdr.size = 0;
        obj->read = 0;
        hal_flash_erase((uint32_t)(vault_base + vault_idx * KEYVAULT_OBJ_SIZE),
                KEYVAULT_OBJ_SIZE);
        hal_flash_write((uint32_t)(vault_base + vault_idx * KEYVAULT_OBJ_SIZE), (void *)obj,
                sizeof(struct obj_hdr));
    }
    return 0;
}

void wolfPKCS11_Store_Close(void* store)
{
    /* Stub */
}

int wolfPKCS11_Store_Read(void* store, unsigned char* buffer, int len)
{
    struct store_object *obj = store;
    if ((uint32_t)len > obj->hdr.size) {
        len = obj->hdr.size;
    }
    if (len > 0) {
        memcpy(buffer, vault_base + obj->vault_idx * KEYVAULT_OBJ_SIZE +
                STORE_PRIV_HDR_SIZE, len);
    }
    return len;
}

int wolfPKCS11_Store_Write(void* store, unsigned char* buffer, int len)
{
    struct store_object *obj = store;
    int pos = 0;
    if (len > (KEYVAULT_OBJ_SIZE - STORE_PRIV_HDR_SIZE)) {
        return -1;
    }
    if (obj->read)
        return -1;
    if (obj->vault_idx > KEYVAULT_MAX_ITEMS)
        return -1;
    obj->hdr.size = len;
    hal_flash_erase((uint32_t)(vault_base + obj->vault_idx * KEYVAULT_OBJ_SIZE),
            KEYVAULT_OBJ_SIZE);
    hal_flash_write((uint32_t)(vault_base + obj->vault_idx * KEYVAULT_OBJ_SIZE),
            (void *)obj, sizeof(struct obj_hdr));
    while (pos < len) {
        uint32_t base = (uint32_t)(vault_base +
                obj->vault_idx * KEYVAULT_OBJ_SIZE);
        uint32_t sz = len;
        if (sz > WOLFBOOT_SECTOR_SIZE) {
            sz = WOLFBOOT_SECTOR_SIZE;
        }
        hal_flash_write(base + STORE_PRIV_HDR_SIZE + pos, buffer + pos, sz);
        pos += sz;
    }
    return len;
}
