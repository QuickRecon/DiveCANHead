// Generic oxygen cell to provide a common calling convention for analog and digital cells

#include "OxygenCell.h"
#include "AnalogOxygen.h"
#include "DigitalOxygen.h"
#include "eeprom_emul.h"
#include "../errors.h"
#include <math.h>

typedef struct OxygenHandle_s
{
    CellType_t type;
    void *cellHandle;
} OxygenHandle_t;

typedef struct CalParameters_s
{
    DiveCANType_t deviceType;
    FO2_t fO2;
    uint16_t pressure_val;

    ShortMillivolts_t cell1;
    ShortMillivolts_t cell2;
    ShortMillivolts_t cell3;

    OxygenCalMethod_t calMethod;
} CalParameters_t;

static QueueHandle_t *getQueueHandle(uint8_t cellNum)
{
    static QueueHandle_t cellQueues[CELL_COUNT];
    QueueHandle_t *queueHandle = NULL;
    if (cellNum >= CELL_COUNT)
    {
        NON_FATAL_ERROR(INVALID_CELL_NUMBER);
        queueHandle = &(cellQueues[0]); // A safe fallback
    }
    else
    {
        queueHandle = &(cellQueues[cellNum]);
    }
    return queueHandle;
}

static OxygenHandle_t *getCell(uint8_t cellNum)
{
    static OxygenHandle_t cells[CELL_COUNT];
    OxygenHandle_t *cellHandle = NULL;
    if (cellNum >= CELL_COUNT)
    {
        NON_FATAL_ERROR(INVALID_CELL_NUMBER);
        cellHandle = &(cells[0]); // A safe fallback
    }
    else
    {
        cellHandle = &(cells[cellNum]);
    }
    return cellHandle;
}

extern void serial_printf(const char *fmt, ...);

QueueHandle_t CreateCell(uint8_t cellNumber, CellType_t type)
{
    OxygenHandle_t *cell = getCell(cellNumber);

    static StaticQueue_t CellQueues_QueueStruct[CELL_COUNT];
    static uint8_t CellQueues_Storage[CELL_COUNT][sizeof(OxygenCell_t)];
    QueueHandle_t *queueHandle = getQueueHandle(cellNumber);
    *queueHandle = xQueueCreateStatic(1, sizeof(OxygenCell_t), CellQueues_Storage[cellNumber], &(CellQueues_QueueStruct[cellNumber]));

    cell->type = type;
    switch (type)
    {
    case CELL_ANALOG:
        cell->cellHandle = Analog_InitCell(cellNumber, *queueHandle);
        break;
    case CELL_DIGITAL:
        cell->cellHandle = Digital_InitCell(cellNumber, *queueHandle);
        break;
    default:
        NON_FATAL_ERROR(UNREACHABLE_ERROR);
    }
    return *queueHandle;
}

DiveCANCalResponse_t DigitalReferenceCalibrate(CalParameters_t *calParams)
{
    DiveCANCalResponse_t calPass = DIVECAN_CAL_RESULT;
    const DigitalOxygenState_t *refCell = NULL;
    uint8_t refCellIndex = 0;
    // Select the first digital cell
    for (uint8_t i = 0; i < CELL_COUNT; ++i)
    {
        const OxygenHandle_t *const cell = getCell(i);
        if ((CELL_DIGITAL == cell->type) && (NULL == refCell))
        {
            refCell = (const DigitalOxygenState_t *)cell->cellHandle;
            refCellIndex = i;
        }
    }

    QueueHandle_t *queueHandle = getQueueHandle(refCellIndex);

    OxygenCell_t refCellData = {0};
    if ((refCell != NULL) && (pdTRUE == xQueuePeek(*queueHandle, &refCellData, TIMEOUT_100MS)) && (refCellData.status == CELL_OK))
    {
        PPO2_t ppO2 = refCellData.ppo2;
        uint16_t pressure = (uint16_t)(refCell->pressure / 1000);

        // Now that we have the PPO2 we cal all the analog cells
        ShortMillivolts_t cellVals[CELL_COUNT] = {0};
        NonFatalError_t calErrors[CELL_COUNT] = {ERR_NONE, ERR_NONE, ERR_NONE};

        for (uint8_t i = 0; i < CELL_COUNT; ++i)
        {
            OxygenHandle_t *cell = getCell(i);
            if (CELL_ANALOG == cell->type)
            {
                AnalogOxygenState_t *analogCell = (AnalogOxygenState_t *)cell->cellHandle;
                cellVals[i] = Calibrate(analogCell, ppO2, &(calErrors[i]));
            }

            if (calErrors[i] == ERR_NONE)
            {
                calPass = DIVECAN_CAL_RESULT;
            }
            else
            {
                calPass = DIVECAN_CAL_FAIL_GEN;
            }
        }

        // Now that calibration is done lets grab the millivolts for the record
        calParams->cell1 = cellVals[CELL_1];
        calParams->cell2 = cellVals[CELL_2];
        calParams->cell3 = cellVals[CELL_3];

        calParams->pressure_val = pressure;
        calParams->fO2 = (FO2_t)round((Numeric_t)ppO2 * (1000.0f / (Numeric_t)pressure));
    }
    else
    {
        // We can't find a digital cell to cal with
        NON_FATAL_ERROR(CAL_METHOD_ERROR);
        calPass = DIVECAN_CAL_FAIL_REJECTED;
    }
    return calPass;
}

void CalibrationTask(void *arg)
{
    CalParameters_t calParams = *((CalParameters_t *)arg);
    DiveCANCalResponse_t calResult = DIVECAN_CAL_FAIL_REJECTED;
    switch (calParams.calMethod)
    {
    case CAL_DIGITAL_REFERENCE: // Calibrate using the solid state cell as a reference
        calResult = DigitalReferenceCalibrate(&calParams);
        osDelay(TIMEOUT_4s); // Give the shearwater time to catch up
        break;
    case CAL_ANALOG_ABSOLUTE:
    case CAL_TOTAL_ABSOLUTE:
        calResult = DIVECAN_CAL_FAIL_REJECTED;
        NON_FATAL_ERROR(UNDEFINED_CAL_METHOD);
        break;
    default:
        calResult = DIVECAN_CAL_FAIL_REJECTED;
        NON_FATAL_ERROR(UNDEFINED_CAL_METHOD);
    }

    txCalResponse(calParams.deviceType, calResult, calParams.cell1, calParams.cell2, calParams.cell3, calParams.fO2, calParams.pressure_val);

    osThreadExit();
}

static osThreadId_t *getOSThreadId(void)
{
    static osThreadId_t calTask;
    return &calTask;
}

bool isCalibrating(void)
{
    osThreadId_t *calTask = getOSThreadId();
    return !((osThreadGetState(*calTask) == osThreadError) ||
             (osThreadGetState(*calTask) == osThreadInactive) ||
             (osThreadGetState(*calTask) == osThreadTerminated));
}

void RunCalibrationTask(DiveCANType_t deviceType, const FO2_t in_fO2, const uint16_t in_pressure_val)
{
    static CalParameters_t calParams;

    calParams.fO2 = in_fO2;
    calParams.pressure_val = in_pressure_val;
    calParams.deviceType = deviceType;
    calParams.cell1 = 0;
    calParams.cell2 = 0;
    calParams.cell3 = 0;

    calParams.calMethod = CAL_DIGITAL_REFERENCE;

    txCalAck(deviceType);

    // Don't start the thread if we're already calibrating, shearwater double shots us sometimes
    if (!isCalibrating())
    {
        static uint32_t CalTask_buffer[CALTASK_STACK_SIZE];
        static StaticTask_t CalTask_ControlBlock;
        static const osThreadAttr_t CalTask_attributes = {
            .name = "CalTask",
            .attr_bits = osThreadDetached,
            .cb_mem = &CalTask_ControlBlock,
            .cb_size = sizeof(CalTask_ControlBlock),
            .stack_mem = &CalTask_buffer[0],
            .stack_size = sizeof(CalTask_buffer),
            .priority = CAN_PPO2_TX_PRIORITY,
            .tz_module = 0,
            .reserved = 0};

        osThreadId_t *calTask = getOSThreadId();
        *calTask = osThreadNew(CalibrationTask, &calParams, &CalTask_attributes);
    }
}
