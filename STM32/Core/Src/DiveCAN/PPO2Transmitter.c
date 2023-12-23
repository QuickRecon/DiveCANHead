#include "PPO2Transmitter.h"
#include "cmsis_os.h"
#include "main.h"

static const uint8_t CELL_1 = 0;
static const uint8_t CELL_2 = 1;
static const uint8_t CELL_3 = 2;

static const uint8_t MAX_DEVIATION = 15; // Max allowable deviation is 0.15 bar PPO2

#define PPO2TXTASK_STACK_SIZE 400 // 264 bytes by static analysis
typedef struct cellValueContainer_s
{
    uint8_t cellNumber;
    PPO2_t PPO2;
} cellValueContainer_t;

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
Consensus_t calculateConsensus(OxygenCell_t *c1, OxygenCell_t *c2, OxygenCell_t *c3);

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

void PPO2TXTask(void *arg)
{
    PPO2TXTask_params_t *params = (PPO2TXTask_params_t *)arg;
    DiveCANDevice_t *dev = params->device;
    int i = 0;
    while (true)
    {

        ++i;
        osDelay(500);

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

        txPPO2(dev->type, consensus.PPO2s[CELL_1], consensus.PPO2s[CELL_2], consensus.PPO2s[CELL_3]);
        txMillivolts(dev->type, consensus.millis[CELL_1], consensus.millis[CELL_2], consensus.millis[CELL_3]);
        txCellState(dev->type, consensus.included[CELL_1], consensus.included[CELL_2], consensus.included[CELL_3], consensus.consensus);

        // serial_printf("%d; C1: (%d, %d, %d); C2: (%d, %d, %d); C3: (%d, %d, %d), Consensus: %d\r\n", i,
        //               consensus.PPO2s[CELL_1], consensus.millis[CELL_1], consensus.included[CELL_1],
        //               consensus.PPO2s[CELL_2], consensus.millis[CELL_2], consensus.included[CELL_2],
        //               consensus.PPO2s[CELL_3], consensus.millis[CELL_3], consensus.included[CELL_3],
        //               consensus.consensus);
    }
}

int CelValComp(const void *num1, const void *num2) // comparing function
{
    const cellValueContainer_t a = *(const cellValueContainer_t *)num1;
    const cellValueContainer_t b = *(const cellValueContainer_t *)num2;
    int ret = 0;
    if (a.PPO2 > b.PPO2)
    {
        ret = 1;
    }
    else if (a.PPO2 < b.PPO2)
    {
        ret = -1;
    }
    else
    {
        ret = 0;
    }
    return ret;
}

/// @brief Calculate the consensus PPO2, cell state aware but does not set the PPO2 to fail value for failed cells
///        In an all fail scenario we want that data to still be intact so we can still have our best guess
/// @param c1
/// @param c2
/// @param c3
/// @return
Consensus_t calculateConsensus(OxygenCell_t *c1, OxygenCell_t *c2, OxygenCell_t *c3)
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

    // Now for the vote itself, the logic here is to first sort the cells
    // by PPO2 and check if the max and min are more than MAX_DEVIATION apart
    cellValueContainer_t sortList[3] = {{CELL_1, consensus.PPO2s[CELL_1]},
                                        {CELL_2, consensus.PPO2s[CELL_2]},
                                        {CELL_3, consensus.PPO2s[CELL_3]}};

    // Do a non-recursive inplace sort
    uint8_t maxIdx = 0;
    uint8_t size = 3;
    for (uint8_t i = 0; i < (size - 1); ++i)
    {
        // Find the maximum element in
        // unsorted array
        maxIdx = i;
        for (uint8_t j = i + 1; j < size; ++j)
        {
            if (sortList[j].PPO2 > sortList[maxIdx].PPO2)
            {
                maxIdx = j;
            }
        }
        // Swap the found minimum element
        // with the first element
        cellValueContainer_t temp = sortList[maxIdx];
        sortList[maxIdx] = sortList[i];
        sortList[i] = temp;
    }

    // Now check the upper and lower cells, and start a tally of the included cells
    uint16_t PPO2_acc = 0; // Start an accumulator to take an average, include the median cell always
    uint8_t includedCellCount = 0;

    if ((consensus.statuses[sortList[CELL_2].cellNumber] == CELL_NEED_CAL) ||
        (consensus.statuses[sortList[CELL_2].cellNumber] == CELL_FAIL) ||
        ((now - sampleTimes[sortList[CELL_2].cellNumber]) > timeout))
    {
        consensus.included[sortList[CELL_2].cellNumber] = false;
        // TODO: panic because the one cell that we were hoping would be good is not good
    } else {
        PPO2_acc = sortList[CELL_2].PPO2;
        ++includedCellCount;
    }
    // Lower cell
    // If we're outside the deviation then mark it in the mask
    // but if we're within it then add it to our average
    if (((sortList[CELL_1].PPO2 - sortList[CELL_2].PPO2) > MAX_DEVIATION) ||
        (consensus.statuses[sortList[CELL_1].cellNumber] == CELL_NEED_CAL) ||
        (consensus.statuses[sortList[CELL_1].cellNumber] == CELL_FAIL) ||
        ((now - sampleTimes[sortList[CELL_1].cellNumber]) > timeout))
    {
        consensus.included[sortList[CELL_1].cellNumber] = false;
    }
    else
    {
        PPO2_acc += sortList[CELL_1].PPO2;
        ++includedCellCount;
    }

    // Upper cell
    if (((sortList[CELL_2].PPO2 - sortList[CELL_3].PPO2) > MAX_DEVIATION) ||
        (consensus.statuses[sortList[CELL_3].cellNumber] == CELL_NEED_CAL) ||
        (consensus.statuses[sortList[CELL_3].cellNumber] == CELL_FAIL) ||
        ((now - sampleTimes[sortList[CELL_3].cellNumber]) > timeout))
    {
        consensus.included[sortList[CELL_3].cellNumber] = false;
    }
    else
    {
        PPO2_acc += sortList[CELL_3].PPO2;
        ++includedCellCount;
    }

    consensus.consensus = (PPO2_t)(PPO2_acc / includedCellCount);

    return consensus;
}
