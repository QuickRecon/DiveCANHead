/**
 * StateVectorParser - Parse binary state vectors from DiveCANHead
 *
 * Binary format (122 bytes total):
 *   uint32_t config           - Configuration bitfield (cell types in bits 8-13)
 *   float    consensus_ppo2   - Voted PPO2 value
 *   float    setpoint         - Current setpoint
 *   float    duty_cycle       - Solenoid duty cycle (0.0-1.0)
 *   float    integral_state   - PID integral accumulator
 *   float[3] cell_ppo2        - Per-cell PPO2 values
 *   uint32_t[3][7] cell_detail - Per-cell detail fields
 *   uint16_t timestamp_sec    - Seconds since boot
 *   uint16_t saturation_count - PID saturation counter
 *   uint8_t  version          - Protocol version
 *   uint8_t  cellsValid       - Bit flags for voting inclusion
 *
 * Cell detail interpretation by type:
 *   ANALOG (1): detail[0] = raw ADC value
 *   O2S (2):    no detail fields
 *   DIVEO2 (3): detail[0]=temp, [1]=err, [2]=phase, [3]=intensity,
 *               [4]=ambientLight, [5]=pressure, [6]=humidity
 */

import {
  CELL_TYPE_NONE,
  CELL_TYPE_ANALOG,
  CELL_TYPE_O2S,
  CELL_TYPE_DIVEO2
} from '../uds/constants.js';

export const STATE_VECTOR_SIZE = 122;
export const STATE_VECTOR_VERSION = 1;

/**
 * Parse a binary state vector into a structured object
 * @param {Uint8Array} data - Raw binary data (122 bytes)
 * @returns {Object|null} Parsed state vector or null if invalid
 */
export function parseStateVector(data) {
  if (!data || data.length < STATE_VECTOR_SIZE) {
    console.warn(`StateVector: Invalid size ${data?.length}, expected ${STATE_VECTOR_SIZE}`);
    return null;
  }

  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  let offset = 0;

  // Helper to read little-endian values (STM32 is little-endian)
  const readUint32 = () => { const v = view.getUint32(offset, true); offset += 4; return v; };
  const readFloat32 = () => { const v = view.getFloat32(offset, true); offset += 4; return v; };
  const readUint16 = () => { const v = view.getUint16(offset, true); offset += 2; return v; };
  const readUint8 = () => { const v = view.getUint8(offset); offset += 1; return v; };

  // Parse header fields
  const config = readUint32();
  const consensus_ppo2 = readFloat32();
  const setpoint = readFloat32();
  const duty_cycle = readFloat32();
  const integral_state = readFloat32();

  // Parse cell PPO2 values
  const cell_ppo2 = [readFloat32(), readFloat32(), readFloat32()];

  // Parse cell detail arrays (3 cells x 7 fields)
  const cell_detail = [];
  for (let cell = 0; cell < 3; cell++) {
    const details = [];
    for (let i = 0; i < 7; i++) {
      details.push(readUint32());
    }
    cell_detail.push(details);
  }

  // Parse trailer fields
  const timestamp_sec = readUint16();
  const saturation_count = readUint16();
  const version = readUint8();
  const cellsValid = readUint8();

  // Extract cell types from config (bits 8-13, 2 bits per cell)
  const cellTypes = [
    (config >> 8) & 0x03,
    (config >> 10) & 0x03,
    (config >> 12) & 0x03
  ];

  // Build result object
  const result = {
    timestamp: timestamp_sec,
    version,
    config,
    cellTypes,
    cellsValid,
    consensus_ppo2,
    setpoint,
    duty_cycle,
    integral_state,
    saturation_count,
    cells: []
  };

  // Build per-cell data based on cell type
  for (let i = 0; i < 3; i++) {
    const cellType = cellTypes[i];
    const ppo2 = cell_ppo2[i];
    const detail = cell_detail[i];
    const included = (cellsValid & (1 << i)) !== 0;

    const cellData = {
      cellNumber: i,
      cellType,
      cellTypeName: getCellTypeName(cellType),
      ppo2,
      included
    };

    // Add type-specific fields
    switch (cellType) {
      case CELL_TYPE_ANALOG:
        cellData.rawAdc = detail[0] & 0xFFFF;  // int16 stored in uint32
        break;

      case CELL_TYPE_DIVEO2:
        cellData.temperature = toSigned32(detail[0]);  // millicelsius
        cellData.error = toSigned32(detail[1]);
        cellData.phase = toSigned32(detail[2]);
        cellData.intensity = toSigned32(detail[3]);
        cellData.ambientLight = toSigned32(detail[4]);
        cellData.pressure = toSigned32(detail[5]);      // microbar
        cellData.humidity = toSigned32(detail[6]);      // milliRH
        break;

      case CELL_TYPE_O2S:
        // O2S has no additional detail fields
        break;

      default:
        // CELL_TYPE_NONE or unknown - no additional fields
        break;
    }

    result.cells.push(cellData);
  }

  return result;
}

/**
 * Get human-readable cell type name
 * @param {number} cellType - Cell type constant
 * @returns {string} Cell type name
 */
export function getCellTypeName(cellType) {
  switch (cellType) {
    case CELL_TYPE_NONE: return 'None';
    case CELL_TYPE_ANALOG: return 'Analog';
    case CELL_TYPE_O2S: return 'O2S';
    case CELL_TYPE_DIVEO2: return 'DiveO2';
    default: return 'Unknown';
  }
}

/**
 * Convert uint32 to signed int32
 * @param {number} value - Unsigned 32-bit value
 * @returns {number} Signed 32-bit value
 */
function toSigned32(value) {
  return value > 0x7FFFFFFF ? value - 0x100000000 : value;
}

