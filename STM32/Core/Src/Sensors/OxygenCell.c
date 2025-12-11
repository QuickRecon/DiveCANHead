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
#include "../Hardware/solenoid.h"
#include "../Hardware/flash.h"

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

/**
 * @brief Force a cell to re-read its calibration data from EEPROM
 * @param cell handle of the cell to refresh
 */
void RefreshCalibrationData(OxygenHandle_t *cell)
{
    switch (cell->type)
    {
    case CELL_ANALOG:
        AnalogReadCalibration((AnalogOxygenState_t *)cell->cellHandle);
        break;
    case CELL_DIVEO2:
        DiveO2ReadCalibration((DiveO2State_t *)cell->cellHandle);
        break;
    case CELL_O2S:
        O2SReadCalibration((OxygenScientificState_t *)cell->cellHandle);
        break;
    default:
        NON_FATAL_ERROR(UNREACHABLE_ERR);
    }
}
#pragma endregion
#pragma region Calibration

/**
 * @brief Calibrate all of the analog cells based on the controller provided data
 * @param calParams Struct containing the FO2 and atmospheric pressure, gets populated with cell millis and error messages
 * @return Calibration status
 */
DiveCANCalResponse_t AnalogReferenceCalibrate(CalParameters_t *calParams)
{
    DiveCANCalResponse_t calPass = DIVECAN_CAL_RESULT_OK;
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
            cellVals[i] = AnalogCalibrate((AnalogOxygenState_t *)cell, ppO2, &(calErrors[i]));
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
 * @see CalParameters_t, DiveO2State_t, OxygenHandle_t, CELL_COUNT, DIVECAN_CAL_RESULT_OK, DIVECAN_CAL_FAIL_GEN, DIVECAN_CAL_FAIL_REJECTED, TIMEOUT_100MS, NONE_ERR, Numeric_t, FO2_t, CELL_DIVEO2, CELL_ANALOG
 */
DiveCANCalResponse_t DigitalReferenceCalibrate(CalParameters_t *calParams)
{
    DiveCANCalResponse_t calPass = DIVECAN_CAL_RESULT_OK;
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
                cellVals[i] = AnalogCalibrate((AnalogOxygenState_t *)(cell->cellHandle), ppO2, &(calErrors[i]));
                /* Check the cell calibrated properly, if it still says needs cal it was outside the cal envelope */
                if (((AnalogOxygenState_t *)(cell->cellHandle))->status == CELL_NEED_CAL)
                {
                    calPass = DIVECAN_CAL_FAIL_FO2_RANGE;
                }

                /* A fail state means some kind of internal fault during cal */
                if (((AnalogOxygenState_t *)(cell->cellHandle))->status == CELL_FAIL)
                {
                    calPass = DIVECAN_CAL_FAIL_GEN;
                }
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

DiveCANCalResponse_t TotalAbsoluteCalibrate(CalParameters_t *calParams)
{
    DiveCANCalResponse_t calPass = DIVECAN_CAL_RESULT_OK;
    PPO2_t ppO2 = (calParams->fO2 * calParams->pressureVal) / 1000;

    /* Now that we have the PPO2 we cal all the analog cells
     */
    ShortMillivolts_t cellVals[CELL_COUNT] = {0};
    NonFatalError_t calErrors[CELL_COUNT] = {NONE_ERR, NONE_ERR, NONE_ERR};

    serial_printf("Using PPO2 %u for cal\r\n", ppO2);

    for (uint8_t i = 0; i < CELL_COUNT; ++i)
    {
        OxygenHandle_t *cell = getCell(i);
        switch (cell->type)
        {
        case CELL_DIVEO2:
            cellVals[i] = DiveO2Calibrate((DiveO2State_t *)(cell->cellHandle), ppO2, &(calErrors[i]));
            /* Check the cell calibrated properly, if it still says needs cal it was outside the cal envelope */
            if (((DiveO2State_t *)(cell->cellHandle))->status == CELL_NEED_CAL)
            {
                calPass = DIVECAN_CAL_FAIL_FO2_RANGE;
            }

            /* A fail state means some kind of internal fault during cal */
            if (((DiveO2State_t *)(cell->cellHandle))->status == CELL_FAIL)
            {
                calPass = DIVECAN_CAL_FAIL_GEN;
            }
            break;
        case CELL_ANALOG:
            cellVals[i] = AnalogCalibrate((AnalogOxygenState_t *)(cell->cellHandle), ppO2, &(calErrors[i]));
            /* Check the cell calibrated properly, if it still says needs cal it was outside the cal envelope */
            if (((AnalogOxygenState_t *)(cell->cellHandle))->status == CELL_NEED_CAL)
            {
                calPass = DIVECAN_CAL_FAIL_FO2_RANGE;
            }

            /* A fail state means some kind of internal fault during cal */
            if (((AnalogOxygenState_t *)(cell->cellHandle))->status == CELL_FAIL)
            {
                calPass = DIVECAN_CAL_FAIL_GEN;
            }
            break;
        case CELL_O2S:
            cellVals[i] = O2SCalibrate((OxygenScientificState_t *)(cell->cellHandle), ppO2, &(calErrors[i]));
            /* Check the cell calibrated properly, if it still says needs cal it was outside the cal envelope */
            if (((OxygenScientificState_t *)(cell->cellHandle))->status == CELL_NEED_CAL)
            {
                calPass = DIVECAN_CAL_FAIL_FO2_RANGE;
            }

            /* A fail state means some kind of internal fault during cal */
            if (((OxygenScientificState_t *)(cell->cellHandle))->status == CELL_FAIL)
            {
                calPass = DIVECAN_CAL_FAIL_GEN;
            }
            break;
        default:
            NON_FATAL_ERROR(UNREACHABLE_ERR);
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
 * @brief We flush the loop with oxygen for 15 seconds and then perform a total absolute calibration
 * @param calParams pointer to the CalParameters struct where the calibration results will be stored.
 * @return DiveCANCalResponse_t - Indicates the success or failure of the calibration process.
 */
DiveCANCalResponse_t SolenoidFlushCalibrate(CalParameters_t *calParams)
{
    /*Do the O2 flush*/
    const uint8_t flushTimeSeconds = 25;

    for (uint8_t i = 0; i < flushTimeSeconds; ++i)
    {
        setSolenoidOn(calParams->powerMode);
        (void)osDelay(TIMEOUT_1S_TICKS);
    }
    setSolenoidOff();

    return TotalAbsoluteCalibrate(calParams);
}

/**
 * @brief This task handles the calibration process of the device by checking the calibration method used and then calling the appropriate function accordingly. The available calibration methods are CAL_DIGITAL_REFERENCE, CAL_ANALOG_ABSOLUTE, and CAL_TOTAL_ABSOLUTE.
 * @param arg A pointer to the CalParameters_t struct which contains all necessary parameters for the calibration process.
 */
void CalibrationTask(void *arg)
{
    CalParameters_t calParams = *((CalParameters_t *)arg);
    DiveCANCalResponse_t calResult = DIVECAN_CAL_FAIL_REJECTED;

    /* Store the current flash values so we can undo a cal if we need to */
    CalCoeff_t previousCalibs[CELL_COUNT] = {0};
    for (uint8_t i = 0; i < CELL_COUNT; ++i)
    {
        bool calOk = GetCalibration(i, &(previousCalibs[i]));
        if (!calOk)
        {
            NON_FATAL_ERROR_DETAIL(EEPROM_ERR, i);
        }
    }

    /* Do the calibration */
    serial_printf("Starting calibrate with method %u\r\n", calParams.calMethod);
    switch (calParams.calMethod)
    {
    case CAL_DIGITAL_REFERENCE:          /* Calibrate using the solid state cell as a reference */
        (void)osDelay(TIMEOUT_4S_TICKS); /* Give the shearwater time to catch up */
        calResult = DigitalReferenceCalibrate(&calParams);
        break;
    case CAL_ANALOG_ABSOLUTE:
        (void)osDelay(TIMEOUT_4S_TICKS);
        calResult = AnalogReferenceCalibrate(&calParams);
        break;
    case CAL_TOTAL_ABSOLUTE:
        (void)osDelay(TIMEOUT_4S_TICKS);
        calResult = TotalAbsoluteCalibrate(&calParams);
        break;
    case CAL_SOLENOID_FLUSH:
        calResult = SolenoidFlushCalibrate(&calParams);
        break;
    default:
        calResult = DIVECAN_CAL_FAIL_REJECTED;
        NON_FATAL_ERROR(UNDEFINED_CAL_METHOD_ERR);
    }

    serial_printf("Sending cal response %d\r\n", calResult);

    if (calResult != DIVECAN_CAL_RESULT_OK)
    {
        /* The cal failed, we need to restore the previous cal values */
        for (uint8_t i = 0; i < CELL_COUNT; ++i)
        {
            bool calOk = SetCalibration(i, previousCalibs[i]);
            if (!calOk)
            {
                NON_FATAL_ERROR_DETAIL(EEPROM_ERR, i);
            }
            else
            {
                serial_printf("Restored cal for cell %d to %f\r\n", i, previousCalibs[i]);
                RefreshCalibrationData(getCell(i));
            }
        }
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

/**
 * @brief Start a new task (one off) to execute a calibration, this will silently fail if a calibration is already being done (why are you trying to calibrate while you calibrate?)
 * @param deviceType DiveCAN device to send responses from
 * @param in_fO2 FO2 reported to us to use in the calibration
 * @param in_pressure_val ambient pressure to use in the calibration (millibar)
 * @param calMethod Calibration method to use for calibration
 */
void RunCalibrationTask(DiveCANType_t deviceType, const FO2_t in_fO2, const uint16_t in_pressure_val, OxygenCalMethod_t calMethod, PowerSelectMode_t powerMode)
{
    static CalParameters_t calParams;

    calParams.fO2 = in_fO2;
    calParams.pressureVal = in_pressure_val;
    calParams.deviceType = deviceType;
    calParams.cell1 = 0;
    calParams.cell2 = 0;
    calParams.cell3 = 0;

    calParams.calMethod = calMethod;
    calParams.powerMode = powerMode;

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

Consensus_t TwoCellConsensus(Consensus_t consensus)
{
    /* Find the two values that we're including*/
    PIDNumeric_t included_values[2] = {0};
    uint8_t idx = 0;
    for (uint8_t cellIdx = 0; cellIdx < CELL_COUNT; ++cellIdx)
    {
        if (consensus.includeArray[cellIdx])
        {
            assert(idx < 2);
            included_values[idx] = consensus.precisionPPO2Array[cellIdx];
            ++idx;
        }
    }

    /* Check to see if they pass the sniff check */
    if ((fabs(included_values[0] - included_values[1]) * 100.0f) > MAX_DEVIATION)
    {
        /* Both cells are too far apart, vote them all out */
        consensus.includeArray[CELL_1] = false;
        consensus.includeArray[CELL_2] = false;
        consensus.includeArray[CELL_3] = false;
    }
    else
    {
        /* Get our average */
        PIDNumeric_t average = ((included_values[0] + included_values[1]) / 2.0f) * 100.0f;
        consensus.consensus = (PPO2_t)(average);
        assert(consensus.consensus < 255);
        consensus.precisionConsensus = average / 100.0f;
    }
    return consensus;
}

Consensus_t ThreeCellConsensus(Consensus_t consensus)
{
    const PIDNumeric_t pairwise_differences[3] = {
        fabs(consensus.precisionPPO2Array[0] - consensus.precisionPPO2Array[1]),
        fabs(consensus.precisionPPO2Array[0] - consensus.precisionPPO2Array[2]),
        fabs(consensus.precisionPPO2Array[1] - consensus.precisionPPO2Array[2])};

    const PIDNumeric_t pairwise_averages[3] = {
        (consensus.precisionPPO2Array[0] + consensus.precisionPPO2Array[1]) / 2.0f,
        (consensus.precisionPPO2Array[0] + consensus.precisionPPO2Array[2]) / 2.0f,
        (consensus.precisionPPO2Array[1] + consensus.precisionPPO2Array[2]) / 2.0f};

    const uint8_t remainder_cell[] = {2, 1, 0}; /* The cell that is not in the pairwise comparison */

    /* Find the minimum value and its index */
    PIDNumeric_t min_difference = pairwise_differences[0];
    uint8_t min_index = 0;
    for (uint8_t i = 0; i < (sizeof(pairwise_differences) / sizeof(pairwise_differences[0])); ++i)
    {
        if (pairwise_differences[i] < min_difference)
        {
            min_difference = pairwise_differences[i];
            min_index = i;
        }
    }

    /* Ensure that these values are within our maximum deviation, if they're too far apart
     * flag them all as failed but carry forward to get a number so we still have a guess to fly off */
    if ((min_difference * 100.0f) > MAX_DEVIATION)
    {
        /* All cells are too far apart, vote them all out */
        consensus.includeArray[CELL_1] = false;
        consensus.includeArray[CELL_2] = false;
        consensus.includeArray[CELL_3] = false;
    }
    /* Check the remainder cell against the average of the 2 */
    else if ((fabs(consensus.precisionPPO2Array[remainder_cell[min_index]] - pairwise_averages[min_index]) * 100.0f) > MAX_DEVIATION)
    {
        /* Vote out the remainder cell */
        consensus.includeArray[remainder_cell[min_index]] = false;
        PIDNumeric_t total_average = pairwise_averages[min_index] * 100.0f;
        consensus.consensus = (PPO2_t)(total_average);
        assert(consensus.consensus < 255);
        consensus.precisionConsensus = pairwise_averages[min_index];
    }
    else
    {
        /* All 3 cells are within range, use all 3 */
        PIDNumeric_t total_average = ((consensus.precisionPPO2Array[0] + consensus.precisionPPO2Array[1] + consensus.precisionPPO2Array[2]) / 3.0f) * 100.0f;
        consensus.consensus = (PPO2_t)(total_average);
        assert(consensus.consensus < 255);
        consensus.precisionConsensus = total_average / 100.0f;
    }
    return consensus;
}

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

    const Timestamp_t timeout = TIMEOUT_10S_TICKS; /* 10 second timeout to avoid stale data, this is almost exclusively for the O2S cell path */
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
        .consensus = PPO2_FAIL,
        .precisionConsensus = (PIDNumeric_t)PPO2_FAIL,
        .includeArray = {true, true, true}};

    /* Do a two pass check, loop through the cells and average the "good" cells
     * Then afterwards we have a few different processes depending on how many "good" cells we have:
        0 good cells: set consensus to 0xFF so we fail safe and don't fire the solenoid
        1 good cell: use that cell but vote it out so we get a vote fail alarm
        2 good cells: ensure they are within the MAX_DEVIATION, if so average them, otherwise vote both out
        3 good cells: do a pairwise comparison to find the closest two, average those, then check to see if the remaining cell is within MAX_DEVIATION of that average,
                      if so use all three, otherwise vote out the remaining cell
     */
    uint8_t includedCellCount = 0;

    for (uint8_t cellIdx = 0; cellIdx < CELL_COUNT; ++cellIdx)
    {
        /* If the PPO2 is invalid (zero or max) we vote the cell out, likewise if the cell status is failed or the cell is timed out it doesn't get included */
        if ((consensus.ppo2Array[cellIdx] == PPO2_FAIL) || (consensus.ppo2Array[cellIdx] == 0))
        {
            consensus.includeArray[cellIdx] = false;
        }
        else if ((consensus.statusArray[cellIdx] == CELL_NEED_CAL) ||
                 (consensus.statusArray[cellIdx] == CELL_FAIL) ||
                 (consensus.statusArray[cellIdx] == CELL_DEGRADED) ||
                 ((now - sampleTimes[cellIdx]) > timeout))
        {
            consensus.includeArray[cellIdx] = false;
        }
        else
        {
            ++includedCellCount;
        }
    }

    /* In the case of no included cells, just set the consensus to FF, which will inhibit the solenoid from firing*/
    if (includedCellCount == 0)
    {
        /* Do nothing as the consensus is FF by the initializer*/
    }
    else if (includedCellCount == 1) /* If we only have one cell, just use that cell's value, but vote it out so we still get a vote fail alarm (because we haven't actually voted) */
    {
        for (uint8_t cellIdx = 0; cellIdx < CELL_COUNT; ++cellIdx)
        {
            if (consensus.includeArray[cellIdx])
            {
                consensus.consensus = consensus.ppo2Array[cellIdx];
                consensus.precisionConsensus = consensus.precisionPPO2Array[cellIdx];
                consensus.includeArray[cellIdx] = false; /* Vote it out so we get a vote fail alarm */
            }
        }
    }
    else if (includedCellCount == 2) /* If we have 2 cells, ensure they are within the MAX_DEVIATION (otherwise alarm)*/
    {
        consensus = TwoCellConsensus(consensus);
    }
    else
    { /* All 3 cells were valid, do a pairwise compare to find the closest two*/
        consensus = ThreeCellConsensus(consensus);
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
