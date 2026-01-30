/**
 * @file uds_state_did.c
 * @brief UDS State Data Identifier (DID) handler implementation
 *
 * Provides read access to system state via individual DIDs.
 * Data is sourced from the stateVectorAccumulator in log.c.
 */

#include "uds_state_did.h"
#include "../../Hardware/log.h"
#include "../../Hardware/pwr_management.h"
#include "../../common.h"
#include "../../errors.h"
#include "main.h"
#include <string.h>

/* External reference to state vector accumulator from log.c */
extern BinaryStateVector_t stateVectorAccumulator;

/* Time conversion constant */
static const uint32_t MS_PER_SECOND = 1000U;

/* Byte indices for little-endian serialization */
static const uint8_t BYTE_IDX_0 = 0U;
static const uint8_t BYTE_IDX_1 = 1U;
static const uint8_t BYTE_IDX_2 = 2U;
static const uint8_t BYTE_IDX_3 = 3U;

/* Cell detail array indices for ANALOG cells */
#define ANALOG_DETAIL_RAW_ADC    0U
#define ANALOG_DETAIL_MILLIVOLTS 1U

/* Cell detail array indices for DIVEO2 cells */
#define DIVEO2_DETAIL_TEMPERATURE   0U
#define DIVEO2_DETAIL_ERROR         1U
#define DIVEO2_DETAIL_PHASE         2U
#define DIVEO2_DETAIL_INTENSITY     3U
#define DIVEO2_DETAIL_AMBIENT_LIGHT 4U
#define DIVEO2_DETAIL_PRESSURE      5U
#define DIVEO2_DETAIL_HUMIDITY      6U

/* Power sources bitfield layout: VCC source in bits 0-1, VBUS source in bits 2-3 */
#define VBUS_SOURCE_BIT_OFFSET 2U

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Get cell type from configuration
 */
static CellType_t getCellTypeFromConfig(const Configuration_t *config, uint8_t cellNum)
{
    CellType_t result = CELL_ANALOG; /* Safe default */

    if ((config == NULL) || (cellNum >= CELL_COUNT))
    {
        NON_FATAL_ERROR_DETAIL(INVALID_CELL_NUMBER_ERR, cellNum);
    }
    else if (cellNum == CELL_1)
    {
        result = config->cell1;
    }
    else if (cellNum == CELL_2)
    {
        result = config->cell2;
    }
    else if (cellNum == CELL_3)
    {
        result = config->cell3;
    }
    else
    {
        /* Unreachable: cellNum bounds checked above */
    }

    return result;
}

/**
 * @brief Write a float32 to buffer in little-endian format
 */
static void writeFloat32(uint8_t *buf, Numeric_t value)
{
    (void)memcpy(buf, &value, sizeof(Numeric_t));
}

/**
 * @brief Write a uint32 to buffer in little-endian format
 */
static void writeUint32(uint8_t *buf, uint32_t value)
{
    buf[BYTE_IDX_0] = (uint8_t)(value);
    buf[BYTE_IDX_1] = (uint8_t)(value >> BYTE_WIDTH);
    buf[BYTE_IDX_2] = (uint8_t)(value >> TWO_BYTE_WIDTH);
    buf[BYTE_IDX_3] = (uint8_t)(value >> THREE_BYTE_WIDTH);
}

/**
 * @brief Write an int32 to buffer in little-endian format
 */
static void writeInt32(uint8_t *buf, int32_t value)
{
    writeUint32(buf, (uint32_t)value);
}

/**
 * @brief Write a uint16 to buffer in little-endian format
 */
static void writeUint16(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value);
    buf[1] = (uint8_t)(value >> BYTE_WIDTH);
}

/**
 * @brief Write an int16 to buffer in little-endian format
 */
static void writeInt16(uint8_t *buf, int16_t value)
{
    writeUint16(buf, (uint16_t)value);
}

/* ============================================================================
 * PPO2 Control State DID Handlers (0xF2xx)
 * ============================================================================ */

static bool handleControlStateDID(uint16_t did, const Configuration_t *config, uint8_t *buf, uint16_t *len)
{
    bool result = true;

    switch (did)
    {
    case UDS_DID_CONSENSUS_PPO2:
        writeFloat32(buf, stateVectorAccumulator.consensusPpo2);
        *len = DATA_SIZE_FLOAT32;
        break;

    case UDS_DID_SETPOINT:
        writeFloat32(buf, stateVectorAccumulator.setpoint);
        *len = DATA_SIZE_FLOAT32;
        break;

    case UDS_DID_CELLS_VALID:
        buf[0] = stateVectorAccumulator.cellsValid;
        *len = DATA_SIZE_UINT8;
        break;

    case UDS_DID_DUTY_CYCLE:
        writeFloat32(buf, stateVectorAccumulator.dutyCycle);
        *len = DATA_SIZE_FLOAT32;
        break;

    case UDS_DID_INTEGRAL_STATE:
        writeFloat32(buf, stateVectorAccumulator.integralState);
        *len = DATA_SIZE_FLOAT32;
        break;

    case UDS_DID_SATURATION_COUNT:
        writeUint16(buf, stateVectorAccumulator.saturationCount);
        *len = DATA_SIZE_UINT16;
        break;

    case UDS_DID_UPTIME_SEC:
        writeUint32(buf, HAL_GetTick() / MS_PER_SECOND);
        *len = DATA_SIZE_FLOAT32;
        break;

    /* Power Monitoring DIDs */
    case UDS_DID_VBUS_VOLTAGE:
        writeFloat32(buf, getVBusVoltage());
        *len = DATA_SIZE_FLOAT32;
        break;

    case UDS_DID_VCC_VOLTAGE:
        writeFloat32(buf, getVCCVoltage());
        *len = DATA_SIZE_FLOAT32;
        break;

    case UDS_DID_BATTERY_VOLTAGE:
        writeFloat32(buf, getBatteryVoltage());
        *len = DATA_SIZE_FLOAT32;
        break;

    case UDS_DID_CAN_VOLTAGE:
        writeFloat32(buf, getCANVoltage());
        *len = DATA_SIZE_FLOAT32;
        break;

    case UDS_DID_THRESHOLD_VOLTAGE:
        writeFloat32(buf, getThresholdVoltage(config->dischargeThresholdMode));
        *len = DATA_SIZE_FLOAT32;
        break;

    case UDS_DID_POWER_SOURCES:
    {
        uint8_t sources = (uint8_t)((uint8_t)GetVCCSource() | (uint8_t)((uint8_t)GetVBusSource() << VBUS_SOURCE_BIT_OFFSET));
        buf[0] = sources;
        *len = DATA_SIZE_UINT8;
        break;
    }

    default:
        /* Expected: Unknown DID within control state range - caller handles NRC */
        result = false;
        break;
    }

    return result;
}

/* ============================================================================
 * Cell DID Handlers (0xF4Nx)
 * ============================================================================ */

/**
 * @brief Handle universal cell DIDs (applicable to all cell types)
 *
 * @param cellNum Cell number (0-2)
 * @param offset DID offset within cell range
 * @param cellType Configured cell type
 * @param buf Output buffer
 * @param len Output: bytes written
 * @return true if handled, false if not a universal DID
 */
static bool handleUniversalCellDID(uint8_t cellNum, uint8_t offset, CellType_t cellType,
                                   uint8_t *buf, uint16_t *len)
{
    bool result = false;

    if (offset == CELL_DID_PPO2)
    {
        writeFloat32(buf, stateVectorAccumulator.cellPpo2[cellNum]);
        *len = DATA_SIZE_FLOAT32;
        result = true;
    }
    else if (offset == CELL_DID_TYPE)
    {
        buf[0] = (uint8_t)cellType;
        *len = DATA_SIZE_UINT8;
        result = true;
    }
    else if (offset == CELL_DID_INCLUDED)
    {
        if ((stateVectorAccumulator.cellsValid & (1U << cellNum)) != 0U)
        {
            buf[0] = 1U;
        }
        else
        {
            buf[0] = 0U;
        }
        *len = DATA_SIZE_UINT8;
        result = true;
    }
    else if (offset == CELL_DID_STATUS)
    {
        buf[0] = stateVectorAccumulator.cellStatus[cellNum];
        *len = DATA_SIZE_UINT8;
        result = true;
    }
    else
    {
        /* Not a universal DID */
    }

    return result;
}

/**
 * @brief Handle analog cell-specific DIDs
 *
 * @param cellNum Cell number (0-2)
 * @param offset DID offset within cell range
 * @param buf Output buffer
 * @param len Output: bytes written
 * @return true if handled, false if not an analog-specific DID
 */
static bool handleAnalogCellDID(uint8_t cellNum, uint8_t offset, uint8_t *buf, uint16_t *len)
{
    bool result = false;

    if (offset == CELL_DID_RAW_ADC)
    {
        writeInt16(buf, (int16_t)stateVectorAccumulator.cellDetail[cellNum][ANALOG_DETAIL_RAW_ADC]);
        *len = DATA_SIZE_UINT16;
        result = true;
    }
    else if (offset == CELL_DID_MILLIVOLTS)
    {
        writeUint16(buf, (uint16_t)stateVectorAccumulator.cellDetail[cellNum][ANALOG_DETAIL_MILLIVOLTS]);
        *len = DATA_SIZE_UINT16;
        result = true;
    }
    else
    {
        /* Not an analog-specific DID */
    }

    return result;
}

/**
 * @brief Handle DiveO2 cell-specific DIDs
 *
 * @param cellNum Cell number (0-2)
 * @param offset DID offset within cell range
 * @param buf Output buffer
 * @param len Output: bytes written
 * @return true if handled, false if not a DiveO2-specific DID
 */
static bool handleDiveO2CellDID(uint8_t cellNum, uint8_t offset, uint8_t *buf, uint16_t *len)
{
    bool result = false;

    if (offset == CELL_DID_TEMPERATURE)
    {
        writeInt32(buf, (int32_t)stateVectorAccumulator.cellDetail[cellNum][DIVEO2_DETAIL_TEMPERATURE]);
        *len = DATA_SIZE_FLOAT32;
        result = true;
    }
    else if (offset == CELL_DID_ERROR)
    {
        writeInt32(buf, (int32_t)stateVectorAccumulator.cellDetail[cellNum][DIVEO2_DETAIL_ERROR]);
        *len = DATA_SIZE_FLOAT32;
        result = true;
    }
    else if (offset == CELL_DID_PHASE)
    {
        writeInt32(buf, (int32_t)stateVectorAccumulator.cellDetail[cellNum][DIVEO2_DETAIL_PHASE]);
        *len = DATA_SIZE_FLOAT32;
        result = true;
    }
    else if (offset == CELL_DID_INTENSITY)
    {
        writeInt32(buf, (int32_t)stateVectorAccumulator.cellDetail[cellNum][DIVEO2_DETAIL_INTENSITY]);
        *len = DATA_SIZE_FLOAT32;
        result = true;
    }
    else if (offset == CELL_DID_AMBIENT_LIGHT)
    {
        writeInt32(buf, (int32_t)stateVectorAccumulator.cellDetail[cellNum][DIVEO2_DETAIL_AMBIENT_LIGHT]);
        *len = DATA_SIZE_FLOAT32;
        result = true;
    }
    else if (offset == CELL_DID_PRESSURE)
    {
        writeInt32(buf, (int32_t)stateVectorAccumulator.cellDetail[cellNum][DIVEO2_DETAIL_PRESSURE]);
        *len = DATA_SIZE_FLOAT32;
        result = true;
    }
    else if (offset == CELL_DID_HUMIDITY)
    {
        writeInt32(buf, (int32_t)stateVectorAccumulator.cellDetail[cellNum][DIVEO2_DETAIL_HUMIDITY]);
        *len = DATA_SIZE_FLOAT32;
        result = true;
    }
    else
    {
        /* Not a DiveO2-specific DID */
    }

    return result;
}

/**
 * @brief Handle a cell DID read request
 *
 * @param cellNum Cell number (0-2)
 * @param offset DID offset within cell range (0x00-0x0F)
 * @param cellType Configured cell type
 * @param buf Output buffer
 * @param len Output: bytes written
 * @return true if handled, false if invalid offset or type mismatch
 */
static bool handleCellDID(uint8_t cellNum, uint8_t offset, CellType_t cellType,
                          uint8_t *buf, uint16_t *len)
{
    bool result = false;

    if ((cellNum >= CELL_COUNT) || (offset > CELL_DID_MAX_OFFSET))
    {
        NON_FATAL_ERROR_DETAIL(INVALID_CELL_NUMBER_ERR, cellNum);
    }
    else if (handleUniversalCellDID(cellNum, offset, cellType, buf, len))
    {
        result = true;
    }
    else if ((cellType == CELL_ANALOG) && handleAnalogCellDID(cellNum, offset, buf, len))
    {
        result = true;
    }
    else if ((cellType == CELL_DIVEO2) && handleDiveO2CellDID(cellNum, offset, buf, len))
    {
        result = true;
    }
    else
    {
        /* Expected: Invalid offset or O2S cells with non-universal DID - caller handles NRC */
    }

    return result;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

bool UDS_StateDID_IsStateDID(uint16_t did)
{
    bool result = false;

    /* PPO2 Control State DIDs (0xF2xx) */
    if ((did >= UDS_DID_CONTROL_BASE) && (did <= UDS_DID_CONTROL_END))
    {
        result = true;
    }
    /* Cell DIDs (0xF400-0xF42F) */
    else if ((did >= UDS_DID_CELL_BASE) && (did < (UDS_DID_CELL_BASE + (CELL_COUNT * UDS_DID_CELL_RANGE))))
    {
        result = true;
    }
    else
    {
        /* DID not in state DID range */
    }

    return result;
}

bool UDS_StateDID_HandleRead(uint16_t did, const Configuration_t *config,
                              uint8_t *responseBuffer, uint16_t *responseLength)
{
    bool result = false;

    if ((responseBuffer == NULL) || (responseLength == NULL))
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
    }
    else
    {
        *responseLength = 0U;

        /* PPO2 Control State DIDs (0xF2xx) */
        if ((did >= UDS_DID_CONTROL_BASE) && (did <= UDS_DID_CONTROL_END))
        {
            result = handleControlStateDID(did, config, responseBuffer, responseLength);
        }
        /* Cell DIDs (0xF4Nx) */
        else if ((did >= UDS_DID_CELL_BASE) && (did < (UDS_DID_CELL_BASE + (CELL_COUNT * UDS_DID_CELL_RANGE))))
        {
            uint8_t cellNum = (uint8_t)((did - UDS_DID_CELL_BASE) / UDS_DID_CELL_RANGE);
            uint8_t offset = (uint8_t)((did - UDS_DID_CELL_BASE) % UDS_DID_CELL_RANGE);

            if (cellNum >= CELL_COUNT)
            {
                NON_FATAL_ERROR_DETAIL(UNREACHABLE_ERR, cellNum);
            }
            else
            {
                CellType_t cellType = getCellTypeFromConfig(config, cellNum);
                result = handleCellDID(cellNum, offset, cellType, responseBuffer, responseLength);
            }
        }
        else
        {
            /* Expected: DID not in state DID range - caller handles NRC */
        }
    }

    return result;
}

/**
 * @brief Get the size of a cell DID's data
 *
 * @param offset DID offset within cell range
 * @param cellType Configured cell type
 * @return Size in bytes, or 0 if invalid offset or type mismatch
 */
static uint16_t getCellDIDSize(uint8_t offset, CellType_t cellType)
{
    uint16_t result = 0U;

    /* Universal offsets */
    if (offset == CELL_DID_PPO2)
    {
        result = DATA_SIZE_FLOAT32;
    }
    else if ((offset == CELL_DID_TYPE) || (offset == CELL_DID_INCLUDED) || (offset == CELL_DID_STATUS))
    {
        result = DATA_SIZE_UINT8;
    }
    /* Analog-specific offsets */
    else if ((cellType == CELL_ANALOG) &&
             ((offset == CELL_DID_RAW_ADC) || (offset == CELL_DID_MILLIVOLTS)))
    {
        result = DATA_SIZE_UINT16;
    }
    /* DiveO2-specific offsets */
    else if ((cellType == CELL_DIVEO2) &&
             ((offset >= CELL_DID_TEMPERATURE) && (offset <= CELL_DID_HUMIDITY)))
    {
        result = DATA_SIZE_FLOAT32;
    }
    else
    {
        /* Invalid offset or type mismatch - result remains 0 */
    }

    return result;
}

/**
 * @brief Get the size of a Cell DID by parsing the DID value
 *
 * @param did Data identifier
 * @param config Pointer to current configuration
 * @return Size in bytes, or 0 if invalid
 */
static uint16_t getStateCellDIDSize(uint16_t did, const Configuration_t *config)
{
    uint16_t result = 0U;
    uint8_t cellNum = (uint8_t)((did - UDS_DID_CELL_BASE) / UDS_DID_CELL_RANGE);
    uint8_t offset = (uint8_t)((did - UDS_DID_CELL_BASE) % UDS_DID_CELL_RANGE);

    if (cellNum >= CELL_COUNT)
    {
        NON_FATAL_ERROR_DETAIL(UNREACHABLE_ERR, cellNum);
    }
    else
    {
        CellType_t cellType = getCellTypeFromConfig(config, cellNum);
        result = getCellDIDSize(offset, cellType);

        if (result == 0U)
        {
            NON_FATAL_ERROR_DETAIL(UDS_INVALID_OPTION_ERR, did);
        }
    }

    return result;
}

uint16_t UDS_StateDID_GetSize(uint16_t did, const Configuration_t *config)
{
    uint16_t result = 0U;

    /* PPO2 Control State DIDs */
    switch (did)
    {
    case UDS_DID_CONSENSUS_PPO2:
    case UDS_DID_SETPOINT:
    case UDS_DID_DUTY_CYCLE:
    case UDS_DID_INTEGRAL_STATE:
    case UDS_DID_UPTIME_SEC:
    case UDS_DID_VBUS_VOLTAGE:
    case UDS_DID_VCC_VOLTAGE:
    case UDS_DID_BATTERY_VOLTAGE:
    case UDS_DID_CAN_VOLTAGE:
    case UDS_DID_THRESHOLD_VOLTAGE:
        result = DATA_SIZE_FLOAT32;
        break;

    case UDS_DID_CELLS_VALID:
    case UDS_DID_POWER_SOURCES:
        result = DATA_SIZE_UINT8;
        break;

    case UDS_DID_SATURATION_COUNT:
        result = DATA_SIZE_UINT16;
        break;

    default:
        /* Check Cell DIDs */
        if ((did >= UDS_DID_CELL_BASE) && (did < (UDS_DID_CELL_BASE + (CELL_COUNT * UDS_DID_CELL_RANGE))))
        {
            result = getStateCellDIDSize(did, config);
        }
        /* else: Invalid DID - result remains 0 */
        break;
    }

    return result;
}
