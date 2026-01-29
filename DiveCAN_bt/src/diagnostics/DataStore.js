/**
 * DataStore - Time-series data storage for real-time diagnostics
 *
 * Primary data source is the binary state vector received once per second.
 * Stores time series for plotting and provides access to latest values.
 */

import { parseStateVector, getCellTypeName } from './StateVectorParser.js';
import {
  CELL_TYPE_NONE,
  CELL_TYPE_ANALOG,
  CELL_TYPE_O2S,
  CELL_TYPE_DIVEO2
} from '../uds/constants.js';

export class DataStore {
  /**
   * Create a new DataStore
   * @param {number} maxPoints - Maximum data points per series
   * @param {number} maxAge - Maximum age in seconds
   */
  constructor(maxPoints = 500, maxAge = 300) {
    this.maxPoints = maxPoints;
    this.maxAge = maxAge;
    this.series = new Map();
    this.latestStateVector = null;
  }

  /**
   * Add a binary state vector to the store
   * @param {Uint8Array} data - Raw binary state vector (122 bytes)
   * @returns {Object|null} Parsed state vector or null if invalid
   */
  addStateVector(data) {
    const sv = parseStateVector(data);
    if (!sv) {
      return null;
    }

    this.latestStateVector = sv;
    const ts = sv.timestamp;

    // Store global fields as time series
    this._addPoint('consensus_ppo2', ts, sv.consensus_ppo2);
    this._addPoint('setpoint', ts, sv.setpoint);
    this._addPoint('duty_cycle', ts, sv.duty_cycle);
    this._addPoint('integral_state', ts, sv.integral_state);
    this._addPoint('saturation_count', ts, sv.saturation_count);

    // Store per-cell fields
    for (const cell of sv.cells) {
      const prefix = `cell${cell.cellNumber}`;

      // PPO2 is always available
      this._addPoint(`${prefix}.ppo2`, ts, cell.ppo2);

      // Type-specific fields
      switch (cell.cellType) {
        case CELL_TYPE_DIVEO2:
          this._addPoint(`${prefix}.temperature`, ts, cell.temperature);
          this._addPoint(`${prefix}.error`, ts, cell.error);
          this._addPoint(`${prefix}.phase`, ts, cell.phase);
          this._addPoint(`${prefix}.intensity`, ts, cell.intensity);
          this._addPoint(`${prefix}.ambientLight`, ts, cell.ambientLight);
          this._addPoint(`${prefix}.pressure`, ts, cell.pressure);
          this._addPoint(`${prefix}.humidity`, ts, cell.humidity);
          break;

        case CELL_TYPE_ANALOG:
          this._addPoint(`${prefix}.rawAdc`, ts, cell.rawAdc);
          break;

        case CELL_TYPE_O2S:
          // O2S only has PPO2, already stored above
          break;
      }
    }

    return sv;
  }

  /**
   * Add a data point to a series
   * @private
   */
  _addPoint(key, timestamp, value) {
    if (value === undefined || value === null || isNaN(value)) return;

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
   * Get latest state vector
   * @returns {Object|null} Parsed state vector or null
   */
  getLatestStateVector() {
    return this.latestStateVector;
  }

  /**
   * Get cell data from latest state vector
   * @param {number} cellNumber - Cell number (0, 1, or 2)
   * @returns {Object|null} Cell data or null
   */
  getCell(cellNumber) {
    if (!this.latestStateVector) return null;
    return this.latestStateVector.cells[cellNumber] || null;
  }

  /**
   * Get cell type from latest state vector
   * @param {number} cellNumber - Cell number (0, 1, or 2)
   * @returns {number} Cell type constant (CELL_TYPE_*)
   */
  getCellType(cellNumber) {
    if (!this.latestStateVector) return CELL_TYPE_NONE;
    return this.latestStateVector.cellTypes[cellNumber] || CELL_TYPE_NONE;
  }

  /**
   * Get human-readable cell type name
   * @param {number} cellNumber - Cell number (0, 1, or 2)
   * @returns {string} Cell type name
   */
  getCellTypeName(cellNumber) {
    return getCellTypeName(this.getCellType(cellNumber));
  }

  /**
   * Check if a cell is included in voting
   * @param {number} cellNumber - Cell number (0, 1, or 2)
   * @returns {boolean}
   */
  isCellIncluded(cellNumber) {
    if (!this.latestStateVector) return false;
    return (this.latestStateVector.cellsValid & (1 << cellNumber)) !== 0;
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
    this.latestStateVector = null;
  }
}
