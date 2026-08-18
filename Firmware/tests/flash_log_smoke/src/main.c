/**
 * @file main.c
 * @brief Smoke tests for the flash-log on-flash TLV format.
 *
 * Pure-logic tests — no flash, no FCB, no zbus. Confirms that the
 * record structs in flash_log_entries.h pack to the bytes the docs
 * promise, and that the consensus status-packing layout matches what
 * the reader will see on the wire.
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "flash_log.h"
#include "flash_log_entries.h"
#include "oxygen_cell_types.h"

/* ============================================================================
 * Entry-header layout
 * ============================================================================ */

ZTEST_SUITE(flash_log_header, NULL, NULL, NULL, NULL, NULL);

ZTEST(flash_log_header, test_header_size_is_12)
{
    /* Documented in docs/FLASH_LOG.md and the on-wire spec in UDS.md.
     * A change here ripples through every reader, including the
     * client-side decoder. */
    zassert_equal(sizeof(fl_entry_hdr_t), 12U,
              "TLV header must be exactly 12 bytes — was %zu",
              sizeof(fl_entry_hdr_t));
}

ZTEST(flash_log_header, test_header_field_offsets)
{
    fl_entry_hdr_t h = {0};
    uint8_t *base = (uint8_t *)&h;

    zassert_equal((uintptr_t)&h.type - (uintptr_t)base, 0U,
              "type must live at offset 0");
    zassert_equal((uintptr_t)&h.flags - (uintptr_t)base, 1U,
              "flags must live at offset 1");
    zassert_equal((uintptr_t)&h.length - (uintptr_t)base, 2U,
              "length must live at offset 2");
    zassert_equal((uintptr_t)&h.ts_boot_us - (uintptr_t)base, 4U,
              "ts_boot_us must live at offset 4");
}

/* ============================================================================
 * Type IDs are stable across releases
 * ============================================================================ */

ZTEST_SUITE(flash_log_types, NULL, NULL, NULL, NULL, NULL);

ZTEST(flash_log_types, test_marker_type_codes)
{
    /* These hex values are part of the on-flash format contract; any
     * change must also bump the wire version in fl_build_header(). */
    zassert_equal(FL_TYPE_BOOT_MARKER,     0x01);
    zassert_equal(FL_TYPE_DIVE_START,      0x02);
    zassert_equal(FL_TYPE_DIVE_END,        0x03);
}

ZTEST(flash_log_types, test_telemetry_type_codes)
{
    zassert_equal(FL_TYPE_CONSENSUS,       0x10);
    zassert_equal(FL_TYPE_PID_SNAPSHOT,    0x11);
    zassert_equal(FL_TYPE_SOLENOID_FIRE,   0x12);
    zassert_equal(FL_TYPE_SOLENOID_CURRENT, 0x13);
    zassert_equal(FL_TYPE_ATMOS_PRESSURE,  0x14);
    zassert_equal(FL_TYPE_POWER_SNAPSHOT,  0x15);
    zassert_equal(FL_TYPE_CELL_RAW_DIVEO2, 0x20);
    zassert_equal(FL_TYPE_CELL_RAW_O2S,    0x21);
    zassert_equal(FL_TYPE_CELL_RAW_ANALOG, 0x22);
    zassert_equal(FL_TYPE_ERROR_EVENT,     0x30);
}

ZTEST(flash_log_types, test_text_and_synthetic_codes)
{
    zassert_equal(FL_TYPE_LOG_TEXT,        0x40);
    zassert_equal(FL_TYPE_DROP_MARKER,     0xFE);
    zassert_equal(FL_TYPE_END_OF_STREAM,   0xFF);
}

/* ============================================================================
 * Payload struct sizes — documented in docs/FLASH_LOG.md
 * ============================================================================ */

ZTEST_SUITE(flash_log_payloads, NULL, NULL, NULL, NULL, NULL);

ZTEST(flash_log_payloads, test_dive_marker_payload_is_6)
{
    /* dive_number u16 + unix_timestamp u32 = 6 bytes, packed. */
    zassert_equal(sizeof(fl_payload_dive_marker_t), 6U);
}

ZTEST(flash_log_payloads, test_can_frame_payload_is_14)
{
    /* id u32 + dlc u8 + data[8] + reserved u8 = 14 bytes. */
    zassert_equal(sizeof(fl_payload_can_frame_t), 14U);
}

ZTEST(flash_log_payloads, test_consensus_payload_size)
{
    /* consensus u8 + ppo2[3] u8 + milli[3] u16 + status_packed u16
     * + confidence u8 + setpoint u8 = 1+3+6+2+1+1 = 14 bytes. */
    zassert_equal(sizeof(fl_payload_consensus_t), 14U);
}

ZTEST(flash_log_payloads, test_pid_payload_size)
{
    /* integral f32 + sat_count u16 + duty f32 + setpoint u8 = 11 bytes. */
    zassert_equal(sizeof(fl_payload_pid_t), 11U);
}

ZTEST(flash_log_payloads, test_solenoid_fire_payload_size)
{
    /* kind u8 + requested_on_us u32 + off_us u32 = 9 bytes packed. */
    zassert_equal(sizeof(fl_payload_solenoid_fire_t), 9U);
}

ZTEST(flash_log_payloads, test_atmos_pressure_payload_size)
{
    zassert_equal(sizeof(fl_payload_atmos_pressure_t), 2U);
}

ZTEST(flash_log_payloads, test_power_snapshot_payload_size)
{
    /* 5 x f32 + i32 + u32 + u16 + u8 + u8 = 32 bytes packed. */
    zassert_equal(sizeof(fl_payload_power_snapshot_t), 32U);
}

ZTEST(flash_log_payloads, test_cell_diveo2_payload_size)
{
    /* All the DiveO2 ancillary fields: 1+1+4+4+4+4+4+4+4 = 30 bytes. */
    zassert_equal(sizeof(fl_payload_cell_diveo2_t), 30U);
}

ZTEST(flash_log_payloads, test_cell_o2s_payload_size)
{
    zassert_equal(sizeof(fl_payload_cell_o2s_t), 3U);
}

ZTEST(flash_log_payloads, test_cell_analog_payload_size)
{
    /* idx u8 + ppo2 u8 + raw_adc i32 + millivolts u16 = 8 bytes. */
    zassert_equal(sizeof(fl_payload_cell_analog_t), 8U);
}

ZTEST(flash_log_payloads, test_error_payload_size)
{
    zassert_equal(sizeof(fl_payload_error_t), 8U);
}

ZTEST(flash_log_payloads, test_drop_marker_payload_size)
{
    /* count u32 + last_dropped_type u8 = 5 bytes packed. */
    zassert_equal(sizeof(fl_payload_drop_marker_t), 5U);
}

/* ============================================================================
 * Consensus packing logic — this exact bit layout is referenced by the
 * client-side decoder, so freeze it under test.
 * ============================================================================ */

ZTEST_SUITE(flash_log_consensus_pack, NULL, NULL, NULL, NULL, NULL);

/* The packing scheme used in flash_log_enqueue_consensus():
 *   bit 0..1: status[0]   bit 2: include[0]
 *   bit 3..4: status[1]   bit 5: include[1]
 *   bit 6..7: status[2]
 *   bit 8:    include[2]
 */
static uint16_t pack_consensus_status(const ConsensusMsg_t *c)
{
    uint16_t p = 0U;
    uint16_t include0 = 0U;
    uint16_t include1 = 0U;
    uint16_t include2 = 0U;

    if (c->include_array[0]) {
        include0 = 1U;
    }
    if (c->include_array[1]) {
        include1 = 1U;
    }
    if (c->include_array[2]) {
        include2 = 1U;
    }

    p |= (uint16_t)(((uint16_t)(c->status_array[0] & 0x03U)) << 0U);
    p |= (uint16_t)(include0 << 2U);
    p |= (uint16_t)(((uint16_t)(c->status_array[1] & 0x03U)) << 3U);
    p |= (uint16_t)(include1 << 5U);
    p |= (uint16_t)(((uint16_t)(c->status_array[2] & 0x03U)) << 6U);
    p |= (uint16_t)(include2 << 8U);
    return p;
}

ZTEST(flash_log_consensus_pack, test_all_ok_all_included)
{
    ConsensusMsg_t c = {
        .status_array = { CELL_OK, CELL_OK, CELL_OK },
        .include_array = { true, true, true },
    };
    uint16_t p = pack_consensus_status(&c);
    /* status[0..2] all 0; include bits set at positions 2, 5, 8. */
    zassert_equal(p, (1U << 2) | (1U << 5) | (1U << 8),
              "all-OK all-included pack mismatch: 0x%x", p);
}

ZTEST(flash_log_consensus_pack, test_mixed_status)
{
    ConsensusMsg_t c = {
        .status_array = { CELL_FAIL, CELL_DEGRADED, CELL_OK },
        .include_array = { false, true, true },
    };
    uint16_t p = pack_consensus_status(&c);
    /* status[0] = 2 at bits 0..1; status[1] = 1 at bits 3..4;
     * status[2] = 0 at bits 6..7. include[0]=0, include[1]=1 at bit 5,
     * include[2]=1 at bit 8. */
    uint16_t expected = (uint16_t)((2U << 0) | (1U << 3) | (1U << 5) | (1U << 8));
    zassert_equal(p, expected, "mixed pack 0x%x != 0x%x", p, expected);
}

ZTEST(flash_log_consensus_pack, test_round_trip)
{
    /* Round-trip: pack then unpack should give the same status / include. */
    ConsensusMsg_t c = {
        .status_array = { CELL_OK, CELL_FAIL, CELL_NEED_CAL },
        .include_array = { true, false, true },
    };
    uint16_t p = pack_consensus_status(&c);

    uint8_t s0 = (uint8_t)((p >> 0) & 0x03U);
    uint8_t i0 = (uint8_t)((p >> 2) & 0x01U);
    uint8_t s1 = (uint8_t)((p >> 3) & 0x03U);
    uint8_t i1 = (uint8_t)((p >> 5) & 0x01U);
    uint8_t s2 = (uint8_t)((p >> 6) & 0x03U);
    uint8_t i2 = (uint8_t)((p >> 8) & 0x01U);

    zassert_equal(s0, (uint8_t)CELL_OK);
    zassert_equal(s1, (uint8_t)CELL_FAIL);
    zassert_equal(s2, (uint8_t)CELL_NEED_CAL);
    zassert_equal(i0, 1U);
    zassert_equal(i1, 0U);
    zassert_equal(i2, 1U);
}

/* ============================================================================
 * Heartbeat ID assignment — confirm HEARTBEAT_FLASH_LOG joined the enum.
 * ============================================================================ */

#include "heartbeat.h"

ZTEST_SUITE(flash_log_heartbeat, NULL, NULL, NULL, NULL, NULL);

ZTEST(flash_log_heartbeat, test_slot_present_and_in_range)
{
    /* The fl_writer_thread registers HEARTBEAT_FLASH_LOG and kicks
     * it once per loop. The slot must exist in the enum, be unique
     * (no aliasing on existing slots), and stay under the 32-bit cap. */
    zassert_true((int)HEARTBEAT_FLASH_LOG > (int)HEARTBEAT_CELL_3,
             "HEARTBEAT_FLASH_LOG should land past the cell slots");
    zassert_true((int)HEARTBEAT_FLASH_LOG < 32,
             "registered_mask is 32 bits; slot id must fit");
    zassert_true((int)HEARTBEAT_FLASH_LOG < (int)HEARTBEAT_COUNT,
             "HEARTBEAT_FLASH_LOG must be a valid id");
}
