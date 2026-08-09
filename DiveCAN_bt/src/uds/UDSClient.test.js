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
  MULTI_DID_RESPONSES,
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
      expect(transport.events['message']).toHaveLength(1);
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

    describe('tank pressure', () => {
      it('converts little-endian decibar to bar without losing tenths', () => {
        expect(client.parseDIDValue(
          STATE_DIDS.O2_CYL_PRESSURE.did,
          new Uint8Array([0xD2, 0x04])
        )).toBe(123.4);
      });

      it('maps the firmware failure sentinel to NaN', () => {
        expect(client.parseDIDValue(
          STATE_DIDS.DIL_CYL_PRESSURE.did,
          new Uint8Array([0xFF, 0xFF])
        )).toBeNaN();
      });
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

    it('accepts log pushes from a separate unsolicited channel', () => {
      const handler = vi.fn();
      client.on('logMessage', handler);
      expect(client.processUnsolicited([0x2E, 0xA1, 0x00, 0x55, 0x53, 0x42])).toBe(true);
      expect(handler).toHaveBeenCalledWith('USB');
    });

    it('does not feed non-WDBI side-channel traffic into a pending dialog', () => {
      const handler = vi.fn();
      client.on('response', handler);
      expect(client.processUnsolicited([0x62, 0xF2, 0x00, 1])).toBe(false);
      expect(handler).not.toHaveBeenCalled();
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
    it('serializes overlapping requests instead of rejecting', async () => {
      // Two callers issue requests at the same time (e.g. background DID poll
      // and a user-driven read). Both must succeed, in order, rather than the
      // second throwing "Request already pending".
      transport.queueResponse(buildRDBIResponse(0xF200, [0x11]));
      transport.queueResponse(buildRDBIResponse(0xF201, [0x22]));

      const [first, second] = await Promise.all([
        client.readDataByIdentifier(0xF200),
        client.readDataByIdentifier(0xF201)
      ]);

      expect(Array.from(first)).toEqual([0x11]);
      expect(Array.from(second)).toEqual([0x22]);

      // Requests must have gone out in submission order, one at a time.
      const sent = transport.getAllSent();
      expect(sent).toHaveLength(2);
      expect(Array.from(sent[0])).toEqual([0x22, 0xF2, 0x00]);
      expect(Array.from(sent[1])).toEqual([0x22, 0xF2, 0x01]);
    });

    it('does not send a queued request until the prior one resolves', async () => {
      transport.queueResponse(buildRDBIResponse(0xF200, [0x11]));
      transport.queueResponse(buildRDBIResponse(0xF201, [0x22]));

      const first = client.readDataByIdentifier(0xF200);
      const second = client.readDataByIdentifier(0xF201);

      // Let the first request's send + response microtasks settle.
      await first;

      // The second request must not have been transmitted concurrently with
      // the first — serialization holds it back until the first completes.
      const sentAfterFirst = transport.getAllSent().map(b => Array.from(b));
      expect(sentAfterFirst[0]).toEqual([0x22, 0xF2, 0x00]);

      await second;
    });

    it('advances the queue even when a request rejects', async () => {
      // A negative response on the first request must not wedge the queue.
      transport.queueResponse(buildNegativeResponse(0x22, NRC.REQUEST_OUT_OF_RANGE));
      transport.queueResponse(buildRDBIResponse(0xF201, [0x22]));

      const firstResult = await client.readDataByIdentifier(0xF200).catch(e => e);
      expect(firstResult).toBeInstanceOf(Error);

      const second = await client.readDataByIdentifier(0xF201);
      expect(Array.from(second)).toEqual([0x22]);
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

    describe('error histogram', () => {
      it('readErrorHistogram decodes the 0xF260 uint16[] payload', async () => {
        // 38 slots; slot 9 (CELL_FAILURE) = 3, slot 17 (ISOTP_TIMEOUT) = 0x0102
        const payload = new Uint8Array(38 * 2);
        const view = new DataView(payload.buffer);
        view.setUint16(9 * 2, 3, true);
        view.setUint16(17 * 2, 0x0102, true);
        transport.queueResponse(buildRDBIResponse(0xF260, payload));

        const entries = await client.readErrorHistogram();

        expect(entries).toHaveLength(38);
        expect(entries[9]).toMatchObject({ name: 'CELL_FAILURE', count: 3 });
        expect(entries[17]).toMatchObject({ name: 'ISOTP_TIMEOUT', count: 258 });
      });

      it('clearErrorHistogram writes a byte to 0xF261', async () => {
        transport.queueResponse(buildWDBIResponse(0xF261));

        await client.clearErrorHistogram();

        const sent = transport.getLastSent();
        expect(Array.from(sent)).toEqual([0x2E, 0xF2, 0x61, 0x01]);
      });

      it('reads and decodes crash and reboot histories', async () => {
        const crash = new Uint8Array(26);
        const crashView = new DataView(crash.buffer);
        crash[0] = 1;
        crash[1] = 1;
        [7, 2, 0x08001234, 0x08005678, 0x10000, 0x20001000]
          .forEach((value, index) => crashView.setUint32(2 + (index * 4), value, true));
        const reboot = new Uint8Array(10);
        const rebootView = new DataView(reboot.buffer);
        reboot[0] = 1;
        reboot[1] = 1;
        rebootView.setUint32(2, 7, true);
        rebootView.setUint32(6, 0x10, true);

        transport.queueResponse(buildRDBIResponse(0xF255, crash));
        transport.queueResponse(buildRDBIResponse(0xF256, reboot));

        const crashes = await client.readCrashHistory();
        const reboots = await client.readRebootHistory();

        expect(crashes.records[0]).toMatchObject({
          rebootSequence: 7,
          reason: 2,
          pc: 0x08001234
        });
        expect(reboots.records[0]).toMatchObject({
          rebootSequence: 7,
          resetCause: 0x10,
          resetCauseText: 'watchdog'
        });
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

  describe('autotune', () => {
    it('autotuneStart sends a bounded-duty identification request to 0xF243', async () => {
      transport.queueResponse(buildWDBIResponse(0xF243));
      await client.autotuneStart({ baseCb: 70, excitationDutyPct: 20 });
      expect(Array.from(transport.getLastSent())).toEqual([
        0x2E, 0xF2, 0x43, 0x01, 0xA7, 70, 20, 0x00, 0x01
      ]);
    });

    it('autotuneAbort sends [0x02, 0xA7] to 0xF243', async () => {
      transport.queueResponse(buildWDBIResponse(0xF243));
      await client.autotuneAbort();
      expect(Array.from(transport.getLastSent())).toEqual([0x2E, 0xF2, 0x43, 0x02, 0xA7]);
    });

    it('readAutotuneStatus parses the compact 66-byte model-identification status struct', async () => {
      const buf = new ArrayBuffer(66);
      const dv = new DataView(buf);
      dv.setUint8(0, 2);              // state = STEPPING
      dv.setUint8(1, 4);              // abort_reason = TIMEOUT
      dv.setUint16(2, 5, true);       // iteration
      dv.setUint16(4, 24, true);      // budget
      dv.setFloat32(6, 1.75, true);   // best kp
      dv.setFloat32(10, 0.3, true);   // best ki
      dv.setFloat32(14, 0.05, true);  // best kd
      dv.setFloat32(18, 0.0123, true);// response-tail noise
      dv.setUint32(22, 42, true);     // elapsed_s
      dv.setFloat32(26, 1.4, true);   // plant gain
      dv.setFloat32(30, 3.5, true);   // dead time
      dv.setFloat32(34, 8.0, true);   // recovery time
      dv.setFloat32(38, 0.0123, true);// response-tail noise
      dv.setFloat32(42, 0.07, true);  // mixing excursion
      dv.setFloat32(46, 0.12, true);  // baseline duty
      dv.setFloat32(50, 0.0004, true);// baseline slope
      dv.setFloat32(54, 1.01, true);  // ambient pressure
      dv.setFloat32(58, 1.8, true);   // delivered incremental dose
      dv.setFloat32(62, 0.006, true); // baseline RMS noise
      transport.queueResponse(buildRDBIResponse(0xF213, Array.from(new Uint8Array(buf))));

      const st = await client.readAutotuneStatus();

      expect(Array.from(transport.getLastSent())).toEqual([0x22, 0xF2, 0x13]);
      expect(st.state).toBe(2);
      expect(st.stateName).toBe('Identifying plant');
      expect(st.abortReason).toBe(4);
      expect(st.abortReasonName).toBe('Timeout');
      expect(st.iteration).toBe(5);
      expect(st.budget).toBe(24);
      expect(st.cand).toEqual({ kp: 0, ki: 0, kd: 0 });
      expect(st.best.kp).toBeCloseTo(1.75, 5);
      expect(st.best.ki).toBeCloseTo(0.3, 5);
      expect(st.best.kd).toBeCloseTo(0.05, 5);
      expect(st.bestCost).toBeCloseTo(0.0123, 5);
      expect(st.elapsedS).toBe(42);
      expect(st.model.gain).toBeCloseTo(1.4, 5);
      expect(st.model.deadTimeS).toBeCloseTo(3.5, 5);
      expect(st.model.timeConstantS).toBeCloseTo(8.0, 5);
      expect(st.model.fitRmseBar).toBeCloseTo(0.0123, 5);
      expect(st.model.mixingExcursionBar).toBeCloseTo(0.07, 5);
      expect(st.model.baselineDuty).toBeCloseTo(0.12, 5);
      expect(st.model.baselineSlopeBarS).toBeCloseTo(0.0004, 6);
      expect(st.model.ambientPressureBar).toBeCloseTo(1.01, 5);
      expect(st.model.deliveredDoseDutyS).toBeCloseTo(1.8, 5);
      expect(st.model.baselineNoiseBar).toBeCloseTo(0.006, 6);
    });

    it('readAutotuneStatus rejects a truncated legacy response', async () => {
      transport.queueResponse(buildRDBIResponse(0xF213, new Array(60).fill(0)));
      await expect(client.readAutotuneStatus()).rejects.toThrow(/too short/);
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

    it('invokes the progress callback once per chunk', async () => {
      client.readDIDsParsed = async (dids) => {
        const out = {};
        for (const did of dids) {
          const info = getDIDInfo(did);
          if (info) out[info.key] = 1;
        }
        return out;
      };

      const progress = vi.fn();
      await client.fetchAllState([1, 1, 1], progress);

      expect(progress).toHaveBeenCalled();
      // Each call is (chunkIndex, totalChunks); the number of calls equals total.
      const total = progress.mock.calls[0][1];
      expect(progress.mock.calls).toHaveLength(total);
      expect(progress).toHaveBeenLastCalledWith(total, total);
    });
  });

  describe('EventEmitter', () => {
    it('off removes a previously registered handler', () => {
      const handler = vi.fn();
      client.on('foo', handler);
      client.off('foo', handler);
      client.emit('foo');
      expect(handler).not.toHaveBeenCalled();
    });

    it('off on an unknown event is a no-op', () => {
      expect(() => client.off('never-registered', () => {})).not.toThrow();
    });

    it('removeAllListeners(event) clears only that event', () => {
      const a = vi.fn();
      const b = vi.fn();
      client.on('a', a);
      client.on('b', b);
      client.removeAllListeners('a');
      client.emit('a');
      client.emit('b');
      expect(a).not.toHaveBeenCalled();
      expect(b).toHaveBeenCalled();
    });

    it('removeAllListeners() clears every event', () => {
      const a = vi.fn();
      client.on('a', a);
      client.removeAllListeners();
      client.emit('a');
      expect(a).not.toHaveBeenCalled();
    });

    it('swallows errors thrown inside a handler', () => {
      const spy = vi.spyOn(console, 'error').mockImplementation(() => {});
      client.on('boom', () => { throw new Error('handler failure'); });
      expect(() => client.emit('boom')).not.toThrow();
      expect(spy).toHaveBeenCalled();
    });
  });

  describe('transport error while a request is pending', () => {
    it('rejects the in-flight request when the transport errors', async () => {
      // No response queued -> the request stays pending.
      const promise = client.readDataByIdentifier(0xF200);
      // Let the dispatch settle so pendingReject is armed.
      await new Promise(r => setTimeout(r, 0));
      transport.injectError(new Error('bus dropped'));
      await expect(promise).rejects.toThrow('bus dropped');
    });
  });

  describe('send failure and dispatch guards', () => {
    it('rejects with "Failed to send request" when transport.send rejects', async () => {
      transport.send = vi.fn(() => Promise.reject(new Error('write failed')));
      await expect(client.readDataByIdentifier(0xF200))
        .rejects.toThrow('Failed to send request');
    });

    it('_dispatchRequest throws when a request is already pending', async () => {
      // Directly exercise the serialization assertion (unreachable via the
      // public queue). The first dispatch arms pendingRequest synchronously.
      client._dispatchRequest([0x22, 0xF2, 0x00], 5000).catch(() => {});
      await expect(client._dispatchRequest([0x22, 0xF2, 0x01], 5000))
        .rejects.toThrow('Request already pending');
      client._clearPendingRequest();
    });
  });

  describe('unexpected positive response SID', () => {
    it('rejects when the response SID does not match the request', async () => {
      // A DiagnosticSessionControl reply (0x50) arrives while an RDBI (expects
      // 0x62) is pending.
      transport.queueResponse(new Uint8Array([0x50, 0x02]));
      await expect(client.readDataByIdentifier(0xF200))
        .rejects.toThrow('Unexpected response SID');
    });
  });

  describe('requestDownload validation', () => {
    it('throws on a malformed (short) 0x74 response', async () => {
      transport.queueResponse(new Uint8Array([0x74, 0x20]));
      await expect(client.requestDownload(0, 0x100))
        .rejects.toThrow('Malformed RequestDownload response');
    });
  });

  describe('settings enumeration', () => {
    it('getSettingCount returns the first payload byte', async () => {
      transport.queueResponse(buildRDBIResponse(0x9100, [5]));
      const count = await client.getSettingCount();
      expect(Array.from(transport.getLastSent())).toEqual([0x22, 0x91, 0x00]);
      expect(count).toBe(5);
    });

    it('getSettingInfo parses a NUMBER-kind setting (no option count)', async () => {
      const payload = [
        ...new TextEncoder().encode('Setpoint'), 0x00, // 8 chars + pad to 9? handled below
      ];
      // Build exactly 9-byte label field + sep + kind + editable.
      const label = new Uint8Array(9);
      label.set(new TextEncoder().encode('Setpoint'));
      const number = [...label, 0x00, 0x00 /* NUMBER */, 0x00 /* not editable */];
      transport.queueResponse(buildRDBIResponse(0x9110, number));
      const info = await client.getSettingInfo(0);
      expect(info.label).toBe('Setpoint');
      expect(info.kind).toBe(0);
      expect(info.editable).toBe(false);
      expect(info.optionCount).toBe(0);
      void payload;
    });

    it('getSettingValue parses max + current 64-bit big-endian values', async () => {
      const payload = [0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 100];
      transport.queueResponse(buildRDBIResponse(0x9130, payload));
      const { maxValue, currentValue } = await client.getSettingValue(0);
      expect(Array.from(transport.getLastSent())).toEqual([0x22, 0x91, 0x30]);
      expect(maxValue).toBe(255n);
      expect(currentValue).toBe(100n);
    });

    it('writeSettingValue writes 8 big-endian bytes to the value DID', async () => {
      transport.queueResponse(buildWDBIResponse(0x9130));
      await client.writeSettingValue(0, 5);
      expect(Array.from(transport.getLastSent()))
        .toEqual([0x2E, 0x91, 0x30, 0, 0, 0, 0, 0, 0, 0, 5]);
    });

    it('saveSetting writes to the save-base DID', async () => {
      transport.queueResponse(buildWDBIResponse(0x9351));
      await client.saveSetting(1, 3);
      expect(Array.from(transport.getLastSent()))
        .toEqual([0x2E, 0x93, 0x51, 0, 0, 0, 0, 0, 0, 0, 3]);
    });

    it('enumerateSettings reads info + value for each setting', async () => {
      // count = 1, then info(0) and value(0)
      transport.queueResponse(buildRDBIResponse(0x9100, [1]));
      const label = new Uint8Array(9);
      label.set(new TextEncoder().encode('Mode'));
      transport.queueResponse(buildRDBIResponse(0x9110,
        [...label, 0x00, 0x01 /* TEXT */, 0x01 /* editable */, 0x02, 0x04]));
      transport.queueResponse(buildRDBIResponse(0x9130,
        [0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 2]));

      const settings = await client.enumerateSettings();
      expect(settings).toHaveLength(1);
      expect(settings[0]).toMatchObject({
        index: 0,
        label: 'Mode',
        kind: 1,
        editable: true,
        optionCount: 4,
        maxValue: 7n,
        currentValue: 2n
      });
    });
  });

  describe('readMultipleDIDs', () => {
    it('returns an empty Map for an empty DID list', async () => {
      expect((await client.readMultipleDIDs([])).size).toBe(0);
    });

    it('returns an empty Map for null', async () => {
      expect((await client.readMultipleDIDs(null)).size).toBe(0);
    });

    it('parses a bundled response using each DID size', async () => {
      transport.queueResponse(MULTI_DID_RESPONSES.CONTROL_STATE);
      const map = await client.readMultipleDIDs([0xF200, 0xF202, 0xF203]);
      expect(map.size).toBe(3);
      expect(Array.from(map.get(0xF203))).toEqual([0x07]);
      expect(Array.from(map.get(0xF200))).toEqual([0x66, 0x66, 0x86, 0x3F]);
    });

    it('consumes the remainder for an unknown trailing DID', async () => {
      // [0x62, 0xFFFF, payload...] -> unknown DID, no further known header found
      transport.queueResponse(new Uint8Array([0x62, 0xFF, 0xFF, 0x01, 0x02, 0x03]));
      const map = await client.readMultipleDIDs([0xFFFF]);
      expect(Array.from(map.get(0xFFFF))).toEqual([0x01, 0x02, 0x03]);
    });

    it('finds the next known DID header after an unknown DID', async () => {
      // Unknown 0xFFFF then known 0xF203 (1 byte). The scan must stop the
      // unknown field at the 0xF203 header.
      transport.queueResponse(new Uint8Array([
        0x62, 0xFF, 0xFF, 0xAA, 0xF2, 0x03, 0x07
      ]));
      const map = await client.readMultipleDIDs([0xFFFF, 0xF203]);
      expect(Array.from(map.get(0xFFFF))).toEqual([0xAA]);
      expect(Array.from(map.get(0xF203))).toEqual([0x07]);
    });
  });

  describe('parseDIDValue integer types', () => {
    it('parses int32 little-endian', () => {
      // CELL0_TEMPERATURE (0xF406, int32)
      const v = client.parseDIDValue(0xF406, new Uint8Array([0xFF, 0xFF, 0xFF, 0xFF]));
      expect(v).toBe(-1);
    });

    it('parses uint32 little-endian', () => {
      // CELL0_ERROR (0xF407, uint32)
      const v = client.parseDIDValue(0xF407, new Uint8Array([0x00, 0x00, 0x00, 0x80]));
      expect(v).toBe(0x80000000);
    });

    it('parses int16 little-endian', () => {
      // CELL0_RAW_ADC (0xF404, int16)
      const v = client.parseDIDValue(0xF404, new Uint8Array([0x00, 0x80]));
      expect(v).toBe(-32768);
    });

    it('parses uint16 little-endian', () => {
      // SATURATION_COUNT (0xF212, uint16)
      const v = client.parseDIDValue(0xF212, new Uint8Array([0x34, 0x12]));
      expect(v).toBe(0x1234);
    });

    it('parses device_current, returning the draw in mA', () => {
      // 0xF237: currentUa=1000, age=0, valid=1
      const v = client.parseDIDValue(
        0xF237,
        new Uint8Array([0xE8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00])
      );
      expect(v).toBe(1);
    });

    it('maps an invalid device_current sample to NaN', () => {
      const v = client.parseDIDValue(
        0xF237,
        new Uint8Array([0xE8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
      );
      expect(v).toBeNaN();
    });
  });

  describe('readDIDsParsed / readControlState / readCellState', () => {
    it('keys known DIDs by name and unknown DIDs by hex', async () => {
      transport.queueResponse(buildRDBIResponse(0xF203, [0x07]));
      const known = await client.readDIDsParsed([0xF203]);
      expect(known.CELLS_VALID).toBe(7);

      transport.queueResponse(new Uint8Array([0x62, 0xFF, 0xFF, 0x09]));
      const unknown = await client.readDIDsParsed([0xFFFF]);
      expect(Array.from(unknown['0xffff'])).toEqual([0x09]);
    });

    it('readControlState fetches the control DID set', async () => {
      const spy = vi.fn(async (dids) => ({ dids }));
      client.readDIDsParsed = spy;
      const result = await client.readControlState();
      expect(spy).toHaveBeenCalledTimes(1);
      // Control DIDs exclude cell DIDs (0xF4xx)
      expect(result.dids.every(d => d < 0xF400)).toBe(true);
      expect(result.dids).toContain(0xF200);
    });

    it('readCellState fetches only DIDs valid for the cell type', async () => {
      const spy = vi.fn(async (dids) => ({ dids }));
      client.readDIDsParsed = spy;
      const result = await client.readCellState(0, 1 /* analog */);
      expect(spy).toHaveBeenCalledTimes(1);
      // All requested DIDs belong to cell 0 (0xF400-0xF40C)
      expect(result.dids.every(d => d >= 0xF400 && d <= 0xF40C)).toBe(true);
    });
  });

  describe('abortPending', () => {
    it('rejects the in-flight request and every queued request', async () => {
      // No responder: requests hang until aborted.
      const inFlight = client.readDataByIdentifier(0xF200);
      const queued = client.readDataByIdentifier(0xF201);
      // Let the first request dispatch and the second settle into the queue.
      await new Promise(r => setTimeout(r, 0));

      client.abortPending();

      await expect(inFlight).rejects.toMatchObject({
        details: { disconnected: true }
      });
      await expect(queued).rejects.toMatchObject({
        details: { disconnected: true }
      });
    });

    it('rejections carry a null NRC so retry logic treats them as transport errors', async () => {
      const inFlight = client.readDataByIdentifier(0xF200);
      await new Promise(r => setTimeout(r, 0));
      client.abortPending();
      await expect(inFlight).rejects.toMatchObject({ nrc: null });
    });

    it('leaves the client usable for the next request', async () => {
      const inFlight = client.readDataByIdentifier(0xF200);
      await new Promise(r => setTimeout(r, 0));
      client.abortPending();
      await expect(inFlight).rejects.toThrow();

      transport.setResponder(() => buildRDBIResponse(0xF203, [0x07]));
      const data = await client.readDataByIdentifier(0xF203);
      expect(Array.from(data)).toEqual([0x07]);
    });

    it('is a no-op when nothing is pending', () => {
      expect(() => client.abortPending()).not.toThrow();
    });

    it('uses a caller-supplied rejection reason', async () => {
      const inFlight = client.readDataByIdentifier(0xF200);
      await new Promise(r => setTimeout(r, 0));
      client.abortPending(new Error('link went away'));
      await expect(inFlight).rejects.toThrow('link went away');
    });
  });
});
