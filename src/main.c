// main.c  

#include "FreeRTOS.h"
#include "task.h"

#include <stm32f4xx.h>
#include "bsp/clock.h"
#include "bsp/board.h"

// #include "drivers/spi.h"
// #include "drivers/ADXL345.h"

void led_init(void);

static StaticTask_t xBlink1TCB;
static StackType_t  xBlink1Stack[ 128 ];

static Statictask_t xBlink2TCB;
static StackType_t xBlink2Stack[ 128 ];

int main(void)
{
    clock_init();
    led_init();

    // xTaskCreateStatic(...) x2, each with its own static TCB + stack

    vTaskStartScheduler();

    for (;;) {}
}

void led_init(void)
{
    
}