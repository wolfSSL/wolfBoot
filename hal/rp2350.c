/* rp2350.c
 *
 * Custom HAL implementation. Defines the
 * functions used by wolfboot for raspberry-pi pico2 (rp2350)
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
#include <target.h>
#include "image.h"
#include "printf.h"

#include "hardware/flash.h"

#ifdef TZEN
#include "armv8m_tz.h"
#include "pico/bootrom.h"

#define NVIC_ICER0 (*(volatile uint32_t *)(0xE000E180))
#define NVIC_ICPR0 (*(volatile uint32_t *)(0xE000E280))
#define NVIC_ITNS0 (*(volatile uint32_t *)(0xE000EF00))

#define SCB_VTOR_NS (*(volatile uint32_t *)(0xE002ED08))

#define NSACR (*(volatile uint32_t *)(0xE000ED8C))
#define CPACR (*(volatile uint32_t *)(0xE000ED88))

#define SHCSR (*(volatile uint32_t *)(0xE000ED24))
#define SHCSR_MEMFAULTENA (1 << 16)
#define SHCSR_BUSFAULTENA (1 << 17)
#define SHCSR_USGFAULTENA (1 << 18)


#define ACCESS_BITS_DBG (1 << 7)
#define ACCESS_BITS_DMA (1 << 6)
#define ACCESS_BITS_CORE1 (1 << 5)
#define ACCESS_BITS_CORE0 (1 << 4)
#define ACCESS_BITS_SP    (1 << 3)
#define ACCESS_BITS_SU    (1 << 2)
#define ACCESS_BITS_NSP   (1 << 1)
#define ACCESS_BITS_NSU   (1 << 0)
#define ACCESS_MAGIC (0xACCE0000)


#define ACCESS_CONTROL (0x40060000)
#define ACCESS_CONTROL_LOCK             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0000))
#define ACCESS_CONTROL_FORCE_CORE_NS    (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0004))
#define ACCESS_CONTROL_CFGRESET         (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0008))
#define ACCESS_CONTROL_GPIOMASK0       (*(volatile uint32_t *)(ACCESS_CONTROL + 0x000C))
#define ACCESS_CONTROL_GPIOMASK1       (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0010))
#define ACCESS_CONTROL_ROM              (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0014))
#define ACCESS_CONTROL_XIP_MAIN         (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0018))
#define ACCESS_CONTROL_SRAM(block)      (*(volatile uint32_t *)(ACCESS_CONTROL + 0x001C + (block) * 4))  /* block = 0..9 */
#define ACCESS_CONTROL_DMA              (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0044))
#define ACCESS_CONTROL_USBCTRL          (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0048))
#define ACCESS_CONTROL_PIO0             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x004C))
#define ACCESS_CONTROL_PIO1             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0050))
#define ACCESS_CONTROL_PIO2             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0054))
#define ACCESS_CONTROL_CORESIGHT_TRACE  (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0058))
#define ACCESS_CONTROL_CORESIGHT_PERIPH (*(volatile uint32_t *)(ACCESS_CONTROL + 0x005C))
#define ACCESS_CONTROL_SYSINFO          (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0060))
#define ACCESS_CONTROL_RESETS           (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0064))
#define ACCESS_CONTROL_IO_BANK0         (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0068))
#define ACCESS_CONTROL_IO_BANK1         (*(volatile uint32_t *)(ACCESS_CONTROL + 0x006C))
#define ACCESS_CONTROL_PADS_BANK0       (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0070))
#define ACCESS_CONTROL_PADS_QSPI        (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0074))
#define ACCESS_CONTROL_BUSCTRL          (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0078))
#define ACCESS_CONTROL_ADC              (*(volatile uint32_t *)(ACCESS_CONTROL + 0x007C))
#define ACCESS_CONTROL_HSTX             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0080))
#define ACCESS_CONTROL_I2C0             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0084))
#define ACCESS_CONTROL_I2C1             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0088))
#define ACCESS_CONTROL_PWM              (*(volatile uint32_t *)(ACCESS_CONTROL + 0x008C))
#define ACCESS_CONTROL_SPI0             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0090))
#define ACCESS_CONTROL_SPI1             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0094))
#define ACCESS_CONTROL_TIMER0           (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0098))
#define ACCESS_CONTROL_TIMER1           (*(volatile uint32_t *)(ACCESS_CONTROL + 0x009C))
#define ACCESS_CONTROL_UART0            (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00A0))
#define ACCESS_CONTROL_UART1            (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00A4))
#define ACCESS_CONTROL_OTP              (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00A8))
#define ACCESS_CONTROL_TBMAN            (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00AC))
#define ACCESS_CONTROL_POWMAN           (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00B0))
#define ACCESS_CONTROL_TRNG             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00B4))
#define ACCESS_CONTROL_SHA256           (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00B8))
#define ACCESS_CONTROL_SYSCFG           (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00BC))
#define ACCESS_CONTROL_CLOCKS           (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00C0))
#define ACCESS_CONTROL_XOSC             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00C4))
#define ACCESS_CONTROL_ROSC             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00C8))
#define ACCESS_CONTROL_PLL_SYS          (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00CC))
#define ACCESS_CONTROL_PLL_USB          (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00D0))
#define ACCESS_CONTROL_TICKS            (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00D4))
#define ACCESS_CONTROL_WATCHDOG         (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00D8))
#define ACCESS_CONTROL_PSM              (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00DC))
#define ACCESS_CONTROL_XIP_CTRL         (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00E0))
#define ACCESS_CONTROL_XIP_QMI          (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00E4))
#define ACCESS_CONTROL_XIP_AUX          (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00E8))

#endif

#ifdef __WOLFBOOT
void hal_init(void)
{
#ifdef PRINTF_ENABLED
    stdio_init_all();
#endif
}

#ifdef TZEN
static void rp2350_configure_sau(void)
{
    /* Disable SAU */
    SAU_CTRL = 0;
    sau_init_region(0, 0x10000000, 0x1002FFFF, 1); /* Secure flash */
    sau_init_region(1, 0x10030000, 0x1003FFFF, 1); /* Non-secure-callable flash */
    sau_init_region(2, 0x10040000, 0x101FFFFF, 0); /* Non-secure flash */
    sau_init_region(3, 0x20000000, 0x2003FFFF, 1); /* Secure RAM (Low 256K) */
    sau_init_region(4, 0x20040000, 0x20081FFF, 0); /* Non-secure RAM (High 256 + 8K) */
    sau_init_region(6, 0x40000000, 0x5FFFFFFF, 0); /* Non-secure peripherals */
    sau_init_region(7, 0xD0000000, 0xDFFFFFFF, 0); /* Non-secure SIO region */


    /* Enable SAU */
    SAU_CTRL = 1;

    /* Enable MemFault, BusFault and UsageFault */
    SHCSR |= SHCSR_MEMFAULTENA | SHCSR_BUSFAULTENA | SHCSR_USGFAULTENA;

    /* Add flag to trap misaligned accesses */
    *((volatile uint32_t *)0xE000ED14) |= 0x00000008;
}

static void rp2350_configure_nvic(void)
{
    /* Disable all interrupts */
    NVIC_ICER0 = 0xFFFFFFFF;
    NVIC_ICPR0 = 0xFFFFFFFF;

    /* Set all interrupts to non-secure */
    NVIC_ITNS0 = 0xFFFFFFFF;
}

static void rp2350_configure_access_control(void)
{
    int i;
    const uint32_t secure_fl = (ACCESS_BITS_SU | ACCESS_BITS_SP | ACCESS_BITS_DMA | ACCESS_BITS_DBG | ACCESS_BITS_CORE0) | ACCESS_MAGIC;
    const uint32_t non_secure_fl = (ACCESS_BITS_NSU | ACCESS_BITS_NSP | ACCESS_BITS_DMA | ACCESS_BITS_DBG | ACCESS_BITS_CORE0 | ACCESS_BITS_CORE1) | ACCESS_MAGIC;

    /* Set access control to Secure for lower RAM (0x20000000 - 0x2003FFFF) */
    for (i = 0; i < 4; i ++)
        ACCESS_CONTROL_SRAM(i) = secure_fl;

    /* Set access control to Non-secure for upper RAM (0x20040000 - 0x20081FFF) */
    for (i = 4; i < 10; i++)
        ACCESS_CONTROL_SRAM(i) = non_secure_fl | secure_fl;

    /* Set access control for peripherals */
    ACCESS_CONTROL_ROM = secure_fl | non_secure_fl;
    ACCESS_CONTROL_XIP_MAIN = non_secure_fl | secure_fl;
    ACCESS_CONTROL_DMA = non_secure_fl;
    ACCESS_CONTROL_TRNG = secure_fl;
    ACCESS_CONTROL_SYSCFG = secure_fl;
    ACCESS_CONTROL_SHA256 = secure_fl;
    ACCESS_CONTROL_IO_BANK0 = non_secure_fl | secure_fl;
    ACCESS_CONTROL_IO_BANK1 = non_secure_fl | secure_fl;
    ACCESS_CONTROL_PADS_BANK0 = non_secure_fl | secure_fl;
    ACCESS_CONTROL_PIO0 = non_secure_fl | secure_fl;
    ACCESS_CONTROL_PIO1 = non_secure_fl | secure_fl;
    ACCESS_CONTROL_PIO2 = non_secure_fl | secure_fl;

    ACCESS_CONTROL_I2C0   = non_secure_fl |secure_fl;
    ACCESS_CONTROL_I2C1   = non_secure_fl | secure_fl;
    ACCESS_CONTROL_PWM    = non_secure_fl | secure_fl;
    ACCESS_CONTROL_SPI0   = non_secure_fl | secure_fl;
    ACCESS_CONTROL_SPI1   = non_secure_fl | secure_fl;
    ACCESS_CONTROL_TIMER0 = non_secure_fl | secure_fl;
    ACCESS_CONTROL_TIMER1 = non_secure_fl | secure_fl;
    ACCESS_CONTROL_UART0  = non_secure_fl | secure_fl;
    ACCESS_CONTROL_UART1  = non_secure_fl | secure_fl;
    ACCESS_CONTROL_ADC    = non_secure_fl | secure_fl;
    ACCESS_CONTROL_RESETS = non_secure_fl | secure_fl;

    /* Force core 1 to non-secure */
    ACCESS_CONTROL_FORCE_CORE_NS = (1 << 1) | ACCESS_MAGIC;

    /* GPIO masks: Each bit represents "NS allowed" for a GPIO pin */
    ACCESS_CONTROL_GPIOMASK0 = 0xFFFFFFFF;
    ACCESS_CONTROL_GPIOMASK1 = 0xFFFFFFFF;

    CPACR |= 0x000000FF; /* Enable access to coprocessors CP0-CP7 */
    NSACR |= 0x000000FF; /* Enable non-secure access to coprocessors CP0-CP7 */

    /* Lock access control */
    ACCESS_CONTROL_LOCK = non_secure_fl | secure_fl;
}
#endif


void hal_prepare_boot(void)
{
#ifdef TZEN
    rp2350_configure_sau();
    rp2350_configure_nvic();
    rp2350_configure_access_control();
#endif
}

#endif

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    flash_range_program(address - XIP_BASE, data, len);
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    flash_range_erase(address - XIP_BASE, len);
    return 0;
}

