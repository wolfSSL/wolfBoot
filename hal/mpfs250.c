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

/* UART base addresses for per-hart access (LO addresses, M-mode compatible) */
const unsigned long MSS_UART_BASE_ADDR[5] = {
    MSS_UART0_LO_BASE,  /* Hart 0 (E51) */
    MSS_UART1_LO_BASE,  /* Hart 1 (U54_1) */
    MSS_UART2_LO_BASE,  /* Hart 2 (U54_2) */
    MSS_UART3_LO_BASE,  /* Hart 3 (U54_3) */
    MSS_UART4_LO_BASE,  /* Hart 4 (U54_4) */
};

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "sdhci.h"

/* Forward declaration of SDHCI IRQ handler */
extern void sdhci_irq_handler(void);
#endif

/* Video Kit DDR/Clock configuration is included in mpfs250.h */

/* Configure L2 cache: enable ways 0,1,3 (0x0B) and set way masks for all masters */
#ifdef WOLFBOOT_RISCV_MMODE
static void mpfs_config_l2_cache(void)
{
    L2_WAY_ENABLE = 0x0B;  /* ways 0, 1, 3 — matches DDR demo config */
    SYSREG_L2_SHUTDOWN_CR = 0;
    L2_WAY_MASK_DMA        = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT0 = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT1 = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT2 = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_AXI4_PORT3 = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_E51_DCACHE  = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_E51_ICACHE  = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_1_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_1_ICACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_2_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_2_ICACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_3_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_3_ICACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_4_DCACHE = L2_WAY_MASK_CACHE_ONLY;
    L2_WAY_MASK_U54_4_ICACHE = L2_WAY_MASK_CACHE_ONLY;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/* Busy-loop delay — MTIME not running in M-mode without HSS.
 * E51 at 80 MHz reset: ~8 iters/us accounting for loop overhead. */
static __attribute__((noinline)) void udelay(uint32_t us)
{
    volatile uint32_t i;
    for (i = 0; i < us * 8; i++)
        ;
}

#endif /* WOLFBOOT_RISCV_MMODE */


/* Multi-Hart Support */
#ifdef WOLFBOOT_RISCV_MMODE

extern uint8_t _main_hart_hls; /* linker-provided address symbol; typed as uint8_t to avoid size confusion */

/* CLINT MSIP register for IPI delivery */
#define CLINT_MSIP_REG(hart) (*(volatile uint32_t*)(CLINT_BASE + (hart) * 4))

/* Signal secondary harts that E51 (main hart) is ready. */
static void mpfs_signal_main_hart_started(void)
{
    HLS_DATA* hls = (HLS_DATA*)&_main_hart_hls;
    hls->in_wfi_indicator = HLS_MAIN_HART_STARTED;
    hls->my_hart_id = MPFS_FIRST_HART;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/* Wake secondary U54 harts by sending software IPIs via CLINT MSIP. */
int mpfs_wake_secondary_harts(void)
{
    int hart_id;
    int woken_count = 0;

    wolfBoot_printf("Waking secondary harts...\n");
    for (hart_id = MPFS_FIRST_U54_HART; hart_id <= MPFS_LAST_U54_HART; hart_id++) {
        CLINT_MSIP_REG(hart_id) = 0x01;
        __asm__ volatile("fence iorw, iorw" ::: "memory");
        udelay(1000);
        woken_count++;
    }
    wolfBoot_printf("Woke %d secondary harts\n", woken_count);
    return woken_count;
}

/* Secondary hart (U54) entry: init per-hart UART and spin in WFI for Linux/SBI. */
void secondary_hart_entry(unsigned long hartid, HLS_DATA* hls)
{
    char msg[] = "Hart X: Woken, waiting for Linux boot...\n";
    (void)hls;
    uart_init_hart(hartid);
    msg[5] = '0' + (char)hartid;
    uart_write_hart(hartid, msg, sizeof(msg) - 1);
    while (1)
        __asm__ volatile("wfi");
}
#endif /* WOLFBOOT_RISCV_MMODE */

#if defined(EXT_FLASH) && defined(TEST_EXT_FLASH) && defined(__WOLFBOOT)
static int test_ext_flash(void);
#endif
#if defined(EXT_FLASH) && defined(UART_QSPI_PROGRAM) && defined(__WOLFBOOT)
static void qspi_uart_program(void);
#endif

void hal_init(void)
{
#ifdef WOLFBOOT_RISCV_MMODE
    mpfs_config_l2_cache();
    mpfs_signal_main_hart_started();
#endif

#ifdef DEBUG_UART
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
    } else {
#if defined(TEST_EXT_FLASH) && defined(__WOLFBOOT)
        test_ext_flash();
#endif
#if defined(UART_QSPI_PROGRAM) && defined(__WOLFBOOT)
        qspi_uart_program();
#endif
    }
#endif /* EXT_FLASH */
}

/* System Controller Mailbox */

static int mpfs_scb_mailbox_busy(void)
{
    return (SCBCTRL_REG(SERVICES_SR_OFFSET) & SERVICES_SR_BUSY_MASK);
}

/* Read 16-byte device serial number via SCB system service (opcode 0x00). */
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

/* ============================================================================
 * UART QSPI Programmer
 *
 * Allows programming the QSPI flash over the debug UART without a JTAG/Libero
 * tool. Enabled at build time with UART_QSPI_PROGRAM=1 in the .config.
 *
 * Protocol (after wolfBoot prints the "QSPI-PROG" prompt):
 *   1. Host sends 'P' within the timeout window to enter programming mode
 *   2. wolfBoot sends "READY\r\n"
 *   3. Host sends [4-byte LE QSPI address][4-byte LE data length]
 *   4. wolfBoot erases required sectors, sends "ERASED\r\n"
 *   5. For each 256-byte chunk:
 *        wolfBoot sends ACK byte (0x06) -> host sends chunk -> wolfBoot writes
 *   6. wolfBoot sends "DONE\r\n" and continues normal boot
 *
 * Host side: tools/scripts/mpfs_qspi_prog.py
 * ============================================================================ */
#if defined(UART_QSPI_PROGRAM) && defined(__WOLFBOOT)

#define QSPI_PROG_CHUNK        256
#define QSPI_PROG_ACK          0x06
#define QSPI_RX_TIMEOUT_MS     5000U  /* 5 s per byte — aborts if host disappears */

/* Returns 0-255 on success, -1 on timeout (so the boot path is never deadlocked). */
static int uart_qspi_rx(void)
{
    uint32_t t;
    for (t = 0; t < QSPI_RX_TIMEOUT_MS; t++) {
        if (MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_DR)
            return (int)(uint8_t)MMUART_RBR(DEBUG_UART_BASE);
        udelay(1000);
    }
    return -1; /* timeout */
}

static void uart_qspi_tx(uint8_t c)
{
    while (!(MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE))
        ;
    MMUART_THR(DEBUG_UART_BASE) = c;
}

static void uart_qspi_puts(const char *s)
{
    while (*s)
        uart_qspi_tx((uint8_t)*s++);
}

static void qspi_uart_program(void)
{
    uint8_t ch = 0;
    uint32_t addr, size, n_sectors, written, t;
    uint32_t i, s;
    uint8_t chunk[QSPI_PROG_CHUNK];

    wolfBoot_printf("QSPI-PROG: Press 'P' within 3s to program flash\r\n");

    /* Drain any stale RX bytes before opening the window */
    while (MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_DR)
        (void)MMUART_RBR(DEBUG_UART_BASE);

    /* Wait up to 3s: 3000 iterations of 1ms each */
    for (t = 0; t < 3000U; t++) {
        udelay(1000);
        if (MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_DR) {
            ch = MMUART_RBR(DEBUG_UART_BASE);
            break;
        }
    }

    if (ch != 'P' && ch != 'p') {
        wolfBoot_printf("QSPI-PROG: No trigger (got 0x%02x LSR=0x%02x), booting\r\n",
                        (unsigned)ch,
                        (unsigned)MMUART_LSR(DEBUG_UART_BASE));
        return;
    }

    wolfBoot_printf("QSPI-PROG: Entering programmer mode\r\n");
    uart_qspi_puts("READY\r\n");

    /* Receive destination address then data length (4 bytes LE each) */
    addr = 0;
    for (i = 0; i < 4; i++) {
        int b = uart_qspi_rx();
        if (b < 0) {
            wolfBoot_printf("QSPI-PROG: RX timeout receiving addr\r\n");
            return;
        }
        addr |= ((uint32_t)(uint8_t)b << (i * 8));
    }
    size = 0;
    for (i = 0; i < 4; i++) {
        int b = uart_qspi_rx();
        if (b < 0) {
            wolfBoot_printf("QSPI-PROG: RX timeout receiving size\r\n");
            return;
        }
        size |= ((uint32_t)(uint8_t)b << (i * 8));
    }

    wolfBoot_printf("QSPI-PROG: addr=0x%x size=%u bytes\r\n", addr, size);

    if (size == 0 || size > 0x200000U) {
        wolfBoot_printf("QSPI-PROG: Invalid size, aborting\r\n");
        return;
    }

    /* Reject writes to unaligned or out-of-partition addresses before any erase */
    if ((addr & (FLASH_SECTOR_SIZE - 1U)) != 0U) {
        wolfBoot_printf("QSPI-PROG: addr 0x%x not sector-aligned, aborting\r\n", addr);
        return;
    }
    if (!((addr >= WOLFBOOT_PARTITION_BOOT_ADDRESS &&
           addr + size <= WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE) ||
          (addr >= WOLFBOOT_PARTITION_UPDATE_ADDRESS &&
           addr + size <= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE))) {
        wolfBoot_printf("QSPI-PROG: addr 0x%x+%u outside allowed partitions, aborting\r\n",
                        addr, size);
        return;
    }

    /* Erase all required sectors (FLASH_SECTOR_SIZE = 64 KB) */
    n_sectors = (size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    wolfBoot_printf("QSPI-PROG: Erasing %u sector(s) at 0x%x...\r\n",
                    n_sectors, addr);
    ext_flash_unlock();
    for (s = 0; s < n_sectors; s++) {
        int ret = ext_flash_erase(addr + s * FLASH_SECTOR_SIZE,
                                  FLASH_SECTOR_SIZE);
        if (ret < 0) {
            wolfBoot_printf("QSPI-PROG: Erase failed at 0x%x (ret %d)\r\n",
                            addr + s * FLASH_SECTOR_SIZE, ret);
            ext_flash_lock();
            return;
        }
    }

    /* "ERASED\r\n" must be the last bytes before the first ACK (0x06).
     * Do not insert any wolfBoot_printf between here and the transfer loop. */
    uart_qspi_puts("ERASED\r\n");

    /* Chunk transfer: wolfBoot requests each 256-byte block with ACK 0x06 */
    written = 0;
    while (written < size) {
        int ret;
        uint32_t chunk_len = size - written;
        if (chunk_len > QSPI_PROG_CHUNK)
            chunk_len = QSPI_PROG_CHUNK;

        uart_qspi_tx(QSPI_PROG_ACK);          /* request next chunk */

        for (i = 0; i < chunk_len; i++) {
            int b = uart_qspi_rx();
            if (b < 0) {
                wolfBoot_printf("QSPI-PROG: RX timeout at 0x%x+%u\r\n",
                                addr + written, i);
                ext_flash_lock();
                return;
            }
            chunk[i] = (uint8_t)b;
        }

        ret = ext_flash_write(addr + written, chunk, (int)chunk_len);
        if (ret < 0) {
            wolfBoot_printf("QSPI-PROG: Write failed at 0x%x (ret %d)\r\n",
                            addr + written, ret);
            ext_flash_lock();
            return;
        }
        written += chunk_len;
    }
    ext_flash_lock();

    wolfBoot_printf("QSPI-PROG: Wrote %u bytes to 0x%x\r\n", written, addr);
    uart_qspi_puts("DONE\r\n");
    wolfBoot_printf("QSPI-PROG: Done, continuing boot\r\n");
}

#endif /* UART_QSPI_PROGRAM */

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
#if defined(EXT_FLASH) && defined(NO_XIP)
    /* Flash is not memory-mapped when using NO_XIP with external flash
     * (e.g. SC SPI). DTS must be loaded via ext_flash_read, not direct
     * dereference. Return NULL so the caller skips the direct-access path. */
    return NULL;
#else
    return (void*)WOLFBOOT_DTS_BOOT_ADDRESS;
#endif
}
#endif

/* PLIC: E51(hart 0)->ctx 0 (M-mode only); U54(1-4)->ctx hart*2-1 (M), hart*2 (S) */
#ifdef WOLFBOOT_RISCV_MMODE
uint32_t plic_get_context(void)
{
    uint32_t hart_id;
    __asm__ volatile("csrr %0, mhartid" : "=r"(hart_id));
    return (hart_id == 0) ? 0 : (hart_id * 2) - 1;
}
#else
extern unsigned long get_boot_hartid(void);
uint32_t plic_get_context(void)
{
    return (uint32_t)get_boot_hartid() * 2;
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
/* SDHCI Platform HAL */

void sdhci_platform_init(void)
{
    SYSREG_SOFT_RESET_CR &= ~MSS_PERIPH_MMC;
}

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

void sdhci_platform_set_bus_mode(int is_emmc)
{
    (void)is_emmc;
}

uint32_t sdhci_reg_read(uint32_t offset)
{
    return *((volatile uint32_t*)(EMMC_SD_BASE + offset));
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    *((volatile uint32_t*)(EMMC_SD_BASE + offset)) = val;
}
#endif /* DISK_SDCARD || DISK_EMMC */

/* DEBUG UART */
#ifdef DEBUG_UART

/* Baud divisor: integer = PCLK/(baudrate*16), fractional (0-63) via 128x scaling. */
static void uart_config_baud(unsigned long base, uint32_t baudrate)
{
    const uint64_t pclk = MSS_APB_AHB_CLK;
    uint32_t div_x128 = (uint32_t)((8UL * pclk) / baudrate);
    uint32_t div_x64  = div_x128 / 2u;
    uint32_t div_int  = div_x64 / 64u;
    uint32_t div_frac = div_x64 - (div_int * 64u);
    div_frac += (div_x128 - (div_int * 128u)) - (div_frac * 2u);
    if (div_int > (uint32_t)UINT16_MAX)
        return;
    MMUART_LCR(base) |= DLAB_MASK;
    MMUART_DMR(base) = (uint8_t)(div_int >> 8);
    MMUART_DLR(base) = (uint8_t)div_int;
    MMUART_LCR(base) &= ~DLAB_MASK;
    if (div_int > 1u) {
        MMUART_MM0(base) |= EFBR_MASK;
        MMUART_DFR(base) = (uint8_t)div_frac;
    } else {
        MMUART_MM0(base) &= ~EFBR_MASK;
    }
}

static void uart_init_base(unsigned long base)
{
    MMUART_MM0(base) &= ~ELIN_MASK;
    MMUART_MM1(base) &= ~EIRD_MASK;
    MMUART_MM2(base) &= ~EERR_MASK;
    MMUART_IER(base)  = 0u;
    MMUART_FCR(base)  = CLEAR_RX_FIFO_MASK | CLEAR_TX_FIFO_MASK | RXRDY_TXRDYN_EN_MASK;
    MMUART_MCR(base) &= ~(LOOP_MASK | RLOOP_MASK);
    MMUART_MM1(base) &= ~(E_MSB_TX_MASK | E_MSB_RX_MASK);
    MMUART_MM2(base) &= ~(EAFM_MASK | ESWM_MASK);
    MMUART_MM0(base) &= ~(ETTG_MASK | ERTO_MASK | EFBR_MASK);
    MMUART_GFR(base)  = 0u;
    MMUART_TTG(base)  = 0u;
    MMUART_RTO(base)  = 0u;
    uart_config_baud(base, 115200);
    MMUART_LCR(base)  = MSS_UART_DATA_8_BITS | MSS_UART_NO_PARITY | MSS_UART_ONE_STOP_BIT;
}

void uart_init(void)
{
    uart_init_base(DEBUG_UART_BASE);
}

void uart_write(const char* buf, unsigned int sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') {
            while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE) == 0);
            MMUART_THR(DEBUG_UART_BASE) = '\r';
        }
        while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE) == 0);
        MMUART_THR(DEBUG_UART_BASE) = c;
    }
}
#endif /* DEBUG_UART */

#ifdef WOLFBOOT_RISCV_MMODE
/* Initialize UART for a secondary hart (1-4). Hart 0 uses uart_init(). */
void uart_init_hart(unsigned long hartid)
{
    unsigned long base;
    if (hartid == 0 || hartid > 4)
        return;
    base = UART_BASE_FOR_HART(hartid);
    /* MSS_PERIPH_MMUART0 = bit 5; shift by hartid selects MMUART1-4 */
    SYSREG_SUBBLK_CLOCK_CR |= (MSS_PERIPH_MMUART0 << hartid);
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    SYSREG_SOFT_RESET_CR &= ~(MSS_PERIPH_MMUART0 << hartid);
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    udelay(100);
    uart_init_base(base);
    udelay(10);
}

/* Write to a specific hart's UART (hart 0-4). */
void uart_write_hart(unsigned long hartid, const char* buf, unsigned int sz)
{
    unsigned long base;
    uint32_t pos = 0;
    if (hartid > 4)
        return;
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
#endif /* WOLFBOOT_RISCV_MMODE */
