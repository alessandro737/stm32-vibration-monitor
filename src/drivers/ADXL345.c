// ADXL345.c
#include "ADXL345.h"
#include <stm32f4xx.h>
#include "spi.h"
#include "bsp/board.h"

void adxl345_init(void) 
{
	// setup ADXL345 CS pin (PA4)
	ADXL345_CS_PORT->MODER &= ~(0x03 << (ADXL345_CS_PIN * 2)); // clear mode bits
	ADXL345_CS_PORT->MODER |= (0x01 << (ADXL345_CS_PIN * 2)); // set to output
	ADXL345_CS_PORT->BSRR = (0x01 << ADXL345_CS_PIN); // set CS high

	//verify devid
	// uint8_t devid = adxl345_read_register(ADXL345_WHO_AM_I);
	// if (devid != ADXL345_WHO_AM_I_RESPONSE) {
	// 	// handle error, device not found
	// 	while(1); // loop here for now, could add error handling later
	// }

	//TODO: Add any additional initialization code for the ADXL345 if needed
	
}

uint8_t adxl345_read_register(uint8_t reg) 
{
	ADXL345_CS_PORT->BSRR = (0x01 << (ADXL345_CS_PIN + 16)); // set CS low
	spi1_transfer(ADXL345_READ | reg); // send read command
	uint8_t value = spi1_transfer(0x00); // read value

	spi1_wait_idle(); // wait for SPI to finish
	ADXL345_CS_PORT->BSRR = (0x01 << ADXL345_CS_PIN); // set CS high
	return value;
}

void adxl345_write_register(uint8_t reg, uint8_t value) 
{
	ADXL345_CS_PORT->BSRR = (0x01 << (ADXL345_CS_PIN + 16)); // set CS low
	spi1_transfer(ADXL345_WRITE | reg); // send write command
	spi1_transfer(value); // send value

	spi1_wait_idle(); // wait for SPI to finish
	ADXL345_CS_PORT->BSRR = (0x01 << ADXL345_CS_PIN); // set CS high
}

void adxl345_read_multiple_registers(uint8_t reg, uint8_t *buf, uint8_t len) 
{
	ADXL345_CS_PORT->BSRR = (0x01 << (ADXL345_CS_PIN + 16)); // set CS low
	spi1_transfer(ADXL345_READ | ADXL345_MULTI_BYTE | reg); // send read command

	for (uint8_t i = 0; i < len; i++) {
		buf[i] = spi1_transfer(0x00); // read value
	}

	spi1_wait_idle(); // wait for SPI to finish
	ADXL345_CS_PORT->BSRR = (0x01 << ADXL345_CS_PIN); // set CS high
}

