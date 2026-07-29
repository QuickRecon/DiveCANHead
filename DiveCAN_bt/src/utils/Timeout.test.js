import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { Timeout, TimeoutManager } from './Timeout.js';

describe('Timeout', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('is inactive until started', () => {
    const timeout = new Timeout(100);
    expect(timeout.isActive).toBe(false);
    expect(timeout.ms).toBe(100);
    expect(timeout.reason).toBe('Operation timed out');
  });

  it('throws when the promise is read before start', () => {
    const timeout = new Timeout(100);
    expect(() => timeout.promise).toThrow('Timeout not started');
  });

  it('start returns this for chaining and activates the timer', () => {
    const timeout = new Timeout(100, 'no reply');
    expect(timeout.start()).toBe(timeout);
    expect(timeout.isActive).toBe(true);
  });

  it('rejects with the given reason and a timeout flag once the deadline passes', async () => {
    const timeout = new Timeout(250, 'device went quiet').start();
    const outcome = expect(timeout.promise).rejects.toMatchObject({
      message: 'device went quiet',
      timeout: true
    });
    await vi.advanceTimersByTimeAsync(250);
    await outcome;
  });

  it('does not reject before the deadline', async () => {
    const timeout = new Timeout(200, 'too slow').start();
    const rejected = vi.fn();
    timeout.promise.catch(rejected);
    await vi.advanceTimersByTimeAsync(199);
    expect(rejected).not.toHaveBeenCalled();
    timeout.cancel();
  });

  it('cancel disarms the timer so the promise never rejects', async () => {
    const timeout = new Timeout(100, 'cancelled anyway').start();
    const rejected = vi.fn();
    timeout.promise.catch(rejected);
    timeout.cancel();
    expect(timeout.isActive).toBe(false);
    await vi.advanceTimersByTimeAsync(1000);
    expect(rejected).not.toHaveBeenCalled();
  });

  it('cancel clears the stored promise so a stale read throws', () => {
    const timeout = new Timeout(100).start();
    timeout.cancel();
    expect(() => timeout.promise).toThrow('Timeout not started');
  });

  it('cancel on a never-started timeout is a harmless no-op', () => {
    const timeout = new Timeout(100);
    expect(() => timeout.cancel()).not.toThrow();
    expect(timeout.isActive).toBe(false);
  });

  it('restarting an active timeout replaces the previous deadline', async () => {
    const timeout = new Timeout(100, 'first arming').start();
    const firstRejected = vi.fn();
    timeout.promise.catch(firstRejected);

    await vi.advanceTimersByTimeAsync(50);
    timeout.start();
    const secondRejected = vi.fn();
    timeout.promise.catch(secondRejected);

    // The original 100 ms deadline passes without firing the replaced timer
    await vi.advanceTimersByTimeAsync(99);
    expect(firstRejected).not.toHaveBeenCalled();
    expect(secondRejected).not.toHaveBeenCalled();

    await vi.advanceTimersByTimeAsync(1);
    expect(firstRejected).not.toHaveBeenCalled();
    expect(secondRejected).toHaveBeenCalledTimes(1);
  });
});

describe('TimeoutManager', () => {
  let manager;

  beforeEach(() => {
    vi.useFakeTimers();
    manager = new TimeoutManager();
  });

  afterEach(() => {
    manager.clearAll();
    vi.useRealTimers();
  });

  it('fires the callback after the deadline and forgets the name', async () => {
    const callback = vi.fn();
    manager.set('ping', 100, callback);
    expect(manager.has('ping')).toBe(true);

    await vi.advanceTimersByTimeAsync(100);
    expect(callback).toHaveBeenCalledTimes(1);
    expect(manager.has('ping')).toBe(false);
  });

  it('setting the same name again replaces the pending timer', async () => {
    const first = vi.fn();
    const second = vi.fn();
    manager.set('response', 100, first);
    manager.set('response', 200, second);

    await vi.advanceTimersByTimeAsync(150);
    expect(first).not.toHaveBeenCalled();
    expect(second).not.toHaveBeenCalled();

    await vi.advanceTimersByTimeAsync(50);
    expect(first).not.toHaveBeenCalled();
    expect(second).toHaveBeenCalledTimes(1);
  });

  it('clear cancels a pending timer', async () => {
    const callback = vi.fn();
    manager.set('response', 100, callback);
    manager.clear('response');
    expect(manager.has('response')).toBe(false);

    await vi.advanceTimersByTimeAsync(1000);
    expect(callback).not.toHaveBeenCalled();
  });

  it('clear on an unknown name is a no-op', () => {
    expect(() => manager.clear('never-set')).not.toThrow();
  });

  it('clearAll cancels every pending timer', async () => {
    const first = vi.fn();
    const second = vi.fn();
    manager.set('a', 50, first);
    manager.set('b', 75, second);
    manager.clearAll();
    expect(manager.has('a')).toBe(false);
    expect(manager.has('b')).toBe(false);

    await vi.advanceTimersByTimeAsync(1000);
    expect(first).not.toHaveBeenCalled();
    expect(second).not.toHaveBeenCalled();
  });
});
