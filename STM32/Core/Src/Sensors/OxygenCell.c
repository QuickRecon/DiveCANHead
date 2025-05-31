/** \file OxygenCell.c
 *  \author Aren Leishman
 *  \brief This is a generic oxygen cell used by both analog and digital cells
 *         as a common calling convention.
 */

#include "OxygenCell.h"
#include <math.h>
#include "AnalogOxygen.h"
#include "DiveO2.h"
#include "OxygenScientific.h"
#include "eeprom_emul.h"
#include "../errors.h"
#include "../Hardware/printer.h"
#include "assert.h"

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
        NON_FATAL_ERROR_DETAIL(INVALID_CELL_NUMBER_ERR, cellNum);
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
        NON_FATAL_ERROR_DETAIL(INVALID_CELL_NUMBER_ERR, cellNum);
        cellHandle = &(cells[0]); /* A safe fallback */
    }
    else
    {
        cellHandle = &(cells[cellNum]);
    }
    return cellHandle;
}

#pragma region Initialisation
/**
 * @brief Initializes and creates a new cell with the given cell number and cell type.
 *
 * @param[in] cellNumber The number of the cell to be initialized.
 * @param[in] type The type of cell to be created (analog or digital).
 * @return The handle to the newly created cell.
 */
QueueHandle_t CreateCell(uint8_t cellNumber, CellType_t type)
{
    /* This is only called at startup, so halt and catch fire is appropriate */
    assert(cellNumber < 3);
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
    case CELL_DIVEO2:
        cell->cellHandle = DiveO2_InitCell(cell, *queueHandle);
        break;
    case CELL_O2S:
        cell->cellHandle = O2S_InitCell(cell, *queueHandle);
        break;
    default:
        NON_FATAL_ERROR(UNREACHABLE_ERR);
    }
    return *queueHandle;
}
#pragma endregion
#pragma region Calibration

/**
 * @brief Calibrate a given analog cell
 * @param calPass Pointer to cal response variable
 * @param i Cell index
 * @param cell Pointer to oxygen cell handle
 * @param ppO2 Calibration PPO2
 * @param cellVals Response variable containing the millivolts of the calibration (i indexed)
 * @param calErrors Response variable containing any calibration errors (i indexed)
 */
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

/**
 * @brief Calibrate all of the analog cells based on the controller provided data
 * @param calParams Struct containing the FO2 and atmospheric pressure, gets populated with cell millis and error messages
 * @return Calibration status
 */
DiveCANCalResponse_t AnalogReferenceCalibrate(CalParameters_t *calParams)
{
    DiveCANCalResponse_t calPass = DIVECAN_CAL_RESULT;
    PPO2_t ppO2 = (calParams->fO2 * calParams->pressureVal) / 1000;

    /* Now that we have the PPO2 we cal all the analog cells
     */
    ShortMillivolts_t cellVals[CELL_COUNT] = {0};
    NonFatalError_t calErrors[CELL_COUNT] = {NONE_ERR, NONE_ERR, NONE_ERR};

    serial_printf("Using PPO2 %u for cal\r\n", ppO2);

    for (uint8_t i = 0; i < CELL_COUNT; ++i)
    {
        OxygenHandle_t *cell = getCell(i);
        if (CELL_ANALOG == cell->type)
        {
            calibrateAnalogCell(&calPass, i, cell, ppO2, cellVals, calErrors);
        }

        if (calErrors[i] != NONE_ERR)
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
 * @see CalParameters_t, DiveO2State_t, OxygenHandle_t, CELL_COUNT, DIVECAN_CAL_RESULT, DIVECAN_CAL_FAIL_GEN, DIVECAN_CAL_FAIL_REJECTED, TIMEOUT_100MS, NONE_ERR, Numeric_t, FO2_t, CELL_DIVEO2, CELL_ANALOG
 */
DiveCANCalResponse_t DigitalReferenceCalibrate(CalParameters_t *calParams)
{
    DiveCANCalResponse_t calPass = DIVECAN_CAL_RESULT;
    const DiveO2State_t *refCell = NULL;
    uint8_t refCellIndex = 0;
    /* Select the first digital cell */
    for (uint8_t i = 0; i < CELL_COUNT; ++i)
    {
        const OxygenHandle_t *const cell = getCell(i);
        if ((CELL_DIVEO2 == cell->type) && (NULL == refCell))
        {
            refCell = (const DiveO2State_t *)cell->cellHandle;
            refCellIndex = i;
        }
    }

    QueueHandle_t *queueHandle = getQueueHandle(refCellIndex);

    OxygenCell_t refCellData = {0};
    BaseType_t peekStatus = xQueuePeek(*queueHandle, &refCellData, TIMEOUT_100MS_TICKS);
    if ((refCell != NULL) && (pdTRUE == peekStatus) && (refCellData.status == CELL_OK))
    {
        PPO2_t ppO2 = refCellData.ppo2;
        uint16_t pressure = (uint16_t)(refCell->pressure / 1000);

        /* Now that we have the PPO2 we cal all the analog cells
         */
        ShortMillivolts_t cellVals[CELL_COUNT] = {0};
        NonFatalError_t calErrors[CELL_COUNT] = {NONE_ERR, NONE_ERR, NONE_ERR};

        for (uint8_t i = 0; i < CELL_COUNT; ++i)
        {
            OxygenHandle_t *cell = getCell(i);
            if (CELL_ANALOG == cell->type)
            {
                calibrateAnalogCell(&calPass, i, cell, ppO2, cellVals, calErrors);
            }

            if (calErrors[i] != NONE_ERR)
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
        NON_FATAL_ERROR(CAL_METHOD_ERR);
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
    case CAL_DIGITAL_REFERENCE:          /* Calibrate using the solid state cell as a reference */
        (void)osDelay(TIMEOUT_4s_TICKS); /* Give the shearwater time to catch up */
        calResult = DigitalReferenceCalibrate(&calParams);
        break;
    case CAL_ANALOG_ABSOLUTE:
        (void)osDelay(TIMEOUT_4s_TICKS);
        calResult = AnalogReferenceCalibrate(&calParams);
        break;
    case CAL_TOTAL_ABSOLUTE:
        calResult = DIVECAN_CAL_FAIL_REJECTED;
        NON_FATAL_ERROR(UNDEFINED_CAL_METHOD_ERR);
        break;
    default:
        calResult = DIVECAN_CAL_FAIL_REJECTED;
        NON_FATAL_ERROR(UNDEFINED_CAL_METHOD_ERR);
    }

    serial_printf("Sending cal response %d\r\n", calResult);
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

/**
 * @brief Start a new task (one off) to execute a calibration, this will silently fail if a calibration is already being done (why are you trying to calibrate while you calibrate?)
 * @param deviceType DiveCAN device to send responses from
 * @param in_fO2 FO2 reported to us to use in the calibration
 * @param in_pressure_val ambient pressure to use in the calibration (millibar)
 * @param calMethod Calibration method to use for calibration
 */
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
        static uint8_t CalTask_buffer[CALTASK_STACK_SIZE];
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

#pragma endregion

#pragma region Consensus

/**
 * @brief Peek the given cell queue handles and calculate the consensus, cells that do not respond within 100ms are marked as failed.
 * @param cell1 Cell 1 queue handle
 * @param cell2 Cell 2 queue handle
 * @param cell3 Cell 3 queue handle
 * @return `Consenus_t` as calculated by `calculateConsensus` using the latest cell values
 */
Consensus_t peekCellConsensus(QueueHandle_t cell1, QueueHandle_t cell2, QueueHandle_t cell3)
{
    /* First retreive the cell data */
    OxygenCell_t c1 = {0};
    bool c1pick = xQueuePeek(cell1, &c1, TIMEOUT_100MS_TICKS);
    OxygenCell_t c2 = {0};
    bool c2pick = xQueuePeek(cell2, &c2, TIMEOUT_100MS_TICKS);
    OxygenCell_t c3 = {0};
    bool c3pick = xQueuePeek(cell3, &c3, TIMEOUT_100MS_TICKS);

    /* If the peek timed out then we mark the cell as failed going into the consensus calculation
     and lodge the nonfatal error */
    if (!c1pick)
    {
        c1.status = CELL_FAIL;
        NON_FATAL_ERROR(TIMEOUT_ERR);
    }
    if (!c2pick)
    {
        c2.status = CELL_FAIL;
        NON_FATAL_ERROR(TIMEOUT_ERR);
    }
    if (!c3pick)
    {
        c3.status = CELL_FAIL;
        NON_FATAL_ERROR(TIMEOUT_ERR);
    }

    /* We calculate the consensus ourselves so we can make interpretations based on the cell confidence*/
    return calculateConsensus(&c1, &c2, &c3);
}

static const uint8_t MAX_DEVIATION = 15; /* Max allowable deviation is 0.15 bar PPO2 */

/** @brief Calculate the consensus PPO2, cell state aware but does not set the PPO2 to fail value for failed cells
 *        In an all fail scenario we want that data to still be intact so we can still have our best guess
 * @param c1
 * @param c2
 * @param c3
 * @return
 */
Consensus_t calculateConsensus(const OxygenCell_t *const c1, const OxygenCell_t *const c2, const OxygenCell_t *const c3)
{
    /* Zeroth step, load up the millis, status and PPO2
     * We also load up the timestamps of each cell sample so that we can check the other tasks
     * haven't been sitting idle and starved us of information
     */
    Timestamp_t sampleTimes[CELL_COUNT] = {
        c1->dataTime,
        c2->dataTime,
        c3->dataTime};

    const Timestamp_t timeout = TIMEOUT_4s_TICKS; /* 4000 millisecond timeout to avoid stale data */
    Timestamp_t now = HAL_GetTick();

    Consensus_t consensus = {
        .statusArray = {
            c1->status,
            c2->status,
            c3->status,
        },
        .ppo2Array = {
            c1->ppo2,
            c2->ppo2,
            c3->ppo2,
        },
        .precisionPPO2Array = {
            c1->precisionPPO2,
            c2->precisionPPO2,
            c3->precisionPPO2,
        },
        .milliArray = {
            c1->millivolts,
            c2->millivolts,
            c3->millivolts,
        },
        .consensus = 0,
        .precisionConsensus = 0,
        .includeArray = {true, true, true}};

    /* Do a two pass check, loop through the cells and average the "good" cells
     * Then afterwards we check each cells value against the average, and exclude deviations
     */
    PIDNumeric_t PPO2_acc = 0; /* Start an accumulator to take an average, include the median cell always */
    uint8_t includedCellCount = 0;

    for (uint8_t cellIdx = 0; cellIdx < CELL_COUNT; ++cellIdx)
    {
        if ((consensus.statusArray[cellIdx] == CELL_NEED_CAL) ||
            (consensus.statusArray[cellIdx] == CELL_FAIL) ||
            (consensus.statusArray[cellIdx] == CELL_DEGRADED) ||
            ((now - sampleTimes[cellIdx]) > timeout))
        {
            consensus.includeArray[cellIdx] = false;
        }
        else
        {
            PPO2_acc += consensus.precisionPPO2Array[cellIdx];
            ++includedCellCount;
        }
    }

    /* Assert that we actually have cells that got included */
    if (includedCellCount > 0)
    {
        /* Now second pass, check to see if any of the included cells are deviant from the average */
        for (uint8_t cellIdx = 0; cellIdx < CELL_COUNT; ++cellIdx)
        {
            /* We want to make sure the cell is actually included before we start checking it */
            if ((includedCellCount > 0) &&
                (consensus.includeArray[cellIdx]) &&
                ((fabs((PPO2_acc / (PIDNumeric_t)includedCellCount) - consensus.precisionPPO2Array[cellIdx]) * 100.0f) > MAX_DEVIATION))
            {
                /* Removing cells in this way can result in a change in the outcome depending on
                 * cell position, depending on exactly how split-brained the cells are, but
                 * frankly if things are that cooked then we're borderline guessing anyway
                 */
                PPO2_acc -= consensus.precisionPPO2Array[cellIdx];
                --includedCellCount;
                consensus.includeArray[cellIdx] = false;
            }
        }
    }

    if (includedCellCount > 0)
    {
        consensus.precisionConsensus = (PPO2_acc / (PIDNumeric_t)includedCellCount);
        PIDNumeric_t tempConsensus = consensus.precisionConsensus * 100.0f;
        assert(tempConsensus < 255.0f);
        consensus.consensus = (PPO2_t)(tempConsensus);
    }

    return consensus;
}

/**
 * @brief Calculate the cell confidence out of 3, 3 means 3 voted-in cells, 2 means 2 voted-in cells, etc
 * @param consensus Consensus struct calculated from `calculateConsensus`
 * @return Cell confidence out of 3
 */
uint8_t cellConfidence(const Consensus_t *const consensus)
{
    uint8_t confidence = 0;
    for (uint8_t i = 0; i < CELL_COUNT; ++i)
    {
        if (consensus->includeArray[i])
        {
            ++confidence;
        }
    }
    return confidence;
}
#pragma endregion
