/* psoc6.c
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

/* Standard library includes */
#include <stdint.h>
#include <string.h>

/* Project includes */
#include "image.h"
#include "printf.h"
#include <target.h>
#include "hal.h"
#include "hal/psoc6.h"

#ifdef WOLFSSL_PSOC6_CRYPTO
#include "wolfssl/wolfcrypt/port/cypress/psoc6_crypto.h"
#endif

#include "cy_device_headers.h"
#include "cy_flash.h"
#include "cy_gpio.h"
#include "cy_sysclk.h"
#include "cy_syslib.h"
#ifdef DEBUG_UART
#include "cy_scb_uart.h"
#endif
#ifdef BOOT_BENCHMARK
#include "cy_tcpwm_counter.h"
#endif
#include "cy_sysint.h"
#ifdef EXT_FLASH
#include "cy_smif_memslot.h"
#endif

#define EMULATED_EEPROM_START (CY_EM_EEPROM_BASE)
#define ROW_SIZE (CY_FLASH_SIZEOF_ROW)
#define FLASH_SUBSECTOR_SIZE (CY_FLASH_SIZEOF_ROW * 8)
#define FLASH_SECTOR_SIZE (CY_FLASH_SIZEOF_ROW * CPUSS_FLASHC_SONOS_MAIN_ROWS)
#define FLASH_BASE_ADDRESS (CY_FLASH_BASE)

/* Magic number constants */
#define BANKMODE_MAGIC (0x4B424C44U)      /* 'DLBK' */

/* NVM_FLASH_WRITEONCE keeps a whole trailer, one flag nibble per sector, in
 * one sector */
#if (((WOLFBOOT_PARTITION_SIZE) / (WOLFBOOT_SECTOR_SIZE)) / 2) + 16 > \
    (WOLFBOOT_SECTOR_SIZE)
    #error "WOLFBOOT_SECTOR_SIZE is too small for the partition trailer"
#endif

#ifdef EXT_FLASH

static cy_stc_smif_context_t smif_context;

/*
 * SFDP-based external memory configuration (self-contained, no BSP generated
 * source dependency).
 */
static cy_stc_smif_mem_cmd_t sfdp_cmd = {
    .command     = 0x5Au,
    .cmdWidth    = CY_SMIF_WIDTH_SINGLE,
    .addrWidth   = CY_SMIF_WIDTH_SINGLE,
    .mode        = 0xFFFFFFFFU,
    .dummyCycles = 8u,
    .dataWidth   = CY_SMIF_WIDTH_SINGLE,
};

/* Zero-initialized command structs — populated at runtime by Cy_SMIF_MemInit()
 * after reading the device's SFDP table */
static cy_stc_smif_mem_cmd_t rdcmd0;
static cy_stc_smif_mem_cmd_t wrencmd0;
static cy_stc_smif_mem_cmd_t wrdiscmd0;
static cy_stc_smif_mem_cmd_t erasecmd0;
static cy_stc_smif_mem_cmd_t chiperasecmd0;
static cy_stc_smif_mem_cmd_t pgmcmd0;
static cy_stc_smif_mem_cmd_t readsts0;
static cy_stc_smif_mem_cmd_t readstsqecmd0;
static cy_stc_smif_mem_cmd_t writestseqcmd0;

static cy_stc_smif_mem_device_cfg_t dev_sfdp_0 = {
    .numOfAddrBytes   = 4u,
    .readSfdpCmd      = &sfdp_cmd,
    .readCmd          = &rdcmd0,
    .writeEnCmd       = &wrencmd0,
    .writeDisCmd      = &wrdiscmd0,
    .programCmd       = &pgmcmd0,
    .eraseCmd         = &erasecmd0,
    .chipEraseCmd     = &chiperasecmd0,
    .readStsRegWipCmd = &readsts0,
    .readStsRegQeCmd  = &readstsqecmd0,
    .writeStsRegQeCmd = &writestseqcmd0,
};

static cy_stc_smif_mem_config_t mem_sfdp_0 = {
    .baseAddress   = CY_XIP_BASE,
    .memMappedSize = CY_XIP_SIZE,
    .flags         = CY_SMIF_FLAG_DETECT_SFDP | CY_SMIF_FLAG_MEMORY_MAPPED,
    .slaveSelect   = CY_SMIF_SLAVE_SELECT_0,
    .dataSelect    = CY_SMIF_DATA_SEL0,
    .deviceCfg     = &dev_sfdp_0,
};

static cy_stc_smif_mem_config_t* mems_sfdp[1] = {
    &mem_sfdp_0,
};

static cy_stc_smif_block_config_t smifBlockConfig_sfdp = {
    .memCount  = 1u,
    .memConfig = mems_sfdp,
};

static cy_stc_smif_block_config_t* smif_blk_config;

static SMIF_Type* QSPIPort = SMIF0;

/*
 * QSPI / SMIF hardware configuration
 *
 *   SS0 - P11_2       D3  - P11_3       D2  - P11_4
 *   D1  - P11_5       D0  - P11_6       SCK - P11_7
 *
 * SMIF Block - SMIF0
 */
#define CY_SMIF_SYSCLK_HFCLK_DIVIDER (CY_SYSCLK_CLKHF_DIVIDE_BY_4)
#define CY_SMIF_INIT_TRY_COUNT (10U)
#define CY_SMIF_INIT_RETRY_DELAY_MS (500U)
#define CY_SMIF_INIT_TIMEOUT_US (1000U)

/*
 * SMIF SlaveSelect configuration — only SS0 is used here.
 * Available slave select options
 *   SS0 - P11_2  (P11_2_SMIF_SPI_SELECT0)  — default, external QSPI flash
 *   SS1 - P11_1  (P11_1_SMIF_SPI_SELECT1)
 *   SS2 - P11_0  (P11_0_SMIF_SPI_SELECT2)
 *   SS3 - P12_4  (P12_4_SMIF_SPI_SELECT3)  — only on 1M/2M packages
 */
static GPIO_PRT_Type* const qspi_SS_Port = GPIO_PRT11;
static const uint32_t qspi_SS_Pin        = 2u;
static const en_hsiom_sel_t qspi_SS_Mux  = P11_2_SMIF_SPI_SELECT0;

static GPIO_PRT_Type* const qspi_D3_Port = GPIO_PRT11;
static const uint32_t qspi_D3_Pin        = 3u;
static const en_hsiom_sel_t qspi_D3_Mux  = P11_3_SMIF_SPI_DATA3;

static GPIO_PRT_Type* const qspi_D2_Port = GPIO_PRT11;
static const uint32_t qspi_D2_Pin        = 4u;
static const en_hsiom_sel_t qspi_D2_Mux  = P11_4_SMIF_SPI_DATA2;

static GPIO_PRT_Type* const qspi_D1_Port = GPIO_PRT11;
static const uint32_t qspi_D1_Pin        = 5u;
static const en_hsiom_sel_t qspi_D1_Mux  = P11_5_SMIF_SPI_DATA1;

static GPIO_PRT_Type* const qspi_D0_Port = GPIO_PRT11;
static const uint32_t qspi_D0_Pin        = 6u;
static const en_hsiom_sel_t qspi_D0_Mux  = P11_6_SMIF_SPI_DATA0;

static GPIO_PRT_Type* const qspi_SCK_Port = GPIO_PRT11;
static const uint32_t qspi_SCK_Pin        = 7u;
static const en_hsiom_sel_t qspi_SCK_Mux  = P11_7_SMIF_SPI_CLK;

/* SMIF GPIO pin configurations */
static cy_stc_gpio_pin_config_t qspi_ss_config = {
    .outVal    = 1,
    .driveMode = CY_GPIO_DM_STRONG_IN_OFF,
    .hsiom     = P11_2_SMIF_SPI_SELECT0,
    .intEdge   = CY_GPIO_INTR_DISABLE,
    .intMask   = 0UL,
    .vtrip     = CY_GPIO_VTRIP_CMOS,
    .slewRate  = CY_GPIO_SLEW_FAST,
    .driveSel  = CY_GPIO_DRIVE_1_2,
    .vregEn    = 0UL,
    .ibufMode  = 0UL,
    .vtripSel  = 0UL,
    .vrefSel   = 0UL,
    .vohSel    = 0UL,
};

static const cy_stc_gpio_pin_config_t qspi_d3_config = {
    .outVal    = 1,
    .driveMode = CY_GPIO_DM_STRONG,
    .hsiom     = P11_3_SMIF_SPI_DATA3,
    .intEdge   = CY_GPIO_INTR_DISABLE,
    .intMask   = 0UL,
    .vtrip     = CY_GPIO_VTRIP_CMOS,
    .slewRate  = CY_GPIO_SLEW_FAST,
    .driveSel  = CY_GPIO_DRIVE_1_2,
    .vregEn    = 0UL,
    .ibufMode  = 0UL,
    .vtripSel  = 0UL,
    .vrefSel   = 0UL,
    .vohSel    = 0UL,
};

static const cy_stc_gpio_pin_config_t qspi_d2_config = {
    .outVal    = 1,
    .driveMode = CY_GPIO_DM_STRONG,
    .hsiom     = P11_4_SMIF_SPI_DATA2,
    .intEdge   = CY_GPIO_INTR_DISABLE,
    .intMask   = 0UL,
    .vtrip     = CY_GPIO_VTRIP_CMOS,
    .slewRate  = CY_GPIO_SLEW_FAST,
    .driveSel  = CY_GPIO_DRIVE_1_2,
    .vregEn    = 0UL,
    .ibufMode  = 0UL,
    .vtripSel  = 0UL,
    .vrefSel   = 0UL,
    .vohSel    = 0UL,
};

static const cy_stc_gpio_pin_config_t qspi_d1_config = {
    .outVal    = 1,
    .driveMode = CY_GPIO_DM_STRONG,
    .hsiom     = P11_5_SMIF_SPI_DATA1,
    .intEdge   = CY_GPIO_INTR_DISABLE,
    .intMask   = 0UL,
    .vtrip     = CY_GPIO_VTRIP_CMOS,
    .slewRate  = CY_GPIO_SLEW_FAST,
    .driveSel  = CY_GPIO_DRIVE_1_2,
    .vregEn    = 0UL,
    .ibufMode  = 0UL,
    .vtripSel  = 0UL,
    .vrefSel   = 0UL,
    .vohSel    = 0UL,
};

static const cy_stc_gpio_pin_config_t qspi_d0_config = {
    .outVal    = 1,
    .driveMode = CY_GPIO_DM_STRONG,
    .hsiom     = P11_6_SMIF_SPI_DATA0,
    .intEdge   = CY_GPIO_INTR_DISABLE,
    .intMask   = 0UL,
    .vtrip     = CY_GPIO_VTRIP_CMOS,
    .slewRate  = CY_GPIO_SLEW_FAST,
    .driveSel  = CY_GPIO_DRIVE_1_2,
    .vregEn    = 0UL,
    .ibufMode  = 0UL,
    .vtripSel  = 0UL,
    .vrefSel   = 0UL,
    .vohSel    = 0UL,
};

static const cy_stc_gpio_pin_config_t qspi_sck_config = {
    .outVal    = 1,
    .driveMode = CY_GPIO_DM_STRONG_IN_OFF,
    .hsiom     = P11_7_SMIF_SPI_CLK,
    .intEdge   = CY_GPIO_INTR_DISABLE,
    .intMask   = 0UL,
    .vtrip     = CY_GPIO_VTRIP_CMOS,
    .slewRate  = CY_GPIO_SLEW_FAST,
    .driveSel  = CY_GPIO_DRIVE_1_2,
    .vregEn    = 0UL,
    .ibufMode  = 0UL,
    .vtripSel  = 0UL,
    .vrefSel   = 0UL,
    .vohSel    = 0UL,
};

/* SMIF block configuration */
static const cy_stc_smif_config_t QSPI_config = {
    .mode          = (uint32_t)CY_SMIF_NORMAL,
    .deselectDelay = 1,
    .rxClockSel    = (uint32_t)CY_SMIF_SEL_INV_INTERNAL_CLK,
    .blockEvent    = (uint32_t)CY_SMIF_BUS_ERROR,
};

/**
 * @brief Initialize external QSPI flash hardware
 *
 * Configures GPIO pins and the SMIF clock, and initializes the SMIF
 * block and external memory device. Only blocking SMIF calls are used, so
 * no SMIF interrupt is set up.
 *
 * Initialization steps:
 * 1. Configure QSPI data, clock, and slave-select GPIO pins
 * 2. Enable SMIF HF clock (CLK_HF2) with divide-by-4
 * 3. Initialize SMIF block in normal mode
 * 4. Enable SMIF block
 * 5. Initialize external memory configuration with retry
 *
 * @return CY_SMIF_SUCCESS on success, error code on failure
 */
/* ext_flash_erase() erases whole sectors, so they must not exceed a wolfBoot
 * sector */
static int psoc6_qspi_geometry_ok(uint32_t erase_size)
{
    return (erase_size != 0U) && ((WOLFBOOT_SECTOR_SIZE % erase_size) == 0U);
}

cy_en_smif_status_t hal_ext_flash_init(void)
{
    cy_en_smif_status_t st;
    uint32_t try_count;

    /* Step 1: Configure QSPI GPIO pins */
    (void)Cy_GPIO_Pin_Init(qspi_D3_Port, qspi_D3_Pin, &qspi_d3_config);
    Cy_GPIO_SetHSIOM(qspi_D3_Port, qspi_D3_Pin, qspi_D3_Mux);

    (void)Cy_GPIO_Pin_Init(qspi_D2_Port, qspi_D2_Pin, &qspi_d2_config);
    Cy_GPIO_SetHSIOM(qspi_D2_Port, qspi_D2_Pin, qspi_D2_Mux);

    (void)Cy_GPIO_Pin_Init(qspi_D1_Port, qspi_D1_Pin, &qspi_d1_config);
    Cy_GPIO_SetHSIOM(qspi_D1_Port, qspi_D1_Pin, qspi_D1_Mux);

    (void)Cy_GPIO_Pin_Init(qspi_D0_Port, qspi_D0_Pin, &qspi_d0_config);
    Cy_GPIO_SetHSIOM(qspi_D0_Port, qspi_D0_Pin, qspi_D0_Mux);

    (void)Cy_GPIO_Pin_Init(qspi_SCK_Port, qspi_SCK_Pin, &qspi_sck_config);
    Cy_GPIO_SetHSIOM(qspi_SCK_Port, qspi_SCK_Pin, qspi_SCK_Mux);

    /* Configure SS pin */
    qspi_ss_config.hsiom = qspi_SS_Mux;
    (void)Cy_GPIO_Pin_Init(qspi_SS_Port, qspi_SS_Pin, &qspi_ss_config);
    Cy_GPIO_SetHSIOM(qspi_SS_Port, qspi_SS_Pin, qspi_SS_Mux);

    /* Step 2: Enable SMIF HF clock (CLK_HF2) sourced from CLK_PATH0 */
    (void)Cy_SysClk_ClkHfSetSource(CY_SYSCLK_CLKHF_IN_CLKPATH2,
                                   CY_SYSCLK_CLKHF_IN_CLKPATH0);
    (void)Cy_SysClk_ClkHfSetDivider(CY_SYSCLK_CLKHF_IN_CLKPATH2,
                                    CY_SMIF_SYSCLK_HFCLK_DIVIDER);
    (void)Cy_SysClk_ClkHfEnable(CY_SYSCLK_CLKHF_IN_CLKPATH2);

    /* Step 3: Initialize SMIF block */
    st = Cy_SMIF_Init(QSPIPort, &QSPI_config, CY_SMIF_INIT_TIMEOUT_US,
                      &smif_context);
    if (st != CY_SMIF_SUCCESS) {
        return st;
    }

    /* Step 4: Enable SMIF block */
    Cy_SMIF_Enable(QSPIPort, &smif_context);

    /* Step 5: Initialize external memory via SFDP with retry */
    smif_blk_config = &smifBlockConfig_sfdp;
    try_count       = CY_SMIF_INIT_TRY_COUNT;
    do {
        st = Cy_SMIF_MemInit(QSPIPort, smif_blk_config, &smif_context);
        try_count--;
        if (st != CY_SMIF_SUCCESS) {
            Cy_SysLib_Delay(CY_SMIF_INIT_RETRY_DELAY_MS);
        }
    } while ((st != CY_SMIF_SUCCESS) && (try_count > 0U));

    if ((st == CY_SMIF_SUCCESS) && !psoc6_qspi_geometry_ok(
            smif_blk_config->memConfig[0]->deviceCfg->eraseSize)) {
        wolfBoot_printf("QSPI erase size 0x%x does not divide "
            "WOLFBOOT_SECTOR_SIZE\n",
            smif_blk_config->memConfig[0]->deviceCfg->eraseSize);
        st = CY_SMIF_BAD_PARAM;
    }
    return st;
}

#ifdef __WOLFBOOT
/**
 * @brief Deinitialize external QSPI flash hardware
 *
 * Reverses the initialization performed by hal_ext_flash_init().
 * Deinitializes SMIF memory, disables the SMIF block, disables the
 * HF clock, and deinitializes GPIO ports.
 */
static void hal_ext_flash_deinit(void)
{
    Cy_SMIF_MemDeInit(QSPIPort);
    Cy_SMIF_Disable(QSPIPort);

    (void)Cy_SysClk_ClkHfDisable(CY_SYSCLK_CLKHF_IN_CLKPATH2);

    Cy_GPIO_Port_Deinit(qspi_SS_Port);
    Cy_GPIO_Port_Deinit(qspi_SCK_Port);
    Cy_GPIO_Port_Deinit(qspi_D0_Port);
    Cy_GPIO_Port_Deinit(qspi_D1_Port);
    Cy_GPIO_Port_Deinit(qspi_D2_Port);
    Cy_GPIO_Port_Deinit(qspi_D3_Port);
}
#endif

#endif /* EXT_FLASH */

#ifdef DUALBANK_SWAP

#define FLASH_BANK1_BASE (PSOC6_FLASH_BASE)
/* Second bank in the single-bank map, before dual-bank mode is enabled */
#define FLASH_BANK2_COPY (PSOC6_FLASH_BASE + PSOC6_FLASH_BANK_SIZE)

typedef struct active_bank {
    uint32_t magic;
    uint32_t mapping;
    uint32_t seq;
    uint32_t check;
} active_bank;

/* Two records in separate work-flash rows: a swap writes the older one, so a
 * power cut never leaves the device without a valid mapping */
#define BANK_RECORD_SLOTS 2U
#define BANK_RECORD_ADDR(slot) (EMULATED_EEPROM_START + ((slot) * ROW_SIZE))

int RAMFUNCTION bankmode_init(void);
#endif

/* Global write buffer to avoid stack allocation and enable RAMFUNCTION usage */
static uint8_t psoc6_write_buffer[ROW_SIZE] __attribute__((aligned(4)));

#if defined(PSOC6_CM4_APP) && \
    (((WOLFBOOT_PARTITION_BOOT_ADDRESS + IMAGE_HEADER_SIZE) % \
      PSOC6_CM4_VTOR_ALIGN) != 0)
#error "PSOC6_CM4_APP needs a 1 KB aligned image: set IMAGE_HEADER_SIZE=1024"
#endif

static int RAMFUNCTION psoc6_in_region(uint32_t address, uint32_t len,
    uint32_t base, uint32_t size)
{
    return (address >= base) && (len <= size) &&
           ((address - base) <= (size - len));
}

/* ProgramRow needs an erased row; WriteRow erases first */
static int RAMFUNCTION psoc6_write_row(uint32_t row)
{
    const uint32_t *cur = (const uint32_t*)PSOC6_FLASH_PTR(row);
    const uint32_t *buf = (const uint32_t*)psoc6_write_buffer;
    uint32_t i;
    int same = 1;
    int erased = 1;
    int ret = 0;

    for (i = 0; i < (ROW_SIZE / sizeof(uint32_t)); i++) {
        if (cur[i] != buf[i]) {
            same = 0;
        }
        if (cur[i] != 0U) {
            erased = 0;
        }
    }
    if (!same) {
        if (erased) {
            ret = (int)Cy_Flash_ProgramRow(row, buf);
        }
        else {
            ret = (int)Cy_Flash_WriteRow(row, buf);
        }
        Cy_SysLib_ClearFlashCacheAndBuffer();
    }
    /* wolfBoot checks for negative errors; PDL status codes are positive */
    return (ret == 0) ? 0 : -1;
}

static int RAMFUNCTION psoc6_is_erased(uint32_t address, uint32_t len)
{
    const uint32_t *p = (const uint32_t*)PSOC6_FLASH_PTR(address);
    uint32_t i;

    for (i = 0; i < (len / sizeof(uint32_t)); i++) {
        if (p[i] != 0U) {
            return 0;
        }
    }
    return 1;
}

int RAMFUNCTION psoc6_flash_range_ok(uint32_t address, uint32_t len)
{
    int ok = 0;

    if (len == 0U) {
        ok = 0;
    }
    else if (psoc6_in_region(address, len, PSOC6_FLASH_BASE,
            PSOC6_FLASH_SIZE) ||
        psoc6_in_region(address, len, PSOC6_WFLASH_BASE, PSOC6_WFLASH_SIZE)) {
        ok = 1;
    }
#ifdef DUALBANK_SWAP
    else if (psoc6_in_region(address, len, PSOC6_FLASH_BANK2_BASE,
            PSOC6_FLASH_BANK_SIZE)) {
        ok = 1;
    }
#endif
    return ok;
}

#ifdef DEBUG_UART
static void psoc6_uart_putc(char c)
{
    while (Cy_SCB_UART_Put(PSOC6_UART_SCB, (uint32_t)(uint8_t)c) == 0UL) {
    }
}

#ifdef __WOLFBOOT
static const cy_stc_scb_uart_config_t psoc6_uart_cfg = {
    .uartMode           = CY_SCB_UART_STANDARD,
    .oversample         = PSOC6_UART_OVERSAMPLE,
    .dataWidth          = 8UL,
    .parity             = CY_SCB_UART_PARITY_NONE,
    .stopBits           = CY_SCB_UART_STOP_BITS_1,
    .breakWidth         = 11UL,
    .ctsPolarity        = CY_SCB_UART_ACTIVE_LOW,
    .rtsPolarity        = CY_SCB_UART_ACTIVE_LOW,
};

void uart_init(void)
{
    Cy_GPIO_Pin_FastInit(PSOC6_UART_PORT, PSOC6_UART_TX_PIN,
        CY_GPIO_DM_STRONG_IN_OFF, 1UL, PSOC6_UART_TX_MUX);
    Cy_GPIO_Pin_FastInit(PSOC6_UART_PORT, PSOC6_UART_RX_PIN,
        CY_GPIO_DM_HIGHZ, 1UL, PSOC6_UART_RX_MUX);

    (void)Cy_SysClk_PeriphAssignDivider(PSOC6_UART_PCLK,
        CY_SYSCLK_DIV_16_5_BIT, PSOC6_UART_DIV_NUM);
    (void)Cy_SysClk_PeriphSetFracDivider(CY_SYSCLK_DIV_16_5_BIT,
        PSOC6_UART_DIV_NUM, PSOC6_UART_DIV_INT, PSOC6_UART_DIV_FRAC);
    (void)Cy_SysClk_PeriphEnableDivider(CY_SYSCLK_DIV_16_5_BIT,
        PSOC6_UART_DIV_NUM);

    if (Cy_SCB_UART_Init(PSOC6_UART_SCB, &psoc6_uart_cfg, NULL) ==
            CY_SCB_UART_SUCCESS) {
        Cy_SCB_UART_Enable(PSOC6_UART_SCB);
    }
}

static void psoc6_uart_flush(void)
{
    while (!Cy_SCB_UART_IsTxComplete(PSOC6_UART_SCB)) {
    }
}
#endif /* __WOLFBOOT */

void uart_write(const char* buf, unsigned int sz)
{
    unsigned int i;

    for (i = 0; i < sz; i++) {
        if (buf[i] == '\n') {
            psoc6_uart_putc('\r');
        }
        psoc6_uart_putc(buf[i]);
    }
}
#endif /* DEBUG_UART */

#if defined(BOOT_BENCHMARK) && defined(__WOLFBOOT)
static const cy_stc_tcpwm_counter_config_t psoc6_timer_cfg = {
    .period             = 0xFFFFFFFFUL,
    .clockPrescaler     = CY_TCPWM_COUNTER_PRESCALER_DIVBY_1,
    .runMode            = CY_TCPWM_COUNTER_CONTINUOUS,
    .countDirection     = CY_TCPWM_COUNTER_COUNT_UP,
    .compareOrCapture   = CY_TCPWM_COUNTER_MODE_CAPTURE,
    .interruptSources   = CY_TCPWM_INT_NONE,
    .captureInputMode   = CY_TCPWM_INPUT_LEVEL,
    .captureInput       = CY_TCPWM_INPUT_0,
    .reloadInputMode    = CY_TCPWM_INPUT_LEVEL,
    .reloadInput        = CY_TCPWM_INPUT_0,
    .startInputMode     = CY_TCPWM_INPUT_LEVEL,
    .startInput         = CY_TCPWM_INPUT_0,
    .stopInputMode      = CY_TCPWM_INPUT_LEVEL,
    .stopInput          = CY_TCPWM_INPUT_0,
    .countInputMode     = CY_TCPWM_INPUT_LEVEL,
    .countInput         = CY_TCPWM_INPUT_1,
};

/* TCPWM0 counter 0 is 32 bits wide; extend it in software across wraps */
uint64_t hal_get_timer_us(void)
{
    static int started;
    static uint32_t last;
    static uint32_t high;
    uint32_t now;

    if (!started) {
        (void)Cy_SysClk_PeriphAssignDivider(PSOC6_TIMER_PCLK,
            CY_SYSCLK_DIV_16_BIT, PSOC6_TIMER_DIV_NUM);
        (void)Cy_SysClk_PeriphSetDivider(CY_SYSCLK_DIV_16_BIT,
            PSOC6_TIMER_DIV_NUM, PSOC6_TIMER_DIV);
        (void)Cy_SysClk_PeriphEnableDivider(CY_SYSCLK_DIV_16_BIT,
            PSOC6_TIMER_DIV_NUM);
        if (Cy_TCPWM_Counter_Init(PSOC6_TIMER_TCPWM, PSOC6_TIMER_CNT,
                &psoc6_timer_cfg) == CY_TCPWM_SUCCESS) {
            Cy_TCPWM_Counter_Enable(PSOC6_TIMER_TCPWM, PSOC6_TIMER_CNT);
            Cy_TCPWM_TriggerStart_Single(PSOC6_TIMER_TCPWM, PSOC6_TIMER_CNT);
        }
        started = 1;
    }
    now = Cy_TCPWM_Counter_GetCounter(PSOC6_TIMER_TCPWM, PSOC6_TIMER_CNT);
    if (now < last) {
        high++;
    }
    last = now;
    return ((uint64_t)high << 32) | now;
}
#endif /* BOOT_BENCHMARK && __WOLFBOOT */

#ifdef __WOLFBOOT
static const cy_stc_pll_manual_config_t srss_0_clock_0_pll_0_pllConfig = {
    .feedbackDiv  = 100,
    .referenceDiv = 2,
    .outputDiv    = 4,
    .lfMode       = false,
    .outputMode   = CY_SYSCLK_FLLPLL_OUTPUT_AUTO,
};

/**
 * @brief Configure PLL for 100 MHz system clock
 *
 * Sets up the Phase-Locked Loop (PLL) to generate a 100 MHz clock from
 * the Internal Main Oscillator (IMO). Configures clock paths and dividers
 * for CM0+, CM4, and peripheral clocks.
 *
 * Clock configuration:
 * - Clock path 1 source: IMO (8 MHz)
 * - PLL output: 100 MHz (8 MHz * 100 / 2 / 4 = 100 MHz)
 * - CLK_HF0: PLL output (100 MHz)
 * - CM4 (fast clock): No division (100 MHz)
 * - Peripheral clock: No division (100 MHz)
 * - CM0+ (slow clock): No division (100 MHz)
 *
 * @note This function will hang in an infinite loop if PLL configuration
 *       or enable fails.
 */
static void hal_set_pll(void)
{
    /*Set clock path 1 source to IMO, this feeds PLL1*/
    Cy_SysClk_ClkPathSetSource(1U, CY_SYSCLK_CLKPATH_IN_IMO);

    /*Set the input for CLK_HF0 to the output of the PLL, which is on clock path
     * 1*/
    Cy_SysClk_ClkHfSetSource(0U, CY_SYSCLK_CLKHF_IN_CLKPATH1);
    Cy_SysClk_ClkHfSetDivider(0U, CY_SYSCLK_CLKHF_NO_DIVIDE);

    /*Set divider for CM4 clock to 0, might be able to lower this to save power
     * if needed*/
    Cy_SysClk_ClkFastSetDivider(0U);

    /*Set divider for peripheral and CM0 clock to 0 - This must be 0 to get
     * fastest clock to CM0*/
    Cy_SysClk_ClkPeriSetDivider(0U);

    /*Set divider for CM0 clock to 0*/
    Cy_SysClk_ClkSlowSetDivider(0U);

    /*Set flash memory wait states */
    Cy_SysLib_SetWaitStates(false, 100);

    /*Configure PLL for 100 MHz*/
    if (CY_SYSCLK_SUCCESS !=
        Cy_SysClk_PllManualConfigure(1U, &srss_0_clock_0_pll_0_pllConfig)) {
        while (1)
            ;
    }
    /*Enable PLL*/
    if (CY_SYSCLK_SUCCESS != Cy_SysClk_PllEnable(1U, 10000u)) {
        while (1)
            ;
    }
}

/**
 * @brief Initialize hardware abstraction layer
 *
 * Performs essential hardware initialization for the PSoC6 bootloader:
 * 1. Sets the Vector Table Offset Register (VTOR) to flash base address
 * 2. Initializes Cypress PDL (Peripheral Driver Library)
 * 3. Initializes the flash subsystem for program/erase operations
 * 4. Configures PLL for 100 MHz system clock
 * 5. Initializes crypto hardware acceleration (if WOLFSSL_PSOC6_CRYPTO enabled)
 *
 * @note Must be called early in the boot process before any flash operations
 *       or cryptographic functions.
 */
/* The ROM runs every flash call through the CM0+ NMI vector, so the table
 * must outlive a self-update erasing the bootloader; the PDL also installs
 * ISRs only when VTOR points at __ramVectors */
static void psoc6_ram_vectors(void)
{
    const cy_israddress *flash_iv =
        (const cy_israddress*)PSOC6_FLASH_PTR(FLASH_BASE_ADDRESS);
    uint32_t i;

    for (i = 0; i < PSOC6_CM0P_SYS_VECTORS; i++) {
        __ramVectors[i] = flash_iv[i];
    }
    for (; i < PSOC6_CM0P_VECTORS; i++) {
        __ramVectors[i] = flash_iv[PSOC6_HARDFAULT_VECTOR];
    }
    SCB->VTOR = (uint32_t)(uintptr_t)__ramVectors;
    __DSB();
}

void hal_init(void)
{
    psoc6_ram_vectors();

    Cy_PDL_Init(CY_DEVICE_CFG);
    Cy_Flash_Init();
    hal_set_pll();
#ifdef DEBUG_UART
    uart_init();
#endif
#ifdef DUALBANK_SWAP
    bankmode_init();
#endif

#ifdef EXT_FLASH
    if (hal_ext_flash_init() != CY_SMIF_SUCCESS) {
        wolfBoot_printf("hal_init: QSPI ext flash init failed\n");
        while (1)
            ;
    }
#endif

#ifdef WOLFSSL_PSOC6_CRYPTO
    psoc6_crypto_port_init();
#endif
}

void hal_prepare_boot(void)
{
#ifdef EXT_FLASH
    hal_ext_flash_deinit();
#endif
#ifdef DEBUG_UART
    psoc6_uart_flush();
#endif
}

#ifdef WOLFBOOT_BOOT_REMOTE_CORE
/* The CM0+ stays parked so it can service the CM4's SROM flash calls (NMI) */
void hal_boot_remote_core(const uint32_t *app_offset)
{
    Cy_SysEnableCM4((uint32_t)(uintptr_t)app_offset);
    while (1) {
        __WFI();
    }
}
#endif

#endif

/**
 * @brief Write data to internal flash memory
 *
 * Writes data to the PSoC6 internal flash with automatic handling of
 * unaligned addresses and partial row writes. Uses read-modify-write
 * for rows that are not fully overwritten.
 *
 * @param address Flash address to write to (must be >= FLASH_BASE_ADDRESS)
 * @param data    Pointer to source data buffer
 * @param len     Number of bytes to write (must be > 0)
 *
 * @return 0 on success, -1 on invalid parameters, or PDL error code on
 *         flash programming failure
 *
 * @note Uses global psoc6_write_buffer for row operations to avoid stack
 *       allocation.
 * @note For optimal performance, align writes to ROW_SIZE (512 bytes)
 *       boundaries.
 */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t* data, int len)
{
    uint32_t write_len;
    uint32_t row_start = address & ~(ROW_SIZE - 1);
    int ret            = 0;
    uint32_t offset    = address - row_start;

    /* Validate input parameters (address checked against base, PDL handles bank
     * mapping) */
    if ((data == NULL) || (len <= 0) ||
        !psoc6_flash_range_ok(row_start,
            ((uint32_t)len + offset + ROW_SIZE - 1U) & ~(ROW_SIZE - 1U)))
        return -1;

    /* Handle unaligned start address with read-modify-write */
    if (address != row_start) {
        /* Read existing row from flash */
        memcpy(psoc6_write_buffer, PSOC6_FLASH_PTR(row_start), ROW_SIZE);

        /* Merge new data at offset, limiting to row boundary */
        write_len = ((ROW_SIZE - offset) < (uint32_t)len) ? (ROW_SIZE - offset)
                                                          : (uint32_t)len;
        memcpy(&psoc6_write_buffer[offset], data, write_len);

        /* Program the updated data into flash row */
        ret = psoc6_write_row(row_start);
        if (ret) {
            return ret;
        }

        row_start += ROW_SIZE;
        data += write_len;
        len -= write_len;
    }

    /* Write remaining rows (row-aligned) */
    while (len > 0) {
        /* Choose write length for this row */
        write_len = ((uint32_t)len >= ROW_SIZE) ? ROW_SIZE : (uint32_t)len;

        if (write_len == ROW_SIZE) {
            /* Full-row write - avoid unnecessary read */
            memcpy(psoc6_write_buffer, data, ROW_SIZE);
        }
        else {
            /* Read-modify-write for partial row support */
            memcpy(psoc6_write_buffer, PSOC6_FLASH_PTR(row_start), ROW_SIZE);
            memcpy(psoc6_write_buffer, data, write_len);
        }

        /* Program the updated data into flash row */
        ret = psoc6_write_row(row_start);
        if (ret) {
            return ret;
        }

        row_start += ROW_SIZE;
        data += write_len;
        len -= (int)write_len;
    }

    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

/**
 * @brief Erase internal flash memory with optimized erase size selection
 *
 * Erases flash memory by automatically selecting the most efficient erase
 * operation based on address alignment and remaining length:
 * - Sector erase (largest): When aligned to FLASH_SECTOR_SIZE and len >= sector
 * - Subsector erase (medium): When aligned to FLASH_SUBSECTOR_SIZE and len >=
 * subsector
 * - Row erase (smallest): For unaligned addresses or small regions
 *
 * @param address Starting flash address (must be row-aligned and >=
 * FLASH_BASE_ADDRESS)
 * @param len     Number of bytes to erase (must be > 0)
 *
 * @return 0 on success, -1 on invalid parameters or erase failure
 *
 * @note Address must be aligned to ROW_SIZE (512 bytes)
 * @note This function optimizes erase time by using larger erase operations
 *       when possible.
 */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    cy_rslt_t status;
    uint32_t step;
    int ret = 0;

    /* Validate input parameters */
    if ((len <= 0) || (address % ROW_SIZE) ||
        !psoc6_flash_range_ok(address,
            ((uint32_t)len + ROW_SIZE - 1U) & ~(ROW_SIZE - 1U)))
        return -1;

    /* Select optimal erase size based on alignment and length */
    while ((len > 0) && (ret == 0)) {
        status = CY_RSLT_SUCCESS;
        if ((address % FLASH_SECTOR_SIZE == 0) &&
            ((uint32_t)len >= FLASH_SECTOR_SIZE)) {
            step = FLASH_SECTOR_SIZE;
            if (!psoc6_is_erased(address, step))
                status = Cy_Flash_EraseSector(address);
        }
        else if ((address % FLASH_SUBSECTOR_SIZE == 0) &&
                 ((uint32_t)len >= FLASH_SUBSECTOR_SIZE)) {
            step = FLASH_SUBSECTOR_SIZE;
            if (!psoc6_is_erased(address, step))
                status = Cy_Flash_EraseSubsector(address);
        }
        else {
            step = ROW_SIZE;
            if (!psoc6_is_erased(address, step))
                status = Cy_Flash_EraseRow(address);
        }
        if (status != CY_RSLT_SUCCESS) {
            ret = -1;
        }
        address += step;
        len -= (int)step;
    }
    Cy_SysLib_ClearFlashCacheAndBuffer();

    return ret;
}

/* With RAM_CODE, wolfBoot links the RAM copy from src/boot_arm.c */
#if !defined(RAM_CODE) || !defined(__WOLFBOOT)
void arch_reboot(void)
{
    NVIC_SystemReset();
}
#endif

/******************************************************************************
 * External Flash Support
 ******************************************************************************/

#ifdef EXT_FLASH
static int RAMFUNCTION psoc6_qspi_offset(uintptr_t address, uint32_t len,
    uint32_t *offset)
{
    const cy_stc_smif_mem_config_t *mem = smif_blk_config->memConfig[0];

    int ret = -1;

    if (psoc6_in_region((uint32_t)address, len, mem->baseAddress,
            mem->deviceCfg->memSize)) {
        *offset = (uint32_t)address - mem->baseAddress;
        ret = 0;
    }
    return ret;
}

int RAMFUNCTION ext_flash_write(uintptr_t address, const uint8_t* data, int len)
{
    uint32_t offset;
    int ret = -1;

    if ((data != NULL) && (len > 0) &&
        (psoc6_qspi_offset(address, (uint32_t)len, &offset) == 0) &&
        (Cy_SMIF_MemWrite(QSPIPort, smif_blk_config->memConfig[0], offset,
            data, (uint32_t)len, &smif_context) == CY_SMIF_SUCCESS)) {
        ret = 0;
    }
    return ret;
}

int RAMFUNCTION ext_flash_read(uintptr_t address, uint8_t* data, int len)
{
    uint32_t offset;
    int ret = -1;

    if ((data != NULL) && (len > 0) &&
        (psoc6_qspi_offset(address, (uint32_t)len, &offset) == 0) &&
        (Cy_SMIF_MemRead(QSPIPort, smif_blk_config->memConfig[0], offset,
            data, (uint32_t)len, &smif_context) == CY_SMIF_SUCCESS)) {
        ret = len;
    }
    return ret;
}

/* hal_ext_flash_init() rejects parts whose erase sector does not divide
 * WOLFBOOT_SECTOR_SIZE, so the PDL never rounds an erase past the request */
int RAMFUNCTION ext_flash_erase(uintptr_t address, int len)
{
    uint32_t offset;
    uint32_t erase_size = smif_blk_config->memConfig[0]->deviceCfg->eraseSize;
    int ret = -1;

    if ((len > 0) && ((address % erase_size) == 0U) &&
        (((uint32_t)len % erase_size) == 0U) &&
        (psoc6_qspi_offset(address, (uint32_t)len, &offset) == 0) &&
        (Cy_SMIF_MemEraseSector(QSPIPort, smif_blk_config->memConfig[0],
            offset, (uint32_t)len, &smif_context) == CY_SMIF_SUCCESS)) {
        ret = 0;
    }
    return ret;
}

void ext_flash_lock(void)
{
}

void ext_flash_unlock(void)
{
}
#endif /* EXT_FLASH */

/******************************************************************************
 * Dual Bank Flash Swap Support
 ******************************************************************************/

#if defined(DUALBANK_SWAP) && defined(__WOLFBOOT)

#if (WOLFBOOT_ORIGIN != FLASH_BANK1_BASE)
#error "The bootloader must point to FLASH_BANK1_BASE"
#endif

static uint32_t RAMFUNCTION psoc6_bank_record_check(const active_bank *rec)
{
    return ~(rec->magic ^ rec->mapping ^ rec->seq);
}

/* Returns the slot of the newest valid record, or -1 */
static int RAMFUNCTION psoc6_bank_record_read(active_bank *out)
{
    active_bank rec;
    uint32_t slot;
    int best = -1;

    for (slot = 0; slot < BANK_RECORD_SLOTS; slot++) {
        memcpy(&rec, PSOC6_FLASH_PTR(BANK_RECORD_ADDR(slot)), sizeof(rec));
        if ((rec.magic == BANKMODE_MAGIC) &&
            (rec.check == psoc6_bank_record_check(&rec)) &&
            ((rec.mapping == CY_FLASH_MAPPING_A) ||
             (rec.mapping == CY_FLASH_MAPPING_B)) &&
            ((best < 0) || ((int32_t)(rec.seq - out->seq) > 0))) {
            *out = rec;
            best = (int)slot;
        }
    }
    return best;
}

static int RAMFUNCTION psoc6_bank_record_write(uint32_t mapping)
{
    active_bank rec;
    uint32_t seq = 1U;
    uint32_t slot = 0U;
    int cur;
    int ret;

    cur = psoc6_bank_record_read(&rec);
    if (cur >= 0) {
        seq = rec.seq + 1U;
        slot = (uint32_t)(1 - cur);
    }
    rec.magic = BANKMODE_MAGIC;
    rec.mapping = mapping;
    rec.seq = seq;
    rec.check = psoc6_bank_record_check(&rec);
    ret = hal_flash_erase(BANK_RECORD_ADDR(slot), ROW_SIZE);
    if (ret == 0) {
        ret = hal_flash_write(BANK_RECORD_ADDR(slot), (const uint8_t*)&rec,
            sizeof(rec));
    }
    return ret;
}

/**
 * @brief Copy bootloader from Bank 1 to Bank 2 for dual-bank operation
 *
 * Creates an identical copy of the bootloader in the second flash bank.
 * This enables the bootloader to execute from either bank when the flash
 * mapping is swapped during firmware updates.
 *
 * The function is idempotent - if both banks already contain identical
 * bootloader code, no flash operations are performed.
 *
 * @note Bootloader size is defined by BOOTLOADER_PARTITION_SIZE.
 * @note This function should be called once during initial setup or after
 *       bootloader updates.
 */
void RAMFUNCTION fork_bootloader(void)
{
    const uint8_t* src = (const uint8_t*)PSOC6_FLASH_PTR(FLASH_BANK1_BASE);
    const uint8_t* dst = (const uint8_t*)PSOC6_FLASH_PTR(FLASH_BANK2_COPY);
    uint32_t i;
    int ret;

    /* Skip fork if bootloader already identical in both banks (idempotent
     * operation) */
    if (memcmp(src, dst, BOOTLOADER_PARTITION_SIZE) == 0)
        return;

    hal_flash_unlock();

    /* Erase entire bootloader region in Bank 2, then copy from Bank 1 row by
     * row */
    ret = hal_flash_erase(FLASH_BANK2_COPY, BOOTLOADER_PARTITION_SIZE);
    if (ret == 0) {
        for (i = 0; i < BOOTLOADER_PARTITION_SIZE; i += ROW_SIZE) {
            ret = hal_flash_write(FLASH_BANK2_COPY + i, src + i, ROW_SIZE);
            if (ret != 0) {
                wolfBoot_printf(
                    "Fork bootloader: write failed at offset 0x%X, ret=%d\n", i,
                    ret);
                break;
            }
        }
    }
    else {
        wolfBoot_printf("Fork bootloader: erase failed, ret=%d\n", ret);
    }

    hal_flash_lock();
}

/* Toggle the bank mapping and record it; with no record, select bank A */
void RAMFUNCTION hal_flash_dualbank_swap(void)
{
    active_bank rec;
    uint32_t mapping = CY_FLASH_MAPPING_A;

    if ((psoc6_bank_record_read(&rec) >= 0) &&
        (rec.mapping == CY_FLASH_MAPPING_A)) {
        mapping = CY_FLASH_MAPPING_B;
    }
    Cy_Flashc_SetMain_Flash_Mapping((cy_en_maptype_t)mapping);
    (void)psoc6_bank_record_write(mapping);

    /* Wait for flash command completion */
    while (FLASHC_FLASH_CMD & FLASHC_V2_FLASH_CMD_INV_Msk)
        ;
}

/* Enter dual-bank mode with the recorded mapping, or bank A on first boot */
int RAMFUNCTION bankmode_init(void)
{
    active_bank rec;
    int ret = 0;

    /* Ensure bootloader is present in both banks */
    fork_bootloader();

    Cy_Flashc_SetMainBankMode(CY_FLASH_DUAL_BANK_MODE);
    __ISB();

    if (psoc6_bank_record_read(&rec) >= 0) {
        Cy_Flashc_SetMain_Flash_Mapping((cy_en_maptype_t)rec.mapping);
    }
    else {
        Cy_Flashc_SetMain_Flash_Mapping(CY_FLASH_MAPPING_A);
        ret = psoc6_bank_record_write(CY_FLASH_MAPPING_A);
    }

    /* Wait for flash controller ready */
    while (FLASHC_FLASH_CMD & FLASHC_V2_FLASH_CMD_INV_Msk)
        ;

    return ret;
}
#endif /* DUALBANK_SWAP && __WOLFBOOT */
