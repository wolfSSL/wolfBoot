/* app_nrf54lm20.c
 *
 * Basic smoke test for the nRF54LM20 target.
 */

#include <stdint.h>
#include <string.h>

#include "target.h"
#include "wolfboot/wolfboot.h"
#include "hal/nrf54lm20.h"
#include "printf.h"

#ifndef TEST_LED_PORT
    #define TEST_LED_PORT 0
#endif
#ifndef TEST_LED_PIN
    #define TEST_LED_PIN 6
#endif

extern void hal_init(void);
extern void wolfBoot_panic(void);

#ifdef RAM_CODE

#define AIRCR *(volatile uint32_t *)(0xE000ED0C)
#define AIRCR_VKEY (0x05FA << 16)
#define AIRCR_SYSRESETREQ (1 << 2)

#define WEAKFUNCTION __attribute__((weak))

void WEAKFUNCTION RAMFUNCTION arch_reboot(void)
{
    AIRCR = AIRCR_SYSRESETREQ | AIRCR_VKEY;
    while(1)
        ;
    wolfBoot_panic();

}
#endif

static void led_toggle(void)
{
    uint32_t mask = (1U << TEST_LED_PIN);
    if (GPIO_OUT(TEST_LED_PORT) & mask)
        GPIO_OUTCLR(TEST_LED_PORT) = mask;
    else
        GPIO_OUTSET(TEST_LED_PORT) = mask;
}

void main(void)
{
    uint32_t version = wolfBoot_current_firmware_version();
    uint8_t version_bytes[sizeof(version)];
    memcpy(version_bytes, &version, sizeof(version));

    hal_init();

    GPIO_PIN_CNF(TEST_LED_PORT, TEST_LED_PIN) =
        (GPIO_CNF_OUT | GPIO_CNF_HIGH_DRIVE_0);
    GPIO_OUTCLR(TEST_LED_PORT) = (1U << TEST_LED_PIN);

    uart_init();
    uart_write("*", 1);
    for (int i = (int)(sizeof(version_bytes) - 1); i >= 0; i--) {
        uart_write((const char*)&version_bytes[i], 1);
    }

    for (;;) {
        led_toggle();
        for (volatile uint32_t n = 0; n < 1000000UL; n++)
            NOP();
    }
}
