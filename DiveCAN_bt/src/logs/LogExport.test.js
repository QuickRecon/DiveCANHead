import { describe, it, expect, vi, afterEach } from 'vitest';
import { toJSON, toCSV, toRawBin, buildExportRows, triggerDownload } from './LogExport.js';
import { parseLogStream } from './LogParser.js';
import {
  buildStream, buildRecord, logTextPayload, bootMarkerPayload,
  FL_TYPE_LOG_TEXT, FL_TYPE_BOOT_MARKER
} from '../../tests/fixtures/log-streams.js';
import { FL_TYPE_CONSENSUS } from '../uds/constants.js';

/** A 14-byte CONSENSUS payload -> decodeConsensus yields array fields (ppo2, millivolts). */
function consensusPayload() {
  return [
    100,          // consensusPpo2
    90, 95, 105,  // ppo2[0..2]
    0x10, 0x27, 0x20, 0x4E, 0x30, 0x75, // millivolts (3x u16 LE)
    0x00, 0x00,   // statusPacked
    80,           // confidence
    130           // setpoint
  ];
}

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

  it('accepts a plain Array for the raw Blob', () => {
    const blob = toRawBin([4, 5]);
    expect(blob.size).toBe(2);
  });

  it('formats array-valued decoded fields as [a,b,c] in the CSV summary', () => {
    const stream = buildStream([
      buildRecord(FL_TYPE_CONSENSUS, consensusPayload(), { tsUs: 5 })
    ]);
    const csv = toCSV(parseLogStream(stream));
    // ppo2 array -> "ppo2=[90,95,105]"; must appear inside the (quoted) summary col
    expect(csv).toContain('ppo2=[90,95,105]');
    // A comma-containing summary field is quoted
    expect(csv.split('\n')[1]).toContain('"');
  });

  describe('triggerDownload', () => {
    // Stub the anchor's click so jsdom does not attempt (unimplemented) navigation.
    function stubAnchors(clicked = []) {
      const realCreate = document.createElement.bind(document);
      vi.spyOn(document, 'createElement').mockImplementation((tag) => {
        const el = realCreate(tag);
        if (tag === 'a') { el.click = vi.fn(() => clicked.push(el.download)); }
        return el;
      });
      return clicked;
    }

    afterEach(() => {
      vi.restoreAllMocks();
      vi.unstubAllGlobals();
    });

    it('creates, clicks and cleans up an anchor for a string payload', () => {
      const createSpy = vi.spyOn(URL, 'createObjectURL').mockReturnValue('blob:mock');
      const revokeSpy = vi.spyOn(URL, 'revokeObjectURL').mockImplementation(() => {});
      const clicked = stubAnchors();

      triggerDownload('log.csv', 'a,b,c', 'text/csv');

      expect(createSpy).toHaveBeenCalledTimes(1);
      expect(clicked).toEqual(['log.csv']);
      expect(revokeSpy).toHaveBeenCalledWith('blob:mock');
      expect(document.querySelector('a')).toBeNull(); // removed from the DOM
    });

    it('passes a Blob through without re-wrapping', () => {
      const createSpy = vi.spyOn(URL, 'createObjectURL').mockReturnValue('blob:mock');
      vi.spyOn(URL, 'revokeObjectURL').mockImplementation(() => {});
      stubAnchors();
      const blob = new Blob([new Uint8Array([1, 2, 3])], { type: 'application/octet-stream' });
      triggerDownload('log.bin', blob);
      expect(createSpy).toHaveBeenCalledWith(blob);
    });

    it('is a no-op when there is no DOM (document undefined)', () => {
      const createSpy = vi.spyOn(URL, 'createObjectURL').mockReturnValue('blob:mock');
      vi.stubGlobal('document', undefined);
      expect(() => triggerDownload('x.txt', 'data')).not.toThrow();
      expect(createSpy).not.toHaveBeenCalled();
    });
  });
});
