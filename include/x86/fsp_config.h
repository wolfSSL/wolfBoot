#ifndef FSP_CONFIG_H
#define FSP_CONFIG_H

#include <stdint.h>

#ifndef FSP_HYPERTHREADING
#define FSP_HYPERTHREADING 0
#endif
#if FSP_HYPERTHREADING
/* The MADT and MP table list four single-thread cores (APIC IDs 0/2/4/6). */
#error "FSP_HYPERTHREADING=1 needs sibling threads in the MADT and MP table"
#endif

#ifndef FSP_INTEL_SPEED_STEP
#define FSP_INTEL_SPEED_STEP 0
#endif

#ifndef FSP_INTEL_SPEED_SHIFT
#define FSP_INTEL_SPEED_SHIFT 0
#endif

#define FSP_BOOT_FREQ_MAX_NO_TURBO 0x1
#ifndef FSP_BOOT_PERFORMANCE_MODE
#define FSP_BOOT_PERFORMANCE_MODE FSP_BOOT_FREQ_MAX_NO_TURBO
#endif

#ifndef FSP_C_STATES
#define FSP_C_STATES 0
#endif

#ifndef FSP_TXT
#define FSP_TXT 0
#endif

#ifndef FSP_IN_BAND_ECC
#define FSP_IN_BAND_ECC 1
#endif

/* TM1/TM2 throttling at the thermal-control limit, the FSP default */
#ifndef FSP_THERMAL_THROTTLING
#define FSP_THERMAL_THROTTLING 1
#endif

/* PCIe switch lane configuration, strapped on GPP_B21/B22 (MUX_SEL1/0) and
 * GPP_S4/S7/S6 (BOOTCFG2/1/0); the BIOS default is 8x1 */
#if defined(NAI_68INT6_PCIE_LANES_1x4_4x1)
#define NAI_PCIE_STRAP_MUX_SEL1 1
#define NAI_PCIE_STRAP_MUX_SEL0 0
#define NAI_PCIE_STRAP_BOOTCFG2 0
#define NAI_PCIE_STRAP_BOOTCFG1 0
#define NAI_PCIE_STRAP_BOOTCFG0 1
#elif defined(NAI_68INT6_PCIE_LANES_4x1_1x4)
#define NAI_PCIE_STRAP_MUX_SEL1 0
#define NAI_PCIE_STRAP_MUX_SEL0 1
#define NAI_PCIE_STRAP_BOOTCFG2 0
#define NAI_PCIE_STRAP_BOOTCFG1 1
#define NAI_PCIE_STRAP_BOOTCFG0 0
#elif defined(NAI_68INT6_PCIE_LANES_1x4_1x4)
#define NAI_PCIE_STRAP_MUX_SEL1 0
#define NAI_PCIE_STRAP_MUX_SEL0 0
#define NAI_PCIE_STRAP_BOOTCFG2 0
#define NAI_PCIE_STRAP_BOOTCFG1 1
#define NAI_PCIE_STRAP_BOOTCFG0 1
#elif defined(NAI_68INT6_PCIE_LANES_1x8)
#define NAI_PCIE_STRAP_MUX_SEL1 0
#define NAI_PCIE_STRAP_MUX_SEL0 0
#define NAI_PCIE_STRAP_BOOTCFG2 1
#define NAI_PCIE_STRAP_BOOTCFG1 0
#define NAI_PCIE_STRAP_BOOTCFG0 0
#else /* 8x1 */
#define NAI_PCIE_STRAP_MUX_SEL1 1
#define NAI_PCIE_STRAP_MUX_SEL0 1
#define NAI_PCIE_STRAP_BOOTCFG2 0
#define NAI_PCIE_STRAP_BOOTCFG1 0
#define NAI_PCIE_STRAP_BOOTCFG0 0
#endif

/* PCIe switch non-transparent mode (GPP_S2) */
#ifndef NAI_68INT6_PCIE_NT
#define NAI_68INT6_PCIE_NT 0
#endif
#if NAI_68INT6_PCIE_NT != 0 && NAI_68INT6_PCIE_NT != 1
#error "NAI_68INT6_PCIE_NT must be 0 or 1"
#endif

/* Backplane PCIe reference clock (GPP_A0) */
#ifndef NAI_68INT6_BACKPLANE_CLK
#define NAI_68INT6_BACKPLANE_CLK 0
#endif
#if NAI_68INT6_BACKPLANE_CLK != 0 && NAI_68INT6_BACKPLANE_CLK != 1
#error "NAI_68INT6_BACKPLANE_CLK must be 0 or 1"
#endif

/* Seconds to wait after the straps, before CPU PCIe link training */
#ifndef NAI_68INT6_PCIE_LINK_DELAY_S
#define NAI_68INT6_PCIE_LINK_DELAY_S 0
#endif
#if NAI_68INT6_PCIE_LINK_DELAY_S < 0 || NAI_68INT6_PCIE_LINK_DELAY_S > 5
#error "NAI_68INT6_PCIE_LINK_DELAY_S must be 0 to 5"
#endif

/* Seconds to wait before polling PCIE_RDY */
#ifndef NAI_68INT6_BOOT_DELAY_S
#define NAI_68INT6_BOOT_DELAY_S 0
#endif
#if NAI_68INT6_BOOT_DELAY_S < 0 || NAI_68INT6_BOOT_DELAY_S > 255
#error "NAI_68INT6_BOOT_DELAY_S must be 0 to 255"
#endif

/* Extra seconds added to the 5 s PCIE_RDY timeout */
#ifndef NAI_68INT6_FPGA_READY_DELAY_S
#define NAI_68INT6_FPGA_READY_DELAY_S 0
#endif
#if NAI_68INT6_FPGA_READY_DELAY_S != 0 && \
    NAI_68INT6_FPGA_READY_DELAY_S != 5 && NAI_68INT6_FPGA_READY_DELAY_S != 10
#error "NAI_68INT6_FPGA_READY_DELAY_S must be 0, 5 or 10"
#endif

#endif
