/* FreeRTOSHooks.c */
#include "FreeRTOS.h"
#include "task.h"

static StaticTask_t xIdleTCB;
static StackType_t  xIdleStack[ configMINIMAL_STACK_SIZE ];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxTCB,
                                    StackType_t **ppxStack,
                                    configSTACK_DEPTH_TYPE *pulStackSize )
{
    *ppxTCB       = &xIdleTCB;
    *ppxStack     = xIdleStack;
    *pulStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
    ( void ) xTask;
    ( void ) pcTaskName;
    taskDISABLE_INTERRUPTS();
    __BKPT(0);
    for( ;; );
}
