import { describe, it, expect, beforeEach } from 'vitest';
import { LogDownloader } from './LogDownloader.js';
import { parseLogStream } from './LogParser.js';
import { UDSClient } from '../uds/UDSClient.js';
import { MockTransport } from '../../tests/mocks/MockTransport.js';
import {
  buildRoutineResponse, buildRequestDownloadResponse, buildTransferResponse,
  buildTransferExitResponse, buildRDBIResponse, buildWDBIResponse, buildNegativeResponse
} from '../../tests/fixtures/uds-responses.js';
import {
  buildStream, buildRecord, bootMarkerPayload, logTextPayload,
  FL_TYPE_BOOT_MARKER, FL_TYPE_LOG_TEXT
} from '../../tests/fixtures/log-streams.js';

function sampleStream() {
  return buildStream([
    buildRecord(FL_TYPE_BOOT_MARKER, bootMarkerPayload(9, 'v2.0.0', 1), { tsUs: 100 }),
    buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(3, 7, 'diving'), { tsUs: 200 })
  ]);
}

/** Split bytes into chunks of `block`, ensuring a final short chunk marks EOS. */
function chunkify(bytes, block) {
  const chunks = [];
  for (let off = 0; off < bytes.length; off += block) {
    chunks.push(bytes.slice(off, off + block));
  }
  if (chunks.length === 0 || chunks[chunks.length - 1].length === block) {
    chunks.push(new Uint8Array(0));
  }
  return chunks;
}

/**
 * Scripted log-download responder. Reads the client's requested max_chunk from
 * the 0x34 SIZE field (LE), floors/caps it, and serves the sample stream in
 * blocks of that size. `overrides` maps a SID (or 'select') to a fault fn.
 */
function logResponder(streamBytes, { overrides = {} } = {}) {
  let chunks = null;
  let idx = 0;
  let block = 0;
  return (req) => {
    const sid = req[0];
    if (sid === 0x31) {
      const rid = (req[2] << 8) | req[3];
      if (overrides.select) { const r = overrides.select(rid, req); if (r !== undefined) return r; }
      return buildRoutineResponse(rid);
    }
    if (sid === 0x22) {
      const did = (req[1] << 8) | req[2];
      if (did === 0xF281) {
        // selector result: stream 0, start 1, end 9, entryCount 2, totalBytes len
        const tb = streamBytes.length;
        return buildRDBIResponse(0xF281, [
          0, 0, 1, 0, 9, 0, 2, 0, 0, 0,
          tb & 0xFF, (tb >> 8) & 0xFF, 0, 0, 0, 0, 0, 0, 0, 0
        ]);
      }
      return buildRDBIResponse(did, []);
    }
    if (sid === 0x34) {
      const requested = req[7] | (req[8] << 8) | (req[9] << 16) | (req[10] << 24);
      block = Math.min(Math.max(requested || 253, 32), 253);
      chunks = chunkify(streamBytes, block);
      idx = 0;
      return buildRequestDownloadResponse(block);
    }
    if (sid === 0x36) {
      const body = idx < chunks.length ? chunks[idx++] : new Uint8Array(0);
      return buildTransferResponse(req[1], body);
    }
    if (sid === 0x37) return buildTransferExitResponse();
    if (sid === 0x2E) return buildWDBIResponse((req[1] << 8) | req[2]);
    return null;
  };
}

describe('LogDownloader', () => {
  let transport;
  let uds;
  let logs;

  beforeEach(() => {
    transport = new MockTransport();
    uds = new UDSClient(transport);
    logs = new LogDownloader(uds);
  });

  it('downloads a full stream and parses records', async () => {
    const stream = sampleStream();
    transport.setResponder(logResponder(stream));
    const result = await logs.downloadLog({ maxChunk: 32 });

    // Anchored on the DCLG magic (0x47434C44 stored little-endian -> D L C G)
    expect(Array.from(result.raw.slice(0, 4))).toEqual([0x44, 0x4C, 0x43, 0x47]);
    const records = parseLogStream(result.raw);
    expect(records.map(r => r.type)).toEqual([FL_TYPE_BOOT_MARKER, FL_TYPE_LOG_TEXT]);

    // seq progression 1..N across the transfer frames
    const seqs = transport.getAllSent().filter(s => s[0] === 0x36).map(s => s[1]);
    expect(seqs[0]).toBe(1);
    expect(seqs[1]).toBe(2);
  });

  it('honours the client max_chunk (block cap)', async () => {
    const stream = sampleStream();
    transport.setResponder(logResponder(stream));
    const result = await logs.downloadLog({ maxChunk: 48 });
    expect(result.negotiatedBlock).toBe(48);
    // every non-final chunk is exactly the negotiated block
    const nonFinal = result.chunkLens.slice(0, -1);
    for (const len of nonFinal) expect(len).toBe(48);
  });

  it('floors max_chunk to 32', async () => {
    const stream = sampleStream();
    transport.setResponder(logResponder(stream));
    const result = await logs.downloadLog({ maxChunk: 8 });
    expect(result.negotiatedBlock).toBe(32);
  });

  it('sends the sentinel address little-endian in the 0x34 request', async () => {
    transport.setResponder(logResponder(sampleStream()));
    await logs.downloadLog({ maxChunk: 61 });
    const dl = transport.getAllSent().find(s => s[0] === 0x34);
    expect(Array.from(dl.slice(3, 7))).toEqual([0xFE, 0xFF, 0xFF, 0xFF]);
    // size field = 61 little-endian
    expect(Array.from(dl.slice(7, 11))).toEqual([61, 0, 0, 0]);
  });

  it('rejects a selector with no match (NRC 0x22)', async () => {
    transport.setResponder(logResponder(sampleStream(), {
      overrides: { select: (rid) => rid === 0xF103 ? buildNegativeResponse(0x31, 0x22) : undefined }
    }));
    await expect(logs.downloadLog()).rejects.toMatchObject({ nrc: 0x22 });
  });

  it('surfaces begin-stream-without-selection (NRC 0x24)', async () => {
    transport.setResponder(logResponder(sampleStream(), {
      overrides: { select: (rid) => rid === 0xF105 ? buildNegativeResponse(0x31, 0x24) : undefined }
    }));
    await expect(logs.beginStream()).rejects.toMatchObject({ nrc: 0x24 });
  });

  it('surfaces by-range as unimplemented (NRC 0x31)', async () => {
    transport.setResponder((req) =>
      req[0] === 0x31 ? buildNegativeResponse(0x31, 0x31) : null);
    await expect(
      uds.routineControl(0xF100, [0])
    ).rejects.toMatchObject({ nrc: 0x31 });
  });

  it('decodes log stats (28-byte FCB stride)', async () => {
    // two 28-byte FCB stats blocks
    const telem = new Uint8Array(28);
    telem.set([5, 0, 0, 0]);         // boot_id_current = 5
    telem.set([3, 0], 8);            // dive_id_latest = 3
    telem.set([0x10, 0], 20);        // sectors_free = 16
    telem.set([0x20, 0], 22);        // sectors_total = 32
    const text = new Uint8Array(28);
    text.set([9, 0, 0, 0]);          // boot_id_current = 9
    const payload = new Uint8Array(56);
    payload.set(telem, 0);
    payload.set(text, 28);
    transport.setResponder(() => buildRDBIResponse(0xF280, payload));

    const stats = await logs.readStats();
    expect(stats.telemetry.bootIdCurrent).toBe(5);
    expect(stats.telemetry.diveIdLatest).toBe(3);
    expect(stats.telemetry.sectorsFree).toBe(16);
    expect(stats.telemetry.sectorsTotal).toBe(32);
    expect(stats.text.bootIdCurrent).toBe(9);
  });

  it('reads/writes verbosity and CAN capture', async () => {
    transport.setResponder((req) => {
      if (req[0] === 0x22) return buildRDBIResponse((req[1] << 8) | req[2], [0x02]);
      if (req[0] === 0x2E) return buildWDBIResponse((req[1] << 8) | req[2]);
      return null;
    });
    expect(await logs.readVerbosity()).toBe(2);
    await logs.setVerbosity(4);
    expect(Array.from(transport.getLastSent())).toEqual([0x2E, 0xF2, 0x83, 4]);
    await logs.setCanCapture(0x03);
    expect(Array.from(transport.getLastSent())).toEqual([0x2E, 0xF2, 0x84, 3]);
  });

  it('erases a log with the magic byte', async () => {
    transport.setResponder((req) => buildWDBIResponse((req[1] << 8) | req[2]));
    await logs.eraseLog(0x03);
    expect(Array.from(transport.getLastSent())).toEqual([0x2E, 0xF2, 0x82, 0x03, 0xA5]);
  });
});
