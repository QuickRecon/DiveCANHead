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
    this.requestDelay = options.requestDelay ?? 0;
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
    const sid = data[0];

    this.logger.debug(`UDS response: SID=0x${sid.toString(16).padStart(2, '0')}`, {
      response: ByteUtils.toHexString(data)
    });

    // Check for unsolicited WDBI (push from ECU) - handle BEFORE checking pending
    if (sid === constants.SID_WRITE_DATA_BY_ID) {
      this._handleUnsolicitedWDBI(data);
      return;  // Don't process as response
    }

    if (!this.pendingRequest) {
      this.logger.warn('Received response but no pending request');
    }

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
   * Handle unsolicited WDBI messages (push from ECU)
   * @param {Uint8Array} data - Raw message [SID, DID_hi, DID_lo, payload...]
   * @private
   */
  _handleUnsolicitedWDBI(data) {
    const did = ByteUtils.beToUint16(data.slice(1, 3));
    const payload = data.slice(3);

    this.logger.debug(`Unsolicited WDBI: DID=0x${did.toString(16).padStart(4, '0')}, len=${payload.length}`);

    if (did === constants.DID_LOG_MESSAGE) {
      const message = new TextDecoder('utf-8').decode(payload);
      this.emit('logMessage', message);
    } else if (did === constants.DID_EVENT_MESSAGE) {
      const message = new TextDecoder('utf-8').decode(payload);
      this.emit('eventMessage', message);
    } else {
      this.emit('unsolicitedMessage', { did, payload });
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

  // ============================================================
  // Log Streaming Methods
  // ============================================================

  /**
   * Enable log streaming from ECU
   * @returns {Promise<void>}
   */
  async enableLogStreaming() {
    await this.writeDataByIdentifier(constants.DID_LOG_STREAM_ENABLE, [0x01]);
    this.logger.info('Log streaming enabled');
  }

  /**
   * Disable log streaming from ECU
   * @returns {Promise<void>}
   */
  async disableLogStreaming() {
    await this.writeDataByIdentifier(constants.DID_LOG_STREAM_ENABLE, [0x00]);
    this.logger.info('Log streaming disabled');
  }

  /**
   * Check if log streaming is enabled
   * @returns {Promise<boolean>}
   */
  async isLogStreamingEnabled() {
    const data = await this.readDataByIdentifier(constants.DID_LOG_STREAM_ENABLE);
    return data[0] !== 0;
  }

  // ============================================================
  // Multi-DID Read Methods (State DID Support)
  // ============================================================

  /**
   * Read multiple DIDs in a single request
   * @param {Array<number>} dids - Array of DID addresses
   * @returns {Promise<Map<number, Uint8Array>>} Map of DID to data
   */
  async readMultipleDIDs(dids) {
    if (!dids || dids.length === 0) {
      return new Map();
    }

    // Build request: [SID, DID1_hi, DID1_lo, DID2_hi, DID2_lo, ...]
    const request = [constants.SID_READ_DATA_BY_ID];
    for (const did of dids) {
      const didBytes = ByteUtils.uint16ToBE(did);
      request.push(didBytes[0], didBytes[1]);
    }

    const response = await this._sendRequest(request);

    // Parse response: [0x62, DID1_hi, DID1_lo, data1..., DID2_hi, DID2_lo, data2..., ...]
    const result = new Map();
    let offset = 1;  // Skip response SID

    while (offset + 2 < response.length) {
      const did = ByteUtils.beToUint16(response.slice(offset, offset + 2));
      offset += 2;

      // Get expected size for this DID
      const didInfo = constants.getDIDInfo(did);
      let dataSize;
      if (didInfo) {
        dataSize = didInfo.size;
      } else {
        // Unknown DID - try to find next DID header or use rest of response
        let nextDIDOffset = response.length;
        for (let i = offset; i + 1 < response.length; i++) {
          const potentialDID = ByteUtils.beToUint16(response.slice(i, i + 2));
          if (constants.getDIDInfo(potentialDID)) {
            nextDIDOffset = i;
            break;
          }
        }
        dataSize = nextDIDOffset - offset;
      }

      const data = response.slice(offset, offset + dataSize);
      result.set(did, data);
      offset += dataSize;
    }

    return result;
  }

  /**
   * Parse a single DID value based on its type definition
   * @param {number} did - DID address
   * @param {Uint8Array} data - Raw data
   * @returns {number|boolean|undefined} Parsed value, or undefined if data insufficient
   */
  parseDIDValue(did, data) {
    const didInfo = constants.getDIDInfo(did);
    if (!didInfo) {
      return data;  // Return raw data for unknown DIDs
    }

    // Check if we have enough data for the expected type
    if (!data || data.length < didInfo.size) {
      console.warn(`DID 0x${did.toString(16)}: expected ${didInfo.size} bytes, got ${data ? data.length : 0}`);
      return undefined;
    }

    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);

    switch (didInfo.type) {
      case 'float32':
        return view.getFloat32(0, true);  // Little-endian
      case 'int32':
        return view.getInt32(0, true);
      case 'uint32':
        return view.getUint32(0, true);
      case 'int16':
        return view.getInt16(0, true);
      case 'uint16':
        return view.getUint16(0, true);
      case 'uint8':
        return data[0];
      case 'bool':
        return data[0] !== 0;
      default:
        return data;
    }
  }

  /**
   * Read multiple DIDs and parse values
   * @param {Array<number>} dids - Array of DID addresses
   * @returns {Promise<Object>} Object with DID keys and parsed values
   */
  async readDIDsParsed(dids) {
    const rawMap = await this.readMultipleDIDs(dids);
    const result = {};

    for (const [did, data] of rawMap) {
      const didInfo = constants.getDIDInfo(did);
      const key = didInfo ? didInfo.key : `0x${did.toString(16).padStart(4, '0')}`;
      result[key] = this.parseDIDValue(did, data);
    }

    return result;
  }

  /**
   * Read all control state DIDs (non-cell DIDs)
   * @returns {Promise<Object>} Object with DID keys and parsed values
   */
  async readControlState() {
    const controlDIDs = constants.getControlStateDIDs();
    const dids = Object.values(controlDIDs).map(info => info.did);
    return await this.readDIDsParsed(dids);
  }

  /**
   * Read all cell DIDs for a specific cell
   * @param {number} cellNum - Cell number (0-2)
   * @param {number} cellType - Cell type constant (to filter valid DIDs)
   * @returns {Promise<Object>} Object with DID keys and parsed values
   */
  async readCellState(cellNum, cellType) {
    const validDIDs = constants.getValidCellDIDs(cellNum, cellType);
    const dids = Object.values(validDIDs).map(info => info.did);
    return await this.readDIDsParsed(dids);
  }

  /**
   * Get cell types from configuration
   * @returns {Promise<Array<number>>} Array of 3 cell types
   */
  async getCellTypes() {
    const data = await this.readDataByIdentifier(constants.DID_CONFIGURATION_BLOCK);
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const config = view.getUint32(0, true);

    // Extract cell types from config bits 8-13 (2 bits per cell)
    return [
      (config >> 8) & 0x03,
      (config >> 10) & 0x03,
      (config >> 12) & 0x03
    ];
  }

  /**
   * Fetch all state DIDs (control + all cells with type filtering)
   * @param {Function} progressCallback - Optional callback (current, total) => void
   * @returns {Promise<Object>} Complete state object
   */
  async fetchAllState(progressCallback = null) {
    // Get cell types first
    const cellTypes = await this.getCellTypes();

    // Collect all DIDs to read
    const allDIDs = [];

    // Add control state DIDs
    const controlDIDs = constants.getControlStateDIDs();
    for (const info of Object.values(controlDIDs)) {
      allDIDs.push(info.did);
    }

    // Add cell DIDs (filtered by type)
    for (let cellNum = 0; cellNum < 3; cellNum++) {
      const validDIDs = constants.getValidCellDIDs(cellNum, cellTypes[cellNum]);
      for (const info of Object.values(validDIDs)) {
        allDIDs.push(info.did);
      }
    }

    // Split into chunks to fit within BLE MTU constraints
    // Request format: 1 (SID) + N*2 (DID bytes) must fit in ~20 byte MTU
    // Safe limit: (20-1)/2 = 9 DIDs max per request, use 8 to be safe
    const DIDS_PER_REQUEST = 4;
    const result = {};
    const totalChunks = Math.ceil(allDIDs.length / DIDS_PER_REQUEST);

    for (let i = 0; i < allDIDs.length; i += DIDS_PER_REQUEST) {
      const chunkIndex = Math.floor(i / DIDS_PER_REQUEST);
      if (progressCallback) {
        progressCallback(chunkIndex + 1, totalChunks);
      }

      const chunk = allDIDs.slice(i, i + DIDS_PER_REQUEST);
      const chunkResult = await this.readDIDsParsed(chunk);
      Object.assign(result, chunkResult);
    }

    // Add cell types to result
    result._cellTypes = cellTypes;

    return result;
  }

}

// Export constants
export * from './constants.js';
