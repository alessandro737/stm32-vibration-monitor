#pragma once

#include <stdint.h>

#define ADXL345_REG_DEVID        0x00
#define ADXL345_REG_BW_RATE      0x2C
#define ADXL345_REG_POWER_CTL    0x2D
#define ADXL345_REG_DATA_FORMAT  0x31

#define ADXL345_REG_DATAX0       0x32

#define ADXL345_DEVICE_ID        0xE5

#define ADXL345_CMD_READ         0x80
#define ADXL345_CMD_WRITE        0x00
#define ADXL345_CMD_MULTI        0x40


typedef struct
{
    int16_t x;
    int16_t y;
    int16_t z;

} ADXL345_Accel;


void adxl345_init(void);

uint8_t adxl345_read_register(uint8_t reg);

void adxl345_write_register(
    uint8_t reg,
    uint8_t value
);

void adxl345_read_multiple_registers(
    uint8_t reg,
    uint8_t *buffer,
    uint8_t length
);

void adxl345_read_acceleration(
    ADXL345_Accel *accel
);