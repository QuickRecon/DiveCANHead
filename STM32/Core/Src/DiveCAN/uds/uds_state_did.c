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
    if ((config == NULL) || (cellNum >= CELL_COUNT))
    {
        NON_FATAL_ERROR_DETAIL(INVALID_CELL_NUMBER_ERR, cellNum);
        return CELL_ANALOG; /* Safe default */
    }

    switch (cellNum)
    {
    case CELL_1:
        return config->cell1;
    case CELL_2:
        return config->cell2;
    case CELL_3:
        return config->cell3;
    default:
        /* Unreachable: cellNum bounds checked above */
        return CELL_ANALOG;
    }
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
    switch (did)
    {
    case UDS_DID_CONSENSUS_PPO2:
        writeFloat32(buf, stateVectorAccumulator.consensusPpo2);
        *len = DATA_SIZE_FLOAT32;
        return true;

    case UDS_DID_SETPOINT:
        writeFloat32(buf, stateVectorAccumulator.setpoint);
        *len = DATA_SIZE_FLOAT32;
        return true;

    case UDS_DID_CELLS_VALID:
        buf[0] = stateVectorAccumulator.cellsValid;
        *len = DATA_SIZE_UINT8;
        return true;

    case UDS_DID_DUTY_CYCLE:
        writeFloat32(buf, stateVectorAccumulator.dutyCycle);
        *len = DATA_SIZE_FLOAT32;
        return true;

    case UDS_DID_INTEGRAL_STATE:
        writeFloat32(buf, stateVectorAccumulator.integralState);
        *len = DATA_SIZE_FLOAT32;
        return true;

    case UDS_DID_SATURATION_COUNT:
        writeUint16(buf, stateVectorAccumulator.saturationCount);
        *len = DATA_SIZE_UINT16;
        return true;

    case UDS_DID_UPTIME_SEC:
        writeUint32(buf, HAL_GetTick() / MS_PER_SECOND);
        *len = DATA_SIZE_FLOAT32;
        return true;

    /* Power Monitoring DIDs */
    case UDS_DID_VBUS_VOLTAGE:
        writeFloat32(buf, getVBusVoltage());
        *len = DATA_SIZE_FLOAT32;
        return true;

    case UDS_DID_VCC_VOLTAGE:
        writeFloat32(buf, getVCCVoltage());
        *len = DATA_SIZE_FLOAT32;
        return true;

    case UDS_DID_BATTERY_VOLTAGE:
        writeFloat32(buf, getBatteryVoltage());
        *len = DATA_SIZE_FLOAT32;
        return true;

    case UDS_DID_CAN_VOLTAGE:
        writeFloat32(buf, getCANVoltage());
        *len = DATA_SIZE_FLOAT32;
        return true;

    case UDS_DID_THRESHOLD_VOLTAGE:
        writeFloat32(buf, getThresholdVoltage(config->dischargeThresholdMode));
        *len = DATA_SIZE_FLOAT32;
        return true;

    case UDS_DID_POWER_SOURCES:
    {
        uint8_t sources = (uint8_t)((uint8_t)GetVCCSource() | ((uint8_t)GetVBusSource() << VBUS_SOURCE_BIT_OFFSET));
        buf[0] = sources;
        *len = DATA_SIZE_UINT8;
        return true;
    }

    default:
        /* Expected: Unknown DID within control state range - caller handles NRC */
        return false;
    }
}

/* ============================================================================
 * Cell DID Handlers (0xF4Nx)
 * ============================================================================ */

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
    if ((cellNum >= CELL_COUNT) || (offset > CELL_DID_MAX_OFFSET))
    {
        NON_FATAL_ERROR_DETAIL(INVALID_CELL_NUMBER_ERR, cellNum);
        return false;
    }

    /* Universal cell DIDs (all types) */
    switch (offset)
    {
    case CELL_DID_PPO2:
        writeFloat32(buf, stateVectorAccumulator.cellPpo2[cellNum]);
        *len = DATA_SIZE_FLOAT32;
        return true;

    case CELL_DID_TYPE:
        buf[0] = (uint8_t)cellType;
        *len = DATA_SIZE_UINT8;
        return true;

    case CELL_DID_INCLUDED:
        if ((stateVectorAccumulator.cellsValid & (1U << cellNum)) != 0U)
        {
            buf[0] = 1U;
        }
        else
        {
            buf[0] = 0U;
        }
        *len = DATA_SIZE_UINT8;
        return true;

    case CELL_DID_STATUS:
        buf[0] = stateVectorAccumulator.cellStatus[cellNum];
        *len = DATA_SIZE_UINT8;
        return true;

    default:
        break; /* Fall through to type-specific handling */
    }

    /* ANALOG-specific DIDs */
    if (cellType == CELL_ANALOG)
    {
        switch (offset)
        {
        case CELL_DID_RAW_ADC:
            writeInt16(buf, (int16_t)stateVectorAccumulator.cellDetail[cellNum][ANALOG_DETAIL_RAW_ADC]);
            *len = DATA_SIZE_UINT16;
            return true;

        case CELL_DID_MILLIVOLTS:
            writeUint16(buf, (uint16_t)stateVectorAccumulator.cellDetail[cellNum][ANALOG_DETAIL_MILLIVOLTS]);
            *len = DATA_SIZE_UINT16;
            return true;

        default:
            /* Expected: Invalid offset for ANALOG cell - caller handles NRC */
            return false;
        }
    }

    /* DIVEO2-specific DIDs */
    if (cellType == CELL_DIVEO2)
    {
        switch (offset)
        {
        case CELL_DID_TEMPERATURE:
            writeInt32(buf, (int32_t)stateVectorAccumulator.cellDetail[cellNum][DIVEO2_DETAIL_TEMPERATURE]);
            *len = DATA_SIZE_FLOAT32;
            return true;

        case CELL_DID_ERROR:
            writeInt32(buf, (int32_t)stateVectorAccumulator.cellDetail[cellNum][DIVEO2_DETAIL_ERROR]);
            *len = DATA_SIZE_FLOAT32;
            return true;

        case CELL_DID_PHASE:
            writeInt32(buf, (int32_t)stateVectorAccumulator.cellDetail[cellNum][DIVEO2_DETAIL_PHASE]);
            *len = DATA_SIZE_FLOAT32;
            return true;

        case CELL_DID_INTENSITY:
            writeInt32(buf, (int32_t)stateVectorAccumulator.cellDetail[cellNum][DIVEO2_DETAIL_INTENSITY]);
            *len = DATA_SIZE_FLOAT32;
            return true;

        case CELL_DID_AMBIENT_LIGHT:
            writeInt32(buf, (int32_t)stateVectorAccumulator.cellDetail[cellNum][DIVEO2_DETAIL_AMBIENT_LIGHT]);
            *len = DATA_SIZE_FLOAT32;
            return true;

        case CELL_DID_PRESSURE:
            writeInt32(buf, (int32_t)stateVectorAccumulator.cellDetail[cellNum][DIVEO2_DETAIL_PRESSURE]);
            *len = DATA_SIZE_FLOAT32;
            return true;

        case CELL_DID_HUMIDITY:
            writeInt32(buf, (int32_t)stateVectorAccumulator.cellDetail[cellNum][DIVEO2_DETAIL_HUMIDITY]);
            *len = DATA_SIZE_FLOAT32;
            return true;

        default:
            /* Expected: Invalid offset for DIVEO2 cell - caller handles NRC */
            return false;
        }
    }

    /* Expected: O2S cells only support universal DIDs (already handled above).
     * Any other offset for O2S is invalid - caller handles NRC */
    return false;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

bool UDS_StateDID_IsStateDID(uint16_t did)
{
    /* PPO2 Control State DIDs (0xF2xx) */
    if ((did >= UDS_DID_CONTROL_BASE) && (did <= UDS_DID_CONTROL_END))
    {
        return true;
    }

    /* Cell DIDs (0xF400-0xF42F) */
    if ((did >= UDS_DID_CELL_BASE) && (did < (UDS_DID_CELL_BASE + (CELL_COUNT * UDS_DID_CELL_RANGE))))
    {
        return true;
    }

    return false;
}

bool UDS_StateDID_HandleRead(uint16_t did, const Configuration_t *config,
                              uint8_t *responseBuffer, uint16_t *responseLength)
{
    if ((responseBuffer == NULL) || (responseLength == NULL))
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
        return false;
    }

    *responseLength = 0U;

    /* PPO2 Control State DIDs (0xF2xx) */
    if ((did >= UDS_DID_CONTROL_BASE) && (did <= UDS_DID_CONTROL_END))
    {
        return handleControlStateDID(did, config, responseBuffer, responseLength);
    }

    /* Cell DIDs (0xF4Nx) */
    if ((did >= UDS_DID_CELL_BASE) && (did < (UDS_DID_CELL_BASE + (CELL_COUNT * UDS_DID_CELL_RANGE))))
    {
        uint8_t cellNum = (uint8_t)((did - UDS_DID_CELL_BASE) / UDS_DID_CELL_RANGE);
        uint8_t offset = (uint8_t)((did - UDS_DID_CELL_BASE) % UDS_DID_CELL_RANGE);

        if (cellNum >= CELL_COUNT)
        {
            NON_FATAL_ERROR_DETAIL(UNREACHABLE_ERR, cellNum);
            return false;
        }

        CellType_t cellType = getCellTypeFromConfig(config, cellNum);
        return handleCellDID(cellNum, offset, cellType, responseBuffer, responseLength);
    }

    /* Expected: DID not in state DID range - caller handles NRC */
    return false;
}

uint16_t UDS_StateDID_GetSize(uint16_t did, const Configuration_t *config)
{
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
        return DATA_SIZE_FLOAT32;

    case UDS_DID_CELLS_VALID:
    case UDS_DID_POWER_SOURCES:
        return DATA_SIZE_UINT8;

    case UDS_DID_SATURATION_COUNT:
        return DATA_SIZE_UINT16;

    default:
        break;
    }

    /* Cell DIDs */
    if ((did >= UDS_DID_CELL_BASE) && (did < (UDS_DID_CELL_BASE + (CELL_COUNT * UDS_DID_CELL_RANGE))))
    {
        uint8_t cellNum = (uint8_t)((did - UDS_DID_CELL_BASE) / UDS_DID_CELL_RANGE);
        uint8_t offset = (uint8_t)((did - UDS_DID_CELL_BASE) % UDS_DID_CELL_RANGE);

        if (cellNum >= CELL_COUNT)
        {
            NON_FATAL_ERROR_DETAIL(UNREACHABLE_ERR, cellNum);
            return 0U;
        }

        CellType_t cellType = getCellTypeFromConfig(config, cellNum);

        /* Universal offsets */
        switch (offset)
        {
        case CELL_DID_PPO2:
            return DATA_SIZE_FLOAT32;
        case CELL_DID_TYPE:
        case CELL_DID_INCLUDED:
        case CELL_DID_STATUS:
            return DATA_SIZE_UINT8;
        default:
            break;
        }

        /* Type-specific offsets */
        if ((cellType == CELL_ANALOG) &&
            ((offset == CELL_DID_RAW_ADC) || (offset == CELL_DID_MILLIVOLTS)))
        {
            return DATA_SIZE_UINT16;
        }
        else if ((cellType == CELL_DIVEO2) &&
                 ((offset >= CELL_DID_TEMPERATURE) && (offset <= CELL_DID_HUMIDITY)))
        {
            return DATA_SIZE_FLOAT32;
        }
        else
        {
            NON_FATAL_ERROR_DETAIL(UDS_INVALID_OPTION_ERR, did);
        }
    }

    /* Expected: Invalid DID or type mismatch - return 0 to indicate unknown size */
    return 0U;
}
