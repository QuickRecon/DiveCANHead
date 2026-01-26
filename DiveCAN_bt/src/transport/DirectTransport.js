/**
 * Direct Transport Layer
 *
 * The Petrel 3 BLE bridge handles ISO-TP internally.
 * This transport sends raw UDS payloads wrapped in DiveCAN datagrams.
 *
 * Packet format from Python reference:
 *   [source, target, len_low, len_high, UDS_payload...]
 *
 * Where len = payload_length + 1 (includes an implicit message type byte)
 */

import { ByteUtils } from '../utils/ByteUtils.js';
import { Logger } from '../utils/Logger.js';
import { TimeoutError } from '../errors/ProtocolErrors.js';

/**
 * Simple EventEmitter
 */
class EventEmitter {
  constructor() {
    this.events = {};
  }
  on(event, callback) {
    if (!this.events[event]) this.events[event] = [];
    this.events[event].push(callback);
    return this;
  }
  off(event, callback) {
    if (!this.events[event]) return this;
    this.events[event] = this.events[event].filter(cb => cb !== callback);
    return this;
  }
  emit(event, ...args) {
    if (!this.events[event]) return;
    this.events[event].forEach(callback => {
      try {
        callback(...args);
      } catch (error) {
        console.error(`Error in event handler for ${event}:`, error);
      }
    });
  }
  removeAllListeners(event) {
    if (event) delete this.events[event];
    else this.events = {};
    return this;
  }
}

/**
 * Direct Transport - sends UDS payloads in DiveCAN datagrams
 * No ISO-TP framing (Petrel handles that)
 */
export class DirectTransport extends EventEmitter {
  /**
   * Create direct transport
   * @param {number} sourceAddress - Source address (e.g., 0xFF for tester)
   * @param {number} targetAddress - Target address (e.g., 0x80 for controller)
   * @param {Object} options - Options
   */
  constructor(sourceAddress = 0xFF, targetAddress = 0x80, options = {}) {
    super();
    this.logger = new Logger('DirectTransport', 'debug');

    this.sourceAddress = sourceAddress;
    this.targetAddress = targetAddress;
    this.options = options;
  }

  /**
   * Send UDS payload
   * @param {Uint8Array|Array} data - UDS payload to send
   * @returns {Promise<void>}
   */
  async send(data) {
    const dataArray = ByteUtils.toUint8Array(data);

    this.logger.debug(`Send: ${dataArray.length} bytes`, {
      data: ByteUtils.toHexString(dataArray)
    });

    // Emit the raw UDS payload - the stack will wrap it in DiveCAN
    this.emit('frame', dataArray);
  }

  /**
   * Process received DiveCAN payload
   * @param {Uint8Array|Array} payload - Received payload (UDS data)
   */
  processFrame(payload) {
    const data = ByteUtils.toUint8Array(payload);

    this.logger.debug(`Received: ${data.length} bytes`, {
      data: ByteUtils.toHexString(data)
    });

    // Emit as complete message
    this.emit('message', data);
  }

  /**
   * Reset transport
   */
  reset() {
    // Nothing to reset for direct transport
  }

  /**
   * Get current state
   * @returns {string} Always 'IDLE' for direct transport
   */
  get state() {
    return 'IDLE';
  }

  /**
   * Check if idle
   * @returns {boolean} Always true for direct transport
   */
  get isIdle() {
    return true;
  }
}
