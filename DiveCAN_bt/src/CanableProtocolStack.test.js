import { describe, it, expect, vi } from 'vitest';
import { CanableProtocolStack } from './CanableProtocolStack.js';

describe('CanableProtocolStack split addressing', () => {
  it('uses FE for dialogs and FF for pushed logs', () => {
    const stack = new CanableProtocolStack();
    expect(stack.transport.sourceAddress).toBe(0xFE);
    expect(stack.logTransport.sourceAddress).toBe(0xFF);
  });

  it('routes FF log pushes without presenting them to the dialog context', () => {
    const stack = new CanableProtocolStack();
    const log = vi.fn();
    stack.on('logMessage', log);
    stack.logTransport.processFrame({
      id: 0x0D0AFF04,
      data: new Uint8Array([7, 0, 0x2E, 0xA1, 0x00, 0x55, 0x53, 0x42])
    });
    expect(log).toHaveBeenCalledWith('USB');
    expect(stack.transport.state).toBe('IDLE');
  });

  it('supplies controller pings only after the real handset goes quiet', async () => {
    vi.useFakeTimers();
    vi.setSystemTime(1000);
    const stack = new CanableProtocolStack({ handsetMissingAfterMs: 2000, handsetPingIntervalMs: 1000 });
    stack.canable.sendFrame = vi.fn().mockResolvedValue(undefined);
    stack._startHandsetGuardian();
    await vi.advanceTimersByTimeAsync(1999);
    expect(stack.canable.sendFrame).not.toHaveBeenCalled();
    await vi.advanceTimersByTimeAsync(501);
    expect(stack.canable.sendFrame).toHaveBeenCalledWith(0x0D000001, new Uint8Array([1, 0, 0]));
    stack._stopHandsetGuardian();
    vi.useRealTimers();
  });

  it('yields synthetic pinging as soon as a real controller ping is observed', async () => {
    vi.useFakeTimers();
    vi.setSystemTime(1000);
    const stack = new CanableProtocolStack({ handsetMissingAfterMs: 1000, handsetPingIntervalMs: 500 });
    stack.canable.sendFrame = vi.fn().mockResolvedValue(undefined);
    stack._startHandsetGuardian();
    await vi.advanceTimersByTimeAsync(1000);
    expect(stack.canable.sendFrame).toHaveBeenCalledTimes(1);
    stack.canable.emit('frame', { id: 0x0D000001, data: new Uint8Array([1, 0, 1]), extended: true });
    await vi.advanceTimersByTimeAsync(999);
    expect(stack.canable.sendFrame).toHaveBeenCalledTimes(1);
    stack._stopHandsetGuardian();
    vi.useRealTimers();
  });
});
