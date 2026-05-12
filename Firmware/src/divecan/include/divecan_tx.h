/**
 * @file divecan_tx.h
 * @brief DiveCAN protocol message composers and low-level CAN transmit API.
 *
 * Declares the tx* composer functions (implemented in divecan_tx.c) and the
 * low-level divecan_send() / divecan_send_blocking() primitives. Consumers
 * include divecan.c and ppo2_transmitter.c.
 */
#ifndef DIVECAN_TX_H
#define DIVECAN_TX_H

#include <zephyr/device.h>

#include "divecan_types.h"
#include "common.h"

/**
 * @brief Initialize the DiveCAN TX layer
 * @param can_dev CAN device from devicetree
 * @return 0 on success, negative errno on failure
 */
Status_t divecan_tx_init(const struct device *can_dev);

/**
 * @brief Transmit a DiveCAN frame asynchronously (non-blocking, uses TX queue).
 *
 * @param msg Fully formed DiveCAN message to transmit
 * @return 0 on success, negative errno if the CAN driver queue is full
 */
Status_t divecan_send(const DiveCANMessage_t *msg);

/**
 * @brief Transmit a DiveCAN frame synchronously (blocks until sent or error).
 *
 * @param msg Fully formed DiveCAN message to transmit
 * @return 0 on success, negative errno on failure
 */
Status_t divecan_send_blocking(const DiveCANMessage_t *msg);

/* Device Metadata */

/**
 * @brief Transmit the bus initialization (start-device) message.
 *
 * @param targetDeviceType Device type being addressed
 * @param deviceType       Our device type
 */
void txStartDevice(DiveCANType_t targetDeviceType, DiveCANType_t deviceType);

/**
 * @brief Transmit the device ID message (manufacturer + firmware version).
 *
 * @param deviceType       Our device type
 * @param manufacturerID   Manufacturer identifier
 * @param firmwareVersion  Firmware version byte
 */
void txID(DiveCANType_t deviceType, DiveCANManufacturer_t manufacturerID,
      uint8_t firmwareVersion);

/**
 * @brief Transmit the device name string (max 8 chars).
 *
 * @param deviceType Our device type
 * @param name       NUL-terminated name string (truncated to 8 chars on wire)
 */
void txName(DiveCANType_t deviceType, const char *name);

/**
 * @brief Transmit the device status message.
 *
 * @param deviceType     Our device type
 * @param batteryVoltage Battery voltage (BatteryV_t units = 0.1 V)
 * @param setpoint       Current PPO2 setpoint in centibar
 * @param error          Current error state
 * @param showBattery    If true, show battery info when no error is present
 */
void txStatus(DiveCANType_t deviceType, BatteryV_t batteryVoltage,
          PPO2_t setpoint, DiveCANError_t error, bool showBattery);

/**
 * @brief Transmit the current setpoint to the dive computer.
 *
 * @param deviceType Our device type
 * @param setpoint   PPO2 setpoint in centibar
 */
void txSetpoint(DiveCANType_t deviceType, PPO2_t setpoint);

/**
 * @brief Transmit the HUD (OBOE) status packet with battery indicator.
 *
 * @param deviceType Our device type
 * @param error      Current error state (checked for DIVECAN_ERR_BAT_LOW)
 */
void txOBOEStat(DiveCANType_t deviceType, DiveCANError_t error);

/* PPO2 Messages */

/**
 * @brief Transmit per-cell PPO2 values.
 *
 * @param deviceType Our device type
 * @param cell1      PPO2 of cell 1 in centibar (0xFF = fail)
 * @param cell2      PPO2 of cell 2 in centibar (0xFF = fail)
 * @param cell3      PPO2 of cell 3 in centibar (0xFF = fail)
 */
void txPPO2(DiveCANType_t deviceType, PPO2_t cell1, PPO2_t cell2,
        PPO2_t cell3);

/**
 * @brief Transmit per-cell millivolt readings.
 *
 * @param deviceType Our device type
 * @param cell1      Millivolts of cell 1
 * @param cell2      Millivolts of cell 2
 * @param cell3      Millivolts of cell 3
 */
void txMillivolts(DiveCANType_t deviceType, Millivolts_t cell1,
          Millivolts_t cell2, Millivolts_t cell3);

/**
 * @brief Transmit per-cell inclusion flags and consensus PPO2.
 *
 * @param deviceType Our device type
 * @param cell1      true if cell 1 is included in the vote
 * @param cell2      true if cell 2 is included in the vote
 * @param cell3      true if cell 3 is included in the vote
 * @param ppo2       Consensus PPO2 in centibar
 */
void txCellState(DiveCANType_t deviceType, bool cell1, bool cell2,
         bool cell3, PPO2_t ppo2);

/* Calibration */

/**
 * @brief Acknowledge receipt of a calibration command.
 *
 * @param deviceType Our device type
 */
void txCalAck(DiveCANType_t deviceType);

/**
 * @brief Transmit the calibration result to the dive computer.
 *
 * @param deviceType          Our device type
 * @param response            Calibration result code
 * @param cell1               Millivolts of cell 1 at calibration point
 * @param cell2               Millivolts of cell 2 at calibration point
 * @param cell3               Millivolts of cell 3 at calibration point
 * @param fo2                 FO2 percentage of the calibration mixture
 * @param atmosphericPressure Atmospheric pressure at calibration time (mbar)
 */
void txCalResponse(DiveCANType_t deviceType, DiveCANCalResponse_t response,
           ShortMillivolts_t cell1, ShortMillivolts_t cell2,
           ShortMillivolts_t cell3, FO2_t fo2,
           uint16_t atmosphericPressure);

#endif /* DIVECAN_TX_H */
