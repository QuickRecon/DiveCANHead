/**
 * ErrorHistogram decoder unit tests
 */
import { describe, it, expect } from 'vitest';
import {
  OP_ERRORS,
  OP_ERROR_COUNT,
  decodeErrorHistogram,
  summarizeErrorHistogram
} from './ErrorHistogram.js';

/** Build a little-endian uint16[] payload from an array of counts. */
const buildHistogram = (counts) => {
  const buf = new Uint8Array(counts.length * 2);
  const view = new DataView(buf.buffer);
  counts.forEach((c, i) => view.setUint16(i * 2, c, true));
  return buf;
};

describe('ErrorHistogram', () => {
  it('OP_ERRORS matches the firmware enum size (OP_ERR_MAX = 38)', () => {
    expect(OP_ERROR_COUNT).toBe(38);
    expect(OP_ERRORS[0].name).toBe('NONE');
    // A few anchor points against firmware include/errors.h ordering
    expect(OP_ERRORS[1].name).toBe('I2C_BUS');
    expect(OP_ERRORS[9].name).toBe('CELL_FAILURE');
    expect(OP_ERRORS[33].name).toBe('POST_FAIL');
    expect(OP_ERRORS[37].name).toBe('SOLENOID_UNDERCURRENT');
  });

  it('decodes a full 38-slot little-endian payload', () => {
    const counts = new Array(38).fill(0);
    counts[9] = 3;      // CELL_FAILURE
    counts[17] = 258;   // ISOTP_TIMEOUT = 0x0102 -> exercises LE byte order
    const entries = decodeErrorHistogram(buildHistogram(counts));

    expect(entries.length).toBe(38);
    expect(entries[9]).toMatchObject({ index: 9, name: 'CELL_FAILURE', count: 3 });
    expect(entries[17]).toMatchObject({ index: 17, name: 'ISOTP_TIMEOUT', count: 258 });
    expect(entries[0]).toMatchObject({ index: 0, name: 'NONE', count: 0 });
  });

  it('tolerates a short payload by decoding only complete slots', () => {
    // Only 4 bytes -> 2 slots
    const entries = decodeErrorHistogram(new Uint8Array([1, 0, 5, 0]));
    expect(entries.length).toBe(2);
    expect(entries[0].count).toBe(1);
    expect(entries[1].count).toBe(5);
  });

  it('returns [] for empty or missing data', () => {
    expect(decodeErrorHistogram(new Uint8Array([]))).toEqual([]);
    expect(decodeErrorHistogram(null)).toEqual([]);
    expect(decodeErrorHistogram(undefined)).toEqual([]);
  });

  it('summarize counts distinct tripped codes and total events, ignoring NONE', () => {
    const counts = new Array(38).fill(0);
    counts[0] = 999;  // NONE — must be ignored even if non-zero
    counts[9] = 3;
    counts[17] = 2;
    const summary = summarizeErrorHistogram(decodeErrorHistogram(buildHistogram(counts)));
    expect(summary).toEqual({ trippedCodes: 2, totalEvents: 5 });
  });

  it('summarize reports zero for a clean histogram', () => {
    const summary = summarizeErrorHistogram(decodeErrorHistogram(buildHistogram(new Array(38).fill(0))));
    expect(summary).toEqual({ trippedCodes: 0, totalEvents: 0 });
  });
});
