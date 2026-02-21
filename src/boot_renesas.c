/* boot_renesas.c
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
#include "wolfboot/wolfboot.h"
#include "hal.h"
#include "image.h"

#ifdef _RENESAS_RZN_
extern uint32_t IMAGE_APP_RAM_start;
#endif

#ifdef __RX__
#include "hal/renesas-rx.h"
#endif

#if defined(__CCRX__)
#include "r_smc_entry.h"
#endif

void RAMFUNCTION arch_reboot(void)
{
#if defined(__RX__)
    /* Disable protect registers */
	PROTECT_OFF();

	/* Issue software reset */
	SYS_SWRR = SYS_SWRR_RESET;
#endif
}


#if defined(__RX__) && defined(__CCRX__)
#pragma inline_asm longJump
static void longJump(const uint32_t *app_offset)
{
    jmp   r1;
}
#endif

/* Calls the application entry point */
void do_boot(const uint32_t *app_offset)
{
    void (*app_entry)(void);
    uint32_t app_sp;

    /* Make sure interrupts are disabled */
    /* Application may assume interrupts are disabled */
#if defined(__CCRX__)
    clrpsw_i();
#elif defined(__GNUC__)
    __builtin_rx_clrpsw('I');
#elif defined(__ICCRX__)
    __disable_interrupt();
#endif

#ifdef BOOT_ENTRY_OFFSET
    /* add byte offset to uint32_t */
    app_offset += BOOT_ENTRY_OFFSET/sizeof(uint32_t);
#endif

    (void) app_offset;
    (void) app_sp;
    (void) app_entry;

#if defined(__RX__)
    /* Do unconditional jump (r1 = app_offset) */
    #if defined(__CCRX__)
    	printf("app_offset 0x%p\n", app_offset);
        longJump(app_offset);
    #else
        app_entry = (void(*))(*app_offset);
        app_entry();
    #endif
#elif defined(_RENESAS_RA_)
    app_sp = VECTOR_SP;

    __asm__ ("ldr r3, [%0]" ::"r"(app_sp));
    __asm__ ("mov sp, r3");

    /*
     * address of Reset Handler is stored in Vector table[] that is defined in startup.c.
     * The vector for Reset Handler is placed right after Initial Stack Pointer.
     * The application assumes to start from 0x10200.
     *
     */
    app_entry = (void(*)(void))(*VECTOR_Reset_Handler);
    (*app_entry)();
#elif defined(_RENESAS_RZN_)
    app_entry = (void(*))(&IMAGE_APP_RAM_start);
    /* Jump to the application project */
    app_entry();
#else
    #error No do_boot defined!
#endif
}


/* Interrupt Handling and Linker Sections for RX */
#if defined(__RX__) && defined(__GNUC__)
typedef void (*fp) (void); /* generic interrupt function pointer typedef */
#ifdef NO_LEADING_UNDERSCORE
extern void _PowerON_Reset(void);
#else
extern void PowerON_Reset(void);
#endif

#define EXVECT_SECT __attribute__ ((section (".exvectors")))
#define FVECT_SECT  __attribute__ ((section (".fvectors")))
#define RVECT_SECT  __attribute__ ((section (".rvectors")))
#define OFS_REG     __attribute__ ((section (".ofs1"))) /* 0xFE7F5D00 */ /* MDE, OFS0, OFS1 */
#define OFS_TMINF   __attribute__ ((section (".ofs2"))) /* 0xFE7F5D10 */
#define OFS_BANKSEL __attribute__ ((section (".ofs3"))) /* 0xFE7F5D20 */
#define OFS_SPCC    __attribute__ ((section (".ofs4"))) /* 0xFE7F5D40 */
#define OFS_TMEF    __attribute__ ((section (".ofs5"))) /* 0xFE7F5D48 */
#define OFS_OSIS    __attribute__ ((section (".ofs6"))) /* 0xFE7F5D50 */
#define OFS_FAW     __attribute__ ((section (".ofs7"))) /* 0xFE7F5D64 */
#define OFS_RCP     __attribute__ ((section (".ofs8"))) /* 0xFE7F5D70 */

/* Generic Interrupt Handler */
void INT_Dummy(void) __attribute__ ((interrupt));
void INT_Dummy(void) {
    /* brk(); */
    /* wait(); */
}


const void *ExceptVectors[] EXVECT_SECT = {
#ifdef __RXv2__
//;0xffffff80  Reserved
    (fp)0,
//;0xffffff84  Reserved
    (fp)0,
//;0xffffff88  Reserved
    (fp)0,
//;0xffffff8C  Reserved
    (fp)0,
//;0xffffff90  Reserved
    (fp)0,
//;0xffffff94  Reserved
    (fp)0,
//;0xffffff98  Reserved
    (fp)0,
//;0xffffff9C  Reserved
    (fp)0,
//;0xffffffA0  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffA4  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffA8  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffAC  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffB0  Reserved
    (fp)0,
//;0xffffffB4  Reserved
    (fp)0,
//;0xffffffB8  Reserved
    (fp)0,
//;0xffffffBC  Reserved
    (fp)0,
//;0xffffffC0  Reserved
    (fp)0,
//;0xffffffC4  Reserved
    (fp)0,
//;0xffffffC8  Reserved
    (fp)0,
//;0xffffffCC  Reserved
    (fp)0,
//;0xffffffd0  Exception(Supervisor Instruction)
    INT_Dummy /* INT_Excep_SuperVisorInst */,
//;0xffffffd4  Exception(Access Instruction)
    INT_Dummy /* INT_Excep_AccessInst */,
//;0xffffffd8  Reserved
    INT_Dummy,
//;0xffffffdc  Exception(Undefined Instruction)
    INT_Dummy /* INT_Excep_UndefinedInst */,
//;0xffffffe0  Reserved
    INT_Dummy,
//;0xffffffe4  Exception(Floating Point)
    INT_Dummy /* INT_Excep_FloatingPoint */,
//;0xffffffe8  Reserved
    INT_Dummy,
//;0xffffffec  Reserved
    INT_Dummy,
//;0xfffffff0  Reserved
    INT_Dummy,
//;0xfffffff4  Reserved
    INT_Dummy,
//;0xfffffff8  NMI
    INT_Dummy /* INT_NonMaskableInterrupt */,
#else
//;0xffffff80  MDES  Endian Select Register
#ifdef __RX_LITTLE_ENDIAN__
    (fp)0xffffffff,
#endif
#ifdef __RX_BIG_ENDIAN__
    (fp)0xfffffff8,
#endif
//;0xffffff84  Reserved
    (fp)0,
//;0xffffff88  Reserved
    (fp)0xFFFFFFFF,
//;0xffffff8c  Reserved
    (fp)0xFFFFFFFF,
//;0xffffff90  Reserved
    (fp)0xFFFFFFFF,
//;0xffffff94  Reserved
    (fp)0xFFFFFFFF,
//;0xffffff98  Reserved
    (fp)0xFFFFFFFF,
//;0xffffff9c  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffa0  ID
    (fp)0xFFFFFFFF,
//;0xffffffa4  ID
    (fp)0xFFFFFFFF,
//;0xffffffa8  ID
    (fp)0xFFFFFFFF,
//;0xffffffac  ID
    (fp)0xFFFFFFFF,
//;0xffffffb0  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffb4  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffb8  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffbc  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffc0  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffc4  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffc8  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffcc  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffd0  Exception(Supervisor Instruction)
    INT_Dummy /* INT_Excep_SuperVisorInst */,
//;0xffffffd4  Exception(Access Instruction)
    INT_Dummy /* INT_Excep_AccessInst */,
//;0xffffffd8  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffdc  Exception(Undefined Instruction)
    INT_Dummy /* INT_Excep_UndefinedInst */,
//;0xffffffe0  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffe4  Exception(Floating Point)
    INT_Dummy /* INT_Excep_FloatingPoint */,
//;0xffffffe8  Reserved
    (fp)0xFFFFFFFF,
//;0xffffffec  Reserved
    (fp)0xFFFFFFFF,
//;0xfffffff0  Reserved
    (fp)0xFFFFFFFF,
//;0xfffffff4  Reserved
    (fp)0xFFFFFFFF,
//;0xfffffff8  NMI
    INT_Dummy /* INT_NonMaskableInterrupt */,
#endif /* __RXv2__ */
};

const void *HardwareVectors[] FVECT_SECT = {
#ifdef NO_LEADING_UNDERSCORE
    _PowerON_Reset
#else
    PowerON_Reset
#endif
};

const fp RelocatableVectors[] RVECT_SECT = {
//;0x0000  Reserved
    (fp)0,
//;0x0004  Reserved
    (fp)0,
//;0x0008  Reserved
    (fp)0,
//;0x000C  Reserved
    (fp)0,
//;0x0010  Reserved
    (fp)0,
//;0x0014  Reserved
    (fp)0,
//;0x0018  Reserved
    (fp)0,
//;0x001C  Reserved
    (fp)0,
//;0x0020  Reserved
    (fp)0,
//;0x0024  Reserved
    (fp)0,
//;0x0028  Reserved
    (fp)0,
//;0x002C  Reserved
    (fp)0,
//;0x0030  Reserved
    (fp)0,
//;0x0034  Reserved
    (fp)0,
//;0x0038  Reserved
    (fp)0,
//;0x003C  Reserved
    (fp)0,
//;0x0040  BSC_BUSERR
    (fp)INT_Dummy /* INT_Excep_BSC_BUSERR */,
//;0x0044  ICU_GROUPIE0
    (fp)INT_Dummy /* INT_Excep_ICU_GROUPIE0 */,
//;0x0048  RAM_RAMERR
    (fp)INT_Dummy /* INT_Excep_RAM_RAMERR */,
//;0x004C  Reserved
    (fp)0,
//;0x0050  Reserved
    (fp)0,
//;0x0054  FCU_FIFERR
    (fp)INT_Dummy /* INT_Excep_FCU_FIFERR */,
//;0x0058  Reserved
    (fp)0,
//;0x005C  FCU_FRDYI
    (fp)INT_Dummy /* INT_Excep_FCU_FRDYI */,
//;0x0060  Reserved
    (fp)0,
//;0x0064  Reserved
    (fp)0,
//;0x0068  ICU_SWINT2
    (fp)INT_Dummy /* INT_Excep_ICU_SWINT2 */,
//;0x006C  ICU_SWINT
    (fp)INT_Dummy /* INT_Excep_ICU_SWINT */,
//;0x0070  CMT0_CMI0
    (fp)INT_Dummy /* INT_Excep_CMT0_CMI0 */,
//;0x0074  CMT1_CMI1
    (fp)INT_Dummy /* INT_Excep_CMT1_CMI1 */,
//;0x0078  CMTW0_CMWI0
    (fp)INT_Dummy /* INT_Excep_CMTW0_CMWI0 */,
//;0x007C  CMTW1_CMWI1
    (fp)INT_Dummy /* INT_Excep_CMTW1_CMWI1 */,
//;0x0080  Reserved
    (fp)0,
//;0x0084  Reserved
    (fp)0,
//;0x0088  USB0_D0FIFO0
    (fp)INT_Dummy /* INT_Excep_USB0_D0FIFO0 */,
//;0x008C  USB0_D1FIFO0
    (fp)INT_Dummy /* INT_Excep_USB0_D1FIFO0 */,
//;0x0090  Reserved
    (fp)0,
//;0x0094  Reserved
    (fp)0,
//;0x0098  RSPI0_SPRI0
    (fp)INT_Dummy /* INT_Excep_RSPI0_SPRI0 */,
//;0x009C  RSPI0_SPTI0
    (fp)INT_Dummy /* INT_Excep_RSPI0_SPTI0 */,
//;0x00A0  RSPI1_SPRI1
    (fp)INT_Dummy /* INT_Excep_RSPI1_SPRI1 */,
//;0x00A4  RSPI1_SPTI1
    (fp)INT_Dummy /* INT_Excep_RSPI1_SPTI1 */,
//;0x00A8  QSPI_SPRI
    (fp)INT_Dummy /* INT_Excep_QSPI_SPRI */,
//;0x00AC  QSPI_SPTI
    (fp)INT_Dummy /* INT_Excep_QSPI_SPTI */,
//;0x00B0  SDHI_SBFAI
    (fp)INT_Dummy /* INT_Excep_SDHI_SBFAI */,
//;0x00B4  MMCIF_MBFAI
    (fp)INT_Dummy /* INT_Excep_MMCIF_MBFAI */,
//;0x00B8  SSIE0_SSITXI0
    (fp)INT_Dummy /* INT_Excep_SSIE0_SSITXI0 */,
//;0x00BC  SSIE0_SSIRXI0
    (fp)INT_Dummy /* INT_Excep_SSIE0_SSIRXI0 */,
//;0x00C0  SSIE1_SSIRTI1
    (fp)INT_Dummy /* INT_Excep_SSIE1_SSIRTI1 */,
//;0x00C4  Reserved
    (fp)0,
//;0x00C8  RIIC1_RXI1
    (fp)INT_Dummy /* INT_Excep_RIIC1_RXI1 */,
//;0x00CC  RIIC1_TXI1
    (fp)INT_Dummy /* INT_Excep_RIIC1_TXI1 */,
//;0x00D0  RIIC0_RXI0
    (fp)INT_Dummy /* INT_Excep_RIIC0_RXI0 */,
//;0x00D4  RIIC0_TXI0
    (fp)INT_Dummy /* INT_Excep_RIIC0_TXI0 */,
//;0x00D8  RIIC2_RXI2
    (fp)INT_Dummy /* INT_Excep_RIIC2_RXI2 */,
//;0x00DC  RIIC2_TXI2
    (fp)INT_Dummy /* INT_Excep_RIIC2_TXI2 */,
//;0x00E0  Reserved
    (fp)0,
//;0x00E4  Reserved
    (fp)0,
//;0x00E8  SCI0_RXI0
    (fp)INT_Dummy /* INT_Excep_SCI0_RXI0 */,
//;0x00EC  SCI0_TXI0
    (fp)INT_Dummy /* INT_Excep_SCI0_TXI0 */,
//;0x00F0  SCI1_RXI1
    (fp)INT_Dummy /* INT_Excep_SCI1_RXI1 */,
//;0x00F4  SCI1_TXI1
    (fp)INT_Dummy /* INT_Excep_SCI1_TXI1 */,
//;0x00F8  SCI2_RXI2
    (fp)INT_Dummy /* INT_Excep_SCI2_RXI2 */,
//;0x00FC  SCI2_TXI2
    (fp)INT_Dummy /* INT_Excep_SCI2_TXI2 */,
//;0x0100  ICU_IRQ0
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ0 */,
//;0x0104  ICU_IRQ1
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ1 */,
//;0x0108  ICU_IRQ2
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ2 */,
//;0x010C  ICU_IRQ3
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ3 */,
//;0x0110  ICU_IRQ4
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ4 */,
//;0x0114  ICU_IRQ5
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ5 */,
//;0x0118  ICU_IRQ6
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ6 */,
//;0x011C  ICU_IRQ7
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ7 */,
//;0x0120  ICU_IRQ8
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ8 */,
//;0x0124  ICU_IRQ9
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ9 */,
//;0x0128  ICU_IRQ10
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ10 */,
//;0x012C  ICU_IRQ11
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ11 */,
//;0x0130  ICU_IRQ12
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ12 */,
//;0x0134  ICU_IRQ13
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ13 */,
//;0x0138  ICU_IRQ14
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ14 */,
//;0x013C  ICU_IRQ15
    (fp)INT_Dummy /* INT_Excep_ICU_IRQ15 */,
//;0x0140  SCI3_RXI3
    (fp)INT_Dummy /* INT_Excep_SCI3_RXI3 */,
//;0x0144  SCI3_TXI3
    (fp)INT_Dummy /* INT_Excep_SCI3_TXI3 */,
//;0x0148  SCI4_RXI4
    (fp)INT_Dummy /* INT_Excep_SCI4_RXI4 */,
//;0x014C  SCI4_TXI4
    (fp)INT_Dummy /* INT_Excep_SCI4_TXI4 */,
//;0x0150  SCI5_RXI5
    (fp)INT_Dummy /* INT_Excep_SCI5_RXI5 */,
//;0x0154  SCI5_TXI5
    (fp)INT_Dummy /* INT_Excep_SCI5_TXI5 */,
//;0x0158  SCI6_RXI6
    (fp)INT_Dummy /* INT_Excep_SCI6_RXI6 */,
//;0x015C  SCI6_TXI6
    (fp)INT_Dummy /* INT_Excep_SCI6_TXI6 */,
//;0x0160  LVD1_LVD1
    (fp)INT_Dummy /* INT_Excep_LVD1_LVD1 */,
//;0x0164  LVD2_LVD2
    (fp)INT_Dummy /* INT_Excep_LVD2_LVD2 */,
//;0x0168  USB0_USBR0
    (fp)INT_Dummy /* INT_Excep_USB0_USBR0 */,
//;0x016C  Reserved
    (fp)0,
//;0x0170  RTC_ALM
    (fp)INT_Dummy /* INT_Excep_RTC_ALM */,
//;0x0174  RTC_PRD
    (fp)INT_Dummy /* INT_Excep_RTC_PRD */,
//;0x0178  Reserved
    (fp)0,
//;0x017C  IWDT_IWUNI
    (fp)INT_Dummy /* INT_Excep_IWDT_IWUNI */,
//;0x0180  WDT_WUNI
    (fp)INT_Dummy /* INT_Excep_WDT_WUNI */,
//;0x0184  PDC_PCDFI
    (fp)INT_Dummy /* INT_Excep_PDC_PCDFI */,
//;0x0188  SCI7_RXI7
    (fp)INT_Dummy /* INT_Excep_SCI7_RXI7 */,
//;0x018C  SCI7_TXI7
    (fp)INT_Dummy /* INT_Excep_SCI7_TXI7 */,
//;0x0190  SCI8_RXI8
    (fp)INT_Dummy /* INT_Excep_SCI8_RXI8 */,
//;0x0194  SCI8_TXI8
    (fp)INT_Dummy /* INT_Excep_SCI8_TXI8 */,
//;0x0198  SCI9_RXI9
    (fp)INT_Dummy /* INT_Excep_SCI9_RXI9 */,
//;0x019C  SCI9_TXI9
    (fp)INT_Dummy /* INT_Excep_SCI9_TXI9 */,
//;0x01A0  SCI10_RXI10
    (fp)INT_Dummy /* INT_Excep_SCI10_RXI10 */,
//;0x01A4  SCI10_TXI10
    (fp)INT_Dummy /* INT_Excep_SCI10_TXI10 */,
//;0x01A8  ICU_GROUPBE0
    (fp)INT_Dummy /* INT_Excep_ICU_GROUPBE0 */,
//;0x01AC  ICU_GROUPBL2
    (fp)INT_Dummy /* INT_Excep_ICU_GROUPBL2 */,
//;0x01B0  RSPI2_SPRI2
    (fp)INT_Dummy /* INT_Excep_RSPI2_SPRI2 */,
//;0x01B4  RSPI2_SPTI2
    (fp)INT_Dummy /* INT_Excep_RSPI2_SPTI2 */,
//;0x01B8  ICU_GROUPBL0
    (fp)INT_Dummy /* INT_Excep_ICU_GROUPBL0 */,
//;0x01BC  ICU_GROUPBL1
    (fp)INT_Dummy /* INT_Excep_ICU_GROUPBL1 */,
//;0x01C0  ICU_GROUPAL0
    (fp)INT_Dummy /* INT_Excep_ICU_GROUPAL0 */,
//;0x01C4  ICU_GROUPAL1
    (fp)INT_Dummy /* INT_Excep_ICU_GROUPAL1 */,
//;0x01C8  SCI11_RXI11
    (fp)INT_Dummy /* INT_Excep_SCI11_RXI11 */,
//;0x01CC  SCI11_TXI11
    (fp)INT_Dummy /* INT_Excep_SCI11_TXI11 */,
//;0x01D0  SCI12_RXI12
    (fp)INT_Dummy /* INT_Excep_SCI12_RXI12 */,
//;0x01D4  SCI12_TXI12
    (fp)INT_Dummy /* INT_Excep_SCI12_TXI12 */,
//;0x01D8  Reserved
    (fp)0,
//;0x01DC  Reserved
    (fp)0,
//;0x01E0  DMAC_DMAC0I
    (fp)INT_Dummy /* INT_Excep_DMAC_DMAC0I */,
//;0x01E4  DMAC_DMAC1I
    (fp)INT_Dummy /* INT_Excep_DMAC_DMAC1I */,
//;0x01E8  DMAC_DMAC2I
    (fp)INT_Dummy /* INT_Excep_DMAC_DMAC2I */,
//;0x01EC  DMAC_DMAC3I
    (fp)INT_Dummy /* INT_Excep_DMAC_DMAC3I */,
//;0x01F0  DMAC_DMAC74I
    (fp)INT_Dummy /* INT_Excep_DMAC_DMAC74I */,
//;0x01F4  OST_OSTDI
    (fp)INT_Dummy /* INT_Excep_OST_OSTDI */,
//;0x01F8  EXDMAC_EXDMAC0I
    (fp)INT_Dummy /* INT_Excep_EXDMAC_EXDMAC0I */,
//;0x01FC  EXDMAC_EXDMAC1I
    (fp)INT_Dummy /* INT_Excep_EXDMAC_EXDMAC1I */,
//;0x0200    PERIB_INTB128
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB128 */,
//;0x0204    PERIB_INTB129
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB129 */,
//;0x0208    PERIB_INTB130
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB130 */,
//;0x020C    PERIB_INTB131
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB131 */,
//;0x0210    PERIB_INTB132
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB132 */,
//;0x0214    PERIB_INTB133
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB133 */,
//;0x0218    PERIB_INTB134
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB134 */,
//;0x021C    PERIB_INTB135
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB135 */,
//;0x0220    PERIB_INTB136
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB136 */,
//;0x0224    PERIB_INTB137
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB137 */,
//;0x0228    PERIB_INTB138
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB138 */,
//;0x022C    PERIB_INTB139
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB139 */,
//;0x0230    PERIB_INTB140
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB140 */,
//;0x0234    PERIB_INTB141
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB141 */,
//;0x0238    PERIB_INTB142
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB142 */,
//;0x023C    PERIB_INTB143
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB143 */,
//;0x0240    PERIB_INTB144
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB144 */,
//;0x0244    PERIB_INTB145
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB145 */,
//;0x0248    PERIB_INTB146
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB146 */,
//;0x024C    PERIB_INTB147
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB147 */,
//;0x0250    PERIB_INTB148
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB148 */,
//;0x0254    PERIB_INTB149
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB149 */,
//;0x0258    PERIB_INTB150
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB150 */,
//;0x025C    PERIB_INTB151
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB151 */,
//;0x0260    PERIB_INTB152
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB152 */,
//;0x0264    PERIB_INTB153
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB153 */,
//;0x0268    PERIB_INTB154
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB154 */,
//;0x026C    PERIB_INTB155
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB155 */,
//;0x0270    PERIB_INTB156
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB156 */,
//;0x0274    PERIB_INTB157
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB157 */,
//;0x0278    PERIB_INTB158
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB158 */,
//;0x027C    PERIB_INTB159
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB159 */,
//;0x0280    PERIB_INTB160
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB160 */,
//;0x0284    PERIB_INTB161
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB161 */,
//;0x0288    PERIB_INTB162
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB162 */,
//;0x028C    PERIB_INTB163
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB163 */,
//;0x0290    PERIB_INTB164
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB164 */,
//;0x0294    PERIB_INTB165
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB165 */,
//;0x0298    PERIB_INTB166
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB166 */,
//;0x029C    PERIB_INTB167
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB167 */,
//;0x02A0    PERIB_INTB168
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB168 */,
//;0x02A4    PERIB_INTB169
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB169 */,
//;0x02A8    PERIB_INTB170
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB170 */,
//;0x02AC    PERIB_INTB171
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB171 */,
//;0x02B0    PERIB_INTB172
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB172 */,
//;0x02B4    PERIB_INTB173
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB173 */,
//;0x02B8    PERIB_INTB174
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB174 */,
//;0x02BC    PERIB_INTB175
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB175 */,
//;0x02C0    PERIB_INTB176
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB176 */,
//;0x02C4    PERIB_INTB177
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB177 */,
//;0x02C8    PERIB_INTB178
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB178 */,
//;0x02CC    PERIB_INTB179
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB179 */,
//;0x02D0    PERIB_INTB180
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB180 */,
//;0x02D4    PERIB_INTB181
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB181 */,
//;0x02D8    PERIB_INTB182
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB182 */,
//;0x02DC    PERIB_INTB183
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB183 */,
//;0x02E0    PERIB_INTB184
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB184 */,
//;0x02E4    PERIB_INTB185
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB185 */,
//;0x02E8    PERIB_INTB186
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB186 */,
//;0x02EC    PERIB_INTB187
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB187 */,
//;0x02F0    PERIB_INTB188
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB188 */,
//;0x02F4    PERIB_INTB189
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB189 */,
//;0x02F8    PERIB_INTB190
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB190 */,
//;0x02FC    PERIB_INTB191
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB191 */,
//;0x0300    PERIB_INTB192
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB192 */,
//;0x0304    PERIB_INTB193
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB193 */,
//;0x0308    PERIB_INTB194
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB194 */,
//;0x030C    PERIB_INTB195
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB195 */,
//;0x0310    PERIB_INTB196
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB196 */,
//;0x0314    PERIB_INTB197
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB197 */,
//;0x0318    PERIB_INTB198
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB198 */,
//;0x031C    PERIB_INTB199
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB199 */,
//;0x0320    PERIB_INTB200
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB200 */,
//;0x0324    PERIB_INTB201
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB201 */,
//;0x0328    PERIB_INTB202
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB202 */,
//;0x032C    PERIB_INTB203
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB203 */,
//;0x0330    PERIB_INTB204
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB204 */,
//;0x0334    PERIB_INTB205
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB205 */,
//;0x0338    PERIB_INTB206
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB206 */,
//;0x033C    PERIB_INTB207
    (fp)INT_Dummy /* INT_Excep_PERIB_INTB207 */,
//;0x0340    PERIA_INTA208
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA208 */,
//;0x0344    PERIA_INTA209
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA209 */,
//;0x0348    PERIA_INTA210
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA210 */,
//;0x034C    PERIA_INTA211
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA211 */,
//;0x0350    PERIA_INTA212
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA212 */,
//;0x0354    PERIA_INTA213
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA213 */,
//;0x0358    PERIA_INTA214
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA214 */,
//;0x035C    PERIA_INTA215
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA215 */,
//;0x0360    PERIA_INTA216
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA216 */,
//;0x0364    PERIA_INTA217
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA217 */,
//;0x0368    PERIA_INTA218
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA218 */,
//;0x036C    PERIA_INTA219
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA219 */,
//;0x0370    PERIA_INTA220
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA220 */,
//;0x0374    PERIA_INTA221
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA221 */,
//;0x0378    PERIA_INTA222
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA222 */,
//;0x037C    PERIA_INTA223
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA223 */,
//;0x0380    PERIA_INTA224
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA224 */,
//;0x0384    PERIA_INTA225
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA225 */,
//;0x0388    PERIA_INTA226
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA226 */,
//;0x038C    PERIA_INTA227
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA227 */,
//;0x0390    PERIA_INTA228
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA228 */,
//;0x0394    PERIA_INTA229
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA229 */,
//;0x0398    PERIA_INTA230
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA230 */,
//;0x039C    PERIA_INTA231
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA231 */,
//;0x03A0    PERIA_INTA232
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA232 */,
//;0x03A4    PERIA_INTA233
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA233 */,
//;0x03A8    PERIA_INTA234
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA234 */,
//;0x03AC    PERIA_INTA235
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA235 */,
//;0x03B0    PERIA_INTA236
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA236 */,
//;0x03B4    PERIA_INTA237
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA237 */,
//;0x03B8    PERIA_INTA238
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA238 */,
//;0x03BC    PERIA_INTA239
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA239 */,
//;0x03C0    PERIA_INTA240
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA240 */,
//;0x03C4    PERIA_INTA241
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA241 */,
//;0x03C8    PERIA_INTA242
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA242 */,
//;0x03CC    PERIA_INTA243
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA243 */,
//;0x03D0    PERIA_INTA244
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA244 */,
//;0x03D4    PERIA_INTA245
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA245 */,
//;0x03D8    PERIA_INTA246
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA246 */,
//;0x03DC    PERIA_INTA247
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA247 */,
//;0x03E0    PERIA_INTA248
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA248 */,
//;0x03E4    PERIA_INTA249
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA249 */,
//;0x03E8    PERIA_INTA250
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA250 */,
//;0x03EC    PERIA_INTA251
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA251 */,
//;0x03F0    PERIA_INTA252
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA252 */,
//;0x03F4    PERIA_INTA253
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA253 */,
//;0x03F8    PERIA_INTA254
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA254 */,
//;0x03FC    PERIA_INTA255
    (fp)INT_Dummy /* INT_Excep_PERIA_INTA255 */,
};

/* Optional inclusion of Option Setting Memory region (OFSM) */
#ifdef WOLFBOOT_RENESAS_OSM

/* MDE, OFS0, OFS1 register */
#ifdef __RX_BIG_ENDIAN__
    #define __MDE_VALUE (0xfffffff8) /* big */
#else
    #define __MDE_VALUE (0xffffffff) /* little */
#endif
#define __OFS0_VALUE (0xffffffff)
#define __OFS1_VALUE (0xffffffff)

const unsigned long __ofsm_sec_ofs1[] OFS_REG = {
    __MDE_VALUE,
    __OFS0_VALUE,
    __OFS1_VALUE,
};

/* TMINF register */
const unsigned long __TMINFreg OFS_TMINF = 0xffffffff;

/* BANKSEL register */
const unsigned long __BANKSELreg OFS_BANKSEL = 0xffffffff;

/* SPCC register */
const unsigned long __SPCCreg OFS_SPCC = 0xffffffff;

/* TMEF register */
const unsigned long __TMEFreg OFS_TMEF = 0xffffffff;

/* OSIS register (ID codes) */
const unsigned long __OSISreg[4] OFS_OSIS = {
    0xffffffff,
    0xffffffff,
    0xffffffff,
    0xffffffff,
};

/* FAW register */
const unsigned long __FAWreg OFS_FAW = 0xffffffff;

/* RCP register */
const unsigned long __RCPreg OFS_RCP = 0xffffffff;
#endif /* WOLFBOOT_RENESAS_OSM */

#endif /* __RX__ */
