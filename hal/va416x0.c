/* va416x0.c
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

#include <stdint.h>

#include "image.h"
#include "string.h"

#include "va416x0.h"

/* Vorago HAL includes */
#include "va416xx_hal.h"
#include "va416xx_hal_clkgen.h"
#include "va416xx_hal_irqrouter.h"
#include "va416xx_hal_timer.h"
#include "va416xx_hal_ioconfig.h"
#include "va416xx_hal_spi.h"

#ifdef USE_HAL_SPI_FRAM
#include "spi_fram.h"
#endif

#include "printf.h"
#include "loader.h"

const stc_iocfg_pin_cfg_t bootDefaultConfig[] =
{
    {VOR_PORTB,14,en_iocfg_dir_dncare, {{.fltclk=0,.invinp=0,.iewo=0,.opendrn=0,.invout=0,.plevel=0,.pen=0,.pwoa=0,.funsel=3,.iodis=0}}}, /* UART1 TX */
    {VOR_PORTB,15,en_iocfg_dir_dncare, {{.fltclk=0,.invinp=0,.iewo=0,.opendrn=0,.invout=0,.plevel=0,.pen=0,.pwoa=0,.funsel=3,.iodis=0}}}, /* UART1 RX */

    {VOR_PORTG, 0,en_iocfg_dir_dncare, {{.fltclk=0,.invinp=0,.iewo=0,.opendrn=0,.invout=0,.plevel=0,.pen=0,.pwoa=0,.funsel=1,.iodis=0}}}, /* UART0 TX */
    {VOR_PORTG, 1,en_iocfg_dir_dncare, {{.fltclk=0,.invinp=0,.iewo=0,.opendrn=0,.invout=0,.plevel=0,.pen=0,.pwoa=0,.funsel=1,.iodis=0}}}, /* UART0 RX */
    {VOR_PORTG, 2,en_iocfg_dir_output, {{.fltclk=0,.invinp=0,.iewo=0,.opendrn=0,.invout=0,.plevel=0,.pen=0,.pwoa=0,.funsel=1,.iodis=0}}}, /* out low */

    {VOR_PORTG, 5,en_iocfg_dir_output, {{.fltclk=0,.invinp=0,.iewo=0,.opendrn=0,.invout=0,.plevel=0,.pen=0,.pwoa=0,.funsel=0,.iodis=0}}}, /* LED DS2 */
    {VOR_PORTF,15,en_iocfg_dir_output, {{.fltclk=0,.invinp=0,.iewo=0,.opendrn=0,.invout=0,.plevel=0,.pen=0,.pwoa=0,.funsel=0,.iodis=0}}}, /* LED DS4 */
    {0} /* end of array - with optimizations end of array was not being properly detected*/
};


#ifdef DEBUG_UART

#if defined(DEBUG_UART_NUM) && DEBUG_UART_NUM == 0
    #define DEBUG_UART_BASE     VOR_UART0
#elif defined(DEBUG_UART_NUM) && DEBUG_UART_NUM == 1
    #define DEBUG_UART_BASE     VOR_UART1
#elif defined(DEBUG_UART_NUM) && DEBUG_UART_NUM == 2
    #define DEBUG_UART_BASE     VOR_UART2
#endif

#ifndef DEBUG_UART_BASE
    /* default to UART0 */
    #define DEBUG_UART_BASE     VOR_UART0
#endif

#ifndef DEBUG_UART_BAUD
    #define DEBUG_UART_BAUD     115200
#endif

#define UART_CLK (SystemCoreClock / 4)
#define UART2_CLK (SystemCoreClock / 2)

#define UART_CALC_CLOCKSCALE(_scc,_baud) ((_scc / (_baud * 16)) << \
                   UART_CLKSCALE_INT_Pos) |  \
                   (((((_scc % (_baud * 16)) * \
                   64 + (_baud * 8)) / \
                   (_baud * 16))) << \
                   UART_CLKSCALE_FRAC_Pos)


static void UartInit(VOR_UART_Type* uart, uint32_t baudrate)
{
    if (VOR_UART0 == uart) {
        VOR_SYSCONFIG->PERIPHERAL_CLK_ENABLE |= CLK_ENABLE_UART0;
        uart->CLKSCALE = UART_CALC_CLOCKSCALE(UART_CLK, baudrate);
    } else if (VOR_UART1 == uart) {
        VOR_SYSCONFIG->PERIPHERAL_CLK_ENABLE |= CLK_ENABLE_UART1;
        uart->CLKSCALE = UART_CALC_CLOCKSCALE(UART_CLK, baudrate);
    } else if (VOR_UART2 == uart) {
        VOR_SYSCONFIG->PERIPHERAL_CLK_ENABLE |= CLK_ENABLE_UART2;
        uart->CLKSCALE = UART_CALC_CLOCKSCALE(UART2_CLK, baudrate);
    } else {
        return;
    }

    /* Configure word size and RTS behavior. */
    uart->CTRL = (3 << UART_CTRL_WORDSIZE_Pos) | (UART_CTRL_DEFRTS_Msk);

    /* Enable CTS flow control IO, if needed */
#ifdef configUART_CTS_FLOW_CONTROL
    uart->CTRL |= UART_CTRL_AUTOCTS_Msk;
#endif

    /* Enable RTS flow control IO, if needed */
#ifdef configUART_RTS_FLOW_CONTROL
    uart->CTRL |= UART_CTRL_AUTORTS_Msk;
#endif

    /* Enable RX interrupts as soon as a character is received */
    uart->IRQ_ENB = UART_IRQ_ENB_IRQ_RX_Msk;
    uart->RXFIFOIRQTRG = 1;
    uart->TXFIFOIRQTRG = 8;

    if (VOR_UART0 == uart) {
        NVIC_SetPriority(UART0_RX_IRQn, 1);
        NVIC_EnableIRQ(UART0_RX_IRQn);
    } else if (VOR_UART1 == uart) {
        NVIC_SetPriority(UART1_RX_IRQn, 1);
        NVIC_EnableIRQ(UART1_RX_IRQn);
    } else {
        NVIC_SetPriority(UART2_RX_IRQn, 1);
        NVIC_EnableIRQ(UART2_RX_IRQn);
    }

    /* Enable UART */
    uart->ENABLE = (UART_ENABLE_RXENABLE_Msk |
                    UART_ENABLE_TXENABLE_Msk);

    /* send a break to let rx state machine reset */
    uart->TXBREAK = 32;
}

void uart_init(void)
{
    UartInit(DEBUG_UART_BASE, DEBUG_UART_BAUD);
}

void uart_write(const char* buf, unsigned int sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') { /* handle CRLF */
            while((DEBUG_UART_BASE->TXSTATUS & UART_TXSTATUS_WRRDY_Msk) == 0);
            DEBUG_UART_BASE->DATA = '\r';
        }
        while((DEBUG_UART_BASE->TXSTATUS & UART_TXSTATUS_WRRDY_Msk) == 0);
        DEBUG_UART_BASE->DATA = c;
    }
}

void uart_flush(void)
{
    /* wait for TX FIFO to be empty */
    while (DEBUG_UART_BASE->TXSTATUS & UART_TXSTATUS_WRBUSY_Msk);
}
#endif /* DEBUG_UART */


/* FRAM Driver */
/* Commands */
#define FRAM_WREN       0x06
#define FRAM_WRDI       0x04
#define FRAM_RDSR       0x05
#define FRAM_WRSR       0x01
#define FRAM_READ       0x03
#define FRAM_WRITE      0x02
#define FRAM_RDID       0x9F
#define FRAM_SLEEP      0xB9

#ifndef USE_HAL_SPI_FRAM
static hal_spi_handle_t spiHandle;

static void FRAM_WaitIdle(uint8_t spiBank)
{
    if (spiBank >= SPI_NUM_BANKS) {
        return;
    }

    /* Wait until TxBuf sends all */
    while (!(VOR_SPI->BANK[spiBank].STATUS & SPI_STATUS_TFE_Msk));
    /* Wait here until bytes are fully transmitted */
    while (VOR_SPI->BANK[spiBank].STATUS & SPI_STATUS_BUSY_Msk);
    /* Clear Tx & RX fifo */
    VOR_SPI->BANK[spiBank].FIFO_CLR =
        (SPI_FIFO_CLR_RXFIFO_Msk | SPI_FIFO_CLR_TXFIFO_Msk);
}

/* Init SPI FRAM access */
hal_status_t FRAM_Init(uint8_t spiBank, uint8_t csNum)
{
    hal_status_t status = hal_status_ok;
    uint8_t spiData[2];

    /* Initialize the SPI handle */
    memset(&spiHandle, 0, sizeof(spiHandle));
    spiHandle.locked = false;
    spiHandle.state = hal_spi_state_reset;
    spiHandle.spi = &VOR_SPI->BANK[spiBank];
    spiHandle.init.blockmode = true;
    spiHandle.init.bmstall = true;
    spiHandle.init.clkDiv = 2; /* 40MHz */
    spiHandle.init.loopback = false;
    spiHandle.init.mdlycap = false;
    spiHandle.init.mode = hal_spi_clkmode_0;
    spiHandle.init.ms = hal_spi_ms_master;
    spiHandle.init.chipSelect = csNum;
    spiHandle.init.wordLen = 8;

    status = HAL_Spi_Init(&spiHandle);
    if (status == hal_status_ok) {
        spiData[0] = FRAM_WREN; /* Set Write Enable Latch(WEL) bit  */
        status = HAL_Spi_Transmit(&spiHandle, spiData, 1, 0, true);
        HAL_Timer_DelayMs(1);
        status = HAL_Spi_Transmit(&spiHandle, spiData, 1, 0, true);
        spiData[0] = FRAM_WRSR;	/* Write single-byte Status Register message */
        spiData[1] = 0x00;	    /* Clear the BP1/BP0 protection */
        status = HAL_Spi_Transmit(&spiHandle, spiData, 2, 0, true);
        FRAM_WaitIdle(spiBank);
        spiHandle.state = hal_spi_state_ready;
    }
    wolfBoot_printf("FRAM_Init: status %d\n", status);
    return status;
}

hal_status_t FRAM_Write(uint8_t spiBank, uint32_t addr, uint8_t *buf,
    uint32_t len)
{
    hal_status_t status = hal_status_ok;
    uint8_t spiData[4];

#ifdef DEBUG_EXT_FLASH
    wolfBoot_printf("fram write: addr 0x%x, dst 0x%x, len %d\n",
        addr, buf, len);
#endif

    FRAM_WaitIdle(spiBank);

    spiData[0] = FRAM_WREN;
    status = HAL_Spi_Transmit(&spiHandle, spiData, 1, 0, true);
    spiData[0] = FRAM_WRITE;          /* Write command */
    spiData[1] = (uint8_t)((addr>>16) & 0xFF); /* Address high byte */
    spiData[2] = (uint8_t)((addr>>8) & 0xFF);  /* Address mid byte  */
    spiData[3] = (uint8_t)( addr & 0xFF);      /* Address low byte */
    status = HAL_Spi_Transmit(&spiHandle, spiData, 4, 0, false);
    return HAL_Spi_Transmit(&spiHandle, buf, len, 0, true);
}

hal_status_t FRAM_Read(uint8_t spiBank, uint32_t addr, uint8_t *buf,
    uint32_t len)
{
    uint8_t spiData[4];

#ifdef DEBUG_EXT_FLASH
    wolfBoot_printf("fram read: addr 0x%x, dst 0x%x, len %d\n",
        addr, buf, len);
#endif

    FRAM_WaitIdle(spiBank);

    spiData[0] = FRAM_READ;           /* Read command */
    spiData[1] = (uint8_t)((addr>>16) & 0xFF); /* Address high byte */
    spiData[2] = (uint8_t)((addr>>8) & 0xFF);  /* Address mid byte  */
    spiData[3] = (uint8_t)( addr & 0xFF);      /* Address low byte */
    return HAL_Spi_TransmitReceive(&spiHandle, spiData, buf, 4, 4, len, 0, true);
}
#endif

#ifndef FRAM_ERASE_VALUE
#define FRAM_ERASE_VALUE 0xFF
#endif
hal_status_t FRAM_Erase(uint8_t spiBank, uint32_t addr, uint32_t len)
{
    hal_status_t status;
    uint8_t data[32];

#ifdef DEBUG_EXT_FLASH
    wolfBoot_printf("fram erase: addr 0x%x, len %d\n", addr, len);
#endif

    /* Write 0xFF to the address and length */
    memset(data, FRAM_ERASE_VALUE, sizeof(data));

    while (len > 0) {
        int erase_len = (len > (int)sizeof(data)) ? (int)sizeof(data) : len;
        status = FRAM_Write(ROM_SPI_BANK, addr, data, erase_len);
        if (status != hal_status_ok) {
            return -(int)status; /* convert to negative error code */
        }
        addr += erase_len;
        len -= erase_len;
    }
    return 0;
}


void RAMFUNCTION hal_flash_unlock(void)
{

}

void RAMFUNCTION hal_flash_lock(void)
{

}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    /* not supported - no internal flash */
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    /* not supported - no internal flash */
    (void)address;
    (void)len;
    return 0;
}


#ifdef EXT_FLASH
void ext_flash_lock(void)
{
    /* Enable writes to code memory space */
    VOR_SYSCONFIG->ROM_PROT |= SYSCONFIG_ROM_PROT_WREN_Msk;
}

void ext_flash_unlock(void)
{
    /* Disable writes to code memory space */
    VOR_SYSCONFIG->ROM_PROT &= ~SYSCONFIG_ROM_PROT_WREN_Msk;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    hal_status_t status;
#ifdef DEBUG_EXT_FLASH
    wolfBoot_printf("ext write: addr 0x%x, dst 0x%x, len %d\n",
        address, data, len);
#endif
    status = FRAM_Write(ROM_SPI_BANK, address, (uint8_t*)data, len);
    if (status == hal_status_ok) {
        /* update the shadow IRAM */
        memcpy((void*)address, data, len);
    }
    else {
        return -(int)status; /* convert to negative error code */
    }
    return len;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    hal_status_t status;
#ifdef DEBUG_EXT_FLASH
    wolfBoot_printf("ext read: addr 0x%x, dst 0x%x, len %d\n",
        address, data, len);
#endif
    status = FRAM_Read(ROM_SPI_BANK, address, data, len);
    if (status == hal_status_ok) {
        /* update the shadow IRAM */
        memcpy((void*)address, data, len);
    }
    else {
        return -(int)status; /* convert to negative error code */
    }
    return len;
}

int ext_flash_erase(uintptr_t address, int len)
{
    hal_status_t status;
#ifdef DEBUG_EXT_FLASH
    wolfBoot_printf("ext erase: addr 0x%x, len %d\n", address, len);
#endif
    status = FRAM_Erase(ROM_SPI_BANK, address, len);
    if (status == hal_status_ok) {
        /* update the shadow IRAM */
        memset((void*)address, 0xFF, len);
    }
    else {
        return -(int)status; /* convert to negative error code */
    }
    return 0;
}

#ifdef TEST_EXT_FLASH

#ifndef TEST_EXT_ADDRESS
    /* Start Address for test 246KB */
    #define TEST_EXT_ADDRESS (246 * 1024)
#endif

static int test_ext_flash(void)
{
    int ret;
    uint32_t i;
    uint8_t pageData[WOLFBOOT_SECTOR_SIZE];
    uint32_t wait = 0;

#ifndef READONLY
    /* Erase sector */
    ret = ext_flash_erase(TEST_EXT_ADDRESS, sizeof(pageData));
    wolfBoot_printf("Sector Erase: Ret %d\n", ret);

    /* Write Page */
    for (i=0; i<sizeof(pageData); i++) {
        pageData[i] = (i & 0xff);
    }
    ret = ext_flash_write(TEST_EXT_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Page Write: Ret %d\n", ret);
#endif

    /* Read page */
    memset(pageData, 0, sizeof(pageData));
    ret = ext_flash_read(TEST_EXT_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Page Read: Ret %d\n", ret);

    wolfBoot_printf("Checking...\n");
    /* Check data */
    for (i=0; i<sizeof(pageData); i++) {
    #if defined(DEBUG_QSPI) && DEBUG_QSPI > 1
        wolfBoot_printf("check[%3d] %02x\n", i, pageData[i]);
    #endif
        if (pageData[i] != (i & 0xff)) {
            wolfBoot_printf("Check Data @ %d failed\n", i);
            return -1;
        }
    }

    wolfBoot_printf("Flash Test Passed\n");
    return ret;
}
#endif /* TEST_EXT_FLASH */
#endif /* EXT_FLASH */

#ifdef __WOLFBOOT /* build for wolfBoot only */
/* Configure Error Detection and Correction (EDAC) */
static void ConfigEdac(uint32_t ramScrub, uint32_t romScrub)
{
    VOR_SYSCONFIG->RAM0_SCRUB = ramScrub;
    VOR_SYSCONFIG->RAM1_SCRUB = ramScrub;
    VOR_SYSCONFIG->ROM_SCRUB = romScrub;

    IRQROUTER_ENABLE_CLOCK();
    NVIC_EnableIRQ(EDAC_MBE_IRQn);
    NVIC_SetPriority(EDAC_MBE_IRQn, 0);
    NVIC_EnableIRQ(EDAC_SBE_IRQn);
    NVIC_SetPriority(EDAC_SBE_IRQn, 0);

    VOR_SYSCONFIG->IRQ_ENB = 0x3f; /* enable all IRQ */
}
#endif /* __WOLFBOOT */

void hal_init(void)
{
    hal_status_t status;

    /* get clock settings and update SystemCoreClock */
    SystemCoreClockUpdate();


#ifdef __WOLFBOOT /* build for wolfBoot only */
    /* Configure PLL to set CPU clock to 100MHz - 40MHz crystal * 2.5 */
    status = HAL_Clkgen_PLL(CLK_CTRL0_XTAL_N_PLL2P5X);
    if (status != hal_status_ok) {
        /* continue anyways */
    }

    /* Disable Watchdog - should be already disabled out of reset */
    VOR_WATCH_DOG->WDOGLOCK    = 0x1ACCE551;
    VOR_WATCH_DOG->WDOGCONTROL = 0x0;
    NVIC_ClearPendingIRQ(WATCHDOG_IRQn);

    /* set FPU CP10 and CP11 Full Access */
    SCB->CPACR |= ((0x3 << 20)|(0x3 << 22));

    /* Init EDAC */
    ConfigEdac(WOLFBOOT_EDAC_RAM_SCRUB, WOLFBOOT_EDAC_ROM_SCRUB);
#endif /* __WOLFBOOT */

    /* Call SDK HAL initialization function */
    status = HAL_Init();
    if (status != hal_status_ok) {
        /* continue anyways */
    }

    /* Configure the pins */
    status = HAL_Iocfg_SetupPins(bootDefaultConfig);
    if (status != hal_status_ok) {
        /* continue anyways */
    }

#ifdef DEBUG_UART
    uart_init();
    #ifdef __WOLFBOOT
    uart_write("wolfBoot HAL Init\n", 18);
    #endif
#endif

    /* Init the FRAM SPI device */
    status = FRAM_Init(ROM_SPI_BANK, ROM_SPI_CSN);
    if (status != hal_status_ok) {
    #ifdef DEBUG
        wolfBoot_printf("FRAM_Init failed\n");
    #endif
        /* continue anyways */
    }

#ifdef TEST_EXT_FLASH
    test_ext_flash();
#endif

}

void hal_prepare_boot(void)
{
#ifdef DEBUG_UART
    uart_flush();
#endif

#ifdef WOLFBOOT_RESTORE_CLOCK
    /* Restore clock to heart-beat oscillator */
    (void)HAL_Clkgen_Init(CLK_CFG_HBO);
    SystemCoreClockUpdate();
#endif
}
