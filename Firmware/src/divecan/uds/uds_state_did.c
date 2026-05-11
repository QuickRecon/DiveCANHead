/**
 * @file uds_state_did.c
 * @brief UDS State Data Identifier (DID) handler implementation
 *
 * Provides read access to system state via individual DIDs.
 * Data is sourced from zbus channels and power management API.
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "uds_state_did.h"
#include "divecan_types.h"
#include "divecan_channels.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "power_management.h"
#include "errors.h"

LOG_MODULE_REGISTER(uds_state_did, LOG_LEVEL_INF);

/* Time conversion constant */
static const uint32_t MS_PER_SECOND = 1000U;

/* Byte indices for little-endian serialization */
static const uint8_t BYTE_IDX_0 = 0U;
static const uint8_t BYTE_IDX_1 = 1U;
static const uint8_t BYTE_IDX_2 = 2U;
static const uint8_t BYTE_IDX_3 = 3U;

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Write a float32 to buffer in little-endian format
 */
static void writeFloat32(uint8_t *buf, float value)
{
	(void)memcpy(buf, &value, sizeof(float));
}

/**
 * @brief Write a uint32 to buffer in little-endian format
 */
static void writeUint32(uint8_t *buf, uint32_t value)
{
	buf[BYTE_IDX_0] = (uint8_t)(value);
	buf[BYTE_IDX_1] = (uint8_t)(value >> DIVECAN_BYTE_WIDTH);
	buf[BYTE_IDX_2] = (uint8_t)(value >> DIVECAN_TWO_BYTE_WIDTH);
	buf[BYTE_IDX_3] = (uint8_t)(value >> DIVECAN_THREE_BYTE_WIDTH);
}

/**
 * @brief Write a uint16 to buffer in little-endian format
 */
static void writeUint16(uint8_t *buf, uint16_t value)
{
	buf[0] = (uint8_t)(value);
	buf[1] = (uint8_t)(value >> DIVECAN_BYTE_WIDTH);
}

/* ============================================================================
 * PPO2 Control State DID Handlers (0xF2xx)
 * ============================================================================ */

static bool handleControlStateDID(uint16_t did, uint8_t *buf, uint16_t *len)
{
	bool result = true;
	ConsensusMsg_t consensus = {0};
	PPO2_t setpoint = 0;

	switch (did) {
	case UDS_DID_CONSENSUS_PPO2:
		(void)zbus_chan_read(&chan_consensus, &consensus, K_NO_WAIT);
		writeFloat32(buf, (float)consensus.precision_consensus);
		*len = sizeof(float);
		break;

	case UDS_DID_SETPOINT:
		(void)zbus_chan_read(&chan_setpoint, &setpoint, K_NO_WAIT);
		writeFloat32(buf, (float)setpoint / 100.0f);
		*len = sizeof(float);
		break;

	case UDS_DID_CELLS_VALID:
	{
		(void)zbus_chan_read(&chan_consensus, &consensus, K_NO_WAIT);
		uint8_t valid = 0;
		for (uint8_t i = 0; i < CELL_MAX_COUNT; i++) {
			if (consensus.include_array[i]) {
				valid |= (1U << i);
			}
		}
		buf[0] = valid;
		*len = sizeof(uint8_t);
		break;
	}

	case UDS_DID_DUTY_CYCLE:
		/* TODO: wire to PID controller channel when ported */
		writeFloat32(buf, 0.0f);
		*len = sizeof(float);
		break;

	case UDS_DID_INTEGRAL_STATE:
		/* TODO: wire to PID controller channel when ported */
		writeFloat32(buf, 0.0f);
		*len = sizeof(float);
		break;

	case UDS_DID_SATURATION_COUNT:
		/* TODO: wire to PID controller channel when ported */
		writeUint16(buf, 0);
		*len = sizeof(uint16_t);
		break;

	case UDS_DID_UPTIME_SEC:
		writeUint32(buf, k_uptime_get_32() / MS_PER_SECOND);
		*len = sizeof(uint32_t);
		break;

	/* Power Monitoring DIDs */
	case UDS_DID_VBUS_VOLTAGE:
		writeFloat32(buf, power_get_battery_voltage(POWER_DEVICE));
		*len = sizeof(float);
		break;

	case UDS_DID_VCC_VOLTAGE:
		writeFloat32(buf, power_get_battery_voltage(POWER_DEVICE));
		*len = sizeof(float);
		break;

	case UDS_DID_BATTERY_VOLTAGE:
		writeFloat32(buf, power_get_battery_voltage(POWER_DEVICE));
		*len = sizeof(float);
		break;

	case UDS_DID_CAN_VOLTAGE:
		/* Jr has no separate CAN voltage sense */
		writeFloat32(buf, -1.0f);
		*len = sizeof(float);
		break;

	case UDS_DID_THRESHOLD_VOLTAGE:
		writeFloat32(buf, power_get_low_battery_threshold());
		*len = sizeof(float);
		break;

	case UDS_DID_POWER_SOURCES:
		/* Jr: single source (battery), no mux */
		buf[0] = 0;
		*len = sizeof(uint8_t);
		break;

	default:
		result = false;
		break;
	}

	return result;
}

/* ============================================================================
 * Cell DID Handlers (0xF4Nx)
 * ============================================================================ */

static bool handleUniversalCellDID(uint8_t cellNum, uint8_t offset,
				   const OxygenCellMsg_t *cellMsg,
				   uint8_t *buf, uint16_t *len)
{
	bool result = false;

	if (offset == CELL_DID_PPO2) {
		writeFloat32(buf, (float)cellMsg->precision_ppo2);
		*len = sizeof(float);
		result = true;
	} else if (offset == CELL_DID_TYPE) {
		/* Cell type from Kconfig */
#if defined(CONFIG_CELL_1_TYPE_ANALOG)
		uint8_t types[] = {1,
#elif defined(CONFIG_CELL_1_TYPE_DIVEO2)
		uint8_t types[] = {0,
#elif defined(CONFIG_CELL_1_TYPE_O2S)
		uint8_t types[] = {2,
#else
		uint8_t types[] = {1,
#endif
#if defined(CONFIG_CELL_2_TYPE_ANALOG)
			1,
#elif defined(CONFIG_CELL_2_TYPE_DIVEO2)
			0,
#elif defined(CONFIG_CELL_2_TYPE_O2S)
			2,
#else
			1,
#endif
#if defined(CONFIG_CELL_3_TYPE_ANALOG)
			1};
#elif defined(CONFIG_CELL_3_TYPE_DIVEO2)
			0};
#elif defined(CONFIG_CELL_3_TYPE_O2S)
			2};
#else
			1};
#endif
		buf[0] = types[cellNum];
		*len = sizeof(uint8_t);
		result = true;
	} else if (offset == CELL_DID_INCLUDED) {
		ConsensusMsg_t consensus = {0};
		(void)zbus_chan_read(&chan_consensus, &consensus, K_NO_WAIT);
		if (consensus.include_array[cellNum]) {
			buf[0] = 1U;
		} else {
			buf[0] = 0U;
		}
		*len = sizeof(uint8_t);
		result = true;
	} else if (offset == CELL_DID_STATUS) {
		buf[0] = (uint8_t)cellMsg->status;
		*len = sizeof(uint8_t);
		result = true;
	} else {
		/* Not a universal DID */
	}

	return result;
}

static bool handleAnalogCellDID(uint8_t offset,
				const OxygenCellMsg_t *cellMsg,
				uint8_t *buf, uint16_t *len)
{
	bool result = false;

	if (offset == CELL_DID_MILLIVOLTS) {
		writeUint16(buf, cellMsg->millivolts);
		*len = sizeof(uint16_t);
		result = true;
	}

	return result;
}

static bool handleCellDID(uint8_t cellNum, uint8_t offset,
			  uint8_t *buf, uint16_t *len)
{
	bool result = false;

	if (cellNum >= CELL_MAX_COUNT) {
		OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, cellNum);
	} else if (offset > CELL_DID_MAX_OFFSET) {
		OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, offset);
	} else {
		/* Read the cell's latest data from zbus */
		OxygenCellMsg_t cellMsg = {0};
		const struct zbus_channel *cell_chans[] = {
			&chan_cell_1,
#if CONFIG_CELL_COUNT >= 2
			&chan_cell_2,
#endif
#if CONFIG_CELL_COUNT >= 3
			&chan_cell_3,
#endif
		};

		if (cellNum < ARRAY_SIZE(cell_chans)) {
			(void)zbus_chan_read(cell_chans[cellNum], &cellMsg, K_NO_WAIT);
		}

		if (handleUniversalCellDID(cellNum, offset, &cellMsg, buf, len)) {
			result = true;
		} else if (handleAnalogCellDID(offset, &cellMsg, buf, len)) {
			result = true;
		} else {
			/* DiveO2/O2S-specific DIDs: not yet wired (need extended cell msg fields) */
		}
	}

	return result;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

bool UDS_StateDID_IsStateDID(uint16_t did)
{
	bool result = false;

	/* PPO2 Control State DIDs (0xF2xx) */
	if ((did >= UDS_DID_CONTROL_BASE) && (did <= UDS_DID_CONTROL_END)) {
		result = true;
	}
	/* Cell DIDs (0xF400-0xF42F) */
	else if ((did >= UDS_DID_CELL_BASE) &&
		 (did < (UDS_DID_CELL_BASE + (CELL_MAX_COUNT * UDS_DID_CELL_RANGE)))) {
		result = true;
	}

	return result;
}

bool UDS_StateDID_HandleRead(uint16_t did, uint8_t *responseBuffer,
			     uint16_t *responseLength)
{
	bool result = false;

	if ((responseBuffer == NULL) || (responseLength == NULL)) {
		OP_ERROR(OP_ERR_NULL_PTR);
	} else {
		*responseLength = 0U;

		/* PPO2 Control State DIDs (0xF2xx) */
		if ((did >= UDS_DID_CONTROL_BASE) && (did <= UDS_DID_CONTROL_END)) {
			result = handleControlStateDID(did, responseBuffer, responseLength);
		}
		/* Cell DIDs (0xF4Nx) */
		else if ((did >= UDS_DID_CELL_BASE) &&
			 (did < (UDS_DID_CELL_BASE + (CELL_MAX_COUNT * UDS_DID_CELL_RANGE)))) {
			uint8_t cellNum = (uint8_t)((did - UDS_DID_CELL_BASE) / UDS_DID_CELL_RANGE);
			uint8_t offset = (uint8_t)((did - UDS_DID_CELL_BASE) % UDS_DID_CELL_RANGE);
			result = handleCellDID(cellNum, offset, responseBuffer, responseLength);
		}
	}

	return result;
}
