/* sim.c
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
#include <errno.h>

#ifdef __APPLE__
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/dyld.h>
#endif

#include "wolfboot/wolfboot.h"
#include "target.h"
#include "printf.h"

#ifdef WOLFBOOT_ELF_FLASH_SCATTER
#include "elf.h"
#endif

#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_client.h"
#include "port/posix/posix_transport_tcp.h"
#elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) /*WOLFBOOT_ENABLE_WOLFHSM_CLIENT*/
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_server.h"
#include "wolfhsm/wh_server_keystore.h"
#include "wolfhsm/wh_nvm.h"
#include "wolfhsm/wh_nvm_flash.h"
#include "wolfhsm/wh_transport_mem.h"
#include "port/posix/posix_flash_file.h"
#endif /* WOLFBOOT_ENABLE_WOLFHSM_SERVER */

/* Global pointer to the internal and external flash base */
uint8_t *sim_ram_base;
static uint8_t *flash_base;

int forceEmergency = 0;
uint32_t erasefail_address = 0xFFFFFFFF;
int flashLocked = 1;
int extFlashLocked = 1;

#define INTERNAL_FLASH_FILE "./internal_flash.dd"
#define EXTERNAL_FLASH_FILE "./external_flash.dd"

#ifdef DUALBANK_SWAP
#define SIM_REGISTER_FILE "./sim_registers.dd"
#define SIM_FLASH_OPTR_SWAP_BANK (1U << 20)
static uint32_t sim_flash_optr;
static void sim_dualbank_register_load(void);
static void sim_dualbank_register_store(void);
uint32_t hal_sim_get_dualbank_state(void);
#endif

/* global used to store command line arguments to forward to the test
 * application */
char **main_argv;
int main_argc;

#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT

/* Client configuration/contexts */
static whTransportClientCb            pttccb[1]      = {PTT_CLIENT_CB};
static posixTransportTcpClientContext tcc[1]         = {};
static posixTransportTcpConfig        mytcpconfig[1] = {{
           .server_ip_string = "127.0.0.1",
           .server_port      = 23456,
}};

static whCommClientConfig cc_conf[1] = {{
    .transport_cb      = pttccb,
    .transport_context = (void*)tcc,
    .transport_config  = (void*)mytcpconfig,
    .client_id         = 12,
}};
static whClientConfig     c_conf[1]  = {{
         .comm = cc_conf,
}};

/* Globally exported HAL symbols */
whClientContext hsmClientCtx   = {0};
const int       hsmDevIdHash   = WH_DEV_ID;
const int       hsmDevIdPubKey = WH_DEV_ID;
const int       hsmKeyIdPubKey = 0xFF;
#ifdef EXT_ENCRYPT
#error "Simulator does not support firmware encryption with wolfHSM(yet)"
const int hsmDevIdCrypt = WH_DEV_ID;
const int hsmKeyIdCrypt = 0xFF;
#endif
#ifdef WOLFBOOT_CERT_CHAIN_VERIFY
const whNvmId hsmNvmIdCertRootCA = 1;
#endif

int hal_hsm_init_connect(void);
int hal_hsm_disconnect(void);

#elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) /*WOLFBOOT_ENABLE_WOLFHSM_CLIENT*/

/* HAL Flash state and configuration */
const whFlashCb       fcb[1]     = {POSIX_FLASH_FILE_CB};
posixFlashFileContext fc[1]      = {0};
posixFlashFileConfig  fc_conf[1] = {{
     .filename       = "wolfBoot_wolfHSM_NVM.bin",
     .partition_size = 16384,
     .erased_byte    = (uint8_t)0,
}};
/* NVM Configuration using PosixSim HAL Flash */
whNvmFlashConfig  nf_conf[1] = {{
     .cb      = fcb,
     .context = fc,
     .config  = fc_conf,
}};
whNvmFlashContext nfc[1]     = {0};
whNvmCb           nfcb[1]    = {WH_NVM_FLASH_CB};

whNvmConfig  n_conf[1] = {{
     .cb      = nfcb,
     .context = nfc,
     .config  = nf_conf,
}};
whNvmContext nvm[1]    = {{0}};

static uint8_t req[]  = {0};
static uint8_t resp[] = {0};

whTransportMemConfig        tmcf[1] = {{
           .req       = (whTransportMemCsr*)req,
           .req_size  = sizeof(req),
           .resp      = (whTransportMemCsr*)resp,
           .resp_size = sizeof(resp),
}};
whTransportServerCb         tscb[1] = {WH_TRANSPORT_MEM_SERVER_CB};
whTransportMemServerContext tmsc[1] = {0};
/* Dummy comm server config */
whCommServerConfig cs_conf[1] = {{
    .transport_cb      = tscb,
    .transport_context = &tmsc,
    .transport_config  = &tmcf,
    .server_id         = 0,
}};

/* Crypto context */
whServerCryptoContext crypto[1] = {{
    .devId = INVALID_DEVID,
}};

#if defined(WOLFHSM_CFG_SHE_EXTENSION)
whServerSheContext    she[1]    = {{0}};
#endif

whServerConfig s_conf[1] = {{
    .comm_config = cs_conf,
    .nvm         = nvm,
    .crypto      = crypto,
}};

whServerContext hsmServerCtx = {0};

const int     hsmDevIdHash       = INVALID_DEVID;
const int     hsmDevIdPubKey     = INVALID_DEVID;
const whNvmId hsmNvmIdCertRootCA = 1;
#ifdef EXT_ENCRYPT
#error "Simulator does not support firmware encryption with wolfHSM(yet)"
const int     hsmDevIdCrypt      = WH_DEV_ID;
const int     hsmKeyIdCrypt      = 0xFF;
#endif

int hal_hsm_server_init(void);
int hal_hsm_server_cleanup(void);

#endif /* WOLFBOOT_ENABLE_WOLFHSM_SERVER*/

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
        wolfBoot_printf( "can't open %s\n", path);
        return -1;
    }

    mmaped_addr = mmap(address, st.st_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, 0);
    if (mmaped_addr == MAP_FAILED)
        return -1;

    wolfBoot_printf( "Simulator assigned %s to base %p\n", path, mmaped_addr);

    *ret_address = mmaped_addr;

    close(fd);
    return 0;
}

#ifdef DUALBANK_SWAP
static void sim_dualbank_register_store(void)
{
    int fd = open(SIM_REGISTER_FILE, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        wolfBoot_printf("Failed to open %s: %s\n", SIM_REGISTER_FILE, strerror(errno));
        return;
    }

    if (pwrite(fd, &sim_flash_optr, sizeof(sim_flash_optr), 0) !=
            (ssize_t)sizeof(sim_flash_optr)) {
        wolfBoot_printf("Failed to store dualbank swap state: %s\n",
            strerror(errno));
    }

    close(fd);
}

static void sim_dualbank_register_load(void)
{
    int fd = open(SIM_REGISTER_FILE, O_RDWR | O_CREAT, 0644);
    uint32_t value = 0;
    int rd;
    if (fd == -1) {
        wolfBoot_printf("Failed to open %s: %s\n", SIM_REGISTER_FILE,
            strerror(errno));
        exit(-1);
    }

    rd = pread(fd, &value, sizeof(value), 0);

    if (rd == (int)sizeof(value)) {
        sim_flash_optr = value;
    } else {
        sim_flash_optr = 0;
        if (pwrite(fd, &sim_flash_optr, sizeof(sim_flash_optr), 0) !=
                sizeof(sim_flash_optr)) {
            wolfBoot_printf("Failed to initialize dualbank swap state: %s\n",
                strerror(errno));
        }
    }

    close(fd);
}

uint32_t hal_sim_get_dualbank_state(void)
{
    return (sim_flash_optr & SIM_FLASH_OPTR_SWAP_BANK) ? 1U : 0U;
}
#endif

void hal_flash_unlock(void)
{
    flashLocked = 0;
}

void hal_flash_lock(void)
{
    flashLocked = 1;
}

#ifdef DUALBANK_SWAP
void hal_flash_dualbank_swap(void)
{
    uint8_t *boot = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uint8_t *update = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    uint8_t *buffer;
    int was_locked = flashLocked;

    buffer = (uint8_t *)malloc(WOLFBOOT_PARTITION_SIZE);
    if (buffer == NULL) {
        wolfBoot_printf("Simulator dualbank swap failed: out of memory\n");
        exit(-1);
    }

    if (was_locked)
        hal_flash_unlock();

    memcpy(buffer, boot, WOLFBOOT_PARTITION_SIZE);
    memcpy(boot, update, WOLFBOOT_PARTITION_SIZE);
    memcpy(update, buffer, WOLFBOOT_PARTITION_SIZE);

    if (msync(boot, WOLFBOOT_PARTITION_SIZE, MS_SYNC) != 0) {
        wolfBoot_printf("msync boot partition failed: %s\n", strerror(errno));
    }
    if (msync(update, WOLFBOOT_PARTITION_SIZE, MS_SYNC) != 0) {
        wolfBoot_printf("msync update partition failed: %s\n", strerror(errno));
    }

    free(buffer);

    sim_flash_optr ^= SIM_FLASH_OPTR_SWAP_BANK;
    sim_dualbank_register_store();
    wolfBoot_printf("Simulator dualbank swap complete, register=%u\n",
        hal_sim_get_dualbank_state());

    if (was_locked)
        hal_flash_lock();
}
#endif

void hal_prepare_boot(void)
{
    /* no op */
}

int hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    int i;
    if (flashLocked == 1) {
        wolfBoot_printf("FLASH IS BEING WRITTEN TO WHILE LOCKED\n");
        return -1;
    }
    if (forceEmergency == 1 && address == WOLFBOOT_PARTITION_BOOT_ADDRESS) {
        /* implicit cast abide compiler warning */
        memset((void*)address, 0, len);
        /* let the rest of the writes work properly for the emergency update */
        forceEmergency = 0;
    }
    else {
        for (i = 0; i < len; i++) {
#ifdef NVM_FLASH_WRITEONCE
            uint8_t *addr = (uint8_t *)address;
            if (addr[i] != FLASH_BYTE_ERASED) {
                /* no writing to non-erased page in NVM_FLASH_WRITEONCE */
                wolfBoot_printf("NVM_FLASH_WRITEONCE non-erased write detected at address %p!\n", addr);
                wolfBoot_printf("Address[%d] = %02x\n", i, addr[i]);
                return -1;
            }
#endif
#ifdef WOLFBOOT_FLAGS_INVERT
            ((uint8_t*)address)[i] |= data[i];
#else
            ((uint8_t*)address)[i] &= data[i];
#endif
        }
    }
    return 0;
}

int hal_flash_erase(uintptr_t address, int len)
{
    if (flashLocked == 1) {
        wolfBoot_printf("FLASH IS BEING ERASED WHILE LOCKED\n");
        return -1;
    }
    /* implicit cast abide compiler warning */
    wolfBoot_printf( "hal_flash_erase addr %p len %d\n", (void*)address, len);
    if (address == erasefail_address + WOLFBOOT_PARTITION_BOOT_ADDRESS) {
        wolfBoot_printf( "POWER FAILURE\n");
        /* Corrupt page */
        memset((void*)address, 0xEE, len);
        exit(0);
    }
    memset((void*)address, FLASH_BYTE_ERASED, len);
    return 0;
}

void hal_init(void)
{
    int ret;
    int i;

    ret = mmap_file(INTERNAL_FLASH_FILE,
        (uint8_t*)ARCH_FLASH_OFFSET, &sim_ram_base);
    if (ret != 0) {
        wolfBoot_printf( "failed to load internal flash file\n");
        exit(-1);
    }

#ifdef EXT_FLASH
    ret = mmap_file(EXTERNAL_FLASH_FILE,
        (uint8_t*)ARCH_FLASH_OFFSET + 0x10000000, &flash_base);
    if (ret != 0) {
        wolfBoot_printf( "failed to load external flash file\n");
        exit(-1);
    }
#endif /* EXT_FLASH */

#ifdef DUALBANK_SWAP
    sim_dualbank_register_load();
#endif

    for (i = 1; i < main_argc; i++) {
        if (strcmp(main_argv[i], "powerfail") == 0) {
            erasefail_address = strtol(main_argv[++i], NULL,  16);
            wolfBoot_printf( "Set power fail to erase at address %x\n",
                erasefail_address);
        }
        /* force a bad write of the boot partition to trigger and test the
         * emergency fallback feature */
        else if (strcmp(main_argv[i], "emergency") == 0)
            forceEmergency = 1;
    }
}

void ext_flash_lock(void)
{
    extFlashLocked = 1;
}

void ext_flash_unlock(void)
{
    extFlashLocked = 0;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    if (extFlashLocked == 1) {
        wolfBoot_printf("EXT FLASH IS BEING WRITTEN TO WHILE LOCKED\n");
        return -1;
    }
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
    if (extFlashLocked == 1) {
        wolfBoot_printf("EXT FLASH IS BEING ERASED WHILE LOCKED\n");
        return -1;
    }
    memset(flash_base + address, FLASH_BYTE_ERASED, len);
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
    lc = (struct load_command*)((uint8_t *)base + sizeof(struct mach_header_64));
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
    wolfBoot_printf("Simulator do_boot app_offset = %p\n", app_offset);

    if (flashLocked == 0) {
        wolfBoot_printf("WARNING FLASH IS UNLOCKED AT BOOT");
    }

    if (extFlashLocked == 0) {
        wolfBoot_printf("WARNING EXT FLASH IS UNLOCKED AT BOOT");
    }

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
        wolfBoot_printf( "Error loading object memory!\n");
        exit(-1);
    }
    module = NSLinkModule(fileImage, "module",
        (NSLINKMODULE_OPTION_PRIVATE | NSLINKMODULE_OPTION_BINDNOW));
    symbol = NSLookupSymbolInModule(module, "__mh_execute_header");
    pSymbolAddress = NSAddressOfSymbol(symbol);
    if (!find_epc(pSymbolAddress, &epc)) {
        wolfBoot_printf( "Error finding entry point!\n");
        exit(-1);
    }

    /* restore mh_bundle type to allow hash to remain valid */
    app_buf[3] = typeVal;

    main = (main_entry)((uint8_t*)pSymbolAddress + epc->entryoff);
    main(main_argc, main_argv, NULL, NULL);

#elif defined (WOLFBOOT_ELF_FLASH_SCATTER)
    uint8_t *entry_point = (sim_ram_base + (unsigned long)app_offset);
    printf("entry point: %p\n", entry_point);
    printf("app offset: %p\n", app_offset);
    typedef int (*main_entry)(int, char**);
    main_entry main;
    main = (main_entry)(entry_point);

    /* TODO: call main ! */
    /* main(main_argc, main_argv); */
    wolfBoot_printf("Simulator for ELF_FLASH_SCATTER image not implemented yet. Exiting...\n");
    exit(0);
#else
    char *envp[1] = {NULL};
    int fd = memfd_create("test_app", 0);
    size_t wret;
    if (fd == -1) {
        wolfBoot_printf( "memfd error\n");
        exit(-1);
    }

    wret = write(fd, app_offset, app_size);
    if (wret != app_size) {
        wolfBoot_printf( "can't write test-app to memfd, address %p\n", app_offset);
        exit(-1);
    }
    wolfBoot_printf("Stored test-app to memfd, address %p (%zu bytes)\n", app_offset, wret);

    ret = fexecve(fd, main_argv, envp);
    wolfBoot_printf( "fexecve error\n");
#endif
    exit(1);
}

#ifdef __APPLE__
#ifdef __GNUC__
    #pragma GCC diagnostic pop
#endif
#endif

#if !defined(WOLFBOOT_DUALBOOT)
int wolfBoot_fallback_is_possible(void)
{
    return 0;
}

int wolfBoot_dualboot_candidate(void)
{
    return 0;
}
#endif

#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT

int hal_hsm_init_connect(void)
{
    int rc = 0;

    rc = wh_Client_Init(&hsmClientCtx, c_conf);
    if (rc != WH_ERROR_OK) {
        fprintf(stderr, "Failed to initialize HSM client\n");
        exit(-1);
    }

    rc = wh_Client_CommInit(&hsmClientCtx, NULL, NULL);
    if (rc != WH_ERROR_OK) {
        fprintf(stderr, "Failed to initialize HSM client communication\n");
        exit(-1);
    }

    return rc;
}


int hal_hsm_disconnect(void)
{
    int rc = 0;

    rc = wh_Client_CommClose(&hsmClientCtx);
    if (rc != WH_ERROR_OK) {
        fprintf(stderr, "Failed to close HSM client connection\n");
        exit(-1);
    }

    rc = wh_Client_Cleanup(&hsmClientCtx);
    if (rc != WH_ERROR_OK) {
        fprintf(stderr, "Failed to cleanup HSM client\n");
        exit(-1);
    }
    return rc;
}

#elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) /*WOLFBOOT_ENABLE_WOLFHSM_CLIENT*/

int hal_hsm_server_init(void)
{
    int rc = 0;

    rc = wh_Nvm_Init(nvm, n_conf);
    if (rc != 0) {
        fprintf(stderr, "Failed to initialize NVM: %d\n", rc);
        exit(-1);
    }

    wolfCrypt_Init();

    rc = wc_InitRng_ex(crypto->rng, NULL, INVALID_DEVID);
    if (rc != 0) {
        fprintf(stderr, "Failed to initialize RNG: %d\n", rc);
        exit(-1);
    }

    rc = wh_Server_Init(&hsmServerCtx, s_conf);
    if (rc != 0) {
        fprintf(stderr, "Failed to initialize HSM server: %d\n", rc);
        exit(-1);
    }

    return rc;
}

int hal_hsm_server_cleanup(void)
{
    int rc = 0;

    rc = wh_Server_Cleanup(&hsmServerCtx);
    if (rc != 0) {
        fprintf(stderr, "Failed to cleanup HSM server: %d\n", rc);
        exit(-1);
    }

    rc = wc_FreeRng(crypto->rng);
    if (rc != 0) {
        fprintf(stderr, "Failed to cleanup RNG: %d\n", rc);
        exit(-1);
    }

    rc = wolfCrypt_Cleanup();
    if (rc != 0) {
        fprintf(stderr, "Failed to cleanup wolfCrypt: %d\n", rc);
        exit(-1);
    }

    return rc;
}
#endif /* WOLFBOOT_ENABLE_WOLFHSM_SERVER */
