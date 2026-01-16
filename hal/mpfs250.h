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

/* Sub-block Clock Control Register (enables peripheral clocks) */
#define SYSREG_SUBBLK_CLOCK_CR (*((volatile uint32_t*)(SYSREG_BASE + 0x84)))

/* Peripheral Soft Reset Control Register */
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

/* Device serial number size in bytes */
#define DEVICE_SERIAL_NUMBER_SIZE 16

/* System Controller register access */
#define SCBCTRL_REG(off) (*((volatile uint32_t*)(SCBCTRL_BASE + (off))))
#define SCBMBOX_REG(off) (*((volatile uint32_t*)(SCBMBOX_BASE + (off))))
#define SCBMBOX_BYTE(off) (*((volatile uint8_t*)(SCBMBOX_BASE + (off))))


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
#ifdef WOLFBOOT_RISCV_MMODE
int mpfs_wake_secondary_harts(void);
void secondary_hart_entry(unsigned long hartid, HLS_DATA* hls);
void uart_init_hart(unsigned long hartid);
void uart_write_hart(unsigned long hartid, const char* buf, unsigned int sz);
#endif
#endif /* __ASSEMBLER__ */


/* ============================================================================
 * DDR Controller and PHY Configuration
 *
 * MPFS DDR subsystem consists of:
 * - DDR Controller (DDRCFG_BASE @ 0x20080000) - timing, addressing, refresh
 * - DDR PHY (CFG_DDR_SGMII_PHY @ 0x20007000) - physical interface, training
 * - Segment registers for address translation
 * - SCB PLLs for clock generation
 *
 * Video Kit: 4x MT40A512M16LY-075:E (LPDDR4, 2GB total)
 * ============================================================================ */

/* DDR Base Addresses */
#define SYSREGSCB_BASE              0x20003000UL
#define DDRCFG_BASE                 0x20080000UL  /* DDR Controller CSR APB (from HSS) */
#define DDR_SEG_BASE                0x20005D00UL  /* From HSS mss_seg.h */

/* SCB PLL Bases */
#define SCB_MSS_PLL_BASE            0x3E001000UL
#define SCB_DDR_PLL_BASE            0x3E010000UL

/* Clock Fabric Mux bases */
#define SCB_CFM_MSS_BASE            0x3E002000UL
#define SCB_CFM_SGMII_BASE          0x3E200000UL

/* DDR Bank Controller (for NV map reset during VREF training) */
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
#define CFM_SGMII_SOFT_RESET        0x000   /* Soft reset */
#define CFM_SGMII_RFCKMUX           0x004   /* Reference clock mux - routes refclk to DDR/SGMII PLLs */
#define CFM_SGMII_SGMII_CLKMUX      0x008   /* SGMII clock mux */
#define CFM_SGMII_SPARE0            0x00C   /* Spare register */
#define CFM_SGMII_CLK_XCVR          0x010   /* Clock receiver config */

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
#define PHY_DYN_CNTL                0xC1C   /* Correct offset from HSS mss_ddr_sgmii_phy_defs.h */
#define PHY_BCLK_SCLK               0x808
#define PHY_TRAINING_SKIP           0x80C
#define PHY_TRAINING_START          0x810
#define PHY_TRAINING_STATUS         0x814
#define PHY_TRAINING_RESET          0x818
#define PHY_TIP_CFG                 0x828
#define PHY_TIP_CFG_PARAMS          0x8D0   /* TIP configuration parameters */
#define PHY_EXPERT_MODE_EN          0x878
/* Expert delay control registers - corrected offsets from HSS */
#define PHY_EXPERT_DLYCNT_MOVE0     0x87C   /* Delay count move reg0 */
#define PHY_EXPERT_DLYCNT_MOVE1     0x880   /* Delay count move reg1 (CA training) */
#define PHY_EXPERT_DLYCNT_DIRECTION0 0x884  /* Delay direction reg0 */
#define PHY_EXPERT_DLYCNT_DIR1      0x888   /* Delay direction reg1 */
#define PHY_EXPERT_DLYCNT_LOAD0     0x88C   /* Delay load reg0 */
#define PHY_EXPERT_DLYCNT_LOAD1     0x890   /* Delay load reg1 */
#define PHY_EXPERT_DFI_STATUS_TO_SHIM 0x8CC  /* DFI status override to shim */
#define PHY_LANE_ALIGN_FIFO_CTRL    0x8D8   /* Lane alignment FIFO control */
#define PHY_EXPERT_MV_RD_DLY        0x89C
#define PHY_EXPERT_DLYCNT_PAUSE     0x8A0
#define PHY_EXPERT_PLLCNT           0x8A4
#define PHY_EXPERT_DQ_READBACK      0x8A8
#define PHY_EXPERT_ADDCMD_READBACK  0x8AC  /* Bits 13:12 = rx_bclksclk, 3:0 = rx_ck */
#define PHY_EXPERT_DFI_STATUS       0x8B0
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

/* LPDDR4 Input Buffer Mode settings (from Libero config) */
#define LIBERO_SETTING_RPC_IBUFMD_ADDCMD    0x00000003UL
#define LIBERO_SETTING_RPC_IBUFMD_CLK       0x00000004UL
#define LIBERO_SETTING_RPC_IBUFMD_DQ        0x00000003UL
#define LIBERO_SETTING_RPC_IBUFMD_DQS       0x00000004UL

/* ODT (On-Die Termination) RPC registers */
#define PHY_RPC1_ODT                0x384   /* ODT_CA - Command/Address */
#define PHY_RPC2_ODT                0x388   /* ODT_CLK - Clock */
#define PHY_RPC3_ODT                0x38C   /* ODT_DQ - Data (set to 0 for WRLVL, 3 normally) */
#define PHY_RPC4_ODT                0x390   /* ODT_DQS - Data Strobe */

/* PVT calibration bits */
#define PVT_CALIB_START             (1U << 0)
#define PVT_CALIB_LOCK              (1U << 14)  /* Bit 14 for calib_lock */
#define PVT_CALIB_STATUS            (1U << 2)   /* Bit 2 for sro_calib_status */
#define PVT_IOEN_OUT                (1U << 4)   /* Bit 4 for sro_ioen_out */

/* IOSCB IO Calibration DDR base (SCB space for PVT calibration) */
#define IOSCB_IO_CALIB_DDR_BASE     0x3E040000UL
#define IOSCB_IO_CALIB_DDR_REG(off) (*(volatile uint32_t*)(IOSCB_IO_CALIB_DDR_BASE + (off)))
#define IOSCB_SOFT_RESET            0x000   /* Soft reset register */
#define IOSCB_IOC_REG0              0x004   /* IOC_REG0 in SCB space */
#define IOSCB_IOC_REG1              0x008   /* IOC_REG1 in SCB space */

/* DDR Controller Register Offsets
 *
 * From HSS mss_ddr_sgmii_regs.h:
 * - MC_BASE2  @ DDRCFG_BASE + 0x4000 = 0x20084000 (all controller registers)
 * - DFI_BASE  @ DDRCFG_BASE + 0x10000 = 0x20090000 (DFI interface)
 *
 * All timing and init registers are in MC_BASE2.
 */

/* MC_BASE2 registers (DDRCFG_BASE + 0x4000) */
#define MC_BASE2                    0x4000
#define MC_CTRLR_SOFT_RESET         (MC_BASE2 + 0x00)
#define MC_AUTOINIT_DISABLE         (MC_BASE2 + 0x10)
#define MC_INIT_FORCE_RESET         (MC_BASE2 + 0x14)
#define MC_INIT_GEARDOWN_EN         (MC_BASE2 + 0x18)
#define MC_INIT_DISABLE_CKE         (MC_BASE2 + 0x1C)
#define MC_INIT_CS                  (MC_BASE2 + 0x20)
#define MC_INIT_PRECHARGE_ALL       (MC_BASE2 + 0x24)
#define MC_INIT_REFRESH             (MC_BASE2 + 0x28)
#define MC_INIT_ZQ_CAL_REQ          (MC_BASE2 + 0x2C)
#define MC_INIT_ACK                 (MC_BASE2 + 0x30)
#define MC_CFG_BL                   (MC_BASE2 + 0x34)
#define MC_CTRLR_INIT               (MC_BASE2 + 0x38)
#define MC_CTRLR_INIT_DONE          (MC_BASE2 + 0x3C)
#define MC_CFG_AUTO_REF_EN          (MC_BASE2 + 0x40)
#define MC_CFG_RAS                  (MC_BASE2 + 0x44)
#define MC_CFG_RCD                  (MC_BASE2 + 0x48)
#define MC_CFG_RRD                  (MC_BASE2 + 0x4C)
#define MC_CFG_RP                   (MC_BASE2 + 0x50)
#define MC_CFG_RC                   (MC_BASE2 + 0x54)
#define MC_CFG_FAW                  (MC_BASE2 + 0x58)
#define MC_CFG_RFC                  (MC_BASE2 + 0x5C)
#define MC_CFG_RTP                  (MC_BASE2 + 0x60)
#define MC_CFG_WR                   (MC_BASE2 + 0x64)
#define MC_CFG_WTR                  (MC_BASE2 + 0x68)
#define MC_CFG_CL                   (MC_BASE2 + 0x74)
#define MC_CFG_STARTUP_DELAY        (MC_BASE2 + 0x80)
#define MC_CFG_MEM_COLBITS          (MC_BASE2 + 0x84)
#define MC_CFG_MEM_ROWBITS          (MC_BASE2 + 0x88)
#define MC_CFG_MEM_BANKBITS         (MC_BASE2 + 0x8C)
#define MC_CFG_XP                   (MC_BASE2 + 0x9C)
#define MC_CFG_XSR                  (MC_BASE2 + 0xA0)
#define MC_CFG_MRD                  (MC_BASE2 + 0xA8)
#define MC_CFG_REF_PER              (MC_BASE2 + 0xB0)
#define MC_INIT_MR_W_REQ            (MC_BASE2 + 0x1F0)  /* MR write request (from HSS mss_ddr_sgmii_regs.h) */
#define MC_INIT_MR_ADDR             (MC_BASE2 + 0x1F4)  /* MR address */
#define MC_INIT_MR_WR_DATA          (MC_BASE2 + 0x1F8)  /* MR write data */
#define MC_INIT_MR_WR_MASK          (MC_BASE2 + 0x1FC)  /* MR write mask */
#define MC_INIT_ZQ_CAL_START        (MC_BASE2 + 0xDC)
#define MC_CFG_AUTO_ZQ_CAL_EN       (MC_BASE2 + 0xE0)
#define MC_CFG_CWL                  (MC_BASE2 + 0xF4)
#define MC_CFG_MEMORY_TYPE          (MC_BASE2 + 0x104)
#define MC_CFG_NUM_RANKS            (MC_BASE2 + 0x10C)
#define MC_CFG_WL                   (MC_BASE2 + 0x188)
#define MC_CFG_RL                   (MC_BASE2 + 0x18C)

/* DFI registers (DDRCFG_BASE + 0x10000) */
#define DFI_BASE                    0x10000
#define MC_DFI_RDDATA_EN            (DFI_BASE + 0x00)
#define MC_DFI_PHY_RDLAT            (DFI_BASE + 0x04)
#define MC_DFI_PHY_WRLAT            (DFI_BASE + 0x08)
#define MC_DFI_PHYUPD_EN            (DFI_BASE + 0x0C)
#define MC_DFI_INIT_COMPLETE        (DFI_BASE + 0x34)
#define MC_DFI_INIT_START           (DFI_BASE + 0x50)

/* Memory Test Controller (MTC) registers - at DDRCFG_BASE + 0x4400 */
#define MTC_BASE                    0x4400
#define MT_EN                       (MTC_BASE + 0x00)
#define MT_EN_SINGLE                (MTC_BASE + 0x04)
#define MT_STOP_ON_ERROR            (MTC_BASE + 0x08)
#define MT_DATA_PATTERN             (MTC_BASE + 0x14)
#define MT_ADDR_PATTERN             (MTC_BASE + 0x18)
#define MT_ADDR_BITS                (MTC_BASE + 0x20)
#define MT_ERROR_STS                (MTC_BASE + 0x24)
#define MT_DONE_ACK                 (MTC_BASE + 0x28)
#define MT_START_ADDR_0             (MTC_BASE + 0xB4)
#define MT_START_ADDR_1             (MTC_BASE + 0xB8)
#define MT_ERROR_MASK_0             (MTC_BASE + 0xBC)
#define MT_ERROR_MASK_1             (MTC_BASE + 0xC0)
#define MT_ERROR_MASK_2             (MTC_BASE + 0xC4)
#define MT_ERROR_MASK_3             (MTC_BASE + 0xC8)
#define MT_ERROR_MASK_4             (MTC_BASE + 0xCC)

/* PHY write calibration register */
#define PHY_EXPERT_WRCALIB          0x8BC

/* DDR Segment Register Offsets */
#define SEG0_0                      0x00
#define SEG0_1                      0x04
#define SEG0_2                      0x08
#define SEG0_3                      0x0C
#define SEG0_4                      0x10
#define SEG0_5                      0x14
#define SEG0_6                      0x18
#define SEG0_BLOCKER                0x1C
#define SEG1_0                      0x20
#define SEG1_1                      0x24
#define SEG1_2                      0x28
#define SEG1_3                      0x2C
#define SEG1_4                      0x30
#define SEG1_5                      0x34
#define SEG1_6                      0x38
#define SEG1_7                      0x3C

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
 * Video Kit Clock/DDR Configuration (Libero-generated settings)
 *
 * Reference: 125 MHz external oscillator
 * DDR: 4x MT40A512M16LY-075:E (LPDDR4, 2GB total)
 * ============================================================================ */

/* MSS PLL Configuration
 *
 * For 600 MHz CPU clock / 150 MHz APB clock:
 * - Reference clock: 125 MHz (from external oscillator)
 * - VCO: ~2.4 GHz
 * - REFDIV = 1 (must be non-zero!)
 * - FBDIV = 20 (2500 MHz / 125 MHz)
 */
#define LIBERO_SETTING_MSS_PLL_CTRL         0x0100001FUL
#define LIBERO_SETTING_MSS_PLL_REF_FB       0x00000500UL  /* RFDIV=5 (from HSS Video Kit config) */
#define LIBERO_SETTING_MSS_PLL_FRACN        0x00000000UL
#define LIBERO_SETTING_MSS_PLL_DIV_0_1      0x03000100UL  /* DIV0=3, DIV1=1 */
#define LIBERO_SETTING_MSS_PLL_DIV_2_3      0x01000300UL
#define LIBERO_SETTING_MSS_PLL_CTRL2        0x00001020UL
#define LIBERO_SETTING_MSS_PLL_PHADJ        0x00004003UL
#define LIBERO_SETTING_MSS_SSCG_REG_0       0x00000000UL
#define LIBERO_SETTING_MSS_SSCG_REG_1       0x00000000UL
#define LIBERO_SETTING_MSS_SSCG_REG_2       0x00000060UL
#define LIBERO_SETTING_MSS_SSCG_REG_3       0x00000001UL

/* MSS Clock Fabric Mux Configuration */
#define LIBERO_SETTING_MSS_BCLKMUX          0x00000208UL
#define LIBERO_SETTING_MSS_PLL_CKMUX        0x00000155UL
#define LIBERO_SETTING_MSS_MSSCLKMUX        0x00000003UL
#define LIBERO_SETTING_MSS_FMETER_ADDR      0x00000000UL
#define LIBERO_SETTING_MSS_FMETER_DATAW     0x00000000UL

/* SGMII Configuration (from Video Kit Libero design hw_sgmii_tip.h)
 * These are used by sgmii_off_mode() to properly configure the SGMII
 * RPC registers even when SGMII is not used (DDR-only mode).
 */
#define LIBERO_SETTING_SGMII_CLK_XCVR       0x00002C30UL  /* Clock receiver */
#define LIBERO_SETTING_SGMII_REFCLKMUX      0x00000005UL  /* Route refclk to DDR/SGMII PLLs */
#define LIBERO_SETTING_SGMII_SGMII_CLKMUX   0x00000000UL  /* SGMII clock mux (not used) */

/* SGMII TIP RPC registers (from hw_sgmii_tip.h) */
#define LIBERO_SETTING_SGMII_MODE           0x08C0F2FFUL  /* SGMII mode config */
#define LIBERO_SETTING_SGMII_PLL_CNTL       0x80140101UL  /* SGMII PLL control */
#define LIBERO_SETTING_SGMII_CH0_CNTL       0x37F07770UL  /* Channel 0 control */
#define LIBERO_SETTING_SGMII_CH1_CNTL       0x37F07770UL  /* Channel 1 control */
#define LIBERO_SETTING_SGMII_RECAL_CNTL     0x000020C8UL  /* Recalibration control */
#define LIBERO_SETTING_SGMII_CLK_CNTL       0xF00050CCUL  /* Clock control */
#define LIBERO_SETTING_SGMII_SPARE_CNTL     0xFF000000UL  /* Spare control */

/* DDR PLL Configuration
 *
 * For LPDDR4 at 1600 MT/s (800 MHz DDR clock):
 * - Reference clock: 125 MHz (from external oscillator via SGMII CFM)
 * - VCO frequency determined by RFDIV and INTIN settings
 * - RFDIV = 5 (bits 13:8 of REF_FB)
 *
 * Values from HSS Video Kit hw_clk_ddr_pll.h (Libero-generated)
 */
#define LIBERO_SETTING_DDR_PLL_CTRL         0x0100003FUL
#define LIBERO_SETTING_DDR_PLL_REF_FB       0x00000500UL  /* RFDIV=5 */
#define LIBERO_SETTING_DDR_PLL_FRACN        0x00000000UL
#define LIBERO_SETTING_DDR_PLL_DIV_0_1      0x02000100UL  /* POST0DIV=1, POST1DIV=2 (from HSS) */
#define LIBERO_SETTING_DDR_PLL_DIV_2_3      0x01000100UL  /* POST2DIV=1, POST3DIV=1 (from HSS) */
#define LIBERO_SETTING_DDR_PLL_CTRL2        0x00001020UL
#define LIBERO_SETTING_DDR_PLL_PHADJ        0x00005003UL  /* Phase init from HSS Video Kit */
#define LIBERO_SETTING_DDR_SSCG_REG_0       0x00000000UL
#define LIBERO_SETTING_DDR_SSCG_REG_1       0x00000000UL
#define LIBERO_SETTING_DDR_SSCG_REG_2       0x00000080UL  /* INTIN=0x80 (128) */
#define LIBERO_SETTING_DDR_SSCG_REG_3       0x00000001UL

/* DDR PHY Mode: LPDDR4, 32-bit, no ECC */
#define LIBERO_SETTING_DDRPHY_MODE          0x00014A24UL

/* DDR Segment Configuration (address translation) */
#define LIBERO_SETTING_SEG0_0               0x80007F80UL
#define LIBERO_SETTING_SEG0_1               0x80007000UL
#define LIBERO_SETTING_SEG0_2               0x00000000UL
#define LIBERO_SETTING_SEG0_3               0x00000000UL
#define LIBERO_SETTING_SEG0_4               0x00000000UL
#define LIBERO_SETTING_SEG0_5               0x00000000UL
#define LIBERO_SETTING_SEG0_6               0x00000000UL
#define LIBERO_SETTING_SEG1_0               0x00000000UL
#define LIBERO_SETTING_SEG1_1               0x00000000UL
#define LIBERO_SETTING_SEG1_2               0x80007F40UL
#define LIBERO_SETTING_SEG1_3               0x80006C00UL
#define LIBERO_SETTING_SEG1_4               0x80007F30UL
#define LIBERO_SETTING_SEG1_5               0x80006800UL
#define LIBERO_SETTING_SEG1_6               0x00000000UL
#define LIBERO_SETTING_SEG1_7               0x00000000UL

/* DDR Training Options */
/* Training skip bits:
 * Bit 0 = skip BCLK_SCLK
 * Bit 1 = skip ADDCMD
 * Bit 2 = skip WRLVL
 * Bit 3 = skip RDGATE
 * Bit 4 = skip DQ_DQS
 *
 * 0x02 = skip ADDCMD only (we do it in software)
 * 0x1F = skip ALL training (use pre-trained values from NV map)
 */
#define LIBERO_SETTING_TRAINING_SKIP_SETTING            0x00000002UL
#define LIBERO_SETTING_TRAINING_SKIP_ALL                0x0000001FUL
#define LIBERO_SETTING_TIP_CFG_PARAMS                   0x07CFE02FUL
#define LIBERO_SETTING_TIP_CONFIG_PARAMS_BCLK_VCOPHS_OFFSET 0x00000002UL

/* DPC Bits - voltage reference settings (from HSS hw_ddr_io_bank.h) */
#define LIBERO_SETTING_DPC_BITS                         0x00050422UL

/* DDR Controller Timing (LPDDR4 @ 1600 Mbps - MT53D512M32D2DS-053)
 * RL=14, WL=8 at 1600 Mbps (800 MHz)
 * Timings for 16Gb x32 LPDDR4 device
 */
#define LIBERO_SETTING_CTRLR_SOFT_RESET_N   0x00000001UL
#define LIBERO_SETTING_CFG_BL               0x00000000UL
#define LIBERO_SETTING_CFG_AUTO_REF_EN      0x00000001UL
#define LIBERO_SETTING_CFG_RAS              0x00000022UL
#define LIBERO_SETTING_CFG_RCD              0x0000000FUL
#define LIBERO_SETTING_CFG_RRD              0x00000008UL
#define LIBERO_SETTING_CFG_RP               0x00000011UL
#define LIBERO_SETTING_CFG_RC               0x00000033UL
#define LIBERO_SETTING_CFG_FAW              0x00000020UL
#define LIBERO_SETTING_CFG_RFC              0x000000E0UL
#define LIBERO_SETTING_CFG_RTP              0x00000008UL
#define LIBERO_SETTING_CFG_WR               0x00000010UL
#define LIBERO_SETTING_CFG_WTR              0x00000008UL
#define LIBERO_SETTING_CFG_STARTUP_DELAY    0x00027100UL
#define LIBERO_SETTING_CFG_MEM_COLBITS      0x0000000AUL
#define LIBERO_SETTING_CFG_MEM_ROWBITS      0x00000010UL
#define LIBERO_SETTING_CFG_MEM_BANKBITS     0x00000003UL
#define LIBERO_SETTING_CFG_NUM_RANKS        0x00000001UL
#define LIBERO_SETTING_CFG_MEMORY_TYPE      0x00000400UL
#define LIBERO_SETTING_CFG_CL               0x00000005UL
#define LIBERO_SETTING_CFG_CWL              0x00000005UL
#define LIBERO_SETTING_CFG_WL               0x00000008UL
#define LIBERO_SETTING_CFG_RL               0x0000000EUL
#define LIBERO_SETTING_CFG_REF_PER          0x00000C34UL
#define LIBERO_SETTING_CFG_XP               0x00000006UL
#define LIBERO_SETTING_CFG_XSR              0x0000001FUL
#define LIBERO_SETTING_CFG_MRD              0x0000000CUL
#define LIBERO_SETTING_CFG_DFI_T_RDDATA_EN  0x00000015UL
#define LIBERO_SETTING_CFG_DFI_T_PHY_RDLAT  0x00000006UL
#define LIBERO_SETTING_CFG_DFI_T_PHY_WRLAT  0x00000003UL
#define LIBERO_SETTING_CFG_DFI_PHYUPD_EN    0x00000001UL

/* DDR function declarations */
#ifndef __ASSEMBLER__
#ifdef WOLFBOOT_RISCV_MMODE
int mpfs_ddr_init(void);
void hal_uart_reinit(void);
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


#endif /* MPFS250_DEF_INCLUDED */

