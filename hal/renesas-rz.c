/* renesas-rz.c
 *
 * Stubs for custom HAL implementation. Defines the
 * functions used by wolfboot for a specific target.
 *
 * Copyright (C) 2024 wolfSSL Inc.
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
#include "user_settings.h"
#include "target.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"

#include "hal_data.h"

#if defined(WOLFBOOT_RENESAS_RSIP) && !defined(WOLFBOOT_RENESAS_APP)

    #include "rsa_pub.h"
    #include "wolfssl/wolfcrypt/wc_port.h"
    #include "wolfssl/wolfcrypt/port/Renesas/renesas-fspsm-crypt.h"
    #include "wolfssl/wolfcrypt/port/Renesas/renesas-fspsm-types.h"
    
    FSPSM_ST pkInfo;
    uint8_t  wrapped_public_key[RSIP_BYTE_SIZE_WRAPPED_KEY_VALUE_RSA_2048_PUBLIC];
    rsip_wrapped_key_t *p_wrapped_public_key = (rsip_wrapped_key_t *) wrapped_public_key;

    int wc_CryptoCb_CryptInitRenesasCmn(struct WOLFSSL* ssl, void* ctx);
#endif

#define  BSC_SDRAM_SPACE    (0x30000000)

#ifdef DEBUG_FLASH_WRITE_VERIFY
static uint8_t readbuf[MINIMUM_BLOCK*sizeof(uint32_t)];
#endif


/* #define DEBUG_FLASH_WRITE_VERIFY */

static inline void hal_panic(void)
{
    while(1)
        ;
}

void hal_init(void);
void hal_prepare_boot(void);
int hal_flash_write(uint32_t addr, const uint8_t *data, int len);
int hal_flash_erase(uint32_t address, int int_len);
void hal_flash_unlock(void);
void hal_flash_lock(void);

int ext_flash_read(unsigned long address, uint8_t *data, int len);
int ext_flash_erase(unsigned long address, int len);
int ext_flash_write(unsigned long address, const uint8_t *data, int len);
void ext_flash_lock(void);
void ext_flash_unlock(void);
uint32_t rz_memcopy(uint32_t *src, uint32_t *dst, uint32_t bytesize);

void* hal_get_primary_address(void);
void* hal_get_update_address(void);

uint32_t rz_memcopy(uint32_t *src, uint32_t *dst, uint32_t bytesize)
{
    uint32_t i;
    uint32_t cnt;

    /* copy count in 4 byte unit */
    cnt = (bytesize + 3) >> 2;

    for (i = 0; i < cnt; i++)
    {
        *dst++ = *src++;
    }

    /* ensuring data-changing */
    __DSB();

    return bytesize;
}

#ifdef EXT_FLASH

int ext_flash_read(unsigned long address, uint8_t *data, int len)
{
    return (int)rz_memcopy((void*)address, (uint32_t*)data, (uint32_t)len);
}

int ext_flash_erase(unsigned long address, int len)
{
    (void) address;
    (void) len;
    return 0;
}

int ext_flash_write(unsigned long address, const uint8_t *data, int len)
{
    (void) address;
    (void) data;
    (void) len;
    return 0;
}

void ext_flash_lock(void)
{
}

void ext_flash_unlock(void)
{
}

#endif

void hal_init(void)
{
#if defined(WOLFBOOT_RENESAS_RSIP) && !defined(WOLFBOOT_RENESAS_APP)
	fsp_err_t err;
	int ret;
	rsa_public_t rsip_pub_key;
	const size_t key_size = sizeof(rsip_pub_key);
	
    err = wolfCrypt_Init();
    if (err != 0) {
          printf("ERROR: wolfCrypt_Init %d\n", err);
          hal_panic();
    }

    /* copy the key from ext flash to RAM */
    ret = ext_flash_read(RENESAS_RSIP_INSTALLEDKEY_FLASH_ADDR,
    		(uint8_t*)RENESAS_RSIP_INSTALLEDKEY_RAM_ADDR, key_size);
    if (ret != key_size){
        wolfBoot_printf("Error reading public key at %lx\n",
        RENESAS_RSIP_INSTALLEDKEY_FLASH_ADDR);
         hal_panic();
    }
    /* import enrypted key */
    XMEMCPY(&rsip_pub_key, (const void*)RENESAS_RSIP_INSTALLEDKEY_RAM_ADDR, key_size);
    err = R_RSIP_KeyImportWithUFPK(&rsip_ctrl,
    		rsip_pub_key.wufpk,
			rsip_pub_key.initial_vector,
            RSIP_KEY_TYPE_RSA_2048_PUBLIC_ENHANCED,
			rsip_pub_key.encrypted_user_key,
            p_wrapped_public_key);
    
    XMEMSET(&pkInfo, 0, sizeof(pkInfo));
    pkInfo.wrapped_key_rsapub2048 = 
              (rsip_wrapped_key_t*)p_wrapped_public_key;

    pkInfo.keyflgs_crypt.bits.rsapub2048_installedkey_set = 1;
    pkInfo.keyflgs_crypt.bits.message_type = 1;
    pkInfo.hash_type = RSIP_HASH_TYPE_SHA256;
    err = wc_CryptoCb_CryptInitRenesasCmn(NULL, &pkInfo);

    if (err < 0) {
         wolfBoot_printf("ERROR: wc_CryptoCb_CryptInitRenesasCmn %d\n", err);
         hal_panic();
    }

#endif
}

void hal_prepare_boot(void)
{
}
/* write data to sdram */
int hal_flash_write(uint32_t addr, const uint8_t *data, int len)
{
    (void)addr;
    (void)data;
    (void)len;
    return 0;
}

/* write data to sdram */
int hal_flash_erase(uint32_t address, int int_len)
{
    (void)address;
    (void)int_len;
    return 0;
}

void hal_flash_unlock(void)
{
    return;
}

void hal_flash_lock(void)
{
    return;
}


void* hal_get_primary_address(void)
{
    return (void*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
}

void* hal_get_update_address(void)
{
    return (void*)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
}
