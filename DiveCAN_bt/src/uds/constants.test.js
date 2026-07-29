/**
 * UDS constants unit tests
 */
import { describe, it, expect } from 'vitest';
import {
  getDIDInfo,
  getCellDIDs,
  getValidCellDIDs,
  getControlStateDIDs,
  STATE_DIDS,
  EXTRA_READ_DIDS,
  ALL_READ_DIDS,
  parseExtraDIDValue,
  decodeCrashHistory,
  decodeRebootHistory,
  formatResetCause,
  decodeDeviceCurrent,
  CELL_TYPE_ANALOG,
  CELL_TYPE_DIVEO2,
  CELL_TYPE_O2S,
  DID_CELL_BASE,
  DID_CELL_RANGE
} from './constants.js';

describe('UDS constants', () => {
  describe('getDIDInfo', () => {
    it('returns info for known DID', () => {
      const info = getDIDInfo(0xF200);
      expect(info).not.toBeNull();
      expect(info.key).toBe('CONSENSUS_PPO2');
      expect(info.type).toBe('float32');
      expect(info.size).toBe(4);
    });

    it('returns null for unknown DID', () => {
      const info = getDIDInfo(0xFFFF);
      expect(info).toBeNull();
    });

    it('returns info for cell DIDs', () => {
      const info = getDIDInfo(0xF400);
      expect(info.key).toBe('CELL0_PPO2');
      expect(info.type).toBe('float32');
    });

    it('returns info with label', () => {
      const info = getDIDInfo(0xF202);
      expect(info.label).toBe('Setpoint');
    });

    it('defines native-resolution cylinder pressure DIDs', () => {
      expect(STATE_DIDS.O2_CYL_PRESSURE).toMatchObject({
        did: 0xF238, size: 2, type: 'tank_pressure', unit: 'bar'
      });
      expect(STATE_DIDS.DIL_CYL_PRESSURE).toMatchObject({
        did: 0xF239, size: 2, type: 'tank_pressure', unit: 'bar'
      });
    });
  });

  describe('getCellDIDs', () => {
    it('returns all DIDs for cell 0', () => {
      const dids = getCellDIDs(0);
      expect(Object.keys(dids).length).toBeGreaterThan(0);
      expect(dids.CELL0_PPO2).toBeDefined();
      expect(dids.CELL0_TYPE).toBeDefined();
      expect(dids.CELL0_INCLUDED).toBeDefined();
    });

    it('returns all DIDs for cell 1', () => {
      const dids = getCellDIDs(1);
      expect(dids.CELL1_PPO2).toBeDefined();
      expect(dids.CELL0_PPO2).toBeUndefined();
    });

    it('returns all DIDs for cell 2', () => {
      const dids = getCellDIDs(2);
      expect(dids.CELL2_PPO2).toBeDefined();
    });

    it('returns empty object for invalid cell number', () => {
      const dids = getCellDIDs(99);
      expect(Object.keys(dids)).toHaveLength(0);
    });

    it('all returned DIDs have correct prefix', () => {
      const dids = getCellDIDs(1);
      for (const key of Object.keys(dids)) {
        expect(key.startsWith('CELL1_')).toBe(true);
      }
    });
  });

  describe('getValidCellDIDs', () => {
    it('filters DIDs by cell type - analog', () => {
      const dids = getValidCellDIDs(0, CELL_TYPE_ANALOG);

      // Should include PPO2, type, included, status (no cellType restriction)
      expect(dids.CELL0_PPO2).toBeDefined();
      expect(dids.CELL0_TYPE).toBeDefined();
      expect(dids.CELL0_INCLUDED).toBeDefined();

      // Should include analog-specific DIDs
      expect(dids.CELL0_RAW_ADC).toBeDefined();
      expect(dids.CELL0_MILLIVOLTS).toBeDefined();

      // Should NOT include DiveO2-specific DIDs
      expect(dids.CELL0_TEMPERATURE).toBeUndefined();
      expect(dids.CELL0_PHASE).toBeUndefined();
    });

    it('filters DIDs by cell type - DiveO2', () => {
      const dids = getValidCellDIDs(0, CELL_TYPE_DIVEO2);

      // Common DIDs
      expect(dids.CELL0_PPO2).toBeDefined();
      expect(dids.CELL0_STATUS).toBeDefined();

      // DiveO2-specific
      expect(dids.CELL0_TEMPERATURE).toBeDefined();
      expect(dids.CELL0_PHASE).toBeDefined();
      expect(dids.CELL0_INTENSITY).toBeDefined();

      // Should NOT include analog-specific
      expect(dids.CELL0_RAW_ADC).toBeUndefined();
      expect(dids.CELL0_MILLIVOLTS).toBeUndefined();
    });

    it('works for different cell numbers', () => {
      const cell0 = getValidCellDIDs(0, CELL_TYPE_ANALOG);
      const cell1 = getValidCellDIDs(1, CELL_TYPE_ANALOG);
      const cell2 = getValidCellDIDs(2, CELL_TYPE_ANALOG);

      expect(cell0.CELL0_PPO2).toBeDefined();
      expect(cell1.CELL1_PPO2).toBeDefined();
      expect(cell2.CELL2_PPO2).toBeDefined();
    });

    it('includes unrestricted DIDs for any cell type', () => {
      const analog = getValidCellDIDs(0, CELL_TYPE_ANALOG);
      const diveo2 = getValidCellDIDs(0, CELL_TYPE_DIVEO2);

      // Both should have PPO2, type, included, status
      expect(analog.CELL0_PPO2).toBeDefined();
      expect(diveo2.CELL0_PPO2).toBeDefined();
      expect(analog.CELL0_STATUS).toBeDefined();
      expect(diveo2.CELL0_STATUS).toBeDefined();
    });
  });

  describe('getControlStateDIDs', () => {
    it('returns non-cell DIDs', () => {
      const dids = getControlStateDIDs();

      expect(dids.CONSENSUS_PPO2).toBeDefined();
      expect(dids.SETPOINT).toBeDefined();
      expect(dids.DUTY_CYCLE).toBeDefined();
      expect(dids.UPTIME_SEC).toBeDefined();
    });

    it('excludes cell DIDs', () => {
      const dids = getControlStateDIDs();

      expect(dids.CELL0_PPO2).toBeUndefined();
      expect(dids.CELL1_PPO2).toBeUndefined();
      expect(dids.CELL2_PPO2).toBeUndefined();
    });

    it('excludes CELLS_VALID (starts with CELL)', () => {
      // CELLS_VALID starts with "CELL" so it's excluded by getControlStateDIDs
      const dids = getControlStateDIDs();
      expect(dids.CELLS_VALID).toBeUndefined();
    });

    it('includes power monitoring DIDs', () => {
      const dids = getControlStateDIDs();

      expect(dids.VBUS_VOLTAGE).toBeDefined();
      expect(dids.VCC_VOLTAGE).toBeDefined();
      expect(dids.BATTERY_VOLTAGE).toBeDefined();
    });
  });

  describe('STATE_DIDS structure', () => {
    it('all entries have required fields', () => {
      for (const [key, info] of Object.entries(STATE_DIDS)) {
        expect(info.did).toBeTypeOf('number');
        expect(info.size).toBeTypeOf('number');
        expect(info.type).toBeTypeOf('string');
        expect(info.label).toBeTypeOf('string');
      }
    });

    it('DID addresses are unique', () => {
      const dids = Object.values(STATE_DIDS).map(info => info.did);
      const uniqueDids = [...new Set(dids)];
      expect(dids).toHaveLength(uniqueDids.length);
    });

    it('cell DIDs follow expected pattern', () => {
      // Cell 0 DIDs start at 0xF400
      expect(STATE_DIDS.CELL0_PPO2.did).toBe(0xF400);
      // Cell 1 DIDs start at 0xF410
      expect(STATE_DIDS.CELL1_PPO2.did).toBe(0xF410);
      // Cell 2 DIDs start at 0xF420
      expect(STATE_DIDS.CELL2_PPO2.did).toBe(0xF420);
    });

    it('float32 DIDs have size 4', () => {
      for (const [, info] of Object.entries(STATE_DIDS)) {
        if (info.type === 'float32') {
          expect(info.size).toBe(4);
        }
      }
    });

    it('bool DIDs have size 1', () => {
      for (const [, info] of Object.entries(STATE_DIDS)) {
        if (info.type === 'bool') {
          expect(info.size).toBe(1);
        }
      }
    });
  });

  describe('extra read-only DIDs', () => {
    it('ALL_READ_DIDS is the union of state + extra DIDs', () => {
      expect(ALL_READ_DIDS.CONSENSUS_PPO2).toBe(STATE_DIDS.CONSENSUS_PPO2);
      expect(ALL_READ_DIDS.FW_COMMIT).toBe(EXTRA_READ_DIDS.FW_COMMIT);
    });

    it('every extra DID has a did, type, label and category', () => {
      for (const info of Object.values(EXTRA_READ_DIDS)) {
        expect(typeof info.did).toBe('number');
        expect(typeof info.type).toBe('string');
        expect(typeof info.label).toBe('string');
        expect(typeof info.category).toBe('string');
      }
    });

    it('parseExtraDIDValue decodes strings, scalars, hex and sem_ver', () => {
      expect(parseExtraDIDValue({ type: 'string' }, new TextEncoder().encode('AP_Aren\0')))
        .toBe('AP_Aren');
      expect(parseExtraDIDValue({ type: 'uint8' }, new Uint8Array([4]))).toBe(4);
      expect(parseExtraDIDValue({ type: 'uint32' }, new Uint8Array([1, 0, 0, 0]))).toBe(1);
      expect(parseExtraDIDValue({ type: 'hex32' }, new Uint8Array([0xEF, 0xBE, 0xAD, 0xDE])))
        .toBe('0xdeadbeef');
      expect(parseExtraDIDValue({ type: 'hex' }, new Uint8Array([0x01, 0xA5]))).toBe('01 a5');
      // 1.2, revision=3, build=5
      expect(parseExtraDIDValue({ type: 'semver' }, new Uint8Array([1, 2, 3, 0, 5, 0, 0, 0])))
        .toBe('1.2.3+5');
      expect(parseExtraDIDValue({ type: 'semver' }, new Uint8Array(8).fill(0xFF))).toBe('n/a');
    });

    it('parseExtraDIDValue decodes POST status (0xF271) state + pass mask', () => {
      // state 5 = CONFIRMED, mask 0x1F = all five checks passed
      expect(parseExtraDIDValue({ type: 'post_status' }, new Uint8Array([5, 0x1F, 0, 0])))
        .toBe('CONFIRMED · passed: cells, consensus, ppo2_tx, handset, solenoid');
      // state 3 = WAITING_HANDSET, mask 0x07 = cells|consensus|ppo2_tx
      expect(parseExtraDIDValue({ type: 'post_status' }, new Uint8Array([3, 0x07, 0, 0])))
        .toBe('WAITING_HANDSET · passed: cells, consensus, ppo2_tx');
      // no bits set
      expect(parseExtraDIDValue({ type: 'post_status' }, new Uint8Array([0, 0x00, 0, 0])))
        .toBe('WAITING_CELLS · passed: none');
    });

    it('parseExtraDIDValue decodes Poseidon gauge (0xF236) percent/flags/age', () => {
      // percent 72, flags b0|b1 (ever_received + fresh), age 15s
      expect(parseExtraDIDValue({ type: 'poseidon_gauge' }, new Uint8Array([72, 0x03, 15, 0])))
        .toBe('72% · fresh · age 15s');
      // ever_received but stale (b0|b2), age 300s
      expect(parseExtraDIDValue({ type: 'poseidon_gauge' }, new Uint8Array([50, 0x05, 0x2C, 0x01])))
        .toBe('50% · stale · age 300s');
      // never received -> "no data"
      expect(parseExtraDIDValue({ type: 'poseidon_gauge' }, new Uint8Array([0, 0x00, 0, 0])))
        .toBe('no data');
    });

    it('decodeDeviceCurrent decodes device current (0xF237) uA/age/valid', () => {
      // 125000 uA = 125 mA, age 3s, valid
      const draw = new Uint8Array([0x48, 0xE8, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00]);
      expect(decodeDeviceCurrent(draw)).toEqual({
        valid: true, currentUa: 125000, currentMa: 125, ageS: 3
      });
      // negative draw (e.g. charging) -50000 uA = -50 mA, age 300s, valid
      const charge = new Uint8Array([0xB0, 0x3C, 0xFF, 0xFF, 0x2C, 0x01, 0x01, 0x00]);
      expect(decodeDeviceCurrent(charge)).toEqual({
        valid: true, currentUa: -50000, currentMa: -50, ageS: 300
      });
      // no provider / no sample -> valid=0
      const invalid = new Uint8Array([0, 0, 0, 0, 0, 0, 0, 0]);
      expect(decodeDeviceCurrent(invalid).valid).toBe(false);
      // short payload -> null
      expect(decodeDeviceCurrent(new Uint8Array([0, 0, 0, 0]))).toBeNull();
    });

    it('parseExtraDIDValue decodes MCUBoot status (0xF270)', () => {
      // swap=None(0), confirmed=1, runningSlot=0, factory flag=0,
      // slot0 v1.2.3, slot1 absent(0xFF), factory absent(0xFF)
      const d = new Uint8Array([
        0, 1, 0, 0,
        1, 2, 3, 0,
        0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF
      ]);
      expect(parseExtraDIDValue({ type: 'mcuboot' }, d))
        .toBe('None · confirmed · run slot0 · s0 1.2.3 · s1 none');
    });

    it('decodes and formats persisted crash history newest first', () => {
      const d = new Uint8Array(2 + 24);
      const view = new DataView(d.buffer);
      d[0] = 1;
      d[1] = 1;
      [12, 2, 0x08001234, 0x08005678, 0x00010000, 0x20001000]
        .forEach((value, index) => view.setUint32(2 + (index * 4), value, true));

      expect(decodeCrashHistory(d)).toEqual({
        version: 1,
        records: [{
          rebootSequence: 12,
          reason: 2,
          pc: 0x08001234,
          lr: 0x08005678,
          cfsr: 0x00010000,
          thread: 0x20001000
        }]
      });
      expect(parseExtraDIDValue({ type: 'crash_history' }, d))
        .toContain('#12 reason 2, PC 0x08001234');
    });

    it('decodes reboot causes and preserves combined flag names', () => {
      const d = new Uint8Array(2 + 8);
      const view = new DataView(d.buffer);
      d[0] = 1;
      d[1] = 1;
      view.setUint32(2, 21, true);
      view.setUint32(6, 0x12, true); // software + watchdog

      expect(formatResetCause(0x12)).toBe('software + watchdog');
      expect(decodeRebootHistory(d)).toEqual({
        version: 1,
        records: [{
          rebootSequence: 21,
          resetCause: 0x12,
          resetCauseText: 'software + watchdog'
        }]
      });
      expect(parseExtraDIDValue({ type: 'reboot_history' }, d))
        .toBe('#21 software + watchdog (0x00000012)');
    });
  });

  describe('DID address constants', () => {
    it('DID_CELL_BASE matches first cell DID', () => {
      expect(DID_CELL_BASE).toBe(0xF400);
    });

    it('DID_CELL_RANGE matches cell DID spacing', () => {
      expect(DID_CELL_RANGE).toBe(0x0010);
      // Each cell has 16 DID slots
      expect(STATE_DIDS.CELL1_PPO2.did - STATE_DIDS.CELL0_PPO2.did).toBe(DID_CELL_RANGE);
      expect(STATE_DIDS.CELL2_PPO2.did - STATE_DIDS.CELL1_PPO2.did).toBe(DID_CELL_RANGE);
    });
  });
});
