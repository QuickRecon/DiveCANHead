/**
 * ConfigurationDecoder - Decode/encode 32-bit configuration bitfield
 *
 * Maps to firmware Configuration_t structure in configuration.h
 */

/**
 * Configuration field definitions
 * Each field specifies byte position, bit offset within byte, width, and metadata
 */
export const CONFIG_FIELDS = {
  firmwareVersion: {
    byte: 0,
    bitStart: 0,
    bitWidth: 8,
    label: 'Firmware Version',
    editable: false,
    type: 'number'
  },
  cell1: {
    byte: 1,
    bitStart: 0,
    bitWidth: 2,
    label: 'Cell 1 Type',
    editable: true,
    type: 'enum',
    options: [
      { value: 0, label: 'DiveO2' },
      { value: 1, label: 'Analog' },
      { value: 2, label: 'O2S' }
    ]
  },
  cell2: {
    byte: 1,
    bitStart: 2,
    bitWidth: 2,
    label: 'Cell 2 Type',
    editable: true,
    type: 'enum',
    options: [
      { value: 0, label: 'DiveO2' },
      { value: 1, label: 'Analog' },
      { value: 2, label: 'O2S' }
    ]
  },
  cell3: {
    byte: 1,
    bitStart: 4,
    bitWidth: 2,
    label: 'Cell 3 Type',
    editable: true,
    type: 'enum',
    options: [
      { value: 0, label: 'DiveO2' },
      { value: 1, label: 'Analog' },
      { value: 2, label: 'O2S' }
    ]
  },
  powerMode: {
    byte: 1,
    bitStart: 6,
    bitWidth: 2,
    label: 'Power Mode',
    editable: true,
    type: 'enum',
    options: [
      { value: 0, label: 'Battery' },
      { value: 1, label: 'Battery then CAN' },
      { value: 2, label: 'CAN' },
      { value: 3, label: 'Off' }
    ]
  },
  calibrationMode: {
    byte: 2,
    bitStart: 0,
    bitWidth: 3,
    label: 'Calibration Method',
    editable: true,
    type: 'enum',
    options: [
      { value: 0, label: 'Digital Reference' },
      { value: 1, label: 'Analog Absolute' },
      { value: 2, label: 'Total Absolute' },
      { value: 3, label: 'Solenoid Flush' }
    ]
  },
  enableUartPrinting: {
    byte: 2,
    bitStart: 3,
    bitWidth: 1,
    label: 'Debug Printing',
    editable: true,
    type: 'bool'
  },
  dischargeThresholdMode: {
    byte: 2,
    bitStart: 4,
    bitWidth: 2,
    label: 'Battery Threshold',
    editable: true,
    type: 'enum',
    options: [
      { value: 0, label: '9V' },
      { value: 1, label: 'Li-Ion 1S' },
      { value: 2, label: 'Li-Ion 2S' },
      { value: 3, label: 'Li-Ion 3S' }
    ]
  },
  ppo2ControlMode: {
    byte: 2,
    bitStart: 6,
    bitWidth: 2,
    label: 'PPO2 Control',
    editable: true,
    type: 'enum',
    options: [
      { value: 0, label: 'Off' },
      { value: 1, label: 'Solenoid PID' },
      { value: 2, label: 'MK15' }
    ]
  },
  extendedMessages: {
    byte: 3,
    bitStart: 0,
    bitWidth: 1,
    label: 'Extended Messages',
    editable: true,
    type: 'bool'
  },
  ppo2DepthCompensation: {
    byte: 3,
    bitStart: 1,
    bitWidth: 1,
    label: 'Depth Compensation',
    editable: true,
    type: 'bool'
  }
};

/**
 * ConfigurationDecoder class
 * Handles encoding/decoding of the 32-bit configuration bitfield
 */
export class ConfigurationDecoder {
  constructor() {
    this.bytes = [0, 0, 0, 0];
  }

  /**
   * Load configuration from 4-byte array
   * @param {number[]} bytes - Array of 4 bytes [byte0, byte1, byte2, byte3]
   * @returns {ConfigurationDecoder} this for chaining
   */
  fromBytes(bytes) {
    if (!Array.isArray(bytes) || bytes.length !== 4) {
      throw new Error('Expected array of 4 bytes');
    }
    this.bytes = bytes.map(b => b & 0xFF);
    return this;
  }

  /**
   * Get configuration as 4-byte array
   * @returns {number[]} Array of 4 bytes
   */
  toBytes() {
    return [...this.bytes];
  }

  /**
   * Get configuration as 32-bit unsigned integer (little-endian)
   * @returns {number} 32-bit value
   */
  toUint32() {
    return this.bytes[0] |
           (this.bytes[1] << 8) |
           (this.bytes[2] << 16) |
           (this.bytes[3] << 24);
  }

  /**
   * Get hex string representation for debugging
   * @returns {string} Hex string like "0x08061203"
   */
  toHexString() {
    // Display as big-endian for readability (byte3, byte2, byte1, byte0)
    const hex = this.bytes
      .slice()
      .reverse()
      .map(b => b.toString(16).padStart(2, '0'))
      .join('');
    return '0x' + hex;
  }

  /**
   * Extract a single field value from the configuration
   * @param {string} fieldName - Field name from CONFIG_FIELDS
   * @returns {number|undefined} Field value or undefined if field not found
   */
  getField(fieldName) {
    const field = CONFIG_FIELDS[fieldName];
    if (!field) return undefined;

    const byte = this.bytes[field.byte];
    const mask = (1 << field.bitWidth) - 1;
    return (byte >> field.bitStart) & mask;
  }

  /**
   * Set a single field value in the configuration
   * @param {string} fieldName - Field name from CONFIG_FIELDS
   * @param {number} value - New value for the field
   * @returns {boolean} true if successful, false if invalid field/value
   */
  setField(fieldName, value) {
    const field = CONFIG_FIELDS[fieldName];
    if (!field) return false;
    if (!field.editable) return false;

    const mask = (1 << field.bitWidth) - 1;
    if (value < 0 || value > mask) return false;

    // Clear the bits for this field, then set new value
    const clearMask = ~(mask << field.bitStart) & 0xFF;
    this.bytes[field.byte] = (this.bytes[field.byte] & clearMask) |
                             ((value & mask) << field.bitStart);
    return true;
  }

  /**
   * Get all field values with their metadata
   * @returns {Object} Object with field names as keys, containing value and metadata
   */
  getAllFields() {
    const result = {};
    for (const [name, field] of Object.entries(CONFIG_FIELDS)) {
      result[name] = {
        value: this.getField(name),
        ...field
      };
    }
    return result;
  }

  /**
   * Get human-readable label for a field's current value
   * @param {string} fieldName - Field name from CONFIG_FIELDS
   * @returns {string} Human-readable label or raw value as string
   */
  getFieldLabel(fieldName) {
    const field = CONFIG_FIELDS[fieldName];
    if (!field) return '';

    const value = this.getField(fieldName);

    if (field.type === 'bool') {
      return value ? 'Enabled' : 'Disabled';
    } else if (field.type === 'enum' && field.options) {
      const option = field.options.find(o => o.value === value);
      return option ? option.label : `Unknown (${value})`;
    } else {
      return String(value);
    }
  }

  /**
   * Create a copy of this decoder
   * @returns {ConfigurationDecoder} New decoder with same bytes
   */
  clone() {
    const copy = new ConfigurationDecoder();
    copy.bytes = [...this.bytes];
    return copy;
  }
}
