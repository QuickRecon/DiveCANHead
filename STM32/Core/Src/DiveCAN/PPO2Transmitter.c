#include "PPO2Transmitter.h"
#include "cmsis_os.h"
#include "main.h"

static const uint8_t CELL_1 = 0;
static const uint8_t CELL_2 = 1;
static const uint8_t CELL_3 = 2;

static const uint8_t MAX_DEVIATION = 15; // Max allowable deviation is 0.15 bar PPO2

#define PPO2TXTASK_STACK_SIZE 200

extern IWDG_HandleTypeDef hiwdg;
typedef struct cellValueContainer_s
{
    uint8_t cellNumber;
    PPO2_t PPO2;
} cellValueContainer_t;

typedef struct PPO2TXTask_params_s
{
    DiveCANDevice_t* device;
    OxygenCell_t *c1;
    OxygenCell_t *c2;
    OxygenCell_t *c3;
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
    .priority = (osPriority_t)osPriorityNormal};

osThreadId_t PPO2TXTaskHandle;
PPO2TXTask_params_t taskParams;

void InitPPO2TX(DiveCANDevice_t* device, OxygenCell_t *c1, OxygenCell_t *c2, OxygenCell_t *c3)
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
    DiveCANDevice_t* dev = params->device;
    OxygenCell_t *c1 = params->c1;
    OxygenCell_t *c2 = params->c2;
    OxygenCell_t *c3 = params->c3;

    int i = 0;
    while (true)
    {
        ++i;
        HAL_IWDG_Refresh(&hiwdg); // PPO2 sampling is the critical loop
        osDelay(500);

        Consensus_t consensus = calculateConsensus(c1, c2, c3);

        txPPO2(dev->type, consensus.PPO2s[CELL_1], consensus.PPO2s[CELL_2], consensus.PPO2s[CELL_3]);
        txMillivolts(dev->type, consensus.millis[CELL_1], consensus.millis[CELL_2], consensus.millis[CELL_3]);
        txCellState(dev->type, consensus.included[CELL_1], consensus.included[CELL_2], consensus.included[CELL_3], consensus.consensus);

        serial_printf("%d; C1: (%d, %d, %d); C2: (%d, %d, %d); C3: (%d, %d, %d), Consensus: %d\r\n", i,
                      consensus.PPO2s[CELL_1], consensus.millis[CELL_1], consensus.included[CELL_1],
                      consensus.PPO2s[CELL_2], consensus.millis[CELL_2], consensus.included[CELL_2],
                      consensus.PPO2s[CELL_3], consensus.millis[CELL_3], consensus.included[CELL_3],
                      consensus.consensus);
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

// Ok kids this is the big one, cell voting logic
// If the shearwater starts doing bad things it probably
// happened in here, everything else is just dragging this logic
// into the real world.
Consensus_t calculateConsensus(OxygenCell_t *c1, OxygenCell_t *c2, OxygenCell_t *c3)
{
    // Zeroth step, load up the millis, status and PPO2
    Consensus_t consensus = {
        .statuses = {
            c1->status(c1),
            c2->status(c2),
            c3->status(c3),
        },
        .PPO2s = {
            c1->ppo2(c1),
            c2->ppo2(c2),
            c3->ppo2(c3),
        },
        .millis = {
            c1->millivolts(c1),
            c2->millivolts(c2),
            c3->millivolts(c3),
        },
        .consensus = 0,
        .included = {true, true, true}
    };

    // First check if any of the cells are begging for cal,
    // because that takes precidence over the rest of this logic
    if ((consensus.statuses[CELL_1] == CELL_NEED_CAL) ||
        (consensus.statuses[CELL_2] == CELL_NEED_CAL) ||
        (consensus.statuses[CELL_3] == CELL_NEED_CAL))
    {
        serial_printf("Needs cal\r\n");
        for (uint8_t i = 0; i < 3; ++i)
        {
            consensus.PPO2s[i] = PPO2_FAIL; // Every cell reads as failed, prompts needs cal warning on the shearwater
        }
    }
    else
    {
        // Now for the vote itself, the logic here is to first sort the cells
        // by PPO2 and check if the max and min are more than MAX_DEVIATION apart
        cellValueContainer_t sortList[3] = {{CELL_1, consensus.PPO2s[CELL_1]},
                                            {CELL_2, consensus.PPO2s[CELL_2]},
                                            {CELL_3, consensus.PPO2s[CELL_3]}};

        qsort(sortList, 3, sizeof(cellValueContainer_t), CelValComp);

        // Now check the upper and lower cells, and start a tally of the included cells
        uint16_t PPO2_acc = sortList[CELL_2].PPO2; // Start an accumulator to take an average, include the median cell always
        uint8_t includedCellCount = 1;

        // Lower cell
        // If we're outside the deviation then mark it in the mask
        // but if we're within it then add it to our average
        if ((sortList[CELL_2].PPO2 - sortList[CELL_1].PPO2) > MAX_DEVIATION)
        {
            consensus.included[sortList[CELL_1].cellNumber] = false;
        }
        else
        {
            consensus.included[sortList[CELL_1].cellNumber] = true;
            PPO2_acc += sortList[CELL_1].PPO2;
            ++includedCellCount;
        }

        // Upper cell
        if ((sortList[CELL_3].PPO2 - sortList[CELL_2].PPO2) > MAX_DEVIATION)
        {
            consensus.included[sortList[CELL_3].cellNumber] = false;
        }
        else
        {
            consensus.included[sortList[CELL_3].cellNumber] = true;
            PPO2_acc += sortList[CELL_3].PPO2;
            ++includedCellCount;
        }

        consensus.consensus = (PPO2_t)(PPO2_acc / includedCellCount);
    }
    return consensus;
}
