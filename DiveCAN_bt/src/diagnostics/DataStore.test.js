/**
 * DataStore unit tests
 */
import { describe, it, expect, beforeEach, vi, afterEach } from 'vitest';
import { DataStore } from './DataStore.js';
import { CELL_TYPE_ANALOG, CELL_TYPE_DIVEO2 } from '../uds/constants.js';

describe('DataStore', () => {
  let store;

  beforeEach(() => {
    store = new DataStore();
  });

  afterEach(() => {
    store.stopPolling();
    vi.restoreAllMocks();
  });

  describe('constructor', () => {
    it('uses default options', () => {
      expect(store.maxPoints).toBe(500);
      expect(store.maxAge).toBe(300);
      expect(store.pollInterval).toBe(200);
    });

    it('accepts custom options', () => {
      const custom = new DataStore({
        maxPoints: 100,
        maxAge: 60,
        pollInterval: 500
      });
      expect(custom.maxPoints).toBe(100);
      expect(custom.maxAge).toBe(60);
      expect(custom.pollInterval).toBe(500);
    });

    it('initializes empty state', () => {
      expect(store.getSeriesKeys()).toEqual([]);
      expect(store.getAllDIDValues()).toEqual({});
    });
  });

  describe('_addPoint', () => {
    it('adds point to new series', () => {
      store._addPoint('test', 100, 42);
      const series = store.getSeries('test');
      expect(series.length).toBe(1);
      expect(series[0].value).toBe(42);
      expect(series[0].timestamp).toBe(100);
    });

    it('adds point to existing series', () => {
      store._addPoint('test', 100, 1);
      store._addPoint('test', 101, 2);
      const series = store.getSeries('test');
      expect(series.length).toBe(2);
    });

    it('ignores undefined values', () => {
      store._addPoint('test', 100, undefined);
      expect(store.getSeries('test').length).toBe(0);
    });

    it('ignores null values', () => {
      store._addPoint('test', 100, null);
      expect(store.getSeries('test').length).toBe(0);
    });

    it('ignores NaN values', () => {
      store._addPoint('test', 100, NaN);
      expect(store.getSeries('test').length).toBe(0);
    });

    it('prunes old points by age', () => {
      store._addPoint('test', 100, 1);
      store._addPoint('test', 200, 2);
      store._addPoint('test', 500, 3);  // This should trigger pruning of point at 100

      const series = store.getSeries('test');
      // With maxAge=300, points older than 500-300=200 are pruned
      expect(series.every(p => p.timestamp >= 200)).toBe(true);
    });

    it('prunes by count', () => {
      const smallStore = new DataStore({ maxPoints: 3 });

      smallStore._addPoint('test', 100, 1);
      smallStore._addPoint('test', 101, 2);
      smallStore._addPoint('test', 102, 3);
      smallStore._addPoint('test', 103, 4);

      const series = smallStore.getSeries('test');
      expect(series.length).toBe(3);
      expect(series[0].value).toBe(2);  // First point pruned
    });
  });

  describe('getSeries', () => {
    it('returns empty array for unknown series', () => {
      expect(store.getSeries('unknown')).toEqual([]);
    });

    it('returns series data', () => {
      store._addPoint('test', 100, 42);
      const series = store.getSeries('test');
      expect(series).toEqual([{ timestamp: 100, value: 42 }]);
    });
  });

  describe('getLatest', () => {
    it('returns null for unknown series', () => {
      expect(store.getLatest('unknown')).toBeNull();
    });

    it('returns last point', () => {
      store._addPoint('test', 100, 1);
      store._addPoint('test', 200, 2);
      const latest = store.getLatest('test');
      expect(latest.value).toBe(2);
      expect(latest.timestamp).toBe(200);
    });
  });

  describe('getSeriesKeys', () => {
    it('returns all series keys', () => {
      store._addPoint('a', 100, 1);
      store._addPoint('b', 100, 2);
      store._addPoint('c', 100, 3);
      expect(store.getSeriesKeys()).toEqual(['a', 'b', 'c']);
    });
  });

  describe('clear', () => {
    it('clears all series', () => {
      store._addPoint('test', 100, 42);
      store.clear();
      expect(store.getSeriesKeys()).toEqual([]);
    });

    it('clears DID values', () => {
      store.didValues.set('TEST', 42);
      store.clear();
      expect(store.getAllDIDValues()).toEqual({});
    });
  });

  describe('subscription system', () => {
    describe('subscribe', () => {
      it('registers callback', () => {
        const callback = vi.fn();
        store.subscribe('CONSENSUS_PPO2', callback);
        expect(store.subscriptions.has('CONSENSUS_PPO2')).toBe(true);
      });

      it('returns unsubscribe function', () => {
        const callback = vi.fn();
        const unsubscribe = store.subscribe('CONSENSUS_PPO2', callback);
        expect(typeof unsubscribe).toBe('function');
      });

      it('supports multiple subscribers', () => {
        const cb1 = vi.fn();
        const cb2 = vi.fn();
        store.subscribe('TEST', cb1);
        store.subscribe('TEST', cb2);
        expect(store.subscriptions.get('TEST').size).toBe(2);
      });
    });

    describe('unsubscribe', () => {
      it('removes callback', () => {
        const callback = vi.fn();
        store.subscribe('TEST', callback);
        store.unsubscribe('TEST', callback);
        expect(store.subscriptions.get('TEST')?.size ?? 0).toBe(0);
      });

      it('cleans up empty subscription sets', () => {
        const callback = vi.fn();
        store.subscribe('TEST', callback);
        store.unsubscribe('TEST', callback);
        expect(store.subscriptions.has('TEST')).toBe(false);
      });
    });

    describe('returned unsubscribe function', () => {
      it('removes callback when called', () => {
        const callback = vi.fn();
        const unsubscribe = store.subscribe('TEST', callback);
        unsubscribe();
        expect(store.subscriptions.has('TEST')).toBe(false);
      });
    });

    describe('_notifySubscribers', () => {
      it('calls subscribed callbacks', () => {
        const callback = vi.fn();
        store.subscribe('TEST', callback);
        store._notifySubscribers('TEST', 42, 41);
        expect(callback).toHaveBeenCalledWith(42, 41, 'TEST');
      });

      it('catches callback errors', () => {
        const errorCallback = () => { throw new Error('Test error'); };
        const normalCallback = vi.fn();

        store.subscribe('TEST', errorCallback);
        store.subscribe('TEST', normalCallback);

        // Should not throw
        expect(() => store._notifySubscribers('TEST', 1, 0)).not.toThrow();
        // Normal callback should still be called
        expect(normalCallback).toHaveBeenCalled();
      });
    });
  });

  describe('DID value management', () => {
    describe('getDIDValue', () => {
      it('returns undefined for unknown DID', () => {
        expect(store.getDIDValue('UNKNOWN')).toBeUndefined();
      });

      it('returns stored value', () => {
        store.didValues.set('CONSENSUS_PPO2', 1.05);
        expect(store.getDIDValue('CONSENSUS_PPO2')).toBe(1.05);
      });
    });

    describe('getAllDIDValues', () => {
      it('returns all values as object', () => {
        store.didValues.set('A', 1);
        store.didValues.set('B', 2);
        expect(store.getAllDIDValues()).toEqual({ A: 1, B: 2 });
      });
    });
  });

  describe('cell type handling', () => {
    describe('getCellType', () => {
      it('returns cached cell type', () => {
        store.cellTypes = [CELL_TYPE_ANALOG, CELL_TYPE_DIVEO2, CELL_TYPE_ANALOG];
        expect(store.getCellType(0)).toBe(CELL_TYPE_ANALOG);
        expect(store.getCellType(1)).toBe(CELL_TYPE_DIVEO2);
        expect(store.getCellType(2)).toBe(CELL_TYPE_ANALOG);
      });

      it('returns 0 for invalid cell number', () => {
        expect(store.getCellType(99)).toBe(0);
      });
    });

    describe('getCellTypeName', () => {
      it('returns human-readable name', () => {
        store.cellTypes = [CELL_TYPE_DIVEO2, CELL_TYPE_ANALOG, 2];
        expect(store.getCellTypeName(0)).toBe('DiveO2');
        expect(store.getCellTypeName(1)).toBe('Analog');
        expect(store.getCellTypeName(2)).toBe('O2S');
      });
    });

    describe('getCachedCellTypes', () => {
      it('returns copy of cell types', () => {
        store.cellTypes = [1, 2, 0];
        const types = store.getCachedCellTypes();
        types[0] = 99;  // Modify copy
        expect(store.cellTypes[0]).toBe(1);  // Original unchanged
      });
    });
  });

  describe('isCellIncluded', () => {
    it('checks bit in CELLS_VALID', () => {
      store.didValues.set('CELLS_VALID', 0b101);  // Cells 0 and 2
      expect(store.isCellIncluded(0)).toBe(true);
      expect(store.isCellIncluded(1)).toBe(false);
      expect(store.isCellIncluded(2)).toBe(true);
    });

    it('returns false when CELLS_VALID undefined', () => {
      expect(store.isCellIncluded(0)).toBe(false);
    });
  });

  describe('_didKeyToSeriesKey', () => {
    it('converts cell DIDs to dotted format', () => {
      expect(store._didKeyToSeriesKey('CELL0_PPO2')).toBe('cell0.ppo2');
      expect(store._didKeyToSeriesKey('CELL1_TEMPERATURE')).toBe('cell1.temperature');
      expect(store._didKeyToSeriesKey('CELL2_RAW_ADC')).toBe('cell2.rawAdc');
    });

    it('converts control DIDs to lowercase', () => {
      expect(store._didKeyToSeriesKey('CONSENSUS_PPO2')).toBe('consensus_ppo2');
      expect(store._didKeyToSeriesKey('SETPOINT')).toBe('setpoint');
    });

    it('handles AMBIENT_LIGHT field mapping', () => {
      expect(store._didKeyToSeriesKey('CELL0_AMBIENT_LIGHT')).toBe('cell0.ambientLight');
    });
  });

  describe('seriesKeyToDIDKey', () => {
    it('converts cell series keys back', () => {
      expect(store.seriesKeyToDIDKey('cell0.ppo2')).toBe('CELL0_PPO2');
      expect(store.seriesKeyToDIDKey('cell1.temperature')).toBe('CELL1_TEMPERATURE');
    });

    it('converts control series keys back', () => {
      expect(store.seriesKeyToDIDKey('consensus_ppo2')).toBe('CONSENSUS_PPO2');
      expect(store.seriesKeyToDIDKey('setpoint')).toBe('SETPOINT');
    });
  });

  describe('polling', () => {
    describe('startPolling', () => {
      it('sets isPolling flag', () => {
        store.startPolling();
        expect(store.isPolling).toBe(true);
      });

      it('does not start multiple timers', () => {
        store.startPolling();
        const timer1 = store.pollTimer;
        store.startPolling();
        expect(store.pollTimer).toBe(timer1);
      });
    });

    describe('stopPolling', () => {
      it('clears isPolling flag', () => {
        store.startPolling();
        store.stopPolling();
        expect(store.isPolling).toBe(false);
      });

      it('clears timer', () => {
        store.startPolling();
        store.stopPolling();
        expect(store.pollTimer).toBeNull();
      });
    });
  });

  describe('_extractCellNum', () => {
    it('extracts cell number from DID key', () => {
      expect(store._extractCellNum('CELL0_PPO2')).toBe(0);
      expect(store._extractCellNum('CELL1_TYPE')).toBe(1);
      expect(store._extractCellNum('CELL2_INCLUDED')).toBe(2);
    });

    it('returns null for non-cell keys', () => {
      expect(store._extractCellNum('CONSENSUS_PPO2')).toBeNull();
      expect(store._extractCellNum('SETPOINT')).toBeNull();
    });
  });

  describe('_isValidDIDForCell', () => {
    beforeEach(() => {
      store.cellTypes = [CELL_TYPE_ANALOG, CELL_TYPE_DIVEO2, CELL_TYPE_ANALOG];
    });

    it('returns true for unrestricted DIDs', () => {
      const didInfo = { cellType: undefined };
      expect(store._isValidDIDForCell('CELL0_PPO2', didInfo)).toBe(true);
    });

    it('returns true when cellType matches', () => {
      const didInfo = { cellType: CELL_TYPE_ANALOG };
      expect(store._isValidDIDForCell('CELL0_MILLIVOLTS', didInfo)).toBe(true);
    });

    it('returns false when cellType does not match', () => {
      const didInfo = { cellType: CELL_TYPE_DIVEO2 };
      expect(store._isValidDIDForCell('CELL0_TEMPERATURE', didInfo)).toBe(false);
    });

    it('returns true for non-cell DIDs', () => {
      const didInfo = { cellType: CELL_TYPE_ANALOG };
      expect(store._isValidDIDForCell('CONSENSUS_PPO2', didInfo)).toBe(true);
    });
  });
});
