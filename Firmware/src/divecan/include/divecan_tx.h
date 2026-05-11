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

/* Low-level CAN transmit */
Status_t divecan_send(const DiveCANMessage_t *msg);
Status_t divecan_send_blocking(const DiveCANMessage_t *msg);

/* Device Metadata */
void txStartDevice(DiveCANType_t targetDeviceType, DiveCANType_t deviceType);
void txID(DiveCANType_t deviceType, DiveCANManufacturer_t manufacturerID,
      uint8_t firmwareVersion);
void txName(DiveCANType_t deviceType, const char *name);
void txStatus(DiveCANType_t deviceType, BatteryV_t batteryVoltage,
          PPO2_t setpoint, DiveCANError_t error, bool showBattery);
void txSetpoint(DiveCANType_t deviceType, PPO2_t setpoint);
void txOBOEStat(DiveCANType_t deviceType, DiveCANError_t error);

/* PPO2 Messages */
void txPPO2(DiveCANType_t deviceType, PPO2_t cell1, PPO2_t cell2,
        PPO2_t cell3);
void txMillivolts(DiveCANType_t deviceType, Millivolts_t cell1,
          Millivolts_t cell2, Millivolts_t cell3);
void txCellState(DiveCANType_t deviceType, bool cell1, bool cell2,
         bool cell3, PPO2_t ppo2);

/* Calibration */
void txCalAck(DiveCANType_t deviceType);
void txCalResponse(DiveCANType_t deviceType, DiveCANCalResponse_t response,
           ShortMillivolts_t cell1, ShortMillivolts_t cell2,
           ShortMillivolts_t cell3, FO2_t fo2,
           uint16_t atmosphericPressure);

#if defined(CONFIG_EXTENDED_MESSAGES)
/* Non-standard messages for diagnostics and debug */
void txLogText(DiveCANType_t deviceType, const char *msg, uint16_t length);
#endif

#endif /* DIVECAN_TX_H */
