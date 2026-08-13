/**
 * spi_test.c
 * A simple test for SPI communication.
 * 
 * written by Alessandro Giusti
 */

#include <stm32f4xx.h>
#include "bsp/clock.h"

#define ADXL345_WHO_AM_I 0x00
#define ADXL345_WHO_AM_I_RESPONSE 0xE5
#define ADXL345_READ 0x80
#define ADXL345_WRITE 0x00
#define ADXL345_CS_LOW() (GPIOA->BSRR = GPIO_BSRR_BR4)
#define ADXL345_CS_HIGH() (GPIOA->BSRR = GPIO_BSRR_BS4)
#define ADXL345_MB_MODE 0x40

int main(void) 
{
	// Initialize the system clock
	clock_init();

	// setup SPI1 pins (PA5, PA6, PA7)
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
	GPIOA->MODER &= ~(GPIO_MODER_MODER5 | GPIO_MODER_MODER6 | GPIO_MODER_MODER7); // clear mode bits
	GPIOA->MODER |= (GPIO_MODER_MODER5_1 | GPIO_MODER_MODER6_1 | GPIO_MODER_MODER7_1); // set to alt function
	
	// set alternate function to AF5 (SPI1)
	GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL5 | GPIO_AFRL_AFSEL6 | GPIO_AFRL_AFSEL7); // clear alt function bits
	GPIOA->AFR[0] |= (0x5 << GPIO_AFRL_AFSEL5_Pos) | (0x5 << GPIO_AFRL_AFSEL6_Pos) | (0x5 << GPIO_AFRL_AFSEL7_Pos); // set to AF5 (SPI1)

	// ADXL345 needs a active low CS (PA4)
	GPIOA->MODER &= ~GPIO_MODER_MODER4; // clear mode bits
	GPIOA->MODER |= GPIO_MODER_MODER4_0; // set to output
	GPIOA->BSRR = GPIO_BSRR_BS4; // set CS high

	// configure SPI1
	RCC->APB2ENR |= RCC_APB2ENR_SPI1EN; // enable SPI1 clock
	SPI1->CR1 = SPI_CR1_CPHA | SPI_CR1_CPOL | SPI_CR1_MSTR | SPI_CR1_BR | SPI_CR1_SSM | SPI_CR1_SSI; // CPHA, CPOL, master, fPCLK/256, software slave management, internal slave select

	// enable SPI1
	SPI1->CR1 |= SPI_CR1_SPE;

	while(1) { // read who_am_i register from ADXL345
		ADXL345_CS_LOW();
		while(!(SPI1->SR & SPI_SR_TXE)); // wait for TXE
		SPI1->DR = ADXL345_READ | ADXL345_WHO_AM_I; // send read command
		while(!(SPI1->SR & SPI_SR_RXNE)); // wait for RXNE
		volatile uint8_t garbage = SPI1->DR; // read response
		while(!(SPI1->SR & SPI_SR_TXE)); // wait for TXE
		SPI1->DR = 0x00; // send dummy byte to receive data
		while(!(SPI1->SR & SPI_SR_RXNE)); // wait for RXNE
		volatile uint8_t response = SPI1->DR; // read response

		while(SPI1->SR & SPI_SR_BSY); // wait for BSY to clear
		ADXL345_CS_HIGH();

		if(response == ADXL345_WHO_AM_I_RESPONSE) {
			// successful communication with ADXL345
			for(volatile uint32_t i = 0; i < 1000000; i++); // delay
		} else {
			// failed communication, handle error
			for(volatile uint32_t i = 0; i < 9000000; i++); // delay
		}
	}
}