// ADXL345.h
#pragma once

#include <stdint.h>


/** ADXL345 registers */
#define ADXL345_WHO_AM_I 0x00
#define ADXL345_BW_RATE 0x2C

/** ADXL345 commands */
#define ADXL345_READ 0x80
#define ADXL345_WRITE 0x00
#define ADXL345_MULTI_BYTE 0x40

/** ADXL345 responses */
#define ADXL345_WHO_AM_I_RESPONSE 0xE5


void adxl345_init(void);
uint8_t adxl345_read_register(uint8_t reg);
void adxl345_write_register(uint8_t reg, uint8_t value);
void adxl345_read_multiple_registers(uint8_t reg, uint8_t *buf, uint8_t len);

