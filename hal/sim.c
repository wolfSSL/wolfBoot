/* sim.c
 *
 * Copyright (C) 2022 wolfSSL Inc.
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

#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "wolfboot/wolfboot.h"
#include "target.h"

static uint8_t *ram_base;
static uint8_t *flash_base;

uint32_t erasefail_address = 0xFFFFFFFF;

#define INTERNAL_FLASH_FILE "./internal_flash.dd"
#define EXTERNAL_FLASH_FILE "./external_flash.dd"

/* global used to store command line arguments to forward to the test
 * application */
char **main_argv;
int main_argc;

static int mmap_file(const char *path, uint8_t *address, uint8_t** ret_address)
{
    struct stat st = { 0 };
    uint8_t *mmaped_addr;
    int ret;
    int fd;

    if (path == NULL)
        return -1;

    ret = stat(path, &st);
    if (ret == -1)
        return -1;

    fd = open(path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr,"can't open %s\n", path);
        return -1;
    }

    mmaped_addr = mmap(address, st.st_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, 0);
    if (mmaped_addr == MAP_FAILED)
        return -1;

    if (address != NULL && mmaped_addr != address) {
        munmap(address, st.st_size);
        return -1;
    }

    *ret_address = mmaped_addr;

    close(fd);
    return 0;
}

void hal_flash_unlock(void)
{
    /* no op */
}

void hal_flash_lock(void)
{
    /* no op */
}

void hal_prepare_boot(void)
{
    /* no op */
}

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint8_t *ptr = 0;

    /* implicit cast abide compiler warning */
    memcpy(ptr + address, data, len);
    return 0;
}

int hal_flash_erase(uint32_t address, int len)
{
    uint8_t *ptr = 0;

    /* implicit cast abide compiler warning */
    fprintf(stderr,"hal_flash_erase addr %x len %d\n", address, len);
    if (address == erasefail_address) {
        fprintf(stderr,"POWER FAILURE\n");
        /* Corrupt page */
        memset(ptr + address, 0xEE, len);
        exit(0);
    }
    memset(ptr + address, 0xff, len);
    return 0;
}

void hal_init(void)
{
    int ret;
    uint8_t *p;
    int i;
    ret = mmap_file(INTERNAL_FLASH_FILE,
                    (uint8_t*)ARCH_FLASH_OFFSET, &p);
    if (ret != 0) {
        fprintf(stderr,"failed to load internal flash file\n");
        exit(-1);
    }

#ifdef EXT_FLASH
    ret = mmap_file(EXTERNAL_FLASH_FILE, (uint8_t*)ARCH_FLASH_OFFSET + 0x10000000, &flash_base);
    if (ret != 0) {
        fprintf(stderr,"failed to load internal flash file\n");
        exit(-1);
    }
#endif /* EXT_FLASH */

    for (i = 1; i < main_argc; i++) {
        if (strcmp(main_argv[i], "powerfail") == 0) {
            erasefail_address = strtol(main_argv[++i], NULL,  16);
            fprintf(stderr,"Set power fail to erase at address %x\n", erasefail_address);
            break;
        }
    }
}

void ext_flash_lock(void)
{
    /* no op */
}

void ext_flash_unlock(void)
{
    /* no op */
}

int  ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    memcpy(flash_base + address, data, len);
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    memcpy(data, flash_base + address, len);
    return len;
}

int  ext_flash_erase(uintptr_t address, int len)
{
    memset(flash_base + address, 0xff, len);
    return 0;
}

void do_boot(const uint32_t *app_offset)
{
    char *envp[1] = {NULL};
    int ret;
    int fd;

    fd = memfd_create("test_app", 0);
    if (fd == -1) {
        fprintf(stderr,"memfd error\n");
        exit(-1);
    }

    ret = write(fd, app_offset, WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE);
    if (ret != WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE) {
        fprintf(stderr,"can't write test-app to memfd\n");
        exit(-1);
    }

    ret = fexecve(fd, main_argv, envp);
    fprintf(stderr,"fexecve error\n");
    exit(1);
}

int wolfBoot_fallback_is_possible(void)
{
    return 0;
}

int wolfBoot_dualboot_candidate(void)
{
    return 0;
}
