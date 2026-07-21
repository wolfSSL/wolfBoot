/* zynq.c
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
#include "hal_fpga.h"

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

/* DTB fixup for kernel command line. Override LINUX_BOOTARGS or
 * LINUX_BOOTARGS_ROOT in your config to customize.
 *
 * Note: console=ttyPS0 is ZynqMP-specific (PS UART0). Versal's default
 * (hal/versal.c) omits the console= token because Versal relies on
 * earlycon alone plus a DT-declared stdout-path. */
#ifndef LINUX_BOOTARGS
#ifndef LINUX_BOOTARGS_ROOT
#define LINUX_BOOTARGS_ROOT "/dev/mmcblk0p4"
#endif
#define LINUX_BOOTARGS \
    "earlycon console=ttyPS0,115200 root=" LINUX_BOOTARGS_ROOT " rootwait"
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
#ifndef WOLFBOOT_ZYNQMP_FSBL
/* PMU firmware version, queried over SMC to ARM-TF. Not available when wolfBoot
 * is itself the FSBL running at EL3 (no ATF below). */
static uint32_t pmuVer;
#define PMUFW_MIN_VER 0x10001 /* v1.1*/
#endif

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
#define PM_MMIO_WRITE      0x13
#define PM_MMIO_READ       0x14

/* FPGA / PL programming (xilfpga via PMU firmware / TF-A) */
#define PM_FPGA_LOAD       0x16 /* 22 */
#define PM_FPGA_GET_STATUS 0x17 /* 23 */
/* pm_fpga_load flags (bit 0 selects full vs partial bitstream) */
#define XFPGA_FULLBIT_EN   0x0
#define XFPGA_PARTIAL_EN   0x1

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
    uint64_t src;        /* address of data buffer */
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

#if defined(WOLFBOOT_ZYNQMP_FSBL) && defined(WOLFBOOT_ZYNQMP_PM_CFG)
/* Direct APU->PMU IPI transport for loading the PMU configuration object.
 *
 * pmu_request() above talks to the PMU over an SMC to the ARM Trusted Firmware
 * SIP service. When wolfBoot is itself the FSBL running at EL3 there is no ATF
 * below it, so the SMC path is unavailable and we must reach the PMU firmware
 * directly over the APU->PMU IPI channel, mirroring what the Xilinx FSBL does
 * via XPm_SetConfiguration(). See hal/board/zynqmp/pm_cfg_obj.c for the data. */
#define PMU_GLOBAL_CNTRL_REG    0xFFD80000UL
#define PMU_GLOBAL_FW_IS_PRESENT (1UL << 4) /* GLOBAL_CNTRL[FW_IS_PRESENT] */

/* APU IPI block (base 0xFF300000): TRIG at +0x00, OBS at +0x04. The PMU
 * channel-0 request is bit 16 in the APU trigger/observation registers. */
#define IPI_APU_TRIG_REG        0xFF300000UL
#define IPI_APU_OBS_REG         0xFF300004UL
#define IPI_PMU_CH0_MASK        0x00010000UL

/* IPI message RAM (base 0xFF990000). APU(buffer index 2) -> PMU(index 7):
 *   request  = base + 2*0x200 + 7*0x40        = 0xFF9905C0
 *   response = base + 2*0x200 + 7*0x40 + 0x20 = 0xFF9905E0 */
#define IPI_APU_TO_PMU_REQ_BUF  0xFF9905C0UL
#define IPI_PMU_TO_APU_RESP_BUF 0xFF9905E0UL

/* PM API id (payload word 0) for loading the configuration object. */
#define PM_SET_CONFIGURATION    0x02U

#define PM_IPI_POLL_MAX         10000000UL

extern const uint32_t pm_cfg_obj[];
extern const uint32_t pm_cfg_obj_words;

/* Send the PMU configuration object to the PMU firmware so it programs its
 * EEMI access-control table (which masters may control which nodes). Returns
 * the PMU status word (0 == success) or -1 on transport failure. */
int zynqmp_pm_set_configuration(void)
{
    volatile uint32_t* fw_present = (volatile uint32_t*)PMU_GLOBAL_CNTRL_REG;
    volatile uint32_t* req  = (volatile uint32_t*)IPI_APU_TO_PMU_REQ_BUF;
    volatile uint32_t* resp = (volatile uint32_t*)IPI_PMU_TO_APU_RESP_BUF;
    volatile uint32_t* trig = (volatile uint32_t*)IPI_APU_TRIG_REG;
    volatile uint32_t* obs  = (volatile uint32_t*)IPI_APU_OBS_REG;
    uint32_t status;
    uint32_t timeout;

    /* PMU firmware must be running to accept the object. */
    if ((*fw_present & PMU_GLOBAL_FW_IS_PRESENT) == 0) {
        wolfBoot_printf("PM config: PMUFW not present, skipping\n");
        return -1;
    }

    /* The PMU reads the object from memory by address; make sure our copy is
     * visible at the point of coherency before handing over the pointer. */
    flush_dcache_range((unsigned long)&pm_cfg_obj[0],
        (unsigned long)&pm_cfg_obj[0] + sizeof(pm_cfg_obj[0]) * pm_cfg_obj_words);

    /* Wait for the PMU channel to be free (observation bit clear). */
    timeout = PM_IPI_POLL_MAX;
    while ((*obs & IPI_PMU_CH0_MASK) != 0) {
        if (--timeout == 0) {
            wolfBoot_printf("PM config: IPI channel busy\n");
            return -1;
        }
    }

    /* Build the request: [PM_SET_CONFIGURATION, cfg-object address]. The
     * object lives in OCM (< 4GB) so the 32-bit truncation is exact. */
    req[0] = PM_SET_CONFIGURATION;
    req[1] = (uint32_t)(uintptr_t)&pm_cfg_obj[0];
    req[2] = 0;
    req[3] = 0;
    req[4] = 0;
    req[5] = 0;
    req[6] = 0;
    __asm__ volatile("dsb sy" ::: "memory");

    /* Trigger the IPI to PMU channel 0. */
    *trig = IPI_PMU_CH0_MASK;
    __asm__ volatile("dsb sy" ::: "memory");

    /* Wait for the PMU to acknowledge (clears the observation bit). */
    timeout = PM_IPI_POLL_MAX;
    while ((*obs & IPI_PMU_CH0_MASK) != 0) {
        if (--timeout == 0) {
            wolfBoot_printf("PM config: no PMUFW ack\n");
            return -1;
        }
    }

    /* Response word 0 holds the PMU status (0 == XST_SUCCESS). */
    status = resp[0];
    wolfBoot_printf("PM config: PMUFW status 0x%x (%d words)\n",
        (unsigned int)status, (int)pm_cfg_obj_words);
    return (int)status;
}
#endif /* WOLFBOOT_ZYNQMP_FSBL && WOLFBOOT_ZYNQMP_PM_CFG */

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

/* PMU-mediated MMIO. As the FSBL wolfBoot runs at EL3 with no ATF beneath it,
 * so the SMC/PMU path is unavailable -- access the register directly (the CSU,
 * eFuse and clock/reset regs these are used for are EL3-accessible, matching
 * what the Xilinx FSBL does). Otherwise (wolfBoot as BL33 above ATF) go through
 * the PM_MMIO_READ/WRITE SMC so the PMU enforces access control. */
uint32_t pmu_mmio_read(uint32_t addr)
{
#if defined(WOLFBOOT_ZYNQMP_FSBL)
    return *((volatile uint32_t*)(uintptr_t)addr);
#else
    uint32_t ret_payload[PM_ARGS_CNT];
    memset(ret_payload, 0, sizeof(ret_payload));
    pmu_request(PM_MMIO_READ, addr, 0, 0, 0, ret_payload);
    return ret_payload[1];
#endif
}

uint32_t pmu_mmio_writemask(uint32_t addr, uint32_t mask, uint32_t val)
{
#if defined(WOLFBOOT_ZYNQMP_FSBL)
    volatile uint32_t* reg = (volatile uint32_t*)(uintptr_t)addr;
    *reg = (*reg & ~mask) | (val & mask);
    return 0;
#else
    uint32_t ret_payload[PM_ARGS_CNT];
    memset(ret_payload, 0, sizeof(ret_payload));
    pmu_request(PM_MMIO_WRITE, addr, mask, val, 0, ret_payload);
    return ret_payload[0]; /* 0=Success, 30=No Access */
#endif
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

#ifdef WOLFBOOT_ZYNQMP_FSBL_SEC
/* Read-only dump of the device eFuse cache (loaded by the BootROM at power-on)
 * directly from the eFuse controller. As the FSBL wolfBoot is at EL3, so the
 * cache registers are read via pmu_mmio_read(), which is direct MMIO here (no
 * PMU/ATF). No eFuse programming is performed. */
void zynqmp_efuse_dump(void)
{
    uint32_t status, sec, chash, aux;
    int i;

    status = pmu_mmio_read(ZYNQMP_EFUSE_STATUS);
    if ((status & ZYNQMP_EFUSE_STATUS_CACHE_DONE) == 0) {
        wolfBoot_printf("eFuse: cache not loaded (STATUS 0x%08x)\n", status);
        return;
    }

    sec   = pmu_mmio_read(ZYNQMP_EFUSE_SEC_CTRL);
    chash = pmu_mmio_read(ZYNQMP_EFUSE_PUF_CHASH);
    aux   = pmu_mmio_read(ZYNQMP_EFUSE_PUF_AUX);

    wolfBoot_printf("eFuse SEC_CTRL 0x%08x:%s%s%s%s%s%s\n", sec,
        (sec & ZYNQMP_EFUSE_SEC_CTRL_RSA_EN)     ? " RSA_EN"       : "",
        (sec & ZYNQMP_EFUSE_SEC_CTRL_ENC_ONLY)   ? " ENC_ONLY"     : "",
        (sec & ZYNQMP_EFUSE_SEC_CTRL_JTAG_DIS)   ? " JTAG_DIS"     : "",
        (sec & ZYNQMP_EFUSE_SEC_CTRL_PPK0_INVLD) ? " PPK0_REVOKED" : "",
        (sec & ZYNQMP_EFUSE_SEC_CTRL_PPK0_WRLK)  ? " PPK0_WRLK"    : "",
        (sec & ZYNQMP_EFUSE_SEC_CTRL_AES_RDLK)   ? " AES_RDLK"     : "");
    wolfBoot_printf("eFuse PUF CHASH 0x%08x AUX 0x%08x\n", chash, aux);

    wolfBoot_printf("eFuse PPK0 hash:");
    for (i = 0; i < 12; i++) {
        wolfBoot_printf(" %08x",
            pmu_mmio_read(ZYNQMP_EFUSE_PPK0_0 + (uint32_t)(i * 4)));
    }
    wolfBoot_printf("\n");
}
#endif /* WOLFBOOT_ZYNQMP_FSBL_SEC */

/* CSU engine access (eFuse/PUF/AES/DMA + SHA3 HAL). Compiled when the CSU is
 * the hash provider (WOLFBOOT_ZYNQMP_CSU, HW_SHA3=1) OR for the FSBL security
 * features (WOLFBOOT_ZYNQMP_FSBL_SEC). The CSU SHA3 HAL below is CSU-only: in
 * FSBL_SEC builds hashing stays in software, so the HAL must not be compiled
 * (it would multiply-define wc_Sha3_384_*). PUF/AES/DMA reach the CSU via
 * pmu_mmio_read/write, which is direct MMIO in FSBL mode. */
#if defined(WOLFBOOT_ZYNQMP_CSU) || defined(WOLFBOOT_ZYNQMP_FSBL_SEC)

/* wc_ForceZero() for scrubbing decrypted plaintext on CSU AES auth failure */
#include <wolfssl/wolfcrypt/memory.h>

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
#endif /* WOLFBOOT_HASH_SHA3_384 */
#endif /* WOLFBOOT_ZYNQMP_CSU (SHA3 HAL) */

/* CSU PUF */
#if defined(CSU_PUF_ROT) || defined(WOLFBOOT_ZYNQMP_FSBL_SEC)
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
#endif /* CSU_PUF_ROT || WOLFBOOT_ZYNQMP_FSBL_SEC */

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

/* AES-GCM engine sizes (Xilinx CSU): 16-byte IV block and 16-byte GCM tag. */
#define CSU_AES_IV_SZ      16
#define CSU_AES_GCM_TAG_SZ 16
/* AES-GCM with a selectable key source. keySrc = CSU_AES_KEY_SRC_KUP (user key
 * from kupKey, 32 bytes) or CSU_AES_KEY_SRC_DEVICE_KEY (kupKey ignored).
 *   Encrypt: in = plaintext (sz),          out = ciphertext||tag (sz+16).
 *   Decrypt: in = ciphertext||tag (sz+16), out = plaintext (sz); the CSU GCM
 *            tag is enforced (GCM_TAG_PASS). */
int csu_aes_ex(int enc, const uint8_t* iv, const uint8_t* in, uint8_t* out,
    uint32_t sz, int keySrc, const uint8_t* kupKey)
{
    int ret;
    uint32_t i, status;
    /* Buffer sizes are direction-dependent: encrypt in=plaintext (sz),
     * out=ciphertext||tag (sz+16); decrypt in=ciphertext||tag (sz+16),
     * out=plaintext (sz). Flushing only the real extents avoids a dc civac
     * past the buffer (which can fault at EL3 at a mapped-region edge). */
    uint32_t in_sz  = (enc == CSU_AES_CFG_ENC) ? sz : sz + CSU_AES_GCM_TAG_SZ;
    uint32_t out_sz = (enc == CSU_AES_CFG_ENC) ? sz + CSU_AES_GCM_TAG_SZ : sz;

    /* Clean inputs and the output region to the point of coherency for the
     * non-coherent CSU DMA. */
    flush_dcache_range((unsigned long)iv, (unsigned long)iv + CSU_AES_IV_SZ);
    flush_dcache_range((unsigned long)in,  (unsigned long)in + in_sz);
    flush_dcache_range((unsigned long)out, (unsigned long)out + out_sz);

    /* Configure SSS for DMA <-> AES */
    ret = pmu_mmio_write(CSU_SSS_CFG,
        (CSU_SSS_CFG_AES(CSU_SSS_CFG_SRC_DMA) |
         CSU_SSS_CFG_DMA(CSU_SSS_CFG_SRC_AES)));
    /* Reset AES (set and clear) */
    if (ret == 0)
        ret = csu_aes_reset();
    /* For a user key, load the 32-byte KUP (big-endian words) before key load */
    if (ret == 0 && keySrc == CSU_AES_KEY_SRC_KUP && kupKey != NULL) {
        for (i = 0; i < 8 && ret == 0; i++) {
            uint32_t w = ((uint32_t)kupKey[i*4]   << 24) |
                         ((uint32_t)kupKey[i*4+1] << 16) |
                         ((uint32_t)kupKey[i*4+2] <<  8) |
                         ((uint32_t)kupKey[i*4+3]);
            ret = pmu_mmio_write(CSU_AES_KUP + (i*4), w);
        }
    }
    /* Select and load the AES key */
    if (ret == 0)
        ret = pmu_mmio_write(CSU_AES_KEY_SRC, (uint32_t)keySrc);
    if (ret == 0)
        ret = pmu_mmio_write(CSU_AES_KEY_LOAD, 1);
    if (ret == 0)
        ret = pmu_mmio_wait(CSU_AES_STATUS, CSU_AES_STATUS_KEY_INIT_DONE,
            CSU_AES_STATUS_KEY_INIT_DONE, CSU_AES_TIMEOUT);
    /* Encrypt/decrypt config and byte-swap on both DMA channels */
    if (ret == 0)
        ret = pmu_mmio_write(CSU_AES_CFG, enc);
    if (ret == 0)
        ret = csu_dma_config(CSUDMA_CH_SRC, 1);
    if (ret == 0)
        ret = csu_dma_config(CSUDMA_CH_DST, 1);

    if (enc == CSU_AES_CFG_ENC) {
        /* DST receives ciphertext||tag; SRC sends IV then plaintext (last). */
        if (ret == 0)
            ret = pmu_mmio_write(CSU_AES_START_MSG, 1);
        if (ret == 0)
            ret = csu_dma_transfer(CSUDMA_CH_SRC, (uintptr_t)iv,
                CSU_AES_IV_SZ, 0);
        if (ret == 0)
            ret = csu_dma_wait_done(CSUDMA_CH_SRC);
        if (ret == 0)
            ret = csu_dma_transfer(CSUDMA_CH_DST, (uintptr_t)out,
                sz + CSU_AES_GCM_TAG_SZ, 0);
        if (ret == 0)
            ret = csu_dma_transfer(CSUDMA_CH_SRC, (uintptr_t)in, sz,
                CSUDMA_SIZE_LAST_WORD);
        if (ret == 0)
            ret = csu_dma_wait_done(CSUDMA_CH_SRC);
        if (ret == 0)
            ret = csu_dma_wait_done(CSUDMA_CH_DST);
    }
    else {
        /* DST receives plaintext (sz); SRC sends IV, then the ciphertext and
         * the 16-byte GCM tag (at in+sz). Both the ciphertext and the tag are
         * sent with the last-word flag, matching the Xilinx CSU AES-GCM driver:
         * the ciphertext transfer MUST carry the flag or the engine never
         * finalizes (stays BUSY). HW-validated. */
        if (ret == 0)
            ret = csu_dma_transfer(CSUDMA_CH_DST, (uintptr_t)out, sz, 0);
        if (ret == 0)
            ret = pmu_mmio_write(CSU_AES_START_MSG, 1);
        if (ret == 0)
            ret = csu_dma_transfer(CSUDMA_CH_SRC, (uintptr_t)iv,
                CSU_AES_IV_SZ, 0);
        if (ret == 0 && csu_dma_wait_done(CSUDMA_CH_SRC) != 0)
            ret = -10; /* IV transfer timeout */
        if (ret == 0)
            ret = csu_dma_transfer(CSUDMA_CH_SRC, (uintptr_t)in, sz,
                CSUDMA_SIZE_LAST_WORD);
        if (ret == 0 && csu_dma_wait_done(CSUDMA_CH_SRC) != 0)
            ret = -11; /* ciphertext transfer timeout */
        if (ret == 0)
            ret = csu_dma_transfer(CSUDMA_CH_SRC, (uintptr_t)(in + sz),
                CSU_AES_GCM_TAG_SZ, CSUDMA_SIZE_LAST_WORD);
        if (ret == 0 && csu_dma_wait_done(CSUDMA_CH_SRC) != 0)
            ret = -12; /* GCM tag transfer timeout */
        /* Decrypt output drains with AES-done (below); no DST-channel wait. */
    }

    /* Disable DMA byte swapping */
    if (ret == 0)
        ret = csu_dma_config(CSUDMA_CH_SRC, 0);
    if (ret == 0)
        ret = csu_dma_config(CSUDMA_CH_DST, 0);
    /* Wait for AES done */
    if (ret == 0)
        ret = pmu_mmio_wait(CSU_AES_STATUS, CSU_AES_STATUS_BUSY,
            0, CSU_AES_TIMEOUT);
    /* Invalidate the DMA-written output so the CPU reads fresh data */
    flush_dcache_range((unsigned long)out, (unsigned long)out + out_sz);
    /* On decrypt, enforce the GCM tag check */
    if (ret == 0 && enc == CSU_AES_CFG_DEC) {
        status = pmu_mmio_read(CSU_AES_STATUS);
        if ((status & CSU_AES_STATUS_GCM_TAG_PASS) == 0)
            ret = -3; /* GCM tag mismatch */
    }
    /* Never release unauthenticated plaintext: on any decrypt failure scrub the
     * output buffer. The CSU DMA wrote plaintext to the point of coherency, so
     * zero it and clean the cache back to DRAM (flush_dcache_range is dc civac).
     */
    if (enc == CSU_AES_CFG_DEC && ret != 0) {
        wc_ForceZero(out, out_sz);
        flush_dcache_range((unsigned long)out,
            (unsigned long)out + out_sz);
    }
    return ret;
}

int csu_aes(int enc, const uint8_t* iv, const uint8_t* in, uint8_t* out, uint32_t sz)
{
    return csu_aes_ex(enc, iv, in, out, sz, CSU_AES_KEY_SRC_DEVICE_KEY, NULL);
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
    uint32_t reg1 = pmu_mmio_read(CSU_IDCODE);
    uint32_t reg2 = pmu_mmio_read(CSU_VERSION);

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

    /* PUF-based key wrap (register -> regenerate KEK -> AES-wrap the red key
     * into a black key) is CSU/eFuse based and implemented by the FSBL security
     * phase using csu_puf_register() / csu_puf_regeneration() / csu_aes(). */
#endif

    return ret;
}

#ifdef WOLFBOOT_ZYNQMP_FSBL_SEC
#ifdef WOLFBOOT_ZYNQMP_PUF_SELFTEST
/* PUF register + regenerate self-test (bring-up). Registers the PUF to produce
 * helper data (syndrome + CHASH + AUX), then regenerates the device KEK from
 * it into the CSU key store. Confirming the regenerated KEK matches across a
 * cold boot needs the AES engine (csu_aes); this validates the register/regen
 * sequence itself. The syndrome is held in RAM (static); persisting it to eFuse
 * PUF_SYN is the separate, gated eFuse-write step. */
static uint32_t puf_syndrome[CSU_PUF_SYNDROME_WORDS];
int zynqmp_puf_test(void)
{
    uint32_t chash = 0, aux = 0;
    int ret;

    memset(puf_syndrome, 0, sizeof(puf_syndrome));
    ret = csu_puf_register(puf_syndrome, &chash, &aux);
    wolfBoot_printf("PUF register: ret %d, CHASH 0x%08x AUX 0x%08x\n",
        ret, (unsigned int)chash, (unsigned int)aux);
    if (ret != 0) {
        return ret;
    }

    ret = csu_puf_regeneration(puf_syndrome, chash, aux);
    wolfBoot_printf("PUF regenerate (KEK): ret %d\n", ret);
    return ret;
}
#endif /* WOLFBOOT_ZYNQMP_PUF_SELFTEST */

#ifdef WOLFBOOT_ZYNQMP_AES_SELFTEST
/* AES-GCM self-test with a user (KUP) key. Encrypts a fixed key/IV/plaintext
 * (a known-answer vector) then decrypts it back, enforcing the CSU GCM tag on
 * the decrypt. Prints the ciphertext+tag so it can also be checked against a
 * software AES-256-GCM reference. eFuse-safe (KUP only; no device key, no
 * eFuse access). */
int zynqmp_aes_test(void)
{
    static const uint8_t key[32] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
        0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
    };
    static uint8_t XALIGNED(64) iv[CSU_AES_IV_SZ] = {
        0xca,0xfe,0xba,0xbe,0xfa,0xce,0xdb,0xad,
        0xde,0xad,0xbe,0xef,0x00,0x00,0x00,0x01
    };
    static uint8_t XALIGNED(64) pt[16] = {
        0x54,0x68,0x65,0x20,0x77,0x6f,0x6c,0x66,
        0x42,0x6f,0x6f,0x74,0x20,0x41,0x45,0x53
    };
    static uint8_t XALIGNED(64) ct[16 + CSU_AES_GCM_TAG_SZ];
    static uint8_t XALIGNED(64) dec[16 + CSU_AES_GCM_TAG_SZ];
    uint32_t ctw[8];
    int ret, ok;

    memset(ct, 0, sizeof(ct));
    memset(dec, 0, sizeof(dec));

    ret = csu_aes_ex(CSU_AES_CFG_ENC, iv, pt, ct, 16,
        CSU_AES_KEY_SRC_KUP, key);
    memcpy(ctw, ct, sizeof(ctw));
    wolfBoot_printf("AES-KUP enc: ret %d CT %08x%08x%08x%08x tag %08x%08x%08x%08x\n",
        ret,
        ctw[0], ctw[1], ctw[2], ctw[3],
        ctw[4], ctw[5], ctw[6], ctw[7]);
    if (ret != 0)
        return ret;

    ret = csu_aes_ex(CSU_AES_CFG_DEC, iv, ct, dec, 16,
        CSU_AES_KEY_SRC_KUP, key);
    ok = (memcmp(dec, pt, 16) == 0);
    wolfBoot_printf("AES-KUP dec: ret %d roundtrip %s\n", ret,
        (ret == 0 && ok) ? "PASS" : "FAIL");
    (void)csu_aes_key_zero();
    return (ret == 0 && ok) ? 0 : -1;
}
#endif /* WOLFBOOT_ZYNQMP_AES_SELFTEST */
#endif /* WOLFBOOT_ZYNQMP_FSBL_SEC */

#endif /* WOLFBOOT_ZYNQMP_CSU || WOLFBOOT_ZYNQMP_FSBL_SEC */


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

/* INVALIDATE-only D-cache maintenance (dc ivac) for a DMA-read destination --
 * the correct post-DMA-read operation (matches Xil_DCacheInvalidateRange /
 * dma_unmap DMA_FROM_DEVICE). flush_dcache_range() is dc CIVAC (clean +
 * invalidate); its CLEAN step can write a speculatively-filled cache line back
 * to DDR AFTER the DMA, clobbering the freshly-DMA'd data. Use this instead for
 * the post-DMA step so nothing is ever written back over the DMA result. */
static inline void qspi_dcache_inval(unsigned long start, unsigned long end)
{
    unsigned long a;
    __asm__ volatile("dsb sy" ::: "memory");
    for (a = (start & ~63UL); a < end; a += 64UL) {
        __asm__ volatile("dc ivac, %0" : : "r"(a) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
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
#ifndef GQSPI_MODE_IO
    /* Single-DMA RX path: arm ONE DMA for the whole destination buffer, then
     * issue the gen-FIFO RX (EXP) entries that clock the bytes into that same
     * ongoing DMA so the read streams continuously (matches the Xilinx xqspipsu
     * driver). Requires a DMA-aligned dst and a 4-byte-multiple size; smaller
     * unaligned or odd-length reads fall back to the bounce buffer below. */
    if (ret == GQSPI_CODE_SUCCESS && rxData && rxSz > 0 &&
        (((size_t)rxData & (QQSPI_DMA_ALIGN-1)) == 0) && ((rxSz & 3) == 0)) {
        uint32_t rxbase;
        uint32_t tc, expo, imm;

        rxbase = reg_genfifo & ~(GQSPI_GEN_FIFO_TX | GQSPI_GEN_FIFO_IMM_MASK |
                                 GQSPI_GEN_FIFO_EXP_MASK);
        rxbase |= (GQSPI_GEN_FIFO_RX | GQSPI_GEN_FIFO_DATA_XFER);
        rxbase |= (pDev->stripe & GQSPI_GEN_FIFO_STRIPE);

        /* Arm the DMA once for the whole transfer. */
        GQSPIDMA_ISR = GQSPIDMA_ISR_DONE;
        (void)GQSPIDMA_ISR;
        GQSPIDMA_DST = ((uintptr_t)rxData & 0xFFFFFFFF);
        GQSPIDMA_DST_MSB = (((uintptr_t)rxData >> 32) & 0xFFF);
        GQSPIDMA_SIZE = rxSz;
        GQSPIDMA_IER = GQSPIDMA_ISR_DONE;
#ifndef ZYNQMP_QSPI_COHERENT
        flush_dcache_range((unsigned long)rxData,
            (unsigned long)rxData + rxSz);
#endif

        /* Issue gen-FIFO RX entries to clock all rxSz bytes as the Xilinx
         * xqspipsu driver does: one EXP entry per set bit of the byte count in
         * ASCENDING exponent order (2^8, 2^9, ...), then a final IMM entry for
         * the low-byte remainder. qspi_gen_fifo_write blocks on
         * GEN_FIFO_NOT_FULL; the controller (auto-start, DMA mode) processes the
         * entries into the one DMA. */
        tc = rxSz;
        expo = 8;                /* 2^8 = 256, smallest EXP unit */
        imm = rxSz & 0xFFU;      /* low-byte remainder -> IMM entry */
        while (tc != 0 && ret == GQSPI_CODE_SUCCESS) {
            if (tc & 0x100U) {   /* bit 'expo' of the original byte count */
                ret = qspi_gen_fifo_write(rxbase | GQSPI_GEN_FIFO_EXP_MASK |
                                          GQSPI_GEN_FIFO_IMM(expo));
            }
            tc >>= 1;
            expo++;
        }
        if (ret == GQSPI_CODE_SUCCESS && imm != 0) {
            ret = qspi_gen_fifo_write((rxbase & ~GQSPI_GEN_FIFO_EXP_MASK) |
                                      GQSPI_GEN_FIFO_IMM(imm));
        }

        if (ret == GQSPI_CODE_SUCCESS) {
            if (qspi_dmaisr_wait(GQSPIDMA_ISR_DONE, 0))
                return GQSPI_CODE_TIMEOUT;
            if (qspi_isr_wait(GQSPI_IXR_GEN_FIFO_EMPTY, 0))
                return GQSPI_CODE_TIMEOUT;
            GQSPIDMA_ISR = GQSPIDMA_ISR_DONE;
            (void)GQSPIDMA_ISR;
            GQSPIDMA_STS = GQSPIDMA_STS | GQSPIDMA_STS_WTC; /* clear WTC (W1C) */
#ifndef ZYNQMP_QSPI_COHERENT
            /* DMA is not cache-coherent: INVALIDATE (not clean+invalidate) the
             * destination so the CPU reads the freshly DMA'd data and nothing
             * is written back over it. */
            qspi_dcache_inval((unsigned long)rxData,
                (unsigned long)rxData + rxSz);
#else
            /* Coherent DMA (CCI): a barrier suffices; no maintenance. */
            __asm__ volatile("dsb sy" ::: "memory");
#endif
        }
        rxSz = 0;
    }
#endif

    /* Bounce / I/O fallback: small unaligned or odd-length reads (e.g. flash ID
     * and status). Per-entry transfer through the aligned bounce buffer. */
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

        GQSPIDMA_ISR = GQSPIDMA_ISR_DONE;
        (void)GQSPIDMA_ISR;
        GQSPIDMA_DST = ((uintptr_t)dmarxptr & 0xFFFFFFFF);
        GQSPIDMA_DST_MSB = ((uintptr_t)dmarxptr >> 32);
        GQSPIDMA_SIZE = xferSz;
        GQSPIDMA_IER = GQSPIDMA_ISR_DONE; /* enable DMA done interrupt */
        flush_dcache_range((unsigned long)dmarxptr,
            (unsigned long)dmarxptr + xferSz);
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
        if (qspi_isr_wait(GQSPI_IXR_GEN_FIFO_EMPTY, 0)) {
            return GQSPI_CODE_TIMEOUT;
        }
        GQSPIDMA_ISR = GQSPIDMA_ISR_DONE; /* clear DMA interrupt */
        (void)GQSPIDMA_ISR;               /* read-back: force W1C to post */
        GQSPIDMA_STS = GQSPIDMA_STS | GQSPIDMA_STS_WTC; /* clear WTC (W1C) */
        qspi_dcache_inval((unsigned long)dmarxptr,
            (unsigned long)dmarxptr + xferSz);
        /* adjust xfer sz */
        if (xferSz > rxSz)
            xferSz = rxSz;
        /* copy result if not aligned */
        if (dmarxptr != rxData) {
            memcpy(rxData, dmarxptr, xferSz);
        }
    #endif

        /* offset size and buffer */
        rxSz -= xferSz;
        rxData += xferSz;
    }

    qspi_cs(pDev, 0); /* Deselect Slave */
    /* Wait for the generic FIFO to drain (including the CS-deassert entry just
     * queued) BEFORE disabling the controller. Otherwise GQSPI_EN=0 can tear
     * the controller down while the CS-deassert is still pending, leaving the
     * flash selected / mid-stream, and the NEXT transfer's command+address is
     * mis-clocked -- back-to-back reads then return shifted/zero data (a delay
     * between transfers hid this by giving the FIFO time to finish). This
     * applies in PIO (GQSPI_MODE_IO) mode too: gspi_fifo_rx() returns once the
     * RX data words are drained, but the CS-deassert entry qspi_cs() just
     * queued is still in the generic FIFO. Disabling the controller without
     * waiting tears down mid-stream, so a SINGLE PIO read works but the
     * back-to-back per-chunk reads of a body load mis-clock and hang. */
    (void)qspi_isr_wait(GQSPI_IXR_GEN_FIFO_EMPTY, 0);
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

/* Soft-reset the flash to a known idle state.
 * FSBL / BootROM may leave the flash in an unexpected mode (XIP enabled,
 * 4-byte addr set, auto-boot probing, etc.). Issue RESET_ENABLE (0x66) +
 * RESET_MEMORY (0x99) to bring it back to defaults before first transaction.
 * Per Micron MT25Q datasheet: t_SHSL2 ~ 40 us max after RESET_MEMORY. */
static int qspi_flash_reset(QspiDev_t* dev)
{
    int ret;
    uint8_t cmd[4]; /* size multiple of uint32_t */

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = RESET_ENABLE_CMD;
    /* Reset commands are always issued in single-SPI mode regardless of
     * dev->mode: the flash's current bus mode is unknown at reset time, and
     * the single-SPI opcode is the universal-compatible form. */
    ret = qspi_transfer(dev, cmd, 1, NULL, 0, NULL, 0, 0,
        GQSPI_GEN_FIFO_MODE_SPI);
#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("Flash Reset Enable: Ret %d\n", ret);
#endif
    if (ret == GQSPI_CODE_SUCCESS) {
        cmd[0] = RESET_MEMORY_CMD;
        ret = qspi_transfer(dev, cmd, 1, NULL, 0, NULL, 0, 0,
            GQSPI_GEN_FIFO_MODE_SPI);
    #if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
        wolfBoot_printf("Flash Reset Memory: Ret %d\n", ret);
    #endif
    }
    /* Allow flash time to complete the reset and become ready. */
    hal_delay_ms(1);
    return ret;
}

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
    /* IOU_TAPDLY_BYPASS is not writable from EL2/EL1 without going through PMU. */
    if (current_el() <= 2) {
        pmu_request(PM_MMIO_WRITE, IOU_TAPDLY_BYPASS_ADDR,
                    IOU_TAPDLY_BYPASS_LQSPI_RX, IOU_TAPDLY_BYPASS_LQSPI_RX,
                    0, NULL);
    }
    else {
        IOU_TAPDLY_BYPASS |= IOU_TAPDLY_BYPASS_LQSPI_RX;
    }
    GQSPI_LPBK_DLY_ADJ = 0;
    GQSPI_DATA_DLY_ADJ = 0;
#elif (GQSPI_CLK_REF / (2 << GQSPI_CLK_DIV)) <= 100000000 /* 100MHz */
    /* At <100 MHz, the Quad-SPI controller should be in clock loopback mode
     * with the clock tap delay bypassed, but the data tap delay enabled. */
    /* IOU_TAPDLY_BYPASS is not writable from EL2/EL1 without going through PMU. */
    if (current_el() <= 2) {
        pmu_request(PM_MMIO_WRITE, IOU_TAPDLY_BYPASS_ADDR,
                    IOU_TAPDLY_BYPASS_LQSPI_RX, IOU_TAPDLY_BYPASS_LQSPI_RX,
                    0, NULL);
    }
    else {
        IOU_TAPDLY_BYPASS |= IOU_TAPDLY_BYPASS_LQSPI_RX;
    }
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

#if defined(DEBUG_ZYNQ)
    /* Verify the QSPI clock + tap-delay assumptions: QSPI_REF_CTRL gives the
     * actual ref-clock divisors (banner "Ref=125MHz" is only an assumption);
     * read back the tap registers to confirm the EL3 writes actually landed. */
    wolfBoot_printf("QSPI clk: REF_CTRL=0x%x TAPDLY=0x%x LPBK=0x%x DATADLY=0x%x\n",
        (uint32_t)QSPI_REF_CTRL, (uint32_t)IOU_TAPDLY_BYPASS,
        (uint32_t)GQSPI_LPBK_DLY_ADJ, (uint32_t)GQSPI_DATA_DLY_ADJ);
#endif

    /* Initialize hardware parameters for Threshold and Interrupts */
    GQSPI_TX_THRESH = 1;
    GQSPI_RX_THRESH = 1;
    GQSPI_GF_THRESH = 31;

    /* Reset DMA by writing only DST_CTRL, like the Xilinx xqspipsu FSBL driver
     * and the Linux spi-zynqmp-gqspi driver. Do not write QSPIDMA_DST_CTRL2
     * (0x824): its AWCACHE and RAM_EMASA/EMASB FIFO-RAM timing-margin bits
     * override the silicon's margins and corrupt sustained DMA transfers. */
    GQSPIDMA_CTRL = GQSPIDMA_CTRL_DEF;
#if defined(DEBUG_ZYNQ)
    wolfBoot_printf("GQSPIDMA CTRL2 (left by boot): 0x%x\n", GQSPIDMA_CTRL2);
#endif
    /* Clear the DMA Write-Transfer-Count (DST_STS.WTC, W1C), as the Linux/Xilinx
     * drivers do. WTC is a saturating count of issued DMA write transfers; if it
     * is not cleared it saturates after the first transfer and the QSPIDMA
     * mis-handles every subsequent transfer. */
    GQSPIDMA_STS = GQSPIDMA_STS | GQSPIDMA_STS_WTC;
    GQSPIDMA_IER = GQSPIDMA_ISR_ALL_MASK;

    GQSPI_EN = 1; /* Enable Device */
#endif /* USE_QNX */
    (void)reg_cfg;
    (void)reg_isr;

    /* Issue flash soft reset so we start from a known state regardless of
     * whatever mode FSBL/BootROM left the device in. Send to each chip in
     * dual-parallel configurations by targeting both chip selects. */
    mDev.mode = GQSPI_GEN_FIFO_MODE_SPI;
    mDev.bus = GQSPI_GEN_FIFO_BUS_LOW;
    mDev.cs = GQSPI_GEN_FIFO_CS_LOWER;
    (void)qspi_flash_reset(&mDev);
#if GQPI_USE_DUAL_PARALLEL == 1
    mDev.bus = GQSPI_GEN_FIFO_BUS_UP;
    mDev.cs = GQSPI_GEN_FIFO_CS_UPPER;
    (void)qspi_flash_reset(&mDev);
#endif

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

#ifdef WOLFBOOT_ZYNQMP_FSBL
/* wolfBoot's psu_init wrapper (hal/zynqmp_psu_shim.c): runs the board
 * (XSA-generated) psu_init sub-stages but fixes the system timestamp counter
 * to 100MHz after clock init and before DDR training, so the training settle
 * delays are accurate. Returns 0 on success. */
extern int zynqmp_psu_init(void);
/* Board file's complete psu_init() (the one the Xilinx FSBL uses). Selectable
 * for A/B testing against our wrapper via -DZYNQMP_USE_BOARD_PSU_INIT. */
extern int psu_init(void);
#endif

#if defined(WOLFBOOT_ZYNQMP_FSBL) && defined(DEBUG_UART)
/* Print the early boot timer/clock and DDR PHY-training state captured by
 * psu_init. All are plain MMIO reads (safe even if DDR itself is marginal).
 * DDR PHY PGSR0 ERR bits [29:20] nonzero => cold-boot DDR training failed. */
static void zynqmp_fsbl_dbg_dump(void)
{
    extern unsigned long zynqmp_dbg_cntfrq_boot, zynqmp_dbg_cntfrq_used;
    extern unsigned int zynqmp_dbg_ts_ctrl, zynqmp_dbg_iopll_ctrl;
    volatile unsigned int* DDRC_STAT = (volatile unsigned int*)0xFD070004UL;
    volatile unsigned int* PHY_PGSR0 = (volatile unsigned int*)0xFD080030UL;
    volatile unsigned int* PHY_PGSR1 = (volatile unsigned int*)0xFD080034UL;
    volatile unsigned int* PHY_PGCR0 = (volatile unsigned int*)0xFD080010UL;
    volatile unsigned int* DPLL_CTRL = (volatile unsigned int*)0xFD1A002CUL;
    volatile unsigned int* DPLL_CFG  = (volatile unsigned int*)0xFD1A0030UL;
    volatile unsigned int* PLL_STAT  = (volatile unsigned int*)0xFD1A0044UL;
    volatile unsigned int* DDR_CTRL  = (volatile unsigned int*)0xFD1A0080UL;
    unsigned int pgsr0 = *PHY_PGSR0;

    wolfBoot_printf("Timer: CNTFRQ boot=%d used=%d\n",
        (int)zynqmp_dbg_cntfrq_boot, (int)zynqmp_dbg_cntfrq_used);
    wolfBoot_printf("Clk: TS_REF_CTRL=0x%x IOPLL_CTRL=0x%x\n",
        zynqmp_dbg_ts_ctrl, zynqmp_dbg_iopll_ctrl);
    wolfBoot_printf("DDR: STAT=0x%x PGSR0=0x%x PGSR1=0x%x PGCR0=0x%x\n",
        *DDRC_STAT, pgsr0, *PHY_PGSR1, *PHY_PGCR0);
    wolfBoot_printf("DDR: PHY train %s (ERR mask 0x%x)\n",
        (pgsr0 & 0x3FF00000U) ? "FAIL" : "ok", (pgsr0 & 0x3FF00000U));
    wolfBoot_printf("DDR clk: DPLL_CTRL=0x%x DPLL_CFG=0x%x PLL_STATUS=0x%x "
        "DDR_CTRL=0x%x\n", *DPLL_CTRL, *DPLL_CFG, *PLL_STAT, *DDR_CTRL);
}
#endif /* WOLFBOOT_ZYNQMP_FSBL && DEBUG_UART */

#ifdef ZYNQMP_ENABLE_CCI
/* Enable CCI-400 snoop + DVM on the APU ACE slave interfaces (S3/S4) so the A53
 * cluster's inner-shareable cacheable traffic reaches DDR coherently. BL31
 * normally does this; as the FSBL replacement wolfBoot must, before any large
 * cacheable DDR access (e.g. the integrity-check SHA). GPV base 0xFD6E0000;
 * SnoopCtrl at 0x1000*(n+1); Status[0] is the change-pending bit. */
static void zynqmp_enable_cci(void)
{
    volatile unsigned int* CCI_STAT = (volatile unsigned int*)0xFD6E0010UL;
    volatile unsigned int* CCI_S3   = (volatile unsigned int*)0xFD6E4000UL;
    volatile unsigned int* CCI_S4   = (volatile unsigned int*)0xFD6E5000UL;

    *CCI_S3 = 0x00000003U;                 /* snoop enable | DVM enable */
    while ((*CCI_STAT & 0x1U) != 0U) {
        /* wait change complete */
    }
    *CCI_S4 = 0x00000003U;
    while ((*CCI_STAT & 0x1U) != 0U) {
        /* wait change complete */
    }
    wolfBoot_printf("CCI: snoop+DVM enabled on S3/S4\n");
}
#endif /* ZYNQMP_ENABLE_CCI */

#ifdef WOLFBOOT_ZYNQMP_PHY_INIT
/* Minimal GEM MDIO management engine, used only to run a board-specific PHY
 * init sequence before boot. See hal/zynq.h for the register/config macros. */

static int gem_mdio_wait_idle(void)
{
    uint32_t spin;
    for (spin = 0; spin < 100000; spin++) {
        if (GEM_NWSR & GEM_NWSR_PHY_IDLE) {
            return 0;
        }
    }
    return -1;
}

static int gem_mdio_write(uint8_t phy, uint8_t reg, uint16_t val)
{
    if (gem_mdio_wait_idle() != 0) {
        return -1;
    }
    GEM_PHYMNTNC = GEM_PHYMNTNC_CLAUSE22 | GEM_PHYMNTNC_OP_W
        | (((uint32_t)phy & 0x1FU) << 23)
        | (((uint32_t)reg & 0x1FU) << 18)
        | (uint32_t)val;
    if (gem_mdio_wait_idle() != 0) {
        return -2;
    }
    return 0;
}

static int gem_mdio_read(uint8_t phy, uint8_t reg, uint16_t *out)
{
    if (gem_mdio_wait_idle() != 0) {
        return -1;
    }
    GEM_PHYMNTNC = GEM_PHYMNTNC_CLAUSE22 | GEM_PHYMNTNC_OP_R
        | (((uint32_t)phy & 0x1FU) << 23)
        | (((uint32_t)reg & 0x1FU) << 18);
    if (gem_mdio_wait_idle() != 0) {
        return -2;
    }
    *out = (uint16_t)(GEM_PHYMNTNC & 0xFFFFU);
    return 0;
}

/* Run the configurable PHY init sequence (default: the board's U-Boot flow).
 * Best-effort: MDIO errors are reported but never block boot. */
static void zynq_phy_init(void)
{
    static const struct {
        uint8_t  op;
        uint8_t  arg0;
        uint16_t arg1;
    } steps[] = { ZYNQMP_PHY_INIT_STEPS };
    uint32_t i;
    uint16_t rval;
    int ret;

    wolfBoot_printf("ZynqMP PHY init (addr 0x%02x)\n", (int)ZYNQMP_PHY_ADDR);

    /* Enable the MDIO management port with a safe MDC divisor. */
    GEM_NWCFG = (GEM_NWCFG & ~(0x7UL << GEM_NWCFG_MDCDIV_SHIFT))
        | (((uint32_t)ZYNQMP_GEM_MDC_DIV & 0x7U) << GEM_NWCFG_MDCDIV_SHIFT);
    GEM_NWCTRL |= GEM_NWCTRL_MDEN;

    for (i = 0; i < (sizeof(steps) / sizeof(steps[0])); i++) {
        switch (steps[i].op) {
        case ZYNQMP_PHY_OP_GPIO:
            if (ZYNQMP_PHY_GPIO_ADDR != 0) {
                *((volatile uint32_t*)ZYNQMP_PHY_GPIO_ADDR) =
                    (uint32_t)steps[i].arg1;
                hal_delay_ms(1);
            }
            break;
        case ZYNQMP_PHY_OP_WR:
            ret = gem_mdio_write(ZYNQMP_PHY_ADDR, steps[i].arg0, steps[i].arg1);
            if (ret != 0) {
                wolfBoot_printf("PHY write reg 0x%02x failed (%d)\n",
                    (int)steps[i].arg0, ret);
            }
            break;
        case ZYNQMP_PHY_OP_RD:
            rval = 0;
            ret = gem_mdio_read(ZYNQMP_PHY_ADDR, steps[i].arg0, &rval);
            if (ret != 0) {
                wolfBoot_printf("PHY read reg 0x%02x failed (%d)\n",
                    (int)steps[i].arg0, ret);
            }
            else {
                wolfBoot_printf("PHY reg 0x%02x = 0x%04x\n",
                    (int)steps[i].arg0, (int)rval);
            }
            break;
        default:
            break;
        }
    }
}
#endif /* WOLFBOOT_ZYNQMP_PHY_INIT */

/* public HAL functions */
void hal_init(void)
{
    const char* bootMsg = "\nwolfBoot Secure Boot\n";

#ifdef WOLFBOOT_ZYNQMP_FSBL
    /* wolfBoot is the FSBL: bring up the PLLs, DDR, MIO mux and clocks before
     * any DDR, UART, QSPI or SD access. Until this runs only the OCM (where
     * wolfBoot executes) and the system counter are available. */
#ifdef ZYNQMP_USE_BOARD_PSU_INIT
    (void)psu_init();        /* board's complete psu_init (FSBL's) */
#else
    (void)zynqmp_psu_init(); /* our wrapper */
#endif
#endif

#ifdef DEBUG_UART
    uart_init();
#endif
    wolfBoot_printf(bootMsg);
    wolfBoot_printf("Current EL: %d\n", current_el());

#if defined(WOLFBOOT_ZYNQMP_FSBL) && defined(DEBUG_UART)
    zynqmp_fsbl_dbg_dump();
#endif

#ifdef ZYNQMP_ENABLE_CCI
    zynqmp_enable_cci();
#endif

#ifndef WOLFBOOT_REPRODUCIBLE_BUILD
    wolfBoot_printf("Build: %s %s\n", __DATE__, __TIME__);
#endif

#ifdef WOLFBOOT_ZYNQMP_PHY_INIT
    zynq_phy_init();
#endif

#if defined(EXT_FLASH) && (EXT_FLASH == 1)
    qspi_init();
#endif

#ifndef WOLFBOOT_ZYNQMP_FSBL
    /* pmu_get_version()/csu_init() query the PMU firmware over an SMC to the
     * EL3 SIP service provided by ARM Trusted Firmware. When wolfBoot itself
     * is the FSBL running at EL3 there is no ATF below it, so these are skipped
     * (the QSPI tap-delay path in qspi_init() already falls back to direct
     * MMIO when current_el() > 2). */
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
#endif /* !WOLFBOOT_ZYNQMP_FSBL */

#if defined(WOLFBOOT_ZYNQMP_FSBL) && defined(WOLFBOOT_ZYNQMP_PM_CFG)
    /* As the FSBL, hand the PMU firmware its configuration object so it grants
     * the APU access to the SoC power/clock/reset/peripheral nodes. Without
     * this the downstream Linux drivers fail probe with -EACCES. */
    (void)zynqmp_pm_set_configuration();
#endif

#if defined(WOLFBOOT_ZYNQMP_FSBL) && defined(WOLFBOOT_ZYNQMP_FSBL_SEC)
    zynqmp_efuse_dump();
#ifdef WOLFBOOT_ZYNQMP_PUF_SELFTEST
    (void)zynqmp_puf_test();
#endif
#ifdef WOLFBOOT_ZYNQMP_AES_SELFTEST
    (void)zynqmp_aes_test();
#endif
#endif
}

void hal_prepare_boot(void)
{
#if defined(EXT_FLASH) && (EXT_FLASH == 1) && GQPI_USE_4BYTE_ADDR == 1
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

    /* Clean and invalidate caches for the loaded application.
     * The application was written to RAM via D-cache, but the CPU will
     * fetch instructions via I-cache from main memory. We must:
     * 1. Clean D-cache (flush dirty data to memory)
     * 2. Invalidate I-cache (ensure fresh instruction fetch) */
    {
        uintptr_t addr;
        uintptr_t end = WOLFBOOT_LOAD_ADDRESS + APP_CACHE_FLUSH_SIZE;
        for (addr = WOLFBOOT_LOAD_ADDRESS; addr < end; addr += CACHE_LINE_SIZE) {
            __asm__ volatile("dc civac, %0" : : "r"(addr) : "memory");
        }
    }
    __asm__ volatile("dsb sy" : : : "memory");
    __asm__ volatile("ic iallu" : : : "memory");
    __asm__ volatile("dsb sy" : : : "memory");
    __asm__ volatile("isb" : : : "memory");
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

#ifdef WOLFBOOT_FPGA_BITSTREAM
/* Program the PL by handing the bitstream to the PMU firmware (xilfpga)
 * via the PM_FPGA_LOAD EEMI call. The bitstream must be a bootgen .bin
 * resident in DDR; it is flushed from the D-cache so the CSU DMA sees
 * the committed bytes. */
int hal_fpga_load(uint32_t flags, uintptr_t addr, size_t size)
{
    uint32_t ret_payload[PM_ARGS_CNT];
    uint32_t pmflags;

    /* size is passed to the PMU as a 32-bit word below. On AArch64 size_t is
     * 64-bit, so reject anything that would truncate. A real bitstream is at
     * most a few tens of MB. */
    if (size == 0 || size > 0xFFFFFFFFUL) {
        wolfBoot_printf("PM_FPGA_LOAD: bad bitstream size\n");
        return -1;
    }

    /* PM_FPGA_LOAD takes the bitstream size in BYTES (the PMU firmware
     * divides by the word length internally for the CSU DMA). This
     * matches stock Xilinx U-Boot (drivers/fpga/zynqmppl.c passes
     * bsize verbatim). For a legacy full bitstream the flags argument
     * is 0; bit 0 selects partial. */
    pmflags = (flags == HAL_FPGA_PARTIAL) ? XFPGA_PARTIAL_EN : XFPGA_FULLBIT_EN;

    /* Ensure the bitstream is committed to DDR before the CSU DMA reads it. */
    flush_dcache_range((unsigned long)addr, (unsigned long)(addr + size));

    memset(ret_payload, 0, sizeof(ret_payload));
    /* arg0=addr_low, arg1=addr_high, arg2=size(bytes), arg3=flags */
    pmu_request(PM_FPGA_LOAD,
        (uint32_t)(addr & 0xFFFFFFFF), (uint32_t)((uint64_t)addr >> 32),
        (uint32_t)size, pmflags, ret_payload);
    if (ret_payload[0] != 0) {
        wolfBoot_printf("PM_FPGA_LOAD failed: %u\n", ret_payload[0]);
        return -1;
    }

    /* Confirm the PL reports configured (PCAP status). This is
     * informational - the load already succeeded above - so a failed
     * query is logged but does not fail the call. */
    memset(ret_payload, 0, sizeof(ret_payload));
    pmu_request(PM_FPGA_GET_STATUS, 0, 0, 0, 0, ret_payload);
    if (ret_payload[0] == 0) {
        wolfBoot_printf("FPGA status: 0x%x\n", ret_payload[1]);
    }
    else {
        wolfBoot_printf("FPGA status query failed: %u\n", ret_payload[0]);
    }

    return 0;
}
#endif /* WOLFBOOT_FPGA_BITSTREAM */

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
    int ret = 0;
    uint8_t cmd[8]; /* size multiple of uint32_t */
    uint32_t idx;
    uintptr_t qaddr;
    int off = 0;

#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("Flash Read: Addr 0x%x, Ptr %p, Len %d\n",
        address, data, len);
#endif

    /* Issue the read as separate, individually-addressed transfers, each sized
     * to a single generic-FIFO RX entry: a power of two <= 4KB (one EXP entry)
     * or the exact remainder when <= 255 (one IMM entry). The ZynqMP GQSPI
     * controller corrupts reads whose data spans more than one RX gen-FIFO entry
     * in a single continuous transfer, so re-issue the read command with a fresh
     * (incremented) address per chunk to keep every transfer to one entry. */
    while (off < len) {
        int rem = len - off;
        int chunk;

        if (rem <= 0xFF) {
            chunk = rem;                 /* one IMM gen-FIFO entry */
        }
        else {
            chunk = 0x1000;              /* one EXP entry, max 4KB */
            while (chunk > rem)
                chunk >>= 1;             /* largest power of two <= rem */
        }
#if defined(GQSPI_MODE_IO) && defined(ZYNQMP_QSPI_PIO_CHUNK)
        /* PIO mode drains the RX FIFO by CPU. Cap each transfer to <= the
         * controller RX FIFO depth so a single gen-FIFO entry can never
         * overflow it (no backpressure dependence). 128 bytes uses an IMM
         * entry and is half the 256-byte RX FIFO. */
        if (chunk > ZYNQMP_QSPI_PIO_CHUNK)
            chunk = ZYNQMP_QSPI_PIO_CHUNK;
#endif

        qaddr = address + (uintptr_t)off;
        if (mDev.stripe) {
            /* For dual parallel the per-chip address is half the combined. */
            qaddr /= 2;
        }

        idx = 0;
        memset(cmd, 0, sizeof(cmd));
        cmd[idx++] = FLASH_READ_CMD;
#if GQPI_USE_4BYTE_ADDR == 1
        cmd[idx++] = ((qaddr >> 24) & 0xFF);
#endif
        cmd[idx++] = ((qaddr >> 16) & 0xFF);
        cmd[idx++] = ((qaddr >> 8)  & 0xFF);
        cmd[idx++] = ((qaddr >> 0)  & 0xFF);
        ret = qspi_transfer(&mDev, cmd, idx, NULL, 0, data + off, chunk,
            GQSPI_DUMMY_READ, mDev.mode);
        if (ret != 0) {
#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
            wolfBoot_printf("Flash Read: Ret %d at off %d\r\n", ret, off);
#endif
            return ret;
        }
        off += chunk;
    }

#if defined(DEBUG_ZYNQ) && DEBUG_ZYNQ >= 2
    wolfBoot_printf("Flash Read: Ret %d\r\n", ret);
#endif
    return len;
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

/* The following helpers (hal_get_timer_us, hal_get_dts_address, hal_dts_fixup)
 * are only compiled into the wolfBoot binary. The test-app build also links
 * hal/zynq.o but must not pull in FDT/MMU-specific code, so __WOLFBOOT gates
 * these symbols out of that build. */
#if defined(MMU) && defined(__WOLFBOOT)
/* Fallback timer frequency if CNTFRQ_EL0 is not configured (e.g. boot path
 * that did not run ATF/BL31). ZynqMP system counter is 100 MHz. */
#ifndef ZYNQMP_TIMER_CLK_FREQ
#define ZYNQMP_TIMER_CLK_FREQ 100000000ULL
#endif

/* Get current time in microseconds using ARMv8 generic timer */
uint64_t hal_get_timer_us(void)
{
    uint64_t count, freq;
    __asm__ volatile("mrs %0, CNTPCT_EL0" : "=r"(count));
    __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(freq));
    /* Fall back to a known frequency rather than returning 0, so udelay()
     * callers that spin on hal_get_timer_us() advancing remain monotonic
     * (matches hal/versal.c). */
    if (freq == 0)
        freq = ZYNQMP_TIMER_CLK_FREQ;
    /* Use __uint128_t to avoid overflow of (count * 1e6) at long uptimes
     * (would overflow uint64_t after ~51h at 100MHz). */
    return (uint64_t)(((__uint128_t)count * 1000000ULL) / freq);
}

void* hal_get_dts_address(void)
{
#ifdef WOLFBOOT_DTS_BOOT_ADDRESS
    return (void*)WOLFBOOT_DTS_BOOT_ADDRESS;
#elif defined(WOLFBOOT_LOAD_DTS_ADDRESS)
    return (void*)WOLFBOOT_LOAD_DTS_ADDRESS;
#else
    return NULL;
#endif
}

int hal_dts_fixup(void* dts_addr)
{
    int off, ret;
    struct fdt_header *fdt = (struct fdt_header *)dts_addr;

    /* Verify FDT header */
    ret = fdt_check_header(dts_addr);
    if (ret != 0) {
        wolfBoot_printf("FDT: Invalid header! %d\n", ret);
        return ret;
    }

    wolfBoot_printf("FDT: Version %d, Size %d\n",
        fdt_version(fdt), fdt_totalsize(fdt));

    /* Expand totalsize so fdt_setprop() has in-blob free space to place
     * a new/larger bootargs property and (when WOLFBOOT_FIT_RAMDISK is in
     * play) the linux,initrd-{start,end} properties. Physical headroom is
     * already guaranteed by the load-address layout (DTB at
     * WOLFBOOT_LOAD_DTS_ADDRESS, kernel loaded much higher), so growing
     * the header is safe. Sizing comes from WOLFBOOT_FDT_FIXUP_HEADROOM
     * in include/fdt.h - same constant as hal/versal.c. */
    fdt_set_totalsize(fdt,
        fdt_totalsize(fdt) + WOLFBOOT_FDT_FIXUP_HEADROOM);

    /* Find /chosen node; create it only if genuinely missing. Any other
     * negative return (malformed FDT, etc.) is surfaced directly rather
     * than masked by a follow-on fdt_add_subnode() failure. */
    off = fdt_find_node_offset(fdt, -1, "chosen");
    if (off == -FDT_ERR_NOTFOUND) {
        off = fdt_add_subnode(fdt, 0, "chosen");
    }
    if (off < 0) {
        wolfBoot_printf("FDT: Failed to find/create chosen node (%d)\n", off);
        return off;
    }

    /* Set bootargs property - overrides PetaLinux default root= with
     * the wolfBoot partition layout. */
    ret = fdt_fixup_str(fdt, off, "chosen", "bootargs", LINUX_BOOTARGS);
    if (ret < 0) {
        wolfBoot_printf("FDT: Failed to set bootargs (%d)\n", ret);
        return ret;
    }

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


/* ============================================================================
 * SDHCI (SD Card / eMMC) Platform Support
 * ============================================================================
 * Platform-specific hooks for the generic SDHCI driver (src/sdhci.c).
 * ZynqMP uses the Arasan SDHCI controller with standard register layout.
 * The generic driver uses Cadence SD4HC register offsets, so we translate:
 *   - HRS registers at 0x000-0x01F (Cadence-specific: reset, PHY, eMMC mode)
 *   - SRS registers at 0x200-0x2FF (standard SDHCI mapped at offset +0x200)
 * Arasan uses standard SDHCI registers at 0x000-0x0FF (no 0x200 offset).
 */
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "sdhci.h"

/* SD controller base address selection:
 *   SD0 (ZYNQMP_SD0_BASE = 0xFF160000) - internal, typically eMMC
 *   SD1 (ZYNQMP_SD1_BASE = 0xFF170000) - external SD card slot on ZCU102
 */
#ifndef ZYNQMP_SDHCI_BASE
#define ZYNQMP_SDHCI_BASE  ZYNQMP_SD1_BASE
#endif

#define CADENCE_SRS_OFFSET      0x200

/* Legacy SDMA system address register (standard SDHCI offset 0x00).
 * The Arasan SDHCI v3.0 on ZynqMP does not support Host Version 4 Enable
 * (HV4E) mode. The generic SDHCI driver uses HV4E-style 64-bit DMA
 * addressing via SRS22/SRS23 (offsets 0x58/0x5C), but on this controller
 * we must use the legacy SRS00 register (offset 0x00) for SDMA addresses.
 * The platform reg_read/reg_write functions transparently redirect
 * SRS22 <-> SRS00, making legacy SDMA work without changes to sdhci.c. */
#define STD_SDHCI_SDMA_ADDR     0x00  /* SDMA System Address (32-bit) */
#define STD_SDHCI_HOST_CTRL2    0x3C  /* Auto CMD Err(16) + Host Ctrl 2(16) */

/* Standard SDHCI register offsets (byte addresses within the controller) */
#define STD_SDHCI_HOST_CTRL1    0x28  /* Host Control 1 (8-bit) */
#define STD_SDHCI_POWER_CTRL    0x29  /* Power Control (8-bit) */
#define STD_SDHCI_BLKGAP_CTRL   0x2A  /* Block Gap Control (8-bit) */
#define STD_SDHCI_WAKEUP_CTRL   0x2B  /* Wakeup Control (8-bit) */
#define STD_SDHCI_CLK_CTRL      0x2C  /* Clock Control (16-bit) */
#define STD_SDHCI_TIMEOUT_CTRL  0x2E  /* Timeout Control (8-bit) */
#define STD_SDHCI_SW_RESET      0x2F  /* Software Reset (8-bit) */

/* Software Reset register bits (at offset 0x2F, 8-bit register) */
#define STD_SDHCI_SRA           0x01  /* Software Reset for All */
#define STD_SDHCI_SRCMD         0x02  /* Software Reset for CMD Line */
#define STD_SDHCI_SRDAT         0x04  /* Software Reset for DAT Line */

/* Handle reads from Cadence HRS registers (0x000-0x1FF) */
static uint32_t zynqmp_sdhci_hrs_read(uint32_t hrs_offset)
{
    volatile uint8_t *base = (volatile uint8_t *)ZYNQMP_SDHCI_BASE;

    switch (hrs_offset) {
    case 0x000: /* HRS00 - Software Reset */
    {
        /* Map standard SRA (byte at 0x2F) to Cadence SWR (bit 0) */
        uint8_t val = *((volatile uint8_t *)(base + STD_SDHCI_SW_RESET));
        return (val & STD_SDHCI_SRA) ? 1U : 0U;
    }
    case 0x010: /* HRS04 - PHY access (Cadence-specific) */
        /* Return ACK set to prevent wait loops from hanging */
        return (1U << 26); /* SDHCI_HRS04_UIS_ACK */
    default:
        /* HRS01 (debounce), HRS02, HRS06 (eMMC mode) - not applicable */
        return 0;
    }
}

/* Handle writes to Cadence HRS registers (0x000-0x1FF) */
static void zynqmp_sdhci_hrs_write(uint32_t hrs_offset, uint32_t val)
{
    volatile uint8_t *base = (volatile uint8_t *)ZYNQMP_SDHCI_BASE;

    switch (hrs_offset) {
    case 0x000: /* HRS00 - Software Reset */
        if (val & 1U) {
            /* Issue SRA via 8-bit write to offset 0x2F (per SDHCI spec).
             * The Arasan controller requires byte-level access for the
             * Software Reset register. */
            *((volatile uint8_t *)(base + STD_SDHCI_SW_RESET)) = STD_SDHCI_SRA;
        }
        break;
    default:
        /* HRS01, HRS04, HRS06 - not applicable on ZynqMP, ignore */
        break;
    }
}

/* Register access functions for generic SDHCI driver.
 * Translates Cadence SD4HC register offsets to standard Arasan SDHCI layout.
 *
 * IMPORTANT: The Arasan SDHCI on ZynqMP requires specific register access
 * widths matching the SDHCI specification (see Xilinx SDPS driver reference):
 *   - Host Control 1 (0x28): 8-bit
 *   - Power Control (0x29):  8-bit
 *   - Clock Control (0x2C):  16-bit
 *   - Timeout Control (0x2E): 8-bit
 *   - Software Reset (0x2F): 8-bit
 * The Cadence driver uses 32-bit SRS10/SRS11 registers that span these byte
 * offsets. We decompose 32-bit writes into the correct access widths. */
uint32_t sdhci_reg_read(uint32_t offset)
{
    volatile uint8_t *base = (volatile uint8_t *)ZYNQMP_SDHCI_BASE;

    /* Cadence SRS registers (0x200+) -> standard SDHCI (subtract 0x200) */
    if (offset >= CADENCE_SRS_OFFSET) {
        uint32_t std_off = offset - CADENCE_SRS_OFFSET;

        /* SRS22 (0x58) -> SRS00 (0x00): Legacy SDMA address register */
        if (std_off == 0x58) {
            return *((volatile uint32_t *)(base + STD_SDHCI_SDMA_ADDR));
        }
        /* SRS23 (0x5C) -> 0: No 64-bit addressing on SDHCI v3.0 */
        if (std_off == 0x5C) {
            return 0;
        }

        {
            uint32_t val = *((volatile uint32_t *)(base + std_off));
            /* Mask out A64S from Capabilities to prevent HV4E init */
            if (std_off == 0x40) { /* SRS16 - Capabilities */
                val &= ~SDHCI_SRS16_A64S;
            }
            return val;
        }
    }
    /* Cadence HRS registers (0x000-0x1FF) -> translate to standard equivalents */
    return zynqmp_sdhci_hrs_read(offset);
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    volatile uint8_t *base = (volatile uint8_t *)ZYNQMP_SDHCI_BASE;

    if (offset >= CADENCE_SRS_OFFSET) {
        uint32_t std_off = offset - CADENCE_SRS_OFFSET;

        /* SRS10 (Cadence 0x228) = standard 0x28-0x2B:
         *   0x28: Host Control 1 (8-bit)
         *   0x29: Power Control (8-bit)
         *   0x2A: Block Gap Control (8-bit)
         *   0x2B: Wakeup Control (8-bit) */
        if (std_off == 0x28) {
            *((volatile uint8_t *)(base + STD_SDHCI_HOST_CTRL1)) =
                (uint8_t)(val & 0xFF);
            *((volatile uint8_t *)(base + STD_SDHCI_POWER_CTRL)) =
                (uint8_t)((val >> 8) & 0xFF);
            *((volatile uint8_t *)(base + STD_SDHCI_BLKGAP_CTRL)) =
                (uint8_t)((val >> 16) & 0xFF);
            *((volatile uint8_t *)(base + STD_SDHCI_WAKEUP_CTRL)) =
                (uint8_t)((val >> 24) & 0xFF);
            return;
        }

        /* SRS11 (Cadence 0x22C) = standard 0x2C-0x2F:
         *   0x2C: Clock Control (16-bit)
         *   0x2E: Timeout Control (8-bit)
         *   0x2F: Software Reset (8-bit) */
        if (std_off == 0x2C) {
            *((volatile uint16_t *)(base + STD_SDHCI_CLK_CTRL)) =
                (uint16_t)(val & 0xFFFF);
            *((volatile uint8_t *)(base + STD_SDHCI_TIMEOUT_CTRL)) =
                (uint8_t)((val >> 16) & 0xFF);
            *((volatile uint8_t *)(base + STD_SDHCI_SW_RESET)) =
                (uint8_t)((val >> 24) & 0xFF);
            return;
        }

        /* SRS22 (0x58) -> SRS00 (0x00): Legacy SDMA address register.
         * The generic driver writes the DMA buffer address here for HV4E mode.
         * Redirect to SRS00 which is the legacy SDMA system address register.
         * Writing SRS00 also restarts DMA after a boundary interrupt. */
        if (std_off == 0x58) {
            *((volatile uint32_t *)(base + STD_SDHCI_SDMA_ADDR)) = val;
            return;
        }
        /* SRS23 (0x5C) -> no-op: No 64-bit addressing on SDHCI v3.0 */
        if (std_off == 0x5C) {
            return;
        }

        /* SRS15 (0x3C) -> mask out HV4E and A64 bits.
         * The generic driver enables HV4E for SDMA, but the Arasan SDHCI v3.0
         * does not support it. These are reserved bits on v3.0 and must not
         * be set. */
        if (std_off == STD_SDHCI_HOST_CTRL2) {
            val &= ~(SDHCI_SRS15_HV4E | SDHCI_SRS15_A64);
        }

        /* All other SRS registers: 32-bit write */
        *((volatile uint32_t *)(base + std_off)) = val;
        return;
    }
    /* Cadence HRS registers (0x000-0x1FF) -> translate to standard equivalents */
    zynqmp_sdhci_hrs_write(offset, val);
}

/* Platform initialization - called from sdhci_init()
 * FSBL already initializes the SD controller on ZynqMP when booting from SD,
 * so we don't need to configure clocks/reset (CRL_APB registers).
 *
 * However, the FSBL uses GPIO-based card detect (polling MIO45 as GPIO)
 * rather than the SDHCI controller's built-in CD mechanism. The default
 * IOU_SLCR SD_CONFIG_REG2 slot type is "Removable" (00), but MIO45 is not
 * routed to the SDHCI controller as a CD function. This causes the Arasan
 * SDHCI to report Card Inserted=0 and gate writes to Bus Power and SD Clock
 * Enable registers.
 *
 * Fix: Set SD1 slot type to "Embedded" (01) in IOU_SLCR SD_CONFIG_REG2.
 * This makes the controller always assert Card Inserted and Card State
 * Stable, allowing normal SDHCI register access. */
void sdhci_platform_init(void)
{
    uint32_t reg;
    volatile int i;
    uint32_t slot_mask;
    uint32_t slot_shift;
    uint32_t reset_bit;

    /* Set the selected SDx slot type to "Embedded Slot for One Device" (01).
     * This feeds into the SDHCI Capabilities register bits 31:30 and makes
     * the controller report card as always present, bypassing the physical
     * CD pin that is not connected to the SDHCI controller on ZCU102. */
#if ZYNQMP_SDHCI_BASE == ZYNQMP_SD0_BASE
    slot_mask  = SD_CONFIG_REG2_SD0_SLOTTYPE_MASK;
    slot_shift = SD_CONFIG_REG2_SD0_SLOTTYPE_SHIFT;
    reset_bit  = RST_LPD_IOU2_SDIO0;
#else
    slot_mask  = SD_CONFIG_REG2_SD1_SLOTTYPE_MASK;
    slot_shift = SD_CONFIG_REG2_SD1_SLOTTYPE_SHIFT;
    reset_bit  = RST_LPD_IOU2_SDIO1;
#endif

    reg = IOU_SLCR_SD_CONFIG_REG2;
    reg &= ~slot_mask;
    reg |= (1UL << slot_shift); /* 01 = Embedded */
    IOU_SLCR_SD_CONFIG_REG2 = reg;

    /* The SDHCI Capabilities register latches IOU_SLCR values on controller
     * reset. Issue SDIOx reset via CRL_APB so the controller picks up the
     * new slot type configuration. */
    RST_LPD_IOU2 |= reset_bit;              /* Assert SDIOx reset */
    for (i = 0; i < 100; i++) {}             /* Brief delay */
    RST_LPD_IOU2 &= ~reset_bit;             /* De-assert SDIOx reset */
    for (i = 0; i < 1000; i++) {}            /* Wait for controller ready */

#ifdef DEBUG_SDHCI
    {
        volatile uint8_t *base = (volatile uint8_t *)ZYNQMP_SDHCI_BASE;
        uint32_t val;

        wolfBoot_printf("sdhci_platform_init: SD%d at 0x%x\n",
#if ZYNQMP_SDHCI_BASE == ZYNQMP_SD0_BASE
            0,
#else
            1,
#endif
            (unsigned int)ZYNQMP_SDHCI_BASE);

        wolfBoot_printf("  SD_CONFIG_REG2: 0x%x\n",
            (unsigned int)IOU_SLCR_SD_CONFIG_REG2);

        /* Read standard SDHCI registers to verify controller access */
        val = *((volatile uint32_t *)(base + 0x24));  /* Present State */
        wolfBoot_printf("  Present State: 0x%x\n", (unsigned int)val);

        val = *((volatile uint32_t *)(base + 0x40));  /* Capabilities */
        wolfBoot_printf("  Capabilities:  0x%x\n", (unsigned int)val);
        (void)val;
    }
#endif
}

/* Platform interrupt setup - called from sdhci_init()
 * Using polling mode for simplicity - no GIC setup needed */
void sdhci_platform_irq_init(void)
{
#ifdef DEBUG_SDHCI
    wolfBoot_printf("sdhci_platform_irq_init: Using polling mode\n");
#endif
}

/* Platform bus mode selection - called from sdhci_init() after software reset */
void sdhci_platform_set_bus_mode(int is_emmc)
{
    (void)is_emmc;
#ifdef DEBUG_SDHCI
    wolfBoot_printf("sdhci_platform_set_bus_mode: is_emmc=%d\n", is_emmc);
#endif
}

/* DMA cache maintenance - called from sdhci_transfer() around SDMA operations.
 * The SDMA engine transfers data directly to/from physical memory, bypassing
 * the CPU's L1/L2 caches. We must ensure cache coherency:
 *   - Before DMA write (card <- memory): clean D-cache so DMA reads correct data
 *   - After DMA read (card -> memory): invalidate D-cache so CPU sees new data */
void sdhci_platform_dma_prepare(void *buf, uint32_t sz, int is_write)
{
    uintptr_t addr;
    uintptr_t start = (uintptr_t)buf & ~(CACHE_LINE_SIZE - 1);
    uintptr_t end = ((uintptr_t)buf + sz + CACHE_LINE_SIZE - 1) &
        ~(CACHE_LINE_SIZE - 1);

    if (is_write) {
        /* Clean D-cache: flush dirty lines to memory for DMA to read */
        for (addr = start; addr < end; addr += CACHE_LINE_SIZE) {
            __asm__ volatile("dc cvac, %0" : : "r"(addr) : "memory");
        }
    } else {
        /* Invalidate D-cache: discard stale lines before DMA writes to memory */
        for (addr = start; addr < end; addr += CACHE_LINE_SIZE) {
            __asm__ volatile("dc civac, %0" : : "r"(addr) : "memory");
        }
    }
    __asm__ volatile("dsb sy" : : : "memory");
}

void sdhci_platform_dma_complete(void *buf, uint32_t sz, int is_write)
{
    /* After DMA read (card->memory): invalidate so CPU sees DMA-written data.
     * For DMA write (card<-memory): DMA only read from memory, so there is no
     * new data for the CPU to see and invalidation could discard dirty lines. */
    uintptr_t addr;
    uintptr_t start = (uintptr_t)buf & ~(CACHE_LINE_SIZE - 1);
    uintptr_t end = ((uintptr_t)buf + sz + CACHE_LINE_SIZE - 1) &
        ~(CACHE_LINE_SIZE - 1);

    if (!is_write) {
        for (addr = start; addr < end; addr += CACHE_LINE_SIZE) {
            __asm__ volatile("dc civac, %0" : : "r"(addr) : "memory");
        }
        __asm__ volatile("dsb sy" : : : "memory");
    }
}
#endif /* DISK_SDCARD || DISK_EMMC */


#endif /* TARGET_zynq */
