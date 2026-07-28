import { describe, it, expect, vi } from 'vitest';
import { IsoTpCanTransport } from './IsoTpCanTransport.js';
class Can { constructor() { this.events = {}; this.sent = []; } on(e, cb) { this.events[e] = cb; } async sendFrame(id, data) { this.sent.push({ id, data }); } }
describe('IsoTpCanTransport', () => {
  it('sends DiveCAN padded single frames', async () => {
    const can = new Can(), t = new IsoTpCanTransport(can); await t.send([0x10, 0x03]);
    expect(can.sent[0].id).toBe(0x0D0A04FF); expect([...can.sent[0].data.slice(0, 4)]).toEqual([3, 0, 0x10, 3]);
  });
  it('reassembles a padded multi-frame reply and sends FC', () => {
    const can = new Can(), t = new IsoTpCanTransport(can), got = vi.fn(); t.on('message', got);
    t.processFrame({ id: 0x0D0AFF04, data: new Uint8Array([0x10, 10, 0, 0x62, 0xF1, 0, 1, 2]) });
    t.processFrame({ id: 0x0D0AFF04, data: new Uint8Array([0x21, 3, 4, 5, 6]) });
    expect(can.sent[0].id).toBe(0x0D0A04FF); expect([...got.mock.calls[0][0]]).toEqual([0x62, 0xF1, 0, 1, 2, 3, 4, 5, 6]);
  });
  it('honours flow control when transmitting multiple frames', async () => {
    const can = new Can(), t = new IsoTpCanTransport(can); const pending = t.send(new Uint8Array(12).fill(0x36));
    while (can.sent.length === 0) await new Promise(resolve => setTimeout(resolve, 0));
    t.processFrame({ id: 0x0D0AFF04, data: new Uint8Array([0x30, 0, 0]) }); await pending;
    expect(can.sent).toHaveLength(2); expect(can.sent[0].data[0]).toBe(0x10); expect(can.sent[1].data[0]).toBe(0x21);
  });
  it('keeps FE dialog and FF broadcast receive contexts independent', () => {
    const can = new Can(), dialog = new IsoTpCanTransport(can, { sourceAddress: 0xFE });
    const logs = new IsoTpCanTransport(can, { sourceAddress: 0xFF });
    const dialogMessage = vi.fn(), logMessage = vi.fn(); dialog.on('message', dialogMessage); logs.on('message', logMessage);
    dialog.processFrame({ id: 0x0D0AFE04, data: new Uint8Array([3, 0, 0x62, 1]) });
    logs.processFrame({ id: 0x0D0AFF04, data: new Uint8Array([4, 0, 0x2E, 0xA1, 0]) });
    expect([...dialogMessage.mock.calls[0][0]]).toEqual([0x62, 1]);
    expect([...logMessage.mock.calls[0][0]]).toEqual([0x2E, 0xA1, 0]);
    expect(dialogMessage).toHaveBeenCalledTimes(1); expect(logMessage).toHaveBeenCalledTimes(1);
  });
});
