/*
 * main.c
 *
 *  Created on: 2024-06-15
 * 	Blink program for STM32F4 Discovery board
 *
 * */

#include <stm32f4xx.h>
#include "bsp/clock.h"


int main(void)
{
	// Initialize the system clock
	clock_init();
	
	// Enable GPIOA clock
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

	// Set PA5 as output
	GPIOA->MODER &= ~GPIO_MODER_MODER5;      // clear both bits
	GPIOA->MODER |=  GPIO_MODER_MODER5_0;

	// Infinite loop
	while (1)
	{
		// Set PA5
		GPIOA->BSRR = GPIO_BSRR_BS5;

		// Delay
		for (volatile uint32_t i = 0; i < 1000000; i++);

		// Reset PA5
		GPIOA->BSRR = GPIO_BSRR_BR5;

		// Delay
		for (volatile uint32_t i = 0; i < 1000000; i++);
		
	}
}