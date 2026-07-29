import { describe, it, expect, vi, afterEach } from 'vitest';
import { CanableConnection } from './CanableConnection.js';
import { MockSerialPort, MockSerial } from '../../tests/mocks/MockSerial.js';

const flush = () => new Promise(r => setTimeout(r, 0));

function setSerial(mock) {
  Object.defineProperty(navigator, 'serial', {
    value: mock,
    configurable: true,
    writable: true
  });
}

function clearSerial() {
  if ('serial' in navigator) {
    delete navigator.serial;
  }
}

describe('CanableConnection', () => {
  it('encodes extended slcan frames', async () => {
    const c = new CanableConnection(); c.writer = { write: vi.fn() };
    await c.sendFrame(0x0D0A04FF, new Uint8Array([2, 0, 0x10]));
    expect(new TextDecoder().decode(c.writer.write.mock.calls[0][0])).toBe('T0D0A04FF3020010\r');
  });
  it('parses extended slcan frames', () => {
    const c = new CanableConnection(), frames = []; c.on('frame', f => frames.push(f));
    c._consume('T0D0AFF04430000000\r');
    expect(frames[0]).toMatchObject({ id: 0x0D0AFF04, extended: true });
    expect([...frames[0].data]).toEqual([0x30, 0, 0, 0]);
  });
});

describe('CanableConnection event emitter', () => {
  it('off() removes a listener and is chainable/no-op for unknown events', () => {
    const c = new CanableConnection();
    const cb = vi.fn();
    c.on('frame', cb);
    expect(c.off('frame', cb)).toBe(c);
    c.emit('frame', {});
    expect(cb).not.toHaveBeenCalled();
    // Unknown event: no throw, returns this
    expect(c.off('nope', cb)).toBe(c);
  });
});

describe('CanableConnection.isSupported / requestPort', () => {
  afterEach(() => {
    clearSerial();
  });

  it('reports support based on navigator.serial presence', () => {
    clearSerial();
    expect(CanableConnection.isSupported()).toBe(false);
    setSerial(new MockSerial());
    expect(CanableConnection.isSupported()).toBe(true);
  });

  it('requestPort throws when Web Serial is unavailable', async () => {
    clearSerial();
    const c = new CanableConnection();
    await expect(c.requestPort()).rejects.toThrow(/Web Serial is not available/);
  });

  it('requestPort forwards filters when supplied, empty object otherwise', async () => {
    const serial = new MockSerial();
    setSerial(serial);
    const c = new CanableConnection();

    await c.requestPort();
    expect(serial.requestedOptions).toEqual({});

    await c.requestPort([{ usbVendorId: 0x1209 }]);
    expect(serial.requestedOptions).toEqual({ filters: [{ usbVendorId: 0x1209 }] });
  });
});

describe('CanableConnection.sendFrame', () => {
  it('encodes standard (11-bit) frames with the lowercase prefix', async () => {
    const c = new CanableConnection(); c.writer = { write: vi.fn() };
    await c.sendFrame(0x123, new Uint8Array([0xAB, 0xCD]), false);
    expect(new TextDecoder().decode(c.writer.write.mock.calls[0][0])).toBe('t1232ABCD\r');
  });

  it('accepts a plain array as the data payload', async () => {
    const c = new CanableConnection(); c.writer = { write: vi.fn() };
    await c.sendFrame(0x1, [0x00], false);
    expect(new TextDecoder().decode(c.writer.write.mock.calls[0][0])).toBe('t001100\r');
  });

  it('rejects payloads longer than 8 bytes', async () => {
    const c = new CanableConnection(); c.writer = { write: vi.fn() };
    await expect(c.sendFrame(0x1, new Uint8Array(9))).rejects.toThrow(RangeError);
  });
});

describe('CanableConnection.command', () => {
  it('throws when there is no writer', async () => {
    const c = new CanableConnection();
    await expect(c.command('O\r')).rejects.toThrow(/not connected/);
  });
});

describe('CanableConnection._parseFrame / _consume', () => {
  it('parses standard data frames', () => {
    const c = new CanableConnection(), frames = [];
    c.on('frame', f => frames.push(f));
    c._consume('t1232ABCD\r');
    expect(frames[0]).toMatchObject({ id: 0x123, extended: false, remote: false });
    expect([...frames[0].data]).toEqual([0xAB, 0xCD]);
  });

  it('parses remote extended frames (R) with no data bytes', () => {
    const c = new CanableConnection(), frames = [];
    c.on('frame', f => frames.push(f));
    c._consume('R0D0AFF042\r');
    expect(frames[0]).toMatchObject({ id: 0x0D0AFF04, extended: true, remote: true });
    expect([...frames[0].data]).toEqual([0, 0]);
  });

  it('parses remote standard frames (r)', () => {
    const c = new CanableConnection(), frames = [];
    c.on('frame', f => frames.push(f));
    c._consume('r1234\r');
    expect(frames[0]).toMatchObject({ id: 0x123, extended: false, remote: true });
  });

  it('ignores lines that are not frame records', () => {
    const c = new CanableConnection(), frames = [];
    c.on('frame', f => frames.push(f));
    c._consume('z0000\r');   // unknown record type
    c._consume('\r');        // empty line
    expect(frames).toHaveLength(0);
  });

  it('drops frames whose payload length disagrees with the DLC', () => {
    const c = new CanableConnection(), frames = [];
    c.on('frame', f => frames.push(f));
    // DLC says 4 bytes but only 2 hex bytes present
    c._consume('t1234ABCD\r');
    expect(frames).toHaveLength(0);
  });

  it('emits an error on the slcan BEL byte and clears the pending line', () => {
    const c = new CanableConnection(), errors = [];
    c.on('error', e => errors.push(e));
    c._consume('garbage\x07');
    expect(errors).toHaveLength(1);
    expect(errors[0].message).toMatch(/rejected command/);
    expect(c.line).toBe('');
  });

  it('accumulates across chunks and ignores stray newlines', () => {
    const c = new CanableConnection(), frames = [];
    c.on('frame', f => frames.push(f));
    c._consume('t123');
    c._consume('2AB');
    c._consume('CD\r\n');
    expect(frames).toHaveLength(1);
    expect([...frames[0].data]).toEqual([0xAB, 0xCD]);
  });
});

describe('CanableConnection.connect / disconnect', () => {
  afterEach(() => {
    clearSerial();
  });

  it('opens the port, sends the slcan init sequence and emits connected', async () => {
    const port = new MockSerialPort();
    const c = new CanableConnection();
    const connected = vi.fn();
    c.on('connected', connected);

    await c.connect(port);

    expect(port.opened).toBe(true);
    expect(port.openOptions).toEqual({ baudRate: 115200 });
    expect(c.isConnected).toBe(true);
    expect(connected).toHaveBeenCalledTimes(1);
    // '\r', close, speed (S4), open
    expect(port.writer.writtenText()).toBe('\rC\rS4\rO\r');

    await c.disconnect();
  });

  it('acquires a port via requestPort when none is passed', async () => {
    const serial = new MockSerial();
    setSerial(serial);
    const c = new CanableConnection();
    await c.connect();
    expect(c.port).toBe(serial.port);
    await c.disconnect();
  });

  it('is a no-op when already connected', async () => {
    const port = new MockSerialPort();
    const c = new CanableConnection();
    await c.connect(port);
    const writesBefore = port.writer.written.length;
    await c.connect(port);
    expect(port.writer.written.length).toBe(writesBefore);
    await c.disconnect();
  });

  it('disconnects and rethrows if the init sequence fails', async () => {
    const port = new MockSerialPort();
    const c = new CanableConnection();
    // Make the very first init command fail
    port.writer.write = async () => { throw new Error('write failed'); };

    await expect(c.connect(port)).rejects.toThrow(/write failed/);
    expect(c.isConnected).toBe(false);
    expect(port.closed).toBe(true);
  });

  it('receives frames read off the port through _readLoop', async () => {
    const port = new MockSerialPort();
    const c = new CanableConnection();
    const frames = [];
    c.on('frame', f => frames.push(f));
    await c.connect(port);

    port.reader.push(new TextEncoder().encode('t1232ABCD\r'));
    await flush();

    expect(frames).toHaveLength(1);
    expect([...frames[0].data]).toEqual([0xAB, 0xCD]);

    await c.disconnect();
  });

  it('emits an error when the read loop throws while still active', async () => {
    const port = new MockSerialPort();
    const c = new CanableConnection();
    const errors = [];
    c.on('error', e => errors.push(e));
    await c.connect(port);

    // Force the in-flight read() to reject
    const pending = port.reader.pending.shift();
    expect(pending).toBeTypeOf('function');
    // Replace read so the loop re-enters and throws
    port.reader.read = () => Promise.reject(new Error('device unplugged'));
    pending({ value: new TextEncoder().encode(''), done: false });
    await flush();

    expect(errors.some(e => /device unplugged/.test(e.message))).toBe(true);

    await c.disconnect();
  });

  it('disconnect is idempotent and always emits disconnected', async () => {
    const port = new MockSerialPort();
    const c = new CanableConnection();
    const disconnected = vi.fn();
    c.on('disconnected', disconnected);
    await c.connect(port);

    await c.disconnect();
    expect(c.isConnected).toBe(false);
    expect(port.closed).toBe(true);
    expect(disconnected).toHaveBeenCalledTimes(1);

    // Second disconnect: no port/reader/writer left, still emits
    await c.disconnect();
    expect(disconnected).toHaveBeenCalledTimes(2);
  });
});
