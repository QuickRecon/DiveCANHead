/**
 * @file uds_state_did.h
 * @brief UDS State Data Identifier (DID) handler
 *
 * Provides read access to system state via individual DIDs.
 * Data is sourced from zbus channels and power management API.
 *
 * DID Ranges:
 * - 0xF2xx: PPO2 control state (consensus, setpoint, duty, PID state)
 * - 0xF4Nx: Per-cell data (N = cell number 0-2, offset 0x00-0x0F)
 */

#ifndef UDS_STATE_DID_H
#define UDS_STATE_DID_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * PPO2 Control State DIDs (0xF2xx)
 * ============================================================================ */
#define UDS_DID_CONTROL_BASE        0xF200U
#define UDS_DID_CONTROL_END         0xF2FFU

#define UDS_DID_CONSENSUS_PPO2      0xF200U  /**< float32: Voted PPO2 (bar) */
#define UDS_DID_SETPOINT            0xF202U  /**< float32: Current setpoint (bar) */
#define UDS_DID_CELLS_VALID         0xF203U  /**< uint8: Bitfield - cells in voting */
#define UDS_DID_DUTY_CYCLE          0xF210U  /**< float32: Solenoid duty (0.0-1.0) */
#define UDS_DID_INTEGRAL_STATE      0xF211U  /**< float32: PID integral accumulator */
#define UDS_DID_SATURATION_COUNT    0xF212U  /**< uint16: PID saturation events */
#define UDS_DID_UPTIME_SEC          0xF220U  /**< uint32: Seconds since boot */

/* Power Monitoring DIDs (0xF23x) */
#define UDS_DID_VBUS_VOLTAGE        0xF230U
#define UDS_DID_VCC_VOLTAGE         0xF231U
#define UDS_DID_BATTERY_VOLTAGE     0xF232U
#define UDS_DID_CAN_VOLTAGE         0xF233U
#define UDS_DID_THRESHOLD_VOLTAGE   0xF234U
#define UDS_DID_POWER_SOURCES       0xF235U

/* Control DIDs (writable) - 0xF24x */
#define UDS_DID_SETPOINT_WRITE      0xF240U
#define UDS_DID_CALIBRATION_TRIGGER 0xF241U

/* Crash-info DIDs (0xF25x) — populated from errors_get_last_crash() */
#define UDS_DID_CRASH_VALID         0xF250U  /**< uint8: 1 if last boot was a crash, else 0 */
#define UDS_DID_CRASH_REASON        0xF251U  /**< uint32: K_ERR_* / FatalOpError_t code */
#define UDS_DID_CRASH_PC            0xF252U  /**< uint32: program counter at fault */
#define UDS_DID_CRASH_LR            0xF253U  /**< uint32: link register at fault */
#define UDS_DID_CRASH_CFSR          0xF254U  /**< uint32: Cortex-M Configurable Fault Status Register */

/* ============================================================================
 * Cell DIDs (0xF4Nx where N = cell number 0-2)
 * ============================================================================ */
#define UDS_DID_CELL_BASE           0xF400U
#define UDS_DID_CELL_RANGE          0x0010U

/* Cell DID offsets */
#define CELL_DID_PPO2               0x00U
#define CELL_DID_TYPE               0x01U
#define CELL_DID_INCLUDED           0x02U
#define CELL_DID_STATUS             0x03U
#define CELL_DID_RAW_ADC            0x04U
#define CELL_DID_MILLIVOLTS         0x05U
#define CELL_DID_TEMPERATURE        0x06U
#define CELL_DID_ERROR              0x07U
#define CELL_DID_PHASE              0x08U
#define CELL_DID_INTENSITY          0x09U
#define CELL_DID_AMBIENT_LIGHT      0x0AU
#define CELL_DID_PRESSURE           0x0BU
#define CELL_DID_HUMIDITY           0x0CU
#define CELL_DID_MAX_OFFSET         0x0CU

/**
 * @brief Check whether a DID falls within the state-DID ranges handled by this module.
 *
 * @param did UDS data identifier to test
 * @return true if this module handles the DID (0xF2xx or 0xF4Nx)
 */
bool UDS_StateDID_IsStateDID(uint16_t did);

/**
 * @brief Handle a ReadDataByIdentifier request for a state DID.
 *
 * Reads the current value from the appropriate zbus channel or power API
 * and encodes it into responseBuffer.
 *
 * @param did            UDS data identifier to read
 * @param responseBuffer Buffer to write encoded value into
 * @param responseLength Set to the number of bytes written on success
 * @return true on success, false if DID unknown or data unavailable
 */
bool UDS_StateDID_HandleRead(uint16_t did, uint8_t *responseBuffer,
                 uint16_t *responseLength);

#endif /* UDS_STATE_DID_H */
