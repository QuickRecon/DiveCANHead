/**
 * UDS (Unified Diagnostic Services - ISO 14229) constants
 *
 * Reconciled against the Zephyr DiveCAN firmware (Firmware/src/divecan/uds/*).
 * The canonical wire reference is the Python client Test Rig/divecan_rig/dut.py.
 *
 * NOTE ON PAYLOADS: the client sends bare UDS payloads (no ISO-TP pad byte); the
 * Petrel BLE bridge adds the pad and does ISO-TP segmentation. dut.py prepends a
 * 0x00 pad because it drives raw CAN; the JS client must NOT include it.
 *
 * ENDIANNESS: DID payloads and log/mcuboot fields are little-endian. The DID
 * header on the wire is big-endian. The one exception is the OTA RequestDownload
 * (0x34) SIZE field, which is BIG-endian; the log-download 0x34 SIZE is LITTLE-endian.
 */

// ============================================================================
// Service IDs (SID)
// ============================================================================
export const SID_SESSION_CONTROL = 0x10;
export const SID_READ_DATA_BY_ID = 0x22;
export const SID_WRITE_DATA_BY_ID = 0x2E;
export const SID_ROUTINE_CONTROL = 0x31;
export const SID_REQUEST_DOWNLOAD = 0x34;
export const SID_TRANSFER_DATA = 0x36;
export const SID_REQUEST_TRANSFER_EXIT = 0x37;

// Response SID offset (positive reply = request SID + 0x40)
export const RESPONSE_SID_OFFSET = 0x40;

// Negative response SID
export const SID_NEGATIVE_RESPONSE = 0x7F;

// ============================================================================
// Diagnostic sessions (SID 0x10 sub-function)
// ============================================================================
export const UDS_SESSION_DEFAULT = 0x01;
export const UDS_SESSION_PROGRAMMING = 0x02;

// ============================================================================
// Negative Response Codes (NRC)
// ============================================================================
export const NRC_SERVICE_NOT_SUPPORTED = 0x11;
export const NRC_SUBFUNCTION_NOT_SUPPORTED = 0x12;
export const NRC_INCORRECT_MESSAGE_LENGTH = 0x13;
export const NRC_RESPONSE_TOO_LONG = 0x14;
export const NRC_CONDITIONS_NOT_CORRECT = 0x22;
export const NRC_REQUEST_SEQUENCE_ERROR = 0x24;
export const NRC_REQUEST_OUT_OF_RANGE = 0x31;
export const NRC_SECURITY_ACCESS_DENIED = 0x33;
export const NRC_GENERAL_PROGRAMMING_FAILURE = 0x72;
export const NRC_WRONG_BLOCK_SEQUENCE = 0x73;
export const NRC_SERVICE_NOT_IN_SESSION = 0x7F;

/** Human-readable NRC names, keyed by code. */
export const NRC_NAMES = {
  0x11: 'Service Not Supported',
  0x12: 'Sub-function Not Supported',
  0x13: 'Incorrect Message Length',
  0x14: 'Response Too Long',
  0x22: 'Conditions Not Correct (dive gate / preconditions)',
  0x24: 'Request Sequence Error (out of state)',
  0x31: 'Request Out Of Range (unknown DID / bad value)',
  0x33: 'Security Access Denied',
  0x72: 'General Programming Failure',
  0x73: 'Wrong Block Sequence Counter',
  0x7F: 'Service Not Available In Active Session (needs programming session)'
};

// ============================================================================
// Identification DIDs (Zephyr firmware)
// ============================================================================
export const DID_FIRMWARE_VERSION = 0xF000; // ASCII git-describe (<=10 bytes)
export const DID_HARDWARE_VERSION = 0xF001; // uint8 (firmware currently reports 0)
export const DID_VARIANT_NAME = 0xF002;     // ASCII build variant (<=31 bytes)
export const DID_SERIAL_NUMBER = 0xF003;    // raw STM32 96-bit UID (<=16 bytes)

// ============================================================================
// Control write DIDs
// ============================================================================
export const DID_SETPOINT_WRITE = 0xF240;      // u8 centibar (0.40-1.60 bar clamp)
export const DID_CALIBRATION_TRIGGER = 0xF241; // u8 fO2 percentage (0-100)
export const DID_SOLENOID_OVERRIDE = 0xF242;   // [channel, magic 0x5A] (HIL raw-fire)
export const SOLENOID_OVERRIDE_MAGIC = 0x5A;
export const DID_ERROR_HISTOGRAM_CLEAR = 0xF261; // write any byte -> clear + persist

// Unsolicited log-message push (Head -> client), sent as a WriteDataByIdentifier.
export const DID_LOG_MESSAGE = 0xA100;

// ============================================================================
// MCUBoot / OTA management DIDs (0xF27x)
// ============================================================================
export const DID_MCUBOOT_STATUS = 0xF270;   // 16 bytes (see McubootStatus.js)
export const DID_POST_STATUS = 0xF271;      // 4 bytes: state, pass_mask, reserved
export const DID_SLOT0_VERSION = 0xF272;    // 8-byte sem_ver (running image)
export const DID_SLOT1_VERSION = 0xF273;    // 8-byte sem_ver (pending image / 0xFF)
export const DID_FACTORY_VERSION = 0xF274;  // 8-byte sem_ver (factory backup / 0xFF)
export const DID_FORCE_REVERT = 0xF275;     // write [0x01]: re-stage slot1 + reboot
export const DID_RESTORE_FACTORY = 0xF276;  // write [0x01]: factory -> slot1 + reboot
export const DID_FACTORY_CAPTURE = 0xF277;  // write [0x01]: bless slot0 as factory
export const DID_FACTORY_FLASH_ERASE = 0xF278; // write [0x01]: chip-erase whole NOR
export const DID_NVS_ERASE = 0xF279;        // write [0x01]: erase settings/cal partition

export const OTA_MANAGEMENT_MAGIC = 0x01;

// ============================================================================
// Flash-log management DIDs (0xF28x)
// ============================================================================
export const DID_LOG_STATS = 0xF280;           // two 28-byte FlashLogFcbStats (>=56 B)
export const DID_LOG_SELECTOR_RESULT = 0xF281; // 20 bytes: last selector resolution
export const DID_LOG_ERASE = 0xF282;           // write [stream_mask, magic 0xA5]
export const DID_LOG_VERBOSITY = 0xF283;       // R/W u8 (1=ERR..4=DBG)
export const DID_LOG_CAN_VERBOSE = 0xF284;     // R/W u8 bitmask bit0=RX bit1=TX
export const LOG_ERASE_MAGIC = 0xA5;

// ============================================================================
// OTA pipeline constants (SID 0x34/0x36/0x37 + 0x31)
// ============================================================================
export const OTA_DATA_FMT = 0x00;      // no compression
export const OTA_ADDR_LEN_FMT = 0x44;  // 4-byte addr + 4-byte size
export const OTA_LENGTH_FMT = 0x20;    // 2-byte max-block in the 0x74 response
export const OTA_REQ_OVERHEAD = 3;     // 0x36 request: pad + SID + seq before data
export const OTA_RID_ACTIVATE = 0xF001; // RoutineControl id: validate slot1 + swap

// MCUBoot image (imgtool) constants
export const MCUBOOT_IMAGE_MAGIC = 0x96F3B83D; // little-endian magic at bytes[0:4]

// ============================================================================
// Flash-log download constants
// ============================================================================
export const LOG_DOWNLOAD_SENTINEL_ADDR = 0xFFFFFFFE; // 0x34 addr routes to log reader
export const LOG_DOWNLOAD_MAGIC = 0x47434C44;         // "DCLG" little-endian
export const LOG_DOWNLOAD_MIN_BLOCK = 32;
export const LOG_DOWNLOAD_DEFAULT_BLOCK = 253;
export const LOG_DOWNLOAD_BLE_CHUNK = 61; // handset FC-overflows a 253-byte chunk

// RoutineControl selector RIDs (0x31 0x01)
export const LOG_RID_SELECT_BY_RANGE = 0xF100;    // UNIMPLEMENTED -> NRC 0x31
export const LOG_RID_SELECT_BY_BOOT = 0xF101;     // params: stream(u8) + boot_id(u32 LE)
export const LOG_RID_SELECT_BY_DIVE = 0xF102;     // params: stream(u8) + dive_id(u16 LE)
export const LOG_RID_SELECT_LATEST_BOOT = 0xF103; // params: stream(u8)
export const LOG_RID_SELECT_LATEST_DIVE = 0xF104; // params: stream(u8)
export const LOG_RID_BEGIN_STREAM = 0xF105;       // no params (needs prior selection)

export const LOG_STREAM_TELEMETRY = 0;
export const LOG_STREAM_TEXT = 1;

// Flash-log TLV record types (FlashLogType_t)
export const FL_TYPE_BOOT_MARKER = 0x01;
export const FL_TYPE_DIVE_START = 0x02;
export const FL_TYPE_DIVE_END = 0x03;
export const FL_TYPE_CAN_RX = 0x04;
export const FL_TYPE_CAN_TX = 0x05;
export const FL_TYPE_CONSENSUS = 0x10;
export const FL_TYPE_PID_SNAPSHOT = 0x11;
export const FL_TYPE_SOLENOID_FIRE = 0x12;
export const FL_TYPE_CELL_RAW_DIVEO2 = 0x20;
export const FL_TYPE_CELL_RAW_O2S = 0x21;
export const FL_TYPE_CELL_RAW_ANALOG = 0x22;
export const FL_TYPE_ERROR_EVENT = 0x30;
export const FL_TYPE_LOG_TEXT = 0x40;
export const FL_TYPE_BATCH = 0xFD;
export const FL_TYPE_DROP_MARKER = 0xFE;
export const FL_TYPE_END_OF_STREAM = 0xFF;

export const FL_ENTRY_HDR_LEN = 12; // type u8, flags u8, length u16 LE, ts u64 LE
export const FL_FCB_STATS_LEN = 28; // natural-alignment C struct (not packed)
export const LOG_DCLG_HEADER_LEN = 16;

/** Record type names for the log viewer. */
export const FL_TYPE_NAMES = {
  0x01: 'Boot Marker',
  0x02: 'Dive Start',
  0x03: 'Dive End',
  0x04: 'CAN RX',
  0x05: 'CAN TX',
  0x10: 'Consensus',
  0x11: 'PID Snapshot',
  0x12: 'Solenoid Fire',
  0x20: 'Cell Raw (DiveO2)',
  0x21: 'Cell Raw (O2S)',
  0x22: 'Cell Raw (Analog)',
  0x30: 'Error Event',
  0x40: 'Log Text',
  0xFD: 'Batch',
  0xFE: 'Drop Marker',
  0xFF: 'End Of Stream'
};

/** Text-log verbosity levels (DID 0xF283). */
export const LOG_LEVEL_NAMES = { 1: 'ERROR', 2: 'WARN', 3: 'INFO', 4: 'DEBUG' };

// ============================================================================
// Settings DIDs
// ============================================================================
export const DID_SETTING_COUNT = 0x9100;
export const DID_SETTING_INFO_BASE = 0x9110;
export const DID_SETTING_VALUE_BASE = 0x9130;
export const DID_SETTING_LABEL_BASE = 0x9150; // + (settingIndex<<4) + optionIndex
export const DID_SETTING_SAVE_BASE = 0x9350;

export const SETTING_LABEL_LEN = 9; // fixed-width fields (space/zero padded)

// Setting kinds
export const SETTING_KIND_NUMBER = 0;
export const SETTING_KIND_TEXT = 1;

// ============================================================================
// Cell type / status enums
// ============================================================================
export const CELL_TYPE_DIVEO2 = 0;
export const CELL_TYPE_ANALOG = 1;
export const CELL_TYPE_O2S = 2;
export const CELL_TYPE_NONE = 0; // legacy alias (firmware uses 0 for DiveO2)

export const CELL_STATUS_OK = 0;
export const CELL_STATUS_DEGRADED = 1;
export const CELL_STATUS_FAIL = 2;
export const CELL_STATUS_NEED_CAL = 3;
export const CELL_STATUS_NAMES = ['OK', 'Degraded', 'Fail', 'Need Cal'];

// Power source constants
export const POWER_SOURCE_DEFAULT = 0;
export const POWER_SOURCE_BATTERY = 1;
export const POWER_SOURCE_CAN = 2;

// ============================================================================
// State DIDs registry (live-pollable, read-only)
// ============================================================================
// Cell DID base and range
export const DID_CELL_BASE = 0xF400;
export const DID_CELL_RANGE = 0x0010; // 16 DIDs per cell

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
export const CELL_DID_BROADCAST = 0x0D; // write-only: 0=stop, !=0=start cell #BCST

export const STATE_DIDS = {
  // PPO2 control state (0xF2xx)
  CONSENSUS_PPO2:    { did: 0xF200, size: 4, type: 'float32', label: 'Consensus PPO2' },
  SETPOINT:          { did: 0xF202, size: 4, type: 'float32', label: 'Setpoint' },
  CELLS_VALID:       { did: 0xF203, size: 1, type: 'uint8',   label: 'Cells Valid' },
  DUTY_CYCLE:        { did: 0xF210, size: 4, type: 'float32', label: 'Duty Cycle' },
  INTEGRAL_STATE:    { did: 0xF211, size: 4, type: 'float32', label: 'Integral State' },
  SATURATION_COUNT:  { did: 0xF212, size: 2, type: 'uint16',  label: 'Saturation Count' },
  UPTIME_SEC:        { did: 0xF220, size: 4, type: 'uint32',  label: 'Uptime (sec)' },

  // Power monitoring (0xF23x)
  VBUS_VOLTAGE:      { did: 0xF230, size: 4, type: 'float32', label: 'VBus Voltage', unit: 'V' },
  VCC_VOLTAGE:       { did: 0xF231, size: 4, type: 'float32', label: 'VCC Voltage', unit: 'V' },
  BATTERY_VOLTAGE:   { did: 0xF232, size: 4, type: 'float32', label: 'Battery Voltage', unit: 'V' },
  CAN_VOLTAGE:       { did: 0xF233, size: 4, type: 'float32', label: 'CAN Voltage', unit: 'V' },
  THRESHOLD_VOLTAGE: { did: 0xF234, size: 4, type: 'float32', label: 'Threshold Voltage', unit: 'V' },
  POWER_SOURCES:     { did: 0xF235, size: 1, type: 'uint8',   label: 'Power Sources' },

  // Cell 0 DIDs (0xF400-0xF40C)
  CELL0_PPO2:          { did: 0xF400, size: 4, type: 'float32', label: 'Cell 0 PPO2' },
  CELL0_TYPE:          { did: 0xF401, size: 1, type: 'uint8',   label: 'Cell 0 Type' },
  CELL0_INCLUDED:      { did: 0xF402, size: 1, type: 'bool',    label: 'Cell 0 Included' },
  CELL0_STATUS:        { did: 0xF403, size: 1, type: 'uint8',   label: 'Cell 0 Status' },
  CELL0_RAW_ADC:       { did: 0xF404, size: 2, type: 'int16',   label: 'Cell 0 Raw ADC', cellType: CELL_TYPE_ANALOG },
  CELL0_MILLIVOLTS:    { did: 0xF405, size: 2, type: 'uint16',  label: 'Cell 0 mV', cellType: CELL_TYPE_ANALOG },
  CELL0_TEMPERATURE:   { did: 0xF406, size: 4, type: 'int32',   label: 'Cell 0 Temp', cellType: CELL_TYPE_DIVEO2 },
  CELL0_ERROR:         { did: 0xF407, size: 4, type: 'uint32',  label: 'Cell 0 Error', cellType: CELL_TYPE_DIVEO2 },
  CELL0_PHASE:         { did: 0xF408, size: 4, type: 'int32',   label: 'Cell 0 Phase', cellType: CELL_TYPE_DIVEO2 },
  CELL0_INTENSITY:     { did: 0xF409, size: 4, type: 'int32',   label: 'Cell 0 Intensity', cellType: CELL_TYPE_DIVEO2 },
  CELL0_AMBIENT_LIGHT: { did: 0xF40A, size: 4, type: 'int32',   label: 'Cell 0 Ambient', cellType: CELL_TYPE_DIVEO2 },
  CELL0_PRESSURE:      { did: 0xF40B, size: 4, type: 'uint32',  label: 'Cell 0 Pressure', cellType: CELL_TYPE_DIVEO2 },
  CELL0_HUMIDITY:      { did: 0xF40C, size: 4, type: 'int32',   label: 'Cell 0 Humidity', cellType: CELL_TYPE_DIVEO2 },

  // Cell 1 DIDs (0xF410-0xF41C)
  CELL1_PPO2:          { did: 0xF410, size: 4, type: 'float32', label: 'Cell 1 PPO2' },
  CELL1_TYPE:          { did: 0xF411, size: 1, type: 'uint8',   label: 'Cell 1 Type' },
  CELL1_INCLUDED:      { did: 0xF412, size: 1, type: 'bool',    label: 'Cell 1 Included' },
  CELL1_STATUS:        { did: 0xF413, size: 1, type: 'uint8',   label: 'Cell 1 Status' },
  CELL1_RAW_ADC:       { did: 0xF414, size: 2, type: 'int16',   label: 'Cell 1 Raw ADC', cellType: CELL_TYPE_ANALOG },
  CELL1_MILLIVOLTS:    { did: 0xF415, size: 2, type: 'uint16',  label: 'Cell 1 mV', cellType: CELL_TYPE_ANALOG },
  CELL1_TEMPERATURE:   { did: 0xF416, size: 4, type: 'int32',   label: 'Cell 1 Temp', cellType: CELL_TYPE_DIVEO2 },
  CELL1_ERROR:         { did: 0xF417, size: 4, type: 'uint32',  label: 'Cell 1 Error', cellType: CELL_TYPE_DIVEO2 },
  CELL1_PHASE:         { did: 0xF418, size: 4, type: 'int32',   label: 'Cell 1 Phase', cellType: CELL_TYPE_DIVEO2 },
  CELL1_INTENSITY:     { did: 0xF419, size: 4, type: 'int32',   label: 'Cell 1 Intensity', cellType: CELL_TYPE_DIVEO2 },
  CELL1_AMBIENT_LIGHT: { did: 0xF41A, size: 4, type: 'int32',   label: 'Cell 1 Ambient', cellType: CELL_TYPE_DIVEO2 },
  CELL1_PRESSURE:      { did: 0xF41B, size: 4, type: 'uint32',  label: 'Cell 1 Pressure', cellType: CELL_TYPE_DIVEO2 },
  CELL1_HUMIDITY:      { did: 0xF41C, size: 4, type: 'int32',   label: 'Cell 1 Humidity', cellType: CELL_TYPE_DIVEO2 },

  // Cell 2 DIDs (0xF420-0xF42C)
  CELL2_PPO2:          { did: 0xF420, size: 4, type: 'float32', label: 'Cell 2 PPO2' },
  CELL2_TYPE:          { did: 0xF421, size: 1, type: 'uint8',   label: 'Cell 2 Type' },
  CELL2_INCLUDED:      { did: 0xF422, size: 1, type: 'bool',    label: 'Cell 2 Included' },
  CELL2_STATUS:        { did: 0xF423, size: 1, type: 'uint8',   label: 'Cell 2 Status' },
  CELL2_RAW_ADC:       { did: 0xF424, size: 2, type: 'int16',   label: 'Cell 2 Raw ADC', cellType: CELL_TYPE_ANALOG },
  CELL2_MILLIVOLTS:    { did: 0xF425, size: 2, type: 'uint16',  label: 'Cell 2 mV', cellType: CELL_TYPE_ANALOG },
  CELL2_TEMPERATURE:   { did: 0xF426, size: 4, type: 'int32',   label: 'Cell 2 Temp', cellType: CELL_TYPE_DIVEO2 },
  CELL2_ERROR:         { did: 0xF427, size: 4, type: 'uint32',  label: 'Cell 2 Error', cellType: CELL_TYPE_DIVEO2 },
  CELL2_PHASE:         { did: 0xF428, size: 4, type: 'int32',   label: 'Cell 2 Phase', cellType: CELL_TYPE_DIVEO2 },
  CELL2_INTENSITY:     { did: 0xF429, size: 4, type: 'int32',   label: 'Cell 2 Intensity', cellType: CELL_TYPE_DIVEO2 },
  CELL2_AMBIENT_LIGHT: { did: 0xF42A, size: 4, type: 'int32',   label: 'Cell 2 Ambient', cellType: CELL_TYPE_DIVEO2 },
  CELL2_PRESSURE:      { did: 0xF42B, size: 4, type: 'uint32',  label: 'Cell 2 Pressure', cellType: CELL_TYPE_DIVEO2 },
  CELL2_HUMIDITY:      { did: 0xF42C, size: 4, type: 'int32',   label: 'Cell 2 Humidity', cellType: CELL_TYPE_DIVEO2 },
};

// DiveO2 fixed-point scale factors (raw integer -> engineering units)
export const DIVEO2_TEMP_SCALE = 0.001;      // milli-degC -> degC
export const DIVEO2_PRESSURE_SCALE = 0.000001; // uhPa -> hPa
export const DIVEO2_HUMIDITY_SCALE = 0.001;  // milli-RH -> %RH

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

// ============================================================================
// Extra read-only DIDs (identity, diagnostics, firmware, flash-log state)
// ============================================================================
// These are readable but not part of the live-polled STATE_DIDS set — many are
// variable-length or struct payloads that can't be bundled into a multi-DID
// read, so they are read one at a time and formatted by `parseExtraDIDValue`.
// `type`: 'string' | 'uint8' | 'uint32' | 'hex32' | 'semver' | 'hex'.

export const EXTRA_READ_DIDS = {
  FW_COMMIT:       { did: 0xF000, type: 'string', label: 'Firmware Commit',   category: 'Identity' },
  HW_VERSION_ID:   { did: 0xF001, type: 'uint8',  label: 'Hardware Version',  category: 'Identity' },
  VARIANT_NAME:    { did: 0xF002, type: 'string', label: 'Variant Name',      category: 'Identity' },
  SERIAL_UID:      { did: 0xF003, type: 'hex',    label: 'Serial (MCU UID)',  category: 'Identity' },

  ALARM_STATE:     { did: 0xF204, type: 'hex32',  label: 'Alarm State',       category: 'Diagnostics' },
  POSEIDON_GAUGE:  { did: 0xF236, type: 'hex',    label: 'Poseidon Gauge',    category: 'Diagnostics' },
  CRASH_VALID:     { did: 0xF250, type: 'uint8',  label: 'Crash Valid',       category: 'Diagnostics' },
  CRASH_REASON:    { did: 0xF251, type: 'uint32', label: 'Crash Reason',      category: 'Diagnostics' },
  CRASH_PC:        { did: 0xF252, type: 'hex32',  label: 'Crash PC',          category: 'Diagnostics' },
  CRASH_LR:        { did: 0xF253, type: 'hex32',  label: 'Crash LR',          category: 'Diagnostics' },
  CRASH_CFSR:      { did: 0xF254, type: 'hex32',  label: 'Crash CFSR',        category: 'Diagnostics' },
  ERROR_HISTOGRAM: { did: 0xF260, type: 'hex',    label: 'Error Histogram',   category: 'Diagnostics' },

  MCUBOOT_STATUS:  { did: 0xF270, type: 'hex',    label: 'MCUBoot Status',    category: 'Firmware' },
  POST_STATUS:     { did: 0xF271, type: 'hex',    label: 'POST Status',       category: 'Firmware' },
  SLOT0_VERSION:   { did: 0xF272, type: 'semver', label: 'Slot0 Version',     category: 'Firmware' },
  SLOT1_VERSION:   { did: 0xF273, type: 'semver', label: 'Slot1 Version',     category: 'Firmware' },
  FACTORY_VERSION: { did: 0xF274, type: 'semver', label: 'Factory Version',   category: 'Firmware' },

  LOG_STATS_RAW:   { did: 0xF280, type: 'hex',    label: 'Log Stats',         category: 'Flash Log' },
  LOG_SELECTOR:    { did: 0xF281, type: 'hex',    label: 'Log Selector',      category: 'Flash Log' },
  LOG_VERBOSITY:   { did: 0xF283, type: 'uint8',  label: 'Log Verbosity',     category: 'Flash Log' },
  LOG_CAN_VERBOSE: { did: 0xF284, type: 'uint8',  label: 'CAN Capture Mask',  category: 'Flash Log' },
};

/** Combined registry of every readable DID (live state + extras). */
export const ALL_READ_DIDS = { ...STATE_DIDS, ...EXTRA_READ_DIDS };

/**
 * Look up any readable DID's info by key (STATE_DIDS or EXTRA_READ_DIDS).
 * @param {string} key
 * @returns {Object|undefined}
 */
export function getReadableDIDInfo(key) {
  return ALL_READ_DIDS[key];
}

/**
 * Format a raw extra-DID payload into a displayable value based on its type.
 * @param {Object} info - Entry from EXTRA_READ_DIDS
 * @param {Uint8Array} data - Raw response bytes
 * @returns {string|number|undefined}
 */
export function parseExtraDIDValue(info, data) {
  if (!data) return undefined;
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  switch (info.type) {
    case 'string':
      return new TextDecoder().decode(data).replace(/\0+$/, '');
    case 'uint8':
      return data.length ? data[0] : undefined;
    case 'uint32':
      return data.length >= 4 ? view.getUint32(0, true) : undefined;
    case 'hex32':
      return data.length >= 4
        ? `0x${view.getUint32(0, true).toString(16).padStart(8, '0')}`
        : undefined;
    case 'semver': {
      if (data.length < 8) return 'n/a';
      let allFF = true;
      for (let i = 0; i < 8; i++) { if (data[i] !== 0xFF) { allFF = false; break; } }
      if (allFF) return 'n/a';
      const rev = view.getUint16(2, true);
      const build = view.getUint32(4, true);
      return `${data[0]}.${data[1]}.${rev}` + (build ? `+${build}` : '');
    }
    case 'hex':
    default:
      return Array.from(data).map(b => b.toString(16).padStart(2, '0')).join(' ');
  }
}
