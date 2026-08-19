// main.c  

#include "FreeRTOS.h"
#include "task.h"

#include <stm32f4xx.h>
#include "bsp/clock.h"
// #include "bsp/board.h"

// #include "drivers/spi.h"
// #include "drivers/ADXL345.h"

#define GREEN_LED_PIN 8
#define RED_LED_PIN 10

static StaticTask_t xBlink1TCB;
static StackType_t  xBlink1Stack[ 128 ];

static StaticTask_t xBlink2TCB;
static StackType_t xBlink2Stack[ 128 ];

typedef struct
{
    GPIO_TypeDef    *port;
    uint32_t        pin;
    uint32_t        period_ms;
} blink_cfg_t;

static const blink_cfg_t green_cfg = {GPIOA, GREEN_LED_PIN, 500}; // using rnd periods
static const blink_cfg_t red_cfg = {GPIOB, RED_LED_PIN, 323};

static inline void led_on(GPIO_TypeDef *port, uint32_t pin)  { port->BSRR = 1UL << (pin + 16); }
static inline void led_off(GPIO_TypeDef *port, uint32_t pin) { port->BSRR = 1UL << pin; }

static void blink_task(void *pvParameters);
void led_init(void);

int main(void)
{   
    // inits
    clock_init();
    led_init();

    // xTaskCreateStatic(...) x2, each with its own static TCB + stack
    TaskHandle_t h1 = xTaskCreateStatic(
        blink_task,        // function
        "GreenBlink",          // name (debug only, ≤ configMAX_TASK_NAME_LEN)
        128,               // stack depth in WORDS — match your array length
        (void *)&green_cfg,              // pvParameters
        1,                 // priority
        xBlink1Stack,      // stack buffer
        &xBlink1TCB        // TCB buffer
    );
    configASSERT(h1 != NULL);

    TaskHandle_t h2 = xTaskCreateStatic(
        blink_task,        // function
        "RedBlink",          // name (debug only, ≤ configMAX_TASK_NAME_LEN)
        128,               // stack depth in WORDS — match your array length
        (void *)&red_cfg,              // pvParameters
        2,                 // priority
        xBlink2Stack,      // stack buffer
        &xBlink2TCB        // TCB buffer
    );
    configASSERT(h2 != NULL);

    vTaskStartScheduler();

    for (;;) {}
}

void led_init(void)
{   // USE active low PA8 and PB10

    // enable gpio port a and b
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;

    // enable pins to be output
    // clear and set
    GPIOA->MODER &= ~GPIO_MODER_MODER8;
    GPIOB->MODER &= ~GPIO_MODER_MODER10;

    // set pins high before output config
    GPIOA->BSRR = (0x1UL << (GREEN_LED_PIN));
    GPIOB->BSRR = (0x1UL << (RED_LED_PIN));
    
    GPIOA->MODER |= (0x1UL << (GREEN_LED_PIN * 2));
    GPIOB->MODER |= (0x1UL << (RED_LED_PIN  * 2));
}

static void blink_task(void *pvParameters)
{
    // pvParameters carries whatever you passed at creation
    const blink_cfg_t *cfg = (const blink_cfg_t *)pvParameters;

    for (;;) {
        // toggle
        led_on(cfg->port, cfg->pin);
        vTaskDelay(pdMS_TO_TICKS(cfg->period_ms));
        led_off(cfg->port, cfg->pin);
        vTaskDelay(pdMS_TO_TICKS(cfg->period_ms));
    }
}