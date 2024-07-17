/* renesas-ra.c
 *
 * Stubs for custom HAL implementation. Defines the
 * functions used by wolfboot for a specific target.
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "user_settings.h"

#include <target.h>
#include "hal.h"

#include "r_flash_hp.h"

#if defined(WOLFSSL_RENESAS_SCEPROTECT_CRYPTONLY) && \
     !defined(WOLFBOOT_RENESAS_APP)

#    include "wolfssl/wolfcrypt/wc_port.h"
#    include "wolfssl/wolfcrypt/port/Renesas/renesas-sce-crypt.h"
#    include "wolfssl/wolfcrypt/port/Renesas/renesas_sync.h"
     User_SCEPKCbInfo pkInfo;
     sce_rsa2048_public_wrapped_key_t wrapped_rsapub2048;
#endif

/* #define DEBUG_FLASH_WRITE_VERIFY */

static inline void hal_panic(void)
{
    while(1)
        ;
}

extern flash_ctrl_t g_flash0_ctrl;
extern flash_cfg_t g_flash0_cfg;

void hal_init(void)
{
    fsp_err_t err;

#if defined(WOLFBOOT_RENESAS_SCEPROTECT) && !defined(WOLFBOOT_RENESAS_APP)
    /* retrieve installed pubkey from flash */
    uint32_t *pubkey = keystore_get_buffer(0);
#endif
    err = R_FLASH_HP_Close(&g_flash0_ctrl);
    err = R_FLASH_HP_Open(&g_flash0_ctrl, &g_flash0_cfg);

     if(err != FSP_ERR_ALREADY_OPEN && err != FSP_SUCCESS){
         printf("ERROR: %d\n", err);
        hal_panic();
     }

    /* Setup Default  Block 0 as Startup Setup Block */
    err = R_FLASH_HP_StartUpAreaSelect(&g_flash0_ctrl, FLASH_STARTUP_AREA_BLOCK0, true);
    if(err != FSP_SUCCESS){
        printf("ERROR: %d\n", err);
        hal_panic();
    }
#if defined(WOLFBOOT_RENESAS_SCEPROTECT) && !defined(WOLFBOOT_RENESAS_APP)
    err = wolfCrypt_Init();
    if (err != 0) {
          printf("ERROR: wolfCrypt_Init %d\n", err);
          hal_panic();
    }

    XMEMSET(&pkInfo, 0, sizeof(pkInfo));
    pkInfo.sce_wrapped_key_rsapub2048 =
              (sce_rsa2048_public_wrapped_key_t*)&wrapped_rsapub2048;
    XMEMCPY(&wrapped_rsapub2048.value, (uint32_t*)pubkey,
                   sizeof(wrapped_rsapub2048.value));

    wrapped_rsapub2048.type =SCE_KEY_INDEX_TYPE_RSA2048_PUBLIC;
    pkInfo.keyflgs_crypt.bits.rsapub2048_installedkey_set = 1;
    pkInfo.keyflgs_crypt.bits.message_type = 1;
    err = wc_CryptoCb_CryptInitRenesasCmn(NULL, &pkInfo);

    if (err < 0) {
         printf("ERROR: wc_CryptoCb_CryptInitRenesasCmn %d\n", err);
         hal_panic();
    }

#endif
}

void hal_prepare_boot(void)
{

}

#define MINIMUM_BLOCK (128)
#define ALIGN_FLASH(a) (((a) / MINIMUM_BLOCK) * MINIMUM_BLOCK)
static uint8_t save[MINIMUM_BLOCK*sizeof(uint32_t)];
#ifdef DEBUG_FLASH_WRITE_VERIFY
static uint8_t readbuf[MINIMUM_BLOCK*sizeof(uint32_t)];
#endif

/* */
static int blockWrite(const uint8_t *data, uint32_t addr, uint32_t len)
{
    for(; len; len-=MINIMUM_BLOCK, data+=MINIMUM_BLOCK, addr+=MINIMUM_BLOCK) {
        /* for the case "data" ls a flash address */
        memcpy(save, data, MINIMUM_BLOCK);

        if(R_FLASH_HP_Write(&g_flash0_ctrl, (uint32_t)save, addr, MINIMUM_BLOCK)
                                             != FSP_SUCCESS)
            return -1;
    }
    return 0;
}

#define IS_FLASH_ADDR(addr) (addr) >= 0xffc00000 ? 1 : 0

int hal_flash_write(uint32_t addr, const uint8_t *data, int int_len)
{
    fsp_err_t err;
    uint32_t len = (uint32_t)int_len;
    uint32_t save_len = 0;
    uint8_t address_tmp = (uint8_t)(addr - ALIGN_FLASH(addr));

    if(addr != ALIGN_FLASH(addr)) {

        memset(save, 0, sizeof(save));

        save_len = (addr - ALIGN_FLASH(addr)) < len ?
                                    (addr - ALIGN_FLASH(addr)) : len;

        memcpy(save, (const void *)ALIGN_FLASH(addr),
                                    MINIMUM_BLOCK * sizeof(uint32_t));
        memcpy(save + (address_tmp), data, save_len);
        addr   = ALIGN_FLASH(addr);

        if((err=R_FLASH_HP_Erase(&g_flash0_ctrl, addr, 1)) != FSP_SUCCESS)
            return -1;

        if((err=R_FLASH_HP_Write(&g_flash0_ctrl, (uint32_t)save, addr,
                            MINIMUM_BLOCK * sizeof(uint32_t))) != FSP_SUCCESS)
            return -1;

#ifdef DEBUG_FLASH_WRITE_VERIFY
        memcpy(readbuf, (const void*)addr, MINIMUM_BLOCK * sizeof(uint32_t));
        if(memcmp(readbuf, save, MINIMUM_BLOCK * sizeof(uint32_t)) != 0) {
            return -1;
        }
#endif
        len -= save_len;
        data += save_len;
        addr += MINIMUM_BLOCK;
    }

    if(len > 0) {
        if(blockWrite(data, addr, ALIGN_FLASH(len)) < 0)
            goto error;
        addr += ALIGN_FLASH(len);
        data += ALIGN_FLASH(len);
        len  -= ALIGN_FLASH(len);
    }

    if(len > 0) {
        memcpy(save, (const void *)addr, MINIMUM_BLOCK);
        memcpy(save, data, len);
        if(R_FLASH_HP_Erase(&g_flash0_ctrl, addr, 1) != FSP_SUCCESS)
            return -1;
        if(R_FLASH_HP_Write(&g_flash0_ctrl,
                    (uint32_t)save, addr, MINIMUM_BLOCK) != FSP_SUCCESS)
            goto error;
    }
    return 0;

error:
    return -1;
}

int hal_flash_erase(uint32_t address, int int_len)
{
    uint32_t len = (uint32_t)int_len;
    #ifdef WOLFBOOT_DUALBANK
    uint32_t block_size = (address <= 0x80000 && address >= 0x10000)
                   || address >= 0x210000 ? (32*1024) :  (8*1024);
    #else /* Lenier mode */
    uint32_t block_size = address >= 0x10000 ? (32*1024) :  (8*1024);
    #endif

    if(len % block_size != 0)
        return -1;

    for( ; len; address+=block_size, len-=block_size) {
        if(R_FLASH_HP_Erase(&g_flash0_ctrl, address, 1)
                != FSP_SUCCESS)
            return -1;
    }
    return 0;
}

#ifdef WOLFBOOT_DUALBANK
    #define FLASH_START_ADDR 0x0
    #define FLASH_END_ADDR   0x7ffff
    #define FLASH1_START_ADDR 0x200000
    #define FLASH1_END_ADDR   0x27ffff
#else
    #define FLASH_START_ADDR 0x0
    #define FLASH_END_ADDR   0xfffff
#endif

void hal_flash_unlock(void)
{
    if(R_FLASH_HP_AccessWindowClear(&g_flash0_ctrl) != FSP_SUCCESS)
        hal_panic();
    return;
}

void hal_flash_lock(void)
{
    if(R_FLASH_HP_AccessWindowSet(&g_flash0_ctrl,
                                        FLASH_START_ADDR, FLASH_END_ADDR)
            != FSP_SUCCESS)
            hal_panic();

   #ifdef WOLFBOOT_DUALBANK
    if(R_FLASH_HP_AccessWindowSet(&g_flash0_ctrl,
                                        FLASH1_START_ADDR, FLASH1_END_ADDR)
            != FSP_SUCCESS)
            hal_panic();
   #endif
    return;
}

#if WOLFBOOT_DUALBANK
void RAMFUNCTION hal_flash_dualbank_swap(void)
{
     flash_cmd_t cmd = FLASH_CMD_SWAPFLAG_TOGGLE;

    hal_flash_unlock();

    if(R_FLASH_HP_Control(cmd, NULL) != FSP_SUCCESS)
        hal_panic();

    hal_flash_lock();

}

void* hal_get_primary_address(void)
{
    return (void*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
}

void* hal_get_update_address(void)
{
    return (void*)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
}
#endif
