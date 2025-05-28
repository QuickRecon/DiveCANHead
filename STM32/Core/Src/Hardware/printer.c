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
#include "fatfs.h"

#define PRINTQUEUE_LENGTH 10

extern CAN_HandleTypeDef hcan1;

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

static bool *getQueueStatus(void)
{
    static bool queueReady = false;
    return &queueReady;
}

static void markQueueReady(void)
{
    *getQueueStatus() = true;
}

static bool isQueueReady(void)
{
    return *getQueueStatus();
}

typedef struct
{
    bool printEnable;
} PrinterTask_params_t;

void InitPrinter(bool printToCanbus)
{
    static PrinterTask_params_t params = {0};
    params.printEnable = printToCanbus;

    /* Setup task */
    static uint8_t PrinterTask_buffer[PRINTER_STACK_SIZE];
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
    markQueueReady();

    osThreadId_t *PrinterTaskHandle = getOSThreadId();
    *PrinterTaskHandle = osThreadNew(PrinterTask, &params, &PrinterTask_attributes);
}

void PrinterTask(void *arg)
{
    const PrinterTask_params_t *const taskParams = (PrinterTask_params_t *)arg;
    QueueHandle_t *printQueue = getQueueHandle();
    while (true)
    {
        PrintQueue_t printItem = {0};

        /* Wait until there is an item in the queue, if there is then print it over the canbus */
        if (pdTRUE == xQueueReceive(*printQueue, &printItem, TIMEOUT_4s_TICKS))
        {
            if (taskParams->printEnable)
            {
                txLogText(DIVECAN_SOLO, printItem.string, (uint16_t)strnlen(printItem.string, LOG_LINE_LENGTH));
            }
            LogMsg(printItem.string);
        }
    }
}

void vprint(const char *fmt, va_list argp)
{
    static PrintQueue_t enQueueItem = {0};
    /* Build the string and queue it if its legal */
    if ((0 < vsprintf(enQueueItem.string, fmt, argp)) && isQueueReady())
    {
        xQueueSend(*(getQueueHandle()), &enQueueItem, 0);
    }
}

/** @brief Print string to canbus and log
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

static const char *const BlockingLogPath = "BLOCKLOG.TXT";

void blocking_fs_log(const char *msg, uint32_t len)
{
    uint32_t freeBytes = 0;
    FRESULT res = FR_OK; /* FatFs function common result code */
    FATFS *ret = &SDFatFS;
    if (FR_OK == f_getfree((TCHAR const *)SDPath, &freeBytes, &ret))
    {
        static FIL LogFile = {0};
        res = f_open(&LogFile, BlockingLogPath, FA_OPEN_APPEND | FA_WRITE);
        if (res != FR_OK)
        {
            NON_FATAL_ERROR(FS_ERR);
        }
        uint32_t byteswritten = 0;
        res = f_write(&LogFile, msg, len, (void *)&byteswritten);
        if (res != FR_OK)
        {
            NON_FATAL_ERROR(FS_ERR);
        }
        res = f_close(&LogFile);
        if (res != FR_OK)
        {
            NON_FATAL_ERROR(FS_ERR);
        }
    }
}

static const uint8_t MAX_MSG_FRAGMENT = 8;
static const uint8_t TX_WAIT_DELAY = 10;
void blocking_can_log(const char *msg, uint32_t len)
{
    uint16_t remainingLength = (uint16_t)len;
    uint8_t bytesToWrite = 0;

    for (uint8_t i = 0; i < len; i += MAX_MSG_FRAGMENT)
    {
        if (remainingLength < MAX_MSG_FRAGMENT)
        {
            bytesToWrite = (uint8_t)remainingLength;
        }
        else
        {
            bytesToWrite = MAX_MSG_FRAGMENT;
        }
        uint8_t msgBuf[8] = {0};
        (void)memcpy(msgBuf, msg + i, bytesToWrite);
        const DiveCANMessage_t message = {
            .id = LOG_TEXT_ID,
            .data = {msgBuf[0], msgBuf[1], msgBuf[2], msgBuf[3], msgBuf[4], msgBuf[5], msgBuf[6], msgBuf[7]},
            .length = bytesToWrite,
            .type = "LOG_TEXT"};

        /* This isn't super time critical so if we're still waiting on stuff to tx then we can quite happily just wait */
        while (0 == HAL_CAN_GetTxMailboxesFreeLevel(&hcan1))
        {
            (void)osDelay(TX_WAIT_DELAY);
        }

        CAN_TxHeaderTypeDef header = {0};
        header.StdId = 0x0;
        header.ExtId = message.id;
        header.RTR = CAN_RTR_DATA;
        header.IDE = CAN_ID_EXT;
        header.DLC = message.length;
        header.TransmitGlobalTime = DISABLE;

        uint32_t mailboxNumber = 0;

        HAL_StatusTypeDef err = HAL_CAN_AddTxMessage(&hcan1, &header, message.data, &mailboxNumber);
        if (HAL_OK != err)
        {
            NON_FATAL_ERROR_DETAIL(CAN_TX_ERR, err);
        }
        remainingLength -= bytesToWrite;
    }
}

void blocking_serial_printf(const char *fmt, ...)
{
    va_list argp = {0};
    va_start(argp, fmt);
    static char outStr[LOG_LINE_LENGTH] = {0};
    int32_t len = vsprintf(outStr, fmt, argp);
    if (0 < len)
    {
        blocking_fs_log(outStr, (uint32_t)len);
    }
    va_end(argp);
}
