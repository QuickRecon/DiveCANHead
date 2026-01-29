/**
 * UDS (Unified Diagnostic Services - ISO 14229) constants
 */

// Service IDs (SID)
export const SID_DIAGNOSTIC_SESSION_CONTROL = 0x10;
export const SID_READ_DATA_BY_ID = 0x22;
export const SID_WRITE_DATA_BY_ID = 0x2E;
export const SID_REQUEST_DOWNLOAD = 0x34;
export const SID_REQUEST_UPLOAD = 0x35;
export const SID_TRANSFER_DATA = 0x36;
export const SID_REQUEST_TRANSFER_EXIT = 0x37;

// Response SID offset
export const RESPONSE_SID_OFFSET = 0x40;

// Negative response SID
export const SID_NEGATIVE_RESPONSE = 0x7F;

// Session types
export const SESSION_DEFAULT = 0x01;
export const SESSION_PROGRAMMING = 0x02;
export const SESSION_EXTENDED_DIAGNOSTIC = 0x03;

// Negative Response Codes (NRC)
export const NRC_SERVICE_NOT_SUPPORTED = 0x11;
export const NRC_SUBFUNCTION_NOT_SUPPORTED = 0x12;
export const NRC_INCORRECT_MESSAGE_LENGTH = 0x13;
export const NRC_CONDITIONS_NOT_CORRECT = 0x22;
export const NRC_REQUEST_SEQUENCE_ERROR = 0x24;
export const NRC_REQUEST_OUT_OF_RANGE = 0x31;
export const NRC_SECURITY_ACCESS_DENIED = 0x33;
export const NRC_GENERAL_PROGRAMMING_FAILURE = 0x72;
export const NRC_WRONG_BLOCK_SEQUENCE_COUNTER = 0x73;
export const NRC_REQUEST_CORRECTLY_RECEIVED_RESPONSE_PENDING = 0x78;

// Common DIDs (available on all DiveCAN devices)
export const DID_BUS_DEVICES = 0x8000;      // Returns list of device IDs on bus
export const DID_SERIAL_NUMBER = 0x8010;    // Device serial number
export const DID_MODEL = 0x8011;            // Device model name
export const DID_DEVICE_NAME_BASE = 0x8100; // 0x81XX where XX = device ID

// Device-specific DIDs
export const DID_HARDWARE_VERSION = 0xF001;
export const DID_CONFIGURATION_BLOCK = 0xF100;
export const DID_CELL_VOLTAGES = 0xF200;
export const DID_PPO2_VALUES = 0xF201;
export const DID_ERROR_STATUS = 0xF300;

// Log streaming DIDs (0xAxxx range)
export const DID_LOG_STREAM_ENABLE = 0xA000;  // Read/Write: enable log push (1 byte)
export const DID_LOG_MESSAGE = 0xA100;         // Push: log message (ECU -> Tester)
export const DID_EVENT_MESSAGE = 0xA200;       // Push: event message (ECU -> Tester)

// Cell type constants (from Configuration_t bits 8-13)
export const CELL_TYPE_NONE = 0;
export const CELL_TYPE_DIVEO2 = 0;  // Note: firmware uses 0 for DiveO2
export const CELL_TYPE_ANALOG = 1;
export const CELL_TYPE_O2S = 2;

// Cell status constants (CellStatus_t enum)
export const CELL_STATUS_OK = 0;
export const CELL_STATUS_DEGRADED = 1;
export const CELL_STATUS_FAIL = 2;
export const CELL_STATUS_NEED_CAL = 3;

// Cell status names for display
export const CELL_STATUS_NAMES = ['OK', 'Degraded', 'Fail', 'Need Cal'];

// Settings DIDs
export const DID_SETTING_COUNT = 0x9100;
export const DID_SETTING_INFO_BASE = 0x9110;
export const DID_SETTING_VALUE_BASE = 0x9130;
export const DID_SETTING_LABEL_BASE = 0x9150;
export const DID_SETTING_SAVE_BASE = 0x9350;

// Setting kinds
export const SETTING_KIND_NUMBER = 0;
export const SETTING_KIND_TEXT = 1;

// Memory addresses (UDS address space)
export const MEMORY_CONFIG = 0xC2000080;
export const MEMORY_LOGS = 0xC3001000;
export const MEMORY_MCU_ID = 0xC5000000;

// Transfer parameters
export const MAX_BLOCK_LENGTH = 126;  // ISO-TP payload - 2 bytes overhead
export const DATA_FORMAT_UNCOMPRESSED = 0x00;
export const ADDRESS_AND_LENGTH_FORMAT = 0x44;  // 4 bytes address, 4 bytes length

// ============================================================================
// State DIDs Registry (DID-based data access)
// ============================================================================
// Each entry contains: { did, size, type, label, cellType? }
// - did: DID address
// - size: data size in bytes
// - type: 'float32' | 'int32' | 'uint32' | 'int16' | 'uint16' | 'uint8' | 'bool'
// - label: human-readable name
// - cellType: (optional) restricts DID to specific cell type

// Cell DID base and range
export const DID_CELL_BASE = 0xF400;
export const DID_CELL_RANGE = 0x0010;  // 16 DIDs per cell

// Cell DID offsets
export const CELL_DID_PPO2 = 0x00;
export const CELL_DID_TYPE = 0x01;
export const CELL_DID_INCLUDED = 0x02;
export const CELL_DID_STATUS = 0x03;
export const CELL_DID_RAW_ADC = 0x04;
export const CELL_DID_MILLIVOLTS = 0x05;
export const CELL_DID_TEMPERATURE = 0x06;
export const CELL_DID_ERROR = 0x07;
export const CELL_DID_PHASE = 0x08;
export const CELL_DID_INTENSITY = 0x09;
export const CELL_DID_AMBIENT_LIGHT = 0x0A;
export const CELL_DID_PRESSURE = 0x0B;
export const CELL_DID_HUMIDITY = 0x0C;

/**
 * State DID registry with metadata for automatic parsing
 */
// Power source constants
export const POWER_SOURCE_DEFAULT = 0;
export const POWER_SOURCE_BATTERY = 1;
export const POWER_SOURCE_CAN = 2;

export const STATE_DIDS = {
  // PPO2 Control State DIDs (0xF2xx)
  CONSENSUS_PPO2:    { did: 0xF200, size: 4, type: 'float32', label: 'Consensus PPO2' },
  SETPOINT:          { did: 0xF202, size: 4, type: 'float32', label: 'Setpoint' },
  CELLS_VALID:       { did: 0xF203, size: 1, type: 'uint8',   label: 'Cells Valid' },
  DUTY_CYCLE:        { did: 0xF210, size: 4, type: 'float32', label: 'Duty Cycle' },
  INTEGRAL_STATE:    { did: 0xF211, size: 4, type: 'float32', label: 'Integral State' },
  SATURATION_COUNT:  { did: 0xF212, size: 2, type: 'uint16',  label: 'Saturation Count' },
  UPTIME_SEC:        { did: 0xF220, size: 4, type: 'uint32',  label: 'Uptime (sec)' },

  // Power Monitoring DIDs (0xF23x)
  VBUS_VOLTAGE:      { did: 0xF230, size: 4, type: 'float32', label: 'VBus Voltage', unit: 'V' },
  VCC_VOLTAGE:       { did: 0xF231, size: 4, type: 'float32', label: 'VCC Voltage', unit: 'V' },
  BATTERY_VOLTAGE:   { did: 0xF232, size: 4, type: 'float32', label: 'Battery Voltage', unit: 'V' },
  CAN_VOLTAGE:       { did: 0xF233, size: 4, type: 'float32', label: 'CAN Voltage', unit: 'V' },
  THRESHOLD_VOLTAGE: { did: 0xF234, size: 4, type: 'float32', label: 'Threshold Voltage', unit: 'V' },
  POWER_SOURCES:     { did: 0xF235, size: 1, type: 'uint8',   label: 'Power Sources' },

  // Cell 0 DIDs (0xF400-0xF40F)
  CELL0_PPO2:          { did: 0xF400, size: 4, type: 'float32', label: 'Cell 0 PPO2' },
  CELL0_TYPE:          { did: 0xF401, size: 1, type: 'uint8',   label: 'Cell 0 Type' },
  CELL0_INCLUDED:      { did: 0xF402, size: 1, type: 'bool',    label: 'Cell 0 Included' },
  CELL0_STATUS:        { did: 0xF403, size: 1, type: 'uint8',   label: 'Cell 0 Status' },
  CELL0_RAW_ADC:       { did: 0xF404, size: 2, type: 'int16',   label: 'Cell 0 Raw ADC', cellType: CELL_TYPE_ANALOG },
  CELL0_MILLIVOLTS:    { did: 0xF405, size: 2, type: 'uint16',  label: 'Cell 0 mV', cellType: CELL_TYPE_ANALOG },
  CELL0_TEMPERATURE:   { did: 0xF406, size: 4, type: 'int32',   label: 'Cell 0 Temp', cellType: CELL_TYPE_DIVEO2 },
  CELL0_ERROR:         { did: 0xF407, size: 4, type: 'int32',   label: 'Cell 0 Error', cellType: CELL_TYPE_DIVEO2 },
  CELL0_PHASE:         { did: 0xF408, size: 4, type: 'int32',   label: 'Cell 0 Phase', cellType: CELL_TYPE_DIVEO2 },
  CELL0_INTENSITY:     { did: 0xF409, size: 4, type: 'int32',   label: 'Cell 0 Intensity', cellType: CELL_TYPE_DIVEO2 },
  CELL0_AMBIENT_LIGHT: { did: 0xF40A, size: 4, type: 'int32',   label: 'Cell 0 Ambient', cellType: CELL_TYPE_DIVEO2 },
  CELL0_PRESSURE:      { did: 0xF40B, size: 4, type: 'int32',   label: 'Cell 0 Pressure', cellType: CELL_TYPE_DIVEO2 },
  CELL0_HUMIDITY:      { did: 0xF40C, size: 4, type: 'int32',   label: 'Cell 0 Humidity', cellType: CELL_TYPE_DIVEO2 },

  // Cell 1 DIDs (0xF410-0xF41F)
  CELL1_PPO2:          { did: 0xF410, size: 4, type: 'float32', label: 'Cell 1 PPO2' },
  CELL1_TYPE:          { did: 0xF411, size: 1, type: 'uint8',   label: 'Cell 1 Type' },
  CELL1_INCLUDED:      { did: 0xF412, size: 1, type: 'bool',    label: 'Cell 1 Included' },
  CELL1_STATUS:        { did: 0xF413, size: 1, type: 'uint8',   label: 'Cell 1 Status' },
  CELL1_RAW_ADC:       { did: 0xF414, size: 2, type: 'int16',   label: 'Cell 1 Raw ADC', cellType: CELL_TYPE_ANALOG },
  CELL1_MILLIVOLTS:    { did: 0xF415, size: 2, type: 'uint16',  label: 'Cell 1 mV', cellType: CELL_TYPE_ANALOG },
  CELL1_TEMPERATURE:   { did: 0xF416, size: 4, type: 'int32',   label: 'Cell 1 Temp', cellType: CELL_TYPE_DIVEO2 },
  CELL1_ERROR:         { did: 0xF417, size: 4, type: 'int32',   label: 'Cell 1 Error', cellType: CELL_TYPE_DIVEO2 },
  CELL1_PHASE:         { did: 0xF418, size: 4, type: 'int32',   label: 'Cell 1 Phase', cellType: CELL_TYPE_DIVEO2 },
  CELL1_INTENSITY:     { did: 0xF419, size: 4, type: 'int32',   label: 'Cell 1 Intensity', cellType: CELL_TYPE_DIVEO2 },
  CELL1_AMBIENT_LIGHT: { did: 0xF41A, size: 4, type: 'int32',   label: 'Cell 1 Ambient', cellType: CELL_TYPE_DIVEO2 },
  CELL1_PRESSURE:      { did: 0xF41B, size: 4, type: 'int32',   label: 'Cell 1 Pressure', cellType: CELL_TYPE_DIVEO2 },
  CELL1_HUMIDITY:      { did: 0xF41C, size: 4, type: 'int32',   label: 'Cell 1 Humidity', cellType: CELL_TYPE_DIVEO2 },

  // Cell 2 DIDs (0xF420-0xF42F)
  CELL2_PPO2:          { did: 0xF420, size: 4, type: 'float32', label: 'Cell 2 PPO2' },
  CELL2_TYPE:          { did: 0xF421, size: 1, type: 'uint8',   label: 'Cell 2 Type' },
  CELL2_INCLUDED:      { did: 0xF422, size: 1, type: 'bool',    label: 'Cell 2 Included' },
  CELL2_STATUS:        { did: 0xF423, size: 1, type: 'uint8',   label: 'Cell 2 Status' },
  CELL2_RAW_ADC:       { did: 0xF424, size: 2, type: 'int16',   label: 'Cell 2 Raw ADC', cellType: CELL_TYPE_ANALOG },
  CELL2_MILLIVOLTS:    { did: 0xF425, size: 2, type: 'uint16',  label: 'Cell 2 mV', cellType: CELL_TYPE_ANALOG },
  CELL2_TEMPERATURE:   { did: 0xF426, size: 4, type: 'int32',   label: 'Cell 2 Temp', cellType: CELL_TYPE_DIVEO2 },
  CELL2_ERROR:         { did: 0xF427, size: 4, type: 'int32',   label: 'Cell 2 Error', cellType: CELL_TYPE_DIVEO2 },
  CELL2_PHASE:         { did: 0xF428, size: 4, type: 'int32',   label: 'Cell 2 Phase', cellType: CELL_TYPE_DIVEO2 },
  CELL2_INTENSITY:     { did: 0xF429, size: 4, type: 'int32',   label: 'Cell 2 Intensity', cellType: CELL_TYPE_DIVEO2 },
  CELL2_AMBIENT_LIGHT: { did: 0xF42A, size: 4, type: 'int32',   label: 'Cell 2 Ambient', cellType: CELL_TYPE_DIVEO2 },
  CELL2_PRESSURE:      { did: 0xF42B, size: 4, type: 'int32',   label: 'Cell 2 Pressure', cellType: CELL_TYPE_DIVEO2 },
  CELL2_HUMIDITY:      { did: 0xF42C, size: 4, type: 'int32',   label: 'Cell 2 Humidity', cellType: CELL_TYPE_DIVEO2 },
};

/**
 * Get DID info by DID address
 * @param {number} did - DID address
 * @returns {Object|null} DID info or null if not found
 */
export function getDIDInfo(did) {
  for (const [key, info] of Object.entries(STATE_DIDS)) {
    if (info.did === did) {
      return { key, ...info };
    }
  }
  return null;
}

/**
 * Get all DIDs for a specific cell number
 * @param {number} cellNum - Cell number (0-2)
 * @returns {Object} Map of DID key to DID info for that cell
 */
export function getCellDIDs(cellNum) {
  const prefix = `CELL${cellNum}_`;
  const result = {};
  for (const [key, info] of Object.entries(STATE_DIDS)) {
    if (key.startsWith(prefix)) {
      result[key] = info;
    }
  }
  return result;
}

/**
 * Get all DIDs valid for a specific cell type
 * @param {number} cellNum - Cell number (0-2)
 * @param {number} cellType - Cell type constant
 * @returns {Object} Map of DID key to DID info
 */
export function getValidCellDIDs(cellNum, cellType) {
  const cellDIDs = getCellDIDs(cellNum);
  const result = {};
  for (const [key, info] of Object.entries(cellDIDs)) {
    // Include if no cellType restriction OR cellType matches
    if (info.cellType === undefined || info.cellType === cellType) {
      result[key] = info;
    }
  }
  return result;
}

/**
 * Get all control state DIDs (non-cell DIDs)
 * @returns {Object} Map of DID key to DID info
 */
export function getControlStateDIDs() {
  const result = {};
  for (const [key, info] of Object.entries(STATE_DIDS)) {
    if (!key.startsWith('CELL')) {
      result[key] = info;
    }
  }
  return result;
}
