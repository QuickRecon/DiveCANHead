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
#define FILENAME_LENGTH 13
#define MAXPATH_LENGTH 255

typedef double timestamp_t;

const char LOG_FILENAMES[6][FILENAME_LENGTH] = {
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

FRESULT GetNextDirIdx(uint32_t *index)
{
    FRESULT res = FR_OK;
    DIR dir = {0};
    FILINFO fno = {0};
    uint32_t maxDir = 0;

    res = f_opendir(&dir, "/"); /* Open the directory */
    if (res == FR_OK)
    {
        do {
            res = f_readdir(&dir, &fno); /* Read a directory item */
            if (((fno.fattrib & AM_DIR) != 0) && (FR_OK == res) && (0 != fno.fname[0]))
            {
                uint32_t dirNum = (uint32_t)strtol(fno.fname, NULL, 10);
                serial_printf("reading dir %s\r\n",fno.fname );
                if (dirNum > maxDir)
                {
                    maxDir = dirNum;
                }
            }
            else if(FR_OK != res)
            {
                serial_printf("Failed to read Dir (%u)\r\n", res);
            } else {
                /* its just a file don't worry about it */
            }
        } while ((res == FR_OK) && (0 != fno.fname[0]));
        res = f_closedir(&dir);
        *index = maxDir + 1;
    }
    else
    {
        serial_printf("Failed to open root (%u)\r\n", res);
    }
    return res;
}

FRESULT MoveIntoDir(char *dirname)
{
    FRESULT res = FR_OK;
    DIR dir = {0};
    FILINFO fno = {0};

    res = f_opendir(&dir, "/"); /* Open the directory */
    if (res == FR_OK)
    {
        while ((res == FR_OK) && (0 != fno.fname[0]))
        {
            res = f_readdir(&dir, &fno); /* Read a directory item */
            if ((0 == (fno.fattrib & AM_DIR)) && (FR_OK == res))
            {
                char NewName[MAXPATH_LENGTH] = {0};
                (void)snprintf(NewName, MAXPATH_LENGTH, "%s/%s", dirname, fno.fname);
                res = f_rename(fno.fname, NewName);
                if (res != FR_OK)
                {
                    serial_printf("Failed to rename %s", fno.fname);
                }
            }
            else
            {
                serial_printf("Failed to read file (%u)\n", res);
            }
        }
        res = f_closedir(&dir);
    }
    else
    {
        serial_printf("Failed to open root (%u)\n", res);
    }
    return res;
}

FRESULT RotateLogfiles(void)
{
    char dirname[FILENAME_LENGTH] = {0};
    FRESULT res = FR_OK;

    /* Step 1, find the latest directory number */
    uint32_t nextFileNum = 0;
    res = GetNextDirIdx(&nextFileNum);

    /* Step 2, make the next directory */
    if (FR_OK == res)
    {
        (void)snprintf(dirname, FILENAME_LENGTH, "%ld", nextFileNum);
        res = f_mkdir(dirname);
    }
    else
    {
        serial_printf("Cannot find next dir (%d)\r\n", res);
    }

    /* Step 3, move */
    if (FR_OK == res)
    {
        res = MoveIntoDir(dirname);
    }
    else
    {
        serial_printf("Cannot make next dir %s (%d)\r\n", dirname, res);
    }
    return res;
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
        if (pdTRUE == xQueueReceive(*logQueue, &logItem, TIMEOUT_4s_TICKS))
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
                    res = RotateLogfiles();
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
    (void)vTaskDelete(NULL); /* Cleanly exit*/
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
        res = RotateLogfiles();

        if (FR_OK == res)
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
        else
        {
            serial_printf("Cannot rotate log files (%d)\r\n", res);
        }
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
    /* Single CPU with cooperative multitasking means that this is
     * valid until we've enqueued (and hence no longer care)
     * This is necessary to save literal kilobytes of ram*/
    static LogQueue_t enQueueItem = {0};
    (void)memset(enQueueItem.string, 0, LOG_LINE_LENGTH);
    enQueueItem.eventType = LOG_EVENT;

    if (msg != NULL)
    {
        /* Single CPU with cooperative multitasking means that this is
         * valid until we've enqueued (and hence no longer care)
         * This is necessary to save literal kilobytes of ram*/
        static char local_msg[LOG_LINE_LENGTH] = {0};
        (void)memset(local_msg, 0, LOG_LINE_LENGTH);
        (void)strncpy(local_msg, msg, LOG_LINE_LENGTH - 1);

        /* Strip existing newlines */
        local_msg[strcspn(local_msg, "\r\n")] = 0;

        /* Build the string and queue it if its legal */
        if (logRunning() && (0 < snprintf(enQueueItem.string, sizeof(enQueueItem.string), "[%f]: %s\r\n", (timestamp_t)osKernelGetTickCount() / (timestamp_t)osKernelGetTickFreq(), local_msg)))
        {
            xQueueSend(*(getQueueHandle()), &enQueueItem, 0);
        }
    }
    else
    {
        /* We have to silently fail here less we cause recursion */
    }
}

void DiveO2CellSample(uint8_t cellNumber, const char *const PPO2, const char *const temperature, const char *const err, const char *const phase, const char *const intensity, const char *const ambientLight, const char *const pressure, const char *const humidity)
{
    /* Single CPU with cooperative multitasking means that this is
     * valid until we've enqueued (and hence no longer care)
     * This is necessary to save literal kilobytes of ram*/
    static LogQueue_t enQueueItem = {0};
    enQueueItem.eventType = LOG_DIVE_O2_SENSOR;

    /* Build the string and queue it if its legal */
    if (logRunning() && (0 < snprintf(enQueueItem.string, sizeof(enQueueItem.string), "%f,%d,%s,%s,%s,%s,%s,%s,%s,%s\r\n", (timestamp_t)osKernelGetTickCount() / (timestamp_t)osKernelGetTickFreq(), cellNumber, PPO2, temperature, err, phase, intensity, ambientLight, pressure, humidity)))
    {
        xQueueSend(*(getQueueHandle()), &enQueueItem, 0);
    }
}

void AnalogCellSample(uint8_t cellNumber, int16_t sample)
{
    /* Single CPU with cooperative multitasking means that this is
     * valid until we've enqueued (and hence no longer care)
     * This is necessary to save literal kilobytes of ram*/
    static LogQueue_t enQueueItem = {0};
    enQueueItem.eventType = LOG_ANALOG_SENSOR;

    /* Build the string and queue it if its legal */
    if (logRunning() && (0 < snprintf(enQueueItem.string, sizeof(enQueueItem.string), "%f,%d,%d\r\n", (timestamp_t)osKernelGetTickCount() / (timestamp_t)osKernelGetTickFreq(), cellNumber, sample)))
    {
        xQueueSend(*(getQueueHandle()), &enQueueItem, 0);
    }
}
