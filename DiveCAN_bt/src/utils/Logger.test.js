/**
 * Logger unit tests
 */
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { Logger } from './Logger.js';

describe('Logger', () => {
  const originalGlobalLevel = Logger.globalLevel;
  let debugSpy;
  let infoSpy;
  let warnSpy;
  let errorSpy;

  beforeEach(() => {
    Logger.globalLevel = Logger.LEVELS.DEBUG;
    debugSpy = vi.spyOn(console, 'debug').mockImplementation(() => {});
    infoSpy = vi.spyOn(console, 'info').mockImplementation(() => {});
    warnSpy = vi.spyOn(console, 'warn').mockImplementation(() => {});
    errorSpy = vi.spyOn(console, 'error').mockImplementation(() => {});
  });

  afterEach(() => {
    Logger.globalLevel = originalGlobalLevel;
    vi.restoreAllMocks();
  });

  describe('setGlobalLevel', () => {
    it('sets the global level from a case-insensitive name', () => {
      Logger.setGlobalLevel('warn');
      expect(Logger.globalLevel).toBe(Logger.LEVELS.WARN);
      Logger.setGlobalLevel('ERROR');
      expect(Logger.globalLevel).toBe(Logger.LEVELS.ERROR);
    });

    it('falls back to INFO for an unknown level', () => {
      Logger.setGlobalLevel('bogus');
      expect(Logger.globalLevel).toBe(Logger.LEVELS.INFO);
    });
  });

  describe('constructor / setLevel', () => {
    it('defaults to debug level', () => {
      const log = new Logger('test');
      expect(log.level).toBe(Logger.LEVELS.DEBUG);
    });

    it('honours an explicit level', () => {
      const log = new Logger('test', 'warn');
      expect(log.level).toBe(Logger.LEVELS.WARN);
    });

    it('falls back to INFO for an unknown level', () => {
      const log = new Logger('test', 'nonsense');
      expect(log.level).toBe(Logger.LEVELS.INFO);
    });

    it('setLevel updates the instance level', () => {
      const log = new Logger('test', 'debug');
      log.setLevel('error');
      expect(log.level).toBe(Logger.LEVELS.ERROR);
    });
  });

  describe('log methods (without data)', () => {
    let log;
    beforeEach(() => { log = new Logger('mod', 'debug'); });

    it('debug logs and includes the name/level prefix', () => {
      log.debug('hello');
      expect(debugSpy).toHaveBeenCalledTimes(1);
      expect(debugSpy.mock.calls[0][0]).toContain('[DEBUG]');
      expect(debugSpy.mock.calls[0][0]).toContain('[mod]');
      expect(debugSpy.mock.calls[0][0]).toContain('hello');
    });

    it('info logs without a trailing data argument', () => {
      log.info('an info');
      expect(infoSpy).toHaveBeenCalledTimes(1);
      expect(infoSpy.mock.calls[0]).toHaveLength(1);
      expect(infoSpy.mock.calls[0][0]).toContain('[INFO]');
    });

    it('warn logs without a trailing data argument', () => {
      log.warn('a warning');
      expect(warnSpy).toHaveBeenCalledTimes(1);
      expect(warnSpy.mock.calls[0]).toHaveLength(1);
      expect(warnSpy.mock.calls[0][0]).toContain('[WARN]');
    });

    it('error logs without a trailing data argument', () => {
      log.error('an error');
      expect(errorSpy).toHaveBeenCalledTimes(1);
      expect(errorSpy.mock.calls[0]).toHaveLength(1);
      expect(errorSpy.mock.calls[0][0]).toContain('[ERROR]');
    });
  });

  describe('log methods (with data)', () => {
    let log;
    beforeEach(() => { log = new Logger('mod', 'debug'); });

    it('debug forwards the data argument', () => {
      const data = { a: 1 };
      log.debug('with data', data);
      expect(debugSpy.mock.calls[0][1]).toBe(data);
    });

    it('info forwards the data argument', () => {
      const data = { b: 2 };
      log.info('with data', data);
      expect(infoSpy.mock.calls[0]).toHaveLength(2);
      expect(infoSpy.mock.calls[0][1]).toBe(data);
    });

    it('warn forwards the data argument', () => {
      const data = { c: 3 };
      log.warn('with data', data);
      expect(warnSpy.mock.calls[0]).toHaveLength(2);
      expect(warnSpy.mock.calls[0][1]).toBe(data);
    });

    it('error forwards the data argument', () => {
      const data = { d: 4 };
      log.error('with data', data);
      expect(errorSpy.mock.calls[0]).toHaveLength(2);
      expect(errorSpy.mock.calls[0][1]).toBe(data);
    });
  });

  describe('level gating', () => {
    it('suppresses everything at NONE level', () => {
      const log = new Logger('mod', 'none');
      log.debug('x'); log.info('x'); log.warn('x'); log.error('x');
      expect(debugSpy).not.toHaveBeenCalled();
      expect(infoSpy).not.toHaveBeenCalled();
      expect(warnSpy).not.toHaveBeenCalled();
      expect(errorSpy).not.toHaveBeenCalled();
    });

    it('respects the global minimum over a more permissive instance level', () => {
      Logger.globalLevel = Logger.LEVELS.WARN;
      const log = new Logger('mod', 'debug'); // instance would allow all
      log.debug('x');
      log.info('x');
      log.warn('x');
      log.error('x');
      expect(debugSpy).not.toHaveBeenCalled(); // gated by global WARN
      expect(infoSpy).not.toHaveBeenCalled();
      expect(warnSpy).toHaveBeenCalledTimes(1);
      expect(errorSpy).toHaveBeenCalledTimes(1);
    });

    it('_effectiveLevel is the max of instance and global', () => {
      Logger.globalLevel = Logger.LEVELS.ERROR;
      const log = new Logger('mod', 'info');
      expect(log._effectiveLevel()).toBe(Logger.LEVELS.ERROR);
    });
  });
});
