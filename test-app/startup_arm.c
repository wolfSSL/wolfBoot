/* startup_arm.c
 *
 * Test bare-metal blinking led application
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

extern unsigned int _stored_data;
extern unsigned int _start_data;
extern unsigned int _end_data;
extern unsigned int _start_bss;
extern unsigned int _end_bss;
extern unsigned int _end_stack;
extern unsigned int _start_heap;

#ifdef STM32F
extern void isr_tim2(void);
#endif

#ifndef STACK_PAINTING
#define STACK_PAINTING 1
#endif

static volatile unsigned int avail_mem = 0;
#if STACK_PAINTING
static unsigned int sp;
#endif

extern void main(void);

void isr_reset(void) {
    register unsigned int *src, *dst;
    src = (unsigned int *) &_stored_data;
    dst = (unsigned int *) &_start_data;
    while (dst < (unsigned int *)&_end_data) {
        *dst = *src;
        dst++;
        src++;
    }

    dst = &_start_bss;
    while (dst < (unsigned int *)&_end_bss) {
        *dst = 0U;
        dst++;
    }

    avail_mem = &_end_stack - &_start_heap;
#if STACK_PAINTING
    {
        asm volatile("mrs %0, msp" : "=r"(sp));
        dst = ((unsigned int *)(&_end_stack)) - (8192 / sizeof(unsigned int)); ;
        while ((unsigned int)dst < sp) {
            *dst = 0xDEADC0DE;
            dst++;
        }
    }
#endif


    main();
}

void isr_fault(void)
{
    /* Panic. */
    while(1) ;;

}

void isr_memfault(void)
{
    /* Panic. */
    while(1) ;;
}

void isr_busfault(void)
{
    /* Panic. */
    while(1) ;;
}

void isr_usagefault(void)
{
    /* Panic. */
    while(1) ;;
}


void isr_empty(void)
{

}



__attribute__ ((section(".isr_vector")))
void (* const IV[])(void) =
{
	(void (*)(void))(&_end_stack),
	isr_reset,                   // Reset
	isr_fault,                   // NMI
	isr_fault,                   // HardFault
	isr_memfault,                // MemFault
	isr_busfault,                // BusFault
	isr_usagefault,              // UsageFault
	0,                           // SecureFault
	0,                          // reserved
	0,                          // reserved
	0,                          // reserved
	isr_empty,                   // SVC
	isr_empty,                   // DebugMonitor
	0,                           // reserved
	isr_empty,                   // PendSV
	isr_empty,                   // SysTick

/* Device specific IRQs for LM3S */

#ifdef LM3S
    isr_empty,                     // GPIO Port A
    isr_empty,                     // GPIO Port B
    isr_empty,                     // GPIO Port C
    isr_empty,                     // GPIO Port D
    isr_empty,                     // GPIO Port E
    isr_empty,                     // UART0 Rx and Tx
    isr_empty,                     // UART1 Rx and Tx
    isr_empty,                     // SSI0 Rx and Tx
    isr_empty,                     // I2C0 Master and Slave
    isr_empty,                     // PWM Fault
    isr_empty,                     // PWM Generator 0
    isr_empty,                     // PWM Generator 1
    isr_empty,                     // PWM Generator 2
    isr_empty,                     // Quadrature Encoder 0
    isr_empty,                     // ADC Sequence 0
    isr_empty,                     // ADC Sequence 1
    isr_empty,                     // ADC Sequence 2
    isr_empty,                     // ADC Sequence 3
    isr_empty,                     // Watchdog timer
    isr_empty,                     // Timer 0 subtimer A
    isr_empty,                     // Timer 0 subtimer B
    isr_empty,                     // Timer 1 subtimer A
    isr_empty,                     // Timer 1 subtimer B
    isr_empty,                     // Timer 2 subtimer A
    isr_empty,                     // Timer 3 subtimer B
    isr_empty,                     // Analog Comparator 0
    isr_empty,                     // Analog Comparator 1
    isr_empty,                     // Analog Comparator 2
    isr_empty,                     // System Control (PLL, OSC, BO)
    isr_empty,                     // FLASH Control
    isr_empty,                     // GPIO Port F
    isr_empty,                     // GPIO Port G
    isr_empty,                     // GPIO Port H
    isr_empty,                     // UART2 Rx and Tx
    isr_empty,                     // SSI1 Rx and Tx
    isr_empty,                     // Timer 3 subtimer A
    isr_empty,                     // Timer 3 subtimer B
    isr_empty,                     // I2C1 Master and Slave
    isr_empty,                     // Quadrature Encoder 1
    isr_empty,                     // CAN0
    isr_empty,                     // CAN1
    isr_empty,                     // CAN2
    isr_empty,                     // Ethernet
    isr_empty,                     // Hibernate
#elif PLATFORM_stm32l5
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
#elif STM32 /* For STM32F4 */
    isr_empty,              // NVIC_WWDG_IRQ 0
    isr_empty,              // PVD_IRQ 1
    isr_empty,              // TAMP_STAMP_IRQ 2
    isr_empty,              // RTC_WKUP_IRQ 3
    isr_empty,              // FLASH_IRQ 4
    isr_empty,              // RCC_IRQ 5
    isr_empty,              // EXTI0_IRQ 6
    isr_empty,              // EXTI1_IRQ 7
    isr_empty,              // EXTI2_IRQ 8
    isr_empty,              // EXTI3_IRQ 9
    isr_empty,              // EXTI4_IRQ 10
    isr_empty,              // DMA1_STREAM0_IRQ 11
    isr_empty,              // DMA1_STREAM1_IRQ 12
    isr_empty,              // DMA1_STREAM2_IRQ 13
    isr_empty,              // DMA1_STREAM3_IRQ 14
    isr_empty,              // DMA1_STREAM4_IRQ 15
    isr_empty,              // DMA1_STREAM5_IRQ 16
    isr_empty,              // DMA1_STREAM6_IRQ 17
    isr_empty,              // ADC_IRQ 18
    isr_empty,              // CAN1_TX_IRQ 19
    isr_empty,              // CAN1_RX0_IRQ 20
    isr_empty,              // CAN1_RX1_IRQ 21
    isr_empty,              // CAN1_SCE_IRQ 22
    isr_empty,              // EXTI9_5_IRQ 23
    isr_empty,              // TIM1_BRK_TIM9_IRQ 24
    isr_empty,              // TIM1_UP_TIM10_IRQ 25
    isr_empty,              // TIM1_TRG_COM_TIM11_IRQ 26
    isr_empty,              // TIM1_CC_IRQ 27
    isr_tim2,               // TIM2_IRQ 28
    isr_empty,              // TIM3_IRQ 29
    isr_empty,              // TIM4_IRQ 30
    isr_empty,              // I2C1_EV_IRQ 31
    isr_empty,              // I2C1_ER_IRQ 32
    isr_empty,              // I2C2_EV_IRQ 33
    isr_empty,              // I2C2_ER_IRQ 34
    isr_empty,              // SPI1_IRQ 35
    isr_empty,              // SPI2_IRQ 36
    isr_empty,              // USART1_IRQ 37
    isr_empty,              // USART2_IRQ 38
    isr_empty,              // USART3_IRQ 39
    isr_empty,              // EXTI15_10_IRQ 40
    isr_empty,              // RTC_ALARM_IRQ 41
    isr_empty,              // USB_FS_WKUP_IRQ 42
    isr_empty,              // TIM8_BRK_TIM12_IRQ 43
    isr_empty,              // TIM8_UP_TIM13_IRQ 44
    isr_empty,              // TIM8_TRG_COM_TIM14_IRQ 45
    isr_empty,              // TIM8_CC_IRQ 46
    isr_empty,              // DMA1_STREAM7_IRQ 47

#endif
};
