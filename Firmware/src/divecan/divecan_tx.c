/**
 * @file divecan_tx.c
 * @brief DiveCAN protocol message composers
 *
 * All tx* functions build DiveCANMessage_t structs with protocol-correct
 * byte layouts and pass them to divecan_send() (in divecan_send.c).
 * This separation allows unit tests to link the real composers against
 * a stub send layer.
 */

#include <zephyr/logging/log.h>
#include <string.h>

#include "divecan_tx.h"
#include "errors.h"

LOG_MODULE_REGISTER(divecan_tx, LOG_LEVEL_INF);

#define BUS_NAME_LEN 8U
#define CAN_DATA_SIZE 8U

/* Bus init magic bytes (protocol-defined) */
static const uint8_t BUS_INIT_BYTE0 = 0x8AU;
static const uint8_t BUS_INIT_BYTE1 = 0xF3U;
static const uint8_t BUS_INIT_LEN = 3U;

/* Calibration response final byte */
static const uint8_t CAL_RESP_FINAL_BYTE = 0x07U;

/* HUD stat magic bytes (protocol-defined) */
static const uint8_t HUD_STAT_BYTE1 = 0x23U;
static const uint8_t HUD_STAT_BYTE4 = 0x1EU;

/* Battery status bytes for HUD */
static const uint8_t BAT_STATUS_OK = 0x01U;
static const uint8_t BAT_STATUS_LOW = 0x00U;

/* Calibration acknowledge fill byte */
static const uint8_t CAL_ACK_FILL_BYTE = 0xFFU;

/* Status byte mask */
static const uint8_t STATUS_BYTE_MASK = 0xFFU;

/* Cell bitmask shift positions */
#define CELL_2_SHIFT 1U
#define CELL_3_SHIFT 2U

/*-----------------------------------------------------------------------------------*/
/* Device Metadata */

/** @brief Transmit the bus initialization message
 * @param targetDeviceType Target device type
 * @param deviceType the device type of this device
 */
void txStartDevice(DiveCANType_t targetDeviceType, DiveCANType_t deviceType)
{
	const DiveCANMessage_t message = {
		.id = BUS_INIT_ID | ((uint32_t)deviceType << DIVECAN_BYTE_WIDTH) | (uint32_t)targetDeviceType,
		.data = {BUS_INIT_BYTE0, BUS_INIT_BYTE1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		.length = BUS_INIT_LEN,
	};
	(void)divecan_send(&message);
}

/** @brief Transmit the id of this device
 * @param deviceType the device type of this device
 * @param manufacturerID Manufacturer ID
 * @param firmwareVersion Firmware version
 */
void txID(DiveCANType_t deviceType, DiveCANManufacturer_t manufacturerID,
	  uint8_t firmwareVersion)
{
	static const uint8_t BUS_ID_MSG_LEN = 3U;
	const DiveCANMessage_t message = {
		.id = BUS_ID_ID | (uint32_t)deviceType,
		.data = {(uint8_t)manufacturerID, 0x00, firmwareVersion, 0x00, 0x00, 0x00, 0x00, 0x00},
		.length = BUS_ID_MSG_LEN,
	};
	(void)divecan_send(&message);
}

/** @brief Transmit the name of this device
 * @param deviceType the device type of this device
 * @param name Name of this device (max 8 chars, excluding null terminator)
 */
void txName(DiveCANType_t deviceType, const char *name)
{
	if (name == NULL) {
		OP_ERROR(OP_ERR_NULL_PTR);
	} else {
		uint8_t data[BUS_NAME_LEN + 1U] = {0};
		(void)strncpy((char *)data, name, BUS_NAME_LEN);

		DiveCANMessage_t message = {
			.id = BUS_NAME_ID | (uint32_t)deviceType,
			.data = {0},
			.length = BUS_NAME_LEN,
		};
		(void)memcpy(message.data, data, BUS_NAME_LEN);
		(void)divecan_send(&message);
	}
}

/** @brief Transmit the current status of this device
 * @param deviceType the device type of this device
 * @param batteryVoltage Battery voltage of this device
 * @param setpoint The setpoint that the PPO2 controller is trying to maintain
 * @param error Current error state
 * @param showBattery Display battery voltage on the handset
 */
void txStatus(DiveCANType_t deviceType, BatteryV_t batteryVoltage,
	      PPO2_t setpoint, DiveCANError_t error, bool showBattery)
{
	uint8_t errByte = (uint8_t)error;
	/* Only send the battery info if there aren't error */
	if ((error != DIVECAN_ERR_BAT_LOW) && showBattery) {
		errByte = DIVECAN_ERR_BAT_NORM;
	}

	static const uint8_t STATUS_MSG_LEN = 8U;
	const DiveCANMessage_t message = {
		.id = BUS_STATUS_ID | (uint32_t)deviceType,
		.data = {batteryVoltage, 0x00, 0x00, 0x00, 0x00, setpoint, STATUS_BYTE_MASK, errByte},
		.length = STATUS_MSG_LEN,
	};
	(void)divecan_send(&message);
}

void txSetpoint(DiveCANType_t deviceType, PPO2_t setpoint)
{
	static const uint8_t SETPOINT_MSG_LEN = 1U;
	const DiveCANMessage_t message = {
		.id = PPO2_SETPOINT_ID | (uint32_t)deviceType,
		.data = {setpoint, 0, 0, 0, 0, 0, 0, 0},
		.length = SETPOINT_MSG_LEN,
	};
	(void)divecan_send(&message);
}

/**
 * @brief Transmit the magic packet to the HUD, this lets it know to show
 *        the yellow bar of low-battery-ness during boot
 * @param deviceType the device type of this device
 * @param error Current error state (we only check if its DIVECAN_ERR_BAT_LOW or not)
 */
void txOBOEStat(DiveCANType_t deviceType, DiveCANError_t error)
{
	uint8_t batByte = BAT_STATUS_OK;
	if (error == DIVECAN_ERR_BAT_LOW) {
		batByte = BAT_STATUS_LOW;
	}

	static const uint8_t HUD_STAT_MSG_LEN = 5U;
	const DiveCANMessage_t message = {
		.id = HUD_STAT_ID | (uint32_t)deviceType,
		.data = {batByte, HUD_STAT_BYTE1, 0x00, 0x00, HUD_STAT_BYTE4, 0x00, 0x00, 0x00},
		.length = HUD_STAT_MSG_LEN,
	};
	(void)divecan_send(&message);
}

/* PPO2 Messages */

/** @brief Transmit the PPO2 of the cells
 * @param deviceType the device type of this device
 * @param cell1 PPO2 of cell 1
 * @param cell2 PPO2 of cell 2
 * @param cell3 PPO2 of cell 3
 */
void txPPO2(DiveCANType_t deviceType, PPO2_t cell1, PPO2_t cell2,
	    PPO2_t cell3)
{
	static const uint8_t PPO2_MSG_LEN = 4U;
	const DiveCANMessage_t message = {
		.id = PPO2_PPO2_ID | (uint32_t)deviceType,
		.data = {0x00, cell1, cell2, cell3, 0x00, 0x00, 0x00, 0x00},
		.length = PPO2_MSG_LEN,
	};
	(void)divecan_send(&message);
}

/** @brief Transmit the millivolts of the cells
 * @param deviceType the device type of this device
 * @param cell1 Millivolts of cell 1
 * @param cell2 Millivolts of cell 2
 * @param cell3 Millivolts of cell 3
 */
void txMillivolts(DiveCANType_t deviceType, Millivolts_t cell1,
		  Millivolts_t cell2, Millivolts_t cell3)
{
	/* Make the cell millis the proper endianness (big-endian for DiveCAN) */
	uint8_t c1hi = (uint8_t)((cell1 >> DIVECAN_BYTE_WIDTH) & DIVECAN_BYTE_MASK);
	uint8_t c1lo = (uint8_t)(cell1 & DIVECAN_BYTE_MASK);
	uint8_t c2hi = (uint8_t)((cell2 >> DIVECAN_BYTE_WIDTH) & DIVECAN_BYTE_MASK);
	uint8_t c2lo = (uint8_t)(cell2 & DIVECAN_BYTE_MASK);
	uint8_t c3hi = (uint8_t)((cell3 >> DIVECAN_BYTE_WIDTH) & DIVECAN_BYTE_MASK);
	uint8_t c3lo = (uint8_t)(cell3 & DIVECAN_BYTE_MASK);

	static const uint8_t MILLIS_MSG_LEN = 7U;
	const DiveCANMessage_t message = {
		.id = PPO2_MILLIS_ID | (uint32_t)deviceType,
		.data = {c1hi, c1lo, c2hi, c2lo, c3hi, c3lo, 0x00, 0x00},
		.length = MILLIS_MSG_LEN,
	};
	(void)divecan_send(&message);
}

/** @brief Transmit the cell states and the consensus PPO2
 * @param deviceType the device type of this device
 * @param cell1 Include cell 1
 * @param cell2 Include cell 2
 * @param cell3 Include cell 3
 * @param ppo2 The consensus PPO2 of the cells
 */
void txCellState(DiveCANType_t deviceType, bool cell1, bool cell2,
		 bool cell3, PPO2_t ppo2)
{
	uint8_t cellMask = (uint8_t)((uint8_t)cell1 |
		(uint8_t)((uint8_t)cell2 << CELL_2_SHIFT) |
		(uint8_t)((uint8_t)cell3 << CELL_3_SHIFT));

	static const uint8_t CELL_STATE_MSG_LEN = 2U;
	const DiveCANMessage_t message = {
		.id = PPO2_STATUS_ID | (uint32_t)deviceType,
		.data = {cellMask, ppo2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		.length = CELL_STATE_MSG_LEN,
	};
	(void)divecan_send(&message);
}

/* Calibration */

/** @brief Acknowledge the request to go into calibration of the cells
 * @param deviceType the device type of this device
 */
void txCalAck(DiveCANType_t deviceType)
{
	static const uint8_t CAL_ACK_MSG_LEN = 8U;
	const DiveCANMessage_t message = {
		.id = CAL_ID | (uint32_t)deviceType,
		.data = {(uint8_t)DIVECAN_CAL_ACK, 0x00, 0x00, 0x00,
			 CAL_ACK_FILL_BYTE, CAL_ACK_FILL_BYTE, CAL_ACK_FILL_BYTE, 0x00},
		.length = CAL_ACK_MSG_LEN,
	};
	(void)divecan_send(&message);
}

/** @brief Send the response to the shearwater that we have completed calibration
 * @param deviceType Device type
 * @param response Calibration result code
 * @param cell1 Millivolts of cell 1
 * @param cell2 Millivolts of cell 2
 * @param cell3 Millivolts of cell 3
 * @param fo2 FO2 of the calibration mixture
 * @param atmosphericPressure Atmospheric pressure at the time of calibration
 */
void txCalResponse(DiveCANType_t deviceType, DiveCANCalResponse_t response,
		   ShortMillivolts_t cell1, ShortMillivolts_t cell2,
		   ShortMillivolts_t cell3, FO2_t fo2,
		   uint16_t atmosphericPressure)
{
	uint8_t atmosHi = (uint8_t)((atmosphericPressure >> DIVECAN_BYTE_WIDTH) & DIVECAN_BYTE_MASK);
	uint8_t atmosLo = (uint8_t)(atmosphericPressure & DIVECAN_BYTE_MASK);

	static const uint8_t CAL_RESP_MSG_LEN = 8U;
	const DiveCANMessage_t message = {
		.id = CAL_ID | (uint32_t)deviceType,
		.data = {(uint8_t)response, cell1, cell2, cell3,
			 fo2, atmosHi, atmosLo, CAL_RESP_FINAL_BYTE},
		.length = CAL_RESP_MSG_LEN,
	};
	(void)divecan_send(&message);
}

#if defined(CONFIG_EXTENDED_MESSAGES)

void txLogText(DiveCANType_t deviceType, const char *msg, uint16_t length)
{
	uint16_t remaining = length;

	for (uint16_t i = 0; i < length; i += CAN_DATA_SIZE) {
		uint8_t bytesToWrite = CAN_DATA_SIZE;
		if (remaining < CAN_DATA_SIZE) {
			bytesToWrite = (uint8_t)remaining;
		}

		uint8_t msgBuf[CAN_DATA_SIZE] = {0};
		(void)memcpy(msgBuf, msg + i, bytesToWrite);

		const DiveCANMessage_t message = {
			.id = LOG_TEXT_ID | (uint32_t)deviceType,
			.data = {msgBuf[0], msgBuf[1], msgBuf[2], msgBuf[3],
				 msgBuf[4], msgBuf[5], msgBuf[6], msgBuf[7]},
			.length = bytesToWrite,
		};
		(void)divecan_send(&message);
		remaining -= bytesToWrite;
	}
}

#endif /* CONFIG_EXTENDED_MESSAGES */
