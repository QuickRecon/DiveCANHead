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
      expect(Object.keys(dids).length).toBe(0);
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
      expect(dids.length).toBe(uniqueDids.length);
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
