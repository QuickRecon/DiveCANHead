/**
 * @file divecan_types.h
 * @brief Core DiveCAN protocol types: CAN IDs, device enums, message structs.
 *
 * Shared by all DiveCAN module sources. Defines the message ID constants,
 * DiveCANType_t / DiveCANError_t / DiveCANManufacturer_t enumerations,
 * the DiveCANMessage_t wire-format struct, and protocol bit-width constants.
 */
#ifndef DIVECAN_TYPES_H
#define DIVECAN_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#include "oxygen_cell_types.h"

/* ---- CAN ID mask ---- */

/** @brief Mask applied to incoming CAN IDs to extract message type, ignoring src/dst nibbles. */
#define DIVECAN_ID_MASK 0x1FFFF000U

/* ---- DiveCAN Message IDs ---- */

#define BUS_ID_ID              0x0D000000U
#define BUS_NAME_ID            0x0D010000U
#define BUS_OFF_ID             0x0D030000U
#define PPO2_PPO2_ID           0x0D040000U
#define HUD_STAT_ID            0x0D070000U
#define PPO2_ATMOS_ID          0x0D080000U
#define MENU_ID                0x0D0A0000U
#define TANK_PRESSURE_ID       0x0D0B0000U
#define PPO2_MILLIS_ID         0x0D110000U
#define CAL_ID                 0x0D120000U
#define CAL_REQ_ID             0x0D130000U
#define CO2_STATUS_ID          0x0D200000U
#define CO2_ID                 0x0D210000U
#define CO2_CAL_ID             0x0D220000U
#define CO2_CAL_REQ_ID         0x0D230000U
#define BUS_MENU_OPEN_ID       0x0D300000U
#define BUS_INIT_ID            0x0D370000U
#define RMS_TEMP_ID            0x0DC10000U
#define RMS_TEMP_ENABLED_ID    0x0DC40000U
#define PPO2_SETPOINT_ID       0x0DC90000U
#define PPO2_STATUS_ID         0x0DCA0000U
#define BUS_STATUS_ID          0x0DCB0000U
#define DIVING_ID              0x0DCC0000U
#define CAN_SERIAL_NUMBER_ID   0x0DD20000U

/* Extensions — not part of the standard but used for debugging
 * and advanced features the Shearwater doesn't natively support.
 * Higher IDs so they get arbitrated away if other things are happening. */
#define LOG_TEXT_ID            0x0F000000U
#define PID_P_GAIN_ID          0x0F100000U
#define PID_I_GAIN_ID          0x0F110000U
#define PID_D_GAIN_ID          0x0F120000U
#define PID_I_STATE_ID         0x0F130000U
#define PID_D_STATE_ID         0x0F140000U
#define SOLENOID_DUTY_ID       0x0F150000U
#define PRECISION_CONSENSUS_ID 0x0F160000U
#define PRECISION_CELL_1_ID    0x0F200000U
#define PRECISION_CELL_2_ID    0x0F210000U
#define PRECISION_CELL_3_ID    0x0F220000U

/* ---- CAN frame constants ---- */

/** @brief Maximum CAN frame data length (standard CAN, not CAN FD). */
#define MAX_CAN_RX_LENGTH 8U

/* ---- Device types ---- */

/**
 * @enum DiveCANType_t
 * @brief This enum represents different types of devices connected via CAN bus.
 *        Each value corresponds to a specific device type.
 */
typedef enum {
    /** @brief DiveCAN controller device (like a Petrel) */
    DIVECAN_CONTROLLER = 1,

    /** @brief DiveCAN Oboe (Oxygen BOard and Electronics) device */
    DIVECAN_OBOE = 2,

    /** @brief DiveCAN Monitor device (like a HUD) */
    DIVECAN_MONITOR = 3,

    /** @brief DiveCAN SOLO (SOLenoid and Oxygen) device */
    DIVECAN_SOLO = 4,

    /** @brief DiveCAN Revo (RMS/battery box) device */
    DIVECAN_REVO = 5
} DiveCANType_t;

/**
 * @enum DiveCANError_t
 * @brief Enum representing potential errors of a DiveCAN device.
 */
typedef enum {
    /** @brief Hides battery status. */
    DIVECAN_ERR_NONE = 0,

    /** @brief Error indicating low battery status. */
    DIVECAN_ERR_BAT_LOW = 0x01,

    /** @brief Battery information is valid and should be shown. */
    DIVECAN_ERR_BAT_NORM = 0x02,

    /** @brief Battery information is valid and should be shown. */
    DIVECAN_ERR_BAT_HIGH = 0x03,

    /** @brief Error indicating solenoid undercurrent */
    DIVECAN_ERR_SOL_UNDERCURRENT = 0x04,

    /** @brief Indicates solenoid is okay */
    DIVECAN_ERR_SOL_NORM = 0x08,

    /** @brief Indicates solenoid overcurrent */
    DIVECAN_ERR_SOL_OVERCURRENT = 0x0C,
} DiveCANError_t;

/**
 * @enum DiveCANManufacturer_t
 * @brief Enum to define different manufacturers of DiveCAN devices.
 */
typedef enum {
    /** @brief ISC (InnerSpace Systems Corp) manufacturer */
    DIVECAN_MANUFACTURER_ISC = 0x00,

    /** @brief SRI (Shearwater Research International) manufacturer */
    DIVECAN_MANUFACTURER_SRI = 0x01,

    /** @brief General/Unknown manufacturer */
    DIVECAN_MANUFACTURER_GEN = 0x02
} DiveCANManufacturer_t;

/**
 * @brief DiveCAN calibration result/response codes.
 */
typedef enum {
    /** @brief Acknowledgment of the start of the calibration process. */
    DIVECAN_CAL_ACK = 0x05,

    /** @brief Calibration result/response code indicating success. */
    DIVECAN_CAL_RESULT_OK = 0x01,

    /** @brief Calibration failed due to low external battery voltage. */
    DIVECAN_CAL_FAIL_LOW_EXT_BAT = 0x10,

    /** @brief Calibration failed because the FO2 sensor reading was out of its valid range. */
    DIVECAN_CAL_FAIL_FO2_RANGE = 0x20,

    /** @brief Calibration was rejected. */
    DIVECAN_CAL_FAIL_REJECTED = 0x08,

    /** @brief Generic calibration failure code. */
    DIVECAN_CAL_FAIL_GEN = 0x09
} DiveCANCalResponse_t;

/* ---- Message struct ---- */

/**
 * @struct DiveCANMessage_t
 * @brief Internal representation of a DiveCAN CAN frame.
 */
typedef struct {
    uint32_t id;
    uint8_t length;
    uint8_t data[MAX_CAN_RX_LENGTH];
} DiveCANMessage_t;

/* ---- Device spec ---- */

/** @brief Maximum DiveCAN device name length including NUL terminator. */
#define DIVECAN_MAX_NAME_SIZE 9U

/**
 * @struct DiveCANDevice_t
 * @brief Contains information about this DiveCAN device.
 */
typedef struct {
    const char *name;
    DiveCANType_t type;
    DiveCANManufacturer_t manufacturer_id;
    uint8_t firmware_version;
} DiveCANDevice_t;

/* ---- Bit width constants (for protocol byte assembly) ---- */

/** @brief 8-bit shift for extracting/inserting the low byte of multi-byte fields. */
static const uint32_t DIVECAN_BYTE_WIDTH = 8U;
static const uint32_t DIVECAN_TWO_BYTE_WIDTH = 16U;
static const uint32_t DIVECAN_THREE_BYTE_WIDTH = 24U;
static const uint32_t DIVECAN_SEVEN_BYTE_WIDTH = 56U;
static const uint32_t DIVECAN_HALF_BYTE_WIDTH = 4U;
static const uint8_t DIVECAN_BYTE_MASK = 0xFFU;

/* ---- FO2 limits ---- */

/** @brief Maximum valid FO2 percentage (100% O2). */
static const FO2_t FO2_MAX_PERCENT = 100U;

/* ---- Battery voltage scaling ---- */

/** @brief Multiplier to convert real battery voltage (V) to BatteryV_t (0.1 V units). */
static const uint8_t BATTERY_FLOAT_TO_INT = 10U;

/** @brief Battery voltage in 0.1 V units (e.g., 77 = 7.7 V). */
typedef uint8_t BatteryV_t;

/* ---- Dive state (for DIVING_ID messages) ---- */

/** @brief Dive state decoded from DIVING_ID messages, published on chan_dive_state. */
typedef struct {
    bool diving;              /**< true when a dive is in progress */
    uint32_t dive_number;     /**< Sequential dive counter from the dive computer */
    uint32_t unix_timestamp;  /**< Unix timestamp from the dive computer at dive start */
} DiveState_t;

#endif /* DIVECAN_TYPES_H */
