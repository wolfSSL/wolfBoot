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
/** SPI settings **/

#define SPI1 (0x40013000)/* SPI1 base address */
#define SPI1_APB2_CLOCK_ER_VAL 	(1 << 12)

#define CEN_GPIOA (1 << 0)
#define CEN_GPIOB (1 << 1)
#define CEN_GPIOC (1 << 2)
#define CEN_GPIOD (1 << 3)
#define CEN_GPIOE (1 << 4)

#ifdef PLATFORM_stm32f4
#define APB2_CLOCK_ER     (*(volatile uint32_t *)(0x40023844))
#define APB2_CLOCK_RST    (*(volatile uint32_t *)(0x40023824))
#define RCC_GPIO_CLOCK_ER (*(volatile uint32_t *)(0x40023830))
#define GPIOA_BASE (0x40020000)
#define GPIOB_BASE (0x40020400)
#define GPIOC_BASE (0x40020800)
#define GPIOD_BASE (0x40020C00)
#define GPIOE_BASE (0x40021000)
#define SPI_GPIO    GPIOB_BASE
#define SPI_CS_GPIO GPIOE_BASE
#define SPI_CS_FLASH 1 /* Flash CS connected to GPIOE1 */
#define SPI_CS_TPM   0 /* TPM CS connected to GPIOE0 */
#define SPI1_PIN_AF    5 /* Alternate function for SPI pins */
#define SPI1_CLOCK_PIN 3 /* SPI_SCK: PB3  */
#define SPI1_MISO_PIN  4 /* SPI_MISO PB4  */
#define SPI1_MOSI_PIN  5 /* SPI_MOSI PB5  */
#endif

#ifdef PLATFORM_stm32wb
#define APB2_CLOCK_ER     (*(volatile uint32_t *)(0x58000060))
#define APB2_CLOCK_RST    (*(volatile uint32_t *)(0x58000040))
#define RCC_GPIO_CLOCK_ER (*(volatile uint32_t *)(0x5800004C))
#define GPIOA_BASE (0x48000000)
#define GPIOB_BASE (0x48000400)
#define GPIOC_BASE (0x48000800)
#define GPIOD_BASE (0x48000C00)

/* STM32WB55 NUCLEO: CN9: D13=SCK, D12=MISO, D11=MOSI, FLASHCS=D10, TPMCS=D9 */
#define SPI_GPIO    GPIOA_BASE
#define SPI_CS_GPIO GPIOA_BASE
#define SPI_CS_FLASH 4 /* Flash CS connected to GPIOA4 */
#define SPI_CS_TPM   9 /* TPM CS connected to GPIOA9 */
#define SPI1_PIN_AF    5 /* Alternate function for SPI pins */
#define SPI1_CLOCK_PIN 5 /* SPI_SCK: PA5  */
#define SPI1_MISO_PIN  6 /* SPI_MISO PA6  */
#define SPI1_MOSI_PIN  7 /* SPI_MOSI PA7  */
#endif

#ifdef PLATFORM_stm32l0
#define APB2_CLOCK_ER     (*(volatile uint32_t *)(0x40021034))
#define APB2_CLOCK_RST    (*(volatile uint32_t *)(0x40021024))
#define RCC_GPIO_CLOCK_ER (*(volatile uint32_t *)(0x4002102C))
#define GPIOB_BASE (0x50000400)
#define SPI_GPIO    GPIOB_BASE
#define SPI_CS_GPIO GPIOB_BASE
#define SPI_CS_FLASH 8 /* Flash CS connected to GPIOB8 */
#define SPI1_PIN_AF    0 /* Alternate function for SPI pins */
#define SPI1_CLOCK_PIN 3 /* SPI_SCK: PB3  */
#define SPI1_MISO_PIN  4 /* SPI_MISO PB4  */
#define SPI1_MOSI_PIN  5 /* SPI_MOSI PB5  */
#endif

#define SPI_PIO_BASE    SPI_GPIO
#define SPI_CS_PIO_BASE SPI_CS_GPIO

#if (SPI_GPIO == GPIOA_BASE)
#   define SPI_PIO_CEN CEN_GPIOA
#elif (SPI_GPIO == GPIOB_BASE)
#   define SPI_PIO_CEN CEN_GPIOB
#elif (SPI_GPIO == GPIOC_BASE)
#   define SPI_PIO_CEN CEN_GPIOC
#elif (SPI_GPIO == GPIOD_BASE)
#   define SPI_PIO_CEN CEN_GPIOD
#elif (SPI_GPIO == GPIOE_BASE)
#   define SPI_PIO_CEN CEN_GPIOE
#endif

#if (SPI_CS_GPIO == GPIOA_BASE)
#   define SPI_PIO_CS_CEN CEN_GPIOA
#elif (SPI_CS_GPIO == GPIOB_BASE)
#   define SPI_PIO_CS_CEN CEN_GPIOB
#elif (SPI_CS_GPIO == GPIOC_BASE)
#   define SPI_PIO_CS_CEN CEN_GPIOC
#elif (SPI_CS_GPIO == GPIOD_BASE)
#   define SPI_PIO_CS_CEN CEN_GPIOD
#elif (SPI_CS_GPIO == GPIOE_BASE)
#   define SPI_PIO_CS_CEN CEN_GPIOE
#endif


#define SPI1_CR1      (*(volatile uint32_t *)(SPI1))
#define SPI1_CR2      (*(volatile uint32_t *)(SPI1 + 0x04))
#define SPI1_SR       (*(volatile uint32_t *)(SPI1 + 0x08))
#define SPI1_DR       (*(volatile uint32_t *)(SPI1 + 0x0c))

#define SPI_CR1_CLOCK_PHASE         (1 << 0)
#define SPI_CR1_CLOCK_POLARITY      (1 << 1)
#define SPI_CR1_MASTER	    		(1 << 2)
#define SPI_CR1_BAUDRATE        	(0x07 << 3)
#define SPI_CR1_SPI_EN		    	(1 << 6)
#define SPI_CR1_LSBFIRST		    (1 << 7)
#define SPI_CR1_SSI			(1 << 8)
#define SPI_CR1_SSM			(1 << 9)
#define SPI_CR1_16BIT_FORMAT        (1 << 11)
#define SPI_CR1_TX_CRC_NEXT			(1 << 12)
#define SPI_CR1_HW_CRC_EN			(1 << 13)
#define SPI_CR1_BIDIOE			    (1 << 14)
#define SPI_CR2_SSOE			    (1 << 2)


#define SPI_SR_RX_NOTEMPTY  	        (1 << 0)
#define SPI_SR_TX_EMPTY			        (1 << 1)
#define SPI_SR_BUSY			            (1 << 7)






#define SPI_PIO_MODE  (*(volatile uint32_t *)(SPI_PIO_BASE + 0x00))
#define SPI_PIO_AFL   (*(volatile uint32_t *)(SPI_PIO_BASE + 0x20))
#define SPI_PIO_AFH   (*(volatile uint32_t *)(SPI_PIO_BASE + 0x24))
#define SPI_PIO_OSPD  (*(volatile uint32_t *)(SPI_PIO_BASE + 0x08))
#define SPI_PIO_PUPD (*(volatile uint32_t *)(SPI_PIO_BASE + 0x0c))
#define SPI_PIO_BSRR (*(volatile uint32_t *)(SPI_PIO_BASE + 0x18))
#define SPI_PIO_CS_MODE  (*(volatile uint32_t *)(SPI_CS_PIO_BASE + 0x00))
#define SPI_PIO_CS_AFL   (*(volatile uint32_t *)(SPI_CS_PIO_BASE + 0x20))
#define SPI_PIO_CS_AFH   (*(volatile uint32_t *)(SPI_CS_PIO_BASE + 0x24))
#define SPI_PIO_CS_OSPD  (*(volatile uint32_t *)(SPI_CS_PIO_BASE + 0x08))
#define SPI_PIO_CS_PUPD (*(volatile uint32_t *)(SPI_CS_PIO_BASE + 0x0c))
#define SPI_PIO_CS_BSRR (*(volatile uint32_t *)(SPI_CS_PIO_BASE + 0x18))
#define SPI_PIO_CS_ODR  (*(volatile uint32_t *)(SPI_CS_PIO_BASE + 0x14))
#define GPIO_MODE_AF (2)


#endif
