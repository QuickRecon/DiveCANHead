#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#include "OxygenCell.h"
#include "AnalogOxygen.h"
#include "DigitalOxygen.h"

// All the C stuff has to be externed
extern "C"
{
    QueueHandle_t xQueueGenericCreateStatic(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize, uint8_t *pucQueueStorage, StaticQueue_t *pxStaticQueue, const uint8_t ucQueueType)
    {
        return NULL;
    }

    AnalogOxygenState_t *Analog_InitCell(OxygenHandle_t *cell, QueueHandle_t outQueue)
    {
        mock().actualCall("txCalResponse");
        return NULL;
    }
    DigitalOxygenState_t *Digital_InitCell(OxygenHandle_t *cell, QueueHandle_t outQueue)
    {
        mock().actualCall("txCalResponse");
        return NULL;
    }
    ShortMillivolts_t Calibrate(AnalogOxygenState_t *handle, const PPO2_t PPO2, NonFatalError_t *calError)
    {
        return 0;
    }
    void serial_printf(const char *fmt, ...)
    {
        /* Literally do nothing we don't care about prints */
    }
    void txCalResponse(DiveCANType_t deviceType, DiveCANCalResponse_t response, ShortMillivolts_t cell1, ShortMillivolts_t cell2, ShortMillivolts_t cell3, FO2_t FO2, uint16_t atmosphericPressure)
    {
        mock().actualCall("txCalResponse");
    }
    void osThreadExit(void)
    {
        mock().actualCall("osThreadExit");
    }
    osThreadState_t osThreadGetState(osThreadId_t thread_id)
    {
        return osThreadInactive;
    }
    void txCalAck(DiveCANType_t deviceType)
    {
        mock().actualCall("txCalAck");
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
        CHECK(consensus.statusArray[0] == expectedConsensus.statusArray[POSITION_1]);
        CHECK(consensus.statusArray[1] == expectedConsensus.statusArray[POSITION_2]);
        CHECK(consensus.statusArray[2] == expectedConsensus.statusArray[POSITION_3]);
        CHECK(consensus.ppo2Array[0] == expectedConsensus.ppo2Array[POSITION_1]);
        CHECK(consensus.ppo2Array[1] == expectedConsensus.ppo2Array[POSITION_2]);
        CHECK(consensus.ppo2Array[2] == expectedConsensus.ppo2Array[POSITION_3]);
        CHECK(consensus.milliArray[0] == expectedConsensus.milliArray[POSITION_1]);
        CHECK(consensus.milliArray[1] == expectedConsensus.milliArray[POSITION_2]);
        CHECK(consensus.milliArray[2] == expectedConsensus.milliArray[POSITION_3]);
        CHECK(consensus.includeArray[0] == expectedConsensus.includeArray[POSITION_1]);
        CHECK(consensus.includeArray[1] == expectedConsensus.includeArray[POSITION_2]);
        CHECK(consensus.includeArray[2] == expectedConsensus.includeArray[POSITION_3]);
        CHECK(consensus.consensus == expectedConsensus.consensus);
    }
}

TEST_GROUP(OxygenCell){
    void setup(){

        // Init stuff
    }

    void teardown(){
        mock().removeAllComparatorsAndCopiers();
mock().clear();
}
}
;

TEST(OxygenCell, calculateConsensus_AveragesCells)
{
    OxygenCell_t c1 = {
        .cellNumber = 0,
        .type = CELL_ANALOG,
        .ppo2 = 110,
        .precision_PPO2 = 1.1f,
        .millivolts = 12,
        .status = CELL_OK,
        .dataTime = 0};
    OxygenCell_t c2 = {
        .cellNumber = 1,
        .type = CELL_ANALOG,
        .ppo2 = 120,
        .precision_PPO2 = 1.2f,
        .millivolts = 13,
        .status = CELL_OK,
        .dataTime = 0};
    OxygenCell_t c3 = {
        .cellNumber = 2,
        .type = CELL_ANALOG,
        .ppo2 = 100,
        .precision_PPO2 = 1.0f,
        .millivolts = 14,
        .status = CELL_OK,
        .dataTime = 0};

    Consensus_t expectedConsensus = {
        .statusArray = {CELL_OK, CELL_OK, CELL_OK},
        .ppo2Array = {110, 120, 100},
        .milliArray = {12, 13, 14},
        .consensus = 110,
        .includeArray = {true, true, true}};

    checkConsensus(expectedConsensus, &c1, &c2, &c3);
}

TEST(OxygenCell, calculateConsensus_ExcludesHigh)
{
    OxygenCell_t c1 = {
        .cellNumber = 0,
        .type = CELL_ANALOG,
        .ppo2 = 110,
        .precision_PPO2 = 1.1f,
        .millivolts = 0,
        .status = CELL_OK,
        .dataTime = 0};

    // CELL 2 should be excluded
    OxygenCell_t c2 = {
        .cellNumber = 1,
        .type = CELL_ANALOG,
        .ppo2 = 130,
        .precision_PPO2 = 1.3f,
        .millivolts = 0,
        .status = CELL_OK,
        .dataTime = 0};
    OxygenCell_t c3 = {
        .cellNumber = 2,
        .type = CELL_ANALOG,
        .ppo2 = 100,
        .precision_PPO2 = 1.0f,
        .millivolts = 0,
        .status = CELL_OK,
        .dataTime = 0};

    Consensus_t expectedConsensus = {
        .statusArray = {CELL_OK, CELL_OK, CELL_OK},
        .ppo2Array = {110, 130, 100},
        .milliArray = {0, 0, 0},
        .consensus = 105,
        .includeArray = {true, false, true}};

    checkConsensus(expectedConsensus, &c1, &c2, &c3);
}

TEST(OxygenCell, calculateConsensus_ExcludesLow)
{
    OxygenCell_t c1 = {
        .cellNumber = 0,
        .type = CELL_ANALOG,
        .ppo2 = 120,
        .precision_PPO2 = 1.2f,
        .millivolts = 0,
        .status = CELL_OK,
        .dataTime = 0};
    OxygenCell_t c2 = {
        .cellNumber = 1,
        .type = CELL_ANALOG,
        .ppo2 = 130,
        .precision_PPO2 = 1.3f,
        .millivolts = 0,
        .status = CELL_OK,
        .dataTime = 0};
    OxygenCell_t c3 = {
        .cellNumber = 2,
        .type = CELL_ANALOG,
        .ppo2 = 100,
        .precision_PPO2 = 1.0f,
        .millivolts = 0,
        .status = CELL_OK,
        .dataTime = 0};

    Consensus_t expectedConsensus = {
        .statusArray = {CELL_OK, CELL_OK, CELL_OK},
        .ppo2Array = {120, 130, 100},
        .milliArray = {0, 0, 0},
        .consensus = 125,
        .includeArray = {true, true, false}};

    checkConsensus(expectedConsensus, &c1, &c2, &c3);
}

TEST(OxygenCell, calculateConsensus_ExcludesTimedOutCell)
{
    uint8_t consensusVal[3] = {105, 110, 115};
    for (int i = 0; i < 3; i++)
    {
        OxygenCell_t c1 = {
            .cellNumber = 0,
            .type = CELL_ANALOG,
            .ppo2 = 120,
            .precision_PPO2 = 1.2f,
            .millivolts = 0,
            .status = CELL_OK,
            .dataTime = (i == 0) ? (Timestamp_t)1500 : (Timestamp_t)0};
        OxygenCell_t c2 = {
            .cellNumber = 1,
            .type = CELL_ANALOG,
            .ppo2 = 110,
            .precision_PPO2 = 1.1f,
            .millivolts = 0,
            .status = CELL_OK,
            .dataTime = (i == 1) ? (Timestamp_t)1500 : (Timestamp_t)0};
        OxygenCell_t c3 = {
            .cellNumber = 2,
            .type = CELL_ANALOG,
            .ppo2 = 100,
            .precision_PPO2 = 1.0f,
            .millivolts = 0,
            .status = CELL_OK,
            .dataTime = (i == 2) ? (Timestamp_t)1500 : (Timestamp_t)0};

        Consensus_t expectedConsensus = {
            .statusArray = {CELL_OK, CELL_OK, CELL_OK},
            .ppo2Array = {120, 110, 100},
            .milliArray = {0, 0, 0},
            .consensus = consensusVal[i],
            .includeArray = {(i == 0) ? false : true,
                             (i == 1) ? false : true,
                             (i == 2) ? false : true}};

        checkConsensus(expectedConsensus, &c1, &c2, &c3);
    }
}

TEST(OxygenCell, calculateConsensus_ExcludesFailedCell)
{
    uint8_t consensusVal[3] = {105, 110, 115};
    for (int i = 0; i < 3; i++)
    {
        OxygenCell_t c1 = {
            .cellNumber = 0,
            .type = CELL_ANALOG,
            .ppo2 = 120,
            .precision_PPO2 = 1.2f,
            .millivolts = 0,
            .status = (i == 0) ? CELL_FAIL : CELL_OK,
            .dataTime = 0};
        OxygenCell_t c2 = {
            .cellNumber = 1,
            .type = CELL_ANALOG,
            .ppo2 = 110,
            .precision_PPO2 = 1.1f,
            .millivolts = 0,
            .status = (i == 1) ? CELL_FAIL : CELL_OK,
            .dataTime = 0};
        OxygenCell_t c3 = {
            .cellNumber = 2,
            .type = CELL_ANALOG,
            .ppo2 = 100,
            .precision_PPO2 = 1.0f,
            .millivolts = 0,
            .status = (i == 2) ? CELL_FAIL : CELL_OK,
            .dataTime = 0};

        Consensus_t expectedConsensus = {
            .statusArray = {(i == 0) ? CELL_FAIL : CELL_OK,
                            (i == 1) ? CELL_FAIL : CELL_OK,
                            (i == 2) ? CELL_FAIL : CELL_OK},
            .ppo2Array = {120, 110, 100},
            .milliArray = {0, 0, 0},
            .consensus = consensusVal[i],
            .includeArray = {(i == 0) ? false : true,
                             (i == 1) ? false : true,
                             (i == 2) ? false : true}};

        checkConsensus(expectedConsensus, &c1, &c2, &c3);
    }
}

TEST(OxygenCell, calculateConsensus_ExcludesCalCell)
{
    uint8_t consensusVal[3] = {105, 110, 115};
    for (int i = 0; i < 3; i++)
    {
        OxygenCell_t c1 = {
            .cellNumber = 0,
            .type = CELL_ANALOG,
            .ppo2 = 120,
            .precision_PPO2 = 1.2f,
            .millivolts = 0,
            .status = (i == 0) ? CELL_NEED_CAL : CELL_OK,
            .dataTime = 0};
        OxygenCell_t c2 = {
            .cellNumber = 1,
            .type = CELL_ANALOG,
            .ppo2 = 110,
            .precision_PPO2 = 1.1f,
            .millivolts = 0,
            .status = (i == 1) ? CELL_NEED_CAL : CELL_OK,
            .dataTime = 0};
        OxygenCell_t c3 = {
            .cellNumber = 2,
            .type = CELL_ANALOG,
            .ppo2 = 100,
            .precision_PPO2 = 1.0f,
            .millivolts = 0,
            .status = (i == 2) ? CELL_NEED_CAL : CELL_OK,
            .dataTime = 0};

        Consensus_t expectedConsensus = {
            .statusArray = {(i == 0) ? CELL_NEED_CAL : CELL_OK,
                            (i == 1) ? CELL_NEED_CAL : CELL_OK,
                            (i == 2) ? CELL_NEED_CAL : CELL_OK},
            .ppo2Array = {120, 110, 100},
            .milliArray = {0, 0, 0},
            .consensus = consensusVal[i],
            .includeArray = {(i == 0) ? false : true,
                             (i == 1) ? false : true,
                             (i == 2) ? false : true}};

        checkConsensus(expectedConsensus, &c1, &c2, &c3);
    }
}

TEST(OxygenCell, calculateConsensus_ExcludesDegradedCell)
{
    uint8_t consensusVal[3] = {105, 110, 115};
    for (int i = 0; i < 3; i++)
    {
        OxygenCell_t c1 = {
            .cellNumber = 0,
            .type = CELL_ANALOG,
            .ppo2 = 120,
            .precision_PPO2 = 1.2f,
            .millivolts = 0,
            .status = (i == 0) ? CELL_DEGRADED : CELL_OK,
            .dataTime = 0};
        OxygenCell_t c2 = {
            .cellNumber = 1,
            .type = CELL_ANALOG,
            .ppo2 = 110,
            .precision_PPO2 = 1.1f,
            .millivolts = 0,
            .status = (i == 1) ? CELL_DEGRADED : CELL_OK,
            .dataTime = 0};
        OxygenCell_t c3 = {
            .cellNumber = 2,
            .type = CELL_ANALOG,
            .ppo2 = 100,
            .precision_PPO2 = 1.0f,
            .millivolts = 0,
            .status = (i == 2) ? CELL_DEGRADED : CELL_OK,
            .dataTime = 0};

        Consensus_t expectedConsensus = {
            .statusArray = {(i == 0) ? CELL_DEGRADED : CELL_OK,
                            (i == 1) ? CELL_DEGRADED : CELL_OK,
                            (i == 2) ? CELL_DEGRADED : CELL_OK},
            .ppo2Array = {120, 110, 100},
            .milliArray = {0, 0, 0},
            .consensus = consensusVal[i],
            .includeArray = {(i == 0) ? false : true,
                             (i == 1) ? false : true,
                             (i == 2) ? false : true}};

        checkConsensus(expectedConsensus, &c1, &c2, &c3);
    }
}

TEST(OxygenCell, calculateConsensus_DualCellFailure)
{
    uint8_t consensusVal[3] = {120, 110, 100};
    for (int i = 0; i < 3; i++)
    {
        OxygenCell_t c1 = {
            .cellNumber = 0,
            .type = CELL_ANALOG,
            .ppo2 = 120,
            .precision_PPO2 = 1.2f,
            .millivolts = 0,
            .status = (i == 0) ? CELL_OK : CELL_FAIL,
            .dataTime = 0};
        OxygenCell_t c2 = {
            .cellNumber = 1,
            .type = CELL_ANALOG,
            .ppo2 = 110,
            .precision_PPO2 = 1.1f,
            .millivolts = 0,
            .status = (i == 1) ? CELL_OK : CELL_FAIL,
            .dataTime = 0};
        OxygenCell_t c3 = {
            .cellNumber = 2,
            .type = CELL_ANALOG,
            .ppo2 = 100,
            .precision_PPO2 = 1.0f,
            .millivolts = 0,
            .status = (i == 2) ? CELL_OK : CELL_FAIL,
            .dataTime = 0};

        Consensus_t expectedConsensus = {
            .statusArray = {(i == 0) ? CELL_OK : CELL_FAIL,
                            (i == 1) ? CELL_OK : CELL_FAIL,
                            (i == 2) ? CELL_OK : CELL_FAIL},
            .ppo2Array = {120, 110, 100},
            .milliArray = {0, 0, 0},
            .consensus = consensusVal[i],
            .includeArray = {(i == 0),
                             (i == 1),
                             (i == 2)}};

        checkConsensus(expectedConsensus, &c1, &c2, &c3);
    }
}

TEST(OxygenCell, calculateConsensus_DivergedDualCellFailure)
{
    uint8_t consensusVal[3] = {200, 100, 20};
    for (int i = 0; i < 3; i++)
    {
        OxygenCell_t c1 = {
            .cellNumber = 0,
            .type = CELL_ANALOG,
            .ppo2 = 200,
            .precision_PPO2 = 2.0f,
            .millivolts = 0,
            .status = (i == 0) ? CELL_OK : CELL_FAIL,
            .dataTime = 0};
        OxygenCell_t c2 = {
            .cellNumber = 1,
            .type = CELL_ANALOG,
            .ppo2 = 100,
            .precision_PPO2 = 1.0f,
            .millivolts = 0,
            .status = (i == 1) ? CELL_OK : CELL_FAIL,
            .dataTime = 0};
        OxygenCell_t c3 = {
            .cellNumber = 2,
            .type = CELL_ANALOG,
            .ppo2 = 20,
            .precision_PPO2 = 0.20f,
            .millivolts = 0,
            .status = (i == 2) ? CELL_OK : CELL_FAIL,
            .dataTime = 0};

        Consensus_t expectedConsensus = {
            .statusArray = {(i == 0) ? CELL_OK : CELL_FAIL,
                            (i == 1) ? CELL_OK : CELL_FAIL,
                            (i == 2) ? CELL_OK : CELL_FAIL},
            .ppo2Array = {200, 100, 20},
            .milliArray = {0, 0, 0},
            .consensus = consensusVal[i],
            .includeArray = {(i == 0),
                             (i == 1),
                             (i == 2)}};

        checkConsensus(expectedConsensus, &c1, &c2, &c3);
    }
}