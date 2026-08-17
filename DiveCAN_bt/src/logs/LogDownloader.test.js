import { describe, it, expect, beforeEach, vi } from 'vitest';
import { LogDownloader } from './LogDownloader.js';
import { MemoryLogDownloadStore } from './LogDownloadStore.js';
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

function selectorResultPayload({
  stream = 0,
  startId = 1,
  endId = 9,
  entryCount = 2,
  totalBytes = 0,
  status = 0
} = {}) {
  const payload = new Uint8Array(20);
  payload[0] = stream;
  payload[2] = startId & 0xFF;
  payload[3] = (startId >> 8) & 0xFF;
  payload[4] = endId & 0xFF;
  payload[5] = (endId >> 8) & 0xFF;
  payload[6] = entryCount & 0xFF;
  payload[7] = (entryCount >> 8) & 0xFF;
  payload[8] = (entryCount >> 16) & 0xFF;
  payload[9] = (entryCount >> 24) & 0xFF;
  payload[10] = totalBytes & 0xFF;
  payload[11] = (totalBytes >> 8) & 0xFF;
  payload[12] = (totalBytes >> 16) & 0xFF;
  payload[13] = (totalBytes >> 24) & 0xFF;
  const unsignedStatus = status >>> 0;
  payload[16] = unsignedStatus & 0xFF;
  payload[17] = (unsignedStatus >> 8) & 0xFF;
  payload[18] = (unsignedStatus >> 16) & 0xFF;
  payload[19] = (unsignedStatus >> 24) & 0xFF;
  return payload;
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
        if (overrides.selectorResult) {
          const r = overrides.selectorResult(did, req);
          if (r !== undefined) return r;
        }
        return buildRDBIResponse(0xF281, selectorResultPayload({ totalBytes: streamBytes.length }));
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
    const progress = [];
    const result = await logs.downloadLog({ maxChunk: 32, onProgress: (p) => progress.push(p) });

    // Anchored on the DCLG magic (0x47434C44 stored little-endian -> D L C G)
    expect(Array.from(result.raw.slice(0, 4))).toEqual([0x44, 0x4C, 0x43, 0x47]);
    const records = parseLogStream(result.raw);
    expect(records.map(r => r.type)).toEqual([FL_TYPE_BOOT_MARKER, FL_TYPE_LOG_TEXT]);

    // seq progression 1..N across the transfer frames
    const seqs = transport.getAllSent().filter(s => s[0] === 0x36).map(s => s[1]);
    expect(seqs[0]).toBe(1);
    expect(seqs[1]).toBe(2);

    // progress reports record count against the selector's entry_count estimate
    const final = progress[progress.length - 1];
    expect(final.entryCount).toBe(2);    // from the 0xF281 selector-result fixture
    expect(final.records).toBe(2);       // both records counted by the end
    expect(final.received).toBe(result.raw.length);
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

  it('downloadAll streams the whole ring in one walk-free select-all session', async () => {
    const stream = sampleStream();
    transport.setResponder(logResponder(stream));

    const result = await logs.downloadAll({ stream: 0 });

    // Exactly one SELECT_ALL (0xF106) then BEGIN_STREAM (0xF105) — NOT a
    // per-boot loop, and no by-boot (0xF101) selectors at all.
    const selects = transport.getAllSent().filter(s => s[0] === 0x31 && s[2] === 0xF1);
    expect(selects.map(s => s[3])).toEqual([0x06, 0x05]);
    expect(selects[0][4]).toBe(0); // select-all carries the telemetry stream byte

    // raw is the head's DCLG stream verbatim (no local re-encode), records parsed.
    expect(result.records).toHaveLength(2);
    expect(result.records[0].type).toBe(FL_TYPE_BOOT_MARKER);
    expect(parseLogStream(result.raw)).toHaveLength(2);
  });

  it('selectAll sends RID 0xF106 with the stream byte', async () => {
    transport.setResponder(logResponder(sampleStream()));
    await logs.selectAll(1);
    expect(Array.from(transport.getLastSent())).toEqual([0x31, 0x01, 0xF1, 0x06, 1]);
  });

  it('rejects a selector with no match (NRC 0x22)', async () => {
    transport.setResponder(logResponder(sampleStream(), {
      overrides: { select: (rid) => rid === 0xF103 ? buildNegativeResponse(0x31, 0x22) : undefined }
    }));
    await expect(logs.downloadLog()).rejects.toMatchObject({ nrc: 0x22 });
  });

  it('polls through busyRepeatRequest (NRC 0x21) while the head builds its index', async () => {
    let selectAttempts = 0;
    transport.setResponder(logResponder(sampleStream(), {
      overrides: {
        select: (rid) => {
          if (rid === 0xF103) { // latest-boot selector defers twice, then resolves
            selectAttempts++;
            if (selectAttempts <= 2) return buildNegativeResponse(0x31, 0x21);
          }
          return undefined;
        }
      }
    }));
    const poller = new LogDownloader(uds, { timeouts: { poll: 1 } });
    const result = await poller.downloadLog({ maxChunk: 253 });
    expect(selectAttempts).toBe(3); // two 0x21 busy answers + one resolve
    expect(parseLogStream(result.raw)).toHaveLength(2);
  });

  it('gives up polling busyRepeatRequest after the selector deadline', async () => {
    transport.setResponder(logResponder(sampleStream(), {
      overrides: { select: (rid) => rid === 0xF103 ? buildNegativeResponse(0x31, 0x21) : undefined }
    }));
    const poller = new LogDownloader(uds, { timeouts: { selector: 5, poll: 1 } });
    await expect(poller.downloadLog()).rejects.toMatchObject({ nrc: 0x21 });
  });

  it('surfaces begin-stream-without-selection (NRC 0x24)', async () => {
    transport.setResponder(logResponder(sampleStream(), {
      overrides: { select: (rid) => rid === 0xF105 ? buildNegativeResponse(0x31, 0x24) : undefined }
    }));
    await expect(logs.beginStream()).rejects.toMatchObject({ nrc: 0x24 });
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

  it('treats zero index-derived log stats as unavailable, not dive zero', async () => {
    const payload = new Uint8Array(56);
    transport.setResponder(() => buildRDBIResponse(0xF280, payload));

    const stats = await logs.readStats();

    expect(stats.telemetry.bootIdCurrent).toBe(0);
    expect(stats.telemetry.bootIdOldest).toBeNull();
    expect(stats.telemetry.diveIdLatest).toBeNull();
    expect(stats.telemetry.entriesTotalEstimate).toBeNull();
    expect(stats.text.bootIdOldest).toBeNull();
    expect(stats.text.diveIdLatest).toBeNull();
    expect(stats.text.entriesTotalEstimate).toBeNull();
  });

  it('polls NRC 0x21 while standalone latest-dive resolution builds a cold index', async () => {
    let attempts = 0;
    transport.setResponder(logResponder(sampleStream(), {
      overrides: {
        select: (rid) => {
          if (rid !== 0xF104) return undefined;
          attempts++;
          return attempts < 3 ? buildNegativeResponse(0x31, 0x21) : buildRoutineResponse(rid);
        },
        selectorResult: () => buildRDBIResponse(0xF281, selectorResultPayload({
          startId: 42, endId: 42, entryCount: 17
        }))
      }
    }));
    const poller = new LogDownloader(uds, { timeouts: { selector: 50, poll: 1 } });

    const result = await poller.resolveLatestDive();

    expect(attempts).toBe(3);
    expect(result.latestRetainedDiveId).toBe(42);
    expect(transport.getAllSent().filter(req => req[0] === 0x34)).toHaveLength(0);
    expect(transport.getAllSent().filter(req => req[0] === 0x36)).toHaveLength(0);
  });

  it('reports a successful 0xF281 latest-dive ID separately from its entry estimate', async () => {
    transport.setResponder(logResponder(sampleStream(), {
      overrides: {
        selectorResult: () => buildRDBIResponse(0xF281, selectorResultPayload({
          startId: 72, endId: 73, entryCount: 912
        }))
      }
    }));

    const result = await logs.resolveLatestDive();

    expect(result.latestRetainedDiveId).toBe(73);
    expect(result.endId).toBe(73);
    expect(result.entryCount).toBe(912);
    expect(result.latestRetainedDiveId).not.toBe(result.entryCount);
    expect(Array.from(transport.getAllSent()[0])).toEqual([0x31, 0x01, 0xF1, 0x04, 0]);
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

  it('downloadAll propagates a selector NRC instead of stitching around it', async () => {
    transport.setResponder(logResponder(sampleStream(), {
      overrides: { select: (rid) => rid === 0xF106 ? buildNegativeResponse(0x31, 0x22) : undefined }
    }));
    await expect(logs.downloadAll({ stream: 0 })).rejects.toMatchObject({ nrc: 0x22 });
  });

  it('downloadAll on the text stream selects RID_SELECT_ALL with stream = TEXT', async () => {
    transport.setResponder(logResponder(sampleStream()));
    await logs.downloadAll({ stream: 1 });
    const selectAll = transport.getAllSent()
      .find(s => s[0] === 0x31 && s[2] === 0xF1 && s[3] === 0x06);
    expect(selectAll[4]).toBe(1); // TEXT stream byte in the select-all params
  });

  it('downloadAll rejects an already-aborted download instead of exporting it', async () => {
    transport.setResponder(logResponder(sampleStream()));
    const controller = new AbortController();
    controller.abort();
    await expect(logs.downloadAll({ signal: controller.signal, maxChunk: 32 }))
      .rejects.toMatchObject({ name: 'AbortError' });
    expect(transport.getAllSent()).toHaveLength(0);
  });

  it('rejects an already-aborted selective download instead of exporting it', async () => {
    transport.setResponder(logResponder(sampleStream()));
    const controller = new AbortController();
    controller.abort();
    await expect(logs.downloadLog({ signal: controller.signal, maxChunk: 32 }))
      .rejects.toMatchObject({ name: 'AbortError' });
    expect(transport.getAllSent()).toHaveLength(0);
  });

  it('continues beyond 4096 BLE blocks until the short end-of-stream block', async () => {
    let transferCount = 0;
    let transferExitCount = 0;
    const sequences = [];
    const fastUds = {
      routineControl: async () => new Uint8Array(),
      readDataByIdentifier: async () => new Uint8Array(20),
      requestDownload: async () => 61,
      transferData: async (seq) => {
        transferCount++;
        sequences.push(seq);
        const body = transferCount <= 4096 ? new Uint8Array(61) : new Uint8Array(0);
        if (transferCount === 1) body.set([0x44, 0x4C, 0x43, 0x47]);
        return new Uint8Array([0x76, seq, ...body]);
      },
      requestTransferExit: async () => { transferExitCount++; }
    };
    const downloader = new LogDownloader(fastUds);

    const result = await downloader.downloadLog();

    expect(transferCount).toBe(4097);
    expect(result.chunkLens).toHaveLength(4097);
    expect(result.chunkLens.at(-1)).toBe(0);
    expect(result.raw).toHaveLength(4096 * 61);
    expect(transferExitCount).toBe(1);
    expect(sequences.slice(253, 257)).toEqual([254, 255, 1, 2]);
    expect(sequences).not.toContain(0);
  });

  it('rejects an explicit block ceiling before EOS and closes the transfer', async () => {
    let transferExitCount = 0;
    const fastUds = {
      routineControl: async () => new Uint8Array(),
      readDataByIdentifier: async () => new Uint8Array(20),
      requestDownload: async () => 61,
      transferData: async (seq) => new Uint8Array([0x76, seq, ...new Uint8Array(61)]),
      requestTransferExit: async () => { transferExitCount++; }
    };
    const downloader = new LogDownloader(fastUds);

    await expect(downloader.downloadLog({ maxBlocks: 3 })).rejects.toMatchObject({
      name: 'LogDownloadIncompleteError',
      code: 'LOG_DOWNLOAD_INCOMPLETE',
      details: { received: 183, chunks: 3, maxBlocks: 3 }
    });
    expect(transferExitCount).toBe(1);
  });

  it('grows the accumulation buffer for streams larger than the initial 4 KiB', async () => {
    const bigText = 'A'.repeat(5000);
    const stream = buildStream([
      buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, bigText), { tsUs: 1 })
    ]);
    transport.setResponder(logResponder(stream));
    const result = await logs.downloadLog({ maxChunk: 253 });
    const records = parseLogStream(result.raw);
    expect(records).toHaveLength(1);
    // level(1) + moduleId(2) + 5000 text bytes
    expect(records[0].payload.length).toBe(5003);
  });

  it('retries after a head reset, verifies the saved prefix, and appends without duplication', async () => {
    const stream = buildStream([
      buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, 'R'.repeat(300)), { tsUs: 1 })
    ]);
    const chunks = chunkify(stream, 32);
    let attempt = 0;
    let index = 0;
    const retryEvents = [];
    const fakeUds = {
      routineControl: async () => new Uint8Array(),
      readDataByIdentifier: async () => selectorResultPayload({ totalBytes: stream.length }),
      requestDownload: async () => { attempt++; index = 0; return 32; },
      transferData: async (seq) => {
        if (attempt === 1 && index === 3) throw new Error('Request timeout after head reset');
        return new Uint8Array([0x76, seq, ...(chunks[index++] || new Uint8Array(0))]);
      },
      requestTransferExit: async () => {}
    };
    const downloader = new LogDownloader(fakeUds);
    const store = new MemoryLogDownloadStore({ resumeKey: 'telemetry:all' });

    const result = await downloader.downloadLogResumable({
      store,
      resumeKey: 'telemetry:all',
      maxChunk: 32,
      retry: { maxAttempts: 3, initialDelayMs: 0, maxDelayMs: 0 },
      onRetry: event => retryEvents.push(event)
    });

    expect(attempt).toBe(2);
    expect(retryEvents).toHaveLength(1);
    expect(retryEvents[0].partialBytes).toBe(96);
    expect(result.complete).toBe(true);
    expect(result.attempts).toBe(2);
    expect(result.metrics).toMatchObject({ expectedBytes: null });
    expect(Array.from(await store.getBytes())).toEqual(Array.from(stream));
    expect(await store.getMetadata()).toMatchObject({
      complete: true,
      transferMetrics: result.metrics
    });
  });

  it('reconciles an advanced full ring and resumes without overwriting the saved prefix', async () => {
    const records = Array.from({ length: 12 }, (_, i) => buildRecord(
      FL_TYPE_LOG_TEXT,
      logTextPayload(2, i, `${String.fromCharCode(65 + i)}-${String(i).padStart(2, '0')}-`.repeat(9)),
      { tsUs: 1000 + i }
    ));
    const appended = buildRecord(
      FL_TYPE_LOG_TEXT, logTextPayload(2, 99, 'written-after-reboot'), { tsUs: 9999 });
    const original = buildStream(records);
    const advanced = buildStream([...records.slice(1), appended]);
    const reconciled = buildStream([...records, appended]);
    const originalChunks = chunkify(original, 253);
    const advancedChunks = chunkify(advanced, 253);
    let attempt = 0;
    let index = 0;
    const progress = [];
    const fakeUds = {
      routineControl: async () => new Uint8Array(),
      readDataByIdentifier: async () => selectorResultPayload(),
      requestDownload: async () => { attempt++; index = 0; return 253; },
      transferData: async (seq) => {
        if (attempt === 1 && index === 1) throw new Error('head rebooted');
        const chunks = attempt === 1 ? originalChunks : advancedChunks;
        return new Uint8Array([0x76, seq, ...(chunks[index++] || new Uint8Array(0))]);
      },
      requestTransferExit: async () => {}
    };
    const downloader = new LogDownloader(fakeUds);
    const store = new MemoryLogDownloadStore({ resumeKey: 'telemetry:all' });

    const result = await downloader.downloadLogResumable({
      store,
      resumeKey: 'telemetry:all',
      maxChunk: 253,
      retry: { maxAttempts: 3, initialDelayMs: 0, maxDelayMs: 0 },
      onProgress: event => progress.push(event)
    });

    expect(attempt).toBe(2);
    expect(result.complete).toBe(true);
    expect(Array.from(await store.getBytes())).toEqual(Array.from(reconciled));
    expect(progress.some(event => event.reconciledFrom > 16)).toBe(true);
  });

  it('preserves an inspectable partial and refuses to splice a changed ring on retry', async () => {
    const stream = sampleStream();
    const chunks = chunkify(stream, 32);
    let attempt = 0;
    let index = 0;
    const fakeUds = {
      routineControl: async () => new Uint8Array(),
      readDataByIdentifier: async () => selectorResultPayload({ totalBytes: stream.length }),
      requestDownload: async () => { attempt++; index = 0; return 32; },
      transferData: async (seq) => {
        if (attempt === 1 && index === 1) throw new Error('head rebooted');
        const body = (chunks[index++] || new Uint8Array(0)).slice();
        if (attempt === 2 && index === 1) body[10] ^= 0xFF;
        return new Uint8Array([0x76, seq, ...body]);
      },
      requestTransferExit: async () => {}
    };
    const downloader = new LogDownloader(fakeUds);
    const store = new MemoryLogDownloadStore({ resumeKey: 'telemetry:all' });

    await expect(downloader.downloadLogResumable({
      store,
      resumeKey: 'telemetry:all',
      maxChunk: 32,
      retry: { maxAttempts: 3, initialDelayMs: 0, maxDelayMs: 0 }
    })).rejects.toMatchObject({
      code: 'LOG_RESUME_MISMATCH',
      partial: { bytes: 32, complete: false }
    });
    expect(Array.from(await store.getBytes())).toEqual(Array.from(stream.slice(0, 32)));
    expect((await store.getMetadata()).complete).toBe(false);
  });

  it('aborts with the original partial untouched when ring reconciliation is ambiguous', async () => {
    const header = buildStream([]);
    const anchor = Uint8Array.from({ length: 128 }, (_, i) => (i * 37 + 11) & 0xFF);
    const original = new Uint8Array(header.length + 64 + anchor.length + 1 + anchor.length + 300);
    original.set(header, 0);
    original.fill(0xEE, header.length, header.length + 64);
    original.set(anchor, header.length + 64);
    original[header.length + 64 + anchor.length] = 0x7B;
    original.set(anchor, header.length + 65 + anchor.length);
    original.fill(0xA5, header.length + 65 + anchor.length * 2);
    const advanced = new Uint8Array(header.length + anchor.length + 300);
    advanced.set(header, 0);
    advanced.set(anchor, header.length);
    advanced.fill(0x5A, header.length + anchor.length);
    const originalChunks = chunkify(original, 253);
    const advancedChunks = chunkify(advanced, 253);
    let attempt = 0;
    let index = 0;
    const fakeUds = {
      routineControl: async () => new Uint8Array(),
      readDataByIdentifier: async () => selectorResultPayload(),
      requestDownload: async () => { attempt++; index = 0; return 253; },
      transferData: async (seq) => {
        if (attempt === 1 && index === 2) throw new Error('head rebooted');
        const chunks = attempt === 1 ? originalChunks : advancedChunks;
        return new Uint8Array([0x76, seq, ...(chunks[index++] || new Uint8Array(0))]);
      },
      requestTransferExit: async () => {}
    };
    const downloader = new LogDownloader(fakeUds);
    const store = new MemoryLogDownloadStore({ resumeKey: 'telemetry:all' });

    await expect(downloader.downloadLogResumable({
      store,
      resumeKey: 'telemetry:all',
      maxChunk: 253,
      retry: { maxAttempts: 3, initialDelayMs: 0, maxDelayMs: 0 }
    })).rejects.toMatchObject({
      code: 'LOG_RESUME_MISMATCH',
      message: expect.stringContaining('ambiguous overlap'),
      partial: { bytes: 506, complete: false }
    });
    expect(Array.from(await store.getBytes())).toEqual(Array.from(original.slice(0, 506)));
  });

  it('throttles routine progress work but always emits a final update', async () => {
    const now = vi.spyOn(Date, 'now').mockReturnValue(1000);
    const stream = buildStream([
      buildRecord(FL_TYPE_LOG_TEXT, logTextPayload(1, 0, 'P'.repeat(300)), { tsUs: 1 })
    ]);
    transport.setResponder(logResponder(stream));
    const progress = [];

    const result = await logs.downloadLog({
      maxChunk: 32,
      progressIntervalMs: Number.POSITIVE_INFINITY,
      progressMinBytes: Number.POSITIVE_INFINITY,
      onProgress: value => progress.push(value)
    });

    expect(result.raw).toHaveLength(stream.length);
    expect(progress).toHaveLength(2); // first response + forced short-EOS response
    expect(progress.at(-1).received).toBe(stream.length);
    now.mockRestore();
  });

  it('reports smoothed KiB/s, average KiB/s, and ETA from actual transfer bytes', () => {
    const now = vi.spyOn(Date, 'now');
    now.mockReturnValueOnce(0).mockReturnValueOnce(1000).mockReturnValueOnce(2000);
    const progress = [];
    const report = logs._progressReporter({
      expectedBytes: 4096,
      progressIntervalMs: 0,
      onProgress: value => progress.push(value)
    });

    report({ transferred: 1024, saved: 1024 });
    report({ transferred: 2048, saved: 2048 });

    expect(progress).toHaveLength(2);
    expect(progress[0]).toMatchObject({
      elapsedMs: 1000,
      expectedBytes: 4096,
      rateKiBps: 1,
      averageKiBps: 1,
      etaSeconds: 3
    });
    expect(progress[1]).toMatchObject({
      elapsedMs: 2000,
      rateKiBps: 1,
      averageKiBps: 1,
      etaSeconds: 2
    });
    now.mockRestore();
  });

  it('readStats returns null when the payload is shorter than two FCB blocks', async () => {
    transport.setResponder(() => buildRDBIResponse(0xF280, new Uint8Array(10)));
    expect(await logs.readStats()).toBeNull();
  });

  it('readSelectorResult returns null for a short payload', async () => {
    transport.setResponder(() => buildRDBIResponse(0xF281, new Uint8Array(10)));
    expect(await logs.readSelectorResult()).toBeNull();
  });

  it('readSelectorResult decodes a full result with a signed status', async () => {
    transport.setResponder(() => buildRDBIResponse(0xF281, [
      1, 0, 2, 0, 9, 0, 5, 0, 0, 0, 0x10, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF
    ]));
    const r = await logs.readSelectorResult();
    expect(r.stream).toBe(1);
    expect(r.startId).toBe(2);
    expect(r.endId).toBe(9);
    expect(r.entryCount).toBe(5);
    expect(r.totalBytes).toBe(0x10);
    expect(r.status).toBe(-1); // 0xFFFFFFFF as signed
  });

  it('selectByDive and selectLatestDive send their routine ids and parameters', async () => {
    transport.setResponder(logResponder(sampleStream()));

    await logs.selectByDive(0x0207, 1);
    // RID 0xF102, params: stream(u8) + dive_id(u16 LE)
    expect(Array.from(transport.getLastSent())).toEqual([0x31, 0x01, 0xF1, 0x02, 1, 0x07, 0x02]);

    await logs.selectLatestDive();
    // RID 0xF104, params: stream(u8) defaulting to telemetry
    expect(Array.from(transport.getLastSent())).toEqual([0x31, 0x01, 0xF1, 0x04, 0]);
  });

  it('a throwing progress listener does not abort the download', async () => {
    const consoleError = vi.spyOn(console, 'error').mockImplementation(() => {});
    transport.setResponder(logResponder(sampleStream()));
    logs.on('progress', () => { throw new Error('listener exploded'); });

    const result = await logs.downloadLog({ maxChunk: 32 });

    expect(parseLogStream(result.raw)).toHaveLength(2);
    expect(consoleError).toHaveBeenCalled();
    consoleError.mockRestore();
  });

  it('off detaches a progress listener', async () => {
    transport.setResponder(logResponder(sampleStream()));
    const listener = vi.fn();
    logs.on('progress', listener);
    logs.off('progress', listener);
    await logs.downloadLog({ maxChunk: 32 });
    expect(listener).not.toHaveBeenCalled();
  });

  it('readVerbosity and readCanCapture return null on empty data, values otherwise', async () => {
    transport.setResponder((req) => buildRDBIResponse((req[1] << 8) | req[2], []));
    expect(await logs.readVerbosity()).toBeNull();
    expect(await logs.readCanCapture()).toBeNull();

    transport.setResponder((req) => buildRDBIResponse((req[1] << 8) | req[2], [0x03]));
    expect(await logs.readCanCapture()).toBe(3);
  });
});
