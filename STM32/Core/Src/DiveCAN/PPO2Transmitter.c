#include "PPO2Transmitter.h"
#include "cmsis_os.h"
#include "main.h"

static const uint8_t CELL_1 = 0;
static const uint8_t CELL_2 = 1;
static const uint8_t CELL_3 = 2;

static const uint8_t MAX_DEVIATION = 15; // Max allowable deviation is 0.15 bar PPO2

#define PPO2TXTASK_STACK_SIZE 400 // 264 bytes by static analysis

typedef struct PPO2TXTask_params_s
{
    DiveCANDevice_t *device;
    QueueHandle_t c1;
    QueueHandle_t c2;
    QueueHandle_t c3;
} PPO2TXTask_params_t;

extern void serial_printf(const char *fmt, ...);

// Forward decls of local funcs
// /void configureADC(void *arg);
void PPO2TXTask(void *arg);
Consensus_t calculateConsensus(const OxygenCell_t *const c1, const OxygenCell_t *const c2, const OxygenCell_t *const c3);
void setFailedCellsValues(Consensus_t *consensus);

// FreeRTOS tasks
static uint32_t PPO2TXTask_buffer[PPO2TXTASK_STACK_SIZE];
static StaticTask_t PPO2TXTask_ControlBlock;
const osThreadAttr_t PPO2TXTask_attributes = {
    .name = "PPO2TXTask",
    .cb_mem = &PPO2TXTask_ControlBlock,
    .cb_size = sizeof(PPO2TXTask_ControlBlock),
    .stack_mem = &PPO2TXTask_buffer[0],
    .stack_size = sizeof(PPO2TXTask_buffer),
    .priority = (osPriority_t)CAN_PPO2_TX_PRIORITY};

osThreadId_t PPO2TXTaskHandle;
PPO2TXTask_params_t taskParams;

void InitPPO2TX(DiveCANDevice_t *device, QueueHandle_t c1, QueueHandle_t c2, QueueHandle_t c3)
{
    PPO2TXTask_params_t params = {
        .device = device,
        .c1 = c1,
        .c2 = c2,
        .c3 = c3};
    taskParams = params;
    PPO2TXTaskHandle = osThreadNew(PPO2TXTask, &taskParams, &PPO2TXTask_attributes);
}

/// @brief The job of this task is to ingest the cell data passed to it via queues, calculate their consensus,
///        then transmit this data via the CAN transiever to any other devices on the CAN network.
/// @param arg Pointer to PPO2TXTask_params_t*, which has the cells and our device spec for the can bus
void PPO2TXTask(void *arg)
{
    PPO2TXTask_params_t *params = (PPO2TXTask_params_t *)arg;
    const DiveCANDevice_t * const dev = params->device;
    uint32_t i = 0;
    do
    {
        ++i;
        osDelay(500);

        // TODO: catch timing out here
        OxygenCell_t c1 = {0};
        xQueuePeek(params->c1, &c1, 100);
        OxygenCell_t c2 = {0};
        xQueuePeek(params->c2, &c2, 100);
        OxygenCell_t c3 = {0};
        xQueuePeek(params->c3, &c3, 100);

        // First we calculate the consensus struct, which includes the voting logic
        // This is aware of cell status but does not set the PPO2 data for Fail states
        Consensus_t consensus = calculateConsensus(&c1, &c2, &c3);

        // Go through each cell and if any need cal, flag cal
        // Also check for fail and mark the cell value as fail
        setFailedCellsValues(&consensus);

        txPPO2(dev->type, consensus.PPO2s[CELL_1], consensus.PPO2s[CELL_2], consensus.PPO2s[CELL_3]);
        txMillivolts(dev->type, consensus.millis[CELL_1], consensus.millis[CELL_2], consensus.millis[CELL_3]);
        txCellState(dev->type, consensus.included[CELL_1], consensus.included[CELL_2], consensus.included[CELL_3], consensus.consensus);

        // serial_printf("%d; C1: (%d, %d, %d); C2: (%d, %d, %d); C3: (%d, %d, %d), Consensus: %d\r\n", i,
        //               consensus.PPO2s[CELL_1], consensus.millis[CELL_1], consensus.included[CELL_1],
        //               consensus.PPO2s[CELL_2], consensus.millis[CELL_2], consensus.included[CELL_2],
        //               consensus.PPO2s[CELL_3], consensus.millis[CELL_3], consensus.included[CELL_3],
        //               consensus.consensus);
    } while (RTOS_LOOP_FOREVER);
}

/// @brief Update the provided consensus object based on the cell states so that not-ok cells are FFed
///        Will also FF the whole set if we're wanting a calibration
/// @param consensus the consensus struct to update
void setFailedCellsValues(Consensus_t *consensus)
{
    // First check if we need to go into "needs cal" state
    if ((consensus->statuses[0] == CELL_NEED_CAL) ||
        (consensus->statuses[1] == CELL_NEED_CAL) ||
        (consensus->statuses[2] == CELL_NEED_CAL))
    {
        for (uint8_t i = 0; i < CELL_COUNT; ++i)
        {
            consensus->PPO2s[i] = PPO2_FAIL;
        }
    }
    else // Otherwise just FF as needed
    {
        for (uint8_t i = 0; i < CELL_COUNT; ++i)
        {
            if (consensus->statuses[i] == CELL_FAIL)
            {
                consensus->PPO2s[i] = PPO2_FAIL;
            }
        }
    }
}

/// @brief Calculate the consensus PPO2, cell state aware but does not set the PPO2 to fail value for failed cells
///        In an all fail scenario we want that data to still be intact so we can still have our best guess
/// @param c1
/// @param c2
/// @param c3
/// @return
Consensus_t calculateConsensus(const OxygenCell_t *const c1, const OxygenCell_t *const c2, const OxygenCell_t *const c3)
{
    // Zeroth step, load up the millis, status and PPO2
    // We also load up the timestamps of each cell sample so that we can check the other tasks
    // haven't been sitting idle and starved us of information
    Timestamp_t sampleTimes[CELL_COUNT] = {
        c1->data_time,
        c2->data_time,
        c3->data_time};

    const Timestamp_t timeout = 1000; // 1000 millisecond timeout to avoid stale data
    Timestamp_t now = HAL_GetTick();

    Consensus_t consensus = {
        .statuses = {
            c1->status,
            c2->status,
            c3->status,
        },
        .PPO2s = {
            c1->ppo2,
            c2->ppo2,
            c3->ppo2,
        },
        .millis = {
            c1->millivolts,
            c2->millivolts,
            c3->millivolts,
        },
        .consensus = 0,
        .included = {true, true, true}};

    // Do a two pass check, loop through the cells and average the "good" cells
    // Then afterwards we check each cells value against the average, and exclude deviations
    uint16_t PPO2_acc = 0; // Start an accumulator to take an average, include the median cell always
    uint8_t includedCellCount = 0;

    for (uint8_t cellIdx = 0; cellIdx < CELL_COUNT; ++cellIdx)
    {
        if ((consensus.statuses[cellIdx] == CELL_NEED_CAL) ||
            (consensus.statuses[cellIdx] == CELL_FAIL) ||
            ((now - sampleTimes[cellIdx]) > timeout))
        {
            consensus.included[cellIdx] = false;
        }
        else
        {
            PPO2_acc += consensus.PPO2s[cellIdx];
            ++includedCellCount;
        }
    }

    // Assert that we actually have cells that got included
    if (includedCellCount > 0)
    {
        // Now second pass, check to see if any of the included cells are deviant from the average
        for (uint8_t cellIdx = 0; cellIdx < CELL_COUNT; ++cellIdx)
        {
            if (consensus.included[cellIdx] && (abs((PPO2_t)(PPO2_acc / includedCellCount) - consensus.PPO2s[cellIdx]) > MAX_DEVIATION)) // We want to make sure the cell is actually included before we start checking it
            {
                // Removing cells in this way can result in a change in the outcome depending on
                // cell position, depending on exactly how split-brained the cells are, but
                // frankly if things are that cooked then we're borderline guessing anyway
                PPO2_acc -= consensus.PPO2s[cellIdx];
                --includedCellCount;
                consensus.included[cellIdx] = false;
            }
        }
    }

    if (includedCellCount > 0)
    {
        consensus.consensus = (PPO2_t)(PPO2_acc / includedCellCount);
    }

    return consensus;
}
