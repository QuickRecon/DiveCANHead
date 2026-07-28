/**
 * @file flash_log_entries.h
 * @brief On-flash TLV record layouts for the flash log.
 *
 * All structs use little-endian host byte order. Total entry size on
 * flash is `sizeof(fl_entry_hdr_t) + payload`, plus the FCB framing
 * (length-encoding bytes + CRC + endmarker).
 *
 * Internal to the flash_log subsystem — not exposed in flash_log.h.
 */
#ifndef FLASH_LOG_ENTRIES_H
#define FLASH_LOG_ENTRIES_H

#include <stdint.h>

#include "common.h"

#define FL_ENTRY_FLAG_DROP_PRECEDED  (1U << 0)

/** @brief Common header prepended to every TLV entry. 12 bytes. */
typedef struct {
    uint8_t  type;        /* FlashLogType_t */
    uint8_t  flags;       /* bitmask of FL_ENTRY_FLAG values */
    uint16_t length;      /* payload bytes after this header */
    uint64_t ts_boot_us;  /* k_uptime_ticks() converted to microseconds */
} __packed fl_entry_hdr_t;

/* ---- Per-type payload layouts ---- */

/** @brief Payload for FL_TYPE_BOOT_MARKER. */
typedef struct {
    uint32_t boot_id;
    char     fw_version[16];   /* truncated, not NUL-padded */
    uint32_t reset_cause;      /* from hwinfo_get_reset_cause(), 0 if unknown */
    uint32_t prev_crash_magic; /* CRASH_MAGIC if prev_crash valid, else 0 */
    uint32_t prev_crash_reason;
    uint32_t prev_crash_pc;
    uint32_t prev_crash_lr;
} __packed fl_payload_boot_marker_t;

/** @brief Payload for FL_TYPE_DIVE_START / FL_TYPE_DIVE_END. */
typedef struct {
    uint16_t dive_number;
    uint32_t unix_timestamp;
} __packed fl_payload_dive_marker_t;

/** @brief Payload for FL_TYPE_CAN_RX / FL_TYPE_CAN_TX. */
typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
    uint8_t  reserved;
} __packed fl_payload_can_frame_t;

/** @brief Payload for FL_TYPE_CONSENSUS — packed representation. */
typedef struct {
    uint8_t  consensus_ppo2;
    uint8_t  ppo2_array[3];
    uint16_t milli_array[3];   /* Millivolts_t = uint16, 0.01 mV units */
    /* status[3] (2 bits each) + include[3] (1 bit each) packed:
     *   bits 0-1: cell0 status, bit 2: cell0 include,
     *   bits 3-4: cell1 status, bit 5: cell1 include,
     *   bits 6-7: cell2 status,
     * second byte: bit 0: cell2 include
     */
    uint16_t status_packed;
    uint8_t  confidence;
    uint8_t  setpoint;
} __packed fl_payload_consensus_t;

/** @brief Payload for FL_TYPE_PID_SNAPSHOT. */
typedef struct {
    Numeric_t integral;
    uint16_t  saturation_count;
    Numeric_t duty;
    uint8_t   setpoint;
} __packed fl_payload_pid_t;

/** @brief Payload for FL_TYPE_SOLENOID_FIRE. */
typedef struct {
    uint8_t  kind;              /* inject start, inject end, flush start or
                                 * flush end; see SOL_FIRE_EVT values in
                                 * flash_log.h */
    uint32_t requested_on_us;
    uint32_t off_us;
} __packed fl_payload_solenoid_fire_t;

/** @brief Payload for FL_TYPE_SOLENOID_CURRENT. */
typedef struct {
    uint8_t  role;              /* solenoid channel that fired */
    uint8_t  classification;    /* SolCurrentClass_t verdict */
    int32_t  baseline_ua;       /* pre-fire idle current (µA, +ve = draw) */
    int32_t  fire_ua;           /* during-fire current (µA, +ve = draw) */
    int32_t  delta_ua;          /* fire_ua - baseline_ua (µA) */
} __packed fl_payload_solenoid_current_t;

/** @brief Payload for FL_TYPE_CELL_RAW_DIVEO2. */
typedef struct {
    uint8_t  cell_index;
    uint8_t  ppo2;
    int32_t  temperature_dc;
    uint32_t err_code;
    int32_t  phase;
    int32_t  intensity;
    int32_t  ambient_light;
    uint32_t pressure_uhpa;
    int32_t  humidity_mrh;
} __packed fl_payload_cell_diveo2_t;

/** @brief Payload for FL_TYPE_CELL_RAW_O2S. */
typedef struct {
    uint8_t cell_index;
    uint8_t ppo2;
    uint8_t status;
} __packed fl_payload_cell_o2s_t;

/** @brief Payload for FL_TYPE_CELL_RAW_ANALOG. */
typedef struct {
    uint8_t  cell_index;
    uint8_t  ppo2;
    int32_t  raw_adc;
    uint16_t millivolts;
} __packed fl_payload_cell_analog_t;

/** @brief Payload for FL_TYPE_ERROR_EVENT. */
typedef struct {
    uint32_t code;
    uint32_t detail;
} __packed fl_payload_error_t;

/** @brief Payload for FL_TYPE_LOG_TEXT — variable-length tail. */
typedef struct {
    uint8_t  level;       /* 1=ERR..4=DBG */
    uint16_t module_id;
    /* Message bytes follow (not NUL-terminated). */
} __packed fl_payload_log_text_t;

/** @brief Payload for FL_TYPE_DROP_MARKER. */
typedef struct {
    uint32_t count;
    uint8_t  last_dropped_type;
} __packed fl_payload_drop_marker_t;

#endif /* FLASH_LOG_ENTRIES_H */
