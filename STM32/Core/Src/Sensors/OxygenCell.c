// Generic oxygen cell to provide a common calling convention for analog and digital cells

#include "OxygenCell.h"
#include "AnalogOxygen.h"
#include "DigitalOxygen.h"
#include "eeprom_emul.h"
#include "../errors.h"

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

StaticQueue_t CellQueues_QueueStruct[CELL_COUNT];
uint8_t CellQueues_Storage[CELL_COUNT][sizeof(OxygenCell_t)];
static QueueHandle_t CellQueues[CELL_COUNT];
static CalParameters_t glob_calParams;
static OxygenHandle_t cells[CELL_COUNT];

extern void serial_printf(const char *fmt, ...);

#define CALTASK_STACK_SIZE 400 // Static analysis 304

static uint32_t CalTask_buffer[CALTASK_STACK_SIZE];
static StaticTask_t CalTask_ControlBlock;
const osThreadAttr_t CalTask_attributes = {
    .name = "CalTask",
    .cb_mem = &CalTask_ControlBlock,
    .cb_size = sizeof(CalTask_ControlBlock),
    .stack_mem = &CalTask_buffer[0],
    .stack_size = sizeof(CalTask_buffer),
    .priority = (osPriority_t)CAN_PPO2_TX_PRIORITY};

QueueHandle_t CreateCell(uint8_t cellNumber, CellType_t type)
{
    OxygenHandle_t *cell = &cells[cellNumber];
    CellQueues[cellNumber] = xQueueCreateStatic(1, sizeof(OxygenCell_t), CellQueues_Storage[cellNumber], &(CellQueues_QueueStruct[cellNumber]));
    cell->type = type;
    switch (type)
    {
    case CELL_ANALOG:
        cell->cellHandle = Analog_InitCell(cellNumber, CellQueues[cellNumber]);
        break;
    case CELL_DIGITAL:
        cell->cellHandle = Digital_InitCell(cellNumber, CellQueues[cellNumber]);
        break;
    default:
        // Panic
    }
    return CellQueues[cellNumber];
}

void DigitalReferenceCalibrate(CalParameters_t *calParams)
{
    DigitalOxygenState_t *refCell = NULL;
    uint8_t refCellIndex = 0;
    // Select the first digital cell
    for (uint8_t i = 0; i < CELL_COUNT; ++i)
    {
        if ((CELL_DIGITAL == cells[i].type) && (NULL == refCell))
        {
            refCell = (DigitalOxygenState_t *)cells[i].cellHandle;
            refCellIndex = i;
        }
    }

    if (refCell != NULL)
    {
        uint16_t pressure = refCell->pressure / 1000;
        OxygenCell_t refCellData = {0};
        xQueuePeek(CellQueues[refCellIndex], &refCellData, 100);
        PPO2_t ppO2 = refCellData.ppo2;
        serial_printf("!!CAL PPO2: %d, %d", ppO2, pressure);
        // Now that we have the PPO2 we cal all the analog cells
        ShortMillivolts_t cellVals[CELL_COUNT] = {0};
        for (uint8_t i = 0; i < CELL_COUNT; ++i)
        {
            if (CELL_ANALOG == cells[i].type)
            {
                AnalogOxygenState_t *analogCell = (AnalogOxygenState_t *)cells[i].cellHandle;
                cellVals[i] = Calibrate(analogCell, ppO2);
            }
        }

        // Now that calibration is done lets grab the millivolts for the record
        calParams->cell1 = cellVals[0];
        calParams->cell2 = cellVals[1];
        calParams->cell3 = cellVals[2];
        calParams->pressure_val = pressure;
        calParams->fO2 = ((float)ppO2 * (1000.0f / (float)pressure));
    }
    else
    {
        serial_printf("Cannot find digital cell to cal");
        // Panic
    }
}

void CalibrationTask(void *arg)
{
    CalParameters_t *calParams = (CalParameters_t *)arg;

    switch (calParams->calMethod)
    {
    case CAL_DIGITAL_REFERENCE: // Calibrate using the solid state cell as a reference
        DigitalReferenceCalibrate(calParams);
        osDelay(1000); // Give the shearwater time to catch up
        break;
    default:
        NonFatalError(UNDEFINED_CAL_METHOD);
        osThreadExit();
    }
    serial_printf("TX cal response");
    txCalResponse(calParams->deviceType, calParams->cell1, calParams->cell2, calParams->cell3, calParams->fO2, calParams->pressure_val);
    osThreadExit();
}
osThreadId_t calTask;
void RunCalibrationTask(DiveCANType_t deviceType, const FO2_t in_fO2, const uint16_t in_pressure_val)
{
    glob_calParams.fO2 = in_fO2;
    glob_calParams.pressure_val = in_pressure_val;
    glob_calParams.deviceType = deviceType;
    glob_calParams.cell1 = 0;
    glob_calParams.cell2 = 0;
    glob_calParams.cell3 = 0;

    glob_calParams.calMethod = CAL_DIGITAL_REFERENCE;

    txCalAck(deviceType);

    // Don't start the thread if we're already calibrating, shearwater double shots us sometimes
    if ((osThreadGetState(calTask) == osThreadError) ||
        (osThreadGetState(calTask) == osThreadInactive) ||
        (osThreadGetState(calTask) == osThreadTerminated))
    {
        calTask = osThreadNew(CalibrationTask, &glob_calParams, &CalTask_attributes);
    }
}
