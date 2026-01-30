/**
 * DataStore - Time-series data storage for real-time diagnostics
 *
 * Uses DID-based polling with subscription model:
 * - Initial fetch of all DIDs on connect
 * - Polling of subscribed DIDs only
 * - Change events when values update
 */

import {
  CELL_TYPE_NONE,
  STATE_DIDS
} from '../uds/constants.js';

export class DataStore {
  /**
   * Create a new DataStore
   * @param {Object} options - Configuration options
   * @param {number} options.maxPoints - Maximum data points per series (default: 500)
   * @param {number} options.maxAge - Maximum age in seconds (default: 300)
   * @param {Object} options.udsClient - UDSClient instance for DID-based polling
   * @param {number} options.pollInterval - Poll interval in ms (default: 200)
   */
  constructor(options = {}) {
    this.maxPoints = options.maxPoints ?? 500;
    this.maxAge = options.maxAge ?? 300;
    this.udsClient = options.udsClient ?? null;
    this.pollInterval = options.pollInterval ?? 200;

    this.series = new Map();

    // DID-based state
    this.didValues = new Map();          // DID key -> latest value
    this.subscriptions = new Map();      // DID key -> Set of callbacks
    this.pollTimer = null;
    this.cellTypes = [0, 0, 0];          // Cached cell types
    this.isPolling = false;
  }

  /**
   * Add a data point to a series
   * @private
   */
  _addPoint(key, timestamp, value) {
    if (value === undefined || value === null || Number.isNaN(value)) return;

    if (!this.series.has(key)) {
      this.series.set(key, []);
    }
    const arr = this.series.get(key);
    arr.push({ timestamp, value });

    // Prune old points by age
    const cutoff = timestamp - this.maxAge;
    while (arr.length > 0 && arr[0].timestamp < cutoff) {
      arr.shift();
    }

    // Prune by count
    while (arr.length > this.maxPoints) {
      arr.shift();
    }
  }

  /**
   * Get time series data for plotting
   * @param {string} key - Series key
   * @returns {Array<{timestamp: number, value: number}>}
   */
  getSeries(key) {
    return this.series.get(key) || [];
  }

  /**
   * Get latest value for a series
   * @param {string} key - Series key
   * @returns {{timestamp: number, value: number}|null}
   */
  getLatest(key) {
    const arr = this.series.get(key);
    return arr && arr.length > 0 ? arr[arr.length - 1] : null;
  }

  /**
   * Get cell type from cached cell types
   * @param {number} cellNumber - Cell number (0, 1, or 2)
   * @returns {number} Cell type constant (CELL_TYPE_*)
   */
  getCellType(cellNumber) {
    return this.cellTypes[cellNumber] || CELL_TYPE_NONE;
  }

  /**
   * Get human-readable cell type name
   * @param {number} cellNumber - Cell number (0, 1, or 2)
   * @returns {string} Cell type name
   */
  getCellTypeName(cellNumber) {
    const CELL_TYPE_NAMES = ['DiveO2', 'Analog', 'O2S'];
    const type = this.getCellType(cellNumber);
    return CELL_TYPE_NAMES[type] || 'Unknown';
  }

  /**
   * Check if a cell is included in voting
   * @param {number} cellNumber - Cell number (0, 1, or 2)
   * @returns {boolean}
   */
  isCellIncluded(cellNumber) {
    const cellsValid = this.getDIDValue('CELLS_VALID');
    if (cellsValid === undefined) return false;
    return (cellsValid & (1 << cellNumber)) !== 0;
  }

  /**
   * Get all available series keys
   * @returns {string[]}
   */
  getSeriesKeys() {
    return Array.from(this.series.keys());
  }

  /**
   * Clear all stored data
   */
  clear() {
    this.series.clear();
    this.didValues.clear();
  }

  // ============================================================
  // DID-Based Polling Methods
  // ============================================================

  /**
   * Initialize pull mode - fetch all DIDs and start polling
   * @param {Function} progressCallback - Optional callback (current, total) => void
   * @returns {Promise<Object>} Initial state values
   */
  async initialize(progressCallback = null) {
    if (!this.udsClient) {
      throw new Error('UDSClient required for pull mode');
    }

    // Fetch all DIDs once
    const state = await this.fetchAllDIDs(progressCallback);

    // Start polling subscribed DIDs
    this.startPolling();

    return state;
  }

  /**
   * Fetch all known DIDs from device
   * @param {Function} progressCallback - Optional callback (current, total) => void
   * @returns {Promise<Object>} Object with all DID values
   */
  async fetchAllDIDs(progressCallback = null) {
    if (!this.udsClient) {
      throw new Error('UDSClient required for fetchAllDIDs');
    }

    const state = await this.udsClient.fetchAllState(progressCallback);

    // Update cached cell types
    if (state._cellTypes) {
      this.cellTypes = state._cellTypes;
    }

    // Update internal state and notify subscribers
    const timestamp = Date.now() / 1000;
    for (const [key, value] of Object.entries(state)) {
      if (key.startsWith('_')) continue;  // Skip metadata

      const oldValue = this.didValues.get(key);
      this.didValues.set(key, value);

      // Add to time series using normalized key for charting compatibility
      const seriesKey = this._didKeyToSeriesKey(key);
      this._addPoint(seriesKey, timestamp, value);

      // Notify subscribers if value changed
      if (oldValue !== value) {
        this._notifySubscribers(key, value, oldValue);
      }
    }

    return state;
  }

  /**
   * Subscribe to a DID value
   * @param {string} didKey - DID key (e.g., 'CONSENSUS_PPO2')
   * @param {Function} callback - Called when value changes: (newValue, oldValue, key) => void
   * @returns {Function} Unsubscribe function
   */
  subscribe(didKey, callback) {
    if (!this.subscriptions.has(didKey)) {
      this.subscriptions.set(didKey, new Set());
    }
    this.subscriptions.get(didKey).add(callback);

    // Return unsubscribe function
    return () => this.unsubscribe(didKey, callback);
  }

  /**
   * Unsubscribe from a DID value
   * @param {string} didKey - DID key
   * @param {Function} callback - Callback to remove
   */
  unsubscribe(didKey, callback) {
    const subs = this.subscriptions.get(didKey);
    if (subs) {
      subs.delete(callback);
      if (subs.size === 0) {
        this.subscriptions.delete(didKey);
      }
    }
  }

  /**
   * Get current value of a DID
   * @param {string} didKey - DID key
   * @returns {*} Current value or undefined
   */
  getDIDValue(didKey) {
    return this.didValues.get(didKey);
  }

  /**
   * Get all current DID values
   * @returns {Object} Object with all DID values
   */
  getAllDIDValues() {
    const result = {};
    for (const [key, value] of this.didValues) {
      result[key] = value;
    }
    return result;
  }

  /**
   * Start polling subscribed DIDs
   * @param {number} intervalMs - Poll interval (default: use constructor value)
   */
  startPolling(intervalMs = null) {
    if (this.isPolling) return;

    const interval = intervalMs ?? this.pollInterval;
    this.isPolling = true;

    this.pollTimer = setInterval(async () => {
      if (!this.isPolling) return;

      try {
        await this._pollSubscribedDIDs();
      } catch (error) {
        console.error('Poll error:', error);
      }
    }, interval);
  }

  /**
   * Stop polling
   */
  stopPolling() {
    this.isPolling = false;
    if (this.pollTimer) {
      clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
  }

  /**
   * Check if a DID is valid for current cell configuration
   * @private
   */
  _isValidDIDForCell(didKey, didInfo) {
    if (didInfo.cellType === undefined) {
      return true;
    }
    const cellNum = this._extractCellNum(didKey);
    if (cellNum === null) {
      return true;
    }
    return didInfo.cellType === this.cellTypes[cellNum];
  }

  /**
   * Collect DIDs to poll from active subscriptions
   * @private
   */
  _collectSubscribedDIDs() {
    const didsToRead = [];
    for (const didKey of this.subscriptions.keys()) {
      const didInfo = STATE_DIDS[didKey];
      if (didInfo && this._isValidDIDForCell(didKey, didInfo)) {
        didsToRead.push(didInfo.did);
      }
    }
    return didsToRead;
  }

  /**
   * Update DID values from poll result
   * @private
   */
  _updateDIDValues(result, timestamp) {
    for (const [key, value] of Object.entries(result)) {
      const oldValue = this.didValues.get(key);
      this.didValues.set(key, value);

      const seriesKey = this._didKeyToSeriesKey(key);
      this._addPoint(seriesKey, timestamp, value);

      if (oldValue !== value) {
        this._notifySubscribers(key, value, oldValue);
      }
    }
  }

  /**
   * Poll only subscribed DIDs
   * @private
   */
  async _pollSubscribedDIDs() {
    if (!this.udsClient || this.subscriptions.size === 0) {
      return;
    }

    const didsToRead = this._collectSubscribedDIDs();
    if (didsToRead.length === 0) {
      return;
    }

    // Read DIDs (chunked to fit BLE MTU)
    // Request: 1 (SID) + N*2 (DID bytes) + ~5 bytes protocol overhead must fit in 20-byte MTU
    // Max safe: (20 - 5 - 1) / 2 = 7 DIDs, use 4 to be conservative
    const DIDS_PER_REQUEST = 4;
    const timestamp = Date.now() / 1000;

    for (let i = 0; i < didsToRead.length; i += DIDS_PER_REQUEST) {
      const chunk = didsToRead.slice(i, i + DIDS_PER_REQUEST);
      const result = await this.udsClient.readDIDsParsed(chunk);
      this._updateDIDValues(result, timestamp);
    }
  }

  /**
   * Notify subscribers of a value change
   * @private
   */
  _notifySubscribers(key, newValue, oldValue) {
    const subs = this.subscriptions.get(key);
    if (subs) {
      for (const callback of subs) {
        try {
          callback(newValue, oldValue, key);
        } catch (error) {
          console.error(`Subscriber error for ${key}:`, error);
        }
      }
    }
  }

  /**
   * Extract cell number from DID key
   * @private
   */
  _extractCellNum(didKey) {
    const match = didKey.match(/^CELL(\d)_/);
    return match ? Number.parseInt(match[1], 10) : null;
  }

  /**
   * Convert DID key to series key for charting
   * Maps CONSENSUS_PPO2 -> consensus_ppo2, CELL0_PPO2 -> cell0.ppo2, etc.
   * @private
   */
  _didKeyToSeriesKey(didKey) {
    // Cell DIDs: CELL0_PPO2 -> cell0.ppo2
    const cellMatch = didKey.match(/^CELL(\d)_(.+)$/);
    if (cellMatch) {
      const cellNum = cellMatch[1];
      const field = cellMatch[2].toLowerCase();
      // Map field names to match state vector format
      const fieldMap = {
        'ambient_light': 'ambientLight',
        'raw_adc': 'rawAdc'
      };
      const mappedField = fieldMap[field] || field;
      return `cell${cellNum}.${mappedField}`;
    }

    // Control DIDs: CONSENSUS_PPO2 -> consensus_ppo2
    return didKey.toLowerCase();
  }

  /**
   * Convert series key back to DID key
   * Maps cell0.ppo2 -> CELL0_PPO2, consensus_ppo2 -> CONSENSUS_PPO2, etc.
   * @param {string} seriesKey - Series key
   * @returns {string|null} DID key or null if not found
   */
  seriesKeyToDIDKey(seriesKey) {
    // Cell series: cell0.ppo2 -> CELL0_PPO2
    const cellMatch = /^cell(\d)\.(.+)$/.exec(seriesKey);
    if (cellMatch) {
      const cellNum = cellMatch[1];
      const field = cellMatch[2];
      // Reverse map field names
      const fieldMap = {
        'ambientlight': 'AMBIENT_LIGHT',
        'rawadc': 'RAW_ADC'
      };
      const mappedField = fieldMap[field.toLowerCase()] || field.toUpperCase();
      return `CELL${cellNum}_${mappedField}`;
    }

    // Control series: consensus_ppo2 -> CONSENSUS_PPO2
    return seriesKey.toUpperCase();
  }

  /**
   * Get cached cell types
   * @returns {Array<number>} Array of 3 cell types
   */
  getCachedCellTypes() {
    return [...this.cellTypes];
  }

  /**
   * Refresh cell types from device
   * @returns {Promise<Array<number>>}
   */
  async refreshCellTypes() {
    if (!this.udsClient) {
      throw new Error('UDSClient required');
    }
    this.cellTypes = await this.udsClient.getCellTypes();
    return this.cellTypes;
  }
}
