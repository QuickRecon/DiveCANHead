#include "printer.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include "cmsis_os.h"
#include "queue.h"
#include "main.h"
#include "../common.h"

extern UART_HandleTypeDef huart2;

void PrinterTask(void *arg);

typedef struct PrintQueue_s
{
    char string[200];
} PrintQueue_t;

// FreeRTOS tasks
static osThreadId_t *getOSThreadId(void)
{
    static osThreadId_t PrinterTaskHandle;
    return &PrinterTaskHandle;
}

static QueueHandle_t *getQueueHandle()
{
    static QueueHandle_t PrintQueue;
    return &PrintQueue;
}

void InitPrinter(void)
{
    // Setup task
    static uint32_t PrinterTask_buffer[PRINTER_STACK_SIZE];
    static StaticTask_t PrinterTask_ControlBlock;
    static const osThreadAttr_t PrinterTask_attributes = {
        .name = "PrinterTask",
        .attr_bits = osThreadDetached,
        .cb_mem = &PrinterTask_ControlBlock,
        .cb_size = sizeof(PrinterTask_ControlBlock),
        .stack_mem = &PrinterTask_buffer[0],
        .stack_size = sizeof(PrinterTask_buffer),
        .priority = PRINTER_PRIORITY,
        .tz_module = 0,
        .reserved = 0};

    // Setup print queue
    static StaticQueue_t PrintQueue_QueueStruct;
    static uint8_t PrintQueue_Storage[10 * sizeof(PrintQueue_t)];
    QueueHandle_t *printQueue = getQueueHandle();
    *printQueue = xQueueCreateStatic(1, sizeof(PrintQueue_t), PrintQueue_Storage, &PrintQueue_QueueStruct);

    osThreadId_t *PrinterTaskHandle = getOSThreadId();
    *PrinterTaskHandle = osThreadNew(PrinterTask, NULL, &PrinterTask_attributes);
}

void PrinterTask(void *arg) // Yes this warns but it needs to be that way for matching the caller
{
    // char string[200];
    QueueHandle_t *printQueue = getQueueHandle();
    while (true)
    {
        PrintQueue_t printItem = {0};

        // Wait until there is an item in the queue, if there is then print it over the uart
        if ((pdTRUE == xQueueReceive(*printQueue, &printItem, TIMEOUT_4s)))
        {
            while (huart2.gState != HAL_UART_STATE_READY)
            {
                osDelay(5);
            }
            // Printing is non-critical so shout our data at the peripheral and if it doesn't make it then we don't really care
            // Better to be fast here and get back to keeping the diver alive rather than printing to a console that may or may not exist
            (void)HAL_UART_Transmit(&huart2, (uint8_t *)(printItem.string), strlen(printItem.string), 0xFFFFFF); // send message via UART
        }
    }
}

static PrintQueue_t enQueueItem = {0};
void vprint(const char *fmt, va_list argp)
{
    if (0 < vsprintf(enQueueItem.string, fmt, argp)) // build string
    {
        xQueueSend(*(getQueueHandle()), &enQueueItem, 0);
    }
}

void serial_printf(const char *fmt, ...) // custom printf() function
{
    va_list argp;
    va_start(argp, fmt);
    vprint(fmt, argp);
    va_end(argp);
}