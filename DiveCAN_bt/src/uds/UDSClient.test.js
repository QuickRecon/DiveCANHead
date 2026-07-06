/**
 * UDSClient unit tests
 */
import { describe, it, expect, beforeEach, vi, afterEach } from 'vitest';
import { UDSClient } from './UDSClient.js';
import { MockTransport } from '../../tests/mocks/MockTransport.js';
import {
  RESPONSES,
  buildRDBIResponse,
  buildNegativeResponse,
  buildWDBIResponse,
  buildSessionResponse,
  buildRoutineResponse,
  buildRequestDownloadResponse,
  buildTransferResponse,
  buildTransferExitResponse,
  NRC
} from '../../tests/fixtures/uds-responses.js';
import {
  FLOAT32_VECTORS,
  UINT8_VECTORS,
  BOOL_VECTORS
} from '../../tests/fixtures/did-test-vectors.js';
import { STATE_DIDS, getDIDInfo } from './constants.js';

describe('UDSClient', () => {
  let client;
  let transport;

  beforeEach(() => {
    transport = new MockTransport();
    client = new UDSClient(transport);
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  describe('constructor', () => {
    it('sets up transport message handler', () => {
      expect(transport.events['message']).toBeDefined();
      expect(transport.events['message'].length).toBe(1);
    });

    it('accepts options', () => {
      const customClient = new UDSClient(transport, { requestDelay: 100 });
      expect(customClient.requestDelay).toBe(100);
    });

    it('defaults requestDelay to 0', () => {
      expect(client.requestDelay).toBe(0);
    });
  });

  describe('readDataByIdentifier', () => {
    it('sends correct request', async () => {
      transport.queueResponse(RESPONSES.RDBI.CONSENSUS_PPO2);

      await client.readDataByIdentifier(0xF200);

      const sent = transport.getLastSent();
      expect(Array.from(sent)).toEqual([0x22, 0xF2, 0x00]);
    });

    it('returns data portion of response', async () => {
      transport.queueResponse(RESPONSES.RDBI.CELLS_VALID);

      const data = await client.readDataByIdentifier(0xF203);

      expect(Array.from(data)).toEqual([0x07]);
    });

    it('verifies DID in response', async () => {
      // Response with wrong DID
      transport.queueResponse(buildRDBIResponse(0xF201, [0x00]));

      await expect(client.readDataByIdentifier(0xF200))
        .rejects.toThrow('DID mismatch');
    });

    it('handles negative response', async () => {
      transport.queueResponse(RESPONSES.NEGATIVE.OUT_OF_RANGE);

      await expect(client.readDataByIdentifier(0xFFFF))
        .rejects.toThrow();
    });

    it('times out on no response', async () => {
      vi.useFakeTimers();

      const promise = client.readDataByIdentifier(0xF200);

      vi.advanceTimersByTime(6000);

      await expect(promise).rejects.toThrow('timeout');

      vi.useRealTimers();
    });
  });

  describe('writeDataByIdentifier', () => {
    it('sends correct request', async () => {
      transport.queueResponse(buildWDBIResponse(0xF240));

      await client.writeDataByIdentifier(0xF240, [0x82]);

      const sent = transport.getLastSent();
      expect(Array.from(sent)).toEqual([0x2E, 0xF2, 0x40, 0x82]);
    });

    it('handles negative response', async () => {
      transport.queueResponse(RESPONSES.NEGATIVE.CONDITIONS_NOT_CORRECT);

      await expect(client.writeDataByIdentifier(0xF240, [0x82]))
        .rejects.toThrow();
    });
  });

  describe('negative response handling', () => {
    it('throws UDSError with NRC', async () => {
      transport.queueResponse(buildNegativeResponse(0x22, NRC.REQUEST_OUT_OF_RANGE));

      try {
        await client.readDataByIdentifier(0xFFFF);
        expect.fail('Should have thrown');
      } catch (error) {
        expect(error.name).toBe('UDSError');
        expect(error.nrc).toBe(0x31);
      }
    });

    it('provides NRC description', async () => {
      transport.queueResponse(buildNegativeResponse(0x22, NRC.REQUEST_OUT_OF_RANGE));

      try {
        await client.readDataByIdentifier(0xFFFF);
      } catch (error) {
        expect(error.getNRCDescription()).toContain('Out of Range');
      }
    });

    it('emits negativeResponse event', async () => {
      const handler = vi.fn();
      client.on('negativeResponse', handler);

      transport.queueResponse(RESPONSES.NEGATIVE.OUT_OF_RANGE);

      try {
        await client.readDataByIdentifier(0xFFFF);
      } catch {
        // Expected
      }

      expect(handler).toHaveBeenCalledWith(expect.objectContaining({
        sid: 0x22,
        nrc: 0x31
      }));
    });
  });

  describe('parseDIDValue', () => {
    describe('float32', () => {
      for (const vector of FLOAT32_VECTORS.slice(0, 3)) {
        it(`parses ${vector.description}`, () => {
          const data = new Uint8Array(vector.rawBytes);
          const result = client.parseDIDValue(vector.did, data);

          if (vector.tolerance) {
            expect(result).toBeCloseTo(vector.expectedValue, 2);
          } else {
            expect(result).toBe(vector.expectedValue);
          }
        });
      }
    });

    describe('uint8', () => {
      for (const vector of UINT8_VECTORS) {
        it(`parses ${vector.description}`, () => {
          const data = new Uint8Array(vector.rawBytes);
          const result = client.parseDIDValue(vector.did, data);
          expect(result).toBe(vector.expectedValue);
        });
      }
    });

    describe('bool', () => {
      for (const vector of BOOL_VECTORS) {
        it(`parses ${vector.description}`, () => {
          const data = new Uint8Array(vector.rawBytes);
          const result = client.parseDIDValue(vector.did, data);
          expect(result).toBe(vector.expectedValue);
        });
      }
    });

    it('returns raw data for unknown DID', () => {
      const data = new Uint8Array([0x01, 0x02, 0x03]);
      const result = client.parseDIDValue(0xFFFF, data);
      expect(result).toEqual(data);
    });

    it('returns undefined for insufficient data', () => {
      // DID 0xF200 expects 4 bytes (float32)
      const data = new Uint8Array([0x00, 0x00]);
      const result = client.parseDIDValue(0xF200, data);
      expect(result).toBeUndefined();
    });

    it('returns undefined for null data', () => {
      const result = client.parseDIDValue(0xF200, null);
      expect(result).toBeUndefined();
    });
  });

  describe('unsolicited WDBI handling', () => {
    it('emits logMessage for log DID', () => {
      const handler = vi.fn();
      client.on('logMessage', handler);

      const message = 'Test log message';
      const payload = new TextEncoder().encode(message);
      const data = new Uint8Array([0x2E, 0xA1, 0x00, ...payload]);

      transport.injectMessage(data);

      expect(handler).toHaveBeenCalledWith(message);
    });

    it('emits unsolicitedMessage for other DIDs', () => {
      const handler = vi.fn();
      client.on('unsolicitedMessage', handler);

      const data = new Uint8Array([0x2E, 0xF2, 0x00, 0x01, 0x02]);

      transport.injectMessage(data);

      expect(handler).toHaveBeenCalledWith(expect.objectContaining({
        did: 0xF200
      }));
    });

    it('does not resolve pending request for unsolicited WDBI', async () => {
      transport.queueResponse(RESPONSES.RDBI.CONSENSUS_PPO2);

      // Start a request
      const requestPromise = client.readDataByIdentifier(0xF200);

      // Inject unsolicited WDBI before response
      const unsolicited = new Uint8Array([0x2E, 0xA1, 0x00, 0x48, 0x69]);
      transport.injectMessage(unsolicited);

      // Request should still complete with queued response
      const data = await requestPromise;
      expect(data).toBeDefined();
    });
  });

  describe('concurrent request handling', () => {
    it('rejects second request when one is pending', async () => {
      vi.useFakeTimers();

      // Start first request (no response queued, will timeout)
      const first = client.readDataByIdentifier(0xF200);

      // Try second request immediately
      await expect(client.readDataByIdentifier(0xF201))
        .rejects.toThrow('pending');

      // Cleanup
      vi.advanceTimersByTime(10000);
      await first.catch(() => {});

      vi.useRealTimers();
    });
  });

  describe('high-level methods', () => {
    describe('readFirmwareVersion', () => {
      it('returns decoded git-describe string', async () => {
        transport.queueResponse(
          buildRDBIResponse(0xF000, new TextEncoder().encode('v1.2.3-4'))
        );

        const version = await client.readFirmwareVersion();

        expect(version).toBe('v1.2.3-4');
      });
    });

    describe('readVariantName', () => {
      it('returns decoded variant string', async () => {
        transport.queueResponse(
          buildRDBIResponse(0xF002, new TextEncoder().encode('Poseidon_Aren'))
        );

        const variant = await client.readVariantName();

        expect(variant).toBe('Poseidon_Aren');
      });
    });

    describe('readSerialNumber', () => {
      it('returns hex string of the raw UID', async () => {
        transport.queueResponse(buildRDBIResponse(0xF003, [0xDE, 0xAD, 0xBE, 0xEF]));

        const serial = await client.readSerialNumber();

        expect(serial).toBe('deadbeef');
      });
    });

    describe('readHardwareVersion', () => {
      it('returns version number', async () => {
        transport.queueResponse(buildRDBIResponse(0xF001, [0x03]));

        const version = await client.readHardwareVersion();

        expect(version).toBe(3);
      });
    });

    describe('writeSetpoint', () => {
      it('sends setpoint value', async () => {
        transport.queueResponse(buildWDBIResponse(0xF240));

        await client.writeSetpoint(130);

        const sent = transport.getLastSent();
        expect(Array.from(sent)).toEqual([0x2E, 0xF2, 0x40, 130]);
      });

      it('rejects invalid setpoint', async () => {
        await expect(client.writeSetpoint(-1)).rejects.toThrow();
        await expect(client.writeSetpoint(256)).rejects.toThrow();
      });
    });

    describe('triggerCalibration', () => {
      it('sends fO2 value', async () => {
        transport.queueResponse(buildWDBIResponse(0xF241));

        await client.triggerCalibration(21);

        const sent = transport.getLastSent();
        expect(Array.from(sent)).toEqual([0x2E, 0xF2, 0x41, 21]);
      });

      it('rejects invalid fO2', async () => {
        await expect(client.triggerCalibration(-1)).rejects.toThrow();
        await expect(client.triggerCalibration(101)).rejects.toThrow();
      });
    });

    describe('writeSolenoidOverride', () => {
      it('sends [channel, 0x5A] to DID 0xF242', async () => {
        transport.queueResponse(buildWDBIResponse(0xF242));
        await client.writeSolenoidOverride(2);
        expect(Array.from(transport.getLastSent())).toEqual([0x2E, 0xF2, 0x42, 2, 0x5A]);
      });

      it('defaults to channel 0', async () => {
        transport.queueResponse(buildWDBIResponse(0xF242));
        await client.writeSolenoidOverride();
        expect(Array.from(transport.getLastSent())).toEqual([0x2E, 0xF2, 0x42, 0, 0x5A]);
      });
    });
  });

  describe('generic services', () => {
    it('enterSession sends session control and resolves on 0x50', async () => {
      transport.queueResponse(buildSessionResponse(0x02));
      const resp = await client.enterSession(0x02);
      expect(Array.from(transport.getLastSent())).toEqual([0x10, 0x02]);
      expect(resp[0]).toBe(0x50);
    });

    it('routineControl sends 0x31 0x01 with big-endian RID + params', async () => {
      transport.queueResponse(buildRoutineResponse(0xF105));
      await client.routineControl(0xF105, [0x00]);
      expect(Array.from(transport.getLastSent())).toEqual([0x31, 0x01, 0xF1, 0x05, 0x00]);
    });

    it('requestDownload sends OTA size big-endian and returns max block', async () => {
      transport.queueResponse(buildRequestDownloadResponse(256));
      const maxBlock = await client.requestDownload(0, 0x1234, { sizeEndian: 'BE' });
      expect(Array.from(transport.getLastSent())).toEqual([
        0x34, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x34
      ]);
      expect(maxBlock).toBe(256);
    });

    it('requestDownload sends log size little-endian with sentinel addr', async () => {
      transport.queueResponse(buildRequestDownloadResponse(253));
      await client.requestDownload(0xFFFFFFFE, 61, { sizeEndian: 'LE' });
      expect(Array.from(transport.getLastSent())).toEqual([
        0x34, 0x00, 0x44, 0xFE, 0xFF, 0xFF, 0xFF, 0x3D, 0x00, 0x00, 0x00
      ]);
    });

    it('transferData sends seq + data and returns full response body', async () => {
      transport.queueResponse(buildTransferResponse(5, [0xAA, 0xBB]));
      const resp = await client.transferData(5, [0x01, 0x02]);
      expect(Array.from(transport.getLastSent())).toEqual([0x36, 5, 0x01, 0x02]);
      expect(Array.from(resp)).toEqual([0x76, 5, 0xAA, 0xBB]);
    });

    it('requestTransferExit sends 0x37', async () => {
      transport.queueResponse(buildTransferExitResponse());
      const resp = await client.requestTransferExit();
      expect(Array.from(transport.getLastSent())).toEqual([0x37]);
      expect(resp[0]).toBe(0x77);
    });
  });

  describe('settings wire format', () => {
    it('getSettingOptionLabel uses (settingIndex<<4)+optionIndex', async () => {
      // setting 2, option 3 -> 0x9150 + (2<<4) + 3 = 0x9173
      transport.queueResponse(buildRDBIResponse(0x9173, new TextEncoder().encode('Absolute ')));
      const label = await client.getSettingOptionLabel(2, 3);
      const sent = transport.getLastSent();
      expect(Array.from(sent.slice(1, 3))).toEqual([0x91, 0x73]);
      expect(label).toBe('Absolute');
    });

    it('getSettingInfo parses fixed-width label + kind + editable + optionCount', async () => {
      // label(9) + sep + kind(TEXT=1) + editable(1) + maxValue + optionCount(3)
      const payload = [
        ...new TextEncoder().encode('PPO2 Mode'), 0x00, 0x01, 0x01, 0x02, 0x03
      ];
      transport.queueResponse(buildRDBIResponse(0x9111, payload));
      const info = await client.getSettingInfo(1);
      expect(info.label).toBe('PPO2 Mode');
      expect(info.kind).toBe(1);
      expect(info.editable).toBe(true);
      expect(info.optionCount).toBe(3);
    });
  });

  describe('event emitter', () => {
    it('emits response event on positive response', async () => {
      const handler = vi.fn();
      client.on('response', handler);

      transport.queueResponse(RESPONSES.RDBI.CELLS_VALID);
      await client.readDataByIdentifier(0xF203);

      expect(handler).toHaveBeenCalled();
    });

    it('emits error event on transport error', () => {
      const handler = vi.fn();
      client.on('error', handler);

      transport.injectError(new Error('Transport error'));

      expect(handler).toHaveBeenCalled();
    });
  });

  describe('inter-request delay', () => {
    it('enforces delay between requests', async () => {
      const delayedClient = new UDSClient(transport, { requestDelay: 50 });

      // First request
      transport.queueResponse(RESPONSES.RDBI.CELLS_VALID);
      const start = Date.now();
      await delayedClient.readDataByIdentifier(0xF203);

      // Second request should wait for delay
      transport.queueResponse(RESPONSES.RDBI.CONSENSUS_PPO2);
      await delayedClient.readDataByIdentifier(0xF200);
      const elapsed = Date.now() - start;

      // Total time should be at least the delay (with some margin for execution)
      expect(elapsed).toBeGreaterThanOrEqual(40);
    });
  });

  describe('fetchAllState resilience', () => {
    it('falls back to individual reads when a bundled chunk fails', async () => {
      const calls = [];
      // Stub the bundled read: any multi-DID request fails; single-DID reads
      // succeed (except one "unsupported" DID which always fails).
      const UNSUPPORTED = STATE_DIDS.POWER_SOURCES.did;
      client.readDIDsParsed = async (dids) => {
        calls.push(dids.slice());
        if (dids.length > 1) {
          throw Object.assign(new Error('bundle failed'), { nrc: 0x31 });
        }
        if (dids[0] === UNSUPPORTED) {
          throw Object.assign(new Error('unsupported'), { nrc: 0x31 });
        }
        const info = getDIDInfo(dids[0]);
        return info ? { [info.key]: 1 } : {};
      };

      const result = await client.fetchAllState([1, 1, 1]); // analog cells

      // Both a bundle attempt and individual fallbacks happened
      expect(calls.some(c => c.length > 1)).toBe(true);
      expect(calls.some(c => c.length === 1)).toBe(true);
      // A supported DID came through the individual fallback
      expect(result.CONSENSUS_PPO2).toBe(1);
      // The unsupported DID was skipped, not fatal
      expect(result.POWER_SOURCES).toBeUndefined();
    });
  });
});
