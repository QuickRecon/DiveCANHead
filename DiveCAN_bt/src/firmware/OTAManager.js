/**
 * OTA firmware-update orchestration over UDSClient.
 *
 * Drives the Zephyr/MCUBoot pipeline that the Test Rig proves end-to-end
 * (Test Rig/tests/test_dut_ota.py, dut.py:887-1025):
 *
 *   0x10 0x02 (programming session, surface only)
 *   -> 0x34 RequestDownload (size BIG-endian; erases slot1)
 *   -> 0x36 TransferData x N (seq from 1, wrap & 0xFF)
 *   -> 0x37 RequestTransferExit (header-check slot1)
 *   -> 0x31 0x01 0xF001 activate (full SHA-256, boot_request_upgrade(TEST), reboot)
 *
 * The activated image runs UNCONFIRMED and auto-reverts on the next reboot
 * unless the head's POST confirms it, so the UI should poll MCUBoot status
 * (0xF270) after activation. Management DIDs 0xF275-0xF279 cover revert /
 * factory restore / capture / chip-erase / NVS-erase.
 */

import * as constants from '../uds/constants.js';
import { UDSError, ValidationError } from '../errors/ProtocolErrors.js';
import { Logger } from '../utils/Logger.js';
import { parseMcubootImage } from './McubootImage.js';
import {
  decodeMcubootStatus, decodePostStatus, decodeSemVer8
} from './McubootStatus.js';

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
      try { cb(...args); } catch (e) { console.error(`OTA handler ${event}`, e); }
    });
  }
}

/** Default per-step timeouts (ms). Deliberately generous — see dut.py notes. */
export const OTA_TIMEOUTS = {
  session: 8000,   // programming-session entry acks slowly (~2-3 s)
  download: 30000, // 0x34 erases all of slot1 up front
  transfer: 15000, // first blocks lag while the flash-log writer flushes
  exit: 10000,
  activate: 12000,
  management: 8000
};

/**
 * Post-activation confirmation poll cadence. The activated image runs
 * UNCONFIRMED and auto-reverts on the next reboot unless the head's POST
 * confirms it, so we poll MCUBoot status (0xF270) after activation. 30 x 2 s
 * covers the reboot + POST window (reads that fail while the head is rebooting
 * are swallowed and retried).
 */
export const OTA_POLL = {
  attempts: 30,
  interval: 2000
};

/** MCUBoot swap type "Revert" (index into SWAP_TYPE_NAMES) — POST failed, rolled back. */
const MCUBOOT_SWAP_REVERT = 3;

/**
 * Default staging-recovery tuning. Field failures are dominated by the
 * phone<->Petrel BLE link dropping mid-transfer, so staging retries rather
 * than aborting; only a genuine head refusal (NRC with a real cause: diving,
 * image too big, flash failure) or a user cancel aborts immediately.
 *
 * - maxAttempts: staging passes (first try + resumes/restarts).
 * - blockRetries: extra sends of a single 0x36 block after a lost reply.
 * - retryDelayMs: settle time before any retry/resume.
 * - staleDownloadWaitMs: head-side downloads only expire via the 30 s UDS S3
 *   inactivity timeout (an explicit session change does NOT reset the OTA
 *   state machine), so a 0x34 refused with NRC 0x24 means we must go silent
 *   past S3 before retrying. Callers must not poll the head during this wait.
 */
export const OTA_RECOVERY = {
  maxAttempts: 4,
  blockRetries: 2,
  retryDelayMs: 1000,
  staleDownloadWaitMs: 35000
};

export class OTAManager extends EventEmitter {
  /**
   * @param {import('../uds/UDSClient.js').UDSClient} uds
   * @param {Object} [options]
   * @param {number} [options.blockSize] - Cap OTA block below the negotiated max
   * @param {Object} [options.timeouts] - Override OTA_TIMEOUTS
   */
  constructor(uds, options = {}) {
    super();
    this.logger = new Logger('OTA', 'debug');
    this.uds = uds;
    this.options = options;
    this.timeouts = { ...OTA_TIMEOUTS, ...options.timeouts };
    this.recovery = { ...OTA_RECOVERY, ...options.recovery };
  }

  /** Enter the programming session (surface only; NRC 0x22 while diving). */
  async enterProgrammingSession() {
    return await this.uds.enterSession(constants.UDS_SESSION_PROGRAMMING, this.timeouts.session);
  }

  /**
   * Run a call, transparently re-entering the programming session once if the
   * head reports the S3 session lapsed (NRC 0x7F).
   * @private
   */
  async _withSession(fn) {
    try {
      return await fn();
    } catch (err) {
      if (err instanceof UDSError && err.nrc === constants.NRC_SERVICE_NOT_IN_SESSION) {
        this.emit('sessionExpired');
        await this.enterProgrammingSession();
        return await fn();
      }
      throw err;
    }
  }

  /**
   * Stage an image into slot1 (0x34 -> 0x36xN -> 0x37). Does NOT activate.
   *
   * Staging is resilient by default: a lost reply retries the same block
   * (a retry answered with NRC 0x73 means the head already wrote it — the
   * original ack was lost — and counts as delivered); a dropped link calls
   * `opts.reconnect` and resumes the open download at the first unacked
   * block; a head whose OTA state machine reset (NRC 0x24 / 0x7F) triggers
   * a full restart from 0x34. Only a genuine refusal (diving, image too
   * big, flash failure), a user abort, or exhausted attempts propagate.
   *
   * @param {Uint8Array|ArrayBuffer|Array} imageBytes
   * @param {Object} [opts]
   * @param {number} [opts.blockSize] - Cap block payload below the negotiated max
   * @param {(done:number,total:number)=>void} [opts.onProgress]
   * @param {AbortSignal} [opts.signal]
   * @param {(cause:Error)=>Promise<void>} [opts.reconnect] - Restore the
   *   transport after a link drop; must no-op when still connected
   * @param {Object} [opts.recovery] - Override OTA_RECOVERY fields
   * @returns {Promise<{blocks:number, block:number, negotiatedBlock:number, image:Object}>}
   */
  async stageImage(imageBytes, opts = {}) {
    const bytes = imageBytes instanceof Uint8Array ? imageBytes : new Uint8Array(imageBytes);
    const image = parseMcubootImage(bytes);
    if (!image.valid) {
      throw new ValidationError(`Not a valid MCUBoot image: ${image.reason}`, 'OTA', { image });
    }

    const recovery = { ...this.recovery, ...opts.recovery };
    // Download progress shared across attempts. `active` means the head has
    // an open download whose blocks up to `acked` are confirmed written, so
    // a later attempt can resume instead of re-erasing and starting over.
    const progress = { negotiated: 0, block: 0, total: 0, acked: 0, active: false };

    let attempt = 1;
    for (;;) {
      this._throwIfAborted(opts);
      try {
        return await this._stageAttempt(bytes, image, opts, recovery, progress);
      } catch (err) {
        const action = this._classifyStagingError(err);
        if (action === 'abort' || attempt >= recovery.maxAttempts) {
          throw err;
        }
        if (action === 'restart') {
          // Head-side download state is gone — resume is impossible.
          progress.active = false;
          progress.acked = 0;
        }
        attempt += 1;
        const mode = progress.active ? `resuming at block ${progress.acked + 1}` : 'restarting from 0x34';
        this.logger.warn(`Staging interrupted (${err.message}); ${mode} — attempt ${attempt}/${recovery.maxAttempts}`);
        this.emit('stagingRetry', {
          attempt, maxAttempts: recovery.maxAttempts, resume: progress.active, error: err
        });
        await this._recoverLink(opts, recovery, err);
      }
    }
  }

  /**
   * One staging pass. Starts a fresh download (0x34) unless `progress.active`
   * says the head still holds an open one, in which case transfers resume at
   * the first unacked block.
   * @private
   */
  async _stageAttempt(bytes, image, opts, recovery, progress) {
    if (!progress.active) {
      const negotiated = await this._requestDownloadRecovered(bytes.length, recovery);

      const cap = opts.blockSize ?? this.options.blockSize ?? negotiated;
      const block = Math.min(cap, negotiated) - constants.OTA_REQ_OVERHEAD;
      if (block <= 0) {
        throw new UDSError(`Bad negotiated block size ${negotiated}`, constants.SID_REQUEST_DOWNLOAD);
      }

      progress.negotiated = negotiated;
      progress.block = block;
      progress.total = Math.ceil(bytes.length / block);
      progress.acked = 0;
      progress.active = true;
      this.logger.info(`Staging ${bytes.length} bytes as ${progress.total} blocks of ${block} (negotiated ${negotiated})`);
    } else {
      this.logger.info(`Resuming open download at block ${progress.acked + 1}/${progress.total}`);
    }

    for (let idx = progress.acked; idx < progress.total; idx++) {
      this._throwIfAborted(opts);
      const off = idx * progress.block;
      // Block index 0 is seq 1; the counter wraps modulo 256 (ISO 14229).
      const seq = (idx + 1) & 0xFF;
      await this._transferBlockRetried(seq, bytes.slice(off, off + progress.block),
        idx, progress, opts, recovery);
      progress.acked = idx + 1;
      this.emit('progress', {
        done: progress.acked, total: progress.total,
        percent: (progress.acked / progress.total) * 100
      });
      if (opts.onProgress) opts.onProgress(progress.acked, progress.total);
    }

    await this.uds.requestTransferExit(this.timeouts.exit);
    progress.active = false;
    this.logger.info(`Staged ${progress.total} blocks; transfer exit accepted`);
    this.emit('staged', { blocks: progress.total, image });
    return {
      blocks: progress.total, block: progress.block,
      negotiatedBlock: progress.negotiated, image
    };
  }

  /**
   * Send one 0x36 block, retrying on lost replies / transport hiccups.
   *
   * NRC 0x73 (wrong block sequence) on a RETRY means the original send was
   * written and only its ack was lost — the head has already advanced its
   * counter — so it is treated as delivered. On the first send it is a real
   * desync and propagates.
   * @private
   */
  async _transferBlockRetried(seq, chunk, idx, progress, opts, recovery) {
    let retried = false;
    let retriesLeft = recovery.blockRetries;
    for (;;) {
      try {
        await this.uds.transferData(seq, chunk, this.timeouts.transfer);
        return;
      } catch (err) {
        if (retried && err instanceof UDSError && err.nrc === constants.NRC_WRONG_BLOCK_SEQUENCE) {
          this.logger.info(`Block ${idx + 1}/${progress.total}: already written head-side (0x73 after retry) — continuing`);
          return;
        }
        this._throwIfAborted(opts);
        if (!this._isTransientError(err) || retriesLeft <= 0) {
          throw err;
        }
        retriesLeft -= 1;
        retried = true;
        this.logger.warn(`Block ${idx + 1}/${progress.total} failed (${err.message}); retrying, ${retriesLeft} retr${retriesLeft === 1 ? 'y' : 'ies'} left`);
        this.emit('blockRetry', { block: idx + 1, total: progress.total, seq, error: err });
        await this._recoverLink(opts, recovery, err);
      }
    }
  }

  /**
   * RequestDownload with stale-download recovery. NRC 0x24 here means a
   * previous download (ours or an earlier tool's) is still open head-side;
   * the only way it clears is the 30 s S3 inactivity timeout, so go silent
   * past it and retry once. The caller must not poll the head meanwhile.
   * @private
   */
  async _requestDownloadRecovered(length, recovery) {
    const request = () => this._withSession(() =>
      this.uds.requestDownload(0, length, { sizeEndian: 'BE' }, this.timeouts.download));
    try {
      return await request();
    } catch (err) {
      if (!(err instanceof UDSError) || err.nrc !== constants.NRC_REQUEST_SEQUENCE_ERROR) {
        throw err;
      }
      const waitS = Math.ceil(recovery.staleDownloadWaitMs / 1000);
      this.logger.warn(`Head has a stale download open; staying silent ${waitS}s so its session expires`);
      this.emit('staleDownload', { waitMs: recovery.staleDownloadWaitMs });
      await this._delay(recovery.staleDownloadWaitMs);
      return await request();
    }
  }

  /**
   * Decide how a staging failure is handled:
   * - 'resume': transport-level loss (timeout, disconnect, send failure) —
   *   the head likely still holds the download; reconnect and continue.
   * - 'restart': the head's OTA state machine reset (0x24 sequence error,
   *   0x7F session lapse) or asks for a retry (0x21 busy) — start over
   *   from 0x34.
   * - 'abort': user abort, invalid image, or a genuine head refusal
   *   (0x22 diving, 0x31 out of range, 0x72 flash failure, ...).
   *
   * Anything unrecognised counts as transport-shaped and resumes: retries
   * are bounded and logged, so retrying an unknown error is cheap, while
   * aborting a recoverable one strands the user (the exact field failure
   * this ladder exists to fix).
   * @private
   */
  _classifyStagingError(err) {
    let action = 'resume';
    if (err?.details?.aborted || err instanceof ValidationError) {
      action = 'abort';
    } else if (err instanceof UDSError && err.nrc !== null && err.nrc !== undefined) {
      const restartNrcs = [
        constants.NRC_REQUEST_SEQUENCE_ERROR,
        constants.NRC_SERVICE_NOT_IN_SESSION,
        constants.NRC_BUSY_REPEAT_REQUEST
      ];
      action = restartNrcs.includes(err.nrc) ? 'restart' : 'abort';
    }
    return action;
  }

  /**
   * True for failures worth re-sending the same block over: lost replies
   * (UDS timeout), send failures, and disconnect aborts all surface as a
   * UDSError with no NRC — or as a non-UDS transport error. A real negative
   * response is never transient here (0x73-on-retry is special-cased by the
   * caller).
   * @private
   */
  _isTransientError(err) {
    if (err instanceof ValidationError || err?.details?.aborted) {
      return false;
    }
    if (err instanceof UDSError) {
      return err.nrc === null || err.nrc === undefined;
    }
    return true;
  }

  /**
   * Settle, then hand the transport back to the app for reconnection if a
   * callback was provided. The callback must no-op when still connected.
   * @private
   */
  async _recoverLink(opts, recovery, cause) {
    await this._delay(recovery.retryDelayMs);
    if (opts.reconnect) {
      await opts.reconnect(cause);
    }
  }

  /** @private */
  _throwIfAborted(opts) {
    if (opts.signal?.aborted) {
      throw new ValidationError('OTA staging aborted', 'OTA', { aborted: true });
    }
  }

  /** @private */
  _delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }

  /**
   * Activate the staged image (0x31 0x01 0xF001). On success the head validates
   * slot1's SHA-256, stages a TEST swap and reboots ~200 ms later — so the 0x71
   * reply is frequently lost. A genuine NRC (e.g. 0x22 SHA mismatch) is a real
   * refusal and is thrown; a lost reply / timeout is reported inconclusive so
   * the caller verifies by effect (reconnect + poll version/status).
   * @returns {Promise<{rebooting:boolean, inconclusive:boolean, response:Uint8Array|null}>}
   */
  async activate() {
    try {
      const response = await this._withSession(() =>
        this.uds.routineControl(constants.OTA_RID_ACTIVATE, [], this.timeouts.activate));
      return { rebooting: true, inconclusive: false, response };
    } catch (err) {
      if (err instanceof UDSError && err.nrc !== null && err.nrc !== undefined) {
        throw err; // genuine refusal (SHA mismatch, wrong session, dive, ...)
      }
      // No reply — the head very likely swapped and rebooted.
      return { rebooting: true, inconclusive: true, response: null };
    }
  }

  /** Stage then activate in one call. */
  async stageAndActivate(imageBytes, opts = {}) {
    const staged = await this.stageImage(imageBytes, opts);
    const activated = await this.activate();
    return { ...staged, ...activated };
  }

  /**
   * Run the whole OTA pipeline back-to-back with no gaps in which the head's
   * session or confirmation window could lapse: enter programming session ->
   * stage -> activate -> poll MCUBoot status until the new image confirms,
   * reverts, or the poll window elapses.
   *
   * Emits a `'phase'` event ({phase, ...}) and calls `opts.onPhase(phase, detail)`
   * as each step begins/ends, so a UI can narrate progress from one call:
   *   'session' -> 'staging' -> 'activating' -> 'polling' ->
   *   ('confirmed' | 'reverted' | 'timeout')
   * Staging progress still flows through the `'progress'` event / `opts.onProgress`.
   *
   * A genuine NRC from any phase (e.g. 0x22 while diving, or SHA mismatch on
   * activate) propagates as a thrown UDSError. `opts.signal` aborts the staging
   * phase and is re-checked before the irreversible activate.
   *
   * @param {Uint8Array|ArrayBuffer|Array} imageBytes
   * @param {Object} [opts]
   * @param {number} [opts.blockSize]
   * @param {(done:number,total:number)=>void} [opts.onProgress]
   * @param {(phase:string,detail:Object)=>void} [opts.onPhase]
   * @param {AbortSignal} [opts.signal]
   * @param {(cause:Error)=>Promise<void>} [opts.reconnect] - Passed to stageImage
   * @param {Object} [opts.recovery] - Passed to stageImage (OTA_RECOVERY overrides)
   * @param {number} [opts.pollAttempts]
   * @param {number} [opts.pollInterval]
   * @returns {Promise<Object>} staged/activated fields plus
   *   {confirmed, reverted, timedOut, status}
   */
  async updateFirmware(imageBytes, opts = {}) {
    const emitPhase = (phase, detail = {}) => {
      this.emit('phase', { phase, ...detail });
      if (opts.onPhase) opts.onPhase(phase, detail);
    };

    emitPhase('session');
    await this.enterProgrammingSession();

    emitPhase('staging');
    const staged = await this.stageImage(imageBytes, {
      blockSize: opts.blockSize,
      onProgress: opts.onProgress,
      signal: opts.signal,
      reconnect: opts.reconnect,
      recovery: opts.recovery
    });

    if (opts.signal?.aborted) {
      throw new ValidationError('OTA update aborted before activation', 'OTA', { aborted: true });
    }

    emitPhase('activating');
    const activated = await this.activate();

    emitPhase('polling');
    const attempts = opts.pollAttempts ?? OTA_POLL.attempts;
    const interval = opts.pollInterval ?? OTA_POLL.interval;
    let status = null;
    let outcome = 'timeout';
    for (let i = 0; i < attempts; i++) {
      try {
        status = await this.readMcubootStatus();
        if (status?.confirmed) { outcome = 'confirmed'; break; }
        if (status?.swapType === MCUBOOT_SWAP_REVERT) { outcome = 'reverted'; break; }
      } catch {
        // Head may be mid-reboot — tolerate transient read failures and retry.
      }
      if (i < attempts - 1) {
        await new Promise(resolve => setTimeout(resolve, interval));
      }
    }

    emitPhase(outcome, { status });
    return {
      ...staged,
      ...activated,
      confirmed: outcome === 'confirmed',
      reverted: outcome === 'reverted',
      timedOut: outcome === 'timeout',
      status
    };
  }

  // -------- MCUBoot / OTA introspection --------

  async readMcubootStatus() {
    return decodeMcubootStatus(await this.uds.readDataByIdentifier(constants.DID_MCUBOOT_STATUS));
  }

  async readPostStatus() {
    return decodePostStatus(await this.uds.readDataByIdentifier(constants.DID_POST_STATUS));
  }

  async readSlot0Version() {
    return decodeSemVer8(await this.uds.readDataByIdentifier(constants.DID_SLOT0_VERSION));
  }

  async readSlot1Version() {
    return decodeSemVer8(await this.uds.readDataByIdentifier(constants.DID_SLOT1_VERSION));
  }

  async readFactoryVersion() {
    return decodeSemVer8(await this.uds.readDataByIdentifier(constants.DID_FACTORY_VERSION));
  }

  // -------- Management writes (programming session, surface only) --------

  /**
   * Write a management DID with the required magic byte, retrying once if the
   * session lapsed. Returns {acked, inconclusive}; a lost reply after a reboot
   * is reported inconclusive rather than thrown.
   * @private
   */
  async _managementWrite(did, magic = constants.OTA_MANAGEMENT_MAGIC) {
    try {
      await this._withSession(() =>
        this.uds.writeDataByIdentifier(did, [magic]));
      return { acked: true, inconclusive: false };
    } catch (err) {
      if (err instanceof UDSError && err.nrc !== null && err.nrc !== undefined) {
        throw err;
      }
      return { acked: false, inconclusive: true };
    }
  }

  /** 0xF275: re-stage slot1 + reboot (1-step rollback). */
  forceRevert() { return this._managementWrite(constants.DID_FORCE_REVERT); }

  /** 0xF276: copy factory backup into slot1 + reboot. */
  restoreFactory() { return this._managementWrite(constants.DID_RESTORE_FACTORY); }

  /** 0xF277: bless the running image as the factory baseline. */
  factoryCapture() { return this._managementWrite(constants.DID_FACTORY_CAPTURE); }

  /** 0xF278: chip-erase the whole external NOR + reboot (DESTRUCTIVE, multi-minute). */
  chipEraseNor() { return this._managementWrite(constants.DID_FACTORY_FLASH_ERASE); }

  /** 0xF279: erase the settings/cal NVS partition + reboot. */
  nvsErase() { return this._managementWrite(constants.DID_NVS_ERASE); }
}
