// spi.c
#include "spi.h"
#include <stm32f4xx.h>

void spi1_init(void)
{
    /*
     * ---------------------------------------------------------
     * GPIO
     * ---------------------------------------------------------
     */

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    /*
     * PA5 = SPI1_SCK
     * PA6 = SPI1_MISO
     * PA7 = SPI1_MOSI
     */

    GPIOA->MODER &= ~(
        GPIO_MODER_MODER5 |
        GPIO_MODER_MODER6 |
        GPIO_MODER_MODER7
    );

    GPIOA->MODER |=
        GPIO_MODER_MODER5_1 |
        GPIO_MODER_MODER6_1 |
        GPIO_MODER_MODER7_1;

    /*
     * AF5 = SPI1
     */

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
     * Configure GPIO speed to Very High Speed for SPI pins (PA5, PA6, PA7).
     * This prevents the LSB/bit 0 from lagging behind the internal peripheral clock.
     */
    GPIOA->OSPEEDR &= ~(
        GPIO_OSPEEDR_OSPEED5 |
        GPIO_OSPEEDR_OSPEED6 |
        GPIO_OSPEEDR_OSPEED7
    );

    GPIOA->OSPEEDR |=
        GPIO_OSPEEDR_OSPEED5_1 | GPIO_OSPEEDR_OSPEED5_0;  // Very High Speed
        // GPIO_OSPEEDR_OSPEED7_1 | GPIO_OSPEEDR_OSPEED7_0;  // Very High Speed



    /*
     * ---------------------------------------------------------
     * SPI1 peripheral
     * ---------------------------------------------------------
     */

    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    /*
     * Disable SPI before configuring CR1.
     */
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

    /*
     * 8-bit data size is the reset/default configuration.
     *
     * Explicitly clear DFF to guarantee 8-bit transfers.
     */
    SPI1->CR1 &= ~SPI_CR1_DFF;

    /*
     * Clear any stale receive data / overrun state.
     */
    (void)SPI1->DR;
    (void)SPI1->SR;

    /*
     * Enable SPI.
     */
    SPI1->CR1 |= SPI_CR1_SPE;
}

void spi1_wait_idle(void)
{
    while (SPI1->SR & SPI_SR_BSY)
        ;
}


void spi1_flush_rx(void)
{
    /*
     * If RXNE is set, consume the stale byte.
     */
    while (SPI1->SR & SPI_SR_RXNE)
    {
        (void)*((volatile uint8_t *)&SPI1->DR);
    }

    /*
     * Read SR after DR to clear OVR if necessary.
     */
    (void)SPI1->SR;
}


uint8_t spi1_transfer(uint8_t data)
{
    /*
     * Wait until transmit buffer is empty.
     */
    while (!(SPI1->SR & SPI_SR_TXE))
        ;

    /*
     * Explicit 8-bit access to DR.
     */
    *((volatile uint8_t *)&SPI1->DR) = data;

    spi1_wait_idle();

    /*
     * Wait until received byte is available.
     */
    while (!(SPI1->SR & SPI_SR_RXNE))
        ;

    /*
     * Explicit 8-bit read from DR.
     */
    return (uint8_t)(SPI1->DR & 0xFFU);
}