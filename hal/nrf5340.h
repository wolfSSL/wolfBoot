/* nrf5340.h
 *
 * Copyright (C) 2024 wolfSSL Inc.
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

#ifndef _HAL_NRF5340_H_
#define _HAL_NRF5340_H_

/* Build-time gate for secure or non-secure peripherals.
 * At boot-time peripherals are secure */
#ifdef TARGET_nrf5340_net
    /* Network core */
    #undef QSPI_FLASH /* not supported on network core */
    #define CORE_STR "net"
#else
    /* Application core */
    #define TARGET_nrf5340_app
    #define CORE_STR "app"
    #ifndef TZEN
        /* at reset/power on wolfBoot is using secure bases */
        #define TZEN
    #endif
#endif

/* Clock */
#ifdef TARGET_nrf5340_app
    #define CPU_CLOCK 128000000UL /* 128MHz */
#else
    #define CPU_CLOCK  64000000UL /*  64MHz */
#endif

/* Flash */
#define FLASH_PAGESZ_APP (4096)            /* 4KB Page */
#define FLASH_BASE_APP   (0x00000000)
#define FLASH_SIZE_APP   (1024UL * 1024UL) /* 1MB Flash */

#define FLASH_PAGESZ_NET (2048)            /* 2KB Page */
#define FLASH_BASE_NET   (0x01000000)
#define FLASH_SIZE_NET   (256UL * 1024UL)  /* 256KB Flash */

#ifdef TARGET_nrf5340_app
    #define FLASH_PAGE_SIZE FLASH_PAGESZ_APP
    #define FLASH_BASE_ADDR FLASH_BASE_APP
    #define FLASH_SIZE      FLASH_SIZE_APP
#else
    #define FLASH_PAGE_SIZE FLASH_PAGESZ_NET
    #define FLASH_BASE_ADDR FLASH_BASE_NET
    #define FLASH_SIZE      FLASH_SIZE_NET
#endif

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define DSB() __asm__ volatile ("dsb")
#define ISB() __asm__ volatile ("isb")
#define NOP() __asm__ volatile ("nop")

void sleep_us(uint32_t usec);

/* PSEL Port (bit 5) - Used for various PSEL (UART,SPI,QSPI,I2C,NFC) */
#define PSEL_PORT(n)         (((n) & 0x1) << 5)

/* Domain Configuration */
#ifdef TARGET_nrf5340_app
    #ifdef TZEN
        #define DCNF_BASE (0x50000000)
    #else
        #define DCNF_BASE (0x40000000)
    #endif
#else
    #define DCNF_BASE     (0x41000000)
#endif

#define DCNF_CPUID         *((volatile uint32_t *)(DCNF_BASE + 0x420))
#ifdef TARGET_nrf5340_app
    /* allows blocking of network cores ability to resources on application core */
    #define DCNF_EXTPERI0_PROT *((volatile uint32_t *)(DCNF_BASE + 0x440))
    #define DCNF_EXTRAM0_PROT  *((volatile uint32_t *)(DCNF_BASE + 0x460)) /* Eight 64KB slaves bit 0=0x20000000-0x20010000 (64KB) */
    #define DCNF_EXTCODE0_PROT *((volatile uint32_t *)(DCNF_BASE + 0x480))
#endif

#ifdef TARGET_nrf5340_app
    /* SPU */
    #define SPU_BASE 0x50003000UL
    #define SPU_EXTDOMAIN_PERM(n)   *((volatile uint32_t *)(SPU_BASE + 0x440 + (((n) & 0x0) * 0x4)))
    #define SPU_EXTDOMAIN_PERM_SECATTR_NONSECURE  0
    #define SPU_EXTDOMAIN_PERM_SECATTR_SECURE     (1 << 4)
    #define SPU_EXTDOMAIN_PERM_UNLOCK             0
    #define SPU_EXTDOMAIN_PERM_LOCK               (1 << 8)
    #define SPU_EXTDOMAIN_PERM_SECUREMAPPING_MASK (0x3)

    #define SPU_BLOCK_SIZE (16 * 1024)
    #define SPU_FLASHREGION_PERM(n) *((volatile uint32_t *)(SPU_BASE + 0x600 + (((n) & 0x3F) * 0x4)))
    #define SPU_FLASHREGION_PERM_EXEC    (1 << 0)
    #define SPU_FLASHREGION_PERM_WRITE   (1 << 1)
    #define SPU_FLASHREGION_PERM_READ    (1 << 2)
    #define SPU_FLASHREGION_PERM_SECATTR (1 << 4)
    #define SPU_FLASHREGION_PERM_LOCK    (1 << 8)
#endif

/* OTP */
#define UICR_BASE (0x00FF8000UL)
#define UICR_USER (UICR_BASE)
#define UICR_OTP  (UICR_BASE + 0x100)

/* Reset */
#ifdef TARGET_nrf5340_app
    #ifdef TZEN
        #define RESET_BASE (0x50005000)
    #else
        #define RESET_BASE (0x40005000)
    #endif
#else
    #define RESET_BASE     (0x41005000)
#endif
#define NETWORK_RESETREAS *((volatile uint32_t *)(RESET_BASE + 0x400))
#define NETWORK_RESETREAS_RESETPIN  (1 << 0)
#define NETWORK_RESETREAS_DOG0      (1 << 1) /* watchdog timer 0 */
#define NETWORK_RESETREAS_SREQ      (1 << 3) /* soft reset */
#define NETWORK_RESETREAS_OFF       (1 << 5) /* wake from off */
#define NETWORK_RESETREAS_MFORCEOFF (1 << 23)
#define NETWORK_FORCEOFF  *((volatile uint32_t *)(RESET_BASE + 0x614))
#define NETWORK_FORCEOFF_RELEASE 0
#define NETWORK_FORCEOFF_HOLD    1
#define NETWORK_ERRATA_161 *((volatile uint32_t *)(RESET_BASE + 0x618))


/* Non-volatile memory controller */
#ifdef TARGET_nrf5340_app
    #ifdef TZEN
        #define NVMC_BASE (0x50039000)
    #else
        #define NVMC_BASE (0x40039000)
    #endif
#else
    #define NVMC_BASE     (0x41080000)
#endif
#define NVMC_READY       *((volatile uint32_t *)(NVMC_BASE + 0x400))
#define NVMC_READYNEXT   *((volatile uint32_t *)(NVMC_BASE + 0x408))
#define NVMC_CONFIG      *((volatile uint32_t *)(NVMC_BASE + 0x504))
#define NVMC_CONFIGNS    *((volatile uint32_t *)(NVMC_BASE + 0x584))
#define NVMC_WRITEUICRNS *((volatile uint32_t *)(NVMC_BASE + 0x588)) /* User information configuration registers (UICR) */

#define NVMC_CONFIG_REN  0 /* read only access */
#define NVMC_CONFIG_WEN  1 /* write enable */
#define NVMC_CONFIG_EEN  2 /* erase enable */
#define NVMC_CONFIG_PEE  3 /* partial erase enable - secure only */

#define NVMC_WRITEUICRNS_SET 1
#define NVMC_WRITEUICRNS_KEY 0xAFBE5A70

/* Clock control */
#ifdef TARGET_nrf5340_app
    #ifdef TZEN
        #define CLOCK_BASE  (0x50005000)
    #else
        #define CLOCK_BASE  (0x40005000)
    #endif
#else
    #define CLOCK_BASE      (0x41005000) /* network core */
#endif
#define CLOCK_HFCLKSTART    *((volatile uint32_t *)(CLOCK_BASE + 0x000))
#define CLOCK_HFCLKSTOP     *((volatile uint32_t *)(CLOCK_BASE + 0x004))
#define CLOCK_HFCLKSTARTED  *((volatile uint32_t *)(CLOCK_BASE + 0x100))
#define CLOCK_HFCLKSTAT     *((volatile uint32_t *)(CLOCK_BASE + 0x40C))
#define CLOCK_HFCLKSRC      *((volatile uint32_t *)(CLOCK_BASE + 0x514))
#define CLOCK_HFCLKSRC_HFXO 1
#define CLOCK_HFCLKCTRL     *((volatile uint32_t *)(CLOCK_BASE + 0x558))
#define CLOCK_HFCLKCTRL_DIV1 0
#define CLOCK_HFCLKCTRL_DIV2 1

#define CLOCK_HFCLK192MSTART     *((volatile uint32_t *)(CLOCK_BASE + 0x020))
#define CLOCK_HFCLK192MSTOP      *((volatile uint32_t *)(CLOCK_BASE + 0x024))
#define CLOCK_HFCLK192MSTARTED   *((volatile uint32_t *)(CLOCK_BASE + 0x124))
#define CLOCK_HFCLK192MSRC       *((volatile uint32_t *)(CLOCK_BASE + 0x580))
#define CLOCK_HFCLK192MSRC_HFXO  1
#define CLOCK_HFCLK192MCTRL      *((volatile uint32_t *)(CLOCK_BASE + 0x5B8))
#define CLOCK_HFCLK192MCTRL_DIV1 0
#define CLOCK_HFCLK192MCTRL_DIV2 1
#define CLOCK_HFCLK192MCTRL_DIV4 2

/* Low frequency: 32.768 kHz */
#define CLOCK_LFCLKSTART      *((volatile uint32_t *)(CLOCK_BASE + 0x008))
#define CLOCK_LFCLKSTOP       *((volatile uint32_t *)(CLOCK_BASE + 0x00C))
#define CLOCK_LFCLKSTARTED    *((volatile uint32_t *)(CLOCK_BASE + 0x104))
#define CLOCK_LFCLKSRC        *((volatile uint32_t *)(CLOCK_BASE + 0x518))
#define CLOCK_LFCLKSRC_LFULP  0 /* ultra-low power RC oscillator */
#define CLOCK_LFCLKSRC_LFRC   1 /* RC oscillator */
#define CLOCK_LFCLKSRC_LFXO   2 /* crystal oscillator */
#define CLOCK_LFCLKSRC_LFSYNT 3 /* synthesized from HFCLK */


/* GPIO Port (0-1) */
#ifdef TARGET_nrf5340_app
    #ifdef TZEN
        #define GPIO_BASE(n) (0x50842500 + (((n) & 0x1) * 0x300))
    #else
        #define GPIO_BASE(n) (0x40842500 + (((n) & 0x1) * 0x300))
    #endif
#else
    #define GPIO_BASE(n)     (0x418C0500 + (((n) & 0x1) * 0x300))
#endif
#define GPIO_OUT(n)          *((volatile uint32_t *)(GPIO_BASE(n) + 0x004))
#define GPIO_OUTSET(n)       *((volatile uint32_t *)(GPIO_BASE(n) + 0x008))
#define GPIO_OUTCLR(n)       *((volatile uint32_t *)(GPIO_BASE(n) + 0x00C))
#define GPIO_IN(n)           *((volatile uint32_t *)(GPIO_BASE(n) + 0x010))
#define GPIO_DIRSET(n)       *((volatile uint32_t *)(GPIO_BASE(n) + 0x018))
#define GPIO_PIN_CNF(n,p)    *((volatile uint32_t *)(GPIO_BASE(n) + 0x200 + ((p) * 0x4)))

#define GPIO_CNF_IN          0
#define GPIO_CNF_IN_DIS      2
#define GPIO_CNF_OUT         3
#define GPIO_CNF_PULL_DIS    0
#define GPIO_CNF_PULL_UP     (3UL << 2)
#define GPIO_CNF_PULL_DOWN   (1UL << 2)
#define GPIO_CNF_STD_DRIVE   0
#define GPIO_CNF_HIGH_DRIVE  (3UL << 8)
#define GPIO_CNF_SENSE_NONE  0
#define GPIO_CNF_MCUSEL(n)   (((n) & 0x7) << 28)

/* UART (0-1) */
#ifdef TARGET_nrf5340_app
    #ifdef TZEN
        #define UART_BASE(n) (0x50008000 + (((n) & 0x1) * 0x1000))
    #else
        #define UART_BASE(n) (0x40008000 + (((n) & 0x1) * 0x1000))
    #endif
#else
    #define UART_BASE(n)     (0x41013000) /* UARTE0 only */

#endif
#define UART_TASK_STARTTX(n) *((volatile uint32_t *)(UART_BASE(n) + 0x008))
#define UART_TASK_STOPTX(n)  *((volatile uint32_t *)(UART_BASE(n) + 0x00C))
#define UART_EVENT_TXDRDY(n) *((volatile uint32_t *)(UART_BASE(n) + 0x11C))
#define UART_EVENT_ENDTX(n)  *((volatile uint32_t *)(UART_BASE(n) + 0x120))
#define UART_ENABLE(n)       *((volatile uint32_t *)(UART_BASE(n) + 0x500))
#define UART_PSEL_TXD(n)     *((volatile uint32_t *)(UART_BASE(n) + 0x50C))
#define UART_PSEL_RXD(n)     *((volatile uint32_t *)(UART_BASE(n) + 0x514))
#define UART_BAUDRATE(n)     *((volatile uint32_t *)(UART_BASE(n) + 0x524))
#define UART_TXD_PTR(n)      *((volatile uint32_t *)(UART_BASE(n) + 0x544))
#define UART_TXD_MAXCOUNT(n) *((volatile uint32_t *)(UART_BASE(n) + 0x548))
#define UART_CONFIG(n)       *((volatile uint32_t *)(UART_BASE(n) + 0x56C))

#define BAUD_115200 0x01D60000UL

void uart_write_sz(const char* c, unsigned int sz);

/* SPI (0-2) */
#ifdef TARGET_nrf5340_app
    #ifdef TZEN
        #define SPI_BASE(n)  (0x50008000 + (((n) & 0x3) * 0x1000))
    #else
        #define SPI_BASE(n)  (0x40008000 + (((n) & 0x3) * 0x1000))
    #endif
#else
    #define SPI_BASE(n)      (0x41013000) /* SPIM0 only */
#endif
#define SPI_TASKS_START(n)   *((volatile uint32_t *)(SPI_BASE(n) + 0x010))
#define SPI_TASKS_STOP(n)    *((volatile uint32_t *)(SPI_BASE(n) + 0x014))
#define SPI_EVENTS_ENDRX(n)  *((volatile uint32_t *)(SPI_BASE(n) + 0x110))
#define SPI_EVENTS_END(n)    *((volatile uint32_t *)(SPI_BASE(n) + 0x118))
#define SPI_EVENTS_ENDTX(n)  *((volatile uint32_t *)(SPI_BASE(n) + 0x120))
#define SPI_EV_RDY(n)        *((volatile uint32_t *)(SPI_BASE(n) + 0x108))
#define SPI_INTENSET(n)      *((volatile uint32_t *)(SPI_BASE(n) + 0x304))
#define SPI_INTENCLR(n)      *((volatile uint32_t *)(SPI_BASE(n) + 0x308))
#define SPI_ENABLE(n)        *((volatile uint32_t *)(SPI_BASE(n) + 0x500))
#define SPI_PSEL_SCK(n)      *((volatile uint32_t *)(SPI_BASE(n) + 0x508))
#define SPI_PSEL_MOSI(n)     *((volatile uint32_t *)(SPI_BASE(n) + 0x50C))
#define SPI_PSEL_MISO(n)     *((volatile uint32_t *)(SPI_BASE(n) + 0x510))
#define SPI_RXDATA(n)        *((volatile uint32_t *)(SPI_BASE(n) + 0x518))
#define SPI_TXDATA(n)        *((volatile uint32_t *)(SPI_BASE(n) + 0x51C))
#define SPI_FREQUENCY(n)     *((volatile uint32_t *)(SPI_BASE(n) + 0x524))
#define SPI_CONFIG(n)        *((volatile uint32_t *)(SPI_BASE(n) + 0x554))

#define SPI_FREQ_K125 0x02000000
#define SPI_FREQ_K250 0x04000000
#define SPI_FREQ_K500 0x08000000
#define SPI_FREQ_M1   0x10000000
#define SPI_FREQ_M2   0x20000000
#define SPI_FREQ_M4   0x40000000
#define SPI_FREQ_M8   0x80000000
#define SPI_FREQ_M16  0x0A000000
#define SPI_FREQ_M32  0x14000000

/* QSPI */
#ifdef TARGET_nrf5340_app
    #ifdef TZEN
        #define QSPI_BASE         (0x5002B000)
    #else
        #define QSPI_BASE         (0x4002B000)
    #endif
    #define QSPI_TASKS_ACTIVATE   *((volatile uint32_t *)(QSPI_BASE + 0x000))
    #define QSPI_TASKS_READSTART  *((volatile uint32_t *)(QSPI_BASE + 0x004))
    #define QSPI_TASKS_WRITESTART *((volatile uint32_t *)(QSPI_BASE + 0x008))
    #define QSPI_TASKS_ERASESTART *((volatile uint32_t *)(QSPI_BASE + 0x00C))
    #define QSPI_TASKS_DEACTIVATE *((volatile uint32_t *)(QSPI_BASE + 0x010))
    #define QSPI_EVENTS_READY     *((volatile uint32_t *)(QSPI_BASE + 0x100))

    #define QSPI_INTEN            *((volatile uint32_t *)(QSPI_BASE + 0x300))
    #define QSPI_INTENSET         *((volatile uint32_t *)(QSPI_BASE + 0x304))
    #define QSPI_INTENCLR         *((volatile uint32_t *)(QSPI_BASE + 0x308))

    #define QSPI_ENABLE           *((volatile uint32_t *)(QSPI_BASE + 0x500))

    #define QSPI_READ_SRC         *((volatile uint32_t *)(QSPI_BASE + 0x504))
    #define QSPI_READ_DST         *((volatile uint32_t *)(QSPI_BASE + 0x508))
    #define QSPI_READ_CNT         *((volatile uint32_t *)(QSPI_BASE + 0x50C))
    #define QSPI_WRITE_DST        *((volatile uint32_t *)(QSPI_BASE + 0x510))
    #define QSPI_WRITE_SRC        *((volatile uint32_t *)(QSPI_BASE + 0x514))
    #define QSPI_WRITE_CNT        *((volatile uint32_t *)(QSPI_BASE + 0x518))
    #define QSPI_ERASE_PTR        *((volatile uint32_t *)(QSPI_BASE + 0x51C))
    #define QSPI_ERASE_LEN        *((volatile uint32_t *)(QSPI_BASE + 0x520))

    #define QSPI_PSEL_SCK         *((volatile uint32_t *)(QSPI_BASE + 0x524))
    #define QSPI_PSEL_CSN         *((volatile uint32_t *)(QSPI_BASE + 0x528))
    #define QSPI_PSEL_IO0         *((volatile uint32_t *)(QSPI_BASE + 0x530))
    #define QSPI_PSEL_IO1         *((volatile uint32_t *)(QSPI_BASE + 0x534))
    #define QSPI_PSEL_IO2         *((volatile uint32_t *)(QSPI_BASE + 0x538))
    #define QSPI_PSEL_IO3         *((volatile uint32_t *)(QSPI_BASE + 0x53C))

    #define QSPI_IFCONFIG0        *((volatile uint32_t *)(QSPI_BASE + 0x544))
    #define QSPI_IFCONFIG1        *((volatile uint32_t *)(QSPI_BASE + 0x600))

    #define QSPI_STATUS           *((volatile uint32_t *)(QSPI_BASE + 0x604))
    #define QSPI_ADDRCONF         *((volatile uint32_t *)(QSPI_BASE + 0x624))
    #define QSPI_CINSTRCONF       *((volatile uint32_t *)(QSPI_BASE + 0x634))
    #define QSPI_CINSTRDAT0       *((volatile uint32_t *)(QSPI_BASE + 0x638))
    #define QSPI_CINSTRDAT1       *((volatile uint32_t *)(QSPI_BASE + 0x63C))
    #define QSPI_IFTIMING         *((volatile uint32_t *)(QSPI_BASE + 0x640))

    #define QSPI_IFCONFIG0_READOC_MASK     0x7
    #define QSPI_IFCONFIG0_READOC_FASTREAD (0) /* opcode 0x0B */
    #define QSPI_IFCONFIG0_READOC_READ2O   (1) /* opcode 0x3B */
    #define QSPI_IFCONFIG0_READOC_READ2IO  (2) /* opcode 0xBB */
    #define QSPI_IFCONFIG0_READOC_READ4O   (3) /* opcode 0x6B */
    #define QSPI_IFCONFIG0_READOC_READ4IO  (4) /* opcode 0xEB */
    #define QSPI_IFCONFIG0_WRITEOC_MASK    ((0x7) << 3)
    #define QSPI_IFCONFIG0_WRITEOC_PP      ((0) << 3) /* opcode 0x02 */
    #define QSPI_IFCONFIG0_WRITEOC_PP2O    ((1) << 3) /* opcode 0xA2 */
    #define QSPI_IFCONFIG0_WRITEOC_PP4O    ((2) << 3) /* opcode 0x32 */
    #define QSPI_IFCONFIG0_WRITEOC_PP4IO   ((3) << 3) /* opcode 0x38 */
    #define QSPI_IFCONFIG0_ADDRMODE_24BIT  ((0) << 6)
    #define QSPI_IFCONFIG0_ADDRMODE_32BIT  ((1) << 6)
    #define QSPI_IFCONFIG0_DPMENABLE       ((1) << 7)
    #define QSPI_IFCONFIG0_PPSIZE_256      ((0) << 12)
    #define QSPI_IFCONFIG0_PPSIZE_512      ((1) << 12)

    #define QSPI_IFCONFIG1_SCKDELAY_MASK   0xFF
    #define QSPI_IFCONFIG1_SCKDELAY(n)     ((n) & QSPI_IFCONFIG1_SCKDELAY_MASK)
    #define QSPI_IFCONFIG1_SPIMODE0        0
    #define QSPI_IFCONFIG1_SPIMODE3        (1UL << 25)
    #define QSPI_IFCONFIG1_SCKFREQ_MASK    ((0xF) << 28)
    #define QSPI_IFCONFIG1_SCKFREQ(n)      (((n) & 0xF) << 28)

    #define QSPI_CINSTRCONF_OPCODE(n)      ((n) & 0xFF)
    #define QSPI_CINSTRCONF_LENGTH(n)      (((n) & 0xF) << 8)
    #define QSPI_CINSTRCONF_LIO2           (1 << 12)
    #define QSPI_CINSTRCONF_LIO3           (1 << 13)
    #define QSPI_CINSTRCONF_WREN           (1 << 15) /* send WREN opcode 0x6 before */

    #define QSPI_IFTIMING_RXDELAY(n)       (((n) & 0x7) << 8)
#else
    /* Disable QSPI Flash */
    #undef QSPI_FLASH
#endif

/* interprocessor communication (IPC) peripheral */
#ifdef TARGET_nrf5340_app
    #ifdef TZEN
    #define IPC_BASE      (0x5002A000)
    #else
    #define IPC_BASE      (0x4002A000)
    #endif
#else
    #define IPC_BASE      (0x41012000) /* network core */
#endif
#define IPC_TASKS_SEND(n)      *((volatile uint32_t *)(IPC_BASE + 0x000 + (((n) & 0xF) * 0x4)))
#define IPC_SUBSCRIBE_SEND(n)  *((volatile uint32_t *)(IPC_BASE + 0x080 + (((n) & 0xF) * 0x4)))
#define IPC_EVENTS_RECEIVE(n)  *((volatile uint32_t *)(IPC_BASE + 0x100 + (((n) & 0xF) * 0x4)))
#define IPC_PUBLISH_RECEIVE(n) *((volatile uint32_t *)(IPC_BASE + 0x180 + (((n) & 0xF) * 0x4)))
#define IPC_SEND_CNF(n)        *((volatile uint32_t *)(IPC_BASE + 0x510 + (((n) & 0xF) * 0x4)))
#define IPC_RECEIVE_CNF(n)     *((volatile uint32_t *)(IPC_BASE + 0x590 + (((n) & 0xF) * 0x4)))
#define IPC_GPMEM(n)           *((volatile uint32_t *)(IPC_BASE + 0x610 + (((n) & 0x1) * 0x4)))

/* RTC - uses LFCLK - 24-bit counter/compare */
#ifdef TARGET_nrf5340_app
    #ifdef TZEN
    #define RTC_BASE(n)     ((0x50014000) + (((n) & 0x1) * 0x1000))
    #else
    #define RTC_BASE(n)     ((0x40014000) + (((n) & 0x1) * 0x1000))
    #endif
#else
    #define RTC_BASE(n)     (0x41011000) /* network core */
#endif
#define RTC_START(n)        *((volatile uint32_t *)(RTC_BASE(n) + 0x000))
#define RTC_STOP(n)         *((volatile uint32_t *)(RTC_BASE(n) + 0x004))
#define RTC_CLEAR(n)        *((volatile uint32_t *)(RTC_BASE(n) + 0x008))
#define RTC_EVENT_TICK(n)   *((volatile uint32_t *)(RTC_BASE(n) + 0x100))
#define RTC_EVENT_OVRFLW(n) *((volatile uint32_t *)(RTC_BASE(n) + 0x104))
#define RTC_EVENT_CC(n,i)   *((volatile uint32_t *)(RTC_BASE(n) + 0x140 + ((i) & 0x3) * 0x4))
#define RTC_EVTENSET(n)     *((volatile uint32_t *)(RTC_BASE(n) + 0x344))
#define RTC_EVTENSET_TICK   (1 << 0)
#define RTC_EVTENSET_OVRFLW (1 << 1)
#define RTC_EVTENSET_CC0    (1 << 16)
#define RTC_EVTENSET_CC1    (1 << 17)
#define RTC_EVTENSET_CC2    (1 << 18)
#define RTC_EVTENSET_CC3    (1 << 19)
#define RTC_COUNTER(n)      *((volatile uint32_t *)(RTC_BASE(n) + 0x504))
#define RTC_PRESCALER(n)    *((volatile uint32_t *)(RTC_BASE(n) + 0x508)) /* default=0 or 32768 per second (12-bit) up to 0xFFF */
#define RTC_CC(n,i)         *((volatile uint32_t *)(RTC_BASE(n) + 0x540 + ((i) & 0x3) * 0x4))
#define RTC_OVERFLOW        0xFFFFFFUL


#endif /* !_HAL_NRF5340_H_ */
