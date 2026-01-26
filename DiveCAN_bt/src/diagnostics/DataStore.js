/**
 * DataStore - Time-series data storage for real-time plotting
 *
 * Stores event data with rolling window and provides access for plotting.
 */

export class DataStore {
  /**
   * Create a new DataStore
   * @param {number} maxPoints - Maximum data points per series
   * @param {number} maxAge - Maximum age in seconds
   */
  constructor(maxPoints = 500, maxAge = 60) {
    this.maxPoints = maxPoints;
    this.maxAge = maxAge;
    this.series = new Map();
    this.cellTypes = new Map();
    this.latestEvents = new Map();
  }

  /**
   * Generate series key for a field
   * @param {string} eventType - Event type
   * @param {string} field - Field name
   * @param {number} cellNumber - Optional cell number
   * @returns {string} Series key
   */
  static key(eventType, field, cellNumber = null) {
    if (cellNumber !== null) {
      return `${eventType}.${cellNumber}.${field}`;
    }
    return `${eventType}.${field}`;
  }

  /**
   * Add a parsed event to the store
   * @param {Object} event - Parsed event from EventParser
   */
  addEvent(event) {
    if (!event || !event.type) return;

    // Track cell types for UI adaptation
    if (['DIVEO2', 'O2S', 'ANALOGCELL'].includes(event.type)) {
      this.cellTypes.set(event.cellNumber, event.type);
    }

    // Store latest event for quick access
    if (event.cellNumber !== undefined) {
      this.latestEvents.set(`${event.type}.${event.cellNumber}`, event);
    } else {
      this.latestEvents.set(event.type, event);
    }

    // Store each numeric field as a time series
    const fields = this._getPlottableFields(event);
    for (const [field, value] of Object.entries(fields)) {
      const key = DataStore.key(event.type, field, event.cellNumber);
      this._addPoint(key, event.timestamp, value);
    }
  }

  /**
   * Get plottable fields from an event
   * @private
   */
  _getPlottableFields(event) {
    switch (event.type) {
      case 'DIVEO2':
        return {
          ppo2: event.ppo2,
          temperature: event.temperature,
          error: event.error,
          phase: event.phase,
          intensity: event.intensity,
          ambientLight: event.ambientLight,
          pressure: event.pressure,
          humidity: event.humidity
        };
      case 'O2S':
        return { ppo2: event.ppo2 };
      case 'ANALOGCELL':
        return { sample: event.sample };
      case 'PID':
        return {
          integralState: event.integralState,
          saturationCount: event.saturationCount,
          dutyCycle: event.dutyCycle,
          setpoint: event.setpoint
        };
      case 'PPO2STATE':
        return {
          c1_value: event.c1_value,
          c2_value: event.c2_value,
          c3_value: event.c3_value,
          consensus: event.consensus
        };
      default:
        return {};
    }
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
   * Get latest event of a specific type (optionally for a cell)
   * @param {string} eventType - Event type
   * @param {number} cellNumber - Optional cell number
   * @returns {Object|null}
   */
  getLatestEvent(eventType, cellNumber = null) {
    if (cellNumber !== null) {
      return this.latestEvents.get(`${eventType}.${cellNumber}`) || null;
    }
    return this.latestEvents.get(eventType) || null;
  }

  /**
   * Get all available series keys
   * @returns {string[]}
   */
  getSeriesKeys() {
    return Array.from(this.series.keys());
  }

  /**
   * Get detected cell type for a cell number
   * @param {number} cellNumber - Cell number (0, 1, or 2)
   * @returns {string|null}
   */
  getCellType(cellNumber) {
    return this.cellTypes.get(cellNumber) || null;
  }

  /**
   * Get all detected cell types
   * @returns {Map<number, string>}
   */
  getAllCellTypes() {
    return new Map(this.cellTypes);
  }

  /**
   * Clear all stored data
   */
  clear() {
    this.series.clear();
    this.cellTypes.clear();
    this.latestEvents.clear();
  }
}
