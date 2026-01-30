/**
 * @file mockUdsDependencies.cpp
 * @brief Mock implementations for UDS module dependencies
 *
 * Provides stubs/mocks for external functions used by UDS, ISO-TP, and state DID modules.
 * These are "silent" mocks that work without explicit expectations unless specifically tested.
 */

#include "CppUTestExt/MockSupport.h"
#include <cstring>

extern "C"
{
#include "common.h"
#include "Transciever.h"
#include "cmsis_os.h"

    /* ========================================================================
     * State vector accumulator - global storage for cell/system state
     * ======================================================================== */
    struct StateVector
    {
        PPO2_t ppo2[3];
        Millivolts_t millivolts[3];
        uint16_t rawAdc[3];
        CellStatus_t status[3];
        CellType_t type[3];
        bool included[3];
        int32_t temperature[3];
        int32_t pressure[3];
        float humidity[3];
    };

    StateVector stateVectorAccumulator = {
        .ppo2 = {100, 100, 100},
        .millivolts = {10, 10, 10},
        .rawAdc = {32767, 32767, 32767},
        .status = {CELL_OK, CELL_OK, CELL_OK},
        .type = {CELL_ANALOG, CELL_ANALOG, CELL_ANALOG},
        .included = {true, true, true},
        .temperature = {25000, 25000, 25000},
        .pressure = {101325000, 101325000, 101325000},
        .humidity = {0.5f, 0.5f, 0.5f},
    };

    /* ========================================================================
     * ISO-TP TX Queue dependency - CAN transmission
     * The TX queue calls sendCANMessageBlocking. Map this to the sendCANMessage
     * mock that tests already use.
     * ======================================================================== */
    void sendCANMessageBlocking(const DiveCANMessage_t message)
    {
        /* Forward to sendCANMessage mock so tests only need to expect one type */
        mock().actualCall("sendCANMessage")
            .withParameter("id", message.id)
            .withParameter("length", message.length)
            .withMemoryBufferParameter("data", message.data, message.length);
    }

    /* ========================================================================
     * FreeRTOS queue functions - Silent operation with functional behavior
     * These simulate a working queue without requiring mock expectations.
     * ======================================================================== */

    /* Simple fake queue storage */
    static uint8_t fakeQueueStorage[4096] = {0};
    static size_t fakeQueueItemSize = 0;
    static size_t fakeQueueCount = 0;
    static size_t fakeQueueMaxCount = 10;

    /* Fake queue handle to return */
    static void *fakeQueueHandle = (void *)0x12345678;

    osMessageQueueId_t osMessageQueueNew(uint32_t msg_count, uint32_t msg_size, const osMessageQueueAttr_t *attr)
    {
        fakeQueueItemSize = msg_size;
        fakeQueueMaxCount = msg_count;
        fakeQueueCount = 0;
        return (osMessageQueueId_t)fakeQueueHandle;
    }

    osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id, const void *msg_ptr, uint8_t msg_prio, uint32_t timeout)
    {
        if (fakeQueueCount >= fakeQueueMaxCount)
        {
            return osErrorResource;
        }
        /* Copy message to fake storage */
        if (fakeQueueItemSize <= sizeof(fakeQueueStorage))
        {
            memcpy(&fakeQueueStorage[fakeQueueCount * fakeQueueItemSize], msg_ptr, fakeQueueItemSize);
            fakeQueueCount++;
        }
        return osOK;
    }

    osStatus_t osMessageQueueGet(osMessageQueueId_t mq_id, void *msg_ptr, uint8_t *msg_prio, uint32_t timeout)
    {
        if (fakeQueueCount == 0)
        {
            return osErrorResource;
        }
        /* Return oldest message */
        memcpy(msg_ptr, fakeQueueStorage, fakeQueueItemSize);
        /* Shift remaining messages */
        fakeQueueCount--;
        if (fakeQueueCount > 0)
        {
            memmove(fakeQueueStorage, &fakeQueueStorage[fakeQueueItemSize], fakeQueueCount * fakeQueueItemSize);
        }
        return osOK;
    }

    uint32_t osMessageQueueGetCount(osMessageQueueId_t mq_id)
    {
        return (uint32_t)fakeQueueCount;
    }

    uint32_t osMessageQueueGetSpace(osMessageQueueId_t mq_id)
    {
        return (uint32_t)(fakeQueueMaxCount - fakeQueueCount);
    }

    /* ========================================================================
     * Power management stubs - Return sensible defaults
     * ======================================================================== */
    ADCV_t getVBusVoltage(void)
    {
        return 5.0f;
    }

    ADCV_t getVCCVoltage(void)
    {
        return 3.3f;
    }

    ADCV_t getBatteryVoltage(void)
    {
        return 7.4f;
    }

    ADCV_t getCANVoltage(void)
    {
        return 5.0f;
    }

    ADCV_t getThresholdVoltage(void)
    {
        return 6.0f;
    }

    typedef enum
    {
        PWR_BATTERY = 0,
        PWR_CAN = 1,
        PWR_NONE = 2
    } PowerSource_t;

    PowerSource_t GetVCCSource(void)
    {
        return PWR_BATTERY;
    }

    PowerSource_t GetVBusSource(void)
    {
        return PWR_BATTERY;
    }

    /* ========================================================================
     * Test helper to reset fake queue state between tests
     * ======================================================================== */
    void resetFakeQueue(void)
    {
        fakeQueueCount = 0;
        memset(fakeQueueStorage, 0, sizeof(fakeQueueStorage));
    }
}

/* C++ callable version - no need since resetFakeQueue is already extern "C" */
