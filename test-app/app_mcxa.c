#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "fsl_common.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "fsl_clock.h"
#include "wolfboot/wolfboot.h"


#define BOARD_LED_GPIO_PORT PORT3
#define BOARD_LED_GPIO  GPIO3
#define BOARD_LED_GPIO_PIN 12U


void gpio_init(void)
{
    /* Write to GPIO3: Peripheral clock is enabled */
    CLOCK_EnableClock(kCLOCK_GateGPIO3);
    /* Write to PORT3: Peripheral clock is enabled */
    CLOCK_EnableClock(kCLOCK_GatePORT3);
    /* GPIO3 peripheral is released from reset */
    RESET_ReleasePeripheralReset(kGPIO3_RST_SHIFT_RSTn);
    /* PORT3 peripheral is released from reset */
    RESET_ReleasePeripheralReset(kPORT3_RST_SHIFT_RSTn);

    gpio_pin_config_t LED_RED_config = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = 0U
    };
    /* Initialize GPIO functionality on pin PIO3_12 (pin 38)  */
    GPIO_PinInit(BOARD_LED_GPIO, BOARD_LED_GPIO_PIN, &LED_RED_config);

    const port_pin_config_t LED_RED = {/* Internal pull-up/down resistor is disabled */
        kPORT_PullDisable,
        /* Low internal pull resistor value is selected. */
        kPORT_LowPullResistor,
        /* Fast slew rate is configured */
        kPORT_FastSlewRate,
        /* Passive input filter is disabled */
        kPORT_PassiveFilterDisable,
        /* Open drain output is disabled */
        kPORT_OpenDrainDisable,
        /* Low drive strength is configured */
        kPORT_LowDriveStrength,
        /* Normal drive strength is configured */
        kPORT_NormalDriveStrength,
        /* Pin is configured as P3_12 */
        kPORT_MuxAlt0,
        /* Digital input enabled */
        kPORT_InputBufferEnable,
        /* Digital input is not inverted */
        kPORT_InputNormal,
        /* Pin Control Register fields [15:0] are not locked */
        kPORT_UnlockRegister};
    /* PORT3_12 (pin 38) is configured as P3_12 */
    PORT_SetPinConfig(BOARD_LED_GPIO_PORT, BOARD_LED_GPIO_PIN, &LED_RED);
}


void main(void) {
    int i = 0;
    gpio_pin_config_t led_config = {
        kGPIO_DigitalOutput, 0,
    };
    /* Write to GPIO3: Peripheral clock is enabled */
    CLOCK_EnableClock(kCLOCK_GateGPIO3);
    /* Write to PORT3: Peripheral clock is enabled */
    CLOCK_EnableClock(kCLOCK_GatePORT3);
    /* GPIO3 peripheral is released from reset */
    RESET_ReleasePeripheralReset(kGPIO3_RST_SHIFT_RSTn);
    /* PORT3 peripheral is released from reset */
    RESET_ReleasePeripheralReset(kPORT3_RST_SHIFT_RSTn);
    gpio_init();

    GPIO_PinWrite(BOARD_LED_GPIO, BOARD_LED_GPIO_PIN, 0);

    while(1)
        __WFI();
}
