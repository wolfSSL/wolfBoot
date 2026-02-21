/* mpfs250.c
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

/* Microchip PolarFire SoC MPFS250T HAL for wolfBoot */
/* Supports:
 *   RISC-V 64-bit architecture
 *   External flash operations
 *   UART communication
 *   System initialization
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "target.h"

#include "mpfs250.h"
#include "riscv.h"
#include "image.h"
#ifndef ARCH_RISCV64
#   error "wolfBoot mpfs250 HAL: wrong architecture selected. Please compile with ARCH=RISCV64."
#endif

#include "printf.h"
#include "loader.h"
#include "hal.h"
#include "gpt.h"
#include "fdt.h"

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "sdhci.h"

/* Forward declaration of SDHCI IRQ handler */
extern void sdhci_irq_handler(void);
#endif

/* Video Kit DDR/Clock configuration is included in mpfs250.h */

/* ============================================================================
 * L2 Cache Controller Configuration
 *
 * The L2 cache controller must be properly configured before using the
 * L2 scratchpad memory. At reset, only 1 cache way is enabled.
 *
 * This function:
 * 1. Enables all cache ways (0-7) and scratchpad ways (8-11)
 * 2. Configures way masks for each master (harts, DMA, AXI ports)
 * 3. Disables L2 shutdown mode
 * ============================================================================ */
#ifdef WOLFBOOT_RISCV_MMODE
static void mpfs_config_l2_cache(void)
{
    uint64_t way_enable_before;
    uint64_t way_enable_after;

    /* Read current way enable state */
    way_enable_before = L2_WAY_ENABLE;

    /* Enable selected cache/scratchpad ways.
     * Value 0x0B (bits 0,1,3) enables ways 0, 1, and 3.
     * This matches the working DDR demo configuration. */
    L2_WAY_ENABLE = 0x0B;

    /* Disable L2 shutdown */
    SYSREG_L2_SHUTDOWN_CR = 0;

    /* Configure way masks - allow all masters to use cache ways 0-7 */
    L2_WAY_MASK_DMA = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT0 = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT1 = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT2 = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT3 = L2_WAY_MASK_CACHE_ONLY;

    /* E51 cache masks */
    L2_WAY_MASK_E51_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_E51_ICACHE = L2_WAY_MASK_CACHE_ONLY;

    /* U54 cache masks (configure even if not using U54s yet) */
    L2_WAY_MASK_U54_1_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_1_ICACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_2_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_2_ICACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_3_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_3_ICACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_4_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_4_ICACHE = L2_WAY_MASK_CACHE_ONLY;

    /* Memory barrier to ensure all writes complete */
    __asm__ volatile("fence iorw, iorw" ::: "memory");

    /* Read back to verify */
    way_enable_after = L2_WAY_ENABLE;

    /* Store for later reporting (can't print yet - UART not initialized) */
    (void)way_enable_before;
    (void)way_enable_after;
}

/* Microsecond delay using busy loop.
 * MTIME counter is not running in bare-metal M-mode (no HSS),
 * so use a calibrated loop at ~40 MHz reset clock. */
static void udelay(uint32_t us)
{
    volatile uint32_t i;
    /* ~10 cycles per iteration at -O2, 40 MHz = 4 iterations per us */
    for (i = 0; i < us * 4; i++)
        ;
}

#endif /* WOLFBOOT_RISCV_MMODE */


/* ============================================================================
 * Multi-Hart Support (M-Mode only)
 * ============================================================================ */
#ifdef WOLFBOOT_RISCV_MMODE
/* ============================================================================
 * Multi-Hart Support
 *
 * These functions handle waking secondary harts (U54 cores) and the
 * communication protocol between E51 (main hart) and U54s.
 * ============================================================================ */

/* Linker symbols for hart stacks and HLS */
extern uint64_t _main_hart_hls;

/* CLINT MSIP register access for sending IPIs */
#define CLINT_MSIP_REG(hart) (*(volatile uint32_t*)(CLINT_BASE + (hart) * 4))

/**
 * mpfs_get_main_hls - Get pointer to main hart's HLS
 * Returns: Pointer to HLS_DATA structure
 */
static HLS_DATA* mpfs_get_main_hls(void)
{
    return (HLS_DATA*)&_main_hart_hls;
}

/**
 * mpfs_signal_main_hart_started - Signal to secondary harts that main hart is ready
 *
 * Called by E51 after basic initialization. Secondary harts are waiting in WFI
 * for this signal before they signal their own readiness.
 */
static void mpfs_signal_main_hart_started(void)
{
    HLS_DATA* hls = mpfs_get_main_hls();

    hls->in_wfi_indicator = HLS_MAIN_HART_STARTED;
    hls->my_hart_id = MPFS_FIRST_HART;

    /* Memory barrier to ensure write is visible to other harts */
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/**
 * mpfs_wake_secondary_harts - Wake all U54 cores via IPI
 *
 * This function implements the hart wake-up protocol:
 * 1. Wait for each hart to signal it's in WFI
 * 2. Send IPI to wake the hart
 * 3. Wait for hart to acknowledge wake-up
 *
 * Returns: Number of harts successfully woken
 */
int mpfs_wake_secondary_harts(void)
{
    int hart_id;
    int woken_count = 0;

    wolfBoot_printf("Waking secondary harts...\n");

    for (hart_id = MPFS_FIRST_U54_HART; hart_id <= MPFS_LAST_U54_HART; hart_id++) {
        /* Note: In this simplified implementation, we just send IPIs.
         * The full implementation would wait for HLS_OTHER_HART_IN_WFI
         * from each hart, but we don't have per-hart HLS pointers yet.
         * For now, we just send the IPI and the hart will wake when ready. */

        wolfBoot_printf("  Sending IPI to hart %d...", hart_id);

        /* Send software interrupt (IPI) to this hart */
        CLINT_MSIP_REG(hart_id) = 0x01;

        /* Memory barrier */
        __asm__ volatile("fence iorw, iorw" ::: "memory");

        /* Small delay for hart to respond (~1ms) */
        udelay(1000);

        woken_count++;
        wolfBoot_printf(" done\n");
    }

    wolfBoot_printf("Woke %d secondary harts\n", woken_count);

    return woken_count;
}

/**
 * secondary_hart_entry - Entry point for secondary harts (U54 cores)
 *
 * Each U54 core uses its own MMUART:
 *   Hart 1 -> MMUART1 (/dev/ttyUSB1), Hart 2 -> MMUART2, etc.
 */
void secondary_hart_entry(unsigned long hartid, HLS_DATA* hls)
{
    /* Message template with placeholder for hart ID at position 5 */
    char msg[] = "Hart X: Woken, waiting for Linux boot...\n";
    (void)hls;

    /* Initialize this hart's dedicated UART */
    uart_init_hart(hartid);

    /* Update hart ID in message (position 5) */
    msg[5] = '0' + (char)hartid;

    /* Write to this hart's UART */
    uart_write_hart(hartid, msg, sizeof(msg) - 1);

    /* Wait for Linux to take over via SBI */
    while (1) {
        __asm__ volatile("wfi");
    }
}
#endif /* WOLFBOOT_RISCV_MMODE */

#if defined(EXT_FLASH) && defined(TEST_EXT_FLASH) && defined(__WOLFBOOT)
static int test_ext_flash(void);
#endif

void hal_init(void)
{
#ifdef WOLFBOOT_RISCV_MMODE
    /* Configure L2 cache controller first (before using L2 scratchpad heavily) */
    mpfs_config_l2_cache();

    /* Signal to secondary harts that main hart is ready */
    mpfs_signal_main_hart_started();
#endif

#ifdef DEBUG_UART
    /* Enable clock and release from soft reset for debug UART */
    SYSREG_SUBBLK_CLOCK_CR |= (MSS_PERIPH_MMUART0 << DEBUG_UART_PORT);
    SYSREG_SOFT_RESET_CR &= ~(MSS_PERIPH_MMUART0 << DEBUG_UART_PORT);
    uart_init();
#endif

#ifdef WOLFBOOT_REPRODUCIBLE_BUILD
    wolfBoot_printf("wolfBoot Version: %s\n", LIBWOLFBOOT_VERSION_STRING);
#else
    wolfBoot_printf("wolfBoot Version: %s (%s %s)\n",
        LIBWOLFBOOT_VERSION_STRING, __DATE__, __TIME__);
#endif

#ifdef WOLFBOOT_RISCV_MMODE
    wolfBoot_printf("Running on E51 (hart 0) in M-mode\n");

#endif

#ifdef EXT_FLASH
    if (qspi_init() != 0) {
        wolfBoot_printf("QSPI: Init failed\n");
    }
#if defined(TEST_EXT_FLASH) && defined(__WOLFBOOT)
    else {
        test_ext_flash();
    }
#endif
#endif /* EXT_FLASH */
}

/* ============================================================================
 * System Controller Mailbox Functions
 *
 * The MPFS system controller provides various system services via a mailbox
 * interface. Commands are sent by writing the opcode to the control register
 * and responses are read from the mailbox RAM.
 * ============================================================================ */

/**
 * mpfs_scb_mailbox_busy - Check if the system controller mailbox is busy
 *
 * Returns: non-zero if busy, 0 if ready
 */
static int mpfs_scb_mailbox_busy(void)
{
    return (SCBCTRL_REG(SERVICES_SR_OFFSET) & SERVICES_SR_BUSY_MASK);
}

/**
 * mpfs_read_serial_number - Read the device serial number via system services
 * @serial: Buffer to store the 16-byte device serial number
 *
 * This function sends a serial number request (opcode 0x00) to the system
 * controller and reads the 16-byte response from the mailbox RAM.
 *
 * Returns: 0 on success, negative error code on failure
 */
int mpfs_read_serial_number(uint8_t *serial)
{
    uint32_t cmd, status;
    int i, timeout;

    if (serial == NULL) {
        return -1;
    }

    /* Check if mailbox is busy */
    if (mpfs_scb_mailbox_busy()) {
        wolfBoot_printf("SCB mailbox busy\n");
        return -2;
    }

    /* Send serial number request command (opcode 0x00)
     * Command format: [31:16] = opcode, [0] = request bit */
    cmd = (SYS_SERV_CMD_SERIAL_NUMBER << SERVICES_CR_COMMAND_SHIFT) |
          SERVICES_CR_REQ_MASK;
    SCBCTRL_REG(SERVICES_CR_OFFSET) = cmd;

    /* Wait for request bit to clear (command accepted) */
    timeout = 10000;
    while ((SCBCTRL_REG(SERVICES_CR_OFFSET) & SERVICES_CR_REQ_MASK) && timeout > 0) {
        timeout--;
    }
    if (timeout == 0) {
        wolfBoot_printf("SCB mailbox request timeout\n");
        return -3;
    }

    /* Wait for busy bit to clear (command completed) */
    timeout = 10000;
    while (mpfs_scb_mailbox_busy() && timeout > 0) {
        timeout--;
    }
    if (timeout == 0) {
        wolfBoot_printf("SCB mailbox busy timeout\n");
        return -4;
    }

    /* Check status (upper 16 bits of status register) */
    status = (SCBCTRL_REG(SERVICES_SR_OFFSET) >> SERVICES_SR_STATUS_SHIFT) & 0xFFFF;
    if (status != 0) {
        wolfBoot_printf("SCB mailbox error: 0x%x\n", status);
        return -5;
    }

    /* Read serial number from mailbox RAM (16 bytes) */
    for (i = 0; i < DEVICE_SERIAL_NUMBER_SIZE; i++) {
        serial[i] = SCBMBOX_BYTE(i);
    }

    return 0;
}

/* Linux kernel command line arguments */
#ifndef LINUX_BOOTARGS
#ifndef LINUX_BOOTARGS_ROOT
#define LINUX_BOOTARGS_ROOT "/dev/mmcblk0p4"
#endif

#define LINUX_BOOTARGS \
    "earlycon root="LINUX_BOOTARGS_ROOT" rootwait uio_pdrv_genirq.of_id=generic-uio"
#endif

/* Microchip OUI (Organizationally Unique Identifier) for MAC address */
#define MICROCHIP_OUI_0 0x00
#define MICROCHIP_OUI_1 0x04
#define MICROCHIP_OUI_2 0xA3

int hal_dts_fixup(void* dts_addr)
{
    int off, ret;
    struct fdt_header *fdt = (struct fdt_header *)dts_addr;
    uint8_t device_serial_number[DEVICE_SERIAL_NUMBER_SIZE];
    uint8_t mac_addr[6];

    /* Verify FDT header */
    ret = fdt_check_header(dts_addr);
    if (ret != 0) {
        wolfBoot_printf("FDT: Invalid header! %d\n", ret);
        return ret;
    }

    wolfBoot_printf("FDT: Version %d, Size %d\n",
        fdt_version(fdt), fdt_totalsize(fdt));

    /* Expand total size to allow adding/modifying properties */
    fdt_set_totalsize(fdt, fdt_totalsize(fdt) + 512);

    /* Find /chosen node */
    off = fdt_find_node_offset(fdt, -1, "chosen");
    if (off < 0) {
        /* Create /chosen node if it doesn't exist */
        off = fdt_add_subnode(fdt, 0, "chosen");
    }

    if (off >= 0) {
        /* Set bootargs property */
        fdt_fixup_str(fdt, off, "chosen", "bootargs", LINUX_BOOTARGS);
    }

    /* Read device serial number from system controller */
    ret = mpfs_read_serial_number(device_serial_number);
    if (ret != 0) {
        wolfBoot_printf("FDT: Failed to read serial number (%d)\n", ret);
        /* Continue without setting MAC addresses */
        return 0;
    }

    wolfBoot_printf("FDT: Device serial: %02x%02x%02x%02x-%02x%02x%02x%02x-"
                    "%02x%02x%02x%02x-%02x%02x%02x%02x\n",
        device_serial_number[15], device_serial_number[14],
        device_serial_number[13], device_serial_number[12],
        device_serial_number[11], device_serial_number[10],
        device_serial_number[9],  device_serial_number[8],
        device_serial_number[7],  device_serial_number[6],
        device_serial_number[5],  device_serial_number[4],
        device_serial_number[3],  device_serial_number[2],
        device_serial_number[1],  device_serial_number[0]);

    /* Build MAC address: Microchip OUI + lower 3 bytes of serial number
     * Format: {0x00, 0x04, 0xA3, serial[2], serial[1], serial[0]} */
    mac_addr[0] = MICROCHIP_OUI_0;
    mac_addr[1] = MICROCHIP_OUI_1;
    mac_addr[2] = MICROCHIP_OUI_2;
    mac_addr[3] = device_serial_number[2];
    mac_addr[4] = device_serial_number[1];
    mac_addr[5] = device_serial_number[0];

    wolfBoot_printf("FDT: MAC0 = %02x:%02x:%02x:%02x:%02x:%02x\n",
        mac_addr[0], mac_addr[1], mac_addr[2],
        mac_addr[3], mac_addr[4], mac_addr[5]);

    /* Set local-mac-address for ethernet@20110000 (mac0) */
    off = fdt_find_node_offset(fdt, -1, "ethernet@20110000");
    if (off >= 0) {
        ret = fdt_setprop(fdt, off, "local-mac-address", mac_addr, 6);
        if (ret != 0) {
            wolfBoot_printf("FDT: Failed to set mac0 address (%d)\n", ret);
        }
    }
    else {
        wolfBoot_printf("FDT: ethernet@20110000 not found\n");
    }

    /* Set local-mac-address for ethernet@20112000 (mac1)
     * Use MAC address + 1 for the second interface */
    mac_addr[5] = device_serial_number[0] + 1;

    wolfBoot_printf("FDT: MAC1 = %02x:%02x:%02x:%02x:%02x:%02x\n",
        mac_addr[0], mac_addr[1], mac_addr[2],
        mac_addr[3], mac_addr[4], mac_addr[5]);

    off = fdt_find_node_offset(fdt, -1, "ethernet@20112000");
    if (off >= 0) {
        ret = fdt_setprop(fdt, off, "local-mac-address", mac_addr, 6);
        if (ret != 0) {
            wolfBoot_printf("FDT: Failed to set mac1 address (%d)\n", ret);
        }
    }
    else {
        wolfBoot_printf("FDT: ethernet@20112000 not found\n");
    }

    return 0;
}
void hal_prepare_boot(void)
{
    /* reset the eMMC/SD card? */


}

void RAMFUNCTION hal_flash_unlock(void)
{

}

void RAMFUNCTION hal_flash_lock(void)
{

}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}


/* Wait for SCB register bits to clear, with timeout */
static int mpfs_scb_wait_clear(uint32_t reg_offset, uint32_t mask,
    uint32_t timeout)
{
    while ((SCBCTRL_REG(reg_offset) & mask) && --timeout)
        ;
    return (timeout == 0) ? -1 : 0;
}

#ifdef EXT_FLASH
/* ==========================================================================
 * QSPI Flash Controller Implementation
 *
 * Both MSS QSPI (0x21000000) and SC QSPI (0x37020100) use CoreQSPI v2
 * with identical register layouts. The controller is selected at build
 * time via MPFS_SC_SPI, which changes QSPI_BASE in the header.
 * ========================================================================== */

/* Microsecond delay using RISC-V time CSR (1 MHz tick rate) */
#ifndef WOLFBOOT_RISCV_MMODE
static void udelay(uint32_t us)
{
    uint64_t start = csr_read(time);
    while ((uint64_t)(csr_read(time) - start) < us)
        ;
}
#endif
/* Forward declarations */
static int qspi_transfer_block(uint8_t read_mode, const uint8_t *cmd,
                                uint32_t cmd_len, uint8_t *data,
                                uint32_t data_len, uint8_t dummy_cycles);
static int qspi_read_id(uint8_t *id_buf);
static int qspi_enter_4byte_mode(void);

/* Send Release from Deep Power-Down / Wake up command */
static void qspi_flash_wakeup(void)
{
    uint8_t cmd = 0xAB;  /* Release from Deep Power-Down */
    qspi_transfer_block(QSPI_MODE_WRITE, &cmd, 1, NULL, 0, 0);
    /* Flash needs tRES1 (3us typ) to wake up */
    udelay(10);
}

int qspi_init(void)
{
    uint8_t id[3];
    uint32_t timeout;

#ifdef MPFS_SC_SPI
    wolfBoot_printf("QSPI: Using SC QSPI Controller (0x%x)\n", QSPI_BASE);

    /* Wait for system controller to finish any pending operations before
     * taking direct control of the SC QSPI peripheral */
    mpfs_scb_wait_clear(SERVICES_SR_OFFSET, SERVICES_SR_BUSY_MASK,
        QSPI_TIMEOUT_TRIES);

#ifdef DEBUG_QSPI
    wolfBoot_printf("QSPI: Initial CTRL=0x%x, STATUS=0x%x, DIRECT=0x%x\n",
        QSPI_CONTROL, QSPI_STATUS, QSPI_DIRECT);
#endif

    /* Disable direct access / XIP mode (SC may have left it enabled) */
    QSPI_DIRECT = 0;
#else
    wolfBoot_printf("QSPI: Using MSS QSPI Controller (0x%x)\n", QSPI_BASE);

    /* Enable QSPI peripheral clock (MSS only) */
    SYSREG_SUBBLK_CLOCK_CR |= SYSREG_SUBBLK_CLOCK_CR_QSPI;
    udelay(1);

    /* Release MSS QSPI from reset (MSS only) */
    SYSREG_SOFT_RESET_CR &= ~SYSREG_SOFT_RESET_CR_QSPI;
    udelay(10);
#endif

    /* Disable controller before configuration */
    QSPI_CONTROL = 0;

    /* Disable all interrupts */
    QSPI_IEN = 0;

    /* Configure QSPI Control Register:
     * - Clock divider for ~5MHz (conservative)
     * - CPOL=1 (clock idle high) for SPI Mode 3
     * - Sample on SCK edge
     * - Enable controller
     */
    QSPI_CONTROL =
        (QSPI_CLK_DIV_30 << QSPI_CTRL_CLKRATE_OFFSET) |
        QSPI_CTRL_CLKIDLE |
        QSPI_CTRL_SAMPLE_SCK |
        QSPI_CTRL_EN;

    /* Wait for controller to be ready */
    timeout = QSPI_TIMEOUT_TRIES;
    while (!(QSPI_STATUS & QSPI_STATUS_READY) && --timeout);
    if (timeout == 0) {
        wolfBoot_printf("QSPI: Controller not ready\n");
        return -1;
    }

    /* Wake up flash from deep power-down (if applicable) */
    qspi_flash_wakeup();

    /* Read and display JEDEC ID for verification */
    if (qspi_read_id(id) == 0) {
        wolfBoot_printf("QSPI: Flash ID = 0x%02x 0x%02x 0x%02x\n",
            id[0], id[1], id[2]);
    }

    /* Enter 4-byte addressing mode for >16MB flash */
    qspi_enter_4byte_mode();

    return 0;
}

/* QSPI Block Transfer Function
 * Modeled after Microchip's MSS_QSPI_polled_transfer_block reference driver.
 *
 * read_mode: 0=write (QSPI_MODE_WRITE), 1=read (QSPI_MODE_READ)
 * cmd: Command buffer (opcode + address bytes)
 * cmd_len: Length of command (opcode + address, NOT including opcode separately)
 * data: Data buffer for read/write
 * data_len: Length of data phase
 * dummy_cycles: Number of idle cycles between command and data phase
 */
static int qspi_transfer_block(uint8_t read_mode, const uint8_t *cmd,
                               uint32_t cmd_len, uint8_t *data,
                               uint32_t data_len, uint8_t dummy_cycles)
{
    uint32_t total_bytes = cmd_len + data_len;
    uint32_t frames;
    uint32_t i;
    uint32_t timeout;
    uint32_t frame_cmd;

    /* Wait for controller to be ready before starting */
    timeout = QSPI_TIMEOUT_TRIES;
    while (!(QSPI_STATUS & QSPI_STATUS_READY) && --timeout);
    if (timeout == 0) {
        wolfBoot_printf("QSPI: Timeout waiting for READY\n");
        return -1;
    }

    /* Drain RX FIFO of any stale data from previous transfers. */
    timeout = QSPI_TIMEOUT_TRIES;
    while ((QSPI_STATUS & QSPI_STATUS_RXAVAIL) && --timeout) {
        (void)QSPI_RX_DATA;
    }
#ifdef DEBUG_QSPI
    if (timeout == 0) {
        /* log warning and continue trying to transfer data */
        wolfBoot_printf("QSPI: Timeout draining RX FIFO\n");
    }
#endif

    /* Configure FRAMES register:
     * - Total bytes: command + data (idle cycles handled by hardware)
     * - Command bytes: TX-only bytes before data phase
     * - Idle cycles: inserted by hardware between command and data
     * - FBYTE: status flags (RXAVAIL/TXAVAIL) refer to individual bytes
     *
     * For write-mode transfers, set CMDBYTES = TOTALBYTES so the entire
     * transfer occurs in the command phase (TX-only). The CoreQSPI data
     * phase shifts TX FIFO output by a fixed offset on writes, causing
     * data rotation in the programmed page. Keeping everything in the
     * command phase avoids this. The flash determines command vs data
     * boundaries from the opcode, not the controller's phase. */
    frame_cmd = read_mode ? cmd_len : total_bytes;
    frames = ((total_bytes & 0xFFFF) << QSPI_FRAMES_TOTALBYTES_OFFSET) |
                ((frame_cmd & 0x1FF) << QSPI_FRAMES_CMDBYTES_OFFSET) |
                ((dummy_cycles & 0xF) << QSPI_FRAMES_IDLE_OFFSET) |
                (1u << QSPI_FRAMES_FBYTE_OFFSET);

    QSPI_FRAMES = frames;

    /* Send command bytes (opcode + address).
     * Use TXAVAIL (bit 3) to check for FIFO space -- CoreQSPI v2 does NOT
     * have a TXFULL status bit (bit 5 is reserved/always 0).
     * A fence (iorw, iorw) after each TX write ensures the store reaches the
     * peripheral before we read STATUS again (RISC-V RVWMO allows posted
     * stores that could cause stale TXAVAIL reads and FIFO overflow). */
    for (i = 0; i < cmd_len; i++) {
        timeout = QSPI_TIMEOUT_TRIES;
        while (!(QSPI_STATUS & QSPI_STATUS_TXAVAIL) && --timeout);
        if (timeout == 0) {
            wolfBoot_printf("QSPI: TX FIFO full timeout\n");
            return -2;
        }
        QSPI_TX_DATA = cmd[i];
        QSPI_IO_FENCE();
    }

    if (read_mode) {
        /* Read mode: poll RXAVAIL for each data byte. */
        for (i = 0; i < data_len; i++) {
            timeout = QSPI_RX_TIMEOUT_TRIES;
            while (!(QSPI_STATUS & QSPI_STATUS_RXAVAIL) && --timeout);
            if (timeout == 0) {
                wolfBoot_printf("QSPI: RX timeout at byte %d, status=0x%x\n",
                    i, QSPI_STATUS);
                return -3;
            }
            data[i] = QSPI_RX_DATA;
        }
        /* Wait for receive complete */
        timeout = QSPI_RX_TIMEOUT_TRIES;
        while (!(QSPI_STATUS & QSPI_STATUS_RXDONE) && --timeout);
        if (timeout == 0) {
            wolfBoot_printf("QSPI: RXDONE timeout\n");
            return -5;
        }
    } else {
        /* Write mode: send data bytes.
         * Must push bytes without delay -- any gap causes FIFO underflow
         * since CoreQSPI continues clocking with empty FIFO.
         * Fence (iorw, iorw) after each write ensures the store reaches the
         * FIFO before we re-read STATUS (prevents FIFO overflow from posted
         * stores). */
        if (data && data_len > 0) {
            for (i = 0; i < data_len; i++) {
                timeout = QSPI_TIMEOUT_TRIES;
                while (!(QSPI_STATUS & QSPI_STATUS_TXAVAIL) && --timeout);
                if (timeout == 0) {
                    wolfBoot_printf("QSPI: TX data timeout\n");
                    return -4;
                }
                QSPI_TX_DATA = data[i];
                QSPI_IO_FENCE();
            }
        }
        /* Wait for transmit complete */
        timeout = QSPI_TIMEOUT_TRIES;
        while (!(QSPI_STATUS & QSPI_STATUS_TXDONE) && --timeout);
        if (timeout == 0) {
            wolfBoot_printf("QSPI: TXDONE timeout, status=0x%x\n",
                QSPI_STATUS);
            return -5;
        }
    }

#ifdef DEBUG_QSPI
    wolfBoot_printf("QSPI: cmd[0]=0x%x, cmd_len=%d, data_len=%d, frames=0x%x\n",
        cmd[0], cmd_len, data_len, frames);
#endif

    return 0;
}

/* Read JEDEC ID from flash */
static int qspi_read_id(uint8_t *id_buf)
{
    uint8_t cmd = QSPI_CMD_READ_ID_OPCODE;
    return qspi_transfer_block(QSPI_MODE_READ, &cmd, 1, id_buf, 3, 0);
}

/* Send Write Enable command */
static int qspi_write_enable(void)
{
    uint8_t cmd = QSPI_CMD_WRITE_ENABLE_OPCODE;
    return qspi_transfer_block(QSPI_MODE_WRITE, &cmd, 1, NULL, 0, 0);
}

/* Wait for flash to be ready (poll status register) */
static int qspi_wait_ready(uint32_t timeout_ms)
{
    uint8_t cmd = QSPI_CMD_READ_STATUS_OPCODE;
    uint8_t status;
    uint32_t count = 0;
    uint32_t max_count = timeout_ms * 1000;  /* Rough timing */
    int ret;

    do {
        ret = qspi_transfer_block(QSPI_MODE_READ, &cmd, 1, &status, 1, 0);
        if (ret != 0) {
            return ret;  /* Propagate transfer error */
        }
        if (!(status & 0x01)) {  /* Bit 0 = WIP (Write In Progress) */
            return 0;  /* Ready */
        }
        count++;
    } while (count < max_count);

    return -1;  /* Timeout */
}

/* Enter 4-byte addressing mode (required for >32MB flash) */
static int qspi_enter_4byte_mode(void)
{
    uint8_t cmd = QSPI_CMD_ENTER_4BYTE_MODE;
    return qspi_transfer_block(QSPI_MODE_WRITE, &cmd, 1, NULL, 0, 0);
}

/* Read from QSPI flash (4-byte addressing) */
static int qspi_flash_read(uint32_t address, uint8_t *data, uint32_t len)
{
    const uint32_t max_chunk = 0xFFFF - 5; /* total_bytes is 16-bit, cmd is 5 */
    uint8_t cmd[5];
    uint32_t remaining = len;
    uint32_t chunk_len;
    int ret;

    while (remaining > 0) {
        chunk_len = (remaining > max_chunk) ? max_chunk : remaining;

        /* Build 4-byte read command */
        cmd[0] = QSPI_CMD_4BYTE_READ_OPCODE;
        cmd[1] = (address >> 24) & 0xFF;
        cmd[2] = (address >> 16) & 0xFF;
        cmd[3] = (address >> 8) & 0xFF;
        cmd[4] = address & 0xFF;

        ret = qspi_transfer_block(QSPI_MODE_READ, cmd, 5, data, chunk_len, 0);
        if (ret != 0) {
            return ret;
        }

        address += chunk_len;
        data += chunk_len;
        remaining -= chunk_len;
    }

    return (int)len;
}

/* Write to QSPI flash - single page (max 256 bytes) */
static int qspi_flash_write_page(uint32_t address, const uint8_t *data, uint32_t len)
{
    uint8_t cmd[5];
    int ret;

    /* Ensure page alignment and length */
    if (len > FLASH_PAGE_SIZE) {
        len = FLASH_PAGE_SIZE;
    }

    /* Enable write */
    ret = qspi_write_enable();
    if (ret != 0) {
        return ret;
    }

    /* Build 4-byte page program command */
    cmd[0] = QSPI_CMD_4BYTE_PAGE_PROG_OPCODE;
    cmd[1] = (address >> 24) & 0xFF;
    cmd[2] = (address >> 16) & 0xFF;
    cmd[3] = (address >> 8) & 0xFF;
    cmd[4] = address & 0xFF;

    /* Send command + data */
    ret = qspi_transfer_block(QSPI_MODE_WRITE, cmd, 5, (uint8_t *)data, len, 0);
    if (ret != 0) {
        return ret;
    }

    /* Wait for write to complete */
    return qspi_wait_ready(1000);  /* 1 second timeout */
}

/* Erase 64KB sector */
static int qspi_flash_sector_erase(uint32_t address)
{
    uint8_t cmd[5];
    int ret;

    /* Enable write */
    ret = qspi_write_enable();
    if (ret != 0) {
        return ret;
    }

    /* Build 4-byte sector erase command */
    cmd[0] = QSPI_CMD_4BYTE_SECTOR_ERASE;
    cmd[1] = (address >> 24) & 0xFF;
    cmd[2] = (address >> 16) & 0xFF;
    cmd[3] = (address >> 8) & 0xFF;
    cmd[4] = address & 0xFF;

    ret = qspi_transfer_block(QSPI_MODE_WRITE, cmd, 5, NULL, 0, 0);
    if (ret != 0) {
        return ret;
    }

    /* Wait for erase to complete (64KB erase can take several seconds) */
    return qspi_wait_ready(10000);  /* 10 second timeout */
}

/* ==========================================================================
 * External Flash API Implementation
 * ========================================================================== */
void ext_flash_lock(void)
{
    /* Optional: Could implement write protection here */
}

void ext_flash_unlock(void)
{
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    uint32_t page_offset;
    uint32_t chunk_len;
    int ret;
    int remaining = len;
    int total = len;

    #ifdef DEBUG_QSPI
    wolfBoot_printf("QSPI: Write 0x%x, len %d\n", (uint32_t)address, len);
    #endif

    /* Write data page by page */
    while (remaining > 0) {
        /* Calculate bytes to write in this page */
        page_offset = address & (FLASH_PAGE_SIZE - 1);
        chunk_len = FLASH_PAGE_SIZE - page_offset;
        if (chunk_len > (uint32_t)remaining) {
            chunk_len = remaining;
        }

        /* Write page */
        ret = qspi_flash_write_page(address, data, chunk_len);
        if (ret != 0) {
            return ret;
        }

        /* Update pointers */
        address += chunk_len;
        data += chunk_len;
        remaining -= chunk_len;
    }

    return total;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    #ifdef DEBUG_QSPI
    wolfBoot_printf("QSPI: Read 0x%x -> 0x%lx, len %d\n",
        (uint32_t)address, (unsigned long)data, len);
    #endif
    return qspi_flash_read((uint32_t)address, data, (uint32_t)len);
}

int ext_flash_erase(uintptr_t address, int len)
{
    uint32_t sector_addr;
    uint32_t end_addr;
    int ret;
    int total = len;

    #ifdef DEBUG_QSPI
    wolfBoot_printf("QSPI: Erase 0x%x, len %d\n", (uint32_t)address, len);
    #endif

    /* Check for invalid length or integer overflow */
    if (len <= 0 || (uint32_t)len > UINT32_MAX - (uint32_t)address) {
        return -1;
    }

    /* Align to sector boundaries */
    sector_addr = address & ~(FLASH_SECTOR_SIZE - 1);
    end_addr = (uint32_t)address + (uint32_t)len;

    /* Erase sectors */
    while (sector_addr < end_addr) {
        #ifdef DEBUG_QSPI
        wolfBoot_printf("QSPI: Erasing sector at 0x%08X\n", sector_addr);
        #endif

        ret = qspi_flash_sector_erase(sector_addr);
        if (ret != 0) {
            wolfBoot_printf("QSPI: Erase failed\n");
            return ret;
        }

        sector_addr += FLASH_SECTOR_SIZE;
    }

    return total;
}

/* Test for external QSPI flash erase/write/read */
#ifdef TEST_EXT_FLASH

#ifndef TEST_EXT_ADDRESS
    #define TEST_EXT_ADDRESS WOLFBOOT_PARTITION_UPDATE_ADDRESS
#endif

static int test_ext_flash(void)
{
    int ret;
    uint32_t i;
    uint8_t pageData[FLASH_PAGE_SIZE];

    wolfBoot_printf("Ext Flash Test at 0x%x\n", TEST_EXT_ADDRESS);

#ifndef TEST_FLASH_READONLY
    /* Erase sector */
    ret = ext_flash_erase(TEST_EXT_ADDRESS, FLASH_SECTOR_SIZE);
    wolfBoot_printf("Sector Erase: Ret %d\n", ret);
    if (ret < 0)
        return ret;

    /* Verify erase (should be all 0xFF) */
    memset(pageData, 0, sizeof(pageData));
    ret = ext_flash_read(TEST_EXT_ADDRESS, pageData, sizeof(pageData));
    if (ret < 0) {
        wolfBoot_printf("Erase verify read failed: Ret %d\n", ret);
        return ret;
    }
    wolfBoot_printf("Erase verify: ");
    for (i = 0; i < 16; i++) {
        wolfBoot_printf("%02x ", pageData[i]);
    }
    wolfBoot_printf("\n");

    /* Write Page */
    for (i = 0; i < sizeof(pageData); i++) {
        pageData[i] = (i & 0xff);
    }
    ret = ext_flash_write(TEST_EXT_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Page Write: Ret %d\n", ret);
    if (ret < 0)
        return ret;
#endif /* !TEST_FLASH_READONLY */

    /* Read page */
    memset(pageData, 0, sizeof(pageData));
    ret = ext_flash_read(TEST_EXT_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Page Read: Ret %d\n", ret);
    if (ret < 0)
        return ret;

    /* Check data */
    for (i = 0; i < sizeof(pageData); i++) {
        if (pageData[i] != (i & 0xff)) {
            wolfBoot_printf("Check Data @ %d failed (0x%02x != 0x%02x)\n",
                i, pageData[i], (i & 0xff));
            wolfBoot_printf("First 16 bytes: ");
            for (i = 0; i < 16; i++) {
                wolfBoot_printf("%02x ", pageData[i]);
            }
            wolfBoot_printf("\n");
            return -1;
        }
    }

    wolfBoot_printf("Ext Flash Test Passed\n");
    return 0;
}
#endif /* TEST_EXT_FLASH */

#else /* !EXT_FLASH */

/* Stubs for when QSPI is disabled */
void ext_flash_lock(void)
{
}

void ext_flash_unlock(void)
{
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int ext_flash_erase(uintptr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

#endif /* EXT_FLASH */

#if defined(MMU) && !defined(WOLFBOOT_NO_PARTITIONS)
void* hal_get_dts_address(void)
{
    return (void*)WOLFBOOT_DTS_BOOT_ADDRESS;
}
#endif

/* ============================================================================
 * PLIC - Platform-Level Interrupt Controller (MPFS250-specific)
 *
 * Generic PLIC functions are in src/boot_riscv.c
 * Platform must provide:
 *   - plic_get_context(): Map current hart to PLIC context
 *   - plic_dispatch_irq(): Dispatch IRQ to appropriate handler
 * ============================================================================ */

/* Get the PLIC context for the current hart
 *
 * PLIC Context IDs for MPFS250:
 *   Hart 0 (E51):  Context 0 = M-mode (E51 has no S-mode)
 *   Hart 1 (U54):  Context 1 = M-mode, Context 2 = S-mode
 *   Hart 2 (U54):  Context 3 = M-mode, Context 4 = S-mode
 *   Hart 3 (U54):  Context 5 = M-mode, Context 6 = S-mode
 *   Hart 4 (U54):  Context 7 = M-mode, Context 8 = S-mode
 */
#ifdef WOLFBOOT_RISCV_MMODE
/* M-mode: Read hart ID directly from CSR */
static uint32_t get_hartid_mmode(void)
{
    uint32_t hartid;
    __asm__ volatile("csrr %0, mhartid" : "=r"(hartid));
    return hartid;
}

uint32_t plic_get_context(void)
{
    uint32_t hart_id = get_hartid_mmode();
    /* E51 (hart 0): M-mode only, context 0
     * U54 (harts 1-4): M-mode context = hart_id * 2 - 1 */
    if (hart_id == 0) {
        return 0;  /* E51 M-mode context */
    }
    return (hart_id * 2) - 1;  /* U54 M-mode context */
}
#else
/* S-mode: Hart ID passed by boot stage, stored in tp register */
extern unsigned long get_boot_hartid(void);
uint32_t plic_get_context(void)
{
    uint32_t hart_id = get_boot_hartid();
    /* Get S-mode context for a given hart (1-4 for U54 cores) */
    return hart_id * 2;
}
#endif

/* Dispatch IRQ to appropriate platform handler */
void plic_dispatch_irq(uint32_t irq)
{
    switch (irq) {
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
        case PLIC_INT_MMC_MAIN:
            sdhci_irq_handler();
            break;
#endif
        default:
            /* Unknown interrupt - ignore */
            break;
    }
}

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
/* ============================================================================
 * SDHCI Platform HAL Functions
 * ============================================================================ */

/* Platform initialization - called from sdhci_init() */
void sdhci_platform_init(void)
{
    /* Release MMC controller from reset */
    SYSREG_SOFT_RESET_CR &= ~MSS_PERIPH_MMC;
}

/* Platform interrupt setup - called from sdhci_init() */
void sdhci_platform_irq_init(void)
{
    /* Set priority for MMC main interrupt */
    plic_set_priority(PLIC_INT_MMC_MAIN, PLIC_PRIORITY_DEFAULT);

    /* Set threshold to 0 (allow all priorities > 0) */
    plic_set_threshold(0);

    /* Enable MMC interrupt for this hart */
    plic_enable_interrupt(PLIC_INT_MMC_MAIN);

#ifdef DEBUG_SDHCI
    wolfBoot_printf("sdhci_platform_irq_init: hart %d, context %d, irq %d enabled\n",
        get_boot_hartid(), plic_get_context(), PLIC_INT_MMC_MAIN);
#endif
}

/* Platform bus mode selection - called from sdhci_init() */
void sdhci_platform_set_bus_mode(int is_emmc)
{
    (void)is_emmc;
    /* Nothing additional needed for MPFS - mode is set in generic driver */
}

/* Register access functions for generic SDHCI driver */
uint32_t sdhci_reg_read(uint32_t offset)
{
    return *((volatile uint32_t*)(EMMC_SD_BASE + offset));
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    *((volatile uint32_t*)(EMMC_SD_BASE + offset)) = val;
}
#endif /* DISK_SDCARD || DISK_EMMC */

/* ============================================================================
 * DEBUG UART Functions
 * ============================================================================ */

#ifdef DEBUG_UART

/* Configure baud divisors with fractional baud rate support.
 *
 * UART baud rate divisor formula: divisor = PCLK / (baudrate * 16)
 *
 * To support fractional divisors (6-bit, 0-63), we scale up the calculation:
 *   divisor_x128 = (PCLK * 8) / baudrate  (128x scaled for rounding precision)
 *   divisor_x64  = divisor_x128 / 2       (64x scaled for 6-bit fractional)
 *   integer_div  = divisor_x64 / 64       (integer portion of divisor)
 *   frac_div     = divisor_x64 % 64       (fractional portion, 0-63)
 *
 * The fractional part is then adjusted using the x128 value for rounding.
 */
static void uart_config_clk(uint32_t baudrate)
{
    const uint64_t pclk = MSS_APB_AHB_CLK;

    /* Scale up for precision: (PCLK * 128) / (baudrate * 16) */
    uint32_t div_x128 = (uint32_t)((8UL * pclk) / baudrate);
    uint32_t div_x64  = div_x128 / 2u;

    /* Extract integer and fractional parts */
    uint32_t div_int  = div_x64 / 64u;
    uint32_t div_frac = div_x64 - (div_int * 64u);

    /* Apply rounding correction from x128 calculation */
    div_frac += (div_x128 - (div_int * 128u)) - (div_frac * 2u);

    if (div_int > (uint32_t)UINT16_MAX)
        return;

    /* Write 16-bit divisor: set DLAB, write high/low bytes, clear DLAB */
    MMUART_LCR(DEBUG_UART_BASE) |= DLAB_MASK;
    MMUART_DMR(DEBUG_UART_BASE) = (uint8_t)(div_int >> 8);
    MMUART_DLR(DEBUG_UART_BASE) = (uint8_t)div_int;
    MMUART_LCR(DEBUG_UART_BASE) &= ~DLAB_MASK;

    /* Enable fractional divisor if integer divisor > 1 */
    if (div_int > 1u) {
        MMUART_MM0(DEBUG_UART_BASE) |= EFBR_MASK;
        MMUART_DFR(DEBUG_UART_BASE) = (uint8_t)div_frac;
    }
    else {
        MMUART_MM0(DEBUG_UART_BASE) &= ~EFBR_MASK;
    }
}

/* New APB clock after MSS PLL lock
 * This should match the configured MSS PLL output 2 (APB/AHB clock).
 * From HSS: LIBERO_SETTING_MSS_APB_AHB_CLK = 150000000 (150 MHz) */

/* Reconfigure UART baud rate divisor for a specific clock */
static void uart_config_clk_with_freq(uint32_t baudrate, uint64_t pclk)
{
    /* Scale up for precision: (PCLK * 128) / (baudrate * 16) */
    uint32_t div_x128 = (uint32_t)((8UL * pclk) / baudrate);
    uint32_t div_x64  = div_x128 / 2u;

    /* Extract integer and fractional parts */
    uint32_t div_int  = div_x64 / 64u;
    uint32_t div_frac = div_x64 - (div_int * 64u);

    /* Apply rounding correction from x128 calculation */
    div_frac += (div_x128 - (div_int * 128u)) - (div_frac * 2u);

    if (div_int > (uint32_t)UINT16_MAX)
        return;

    /* Write 16-bit divisor: set DLAB, write high/low bytes, clear DLAB */
    MMUART_LCR(DEBUG_UART_BASE) |= DLAB_MASK;
    MMUART_DMR(DEBUG_UART_BASE) = (uint8_t)(div_int >> 8);
    MMUART_DLR(DEBUG_UART_BASE) = (uint8_t)div_int;
    MMUART_LCR(DEBUG_UART_BASE) &= ~DLAB_MASK;

    /* Enable fractional divisor if integer divisor > 1 */
    if (div_int > 1u) {
        MMUART_MM0(DEBUG_UART_BASE) |= EFBR_MASK;
        MMUART_DFR(DEBUG_UART_BASE) = (uint8_t)div_frac;
    }
    else {
        MMUART_MM0(DEBUG_UART_BASE) &= ~EFBR_MASK;
    }
}


void uart_init(void)
{
    /* Disable special modes: LIN, IrDA, SmartCard */
    MMUART_MM0(DEBUG_UART_BASE) &= ~ELIN_MASK;
    MMUART_MM1(DEBUG_UART_BASE) &= ~EIRD_MASK;
    MMUART_MM2(DEBUG_UART_BASE) &= ~EERR_MASK;

    /* Disable interrupts */
    MMUART_IER(DEBUG_UART_BASE) = 0u;

    /* Reset and configure FIFOs, enable RXRDYN/TXRDYN pins */
    MMUART_FCR(DEBUG_UART_BASE) = 0u;
    MMUART_FCR(DEBUG_UART_BASE) |= CLEAR_RX_FIFO_MASK | CLEAR_TX_FIFO_MASK;
    MMUART_FCR(DEBUG_UART_BASE) |= RXRDY_TXRDYN_EN_MASK;

    /* Disable loopback (local and remote) */
    MMUART_MCR(DEBUG_UART_BASE) &= ~(LOOP_MASK | RLOOP_MASK);

    /* Set LSB-first for TX/RX */
    MMUART_MM1(DEBUG_UART_BASE) &= ~(E_MSB_TX_MASK | E_MSB_RX_MASK);

    /* Disable AFM, single wire mode */
    MMUART_MM2(DEBUG_UART_BASE) &= ~(EAFM_MASK | ESWM_MASK);

    /* Disable TX time guard, RX timeout, fractional baud */
    MMUART_MM0(DEBUG_UART_BASE) &= ~(ETTG_MASK | ERTO_MASK | EFBR_MASK);

    /* Clear timing registers */
    MMUART_GFR(DEBUG_UART_BASE) = 0u;
    MMUART_TTG(DEBUG_UART_BASE) = 0u;
    MMUART_RTO(DEBUG_UART_BASE) = 0u;

    /* Configure baud rate (115200) */
    uart_config_clk(115200);

    /* Set line config: 8N1 */
    MMUART_LCR(DEBUG_UART_BASE) = MSS_UART_DATA_8_BITS |
                                  MSS_UART_NO_PARITY |
                                  MSS_UART_ONE_STOP_BIT;
}

void uart_write(const char* buf, unsigned int sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') { /* handle CRLF */
            while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE) == 0);
            MMUART_THR(DEBUG_UART_BASE) = '\r';
        }
        while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE) == 0);
        MMUART_THR(DEBUG_UART_BASE) = c;
    }
}
#endif /* DEBUG_UART */

#ifdef WOLFBOOT_RISCV_MMODE
/**
 * uart_init_hart - Initialize UART for a specific hart
 *
 * Each U54 core uses its own MMUART:
 *   Hart 0 (E51)   -> MMUART0 (already initialized by hal_init)
 *   Hart 1 (U54_1) -> MMUART1
 *   Hart 2 (U54_2) -> MMUART2
 *   Hart 3 (U54_3) -> MMUART3
 *   Hart 4 (U54_4) -> MMUART4
 *
 * @hartid: The hart ID (1-4 for U54 cores)
 */
void uart_init_hart(unsigned long hartid)
{
    unsigned long base;

    if (hartid == 0 || hartid > 4) {
        return;  /* Hart 0 uses main UART, invalid harts ignored */
    }

    base = UART_BASE_FOR_HART(hartid);

    /* Enable clock and release from soft reset for this UART
     * The peripheral bit positions are:
     *   MMUART0 = bit 5, MMUART1 = bit 6, MMUART2 = bit 7, etc.
     * MSS_PERIPH_MMUART0 = (1 << 5), so shift by hartid */
    SYSREG_SUBBLK_CLOCK_CR |= (MSS_PERIPH_MMUART0 << hartid);

    /* Memory barrier before modifying reset */
    __asm__ volatile("fence iorw, iorw" ::: "memory");

    /* Release from soft reset */
    SYSREG_SOFT_RESET_CR &= ~(MSS_PERIPH_MMUART0 << hartid);

    /* Memory barrier */
    __asm__ volatile("fence iorw, iorw" ::: "memory");

    /* Longer delay for clock to stabilize (critical for reliable UART) */
    udelay(100);

    /* Disable special modes: LIN, IrDA, SmartCard */
    MMUART_MM0(base) &= ~ELIN_MASK;
    MMUART_MM1(base) &= ~EIRD_MASK;
    MMUART_MM2(base) &= ~EERR_MASK;

    /* Disable interrupts */
    MMUART_IER(base) = 0u;

    /* Reset and configure FIFOs */
    MMUART_FCR(base) = 0u;
    MMUART_FCR(base) |= CLEAR_RX_FIFO_MASK | CLEAR_TX_FIFO_MASK;
    MMUART_FCR(base) |= RXRDY_TXRDYN_EN_MASK;

    /* Disable loopback */
    MMUART_MCR(base) &= ~(LOOP_MASK | RLOOP_MASK);

    /* Set LSB-first */
    MMUART_MM1(base) &= ~(E_MSB_TX_MASK | E_MSB_RX_MASK);

    /* Disable AFM, single wire mode */
    MMUART_MM2(base) &= ~(EAFM_MASK | ESWM_MASK);

    /* Disable TX time guard, RX timeout, fractional baud */
    MMUART_MM0(base) &= ~(ETTG_MASK | ERTO_MASK | EFBR_MASK);

    /* Clear timing registers */
    MMUART_GFR(base) = 0u;
    MMUART_TTG(base) = 0u;
    MMUART_RTO(base) = 0u;

    /* Configure baud rate (115200)
     * Using EXACT same calculation as uart_config_clk for consistency */
    {
        const uint64_t pclk = MSS_APB_AHB_CLK;
        const uint32_t baudrate = 115200;

        /* Scale up for precision: (PCLK * 128) / (baudrate * 16) */
        uint32_t div_x128 = (uint32_t)((8UL * pclk) / baudrate);
        uint32_t div_x64  = div_x128 / 2u;

        /* Extract integer and fractional parts */
        uint32_t div_int  = div_x64 / 64u;
        uint32_t div_frac = div_x64 - (div_int * 64u);

        /* Apply rounding correction from x128 calculation (same as uart_config_clk) */
        div_frac += (div_x128 - (div_int * 128u)) - (div_frac * 2u);

        /* Enable DLAB to access divisor registers */
        MMUART_LCR(base) |= DLAB_MASK;

        /* Write DMR before DLR (same order as uart_config_clk) */
        MMUART_DMR(base) = (uint8_t)(div_int >> 8);
        MMUART_DLR(base) = (uint8_t)div_int;

        /* Clear DLAB */
        MMUART_LCR(base) &= ~DLAB_MASK;

        /* Configure fractional baud rate if needed */
        if (div_frac > 0u) {
            MMUART_MM0(base) |= EFBR_MASK;
            MMUART_DFR(base) = (uint8_t)div_frac;
        } else {
            MMUART_MM0(base) &= ~EFBR_MASK;
        }
    }

    /* Set line config: 8N1 */
    MMUART_LCR(base) = MSS_UART_DATA_8_BITS |
                       MSS_UART_NO_PARITY |
                       MSS_UART_ONE_STOP_BIT;

    /* Small delay after configuration */
    udelay(10);
}

/**
 * uart_write_hart - Write string to a specific hart's UART
 *
 * @hartid: The hart ID (0-4)
 * @buf: Buffer to write
 * @sz: Number of bytes to write
 */
void uart_write_hart(unsigned long hartid, const char* buf, unsigned int sz)
{
    unsigned long base;
    uint32_t pos = 0;

    if (hartid > 4) {
        return;
    }

    base = UART_BASE_FOR_HART(hartid);

    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') {
            while ((MMUART_LSR(base) & MSS_UART_THRE) == 0);
            MMUART_THR(base) = '\r';
        }
        while ((MMUART_LSR(base) & MSS_UART_THRE) == 0);
        MMUART_THR(base) = c;
    }
}

/**
 * uart_printf_hart - Simple printf to a specific hart's UART
 * Only supports %d, %x, %s, %lu formats for minimal footprint
 */
static void uart_printf_hart(unsigned long hartid, const char* fmt, ...)
{
    char buf[128];
    int len = 0;
    const char* p = fmt;

    /* Very simple printf implementation */
    while (*p && len < (int)sizeof(buf) - 1) {
        if (*p == '%') {
            p++;
            if (*p == 'l' && *(p+1) == 'u') {
                /* %lu - unsigned long */
                p += 2;
                /* Skip for now - just print placeholder */
                buf[len++] = '[';
                buf[len++] = 'N';
                buf[len++] = ']';
            } else if (*p == 'd') {
                p++;
                buf[len++] = '[';
                buf[len++] = 'N';
                buf[len++] = ']';
            } else if (*p == 's') {
                p++;
                buf[len++] = '[';
                buf[len++] = 'S';
                buf[len++] = ']';
            } else {
                buf[len++] = '%';
                buf[len++] = *p++;
            }
        } else {
            buf[len++] = *p++;
        }
    }
    buf[len] = '\0';

    uart_write_hart(hartid, buf, len);
}
#endif /* WOLFBOOT_RISCV_MMODE */
