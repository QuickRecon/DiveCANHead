import { describe, it, expect, vi } from 'vitest';
import { CanableProtocolStack } from './CanableProtocolStack.js';

describe('CanableProtocolStack split addressing', () => {
  it.each([0x02, 0x04])('addresses head 0x%s without mixing dialog and log traffic', async address => {
    const stack = new CanableProtocolStack({ targetAddress: address, handsetEmulation: false });
    stack.canable.sendFrame = vi.fn().mockResolvedValue(undefined);
    const dialog = vi.fn();
    const log = vi.fn();
    stack.transport.on('message', dialog);
    stack.on('logMessage', log);

    await stack.transport.send([0x22, 0xF0, 0x00]);
    expect(stack.canable.sendFrame.mock.calls[0][0]).toBe(0x0D0A00FE | (address << 8));

    // CANable emits each physical frame to both independent receive contexts.
    const reply = { id: 0x0D0AFE00 | address, data: new Uint8Array([4, 0, 0x62, 0xF0, 0]) };
    const push = { id: 0x0D0AFF00 | address, data: new Uint8Array([7, 0, 0x2E, 0xA1, 0, 0x55, 0x53, 0x42]) };
    stack.canable.emit('frame', reply);
    stack.canable.emit('frame', push);
    expect(dialog).toHaveBeenCalledExactlyOnceWith(new Uint8Array([0x62, 0xF0, 0]));
    expect(log).toHaveBeenCalledExactlyOnceWith('USB');

    const otherHead = address === 0x02 ? 0x04 : 0x02;
    stack.canable.emit('frame', { ...reply, id: 0x0D0AFE00 | otherHead });
    stack.canable.emit('frame', { ...push, id: 0x0D0AFF00 | otherHead });
    expect(dialog).toHaveBeenCalledTimes(1);
    expect(log).toHaveBeenCalledTimes(1);
  });

  it('uses FE for dialogs and FF for pushed logs', () => {
    const stack = new CanableProtocolStack();
    expect(stack.transport.sourceAddress).toBe(0xFE);
    expect(stack.logTransport.sourceAddress).toBe(0xFF);
    expect(stack.logs.maxChunk).toBe(253);
    expect(stack.logs.timeouts.block).toBe(10000);
  });

  it('allows explicit CANable log block and timeout overrides', () => {
    const stack = new CanableProtocolStack({ logs: { maxChunk: 128, timeouts: { block: 1234 } } });
    expect(stack.logs.maxChunk).toBe(128);
    expect(stack.logs.timeouts.block).toBe(1234);
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

  it('emits an error (not a throw) when a synthetic handset ping fails to send', async () => {
    vi.useFakeTimers();
    vi.setSystemTime(1000);
    const stack = new CanableProtocolStack({ handsetMissingAfterMs: 1000, handsetPingIntervalMs: 500 });
    const errSpy = vi.fn();
    stack.on('error', errSpy);
    stack.canable.sendFrame = vi.fn().mockRejectedValue(new Error('bus write failed'));
    stack._startHandsetGuardian();
    await vi.advanceTimersByTimeAsync(1000);
    expect(errSpy).toHaveBeenCalledTimes(1);
    expect(errSpy.mock.calls[0][0].message).toMatch(/bus write failed/);
    stack._stopHandsetGuardian();
    vi.useRealTimers();
  });
});

describe('CanableProtocolStack lifecycle & accessors', () => {
  it('forwards OTA recovery events', () => {
    const stack = new CanableProtocolStack({ handsetEmulation: false });
    const seen = [];
    stack.on('otaBlockRetry', p => seen.push(['block', p]));
    stack.on('otaStagingRetry', p => seen.push(['staging', p]));
    stack.on('otaStaleDownload', p => seen.push(['stale', p]));

    stack.ota.emit('blockRetry', { block: 2 });
    stack.ota.emit('stagingRetry', { attempt: 2 });
    stack.ota.emit('staleDownload', { waitMs: 10 });

    expect(seen).toEqual([
      ['block', { block: 2 }],
      ['staging', { attempt: 2 }],
      ['stale', { waitMs: 10 }]
    ]);
  });

  it('aborts pending UDS work and resets both transports when the serial link drops', () => {
    const stack = new CanableProtocolStack({ handsetEmulation: false });
    const abort = vi.spyOn(stack.uds, 'abortPending');
    const txReset = vi.spyOn(stack.transport, 'reset');
    const logReset = vi.spyOn(stack.logTransport, 'reset');
    const disconnected = vi.fn();
    stack.on('disconnected', disconnected);

    stack.canable.emit('disconnected');

    expect(abort).toHaveBeenCalledWith(expect.objectContaining({
      details: expect.objectContaining({ disconnected: true })
    }));
    expect(txReset).toHaveBeenCalledTimes(1);
    expect(logReset).toHaveBeenCalledTimes(1);
    expect(disconnected).toHaveBeenCalledTimes(1);
  });

  it('connect() opens the CAN link', async () => {
    const stack = new CanableProtocolStack({ handsetEmulation: false });
    stack.canable.connect = vi.fn().mockResolvedValue(undefined);
    await stack.connect();
    expect(stack.canable.connect).toHaveBeenCalledTimes(1);
  });

  it('disconnect() resets transports and closes the CAN link', async () => {
    const stack = new CanableProtocolStack({ handsetEmulation: false });
    stack.canable.disconnect = vi.fn().mockResolvedValue(undefined);
    const txReset = vi.spyOn(stack.transport, 'reset');
    const logReset = vi.spyOn(stack.logTransport, 'reset');
    await stack.disconnect();
    expect(txReset).toHaveBeenCalledTimes(1);
    expect(logReset).toHaveBeenCalledTimes(1);
    expect(stack.canable.disconnect).toHaveBeenCalledTimes(1);
  });

  it('setTargetAddress fans out to both transports', () => {
    const stack = new CanableProtocolStack({ handsetEmulation: false });
    stack.setTargetAddress(0x07);
    expect(stack.targetAddress).toBe(0x07);
    expect(stack.transport.targetAddress).toBe(0x07);
    expect(stack.logTransport.targetAddress).toBe(0x07);
  });

  it('reports connection state and info', () => {
    const stack = new CanableProtocolStack({ handsetEmulation: false });
    expect(stack.isConnected).toBe(false);
    expect(stack.connectionInfo).toBeNull();
    // Make the underlying CANable look connected (port + writer both set).
    stack.canable.port = {};
    stack.canable.writer = {};
    expect(stack.isConnected).toBe(true);
    expect(stack.connectionInfo).toEqual({
      device: 'CANable (Web Serial)',
      serialBaud: 115200,
      transportState: 'IDLE'
    });
  });

  it('exposes the ota, logs and uds sub-managers', () => {
    const stack = new CanableProtocolStack({ handsetEmulation: false });
    expect(stack.uds).toBeDefined();
    expect(stack.ota).toBeDefined();
    expect(stack.logs).toBeDefined();
  });

  it('delegates identity reads to the UDS client', async () => {
    const stack = new CanableProtocolStack({ handsetEmulation: false });
    stack.uds.readSerialNumber = vi.fn().mockResolvedValue('SN1');
    stack.uds.readFirmwareVersion = vi.fn().mockResolvedValue('v1');
    stack.uds.readVariantName = vi.fn().mockResolvedValue('Poseidon');
    stack.uds.readHardwareVersion = vi.fn().mockResolvedValue('rev2');
    expect(await stack.readSerialNumber()).toBe('SN1');
    expect(await stack.readFirmwareVersion()).toBe('v1');
    expect(await stack.readVariantName()).toBe('Poseidon');
    expect(await stack.readHardwareVersion()).toBe('rev2');
  });

  it('on()/off() add and remove listeners; a throwing handler is isolated', () => {
    const stack = new CanableProtocolStack({ handsetEmulation: false });
    const fn = vi.fn();
    stack.on('custom', fn);
    stack.off('custom', fn);
    stack.emit('custom', 1);
    expect(fn).not.toHaveBeenCalled();

    const errSpy = vi.spyOn(console, 'error').mockImplementation(() => {});
    stack.on('boom', () => { throw new Error('handler blew up'); });
    expect(() => stack.emit('boom')).not.toThrow();
    expect(errSpy).toHaveBeenCalled();
    errSpy.mockRestore();
  });
});
