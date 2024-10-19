#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#include "OxygenCell.h"
#include "PPO2Transmitter.h"

// All the C stuff has to be externed
extern "C"
{
    // Extern to access "hidden" internal methods
    extern void setFailedCellsValues(Consensus_t *consensus);
    extern void PPO2TXTask(void *arg);

    typedef struct PPO2TXTask_params_s
    {
        DiveCANDevice_t *device;
        QueueHandle_t c1;
        QueueHandle_t c2;
        QueueHandle_t c3;
    } PPO2TXTask_params_t;

    struct QueueDefinition
    {
        OxygenCell_t cellData;
    };

    // Here be mocks
    uint32_t HAL_GetTick(void)
    {
        mock().actualCall("HAL_GetTick");
        return 0;
    }

    BaseType_t xQueuePeek(QueueHandle_t xQueue, void *const pvBuffer, TickType_t xTicksToWait)
    {
        mock().actualCall("xQueuePeek");
        *((OxygenCell_t*)pvBuffer) = xQueue->cellData;
        return true;
    }

    void txPPO2(const DiveCANType_t deviceType, const PPO2_t cell1, const PPO2_t cell2, const PPO2_t cell3)
    {
        mock().actualCall("txPPO2").withParameter("deviceType", deviceType).withParameter("cell1", cell1).withParameter("cell2", cell2).withParameter("cell3", cell3);
    }

    void txMillivolts(const DiveCANType_t deviceType, const Millivolts_t cell1, const Millivolts_t cell2, const Millivolts_t cell3)
    {
        mock().actualCall("txMillivolts").withParameter("deviceType", deviceType).withParameter("cell1", cell1).withParameter("cell2", cell2).withParameter("cell3", cell3);
    }

    void txCellState(const DiveCANType_t deviceType, const bool cell1, const bool cell2, const bool cell3, const PPO2_t PPO2)
    {
        mock().actualCall("txCellState").withParameter("deviceType", deviceType).withParameter("cell1", cell1).withParameter("cell2", cell2).withParameter("cell3", cell3).withParameter("PPO2", PPO2);
    }

    osThreadId_t osThreadNew(osThreadFunc_t func, void *argument, const osThreadAttr_t *attr)
    {
        // We're only checking the thread parameters, func and attributes are too burried in RTOS land to meaningfully test
        mock().actualCall("osThreadNew").withParameterOfType("PPO2TXTask_params", "argument", argument);
        return NULL;
    }

    osStatus_t osDelay(uint32_t ticks)
    {
        mock().actualCall("osDelay");
        return osOK;
    }
}

class PPO2TXTask_paramsComparator : public MockNamedValueComparator
{
public:
    virtual bool isEqual(const void *object1, const void *object2)
    {
        bool equal = true;
        PPO2TXTask_params_t *struct1 = (PPO2TXTask_params_t *)object1;
        PPO2TXTask_params_t *struct2 = (PPO2TXTask_params_t *)object2;

        equal &= strncmp(struct1->device->name, struct2->device->name, sizeof(struct1->device->name)) == 0;
        equal &= struct1->device->type == struct2->device->type;
        equal &= struct1->device->manufacturerID == struct2->device->manufacturerID;
        equal &= struct1->device->firmwareVersion == struct2->device->firmwareVersion;
        equal &= struct1->c1 == struct2->c1;
        equal &= struct1->c2 == struct2->c2;
        equal &= struct1->c3 == struct2->c3;

        return equal;
    }
    virtual SimpleString valueToString(const void *object)
    {
        return ((PPO2TXTask_params_t *)object)->device->name;
    }
};

PPO2TXTask_paramsComparator taskParamsComparator;
TEST_GROUP(PPO2Transmitter){
    void setup(){
        // Init stuff
        mock().installComparator("PPO2TXTask_params", taskParamsComparator);
}

void teardown()
{
    mock().removeAllComparatorsAndCopiers();
    mock().clear();
}
}
;

TEST(PPO2Transmitter, InitPPO2TX)
{
    // All the init does is structure its parameters and spool up the thread with the appropriate parameters
    QueueDefinition c1_struct = {0};
    QueueDefinition c2_struct = {0};
    QueueDefinition c3_struct = {0};

    DiveCANDevice_t mockDevice = {
        .name = "test",
        .type = DIVECAN_CONTROLLER,
        .manufacturerID = DIVECAN_MANUFACTURER_GEN,
        .firmwareVersion = 8};

    QueueHandle_t c1 = &c1_struct;
    QueueHandle_t c2 = &c2_struct;
    QueueHandle_t c3 = &c3_struct;

    PPO2TXTask_params_t expectedParams = {
        .device = &mockDevice,
        .c1 = c1,
        .c2 = c2,
        .c3 = c3};

    mock().expectOneCall("osThreadNew").withParameterOfType("PPO2TXTask_params", "argument", &expectedParams);
    InitPPO2TX(&mockDevice, c1, c2, c3);
}

TEST(PPO2Transmitter, TaskTXsValues)
{
    CHECK(RTOS_LOOP_FOREVER == false); // Ensure that we're compiled with the correct options for testing RTOS

    QueueDefinition c1_struct = {.cellData = {
                                     .cellNumber = 0,
                                     .type = CELL_ANALOG,
                                     .ppo2 = 90,
                                     .millivolts = 11,
                                     .status = CELL_OK,
                                     .dataTime = 0}};
    QueueDefinition c2_struct = {.cellData = {
                                     .cellNumber = 1,
                                     .type = CELL_ANALOG,
                                     .ppo2 = 110,
                                     .millivolts = 12,
                                     .status = CELL_OK,
                                     .dataTime = 0}};
    QueueDefinition c3_struct = {.cellData = {
                                     .cellNumber = 2,
                                     .type = CELL_ANALOG,
                                     .ppo2 = 100,
                                     .millivolts = 13,
                                     .status = CELL_OK,
                                     .dataTime = 0}};

    DiveCANDevice_t mockDevice = {
        .name = "test",
        .type = DIVECAN_SOLO,
        .manufacturerID = DIVECAN_MANUFACTURER_GEN,
        .firmwareVersion = 8};

    QueueHandle_t c1 = &c1_struct;
    QueueHandle_t c2 = &c2_struct;
    QueueHandle_t c3 = &c3_struct;

    PPO2TXTask_params_t expectedParams = {
        .device = &mockDevice,
        .c1 = c1,
        .c2 = c2,
        .c3 = c3};

    // The job of this test is mainly to make sure that our can TX methods are fired correctly
    mock().expectOneCall("osDelay");
    mock().expectOneCall("HAL_GetTick");
    mock().expectNCalls(3, "xQueuePeek");

    mock().expectOneCall("txPPO2").withParameter("deviceType", DIVECAN_SOLO).withParameter("cell1", 90).withParameter("cell2", 110).withParameter("cell3", 100);
    mock().expectOneCall("txMillivolts").withParameter("deviceType", DIVECAN_SOLO).withParameter("cell1", 11).withParameter("cell2", 12).withParameter("cell3", 13);
    mock().expectOneCall("txCellState").withParameter("deviceType", DIVECAN_SOLO).withParameter("cell1", true).withParameter("cell2", true).withParameter("cell3", true).withParameter("PPO2", 100);
    PPO2TXTask(&expectedParams);
}

TEST(PPO2Transmitter, setFailedCellsValues_FFsFailedCells)
{
    bool FailedPermutations[8][3] = {
        {0, 0, 0},
        {0, 0, 1},
        {0, 1, 0},
        {0, 1, 1},
        {1, 0, 0},
        {1, 0, 1},
        {1, 1, 0},
        {1, 1, 1}};

    PPO2_t referencePPO2Vals[3] = {1, 2, 3};

    for (uint8_t i = 0; i < 8; ++i)
    {
        Consensus_t consensus = {
            .statusArray = {(FailedPermutations[i][0] == 0) ? CELL_OK : CELL_FAIL,
                         (FailedPermutations[i][1] == 0) ? CELL_OK : CELL_FAIL,
                         (FailedPermutations[i][2] == 0) ? CELL_OK : CELL_FAIL},
            .ppo2Array = {referencePPO2Vals[0], referencePPO2Vals[1], referencePPO2Vals[2]},
            .milliArray = {0, 0, 0},
            .consensus = 0,
            .includeArray = {true, true, true}};

        mock().expectOneCall("isCalibrating");
        setFailedCellsValues(&consensus);

        for (uint8_t j = 0; j < 3; ++j)
        {
            if (FailedPermutations[i][j] == 1)
            {
                CHECK(consensus.ppo2Array[j] == 0xFF);
            }
            else
            {
                CHECK(consensus.ppo2Array[j] == referencePPO2Vals[j]);
            }
        }
    }
}

TEST(PPO2Transmitter, setFailedCellsValues_FFsCalNeededCells)
{
    bool FailedPermutations[8][3] = {
        {0, 0, 0},
        {0, 0, 1},
        {0, 1, 0},
        {0, 1, 1},
        {1, 0, 0},
        {1, 0, 1},
        {1, 1, 0},
        {1, 1, 1}};

    PPO2_t referencePPO2Vals[3] = {1, 2, 3};

    for (uint8_t i = 0; i < 8; ++i)
    {
        Consensus_t consensus = {
            .statusArray = {(FailedPermutations[i][0] == 0) ? CELL_OK : CELL_NEED_CAL,
                         (FailedPermutations[i][1] == 0) ? CELL_OK : CELL_NEED_CAL,
                         (FailedPermutations[i][2] == 0) ? CELL_OK : CELL_NEED_CAL},
            .ppo2Array = {referencePPO2Vals[0], referencePPO2Vals[1], referencePPO2Vals[2]},
            .milliArray = {0, 0, 0},
            .consensus = 0,
            .includeArray = {true, true, true}};

        mock().expectOneCall("isCalibrating");
        setFailedCellsValues(&consensus);

        // If any of the cells need cal then they should all be FF
        if ((FailedPermutations[i][0] == 0) &&
            (FailedPermutations[i][1] == 0) &&
            (FailedPermutations[i][2] == 0))
        {
            for (uint8_t j = 0; j < 3; ++j)
            {
                CHECK(consensus.ppo2Array[j] == referencePPO2Vals[j]);
            }
        }
        else
        {
            for (uint8_t j = 0; j < 3; ++j)
            {
                CHECK(consensus.ppo2Array[j] == 0xFF);
            }
        }
    }
}
