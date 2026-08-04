/* pic32cz.c
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

#include "image.h"
#include <stdint.h>
#include <string.h>

#include <hal/pic32c.h>

#ifdef DEBUG_UART
#include "uart_drv.h"
void uart_deinit(void);
#endif

#if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)

/* wolfHSM */
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_client.h"
#include "wolfhsm/wh_client_crypto.h"
#include "wolfhsm/wh_transport_mem.h"

/* PIC32CZ wolfHSM port (shared with the czhsm-client reference application) */
#include "port/client/czhsm_client.h"
#include "port/client/fwmetadata.h"
/* PIC32CZ_CFG_* (shared-memory window) comes from arch.mk, not a header: unlike
 * the czhsm-client application, wolfBoot does not define WOLFHSM_CFG, so the
 * port sources skip "pic32cz_cfg.h" and take the configuration on the command
 * line instead. */

/* FCW_BASE */
#include "hal/pic32cz_registers.h"

#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/asn.h>

#ifndef WOLFBOOT_WOLFHSM_CLIENT_ID
#error "WOLFBOOT_WOLFHSM_CLIENT_ID is not defined. Set WOLFHSM_CLIENT_ID in your .config."
#endif

#define FCW_MUTEX (*(volatile uint32_t*)(FCW_BASE + 0x08U))
#define FCW_MUTEX_RELEASE (0x2)

#endif /* WOLFBOOT_ENABLE_WOLFHSM_CLIENT */

/* if the SUPC PLL regulator was already on at reset */
static int pic32_vreg_pll_was_enabled = 0;

#define SUPC_BASE (0x44020000U)
#define SUPC_VREGCTRL (*(volatile uint32_t *)(SUPC_BASE + 0x1CU))
#define SUPC_STATUS (*(volatile uint32_t *)(SUPC_BASE + 0x0CU))

#define SUPC_VREGCTRL_AVREGEN_PLLREG_EN (4)
#define SUPC_VREGCTRL_AVREGEN_SHIFT (16)
#define SUPC_STATUS_ADDVREGRDY_PLL (4)
#define SUPC_STATUS_ADDVREGRDY_SHIFT (8)

static void pic32_delay_cnt(uint32_t ticks)
{
    uint32_t i = 0;
    for (i = 0; i < ticks; i++) {
        __asm__ __volatile__("nop");
    }
}

#define PLL_VREG_SETTLE_TICKS (4000)

static void pic32_supc_vreg_pll_enable(void)
{
    pic32_vreg_pll_was_enabled =
        ((SUPC_VREGCTRL >> SUPC_VREGCTRL_AVREGEN_SHIFT)
         & SUPC_VREGCTRL_AVREGEN_PLLREG_EN) != 0;

    if (pic32_vreg_pll_was_enabled) {
        return;
    }

    SUPC_VREGCTRL |= SUPC_VREGCTRL_AVREGEN_PLLREG_EN
        << SUPC_VREGCTRL_AVREGEN_SHIFT;

    /* wait for the vreg to be ready */
    while (!(SUPC_STATUS &
             (SUPC_STATUS_ADDVREGRDY_PLL << SUPC_STATUS_ADDVREGRDY_SHIFT))) {}

    pic32_delay_cnt(PLL_VREG_SETTLE_TICKS);
}

#ifdef DUALBANK_SWAP
void hal_flash_dualbank_swap(void)
{
    pic32_flash_dualbank_swap();
}
#endif /* DUALBANK_SWAP */

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    return pic32_flash_write(address, data, len);
}

void hal_flash_unlock(void)
{
    pic32_fcw_grab();
}

void hal_flash_lock(void)
{
    pic32_fcw_release();
}

int hal_flash_erase(uint32_t addr, int len)
{
    return pic32_flash_erase(addr, len);
}

void hal_init(void)
{
#if defined(TEST_CLOCK)
    pic32_clock_test(48000000);
#endif
    pic32_supc_vreg_pll_enable();
    pic32_clock_pll0_init(12, 225, 1, 3);
    pic32_clock_gclk_gen0(2, 1);
    pic32_delay_cnt(700);
#ifdef DEBUG_UART
    /* Must follow the PLL setup above: the SERCOM baud divisor assumes
     * GCLK1 = PLL0/2 = 150 MHz. */
    uart_init(115200, 8, 'N', 1);
    uart_write("wolfBoot PIC32CZ\r\n", 18);
#endif
#if defined(TEST_FLASH)
    pic32_flash_test();
    while (1) {}
#endif
#if defined(TEST_CLOCK)
    pic32_clock_test(300000000);
    pic32_clock_reset();
    pic32_clock_test(48000000);
    while (1) {};
#endif
}

void hal_prepare_boot(void)
{
#ifdef DEBUG_UART
    uart_deinit();
#endif

#ifdef WOLFBOOT_RESTORE_CLOCK
    pic32_clock_reset();

    if (!pic32_vreg_pll_was_enabled) {
        SUPC_VREGCTRL &= ~((uint32_t)SUPC_VREGCTRL_AVREGEN_PLLREG_EN
                           << SUPC_VREGCTRL_AVREGEN_SHIFT);
    }
#endif
}

#if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)

/* wolfBoot's signing public key, DER encoded, as a 'hsm_pubkey_der[]' C array.
 */
#ifdef HSM_PUBKEY_HEADER
#include HSM_PUBKEY_HEADER
#endif

whClientContext hsmClientCtx = {0};

const int hsmDevIdHash   = WH_DEV_ID;
const int hsmDevIdPubKey = WH_DEV_ID;

/* Key slot the verification key lives at on the server; image.c references it by
 * this id (wh_Client_EccSetKeyId(hsmKeyIdPubKey)). wolfBoot either pushes the key
 * here at boot (HSM_PUBKEY_HEADER) or relies on it being pre-provisioned. */
const int hsmKeyIdPubKey = 0xFF;

/*
 * Boot the HSM core and connect to the wolfHSM server running on it.
 */
int hal_hsm_init_connect(void)
{
    int            rc;
    whClientConfig cfg;

    memset((void*)PIC32CZ_CFG_SHARED_MEM_PHYS_BASE, 0,
           PIC32CZ_CFG_SHARED_MEM_TOTAL_SIZE);

    FCW_MUTEX = FCW_MUTEX_RELEASE;

#if defined(HSM_FW_ADDR) && defined(HSM_FW_SIZE)
    /* Load/boot the HSM core from the firmware image; blocks until it has booted
     * and acknowledged. Skipped when HSM_FW_BIN is unset: the firmware is then
     * assumed already resident and wolfBoot connects to the running server. */
    rc = czhsm_load_hsm_firmware(HSM_FW_ADDR, HSM_FW_SIZE);
    if (rc != 0) {
        return rc;
    }
#endif

    rc = czhsm_setup(&cfg);
    if (rc != 0) {
        return rc;
    }

    rc = wh_Client_Init(&hsmClientCtx, &cfg);
    if (rc != WH_ERROR_OK) {
        return rc;
    }

    rc = wh_Client_CommInit(&hsmClientCtx, NULL, NULL);
    if (rc != WH_ERROR_OK) {
        return rc;
    }

#ifdef HSM_PUBKEY_HEADER
    {
        ecc_key pubkey;
        whKeyId keyId = (whKeyId)hsmKeyIdPubKey;
        word32  idx   = 0;

        rc = wc_ecc_init_ex(&pubkey, NULL, INVALID_DEVID);
        if (rc != 0) {
            return rc;
        }

        rc = wc_EccPublicKeyDecode(hsm_pubkey_der, &idx, &pubkey,
                                   (word32)sizeof(hsm_pubkey_der));
        if (rc == 0) {
            rc = wh_Client_EccImportKey(&hsmClientCtx, &pubkey, &keyId,
                                        WH_NVM_FLAGS_USAGE_VERIFY, 0, NULL);
        }

        wc_ecc_free(&pubkey);
    }
#endif /* HSM_PUBKEY_HEADER */

    return rc;
}

/*
 * Close the wolfHSM session before handing control to the application.
 */
int hal_hsm_disconnect(void)
{
    int rc;

    rc = wh_Client_CommClose(&hsmClientCtx);
    if (rc != WH_ERROR_OK) {
        return rc;
    }

    return wh_Client_Cleanup(&hsmClientCtx);
}

#endif /* WOLFBOOT_ENABLE_WOLFHSM_CLIENT */
