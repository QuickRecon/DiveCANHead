#include <zephyr/ztest.h>
#include <string.h>

#include "divecan_tx.h"

/* ---- Stub API (from divecan_tx_stub.c) ---- */
void test_tx_reset(void);
int test_tx_get_count(void);
const DiveCANMessage_t *test_tx_get_frame(int index);
const DiveCANMessage_t *test_tx_last(void);

/* ---- Fixtures ---- */

static void tx_before(void *fixture)
{
	ARG_UNUSED(fixture);
	test_tx_reset();
}

ZTEST_SUITE(divecan_tx, NULL, NULL, tx_before, NULL, NULL);

/* ---- PPO2 message ---- */

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

/* ---- Millivolts message (big-endian) ---- */

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

/* ---- Cell state bitmask ---- */

ZTEST(divecan_tx, test_txCellState_bitmask)
{
	txCellState(DIVECAN_SOLO, true, false, true, 130);

	const DiveCANMessage_t *f = test_tx_last();
	zassert_not_null(f);
	zassert_equal(f->data[0], 0x05); /* bit 0 + bit 2 */
	zassert_equal(f->data[1], 130);
	zassert_equal(f->length, 2);
}

ZTEST(divecan_tx, test_txCellState_all_included)
{
	txCellState(DIVECAN_SOLO, true, true, true, 98);

	const DiveCANMessage_t *f = test_tx_last();
	zassert_equal(f->data[0], 0x07);
}

/* ---- Status message ---- */

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

ZTEST(divecan_tx, test_txStatus_low_battery)
{
	txStatus(DIVECAN_SOLO, 20, 130, DIVECAN_ERR_BAT_LOW, true);

	const DiveCANMessage_t *f = test_tx_last();
	zassert_equal(f->data[7], DIVECAN_ERR_BAT_LOW);
}

/* ---- Bus ID ---- */

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

/* ---- Name (8-byte truncation) ---- */

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

ZTEST(divecan_tx, test_txName_short)
{
	txName(DIVECAN_SOLO, "Hi");

	const DiveCANMessage_t *f = test_tx_last();
	zassert_equal(f->data[0], 'H');
	zassert_equal(f->data[1], 'i');
	zassert_equal(f->data[2], 0);
}

/* ---- Bus init ---- */

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

/* ---- Calibration ACK ---- */

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

/* ---- Calibration response ---- */

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

/* ---- OBOE stat ---- */

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

ZTEST(divecan_tx, test_txOBOEStat_low_battery)
{
	txOBOEStat(DIVECAN_SOLO, DIVECAN_ERR_BAT_LOW);

	const DiveCANMessage_t *f = test_tx_last();
	zassert_equal(f->data[0], 0x00);
}

/* ---- Setpoint ---- */

ZTEST(divecan_tx, test_txSetpoint)
{
	txSetpoint(DIVECAN_SOLO, 130);

	const DiveCANMessage_t *f = test_tx_last();
	zassert_not_null(f);
	zassert_equal(f->id & 0x1FFFF000U, PPO2_SETPOINT_ID);
	zassert_equal(f->data[0], 130);
	zassert_equal(f->length, 1);
}
