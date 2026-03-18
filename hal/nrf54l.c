/* nrf54l.c
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

#ifdef TARGET_nrf54l

#include <stdint.h>
#include <inttypes.h>

#include <string.h>

#include "hal.h"
#include "image.h"
#include "nrf54l.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"
#ifdef TZEN
#include "hal/armv8m_tz.h"
#endif

/* UART */

#if defined(DEBUG_UART) || (UART_FLASH)

#define UART_WRITE_BUF_SIZE 128


static void uart_init_device(int device, uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    int port = UART_PORT_NUM(device);
    int pinTx = UART_PIN_NUM_TX(device);
    int pinRx = UART_PIN_NUM_RX(device);

    UART_ENABLE(device) = UART_ENABLE_ENABLE_Disabled;

    /* Pre-drive TX high (IDLE) before configuring as output to avoid a low
     * glitch that would look like start bits to the receiver. */
    GPIO_OUTSET(port) = (1U << pinTx);
    /* Configure TX pin */
    GPIO_PIN_CNF(port, pinTx) = (GPIO_CNF_OUT | GPIO_CNF_STD_DRIVE_0 | GPIO_CNF_MCUSEL(0));
    /* Configure RX pin */
    GPIO_PIN_CNF(port, pinRx) = (GPIO_CNF_IN | GPIO_CNF_STD_DRIVE_0 | GPIO_CNF_MCUSEL(0));

    UART_PSEL_TXD(device) = ((pinTx << UART_PSEL_TXD_PIN_Pos) & UART_PSEL_TXD_PIN_Msk) |
                     ((port << UART_PSEL_TXD_PORT_Pos) & UART_PSEL_TXD_PORT_Msk);
    UART_PSEL_RXD(device) = ((pinRx << UART_PSEL_RXD_PIN_Pos) & UART_PSEL_RXD_PIN_Msk) |
                     ((port << UART_PSEL_RXD_PORT_Pos) & UART_PSEL_RXD_PORT_Msk);
    UART_PSEL_CTS(device) = UART_PSEL_CTS_CONNECT_Disconnected;
    UART_PSEL_RTS(device) = UART_PSEL_RTS_CONNECT_Disconnected;
    UART_BAUDRATE(device) = UART_BAUDRATE_VALUE(bitrate);
    UART_CONFIG(device) = UART_CONFIG_VALUE(data, parity, stop);

    UART_ENABLE(device) = UART_ENABLE_ENABLE_Enabled;
}

void uart_write_raw(int device, const char* buffer, unsigned int sz)
{
    /* EasyDMA requires a RAM buffer */
    static uint8_t uartTxBuf[UART_WRITE_BUF_SIZE];

    while (sz > 0) {
        /*
         *  loop until all bytes written,
         *  but only write UART_WRITE_BUF_SIZE max chars at once
         */
        unsigned int xfer = sz;
        if (xfer > sizeof(uartTxBuf))
            xfer = sizeof(uartTxBuf);
        memcpy(uartTxBuf, buffer, xfer);

        UART_EVENTS_DMA_TX_END(device) = 0;
        UART_EVENTS_DMA_TX_BUSERROR(device) = 0;

        UART_DMA_TX_PTR(device) = (uint32_t)uartTxBuf;
        UART_DMA_TX_MAXCNT(device) = xfer;
        UART_TASKS_DMA_TX_START(device) = UART_TASKS_DMA_TX_START_START_Trigger;

        while ((UART_EVENTS_DMA_TX_END(device) == 0) &&
               (UART_EVENTS_DMA_TX_BUSERROR(device) == 0))
            ;

        if (UART_EVENTS_DMA_TX_BUSERROR(device) != 0) {
            UART_TASKS_DMA_TX_STOP(device) =
                UART_TASKS_DMA_TX_STOP_STOP_Trigger;
            break;
        }

        sz -= xfer;
        buffer += xfer;
    }
}

void uart_write_device(int device, const char* buf, unsigned int sz)
{
    static char buffer[UART_WRITE_BUF_SIZE];
    int bufsz = 0;

    for (int i = 0; i < (int)sz && bufsz < UART_WRITE_BUF_SIZE; i++) {
        char ch = (char) buf[i];

        if (ch == '\r')
            continue;

        if (ch == '\n') {
            if (bufsz >= (UART_WRITE_BUF_SIZE - 1))
                break;

            buffer[bufsz++] = '\r';
        }
        buffer[bufsz++] = ch;
    }
    uart_write_raw(device, buffer, bufsz);
}

void uart_write(const char* buf, unsigned int sz)
{
   uart_write_device(DEVICE_MONITOR, buf, sz);
}

#endif /* DEBUG_UART || UART_FLASH */

#if (defined DEBUG_UART || UART_FLASH)
#define UART_RX_TIMEOUT 1000000UL
int uart_read(int device, uint8_t* buf, unsigned int sz)
{
    if ((buf == NULL) || (sz == 0))
        return -1;

    UART_EVENTS_DMA_RX_END(device) = 0;
    UART_EVENTS_DMA_RX_BUSERROR(device) = 0;

    UART_DMA_RX_PTR(device) = (uint32_t)buf;
    UART_DMA_RX_MAXCNT(device) = sz;

    UART_TASKS_DMA_RX_START(device) = UART_TASKS_DMA_RX_START_START_Trigger;

    for (uint32_t guard = 0; UART_EVENTS_DMA_RX_END(device) == 0; guard++) {
        if (UART_EVENTS_DMA_RX_BUSERROR(device) != 0) {
            UART_TASKS_DMA_RX_STOP(device) = UART_TASKS_DMA_RX_STOP_STOP_Trigger;
            return -1;
        }
        if (guard > UART_RX_TIMEOUT) {
            UART_TASKS_DMA_RX_STOP(device) = UART_TASKS_DMA_RX_STOP_STOP_Trigger;
            return 0;
        }
    }

    UART_TASKS_DMA_RX_STOP(device) = UART_TASKS_DMA_RX_STOP_STOP_Trigger;
    return (int)UART_DMA_RX_AMOUNT(device);
}
#endif   /* DEBUG_UART || UART_FLASH */

static void RAMFUNCTION flash_wait_ready(void)
{
    while ((RRAMC_READY & RRAMC_READY_READY_Msk) == 0U)
        ;
}

static void RAMFUNCTION flash_wait_ready_next(void)
{
    while ((RRAMC_READYNEXT & RRAMC_READYNEXT_READYNEXT_Msk) == 0U)
        ;
}

static void RAMFUNCTION flash_wait_buf_empty(void)
{
    while ((RRAMC_BUFSTATUS_WRITEBUFEMPTY &
            RRAMC_BUFSTATUS_WRITEBUFEMPTY_EMPTY_Msk) == 0U)
        ;
}

static void RAMFUNCTION flash_commit_writebuf(void)
{
    if ((RRAMC_BUFSTATUS_WRITEBUFEMPTY &
         RRAMC_BUFSTATUS_WRITEBUFEMPTY_EMPTY_Msk) == 0U) {
        RRAMC_TASKS_COMMITWRITEBUF =
            RRAMC_TASKS_COMMITWRITEBUF_TASKS_COMMITWRITEBUF_Trigger;
        flash_wait_ready();
        flash_wait_buf_empty();
    }
}

static void RAMFUNCTION flash_write_enable(int enable)
{
    uint32_t cfg = RRAMC_CONFIG;
    if (enable != 0)
        cfg |= RRAMC_CONFIG_WEN_Msk;
    else
        cfg &= ~RRAMC_CONFIG_WEN_Msk;
    RRAMC_CONFIG = cfg;
    flash_wait_ready();
}

static int RAMFUNCTION flash_program_range(uint32_t address,
                                           const uint8_t *data, int len)
{
    int i = 0;

    while (i < len) {
        flash_wait_ready_next();

        if ((((address + i) & 0x3U) == 0U) &&
            ((((uintptr_t)(data + i)) & 0x3U) == 0U) &&
            (len - i) >= 4) {
            const uint32_t *src = (const uint32_t *)(data + i);
            volatile uint32_t *dst = (volatile uint32_t *)(address + i);
            *dst = *src;
            i += 4;
        }
        else {
            uint32_t word;
            volatile uint32_t *dst =
                (volatile uint32_t *)((address + i) & ~0x3U);
            int offset = (int)((address + i) & 0x3U);

            word = *dst;
            ((uint8_t *)&word)[offset] = data[i];
            *dst = word;
            i++;
        }
    }

    return 0;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    flash_write_enable(1);
    flash_program_range(address, data, len);
    flash_commit_writebuf();
    flash_write_enable(0);
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end = address + (uint32_t)len;
    uint8_t blank[64];

    memset(blank, 0xFF, sizeof(blank));

    flash_write_enable(1);
    while (address < end) {
        int chunk = (int)(end - address);
        if (chunk > (int)sizeof(blank))
            chunk = (int)sizeof(blank);
        flash_program_range(address, blank, chunk);
        address += (uint32_t)chunk;
    }
    flash_commit_writebuf();
    flash_write_enable(0);
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flash_write_enable(1);
}

void RAMFUNCTION hal_flash_lock(void)
{
    flash_write_enable(0);
}

#if (UART_FLASH)
int uart_tx(const uint8_t c)
{

    uart_write((const char *)&c, 1);
    return 0;
}

int uart_rx(uint8_t *c)
{
    return uart_read(DEVICE_DOWNLOAD, c, 1);
}

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    uart_init_device(DEVICE_DOWNLOAD, bitrate, data, parity, stop);
    return 0;
}
#else
void uart_init(void)
{
    uart_init_device(DEVICE_DOWNLOAD, 115200, 8, 'N', 1);
}
#endif

static void high_freq_clock_init(void)
{
    /* Start the HFXO and wait until it is running */
    CLOCK_EVENTS_XOSTARTED = 0;
    CLOCK_TASKS_XOSTART = CLOCK_TASKS_XOSTART_TASKS_XOSTART_Trigger;

    while ((CLOCK_EVENTS_XOSTARTED == 0) ||
           ((CLOCK_XO_STAT & CLOCK_XO_STAT_STATE_Msk) ==
            (CLOCK_XO_STAT_STATE_NotRunning << CLOCK_XO_STAT_STATE_Pos))) {
        /* wait */
    }
}

static void low_freq_clock_init(void)
{
    /* Configure the 32.768 kHz crystal load caps using factory trim when present */
    uint32_t intcap = OSCILLATORS_XOSC32KI_INTCAP_ResetValue &
                      OSCILLATORS_XOSC32KI_INTCAP_VAL_Msk;

    if (FICR_XOSC32KTRIM != FICR_XOSC32KTRIM_ResetValue) {
        uint32_t trim = (FICR_XOSC32KTRIM & FICR_XOSC32KTRIM_OFFSET_Msk) >>
                        FICR_XOSC32KTRIM_OFFSET_Pos;
        intcap = trim & (OSCILLATORS_XOSC32KI_INTCAP_VAL_Msk >>
                         OSCILLATORS_XOSC32KI_INTCAP_VAL_Pos);
    }

    OSCILLATORS_XOSC32KI_INTCAP =
        (intcap << OSCILLATORS_XOSC32KI_INTCAP_VAL_Pos) &
        OSCILLATORS_XOSC32KI_INTCAP_VAL_Msk;

    /* Start the LFCLK from the external LFXO and wait until it is running */
    CLOCK_EVENTS_LFCLKSTARTED = 0;
    CLOCK_LFCLK_SRC = CLOCK_LFCLK_SRC_SRC_LFXO;
    CLOCK_TASKS_LFCLKSTART = CLOCK_TASKS_LFCLKSTART_TASKS_LFCLKSTART_Trigger;

    while ((CLOCK_EVENTS_LFCLKSTARTED == 0) ||
           ((CLOCK_LFCLK_STAT & CLOCK_LFCLK_STAT_SRC_Msk) !=
            (CLOCK_LFCLK_STAT_SRC_LFXO << CLOCK_LFCLK_STAT_SRC_Pos)) ||
           ((CLOCK_LFCLK_STAT & CLOCK_LFCLK_STAT_STATE_Msk) ==
            (CLOCK_LFCLK_STAT_STATE_NotRunning << CLOCK_LFCLK_STAT_STATE_Pos))) {
        /* wait */
    }
}

static void clock_init(void)
{
    high_freq_clock_init();
    low_freq_clock_init();
}

static void clock_deinit(void)
{
}



static void hal_handle_approtect(void)
{
#ifdef DEBUG_SYMBOLS
    /* APPROTECT re-enables on every reset unless firmware
     * explicitly opens the TAMPC signals. */
    volatile uint32_t *regs[] = {
        &TAMPC_PROTECT_DOMAIN0_DBGEN_CTRL,
        &TAMPC_PROTECT_DOMAIN0_NIDEN_CTRL,
        &TAMPC_PROTECT_DOMAIN0_SPIDEN_CTRL,
        &TAMPC_PROTECT_DOMAIN0_SPNIDEN_CTRL,
        &TAMPC_PROTECT_AP0_DBGEN_CTRL,
    };
    unsigned int i;
    for (i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        if (!(*regs[i] & TAMPC_SIGNAL_LOCK_Msk)) {
            *regs[i] = TAMPC_SIGNAL_CLEAR_WRITEPROTECTION;
            *regs[i] = TAMPC_SIGNAL_OPEN;
        }
    }
#endif
}

#if defined(TZEN) && TZ_SECURE()
/* Make a single peripheral non-secure */
static void spu_periph_set_ns(uint32_t periph_s_addr)
{
    uint32_t spu_base  = SPU_BASE_FOR(periph_s_addr);
    uint32_t slave_idx = SPU_SLAVE_IDX(periph_s_addr);
    SPU_PERIPH_PERM(spu_base, slave_idx) &= ~SPU_PERIPH_PERM_SECATTR;
}

/* Mark all 32 GPIO pins of a GPIO port as non-secure */
static void spu_gpio_pins_set_ns(uint32_t spu_base, uint32_t gpio_port)
{
    uint32_t pin;
    for (pin = 0; pin < 32; pin++) {
        SPU_FEATURE_GPIO_PIN(spu_base, gpio_port, pin) &=
            ~SPU_FEATURE_SECATTR;
    }
}

/* Mark a flash/RAM region as NonSecure in the MPC.
 * start and end must be 4 KB aligned */
static void mpc_region_set_ns(uint32_t region, uint32_t start, uint32_t end)
{
    MPC_OVERRIDE_STARTADDR(region) = start;
    MPC_OVERRIDE_ENDADDR(region)   = end;
    /* set READ, WRITE, EXECUTE, don't set SECATTR, i.e. make non-secure */
    MPC_OVERRIDE_PERM(region)      = MPC_PERM_READ | MPC_PERM_WRITE |
                                     MPC_PERM_EXECUTE;
    /* apply all, including SECATTR */
    MPC_OVERRIDE_PERMMASK(region)  = MPC_PERM_READ | MPC_PERM_WRITE |
                                     MPC_PERM_EXECUTE | MPC_PERM_SECURE;
    MPC_OVERRIDE_CONFIG(region)    = MPC_CONFIG_ENABLE;
}

static void hal_tz_init(void)
{
    /* Memory must be marked as NS via both MPC and SAU. Only marking it via
     * SAU will just cause accesses from secure code to be non-secure, and the
     * MPC will restrict them because it considers the memory secure. */

    /* MPC: NS flash (boot partition only) */
    mpc_region_set_ns(0,
        WOLFBOOT_PARTITION_BOOT_ADDRESS,
        WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE);

    /* MPC: NS RAM */
    mpc_region_set_ns(1, NS_RAM_BASE, NS_RAM_BASE + NS_RAM_SIZE);


    /* SAU: NS flash (boot partition only) */
    sau_init_region(0,
        WOLFBOOT_PARTITION_BOOT_ADDRESS,
        WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 1,
        0);

    /* SAU: NSC (Non-Secure Callable) */
    sau_init_region(1,
        WOLFBOOT_NSC_ADDRESS,
        WOLFBOOT_NSC_ADDRESS + WOLFBOOT_NSC_SIZE - 1,
        1);

    /* SAU: NS RAM */
    sau_init_region(2,
        NS_RAM_BASE,
        NS_RAM_BASE + NS_RAM_SIZE - 1,
        0);

    /* Region 3: NS peripherals (covered by SPU, not MPC) */
    sau_init_region(3, 0x40000000, 0x4FFFFFFF, 0);

    /* Enable SAU and SecureFault */
    SAU_CTRL = SAU_INIT_CTRL_ENABLE;
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;
}

static void periph_unsecure(void)
{
    /* UARTE20: UART connected to JLink on nRF54l15-DK */
    spu_periph_set_ns(UARTE20_S_BASE);

    /* UARTE20 + LED1 GPIO pins */
    spu_periph_set_ns(GPIO_P1_S_BASE);
    spu_gpio_pins_set_ns(SPU20_BASE, 1);

    /* UARTE30 (download UART) */
    spu_periph_set_ns(UARTE30_S_BASE);

    /* UARTE30 pins */
    spu_periph_set_ns(GPIO_P0_S_BASE);
    spu_gpio_pins_set_ns(SPU30_BASE, 0);
}
#endif /* defined(TZEN) && TZ_SECURE() */

void hal_init(void)
{
#ifdef DEBUG_UART
    const char* bootStr = "wolfBoot HAL Init\n";
#endif

#ifdef __WOLFBOOT
    hal_handle_approtect();
    clock_init();
#endif

#if defined(TZEN) && TZ_SECURE()
    hal_tz_init();
#endif

#ifdef DEBUG_UART
    uart_init_device(DEVICE_MONITOR, 115200, 8, 'N', 1);
    uart_write(bootStr, strlen(bootStr));
#endif
}

void hal_prepare_boot(void)
{
    clock_deinit();

#if defined(TZEN) && TZ_SECURE()
    periph_unsecure();
    DSB();
    ISB();
#endif
}


#ifdef WOLFCRYPT_SECURE_MODE
void hal_trng_init(void)
{
    uint32_t state;

    CRACEN_ENABLE |= CRACEN_ENABLE_RNG_Msk;

    /* Soft-reset the RNGCONTROL block */
    CRACENCORE_RNG_CONTROL = CRACENCORE_RNG_CONTROL_SOFTRST_Msk;

    /* Configure: ring oscillator clock divider=0, init wait=512, off timer=0 */
    CRACENCORE_RNG_CLKDIV      = 0;
    CRACENCORE_RNG_INITWAITVAL = CRACENCORE_RNG_INITWAITVAL_DEFAULT;
    CRACENCORE_RNG_SWOFFTMRVAL = 0;

    /* Enable with 4 AES-128 conditioning blocks */
    CRACENCORE_RNG_CONTROL = CRACENCORE_RNG_CONTROL_ENABLE_Msk |
        (CRACENCORE_RNG_NB128BITBLOCKS_DEFAULT
         << CRACENCORE_RNG_CONTROL_NB128BITBLOCKS_Pos);

    /* Wait until FSM leaves RESET/STARTUP */
    do {
        state = (CRACENCORE_RNG_STATUS & CRACENCORE_RNG_STATUS_STATE_Msk)
                >> CRACENCORE_RNG_STATUS_STATE_Pos;
    } while (state == CRACENCORE_RNG_STATUS_STATE_RESET ||
             state == CRACENCORE_RNG_STATUS_STATE_STARTUP);
}

void hal_trng_fini(void)
{
    CRACENCORE_RNG_CONTROL = 0;
    CRACEN_ENABLE &= ~CRACEN_ENABLE_RNG_Msk;
}

int hal_trng_get_entropy(unsigned char *out, unsigned int len)
{
    unsigned int i = 0;

    while (i < len) {
        uint32_t word;
        unsigned int j;
        unsigned int avail;

        /* wait until at least one 32-bit word is available */
        while ((avail = CRACENCORE_RNG_FIFOLEVEL) == 0) {}

        /* read all available words */
        while (avail-- > 0 && i < len) {
            word = CRACENCORE_RNG_FIFO;
            for (j = 0; j < 4 && i < len; j++, i++) {
                out[i] = (unsigned char)(word & 0xFF);
                word >>= 8;
            }
        }
    }

    return 0;
}
#endif /* WOLFCRYPT_SECURE_MODE */

#endif /* TARGET_nrf54l */
