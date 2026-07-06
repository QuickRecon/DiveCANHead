import { describe, it, expect, beforeEach } from 'vitest';
import { BLEConnection } from './BLEConnection.js';
import { MockBluetoothRemoteGATTCharacteristic } from '../../tests/mocks/MockBLE.js';

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
});
