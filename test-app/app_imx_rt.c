
#include "wolfboot/wolfboot.h"
#include "fsl_common.h"
#include "fsl_clock.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"

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

void BOARD_ConfigMPU(void);
void BOARD_InitPins(void);
void BOARD_InitBootClocks(void);
void BOARD_InitDebugConsole(void);

void main()
{
    BOARD_ConfigMPU();
    BOARD_InitPins();
    BOARD_InitBootClocks();

    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000U);
    BOARD_InitDebugConsole();
    PRINTF("wolfBoot Test app, version = %d\n", wolfBoot_current_firmware_version());
    while(1) {
        SDK_DelayAtLeastUs(100000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
        if (g_pinSet)
        {
            GPIO_PinWrite(GPIO1, 9U, 0U);
            g_pinSet = false;
        }
        else
        {
            GPIO_PinWrite(GPIO1, 9U, 1U);
            g_pinSet = true;
        }
    }
}

