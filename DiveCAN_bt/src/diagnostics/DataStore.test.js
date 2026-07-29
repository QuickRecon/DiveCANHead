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
      expect(series).toHaveLength(1);
      expect(series[0].value).toBe(42);
      expect(series[0].timestamp).toBe(100);
    });

    it('adds point to existing series', () => {
      store._addPoint('test', 100, 1);
      store._addPoint('test', 101, 2);
      const series = store.getSeries('test');
      expect(series).toHaveLength(2);
    });

    it('ignores undefined values', () => {
      store._addPoint('test', 100, undefined);
      expect(store.getSeries('test')).toHaveLength(0);
    });

    it('ignores null values', () => {
      store._addPoint('test', 100, null);
      expect(store.getSeries('test')).toHaveLength(0);
    });

    it('ignores NaN values', () => {
      store._addPoint('test', 100, NaN);
      expect(store.getSeries('test')).toHaveLength(0);
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
      expect(series).toHaveLength(3);
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

  describe('log-drain quiescence', () => {
    /** Minimal EventEmitter stand-in for a UDSClient. */
    const makeFakeUds = () => {
      const handlers = {};
      return {
        on(evt, cb) { (handlers[evt] ||= []).push(cb); return this; },
        emit(evt) { (handlers[evt] || []).forEach(cb => cb()); }
      };
    };

    it('stamps log activity when the UDS client pushes a message', () => {
      const uds = makeFakeUds();
      const s = new DataStore({ udsClient: uds });
      expect(s._lastLogActivityMs).toBe(0);
      uds.emit('logMessage');
      expect(s._lastLogActivityMs).toBeGreaterThan(0);
    });

    it('resolves after the quiet window when no pushes arrive', async () => {
      const s = new DataStore({ logDrainQuietMs: 40, logDrainMaxMs: 500 });
      const start = Date.now();
      await s.waitForLogQuiescence();
      const elapsed = Date.now() - start;
      expect(elapsed).toBeGreaterThanOrEqual(30);   // ~quiet window (margin for timers)
      expect(elapsed).toBeLessThan(500);            // resolved via quiet, not the cap
    });

    it('gives up at the hard cap when pushes never stop', async () => {
      const uds = makeFakeUds();
      const s = new DataStore({ udsClient: uds, logDrainQuietMs: 60, logDrainMaxMs: 200 });
      // Keep stamping activity faster than the quiet window so it never goes idle.
      const ticker = setInterval(() => uds.emit('logMessage'), 15);
      const start = Date.now();
      try {
        await s.waitForLogQuiescence();
      } finally {
        clearInterval(ticker);
      }
      const elapsed = Date.now() - start;
      expect(elapsed).toBeGreaterThanOrEqual(180);  // hit the cap (~maxMs)
    });
  });

  // A minimal UDSClient stand-in with mockable async DID methods and an `on`
  // that captures push handlers (so the constructor's stamp wiring runs).
  const makeUds = (overrides = {}) => ({
    on: vi.fn(),
    readDIDsParsed: vi.fn().mockResolvedValue({}),
    fetchAllState: vi.fn().mockResolvedValue({}),
    readDataByIdentifier: vi.fn().mockResolvedValue(new Uint8Array([0])),
    ...overrides
  });

  describe('fetchAllDIDs', () => {
    it('rejects without a udsClient', async () => {
      await expect(store.fetchAllDIDs()).rejects.toThrow('UDSClient required');
    });

    it('reads cell types then fetches all state', async () => {
      const uds = makeUds({
        readDIDsParsed: vi.fn().mockResolvedValue({
          CELL0_TYPE: CELL_TYPE_ANALOG,
          CELL1_TYPE: CELL_TYPE_DIVEO2,
          CELL2_TYPE: 2
        }),
        fetchAllState: vi.fn().mockResolvedValue({ CONSENSUS_PPO2: 1.05 })
      });
      const s = new DataStore({ udsClient: uds });

      const state = await s.fetchAllDIDs();

      expect(state).toEqual({ CONSENSUS_PPO2: 1.05 });
      expect(s.cellTypes).toEqual([CELL_TYPE_ANALOG, CELL_TYPE_DIVEO2, 2]);
      expect(uds.fetchAllState).toHaveBeenCalledWith(
        [CELL_TYPE_ANALOG, CELL_TYPE_DIVEO2, 2],
        null
      );
      // Stored, charted, and available via getDIDValue
      expect(s.getDIDValue('CONSENSUS_PPO2')).toBe(1.05);
      expect(s.getSeries('consensus_ppo2')).toHaveLength(1);
    });

    it('defaults cell types to 0 when the type read fails', async () => {
      const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
      const uds = makeUds({
        readDIDsParsed: vi.fn().mockRejectedValue(new Error('desync')),
        fetchAllState: vi.fn().mockResolvedValue({})
      });
      const s = new DataStore({ udsClient: uds });

      await s.fetchAllDIDs();

      expect(s.cellTypes).toEqual([0, 0, 0]);
      expect(uds.fetchAllState).toHaveBeenCalledWith([0, 0, 0], null);
      expect(warn).toHaveBeenCalled();
    });

    it('adopts _cellTypes from the fetch result and skips metadata keys', async () => {
      const uds = makeUds({
        readDIDsParsed: vi.fn().mockResolvedValue({ CELL0_TYPE: 0, CELL1_TYPE: 0, CELL2_TYPE: 0 }),
        fetchAllState: vi.fn().mockResolvedValue({
          _cellTypes: [CELL_TYPE_ANALOG, CELL_TYPE_ANALOG, CELL_TYPE_ANALOG],
          SETPOINT: 1.2
        })
      });
      const s = new DataStore({ udsClient: uds });

      await s.fetchAllDIDs();

      expect(s.cellTypes).toEqual([CELL_TYPE_ANALOG, CELL_TYPE_ANALOG, CELL_TYPE_ANALOG]);
      // Metadata key not stored as a DID value
      expect(s.getDIDValue('_cellTypes')).toBeUndefined();
      expect(s.getDIDValue('SETPOINT')).toBe(1.2);
    });

    it('notifies subscribers only when a value changes', async () => {
      const uds = makeUds({
        readDIDsParsed: vi.fn().mockResolvedValue({ CELL0_TYPE: 0, CELL1_TYPE: 0, CELL2_TYPE: 0 }),
        fetchAllState: vi.fn().mockResolvedValue({ SETPOINT: 1.2 })
      });
      const s = new DataStore({ udsClient: uds });
      const cb = vi.fn();
      s.subscribe('SETPOINT', cb);

      await s.fetchAllDIDs();
      expect(cb).toHaveBeenCalledWith(1.2, undefined, 'SETPOINT');

      // Second fetch with same value: no further notification
      await s.fetchAllDIDs();
      expect(cb).toHaveBeenCalledTimes(1);
    });

    it('forwards the progress callback to fetchAllState', async () => {
      const progress = vi.fn();
      const uds = makeUds();
      const s = new DataStore({ udsClient: uds });

      await s.fetchAllDIDs(progress);

      expect(uds.fetchAllState).toHaveBeenCalledWith([0, 0, 0], progress);
    });
  });

  describe('initialize', () => {
    it('rejects without a udsClient', async () => {
      await expect(store.initialize()).rejects.toThrow('UDSClient required');
    });

    it('drains logs, fetches state, then starts polling', async () => {
      const uds = makeUds({
        readDIDsParsed: vi.fn().mockResolvedValue({ CELL0_TYPE: 0, CELL1_TYPE: 0, CELL2_TYPE: 0 }),
        fetchAllState: vi.fn().mockResolvedValue({ CONSENSUS_PPO2: 0.9 })
      });
      const s = new DataStore({ udsClient: uds, logDrainQuietMs: 1, logDrainMaxMs: 50 });

      const state = await s.initialize();

      expect(state).toEqual({ CONSENSUS_PPO2: 0.9 });
      expect(s.isPolling).toBe(true);
      s.stopPolling();
    });
  });

  describe('_collectSubscribedDIDs', () => {
    it('collects DIDs of subscribed STATE_DIDs', () => {
      store.subscribe('CONSENSUS_PPO2', vi.fn());
      store.subscribe('SETPOINT', vi.fn());
      expect(store._collectSubscribedDIDs()).toEqual([0xF200, 0xF202]);
    });

    it('skips unknown DID keys', () => {
      store.subscribe('NOT_A_REAL_DID', vi.fn());
      expect(store._collectSubscribedDIDs()).toEqual([]);
    });

    it('filters cell DIDs by configured cell type', () => {
      store.cellTypes = [CELL_TYPE_ANALOG, 0, 0];
      // CELL0_MILLIVOLTS is analog-only -> included; CELL0_TEMPERATURE is DiveO2 -> excluded
      store.subscribe('CELL0_MILLIVOLTS', vi.fn());
      store.subscribe('CELL0_TEMPERATURE', vi.fn());
      expect(store._collectSubscribedDIDs()).toEqual([0xF405]);
    });
  });

  describe('_collectSubscribedExtraDIDs', () => {
    it('collects subscribed EXTRA_READ_DIDs with their info', () => {
      store.subscribe('CRASH_REASON', vi.fn());
      store.subscribe('CONSENSUS_PPO2', vi.fn());  // STATE_DID, not extra
      const extras = store._collectSubscribedExtraDIDs();
      expect(extras).toHaveLength(1);
      expect(extras[0].key).toBe('CRASH_REASON');
      expect(extras[0].info.did).toBe(0xF251);
    });
  });

  describe('_updateDIDValues', () => {
    it('stores values, charts them, and notifies on change', () => {
      const cb = vi.fn();
      store.subscribe('CONSENSUS_PPO2', cb);
      store._updateDIDValues({ CONSENSUS_PPO2: 1.1 }, 123);

      expect(store.getDIDValue('CONSENSUS_PPO2')).toBe(1.1);
      expect(store.getSeries('consensus_ppo2')).toEqual([{ timestamp: 123, value: 1.1 }]);
      expect(cb).toHaveBeenCalledWith(1.1, undefined, 'CONSENSUS_PPO2');
    });

    it('does not notify when the value is unchanged', () => {
      const cb = vi.fn();
      store.didValues.set('SETPOINT', 1.3);
      store.subscribe('SETPOINT', cb);
      store._updateDIDValues({ SETPOINT: 1.3 }, 200);
      expect(cb).not.toHaveBeenCalled();
    });
  });

  describe('_pollSubscribedExtraDIDs', () => {
    it('reads each extra DID and stores the parsed value', async () => {
      const uds = makeUds({
        readDataByIdentifier: vi.fn().mockResolvedValue(new Uint8Array([0, 0, 0, 7]))
      });
      const s = new DataStore({ udsClient: uds });
      s.subscribe('CRASH_REASON', vi.fn());

      await s._pollSubscribedExtraDIDs(500);

      expect(uds.readDataByIdentifier).toHaveBeenCalledWith(0xF251);
      expect(s.getDIDValue('CRASH_REASON')).toBeDefined();
    });

    it('isolates a per-DID failure and leaves the value unchanged', async () => {
      const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
      const uds = makeUds({
        readDataByIdentifier: vi.fn().mockRejectedValue(new Error('NRC'))
      });
      const s = new DataStore({ udsClient: uds });
      s.subscribe('CRASH_REASON', vi.fn());

      await s._pollSubscribedExtraDIDs(500);

      expect(s.getDIDValue('CRASH_REASON')).toBeUndefined();
      expect(warn).toHaveBeenCalled();
    });
  });

  describe('_pollSubscribedDIDs', () => {
    it('returns early without a udsClient', async () => {
      store.subscribe('CONSENSUS_PPO2', vi.fn());
      await expect(store._pollSubscribedDIDs()).resolves.toBeUndefined();
    });

    it('returns early with no subscriptions', async () => {
      const uds = makeUds();
      const s = new DataStore({ udsClient: uds });
      await s._pollSubscribedDIDs();
      expect(uds.readDIDsParsed).not.toHaveBeenCalled();
    });

    it('chunks bundled scalar reads into requests of 4', async () => {
      const readDIDsParsed = vi.fn().mockResolvedValue({});
      const uds = makeUds({ readDIDsParsed });
      const s = new DataStore({ udsClient: uds });
      // 5 non-cell-typed STATE_DIDs -> 2 chunks (4 + 1)
      ['CONSENSUS_PPO2', 'SETPOINT', 'DUTY_CYCLE', 'INTEGRAL_STATE', 'UPTIME_SEC']
        .forEach((k) => s.subscribe(k, vi.fn()));

      await s._pollSubscribedDIDs();

      expect(readDIDsParsed).toHaveBeenCalledTimes(2);
      expect(readDIDsParsed.mock.calls[0][0]).toHaveLength(4);
      expect(readDIDsParsed.mock.calls[1][0]).toHaveLength(1);
    });

    it('reads both bundled and extra DIDs in one cycle', async () => {
      const readDIDsParsed = vi.fn().mockResolvedValue({ CONSENSUS_PPO2: 1.0 });
      const readDataByIdentifier = vi.fn().mockResolvedValue(new Uint8Array([0, 0, 0, 1]));
      const uds = makeUds({ readDIDsParsed, readDataByIdentifier });
      const s = new DataStore({ udsClient: uds });
      s.subscribe('CONSENSUS_PPO2', vi.fn());
      s.subscribe('CRASH_REASON', vi.fn());

      await s._pollSubscribedDIDs();

      expect(readDIDsParsed).toHaveBeenCalledTimes(1);
      expect(readDataByIdentifier).toHaveBeenCalledWith(0xF251);
    });
  });

  describe('polling loop (timer-driven)', () => {
    beforeEach(() => {
      vi.useFakeTimers();
    });

    afterEach(() => {
      vi.useRealTimers();
    });

    it('polls subscribed DIDs on each interval tick', async () => {
      const readDIDsParsed = vi.fn().mockResolvedValue({ CONSENSUS_PPO2: 1.0 });
      const uds = makeUds({ readDIDsParsed });
      const s = new DataStore({ udsClient: uds, pollInterval: 100 });
      s.subscribe('CONSENSUS_PPO2', vi.fn());
      s.startPolling();

      await vi.advanceTimersByTimeAsync(120);

      expect(readDIDsParsed).toHaveBeenCalled();
      s.stopPolling();
    });

    it('skips a tick when a poll cycle is already in flight', async () => {
      const uds = makeUds();
      const s = new DataStore({ udsClient: uds, pollInterval: 100 });
      s.subscribe('CONSENSUS_PPO2', vi.fn());
      s.startPolling();
      s._pollInFlight = true;  // pretend previous cycle still running

      await vi.advanceTimersByTimeAsync(120);

      expect(uds.readDIDsParsed).not.toHaveBeenCalled();
      s.stopPolling();
    });

    it('logs and recovers when a poll cycle throws', async () => {
      const error = vi.spyOn(console, 'error').mockImplementation(() => {});
      const uds = makeUds({ readDIDsParsed: vi.fn().mockRejectedValue(new Error('bus')) });
      const s = new DataStore({ udsClient: uds, pollInterval: 100 });
      s.subscribe('CONSENSUS_PPO2', vi.fn());
      s.startPolling();

      await vi.advanceTimersByTimeAsync(120);

      expect(error).toHaveBeenCalledWith('Poll error:', expect.any(Error));
      expect(s._pollInFlight).toBe(false);  // guard reset in finally
      s.stopPolling();
    });
  });

  describe('refreshCellTypes', () => {
    it('rejects without a udsClient', async () => {
      await expect(store.refreshCellTypes()).rejects.toThrow('UDSClient required');
    });

    it('reads and caches the three cell type DIDs', async () => {
      const uds = makeUds({
        readDIDsParsed: vi.fn().mockResolvedValue({
          CELL0_TYPE: CELL_TYPE_DIVEO2,
          CELL1_TYPE: CELL_TYPE_ANALOG,
          CELL2_TYPE: 2
        })
      });
      const s = new DataStore({ udsClient: uds });

      const types = await s.refreshCellTypes();

      expect(types).toEqual([CELL_TYPE_DIVEO2, CELL_TYPE_ANALOG, 2]);
      expect(s.cellTypes).toEqual([CELL_TYPE_DIVEO2, CELL_TYPE_ANALOG, 2]);
      expect(uds.readDIDsParsed).toHaveBeenCalledWith([0xF401, 0xF411, 0xF421]);
    });

    it('defaults missing types to 0', async () => {
      const uds = makeUds({
        readDIDsParsed: vi.fn().mockResolvedValue({ CELL0_TYPE: CELL_TYPE_ANALOG })
      });
      const s = new DataStore({ udsClient: uds });

      const types = await s.refreshCellTypes();

      expect(types).toEqual([CELL_TYPE_ANALOG, 0, 0]);
    });
  });
});
