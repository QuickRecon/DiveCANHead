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

// Run through each permutation of cell positions and check
// the consensus against the expected value
void checkConsensus(Consensus_t expectedConsensus, OxygenCell_t *c1, OxygenCell_t *c2, OxygenCell_t *c3)
{
    Consensus_t consensus;

    OxygenCell_t *cells[3] = {c1, c2, c3};

    uint8_t permutations[6][3] = {
        {0, 1, 2},
        {0, 2, 1},
        {1, 0, 2},
        {1, 2, 0},
        {2, 0, 1},
        {2, 1, 0}};

    for (uint8_t i = 0; i < 6; ++i)
    {
        uint8_t POSITION_1 = permutations[i][0];
        uint8_t POSITION_2 = permutations[i][1];
        uint8_t POSITION_3 = permutations[i][2];

        mock().expectOneCall("HAL_GetTick");
        consensus = calculateConsensus(cells[POSITION_1], cells[POSITION_2], cells[POSITION_3]);

        // Verify our structure populates correctly
        CHECK(consensus.statuses[0] == expectedConsensus.statuses[POSITION_1]);
        CHECK(consensus.statuses[1] == expectedConsensus.statuses[POSITION_2]);
        CHECK(consensus.statuses[2] == expectedConsensus.statuses[POSITION_3]);
        CHECK(consensus.PPO2s[0] == expectedConsensus.PPO2s[POSITION_1]);
        CHECK(consensus.PPO2s[1] == expectedConsensus.PPO2s[POSITION_2]);
        CHECK(consensus.PPO2s[2] == expectedConsensus.PPO2s[POSITION_3]);
        CHECK(consensus.millis[0] == expectedConsensus.millis[POSITION_1]);
        CHECK(consensus.millis[1] == expectedConsensus.millis[POSITION_2]);
        CHECK(consensus.millis[2] == expectedConsensus.millis[POSITION_3]);
        CHECK(consensus.included[0] == expectedConsensus.included[POSITION_1]);
        CHECK(consensus.included[1] == expectedConsensus.included[POSITION_2]);
        CHECK(consensus.included[2] == expectedConsensus.included[POSITION_3]);
        CHECK(consensus.consensus == expectedConsensus.consensus);
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
        .millivolts = 12,
        .status = CELL_OK,
        .data_time = 0};
    OxygenCell_t c2 = {
        .cellNumber = 1,
        .type = CELL_ANALOG,
        .ppo2 = 120,
        .millivolts = 13,
        .status = CELL_OK,
        .data_time = 0};
    OxygenCell_t c3 = {
        .cellNumber = 2,
        .type = CELL_ANALOG,
        .ppo2 = 100,
        .millivolts = 14,
        .status = CELL_OK,
        .data_time = 0};

    Consensus_t expectedConsensus = {
        .statuses = {CELL_OK, CELL_OK, CELL_OK},
        .PPO2s = {110, 120, 100},
        .millis = {12, 13, 14},
        .consensus = 110,
        .included = {true, true, true}};

    checkConsensus(expectedConsensus, &c1, &c2, &c3);
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

    // CELL 2 should be excluded
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

    Consensus_t expectedConsensus = {
        .statuses = {CELL_OK, CELL_OK, CELL_OK},
        .PPO2s = {110, 130, 100},
        .millis = {0, 0, 0},
        .consensus = 105,
        .included = {true, false, true}};

    checkConsensus(expectedConsensus, &c1, &c2, &c3);
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

    Consensus_t expectedConsensus = {
        .statuses = {CELL_OK, CELL_OK, CELL_OK},
        .PPO2s = {120, 130, 100},
        .millis = {0, 0, 0},
        .consensus = 125,
        .included = {true, true, false}};

    checkConsensus(expectedConsensus, &c1, &c2, &c3);
}

TEST(PPO2Transmitter, calculateConsensus_ExcludesTimedOutCell)
{
    uint8_t concensusVal[3] = {105, 110, 115};
    for (int i = 0; i < 3; i++)
    {
        OxygenCell_t c1 = {
            .cellNumber = 0,
            .type = CELL_ANALOG,
            .ppo2 = 120,
            .millivolts = 0,
            .status = CELL_OK,
            .data_time = (i == 0) ? (Timestamp_t)1500 : (Timestamp_t)0};
        OxygenCell_t c2 = {
            .cellNumber = 1,
            .type = CELL_ANALOG,
            .ppo2 = 110,
            .millivolts = 0,
            .status = CELL_OK,
            .data_time = (i == 1) ? (Timestamp_t)1500 : (Timestamp_t)0};
        OxygenCell_t c3 = {
            .cellNumber = 2,
            .type = CELL_ANALOG,
            .ppo2 = 100,
            .millivolts = 0,
            .status = CELL_OK,
            .data_time = (i == 2) ? (Timestamp_t)1500 : (Timestamp_t)0};

        Consensus_t expectedConsensus = {
            .statuses = {CELL_OK, CELL_OK, CELL_OK},
            .PPO2s = {120, 110, 100},
            .millis = {0, 0, 0},
            .consensus = concensusVal[i],
            .included = {(i == 0) ? false : true,
                         (i == 1) ? false : true,
                         (i == 2) ? false : true}};

        checkConsensus(expectedConsensus, &c1, &c2, &c3);
    }
}

TEST(PPO2Transmitter, calculateConsensus_ExcludesFailedCell)
{
    uint8_t concensusVal[3] = {105, 110, 115};
    for (int i = 0; i < 3; i++)
    {
        OxygenCell_t c1 = {
            .cellNumber = 0,
            .type = CELL_ANALOG,
            .ppo2 = 120,
            .millivolts = 0,
            .status = (i == 0) ? CELL_FAIL : CELL_OK,
            .data_time = 0};
        OxygenCell_t c2 = {
            .cellNumber = 1,
            .type = CELL_ANALOG,
            .ppo2 = 110,
            .millivolts = 0,
            .status = (i == 1) ? CELL_FAIL : CELL_OK,
            .data_time = 0};
        OxygenCell_t c3 = {
            .cellNumber = 2,
            .type = CELL_ANALOG,
            .ppo2 = 100,
            .millivolts = 0,
            .status = (i == 2) ? CELL_FAIL : CELL_OK,
            .data_time = 0};

        Consensus_t expectedConsensus = {
            .statuses = {(i == 0) ? CELL_FAIL : CELL_OK,
                         (i == 1) ? CELL_FAIL : CELL_OK,
                         (i == 2) ? CELL_FAIL : CELL_OK},
            .PPO2s = {120, 110, 100},
            .millis = {0, 0, 0},
            .consensus = concensusVal[i],
            .included = {(i == 0) ? false : true,
                         (i == 1) ? false : true,
                         (i == 2) ? false : true}};

        checkConsensus(expectedConsensus, &c1, &c2, &c3);
    }
}

TEST(PPO2Transmitter, calculateConsensus_ExcludesCalCell)
{
    uint8_t concensusVal[3] = {105, 110, 115};
    for (int i = 0; i < 3; i++)
    {
        OxygenCell_t c1 = {
            .cellNumber = 0,
            .type = CELL_ANALOG,
            .ppo2 = 120,
            .millivolts = 0,
            .status = (i == 0) ? CELL_NEED_CAL : CELL_OK,
            .data_time = 0};
        OxygenCell_t c2 = {
            .cellNumber = 1,
            .type = CELL_ANALOG,
            .ppo2 = 110,
            .millivolts = 0,
            .status = (i == 1) ? CELL_NEED_CAL : CELL_OK,
            .data_time = 0};
        OxygenCell_t c3 = {
            .cellNumber = 2,
            .type = CELL_ANALOG,
            .ppo2 = 100,
            .millivolts = 0,
            .status = (i == 2) ? CELL_NEED_CAL : CELL_OK,
            .data_time = 0};

        Consensus_t expectedConsensus = {
            .statuses = {(i == 0) ? CELL_NEED_CAL : CELL_OK,
                         (i == 1) ? CELL_NEED_CAL : CELL_OK,
                         (i == 2) ? CELL_NEED_CAL : CELL_OK},
            .PPO2s = {120, 110, 100},
            .millis = {0, 0, 0},
            .consensus = concensusVal[i],
            .included = {(i == 0) ? false : true,
                         (i == 1) ? false : true,
                         (i == 2) ? false : true}};

        checkConsensus(expectedConsensus, &c1, &c2, &c3);
    }
}

TEST(PPO2Transmitter, calculateConsensus_DualCellFailure)
{
    uint8_t concensusVal[3] = {120, 110, 100};
    for (int i = 0; i < 3; i++)
    {
        OxygenCell_t c1 = {
            .cellNumber = 0,
            .type = CELL_ANALOG,
            .ppo2 = 120,
            .millivolts = 0,
            .status = (i == 0) ? CELL_OK : CELL_FAIL,
            .data_time = 0};
        OxygenCell_t c2 = {
            .cellNumber = 1,
            .type = CELL_ANALOG,
            .ppo2 = 110,
            .millivolts = 0,
            .status = (i == 1) ? CELL_OK : CELL_FAIL,
            .data_time = 0};
        OxygenCell_t c3 = {
            .cellNumber = 2,
            .type = CELL_ANALOG,
            .ppo2 = 100,
            .millivolts = 0,
            .status = (i == 2) ? CELL_OK : CELL_FAIL,
            .data_time = 0};

        Consensus_t expectedConsensus = {
            .statuses = {(i == 0) ? CELL_OK : CELL_FAIL,
                         (i == 1) ? CELL_OK : CELL_FAIL,
                         (i == 2) ? CELL_OK : CELL_FAIL},
            .PPO2s = {120, 110, 100},
            .millis = {0, 0, 0},
            .consensus = concensusVal[i],
            .included = {(i == 0),
                         (i == 1),
                         (i == 2)}};

        checkConsensus(expectedConsensus, &c1, &c2, &c3);
    }
}