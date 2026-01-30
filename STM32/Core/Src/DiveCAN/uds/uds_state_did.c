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
#include "main.h"
#include <string.h>

/* External reference to state vector accumulator from log.c */
extern BinaryStateVector_t stateVectorAccumulator;

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Get cell type from configuration
 */
static CellType_t getCellTypeFromConfig(const Configuration_t *config, uint8_t cellNum)
{
    if (config == NULL || cellNum > 2U)
    {
        return CELL_ANALOG; /* Safe default */
    }

    switch (cellNum)
    {
    case 0:
        return config->cell1;
    case 1:
        return config->cell2;
    case 2:
        return config->cell3;
    default:
        return CELL_ANALOG;
    }
}

/**
 * @brief Write a float32 to buffer in little-endian format
 */
static void writeFloat32(uint8_t *buf, Numeric_t value)
{
    memcpy(buf, &value, sizeof(Numeric_t));
}

/**
 * @brief Write a uint32 to buffer in little-endian format
 */
static void writeUint32(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value);
    buf[1] = (uint8_t)(value >> 8);
    buf[2] = (uint8_t)(value >> 16);
    buf[3] = (uint8_t)(value >> 24);
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
    buf[1] = (uint8_t)(value >> 8);
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
        writeFloat32(buf, stateVectorAccumulator.consensus_ppo2);
        *len = 4U;
        return true;

    case UDS_DID_SETPOINT:
        writeFloat32(buf, stateVectorAccumulator.setpoint);
        *len = 4U;
        return true;

    case UDS_DID_CELLS_VALID:
        buf[0] = stateVectorAccumulator.cellsValid;
        *len = 1U;
        return true;

    case UDS_DID_DUTY_CYCLE:
        writeFloat32(buf, stateVectorAccumulator.duty_cycle);
        *len = 4U;
        return true;

    case UDS_DID_INTEGRAL_STATE:
        writeFloat32(buf, stateVectorAccumulator.integral_state);
        *len = 4U;
        return true;

    case UDS_DID_SATURATION_COUNT:
        writeUint16(buf, stateVectorAccumulator.saturation_count);
        *len = 2U;
        return true;

    case UDS_DID_UPTIME_SEC:
        writeUint32(buf, HAL_GetTick() / 1000U);
        *len = 4U;
        return true;

    /* Power Monitoring DIDs */
    case UDS_DID_VBUS_VOLTAGE:
        writeFloat32(buf, getVBusVoltage());
        *len = 4U;
        return true;

    case UDS_DID_VCC_VOLTAGE:
        writeFloat32(buf, getVCCVoltage());
        *len = 4U;
        return true;

    case UDS_DID_BATTERY_VOLTAGE:
        writeFloat32(buf, getBatteryVoltage());
        *len = 4U;
        return true;

    case UDS_DID_CAN_VOLTAGE:
        writeFloat32(buf, getCANVoltage());
        *len = 4U;
        return true;

    case UDS_DID_THRESHOLD_VOLTAGE:
        writeFloat32(buf, getThresholdVoltage(config->dischargeThresholdMode));
        *len = 4U;
        return true;

    case UDS_DID_POWER_SOURCES:
    {
        uint8_t sources = (uint8_t)GetVCCSource() | ((uint8_t)GetVBusSource() << 2);
        buf[0] = sources;
        *len = 1U;
        return true;
    }

    default:
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
    if (cellNum > 2U || offset > CELL_DID_MAX_OFFSET)
    {
        return false;
    }

    /* Universal cell DIDs (all types) */
    switch (offset)
    {
    case CELL_DID_PPO2:
        writeFloat32(buf, stateVectorAccumulator.cell_ppo2[cellNum]);
        *len = 4U;
        return true;

    case CELL_DID_TYPE:
        buf[0] = (uint8_t)cellType;
        *len = 1U;
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
        *len = 1U;
        return true;

    case CELL_DID_STATUS:
        buf[0] = stateVectorAccumulator.cell_status[cellNum];
        *len = 1U;
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
            writeInt16(buf, (int16_t)stateVectorAccumulator.cell_detail[cellNum][0]);
            *len = 2U;
            return true;

        case CELL_DID_MILLIVOLTS:
            writeUint16(buf, (uint16_t)stateVectorAccumulator.cell_detail[cellNum][1]);
            *len = 2U;
            return true;

        default:
            /* Invalid offset for ANALOG cell - return NRC */
            return false;
        }
    }

    /* DIVEO2-specific DIDs */
    if (cellType == CELL_DIVEO2)
    {
        switch (offset)
        {
        case CELL_DID_TEMPERATURE:
            writeInt32(buf, (int32_t)stateVectorAccumulator.cell_detail[cellNum][0]);
            *len = 4U;
            return true;

        case CELL_DID_ERROR:
            writeInt32(buf, (int32_t)stateVectorAccumulator.cell_detail[cellNum][1]);
            *len = 4U;
            return true;

        case CELL_DID_PHASE:
            writeInt32(buf, (int32_t)stateVectorAccumulator.cell_detail[cellNum][2]);
            *len = 4U;
            return true;

        case CELL_DID_INTENSITY:
            writeInt32(buf, (int32_t)stateVectorAccumulator.cell_detail[cellNum][3]);
            *len = 4U;
            return true;

        case CELL_DID_AMBIENT_LIGHT:
            writeInt32(buf, (int32_t)stateVectorAccumulator.cell_detail[cellNum][4]);
            *len = 4U;
            return true;

        case CELL_DID_PRESSURE:
            writeInt32(buf, (int32_t)stateVectorAccumulator.cell_detail[cellNum][5]);
            *len = 4U;
            return true;

        case CELL_DID_HUMIDITY:
            writeInt32(buf, (int32_t)stateVectorAccumulator.cell_detail[cellNum][6]);
            *len = 4U;
            return true;

        default:
            /* Invalid offset for DIVEO2 cell - return NRC */
            return false;
        }
    }

    /* O2S cells only support universal DIDs (already handled above) */
    /* Any other offset for O2S is invalid */
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
    if ((did >= UDS_DID_CELL_BASE) && (did < (UDS_DID_CELL_BASE + (3U * UDS_DID_CELL_RANGE))))
    {
        return true;
    }

    return false;
}

bool UDS_StateDID_HandleRead(uint16_t did, const Configuration_t *config,
                              uint8_t *responseBuffer, uint16_t *responseLength)
{
    if (responseBuffer == NULL || responseLength == NULL)
    {
        return false;
    }

    *responseLength = 0U;

    /* PPO2 Control State DIDs (0xF2xx) */
    if ((did >= UDS_DID_CONTROL_BASE) && (did <= UDS_DID_CONTROL_END))
    {
        return handleControlStateDID(did, config, responseBuffer, responseLength);
    }

    /* Cell DIDs (0xF4Nx) */
    if ((did >= UDS_DID_CELL_BASE) && (did < (UDS_DID_CELL_BASE + (3U * UDS_DID_CELL_RANGE))))
    {
        uint8_t cellNum = (uint8_t)((did - UDS_DID_CELL_BASE) / UDS_DID_CELL_RANGE);
        uint8_t offset = (uint8_t)((did - UDS_DID_CELL_BASE) % UDS_DID_CELL_RANGE);

        if (cellNum > 2U)
        {
            return false;
        }

        CellType_t cellType = getCellTypeFromConfig(config, cellNum);
        return handleCellDID(cellNum, offset, cellType, responseBuffer, responseLength);
    }

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
        return 4U;

    case UDS_DID_CELLS_VALID:
    case UDS_DID_POWER_SOURCES:
        return 1U;

    case UDS_DID_SATURATION_COUNT:
        return 2U;

    default:
        break;
    }

    /* Cell DIDs */
    if ((did >= UDS_DID_CELL_BASE) && (did < (UDS_DID_CELL_BASE + (3U * UDS_DID_CELL_RANGE))))
    {
        uint8_t cellNum = (uint8_t)((did - UDS_DID_CELL_BASE) / UDS_DID_CELL_RANGE);
        uint8_t offset = (uint8_t)((did - UDS_DID_CELL_BASE) % UDS_DID_CELL_RANGE);

        if (cellNum > 2U)
        {
            return 0U;
        }

        CellType_t cellType = getCellTypeFromConfig(config, cellNum);

        /* Universal offsets */
        switch (offset)
        {
        case CELL_DID_PPO2:
            return 4U;
        case CELL_DID_TYPE:
        case CELL_DID_INCLUDED:
        case CELL_DID_STATUS:
            return 1U;
        default:
            break;
        }

        /* Type-specific offsets */
        if (cellType == CELL_ANALOG)
        {
            if (offset == CELL_DID_RAW_ADC || offset == CELL_DID_MILLIVOLTS)
            {
                return 2U;
            }
        }
        else if (cellType == CELL_DIVEO2)
        {
            if (offset >= CELL_DID_TEMPERATURE && offset <= CELL_DID_HUMIDITY)
            {
                return 4U;
            }
        }
    }

    return 0U; /* Invalid DID or type mismatch */
}
