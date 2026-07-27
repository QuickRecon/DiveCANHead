import { describe, it, expect } from 'vitest';
import {
  parseLogStream, parseDclgHeader, decodeBootMarker, decodeDiveMarker,
  decodeCanFrame, decodeLogText, decodeRecord, makeRecordCounter
} from './LogParser.js';
import {
  buildStream, buildRecord, buildDclgHeader,
  bootMarkerPayload, diveMarkerPayload, canFramePayload, logTextPayload,
  FL_TYPE_BOOT_MARKER, FL_TYPE_DIVE_START, FL_TYPE_LOG_TEXT, FL_TYPE_CAN_RX,
  FL_TYPE_BATCH, FL_TYPE_END_OF_STREAM
} from '../../tests/fixtures/log-streams.js';

describe('LogParser', () => {
  it('parses the DCLG header', () => {
    const bytes = new Uint8Array(buildDclgHeader({ stream: 1, totalBytes: 128, entryCount: 4 }));
    const hdr = parseDclgHeader(bytes);
    expect(hdr.magic).toBe(0x47434C44);
    expect(hdr.version).toBe(1);
    expect(hdr.stream).toBe(1);
    expect(hdr.totalBytes).toBe(128);
    expect(hdr.entryCount).toBe(4);
  });

  it('returns null when the DCLG magic is absent', () => {
    expect(parseDclgHeader(new Uint8Array(16))).toBeNull();
  });

  it('walks records and skips the download header', () => {
    const stream = buildStream([
      buildRecord(FL_TYPE_BOOT_MARKER, bootMarkerPayload(42, 'v1.0.0', 3), { tsUs: 1000 }),
      buildRecord(FL_TYPE_DIVE_START, diveMarkerPayload(7, 1700000000), { tsUs: 2000 })
    ]);
    const records = parseLogStream(stream);
    expect(records).toHaveLength(2);
    expect(records[0].type).toBe(FL_TYPE_BOOT_MARKER);
    expect(records[0].tsUs).toBe(1000n);
    expect(decodeBootMarker(records[0].payload)).toEqual({
      bootId: 42, fwVersion: 'v1.0.0', resetCause: 3
    });
    expect(decodeDiveMarker(records[1].payload)).toEqual({
      diveNumber: 7, unixTimestamp: 1700000000
    });
  });

  it('flattens BATCH containers', () => {
    const inner = [
      ...buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(2, 5, 'hi')),
      ...buildRecord(FL_TYPE_CAN_RX, canFramePayload(0x123, 3, [1, 2, 3]))
    ];
    const stream = buildStream([buildRecord(FL_TYPE_BATCH, inner)]);
    const records = parseLogStream(stream);
    expect(records.map(r => r.type)).toEqual([FL_TYPE_LOG_TEXT, FL_TYPE_CAN_RX]);
    expect(decodeLogText(records[0].payload)).toEqual({ level: 2, moduleId: 5, text: 'hi' });
    const can = decodeCanFrame(records[1].payload);
    expect(can.id).toBe(0x123);
    expect(can.dlc).toBe(3);
    expect(Array.from(can.data.slice(0, 3))).toEqual([1, 2, 3]);
  });

  it('stops at END_OF_STREAM', () => {
    const stream = buildStream([
      buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, 'a')),
      buildRecord(FL_TYPE_END_OF_STREAM, []),
      buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, 'unreachable'))
    ]);
    const records = parseLogStream(stream);
    expect(records).toHaveLength(1);
  });

  it('breaks on a truncated tail without throwing', () => {
    const good = buildStream([buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, 'ok'))]);
    // Append a header claiming a longer payload than exists
    const truncated = new Uint8Array([...good, FL_TYPE_LOG_TEXT, 0, 0xFF, 0x00, 0, 0, 0, 0, 0, 0, 0, 0]);
    const records = parseLogStream(truncated);
    expect(records).toHaveLength(1);
  });

  it('decodeRecord dispatches by type', () => {
    const rec = { type: FL_TYPE_LOG_TEXT, payload: new Uint8Array(logTextPayload(3, 9, 'x')) };
    expect(decodeRecord(rec)).toEqual({ level: 3, moduleId: 9, text: 'x' });
  });

  describe('makeRecordCounter (resumable progress)', () => {
    it('counts records incrementally as bytes arrive, resuming its cursor', () => {
      const stream = buildStream([
        buildRecord(FL_TYPE_BOOT_MARKER, bootMarkerPayload(1, 'v', 0)),
        buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, 'aaaa')),
        buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, 'bbbb'))
      ]);
      const counter = makeRecordCounter();
      // Feed the stream one byte at a time; the count only advances past complete
      // entries and never exceeds the true total.
      const total = parseLogStream(stream).length;
      let last = 0;
      for (let i = 1; i <= stream.length; i++) {
        const c = counter(stream.subarray(0, i));
        expect(c).toBeGreaterThanOrEqual(last); // monotonic
        expect(c).toBeLessThanOrEqual(total);
        last = c;
      }
      expect(last).toBe(total);
      expect(total).toBe(3);
    });

    it('flattens BATCH sub-records in the running count', () => {
      const inner = [
        ...buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, 'a')),
        ...buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, 'b'))
      ];
      const stream = buildStream([buildRecord(FL_TYPE_BATCH, inner)]);
      const counter = makeRecordCounter();
      expect(counter(stream)).toBe(2);
    });
  });
});
