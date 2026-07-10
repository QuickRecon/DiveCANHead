/**
 * DiveCAN Datagram Framing Layer
 * Handles DiveCAN datagram construction and parsing
 */

import { ByteUtils } from '../utils/ByteUtils.js';
import { ValidationError } from '../errors/ProtocolErrors.js';
import { Logger } from '../utils/Logger.js';
import * as constants from './constants.js';

/**
 * DiveCAN datagram framer
 */
export class DiveCANFramer {
  /**
   * Create a DiveCAN framer
   * @param {number} sourceAddress - Source address (default: BT_CLIENT_ADDRESS)
   * @param {number} targetAddress - Target address (default: CONTROLLER_ADDRESS)
   */
  constructor(sourceAddress = constants.BT_CLIENT_ADDRESS, targetAddress = constants.CONTROLLER_ADDRESS) {
    this.logger = new Logger('DiveCAN', 'debug');
    this._sourceAddress = sourceAddress;
    this._targetAddress = targetAddress;
  }

  /**
   * Get source address
   * @returns {number} Source address
   */
  get sourceAddress() {
    return this._sourceAddress;
  }

  /**
   * Set source address
   * @param {number} address - Source address
   */
  set sourceAddress(address) {
    DiveCANFramer.validateAddress(address);
    this._sourceAddress = address;
  }

  /**
   * Get target address
   * @returns {number} Target address
   */
  get targetAddress() {
    return this._targetAddress;
  }

  /**
   * Set target address
   * @param {number} address - Target address
   */
  set targetAddress(address) {
    DiveCANFramer.validateAddress(address);
    this._targetAddress = address;
  }

  /**
   * Frame payload into DiveCAN datagram
   * Format: [source, target, len_low, len_high/chunk, payload...]
   * @param {Uint8Array|Array} payload - Payload data
   * @param {number} messageType - Message type (default: MSG_TYPE_UDS)
   * @returns {Uint8Array} DiveCAN datagram
   */
  frame(payload, messageType = constants.MSG_TYPE_UDS) {
    if (!payload || payload.length === 0) {
      throw new ValidationError('Payload cannot be empty', 'DiveCAN');
    }

    // Length includes message type byte
    const dataLength = payload.length + 1;

    if (dataLength > constants.MAX_DATAGRAM_SIZE) {
      throw new ValidationError(
        `Payload too large: ${dataLength} bytes (max: ${constants.MAX_DATAGRAM_SIZE})`,
        'DiveCAN',
        { payloadLength: payload.length, maxSize: constants.MAX_DATAGRAM_SIZE }
      );
    }

    // Calculate length bytes
    const lenLow = dataLength & 0xFF;
    const lenHigh = (dataLength >> 8) & 0xFF;

    // Build datagram
    const datagram = new Uint8Array(4 + payload.length);
    datagram[0] = this._sourceAddress;
    datagram[1] = this._targetAddress;
    datagram[2] = lenLow;
    datagram[3] = lenHigh;  // Also serves as chunk index in some contexts
    datagram.set(payload, 4);

    this.logger.debug(`DiveCAN frame: ${payload.length} bytes payload`, {
      source: `0x${this._sourceAddress.toString(16).padStart(2, '0')}`,
      target: `0x${this._targetAddress.toString(16).padStart(2, '0')}`,
      length: dataLength,
      datagram: ByteUtils.toHexString(datagram)
    });

    return datagram;
  }

  /**
   * Parse DiveCAN datagram
   * @param {Uint8Array|Array} datagram - DiveCAN datagram
   * @returns {Object} Parsed datagram {source, target, length, chunk, payload}
   *
   * Note: the Petrel BLE bridge's per-notification transport header
   * ([0x01|0x02, 0x00]) is stripped upstream in `BLEConnection._handleData`,
   * once per notification — which is the only correct place, since the header
   * is per notification and SLIP frames span notifications. This parser
   * therefore assumes a clean datagram and does NOT strip a header itself;
   * doing so here (on the reassembled SLIP packet) would double-strip and
   * silently eat two real payload bytes whenever a datagram happened to begin
   * with that byte pattern.
   */
  parse(datagram) {
    if (!datagram || datagram.length < 4) {
      throw new ValidationError(
        'Datagram too short (minimum 4 bytes)',
        'DiveCAN',
        { length: datagram ? datagram.length : 0 }
      );
    }

    const source = datagram[0];
    const target = datagram[1];
    const lenLow = datagram[2];
    const lenHigh = datagram[3];

    // Reconstruct length
    let length = lenLow | (lenHigh << 8);

    // Extract payload (exclude 4-byte header)
    let payload = ByteUtils.slice(datagram, 4);

    // Sanity check: if declared length is way off (>1000), the header might be wrong
    if (length > 1000 || Math.abs(payload.length - length) > 10) {
      this.logger.warn(`Length mismatch: header=${length}, actual=${payload.length}. Using actual payload length.`);
      length = payload.length;
    }

    const result = {
      source,
      target,
      length,
      chunk: lenHigh,  // lenHigh can also represent chunk index
      payload
    };

    this.logger.debug(`DiveCAN parse: ${payload.length} bytes payload`, {
      source: `0x${source.toString(16).padStart(2, '0')}`,
      target: `0x${target.toString(16).padStart(2, '0')}`,
      length,
      payload: ByteUtils.toHexString(payload)
    });

    return result;
  }

  /**
   * Validate address is in valid range
   * @param {number} address - Address to validate
   * @throws {ValidationError} If address is invalid
   */
  static validateAddress(address) {
    if (typeof address !== 'number' || address < 0 || address > 0xFF) {
      throw new ValidationError(
        `Invalid address: ${address} (must be 0x00-0xFF)`,
        'DiveCAN',
        { address }
      );
    }
  }
}

// Re-export constants for convenience
export * from './constants.js';
