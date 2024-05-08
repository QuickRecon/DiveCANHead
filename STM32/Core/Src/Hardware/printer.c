#include "printer.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include "cmsis_os.h"
#include "queue.h"
#include "main.h"
#include "../common.h"
#include "log.h"

#define PRINTQUEUE_LENGTH 10

extern UART_HandleTypeDef huart2;

void PrinterTask(void *arg);

typedef struct
{
    char string[LOG_LINE_LENGTH];
} PrintQueue_t;

/* FreeRTOS tasks */
static osThreadId_t *getOSThreadId(void)
{
    static osThreadId_t PrinterTaskHandle;
    return &PrinterTaskHandle;
}

static QueueHandle_t *getQueueHandle(void)
{
    static QueueHandle_t PrintQueue;
    return &PrintQueue;
}

typedef struct
{
    bool uartEnable;
} PrinterTask_params_t;

void InitPrinter(bool uartOut)
{
    static PrinterTask_params_t params = {0};
    params.uartEnable = uartOut;

    /* Setup task */
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

    /* Setup print queue */
    static StaticQueue_t PrintQueue_QueueStruct;
    static uint8_t PrintQueue_Storage[PRINTQUEUE_LENGTH * sizeof(PrintQueue_t)];
    QueueHandle_t *printQueue = getQueueHandle();
    *printQueue = xQueueCreateStatic(PRINTQUEUE_LENGTH, sizeof(PrintQueue_t), PrintQueue_Storage, &PrintQueue_QueueStruct);

    osThreadId_t *PrinterTaskHandle = getOSThreadId();
    *PrinterTaskHandle = osThreadNew(PrinterTask, &params, &PrinterTask_attributes);
}

void PrinterTask(void *arg) /* Yes this warns but it needs to be that way for matching the caller */
{
    PrinterTask_params_t *taskParams = (PrinterTask_params_t *)arg;
    QueueHandle_t *printQueue = getQueueHandle();
    while (true)
    {
        PrintQueue_t printItem = {0};

        /* Wait until there is an item in the queue, if there is then print it over the uart */
        if (pdTRUE == xQueueReceive(*printQueue, &printItem, TIMEOUT_4s_TICKS))
        {
            /* Printing is non-critical so shout our data at the peripheral and if it doesn't make it then we don't really care
             * Better to be fast here and get back to keeping the diver alive rather than printing to a console that may or may not exist
             * TODO(Aren): this is a blocking call, this is bad
             */
            if (taskParams->uartEnable)
            {
                while (huart2.gState != HAL_UART_STATE_READY)
                {
                    (void)osDelay(TIMEOUT_5MS);
                }
                (void)HAL_UART_Transmit(&huart2, (uint8_t *)(printItem.string), (uint16_t)strnlen(printItem.string, LOG_LINE_LENGTH), TIMEOUT_4s_TICKS);
            }
            LogMsg(printItem.string);
        }
    }
}

void vprint(const char *fmt, va_list argp)
{
    static PrintQueue_t enQueueItem = {0};
    /* Build the string and queue it if its legal */
    if (0 < vsprintf(enQueueItem.string, fmt, argp))
    {
        xQueueSend(*(getQueueHandle()), &enQueueItem, 0);
    }
}

/** @brief Print string to UART2
 *  @param fmt printf-style format string
 *  @param  parameters
 */
void serial_printf(const char *fmt, ...)
{
    va_list argp = {0};
    va_start(argp, fmt);
    vprint(fmt, argp);
    va_end(argp);
}

void blocking_serial_printf(const char *fmt, ...)
{
    va_list argp = {0};
    va_start(argp, fmt);
    static char outStr[LOG_LINE_LENGTH] = {0};
    if (0 < vsprintf(outStr, fmt, argp))
    {
        (void)HAL_UART_Transmit(&huart2, (uint8_t *)(outStr), (uint16_t)strnlen(outStr, LOG_LINE_LENGTH), TIMEOUT_4s_TICKS);
    }
    va_end(argp);
}
