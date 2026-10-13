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
#include "aarch64_arch.h"
#include "image.h"
#include "printf.h"

#ifndef ARCH_AARCH64
#error "wolfBoot Agilex 5 HAL requires ARCH=AARCH64"
#endif

#ifndef LINUX_BOOTARGS
#ifndef LINUX_BOOTARGS_ROOT
#define LINUX_BOOTARGS_ROOT "/dev/mmcblk0p4"
#endif
#define AGILEX5_STR2(x) #x
#define AGILEX5_STR(x) AGILEX5_STR2(x)
#define LINUX_BOOTARGS \
    "earlycon=uart8250,mmio32," AGILEX5_STR(DEBUG_UART_BASE) \
    " console=ttyS0,115200 root=" LINUX_BOOTARGS_ROOT " rootwait"
#endif

#ifdef DEBUG_UART
#include "ns16550.h"

static struct ns16550_dev uart_console;

void uart_init(void)
{
    uart_console.base = (uintptr_t)DEBUG_UART_BASE;
    uart_console.reg_shift = 2;
    uart_console.reg_off = 0;
    uart_console.clk_hz = 0;
    uart_console.io_width = 4;
    uart_console.crlf = 1;
    /* Keep the baud, clock and pinmux settings inherited from SPL. */
}

void uart_write(const char *buf, uint32_t len)
{
    (void)ns16550_write(&uart_console, buf, len);
}
#else
#define uart_init() do {} while (0)
#endif

uint64_t hal_get_timer_us(void)
{
    uint64_t frequency = timer_get_freq();
    uint64_t count = timer_get_count();
    uint64_t whole;
    uint64_t rem;

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
#ifdef WOLFBOOT_FDT_DISABLE_PATHS
static const char* const fdt_disable_paths[] = { WOLFBOOT_FDT_DISABLE_PATHS };
#endif

int hal_dts_fixup(void *dts_addr, uint32_t capacity)
{
    fdt_ctx ctx;
    int off;
    int ret;
#ifdef WOLFBOOT_FDT_DISABLE_PATHS
    unsigned int i;
#endif

    /* Bare-metal and RTOS payloads carry no DTB. */
    if (dts_addr == NULL)
        return 0;

    /* Validate the blob against the window it actually occupies. */
    ret = fdt_open(&ctx, dts_addr, capacity);
    if (ret != 0) {
        wolfBoot_printf("FDT: invalid header (%d)\n", ret);
        return ret;
    }
    ret = fdt_grow(&ctx, WOLFBOOT_FDT_FIXUP_HEADROOM);
    if (ret != 0) {
        wolfBoot_printf("FDT: no headroom for fixups (%d)\n", ret);
        return ret;
    }

    off = fdt_find_devtype(&ctx, -1, "memory");
    if (off >= 0) {
        uint64_t reg[2];

        reg[0] = cpu_to_fdt64(AGILEX5_DDR_BASE);
        reg[1] = cpu_to_fdt64(AGILEX5_DDR_SIZE);
        ret = fdt_setprop(&ctx, off, "reg", reg, sizeof(reg));
        if (ret != 0) {
            wolfBoot_printf("FDT: failed to set memory (%d)\n", ret);
            return ret;
        }
        wolfBoot_printf("FDT: Set memory, start=0x%lx, size=0x%lx\n",
            (unsigned long)AGILEX5_DDR_BASE,
            (unsigned long)AGILEX5_DDR_SIZE);
    }

    /* wolfBoot does not program the fabric, so the board config lists the
     * nodes whose hardware sits behind the unprogrammed FPGA. */
#ifdef WOLFBOOT_FDT_DISABLE_PATHS
    for (i = 0; i < sizeof(fdt_disable_paths) /
                    sizeof(fdt_disable_paths[0]); i++) {
        off = fdt_path_offset(&ctx, fdt_disable_paths[i]);
        if (off < 0)
            continue;
        ret = fdt_fixup_str(&ctx, off, fdt_disable_paths[i], "status",
                            "disabled");
        if (ret != 0) {
            wolfBoot_printf("FDT: failed to disable %s (%d)\n",
                fdt_disable_paths[i], ret);
            return ret;
        }
    }
#endif

    off = fdt_subnode_offset(&ctx, 0, "chosen");
    if (off == -FDT_ERR_NOTFOUND)
        off = fdt_add_subnode(&ctx, 0, "chosen");
    if (off < 0)
        return off;
    return fdt_fixup_str(&ctx, off, "chosen", "bootargs", LINUX_BOOTARGS);
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
    /* SPL initialized the clocks and pinmux; the generic driver then soft
     * resets the host, so restore the 013B PHY timing here. */
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
