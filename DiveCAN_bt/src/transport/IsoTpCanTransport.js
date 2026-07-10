class Emitter { constructor() { this.events = {}; } on(e, cb) { (this.events[e] ||= []).push(cb); return this; } off(e, cb) { if (this.events[e]) this.events[e] = this.events[e].filter(x => x !== cb); return this; } emit(e, ...a) { for (const cb of this.events[e] || []) cb(...a); } }
const MENU_ID = 0x0D0A0000;
/** DiveCAN padded ISO-TP over raw 29-bit CAN. */
export class IsoTpCanTransport extends Emitter {
  constructor(can, options = {}) { super(); this.can = can; this.sourceAddress = options.sourceAddress ?? 0xFF; this.targetAddress = options.targetAddress ?? 0x04; this.messageId = options.messageId ?? MENU_ID; this.fcTimeout = options.fcTimeout ?? 1500; this.rxTimeout = options.rxTimeout ?? 1500; this.rx = null; this.fcWaiter = null; this.sendChain = Promise.resolve(); can.on('frame', f => this.processFrame(f)); }
  setTargetAddress(a) { this.targetAddress = a; }
  _id(target = this.targetAddress, source = this.sourceAddress) { return this.messageId | ((target & 0x0F) << 8) | (source & 0xFF); }
  async send(payload) { const data = payload instanceof Uint8Array ? payload : new Uint8Array(payload); const op = this.sendChain.then(() => this._send(data)); this.sendChain = op.catch(() => {}); return op; }
  async _send(data) {
    if (data.length <= 6) { const f = new Uint8Array(8); f[0] = data.length + 1; f[1] = 0; f.set(data, 2); await this.can.sendFrame(this._id(), f); return; }
    const total = data.length + 1, first = new Uint8Array(8); first[0] = 0x10 | ((total >> 8) & 15); first[1] = total & 255; first[2] = 0; first.set(data.slice(0, 5), 3);
    const waiting = this._waitFC(); await this.can.sendFrame(this._id(), first); let fc = await waiting, offset = 5, seq = 1, count = 0;
    while (offset < data.length) { if (fc.blockSize && count >= fc.blockSize) { fc = await this._waitFC(); count = 0; } await this._delay(this._stmin(fc.stmin)); const n = Math.min(7, data.length - offset), f = new Uint8Array(8); f[0] = 0x20 | (seq & 15); f.set(data.slice(offset, offset + n), 1); await this.can.sendFrame(this._id(), f); offset += n; seq = (seq + 1) & 15; count++; }
  }
  processFrame({ id, data, extended = true, remote = false }) {
    // Addressing occupies the low 16 bits when the peer is the 0xFF client,
    // so compare only the base/message portion here (not DIVECAN_ID_MASK,
    // whose low-nibble target assumption is for ordinary DiveCAN nodes).
    if (!extended || remote || (id & 0x1FFF0000) !== this.messageId || ((id >> 8) & 15) !== (this.sourceAddress & 15)) return;
    const source = id & 255, pci = data[0] & 0xF0;
    if (pci === 0x30) { if (this.fcWaiter && (source === this.targetAddress || source === 0xFF)) { const w = this.fcWaiter, status = data[0] & 15; this._clearFC(); status === 0 ? w.resolve({ blockSize: data[1], stmin: data[2] }) : w.reject(new Error(`ISO-TP flow control status ${status}`)); } return; }
    if (source !== this.targetAddress && this.targetAddress !== 0xFF) return;
    if (pci === 0x00) { const length = (data[0] & 15) - 1; if (length >= 0) this.emit('message', data.slice(2, 2 + length)); }
    else if (pci === 0x10) { const total = (((data[0] & 15) << 8) | data[1]) - 1; this.rx = { total, bytes: [...data.slice(3, 3 + Math.min(5, total))], seq: 1, source }; this._armRx(); this.can.sendFrame(this._id(source), new Uint8Array([0x30, 0, 0])).catch(e => this.emit('error', e)); }
    else if (pci === 0x20 && this.rx && source === this.rx.source) { if ((data[0] & 15) !== this.rx.seq) { this._resetRx(new Error('ISO-TP sequence mismatch')); return; } const left = this.rx.total - this.rx.bytes.length; this.rx.bytes.push(...data.slice(1, 1 + Math.min(7, left))); this.rx.seq = (this.rx.seq + 1) & 15; if (this.rx.bytes.length >= this.rx.total) { const msg = new Uint8Array(this.rx.bytes.slice(0, this.rx.total)); this._resetRx(); this.emit('message', msg); } else this._armRx(); }
  }
  _waitFC() { if (this.fcWaiter) this.fcWaiter.reject(new Error('Flow-control waiter replaced')); return new Promise((resolve, reject) => { const timer = setTimeout(() => { this.fcWaiter = null; reject(new Error('ISO-TP flow-control timeout')); }, this.fcTimeout); this.fcWaiter = { resolve, reject, timer }; }); }
  _clearFC() { if (this.fcWaiter) clearTimeout(this.fcWaiter.timer); this.fcWaiter = null; }
  _armRx() { clearTimeout(this.rxTimer); this.rxTimer = setTimeout(() => this._resetRx(new Error('ISO-TP receive timeout')), this.rxTimeout); }
  _resetRx(error) { clearTimeout(this.rxTimer); this.rx = null; if (error) this.emit('error', error); }
  _stmin(v) { return v <= 0x7F ? v : (v >= 0xF1 && v <= 0xF9 ? 1 : 0); } _delay(ms) { return ms ? new Promise(r => setTimeout(r, ms)) : Promise.resolve(); }
  reset() { this._resetRx(); this._clearFC(); } get state() { return this.rx ? 'RECEIVING' : 'IDLE'; } get isIdle() { return !this.rx && !this.fcWaiter; }
}
