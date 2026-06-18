/* mpfs250.h
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#ifndef MPFS250_DEF_INCLUDED
#define MPFS250_DEF_INCLUDED

/* Generic RISC-V definitions are included at the end of this file
 * (after PLIC_BASE is defined) to enable PLIC function declarations */

/* PolarFire SoC MPFS250T board specific configuration */

/* APB/AHB Peripheral Bus Clock Frequency (used for UART baud rate divisor)
 * M-mode (out of reset, no PLL): 40 MHz
 * S-mode (after HSS configures PLL): 150 MHz
 */
#ifndef MSS_APB_AHB_CLK
    #ifdef WOLFBOOT_RISCV_MMODE
        #define MSS_APB_AHB_CLK    40000000
    #else
        #define MSS_APB_AHB_CLK    150000000
    #endif
#endif

/* CPU Core Clock Frequency (used for mcycle-based benchmarking)
 * The E51 core runs at 2x the APB bus clock on reset.
 * After HSS configures the PLL, CPU runs at 600 MHz. */
#ifndef MSS_CPU_CLK
    #ifdef WOLFBOOT_RISCV_MMODE
        #define MSS_CPU_CLK         (MSS_APB_AHB_CLK * 2)
    #else
        #define MSS_CPU_CLK         600000000
    #endif
#endif

/* E51 reset clock in MHz, used to seed mpfs_cpu_freq_mhz for udelay().
 * Bumped to the Libero PLL rate after mss_pll_init() (see hal/mpfs250_ddr.c). */
#ifndef MPFS_CPU_FREQ_RESET_MHZ
#define MPFS_CPU_FREQ_RESET_MHZ     80U
#endif

/* Full-DDRC-reinit attempts in hal_init() (per-attempt failure rate ~30%). */
#ifndef MPFS_DDR_MAX_OUTER_RETRY
#define MPFS_DDR_MAX_OUTER_RETRY    6U
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
#define SYSREG_SOFT_RESET_CR_QSPI  (1U << 19)

/* eNVM Control Register (offset 0xB8).  Bits [5:0] set the AHB-to-eNVM
 * clock divider (period = (value+1) * AHB period); bit 6 (clock-okay)
 * latches once a new divider has taken effect.  Must be reprogrammed for
 * the faster AHB clock BEFORE the MSS PLL mux switch (HSS does this in
 * mss_mux_post_mss_pll_config with LIBERO_SETTING_MSS_ENVM_CR). */
#define SYSREG_ENVM_CR (*((volatile uint32_t*)(SYSREG_BASE + 0xB8)))
#define SYSREG_ENVM_CR_CLOCK_OKAY (1U << 6)

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

/* MSS Watchdog Timer (per-hart) */
#define MSS_WDT_E51_BASE       0x20001000UL
#define MSS_WDT_U54_1_BASE     0x20101000UL
#define MSS_WDT_U54_2_BASE     0x20103000UL
#define MSS_WDT_U54_3_BASE     0x20105000UL
#define MSS_WDT_U54_4_BASE     0x20107000UL
#define MSS_WDT_REFRESH(base)  (*(volatile uint32_t*)((base) + 0x00))
#define MSS_WDT_CONTROL(base)  (*(volatile uint32_t*)((base) + 0x04))
#define MSS_WDT_STATUS(base)   (*(volatile uint32_t*)((base) + 0x08))
#define MSS_WDT_TIME(base)     (*(volatile uint32_t*)((base) + 0x0C))
#define MSS_WDT_MVRP(base)     (*(volatile uint32_t*)((base) + 0x10))
#define MSS_WDT_CTRL_ENABLE    (1U << 0)

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
#define MMUART_MM0(base) *((volatile uint8_t*)((base)) + 0x30) /* Mode register 0 */
#define MMUART_MM1(base) *((volatile uint8_t*)((base)) + 0x34) /* Mode register 1 */
#define MMUART_MM2(base) *((volatile uint8_t*)((base)) + 0x38) /* Mode register 2 */
#define MMUART_DFR(base) *((volatile uint8_t*)((base)) + 0x3C) /* Fractional divisor register */
#define MMUART_GFR(base) *((volatile uint8_t*)((base)) + 0x44) /* Global filter register */
#define MMUART_TTG(base) *((volatile uint8_t*)((base)) + 0x48) /* TX time guard register */
#define MMUART_RTO(base) *((volatile uint8_t*)((base)) + 0x4C) /* RX timeout register */
#define MMUART_DLR(base) *((volatile uint8_t*)((base)) + 0x80) /* Divisor latch LSB */
#define MMUART_DMR(base) *((volatile uint8_t*)((base)) + 0x84) /* Divisor latch MSB */
#define MMUART_THR(base) *((volatile uint8_t*)((base)) + 0x100) /* Transmitter holding register */
#define MMUART_FCR(base) *((volatile uint8_t*)((base)) + 0x104) /* FIFO control register */


/* LCR (Line Control Register) */
#define MSS_UART_DATA_8_BITS        ((uint8_t)0x03)
#define MSS_UART_NO_PARITY          ((uint8_t)0x00)
#define MSS_UART_ONE_STOP_BIT       ((uint8_t)0x00)

/* LSR (Line Status Register) */
#define MSS_UART_DR                 ((uint8_t)0x01)    /* Data ready */
#define MSS_UART_THRE               ((uint8_t)0x20)    /* Transmitter holding register empty */
#define MSS_UART_TEMT               ((uint8_t)0x40)    /* Transmitter empty (FIFO + shift register) */

#define ELIN_MASK            (1U << 3) /* Enable LIN header detection */
#define EIRD_MASK            (1U << 2) /* Enable IrDA modem */
#define EERR_MASK            (1U << 0) /* Enable ERR / NACK during stop time */

#define RXRDY_TXRDYN_EN_MASK (1U << 0) /* Enable TXRDY and RXRDY signals */
#define CLEAR_RX_FIFO_MASK   (1U << 1) /* Clear receiver FIFO */
#define CLEAR_TX_FIFO_MASK   (1U << 2) /* Clear transmitter FIFO */

#define RTS_MASK             (1U << 1) /* Request To Send */
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

/* Crypto Engine: Athena F5200 (200 MHz) */
#define ATHENA_BASE (SYSREG_BASE + 0x125000)


/* L2 Cache Controller (CACHE_CTRL @ 0x02010000) */
#define L2_CACHE_BASE               0x02010000UL

#define L2_CONFIG                   (*(volatile uint64_t*)(L2_CACHE_BASE + 0x000))
#define L2_WAY_ENABLE               (*(volatile uint64_t*)(L2_CACHE_BASE + 0x008))
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
#define L2_SCRATCH_SIZE             (L2_NUM_SCRATCH_WAYS * L2_WAY_BYTE_LENGTH)

#define L2_WAY_ENABLE_RESET         0x01
#define L2_WAY_ENABLE_ALL_CACHE     0xFF
#define L2_WAY_ENABLE_WITH_SCRATCH  0x0FFF
#define L2_WAY_MASK_CACHE_ONLY      0xFF


/* CLINT - Core Local Interruptor */
#ifndef CLINT_BASE
#define CLINT_BASE                  0x02000000UL
#endif

/* RTC Clock Frequency (1 MHz after divisor) */
#define RTC_CLOCK_FREQ              1000000UL

/* In M-mode CLINT MTIME is not running without HSS; use mcycle (CPU clock) instead.
 * In S-mode MTIME runs at 1 MHz (default RISCV_SMODE_TIMER_FREQ). */
#if defined(WOLFBOOT_RISCV_MMODE) && !defined(RISCV_SMODE_TIMER_FREQ)
#define RISCV_SMODE_TIMER_FREQ      MSS_CPU_CLK
#endif


/* Hart Local Storage (HLS) - per-hart communication structure, 64 bytes at top of stack */
#define HLS_DEBUG_AREA_SIZE         64

#ifndef __ASSEMBLER__
typedef struct {
    volatile uint32_t in_wfi_indicator;  /* 0x00: Hart status indicator */
    volatile uint32_t my_hart_id;        /* 0x04: Hart ID */
    volatile uint32_t shared_mem_marker; /* 0x08: Init marker */
    volatile uint32_t shared_mem_status; /* 0x0C: Status */
    volatile uint64_t* shared_mem;       /* 0x10: Shared memory pointer */
    volatile uint64_t reserved[5];       /* 0x18: Reserved/padding to 64 bytes */
} HLS_DATA;  /* 64 bytes total */
#endif /* __ASSEMBLER__ */

#define HLS_MAIN_HART_STARTED       0x12344321UL
#define HLS_OTHER_HART_IN_WFI       0x12345678UL

/* DTIM address of the E51 "main hart started" gate flag polled by the
 * parked secondary harts in boot_riscv_start.S.  This must NOT live in
 * L2-scratch (the legacy HLS location): cacheable stores to the
 * scratchpad can be silently lost on dirty-line eviction, so whether the
 * secondaries ever saw the flag depended on the image's cache-line
 * layout (observed: a 304-byte text shift left harts 2-4 stuck in the
 * eNVM gate until the kernel's HSM hart_start IPI, missing the kernel's
 * 1s online window).  The E51 DTIM is uncached and coherent for every
 * hart.  Keep clear of the SBI shared block (DTIM+0x00, src/riscv_sbi.c)
 * and the hart-start mailboxes (DTIM+0x100, hal/mpfs250.c).
 * No UL suffix: also used from assembly (boot_riscv_start.S). */
#define MPFS_DTIM_MAIN_STARTED_ADDR 0x010000F0

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
void secondary_hart_entry(unsigned long hartid, HLS_DATA* hls);
#endif
#endif /* __ASSEMBLER__ */



/* PLIC - Platform-Level Interrupt Controller (base 0x0C000000, 64MB) */
#define PLIC_BASE               0x0C000000UL
#define PLIC_SIZE               0x04000000UL
#define PLIC_NUM_SOURCES        186
#define PLIC_NUM_HARTS          5
#define PLIC_NUM_CONTEXTS       10
#define OFFSET_TO_MSS_GLOBAL_INTS   13

#define PLIC_INT_MMC_MAIN       88


/* ============================================================================
 * DDR Controller and PHY (LPDDR4) - Video Kit MPFS250T
 *
 * MPFS DDR subsystem consists of:
 * - DDR Controller (DDRCFG_BASE @ 0x20080000) - timing, addressing, refresh
 * - DDR PHY (CFG_DDR_SGMII_PHY @ 0x20007000) - physical interface, training
 * - Segment registers for address translation
 * - SCB PLLs for clock generation
 *
 * Video Kit memory: Micron MT53D512M32D2DS-053 LPDDR4 (2GB, x32 @ 1600 Mbps)
 * ============================================================================ */

/* SCB Configuration Block (SCBCFG @ 0x37080000) */
#define SCBCFG_BASE                 0x37080000UL
#define SCBCFG_TIMER                (*(volatile uint32_t*)(SCBCFG_BASE + 0x08))
#define MSS_SCB_ACCESS_CONFIG       0x0008A080UL

/* DDR SGMII PHY Configuration (CFG_DDR_SGMII_PHY @ 0x20007000) */
#define CFG_DDR_SGMII_PHY_BASE      0x20007000UL
#define DDRPHY_STARTUP              (*(volatile uint32_t*)(CFG_DDR_SGMII_PHY_BASE + 0x008))
#define DDRPHY_DYN_CNTL             (*(volatile uint32_t*)(CFG_DDR_SGMII_PHY_BASE + 0xC1C))
#define DDRPHY_STARTUP_CONFIG       0x003F1F00UL
#define DDRPHY_DYN_CNTL_CONFIG      0x0000047FUL

/* DFI APB interface control (enables DDR PHY APB access) */
#define SYSREG_DFIAPB_CR            (*(volatile uint32_t*)(SYSREG_BASE + 0x98))

/* L2 cache flush registers (used at boot for DDR coherency) */
#define L2_FLUSH64                  (*(volatile uint64_t*)(L2_CACHE_BASE + 0x200))
#define L2_FLUSH32                  (*(volatile uint32_t*)(L2_CACHE_BASE + 0x240))

/* DDR Base Addresses */
#define SYSREGSCB_BASE              0x20003000UL
#define DDRCFG_BASE                 0x20080000UL  /* DDR Controller CSR APB */
#define DDR_SEG_BASE                0x20005D00UL  /* From HSS mss_seg.h */

/* SCB PLL Bases */
#define SCB_MSS_PLL_BASE            0x3E001000UL
#define SCB_DDR_PLL_BASE            0x3E010000UL

/* Clock Fabric Mux bases */
#define SCB_CFM_MSS_BASE            0x3E002000UL
#define SCB_CFM_SGMII_BASE          0x3E200000UL

/* DDR Bank Controller (NV map reset during VREF training) */
#define SCB_BANKCONT_DDR_BASE       0x3E020000UL

/* Register Access Macros */
#define SYSREG_REG(off)     (*(volatile uint32_t*)(SYSREG_BASE + (off)))
#define SYSREGSCB_REG(off)  (*(volatile uint32_t*)(SYSREGSCB_BASE + (off)))
#define DDRCFG_REG(off)     (*(volatile uint32_t*)(DDRCFG_BASE + (off)))
#define DDRPHY_REG(off)     (*(volatile uint32_t*)(CFG_DDR_SGMII_PHY_BASE + (off)))
#define DDR_BANKCONT_REG(off) (*(volatile uint32_t*)(SCB_BANKCONT_DDR_BASE + (off)))
#define DDR_SEG_REG(off)    (*(volatile uint32_t*)(DDR_SEG_BASE + (off)))
#define SCBCFG_REG(off)     (*(volatile uint32_t*)(SCBCFG_BASE + (off)))
#define MSS_PLL_REG(off)    (*(volatile uint32_t*)(SCB_MSS_PLL_BASE + (off)))
#define DDR_PLL_REG(off)    (*(volatile uint32_t*)(SCB_DDR_PLL_BASE + (off)))
#define CFM_MSS_REG(off)    (*(volatile uint32_t*)(SCB_CFM_MSS_BASE + (off)))
#define CFM_SGMII_REG(off)  (*(volatile uint32_t*)(SCB_CFM_SGMII_BASE + (off)))

/* SYSREG Offsets */
#define SYSREG_SUBBLK_CLOCK_CR_OFF  0x84
#define SYSREG_SOFT_RESET_CR_OFF    0x88
#define SYSREG_DFIAPB_CR_OFF        0x98
#define MSSIO_CONTROL_CR_OFF        0x1BC

/* PLL Register Offsets */
#define PLL_SOFT_RESET              0x000
#define PLL_CTRL                    0x004
#define PLL_REF_FB                  0x008
#define PLL_FRACN                   0x00C
#define PLL_DIV_0_1                 0x010
#define PLL_DIV_2_3                 0x014
#define PLL_CTRL2                   0x018
#define PLL_PHADJ                   0x020
#define PLL_SSCG_0                  0x024
#define PLL_SSCG_1                  0x028
#define PLL_SSCG_2                  0x02C
#define PLL_SSCG_3                  0x030

/* PLL Control Bits */
#define PLL_POWERDOWN_B             (1UL << 0)
#define PLL_LOCK_BIT                (1UL << 25)
#define PLL_INIT_OUT_RESET          0x00000003UL

/* CFM Register Offsets */
#define CFM_BCLKMUX                 0x004
#define CFM_PLL_CKMUX               0x008
#define CFM_MSSCLKMUX               0x00C
#define CFM_FMETER_ADDR             0x014
#define CFM_FMETER_DATAW            0x018

/* SGMII CFM Register Offsets (at SCB_CFM_SGMII_BASE 0x3E200000) */
#define CFM_SGMII_SOFT_RESET        0x000
#define CFM_SGMII_RFCKMUX           0x004   /* Routes refclk to DDR/SGMII PLLs */
#define CFM_SGMII_SGMII_CLKMUX      0x008
#define CFM_SGMII_SPARE0            0x00C
#define CFM_SGMII_CLK_XCVR          0x010

/* DDR PHY Register Offsets */
#define PHY_SOFT_RESET              0x000
#define PHY_MODE                    0x004
#define PHY_STARTUP                 0x008
#define PHY_PLL_CTRL_MAIN           0x084
#define PHY_DPC_BITS                0x184
#define PHY_BANK_STATUS             0x188
#define PHY_IOC_REG0                0x204
#define PHY_IOC_REG1                0x208
#define PHY_IOC_REG2                0x20C
#define PHY_IOC_REG3                0x210
#define PHY_IOC_REG6                0x21C   /* Calibration reset/clock divider */
#define PHY_DYN_CNTL                0xC1C
#define PHY_SOFT_RESET_TIP          0x800
#define PHY_RANK_SELECT             0x804
#define PHY_LANE_SELECT             0x808  /* was wrongly named PHY_BCLK_SCLK */
/* The real BCLK/SCLK training answer is at 0x870 (bclksclk_answer).
 * Old PHY_BCLK_SCLK at 0x808 was lane_select, so any printf of it
 * was meaningless. */
#define PHY_BCLKSCLK_ANSWER         0x870
#define PHY_TRAINING_SKIP           0x80C
#define PHY_TRAINING_START          0x810
#define PHY_TRAINING_STATUS         0x814
#define PHY_TRAINING_RESET          0x818
#define PHY_TIP_CFG                 0x828
#define PHY_TIP_CFG_PARAMS          0x8D0
#define PHY_EXPERT_MODE_EN          0x878
#define PHY_EXPERT_DLYCNT_MOVE0     0x87C
#define PHY_EXPERT_DLYCNT_MOVE1     0x880
#define PHY_EXPERT_DLYCNT_DIRECTION0 0x884
#define PHY_EXPERT_DLYCNT_DIR1      0x888
#define PHY_EXPERT_DLYCNT_LOAD0     0x88C
#define PHY_EXPERT_DLYCNT_LOAD1     0x890
#define PHY_EXPERT_DFI_STATUS_TO_SHIM 0x8CC
#define PHY_LANE_ALIGN_FIFO_CTRL    0x8D8
#define PHY_EXPERT_MV_RD_DLY        0x89C
#define PHY_EXPERT_DLYCNT_PAUSE     0x8A0
#define PHY_EXPERT_PLLCNT           0x8A4
#define PHY_EXPERT_DQ_READBACK      0x8A8
#define PHY_EXPERT_ADDCMD_READBACK  0x8AC  /* Bits 13:12 = rx_bclksclk, 3:0 = rx_ck */
/* 0x8B0 is expert_read_gate_controls, NOT a DFI status register.
 * Previous PHY_EXPERT_DFI_STATUS define here pointed writes
 * (0x6/0x4/0x0 for DQ/DQS output delay setup) at the wrong register.
 * The correct register is PHY_EXPERT_DFI_STATUS_TO_SHIM at 0x8CC. */
#define PHY_EXPERT_READ_GATE_CONTROLS 0x8B0
#define PHY_EXPERT_WRCALIB          0x8BC
#define PHY_RPC95_IBUFMD_ADDCMD     0x57C   /* LPDDR4 Input Buffer Mode - ADDCMD */
#define PHY_RPC96_IBUFMD_CLK        0x580   /* LPDDR4 Input Buffer Mode - CLK */
#define PHY_RPC97_IBUFMD_DQ         0x584   /* LPDDR4 Input Buffer Mode - DQ */
#define PHY_RPC98_IBUFMD_DQS        0x588   /* LPDDR4 Input Buffer Mode - DQS */
#define PHY_RPC145                  0x644   /* ADDCMD delay offset (A9 loopback) */
#define PHY_RPC147                  0x64C   /* DDR clock loopback delay */
#define PHY_RPC156                  0x670
#define PHY_RPC166                  0x698
#define PHY_RPC168                  0x6A0   /* RX_MD_CLKN for LPDDR4 training */
#define PHY_RPC220                  0x770

/* ODT (On-Die Termination) RPC registers */
#define PHY_RPC1_ODT                0x384   /* ODT_CA */
#define PHY_RPC2_ODT                0x388   /* ODT_CLK */
#define PHY_RPC3_ODT                0x38C   /* ODT_DQ (0 for WRLVL, 3 normally) */
#define PHY_RPC4_ODT                0x390   /* ODT_DQS */

/* PVT calibration bits */
#define PVT_CALIB_START             (1U << 0)
#define PVT_CALIB_LOCK              (1U << 14)
#define PVT_CALIB_STATUS            (1U << 2)
#define PVT_IOEN_OUT                (1U << 4)

/* IOSCB IO Calibration DDR base (SCB space for PVT calibration) */
#define IOSCB_IO_CALIB_DDR_BASE     0x3E040000UL
#define IOSCB_IO_CALIB_DDR_REG(off) (*(volatile uint32_t*)(IOSCB_IO_CALIB_DDR_BASE + (off)))
#define IOSCB_SOFT_RESET            0x000
#define IOSCB_IOC_REG0              0x004
#define IOSCB_IOC_REG1              0x008


/* DDR Segment Register Offsets.
 * SEG is a 256-byte-stride peripheral pair (mss_seg.h:54): seg_t has
 * 8 x u32 control regs + 56 x u32 fill = 256 B.  SEG[0] is at base
 * (DDR_SEG_BASE = 0x20005D00); SEG[1] is at base + 0x100 (= 0x20005E00).
 * Phase 3.10.3 (A) finding: wolfBoot previously put SEG1_X at offset
 * 0x20-0x3C (overwriting unrelated registers), so the SEG1 cached/
 * non-cached DDR address-mapping registers were never written --
 * 0x80000000 stores faulted with cause=7 (store access fault). */
#define SEG0_0                      0x00
#define SEG0_1                      0x04
#define SEG0_2                      0x08
#define SEG0_3                      0x0C
#define SEG0_4                      0x10
#define SEG0_5                      0x14
#define SEG0_6                      0x18
#define SEG0_BLOCKER                0x1C
#define SEG1_0                      0x100
#define SEG1_1                      0x104
#define SEG1_2                      0x108
#define SEG1_3                      0x10C
#define SEG1_4                      0x110
#define SEG1_5                      0x114
#define SEG1_6                      0x118
#define SEG1_7                      0x11C

/* DDR Memory Map */
#define DDR_BASE_CACHED             0x80000000UL   /* Cached access */
#define DDR_BASE_NONCACHED          0xC0000000UL   /* Non-cached access */
#define DDR_BASE_NONCACHED_WCB      0xD0000000UL   /* Non-cached with write-combining */
#define DDR_SIZE                    0x80000000UL   /* 2GB (Video Kit) */

/* DDR Init return codes */
#define DDR_INIT_SUCCESS            0
#define DDR_INIT_TIMEOUT            -1
#define DDR_INIT_TRAINING_FAIL      -2
#define DDR_INIT_MEM_TEST_FAIL      -3


/* ============================================================================
 * Video Kit Clock/DDR Configuration
 *
 * The LIBERO_SETTING_* values come from a Libero/HSS-generated
 * fpga_design_config.h for the target board.  Set LIBERO_FPGA_CONFIG_DIR
 * at build time to point at the directory containing fpga_design_config.h
 * (see arch.mk and the polarfire_mpfs250_m.config example).
 * ============================================================================ */
#ifdef MPFS_DDR_INIT
#include "fpga_design_config.h"
#endif

/* DDR function declarations */
#ifndef __ASSEMBLER__
#ifdef WOLFBOOT_RISCV_MMODE
int mpfs_ddr_init(unsigned int outer_retry);
void hal_uart_reinit(void);

/* Verbose DDR/PLL/PHY/training trace (build with -DDEBUG_DDR).  Defined in
 * the header so both hal/mpfs250.c (WDT/L2/text-verify dumps) and
 * hal/mpfs250_ddr.c (the DDR driver) can use it. */
#ifdef DEBUG_DDR
#   define DBG_DDR(_f_, ...) wolfBoot_printf(_f_, ##__VA_ARGS__)
#else
#   define DBG_DDR(_f_, ...) do { } while (0)
#endif

/* Full system memory barrier (AXI/peripheral ordering). */
static inline void mb(void)
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/* mcycle-based microsecond busy delay.  Defined in hal/mpfs250.c (it must
 * exist in non-DDR M-mode builds too); read from hal/mpfs250_ddr.c. */
void udelay(uint32_t us);

/* CPU/APB clock rates, defined in hal/mpfs250.c (read by udelay() and the
 * UART baud divisor in every M-mode build) and updated by mss_pll_init() in
 * hal/mpfs250_ddr.c when the MSS PLL is raised. */
extern uint32_t mpfs_cpu_freq_mhz;
extern uint32_t mpfs_apb_clk_hz;

#ifdef MPFS_DDR_INIT
/* MSSIO IOMUX setup: defined in hal/mpfs250.c (alongside the SDHCI platform
 * helpers) but called from nwc_init() in hal/mpfs250_ddr.c. */
void mpfs_iomux_init(void);
#endif

/* PDMA-based memcpy: src must be CPU-readable (L2 Scratch / LIM / DDR
 * already loaded), dst is the destination AXI address.  When dst is in
 * the cached DDR window (top 4 bits = 0x8) the helper rewrites it to
 * the non-cached alias (top 4 bits = 0xC) before kicking PDMA, since
 * PDMA-via-non-cached is the only AXI write path that consistently
 * lands in DDR on this board.  Returns 0 on success, non-zero on
 * timeout. */
int mpfs_pdma_memcpy(void *dst, const void *src, uint32_t bytes);
#endif
#endif /* __ASSEMBLER__ */


#ifdef EXT_FLASH
/* QSPI Flash Controller
 *
 * Two CoreQSPI v2 controllers with identical register layouts:
 *   SC QSPI  (MPFS_SC_SPI=1, default): 0x37020100 -- fabric-connected flash
 *   MSS QSPI (MPFS_SC_SPI=0):          0x21000000 -- MSS QSPI pins
 */

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
#define QSPI_CTRL_SAMPLE_SCK   (0x0u << QSPI_CTRL_SAMPLE_OFFSET)

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
#define QSPI_STATUS_READY   (1u << 7)

/* QSPI Clock Configuration */
#define QSPI_CLK_DIV_2  0x01u
#define QSPI_CLK_DIV_4  0x02u
#define QSPI_CLK_DIV_6  0x03u
#define QSPI_CLK_DIV_8  0x04u
#define QSPI_CLK_DIV_10 0x05u
#define QSPI_CLK_DIV_12 0x06u
#define QSPI_CLK_DIV_30 0x0Fu  /* ~5 MHz from 150 MHz APB */

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
