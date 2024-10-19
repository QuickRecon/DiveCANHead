#pragma once
#include "../common.h"

#include "cmsis_os.h"
#include "queue.h"

#include "../DiveCAN/Transciever.h"

#define CELL_COUNT 3

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        /* Configuration*/
        uint8_t cellNumber;

        CellType_t type;

        PPO2_t ppo2;
        Millivolts_t millivolts;
        CellStatus_t status;

        Timestamp_t dataTime;

    } OxygenCell_t;

    /** @struct OxygenHandle_s
     *  @brief Contains the type and a pointer to an oxygen cell handle.
     *
     *  The `type` field contains the type of the cell, while the `cellHandle`
     *  field contains a pointer to the actual cell handle object.
     */
    typedef struct
    {
        CellType_t type;
        void *cellHandle;

        uint8_t cellNumber;
        uint32_t processorBuffer[CELL_PROCESSOR_STACK_SIZE];
        StaticTask_t processorControlblock;
    } OxygenHandle_t;

    typedef enum
    {
        CAL_DIGITAL_REFERENCE = 0,
        CAL_ANALOG_ABSOLUTE = 1,
        CAL_TOTAL_ABSOLUTE = 2
    } OxygenCalMethod_t;

    typedef struct
    {
        CellStatus_t statusArray[3];
        PPO2_t ppo2Array[3];
        Millivolts_t milliArray[3];
        PPO2_t consensus;
        bool includeArray[3];
    } Consensus_t;

    QueueHandle_t CreateCell(uint8_t cellNumber, CellType_t type);

    bool isCalibrating(void);
    void RunCalibrationTask(DiveCANType_t deviceType, const FO2_t in_fO2, const uint16_t in_pressure_val, OxygenCalMethod_t calMethod);
    uint8_t cellConfidence(Consensus_t consensus);
    Consensus_t calculateConsensus(const OxygenCell_t *const c1, const OxygenCell_t *const c2, const OxygenCell_t *const c3);
#ifdef __cplusplus
}
#endif
