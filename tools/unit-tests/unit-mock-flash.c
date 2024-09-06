/* unit-mock-flash.c
 *
 * Mock flash access for unit tests
 * usage: #include "unit-mock-flash.c"
 *
 *
 * Copyright (C) 2024 wolfSSL Inc.
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

static int locked = 1;
static int ext_locked = 1;
static int erased_boot = 0;
static int erased_update = 0;
static int erased_swap = 0;
static int erased_nvm_bank0 = 0;
static int erased_nvm_bank1 = 0;
static int erased_vault = 0;
const char *argv0;


/* Mocks */
void hal_init(void)
{
}
int hal_flash_write(haladdr_t address, const uint8_t *data, int len)
{
    int i;
    uint8_t *a = (uint8_t *)(uintptr_t)address;
    fail_if(locked, "Attempting to write to a locked FLASH");
    if ((address >= WOLFBOOT_PARTITION_SWAP_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_SECTOR_SIZE)) {
        for (i = 0; i < len; i++) {
            a[i] = data[i];
        }
    }
    if ((address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        for (i = 0; i < len; i++) {
            a[i] = data[i];
        }
    }
    if ((address >= WOLFBOOT_PARTITION_BOOT_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        for (i = 0; i < len; i++) {
            a[i] = data[i];
        }
    }
#ifdef MOCK_KEYVAULT
    if ((address >= (const uintptr_t)vault_base) && (address < (const uintptr_t)vault_base + keyvault_size)) {
        for (i = 0; i < len; i++) {
            a[i] = data[i];
        }
    }
#endif
    return 0;
}
int hal_flash_erase(haladdr_t address, int len)
{
    fail_if(locked, "Attempting to erase a locked FLASH");
    if ((address >= WOLFBOOT_PARTITION_BOOT_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        erased_boot++;
        memset((void*)(uintptr_t)address, 0xFF, len);
        if (address >= WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank0++;
        } else if (address >= WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 2 * WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank1++;
        }
    } else if ((address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        erased_update++;
        memset((void *)(uintptr_t)address, 0xFF, len);
        if (address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank0++;
        } else if (address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE - 2 * WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank1++;
        }
    } else if ((address >= WOLFBOOT_PARTITION_SWAP_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_SWAP_ADDRESS + WOLFBOOT_SECTOR_SIZE)) {
        erased_swap++;
        memset((void *)(uintptr_t)address, 0xFF, len);
#ifdef MOCK_KEYVAULT
    } else if ((address >= (uintptr_t)vault_base) && (address < (uintptr_t)vault_base + keyvault_size)) {
        printf("Erasing vault from %p : %p bytes\n", address, len);
        erased_vault++;
        memset((void *)(uintptr_t)address, 0xFF, len);
#endif
    } else {
        fail("Invalid address\n");
        return -1;
    }
    return 0;
}
void hal_flash_unlock(void)
{
    fail_unless(locked, "Double unlock detected\n");
    locked--;
}
void hal_flash_lock(void)
{
    fail_if(locked, "Double lock detected\n");
    locked++;
}

void hal_prepare_boot(void)
{
}

int ext_flash_erase(uintptr_t address, int len)
{
#ifdef PART_BOOT_EXT
    if ((address >= WOLFBOOT_PARTITION_BOOT_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        erased_update++;
        memset((void *)(uintptr_t)address, 0xFF, len);
        if (address >= WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank0++;
        } else if (address >= WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 2 * WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank1++;
        }
    } else
#endif
    if ((address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        erased_update++;
        memset((void *)(uintptr_t)address, 0xFF, len);
        if (address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank0++;
        } else if (address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE - 2 * WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank1++;
        }
    } else if ((address >= WOLFBOOT_PARTITION_SWAP_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_SWAP_ADDRESS + WOLFBOOT_SECTOR_SIZE)) {
        erased_swap++;
        memset((void *)(uintptr_t)address, 0xFF, len);
    } else {
        fail("Invalid address: %p\n", address);
        return -1;
    }
    return 0;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    int i;
    uint8_t *a = (uint8_t *)address;
    fail_if(ext_locked, "Attempting to write to a locked FLASH");
    for (i = 0; i < len; i++) {
        a[i] = data[i];
    }
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    int i;
    uint8_t *a = (uint8_t *)address;
    for (i = 0; i < len; i++) {
         data[i] = a[i];
    }
    return len;
}

void ext_flash_unlock(void)
{
    fail_unless(ext_locked, "Double ext unlock detected\n");
    ext_locked--;
}
void ext_flash_lock(void)
{
    fail_if(ext_locked, "Double ext lock detected\n");
    ext_locked++;
}


/* A simple mock memory */
static int mmap_file(const char *path, uint8_t *address, uint32_t len,
        uint8_t** ret_address)
{
    struct stat st = { 0 };
    uint8_t *mmaped_addr;
    int ret;
    int fd;
    int i;

    if (path == NULL)
        return -1;

    fd = open(path, O_RDWR|O_CREAT|O_TRUNC, 0666);
    if (fd == -1) {
        fprintf(stderr, "can't open %s\n", path);
        return -1;
    }
    fprintf(stderr, "Open file: %s success.\n", path);
    for (i = 0; i < len; i+=4) {
        const uint32_t erased_word = 0xBADBADBA;
        write(fd, &erased_word, 4);
    }
    lseek(fd, SEEK_SET, 0);

    mmaped_addr = mmap(address, len, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, 0);
    if (mmaped_addr == MAP_FAILED) {
        fprintf(stderr, "MMAP failed.\n");
        return -1;
    }

    fprintf(stderr, "Simulator assigned %s to base %p\n", path, mmaped_addr);

    if (ret_address)
        *ret_address = mmaped_addr;

    close(fd);
    return 0;
}


/* End Mocks */
