// spi.c
#include "spi.h"
#include <stm32f4xx.h>


void spi1_init(void) {
    // setup SPI1 pins (PA5, PA6, PA7)
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
	GPIOA->MODER &= ~(GPIO_MODER_MODER5 | GPIO_MODER_MODER6 | GPIO_MODER_MODER7); // clear mode bits
	GPIOA->MODER |= (GPIO_MODER_MODER5_1 | GPIO_MODER_MODER6_1 | GPIO_MODER_MODER7_1); // set to alt function
	
	// set alternate function to AF5 (SPI1)
	GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL5 | GPIO_AFRL_AFSEL6 | GPIO_AFRL_AFSEL7); // clear alt function bits
	GPIOA->AFR[0] |= (0x5 << GPIO_AFRL_AFSEL5_Pos) | (0x5 << GPIO_AFRL_AFSEL6_Pos) | (0x5 << GPIO_AFRL_AFSEL7_Pos); // set to AF5 (SPI1)

	// configure SPI1
	RCC->APB2ENR |= RCC_APB2ENR_SPI1EN; // enable SPI1 clock
	SPI1->CR1 = SPI_CR1_CPHA | SPI_CR1_CPOL | SPI_CR1_MSTR | SPI_CR1_BR | SPI_CR1_SSM | SPI_CR1_SSI; // CPHA, CPOL, master, fPCLK/256, software slave management, internal slave select

	// enable SPI1
	SPI1->CR1 |= SPI_CR1_SPE;
}

uint8_t spi1_transfer(uint8_t data) {
	while(!(SPI1->SR & SPI_SR_TXE)); // wait for TXE
	SPI1->DR = data; // send data
	while(!(SPI1->SR & SPI_SR_RXNE)); // wait for RXNE
	return SPI1->DR; // read response
}

void spi1_wait_idle(void) {
	while(SPI1->SR & SPI_SR_BSY); // wait for BSY to clear
}

