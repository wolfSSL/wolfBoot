/**
 * @file gpio_config.h
 *
 */

#ifndef GPIO_CONFIG_H
#define GPIO_CONFIG_H

#include <stdint.h>

#define GPIO_COMM_4_PORT_ID 0x6a
#define GPIO_COMM_0_PORT_ID (0x6e)
#define GPIO_COMM_1_PORT_ID (0x6d)
#define GPIO_COMM_5_PORT_ID (0x69)
#define GPIO_MODE_NATIVE_1 0x01
#define GPIO_MODE_GPIO (0x0)
#define GPIO_RESET_PLTRST 0x02
#define GPIO_RESET_HOSTDEEPRESET (0x01)
#define GPIO_DIR_INPUT (0x1)
#define GPIO_DIR_OUTPUT (0x2)
#define GPIO_INTERRUPT_DISABLE (0x0)
#define GPIO_INTERRUPT_SCI (1 << 2)
#define GPIO_TERM_NONE (0x0)
#define GPIO_RXEVCONF_LEVEL (0)
#define GPIO_MODE_SHIFT 10
#define GPIO_MODE_MASK (0x7) << GPIO_MODE_SHIFT
#define GPIO_RESET_SHIFT 30
#define GPIO_RESET_MASK (0x3) << GPIO_RESET_SHIFT
#define GPIO_DIR_SHIFT (0x8)
#define GPIO_DIR_MASK (0x3) << GPIO_DIR_SHIFT
#define GPIO_RXINV_SHIFT (23)
#define GPIO_RXINV_MASK (0x1) << 23
#define GPIO_INTERRUPT_SHIFT (17)
#define GPIO_INTERRUPT_MASK (0xf) << GPIO_INTERRUPT_SHIFT
#define GPIO_TERM_SHIFT (10)
#define GPIO_TERM_MASK (0xf) << GPIO_TERM_SHIFT
#define GPIO_RXEVCONF_SHIFT (25)
#define GPIO_RXEVCONF_MASK (0x3) << GPIO_RXEVCONF_SHIFT

#define GPIO_GPPC_B9_CFG_OFF (0x790)
#define GPIO_GPPC_B10_CFG_OFF (0x7a0)
#define GPIO_GPPC_C6_CFG_OFF (0x760)
#define GPIO_GPPC_C7_CFG_OFF (0x770)
#define GPIO_GPPC_C8_CFG_OFF (0x780)
#define GPIO_GPPC_C9_CFG_OFF (0x790)
#define GPIO_GPPC_C10_CFG_OFF (0x7a0)
#define GPIO_GPPC_C11_CFG_OFF (0x7b0)
#define GPIO_GPPC_C12_CFG_OFF (0x7c0)
#define GPIO_GPPC_C13_CFG_OFF (0x7d0)
#define GPIO_GPPC_C14_CFG_OFF (0x7e0)
#define GPIO_GPPC_C15_CFG_OFF (0x7f0)
#define GPIO_GPPC_C20_CFG_OFF (0x840)
#define GPIO_GPPC_C21_CFG_OFF (0x850)
#define GPIO_GPPC_C22_CFG_OFF (0x860)
#define GPIO_GPPC_D0_CFG_OFF (0x900)
#define GPIO_GPPC_D1_CFG_OFF (0x910)
#define GPIO_GPPC_D2_CFG_OFF (0x920)
#define GPIO_GPP_R0_CFG_OFF (0x700)
#define GPIO_GPP_R1_CFG_OFF (0x710)
#define GPIO_GPP_R2_CFG_OFF (0x720)
#define GPIO_GPP_R3_CFG_OFF (0x730)
#define GPIO_GPP_R4_CFG_OFF (0x740)
#define GPIO_GPP_R5_CFG_OFF (0x750)
#define GPIO_GPPC_A8_CFG_OFF (0xa20)
#define GPIO_GPPC_E12_CFG_OFF (0xb30)
#define GPIO_GPPC_E15_CFG_OFF (0xb60)
#define GPIO_GPPC_E16_CFG_OFF (0xb70)
#define GPIO_GPPC_F2_CFG_OFF (0xba0)
#define GPIO_GPPC_F4_CFG_OFF (0xbc0)
#define GPIO_GPPC_F5_CFG_OFF (0xbd0)
#define GPIO_GPPC_F9_CFG_OFF (0x910)
#define GPIO_GPPC_A9_CFG_OFF (0xa30)
#define GPIO_GPPC_T2_CFG_OFF (0x8c0)
#define GPIO_GPPC_T3_CFG_OFF (0x8d0)

#define GPIO_OWN_MASK (0x3)
/* struct tgl_gpio, enum gpio_config_flags and struct tgl_gpio_conf are defined
 * by the includer (src/x86/tgl_fsp.c); only the pad tables live here. */

#if defined (TARGET_kontron_vx3060_s2) || defined (TARGET_nai_68int6)

#if defined (TARGET_nai_68int6)
/*
 * Pad settings snapshotted from a running 68INT6 (PCH private config space,
 * stock BIOS up). An entry is the value the board runs with, not necessarily
 * one chosen by intent. The tempram/presilicon split is a judgement a snapshot
 * cannot recover: tempram carries only what the console needs (it must work
 * first), everything else is deferred to presilicon. HOSTSW_OWN and PADCFGLOCK
 * are not captured, so GPIO_SET_OWN never appears below.
 */

/* Console UART pads only. The 68INT6 console is UART2 on C20/C21 (PCI 00:19.2),
 * unlike Kontron's UART0 on C8/C9, so these pads must be muxed or
 * X86_UART_NUMBER=2 alone yields a silent console. */
static const struct tgl_gpio_conf gpio_table_tempram[] = {
    /* PAD C20  (DW0=0x44000702 DW1=0x00020122) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C20_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD C21  (DW0=0x44000700 DW1=0x00000123) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C21_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
};

/* Nothing is required between tempram and memory init on this board. */
static const struct tgl_gpio_conf gpio_table_premem[] = {
};

/* Everything else the stock firmware had programmed. Applied after silicon
 * init, where these values are at worst a no-op because they are what the
 * pads already hold. */
static const struct tgl_gpio_conf gpio_table_presilicon[] = {
    /* PAD C8  (DW0=0x44000702 DW1=0x00020176) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C8_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD C9  (DW0=0x44000700 DW1=0x00000177) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C9_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD C10  (DW0=0x44000300 DW1=0x00000018) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C10_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD C11  (DW0=0x44000300 DW1=0x00000019) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C11_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD C12  (DW0=0x44000702 DW1=0x0002011A) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C12_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD C13  (DW0=0x44000700 DW1=0x0000011B) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C13_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD C14  (DW0=0x40100102 DW1=0x0000301C) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C14_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_term = 0x0C,
     .gpio_interrupt = 0x08,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x00,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD C15  (DW0=0x84000201 DW1=0x0000001D) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C15_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* PAD F2  (DW0=0x44001600 DW1=0x00003C3F) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_F2_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = 0x05,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = 0x0F,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD A8  (DW0=0x44000B00 DW1=0x00000048) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_A8_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = 0x02,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD E12  (DW0=0x84000201 DW1=0x00000038) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_E12_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* PAD E15  (DW0=0x44000B00 DW1=0x0000003B) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_E15_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = 0x02,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD E16  (DW0=0x84000102 DW1=0x0000003C) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_E16_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* PAD F9  (DW0=0x84000201 DW1=0x0000005F) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_F9_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* PAD A9  (DW0=0x44000B00 DW1=0x00000049) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_A9_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = 0x02,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD F4  (DW0=0x44001600 DW1=0x00003C41) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_F4_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = 0x05,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = 0x0F,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD F5  (DW0=0x84000200 DW1=0x00000042) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_F5_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* PAD T2  (DW0=0x44000B00 DW1=0x00001032) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_T2_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = 0x02,
     .gpio_dir = 0x03,
     .gpio_term = 0x04,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD T3  (DW0=0x44000B00 DW1=0x00001033) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_T3_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = 0x02,
     .gpio_dir = 0x03,
     .gpio_term = 0x04,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD C22  (DW0=0x44000102 DW1=0x00000024) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C22_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD B10  (DW0=0x44000300 DW1=0x00000022) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_B10_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD B9  (DW0=0x44000300 DW1=0x00000021) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_B9_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD C6  (DW0=0x04000702 DW1=0x00000074) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C6_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = 0x00},
    /* PAD C7  (DW0=0x04000702 DW1=0x00000075) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C7_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = 0x00},
    /* PAD D1  (DW0=0x84000200 DW1=0x0000002D) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_1_PORT_ID,
             .cfg_offset = GPIO_GPPC_D1_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* PAD D0  (DW0=0x84000102 DW1=0x0000002C) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_1_PORT_ID,
             .cfg_offset = GPIO_GPPC_D0_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* PAD D2  (DW0=0x84000201 DW1=0x0000002E) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_1_PORT_ID,
             .cfg_offset = GPIO_GPPC_D2_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* PAD R0  (DW0=0x44000700 DW1=0x0003C058) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R0_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD R1  (DW0=0x44000700 DW1=0x0003FC59) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R1_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = 0x03,
     .gpio_term = 0x0F,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD R2  (DW0=0x44000600 DW1=0x0003FC5A) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R2_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = 0x0F,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD R3  (DW0=0x44000700 DW1=0x0003FC5B) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R3_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = 0x03,
     .gpio_term = 0x0F,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD R4  (DW0=0x44000700 DW1=0x0003C05C) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R4_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = 0x03,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET},
    /* PAD R5  (DW0=0x84000200 DW1=0x0000005D) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R5_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET |
          GPIO_SET_TERM | GPIO_SET_INTERRUPT | GPIO_SET_RXINV |
          GPIO_SET_RXEVCONF),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_interrupt = 0x00,
     .gpio_rxinv = 0x00,
     .gpio_rxevconf = 0x02,
     .gpio_reset = GPIO_RESET_PLTRST},
};

#else /* TARGET_kontron_vx3060_s2 */
static const struct tgl_gpio_conf gpio_table_tempram[] = {
    /* UART 0 */
    {.gpio =
         {
             /* PAD C8 */
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C8_CFG_OFF,
         },
     .flags =
         (GPIO_SET_DIRECTION | GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             /* PAD C9 */
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C9_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C10_CFG_OFF,
         },
     .flags =
         (GPIO_SET_DIRECTION | GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C11_CFG_OFF,
         },
     .flags =
         (GPIO_SET_DIRECTION | GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* UART - 1*/
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C12_CFG_OFF,
         },
     .flags =
         (GPIO_SET_DIRECTION | GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C13_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C14_CFG_OFF,
         },
     .flags =
         (GPIO_SET_DIRECTION | GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C15_CFG_OFF,
         },
     .flags =
         (GPIO_SET_DIRECTION | GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* UART - 2*/
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C20_CFG_OFF,
         },
     .flags =
         (GPIO_SET_DIRECTION | GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C21_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_reset = GPIO_RESET_PLTRST},
};

static const struct tgl_gpio_conf gpio_table_premem[] = {
};

static const struct tgl_gpio_conf gpio_table_presilicon[] = {
    /* Disable CNVi (Bluetooth Radio Interface) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_F2_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* set to GPIO mode as native functions aren't used */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_A8_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_E12_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_E15_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_E16_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},

    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_F9_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* test points */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_A9_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_F4_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_F5_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
     /* FUSA_DIAGTEST pads (T2/T3) are intentionally not programmed. */
    /* TPM */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C22_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RXINV | GPIO_SET_RESET |
          GPIO_SET_RXEVCONF | GPIO_SET_TERM | GPIO_SET_INTERRUPT),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_rxinv = 1,
     .gpio_rxevconf = GPIO_RXEVCONF_LEVEL,
     .gpio_interrupt = GPIO_INTERRUPT_SCI,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET,
     .gpio_term = GPIO_TERM_NONE},
    /* PCH I2C5 */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_B10_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_INTERRUPT | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_B9_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    /* SMLINK1 to PD */
    {/* SM1 CLK */
     .gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C6_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             /* SM1 DATA */
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C7_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    /* watchdog */
    {.gpio =
         {
             /* PLD_WDT_IRQ0 */
             .comm_port_id = GPIO_COMM_1_PORT_ID,
             .cfg_offset = GPIO_GPPC_D1_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             /* PLD_WDT_IRQ1 */
             .comm_port_id = GPIO_COMM_1_PORT_ID,
             .cfg_offset = GPIO_GPPC_D0_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET,
     .gpio_term = GPIO_TERM_NONE},
    /* wake from i225/E810/i210/M2_TOP/M2_BOT/XMC */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_1_PORT_ID,
             .cfg_offset = GPIO_GPPC_D2_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET,
     .gpio_term = GPIO_TERM_NONE},
    /* Audio, disabled */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R0_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R1_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R2_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R3_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R4_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R5_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},

};
#endif /* TARGET_nai_68int6 */

#endif /* TARGET_kontron_vx3060_s2 || TARGET_nai_68int6 */

#endif // GPIO_CONFIG_H
