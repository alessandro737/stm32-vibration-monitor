// spi.h
#pragma once

#include <stdint.h>

void spi1_init(void);

uint8_t spi1_transfer(uint8_t data);

void spi1_wait_idle(void);

void spi1_flush_rx(void);

