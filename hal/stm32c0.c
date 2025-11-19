/* stm32c0.c
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
#include <image.h>

#ifndef NVM_FLASH_WRITEONCE
#   error "wolfBoot STM32C0 HAL: no WRITEONCE support detected. Please define NVM_FLASH_WRITEONCE"
#endif

/* STM32 C0 register configuration */

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define ISB() __asm__ volatile ("isb")
#define DSB() __asm__ volatile ("dsb")


/*** RCC ***/
#define RCC_BASE (0x40021000)
#define RCC_CR              (*(volatile uint32_t *)(RCC_BASE + 0x00))  /* RM0490 - 5.4.1 */
#define RCC_CFGR            (*(volatile uint32_t *)(RCC_BASE + 0x08))  /* RM0490 - 5.4.3 */
#define APB1_CLOCK_ER       (*(volatile uint32_t *)(RCC_BASE + 0x3C))  /* RM0490 - 5.4.13 */
#define APB2_CLOCK_ER       (*(volatile uint32_t *)(RCC_BASE + 0x40))  /* RM0490 - 5.4.14 */

#define RCC_CR_HSIRDY               (1 << 10)
#define RCC_CR_HSION                (1 << 8)

#define RCC_CR_HSIDIV_SHIFT 11
#define RCC_CR_HSIDIV_MASK 0x7
#define RCC_CR_HSIDIV_1  (0ul << RCC_CR_HSIDIV_SHIFT)
#define RCC_CR_HSIDIV_2  (1ul << RCC_CR_HSIDIV_SHIFT)
#define RCC_CR_HSIDIV_4  (2ul << RCC_CR_HSIDIV_SHIFT)
#define RCC_CR_HSIDIV_8  (3ul << RCC_CR_HSIDIV_SHIFT)
#define RCC_CR_HSIDIV_16  (4ul << RCC_CR_HSIDIV_SHIFT)
#define RCC_CR_HSIDIV_32  (5ul << RCC_CR_HSIDIV_SHIFT)
#define RCC_CR_HSIDIV_64  (6ul << RCC_CR_HSIDIV_SHIFT)
#define RCC_CR_HSIDIV_128  (7ul << RCC_CR_HSIDIV_SHIFT)

#define RCC_CFGR_SW_HSISYS          0x0

/*** APB PRESCALER ***/
#define RCC_PRESCALER_DIV_NONE 0

/*** FLASH ***/
#define PWR_APB1_CLOCK_ER_VAL       (1 << 28)
#define SYSCFG_APB2_CLOCK_ER_VAL    (1 << 0) /* RM0490 - 5.4.14 - RCC_APBENR2 - SYSCFGEN */

#define FLASH_BASE          (0x40022000)  /*FLASH_R_BASE = 0x40000000UL + 0x00020000UL + 0x00002000UL */
#define FLASH_ACR           (*(volatile uint32_t *)(FLASH_BASE + 0x00)) /* RM0490 - 3.7.1 - FLASH_ACR */
#define FLASH_KEY           (*(volatile uint32_t *)(FLASH_BASE + 0x08)) /* RM0490 - 3.7.2 - FLASH_KEYR */
#define FLASH_OPTKEY        (*(volatile uint32_t *)(FLASH_BASE + 0x0C)) /* RM0490 - 3.7.3 - FLASH_OPTKEYR */
#define FLASH_SR            (*(volatile uint32_t *)(FLASH_BASE + 0x10)) /* RM0490 - 3.7.4 - FLASH_SR */
#define FLASH_CR            (*(volatile uint32_t *)(FLASH_BASE + 0x14)) /* RM0490 - 3.7.5 - FLASH_CR */
#define FLASH_SECR          (*(volatile uint32_t *)(FLASH_BASE + 0x80)) /* RM0490 - 3.7.13 - FLASH_SECR */

#define FLASHMEM_ADDRESS_SPACE (0x08000000)
#define FLASH_PAGE_SIZE        (0x800) /* 2KB */
#define FLASH_PAGE_SIZE_SHIFT  11  /* (1 << FLASH_PAGE_SIZE_SHIFT) == FLASH_PAGE_SIZE*/

/* Register values */

#define FLASH_ACR_LAT_SHIFT                   0      /* RM0490 - 3.7.1 -FLASH_ACT */
#define FLASH_ACR_LAT_MASK                    0x01   /* RM0490 - 3.7.1 -FLASH_ACT */

#define FLASH_SR_BSY1                         (1 << 16) /* RM0490 - 3.7.4 - FLASH_SR */
#define FLASH_SR_SIZERR                       (1 << 6)  /* RM0490 - 3.7.4 - FLASH_SR */
#define FLASH_SR_PGAERR                       (1 << 5)  /* RM0490 - 3.7.4 - FLASH_SR */
#define FLASH_SR_WRPERR                       (1 << 4)  /* RM0490 - 3.7.4 - FLASH_SR */
#define FLASH_SR_PROGERR                      (1 << 3)  /* RM0490 - 3.7.4 - FLASH_SR */
#define FLASH_SR_EOP                          (1 << 0)  /* RM0490 - 3.7.4 - FLASH_SR */

#define FLASH_CR_LOCK                         (1UL << 31) /* RM0490 - 3.7.5 - FLASH_CR */
#define FLASH_CR_STRT                         (1 << 16) /* RM0490 - 3.7.5 - FLASH_CR */

#define FLASH_CR_PER                          (1 << 1)  /* RM0490 - 3.7.5 - FLASH_CR */
#define FLASH_CR_PG                           (1 << 0)  /* RM0490 - 3.7.5 - FLASH_CR */
#define FLASH_CR_SEC_PROT                     (1 << 28) /* RM0490 - 3.7.5 - FLASH_CR */

#define FLASH_CR_PNB_SHIFT                     3        /* RM0490 - 4.7.5 - FLASH_CR - PNB bits 9:3 */
#define FLASH_CR_PNB_MASK                      0x7f     /* RM0490 - 4.7.5 - FLASH_CR - PNB bits 9:3 - 7 bits */

#define FLASH_SECR_SEC_SIZE_POS               (0U)
#define FLASH_SECR_SEC_SIZE_MASK              (0xFF)

#define FLASH_KEY1                            (0x45670123)
#define FLASH_KEY2                            (0xCDEF89AB)

#define FLASH_OPTKEY1                         (0x08192A3B)
#define FLASH_OPTKEY2                         (0x4C5D6E7F)


static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg, mask_val, set_val;
    reg = FLASH_ACR;
    mask_val = FLASH_ACR_LAT_MASK << FLASH_ACR_LAT_SHIFT;
    set_val = (waitstates << FLASH_ACR_LAT_SHIFT) & mask_val;
    if ((reg & mask_val) != set_val)
        FLASH_ACR =  (reg & ~mask_val) | set_val;
}

static RAMFUNCTION void flash_wait_complete(void)
{
    while ((FLASH_SR & FLASH_SR_BSY1) == FLASH_SR_BSY1)
        ;
}

static void RAMFUNCTION flash_clear_errors(void)
{
    /* Consider only writing here as there is no reason to read first (rc_w1),
     * unless other error bits are set*/
    FLASH_SR |= (FLASH_SR_SIZERR | FLASH_SR_PGAERR | FLASH_SR_WRPERR |
                 FLASH_SR_PROGERR);
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;
    flash_clear_errors();
    FLASH_CR |= FLASH_CR_PG;

    while (i < len) {
        flash_clear_errors();
        if ((len - i > 3) && ((((address + i) & 0x07) == 0) &&
                ((((uint32_t)data) + i) & 0x07) == 0)) {
            src = (uint32_t *)data;
            dst = (uint32_t *)(address + FLASHMEM_ADDRESS_SPACE);
            flash_wait_complete();
            dst[i >> 2] = src[i >> 2];
            dst[(i >> 2) + 1] = src[(i >> 2) + 1];
            flash_wait_complete();
            i+=8;
        } else {
            uint32_t val[2];
            uint8_t *vbytes = (uint8_t *)(val);
            int off = (address + i) - (((address + i) >> 3) << 3);
            uint32_t base_addr = address & (~0x07); /* aligned to 64 bit */
            int u32_idx = (i >> 2);
            dst = (uint32_t *)(base_addr);
            val[0] = dst[u32_idx];
            val[1] = dst[u32_idx + 1];
            while ((off < 8) && (i < len))
                vbytes[off++] = data[i++];
            dst[u32_idx] = val[0];
            dst[u32_idx + 1] = val[1];
            flash_wait_complete();
        }
    }
    if ((FLASH_SR & FLASH_SR_EOP) == FLASH_SR_EOP)
        FLASH_SR |= FLASH_SR_EOP;
    FLASH_CR &= ~FLASH_CR_PG;
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flash_wait_complete();
    if ((FLASH_CR & FLASH_CR_LOCK) != 0) {
        FLASH_KEY = FLASH_KEY1;
        DMB();
        FLASH_KEY = FLASH_KEY2;
        DMB();
        while ((FLASH_CR & FLASH_CR_LOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
    flash_wait_complete();
    if ((FLASH_CR & FLASH_CR_LOCK) == 0)
        FLASH_CR |= FLASH_CR_LOCK;
}


int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end_address;
    uint32_t p;
    if (len == 0)
        return -1;
    end_address = address + len - 1;
    for (p = address; p < end_address; p += FLASH_PAGE_SIZE) {
        uint32_t reg = FLASH_CR & (~(FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT));
        FLASH_CR = reg | ((p >> FLASH_PAGE_SIZE_SHIFT) << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER;
        DMB();
        FLASH_CR |= FLASH_CR_STRT;
        flash_wait_complete();
        FLASH_CR &= ~FLASH_CR_PER;
    }
    return 0;
}

static void clock_pll_off(void)
{
    uint32_t reg32;

    /* Select HSISYS as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSISYS);
    DMB();
}

/* This implementation will setup HSI RC 48 MHz as System Clock Source and set
 * flash wait state to 1 */
static void clock_pll_on(int powersave)
{
    uint32_t reg32;
    uint32_t cpu_freq, flash_waitstates;

    /* Enable Power controller */
    APB1_CLOCK_ER |= PWR_APB1_CLOCK_ER_VAL;

    /* Select clock parameters (CPU Speed = 48MHz) */
    cpu_freq = 48000000;
    (void)cpu_freq; /* not used */
    flash_waitstates = 1;

    flash_set_waitstates(flash_waitstates);

    /* Enable internal high-speed oscillator. */
    RCC_CR |= RCC_CR_HSION;
    DMB();
    while ((RCC_CR & RCC_CR_HSIRDY) == 0) {};

    /* Select HSISYS as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSISYS);
    DMB();

    /* SYSCFG, COMP and VREFBUF clock enable */
    APB2_CLOCK_ER |= SYSCFG_APB2_CLOCK_ER_VAL;
}

void hal_init(void)
{
    clock_pll_on(0);
}

#ifdef FLASH_SECURABLE_MEMORY_SUPPORT
static void RAMFUNCTION do_secure_boot(void)
{
    uint32_t sec_size = (FLASH_SECR & FLASH_SECR_SEC_SIZE_MASK);

    /* The "SEC_SIZE" is the number of pages (2KB) to extend from base 0x8000000
     * and it is programmed using the STM32CubeProgrammer option bytes.
     * Example: STM32_Programmer_CLI -c port=swd mode=hotplug -ob SEC_SIZE=  */
#ifndef NO_FLASH_SEC_SIZE_CHECK
    /* Make sure at least the first sector is protected and the size is not
     * larger than boot partition */
    if (sec_size <= 1 ||
        sec_size > (WOLFBOOT_PARTITION_BOOT_ADDRESS / WOLFBOOT_SECTOR_SIZE)) {
        /* panic: invalid sector size */
        while(1)
            ;
    }
#endif

    /* TODO: Add checks for WRP, RDP and BootLock. Add warning to help lock down
     *       target in production */

    /* unlock flash to access FLASH_CR write */
    hal_flash_unlock();

    ISB();

    /* Activate secure user memory */
    /* secure code to make sure SEC_PROT gets set (based on reference code) */
    do {
        FLASH_CR |= FLASH_CR_SEC_PROT;
    } while ((FLASH_CR & FLASH_CR_SEC_PROT) == 0);

    DSB();
}
#endif

void RAMFUNCTION hal_prepare_boot(void)
{
#ifdef SPI_FLASH
    spi_flash_release();
#endif
#ifdef WOLFBOOT_RESTORE_CLOCK
    clock_pll_off();
#endif
#ifdef FLASH_SECURABLE_MEMORY_SUPPORT
    do_secure_boot();
#endif
}
