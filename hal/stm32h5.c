/* stm32h5.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
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
#include "hal/armv8m_tz.h"

#include "uart_drv.h"

#define PLL_SRC_HSE 1

#if TZ_SECURE()
static int is_flash_nonsecure(uint32_t address)
{
    if (address >= WOLFBOOT_PARTITION_BOOT_ADDRESS &&
            address < WOLFBOOT_PARTITION_BOOT_ADDRESS +
            WOLFBOOT_PARTITION_SIZE) {
        return 1;
    }
    return 0;
}
#endif


static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    uint32_t wrhighfreq = 1; /* default flash signal delay */

    if ((reg & FLASH_ACR_LATENCY_MASK) < waitstates) {
        /* clear wrhighfreq and latency */
        reg &= ~(FLASH_ACR_LATENCY_MASK |
                (FLASH_ACR_WRHIGHFREQ_MASK << FLASH_ACR_WRHIGHFREQ_SHIFT));
        if (waitstates > 3) { /* wait states 4 and 5 require = 2 */
            wrhighfreq = 2;
        }
        reg |= (waitstates | (wrhighfreq << FLASH_ACR_WRHIGHFREQ_SHIFT));
        FLASH_ACR = reg;
        ISB();
        DMB();
        /* wait for the register to be updated */
        while (FLASH_ACR != reg);
    }
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

    hal_flash_clear_errors(0);
    src = (uint32_t *)data;
    dst = (uint32_t *)address;

#if (TZ_SECURE())
    dst = (uint32_t *)(address | FLASH_SECURE_MMAP_BASE);
    if (is_flash_nonsecure(address)) {
        hal_tz_claim_nonsecure_area(address, len);
    }
#endif
    while (i < len) {
        dword[0] = src[i >> 2];
        if (len > i + 1)
            dword[1] = src[(i >> 2) + 1];
        else
            dword[1] = 0xFFFFFFFF;
        FLASH_CR |= FLASH_CR_PG;
        dst[i >> 2] = dword[0];
        ISB();
        dst[(i >> 2) + 1] = dword[1];
        ISB();
        hal_flash_wait_complete(0);
        if ((FLASH_SR & FLASH_SR_EOP) != 0)
            FLASH_SR |= FLASH_SR_EOP;
        FLASH_CR &= ~FLASH_CR_PG;
        i+=8;
        DSB();
    }
#if (TZ_SECURE())
    if (is_flash_nonsecure(address)) {
        hal_tz_release_nonsecure_area();
    }
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
    uint32_t start_address;
    uint32_t end_address;
    uint32_t p;

    hal_flash_clear_errors(0);
    if (len == 0)
        return -1;

    if (address < 0x08000000)
        return -1;

#if TZ_SECURE()
    if (address & FLASH_SECURE_MMAP_BIT) {
        /* Get address in non-secure address space */
        start_address = address & ~FLASH_SECURE_MMAP_BIT;
    }
    else {
        if (is_flash_nonsecure(address)) {
            hal_tz_claim_nonsecure_area(address, len);
        }
        start_address = address;
    }
#else
    start_address = address;
#endif

    end_address = start_address + len - 1;
    for (p = start_address; p < end_address; p += FLASH_PAGE_SIZE) {
        uint32_t reg;
        uint32_t base;
        uint32_t bnksel = 0;
        base = FLASHMEM_ADDRESS_SPACE;
        reg = FLASH_CR & (~((FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) | FLASH_CR_SER | FLASH_CR_BER | FLASH_CR_PG | FLASH_CR_MER | FLASH_CR_BKSEL));
        if (p >= FLASH_BANK2_BASE && p <= FLASH_TOP)
        {
            base = FLASH_BANK2_BASE;
            bnksel = 1;
        }
        /* Check for swapped banks to invert bnksel */
        if ((FLASH_OPTSR_CUR & FLASH_OPTSR_SWAP_BANK) >> 31)
            bnksel = !bnksel;
        reg |= ((((p - base)  >> 13) << FLASH_CR_PNB_SHIFT) | FLASH_CR_SER | (bnksel << 31));
        FLASH_CR = reg;
        ISB();
        FLASH_CR |= FLASH_CR_STRT;
        hal_flash_wait_complete(bnksel);
    }
    /* If the erase operation is completed, disable the associated bits */
    FLASH_CR &= ~FLASH_CR_SER ;

#if TZ_SECURE()
    if (!(address & FLASH_SECURE_MMAP_BIT) && is_flash_nonsecure(address)) {
        hal_tz_release_nonsecure_area();
    }
#endif
    return 0;
}

static void clock_pll_off(void)
{
    /* Select HSI as SYSCLK source. */
    RCC_CFGR1 &= ~(0x07 << RCC_CFGR1_SW_SHIFT);
    DMB();

    /* Turn off PLL1 */
    RCC_PLL1CFGR &= ~RCC_PLLCFGR_PLL1PEN;
    DMB();
    RCC_CR &= ~RCC_CR_PLL1ON;
    DMB();
    /* Wait until PLL1 is disabled */
    while ((RCC_CR & RCC_CR_PLL1RDY) != 0)
        ;

    /* Turn off PLL2 */
    RCC_PLL2CFGR &= ~RCC_PLLCFGR_PLLPEN;
    DMB();
    RCC_CR &= ~RCC_CR_PLL2ON;
    DMB();
    /* Wait until PLL2 is disabled */
    while ((RCC_CR & RCC_CR_PLL2RDY) != 0)
        ;


}

/* If PLL_SRC_HSE is set then HSE (8MHz) is used otherwise HSI 64 MHz is used
 * and system clock is 250MHz */

static void clock_pll_on(void)
{
    uint32_t reg32;
    uint32_t plln, pllm, pllq, pllp, pllr, hpre, apb1pre, apb2pre, apb3pre, flash_waitstates;

#if PLL_SRC_HSE
    pllm = 1;
    plln = 62;
    pllp = 2; /* 250Mhz */
    pllq = 5; /* 100Mhz */
    pllr = 2;
#else
    pllm = 4;
    plln = 31;
    pllp = 2;
    pllq = 5;
    pllr = 2;
#endif
    flash_waitstates = 5;

    /* Set voltage scaler */
    reg32 = PWR_VOSCR & (~PWR_VOS_MASK);
    PWR_VOSCR = reg32 | PWR_VOS_SCALE_0;

    /* Wait until scale has changed */
    while ((PWR_VOSSR & PWR_VOSRDY) == 0)
        ;

    /* Disable PLL1 */
    RCC_CR &= ~RCC_CR_PLL1ON;

    /* Wait until PLL1 is disabled */
    while ((RCC_CR & RCC_CR_PLL1RDY) != 0)
        ;

    /* Set flash wait states */
    flash_set_waitstates(flash_waitstates);

#if PLL_SRC_HSE
    /* PLL Oscillator configuration */
    RCC_CR |= RCC_CR_HSEON | RCC_CR_HSEBYP;

    /* Wait until HSE is Ready */
    while ((RCC_CR & RCC_CR_HSERDY) == 0)
        ;

    /* Configure PLL1 div/mul factors */
    reg32 = RCC_PLL1CFGR;
    reg32 &= ~((0x3F << RCC_PLLCFGR_PLLM_SHIFT) | (0x03));
    reg32 |= (pllm << RCC_PLLCFGR_PLLM_SHIFT) | RCC_PLLCFGR_PLLSRC_HSE;
    RCC_PLL1CFGR = reg32;
#else
    RCC_CR |= RCC_CR_HSION;

    /* Wait until HSI is Ready */
    while ((RCC_CR & RCC_CR_HSIRDY) == 0)
          ;

    RCC_CR |= RCC_CR_CSION;

    /* Wait until CSI is Ready */
    while ((RCC_CR & RCC_CR_HSIRDY) == 0)
          ;

    /* Configure PLL1 div/mul factors */
    reg32 = RCC_PLL1CFGR;
    reg32 &= ~((0x3F << RCC_PLLCFGR_PLLM_SHIFT) | (0x03));
    reg32 |= (pllm << RCC_PLLCFGR_PLLM_SHIFT) | RCC_PLLCFGR_PLLSRC_CSI;
    RCC_PLL1CFGR = reg32;

#endif
    DMB();

    RCC_PLL1DIVR = ((plln - 1) << RCC_PLLDIVR_DIVN_SHIFT) |
                   ((pllp - 1) << RCC_PLLDIVR_DIVP_SHIFT) |
                   ((pllq - 1) << RCC_PLLDIVR_DIVQ_SHIFT) |
                   ((pllr - 1) << RCC_PLLDIVR_DIVR_SHIFT);
    DMB();

    /* Disable Fractional PLL */
    RCC_PLL1CFGR &= ~RCC_PLLCFGR_PLLFRACEN;
    DMB();

    /* Configure Fractional PLL factor */
    RCC_PLL1FRACR = 0x00000000;
    DMB();

    /* Enable Fractional PLL */
    RCC_PLL1CFGR |= RCC_PLLCFGR_PLLFRACEN;
    DMB();

    /* Select PLL1 Input frequency range: VCI */
    RCC_PLL1CFGR |= RCC_PLLCFGR_RGE_2_4 << RCC_PLLCFGR_PLLRGE_SHIFT;

    /* Select PLL1 Output frequency range: VCO = 0 */
    RCC_PLL1CFGR &= ~RCC_PLLCFGR_PLLVCOSEL;
    DMB();

    /* Enable PLL1 system clock out (DIV: P and Q) */
    RCC_PLL1CFGR |= RCC_PLLCFGR_PLL1PEN | RCC_PLLCFGR_PLL1QEN;

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
    reg32 |= (   (hpre) << RCC_CFGR2_HPRE_SHIFT) |
             ((apb1pre) << RCC_CFGR2_PPRE1_SHIFT) |
             ((apb2pre) << RCC_CFGR2_PPRE2_SHIFT) |
             ((apb3pre) << RCC_CFGR2_PPRE3_SHIFT);
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

    /* Set PLL1 as system clock */
    RCC_PLL1CFGR |= RCC_PLLCFGR_PLL1PEN;

}

#if (TZ_SECURE())

#define NVIC_ISER_BASE (0xE000E100)
#define NVIC_ICER_BASE (0xE000E180)
#define NVIC_IPRI_BASE (0xE000E400)
#define NVIC_USART3_IRQ 60

/* Cortex M-33 has an extra register to set up non-secure interrupts */
#define NVIC_ITNS_BASE (0xE000E380)



static void periph_unsecure(void)
{
    volatile uint32_t reg;
    volatile uint32_t *nvic_itns;
    uint32_t nvic_reg_pos, nvic_reg_off;

    /* Enable clock for User LED GPIOs */
    RCC_AHB2_CLOCK_ER |= LED_AHB2_ENABLE;

    /* Enable GPIO clock for accessing SECCFGR registers */
    RCC_AHB2_CLOCK_ER |= GPIOA_AHB2_CLOCK_ER;
    RCC_AHB2_CLOCK_ER |= GPIOB_AHB2_CLOCK_ER;
    RCC_AHB2_CLOCK_ER |= GPIOC_AHB2_CLOCK_ER;
    RCC_AHB2_CLOCK_ER |= GPIOD_AHB2_CLOCK_ER;

    /* Enable clock for LPUART1 */
    RCC_APB2_CLOCK_ER |= UART1_APB2_CLOCK_ER_VAL;

    /* Enable clock for USART3 */
    RCC_APB1L_CLOCK_ER |= UART3_APB1L_CLOCK_ER_VAL;


    PWR_CR2 |= PWR_CR2_IOSV;
    /* Un-secure User LED GPIO pins */
    GPIO_SECCFGR(GPIOG_BASE) &= ~(1 << LED_BOOT_PIN);  /* PG4 - Nucleo board - Orange Led */
    GPIO_SECCFGR(GPIOB_BASE) &= ~(1 << LED_USR_PIN);   /* PB0 - Nucleo board - Green Led */
    GPIO_SECCFGR(GPIOF_BASE) &= ~(1 << LED_EXTRA_PIN); /* PF4 - Nucleo board - Blue Led */

    /* Unsecure LPUART1 */
    GPIO_SECCFGR(GPIOB_BASE) &= ~(1<<UART1_TX_PIN);
    GPIO_SECCFGR(GPIOB_BASE) &= ~(1<<UART1_RX_PIN);
    reg = TZSC_SECCFGR2;
    if (reg & TZSC_SECCFGR2_LPUART1SEC) {
        reg &= (~TZSC_SECCFGR2_LPUART1SEC);
        DMB();
        TZSC_SECCFGR2 = reg;
    }
    /* Unsecure USART3 */
    GPIO_SECCFGR(GPIOD_BASE) &= ~(1<<UART3_TX_PIN);
    GPIO_SECCFGR(GPIOD_BASE) &= ~(1<<UART3_RX_PIN);
    reg = TZSC_SECCFGR1;
    if (reg & TZSC_SECCFGR1_USART3SEC) {
        reg &= (~TZSC_SECCFGR1_USART3SEC);
        DMB();
        TZSC_SECCFGR1 = reg;
    }

    /* Set USART3 interrupt as non-secure */
    nvic_reg_pos = NVIC_USART3_IRQ / 32;
    nvic_reg_off = NVIC_USART3_IRQ % 32;
    nvic_itns = ((volatile uint32_t *)(NVIC_ITNS_BASE + 4 * nvic_reg_pos));
    *nvic_itns |= (1 << nvic_reg_off);
}
#endif /* TZ_SECURE() */


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
#ifdef WOLFCRYPT_SECURE_MODE
    uint8_t wolfboot_final_sector =
        (WOLFBOOT_PARTITION_BOOT_ADDRESS - FLASHMEM_ADDRESS_SPACE) / WOLFBOOT_SECTOR_SIZE - 1;
    uint8_t partition_final_sector =
        wolfboot_final_sector + (WOLFBOOT_PARTITION_SIZE / WOLFBOOT_SECTOR_SIZE);
#endif
    cur_opts = (FLASH_OPTSR_CUR & FLASH_OPTSR_SWAP_BANK) >> 31;
    hal_flash_clear_errors(0);
    hal_flash_unlock();
    hal_flash_opt_unlock();
    if (cur_opts) {
        FLASH_OPTSR_PRG &= ~(FLASH_OPTSR_SWAP_BANK);
#ifdef WOLFCRYPT_SECURE_MODE
        FLASH_SECWM1R_PRG = wolfboot_final_sector << FLASH_SECWM_END_SHIFT;
        FLASH_SECWM2R_PRG = partition_final_sector << FLASH_SECWM_END_SHIFT;
#endif
    }
    else {
        FLASH_OPTSR_PRG |= FLASH_OPTSR_SWAP_BANK;
#ifdef WOLFCRYPT_SECURE_MODE
        FLASH_SECWM1R_PRG = partition_final_sector << FLASH_SECWM_END_SHIFT;
        FLASH_SECWM2R_PRG = wolfboot_final_sector << FLASH_SECWM_END_SHIFT;
#endif
    }

    FLASH_OPTCR |= FLASH_OPTCR_OPTSTRT;
    DMB();
    hal_flash_opt_lock();
    hal_flash_lock();
    stm32h5_reboot();
}


#define BOOTLOADER_COPY_MEM_SIZE 0x1000
static uint8_t bootloader_copy_mem[BOOTLOADER_COPY_MEM_SIZE];

static void fork_bootloader(void)
{
    uint32_t data = (uint32_t) FLASHMEM_ADDRESS_SPACE;
    uint32_t dst  = FLASH_BANK2_BASE;
    int i;


#if TZ_SECURE()
    data = (uint32_t)((data & (~FLASHMEM_ADDRESS_SPACE)) | FLASH_SECURE_MMAP_BASE);
    dst = (uint32_t)((dst & (~FLASHMEM_ADDRESS_SPACE)) | FLASH_SECURE_MMAP_BASE);
#endif

    /* Return if content already matches */
    if (memcmp((void *)data, (const char*)dst, BOOTLOADER_SIZE) == 0)
        return;

    hal_flash_unlock();
    /* Mass-erase second block */
    hal_flash_erase(dst, BOOTLOADER_SIZE);
    /* Read the wolfBoot image in RAM */
    for (i = 0; i < BOOTLOADER_SIZE;
            i += BOOTLOADER_COPY_MEM_SIZE) {
        memcpy(bootloader_copy_mem, (void*)(data + i),
                BOOTLOADER_COPY_MEM_SIZE);
        hal_flash_write(dst + i, bootloader_copy_mem,
                BOOTLOADER_COPY_MEM_SIZE);
    }
    hal_flash_lock();
}
#endif

void hal_init(void)
{
    clock_pll_on();

#ifdef DEBUG_UART
    uart_init(115200, 8, 'N', 1);
    uart_write("wolfBoot Init\n", 14);
#endif

#if TZ_SECURE()
    hal_gtzc_init();
    hal_tz_sau_init();
#endif

#if defined(DUALBANK_SWAP) && defined(__WOLFBOOT)
    fork_bootloader();
#endif
}



void hal_prepare_boot(void)
{

    /* Keep clock settings when staging a NS-application */
#if (TZ_SECURE())
    periph_unsecure();
#else
    #ifdef WOLFBOOT_RESTORE_CLOCK
    clock_pll_off();
    #endif
#endif
}

#ifdef FLASH_OTP_KEYSTORE

#define FLASH_OTP_BLOCK_SIZE (64)

/* Public API */

int hal_flash_otp_set_readonly(uint32_t flashAddress, uint16_t length)
{
    uint32_t start_block = (flashAddress - FLASH_OTP_BASE) / FLASH_OTP_BLOCK_SIZE;
    uint32_t count = length / FLASH_OTP_BLOCK_SIZE;
    uint32_t bmap = 0;
    unsigned int i;
    if (start_block + count > 32)
        return -1;

    if ((length % FLASH_OTP_BLOCK_SIZE) != 0)
    {
        count++;
    }

    /* Turn on the bits */
    for (i = start_block; i < (start_block + count); i++) {
        bmap |= (1 << i);
    }
    /* Enable OTP write protection for the selected blocks */
    while ((bmap & FLASH_OTPBLR_CUR) != bmap) {
        FLASH_OTPBLR_PRG |= bmap;
        ISB();
        DSB();
    }
    return 0;
}

int hal_flash_otp_write(uint32_t flashAddress, const void* data, uint16_t length)
{
    volatile uint16_t tmp_msw, tmp_lsw;
    uint16_t *pdata = (uint16_t *)data;
    uint16_t idx = 0;
    if (!(flashAddress >= FLASH_OTP_BASE && flashAddress <= FLASH_OTP_END)) {
        return -1;
    }

    /* Reject misaligned destination address */
    if ((flashAddress & 0x01) != 0) {
        return -1;
    }

    hal_flash_wait_complete(0);
    hal_flash_wait_buffer_empty(0);
    hal_flash_unlock();
    hal_flash_clear_errors(0);

    /* Truncate to 2B alignment */
    length = (length / 2 * 2);

    while ((idx < length) && (flashAddress <= FLASH_OTP_END-1)) {
        hal_flash_wait_complete(0);
        /* Set PG bit */
        FLASH_CR |= FLASH_CR_PG;
        /* Program an OTP word (16 bits) */
        *(volatile uint16_t*)flashAddress = pdata[0];
        /* Program a second OTP word (16 bits) */
        *(volatile uint16_t*)(flashAddress + sizeof(uint16_t)) = pdata[1];
        ISB();
        DSB();

        /* Wait until not busy */
        while ((FLASH_SR & FLASH_SR_BSY) != 0)
            ;

        /* Read it back */
        tmp_msw = *(volatile uint16_t*)flashAddress;
        tmp_lsw = *(volatile uint16_t*)(flashAddress + sizeof(uint16_t));
        if ((tmp_msw != pdata[0]) || (tmp_lsw != pdata[1])) {
            /* Provisioning failed. OTP already programmed? */
            while(1)
                ;
        }

        /* Clear PG bit */
        FLASH_CR &= ~FLASH_CR_PG;

        /* Advance to next two words */
        flashAddress += (2 * sizeof(uint16_t));
        pdata += 2;
        idx += (2 * sizeof(uint16_t));
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
