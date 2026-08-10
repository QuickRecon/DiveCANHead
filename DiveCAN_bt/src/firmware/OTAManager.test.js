import { describe, it, expect, beforeEach } from 'vitest';
import { OTAManager } from './OTAManager.js';
import { UDSClient } from '../uds/UDSClient.js';
import { MockTransport } from '../../tests/mocks/MockTransport.js';
import { buildMcubootImage } from '../../tests/fixtures/mcuboot-image.js';
import {
  buildSessionResponse, buildRequestDownloadResponse, buildTransferResponse,
  buildTransferExitResponse, buildRoutineResponse, buildWDBIResponse,
  buildNegativeResponse, buildRDBIResponse
} from '../../tests/fixtures/uds-responses.js';

/**
 * Build a scripted OTA responder. `overrides` maps a SID to a function
 * (request, index) => responseBytes|null to inject faults.
 */
function otaResponder({ maxBlock = 64, overrides = {} } = {}) {
  return (req) => {
    const sid = req[0];
    if (overrides[sid]) return overrides[sid](req);
    switch (sid) {
      case 0x10: return buildSessionResponse(req[1]);
      case 0x34: return buildRequestDownloadResponse(maxBlock);
      case 0x36: return buildTransferResponse(req[1]);
      case 0x37: return buildTransferExitResponse();
      case 0x31: return buildRoutineResponse((req[2] << 8) | req[3]);
      case 0x2E: return buildWDBIResponse((req[1] << 8) | req[2]);
      default: return null;
    }
  };
}

describe('OTAManager', () => {
  let transport;
  let uds;
  let ota;

  beforeEach(() => {
    transport = new MockTransport();
    uds = new UDSClient(transport);
    ota = new OTAManager(uds);
  });

  it('stages an image: 0x34 -> 0x36xN -> 0x37 with correct block sizing', async () => {
    transport.setResponder(otaResponder({ maxBlock: 64 }));
    const image = buildMcubootImage({ imageSize: 200, trailerSize: 0, headerSize: 32 }); // 232 bytes
    const progress = [];
    const result = await ota.stageImage(image, { onProgress: (d, t) => progress.push([d, t]) });

    // block = 64 - 3 overhead = 61; ceil(232/61) = 4
    expect(result.block).toBe(61);
    expect(result.blocks).toBe(4);
    expect(progress[progress.length - 1]).toEqual([4, 4]);

    const sent = transport.getAllSent().map(a => Array.from(a));
    // First frame is the RequestDownload with size big-endian (232 = 0x000000E8)
    const dl = sent.find(s => s[0] === 0x34);
    expect(dl.slice(7)).toEqual([0x00, 0x00, 0x00, 0xE8]);
    // Transfer sequence counters 1..4
    const seqs = sent.filter(s => s[0] === 0x36).map(s => s[1]);
    expect(seqs).toEqual([1, 2, 3, 4]);
    // Ends with RequestTransferExit
    expect(sent.some(s => s[0] === 0x37)).toBe(true);
  });

  it('rejects a wrong block sequence (NRC 0x73)', async () => {
    transport.setResponder(otaResponder({
      overrides: { 0x36: () => buildNegativeResponse(0x36, 0x73) }
    }));
    const image = buildMcubootImage({ imageSize: 100 });
    await expect(ota.stageImage(image)).rejects.toMatchObject({ nrc: 0x73 });
  });

  it('rejects an oversize image (NRC 0x31)', async () => {
    transport.setResponder(otaResponder({
      overrides: { 0x34: () => buildNegativeResponse(0x34, 0x31) }
    }));
    const image = buildMcubootImage({ imageSize: 100 });
    await expect(ota.stageImage(image)).rejects.toMatchObject({ nrc: 0x31 });
  });

  it('refuses to stage a non-MCUBoot file', async () => {
    transport.setResponder(otaResponder());
    await expect(ota.stageImage(new Uint8Array(64))).rejects.toThrow(/MCUBoot/);
  });

  it('activate rethrows a genuine NRC (0x22 SHA mismatch)', async () => {
    transport.setResponder(otaResponder({
      overrides: { 0x31: () => buildNegativeResponse(0x31, 0x22) }
    }));
    await expect(ota.activate()).rejects.toMatchObject({ nrc: 0x22 });
  });

  it('activate treats a lost reply as inconclusive (head rebooted)', async () => {
    transport.setResponder(otaResponder({
      overrides: { 0x31: () => null } // no reply
    }));
    ota.timeouts.activate = 50; // fail fast for the test
    const res = await ota.activate();
    expect(res.rebooting).toBe(true);
    expect(res.inconclusive).toBe(true);
  });

  it('management write surfaces persistent session gating (NRC 0x7F)', async () => {
    transport.setResponder(otaResponder({
      overrides: { 0x2E: () => buildNegativeResponse(0x2E, 0x7F) }
    }));
    // _withSession re-enters the session once, then the retry still 0x7F -> throws
    await expect(ota.forceRevert()).rejects.toMatchObject({ nrc: 0x7F });
  });

  it('decodes MCUBoot status', async () => {
    transport.setResponder((req) => {
      // read 0xF270 -> 16-byte status
      return new Uint8Array([0x62, 0xF2, 0x70,
        1, 1, 0, 1, 1, 2, 3, 0, 0xFF, 0xFF, 0xFF, 0xFF, 2, 0, 0, 0]);
    });
    const status = await ota.readMcubootStatus();
    expect(status.swapTypeName).toBe('Test');
    expect(status.confirmed).toBe(true);
    expect(status.slot0Version).toEqual({ major: 1, minor: 2, revision: 3 });
    expect(status.slot1Version).toBeNull();
  });

  // 16-byte 0xF270 status: [swapType, confirmed, runningSlot, flags, slot0(4), slot1(4), factory(4)]
  const mcubootStatusBytes = (swapType, confirmed) => [
    swapType, confirmed, 0, 0,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  ];

  it('updateFirmware runs the full pipeline and returns confirmed', async () => {
    let polls = 0;
    const base = otaResponder({ maxBlock: 64 });
    transport.setResponder((req) => {
      if (req[0] === 0x22 && req[1] === 0xF2 && req[2] === 0x70) {
        polls += 1;
        // Unconfirmed on the first poll, confirmed thereafter (POST passed).
        return buildRDBIResponse(0xF270, mcubootStatusBytes(0, polls > 1 ? 1 : 0));
      }
      return base(req);
    });
    const image = buildMcubootImage({ imageSize: 100 });
    const phases = [];
    const result = await ota.updateFirmware(image, {
      onPhase: (p) => phases.push(p),
      pollInterval: 1
    });

    expect(result.confirmed).toBe(true);
    expect(result.reverted).toBe(false);
    expect(result.timedOut).toBe(false);
    expect(phases).toEqual(['session', 'staging', 'activating', 'polling', 'confirmed']);
    expect(polls).toBeGreaterThanOrEqual(2);

    const sent = transport.getAllSent().map(a => Array.from(a));
    expect(sent.some(s => s[0] === 0x10 && s[1] === 0x02)).toBe(true); // programming session
    expect(sent.some(s => s[0] === 0x34)).toBe(true);                  // request download
    expect(sent.some(s => s[0] === 0x31)).toBe(true);                  // activate
  });

  it('updateFirmware reports a revert when POST fails', async () => {
    const base = otaResponder({ maxBlock: 64 });
    transport.setResponder((req) => {
      if (req[0] === 0x22 && req[1] === 0xF2 && req[2] === 0x70) {
        return buildRDBIResponse(0xF270, mcubootStatusBytes(3, 0)); // swapType 3 = Revert
      }
      return base(req);
    });
    const image = buildMcubootImage({ imageSize: 100 });
    const result = await ota.updateFirmware(image, { pollInterval: 1 });

    expect(result.reverted).toBe(true);
    expect(result.confirmed).toBe(false);
  });

  it('updateFirmware times out if the image never confirms', async () => {
    const base = otaResponder({ maxBlock: 64 });
    transport.setResponder((req) => {
      if (req[0] === 0x22 && req[1] === 0xF2 && req[2] === 0x70) {
        return buildRDBIResponse(0xF270, mcubootStatusBytes(1, 0)); // stays unconfirmed
      }
      return base(req);
    });
    const image = buildMcubootImage({ imageSize: 100 });
    const result = await ota.updateFirmware(image, { pollAttempts: 3, pollInterval: 1 });

    expect(result.timedOut).toBe(true);
    expect(result.confirmed).toBe(false);
    expect(result.reverted).toBe(false);
  });

  it('updateFirmware aborts before activation when signaled', async () => {
    transport.setResponder(otaResponder({ maxBlock: 64 }));
    const image = buildMcubootImage({ imageSize: 100 });
    const controller = new AbortController();
    controller.abort();

    await expect(ota.updateFirmware(image, { signal: controller.signal }))
      .rejects.toMatchObject({ details: { aborted: true } });

    const sent = transport.getAllSent().map(a => Array.from(a));
    expect(sent.some(s => s[0] === 0x31)).toBe(false); // never activated
  });

  it('updateFirmware propagates a genuine NRC (0x22 SHA mismatch) from activate', async () => {
    transport.setResponder(otaResponder({
      overrides: { 0x31: () => buildNegativeResponse(0x31, 0x22) }
    }));
    const image = buildMcubootImage({ imageSize: 100 });
    await expect(ota.updateFirmware(image, { pollInterval: 1 }))
      .rejects.toMatchObject({ nrc: 0x22 });
  });

  describe('staging recovery', () => {
    /** Fast recovery tuning so tests never sit in real backoffs. */
    const FAST = { retryDelayMs: 1, staleDownloadWaitMs: 5 };

    beforeEach(() => {
      ota = new OTAManager(uds, { recovery: FAST });
      ota.timeouts.transfer = 40; // lost replies time out quickly
    });

    /** Count sent requests by SID. */
    const sids = () => transport.getAllSent().map(a => a[0]);

    it('retries a block whose reply was lost and completes', async () => {
      let dropped = false;
      transport.setResponder((req) => {
        if (req[0] === 0x36 && req[1] === 2 && !dropped) {
          dropped = true;
          return null; // swallow the reply once
        }
        return otaResponder({ maxBlock: 64 })(req);
      });
      const image = buildMcubootImage({ imageSize: 200, trailerSize: 0, headerSize: 32 });
      const result = await ota.stageImage(image);

      expect(result.blocks).toBe(4);
      // Block 2 was sent twice; only one 0x34 (no restart, no re-erase)
      const seqs = transport.getAllSent().filter(a => a[0] === 0x36).map(a => a[1]);
      expect(seqs).toEqual([1, 2, 2, 3, 4]);
      expect(sids().filter(s => s === 0x34)).toHaveLength(1);
    });

    it('treats NRC 0x73 on a retried block as delivered (lost ack)', async () => {
      let sends = 0;
      transport.setResponder((req) => {
        if (req[0] === 0x36 && req[1] === 2) {
          sends += 1;
          if (sends === 1) return null;                       // ack lost
          if (sends === 2) return buildNegativeResponse(0x36, 0x73); // head already advanced
        }
        return otaResponder({ maxBlock: 64 })(req);
      });
      const image = buildMcubootImage({ imageSize: 200, trailerSize: 0, headerSize: 32 });
      const result = await ota.stageImage(image);

      expect(result.blocks).toBe(4);
      // All four blocks acked exactly once from the head's perspective
      const seqs = transport.getAllSent().filter(a => a[0] === 0x36).map(a => a[1]);
      expect(seqs).toEqual([1, 2, 2, 3, 4]);
    });

    it('drains a late block ack before retrying so it cannot poison the next block', async () => {
      ota.timeouts.transfer = 10;
      let seq1Sends = 0;
      transport.setResponder((req) => {
        if (req[0] === 0x36 && req[1] === 1) {
          seq1Sends += 1;
          if (seq1Sends === 1) {
            setTimeout(() => transport.injectMessage(buildTransferResponse(1)), 15);
            return null;
          }
          return buildNegativeResponse(0x36, 0x73);
        }
        return otaResponder({ maxBlock: 64 })(req);
      });

      const image = buildMcubootImage({ imageSize: 100 });
      const result = await ota.stageImage(image, {
        recovery: { ...FAST, retryDelayMs: 10 }
      });

      expect(result.blocks).toBe(3);
      const seqs = transport.getAllSent().filter(a => a[0] === 0x36).map(a => a[1]);
      expect(seqs).toEqual([1, 1, 2, 3]);
    });

    it('resumes an open download after transport loss without re-erasing', async () => {
      let dropped = false;
      const reconnect = [];
      transport.setResponder((req) => {
        if (req[0] === 0x36 && req[1] === 2 && !dropped) {
          dropped = true;
          return null;
        }
        return otaResponder({ maxBlock: 64 })(req);
      });
      // blockRetries 0: the lost reply escalates straight to the outer
      // resume path instead of being absorbed by the block-level retry.
      const image = buildMcubootImage({ imageSize: 200, trailerSize: 0, headerSize: 32 });
      const retryEvents = [];
      ota.on('stagingRetry', (e) => retryEvents.push(e));
      const result = await ota.stageImage(image, {
        recovery: { ...FAST, blockRetries: 0 },
        reconnect: async (cause) => { reconnect.push(cause); }
      });

      expect(result.blocks).toBe(4);
      expect(reconnect).toHaveLength(1);
      expect(retryEvents).toEqual([expect.objectContaining({ resume: true })]);
      // Resume continues the same download: exactly one 0x34
      expect(sids().filter(s => s === 0x34)).toHaveLength(1);
      const seqs = transport.getAllSent().filter(a => a[0] === 0x36).map(a => a[1]);
      expect(seqs).toEqual([1, 2, 2, 3, 4]);
    });

    it('restarts from 0x34 when the head lost the download (NRC 0x24)', async () => {
      let rejected = false;
      transport.setResponder((req) => {
        if (req[0] === 0x36 && req[1] === 2 && !rejected) {
          rejected = true;
          return buildNegativeResponse(0x36, 0x24); // head OTA SM back in IDLE
        }
        return otaResponder({ maxBlock: 64 })(req);
      });
      const image = buildMcubootImage({ imageSize: 200, trailerSize: 0, headerSize: 32 });
      const retryEvents = [];
      ota.on('stagingRetry', (e) => retryEvents.push(e));
      const result = await ota.stageImage(image);

      expect(result.blocks).toBe(4);
      expect(retryEvents).toEqual([expect.objectContaining({ resume: false })]);
      // Full restart: two 0x34s, and the block sequence starts over
      expect(sids().filter(s => s === 0x34)).toHaveLength(2);
      const seqs = transport.getAllSent().filter(a => a[0] === 0x36).map(a => a[1]);
      expect(seqs).toEqual([1, 2, 1, 2, 3, 4]);
    });

    it('waits out a stale head-side download (0x34 refused with NRC 0x24)', async () => {
      let refusals = 0;
      transport.setResponder((req) => {
        if (req[0] === 0x34 && refusals === 0) {
          refusals += 1;
          return buildNegativeResponse(0x34, 0x24); // earlier download still open
        }
        return otaResponder({ maxBlock: 64 })(req);
      });
      const image = buildMcubootImage({ imageSize: 100 });
      const stale = [];
      ota.on('staleDownload', (e) => stale.push(e));
      const result = await ota.stageImage(image);

      expect(result.blocks).toBeGreaterThan(0);
      expect(stale).toHaveLength(1);
      expect(sids().filter(s => s === 0x34)).toHaveLength(2);
    });

    it('cancels immediately during the stale-download silence window', async () => {
      const controller = new AbortController();
      transport.setResponder((req) => {
        if (req[0] === 0x34) return buildNegativeResponse(0x34, 0x24);
        return otaResponder({ maxBlock: 64 })(req);
      });
      ota.on('staleDownload', () => controller.abort());

      await expect(ota.stageImage(buildMcubootImage({ imageSize: 100 }), {
        signal: controller.signal,
        recovery: { ...FAST, staleDownloadWaitMs: 1000 }
      })).rejects.toMatchObject({ details: { aborted: true } });

      expect(sids().filter(s => s === 0x34)).toHaveLength(1);
    });

    it('gives up after maxAttempts and surfaces the last error', async () => {
      transport.setResponder((req) => {
        if (req[0] === 0x36) return null; // every block reply lost
        return otaResponder({ maxBlock: 64 })(req);
      });
      const image = buildMcubootImage({ imageSize: 100 });
      await expect(ota.stageImage(image, {
        recovery: { ...FAST, blockRetries: 0, maxAttempts: 2 }
      })).rejects.toMatchObject({ details: { timeout: 40 } });

      // Two staging passes, both stuck on block 1
      const seqs = transport.getAllSent().filter(a => a[0] === 0x36).map(a => a[1]);
      expect(seqs).toEqual([1, 1]);
    });

    it('a user abort cuts through the retry ladder immediately', async () => {
      const controller = new AbortController();
      transport.setResponder((req) => {
        if (req[0] === 0x36) {
          controller.abort();  // cancel while the block is in flight...
          return null;         // ...and lose the reply so a retry would follow
        }
        return otaResponder({ maxBlock: 64 })(req);
      });
      const image = buildMcubootImage({ imageSize: 100 });
      await expect(ota.stageImage(image, { signal: controller.signal }))
        .rejects.toMatchObject({ details: { aborted: true } });

      const seqs = transport.getAllSent().filter(a => a[0] === 0x36).map(a => a[1]);
      expect(seqs).toEqual([1]); // no retry after the abort
    });

    it('still rejects a genuine head refusal without retrying (NRC 0x22)', async () => {
      transport.setResponder(otaResponder({
        overrides: { 0x34: () => buildNegativeResponse(0x34, 0x22) } // diving
      }));
      const image = buildMcubootImage({ imageSize: 100 });
      await expect(ota.stageImage(image)).rejects.toMatchObject({ nrc: 0x22 });
      expect(sids().filter(s => s === 0x34)).toHaveLength(1);
    });
  });
});
