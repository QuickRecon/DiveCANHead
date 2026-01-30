/**
 * Configurable logging framework
 */
export class Logger {
  static LEVELS = {
    DEBUG: 0,
    INFO: 1,
    WARN: 2,
    ERROR: 3,
    NONE: 4
  };

  // Global minimum level - all loggers respect this
  static globalLevel = Logger.LEVELS.DEBUG;

  /**
   * Set global log level for all loggers
   * @param {string} level - Log level (debug, info, warn, error, none)
   */
  static setGlobalLevel(level) {
    const levelUpper = level.toUpperCase();
    Logger.globalLevel = Logger.LEVELS[levelUpper] ?? Logger.LEVELS.INFO;
  }

  /**
   * Create a logger
   * @param {string} name - Logger name
   * @param {string} level - Log level (debug, info, warn, error, none)
   */
  constructor(name, level = 'debug') {
    this.name = name;
    this.setLevel(level);
  }

  /**
   * Set log level
   * @param {string} level - Log level
   */
  setLevel(level) {
    const levelUpper = level.toUpperCase();
    this.level = Logger.LEVELS[levelUpper] ?? Logger.LEVELS.INFO;
  }

  /**
   * Format log message
   * @private
   */
  _format(level, msg, data) {
    const timestamp = new Date().toISOString();
    const prefix = `[${timestamp}] [${level}] [${this.name}]`;

    if (data !== undefined) {
      return `${prefix} ${msg}`;
    }
    return `${prefix} ${msg}`;
  }

  /**
   * Get effective log level (respects global minimum)
   * @private
   */
  _effectiveLevel() {
    return Math.max(this.level, Logger.globalLevel);
  }

  /**
   * Log debug message
   * @param {string} msg - Message
   * @param {*} data - Optional data to log
   */
  debug(msg, data) {
    if (this._effectiveLevel() <= Logger.LEVELS.DEBUG) {
      console.debug(this._format('DEBUG', msg, data), data);
    }
  }

  /**
   * Log info message
   * @param {string} msg - Message
   * @param {*} data - Optional data to log
   */
  info(msg, data) {
    if (this._effectiveLevel() <= Logger.LEVELS.INFO) {
      if (data === undefined) {
        console.info(this._format('INFO', msg, data));
      } else {
        console.info(this._format('INFO', msg, data), data);
      }
    }
  }

  /**
   * Log warning message
   * @param {string} msg - Message
   * @param {*} data - Optional data to log
   */
  warn(msg, data) {
    if (this._effectiveLevel() <= Logger.LEVELS.WARN) {
      if (data === undefined) {
        console.warn(this._format('WARN', msg, data));
      } else {
        console.warn(this._format('WARN', msg, data), data);
      }
    }
  }

  /**
   * Log error message
   * @param {string} msg - Message
   * @param {*} data - Optional data to log
   */
  error(msg, data) {
    if (this._effectiveLevel() <= Logger.LEVELS.ERROR) {
      if (data === undefined) {
        console.error(this._format('ERROR', msg, data));
      } else {
        console.error(this._format('ERROR', msg, data), data);
      }
    }
  }
}
