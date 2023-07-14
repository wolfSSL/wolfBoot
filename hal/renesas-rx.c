/* remesas-rx.c
 *
 * Stubs for custom HAL implementation. Defines the 
 * functions used by wolfboot for a specific target.
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <target.h>
#include "image.h"
#include "hal.h"
#include "r_flash_rx.h"

#if defined(WOLFBOOT_RENESAS_TSIP)  && \
    !defined(WOLFBOOT_RENESAS_APP)
#    include "wolfssl/wolfcrypt/wc_port.h"
#    include "wolfssl/wolfcrypt/port/Renesas/renesas-tsip-crypt.h"
#    include "wolfssl/wolfcrypt/port/Renesas/renesas_sync.h"
#    include "key_data.h"
#    include "wolfssl/wolfcrypt/port/Renesas/renesas_tsip_types.h"
TsipUserCtx pkInfo;
#endif

static inline void hal_panic(void)
{
    while(1)
        ;
}

void hal_init(void)
{
#if defined(WOLFBOOT_RENESAS_TSIP) &&\
    !defined(WOLFBOOT_RENESAS_APP)
    int err;
    uint32_t key_type = 0;
    int tsip_key_type = -1;
    /* retrive installed pubkey data from flash */
    struct rsa2048_pub *encrypted_user_key_data =
                    (struct rsa2048_pub*)keystore_get_buffer(0);
#endif

    if(R_FLASH_Open() != FLASH_SUCCESS)
        hal_panic();

#if defined(WOLFBOOT_RENESAS_TSIP) && \
    !defined(WOLFBOOT_RENESAS_APP)
    err = wolfCrypt_Init();
    if (err != 0) {
       printf("ERROR: wolfCrypt_Init %d\n", err);
       hal_panic();
    }

    key_type = keystore_get_key_type(0);
    switch(key_type){

        case AUTH_KEY_RSA2048:
            tsip_key_type = TSIP_RSA2048;
            break;
        case AUTH_KEY_RSA4096:
            tsip_key_type = TSIP_RSA4096;
            break;
        case AUTH_KEY_ED448:
        case AUTH_KEY_ECC384:
        case AUTH_KEY_ECC521:
        case AUTH_KEY_ED25519:
        case AUTH_KEY_ECC256:
        case AUTH_KEY_RSA3072:
        default:
            tsip_key_type = -1;
            break;
    }

    if (tsip_key_type == -1) {
        printf("key type (%d) not supported\n", key_type);
        hal_panic();
    }
    /* inform user key */
    tsip_inform_user_keys_ex((byte*)&encrypted_user_key_data->wufpk,
                            (byte*)&encrypted_user_key_data->initial_vector,
                            (byte*)&encrypted_user_key_data->encrypted_user_key,
                            0/* dummy */);
    /* TSIP specific RSA public key */
    if (tsip_use_PublicKey_buffer(&pkInfo,
                (const char*)&encrypted_user_key_data->encrypted_user_key,
                 RSA2048_PUB_SIZE,
                 tsip_key_type) != 0) {
            printf("ERROR tsip_use_PublicKey_buffer\n");
            hal_panic();
    }
    /* Init Crypt Callback */
    pkInfo.sing_hash_type = sha256_mac;
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

#define MIN_PROG (0x8000)
#define ALIGN_FLASH(a) ((a) / MIN_PROG * MIN_PROG)
static uint8_t save[MIN_PROG];

int blockWrite(const uint8_t *data, uint32_t addr, int len)
{
    for(; len; len-=MIN_PROG, data+=MIN_PROG, addr+=MIN_PROG) {
        memcpy(save, data, MIN_PROG); /* for the case "data" ls a flash address */
        if(R_FLASH_Write((uint32_t)save, addr, MIN_PROG) != FLASH_SUCCESS)
            return -1;
    }
    return 0;
}

#define IS_FLASH(addr) (addr) >= 0xffc00000 ? 1 : 0

int hal_flash_write(uint32_t addr, const uint8_t *data, int len)
{
    uint32_t save_len = 0;

    if(addr != ALIGN_FLASH(addr)) {
        save_len = (addr - ALIGN_FLASH(addr)) < len ? (addr - ALIGN_FLASH(addr)) : len;
        memcpy(save, (const void *)ALIGN_FLASH(addr), MIN_PROG);
        memcpy(save + (addr - ALIGN_FLASH(addr)), data, save_len);
        addr   = ALIGN_FLASH(addr);
        if(R_FLASH_Erase((flash_block_address_t)addr, 1) != FLASH_SUCCESS)
            return -1;
        if(R_FLASH_Write((uint32_t)save, addr, MIN_PROG) != FLASH_SUCCESS)
            return -1;
        len -= save_len;
        data += save_len;
        addr += MIN_PROG;
    }

    if(len > 0) {
        if(blockWrite(data, addr, ALIGN_FLASH(len)) < 0)
            goto error;
        addr += ALIGN_FLASH(len);
        data += ALIGN_FLASH(len);
        len  -= ALIGN_FLASH(len);
    }

    if(len > 0) {
        memcpy(save, (const void *)addr, MIN_PROG);
        memcpy(save, data, len);
        if(R_FLASH_Erase((flash_block_address_t)addr, 1) != FLASH_SUCCESS)
        return -1;
        if(R_FLASH_Write((uint32_t)save, addr, MIN_PROG) != FLASH_SUCCESS)
            goto error;
    }
    return 0;
error:
    return -1;
}


int hal_flash_erase(uint32_t address, int len)
{
    int block_size = address >= 0xffff0000 ? 
         FLASH_CF_SMALL_BLOCK_SIZE :  FLASH_CF_MEDIUM_BLOCK_SIZE;

    if(len % block_size != 0)
        return -1;
    for( ; len; address+=block_size, len-=block_size) {
        if(R_FLASH_Erase((flash_block_address_t)address, 1)
                != FLASH_SUCCESS)
            return -1;
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flash_access_window_config_t info;

    info.start_addr = (uint32_t) FLASH_CF_BLOCK_132; 
    info.end_addr   = (uint32_t) FLASH_CF_BLOCK_0; 
    R_BSP_InterruptsDisable(); 
    if(R_FLASH_Control(FLASH_CMD_ACCESSWINDOW_SET, (void *)&info)
        != FLASH_SUCCESS)
        hal_panic();
    R_BSP_InterruptsEnable();
    return;
}

void RAMFUNCTION hal_flash_lock(void)
{
    flash_access_window_config_t info;
    info.start_addr = (uint32_t) FLASH_CF_BLOCK_END; 
    info.end_addr   = (uint32_t) FLASH_CF_BLOCK_END;
    R_BSP_InterruptsDisable(); 
    if(R_FLASH_Control(FLASH_CMD_ACCESSWINDOW_SET, (void *)&info)
        != FLASH_SUCCESS)
        hal_panic();
    R_BSP_InterruptsEnable();
    return;
}


void RAMFUNCTION hal_flash_dualbank_swap(void)
{
    flash_cmd_t cmd = FLASH_CMD_SWAPFLAG_TOGGLE;
    printf("FLASH_CMD_SWAPFLAG_TOGGLE=%d\n", FLASH_CMD_SWAPFLAG_TOGGLE);
    hal_flash_unlock();
    if(R_FLASH_Control(cmd, NULL) != FLASH_SUCCESS)
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
