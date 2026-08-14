// main.c

#include <stm32f4xx.h>
#include "bsp/clock.h"
#include "bsp/board.h"
#include "drivers/spi.h"
#include "drivers/ADXL345.h"

static volatile uint8_t devid;

int main(void)
{
	// Initialize the system clock
	clock_init();

	// Initialize SPI1
	spi1_init();

	// Initialize ADXL345
	adxl345_init();

	devid = adxl345_read_register(ADXL345_WHO_AM_I);
	
	while (1);
}

