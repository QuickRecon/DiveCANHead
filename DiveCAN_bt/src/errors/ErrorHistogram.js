/**
 * Error-histogram decoding (DID 0xF260).
 *
 * The head keeps one saturated uint16 occurrence counter per operational error
 * code (`OpError_t` in firmware `include/errors.h`) in noinit RAM, mirrored to
 * NVS. DID 0xF260 returns the whole array as `uint16[OP_ERR_MAX]`, little-endian
 * (index == enum value). DID 0xF261 clears it (write any byte; persists to NVS).
 *
 * OP_ERRORS is kept in exact enum order — the array index IS the error code, so
 * do not reorder or insert; append new codes to match the firmware enum.
 */

/**
 * Per-code metadata, indexed by `OpError_t` value. Index 0 is OP_ERR_NONE (a
 * sentinel that never accumulates). Descriptions are condensed from the enum
 * doc comments in firmware `include/errors.h`.
 * @type {Array<{name: string, category: string, description: string}>}
 */
export const OP_ERRORS = [
  { name: 'NONE',                 category: '—',             description: 'No error (sentinel)' },
  { name: 'I2C_BUS',              category: 'Hardware',      description: 'I2C operation failed' },
  { name: 'UART',                 category: 'Hardware',      description: 'UART operation failed' },
  { name: 'CAN_TX',               category: 'Hardware',      description: 'Could not queue outbound CAN message' },
  { name: 'CAN_OVERFLOW',         category: 'Hardware',      description: 'Inbound CAN message longer than 8 bytes' },
  { name: 'INT_ADC',              category: 'Hardware',      description: 'Internal ADC read error' },
  { name: 'EXT_ADC',              category: 'Hardware',      description: 'External ADC (ADS1115) read error' },
  { name: 'FLASH',                category: 'Hardware',      description: 'Flash storage read/write failed' },
  { name: 'CELL_OVERRANGE',       category: 'Sensors',       description: 'Cell reported an undisplayable value' },
  { name: 'CELL_FAILURE',         category: 'Sensors',       description: 'Cell reported an error' },
  { name: 'INVALID_CELL',         category: 'Sensors',       description: 'Cell number cannot be mapped to an input' },
  { name: 'MATH',                 category: 'Math/Safety',   description: 'Computation out-of-range or overflow' },
  { name: 'CAL_METHOD',           category: 'Calibration',   description: 'Configured calibration method cannot complete' },
  { name: 'CAL_MISMATCH',         category: 'Calibration',   description: 'Stored calibration does not match readback' },
  { name: 'VBUS_UNDERVOLT',       category: 'Power',         description: 'VBus undervolted — cell readings unreliable' },
  { name: 'VCC_UNDERVOLT',        category: 'Power',         description: 'VCC undervolted — cannot write flash' },
  { name: 'SOLENOID_DISABLED',    category: 'Power',         description: 'Solenoid fire attempted while inhibited' },
  { name: 'ISOTP_TIMEOUT',        category: 'ISO-TP',        description: 'Timeout waiting for flow control / consecutive frame' },
  { name: 'ISOTP_SEQ',            category: 'ISO-TP',        description: 'Consecutive-frame sequence number error' },
  { name: 'ISOTP_OVERFLOW',       category: 'ISO-TP',        description: 'Message exceeds maximum payload' },
  { name: 'ISOTP_STATE',          category: 'ISO-TP',        description: 'Invalid state transition' },
  { name: 'UDS_NRC',              category: 'UDS',           description: 'Sent a negative response (NRC logged)' },
  { name: 'UDS_TOO_FULL',         category: 'UDS',           description: 'Response buffer too full to fit data' },
  { name: 'UDS_INVALID',          category: 'UDS',           description: 'Invalid UDS operation attempted' },
  { name: 'CONFIG',               category: 'Configuration', description: 'Failed to load configuration' },
  { name: 'TIMEOUT',              category: 'System',        description: 'What we were waiting for never came' },
  { name: 'OUT_OF_DATE',          category: 'System',        description: 'Data being used is out of date' },
  { name: 'QUEUE',                category: 'System',        description: 'Could not lodge an element in a queue' },
  { name: 'NULL_PTR',             category: 'System',        description: 'Null pointer passed where not permitted' },
  { name: 'LOGGING',              category: 'System',        description: 'Logging quit due to an error' },
  { name: 'LOG_TRUNCATED',        category: 'System',        description: 'Log push queue full — oldest message dropped' },
  { name: 'UNREACHABLE',          category: 'System',        description: 'Supposedly-unreachable code was hit' },
  { name: 'UNKNOWN',              category: 'System',        description: 'Encountered an unhandled error' },
  { name: 'POST_FAIL',            category: 'POST',          description: 'Self-test refused to confirm image (reverted)' },
  { name: 'GPIO',                 category: 'Hardware',      description: 'Runtime GPIO call returned an error' },
  { name: 'DEVICE_NOT_READY',     category: 'System',        description: 'Required device not ready when accessed' },
  { name: 'SOLENOID_OVERCURRENT', category: 'Solenoid',      description: 'Fire-current above expected window (possible short)' },
  { name: 'SOLENOID_UNDERCURRENT',category: 'Solenoid',      description: 'Fire-current below window (open coil / boost fault)' }
];

/** Number of error codes the firmware tracks (OP_ERR_MAX). */
export const OP_ERROR_COUNT = OP_ERRORS.length;

/**
 * Decode the raw 0xF260 payload into a per-code breakdown.
 *
 * Tolerant of a short payload (decodes as many complete uint16 slots as the
 * data holds) so a truncated read degrades gracefully rather than throwing.
 *
 * @param {Uint8Array} data - Raw DID payload (uint16[] little-endian).
 * @returns {Array<{index:number, name:string, category:string,
 *   description:string, count:number}>} One entry per decoded code, enum order.
 */
export function decodeErrorHistogram(data) {
  if (!data || data.length < 2) {
    return [];
  }
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const slots = Math.min(OP_ERRORS.length, Math.floor(data.length / 2));
  const out = [];
  for (let i = 0; i < slots; i++) {
    const meta = OP_ERRORS[i];
    out.push({
      index: i,
      name: meta ? meta.name : `CODE_${i}`,
      category: meta ? meta.category : 'Unknown',
      description: meta ? meta.description : `Unknown code ${i}`,
      count: view.getUint16(i * 2, true)
    });
  }
  return out;
}

/**
 * Summarise a decoded histogram: how many distinct codes tripped and the total
 * event count. Index 0 (NONE) is ignored.
 *
 * @param {Array<{index:number, count:number}>} entries - decodeErrorHistogram output.
 * @returns {{trippedCodes:number, totalEvents:number}}
 */
export function summarizeErrorHistogram(entries) {
  let trippedCodes = 0;
  let totalEvents = 0;
  for (const e of entries) {
    if (e.index !== 0 && e.count > 0) {
      trippedCodes += 1;
      totalEvents += e.count;
    }
  }
  return { trippedCodes, totalEvents };
}
