/* tgl_fsp.c
 * Copyright (C) 2023 wolfSSL Inc.
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
 *
 *
 * Machine dependent code for TigerLake x86 with FSP
 */
/**
 * @file tgl_fsp.c
 *
 * @brief Machine dependent code for TigerLake x86 with FSP
 *
 * This file contains machine dependent code for TigerLake x86 platform
 * with Firmware Support Package (FSP).
 */

#ifndef TGL_FSP_H
#define TGL_FSP_H

#include <stdint.h>
#include <stddef.h>
#include <x86/hob.h>
#include <pci.h>
#include <x86/mptable.h>
#include <uart_drv.h>
#include <printf.h>
#include <string.h>

#include <x86/common.h>
#include <x86/fsp/FsptUpd.h>
#include <x86/fsp/FspmUpd.h>
#include <x86/fsp/FspsUpd.h>
#include <x86/ahci.h>
#include <stage2_params.h>

#define PCR_DMI_PORT_ID 0x88
#define PCR_DMI_LPCLGIR1 0x2730

#define PCI_ESPI_LGIR1 (0x84)
#define PCI_ESPI_BUS 0x0
#define PCI_ESPI_DEV 0x1f
#define PCI_ESPI_FUN 0x0

#define CODE_REGION_BASE 0x0
#define CODE_REGION_SIZE 0x1000

#define ENDLINE "\r\n"

#define TCO_BASE_ADDRESS             0x400
#define PCH_PWRM_BASE_ADDRESS 0xFE000000
#define R_PMC_PWRM_GEN_PMCON_A 0x1020
#define B_PMC_PWRM_GEN_PMCON_A_DISB (1 << 23)
#define IO_APIC_IDX 0xfec00000
#define IO_APIC_DAT 0xfec00010
#define CPLD_ADDRESS 0x800
#define CPLD_LENGTH 0x100
#define ECE1200_TEMP_ADDRESS_ALIGNED 0x80
#define ECE1200_INDEX 0x8c
#define ECE1200_DATA (ECE1200_INDEX+1)
#define ECE1200_LENGTH 0x10
#define CPLD_ID 0x0
#define CPLD_BOARD_ID 0x1
#define CPLD_WATCHDOG 0x55
#define CPLD_IPMI 0x86
#define CPLD_SERIAL_LINES_CTL 0x07
#define CPLD_SERIAL1_TXEN (1<<2)
#define GPIO_COMM_4_PORT_ID 0x6a
#define GPIO_COMM_0_PORT_ID (0x6e)
#define GPIO_COMM_1_PORT_ID (0x6d)
#define GPIO_COMM_5_PORT_ID (0x69)
#define GPIO_MODE_NATIVE_1 0x01
#define GPIO_MODE_GPIO (0x0)
#define GPIO_RESET_PLTRST 0x02
#define GPIO_RESET_HOSTDEEPRESET (0x01)
#define GPIO_DIR_INPUT (0x1)
#define GPIO_DIR_OUTPUT (0x2)
#define GPIO_INTERRUPT_DISABLE (0x0)
#define GPIO_INTERRUPT_SCI (1 << 2)
#define GPIO_TERM_NONE (0x0)
#define GPIO_RXEVCONF_LEVEL (0)
#define GPIO_MODE_SHIFT 10
#define GPIO_MODE_MASK (0x7) << GPIO_MODE_SHIFT
#define GPIO_RESET_SHIFT 30
#define GPIO_RESET_MASK (0x3) << GPIO_RESET_SHIFT
#define GPIO_DIR_SHIFT (0x8)
#define GPIO_DIR_MASK (0x3) << GPIO_DIR_SHIFT
#define GPIO_RXINV_SHIFT (23)
#define GPIO_RXINV_MASK (0x1) << 23
#define GPIO_INTERRUPT_SHIFT (17)
#define GPIO_INTERRUPT_MASK (0xf) << GPIO_INTERRUPT_SHIFT
#define GPIO_TERM_SHIFT (10)
#define GPIO_TERM_MASK (0xf) << GPIO_TERM_SHIFT
#define GPIO_RXEVCONF_SHIFT (25)
#define GPIO_RXEVCONF_MASK (0x3) << GPIO_RXEVCONF_SHIFT
#define PCR_INTERRUPT_PORT_ID (0xc4)
#define GIC_OFFSET (0x31FC)
#define GIC_SHUTDOWN_STATUS_BIT (1 << 0)


#define GPIO_GPPC_B9_CFG_OFF (0x790)
#define GPIO_GPPC_B10_CFG_OFF (0x7a0)
#define GPIO_GPPC_C6_CFG_OFF (0x760)
#define GPIO_GPPC_C7_CFG_OFF (0x770)
#define GPIO_GPPC_C8_CFG_OFF (0x780)
#define GPIO_GPPC_C9_CFG_OFF (0x790)
#define GPIO_GPPC_C10_CFG_OFF (0x7a0)
#define GPIO_GPPC_C11_CFG_OFF (0x7b0)
#define GPIO_GPPC_C12_CFG_OFF (0x7c0)
#define GPIO_GPPC_C13_CFG_OFF (0x7d0)
#define GPIO_GPPC_C14_CFG_OFF (0x7e0)
#define GPIO_GPPC_C15_CFG_OFF (0x7f0)
#define GPIO_GPPC_C20_CFG_OFF (0x840)
#define GPIO_GPPC_C21_CFG_OFF (0x850)
#define GPIO_GPPC_C22_CFG_OFF (0x860)
#define GPIO_GPPC_D0_CFG_OFF (0x900)
#define GPIO_GPPC_D1_CFG_OFF (0x910)
#define GPIO_GPPC_D2_CFG_OFF (0x920)
#define GPIO_GPP_R0_CFG_OFF (0x700)
#define GPIO_GPP_R1_CFG_OFF (0x710)
#define GPIO_GPP_R2_CFG_OFF (0x720)
#define GPIO_GPP_R3_CFG_OFF (0x730)
#define GPIO_GPP_R4_CFG_OFF (0x740)
#define GPIO_GPP_R5_CFG_OFF (0x750)
#define GPIO_GPPC_A8_CFG_OFF (0xa20)
#define GPIO_GPPC_E12_CFG_OFF (0xb30)
#define GPIO_GPPC_E15_CFG_OFF (0xb60)
#define GPIO_GPPC_E16_CFG_OFF (0xb70)
#define GPIO_GPPC_F2_CFG_OFF (0xba0)
#define GPIO_GPPC_F4_CFG_OFF (0xbc0)
#define GPIO_GPPC_F5_CFG_OFF (0xbd0)
#define GPIO_GPPC_F9_CFG_OFF (0x910)
#define GPIO_GPPC_A9_CFG_OFF (0xa30)
#define GPIO_GPPC_T2_CFG_OFF (0x8c0)
#define GPIO_GPPC_T3_CFG_OFF (0x8d0)

#ifdef DEBUG_GPIO
#define GPIO_DEBUG_PRINTF(...) wolfBoot_printf(__VA_ARGS__)
#else
#define GPIO_DEBUG_PRINTF(...) do {} while(0)
#endif /* DEBUG_GPIO */

SI_PCH_DEVICE_INTERRUPT_CONFIG mPchHDevIntConfig[] = {
    {30, 0, SiPchIntA, 16},
};

#if defined(BUILD_LOADER_STAGE1)
#define FIT_NUM_ENTRIES 2
__attribute__((__section__(".boot"))) const struct fit_table_entry fit_table[FIT_NUM_ENTRIES] =
{
    {
        .address = 0x2020205F5449465F,
        .size_split_16_lo = FIT_NUM_ENTRIES,
        .size_split_16_hi = 0,
        .reserved = 0,
        .version = 0x100,
        .type = FIT_ENTRY_FIT_HEADER,
        .checksum_valid = 0,
        .checksum = 0
    },
    {
        .address = UCODE0_ADDRESS,
        .size_split_16_lo = 0,
        .size_split_16_hi = 0,
        .reserved = 0,
        .version = 0x100,
        .type = FIT_ENTRY_UCODE_UPDATE,
        .checksum_valid = 0,
        .checksum = 0
    },
};

__attribute__((__section__(".jmpto32"))) const
FSPT_UPD TempRamInitParams = {
  .FspUpdHeader = {
    .Signature = FSPT_UPD_SIGNATURE,
    .Revision  = 0,
    .Reserved  = {0},
  },
  .FsptCoreUpd = {
    .MicrocodeRegionBase   = 0x0,
    .MicrocodeRegionSize   = 0x0,
    /* start of bootloader in memorypmapped cache */
    .CodeRegionBase        = 0xffa50000,
    .CodeRegionSize        = 0xc00000,
    .Reserved              = {0},
  },
  .FsptConfig = {
    .PcdSerialIoUartDebugEnable = 1,
    .PcdSerialIoUartNumber      = 0,
    .PcdSerialIoUartMode        = 1,
    .PcdSerialIoUartBaudRate    = 115200,
    .PcdPciExpressBaseAddress   = PCI_ECAM_BASE,
    .PcdPciExpressRegionLength  = 0x10000000,
    .PcdSerialIoUartParity      = 1, // NoParity
    .PcdSerialIoUartDataBits    = 8,
    .PcdSerialIoUartStopBits    = 1,
    .PcdSerialIoUartAutoFlow    = 0,
    .PcdSerialIoUartRxPinMux    = 0,
    .PcdSerialIoUartTxPinMux    = 0,
    .PcdSerialIoUartRtsPinMux   = 0,
    .PcdSerialIoUartCtsPinMux   = 0,
    .PcdLpcUartDebugEnable      = 1,
    .PcdSerialIoUartDebugMmioBase = X86_UART_BASE,
  },
  .UpdTerminator = 0x55AA,
};

uint32_t smbus_address_table[] = { 0xa2, 0xa0, 0xa2, 0xa0 };

#define FSPM_UDP_BASE 0xffe36000

static int fsp_set_memory_cfg(FSPM_UPD *udp)
{
    FSP_M_CONFIG *mem_cfg;

    mem_cfg = &udp->FspmConfig;
    mem_cfg->PlatformMemorySize = 93806592;
    mem_cfg->DqMapCpu2DramMc0Ch0[0] = 13;
    mem_cfg->DqMapCpu2DramMc0Ch0[1] = 12;
    mem_cfg->DqMapCpu2DramMc0Ch0[2] = 14;
    mem_cfg->DqMapCpu2DramMc0Ch0[3] = 15;
    mem_cfg->DqMapCpu2DramMc0Ch0[4] = 11;
    mem_cfg->DqMapCpu2DramMc0Ch0[5] = 8;
    mem_cfg->DqMapCpu2DramMc0Ch0[6] = 9;
    mem_cfg->DqMapCpu2DramMc0Ch0[7] = 10;
    mem_cfg->DqMapCpu2DramMc0Ch0[8] = 2;
    mem_cfg->DqMapCpu2DramMc0Ch0[9] = 3;
    mem_cfg->DqMapCpu2DramMc0Ch0[10] = 1;
    mem_cfg->DqMapCpu2DramMc0Ch0[11] = 0;
    mem_cfg->DqMapCpu2DramMc0Ch0[12] = 7;
    mem_cfg->DqMapCpu2DramMc0Ch0[13] = 4;
    mem_cfg->DqMapCpu2DramMc0Ch0[14] = 5;
    mem_cfg->DqMapCpu2DramMc0Ch0[15] = 6;
    mem_cfg->DqMapCpu2DramMc0Ch1[0] = 11;
    mem_cfg->DqMapCpu2DramMc0Ch1[1] = 12;
    mem_cfg->DqMapCpu2DramMc0Ch1[2] = 13;
    mem_cfg->DqMapCpu2DramMc0Ch1[3] = 10;
    mem_cfg->DqMapCpu2DramMc0Ch1[4] = 14;
    mem_cfg->DqMapCpu2DramMc0Ch1[5] = 8;
    mem_cfg->DqMapCpu2DramMc0Ch1[6] = 15;
    mem_cfg->DqMapCpu2DramMc0Ch1[7] = 9;
    mem_cfg->DqMapCpu2DramMc0Ch1[8] = 2;
    mem_cfg->DqMapCpu2DramMc0Ch1[9] = 3;
    mem_cfg->DqMapCpu2DramMc0Ch1[10] = 1;
    mem_cfg->DqMapCpu2DramMc0Ch1[11] = 0;
    mem_cfg->DqMapCpu2DramMc0Ch1[12] = 6;
    mem_cfg->DqMapCpu2DramMc0Ch1[13] = 5;
    mem_cfg->DqMapCpu2DramMc0Ch1[14] = 7;
    mem_cfg->DqMapCpu2DramMc0Ch1[15] = 4;
    mem_cfg->DqMapCpu2DramMc0Ch2[0] = 15;
    mem_cfg->DqMapCpu2DramMc0Ch2[1] = 14;
    mem_cfg->DqMapCpu2DramMc0Ch2[2] = 12;
    mem_cfg->DqMapCpu2DramMc0Ch2[3] = 13;
    mem_cfg->DqMapCpu2DramMc0Ch2[4] = 11;
    mem_cfg->DqMapCpu2DramMc0Ch2[5] = 10;
    mem_cfg->DqMapCpu2DramMc0Ch2[6] = 8;
    mem_cfg->DqMapCpu2DramMc0Ch2[7] = 9;
    mem_cfg->DqMapCpu2DramMc0Ch2[8] = 0;
    mem_cfg->DqMapCpu2DramMc0Ch2[9] = 1;
    mem_cfg->DqMapCpu2DramMc0Ch2[10] = 2;
    mem_cfg->DqMapCpu2DramMc0Ch2[11] = 3;
    mem_cfg->DqMapCpu2DramMc0Ch2[12] = 4;
    mem_cfg->DqMapCpu2DramMc0Ch2[13] = 7;
    mem_cfg->DqMapCpu2DramMc0Ch2[14] = 6;
    mem_cfg->DqMapCpu2DramMc0Ch2[15] = 5;
    mem_cfg->DqMapCpu2DramMc0Ch3[0] = 12;
    mem_cfg->DqMapCpu2DramMc0Ch3[1] = 13;
    mem_cfg->DqMapCpu2DramMc0Ch3[2] = 11;
    mem_cfg->DqMapCpu2DramMc0Ch3[3] = 10;
    mem_cfg->DqMapCpu2DramMc0Ch3[4] = 9;
    mem_cfg->DqMapCpu2DramMc0Ch3[5] = 15;
    mem_cfg->DqMapCpu2DramMc0Ch3[6] = 8;
    mem_cfg->DqMapCpu2DramMc0Ch3[7] = 14;
    mem_cfg->DqMapCpu2DramMc0Ch3[8] = 2;
    mem_cfg->DqMapCpu2DramMc0Ch3[9] = 3;
    mem_cfg->DqMapCpu2DramMc0Ch3[10] = 0;
    mem_cfg->DqMapCpu2DramMc0Ch3[11] = 1;
    mem_cfg->DqMapCpu2DramMc0Ch3[12] = 5;
    mem_cfg->DqMapCpu2DramMc0Ch3[13] = 4;
    mem_cfg->DqMapCpu2DramMc0Ch3[14] = 7;
    mem_cfg->DqMapCpu2DramMc0Ch3[15] = 6;
    mem_cfg->DqMapCpu2DramMc1Ch0[0] = 15;
    mem_cfg->DqMapCpu2DramMc1Ch0[1] = 14;
    mem_cfg->DqMapCpu2DramMc1Ch0[2] = 13;
    mem_cfg->DqMapCpu2DramMc1Ch0[3] = 12;
    mem_cfg->DqMapCpu2DramMc1Ch0[4] = 10;
    mem_cfg->DqMapCpu2DramMc1Ch0[5] = 8;
    mem_cfg->DqMapCpu2DramMc1Ch0[6] = 9;
    mem_cfg->DqMapCpu2DramMc1Ch0[7] = 11;
    mem_cfg->DqMapCpu2DramMc1Ch0[8] = 0;
    mem_cfg->DqMapCpu2DramMc1Ch0[9] = 1;
    mem_cfg->DqMapCpu2DramMc1Ch0[10] = 2;
    mem_cfg->DqMapCpu2DramMc1Ch0[11] = 3;
    mem_cfg->DqMapCpu2DramMc1Ch0[12] = 7;
    mem_cfg->DqMapCpu2DramMc1Ch0[13] = 4;
    mem_cfg->DqMapCpu2DramMc1Ch0[14] = 5;
    mem_cfg->DqMapCpu2DramMc1Ch0[15] = 6;
    mem_cfg->DqMapCpu2DramMc1Ch1[0] = 15;
    mem_cfg->DqMapCpu2DramMc1Ch1[1] = 14;
    mem_cfg->DqMapCpu2DramMc1Ch1[2] = 11;
    mem_cfg->DqMapCpu2DramMc1Ch1[3] = 10;
    mem_cfg->DqMapCpu2DramMc1Ch1[4] = 13;
    mem_cfg->DqMapCpu2DramMc1Ch1[5] = 12;
    mem_cfg->DqMapCpu2DramMc1Ch1[6] = 8;
    mem_cfg->DqMapCpu2DramMc1Ch1[7] = 9;
    mem_cfg->DqMapCpu2DramMc1Ch1[8] = 1;
    mem_cfg->DqMapCpu2DramMc1Ch1[9] = 7;
    mem_cfg->DqMapCpu2DramMc1Ch1[10] = 0;
    mem_cfg->DqMapCpu2DramMc1Ch1[11] = 6;
    mem_cfg->DqMapCpu2DramMc1Ch1[12] = 3;
    mem_cfg->DqMapCpu2DramMc1Ch1[13] = 5;
    mem_cfg->DqMapCpu2DramMc1Ch1[14] = 2;
    mem_cfg->DqMapCpu2DramMc1Ch1[15] = 4;
    mem_cfg->DqMapCpu2DramMc1Ch2[0] = 15;
    mem_cfg->DqMapCpu2DramMc1Ch2[1] = 14;
    mem_cfg->DqMapCpu2DramMc1Ch2[2] = 13;
    mem_cfg->DqMapCpu2DramMc1Ch2[3] = 12;
    mem_cfg->DqMapCpu2DramMc1Ch2[4] = 9;
    mem_cfg->DqMapCpu2DramMc1Ch2[5] = 10;
    mem_cfg->DqMapCpu2DramMc1Ch2[6] = 11;
    mem_cfg->DqMapCpu2DramMc1Ch2[7] = 8;
    mem_cfg->DqMapCpu2DramMc1Ch2[8] = 0;
    mem_cfg->DqMapCpu2DramMc1Ch2[9] = 1;
    mem_cfg->DqMapCpu2DramMc1Ch2[10] = 7;
    mem_cfg->DqMapCpu2DramMc1Ch2[11] = 6;
    mem_cfg->DqMapCpu2DramMc1Ch2[12] = 3;
    mem_cfg->DqMapCpu2DramMc1Ch2[13] = 2;
    mem_cfg->DqMapCpu2DramMc1Ch2[14] = 5;
    mem_cfg->DqMapCpu2DramMc1Ch2[15] = 4;
    mem_cfg->DqMapCpu2DramMc1Ch3[0] = 4;
    mem_cfg->DqMapCpu2DramMc1Ch3[1] = 3;
    mem_cfg->DqMapCpu2DramMc1Ch3[2] = 5;
    mem_cfg->DqMapCpu2DramMc1Ch3[3] = 2;
    mem_cfg->DqMapCpu2DramMc1Ch3[4] = 6;
    mem_cfg->DqMapCpu2DramMc1Ch3[5] = 7;
    mem_cfg->DqMapCpu2DramMc1Ch3[6] = 0;
    mem_cfg->DqMapCpu2DramMc1Ch3[7] = 1;
    mem_cfg->DqMapCpu2DramMc1Ch3[8] = 15;
    mem_cfg->DqMapCpu2DramMc1Ch3[9] = 14;
    mem_cfg->DqMapCpu2DramMc1Ch3[10] = 10;
    mem_cfg->DqMapCpu2DramMc1Ch3[11] = 11;
    mem_cfg->DqMapCpu2DramMc1Ch3[12] = 12;
    mem_cfg->DqMapCpu2DramMc1Ch3[13] = 9;
    mem_cfg->DqMapCpu2DramMc1Ch3[14] = 8;
    mem_cfg->DqMapCpu2DramMc1Ch3[15] = 13;
    mem_cfg->TsegSize = 8388608;
    mem_cfg->SpdAddressTable[0] = 160;
    mem_cfg->SpdAddressTable[1] = 0;
    mem_cfg->SpdAddressTable[2] = 0;
    mem_cfg->SpdAddressTable[3] = 0;
    mem_cfg->SpdAddressTable[4] = 0;
    mem_cfg->SpdAddressTable[5] = 0;
    mem_cfg->SpdAddressTable[6] = 0;
    mem_cfg->SpdAddressTable[7] = 0;
    mem_cfg->SpdAddressTable[8] = 164;
    mem_cfg->SpdAddressTable[9] = 0;
    mem_cfg->SpdAddressTable[10] = 0;
    mem_cfg->SpdAddressTable[11] = 0;
    mem_cfg->SpdAddressTable[12] = 0;
    mem_cfg->SpdAddressTable[13] = 0;
    mem_cfg->SpdAddressTable[14] = 0;
    mem_cfg->SpdAddressTable[15] = 0;
    mem_cfg->VtdBaseAddress[0] = 4275634176;
    mem_cfg->VtdBaseAddress[1] = 4275642368;
    mem_cfg->VtdBaseAddress[2] = 4275638272;
    mem_cfg->VtdBaseAddress[3] = 4275585024;
    mem_cfg->VtdBaseAddress[4] = 4275589120;
    mem_cfg->VtdBaseAddress[5] = 4275593216;
    mem_cfg->VtdBaseAddress[6] = 4275597312;
    mem_cfg->VtdBaseAddress[7] = 0;
    mem_cfg->VtdBaseAddress[8] = 0;
    mem_cfg->UserBd = 5;
    mem_cfg->RMT = 1;
    mem_cfg->NModeSupport = 2;
    mem_cfg->PchHdaEnable = 0;
    mem_cfg->GttMmAdr = 2147483648;
    mem_cfg->DdiPort2Hpd = 1;
    mem_cfg->DdiPort4Hpd = 1;
    mem_cfg->DdiPort2Ddc = 1;
    mem_cfg->DdiPort4Ddc = 1;
    mem_cfg->EnableC6Dram = 0;
    mem_cfg->HyperThreading = 0;
    mem_cfg->CpuRatio = 0;
    mem_cfg->FClkFrequency = 1;
    mem_cfg->VmxEnable = 0;
    mem_cfg->BiosGuard = 0;
    mem_cfg->EnableSgx = 0;
    mem_cfg->TxtDprMemorySize = 4194304;
    mem_cfg->BiosAcmBase = 4285267968;
    mem_cfg->ConfigTdpLevel = 2;
    mem_cfg->PcieClkSrcUsage[0] = 128;
    mem_cfg->PcieClkSrcUsage[1] = 128;
    mem_cfg->PcieClkSrcUsage[2] = 128;
    mem_cfg->PcieClkSrcUsage[3] = 128;
    mem_cfg->PcieClkSrcUsage[4] = 128;
    mem_cfg->PcieClkSrcUsage[5] = 128;
    mem_cfg->PcieClkSrcUsage[6] = 128;
    mem_cfg->PcieClkSrcUsage[7] = 128;
    mem_cfg->PcieClkSrcUsage[8] = 128;
    mem_cfg->PcieClkSrcUsage[9] = 128;
    mem_cfg->PcieClkSrcUsage[10] = 128;
    mem_cfg->PcieClkSrcUsage[11] = 128;
    mem_cfg->PcieClkSrcUsage[12] = 128;
    mem_cfg->PcieClkSrcUsage[13] = 128;
    mem_cfg->PcieClkSrcUsage[14] = 128;
    mem_cfg->PcieClkSrcUsage[15] = 128;
    mem_cfg->PcieRpEnableMask = 1520787455;
    mem_cfg->PcdDebugInterfaceFlags = 16;
    mem_cfg->SerialIoUartDebugControllerNumber = 0;
    mem_cfg->MrcSafeConfig = 1;
    mem_cfg->TcssItbtPcie0En = 0;
    mem_cfg->TcssItbtPcie1En = 0;
    mem_cfg->TcssItbtPcie2En = 0;
    mem_cfg->TcssItbtPcie3En = 0;
    mem_cfg->TcssXdciEn = 1;
    mem_cfg->TcssDma0En = 0;
    mem_cfg->TcssDma1En = 0;
    mem_cfg->RMC = 0;
    mem_cfg->EccSupport = 0;
    mem_cfg->Ibecc = 1;
    mem_cfg->IbeccParity = 1;
    mem_cfg->RankInterleave = 0;
    mem_cfg->EnhancedInterleave = 0;
    mem_cfg->ChHashEnable = 1;
    mem_cfg->ChHashMask = 12492;
    mem_cfg->PowerDownMode = 0;
    mem_cfg->SafeMode = 1;
    mem_cfg->UsbTcPortEnPreMem = 15;
    mem_cfg->WrcFeatureEnable = 0;
    mem_cfg->McParity = 1;
    mem_cfg->PchHdaSdiEnable[0] = 1;
    mem_cfg->PchHdaSdiEnable[1] = 1;
    mem_cfg->PchHdaAudioLinkDmicEnable[0] = 0;
    mem_cfg->PchHdaAudioLinkDmicEnable[1] = 0;
    mem_cfg->PchHdaAudioLinkDmicClkAPinMux[0] = 692456454;
    mem_cfg->PchHdaAudioLinkDmicClkAPinMux[1] = 692456964;
    mem_cfg->PchHdaAudioLinkDmicClkBPinMux[0] = 692458498;
    mem_cfg->PchHdaAudioLinkDmicClkBPinMux[1] = 692459011;
    mem_cfg->PchHdaDspEnable = 0;
    mem_cfg->PchHdaAudioLinkDmicDataPinMux[0] = 692454407;
    mem_cfg->PchHdaAudioLinkDmicDataPinMux[1] = 692454917;
    mem_cfg->PchHdaAudioLinkSndwEnable[0] = 1;
    mem_cfg->PchHdaAudioLinkSndwEnable[1] = 1;
    mem_cfg->PchHdaAudioLinkSndwEnable[2] = 0;
    mem_cfg->PchHdaAudioLinkSndwEnable[3] = 0;
    mem_cfg->SkipCpuReplacementCheck = 1;
    mem_cfg->SerialIoUartDebugMode = 4;
    mem_cfg->PcieRefPllSsc = 0;
    return 0;
}
#endif /* BUILD_LOADER_STAGE1 */

static int disable_watchdog_tco()
{
    uint16_t v;

    wolfBoot_printf("disable_watchdog_tco" ENDLINE);

    pci_config_write32(0, 31, 4, 0x50, TCO_BASE_ADDRESS);
    pci_config_write32(0, 31, 4, 0x54, 1<<8);

    v = io_read16(TCO_BASE_ADDRESS + 0x8);
    v |= (1 << 11);
    io_write16(TCO_BASE_ADDRESS + 0x8, v);

    return 0;
}

static void fsp_set_silicon_cfg(FSPS_UPD *fsps)
{
    FSP_S_CONFIG *upd = &fsps->FspsConfig;

    upd->GraphicsConfigPtr = 2024131364;

    upd->SataPortsEnable[0] = 1;
    upd->SataPortsEnable[1] = 1;
    upd->SataPortsEnable[2] = 0;
    upd->SataPortsEnable[3] = 0;
    upd->SataPortsEnable[4] = 0;
    upd->SataPortsEnable[5] = 0;
    upd->SataPortsEnable[6] = 0;
    upd->SataPortsEnable[7] = 0;
    upd->PortUsb30Enable[0] = 1;
    upd->PortUsb30Enable[1] = 1;
    upd->PortUsb30Enable[2] = 1;
    upd->PortUsb30Enable[3] = 1;
    upd->PortUsb30Enable[4] = 0;
    upd->PortUsb30Enable[5] = 0;
    upd->PortUsb30Enable[6] = 0;
    upd->PortUsb30Enable[7] = 0;
    upd->PortUsb30Enable[8] = 0;
    upd->PortUsb30Enable[9] = 0;
    upd->XdciEnable = 1;
    upd->DevIntConfigPtr = 2023910720;
    upd->NumOfDevIntConfig = 52;
    upd->SerialIoSpiMode[0] = 1;
    upd->SerialIoSpiMode[1] = 0;
    upd->SerialIoSpiMode[2] = 0;
    upd->SerialIoSpiMode[3] = 0;
    upd->SerialIoSpiMode[4] = 0;
    upd->SerialIoSpiMode[5] = 0;
    upd->SerialIoSpiMode[6] = 0;
    upd->SerialIoUartMode[0] = 4;
    upd->SerialIoUartMode[1] = 1;
    upd->SerialIoUartMode[2] = 1;
    upd->SerialIoUartMode[3] = 0;
    upd->SerialIoUartMode[4] = 0;
    upd->SerialIoUartMode[5] = 0;
    upd->SerialIoUartMode[6] = 0;
    upd->SerialIoUartAutoFlow[0] = 0;
    upd->SerialIoUartAutoFlow[1] = 0;
    upd->SerialIoUartAutoFlow[2] = 0;
    upd->SerialIoUartAutoFlow[3] = 1;
    upd->SerialIoUartAutoFlow[4] = 1;
    upd->SerialIoUartAutoFlow[5] = 0;
    upd->SerialIoUartAutoFlow[6] = 0;
    upd->SerialIoUartRtsPinMuxPolicy[0] = 420160010;
    upd->SerialIoUartRtsPinMuxPolicy[1] = 0;
    upd->SerialIoUartRtsPinMuxPolicy[2] = 0;
    upd->SerialIoUartRtsPinMuxPolicy[3] = 0;
    upd->SerialIoUartRtsPinMuxPolicy[4] = 0;
    upd->SerialIoUartRtsPinMuxPolicy[5] = 0;
    upd->SerialIoUartRtsPinMuxPolicy[6] = 0;
    upd->SerialIoUartCtsPinMuxPolicy[0] = 420164107;
    upd->SerialIoUartCtsPinMuxPolicy[1] = 0;
    upd->SerialIoUartCtsPinMuxPolicy[2] = 0;
    upd->SerialIoUartCtsPinMuxPolicy[3] = 0;
    upd->SerialIoUartCtsPinMuxPolicy[4] = 0;
    upd->SerialIoUartCtsPinMuxPolicy[5] = 0;
    upd->SerialIoUartCtsPinMuxPolicy[6] = 0;
    upd->SerialIoUartRxPinMuxPolicy[0] = 420151816;
    upd->SerialIoUartRxPinMuxPolicy[1] = 0;
    upd->SerialIoUartRxPinMuxPolicy[2] = 0;
    upd->SerialIoUartRxPinMuxPolicy[3] = 0;
    upd->SerialIoUartRxPinMuxPolicy[4] = 0;
    upd->SerialIoUartRxPinMuxPolicy[5] = 0;
    upd->SerialIoUartRxPinMuxPolicy[6] = 0;
    upd->SerialIoUartTxPinMuxPolicy[0] = 420155913;
    upd->SerialIoUartTxPinMuxPolicy[1] = 0;
    upd->SerialIoUartTxPinMuxPolicy[2] = 0;
    upd->SerialIoUartTxPinMuxPolicy[3] = 0;
    upd->SerialIoUartTxPinMuxPolicy[4] = 0;
    upd->SerialIoUartTxPinMuxPolicy[5] = 0;
    upd->SerialIoUartTxPinMuxPolicy[6] = 0;
    upd->SerialIoDebugUartNumber = 0;
    upd->SerialIoI2cMode[0] = 0;
    upd->SerialIoI2cMode[1] = 0;
    upd->SerialIoI2cMode[2] = 0;
    upd->SerialIoI2cMode[3] = 0;
    upd->SerialIoI2cMode[4] = 0;
    upd->SerialIoI2cMode[5] = 0;
    upd->SerialIoI2cMode[6] = 0;
    upd->SerialIoI2cMode[7] = 0;
    upd->Usb2PhyPetxiset[0] = 6;
    upd->Usb2PhyPetxiset[1] = 6;
    upd->Usb2PhyPetxiset[2] = 6;
    upd->Usb2PhyPetxiset[3] = 6;
    upd->Usb2PhyPetxiset[4] = 6;
    upd->Usb2PhyPetxiset[5] = 6;
    upd->Usb2PhyPetxiset[6] = 6;
    upd->Usb2PhyPetxiset[7] = 6;
    upd->Usb2PhyPetxiset[8] = 6;
    upd->Usb2PhyPetxiset[9] = 6;
    upd->Usb2PhyPetxiset[10] = 6;
    upd->Usb2PhyPetxiset[11] = 6;
    upd->Usb2PhyPetxiset[12] = 6;
    upd->Usb2PhyPetxiset[13] = 6;
    upd->Usb2PhyPetxiset[14] = 0;
    upd->Usb2PhyPetxiset[15] = 0;
    upd->Usb2PhyTxiset[0] = 0;
    upd->Usb2PhyTxiset[1] = 0;
    upd->Usb2PhyTxiset[2] = 0;
    upd->Usb2PhyTxiset[3] = 0;
    upd->Usb2PhyTxiset[4] = 0;
    upd->Usb2PhyTxiset[5] = 0;
    upd->Usb2PhyTxiset[6] = 0;
    upd->Usb2PhyTxiset[7] = 0;
    upd->Usb2PhyTxiset[8] = 0;
    upd->Usb2PhyTxiset[9] = 0;
    upd->Usb2PhyTxiset[10] = 0;
    upd->Usb2PhyTxiset[11] = 0;
    upd->Usb2PhyTxiset[12] = 0;
    upd->Usb2PhyTxiset[13] = 0;
    upd->Usb2PhyTxiset[14] = 0;
    upd->Usb2PhyTxiset[15] = 0;
    upd->Usb2PhyPredeemp[0] = 3;
    upd->Usb2PhyPredeemp[1] = 3;
    upd->Usb2PhyPredeemp[2] = 3;
    upd->Usb2PhyPredeemp[3] = 3;
    upd->Usb2PhyPredeemp[4] = 3;
    upd->Usb2PhyPredeemp[5] = 3;
    upd->Usb2PhyPredeemp[6] = 3;
    upd->Usb2PhyPredeemp[7] = 3;
    upd->Usb2PhyPredeemp[8] = 3;
    upd->Usb2PhyPredeemp[9] = 3;
    upd->Usb2PhyPredeemp[10] = 3;
    upd->Usb2PhyPredeemp[11] = 3;
    upd->Usb2PhyPredeemp[12] = 3;
    upd->Usb2PhyPredeemp[13] = 3;
    upd->Usb2PhyPredeemp[14] = 0;
    upd->Usb2PhyPredeemp[15] = 0;
    upd->Usb2PhyPehalfbit[0] = 0;
    upd->Usb2PhyPehalfbit[1] = 0;
    upd->Usb2PhyPehalfbit[2] = 0;
    upd->Usb2PhyPehalfbit[3] = 0;
    upd->Usb2PhyPehalfbit[4] = 0;
    upd->Usb2PhyPehalfbit[5] = 0;
    upd->Usb2PhyPehalfbit[6] = 0;
    upd->Usb2PhyPehalfbit[7] = 0;
    upd->Usb2PhyPehalfbit[8] = 0;
    upd->Usb2PhyPehalfbit[9] = 0;
    upd->Usb2PhyPehalfbit[10] = 0;
    upd->Usb2PhyPehalfbit[11] = 0;
    upd->Usb2PhyPehalfbit[12] = 0;
    upd->Usb2PhyPehalfbit[13] = 0;
    upd->Usb2PhyPehalfbit[14] = 0;
    upd->Usb2PhyPehalfbit[15] = 0;
    upd->PchTsnEnable[0] = 0;
    upd->PchTsnEnable[1] = 0;
    upd->CnviMode = 0;
    upd->CnviBtCore = 0;
    upd->SataLedEnable = 1;
    upd->IomTypeCPortPadCfg[0] = 0;
    upd->IomTypeCPortPadCfg[1] = 0;
    upd->IomTypeCPortPadCfg[2] = 0;
    upd->IomTypeCPortPadCfg[3] = 0;
    upd->IomTypeCPortPadCfg[4] = 0;
    upd->IomTypeCPortPadCfg[5] = 0;
    upd->IomTypeCPortPadCfg[6] = 0;
    upd->IomTypeCPortPadCfg[7] = 0;
    upd->UsbTcPortEn = 15;
    upd->AesEnable = 0;
    upd->PchWriteProtectionEnable[0] = 1;
    upd->PchWriteProtectionEnable[1] = 1;
    upd->PchWriteProtectionEnable[2] = 0;
    upd->PchWriteProtectionEnable[3] = 0;
    upd->PchWriteProtectionEnable[4] = 0;
    upd->PchProtectedRangeLimit[0] = 13679;
    upd->PchProtectedRangeLimit[1] = 16383;
    upd->PchProtectedRangeLimit[2] = 0;
    upd->PchProtectedRangeLimit[3] = 0;
    upd->PchProtectedRangeLimit[4] = 0;
    upd->PchProtectedRangeBase[0] = 13312;
    upd->PchProtectedRangeBase[1] = 13744;
    upd->PchProtectedRangeBase[2] = 0;
    upd->PchProtectedRangeBase[3] = 0;
    upd->PchProtectedRangeBase[4] = 0;
    upd->PcieRpClkReqDetect[0] = 1;
    upd->PcieRpClkReqDetect[1] = 1;
    upd->PcieRpClkReqDetect[2] = 1;
    upd->PcieRpClkReqDetect[3] = 1;
    upd->PcieRpClkReqDetect[4] = 1;
    upd->PcieRpClkReqDetect[5] = 1;
    upd->PcieRpClkReqDetect[6] = 1;
    upd->PcieRpClkReqDetect[7] = 1;
    upd->PcieRpClkReqDetect[8] = 1;
    upd->PcieRpClkReqDetect[9] = 1;
    upd->PcieRpClkReqDetect[10] = 1;
    upd->PcieRpClkReqDetect[11] = 1;
    upd->PcieRpClkReqDetect[12] = 0;
    upd->PcieRpClkReqDetect[13] = 0;
    upd->PcieRpClkReqDetect[14] = 0;
    upd->PcieRpClkReqDetect[15] = 0;
    upd->PcieRpClkReqDetect[16] = 0;
    upd->PcieRpClkReqDetect[17] = 0;
    upd->PcieRpClkReqDetect[18] = 0;
    upd->PcieRpClkReqDetect[19] = 0;
    upd->PcieRpClkReqDetect[20] = 0;
    upd->PcieRpClkReqDetect[21] = 0;
    upd->PcieRpClkReqDetect[22] = 0;
    upd->PcieRpClkReqDetect[23] = 0;
    upd->PcieRpAdvancedErrorReporting[0] = 1;
    upd->PcieRpAdvancedErrorReporting[1] = 1;
    upd->PcieRpAdvancedErrorReporting[2] = 1;
    upd->PcieRpAdvancedErrorReporting[3] = 1;
    upd->PcieRpAdvancedErrorReporting[4] = 1;
    upd->PcieRpAdvancedErrorReporting[5] = 1;
    upd->PcieRpAdvancedErrorReporting[6] = 1;
    upd->PcieRpAdvancedErrorReporting[7] = 1;
    upd->PcieRpAdvancedErrorReporting[8] = 1;
    upd->PcieRpAdvancedErrorReporting[9] = 1;
    upd->PcieRpAdvancedErrorReporting[10] = 1;
    upd->PcieRpAdvancedErrorReporting[11] = 1;
    upd->PcieRpAdvancedErrorReporting[12] = 0;
    upd->PcieRpAdvancedErrorReporting[13] = 0;
    upd->PcieRpAdvancedErrorReporting[14] = 0;
    upd->PcieRpAdvancedErrorReporting[15] = 0;
    upd->PcieRpAdvancedErrorReporting[16] = 0;
    upd->PcieRpAdvancedErrorReporting[17] = 0;
    upd->PcieRpAdvancedErrorReporting[18] = 0;
    upd->PcieRpAdvancedErrorReporting[19] = 0;
    upd->PcieRpAdvancedErrorReporting[20] = 0;
    upd->PcieRpAdvancedErrorReporting[21] = 0;
    upd->PcieRpAdvancedErrorReporting[22] = 0;
    upd->PcieRpAdvancedErrorReporting[23] = 0;
    upd->PcieRpMaxPayload[0] = 1;
    upd->PcieRpMaxPayload[1] = 1;
    upd->PcieRpMaxPayload[2] = 1;
    upd->PcieRpMaxPayload[3] = 1;
    upd->PcieRpMaxPayload[4] = 1;
    upd->PcieRpMaxPayload[5] = 1;
    upd->PcieRpMaxPayload[6] = 1;
    upd->PcieRpMaxPayload[7] = 1;
    upd->PcieRpMaxPayload[8] = 1;
    upd->PcieRpMaxPayload[9] = 1;
    upd->PcieRpMaxPayload[10] = 1;
    upd->PcieRpMaxPayload[11] = 1;
    upd->PcieRpMaxPayload[12] = 0;
    upd->PcieRpMaxPayload[13] = 0;
    upd->PcieRpMaxPayload[14] = 0;
    upd->PcieRpMaxPayload[15] = 0;
    upd->PcieRpMaxPayload[16] = 0;
    upd->PcieRpMaxPayload[17] = 0;
    upd->PcieRpMaxPayload[18] = 0;
    upd->PcieRpMaxPayload[19] = 0;
    upd->PcieRpMaxPayload[20] = 0;
    upd->PcieRpMaxPayload[21] = 0;
    upd->PcieRpMaxPayload[22] = 0;
    upd->PcieRpMaxPayload[23] = 0;
    upd->PcieRpAspm[0] = 0;
    upd->PcieRpAspm[1] = 0;
    upd->PcieRpAspm[2] = 0;
    upd->PcieRpAspm[3] = 0;
    upd->PcieRpAspm[4] = 0;
    upd->PcieRpAspm[5] = 0;
    upd->PcieRpAspm[6] = 0;
    upd->PcieRpAspm[7] = 0;
    upd->PcieRpAspm[8] = 0;
    upd->PcieRpAspm[9] = 0;
    upd->PcieRpAspm[10] = 0;
    upd->PcieRpAspm[11] = 0;
    upd->PcieRpAspm[12] = 4;
    upd->PcieRpAspm[13] = 4;
    upd->PcieRpAspm[14] = 4;
    upd->PcieRpAspm[15] = 4;
    upd->PcieRpAspm[16] = 4;
    upd->PcieRpAspm[17] = 4;
    upd->PcieRpAspm[18] = 4;
    upd->PcieRpAspm[19] = 4;
    upd->PcieRpAspm[20] = 4;
    upd->PcieRpAspm[21] = 4;
    upd->PcieRpAspm[22] = 4;
    upd->PcieRpAspm[23] = 4;
    upd->PcieRpL1Substates[0] = 0;
    upd->PcieRpL1Substates[1] = 0;
    upd->PcieRpL1Substates[2] = 0;
    upd->PcieRpL1Substates[3] = 0;
    upd->PcieRpL1Substates[4] = 0;
    upd->PcieRpL1Substates[5] = 0;
    upd->PcieRpL1Substates[6] = 0;
    upd->PcieRpL1Substates[7] = 0;
    upd->PcieRpL1Substates[8] = 0;
    upd->PcieRpL1Substates[9] = 0;
    upd->PcieRpL1Substates[10] = 0;
    upd->PcieRpL1Substates[11] = 0;
    upd->PcieRpL1Substates[12] = 3;
    upd->PcieRpL1Substates[13] = 3;
    upd->PcieRpL1Substates[14] = 3;
    upd->PcieRpL1Substates[15] = 3;
    upd->PcieRpL1Substates[16] = 3;
    upd->PcieRpL1Substates[17] = 3;
    upd->PcieRpL1Substates[18] = 3;
    upd->PcieRpL1Substates[19] = 3;
    upd->PcieRpL1Substates[20] = 3;
    upd->PcieRpL1Substates[21] = 3;
    upd->PcieRpL1Substates[22] = 3;
    upd->PcieRpL1Substates[23] = 3;
    upd->PcieRpLtrEnable[0] = 1;
    upd->PcieRpLtrEnable[1] = 1;
    upd->PcieRpLtrEnable[2] = 1;
    upd->PcieRpLtrEnable[3] = 1;
    upd->PcieRpLtrEnable[4] = 1;
    upd->PcieRpLtrEnable[5] = 1;
    upd->PcieRpLtrEnable[6] = 1;
    upd->PcieRpLtrEnable[7] = 1;
    upd->PcieRpLtrEnable[8] = 1;
    upd->PcieRpLtrEnable[9] = 1;
    upd->PcieRpLtrEnable[10] = 1;
    upd->PcieRpLtrEnable[11] = 1;
    upd->PcieRpLtrEnable[12] = 0;
    upd->PcieRpLtrEnable[13] = 0;
    upd->PcieRpLtrEnable[14] = 0;
    upd->PcieRpLtrEnable[15] = 0;
    upd->PcieRpLtrEnable[16] = 0;
    upd->PcieRpLtrEnable[17] = 0;
    upd->PcieRpLtrEnable[18] = 0;
    upd->PcieRpLtrEnable[19] = 0;
    upd->PcieRpLtrEnable[20] = 0;
    upd->PcieRpLtrEnable[21] = 0;
    upd->PcieRpLtrEnable[22] = 0;
    upd->PcieRpLtrEnable[23] = 0;
    upd->IehMode = 1;
    upd->Usb2OverCurrentPin[0] = 255;
    upd->Usb2OverCurrentPin[1] = 3;
    upd->Usb2OverCurrentPin[2] = 255;
    upd->Usb2OverCurrentPin[3] = 255;
    upd->Usb2OverCurrentPin[4] = 255;
    upd->Usb2OverCurrentPin[5] = 255;
    upd->Usb2OverCurrentPin[6] = 3;
    upd->Usb2OverCurrentPin[7] = 255;
    upd->Usb2OverCurrentPin[8] = 1;
    upd->Usb2OverCurrentPin[9] = 255;
    upd->Usb2OverCurrentPin[10] = 5;
    upd->Usb2OverCurrentPin[11] = 5;
    upd->Usb2OverCurrentPin[12] = 6;
    upd->Usb2OverCurrentPin[13] = 6;
    upd->Usb2OverCurrentPin[14] = 7;
    upd->Usb2OverCurrentPin[15] = 7;
    upd->Usb3OverCurrentPin[0] = 2;
    upd->Usb3OverCurrentPin[1] = 255;
    upd->Usb3OverCurrentPin[2] = 255;
    upd->Usb3OverCurrentPin[3] = 255;
    upd->Usb3OverCurrentPin[4] = 2;
    upd->Usb3OverCurrentPin[5] = 2;
    upd->Usb3OverCurrentPin[6] = 3;
    upd->Usb3OverCurrentPin[7] = 3;
    upd->Usb3OverCurrentPin[8] = 4;
    upd->Usb3OverCurrentPin[9] = 4;
    upd->SgxEpoch0 = 6142344250440060711U;
    upd->SgxEpoch1 = 15521488688214965697U;
    upd->PchDmiAspmCtrl = 0;
    upd->CpuPcieSetSecuredRegisterLock = 1;
    upd->CpuPcieRpPmSci[0] = 1;
    upd->CpuPcieRpPmSci[1] = 0;
    upd->CpuPcieRpPmSci[2] = 0;
    upd->CpuPcieRpPmSci[3] = 0;
    upd->CpuPcieRpMaxPayload[0] = 1;
    upd->CpuPcieRpMaxPayload[1] = 0;
    upd->CpuPcieRpMaxPayload[2] = 0;
    upd->CpuPcieRpMaxPayload[3] = 0;
    upd->CpuPcieRpAspm[0] = 0;
    upd->CpuPcieRpAspm[1] = 2;
    upd->CpuPcieRpAspm[2] = 2;
    upd->CpuPcieRpAspm[3] = 2;
    upd->CpuPcieRpL1Substates[0] = 0;
    upd->CpuPcieRpL1Substates[1] = 2;
    upd->CpuPcieRpL1Substates[2] = 2;
    upd->CpuPcieRpL1Substates[3] = 2;
    upd->CpuPcieRpLtrEnable[0] = 1;
    upd->CpuPcieRpLtrEnable[1] = 0;
    upd->CpuPcieRpLtrEnable[2] = 0;
    upd->CpuPcieRpLtrEnable[3] = 0;
    upd->Eist = 0;
    upd->ProcHotResponse = 0;
    upd->ThermalMonitor = 0;
    upd->Cx = 0;
    upd->EnableItbm = 0;
    upd->PcieRpLtrMaxSnoopLatency[0] = 4099;
    upd->PcieRpLtrMaxSnoopLatency[1] = 4099;
    upd->PcieRpLtrMaxSnoopLatency[2] = 4099;
    upd->PcieRpLtrMaxSnoopLatency[3] = 4099;
    upd->PcieRpLtrMaxSnoopLatency[4] = 4099;
    upd->PcieRpLtrMaxSnoopLatency[5] = 4099;
    upd->PcieRpLtrMaxSnoopLatency[6] = 4099;
    upd->PcieRpLtrMaxSnoopLatency[7] = 4099;
    upd->PcieRpLtrMaxSnoopLatency[8] = 4099;
    upd->PcieRpLtrMaxSnoopLatency[9] = 4099;
    upd->PcieRpLtrMaxSnoopLatency[10] = 4099;
    upd->PcieRpLtrMaxSnoopLatency[11] = 4099;
    upd->PcieRpLtrMaxSnoopLatency[12] = 0;
    upd->PcieRpLtrMaxSnoopLatency[13] = 0;
    upd->PcieRpLtrMaxSnoopLatency[14] = 0;
    upd->PcieRpLtrMaxSnoopLatency[15] = 0;
    upd->PcieRpLtrMaxSnoopLatency[16] = 0;
    upd->PcieRpLtrMaxSnoopLatency[17] = 0;
    upd->PcieRpLtrMaxSnoopLatency[18] = 0;
    upd->PcieRpLtrMaxSnoopLatency[19] = 0;
    upd->PcieRpLtrMaxSnoopLatency[20] = 0;
    upd->PcieRpLtrMaxSnoopLatency[21] = 0;
    upd->PcieRpLtrMaxSnoopLatency[22] = 0;
    upd->PcieRpLtrMaxSnoopLatency[23] = 0;
    upd->PcieRpLtrMaxNoSnoopLatency[0] = 4099;
    upd->PcieRpLtrMaxNoSnoopLatency[1] = 4099;
    upd->PcieRpLtrMaxNoSnoopLatency[2] = 4099;
    upd->PcieRpLtrMaxNoSnoopLatency[3] = 4099;
    upd->PcieRpLtrMaxNoSnoopLatency[4] = 4099;
    upd->PcieRpLtrMaxNoSnoopLatency[5] = 4099;
    upd->PcieRpLtrMaxNoSnoopLatency[6] = 4099;
    upd->PcieRpLtrMaxNoSnoopLatency[7] = 4099;
    upd->PcieRpLtrMaxNoSnoopLatency[8] = 4099;
    upd->PcieRpLtrMaxNoSnoopLatency[9] = 4099;
    upd->PcieRpLtrMaxNoSnoopLatency[10] = 4099;
    upd->PcieRpLtrMaxNoSnoopLatency[11] = 4099;
    upd->PcieRpLtrMaxNoSnoopLatency[12] = 0;
    upd->PcieRpLtrMaxNoSnoopLatency[13] = 0;
    upd->PcieRpLtrMaxNoSnoopLatency[14] = 0;
    upd->PcieRpLtrMaxNoSnoopLatency[15] = 0;
    upd->PcieRpLtrMaxNoSnoopLatency[16] = 0;
    upd->PcieRpLtrMaxNoSnoopLatency[17] = 0;
    upd->PcieRpLtrMaxNoSnoopLatency[18] = 0;
    upd->PcieRpLtrMaxNoSnoopLatency[19] = 0;
    upd->PcieRpLtrMaxNoSnoopLatency[20] = 0;
    upd->PcieRpLtrMaxNoSnoopLatency[21] = 0;
    upd->PcieRpLtrMaxNoSnoopLatency[22] = 0;
    upd->PcieRpLtrMaxNoSnoopLatency[23] = 0;
    upd->CpuPcieRpLtrMaxSnoopLatency[0] = 4099;
    upd->CpuPcieRpLtrMaxSnoopLatency[1] = 4111;
    upd->CpuPcieRpLtrMaxSnoopLatency[2] = 4111;
    upd->CpuPcieRpLtrMaxSnoopLatency[3] = 4111;
    upd->CpuPcieRpLtrMaxNoSnoopLatency[0] = 4099;
    upd->CpuPcieRpLtrMaxNoSnoopLatency[1] = 4111;
    upd->CpuPcieRpLtrMaxNoSnoopLatency[2] = 4111;
    upd->CpuPcieRpLtrMaxNoSnoopLatency[3] = 4111;
}
/**
 * @brief Update S parameters in FSPS_UPD structure.
 *
 * This function updates the S parameters in the FSPS_UPD structure for
 * TigerLake x86 platform with FSP.
 *
 * @param[in,out] default_s_params Pointer to the default S parameters data.
 * @return 0 on success, -1 on failure.
 */
int fsp_machine_update_s_parameters(uint8_t *default_s_params)
{
    FSP_S_CONFIG *upd;
    unsigned int i;

    fsp_set_silicon_cfg((FSPS_UPD*)default_s_params);

    upd = &((FSPS_UPD*)default_s_params)->FspsConfig;

    upd->MicrocodeRegionBase = 0x0;
    upd->MicrocodeRegionSize = 0x0;
    /* we can assume that is under 4gb */
    upd->DevIntConfigPtr = (uint32_t)(uintptr_t)mPchHDevIntConfig;
    upd->NumOfDevIntConfig = sizeof(mPchHDevIntConfig)/sizeof(mPchHDevIntConfig[0]);
    upd->SataEnable = 1;
    upd->SataMode = 0;
    upd->SataSalpSupport = 0;
    upd->EnableMultiPhaseSiliconInit = 0;
    upd->Enable8254ClockGating = 0;
    memset(upd->SataPortsEnable, 0, sizeof(upd->SataPortsEnable));
    upd->SataPortsEnable[0] = upd->SataPortsEnable[1] = 1;
    upd->PortUsb30Enable[0] = upd->PortUsb30Enable[1] = upd->PortUsb30Enable[2] = upd->PortUsb30Enable[3] = 1;
    upd->XdciEnable = 1;

    for (i = 0; i < sizeof(upd->SerialIoUartMode); i++)
        upd->SerialIoUartMode[i] = 0x0;
    upd->SerialIoUartMode[0] = 0x4;

    upd->EnableMultiPhaseSiliconInit = 0;
    upd->SerialIoUartMode[1] = upd->SerialIoUartMode[2] = 0x1;
    upd->SerialIoDebugUartNumber = 0x0;

    memset(upd->PcieRpHotPlug, 0, sizeof(upd->PcieRpHotPlug));
    memset(upd->CpuPcieRpHotPlug, 0, sizeof(upd->CpuPcieRpHotPlug));
    return 0;
}
/**
 * @brief Configure GPIO settings for a specific device.
 *
 * This function configures GPIO settings for a specific device on TigerLake x86
 * platform.
 *
 * @param[in,out] gpio Pointer to the tgl_gpio_info structure containing GPIO
 * information.
 * @return void
 */
static int tgl_setup_lpc_decode(uint32_t address, uint32_t length,
                                uint32_t range)
{
    uint32_t val;
    uint32_t reg;

    /* Only 4 range, zero based */
    if (range > 3)
        return -1;

    val = (length - 1) << 16;
    val |= address;
    val |= 0x1; /* enable */
    reg = PCR_DMI_LPCLGIR1 + range * 4;

    /* TODO: check DMI locked */
    /* setup up decoding in DMI - generic I/O range 0*/
    pch_write32(PCR_DMI_PORT_ID, reg, val);

    reg = PCI_ESPI_LGIR1 + range * 4;
    /* setup up decoding in eSPI - generic I/O range 0*/
    pci_config_write32(PCI_ESPI_BUS, PCI_ESPI_DEV, PCI_ESPI_FUN,
                       reg, val);

    return 0;
}

#define GPIO_OWN_MASK (0x3)
struct tgl_gpio {
    uint8_t comm_port_id;
    uint32_t cfg_offset;
};

enum gpio_config_flags {
    GPIO_SET_MODE = (1 << 0),
    GPIO_SET_OWN = (1 << 1),
    GPIO_SET_INTERRUPT = (1 << 2),
    GPIO_SET_DIRECTION = (1 << 3),
    GPIO_SET_RXINV = (1 << 4),
    GPIO_SET_RESET = (1 << 5),
    GPIO_SET_TERM = (1 << 6),
    GPIO_SET_RXEVCONF = (1 << 7)
};

struct tgl_gpio_conf {
    struct tgl_gpio gpio;
    enum gpio_config_flags flags;
    uint8_t gpio_mode;
    uint8_t gpio_own;
    uint8_t gpio_interrupt;
    uint8_t gpio_dir;
    uint8_t gpio_reset;
    uint8_t gpio_term;
    uint8_t gpio_rxevconf;
    uint8_t gpio_rxinv;
};

#if defined (TARGET_kontron_vx3060_s2)
static const struct tgl_gpio_conf gpio_table_tempram[] = {
    /* UART 0 */
    {.gpio =
         {
             /* PAD C8 */
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C8_CFG_OFF,
         },
     .flags =
         (GPIO_SET_DIRECTION | GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             /* PAD C9 */
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C9_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C10_CFG_OFF,
         },
     .flags =
         (GPIO_SET_DIRECTION | GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C11_CFG_OFF,
         },
     .flags =
         (GPIO_SET_DIRECTION | GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* UART - 1*/
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C12_CFG_OFF,
         },
     .flags =
         (GPIO_SET_DIRECTION | GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C13_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C14_CFG_OFF,
         },
     .flags =
         (GPIO_SET_DIRECTION | GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C15_CFG_OFF,
         },
     .flags =
         (GPIO_SET_DIRECTION | GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* UART - 2*/
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C20_CFG_OFF,
         },
     .flags =
         (GPIO_SET_DIRECTION | GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C21_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_term = GPIO_TERM_NONE,
     .gpio_reset = GPIO_RESET_PLTRST},
};

static const struct tgl_gpio_conf gpio_table_premem[] = {
    /* Disable CNVi (Bluetooth Radio Interface) */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_F2_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
};

static const struct tgl_gpio_conf gpio_table_presilicon[] = {

    /* set to GPIO mode as native functions aren't used */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_A8_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_E12_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_E15_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_E16_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},

    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_F9_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
    /* test points */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_A9_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_F4_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_F5_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
     /* FUSA_DIAGTEST_PCHMODE and FUSA_DIAGTEST_EN, disabled in SBL*/
/*     {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_T2_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_T3_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_OUTPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST}, */
    /* TPM */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C22_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_RXINV | GPIO_SET_RESET |
          GPIO_SET_RXEVCONF | GPIO_SET_TERM | GPIO_SET_INTERRUPT),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_rxinv = 1,
     .gpio_rxevconf = GPIO_RXEVCONF_LEVEL,
     .gpio_interrupt = GPIO_INTERRUPT_SCI,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET,
     .gpio_term = GPIO_TERM_NONE},
    /* PCH I2C5 */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_B10_CFG_OFF,
         },
     .flags =
         (GPIO_SET_MODE | GPIO_SET_INTERRUPT | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_0_PORT_ID,
             .cfg_offset = GPIO_GPPC_B9_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    /* SMLINK1 to PD */
    {/* SM1 CLK */
     .gpio =
         {
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C6_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             /* SM1 DATA */
             .comm_port_id = GPIO_COMM_4_PORT_ID,
             .cfg_offset = GPIO_GPPC_C7_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_NATIVE_1,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    /* watchdog */
    {.gpio =
         {
             /* PLD_WDT_IRQ0 */
             .comm_port_id = GPIO_COMM_1_PORT_ID,
             .cfg_offset = GPIO_GPPC_D1_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             /* PLD_WDT_IRQ1 */
             .comm_port_id = GPIO_COMM_1_PORT_ID,
             .cfg_offset = GPIO_GPPC_D0_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET,
     .gpio_term = GPIO_TERM_NONE},
    /* wake from i225/E810/i210/M2_TOP/M2_BOT/XMC */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_1_PORT_ID,
             .cfg_offset = GPIO_GPPC_D2_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_HOSTDEEPRESET,
     .gpio_term = GPIO_TERM_NONE},
    /* Audio, disabled */
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R0_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R1_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R2_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R3_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R4_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},
    {.gpio =
         {
             .comm_port_id = GPIO_COMM_5_PORT_ID,
             .cfg_offset = GPIO_GPP_R5_CFG_OFF,
         },
     .flags = (GPIO_SET_MODE | GPIO_SET_DIRECTION | GPIO_SET_INTERRUPT |
               GPIO_SET_RESET | GPIO_SET_TERM),
     .gpio_mode = GPIO_MODE_GPIO,
     .gpio_dir = GPIO_DIR_INPUT,
     .gpio_interrupt = GPIO_INTERRUPT_DISABLE,
     .gpio_reset = GPIO_RESET_PLTRST,
     .gpio_term = GPIO_TERM_NONE},

};
#endif /* TARGET_kontron_vx3060_s2 */
/**
 * @brief Configure GPIO settings for a specific device.
 * @details This function configures GPIO settings for a specific device on
 * TigerLake x86 platform.
 *
 * @param[in,out] gpio Pointer to the tgl_gpio_info structure containing
 * GPIO information.
 * @return void
 */
static void tgl_gpio_configure(const struct tgl_gpio_conf *gpio)
{
    uint32_t dw0, _dw0, dw1, _dw1;
    struct tgl_gpio g;

    g = gpio->gpio;

    GPIO_DEBUG_PRINTF("Gpio port: %d off: 0x%x :\r\n", i, g.comm_port_id,
                      g.cfg_offset);

    dw0 = _dw0 = pch_read32(g.comm_port_id, g.cfg_offset);
    dw1 = _dw1 = pch_read32(g.comm_port_id, g.cfg_offset + 4);

    if (gpio->flags & GPIO_SET_MODE) {
        dw0 &= ~(GPIO_MODE_MASK);
        dw0 |= gpio->gpio_mode << GPIO_MODE_SHIFT;
    }
    if (gpio->flags & GPIO_SET_RESET) {
        dw0 &= ~(GPIO_RESET_MASK);
        dw0 |= gpio->gpio_reset << GPIO_RESET_SHIFT;
    }
    if (gpio->flags & GPIO_SET_DIRECTION) {
        dw0 &= ~(GPIO_DIR_MASK);
        dw0 |= gpio->gpio_dir << GPIO_DIR_SHIFT;
    }
    if (gpio->flags & GPIO_SET_INTERRUPT) {
        dw0 &= ~(GPIO_INTERRUPT_MASK);
        dw0 |= gpio->gpio_interrupt << GPIO_INTERRUPT_SHIFT;
    }
    if (gpio->flags & GPIO_SET_TERM) {
        dw1 &= ~(GPIO_TERM_MASK);
        dw1 |= gpio->gpio_term << GPIO_TERM_SHIFT;
    }
    if (gpio->flags & GPIO_SET_RXEVCONF) {
        dw0 &= ~(GPIO_RXEVCONF_MASK);
        dw0 |= gpio->gpio_rxevconf << GPIO_RXEVCONF_SHIFT;
    }

    GPIO_DEBUG_PRINTF("dw0: 0x%x -> 0x%x, dw1: 0x%x->0x%x\r\n", 
                      _dw0, dw0, _dw1, dw1);
    if (_dw1 != dw1) {
        pch_write32(g.comm_port_id, g.cfg_offset + 4, dw1);
    }
    if (_dw0 != dw0) {
        pch_write32(g.comm_port_id, g.cfg_offset, dw0);
    }
}

#ifdef TARGET_kontron_vx3060_s2
/**
 * @brief Set up ECE1200 device on Kontron VX3060-S2 board.
 *
 * This function sets up the ECE1200 device on the Kontron VX3060-S2 board.
 *
 * @return void
 */
static void setup_ece1200()
{
    uint8_t reg;

    wolfBoot_printf("setup ece1200" ENDLINE);
    delay(2000);

    io_write8(ECE1200_INDEX, 0x55); /* conf mode */
    io_write8(ECE1200_INDEX, 0x36);

    io_write8(ECE1200_INDEX, 0x07);
    io_write8(ECE1200_DATA, 0x01);
    io_write8(ECE1200_INDEX, 0x40);
    reg = io_read8(ECE1200_DATA); /* SERIRQ Enable and Mode reg */
    reg |= (1 << 7); /* SIRQ_EN */
    reg |= (1 << 6); /* SIRQ_MD (continuous mode) */
    io_write8(ECE1200_DATA, reg);
    io_write8(ECE1200_INDEX, 0xaa); /* close conf mode */
}
/**
 * @brief Configure Kontron CPLD for special settings.
 *
 * This function configures the Kontron CPLD for special settings on the Kontron
 * VX3060-S2 board.
 *
 * @return 0 on success, -1 on failure.
 */
static int configure_kontron_cpld()
{
    uint8_t reg;
    int i, ret;

    wolfBoot_printf("setup kontron cpld" ENDLINE);
    ret = tgl_setup_lpc_decode(CPLD_ADDRESS, CPLD_LENGTH, 0);
    if (ret < 0)
        return ret;

    /* The address of ECE1200 results already accessible, there is no need to
     * setup the decoding */
    setup_ece1200();

    delay(100);

    /* set IMPI in programming mode, disable the IMPI watchdog */
    io_write8(CPLD_ADDRESS + 0x76, 0x80);

    /* enable serial tx 1  */
    io_write8(CPLD_ADDRESS + CPLD_SERIAL_LINES_CTL, CPLD_SERIAL1_TXEN);

    return 0;
}

#define CPLD_I2C_MISC 0x78
#define CPLD_I2C_MISC_FORCE_RESCUE (1 << 7)
/**
 * @brief Ask for recovery mode using CPLD.
 *
 * This function asks for recovery mode using CPLD on the Kontron VX3060-S2 board.
 *
 * @return void
 */
static void kontron_ask_for_recovery() {
   uint32_t reg;
   uint8_t ch;
   int ret;
   int i;

    wolfBoot_printf("Press any key within 2 seconds to toogle BIOS flash chip\r\n");

   for (i = 0; i < 20; i++) {
       ret = uart_rx(&ch);
       if (ret != -1) {
           uint8_t reg = io_read8(CPLD_ADDRESS + CPLD_I2C_MISC);
           reg |= CPLD_I2C_MISC_FORCE_RESCUE;
           io_write8(CPLD_ADDRESS + CPLD_I2C_MISC, reg);
           wolfBoot_printf("Booting from the other flash chip...\r\n");
           reset(1);
       }

       delay(100);
   }
}
#endif /* TARGET_kontron_vx3060_s2 */

/**
 * @brief Callback function after temporary RAM initialization.
 *
 * This function is a callback after temporary RAM initialization on TigerLake x86.
 *
 * @return 0 on success, -1 on failure.
 */
int post_temp_ram_init_cb(void)
{
    disable_watchdog_tco();

#ifdef TARGET_kontron_vx3060_s2
    uint8_t reg;
    int err;
    unsigned int i;

    reg = pch_read32(PCR_INTERRUPT_PORT_ID, GIC_OFFSET);
    if (reg & GIC_SHUTDOWN_STATUS_BIT) {
        /* CPU is in shutdown mode, probably after a triple fault. Force a full reset. */
        wolfBoot_printf("CPU Shutdown mode detected. Force reset.\r\n");
        reset(0);
        panic();
    }

    err = configure_kontron_cpld();
    if (err != 0)
        return err;

    /* setup GPIOs */
    for (i = 0; i < sizeof(gpio_table_tempram)/sizeof(gpio_table_tempram[0]); i++) {
        tgl_gpio_configure(&gpio_table_tempram[i]);
    }


    kontron_ask_for_recovery();
#else
    (void)tgl_gpio_configure;
#endif /* TARGET_kontron_vx3060_s2 */

    return 0;
}

#if defined(BUILD_LOADER_STAGE1)
/**
 * @brief Update M parameters in FSPM_UPD structure.
 *
 * This function updates the M parameters in the FSPM_UPD structure for
 * TigerLake x86 platform with FSP.
 *
 * @param[in,out] default_m_params Pointer to the default M parameters data.
 * @param[in] mem_base Base address of memory.
 * @param[in] mem_size Size of memory.
 * @return 0 on success, -1 on failure.
 */
int fsp_machine_update_m_parameters(uint8_t *default_m_params,
                                    uint32_t mem_base,
                                    uint32_t mem_size)
{
    FSPM_UPD *new_udp;

    wolfBoot_printf("machine_update_m_params" ENDLINE);

    new_udp = (FSPM_UPD*)default_m_params;
    fsp_set_memory_cfg(new_udp);

    new_udp->FspmArchUpd.BootLoaderTolumSize = 0;
    new_udp->FspmArchUpd.BootMode = BOOT_WITH_FULL_CONFIGURATION;

    new_udp->FspmArchUpd.NvsBufferPtr        = 0;
    new_udp->FspmArchUpd.StackBase = mem_base;
    new_udp->FspmArchUpd.StackSize = mem_size;

    return 0;
}

int fsp_pre_mem_init_cb(void)
{
    unsigned int i;

    for (i = 0; i < sizeof(gpio_table_premem)/sizeof(gpio_table_premem[0]); i++) {
        tgl_gpio_configure(&gpio_table_premem[i]);
    }

    return 0;
}
#endif /* BUILD_LOADER_STAGE1 */

int fsp_pre_silicon_init_cb(void)
{
    unsigned int i;

    for (i = 0; i < sizeof(gpio_table_presilicon)/sizeof(gpio_table_presilicon[0]); i++) {
        tgl_gpio_configure(&gpio_table_presilicon[i]);
    }

    return 0;
}
#endif /* TGL_FSP_H */
