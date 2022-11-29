/* gd32vf103.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
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
#include "image.h"
#include "loader.h"






/* GD32VF103 register configuration */
#define FLASH_BASE  (0x40022000U)
#define FLASH_WS     *((volatile uint32_t *) (FLASH_BASE + 0x00))
#define FLASH_KEY    *((volatile uint32_t *) (FLASH_BASE + 0x04))
#define FLASH_OBKEY  *((volatile uint32_t *) (FLASH_BASE + 0x08))
#define FLASH_STAT   *((volatile uint32_t *) (FLASH_BASE + 0x0C))
#define FLASH_CTL    *((volatile uint32_t *) (FLASH_BASE + 0x10))
#define FLASH_ADDR   *((volatile uint32_t *) (FLASH_BASE + 0x14))
#define FLASH_OBSTAT *((volatile uint32_t *) (FLASH_BASE + 0x1C))
#define FLASH_WP     *((volatile uint32_t *) (FLASH_BASE + 0x20))
#define FLASH_PID    *((volatile uint32_t *) (FLASH_BASE + 0x100))

#define RCU_BASE     (0x40021000U)
#define RCU_CTL      *((volatile uint32_t *)(RCU_BASE + 0x00U))
#define RCU_CFG0     *((volatile uint32_t *)(RCU_BASE + 0x04U))
#define RCU_INT      *((volatile uint32_t *)(RCU_BASE + 0x08U))
#define RCU_APB2RST  *((volatile uint32_t *)(RCU_BASE + 0x0CU))
#define RCU_APB1RST  *((volatile uint32_t *)(RCU_BASE + 0x10U))
#define RCU_AHBEN    *((volatile uint32_t *)(RCU_BASE + 0x14U))
#define RCU_APB2EN   *((volatile uint32_t *)(RCU_BASE + 0x18U))
#define RCU_APB1EN   *((volatile uint32_t *)(RCU_BASE + 0x1CU))
#define RCU_BDCTL    *((volatile uint32_t *)(RCU_BASE + 0x20U))
#define RCU_RSTSCK   *((volatile uint32_t *)(RCU_BASE + 0x24U))
#define RCU_AHBRST   *((volatile uint32_t *)(RCU_BASE + 0x28U))
#define RCU_CFG1     *((volatile uint32_t *)(RCU_BASE + 0x2CU))
#define RCU_DSV      *((volatile uint32_t *)(RCU_BASE + 0x34U))


#define FLASH_WS_MASK (0x07)

#define FLASH_CTL_PG_CMD     (1 << 0)
#define FLASH_CTL_PAGE_ERASE (1 << 1)
#define FLASH_CTL_START      (1 << 6)
#define FLASH_CTL_LK         (1 << 7)

#define FLASH_STAT_BUSY    (1 << 0)
#define FLASH_STAT_PGERR   (1 << 2)
#define FLASH_STAT_WPERR   (1 << 4)
#define FLASH_STAT_EOO     (1 << 5)

/* unlock key for flash and option bytes */
#define UNLOCK_KEY0                ((uint32_t)0x45670123U)
#define UNLOCK_KEY1                ((uint32_t)0xCDEF89ABU)



/* Assembly helpers */
#define FENCE()  do { __asm__ volatile ("fence"); \
                   __asm__ volatile ("fence.i"); \
                 } while (0)

static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_WS & (~FLASH_WS_MASK);
    FLASH_WS = reg | (waitstates & FLASH_WS_MASK);    
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;

    /* wait until not busy */
    while(FLASH_STAT & FLASH_STAT_BUSY)
        ;
    /* clear other error flags */
    FLASH_STAT = FLASH_STAT_PGERR;
    FENCE();
    FLASH_STAT = FLASH_STAT_WPERR;
    FENCE();
    FLASH_STAT = FLASH_STAT_EOO;

    while (i < len) {
        if ((len - i > 3) && ((((address + i) & 0x03) == 0)  && ((((uint32_t)data) + i) & 0x03) == 0)) {
            src = (uint32_t *)data;
            dst = (uint32_t *)(address);
            FLASH_CTL |= FLASH_CTL_PG_CMD;
            dst[i >> 2] = src[i >> 2];
            i+=4;
        } else {
            uint32_t val;
            uint8_t *vbytes = (uint8_t *)(&val);
            int off = (address + i) - (((address + i) >> 2) << 2);
            dst = (uint32_t *)(address - off);
            val = dst[i >> 2];
            vbytes[off] = data[i];
            FLASH_CTL |= FLASH_CTL_PG_CMD;
            dst[i >> 2] = val;
            i++;
        }
        /* wait until not busy */
        while(FLASH_STAT & FLASH_STAT_BUSY)
            ;
        if (FLASH_STAT & (FLASH_STAT_PGERR | FLASH_STAT_WPERR))
            return -1;
        FLASH_CTL &= (~FLASH_CTL_PG_CMD);
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    if ((FLASH_CTL & FLASH_CTL_LK) != 0) {
        FLASH_KEY = UNLOCK_KEY0;
        FENCE();
        FLASH_KEY = UNLOCK_KEY1;
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
    FLASH_CTL |= FLASH_CTL_LK;
}


int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int start = -1, end = -1;
    uint32_t end_address;
    uint32_t p;
    if (len == 0)
        return -1;
    end_address = address + len - 1;

    /* wait until not busy */
    while(FLASH_STAT & FLASH_STAT_BUSY)
        ;
    /* clear other flags */
    FLASH_STAT = FLASH_STAT_PGERR;
    FENCE();
    FLASH_STAT = FLASH_STAT_WPERR;
    FENCE();
    FLASH_STAT = FLASH_STAT_EOO;

    while (address < end_address) {
        /* Erase page(s) */
        FLASH_CTL |= FLASH_CTL_PAGE_ERASE;
        FLASH_ADDR = address;
        FLASH_CTL |= FLASH_CTL_START;
        while(FLASH_STAT & FLASH_STAT_BUSY)
            ;
        FLASH_CTL &= (~FLASH_CTL_PAGE_ERASE);
        if (FLASH_STAT & (FLASH_STAT_PGERR | FLASH_STAT_WPERR))
            return -1;
        address += WOLFBOOT_SECTOR_SIZE;
    }
    return 0;
}


#ifdef __WOLFBOOT

#define RCU_CTL_IRC8MEN      (1 << 0)
#define RCU_CTL_IRC8MSTB     (1 << 1)
#define RCU_CTL_HXTALEN      (1 << 16)
#define RCU_CTL_HXTALSTB     (1 << 17)
#define RCU_CTL_HXTALBPS     (1 << 18)
#define RCU_CTL_CKMEN        (1 << 19)
#define RCU_CTL_PLLEN        (1 << 24)
#define RCU_CTL_PLLSTB       (1 << 25)
#define RCU_CTL_PLL1EN       (1 << 26)
#define RCU_CTL_PLL1STB      (1 << 27)
#define RCU_CTL_PLL2EN       (1 << 28)
#define RCU_CTL_PLL2STB      (1 << 29)

#define RCU_CFG0_SCS         (0x03 << 0)
#define RCU_CFG0_AHBPSC      (0x0F << 4)
#define RCU_CFG0_APB1PSC     (0x07 << 8)
#define RCU_CFG0_APB2PSC     (0x07 << 11)
#define RCU_CFG0_ADCPSC      (0x03 << 14)
#define RCU_CFG0_PLLSEL      (1 << 16)
#define RCU_CFG0_PREDV0_LSB  (1 << 17)
#define RCU_CFG0_PLLMF       (0x0F << 18)
#define RCU_CFG0_USBFSPSC    (0x03 << 22)
#define RCU_CFG0_ADCPSC_2    (1 << 28)
#define RCU_CFG0_CKOUT0SEL   (0x0F << 24)
#define RCU_CFG0_PLLMF_4     (1 << 29)

#define RCU_CFG1_PREDV0      (0x0F << 0)
#define RCU_CFG1_PREDV1      (0x0F << 4)
#define RCU_CFG1_PLL1MF      (0x0F << 8)
#define RCU_CFG1_PLL2MF      (0x0F << 12)
#define RCU_CFG1_PREDV0SEL   (1 << 16)
#define RCU_CFG1_I2S1SEL     (1 << 17)
#define RCU_CFG1_I2S2SEL     (1 << 18)

#define RCU_PREDV0SRC_HXTAL  (0) 
#define RCU_PREDV0_DIV2      (1 << 0)
#define RCU_PREDV1_DIV2      (1 << 4)
#define RCU_PLL1_MUL20       (15 << 8)
#define RCU_PLL2_MUL20       (15 << 12)

#define RCU_CKSYSSRC_PLL     (2 << 0)
#define RCU_SCSS_PLL         (2 << 2)


static void system_clock_config(void)
{
    volatile uint32_t timeout   = 0U;
    volatile uint32_t stab_flag = 0U;

    /* enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* wait until HXTAL is stable or the startup time is longer than HXTAL_STARTUP_TIMEOUT */
    do{
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
        FENCE();
    }while(0U == stab_flag);

    /* if fail */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        wolfBoot_panic();
    }

    /* HXTAL is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= (0 << 4);
    /* APB2 = AHB/1 */
    RCU_CFG0 |= (0 << 11);
    /* APB1 = AHB/2 */
    RCU_CFG0 |= (4 << 8);

    /* CK_PLL = (CK_PREDIV0) * 27 = 108 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= (RCU_CFG0_PLLSEL | RCU_CFG0_PLLMF_4 | (10 << 18));

    RCU_CFG1 &= ~(RCU_CFG1_PREDV0SEL | RCU_CFG1_PREDV1 | RCU_CFG1_PLL1MF | RCU_CFG1_PREDV0);
    RCU_CFG1 |= (RCU_PREDV0SRC_HXTAL | RCU_PREDV0_DIV2 | RCU_PREDV1_DIV2 | RCU_PLL1_MUL20 | RCU_PLL2_MUL20);

    /* enable PLL1 */
    RCU_CTL |= RCU_CTL_PLL1EN;
    /* wait till PLL1 is ready */
    while(0U == (RCU_CTL & RCU_CTL_PLL1STB)){
        FENCE();
    }

    /* enable PLL2 */
    RCU_CTL |= RCU_CTL_PLL2EN;
    /* wait till PLL1 is ready */
    while(0U == (RCU_CTL & RCU_CTL_PLL2STB)){
        FENCE();
    }

    /* enable PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* wait until PLL is stable */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
        FENCE();
    }

    /* select PLL as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* wait until PLL is selected as system clock */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
        FENCE();
    }
}

void hal_init(void)
{
    /* reset the RCC clock configuration to the default reset state */
    /* enable IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;
    while(0U == (RCU_CTL & RCU_CTL_IRC8MSTB)){

    }
    RCU_CFG0 |= (8 << 4);
    FENCE();
    RCU_CFG0 |= (9 << 4);
    /* reset SCS, AHBPSC, APB1PSC, APB2PSC, ADCPSC, CKOUT0SEL bits */
    RCU_CFG0 &= ~(RCU_CFG0_SCS | RCU_CFG0_AHBPSC | RCU_CFG0_APB1PSC | RCU_CFG0_APB2PSC |
            RCU_CFG0_ADCPSC | RCU_CFG0_ADCPSC_2 | RCU_CFG0_CKOUT0SEL);

    /* reset HXTALEN, CKMEN, PLLEN bits */
    FENCE();
    RCU_CTL &= ~(RCU_CTL_HXTALEN | RCU_CTL_CKMEN | RCU_CTL_PLLEN);

    /* Reset HXTALBPS bit */
    FENCE();
    RCU_CTL &= ~(RCU_CTL_HXTALBPS);

    /* reset PLLSEL, PREDV0_LSB, PLLMF, USBFSPSC bits */

    FENCE();
    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0_LSB | RCU_CFG0_PLLMF |
            RCU_CFG0_USBFSPSC | RCU_CFG0_PLLMF_4);
    RCU_CFG1 = 0x00000000U;

    /* Reset HXTALEN, CKMEN, PLLEN, PLL1EN and PLL2EN bits */
    FENCE();
    RCU_CTL &= ~(RCU_CTL_PLLEN | RCU_CTL_PLL1EN | RCU_CTL_PLL2EN | RCU_CTL_CKMEN | RCU_CTL_HXTALEN);
    /* disable all interrupts */
    RCU_INT = 0x00FF0000U;

    flash_set_waitstates(2);

    /* Configure the System clock source, PLL Multiplier, AHB/APBx prescalers and Flash settings */
    system_clock_config();
}

void hal_prepare_boot(void)
{
#ifdef SPI_FLASH
    spi_release();
#endif
    hal_flash_lock();

    /* enable IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;
    while(0U == (RCU_CTL & RCU_CTL_IRC8MSTB)){
    }
    /* reset CTL register */
    RCU_CTL &= ~(RCU_CTL_HXTALEN | RCU_CTL_CKMEN | RCU_CTL_PLLEN);
    RCU_CTL &= ~RCU_CTL_HXTALBPS;
    RCU_CTL &= ~(RCU_CTL_PLL1EN | RCU_CTL_PLL2EN);
    /* reset CFG0 register */
    RCU_CFG0 &= ~(RCU_CFG0_SCS | RCU_CFG0_AHBPSC | RCU_CFG0_APB1PSC | RCU_CFG0_APB2PSC |
                  RCU_CFG0_ADCPSC | RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0_LSB | RCU_CFG0_PLLMF |
                  RCU_CFG0_USBFSPSC | RCU_CFG0_CKOUT0SEL | RCU_CFG0_ADCPSC_2 | RCU_CFG0_PLLMF_4);
    /* reset INT and CFG1 register */
    RCU_INT = 0x00ff0000U;
    RCU_CFG1 &= ~(RCU_CFG1_PREDV0 | RCU_CFG1_PREDV1 | RCU_CFG1_PLL1MF | RCU_CFG1_PLL2MF |
                  RCU_CFG1_PREDV0SEL | RCU_CFG1_I2S1SEL | RCU_CFG1_I2S2SEL);



}

#endif /* __WOLFBOOT */
