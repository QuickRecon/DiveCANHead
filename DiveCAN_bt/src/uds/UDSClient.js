/**
 * UDS (Unified Diagnostic Services) Client
 * Implements UDS diagnostic services over transport layer
 */

import { ByteUtils } from '../utils/ByteUtils.js';
import { UDSError } from '../errors/ProtocolErrors.js';
import { Logger } from '../utils/Logger.js';
import { decodeErrorHistogram } from '../errors/ErrorHistogram.js';
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
    this.pendingResponseMatcher = null;
    this.lastResponseTime = 0;

    // Serialization queue. Concurrent callers (background DID polling +
    // user-driven settings reads / manual solenoid fire) take turns on the
    // single ISO-TP context instead of colliding. Overlapping sends clobber
    // each other's frames on the wire and corrupt the pending-request slot,
    // which surfaced as spurious "Request already pending" throws and
    // intermittent bus errors. When idle, a request is dispatched synchronously
    // (the timeout timer is armed in the same tick) so timing semantics match a
    // direct send; only when a request is already in flight does the next one
    // wait its turn.
    this._requestQueue = [];
    this._requestBusy = false;

    // Inter-request delay (ms) - allows Petrel ISO-TP layer to settle
    this.requestDelay = options.requestDelay ?? 0;
    this.lastRequestTime = 0;

    // Set up transport message handler
    this.transport.on('message', (data) => this._handleResponse(data));
    this.transport.on('error', (error) => {
      if (this.pendingReject) {
        this._clearPendingRequest();
        this.pendingReject(error);
        this.pendingResolve = null;
        this.pendingReject = null;
        this.pendingRequest = null;
        this.pendingResponseMatcher = null;
      }
      this.emit('error', error);
    });
  }

  /**
   * Send UDS request and wait for response
   * @param {Array|Uint8Array} request - UDS request
   * @param {number} timeout - Timeout in ms (default: 5000)
   * @param {(response: Uint8Array) => boolean} [responseMatcher] - Optional
   *   service-specific positive-response correlation guard
   * @returns {Promise<Uint8Array>} Response data
   * @private
   */
  _sendRequest(request, timeout = 5000, responseMatcher = null) {
    // Enqueue and pump. Callers are serialized rather than rejected: a
    // background poll and a user action can be issued at the same time and
    // simply take turns on the single ISO-TP context.
    return new Promise((resolve, reject) => {
      this._requestQueue.push({ request, timeout, responseMatcher, resolve, reject });
      this._pumpRequestQueue();
    });
  }

  /**
   * Dispatch the next queued request if none is in flight. Runs the dispatch
   * synchronously when idle so the timeout timer is armed in the same tick as
   * the caller; the queue is advanced (regardless of resolve/reject) once the
   * in-flight exchange settles, so one failed request never wedges the queue.
   * @private
   */
  _pumpRequestQueue() {
    if (this._requestBusy) {
      return;
    }
    const job = this._requestQueue.shift();
    if (!job) {
      return;
    }
    this._requestBusy = true;
    this._dispatchRequest(job.request, job.timeout, job.responseMatcher).then(job.resolve, job.reject).then(
      () => {
        this._requestBusy = false;
        this._pumpRequestQueue();
      },
      () => {
        this._requestBusy = false;
        this._pumpRequestQueue();
      }
    );
  }

  /**
   * Perform a single UDS request/response exchange. Assumes the caller has
   * serialized access via the request queue so only one exchange is ever in
   * flight.
   * @private
   */
  async _dispatchRequest(request, timeout, responseMatcher = null) {
    if (this.pendingRequest) {
      // Should be unreachable now that _sendRequest serializes access; treated
      // as an assertion against a future serialization regression.
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
      this.pendingResponseMatcher = responseMatcher;

      // Set timeout
      this.pendingTimer = setTimeout(() => {
        this.pendingTimer = null;
        this.pendingRequest = null;
        this.pendingResolve = null;
        this.pendingReject = null;
        this.pendingResponseMatcher = null;
        reject(new UDSError('Request timeout', sid, null, { timeout }));
      }, timeout);

      // Send request - handle rejection via promise chain
      this.transport.send(requestArray).catch(error => {
        clearTimeout(this.pendingTimer);
        this.pendingTimer = null;
        this.pendingRequest = null;
        this.pendingResolve = null;
        this.pendingReject = null;
        this.pendingResponseMatcher = null;
        reject(new UDSError('Failed to send request', sid, null, { cause: error }));
      });
    });
  }

  /**
   * Abort the in-flight request and flush the queue, rejecting every waiter.
   *
   * Called by the protocol stack when the transport drops so callers fail
   * fast instead of waiting out their timeouts. The rejection carries
   * `details.disconnected` and a null NRC, which retry logic upstream
   * (OTAManager) classifies as a transient transport error.
   * @param {Error} [reason] - Rejection reason; defaults to a disconnect UDSError
   */
  abortPending(reason) {
    const error = reason ?? new UDSError('Transport disconnected', 0, null, { disconnected: true });

    const queued = this._requestQueue.splice(0, this._requestQueue.length);
    for (const job of queued) {
      job.reject(error);
    }

    this._clearPendingRequest();
    if (this.pendingReject) {
      const reject = this.pendingReject;
      this.pendingRequest = null;
      this.pendingResolve = null;
      this.pendingReject = null;
      this.pendingResponseMatcher = null;
      this.logger.warn(`Aborting in-flight request: ${error.message}`);
      reject(error);
    }
  }

  /**
   * Handle UDS response
   * @private
   */
  _handleResponse(data) {
    const sid = data[0];
    this.lastResponseTime = Date.now();

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
   * Process a message from a receive-only side channel such as the CANable
   * 0xFF log context. Only unsolicited WDBI pushes are accepted here, so a
   * broadcast can never satisfy or reject an addressed dialog request.
   */
  processUnsolicited(data) {
    const bytes = ByteUtils.toUint8Array(data);
    if (bytes[0] !== constants.SID_WRITE_DATA_BY_ID) {
      this.logger.warn('Ignoring non-WDBI message on unsolicited channel');
      return false;
    }
    this._handleUnsolicitedWDBI(bytes);
    return true;
  }

  /**
   * Handle negative response
   * @private
   */
  _handleNegativeResponse(data) {
    const requestedSid = data[1];
    const nrc = data[2];

    if (requestedSid !== this.pendingRequest[0]) {
      this.logger.warn(`Ignoring stale negative response for SID 0x${requestedSid.toString(16)}`);
      return;
    }

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
      this.pendingResponseMatcher = null;
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
        this.pendingResponseMatcher = null;
      }
      return;
    }

    if (this.pendingResponseMatcher && !this.pendingResponseMatcher(data)) {
      this.logger.warn(`Ignoring stale or mismatched positive response for SID 0x${sid.toString(16)}`);
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
      this.pendingResponseMatcher = null;
    }
  }

  /**
   * Wait until the receive side has been quiet for a full interval.
   *
   * OTA calls this after a timed-out request so a response that was merely
   * late is consumed while no new request is pending, rather than being
   * mistaken for the retry or the following block.
   * @param {number} quietMs
   * @param {AbortSignal} [signal]
   */
  async waitForResponseQuiescence(quietMs, signal = null) {
    let lastActivity = Math.max(this.lastResponseTime, Date.now());
    for (;;) {
      if (signal?.aborted) {
        throw new UDSError('Response quiet wait aborted', 0, null, { aborted: true });
      }
      const remaining = quietMs - (Date.now() - lastActivity);
      if (remaining <= 0) return;
      await this._abortableDelay(remaining, signal);
      if (this.lastResponseTime > lastActivity) {
        lastActivity = this.lastResponseTime;
      }
    }
  }

  /** @private */
  _abortableDelay(ms, signal = null) {
    return new Promise((resolve, reject) => {
      let timer = null;
      const onAbort = () => {
        clearTimeout(timer);
        signal?.removeEventListener('abort', onAbort);
        reject(new UDSError('Response quiet wait aborted', 0, null, { aborted: true }));
      };
      timer = setTimeout(() => {
        signal?.removeEventListener('abort', onAbort);
        resolve();
      }, ms);
      signal?.addEventListener('abort', onAbort, { once: true });
    });
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

  // ============================================================
  // Error histogram (DID 0xF260 read, 0xF261 clear)
  // ============================================================

  /**
   * Read and decode the per-code error histogram (DID 0xF260).
   * @returns {Promise<Array<{index:number, name:string, category:string,
   *   description:string, count:number}>>} One entry per error code, enum order.
   */
  async readErrorHistogram() {
    const data = await this.readDataByIdentifier(constants.DID_ERROR_HISTOGRAM);
    return decodeErrorHistogram(data);
  }

  /**
   * Clear all error-histogram counters and persist the reset to NVS
   * (DID 0xF261 accepts any byte payload).
   * @returns {Promise<void>}
   */
  async clearErrorHistogram() {
    await this.writeDataByIdentifier(constants.DID_ERROR_HISTOGRAM_CLEAR, [0x01]);
    this.logger.info('Cleared error histogram');
  }

  /** Read the five-entry persisted crash ring (DID 0xF255). */
  async readCrashHistory() {
    const data = await this.readDataByIdentifier(constants.DID_CRASH_HISTORY);
    return constants.decodeCrashHistory(data);
  }

  /** Read the five-entry persisted reboot-cause ring (DID 0xF256). */
  async readRebootHistory() {
    const data = await this.readDataByIdentifier(constants.DID_REBOOT_HISTORY);
    return constants.decodeRebootHistory(data);
  }

  // ============================================================
  // Generic UDS services (session / routine / transfer)
  // ============================================================

  /**
   * Diagnostic Session Control (Service 0x10).
   * @param {number} session - UDS_SESSION_DEFAULT | UDS_SESSION_PROGRAMMING
   * @param {number} timeout - Timeout in ms (programming entry can be slow)
   * @returns {Promise<Uint8Array>} Positive response [0x50, session, ...]
   */
  async enterSession(session, timeout = 8000) {
    const request = [constants.SID_SESSION_CONTROL, session];
    return await this._sendRequest(request, timeout);
  }

  /**
   * RoutineControl - Start Routine (Service 0x31 0x01).
   * @param {number} routineId - 16-bit routine identifier
   * @param {Array|Uint8Array} params - Routine parameters
   * @param {number} timeout - Timeout in ms
   * @returns {Promise<Uint8Array>} Positive response [0x71, 0x01, rid_hi, rid_lo, ...]
   */
  async routineControl(routineId, params = [], timeout = 10000) {
    const ridBytes = ByteUtils.uint16ToBE(routineId);
    const request = ByteUtils.concat(
      [constants.SID_ROUTINE_CONTROL, 0x01, ridBytes[0], ridBytes[1]],
      ByteUtils.toUint8Array(params.length ? params : [])
    );
    return await this._sendRequest(request, timeout);
  }

  /**
   * RequestDownload (Service 0x34).
   * @param {number} address - Memory address (LE in the addr field)
   * @param {number} size - Transfer size / negotiated block hint
   * @param {Object} [options]
   * @param {'BE'|'LE'} [options.sizeEndian='BE'] - Endianness of the SIZE field.
   *   OTA uses big-endian; log download uses little-endian.
   * @param {number} [timeout=30000] - Timeout in ms (slot1 erase is slow)
   * @returns {Promise<number>} Negotiated max block from the 0x74 response
   */
  async requestDownload(address, size, options = {}, timeout = 30000) {
    const sizeEndian = options.sizeEndian || 'BE';
    const addrBytes = ByteUtils.uint32ToLE(address);
    const sizeBytes = sizeEndian === 'LE'
      ? ByteUtils.uint32ToLE(size)
      : ByteUtils.uint32ToBE(size);
    const request = ByteUtils.concat(
      [constants.SID_REQUEST_DOWNLOAD, constants.OTA_DATA_FMT, constants.OTA_ADDR_LEN_FMT],
      addrBytes,
      sizeBytes
    );
    const response = await this._sendRequest(request, timeout);
    // Response: [0x74, lengthFormat, maxBlock_hi, maxBlock_lo]
    if (response.length < 4) {
      throw new UDSError('Malformed RequestDownload response', constants.SID_REQUEST_DOWNLOAD);
    }
    return (response[2] << 8) | response[3];
  }

  /**
   * TransferData (Service 0x36).
   * @param {number} seq - Block sequence counter
   * @param {Array|Uint8Array} data - Block payload (empty for log pull)
   * @param {number} timeout - Timeout in ms
   * @returns {Promise<Uint8Array>} Positive response [0x76, seq, ...body]
   */
  async transferData(seq, data = [], timeout = 15000) {
    const expectedSeq = seq & 0xFF;
    const request = ByteUtils.concat(
      [constants.SID_TRANSFER_DATA, expectedSeq],
      ByteUtils.toUint8Array(data.length ? data : [])
    );
    return await this._sendRequest(request, timeout,
      response => response.length >= 2 && response[1] === expectedSeq);
  }

  /**
   * RequestTransferExit (Service 0x37).
   * @param {number} timeout - Timeout in ms
   * @returns {Promise<Uint8Array>} Positive response [0x77]
   */
  async requestTransferExit(timeout = 10000) {
    return await this._sendRequest([constants.SID_REQUEST_TRANSFER_EXIT], timeout);
  }

  // ============================================================
  // High-level device identification
  // ============================================================

  /**
   * High-level: Read firmware version (git-describe ASCII)
   * @returns {Promise<string>}
   */
  async readFirmwareVersion() {
    const data = await this.readDataByIdentifier(constants.DID_FIRMWARE_VERSION);
    return ByteUtils.trimTrailing(new TextDecoder().decode(data), '\0');
  }

  /**
   * High-level: Read build variant name (ASCII)
   * @returns {Promise<string>}
   */
  async readVariantName() {
    const data = await this.readDataByIdentifier(constants.DID_VARIANT_NAME);
    return ByteUtils.trimTrailing(new TextDecoder().decode(data), '\0');
  }

  /**
   * High-level: Read serial number (raw MCU UID, returned as hex string)
   * @returns {Promise<string>}
   */
  async readSerialNumber() {
    const data = await this.readDataByIdentifier(constants.DID_SERIAL_NUMBER);
    return ByteUtils.toHexString(data, '');
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
   * Get setting metadata.
   *
   * Firmware wire layout (fixed offsets, uds_settings.c readSettingInfoDID):
   *   label[9 padded] + separator(1) + kind(1) + editable(1)
   *   for TEXT settings only: + maxValue(1) + optionCount(1)
   *
   * @param {number} index - Setting index (0-based)
   * @returns {Promise<{label: string, kind: number, editable: boolean, optionCount: number}>}
   */
  async getSettingInfo(index) {
    const did = constants.DID_SETTING_INFO_BASE + index;
    const data = await this.readDataByIdentifier(did);

    const L = constants.SETTING_LABEL_LEN;
    // Label is a fixed-width padded field (trim trailing NUL/space padding).
    const label = ByteUtils.trimTrailing(new TextDecoder().decode(data.slice(0, L)), '\0 ');
    const kind = data[L + 1];
    const editable = data[L + 2] === 1;

    let optionCount = 0;
    if (kind === constants.SETTING_KIND_TEXT && data.length >= L + 5) {
      // data[L+3] = maxValue, data[L+4] = optionCount
      optionCount = data[L + 4];
    }

    return { label, kind, editable, optionCount };
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
    // Firmware DID decode: HIGH nibble = setting index, LOW nibble = option index.
    const did = constants.DID_SETTING_LABEL_BASE + (settingIndex << 4) + optionIndex;
    const data = await this.readDataByIdentifier(did);
    // 9-byte space-padded, no NUL terminator.
    return ByteUtils.trimTrailing(new TextDecoder().decode(data), '\0 ');
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
        optionCount: info.optionCount,
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

  /**
   * HIL raw solenoid fire (DID 0xF242). Fires the given channel for a fixed
   * ~1.5 s. Requires a programming session, surface (not in dive), and PPO2
   * mode OFF — the firmware returns an NRC otherwise.
   * @param {number} channel - Solenoid channel (0-based)
   * @returns {Promise<void>}
   */
  async writeSolenoidOverride(channel = 0) {
    await this.writeDataByIdentifier(
      constants.DID_SOLENOID_OVERRIDE,
      [channel & 0xFF, constants.SOLENOID_OVERRIDE_MAGIC]
    );
    this.logger.info(`Solenoid override fired on channel ${channel}`);
  }

  // ============================================================
  // PID Autotune Methods (DID 0xF243 control, 0xF213 status)
  // ============================================================

  /**
   * Start an on-device PID autotune run (DID 0xF243, write-only).
   * Requires a programming session; the firmware also refuses (NRC 0x22) while
   * diving or when the PPO2 control mode is not PID.
   * @param {Object} params
   * @param {number} params.baseCb - Base setpoint in centibar (e.g. 70 = 0.70 bar)
   * @param {number} params.excitationDutyPct - Incremental duty pulse (5..30 percent)
   * @returns {Promise<void>}
   */
  async autotuneStart({ baseCb, excitationDutyPct }) {
    const data = [
      constants.AUTOTUNE_CMD_START,
      constants.AUTOTUNE_MAGIC,
      baseCb & 0xFF,
      excitationDutyPct & 0xFF,
      0x00,
      0x01
    ];
    await this.writeDataByIdentifier(constants.DID_AUTOTUNE_CONTROL, data);
    this.logger.info(`Autotune START base=${baseCb}cb dutyStep=${excitationDutyPct}%`);
  }

  /**
   * Abort an in-progress autotune run (DID 0xF243, write-only).
   * @returns {Promise<void>}
   */
  async autotuneAbort() {
    await this.writeDataByIdentifier(
      constants.DID_AUTOTUNE_CONTROL,
      [constants.AUTOTUNE_CMD_ABORT, constants.AUTOTUNE_MAGIC]
    );
    this.logger.info('Autotune ABORT');
  }

  /**
   * Read autotune status (DID 0xF213). Parses the compact 66-byte LE struct.
   *
   * Wire layout (offsets into the returned data bytes):
   *   [0]  state (u8) [1] abort_reason (u8) [2] iteration (u16) [4] budget (u16)
   *   [6] best_kp (f32) [10] best_ki (f32) [14] best_kd (f32)
   *   [18] response-tail noise (f32) [22] elapsed_s (u32)
   *   [26] plant_gain [30] dead_time_s [34] recovery_s [38] tail_noise
   *   [42] mixing_excursion [46] baseline_duty [50] baseline_slope
   *   [54] ambient_pressure [58] delivered_dose [62] baseline_noise
   *
   * @returns {Promise<{state:number, stateName:string, abortReason:number,
   *   abortReasonName:string, iteration:number, budget:number,
   *   cand:{kp:number, ki:number, kd:number}, best:{kp:number, ki:number, kd:number},
   *   bestCost:number, elapsedS:number}>}
   */
  async readAutotuneStatus() {
    const data = await this.readDataByIdentifier(constants.DID_AUTOTUNE_STATUS);
    if (data.byteLength < 66) {
      throw new UDSError(`Autotune status too short (${data.byteLength} bytes)`,
        constants.SID_READ_DATA_BY_ID);
    }
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);

    const state = data[0];
    const abortReason = data[1];

    return {
      state,
      stateName: constants.AUTOTUNE_STATE_NAMES[state] ?? `Unknown(${state})`,
      abortReason,
      abortReasonName: constants.AUTOTUNE_ABORT_NAMES[abortReason] ?? `Unknown(${abortReason})`,
      iteration: view.getUint16(2, true),
      budget: view.getUint16(4, true),
      cand: {
        kp: 0,
        ki: 0,
        kd: 0
      },
      best: {
        kp: view.getFloat32(6, true),
        ki: view.getFloat32(10, true),
        kd: view.getFloat32(14, true)
      },
      bestCost: view.getFloat32(18, true),
      elapsedS: view.getUint32(22, true),
      model: {
        gain: view.getFloat32(26, true),
        deadTimeS: view.getFloat32(30, true),
        timeConstantS: view.getFloat32(34, true),
        fitRmseBar: view.getFloat32(38, true),
        mixingExcursionBar: view.getFloat32(42, true),
        baselineDuty: view.getFloat32(46, true),
        baselineSlopeBarS: view.getFloat32(50, true),
        ambientPressureBar: view.getFloat32(54, true),
        deliveredDoseDutyS: view.getFloat32(58, true),
        baselineNoiseBar: view.getFloat32(62, true)
      }
    };
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
      case 'tank_pressure': {
        // Firmware publishes the transducer's native decibar value. Preserve
        // the 0.1 bar precision while mapping its wire failure sentinel to NaN
        // so unavailable/bad readings are not plotted as 6553.5 bar.
        const decibar = view.getUint16(0, true);
        return decibar === 0xFFFF ? Number.NaN : decibar / 10;
      }
      case 'uint8':
        return data[0];
      case 'bool':
        return data[0] !== 0;
      case 'device_current': {
        // 0xF237 packed struct: decode with the shared helper, then surface the
        // numeric draw in mA so it flows through the plot/time-series path. An
        // unavailable reading (no provider / no sample yet) becomes NaN, which
        // the store skips for plotting and the power page renders as "--".
        const decoded = constants.decodeDeviceCurrent(data);
        return decoded?.valid ? decoded.currentMa : Number.NaN;
      }
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
  /**
   * Collect every DID to fetch for a full state read: control state plus
   * every cell DID valid for that cell's configured type.
   * @private
   */
  _collectAllStateDIDs(cellTypes) {
    const allDIDs = [];

    const controlDIDs = constants.getControlStateDIDs();
    for (const info of Object.values(controlDIDs)) {
      allDIDs.push(info.did);
    }

    for (let cellNum = 0; cellNum < 3; cellNum++) {
      const validDIDs = constants.getValidCellDIDs(cellNum, cellTypes[cellNum]);
      for (const info of Object.values(validDIDs)) {
        allDIDs.push(info.did);
      }
    }

    return allDIDs;
  }

  /**
   * Read one chunk of DIDs into `result`. If the chunk read fails (a DID in
   * it is unsupported on this variant, or the head is slow/desynced), retry
   * each DID individually so a single failure doesn't blank the whole fetch.
   * @private
   */
  async _fetchChunkWithFallback(chunk, result) {
    try {
      Object.assign(result, await this.readDIDsParsed(chunk));
    } catch (error) {
      for (const did of chunk) {
        try {
          Object.assign(result, await this.readDIDsParsed([did]));
        } catch (didError) {
          // Skip this DID for this cycle.
        }
      }
    }
  }

  async fetchAllState(cellTypes, progressCallback = null) {
    const allDIDs = this._collectAllStateDIDs(cellTypes);

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
      await this._fetchChunkWithFallback(chunk, result);
    }

    // Add cell types to result
    result._cellTypes = cellTypes;

    return result;
  }

}

// Export constants
export * from './constants.js';
