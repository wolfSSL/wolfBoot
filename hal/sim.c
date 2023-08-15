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

/* Note: All logging must use stderr to avoid issue with scripts
 * printing version information */

#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/dyld.h>
#endif

#include "wolfboot/wolfboot.h"
#include "target.h"

/* Global pointer to the internal and external flash base */
uint8_t *sim_ram_base;
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
        fprintf(stderr, "can't open %s\n", path);
        return -1;
    }

    mmaped_addr = mmap(address, st.st_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, 0);
    if (mmaped_addr == MAP_FAILED)
        return -1;

    fprintf(stderr, "Simulator assigned %s to base %p\n", path, mmaped_addr);

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

int hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    /* implicit cast abide compiler warning */
    memcpy((void*)address, data, len);
    return 0;
}

int hal_flash_erase(uintptr_t address, int len)
{
    /* implicit cast abide compiler warning */
    fprintf(stderr, "hal_flash_erase addr %p len %d\n", (void*)address, len);
    if (address == erasefail_address + WOLFBOOT_PARTITION_BOOT_ADDRESS) {
        fprintf(stderr, "POWER FAILURE\n");
        /* Corrupt page */
        memset((void*)address, 0xEE, len);
        exit(0);
    }
    memset((void*)address, 0xff, len);
    return 0;
}

void hal_init(void)
{
    int ret;
    int i;

    ret = mmap_file(INTERNAL_FLASH_FILE,
        (uint8_t*)ARCH_FLASH_OFFSET, &sim_ram_base);
    if (ret != 0) {
        fprintf(stderr, "failed to load internal flash file\n");
        exit(-1);
    }

#ifdef EXT_FLASH
    ret = mmap_file(EXTERNAL_FLASH_FILE,
        (uint8_t*)ARCH_FLASH_OFFSET + 0x10000000, &flash_base);
    if (ret != 0) {
        fprintf(stderr, "failed to load internal flash file\n");
        exit(-1);
    }
#endif /* EXT_FLASH */

    for (i = 1; i < main_argc; i++) {
        if (strcmp(main_argv[i], "powerfail") == 0) {
            erasefail_address = strtol(main_argv[++i], NULL,  16);
            fprintf(stderr, "Set power fail to erase at address %x\n",
                erasefail_address);
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

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    memcpy(flash_base + address, data, len);
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    memcpy(data, flash_base + address, len);
    return len;
}

int ext_flash_erase(uintptr_t address, int len)
{
    memset(flash_base + address, 0xff, len);
    return 0;
}

#ifdef __APPLE__
#ifdef __GNUC__
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

/* Find the MachO entry point */
static int find_epc(void *base, struct entry_point_command **entry)
{
    struct mach_header_64 *mh;
    struct load_command *lc;
    int i;
    unsigned long text = 0;

    *entry = NULL;

    mh = (struct mach_header_64*)base;
    lc = (struct load_command*)(base + sizeof(struct mach_header_64));
    for (i=0; i<(int)mh->ncmds; i++) {
        if (lc->cmd == LC_MAIN) { /* 0x80000028 */
            *entry = (struct entry_point_command *)lc;
            return 1;
        }
        lc = (struct load_command*)((unsigned long)lc + lc->cmdsize);
    }
    return 0;
}
#endif

void do_boot(const uint32_t *app_offset)
{
    int ret;
    size_t app_size = WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE;

#ifdef __APPLE__
    typedef int (*main_entry)(int, char**, char**, char**);
    NSObjectFileImage fileImage = NULL;
    NSModule module = NULL;
    NSSymbol symbol = NULL;
    void *pSymbolAddress = NULL;
    struct entry_point_command *epc;
    main_entry main;
    uint32_t *app_buf = (uint32_t*)app_offset;
    uint32_t typeVal;

    /* change to mh_bundle type - workaround to load object */
    typeVal = app_buf[3];
    if (typeVal != MH_BUNDLE)
        app_buf[3] = MH_BUNDLE;

    ret = NSCreateObjectFileImageFromMemory(app_buf, app_size, &fileImage);
    if (ret != 1 || fileImage == NULL) {
        fprintf(stderr, "Error loading object memory!\n");
        exit(-1);
    }
    module = NSLinkModule(fileImage, "module",
        (NSLINKMODULE_OPTION_PRIVATE | NSLINKMODULE_OPTION_BINDNOW));
    symbol = NSLookupSymbolInModule(module, "__mh_execute_header");
    pSymbolAddress = NSAddressOfSymbol(symbol);
    if (!find_epc(pSymbolAddress, &epc)) {
        fprintf(stderr, "Error finding entry point!\n");
        exit(-1);
    }

    /* restore mh_bundle type to allow hash to remain valid */
    app_buf[3] = typeVal;

    main = (main_entry)(void*)(pSymbolAddress + epc->entryoff);
    main(main_argc, main_argv, NULL, NULL);
#else
    char *envp[1] = {NULL};
    int fd = memfd_create("test_app", 0);
    if (fd == -1) {
        fprintf(stderr, "memfd error\n");
        exit(-1);
    }

    if ((size_t)write(fd, app_offset, app_size) != app_size) {
        fprintf(stderr, "can't write test-app to memfd\n");
        exit(-1);
    }

    ret = fexecve(fd, main_argv, envp);
    fprintf(stderr, "fexecve error\n");
#endif
    exit(1);
}

#ifdef __APPLE__
#ifdef __GNUC__
    #pragma GCC diagnostic pop
#endif
#endif

int wolfBoot_fallback_is_possible(void)
{
    return 0;
}

int wolfBoot_dualboot_candidate(void)
{
    return 0;
}
