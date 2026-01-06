/**
 * Protocol-specific error classes
 */

/**
 * Base protocol error
 */
export class ProtocolError extends Error {
  /**
   * Create a protocol error
   * @param {string} message - Error message
   * @param {string} layer - Protocol layer (BLE, SLIP, ISOTP, UDS, etc.)
   * @param {Object} details - Additional error details
   */
  constructor(message, layer, details = {}) {
    super(message);
    this.name = 'ProtocolError';
    this.layer = layer;
    this.details = details;
    this.timestamp = new Date();

    if (details.cause) {
      this.cause = details.cause;
    }
  }

  /**
   * Get full error chain as string
   * @returns {string} Error chain
   */
  getFullError() {
    let msg = `[${this.layer}] ${this.message}`;
    if (this.cause) {
      msg += `\n  Caused by: ${this.cause.message || this.cause}`;
      if (this.cause.getFullError) {
        msg += `\n  ${this.cause.getFullError()}`;
      }
    }
    return msg;
  }
}

/**
 * BLE connection errors
 */
export class BLEError extends ProtocolError {
  constructor(message, details = {}) {
    super(message, 'BLE', details);
    this.name = 'BLEError';
  }
}

/**
 * SLIP encoding/decoding errors
 */
export class SLIPError extends ProtocolError {
  constructor(message, details = {}) {
    super(message, 'SLIP', details);
    this.name = 'SLIPError';
  }
}

/**
 * DiveCAN datagram errors
 */
export class DiveCANError extends ProtocolError {
  constructor(message, details = {}) {
    super(message, 'DiveCAN', details);
    this.name = 'DiveCANError';
  }
}

/**
 * ISO-TP protocol errors
 */
export class ISOTPError extends ProtocolError {
  /**
   * Create an ISO-TP error
   * @param {string} message - Error message
   * @param {Object} details - Additional details (state, sequenceNumber, etc.)
   */
  constructor(message, details = {}) {
    super(message, 'ISO-TP', details);
    this.name = 'ISOTPError';
  }
}

/**
 * UDS service errors
 */
export class UDSError extends ProtocolError {
  /**
   * Create a UDS error
   * @param {string} message - Error message
   * @param {number} sid - Service ID
   * @param {number} nrc - Negative Response Code (optional)
   * @param {Object} details - Additional details
   */
  constructor(message, sid, nrc = null, details = {}) {
    super(message, 'UDS', { ...details, sid, nrc });
    this.name = 'UDSError';
    this.sid = sid;
    this.nrc = nrc;
  }

  /**
   * Get human-readable NRC description
   * @returns {string} NRC description
   */
  getNRCDescription() {
    if (!this.nrc) return 'No NRC';

    const nrcMap = {
      0x11: 'Service Not Supported',
      0x12: 'Subfunction Not Supported',
      0x13: 'Incorrect Message Length or Invalid Format',
      0x22: 'Conditions Not Correct',
      0x24: 'Request Sequence Error',
      0x31: 'Request Out of Range',
      0x33: 'Security Access Denied',
      0x72: 'General Programming Failure',
      0x73: 'Wrong Block Sequence Counter',
      0x78: 'Request Correctly Received - Response Pending'
    };

    return nrcMap[this.nrc] || `Unknown NRC: 0x${this.nrc.toString(16).padStart(2, '0')}`;
  }
}

/**
 * Timeout errors
 */
export class TimeoutError extends ProtocolError {
  /**
   * Create a timeout error
   * @param {string} message - Error message
   * @param {string} layer - Protocol layer
   * @param {Object} details - Additional details (operation, duration, etc.)
   */
  constructor(message, layer, details = {}) {
    super(message, layer, details);
    this.name = 'TimeoutError';
  }
}

/**
 * Validation errors
 */
export class ValidationError extends ProtocolError {
  /**
   * Create a validation error
   * @param {string} message - Error message
   * @param {string} layer - Protocol layer
   * @param {Object} details - Additional details (field, value, etc.)
   */
  constructor(message, layer, details = {}) {
    super(message, layer, details);
    this.name = 'ValidationError';
  }
}
