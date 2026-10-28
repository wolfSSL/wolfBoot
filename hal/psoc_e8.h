/* psoc_e8.h
 *
 * Register definitions for Infineon PSOC Edge E8 (CAT1D, Cortex-M33).
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

#ifndef PSOC_E8_H_INCLUDED
#define PSOC_E8_H_INCLUDED

#include <stdint.h>

/* A host-built unit test cannot assemble the barriers; the compiler barrier
 * alone stands in there. */
#if defined(__arm__) || defined(__ARM_ARCH)
#define ISB() __asm__ volatile ("isb" ::: "memory")
#define DSB() __asm__ volatile ("dsb" ::: "memory")
#else
#define ISB() __asm__ volatile ("" ::: "memory")
#define DSB() __asm__ volatile ("" ::: "memory")
#endif

/* Each memory has four aliases (S/NS x code/system bus). Within an SMIF
 * window the low 26 bits are the device offset. */
#define PSOC_E8_SMIF_WINDOW     (0x04000000UL)
#define PSOC_E8_RRAM_SBUS_S     (0x32000000UL)
#define PSOC_E8_RRAM_SIZE       (0x00080000UL)

/* Build options, defaulted for KIT_PSE84_AI. The console is SCB2 on P6.5 (RX)
 * / P6.7 (TX), the KitProg3 COM port; its clock destination is
 * PCLK_SCB2_CLOCK_SCB_EN (group 1, index 1). wolfBoot leaves the clocks as
 * extended boot sets them: CLK_HF0 (CM33) 200 MHz, CLK_HF10 100 MHz. */
#ifndef PSOC_E8_UART_SCB
#define PSOC_E8_UART_SCB        2
#endif
#ifndef PSOC_E8_UART_PORT
#define PSOC_E8_UART_PORT       6
#endif
#ifndef PSOC_E8_UART_RX_PIN
#define PSOC_E8_UART_RX_PIN     5
#endif
#ifndef PSOC_E8_UART_TX_PIN
#define PSOC_E8_UART_TX_PIN     7
#endif
#ifndef PSOC_E8_UART_HSIOM_SEL
#define PSOC_E8_UART_HSIOM_SEL  20
#endif
#ifndef PSOC_E8_UART_PCLK_GR
#define PSOC_E8_UART_PCLK_GR    1
#endif
#ifndef PSOC_E8_UART_PCLK_IDX
#define PSOC_E8_UART_PCLK_IDX   1
#endif
#ifndef PSOC_E8_UART_PCLK_DIV
#define PSOC_E8_UART_PCLK_DIV   0
#endif
#ifndef PSOC_E8_UART_BAUD
#define PSOC_E8_UART_BAUD       115200UL
#endif
#define PSOC_E8_UART_OVS        8UL
#ifndef PSOC_E8_PCLK_HZ
#define PSOC_E8_PCLK_HZ         100000000UL
#endif
#ifndef PSOC_E8_CPU_HZ
#define PSOC_E8_CPU_HZ          200000000UL
#endif
/* QSPI NOR erase sector. */
#ifndef PSOC_E8_QSPI_SECTOR_SIZE
#define PSOC_E8_QSPI_SECTOR_SIZE (0x40000UL)
#endif
#ifndef PSOC_E8_RRAM_TIMEOUT
#define PSOC_E8_RRAM_TIMEOUT    (1000000UL)
#endif

/* Peripherals, Secure alias. */
#define PERI0_BASE      (0x52000000UL)
#define PERI_PCLK0_BASE (0x52040000UL)
#define HSIOM_BASE      (0x52800000UL)
#define GPIO_BASE       (0x52810000UL)

/* A slave in a gated PERI group bus-faults on access. */
#define PSOC_E8_PERI_GR_SCB     1
#define PERI_GR_SL_CTL(g) \
    (*(volatile uint32_t *)(PERI0_BASE + 0x4000UL + \
                            ((uint32_t)(g) * 0x40UL) + 0x10UL))
#define PERI_GR_SL_CTL2(g) \
    (*(volatile uint32_t *)(PERI0_BASE + 0x4000UL + \
                            ((uint32_t)(g) * 0x40UL) + 0x14UL))

/* PERI_PCLK: 0x2000 per group; dividers by number, CLOCK_CTL by destination. */
#define PERI_PCLK_GR(g)         (PERI_PCLK0_BASE + ((uint32_t)(g) * 0x2000UL))
#define PERI_PCLK_DIV_CMD(g)    (*(volatile uint32_t *)(PERI_PCLK_GR(g)))
#define PERI_PCLK_CLOCK_CTL(g,i)  \
    (*(volatile uint32_t *)(PERI_PCLK_GR(g) + 0xC00UL + ((uint32_t)(i) * 4UL)))
#define PERI_PCLK_DIV_16_5_CTL(g,d) \
    (*(volatile uint32_t *)(PERI_PCLK_GR(g) + 0x1800UL + ((uint32_t)(d) * 4UL)))
#define PERI_PCLK_DIV_16_5_EN       (1UL << 0)
#define PERI_PCLK_DIV_16_5_FRAC_Pos (3U)
#define PERI_PCLK_DIV_16_5_INT_Pos  (8U)
#define PERI_PCLK_DIV_TYPE_16_5     (2UL)
#define PERI_PCLK_CLOCK_CTL_TYPE_Pos (8U)
/* A divider's EN bit is status only; DIV_CMD starts it. PA_TYPE 3 with
 * PA_DIV 0xFF selects no phase alignment. */
#define PERI_PCLK_DIV_CMD_ENABLE    (1UL << 31)
#define PERI_PCLK_DIV_CMD_PA_NONE   ((3UL << 24) | (0xFFUL << 16))
#define PERI_PCLK_DIV_CMD_TYPE_Pos  (8U)

#ifndef PSOC_E8_PERI_TIMEOUT
#define PSOC_E8_PERI_TIMEOUT        (1000000UL)
#endif

/* HSIOM: 0x10 per port, one mux byte per pin (pins 4-7 in PORT_SEL1). */
#define HSIOM_PORT_SEL(p, pin) \
    (*(volatile uint32_t *)(HSIOM_BASE + ((uint32_t)(p) * 0x10UL) + \
                            (((uint32_t)(pin) < 4U) ? 0x00UL : 0x04UL)))
#define HSIOM_SEL_SHIFT(pin)    ((((uint32_t)(pin)) & 3U) * 8U)
/* 1 marks a pin non-secure, and secure writes to its HSIOM/GPIO are then
 * ignored, so it is cleared first. */
#define HSIOM_SECURE_PRT_NSMASK(p) \
    (*(volatile uint32_t *)(HSIOM_BASE + 0x1000UL + ((uint32_t)(p) * 0x10UL)))

/* GPIO: 0x80 per port, 4 drive-mode bits per pin in CFG. */
#define GPIO_PRT_CFG(p) \
    (*(volatile uint32_t *)(GPIO_BASE + ((uint32_t)(p) * 0x80UL) + 0x44UL))
#define GPIO_CFG_DM_HIGHZ       (0x8UL)  /* input buffer on, no drive */
#define GPIO_CFG_DM_STRONG      (0x6UL)  /* strong drive, input buffer off */
#define GPIO_CFG_DM_MASK        (0xFUL)

/* SCB2-SCB11 are contiguous; SCB0 and SCB1 are not. */
#define PSOC_E8_SCB_BASE(n) \
    ((n) == 0 ? 0x52990000UL : (n) == 1 ? 0x52D00000UL : \
     (0x52980000UL + ((uint32_t)(n) * 0x10000UL)))

#define SCB_CTRL(b)          (*(volatile uint32_t *)((b) + 0x000UL))
#define SCB_UART_CTRL(b)     (*(volatile uint32_t *)((b) + 0x040UL))
#define SCB_UART_TX_CTRL(b)  (*(volatile uint32_t *)((b) + 0x044UL))
#define SCB_UART_RX_CTRL(b)  (*(volatile uint32_t *)((b) + 0x048UL))
#define SCB_TX_CTRL(b)       (*(volatile uint32_t *)((b) + 0x200UL))
#define SCB_TX_FIFO_CTRL(b)  (*(volatile uint32_t *)((b) + 0x204UL))
#define SCB_TX_FIFO_STATUS(b) (*(volatile uint32_t *)((b) + 0x208UL))
#define SCB_TX_FIFO_WR(b)    (*(volatile uint32_t *)((b) + 0x240UL))
#define SCB_RX_CTRL(b)       (*(volatile uint32_t *)((b) + 0x300UL))
#define SCB_RX_FIFO_CTRL(b)  (*(volatile uint32_t *)((b) + 0x304UL))

#define SCB_CTRL_ENABLED     (1UL << 31)
#define SCB_CTRL_MODE_UART   (2UL << 24)
#define SCB_CTRL_OVS_MASK    (0xFUL)
#define SCB_DATA_WIDTH(n)    ((uint32_t)((n) - 1))
#define SCB_TX_FIFO_USED_Msk     (0x1FFUL)
#define SCB_TX_FIFO_SR_VALID_Msk (1UL << 15)
#define SCB_TX_FIFO_SAFE_DEPTH   (8UL)

/* SMIF0: MMIO command FIFOs and per-device XIP setup in the core block, the
 * XIP line cache in its own block. */
#define SMIF0_CACHE_BASE    (0x54450000UL)
#define SMIF0_CORE_BASE     (0x54460000UL)

#define SMIF_STATUS         (*(volatile uint32_t *)(SMIF0_CORE_BASE + 0x004UL))
#define SMIF_TX_CMD_STATUS  (*(volatile uint32_t *)(SMIF0_CORE_BASE + 0x048UL))
#define SMIF_TX_CMD_WR      (*(volatile uint32_t *)(SMIF0_CORE_BASE + 0x050UL))
#define SMIF_TX_DATA_STATUS (*(volatile uint32_t *)(SMIF0_CORE_BASE + 0x088UL))
#define SMIF_TX_DATA_WR1    (*(volatile uint32_t *)(SMIF0_CORE_BASE + 0x090UL))
#define SMIF_RX_DATA_STATUS (*(volatile uint32_t *)(SMIF0_CORE_BASE + 0x0C4UL))
#define SMIF_RX_DATA_RD1    (*(volatile uint32_t *)(SMIF0_CORE_BASE + 0x0D0UL))
#define SMIF_SLOW_CA_CMD    (*(volatile uint32_t *)(SMIF0_CORE_BASE + 0x108UL))
#define SMIF_FAST_CA_CMD    (*(volatile uint32_t *)(SMIF0_CORE_BASE + 0x188UL))
#define SMIF_DEVICE_CTL(n) \
    (*(volatile uint32_t *)(SMIF0_CORE_BASE + 0x800UL + ((n) * 0x80UL)))
#define SMIF_DEVICE_ADDR_CTL(n) \
    (*(volatile uint32_t *)(SMIF0_CORE_BASE + 0x820UL + ((n) * 0x80UL)))
#define SMIF_DEVICES            4
#define SMIF_DEVICE_CTL_ENABLED (1UL << 31)
#define SMIF_STATUS_BUSY        (1UL << 31)
#define SMIF_CA_CMD_INV         (1UL << 0)
#define SMIF_FIFO_USED_Msk      (0xFUL)
#define SMIF_FIFO_DEPTH         (8UL)

/* TX_CMD_MMIO_FIFO_WR: MODE[26:24], one-hot SS[23:20], LAST_BYTE[19] (release
 * select), WIDTH[17:16], byte[7:0]; count entries hold count - 1 in [15:0]. */
#define SMIF_CMD_MODE_TX        (0UL << 24)
#define SMIF_CMD_MODE_TX_COUNT  (1UL << 24)
#define SMIF_CMD_MODE_RX_COUNT  (2UL << 24)
#define SMIF_CMD_SS(dev)        ((1UL << (dev)) << 20)
#define SMIF_CMD_LAST           (1UL << 19)

#define SMIF_CACHE_MAINT_ALL \
    (*(volatile uint32_t *)(SMIF0_CACHE_BASE + 0x20UL))
#define SMIF_CACHE_MAINT_STATUS \
    (*(volatile uint32_t *)(SMIF0_CACHE_BASE + 0x28UL))
#define SMIF_CACHE_INVALIDATE_ALL   (1UL << 1)
#define SMIF_CACHE_MAINT_ONGOING    (1UL << 2)

/* CM33 instruction cache. */
#define ICACHE0_CMD             (*(volatile uint32_t *)(0x52223008UL))
#define ICACHE_CMD_INV          (1UL << 0)

/* RRAM indirect write: select the 16-byte block in NVM_ADDR, write its four
 * words at the mapped address, then STARTOP. */
#define RRAMC_PC_MASK       (*(volatile uint32_t *)(0x52210020UL))
#define RRAMC_NVM_STATUS    (*(volatile uint32_t *)(0x52213000UL))
#define RRAMC_NVM_PROG      (*(volatile uint32_t *)(0x52213004UL))
#define RRAMC_NVM_ADDR      (*(volatile uint32_t *)(0x52213008UL))
#define RRAMC_PC_MASK_PC_LOCK_Msk   (0xFUL << 28)
#define RRAMC_NVM_STATUS_BUSY       (1UL << 0)
#define RRAMC_NVM_STATUS_VERR_Pos   (2U)
#define RRAMC_NVM_STATUS_VERR_Msk   (0x7UL << 2)
#define RRAMC_NVM_STATUS_ECC4MRD    (1UL << 19)
#define RRAMC_NVM_PROG_ACTION_Msk   (0xFUL)
#define RRAMC_NVM_PROG_IDLE         (0UL)
#define RRAMC_NVM_PROG_IND_WRITE    (1UL)
#define RRAMC_NVM_PROG_STARTOP      (0x3UL << 14)
#define RRAM_BLOCK_SIZE             (16U)
#define RRAM_WRITE_TRIALS           (3U)

/* IPC channel 2 serialises RRAM writers (vendor erratum workaround); reading
 * ACQUIRE attempts the lock. */
#define IPC0_RRAM_ACQUIRE   (*(volatile uint32_t *)(0x522A0040UL))
#define IPC0_RRAM_RELEASE   (*(volatile uint32_t *)(0x522A0044UL))
#define IPC_ACQUIRE_SUCCESS (1UL << 31)

#define AIRCR                   (*(volatile uint32_t *)(0xE000ED0CUL))
#define AIRCR_VKEY              (0x05FAUL << 16)
#define AIRCR_SYSRESETREQ       (1UL << 2)

/* CM33 cycle counter, the time base for hal_get_timer_us(). */
#define DEMCR                   (*(volatile uint32_t *)(0xE000EDFCUL))
#define DEMCR_TRCENA            (1UL << 24)
#define DWT_CTRL                (*(volatile uint32_t *)(0xE0001000UL))
#define DWT_CTRL_CYCCNTENA      (1UL << 0)
#define DWT_CYCCNT              (*(volatile uint32_t *)(0xE0001004UL))

/* SPI NOR commands. */
#define NOR_CMD_RDSR            (0x05U)
#define NOR_CMD_CLPEF           (0x82U)   /* clear program/erase failure */
#define NOR_SR_E_ERR            (1U << 5)
#define NOR_SR_P_ERR            (1U << 6)

/* Memory protection controllers, ROT view. Each LUT word holds eight 4-bit
 * entries per block: bit 0 Non-secure, bit 1 read, bit 2 write. */
#define RAMC1_MPC_BASE          (0x52244000UL)
#define SMIF0_AHB_MPC_BASE      (0x54454000UL)
#define MPC_ROT_CTRL(b)         (*(volatile uint32_t *)((b) + 0x200UL))
#define MPC_ROT_BLK_MAX(b)      (*(volatile uint32_t *)((b) + 0x208UL))
#define MPC_ROT_BLK_CFG(b)      (*(volatile uint32_t *)((b) + 0x20CUL))
#define MPC_ROT_BLK_IDX(b)      (*(volatile uint32_t *)((b) + 0x210UL))
#define MPC_ROT_BLK_PC(b)       (*(volatile uint32_t *)((b) + 0x214UL))
#define MPC_ROT_BLK_LUT(b)      (*(volatile uint32_t *)((b) + 0x218UL))
#define MPC_ROT_CTRL_AUTO_INC   (1UL << 8)
#define MPC_ROT_BLK_CFG_SIZE_Msk (0xFUL)
#define MPC_ROT_BLK_CFG_INIT_BUSY (1UL << 31)
#define MPC_ROT_BLKS_PER_IDX    (8U)
#define MPC_ROT_NS_RW           (0x7UL)
/* Protection context of the CM33, Secure and Non-secure alike. */
#define PSOC_E8_PC_CM33         (2U)

/* SAU */
#define SAU_CTRL                (*(volatile uint32_t *)(0xE000EDD0UL))
#define SAU_RNR                 (*(volatile uint32_t *)(0xE000EDD8UL))
#define SAU_RBAR                (*(volatile uint32_t *)(0xE000EDDCUL))
#define SAU_RLAR                (*(volatile uint32_t *)(0xE000EDE0UL))
#define SAU_CTRL_ENABLE         (1UL << 0)
#define SAU_RLAR_ENABLE         (1UL << 0)
#define SAU_RLAR_NSC            (1UL << 1)

/* Crypto block TRNG, used to seed the Secure world's DRBG. */
#define CRYPTO_BASE             (0x52440000UL)
#define CRYPTO_CTL              (*(volatile uint32_t *)(CRYPTO_BASE + 0x000UL))
#define CRYPTO_INTR             (*(volatile uint32_t *)(CRYPTO_BASE + 0x100UL))
/* INTR flags are write-one-to-clear. */
#define CRYPTO_INTR_CLR(m)      (CRYPTO_INTR = (m))
#define CRYPTO_TR_CTL0          (*(volatile uint32_t *)(CRYPTO_BASE + 0x280UL))
#define CRYPTO_TR_CTL1          (*(volatile uint32_t *)(CRYPTO_BASE + 0x284UL))
#define CRYPTO_TR_CTL2          (*(volatile uint32_t *)(CRYPTO_BASE + 0x288UL))
#define CRYPTO_TR_CMD           (*(volatile uint32_t *)(CRYPTO_BASE + 0x290UL))
#define CRYPTO_TR_RESULT        (*(volatile uint32_t *)(CRYPTO_BASE + 0x298UL))
#define CRYPTO_TR_GARO_CTL      (*(volatile uint32_t *)(CRYPTO_BASE + 0x2A0UL))
#define CRYPTO_TR_FIRO_CTL      (*(volatile uint32_t *)(CRYPTO_BASE + 0x2A4UL))
#define CRYPTO_TR_MON_CTL       (*(volatile uint32_t *)(CRYPTO_BASE + 0x2C0UL))
#define CRYPTO_TR_MON_CMD       (*(volatile uint32_t *)(CRYPTO_BASE + 0x2C8UL))
#define CRYPTO_TR_MON_RC_CTL    (*(volatile uint32_t *)(CRYPTO_BASE + 0x2D0UL))
#define CRYPTO_TR_MON_AP_CTL    (*(volatile uint32_t *)(CRYPTO_BASE + 0x2E0UL))
#define CRYPTO_STATUS           (*(volatile uint32_t *)(CRYPTO_BASE + 0x1004UL))
#define CRYPTO_CTL_ENABLED      (1UL << 31)
#define CRYPTO_INTR_TR_INITIALIZED (1UL << 2)
#define CRYPTO_INTR_TR_DATA_AVAILABLE (1UL << 3)
#define CRYPTO_INTR_TR_AP_ERROR (1UL << 19)
#define CRYPTO_INTR_TR_RC_ERROR (1UL << 20)
#define CRYPTO_INTR_TR_ALL      (CRYPTO_INTR_TR_INITIALIZED | \
                                 CRYPTO_INTR_TR_DATA_AVAILABLE | \
                                 CRYPTO_INTR_TR_AP_ERROR | \
                                 CRYPTO_INTR_TR_RC_ERROR)
/* TR_CTL0: init delay 3, von Neumann correction, stop on either health test
 * failure. TR_CTL1: all six ring oscillators. */
#define CRYPTO_TR_CTL0_DEFAULT  ((3UL << 16) | (1UL << 24) | (1UL << 28) | \
                                 (1UL << 29))
#define CRYPTO_TR_CTL1_ALL_RO   (0x3FUL)
#define CRYPTO_TR_POLY          (0x04C11DB7UL)
#define CRYPTO_TR_MON_BS_TR     (2UL)     /* monitor the TRNG bit stream */
#define CRYPTO_TR_MON_START_AP_RC (0x3UL)
#define CRYPTO_TR_RC_CUTOFF     (255UL)
#define CRYPTO_TR_AP_CTL        ((65535UL << 16) | 65535UL)

/* HAL services also used by the application. */
void psoc_e8_peri_init(void);
void psoc_e8_pin_setup(int port, int pin, uint32_t sel, uint32_t drive);
int psoc_e8_pclk_setup(uint32_t gr, uint32_t idx, uint32_t div,
        uint32_t pclk_hz, uint32_t target_hz);
void psoc_e8_delay_us(uint32_t us);
uint64_t hal_get_timer_us(void);

/* Console gateway for the Non-secure application. */
#define PSOC_E8_NSC_UART_MAX    (256U)
void psoc_e8_nsc_uart_write(const char *buf, uint32_t len);

#endif /* PSOC_E8_H_INCLUDED */
