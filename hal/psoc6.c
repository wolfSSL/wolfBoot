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
#include "psoc6_02_config.h"
#include <target.h>

#ifdef WOLFSSL_PSOC6_CRYPTO
#include "wolfssl/wolfcrypt/port/cypress/psoc6_crypto.h"
#endif

#include "cy_flash.h"
#include "cy_sysclk.h"
#include "cy_syslib.h"

#define EMULATED_EEPROM_START (CY_EM_EEPROM_BASE)
#define ROW_SIZE (CY_FLASH_SIZEOF_ROW)
#define FLASH_SUBSECTOR_SIZE (CY_FLASH_SIZEOF_ROW * 8)
#define FLASH_SECTOR_SIZE (CY_FLASH_SIZEOF_ROW * CPUSS_FLASHC_SONOS_MAIN_ROWS)
#define FLASH_BASE_ADDRESS (CY_FLASH_BASE)
#define FLASH_SIZE (CY_FLASH_SIZE)
#define CPU_FREQ (100000000)

/* Magic number constants */
#define EXT_FLASH_TMP_MAGIC (0x4C465845U) /* 'EXFL' */
#define BANKMODE_MAGIC (0x4B424C44U)      /* 'DLBK' */
#define EXT_FLASH_WORD_ERASED_VALUE (0xFFFFFFFFU)

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

#define TMP_SECTOR_INFO_ADDR EMULATED_EEPROM_START

/**
 * Address of the temporary sector in external flash.
 *
 * External flash has a larger minimum erase size (determined at runtime via
 * SFDP, e.g. 0x40000 bytes for S25FL512S) than internal flash rows. Since
 * read-modify-erase-write cannot use SRAM as a temporary buffer due to size
 * constraints, we use a dedicated sector in external flash for temporary
 * storage during sector updates.
 */
#define SWAP_TMP_SECTOR_ADDR (WOLFBOOT_PARTITION_UPDATE_ADDRESS + CY_FLASH_SIZE)

typedef struct tmp_sector_info {
    uint32_t magic;
    uint32_t sector_address;
} tmp_sector_info;

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

#if CY_CPU_CORTEX_M0P
/* CM0+ SMIF interrupt configuration */
static cy_stc_sysint_t smifIntConfig = {
    /* ATTENTION: make sure proper Interrupts configured for CM0p or M4 cores */
    .intrSrc      = NvicMux7_IRQn,
    .cm0pSrc      = smif_interrupt_IRQn,
    .intrPriority = 1,
};
#endif

#if CY_CPU_CORTEX_M0P
static void Isr_SMIF(void)
{
    Cy_SMIF_Interrupt(QSPIPort, &smif_context);
}
#endif

/**
 * @brief Initialize external QSPI flash hardware
 *
 * Configures GPIO pins, SMIF clock, interrupt, and initializes the SMIF
 * block and external memory device.
 *
 * Initialization steps:
 * 1. Configure QSPI data, clock, and slave-select GPIO pins
 * 2. Enable SMIF HF clock (CLK_HF2) with divide-by-4
 * 3. Setup SMIF interrupt for CM0+
 * 4. Initialize SMIF block in normal mode
 * 5. Enable SMIF block
 * 6. Initialize external memory configuration with retry
 *
 * @return CY_SMIF_SUCCESS on success, error code on failure
 */
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

    /* Step 3: Setup the interrupt for the SMIF block.  For the CM0+ there
     * is a two stage process to setup the interrupts. */
#if CY_CPU_CORTEX_M0P
    (void)Cy_SysInt_Init(&smifIntConfig, Isr_SMIF);
#endif

    /* Step 4: Initialize SMIF block */
    st = Cy_SMIF_Init(QSPIPort, &QSPI_config, CY_SMIF_INIT_TIMEOUT_US,
                      &smif_context);
    if (st != CY_SMIF_SUCCESS) {
        return st;
    }

#if CY_CPU_CORTEX_M0P
    NVIC_EnableIRQ(
        smifIntConfig.intrSrc); /* Finally, Enable the SMIF interrupt */
#endif

    /* Step 5: Enable SMIF block */
    Cy_SMIF_Enable(QSPIPort, &smif_context);

    /* Step 6: Initialize external memory via SFDP with retry */
    smif_blk_config = &smifBlockConfig_sfdp;
    try_count       = CY_SMIF_INIT_TRY_COUNT;
    do {
        st = Cy_SMIF_MemInit(QSPIPort, smif_blk_config, &smif_context);
        try_count--;
        if (st != CY_SMIF_SUCCESS) {
            Cy_SysLib_Delay(CY_SMIF_INIT_RETRY_DELAY_MS);
        }
    } while ((st != CY_SMIF_SUCCESS) && (try_count > 0U));

    return st;
}

/**
 * @brief Deinitialize external QSPI flash hardware
 *
 * Reverses the initialization performed by hal_ext_flash_init().
 * Deinitializes SMIF memory, disables the SMIF block, disables the
 * HF clock, disconnects interrupts (on CM0+), and deinitializes
 * GPIO ports. Matches mcuboot's qspi_deinit() sequence.
 */
static void hal_ext_flash_deinit(void)
{
    Cy_SMIF_MemDeInit(QSPIPort);
    Cy_SMIF_Disable(QSPIPort);

    (void)Cy_SysClk_ClkHfDisable(CY_SYSCLK_CLKHF_IN_CLKPATH2);

#if CY_CPU_CORTEX_M0P
    NVIC_DisableIRQ(smifIntConfig.intrSrc);
    Cy_SysInt_DisconnectInterruptSource(smifIntConfig.intrSrc,
                                        smifIntConfig.cm0pSrc);
#endif

    Cy_GPIO_Port_Deinit(qspi_SS_Port);
    Cy_GPIO_Port_Deinit(qspi_SCK_Port);
    Cy_GPIO_Port_Deinit(qspi_D0_Port);
    Cy_GPIO_Port_Deinit(qspi_D1_Port);
    Cy_GPIO_Port_Deinit(qspi_D2_Port);
    Cy_GPIO_Port_Deinit(qspi_D3_Port);
}

#endif /* EXT_FLASH */

#ifdef DUALBANK_SWAP

#define FLASH_BANK1_BASE (0x10000000) /* Base address of Flash Bank1 */
#define FLASH_BANK2_BASE (0x12000000) /* Base address of Flash Bank2 */

typedef struct active_bank {
    uint32_t magic;
    uint32_t mapping; /* Changed from cy_en_maptype_t to uint32_t for 32-bit
                         alignment */
} active_bank;

/* EXT_FLASH and DUALBANK_SWAP are mutually exclusive, so its safe to overwrite
 * the data at same address. */
#define ACTIVE_BANK_MODE_DATA_ADDRESS (EMULATED_EEPROM_START)

int RAMFUNCTION bankmode_init(void);
#endif

/* Global write buffer to avoid stack allocation and enable RAMFUNCTION usage */
static uint8_t psoc6_write_buffer[ROW_SIZE];

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
void hal_init(void)
{
#define VTOR (*(volatile uint32_t*)(0xE000ED08))
    VTOR = FLASH_BASE_ADDRESS;
#undef VTOR

#ifdef DUALBANK_SWAP
    bankmode_init();
#endif

    Cy_PDL_Init(CY_DEVICE_CFG);
    Cy_Flash_Init();
    hal_set_pll();

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
}

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
    if (!data || (len <= 0) || (address < FLASH_BASE_ADDRESS))
        return -1;

    /* Handle unaligned start address with read-modify-write */
    if (address != row_start) {
        /* Read existing row from flash */
        memcpy(psoc6_write_buffer, (const void*)row_start, ROW_SIZE);

        /* Merge new data at offset, limiting to row boundary */
        write_len = ((ROW_SIZE - offset) < (uint32_t)len) ? (ROW_SIZE - offset)
                                                          : (uint32_t)len;
        memcpy(&psoc6_write_buffer[offset], data, write_len);

        /* Program the updated data into flash row */
        ret =
            Cy_Flash_ProgramRow(row_start, (const uint32_t*)psoc6_write_buffer);
        if (ret) {
            wolfBoot_printf(
                "hal_flash_write: ProgramRow failed at 0x%08X ret=%d\n",
                row_start, ret);
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
            memcpy(psoc6_write_buffer, (const void*)row_start, ROW_SIZE);
            memcpy(psoc6_write_buffer, data, write_len);
        }

        /* Program the updated data into flash row */
        ret =
            Cy_Flash_ProgramRow(row_start, (const uint32_t*)psoc6_write_buffer);
        if (ret) {
            wolfBoot_printf(
                "hal_flash_write: ProgramRow failed at 0x%08X ret=%d\n",
                row_start, ret);
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

    /* Validate input parameters */
    if ((len <= 0) || (address % ROW_SIZE) || (address < FLASH_BASE_ADDRESS))
        return -1;

    /* Select optimal erase size based on alignment and length */
    while (len > 0) {
        if ((address % FLASH_SECTOR_SIZE == 0) &&
            ((uint32_t)len >= FLASH_SECTOR_SIZE)) {
            /* Erase full sector (most efficient) */
            status = Cy_Flash_EraseSector(address);
            if (status != CY_RSLT_SUCCESS)
                return -1;

            address += FLASH_SECTOR_SIZE;
            len -= FLASH_SECTOR_SIZE;
        }
        else if ((address % FLASH_SUBSECTOR_SIZE == 0) &&
                 ((uint32_t)len >= FLASH_SUBSECTOR_SIZE)) {
            /* Erase subsector (medium efficiency) */
            status = Cy_Flash_EraseSubsector(address);
            if (status != CY_RSLT_SUCCESS)
                return -1;

            address += FLASH_SUBSECTOR_SIZE;
            len -= FLASH_SUBSECTOR_SIZE;
        }
        else {
            /* Erase single row (smallest granularity) */
            status = Cy_Flash_EraseRow(address);
            if (status != CY_RSLT_SUCCESS)
                return -1;

            address += ROW_SIZE;
            len -= ROW_SIZE;
        }
    }

    return 0;
}

/**
 * @brief Perform a system reset
 *
 * Triggers a software reset of the PSoC6 device by calling NVIC_SystemReset().
 *
 * @note This function does not return.
 */
void RAMFUNCTION arch_reboot(void)
{
    NVIC_SystemReset();
}

/******************************************************************************
 * External Flash Support
 ******************************************************************************/

#ifdef EXT_FLASH

/**
 * @brief Write data to external flash with sector management
 *
 * Handles writing to external flash which has larger erase sectors
 * (e.g. 0x40000 bytes for S25FL512S, determined at runtime via SFDP) than
 * the row size used by wolfBoot. Implements a temporary sector backup
 * mechanism to preserve existing data during partial sector writes.
 *
 * Algorithm:
 * 1. Check if the target sector differs from the cached temporary sector
 * 2. If different: backup entire source sector to temp sector, then erase
 * source
 * 3. If same: check if target area needs erasing (contains non-0xFF data)
 * 4. Write new data to the target location
 *
 * @param address External flash address (must be row-aligned)
 * @param data    Pointer to source data buffer
 * @param len     Number of bytes to write (must be row-aligned)
 *
 * @return 0 on success, -1 on error
 *
 * @note Uses internal emulated EEPROM to track temporary sector state for
 *       power-fail safety.
 * @note SWAP_TMP_SECTOR_ADDR is used as temporary storage during sector
 * updates.
 */
int RAMFUNCTION ext_flash_write(uintptr_t address, const uint8_t* data, int len)
{
    tmp_sector_info info;
    cy_rslt_t status;
    uint32_t base_address;
    uint32_t erase_size;
    uint32_t sector_start;
    uint8_t need_update;
    uint32_t offset;
    int ret = -1;
    int i;

    /* Validate alignment requirements */
    if (!data || (address % ROW_SIZE) || (len % ROW_SIZE))
        return -1;

    base_address = smif_blk_config->memConfig[0]->baseAddress;
    erase_size   = smif_blk_config->memConfig[0]->deviceCfg->eraseSize;

    memcpy(&info, (void*)TMP_SECTOR_INFO_ADDR, sizeof(tmp_sector_info));
    sector_start = address - (address % erase_size);

    /* Determine if temporary sector needs updating (invalid magic or different
     * sector) */
    need_update = ((info.magic != EXT_FLASH_TMP_MAGIC) ||
                   (info.sector_address != sector_start));

    if (need_update) {
        /* Invalidate temp sector info before starting update process */
        if (hal_flash_erase(TMP_SECTOR_INFO_ADDR, ROW_SIZE) != 0)
            goto error;

        /* Erase the temporary sector */
        status = Cy_SMIF_MemEraseSector(QSPIPort, smif_blk_config->memConfig[0],
                                        SWAP_TMP_SECTOR_ADDR - base_address,
                                        erase_size, &smif_context);
        if (status != CY_SMIF_SUCCESS)
            goto error;

        /* Backup source sector to temporary location row by row */
        for (offset = 0; offset < erase_size; offset += ROW_SIZE) {
            status =
                Cy_SMIF_MemRead(QSPIPort, smif_blk_config->memConfig[0],
                                sector_start - base_address + offset,
                                psoc6_write_buffer, ROW_SIZE, &smif_context);
            if (status != CY_SMIF_SUCCESS)
                goto error;

            status =
                Cy_SMIF_MemWrite(QSPIPort, smif_blk_config->memConfig[0],
                                 SWAP_TMP_SECTOR_ADDR - base_address + offset,
                                 psoc6_write_buffer, ROW_SIZE, &smif_context);
            if (status != CY_SMIF_SUCCESS)
                goto error;
        }

        /* Mark temporary sector as valid */
        info.magic          = EXT_FLASH_TMP_MAGIC;
        info.sector_address = sector_start;
        if (hal_flash_write(TMP_SECTOR_INFO_ADDR, (const void*)&info,
                            sizeof(tmp_sector_info)) != 0)
            goto error;

        /* Erase the source sector */
        status = Cy_SMIF_MemEraseSector(QSPIPort, smif_blk_config->memConfig[0],
                                        sector_start - base_address, erase_size,
                                        &smif_context);
        if (status != CY_SMIF_SUCCESS)
            goto error;
    }
    else {
        /* Check if target write region needs erasing (contains non-erased data)
         */
        for (i = 0; i < len; i += ROW_SIZE) {
            status =
                Cy_SMIF_MemRead(QSPIPort, smif_blk_config->memConfig[0],
                                address - base_address + i, psoc6_write_buffer,
                                ROW_SIZE, &smif_context);
            if (status != CY_SMIF_SUCCESS) {
                wolfBoot_printf(
                    "ext_flash_write: SMIF read failed at 0x%08lX\n",
                    (unsigned long)(address + i));
                goto error;
            }

            /* Check if all bytes are in erased state. */
            if ((*(uint32_t*)psoc6_write_buffer !=
                 EXT_FLASH_WORD_ERASED_VALUE) ||
                (memcmp(psoc6_write_buffer + 4, psoc6_write_buffer,
                        ROW_SIZE - 4) != 0)) {
                /* Non-erased data found, erase the containing sector */
                status = Cy_SMIF_MemEraseSector(
                    QSPIPort, smif_blk_config->memConfig[0],
                    sector_start - base_address, erase_size, &smif_context);
                if (status != CY_SMIF_SUCCESS) {
                    wolfBoot_printf("ext_flash_write: SMIF erase failed for "
                                    "sector 0x%08lX\n",
                                    (unsigned long)sector_start);
                    goto error;
                }
                break;
            }
        }
    }

    /* Write the row data */
    status = Cy_SMIF_MemWrite(QSPIPort, smif_blk_config->memConfig[0],
                              address - base_address, data, len, &smif_context);
    if (status != CY_SMIF_SUCCESS)
        goto error;

    ret = 0;

error:
    return ret;
}

/**
 * @brief Read data from external flash with temporary sector redirection
 *
 * Reads data from external flash, automatically redirecting reads to the
 * temporary backup sector if the requested address falls within a sector
 * that is currently being updated (indicated by valid tmp_sector_info).
 *
 * This redirection ensures data consistency during interrupted write
 * operations, as the temporary sector contains the original data before
 * modification.
 *
 * @param address External flash address to read from
 * @param data    Pointer to destination buffer
 * @param len     Number of bytes to read (must be > 0)
 *
 * @return Number of bytes read on success, -1 on invalid parameters or
 *         SMIF failure
 *
 * @note Redirection is transparent to the caller and ensures read consistency
 *       even during power-fail recovery scenarios.
 */
int RAMFUNCTION ext_flash_read(uintptr_t address, uint8_t* data, int len)
{
    tmp_sector_info info;
    uint32_t base_address;
    uint32_t erase_size;
    uint32_t sector_start;
    uint32_t offset;
    cy_rslt_t status;

    if (!data || (len <= 0))
        return -1;

    base_address = smif_blk_config->memConfig[0]->baseAddress;
    erase_size   = smif_blk_config->memConfig[0]->deviceCfg->eraseSize;

    memcpy(&info, (void*)TMP_SECTOR_INFO_ADDR, sizeof(tmp_sector_info));

    /* Redirect to temporary sector if active and address falls within that
     * sector */
    if (info.magic == EXT_FLASH_TMP_MAGIC) {
        sector_start = address - (address % erase_size);
        if (sector_start == info.sector_address) {
            /* Remap to temporary sector while preserving offset within sector
             */
            address = SWAP_TMP_SECTOR_ADDR + (address % erase_size);
        }
    }

    offset = address - base_address;
    status = Cy_SMIF_MemRead(QSPIPort, smif_blk_config->memConfig[0], offset,
                             data, len, &smif_context);
    if (status != CY_SMIF_SUCCESS)
        return -1;
    return len;
}

/**
 * @brief External flash erase operation (no-op)
 *
 * External flash erase is handled by ext_flash_write() due to sector size
 * mismatch. wolfBoot expects row-sized erase granularity, but external
 * flash minimum erase size is much larger (eg. 0x40000 bytes for S25FL512S).
 * The write function manages sector erasing and preserves existing data
 * via temporary sector backup.
 *
 * @param address External flash address (unused)
 * @param len Number of bytes (unused)
 * @return Always returns 0 (success)
 */
int RAMFUNCTION ext_flash_erase(uintptr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}
#endif /* EXT_FLASH */

#ifdef EXT_FLASH

/**
 * @brief Lock external flash and finalize sector operations
 *
 * Invalidates the temporary sector tracking information stored in internal
 * emulated EEPROM. This signals that any pending sector update has been
 * completed and the temporary sector data is no longer needed.
 *
 * Should be called after a series of external flash write operations to
 * commit the changes and free the temporary sector for future use.
 *
 * @note Erases the tmp_sector_info structure if it contains valid magic,
 *       which clears the EXT_FLASH_TMP_MAGIC marker.
 */
void ext_flash_lock(void)
{
    tmp_sector_info info;
    memcpy(&info, (void*)TMP_SECTOR_INFO_ADDR, sizeof(tmp_sector_info));

    /* Clear temporary sector metadata to invalidate any active state */
    if (info.magic == EXT_FLASH_TMP_MAGIC) {
        hal_flash_erase(TMP_SECTOR_INFO_ADDR, ROW_SIZE);
    }
}

void ext_flash_unlock(void)
{
}
#endif /* EXT_FLASH */

/******************************************************************************
 * Dual Bank Flash Swap Support
 ******************************************************************************/

#if defined(DUALBANK_SWAP) && defined(__WOLFBOOT)

#if (WOLFBOOT_BOOTLOADER_START != FLASH_BANK1_BASE)
#error "The bootloader must point to FLASH_BANK1_BASE"
#endif

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
 * @note Bootloader size is defined by WOLFBOOT_BOOTLOADER_SIZE.
 * @note This function should be called once during initial setup or after
 *       bootloader updates.
 */
void RAMFUNCTION fork_bootloader(void)
{
    uint8_t* src = (uint8_t*)FLASH_BANK1_BASE;
    uint8_t* dst = (uint8_t*)(FLASH_BANK1_BASE + (CY_FLASH_SIZE / 2));
    uint32_t i;
    int ret;

    /* Skip fork if bootloader already identical in both banks (idempotent
     * operation) */
    if (memcmp(src, dst, WOLFBOOT_BOOTLOADER_SIZE) == 0)
        return;

    hal_flash_unlock();

    /* Erase entire bootloader region in Bank 2, then copy from Bank 1 row by
     * row */
    ret = hal_flash_erase((uintptr_t)dst, WOLFBOOT_BOOTLOADER_SIZE);
    if (ret == 0) {
        for (i = 0; i < WOLFBOOT_BOOTLOADER_SIZE; i += ROW_SIZE) {
            ret = hal_flash_write((uintptr_t)(dst + i), src + i, ROW_SIZE);
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

/**
 * @brief Swap active flash bank mapping and persist the new state
 *
 * Toggles between Bank A and Bank B flash mapping to switch which physical
 * flash bank appears at the base address. This enables A/B firmware update
 * schemes where new firmware is written to the inactive bank and activated
 * by swapping.
 *
 * The current bank mapping is persisted to emulated EEPROM so it survives
 * reset and can be restored on subsequent boots.
 *
 * State transitions:
 * - Bank A -> Bank B
 * - Bank B -> Bank A
 * - Invalid/first boot -> Bank A (initialized)
 *
 * @note Waits for flash controller to complete before returning.
 * @note Bank mode data is stored at ACTIVE_BANK_MODE_DATA_ADDRESS in
 *       emulated EEPROM.
 */
void RAMFUNCTION hal_flash_dualbank_swap(void)
{
    active_bank bankmode_data = *(active_bank*)ACTIVE_BANK_MODE_DATA_ADDRESS;

    /* Read and validate saved bank mode from emulated EEPROM area */
    if (bankmode_data.magic == BANKMODE_MAGIC) {
        if (bankmode_data.mapping == CY_FLASH_MAPPING_A) {
            bankmode_data.mapping = CY_FLASH_MAPPING_B;
        }
        else {
            bankmode_data.mapping = CY_FLASH_MAPPING_A;
        }
    }
    else {
        /* First boot: initialize to Bank A */
        bankmode_data.magic   = BANKMODE_MAGIC;
        bankmode_data.mapping = CY_FLASH_MAPPING_A;
    }

    /* Set the active bank mapping */
    Cy_Flashc_SetMain_Flash_Mapping(bankmode_data.mapping);

    /* Persist bank mode to emulated EEPROM flash area */
    hal_flash_erase(ACTIVE_BANK_MODE_DATA_ADDRESS, ROW_SIZE);
    hal_flash_write(ACTIVE_BANK_MODE_DATA_ADDRESS, (uint8_t*)&bankmode_data,
                    sizeof(active_bank));

    /* Wait for flash command completion */
    while (FLASHC_FLASH_CMD & FLASHC_V2_FLASH_CMD_INV_Msk)
        ;
}

/**
 * @brief Initialize dual-bank flash mode on system startup
 *
 * Sets up the flash controller for dual-bank operation and restores the
 * previously active bank mapping from persistent storage. Called during
 * bootloader initialization.
 *
 * Initialization sequence:
 * 1. Fork bootloader to Bank 2 (if not already present)
 * 2. Enable dual-bank mode in flash controller
 * 3. Read saved bank mapping from emulated EEPROM
 * 4. Restore bank mapping or initialize to Bank A on first boot
 * 5. Wait for flash controller ready
 *
 * @return 0 on success
 *
 * @note Must be called before any firmware verification or boot operations
 *       that depend on correct flash mapping.
 * @note On first boot (no valid saved state), initializes to Bank A and
 *       persists this configuration.
 */
int RAMFUNCTION bankmode_init(void)
{
    /* Ensure bootloader is present in both banks */
    fork_bootloader();

    /* Enable dual bank mode */
    Cy_Flashc_SetMainBankMode(CY_FLASH_DUAL_BANK_MODE);
    __ASM volatile("isb 0xF" ::: "memory");

    /* Read and restore bank mode from emulated EEPROM flash area */
    active_bank bankmode_data = *(active_bank*)ACTIVE_BANK_MODE_DATA_ADDRESS;
    if (bankmode_data.magic == BANKMODE_MAGIC) {
        /* Restore previously saved bank mapping */
        Cy_Flashc_SetMain_Flash_Mapping(bankmode_data.mapping);
    }
    else {
        /* First boot: initialize to Bank A */
        bankmode_data.magic   = BANKMODE_MAGIC;
        bankmode_data.mapping = CY_FLASH_MAPPING_A;

        /* Save initial bank mode to emulated EEPROM flash area */
        hal_flash_erase(ACTIVE_BANK_MODE_DATA_ADDRESS, ROW_SIZE);
        hal_flash_write(ACTIVE_BANK_MODE_DATA_ADDRESS, (uint8_t*)&bankmode_data,
                        sizeof(active_bank));
    }

    /* Wait for flash controller ready */
    while (FLASHC_FLASH_CMD & FLASHC_V2_FLASH_CMD_INV_Msk)
        ;

    return 0;
}
#endif /* DUALBANK_SWAP && __WOLFBOOT */
