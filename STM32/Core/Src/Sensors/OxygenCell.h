#pragma once
#include "../common.h"

#include "cmsis_os.h"
#include "queue.h"

typedef struct OxygenCellStruct OxygenCell_t;
#include "../DiveCAN/Transciever.h"

#define CELL_COUNT 3

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct OxygenCellStruct
    {
        /* Configuration*/
        uint8_t cellNumber;

        CellType_t type;

        PPO2_t ppo2;
        PIDNumeric_t precisionPPO2;
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
        uint8_t processorBuffer[CELL_PROCESSOR_STACK_SIZE];
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
        PIDNumeric_t precisionPPO2Array[3];
        Millivolts_t milliArray[3];
        PPO2_t consensus;
        PIDNumeric_t precisionConsensus;
        bool includeArray[3];
    } Consensus_t;

    /** @struct CalParameters_s
     *  @brief Contains calibration parameters for an oxygen sensor.
     *
     *  The `deviceType` field specifies the type of device being used, while the
     *  `fO2`, `pressureVal`, and `calMethod` fields specify the FO2 value,
     *  pressure value, and calibration method to be used. The remaining fields
     *  are for individual cell parameters.
     */
    typedef struct
    {
        DiveCANType_t deviceType;
        FO2_t fO2;
        uint16_t pressureVal;

        ShortMillivolts_t cell1;
        ShortMillivolts_t cell2;
        ShortMillivolts_t cell3;

        OxygenCalMethod_t calMethod;
    } CalParameters_t;

    QueueHandle_t CreateCell(uint8_t cellNumber, CellType_t type);

    bool isCalibrating(void);
    void RunCalibrationTask(DiveCANType_t deviceType, const FO2_t in_fO2, const uint16_t in_pressure_val, OxygenCalMethod_t calMethod);

    Consensus_t peekCellConsensus(QueueHandle_t cell1, QueueHandle_t cell2, QueueHandle_t cell3);
    Consensus_t calculateConsensus(const OxygenCell_t *const c1, const OxygenCell_t *const c2, const OxygenCell_t *const c3);
    uint8_t cellConfidence(const Consensus_t *const consensus);

#ifdef TESTING
    void CalibrationTask(void *arg);
    DiveCANCalResponse_t DigitalReferenceCalibrate(CalParameters_t *calParams);
    DiveCANCalResponse_t AnalogReferenceCalibrate(CalParameters_t *calParams);
#endif // TESTING

#ifdef __cplusplus
}
#endif
