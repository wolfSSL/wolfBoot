#ifndef SPI_DRV_STM32_H_INCLUDED
#define SPI_DRV_STM32_H_INCLUDED
#include <stdint.h>
/** SPI settings **/

#define SPI1 (0x40013000)/* SPI1 base address */
#define SPI_FLASH_PIN  1 /* Flash CS connected to GPIOE1 */
#define SPI1_PIN_AF    5 /* Alternate function for SPI pins */
#define SPI1_CLOCK_PIN 3 /* SPI_SCK: PB3  */
#define SPI1_MISO_PIN  4 /* SPI_MISO PB4  */
#define SPI1_MOSI_PIN  5 /* SPI_MOSI PB5  */

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

#define APB2_CLOCK_ER           (*(volatile uint32_t *)(0x40023844))
#define APB2_CLOCK_RST          (*(volatile uint32_t *)(0x40023824))
#define SPI1_APB2_CLOCK_ER_VAL 	(1 << 12)


#define CLOCK_SPEED (168000000) 


#define AHB1_CLOCK_ER (*(volatile uint32_t *)(0x40023830))
#define GPIOB_AHB1_CLOCK_ER (1 << 1)
#define GPIOE_AHB1_CLOCK_ER (1 << 4)
#define GPIOB_BASE 0x40020400
#define GPIOE_BASE 0x40021000

#define GPIOB_MODE  (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_AFL   (*(volatile uint32_t *)(GPIOB_BASE + 0x20))
#define GPIOB_AFH   (*(volatile uint32_t *)(GPIOB_BASE + 0x24))
#define GPIOB_OSPD  (*(volatile uint32_t *)(GPIOB_BASE + 0x08))
#define GPIOB_PUPD (*(volatile uint32_t *)(GPIOB_BASE + 0x0c))
#define GPIOB_BSRR (*(volatile uint32_t *)(GPIOB_BASE + 0x18))
#define GPIOE_MODE  (*(volatile uint32_t *)(GPIOE_BASE + 0x00))
#define GPIOE_AFL   (*(volatile uint32_t *)(GPIOE_BASE + 0x20))
#define GPIOE_AFH   (*(volatile uint32_t *)(GPIOE_BASE + 0x24))
#define GPIOE_OSPD  (*(volatile uint32_t *)(GPIOE_BASE + 0x08))
#define GPIOE_PUPD (*(volatile uint32_t *)(GPIOE_BASE + 0x0c))
#define GPIOE_BSRR (*(volatile uint32_t *)(GPIOE_BASE + 0x18))
#define GPIOE_ODR  (*(volatile uint32_t *)(GPIOE_BASE + 0x14))
#define GPIO_MODE_AF (2)


#endif
