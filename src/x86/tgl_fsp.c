/* tgl_fsp.c
 * Copyright (C) 2023 wolfSSL Inc.
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
#define GPIO_PAD_CONF_OFF 0x700
#define GPIO_C_8_OFF 0x8
#define GPIO_C_9_OFF 0x9
#define GPIO_C_10_OFF 0x10
#define GPIO_C_11_OFF 0x11
#define GPIO_MODE_NATIVE_1 0x01
#define GPIO_RESET_PLTRST 0x02
#define GPIO_MODE_MASK (0x7) << 10
#define GPIO_MODE_SHIFT 10
#define GPIO_RESET_MASK (0x3) << 30
#define GPIO_RESET_SHIFT 30


SI_PCH_DEVICE_INTERRUPT_CONFIG mPchHDevIntConfig[] = {
    {30, 0, SiPchIntA, 16},
};

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
    mem_cfg->PchHdaEnable = 0;
    mem_cfg->DdiPort2Hpd = 1;
    mem_cfg->DdiPort4Hpd = 1;
    mem_cfg->DdiPort2Ddc = 1;
    mem_cfg->DdiPort4Ddc = 1;
    mem_cfg->EnableC6Dram = 0;
    mem_cfg->HyperThreading = 1;
    mem_cfg->CpuRatio = 0;
    mem_cfg->FClkFrequency = 1;
    mem_cfg->VmxEnable = 0;
    mem_cfg->BiosGuard = 0;
    mem_cfg->EnableSgx = 0;
    mem_cfg->TxtDprMemorySize = 4194304;
    mem_cfg->BiosAcmBase = 4285267968;
    mem_cfg->ConfigTdpLevel = 2;
    mem_cfg->PchNumRsvdSmbusAddresses = 4;
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
    mem_cfg->RsvdSmbusAddressTablePtr = 4277460020;
    mem_cfg->PcdDebugInterfaceFlags = 16;
    mem_cfg->SerialIoUartDebugControllerNumber = 0;
    mem_cfg->MrcSafeConfig = 1;
    mem_cfg->TcssItbtPcie0En = 0;
    mem_cfg->TcssItbtPcie1En = 0;
    mem_cfg->TcssItbtPcie2En = 0;
    mem_cfg->TcssItbtPcie3En = 0;
    mem_cfg->TcssXhciEn = 0;
    mem_cfg->TcssDma0En = 0;
    mem_cfg->TcssDma1En = 0;
    mem_cfg->RMC = 0;
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

    (void)default_s_params;

    memcpy(default_s_params, (uint8_t*)FSP_S_UPD_DATA_BASE, 0xee0);

    upd = &((FSPS_UPD*)default_s_params)->FspsConfig;
    upd->MicrocodeRegionBase = 0x0;
    upd->MicrocodeRegionSize = 0x0;
    upd->DevIntConfigPtr = (uint32_t)mPchHDevIntConfig;
    upd->NumOfDevIntConfig = sizeof(mPchHDevIntConfig)/sizeof(mPchHDevIntConfig[0]);
    upd->SataEnable = 1;
    upd->SataMode = 0;
    upd->SataSalpSupport = 0;
    upd->EnableMultiPhaseSiliconInit = 0;
    upd->Enable8254ClockGating = 0;
    memset(upd->SataPortsEnable, 1, sizeof(upd->SataPortsEnable));
/*
     upd->SataRstRaid0 = 1;
    upd->SataRstRaid1 = 1;
    upd->SataRstRaid10 = 1;
    upd->SataRstRaid5 = 1;
 */


    for (i = 0; i < sizeof(upd->SerialIoUartMode); i++)
        upd->SerialIoUartMode[i] = 0x1;

    upd->SerialIoDebugUartNumber = 0x0;
    upd->SerialIoUartMode[0] = 0x4;

    upd->EnableMultiPhaseSiliconInit = 0;
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
    pci_config_write32(PCI_ESPI_BUS, PCI_ESPI_DEV, PCI_ESPI_FUN, PCI_ESPI_LGIR1, val);

    return 0;
}

/* only support native mode */
struct tgl_gpio_info {
    uint8_t comm_port_id;
    uint8_t gpio_pad_off;
    uint8_t pad_mode:3;
    uint8_t pad_reset:2;
};

/**
 * @brief Configure GPIO settings for a specific device.
 * @details This function configures GPIO settings for a specific device on
 * TigerLake x86 platform.
 *
 * @param[in,out] gpio Pointer to the tgl_gpio_info structure containing
 * GPIO information.
 * @return void
 */
static void tgl_gpio_configure(struct tgl_gpio_info *gpio)
{
    uint16_t off;
    uint32_t dw0;

    off = gpio->gpio_pad_off * 16 + GPIO_PAD_CONF_OFF;
    dw0 = pch_read32(gpio->comm_port_id, off);

    dw0 &= ~(GPIO_MODE_MASK);
    dw0 |= gpio->pad_mode << GPIO_MODE_SHIFT;

    dw0 &= ~(GPIO_RESET_MASK);
    dw0 |= gpio->pad_reset << GPIO_RESET_SHIFT;

    pch_write32(gpio->comm_port_id, off, dw0);
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
    reg = io_read8(ECE1200_DATA);

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
    struct tgl_gpio_info uart_gpio =  {
            .comm_port_id = GPIO_COMM_4_PORT_ID,
            .pad_mode = GPIO_MODE_NATIVE_1,
            .pad_reset = GPIO_RESET_PLTRST,
    };
    uint8_t reg;
    int err;
    int i;

    err = configure_kontron_cpld();
    if (err != 0)
        return err;

    /* setup uart gpios */
    for (i = 0; i < 4; i++) {
        uart_gpio.gpio_pad_off = GPIO_C_8_OFF + i;
        tgl_gpio_configure(&uart_gpio);
    }

    kontron_ask_for_recovery();
#else
    (void)tgl_gpio_configure;
#endif /* TARGET_kontron_vx3060_s2 */

    return 0;
}

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
    int i;

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

#endif /* TGL_FSP_H */
