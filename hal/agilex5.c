/* agilex5.c
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1335, USA
 */

#ifdef TARGET_agilex5

#include <stdint.h>

#include "fdt.h"
#include "hal.h"
#include "hal/agilex5.h"
#include "image.h"
#include "printf.h"

#ifndef ARCH_AARCH64
#error "wolfBoot Agilex 5 HAL requires ARCH=AARCH64"
#endif

#ifndef LINUX_BOOTARGS
#ifndef LINUX_BOOTARGS_ROOT
#define LINUX_BOOTARGS_ROOT "/dev/mmcblk0p4"
#endif
#define LINUX_BOOTARGS \
    "earlycon=uart8250,mmio32,0x10c02000 console=ttyS0,115200 " \
    "root=" LINUX_BOOTARGS_ROOT " rootwait"
#endif

#ifdef DEBUG_UART
#define UART_RBR 0U
#define UART_THR 0U
#define UART_LSR 5U
#define UART_LSR_THRE (1U << 5)
#define UART_LSR_TEMT (1U << 6)
#define UART_TIMEOUT 1000000U
#define UART_REG(_n) \
    (*(volatile uint32_t *)(AGILEX5_UART0_BASE + ((uintptr_t)(_n) << 2)))

void uart_init(void)
{
    /* SPL configured the DesignWare APB UART, its clock and pinmux. */
}

void uart_write(const char *buf, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        uint32_t timeout = UART_TIMEOUT;
        if (buf[i] == '\n') {
            while (((UART_REG(UART_LSR) & UART_LSR_THRE) == 0U) &&
                    (--timeout != 0U)) {}
            if (timeout != 0U)
                UART_REG(UART_THR) = '\r';
            timeout = UART_TIMEOUT;
        }
        while (((UART_REG(UART_LSR) & UART_LSR_THRE) == 0U) &&
                (--timeout != 0U)) {}
        if (timeout != 0U)
            UART_REG(UART_THR) = (uint32_t)(uint8_t)buf[i];
    }
}
#else
#define uart_init() do {} while (0)
#endif

static uint64_t timer_count(void)
{
    uint64_t count;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(count));
    return count;
}

static uint64_t timer_frequency(void)
{
    uint64_t frequency;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    return frequency;
}

uint64_t hal_get_timer_us(void)
{
    uint64_t frequency = timer_frequency();
    uint64_t count = timer_count();
    uint64_t whole;
    uint64_t rem;

    /* BL31 normally programs CNTFRQ_EL0. Keep delay loops live even when a
     * non-conforming handoff leaves it clear.
     */
    if (frequency == 0U)
        frequency = 100000000U;
    /* Split the conversion so it stays in 64-bit math: a __uint128_t divide
     * pulls in libgcc's __udivti3, which the bare-metal link has no runtime
     * for. The whole and remainder parts are exact and cannot overflow. */
    whole = count / frequency;
    rem   = count % frequency;
    return whole * 1000000ULL + (rem * 1000000ULL) / frequency;
}

void hal_init(void)
{
    uart_init();
#if defined(DEBUG_UART) && defined(__WOLFBOOT)
    wolfBoot_printf("\nwolfBoot Secure Boot - Altera Agilex 5\n");
    wolfBoot_printf("TF-A BL33 entry, current EL: %d\n", current_el());
#endif
}

void hal_prepare_boot(void)
{
#ifdef DEBUG_UART
    uint32_t timeout = UART_TIMEOUT;
    while (((UART_REG(UART_LSR) & UART_LSR_TEMT) == 0U) &&
            (--timeout != 0U)) {}
#endif
    __asm__ volatile("dsb sy" : : : "memory");
    __asm__ volatile("ic iallu" : : : "memory");
    __asm__ volatile("dsb sy" : : : "memory");
    __asm__ volatile("isb" : : : "memory");
}

#ifdef MMU
void *hal_get_dts_address(void)
{
    return (void *)WOLFBOOT_LOAD_DTS_ADDRESS;
}

void *hal_get_dts_update_address(void)
{
#ifdef WOLFBOOT_DTS_UPDATE_ADDRESS
    return (void *)WOLFBOOT_DTS_UPDATE_ADDRESS;
#else
    return NULL;
#endif
}

#ifdef __WOLFBOOT
int hal_dts_fixup(void *dts_addr)
{
    struct fdt_header *fdt = (struct fdt_header *)dts_addr;
    int off;
    int ret = fdt_check_header(dts_addr);

    if (ret != 0) {
        wolfBoot_printf("FDT: invalid header (%d)\n", ret);
        return ret;
    }
    fdt_set_totalsize(fdt,
        fdt_totalsize(fdt) + WOLFBOOT_FDT_FIXUP_HEADROOM);

    /* U-Boot normally patches the memory node after SPL has measured the
     * LPDDR4.  wolfBoot bypasses U-Boot and passes this DTB directly to
     * Linux, so preserve the board's 1792 MiB memory map explicitly. */
    off = fdt_find_devtype(fdt, -1, "memory");
    if (off >= 0) {
        uint64_t reg[2];

        reg[0] = cpu_to_fdt64(AGILEX5_DDR_BASE);
        reg[1] = cpu_to_fdt64(AGILEX5_DDR_SIZE);
        ret = fdt_setprop(fdt, off, "reg", reg, sizeof(reg));
        if (ret != 0) {
            wolfBoot_printf("FDT: failed to set memory (%d)\n", ret);
            return ret;
        }
        wolfBoot_printf("FDT: Set memory, start=0x%lx, size=0x%lx\n",
            (unsigned long)AGILEX5_DDR_BASE,
            (unsigned long)AGILEX5_DDR_SIZE);
    }

    /* The board DT contains an optional FPGA-backed gpio-leds node.  The
     * normal GSRD path programs the fabric in U-Boot before probing it; the
     * direct wolfBoot handoff does not yet program that optional design.
     * Keep Linux from touching an unconfigured fabric register while the
     * HPS, FCS and storage paths remain available. */
    off = fdt_find_node_offset(fdt, -1, "leds");
    if (off >= 0) {
        ret = fdt_fixup_str(fdt, off, "leds", "status", "disabled");
        if (ret != 0) {
            wolfBoot_printf("FDT: failed to disable FPGA LEDs (%d)\n", ret);
            return ret;
        }
    }

    off = fdt_find_node_offset(fdt, -1, "chosen");
    if (off == -FDT_ERR_NOTFOUND)
        off = fdt_add_subnode(fdt, 0, "chosen");
    if (off < 0)
        return off;
    return fdt_fixup_str(fdt, off, "chosen", "bootargs", LINUX_BOOTARGS);
}
#endif
#endif

void RAMFUNCTION hal_flash_unlock(void) {}
void RAMFUNCTION hal_flash_lock(void) {}

int RAMFUNCTION hal_flash_write(uintptr_t address, const uint8_t *data,
    int len)
{
    (void)address;
    (void)data;
    (void)len;
    return -1;
}

int RAMFUNCTION hal_flash_erase(uintptr_t address, int len)
{
    (void)address;
    (void)len;
    return -1;
}

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "sdhci.h"

#define AGILEX5_SDHCI_HRS05                  0x014U
#define AGILEX5_SDHCI_HRS07                  0x01CU
#define AGILEX5_SDHCI_HRS09                  0x024U
#define AGILEX5_SDHCI_HRS10                  0x028U
#define AGILEX5_SDHCI_HRS16                  0x040U

#define AGILEX5_SDHCI_PHY_DQ_TIMING          0x2000U
#define AGILEX5_SDHCI_PHY_DQS_TIMING         0x2004U
#define AGILEX5_SDHCI_PHY_GATE_LPBK_CTRL     0x2008U
#define AGILEX5_SDHCI_PHY_DLL_MASTER_CTRL    0x200CU
#define AGILEX5_SDHCI_PHY_DLL_SLAVE_CTRL     0x2010U

#define AGILEX5_SDHCI_PHY_INIT_COMPLETE      (1U << 1)
#define AGILEX5_SDHCI_PHY_SW_RESET           (1U << 0)
#define AGILEX5_SDHCI_PHY_INIT_TIMEOUT       1000000U

static void agilex5_sdhci_write_phy(uint32_t address, uint32_t value)
{
    sdhci_reg_write(SDHCI_HRS04, address);
    sdhci_reg_write(AGILEX5_SDHCI_HRS05, value);
}

static int agilex5_sdhci_phy_init(void)
{
    uint32_t reg;
    uint32_t timeout = AGILEX5_SDHCI_PHY_INIT_TIMEOUT;

    reg = sdhci_reg_read(AGILEX5_SDHCI_HRS09);
    reg &= ~AGILEX5_SDHCI_PHY_SW_RESET;
    sdhci_reg_write(AGILEX5_SDHCI_HRS09, reg);

    /* Match the 013B SD default-speed settings used by its U-Boot DTS. */
    agilex5_sdhci_write_phy(AGILEX5_SDHCI_PHY_DQS_TIMING, 0x00780000U);
    agilex5_sdhci_write_phy(AGILEX5_SDHCI_PHY_GATE_LPBK_CTRL, 0x81A40040U);
    agilex5_sdhci_write_phy(AGILEX5_SDHCI_PHY_DLL_MASTER_CTRL, 0x00800004U);
    agilex5_sdhci_write_phy(AGILEX5_SDHCI_PHY_DLL_SLAVE_CTRL, 0x00A000FEU);

    reg |= AGILEX5_SDHCI_PHY_SW_RESET;
    sdhci_reg_write(AGILEX5_SDHCI_HRS09, reg);
    while (((sdhci_reg_read(AGILEX5_SDHCI_HRS09) &
            AGILEX5_SDHCI_PHY_INIT_COMPLETE) == 0U) &&
            (--timeout > 0U)) {}
    if (timeout == 0U)
        return -1;

    agilex5_sdhci_write_phy(AGILEX5_SDHCI_PHY_DQ_TIMING, 0x28000001U);

    reg = sdhci_reg_read(AGILEX5_SDHCI_HRS09);
    reg &= ~((1U << 16) | (1U << 15) | (1U << 3) | (1U << 2));
    reg |= 0x0001800CU;
    sdhci_reg_write(AGILEX5_SDHCI_HRS09, reg);
    sdhci_reg_write(AGILEX5_SDHCI_HRS10, 0x00020000U);
    sdhci_reg_write(AGILEX5_SDHCI_HRS16, 0x00000000U);
    sdhci_reg_write(AGILEX5_SDHCI_HRS07, 0x00080000U);

    return 0;
}

uint32_t sdhci_reg_read(uint32_t offset)
{
    return *(volatile uint32_t *)(AGILEX5_SDHCI_BASE + offset);
}

void sdhci_reg_write(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(AGILEX5_SDHCI_BASE + offset) = value;
}

void sdhci_platform_init(void)
{
    /* SPL initialized the host, clocks, reset and pinmux while loading BL31
     * and BL33. Preserve that live host state and restore the 013B PHY timing
     * required after wolfBoot takes ownership from TF-A. */
    if (agilex5_sdhci_phy_init() != 0)
        wolfBoot_printf("SDHCI: Cadence PHY initialization timed out\n");
}

void sdhci_platform_irq_init(void)
{
    /* The generic driver polls; BL31 retains ownership of GICv3 setup. */
}

void sdhci_platform_set_bus_mode(int is_emmc)
{
    (void)is_emmc;
}

#ifndef SDHCI_SDMA_DISABLED
void sdhci_platform_dma_prepare(void *buf, uint32_t size, int is_write)
{
    uintptr_t addr;
    uintptr_t start = (uintptr_t)buf & ~(CACHE_LINE_SIZE - 1U);
    uintptr_t end = ((uintptr_t)buf + size + CACHE_LINE_SIZE - 1U) &
        ~(CACHE_LINE_SIZE - 1U);

    for (addr = start; addr < end; addr += CACHE_LINE_SIZE) {
        if (is_write != 0)
            __asm__ volatile("dc cvac, %0" : : "r"(addr) : "memory");
        else
            __asm__ volatile("dc ivac, %0" : : "r"(addr) : "memory");
    }
    __asm__ volatile("dsb sy" : : : "memory");
}

void sdhci_platform_dma_complete(void *buf, uint32_t size, int is_write)
{
    uintptr_t addr;
    uintptr_t start = (uintptr_t)buf & ~(CACHE_LINE_SIZE - 1U);
    uintptr_t end = ((uintptr_t)buf + size + CACHE_LINE_SIZE - 1U) &
        ~(CACHE_LINE_SIZE - 1U);

    if (is_write == 0) {
        for (addr = start; addr < end; addr += CACHE_LINE_SIZE)
            __asm__ volatile("dc ivac, %0" : : "r"(addr) : "memory");
        __asm__ volatile("dsb sy" : : : "memory");
    }
}
#endif
#endif

#endif /* TARGET_agilex5 */
