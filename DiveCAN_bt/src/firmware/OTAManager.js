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
import { ByteUtils } from '../utils/ByteUtils.js';
import { parseMcubootImage } from './McubootImage.js';
import {
  decodeMcubootStatus, decodePostStatus, decodeSemVer8
} from './McubootStatus.js';

class EventEmitter {
  constructor() { this.events = {}; }
  on(event, cb) { (this.events[event] ||= []).push(cb); return this; }
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

export class OTAManager extends EventEmitter {
  /**
   * @param {import('../uds/UDSClient.js').UDSClient} uds
   * @param {Object} [options]
   * @param {number} [options.blockSize] - Cap OTA block below the negotiated max
   * @param {Object} [options.timeouts] - Override OTA_TIMEOUTS
   */
  constructor(uds, options = {}) {
    super();
    this.uds = uds;
    this.options = options;
    this.timeouts = { ...OTA_TIMEOUTS, ...(options.timeouts || {}) };
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
   * @param {Uint8Array|ArrayBuffer|Array} imageBytes
   * @param {Object} [opts]
   * @param {number} [opts.blockSize]
   * @param {(done:number,total:number)=>void} [opts.onProgress]
   * @param {AbortSignal} [opts.signal]
   * @returns {Promise<{blocks:number, block:number, negotiatedBlock:number, image:Object}>}
   */
  async stageImage(imageBytes, opts = {}) {
    const bytes = imageBytes instanceof Uint8Array ? imageBytes : new Uint8Array(imageBytes);
    const image = parseMcubootImage(bytes);
    if (!image.valid) {
      throw new ValidationError(`Not a valid MCUBoot image: ${image.reason}`, 'OTA', { image });
    }

    const negotiated = await this._withSession(() =>
      this.uds.requestDownload(0, bytes.length, { sizeEndian: 'BE' }, this.timeouts.download));

    const cap = opts.blockSize ?? this.options.blockSize ?? negotiated;
    const block = Math.min(cap, negotiated) - constants.OTA_REQ_OVERHEAD;
    if (block <= 0) {
      throw new UDSError(`Bad negotiated block size ${negotiated}`, constants.SID_REQUEST_DOWNLOAD);
    }

    const total = Math.ceil(bytes.length / block);
    let seq = 1;
    let done = 0;
    for (let off = 0; off < bytes.length; off += block) {
      if (opts.signal?.aborted) {
        throw new ValidationError('OTA staging aborted', 'OTA', { aborted: true });
      }
      await this.uds.transferData(seq, bytes.slice(off, off + block), this.timeouts.transfer);
      done += 1;
      this.emit('progress', { done, total, percent: (done / total) * 100 });
      if (opts.onProgress) opts.onProgress(done, total);
      seq = (seq + 1) & 0xFF;
    }

    await this.uds.requestTransferExit(this.timeouts.exit);
    this.emit('staged', { blocks: total, image });
    return { blocks: total, block, negotiatedBlock: negotiated, image };
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
