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

  it('off() removes a registered listener', () => {
    const can = new Can(), t = new IsoTpCanTransport(can), fn = vi.fn();
    t.on('message', fn); t.off('message', fn);
    t.processFrame({ id: 0x0D0AFF04, data: new Uint8Array([3, 0, 0x62, 1]) });
    expect(fn).not.toHaveBeenCalled();
  });

  it('setTargetAddress updates the target used for addressing', () => {
    const can = new Can(), t = new IsoTpCanTransport(can);
    t.setTargetAddress(0x09);
    expect(t.targetAddress).toBe(0x09);
  });

  it('rejects the send when flow control returns a non-zero status', async () => {
    const can = new Can(), t = new IsoTpCanTransport(can);
    const pending = t.send(new Uint8Array(12).fill(0x36));
    while (can.sent.length === 0) await new Promise(r => setTimeout(r, 0));
    // FC status 1 (WAIT overflow / abort) -> the waiter rejects
    t.processFrame({ id: 0x0D0AFF04, data: new Uint8Array([0x31, 0, 0]) });
    await expect(pending).rejects.toThrow(/flow control status 1/);
  });

  it('honours a 0xF1-0xF9 STmin (mapped to a 1 ms inter-frame gap)', async () => {
    const can = new Can(), t = new IsoTpCanTransport(can);
    const pending = t.send(new Uint8Array(12).fill(0x36));
    while (can.sent.length === 0) await new Promise(r => setTimeout(r, 0));
    // stmin 0xF1 -> _stmin() returns 1 ms
    t.processFrame({ id: 0x0D0AFF04, data: new Uint8Array([0x30, 0, 0xF1]) });
    await pending;
    expect(can.sent).toHaveLength(2);
    expect(can.sent[1].data[0]).toBe(0x21);
  });

  it('rearms the receive timer between consecutive frames until the message completes', () => {
    const can = new Can(), t = new IsoTpCanTransport(can), got = vi.fn();
    t.on('message', got);
    // FF declares a 15-byte message (total field 16), carrying the first 5 bytes
    t.processFrame({ id: 0x0D0AFF04, data: new Uint8Array([0x10, 16, 0, 1, 2, 3, 4, 5]) });
    // CF seq 1: 7 more bytes -> 12/15, still incomplete -> _armRx branch
    t.processFrame({ id: 0x0D0AFF04, data: new Uint8Array([0x21, 6, 7, 8, 9, 10, 11, 12]) });
    expect(got).not.toHaveBeenCalled();
    // CF seq 2: final 3 bytes -> complete
    t.processFrame({ id: 0x0D0AFF04, data: new Uint8Array([0x22, 13, 14, 15, 0, 0, 0, 0]) });
    expect(got).toHaveBeenCalledTimes(1);
    expect([...got.mock.calls[0][0]]).toEqual([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
  });

  it('drops a consecutive frame whose sequence number does not match', () => {
    const can = new Can(), t = new IsoTpCanTransport(can), got = vi.fn(), err = vi.fn();
    t.on('message', got); t.on('error', err);
    t.processFrame({ id: 0x0D0AFF04, data: new Uint8Array([0x10, 16, 0, 1, 2, 3, 4, 5]) });
    // Wrong seq (expected 1, got 5) -> reset with a sequence-mismatch error
    t.processFrame({ id: 0x0D0AFF04, data: new Uint8Array([0x25, 6, 7, 8, 9, 10, 11, 12]) });
    expect(got).not.toHaveBeenCalled();
    expect(err.mock.calls[0][0].message).toMatch(/sequence mismatch/);
    expect(t.state).toBe('IDLE');
  });

  it('reset() clears any in-progress receive and reports idle state', () => {
    const can = new Can(), t = new IsoTpCanTransport(can);
    t.processFrame({ id: 0x0D0AFF04, data: new Uint8Array([0x10, 16, 0, 1, 2, 3, 4, 5]) });
    expect(t.state).toBe('RECEIVING');
    expect(t.isIdle).toBe(false);
    t.reset();
    expect(t.state).toBe('IDLE');
    expect(t.isIdle).toBe(true);
  });

  it('reset() rejects a send waiting for flow control and leaves the transport reusable', async () => {
    const can = new Can(), t = new IsoTpCanTransport(can);
    const pending = t.send(new Uint8Array(12).fill(0x36));
    while (can.sent.length === 0) await new Promise(r => setTimeout(r, 0));

    t.reset();

    await expect(pending).rejects.toThrow(/transport reset/);
    expect(t.isIdle).toBe(true);
    await t.send([0x10, 0x03]);
    expect(can.sent.at(-1).data[0]).toBe(3);
  });

  it('ignores frames from a non-target source once a target is fixed', () => {
    const can = new Can(), t = new IsoTpCanTransport(can, { targetAddress: 0x04 }), got = vi.fn();
    t.on('message', got);
    // source 0x05 != target 0x04 -> single frame ignored
    t.processFrame({ id: 0x0D0AFF05, data: new Uint8Array([3, 0, 0x62, 1]) });
    expect(got).not.toHaveBeenCalled();
  });
});
