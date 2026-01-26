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
