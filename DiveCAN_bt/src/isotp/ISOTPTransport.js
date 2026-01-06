/**
 * ISO-TP (ISO 15765-2) Transport Layer
 * Handles single frame and multi-frame ISO-TP messages with extended addressing
 */

import { ByteUtils } from '../utils/ByteUtils.js';
import { ISOTPError, TimeoutError } from '../errors/ProtocolErrors.js';
import { Logger } from '../utils/Logger.js';
import { TimeoutManager } from '../utils/Timeout.js';
import { ISOTPStateMachine } from './ISOTPStateMachine.js';
import * as constants from './constants.js';

/**
 * Simple EventEmitter (reused from BLE module)
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
 * ISO-TP Transport Layer
 */
export class ISOTPTransport extends EventEmitter {
  /**
   * Create ISO-TP transport
   * @param {number} sourceAddress - Source address (e.g., 0xFF for tester)
   * @param {number} targetAddress - Target address (e.g., 0x80 for controller)
   * @param {Object} options - Options
   */
  constructor(sourceAddress, targetAddress, options = {}) {
    super();
    this.logger = new Logger('ISO-TP', 'debug');

    this.sourceAddress = sourceAddress;
    this.targetAddress = targetAddress;

    this.options = {
      blockSize: constants.DEFAULT_BLOCK_SIZE,
      stmin: constants.DEFAULT_STMIN,
      timeoutNBs: constants.TIMEOUT_N_BS,
      timeoutNCr: constants.TIMEOUT_N_CR,
      ...options
    };

    this.stateMachine = new ISOTPStateMachine();
    this.timeoutManager = new TimeoutManager();

    // Pending send promise
    this.sendPromise = null;
    this.sendResolve = null;
    this.sendReject = null;
  }

  /**
   * Send data using ISO-TP
   * Automatically chooses SF or FF+CF based on data length
   * @param {Uint8Array|Array} data - Data to send
   * @returns {Promise<void>} Resolves when complete
   */
  async send(data) {
    if (!this.stateMachine.isIdle()) {
      throw new ISOTPError('Transport busy', {
        state: this.stateMachine.getState()
      });
    }

    const dataArray = ByteUtils.toUint8Array(data);

    if (dataArray.length === 0) {
      throw new ISOTPError('Cannot send empty data');
    }

    if (dataArray.length > constants.MAX_PAYLOAD) {
      throw new ISOTPError(`Payload too large: ${dataArray.length} bytes (max: ${constants.MAX_PAYLOAD})`);
    }

    this.logger.debug(`Sending ${dataArray.length} bytes`);

    // Choose single frame or multi-frame
    if (dataArray.length <= constants.SF_MAX_PAYLOAD) {
      await this._sendSingleFrame(dataArray);
    } else {
      await this._sendMultiFrame(dataArray);
    }
  }

  /**
   * Send single frame
   * Format: [TA, 0x0N, payload...]
   * @private
   */
  async _sendSingleFrame(data) {
    const frame = new Uint8Array(2 + data.length);
    frame[0] = this.targetAddress;  // Target address
    frame[1] = 0x00 | data.length;  // PCI: Single frame + length
    frame.set(data, 2);

    this.logger.debug(`Send SF: ${data.length} bytes`, {
      frame: ByteUtils.toHexString(frame)
    });

    this.emit('frame', frame);
  }

  /**
   * Send multi-frame message
   * Format: FF, then CFs
   * @private
   */
  async _sendMultiFrame(data) {
    return new Promise((resolve, reject) => {
      this.sendResolve = resolve;
      this.sendReject = reject;

      this.stateMachine.beginTransmitting(data);

      // Send first frame
      this._sendFirstFrame(data);

      // Set timeout for FC
      this.timeoutManager.set('N_BS', this.options.timeoutNBs, () => {
        this.stateMachine.reset();
        const error = new TimeoutError('Timeout waiting for flow control', 'ISO-TP', {
          timeout: this.options.timeoutNBs
        });
        if (this.sendReject) {
          this.sendReject(error);
          this.sendReject = null;
          this.sendResolve = null;
        }
      });
    });
  }

  /**
   * Send first frame
   * Format: [TA, 0x1N, len_low, first 5 bytes...]
   * @private
   */
  _sendFirstFrame(data) {
    const length = data.length;
    const lenHigh = (length >> 8) & 0x0F;
    const lenLow = length & 0xFF;

    const frame = new Uint8Array(8);
    frame[0] = this.targetAddress;           // Target address
    frame[1] = constants.PCI_FF | lenHigh;   // PCI: First frame + high nibble of length
    frame[2] = lenLow;                        // Low byte of length
    frame.set(data.slice(0, constants.FF_FIRST_PAYLOAD), 3);  // First 5 bytes

    this.logger.debug(`Send FF: ${length} bytes total`, {
      frame: ByteUtils.toHexString(frame)
    });

    this.stateMachine.txContext.sent = constants.FF_FIRST_PAYLOAD;
    this.stateMachine.txContext.sequenceNumber = 0;

    this.emit('frame', frame);
  }

  /**
   * Send consecutive frame
   * Format: [TA, 0x2N, 6 bytes...]
   * @private
   */
  async _sendConsecutiveFrame() {
    const chunk = this.stateMachine.getNextTxChunk(constants.CF_PAYLOAD);

    const frame = new Uint8Array(2 + chunk.data.length);
    frame[0] = this.targetAddress;                              // Target address
    frame[1] = constants.PCI_CF | chunk.sequenceNumber;         // PCI: CF + sequence
    frame.set(chunk.data, 2);

    this.logger.debug(`Send CF ${chunk.sequenceNumber}: ${chunk.data.length} bytes`, {
      frame: ByteUtils.toHexString(frame)
    });

    this.emit('frame', frame);

    if (chunk.done) {
      // Transmission complete
      this.timeoutManager.clear('N_BS');
      this.stateMachine.reset();

      if (this.sendResolve) {
        this.sendResolve();
        this.sendResolve = null;
        this.sendReject = null;
      }
    } else {
      // Check if need to wait for FC again
      if (this.stateMachine.needFlowControl()) {
        this.stateMachine.transitionTo(constants.States.WAIT_FC);
        this.timeoutManager.set('N_BS', this.options.timeoutNBs, () => {
          this.stateMachine.reset();
          const error = new TimeoutError('Timeout waiting for flow control', 'ISO-TP');
          if (this.sendReject) {
            this.sendReject(error);
            this.sendReject = null;
            this.sendResolve = null;
          }
        });
      } else {
        // Continue sending after stmin delay
        const stmin = this.stateMachine.getStmin();
        setTimeout(() => this._sendConsecutiveFrame(), stmin);
      }
    }
  }

  /**
   * Process received frame
   * @param {Uint8Array|Array} frame - Received frame
   */
  processFrame(frame) {
    if (!frame || frame.length < 2) {
      this.logger.warn('Frame too short, ignoring');
      return;
    }

    const ta = frame[0];  // Target address (source in response)
    const pci = frame[1];

    // Accept frames from target or broadcast (Shearwater quirk)
    const validSource = (ta === this.sourceAddress || ta === 0xFF);
    if (!validSource) {
      this.logger.debug(`Ignoring frame from 0x${ta.toString(16)} (expected 0x${this.sourceAddress.toString(16)})`);
      return;
    }

    const pciType = pci & 0xF0;

    this.logger.debug(`Received frame: PCI=0x${pci.toString(16).padStart(2, '0')}`, {
      frame: ByteUtils.toHexString(frame)
    });

    try {
      if (pciType === constants.PCI_SF) {
        this._processSingleFrame(frame, pci);
      } else if (pciType === constants.PCI_FF) {
        this._processFirstFrame(frame, pci);
      } else if (pciType === constants.PCI_CF) {
        this._processConsecutiveFrame(frame, pci);
      } else if (pciType === constants.PCI_FC) {
        this._processFlowControl(frame, pci);
      } else {
        this.logger.warn(`Unknown PCI type: 0x${pciType.toString(16)}`);
      }
    } catch (error) {
      this.logger.error('Error processing frame', error);
      this.emit('error', error);
      this.stateMachine.reset();
    }
  }

  /**
   * Process single frame
   * @private
   */
  _processSingleFrame(frame, pci) {
    const length = pci & 0x0F;
    const data = frame.slice(2, 2 + length);

    this.logger.debug(`Received SF: ${data.length} bytes`);

    this.emit('message', data);
  }

  /**
   * Process first frame
   * @private
   */
  _processFirstFrame(frame, pci) {
    const lenHigh = pci & 0x0F;
    const lenLow = frame[2];
    const totalLength = (lenHigh << 8) | lenLow;

    this.logger.debug(`Received FF: ${totalLength} bytes total`);

    const firstData = frame.slice(3, 3 + constants.FF_FIRST_PAYLOAD);

    this.stateMachine.beginReceiving(totalLength);
    this.stateMachine.updateRxContext(firstData, 0);

    // Send flow control
    this._sendFlowControl(constants.FC_CTS, this.options.blockSize, this.options.stmin);

    // Set timeout for first CF
    this.timeoutManager.set('N_CR', this.options.timeoutNCr, () => {
      this.logger.error('Timeout waiting for consecutive frame');
      this.stateMachine.reset();
      this.emit('error', new TimeoutError('Timeout waiting for consecutive frame', 'ISO-TP'));
    });
  }

  /**
   * Process consecutive frame
   * @private
   */
  _processConsecutiveFrame(frame, pci) {
    if (!this.stateMachine.isReceiving()) {
      this.logger.warn('Received CF but not receiving, ignoring');
      return;
    }

    const seqNum = pci & 0x0F;
    const expectedSeq = (this.stateMachine.rxContext.sequenceNumber + 1) & 0x0F;

    if (seqNum !== expectedSeq) {
      this.logger.error(`Sequence error: expected ${expectedSeq}, got ${seqNum}`);
      this.stateMachine.reset();
      this.timeoutManager.clear('N_CR');
      this.emit('error', new ISOTPError('Sequence number mismatch', {
        expected: expectedSeq,
        received: seqNum
      }));
      return;
    }

    const data = frame.slice(2);
    this.stateMachine.updateRxContext(data, seqNum);

    this.logger.debug(`Received CF ${seqNum}: ${data.length} bytes (${this.stateMachine.rxContext.received}/${this.stateMachine.rxContext.totalLength})`);

    // Check if complete
    if (this.stateMachine.isRxComplete()) {
      this.timeoutManager.clear('N_CR');
      const message = this.stateMachine.getRxMessage();
      this.stateMachine.reset();

      this.logger.debug(`Message complete: ${message.length} bytes`);
      this.emit('message', message);
    } else {
      // Reset timeout for next CF
      this.timeoutManager.set('N_CR', this.options.timeoutNCr, () => {
        this.logger.error('Timeout waiting for consecutive frame');
        this.stateMachine.reset();
        this.emit('error', new TimeoutError('Timeout waiting for consecutive frame', 'ISO-TP'));
      });
    }
  }

  /**
   * Process flow control
   * @private
   */
  _processFlowControl(frame, pci) {
    if (!this.stateMachine.isWaitingFC()) {
      this.logger.warn('Received FC but not waiting, ignoring');
      return;
    }

    const flowStatus = pci & 0x0F;
    const blockSize = frame[2] || 0;
    const stmin = frame[3] || 0;

    this.logger.debug(`Received FC: status=${flowStatus}, BS=${blockSize}, STmin=${stmin}`);

    this.timeoutManager.clear('N_BS');

    if (flowStatus === constants.FC_CTS) {
      // Continue to send
      this.stateMachine.updateFlowControl(blockSize, stmin);
      this.emit('flowControl', { status: 'CTS', blockSize, stmin });

      // Send next consecutive frame
      setTimeout(() => this._sendConsecutiveFrame(), stmin);

    } else if (flowStatus === constants.FC_WAIT) {
      // Wait for another FC
      this.logger.debug('FC WAIT received');
      this.emit('flowControl', { status: 'WAIT' });

      this.timeoutManager.set('N_BS', this.options.timeoutNBs, () => {
        this.stateMachine.reset();
        const error = new TimeoutError('Timeout waiting for flow control after WAIT', 'ISO-TP');
        if (this.sendReject) {
          this.sendReject(error);
          this.sendReject = null;
          this.sendResolve = null;
        }
      });

    } else if (flowStatus === constants.FC_OVFLW) {
      // Overflow - abort
      this.logger.error('FC OVERFLOW received');
      this.stateMachine.reset();
      const error = new ISOTPError('Flow control overflow', { flowStatus });
      this.emit('flowControl', { status: 'OVERFLOW' });
      if (this.sendReject) {
        this.sendReject(error);
        this.sendReject = null;
        this.sendResolve = null;
      }
    }
  }

  /**
   * Send flow control frame
   * Format: [TA, 0x3N, BS, STmin]
   * @private
   */
  _sendFlowControl(flowStatus, blockSize, stmin) {
    const frame = new Uint8Array(8);
    frame[0] = this.targetAddress;
    frame[1] = constants.PCI_FC | flowStatus;
    frame[2] = blockSize;
    frame[3] = stmin;

    this.logger.debug(`Send FC: status=${flowStatus}, BS=${blockSize}, STmin=${stmin}`, {
      frame: ByteUtils.toHexString(frame)
    });

    this.emit('frame', frame);
  }

  /**
   * Reset transport
   */
  reset() {
    this.timeoutManager.clearAll();
    this.stateMachine.reset();

    if (this.sendReject) {
      this.sendReject(new ISOTPError('Transport reset'));
      this.sendReject = null;
      this.sendResolve = null;
    }
  }

  /**
   * Get current state
   * @returns {string} Current state
   */
  get state() {
    return this.stateMachine.getState();
  }

  /**
   * Check if idle
   * @returns {boolean} True if idle
   */
  get isIdle() {
    return this.stateMachine.isIdle();
  }
}

// Export constants
export * from './constants.js';
