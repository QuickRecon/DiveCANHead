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

/** Hard safety ceiling for a single decoded log stream. The largest current
 * on-head ring is 48 MiB; 64 MiB leaves room for the DCLG header and future
 * format growth without allowing a broken peer to stream forever. */
export const LOG_DOWNLOAD_MAX_BYTES = 64 * 1024 * 1024;

/** Progress callbacks are UI work, not transfer flow control. Keep them well
 * below the 10s/100s of TransferData responses per second seen on USB-CAN. */
export const LOG_PROGRESS_INTERVAL_MS = 250;
export const LOG_PROGRESS_MIN_BYTES = 64 * 1024;

/** Automatic recovery policy for a head reset or transient transport loss. */
export const LOG_RETRY_DEFAULTS = {
  maxAttempts: 5,
  initialDelayMs: 5000,
  maxDelayMs: 30000,
  cleanupTimeoutMs: 2000
};

/** Disk prefix is compared in windows so a 48 MiB resume never enters heap. */
export const LOG_RESUME_VERIFY_WINDOW_BYTES = 64 * 1024;
export const LOG_RESUME_ANCHOR_BYTES = 128;
export const LOG_RESUME_MIN_ANCHOR_BYTES = 32;

/** Raised when a transfer stops before the head's short end-of-stream block. */
export class LogDownloadIncompleteError extends Error {
  constructor(message, details = {}) {
    super(message);
    this.name = 'LogDownloadIncompleteError';
    this.code = 'LOG_DOWNLOAD_INCOMPLETE';
    this.details = details;
  }
}

/** Raised rather than splicing bytes when the on-head ring changed on retry. */
export class LogResumeMismatchError extends Error {
  constructor(message, details = {}) {
    super(message);
    this.name = 'LogResumeMismatchError';
    this.code = 'LOG_RESUME_MISMATCH';
    this.details = details;
  }
}

class StorePrefixVerifier {
  constructor(store, total, windowBytes = LOG_RESUME_VERIFY_WINDOW_BYTES) {
    this.store = store;
    this.total = total;
    this.windowBytes = windowBytes;
    this.windowStart = -1;
    this.window = new Uint8Array(0);
  }

  async verify(bytes, offset) {
    let inputOffset = 0;
    while (inputOffset < bytes.length) {
      const absolute = offset + inputOffset;
      const windowStart = Math.floor(absolute / this.windowBytes) * this.windowBytes;
      if (windowStart !== this.windowStart) {
        const length = Math.min(this.windowBytes, this.total - windowStart);
        this.window = await this.store.read(windowStart, length);
        this.windowStart = windowStart;
        if (this.window.length !== length) {
          throw new LogResumeMismatchError('Saved partial log is shorter than its recorded size', {
            offset: absolute, expected: length, actual: this.window.length
          });
        }
      }
      const within = absolute - this.windowStart;
      const count = Math.min(bytes.length - inputOffset, this.window.length - within);
      for (let i = 0; i < count; i++) {
        if (bytes[inputOffset + i] !== this.window[within + i]) {
          throw new LogResumeMismatchError(
            `Retried stream differs from the saved partial log at byte ${absolute + i}`,
            { offset: absolute + i, expected: this.window[within + i], actual: bytes[inputOffset + i] });
        }
      }
      inputOffset += count;
    }
  }
}

function decodeFcbStats(b) {
  const bootIdOldest = ByteUtils.leToUint32(b.slice(4, 8));
  const diveIdLatest = ByteUtils.leToUint16(b.slice(8, 10));
  const entriesTotalEstimate = ByteUtils.leToUint32(b.slice(12, 16));
  return {
    bootIdCurrent: ByteUtils.leToUint32(b.slice(0, 4)),
    // v0.0.1 deliberately leaves index-derived 0xF280 fields zero to avoid a
    // synchronous full-ring walk. Zero therefore means unavailable, not ID 0
    // or a count of zero retained dives.
    bootIdOldest: bootIdOldest || null,
    diveIdLatest: diveIdLatest || null,
    entriesTotalEstimate: entriesTotalEstimate || null,
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

  /** DID 0xF280 -> {telemetry, text} FCB stats (28-byte stride).
   * Index-derived zero fields are returned as null because released v0.0.1
   * deliberately reports them as unavailable. */
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
   *   maxBytes, onProgress, signal)
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
   * Resolve a selection using the same NRC 0x21 polling path as downloadLog,
   * then read the firmware's 0xF281 selector-result DID.
   *
   * @param {Object} [opts]
   * @param {number} [opts.stream=TELEMETRY]
   * @param {(dl:LogDownloader)=>Promise<Uint8Array>} [opts.selector]
   * @param {AbortSignal} [opts.signal]
   * @returns {Promise<Object|null>} Decoded 0xF281 selector result
   */
  async resolveSelection(opts = {}) {
    const stream = opts.stream ?? constants.LOG_STREAM_TELEMETRY;
    if (opts.signal?.aborted) {
      throw new DOMException('Log resolve cancelled', 'AbortError');
    }
    await this._runSelector(opts, stream);
    return await this.readSelectorResult();
  }

  /**
   * Resolve the latest retained dive and expose 0xF281's ID fields with dive
   * semantics. v0.0.1 currently returns zero IDs even after a successful
   * resolve, so latestRetainedDiveId remains null rather than fabricating 0.
   */
  async resolveLatestDive(stream = constants.LOG_STREAM_TELEMETRY, opts = {}) {
    const result = await this.resolveSelection({
      ...opts,
      stream,
      selector: (downloader) => downloader.selectLatestDive(stream)
    });
    if (result === null) return null;
    return {
      ...result,
      latestRetainedDiveId: result.endId || result.startId || null
    };
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

  /** Build a time/byte-throttled progress reporter with a forced terminal update. */
  _progressReporter(opts) {
    const interval = opts.progressIntervalMs ?? LOG_PROGRESS_INTERVAL_MS;
    const minimumBytes = opts.progressMinBytes ?? LOG_PROGRESS_MIN_BYTES;
    const expectedBytes = Number.isFinite(opts.expectedBytes) && opts.expectedBytes > 0
      ? opts.expectedBytes
      : null;
    const startedAt = Date.now();
    let lastAt = startedAt;
    let lastTransferred = 0;
    let smoothedBytesPerSecond = null;
    let reported = false;
    return (progress, force = false) => {
      const now = Date.now();
      if (!force && reported && (now - lastAt) < interval &&
          (progress.transferred - lastTransferred) < minimumBytes) return;
      const sampleMs = now - lastAt;
      const sampleBytes = progress.transferred - lastTransferred;
      if (sampleMs > 0 && sampleBytes >= 0) {
        const sampleBytesPerSecond = sampleBytes * 1000 / sampleMs;
        smoothedBytesPerSecond = smoothedBytesPerSecond === null
          ? sampleBytesPerSecond
          : (smoothedBytesPerSecond * 0.75) + (sampleBytesPerSecond * 0.25);
      }
      const elapsedMs = Math.max(0, now - startedAt);
      const averageBytesPerSecond = elapsedMs > 0
        ? progress.transferred * 1000 / elapsedMs
        : null;
      const rateBytesPerSecond = smoothedBytesPerSecond ?? averageBytesPerSecond;
      const remainingTransferBytes = expectedBytes === null
        ? null
        : Math.max(0, expectedBytes - progress.transferred);
      const enriched = {
        ...progress,
        elapsedMs,
        expectedBytes,
        rateKiBps: rateBytesPerSecond === null ? null : rateBytesPerSecond / 1024,
        averageKiBps: averageBytesPerSecond === null ? null : averageBytesPerSecond / 1024,
        etaSeconds: remainingTransferBytes === null || !(rateBytesPerSecond > 0)
          ? null
          : remainingTransferBytes / rateBytesPerSecond
      };
      lastAt = now;
      lastTransferred = progress.transferred;
      reported = true;
      this.emit('progress', enriched);
      if (opts.onProgress) opts.onProgress(enriched);
      return enriched;
    };
  }

  /** Return DCLG-aligned bytes for a disk attempt's first response block. */
  _firstStoredBody(body) {
    const magic = ByteUtils.uint32ToLE(constants.LOG_DOWNLOAD_MAGIC);
    for (let i = 0; i + magic.length <= body.length; i++) {
      if (body[i] === magic[0] && body[i + 1] === magic[1] &&
          body[i + 2] === magic[2] && body[i + 3] === magic[3]) return body.slice(i);
    }
    throw new LogResumeMismatchError('Retried stream did not begin with a DCLG header', {
      offset: 0, received: body.length
    });
  }

  /** Find a unique byte sequence in the saved record stream using bounded reads. */
  async _findStoredAnchor(store, total, needle) {
    const firstOffset = constants.LOG_DCLG_HEADER_LEN;
    const lastOffset = total - needle.length;
    if (lastOffset < firstOffset) return null;
    let match = null;
    for (let offset = firstOffset; offset <= lastOffset; offset += LOG_RESUME_VERIFY_WINDOW_BYTES) {
      const scanEnd = Math.min(total, offset + LOG_RESUME_VERIFY_WINDOW_BYTES + needle.length - 1);
      const haystack = await store.read(offset, scanEnd - offset);
      const ownedEnd = Math.min(lastOffset + 1, offset + LOG_RESUME_VERIFY_WINDOW_BYTES);
      for (let i = 0; offset + i < ownedEnd && i + needle.length <= haystack.length; i++) {
        if (haystack[i] !== needle[0]) continue;
        let equal = true;
        for (let j = 1; j < needle.length; j++) {
          if (haystack[i + j] !== needle[j]) { equal = false; break; }
        }
        if (!equal) continue;
        const candidate = offset + i;
        if (match !== null) {
          throw new LogResumeMismatchError(
            'Retried ring has an ambiguous overlap with the saved partial log',
            { firstOffset: match, secondOffset: candidate, anchorBytes: needle.length });
        }
        match = candidate;
      }
    }
    return match;
  }

  /**
   * Reconcile the beginning of a restarted ring with an existing partial.
   * Exact snapshots map byte zero to byte zero. If a full ring advanced while
   * the head rebooted, locate a unique bounded anchor from the new oldest
   * record inside the saved record stream. Later bytes are still verified all
   * the way to the saved boundary before any append is allowed.
   */
  async _prepareStoredReplay(store, resumeBytes, firstBody) {
    const exactLength = Math.min(resumeBytes, firstBody.length);
    const savedStart = await store.read(0, exactLength);
    let exact = savedStart.length === exactLength;
    for (let i = 0; exact && i < exactLength; i++) exact = savedStart[i] === firstBody[i];
    if (exact) return { bodyOffset: 0, storedOffset: 0, reconciledFrom: 0 };

    const headerLength = constants.LOG_DCLG_HEADER_LEN;
    const anchorLength = Math.min(LOG_RESUME_ANCHOR_BYTES, firstBody.length - headerLength);
    if (anchorLength < LOG_RESUME_MIN_ANCHOR_BYTES) {
      throw new LogResumeMismatchError(
        'Retried ring changed and the first block is too small to prove a safe overlap',
        { availableAnchorBytes: Math.max(0, anchorLength), minimumAnchorBytes: LOG_RESUME_MIN_ANCHOR_BYTES });
    }

    // Magic/version/flags/stream/reserved must describe the same stream. The
    // two estimate fields may legitimately differ after selector re-resolution.
    const savedHeader = await store.read(0, 8);
    for (let i = 0; i < 8; i++) {
      if (savedHeader[i] !== firstBody[i]) {
        throw new LogResumeMismatchError('Retried data is not the same DCLG stream', {
          offset: i, expected: savedHeader[i], actual: firstBody[i]
        });
      }
    }

    const anchor = firstBody.subarray(headerLength, headerLength + anchorLength);
    const storedOffset = await this._findStoredAnchor(store, resumeBytes, anchor);
    if (storedOffset === null) {
      throw new LogResumeMismatchError(
        'Retried ring cannot be reconciled with the saved partial log',
        { resumeBytes, anchorBytes: anchorLength });
    }
    return { bodyOffset: headerLength, storedOffset, reconciledFrom: storedOffset };
  }

  /**
   * Pull chunks (seq from 1, wrap skipping 0; short chunk = EOS).
   *
   * With no store, bytes use the existing amortised in-memory buffer. With a
   * store, an existing prefix is replayed and verified directly from disk in
   * bounded windows; only bytes beyond that prefix are appended.
   * @private
   */
  async _pullLogChunks(opts, negotiatedBlock, entryCount, io = {}) {
    const store = io.store || null;
    const resumeBytes = store ? (io.resumeBytes || 0) : 0;
    const verifier = store && resumeBytes > 0 ? new StorePrefixVerifier(store, resumeBytes) : null;
    const collectChunkLens = opts.collectChunkLens ?? !store;
    const chunkLens = [];
    const countRecords = store ? null : makeRecordCounter();
    const report = this._progressReporter(opts);
    let lastProgress = null;
    const maxBlocks = opts.maxBlocks ?? Number.POSITIVE_INFINITY;
    const maxBytes = opts.maxBytes ?? LOG_DOWNLOAD_MAX_BYTES;
    let acc = store ? null : new Uint8Array(4096);
    let accLen = 0;
    let saved = resumeBytes;
    let transferred = 0;
    let replayStoredOffset = 0;
    let reconciledFrom = 0;
    let seq = 1;
    let chunks = 0;
    for (let i = 0; ; i++) {
      if (opts.signal?.aborted) {
        throw new DOMException('Log download cancelled', 'AbortError');
      }
      if (i >= maxBlocks) {
        throw new LogDownloadIncompleteError(
          `Log download reached the ${maxBlocks}-block safety limit before end-of-stream`,
          { received: saved, transferred, chunks, maxBlocks, maxBytes });
      }
      const resp = await this.uds.transferData(seq, [], this.timeouts.block);
      const wireBody = resp.slice(2); // strip [0x76, seq]
      let body = wireBody;
      if (store && transferred === 0 && wireBody.length > 0) body = this._firstStoredBody(wireBody);
      if (collectChunkLens) chunkLens.push(body.length);
      chunks++;

      if (store) {
        let bodyOffset = 0;
        if (transferred === 0 && resumeBytes > 0) {
          const replay = await this._prepareStoredReplay(store, resumeBytes, body);
          bodyOffset = replay.bodyOffset;
          replayStoredOffset = replay.storedOffset;
          reconciledFrom = replay.reconciledFrom;
        }
        if (replayStoredOffset < resumeBytes) {
          const verifyLength = Math.min(body.length - bodyOffset, resumeBytes - replayStoredOffset);
          await verifier.verify(body.subarray(bodyOffset, bodyOffset + verifyLength), replayStoredOffset);
          bodyOffset += verifyLength;
          replayStoredOffset += verifyLength;
        }
        if (bodyOffset < body.length) {
          const appendBytes = body.subarray(bodyOffset);
          if (saved + appendBytes.length > maxBytes) {
            throw new LogDownloadIncompleteError(
              `Log download exceeded the ${maxBytes}-byte safety limit before end-of-stream`,
              { received: saved, transferred, chunks, maxBlocks, maxBytes });
          }
          await store.append(appendBytes);
          saved += appendBytes.length;
        }
        transferred += body.length;
      } else {
        if (saved + body.length > maxBytes) {
          throw new LogDownloadIncompleteError(
            `Log download exceeded the ${maxBytes}-byte safety limit before end-of-stream`,
            { received: saved, transferred, chunks, maxBlocks, maxBytes });
        }
        acc = this._ensureCapacity(acc, accLen, body.length);
        acc.set(body, accLen);
        accLen += body.length;
        saved += body.length;
        transferred += body.length;
      }

      const records = countRecords ? countRecords(acc.subarray(0, accLen)) : null;
      const progress = {
        received: saved,
        saved,
        transferred,
        replayed: Math.min(replayStoredOffset, resumeBytes),
        resumeBytes,
        reconciledFrom,
        records,
        entryCount,
        chunks,
        attempt: io.attempt || 1
      };
      const eos = wireBody.length < negotiatedBlock;
      lastProgress = report(progress, eos) || lastProgress;
      seq = (seq + 1) & 0xFF;
      if (seq === 0) seq = 1;
      if (eos) {
        if (store && replayStoredOffset < resumeBytes) {
          throw new LogResumeMismatchError(
            'Retried stream ended before reaching the saved partial-log boundary',
            { expected: resumeBytes, actual: replayStoredOffset, reconciledFrom });
        }
        return {
          acc, accLen, chunkLens, chunks, received: saved, transferred,
          metrics: lastProgress === null ? null : {
            elapsedMs: lastProgress.elapsedMs,
            expectedBytes: lastProgress.expectedBytes,
            rateKiBps: lastProgress.rateKiBps,
            averageKiBps: lastProgress.averageKiBps,
            etaSeconds: lastProgress.etaSeconds
          }
        };
      }
    }
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

  /** Run one selector/begin/0x34/0x36/0x37 attempt. @private */
  async _downloadAttempt(opts, io = {}) {
    const stream = opts.stream ?? constants.LOG_STREAM_TELEMETRY;
    const maxChunk = opts.maxChunk ?? this.maxChunk;

    // 1. Resolve a range.
    await this._runSelector(opts, stream);
    const { selector, entryCount } = await this._readSelectorEstimate();

    // 2. Arm the stream.
    await this.beginStream();

    // 3. RequestDownload with the sentinel addr (LE) + client max_chunk (LE size).
    const negotiatedBlock = await this.uds.requestDownload(
      constants.LOG_DOWNLOAD_SENTINEL_ADDR, maxChunk, { sizeEndian: 'LE' }, this.timeouts.block);

    // 4. Pull until the head sends a short end-of-stream block. Always attempt
    // 0x37, but after a failed pull use a short cleanup timeout so a rebooted
    // head cannot add another 40 seconds to every recovery attempt.
    let pulled;
    let pullError = null;
    try {
      pulled = await this._pullLogChunks(opts, negotiatedBlock, entryCount, io);
    } catch (error) {
      pullError = error;
    }

    try {
      const cleanupTimeout = pullError === null
        ? this.timeouts.block
        : (opts.retry?.cleanupTimeoutMs ?? LOG_RETRY_DEFAULTS.cleanupTimeoutMs);
      await this.uds.requestTransferExit(cleanupTimeout);
    } catch (error) {
      if (pullError === null) throw error;
    }
    if (pullError !== null) throw pullError;

    return { ...pulled, negotiatedBlock, selector };
  }

  _retryableLogError(error) {
    if (error?.name === 'AbortError' || error?.code === 'LOG_DOWNLOAD_INCOMPLETE' ||
        error?.code === 'LOG_RESUME_MISMATCH') return false;
    if (error?.nrc == null) return true;
    // 0x21 busyRepeatRequest is transient; other selector/protocol NRCs mean
    // retrying the identical request cannot safely make progress.
    return error.nrc === constants.NRC_BUSY_REPEAT_REQUEST;
  }

  _retryDelay(ms, signal) {
    if (signal?.aborted) return Promise.reject(new DOMException('Log download cancelled', 'AbortError'));
    return new Promise((resolve, reject) => {
      const onAbort = () => {
        clearTimeout(timer);
        reject(new DOMException('Log download cancelled', 'AbortError'));
      };
      const timer = setTimeout(() => {
        signal?.removeEventListener('abort', onAbort);
        resolve();
      }, ms);
      signal?.addEventListener('abort', onAbort, { once: true });
    });
  }

  /**
   * Download a flash-log stream. Runs the selector, begins the stream, then
   * pulls chunks until a short chunk terminates it. This legacy/in-memory API
   * remains useful for small selective downloads; use downloadLogResumable()
   * for full-ring browser transfers.
   * @param {Object} [opts]
   * @param {number} [opts.stream=TELEMETRY]
   * @param {(dl:LogDownloader)=>Promise<Uint8Array>} [opts.selector] - defaults to latest boot
   * @param {number} [opts.maxChunk] - client max receivable block (overrides ctor)
   * @param {number} [opts.maxBlocks] - Optional explicit block safety ceiling
   * @param {number} [opts.maxBytes=67108864] - Decoded-stream safety ceiling
   * @param {(received:number,total:number)=>void} [opts.onProgress]
   * @param {AbortSignal} [opts.signal]
   * @returns {Promise<{raw:Uint8Array, negotiatedBlock:number, chunkLens:number[], selector:Object|null}>}
   */
  async downloadLog(opts = {}) {
    if (opts.signal?.aborted) {
      throw new DOMException('Log download cancelled', 'AbortError');
    }
    const result = await this._downloadAttempt(opts);
    const { acc, accLen, chunkLens, negotiatedBlock, selector } = result;

    // 6. Anchor on the DCLG magic, trimming any leading garbage.
    const raw = this._trimToMagic(acc.subarray(0, accLen));

    this.emit('done', { raw, negotiatedBlock, chunkLens });
    return { raw, negotiatedBlock, chunkLens, selector };
  }

  /**
   * Stream a log to a resumable store. On a transient failure, restart the
   * selector and verify the retransmitted stream byte-for-byte against the
   * saved prefix before appending. A changed ring raises LOG_RESUME_MISMATCH;
   * incompatible data is never spliced into the partial artifact.
   *
   * @param {Object} opts downloadLog options plus:
   * @param {Object} opts.store LogDownloadStore-compatible sink
   * @param {string} [opts.resumeKey] stable selector identity
   * @param {Object} [opts.retry] maxAttempts/initialDelayMs/maxDelayMs
   */
  async downloadLogResumable(opts = {}) {
    const store = opts.store;
    if (!store) throw new TypeError('downloadLogResumable requires a store');
    if (opts.signal?.aborted) throw new DOMException('Log download cancelled', 'AbortError');

    const retry = { ...LOG_RETRY_DEFAULTS, ...opts.retry };
    const metadata = await store.getMetadata();
    if (metadata.resumeKey && opts.resumeKey && metadata.resumeKey !== opts.resumeKey) {
      throw new LogResumeMismatchError('Saved partial log belongs to a different selector', {
        expected: metadata.resumeKey, actual: opts.resumeKey
      });
    }
    await store.setMetadata({ resumeKey: opts.resumeKey || metadata.resumeKey || null, complete: false });

    let lastError = null;
    for (let attempt = 1; attempt <= retry.maxAttempts; attempt++) {
      const resumeBytes = await store.size();
      await store.beginAttempt();
      try {
        const result = await this._downloadAttempt({ ...opts, retry }, { store, resumeBytes, attempt });
        await store.finishAttempt({
          complete: true,
          attempts: (metadata.attempts || 0) + attempt,
          negotiatedBlock: result.negotiatedBlock,
          transferMetrics: result.metrics,
          lastError: null
        });
        const file = await store.getFile();
        const done = {
          raw: null,
          file,
          store,
          bytes: await store.size(),
          complete: true,
          resumedFrom: resumeBytes,
          attempts: attempt,
          negotiatedBlock: result.negotiatedBlock,
          metrics: result.metrics,
          chunkLens: result.chunkLens,
          selector: result.selector
        };
        this.emit('done', done);
        return done;
      } catch (error) {
        lastError = error;
        await store.finishAttempt({
          complete: false,
          attempts: (metadata.attempts || 0) + attempt,
          lastError: { name: error?.name || 'Error', code: error?.code || null, message: error?.message || String(error) }
        });
        const partial = { store, bytes: await store.size(), complete: false, attempt };
        error.partial = partial;
        const retryable = attempt < retry.maxAttempts && this._retryableLogError(error);
        if (!retryable) throw error;

        const delayMs = Math.min(retry.maxDelayMs, retry.initialDelayMs * (2 ** (attempt - 1)));
        const event = { attempt, nextAttempt: attempt + 1, delayMs, error, partialBytes: partial.bytes };
        this.emit('retry', event);
        if (opts.onRetry) opts.onRetry(event);
        await this._retryDelay(delayMs, opts.signal);
      }
    }
    throw lastError;
  }

  async downloadAllResumable(opts = {}) {
    const stream = opts.stream ?? constants.LOG_STREAM_TELEMETRY;
    return this.downloadLogResumable({
      ...opts,
      stream,
      selector: (downloader) => downloader.selectAll(stream)
    });
  }
}
