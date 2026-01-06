/**
 * Promise-based timeout management
 */
export class Timeout {
  /**
   * Create a timeout
   * @param {number} ms - Timeout in milliseconds
   * @param {string} reason - Reason for timeout (for error message)
   */
  constructor(ms, reason = 'Operation timed out') {
    this.ms = ms;
    this.reason = reason;
    this.timer = null;
    this._promise = null;
    this._reject = null;
  }

  /**
   * Start the timeout
   * @returns {Timeout} this for chaining
   */
  start() {
    if (this.timer) {
      this.cancel();
    }

    this._promise = new Promise((_, reject) => {
      this._reject = reject;
      this.timer = setTimeout(() => {
        const error = new Error(this.reason);
        error.timeout = true;
        reject(error);
      }, this.ms);
    });

    return this;
  }

  /**
   * Cancel the timeout
   */
  cancel() {
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = null;
      this._reject = null;
      this._promise = null;
    }
  }

  /**
   * Get the timeout promise
   * @returns {Promise} Promise that rejects on timeout
   */
  get promise() {
    if (!this._promise) {
      throw new Error('Timeout not started');
    }
    return this._promise;
  }

  /**
   * Check if timeout is active
   * @returns {boolean} True if active
   */
  get isActive() {
    return this.timer !== null;
  }
}

/**
 * Timeout manager for multiple named timeouts
 */
export class TimeoutManager {
  constructor() {
    this.timers = new Map();
  }

  /**
   * Set a timeout
   * @param {string} name - Timeout name
   * @param {number} ms - Timeout in milliseconds
   * @param {Function} callback - Callback to call on timeout
   */
  set(name, ms, callback) {
    this.clear(name);
    const timer = setTimeout(() => {
      this.timers.delete(name);
      callback();
    }, ms);
    this.timers.set(name, timer);
  }

  /**
   * Clear a specific timeout
   * @param {string} name - Timeout name
   */
  clear(name) {
    if (this.timers.has(name)) {
      clearTimeout(this.timers.get(name));
      this.timers.delete(name);
    }
  }

  /**
   * Clear all timeouts
   */
  clearAll() {
    this.timers.forEach(timer => clearTimeout(timer));
    this.timers.clear();
  }

  /**
   * Check if a timeout is active
   * @param {string} name - Timeout name
   * @returns {boolean} True if active
   */
  has(name) {
    return this.timers.has(name);
  }
}
