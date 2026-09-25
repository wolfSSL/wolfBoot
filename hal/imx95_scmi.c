/* imx95_scmi.c
 *
 * Minimal SCMI-over-MU client for the NXP i.MX95 Cortex-A55 (BL33).
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

/* With no U-Boot ahead of wolfBoot, clocks, pinmux and power domains are still
 * owned by the M33 System Manager and must be requested over SCMI; the pad
 * registers data-abort on direct access from BL33. This is the smallest client
 * that does it: SMT transport over MU2, then clock, pinctrl and power calls.
 * Cross-checked against U-Boot drivers/firmware/scmi, clk/clk_scmi.c and
 * pinctrl/pinctrl-scmi.c. */

#include <stdint.h>
#include "printf.h"
#include "hal/imx95_a55.h"

#if defined(IMX95_SCMI_COLD_INIT) || defined(IMX95_INIT_M7) || \
    defined(IMX95_STAGE1)

#define MU2_BASE        0x445B0000UL
#define MU2_GCR         (MU2_BASE + 0x114)   /* set BIT0: ring A2P doorbell */
#define MU2_GSR         (MU2_BASE + 0x118)   /* BIT0: GIR0 ack (write 1 to clr) */
#define SCMI_SHMEM      0x445B1000UL         /* scmi_buf0, 1 KiB */

/* SMT header offsets within the shared buffer */
#define SMT_CHAN_STATUS 0x04
#define SMT_FLAGS       0x10
#define SMT_LENGTH      0x14
#define SMT_MSG_HEADER  0x18
#define SMT_PAYLOAD     0x1C
#define CHAN_FREE       0x1UL

#define SCMI_PROTO_PERF     0x13
#define SCMI_PROTO_CLOCK    0x14
#define SCMI_PROTO_PINCTRL  0x19
#define SCMI_PROTO_POWER    0x11
#define CLOCK_RATE_SET      0x5
#define CLOCK_RATE_GET      0x6
#define CLOCK_CONFIG_SET    0x7
#define CLOCK_PARENT_SET    0xD
#define CLOCK_RATE_ROUND_CLOSEST (1UL << 3)
#define PINCTRL_CONFIG_SET  0x6
#define PWD_STATE_SET       0x4
#define PWD_STATE_GET       0x5
#define PERF_LEVEL_SET      0x7
#define PINCTRL_TYPE_MUX    192
#define PINCTRL_TYPE_CONFIG 193

/* Power domain id for the M7 mix (dt-bindings/power/fsl,imx95-power.h). */
#define IMX95_PD_DDR        12
#define IMX95_PD_M7         17

/* Performance domain 8 is the A55 cluster; level 3 is its top operating
 * point. U-Boot's set_arm_core_max_clk() uses the same pair. */
#define IMX95_PERF_DOM_ARM  8
#define IMX95_PERF_LVL_MAX  3

/* Pad ALT mode passed as the SCMI MUX value: 0 = uSDHC2 function, 5 = GPIO3. */
#define PAD_ALT_LPUART1     0
#define PAD_ALT_USDHC1      0
#define PAD_ALT_USDHC2      0
#define PAD_ALT_GPIO        5

#define IMX95_CLK_24M       2
#define IMX95_CLK_SYSPLL1_PFD1 9
#define IMX95_CLK_LPUART1   52    /* IMX95_CCM_NUM_CLK_SRC(41) + 11 */
#define IMX95_CLK_USDHC1    158   /* IMX95_CCM_NUM_CLK_SRC(41) + 117 */
#define IMX95_CLK_USDHC2    159   /* IMX95_CCM_NUM_CLK_SRC(41) + 118 */

#define SCMI_TIMEOUT        2000000

/* Build with -DIMX95_SCMI_DEBUG for a per-step trace of the cold-init. */
#ifdef IMX95_SCMI_DEBUG
#define SCMI_DBG(...) wolfBoot_printf(__VA_ARGS__)
#else
#define SCMI_DBG(...) do { } while (0)
#endif

static inline uint32_t rd(uintptr_t a) { return *(volatile uint32_t*)a; }
static inline void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t*)a = v; }

/* One synchronous SCMI command. payload[] holds n_in request words on entry
 * and n_out response words on return, word 0 being the SCMI status. The two
 * counts differ per message - POWER_STATE_GET takes one word and answers with
 * two - and sending the wrong number makes the System Manager read a field
 * from the wrong offset. Returns 0 on success. */
static int scmi_cmd(uint32_t proto, uint32_t msg_id,
                    uint32_t* payload, uint32_t n_in, uint32_t n_out)
{
    uintptr_t sh = SCMI_SHMEM;
    uint32_t n, i;

    /* Wait for the channel to be free. */
    for (n = 0; n < SCMI_TIMEOUT; n++) {
        if (rd(sh + SMT_CHAN_STATUS) & CHAN_FREE)
            break;
    }
    if (n == SCMI_TIMEOUT)
        return -1;

    wr(sh + SMT_FLAGS, 0);                       /* poll, no interrupt */
    wr(sh + SMT_LENGTH, 4 + n_in * 4);           /* header word + payload */
    wr(sh + SMT_MSG_HEADER, (proto << 10) | (msg_id & 0xFF));
    for (i = 0; i < n_in; i++)
        wr(sh + SMT_PAYLOAD + i * 4, payload[i]);

    /* Hand the channel to the SM and ring the doorbell. */
    wr(sh + SMT_CHAN_STATUS, rd(sh + SMT_CHAN_STATUS) & ~CHAN_FREE);
    wr(MU2_GCR, rd(MU2_GCR) | 0x1);

    /* The SM sets FREE again when the response is in place. */
    for (n = 0; n < SCMI_TIMEOUT; n++) {
        if (rd(sh + SMT_CHAN_STATUS) & CHAN_FREE)
            break;
    }
    if (n == SCMI_TIMEOUT)
        return -2;
    wr(MU2_GSR, 0x1);                            /* clear GIR0 ack */

    for (i = 0; i < n_out; i++)
        payload[i] = rd(sh + SMT_PAYLOAD + i * 4);
    return (int)payload[0];                      /* SCMI status, 0 = OK */
}

#if (defined(DISK_SDCARD) && defined(IMX95_SCMI_COLD_INIT)) || \
    defined(IMX95_STAGE1)
static int scmi_clock_enable(uint32_t clock_id)
{
    uint32_t p[3];
    p[0] = clock_id;
    p[1] = 1;    /* attributes: enable */
    p[2] = 0;    /* oem_config_val */
    return scmi_cmd(SCMI_PROTO_CLOCK, CLOCK_CONFIG_SET, p, 3, 1);
}

/* One pad: MUX(=mux) + CONFIG(=conf). identifier is the mux register offset/4
 * (the SM's pin numbering); function_id 0xFFFFFFFF, attributes = ncfgs<<2. */
static int scmi_pin_config(uint32_t mux_ofs, uint32_t mux, uint32_t conf)
{
    uint32_t p[7];
    p[0] = mux_ofs / 4;      /* identifier */
    p[1] = 0xFFFFFFFF;       /* function_id */
    p[2] = (uint32_t)(2 << 2); /* attributes: 2 configs */
    p[3] = PINCTRL_TYPE_MUX;    p[4] = mux;
    p[5] = PINCTRL_TYPE_CONFIG; p[6] = conf;
    return scmi_cmd(SCMI_PROTO_PINCTRL, PINCTRL_CONFIG_SET, p, 7, 1);
}

/* Three uSDHC2 signals are carrier GPIOs on the SMARC, not controller pins:
 * their pads must first be SCMI-muxed to GPIO3 (ALT5), then driven by direct
 * RGPIO3 MMIO (which is permitted from BL33). Bits from the SMARC DTS:
 *   GPIO3.0  SD2_CD_B   card detect     (input, active low)
 *   GPIO3.7  SD2_RESET_B SDIO_PWR_EN    (output high = card power on)
 *   GPIO3.19 SD2_VSELECT PMIC_SD2_VSEL  (output low = 3.3V, high = 1.8V) */
#define RGPIO3_BASE  0x43820000UL
#define RGPIO3_PDOR  (RGPIO3_BASE + 0x40)
#define RGPIO3_PDDR  (RGPIO3_BASE + 0x54)
#define GPIO3_CD     0U
#define GPIO3_PWR    7U
#define GPIO3_VSEL   19U

static void delay_loops(uint32_t loops)
{
    volatile uint32_t d = loops;
    while (d-- > 0U) { }
}

int imx95_usdhc2_cold_init(void)
{
    /* All nine uSDHC2 pads: {mux register offset, ALT mode, pad conf}. The six
     * data/clk/cmd pins take the uSDHC2 function (ALT0); the three carrier GPIO
     * signals take GPIO3 (ALT5). Values are the SMARC usdhc2/cd/pwr-en/vsel
     * pinctrl groups. */
    static const uint32_t pads[9][3] = {
        { 0x1A4, PAD_ALT_USDHC2, 0x158e }, /* SD2_CLK   */
        { 0x1A8, PAD_ALT_USDHC2, 0x138e }, /* SD2_CMD   */
        { 0x1AC, PAD_ALT_USDHC2, 0x138e }, /* SD2_DATA0 */
        { 0x1B0, PAD_ALT_USDHC2, 0x138e }, /* SD2_DATA1 */
        { 0x1B4, PAD_ALT_USDHC2, 0x138e }, /* SD2_DATA2 */
        { 0x1B8, PAD_ALT_USDHC2, 0x138e }, /* SD2_DATA3 */
        { 0x1A0, PAD_ALT_GPIO,   0x1100 }, /* SD2_CD_B    -> GPIO3.0  */
        { 0x1BC, PAD_ALT_GPIO,   0x011e }, /* SD2_RESET_B -> GPIO3.7  */
        { 0x154, PAD_ALT_GPIO,   0x0004 }, /* SD2_VSELECT -> GPIO3.19 */
    };
    int i, ret;
    uint32_t pddr, pdor;

    ret = scmi_clock_enable(IMX95_CLK_USDHC2);
    SCMI_DBG("scmi: clock enable(%d) -> %d\n", IMX95_CLK_USDHC2, ret);
    if (ret != 0) {
        wolfBoot_printf("scmi: uSDHC2 clock enable failed (%d)\n", ret);
        return ret;
    }

    for (i = 0; i < 9; i++) {
        ret = scmi_pin_config(pads[i][0], pads[i][1], pads[i][2]);
        SCMI_DBG("scmi: pad 0x%x mux %d -> %d\n",
            (unsigned)pads[i][0], (int)pads[i][1], ret);
        if (ret != 0) {
            wolfBoot_printf("scmi: pad 0x%x config failed (%d)\n",
                (unsigned)pads[i][0], ret);
            return ret;
        }
    }

    /* Card detect as input; voltage-select and power-enable as outputs. */
    pddr = rd(RGPIO3_PDDR);
    pddr &= ~(1U << GPIO3_CD);
    pddr |= (1U << GPIO3_VSEL) | (1U << GPIO3_PWR);
    wr(RGPIO3_PDDR, pddr);

    /* Select 3.3V I/O (VSEL low) before powering the card. */
    pdor = rd(RGPIO3_PDOR);
    pdor &= ~(1U << GPIO3_VSEL);
    wr(RGPIO3_PDOR, pdor);

    /* Power on, then wait past the regulator startup delay (DTS
     * startup-delay-us 20000). Caches are off here, so the loop count is
     * deliberately generous. */
    pdor |= (1U << GPIO3_PWR);
    wr(RGPIO3_PDOR, pdor);
    delay_loops(40000000U);

    SCMI_DBG("scmi: GPIO3 PDDR=0x%x PDOR=0x%x\n",
        (unsigned)rd(RGPIO3_PDDR), (unsigned)rd(RGPIO3_PDOR));
    wolfBoot_printf("scmi: uSDHC2 clock+pinmux+power up\n");
    return 0;
}
#endif /* DISK_SDCARD && IMX95_SCMI_COLD_INIT */

#ifdef IMX95_INIT_M7
/* Cortex-M7 TCM system-view bases and size (256 KiB each at TCM_SIZE=000b). */
#define IMX95_M7_ITCM_SYS   0x203C0000UL
#define IMX95_M7_DTCM_SYS   0x20400000UL
#define IMX95_M7_TCM_BYTES  0x40000UL

/* Match U-Boot's power_on_m7(): power up the M7 mix over SCMI, then scrub its
 * TCM so that never-written words carry valid ECC. U-Boot does this at board
 * init, so wolfBoot must too when it replaces U-Boot as BL33 on a board whose
 * Cortex-M7 is launched later by Linux remoteproc. */
int imx95_m7_tcm_init(void)
{
    uint32_t p[3];
    volatile uint32_t *w;
    uint32_t i, words;
    int ret;

    /* SCMI power domain: STATE_SET {flags=0, domain_id=M7, pstate=0=on}. */
    p[0] = 0;
    p[1] = IMX95_PD_M7;
    p[2] = 0;
    ret = scmi_cmd(SCMI_PROTO_POWER, PWD_STATE_SET, p, 3, 1);
    SCMI_DBG("scmi: M7 power-on -> %d\n", ret);
    if (ret != 0) {
        wolfBoot_printf("scmi: M7 power domain on failed (%d)\n", ret);
        return ret;
    }

    /* Scrub ITCM + DTCM to initialize ECC. Word writes: the TCM system view is
     * mapped Device, where an unaligned or wider access would fault. */
    words = (uint32_t)(IMX95_M7_TCM_BYTES / 4U);
    w = (volatile uint32_t *)IMX95_M7_ITCM_SYS;
    for (i = 0; i < words; i++)
        w[i] = 0U;
    w = (volatile uint32_t *)IMX95_M7_DTCM_SYS;
    for (i = 0; i < words; i++)
        w[i] = 0U;

    wolfBoot_printf("scmi: M7 powered, TCM ECC initialized\n");
    return 0;
}
#endif /* IMX95_INIT_M7 */

#ifdef IMX95_STAGE1
/* Report what the System Manager currently has a clock running at. */
static uint32_t scmi_clock_rate(uint32_t clock_id)
{
    uint32_t p[3];

    p[0] = clock_id;
    if (scmi_cmd(SCMI_PROTO_CLOCK, CLOCK_RATE_GET, p, 1, 3) != 0)
        return 0;
    return p[1];
}

/* Parent a peripheral clock and give it a rate, then enable it. Enabling alone
 * leaves whatever the previous owner set, which is invisible when a stage runs
 * after U-Boot SPL and fatal when it runs instead of it. */
static int scmi_clock_setup(uint32_t clock_id, uint32_t parent_id,
                            uint32_t rate)
{
    uint32_t p[4];
    int ret;

    p[0] = clock_id;
    p[1] = 0;                  /* attributes: off while reparenting */
    p[2] = 0;
    ret = scmi_cmd(SCMI_PROTO_CLOCK, CLOCK_CONFIG_SET, p, 3, 1);
    if (ret != 0)
        return ret;

    p[0] = clock_id;
    p[1] = parent_id;
    ret = scmi_cmd(SCMI_PROTO_CLOCK, CLOCK_PARENT_SET, p, 2, 1);
    if (ret != 0)
        return ret;

    p[0] = CLOCK_RATE_ROUND_CLOSEST;
    p[1] = clock_id;
    p[2] = rate;
    p[3] = 0;
    ret = scmi_cmd(SCMI_PROTO_CLOCK, CLOCK_RATE_SET, p, 4, 1);
    if (ret != 0)
        return ret;

    return scmi_clock_enable(clock_id);
}

/* uSDHC1 is the on-module eMMC. The ROM has already read the first container
 * through it, so this only makes the state explicit rather than depending on
 * whatever the ROM happened to leave behind. Eight data lines, no card detect
 * and no card power to switch - it is soldered down. */
int imx95_usdhc1_cold_init(void)
{
    static const uint32_t pads[11][2] = {
        { 0x128, 0x158e }, /* SD1_CLK    */
        { 0x12C, 0x138e }, /* SD1_CMD    */
        { 0x130, 0x138e }, /* SD1_DATA0  */
        { 0x134, 0x138e }, /* SD1_DATA1  */
        { 0x138, 0x138e }, /* SD1_DATA2  */
        { 0x13C, 0x138e }, /* SD1_DATA3  */
        { 0x140, 0x138e }, /* SD1_DATA4  */
        { 0x144, 0x138e }, /* SD1_DATA5  */
        { 0x148, 0x138e }, /* SD1_DATA6  */
        { 0x14C, 0x138e }, /* SD1_DATA7  */
        { 0x150, 0x158e }  /* SD1_STROBE */
    };
    uint32_t was;
    int i, ret;

    /* The divider in hal/imx95_usdhc.c is written against a 400 MHz module
     * clock, which is what U-Boot's init_clk_usdhc() sets. */
    was = scmi_clock_rate(IMX95_CLK_USDHC1);
    ret = scmi_clock_setup(IMX95_CLK_USDHC1, IMX95_CLK_SYSPLL1_PFD1,
                           400000000UL);
    if (ret != 0) {
        wolfBoot_printf("scmi: uSDHC1 clock setup failed (%d)\n", ret);
        return ret;
    }
    wolfBoot_printf("scmi: uSDHC1 clock %u -> %u Hz\n",
        (unsigned)was, (unsigned)scmi_clock_rate(IMX95_CLK_USDHC1));

    for (i = 0; i < 11; i++) {
        ret = scmi_pin_config(pads[i][0], PAD_ALT_USDHC1, pads[i][1]);
        if (ret != 0) {
            wolfBoot_printf("scmi: pad 0x%x config failed (%d)\n",
                (unsigned)pads[i][0], ret);
            return ret;
        }
    }

    wolfBoot_printf("scmi: uSDHC1 clock+pinmux up\n");
    return 0;
}

/* Parent the console UART to the 24 MHz oscillator and enable it. Stage 1 runs
 * before anything else has touched the clock tree, so the console is silent
 * until this has been done. Mirrors U-Boot's init_uart_clk(). */
int imx95_scmi_uart_clk_init(void)
{
    /* SMARC SER1 on the AONMIX LPUART1 pads, both at ALT0. */
    static const uint32_t pads[2][2] = {
        { 0x1D0, 0x31e },  /* UART1_RXD */
        { 0x1D4, 0x31e }   /* UART1_TXD */
    };
    int i, ret;

    for (i = 0; i < 2; i++) {
        ret = scmi_pin_config(pads[i][0], PAD_ALT_LPUART1, pads[i][1]);
        if (ret != 0)
            return ret;
    }

    return scmi_clock_setup(IMX95_CLK_LPUART1, IMX95_CLK_24M, 24000000UL);
}

/* Raise the A55 cluster to its maximum operating point. Nothing before stage 1
 * does this, so without it the whole boot runs at the reset rate. */
int imx95_scmi_arm_max_clk(void)
{
    uint32_t p[2];

    p[0] = IMX95_PERF_DOM_ARM;
    p[1] = IMX95_PERF_LVL_MAX;
    return scmi_cmd(SCMI_PROTO_PERF, PERF_LEVEL_SET, p, 2, 1);
}

/* 1 when the DDR mix is powered, 0 when it is off, negative if the System
 * Manager would not answer. The OEI is what brings DDR up, so an off domain
 * means it did not run and nothing loaded into DRAM would survive. */
int imx95_scmi_ddr_powered(void)
{
    uint32_t p[2];
    int ret;

    p[0] = IMX95_PD_DDR;
    ret = scmi_cmd(SCMI_PROTO_POWER, PWD_STATE_GET, p, 1, 2);
    if (ret != 0)
        return ret;
    /* Reply word 1 is the power state; bit 30 set means off. */
    return ((p[1] & (1UL << 30)) != 0UL) ? 0 : 1;
}
#endif /* IMX95_STAGE1 */

#endif /* IMX95_SCMI_COLD_INIT || IMX95_INIT_M7 || IMX95_STAGE1 */
