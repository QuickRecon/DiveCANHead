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
#define UDS_DID_SOLENOID_OVERRIDE   0xF242U  /**< write-only [channel,magic 0x5A]: HIL — fire one raw solenoid channel for a fixed ~1.5 s (can't lock on); refused unless PPO2 mode is OFF + programming session + !in_dive */

/* Crash-info DIDs (0xF25x) — populated from errors_get_last_crash() */
#define UDS_DID_CRASH_VALID         0xF250U  /**< uint8: 1 if last boot was a crash, else 0 */
#define UDS_DID_CRASH_REASON        0xF251U  /**< uint32: K_ERR_* / FatalOpError_t code */
#define UDS_DID_CRASH_PC            0xF252U  /**< uint32: program counter at fault */
#define UDS_DID_CRASH_LR            0xF253U  /**< uint32: link register at fault */
#define UDS_DID_CRASH_CFSR          0xF254U  /**< uint32: Cortex-M Configurable Fault Status Register */

/* Error-histogram DIDs (0xF26x) — populated from error_histogram_snapshot() */
#define UDS_DID_ERROR_HISTOGRAM       0xF260U  /**< uint16[OP_ERR_MAX]: per-code occurrence counts (saturated) */
#define UDS_DID_ERROR_HISTOGRAM_CLEAR 0xF261U  /**< write-only: any value clears all counters and persists to NVS */

/* OTA / MCUBoot status DIDs (0xF27x) — populated from boot_*, firmware_confirm_*, factory_image_* */
#define UDS_DID_MCUBOOT_STATUS        0xF270U  /**< 16 B: swap_type, confirmed, slot, factory flag, slot0/slot1/factory versions (4 B each, truncated to major/minor/rev_lo/rev_hi) */
#define UDS_DID_POST_STATUS           0xF271U  /**< 4 B: PostState_t, pass-mask (low 8 bits), reserved x2 */
#define UDS_DID_OTA_VERSION           0xF272U  /**< 8 B: slot0 sem_ver (major/minor/rev16/build32) */
#define UDS_DID_OTA_PENDING_VERSION   0xF273U  /**< 8 B: slot1 sem_ver, all 0xFF if slot1 has no valid header */
#define UDS_DID_OTA_FACTORY_VERSION   0xF274U  /**< 8 B: factory backup sem_ver, all 0xFF if not captured */
#define UDS_DID_OTA_FORCE_REVERT      0xF275U  /**< write-only, magic 0x01: re-stage slot1 (1-step rollback) */
#define UDS_DID_OTA_RESTORE_FACTORY   0xF276U  /**< write-only, magic 0x01: copy factory backup into slot1 + reboot */
#define UDS_DID_OTA_FACTORY_CAPTURE   0xF277U  /**< write-only, magic 0x01: force re-capture of slot0 into factory backup */
#define UDS_DID_FACTORY_FLASH_ERASE   0xF278U  /**< write-only, magic 0x01: chip-erase the external NOR (slot1/factory/log/NVS) + reboot */
#define UDS_DID_NVS_ERASE             0xF279U  /**< write-only, magic 0x01: erase ONLY the NVS/settings (storage) partition + reboot; keeps flash log + OTA slot1/factory (cal lives in NVS, so it is cleared too) */

/* Flash log management DIDs (0xF28x) — see docs/FLASH_LOG.md */
#define UDS_DID_LOG_STATS             0xF280U  /**< 48 B: FlashLogStats_t per-FCB breakdown */
#define UDS_DID_LOG_SELECTOR_RESULT   0xF281U  /**< 20 B: stream/start/end/count/bytes/status of the live selection */
#define UDS_DID_LOG_ERASE             0xF282U  /**< write-only, 2 B: stream_mask u8 + magic 0xA5; gated to programming + !in_dive */
#define UDS_DID_LOG_VERBOSITY         0xF283U  /**< RW, 1 B: text-FCB min level (1=ERR..4=DBG), persisted to NVS */
#define UDS_DID_LOG_CAN_VERBOSE       0xF284U  /**< RW, 1 B: CAN-capture bitmask (bit0=RX, bit1=TX), persisted to NVS */

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
/* Highest READABLE cell offset; CELL_DID_BROADCAST below is write-only and is
 * handled by the WDBI dispatcher, not the read path. */
#define CELL_DID_MAX_OFFSET         0x0CU
#define CELL_DID_BROADCAST          0x0DU  /**< write-only, 1 B: 0=stop, nonzero=start this cell's UART broadcast (sends #BCST) */

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
 * @param maxLength      Maximum bytes the handler may write into responseBuffer
 * @param responseLength Set to the number of bytes written on success
 * @return true on success, false if DID unknown, data unavailable, or the
 *         payload would not fit within @p maxLength bytes
 */
bool UDS_StateDID_HandleRead(uint16_t did, uint8_t *responseBuffer,
                 uint16_t maxLength,
                 uint16_t *responseLength);

#endif /* UDS_STATE_DID_H */
