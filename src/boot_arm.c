/* boot_arm.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#include <stdint.h>

#include "image.h"
#include "loader.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"

extern unsigned int _start_text;
extern unsigned int _stored_data;
extern unsigned int _start_data;
extern unsigned int _end_data;
extern unsigned int _start_bss;
extern unsigned int _end_bss;

extern uint32_t *END_STACK;

extern void main(void);
#ifdef TARGET_va416x0
extern void SysTick_Handler(void);
#endif

#ifndef WOLFBOOT_NO_MPU

#ifndef MPU_BASE
#define MPU_BASE (0xE000ED90)
#endif
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
    uint32_t wolfboot_flash_size = (uint32_t)&_stored_data -
                                   (uint32_t)&_start_text;
    uint32_t wolfboot_mpusize;
    uint32_t ram_base = (uint32_t)(&_start_data);
    uint32_t flash_base = (uint32_t)(&_start_text);
    if (MPU_TYPE == 0)
        return;

    /* Read access to address space with XN */
    mpu_setaddr(0, 0);
    mpu_setattr(0, MPUSIZE_4G | MPU_RASR_ENABLE | MPU_RASR_ATTR_SCB |
        MPU_RASR_ATTR_AP_PRW_UNO | MPU_RASR_ATTR_XN);

    wolfboot_mpusize = mpusize(wolfboot_flash_size);
    if (wolfboot_mpusize == MPUSIZE_ERR)
        return;

    /* wolfBoot .text section in flash memory (exec OK) */
    mpu_setaddr(1, flash_base);
    mpu_setattr(1, wolfboot_mpusize | MPU_RASR_ENABLE | MPU_RASR_ATTR_SCB |
        MPU_RASR_ATTR_AP_PRW_UNO);

    /* Data in RAM */
    mpu_setaddr(2, ram_base);

    mpu_setattr(2, MPUSIZE_64K | MPU_RASR_ENABLE | MPU_RASR_ATTR_SCB |
        MPU_RASR_ATTR_AP_PRW_UNO
    #ifndef RAM_CODE
        | MPU_RASR_ATTR_XN
    #endif
    );

    /* Peripherals 0x40000000:0x5FFFFFFF (512MB)*/
    mpu_setaddr(5, 0x40000000);
    mpu_setattr(5, MPUSIZE_512M | MPU_RASR_ENABLE | MPU_RASR_ATTR_S |
        MPU_RASR_ATTR_B | MPU_RASR_ATTR_AP_PRW_UNO | MPU_RASR_ATTR_XN);

    /* External peripherals 0xA0000000:0xCFFFFFFF (1GB)*/
    mpu_setaddr(6, 0xA0000000);
    mpu_setattr(6, MPUSIZE_1G | MPU_RASR_ENABLE | MPU_RASR_ATTR_S |
        MPU_RASR_ATTR_B | MPU_RASR_ATTR_AP_PRW_UNO | MPU_RASR_ATTR_XN);

    /* System control 0xE0000000:0xEFFFFFF */
    mpu_setaddr(7, 0xE0000000);
    mpu_setattr(7, MPUSIZE_256M | MPU_RASR_ENABLE | MPU_RASR_ATTR_S |
        MPU_RASR_ATTR_B | MPU_RASR_ATTR_AP_PRW_UNO | MPU_RASR_ATTR_XN);
    mpu_on();
}

static void RAMFUNCTION mpu_off(void)
{
    mpu_is_on = 0;
    MPU_CTRL = 0;
}
#else
#define mpu_init() do{}while(0)
#define mpu_off() do{}while(0)
#endif /* !WOLFBOOT_NO_MPU */


#ifdef CORTEX_R5
#define MINITGCR   ((volatile uint32_t *)0xFFFFFF5C)
#define MSINENA    ((volatile uint32_t *)0xFFFFFF60)
#define MSTCGSTAT  ((volatile uint32_t *)0xFFFFFF68)

#define MINIDONE_FLAG  0x0100

asm(
        " .global __STACK_END\n"
        "_c_int00:\n"
        "  movw sp, __STACK_END\n"
        "  movt sp, __STACK_END\n"
        "  b isr_reset\n"
        );
#endif

void isr_reset(void) {
    register unsigned int *src, *dst;
#if defined(TARGET_kinetis)
    /* Immediately disable Watchdog after boot */
    /*  Write Keys to unlock register */
    *((volatile unsigned short *)0x4005200E) = 0xC520;
    *((volatile unsigned short *)0x4005200E) = 0xD928;
    /* disable watchdog via STCTRLH register */
    *((volatile unsigned short *)0x40052000) = 0x01D2u;
#endif

    /* init stack pointers and SRAM */
#ifdef CORTEX_R5
    /* 2.2.4.2 Auto-Initialization of On-Chip SRAM Modules */
    /* 1. Set memory self-init */
    *MINITGCR = 0xA;
    *MSTCGSTAT = 0;
    /* 2. enable self-init for L2 SRAM (see Table 2-7 of TRM) */
    *MSINENA = 0x1;
    /* 3-5. wait to complete */
    while ( (*MSTCGSTAT & MINIDONE_FLAG) != MINIDONE_FLAG)
        ;
    /* set opposite bit pattern to maximize not setting incorrectly (2.5.1.21 note) */
    *MINITGCR = 0x5;
    /* Clear global stat */
    *MSTCGSTAT = 0;

    /* init stack pointers */

    asm(
            " cps   #0x1f\n"
            "  movw sp, __STACK_END\n"
            "  movt sp, __STACK_END\n"
            );
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

    mpu_init();

    /* Run the program! */
    main();
}

#ifdef CORTEX_R5

/* forward to app handler */

/* jump to address in only parameter */
__attribute__ ((section(".text")))
const void* isr_table2[] = {
    (void*)(WOLFBOOT_PARTITION_BOOT_ADDRESS + IMAGE_HEADER_SIZE + 0x08),
    (void*)(WOLFBOOT_PARTITION_BOOT_ADDRESS + IMAGE_HEADER_SIZE + 0x08),
    (void*)(WOLFBOOT_PARTITION_BOOT_ADDRESS + IMAGE_HEADER_SIZE + 0x08),
    (void*)(WOLFBOOT_PARTITION_BOOT_ADDRESS + IMAGE_HEADER_SIZE + 0x08),
};

void isr_swi(void);
void isr_abort_prefetch(void);
void isr_abort_data(void);
void isr_reserved(void);

asm(
        "isr_table:\n"
        " .word    isr_table2"
);

asm(
        "isr_swi:"
        "  mov r1, #0x00\n"
        "  ldr r0, isr_table\n"
        "  ldr r0, [r0, r1]\n"
        "  bx r0\n");
asm(
        "isr_abort_prefetch:"
        "  mov r1, #0x04\n"
        "  ldr r0, isr_table\n"
        "  ldr r0, [r0, r1]\n"
        "  bx r0\n");
asm(
        "isr_abort_data:"
        "  mov r1, #0x08\n"
        "  ldr r0, isr_table\n"
        "  ldr r0, [r0, r1]\n"
        "  bx r0\n");
asm(
        "isr_reserved:"
        "  mov r1, #0x0c\n"
        "  ldr r0, isr_table\n"
        "  ldr r0, [r0, r1]\n"
        "  bx r0\n");



#endif /* CORTEX_R5 */

#ifdef DEBUG_HARDFAULT
__attribute__((section(".boot"))) __attribute__((used))
void HardFault_HandlerC( uint32_t *hardfault_args )
{
    /* Using volatile to prevent the compiler/linker optimizing them out */
    volatile uint32_t stacked_r0;
    volatile uint32_t stacked_r1;
    volatile uint32_t stacked_r2;
    volatile uint32_t stacked_r3;
    volatile uint32_t stacked_r12;
    volatile uint32_t stacked_lr;
    volatile uint32_t stacked_pc;
    volatile uint32_t stacked_psr;
    volatile uint32_t _CFSR;
    volatile uint32_t _HFSR;
    volatile uint32_t _DFSR;
    volatile uint32_t _AFSR;
    volatile uint32_t _BFAR;
    volatile uint32_t _MMAR;

    stacked_r0 =  ((uint32_t)hardfault_args[0]);
    stacked_r1 =  ((uint32_t)hardfault_args[1]);
    stacked_r2 =  ((uint32_t)hardfault_args[2]);
    stacked_r3 =  ((uint32_t)hardfault_args[3]);
    stacked_r12 = ((uint32_t)hardfault_args[4]);
    stacked_lr =  ((uint32_t)hardfault_args[5]);
    stacked_pc =  ((uint32_t)hardfault_args[6]);
    stacked_psr = ((uint32_t)hardfault_args[7]);

    /* Configurable Fault Status Register */
    /* Consists of MMSR, BFSR and UFSR */
    _CFSR = (*((volatile uint32_t *)(0xE000ED28)));
    /* Hard Fault Status Register */
    _HFSR = (*((volatile uint32_t *)(0xE000ED2C)));
    /* Debug Fault Status Register */
    _DFSR = (*((volatile uint32_t *)(0xE000ED30)));
    /* Auxiliary Fault Status Register */
    _AFSR = (*((volatile uint32_t *)(0xE000ED3C)));
    /* MemManage Fault Address Register */
    _MMAR = (*((volatile uint32_t *)(0xE000ED34)));
    /* Bus Fault Address Register */
    _BFAR = (*((volatile uint32_t *)(0xE000ED38)));

    wolfBoot_printf("\n\nHard fault handler (all numbers in hex):\n");
    wolfBoot_printf("R0 = %lx\n", stacked_r0);
    wolfBoot_printf("R1 = %lx\n", stacked_r1);
    wolfBoot_printf("R2 = %lx\n", stacked_r2);
    wolfBoot_printf("R3 = %lx\n", stacked_r3);
    wolfBoot_printf("R12 = %lx\n", stacked_r12);
    wolfBoot_printf("LR [R14] = %lx  subroutine call return address\n",
        stacked_lr);
    wolfBoot_printf("PC [R15] = %lx  program counter\n", stacked_pc);
    wolfBoot_printf("PSR = %lx\n", stacked_psr);
    wolfBoot_printf("CFSR = %lx\n", _CFSR);
    wolfBoot_printf("HFSR = %lx\n", _HFSR);
    wolfBoot_printf("DFSR = %lx\n", _DFSR);
    wolfBoot_printf("AFSR = %lx\n", _AFSR);
    wolfBoot_printf("MMAR = %lx\n", _MMAR);
    wolfBoot_printf("BFAR = %lx\n", _BFAR);

    /* Break into the debugger */
    __asm("BKPT #0\n");
}

__attribute__((section(".boot")))  __attribute__((naked))
void isr_fault(void)
{
    __asm volatile
    (
        " movs r0,#4      \n"  /* load bit mask into R0 */
        " mov  r1, lr     \n"  /* load link register into R1 */
        " tst r0, r1      \n"  /* compare with bitmask */
        " beq _MSP        \n"  /* if bitmask is set: stack pointer is in PSP. Otherwise in MSP */
        " mrs r0, psp     \n"  /* otherwise: stack pointer is in PSP */
        " b _GetPC        \n"  /* go to part which loads the PC */
        "_MSP:            \n"  /* stack pointer is in MSP register */
        " mrs r0, msp     \n"  /* load stack pointer into R0 */
        "_GetPC:          \n"  /* find out where the hard fault happened */
        " ldr r1,[r0,#20] \n"  /* load program counter into R1. R1 contains address of the next instruction where the hard fault happened */
        " ldr r2, =HardFault_HandlerC \n"
        " bx r2           \n"
        " bx lr           \n"  /* decode more information. R0 contains pointer to stack frame */
    );
}
#else
void isr_fault(void)
{
    /* Panic. */
    wolfBoot_panic();
}
#endif /* DEBUG_HARDFAULT */

void isr_empty(void)
{
    /* Ignore unmapped event and continue */
}




#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U) && defined(TZEN)
#   define isr_securefault isr_fault
#else
#   define isr_securefault 0
#endif

/* This is the main loop for the bootloader.
 *
 * It performs the following actions:
 *  - globally disable interrupts
 *  - update the Interrupt Vector using the address of the app
 *  - Set the initial stack pointer and the offset of the app
 *  - Change the stack pointer
 *  - Call the application entry point
 *
 */

#ifdef TZEN
#include "hal.h"
#define VTOR (*(volatile uint32_t *)(0xE002ED08))
#else
#define VTOR (*(volatile uint32_t *)(0xE000ED08))
#endif


static void  *app_entry;
static uint32_t app_end_stack;


void RAMFUNCTION do_boot(const uint32_t *app_offset)
{
#if defined(CORTEX_R5)
    (void)app_entry;
    (void)app_end_stack;
  /* limitations with TI arm compiler requires assembly */
    asm volatile("do_boot_r5:\n"
                 "  mov     pc, r0\n");

#elif defined(CORTEX_M33) /* Armv8 boot procedure */

    /* Get stack pointer, entry point */
    app_end_stack = (*((uint32_t *)(app_offset)));
    app_entry = (void *)(*((uint32_t *)(app_offset + 1)));
    /* Disable interrupts */
    asm volatile("cpsid i");

    /* Update IV */
    VTOR = ((uint32_t)app_offset);
    asm volatile("msr msplim, %0" ::"r"(0));
#   if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U) && defined(TZEN)
    asm volatile("msr msp_ns, %0" ::"r"(app_end_stack));
    /* Jump to non secure app_entry */
    asm volatile("mov r7, %0" ::"r"(app_entry));
    asm volatile("bic.w   r7, r7, #1");
    /* Re-enable interrupts to allow non-secure OS handlers */
    asm volatile("cpsie i");
    asm volatile("blxns   r7" );
#   else
    asm volatile("msr msp, %0" ::"r"(app_end_stack));
    asm volatile("mov pc, %0":: "r"(app_entry));
#   endif

#else /* Armv6/v7 boot procedure */

    mpu_off();
#   ifndef NO_VTOR
    /* Disable interrupts */
    asm volatile("cpsid i");
    /* Update IV */
    VTOR = ((uint32_t)app_offset);
#   endif
    /* Get stack pointer, entry point */
    app_end_stack = (*((uint32_t *)(app_offset)));
    app_entry = (void *)(*((uint32_t *)(app_offset + 1)));

    /* Update stack pointer */
    asm volatile("msr msp, %0" ::"r"(app_end_stack));
#   ifndef NO_VTOR
    asm volatile("cpsie i");
#   endif

    /* Unconditionally jump to app_entry */
    asm volatile("mov pc, %0" ::"r"(app_entry));
#endif
}

#ifdef TARGET_psoc6
typedef void(*NMIHANDLER)(void);
#   define isr_NMI (NMIHANDLER)(0x0000000D)
#else
#   define isr_NMI isr_empty
#endif

#ifdef CORTEX_R5
asm volatile (
"  .sect \".isr_vector\"\n"
"resetEntry:\n"
"  b   _c_int00\n"           // Reset
"  b   isr_fault\n"           // Undefined
"  b   isr_swi  \n"           // Software interrupt
"  b   isr_abort_prefetch\n"  // Abort (Prefetch)
"  b   isr_abort_data\n"      // Abort (Data)
"  b   isr_reserved\n"        // Reserved
"  ldr pc,[pc,#-0x1b0]\n"             // IRQ                                                                                           |
"  ldr pc,[pc,#-0x1b0]\n"             // FIQ
              );

#else
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
    isr_securefault,             // SecureFault on M23/33, reserved otherwise (0)
    0,                           // reserved
    0,                           // reserved
    0,                           // reserved
    isr_empty,                   // SVC
    isr_empty,                   // DebugMonitor
    0,                           // reserved
    isr_empty,                   // PendSV

#ifdef TARGET_va416x0
    SysTick_Handler,             // SysTick
#else
    isr_empty,                   // SysTick
#endif

    /* Fill with extra unused handlers */
#if defined(TARGET_stm32l5) || defined(TARGET_stm32u5) || \
    defined(TARGET_stm32h7) || defined(TARGET_rp2350)
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
#endif

#ifdef RAM_CODE

#ifdef CORTEX_R5
  // Section 2.5.1.45 of spnu563A
#  define SYSECR    *((volatile uint32_t *)0xFFFFFFE0)
#  define ECR_RESET (1 << 15)
#else
#  define AIRCR *(volatile uint32_t *)(0xE000ED0C)
#  define AIRCR_VKEY (0x05FA << 16)
#  define AIRCR_SYSRESETREQ (1 << 2)
#endif

void RAMFUNCTION arch_reboot(void)
{
#ifdef CORTEX_R5
    SYSECR = ECR_RESET;
#else
    AIRCR = AIRCR_SYSRESETREQ | AIRCR_VKEY;
#endif
    while(1)
        ;
    wolfBoot_panic();
}
#endif /* RAM_CODE */
