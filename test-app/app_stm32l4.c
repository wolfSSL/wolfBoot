#include "led.h"
#include "hal.h"
#include "wolfboot/wolfboot.h"

#ifdef PLATFORM_stm32l4

void main(void)
{
    uint32_t boot_version;
    hal_init();
    boot_led_on();
    boot_version = wolfBoot_current_firmware_version();
    if(boot_version == 1) {
        /* Turn on Blue LED */
        boot_led_on();
        wolfBoot_update_trigger();
    } else if(boot_version >= 2) {
        /* Turn on Red LED */
        led_on();
        wolfBoot_success();
    }
    /* Wait for reboot */
  while(1) { 
  }
}
#endif /* PLATFORM_stm32l4 */
