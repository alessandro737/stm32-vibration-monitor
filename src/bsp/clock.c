//clock.c
#include <stm32f4xx.h>
#include "bsp/clock.h"


void clock_init(void)
{
	// enable hsi
	RCC->CR |= RCC_CR_HSION;
	while (!(RCC->CR & RCC_CR_HSIRDY)); // wait for hsi ready

	// enable APB1 peripheral clock and adjust PWR voltage regulator
	RCC->APB1ENR |= RCC_APB1ENR_PWREN;
	PWR->CR = (PWR->CR & ~PWR_CR_VOS) | (0x3UL << PWR_CR_VOS_Pos); // set voltage scaling to scale 1
	
	// RCC configuration apb and ahb prescalers
	RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2); // clear bits
	RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1; // set bits

	// configure pll
	RCC->PLLCFGR &= ~(RCC_PLLCFGR_PLLM | RCC_PLLCFGR_PLLN | RCC_PLLCFGR_PLLP | RCC_PLLCFGR_PLLSRC); // clear bits

	RCC->PLLCFGR |= (8 << RCC_PLLCFGR_PLLM_Pos) |
	 				(100 << RCC_PLLCFGR_PLLN_Pos) |
	  				(0 << RCC_PLLCFGR_PLLP_Pos) |
					RCC_PLLCFGR_PLLSRC_HSI; // set bits
				
	// turn on pll
	RCC->CR |= RCC_CR_PLLON;
	while (!(RCC->CR & RCC_CR_PLLRDY)); // wait for pll ready
	while ((PWR->CSR & PWR_CSR_VOSRDY) == 0); // wait for voltage scaling ready

	   	
	// set flash wait states (100 MHz, 3 wait states) && enable prefetch, instruction cache and data cache
	FLASH->ACR = FLASH_ACR_LATENCY_3WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
	while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_3WS); // wait for flash ready


	// select pll as system clock
	RCC->CFGR &= ~RCC_CFGR_SW; // clear bits
	RCC->CFGR |= RCC_CFGR_SW_PLL; // set bits

	// wait for pll to be selected as system clock
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

	// update SystemCoreClock variable
	SystemCoreClockUpdate();

}