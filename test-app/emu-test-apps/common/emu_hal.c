/* emu_hal.c
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

#include <stdint.h>
#include <string.h>
#include "target.h"
#include "hal.h"

#ifndef EMU_FLASH_SECTOR_SIZE
#define EMU_FLASH_SECTOR_SIZE WOLFBOOT_SECTOR_SIZE
#endif

#if defined(EMU_STM32)
#define FLASH_BASE        0x40022000u
#define FLASH_NSKEYR      (*(volatile uint32_t *)(FLASH_BASE + 0x004u))
#define FLASH_NSCR        (*(volatile uint32_t *)(FLASH_BASE + 0x028u))
#define FLASH_KEY1        0x45670123u
#define FLASH_KEY2        0xCDEF89ABu
#define FLASH_CR_LOCK     (1u << 0)
#define FLASH_CR_PG       (1u << 1)
#define FLASH_CR_SER      (1u << 2)
#define FLASH_CR_STRT     (1u << 5)
#define FLASH_CR_SNB_SHIFT 6
#define FLASH_CR_SNB_MASK (0x7fu << FLASH_CR_SNB_SHIFT)
#endif

#if defined(EMU_NRF5340)
#define NVMC_BASE         0x40039000u
#define NVMC_CONFIG       (*(volatile uint32_t *)(NVMC_BASE + 0x504u))
#define NVMC_CONFIGNS     (*(volatile uint32_t *)(NVMC_BASE + 0x584u))
#endif

#if defined(EMU_NRF54L)
/* RRAM is written in place through the RRAMC: CONFIG.WEN gates the stores and
 * 0xFFFFFFFF words are the erase. Only reached with TZEN=0; the non-secure app
 * goes through the wolfBoot_nsc_* veneers, as the RRAMC stays secure. */
#ifdef NRF54LM20
#define RRAMC_BASE        0x5004E000u
#else
#define RRAMC_BASE        0x5004B000u
#endif
#define RRAMC_TASKS_COMMITWRITEBUF (*(volatile uint32_t *)(RRAMC_BASE + 0x008u))
#define RRAMC_READY       (*(volatile uint32_t *)(RRAMC_BASE + 0x400u))
#define RRAMC_READYNEXT   (*(volatile uint32_t *)(RRAMC_BASE + 0x404u))
#define RRAMC_BUFSTATUS_WRITEBUFEMPTY (*(volatile uint32_t *)(RRAMC_BASE + 0x418u))
#define RRAMC_CONFIG      (*(volatile uint32_t *)(RRAMC_BASE + 0x500u))
#define RRAMC_CONFIG_WEN  (1u << 0)
#define RRAMC_READY_BIT   (1u << 0)
#define RRAMC_BUFEMPTY_BIT (1u << 0)

static void nrf54l_write_enable(int enable)
{
    if (enable != 0) {
        RRAMC_CONFIG |= RRAMC_CONFIG_WEN;
    }
    else {
        RRAMC_CONFIG &= ~RRAMC_CONFIG_WEN;
    }
    while ((RRAMC_READY & RRAMC_READY_BIT) == 0u) {
    }
}

static void nrf54l_wait_ready_next(void)
{
    while ((RRAMC_READYNEXT & RRAMC_READY_BIT) == 0u) {
    }
}

static void nrf54l_commit(void)
{
    if ((RRAMC_BUFSTATUS_WRITEBUFEMPTY & RRAMC_BUFEMPTY_BIT) == 0u) {
        RRAMC_TASKS_COMMITWRITEBUF = 1u;
        while ((RRAMC_READY & RRAMC_READY_BIT) == 0u) {
        }
        while ((RRAMC_BUFSTATUS_WRITEBUFEMPTY & RRAMC_BUFEMPTY_BIT) == 0u) {
        }
    }
}

/* Word stores where aligned, read-modify-write of the word otherwise */
static void nrf54l_program(uint32_t address, const uint8_t *data, uint32_t len)
{
    uint32_t i = 0u;
    uint32_t off;
    uint32_t word;
    volatile uint32_t *dst;

    while (i < len) {
        nrf54l_wait_ready_next();
        dst = (volatile uint32_t *)((address + i) & ~0x3u);
        off = (address + i) & 0x3u;
        word = *dst;
        if ((off == 0u) && ((len - i) >= 4u)) {
            memcpy(&word, data + i, sizeof(word));
            i += 4u;
        }
        else {
            ((uint8_t *)&word)[off] = data[i];
            i++;
        }
        *dst = word;
    }
}

static int nrf54l_flash_write(uint32_t address, const uint8_t *data, int len)
{
    nrf54l_write_enable(1);
    nrf54l_program(address, data, (uint32_t)len);
    nrf54l_commit();
    nrf54l_write_enable(0);
    return 0;
}

static const uint32_t nrf54l_blank[16] = {
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
};

/* Erasing is programming 0xFF over exactly [address, address + len) */
static int nrf54l_flash_erase(uint32_t address, int len)
{
    uint32_t done = 0u;
    uint32_t chunk;

    nrf54l_write_enable(1);
    while (done < (uint32_t)len) {
        chunk = (uint32_t)len - done;
        if (chunk > sizeof(nrf54l_blank)) {
            chunk = sizeof(nrf54l_blank);
        }
        nrf54l_program(address + done, (const uint8_t *)nrf54l_blank, chunk);
        done += chunk;
    }
    nrf54l_commit();
    nrf54l_write_enable(0);
    return 0;
}
#endif

#if defined(EMU_M2354)
/* M2354 flash is programmed through the FMC ISP engine, never by storing to
 * the mapped address. Only reached with TZEN=0: the non-secure app goes
 * through the wolfBoot_nsc_* veneers instead, as FMC stays secure. */
#define FMC_BASE          0x4000C000u
#define FMC_ISPCTL        (*(volatile uint32_t *)(FMC_BASE + 0x00u))
#define FMC_ISPADDR       (*(volatile uint32_t *)(FMC_BASE + 0x04u))
#define FMC_ISPDAT        (*(volatile uint32_t *)(FMC_BASE + 0x08u))
#define FMC_ISPCMD        (*(volatile uint32_t *)(FMC_BASE + 0x0Cu))
#define FMC_ISPTRG        (*(volatile uint32_t *)(FMC_BASE + 0x10u))
#define FMC_ISPCTL_ISPEN  (1u << 0)
#define FMC_ISPCTL_APUEN  (1u << 3)
#define FMC_ISPCTL_ISPFF  (1u << 6)
#define FMC_ISPTRG_ISPGO  (1u << 0)
#define FMC_ISPCMD_PROGRAM     0x21u
#define FMC_ISPCMD_PAGE_ERASE  0x22u
#define M2354_NS_OFFSET   0x10000000u

#define SYS_BASE          0x40000000u
#define SYS_REGLCTL       (*(volatile uint32_t *)(SYS_BASE + 0x100u))

static int m2354_isp(uint32_t cmd, uint32_t addr, uint32_t data)
{
    /* The ISP engine addresses physical flash; drop the alias bit. */
    FMC_ISPCMD = cmd;
    FMC_ISPADDR = addr & ~M2354_NS_OFFSET;
    FMC_ISPDAT = data;
    FMC_ISPTRG = FMC_ISPTRG_ISPGO;
    while ((FMC_ISPTRG & FMC_ISPTRG_ISPGO) != 0u) {
    }
    if ((FMC_ISPCTL & FMC_ISPCTL_ISPFF) != 0u) {
        FMC_ISPCTL |= FMC_ISPCTL_ISPFF;   /* write-1-to-clear */
        return -1;
    }
    return 0;
}
#endif

void hal_init(void)
{
}

void hal_prepare_boot(void)
{
}

#if defined(EMU_IMXRT700)
/* XSPI0 target-group 0 IP commands, secure alias. The NOR is read-only through
 * the XIP window, so program and erase run as IP commands on LUT sequences the
 * app installs itself. The RT700 test app runs in the secure world in both
 * configurations, so it drives XSPI0 directly. */
#define XSPI0_BASE          0x50184000u
#define XSPI0_TBDR          (*(volatile uint32_t *)(XSPI0_BASE + 0x154u))
#define XSPI0_LUT(n)        (*(volatile uint32_t *)(XSPI0_BASE + 0x310u + 4u * (n)))
#define XSPI0_SFP_TG_IPCR   (*(volatile uint32_t *)(XSPI0_BASE + 0x958u))
#define XSPI0_SFP_TG_SFAR   (*(volatile uint32_t *)(XSPI0_BASE + 0x95Cu))
/* IP commands take the NOR address in the non-secure numbering the wolfBoot
 * HAL uses (and its flash protection windows match), whichever alias the
 * partitions are configured at. */
#define XSPI0_NOR_BASE_NS   0x28000000u
#define XSPI0_NOR_WINDOW    0x07FFFFFFu
#define XSPI_LUT_WORDS      5u
#define XSPI_LUT_CMD_SDR    0x01u
#define XSPI_LUT_RADDR_SDR  0x02u
#define XSPI_LUT_WRITE_SDR  0x08u
#define XSPI_LUT(instr, operand) ((((uint32_t)(instr)) << 10) | (uint32_t)(operand))
#define XSPI_SEQ_WREN       10u
#define XSPI_SEQ_ERASE      11u
#define XSPI_SEQ_PROGRAM    12u
#define NOR_OP_WREN         0x06u
#define NOR_OP_SE_4K        0x21u
#define NOR_OP_PP           0x12u
#define NOR_PAGE_SIZE       256u
#define NOR_SECTOR_SIZE     4096u

static void imxrt700_lut_install(void)
{
    uint32_t i;

    for (i = 0u; i < 3u * XSPI_LUT_WORDS; i++) {
        XSPI0_LUT(XSPI_SEQ_WREN * XSPI_LUT_WORDS + i) = 0u;
    }
    XSPI0_LUT(XSPI_SEQ_WREN * XSPI_LUT_WORDS) =
        XSPI_LUT(XSPI_LUT_CMD_SDR, NOR_OP_WREN);
    XSPI0_LUT(XSPI_SEQ_ERASE * XSPI_LUT_WORDS) =
        XSPI_LUT(XSPI_LUT_CMD_SDR, NOR_OP_SE_4K) |
        (XSPI_LUT(XSPI_LUT_RADDR_SDR, 32u) << 16);
    XSPI0_LUT(XSPI_SEQ_PROGRAM * XSPI_LUT_WORDS) =
        XSPI_LUT(XSPI_LUT_CMD_SDR, NOR_OP_PP) |
        (XSPI_LUT(XSPI_LUT_RADDR_SDR, 32u) << 16);
    XSPI0_LUT(XSPI_SEQ_PROGRAM * XSPI_LUT_WORDS + 1u) =
        XSPI_LUT(XSPI_LUT_WRITE_SDR, 4u);
}

static void imxrt700_ip_command(uint32_t seq, uint32_t address, uint32_t size)
{
    XSPI0_SFP_TG_SFAR = XSPI0_NOR_BASE_NS + (address & XSPI0_NOR_WINDOW);
    XSPI0_SFP_TG_IPCR = (seq << 24) | (size & 0xFFFFu);
}

/* One page-program: the NOR wraps within a 256-byte page, so the caller keeps
 * each chunk inside one page. */
static void imxrt700_program_page(uint32_t address, const uint8_t *data,
    uint32_t len)
{
    uint32_t i;
    uint32_t word;

    imxrt700_ip_command(XSPI_SEQ_WREN, address, 0u);
    imxrt700_ip_command(XSPI_SEQ_PROGRAM, address, len);
    for (i = 0u; i < len; i += 4u) {
        word = 0xFFFFFFFFu;
        memcpy(&word, data + i, ((len - i) < 4u) ? (len - i) : 4u);
        XSPI0_TBDR = word;
    }
}

static int imxrt700_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t done = 0u;
    uint32_t chunk;

    imxrt700_lut_install();
    while (done < (uint32_t)len) {
        chunk = NOR_PAGE_SIZE - ((address + done) & (NOR_PAGE_SIZE - 1u));
        if (chunk > ((uint32_t)len - done)) {
            chunk = (uint32_t)len - done;
        }
        imxrt700_program_page(address + done, data + done, chunk);
        done += chunk;
    }
    return 0;
}

static int imxrt700_flash_erase(uint32_t address, int len)
{
    uint32_t p;
    uint32_t end;

    imxrt700_lut_install();
    end = address + (uint32_t)len;
    for (p = address & ~(NOR_SECTOR_SIZE - 1u); p < end; p += NOR_SECTOR_SIZE) {
        imxrt700_ip_command(XSPI_SEQ_WREN, p, 0u);
        imxrt700_ip_command(XSPI_SEQ_ERASE, p, 0u);
    }
    return 0;
}
#endif

#if defined(EMU_MCXN947)
#include "fsl_flash.h"

/* The flash is programmed through the ROM flash API, which takes the flash
 * offset; the partitions sit at the Secure alias. */
#define MCXN_FLASH_S_ALIAS  0x10000000u
#define MCXN_FLASH_SIZE     0x00200000u
#define MCXN_FLASH_PHRASE   FSL_FEATURE_FLASH_PFLASH_PHRASE_SIZE

static flash_config_t mcxn_flash;
static int mcxn_flash_ready;

static uint32_t mcxn_flash_offset(uint32_t address)
{
    if ((address >= MCXN_FLASH_S_ALIAS) &&
        (address < (MCXN_FLASH_S_ALIAS + MCXN_FLASH_SIZE))) {
        address -= MCXN_FLASH_S_ALIAS;
    }
    return address;
}

static int mcxn_flash_init(void)
{
    if (mcxn_flash_ready == 0) {
        memset(&mcxn_flash, 0, sizeof(mcxn_flash));
        if (FLASH_Init(&mcxn_flash) != kStatus_FLASH_Success) {
            return -1;
        }
        mcxn_flash_ready = 1;
    }
    return 0;
}

static int mcxn_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint8_t phrase[MCXN_FLASH_PHRASE];
    uint32_t aligned;
    uint32_t off;
    uint32_t copy;
    int done = 0;

    if (mcxn_flash_init() != 0) {
        return -1;
    }
    address = mcxn_flash_offset(address);
    while (done < len) {
        off = address & (MCXN_FLASH_PHRASE - 1u);
        copy = (uint32_t)(len - done);
        if ((off == 0u) && (copy >= MCXN_FLASH_PHRASE)) {
            copy &= ~(MCXN_FLASH_PHRASE - 1u);
            if (FLASH_Program(&mcxn_flash, address, (uint8_t *)data + done,
                    copy) != kStatus_FLASH_Success) {
                return -1;
            }
        }
        else {
            aligned = address - off;
            if (copy > (MCXN_FLASH_PHRASE - off)) {
                copy = MCXN_FLASH_PHRASE - off;
            }
            memcpy(phrase, (const void *)aligned, sizeof(phrase));
            memcpy(phrase + off, data + done, copy);
            if (FLASH_Program(&mcxn_flash, aligned, phrase,
                    sizeof(phrase)) != kStatus_FLASH_Success) {
                return -1;
            }
        }
        address += copy;
        done += (int)copy;
    }
    return 0;
}

static int mcxn_flash_erase(uint32_t address, int len)
{
    uint32_t p;
    uint32_t end;

    if (mcxn_flash_init() != 0) {
        return -1;
    }
    address = mcxn_flash_offset(address);
    end = address + (uint32_t)len;
    for (p = address & ~(EMU_FLASH_SECTOR_SIZE - 1u); p < end;
            p += EMU_FLASH_SECTOR_SIZE) {
        if (FLASH_Erase(&mcxn_flash, p, EMU_FLASH_SECTOR_SIZE,
                kFLASH_ApiEraseKey) != kStatus_FLASH_Success) {
            return -1;
        }
    }
    return 0;
}
#endif

#if defined(EMU_LPC55S69)
/* LPC55S69 flash changes only through the boot ROM flash API (iap1 version 1
 * driver), reached with TZEN=0 only. An erased page that was never programmed
 * faults on read (ECC), so erase programs 0xFF back, as the wolfBoot HAL does. */
#define LPC55_ROM_API_TREE      0x130010F0u
#define LPC55_FLASH_PAGE_SIZE   512u
#define LPC55_FLASH_ERASE_KEY   0x6B65666Cu
#define LPC55_FLASH_CFG_WORDS   15u
#define LPC55_FLASH_CFG_FREQ    10u
#define LPC55_FLASH_FREQ_MHZ    96u

typedef struct {
    uint32_t version;
    int (*init)(uint32_t *config);
    int (*erase)(uint32_t *config, uint32_t start, uint32_t len, uint32_t key);
    int (*program)(uint32_t *config, uint32_t start, const uint8_t *src,
        uint32_t len);
} lpc55_flash_api_t;

typedef struct {
    void (*run_bootloader)(void *arg);
    uint32_t version;
    const char *copyright;
    uint32_t reserved;
    const lpc55_flash_api_t *flash;
} lpc55_rom_tree_t;

static uint32_t lpc55_flash_cfg[LPC55_FLASH_CFG_WORDS];
static uint8_t lpc55_page[LPC55_FLASH_PAGE_SIZE];
static const lpc55_flash_api_t *lpc55_flash;

static int lpc55s69_flash_init(void)
{
    if (lpc55_flash == 0) {
        lpc55_flash = ((const lpc55_rom_tree_t *)LPC55_ROM_API_TREE)->flash;
        lpc55_flash_cfg[LPC55_FLASH_CFG_FREQ] = LPC55_FLASH_FREQ_MHZ;
        if (lpc55_flash->init(lpc55_flash_cfg) != 0) {
            lpc55_flash = 0;
            return -1;
        }
    }
    return 0;
}

static int lpc55s69_page_write(uint32_t page, const uint8_t *buf)
{
    if (lpc55_flash->erase(lpc55_flash_cfg, page, LPC55_FLASH_PAGE_SIZE,
            LPC55_FLASH_ERASE_KEY) != 0) {
        return -1;
    }
    if (lpc55_flash->program(lpc55_flash_cfg, page, buf,
            LPC55_FLASH_PAGE_SIZE) != 0) {
        return -1;
    }
    return 0;
}

static int lpc55s69_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t page;
    uint32_t off;
    uint32_t chunk;
    int ret;

    ret = lpc55s69_flash_init();
    while ((ret == 0) && (len > 0)) {
        page = address & ~(LPC55_FLASH_PAGE_SIZE - 1u);
        off = address - page;
        chunk = LPC55_FLASH_PAGE_SIZE - off;
        if (chunk > (uint32_t)len) {
            chunk = (uint32_t)len;
        }
        memcpy(lpc55_page, (const void *)page, LPC55_FLASH_PAGE_SIZE);
        memcpy(lpc55_page + off, data, chunk);
        ret = lpc55s69_page_write(page, lpc55_page);
        address += chunk;
        data += chunk;
        len -= (int)chunk;
    }
    return ret;
}

static int lpc55s69_flash_erase(uint32_t address, int len)
{
    uint32_t p;
    uint32_t end;
    int ret;

    ret = lpc55s69_flash_init();
    memset(lpc55_page, 0xFF, LPC55_FLASH_PAGE_SIZE);
    end = address + (uint32_t)len;
    for (p = address & ~(LPC55_FLASH_PAGE_SIZE - 1u); (ret == 0) && (p < end);
            p += LPC55_FLASH_PAGE_SIZE) {
        ret = lpc55s69_page_write(p, lpc55_page);
    }
    return ret;
}
#endif

#if defined(EMU_MCXW)
/* MCX W71 FMU0: one phrase per command, data stored through the secure flash
 * alias while FSTAT.PEWEN is set, as the wolfBoot HAL does. Only reached with
 * TZEN=0: the non-secure app goes through the wolfBoot_nsc_* veneers. */
#define FMU0_BASE           0x40020000u
#define FMU0_FSTAT          (*(volatile uint32_t *)(FMU0_BASE + 0x00u))
#define FMU0_FCCOB0         (*(volatile uint32_t *)(FMU0_BASE + 0x10u))
#define FMU_FSTAT_CCIF      0x00000080u
#define FMU_FSTAT_PEWEN     0x03000000u
#define FMU_FSTAT_PERDY     0x80000000u
#define FMU_FSTAT_ERR       0x00000075u
#define FMU_FSTAT_CLEAR     0x00010074u
#define FMU_CMD_PGMPHR      0x24u
#define FMU_CMD_ERSSCR      0x42u
#define MCXW_SECURE_ALIAS   0x10000000u
#define MCXW_PHRASE_SIZE    16u
#define MCXW_PHRASE_WORDS   (MCXW_PHRASE_SIZE / 4u)
#define MCXW_SECTOR_SIZE    0x2000u
/* The flash cannot be read while a command runs, so this code sits in SRAM */
#define MCXW_RAMFUNCTION    __attribute__((used, noinline, long_call, \
                                section(".ramcode")))

static uint32_t MCXW_RAMFUNCTION mcxw_fmu_wait(uint32_t mask)
{
    uint32_t fstat;

    do {
        fstat = FMU0_FSTAT;
    } while (((fstat & mask) == 0u) && ((fstat & FMU_FSTAT_ERR) == 0u));
    return fstat;
}

static int MCXW_RAMFUNCTION mcxw_fmu_cmd(uint32_t cmd, uint32_t address,
    const uint32_t *words)
{
    volatile uint32_t *dst;
    uint32_t primask;
    uint32_t fstat;
    uint32_t i;

    dst = (volatile uint32_t *)(address | MCXW_SECURE_ALIAS);
    __asm volatile("mrs %0, primask" : "=r"(primask));
    __asm volatile("cpsid i" ::: "memory");

    while ((FMU0_FSTAT & FMU_FSTAT_CCIF) == 0u) {
    }
    FMU0_FSTAT = FMU_FSTAT_CLEAR;
    FMU0_FCCOB0 = cmd;
    FMU0_FSTAT = FMU_FSTAT_CCIF;

    fstat = mcxw_fmu_wait(FMU_FSTAT_PEWEN);
    if ((fstat & FMU_FSTAT_ERR) == 0u) {
        for (i = 0u; i < MCXW_PHRASE_WORDS; i++) {
            dst[i] = (words != 0) ? words[i] : 0u;
        }
        fstat = mcxw_fmu_wait(FMU_FSTAT_PERDY);
    }
    if ((fstat & FMU_FSTAT_ERR) == 0u) {
        FMU0_FSTAT = FMU_FSTAT_PERDY;
    }
    do {
        fstat = FMU0_FSTAT;
    } while ((fstat & FMU_FSTAT_CCIF) == 0u);

    __asm volatile("msr primask, %0" : : "r"(primask) : "memory");
    return ((fstat & FMU_FSTAT_ERR) != 0u) ? -1 : 0;
}

/* Merges into the current phrase; a programmed phrase is only rewritten after
 * an erase, which is what the ECC flash allows. */
static int mcxw_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t phrase[MCXW_PHRASE_WORDS];
    uint8_t *p = (uint8_t *)phrase;
    const volatile uint32_t *cur;
    uint32_t off;
    uint32_t chunk;
    uint32_t i;
    int erased;
    int changed;
    int ret = 0;

    address &= ~MCXW_SECURE_ALIAS;
    while ((len > 0) && (ret == 0)) {
        off = address & (MCXW_PHRASE_SIZE - 1u);
        chunk = MCXW_PHRASE_SIZE - off;
        if (chunk > (uint32_t)len) {
            chunk = (uint32_t)len;
        }
        cur = (const volatile uint32_t *)(address - off);
        erased = 1;
        for (i = 0u; i < MCXW_PHRASE_WORDS; i++) {
            phrase[i] = cur[i];
            if (phrase[i] != 0xFFFFFFFFu) {
                erased = 0;
            }
        }
        changed = 0;
        for (i = 0u; i < chunk; i++) {
            if (p[off + i] != data[i]) {
                p[off + i] = data[i];
                changed = 1;
            }
        }
        if (changed && !erased) {
            ret = -1;
        }
        else if (changed) {
            ret = mcxw_fmu_cmd(FMU_CMD_PGMPHR, address - off, phrase);
        }
        address += chunk;
        data += chunk;
        len -= (int)chunk;
    }
    return ret;
}

static int mcxw_flash_erase(uint32_t address, int len)
{
    uint32_t end;
    int ret = 0;

    address &= ~MCXW_SECURE_ALIAS;
    end = address + (uint32_t)len;
    address -= address % MCXW_SECTOR_SIZE;
    while ((address < end) && (ret == 0)) {
        ret = mcxw_fmu_cmd(FMU_CMD_ERSSCR, address, 0);
        address += MCXW_SECTOR_SIZE;
    }
    return ret;
}
#endif

#if defined(EMU_RW612)
/* FlexSPI NOR through the boot ROM driver, the path hal/rw612.c takes. Only
 * reached with TZEN=0: the non-secure app goes through the wolfBoot_nsc_*
 * veneers instead, as FlexSPI and the ROM driver stay secure. */
#define RW612_CHIP_INFO          (*(volatile uint32_t *)0x45001114u)
#define RW612_ROM_API_TREE_A0    0x13024100u
#define RW612_ROM_API_TREE_A1    0x13030000u
#define RW612_ROM_API_FLEXSPI_IDX 5u
#define RW612_FLASH_BASE         0x08000000u
#define RW612_FLASH_ALIAS_MASK   0x0FFFFFFFu
#define RW612_FCB_ADDRESS        (RW612_FLASH_BASE + 0x400u)
#define RW612_FCB_SIZE           0x200u
#define RW612_NOR_PAGE_SIZE      256u
#define RW612_NOR_SECTOR_SIZE    0x1000u
#define RW612_FLEXSPI_AHBCR      (*(volatile uint32_t *)0x4013400Cu)
#define RW612_AHBCR_CLRAHBRXBUF  0x00000002u
#define RW612_CACHE64_CCR        (*(volatile uint32_t *)0x40033800u)
#define RW612_CCR_INVW0          0x01000000u
#define RW612_CCR_INVW1          0x04000000u
#define RW612_CCR_GO             0x80000000u

/* Leading entries of flexspi_nor_flash_driver_t (fsl_romapi_flexspi.h) */
typedef struct {
    uint32_t version;
    int32_t (*init)(uint32_t instance, void *config);
    int32_t (*wait_busy)(uint32_t instance, void *config, uint32_t address,
        int keep_state);
    int32_t (*page_program)(uint32_t instance, void *config, uint32_t dst,
        const uint32_t *src, int keep_state);
    int32_t (*erase_all)(uint32_t instance, void *config);
    int32_t (*erase)(uint32_t instance, void *config, uint32_t start,
        uint32_t length);
    int32_t (*erase_sector)(uint32_t instance, void *config, uint32_t address);
} rw612_nor_driver_t;

static uint32_t rw612_nor_cfg[RW612_FCB_SIZE / sizeof(uint32_t)];
static uint32_t rw612_page[RW612_NOR_PAGE_SIZE / sizeof(uint32_t)];
static const rw612_nor_driver_t *rw612_nor;

static void rw612_flash_cache_flush(void)
{
    RW612_FLEXSPI_AHBCR |= RW612_AHBCR_CLRAHBRXBUF;
    RW612_FLEXSPI_AHBCR &= ~RW612_AHBCR_CLRAHBRXBUF;
    RW612_CACHE64_CCR |= RW612_CCR_INVW0 | RW612_CCR_INVW1 | RW612_CCR_GO;
    while ((RW612_CACHE64_CCR & RW612_CCR_GO) != 0u) {
    }
    RW612_CACHE64_CCR &= ~(RW612_CCR_INVW0 | RW612_CCR_INVW1);
}

static int rw612_nor_init(void)
{
    uint32_t tree;

    if (rw612_nor != 0) {
        return 0;
    }
    tree = ((RW612_CHIP_INFO & 0x0Fu) == 0u) ?
        RW612_ROM_API_TREE_A0 : RW612_ROM_API_TREE_A1;
    memcpy(rw612_nor_cfg, (const void *)RW612_FCB_ADDRESS,
        sizeof(rw612_nor_cfg));
    rw612_nor = (const rw612_nor_driver_t *)(uintptr_t)
        ((const uint32_t *)(uintptr_t)tree)[RW612_ROM_API_FLEXSPI_IDX];
    if (rw612_nor->init(0u, rw612_nor_cfg) != 0) {
        rw612_nor = 0;
        return -1;
    }
    rw612_flash_cache_flush();
    return 0;
}

static int rw612_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t offset;
    uint32_t page_off;
    uint32_t chunk;
    uint32_t i;
    int ret = 0;

    if (rw612_nor_init() != 0) {
        return -1;
    }
    offset = (address & RW612_FLASH_ALIAS_MASK) - RW612_FLASH_BASE;
    while ((len > 0) && (ret == 0)) {
        page_off = offset & (RW612_NOR_PAGE_SIZE - 1u);
        chunk = RW612_NOR_PAGE_SIZE - page_off;
        if (chunk > (uint32_t)len) {
            chunk = (uint32_t)len;
        }
        for (i = 0u; i < (RW612_NOR_PAGE_SIZE / sizeof(uint32_t)); i++) {
            rw612_page[i] = 0xFFFFFFFFu;
        }
        memcpy((uint8_t *)rw612_page + page_off, data, chunk);
        if (rw612_nor->page_program(0u, rw612_nor_cfg, offset - page_off,
                rw612_page, 0) != 0) {
            ret = -1;
        }
        rw612_flash_cache_flush();
        offset += chunk;
        data += chunk;
        len -= (int)chunk;
    }
    return ret;
}

static int rw612_flash_erase(uint32_t address, int len)
{
    uint32_t offset;
    uint32_t end;
    int ret = 0;

    if (rw612_nor_init() != 0) {
        return -1;
    }
    offset = (address & RW612_FLASH_ALIAS_MASK) - RW612_FLASH_BASE;
    end = offset + (uint32_t)len;
    offset -= offset % RW612_NOR_SECTOR_SIZE;
    while ((offset < end) && (ret == 0)) {
        if (rw612_nor->erase_sector(0u, rw612_nor_cfg, offset) != 0) {
            ret = -1;
        }
        rw612_flash_cache_flush();
        offset += RW612_NOR_SECTOR_SIZE;
    }
    return ret;
}
#endif

#if defined(EMU_MCXA)
/* MCX A153: internal flash is programmed through the ROM API only, one
 * 16-byte phrase at a time, merged with the current contents as the wolfBoot
 * HAL does (programming can only clear bits). */
#define MCXA_ROM_API_TREE     0x03003FE0u
#define MCXA_FLASH_ERASE_KEY  0x6B65666Cu
#define MCXA_PHRASE_SIZE      16u
#define MCXA_SECTOR_SIZE      0x2000u

struct mcxa_flash_api {
    int32_t (*init)(void *config);
    int32_t (*erase_sector)(void *config, uint32_t start, uint32_t len,
        uint32_t key);
    int32_t (*program_phrase)(void *config, uint32_t start,
        const uint8_t *src, uint32_t len);
};

static uint32_t mcxa_flash_config[16];
static int mcxa_flash_ready;

static const struct mcxa_flash_api *mcxa_flash(void)
{
    const struct mcxa_flash_api *api =
        *(const struct mcxa_flash_api * const *)(MCXA_ROM_API_TREE + 4u);

    if (mcxa_flash_ready == 0) {
        if (api->init(mcxa_flash_config) == 0) {
            mcxa_flash_ready = 1;
        }
    }
    return api;
}

static int mcxa_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint8_t phrase[MCXA_PHRASE_SIZE];
    uint32_t off;
    uint32_t chunk;
    uint32_t i;
    int blank;
    int ret = 0;

    while ((len > 0) && (ret == 0)) {
        off = address & (MCXA_PHRASE_SIZE - 1u);
        chunk = MCXA_PHRASE_SIZE - off;
        if (chunk > (uint32_t)len) {
            chunk = (uint32_t)len;
        }
        memcpy(phrase, (const void *)(address - off), sizeof(phrase));
        memcpy(phrase + off, data, chunk);
        blank = 1;
        for (i = 0u; i < MCXA_PHRASE_SIZE; i++) {
            if (phrase[i] != 0xFFu) {
                blank = 0;
            }
        }
        if (!blank && mcxa_flash()->program_phrase(mcxa_flash_config,
                address - off, phrase, sizeof(phrase)) != 0) {
            ret = -1;
        }
        address += chunk;
        data += chunk;
        len -= (int)chunk;
    }
    return ret;
}

static int mcxa_flash_erase(uint32_t address, int len)
{
    uint32_t start = address & ~(MCXA_SECTOR_SIZE - 1u);
    uint32_t end = (address + (uint32_t)len + MCXA_SECTOR_SIZE - 1u) &
        ~(MCXA_SECTOR_SIZE - 1u);

    if (mcxa_flash()->erase_sector(mcxa_flash_config, start, end - start,
            MCXA_FLASH_ERASE_KEY) != 0) {
        return -1;
    }
    return 0;
}
#endif

#if defined(EMU_NRF5340_NET)
/* Network core NVMC: word stores with CONFIG.WEN set; nRF53 has no ERASEPAGE
 * register, a page is erased by writing 0xFFFFFFFF to its first word with
 * CONFIG.EEN set, as the wolfBoot HAL does. */
#define NVMC_NET_BASE     0x41080000u
#define NVMC_NET_READY    (*(volatile uint32_t *)(NVMC_NET_BASE + 0x400u))
#define NVMC_NET_CONFIG   (*(volatile uint32_t *)(NVMC_NET_BASE + 0x504u))
#define NVMC_NET_WEN      1u
#define NVMC_NET_EEN      2u
#define NVMC_NET_PAGE     0x800u

static void nrf5340_net_wait_ready(void)
{
    while (NVMC_NET_READY == 0u) {
    }
}

static int nrf5340_net_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t i = 0u;
    uint32_t off;
    uint32_t word;
    volatile uint32_t *dst;

    NVMC_NET_CONFIG = NVMC_NET_WEN;
    nrf5340_net_wait_ready();
    while (i < (uint32_t)len) {
        dst = (volatile uint32_t *)((address + i) & ~0x3u);
        off = (address + i) & 0x3u;
        word = *dst;
        if ((off == 0u) && (((uint32_t)len - i) >= 4u)) {
            memcpy(&word, data + i, sizeof(word));
            i += 4u;
        }
        else {
            ((uint8_t *)&word)[off] = data[i];
            i++;
        }
        *dst = word;
        nrf5340_net_wait_ready();
    }
    NVMC_NET_CONFIG = 0u;
    return 0;
}

static int nrf5340_net_flash_erase(uint32_t address, int len)
{
    uint32_t end = address + (uint32_t)len;
    uint32_t p;

    for (p = address & ~(NVMC_NET_PAGE - 1u); p < end; p += NVMC_NET_PAGE) {
        NVMC_NET_CONFIG = NVMC_NET_EEN;
        nrf5340_net_wait_ready();
        *(volatile uint32_t *)p = 0xFFFFFFFFu;
        nrf5340_net_wait_ready();
    }
    NVMC_NET_CONFIG = 0u;
    return 0;
}
#endif

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    if (data == 0 || len <= 0) {
        return 0;
    }
#if defined(EMU_M2354)
    {
        int i = 0;
        while (i < len) {
            uint32_t here = address + (uint32_t)i;
            uint32_t word_addr = here & ~0x3u;
            uint32_t off = here - word_addr;
            uint32_t word = *(volatile uint32_t *)word_addr;
            uint8_t *b = (uint8_t *)&word;
            while (off < 4u && i < len) {
                b[off] = data[i];
                off++;
                i++;
            }
            if (m2354_isp(FMC_ISPCMD_PROGRAM, word_addr, word) != 0) {
                return -1;
            }
        }
    }
#elif defined(EMU_IMXRT700)
    return imxrt700_flash_write(address, data, len);
#elif defined(EMU_MCXN947)
    return mcxn_flash_write(address, data, len);
#elif defined(EMU_LPC55S69)
    return lpc55s69_flash_write(address, data, len);
#elif defined(EMU_MCXW)
    return mcxw_flash_write(address, data, len);
#elif defined(EMU_RW612)
    return rw612_flash_write(address, data, len);
#elif defined(EMU_NRF54L)
    return nrf54l_flash_write(address, data, len);
#elif defined(EMU_MCXA)
    return mcxa_flash_write(address, data, len);
#elif defined(EMU_NRF5340_NET)
    return nrf5340_net_flash_write(address, data, len);
#else
    memcpy((void *)address, data, (size_t)len);
#endif
    return 0;
}

int hal_flash_erase(uint32_t address, int len)
{
#if defined(EMU_M2354)
    uint32_t p;
    uint32_t end;
    if (len <= 0) {
        return 0;
    }
    end = address + (uint32_t)len;
    for (p = address & ~(EMU_FLASH_SECTOR_SIZE - 1u); p < end;
            p += EMU_FLASH_SECTOR_SIZE) {
        if (m2354_isp(FMC_ISPCMD_PAGE_ERASE, p, 0u) != 0) {
            return -1;
        }
    }
    return 0;
#elif defined(EMU_NRF5340)
    (void)address;
    (void)len;
    return 0;
#elif defined(EMU_IMXRT700)
    if (len <= 0) {
        return 0;
    }
    return imxrt700_flash_erase(address, len);
#elif defined(EMU_MCXN947)
    if (len <= 0) {
        return 0;
    }
    return mcxn_flash_erase(address, len);
#elif defined(EMU_LPC55S69)
    if (len <= 0) {
        return 0;
    }
    return lpc55s69_flash_erase(address, len);
#elif defined(EMU_MCXW)
    if (len <= 0) {
        return 0;
    }
    return mcxw_flash_erase(address, len);
#elif defined(EMU_RW612)
    if (len <= 0) {
        return 0;
    }
    return rw612_flash_erase(address, len);
#elif defined(EMU_NRF54L)
    if (len <= 0) {
        return 0;
    }
    return nrf54l_flash_erase(address, len);
#elif defined(EMU_MCXA)
    if (len <= 0) {
        return 0;
    }
    return mcxa_flash_erase(address, len);
#elif defined(EMU_NRF5340_NET)
    if (len <= 0) {
        return 0;
    }
    return nrf5340_net_flash_erase(address, len);
#else
    uint32_t end;
#if defined(EMU_STM32)
    uint32_t base = WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uint32_t sector = EMU_FLASH_SECTOR_SIZE;
    uint32_t start_sector;
    uint32_t end_sector;
    uint32_t snb;
    static uint32_t last_erase_sector = 0xFFFFFFFFu;
#endif
    if (len <= 0) {
        return 0;
    }
#if defined(EMU_STM32)
    if (sector == 0u) {
        return 0;
    }
    if (address < base) {
        return 0;
    }
    end = address + (uint32_t)len;
    start_sector = (address - base) / sector;
    end_sector = ((end - 1u) - base) / sector;
    for (snb = start_sector; snb <= end_sector; ++snb) {
        if (snb == last_erase_sector) {
            continue;
        }
        uint32_t cr = FLASH_NSCR & ~FLASH_CR_SNB_MASK;
        cr |= FLASH_CR_SER | (snb << FLASH_CR_SNB_SHIFT);
        FLASH_NSCR = cr;
        FLASH_NSCR = cr | FLASH_CR_STRT;
        last_erase_sector = snb;
    }
    FLASH_NSCR &= ~FLASH_CR_SER;
#else
    end = address + (uint32_t)len;
    memset((void *)address, 0xFF, (size_t)(end - address));
#endif
    return 0;
#endif
}

void hal_flash_unlock(void)
{
#if defined(EMU_M2354)
    /* SYS register write protection guards ISPCTL. */
    SYS_REGLCTL = 0x59u;
    SYS_REGLCTL = 0x16u;
    SYS_REGLCTL = 0x88u;
    FMC_ISPCTL |= FMC_ISPCTL_ISPEN | FMC_ISPCTL_APUEN;
#elif defined(EMU_STM32)
    if ((FLASH_NSCR & FLASH_CR_LOCK) != 0u) {
        FLASH_NSKEYR = FLASH_KEY1;
        FLASH_NSKEYR = FLASH_KEY2;
    }
    FLASH_NSCR |= FLASH_CR_PG;
#elif defined(EMU_NRF5340)
    NVMC_CONFIG = 1u;
    NVMC_CONFIGNS = 1u;
#elif defined(EMU_NRF54L)
    nrf54l_write_enable(1);
#endif
}

void hal_flash_lock(void)
{
#if defined(EMU_M2354)
    FMC_ISPCTL &= ~(FMC_ISPCTL_ISPEN | FMC_ISPCTL_APUEN);
    SYS_REGLCTL = 0u;
#elif defined(EMU_STM32)
    FLASH_NSCR &= ~FLASH_CR_PG;
    FLASH_NSCR |= FLASH_CR_LOCK;
#elif defined(EMU_NRF5340)
    NVMC_CONFIG = 0u;
    NVMC_CONFIGNS = 0u;
#elif defined(EMU_NRF54L)
    nrf54l_write_enable(0);
#endif
}
