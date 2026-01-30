#include <wolfHAL/platform/st/stm32wb55xx.h>

whal_StRcc_PeriphClk periphClkEn[] =
{
    WHAL_ST_RCC_PERIPH_GPIOA,
    WHAL_ST_RCC_PERIPH_GPIOB,
    WHAL_ST_RCC_PERIPH_LPUART1,
    WHAL_ST_RCC_PERIPH_FLASH,
};

whal_Clock wbClock = {
    WHAL_STM32WB55_RCC_DEVICE,

    .cfg = &(whal_StRcc_Cfg){
        .sysClkSrc = WHAL_ST_RCC_SYSCLK_SRC_PLL,
        .sysClkCfg.pll =
        {
            .clkSrc = WHAL_ST_RCC_PLLCLK_SRC_MSI,
            /* 64 MHz */
            .n = 32,
            .m = 0,
            .r = 1,
            .q = 0,
            .p = 0,
        },
        .periphClkEn = periphClkEn, 
        .periphClkEnCount = sizeof(periphClkEn) / sizeof(whal_StRcc_PeriphClk),
    },
};

whal_Gpio wbGpio = {
    WHAL_STM32WB55_GPIO_DEVICE,

    .pinCfg = &(whal_StGpio_Cfg[3]){
        { /* LED */
            .port = WHAL_STGPIO_PORT_B,
            .pin = 5,
            .mode = WHAL_STGPIO_MODE_OUT,
            .outType = WHAL_STGPIO_OUTTYPE_PUSHPULL,
            .speed = WHAL_STGPIO_SPEED_LOW,
            .pull = WHAL_STGPIO_PULL_UP,
            .altFn = 0,
        },
        { /* LPUART1 TX */
            .port = WHAL_STGPIO_PORT_A,
            .pin = 2,
            .mode = WHAL_STGPIO_MODE_ALTFN,
            .outType = WHAL_STGPIO_OUTTYPE_PUSHPULL,
            .speed = WHAL_STGPIO_SPEED_FAST,
            .pull = WHAL_STGPIO_PULL_UP,
            .altFn = 8,
        },
        { /* LPUART1 RX */
            .port = WHAL_STGPIO_PORT_A,
            .pin = 3,
            .mode = WHAL_STGPIO_MODE_ALTFN,
            .outType = WHAL_STGPIO_OUTTYPE_PUSHPULL,
            .speed = WHAL_STGPIO_SPEED_FAST,
            .pull = WHAL_STGPIO_PULL_UP,
            .altFn = 8,
        },
    },
    .pinCount = 3,
};

whal_Uart wbUart = {
    WHAL_STM32WB55_LPUART1_DEVICE,

    .cfg = &(whal_StUart_Cfg){
        .baud = 115200,
        .sysClk = &wbClock,
    },
};

whal_Flash wbFlash = {
    WHAL_STM32WB55_FLASH_DEVICE,

    .cfg = &(whal_StFlash_Cfg) {
        .startAddr = 0x08000000,
        .size = 0x100000,
    },
};

void hal_pre_init()
{
    /* The flash read latency needs to be set prior to 
     * calling whal_Clock_Enable in hal_init() */

    whal_StFlash_SetLatencyArgs wbFlashLatency = {
        .latency = WHAL_ST_FLASH_LATENCY_3
    };
    whal_Flash_Cmd(&wbFlash, WHAL_ST_FLASH_CMD_SET_LATENCY, &wbFlashLatency);
}

void hal_post_prepare_boot()
{
    whal_StFlash_SetLatencyArgs wbFlashLatency = {
        .latency = WHAL_ST_FLASH_LATENCY_0
    };
    whal_Flash_Cmd(&wbFlash, WHAL_ST_FLASH_CMD_SET_LATENCY, &wbFlashLatency);
}
