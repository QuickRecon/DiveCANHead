/**
 * ISO-TP State Machine
 * Manages state transitions and context for ISO-TP transport
 */

import { Logger } from '../utils/Logger.js';
import { States } from './constants.js';

/**
 * ISO-TP state machine
 */
export class ISOTPStateMachine {
  constructor() {
    this.logger = new Logger('ISO-TP-SM', 'debug');
    this.state = States.IDLE;
    this.rxContext = null;
    this.txContext = null;
  }

  /**
   * Transition to new state
   * @param {string} newState - New state
   */
  transitionTo(newState) {
    if (this.state === newState) {
      return;
    }

    this.logger.debug(`State transition: ${this.state} -> ${newState}`);
    this.state = newState;
  }

  /**
   * Begin receiving multi-frame message
   * @param {number} totalLength - Total message length
   */
  beginReceiving(totalLength) {
    this.rxContext = {
      totalLength,
      received: 0,
      buffer: new Uint8Array(totalLength),
      sequenceNumber: 0,
      lastFrameTime: Date.now()
    };
    this.transitionTo(States.RECEIVING);
  }

  /**
   * Update RX context with received data
   * @param {Uint8Array} data - Received data
   * @param {number} sequenceNumber - Sequence number
   */
  updateRxContext(data, sequenceNumber) {
    if (!this.rxContext) {
      throw new Error('No RX context');
    }

    // Copy data to buffer
    this.rxContext.buffer.set(data, this.rxContext.received);
    this.rxContext.received += data.length;
    this.rxContext.sequenceNumber = sequenceNumber;
    this.rxContext.lastFrameTime = Date.now();
  }

  /**
   * Check if RX is complete
   * @returns {boolean} True if complete
   */
  isRxComplete() {
    return this.rxContext && this.rxContext.received >= this.rxContext.totalLength;
  }

  /**
   * Get received message
   * @returns {Uint8Array} Received message
   */
  getRxMessage() {
    if (!this.rxContext) {
      throw new Error('No RX context');
    }
    return this.rxContext.buffer.slice(0, this.rxContext.received);
  }

  /**
   * Begin transmitting multi-frame message
   * @param {Uint8Array} data - Data to transmit
   */
  beginTransmitting(data) {
    this.txContext = {
      data,
      sent: 0,
      sequenceNumber: 0,
      blockCounter: 0,
      blockSize: 0,
      stmin: 0,
      lastFrameTime: Date.now()
    };
    this.transitionTo(States.WAIT_FC);
  }

  /**
   * Update TX context with flow control parameters
   * @param {number} blockSize - Block size
   * @param {number} stmin - Minimum separation time
   */
  updateFlowControl(blockSize, stmin) {
    if (!this.txContext) {
      throw new Error('No TX context');
    }

    this.txContext.blockSize = blockSize;
    this.txContext.stmin = stmin;
    this.txContext.blockCounter = 0;
    this.transitionTo(States.TRANSMITTING);
  }

  /**
   * Get next TX chunk
   * @param {number} chunkSize - Chunk size
   * @returns {Object} {data, sequenceNumber, done}
   */
  getNextTxChunk(chunkSize) {
    if (!this.txContext) {
      throw new Error('No TX context');
    }

    const remaining = this.txContext.data.length - this.txContext.sent;
    const size = Math.min(remaining, chunkSize);
    const data = this.txContext.data.slice(this.txContext.sent, this.txContext.sent + size);

    this.txContext.sent += size;
    this.txContext.sequenceNumber = (this.txContext.sequenceNumber + 1) & 0x0F;
    this.txContext.blockCounter++;
    this.txContext.lastFrameTime = Date.now();

    const done = this.txContext.sent >= this.txContext.data.length;

    return { data, sequenceNumber: this.txContext.sequenceNumber, done };
  }

  /**
   * Check if block size limit reached (need to wait for FC)
   * @returns {boolean} True if limit reached
   */
  needFlowControl() {
    if (!this.txContext) return false;
    if (this.txContext.blockSize === 0) return false;  // Infinite
    return this.txContext.blockCounter >= this.txContext.blockSize;
  }

  /**
   * Get minimum separation time
   * @returns {number} Separation time in ms
   */
  getStmin() {
    return this.txContext ? this.txContext.stmin : 0;
  }

  /**
   * Reset state machine
   */
  reset() {
    this.logger.debug('Resetting state machine');
    this.state = States.IDLE;
    this.rxContext = null;
    this.txContext = null;
  }

  /**
   * Get current state
   * @returns {string} Current state
   */
  getState() {
    return this.state;
  }

  /**
   * Check if idle
   * @returns {boolean} True if idle
   */
  isIdle() {
    return this.state === States.IDLE;
  }

  /**
   * Check if receiving
   * @returns {boolean} True if receiving
   */
  isReceiving() {
    return this.state === States.RECEIVING;
  }

  /**
   * Check if transmitting
   * @returns {boolean} True if transmitting
   */
  isTransmitting() {
    return this.state === States.TRANSMITTING;
  }

  /**
   * Check if waiting for flow control
   * @returns {boolean} True if waiting
   */
  isWaitingFC() {
    return this.state === States.WAIT_FC;
  }
}
