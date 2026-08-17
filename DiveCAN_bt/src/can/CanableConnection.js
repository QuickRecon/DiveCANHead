// Web Serial connection for CANable adapters running slcan/Lawicel firmware.
export class CanableConnection {
  constructor(options = {}) { this.options = { baudRate: 115200, canSpeed: 'S4', verifyConnection: false, verifyTimeoutMs: 1000, verifySettleMs: 50, ...options }; this.events = {}; this.port = null; this.reader = null; this.writer = null; this.line = ''; this.connecting = false; this.readError = null; }
  on(e, cb) {
    if (!this.events[e]) { this.events[e] = []; }
    this.events[e].push(cb);
    return this;
  }
  off(e, cb) {
    if (this.events[e]) { this.events[e] = this.events[e].filter(x => x !== cb); }
    return this;
  }
  emit(e, ...args) { for (const cb of this.events[e] || []) cb(...args); }
  static isSupported() { return typeof navigator !== 'undefined' && navigator.serial !== undefined; }
  async requestPort(filters = []) {
    if (!CanableConnection.isSupported()) { throw new Error('Web Serial is not available'); }
    return navigator.serial.requestPort(filters.length ? { filters } : {});
  }
  async connect(port = null) {
    if (this.isConnected) return;
    this.connecting = true;
    this.readError = null;
    try {
      this.port = port || await this.requestPort(this.options.filters || []);
      await this.port.open({ baudRate: this.options.baudRate });
      this.writer = this.port.writable.getWriter();
      this.reader = this.port.readable.getReader();
      this.readTask = this._readLoop();
      await this.command('\r');
      await this.command('C\r');
      await this.command(`${this.options.canSpeed}\r`);
      if (this.options.verifyConnection) await this._verifyConnection();
      if (this.readError) throw this.readError;
      await this.command('O\r');
      if (this.options.verifyConnection && this.options.verifySettleMs > 0) {
        await new Promise(resolve => setTimeout(resolve, this.options.verifySettleMs));
      }
      if (this.readError) throw this.readError;
      this.emit('connected');
    } catch (error) {
      await this.disconnect();
      throw error;
    } finally {
      this.connecting = false;
    }
  }

  async _verifyConnection() {
    return new Promise((resolve, reject) => {
      const cleanup = () => {
        clearTimeout(timer);
        this.off('line', onLine);
        this.off('error', onError);
      };
      const onLine = (line) => {
        // The stock CANable2 V command returns a git revision/path. Some
        // firmware revisions also emit "0000" for other utility commands;
        // do not mistake that stale line for the version response.
        if (!line || line === '0000' || /^[TtRr][0-9A-F]/.test(line)) return;
        cleanup();
        resolve(line);
      };
      const onError = (error) => { cleanup(); reject(error); };
      const timer = setTimeout(() => {
        cleanup();
        reject(new Error(
          `CANable did not respond at ${this.options.baudRate} baud; reconnect the adapter or try 115200`));
      }, this.options.verifyTimeoutMs);
      this.on('line', onLine);
      this.on('error', onError);
      this.command('V\r').catch(onError);
    });
  }
  async command(text) {
    if (!this.writer) { throw new Error('CANable is not connected'); }
    await this.writer.write(new TextEncoder().encode(text));
  }
  async sendFrame(id, data, extended = true) {
    const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
    if (bytes.length > 8) throw new RangeError('Classic CAN frames are limited to 8 data bytes');
    const width = extended ? 8 : 3, prefix = extended ? 'T' : 't';
    const canId = id.toString(16).toUpperCase().padStart(width, '0');
    const payload = [...bytes].map(b => b.toString(16).toUpperCase().padStart(2, '0')).join('');
    await this.command(`${prefix}${canId}${bytes.length.toString(16).toUpperCase()}${payload}\r`);
  }
  async _readLoop() {
    const decoder = new TextDecoder();
    let linkLost = false;
    try {
      while (this.reader) {
        const { value, done } = await this.reader.read();
        if (done) { linkLost = this.reader !== null; break; }
        this._consume(decoder.decode(value, { stream: true }));
      }
    } catch (e) {
      if (this.reader) { this.readError = e; this.emit('error', e); linkLost = true; }
    } finally {
      // A spontaneous EOF/read failure is a real transport disconnect. Defer
      // cleanup until this read task has settled so disconnect() never waits on
      // itself; explicit disconnect sets reader=null first and skips this path.
      if (linkLost && !this.connecting) {
        setTimeout(() => this.disconnect().catch(error => this.emit('error', error)), 0);
      }
    }
  }
  // '\x07' is the slcan BEL error byte; JS has no '\a' escape (it would just be the letter 'a').
  _consume(text) { for (const ch of text) { if (ch === '\r' || ch === '\x07') { const line = this.line; this.line = ''; if (ch === '\x07') this.emit('error', new Error('CANable rejected command')); else if (line) { this.emit('line', line); this._parseFrame(line); } } else if (ch !== '\n') this.line += ch; } }
  _parseFrame(line) {
    const extended = line[0] === 'T' || line[0] === 'R'; if (!extended && line[0] !== 't' && line[0] !== 'r') return;
    const remote = line[0] === 'R' || line[0] === 'r', width = extended ? 8 : 3;
    const id = Number.parseInt(line.slice(1, 1 + width), 16), dlc = Number.parseInt(line[1 + width], 16);
    const hex = line.slice(2 + width, 2 + width + (remote ? 0 : dlc * 2)); if (!Number.isFinite(id) || !Number.isFinite(dlc) || (!remote && hex.length !== dlc * 2)) return;
    const data = new Uint8Array(dlc); for (let i = 0; i < dlc; i++) data[i] = Number.parseInt(hex.slice(i * 2, i * 2 + 2), 16);
    this.emit('frame', { id, data, extended, remote });
  }
  async disconnect() {
    const reader = this.reader;
    this.reader = null;
    if (reader) {
      try { await reader.cancel(); } catch {}
      try { reader.releaseLock(); } catch {}
    }
    if (this.readTask) {
      try { await this.readTask; } catch {}
      this.readTask = null;
    }
    if (this.writer) {
      try { this.writer.releaseLock(); } catch {}
      this.writer = null;
    }
    const port = this.port;
    this.port = null;
    if (port) {
      // A physically removed Web Serial device commonly rejects close() with
      // the same NetworkError that ended read(). Cleanup is already complete;
      // never let that secondary error suppress the disconnected lifecycle.
      try { await port.close(); } catch {}
    }
    this.emit('disconnected');
  }
  get isConnected() { return this.port !== null && this.writer !== null; }
}
