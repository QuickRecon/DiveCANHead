#include "PPO2Transmitter.h"
#include "cmsis_os.h"
#include "main.h"
#include "../errors.h"
#include "../Hardware/printer.h"
#include "../PPO2Control/PPO2Control.h"

typedef struct
{
    DiveCANDevice_t * device;
    QueueHandle_t c1;
    QueueHandle_t c2;
    QueueHandle_t c3;
} PPO2TXTask_params_t;

/* Forward decls of local funcs */
void PPO2TXTask(void *arg);
Consensus_t calculateConsensus(const OxygenCell_t *const c1, const OxygenCell_t *const c2, const OxygenCell_t *const c3);
void setFailedCellsValues(Consensus_t *consensus);

static osThreadId_t *getOSThreadId(void)
{
    static osThreadId_t PPO2TXTaskHandle;
    return &PPO2TXTaskHandle;
}

/**
 * @brief Initializes PPO2 TX Task with specified parameters.
 * @param device Pointer to DiveCAN device struct
 * @param c1 QueueHandle_t for cell queue 1
 * @param c2 QueueHandle_t for cell queue 2
 * @param c3 QueueHandle_t for cell queue 3
 */
void InitPPO2TX(const DiveCANDevice_t * const device, QueueHandle_t c1, QueueHandle_t c2, QueueHandle_t c3)
{
    /* Need to init the struct locally then value copy into the static */
    static PPO2TXTask_params_t taskParams;
    static DiveCANDevice_t taskDiveCANDevice;

    taskDiveCANDevice = *device;

    PPO2TXTask_params_t params = {
        .device = &taskDiveCANDevice,
        .c1 = c1,
        .c2 = c2,
        .c3 = c3};

    taskParams = params;

    static uint32_t PPO2TXTask_buffer[PPO2TXTASK_STACK_SIZE];
    static StaticTask_t PPO2TXTask_ControlBlock;
    static const osThreadAttr_t PPO2TXTask_attributes = {
        .name = "PPO2TXTask",
        .attr_bits = osThreadDetached,
        .cb_mem = &PPO2TXTask_ControlBlock,
        .cb_size = sizeof(PPO2TXTask_ControlBlock),
        .stack_mem = &PPO2TXTask_buffer[0],
        .stack_size = sizeof(PPO2TXTask_buffer),
        .priority = CAN_PPO2_TX_PRIORITY,
        .tz_module = 0,
        .reserved = 0};

    osThreadId_t *PPO2TXTaskHandle = getOSThreadId();
    *PPO2TXTaskHandle = osThreadNew(PPO2TXTask, &taskParams, &PPO2TXTask_attributes);
}

/**
 * @brief The job of this task is to ingest the cell data passed to it via queues, calculate their consensus,
 *        then transmit this data via the CAN transiever to any other devices on the CAN network.
 * @param arg Pointer to PPO2TXTask_params_t*, which has the cells and our device spec for the can bus
 */
void PPO2TXTask(void *arg)
{
    PPO2TXTask_params_t *params = (PPO2TXTask_params_t *)arg;
    const DiveCANDevice_t *const dev = params->device;
    uint32_t i = 0;
    do
    {
        ++i;
        (void)osDelay(TIMEOUT_100MS);

        OxygenCell_t c1 = {0};
        bool c1pick = xQueuePeek(params->c1, &c1, TIMEOUT_100MS_TICKS);
        OxygenCell_t c2 = {0};
        bool c2pick = xQueuePeek(params->c2, &c2, TIMEOUT_100MS_TICKS);
        OxygenCell_t c3 = {0};
        bool c3pick = xQueuePeek(params->c3, &c3, TIMEOUT_100MS_TICKS);

        /* If the peek timed out then we mark the cell as failed going into the consensus calculation
         and lodge the nonfatal error */
        if (!c1pick)
        {
            c1.status = CELL_FAIL;
            NON_FATAL_ERROR(TIMEOUT_ERROR);
        }
        if (!c2pick)
        {
            c2.status = CELL_FAIL;
            NON_FATAL_ERROR(TIMEOUT_ERROR);
        }
        if (!c3pick)
        {
            c3.status = CELL_FAIL;
            NON_FATAL_ERROR(TIMEOUT_ERROR);
        }
        /* First we calculate the consensus struct, which includes the voting logic
         This is aware of cell status but does not set the PPO2 data for Fail states */
        Consensus_t consensus = calculateConsensus(&c1, &c2, &c3);

        /* Go through each cell and if any need cal, flag cal
         Also check for fail and mark the cell value as fail */
        setFailedCellsValues(&consensus);

        txPPO2(dev->type, consensus.ppo2Array[CELL_1], consensus.ppo2Array[CELL_2], consensus.ppo2Array[CELL_3]);
        txMillivolts(dev->type, consensus.milliArray[CELL_1], consensus.milliArray[CELL_2], consensus.milliArray[CELL_3]);
        txCellState(dev->type, consensus.includeArray[CELL_1], consensus.includeArray[CELL_2], consensus.includeArray[CELL_3], consensus.consensus);

    } while (RTOS_LOOP_FOREVER);
}

/** @brief Update the provided consensus object based on the cell states so that not-ok cells are FFed
 *        Will also FF the whole set if we're wanting a calibration
 * @param consensus the consensus struct to update
 */
void setFailedCellsValues(Consensus_t *consensus)
{
    /* First check if we need to go into "needs cal" state */
    bool needsCal = (consensus->statusArray[0] == CELL_NEED_CAL) ||
                    (consensus->statusArray[1] == CELL_NEED_CAL) ||
                    (consensus->statusArray[2] == CELL_NEED_CAL);

    if (needsCal && (!isCalibrating()))
    {
        for (uint8_t i = 0; i < CELL_COUNT; ++i)
        {
            consensus->ppo2Array[i] = PPO2_FAIL;
        }
    }
    else /* Otherwise just FF as needed */
    {
        for (uint8_t i = 0; i < CELL_COUNT; ++i)
        {
            if (consensus->statusArray[i] == CELL_FAIL)
            {
                consensus->ppo2Array[i] = PPO2_FAIL;
            }
        }
    }
}
