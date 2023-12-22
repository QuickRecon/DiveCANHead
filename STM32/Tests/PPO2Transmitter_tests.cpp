#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#include "OxygenCell.h"
#include "PPO2Transmitter.h"
extern "C"
{
    extern Consensus_t calculateConsensus(OxygenCell_t *c1, OxygenCell_t *c2, OxygenCell_t *c3);

    uint32_t HAL_GetTick(void)
    {
        mock().actualCall("HAL_GetTick");
        return 0;
    }

    BaseType_t xQueuePeek(QueueHandle_t xQueue, void *const pvBuffer, TickType_t xTicksToWait)
    {
        mock().actualCall("xQueuePeek");
        return 0;
    }

    void txPPO2(const DiveCANType_t deviceType, const PPO2_t cell1, const PPO2_t cell2, const PPO2_t cell3)
    {
        mock().actualCall("txPPO2");
    }

    void txMillivolts(const DiveCANType_t deviceType, const Millivolts_t cell1, const Millivolts_t cell2, const Millivolts_t cell3)
    {
        mock().actualCall("txMillivolts");
    }

    void txCellState(const DiveCANType_t deviceType, const bool cell1, const bool cell2, const bool cell3, const PPO2_t PPO2)
    {
        mock().actualCall("txCellState");
    }

    osThreadId_t osThreadNew(osThreadFunc_t func, void *argument, const osThreadAttr_t *attr)
    {
        mock().actualCall("osThreadNew");
        return NULL;
    }

    osStatus_t osDelay(uint32_t ticks)
    {
        mock().actualCall("osDelay");
        return osOK;
    }
}

TEST_GROUP(PPO2Transmitter){
    void setup(){
        // Init stuff
    }

    void teardown(){
        mock().clear();
}
}
;

TEST(PPO2Transmitter, calculateConsensus_AveragesCells)
{
    OxygenCell_t c1 = {
        .cellNumber = 0,
        .type = CELL_ANALOG,
        .ppo2 = 110,
        .millivolts = 0,
        .status = CELL_OK,
        .data_time = 0};
    OxygenCell_t c2 = {
        .cellNumber = 1,
        .type = CELL_ANALOG,
        .ppo2 = 120,
        .millivolts = 0,
        .status = CELL_OK,
        .data_time = 0};
    OxygenCell_t c3 = {
        .cellNumber = 2,
        .type = CELL_ANALOG,
        .ppo2 = 100,
        .millivolts = 0,
        .status = CELL_OK,
        .data_time = 0};

    Consensus_t consensus;

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c1, &c2, &c3);
    CHECK(consensus.consensus == 110);

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c2, &c1, &c3);
    CHECK(consensus.consensus == 110);

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c3, &c2, &c1);
    CHECK(consensus.consensus == 110);

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c1, &c3, &c2);
    CHECK(consensus.consensus == 110);
}

TEST(PPO2Transmitter, calculateConsensus_ExcludesHigh)
{
    OxygenCell_t c1 = {
        .cellNumber = 0,
        .type = CELL_ANALOG,
        .ppo2 = 110,
        .millivolts = 0,
        .status = CELL_OK,
        .data_time = 0};
    OxygenCell_t c2 = { // CELL 2 should be excluded
        .cellNumber = 1,
        .type = CELL_ANALOG,
        .ppo2 = 130,
        .millivolts = 0,
        .status = CELL_OK,
        .data_time = 0};
    OxygenCell_t c3 = {
        .cellNumber = 2,
        .type = CELL_ANALOG,
        .ppo2 = 100,
        .millivolts = 0,
        .status = CELL_OK,
        .data_time = 0};

    Consensus_t consensus;

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c1, &c2, &c3);
    CHECK(consensus.included[1] == false);
    CHECK(consensus.consensus == 105);

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c2, &c1, &c3);
    CHECK(consensus.included[0] == false);
    CHECK(consensus.consensus == 105);

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c3, &c2, &c1);
    CHECK(consensus.included[1] == false);
    CHECK(consensus.consensus == 105);

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c1, &c3, &c2);
    CHECK(consensus.included[2] == false);
    CHECK(consensus.consensus == 105);
}

TEST(PPO2Transmitter, calculateConsensus_ExcludesLow)
{
    OxygenCell_t c1 = {
        .cellNumber = 0,
        .type = CELL_ANALOG,
        .ppo2 = 120,
        .millivolts = 0,
        .status = CELL_OK,
        .data_time = 0};
    OxygenCell_t c2 = {
        .cellNumber = 1,
        .type = CELL_ANALOG,
        .ppo2 = 130,
        .millivolts = 0,
        .status = CELL_OK,
        .data_time = 0};
    OxygenCell_t c3 = {
        .cellNumber = 2,
        .type = CELL_ANALOG,
        .ppo2 = 100,
        .millivolts = 0,
        .status = CELL_OK,
        .data_time = 0};

    Consensus_t consensus;

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c1, &c2, &c3);
    CHECK(consensus.included[2] == false);
    CHECK(consensus.consensus == 125);

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c2, &c1, &c3);
    CHECK(consensus.included[2] == false);
    CHECK(consensus.consensus == 125);

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c3, &c2, &c1);
    CHECK(consensus.included[0] == false);
    CHECK(consensus.consensus == 125);

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c1, &c3, &c2);
    CHECK(consensus.included[1] == false);
    CHECK(consensus.consensus == 125);
}

TEST(PPO2Transmitter, calculateConsensus_ExcludesTimedOutCell)
{
    OxygenCell_t c1 = {
        .cellNumber = 0,
        .type = CELL_ANALOG,
        .ppo2 = 120,
        .millivolts = 0,
        .status = CELL_OK,
        .data_time = 1500};
    OxygenCell_t c2 = {
        .cellNumber = 1,
        .type = CELL_ANALOG,
        .ppo2 = 110,
        .millivolts = 0,
        .status = CELL_OK,
        .data_time = 0};
    OxygenCell_t c3 = {
        .cellNumber = 2,
        .type = CELL_ANALOG,
        .ppo2 = 100,
        .millivolts = 0,
        .status = CELL_OK,
        .data_time = 0};

    Consensus_t consensus;

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c1, &c2, &c3);
    CHECK(consensus.included[0] == false);
    CHECK(consensus.consensus == 105);

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c2, &c1, &c3);
    CHECK(consensus.included[1] == false);
    CHECK(consensus.consensus == 105);

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c3, &c2, &c1);
    CHECK(consensus.included[2] == false);
    CHECK(consensus.consensus == 105);

    mock().expectOneCall("HAL_GetTick");
    consensus = calculateConsensus(&c1, &c3, &c2);
    CHECK(consensus.included[0] == false);
    CHECK(consensus.consensus == 105);
}