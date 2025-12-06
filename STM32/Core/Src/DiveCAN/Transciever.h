#pragma once
#include "../common.h"
#include "stdbool.h"

#include "cmsis_os.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define ID_MASK 0x1FFFF000

#define BUS_ID_ID 0xD000000
#define BUS_NAME_ID 0xD010000
#define BUS_OFF_ID 0xD030000
#define PPO2_PPO2_ID 0xD040000
#define HUD_STAT_ID 0xD070000
#define PPO2_ATMOS_ID 0xD080000

#define MENU_ID 0xD0A0000

#define PPO2_MILLIS_ID 0xD110000
#define CAL_ID 0xD120000
#define CAL_REQ_ID 0xD130000

#define CAN_UNKNOWN_1 0xd200000

#define BUS_MENU_OPEN_ID 0xD300000

#define BUS_INIT_ID 0xD370000

#define CAN_UNKNOWN_2 0xdc10000
#define CAN_UNKNOWN_3 0xdc40000
#define PPO2_SETPOINT_ID 0xDC90000
#define PPO2_STATUS_ID 0xDCA0000
#define BUS_STATUS_ID 0xDCB0000
#define CAN_UNKNOWN_4 0xDCC0000

#define CAN_SERIAL_NUMBER 0xdd20000

/* Extensions, these are not part of the standard but we use it for debugging, and adv features the shearwater doesn't natively support */
/* We're using higher IDs here so they get arbitrated away if other things are happening */
#define LOG_TEXT_ID 0xF000000

/* PID internal state */
#define PID_P_GAIN_ID 0xF100000
#define PID_I_GAIN_ID 0xF110000
#define PID_D_GAIN_ID 0xF120000
#define PID_I_STATE_ID 0xF130000
#define PID_D_STATE_ID 0xF140000
#define SOLENOID_DUTY_ID 0xF150000
#define PRECISION_CONSENSUS_ID 0xF160000

/* Precision cell values */
#define PRECISION_CELL_1_ID 0xF200000
#define PRECISION_CELL_2_ID 0xF210000
#define PRECISION_CELL_3_ID 0xF220000

#define MAX_CAN_RX_LENGTH 8

  /**
   * @struct DiveCANMessage_s
   * @brief Struct to represent a DiveCAN message.
   */
  typedef struct
  {
    uint32_t id;
    uint8_t length;
    uint8_t data[MAX_CAN_RX_LENGTH];
    const char *type;
  } DiveCANMessage_t;

  /**
   * @enum    DiveCANType_e
   * @brief   This enum represents different types of devices connected via CAN bus.
   *          Each value corresponds to a specific device type.
   */
  typedef enum
  {
    /** @brief DiveCAN controller device (like a Petrel) */
    DIVECAN_CONTROLLER = 1,

    /** @brief DiveCAN Oboe (Oxygen BOard and Electronics) device */
    DIVECAN_OBOE = 2,

    /** @brief DiveCAN Monitor device (like a HUD) */
    DIVECAN_MONITOR = 3,

    /** @brief DiveCAN SOLO (SOLenoid and Oxygen) device  */
    DIVECAN_SOLO = 4,

    /** @brief DiveCAN Revo (RMS/battery box) device */
    DIVECAN_REVO = 5
  } DiveCANType_t;

  /**
   * @enum DiveCANError_e
   * @brief Enum representing potential errors of a DiveCAN device.
   */
  typedef enum
  {
    /**
     * @brief An unknown error 1.
     */
    DIVECAN_ERR_UNKNOWN1 = 0,

    /**
     * @brief Error indicating low battery status.
     */
    DIVECAN_ERR_LOW_BATTERY = 1 << 0,

    /**
     * @brief Battery information is valid and should be shown.
     */
    DIVECAN_ERR_BAT_AVAIL = 1 << 1,

    /**
     * @brief Error indicating a problem with the solenoid.
     */
    DIVECAN_ERR_SOLENOID = 1 << 2,

    /**
     * @brief Indicates there are no errors in the system.
     */
    DIVECAN_ERR_NONE = 1 << 3,

    /**
     * @brief An unknown error 4.
     */
    DIVECAN_ERR_UNKNOWN4 = 1 << 5,

    /**
     * @brief An unknown error 5.
     */
    DIVECAN_ERR_UNKNOWN5 = 1 << 6,

    /**
     * @brief An unknown error 6.
     */
    DIVECAN_ERR_UNKNOWN6 = 1 << 7
  } DiveCANError_t;

  /**
   *  \enum DiveCANManufacturer_t
   *  \brief Enum to define different manufacturers of DiveCAN devices.
   */
  typedef enum
  {
    /**
     * @brief Identifies the ISC (InnerSpace Systems Corp) manufacturer for a DiveCAN device.
     */
    DIVECAN_MANUFACTURER_ISC = 0x00,

    /**
     * @brief Identifies the SRI (Shearwater Research International) manufacturer for a DiveCAN device.
     */
    DIVECAN_MANUFACTURER_SRI = 0x01,

    /**
     * @brief Identifies a General/Unknown manufacturer for a DiveCAN device.
     */
    DIVECAN_MANUFACTURER_GEN = 0x02
  } DiveCANManufacturer_t;

  void InitRXQueue(void);
  BaseType_t GetLatestCAN(const Timestamp_t blockTime, DiveCANMessage_t *message);
  void rxInterrupt(const uint32_t id, const uint8_t length, const uint8_t *const data);

  /* Device Metadata */
  void txStartDevice(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType);
  void txID(const DiveCANType_t deviceType, const DiveCANManufacturer_t manufacturerID, uint8_t firmwareVersion);
  void txName(const DiveCANType_t deviceType, const char *name);
  void txStatus(const DiveCANType_t deviceType, const BatteryV_t batteryVoltage, const PPO2_t setpoint, const DiveCANError_t error, bool showBattery);
  void txOBOEStat(const DiveCANType_t deviceType, const DiveCANError_t error);

  /* PPO2 Messages */
  void txPPO2(const DiveCANType_t deviceType, const PPO2_t cell1, const PPO2_t cell2, const PPO2_t cell3);
  void txMillivolts(const DiveCANType_t deviceType, const Millivolts_t cell1, const Millivolts_t cell2, const Millivolts_t cell3);
  void txCellState(const DiveCANType_t deviceType, const bool cell1, const bool cell2, const bool cell3, PPO2_t PPO2);

  /**
   * @brief DiveCAN calibration result/response codes.
   */
  typedef enum
  {
    /**
     * @brief Acknowledgment of the start of the calibration process.
     */
    DIVECAN_CAL_ACK = 0x05,

    /**
     * @brief Calibration result/response code indicating success.
     */
    DIVECAN_CAL_RESULT_OK = 0x01,

    /**
     * @brief Calibration failed due to low external battery voltage.
     */
    DIVECAN_CAL_FAIL_LOW_EXT_BAT = 0b00010000,

    /**
     * @brief Calibration failed because the FO2 sensor reading was out of its valid range.
     */
    DIVECAN_CAL_FAIL_FO2_RANGE = 0b00100000,

    /**
     * @brief Calibration was rejected.
     */
    DIVECAN_CAL_FAIL_REJECTED = 0b00001000,

    /**
     * @brief Generic calibration failure code.
     */
    DIVECAN_CAL_FAIL_GEN = 0x09
  } DiveCANCalResponse_t;

  typedef enum
  {
    DYNAMIC_NUM = 2,
    STATIC_TEXT = 1,
    DYNAMIC_TEXT = 0,
  } DiveCANMenuItemType_t;

  typedef struct OxygenCellStruct OxygenCell_t;

  void txCalAck(const DiveCANType_t deviceType);
  void txCalResponse(const DiveCANType_t deviceType, DiveCANCalResponse_t response, const ShortMillivolts_t cell1, const ShortMillivolts_t cell2, const ShortMillivolts_t cell3, const FO2_t FO2, const uint16_t atmosphericPressure);

  /* Bus Devices */
  void txMenuAck(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, uint8_t itemCount);
  void txMenuItem(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, const char *const fieldText, const bool textField, const bool editable);
  void txMenuSaveAck(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t fieldId);
  void txMenuFlags(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, uint64_t maxVal, uint64_t currentVal);
  void txMenuField(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, const char *fieldText);

  /* Non-standard messages for diagnoses and debug */
  void txLogText(const DiveCANType_t deviceType, const char *msg, uint16_t length);
  void txPIDState(const DiveCANType_t deviceType, PIDNumeric_t proportional_gain, PIDNumeric_t integral_gain, PIDNumeric_t derivative_gain, PIDNumeric_t integral_state, PIDNumeric_t derivative_state, PIDNumeric_t duty_cycle, PIDNumeric_t precisionConsensus);
  void txPrecisionCells(const DiveCANType_t deviceType, OxygenCell_t c1, OxygenCell_t c2, OxygenCell_t c3);
#ifdef __cplusplus
}
#endif
