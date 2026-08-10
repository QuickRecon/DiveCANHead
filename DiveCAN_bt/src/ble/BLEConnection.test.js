import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { BLEConnection } from './BLEConnection.js';
import { MockBluetoothRemoteGATTCharacteristic, MockBluetoothDevice, MockBluetooth } from '../../tests/mocks/MockBLE.js';

const DEVICE_INFO_SERVICE_UUID = '0000180a-0000-1000-8000-00805f9b34fb';

/** Install a navigator.bluetooth mock on the jsdom navigator (configurable so
 *  afterEach can delete it and not pollute other test files). */
function setBluetooth(mock) {
  Object.defineProperty(navigator, 'bluetooth', {
    value: mock,
    configurable: true,
    writable: true
  });
}

function clearBluetooth() {
  if ('bluetooth' in navigator) {
    delete navigator.bluetooth;
  }
}

/** Reassemble the fragments a mock characteristic captured back into the
 *  original SLIP stream: strip each [0x01,0x00] header and concatenate. */
function reassemble(writeQueue) {
  const parts = writeQueue.map(v => {
    const b = v instanceof Uint8Array ? v : new Uint8Array(v.buffer || v);
    return b.slice(2); // strip [0x01, 0x00] header
  });
  const total = parts.reduce((n, p) => n + p.length, 0);
  const out = new Uint8Array(total);
  let off = 0;
  for (const p of parts) { out.set(p, off); off += p.length; }
  return out;
}

describe('BLEConnection.write fragmentation', () => {
  let conn;
  let char;

  beforeEach(() => {
    // mtu 20 -> 18 bytes of SLIP payload per fragment; no pacing delay in tests
    conn = new BLEConnection({ mtu: 20, writeGapMs: 0 });
    char = new MockBluetoothRemoteGATTCharacteristic({}, 'test');
    conn.characteristic = char;
    conn._isConnected = true;
  });

  it('sends a small payload as a single header-prefixed fragment', async () => {
    const data = new Uint8Array([0x22, 0xF2, 0x00]);
    await conn.write(data);
    expect(char.writeQueue).toHaveLength(1);
    expect(Array.from(char.writeQueue[0])).toEqual([0x01, 0x00, 0x22, 0xF2, 0x00]);
  });

  it('fragments a large payload across the MTU, each with the [0x01,0x00] header', async () => {
    // A 262-byte SLIP stream (the OTA 0x36 case from the bug report)
    const data = new Uint8Array(262).map((_, i) => (i + 1) & 0xFF);
    await conn.write(data);

    // 262 bytes / 18 per fragment = 15 fragments
    expect(char.writeQueue).toHaveLength(Math.ceil(262 / 18));
    // Every fragment carries the header and fits within the MTU
    for (const v of char.writeQueue) {
      const b = v instanceof Uint8Array ? v : new Uint8Array(v);
      expect(b[0]).toBe(0x01);
      expect(b[1]).toBe(0x00);
      expect(b.length).toBeLessThanOrEqual(20);
    }
    // Reassembled fragments reconstruct the original stream exactly
    expect(Array.from(reassemble(char.writeQueue))).toEqual(Array.from(data));
  });

  it('throws when not connected', async () => {
    conn._isConnected = false;
    await expect(conn.write(new Uint8Array([1, 2, 3]))).rejects.toThrow(/Not connected/);
  });
});

describe('BLEConnection._handleData header stripping', () => {
  it('strips both 0x01 (fresh) and 0x02 (continuation) transport headers', () => {
    const conn = new BLEConnection();
    const seen = [];
    conn.on('data', (d) => seen.push(Array.from(d)));

    const mk = (hdr0, ...rest) => ({ buffer: new Uint8Array([hdr0, 0x00, ...rest]).buffer });
    conn._handleData(mk(0x01, 0xAA, 0xBB));
    conn._handleData(mk(0x02, 0xCC, 0xDD));

    expect(seen[0]).toEqual([0xAA, 0xBB]);
    expect(seen[1]).toEqual([0xCC, 0xDD]);
  });

  it('passes through payloads without a recognised transport header', () => {
    const conn = new BLEConnection();
    const seen = [];
    conn.on('data', (d) => seen.push(Array.from(d)));
    // Header byte 0x03 is not 0x01/0x02, so the whole buffer is emitted verbatim.
    conn._handleData({ buffer: new Uint8Array([0x03, 0x00, 0xAA]).buffer });
    conn._handleData({ buffer: new Uint8Array([0x99]).buffer });
    expect(seen[0]).toEqual([0x03, 0x00, 0xAA]);
    expect(seen[1]).toEqual([0x99]);
  });
});

describe('BLEConnection.isAvailable', () => {
  afterEach(() => {
    clearBluetooth();
  });

  it('returns true when navigator.bluetooth is present', () => {
    setBluetooth(new MockBluetooth());
    expect(BLEConnection.isAvailable()).toBe(true);
  });

  it('returns false when navigator.bluetooth is absent', () => {
    clearBluetooth();
    expect(BLEConnection.isAvailable()).toBe(false);
  });
});

describe('BLEConnection.scan', () => {
  let conn;

  beforeEach(() => {
    conn = new BLEConnection();
  });

  afterEach(() => {
    clearBluetooth();
    vi.useRealTimers();
  });

  it('throws a BLEError when Web Bluetooth is unavailable', async () => {
    clearBluetooth();
    await expect(conn.scan()).rejects.toThrow(/Web Bluetooth not available/);
  });

  it('returns the selected device and applies the default Petrel filter', async () => {
    const bt = new MockBluetooth();
    setBluetooth(bt);
    const devices = await conn.scan();
    expect(devices).toHaveLength(1);
    expect(devices[0]).toBeInstanceOf(MockBluetoothDevice);
    // Default filter targets the Petrel service UUID
    expect(bt.filters).toEqual([{ services: ['fe25c237-0ece-443c-b0aa-e02033e7029d'] }]);
  });

  it('merges caller-supplied scan options over the defaults', async () => {
    const bt = new MockBluetooth();
    setBluetooth(bt);
    await conn.scan({ filters: [{ name: 'Custom' }] });
    expect(bt.filters).toEqual([{ name: 'Custom' }]);
  });

  it('returns an empty array when the user cancels (NotFoundError)', async () => {
    const bt = new MockBluetooth();
    bt.requestDevice = async () => {
      const err = new Error('cancelled');
      err.name = 'NotFoundError';
      throw err;
    };
    setBluetooth(bt);
    const devices = await conn.scan();
    expect(devices).toEqual([]);
  });

  it('wraps other request errors in a BLEError', async () => {
    const bt = new MockBluetooth();
    bt.requestDevice = async () => { throw new Error('radio off'); };
    setBluetooth(bt);
    await expect(conn.scan()).rejects.toThrow(/Scan failed/);
  });

  it('rejects with a BLEError when the scan times out', async () => {
    vi.useFakeTimers();
    const bt = new MockBluetooth();
    // requestDevice never resolves, so the timeout race wins
    bt.requestDevice = () => new Promise(() => {});
    setBluetooth(bt);

    const p = conn.scan({}, 5000);
    const assertion = expect(p).rejects.toThrow(/Scan failed/);
    await vi.advanceTimersByTimeAsync(5000);
    await assertion;
  });
});

describe('BLEConnection.connect / disconnect', () => {
  let conn;

  afterEach(() => {
    clearBluetooth();
    vi.useRealTimers();
  });

  it('connects, discovers the service/characteristic and emits connected', async () => {
    conn = new BLEConnection();
    const device = new MockBluetoothDevice('Petrel 3', 'petrel-1');
    const connected = vi.fn();
    conn.on('connected', connected);

    await conn.connect(device);

    expect(conn._isConnected).toBe(true);
    expect(connected).toHaveBeenCalledTimes(1);
    expect(conn.isConnected).toBe(true);
    // Characteristic notifications were enabled
    expect(conn.characteristic.notificationsStarted).toBe(true);
  });

  it('routes characteristic notifications through _handleData to the data event', async () => {
    conn = new BLEConnection();
    const device = new MockBluetoothDevice();
    await conn.connect(device);

    const seen = [];
    conn.on('data', (d) => seen.push(Array.from(d)));
    conn.characteristic.simulateNotification(new Uint8Array([0x01, 0x00, 0x42]));
    expect(seen[0]).toEqual([0x42]);
  });

  it('is a no-op when already connected', async () => {
    conn = new BLEConnection();
    conn._isConnected = true;
    const connected = vi.fn();
    conn.on('connected', connected);
    await conn.connect(new MockBluetoothDevice());
    expect(connected).not.toHaveBeenCalled();
  });

  it('still connects when the optional model-number read fails', async () => {
    conn = new BLEConnection();
    const device = new MockBluetoothDevice();
    const origGet = device.gatt.getPrimaryService.bind(device.gatt);
    device.gatt.getPrimaryService = async (uuid) => {
      if (uuid === DEVICE_INFO_SERVICE_UUID) {
        throw new Error('no device info service');
      }
      return origGet(uuid);
    };
    await conn.connect(device);
    expect(conn._isConnected).toBe(true);
  });

  it('wraps a GATT failure in a BLEError and resets state', async () => {
    conn = new BLEConnection();
    const device = new MockBluetoothDevice();
    device.gatt.connect = async () => { throw new Error('gatt unreachable'); };

    await expect(conn.connect(device)).rejects.toThrow(/Connection failed/);
    expect(conn._isConnected).toBe(false);
    expect(conn.device).toBeNull();
    expect(conn.server).toBeNull();
    expect(conn.characteristic).toBeNull();
  });

  it('emits disconnected when the device fires gattserverdisconnected', async () => {
    conn = new BLEConnection();
    const device = new MockBluetoothDevice();
    const disconnected = vi.fn();
    conn.on('disconnected', disconnected);
    await conn.connect(device);

    device.dispatchEvent('gattserverdisconnected');
    expect(disconnected).toHaveBeenCalledTimes(1);
    expect(conn._isConnected).toBe(false);
  });

  it('keeps exactly one notification listener across reconnects', async () => {
    conn = new BLEConnection();
    const device = new MockBluetoothDevice();
    const data = vi.fn();
    conn.on('data', data);

    await conn.connect(device);
    device.dispatchEvent('gattserverdisconnected');
    await conn.connect(device);
    conn.characteristic.simulateNotification(new Uint8Array([0x01, 0x00, 0x42]));

    expect(data).toHaveBeenCalledTimes(1);
    expect(conn.characteristic.events.characteristicvaluechanged).toHaveLength(1);
    expect(device.events.gattserverdisconnected).toHaveLength(1);
  });

  it('disconnect() tears down the GATT server and emits disconnected', async () => {
    conn = new BLEConnection();
    const device = new MockBluetoothDevice();
    const disconnected = vi.fn();
    conn.on('disconnected', disconnected);
    await conn.connect(device);
    const server = conn.server;

    await conn.disconnect();
    expect(server.connected).toBe(false);
    expect(disconnected).toHaveBeenCalledTimes(1);
    expect(conn._isConnected).toBe(false);
  });

  it('disconnect() is a no-op when not connected', async () => {
    conn = new BLEConnection();
    const disconnected = vi.fn();
    conn.on('disconnected', disconnected);
    await conn.disconnect();
    expect(disconnected).not.toHaveBeenCalled();
  });

  it('swallows errors thrown while disconnecting the server', async () => {
    conn = new BLEConnection();
    const device = new MockBluetoothDevice();
    await conn.connect(device);
    conn.server.disconnect = () => { throw new Error('disconnect boom'); };
    // Should not reject despite the underlying throw
    await expect(conn.disconnect()).resolves.toBeUndefined();
    expect(conn._isConnected).toBe(false);
  });

  it('schedules an auto-reconnect after an unexpected disconnect', async () => {
    vi.useFakeTimers();
    conn = new BLEConnection({ autoReconnect: true });
    const device = new MockBluetoothDevice();
    await conn.connect(device);
    const connectSpy = vi.spyOn(conn, 'connect');

    // Unexpected drop
    device.dispatchEvent('gattserverdisconnected');
    expect(conn._isConnected).toBe(false);

    await vi.advanceTimersByTimeAsync(1000);
    expect(connectSpy).toHaveBeenCalledWith(device);
  });

  it('logs but does not throw when the auto-reconnect attempt fails', async () => {
    vi.useFakeTimers();
    conn = new BLEConnection({ autoReconnect: true });
    const device = new MockBluetoothDevice();
    await conn.connect(device);

    // Sabotage the GATT so the scheduled reconnect rejects
    device.gatt.connect = async () => { throw new Error('still unreachable'); };
    device.dispatchEvent('gattserverdisconnected');

    // Should resolve without an unhandled rejection escaping
    await vi.advanceTimersByTimeAsync(1000);
    expect(conn._isConnected).toBe(false);
  });
});

describe('BLEConnection.write pacing and error handling', () => {
  afterEach(() => {
    vi.useRealTimers();
  });

  it('paces multi-fragment writes by writeGapMs between fragments', async () => {
    vi.useFakeTimers();
    const conn = new BLEConnection({ mtu: 20, writeGapMs: 8 });
    const char = new MockBluetoothRemoteGATTCharacteristic({}, 'test');
    conn.characteristic = char;
    conn._isConnected = true;

    // 40 bytes -> 3 fragments (18/18/4), 2 inter-fragment gaps
    const data = new Uint8Array(40).map((_, i) => i & 0xFF);
    const p = conn.write(data);
    await vi.runAllTimersAsync();
    await p;

    expect(char.writeQueue).toHaveLength(Math.ceil(40 / 18));
  });

  it('wraps a characteristic write failure in a BLEError', async () => {
    const conn = new BLEConnection({ mtu: 20, writeGapMs: 0 });
    const char = new MockBluetoothRemoteGATTCharacteristic({}, 'test');
    char.writeValueWithoutResponse = async () => { throw new Error('gatt write error'); };
    conn.characteristic = char;
    conn._isConnected = true;

    await expect(conn.write(new Uint8Array([1, 2, 3]))).rejects.toThrow(/Write failed/);
  });
});

describe('BLEConnection getters', () => {
  it('mtu reflects the configured option', () => {
    expect(new BLEConnection({ mtu: 64 }).mtu).toBe(64);
    expect(new BLEConnection().mtu).toBe(20);
  });

  it('deviceInfo is null before a device is attached', () => {
    expect(new BLEConnection().deviceInfo).toBeNull();
  });

  it('deviceInfo reports id/name/connected once connected', async () => {
    const conn = new BLEConnection();
    const device = new MockBluetoothDevice('Petrel 3', 'petrel-42');
    await conn.connect(device);
    expect(conn.deviceInfo).toEqual({ id: 'petrel-42', name: 'Petrel 3', connected: true });
    clearBluetooth();
  });

  it('isConnected is false when the server is gone even if the flag is set', () => {
    const conn = new BLEConnection();
    conn._isConnected = true;
    conn.server = null;
    expect(conn.isConnected).toBeFalsy();
  });
});

describe('BLEConnection EventEmitter', () => {
  it('off() removes a previously registered listener', () => {
    const conn = new BLEConnection();
    const cb = vi.fn();
    conn.on('data', cb);
    conn.off('data', cb);
    conn.emit('data', [1]);
    expect(cb).not.toHaveBeenCalled();
  });

  it('off() is a no-op for an unknown event', () => {
    const conn = new BLEConnection();
    expect(conn.off('nope', () => {})).toBe(conn);
  });

  it('emit swallows errors thrown inside a handler', () => {
    const conn = new BLEConnection();
    const good = vi.fn();
    conn.on('data', () => { throw new Error('handler boom'); });
    conn.on('data', good);
    // Must not throw, and later handlers still run
    expect(() => conn.emit('data', [1])).not.toThrow();
    expect(good).toHaveBeenCalled();
  });

  it('emit is a no-op for an event with no listeners', () => {
    const conn = new BLEConnection();
    expect(() => conn.emit('unheard')).not.toThrow();
  });

  it('removeAllListeners clears one event or every event', () => {
    const conn = new BLEConnection();
    const a = vi.fn(), b = vi.fn();
    conn.on('data', a);
    conn.on('connected', b);
    conn.removeAllListeners('data');
    conn.emit('data');
    expect(a).not.toHaveBeenCalled();
    conn.removeAllListeners();
    conn.emit('connected');
    expect(b).not.toHaveBeenCalled();
  });
});
