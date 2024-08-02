/** \file OxygenCell.c
 *  \author Aren Leishman
 *  \brief This is a generic oxygen cell used by both analog and digital cells
 *         as a common calling convention.
 */

#include "OxygenCell.h"
#include "AnalogOxygen.h"
#include "DigitalOxygen.h"
#include "eeprom_emul.h"
#include "../errors.h"
#include <math.h>
#include "../Hardware/printer.h"
#include "assert.h"

/** @struct CalParameters_s
 *  @brief Contains calibration parameters for an oxygen sensor.
 *
 *  The `deviceType` field specifies the type of device being used, while the
 *  `fO2`, `pressureVal`, and `calMethod` fields specify the FO2 value,
 *  pressure value, and calibration method to be used. The remaining fields
 *  are for individual cell parameters.
 */
typedef struct
{
    DiveCANType_t deviceType;
    FO2_t fO2;
    uint16_t pressureVal;

    ShortMillivolts_t cell1;
    ShortMillivolts_t cell2;
    ShortMillivolts_t cell3;

    OxygenCalMethod_t calMethod;
} CalParameters_t;

/** @fn getQueueHandle
 *  @brief Returns a pointer to the queue handle for a given oxygen cell.
 *
 *  The `cellNum` parameter specifies which cell to retrieve the queue handle
 *  for. If `cellNum` is invalid, an error message will be logged and a safe
 *  fallback queue handle will be returned.
 *
 *  @param[in] cellNum Index of the oxygen cell to retrieve the queue handle for.
 *  @return Pointer to the queue handle for the specified oxygen cell.
 */
static QueueHandle_t *getQueueHandle(uint8_t cellNum)
{
    static QueueHandle_t cellQueues[CELL_COUNT];
    QueueHandle_t *queueHandle = NULL;
    if (cellNum >= CELL_COUNT)
    {
        NON_FATAL_ERROR(INVALID_CELL_NUMBER);
        queueHandle = &(cellQueues[0]); /* A safe fallback */
    }
    else
    {
        queueHandle = &(cellQueues[cellNum]);
    }
    return queueHandle;
}

/** @fn getCell
 *  @brief Returns a pointer to the oxygen cell handle for a given oxygen cell.
 *
 *  The `cellNum` parameter specifies which cell to retrieve the cell handle for.
 *  If `cellNum` is invalid, an error message will be logged and a safe fallback
 *  cell handle will be returned.
 *
 *  @param[in] cellNum Index of the oxygen cell to retrieve the cell handle for.
 *  @return Pointer to the oxygen cell handle for the specified oxygen cell.
 */
static OxygenHandle_t *getCell(uint8_t cellNum)
{
    static OxygenHandle_t cells[CELL_COUNT];
    OxygenHandle_t *cellHandle = NULL;
    if (cellNum >= CELL_COUNT)
    {
        NON_FATAL_ERROR(INVALID_CELL_NUMBER);
        cellHandle = &(cells[0]); /* A safe fallback */
    }
    else
    {
        cellHandle = &(cells[cellNum]);
    }
    return cellHandle;
}

/**
 * @brief Initializes and creates a new cell with the given cell number and cell type.
 *
 * @param[in] cellNumber The number of the cell to be initialized.
 * @param[in] type The type of cell to be created (analog or digital).
 * @return The handle to the newly created cell.
 */
QueueHandle_t CreateCell(uint8_t cellNumber, CellType_t type)
{
    assert(cellNumber < 3); // This is only called at startup, so halt and catch fire is appropriate
    OxygenHandle_t *cell = getCell(cellNumber);
    cell->cellNumber = cellNumber;

    static StaticQueue_t CellQueues_QueueStruct[CELL_COUNT];
    static uint8_t CellQueues_Storage[CELL_COUNT][sizeof(OxygenCell_t)];
    QueueHandle_t *queueHandle = getQueueHandle(cellNumber);
    *queueHandle = xQueueCreateStatic(1, sizeof(OxygenCell_t), CellQueues_Storage[cellNumber], &(CellQueues_QueueStruct[cellNumber]));

    cell->type = type;
    switch (type)
    {
    case CELL_ANALOG:
        cell->cellHandle = Analog_InitCell(cell, *queueHandle);
        break;
    case CELL_DIGITAL:
        cell->cellHandle = Digital_InitCell(cell, *queueHandle);
        break;
    default:
        NON_FATAL_ERROR(UNREACHABLE_ERROR);
    }
    return *queueHandle;
}

static void calibrateAnalogCell(DiveCANCalResponse_t *calPass, uint8_t i, OxygenHandle_t *cell, PPO2_t ppO2, ShortMillivolts_t *cellVals, NonFatalError_t *calErrors)
{
    AnalogOxygenState_t *analogCell = (AnalogOxygenState_t *)cell->cellHandle;
    cellVals[i] = Calibrate(analogCell, ppO2, &(calErrors[i]));

    /* Check the cell calibrated properly, if it still says needs cal it was outside the cal envelope */
    if (analogCell->status == CELL_NEED_CAL)
    {
        *calPass = DIVECAN_CAL_FAIL_FO2_RANGE;
    }

    /* A fail state means some kind of internal fault during cal */
    if (analogCell->status == CELL_FAIL)
    {
        *calPass = DIVECAN_CAL_FAIL_GEN;
    }
}

DiveCANCalResponse_t AnalogReferenceCalibrate(CalParameters_t *calParams)
{
    DiveCANCalResponse_t calPass = DIVECAN_CAL_RESULT;
    PPO2_t ppO2 = (calParams->fO2 * calParams->pressureVal)/1000;

    /* Now that we have the PPO2 we cal all the analog cells
     */
    ShortMillivolts_t cellVals[CELL_COUNT] = {0};
    NonFatalError_t calErrors[CELL_COUNT] = {ERR_NONE, ERR_NONE, ERR_NONE};

    serial_printf("Using PPO2 %u for cal\r\n", ppO2);

    for (uint8_t i = 0; i < CELL_COUNT; ++i)
    {
        OxygenHandle_t *cell = getCell(i);
        if (CELL_ANALOG == cell->type)
        {
            calibrateAnalogCell(&calPass, i, cell, ppO2, cellVals, calErrors);
        }

        if (calErrors[i] != ERR_NONE)
        {
            calPass = DIVECAN_CAL_FAIL_GEN;
        }
    }

    /* Now that calibration is done lets grab the millivolts for the record */
    calParams->cell1 = cellVals[CELL_1];
    calParams->cell2 = cellVals[CELL_2];
    calParams->cell3 = cellVals[CELL_3];

    return calPass;
}

/**
 * @brief Calibrates the Oxygen sensor using a digital reference cell.
 *
 * This function searches for a digital reference cell and uses it to calibrate all the analog cells.
 * The calibration parameters are then stored in the `CalParameters_t` struct.
 *
 * @param calParams Pointer to the CalParameters struct where the calibration results will be stored.
 * @return DiveCANCalResponse_t - Indicates the success or failure of the calibration process.
 * @see CalParameters_t, DigitalOxygenState_t, OxygenHandle_t, CELL_COUNT, DIVECAN_CAL_RESULT, DIVECAN_CAL_FAIL_GEN, DIVECAN_CAL_FAIL_REJECTED, TIMEOUT_100MS, ERR_NONE, Numeric_t, FO2_t, CELL_DIGITAL, CELL_ANALOG
 */
DiveCANCalResponse_t DigitalReferenceCalibrate(CalParameters_t *calParams)
{
    DiveCANCalResponse_t calPass = DIVECAN_CAL_RESULT;
    const DigitalOxygenState_t *refCell = NULL;
    uint8_t refCellIndex = 0;
    /* Select the first digital cell */
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
    if ((refCell != NULL) && (pdTRUE == xQueuePeek(*queueHandle, &refCellData, TIMEOUT_100MS_TICKS)) && (refCellData.status == CELL_OK))
    {
        PPO2_t ppO2 = refCellData.ppo2;
        uint16_t pressure = (uint16_t)(refCell->pressure / 1000);

        /* Now that we have the PPO2 we cal all the analog cells
         */
        ShortMillivolts_t cellVals[CELL_COUNT] = {0};
        NonFatalError_t calErrors[CELL_COUNT] = {ERR_NONE, ERR_NONE, ERR_NONE};

        for (uint8_t i = 0; i < CELL_COUNT; ++i)
        {
            OxygenHandle_t *cell = getCell(i);
            if (CELL_ANALOG == cell->type)
            {
                calibrateAnalogCell(&calPass, i, cell, ppO2, cellVals, calErrors);
            }

            if (calErrors[i] != ERR_NONE)
            {
                calPass = DIVECAN_CAL_FAIL_GEN;
            }
        }

        /* Now that calibration is done lets grab the millivolts for the record */
        calParams->cell1 = cellVals[CELL_1];
        calParams->cell2 = cellVals[CELL_2];
        calParams->cell3 = cellVals[CELL_3];

        calParams->pressureVal = pressure;
        calParams->fO2 = (FO2_t)round((Numeric_t)ppO2 * (1000.0f / (Numeric_t)pressure));
    }
    else
    {
        /* We can't find a digital cell to cal with */
        NON_FATAL_ERROR(CAL_METHOD_ERROR);
        calPass = DIVECAN_CAL_FAIL_REJECTED;
    }
    return calPass;
}

/**
 * @brief This task handles the calibration process of the device by checking the calibration method used and then calling the appropriate function accordingly. The available calibration methods are CAL_DIGITAL_REFERENCE, CAL_ANALOG_ABSOLUTE, and CAL_TOTAL_ABSOLUTE.
 * @param arg A pointer to the CalParameters_t struct which contains all necessary parameters for the calibration process.
 */
void CalibrationTask(void *arg)
{
    CalParameters_t calParams = *((CalParameters_t *)arg);
    DiveCANCalResponse_t calResult = DIVECAN_CAL_FAIL_REJECTED;
    serial_printf("Starting calibrate with method %u\r\n", calParams.calMethod);
    switch (calParams.calMethod)
    {
    case CAL_DIGITAL_REFERENCE:    /* Calibrate using the solid state cell as a reference */
        (void)osDelay(TIMEOUT_4s); /* Give the shearwater time to catch up */
        calResult = DigitalReferenceCalibrate(&calParams);
        break;
    case CAL_ANALOG_ABSOLUTE:
        (void)osDelay(TIMEOUT_4s);
        calResult = AnalogReferenceCalibrate(&calParams);
        break;
    case CAL_TOTAL_ABSOLUTE:
        calResult = DIVECAN_CAL_FAIL_REJECTED;
        NON_FATAL_ERROR(UNDEFINED_CAL_METHOD);
        break;
    default:
        calResult = DIVECAN_CAL_FAIL_REJECTED;
        NON_FATAL_ERROR(UNDEFINED_CAL_METHOD);
    }

    txCalResponse(calParams.deviceType, calResult, calParams.cell1, calParams.cell2, calParams.cell3, calParams.fO2, calParams.pressureVal);

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

void RunCalibrationTask(DiveCANType_t deviceType, const FO2_t in_fO2, const uint16_t in_pressure_val, OxygenCalMethod_t calMethod)
{
    static CalParameters_t calParams;

    calParams.fO2 = in_fO2;
    calParams.pressureVal = in_pressure_val;
    calParams.deviceType = deviceType;
    calParams.cell1 = 0;
    calParams.cell2 = 0;
    calParams.cell3 = 0;

    calParams.calMethod = calMethod;

    txCalAck(deviceType);

    /*
     Don't start the thread if we're already calibrating, shearwater double shots us sometimes */
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
