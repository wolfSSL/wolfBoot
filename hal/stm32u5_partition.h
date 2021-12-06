/* stm32u5_partition.h
 *
 * Copyright (C) 2021 wolfSSL Inc.
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

#ifndef STM32U5_PARTITION_H
#define STM32U5_PARTITION_H

#define SCS_BASE  (0xE000E000UL)
#define SCS_NS_BASE  (0xE002E000UL)
#define SCB_BASE  (SCS_BASE + 0x0D00UL)
#define SCB_NS_BASE  (SCS_BASE + 0x0D00UL)
#define SAU_BASE  (SCS_BASE + 0x0DD0UL)
#define FPU_BASE  (SCS_BASE + 0x0F30UL)
#define NVIC_BASE (SCS_BASE + 0x0100UL)

#define SAU_CTRL      (*(volatile uint32_t *)(SAU_BASE + 0x00))
#define SAU_RNR       (*(volatile uint32_t *)(SAU_BASE + 0x08))
#define SAU_RBAR      (*(volatile uint32_t *)(SAU_BASE + 0x0C))
#define SAU_RLAR      (*(volatile uint32_t *)(SAU_BASE + 0x10))

#define SCB_CPACR     (*(volatile uint32_t *)(SCB_BASE + 0x88))
#define SCB_NSACR     (*(volatile uint32_t *)(SCB_BASE + 0x8C))
#define SCB_VTOR      (*(volatile uint32_t *)(SCB_BASE + 0x08))
#define FPU_FPCCR     (*(volatile uint32_t *)(FPU_BASE + 0x04))

#define SCB_NS_CPACR  (*(volatile uint32_t *)(SCB_NS_BASE + 0x88))

/* SAU Control Register Definitions */
#define SAU_CTRL_ALLNS_Pos                  1U                                            /*!< SAU CTRL: ALLNS Position */
#define SAU_CTRL_ALLNS_Msk                 (1UL << SAU_CTRL_ALLNS_Pos)                    /*!< SAU CTRL: ALLNS Mask */

#define SAU_CTRL_ENABLE_Pos                 0U                                            /*!< SAU CTRL: ENABLE Position */
#define SAU_CTRL_ENABLE_Msk                (1UL /*<< SAU_CTRL_ENABLE_Pos*/)               /*!< SAU CTRL: ENABLE Mask */

/* SAU Type Register Definitions */
#define SAU_TYPE_SREGION_Pos                0U                                            /*!< SAU TYPE: SREGION Position */
#define SAU_TYPE_SREGION_Msk               (0xFFUL /*<< SAU_TYPE_SREGION_Pos*/)           /*!< SAU TYPE: SREGION Mask */

/* SAU Region Number Register Definitions */
#define SAU_RNR_REGION_Pos                  0U                                            /*!< SAU RNR: REGION Position */
#define SAU_RNR_REGION_Msk                 (0xFFUL /*<< SAU_RNR_REGION_Pos*/)             /*!< SAU RNR: REGION Mask */

/* SAU Region Base Address Register Definitions */
#define SAU_RBAR_BADDR_Pos                  5U                                            /*!< SAU RBAR: BADDR Position */
#define SAU_RBAR_BADDR_Msk                 (0x7FFFFFFUL << SAU_RBAR_BADDR_Pos)            /*!< SAU RBAR: BADDR Mask */

/* SAU Region Limit Address Register Definitions */
#define SAU_RLAR_LADDR_Pos                  5U                                            /*!< SAU RLAR: LADDR Position */
#define SAU_RLAR_LADDR_Msk                 (0x7FFFFFFUL << SAU_RLAR_LADDR_Pos)            /*!< SAU RLAR: LADDR Mask */

#define SAU_RLAR_NSC_Pos                    1U                                            /*!< SAU RLAR: NSC Position */
#define SAU_RLAR_NSC_Msk                   (1UL << SAU_RLAR_NSC_Pos)                      /*!< SAU RLAR: NSC Mask */

#define SAU_RLAR_ENABLE_Pos                 0U                                            /*!< SAU RLAR: ENABLE Position */
#define SAU_RLAR_ENABLE_Msk                (1UL /*<< SAU_RLAR_ENABLE_Pos*/)               /*!< SAU RLAR: ENABLE Mask */

/* SCB Non-Secure Access Control Register Definitions */
#define SCB_NSACR_CP11_Pos                 11U                                            /*!< SCB NSACR: CP11 Position */
#define SCB_NSACR_CP11_Msk                 (1UL << SCB_NSACR_CP11_Pos)                    /*!< SCB NSACR: CP11 Mask */

#define SCB_NSACR_CP10_Pos                 10U                                            /*!< SCB NSACR: CP10 Position */
#define SCB_NSACR_CP10_Msk                 (1UL << SCB_NSACR_CP10_Pos)                    /*!< SCB NSACR: CP10 Mask */

#define SCB_NSACR_CPn_Pos                   0U                                            /*!< SCB NSACR: CPn Position */
#define SCB_NSACR_CPn_Msk                  (1UL /*<< SCB_NSACR_CPn_Pos*/)

#define FPU_FPCCR_CLRONRET_Pos             28U                                            /*!< FPCCR: CLRONRET Position */
#define FPU_FPCCR_CLRONRET_Msk             (1UL << FPU_FPCCR_CLRONRET_Pos)                /*!< FPCCR: CLRONRET bit Mask */

#define FPU_FPCCR_CLRONRETS_Pos            27U                                            /*!< FPCCR: CLRONRETS Position */
#define FPU_FPCCR_CLRONRETS_Msk            (1UL << FPU_FPCCR_CLRONRETS_Pos)               /*!< FPCCR: CLRONRETS bit Mask */

#define FPU_FPCCR_TS_Pos                   26U                                            /*!< FPCCR: TS Position */
#define FPU_FPCCR_TS_Msk                   (1UL << FPU_FPCCR_TS_Pos)

/*
//   <q> Enable SAU
//   <i> Value for SAU->CTRL register bit ENABLE
*/
#define SAU_INIT_CTRL_ENABLE   0

/*
//   <o> When SAU is disabled
//     <0=> All Memory is Secure
//     <1=> All Memory is Non-Secure
//   <i> Value for SAU->CTRL register bit ALLNS
//   <i> When all Memory is Non-Secure (ALLNS is 1), IDAU can override memory map configuration.
*/
#define SAU_INIT_CTRL_ALLNS   1

/*
// <h>Initialize Security Attribution Unit (SAU) Address Regions
// <i>SAU configuration specifies regions to be one of:
// <i> - Secure and Non-Secure Callable
// <i> - Non-Secure
// <i>Note: All memory regions not configured by SAU are Secure
*/
#define SAU_REGIONS_MAX   8                 /* Max. number of SAU regions */

/*
//   <e>Initialize SAU Region 0
//   <i> Setup SAU Region 0 memory attributes
*/
#define SAU_INIT_REGION0    0

/*
//     <o>Start Address <0-0xFFFFFFE0>
*/

#define SAU_INIT_START0     0x0C0FE000      /* start address of SAU region 0 */

/*
//     <o>End Address <0x1F-0xFFFFFFFF>
*/
#define SAU_INIT_END0       0x0C0FFFFF      /* end address of SAU region 0 */

/*
//     <o>Region is
//         <0=>Non-Secure
//         <1=>Secure, Non-Secure Callable
*/
#define SAU_INIT_NSC0       1

/*
//   <e>Initialize SAU Region 1
//   <i> Setup SAU Region 1 memory attributes
*/
#define SAU_INIT_REGION1    0

/*
//     <o>Start Address <0-0xFFFFFFE0>
*/
#define SAU_INIT_START1     0x08100000      /* start address of SAU region 1 */
/*
//     <o>End Address <0x1F-0xFFFFFFFF>
*/
#define SAU_INIT_END1       0x081FFFFF      /* end address of SAU region 1 */
/*
//     <o>Region is
//         <0=>Non-Secure
//         <1=>Secure, Non-Secure Callable
*/
#define SAU_INIT_NSC1       0

/*
//   <e>Initialize SAU Region 2
//   <i> Setup SAU Region 2 memory attributes
*/
#define SAU_INIT_REGION2    0

/*
//     <o>Start Address <0-0xFFFFFFE0>
*/
#define SAU_INIT_START2     0x20040000      /* start address of SAU region 2 */

/*
//     <o>End Address <0x1F-0xFFFFFFFF>
*/
#define SAU_INIT_END2       0x200BFFFF      /* end address of SAU region 2 */

/*
//     <o>Region is
//         <0=>Non-Secure
//         <1=>Secure, Non-Secure Callable
*/
#define SAU_INIT_NSC2       0

/*
//   <e>Initialize SAU Region 3
//   <i> Setup SAU Region 3 memory attributes
*/
#define SAU_INIT_REGION3    0

/*
//     <o>Start Address <0-0xFFFFFFE0>
*/
#define SAU_INIT_START3     0x40000000      /* start address of SAU region 3 */

/*
//     <o>End Address <0x1F-0xFFFFFFFF>
*/
#define SAU_INIT_END3       0x4FFFFFFF      /* end address of SAU region 3 */

/*
//     <o>Region is
//         <0=>Non-Secure
//         <1=>Secure, Non-Secure Callable
*/
#define SAU_INIT_NSC3       0

/*
//   <e>Initialize SAU Region 4
//   <i> Setup SAU Region 4 memory attributes
*/
#define SAU_INIT_REGION4    0

/*
//     <o>Start Address <0-0xFFFFFFE0>
*/
#define SAU_INIT_START4     0x60000000      /* start address of SAU region 4 */

/*
//     <o>End Address <0x1F-0xFFFFFFFF>
*/
#define SAU_INIT_END4       0x9FFFFFFF      /* end address of SAU region 4 */

/*
//     <o>Region is
//         <0=>Non-Secure
//         <1=>Secure, Non-Secure Callable
*/
#define SAU_INIT_NSC4       0

/*
//   <e>Initialize SAU Region 5
//   <i> Setup SAU Region 5 memory attributes
*/
#define SAU_INIT_REGION5    0

/*
//     <o>Start Address <0-0xFFFFFFE0>
*/
#define SAU_INIT_START5     0x0BF90000      /* start address of SAU region 5 */

/*
//     <o>End Address <0x1F-0xFFFFFFFF>
*/
#define SAU_INIT_END5       0x0BFA8FFF      /* end address of SAU region 5 */

/*
//     <o>Region is
//         <0=>Non-Secure
//         <1=>Secure, Non-Secure Callable
*/
#define SAU_INIT_NSC5       0

/*
//   <e>Initialize SAU Region 6
//   <i> Setup SAU Region 6 memory attributes
*/
#define SAU_INIT_REGION6    0

/*
//     <o>Start Address <0-0xFFFFFFE0>
*/
#define SAU_INIT_START6     0x00000000      /* start address of SAU region 6 */

/*
//     <o>End Address <0x1F-0xFFFFFFFF>
*/
#define SAU_INIT_END6       0x00000000      /* end address of SAU region 6 */

/*
//     <o>Region is
//         <0=>Non-Secure
//         <1=>Secure, Non-Secure Callable
*/
#define SAU_INIT_NSC6       0

/*
//   <e>Initialize SAU Region 7
//   <i> Setup SAU Region 7 memory attributes
*/
#define SAU_INIT_REGION7    0

/*
//     <o>Start Address <0-0xFFFFFFE0>
*/
#define SAU_INIT_START7     0x00000000      /* start address of SAU region 7 */

/*
//     <o>End Address <0x1F-0xFFFFFFFF>
*/
#define SAU_INIT_END7       0x00000000      /* end address of SAU region 7 */

/*
//     <o>Region is
//         <0=>Non-Secure
//         <1=>Secure, Non-Secure Callable
*/
#define SAU_INIT_NSC7       0

// <e>Setup behaviour of Floating Point Unit

#define TZ_FPU_NS_USAGE 1

/*
// <o>Floating Point Unit usage
//     <0=> Secure state only
//     <3=> Secure and Non-Secure state
//   <i> Value for SCB->NSACR register bits CP10, CP11
*/
#define SCB_NSACR_CP10_11_VAL       3

/*
// <o>Treat floating-point registers as Secure
//     <0=> Disabled
//     <1=> Enabled
//   <i> Value for FPU->FPCCR register bit TS
*/
#define FPU_FPCCR_TS_VAL            0

/*
// <o>Clear on return (CLRONRET) accessibility
//     <0=> Secure and Non-Secure state
//     <1=> Secure state only
//   <i> Value for FPU->FPCCR register bit CLRONRETS
*/
#define FPU_FPCCR_CLRONRETS_VAL     0

/*
// <o>Clear floating-point caller saved registers on exception return
//     <0=> Disabled
//     <1=> Enabled
//   <i> Value for FPU->FPCCR register bit CLRONRET
*/
#define FPU_FPCCR_CLRONRET_VAL      1


#define SAU_INIT_REGION(n) \
    SAU_RNR  =  (n                                     & SAU_RNR_REGION_Msk); \
    SAU_RBAR =  (SAU_INIT_START##n                     & SAU_RBAR_BADDR_Msk); \
    SAU_RLAR =  (SAU_INIT_END##n                       & SAU_RLAR_LADDR_Msk) | \
                ((SAU_INIT_NSC##n << SAU_RLAR_NSC_Pos)  & SAU_RLAR_NSC_Msk)   | 1U

#define GTZC1_MPCBB3_S_BASE        (0x50033400)
#define GTZC1_MPCBB3_S_CR          (*(volatile uint32_t *)(GTZC1_MPCBB3_S_BASE + 0x00))
#define GTZC1_MPCBB3_S_CFGLOCKR1   (*(volatile uint32_t *)(GTZC1_MPCBB3_S_BASE + 0x10))
#define GTZC1_MPCBB3_S_SECCFGR0    (GTZC1_MPCBB3_S_BASE + 0x100)
#define GTZC1_MPCBB3_S_PRIVCFGR0   (GTZC1_MPCBB3_S_BASE + 0x200)

#define SET_GTZC1_MPCBBx_SECCFGR(x,n) \
    (*((volatile uint32_t *)(GTZC1_MPCBB##x##_S_SECCFGR0 ) + n ))= GTZC1_MPCBB##x##_S_SECCFGR##n##_VAL

#define SET_GTZC1_MPCBBx_PRIVCFGR(x,n) \
    (*((volatile uint32_t *)(GTZC1_MPCBB##x##_S_PRIVCFGR0 ) + n ))= GTZC1_MPCBB##x##_S_PRIVCFGR##n##_VAL

/*SRAM3 - SECCFG*/
#define GTZC1_MPCBB3_S_SECCFGR0_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR1_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR2_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR3_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR4_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR5_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR6_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR7_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR8_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR9_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR10_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR11_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR12_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR13_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR14_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR15_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR16_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR17_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR18_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR19_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR20_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR21_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR22_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR23_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR24_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR25_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR26_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR27_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR28_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR29_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR30_VAL (0x00000000)
#define GTZC1_MPCBB3_S_SECCFGR31_VAL (0x00000000)

/*SRAM3 - PRIVCFG*/
#define GTZC1_MPCBB3_S_PRIVCFGR0_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR1_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR2_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR3_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR4_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR5_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR6_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR7_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR8_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR9_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR10_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR11_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR12_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR13_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR14_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR15_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR16_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR17_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR18_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR19_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR20_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR21_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR22_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR23_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR24_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR25_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR26_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR27_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR28_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR29_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR30_VAL (0x00000000)
#define GTZC1_MPCBB3_S_PRIVCFGR31_VAL (0x00000000)

/**
  \brief   Setup a SAU Region
  \details Writes the region information contained in SAU_Region to the
           registers SAU_RNR, SAU_RBAR, and SAU_RLAR
 */
static __inline void TZ_SAU_Setup (void)
{

  #if defined (SAU_INIT_REGION0) && (SAU_INIT_REGION0 == 1U)
    SAU_INIT_REGION(0);
  #endif

  #if defined (SAU_INIT_REGION1) && (SAU_INIT_REGION1 == 1U)
    SAU_INIT_REGION(1);
  #endif

  #if defined (SAU_INIT_REGION2) && (SAU_INIT_REGION2 == 1U)
    SAU_INIT_REGION(2);
  #endif

  #if defined (SAU_INIT_REGION3) && (SAU_INIT_REGION3 == 1U)
    SAU_INIT_REGION(3);
  #endif

  #if defined (SAU_INIT_REGION4) && (SAU_INIT_REGION4 == 1U)
    SAU_INIT_REGION(4);
  #endif

  #if defined (SAU_INIT_REGION5) && (SAU_INIT_REGION5 == 1U)
    SAU_INIT_REGION(5);
  #endif

  #if defined (SAU_INIT_REGION6) && (SAU_INIT_REGION6 == 1U)
    SAU_INIT_REGION(6);
  #endif

  #if defined (SAU_INIT_REGION7) && (SAU_INIT_REGION7 == 1U)
    SAU_INIT_REGION(7);
  #endif

    SAU_CTRL = ((SAU_INIT_CTRL_ENABLE << SAU_CTRL_ENABLE_Pos) & SAU_CTRL_ENABLE_Msk) |
                ((SAU_INIT_CTRL_ALLNS  << SAU_CTRL_ALLNS_Pos)  & SAU_CTRL_ALLNS_Msk)   ;

  #if defined (TZ_FPU_NS_USAGE) && (TZ_FPU_NS_USAGE == 1U)

    SCB_NSACR = (SCB_NSACR & ~(SCB_NSACR_CP10_Msk | SCB_NSACR_CP11_Msk)) |
                   ((SCB_NSACR_CP10_11_VAL << SCB_NSACR_CP10_Pos) & (SCB_NSACR_CP10_Msk | SCB_NSACR_CP11_Msk));

    SCB_CPACR |= ((0x3 << 20)|(0x3 << 22));     /* set CP10 and CP11 Full Access */

    FPU_FPCCR = (FPU_FPCCR & ~(FPU_FPCCR_TS_Msk | FPU_FPCCR_CLRONRETS_Msk | FPU_FPCCR_CLRONRET_Msk)) |
                   ((FPU_FPCCR_TS_VAL        << FPU_FPCCR_TS_Pos       ) & FPU_FPCCR_TS_Msk       ) |
                   ((FPU_FPCCR_CLRONRETS_VAL << FPU_FPCCR_CLRONRETS_Pos) & FPU_FPCCR_CLRONRETS_Msk) |
                   ((FPU_FPCCR_CLRONRET_VAL  << FPU_FPCCR_CLRONRET_Pos ) & FPU_FPCCR_CLRONRET_Msk );

  #endif

}

#endif  /* STM32U5_PARTITION_H */
