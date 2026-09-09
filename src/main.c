// main.c  

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <stm32f4xx.h>

#include "bsp/clock.h"
#include "bsp/board.h"

#include "drivers/spi.h"
#include "drivers/ADXL345.h"

#define PROBE_PIN 8
#define FKL_PROBE_PIN 10
#define HOG_PROBE_PIN 4
#define USE_MUTEX 0   // 0 = binary semaphore (no inheritance), 1 = mutex
#define HOLD_ITERATIONS 3300000

static StaticTask_t xAcquisitionTCB;
static StackType_t xAcquisitionStack[128];

static StaticTask_t xFakeLoggerTCB;
static StackType_t xFKLoggerStack[256];

static SemaphoreHandle_t xSpiLock;
static StaticSemaphore_t xSpiLockBuffer;

static StaticTask_t xHoggerTCB;
static StackType_t xHoggerStack[128];

static void fklogger_task (void *pvParameters);
void fklogger_probe_init (void);

static void acquisition_task (void *pvParameters);
void probe_init(void);

static void hogger_task (void *pvParameters);
void hogger_probe_init (void);

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
    fklogger_probe_init();
    hogger_probe_init();

#if USE_MUTEX
    xSpiLock = xSemaphoreCreateMutexStatic(&xSpiLockBuffer);
#else
    xSpiLock = xSemaphoreCreateBinaryStatic(&xSpiLockBuffer);
    xSemaphoreGive(xSpiLock); // give the binary semaphore so it can be taken
#endif
    configASSERT(xSpiLock != NULL);

    TaskHandle_t acq_handle = xTaskCreateStatic(
        acquisition_task,
        "Acq_task",
        128,
        NULL,
        3,
        xAcquisitionStack,
        &xAcquisitionTCB
    );
    configASSERT(acq_handle != NULL);

    TaskHandle_t log_handle = xTaskCreateStatic(
        fklogger_task,
        "FKLogger_task",
        256,
        NULL,
        1,
        xFKLoggerStack,
        &xFakeLoggerTCB
    );
    configASSERT(log_handle != NULL);

    TaskHandle_t hog_handle = xTaskCreateStatic(
        hogger_task,
        "Hogger_task",
        128,
        NULL,
        2,
        xHoggerStack,
        &xHoggerTCB
    );
    configASSERT(hog_handle != NULL);

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
        // critical section
        xSemaphoreTake(xSpiLock, portMAX_DELAY);
        adxl345_read_acceleration(&sample);
        xSemaphoreGive(xSpiLock);
        // probe low
        GPIOA->BSRR = (0x1UL << (PROBE_PIN + 16));
    }
}

void probe_init(void)
{
    // enable gpio port A
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR; // read back to ensure clock is enabled

    // enable pins to be output
    // clear and set
    GPIOA->MODER &= ~(0x03UL << (PROBE_PIN * 2));

    // set pins low before output config
    GPIOA->BSRR = (0x1UL << (PROBE_PIN + 16));
    
    GPIOA->MODER |= (0x1UL << (PROBE_PIN * 2));
}

static void fklogger_task (void *pvParameters)
{
    (void)pvParameters;

    volatile uint8_t id;

    for (;;) {
        
        // probe high
        GPIOB->BSRR = (0x1UL << (FKL_PROBE_PIN));

        xSemaphoreTake(xSpiLock, portMAX_DELAY);
        // spi transac
        id = adxl345_read_register(ADXL345_REG_DEVID);
        (void)id; // silence unused variable warning

        static volatile uint32_t sink;
        for (uint32_t i = 0; i < HOLD_ITERATIONS; i++) {
            sink = i;
            (void)sink; // silence unused variable warning
        }
        xSemaphoreGive(xSpiLock);

        // probe low
        GPIOB->BSRR = (0x1UL << (FKL_PROBE_PIN + 16));

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void fklogger_probe_init (void)
{
    // enable gpio port b
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR; // read back to ensure clock is enabled

    // enable pins to be output
    // clear and set
    GPIOB->MODER &= ~(0x03UL << (FKL_PROBE_PIN * 2));

    // set pins low before output config
    GPIOB->BSRR = (0x1UL << (FKL_PROBE_PIN + 16));
    
    GPIOB->MODER |= (0x1UL << (FKL_PROBE_PIN * 2));
}

static void hogger_task (void *pvParameters){
    (void)pvParameters;

    for (;;) {
        GPIOB->BSRR = (0x1UL << HOG_PROBE_PIN);
        TickType_t start = xTaskGetTickCount();
        while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(20)) { }
        GPIOB->BSRR = (0x1UL << (HOG_PROBE_PIN + 16));
        vTaskDelay(pdMS_TO_TICKS(20));
    }

}

void hogger_probe_init (void){
    // GPIO port B is already enabled 
    // in fklogger_probe_init, 
    // no need to enable again
    
    // enable pins to be output
    // clear and set
    GPIOB->MODER &= ~(0x03UL << (HOG_PROBE_PIN * 2));

    // set pins low before output config
    GPIOB->BSRR = (0x1UL << (HOG_PROBE_PIN + 16));
    
    GPIOB->MODER |= (0x1UL << (HOG_PROBE_PIN * 2));
}
