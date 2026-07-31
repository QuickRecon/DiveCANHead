/**
 * Flash-log download + management over UDSClient.
 *
 * Download sequence (one per selection):
 *
 *   0x31 0x01 <selector RID> <params>   (resolve a range: all / boot / dive)
 *   -> 0x31 0x01 0xF105                  (BeginStream)
 *   -> 0x34 RequestDownload, sentinel addr 0xFFFFFFFE, size = max_chunk (LE)
 *   -> 0x36 TransferData x N             (seq from 1, wrap SKIPPING 0)
 *   -> 0x37 RequestTransferExit
 *
 * A chunk shorter than the negotiated block ends the stream. The result is a
 * 16-byte DCLG header + concatenated TLV records (parse with LogParser).
 *
 * "Download all" (downloadAll) uses the head's RID_SELECT_ALL selector, which
 * resolves the entire resident ring in one WALK-FREE selection: the complete
 * log streams in a single 0x34/0x36/0x37 session. The former per-boot
 * enumeration (read stats -> loop oldest..current by-boot -> trim sector
 * overlap -> re-encode a local DCLG stream) is gone — it existed only to work
 * around firmware that had no "all" primitive and paid a full index-build walk
 * per selector.
 */

import * as constants from '../uds/constants.js';
import { ByteUtils } from '../utils/ByteUtils.js';
import { makeRecordCounter, parseLogStream } from './LogParser.js';

class EventEmitter {
  constructor() { this.events = {}; }
  on(event, cb) {
    if (!this.events[event]) { this.events[event] = []; }
    this.events[event].push(cb);
    return this;
  }
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

/** Default timeouts (ms). An index-backed selector (by-boot/dive/latest) that
 * finds the FCB index cold is answered with busyRepeatRequest (NRC 0x21) while
 * the head builds it on its worker thread; _runSelector polls every `poll` ms
 * up to `selector` ms total. RID_SELECT_ALL is walk-free and never defers. */
export const LOG_TIMEOUTS = {
  selector: 40000,
  poll: 250,
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
    this.timeouts = { ...LOG_TIMEOUTS, ...options.timeouts };
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
    return d?.length ? d[0] : null;
  }

  /** DID 0xF283 write. */
  async setVerbosity(level) {
    await this.uds.writeDataByIdentifier(constants.DID_LOG_VERBOSITY, [level & 0xFF]);
  }

  /** DID 0xF284 -> CAN-capture bitmask (bit0=RX, bit1=TX). */
  async readCanCapture() {
    const d = await this.uds.readDataByIdentifier(constants.DID_LOG_CAN_VERBOSE);
    return d?.length ? d[0] : null;
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

  /**
   * Select the entire resident ring (walk-free "download all"). The head
   * resolves this with no marker-index build, so it returns immediately even
   * on a full ring. See downloadAll().
   */
  selectAll(stream = constants.LOG_STREAM_TELEMETRY) {
    return this.uds.routineControl(constants.LOG_RID_SELECT_ALL,
      [stream], this.timeouts.selector);
  }

  beginStream() {
    return this.uds.routineControl(constants.LOG_RID_BEGIN_STREAM, [], this.timeouts.beginStream);
  }

  /**
   * Download every retained entry in one walk-free select-all session.
   *
   * The head's RID_SELECT_ALL resolves the whole resident ring without a
   * marker-index build, so this is a single 0x34/0x36/0x37 transfer of the
   * complete DCLG stream — no per-boot enumeration, no local re-stitching.
   * `raw` is the head's stream verbatim; `records` is it parsed.
   *
   * @param {Object} [opts] - downloadLog options (stream, maxChunk, maxBlocks,
   *   onProgress, signal)
   * @returns {Promise<{raw:Uint8Array, records:Array, negotiatedBlock:number,
   *   chunkLens:number[], selector:Object|null}>}
   */
  async downloadAll(opts = {}) {
    const stream = opts.stream ?? constants.LOG_STREAM_TELEMETRY;
    const result = await this.downloadLog({
      ...opts,
      stream,
      selector: (downloader) => downloader.selectAll(stream)
    });
    const records = parseLogStream(result.raw);
    this.emit('doneAll', { raw: result.raw, records });
    return { ...result, records };
  }

  /**
   * Resolve a boot/dive range via the caller's selector (default: latest boot),
   * transparently polling through busyRepeatRequest (NRC 0x21) while the head
   * builds its FCB index. Re-issuing the identical selector is how the head
   * hands back the result once its worker finishes. RID_SELECT_ALL never
   * defers, so downloadAll passes straight through on the first attempt.
   * @private
   */
  async _runSelector(opts, stream) {
    const runOnce = () => (opts.selector ? opts.selector(this) : this.selectLatestBoot(stream));
    const deadline = Date.now() + this.timeouts.selector;
    for (;;) {
      try {
        await runOnce();
        return;
      } catch (error) {
        const building = error?.nrc === constants.NRC_BUSY_REPEAT_REQUEST;
        if (!building || Date.now() >= deadline) throw error;
        if (opts.signal?.aborted) throw new DOMException('Log download cancelled', 'AbortError');
        this.emit('indexBuilding', { stream });
        await new Promise(resolve => setTimeout(resolve, this.timeouts.poll));
      }
    }
  }

  /**
   * Read the resolved range for a progress denominator (best-effort).
   * NOTE: the firmware hard-codes total_bytes to 0 (it avoids a pre-walk), so
   * entry_count is the only usable progress denominator — drive the bar off
   * the running record count vs this estimate.
   * @private
   */
  async _readSelectorEstimate() {
    let selector = null;
    let entryCount = 0;
    try {
      selector = await this.readSelectorResult();
      entryCount = selector?.entryCount || 0;
    } catch { /* selector-result read is advisory */ }
    return { selector, entryCount };
  }

  /** Grow `acc` (doubling) so it can hold `accLen + extra` bytes. @private */
  _ensureCapacity(acc, accLen, extra) {
    let grown = acc;
    if (accLen + extra > acc.length) {
      let cap = acc.length * 2;
      while (cap < accLen + extra) { cap *= 2; }
      grown = new Uint8Array(cap);
      grown.set(acc.subarray(0, accLen));
    }
    return grown;
  }

  /**
   * Pull chunks (seq from 1, wrap skipping 0; short chunk = EOS). Bytes are
   * appended into a single amortised-growth buffer so the resumable record
   * counter (progress) stays O(total), not O(total^2).
   * @private
   */
  async _pullLogChunks(opts, negotiatedBlock, maxBlocks, entryCount) {
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
      acc = this._ensureCapacity(acc, accLen, body.length);
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
    return { acc, accLen, chunkLens };
  }

  /** Anchor on the DCLG magic, trimming any leading garbage. @private */
  _trimToMagic(rawAll) {
    const magic = ByteUtils.uint32ToLE(constants.LOG_DOWNLOAD_MAGIC);
    let idx = -1;
    for (let i = 0; i + 4 <= rawAll.length; i++) {
      if (rawAll[i] === magic[0] && rawAll[i + 1] === magic[1]
        && rawAll[i + 2] === magic[2] && rawAll[i + 3] === magic[3]) { idx = i; break; }
    }
    return rawAll.slice(Math.max(idx, 0));
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
    await this._runSelector(opts, stream);

    const { selector, entryCount } = await this._readSelectorEstimate();

    // 2. Arm the stream.
    await this.beginStream();

    // 3. RequestDownload with the sentinel addr (LE) + client max_chunk (LE size).
    const negotiatedBlock = await this.uds.requestDownload(
      constants.LOG_DOWNLOAD_SENTINEL_ADDR, maxChunk, { sizeEndian: 'LE' }, this.timeouts.block);

    // 4. Pull chunks until a short chunk (or abort) ends the stream.
    const { acc, accLen, chunkLens } = await this._pullLogChunks(opts, negotiatedBlock, maxBlocks, entryCount);

    // 5. End the transfer.
    await this.uds.requestTransferExit(this.timeouts.block);

    // 6. Anchor on the DCLG magic, trimming any leading garbage.
    const raw = this._trimToMagic(acc.subarray(0, accLen));

    this.emit('done', { raw, negotiatedBlock, chunkLens });
    return { raw, negotiatedBlock, chunkLens, selector };
  }
}
