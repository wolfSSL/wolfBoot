/* hal.h
 *
 * The HAL API definitions.
 *
 * Copyright (C) 2021 wolfSSL Inc.
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

#ifndef H_HAL_
#define H_HAL_

#ifdef __cplusplus
extern "C" {
#endif

#include "target.h"

/* Architecture specific calls */
#ifdef MMU
extern void do_boot(const uint32_t *app_offset, const uint32_t* dts_offset);
#else
extern void do_boot(const uint32_t *app_offset);
#endif
extern void arch_reboot(void);

/* Simulator-only calls */
#ifdef TARGET_sim
void hal_set_internal_flash_file(const char* file);
void hal_set_external_flash_file(const char* file);
void hal_deinit();
#endif

#if !defined(ARCH_64BIT) && \
    (defined(ARCH_x86_64) || defined(ARCH_AARCH64) || defined(ARCH_SIM))
    #define ARCH_64BIT
#endif

void hal_init(void);
#ifdef ARCH_64BIT
    typedef uintptr_t haladdr_t; /* 64-bit platforms */
    int hal_flash_write(uintptr_t address, const uint8_t *data, int len);
    int hal_flash_erase(uintptr_t address, int len);
#else
    typedef uint32_t haladdr_t; /* original 32-bit */
    int hal_flash_write(uint32_t address, const uint8_t *data, int len);
    int hal_flash_erase(uint32_t address, int len);
#endif
void hal_flash_unlock(void);
void hal_flash_lock(void);
void hal_prepare_boot(void);

#ifdef DUALBANK_SWAP
    void hal_flash_dualbank_swap(void);
#endif

#ifdef WOLFBOOT_DUALBOOT
    void* hal_get_primary_address(void);
    void* hal_get_update_address(void);
#endif

#ifdef MMU
    void *hal_get_dts_address(void);
    void *hal_get_dts_update_address(void);
#endif

#if !defined(SPI_FLASH) && !defined(QSPI_FLASH) && !defined(OCTOSPI_FLASH)
    /* user supplied external flash interfaces */
    int  ext_flash_write(uintptr_t address, const uint8_t *data, int len);
    int  ext_flash_read(uintptr_t address, uint8_t *data, int len);
    int  ext_flash_erase(uintptr_t address, int len);
    void ext_flash_lock(void);
    void ext_flash_unlock(void);
#else
    #include "spi_flash.h"
    #define ext_flash_lock() do{}while(0)
    #define ext_flash_unlock() do{}while(0)
    #define ext_flash_read spi_flash_read
    #define ext_flash_write spi_flash_write
    static inline int ext_flash_erase(uintptr_t address, int len)
    {
        int ret = 0;
        uint32_t end = address + len - 1;
        uint32_t p;
        for (p = address; p <= end; p += SPI_FLASH_SECTOR_SIZE) {
            ret = spi_flash_sector_erase(p);
            if (ret != 0) {
                break;
            }
        }
        return ret;
    }
#endif /* !SPI_FLASH */

#ifdef TZEN

/* TrustZone hal API */

void hal_tz_claim_nonsecure_area(uint32_t address, int len);
void hal_tz_release_nonsecure_area(void);
void hal_tz_sau_init(void);
void hal_tz_sau_ns_region(void);
void hal_gtzc_init(void);

/* Needed by TZ to claim/release nonsecure flash areas */
void hal_flash_wait_complete(uint8_t bank);
void hal_flash_clear_errors(uint8_t bank);

#endif

#ifdef WOLFCRYPT_SECURE_MODE

void hal_trng_init(void);
void hal_trng_fini(void);
int hal_trng_get_entropy(unsigned char *out, unsigned len);

#endif

#ifdef FLASH_OTP_KEYSTORE

int hal_flash_otp_write(uint32_t flashAddress, const void* data, uint16_t length);
int hal_flash_otp_set_readonly(uint32_t flashAddress, uint16_t length);
int hal_flash_otp_read(uint32_t flashAddress, void* data, uint32_t length);

#endif

#ifdef __cplusplus
}
#endif

#endif /* H_HAL_FLASH_ */
