/* core_cm0plus.h
 *
 * Host stand-in for the Arm CMSIS Cortex-M0+ core header, so the PSoC 6 HAL
 * unit tests build against the real Infineon PDL headers: core registers and
 * intrinsics are mock objects the tests define and inspect.
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

#ifndef CORE_CM0PLUS_STUB_H
#define CORE_CM0PLUS_STUB_H

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
#define __USED __attribute__((used))
#define __NO_RETURN __attribute__((noreturn))
#define __ALIGNED(x) __attribute__((aligned(x)))
#define __PACKED __attribute__((packed))
#define __ASM __asm__
#define __INLINE inline
#define __CORTEX_M 0U

#define _VAL2FLD(field, value) \
    (((uint32_t)(value) << field ## _Pos) & field ## _Msk)
#define _FLD2VAL(field, value) \
    (((uint32_t)(value) & field ## _Msk) >> field ## _Pos)

typedef struct {
    __IOM uint32_t ISER[1];
    uint32_t RESERVED0[31];
    __IOM uint32_t ICER[1];
    uint32_t RESERVED1[31];
    __IOM uint32_t ISPR[1];
    uint32_t RESERVED2[31];
    __IOM uint32_t ICPR[1];
    uint32_t RESERVED3[31];
    uint32_t RESERVED4[64];
    __IOM uint32_t IP[8];
} NVIC_Type;

typedef struct {
    __IM uint32_t CPUID;
    __IOM uint32_t ICSR;
    __IOM uint32_t VTOR;
    __IOM uint32_t AIRCR;
    __IOM uint32_t SCR;
    __IOM uint32_t CCR;
    uint32_t RESERVED1;
    __IOM uint32_t SHP[2];
    __IOM uint32_t SHCSR;
} SCB_Type;

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
extern SCB_Type mock_scb;
extern SysTick_Type mock_systick;
#define NVIC    (&mock_nvic)
#define SCB     (&mock_scb)
#define SysTick (&mock_systick)

extern uint32_t mock_primask;
extern int mock_barriers;
extern int mock_wfi;
static inline uint32_t __get_PRIMASK(void) { return mock_primask; }
static inline void __set_PRIMASK(uint32_t v) { mock_primask = v; }
static inline void __disable_irq(void) { mock_primask = 1; }
static inline void __enable_irq(void) { mock_primask = 0; }
static inline void __DSB(void) { mock_barriers++; }
static inline void __ISB(void) { mock_barriers++; }
static inline void __DMB(void) { mock_barriers++; }
static inline void __NOP(void) { }
void mock_wfi_hook(void);
static inline void __WFI(void) { mock_wfi++; mock_wfi_hook(); }
static inline void __WFE(void) { }
static inline void __SEV(void) { }
static inline uint32_t __get_IPSR(void) { return 0; }

static inline void NVIC_EnableIRQ(IRQn_Type irq)
{
    NVIC->ISER[0] = 1UL << ((uint32_t)irq & 0x1FUL);
}
static inline void NVIC_DisableIRQ(IRQn_Type irq)
{
    NVIC->ICER[0] = 1UL << ((uint32_t)irq & 0x1FUL);
}
static inline void NVIC_ClearPendingIRQ(IRQn_Type irq)
{
    NVIC->ICPR[0] = 1UL << ((uint32_t)irq & 0x1FUL);
}
static inline void NVIC_SetPriority(IRQn_Type irq, uint32_t priority)
{
    (void)irq;
    (void)priority;
}
static inline uint32_t NVIC_GetEnableIRQ(IRQn_Type irq)
{
    return (NVIC->ISER[0] >> ((uint32_t)irq & 0x1FUL)) & 1UL;
}
void NVIC_SystemReset(void);

#endif /* CORE_CM0PLUS_STUB_H */
