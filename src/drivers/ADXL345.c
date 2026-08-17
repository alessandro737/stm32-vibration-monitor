#include "ADXL345.h"

#include <stm32f4xx.h>

#include "spi.h"
#include "bsp/board.h"


static inline void adxl345_cs_low(void)
{
    ADXL345_CS_PORT->BSRR =
        (1U << (ADXL345_CS_PIN + 16U));
}


static inline void adxl345_cs_high(void)
{
    ADXL345_CS_PORT->BSRR =
        (1U << ADXL345_CS_PIN);
}


void adxl345_init(void)
{
    /*
     * CS inactive.
     */
    adxl345_cs_high();

    /*
     * Configure CS as GPIO output.
     */

    ADXL345_CS_PORT->MODER &=
        ~(3U << (ADXL345_CS_PIN * 2U));

    ADXL345_CS_PORT->MODER |=
        (1U << (ADXL345_CS_PIN * 2U));

    /*
     * Give the device a little time after power-up.
     */
    for (volatile uint32_t i = 0; i < 100000; i++)
        ;
}


uint8_t adxl345_read_register(uint8_t reg)
{
    uint8_t v, tail;
    adxl345_cs_low();
    spi1_transfer(ADXL345_CMD_READ | (reg & 0x3F));
    v    = spi1_transfer(0x00);   /* bits 7:1 valid, bit 0 stale */
    tail = spi1_transfer(0x00);   /* bit 0 == v's true bit 0     */
    spi1_wait_idle();
    adxl345_cs_high();
    return (uint8_t)((v & 0xFE) | (tail & 0x01));
}


void adxl345_write_register(
    uint8_t reg,
    uint8_t value
)
{
    adxl345_cs_low();

    spi1_transfer(
        ADXL345_CMD_WRITE |
        (reg & 0x3F)
    );

    spi1_transfer(value);

    spi1_wait_idle();

    adxl345_cs_high();
}


void adxl345_read_multiple_registers(
    uint8_t reg,
    uint8_t *buffer,
    uint8_t length
)
{
    adxl345_cs_low();

    spi1_transfer(
        ADXL345_CMD_READ |
        ADXL345_CMD_MULTI |
        (reg & 0x3F)
    );

    for (uint8_t i = 0; i < length; i++)
    {
        buffer[i] = spi1_transfer(0x00);
    }

    spi1_wait_idle();

    adxl345_cs_high();
}


void adxl345_read_acceleration(
    ADXL345_Accel *accel
)
{
    uint8_t buffer[6];

    adxl345_read_multiple_registers(
        ADXL345_REG_DATAX0,
        buffer,
        6
    );

    accel->x =
        (int16_t)(
            ((uint16_t)buffer[1] << 8) |
            buffer[0]
        );

    accel->y =
        (int16_t)(
            ((uint16_t)buffer[3] << 8) |
            buffer[2]
        );

    accel->z =
        (int16_t)(
            ((uint16_t)buffer[5] << 8) |
            buffer[4]
        );
}