#include "includes.h"

#define STARTUP_TASK_PRIORITY       2U
#define HELLO_TASK_PRIORITY         4U
#define LED_TASK_PRIORITY           5U

#define STARTUP_TASK_STACK_SIZE  2048U
#define HELLO_TASK_STACK_SIZE    2048U
#define LED_TASK_STACK_SIZE      1024U

static TX_THREAD startup_task_control_block;
static TX_THREAD hello_task_control_block;
static TX_THREAD led_task_control_block;

static uint64_t startup_task_stack[STARTUP_TASK_STACK_SIZE / sizeof(uint64_t)];
static uint64_t hello_task_stack[HELLO_TASK_STACK_SIZE / sizeof(uint64_t)];
static uint64_t led_task_stack[LED_TASK_STACK_SIZE / sizeof(uint64_t)];

static void startup_task_entry(ULONG thread_input);
static void hello_task_entry(ULONG thread_input);
static void led_task_entry(ULONG thread_input);
static void application_tasks_create(void);

int main(void)
{
    system_init();

    HAL_SuspendTick();
    tx_kernel_enter();

    while (1)
    {
    }
}

void tx_application_define(void *first_unused_memory)
{
    (void)first_unused_memory;

    (void)tx_thread_create(&startup_task_control_block,
                           "startup_task",
                           startup_task_entry,
                           0UL,
                           startup_task_stack,
                           sizeof(startup_task_stack),
                           STARTUP_TASK_PRIORITY,
                           STARTUP_TASK_PRIORITY,
                           TX_NO_TIME_SLICE,
                           TX_AUTO_START);
}

static void startup_task_entry(ULONG thread_input)
{
    (void)thread_input;

    HAL_ResumeTick();
    bsp_init();

    bsp_uart4_write_string("\r\nhello from ART-Pi STM32H750 ThreadX\r\n");
    bsp_uart4_write_string("UART4: PA0/TX PI9/RX, LED: PI8 PC15\r\n");

    application_tasks_create();

    while (1)
    {
        tx_thread_sleep(1000U);
    }
}

static void hello_task_entry(ULONG thread_input)
{
    ULONG heartbeat = 0UL;
    char message[96];

    (void)thread_input;

    while (1)
    {
        (void)snprintf(message,
                       sizeof(message),
                       "hello heartbeat %lu, tick=%lu\r\n",
                       heartbeat,
                       tx_time_get());
        bsp_uart4_write_string(message);
        heartbeat++;
        tx_thread_sleep(1000U);
    }
}

static void led_task_entry(ULONG thread_input)
{
    (void)thread_input;

    bsp_led_on(BSP_LED_BLUE);
    bsp_led_off(BSP_LED_RED);

    while (1)
    {
        tx_thread_sleep(500U);
        bsp_led_toggle(BSP_LED_BLUE);
        bsp_led_toggle(BSP_LED_RED);
    }
}

static void application_tasks_create(void)
{
    (void)tx_thread_create(&hello_task_control_block,
                           "hello_task",
                           hello_task_entry,
                           0UL,
                           hello_task_stack,
                           sizeof(hello_task_stack),
                           HELLO_TASK_PRIORITY,
                           HELLO_TASK_PRIORITY,
                           TX_NO_TIME_SLICE,
                           TX_AUTO_START);

    (void)tx_thread_create(&led_task_control_block,
                           "led_task",
                           led_task_entry,
                           0UL,
                           led_task_stack,
                           sizeof(led_task_stack),
                           LED_TASK_PRIORITY,
                           LED_TASK_PRIORITY,
                           TX_NO_TIME_SLICE,
                           TX_AUTO_START);
}

