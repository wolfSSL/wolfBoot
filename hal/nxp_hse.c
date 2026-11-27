/* nxp_hse.c
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

/* Host interface to the NXP HSE firmware on the security core of the S32G,
 * S32R and S32K3 families.
 *
 * A request is a service descriptor of at most 256 bytes. The host writes
 * the descriptor's physical address to a transmit register of the Messaging
 * Unit and HSE answers with one response word in the matching receive
 * register. HSE reads descriptors and data buffers by physical address, so
 * a buffer in cached memory is cleaned before the request and invalidated
 * after it, and a store barrier separates the descriptor from the request.
 *
 * The service structures are packed to 4 bytes and hold addresses of 8 or 4
 * bytes depending on the HSE variant (NXP_HSE_ADDR64), which moves the
 * fields behind them. Only the 64-bit layout has run on hardware (S32G2).
 *
 * The HSE firmware is not part of wolfBoot: it is an NXP image. */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "hal.h"

/* hal.h declares this for disk and benchmark builds only. */
uint64_t hal_get_timer_us(void);

#if defined(DEBUG_UART)
    #define PRINTF_ENABLED
#endif
#include "printf.h"

#ifndef NXP_HSE_RD32
#if defined(TARGET_s32g2)
#include "hal/s32g2.h"
#include "aarch64_arch.h"
#define NXP_HSE_CLEAN(p, n) \
    flush_dcache_range((uintptr_t)(p), (uintptr_t)(p) + (n))
#define NXP_HSE_INVAL(p, n) \
    invalidate_dcache_range((uintptr_t)(p), (uintptr_t)(p) + (n))
#define NXP_HSE_BARRIER()       __asm__ volatile("dsb sy")
#else
#error "nxp_hse: no cache maintenance hooks for this target"
#endif
#define NXP_HSE_RD32(a)         (*(volatile uint32_t*)(uintptr_t)(a))
#define NXP_HSE_WR32(a, v)      (*(volatile uint32_t*)(uintptr_t)(a) = (v))
#endif
#include "hal/nxp_hse.h"

/* Messaging Unit */
#define MU_FSR                  0x104   /* [31:16] HSE status, [15:0] busy */
#define MU_TSR                  0x124   /* transmit register empty */
#define MU_RSR                  0x12C   /* receive register full */
#define MU_TR(n)                (0x200 + 4 * (n))
#define MU_RR(n)                (0x280 + 4 * (n))

#define HSE_SRV_GET_ATTR        0x00A50002UL
#define HSE_SRV_SYSIMG_PUBLISH  0x00000011UL
#define HSE_SRV_SYSIMG_SIZE     0x00000012UL
#define HSE_SRV_FORMAT_CATALOGS 0x00000101UL
#define HSE_SRV_IMPORT_KEY      0x00000104UL
#define HSE_SRV_SMR_INSTALL     0x00000501UL
#define HSE_SRV_SMR_VERIFY      0x00000502UL
#define HSE_SRV_CR_INSTALL      0x00000503UL

#define HSE_KEY_HANDLE_NONE     0xFFFFFFFFUL
#define HSE_KEY_FLAG_VERIFY     0x08U
#define HSE_SIGN_ECDSA          0x80U
#define HSE_HASH_SHA2_256       0x04U
#define HSE_SMR_FLAG_INSTALL_AUTH 0x04U
#define HSE_CR_KEEP_IN_RESET    0x7455U
#define HSE_CR_AUTO_START       0x35A5U
#define HSE_PUBLISH_ALL         0x02U

#ifndef NXP_HSE_TIMEOUT_US
#define NXP_HSE_TIMEOUT_US      1000000U
#endif

/* Service descriptor: service id, four reserved bytes, then the service's
 * parameters. Offsets below are inside the parameters. */
#define DESC_SRV_ID             0
#define DESC_PARAMS             8

#ifdef NXP_HSE_ADDR64
#define HSE_ADDR_SIZE           8
#define GET_ATTR_SIZE           16
#define FORMAT_RAM              8
#define FORMAT_SIZE             16
#define IMPORT_KEY0             12
#define IMPORT_KEYLEN0          36
#define IMPORT_CIPHER_KEY       44
#define IMPORT_CONTAINER_KEY    96
#define IMPORT_SIZE             136
#define SMRI_DATA               12
#define SMRI_DATA_LEN           20
#define SMRI_TAG0               24
#define SMRI_TAG1               32
#define SMRI_TAG_LEN0           40
#define SMRI_TAG_LEN1           42
#define SMRI_SIZE               60
#define SMRE_SIZE               12
#define SMRE_FLAGS              16
#define SMRE_KEY                24
#define SMRE_SIGN               28
#define SMRE_HASH               32
#define SMRE_TAG0               44
#define SMRE_TAG1               48
#define CRI_SIZE                12
#define SYSIMG_SIZE_SIZE        8
#define PUBLISH_LEN             12
#define PUBLISH_BUF             20
#define PUBLISH_SIZE            28
#else
#define HSE_ADDR_SIZE           4
#define GET_ATTR_SIZE           12
#define FORMAT_RAM              4
#define FORMAT_SIZE             8
#define IMPORT_KEY0             8
#define IMPORT_KEYLEN0          20
#define IMPORT_CIPHER_KEY       28
#define IMPORT_CONTAINER_KEY    64
#define IMPORT_SIZE             92
#define SMRI_DATA               8
#define SMRI_DATA_LEN           12
#define SMRI_TAG0               16
#define SMRI_TAG1               20
#define SMRI_TAG_LEN0           24
#define SMRI_TAG_LEN1           26
#define SMRI_SIZE               36
#define SMRE_SIZE               8
#define SMRE_FLAGS              12
#define SMRE_KEY                20
#define SMRE_SIGN               24
#define SMRE_HASH               28
#define SMRE_TAG0               36
#define SMRE_TAG1               40
#define CRI_SIZE                8
#define SYSIMG_SIZE_SIZE        4
#define PUBLISH_LEN             8
#define PUBLISH_BUF             12
#define PUBLISH_SIZE            16
#endif
/* the same in both layouts */
#define GET_ATTR_ID             0       /* 16 bits */
#define GET_ATTR_LEN            4
#define GET_ATTR_ADDR           8
#define IMPORT_HANDLE           0
#define IMPORT_KEY_INFO         4
#define SMRI_INDEX              1
#define SMRI_ENTRY              4
#define SMRE_SRC                0
#define SMRE_DEST               4
#define CRI_ENTRY               4
#define PUBLISH_OPTIONS         0
#define PUBLISH_OFFSET          4

static uint8_t hse_attr_buf[NXP_HSE_ATTR_MAX] __attribute__((aligned(64)));

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_addr(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v);
#ifdef NXP_HSE_ADDR64
    put32(p + 4, (uint32_t)(v >> 32));
#endif
}

static uint32_t hse_status(void)
{
    return NXP_HSE_RD32(NXP_HSE_MU_BASE + MU_FSR) >> 16;
}

uint32_t nxp_hse_status(void)
{
    return hse_status();
}

int nxp_hse_wait_init(uint32_t timeout_us)
{
    uint64_t end = hal_get_timer_us() + timeout_us;

    for (;;) {
        if ((hse_status() & NXP_HSE_STATUS_INIT_OK) != 0)
            return 0;
        if (hal_get_timer_us() >= end)
            return -1;
    }
}

int nxp_hse_request(uint32_t channel, const uint8_t *desc, uint32_t len,
    uint32_t *rsp)
{
    uintptr_t mu = NXP_HSE_MU_BASE;
    uintptr_t slot;
    uint32_t bit;
    uint32_t i;
    uint32_t val;
    uint64_t end;

    if (desc == NULL || rsp == NULL || channel >= NXP_HSE_MU_CHANNELS ||
            len > NXP_HSE_DESC_SIZE || (len & 3U) != 0)
        return -1;
    bit = 1U << channel;
    *rsp = 0;
    if ((hse_status() & NXP_HSE_STATUS_INIT_OK) == 0)
        return -2;
    /* The channel must be idle: not busy, transmit register empty and no
     * response left unread. */
    if ((NXP_HSE_RD32(mu + MU_FSR) & bit) != 0 ||
            (NXP_HSE_RD32(mu + MU_TSR) & bit) == 0 ||
            (NXP_HSE_RD32(mu + MU_RSR) & bit) != 0)
        return -3;

    slot = NXP_HSE_DESC_BASE + channel * NXP_HSE_DESC_SIZE;
    for (i = 0; i < NXP_HSE_DESC_SIZE; i += 4) {
        val = 0;
        if (i < len)
            val = (uint32_t)desc[i] | ((uint32_t)desc[i + 1] << 8) |
                ((uint32_t)desc[i + 2] << 16) | ((uint32_t)desc[i + 3] << 24);
        NXP_HSE_WR32(slot + i, val);
    }
    /* The descriptor memory and the MU are different devices: without a
     * barrier the request could be signalled before the descriptor lands. */
    NXP_HSE_BARRIER();
    NXP_HSE_WR32(mu + MU_TR(channel), (uint32_t)slot);

    end = hal_get_timer_us() + NXP_HSE_TIMEOUT_US;
    while ((NXP_HSE_RD32(mu + MU_RSR) & bit) == 0) {
        if (hal_get_timer_us() >= end)
            return -4;
    }
    *rsp = NXP_HSE_RD32(mu + MU_RR(channel));
    return (*rsp == NXP_HSE_RSP_OK) ? 0 : -5;
}

static int hse_call(uint8_t *desc, uint32_t srv, uint32_t len, uint32_t *rsp)
{
    put32(desc + DESC_SRV_ID, srv);
    return nxp_hse_request(NXP_HSE_CHANNEL_ADMIN, desc, DESC_PARAMS + len,
        rsp);
}

int nxp_hse_get_attr(uint16_t id, uint8_t *out, uint32_t len, uint32_t *rsp)
{
    uint8_t desc[DESC_PARAMS + GET_ATTR_SIZE];
    int ret;

    if (out == NULL || len == 0 || len > sizeof(hse_attr_buf))
        return -1;
    memset(desc, 0, sizeof(desc));
    put32(desc + DESC_SRV_ID, HSE_SRV_GET_ATTR);
    put16(desc + DESC_PARAMS + GET_ATTR_ID, id);
    put32(desc + DESC_PARAMS + GET_ATTR_LEN, len);
    put_addr(desc + DESC_PARAMS + GET_ATTR_ADDR,
        (uint64_t)(uintptr_t)hse_attr_buf);

    memset(hse_attr_buf, 0, sizeof(hse_attr_buf));
    NXP_HSE_CLEAN(hse_attr_buf, sizeof(hse_attr_buf));
    ret = nxp_hse_request(NXP_HSE_CHANNEL_ATTR, desc, sizeof(desc), rsp);
    NXP_HSE_INVAL(hse_attr_buf, sizeof(hse_attr_buf));
    if (ret != 0)
        return ret;
    memcpy(out, hse_attr_buf, len);
    return 0;
}

/* nvm and ram: arrays of 8-byte group entries {MU mask, owner, key type,
 * slots, max bits (16)}, closed by an all-zero entry. */
int nxp_hse_format_catalogs(uint64_t nvm, uint64_t ram, uint32_t *rsp)
{
    uint8_t desc[DESC_PARAMS + FORMAT_SIZE];

    memset(desc, 0, sizeof(desc));
    put_addr(desc + DESC_PARAMS, nvm);
    put_addr(desc + DESC_PARAMS + FORMAT_RAM, ram);
    return hse_call(desc, HSE_SRV_FORMAT_CATALOGS, FORMAT_SIZE, rsp);
}

/* info: a NXP_HSE_KEY_INFO_SIZE block written here; pub: X then Y. */
int nxp_hse_import_ecc_pub(uint32_t handle, uint8_t curve, uint16_t bits,
    uint64_t info, uint64_t pub, uint32_t *rsp)
{
    uint8_t desc[DESC_PARAMS + IMPORT_SIZE];
    uint8_t *ki = (uint8_t*)(uintptr_t)info;

    memset(ki, 0, NXP_HSE_KEY_INFO_SIZE);
    ki[0] = HSE_KEY_FLAG_VERIFY;
    put16(ki + 2, bits);
    ki[12] = NXP_HSE_KEY_TYPE_ECC_PUB;
    ki[13] = curve;
    NXP_HSE_CLEAN(ki, NXP_HSE_KEY_INFO_SIZE);

    memset(desc, 0, sizeof(desc));
    put32(desc + DESC_PARAMS + IMPORT_HANDLE, handle);
    put_addr(desc + DESC_PARAMS + IMPORT_KEY_INFO, info);
    put_addr(desc + DESC_PARAMS + IMPORT_KEY0, pub);
    put16(desc + DESC_PARAMS + IMPORT_KEYLEN0, (uint16_t)(bits / 4));
    /* a plain key: no wrapping key and no key container */
    put32(desc + DESC_PARAMS + IMPORT_CIPHER_KEY, HSE_KEY_HANDLE_NONE);
    put32(desc + DESC_PARAMS + IMPORT_CONTAINER_KEY, HSE_KEY_HANDLE_NONE);
    return hse_call(desc, HSE_SRV_IMPORT_KEY, IMPORT_SIZE, rsp);
}

/* ECDSA with SHA-256 over size bytes at src on the boot medium, copied to
 * dest once verified; the signature halves R and S sit at tag_r and tag_s
 * on the medium. entry is a NXP_HSE_SMR_ENTRY_SIZE block written here.
 * data, r and s are copies in memory that HSE checks at installation. */
int nxp_hse_smr_install(uint8_t index, uint64_t entry, uint32_t src,
    uint64_t dest, uint32_t size, uint8_t medium, uint32_t key,
    uint32_t tag_r, uint32_t tag_s, uint64_t data, uint64_t r, uint64_t s,
    uint16_t half, uint32_t *rsp)
{
    uint8_t desc[DESC_PARAMS + SMRI_SIZE];
    uint8_t *e = (uint8_t*)(uintptr_t)entry;

    memset(e, 0, NXP_HSE_SMR_ENTRY_SIZE);
    put32(e + SMRE_SRC, src);
    put_addr(e + SMRE_DEST, dest);
    put32(e + SMRE_SIZE, size);
    e[SMRE_FLAGS] = (uint8_t)(medium | HSE_SMR_FLAG_INSTALL_AUTH);
    put32(e + SMRE_KEY, key);
    e[SMRE_SIGN] = HSE_SIGN_ECDSA;
    e[SMRE_HASH] = HSE_HASH_SHA2_256;
    put32(e + SMRE_TAG0, tag_r);
    put32(e + SMRE_TAG1, tag_s);
    NXP_HSE_CLEAN(e, NXP_HSE_SMR_ENTRY_SIZE);

    memset(desc, 0, sizeof(desc));
    desc[DESC_PARAMS + SMRI_INDEX] = index;
    put_addr(desc + DESC_PARAMS + SMRI_ENTRY, entry);
    put_addr(desc + DESC_PARAMS + SMRI_DATA, data);
    put32(desc + DESC_PARAMS + SMRI_DATA_LEN, size);
    put_addr(desc + DESC_PARAMS + SMRI_TAG0, r);
    put_addr(desc + DESC_PARAMS + SMRI_TAG1, s);
    put16(desc + DESC_PARAMS + SMRI_TAG_LEN0, half);
    put16(desc + DESC_PARAMS + SMRI_TAG_LEN1, half);
    return hse_call(desc, HSE_SRV_SMR_INSTALL, SMRI_SIZE, rsp);
}

/* Release core (HSE numbering) at reset_addr when the SMRs in smr_map
 * verify; keep it in reset otherwise. entry is a NXP_HSE_CR_ENTRY_SIZE
 * block written here. */
int nxp_hse_cr_install(uint8_t index, uint64_t entry, uint8_t core,
    uint32_t smr_map, uint32_t reset_addr, uint32_t *rsp)
{
    uint8_t desc[DESC_PARAMS + CRI_SIZE];
    uint8_t *e = (uint8_t*)(uintptr_t)entry;

    memset(e, 0, NXP_HSE_CR_ENTRY_SIZE);
    e[0] = core;
    put16(e + 2, HSE_CR_KEEP_IN_RESET);
    put32(e + 4, smr_map);
    put32(e + 8, reset_addr);
    put16(e + 24, HSE_CR_AUTO_START);
    NXP_HSE_CLEAN(e, NXP_HSE_CR_ENTRY_SIZE);

    memset(desc, 0, sizeof(desc));
    desc[DESC_PARAMS] = index;
    put_addr(desc + DESC_PARAMS + CRI_ENTRY, entry);
    return hse_call(desc, HSE_SRV_CR_INSTALL, CRI_SIZE, rsp);
}

/* HSE loads the SMR from the medium and verifies it. */
int nxp_hse_smr_verify(uint8_t index, uint32_t *rsp)
{
    uint8_t desc[DESC_PARAMS + 4];

    memset(desc, 0, sizeof(desc));
    desc[DESC_PARAMS] = index;
    return hse_call(desc, HSE_SRV_SMR_VERIFY, 4, rsp);
}

/* size: address of a 32-bit result. For HSE variants without their own
 * flash, which hand the key store and tables to the host to store. */
int nxp_hse_sysimg_size(uint64_t size, uint32_t *rsp)
{
    uint8_t desc[DESC_PARAMS + SYSIMG_SIZE_SIZE];

    memset(desc, 0, sizeof(desc));
    put_addr(desc + DESC_PARAMS, size);
    return hse_call(desc, HSE_SRV_SYSIMG_SIZE, SYSIMG_SIZE_SIZE, rsp);
}

/* HSE writes its system image (keys, SMR and core reset tables) to buf.
 * offset and len are addresses of 32-bit values; len holds the buffer size
 * on entry and the image size on return. The caller stores the image. */
int nxp_hse_sysimg_publish(uint64_t offset, uint64_t len, uint64_t buf,
    uint32_t *rsp)
{
    uint8_t desc[DESC_PARAMS + PUBLISH_SIZE];

    memset(desc, 0, sizeof(desc));
    desc[DESC_PARAMS + PUBLISH_OPTIONS] = HSE_PUBLISH_ALL;
    put_addr(desc + DESC_PARAMS + PUBLISH_OFFSET, offset);
    put_addr(desc + DESC_PARAMS + PUBLISH_LEN, len);
    put_addr(desc + DESC_PARAMS + PUBLISH_BUF, buf);
    return hse_call(desc, HSE_SRV_SYSIMG_PUBLISH, PUBLISH_SIZE, rsp);
}

void nxp_hse_report(void)
{
    struct nxp_hse_fw_version ver;
    uint8_t lc = 0;
    uint32_t rsp = 0;
    uint32_t left = 0;
    uint32_t boot[5] = { 0, 0, 0, 0, 0 };
    uint64_t t0 = hal_get_timer_us();
    int ret;

    if (nxp_hse_wait_init(NXP_HSE_TIMEOUT_US) != 0) {
        wolfBoot_printf("HSE: firmware not running (status 0x%x)\n",
            (unsigned int)hse_status());
        return;
    }
    wolfBoot_printf("HSE: status 0x%x after %u ms\n",
        (unsigned int)hse_status(),
        (unsigned int)((hal_get_timer_us() - t0) / 1000));
    ret = nxp_hse_get_attr(NXP_HSE_ATTR_FW_VERSION, (uint8_t*)&ver,
        sizeof(ver), &rsp);
    if (ret != 0) {
        wolfBoot_printf("HSE: version read failed (%d, 0x%x)\n", ret,
            (unsigned int)rsp);
        return;
    }
    wolfBoot_printf("HSE: firmware %d.%d.%d, type %d, SoC %d\n",
        (int)ver.major, (int)ver.minor, (int)ver.patch, (int)ver.fw_type,
        (int)ver.soc_type);
    ret = nxp_hse_get_attr(NXP_HSE_ATTR_LIFECYCLE, &lc, sizeof(lc), &rsp);
    if (ret != 0)
        wolfBoot_printf("HSE: lifecycle read failed (%d, 0x%x)\n", ret,
            (unsigned int)rsp);
    else
        wolfBoot_printf("HSE: lifecycle 0x%x\n", (unsigned int)lc);
    ret = nxp_hse_get_attr(NXP_HSE_ATTR_ROLLBACK_LEFT, (uint8_t*)&left,
        sizeof(left), &rsp);
    if (ret != 0)
        wolfBoot_printf("HSE: rollback counter read failed (%d, 0x%x)\n",
            ret, (unsigned int)rsp);
    else
        wolfBoot_printf("HSE: rollback counter updates left %u\n",
            (unsigned int)left);
    if ((hse_status() & NXP_HSE_STATUS_INSTALL_OK) != 0) {
        /* verified, passed, cores booted, primary cores, installed */
        ret = nxp_hse_get_attr(NXP_HSE_ATTR_SMR_CORE_BOOT, (uint8_t*)boot,
            sizeof(boot), &rsp);
        wolfBoot_printf("HSE: system image installed (%s), SMR verified "
            "0x%x passed 0x%x, cores booted 0x%x, SMR installed 0x%x (%d)\n",
            (hse_status() & NXP_HSE_STATUS_SYSIMG_PRIMARY) != 0 ?
                "primary" : "backup",
            (unsigned int)boot[0], (unsigned int)boot[1],
            (unsigned int)boot[2], (unsigned int)boot[4], ret);
    }
}
