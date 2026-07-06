import { describe, it, expect, beforeEach } from 'vitest';
import { OTAManager } from './OTAManager.js';
import { UDSClient } from '../uds/UDSClient.js';
import { MockTransport } from '../../tests/mocks/MockTransport.js';
import { buildMcubootImage } from '../../tests/fixtures/mcuboot-image.js';
import {
  buildSessionResponse, buildRequestDownloadResponse, buildTransferResponse,
  buildTransferExitResponse, buildRoutineResponse, buildWDBIResponse,
  buildNegativeResponse
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
});
