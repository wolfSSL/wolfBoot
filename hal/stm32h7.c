/* stm32h7.c
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

#include "hal/stm32h7.h"

static uint32_t stm32h7_cache[STM32H7_WORD_SIZE / sizeof(uint32_t)];

static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    if ((reg & FLASH_ACR_LATENCY_MASK) != waitstates)
        FLASH_ACR = (reg & ~FLASH_ACR_LATENCY_MASK) | waitstates;
}

static RAMFUNCTION void flash_wait_last(void)
{
    while ((FLASH_OPTSR_CUR & FLASH_OPTSR_CUR_BSY))
        ;
}

static RAMFUNCTION void flash_wait_complete(uint8_t bank)
{
    if (bank == 0) {
        while ((FLASH_SR1 & FLASH_SR_QW) == FLASH_SR_QW);
    }
    else {
        while ((FLASH_SR2 & FLASH_SR_QW) == FLASH_SR_QW);
    }
}

static void RAMFUNCTION flash_clear_errors(uint8_t bank)
{
    if (bank == 0) {
        FLASH_SR1 |= (FLASH_SR_WRPERR | FLASH_SR_PGSERR | FLASH_SR_STRBERR |
                      FLASH_SR_INCERR | FLASH_SR_OPERR | FLASH_SR_RDPERR |
                      FLASH_SR_RDSERR | FLASH_SR_SNECCERR | FLASH_SR_DBECCERR);
    }
    else {
        FLASH_SR2 |= (FLASH_SR_WRPERR | FLASH_SR_PGSERR | FLASH_SR_STRBERR |
                      FLASH_SR_INCERR | FLASH_SR_OPERR | FLASH_SR_RDPERR |
                      FLASH_SR_RDSERR | FLASH_SR_SNECCERR | FLASH_SR_DBECCERR);
    }
}

static void RAMFUNCTION flash_program_on(uint8_t bank)
{
    if (bank == 0) {
        FLASH_CR1 |= FLASH_CR_PG;
        while ((FLASH_CR1 & FLASH_CR_PG) == 0)
            ;
    }
    else {
        FLASH_CR2 |= FLASH_CR_PG;
        while ((FLASH_CR2 & FLASH_CR_PG) == 0)
            ;
    }
}

static void RAMFUNCTION flash_program_off(uint8_t bank)
{
    if (bank == 0) {
        FLASH_CR1 &= ~FLASH_CR_PG;
    }
    else {
        FLASH_CR2 &= ~FLASH_CR_PG;
    }
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0, ii =0;
    uint32_t *src, *dst;
    uint8_t bank=0;
    uint8_t *vbytes = (uint8_t *)(stm32h7_cache);
    int off;
    uint32_t base_addr;

    if ((address & FLASH_BANK2_BASE_REL) != 0) {
        bank = 1;
    }

    while (i < len) {
        if ((len - i > 32) && ((((address + i) & 0x1F) == 0) &&
            ((((uint32_t)data) + i) & 0x1F) == 0))
        {
            flash_wait_last();
            flash_clear_errors(0);
            flash_clear_errors(1);
            flash_program_on(bank);
            flash_wait_complete(bank);
            src = (uint32_t *)(data + i);
            dst = (uint32_t *)(address + i);
            for (ii = 0; ii < 8; ii++) {
                dst[ii] = src[ii];
            }
            i+=32;
        }
        else {
            off = (address + i) - (((address + i) >> 5) << 5);
            base_addr = (address + i) & (~0x1F); /* aligned to 256 bit */
            dst = (uint32_t *)(base_addr);
            for (ii = 0; ii < 8; ii++) {
                stm32h7_cache[ii] = dst[ii];
            }
            /* Check if flags page */
            if (STM32H7_BOOT_FLAGS_PAGE(address)) {
                if (base_addr != STM32H7_PART_BOOT_END - STM32H7_WORD_SIZE)
                    return -1;
                hal_flash_erase(STM32H7_PART_BOOT_FLAGS_PAGE_ADDRESS,
                    STM32H7_SECTOR_SIZE);
            }
            else if (STM32H7_UPDATE_FLAGS_PAGE(address)) {
                if (base_addr != STM32H7_PART_UPDATE_END - STM32H7_WORD_SIZE)
                    return -1;
                hal_flash_erase(STM32H7_PART_UPDATE_FLAGS_PAGE_ADDRESS,
                    STM32H7_SECTOR_SIZE);
            }
            /* Replace bytes in cache */
            while ((off < STM32H7_WORD_SIZE) && (i < len)) {
                vbytes[off++] = data[i++];
            }

            /* Actual write from cache to FLASH */
            flash_wait_last();
            flash_clear_errors(0);
            flash_clear_errors(1);
            flash_program_on(bank);
            flash_wait_complete(bank);
            ISB();
            DSB();
            for (ii = 0; ii < 8; ii++) {
                dst[ii] = stm32h7_cache[ii];
            }
            ISB();
            DSB();
        }
        flash_wait_complete(bank);
        flash_program_off(bank);
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flash_wait_complete(1);
    if ((FLASH_CR1 & FLASH_CR_LOCK) != 0) {
        FLASH_KEYR1 = FLASH_KEY1;
        DMB();
        FLASH_KEYR1 = FLASH_KEY2;
        DMB();
        while ((FLASH_CR1 & FLASH_CR_LOCK) != 0)
            ;
    }

    flash_wait_complete(2);
    if ((FLASH_CR2 & FLASH_CR_LOCK) != 0) {
        FLASH_KEYR2 = FLASH_KEY1;
        DMB();
        FLASH_KEYR2 = FLASH_KEY2;
        DMB();
        while ((FLASH_CR2 & FLASH_CR_LOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
    flash_wait_complete(1);
    if ((FLASH_CR1 & FLASH_CR_LOCK) == 0)
        FLASH_CR1 |= FLASH_CR_LOCK;

    flash_wait_complete(2);
    if ((FLASH_CR2 & FLASH_CR_LOCK) == 0)
        FLASH_CR2 |= FLASH_CR_LOCK;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end_address;
    uint32_t p;

    if (len == 0)
        return -1;
    end_address = (address - FLASHMEM_ADDRESS_SPACE) + len - 1;
    for (p = (address - FLASHMEM_ADDRESS_SPACE);
         p < end_address;
         p += FLASH_PAGE_SIZE)
    {
        if (p < FLASH_BANK2_BASE_REL) {
            uint32_t reg = FLASH_CR1 &
                (~((FLASH_CR_SNB_MASK << FLASH_CR_SNB_SHIFT) | FLASH_CR_PSIZE));
            FLASH_CR1 = reg |
                (((p >> 17) << FLASH_CR_SNB_SHIFT) | FLASH_CR_SER | 0x00);
            DMB();
            FLASH_CR1 |= FLASH_CR_STRT;
            flash_wait_complete(1);
        }
        if ((p>= FLASH_BANK2_BASE_REL) &&
            (p <= (FLASH_TOP - FLASHMEM_ADDRESS_SPACE))) {
            uint32_t reg = FLASH_CR2 &
                (~((FLASH_CR_SNB_MASK << FLASH_CR_SNB_SHIFT) | FLASH_CR_PSIZE));
            p-= (FLASH_BANK2_BASE);
            FLASH_CR2 = reg |
                (((p >> 17) << FLASH_CR_SNB_SHIFT) | FLASH_CR_SER | 0x00);
            DMB();
            FLASH_CR2 |= FLASH_CR_STRT;
            flash_wait_complete(2);
        }
    }
    return 0;
}

#ifdef DEBUG_UART
static int uart_init(void)
{
    uint32_t reg;

    /* Set general UART clock source (all uarts but nr 1 and 6) */
    /* USART234578SEL bits 2:0 */
    RCC_D2CCIP2R &= ~(0x7 << 0);
    RCC_D2CCIP2R |=  (0x3 << 0); /* 000 = pclk1 (120MHz), 011 = hsi (64MHz) */

#if UART_PORT == 3
    /* Enable clock for USART_3 and reset */
    APB1_CLOCK_LER |= RCC_APB1_USART3_EN;

    APB1_CLOCK_LRST |= RCC_APB1_USART3_EN;
    APB1_CLOCK_LRST &= ~RCC_APB1_USART3_EN;
#elif UART_PORT == 5
    /* Enable clock for USART_5 and reset */
    APB1_CLOCK_LER |= RCC_APB1_UART5_EN;

    APB1_CLOCK_LRST |= RCC_APB1_UART5_EN;
    APB1_CLOCK_LRST &= ~RCC_APB1_UART5_EN;
#else
    /* Enable clock for USART_2 and reset */
    APB1_CLOCK_LER |= RCC_APB1_USART2_EN;

    APB1_CLOCK_LRST |= RCC_APB1_USART2_EN;
    APB1_CLOCK_LRST &= ~RCC_APB1_USART2_EN;
#endif

    /* Enable UART pins */
#if UART_PORT == 5
    AHB4_CLOCK_ENR |= RCC_AHB4_GPIOB_EN;
#else
    AHB4_CLOCK_ENR |= RCC_AHB4_GPIOD_EN;
#endif

    /* Set mode = AF. The PORT D I/O pin is first reset and then set to AF
     * (bit config 10:Alternate function mode) */
    reg = GPIO_MODE(UART_GPIO_BASE) & ~(0x03 << (UART_TX_PIN * 2));
    GPIO_MODE(UART_GPIO_BASE) = reg | (2 << (UART_TX_PIN * 2));
    reg = GPIO_MODE(UART_GPIO_BASE) & ~(0x03 << (UART_RX_PIN * 2));
    GPIO_MODE(UART_GPIO_BASE) = reg | (2 << (UART_RX_PIN * 2));

    /* Alternate function. Use AFLR for pins 0-7 and AFHR for pins 8-15 */
#if UART_TX_PIN < 8
    reg = GPIO_AFRL(UART_GPIO_BASE) & ~(0xf << ((UART_TX_PIN) * 4));
    GPIO_AFRL(UART_GPIO_BASE) = reg | (UART_PIN_AF << ((UART_TX_PIN) * 4));
#else
    reg = GPIO_AFRH(UART_GPIO_BASE) & ~(0xf << ((UART_TX_PIN - 8) * 4));
    GPIO_AFRH(UART_GPIO_BASE) = reg | (UART_PIN_AF << ((UART_TX_PIN - 8) * 4));
#endif
#if UART_RX_PIN < 8
    reg = GPIO_AFRL(UART_GPIO_BASE) & ~(0xf << ((UART_RX_PIN)*4));
    GPIO_AFRL(UART_GPIO_BASE) = reg | (UART_PIN_AF << ((UART_RX_PIN)*4));
#else
    reg = GPIO_AFRH(UART_GPIO_BASE) & ~(0xf << ((UART_RX_PIN - 8) * 4));
    GPIO_AFRH(UART_GPIO_BASE) = reg | (UART_PIN_AF << ((UART_RX_PIN - 8) * 4));
#endif

    /* Disable UART to enable settings to be written into the registers. */
    if (UART_CR1(UART_BASE) & UART_CR1_UART_ENABLE) {
        UART_CR1(UART_BASE) &= ~UART_CR1_UART_ENABLE;
    }

    /* Clock Prescaler */
    UART_PRESC(UART_BASE) = 0; /* no div (div=1) */

    /* Configure clock (speed/bitrate). Requires UE = 0. */
    UART_BRR(UART_BASE) = (uint16_t)(CLOCK_SPEED / BAUD_RATE);

    /* Enable FIFO mode */
    UART_CR1(UART_BASE) |= UART_CR1_FIFOEN;

    /* Enable 16-bit oversampling */
    UART_CR1(UART_BASE) &= ~UART_CR1_OVER8;

    /* Configure the M bits (word length) */
    /* Word length is 8 bits by default (0=1 start, 8 data, 0 stop) */
    UART_CR1(UART_BASE) &= ~(UART_CR1_M0 | UART_CR1_M1);

    /* Configure stop bits (00: 1 stop bit / 10: 2 stop bits.) */
    UART_CR2(UART_BASE) &= ~UART_CR2_STOP_MASK;
    UART_CR2(UART_BASE) |= UART_CR2_STOP(0);

    /* Configure parity bits, disabled */
    UART_CR1(UART_BASE) &= ~(UART_CR1_PARITY_ENABLED | UART_CR1_PARITY_ODD);

    /* In asynchronous mode, the following bits must be kept cleared:
     * - LINEN and CLKEN bits in the UART_CR2 register,
     * - SCEN, HDSEL and IREN  bits in the UART_CR3 register.*/
    UART_CR2(UART_BASE) &= ~(UART_CR2_LINEN | UART_CR2_CLKEN);
    UART_CR3(UART_BASE) &= ~(UART_CR3_SCEN | UART_CR3_HDSEL | UART_CR3_IREN);

    /* Turn on UART */
    UART_CR1(UART_BASE) |= (UART_CR1_TX_ENABLE | UART_CR1_RX_ENABLE |
        UART_CR1_UART_ENABLE);

    return 0;
}

void uart_write(const char* buf, unsigned int sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        while ((UART_ISR(UART_BASE) & UART_ISR_TX_FIFO_NOT_FULL) == 0);

        UART_TDR(UART_BASE) = buf[pos++];
    }
}
#endif /* DEBUG_UART */

static void clock_pll_off(void)
{
    uint32_t reg32;

    /* Select HSI as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 2) |(1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSISYS);
    DMB();
    /* Turn off PLL */
    RCC_CR &= ~RCC_CR_PLL1ON;
    DMB();
}

/* This implementation will setup HSI RC 16 MHz as PLL Source Mux, PLLCLK
 * as System Clock Source */
static void clock_pll_on(int powersave)
{
    uint32_t reg32;
    uint32_t plln, pllm, pllq, pllp, pllr, hpre, d1cpre, d1ppre;
    uint32_t d2ppre1, d2ppre2, d3ppre, flash_waitstates;

    PWR_CR3 |= PWR_CR3_LDOEN;
    while ((PWR_CSR1 & PWR_CSR1_ACTVOSRDY) == 0) {};

    PWR_D3CR |= (PWR_D3CR_VOS_SCALE_1 << PWR_D3CR_VOS_SHIFT);
    /* Delay after setting the voltage scaling */
    reg32 = PWR_D3CR;
    SYSCFG_PWRCR |= SYSCFG_PWRCR_ODEN;
    /* Delay after setting the voltage scaling */
    reg32 = PWR_D3CR;
    while ((PWR_D3CR & PWR_D3CR_VOSRDY) == 0) {};

    /* Select clock parameters (CPU Speed = 480MHz) */
    pllm = 1;
    plln = 120;
    pllp = 2;
    pllq = 20;
    pllr = 2;
    d1cpre =   RCC_PRESCALER_DIV_NONE;
    hpre  =    RCC_PRESCALER_DIV_2;
    d1ppre =  (RCC_PRESCALER_DIV_2 >> 1);
    d2ppre1 = (RCC_PRESCALER_DIV_2 >> 1);
    d2ppre2 = (RCC_PRESCALER_DIV_2 >> 1);
    d3ppre =  (RCC_PRESCALER_DIV_2 >> 1);
    flash_waitstates = 4;

    flash_set_waitstates(flash_waitstates);

    /* Enable internal high-speed oscillator. */
    RCC_CR |= RCC_CR_HSION;
    DMB();
    while ((RCC_CR & RCC_CR_HSIRDY) == 0) {};

    /* Select HSI as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 2) |(1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSISYS);
    DMB();

    /* Enable external high-speed oscillator. */
    reg32 = RCC_CR;
    reg32 |= RCC_CR_HSEBYP;
    RCC_CR = (reg32 | RCC_CR_HSEON);
    DMB();
    while ((RCC_CR & RCC_CR_HSERDY) == 0) {};

    /*
     * Set prescalers for D1: D1CPRE, D1PPRE, HPRE
     */
    RCC_D1CFGR |= (hpre << 0); /* RM0433 - 7.7.8- RCC_CFGR */
    DMB();

    reg32 = RCC_D1CFGR;
    reg32 &= ~(0xF0); /* don't change bits [0-3] that were previously set */
    RCC_D1CFGR = (reg32 | (d1ppre << 4));  /* RM0433 - 7.7.8- RCC_CFGR */
    DMB();

    reg32 = RCC_D1CFGR;
    reg32 &= ~(0x100); /* don't change bits [0-7] */
    RCC_D1CFGR = (reg32 | (d1cpre << 8));  /* RM0433 - 7.7.8- RCC_CFGR */
    DMB();

    /*
     * Set prescalers for D2: D2PPRE1, D2PPRE2
     */
    reg32 = RCC_D2CFGR;
    reg32 &= ~(0xF0); /* don't change bits [0-3] */
    RCC_D2CFGR = (reg32 | (d2ppre1 << 4));  /* RM0433 - 7.7.8- RCC_CFGR */
    DMB();

    reg32 = RCC_D2CFGR;
    reg32 &= ~(0x100); /* don't change bits [0-7] */
    RCC_D2CFGR = (reg32 | (d2ppre2 << 8));  /* RM0433 - 7.7.8- RCC_CFGR */
    DMB();

    /*
     * Set prescalers for D3: D3PPRE
     */
    reg32 = RCC_D3CFGR;
    RCC_D3CFGR = (reg32 | (d3ppre << 4));  /* RM0433 - 7.7.8- RCC_CFGR */
    DMB();


    /*
     * Set PLL config
     */

    /* PLL Clock source selection + DIVM1 */
    reg32 = RCC_PLLCKSELR;
    reg32 |= RCC_PLLCKSELR_PLLSRC_HSE;
    reg32 |= ((pllm) << 4);
    RCC_PLLCKSELR = reg32;
    DMB();

    reg32 = RCC_PLL1DIVR;
    reg32 |= (plln -1);
    reg32 |= ((pllp - 1) << 9);
    reg32 |= ((pllq - 1) << 16);
    reg32 |= ((pllr - 1) << 24);
    RCC_PLL1DIVR = reg32;
    DMB();

    RCC_PLLCFGR |= (RCC_PLLCFGR_PLL1RGE_2_4 << RCC_PLLCFGR_PLL1RGE_SHIFT);
    RCC_PLLCFGR |= RCC_PLLCFGR_DIVP1EN;
    RCC_PLLCFGR |= RCC_PLLCFGR_DIVQ1EN;
    RCC_PLLCFGR |= RCC_PLLCFGR_DIVR1EN;

    RCC_CR |= RCC_CR_PLL1ON;
    DMB();
    while ((RCC_CR & RCC_CR_PLL1RDY) == 0) {};

    /* Select PLL as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 2) |(1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_PLL);
    DMB();

    /* Wait for PLL clock to be selected. */
    while ((RCC_CFGR & ((1 << 2) | (1 << 1) | (1 << 0))) != RCC_CFGR_SW_PLL) {};
}

void RAMFUNCTION hal_flash_dualbank_swap(void)
{
    hal_flash_unlock();
    DMB();
    ISB();
    if (SYSCFG_UR0 & SYSCFG_UR0_BKS)
        SYSCFG_UR0 &= ~SYSCFG_UR0_BKS;
    else
        SYSCFG_UR0 |= SYSCFG_UR0_BKS;
    DMB();
    hal_flash_lock();
}

void hal_init(void)
{
    clock_pll_on(0);

#ifdef DEBUG_UART
    uart_init();
    uart_write("wolfBoot Init\n", 14);
#endif
}

void hal_prepare_boot(void)
{
#ifdef SPI_FLASH
    spi_flash_release();
#endif
#ifdef WOLFBOOT_RESTORE_CLOCK
    clock_pll_off();
#endif
}

#ifdef FLASH_OTP_KEYSTORE
static void flash_otp_wait(void)
{
    /* Wait for the FLASH operation to complete by polling on QW flag to be reset. */
    while ( (FLASH_SR1 & FLASH_SR_QW) == FLASH_SR_QW ) {
        /* TODO: check timeout */
    }

    /* Check FLASH End of Operation flag */
    if ( (FLASH_SR1 & FLASH_SR_EOP) == FLASH_SR_EOP ) {
        FLASH_SR1 &= FLASH_SR_EOP; /* Clear FLASH End of Operation pending bit */
    }
}

static void hal_flash_otp_unlock(void)
{
    if ((FLASH_OPTCR & FLASH_OPTCR_OPTLOCK) != 0U) {
        FLASH_OPTKEYR = FLASH_OPT_KEY1;
        FLASH_OPTKEYR = FLASH_OPT_KEY2;
    }
}

static void hal_flash_otp_lock(void)
{
    /* Set the OPTLOCK Bit to lock the FLASH Option Byte Registers access */
    FLASH_OPTCR |= FLASH_OPTCR_OPTLOCK;
}

/* Public API */

int hal_flash_otp_set_readonly(uint32_t flashAddress, uint16_t length)
{
    /* TODO: set WP on OTP if needed */
    return 0;
}

int hal_flash_otp_write(uint32_t flashAddress, const void* data, uint16_t length)
{
    volatile uint16_t tmp;
    uint16_t idx = 0;
    const uint16_t *pdata = (const uint16_t *)data;
    if (!(flashAddress >= FLASH_OTP_BASE && flashAddress <= FLASH_OTP_END)) {
        return -1;
    }

    hal_flash_unlock();
    hal_flash_otp_unlock();

    while (idx < length && flashAddress <= FLASH_OTP_END-1) {
        /* Clear errors */
        flash_clear_errors(0); /* bank 1 */
        /* Wait for last operation to be completed */
        flash_otp_wait();

        FLASH_OPTCR &= ~(FLASH_OPTCR_OPTLOCK); /* unlock FLASH_OPTCR register */

        /* Set OTP_PG bit */
        FLASH_OPTCR |= FLASH_OPTCR_PG_OTP;

        ISB();
        DSB();

        /* Program an OTP word (16 bits) */
        *(volatile uint16_t*)flashAddress = *pdata;

        /* Read it back */
        tmp = *(volatile uint16_t*)flashAddress;
        (void)tmp; /* avoid unused warnings */

        /* Wait for last operation to be completed */
        flash_otp_wait();

        /* clear OTP_PG bit */
        FLASH_OPTCR &= ~FLASH_OPTCR_PG_OTP;

        flashAddress += sizeof(uint16_t);
        pdata++;
        idx += sizeof(uint16_t);
    }

    hal_flash_otp_lock();
    hal_flash_lock();
    return 0;
}

int hal_flash_otp_read(uint32_t flashAddress, void* data, uint32_t length)
{
    uint32_t i;
    uint16_t *pdata = (uint16_t *)data;
    if (!(flashAddress >= FLASH_OTP_BASE && flashAddress <= FLASH_OTP_END)) {
        return -1;
    }
    for (i = 0;
        (i < length) && (flashAddress <= (FLASH_OTP_END-1));
        i += sizeof(uint16_t))
    {
        *pdata = *(volatile uint16_t*)flashAddress;
        flashAddress += sizeof(uint16_t);
        pdata++;
    }
    return 0;
}

#endif /* FLASH_OTP_KEYSTORE */

