import { describe, it, expect } from 'vitest';
import {
  parseLogStream, parseDclgHeader, decodeBootMarker, decodeDiveMarker,
  decodeCanFrame, decodeLogText, decodeConsensus, decodeRecord, makeRecordCounter
} from './LogParser.js';
import {
  buildStream, buildRecord, buildDclgHeader,
  bootMarkerPayload, diveMarkerPayload, canFramePayload, logTextPayload,
  FL_TYPE_BOOT_MARKER, FL_TYPE_DIVE_START, FL_TYPE_LOG_TEXT, FL_TYPE_CAN_RX,
  FL_TYPE_BATCH, FL_TYPE_END_OF_STREAM
} from '../../tests/fixtures/log-streams.js';
import {
  FL_TYPE_DIVE_END, FL_TYPE_CAN_TX, FL_TYPE_CONSENSUS
} from '../uds/constants.js';

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

  describe('decodeRecord dispatch (all branches)', () => {
    const consensus = [
      100, 90, 95, 105,
      0x10, 0x27, 0x20, 0x4E, 0x30, 0x75,
      0x00, 0x00, 80, 130
    ];

    it('dispatches BOOT_MARKER', () => {
      const rec = { type: FL_TYPE_BOOT_MARKER, payload: new Uint8Array(bootMarkerPayload(5, 'v', 2)) };
      expect(decodeRecord(rec)).toEqual({ bootId: 5, fwVersion: 'v', resetCause: 2 });
    });

    it('dispatches DIVE_START and DIVE_END to the dive-marker decoder', () => {
      const start = { type: FL_TYPE_DIVE_START, payload: new Uint8Array(diveMarkerPayload(3, 1700)) };
      const end = { type: FL_TYPE_DIVE_END, payload: new Uint8Array(diveMarkerPayload(3, 1800)) };
      expect(decodeRecord(start)).toEqual({ diveNumber: 3, unixTimestamp: 1700 });
      expect(decodeRecord(end)).toEqual({ diveNumber: 3, unixTimestamp: 1800 });
    });

    it('dispatches CAN_RX and CAN_TX to the CAN-frame decoder', () => {
      const rx = { type: FL_TYPE_CAN_RX, payload: new Uint8Array(canFramePayload(0x321, 2, [9, 8])) };
      const tx = { type: FL_TYPE_CAN_TX, payload: new Uint8Array(canFramePayload(0x321, 2, [9, 8])) };
      expect(decodeRecord(rx).id).toBe(0x321);
      expect(decodeRecord(tx).id).toBe(0x321);
    });

    it('dispatches CONSENSUS', () => {
      const rec = { type: FL_TYPE_CONSENSUS, payload: new Uint8Array(consensus) };
      const decoded = decodeRecord(rec);
      expect(decoded.consensusPpo2).toBe(100);
      expect(decoded.ppo2).toEqual([90, 95, 105]);
      expect(decoded.setpoint).toBe(130);
    });

    it('returns null for an unknown type', () => {
      expect(decodeRecord({ type: 0x99, payload: new Uint8Array(4) })).toBeNull();
    });
  });

  describe('decoder null-guards for short payloads', () => {
    it('decodeBootMarker returns null below 24 bytes', () => {
      expect(decodeBootMarker(new Uint8Array(23))).toBeNull();
    });
    it('decodeDiveMarker returns null below 6 bytes', () => {
      expect(decodeDiveMarker(new Uint8Array(5))).toBeNull();
    });
    it('decodeCanFrame returns null below 13 bytes', () => {
      expect(decodeCanFrame(new Uint8Array(12))).toBeNull();
    });
    it('decodeLogText returns null below 3 bytes', () => {
      expect(decodeLogText(new Uint8Array(2))).toBeNull();
    });
    it('decodeConsensus returns null below 14 bytes', () => {
      expect(decodeConsensus(new Uint8Array(13))).toBeNull();
    });
  });

  it('decodeBootMarker keeps the full version string when there is no NUL terminator', () => {
    const decoded = decodeBootMarker(new Uint8Array(bootMarkerPayload(1, 'ABCDEFGHIJKLMNOP', 0)));
    expect(decoded.fwVersion).toBe('ABCDEFGHIJKLMNOP'); // exactly 16 chars, no NUL
  });

  it('parseDclgHeader returns null when the buffer is shorter than the header', () => {
    expect(parseDclgHeader(new Uint8Array(15))).toBeNull();
  });

  it('parseLogStream accepts a plain array input', () => {
    const stream = buildStream([buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, 'cd'))]);
    const records = parseLogStream(Array.from(stream));
    expect(records).toHaveLength(1);
  });

  it('parseLogStream accepts an ArrayBuffer input', () => {
    const stream = buildStream([buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, 'ab'))]);
    const records = parseLogStream(stream.buffer.slice(stream.byteOffset, stream.byteOffset + stream.byteLength));
    expect(records).toHaveLength(1);
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

    it('flattens a BATCH nested inside a BATCH in the running count', () => {
      const innermost = [
        ...buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, 'x')),
        ...buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, 'y'))
      ];
      const inner = [
        ...buildRecord(FL_TYPE_BATCH, innermost),
        ...buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, 'z'))
      ];
      const stream = buildStream([buildRecord(FL_TYPE_BATCH, inner)]);
      const counter = makeRecordCounter();
      expect(counter(stream)).toBe(3);
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
