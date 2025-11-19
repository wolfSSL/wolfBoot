/* zynq.c
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

#ifdef TARGET_zynq

#include "hal/zynq.h"

#ifndef ARCH_AARCH64
#   error "wolfBoot zynq HAL: wrong architecture selected. Please compile with ARCH=AARCH64."
#endif

#if defined(__QNXNTO__) && !defined(NO_QNX)
    #define USE_QNX
#elif defined(USE_BUILTIN_STARTUP)
    /* to use the Xilinx QSPI driver define USE_XQSPIPSU */
#endif

#include <target.h>
#include "image.h"
#include "printf.h"

#include <stdint.h>
#include <string.h>

#ifdef USE_XQSPIPSU
    /* Xilinx BSP Driver */
    #include "xqspipsu.h"
    #ifndef QSPI_DEVICE_ID
    #define QSPI_DEVICE_ID      XPAR_XQSPIPSU_0_DEVICE_ID
    #endif
    #ifndef QSPI_CLK_PRESACALE
    #define QSPI_CLK_PRESACALE  XQSPIPSU_CLK_PRESCALE_8
    #endif
#elif defined(USE_QNX)
    /* QNX QSPI driver */
    #include <sys/siginfo.h>
    #include "xzynq_gqspi.h"
#else
    /* QSPI bare-metal */
#endif

/* QSPI Slave Device Information */
typedef struct QspiDev {
    uint32_t mode;   /* GQSPI_GEN_FIFO_MODE_SPI, GQSPI_GEN_FIFO_MODE_DSPI or GQSPI_GEN_FIFO_MODE_QSPI */
    uint32_t bus;    /* GQSPI_GEN_FIFO_BUS_LOW, GQSPI_GEN_FIFO_BUS_UP or GQSPI_GEN_FIFO_BUS_BOTH */
    uint32_t cs;     /* GQSPI_GEN_FIFO_CS_LOWER, GQSPI_GEN_FIFO_CS_UPPER */
    uint32_t stripe; /* OFF=0 or ON=GQSPI_GEN_FIFO_STRIPE */
#ifdef USE_XQSPIPSU
    XQspiPsu qspiPsuInst;
#elif defined(USE_QNX)
    xzynq_qspi_t* qnx;
#endif
} QspiDev_t;

static QspiDev_t mDev;
static uint32_t pmuVer;
#define PMUFW_MIN_VER 0x10001 /* v1.1*/

/* forward declarations */
static int qspi_wait_ready(QspiDev_t* dev);
static int qspi_status(QspiDev_t* dev, uint8_t* status);
static int qspi_wait_we(QspiDev_t* dev);
#ifdef TEST_EXT_FLASH
static int test_ext_flash(QspiDev_t* dev);
#endif

/* asm function */
extern void flush_dcache_range(unsigned long start, unsigned long stop);
extern unsigned int current_el(void);

void hal_delay_ms(uint64_t ms);
uint64_t hal_timer_ms(void);

#ifdef DEBUG_UART
void uart_init(void)
{
    /* Disable Interrupts */
    ZYNQMP_UART_IDR = ZYNQMP_UART_ISR_MASK;
    /* Disable TX/RX */
    ZYNQMP_UART_CR = (ZYNQMP_UART_CR_TX_DIS | ZYNQMP_UART_CR_RX_DIS);
    /* Clear ISR */
    ZYNQMP_UART_ISR = ZYNQMP_UART_ISR_MASK;

    /* 8-bits, no parity */
    ZYNQMP_UART_MR = ZYNQMP_UART_MR_PARITY_NONE;

    /* FIFO Trigger Level */
    ZYNQMP_UART_RXWM = 32; /* half of 64 byte FIFO */
    ZYNQMP_UART_TXWM = 32; /* half of 64 byte FIFO */

    /* RX Timeout - disable */
    ZYNQMP_UART_RXTOUT = 0;

    /* baud (115200) = master clk / (BR_GEN * (BR_DIV + 1)) */
    ZYNQMP_UART_BR_GEN = UART_CLK_REF / (DEBUG_UART_BAUD * (DEBUG_UART_DIV+1));
    ZYNQMP_UART_BR_DIV = DEBUG_UART_DIV;

    /* Reset TX/RX */
    ZYNQMP_UART_CR = (ZYNQMP_UART_CR_TXRST | ZYNQMP_UART_CR_RXRST);

    /* Enable TX/RX */
    ZYNQMP_UART_CR = (ZYNQMP_UART_CR_TX_EN | ZYNQMP_UART_CR_RX_EN);
}

void uart_write(const char* buf, uint32_t sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') { /* handle CRLF */
            while (ZYNQMP_UART_SR & ZYNQMP_UART_SR_TXFULL);
            ZYNQMP_UART_FIFO = '\r';
        }
        while (ZYNQMP_UART_SR & ZYNQMP_UART_SR_TXFULL);
        ZYNQMP_UART_FIFO = c;
    }
    /* Wait till TX Fifo is empty */
    while (!(ZYNQMP_UART_SR & ZYNQMP_UART_SR_TXEMPTY));
}
#endif /* DEBUG_UART */

/* This struct defines the way the registers are stored on the stack during an
 * exception. */
struct pt_regs {
    uint64_t elr;
    uint64_t regs[8];
};

/*
 * void smc_call(arg0, arg1...arg7)
 *
 * issue the secure monitor call
 *
 * x0~x7: input arguments
 * x0~x3: output arguments
 */
static void smc_call(struct pt_regs *args)
{
    asm volatile(
        "ldr x0, %0\n"
        "ldr x1, %1\n"
        "ldr x2, %2\n"
        "ldr x3, %3\n"
        "ldr x4, %4\n"
        "ldr x5, %5\n"
        "ldr x6, %6\n"
        "smc #0\n"
        "str x0, %0\n"
        "str x1, %1\n"
        "str x2, %2\n"
        "str x3, %3\n"
        : "+m" (args->regs[0]), "+m" (args->regs[1]),
          "+m" (args->regs[2]), "+m" (args->regs[3])
        :  "m" (args->regs[4]),  "m" (args->regs[5]),
           "m" (args->regs[6])
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8",
          "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17");
}

#define PM_ARGS_CNT        8
#define PM_SIP_SVC         0xC2000000
#define PM_GET_API_VERSION 0x01
#define PM_SECURE_SHA      0x1A
#define PM_SECURE_RSA      0x1B
#define PM_MMIO_WRITE      0x13
#define PM_MMIO_READ       0x14

/* AES */
/* requires PMU built with -DENABLE_SECURE_VAL=1 */
#define PM_SECURE_AES      0x2F
typedef struct pmu_aes {
    uint64_t src;    /* source address */
    uint64_t iv;     /* initialization vector address */
    uint64_t key;    /* key address */
    uint64_t dst;    /* destination address */
    uint64_t size;   /* size */
    uint64_t op;     /* operation: 0=Decrypt, 1=Encrypt */
    uint64_t keySrc; /* key source 0=KUP, 1=Device Key, 2=Use PUF (do regen) */
} pmu_aes;

/* EFUSE */
/* requires PMU built with -DENABLE_EFUSE_ACCESS=1 */
#define PM_EFUSE_ACCESS    0x35
typedef struct pmu_efuse {
    uint64_t src;        /* adress of data buffer */
    uint32_t size;       /* size in words */
    uint32_t offset;     /* offset */
    uint32_t flag;       /* 0: to read efuse, 1: to write efuse */
    uint32_t pufUserFuse;/* 0: PUF HD, 1: eFuses for User Data */
} pmu_efuse;

/* Secure Monitor Call (SMC) to BL31 Silicon Provider (SIP) service,
 * which is the PMU Firmware */
static int pmu_request(uint32_t api_id,
    uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3,
    uint32_t *ret_payload)
{
    struct pt_regs regs;

    regs.regs[0] = PM_SIP_SVC | api_id;
    regs.regs[1] = ((uint64_t)arg1 << 32) | arg0;
    regs.regs[2] = ((uint64_t)arg3 << 32) | arg2;

    smc_call(&regs);

    if (ret_payload != NULL) {
        ret_payload[0] = (uint32_t)(regs.regs[0]);
        ret_payload[1] = (uint32_t)(regs.regs[0] >> 32);
        ret_payload[2] = (uint32_t)(regs.regs[1]);
        ret_payload[3] = (uint32_t)(regs.regs[1] >> 32);
        ret_payload[4] = (uint32_t)(regs.regs[2]);
        ret_payload[5] = (uint32_t)(regs.regs[2] >> 32);
        ret_payload[6] = (uint32_t)(regs.regs[3]);
        ret_payload[7] = (uint32_t)(regs.regs[3] >> 32);
    }
    return (ret_payload != NULL) ? ret_payload[0] : 0;
}


uint32_t pmu_get_version(void)
{
    uint32_t ret_payload[PM_ARGS_CNT];
    memset(ret_payload, 0, sizeof(ret_payload));
    pmu_request(PM_GET_API_VERSION, 0, 0, 0, 0, ret_payload);
    return ret_payload[1];
}

/* Aligned data buffer for DMA */
#define EFUSE_MAX_BUFSZ (sizeof(pmu_efuse) + 48 /* SHA3-384 Digest */)
static uint8_t XALIGNED(32) efuseBuf[EFUSE_MAX_BUFSZ];

uint32_t pmu_efuse_read(uint32_t offset, uint32_t* data, uint32_t size)
{
    pmu_efuse* efuseCmd = (pmu_efuse*)efuseBuf;
    uint8_t* efuseData = (efuseBuf + sizeof(pmu_efuse));
    uint64_t efuseCmdPtr = (uint64_t)efuseCmd;
    uint32_t ret_payload[PM_ARGS_CNT];
    memset(ret_payload, 0, sizeof(ret_payload));
    memset(efuseBuf, 0, sizeof(efuseBuf));
    efuseCmd->src = (uint64_t)efuseData;
    efuseCmd->offset = (offset & 0xFF); /* offset is only the last 0xFF bits */
    efuseCmd->size = (size/sizeof(uint32_t)); /* number of 32-bit words */
    pmu_request(PM_EFUSE_ACCESS, (efuseCmdPtr >> 32), (efuseCmdPtr & 0xFFFFFFFF),
        0, 0, ret_payload);
    memcpy(data, efuseData, size);
    return ret_payload[0]; /* 0=Success, 30=No Access */
}

uint32_t pmu_mmio_read(uint32_t addr)
{
    uint32_t ret_payload[PM_ARGS_CNT];
    memset(ret_payload, 0, sizeof(ret_payload));
    pmu_request(PM_MMIO_READ, addr, 0, 0, 0, ret_payload);
    return ret_payload[1];
}

uint32_t pmu_mmio_writemask(uint32_t addr, uint32_t mask, uint32_t val)
{
    uint32_t ret_payload[PM_ARGS_CNT];
    memset(ret_payload, 0, sizeof(ret_payload));
    pmu_request(PM_MMIO_WRITE, addr, mask, val, 0, ret_payload);
    return ret_payload[0]; /* 0=Success, 30=No Access */
}

uint32_t pmu_mmio_write(uint32_t addr, uint32_t val)
{
    return pmu_mmio_writemask(addr, 0xFFFFFFFF, val);
}

int pmu_mmio_wait(uint32_t addr, uint32_t wait_mask, uint32_t wait_val,
    uint32_t tries)
{
    uint32_t regval, timeout = 0;
    while ((((regval = pmu_mmio_read(addr)) & wait_mask) != wait_val)
        && ++timeout < tries);
    return (timeout < tries) ? 0 : -1;
}

#ifdef WOLFBOOT_ZYNQMP_CSU

#ifdef WOLFBOOT_HASH_SHA3_384
#include <wolfssl/wolfcrypt/sha3.h>
#define XSECURE_SHA3_INIT   1U
#define XSECURE_SHA3_UPDATE 2U
#define XSECURE_SHA3_FINAL  4U
static uint32_t csu_sha3(uint64_t addr, uint32_t sz, uint32_t flags)
{
    uint32_t ret_payload[PM_ARGS_CNT];
    memset(ret_payload, 0, sizeof(ret_payload));
    pmu_request(PM_SECURE_SHA, (addr >> 32), (addr & 0xFFFFFFFF), sz, flags,
        ret_payload);
    return ret_payload[0];
}

int wc_InitSha3_384(wc_Sha3* sha, void* heap, int devId)
{
    (void)sha;
    (void)heap;
    (void)devId;
    return csu_sha3(0, 0, XSECURE_SHA3_INIT);
}
int wc_Sha3_384_Update(wc_Sha3* sha, const byte* data, word32 len)
{
    (void)sha;
    flush_dcache_range(
        (unsigned long)data,
        (unsigned long)data + len);
    return csu_sha3((uint64_t)data, len, XSECURE_SHA3_UPDATE);
}
int wc_Sha3_384_Final(wc_Sha3* sha, byte* out)
{
    (void)sha;
    flush_dcache_range(
        (unsigned long)out,
        (unsigned long)out + WC_SHA3_384_DIGEST_SIZE);
    return csu_sha3((uint64_t)out, 0, XSECURE_SHA3_FINAL);
}
void wc_Sha3_384_Free(wc_Sha3* sha)
{
    (void)sha;
}
#else
#   error HW_SHA3=1 only supported with HASH=SHA3
#endif

/* CSU PUF */
#ifdef CSU_PUF_ROT
/* 1544 bytes is fixed size for boot header used by CSU ROM */
#define CSU_PUF_SYNDROME_WORDS 386
#ifndef CSU_PUF_REG_TRIES
#define CSU_PUF_REG_TRIES    500000
#endif

int csu_puf_register(uint32_t* syndrome, uint32_t* chash, uint32_t* aux)
{
    int ret;
    uint32_t reg32, puf_status = 0, idx = 0;

#if defined(DEBUG_CSU) && DEBUG_CSU >= 1
    wolfBoot_printf("CSU Puf Register\n");
#endif

    /* try a read from register to make sure PMU has permission */
    reg32 = pmu_mmio_read(CSU_PUF_SHUTTER);
    if (reg32 == 0) {
        wolfBoot_printf("PMUFW PUF Register access not enabled in "
                        "pm_mmio_access pmAccessTable!\n");
        return -1;
    }

    ret = pmu_mmio_write(CSU_PUF_CFG0, CSU_PUF_CFG0_INIT);
    if (ret == 0)
        ret = pmu_mmio_write(CSU_PUF_CFG1, CSU_PUF_CFG1_INIT);
    if (ret == 0)
        ret = pmu_mmio_write(CSU_PUF_SHUTTER, CSU_PUF_SHUTTER_INIT);
    if (ret == 0)
        ret = pmu_mmio_write(CSU_PUF_CMD, CSU_PUF_CMD_REGISTRATION);
    while (ret == 0) {
        /* wait for PUF word ready */
        ret = pmu_mmio_wait(CSU_PUF_STATUS,
            CSU_PUF_STATUS_SYN_WRD_RDY_MASK,
            CSU_PUF_STATUS_SYN_WRD_RDY_MASK,
            CSU_PUF_REG_TRIES);
        if (ret != 0)
            break;

        if ((idx > CSU_PUF_SYNDROME_WORDS-2) /* room for chash and aux */) {
            ret = -2; /* overrun */
            break;
        }

        puf_status = pmu_mmio_read(CSU_PUF_STATUS);
        /* Read in the syndrome */
        syndrome[idx++] = pmu_mmio_read(CSU_PUF_WORD);
        if (puf_status & CSU_PUF_STATUS_KEY_RDY_MASK) {
            *chash = pmu_mmio_read(CSU_PUF_WORD);
            syndrome[CSU_PUF_SYNDROME_WORDS-2] = *chash;
            *aux = (puf_status & CSU_PUF_STATUS_AUX_MASK) >> 4;
            syndrome[CSU_PUF_SYNDROME_WORDS-1] = *aux;
            ret = 0;
            break;
        }
    }

#if defined(DEBUG_CSU) && DEBUG_CSU >= 1
    wolfBoot_printf("Ret %d, Syndrome %d, CHASH 0x%08x, AUX 0x%08x\n",
        ret, (CSU_PUF_SYNDROME_WORDS*4), *chash, *aux);
    for (idx=0; idx<CSU_PUF_SYNDROME_WORDS; idx++) {
        wolfBoot_printf("%08x", syndrome[idx]);
    }
    wolfBoot_printf("\n");
#endif

    return ret;
}

int csu_puf_regeneration(uint32_t* syndrome, uint32_t chash, uint32_t aux)
{
    int ret;
    uint32_t puf_status = 0;

    (void)syndrome;
    (void)chash;
    (void)aux;

    ret = pmu_mmio_write(CSU_PUF_CFG0, CSU_PUF_CFG0_INIT);
    if (ret == 0)
        ret = pmu_mmio_write(CSU_PUF_SHUTTER, CSU_PUF_SHUTTER_INIT);
    if (ret == 0)
        ret = pmu_mmio_write(CSU_PUF_CMD, CSU_PUF_CMD_REGENERATION);

    /* wait 6ms */
    hal_delay_ms(6);

    /* read the puf_status */
    puf_status = pmu_mmio_read(CSU_PUF_STATUS);
    wolfBoot_printf("Regen: PUF Status 0x%08x\n", puf_status);

    return ret;
}
#endif /* CSU_PUF_ROT */

#define CSU_AES_TIMEOUT 150000
#define CSU_DMA_TIMEOUT 300000000U

static int csu_dma_wait_done(int ch)
{
    /* wait for DMA channel done */
    int ret = pmu_mmio_wait(CSUDMA_ISTS(ch), CSUDMA_ISR_DONE, CSUDMA_ISR_DONE,
        CSU_DMA_TIMEOUT);
    /* clear status interrupt */
    if (ret == 0)
        ret = pmu_mmio_write(CSUDMA_ISTS(ch), pmu_mmio_read(CSUDMA_ISTS(ch)));
    return ret;
}
static int csu_dma_transfer(int ch, uintptr_t addr, uint32_t sz, uint32_t flags)
{
    int ret = pmu_mmio_write(CSUDMA_ADDR(ch), (addr & 0xFFFFFFFF));
    if (ret == 0)
        ret = pmu_mmio_write(CSUDMA_ADDR_MSB(ch), (addr >> 32));
    if (ret == 0)
        ret = pmu_mmio_write(CSUDMA_SIZE(ch), (sz | flags));
    return ret;
}

static int csu_aes_reset(void)
{
    /* Reset AES (set and clear) */
    int ret = pmu_mmio_write(CSU_AES_RESET, 1);
    if (ret == 0)
        ret = pmu_mmio_write(CSU_AES_RESET, 0);
    return ret;
}

static int csu_dma_config(int ch, int doSwap)
{
    int ret = 0;
    uint32_t regs, reg;
    regs = reg = pmu_mmio_read(CSUDMA_CTRL(ch));
    if (doSwap)
        reg |= CSUDMA_CTRL_ENDIANNESS;
    else
        reg &= ~CSUDMA_CTRL_ENDIANNESS;
    if (regs != reg)
        ret = pmu_mmio_write(CSUDMA_CTRL(ch), reg);
    return ret;
}

/* AES GCM Encrypt or Decrypt with Device Key setup by CSU ROM */
/* Output must also have room for updated IV at end */
#define AES_GCM_TAG_SZ 12
int csu_aes(int enc, const uint8_t* iv, const uint8_t* in, uint8_t* out, uint32_t sz)
{
    int ret;
    uint32_t reg;

    /* Flush data cache for variables used */
    flush_dcache_range((unsigned long)iv,  (unsigned long)iv + AES_GCM_TAG_SZ);
    flush_dcache_range((unsigned long)in,  (unsigned long)in);
    flush_dcache_range((unsigned long)out, (unsigned long)out + AES_GCM_TAG_SZ);

    /* Configure SSS for DMA <-> AES */
    ret = pmu_mmio_write(CSU_SSS_CFG,
        (CSU_SSS_CFG_AES(CSU_SSS_CFG_SRC_DMA) |
         CSU_SSS_CFG_DMA(CSU_SSS_CFG_SRC_AES)));
    /* Reset AES (set and clear) */
    if (ret == 0)
        ret = csu_aes_reset();
    /* Setup AES GCM Key (use device key) */
    if (ret == 0)
        ret = pmu_mmio_write(CSU_AES_KEY_SRC, CSU_AES_KEY_SRC_DEVICE_KEY);
    /* Trigger key load */
    if (ret == 0)
        ret = pmu_mmio_write(CSU_AES_KEY_LOAD, 1);
    /* Wait till key init done */
    if (ret == 0)
        ret = pmu_mmio_wait(CSU_AES_STATUS, CSU_AES_STATUS_KEY_INIT_DONE,
            CSU_AES_STATUS_KEY_INIT_DONE, CSU_AES_TIMEOUT);
    /* Enable DMA byte swapping */
    if (ret == 0)
        ret = csu_dma_config(CSUDMA_CH_SRC, 1);
    if (ret == 0)
        ret = csu_dma_config(CSUDMA_CH_DST, 1);
    /* Set encrypt or decrypt */
    if (ret == 0)
        ret = pmu_mmio_write(CSU_AES_CFG, enc);
    /* Issue start and wait for DMA IV and data */
    if (ret == 0)
        ret = pmu_mmio_write(CSU_AES_START_MSG, 1);
    /* Send IV with byte swap (not last) */
    if (ret == 0)
        ret = csu_dma_transfer(CSUDMA_CH_SRC,
            (uintptr_t)iv, AES_GCM_TAG_SZ, 0);
    /* wait for IV to send and cear interrupt */
    if (ret == 0)
        ret = csu_dma_wait_done(CSUDMA_CH_SRC);
    /* Setup data to recieve */
    if (ret == 0)
        ret = csu_dma_transfer(CSUDMA_CH_DST,
            (uintptr_t)out, sz + AES_GCM_TAG_SZ, 0);
    /* Send data */
    if (ret == 0)
        ret = csu_dma_transfer(CSUDMA_CH_SRC,
            (uintptr_t)in, sz, CSUDMA_SIZE_LAST_WORD);
    /* Wait for DMA to complete and clear */
    if (ret == 0)
        ret = csu_dma_wait_done(CSUDMA_CH_SRC);
    if (ret == 0)
        ret = csu_dma_wait_done(CSUDMA_CH_DST);
    /* Disable DMA byte swapping */
    if (ret == 0)
        ret = csu_dma_config(CSUDMA_CH_SRC, 0);
    if (ret == 0)
        ret = csu_dma_config(CSUDMA_CH_DST, 0);
    /* Wait for AES done */
    if (ret == 0)
        ret = pmu_mmio_wait(CSU_AES_STATUS, CSU_AES_STATUS_BUSY,
            0, CSU_AES_TIMEOUT);
    return ret;
}

/* zero the kup and expanded key */
int csu_aes_key_zero(void)
{
    int ret;
    uint32_t reg = pmu_mmio_read(CSU_AES_KEY_CLEAR);
    ret = pmu_mmio_write(CSU_AES_KEY_CLEAR,
        (reg | CSU_AES_KEY_CLEAR_KUP | CSU_AES_KEY_CLEAR_EXP));
    if (ret == 0) {
        ret = pmu_mmio_wait(CSU_AES_STATUS,
            (CSU_AES_STATUS_AES_KEY_ZEROED | CSU_AES_STATUS_KUP_ZEROED),
            (CSU_AES_STATUS_AES_KEY_ZEROED | CSU_AES_STATUS_KUP_ZEROED),
            CSU_AES_TIMEOUT);
    }
    return ret;
}

int csu_init(void)
{
    int ret = 0;
#ifdef CSU_PUF_ROT
    #if 0
    uint32_t syndrome[CSU_PUF_SYNDROME_WORDS];
    uint32_t chash=0, aux=0;
    #if defined(DEBUG_CSU) && DEBUG_CSU >= 1
    uint32_t idx;
    #endif
    #endif
#endif
    uint32_t reg1 = pmu_mmio_read(CSU_IDCODE);
    uint32_t reg2 = pmu_mmio_read(CSU_VERSION);
    uint64_t ms;

    wolfBoot_printf("CSU ID 0x%08x, Ver 0x%08x\n",
        reg1, reg2 & CSU_VERSION_MASK);

#ifdef DEBUG_CSU
    /* Enable JTAG */
    wolfBoot_printf("Enabling JTAG\n");
    pmu_mmio_write(CSU_JTAG_SEC, 0x3F);
    pmu_mmio_write(CSU_JTAG_DAP_CFG, 0xFF);
    pmu_mmio_write(CSU_JTAG_CHAIN_CFG, 0x3);
    pmu_mmio_write(CRL_APB_DBG_LPD_CTRL, 0x01002002);
    pmu_mmio_write(CRL_APB_RST_LPD_DBG, 0x0);
    pmu_mmio_write(CSU_PCAP_PROG, 0x1);

    /* Wait until JTAG is attached */
    while ((reg1 = pmu_mmio_read(CSU_JTAG_CHAIN_STATUS)) == 0);
    wolfBoot_printf("JTAG Attached: status 0x%x\n", reg1);
    hal_delay_ms(500); /* give time for debugger to break */
#endif

#ifdef CSU_PUF_ROT
    reg1 = pmu_mmio_read(CSU_PUF_STATUS);
    wolfBoot_printf("PUF Status 0x%08x\n", reg1);

    /* Read eFuse SEC ctrl bits */
    pmu_efuse_read(ZYNQMP_EFUSE_SEC_CTRL, &reg1, sizeof(reg1));
    wolfBoot_printf("eFuse SEC_CTRL 0x%08x\n", reg1);

    /* Read eFUSE helper data */
    pmu_efuse_read(ZYNQMP_EFUSE_PUF_CHASH, &reg1, sizeof(reg1));
    pmu_efuse_read(ZYNQMP_EFUSE_PUF_AUX, &reg2, sizeof(reg2));
    wolfBoot_printf("eFuse PUF CHASH 0x%08x, AUX 0x%08x\n", reg1, reg2);

    /* CSU PUF only supported with eFuses */
    /* Keeping code for reference in future generations like Versal */
    /* Red (sensitive key), Black (protected key), Grey (unknown) */
    #if 0
    memset(syndrome, 0, sizeof(syndrome));
    ms = hal_timer_ms();
    ret = csu_puf_register(syndrome, &chash, &aux);
    wolfBoot_printf("CSU Register PUF %d: %dms\n", ret, hal_timer_ms() - ms);

    if (ret == 0) {
        ms = hal_timer_ms();
        /* regenerate - load kek */
        ret = csu_puf_regeneration(syndrome, chash, aux);
        wolfBoot_printf("CSU Regen PUF %d: %dms\n", ret, hal_timer_ms() - ms);
    }
    if (ret == 0) {
        /* Use CSU ROM device key and IV to encrypt the red key */
        /* Possible PUF syndrome location 0xFFC30000 */
    #if defined(DEBUG_CSU) && DEBUG_CSU >= 1
        wolfBoot_printf("Red Key %d\n", sizeof(redKey));
        for (idx=0; idx<sizeof(redKey); idx++) {
            wolfBoot_printf("%02x", redKey[idx]);
        }
        wolfBoot_printf("\nBlack IV %d\n", sizeof(blackIv));
        for (idx=0; idx<sizeof(blackIv); idx++) {
            wolfBoot_printf("%02x", blackIv[idx]);
        }
        wolfBoot_printf("\n");
    #endif

        ret = csu_aes(CSU_AES_CFG_ENC, blackIv, redKey, blackKey, KEY_WRAP_SZ);

    #if defined(DEBUG_CSU) && DEBUG_CSU >= 1
        wolfBoot_printf("Black Key %d\n", KEY_WRAP_SZ);
        for (idx=0; idx<KEY_WRAP_SZ; idx++) {
            wolfBoot_printf("%02x", blackKey[idx]);
        }
        wolfBoot_printf("\nNew IV %d\n", AES_GCM_TAG_SZ);
        for (idx=0; idx<AES_GCM_TAG_SZ; idx++) {
            wolfBoot_printf("%02x", blackKey[KEY_WRAP_SZ+idx]);
        }
        wolfBoot_printf("\n");
    #endif
    #endif
    }
#endif

    return ret;
}
#endif /* WOLFBOOT_ZYNQMP_CSU */


#ifdef USE_XQSPIPSU
/* Xilinx BSP Driver */

/* Aligned page data buffer for DMA */
static uint8_t XALIGNED(32) pageData[FLASH_PAGE_SIZE];
static int qspi_transfer(QspiDev_t* pDev,
    const uint8_t* cmdData, uint32_t cmdSz,
    const uint8_t* txData, uint32_t txSz,
    uint8_t* rxData, uint32_t rxSz, uint32_t dummySz,
    uint32_t mode)
{
    int ret;
    XQspiPsu_Msg msgs[4];
    uint32_t msgCnt = 0, busWidth = XQSPIPSU_SELECT_MODE_SPI;
    uint8_t* rxPtr = rxData;

    /* Chip Select */
    if (pDev->cs == GQSPI_GEN_FIFO_CS_BOTH) {
        XQspiPsu_SelectFlash(&pDev->qspiPsuInst,
            XQSPIPSU_SELECT_FLASH_CS_BOTH, XQSPIPSU_SELECT_FLASH_BUS_BOTH);
    }
    else if (pDev->cs == GQSPI_GEN_FIFO_CS_LOWER) {
        XQspiPsu_SelectFlash(&pDev->qspiPsuInst,
            XQSPIPSU_SELECT_FLASH_CS_LOWER, XQSPIPSU_SELECT_FLASH_BUS_LOWER);
    }
    else {
        XQspiPsu_SelectFlash(&pDev->qspiPsuInst,
            XQSPIPSU_SELECT_FLASH_CS_UPPER, XQSPIPSU_SELECT_FLASH_BUS_UPPER);
    }

    /* Transfer Bus Width - only applies to read/write command */
    if (mode == GQSPI_GEN_FIFO_MODE_QSPI)
        busWidth = XQSPIPSU_SELECT_MODE_QUADSPI;
    else if (mode == GQSPI_GEN_FIFO_MODE_DSPI)
        busWidth = XQSPIPSU_SELECT_MODE_DUALSPI;

    /* Command */
    memset(&msgs[msgCnt], 0, sizeof(XQspiPsu_Msg));
    msgs[msgCnt].TxBfrPtr = (uint8_t*)cmdData;
    msgs[msgCnt].ByteCount = cmdSz;
    msgs[msgCnt].BusWidth = XQSPIPSU_SELECT_MODE_SPI;
    msgs[msgCnt].Flags = XQSPIPSU_MSG_FLAG_TX;
    msgCnt++;

    /* TX */
    if (txData) {
        memset(&msgs[msgCnt], 0, sizeof(XQspiPsu_Msg));
        msgs[msgCnt].TxBfrPtr = (uint8_t*)txData;
        msgs[msgCnt].ByteCount = txSz;
        msgs[msgCnt].BusWidth = busWidth;
        msgs[msgCnt].Flags = XQSPIPSU_MSG_FLAG_TX;
        if (pDev->stripe & GQSPI_GEN_FIFO_STRIPE)
            msgs[msgCnt].Flags |= XQSPIPSU_MSG_FLAG_STRIPE;
        msgCnt++;
    }

    /* Dummy */
    if (dummySz > 0) {
        memset(&msgs[msgCnt], 0, sizeof(XQspiPsu_Msg));
        msgs[msgCnt].ByteCount = dummySz; /* not used */
        msgs[msgCnt].BusWidth = busWidth;
        msgCnt++;
    }

    /* RX */
    if (rxData) {
        /* If RX pointer is not 32 byte aligned then use temp page data buffer */
        if (((size_t)rxPtr % 32) != 0)
            rxPtr = pageData;
        if (rxSz > (uint32_t)sizeof(pageData))
            rxSz = (uint32_t)sizeof(pageData);
        memset(&msgs[msgCnt], 0, sizeof(XQspiPsu_Msg));
        msgs[msgCnt].RxBfrPtr = rxPtr;
        msgs[msgCnt].ByteCount = rxSz;
        msgs[msgCnt].BusWidth = busWidth;
        msgs[msgCnt].Flags = XQSPIPSU_MSG_FLAG_RX;
        if (pDev->stripe & GQSPI_GEN_FIFO_STRIPE)
            msgs[msgCnt].Flags |= XQSPIPSU_MSG_FLAG_STRIPE;
        msgCnt++;
    }

    ret = XQspiPsu_PolledTransfer(&pDev->qspiPsuInst, msgs, msgCnt);
    if (ret < 0) {
        wolfBoot_printf("QSPI Transfer failed! %d\n", ret);
        return GQSPI_CODE_FAILED;
    }

    /* if unaligned read, return results */
    if (rxData && rxPtr == pageData) {
        memcpy(rxData, pageData, rxSz);
    }

    return GQSPI_CODE_SUCCESS;
}

#elif defined(USE_QNX)
/* QNX QSPI driver */
static int qspi_transfer(QspiDev_t* pDev,
    const uint8_t* cmdData, uint32_t cmdSz,
    const uint8_t* txData, uint32_t txSz,
    uint8_t* rxData, uint32_t rxSz, uint32_t dummySz,
    uint32_t mode)
{
    int ret;
    qspi_buf cmd_buf;
    qspi_buf tx_buf;
    qspi_buf rx_buf;
    uint32_t flags;

    flags = TRANSFER_FLAG_DEBUG;
    if (mode == GQSPI_GEN_FIFO_MODE_QSPI)
        flags |= TRANSFER_FLAG_MODE(TRANSFER_FLAG_MODE_QSPI);
    else if (mode == GQSPI_GEN_FIFO_MODE_DSPI)
        flags |= TRANSFER_FLAG_MODE(TRANSFER_FLAG_MODE_DSPI);
    else
        flags |= TRANSFER_FLAG_MODE(TRANSFER_FLAG_MODE_SPI);
    if (pDev->stripe & GQSPI_GEN_FIFO_STRIPE)
        flags |= TRANSFER_FLAG_STRIPE;
    if (pDev->cs & GQSPI_GEN_FIFO_CS_LOWER)
        flags |= TRANSFER_FLAG_LOW_DB | TRANSFER_FLAG_CS(TRANSFER_FLAG_CS_LOW);
    if (pDev->cs & GQSPI_GEN_FIFO_CS_UPPER)
        flags |= TRANSFER_FLAG_UP_DB | TRANSFER_FLAG_CS(TRANSFER_FLAG_CS_UP);

    memset(&cmd_buf, 0, sizeof(cmd_buf));
    cmd_buf.offset = (uint8_t*)cmdData;
    cmd_buf.len = cmdSz;

    memset(&tx_buf, 0, sizeof(tx_buf));
    tx_buf.offset = (uint8_t*)txData;
    tx_buf.len = txSz;

    memset(&rx_buf, 0, sizeof(rx_buf));
    rx_buf.offset = rxData;
    rx_buf.len = rxSz;

    /* Send the TX buffer */
    ret = xzynq_qspi_transfer(pDev->qnx,
        txData ? &tx_buf : NULL,
        rxData ? &rx_buf : NULL,
        &cmd_buf, flags);
    if (ret < 0) {
        wolfBoot_printf("QSPI Transfer failed! %d\n", ret);
        return GQSPI_CODE_FAILED;
    }
    return GQSPI_CODE_SUCCESS;
}
#else
/* QSPI bare-metal driver */
static inline int qspi_isr_wait(uint32_t wait_mask, uint32_t wait_val)
{
    uint32_t timeout = 0;
    while ((GQSPI_ISR & wait_mask) == wait_val &&
           ++timeout < GQSPI_TIMEOUT_TRIES);
    if (timeout == GQSPI_TIMEOUT_TRIES) {
        return -1;
    }
    return 0;
}
#ifndef GQSPI_MODE_IO
static inline int qspi_dmaisr_wait(uint32_t wait_mask, uint32_t wait_val)
{
    uint32_t timeout = 0;
    while ((GQSPIDMA_ISR & wait_mask) == wait_val &&
           ++timeout < GQSPIDMA_TIMEOUT_TRIES);
    if (timeout == GQSPIDMA_TIMEOUT_TRIES) {
        return -1;
    }
    return 0;
}
#endif

static int qspi_gen_fifo_write(uint32_t reg_genfifo)
{
    /* wait until the gen FIFO is not full to write */
    if (qspi_isr_wait(GQSPI_IXR_GEN_FIFO_NOT_FULL, 0)) {
        return GQSPI_CODE_TIMEOUT;
    }

#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 3
    wolfBoot_printf("FifoEntry=%08x\n", reg_genfifo);
#endif
    GQSPI_GEN_FIFO = reg_genfifo;
    return GQSPI_CODE_SUCCESS;
}

static int gspi_fifo_tx(const uint8_t* data, uint32_t sz)
{
    uint32_t tmp32;
    while (sz > 0) {
        /* Wait for TX FIFO not full */
        if (qspi_isr_wait(GQSPI_IXR_TX_FIFO_FULL, GQSPI_IXR_TX_FIFO_FULL)) {
            return GQSPI_CODE_TIMEOUT;
        }

    #if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 3
        uint32_t txSz = sz;
        if (txSz > GQSPI_FIFO_WORD_SZ)
            txSz = GQSPI_FIFO_WORD_SZ;
        memcpy(&tmp32, data, txSz);
        GQSPI_TXD = tmp32;
        wolfBoot_printf("TXD=%08x\n", tmp32);

        sz -= txSz;
        data += txSz;
    #else
        /* Write data */
        if (sz >= 4) {
            GQSPI_TXD = *(uint32_t*)data;
            data += 4;
            sz -= 4;
        }
        else {
            tmp32 = 0;
            memcpy(&tmp32, data, sz);
            GQSPI_TXD = tmp32;
            sz = 0;
        }
    #endif
    }
    return GQSPI_CODE_SUCCESS;
}

#ifdef GQSPI_MODE_IO
static int gspi_fifo_rx(uint8_t* data, uint32_t sz)
{
    uint32_t tmp32;

    while (sz > 0) {
        /* Wait for RX FIFO not empty */
        if (qspi_isr_wait(GQSPI_IXR_RX_FIFO_NOT_EMPTY, 0)) {
            return GQSPI_CODE_TIMEOUT;
        }

    #if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 3
        uint32_t rxSz = sz;
        if (rxSz > GQSPI_FIFO_WORD_SZ)
            rxSz = GQSPI_FIFO_WORD_SZ;
        tmp32 = GQSPI_RXD;
        memcpy(data, &tmp32, rxSz);
        wolfBoot_printf("RXD=%08x\n", tmp32);
        sz -= rxSz;
        data += rxSz;
    #else
        if (sz >= 4) {
            *(uint32_t*)data = GQSPI_RXD;
            data += 4;
            sz -= 4;
        }
        else {
            tmp32 = GQSPI_RXD;
            memcpy(data, &tmp32, sz);
            sz = 0;
        }
    #endif
    }
    return GQSPI_CODE_SUCCESS;
}
#endif

static int qspi_cs(QspiDev_t* pDev, int csAssert)
{
    uint32_t reg_genfifo;

    /* Select slave bus, bank, mode and cs clocks */
    reg_genfifo = (pDev->bus & GQSPI_GEN_FIFO_BUS_MASK);
    reg_genfifo |= GQSPI_GEN_FIFO_MODE_SPI;
    if (csAssert) {
        reg_genfifo |= (pDev->cs & GQSPI_GEN_FIFO_CS_MASK);
        reg_genfifo |= GQSPI_GEN_FIFO_IMM(GQSPI_CS_ASSERT_CLOCKS);
    }
    else {
        reg_genfifo |= GQSPI_GEN_FIFO_IMM(GQSPI_CS_DEASSERT_CLOCKS);
    }
    return qspi_gen_fifo_write(reg_genfifo);
}

static uint32_t qspi_calc_exp(uint32_t xferSz, uint32_t* reg_genfifo)
{
    uint32_t expval;
    *reg_genfifo &= ~(GQSPI_GEN_FIFO_IMM_MASK | GQSPI_GEN_FIFO_EXP_MASK);
    if (xferSz > GQSPI_GEN_FIFO_IMM_MASK) {
        /* Use exponent mode (DMA max is 2^28) */
        for (expval=28; expval>=8; expval--) {
            /* find highest value */
            if (xferSz >= (1UL << expval)) {
                *reg_genfifo |= GQSPI_GEN_FIFO_EXP_MASK;
                *reg_genfifo |= GQSPI_GEN_FIFO_IMM(expval); /* IMM=exponent */
                xferSz = (1UL << expval);
                break;
            }
        }
    }
    else {
        /* Use length mode */
        *reg_genfifo |= GQSPI_GEN_FIFO_IMM(xferSz); /* IMM=actual length */
    }
    return xferSz;
}

#ifndef GQSPI_MODE_IO
static uint8_t XALIGNED(QQSPI_DMA_ALIGN) dmatmp[GQSPI_DMA_TMPSZ];
#endif

static int qspi_transfer(QspiDev_t* pDev,
    const uint8_t* cmdData, uint32_t cmdSz,
    const uint8_t* txData, uint32_t txSz,
    uint8_t* rxData, uint32_t rxSz, uint32_t dummySz,
    uint32_t mode)
{
    int ret = GQSPI_CODE_SUCCESS;
    uint32_t reg_genfifo, xferSz;
#ifndef GQSPI_MODE_IO
    uint8_t* dmarxptr = NULL;
#endif
    GQSPI_EN = 1; /* Enable device */
    qspi_cs(pDev, 1); /* Select slave */

    /* Setup bus slave selection */
    reg_genfifo = ((pDev->bus & GQSPI_GEN_FIFO_BUS_MASK) |
                   (pDev->cs & GQSPI_GEN_FIFO_CS_MASK) |
                    GQSPI_GEN_FIFO_MODE_SPI);

    /* Cmd Data */
    xferSz = cmdSz;
    while (ret == GQSPI_CODE_SUCCESS && cmdData && xferSz > 0) {
       /* Enable TX and send command inline */
       reg_genfifo &= ~(GQSPI_GEN_FIFO_RX | GQSPI_GEN_FIFO_IMM_MASK);
       reg_genfifo |= GQSPI_GEN_FIFO_TX;
       reg_genfifo |= GQSPI_GEN_FIFO_IMM(*cmdData); /* IMM is data */

       /* Submit general FIFO operation */
       ret = qspi_gen_fifo_write(reg_genfifo);
       if (ret != GQSPI_CODE_SUCCESS) {
           wolfBoot_printf("zynq.c:%d (error %d)\n", __LINE__, ret);
           break;
       }

       /* offset size and buffer */
       xferSz--;
       cmdData++;
    }

    /* Set desired data mode */
    reg_genfifo |= (mode & GQSPI_GEN_FIFO_MODE_MASK);

    /* TX Data */
    while (ret == GQSPI_CODE_SUCCESS && txData && txSz > 0) {
        /* Enable TX */
        reg_genfifo &= ~(GQSPI_GEN_FIFO_RX | GQSPI_GEN_FIFO_IMM_MASK |
                         GQSPI_GEN_FIFO_EXP_MASK);
        reg_genfifo |= (GQSPI_GEN_FIFO_TX | GQSPI_GEN_FIFO_DATA_XFER);
        reg_genfifo |= (pDev->stripe & GQSPI_GEN_FIFO_STRIPE);
        xferSz = qspi_calc_exp(txSz, &reg_genfifo);

        /* Submit general FIFO operation */
        ret = qspi_gen_fifo_write(reg_genfifo);
        if (ret != GQSPI_CODE_SUCCESS) {
            wolfBoot_printf("zynq.c:%d (error %d)\n", __LINE__, ret);
        }

        /* Fill FIFO */
        ret = gspi_fifo_tx(txData, xferSz);
        if (ret != GQSPI_CODE_SUCCESS) {
            wolfBoot_printf("zynq.c:%d (error %d)\n", __LINE__, ret);
            break;
        }

        /* offset size and buffer */
        txSz -= xferSz;
        txData += xferSz;
    }

    /* Dummy operations */
    if (ret == GQSPI_CODE_SUCCESS && dummySz) {
        /* Send dummy clocks (Disable TX & RX), do not set stripe */
        reg_genfifo &= ~(GQSPI_GEN_FIFO_TX | GQSPI_GEN_FIFO_RX |
                         GQSPI_GEN_FIFO_IMM_MASK | GQSPI_GEN_FIFO_EXP_MASK |
                         GQSPI_GEN_FIFO_STRIPE);
        reg_genfifo |= GQSPI_GEN_FIFO_DATA_XFER;
        /* IMM is number of dummy clock cycles */
        reg_genfifo |= GQSPI_GEN_FIFO_IMM(dummySz);
        ret = qspi_gen_fifo_write(reg_genfifo); /* Submit FIFO Dummy Op */
    }

    /* RX Data */
    while (ret == GQSPI_CODE_SUCCESS && rxData && rxSz > 0) {
        /* Enable RX */
        reg_genfifo &= ~(GQSPI_GEN_FIFO_TX | GQSPI_GEN_FIFO_IMM_MASK |
                         GQSPI_GEN_FIFO_EXP_MASK);
        reg_genfifo |= (GQSPI_GEN_FIFO_RX | GQSPI_GEN_FIFO_DATA_XFER);
        reg_genfifo |= (pDev->stripe & GQSPI_GEN_FIFO_STRIPE);

        xferSz = qspi_calc_exp(rxSz, &reg_genfifo);
    #ifndef GQSPI_MODE_IO
        /* check if pointer is aligned or odd remainder */
        dmarxptr = rxData;
        if (((size_t)rxData & (QQSPI_DMA_ALIGN-1)) || (xferSz & 3)) {
            dmarxptr = (uint8_t*)dmatmp;
            xferSz = ((xferSz + (QQSPI_DMA_ALIGN-1)) & ~(QQSPI_DMA_ALIGN-1));
            if (xferSz > (uint32_t)sizeof(dmatmp)) {
                xferSz = (uint32_t)sizeof(dmatmp);
            }
            /* re-adjust transfer */
            xferSz = qspi_calc_exp(xferSz, &reg_genfifo);
        }

        GQSPIDMA_DST = ((uintptr_t)dmarxptr & 0xFFFFFFFF);
        GQSPIDMA_DST_MSB = ((uintptr_t)dmarxptr >> 32);
        GQSPIDMA_SIZE = xferSz;
        GQSPIDMA_IER = GQSPIDMA_ISR_DONE; /* enable DMA done interrupt */
        flush_dcache_range((unsigned long)dmarxptr,
            (unsigned long)dmarxptr + xferSz);
    #endif

#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    #ifndef GQSPI_MODE_IO
        wolfBoot_printf("DMA: ptr %p, xferSz %d\n", dmarxptr, xferSz);
    #else
        wolfBoot_printf("IO: ptr %p, xferSz %d\n", rxData, xferSz);
    #endif
#endif

        /* Submit general FIFO operation */
        ret = qspi_gen_fifo_write(reg_genfifo);
        if (ret != GQSPI_CODE_SUCCESS) {
            wolfBoot_printf("zynq.c:%d (error %d)\n", __LINE__, ret);
            break;
        }

    #ifdef GQSPI_MODE_IO
        /* Read FIFO */
        ret = gspi_fifo_rx(rxData, xferSz);
        if (ret != GQSPI_CODE_SUCCESS) {
            wolfBoot_printf("zynq.c:%d (error %d)\n", __LINE__, ret);
        }
    #else
        /* Wait for DMA done */
        if (qspi_dmaisr_wait(GQSPIDMA_ISR_DONE, 0)) {
            return GQSPI_CODE_TIMEOUT;
        }
        GQSPIDMA_ISR = GQSPIDMA_ISR_DONE; /* clear DMA interrupt */
        /* adjust xfer sz */
        if (xferSz > rxSz)
            xferSz = rxSz;
        /* copy result if not aligned */
        if (dmarxptr != rxData) {
            memcpy(rxData, dmarxptr, xferSz);
        }
        #if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 3
        if (xferSz <= 1024) {
            for (uint32_t i=0; i<xferSz; i+=4) {
                wolfBoot_printf("RXD=%08x\n", *((uint32_t*)&rxData[i]));
            }
        }
        #endif
    #endif

        /* offset size and buffer */
        rxSz -= xferSz;
        rxData += xferSz;
    }

    qspi_cs(pDev, 0); /* Deselect Slave */
    GQSPI_EN = 0; /* Disable Device */

    return ret;
}
#endif /* QSPI Implementation */

static int qspi_flash_read_id(QspiDev_t* dev, uint8_t* id, uint32_t idSz)
{
    int ret;
    uint8_t cmd[4]; /* size multiple of uint32_t */
    uint8_t status = 0;

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = READ_ID_CMD;
    ret = qspi_transfer(&mDev, cmd, 1, NULL, 0, cmd, sizeof(cmd), 0,
        GQSPI_GEN_FIFO_MODE_SPI);

    wolfBoot_printf("Read FlashID %s: Ret %d, %02x %02x %02x\n",
        (dev->cs & GQSPI_GEN_FIFO_CS_LOWER) ? "Lower" : "Upper",
        ret, cmd[0],  cmd[1],  cmd[2]);

    if (ret == GQSPI_CODE_SUCCESS && id) {
        if (idSz > sizeof(cmd))
            idSz = sizeof(cmd);
        memcpy(id, cmd, idSz);
    }

    qspi_status(dev, &status);
    if (status & WRITE_EN_MASK) {
        wolfBoot_printf("Write disabled: status %02x\n", status);
        ret = -1;
    }

    return ret;
}

static int qspi_write_enable(QspiDev_t* dev)
{
    int ret;
    uint8_t cmd[4]; /* size multiple of uint32_t */
    uint8_t status = 0;

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = WRITE_ENABLE_CMD;
    ret = qspi_transfer(&mDev, cmd, 1, NULL, 0, NULL, 0, 0,
        GQSPI_GEN_FIFO_MODE_SPI);
#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("Write Enable: Ret %d\n", ret);
#endif
    ret = qspi_wait_ready(dev);
#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("Wait ready: Ret %d\n", ret);
#endif

    ret = qspi_wait_we(dev);
#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("Wait we: Ret %d\n", ret);
#endif

    qspi_status(dev, &status);
    if ((status & WRITE_EN_MASK) == 0) {
        wolfBoot_printf("Write enable failed: status %02x\n", status);
        ret = -1;
    }

    return ret;
}
static int qspi_write_disable(QspiDev_t* dev)
{
    int ret;
    uint8_t cmd[4]; /* size multiple of uint32_t */

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = WRITE_DISABLE_CMD;
    ret = qspi_transfer(dev, cmd, 1, NULL, 0, NULL, 0, 0,
        GQSPI_GEN_FIFO_MODE_SPI);
#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("Write Disable: Ret %d\n", ret);
#endif
    return ret;
}

static int qspi_flash_status(QspiDev_t* dev, uint8_t* status)
{
    int ret;
    uint8_t cmd[4]; /* size multiple of uint32_t */

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = READ_FSR_CMD;
    ret = qspi_transfer(dev, cmd, 1, NULL, 0, cmd, 2, 0,
        GQSPI_GEN_FIFO_MODE_SPI);
#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("Flash Status: Ret %d Cmd %02x %02x\n", ret, cmd[0], cmd[1]);
#endif
    if (ret == GQSPI_CODE_SUCCESS && status) {
        if (dev->stripe) {
            cmd[0] &= cmd[1];
        }
        *status = cmd[0];
    }
    return ret;
}

static int qspi_status(QspiDev_t* dev, uint8_t* status)
{
    int ret;
    uint8_t cmd[4]; /* size multiple of uint32_t */

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = READ_SR_CMD;
    ret = qspi_transfer(dev, cmd, 1, NULL, 0, cmd, 2, 0,
        GQSPI_GEN_FIFO_MODE_SPI);
#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("Status: Ret %d Cmd %02x %02x\n", ret, cmd[0], cmd[1]);
#endif
    if (ret == GQSPI_CODE_SUCCESS && status) {
        if (dev->stripe) {
            cmd[0] &= cmd[1];
        }
        *status = cmd[0];
    }
    return ret;
}

static int qspi_wait_ready(QspiDev_t* dev)
{
    int ret;
    uint32_t timeout;
    uint8_t status = 0;

    timeout = 0;
    while (++timeout < QSPI_FLASH_READY_TRIES) {
        ret = qspi_flash_status(dev, &status);
        if (ret == GQSPI_CODE_SUCCESS && (status & FLASH_READY_MASK)) {
            return ret;
        }
    }

    wolfBoot_printf("Flash Ready Timeout!\n");

    return GQSPI_CODE_TIMEOUT;
}

static int qspi_wait_we(QspiDev_t* dev)
{
    int ret;
    uint32_t timeout;
    uint8_t status = 0;

    timeout = 0;
    while (++timeout < QSPI_FLASH_READY_TRIES) {
        ret = qspi_status(dev, &status);
        if (ret == GQSPI_CODE_SUCCESS &&
        (status & WRITE_EN_MASK)
        ) {
            return ret;
        }
    }

    wolfBoot_printf("Flash WE Timeout!\n");

    return GQSPI_CODE_TIMEOUT;
}


#if GQPI_USE_4BYTE_ADDR == 1
static int qspi_enter_4byte_addr(QspiDev_t* dev)
{
    int ret;
    uint8_t cmd[4]; /* size multiple of uint32_t */

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = ENTER_4B_ADDR_MODE_CMD;
    (void)qspi_wait_ready(&mDev); /* Wait for not busy */
    ret = qspi_write_enable(&mDev);
    if (ret == GQSPI_CODE_SUCCESS) {
        ret = qspi_transfer(dev, cmd, 1, NULL, 0, NULL, 0, 0,
            GQSPI_GEN_FIFO_MODE_SPI);
    #if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
        wolfBoot_printf("Enter 4-byte address mode: Ret %d\n", ret);
    #endif
        if (ret == GQSPI_CODE_SUCCESS) {
            ret = qspi_wait_ready(&mDev); /* Wait for not busy */
        }
        qspi_write_disable(&mDev);
    }
    return ret;
}
static int qspi_exit_4byte_addr(QspiDev_t* dev)
{
    int ret;
    uint8_t cmd[4]; /* size multiple of uint32_t */

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = EXIT_4B_ADDR_MODE_CMD;
    ret = qspi_write_enable(&mDev);
    if (ret == GQSPI_CODE_SUCCESS) {
        ret = qspi_transfer(dev, cmd, 1, NULL, 0, NULL, 0, 0,
            GQSPI_GEN_FIFO_MODE_SPI);
    #if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
        wolfBoot_printf("Exit 4-byte address mode: Ret %d\n", ret);
    #endif
        if (ret == GQSPI_CODE_SUCCESS) {
            ret = qspi_wait_ready(&mDev); /* Wait for not busy */
        }
        qspi_write_disable(&mDev);
    }
    return ret;
}
#endif

/* QSPI functions */
void qspi_init(void)
{
    int ret;
    uint32_t reg_cfg, reg_isr;
    uint8_t id_low[4];
#if GQPI_USE_DUAL_PARALLEL == 1
    uint8_t id_hi[4];
#endif
    uint32_t timeout;
#ifdef USE_XQSPIPSU
    XQspiPsu_Config *QspiConfig;
#endif

    memset(&mDev, 0, sizeof(mDev));

#ifdef USE_XQSPIPSU
    /* Xilinx BSP Driver */
    QspiConfig = XQspiPsu_LookupConfig(QSPI_DEVICE_ID);
    if (QspiConfig == NULL) {
        wolfBoot_printf("QSPI config lookup failed\n");
        return;
    }
    ret = XQspiPsu_CfgInitialize(&mDev.qspiPsuInst, QspiConfig, QspiConfig->BaseAddress);
    if (ret != 0) {
        wolfBoot_printf("QSPI config init failed\n");
        return;
    }
    XQspiPsu_SetOptions(&mDev.qspiPsuInst, XQSPIPSU_MANUAL_START_OPTION);
    XQspiPsu_SetClkPrescaler(&mDev.qspiPsuInst, QSPI_CLK_PRESACALE);

#elif defined(USE_QNX)
    /* QNX QSPI driver */
    mDev.qnx = xzynq_qspi_open();
    if (mDev.qnx == NULL) {
        wolfBoot_printf("QSPI failed to open\n");
        return;
    }
#else
    /* QSPI bare-metal driver */
    wolfBoot_printf("QSPI Init: Ref=%dMHz, Div=%d, Bus=%d, IO=%s\n",
        GQSPI_CLK_REF/1000000,
        (2 << GQSPI_CLK_DIV),
        (GQSPI_CLK_REF / (2 << GQSPI_CLK_DIV)),
    #ifdef GQSPI_MODE_IO
        "Poll"
    #else
        "DMA"
    #endif
    );

    /* Disable Linear Mode in case FSBL enabled it */
    LQSPI_EN = 0;

    /* Select Generic Quad-SPI */
    GQSPI_SEL = 1;

    /* Clear and disable all interrupts */
    reg_isr = GQSPI_ISR;
    GQSPI_ISR = (reg_isr | GQSPI_ISR_WR_TO_CLR_MASK); /* Clear poll timeout counter interrupt */
    reg_cfg = GQSPIDMA_ISR;
    GQSPIDMA_ISR = reg_cfg; /* clear all active interrupts */
    GQSPI_IER = GQSPI_IXR_GEN_FIFO_EMPTY;
    GQSPI_IDR = GQSPI_IXR_ALL_MASK; /* disable interrupts */
    GQSPIDMA_IDR = GQSPIDMA_ISR_ALL_MASK;

    GQSPI_EN = 0; /* Disable device */

    /* Initialize clock divisor, write protect hold and start mode */
#ifdef GQSPI_MODE_IO
    reg_cfg  = GQSPI_CFG_MODE_EN_IO; /* Use I/O Transfer Mode */
    reg_cfg |= GQSPI_CFG_START_GEN_FIFO; /* Trigger GFIFO commands to start */
#else
    reg_cfg  = GQSPI_CFG_MODE_EN_DMA; /* Use DMA Transfer Mode */
#endif
    reg_cfg |= GQSPI_CFG_BAUD_RATE_DIV(GQSPI_CLK_DIV); /* Clock Divider */
    reg_cfg |= GQSPI_CFG_WP_HOLD; /* Use WP Hold */
    reg_cfg &= ~(GQSPI_CFG_CLK_POL | GQSPI_CFG_CLK_PH); /* Use POL=0,PH=0 */
    GQSPI_CFG = reg_cfg;

#if (GQSPI_CLK_REF / (2 << GQSPI_CLK_DIV)) <= 40000000 /* 40MHz */
    /* At <40 MHz, the Quad-SPI controller should be in non-loopback mode with
     * the clock and data tap delays bypassed. */
    IOU_TAPDLY_BYPASS |= IOU_TAPDLY_BYPASS_LQSPI_RX;
    GQSPI_LPBK_DLY_ADJ = 0;
    GQSPI_DATA_DLY_ADJ = 0;
#elif (GQSPI_CLK_REF / (2 << GQSPI_CLK_DIV)) <= 100000000 /* 100MHz */
    /* At <100 MHz, the Quad-SPI controller should be in clock loopback mode
     * with the clock tap delay bypassed, but the data tap delay enabled. */
    IOU_TAPDLY_BYPASS |= IOU_TAPDLY_BYPASS_LQSPI_RX;
    GQSPI_LPBK_DLY_ADJ = GQSPI_LPBK_DLY_ADJ_USE_LPBK;
    GQSPI_DATA_DLY_ADJ = (GQSPI_DATA_DLY_ADJ_USE_DATA_DLY |
                          GQSPI_DATA_DLY_ADJ_DATA_DLY_ADJ(2));
#elif (GQSPI_CLK_REF / (2 << GQSPI_CLK_DIV)) <= 150000000 /* 150MHz */
    /* At <150 MHz, only the generic controller can be used.
     * The generic controller should be in clock loopback mode and the clock
     * tap delay enabled, but the data tap delay disabled. */
    /* For EL2 or lower must use IOCTL_SET_TAPDELAY_BYPASS ARG1=2, ARG2=0 */
    if (current_el() <= 2) {
        reg_cfg = 0;
        pmu_request(PM_MMIO_WRITE, IOU_TAPDLY_BYPASS_ADDR, 0x7, reg_cfg, 0, NULL);
    }
    else {
        IOU_TAPDLY_BYPASS = 0;
    }
    GQSPI_LPBK_DLY_ADJ = GQSPI_LPBK_DLY_ADJ_USE_LPBK;
    GQSPI_DATA_DLY_ADJ = 0;
#endif

    /* Initialize hardware parameters for Threshold and Interrupts */
    GQSPI_TX_THRESH = 1;
    GQSPI_RX_THRESH = 1;
    GQSPI_GF_THRESH = 31;

    /* Reset DMA */
    GQSPIDMA_CTRL = GQSPIDMA_CTRL_DEF;
    GQSPIDMA_CTRL2 = GQSPIDMA_CTRL2_DEF;
    GQSPIDMA_IER = GQSPIDMA_ISR_ALL_MASK;

    GQSPI_EN = 1; /* Enable Device */
#endif /* USE_QNX */
    (void)reg_cfg;
    (void)reg_isr;

    /* ------ Flash Read ID (retry) ------ */
    timeout = 0;
    while (++timeout < QSPI_FLASH_READY_TRIES) {
        /* Slave Select - lower chip */
        mDev.mode = GQSPI_GEN_FIFO_MODE_SPI;
        mDev.bus = GQSPI_GEN_FIFO_BUS_LOW;
        mDev.cs = GQSPI_GEN_FIFO_CS_LOWER;
        ret = qspi_flash_read_id(&mDev, id_low, sizeof(id_low));
        if (ret != GQSPI_CODE_SUCCESS) {
            continue;
        }

    #if GQPI_USE_DUAL_PARALLEL == 1
        /* Slave Select - upper chip */
        mDev.mode = GQSPI_GEN_FIFO_MODE_SPI;
        mDev.bus = GQSPI_GEN_FIFO_BUS_UP;
        mDev.cs = GQSPI_GEN_FIFO_CS_UPPER;
        ret = qspi_flash_read_id(&mDev, id_hi, sizeof(id_hi));
        if (ret != GQSPI_CODE_SUCCESS) {
            continue;
        }

        /* ID's for upper and lower must match */
        if ((id_hi[0] == 0 || id_hi[0] == 0xFF) ||
            (id_hi[0] != id_low[0] &&
            id_hi[1] != id_low[1] &&
            id_hi[2] != id_low[2]))
        {
            wolfBoot_printf("Flash ID error!\n");
            continue;
        }
    #endif
        break; /* success */
    }

    /* Slave Select */
    mDev.mode = GQSPI_QSPI_MODE;
#if GQPI_USE_DUAL_PARALLEL == 1
    mDev.bus = GQSPI_GEN_FIFO_BUS_BOTH; /* GQSPI_GEN_FIFO_BUS_LOW or GQSPI_GEN_FIFO_BUS_UP */
    mDev.cs = GQSPI_GEN_FIFO_CS_BOTH; /* GQSPI_GEN_FIFO_CS_LOWER or GQSPI_GEN_FIFO_CS_UPPER */
    mDev.stripe = GQSPI_GEN_FIFO_STRIPE;
#endif

#if GQPI_USE_4BYTE_ADDR == 1
    /* Enter 4-byte address mode */
    ret = qspi_enter_4byte_addr(&mDev);
    if (ret != GQSPI_CODE_SUCCESS)
        return;
#endif

#ifdef TEST_EXT_FLASH
    test_ext_flash(&mDev);
#endif
}

void hal_delay_ms(uint64_t ms)
{
    uint64_t start = hal_timer_ms();
    uint64_t end = start + ms;

    while (1) {
        uint64_t cur = hal_timer_ms();
        /* check for timer rollover or expiration */
        if (cur < start || cur >= end) {
            break;
        }
    }
}

uint64_t hal_timer_ms(void)
{
    uint64_t val;
    unsigned long cntfrq;
    unsigned long cntpct;
    asm volatile("mrs %0, cntfrq_el0" : "=r" (cntfrq));
    asm volatile("mrs %0, cntpct_el0" : "=r" (cntpct));
    val = cntpct * 1000;
    val /= cntfrq;
    return val;
}

/* public HAL functions */
void hal_init(void)
{
    const char* bootMsg = "\nwolfBoot Secure Boot\n";

#ifdef DEBUG_UART
    uart_init();
#endif
    wolfBoot_printf(bootMsg);
    wolfBoot_printf("Current EL: %d\n", current_el());

    qspi_init();

    pmuVer = pmu_get_version();
    wolfBoot_printf("PMUFW Ver: %d.%d\n",
        (int)(pmuVer >> 16), (int)(pmuVer & 0xFFFF));

#ifdef WOLFBOOT_ZYNQMP_CSU
    if (pmuVer >= PMUFW_MIN_VER) {
        csu_init();
    }
    else {
        wolfBoot_printf("Skipping CSU Init (PMUFW not found)\n");
    }
#endif
}

void hal_prepare_boot(void)
{
#if GQPI_USE_4BYTE_ADDR == 1
    /* Exit 4-byte address mode */
    int ret = qspi_exit_4byte_addr(&mDev);
    if (ret != GQSPI_CODE_SUCCESS)
        return;
#endif

#ifdef USE_QNX
    if (mDev.qnx) {
        xzynq_qspi_close(mDev.qnx);
        mDev.qnx = NULL;
    }
#endif
}

/* Flash functions must be relocated to RAM for execution */
int RAMFUNCTION hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}


int RAMFUNCTION hal_flash_erase(uintptr_t address, int len)
{
    return 0;
}

/* Xilinx Write uses SPI mode and Page Program 0x02 */
/* Issues using write with QSPI mode */
int RAMFUNCTION ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    int ret = 0;
    uint8_t cmd[8]; /* size multiple of uint32_t */
    uint32_t xferSz, page, pages, idx;
    uintptr_t addr;

#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("Flash Write: Addr 0x%x, Ptr %p, Len %d\n",
        address, data, len);
#endif

    /* write by page */
    pages = ((len + (FLASH_PAGE_SIZE-1)) / FLASH_PAGE_SIZE);
    for (page = 0; page < pages; page++) {
        ret = qspi_write_enable(&mDev);
        if (ret != GQSPI_CODE_SUCCESS) {
            break;
        }
        xferSz = len;
        if (xferSz > FLASH_PAGE_SIZE)
            xferSz = FLASH_PAGE_SIZE;

        addr = address + (page * FLASH_PAGE_SIZE);
        if (mDev.stripe) {
            /* For dual parallel the address divide by 2 */
            addr /= 2;
        }

        /* ------ Write Flash (page at a time) ------ */
        memset(cmd, 0, sizeof(cmd));
        idx = 0;
        cmd[idx++] = PAGE_PROG_CMD;
    #if GQPI_USE_4BYTE_ADDR == 1
        cmd[idx++] = ((addr >> 24) & 0xFF);
    #endif
        cmd[idx++] = ((addr >> 16) & 0xFF);
        cmd[idx++] = ((addr >> 8)  & 0xFF);
        cmd[idx++] = ((addr >> 0)  & 0xFF);
        ret = qspi_transfer(&mDev, cmd, idx,
            (const uint8_t*)(data + (page * FLASH_PAGE_SIZE)),
            xferSz, NULL, 0, 0, GQSPI_GEN_FIFO_MODE_SPI);
        wolfBoot_printf("Flash Page %d Write: Ret %d\n", page, ret);
        if (ret != GQSPI_CODE_SUCCESS)
            break;

        ret = qspi_wait_ready(&mDev); /* Wait for not busy */
        if (ret != GQSPI_CODE_SUCCESS) {
            break;
        }
        qspi_write_disable(&mDev);
        len -= xferSz;
    }

    return ret;
}

#if GQSPI_QSPI_MODE == GQSPI_GEN_FIFO_MODE_QSPI && GQPI_USE_4BYTE_ADDR == 1
#define FLASH_READ_CMD QUAD_READ_4B_CMD
#elif GQSPI_QSPI_MODE == GQSPI_GEN_FIFO_MODE_DSPI && GQPI_USE_4BYTE_ADDR == 1
#define FLASH_READ_CMD DUAL_READ_4B_CMD
#elif GQPI_USE_4BYTE_ADDR == 1
#define FLASH_READ_CMD FAST_READ_4B_CMD
#elif GQSPI_QSPI_MODE == GQSPI_GEN_FIFO_MODE_QSPI
#define FLASH_READ_CMD QUAD_READ_CMD
#elif GQSPI_QSPI_MODE == GQSPI_GEN_FIFO_MODE_DSPI
#define FLASH_READ_CMD DUAL_READ_CMD
#else
#define FLASH_READ_CMD FAST_READ_CMD
#endif

int RAMFUNCTION ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    int ret;
    uint8_t cmd[8]; /* size multiple of uint32_t */
    uint32_t idx = 0;

#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("Flash Read: Addr 0x%x, Ptr %p, Len %d\n",
        address, data, len);
#endif

    if (mDev.stripe) {
        /* For dual parallel the address divide by 2 */
        address /= 2;
    }

    /* ------ Read Flash ------ */
    memset(cmd, 0, sizeof(cmd));
    cmd[idx++] = FLASH_READ_CMD;
#if GQPI_USE_4BYTE_ADDR == 1
    cmd[idx++] = ((address >> 24) & 0xFF);
#endif
    cmd[idx++] = ((address >> 16) & 0xFF);
    cmd[idx++] = ((address >> 8)  & 0xFF);
    cmd[idx++] = ((address >> 0)  & 0xFF);
    ret = qspi_transfer(&mDev, cmd, idx, NULL, 0, data, len, GQSPI_DUMMY_READ,
        mDev.mode);
#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("Flash Read: Ret %d\r\n", ret);
#endif

    return (ret == 0) ? len : ret;
}

/* Issues a sector erase based on flash address */
int RAMFUNCTION ext_flash_erase(uintptr_t address, int len)
{
    int ret = 0;
    uint8_t cmd[8]; /* size multiple of uint32_t */
    uint32_t idx = 0;
    uintptr_t qspiaddr;

#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("Flash Erase: Addr 0x%x, Len %d\n",  address, len);
#endif

    while (len > 0) {
        /* For dual parallel the address divide by 2 */
        qspiaddr = (mDev.stripe) ? address / 2 : address;

        ret = qspi_write_enable(&mDev);
        if (ret == GQSPI_CODE_SUCCESS) {
            /* ------ Erase Flash ------ */
            memset(cmd, 0, sizeof(cmd));
            cmd[idx++] = SEC_ERASE_CMD;
        #if GQPI_USE_4BYTE_ADDR == 1
            cmd[idx++] = ((qspiaddr >> 24) & 0xFF);
        #endif
            cmd[idx++] = ((qspiaddr >> 16) & 0xFF);
            cmd[idx++] = ((qspiaddr >> 8)  & 0xFF);
            cmd[idx++] = ((qspiaddr >> 0)  & 0xFF);
            ret = qspi_transfer(&mDev, cmd, idx, NULL, 0, NULL, 0, 0,
                GQSPI_GEN_FIFO_MODE_SPI);
            wolfBoot_printf("Flash Erase: Ret %d\n", ret);
            if (ret == GQSPI_CODE_SUCCESS) {
                ret = qspi_wait_ready(&mDev); /* Wait for not busy */
            }
            qspi_write_disable(&mDev);
        }

        address += WOLFBOOT_SECTOR_SIZE;
        len -= WOLFBOOT_SECTOR_SIZE;
    }

    return ret;
}

void RAMFUNCTION ext_flash_lock(void)
{

}

void RAMFUNCTION ext_flash_unlock(void)
{

}

#ifdef MMU
void* hal_get_dts_address(void)
{
    return (void*)WOLFBOOT_DTS_BOOT_ADDRESS;
}

int hal_dts_fixup(void* dts_addr)
{
    /* place FDT fixup specific to ZynqMP here */
    //fdt_set_boot_cpuid_phys(buf, fdt_boot_cpuid_phys(fdt));
    return 0;
}
#endif


#ifdef TEST_EXT_FLASH
#ifndef TEST_EXT_ADDRESS
#define TEST_EXT_ADDRESS 0x2800000 /* 40MB */
#endif
static int test_ext_flash(QspiDev_t* dev)
{
    int ret;
    uint32_t i;
    uint8_t pageData[FLASH_PAGE_SIZE*4];

#ifndef TEST_FLASH_READONLY
    /* Erase sector */
    ret = ext_flash_erase(TEST_EXT_ADDRESS, WOLFBOOT_SECTOR_SIZE);
    wolfBoot_printf("Erase Sector: Ret %d\n", ret);

    /* Write Pages */
    for (i=0; i<sizeof(pageData); i++) {
        pageData[i] = (i & 0xff);
    }
    ret = ext_flash_write(TEST_EXT_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Write Page: Ret %d\n", ret);
#endif /* !TEST_FLASH_READONLY */

    /* Read page */
    memset(pageData, 0, sizeof(pageData));
    ret = ext_flash_read(TEST_EXT_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Read Page: Ret %d\n", ret);

    wolfBoot_printf("Checking...\n");
    /* Check data */
    for (i=0; i<sizeof(pageData); i++) {
        wolfBoot_printf("check[%3d] %02x\n", i, pageData[i]);
        if (pageData[i] != (i & 0xff)) {
            wolfBoot_printf("Check Data @ %d failed\n", i);
            return GQSPI_CODE_FAILED;
        }
    }

    wolfBoot_printf("Flash Test Passed\n");
    return ret;
}
#endif /* TEST_EXT_FLASH */

#endif /* TARGET_zynq */
