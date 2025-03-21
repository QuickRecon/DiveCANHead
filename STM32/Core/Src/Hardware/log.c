#include "log.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "fatfs.h"
#include "cmsis_os.h"
#include "queue.h"
#include "main.h"
#include "../common.h"
#include "./printer.h"
#include "../errors.h"
#include "../PPO2Control/PPO2Control.h"

extern SD_HandleTypeDef hsd1;

#define LOGQUEUE_LENGTH 12
#define FILENAME_LENGTH 13
#define MAXPATH_LENGTH 255

typedef float timestamp_t;

#define LOGFILE_COUNT 2

const char LOG_FILENAMES[LOGFILE_COUNT][FILENAME_LENGTH] = {
    "LOG.TXT",
    "EVENTS.CSV"};

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

void LogTask(void *args);

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

static osMessageQueueId_t *getQueueHandle(void)
{
    static osMessageQueueId_t PrintQueue;
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
        do
        {
            res = f_readdir(&dir, &fno); /* Read a directory item */
            if (((fno.fattrib & AM_DIR) != 0) && (FR_OK == res) && (0 != fno.fname[0]))
            {
                uint32_t dirNum = (uint32_t)strtol(fno.fname, NULL, 10);
                if (dirNum > maxDir)
                {
                    maxDir = dirNum;
                }
            }
            else if (FR_OK != res)
            {
                blocking_serial_printf("Failed to read Dir (%u)\r\n", res);
            }
            else
            {
                /* its just a file don't worry about it */
            }
        } while ((res == FR_OK) && (0 != fno.fname[0]));
        res = f_closedir(&dir);
        *index = maxDir + 1;
    }
    else
    {
        blocking_serial_printf("Failed to open root (%u)\r\n", res);
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
        do
        {
            res = f_readdir(&dir, &fno); /* Read a directory item */
            if ((!((fno.fattrib & AM_DIR) != 0)) && (FR_OK == res))
            {
                char NewName[MAXPATH_LENGTH] = {0};
                (void)snprintf(NewName, MAXPATH_LENGTH, "%s/%s", dirname, fno.fname);
                res = f_rename(fno.fname, NewName);
                blocking_serial_printf("Moved %s to %s\r\n", fno.fname, NewName);
                if (res != FR_OK)
                {
                    blocking_serial_printf("Failed to rename %s\r\n", fno.fname);
                }
            }
            else if (FR_OK != res)
            {
                blocking_serial_printf("Failed to read file (%u)\r\n", res);
            }
            else
            {
                /* its just a dir don't worry about it */
            }
        } while ((res == FR_OK) && (0 != fno.fname[0]));
        res = f_closedir(&dir);
    }
    else
    {
        blocking_serial_printf("Failed to open root (%u)\n", res);
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
        blocking_serial_printf("Cannot find next dir (%d)\r\n", res);
    }
    serial_printf("Found next dir %lu\r\n", nextFileNum);
    /* Step 3, move */
    if (FR_OK == res)
    {
        res = MoveIntoDir(dirname);
    }
    else
    {
        blocking_serial_printf("Cannot make next dir %s (%d)\r\n", dirname, res);
    }
    blocking_serial_printf("finished move\r\n");
    return res;
}

extern UART_HandleTypeDef huart2;
void LogTask(void *) /* Yes this warns but it needs to be that way for matching the caller */
{
    FIL LOG_FILES[LOGFILE_COUNT] = {0};
    osMessageQueueId_t *logQueue = getQueueHandle();
    FRESULT res = FR_OK; /* FatFs function common result code */

    uint8_t currSyncFile = 0;

    for (uint32_t i = 0; i < LOGFILE_COUNT; ++i)
    {
        if (FR_OK == res)
        {
            res = f_open(&(LOG_FILES[i]), LOG_FILENAMES[i], FA_OPEN_APPEND | FA_WRITE);
            if (res != FR_OK)
            {
                blocking_serial_printf("Failed to open %s", LOG_FILENAMES[i]);
            }
        }
    }

    uint32_t lastSynced = HAL_GetTick();
    while (FR_OK == res)
    {
        LogQueue_t logItem = {0};
        /* Wait until there is an item in the queue, if there is then Log it*/
        if (osOK == osMessageQueueGet(*logQueue, &logItem, NULL, TIMEOUT_4s_TICKS))
        {
            uint32_t expectedLength = (uint32_t)strnlen((char *)logItem.string, LOG_LINE_LENGTH);
            uint32_t byteswritten = 0;

            res = f_write(&(LOG_FILES[logItem.eventType]), logItem.string, expectedLength, (void *)&byteswritten);
            if ((expectedLength > byteswritten) && (FR_OK == res))
            {
                /* Out of space (file grown > 4Gig?)*/
                blocking_serial_printf("Rotating logs");
                res = RotateLogfiles();
            }

            if (FR_OK != res)
            {
                blocking_serial_printf("Cannot write %s\r\n", LOG_FILENAMES[logItem.eventType]);
            }
        }

        if ((HAL_GetTick() - lastSynced) > TIMEOUT_4s_TICKS)
        {
            res = f_sync(&(LOG_FILES[currSyncFile]));
            currSyncFile = (currSyncFile + 1) % LOGFILE_COUNT;
            lastSynced = HAL_GetTick();
            if (res != FR_OK)
            {
                blocking_serial_printf("Failed to sync %s", LOG_FILENAMES[currSyncFile]);
            }
        }
    }

    NON_FATAL_ERROR_DETAIL(LOGGING_ERR, res);
    (void)vTaskDelete(NULL); /* Cleanly exit*/
}

void InitLog(void)
{
    /* Setup print queue */
    osMessageQueueId_t *LogQueue = getQueueHandle();
    static uint8_t LogQueue_Storage[LOGQUEUE_LENGTH * sizeof(LogQueue_t)];
    static StaticQueue_t LogQueue_ControlBlock = {0};
    const osMessageQueueAttr_t LogQueue_Attributes = {
        .name = "LogQueue",
        .cb_mem = &LogQueue_ControlBlock,
        .cb_size = sizeof(LogQueue_ControlBlock),
        .mq_mem = &LogQueue_Storage,
        .mq_size = sizeof(LogQueue_Storage)};
    *LogQueue = osMessageQueueNew(LOGQUEUE_LENGTH, sizeof(LogQueue_t), &LogQueue_Attributes);
}

void StartLogTask(void)
{
    FRESULT res = FR_OK; /* FatFs function common result code */

    res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
    if (res != FR_OK)
    {
        blocking_serial_printf("Cannot mount SD card, is one installed?\r\nLogging disabled\r\n");
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

void checkQueueStarvation(LogType_t eventType)
{
    char buff[LOG_LINE_LENGTH] = "";
    if (0 == osMessageQueueGetSpace(*(getQueueHandle())) &&
        0 < snprintf(buff, LOG_LINE_LENGTH, "Queue Starvation in log %d", eventType))
    {
        LogMsg(buff);
    }
}

void LogMsg(const char *msg)
{
    /* Single CPU with cooperative multitasking means that this is
     * valid until we've enqueued (and hence no longer care)
     * This is necessary to save literal kilobytes of ram*/
    static LogQueue_t enQueueItem = {0};
    static uint32_t logMsgIndex = 0;
    (void)memset(enQueueItem.string, 0, LOG_LINE_LENGTH);
    enQueueItem.eventType = LOG_TEXT;

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

        /* Higher priority message, enqueue as long as the queue exists, remove existing elements if needed */
        if ((getQueueHandle() != NULL) && (0 < snprintf(enQueueItem.string, LOG_LINE_LENGTH, "[%0.4f, %lu]: %s\r\n", (timestamp_t)osKernelGetTickCount() / (timestamp_t)osKernelGetTickFreq(), logMsgIndex, local_msg)))
        {
            /* High priority, clear old items to make room */
            if (0 == osMessageQueueGetSpace(*(getQueueHandle())))
            {
                LogQueue_t logItem = {0};
                (void)osMessageQueueGet(*(getQueueHandle()), &logItem, NULL, TIMEOUT_4s_TICKS);
            }
            (void)osMessageQueuePut(*(getQueueHandle()), &enQueueItem, 1, 0);
            ++logMsgIndex;
        }
    }
    else
    {
        /* We have to silently fail here less we cause recursion */
    }
}

void DiveO2CellSample(uint8_t cellNumber, int32_t PPO2, int32_t temperature, int32_t err, int32_t phase, int32_t intensity, int32_t ambientLight, int32_t pressure, int32_t humidity)
{
    /* Single CPU with cooperative multitasking means that this is
     * valid until we've enqueued (and hence no longer care)
     * This is necessary to save literal kilobytes of ram*/
    static LogQueue_t enQueueItem = {0};
    (void)memset(enQueueItem.string, 0, LOG_LINE_LENGTH);
    enQueueItem.eventType = LOG_EVENT;
    checkQueueStarvation(enQueueItem.eventType);
    /* Lower priority message, only enqueue if the log task is running AND we have room in the queue */
    if (logRunning() &&
        (0 < snprintf(enQueueItem.string, LOG_LINE_LENGTH, "%0.4f,DIVEO2,%u,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\r\n", (timestamp_t)osKernelGetTickCount() / (timestamp_t)osKernelGetTickFreq(), cellNumber, PPO2, temperature, err, phase, intensity, ambientLight, pressure, humidity)) &&
        (0 != osMessageQueueGetSpace(*(getQueueHandle()))))
    {
        (void)osMessageQueuePut(*(getQueueHandle()), &enQueueItem, 1, 0);
    }
}

void O2SCellSample(uint8_t cellNumber, O2SNumeric_t PPO2)
{
    /* Single CPU with cooperative multitasking means that this is
     * valid until we've enqueued (and hence no longer care)
     * This is necessary to save literal kilobytes of ram*/
    static LogQueue_t enQueueItem = {0};
    (void)memset(enQueueItem.string, 0, LOG_LINE_LENGTH);
    enQueueItem.eventType = LOG_EVENT;
    checkQueueStarvation(enQueueItem.eventType);
    /* Lower priority message, only enqueue if the log task is running AND we have room in the queue */
    if (logRunning() &&
        (0 < snprintf(enQueueItem.string, LOG_LINE_LENGTH, "%0.4f,O2S,%u,%f\r\n", (timestamp_t)osKernelGetTickCount() / (timestamp_t)osKernelGetTickFreq(), cellNumber, PPO2)) &&
        (0 != osMessageQueueGetSpace(*(getQueueHandle()))))
    {
        (void)osMessageQueuePut(*(getQueueHandle()), &enQueueItem, 1, 0);
    }
}

void AnalogCellSample(uint8_t cellNumber, int16_t sample)
{
    /* Single CPU with cooperative multitasking means that this is
     * valid until we've enqueued (and hence no longer care)
     * This is necessary to save literal kilobytes of ram*/
    static LogQueue_t enQueueItem = {0};
    (void)memset(enQueueItem.string, 0, LOG_LINE_LENGTH);
    enQueueItem.eventType = LOG_EVENT;
    checkQueueStarvation(enQueueItem.eventType);
    /* Lower priority message, only enqueue if the log task is running AND we have room in the queue */
    if (logRunning() &&
        (0 < snprintf(enQueueItem.string, LOG_LINE_LENGTH, "%0.4f,ANALOGCELL,%u,%d\r\n", (timestamp_t)osKernelGetTickCount() / (timestamp_t)osKernelGetTickFreq(), cellNumber, sample)) &&
        (0 != osMessageQueueGetSpace(*(getQueueHandle()))))
    {
        (void)osMessageQueuePut(*(getQueueHandle()), &enQueueItem, 1, 0);
    }
}

void LogDiveCANMessage(const DiveCANMessage_t *const message, bool rx)
{
    /* Single CPU with cooperative multitasking means that this is
     * valid until we've enqueued (and hence no longer care)
     * This is necessary to save literal kilobytes of ram*/
    static LogQueue_t enQueueItem = {0};
    static uint32_t logMsgIndex = 0;
    (void)memset(enQueueItem.string, 0, LOG_LINE_LENGTH);
    enQueueItem.eventType = LOG_EVENT;
    checkQueueStarvation(enQueueItem.eventType);
    if (message != NULL)
    {
        /* Lower priority message, only enqueue if the log task is running AND we have room in the queue */
        if (logRunning())
        {
            timestamp_t timestamp = (timestamp_t)osKernelGetTickCount() / (timestamp_t)osKernelGetTickFreq();

            /* Set RX vs TX*/
            const char *dir_str = "tx";
            if (rx)
            {
                dir_str = "rx";
            }

            uint8_t strLen = (uint8_t)snprintf(enQueueItem.string, LOG_LINE_LENGTH, "%0.4f,%lu,CAN,%s,%u,%#010lx,%#02x,%#02x,%#02x,%#02x,%#02x,%#02x,%#02x,%#02x\r\n", timestamp, logMsgIndex, dir_str, message->length, message->id,
                                               message->data[0], message->data[1], message->data[2], message->data[3], message->data[4], message->data[5], message->data[6], message->data[7]);
            if (strLen > 0)
            {
                /* High priority, clear old items to make room */
                if (0 == osMessageQueueGetSpace(*(getQueueHandle())))
                {
                    LogQueue_t logItem = {0};
                    (void)osMessageQueueGet(*(getQueueHandle()), &logItem, NULL, TIMEOUT_4s_TICKS);
                }
                (void)osMessageQueuePut(*(getQueueHandle()), &enQueueItem, 1, 0);
                ++logMsgIndex;
            }
        }
    }
    else
    {
        NON_FATAL_ERROR(NULL_PTR);
    }
}

void LogPIDState(const PIDState_t *const pid_state, PIDNumeric_t dutyCycle, PIDNumeric_t setpoint)
{
    /* Single CPU with cooperative multitasking means that this is
     * valid until we've enqueued (and hence no longer care)
     * This is necessary to save literal kilobytes of ram*/
    static LogQueue_t enQueueItem = {0};
    static uint32_t logMsgIndex = 0;
    (void)memset(enQueueItem.string, 0, LOG_LINE_LENGTH);
    enQueueItem.eventType = LOG_EVENT;
    checkQueueStarvation(enQueueItem.eventType);
    if (pid_state != NULL)
    {
        /* Lower priority message, only enqueue if the log task is running AND we have room in the queue */
        if (logRunning())
        {
            timestamp_t timestamp = (timestamp_t)osKernelGetTickCount() / (timestamp_t)osKernelGetTickFreq();

            uint8_t strLen = (uint8_t)snprintf(enQueueItem.string, LOG_LINE_LENGTH, "%0.4f,%lu,PID,%f,%d,%f,%f\r\n", timestamp, logMsgIndex, (float)pid_state->integralState, pid_state->saturationCount, (float)dutyCycle, (float)setpoint);
            if (strLen > 0)
            {
                /* High priority, clear old items to make room */
                if (0 == osMessageQueueGetSpace(*(getQueueHandle())))
                {
                    LogQueue_t logItem = {0};
                    (void)osMessageQueueGet(*(getQueueHandle()), &logItem, NULL, TIMEOUT_4s_TICKS);
                }
                (void)osMessageQueuePut(*(getQueueHandle()), &enQueueItem, 1, 0);
                ++logMsgIndex;
            }
        }
    }
}

void LogPPO2State(bool c1_included, bool c2_included, bool c3_included, PIDNumeric_t c1, PIDNumeric_t c2, PIDNumeric_t c3, PIDNumeric_t consensus)
{
    /* Single CPU with cooperative multitasking means that this is
     * valid until we've enqueued (and hence no longer care)
     * This is necessary to save literal kilobytes of ram*/
    static LogQueue_t enQueueItem = {0};
    static uint32_t logMsgIndex = 0;
    (void)memset(enQueueItem.string, 0, LOG_LINE_LENGTH);
    enQueueItem.eventType = LOG_EVENT;
    checkQueueStarvation(enQueueItem.eventType);
    /* Lower priority message, only enqueue if the log task is running AND we have room in the queue */
    if (logRunning())
    {
        timestamp_t timestamp = (timestamp_t)osKernelGetTickCount() / (timestamp_t)osKernelGetTickFreq();

        uint8_t strLen = (uint8_t)snprintf(enQueueItem.string, LOG_LINE_LENGTH, "%0.4f,%lu,PPO2,%d,%f,%d,%f,%d,%f,%f\r\n", timestamp, logMsgIndex, c1_included, (float)c1, c2_included, (float)c2, c3_included, (float)c3, consensus);
        if (strLen > 0)
        {
            /* High priority, clear old items to make room */
            if (0 == osMessageQueueGetSpace(*(getQueueHandle())))
            {
                LogQueue_t logItem = {0};
                (void)osMessageQueueGet(*(getQueueHandle()), &logItem, NULL, TIMEOUT_4s_TICKS);
            }
            (void)osMessageQueuePut(*(getQueueHandle()), &enQueueItem, 1, 0);
            ++logMsgIndex;
        }
    }
}

void LogRXDiveCANMessage(const DiveCANMessage_t *const message)
{
    LogDiveCANMessage(message, true);
}

void LogTXDiveCANMessage(const DiveCANMessage_t *const message)
{
    // LogDiveCANMessage(message, false);
}
