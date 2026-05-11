/**
 * Utility functions for byte array operations
 */
export class ByteUtils {
  /**
   * Convert byte array to hex string
   * @param {Uint8Array|Array} bytes - Byte array
   * @param {string} separator - Separator between bytes (default: ' ')
   * @returns {string} Hex string
   */
  static toHexString(bytes, separator = ' ') {
    if (!bytes || bytes.length === 0) return '';
    return Array.from(bytes)
      .map(b => b.toString(16).padStart(2, '0'))
      .join(separator);
  }

  /**
   * Convert hex string to byte array
   * @param {string} hex - Hex string (with or without separators)
   * @returns {Uint8Array} Byte array
   */
  static fromHexString(hex) {
    const cleaned = hex.replaceAll(/[^0-9a-fA-F]/g, '');
    const bytes = new Uint8Array(cleaned.length / 2);
    for (let i = 0; i < bytes.length; i++) {
      bytes[i] = Number.parseInt(cleaned.slice(i * 2, i * 2 + 2), 16);
    }
    return bytes;
  }

  /**
   * Concatenate multiple byte arrays
   * @param {...Uint8Array|Array} arrays - Arrays to concatenate
   * @returns {Uint8Array} Concatenated array
   */
  static concat(...arrays) {
    const totalLength = arrays.reduce((sum, arr) => sum + arr.length, 0);
    const result = new Uint8Array(totalLength);
    let offset = 0;
    for (const arr of arrays) {
      result.set(arr, offset);
      offset += arr.length;
    }
    return result;
  }

  /**
   * Check if two byte arrays are equal
   * @param {Uint8Array|Array} a - First array
   * @param {Uint8Array|Array} b - Second array
   * @returns {boolean} True if equal
   */
  static equals(a, b) {
    if (!a || !b) return a === b;
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) {
      if (a[i] !== b[i]) return false;
    }
    return true;
  }

  /**
   * Slice byte array (always returns Uint8Array)
   * @param {Uint8Array|Array} array - Source array
   * @param {number} start - Start index
   * @param {number} end - End index (exclusive)
   * @returns {Uint8Array} Sliced array
   */
  static slice(array, start, end) {
    if (array instanceof Uint8Array) {
      return array.slice(start, end);
    }
    return new Uint8Array(array.slice(start, end));
  }

  /**
   * Convert value to Uint8Array
   * @param {Uint8Array|Array|number} value - Value to convert
   * @returns {Uint8Array} Byte array
   */
  static toUint8Array(value) {
    if (value instanceof Uint8Array) return value;
    if (Array.isArray(value)) return new Uint8Array(value);
    if (typeof value === 'number') return new Uint8Array([value]);
    throw new Error('Cannot convert to Uint8Array');
  }

  /**
   * Extract bits from a byte
   * @param {number} byte - Byte value
   * @param {number} start - Start bit (0-7)
   * @param {number} length - Number of bits
   * @returns {number} Extracted bits
   */
  static extractBits(byte, start, length) {
    const mask = (1 << length) - 1;
    return (byte >> start) & mask;
  }

  /**
   * Convert 16-bit value to big-endian bytes
   * @param {number} value - 16-bit value
   * @returns {Uint8Array} 2 bytes in big-endian
   */
  static uint16ToBE(value) {
    return new Uint8Array([
      (value >> 8) & 0xFF,
      value & 0xFF
    ]);
  }

  /**
   * Convert 32-bit value to big-endian bytes
   * @param {number} value - 32-bit value
   * @returns {Uint8Array} 4 bytes in big-endian
   */
  static uint32ToBE(value) {
    return new Uint8Array([
      (value >> 24) & 0xFF,
      (value >> 16) & 0xFF,
      (value >> 8) & 0xFF,
      value & 0xFF
    ]);
  }

  /**
   * Convert big-endian bytes to 16-bit value
   * @param {Uint8Array|Array} bytes - 2 bytes
   * @returns {number} 16-bit value
   */
  static beToUint16(bytes) {
    return (bytes[0] << 8) | bytes[1];
  }

  /**
   * Convert big-endian bytes to 32-bit value
   * @param {Uint8Array|Array} bytes - 4 bytes
   * @returns {number} 32-bit value
   */
  static beToUint32(bytes) {
    return (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
  }

  /**
   * Convert 8 bytes big-endian to BigInt
   * @param {Uint8Array|Array} bytes - 8 bytes
   * @returns {bigint} 64-bit value as BigInt
   */
  static beToUint64(bytes) {
    let result = 0n;
    for (let i = 0; i < 8; i++) {
      result = (result << 8n) | BigInt(bytes[i] || 0);
    }
    return result;
  }

  /**
   * Convert BigInt to 8 bytes big-endian
   * @param {bigint|number} value - 64-bit value
   * @returns {Uint8Array} 8 bytes in big-endian
   */
  static uint64ToBE(value) {
    const bytes = new Uint8Array(8);
    let v = BigInt(value);
    for (let i = 7; i >= 0; i--) {
      bytes[i] = Number(v & 0xFFn);
      v = v >> 8n;
    }
    return bytes;
  }
}
