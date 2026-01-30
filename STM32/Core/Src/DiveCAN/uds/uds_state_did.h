/**
 * @file uds_state_did.h
 * @brief UDS State Data Identifier (DID) handler
 *
 * Provides read access to system state via individual DIDs instead of
 * the legacy binary state vector push mechanism.
 *
 * DID Ranges:
 * - 0xF2xx: PPO2 control state (consensus, setpoint, duty, PID state)
 * - 0xF4Nx: Per-cell data (N = cell number 0-2, offset 0x00-0x0F)
 */

#ifndef UDS_STATE_DID_H
#define UDS_STATE_DID_H

#include <stdint.h>
#include <stdbool.h>
#include "../../configuration.h"
#include "../../common.h"

/* ============================================================================
 * PPO2 Control State DIDs (0xF2xx)
 * ============================================================================ */
#define UDS_DID_CONTROL_BASE        0xF200U  /**< Base address for control state DIDs */
#define UDS_DID_CONTROL_END         0xF2FFU  /**< End address for control state DIDs */

#define UDS_DID_CONSENSUS_PPO2      0xF200U  /**< float32: Voted PPO2 (bar) */
#define UDS_DID_SETPOINT            0xF202U  /**< float32: Current setpoint (bar) */
#define UDS_DID_CELLS_VALID         0xF203U  /**< uint8: Bitfield - cells in voting */
#define UDS_DID_DUTY_CYCLE          0xF210U  /**< float32: Solenoid duty (0.0-1.0) */
#define UDS_DID_INTEGRAL_STATE      0xF211U  /**< float32: PID integral accumulator */
#define UDS_DID_SATURATION_COUNT    0xF212U  /**< uint16: PID saturation events */
#define UDS_DID_UPTIME_SEC          0xF220U  /**< uint32: Seconds since boot */

/* Power Monitoring DIDs (0xF23x) */
#define UDS_DID_VBUS_VOLTAGE        0xF230U  /**< float32: VBus rail voltage (V) */
#define UDS_DID_VCC_VOLTAGE         0xF231U  /**< float32: VCC rail voltage (V) */
#define UDS_DID_BATTERY_VOLTAGE     0xF232U  /**< float32: Battery voltage (V) */
#define UDS_DID_CAN_VOLTAGE         0xF233U  /**< float32: CAN bus voltage (V) */
#define UDS_DID_THRESHOLD_VOLTAGE   0xF234U  /**< float32: Low-voltage threshold (V) */
#define UDS_DID_POWER_SOURCES       0xF235U  /**< uint8: VCC src (0-1) | VBUS src (2-3) */

/* Control DIDs (writable) - 0xF24x */
#define UDS_DID_SETPOINT_WRITE      0xF240U  /**< uint8: Write setpoint (0-255 = 0.00-2.55 bar) */
#define UDS_DID_CALIBRATION_TRIGGER 0xF241U  /**< uint8: Trigger calibration with fO2 (0-100%) */

/* ============================================================================
 * Cell DIDs (0xF4Nx where N = cell number 0-2)
 * ============================================================================ */
#define UDS_DID_CELL_BASE           0xF400U  /**< Base address for cell DIDs */
#define UDS_DID_CELL_RANGE          0x0010U  /**< 16 DIDs per cell */

/* Cell DID offsets (add to CELL_BASE + (cellNum * CELL_RANGE)) */
#define CELL_DID_PPO2               0x00U  /**< float32: Cell PPO2 (all types) */
#define CELL_DID_TYPE               0x01U  /**< uint8: Cell type enum (all types) */
#define CELL_DID_INCLUDED           0x02U  /**< uint8/bool: In voting (all types) */
#define CELL_DID_STATUS             0x03U  /**< uint8: Cell status enum (all types) */
#define CELL_DID_RAW_ADC            0x04U  /**< int16: Raw ADC value (ANALOG only) */
#define CELL_DID_MILLIVOLTS         0x05U  /**< uint16: Millivolts (ANALOG only) */
#define CELL_DID_TEMPERATURE        0x06U  /**< int32: Temp millicelsius (DIVEO2 only) */
#define CELL_DID_ERROR              0x07U  /**< int32: Error code (DIVEO2 only) */
#define CELL_DID_PHASE              0x08U  /**< int32: Phase value (DIVEO2 only) */
#define CELL_DID_INTENSITY          0x09U  /**< int32: Intensity (DIVEO2 only) */
#define CELL_DID_AMBIENT_LIGHT      0x0AU  /**< int32: Ambient light (DIVEO2 only) */
#define CELL_DID_PRESSURE           0x0BU  /**< int32: Pressure microbar (DIVEO2 only) */
#define CELL_DID_HUMIDITY           0x0CU  /**< int32: Humidity milliRH (DIVEO2 only) */

/* Maximum cell DID offset */
#define CELL_DID_MAX_OFFSET         0x0CU

/* ============================================================================
 * State Data Accumulator Structure
 * ============================================================================ */

/**
 * @brief State accumulator for DID-based data access
 *
 * Contains complete system state updated by various tasks.
 * Used as the data source for state DID reads.
 *
 * Cell type is determined from config field (bits 8-13, 2 bits per cell):
 *   - CELL_ANALOG (1): detail[0] = raw ADC value (int16 in low bits)
 *   - CELL_O2S (2): no detail fields used
 *   - CELL_DIVEO2 (0): detail[0-6] = temp, err, phase, intensity, ambientLight, pressure, humidity
 *
 * Fields ordered for natural alignment (4-byte, then 2-byte, then 1-byte).
 */
typedef struct __attribute__((packed))
{
    /* 4-byte aligned fields (112 bytes) */
    uint32_t config;               /**< Full Configuration_t bitfield (cell types in bits 8-13) */
    PrecisionPPO2_t consensusPpo2; /**< Voted PPO2 value */
    PrecisionPPO2_t setpoint;       /**< Current setpoint */
    Percent_t dutyCycle;           /**< Solenoid duty cycle (0.0-1.0) */
    PIDHalfNumeric_t integralState; /**< PID integral accumulator */
    PrecisionPPO2_t cellPpo2[3];   /**< Per-cell PPO2 (float precision from precisionPPO2) */
    uint32_t cellDetail[3][7]; /**< Per-cell detail fields (interpretation depends on cell type) */

    /* 2-byte aligned fields (4 bytes) */
    uint16_t timestampSec;    /**< Seconds since boot (wraps at ~18 hours) */
    uint16_t saturationCount; /**< PID saturation event counter */

    /* 1-byte fields (5 bytes) */
    uint8_t version;           /**< Protocol version (for client compatibility) */
    uint8_t cellsValid;        /**< Bit flags: which cells included in voting (bits 0-2) */
    uint8_t cellStatus[3];    /**< Per-cell status (CellStatus_t enum values) */
} BinaryStateVector_t;

/* Expected size of BinaryStateVector_t for protocol compatibility */
#define BINARY_STATE_VECTOR_SIZE 125U
_Static_assert(sizeof(BinaryStateVector_t) == BINARY_STATE_VECTOR_SIZE, "BinaryStateVector_t size must be 125 bytes");

/**
 * @brief Check if a DID is handled by the state DID module
 *
 * @param did Data identifier to check
 * @return true if this module handles the DID, false otherwise
 */
bool UDS_StateDID_IsStateDID(uint16_t did);

/**
 * @brief Handle a state DID read request
 *
 * Reads the requested state value and writes it to the response buffer.
 * Returns NRC 0x31 (Request Out of Range) for:
 * - Invalid DIDs
 * - Cell-type-specific DIDs when cell is configured as different type
 *
 * @param did Data identifier to read
 * @param config Pointer to current configuration (for cell type info)
 * @param responseBuffer Buffer to write response data (caller must ensure adequate size)
 * @param responseLength Output: number of bytes written to responseBuffer
 * @return true if DID handled successfully, false if DID not found or type mismatch
 */
bool UDS_StateDID_HandleRead(uint16_t did, const Configuration_t *config,
                              uint8_t *responseBuffer, uint16_t *responseLength);

/**
 * @brief Get the size in bytes of a state DID's data
 *
 * @param did Data identifier
 * @param config Pointer to current configuration (for cell type validation)
 * @return Size in bytes, or 0 if DID is invalid or type mismatch
 */
uint16_t UDS_StateDID_GetSize(uint16_t did, const Configuration_t *config);

#endif /* UDS_STATE_DID_H */
