/**
 * @file freertos_app.c
 * @brief FreeRTOS scheduling shell around the shared bounded Modbus poll code.
 */

#include "freertos_app.h"

#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "main.h"

#define MODBUS_TASK_STACK_WORDS (768U)
#define MODBUS_TASK_PRIORITY    (tskIDLE_PRIORITY + 2U)

static StaticTask_t g_modbus_task_control;
static StackType_t g_modbus_task_stack[MODBUS_TASK_STACK_WORDS];
static TaskHandle_t g_modbus_task_handle;

static StaticTask_t g_idle_task_control;
static StackType_t g_idle_task_stack[configMINIMAL_STACK_SIZE];

/** @brief Stop deterministically after a failed FreeRTOS configuration assertion. */
void vApplicationAssert(const char *file, int line)
{
    (void)file;
    (void)line;
    __disable_irq();
    for(;;)
    {
    }
}

/** @brief Run the same bounded Modbus service used by the bare-metal loop. */
static void modbus_task(void *argument)
{
    (void)argument;

    if(app_init() != HAL_OK)
    {
        Error_Handler();
    }

    for(;;)
    {
        app_poll();
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}

/** @brief Supply caller-owned storage for the mandatory FreeRTOS idle task. */
void vApplicationGetIdleTaskMemory(StaticTask_t **idle_task_control,
                                   StackType_t **idle_task_stack,
                                   uint32_t *idle_task_stack_size)
{
    *idle_task_control = &g_idle_task_control;
    *idle_task_stack = g_idle_task_stack;
    *idle_task_stack_size = configMINIMAL_STACK_SIZE;
}

/** @brief Stop deterministically if FreeRTOS detects a task stack overflow. */
void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    Error_Handler();
}

/** @brief Create all static objects and transfer control to FreeRTOS. */
HAL_StatusTypeDef freertos_app_start(void)
{
    g_modbus_task_handle = xTaskCreateStatic(modbus_task,
                                             "modbus",
                                             MODBUS_TASK_STACK_WORDS,
                                             NULL,
                                             MODBUS_TASK_PRIORITY,
                                             g_modbus_task_stack,
                                             &g_modbus_task_control);
    if(g_modbus_task_handle == NULL)
    {
        return HAL_ERROR;
    }

    vTaskStartScheduler();
    return HAL_ERROR;
}
