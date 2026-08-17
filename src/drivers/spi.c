// spi.c
#include "spi.h"
#include <stm32f4xx.h>

void spi1_init(void)
{
    // GPIO Configuration for SPI1 pins (PA5, PA6, PA7) (SCK, MISO, MOSI)

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    GPIOA->MODER &= ~(
        GPIO_MODER_MODER5 |
        GPIO_MODER_MODER6 |
        GPIO_MODER_MODER7
    );

    GPIOA->MODER |=
        GPIO_MODER_MODER5_1 |
        GPIO_MODER_MODER6_1 |
        GPIO_MODER_MODER7_1;

    // AF5 = SPI1
    GPIOA->AFR[0] &= ~(
        GPIO_AFRL_AFSEL5 |
        GPIO_AFRL_AFSEL6 |
        GPIO_AFRL_AFSEL7
    );

    GPIOA->AFR[0] |=
        (5U << GPIO_AFRL_AFSEL5_Pos) |
        (5U << GPIO_AFRL_AFSEL6_Pos) |
        (5U << GPIO_AFRL_AFSEL7_Pos);

    /*
     * SCK slew rate. At reset (00) the clock edge arrives at the sensor late enough
     * that its MISO response misses our sampling edge; see engineering log 2026-08-17
     */
    GPIOA->OSPEEDR &= ~(
        GPIO_OSPEEDR_OSPEED5 |
        GPIO_OSPEEDR_OSPEED7
    );

    GPIOA->OSPEEDR |=
        GPIO_OSPEEDR_OSPEED5_1 | GPIO_OSPEEDR_OSPEED5_0 |  // Very High Speed
        GPIO_OSPEEDR_OSPEED7_1 | GPIO_OSPEEDR_OSPEED7_0;  // Very High Speed

    // SPI1 Peripheral Clock Enable and Configuration
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    SPI1->CR1 = 0;

    /*
     * SPI mode 3:
     *
     * CPOL = 1
     * CPHA = 1
     *
     * Master
     * MSB first
     * Software NSS
     *
     * BR = 111 -> PCLK / 256
     *
     * Deliberately slow for initial debugging.
     */
    SPI1->CR1 =
        SPI_CR1_CPOL |
        SPI_CR1_CPHA |
        SPI_CR1_MSTR |
        SPI_CR1_SSM  |
        SPI_CR1_SSI  |
        (7U << SPI_CR1_BR_Pos);

    SPI1->CR1 &= ~SPI_CR1_DFF; // 8-bit data frame format

    // clear any stale data in the RX FIFO and clear OVR flag if set
    (void)SPI1->DR;
    (void)SPI1->SR;

    // Enable SPI1
    SPI1->CR1 |= SPI_CR1_SPE;
}

void spi1_wait_idle(void)
{
    while (SPI1->SR & SPI_SR_BSY)
        ;
}


void spi1_flush_rx(void)
{
    
    while (SPI1->SR & SPI_SR_RXNE) // while RXNE is set, read data to clear it
    {
        (void)*((volatile uint8_t *)&SPI1->DR);
    }

    (void)SPI1->SR; // read SR to clear OVR flag if set
}


uint8_t spi1_transfer(uint8_t data)
{
    while (!(SPI1->SR & SPI_SR_TXE)) // wait until TXE is set
        ;

    *((volatile uint8_t *)&SPI1->DR) = data; // write data to DR (8-bit access)

    spi1_wait_idle(); // wait until BSY is cleared

    while (!(SPI1->SR & SPI_SR_RXNE)) // wait until RXNE is set
        ;

    return (uint8_t)(SPI1->DR & 0xFFU);          /* word read, mask */
}