/**
 * @file oxygen_cell_types.h
 * @brief Domain types, constants, and zbus message structs for oxygen cells.
 *
 * Shared by sensor drivers, math helpers, calibration, and the DiveCAN
 * transmitter. Defines the PPO2_t/Millivolts_t typedefs, per-cell message
 * struct, consensus message struct, and calibration request/response types.
 */
#ifndef OXYGEN_CELL_TYPES_H
#define OXYGEN_CELL_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Domain value types ---- */

typedef uint8_t PPO2_t;           /**< PPO2 in centibar (0-254; 0xFF = fail/uncal) */
typedef uint16_t Millivolts_t;    /**< Cell voltage in units of 0.01 mV */
typedef uint8_t ShortMillivolts_t;/**< Cell voltage in mV (DiveCAN wire format) */
typedef float CalCoeff_t;         /**< Sensor calibration coefficient */
typedef uint8_t FO2_t;            /**< Fraction of O2 in percent (0-100) */

/* ---- Constants ---- */

/** @brief PPO2 sentinel value meaning failed or uncalibrated (0xFF). */
#define PPO2_FAIL        0xFFU
/** @brief Maximum inter-cell deviation before a cell is excluded (0.15 bar in centibar). */
#define MAX_DEVIATION    15U
/** @brief Highest representable valid PPO2 (centibar). */
#define MAX_VALID_PPO2   254U
/** @brief Maximum number of oxygen cells supported. */
#define CELL_MAX_COUNT   3U

/* ADC counts to millivolts: ADS1115 at +/-0.256V full scale, 15-bit signed */
#define COUNTS_TO_MILLIS ((0.256f * 100000.0f) / 32767.0f)

/* Calibration coefficient bounds — chosen so 8-13mV in air is valid */
/** @brief Upper bound for a valid analog cell calibration coefficient. */
#define ANALOG_CAL_UPPER 0.02625f
/** @brief Lower bound for a valid analog cell calibration coefficient. */
#define ANALOG_CAL_LOWER 0.01428f

/* DiveO2 calibration coefficient bounds — within 10% of nominal 1000000 */
/** @brief Upper bound for a valid DiveO2 calibration coefficient. */
#define DIVEO2_CAL_UPPER 1100000.0f
/** @brief Lower bound for a valid DiveO2 calibration coefficient. */
#define DIVEO2_CAL_LOWER  800000.0f
/** @brief Default DiveO2 calibration coefficient (uncalibrated). */
#define DIVEO2_CAL_DEFAULT 1000000.0f

/* O2S calibration coefficient bounds — within 20% of nominal 1.0 */
/** @brief Upper bound for a valid O2S calibration coefficient. */
#define O2S_CAL_UPPER 1.2f
/** @brief Lower bound for a valid O2S calibration coefficient. */
#define O2S_CAL_LOWER 0.8f
/** @brief Default O2S calibration coefficient (uncalibrated). */
#define O2S_CAL_DEFAULT 1.0f

/* ---- Cell status ---- */

/** @brief Operational status of a single oxygen cell. */
typedef enum {
    CELL_OK = 0,      /**< Cell reading is valid and within expected range */
    CELL_DEGRADED,    /**< Cell reading is valid but showing signs of degradation */
    CELL_FAIL,        /**< Cell has reported an unrecoverable error */
    CELL_NEED_CAL,    /**< Cell requires calibration before readings are trusted */
} CellStatus_t;

/* ---- zbus message types ---- */

/** @brief Per-cell reading published on chan_cell_1..3. */
typedef struct {
    uint8_t cell_number;
    PPO2_t ppo2;               /**< PPO2 in centibar; 0xFF = fail */
    double precision_ppo2;     /**< PPO2 in bar, full precision for PID */
    Millivolts_t millivolts;
    CellStatus_t status;
    int64_t timestamp_ticks;   /**< k_uptime_ticks() — 64-bit, no overflow */
    uint32_t pressure_uhpa;    /**< Pressure in units of 10^-3 hPa (DiveO2 native); 0 for analog */
} OxygenCellMsg_t;

/** @brief Voted consensus result published on chan_consensus. */
typedef struct {
    PPO2_t consensus_ppo2;
    double precision_consensus;
    PPO2_t ppo2_array[CELL_MAX_COUNT];
    double precision_ppo2_array[CELL_MAX_COUNT];
    Millivolts_t milli_array[CELL_MAX_COUNT];
    CellStatus_t status_array[CELL_MAX_COUNT];
    bool include_array[CELL_MAX_COUNT];
    uint8_t confidence;        /**< Number of cells that voted in (0-3) */
} ConsensusMsg_t;

/* ---- Calibration types ---- */

/** @brief Calibration method identifier carried in a CalRequest_t. */
typedef enum {
    CAL_DIGITAL_REFERENCE = 0, /**< Use digital cell as reference */
    CAL_ANALOG_ABSOLUTE = 1,   /**< Absolute analog calibration against known gas */
    CAL_TOTAL_ABSOLUTE = 2,    /**< Absolute calibration of all cells */
    CAL_SOLENOID_FLUSH = 3,    /**< Flush then calibrate */
} CalMethod_t;

/** @brief Result of a completed calibration sequence. */
typedef enum {
    CAL_RESULT_OK = 0,    /**< Calibration succeeded */
    CAL_RESULT_REJECTED,  /**< Result out of range; old coefficients retained */
    CAL_RESULT_FAILED,    /**< Hardware or sensor error during calibration */
    CAL_RESULT_BUSY,      /**< Calibration already in progress */
} CalResult_t;

/** @brief Calibration request published on chan_cal_request. */
typedef struct {
    CalMethod_t method;
    FO2_t fo2;
    uint16_t pressure_mbar;
} CalRequest_t;

/** @brief Calibration response published on chan_cal_response. */
typedef struct {
    CalResult_t result;
    ShortMillivolts_t cell_mv[CELL_MAX_COUNT]; /**< Per-cell mV at calibration point */
} CalResponse_t;

#endif /* OXYGEN_CELL_TYPES_H */
