/**
 * MockSerial - Mock Web Serial API for testing CanableConnection.
 *
 * Provides a MockSerialPort with readable/writable streams whose reader/writer
 * mirror the pieces of the Web Serial contract CanableConnection actually uses:
 * open/close on the port, getWriter().write()/releaseLock(), and
 * getReader().read()/cancel()/releaseLock(). The reader is a small async queue
 * so a test can push received bytes and observe them flow through _readLoop.
 */

export class MockSerialWriter {
  constructor() {
    this.written = [];
    this.locked = true;
  }

  async write(chunk) {
    this.written.push(chunk);
  }

  releaseLock() {
    this.locked = false;
  }

  /** Decode every written chunk back to a single string (TextEncoder output). */
  writtenText() {
    const dec = new TextDecoder();
    return this.written.map(c => dec.decode(c)).join('');
  }
}

export class MockSerialReader {
  constructor() {
    this.queue = [];
    this.pending = [];
    this.closed = false;
    this.locked = true;
  }

  read() {
    if (this.queue.length) {
      return Promise.resolve(this.queue.shift());
    }
    if (this.closed) {
      return Promise.resolve({ value: undefined, done: true });
    }
    return new Promise(resolve => this.pending.push(resolve));
  }

  /** Feed bytes (Uint8Array) to a waiting or future read(). */
  push(value) {
    const item = { value, done: false };
    if (this.pending.length) {
      this.pending.shift()(item);
    } else {
      this.queue.push(item);
    }
  }

  close() {
    this.closed = true;
    while (this.pending.length) {
      this.pending.shift()({ value: undefined, done: true });
    }
  }

  async cancel() {
    this.close();
  }

  releaseLock() {
    this.locked = false;
  }
}

export class MockSerialPort {
  constructor() {
    this.opened = false;
    this.closed = false;
    this.openOptions = null;
    this.writer = new MockSerialWriter();
    this.reader = new MockSerialReader();
    this.writable = { getWriter: () => this.writer };
    this.readable = { getReader: () => this.reader };
  }

  async open(options) {
    this.opened = true;
    this.openOptions = options;
  }

  async close() {
    this.closed = true;
    this.opened = false;
  }
}

export class MockSerial {
  constructor(port = null) {
    this.port = port || new MockSerialPort();
    this.requestedOptions = null;
  }

  async requestPort(options) {
    this.requestedOptions = options;
    return this.port;
  }
}
