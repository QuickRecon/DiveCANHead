/**
 * UDS (Unified Diagnostic Services) Client
 * Implements UDS diagnostic services over transport layer
 */

import { ByteUtils } from '../utils/ByteUtils.js';
import { UDSError } from '../errors/ProtocolErrors.js';
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

    return new Promise((resolve, reject) => {
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

      // Send request - handle rejection via promise chain
      this.transport.send(requestArray).catch(error => {
        clearTimeout(this.pendingTimer);
        this.pendingTimer = null;
        this.pendingRequest = null;
        this.pendingResolve = null;
        this.pendingReject = null;
        reject(new UDSError('Failed to send request', sid, null, { cause: error }));
      });
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

    // Check for unsolicited WDBI (push from Head) - handle BEFORE checking pending
    if (sid === constants.SID_WRITE_DATA_BY_ID) {
      this._handleUnsolicitedWDBI(data);
      return;  // Don't process as response
    }

    if (!this.pendingRequest) {
      this.logger.warn('Received response but no pending request');
      return;
    }

    // Check for negative response
    if (sid === constants.SID_NEGATIVE_RESPONSE) {
      this._handleNegativeResponse(data);
      return;
    }

    // Positive response
    this._handlePositiveResponse(data, sid);
  }

  /**
   * Handle negative response
   * @private
   */
  _handleNegativeResponse(data) {
    const requestedSid = data[1];
    const nrc = data[2];

    this.logger.warn(`Negative response: SID=0x${requestedSid.toString(16)}, NRC=0x${nrc.toString(16)}`);

    const error = new UDSError('Negative response', requestedSid, nrc);
    this.emit('negativeResponse', { sid: requestedSid, nrc, description: error.getNRCDescription() });

    this._clearPendingRequest();
    if (this.pendingReject) {
      this.lastRequestTime = Date.now();
      this.pendingReject(error);
      this.pendingResolve = null;
      this.pendingReject = null;
      this.pendingRequest = null;
    }
  }

  /**
   * Handle positive response
   * @private
   */
  _handlePositiveResponse(data, sid) {
    const expectedSid = this.pendingRequest[0] + constants.RESPONSE_SID_OFFSET;
    if (sid !== expectedSid) {
      this.logger.error(`Unexpected response SID: expected 0x${expectedSid.toString(16)}, got 0x${sid.toString(16)}`);
      this._clearPendingRequest();
      if (this.pendingReject) {
        this.lastRequestTime = Date.now();
        this.pendingReject(new UDSError('Unexpected response SID', sid));
        this.pendingResolve = null;
        this.pendingReject = null;
        this.pendingRequest = null;
      }
      return;
    }

    this.emit('response', data);

    this._clearPendingRequest();
    if (this.pendingResolve) {
      this.lastRequestTime = Date.now();
      this.pendingResolve(data);
      this.pendingResolve = null;
      this.pendingReject = null;
      this.pendingRequest = null;
    }
  }

  /**
   * Clear pending request timer
   * @private
   */
  _clearPendingRequest() {
    if (this.pendingTimer) {
      clearTimeout(this.pendingTimer);
      this.pendingTimer = null;
    }
  }

  /**
   * Handle unsolicited WDBI messages (push from Head)
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
  // Control Methods (Setpoint, Calibration)
  // ============================================================

  /**
   * Write setpoint value
   * @param {number} ppo2 - Setpoint in centibar (0-255 = 0.00-2.55 bar)
   * @returns {Promise<void>}
   */
  async writeSetpoint(ppo2) {
    if (ppo2 < 0 || ppo2 > 255) {
      throw new Error('Setpoint must be 0-255 (centibar)');
    }
    await this.writeDataByIdentifier(constants.DID_SETPOINT_WRITE, [Math.round(ppo2)]);
    this.logger.info(`Set setpoint to ${ppo2} centibar (${(ppo2 / 100).toFixed(2)} bar)`);
  }

  /**
   * Trigger calibration with specified fO2
   * Uses current atmospheric pressure from device
   * @param {number} fO2 - Oxygen fraction percentage (0-100)
   * @returns {Promise<void>}
   */
  async triggerCalibration(fO2) {
    if (fO2 < 0 || fO2 > 100) {
      throw new Error('fO2 must be 0-100 (percentage)');
    }
    await this.writeDataByIdentifier(constants.DID_CALIBRATION_TRIGGER, [Math.round(fO2)]);
    this.logger.info(`Triggered calibration with fO2=${fO2}%`);
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
   * Fetch all state DIDs (control + all cells)
   * @param {Array<number>} cellTypes - Array of 3 cell types (from settings)
   * @param {Function} progressCallback - Optional callback (current, total) => void
   * @returns {Promise<Object>} Complete state object
   */
  async fetchAllState(cellTypes, progressCallback = null) {
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
