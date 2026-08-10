import { describe, it, expect, vi } from 'vitest';
import { DiveCANProtocolStack } from './DiveCANProtocolStack.js';
import { BT_CLIENT_ADDRESS, CONTROLLER_ADDRESS } from './divecan/DiveCANFramer.js';

const SLIP_END = 0xC0;

/** Build a controller->client DiveCAN datagram carrying a UDS payload */
function controllerDatagram(payload) {
  const datagram = new Uint8Array(4 + payload.length);
  datagram[0] = CONTROLLER_ADDRESS;
  datagram[1] = BT_CLIENT_ADDRESS;
  datagram[2] = (payload.length + 1) & 0xFF;
  datagram[3] = ((payload.length + 1) >> 8) & 0xFF;
  datagram.set(payload, 4);
  return datagram;
}

/** Let queued microtasks/macrotasks (async event handlers) drain */
function flushAsync() {
  return new Promise(resolve => setTimeout(resolve, 0));
}

describe('DiveCANProtocolStack layer wiring', () => {
  it('defaults to BT client source and controller target addressing', () => {
    const stack = new DiveCANProtocolStack();
    expect(stack.divecan.sourceAddress).toBe(BT_CLIENT_ADDRESS);
    expect(stack.divecan.targetAddress).toBe(CONTROLLER_ADDRESS);
    expect(stack.transport.sourceAddress).toBe(BT_CLIENT_ADDRESS);
    expect(stack.transport.targetAddress).toBe(CONTROLLER_ADDRESS);
  });

  it('honours source and target address overrides', () => {
    const stack = new DiveCANProtocolStack({ sourceAddress: 0xAA, targetAddress: 0x04 });
    expect(stack.divecan.sourceAddress).toBe(0xAA);
    expect(stack.divecan.targetAddress).toBe(0x04);
    expect(stack.transport.sourceAddress).toBe(0xAA);
    expect(stack.transport.targetAddress).toBe(0x04);
  });

  it('exposes each layer and feature manager through accessors', () => {
    const stack = new DiveCANProtocolStack();
    expect(stack.ble).toBeDefined();
    expect(stack.slip).toBeDefined();
    expect(stack.divecan).toBeDefined();
    expect(stack.transport).toBeDefined();
    expect(stack.uds).toBeDefined();
    expect(stack.ota).toBeDefined();
    expect(stack.logs).toBeDefined();
  });

  it('delivers BLE bytes up through SLIP and DiveCAN to the transport', () => {
    const stack = new DiveCANProtocolStack();
    const messages = [];
    const rawData = [];
    stack.transport.on('message', (m) => messages.push(m));
    stack.on('data', (d) => rawData.push(d));

    const udsPayload = new Uint8Array([0x62, 0xF1, 0x90, 0x41, 0x42]);
    const slipBytes = stack.slip.encode(controllerDatagram(udsPayload));
    stack.ble.emit('data', slipBytes);

    expect(messages).toHaveLength(1);
    expect(Array.from(messages[0])).toEqual(Array.from(udsPayload));
    expect(rawData).toHaveLength(1);
    expect(rawData[0].layer).toBe('BLE');
  });

  it('reassembles a datagram split across two BLE notifications', () => {
    const stack = new DiveCANProtocolStack();
    const messages = [];
    stack.transport.on('message', (m) => messages.push(m));

    const udsPayload = new Uint8Array([0x71, 0x01, 0xFF, 0x00]);
    const slipBytes = stack.slip.encode(controllerDatagram(udsPayload));
    stack.ble.emit('data', slipBytes.slice(0, 3));
    expect(messages).toHaveLength(0);

    stack.ble.emit('data', slipBytes.slice(3));
    expect(messages).toHaveLength(1);
    expect(Array.from(messages[0])).toEqual(Array.from(udsPayload));
  });

  it('emits error when the RX pipeline sees a malformed datagram', () => {
    const stack = new DiveCANProtocolStack();
    const errors = [];
    stack.on('error', (e) => errors.push(e));

    // A complete SLIP frame whose content is too short to be a DiveCAN datagram
    stack.ble.emit('data', new Uint8Array([0x01, 0x02, SLIP_END]));

    expect(errors).toHaveLength(1);
    expect(errors[0].message).toMatch(/too short/i);
  });

  it('wraps outbound UDS payloads in DiveCAN and SLIP before the BLE write', async () => {
    const stack = new DiveCANProtocolStack();
    stack.ble.write = vi.fn().mockResolvedValue(undefined);
    const framed = [];
    stack.on('frame', (f) => framed.push(f));

    const request = new Uint8Array([0x22, 0xF1, 0x90]);
    await stack.transport.send(request);
    await flushAsync();

    expect(stack.ble.write).toHaveBeenCalledTimes(1);
    const written = stack.ble.write.mock.calls[0][0];
    expect(written[written.length - 1]).toBe(SLIP_END);
    // Header: [source, target, lenLow, lenHigh] then the UDS payload
    expect(written[0]).toBe(BT_CLIENT_ADDRESS);
    expect(written[1]).toBe(CONTROLLER_ADDRESS);
    expect(written[2]).toBe(request.length + 1);
    expect(written[3]).toBe(0x00);
    expect(Array.from(written.slice(4, 4 + request.length))).toEqual(Array.from(request));

    expect(framed).toHaveLength(1);
    expect(framed[0].layer).toBe('Transport');
  });

  it('emits error when the BLE write fails', async () => {
    const stack = new DiveCANProtocolStack();
    stack.ble.write = vi.fn().mockRejectedValue(new Error('GATT write failed'));
    const errors = [];
    stack.on('error', (e) => errors.push(e));

    await expect(stack.transport.send(new Uint8Array([0x3E, 0x00])))
      .rejects.toThrow('GATT write failed');

    expect(errors).toHaveLength(1);
    expect(errors[0].message).toBe('GATT write failed');
  });

  it('forwards lifecycle events and resets lower layers on disconnect', () => {
    const stack = new DiveCANProtocolStack();
    const seen = [];
    stack.on('connected', () => seen.push('connected'));
    stack.on('disconnected', () => seen.push('disconnected'));
    const slipReset = vi.spyOn(stack.slip, 'reset');
    const transportReset = vi.spyOn(stack.transport, 'reset');

    stack.ble.emit('connected');
    stack.ble.emit('disconnected');

    expect(seen).toEqual(['connected', 'disconnected']);
    expect(slipReset).toHaveBeenCalled();
    expect(transportReset).toHaveBeenCalled();
  });

  it('forwards errors from the BLE, transport, and UDS layers', () => {
    const stack = new DiveCANProtocolStack();
    const errors = [];
    stack.on('error', (e) => errors.push(e));

    stack.ble.emit('error', new Error('ble'));
    stack.transport.emit('error', new Error('transport'));
    stack.uds.emit('error', new Error('uds'));

    // Transport errors surface twice: forwarded directly by the stack and
    // re-emitted through the UDS client's own transport error handler.
    expect(errors.map(e => e.message)).toEqual(
      expect.arrayContaining(['ble', 'transport', 'uds'])
    );
  });

  it('forwards UDS responses, NRCs, log messages, and unsolicited data', () => {
    const stack = new DiveCANProtocolStack();
    const seen = {};
    stack.on('udsResponse', (r) => { seen.response = r; });
    stack.on('udsNegativeResponse', (n) => { seen.nrc = n; });
    stack.on('logMessage', (m) => { seen.log = m; });
    stack.on('unsolicitedMessage', (d) => { seen.unsolicited = d; });

    stack.uds.emit('response', { sid: 0x62 });
    stack.uds.emit('negativeResponse', 0x31);
    stack.uds.emit('logMessage', 'hello from head');
    stack.uds.emit('unsolicitedMessage', new Uint8Array([0x7E, 0x00]));

    expect(seen.response).toEqual({ sid: 0x62 });
    expect(seen.nrc).toBe(0x31);
    expect(seen.log).toBe('hello from head');
    expect(Array.from(seen.unsolicited)).toEqual([0x7E, 0x00]);
  });

  it('forwards OTA and log download feature-manager events', () => {
    const stack = new DiveCANProtocolStack();
    const seen = [];
    stack.on('otaProgress', (p) => seen.push(['otaProgress', p]));
    stack.on('otaStaged', (p) => seen.push(['otaStaged', p]));
    stack.on('otaSessionExpired', () => seen.push(['otaSessionExpired']));
    stack.on('otaBlockRetry', (p) => seen.push(['otaBlockRetry', p]));
    stack.on('otaStagingRetry', (p) => seen.push(['otaStagingRetry', p]));
    stack.on('otaStaleDownload', (p) => seen.push(['otaStaleDownload', p]));
    stack.on('logProgress', (p) => seen.push(['logProgress', p]));
    stack.on('logDownloadDone', (p) => seen.push(['logDownloadDone', p]));

    stack.ota.emit('progress', { sent: 1 });
    stack.ota.emit('staged', { ok: true });
    stack.ota.emit('sessionExpired');
    stack.ota.emit('blockRetry', { block: 2 });
    stack.ota.emit('stagingRetry', { attempt: 2 });
    stack.ota.emit('staleDownload', { waitMs: 10 });
    stack.logs.emit('progress', { pct: 50 });
    stack.logs.emit('done', { bytes: 128 });

    expect(seen).toEqual([
      ['otaProgress', { sent: 1 }],
      ['otaStaged', { ok: true }],
      ['otaSessionExpired'],
      ['otaBlockRetry', { block: 2 }],
      ['otaStagingRetry', { attempt: 2 }],
      ['otaStaleDownload', { waitMs: 10 }],
      ['logProgress', { pct: 50 }],
      ['logDownloadDone', { bytes: 128 }]
    ]);
  });
});

describe('DiveCANProtocolStack connection management', () => {
  it('discoverDevices delegates to the BLE scanner', async () => {
    const stack = new DiveCANProtocolStack();
    const devices = [{ name: 'Petrel 3' }];
    stack.ble.scan = vi.fn().mockResolvedValue(devices);

    const result = await stack.discoverDevices({ acceptAllDevices: true }, 5000);

    expect(result).toBe(devices);
    expect(stack.ble.scan).toHaveBeenCalledWith({ acceptAllDevices: true }, 5000);
  });

  it('connect delegates to the BLE layer', async () => {
    const stack = new DiveCANProtocolStack();
    stack.ble.connect = vi.fn().mockResolvedValue(undefined);
    const device = { name: 'Petrel 3' };

    await stack.connect(device);

    expect(stack.ble.connect).toHaveBeenCalledWith(device);
  });

  it('connect rethrows BLE failures', async () => {
    const stack = new DiveCANProtocolStack();
    stack.ble.connect = vi.fn().mockRejectedValue(new Error('gatt refused'));

    await expect(stack.connect({ name: 'Petrel 3' })).rejects.toThrow('gatt refused');
  });

  it('disconnect delegates to the BLE layer', async () => {
    const stack = new DiveCANProtocolStack();
    stack.ble.disconnect = vi.fn().mockResolvedValue(undefined);

    await stack.disconnect();

    expect(stack.ble.disconnect).toHaveBeenCalled();
  });

  it('setTargetAddress retargets the DiveCAN framer', () => {
    const stack = new DiveCANProtocolStack();
    stack.setTargetAddress(0x04);
    expect(stack.targetAddress).toBe(0x04);
    expect(stack.divecan.targetAddress).toBe(0x04);
  });

  it('reports disconnected state with null connection info', () => {
    const stack = new DiveCANProtocolStack();
    expect(stack.isConnected).toBeFalsy();
    expect(stack.connectionInfo).toBeNull();
  });

  it('reports device info and transport state when connected', () => {
    const stack = new DiveCANProtocolStack();
    stack.ble.device = { id: 'dev-1', name: 'Petrel 3' };
    stack.ble.server = { connected: true };
    stack.ble._isConnected = true;

    expect(stack.isConnected).toBe(true);
    expect(stack.connectionInfo).toEqual({
      device: { id: 'dev-1', name: 'Petrel 3', connected: true },
      transportState: 'IDLE'
    });
  });
});

describe('DiveCANProtocolStack UDS convenience methods', () => {
  it('delegates identity reads to the UDS client', async () => {
    const stack = new DiveCANProtocolStack();
    stack.uds.readSerialNumber = vi.fn().mockResolvedValue('SN123');
    stack.uds.readFirmwareVersion = vi.fn().mockResolvedValue('1.2.3');
    stack.uds.readVariantName = vi.fn().mockResolvedValue('Poseidon_Aren');
    stack.uds.readHardwareVersion = vi.fn().mockResolvedValue(2);

    expect(await stack.readSerialNumber()).toBe('SN123');
    expect(await stack.readFirmwareVersion()).toBe('1.2.3');
    expect(await stack.readVariantName()).toBe('Poseidon_Aren');
    expect(await stack.readHardwareVersion()).toBe(2);
  });

  it('delegates settings reads to the UDS client', async () => {
    const stack = new DiveCANProtocolStack();
    stack.uds.getSettingCount = vi.fn().mockResolvedValue(4);
    stack.uds.getSettingInfo = vi.fn().mockResolvedValue({ label: 'Setpoint', kind: 1, editable: true });
    stack.uds.getSettingValue = vi.fn().mockResolvedValue({ maxValue: 10n, currentValue: 7n });
    stack.uds.getSettingOptionLabel = vi.fn().mockResolvedValue('High');
    stack.uds.enumerateSettings = vi.fn().mockResolvedValue([{ index: 0 }]);

    expect(await stack.getSettingCount()).toBe(4);
    expect(await stack.getSettingInfo(2)).toEqual({ label: 'Setpoint', kind: 1, editable: true });
    expect(stack.uds.getSettingInfo).toHaveBeenCalledWith(2);
    expect(await stack.getSettingValue(2)).toEqual({ maxValue: 10n, currentValue: 7n });
    expect(await stack.getSettingOptionLabel(2, 1)).toBe('High');
    expect(stack.uds.getSettingOptionLabel).toHaveBeenCalledWith(2, 1);
    expect(await stack.enumerateSettings()).toEqual([{ index: 0 }]);
  });

  it('delegates settings writes to the UDS client', async () => {
    const stack = new DiveCANProtocolStack();
    stack.uds.writeSettingValue = vi.fn().mockResolvedValue(undefined);
    stack.uds.saveSetting = vi.fn().mockResolvedValue(undefined);

    await stack.writeSettingValue(3, 5n);
    await stack.saveSetting(3, 5n);

    expect(stack.uds.writeSettingValue).toHaveBeenCalledWith(3, 5n);
    expect(stack.uds.saveSetting).toHaveBeenCalledWith(3, 5n);
  });
});

describe('DiveCANProtocolStack event emitter', () => {
  it('off removes a previously registered listener', () => {
    const stack = new DiveCANProtocolStack();
    const listener = vi.fn();
    stack.on('connected', listener);
    stack.off('connected', listener);
    stack.ble.emit('connected');
    expect(listener).not.toHaveBeenCalled();
  });

  it('off on an event with no listeners is a no-op', () => {
    const stack = new DiveCANProtocolStack();
    expect(stack.off('never-registered', () => {})).toBe(stack);
  });

  it('removeAllListeners clears a single event or every event', () => {
    const stack = new DiveCANProtocolStack();
    const connected = vi.fn();
    const disconnected = vi.fn();
    stack.on('connected', connected);
    stack.on('disconnected', disconnected);

    stack.removeAllListeners('connected');
    stack.ble.emit('connected');
    stack.ble.emit('disconnected');
    expect(connected).not.toHaveBeenCalled();
    expect(disconnected).toHaveBeenCalledTimes(1);

    stack.removeAllListeners();
    stack.ble.emit('disconnected');
    expect(disconnected).toHaveBeenCalledTimes(1);
  });

  it('survives a throwing event handler and still calls later listeners', () => {
    const stack = new DiveCANProtocolStack();
    const consoleError = vi.spyOn(console, 'error').mockImplementation(() => {});
    const second = vi.fn();
    stack.on('connected', () => { throw new Error('listener exploded'); });
    stack.on('connected', second);

    stack.ble.emit('connected');

    expect(second).toHaveBeenCalledTimes(1);
    expect(consoleError).toHaveBeenCalled();
    consoleError.mockRestore();
  });
});
