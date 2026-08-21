// main.c  

#include "FreeRTOS.h"
#include "task.h"

#include <stm32f4xx.h>
#include "bsp/clock.h"
#include "bsp/board.h"

#include "drivers/spi.h"
#include "drivers/ADXL345.h"

#define PROBE_PIN 8

static StaticTask_t xAcquisitionTCB;
static StackType_t xAcquisitionStack[128];

static void acquisition_task (void *pvParameters);
void probe_init(void);

int main(void)
{   
    // inits
    clock_init();
    // led_init();
    spi1_init();
    if (!adxl345_init()) {
        __BKPT(0);      // halts with the failure visible
        while (1);
    } 
    probe_init();   

    TaskHandle_t acq_handle = xTaskCreateStatic(
        acquisition_task,
        "ADXL_XYZ",
        128,
        NULL,
        1,
        xAcquisitionStack,
        &xAcquisitionTCB
    );
    configASSERT(acq_handle != NULL);

    vTaskStartScheduler();

    for (;;) {}
}

static void acquisition_task(void *pvParameters)
{
    (void)pvParameters;

    ADXL345_Accel_t sample;              // storage, not a pointer
    TickType_t xLastWake = xTaskGetTickCount();
    static volatile uint32_t overruns = 0;

    for (;;) {
        if (xTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(10)) == pdFALSE) {
            overruns++;
        }

        // probe high
        GPIOA->BSRR = (0x1UL << (PROBE_PIN));
        adxl345_read_acceleration(&sample);
        // probe low
        GPIOA->BSRR = (0x1UL << (PROBE_PIN + 16));
    }
}

void probe_init(void)
{
        // enable gpio port a and b
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // enable pins to be output
    // clear and set
    GPIOA->MODER &= ~GPIO_MODER_MODER8;

    // set pins low before output config
    GPIOA->BSRR = (0x1UL << (PROBE_PIN + 16));
    
    GPIOA->MODER |= (0x1UL << (PROBE_PIN * 2));
}
