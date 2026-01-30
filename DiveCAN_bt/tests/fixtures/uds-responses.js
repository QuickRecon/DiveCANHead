/**
 * UDS response test vectors
 *
 * Common UDS message patterns for testing
 */

// UDS Service IDs
export const SID_READ_DATA_BY_ID = 0x22;
export const SID_WRITE_DATA_BY_ID = 0x2E;
export const SID_NEGATIVE_RESPONSE = 0x7F;
export const RESPONSE_SID_OFFSET = 0x40;

// Negative Response Codes
export const NRC = {
  SERVICE_NOT_SUPPORTED: 0x11,
  SUBFUNCTION_NOT_SUPPORTED: 0x12,
  INCORRECT_MESSAGE_LENGTH: 0x13,
  CONDITIONS_NOT_CORRECT: 0x22,
  REQUEST_SEQUENCE_ERROR: 0x24,
  REQUEST_OUT_OF_RANGE: 0x31,
  SECURITY_ACCESS_DENIED: 0x33,
  GENERAL_PROGRAMMING_FAILURE: 0x72,
  WRONG_BLOCK_SEQUENCE: 0x73,
  RESPONSE_PENDING: 0x78
};

/**
 * Build a positive response for ReadDataByIdentifier
 * @param {number} did - DID address
 * @param {Array|Uint8Array} data - Response data
 * @returns {Uint8Array}
 */
export function buildRDBIResponse(did, data) {
  const response = new Uint8Array(3 + data.length);
  response[0] = SID_READ_DATA_BY_ID + RESPONSE_SID_OFFSET;  // 0x62
  response[1] = (did >> 8) & 0xFF;
  response[2] = did & 0xFF;
  response.set(data, 3);
  return response;
}

/**
 * Build a negative response
 * @param {number} sid - Original request SID
 * @param {number} nrc - Negative Response Code
 * @returns {Uint8Array}
 */
export function buildNegativeResponse(sid, nrc) {
  return new Uint8Array([SID_NEGATIVE_RESPONSE, sid, nrc]);
}

/**
 * Build a positive response for WriteDataByIdentifier
 * @param {number} did - DID address
 * @returns {Uint8Array}
 */
export function buildWDBIResponse(did) {
  return new Uint8Array([
    SID_WRITE_DATA_BY_ID + RESPONSE_SID_OFFSET,  // 0x6E
    (did >> 8) & 0xFF,
    did & 0xFF
  ]);
}

// Pre-built test responses
export const RESPONSES = {
  // ReadDataByIdentifier positive responses
  RDBI: {
    // DID 0xF200 - Consensus PPO2 (float32 = 1.05)
    CONSENSUS_PPO2: buildRDBIResponse(0xF200, [0x66, 0x66, 0x86, 0x3F]),

    // DID 0xF202 - Setpoint (float32 = 1.30)
    SETPOINT: buildRDBIResponse(0xF202, [0x66, 0x66, 0xA6, 0x3F]),

    // DID 0xF203 - Cells Valid (uint8 = 0x07 = all 3 cells)
    CELLS_VALID: buildRDBIResponse(0xF203, [0x07]),

    // DID 0xF400 - Cell 0 PPO2 (float32 = 1.02)
    CELL0_PPO2: buildRDBIResponse(0xF400, [0xC3, 0xF5, 0x82, 0x3F]),

    // DID 0xF401 - Cell 0 Type (uint8 = 1 = Analog)
    CELL0_TYPE: buildRDBIResponse(0xF401, [0x01]),

    // DID 0xF402 - Cell 0 Included (bool = true)
    CELL0_INCLUDED: buildRDBIResponse(0xF402, [0x01]),

    // DID 0x8010 - Serial Number (string)
    SERIAL_NUMBER: buildRDBIResponse(0x8010, new TextEncoder().encode('SN12345678')),

    // DID 0x8011 - Model (string)
    MODEL: buildRDBIResponse(0x8011, new TextEncoder().encode('DiveCANHead'))
  },

  // Negative responses
  NEGATIVE: {
    SERVICE_NOT_SUPPORTED: buildNegativeResponse(SID_READ_DATA_BY_ID, NRC.SERVICE_NOT_SUPPORTED),
    INCORRECT_LENGTH: buildNegativeResponse(SID_READ_DATA_BY_ID, NRC.INCORRECT_MESSAGE_LENGTH),
    OUT_OF_RANGE: buildNegativeResponse(SID_READ_DATA_BY_ID, NRC.REQUEST_OUT_OF_RANGE),
    CONDITIONS_NOT_CORRECT: buildNegativeResponse(SID_WRITE_DATA_BY_ID, NRC.CONDITIONS_NOT_CORRECT)
  },

  // WriteDataByIdentifier responses
  WDBI: {
    SETPOINT: buildWDBIResponse(0xF240),
    CALIBRATION: buildWDBIResponse(0xF241)
  }
};

// Multi-DID response vectors
export const MULTI_DID_RESPONSES = {
  // Response with multiple DIDs: [0x62, DID1_hi, DID1_lo, data1..., DID2_hi, DID2_lo, data2...]
  CONTROL_STATE: (() => {
    // Build multi-DID response for control state DIDs
    // 0xF200 (4 bytes), 0xF202 (4 bytes), 0xF203 (1 byte)
    const response = new Uint8Array(3 + 4 + 2 + 4 + 2 + 1);
    let offset = 0;

    // Response SID
    response[offset++] = 0x62;

    // DID 0xF200 - Consensus PPO2 (1.05)
    response[offset++] = 0xF2;
    response[offset++] = 0x00;
    response.set([0x66, 0x66, 0x86, 0x3F], offset);
    offset += 4;

    // DID 0xF202 - Setpoint (1.30)
    response[offset++] = 0xF2;
    response[offset++] = 0x02;
    response.set([0x66, 0x66, 0xA6, 0x3F], offset);
    offset += 4;

    // DID 0xF203 - Cells Valid (0x07)
    response[offset++] = 0xF2;
    response[offset++] = 0x03;
    response[offset++] = 0x07;

    return response;
  })()
};
