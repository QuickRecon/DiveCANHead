#ifndef OXYGEN_CELL_TYPES_H
#define OXYGEN_CELL_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Domain value types ---- */

typedef uint8_t PPO2_t;
typedef uint16_t Millivolts_t;
typedef uint8_t ShortMillivolts_t;
typedef float CalCoeff_t;
typedef uint8_t FO2_t;

/* ---- Constants ---- */

#define PPO2_FAIL        0xFFU
#define MAX_DEVIATION    15U    /* 0.15 bar in centibar */
#define MAX_VALID_PPO2   254U
#define CELL_MAX_COUNT   3U

/* ADC counts to millivolts: ADS1115 at +/-0.256V full scale, 15-bit signed */
#define COUNTS_TO_MILLIS ((0.256f * 100000.0f) / 32767.0f)

/* Calibration coefficient bounds — chosen so 8-13mV in air is valid */
#define ANALOG_CAL_UPPER 0.02625f
#define ANALOG_CAL_LOWER 0.01428f

/* DiveO2 calibration coefficient bounds — within 10% of nominal 1000000 */
#define DIVEO2_CAL_UPPER 1100000.0f
#define DIVEO2_CAL_LOWER  800000.0f
#define DIVEO2_CAL_DEFAULT 1000000.0f

/* O2S calibration coefficient bounds — within 20% of nominal 1.0 */
#define O2S_CAL_UPPER 1.2f
#define O2S_CAL_LOWER 0.8f
#define O2S_CAL_DEFAULT 1.0f

/* ---- Cell status ---- */

typedef enum {
	CELL_OK = 0,
	CELL_DEGRADED,
	CELL_FAIL,
	CELL_NEED_CAL,
} CellStatus_t;

/* ---- zbus message types ---- */

typedef struct {
	uint8_t cell_number;
	PPO2_t ppo2;               /* centibar, 0xFF = fail */
	double precision_ppo2;     /* bar, full precision for PID */
	Millivolts_t millivolts;
	CellStatus_t status;
	int64_t timestamp_ticks;   /* k_uptime_ticks() — 64-bit, no overflow */
} OxygenCellMsg_t;

typedef struct {
	PPO2_t consensus_ppo2;
	double precision_consensus;
	PPO2_t ppo2_array[CELL_MAX_COUNT];
	double precision_ppo2_array[CELL_MAX_COUNT];
	Millivolts_t milli_array[CELL_MAX_COUNT];
	CellStatus_t status_array[CELL_MAX_COUNT];
	bool include_array[CELL_MAX_COUNT];
	uint8_t confidence;        /* 0-3: how many cells voted in */
} ConsensusMsg_t;

/* ---- Calibration types ---- */

typedef enum {
	CAL_DIGITAL_REFERENCE = 0,
	CAL_ANALOG_ABSOLUTE = 1,
	CAL_TOTAL_ABSOLUTE = 2,
	CAL_SOLENOID_FLUSH = 3,
} CalMethod_t;

typedef enum {
	CAL_RESULT_OK = 0,
	CAL_RESULT_REJECTED,
	CAL_RESULT_FAILED,
	CAL_RESULT_BUSY,
} CalResult_t;

typedef struct {
	CalMethod_t method;
	FO2_t fo2;
	uint16_t pressure_mbar;
} CalRequest_t;

typedef struct {
	CalResult_t result;
	ShortMillivolts_t cell_mv[CELL_MAX_COUNT];
} CalResponse_t;

#endif /* OXYGEN_CELL_TYPES_H */
