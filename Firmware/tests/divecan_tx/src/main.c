/**
 * @file main.c
 * @brief DiveCAN protocol message composer unit tests
 *
 * Pure host build — no Zephyr CAN driver. Uses divecan_send_stub.c to capture
 * every DiveCANMessage_t that the composers in divecan_tx.c would hand to the
 * CAN driver. Each test calls a tx* function and then inspects the captured
 * frame via test_tx_last() to assert byte-exact protocol layout.
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "divecan_tx.h"

/* ---- Stub API (from divecan_tx_stub.c) ---- */
void test_tx_reset(void);
int test_tx_get_count(void);
const DiveCANMessage_t *test_tx_get_frame(int index);
const DiveCANMessage_t *test_tx_last(void);

/* ---- Fixtures ---- */

/**
 * @brief Per-test setup: clear the captured-frame ring buffer before each test.
 *
 * Ensures no frame left over from a previous test is accidentally consumed by
 * test_tx_last().
 */
static void tx_before(void *fixture)
{
    ARG_UNUSED(fixture);
    test_tx_reset();
}

/** @brief Suite: byte-layout verification for every DiveCAN TX message composer. */
ZTEST_SUITE(divecan_tx, NULL, NULL, tx_before, NULL, NULL);

/** @brief txPPO2 packs three cell PPO2 values into bytes [1..3] with byte[0] = 0x00. */
ZTEST(divecan_tx, test_txPPO2_byte_layout)
{
    txPPO2(DIVECAN_SOLO, 98, 99, 100);

    const DiveCANMessage_t *f = test_tx_last();
    zassert_not_null(f);
    zassert_equal(f->id & 0x1FFFF000U, PPO2_PPO2_ID);
    zassert_equal(f->data[0], 0x00);
    zassert_equal(f->data[1], 98);
    zassert_equal(f->data[2], 99);
    zassert_equal(f->data[3], 100);
    zassert_equal(f->length, 4);
}

/** @brief txMillivolts encodes each 16-bit millivolt value big-endian (hi byte first). */
ZTEST(divecan_tx, test_txMillivolts_big_endian)
{
    txMillivolts(DIVECAN_SOLO, 0x0102, 0x0304, 0x0506);

    const DiveCANMessage_t *f = test_tx_last();
    zassert_not_null(f);
    zassert_equal(f->data[0], 0x01);
    zassert_equal(f->data[1], 0x02);
    zassert_equal(f->data[2], 0x03);
    zassert_equal(f->data[3], 0x04);
    zassert_equal(f->data[4], 0x05);
    zassert_equal(f->data[5], 0x06);
    zassert_equal(f->length, 7);
}

/** @brief txCellState encodes the include flags as a bitmask in byte[0] (bit0=cell1, bit2=cell3). */
ZTEST(divecan_tx, test_txCellState_bitmask)
{
    txCellState(DIVECAN_SOLO, true, false, true, 130);

    const DiveCANMessage_t *f = test_tx_last();
    zassert_not_null(f);
    zassert_equal(f->data[0], 0x05); /* bit 0 + bit 2 */
    zassert_equal(f->data[1], 130);
    zassert_equal(f->length, 2);
}

/** @brief All three cells included produces bitmask 0x07 (bits 0, 1, and 2 set). */
ZTEST(divecan_tx, test_txCellState_all_included)
{
    txCellState(DIVECAN_SOLO, true, true, true, 98);

    const DiveCANMessage_t *f = test_tx_last();
    zassert_equal(f->data[0], 0x07);
}

/** @brief txStatus places battery voltage in byte[0], setpoint in byte[5], error byte in byte[7]. */
ZTEST(divecan_tx, test_txStatus_battery_and_setpoint)
{
    txStatus(DIVECAN_SOLO, 42, 130, DIVECAN_ERR_NONE, true);

    const DiveCANMessage_t *f = test_tx_last();
    zassert_not_null(f);
    zassert_equal(f->data[0], 42);  /* battery voltage */
    zassert_equal(f->data[5], 130); /* setpoint */
    zassert_equal(f->data[7], DIVECAN_ERR_BAT_NORM);
    zassert_equal(f->length, 8);
}

/** @brief Low-battery error propagates to byte[7] unchanged (no battery-normal override). */
ZTEST(divecan_tx, test_txStatus_low_battery)
{
    txStatus(DIVECAN_SOLO, 20, 130, DIVECAN_ERR_BAT_LOW, true);

    const DiveCANMessage_t *f = test_tx_last();
    zassert_equal(f->data[7], DIVECAN_ERR_BAT_LOW);
}

/** @brief txID sets BUS_ID_ID base, manufacturer byte[0], and firmware version byte[2]. */
ZTEST(divecan_tx, test_txID_fields)
{
    txID(DIVECAN_SOLO, DIVECAN_MANUFACTURER_GEN, 42);

    const DiveCANMessage_t *f = test_tx_last();
    zassert_not_null(f);
    zassert_equal(f->id & 0x1FFFF000U, BUS_ID_ID);
    zassert_equal(f->data[0], DIVECAN_MANUFACTURER_GEN);
    zassert_equal(f->data[2], 42);
    zassert_equal(f->length, 3);
}

/** @brief txName truncates names longer than 8 characters to exactly 8 bytes. */
ZTEST(divecan_tx, test_txName_truncation)
{
    /* "LONGERNAME" is 10 chars, truncated to 8 → "LONGERNA" */
    txName(DIVECAN_SOLO, "LONGERNAME");

    const DiveCANMessage_t *f = test_tx_last();
    zassert_not_null(f);
    zassert_equal(f->length, 8);
    zassert_equal(f->data[0], 'L');
    zassert_equal(f->data[7], 'A');
}

/** @brief txName pads short names with zero bytes beyond the last character. */
ZTEST(divecan_tx, test_txName_short)
{
    txName(DIVECAN_SOLO, "Hi");

    const DiveCANMessage_t *f = test_tx_last();
    zassert_equal(f->data[0], 'H');
    zassert_equal(f->data[1], 'i');
    zassert_equal(f->data[2], 0);
}

/** @brief txStartDevice composes the 29-bit CAN ID from BUS_INIT_ID, device type, and target type. */
ZTEST(divecan_tx, test_txStartDevice_id_composition)
{
    txStartDevice(DIVECAN_CONTROLLER, DIVECAN_SOLO);

    const DiveCANMessage_t *f = test_tx_last();
    zassert_not_null(f);
    uint32_t expected_id = BUS_INIT_ID |
        ((uint32_t)DIVECAN_SOLO << 8) |
        (uint32_t)DIVECAN_CONTROLLER;
    zassert_equal(f->id, expected_id);
    zassert_equal(f->data[0], 0x8A);
    zassert_equal(f->data[1], 0xF3);
    zassert_equal(f->length, 3);
}

/** @brief txCalAck sends DIVECAN_CAL_ACK byte followed by 0xFF fill bytes per protocol spec. */
ZTEST(divecan_tx, test_txCalAck)
{
    txCalAck(DIVECAN_SOLO);

    const DiveCANMessage_t *f = test_tx_last();
    zassert_not_null(f);
    zassert_equal(f->data[0], 0x05);
    zassert_equal(f->data[4], 0xFF);
    zassert_equal(f->data[5], 0xFF);
    zassert_equal(f->data[6], 0xFF);
    zassert_equal(f->length, 8);
}

/** @brief txCalResponse packs result, cell millivolts, FO2, and big-endian atmos pressure correctly. */
ZTEST(divecan_tx, test_txCalResponse_fields)
{
    txCalResponse(DIVECAN_SOLO, DIVECAN_CAL_RESULT_OK,
              11, 12, 13, 21, 1013);

    const DiveCANMessage_t *f = test_tx_last();
    zassert_not_null(f);
    zassert_equal(f->data[0], 0x01);
    zassert_equal(f->data[1], 11);
    zassert_equal(f->data[2], 12);
    zassert_equal(f->data[3], 13);
    zassert_equal(f->data[4], 21);
    zassert_equal(f->data[5], 0x03); /* 1013 >> 8 */
    zassert_equal(f->data[6], 0xF5); /* 1013 & 0xFF */
    zassert_equal(f->data[7], 0x07);
    zassert_equal(f->length, 8);
}

/** @brief txOBOEStat with no error sends battery-OK byte (0x01) and protocol magic bytes. */
ZTEST(divecan_tx, test_txOBOEStat_ok)
{
    txOBOEStat(DIVECAN_SOLO, DIVECAN_ERR_NONE);

    const DiveCANMessage_t *f = test_tx_last();
    zassert_not_null(f);
    zassert_equal(f->data[0], 0x01);
    zassert_equal(f->data[1], 0x23);
    zassert_equal(f->data[4], 0x1E);
    zassert_equal(f->length, 5);
}

/** @brief txOBOEStat with DIVECAN_ERR_BAT_LOW sends battery-low byte (0x00). */
ZTEST(divecan_tx, test_txOBOEStat_low_battery)
{
    txOBOEStat(DIVECAN_SOLO, DIVECAN_ERR_BAT_LOW);

    const DiveCANMessage_t *f = test_tx_last();
    zassert_equal(f->data[0], 0x00);
}

/** @brief txSetpoint sends a 1-byte frame with PPO2_SETPOINT_ID and the setpoint value in byte[0]. */
ZTEST(divecan_tx, test_txSetpoint)
{
    txSetpoint(DIVECAN_SOLO, 130);

    const DiveCANMessage_t *f = test_tx_last();
    zassert_not_null(f);
    zassert_equal(f->id & 0x1FFFF000U, PPO2_SETPOINT_ID);
    zassert_equal(f->data[0], 130);
    zassert_equal(f->length, 1);
}
