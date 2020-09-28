
#include "fsl_debug_console.h"
#include "fsl_gpio.h"

#ifndef BOARD_USER_LED_GPIO
#define BOARD_USER_LED_GPIO GPIO1
#endif
#ifndef BOARD_USER_LED_GPIO_PIN
#define BOARD_USER_LED_GPIO_PIN (9U)
#endif

#define EXAMPLE_LED_GPIO     BOARD_USER_LED_GPIO
#define EXAMPLE_LED_GPIO_PIN BOARD_USER_LED_GPIO_PIN
#define EXAMPLE_DELAY_COUNT  8000000

static int g_pinSet = false;

void delay(void)
{
    volatile uint32_t i = 0;
    for (i = 0; i < EXAMPLE_DELAY_COUNT; ++i)
    {
        __asm("NOP"); /* delay */
    }
}

void main()
{
    BOARD_ConfigMPU();
    BOARD_InitPins();
    BOARD_InitBootClocks();
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000U);
    BOARD_InitDebugConsole();
    PRINTF("wolfBoot Test app, version = %\n", wolfBoot_get_image_version());
    while(1) {
        SDK_DelayAtLeastUs(100000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
        if (g_pinSet)
        {
            GPIO_PinWrite(EXAMPLE_LED_GPIO, EXAMPLE_LED_GPIO_PIN, 0U);
            g_pinSet = false;
        }
        else
        {
            GPIO_PinWrite(EXAMPLE_LED_GPIO, EXAMPLE_LED_GPIO_PIN, 1U);
            g_pinSet = true;
        }
    }

}

