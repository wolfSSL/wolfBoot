/* spi_drv_stm32.h
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

#ifndef SPI_DRV_STM32_H_INCLUDED
#define SPI_DRV_STM32_H_INCLUDED

#include <stdint.h>

#ifdef PLATFORM_stm32f4
#define APB2_CLOCK_ER     (*(volatile uint32_t *)(0x40023844))
#define APB2_CLOCK_RST    (*(volatile uint32_t *)(0x40023824))
#define RCC_GPIO_CLOCK_ER (*(volatile uint32_t *)(0x40023830))
#define GPIOA_BASE (0x40020000)
#define GPIOB_BASE (0x40020400)
#define GPIOC_BASE (0x40020800)
#define GPIOD_BASE (0x40020C00)
#define GPIOE_BASE (0x40021000)
#define GPIO_BASE GPIOA_BASE
#define SPI_GPIO      GPIOB_BASE
#define SPI_CS_GPIO   GPIOE_BASE
#define SPI_CS_FLASH  1 /* Flash CS connected to GPIOE1 */
#define SPI_CS_TPM    0 /* TPM CS connected to GPIOE0 */
#define SPI_PIN_AF    5 /* Alternate function for SPI pins */
#define SPI_CLOCK_PIN 3 /* SPI_SCK: PB3  */
#define SPI_MISO_PIN  4 /* SPI_MISO PB4  */
#define SPI_MOSI_PIN  5 /* SPI_MOSI PB5  */
#endif /* PLATFORM_stm32f4 */


#ifdef PLATFORM_stm32u5
#ifdef TZEN
#define PERIPH_BASE (0x50000000UL)
#else
#define PERIPH_BASE (0x40000000UL)
#endif

#define APB2PERIPH_BASE (PERIPH_BASE + 0x00010000UL)
#define AHB2PERIPH_BASE (PERIPH_BASE + 0x02020000UL)

#define SPI1_BASE (APB2PERIPH_BASE + 0x3000UL)

#define RCC_BASE  (APB2PERIPH_BASE + 0x0C00UL)
#define APB2_CLOCK_ER     (*(volatile uint32_t *)(RCC_BASE + 0xA4))
#define APB2_CLOCK_RST    (*(volatile uint32_t *)(RCC_BASE + 0x7C))
#define RCC_GPIO_CLOCK_ER (*(volatile uint32_t *)(RCC_BASE + 0x8C))

#define GPIO_BASE  (APB2PERIPH_BASE + 0x02020000UL)
#define GPIOA_BASE (GPIO_BASE + 0x00000UL)
#define GPIOB_BASE (GPIO_BASE + 0x00400UL)
#define GPIOC_BASE (GPIO_BASE + 0x00800UL)
#define GPIOD_BASE (GPIO_BASE + 0x00C00UL)
#define GPIOE_BASE (GPIO_BASE + 0x01000UL)
#define GPIOF_BASE (GPIO_BASE + 0x01400UL)
#define GPIOG_BASE (GPIO_BASE + 0x01800UL)
#define GPIOH_BASE (GPIO_BASE + 0x01C00UL)
#define GPIOI_BASE (GPIO_BASE + 0x02000UL)

/* STMOD+ Port */
#define SPI_GPIO      GPIOE_BASE
#define SPI_CS_GPIO   GPIOE_BASE
#define SPI_CS_FLASH  11 /* Flash CS connected to GPIOE11 */
#define SPI_CS_TPM    10 /* TPM   CS connected to GPIOE10 */

#define SPI_PIN_AF    5 /* Alternate function for SPI pins */
#define SPI_CLOCK_PIN 13 /* SPI_SCK: PE13  */
#define SPI_MISO_PIN  14 /* SPI_MISO PE14 */
#define SPI_MOSI_PIN  15 /* SPI_MOSI PE15  */
#endif /* PLATFORM_stm32u5 */


#ifdef PLATFORM_stm32wb
#define APB2_CLOCK_ER     (*(volatile uint32_t *)(0x58000060))
#define APB2_CLOCK_RST    (*(volatile uint32_t *)(0x58000040))
#define RCC_GPIO_CLOCK_ER (*(volatile uint32_t *)(0x5800004C))
#define GPIOA_BASE (0x48000000)
#define GPIOB_BASE (0x48000400)
#define GPIOC_BASE (0x48000800)
#define GPIOD_BASE (0x48000C00)

/* STM32WB55 NUCLEO: CN9: D13=SCK, D12=MISO, D11=MOSI, FLASHCS=D10, TPMCS=D9 */
#define SPI_GPIO      GPIOA_BASE
#define SPI_CS_GPIO   GPIOA_BASE
#define SPI_CS_FLASH  4 /* Flash CS connected to GPIOA4 */
#define SPI_CS_TPM    9 /* TPM CS connected to GPIOA9 */
#define SPI_PIN_AF    5 /* Alternate function for SPI pins */
#define SPI_CLOCK_PIN 5 /* SPI_SCK: PA5  */
#define SPI_MISO_PIN  6 /* SPI_MISO PA6  */
#define SPI_MOSI_PIN  7 /* SPI_MOSI PA7  */
#endif /* PLATFORM_stm32wb */


#ifdef PLATFORM_stm32l0
#define APB2_CLOCK_ER     (*(volatile uint32_t *)(0x40021034))
#define APB2_CLOCK_RST    (*(volatile uint32_t *)(0x40021024))
#define RCC_GPIO_CLOCK_ER (*(volatile uint32_t *)(0x4002102C))
#define GPIOA_BASE (0x50000000)
#define GPIOB_BASE (0x50000400)

#define SPI_CS_GPIO GPIOB_BASE
#define SPI_PIN_AF    0 /* Alternate function for SPI pins */

#ifndef SPI_ALT_CONFIGURATION
    #define SPI_GPIO      GPIOB_BASE
    #define SPI_CS_FLASH  8 /* Flash CS connected to GPIOB8 */
    #define SPI_CLOCK_PIN 3 /* SPI_SCK: PB3  */
    #define SPI_MISO_PIN  4 /* SPI_MISO PB4  */
    #define SPI_MOSI_PIN  5 /* SPI_MOSI PB5  */
#else
    #define SPI_GPIO      GPIOA_BASE
    #define SPI_CS_FLASH  6 /* Flash CS connected to GPIOB6 */
    #define SPI_CLOCK_PIN 5 /* SPI_SCK: PA5  */
    #define SPI_MISO_PIN  6 /* SPI_MISO PA6  */
    #define SPI_MOSI_PIN  7 /* SPI_MOSI PA7  */
#endif /* SPI_ALT_CONFIGURATION */
#endif /* PLATFORM_stm32l0 */


#ifdef PLATFORM_stm32h7

#define RCC_BASE          (0x58024400UL)
#define RCC_GPIO_CLOCK_ER (*(volatile uint32_t *)(RCC_BASE + 0xE0)) /* RM0433 - 8.7.43 (RCC_AHB4ENR) */
#define APB2_CLOCK_ER     (*(volatile uint32_t *)(RCC_BASE + 0xF0)) /* RM0433 - 8.7.47 */
#define APB2_CLOCK_RST    (*(volatile uint32_t *)(RCC_BASE + 0x98)) /* RM0433 - 8.7.35 */

#define DLYB_QSPI_BASE    (0x52006000UL)
#define DLYB_CR(base)     (*(volatile uint32_t *)((base) + 0x00)) /* Control register */
#define DLYB_CFGR(base)   (*(volatile uint32_t *)((base) + 0x04)) /* Configuration register */

#define DLYB_CR_SEN (1 << 1) /* Sampler length enable bit */
#define DLYB_CR_DEN (1 << 0) /* Delay block enable bit */


#define GPIOA_BASE (0x58020000)
#define GPIOB_BASE (0x58020400)
#define GPIOC_BASE (0x58020800)
#define GPIOD_BASE (0x58020C00)
#define GPIOE_BASE (0x58021000)
#define GPIOF_BASE (0x58021400)
#define GPIOG_BASE (0x58021800)
#define GPIOH_BASE (0x58021C00)
#define GPIOI_BASE (0x58022000)
#define GPIOJ_BASE (0x58022400)
#define GPIOK_BASE (0x58022800)


/** QSPI **/
/* Default Base (E) and AF (alternate function=10) for QSPI */
#define QSPI_GPIO            GPIOE_BASE
#define QSPI_PIN_AF          10
/* Default to flash bank 1 (2 for alt) */
#ifndef QSPI_FLASH_BANK
    #ifdef QSPI_ALT_CONFIGURATION
        #define QSPI_FLASH_BANK      2
    #else
        #define QSPI_FLASH_BANK      1
    #endif
#endif
/* Default flash size to 16MB */
#ifndef QSPI_FLASH_SIZE
#define QSPI_FLASH_SIZE      23 /* 2^24 = 16 MB */
#endif

/* QSPI uses hclk3 (240MHz max) by default */
#define RCC_D1CCIPR        (*(volatile uint32_t *)(RCC_BASE + 0x4C)) /* RM0433 - 8.7.19 (RCC_D1CCIPR) */
#define RCC_D1CCIPR_QSPISEL_MASK (0x3 << 4)
#define RCC_D1CCIPR_QSPISEL(sel) (((sel) & 0x3) << 4) /* 0=hclk3, 1=pll1_q_ck, 2=pll2_r_ck, 3=per_ck */
#define RCC_D1CCIPR_QSPISEL_HCLK3 0
#define RCC_D1CCIPR_QSPISEL_PLL1  1
#define RCC_D1CCIPR_QSPISEL_PLL2  2
#define RCC_D1CCIPR_QSPISEL_PERCK 3
#define AHB3_CLOCK_RST     (*(volatile uint32_t *)(RCC_BASE + 0x7C)) /* RM0433 - 8.7.28 - RCC_AHB3RSTR */
#define AHB3_CLOCK_EN      (*(volatile uint32_t *)(RCC_BASE + 0xD4)) /* RM0433 - 8.7.40 - RCC_AHB3ENR */
#define RCC_AHB3ENR_QSPIEN (1 << 14) /* QUADSPI/OCTOSPI clock enabled */

#define HCLK3_MHZ       240000000
#define PERCK_MHZ       64000000
#ifndef QSPI_CLOCK_BASE
#define QSPI_CLOCK_BASE PERCK_MHZ
#endif
#ifndef QSPI_CLOCK_SEL
/* Enable QUADSPI clock on PER_CK (64MHz) */
#define QSPI_CLOCK_SEL  RCC_D1CCIPR_QSPISEL_PERCK
#endif
#ifndef QSPI_CLOCK_MHZ
#define QSPI_CLOCK_MHZ  16000000
#endif

/* QSPI CLK PB2 (alt OCTOSPIM_P1_CLK)*/
#define QSPI_CLOCK_PIO_BASE  GPIOB_BASE
#define QSPI_CLOCK_PIN       2
#define QSPI_CLOCK_PIN_AF    9

/* nQSPI_CS PG6 (alt PB6 -> OCTOSPIM_P1_NCS) */
#ifndef QSPI_ALT_CONFIGURATION
#define QSPI_CS_PIO_BASE     GPIOG_BASE
#define QSPI_CS_FLASH_PIN    6
#else
#define QSPI_CS_PIO_BASE     GPIOB_BASE
#define QSPI_CS_FLASH_PIN    6
#endif

/* QSPI_IO0 (MOSI) - PD11 (alt PE7 -> OCTOSPIM_P1_IO4) */
#ifndef QSPI_ALT_CONFIGURATION
#define QSPI_IO0_PIO_BASE   GPIOD_BASE
#define QSPI_IO0_PIN        11
#define QSPI_IO0_PIN_AF     9
#else
#define QSPI_IO0_PIO_BASE   GPIOE_BASE
#define QSPI_IO0_PIN        7
#endif

/* QSPI_IO1 (MISO) - PD12 (alt PE8 -> OCTOSPIM_P1_IO5) */
#ifndef QSPI_ALT_CONFIGURATION
#define QSPI_IO1_PIO_BASE   GPIOD_BASE
#define QSPI_IO1_PIN        12
#define QSPI_IO1_PIN_AF     9
#else
#define QSPI_IO1_PIO_BASE   GPIOE_BASE
#define QSPI_IO1_PIN        8
#endif

/* QSPI_IO2 - PE2 (alt PE9 -> OCTOSPIM_P1_IO6) */
#ifndef QSPI_ALT_CONFIGURATION
#define QSPI_IO2_PIO_BASE   GPIOE_BASE
#define QSPI_IO2_PIN        2
#define QSPI_IO2_PIN_AF     9
#else
#define QSPI_IO2_PIO_BASE   GPIOE_BASE
#define QSPI_IO2_PIN        9
#endif

/* QSPI_IO3 - PD13 (alt PE10 -> OCTOSPIM_P1_IO7) */
#ifndef QSPI_ALT_CONFIGURATION
#define QSPI_IO3_PIO_BASE   GPIOD_BASE
#define QSPI_IO3_PIN        13
#define QSPI_IO3_PIN_AF     9
#else
#define QSPI_IO3_PIO_BASE   GPIOE_BASE
#define QSPI_IO3_PIN        10
#endif

#endif /* PLATFORM_stm32h7 */


/* Setup SPI PIO Bases */
#ifndef SPI_CLOCK_PIO_BASE
#define SPI_CLOCK_PIO_BASE SPI_GPIO
#endif
#ifndef SPI_MISO_PIO_BASE
#define SPI_MISO_PIO_BASE  SPI_GPIO
#endif
#ifndef SPI_MOSI_PIO_BASE
#define SPI_MOSI_PIO_BASE  SPI_GPIO
#endif
#ifndef SPI_CS_PIO_BASE
#define SPI_CS_PIO_BASE    SPI_CS_GPIO
#endif
#ifndef SPI_CS_TPM_PIO_BASE
#define SPI_CS_TPM_PIO_BASE SPI_CS_GPIO
#endif

#if defined(QSPI_FLASH) || defined(OCTOSPI_FLASH)
#ifndef QSPI_CS_PIO_BASE
#define QSPI_CS_PIO_BASE    QSPI_CS_GPIO
#endif
#ifndef QSPI_CLOCK_PIO_BASE
#define QSPI_CLOCK_PIO_BASE QSPI_GPIO
#endif
#ifndef QSPI_IO0_PIO_BASE
#define QSPI_IO0_PIO_BASE   QSPI_GPIO
#endif
#ifndef QSPI_IO1_PIO_BASE
#define QSPI_IO1_PIO_BASE   QSPI_GPIO
#endif
#ifndef QSPI_IO2_PIO_BASE
#define QSPI_IO2_PIO_BASE   QSPI_GPIO
#endif
#ifndef QSPI_IO3_PIO_BASE
#define QSPI_IO3_PIO_BASE   QSPI_GPIO
#endif
#endif /* QSPI_FLASH || OCTOSPI_FLASH */

/* Setup alternate functions */
#ifndef SPI_CLOCK_PIN_AF
#define SPI_CLOCK_PIN_AF SPI_PIN_AF
#endif
#ifndef SPI_MISO_PIN_AF
#define SPI_MISO_PIN_AF  SPI_PIN_AF
#endif
#ifndef SPI_MOSI_PIN_AF
#define SPI_MOSI_PIN_AF  SPI_PIN_AF
#endif

#if defined(QSPI_FLASH) || defined(OCTOSPI_FLASH)
#ifndef QSPI_CS_FLASH_AF
#define QSPI_CS_FLASH_AF  QSPI_PIN_AF
#endif

#ifndef QSPI_CLOCK_PIN_AF
#define QSPI_CLOCK_PIN_AF QSPI_PIN_AF
#endif
#ifndef QSPI_IO0_PIN_AF
#define QSPI_IO0_PIN_AF   QSPI_PIN_AF
#endif
#ifndef QSPI_IO1_PIN_AF
#define QSPI_IO1_PIN_AF   QSPI_PIN_AF
#endif
#ifndef QSPI_IO2_PIN_AF
#define QSPI_IO2_PIN_AF   QSPI_PIN_AF
#endif
#ifndef QSPI_IO3_PIN_AF
#define QSPI_IO3_PIN_AF   QSPI_PIN_AF
#endif
#endif /* QSPI_FLASH || OCTOSPI_FLASH */


/* SPI */
#ifndef SPI1_BASE
#define SPI1_BASE (0x40013000) /* SPI1 base address */
#endif

#define SPI1_APB2_CLOCK_ER_VAL 	(1 << 12)

#define SPI1_CR1      (*(volatile uint32_t *)(SPI1_BASE))
#define SPI1_CR2      (*(volatile uint32_t *)(SPI1_BASE + 0x04))
#define SPI1_SR       (*(volatile uint32_t *)(SPI1_BASE + 0x08))
#define SPI1_DR       (*(volatile uint32_t *)(SPI1_BASE + 0x0c))

#define SPI_CR1_CLOCK_PHASE         (1 << 0)
#define SPI_CR1_CLOCK_POLARITY      (1 << 1)
#define SPI_CR1_MASTER	    		(1 << 2)
#define SPI_CR1_BAUDRATE        	(0x07 << 3)
#define SPI_CR1_SPI_EN		    	(1 << 6)
#define SPI_CR1_LSBFIRST		    (1 << 7)
#define SPI_CR1_SSI			        (1 << 8)
#define SPI_CR1_SSM			        (1 << 9)
#define SPI_CR1_16BIT_FORMAT        (1 << 11)
#define SPI_CR1_TX_CRC_NEXT			(1 << 12)
#define SPI_CR1_HW_CRC_EN			(1 << 13)
#define SPI_CR1_BIDIOE			    (1 << 14)
#define SPI_CR2_SSOE			    (1 << 2)

#define SPI_SR_RX_NOTEMPTY  	    (1 << 0)
#define SPI_SR_TX_EMPTY			    (1 << 1)
#define SPI_SR_BUSY			        (1 << 7)


/* GPIO */
#define GPIO_MODE(base)    (*(volatile uint32_t *)(base + 0x00)) /* GPIOx_MODER */
#define GPIO_OTYPE(base)   (*(volatile uint32_t *)(base + 0x04)) /* GPIOx_OTYPER */
#define GPIO_OSPD(base)    (*(volatile uint32_t *)(base + 0x08)) /* GPIOx_OSPEEDR */
#define GPIO_PUPD(base)    (*(volatile uint32_t *)(base + 0x0c)) /* GPIOx_PUPDR */
#define GPIO_ODR(base)     (*(volatile uint32_t *)(base + 0x14)) /* GPIOx_ODR */
#define GPIO_BSRR(base)    (*(volatile uint32_t *)(base + 0x18)) /* GPIOx_BSRR */
#define GPIO_AFL(base)     (*(volatile uint32_t *)(base + 0x20)) /* GPIOx_AFRL */
#define GPIO_AFH(base)     (*(volatile uint32_t *)(base + 0x24)) /* GPIOx_AFRH */

#define GPIO_MODE_INPUT  (0)
#define GPIO_MODE_OUTPUT (1)
#define GPIO_MODE_AF     (2)
#define GPIO_MODE_ANALOG (3)


/* QUADSPI */
#ifndef QUADSPI_BASE
#define QUADSPI_BASE      0x52005000UL
#endif

#define QUADSPI_CR                (*(volatile uint32_t *)(QUADSPI_BASE + 0x00)) /* Control register */
#define QUADSPI_DCR               (*(volatile uint32_t *)(QUADSPI_BASE + 0x04)) /* Device Configuration register */
#define QUADSPI_SR                (*(volatile uint32_t *)(QUADSPI_BASE + 0x08)) /* Status register */
#define QUADSPI_FCR               (*(volatile uint32_t *)(QUADSPI_BASE + 0x0C)) /* Flag Clear register */
#define QUADSPI_DLR               (*(volatile uint32_t *)(QUADSPI_BASE + 0x10)) /* Data Length register */
#define QUADSPI_CCR               (*(volatile uint32_t *)(QUADSPI_BASE + 0x14)) /* Communication Configuration register */
#define QUADSPI_AR                (*(volatile uint32_t *)(QUADSPI_BASE + 0x18)) /* Address register */
#define QUADSPI_ABR               (*(volatile uint32_t *)(QUADSPI_BASE + 0x1C)) /* Alternate Bytes register */
#define QUADSPI_DR                (*(volatile uint8_t  *)(QUADSPI_BASE + 0x20)) /* Data register */
#define QUADSPI_DR32              (*(volatile uint32_t *)(QUADSPI_BASE + 0x20)) /* Data register - 32 bit */
#define QUADSPI_PSMKR             (*(volatile uint32_t *)(QUADSPI_BASE + 0x24)) /* Polling Status Mask register */
#define QUADSPI_PSMAR             (*(volatile uint32_t *)(QUADSPI_BASE + 0x28)) /* Polling Status Match register */
#define QUADSPI_PIR               (*(volatile uint32_t *)(QUADSPI_BASE + 0x2C)) /* Polling Interval register */
#define QUADSPI_LPTR              (*(volatile uint32_t *)(QUADSPI_BASE + 0x30)) /* Low Power Timeout register */

#define QUADSPI_CR_PRESCALER_MASK (0xFF << 24)
#define QUADSPI_CR_PRESCALER(pre) ((((pre)-1) & 0xFF) << 24) /* Clock prescaler: quadspi_ker_ck/pre+1 */
#define QUADSPI_CR_FTIE           (1 << 13) /* FIFO threshold interrupt enable */
#define QUADSPI_CR_FTHRES_MASK    (0x1F << 8)
#define QUADSPI_CR_FTHRES(thr)    ((((thr)-1) & 0x1F) << 8) /* FIFO threshold level */
#define QUADSPI_CR_FSEL           (1 << 7)  /* 0=Flash 1 or 1=Flash 2 */
#define QUADSPI_CR_DFM            (1 << 6)  /* Dual-flash mode */
#define QUADSPI_CR_SSHIFT         (1 << 4)  /* Sample shift 1=1/2 cycle shift */
#define QUADSPI_CR_ABORT          (1 << 1)  /* Abort request */
#define QUADSPI_CR_EN             (1 << 0)  /* Enable the QUADSPI */

#define QUADSPI_CCR_DDRM          (1 << 31) /* Double data rate mode */
#define QUADSPI_CCR_DHHC          (1 << 30) /* Delay the data output by 1/4 of the QUADSPI output clock cycle in DDR mode */
#define QUADSPI_CCR_FRCM          (1 << 29) /* Free running clock mode */
#define QUADSPI_CCR_SIOO          (1 << 28) /* Send instruction only once mode */
#define QUADSPI_CCR_FMODE_MASK    (0x3 << 26)
#define QUADSPI_CCR_FMODE(fmode)  (((fmode) & 0x3) << 26)   /* Functional Mode (0=indirect write, 1=indirect read, 2=auto poll, 3=mem mapped) */
#define QUADSPI_CCR_DMODE_MASK    (0x3 << 24)
#define QUADSPI_CCR_DMODE(dmode)  (((dmode) & 0x3) << 24)   /* Data mode (0=no data, 1=data on single line, 2=data on two lines, 3=data on four lines) */
#define QUADSPI_CCR_DCYC_MASK     (0x1F << 18)
#define QUADSPI_CCR_DCYC(dcyc)    (((dcyc) & 0x1F) << 18)   /* Number of dummy cycles */
#define QUADSPI_CCR_ABSIZE_MASK   (0x3 << 16)
#define QUADSPI_CCR_ABSIZE(absz)  (((absz) & 0x3) << 16)    /* Alternate bytes size */
#define QUADSPI_CCR_ABMODE_MASK   (0x3 << 14)
#define QUADSPI_CCR_ABMODE(abmode) (((abmode) & 0x3) << 14) /* Alternate bytes mode (0=no alt data, 1=alt on single line, 2=alt on two lines, 3=alt on four lines) */
#define QUADSPI_CCR_ADSIZE_MASK   (0x3 << 12)
#define QUADSPI_CCR_ADSIZE(sz)    (((sz) & 0x3) << 12)      /* Address bytes size (0=8-bit, 1=16-bit, 2=24-bit, 3=32-bit) */
#define QUADSPI_CCR_ADMODE_MASK   (0x3 << 10)
#define QUADSPI_CCR_ADMODE(admode) (((admode) & 0x3) << 10) /* Address mode (0=no addr, 1=addr on single line, 2=addr on two lines, 3=addr on four lines) */
#define QUADSPI_CCR_IMODE_MASK    (0x3 << 8)
#define QUADSPI_CCR_IMODE(imode)  (((imode) & 0x3) << 8)    /* Instruction mode (0=no instr, 1=instr on single line, 2=instr on two lines, 3=instr on four lines) */
#define QUADSPI_CCR_INSTRUCTION_MASK (0xFF << 0)
#define QUADSPI_CCR_INSTRUCTION(ins) (((ins) & 0xFF) << 0)  /* Instruction to be send to the external SPI device */

#define QUADSPI_DCR_FSIZE_MASK    (0x1F << 16)
#define QUADSPI_DCR_FSIZE(fsz)    (((fsz) & 0x1F) << 16)    /* Flash memory size (2 ^ (fsize + 1)). Example 16MB is 23 */
#define QUADSPI_DCR_CSHT_MASK     (0x7 << 8)
#define QUADSPI_DCR_CSHT(csht)    (((csht) & 0x7) <<  8)    /* Chip select high time - Number of clock cycles (+1) nCS to remain high between commands */
#define QUADSPI_DCR_CKMODE_3      (1 << 0)                  /* Clock mode 3 - clk high while nCS released */
#define QUADSPI_DCR_CKMODE_0      (0)                       /* Clock mode 0 - clk low while nCS released */

#define QUADSPI_SR_TCF            (1 << 1)                  /* Transfer complete flag - set in indirect mode when the programmed number of data has been transferred */
#define QUADSPI_SR_FTF            (1 << 2)                  /* FIFO threshold flag */
#define QUADSPI_SR_BUSY           (1 << 5)                  /* Busy - operation is ongoing when set */


/* OCTOSPI (1=0x5000, 2=0xA000)*/
#ifndef OCTOSPI_BASE
#define OCTOSPI_BASE      0x52005000
#endif

#define OCTOSPI_CR                (*(volatile uint32_t *)(OCTOSPI_BASE + 0x00)) /* Control register */
#define OCTOSPI_DCR1              (*(volatile uint32_t *)(OCTOSPI_BASE + 0x08)) /* Device Configuration register 1 */
#define OCTOSPI_DCR2              (*(volatile uint32_t *)(OCTOSPI_BASE + 0x0C)) /* Device Configuration register 2 */
#define OCTOSPI_DCR3              (*(volatile uint32_t *)(OCTOSPI_BASE + 0x10)) /* Device Configuration register 3 */
#define OCTOSPI_DCR4              (*(volatile uint32_t *)(OCTOSPI_BASE + 0x14)) /* Device Configuration register 4 */
#define OCTOSPI_SR                (*(volatile uint32_t *)(OCTOSPI_BASE + 0x20)) /* Status register */
#define OCTOSPI_FCR               (*(volatile uint32_t *)(OCTOSPI_BASE + 0x24)) /* Flag Clear register */
#define OCTOSPI_DLR               (*(volatile uint32_t *)(OCTOSPI_BASE + 0x40)) /* Data Length register */
#define OCTOSPI_AR                (*(volatile uint32_t *)(OCTOSPI_BASE + 0x48)) /* Address register */
#define OCTOSPI_DR                (*(volatile uint8_t  *)(OCTOSPI_BASE + 0x50)) /* Data register */
#define OCTOSPI_DR32              (*(volatile uint32_t *)(OCTOSPI_BASE + 0x50)) /* Data register - 32 bit */
#define OCTOSPI_PSMKR             (*(volatile uint32_t *)(OCTOSPI_BASE + 0x80)) /* Polling Status Mask register */
#define OCTOSPI_PSMAR             (*(volatile uint32_t *)(OCTOSPI_BASE + 0x88)) /* Polling Status Match register */
#define OCTOSPI_PIR               (*(volatile uint32_t *)(OCTOSPI_BASE + 0x90)) /* Polling Interval register */
#define OCTOSPI_CCR               (*(volatile uint32_t *)(OCTOSPI_BASE + 0x100)) /* Communication Configuration register */
#define OCTOSPI_TCR               (*(volatile uint32_t *)(OCTOSPI_BASE + 0x108)) /* Timing Configuration register */
#define OCTOSPI_IR                (*(volatile uint32_t *)(OCTOSPI_BASE + 0x110)) /* Instruction register */
#define OCTOSPI_ABR               (*(volatile uint32_t *)(OCTOSPI_BASE + 0x120)) /* Alternate Bytes register */
#define OCTOSPI_LPTR              (*(volatile uint32_t *)(OCTOSPI_BASE + 0x130)) /* Low Power Timeout register */

#define OCTOSPI_CR_FMODE_MASK     (0x3 << 28)
#define OCTOSPI_CR_FMODE(fmode)   (((fmode) & 0x3) << 28)  /* Functional Mode (0=indirect write, 1=indirect read, 2=auto poll, 3=mem mapped) */
#define OCTOSPI_CR_FTIE           (1 << 18)                /* FIFO threshold interrupt enable */
#define OCTOSPI_CR_TCIE           (1 << 17)                /* Transfer complete interrupt enable */
#define OCTOSPI_CR_FTHRES_MASK    (0x1F << 8)
#define OCTOSPI_CR_FTHRES(thr)    ((((thr)-1) & 0x1F) << 8) /* FIFO threshold level */
#define OCTOSPI_CR_FSEL           (1 << 7)                 /* 0=Flash 1 or 1=Flash 2 */
#define OCTOSPI_CR_ABORT          (1 << 1)                 /* Abort request */
#define OCTOSPI_CR_EN             (1 << 0)                 /* Enable the QUADSPI */

#define OCTOCPI_DCR1_MTYP_MASK    (0x7 << 24)
#define OCTOCPI_DCR1_MTYP(mtyp)   (((mtyp) & 0x7) << 24)   /* Memory type: 0=Micron, 1=Macronix, 2=Std, 3=Macronix RAM, 4=HyperBus mem, 5=HyperBus reg */
#define OCTOSPI_DCR1_DEVSIZE_MASK (0x1F << 16)
#define OCTOSPI_DCR1_DEVSIZE(dsz) (((dsz) & 0x1F) << 16)   /* Device memory size (2 ^ (fsize + 1)). Example 16MB is 23 */
#define OCTOSPI_DCR1_CSHT_MASK    (0x3F << 8)
#define OCTOSPI_DCR1_CSHT(csht)   (((csht) & 0x3F) << 8)   /* Chip select high time - Number of clock cycles (+1) nCS to remain high between commands */
#define OCTOSPI_DCR1_CKMODE_3     (1 << 0)                 /* Clock mode 3 - clk high while nCS released */
#define OCTOSPI_DCR1_CKMODE_0     (0)                      /* Clock mode 0 - clk low while nCS released */

#define OCTOSPI_DCR2_PRESCALER_MASK (0xFF << 0)
#define OCTOSPI_DCR2_PRESCALER(pre) ((((pre)-1) & 0xFF) << 0) /* Clock prescaler: quadspi_ker_ck/pre+1 */

#define OCTOSPI_SR_TCF            (1 << 1)                 /* Transfer complete flag - set in indirect mode when the programmed number of data has been transferred */
#define OCTOSPI_SR_FTF            (1 << 2)                 /* FIFO threshold flag */
#define OCTOSPI_SR_BUSY           (1 << 5)                 /* Busy - operation is ongoing when set */
#define OCTOSPI_SR_FLEVEL(sr)     (((sr) >> 8) & 0x3F)     /* FIFO level */

#define OCTOSPI_CCR_SIOO          (1 << 31)                /* Send instruction only once mode */
#define OCTOSPI_CCR_DDTR          (1 << 27)                /* Data Double transfer rate mode */
#define OCTOSPI_CCR_DMODE_MASK    (0x7 << 24)
#define OCTOSPI_CCR_DMODE(dmode)  (((dmode) & 0x7) << 24)  /* Data mode (0=no data, 1=data on single line, 2=data on two lines, 3=data on four lines, 4=data on eight lines) */
#define OCTOSPI_CCR_ABSIZE_MASK   (0x3 << 20)
#define OCTOSPI_CCR_ABSIZE(absz)  (((absz) & 0x3) << 20)   /* Alternate bytes size */
#define OCTOSPI_CCR_ABDTR         (1 << 19)                /* Alternate bytes Double transfer rate mode */
#define OCTOSPI_CCR_ABMODE_MASK   (0x7 << 16)
#define OCTOSPI_CCR_ABMODE(abm)   (((abm) & 0x7) << 16)    /* Alternate bytes mode (0=no alt data, 1=alt on single line, 2=alt on two lines, 3=alt on four lines, 4=alt on eight lines) */
#define OCTOSPI_CCR_ADSIZE_MASK   (0x3 << 12)
#define OCTOSPI_CCR_ADSIZE(sz)    (((sz) & 0x3) << 12)     /* Address bytes size (0=8-bit, 1=16-bit, 2=24-bit, 3=32-bit) */
#define OCTOSPI_CCR_ADMODE_MASK   (0x7 << 8)
#define OCTOSPI_CCR_ADMODE(adm)   (((adm) & 0x7) << 8)     /* Address mode (0=no addr, 1=addr on single line, 2=addr on two lines, 3=addr on four lines, 4=addr on eight lines) */
#define OCTOSPI_CCR_ISIZE_MASK    (0x3 << 4)
#define OCTOSPI_CCR_ISIZE(sz)     (((sz) & 0x3) << 4)      /* Instruction size (0=8-bit, 1=16-bit, 2=24-bit, 3=32-bit) */
#define OCTOSPI_CCR_IMODE_MASK    (0x7 << 0)
#define OCTOSPI_CCR_IMODE(imode)  (((imode) & 0x7) << 0)   /* Instruction mode (0=no instr, 1=instr on single line, 2=instr on two lines, 3=instr on four lines, 4=instr on eight lines) */

#define OCTOSPI_TCR_SSHIFT        (1 << 30)                /* Sample shift 1=1/2 cycle shift */
#define OCTOSPI_TCR_DHQC          (1 << 28)                /* Delay the data output by 1/4 of clock cycle */
#define OCTOSPI_TCR_DCYC_MASK     (0x1F << 0)
#define OCTOSPI_TCR_DCYC(dcyc)    (((dcyc) & 0x1F) << 0)   /* Number of dummy cycles */


#endif /* !SPI_DRV_STM32_H_INCLUDED */
