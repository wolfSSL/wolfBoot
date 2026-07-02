/* rtl8735b.c
 *
 * HAL for the RealTek RTL8735B SoC (Cortex-M33), as used on the AmebaPro2 EVB
 * and compatible boards.
 *
 * Copyright (C) 2026 wolfSSL Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 *
 *
 * Model A (non-TrustZone): the RealTek bootloader stages wolfBoot into SRAM via
 * the RealTek RAM_FUNCTION_START_TABLE below. wolfBoot then reads the
 * BOOT/UPDATE/SWAP partitions from external SPI NOR (ext_flash_*), verifies the
 * application, copies it into DDR (src/update_ram.c RAMBOOT path) and jumps.
 *
 * Flash/UART/cache backend, selected at build time with HAL_BACKEND (arch.mk):
 *   HAL_BACKEND=sdk   (default) - use the RealTek SDK drivers (flash_api /
 *                     hal_uart / hal_pinmux / hal_cache). Compiled with the SDK
 *                     include dirs via -iquote, a stub cmsis_os.h (so the chain
 *                     does not pull FreeRTOS), the bootloader CONFIG_* defines,
 *                     and -mcmse. See hal/rtl8735b/README.
 *   HAL_BACKEND=bare  - a smaller backend with no SDK dependency (direct
 *                     register / ROM access). Scaffold only: links standalone
 *                     but the ext_flash_* entry points return -1. The flash
 *                     path is the hard part -- the ROM exposes the flash leaf
 *                     functions (hal_flash_stubs) and SPIC primitives
 *                     (hal_spic_stubs) via romsym, but the bootloader hands off
 *                     no SPIC adaptor (see hal_init), so a bare backend must
 *                     reimplement spic_init() (controller training/calibration)
 *                     on hal_spic_stubs before it can read flash. Future work.
 *
 * NOTE: wolfBoot's own "hal.h" is intentionally NOT included here. The RealTek
 * SDK also ships a "hal.h" (pulled by its objects.h), and in the SDK backend the
 * SDK include dirs are passed via -iquote so that header resolves to the SDK
 * one. Including wolfBoot's hal.h in the same translation unit would clash, so
 * this file does not include it. The HAL entry points it implements (hal_init,
 * ext_flash_*, hal_prepare_boot, ...) are prototyped by wolfBoot's hal.h in the
 * translation units that call them; here they are simply defined below.
 */

#include <stdint.h>
#include <string.h>

#include "target.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"             /* wolfBoot_printf, uart_init, uart_write */

#ifdef HAL_BACKEND_SDK
    /* The SDK's basic_types.h defines likely()/unlikely() too; drop wolfBoot's
     * so the SDK definitions apply without a redefinition warning. */
    #undef likely
    #undef unlikely

    /* RealTek SDK drivers: SPI NOR (flash_api.h), I/D cache maintenance
     * (hal_cache.h), and the UART/pinmux register-level leaf functions used to
     * bring up the DEBUG_UART console (hal_uart.h / hal_pinmux.h). */
    #include "flash_api.h"
    #include "hal_cache.h"
    #include "hal_uart.h"
    #include "hal_pinmux.h"

    /* Set by the SDK flash driver; seeded from the bootloader-provided adaptor. */
    extern hal_spic_adaptor_t *pglob_spic_adaptor;
    /* flash_init() is an SDK symbol not declared in flash_api.h. */
    extern void flash_init(flash_t *obj);

    static flash_t hal_flash_obj;
#endif /* HAL_BACKEND_SDK */

/* ===========================================================================
 * RealTek RAM start-table: launched by the RealTek bootloader (backend common).
 *
 * Field order/types mirror the SDK rtl8735b_ramstart.h so the bootloader reads
 * each field at the right offset. RamStartFun points at the trampoline below,
 * which sets MSP and branches to wolfBoot's isr_reset. Embedding the table here
 * makes wolfboot.elf directly launchable (no separate shim).
 * ===========================================================================
 */

extern void wolfboot_ram_entry(void);

/* Scratch SRAM the bootloader writes through the RAM start-table pointers
 * (pbl_peri_buf, phal_spic_adaptor, ...). It lives in .ram.noinit, which the
 * linker keeps OUTSIDE the _start_bss.._end_bss range that isr_reset
 * (src/boot_arm.c) zeroes -- otherwise anything the bootloader stored here
 * before jumping to wolfBoot would be wiped before hal_init() runs. This is
 * latent today (hal_init does a fresh flash_init and ignores the passed
 * adaptor) but is required once the SPIC adaptor reuse is enabled. */
static uint8_t hal_bl_scratch[2048]
    __attribute__((section(".ram.noinit"), aligned(32)));

/* Exactly the 10 bytes "AmebaPro2\xff" (the trailing 0xff matters; a NUL pad
 * fails as "Invalid FW Image Signature"). */
const unsigned char hal_ram_img_sig[10]
    __attribute__((section(".ram.img.signature"), aligned(4))) = {
    'A', 'm', 'e', 'b', 'a', 'P', 'r', 'o', '2', 0xff
};

typedef struct {
    void     *Signature;
    void    (*RamStartFun)(void);
    void    (*RamWakeupFun)(void);
    void    (*RamPatchFun0)(void);
    void    (*RamPatchFun1)(void);
    void     *sys_cp_fw_info;
    void     *pbl_peri_buf;
    void     *pxip_sce_restore;
    uint32_t  entry_start;
    uint32_t  entry_end;
    uint8_t  *hash_data;
    uint32_t  ddr_hash_start1;
    uint32_t  ddr_hash_end1;
    uint32_t  ddr_hash_start2;
    uint32_t  ddr_hash_end2;
    uint8_t  *ddr_hash_data;
    uint32_t  boot_cfg_w;
    uint32_t  msp_start;
    uint32_t  msp_limit;
    uint32_t  start_tbl_size;
    void     *phal_spic_adaptor;
    uint32_t  flash_user_data_offset;
    uint32_t  flash_user_data_len;
    void     *pbl_shared_buf;
    uint32_t  init_flags;
    uint32_t  boot_status;
    uint8_t   reserved1;
    uint8_t   sys_tmr_id;
    uint16_t  pad;
    void     *pfw_image_info;
    void     *pbl_ld_voe_info;
    void     *pSnand_layout_info;
    uint32_t  reserved2[2];
} ram_start_table_t;

const ram_start_table_t ram_start_table
    __attribute__((section(".ram.func.table"), used)) = {
    .Signature          = (void *)hal_ram_img_sig,
    .RamStartFun        = wolfboot_ram_entry,
    .msp_start          = 0x20120000u,   /* valid early SRAM stack */
    .msp_limit          = 0x2011f000u,
    .start_tbl_size     = sizeof(ram_start_table_t),
    .pbl_peri_buf       = &hal_bl_scratch[0],
    .pbl_shared_buf     = &hal_bl_scratch[256],
    .pfw_image_info     = &hal_bl_scratch[512],
    .pbl_ld_voe_info    = &hal_bl_scratch[768],
    .pSnand_layout_info = &hal_bl_scratch[1024],
    .sys_cp_fw_info     = &hal_bl_scratch[1280],
    .phal_spic_adaptor  = &hal_bl_scratch[1536],
};

/* Trampoline the bootloader jumps to: set MSP to the top of wolfBoot's SRAM
 * region and branch into wolfBoot's normal reset path (src/boot_arm.c). This
 * removes any dependency on the loader honoring the ARM vector table's word0. */
__attribute__((naked, used, section(".ram.code_text")))
void wolfboot_ram_entry(void)
{
    __asm__ volatile(
        "ldr  r0, =END_STACK\n\t"
        "msr  msp, r0\n\t"
        "ldr  r0, =isr_reset\n\t"
        "bx   r0\n\t"
    );
}

/* ===========================================================================
 * Debug UART (wolfBoot banner / logs).
 * ===========================================================================
 */
#ifdef DEBUG_UART
/* Bare-metal console on UART1 (0x40040400), which the RealTek Zephyr port uses
 * as its "loguart" (DTS serial@40040400, pins PORT_F 4=TX/3=RX = 0xA4/0xA3,
 * function PID_UART1). It reaches the EVB FT232 / ttyUSB5. Registers:
 * TFLVR @ 0x54 (tx_fifo_lv bits[4:0]), THR @ 0x24, TX FIFO depth 16.
 *
 * The RealTek ROM printf is NOT used (its ROM stdio_port putc is unregistered
 * from wolfBoot -> INVSTATE fault) and the SDK hal_uart_init() hangs (it pulls
 * IRQ/GDMA/OS primitives). uart_init() instead brings UART1 up self-containedly:
 * init the pinmux manager, route PF4/PF3 to UART1, enable the UART1 clock, then
 * program baud + 8N1 via the register-level leaf functions with a hand-populated
 * adapter (the leaf set_baudrate only needs the baud tables + sclk). Verified on
 * hardware: the wolfBoot banner prints cleanly on ttyUSB5. */
#ifndef RTL8735B_LOGUART_BASE
#define RTL8735B_LOGUART_BASE   0x40040400UL   /* UART1 (Zephyr loguart serial@40040400) */
#endif
#define RTL8735B_UART_TFLVR     (*(volatile uint32_t *)(RTL8735B_LOGUART_BASE + 0x54))
#define RTL8735B_UART_THR       (*(volatile uint32_t *)(RTL8735B_LOGUART_BASE + 0x24))
#define RTL8735B_UART_TX_FIFO   16u

/* Console pins (per the RealTek Zephyr board pinctrl): PORT_F pin4 = TXD,
 * pin3 = RXD, function PID_UART1. Pin name = (port<<5)|pin; UART1 func id =
 * (FUNC_UART<<28)|1 = 0x60000001. The RealTek bootloader muxes these pins to
 * its own console peripheral, so wolfBoot must (re)route them to UART1
 * (0x40040400) before its writes reach the FT232/ttyUSB5 console. */
#define RTL8735B_PF4_TXD   0xA4u
#define RTL8735B_PF3_RXD   0xA3u
#define RTL8735B_PID_UART1 0x60000001u

#ifdef HAL_BACKEND_SDK
/* UART baud tables for hal_uart_set_baudrate (which only needs the adapter's
 * table pointers + sclk populated; it then writes DLL/DLM + OVSR). These are
 * the SDK fwlib def_*_40m_patch[] values, EXCEPT index 13 (115200) which is
 * retuned for this board's measured UART1 clock (~50 MHz, not the SDK comment's
 * 40 MHz PXP value): OVSR 16 * DIV 27 = 432, 50e6/432 ~= 115740 (within 0.5%).
 * wolfBoot only ever requests 115200, so the other (40 MHz) entries are
 * reference-only. */
static const uint32_t rtl8735b_baud_tbl[] = {
    110, 300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 28800, 38400, 57600,
    76800, 115200, 128000, 153600, 230400, 380400, 460800, 500000, 921600,
    1000000, 1500000, 1536000, 2000000, 2500000, 3000000, 3500000, 4000000,
    6000000, 8000000, 10000000, 12000000, 16000000, 20000000
};
static const uint8_t rtl8735b_ovsr_tbl[] = {
    20, 20, 20, 20, 20, 17, 17, 15, 10, 11, 10, 11, 10, 16, 12, 10, 10, 15, 17,
    20, 14, 20, 14, 9, 13, 7, 20, 19, 7, 13, 6, 5, 5, 10, 6
};
static const uint16_t rtl8735b_div_tbl[] = {
    18173, 6664, 3332, 1666, 833, 490, 245, 185, 208, 126, 104, 63, 52, 27, 26,
    26, 17, 7, 5, 4, 3, 2, 2, 3, 2, 3, 1, 1, 2, 1, 2, 2, 2, 1, 1
};
static const uint8_t rtl8735b_adj10_tbl[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 0, 4, 0, 5, 0, 5, 2, 3,
    2, 0, 0, 2, 3, 1, 4, 3, 0, 7
};
static const uint8_t rtl8735b_adj9_tbl[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 0, 3, 0, 4, 0, 4, 2, 3,
    2, 0, 0, 2, 3, 1, 4, 3, 0, 6
};
static const uint8_t rtl8735b_adj8_tbl[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 0, 3, 0, 4, 0, 4, 2, 3,
    2, 0, 0, 2, 3, 1, 3, 3, 0, 5
};
static hal_uart_adapter_t rtl8735b_uart1;
static hal_pin_mux_mang_t rtl8735b_pinmux_mgr;
#endif /* HAL_BACKEND_SDK */

void uart_init(void)
{
#ifdef HAL_BACKEND_SDK
    /* Initialize the pinmux manager (the bootloader's state does not carry into
     * wolfBoot), then route the console pins to UART1 (the bootloader leaves
     * them on its own console peripheral). */
    hal_pinmux_manager_init(&rtl8735b_pinmux_mgr);
    (void)hal_pinmux_register(RTL8735B_PF4_TXD, RTL8735B_PID_UART1);
    (void)hal_pinmux_register(RTL8735B_PF3_RXD, RTL8735B_PID_UART1);

    /* Bring up UART1 without the hanging hal_uart_init: hand-populate the
     * adapter (the SDK 40 MHz baud tables + the UartSCLK_40M enum), enable the
     * clock, then use the register-level leaf functions for 8N1 format and baud.
     * The board's real UART1 clock is ~50 MHz (see the baud-table note above):
     * the sclk enum stays 40M, but the 115200 table entry is retuned (OVSR/DIV)
     * so the programmed divisor matches the actual clock. */
    memset(&rtl8735b_uart1, 0, sizeof(rtl8735b_uart1));
    rtl8735b_uart1.base_addr = (UART_Type *)RTL8735B_LOGUART_BASE;
    rtl8735b_uart1.uart_idx = (uint8_t)Uart1;
    rtl8735b_uart1.uart_sclk = (uint8_t)UartSCLK_40M;
    rtl8735b_uart1.pdef_baudrate_tbl = rtl8735b_baud_tbl;
    rtl8735b_uart1.pdef_ovsr_tbl = rtl8735b_ovsr_tbl;
    rtl8735b_uart1.pdef_div_tbl = rtl8735b_div_tbl;
    rtl8735b_uart1.pdef_ovsradjbit_tbl10 = rtl8735b_adj10_tbl;
    rtl8735b_uart1.pdef_ovsradjbit_tbl9 = rtl8735b_adj9_tbl;
    rtl8735b_uart1.pdef_ovsradjbit_tbl8 = rtl8735b_adj8_tbl;
    hal_uart_en_ctrl((uint8_t)Uart1, ON);
    (void)hal_uart_set_format(&rtl8735b_uart1, 8, 0, 1);
    (void)hal_uart_set_baudrate(&rtl8735b_uart1, 115200);
#endif
}

static void rtl8735b_uart_putc(char c)
{
    uint32_t timeout = 0;

    /* Wait while the TX FIFO is full (level >= depth); bounded so a
     * misconfigured base cannot hang. */
    while ((RTL8735B_UART_TFLVR & 0x1Fu) >= RTL8735B_UART_TX_FIFO) {
        if (++timeout > 1000000u) {
            break;
        }
    }
    RTL8735B_UART_THR = (uint32_t)(unsigned char)c;
}

void uart_write(const char *buf, unsigned int sz)
{
    unsigned int i;

    for (i = 0; i < sz; i++) {
        if (buf[i] == '\n') {
            rtl8735b_uart_putc('\r');
        }
        rtl8735b_uart_putc(buf[i]);
    }
}
#endif /* DEBUG_UART */

/* ===========================================================================
 * HAL init / boot handoff.
 * ===========================================================================
 */
void hal_init(void)
{
#ifdef DEBUG_UART
    /* AON boot-reason register (AON_BASE 0x40009000 + 0x104): bit 0 = Vendor
     * watchdog, bit 1 = AON watchdog, bit 4 = brown-out. */
    volatile uint32_t *aon_boot_reason = (volatile uint32_t *)0x40009104u;
    uint32_t rr;
#endif

    /* Enable the FPU (CP10/CP11 in CPACR) before any other code runs. arch.mk
     * builds every backend with -mfpu=fpv5-sp-d16, so the compiler may emit VFP
     * instructions (and the RealTek SDK drivers certainly do); they fault if the
     * FPU is left disabled after reset. This is backend-independent, so it is
     * not gated on HAL_BACKEND_SDK. */
    *((volatile uint32_t *)0xE000ED88) |= (0xFu << 20);
    __asm__ volatile("dsb\n\tisb\n\t");

#ifdef DEBUG_UART
    uart_init();
#endif

    wolfBoot_printf("wolfBoot HAL: RTL8735B (AmebaPro2) init\n");

#ifdef DEBUG_UART
    /* Report why the SoC last reset, then W1C the latched status bits so each
     * boot shows its own cause (helps spot e.g. an unfed watchdog reset). */
    rr = *aon_boot_reason;
    wolfBoot_printf("Reset reason: 0x%x%s%s%s\n", rr,
        (rr & 0x1u)  ? " VNDR-WDT" : "",
        (rr & 0x2u)  ? " AON-WDT"  : "",
        (rr & 0x10u) ? " BOD"      : "");
    *aon_boot_reason = rr & 0x33u;
#endif

#ifdef HAL_BACKEND_SDK
    /* Initialize the external SPI NOR with a fresh spic_init() (flash_init()
     * binds a new adaptor). The RealTek bootloader does NOT hand off its own
     * initialized SPIC adaptor to wolfBoot -- verified on hardware: the RAM
     * start-table phal_spic_adaptor field is left at our placeholder buffer, and
     * that buffer holds no valid adaptor (a real adaptor has spic_dev pointing at
     * the SPIC base 0x40006000; the handoff buffer is unrelated data). So a fresh
     * bind is required, not merely preferred -- there is no adaptor to reuse. */
    memset(&hal_flash_obj, 0, sizeof(hal_flash_obj));
    flash_init(&hal_flash_obj);
    wolfBoot_printf("wolfBoot HAL: flash init done\n");
#endif
}

void hal_prepare_boot(void)
{
    /* The application was written into DDR via ext_flash_read()/memcpy. The
     * RTL8735B has its own I/D cache (not Arm SCB): clean+invalidate D-cache so
     * the writes hit DDR, then invalidate I-cache so stale lines do not shadow
     * the new app, before do_boot() jumps. */
#ifdef HAL_BACKEND_SDK
    dcache_clean_invalidate();
    icache_invalidate();
#endif
    __asm__ volatile("dsb\n\tisb\n\t");
}

/* ===========================================================================
 * Internal flash: physically unused in this design. wolfBoot is SRAM-resident
 * and ALL partitions (BOOT/UPDATE/SWAP) live in external SPI NOR (PART_*_EXT),
 * so libwolfboot never routes a write/erase through these entry points -- they
 * are intentional no-ops, not unimplemented stubs. (A future config that placed
 * a partition in internal flash would need a real implementation here.)
 * ===========================================================================
 */
void hal_flash_unlock(void)
{
}

void hal_flash_lock(void)
{
}

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

/* ===========================================================================
 * External SPI NOR (raw byte offsets).
 * ===========================================================================
 */
void ext_flash_unlock(void)
{
#ifdef HAL_BACKEND_SDK
    flash_global_unlock();
#endif
}

void ext_flash_lock(void)
{
#ifdef HAL_BACKEND_SDK
    flash_global_lock();
#endif
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    if (len <= 0) {
        return -1;
    }
#ifdef HAL_BACKEND_SDK
    /* flash_stream_read returns 1 on success; propagate a read failure rather
     * than handing potentially-stale data to the verifier. */
    if (flash_stream_read(&hal_flash_obj, (uint32_t)address, (uint32_t)len,
                          data) != 1) {
        return -1;
    }
    return len;
#else
    /* TODO(bare): implement via direct SPIC / ROM hal_flash_stream_read. */
    (void)address;
    (void)data;
    return -1;
#endif
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    if (len <= 0) {
        return -1;
    }
#ifdef HAL_BACKEND_SDK
    /* flash_stream_write returns 1 on success; propagate a write failure. */
    if (flash_stream_write(&hal_flash_obj, (uint32_t)address, (uint32_t)len,
                           (uint8_t *)data) != 1) {
        return -1;
    }
    return 0;
#else
    /* TODO(bare): implement via direct SPIC / ROM hal_flash_burst_write. */
    (void)address;
    (void)data;
    return -1;
#endif
}

int ext_flash_erase(uintptr_t address, int len)
{
    uint32_t sector_addr;
    uint32_t end_addr;

    if (len <= 0 || (uint32_t)len > UINT32_MAX - (uint32_t)address) {
        return -1;
    }

    /* Erase every WOLFBOOT_SECTOR_SIZE sector spanning [address, address+len).
     * Unlike flash_stream_read/write, the SDK flash_erase_sector returns void
     * (no status to propagate), so there is no per-sector failure to check. */
    sector_addr = (uint32_t)address & ~((uint32_t)WOLFBOOT_SECTOR_SIZE - 1);
    end_addr = (uint32_t)address + (uint32_t)len;
    while (sector_addr < end_addr) {
#ifdef HAL_BACKEND_SDK
        flash_erase_sector(&hal_flash_obj, sector_addr);
#else
        /* TODO(bare): implement via direct SPIC / ROM hal_flash_sector_erase. */
        return -1;
#endif
        sector_addr += WOLFBOOT_SECTOR_SIZE;
    }
    return 0;
}
