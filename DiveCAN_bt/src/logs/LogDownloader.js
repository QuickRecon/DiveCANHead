/**
 * Flash-log download + management over UDSClient.
 *
 * Mirrors the Test Rig reference (Test Rig/tests/test_dut_logs.py, dut.py:
 * 1268-1436). Download sequence:
 *
 *   0x31 0x01 <selector RID> <params>   (resolve a boot/dive range)
 *   -> 0x31 0x01 0xF105                  (BeginStream)
 *   -> 0x34 RequestDownload, sentinel addr 0xFFFFFFFE, size = max_chunk (LE)
 *   -> 0x36 TransferData x N             (seq from 1, wrap SKIPPING 0)
 *   -> 0x37 RequestTransferExit
 *
 * A chunk shorter than the negotiated block ends the stream. The result is a
 * 16-byte DCLG header + concatenated TLV records (parse with LogParser).
 */

import * as constants from '../uds/constants.js';
import { ByteUtils } from '../utils/ByteUtils.js';
import { makeRecordCounter } from './LogParser.js';

class EventEmitter {
  constructor() { this.events = {}; }
  on(event, cb) { (this.events[event] ||= []).push(cb); return this; }
  off(event, cb) {
    if (this.events[event]) this.events[event] = this.events[event].filter(f => f !== cb);
    return this;
  }
  emit(event, ...args) {
    (this.events[event] || []).forEach(cb => {
      try { cb(...args); } catch (e) { console.error(`Log handler ${event}`, e); }
    });
  }
}

/** Default timeouts (ms). The first selector after boot builds the FCB index (~24 s). */
export const LOG_TIMEOUTS = {
  selector: 40000,
  beginStream: 10000,
  block: 40000,
  read: 4000
};

function decodeFcbStats(b) {
  return {
    bootIdCurrent: ByteUtils.leToUint32(b.slice(0, 4)),
    bootIdOldest: ByteUtils.leToUint32(b.slice(4, 8)),
    diveIdLatest: ByteUtils.leToUint16(b.slice(8, 10)),
    entriesTotalEstimate: ByteUtils.leToUint32(b.slice(12, 16)),
    dropsSinceBoot: ByteUtils.leToUint32(b.slice(16, 20)),
    sectorsFree: ByteUtils.leToUint16(b.slice(20, 22)),
    sectorsTotal: ByteUtils.leToUint16(b.slice(22, 24))
  };
}

export class LogDownloader extends EventEmitter {
  /**
   * @param {import('../uds/UDSClient.js').UDSClient} uds
   * @param {Object} [options]
   * @param {number} [options.maxChunk] - Client max receivable block (default BLE 61)
   * @param {Object} [options.timeouts] - Override LOG_TIMEOUTS
   */
  constructor(uds, options = {}) {
    super();
    this.uds = uds;
    this.options = options;
    this.timeouts = { ...LOG_TIMEOUTS, ...(options.timeouts || {}) };
    this.maxChunk = options.maxChunk ?? constants.LOG_DOWNLOAD_BLE_CHUNK;
  }

  // -------- management reads/writes --------

  /** DID 0xF280 -> {telemetry, text} FCB stats (28-byte stride). */
  async readStats() {
    const d = await this.uds.readDataByIdentifier(constants.DID_LOG_STATS);
    const n = constants.FL_FCB_STATS_LEN;
    if (!d || d.length < 2 * n) return null;
    return { telemetry: decodeFcbStats(d.slice(0, n)), text: decodeFcbStats(d.slice(n, 2 * n)) };
  }

  /** DID 0xF281 -> last selector resolution ({status} is a signed errno). */
  async readSelectorResult() {
    const d = await this.uds.readDataByIdentifier(constants.DID_LOG_SELECTOR_RESULT);
    if (!d || d.length < 20) return null;
    return {
      stream: d[0],
      startId: ByteUtils.leToUint16(d.slice(2, 4)),
      endId: ByteUtils.leToUint16(d.slice(4, 6)),
      entryCount: ByteUtils.leToUint32(d.slice(6, 10)),
      totalBytes: ByteUtils.leToUint32(d.slice(10, 14)),
      status: ByteUtils.leToUint32(d.slice(16, 20)) | 0 // signed
    };
  }

  /** DID 0xF283 -> text-log verbosity (1=ERR..4=DBG). */
  async readVerbosity() {
    const d = await this.uds.readDataByIdentifier(constants.DID_LOG_VERBOSITY);
    return d && d.length ? d[0] : null;
  }

  /** DID 0xF283 write. */
  async setVerbosity(level) {
    await this.uds.writeDataByIdentifier(constants.DID_LOG_VERBOSITY, [level & 0xFF]);
  }

  /** DID 0xF284 -> CAN-capture bitmask (bit0=RX, bit1=TX). */
  async readCanCapture() {
    const d = await this.uds.readDataByIdentifier(constants.DID_LOG_CAN_VERBOSE);
    return d && d.length ? d[0] : null;
  }

  /** DID 0xF284 write. */
  async setCanCapture(mask) {
    await this.uds.writeDataByIdentifier(constants.DID_LOG_CAN_VERBOSE, [mask & 0xFF]);
  }

  /** DID 0xF282 erase ([stream_mask, magic 0xA5]); bit0=telemetry, bit1=text. */
  async eraseLog(streamMask) {
    await this.uds.writeDataByIdentifier(
      constants.DID_LOG_ERASE, [streamMask & 0x03, constants.LOG_ERASE_MAGIC]);
  }

  // -------- selectors --------

  selectByBoot(bootId, stream = constants.LOG_STREAM_TELEMETRY) {
    return this.uds.routineControl(constants.LOG_RID_SELECT_BY_BOOT,
      [stream, ...ByteUtils.uint32ToLE(bootId)], this.timeouts.selector);
  }

  selectByDive(diveNumber, stream = constants.LOG_STREAM_TELEMETRY) {
    return this.uds.routineControl(constants.LOG_RID_SELECT_BY_DIVE,
      [stream, ...ByteUtils.uint16ToLE(diveNumber)], this.timeouts.selector);
  }

  selectLatestBoot(stream = constants.LOG_STREAM_TELEMETRY) {
    return this.uds.routineControl(constants.LOG_RID_SELECT_LATEST_BOOT,
      [stream], this.timeouts.selector);
  }

  selectLatestDive(stream = constants.LOG_STREAM_TELEMETRY) {
    return this.uds.routineControl(constants.LOG_RID_SELECT_LATEST_DIVE,
      [stream], this.timeouts.selector);
  }

  beginStream() {
    return this.uds.routineControl(constants.LOG_RID_BEGIN_STREAM, [], this.timeouts.beginStream);
  }

  /**
   * Download a flash-log stream. Runs the selector, begins the stream, then
   * pulls chunks until a short chunk terminates it.
   * @param {Object} [opts]
   * @param {number} [opts.stream=TELEMETRY]
   * @param {(dl:LogDownloader)=>Promise<Uint8Array>} [opts.selector] - defaults to latest boot
   * @param {number} [opts.maxChunk] - client max receivable block (overrides ctor)
   * @param {number} [opts.maxBlocks=4096]
   * @param {(received:number,total:number)=>void} [opts.onProgress]
   * @param {AbortSignal} [opts.signal]
   * @returns {Promise<{raw:Uint8Array, negotiatedBlock:number, chunkLens:number[], selector:Object|null}>}
   */
  async downloadLog(opts = {}) {
    const stream = opts.stream ?? constants.LOG_STREAM_TELEMETRY;
    const maxChunk = opts.maxChunk ?? this.maxChunk;
    const maxBlocks = opts.maxBlocks ?? 4096;

    // 1. Resolve a range.
    if (opts.selector) {
      await opts.selector(this);
    } else {
      await this.selectLatestBoot(stream);
    }

    // Read the resolved range for a progress denominator (best-effort).
    // NOTE: the firmware hard-codes total_bytes to 0 (it avoids a pre-walk), so
    // entry_count is the only usable progress denominator — drive the bar off
    // the running record count vs this estimate.
    let selector = null;
    let entryCount = 0;
    try {
      selector = await this.readSelectorResult();
      entryCount = selector?.entryCount || 0;
    } catch { /* selector-result read is advisory */ }

    // 2. Arm the stream.
    await this.beginStream();

    // 3. RequestDownload with the sentinel addr (LE) + client max_chunk (LE size).
    const negotiatedBlock = await this.uds.requestDownload(
      constants.LOG_DOWNLOAD_SENTINEL_ADDR, maxChunk, { sizeEndian: 'LE' }, this.timeouts.block);

    // 4. Pull chunks (seq from 1, wrap skipping 0; short chunk = EOS). Bytes are
    //    appended into a single amortised-growth buffer so the resumable record
    //    counter (progress) stays O(total), not O(total^2).
    const chunkLens = [];
    const countRecords = makeRecordCounter();
    let acc = new Uint8Array(4096);
    let accLen = 0;
    let received = 0;
    let seq = 1;
    for (let i = 0; i < maxBlocks; i++) {
      if (opts.signal?.aborted) break;
      const resp = await this.uds.transferData(seq, [], this.timeouts.block);
      const body = resp.slice(2); // strip [0x76, seq]
      if (accLen + body.length > acc.length) {
        let cap = acc.length * 2;
        while (cap < accLen + body.length) { cap *= 2; }
        const bigger = new Uint8Array(cap);
        bigger.set(acc.subarray(0, accLen));
        acc = bigger;
      }
      acc.set(body, accLen);
      accLen += body.length;
      chunkLens.push(body.length);
      received += body.length;
      const records = countRecords(acc.subarray(0, accLen));
      const progress = { received, records, entryCount, chunks: chunkLens.length };
      this.emit('progress', progress);
      if (opts.onProgress) opts.onProgress(progress);
      seq = (seq + 1) & 0xFF;
      if (seq === 0) seq = 1;
      if (body.length < negotiatedBlock) break; // short chunk = end of stream
    }

    // 5. End the transfer.
    await this.uds.requestTransferExit(this.timeouts.block);

    // 6. Anchor on the DCLG magic, trimming any leading garbage.
    const rawAll = acc.subarray(0, accLen);
    const magic = ByteUtils.uint32ToLE(constants.LOG_DOWNLOAD_MAGIC);
    let idx = -1;
    for (let i = 0; i + 4 <= rawAll.length; i++) {
      if (rawAll[i] === magic[0] && rawAll[i + 1] === magic[1]
        && rawAll[i + 2] === magic[2] && rawAll[i + 3] === magic[3]) { idx = i; break; }
    }
    const raw = rawAll.slice(idx < 0 ? 0 : idx);

    this.emit('done', { raw, negotiatedBlock, chunkLens });
    return { raw, negotiatedBlock, chunkLens, selector };
  }
}
