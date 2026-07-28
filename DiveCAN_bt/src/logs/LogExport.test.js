import { describe, it, expect } from 'vitest';
import { toJSON, toCSV, toRawBin, buildExportRows } from './LogExport.js';
import { parseLogStream } from './LogParser.js';
import {
  buildStream, buildRecord, logTextPayload, bootMarkerPayload,
  FL_TYPE_LOG_TEXT, FL_TYPE_BOOT_MARKER
} from '../../tests/fixtures/log-streams.js';

function sampleRecords() {
  const stream = buildStream([
    buildRecord(FL_TYPE_BOOT_MARKER, bootMarkerPayload(1, 'v1', 0), { tsUs: 10 }),
    buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(2, 5, 'hello, world'), { tsUs: 20 })
  ]);
  return parseLogStream(stream);
}

describe('LogExport', () => {
  it('builds export rows with stringified timestamps', () => {
    const rows = buildExportRows(sampleRecords());
    expect(rows).toHaveLength(2);
    expect(rows[0].tsUs).toBe('10');
    expect(rows[1].decoded).toEqual({ level: 2, moduleId: 5, text: 'hello, world' });
  });

  it('produces valid JSON', () => {
    const json = toJSON(sampleRecords());
    const parsed = JSON.parse(json);
    expect(parsed[1].typeName).toBe('Log Text');
    expect(parsed[1].decoded.text).toBe('hello, world');
  });

  it('produces CSV with a header and escaped commas', () => {
    const csv = toCSV(sampleRecords());
    const lines = csv.split('\n');
    expect(lines[0]).toBe('index,type,type_name,ts_us,flags,summary,payload_hex');
    expect(lines).toHaveLength(3);
    // The log-text summary contains a comma -> must be quoted
    expect(lines[2]).toContain('"');
  });

  it('wraps raw bytes in a Blob', () => {
    const blob = toRawBin(new Uint8Array([1, 2, 3]));
    expect(blob.size).toBe(3);
    expect(blob.type).toBe('application/octet-stream');
  });
});
