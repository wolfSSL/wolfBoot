/* mpfs250.h
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

#ifndef MPFS250_DEF_INCLUDED
#define MPFS250_DEF_INCLUDED

/* Generic RISC-V definitions are included at the end of this file
 * (after PLIC_BASE is defined) to enable PLIC function declarations */

/* PolarFire SoC MPFS250T board specific configuration */

/* APB/AHB Clock Frequency
 * M-mode (out of reset): 40 MHz
 * S-mode (after HSS): 150 MHz
 */
#ifndef MSS_APB_AHB_CLK
    #ifdef WOLFBOOT_RISCV_MMODE
        #define MSS_APB_AHB_CLK    40000000
    #else
        #define MSS_APB_AHB_CLK    150000000
    #endif
#endif

/* Hardware Base Address */
#define SYSREG_BASE 0x20002000

/* Write "0xDEAD" to cause a full MSS reset*/
#define SYSREG_MSS_RESET_CR (*((volatile uint32_t*)(SYSREG_BASE + 0x18)))

/* Peripheral Subblock Clock Control Register (offset 0x84) */
#define SYSREG_SUBBLK_CLOCK_CR (*((volatile uint32_t*)(SYSREG_BASE + 0x84)))
#define SYSREG_SUBBLK_CLOCK_CR_ENVM (1U << 0)
#define SYSREG_SUBBLK_CLOCK_CR_MMC  (1U << 3)
#define SYSREG_SUBBLK_CLOCK_CR_QSPI (1U << 19)

/* Peripheral Soft Reset Control Register (offset 0x88) */
#define SYSREG_SOFT_RESET_CR (*((volatile uint32_t*)(SYSREG_BASE + 0x88)))

/* MSS Peripheral control bits (shared by SUBBLK_CLOCK_CR and SOFT_RESET_CR) */
#define MSS_PERIPH_ENVM     (1U << 0)
#define MSS_PERIPH_MMC      (1U << 3)
#define MSS_PERIPH_MMUART0  (1U << 5)
#define MSS_PERIPH_MMUART1  (1U << 6)
#define MSS_PERIPH_MMUART2  (1U << 7)
#define MSS_PERIPH_MMUART3  (1U << 8)
#define MSS_PERIPH_MMUART4  (1U << 9)
#define MSS_PERIPH_SPI0     (1U << 10)
#define MSS_PERIPH_SPI1     (1U << 11)
#define MSS_PERIPH_QSPI     (1U << 19)
#define MSS_PERIPH_GPIO0    (1U << 20)
#define MSS_PERIPH_GPIO1    (1U << 21)
#define MSS_PERIPH_GPIO2    (1U << 22)
#define MSS_PERIPH_DDRC     (1U << 23)
#define MSS_PERIPH_ATHENA   (1U << 28)  /* Crypto hardware accelerator */


/* UART */
#define MSS_UART0_LO_BASE  0x20000000UL
#define MSS_UART1_LO_BASE  0x20100000UL
#define MSS_UART2_LO_BASE  0x20102000UL
#define MSS_UART3_LO_BASE  0x20104000UL
#define MSS_UART4_LO_BASE  0x20106000UL

#define MSS_UART0_HI_BASE  0x28000000UL
#define MSS_UART1_HI_BASE  0x28100000UL
#define MSS_UART2_HI_BASE  0x28102000UL
#define MSS_UART3_HI_BASE  0x28104000UL
#define MSS_UART4_HI_BASE  0x28106000UL

/* UART base address array for per-hart access (LO addresses for M-mode) */
#ifndef __ASSEMBLER__
static const unsigned long MSS_UART_BASE_ADDR[] = {
    MSS_UART0_LO_BASE,  /* Hart 0 (E51) -> MMUART0 */
    MSS_UART1_LO_BASE,  /* Hart 1 (U54_1) -> MMUART1 */
    MSS_UART2_LO_BASE,  /* Hart 2 (U54_2) -> MMUART2 */
    MSS_UART3_LO_BASE,  /* Hart 3 (U54_3) -> MMUART3 */
    MSS_UART4_LO_BASE   /* Hart 4 (U54_4) -> MMUART4 */
};
#define UART_BASE_FOR_HART(hart) (MSS_UART_BASE_ADDR[(hart) < 5 ? (hart) : 0])
#endif /* __ASSEMBLER__ */

/* Debug UART port selection (0-4): M-mode defaults to UART0, S-mode to UART1 */
#ifndef DEBUG_UART_PORT
    #ifdef WOLFBOOT_RISCV_MMODE
        #define DEBUG_UART_PORT 0
    #else
        #define DEBUG_UART_PORT 1
    #endif
#endif

/* Derive base address from port number */
#if DEBUG_UART_PORT == 0
    #define DEBUG_UART_BASE MSS_UART0_LO_BASE
#elif DEBUG_UART_PORT == 1
    #define DEBUG_UART_BASE MSS_UART1_LO_BASE
#elif DEBUG_UART_PORT == 2
    #define DEBUG_UART_BASE MSS_UART2_LO_BASE
#elif DEBUG_UART_PORT == 3
    #define DEBUG_UART_BASE MSS_UART3_LO_BASE
#elif DEBUG_UART_PORT == 4
    #define DEBUG_UART_BASE MSS_UART4_LO_BASE
#else
    #error "Invalid DEBUG_UART_PORT (must be 0-4)"
#endif

#define MMUART_RBR(base) *((volatile uint8_t*)((base)) + 0x00) /* Receiver buffer register */
#define MMUART_IER(base) *((volatile uint8_t*)((base)) + 0x04) /* Interrupt enable register */
#define MMUART_IIR(base) *((volatile uint8_t*)((base)) + 0x08) /* Interrupt ID register */
#define MMUART_LCR(base) *((volatile uint8_t*)((base)) + 0x0C) /* Line control register */
#define MMUART_MCR(base) *((volatile uint8_t*)((base)) + 0x10) /* Modem control register */
#define MMUART_LSR(base) *((volatile uint8_t*)((base)) + 0x14) /* Line status register */
#define MMUART_MSR(base) *((volatile uint8_t*)((base)) + 0x18) /* Modem status register */
#define MMUART_SCR(base) *((volatile uint8_t*)((base)) + 0x1C) /* Scratch register */
#define MMUART_IEM(base) *((volatile uint8_t*)((base)) + 0x24) /* Interrupt enable mask */
#define MMUART_IIM(base) *((volatile uint8_t*)((base)) + 0x28) /* multi-mode Interrupt ID register */
#define MMUART_MM0(base) *((volatile uint8_t*)((base)) + 0x30) /* Mode register 0 */
#define MMUART_MM1(base) *((volatile uint8_t*)((base)) + 0x34) /* Mode register 1 */
#define MMUART_MM2(base) *((volatile uint8_t*)((base)) + 0x38) /* Mode register 2 */
#define MMUART_DFR(base) *((volatile uint8_t*)((base)) + 0x3C) /* Data frame register */
#define MMUART_GFR(base) *((volatile uint8_t*)((base)) + 0x44) /* Global filter register */
#define MMUART_TTG(base) *((volatile uint8_t*)((base)) + 0x48) /* TX time guard register */
#define MMUART_RTO(base) *((volatile uint8_t*)((base)) + 0x4C) /* RX timeout register */
#define MMUART_ADR(base) *((volatile uint8_t*)((base)) + 0x50) /* Address register */
#define MMUART_DLR(base) *((volatile uint8_t*)((base)) + 0x80) /* Divisor latch register */
#define MMUART_DMR(base) *((volatile uint8_t*)((base)) + 0x84) /* Divisor mode register */
#define MMUART_THR(base) *((volatile uint8_t*)((base)) + 0x100) /* Transmitter holding register */
#define MMUART_FCR(base) *((volatile uint8_t*)((base)) + 0x104) /* FIFO control register */


/* LCR (Line Control Register) */
#define MSS_UART_DATA_8_BITS        ((uint8_t)0x03)
#define MSS_UART_NO_PARITY          ((uint8_t)0x00)
#define MSS_UART_ONE_STOP_BIT       ((uint8_t)0x00)

/* LSR (Line Status Register) */
#define MSS_UART_THRE               ((uint8_t)0x20)    /* Transmitter holding register empty */
#define MSS_UART_TEMT               ((uint8_t)0x40)    /* Transmit empty */

#define ELIN_MASK            (1U << 3) /* Enable LIN header detection */
#define EIRD_MASK            (1U << 2) /* Enable IrDA modem */
#define EERR_MASK            (1U << 0) /* Enable ERR / NACK during stop time */

#define RXRDY_TXRDYN_EN_MASK (1U << 0) /* Enable TXRDY and RXRDY signals */
#define CLEAR_RX_FIFO_MASK   (1U << 1) /* Clear receiver FIFO */
#define CLEAR_TX_FIFO_MASK   (1U << 2) /* Clear transmitter FIFO */

#define LOOP_MASK            (1U << 4) /* Local loopback */
#define RLOOP_MASK           (1U << 5) /* Remote loopback & Automatic echo*/

#define E_MSB_RX_MASK        (1U << 0) /* MSB / LSB first for receiver */
#define E_MSB_TX_MASK        (1U << 1) /* MSB / LSB first for transmitter */

#define EAFM_MASK            (1U << 1) /* Enable 9-bit address flag mode */
#define ETTG_MASK            (1U << 5) /* Enable transmitter time guard */
#define ERTO_MASK            (1U << 6) /* Enable receiver time-out */
#define ESWM_MASK            (1U << 3) /* Enable single wire half-duplex mode */
#define EFBR_MASK            (1U << 7) /* Enable fractional baud rate mode */

/* Line Control register bit masks */
#define SB_MASK              (1U << 6) /* Set break */
#define DLAB_MASK            (1U << 7) /* Divisor latch access bit */

/* ============================================================================
 * EMMC/SD Card Controller (Cadence SD4HC)
 * Base Address: 0x20008000
 * ============================================================================ */
#define EMMC_SD_BASE 0x20008000UL

/* ============================================================================
 * System Controller Mailbox
 * Control Base: 0x37020000
 * Mailbox RAM:  0x37020800
 *
 * Used for system services like reading the device serial number.
 * ============================================================================ */
#define SCBCTRL_BASE       0x37020000UL
#define SCBMBOX_BASE       0x37020800UL

/* System Services Control and Status Register offsets (from SCBCTRL_BASE) */
#define SERVICES_CR_OFFSET 0x50u
#define SERVICES_SR_OFFSET 0x54u

/* Control Register bits */
#define SERVICES_CR_REQ_MASK      0x01u
#define SERVICES_CR_COMMAND_SHIFT 16

/* Status Register bits */
#define SERVICES_SR_BUSY_MASK     0x02u
#define SERVICES_SR_STATUS_SHIFT  16

/* System Service command opcodes */
#define SYS_SERV_CMD_SERIAL_NUMBER 0x00u
#define SYS_SERV_CMD_SPI_COPY      0x50u /* SCB mailbox SPI copy service */

/* Device serial number size in bytes */
#define DEVICE_SERIAL_NUMBER_SIZE 16

/* Timeout loop iteration counts (override at build time via CFLAGS) */
#ifndef MPFS_SCB_TIMEOUT
#define MPFS_SCB_TIMEOUT          10000     /* SCB mailbox polling */
#endif
#ifndef QSPI_TIMEOUT_TRIES
#define QSPI_TIMEOUT_TRIES       100000    /* QSPI controller/TX polling */
#endif
#ifndef QSPI_RX_TIMEOUT_TRIES
#define QSPI_RX_TIMEOUT_TRIES    1000000   /* QSPI RX polling (longer) */
#endif

/* System Controller register access */
#define SCBCTRL_REG(off) (*((volatile uint32_t*)(SCBCTRL_BASE + (off))))
#define SCBMBOX_REG(off) (*((volatile uint32_t*)(SCBMBOX_BASE + (off))))
#define SCBMBOX_BYTE(off) (*((volatile uint8_t*)(SCBMBOX_BASE + (off))))

/* System Controller Mailbox API */
#ifndef __ASSEMBLER__
int mpfs_scb_service_call(uint8_t opcode, const uint8_t *mb_data,
    uint32_t mb_len, uint32_t timeout);
int mpfs_scb_read_mailbox(uint8_t *out, uint32_t len);
int mpfs_read_serial_number(uint8_t *serial);
#endif /* __ASSEMBLER__ */

/* Crypto Engine: Athena F5200 TeraFire Crypto Processor (1x), 200 MHz */
#define ATHENA_BASE (SYSREG_BASE + 0x125000)


/* ============================================================================
 * L2 Cache Controller (CACHE_CTRL @ 0x02010000)
 * Controls cache ways, way masks, and scratchpad configuration
 * ============================================================================ */
#define L2_CACHE_BASE               0x02010000UL

/* L2 Cache Control Registers */
#define L2_CONFIG                   (*(volatile uint64_t*)(L2_CACHE_BASE + 0x000))
#define L2_WAY_ENABLE               (*(volatile uint64_t*)(L2_CACHE_BASE + 0x008))
#define L2_FLUSH64                  (*(volatile uint64_t*)(L2_CACHE_BASE + 0x200))
#define L2_FLUSH32                  (*(volatile uint32_t*)(L2_CACHE_BASE + 0x240))

/* Way Mask Registers - control which cache ways each master can access
 * Value 0xFF = access to ways 0-7 (cache ways)
 * Scratchpad ways (8-11) require explicit enabling */
#define L2_WAY_MASK_DMA             (*(volatile uint64_t*)(L2_CACHE_BASE + 0x800))
#define L2_WAY_MASK_AXI4_PORT0      (*(volatile uint64_t*)(L2_CACHE_BASE + 0x808))
#define L2_WAY_MASK_AXI4_PORT1      (*(volatile uint64_t*)(L2_CACHE_BASE + 0x810))
#define L2_WAY_MASK_AXI4_PORT2      (*(volatile uint64_t*)(L2_CACHE_BASE + 0x818))
#define L2_WAY_MASK_AXI4_PORT3      (*(volatile uint64_t*)(L2_CACHE_BASE + 0x820))
#define L2_WAY_MASK_E51_DCACHE      (*(volatile uint64_t*)(L2_CACHE_BASE + 0x828))
#define L2_WAY_MASK_E51_ICACHE      (*(volatile uint64_t*)(L2_CACHE_BASE + 0x830))
#define L2_WAY_MASK_U54_1_DCACHE    (*(volatile uint64_t*)(L2_CACHE_BASE + 0x838))
#define L2_WAY_MASK_U54_1_ICACHE    (*(volatile uint64_t*)(L2_CACHE_BASE + 0x840))
#define L2_WAY_MASK_U54_2_DCACHE    (*(volatile uint64_t*)(L2_CACHE_BASE + 0x848))
#define L2_WAY_MASK_U54_2_ICACHE    (*(volatile uint64_t*)(L2_CACHE_BASE + 0x850))
#define L2_WAY_MASK_U54_3_DCACHE    (*(volatile uint64_t*)(L2_CACHE_BASE + 0x858))
#define L2_WAY_MASK_U54_3_ICACHE    (*(volatile uint64_t*)(L2_CACHE_BASE + 0x860))
#define L2_WAY_MASK_U54_4_DCACHE    (*(volatile uint64_t*)(L2_CACHE_BASE + 0x868))
#define L2_WAY_MASK_U54_4_ICACHE    (*(volatile uint64_t*)(L2_CACHE_BASE + 0x870))

/* L2 Shutdown Control Register */
#define SYSREG_L2_SHUTDOWN_CR       (*(volatile uint32_t*)(SYSREG_BASE + 0x174))

/* L2 Cache/Scratchpad constants */
#define L2_NUM_CACHE_WAYS           8       /* Ways 0-7 are cache */
#define L2_NUM_SCRATCH_WAYS         4       /* Ways 8-11 are scratchpad */
#define L2_WAY_BYTE_LENGTH          0x20000 /* 128KB per way */
#define L2_SCRATCH_BASE             0x0A000000UL
#define L2_SCRATCH_SIZE             (L2_NUM_SCRATCH_WAYS * L2_WAY_BYTE_LENGTH) /* 512KB */

/* Way enable values */
#define L2_WAY_ENABLE_RESET         0x01    /* Only way 0 at reset */
#define L2_WAY_ENABLE_ALL_CACHE     0xFF    /* Ways 0-7 (all cache ways) */
#define L2_WAY_ENABLE_WITH_SCRATCH  0x0FFF  /* Ways 0-11 (cache + scratchpad) */

/* Way mask for cache-only access (no scratchpad) */
#define L2_WAY_MASK_CACHE_ONLY      0xFF


/* ============================================================================
 * NWC (Northwest Corner) - Clock and System Configuration
 *
 * The NWC contains clocks, PLLs, SGMII, and DDR PHY configuration.
 * These registers must be configured for proper system operation.
 * ============================================================================ */

/* SCB Configuration Block (SCBCFG @ 0x37080000) */
#define SCBCFG_BASE                 0x37080000UL
#define SCBCFG_TIMER                (*(volatile uint32_t*)(SCBCFG_BASE + 0x08))

/* MSS_SCB_ACCESS_CONFIG value for proper SCB access timing */
#define MSS_SCB_ACCESS_CONFIG       0x0008A080UL

/* DDR SGMII PHY Configuration (CFG_DDR_SGMII_PHY @ 0x20007000) */
#define CFG_DDR_SGMII_PHY_BASE      0x20007000UL
#define DDRPHY_STARTUP              (*(volatile uint32_t*)(CFG_DDR_SGMII_PHY_BASE + 0x008))
#define DDRPHY_DYN_CNTL             (*(volatile uint32_t*)(CFG_DDR_SGMII_PHY_BASE + 0xC1C))

/* DDR PHY startup configuration value (from working DDR demo) */
#define DDRPHY_STARTUP_CONFIG       0x003F1F00UL
#define DDRPHY_DYN_CNTL_CONFIG      0x0000047FUL

/* DFI APB interface control (enables DDR PHY APB access) */
#define SYSREG_DFIAPB_CR            (*(volatile uint32_t*)(SYSREG_BASE + 0x98))

/* CLINT - Core Local Interruptor (for timer and software interrupts)
 * Note: CLINT macros are defined in hal/riscv.h, only define base if not present */
#ifndef CLINT_BASE
#define CLINT_BASE                  0x02000000UL
#endif

/* RTC Clock Frequency (1 MHz after divisor) */
#define RTC_CLOCK_FREQ              1000000UL


/* ============================================================================
 * Hart Local Storage (HLS) - Per-hart communication structure
 *
 * Used for inter-hart communication during boot.
 * Located at top of each hart's stack (sp - 64).
 * ============================================================================ */
#define HLS_DEBUG_AREA_SIZE         64

#ifndef __ASSEMBLER__
typedef struct {
    volatile uint32_t in_wfi_indicator;  /* 0x00: Hart status indicator */
    volatile uint32_t my_hart_id;        /* 0x04: Hart ID */
    volatile uint32_t shared_mem_marker; /* 0x08: Init marker */
    volatile uint32_t shared_mem_status; /* 0x0C: Status */
    volatile uint64_t* shared_mem;       /* 0x10: Shared memory pointer */
    volatile uint64_t reserved[2];       /* 0x18: Reserved/padding */
} HLS_DATA;  /* Size: 64 bytes (HLS_DEBUG_AREA_SIZE) */
#endif /* __ASSEMBLER__ */

/* HLS status indicator values */
#define HLS_MAIN_HART_STARTED       0x12344321UL
#define HLS_OTHER_HART_IN_WFI       0x12345678UL
#define HLS_OTHER_HART_PASSED_WFI   0x87654321UL
#define HLS_MAIN_HART_FIN_INIT      0x55555555UL

/* Number of harts on MPFS */
#define MPFS_NUM_HARTS              5
#define MPFS_FIRST_HART             0   /* E51 is hart 0 */
#define MPFS_FIRST_U54_HART         1   /* First U54 is hart 1 */
#define MPFS_LAST_U54_HART          4   /* Last U54 is hart 4 */

/* Stack configuration per hart */
#ifndef STACK_SIZE_PER_HART
#define STACK_SIZE_PER_HART         8192
#endif

/* Multi-hart function declarations */
#ifndef __ASSEMBLER__
#ifdef DEBUG_UART
void uart_init(void);
void uart_write(const char* buf, unsigned int sz);
#endif
#ifdef WOLFBOOT_RISCV_MMODE
int mpfs_wake_secondary_harts(void);
void secondary_hart_entry(unsigned long hartid, HLS_DATA* hls);
void uart_init_hart(unsigned long hartid);
void uart_write_hart(unsigned long hartid, const char* buf, unsigned int sz);
#endif
#endif /* __ASSEMBLER__ */



/* ============================================================================
 * PLIC - Platform-Level Interrupt Controller (MPFS250-specific configuration)
 * Base Address: 0x0c000000, Size: 64MB
 *
 * Generic PLIC register access is provided by hal/riscv.h
 * ============================================================================ */
#define PLIC_BASE               0x0C000000UL
#define PLIC_SIZE               0x04000000UL  /* 64MB */

/* Number of interrupt sources and contexts */
#define PLIC_NUM_SOURCES        186           /* riscv,ndev = 0xBA = 186 */
#define PLIC_NUM_HARTS          5             /* 1x E51 + 4x U54 */
#define PLIC_NUM_CONTEXTS       10            /* 2 contexts per hart (M-mode + S-mode) */

/* MSS Global Interrupt offset - PLIC interrupts 0-12 are local, 13+ are MSS */
#define OFFSET_TO_MSS_GLOBAL_INTS   13

/* PLIC Interrupt Sources (PLIC IRQ numbers) */
#define PLIC_INT_MMC_MAIN       88            /* MMC/SD controller main interrupt */
#define PLIC_INT_MMC_WAKEUP     89            /* MMC/SD controller wakeup interrupt */

/* PLIC Context IDs for each hart
 * Hart 0 (E51):  Context 0 = M-mode (no S-mode on E51)
 * Hart 1 (U54):  Context 1 = M-mode, Context 2 = S-mode
 * Hart 2 (U54):  Context 3 = M-mode, Context 4 = S-mode
 * Hart 3 (U54):  Context 5 = M-mode, Context 6 = S-mode
 * Hart 4 (U54):  Context 7 = M-mode, Context 8 = S-mode
 */
#define PLIC_CONTEXT_E51_M      0
#define PLIC_CONTEXT_U54_1_M    1
#define PLIC_CONTEXT_U54_1_S    2
#define PLIC_CONTEXT_U54_2_M    3
#define PLIC_CONTEXT_U54_2_S    4
#define PLIC_CONTEXT_U54_3_M    5
#define PLIC_CONTEXT_U54_3_S    6
#define PLIC_CONTEXT_U54_4_M    7
#define PLIC_CONTEXT_U54_4_S    8


#ifdef EXT_FLASH
/* ==========================================================================
 * QSPI Flash Controller Definitions
 *
 * PolarFire SoC has two CoreQSPI v2 controllers with identical registers:
 *
 * 1. System Controller QSPI (MPFS_SC_SPI=1, default):
 *    - SC QSPI at 0x37020100 (size 0x100)
 *    - For fabric-connected flash (design flash)
 *    - Direct register access (same register layout as MSS QSPI)
 *    - Supports read, write, and erase operations
 *    - Does NOT require MSS clock enable or soft reset
 *
 * 2. MSS QSPI Controller (MPFS_SC_SPI=0):
 *    - MSS QSPI at 0x21000000 (size 0x1000)
 *    - For external flash connected to MSS QSPI pins
 *    - Requires MSS QSPI clock enable and soft reset release
 *    - Supports read, write, and erase operations
 *
 * ========================================================================== */

/* QSPI Controller Base Address */
#ifndef QSPI_BASE
#ifdef MPFS_SC_SPI
#define QSPI_BASE 0x37020100u /* SC QSPI Controller (fabric-connected flash) */
#else
#define QSPI_BASE 0x21000000u /* MSS QSPI Controller (external flash) */
#endif
#endif

/* QSPI Register Offsets */
#define QSPI_CONTROL    (*(volatile uint32_t *)(QSPI_BASE + 0x00))
#define QSPI_FRAMES     (*(volatile uint32_t *)(QSPI_BASE + 0x04))
#define QSPI_IEN        (*(volatile uint32_t *)(QSPI_BASE + 0x0C))
#define QSPI_STATUS     (*(volatile uint32_t *)(QSPI_BASE + 0x10))
#define QSPI_DIRECT     (*(volatile uint32_t *)(QSPI_BASE + 0x14))
#define QSPI_ADDRUP     (*(volatile uint32_t *)(QSPI_BASE + 0x18))
#define QSPI_RX_DATA    (*(volatile uint8_t  *)(QSPI_BASE + 0x40))
#define QSPI_TX_DATA    (*(volatile uint8_t  *)(QSPI_BASE + 0x44))
#define QSPI_X4_RX_DATA (*(volatile uint32_t *)(QSPI_BASE + 0x48))
#define QSPI_X4_TX_DATA (*(volatile uint32_t *)(QSPI_BASE + 0x4C))
#define QSPI_FRAMESUP   (*(volatile uint32_t *)(QSPI_BASE + 0x50))

/* QSPI Control Register Bits */
#define QSPI_CTRL_EN_OFFSET      0
#define QSPI_CTRL_XIP_OFFSET     2
#define QSPI_CTRL_XIPADDR_OFFSET 3
#define QSPI_CTRL_CLKIDLE_OFFSET 10
#define QSPI_CTRL_SAMPLE_OFFSET  11
#define QSPI_CTRL_QMODE0_OFFSET  13
#define QSPI_CTRL_QMODE12_OFFSET 14
#define QSPI_CTRL_FLAGSX4_OFFSET 16
#define QSPI_CTRL_CLKRATE_OFFSET 24

#define QSPI_CTRL_EN           (1u << QSPI_CTRL_EN_OFFSET)
#define QSPI_CTRL_XIP          (1u << QSPI_CTRL_XIP_OFFSET)
#define QSPI_CTRL_CLKIDLE      (1u << QSPI_CTRL_CLKIDLE_OFFSET)
#define QSPI_CTRL_SAMPLE_MASK  (0x3u << QSPI_CTRL_SAMPLE_OFFSET)
#define QSPI_CTRL_SAMPLE_SCK   (0x0u << QSPI_CTRL_SAMPLE_OFFSET)
#define QSPI_CTRL_SAMPLE_HCLKF (0x1u << QSPI_CTRL_SAMPLE_OFFSET)
#define QSPI_CTRL_SAMPLE_HCLKR (0x2u << QSPI_CTRL_SAMPLE_OFFSET)
#define QSPI_CTRL_QMODE0       (1u << QSPI_CTRL_QMODE0_OFFSET)
#define QSPI_CTRL_QMODE12_MASK (0x3u << QSPI_CTRL_QMODE12_OFFSET)
#define QSPI_CTRL_CLKRATE_MASK (0xFu << QSPI_CTRL_CLKRATE_OFFSET)

/* QSPI Frames Register Bits */
#define QSPI_FRAMES_TOTALBYTES_OFFSET 0
#define QSPI_FRAMES_CMDBYTES_OFFSET   16
#define QSPI_FRAMES_QSPI_OFFSET       25
#define QSPI_FRAMES_IDLE_OFFSET       26
#define QSPI_FRAMES_FBYTE_OFFSET      30
#define QSPI_FRAMES_FWORD_OFFSET      31

#define QSPI_FRAMES_TOTALBYTES_MASK (0xFFFFu << QSPI_FRAMES_TOTALBYTES_OFFSET)
#define QSPI_FRAMES_CMDBYTES_MASK   (0x1FFu << QSPI_FRAMES_CMDBYTES_OFFSET)
#define QSPI_FRAMES_QSPI            (1u << QSPI_FRAMES_QSPI_OFFSET)
#define QSPI_FRAMES_IDLE_MASK       (0xFu << QSPI_FRAMES_IDLE_OFFSET)

/* QSPI Status Register Bits */
#define QSPI_STATUS_TXDONE  (1u << 0)
#define QSPI_STATUS_RXDONE  (1u << 1)
#define QSPI_STATUS_RXAVAIL (1u << 2)
#define QSPI_STATUS_TXAVAIL (1u << 3)
#define QSPI_STATUS_RXEMPTY (1u << 4)
/* Bit 5 is reserved in CoreQSPI v2 */
#define QSPI_STATUS_READY   (1u << 7)
#define QSPI_STATUS_FLAGSX4 (1u << 8)

/* QSPI Clock Configuration */
#define QSPI_CLK_DIV_2  0x01u
#define QSPI_CLK_DIV_4  0x02u
#define QSPI_CLK_DIV_6  0x03u
#define QSPI_CLK_DIV_8  0x04u
#define QSPI_CLK_DIV_10 0x05u
#define QSPI_CLK_DIV_12 0x06u
#define QSPI_CLK_DIV_30 0x0Fu  /* Conservative: ~5MHz from 150MHz APB */

/* QSPI SPI Modes */
#define QSPI_SPI_MODE0 0  /* CPOL=0, CPHA=0 */
#define QSPI_SPI_MODE3 1  /* CPOL=1, CPHA=1 */

/* QSPI IO Formats */
#define QSPI_IO_FORMAT_NORMAL    0  /* 1-bit SPI */
#define QSPI_IO_FORMAT_DUAL_EX0  1  /* 2-bit with extended mode 0 */
#define QSPI_IO_FORMAT_QUAD_EX0  2  /* 4-bit with extended mode 0 */
#define QSPI_IO_FORMAT_DUAL_EX1  3  /* 2-bit with extended mode 1 */
#define QSPI_IO_FORMAT_QUAD_EX1  4  /* 4-bit with extended mode 1 */
#define QSPI_IO_FORMAT_DUAL_FULL 5  /* Full 2-bit mode */
#define QSPI_IO_FORMAT_QUAD_FULL 6  /* Full 4-bit mode */

/* Micron MT25QL01G Flash Commands */
#define QSPI_CMD_READ_ID_OPCODE         0x9Fu   /* JEDEC ID Read */
#define QSPI_CMD_MIO_READ_ID_OPCODE     0xAFu   /* Multiple IO Read ID */
#define QSPI_CMD_READ_STATUS_OPCODE     0x05u   /* Read Status Register */
#define QSPI_CMD_WRITE_ENABLE_OPCODE    0x06u   /* Write Enable */
#define QSPI_CMD_WRITE_DISABLE_OPCODE   0x04u   /* Write Disable */
#define QSPI_CMD_4BYTE_READ_OPCODE      0x13u   /* 4-byte address read */
#define QSPI_CMD_4BYTE_FAST_READ_OPCODE 0x0Cu   /* 4-byte fast read */
#define QSPI_CMD_4BYTE_QUAD_READ_OPCODE 0xECu   /* 4-byte quad I/O read */
#define QSPI_CMD_4BYTE_PAGE_PROG_OPCODE 0x12u   /* 4-byte page program */
#define QSPI_CMD_4BYTE_SECTOR_ERASE     0xDCu   /* 4-byte 64KB sector erase */
#define QSPI_CMD_ENTER_4BYTE_MODE       0xB7u   /* Enter 4-byte address mode */
#define QSPI_CMD_EXIT_4BYTE_MODE        0xE9u   /* Exit 4-byte address mode */

/* Flash Geometry - Micron MT25QL01GBBB (128MB) */
#ifndef FLASH_DEVICE_SIZE
#define FLASH_DEVICE_SIZE   (128 * 1024 * 1024)  /* 128MB (1Gb) */
#endif

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE     256                   /* 256 bytes */
#endif

#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE   (64 * 1024)          /* 64KB sectors */
#endif

/* QSPI Transfer Modes */
#define QSPI_MODE_WRITE     0
#define QSPI_MODE_READ      1

/* I/O fence: ensure MMIO store reaches the peripheral before the next read.
 * Required on RISC-V RVWMO to prevent stale TXAVAIL reads after TX writes. */
#define QSPI_IO_FENCE() __asm__ __volatile__("fence iorw, iorw" ::: "memory")

/* Function declarations for QSPI (when EXT_FLASH enabled) */
#ifndef __ASSEMBLER__
int qspi_init(void);
#endif /* __ASSEMBLER__ */

#endif /* EXT_FLASH */


#endif /* MPFS250_DEF_INCLUDED */
