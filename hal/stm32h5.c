/* stm32h5.c
 *
 * Copyright (C) 2024 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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
#include <image.h>
#include <string.h>

#include "hal.h"
#include "hal/stm32h5.h"

static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    if ((reg & FLASH_ACR_LATENCY_MASK) < waitstates)
        do {
            FLASH_ACR =  (reg & ~(FLASH_ACR_LATENCY_MASK | (FLASH_ACR_WRHIGHFREQ_MASK << FLASH_ACR_WRHIGHFREQ_SHIFT))) |
                         waitstates | (0x02 << FLASH_ACR_WRHIGHFREQ_SHIFT) ;
        }
        while ((FLASH_ACR & FLASH_ACR_LATENCY_MASK) != waitstates);
}

void RAMFUNCTION hal_flash_wait_complete(uint8_t bank)
{
    while ((FLASH_SR & FLASH_SR_BSY) == FLASH_SR_BSY)
        ;
#if (TZ_SECURE())
    while ((FLASH_NS_SR & FLASH_SR_BSY) == FLASH_SR_BSY)
        ;
#endif

}

static void RAMFUNCTION hal_flash_wait_buffer_empty(uint8_t bank)
{
    while ((FLASH_SR & FLASH_SR_DBNE) == FLASH_SR_DBNE)
        ;
#if (TZ_SECURE())
    while ((FLASH_NS_SR & FLASH_SR_DBNE) == FLASH_SR_DBNE)
        ;
#endif

}

void RAMFUNCTION hal_flash_clear_errors(uint8_t bank)
{
    FLASH_CCR |= ( FLASH_CCR_CLR_WBNE | FLASH_CCR_CLR_DBNE | FLASH_CCR_CLR_INCE|
            FLASH_CCR_CLR_PGSE | FLASH_CCR_CLR_OPTE | FLASH_CCR_CLR_OPTWE |
            FLASH_CCR_CLR_WRPE | FLASH_CCR_CLR_EOP);

}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;
    uint32_t dword[2];
    volatile uint32_t *sr, *cr;

    cr = &FLASH_CR;
    sr = &FLASH_SR;

    hal_flash_clear_errors(0);
    src = (uint32_t *)data;
    dst = (uint32_t *)address;

#if (TZ_SECURE())
    if ( ((address < FLASH_BANK2_BASE) && (address >= WOLFBOOT_PARTITION_BOOT_ADDRESS)) ||
        (address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS))
        hal_tz_claim_nonsecure_area(address, len);
    /* Convert into secure address space */
    dst = (uint32_t *)((address & (~FLASHMEM_ADDRESS_SPACE)) | FLASH_SECURE_MMAP_BASE);
#endif

    while (i < len) {
        dword[0] = src[i >> 2];
        dword[1] = src[(i >> 2) + 1];
        *cr |= FLASH_CR_PG;
        dst[i >> 2] = dword[0];
        ISB();
        dst[(i >> 2) + 1] = dword[1];
        hal_flash_wait_complete(0);
        if ((*sr & FLASH_SR_EOP) != 0)
            *sr |= FLASH_SR_EOP;
        *cr &= ~FLASH_CR_PG;
        i+=8;
    }
#if (TZ_SECURE())
    hal_tz_release_nonsecure_area();
#endif
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    hal_flash_wait_complete(0);
    if ((FLASH_CR & FLASH_CR_LOCK) != 0) {
        FLASH_KEYR = FLASH_KEY1;
        DMB();
        FLASH_KEYR = FLASH_KEY2;
        DMB();
        while ((FLASH_CR & FLASH_CR_LOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
    hal_flash_wait_complete(0);
    if ((FLASH_CR & FLASH_CR_LOCK) == 0)
        FLASH_CR |= FLASH_CR_LOCK;
}

void RAMFUNCTION hal_flash_opt_unlock(void)
{
    hal_flash_wait_complete(0);
    if ((FLASH_OPTCR & FLASH_OPTCR_OPTLOCK) != 0) {
        FLASH_OPTKEYR = FLASH_OPTKEY1;
        DMB();
        FLASH_OPTKEYR = FLASH_OPTKEY2;
        DMB();
        while ((FLASH_CR & FLASH_CR_LOCK) != 0)
            ;
    }

}

void RAMFUNCTION hal_flash_opt_lock(void)
{
    FLASH_OPTCR |= FLASH_OPTCR_OPTSTRT;
    hal_flash_wait_complete(0);
    if ((FLASH_OPTCR & FLASH_OPTCR_OPTLOCK) == 0)
        FLASH_OPTCR |= FLASH_OPTCR_OPTLOCK;
}


int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end_address;
    uint32_t p;

    hal_flash_clear_errors(0);
    if (len == 0)
        return -1;

    if (address < 0x08000000)
        return -1;

    end_address = address + len - 1;
    for (p = address; p < end_address; p += FLASH_PAGE_SIZE) {
        uint32_t reg;
        uint32_t base;
        uint32_t bnksel = 0;
        base = FLASHMEM_ADDRESS_SPACE;
        reg = FLASH_CR & (~((FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) | FLASH_CR_BER));

        if(p >= (FLASH_BANK2_BASE) && (p <= (FLASH_TOP) ))
        {
#if TZ_SECURE()
            /* When in secure mode, skip erasing non-secure pages: will be erased upon claim */
            return 0;
#endif
            base = FLASH_BANK2_BASE;
            bnksel = 1;
        } else {
            FLASH_CR &= ~FLASH_CR_SER ;
            return 0; /* Address out of range */
        }
        reg |= ((((p - base)  >> 13) << FLASH_CR_PNB_SHIFT) | FLASH_CR_SER | (bnksel << 31));
        FLASH_CR = reg;
        DMB();
        FLASH_CR |= FLASH_CR_STRT;
        hal_flash_wait_complete(0);
    }
    /* If the erase operation is completed, disable the associated bits */
    FLASH_CR &= ~FLASH_CR_SER ;
    return 0;
}

static void clock_pll_off(void)
{
    uint32_t reg32;

    /* Select HSI as SYSCLK source. */
    RCC_CFGR1 &= ~(0x07 << RCC_CFGR1_SW_SHIFT);
    DMB();
    /* Turn off PLL1 */
    RCC_PLL1CFGR &= ~RCC_PLL1CFGR_PLL1PEN;
    DMB();
    RCC_CR &= ~RCC_CR_PLL1ON;
    DMB();

    /* Wait until PLL1 is disabled */
    while ((RCC_CR & RCC_CR_PLL1RDY) != 0)
        ;

}

/*This implementation will setup MSI 48 MHz as PLL Source Mux, PLLCLK as System Clock Source*/

static void clock_pll_on(void)
{
    uint32_t reg32;
    uint32_t plln, pllm, pllq, pllp, pllr, hpre, apb1pre, apb2pre, apb3pre, flash_waitstates;

    /* Select clock parameters (CPU Speed = 125 MHz) */
    pllm = 4;
    plln = 125; /* TODO: increase to 250 MHz */
    pllp = 2;
    pllq = 2;
    pllr = 2;
    flash_waitstates = 5;

    /* Disable PLL1 */
    RCC_CR &= ~RCC_CR_PLL1ON;

    /* Wait until PLL1 is disabled */
    while ((RCC_CR & RCC_CR_PLL1RDY) != 0)
        ;

    /* Set flash wait states */
    flash_set_waitstates(flash_waitstates);

    /* PLL Oscillator configuration */
    RCC_CR |= RCC_CR_HSEON | RCC_CR_HSEBYP | RCC_CR_HSEEXT;

    /* Wait until HSE is Ready */
    while ((RCC_CR & RCC_CR_HSERDY) == 0)
        ;

    /* Configure PLL1 div/mul factors */
    reg32 = RCC_PLL1CFGR;
    reg32 &= ~((0x3F << RCC_PLL1CFGR_PLL1M_SHIFT) | (0x03));
    reg32 |= (pllm << RCC_PLL1CFGR_PLL1M_SHIFT) | RCC_PLL1CFGR_PLL1SRC_HSE;
    RCC_PLL1CFGR = reg32;
    DMB();

    RCC_PLL1DIVR = ((plln - 1) << RCC_PLL1DIVR_DIVN_SHIFT) | ((pllp - 1) << RCC_PLL1DIVR_DIVP_SHIFT) |
        ((pllq - 1) << RCC_PLL1DIVR_DIVQ_SHIFT) | ((pllr - 1) << RCC_PLL1DIVR_DIVR_SHIFT);
    DMB();


    /* Disable Fractional PLL */
    RCC_PLL1CFGR &= ~RCC_PLL1CFGR_PLL1FRACEN;
    DMB();


    /* Configure Fractional PLL factor */
    RCC_PLL1FRACR = 0x00000000;
    DMB();

    /* Enable Fractional PLL */
    RCC_PLL1CFGR |= RCC_PLL1CFGR_PLL1FRACEN;
    DMB();

    /* Select PLL1 Input frequency range: VCI */
    RCC_PLL1CFGR |= RCC_PLL1CFGR_RGE_1_2 << RCC_PLL1CFGR_PLL1RGE_SHIFT;

    /* Select PLL1 Output frequency range: VCO = 0 */
    RCC_PLL1CFGR &= ~RCC_PLL1CFGR_PLL1VCOSEL;
    DMB();

    /* Enable PLL1 system clock out (DIV: P) */
    RCC_PLL1CFGR |= RCC_PLL1CFGR_PLL1PEN;

    /* Enable PLL1 */
    RCC_CR |= RCC_CR_PLL1ON;

    /* Set up APB3, 2, 1 and AHB prescalers */
    hpre = RCC_AHB_PRESCALER_DIV_NONE;
    apb1pre = RCC_APB_PRESCALER_DIV_NONE;
    apb2pre = RCC_APB_PRESCALER_DIV_NONE;
    apb3pre = RCC_APB_PRESCALER_DIV_NONE;
    reg32 = RCC_CFGR2;
    reg32 &= ~( (0x0F << RCC_CFGR2_HPRE_SHIFT) |
            (0x07 << RCC_CFGR2_PPRE1_SHIFT) |
            (0x07 << RCC_CFGR2_PPRE2_SHIFT) |
            (0x07 << RCC_CFGR2_PPRE3_SHIFT));
    reg32 |= ((hpre) << RCC_CFGR2_HPRE_SHIFT) | ((apb1pre) << RCC_CFGR2_PPRE1_SHIFT) |
        ((apb2pre) << RCC_CFGR2_PPRE2_SHIFT) | ((apb3pre) << RCC_CFGR2_PPRE3_SHIFT);
    RCC_CFGR2 = reg32;
    DMB();

    /* Wait until PLL1 is Ready */
    while ((RCC_CR & RCC_CR_PLL1RDY) == 0)
        ;

    /* Set PLL as clock source */
    reg32 = RCC_CFGR1 & (~RCC_CFGR1_SW_MASK);
    RCC_CFGR1 = reg32 | RCC_CFGR1_SW_PLL1;
    DMB();

    /* Wait until selection of PLL as source is complete */
    while ((RCC_CFGR1 & (RCC_CFGR1_SW_PLL1 << RCC_CFGR1_SWS_SHIFT)) == 0)
        ;

}

#if (TZ_SECURE())
static void periph_unsecure(void)
{
    uint32_t pin;

    /*Enable clock for User LED GPIOs */
    RCC_AHB2_CLOCK_ER|= LED_AHB2_ENABLE;

    /* Enable clock for LPUART1 */
    RCC_APB2_CLOCK_ER |= UART1_APB2_CLOCK_ER_VAL;


    PWR_CR2 |= PWR_CR2_IOSV;
    /*Un-secure User LED GPIO pins */
    GPIO_SECCFGR(GPIOG_BASE) &= ~(1 << 4);
    GPIO_SECCFGR(GPIOB_BASE) &= ~(1 << 0);
    GPIO_SECCFGR(GPIOF_BASE) &= ~(1 << 4);

#if 0
    /* Unsecure LPUART1 */
    TZSC_PRIVCFGR2 &= ~(TZSC_PRIVCFG2_LPUARTPRIV);
    GPIO_SECCFGR(GPIOG_BASE) &= ~(1<<UART1_TX_PIN);
    GPIO_SECCFGR(GPIOG_BASE) &= ~(1<<UART1_RX_PIN);
#endif

}
#endif


#define AIRCR *(volatile uint32_t *)(0xE000ED0C)
#define AIRCR_VKEY (0x05FA << 16)
#define AIRCR_SYSRESETREQ (1 << 2)

static void RAMFUNCTION stm32h5_reboot(void)
{
    AIRCR = AIRCR_SYSRESETREQ | AIRCR_VKEY;
    while(1)
        ;

}



#if defined(DUALBANK_SWAP) && defined(__WOLFBOOT)
void RAMFUNCTION hal_flash_dualbank_swap(void)
{
    uint32_t cur_opts;
    cur_opts = (FLASH_OPTSR_CUR & FLASH_OPTSR_SWAP_BANK) >> 31;
    hal_flash_clear_errors(0);
    hal_flash_unlock();
    hal_flash_opt_unlock();
    if (cur_opts)
        FLASH_OPTSR_PRG &= ~(FLASH_OPTSR_SWAP_BANK);
    else
        FLASH_OPTSR_PRG |= FLASH_OPTSR_SWAP_BANK;

    FLASH_OPTCR |= FLASH_OPTCR_OPTSTRT;
    DMB();
    hal_flash_opt_lock();
    hal_flash_lock();
    stm32h5_reboot();
}

static uint8_t bootloader_copy_mem[BOOTLOADER_SIZE];

static void fork_bootloader(void)
{
    uint32_t data = (uint32_t) FLASHMEM_ADDRESS_SPACE;
    uint32_t dst  = FLASH_BANK2_BASE;
    uint32_t r = 0, w = 0;
    int i;


#if TZ_SECURE()
    data = (uint32_t)((data & (~FLASHMEM_ADDRESS_SPACE)) | FLASH_SECURE_MMAP_BASE);
    dst = (uint32_t)((dst & (~FLASHMEM_ADDRESS_SPACE)) | FLASH_SECURE_MMAP_BASE);
#endif

    /* Return if content already matches */
    if (memcmp((void *)data, (const char*)dst, BOOTLOADER_SIZE) == 0)
        return;

    /* Read the wolfBoot image in RAM */
    memcpy(bootloader_copy_mem, (void*)data, BOOTLOADER_SIZE);

    /* Mass-erase */
    hal_flash_unlock();
    hal_flash_erase(dst, BOOTLOADER_SIZE);
    hal_flash_write(dst, bootloader_copy_mem, BOOTLOADER_SIZE);
    hal_flash_lock();
}
#endif

void hal_init(void)
{
#if TZ_SECURE()
    hal_tz_sau_init();
    hal_gtzc_init();
#endif
    clock_pll_on();



#if defined(DUALBANK_SWAP) && defined(__WOLFBOOT)
    if ((FLASH_OPTSR_CUR & (FLASH_OPTSR_SWAP_BANK)) == 0)
        fork_bootloader();
#endif


}


void hal_prepare_boot(void)
{
    clock_pll_off();
#if (TZ_SECURE())
    periph_unsecure();
#endif
}

#ifdef FLASH_OTP_KEYSTORE

/* Public API */

int hal_flash_otp_write(uint32_t flashAddress, const void* data, uint16_t length)
{
    volatile uint16_t tmp;
    uint16_t *pdata = (uint16_t *)data;
    uint16_t idx = 0, len_align;
    uint16_t last_word;
    if (!(flashAddress >= FLASH_OTP_BASE && flashAddress <= FLASH_OTP_END)) {
        return -1;
    }

    hal_flash_wait_complete(0);
    hal_flash_wait_buffer_empty(0);
    hal_flash_unlock();
    hal_flash_clear_errors(0);


    /* Truncate to 2B alignment */
    length = (length / 2 * 2);

    while (idx < length && flashAddress <= FLASH_OTP_END-1) {
        hal_flash_wait_complete(0);
        /* Set PG bit */
        FLASH_CR |= FLASH_CR_PG;
        /* Program an OTP word (32 bits) */
        *(volatile uint16_t*)flashAddress = *pdata;
        ISB();
        DSB();
        /* Read it back */
        tmp = *(volatile uint16_t*)flashAddress;
        if (tmp != *pdata) {
            /* Provisioning failed. OTP already programmed? */
            while(1)
                ;
        }

        /* Clear PG bit */
        FLASH_CR &= ~FLASH_CR_PG;
        flashAddress += sizeof(uint16_t);
        pdata++;
        idx += sizeof(uint16_t);
    }

    hal_flash_lock();
    return 0;
}

int hal_flash_otp_read(uint32_t flashAddress, void* data, uint32_t length)
{
    uint16_t i;
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
