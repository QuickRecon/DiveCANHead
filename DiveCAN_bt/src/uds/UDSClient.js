/**
 * UDS (Unified Diagnostic Services) Client
 * Implements UDS diagnostic services over transport layer
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
   * @param {DirectTransport} transport - Transport layer
   * @param {Object} options - Options
   */
  constructor(transport, options = {}) {
    super();
    this.logger = new Logger('UDS', 'debug');
    this.transport = transport;
    this.options = options;

    this.pendingRequest = null;
    this.pendingResolve = null;
    this.pendingReject = null;
    this.pendingTimer = null;

    // Inter-request delay (ms) - allows Petrel ISO-TP layer to settle
    this.requestDelay = options.requestDelay ?? 500;
    this.lastRequestTime = 0;

    // Set up transport message handler
    this.transport.on('message', (data) => this._handleResponse(data));
    this.transport.on('error', (error) => {
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

    // Enforce inter-request delay to allow Petrel ISO-TP layer to settle
    const now = Date.now();
    const elapsed = now - this.lastRequestTime;
    if (elapsed < this.requestDelay) {
      await new Promise(r => setTimeout(r, this.requestDelay - elapsed));
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
      this.pendingTimer = setTimeout(() => {
        this.pendingTimer = null;
        this.pendingRequest = null;
        this.pendingResolve = null;
        this.pendingReject = null;
        reject(new UDSError('Request timeout', sid, null, { timeout }));
      }, timeout);

      try {
        await this.transport.send(requestArray);
      } catch (error) {
        clearTimeout(this.pendingTimer);
        this.pendingTimer = null;
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

      if (this.pendingTimer) {
        clearTimeout(this.pendingTimer);
        this.pendingTimer = null;
      }
      if (this.pendingReject) {
        this.lastRequestTime = Date.now();  // Track completion time for inter-request delay
        this.pendingReject(error);
        this.pendingResolve = null;
        this.pendingReject = null;
        this.pendingRequest = null;
      }
      return;
    }

    // Positive response
    if (this.pendingRequest) {
      const expectedSid = this.pendingRequest[0] + constants.RESPONSE_SID_OFFSET;
      if (sid !== expectedSid) {
        this.logger.error(`Unexpected response SID: expected 0x${expectedSid.toString(16)}, got 0x${sid.toString(16)}`);
        if (this.pendingTimer) {
          clearTimeout(this.pendingTimer);
          this.pendingTimer = null;
        }
        if (this.pendingReject) {
          this.lastRequestTime = Date.now();  // Track completion time for inter-request delay
          this.pendingReject(new UDSError('Unexpected response SID', sid));
          this.pendingResolve = null;
          this.pendingReject = null;
          this.pendingRequest = null;
        }
        return;
      }
    } else { // Unprovoked message, Broadcast from HEAD?
      if(data[1] == 0xa1) { // LOG broadcast message
        const decoder = new TextDecoder('utf-8')
        console.log(`HEAD message: ${decoder.decode(data.slice(3))}`);
      }
    }



    this.emit('response', data);

    if (this.pendingTimer) {
      clearTimeout(this.pendingTimer);
      this.pendingTimer = null;
    }
    if (this.pendingResolve) {
      this.lastRequestTime = Date.now();  // Track completion time for inter-request delay
      this.pendingResolve(data);
      this.pendingResolve = null;
      this.pendingReject = null;
      this.pendingRequest = null;
    }
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
   * High-level: Read serial number
   * @returns {Promise<string>} Serial number string
   */
  async readSerialNumber() {
    const data = await this.readDataByIdentifier(constants.DID_SERIAL_NUMBER);
    return new TextDecoder().decode(data);
  }

  /**
   * High-level: Read model name
   * @returns {Promise<string>} Model name string
   */
  async readModel() {
    const data = await this.readDataByIdentifier(constants.DID_MODEL);
    return new TextDecoder().decode(data);
  }

  /**
   * High-level: Enumerate devices on the DiveCAN bus
   * @returns {Promise<Array<number>>} Array of device IDs on the bus
   */
  async enumerateBusDevices() {
    const data = await this.readDataByIdentifier(constants.DID_BUS_DEVICES);
    return Array.from(data);
  }

  /**
   * High-level: Get device name by ID
   * @param {number} deviceId - Device ID (e.g., 0x01 for NERD, 0x04 for SOLO)
   * @returns {Promise<string>} Device name
   */
  async getDeviceName(deviceId) {
    const did = constants.DID_DEVICE_NAME_BASE + deviceId;
    const data = await this.readDataByIdentifier(did);
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

  // ============================================================
  // Settings System Methods
  // ============================================================

  /**
   * Get number of settings on target device
   * @returns {Promise<number>} Setting count
   */
  async getSettingCount() {
    const data = await this.readDataByIdentifier(constants.DID_SETTING_COUNT);
    return data[0];
  }

  /**
   * Get setting metadata
   * @param {number} index - Setting index (0-based)
   * @returns {Promise<{label: string, kind: number, editable: boolean}>}
   */
  async getSettingInfo(index) {
    const did = constants.DID_SETTING_INFO_BASE + index;
    const data = await this.readDataByIdentifier(did);

    // Parse response: [label(N), 0x00, kind(1), editable(1)]
    const nullIndex = data.indexOf(0);
    const label = new TextDecoder().decode(data.slice(0, nullIndex));
    const kind = data[nullIndex + 1];
    const editable = data[nullIndex + 2] === 1;

    return { label, kind, editable };
  }

  /**
   * Get setting current and max value
   * @param {number} index - Setting index (0-based)
   * @returns {Promise<{maxValue: bigint, currentValue: bigint}>}
   */
  async getSettingValue(index) {
    const did = constants.DID_SETTING_VALUE_BASE + index;
    const data = await this.readDataByIdentifier(did);

    // Parse response: [maxValue(8 BE), currentValue(8 BE)]
    const maxValue = ByteUtils.beToUint64(data.slice(0, 8));
    const currentValue = ByteUtils.beToUint64(data.slice(8, 16));

    return { maxValue, currentValue };
  }

  /**
   * Get option label for selection-type setting
   * @param {number} settingIndex - Setting index
   * @param {number} optionIndex - Option index
   * @returns {Promise<string>} Option label
   */
  async getSettingOptionLabel(settingIndex, optionIndex) {
    const did = constants.DID_SETTING_LABEL_BASE + settingIndex + (optionIndex << 4);
    const data = await this.readDataByIdentifier(did);
    return new TextDecoder().decode(data);
  }

  /**
   * Write setting value (temporary, not persisted)
   * @param {number} index - Setting index
   * @param {bigint|number} value - New value
   * @returns {Promise<void>}
   */
  async writeSettingValue(index, value) {
    const did = constants.DID_SETTING_VALUE_BASE + index;
    const valueBytes = ByteUtils.uint64ToBE(BigInt(value));
    await this.writeDataByIdentifier(did, valueBytes);
    this.logger.info(`Wrote setting ${index} = ${value}`);
  }

  /**
   * Save setting to flash (persisted)
   * @param {number} index - Setting index
   * @param {bigint|number} value - Value to save
   * @returns {Promise<void>}
   */
  async saveSetting(index, value) {
    const did = constants.DID_SETTING_SAVE_BASE + index;
    const valueBytes = ByteUtils.uint64ToBE(BigInt(value));
    await this.writeDataByIdentifier(did, valueBytes);
    this.logger.info(`Saved setting ${index} = ${value} to flash`);
  }

  /**
   * Enumerate all settings on device
   * @returns {Promise<Array<{index: number, label: string, kind: number, editable: boolean, maxValue: bigint, currentValue: bigint}>>}
   */
  async enumerateSettings() {
    const count = await this.getSettingCount();
    this.logger.info(`Found ${count} settings`);
    const settings = [];

    for (let i = 0; i < count; i++) {
      const info = await this.getSettingInfo(i);
      const value = await this.getSettingValue(i);
      settings.push({
        index: i,
        label: info.label,
        kind: info.kind,
        editable: info.editable,
        maxValue: value.maxValue,
        currentValue: value.currentValue
      });
    }

    return settings;
  }

}

// Export constants
export * from './constants.js';
