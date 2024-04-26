#include "log.h"
#include "fatfs.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "cmsis_os.h"
#include "queue.h"
#include "main.h"
#include "../common.h"
#include "./printer.h"
#include "../errors.h"

extern SD_HandleTypeDef hsd1;

#define LOGQUEUE_LENGTH 10

const char *const LOG_FILENAMES[6] = {
    "LOG.TXT",
    "DIVECAN.CSV",
    "I2C.CSV",
    "PPO2.CSV",
    "ANALOG.CSV",
    "DIVEO2.CSV"};

/* SD card driver overrides to make the DMA work properly */
uint8_t BSP_SD_WriteBlocks_DMA(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks)
{
    uint8_t sd_state = MSD_OK;
    /* Since we are only using 1 DMA channel for SDIO
     * Change DMA direction before calling SD Read
     * Direction can only be changed when DMA is disabled */

    __HAL_DMA_DISABLE(hsd1.hdmatx);

    hsd1.hdmatx->Init.Direction = DMA_MEMORY_TO_PERIPH;

    hsd1.hdmatx->Instance->CCR |= DMA_CCR_DIR;

    /* Write block(s) in DMA transfer mode */
    if (HAL_SD_WriteBlocks_DMA(&hsd1, (uint8_t *)pData, WriteAddr, NumOfBlocks) != HAL_OK)
    {
        sd_state = MSD_ERROR;
    }

    return sd_state;
}

uint8_t BSP_SD_ReadBlocks_DMA(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks)
{
    uint8_t sd_state = MSD_OK;

    /* Since we are only using 1 DMA channel for SDIO
     * Change DMA direction before calling SD Read
     * Direction can only be changed when DMA is disabled */

    __HAL_DMA_DISABLE(hsd1.hdmarx);

    hsd1.hdmarx->Init.Direction = DMA_PERIPH_TO_MEMORY;

    hsd1.hdmarx->Instance->CCR &= ~DMA_CCR_DIR;
    /* Read block(s) in DMA transfer mode */
    if (HAL_SD_ReadBlocks_DMA(&hsd1, (uint8_t *)pData, ReadAddr, NumOfBlocks) != HAL_OK)
    {
        sd_state = MSD_ERROR;
    }

    return sd_state;
}

void LogTask(void *arg);

typedef struct
{
    LogType_t eventType;
    char string[LOG_LINE_LENGTH];
} LogQueue_t;

/* FreeRTOS tasks */
static osThreadId_t *getOSThreadId(void)
{
    static osThreadId_t LogTaskHandle;
    return &LogTaskHandle;
}

static QueueHandle_t *getQueueHandle(void)
{
    static QueueHandle_t PrintQueue;
    return &PrintQueue;
}

void LogTask(void *arg) /* Yes this warns but it needs to be that way for matching the caller */
{
    QueueHandle_t *logQueue = getQueueHandle();
    FRESULT res = FR_OK; /* FatFs function common result code */

    LogType_t currLog = LOG_EVENT;
    res = f_open(&SDFile, LOG_FILENAMES[currLog], FA_OPEN_APPEND | FA_WRITE);
    while (FR_OK == res)
    {
        LogQueue_t logItem = {0};
        /* Wait until there is an item in the queue, if there is then Log it*/
        if ((pdTRUE == xQueueReceive(*logQueue, &logItem, TIMEOUT_4s_TICKS)))
        {
            if (logItem.eventType != currLog)
            {
                res = f_close(&SDFile);
                if (FR_OK == res)
                {
                    res = f_open(&SDFile, LOG_FILENAMES[logItem.eventType], FA_OPEN_APPEND | FA_WRITE);
                    currLog = logItem.eventType;
                }
                else
                {
                    serial_printf("Cannot close");
                }
            }
            if (FR_OK == res)
            {
                uint32_t expectedLength = strnlen((char *)logItem.string, LOG_LINE_LENGTH);
                uint32_t byteswritten = 0;
                res = f_write(&SDFile, logItem.string, expectedLength, (void *)&byteswritten);
                if (expectedLength > byteswritten)
                {
                    /* Out of space (file grown > 4Gig?)*/
                    /* TODO: Handle this, move/delete file? and rollover?*/
                }
            }
            else
            {
                serial_printf("Cannot open %s\r\n", LOG_FILENAMES[logItem.eventType]);
            }

            if (FR_OK == res)
            {
                res = f_sync(&SDFile);
            }
            else
            {
                serial_printf("Cannot write %s\r\n", LOG_FILENAMES[logItem.eventType]);
            }
        }
    }

    NON_FATAL_ERROR_DETAIL(LOGGING_ERR, res);
    vTaskDelete(NULL); /* Cleanly exit*/
}

void InitLog(void)
{
    FRESULT res = FR_OK; /* FatFs function common result code */

    res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
    if (res != FR_OK)
    {
        Error_Handler();
    }
    else
    {
        /* Setup task */
        static uint32_t LogTask_buffer[LOG_STACK_SIZE];
        static StaticTask_t LogTask_ControlBlock;
        static const osThreadAttr_t LogTask_attributes = {
            .name = "LogTask",
            .attr_bits = osThreadDetached,
            .cb_mem = &LogTask_ControlBlock,
            .cb_size = sizeof(LogTask_ControlBlock),
            .stack_mem = &LogTask_buffer[0],
            .stack_size = sizeof(LogTask_buffer),
            .priority = LOG_PRIORITY,
            .tz_module = 0,
            .reserved = 0};

        /* Setup print queue */
        static StaticQueue_t LogQueue_QueueStruct;
        static uint8_t LogQueue_Storage[LOGQUEUE_LENGTH * sizeof(LogQueue_t)];
        QueueHandle_t *LogQueue = getQueueHandle();
        *LogQueue = xQueueCreateStatic(LOGQUEUE_LENGTH, sizeof(LogQueue_t), LogQueue_Storage, &LogQueue_QueueStruct);

        osThreadId_t *LogTaskHandle = getOSThreadId();
        *LogTaskHandle = osThreadNew(LogTask, NULL, &LogTask_attributes);
    }
}

bool logRunning(void)
{
    osThreadId_t *logTask = getOSThreadId();
    return !((osThreadGetState(*logTask) == osThreadError) ||
             (osThreadGetState(*logTask) == osThreadInactive) ||
             (osThreadGetState(*logTask) == osThreadTerminated));
}

void LogMsg(const char *msg)
{
    static LogQueue_t enQueueItem = {0};
    enQueueItem.eventType = LOG_EVENT;

    char local_msg[LOG_LINE_LENGTH] = {0};
    (void)strncpy(local_msg, msg, LOG_LINE_LENGTH);

    /* Strip existing newlines */
    local_msg[strcspn(local_msg, "\r\n")] = 0;

    /* Build the string and queue it if its legal */
    if (logRunning() && (0 < snprintf(enQueueItem.string, sizeof(enQueueItem.string), "[%f]: %s\r\n", (double)osKernelGetTickCount() / (double)osKernelGetTickFreq(), local_msg)))
    {
        xQueueSend(*(getQueueHandle()), &enQueueItem, 0);
    }
}

void DiveO2CellSample(const char *const PPO2, const char *const temperature, const char *const err, const char *const phase, const char *const intensity, const char *const ambientLight, const char *const pressure, const char *const humidity)
{
    static LogQueue_t enQueueItem = {0};
    enQueueItem.eventType = LOG_DIVE_O2_SENSOR;

    /* Build the string and queue it if its legal */
    if (logRunning() && (0 < snprintf(enQueueItem.string, sizeof(enQueueItem.string), "%f,%s,%s,%s,%s,%s,%s,%s,%s\r\n", (double)osKernelGetTickCount() / (double)osKernelGetTickFreq(), PPO2, temperature, err, phase, intensity, ambientLight, pressure, humidity)))
    {
        xQueueSend(*(getQueueHandle()), &enQueueItem, 0);
    }
}