/* boot_arm.c
 *
 * Copyright (C) 2020 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include <stdint.h>

#include "image.h"
#include "loader.h"
#include "wolfboot/wolfboot.h"
#if defined(PLATFORM_stm32l5) && (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)  
#include "stm32l5_partition.h"
#endif

extern unsigned int _start_text;
extern unsigned int _stored_data;
extern unsigned int _start_data;
extern unsigned int _end_data;
extern unsigned int _start_bss;
extern unsigned int _end_bss;

extern uint32_t *END_STACK;

extern void main(void);

#ifndef WOLFBOOT_NO_MPU
#define MPU_BASE (0xE000ED90)
#define MPU_TYPE            *((volatile uint32_t *)(MPU_BASE + 0x00))
#define MPU_CTRL            *((volatile uint32_t *)(MPU_BASE + 0x04))
#define MPU_RNR             *((volatile uint32_t *)(MPU_BASE + 0x08))
#define MPU_RBAR            *((volatile uint32_t *)(MPU_BASE + 0x0C))
#define MPU_RASR            *((volatile uint32_t *)(MPU_BASE + 0x10))

#define MPU_RASR_ENABLE             (1 << 0)
#define MPU_RASR_ATTR_XN            (1 << 28)
#define MPU_RASR_ATTR_AP            (7 << 24)
#define MPU_RASR_ATTR_AP_PNO_UNO    (0 << 24)
#define MPU_RASR_ATTR_AP_PRW_UNO    (1 << 24)
#define MPU_RASR_ATTR_AP_PRW_URO    (2 << 24)
#define MPU_RASR_ATTR_AP_PRW_URW    (3 << 24)
#define MPU_RASR_ATTR_AP_PRO_UNO    (5 << 24)
#define MPU_RASR_ATTR_AP_PRO_URO    (6 << 24)
#define MPU_RASR_ATTR_TEX           (7 << 19)
#define MPU_RASR_ATTR_S             (1 << 18)
#define MPU_RASR_ATTR_C             (1 << 17)
#define MPU_RASR_ATTR_B             (1 << 16)
#define MPU_RASR_ATTR_SCB           (7 << 16)

static int mpu_is_on = 0;

static void mpu_setaddr(int region, uint32_t addr)
{
    MPU_RNR = region;
    MPU_RBAR = addr;
}

static void mpu_setattr(int region, uint32_t attr)
{
    MPU_RNR = region;
    MPU_RASR = attr;
}

static void mpu_on(void)
{
    if (mpu_is_on)
        return;
    if (MPU_TYPE == 0)
        return;
    MPU_CTRL = 1;
    mpu_is_on = 1;
}

#define MPUSIZE_8K      (0x0c << 1)
#define MPUSIZE_16K     (0x0d << 1)
#define MPUSIZE_32K     (0x0e << 1)
#define MPUSIZE_64K     (0x0f << 1)
/* ... */
#define MPUSIZE_256M    (0x1b << 1)
#define MPUSIZE_512M    (0x1c << 1)
#define MPUSIZE_1G      (0x1d << 1)
#define MPUSIZE_4G      (0x1f << 1)
#define MPUSIZE_ERR     (0xff << 1)
static uint32_t mpusize(uint32_t size)
{
    if (size <= (8 * 1024))
        return MPUSIZE_8K;
    if (size <= (16 * 1024))
        return MPUSIZE_16K;
    if (size <= (32 * 1024))
        return MPUSIZE_32K;
    if (size <= (64 * 1024))
        return MPUSIZE_64K;
    return MPUSIZE_ERR;
}

static void mpu_init(void)
{
    uint32_t wolfboot_flash_size = (uint32_t)&_stored_data - (uint32_t)&_start_text;
    uint32_t wolfboot_mpusize;
    uint32_t ram_base = (uint32_t)(&_start_data);
    uint32_t flash_base = (uint32_t)(&_start_text);
    if (MPU_TYPE == 0)
        return;

    /* Read access to address space with XN */
    mpu_setaddr(0, 0);
    mpu_setattr(0, MPUSIZE_4G | MPU_RASR_ENABLE | MPU_RASR_ATTR_SCB | MPU_RASR_ATTR_AP_PRW_UNO | MPU_RASR_ATTR_XN);

    wolfboot_mpusize = mpusize(wolfboot_flash_size);
    if (wolfboot_mpusize == MPUSIZE_ERR)
        return;

    /* wolfBoot .text section in flash memory (exec OK) */
    mpu_setaddr(1, flash_base);
    mpu_setattr(1, wolfboot_mpusize | MPU_RASR_ENABLE | MPU_RASR_ATTR_SCB | MPU_RASR_ATTR_AP_PRW_UNO);

    /* Data in RAM */
    mpu_setaddr(2, ram_base);
#ifdef RAM_CODE
    mpu_setattr(2, MPUSIZE_64K | MPU_RASR_ENABLE | MPU_RASR_ATTR_SCB | MPU_RASR_ATTR_AP_PRW_UNO);
#else
    mpu_setattr(2, MPUSIZE_64K | MPU_RASR_ENABLE | MPU_RASR_ATTR_SCB | MPU_RASR_ATTR_AP_PRW_UNO | MPU_RASR_ATTR_XN);
#endif

    /* Peripherals 0x40000000:0x5FFFFFFF (512MB)*/
    mpu_setaddr(5, 0x40000000);
    mpu_setattr(5, MPUSIZE_512M | MPU_RASR_ENABLE | MPU_RASR_ATTR_S | MPU_RASR_ATTR_B | MPU_RASR_ATTR_AP_PRW_UNO | MPU_RASR_ATTR_XN);

    /* External peripherals 0xA0000000:0xCFFFFFFF (1GB)*/
    mpu_setaddr(6, 0xA0000000);
    mpu_setattr(6, MPUSIZE_1G | MPU_RASR_ENABLE | MPU_RASR_ATTR_S | MPU_RASR_ATTR_B | MPU_RASR_ATTR_AP_PRW_UNO | MPU_RASR_ATTR_XN);

    /* System control 0xE0000000:0xEFFFFFF */
    mpu_setaddr(7, 0xE0000000);
    mpu_setattr(7, MPUSIZE_256M | MPU_RASR_ENABLE | MPU_RASR_ATTR_S | MPU_RASR_ATTR_B | MPU_RASR_ATTR_AP_PRW_UNO | MPU_RASR_ATTR_XN);
    mpu_on();

}

static void mpu_off(void)
{
    mpu_is_on = 0;
    MPU_CTRL = 0;
}
#else /* NO MPU */
#define mpu_init() do{}while(0)
#define mpu_off() do{}while(0)
#endif /* NO MPU */


void isr_reset(void) {
    register unsigned int *src, *dst;
#if defined(PLATFORM_kinetis)
    /* Immediately disable Watchdog after boot */
    /*  Write Keys to unlock register */
    *((volatile unsigned short *)0x4005200E) = 0xC520;
    *((volatile unsigned short *)0x4005200E) = 0xD928;
    /* disable watchdog via STCTRLH register */
    *((volatile unsigned short *)0x40052000) = 0x01D2u;
#endif
    /* Copy the .data section from flash to RAM. */
    src = (unsigned int *) &_stored_data;
    dst = (unsigned int *) &_start_data;
    while (dst < (unsigned int *)&_end_data) {
        *dst = *src;
        dst++;
        src++;
    }

    /* Initialize the BSS section to 0 */
    dst = &_start_bss;
    while (dst < (unsigned int *)&_end_bss) {
        *dst = 0U;
        dst++;
    }

#if defined(PLATFORM_stm32l5) && (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)  
    TZ_SAU_Setup();  
#else
    mpu_init();
#endif 

    /* Run the program! */
    main();
}
void isr_fault(void)
{
    /* Panic. */
    while(1) ;;
}

void isr_empty(void)
{
    /* Ignore unmapped event and continue */
}

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)  
void isr_securefault(void)
{
    /* Panic. */
    while(1) ;;
}

#endif


/* This is the main loop for the bootloader.
 *
 * It performs the following actions:
 *  - globally disable interrutps
 *  - update the Interrupt Vector using the address of the app
 *  - Set the initial stack pointer and the offset of the app
 *  - Change the stack pointer
 *  - Call the application entry point
 *
 */
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
#define VTOR (*(volatile uint32_t *)(0xE002ED08)) // SCB_NS -> VTOR
#else
#define VTOR (*(volatile uint32_t *)(0xE000ED08))
#endif
static void  *app_entry;
static uint32_t app_end_stack;


void RAMFUNCTION do_boot(const uint32_t *app_offset)
{
    mpu_off();
#ifndef NO_VTOR
    /* Disable interrupts */
    asm volatile("cpsid i");
    /* Update IV */
    VTOR = ((uint32_t)app_offset);
#endif

    /* Get stack pointer, entry point */
    app_end_stack = (*((uint32_t *)(app_offset)));
    app_entry = (void *)(*((uint32_t *)(app_offset + 1)));

    /* Update stack pointer */
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)  
   asm volatile("msr msplim, %0" ::"r"(0));
   asm volatile("msr msp_ns, %0" ::"r"(app_end_stack)); 
#else
    asm volatile("msr msp, %0" ::"r"(app_end_stack));    
#endif
#ifndef NO_VTOR
    asm volatile("cpsie i");
#endif

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)      
    asm volatile("mov r7, %0" ::"r"(app_entry));
    asm volatile("bic.w   r7, r7, #1");
    asm volatile("blxns   r7" );/* Jump to non secure app_entry */          
#else
    /* Unconditionally jump to app_entry */
    asm volatile("mov pc, %0" ::"r"(app_entry));
#endif

}

#ifdef PLATFORM_psoc6
typedef void(*NMIHANDLER)(void);
#   define isr_NMI (NMIHANDLER)(0x0000000D)
#else
#   define isr_NMI isr_empty
#endif

__attribute__ ((section(".isr_vector")))
void (* const IV[])(void) =
{
	(void (*)(void))(&END_STACK),
	isr_reset,                   // Reset
	isr_NMI,                     // NMI
	isr_fault,                   // HardFault
	isr_fault,                   // MemFault
	isr_fault,                   // BusFault
	isr_fault,                   // UsageFault
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)  
	isr_securefault,            // SecureFault
#else
    0,                          // reserved
#endif
    0,                          // reserved
    0,                          // reserved
    0,                          // reserved
	isr_empty,                   // SVC
	isr_empty,                   // DebugMonitor
	0,                           // reserved
	isr_empty,                   // PendSV
	isr_empty,                   // SysTick
#ifdef PLATFORM_stm32l5    
    isr_empty, //	WWDG_IRQHandler
    isr_empty, //	PVD_PVM_IRQHandler
    isr_empty, //	RTC_IRQHandler
    isr_empty, //	RTC_S_IRQHandler
    isr_empty, //	TAMP_IRQHandler
    isr_empty, //	TAMP_S_IRQHandler
    isr_empty, //	FLASH_IRQHandler
    isr_empty, //	FLASH_S_IRQHandler
    isr_empty, //	GTZC_IRQHandler
    isr_empty, //	RCC_IRQHandler
    isr_empty, //	RCC_S_IRQHandler
    isr_empty, //	EXTI0_IRQHandler
    isr_empty, //	EXTI1_IRQHandler
    isr_empty, //	EXTI2_IRQHandler
    isr_empty, //	EXTI3_IRQHandler
    isr_empty, //	EXTI4_IRQHandler
    isr_empty, //	EXTI5_IRQHandler
    isr_empty, //	EXTI6_IRQHandler
    isr_empty, //	EXTI7_IRQHandler
    isr_empty, //	EXTI8_IRQHandler
    isr_empty, //	EXTI9_IRQHandler
    isr_empty, //	EXTI10_IRQHandler
    isr_empty, //	EXTI11_IRQHandler
    isr_empty, //	EXTI12_IRQHandler
    isr_empty, //	EXTI13_IRQHandler
    isr_empty, //	EXTI14_IRQHandler
    isr_empty, //	EXTI15_IRQHandler
    isr_empty, //	DMAMUX1_IRQHandler
    isr_empty, //	DMAMUX1_S_IRQHandler
    isr_empty, //	DMA1_Channel1_IRQHandler
    isr_empty, //	DMA1_Channel2_IRQHandler
    isr_empty, //	DMA1_Channel3_IRQHandler
    isr_empty, //	DMA1_Channel4_IRQHandler
    isr_empty, //	DMA1_Channel5_IRQHandler
    isr_empty, //	DMA1_Channel6_IRQHandler
    isr_empty, //	DMA1_Channel7_IRQHandler
    isr_empty, //	DMA1_Channel8_IRQHandler
    isr_empty, //	ADC1_2_IRQHandler
    isr_empty, //	DAC_IRQHandler
    isr_empty, //	FDCAN1_IT0_IRQHandler
    isr_empty, //	FDCAN1_IT1_IRQHandler
    isr_empty, //	TIM1_BRK_IRQHandler
    isr_empty, //	TIM1_UP_IRQHandler
    isr_empty, //	TIM1_TRG_COM_IRQHandler
    isr_empty, //	TIM1_CC_IRQHandler
    isr_empty, //	TIM2_IRQHandler
    isr_empty, //	TIM3_IRQHandler
    isr_empty, //	TIM4_IRQHandler
    isr_empty, //	TIM5_IRQHandler
    isr_empty, //	TIM6_IRQHandler
    isr_empty, //	TIM7_IRQHandler
    isr_empty, //	TIM8_BRK_IRQHandler
    isr_empty, //	TIM8_UP_IRQHandler
    isr_empty, //	TIM8_TRG_COM_IRQHandler
    isr_empty, //	TIM8_CC_IRQHandler
    isr_empty, //	I2C1_EV_IRQHandler
    isr_empty, //	I2C1_ER_IRQHandler
    isr_empty, //	I2C2_EV_IRQHandler
    isr_empty, //	I2C2_ER_IRQHandler
    isr_empty, //	SPI1_IRQHandler
    isr_empty, //	SPI2_IRQHandler
    isr_empty, //	USART1_IRQHandler
    isr_empty, //	USART2_IRQHandler
    isr_empty, //	USART3_IRQHandler
    isr_empty, //	UART4_IRQHandler
    isr_empty, //	UART5_IRQHandler
    isr_empty, //	LPUART1_IRQHandler
    isr_empty, //	LPTIM1_IRQHandler
    isr_empty, //	LPTIM2_IRQHandler
    isr_empty, //	TIM15_IRQHandler
    isr_empty, //	TIM16_IRQHandler
    isr_empty, //	TIM17_IRQHandler
    isr_empty, //	COMP_IRQHandler
    isr_empty, //	USB_FS_IRQHandler
    isr_empty, //	CRS_IRQHandler
    isr_empty, //	FMC_IRQHandler
    isr_empty, //	OCTOSPI1_IRQHandler
    isr_empty, //	0
    isr_empty, //	SDMMC1_IRQHandler
    isr_empty, //	0
    isr_empty, //	DMA2_Channel1_IRQHandler
    isr_empty, //	DMA2_Channel2_IRQHandler
    isr_empty, //	DMA2_Channel3_IRQHandler
    isr_empty, //	DMA2_Channel4_IRQHandler
    isr_empty, //	DMA2_Channel5_IRQHandler
    isr_empty, //	DMA2_Channel6_IRQHandler
    isr_empty, //	DMA2_Channel7_IRQHandler
    isr_empty, //	DMA2_Channel8_IRQHandler
    isr_empty, //	I2C3_EV_IRQHandler
    isr_empty, //	I2C3_ER_IRQHandler
    isr_empty, //	SAI1_IRQHandler
    isr_empty, //	SAI2_IRQHandler
    isr_empty, //	TSC_IRQHandler
    isr_empty, //	AES_IRQHandler
    isr_empty, //	RNG_IRQHandler
    isr_empty, //	FPU_IRQHandler
    isr_empty, //	HASH_IRQHandler
    isr_empty, //	PKA_IRQHandler
    isr_empty, //	LPTIM3_IRQHandler
    isr_empty, //	SPI3_IRQHandler
    isr_empty, //	I2C4_ER_IRQHandler
    isr_empty, //	I2C4_EV_IRQHandler
    isr_empty, //	DFSDM1_FLT0_IRQHandler
    isr_empty, //	DFSDM1_FLT1_IRQHandler
    isr_empty, //	DFSDM1_FLT2_IRQHandler
    isr_empty, //	DFSDM1_FLT3_IRQHandler
    isr_empty, //	UCPD1_IRQHandler
    isr_empty, //	ICACHE_IRQHandler
    isr_empty, //	OTFDEC1_IRQHandler
#else
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
    isr_empty,
#endif
};

#ifdef RAM_CODE

#define AIRCR *(volatile uint32_t *)(0xE000ED0C)
#define AIRCR_VKEY (0x05FA << 16)
#   define AIRCR_SYSRESETREQ (1 << 2)

void RAMFUNCTION arch_reboot(void)
{
    AIRCR = AIRCR_SYSRESETREQ | AIRCR_VKEY;
    while(1)
        ;

}
#endif
