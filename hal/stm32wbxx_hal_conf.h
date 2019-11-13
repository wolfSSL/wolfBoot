#ifndef __STM32WBxx_HAL_CONF_H
#define __STM32WBxx_HAL_CONF_H
#define HAL_MODULE_ENABLED  
#define HAL_PKA_MODULE_ENABLED
//#define HAL_FLASH_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define USE_HAL_PKA_REGISTER_CALLBACKS       0u
//#include "stm32wbxx_hal_flash.h"
#include "stm32wbxx_hal_pka.h"
#include "stm32wbxx_hal_rcc.h"
#define assert_param(expr) ((void)0U)
#endif /* __STM32WBxx_HAL_CONF_H */
