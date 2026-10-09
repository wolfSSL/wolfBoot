/* core_cm33.h
 *
 * Host stand-in for the Arm CMSIS core header, so the HAL unit tests build
 * against a vendor's real device headers: core registers and intrinsics are
 * mock objects the tests define and inspect.
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#ifndef CORE_CM33_STUB_H
#define CORE_CM33_STUB_H

#include <stdint.h>

#define __I     volatile const
#define __O     volatile
#define __IO    volatile
#define __IM    volatile const
#define __OM    volatile
#define __IOM   volatile
#define __STATIC_INLINE static inline
#define __STATIC_FORCEINLINE static inline
#define __WEAK __attribute__((weak))
#define __ASM __asm__
#define __CORTEX_M 33U

typedef struct {
    __IOM uint32_t ISER[16];
    uint32_t RESERVED0[16];
    __IOM uint32_t ICER[16];
    uint32_t RESERVED1[16];
    __IOM uint32_t ISPR[16];
    uint32_t RESERVED2[16];
    __IOM uint32_t ICPR[16];
    uint32_t RESERVED3[16];
    __IOM uint32_t IABR[16];
    uint32_t RESERVED4[16];
    __IOM uint32_t ITNS[16];
    uint32_t RESERVED5[16];
    __IOM uint8_t IPR[496];
} NVIC_Type;

typedef struct {
    __IOM uint32_t CTRL;
    __IOM uint32_t LOAD;
    __IOM uint32_t VAL;
    __IM uint32_t CALIB;
} SysTick_Type;
#define SysTick_CTRL_ENABLE_Msk  (1UL << 0)
#define SysTick_CTRL_TICKINT_Msk (1UL << 1)
#define SysTick_LOAD_RELOAD_Msk  0xFFFFFFUL

extern NVIC_Type mock_nvic;
extern SysTick_Type mock_systick;
#define NVIC    (&mock_nvic)
#define SysTick (&mock_systick)

extern uint32_t mock_primask;
extern int mock_barriers;
static inline uint32_t __get_PRIMASK(void) { return mock_primask; }
static inline void __set_PRIMASK(uint32_t v) { mock_primask = v; }
static inline void __disable_irq(void) { mock_primask = 1; }
static inline void __enable_irq(void) { mock_primask = 0; }
static inline void __DSB(void) { mock_barriers++; }
static inline void __ISB(void) { }
static inline void __DMB(void) { }
static inline void __NOP(void) { }
static inline uint32_t __get_IPSR(void) { return 0; }

static inline void NVIC_EnableIRQ(IRQn_Type irq)
{
    NVIC->ISER[(uint32_t)irq >> 5] = 1UL << ((uint32_t)irq & 0x1FUL);
}
static inline void NVIC_DisableIRQ(IRQn_Type irq)
{
    NVIC->ICER[(uint32_t)irq >> 5] = 1UL << ((uint32_t)irq & 0x1FUL);
}
static inline void NVIC_ClearPendingIRQ(IRQn_Type irq)
{
    NVIC->ICPR[(uint32_t)irq >> 5] = 1UL << ((uint32_t)irq & 0x1FUL);
}
static inline void NVIC_SetPriority(IRQn_Type irq, uint32_t priority)
{
    NVIC->IPR[(uint32_t)irq] = (uint8_t)priority;
}
static inline void NVIC_SetTargetState(IRQn_Type irq)
{
    NVIC->ITNS[(uint32_t)irq >> 5] |= 1UL << ((uint32_t)irq & 0x1FUL);
}

#endif /* CORE_CM33_STUB_H */
