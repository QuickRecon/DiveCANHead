import { CanableConnection } from './can/CanableConnection.js';
import { IsoTpCanTransport } from './transport/IsoTpCanTransport.js';
import { UDSClient } from './uds/UDSClient.js';
import { OTAManager } from './firmware/OTAManager.js';
import { LogDownloader } from './logs/LogDownloader.js';

/** Protocol stack for direct USB-CAN access through an slcan CANable. */
export class CanableProtocolStack {
  constructor(options = {}) {
    this.events = {};
    this._can = new CanableConnection(options.canable);
    this._transport = new IsoTpCanTransport(this._can, {
      // Keep addressed UDS traffic off the permanent 0xFF log-push channel.
      sourceAddress: options.sourceAddress ?? 0xFE,
      targetAddress: options.targetAddress ?? 0x04,
      ...(options.transport || {})
    });
    this._logTransport = new IsoTpCanTransport(this._can, {
      sourceAddress: options.logAddress ?? 0xFF,
      targetAddress: options.targetAddress ?? 0x04,
      ...(options.logTransport || {})
    });
    this._uds = new UDSClient(this._transport, options.uds);
    this._ota = new OTAManager(this._uds, options.ota);
    this._logs = new LogDownloader(this._uds, options.logs);
    this._handset = {
      enabled: options.handsetEmulation !== false,
      missingAfterMs: options.handsetMissingAfterMs ?? 2500,
      pingIntervalMs: options.handsetPingIntervalMs ?? 1000,
      lastRealPingAt: Date.now(),
      emulating: false,
      timer: null
    };
    this._can.on('frame', frame => this._observeHandsetFrame(frame));
    for (const event of ['connected', 'disconnected', 'error']) this._can.on(event, (...a) => this.emit(event, ...a));
    this._transport.on('error', e => this.emit('error', e));
    this._logTransport.on('error', e => this.emit('error', e));
    this._logTransport.on('message', message => this._uds.processUnsolicited(message));
    this._uds.on('error', e => this.emit('error', e));
    this._uds.on('response', r => this.emit('udsResponse', r));
    this._uds.on('negativeResponse', r => this.emit('udsNegativeResponse', r));
    this._uds.on('logMessage', m => this.emit('logMessage', m));
    this._uds.on('unsolicitedMessage', m => this.emit('unsolicitedMessage', m));
    this._ota.on('progress', p => this.emit('otaProgress', p));
    this._ota.on('staged', p => this.emit('otaStaged', p));
    this._ota.on('sessionExpired', () => this.emit('otaSessionExpired'));
    this._logs.on('progress', p => this.emit('logProgress', p));
    this._logs.on('done', p => this.emit('logDownloadDone', p));
  }
  on(e, cb) { (this.events[e] ||= []).push(cb); return this; }
  off(e, cb) { if (this.events[e]) this.events[e] = this.events[e].filter(x => x !== cb); return this; }
  emit(e, ...a) { for (const cb of this.events[e] || []) { try { cb(...a); } catch (error) { console.error(`Handler for ${e} failed`, error); } } }
  async connect(port = null) { await this._can.connect(port); this._startHandsetGuardian(); }
  async disconnect() { this._stopHandsetGuardian(); this._transport.reset(); this._logTransport.reset(); await this._can.disconnect(); }
  setTargetAddress(address) { this._transport.setTargetAddress(address); this._logTransport.setTargetAddress(address); }
  get targetAddress() { return this._transport.targetAddress; }
  get isConnected() { return this._can.isConnected; }
  get connectionInfo() { return this.isConnected ? { device: 'CANable (Web Serial)', transportState: this._transport.state } : null; }
  get canable() { return this._can; } get transport() { return this._transport; } get logTransport() { return this._logTransport; } get uds() { return this._uds; }
  get ota() { return this._ota; } get logs() { return this._logs; }
  async readSerialNumber() { return this._uds.readSerialNumber(); }
  async readFirmwareVersion() { return this._uds.readFirmwareVersion(); }
  async readVariantName() { return this._uds.readVariantName(); }
  async readHardwareVersion() { return this._uds.readHardwareVersion(); }

  _observeHandsetFrame({ id, extended = true, remote = false }) {
    // Controller BUS_ID: base 0x0D000000, sender in the low nibble = 1.
    if (extended && !remote && (id & 0x1FFFF000) === 0x0D000000 && (id & 0x0F) === 0x01) {
      this._handset.lastRealPingAt = Date.now();
      if (this._handset.emulating) {
        this._handset.emulating = false;
        this.emit('handsetEmulation', { active: false, reason: 'real-handset-ping' });
      }
    }
  }

  _startHandsetGuardian() {
    if (!this._handset.enabled || this._handset.timer) return;
    this._handset.lastRealPingAt = Date.now();
    this._handset.timer = setInterval(() => this._handsetGuardianTick(),
      Math.min(this._handset.pingIntervalMs, 500));
  }

  async _handsetGuardianTick() {
    const now = Date.now();
    if (!this._handset.emulating) {
      if ((now - this._handset.lastRealPingAt) < this._handset.missingAfterMs) return;
      this._handset.emulating = true;
      this._handset.lastSyntheticPingAt = 0;
      this.emit('handsetEmulation', { active: true, reason: 'handset-ping-timeout' });
    }
    if ((now - this._handset.lastSyntheticPingAt) < this._handset.pingIntervalMs) return;
    this._handset.lastSyntheticPingAt = now;
    try {
      // Normal controller BUS_ID shape: SRI manufacturer, reserved byte,
      // firmware byte. The head uses the ID/source as liveness evidence and
      // does not inspect these bytes before responding.
      await this._can.sendFrame(0x0D000001, new Uint8Array([0x01, 0x00, 0x00]));
      this.emit('handsetPing', { synthetic: true, timestamp: now });
    } catch (error) {
      this.emit('error', error);
    }
  }

  _stopHandsetGuardian() {
    if (this._handset.timer) clearInterval(this._handset.timer);
    this._handset.timer = null;
    this._handset.emulating = false;
  }
}
