/**
 * UDS (Unified Diagnostic Services) Client
 * Implements UDS diagnostic services over ISO-TP transport
 */

import { ByteUtils } from '../utils/ByteUtils.js';
import { UDSError, ValidationError } from '../errors/ProtocolErrors.js';
import { Logger } from '../utils/Logger.js';
import * as constants from './constants.js';

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
 * UDS Client
 */
export class UDSClient extends EventEmitter {
  /**
   * Create UDS client
   * @param {ISOTPTransport} isotpTransport - ISO-TP transport layer
   * @param {Object} options - Options
   */
  constructor(isotpTransport, options = {}) {
    super();
    this.logger = new Logger('UDS', 'debug');
    this.isotp = isotpTransport;
    this.options = options;

    this.currentSession = constants.SESSION_DEFAULT;
    this.pendingRequest = null;
    this.pendingResolve = null;
    this.pendingReject = null;

    // Set up ISO-TP message handler
    this.isotp.on('message', (data) => this._handleResponse(data));
    this.isotp.on('error', (error) => {
      if (this.pendingReject) {
        this.pendingReject(error);
        this.pendingResolve = null;
        this.pendingReject = null;
        this.pendingRequest = null;
      }
      this.emit('error', error);
    });
  }

  /**
   * Send UDS request and wait for response
   * @param {Array|Uint8Array} request - UDS request
   * @param {number} timeout - Timeout in ms (default: 5000)
   * @returns {Promise<Uint8Array>} Response data
   * @private
   */
  async _sendRequest(request, timeout = 5000) {
    if (this.pendingRequest) {
      throw new UDSError('Request already pending', 0);
    }

    const requestArray = ByteUtils.toUint8Array(request);
    const sid = requestArray[0];

    this.logger.debug(`UDS request: SID=0x${sid.toString(16).padStart(2, '0')}`, {
      request: ByteUtils.toHexString(requestArray)
    });

    return new Promise(async (resolve, reject) => {
      this.pendingRequest = requestArray;
      this.pendingResolve = resolve;
      this.pendingReject = reject;

      // Set timeout
      const timer = setTimeout(() => {
        this.pendingRequest = null;
        this.pendingResolve = null;
        this.pendingReject = null;
        reject(new UDSError('Request timeout', sid, null, { timeout }));
      }, timeout);

      try {
        await this.isotp.send(requestArray);
      } catch (error) {
        clearTimeout(timer);
        this.pendingRequest = null;
        this.pendingResolve = null;
        this.pendingReject = null;
        reject(new UDSError('Failed to send request', sid, null, { cause: error }));
      }
    });
  }

  /**
   * Handle UDS response
   * @private
   */
  _handleResponse(data) {
    if (!this.pendingRequest) {
      this.logger.warn('Received response but no pending request');
      return;
    }

    const sid = data[0];

    this.logger.debug(`UDS response: SID=0x${sid.toString(16).padStart(2, '0')}`, {
      response: ByteUtils.toHexString(data)
    });

    // Check for negative response
    if (sid === constants.SID_NEGATIVE_RESPONSE) {
      const requestedSid = data[1];
      const nrc = data[2];

      this.logger.warn(`Negative response: SID=0x${requestedSid.toString(16)}, NRC=0x${nrc.toString(16)}`);

      const error = new UDSError('Negative response', requestedSid, nrc);
      this.emit('negativeResponse', { sid: requestedSid, nrc, description: error.getNRCDescription() });

      if (this.pendingReject) {
        this.pendingReject(error);
        this.pendingResolve = null;
        this.pendingReject = null;
        this.pendingRequest = null;
      }
      return;
    }

    // Positive response
    const expectedSid = this.pendingRequest[0] + constants.RESPONSE_SID_OFFSET;
    if (sid !== expectedSid) {
      this.logger.error(`Unexpected response SID: expected 0x${expectedSid.toString(16)}, got 0x${sid.toString(16)}`);
      if (this.pendingReject) {
        this.pendingReject(new UDSError('Unexpected response SID', sid));
        this.pendingResolve = null;
        this.pendingReject = null;
        this.pendingRequest = null;
      }
      return;
    }

    this.emit('response', data);

    if (this.pendingResolve) {
      this.pendingResolve(data);
      this.pendingResolve = null;
      this.pendingReject = null;
      this.pendingRequest = null;
    }
  }

  /**
   * Start diagnostic session
   * Service 0x10: DiagnosticSessionControl
   * @param {number} sessionType - Session type
   * @returns {Promise<void>}
   */
  async startSession(sessionType) {
    const request = [constants.SID_DIAGNOSTIC_SESSION_CONTROL, sessionType];
    await this._sendRequest(request);
    this.currentSession = sessionType;
    this.logger.info(`Session changed to: ${sessionType}`);
  }

  /**
   * Read data by identifier
   * Service 0x22: ReadDataByIdentifier
   * @param {number} did - Data identifier
   * @returns {Promise<Uint8Array>} Data
   */
  async readDataByIdentifier(did) {
    const didBytes = ByteUtils.uint16ToBE(did);
    const request = [constants.SID_READ_DATA_BY_ID, didBytes[0], didBytes[1]];
    const response = await this._sendRequest(request);

    // Response: [0x62, DID_high, DID_low, ...data]
    const responseDid = ByteUtils.beToUint16(response.slice(1, 3));
    if (responseDid !== did) {
      throw new UDSError('DID mismatch in response', constants.SID_READ_DATA_BY_ID, null, {
        expectedDid: did,
        receivedDid: responseDid
      });
    }

    return response.slice(3);  // Return data only
  }

  /**
   * Write data by identifier
   * Service 0x2E: WriteDataByIdentifier
   * @param {number} did - Data identifier
   * @param {Uint8Array|Array} data - Data to write
   * @returns {Promise<void>}
   */
  async writeDataByIdentifier(did, data) {
    // Validate session
    if (this.currentSession === constants.SESSION_DEFAULT) {
      throw new UDSError('Write not allowed in default session', constants.SID_WRITE_DATA_BY_ID, null, {
        requiredSession: 'EXTENDED or PROGRAMMING'
      });
    }

    const didBytes = ByteUtils.uint16ToBE(did);
    const dataArray = ByteUtils.toUint8Array(data);
    const request = ByteUtils.concat([constants.SID_WRITE_DATA_BY_ID], didBytes, dataArray);

    await this._sendRequest(request);
    this.logger.info(`Wrote ${dataArray.length} bytes to DID 0x${did.toString(16).padStart(4, '0')}`);
  }

  /**
   * Request upload (read from device memory)
   * Service 0x35: RequestUpload
   * @param {number} address - Memory address
   * @param {number} length - Number of bytes to read
   * @returns {Promise<number>} Maximum block length
   */
  async requestUpload(address, length) {
    const addressBytes = ByteUtils.uint32ToBE(address);
    const lengthBytes = ByteUtils.uint32ToBE(length);

    const request = ByteUtils.concat(
      [constants.SID_REQUEST_UPLOAD, constants.DATA_FORMAT_UNCOMPRESSED, constants.ADDRESS_AND_LENGTH_FORMAT],
      addressBytes,
      lengthBytes
    );

    const response = await this._sendRequest(request);

    // Response: [0x75, lengthFormatIdentifier, maxBlockLength...]
    const lengthFormat = response[1];
    const maxBlockLength = ByteUtils.beToUint16(response.slice(2, 4));

    this.logger.info(`Upload request accepted: maxBlockLength=${maxBlockLength}`);
    return maxBlockLength;
  }

  /**
   * Request download (write to device memory)
   * Service 0x34: RequestDownload
   * @param {number} address - Memory address
   * @param {number} length - Number of bytes to write
   * @returns {Promise<number>} Maximum block length
   */
  async requestDownload(address, length) {
    const addressBytes = ByteUtils.uint32ToBE(address);
    const lengthBytes = ByteUtils.uint32ToBE(length);

    const request = ByteUtils.concat(
      [constants.SID_REQUEST_DOWNLOAD, constants.DATA_FORMAT_UNCOMPRESSED, constants.ADDRESS_AND_LENGTH_FORMAT],
      addressBytes,
      lengthBytes
    );

    const response = await this._sendRequest(request);

    // Response: [0x74, lengthFormatIdentifier, maxBlockLength...]
    const maxBlockLength = ByteUtils.beToUint16(response.slice(2, 4));

    this.logger.info(`Download request accepted: maxBlockLength=${maxBlockLength}`);
    return maxBlockLength;
  }

  /**
   * Transfer data block
   * Service 0x36: TransferData
   * @param {number} sequence - Block sequence counter (1-255)
   * @param {Uint8Array|Array} data - Data to transfer (for download) or null (for upload)
   * @returns {Promise<Uint8Array>} Transferred data (for upload) or empty (for download)
   */
  async transferData(sequence, data = null) {
    if (sequence < 1 || sequence > 255) {
      throw new ValidationError('Sequence must be 1-255', 'UDS', { sequence });
    }

    let request;
    if (data !== null) {
      // Download (write)
      const dataArray = ByteUtils.toUint8Array(data);
      request = ByteUtils.concat([constants.SID_TRANSFER_DATA, sequence], dataArray);
    } else {
      // Upload (read)
      request = [constants.SID_TRANSFER_DATA, sequence];
    }

    const response = await this._sendRequest(request);

    // Response: [0x76, sequence, ...data]
    const responseSeq = response[1];
    if (responseSeq !== sequence) {
      throw new UDSError('Sequence mismatch', constants.SID_TRANSFER_DATA, null, {
        expectedSeq: sequence,
        receivedSeq: responseSeq
      });
    }

    return response.slice(2);  // Return data (empty for download, payload for upload)
  }

  /**
   * Request transfer exit
   * Service 0x37: RequestTransferExit
   * @returns {Promise<void>}
   */
  async requestTransferExit() {
    const request = [constants.SID_REQUEST_TRANSFER_EXIT];
    await this._sendRequest(request);
    this.logger.info('Transfer exit complete');
  }

  /**
   * High-level: Upload memory from device
   * @param {number} address - Memory address
   * @param {number} length - Number of bytes
   * @param {Function} progressCallback - Progress callback (bytesRead, totalBytes)
   * @returns {Promise<Uint8Array>} Memory data
   */
  async uploadMemory(address, length, progressCallback = null) {
    this.logger.info(`Uploading ${length} bytes from 0x${address.toString(16).padStart(8, '0')}`);

    // Request upload
    const maxBlockLength = await this.requestUpload(address, length);

    // Transfer data in blocks
    const buffer = new Uint8Array(length);
    let offset = 0;
    let sequence = 1;

    while (offset < length) {
      const blockSize = Math.min(maxBlockLength, length - offset);
      const data = await this.transferData(sequence);

      buffer.set(data, offset);
      offset += data.length;

      if (progressCallback) {
        progressCallback(offset, length);
      }

      sequence = (sequence % 255) + 1;  // Wrap at 255, skip 0
    }

    // Exit transfer
    await this.requestTransferExit();

    this.logger.info(`Upload complete: ${offset} bytes`);
    return buffer;
  }

  /**
   * High-level: Download memory to device
   * @param {number} address - Memory address
   * @param {Uint8Array|Array} data - Data to write
   * @param {Function} progressCallback - Progress callback (bytesWritten, totalBytes)
   * @returns {Promise<void>}
   */
  async downloadMemory(address, data, progressCallback = null) {
    const dataArray = ByteUtils.toUint8Array(data);
    const length = dataArray.length;

    this.logger.info(`Downloading ${length} bytes to 0x${address.toString(16).padStart(8, '0')}`);

    // Request download
    const maxBlockLength = await this.requestDownload(address, length);

    // Transfer data in blocks
    let offset = 0;
    let sequence = 1;

    while (offset < length) {
      const blockSize = Math.min(maxBlockLength, length - offset);
      const block = dataArray.slice(offset, offset + blockSize);

      await this.transferData(sequence, block);

      offset += blockSize;

      if (progressCallback) {
        progressCallback(offset, length);
      }

      sequence = (sequence % 255) + 1;  // Wrap at 255, skip 0
    }

    // Exit transfer
    await this.requestTransferExit();

    this.logger.info(`Download complete: ${offset} bytes`);
  }

  /**
   * High-level: Read firmware version
   * @returns {Promise<string>} Firmware version string
   */
  async readFirmwareVersion() {
    const data = await this.readDataByIdentifier(constants.DID_FIRMWARE_VERSION);
    return new TextDecoder().decode(data);
  }

  /**
   * High-level: Read hardware version
   * @returns {Promise<number>} Hardware version
   */
  async readHardwareVersion() {
    const data = await this.readDataByIdentifier(constants.DID_HARDWARE_VERSION);
    return data[0];
  }

  /**
   * High-level: Read configuration
   * @returns {Promise<Uint8Array>} Configuration data
   */
  async readConfiguration() {
    return await this.readDataByIdentifier(constants.DID_CONFIGURATION_BLOCK);
  }

  /**
   * High-level: Write configuration
   * @param {Uint8Array|Array} config - Configuration data
   * @returns {Promise<void>}
   */
  async writeConfiguration(config) {
    await this.writeDataByIdentifier(constants.DID_CONFIGURATION_BLOCK, config);
  }

  /**
   * Get current session state
   * @returns {number} Current session
   */
  get sessionState() {
    return this.currentSession;
  }

  /**
   * Check if in programming session
   * @returns {boolean} True if in programming session
   */
  get isInProgrammingSession() {
    return this.currentSession === constants.SESSION_PROGRAMMING;
  }

  /**
   * Check if in extended diagnostic session
   * @returns {boolean} True if in extended diagnostic session
   */
  get isInExtendedSession() {
    return this.currentSession === constants.SESSION_EXTENDED_DIAGNOSTIC;
  }
}

// Export constants
export * from './constants.js';
